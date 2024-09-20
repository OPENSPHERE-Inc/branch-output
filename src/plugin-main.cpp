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
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/deque.h>
#include <util/threading.h>
#include <util/platform.h>
#include <obs.hpp>

#include "plugin-support.h"
#include "plugin-main.hpp"
#include "utils.hpp"

#define SETTINGS_JSON_NAME "recently.json"
#define FILTER_ID "osi_branch_output"
#define OUTPUT_MAX_RETRIES 7
#define OUTPUT_RETRY_DELAY_SECS 1
#define CONNECT_ATTEMPTING_TIMEOUT_NS 15000000000ULL
#define AVAILAVILITY_CHECK_INTERVAL_NS 1000000000ULL
#define TASK_INTERVAL_MS 1000

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

BranchOutputStatusDock *statusDock = nullptr;

void stopOutput(BranchOutputFilter *filter)
{
    pthread_mutex_lock(&filter->outputMutex);
    {
        OBSMutexAutoUnlock locked(&filter->outputMutex);

        obs_source_t *parent = obs_filter_get_parent(filter->filterSource);
        filter->connectAttemptingAt = 0;

        if (filter->recordingOutput) {
            if (filter->recordingActive) {
                obs_source_dec_showing(parent);
                obs_output_stop(filter->recordingOutput);
            }

            obs_output_release(filter->recordingOutput);
            filter->recordingOutput = nullptr;
        }

        if (filter->streamOutput) {
            if (filter->outputActive) {
                obs_source_dec_showing(parent);
                obs_output_stop(filter->streamOutput);
            }

            obs_output_release(filter->streamOutput);
            filter->streamOutput = nullptr;
        }

        if (filter->service) {
            obs_service_release(filter->service);
            filter->service = nullptr;
        }

        if (filter->audioEncoder) {
            obs_encoder_release(filter->audioEncoder);
            filter->audioEncoder = nullptr;
        }

        if (filter->videoEncoder) {
            obs_encoder_release(filter->videoEncoder);
            filter->videoEncoder = nullptr;
        }

        if (filter->audioSourceType == AUDIO_SOURCE_TYPE_CAPTURE) {
            if (filter->audioSource) {
                auto source = obs_weak_source_get_source(filter->audioSource);
                if (source) {
                    obs_source_remove_audio_capture_callback(source, audioCaptureCallback, filter);
                    obs_source_release(source);
                }
                obs_weak_source_release(filter->audioSource);
                filter->audioSource = nullptr;
            }

        } else if (filter->audioSourceType == AUDIO_SOURCE_TYPE_MASTER) {
            obs_remove_raw_audio_callback(filter->audioMixIdx, masterAudioCallback, filter);
        }
        filter->audioSourceType = AUDIO_SOURCE_TYPE_SILENCE;

        if (filter->audioOutput) {
            audio_output_close(filter->audioOutput);
            filter->audioOutput = nullptr;
        }

        if (filter->view) {
            obs_view_set_source(filter->view, 0, nullptr);
            obs_view_remove(filter->view);
            obs_view_destroy(filter->view);
            filter->view = nullptr;
        }

        filter->audioBufferFrames = 0;
        filter->audioSkip = 0;
        deque_free(&filter->audioBuffer);

        if (filter->recordingActive) {
            filter->recordingActive = false;
            obs_log(LOG_INFO, "%s: Stopping recording output succeeded", obs_source_get_name(filter->filterSource));
        }

        if (filter->outputActive) {
            filter->outputActive = false;
            obs_log(LOG_INFO, "%s: Stopping stream output succeeded", obs_source_get_name(filter->filterSource));
        }
    }
}

