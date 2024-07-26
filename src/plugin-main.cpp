/*
Source Output Plugin
Copyright (C) 2024 OPENSPHERE Inc. info@opensphere.co.jp

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/deque.h>
#include <util/threading.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define NO_AUDIO false

struct filter_t {
	bool output_active;

	// Filter source
	obs_source_t *source;
	obs_weak_source_t *audio_source;

	// User choosed encoder
	obs_encoder_t *video_encoder;
	obs_encoder_t *audio_encoder;
	
	obs_view_t *view;
	video_t *video_output;
	audio_t *audio_output;
	obs_output_t *stream_output;
	obs_service_t *service;

	// Audio context
	bool audio_enabled;
	deque audio_buffer;
	size_t audio_buffer_frames;
	pthread_mutex_t audio_buffer_mutex;
	uint8_t* audio_conv_buffer;
	size_t audio_conv_buffer_size;
};

struct audio_buffer_chunk_header_t {
	size_t data_idx[MAX_AUDIO_CHANNELS]; // Zero means unused channel
	uint32_t frames;
	uint64_t timestamp;
	size_t offset;
	size_t channels;
};

void audio_capture_callback(void *data, obs_source_t*, const audio_data *audio_data, bool muted)
{
	auto filter = (filter_t *)data;

	if (NO_AUDIO || !filter->output_active || !audio_data->frames || muted) {
		filter->audio_enabled = false;
		return;
	}

	pthread_mutex_lock(&filter->audio_buffer_mutex);
	{	
		// Compute header
		audio_buffer_chunk_header_t header = {0};
		header.frames = audio_data->frames;
		header.timestamp = audio_data->timestamp;
		for (int ch = 0; ch < MAX_AUDIO_CHANNELS; ch++) {
			if (!audio_data->data[ch]) {
				continue;
			}
			header.data_idx[ch] = sizeof(audio_buffer_chunk_header_t) + header.channels * audio_data->frames * 4;
			header.channels++;
		}

		// Push audio data to buffer
		deque_push_back(&filter->audio_buffer, &header, sizeof(audio_buffer_chunk_header_t));
		for (int ch = 0; ch < MAX_AUDIO_CHANNELS; ch++) {
			if (!audio_data->data[ch]) {
				continue;
			}
			deque_push_back(&filter->audio_buffer, audio_data->data[ch], audio_data->frames * 4);
		}

		// Ensure allocation of audio_conv_buffer
		size_t data_size = sizeof(audio_buffer_chunk_header_t) + header.channels * audio_data->frames * 4;
		if (data_size > filter->audio_conv_buffer_size) {
			obs_log(LOG_INFO, "Expand audio_conv_buffer from %zu to %zu bytes", filter->audio_conv_buffer_size, data_size);
			filter->audio_conv_buffer = (uint8_t*)brealloc(filter->audio_conv_buffer, data_size);
			filter->audio_conv_buffer_size = data_size;
		}

		// Increment buffer usage
		filter->audio_buffer_frames += audio_data->frames;
		// Enable audio
		filter->audio_enabled = true;
	}
	pthread_mutex_unlock(&filter->audio_buffer_mutex);

	return;
}

bool audio_input_callback(void *param, uint64_t start_ts_in, uint64_t, uint64_t *out_ts, uint32_t mixers,
	audio_output_data *mixes)
{
	auto filter = (filter_t *)param;

	obs_audio_info audio_info;
	if (!filter->audio_enabled || !filter->output_active || !obs_get_audio_info(&audio_info)) {
		// Silence
		*out_ts = start_ts_in;
		return true;
	}

	// TODO: Shorten the critical section to reduce audio delay
	pthread_mutex_lock(&filter->audio_buffer_mutex);
	{
		if (filter->audio_buffer_frames < AUDIO_OUTPUT_FRAMES) {
			// Wait until enough frames are receved.
			pthread_mutex_unlock(&filter->audio_buffer_mutex);
			return false;
		}

		size_t max_frames = AUDIO_OUTPUT_FRAMES;

		while (max_frames > 0 && filter->audio_buffer_frames) {
			// Peek header of first chunk
			deque_peek_front(&filter->audio_buffer, filter->audio_conv_buffer, sizeof(audio_buffer_chunk_header_t));
			auto header = (audio_buffer_chunk_header_t*)filter->audio_conv_buffer;
			size_t data_size = sizeof(audio_buffer_chunk_header_t) + header->channels * header->frames * 4;

			// Read chunk data
			deque_peek_front(&filter->audio_buffer, filter->audio_conv_buffer, data_size);		

			if (max_frames == AUDIO_OUTPUT_FRAMES) {
				// FIXME: No timestamp modifications
				*out_ts = start_ts_in;
				/*
				// Update timestamp with first chunk frame (+offset)
				*out_ts = header->timestamp + audio_frames_to_ns(audio_info.samples_per_sec, header->offset);
				*/
			}

			size_t chunk_frames = header->frames - header->offset;
			size_t frames = (chunk_frames > max_frames) ? max_frames : chunk_frames;
			size_t out_offset = AUDIO_OUTPUT_FRAMES - max_frames;

			for (size_t mix_idx = 0; mix_idx < MAX_AUDIO_MIXES; mix_idx++) {
				if ((mixers & (1 << mix_idx)) == 0) {
					continue;
				}
				for (size_t ch = 0; ch < MAX_AUDIO_CHANNELS; ch++) {
					auto out = mixes[mix_idx].data[ch] + out_offset;
					if (!header->data_idx[ch]) {
						continue;
					}
					auto in = (float*)(filter->audio_conv_buffer + header->data_idx[ch]) + header->offset;

					for (size_t i = 0; i < frames; i++) {
						*out += *(in++);
						if (*out > 1.0f) {
							*out = 1.0f;
						} else if (*out < -1.0f) {
							*out = -1.0f;
						}
						out++;
					}
				}
			}

			if (frames == chunk_frames) {
				// Remove fulfilled chunk from buffer
				deque_pop_front(&filter->audio_buffer, NULL, data_size);
			} else {
				// Chunk frames are remaining -> modify chunk header
				header->offset += frames;
				deque_place(&filter->audio_buffer, 0, header, sizeof(audio_buffer_chunk_header_t));
			}

			max_frames -= frames;

			// Decrement buffer usage
			filter->audio_buffer_frames -= frames;
		}
	}	
	pthread_mutex_unlock(&filter->audio_buffer_mutex);

	return true;
}

