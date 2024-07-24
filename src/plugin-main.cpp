/*
Dedicated Output Plugin
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

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

typedef struct {
	// Filter source
	obs_source_t *source;
	// User choosed encoder
	obs_encoder_t *video_encoder;
	obs_encoder_t *audio_encoder;
	
	obs_view_t *view;
	video_t *video_output;
	audio_t *audio_output;
	obs_output_t *stream_output;
	obs_service_t *service;
} filter_t;


const char *get_name(void *)
{
	return "Dedicated Output";
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

bool encoder_changed(void *, obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	obs_properties_remove_by_name(props, "encoder_group");

	const char *encoder_id = obs_data_get_string(settings, "encoder");;
	obs_properties_t *encoder_props = obs_get_encoder_properties(encoder_id);
	if (encoder_props) {
		obs_properties_add_group(props, "encoder_group", obs_encoder_get_display_name(encoder_id), OBS_GROUP_NORMAL, encoder_props);
	}

	return true;
}

obs_properties_t *get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	// "Stream" group
	obs_properties_t *stream_group = obs_properties_create();
	obs_properties_add_text(stream_group, "server", obs_module_text("Server"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(stream_group, "key", obs_module_text("Key"), OBS_TEXT_PASSWORD);
	obs_properties_add_group(props, "stream", obs_module_text("Stream"), OBS_GROUP_NORMAL, stream_group);

	// "Encoder" prop
	obs_property_t *encoder_prop = obs_properties_add_list(
		props, "encoder", obs_module_text("Encoder"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	const char *encoder_id = NULL;
	size_t i = 0;
	while (obs_enum_encoder_types(i++, &encoder_id)) {
		if (obs_get_encoder_type(encoder_id) != OBS_ENCODER_VIDEO) {
			continue;
		}

		const uint32_t caps = obs_get_encoder_caps(encoder_id);
		if ((caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)) != 0) {
			continue;
		}

		const char *name = obs_encoder_get_display_name(encoder_id);
		obs_property_list_add_string(encoder_prop, name, encoder_id);
	}
	obs_property_set_modified_callback2(encoder_prop, encoder_changed, data);

	// "Encoder" group (Initially empty)
	obs_properties_t *encoder_group = obs_properties_create();
	obs_properties_add_group(props, "encoder_group", obs_module_text("Encoder"), OBS_GROUP_NORMAL, encoder_group);

	return props;
}

void get_defaults(obs_data_t *defaults)
{
	config_t *config = obs_frontend_get_profile_config();
	const char *mode = config_get_string(config, "Output", "Mode");
	bool advanced_out = strcmp(mode, "Advanced") == 0 || strcmp(mode, "advanced") == 0;

	const char *encoder_id;
	if (advanced_out) {
		encoder_id = config_get_string(config, "AdvOut", "Encoder");
	} else {
		encoder_id = config_get_string(config, "SimpleOutput", "StreamEncoder");
	}
	obs_data_set_default_string(defaults, "encoder", encoder_id);
}

void *create(obs_data_t *settings, obs_source_t *source)
{
	blog(LOG_INFO, "[dedicated-output] Filter creating");
	
	auto filter = (filter_t *)bzalloc(sizeof(filter_t));
	filter->source = source;

	update(filter, settings);

	blog(LOG_INFO, "[dedicated-output] Filter created");
	return filter;
}

void destroy(void *data)
{
	blog(LOG_INFO, "[dedicated-output] Filter destroying");
	
	auto filter = (filter_t *)data;
	bfree(filter);

	blog(LOG_INFO, "[dedicated-output] Filter destroyed");
}


bool audio_input_callback(void *param, uint64_t start_ts_in, uint64_t, uint64_t *out_ts, uint32_t mixers,
	struct audio_output_data *mixes)
{
	auto filter = (filter_t *)param;
	if (obs_source_removed(filter->source)) {
		*out_ts = start_ts_in;
		return true;
	}

	auto audio_source = obs_filter_get_parent(filter->source);
	if (!audio_source || obs_source_removed(audio_source)) {
		*out_ts = start_ts_in;
		return true;
	}

	const uint64_t source_ts = obs_source_get_audio_timestamp(audio_source);
	if (!source_ts) {
		*out_ts = start_ts_in;
		return true;
	}

	if (obs_source_audio_pending(audio_source)) {
		return false;
	}

	struct obs_source_audio_mix audio;
	obs_source_get_audio_mix(audio_source, &audio);

	auto channels = audio_output_get_channels(filter->audio_output);
	for (size_t mix_idx = 0; mix_idx < MAX_AUDIO_MIXES; mix_idx++) {
		if ((mixers & (1 << mix_idx)) == 0) {
			continue;
		}
		for (size_t ch = 0; ch < channels; ch++) {
			float *out = mixes[mix_idx].data[ch];
			float *in = audio.output[0].data[ch];
			if (!in)
				continue;
			for (size_t i = 0; i < AUDIO_OUTPUT_FRAMES; i++) {
				out[i] += (in[i] > 1.0f) ? 1.0f : (in[i] < -1.0f) ? -1.0f : in[i];
			}
		}
	}

	*out_ts = source_ts;

	return true;
}

static void update_video_encoder(filter_t *filter, obs_data_t *settings)
{
	if (obs_encoder_video(filter->video_encoder) != filter->video_output) {
		if (obs_encoder_active(filter->video_encoder)) {
			obs_encoder_release(filter->video_encoder);
		}

		const char *encoder_id = obs_encoder_get_id(filter->video_encoder);
		filter->video_encoder = obs_video_encoder_create(encoder_id, obs_source_get_name(filter->source), settings, NULL);
		obs_encoder_set_scaled_size(filter->video_encoder, 0, 0);
		obs_encoder_set_video(filter->video_encoder, filter->video_output);
	}

	if (filter->stream_output && obs_output_get_video_encoder(filter->stream_output) != filter->video_encoder) {
		obs_output_set_video_encoder(filter->stream_output, filter->video_encoder);
	}
}

static void update_audio_encoder(filter_t *filter, obs_data_t *settings)
{
	if (obs_encoder_audio(filter->audio_encoder) != filter->audio_output) {
		if (obs_encoder_active(filter->audio_encoder)) {
			obs_encoder_release(filter->audio_encoder);
		}

		filter->audio_encoder = obs_audio_encoder_create("ffmpeg_aac", obs_source_get_name(filter->source), NULL, 0, NULL);
		obs_encoder_set_audio(filter->audio_encoder, filter->audio_output);
	}

	if (filter->stream_output && obs_output_get_audio_encoder(filter->stream_output, 0) != filter->audio_encoder) {
		obs_output_set_audio_encoder(filter->stream_output, filter->audio_encoder, 0);
	}
}


#define FTL_PROTOCOL "ftl"
#define RTMP_PROTOCOL "rtmp"

void start_output(filter_t* filter, obs_data_t* settings)
{
	obs_source_t *parent = obs_filter_get_parent(filter->source);
	if (!parent) {
		return;
	}

	// Open video output
	struct obs_video_info ovi = {0};
	obs_get_video_info(&ovi);

	uint32_t width = obs_source_get_width(parent);
	width += (width & 1);

	uint32_t height = obs_source_get_height(parent);
	height += (height & 1);

	ovi.base_width = width;
	ovi.base_height = height;
	ovi.output_width = width;
	ovi.output_height = height;

	if (!filter->view) {
		filter->view = obs_view_create();
	}
	obs_view_set_source(filter->view, 0, parent);
	filter->video_output = obs_view_add2(filter->view, &ovi);

	// Open audio aoutput
	struct audio_output_info oi = {0};

	oi.name = obs_source_get_name(filter->source);
	oi.speakers = SPEAKERS_STEREO;
	oi.samples_per_sec = audio_output_get_sample_rate(obs_get_audio());
	oi.format = AUDIO_FORMAT_FLOAT_PLANAR;
	oi.input_param = filter;
	oi.input_callback = audio_input_callback;

	audio_output_open(&filter->audio_output, &oi);

	if (!filter->service) {
		// We always use "rtmp_custom"
		filter->service = obs_service_create("rtmp_custom", obs_source_get_name(filter->source), settings, NULL);
	} else {
		obs_service_update(filter->service, settings);
	}
	obs_service_apply_encoder_settings(filter->service, settings, NULL);

	const char *type = obs_service_get_preferred_output_type(filter->service);

	if (!type) {
		type = "rtmp_output";
		const char *url = obs_service_get_connect_info(filter->service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
		if (url != NULL && !strncmp(url, FTL_PROTOCOL, strlen(FTL_PROTOCOL))) {
			type = "ftl_output";
		} else if (url != NULL && strncmp(url, RTMP_PROTOCOL, strlen(RTMP_PROTOCOL))) {
			type = "ffmpeg_mpegts_muxer";
		}
	}

	if (!filter->stream_output) {
		filter->stream_output = obs_output_create(type, obs_source_get_name(filter->source), settings, NULL);
	} else {
		obs_output_update(filter->stream_output, settings);
	}
	obs_output_set_service(filter->stream_output, filter->service);

	update_video_encoder(filter, settings);
	update_audio_encoder(filter, settings);
}

void update(void *data, obs_data_t *settings)
{
	auto filter = (filter_t *)data;

	


}

obs_source_info create_dedicated_output_filter_info()
{
	obs_source_info filter_info = {};

	filter_info.id = "dedicated_output";
	filter_info.type = OBS_SOURCE_TYPE_FILTER;
	filter_info.output_flags = OBS_SOURCE_VIDEO;

	filter_info.get_name = get_name;
	filter_info.get_properties = get_properties;
	filter_info.get_defaults = get_defaults;

	filter_info.create = create;
	filter_info.destroy = destroy;
	filter_info.update = update;

	return filter_info;
}

struct obs_source_info dedicated_output_filter_info;

bool obs_module_load(void)
{
	dedicated_output_filter_info = create_dedicated_output_filter_info();
	obs_register_source(&dedicated_output_filter_info);

	obs_log(LOG_INFO, "[dedicated-output] Plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "[dedicated-output] Plugin unloaded");
}
