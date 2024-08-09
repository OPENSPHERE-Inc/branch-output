/*
Branch Output Plugin
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
#include <util/platform.h>
#include "plugin-main.hpp"


OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

void stop_output(filter_t *filter)
{
    obs_source_t *parent = obs_filter_get_parent(filter->source);
    filter->connect_attempting_at = 0;

    if (filter->stream_output) {
        if (filter->output_active) {
            obs_source_dec_showing(parent);
            obs_output_stop(filter->stream_output);
        }

        obs_output_release(filter->stream_output);
        filter->stream_output = NULL;
    }

    if (filter->service) {
        obs_service_release(filter->service);
        filter->service = NULL;
    }

    if (filter->audio_encoder) {
        obs_encoder_release(filter->audio_encoder);
        filter->audio_encoder = NULL;
    }

    if (filter->video_encoder) {
        obs_encoder_release(filter->video_encoder);
        filter->video_encoder = NULL;
    }

    if (filter->audio_source_type == AUDIO_SOURCE_TYPE_CAPTURE) {
        if (filter->audio_source) {
            auto source = obs_weak_source_get_source(filter->audio_source);
            if (source) {
                obs_source_remove_audio_capture_callback(source, audio_capture_callback, filter);
                obs_source_release(source);
            }
            obs_weak_source_release(filter->audio_source);
            filter->audio_source = NULL;
        }

    } else if (filter->audio_source_type == AUDIO_SOURCE_TYPE_MASTER) {
        obs_remove_raw_audio_callback(filter->audio_mix_idx, master_audio_callback, filter);
    }
    filter->audio_source_type = AUDIO_SOURCE_TYPE_SILENCE;

    if (filter->audio_output) {
        audio_output_close(filter->audio_output);
        filter->audio_output = NULL;
    }

    if (filter->view) {
        obs_view_set_source(filter->view, 0, NULL);
        obs_view_remove(filter->view);
        obs_view_destroy(filter->view);
        filter->view = NULL;
    }

    filter->audio_buffer_frames = 0;
    filter->audio_skip = 0;
    deque_free(&filter->audio_buffer);

    if (filter->output_active) {
        filter->output_active = false;
        obs_log(LOG_INFO, "%s: Stopping stream output succeeded", obs_source_get_name(filter->source));
    }
}

#define FTL_PROTOCOL "ftl"
#define RTMP_PROTOCOL "rtmp"

void start_output(filter_t *filter, obs_data_t *settings)
{
    // Force release references
    stop_output(filter);

    // Abort when obs initializing or filter disabled.
    if (!obs_initialized() || !obs_source_enabled(filter->source)) {
        return;
    }

    // Retrieve filter source
    auto parent = obs_filter_get_parent(filter->source);
    if (!parent) {
        obs_log(LOG_ERROR, "%s: Filter source not found", obs_source_get_name(filter->source));
        return;
    }

    obs_video_info ovi = {0};
    if (!obs_get_video_info(&ovi)) {
        // Abort when no video situation
        return;
    }

    // Round up to a multiple of 2
    filter->width = obs_source_get_width(parent);
    filter->width += (filter->width & 1);
    // Round up to a multiple of 2
    filter->height = obs_source_get_height(parent);
    filter->height += (filter->height & 1);

    ovi.base_width = filter->width;
    ovi.base_height = filter->height;
    ovi.output_width = filter->width;
    ovi.output_height = filter->height;

    if (filter->width == 0 || filter->height == 0 || ovi.fps_den == 0 || ovi.fps_num == 0) {
        // Abort when invalid video parameters situation
        return;
    }

    // Update active revision with stored settings.
    filter->active_settings_rev = filter->stored_settings_rev;

    // Create service - We always use "rtmp_custom" as service
    filter->service = obs_service_create("rtmp_custom", obs_source_get_name(filter->source), settings, NULL);
    if (!filter->service) {
        obs_log(LOG_ERROR, "%s: Service creation failed", obs_source_get_name(filter->source));
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
        obs_log(LOG_ERROR, "%s: Stream output creation failed", obs_source_get_name(filter->source));
        return;
    }
    obs_output_set_reconnect_settings(filter->stream_output, OUTPUT_MAX_RETRIES, OUTPUT_RETRY_DELAY_SECS);
    obs_output_set_service(filter->stream_output, filter->service);
    filter->connect_attempting_at = os_gettime_ns();

    // Open video output
    // Create view and associate it with filter source
    filter->view = obs_view_create();

    obs_view_set_source(filter->view, 0, parent);
    filter->video_output = obs_view_add2(filter->view, &ovi);
    if (!filter->video_output) {
        obs_log(LOG_ERROR, "%s: Video output association failed", obs_source_get_name(filter->source));
        return;
    }

    // Retrieve audio source
    filter->audio_source_type = AUDIO_SOURCE_TYPE_SILENCE;
    filter->audio_source = NULL;
    filter->audio_mix_idx = 0;
    filter->audio_channels = (speaker_layout)audio_output_get_channels(obs_get_audio());
    filter->samples_per_sec = audio_output_get_sample_rate(obs_get_audio());
    filter->audio_buffer_frames = 0;
    filter->audio_skip = 0;

    if (obs_data_get_bool(settings, "custom_audio_source")) {
        // Apply custom audio source
        auto source_uuid = obs_data_get_string(settings, "audio_source");

        if (strlen(source_uuid) && strcmp(source_uuid, "no_audio")) {
            if (!strncmp(source_uuid, "master_track_", strlen("master_track_"))) {
                // Use master audio track
                size_t trackNo = 0;
                sscanf(source_uuid, "master_track_%zu", &trackNo);
                obs_log(LOG_INFO, "%s: Use master track %zu", obs_source_get_name(filter->source), trackNo);

                if (trackNo <= MAX_AUDIO_MIXES) {
                    filter->audio_mix_idx = trackNo - 1;

                    audio_convert_info conv = {0};
                    conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
                    conv.samples_per_sec = filter->samples_per_sec;
                    conv.speakers = filter->audio_channels;
                    conv.allow_clipping = true;

                    filter->audio_source_type = AUDIO_SOURCE_TYPE_MASTER;

                    obs_add_raw_audio_callback(filter->audio_mix_idx, &conv, master_audio_callback, filter);
                }

            } else {
                auto source = obs_get_source_by_uuid(source_uuid);
                if (source) {
                    // Use custom audio source
                    obs_log(
                        LOG_INFO, "%s: Use %s as an audio source", obs_source_get_name(filter->source),
                        obs_source_get_name(source)
                    );
                    filter->audio_source = obs_source_get_weak_source(source);
                    filter->audio_source_type = AUDIO_SOURCE_TYPE_CAPTURE;

                    if (!filter->audio_source) {
                        obs_log(LOG_ERROR, "%s: Audio source retrieval failed", obs_source_get_name(filter->source));
                        obs_source_release(source);
                        return;
                    }

                    // Register audio capture callback (It forwards audio to output)
                    obs_source_add_audio_capture_callback(source, audio_capture_callback, filter);

                    obs_source_release(source);
                }
            }
        }

    } else {
        // Use filter's audio
        obs_log(LOG_INFO, "%s: Use filter audio as an audio source", obs_source_get_name(filter->source));
        filter->audio_source_type = AUDIO_SOURCE_TYPE_FILTER;
    }

    if (filter->audio_source_type == AUDIO_SOURCE_TYPE_SILENCE) {
        obs_log(LOG_INFO, "%s: Audio is disabled", obs_source_get_name(filter->source));
    }

    // Open audio output (Audio will be captured from filter source via audio_input_callback)
    audio_output_info oi = {0};

    oi.name = obs_source_get_name(filter->source);
    oi.speakers = filter->audio_channels;
    oi.samples_per_sec = filter->samples_per_sec;
    oi.format = AUDIO_FORMAT_FLOAT_PLANAR;
    oi.input_param = filter;
    oi.input_callback = audio_input_callback;

    if (audio_output_open(&filter->audio_output, &oi) < 0) {
        obs_log(LOG_ERROR, "%s: Opening audio output failed", obs_source_get_name(filter->source));
        return;
    }

    // Setup video encoder
    auto video_encoder_id = obs_data_get_string(settings, "video_encoder");
    filter->video_encoder =
        obs_video_encoder_create(video_encoder_id, obs_source_get_name(filter->source), settings, NULL);
    if (!filter->video_encoder) {
        obs_log(LOG_ERROR, "%s: Video encoder creation failed", obs_source_get_name(filter->source));
        return;
    }
    obs_encoder_set_scaled_size(filter->video_encoder, 0, 0);
    obs_encoder_set_video(filter->video_encoder, filter->video_output);
    obs_output_set_video_encoder(filter->stream_output, filter->video_encoder);

    // Setup audo encoder
    auto audio_encoder_id = obs_data_get_string(settings, "audio_encoder");
    auto audio_bitrate = obs_data_get_int(settings, "audio_bitrate");
    auto audio_encoder_settings = obs_encoder_defaults(audio_encoder_id);
    obs_data_set_int(audio_encoder_settings, "bitrate", audio_bitrate);

    // Use track 0 only.
    filter->audio_encoder = obs_audio_encoder_create(
        audio_encoder_id, obs_source_get_name(filter->source), audio_encoder_settings, 0, NULL
    );
    obs_data_release(audio_encoder_settings);
    if (!filter->audio_encoder) {
        obs_log(LOG_ERROR, "%s: Audio encoder creation failed", obs_source_get_name(filter->source));
        return;
    }
    obs_encoder_set_audio(filter->audio_encoder, filter->audio_output);
    obs_output_set_audio_encoder(filter->stream_output, filter->audio_encoder, 0);

    // Start stream output
    if (obs_output_start(filter->stream_output)) {
        filter->output_active = true;
        obs_source_inc_showing(obs_filter_get_parent(filter->source));
        obs_log(LOG_INFO, "%s: Starting stream output succeeded", obs_source_get_name(filter->source));
    } else {
        obs_log(LOG_ERROR, "%s: Starting stream output failed", obs_source_get_name(filter->source));
    }
}

void update(void *data, obs_data_t *settings)
{
    auto filter = (filter_t *)data;
    obs_log(LOG_DEBUG, "%s: Filter updating", obs_source_get_name(filter->source));

    // It's unwelcome to do stopping output during attempting connect to service.
    // So we just count up revision (Settings will be applied on video_tick())
    filter->stored_settings_rev++;

    // Save settings as default
    auto config_dir_path = obs_module_get_config_path(obs_current_module(), "");
    os_mkdirs(config_dir_path);
    bfree(config_dir_path);

    auto path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    obs_data_save_json_safe(settings, path, "tmp", "bak");
    bfree(path);

    obs_log(LOG_INFO, "%s: Filter updated", obs_source_get_name(filter->source));
}

inline void load_recently(obs_data_t *settings)
{
    obs_log(LOG_DEBUG, "Recently settings loading");
    auto path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    auto recently_settings = obs_data_create_from_json_file(path);
    bfree(path);

    if (recently_settings) {
        obs_data_erase(recently_settings, "server");
        obs_data_erase(recently_settings, "key");
        obs_data_erase(recently_settings, "custom_audio_source");
        obs_data_erase(recently_settings, "audio_source");
        obs_data_apply(settings, recently_settings);
    }

    obs_data_release(recently_settings);
    obs_log(LOG_INFO, "Recently settings loaded");
}

void *create(obs_data_t *settings, obs_source_t *source)
{
    obs_log(LOG_DEBUG, "%s: Filter creating", obs_source_get_name(source));
    obs_log(LOG_DEBUG, "filter_settings_json=%s", obs_data_get_json(settings));

    auto filter = (filter_t *)bzalloc(sizeof(filter_t));
    pthread_mutex_init(&filter->audio_buffer_mutex, NULL);

    filter->source = source;

    if (!strcmp(obs_data_get_last_json(settings), "{}")) {
        // Maybe initial creation
        load_recently(settings);
    }

    // Fiter activate immediately when "server" is exists.
    auto server = obs_data_get_string(settings, "server");
    filter->filter_active = !!strlen(server);

    obs_log(LOG_INFO, "%s: Filter created", obs_source_get_name(filter->source));
    return filter;
}

void destroy(void *data)
{
    auto filter = (filter_t *)data;
    auto source = filter->source;
    obs_log(LOG_DEBUG, "%s: Filter destroying", obs_source_get_name(source));

    stop_output(filter);
    pthread_mutex_destroy(&filter->audio_buffer_mutex);
    bfree(filter->audio_conv_buffer);
    bfree(filter);

    obs_log(LOG_INFO, "%s: Filter destroyed", obs_source_get_name(source));
}

inline void restart_output(filter_t *filter)
{
    if (filter->output_active) {
        stop_output(filter);
    }

    auto settings = obs_source_get_settings(filter->source);
    auto server = obs_data_get_string(settings, "server");
    if (strlen(server)) {
        start_output(filter, settings);
    }
    obs_data_release(settings);
}

inline bool connect_attempting_timed_out(filter_t *filter)
{
    return filter->connect_attempting_at &&
           os_gettime_ns() - filter->connect_attempting_at > CONNECT_ATTEMPTING_TIMEOUT_NS;
}

inline bool source_available(filter_t *filter, obs_source_t *source)
{
    auto now = os_gettime_ns();
    if (now - filter->last_available_at < AVAILAVILITY_CHECK_INTERVAL_NS) {
        return true;
    }
    filter->last_available_at = now;

    auto found = !!obs_scene_from_source(source);
    if (found) {
        return true;
    }

    obs_frontend_source_list scenes = {0};
    obs_frontend_get_scenes(&scenes);

    for (size_t i = 0; i < scenes.sources.num && !found; i++) {
        obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
        found = !!obs_scene_find_source_recursive(scene, obs_source_get_name(source));
    }

    obs_frontend_source_list_free(&scenes);

    return found;
}

// NOTE: Becareful this function is called so offen.
void video_tick(void *data, float)
{
    auto filter = (filter_t *)data;
    // Block output initiation until filter is active.
    if (!filter->filter_active) {
        return;
    }

    auto source_enabled = obs_source_enabled(filter->source);

    if (filter->output_active) {
        auto stream_active = obs_output_active(filter->stream_output);

        if (source_enabled) {
            if (connect_attempting_timed_out(filter)) {
                if (!stream_active) {
                    // Retry connection
                    obs_log(
                        LOG_INFO, "%s: Attempting reactivate the stream output", obs_source_get_name(filter->source)
                    );
                    auto settings = obs_source_get_settings(filter->source);
                    start_output(filter, settings);
                    obs_data_release(settings);
                    return;
                }

                if (filter->active_settings_rev < filter->stored_settings_rev) {
                    // Settings has been changed
                    obs_log(
                        LOG_INFO, "%s: Settings change detected, Attempting restart",
                        obs_source_get_name(filter->source)
                    );
                    restart_output(filter);
                    return;
                }

                if (stream_active) {
                    // Monitoring source
                    auto parent = obs_filter_get_parent(filter->source);
                    auto width = obs_source_get_width(parent);
                    width += (width & 1);
                    uint32_t height = obs_source_get_height(parent);
                    height += (height & 1);

                    if (!width || !height || !source_available(filter, parent)) {
                        // Stop output when source resolution is zero or source had been removed
                        stop_output(filter);
                        return;
                    }

                    if (filter->width != width || filter->height != height) {
                        // Restart output when source resolution was changed.
                        obs_log(
                            LOG_INFO, "%s: Attempting restart the stream output", obs_source_get_name(filter->source)
                        );
                        auto settings = obs_source_get_settings(filter->source);
                        start_output(filter, settings);
                        obs_data_release(settings);
                        return;
                    }
                }
            }

        } else {
            if (stream_active) {
                // Clicked filter's "Eye" icon (Hide)
                stop_output(filter);
                return;
            }
        }

    } else {
        if (source_enabled) {
            // Clicked filter's "Eye" icon (Show)
            restart_output(filter);
            return;
        }
    }
}

const char *get_name(void *)
{
    return "Branch Output";
}

obs_source_info create_filter_info()
{
    obs_source_info filter_info = {0};

    filter_info.id = FILTER_ID;
    filter_info.type = OBS_SOURCE_TYPE_FILTER;
    filter_info.output_flags = OBS_SOURCE_VIDEO;

    filter_info.get_name = get_name;
    filter_info.get_properties = get_properties;
    filter_info.get_defaults = get_defaults;

    filter_info.create = create;
    filter_info.destroy = destroy;
    filter_info.update = update;

    filter_info.filter_audio = audio_filter_callback;
    filter_info.video_tick = video_tick;

    return filter_info;
}

obs_source_info filter_info;
BranchOutputStatus* status = nullptr;

bool obs_module_load()
{
    filter_info = create_filter_info();
    obs_register_source(&filter_info);

    obs_log(LOG_INFO, "Plugin loaded successfully (version %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_post_load()
{
    status = create_dock();
}

void obs_module_unload()
{
    obs_log(LOG_INFO, "Plugin unloaded");
}