obs_data_t *createRecordingSettings(BranchOutputFilter *filter, obs_data_t *settings)
{
    obs_data_t *recordingSettings = obs_data_create();
    auto config = obs_frontend_get_profile_config();
    QString filenameFormat = config_get_string(config, "Output", "FilenameFormatting");
    auto path = obs_data_get_string(settings, "path");
    auto recFormat = obs_data_get_string(settings, "rec_format");

    // Add filter name to filename format
    QString sourceName = obs_source_get_name(obs_filter_get_parent(filter->filterSource));
    QString filterName = obs_source_get_name(filter->filterSource);
    filenameFormat = QString("%1_%2_%3")
                         .arg(sourceName.replace(QRegularExpression("[\\s/\\\\.:;*?\"<>|&$,]"), "-"))
                         .arg(filterName.replace(QRegularExpression("[\\s/\\\\.:;*?\"<>|&$,]"), "-"))
                         .arg(filenameFormat);
    auto compositePath = getOutputFilename(path, recFormat, true, false, qUtf8Printable(filenameFormat));

    obs_data_set_string(recordingSettings, "path", qUtf8Printable(compositePath));

    auto splitFile = obs_data_get_string(settings, "split_file");
    if (strlen(splitFile) > 0) {
        obs_data_set_string(recordingSettings, "directory", path);
        obs_data_set_string(recordingSettings, "format", qUtf8Printable(filenameFormat));
        obs_data_set_string(recordingSettings, "extension", qUtf8Printable(getFormatExt(recFormat)));
        obs_data_set_bool(recordingSettings, "allow_spaces", false);
        obs_data_set_bool(recordingSettings, "allow_overwrite", false);
        obs_data_set_bool(recordingSettings, "split_file", true);

        auto maxTimeSec = !strcmp(splitFile, "by_time") ? obs_data_get_int(settings, "split_file_time_mins") * 60 : 0;
        obs_data_set_int(recordingSettings, "max_time_sec", maxTimeSec);

        auto maxSizeMb = !strcmp(splitFile, "by_size") ? obs_data_get_int(settings, "split_file_size_mb") : 0;
        obs_data_set_int(recordingSettings, "max_size_mb", maxSizeMb);
    }

    return recordingSettings;
}

#define FTL_PROTOCOL "ftl"
#define RTMP_PROTOCOL "rtmp"