void stop_output(filter_t* filter)
{
	obs_source_t* parent = obs_filter_get_parent(filter->source);

	if (filter->output_active) {
		obs_source_dec_showing(parent);
		obs_output_stop(filter->stream_output);
	}
		
	if (filter->audio_encoder) {
		obs_encoder_release(filter->audio_encoder);
		filter->audio_encoder = NULL;
	}

	if (filter->video_encoder) {
		obs_encoder_release(filter->video_encoder);
		filter->video_encoder = NULL;
	}
	
	if (filter->audio_output) {
		audio_output_close(filter->audio_output);
		filter->audio_output = NULL;
	}

	if (filter->audio_source) {
		obs_source_remove_audio_capture_callback(parent, audio_capture_callback, filter);
		obs_weak_source_release(filter->audio_source);
		filter->audio_source = NULL;
		filter->audio_enabled = false;
	}

	if (filter->view) {
		obs_view_set_source(filter->view, 0, NULL);
		obs_view_remove(filter->view);
		obs_view_destroy(filter->view);
		filter->view = NULL;
	}
	
	if (filter->stream_output) {
		obs_output_release(filter->stream_output);
		filter->stream_output = NULL;
	}
	
	if (filter->service) {
		obs_service_release(filter->service);
		filter->service = NULL;
	}
	
	filter->audio_buffer_frames = 0;
	deque_free(&filter->audio_buffer);

	if (filter->output_active) {
		filter->output_active = false;
		obs_log(LOG_INFO, "Stopping stream output succeeded.");
	}
}


#define FTL_PROTOCOL "ftl"
#define RTMP_PROTOCOL "rtmp"