void startOutput(BranchOutputFilter *filter, obs_data_t *settings)
{
    // Force release references
    stopOutput(filter);

    pthread_mutex_lock(&filter->outputMutex);
    {
        OBSMutexAutoUnlock locked(&filter->outputMutex);

        // Abort when obs initializing or filter disabled.
        if (!obs_initialized() || !obs_source_enabled(filter->filterSource) || filter->outputActive ||
            filter->recordingActive) {
            return;
        }

        // Mandatory paramters
        if (!strlen(obs_data_get_string(settings, "server")) && !obs_data_get_string(settings, "stream_recording")) {
            return;
        }

        // Retrieve filter source
        auto parent = obs_filter_get_parent(filter->filterSource);
        if (!parent) {
            obs_log(LOG_ERROR, "%s: Filter source not found", obs_source_get_name(filter->filterSource));
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
        filter->activeSettingsRev = filter->storedSettingsRev;

        //--- Create service and open stream output ---//
        if (!!strlen(obs_data_get_string(settings, "server"))) {
            // Create service - We always use "rtmp_custom" as service
            filter->service =
                obs_service_create("rtmp_custom", obs_source_get_name(filter->filterSource), settings, nullptr);
            if (!filter->service) {
                obs_log(LOG_ERROR, "%s: Service creation failed", obs_source_get_name(filter->filterSource));
                return;
            }
            obs_service_apply_encoder_settings(filter->service, settings, nullptr);

            // Determine output type
            auto type = obs_service_get_preferred_output_type(filter->service);
            if (!type) {
                type = "rtmp_output";
                auto url = obs_service_get_connect_info(filter->service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
                if (url != nullptr && !strncmp(url, FTL_PROTOCOL, strlen(FTL_PROTOCOL))) {
                    type = "ftl_output";
                } else if (url != nullptr && strncmp(url, RTMP_PROTOCOL, strlen(RTMP_PROTOCOL))) {
                    type = "ffmpeg_mpegts_muxer";
                }
            }

            // Create stream output
            filter->streamOutput =
                obs_output_create(type, obs_source_get_name(filter->filterSource), settings, nullptr);
            if (!filter->streamOutput) {
                obs_log(LOG_ERROR, "%s: Stream output creation failed", obs_source_get_name(filter->filterSource));
                return;
            }
            obs_output_set_reconnect_settings(filter->streamOutput, OUTPUT_MAX_RETRIES, OUTPUT_RETRY_DELAY_SECS);
            obs_output_set_service(filter->streamOutput, filter->service);
            filter->connectAttemptingAt = os_gettime_ns();
        }

        //--- Open video output ---//
        // Create view and associate it with filter source
        filter->view = obs_view_create();

        obs_view_set_source(filter->view, 0, parent);
        filter->videoOutput = obs_view_add2(filter->view, &ovi);
        if (!filter->videoOutput) {
            obs_log(LOG_ERROR, "%s: Video output association failed", obs_source_get_name(filter->filterSource));
            return;
        }

        //--- Open audio output ---//
        // Retrieve audio source
        filter->audioSourceType = AUDIO_SOURCE_TYPE_SILENCE;
        filter->audioSource = nullptr;
        filter->audioMixIdx = 0;
        filter->audioChannels = (speaker_layout)audio_output_get_channels(obs_get_audio());
        filter->samplesPerSec = audio_output_get_sample_rate(obs_get_audio());
        filter->audioBufferFrames = 0;
        filter->audioSkip = 0;

        if (obs_data_get_bool(settings, "custom_audio_source")) {
            // Apply custom audio source
            auto audioSourceUuid = obs_data_get_string(settings, "audio_source");

            if (strlen(audioSourceUuid) && strcmp(audioSourceUuid, "no_audio")) {
                if (!strncmp(audioSourceUuid, "master_track", strlen("master_track"))) {
                    // Use master audio track
                    auto trackNo = obs_data_get_int(settings, "audio_track");
                    if (trackNo > 0 && trackNo <= MAX_AUDIO_MIXES) {
                        filter->audioMixIdx = trackNo - 1;

                        audio_convert_info conv = {0};
                        conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
                        conv.samples_per_sec = filter->samplesPerSec;
                        conv.speakers = filter->audioChannels;
                        conv.allow_clipping = true;

                        filter->audioSourceType = AUDIO_SOURCE_TYPE_MASTER;

                        obs_add_raw_audio_callback(filter->audioMixIdx, &conv, masterAudioCallback, filter);
                    }

                } else {
                    auto source = obs_get_source_by_uuid(audioSourceUuid);
                    if (source) {
                        // Use custom audio source
                        obs_log(
                            LOG_INFO, "%s: Use %s as an audio source", obs_source_get_name(filter->filterSource),
                            obs_source_get_name(source)
                        );
                        filter->audioSource = obs_source_get_weak_source(source);
                        filter->audioSourceType = AUDIO_SOURCE_TYPE_CAPTURE;

                        if (!filter->audioSource) {
                            obs_log(
                                LOG_ERROR, "%s: Audio source retrieval failed",
                                obs_source_get_name(filter->filterSource)
                            );
                            obs_source_release(source);
                            return;
                        }

                        // Register audio capture callback (It forwards audio to output)
                        obs_source_add_audio_capture_callback(source, audioCaptureCallback, filter);

                        obs_source_release(source);
                    }
                }
            }

        } else {
            // Use filter's audio
            obs_log(LOG_INFO, "%s: Use filter audio as an audio source", obs_source_get_name(filter->filterSource));
            filter->audioSourceType = AUDIO_SOURCE_TYPE_FILTER;
        }

        if (filter->audioSourceType == AUDIO_SOURCE_TYPE_SILENCE) {
            obs_log(LOG_INFO, "%s: Audio is disabled", obs_source_get_name(filter->filterSource));
        }

        // Open audio output (Audio will be captured from filter source via audioInputCallback)
        audio_output_info oi = {0};

        oi.name = obs_source_get_name(filter->filterSource);
        oi.speakers = filter->audioChannels;
        oi.samples_per_sec = filter->samplesPerSec;
        oi.format = AUDIO_FORMAT_FLOAT_PLANAR;
        oi.input_param = filter;
        oi.input_callback = audioInputCallback;

        if (audio_output_open(&filter->audioOutput, &oi) < 0) {
            obs_log(LOG_ERROR, "%s: Opening audio output failed", obs_source_get_name(filter->filterSource));
            return;
        }

        //--- Setup video encoder ---//
        auto video_encoder_id = obs_data_get_string(settings, "video_encoder");
        filter->videoEncoder =
            obs_video_encoder_create(video_encoder_id, obs_source_get_name(filter->filterSource), settings, nullptr);
        if (!filter->videoEncoder) {
            obs_log(LOG_ERROR, "%s: Video encoder creation failed", obs_source_get_name(filter->filterSource));
            return;
        }
        obs_encoder_set_scaled_size(filter->videoEncoder, 0, 0);
        obs_encoder_set_video(filter->videoEncoder, filter->videoOutput);

        //--- Setup audo encoder ---//
        auto audio_encoder_id = obs_data_get_string(settings, "audio_encoder");
        auto audio_bitrate = obs_data_get_int(settings, "audio_bitrate");
        auto audio_encoder_settings = obs_encoder_defaults(audio_encoder_id);
        obs_data_set_int(audio_encoder_settings, "bitrate", audio_bitrate);

        // Use track 0 only.
        filter->audioEncoder = obs_audio_encoder_create(
            audio_encoder_id, obs_source_get_name(filter->filterSource), audio_encoder_settings, 0, nullptr
        );
        obs_data_release(audio_encoder_settings);
        if (!filter->audioEncoder) {
            obs_log(LOG_ERROR, "%s: Audio encoder creation failed", obs_source_get_name(filter->filterSource));
            return;
        }
        obs_encoder_set_audio(filter->audioEncoder, filter->audioOutput);

        //--- Create recording output (if requested) ---//
        if (obs_data_get_bool(settings, "stream_recording")) {
            auto recFormat = obs_data_get_string(settings, "rec_format");
            const char *outputId = !strcmp(recFormat, "hybrid_mp4") ? "mp4_output" : "ffmpeg_muxer";

            OBSDataAutoRelease recordingSettings = createRecordingSettings(filter, settings);
            filter->recordingOutput =
                obs_output_create(outputId, obs_source_get_name(filter->filterSource), recordingSettings, nullptr);
            if (!filter->recordingOutput) {
                obs_log(LOG_ERROR, "%s: Recording output creation failed", obs_source_get_name(filter->filterSource));
                return;
            }

            obs_output_set_audio_encoder(filter->recordingOutput, filter->audioEncoder, 0);
            obs_output_set_video_encoder(filter->recordingOutput, filter->videoEncoder);

            // Start recording output
            if (obs_output_start(filter->recordingOutput)) {
                filter->recordingActive = true;
                obs_source_inc_showing(obs_filter_get_parent(filter->filterSource));
                obs_log(LOG_INFO, "%s: Starting recording output succeeded", obs_source_get_name(filter->filterSource));
            } else {
                obs_log(LOG_ERROR, "%s: Starting recording output failed", obs_source_get_name(filter->filterSource));
            }
        }

        //--- Start streaming output (if requested) ---//
        if (filter->streamOutput) {
            obs_output_set_audio_encoder(filter->streamOutput, filter->audioEncoder, 0);
            obs_output_set_video_encoder(filter->streamOutput, filter->videoEncoder);

            // Start streaming output
            if (obs_output_start(filter->streamOutput)) {
                filter->outputActive = true;
                obs_source_inc_showing(obs_filter_get_parent(filter->filterSource));
                obs_log(LOG_INFO, "%s: Starting streaming output succeeded", obs_source_get_name(filter->filterSource));
            } else {
                obs_log(LOG_ERROR, "%s: Starting streaming output failed", obs_source_get_name(filter->filterSource));
            }
        }
    }
}

void update(void *data, obs_data_t *settings)
{
    auto filter = (BranchOutputFilter *)data;
    obs_log(LOG_DEBUG, "%s: Filter updating", obs_source_get_name(filter->filterSource));

    // It's unwelcome to do stopping output during attempting connect to service.
    // So we just count up revision (Settings will be applied on videoTick())
    filter->storedSettingsRev++;

    // Save settings as default
    OBSString config_dir_path = obs_module_get_config_path(obs_current_module(), "");
    os_mkdirs(config_dir_path);

    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    obs_data_save_json_safe(settings, path, "tmp", "bak");

    obs_log(LOG_INFO, "%s: Filter updated", obs_source_get_name(filter->filterSource));
}

inline void loadRecently(obs_data_t *settings)
{
    obs_log(LOG_DEBUG, "Recently settings loading");
    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    OBSDataAutoRelease recently_settings = obs_data_create_from_json_file(path);

    if (recently_settings) {
        obs_data_erase(recently_settings, "server");
        obs_data_erase(recently_settings, "key");
        obs_data_erase(recently_settings, "use_auth");
        obs_data_erase(recently_settings, "username");
        obs_data_erase(recently_settings, "password");
        obs_data_erase(recently_settings, "custom_audio_source");
        obs_data_erase(recently_settings, "audio_source");
        obs_data_erase(recently_settings, "audio_track");
        obs_data_apply(settings, recently_settings);
    }

    obs_log(LOG_INFO, "Recently settings loaded");
}

inline void restartOutput(BranchOutputFilter *filter)
{
    if (filter->outputActive || filter->recordingActive) {
        stopOutput(filter);
    }

    OBSDataAutoRelease settings = obs_source_get_settings(filter->filterSource);
    if (!!strlen(obs_data_get_string(settings, "server")) || obs_data_get_bool(settings, "stream_recording")) {
        startOutput(filter, settings);
    }
}

inline bool connectAttemptingTimedOut(BranchOutputFilter *filter)
{
    return filter->connectAttemptingAt && os_gettime_ns() - filter->connectAttemptingAt > CONNECT_ATTEMPTING_TIMEOUT_NS;
}

inline bool sourceAvailable(BranchOutputFilter *filter, obs_source_t *source)
{
    auto now = os_gettime_ns();
    if (now - filter->lastAvailableAt < AVAILAVILITY_CHECK_INTERVAL_NS) {
        return true;
    }
    filter->lastAvailableAt = now;

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

// Controlling output status here.
// Start / Stop should only heppen in this function as possible because rapid manipulation caused crash easily.
// NOTE: Becareful this function is called so offen.
void intervalTask(BranchOutputFilter *filter)
{
    // Block output initiation until filter is active.
    if (!filter->initialized) {
        return;
    }

    auto interlockType = statusDock->getInterlockType();
    auto sourceEnabled = obs_source_enabled(filter->filterSource);

    if (filter->outputActive || filter->recordingActive) {
        auto outputAlive = (filter->streamOutput && obs_output_active(filter->streamOutput)) ||
                           (filter->recordingOutput && obs_output_active(filter->recordingOutput));

        if (sourceEnabled) {
            if (filter->outputActive && !connectAttemptingTimedOut(filter)) {
                return;
            }

            // Check interlock condition
            if (interlockType == INTERLOCK_TYPE_STREAMING) {
                if (!obs_frontend_streaming_active()) {
                    // Stop output when streaming is not active
                    stopOutput(filter);
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_RECORDING) {
                if (!obs_frontend_recording_active()) {
                    // Stop output when recording is not active
                    stopOutput(filter);
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_STREAMING_RECORDING) {
                if (!obs_frontend_streaming_active() && !obs_frontend_recording_active()) {
                    // Stop output when streaming and recording are not active
                    stopOutput(filter);
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_VIRTUAL_CAM) {
                if (!obs_frontend_virtualcam_active()) {
                    // Stop output when virtual cam is not active
                    stopOutput(filter);
                    return;
                }
            }

            if (filter->activeSettingsRev < filter->storedSettingsRev) {
                // Settings has been changed
                obs_log(
                    LOG_INFO, "%s: Settings change detected, Attempting restart",
                    obs_source_get_name(filter->filterSource)
                );
                restartOutput(filter);
                return;
            }

            if (!outputAlive) {
                // Retry connection
                obs_log(
                    LOG_INFO, "%s: Attempting reactivate the stream output", obs_source_get_name(filter->filterSource)
                );
                OBSDataAutoRelease settings = obs_source_get_settings(filter->filterSource);
                startOutput(filter, settings);
                return;
            }

            if (outputAlive) {
                // Monitoring source
                auto parent = obs_filter_get_parent(filter->filterSource);
                auto width = obs_source_get_width(parent);
                width += (width & 1);
                uint32_t height = obs_source_get_height(parent);
                height += (height & 1);

                if (!width || !height || !sourceAvailable(filter, parent)) {
                    // Stop output when source resolution is zero or source had been removed
                    stopOutput(filter);
                    return;
                }

                if (filter->width != width || filter->height != height) {
                    // Restart output when source resolution was changed.
                    obs_log(
                        LOG_INFO, "%s: Attempting restart the stream output", obs_source_get_name(filter->filterSource)
                    );
                    OBSDataAutoRelease settings = obs_source_get_settings(filter->filterSource);
                    startOutput(filter, settings);
                    return;
                }
            }

        } else {
            if (outputAlive) {
                // Clicked filter's "Eye" icon (Hide)
                stopOutput(filter);
                return;
            }
        }

    } else {
        if (sourceEnabled) {
            // Clicked filter's "Eye" icon (Show)
            // Check interlock condition
            if (interlockType == INTERLOCK_TYPE_STREAMING) {
                if (obs_frontend_streaming_active()) {
                    restartOutput(filter);
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_RECORDING) {
                if (obs_frontend_recording_active()) {
                    restartOutput(filter);
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_STREAMING_RECORDING) {
                if (obs_frontend_streaming_active() || obs_frontend_recording_active()) {
                    restartOutput(filter);
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_VIRTUAL_CAM) {
                if (obs_frontend_virtualcam_active()) {
                    restartOutput(filter);
                    return;
                }
            } else {
                restartOutput(filter);
                return;
            }
        }
    }
}

void *create(obs_data_t *settings, obs_source_t *source)
{
    obs_log(LOG_DEBUG, "%s: Filter creating", obs_source_get_name(source));
    obs_log(LOG_DEBUG, "filter_settings_json=%s", obs_data_get_json(settings));

    auto filter = (BranchOutputFilter *)bzalloc(sizeof(BranchOutputFilter));
    pthread_mutex_init(&filter->outputMutex, nullptr);
    pthread_mutex_init(&filter->audioBufferMutex, nullptr);

    filter->filterSource = source;

    if (!strcmp(obs_data_get_last_json(settings), "{}")) {
        // Maybe initial creation
        loadRecently(settings);
    }

    // Migrate audio_source schema
    auto audioSource = obs_data_get_string(settings, "audio_source");
    if (!strncmp(audioSource, "master_track_", strlen("master_track_"))) {
        // Separate out track number
        size_t trackNo = 0;
        sscanf(audioSource, "master_track_%zu", &trackNo);

        obs_data_set_string(settings, "audio_source", "master_track");
        obs_data_set_int(settings, "audio_track", trackNo);
    }

    // Fiter activate immediately when "server" or "stream_recording" is exists.
    filter->initialized = !!strlen(obs_data_get_string(settings, "server")) ||
                          obs_data_get_bool(settings, "stream_recording");

    // Start interval timer
    filter->intervalTimer = new QTimer();
    filter->intervalTimer->setInterval(TASK_INTERVAL_MS);
    filter->intervalTimer->start();
    QObject::connect(filter->intervalTimer, &QTimer::timeout, [filter]() { intervalTask(filter); });

    obs_log(LOG_INFO, "%s: Filter created", obs_source_get_name(filter->filterSource));
    return filter;
}

void destroy(void *data)
{
    auto filter = (BranchOutputFilter *)data;
    auto source = filter->filterSource;
    obs_log(LOG_DEBUG, "%s: Filter destroying", obs_source_get_name(source));

    // Stop interval timer (in proper thread)
    filter->intervalTimer->deleteLater();

    stopOutput(filter);
    pthread_mutex_destroy(&filter->audioBufferMutex);
    pthread_mutex_destroy(&filter->outputMutex);
    bfree(filter->audioConvBuffer);
    bfree(filter);

    obs_log(LOG_INFO, "%s: Filter destroyed", obs_source_get_name(source));
}

void filterAdd(void *data, obs_source_t *)
{
    // Register to output status dock
    auto filter = (BranchOutputFilter *)data;
    statusDock->addFilter(filter);
}

void filterRemove(void *data, obs_source_t *)
{
    // Unregister from output status dock
    auto filter = (BranchOutputFilter *)data;
    statusDock->removeFilter(filter);
}

void videoRender(void *data, gs_effect_t *)
{
    auto filter = (BranchOutputFilter *)data;
    obs_source_skip_video_filter(filter->filterSource);
}

obs_source_info createFilterInfo()
{
    obs_source_info filter_info = {0};

    filter_info.id = FILTER_ID;
    filter_info.type = OBS_SOURCE_TYPE_FILTER;
    filter_info.output_flags = OBS_SOURCE_VIDEO;

    filter_info.get_name = [](void *) {
        return "Branch Output";
    };
    filter_info.get_properties = getProperties;
    filter_info.get_defaults = getDefaults;

    filter_info.create = create;
    filter_info.destroy = destroy;
    filter_info.update = update;
    filter_info.filter_add = filterAdd;
    filter_info.filter_remove = filterRemove;
    filter_info.filter_audio = audioFilterCallback;
    filter_info.video_render = videoRender;

    return filter_info;
}

obs_source_info filterInfo;

bool obs_module_load()
{
    filterInfo = createFilterInfo();
    obs_register_source(&filterInfo);

    obs_log(LOG_INFO, "Plugin loaded successfully (version %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_post_load()
{
    statusDock = createOutputStatusDock();
}

void obs_module_unload()
{
    obs_log(LOG_INFO, "Plugin unloaded");
}