void start_output(filter_t* filter, obs_data_t* settings)
{
	// Force release references
	stop_output(filter);

	// Abort when obs initializing or source disabled.
	if (!obs_initialized() || !obs_source_enabled(filter->source)) {
		return;
	}

	// Retrieve filter source
	auto parent = obs_filter_get_parent(filter->source);
	if (!parent) {
		obs_log(LOG_ERROR, "Filter source not found.");
		return;
	}

	obs_video_info ovi = {0};
	if (!obs_get_video_info(&ovi)) {
		// Abort when no video situation
		return;
	}

	// Round up to a multiple of 2
	auto width = obs_source_get_width(parent);
	width += (width & 1);
	// Round up to a multiple of 2
	auto height = obs_source_get_height(parent);
	height += (height & 1);

	ovi.base_width = width;
	ovi.base_height = height;
	ovi.output_width = width;
	ovi.output_height = height;

	if (width == 0 || height == 0 || ovi.fps_den == 0 || ovi.fps_num == 0) {
		// Abort when invalid video parameters situation
		return;
	}

	// Create service - We always use "rtmp_custom" as service
	filter->service = obs_service_create("rtmp_custom", obs_source_get_name(filter->source), settings, NULL);
	if (!filter->service) {
		obs_log(LOG_ERROR, "Service creation failed.");
		return;
	}
	obs_service_apply_encoder_settings(filter->service, settings, NULL);

	// Determine output type
	auto type = obs_service_get_preferred_output_type(filter->service);
	if (!type) {
		type = "rtmp_output";
		auto url = obs_service_get_connect_info(filter->service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
		if (url != NULL && !strncmp(url, FTL_PROTOCOL, strlen(FTL_PROTOCOL))) {
			type = "ftl_output";
		} else if (url != NULL && strncmp(url, RTMP_PROTOCOL, strlen(RTMP_PROTOCOL))) {
			type = "ffmpeg_mpegts_muxer";
		}
	}

	// Create stream output
	filter->stream_output = obs_output_create(type, obs_source_get_name(filter->source), settings, NULL);
	if (!filter->stream_output) {
		obs_log(LOG_ERROR, "Stream output creation failed.");
		return;
	}
	obs_output_set_service(filter->stream_output, filter->service);

	// Open video output
	// Create view and associate it with filter source
	filter->view = obs_view_create();

	obs_view_set_source(filter->view, 0, parent);
	filter->video_output = obs_view_add2(filter->view, &ovi);
	if (!filter->video_output) {
		obs_log(LOG_ERROR, "Video output association failed.");
		return;
	}

	// Retrieve audio source
	filter->audio_source = obs_source_get_weak_source(parent);
	if (!filter->audio_source) {
		obs_log(LOG_ERROR, "Audio source retrieval failed.");
		return;
	}

	// Register audio capture callback (It forwards audio to output)
	obs_source_add_audio_capture_callback(parent, audio_capture_callback, filter);

	// Open audio output (Audio will be captured from filter source via audio_input_callback)
	audio_output_info oi = {0};

	oi.name = obs_source_get_name(filter->source);
	oi.speakers = SPEAKERS_STEREO;
	oi.samples_per_sec = audio_output_get_sample_rate(obs_get_audio());
	oi.format = AUDIO_FORMAT_FLOAT_PLANAR;
	oi.input_param = filter;
	oi.input_callback = audio_input_callback;

	if (audio_output_open(&filter->audio_output, &oi) < 0) {
		obs_log(LOG_ERROR, "Opening audio output failed.");
		return;
	}

	// Setup video encoder
	auto encoder_id = obs_data_get_string(settings, "encoder");
	filter->video_encoder = obs_video_encoder_create(encoder_id, obs_source_get_name(filter->source), settings, NULL);
	if (!filter->video_encoder) {
		obs_log(LOG_ERROR, "Video encoder creation failed.");
		return;
	}
	obs_encoder_set_scaled_size(filter->video_encoder, 0, 0);
	obs_encoder_set_video(filter->video_encoder, filter->video_output);
	obs_output_set_video_encoder(filter->stream_output, filter->video_encoder);

	// Setup audo encoder
	filter->audio_encoder = obs_audio_encoder_create("ffmpeg_aac", obs_source_get_name(filter->source), NULL, 0, NULL);
	if (!filter->audio_encoder) {
		obs_log(LOG_ERROR, "Audio encoder creation failed.");
		return;
	}
	obs_encoder_set_audio(filter->audio_encoder, filter->audio_output);
	obs_output_set_audio_encoder(filter->stream_output, filter->audio_encoder, 0);

	// Start stream output
	if (obs_output_start(filter->stream_output)) {
		filter->output_active = true;
		obs_source_inc_showing(obs_filter_get_parent(filter->source));
		obs_log(LOG_INFO, "Starting stream output succeeded.");
	} else {
		obs_log(LOG_ERROR, "Starting stream output failed.");
	}
}

void get_defaults(obs_data_t *defaults)
{
	auto config = obs_frontend_get_profile_config();
	auto mode = config_get_string(config, "Output", "Mode");
	bool advanced_out = strcmp(mode, "Advanced") == 0 || strcmp(mode, "advanced") == 0;

	const char *encoder_id;
	if (advanced_out) {
		encoder_id = config_get_string(config, "AdvOut", "Encoder");
	} else {
		encoder_id = config_get_string(config, "SimpleOutput", "StreamEncoder");
	}
	obs_data_set_default_string(defaults, "encoder", encoder_id);
}

void update(void *data, obs_data_t *settings)
{
	obs_log(LOG_INFO, "Filter updating");

	auto filter = (filter_t *)data;

	if (filter->output_active) {
		// Stop output before
		stop_output(filter);
	}

	auto server = obs_data_get_string(settings, "server");
	if (server && strlen(server)) {
		// Start output again
		start_output(filter, settings);
	}

	obs_log(LOG_INFO, "Filter updated");
}

bool encoder_available(const char *encoder)
{
	const char *val;
	int i = 0;

	while (obs_enum_encoder_types(i++, &val)) {
		if (strcmp(val, encoder) == 0) {
			return true;
		}
	}

	return false;
}

void add_apply_button(obs_properties_t *props) {
	obs_properties_add_button(
		props, "apply",
		obs_module_text("Apply"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			auto filter = (filter_t *)data;
			auto settings = obs_source_get_settings(filter->source);
			update(filter, settings);
			obs_data_release(settings);
			return true;
		});
}

bool encoder_changed(void *, obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	obs_properties_remove_by_name(props, "encoder_group");
	obs_properties_remove_by_name(props, "apply");

	const char *encoder_id = obs_data_get_string(settings, "encoder");;
	obs_properties_t *encoder_props = obs_get_encoder_properties(encoder_id);
	if (encoder_props) {
		obs_properties_add_group(props, "encoder_group", obs_encoder_get_display_name(encoder_id), OBS_GROUP_NORMAL, encoder_props);
	}

	add_apply_button(props);

	return true;
}

obs_properties_t *get_properties(void *data)
{
	auto props = obs_properties_create();
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	// "Stream" group
	auto stream_group = obs_properties_create();
	obs_properties_add_text(stream_group, "server", obs_module_text("Server"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(stream_group, "key", obs_module_text("Key"), OBS_TEXT_PASSWORD);
	obs_properties_add_group(props, "stream", obs_module_text("Stream"), OBS_GROUP_NORMAL, stream_group);

	// "Encoder" prop
	auto encoder_prop = obs_properties_add_list(
		props, "encoder", obs_module_text("Encoder"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	const char *encoder_id = NULL;
	size_t i = 0;
	while (obs_enum_encoder_types(i++, &encoder_id)) {
		if (obs_get_encoder_type(encoder_id) != OBS_ENCODER_VIDEO) {
			continue;
		}

		auto caps = obs_get_encoder_caps(encoder_id);
		if ((caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)) != 0) {
			continue;
		}

		auto name = obs_encoder_get_display_name(encoder_id);
		obs_property_list_add_string(encoder_prop, name, encoder_id);
	}
	obs_property_set_modified_callback2(encoder_prop, encoder_changed, data);

	// "Encoder" group (Initially empty)
	auto encoder_group = obs_properties_create();
	obs_properties_add_group(props, "encoder_group", obs_module_text("Encoder"), OBS_GROUP_NORMAL, encoder_group);

	add_apply_button(props);

	return props;
}

void *create(obs_data_t *settings, obs_source_t *source)
{
	obs_log(LOG_INFO, "Filter creating");
	
	auto filter = (filter_t *)bzalloc(sizeof(filter_t));
	pthread_mutex_init(&filter->audio_buffer_mutex, NULL);

	filter->source = source;

	update(filter, settings);

	obs_log(LOG_INFO, "Filter created");
	return filter;
}

void destroy(void *data)
{
	obs_log(LOG_INFO, "Filter destroying");
	
	auto filter = (filter_t *)data;

	stop_output(filter);
	pthread_mutex_destroy(&filter->audio_buffer_mutex);
	bfree(filter->audio_conv_buffer);
	bfree(filter);

	obs_log(LOG_INFO, "Filter destroyed");
}

void video_tick(void *data, float)
{
	auto filter = (filter_t *)data;

	if (filter->output_active && !obs_source_enabled(filter->source)) {
		stop_output(filter);
	} else if (!filter->output_active && obs_source_enabled(filter->source)) {
		auto settings = obs_source_get_settings(filter->source);
		auto server = obs_data_get_string(settings, "server");
		if (server && strlen(server)) {
			start_output(filter, settings);
		}
		obs_data_release(settings);
	}
}

const char *get_name(void *)
{
	return "Source output";
}

obs_source_info create_filter_info()
{
	obs_source_info filter_info = {0};

	filter_info.id = "source_output";
	filter_info.type = OBS_SOURCE_TYPE_FILTER;
	filter_info.output_flags = OBS_SOURCE_VIDEO;

	filter_info.get_name = get_name;
	filter_info.get_properties = get_properties;
	filter_info.get_defaults = get_defaults;

	filter_info.create = create;
	filter_info.destroy = destroy;
	filter_info.update = update;

	filter_info.video_tick = video_tick;

	return filter_info;
}


obs_source_info filter_info;

bool obs_module_load(void)
{
	filter_info = create_filter_info();
	obs_register_source(&filter_info);

	obs_log(LOG_INFO, "Plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "Plugin unloaded");
}
