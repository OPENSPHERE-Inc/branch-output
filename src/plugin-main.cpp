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

#include "audio/audio-capture.hpp"
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

        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &filter->audios[i];
            if (audioContext->encoder) {
                obs_encoder_release(audioContext->encoder);
                audioContext->encoder = nullptr;
            }
        }

        if (filter->videoEncoder) {
            obs_encoder_release(filter->videoEncoder);
            filter->videoEncoder = nullptr;
        }

        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &filter->audios[i];
            if (audioContext->capture) {
                delete audioContext->capture;
                audioContext->capture = nullptr;
            }
        }

        if (filter->view) {
            obs_view_set_source(filter->view, 0, nullptr);
            obs_view_remove(filter->view);
            obs_view_destroy(filter->view);
            filter->view = nullptr;
        }

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
    QString filenameFormat = obs_data_get_string(settings, "filename_formatting");
    if (filenameFormat.isEmpty()) {
        filenameFormat = QString("%1_%2_") + QString(config_get_string(config, "Output", "FilenameFormatting"));
    }

    // Sanitize filename
#ifdef __APPLE__
    filenameFormat.replace(QRegularExpression("[:]"), "");
#elif defined(_WIN32)
    filenameFormat.replace(QRegularExpression("[<>:\"\\|\\?\\*]"), "");
#else
    // TODO: Add filtering for other platforms
#endif

    auto path = obs_data_get_string(settings, "path");
    auto recFormat = obs_data_get_string(settings, "rec_format");

    // Add filter name to filename format
    QString sourceName = obs_source_get_name(obs_filter_get_parent(filter->filterSource));
    QString filterName = obs_source_get_name(filter->filterSource);
    filenameFormat = filenameFormat.arg(sourceName.replace(QRegularExpression("[\\s/\\\\.:;*?\"<>|&$,]"), "-"))
                         .arg(filterName.replace(QRegularExpression("[\\s/\\\\.:;*?\"<>|&$,]"), "-"));
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

// Decide source/scene is private or not
inline bool sourceIsPrivate(obs_source_t *source)
{
    auto finder = source;
    auto callback = [](void *param, obs_source_t *_source) {
        auto _finder = (obs_source_t **)param;
        if (_source == *_finder) {
            *_finder = nullptr;
            return false;
        }
        return true;
    };

    obs_enum_scenes(callback, &finder);
    if (finder != nullptr) {
        obs_enum_sources(callback, &finder);
    }

    return finder != nullptr;
}

inline void determineOutputResolution(BranchOutputFilter *filter, obs_data_t *settings, obs_video_info *ovi)
{
    auto resolution = obs_data_get_string(settings, "resolution");
    if (!strcmp(resolution, "custom")) {
        // Custom resolution
        ovi->output_width = obs_data_get_int(settings, "custom_width");
        ovi->output_height = obs_data_get_int(settings, "custom_height");

    } else if (!strcmp(resolution, "output")) {
        // Nothing to do

    } else if (!strcmp(resolution, "canvas")) {
        // Copy canvas resolution
        ovi->output_width = ovi->base_width;
        ovi->output_height = ovi->base_height;

    } else if (!strcmp(resolution, "three_quarters")) {
        // Rescale source resolution
        ovi->output_width = filter->width * 3 / 4;
        ovi->output_height = filter->height * 3 / 4;

    } else if (!strcmp(resolution, "half")) {
        // Rescale source resolution
        ovi->output_width = filter->width / 2;
        ovi->output_height = filter->height / 2;

    } else if (!strcmp(resolution, "quarter")) {
        // Rescale source resolution
        ovi->output_width = filter->width / 4;
        ovi->output_height = filter->height / 4;

    } else {
        // Copy source resolution
        ovi->output_width = filter->width;
        ovi->output_height = filter->height;
    }

    // Round up to a multiple of 2
    ovi->output_width += (ovi->output_width & 1);
    ovi->output_height += (ovi->output_height & 1);

    auto downscaleFilter = obs_data_get_string(settings, "downscale_filter");
    if (!strcmp(downscaleFilter, "bilinear")) {
        ovi->scale_type = OBS_SCALE_BILINEAR;
    } else if (!strcmp(downscaleFilter, "area")) {
        ovi->scale_type = OBS_SCALE_AREA;
    } else if (!strcmp(downscaleFilter, "bicubic")) {
        ovi->scale_type = OBS_SCALE_BICUBIC;
    } else if (!strcmp(downscaleFilter, "lanczos")) {
        ovi->scale_type = OBS_SCALE_LANCZOS;
    }
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
            obs_log(LOG_ERROR, "%s: Ignore unavailable filter", obs_source_get_name(filter->filterSource));
            return;
        }

        // Retrieve filter source
        auto parent = obs_filter_get_parent(filter->filterSource);
        if (!parent) {
            obs_log(LOG_ERROR, "%s: Filter source not found", obs_source_get_name(filter->filterSource));
            return;
        }

        // Ignore private sources
        if (sourceIsPrivate(parent)) {
            obs_log(LOG_ERROR, "%s: Ignore private source", obs_source_get_name(filter->filterSource));
            return;
        }

        // Mandatory paramters
        if (!strlen(obs_data_get_string(settings, "server")) && !obs_data_get_string(settings, "stream_recording")) {
            obs_log(LOG_ERROR, "%s: Nothing to do", obs_source_get_name(filter->filterSource));
            return;
        }

        obs_video_info ovi = {0};
        if (!obs_get_video_info(&ovi)) {
            // Abort when no video situation
            obs_log(LOG_ERROR, "%s: No video", obs_source_get_name(filter->filterSource));
            return;
        }

        obs_video_info encvi = ovi;

        // Round up to a multiple of 2
        filter->width = obs_source_get_width(parent);
        filter->width += (filter->width & 1);
        // Round up to a multiple of 2
        filter->height = obs_source_get_height(parent);
        filter->height += (filter->height & 1);

        ovi.base_width = filter->width;
        ovi.base_height = filter->height;
        ovi.output_width = encvi.base_width = filter->width;
        ovi.output_height = encvi.base_height = filter->height;

        if (filter->width == 0 || filter->height == 0 || ovi.fps_den == 0 || ovi.fps_num == 0) {
            // Abort when invalid video parameters situation
            obs_log(LOG_ERROR, "%s: Invalid video spec", obs_source_get_name(filter->filterSource));
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

        //--- Open audio output(s) ---//
        memset(filter->audios, 0, sizeof(filter->audios));

        obs_audio_info ai = {0};
        if (!obs_get_audio_info(&ai)) {
            obs_log(LOG_ERROR, "%s: Failed to get audio info", obs_source_get_name(filter->filterSource));
            return;
        }

        if (obs_data_get_bool(settings, "custom_audio_source")) {
            // Apply custom audio source
            bool multitrack = obs_data_get_bool(settings, "multitrack_audio");

            for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
                auto audioContext = &filter->audios[i];
                if (!multitrack && i > 0) {
                    // Signle track mode
                    break;
                }

                size_t track = i + 1;

                char audioSourceListName[15] = "audio_source_1";
                setAudioSourceListName(audioSourceListName, 15, track);

                char audioTrackListName[14] = "audio_track_1";
                setAudioTrackListName(audioTrackListName, 14, track);

                char audioDestListName[13] = "audio_dest_1";
                setAudioDestListName(audioDestListName, 13, track);

                auto audioDest = obs_data_get_string(settings, audioDestListName);
                audioContext->streaming = !strcmp(audioDest, "streaming") || !strcmp(audioDest, "both");
                audioContext->recording = !strcmp(audioDest, "recording") || !strcmp(audioDest, "both");

                auto audioSourceUuid = obs_data_get_string(settings, audioSourceListName);
                if (!strcmp(audioSourceUuid, "disabled")) {
                    // Disabled track
                    obs_log(LOG_INFO, "%s: Track %d is disabled", obs_source_get_name(filter->filterSource), track);
                    continue;

                } else if (!strcmp(audioSourceUuid, "no_audio")) {
                    // Silence audio
                    obs_log(
                        LOG_INFO, "%s: Use silence for track %d (%s)", obs_source_get_name(filter->filterSource), track,
                        audioDest
                    );

                    audioContext->capture = new AudioCapture("Silence", ai.samples_per_sec, ai.speakers);
                    audioContext->audio = audioContext->capture->getAudio();
                    audioContext->name = audioContext->capture->getName();

                } else if (!strcmp(audioSourceUuid, "master_track")) {
                    // Master audio
                    auto masterTrack = obs_data_get_int(settings, audioTrackListName);
                    if (masterTrack < 1 || masterTrack > MAX_AUDIO_MIXES) {
                        obs_log(
                            LOG_ERROR, "%s: Invalid master audio track No.%d for track %d",
                            obs_source_get_name(filter->filterSource), masterTrack, track
                        );
                        return;
                    }
                    obs_log(
                        LOG_INFO, "%s: Use master audio track No.%d for track %d (%s)",
                        obs_source_get_name(filter->filterSource), masterTrack, track, audioDest
                    );

                    audioContext->mixIndex = masterTrack - 1;
                    audioContext->audio = obs_get_audio();
                    audioContext->name = QTStr("MasterTrack%1").arg(masterTrack);

                } else if (!strcmp(audioSourceUuid, "filter")) {
                    // Filter pipline's audio
                    obs_log(
                        LOG_INFO, "%s: Use filter audio for track %d (%s)", obs_source_get_name(filter->filterSource),
                        track, audioDest
                    );

                    audioContext->capture = new FilterAudioCapture(
                        obs_source_get_name(filter->filterSource), ai.samples_per_sec, ai.speakers
                    );
                    audioContext->audio = audioContext->capture->getAudio();
                    audioContext->name = audioContext->capture->getName();

                } else {
                    // Specific source's audio
                    OBSSourceAutoRelease source = obs_get_source_by_uuid(audioSourceUuid);
                    if (!source) {
                        // Non-stopping error
                        obs_log(
                            LOG_WARNING, "%s: Ignore audio source for track %d (%s)",
                            obs_source_get_name(filter->filterSource), track, audioDest
                        );
                        continue;
                    }

                    // Use custom audio source
                    obs_log(
                        LOG_INFO, "%s: Use %s audio for track %d", obs_source_get_name(filter->filterSource),
                        obs_source_get_name(source), track
                    );

                    audioContext->capture = new SourceAudioCapture(source, ai.samples_per_sec, ai.speakers);
                    audioContext->audio = audioContext->capture->getAudio();
                    audioContext->name = audioContext->capture->getName();
                }

                if (!audioContext->audio) {
                    obs_log(
                        LOG_ERROR, "%s: Audio creation failed for track %d (%s)",
                        obs_source_get_name(filter->filterSource), track, audioDest
                    );
                    if (audioContext->capture) {
                        delete audioContext->capture;
                        audioContext->capture = nullptr;
                    }
                    return;
                }
            }
        } else {
            // Filter pipeline's audio
            obs_log(LOG_INFO, "%s: Use filter audio for track 1", obs_source_get_name(filter->filterSource));
            auto audioContext = &filter->audios[0];
            audioContext->capture =
                new FilterAudioCapture(obs_source_get_name(filter->filterSource), ai.samples_per_sec, ai.speakers);
            audioContext->audio = audioContext->capture->getAudio();
            audioContext->streaming = true;
            audioContext->recording = true;
            audioContext->name = audioContext->capture->getName();

            if (!audioContext->audio) {
                obs_log(LOG_ERROR, "%s: Audio creation failed", obs_source_get_name(filter->filterSource));
                delete audioContext->capture;
                audioContext->capture = nullptr;
                return;
            }
        }

        //--- Setup video encoder ---//
        auto video_encoder_id = obs_data_get_string(settings, "video_encoder");
        filter->videoEncoder =
            obs_video_encoder_create(video_encoder_id, obs_source_get_name(filter->filterSource), settings, nullptr);
        if (!filter->videoEncoder) {
            obs_log(LOG_ERROR, "%s: Video encoder creation failed", obs_source_get_name(filter->filterSource));
            return;
        }

        determineOutputResolution(filter, settings, &encvi);

        if (encvi.base_width == encvi.output_width && encvi.base_height == encvi.output_height) {
            // No scaling
            obs_encoder_set_scaled_size(filter->videoEncoder, 0, 0);
        } else {
            obs_log(
                LOG_DEBUG, "%s: Output resolution is %dx%d (scaling=%d)", obs_source_get_name(filter->filterSource),
                encvi.output_width, encvi.output_height, encvi.scale_type
            );
            obs_encoder_set_scaled_size(filter->videoEncoder, encvi.output_width, encvi.output_height);
            obs_encoder_set_gpu_scale_type(filter->videoEncoder, encvi.scale_type);
        }
        obs_encoder_set_video(filter->videoEncoder, filter->videoOutput);

        //--- Setup audio encoder ---//
        auto audio_encoder_id = obs_data_get_string(settings, "audio_encoder");
        auto audio_bitrate = obs_data_get_int(settings, "audio_bitrate");
        OBSDataAutoRelease audio_encoder_settings = obs_encoder_defaults(audio_encoder_id);
        obs_data_set_int(audio_encoder_settings, "bitrate", audio_bitrate);

        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &filter->audios[i];
            if (!audioContext->audio) {
                continue;
            }

            audioContext->encoder = obs_audio_encoder_create(
                audio_encoder_id, qUtf8Printable(audioContext->name), audio_encoder_settings, audioContext->mixIndex,
                nullptr
            );
            if (!audioContext->encoder) {
                obs_log(
                    LOG_ERROR, "%s: Audio encoder creation failed for track %d",
                    obs_source_get_name(filter->filterSource), i + 1
                );
                return;
            }
            obs_encoder_set_audio(audioContext->encoder, audioContext->audio);
        }

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

            size_t encIndex = 0;
            for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
                auto audioContext = &filter->audios[i];
                if (!audioContext->encoder || !audioContext->recording) {
                    continue;
                }

                obs_output_set_audio_encoder(filter->recordingOutput, audioContext->encoder, encIndex++);
            }

            if (!encIndex) {
                // No audio encoder -> fallback first available encoder
                for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
                    auto audioContext = &filter->audios[i];
                    if (audioContext->encoder) {
                        obs_log(
                            LOG_WARNING, "%s: No audio encoder selected for recording, using track %d",
                            obs_source_get_name(filter->filterSource), i + 1
                        );
                        obs_output_set_audio_encoder(filter->recordingOutput, audioContext->encoder, encIndex++);
                        break;
                    }
                }
                if (!encIndex) {
                    obs_log(LOG_ERROR, "%s: No audio encoder for recording", obs_source_get_name(filter->filterSource));
                    return;
                }
            }

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
            size_t encIndex = 0;
            for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
                auto audioContext = &filter->audios[i];
                if (!audioContext->encoder || !audioContext->streaming) {
                    continue;
                }

                obs_output_set_audio_encoder(filter->streamOutput, audioContext->encoder, encIndex++);
            }

            if (!encIndex) {
                // No audio encoder -> fallback first available encoder
                for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
                    auto audioContext = &filter->audios[i];
                    if (audioContext->encoder) {
                        obs_log(
                            LOG_WARNING, "%s: No audio encoder selected for streaming, using track %d",
                            obs_source_get_name(filter->filterSource), i + 1
                        );
                        obs_output_set_audio_encoder(filter->streamOutput, audioContext->encoder, encIndex++);
                        break;
                    }
                }
                if (!encIndex) {
                    obs_log(LOG_ERROR, "%s: No audio encoder for streaming", obs_source_get_name(filter->filterSource));
                    return;
                }
            }

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

void reconnectStreamOutput(BranchOutputFilter *filter)
{
    pthread_mutex_lock(&filter->outputMutex);
    {
        OBSMutexAutoUnlock locked(&filter->outputMutex);

        if (filter->outputActive) {
            obs_output_force_stop(filter->streamOutput);

            filter->connectAttemptingAt = os_gettime_ns();

            if (!obs_output_start(filter->streamOutput)) {
                obs_log(LOG_ERROR, "%s: Reconnect streaming output failed", obs_source_get_name(filter->filterSource));
            }
        }
    }
}

void restartRecordingOutput(BranchOutputFilter *filter)
{
    pthread_mutex_lock(&filter->outputMutex);
    {
        OBSMutexAutoUnlock locked(&filter->outputMutex);

        filter->connectAttemptingAt = os_gettime_ns();

        if (filter->recordingActive) {
            obs_output_force_stop(filter->recordingOutput);

            if (!obs_output_start(filter->recordingOutput)) {
                obs_log(LOG_ERROR, "%s: Restart recording output failed", obs_source_get_name(filter->filterSource));
            }
        }
    }
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
        obs_data_erase(recently_settings, "stream_recording");
        obs_data_erase(recently_settings, "custom_audio_source");
        obs_data_erase(recently_settings, "audio_source");
        obs_data_erase(recently_settings, "audio_track");
        obs_data_erase(recently_settings, "multitrack_audio");
        obs_data_erase(recently_settings, "audio_source_2");
        obs_data_erase(recently_settings, "audio_track_2");
        obs_data_erase(recently_settings, "audio_dest_2");
        obs_data_erase(recently_settings, "audio_source_3");
        obs_data_erase(recently_settings, "audio_track_3");
        obs_data_erase(recently_settings, "audio_dest_3");
        obs_data_erase(recently_settings, "audio_source_4");
        obs_data_erase(recently_settings, "audio_track_4");
        obs_data_erase(recently_settings, "audio_dest_4");
        obs_data_erase(recently_settings, "audio_source_5");
        obs_data_erase(recently_settings, "audio_track_5");
        obs_data_erase(recently_settings, "audio_dest_5");
        obs_data_erase(recently_settings, "audio_source_6");
        obs_data_erase(recently_settings, "audio_track_6");
        obs_data_erase(recently_settings, "audio_dest_6");
        obs_data_erase(recently_settings, "resolution");
        obs_data_erase(recently_settings, "custom_width");
        obs_data_erase(recently_settings, "custom_height");
        obs_data_erase(recently_settings, "downscale_filter");
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

// Controlling output status here.
// Start / Stop should only heppen in this function as possible because rapid manipulation caused crash easily.
// NOTE: Becareful this function is called so offen.
void intervalTask(BranchOutputFilter *filter)
{
    // Block output initiation until filter is active.
    if (!filter->initialized) {
        return;
    }

    auto interlockType = statusDock ? statusDock->getInterlockType() : INTERLOCK_TYPE_ALWAYS_ON;
    auto sourceEnabled = obs_source_enabled(filter->filterSource);

    if (!filter->outputActive && !filter->recordingActive) {
        // Evaluate start condition
        auto parent = obs_filter_get_parent(filter->filterSource);
        if (!parent || !sourceInFrontend(parent)) {
            // Ignore when source in no longer exists in frontend
            return;
        }

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

    } else {
        // Evaluate stop or restart condition
        auto streamingAlive = filter->streamOutput && obs_output_active(filter->streamOutput);
        auto recordingAlive = filter->recordingOutput && obs_output_active(filter->recordingOutput);

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

            if (streamingAlive || recordingAlive) {
                // Monitoring source
                auto parent = obs_filter_get_parent(filter->filterSource);
                auto width = obs_source_get_width(parent);
                width += (width & 1);
                uint32_t height = obs_source_get_height(parent);
                height += (height & 1);

                if (!width || !height || !sourceInFrontend(parent)) {
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

            if (filter->recordingActive && !recordingAlive) {
                // Restart recording
                obs_log(
                    LOG_INFO, "%s: Attempting reactivate the recording output",
                    obs_source_get_name(filter->filterSource)
                );
                restartRecordingOutput(filter);
                return;
            }

            if (filter->outputActive && !streamingAlive) {
                // Reconnect streaming
                obs_log(
                    LOG_INFO, "%s: Attempting reactivate the stream output", obs_source_get_name(filter->filterSource)
                );
                reconnectStreamOutput(filter);
                return;
            }

        } else {
            if (streamingAlive || recordingAlive) {
                // Clicked filter's "Eye" icon (Hide)
                stopOutput(filter);
                return;
            }
        }
    }
}

//--- OBS Plugin Callbacks ---//

void *create(obs_data_t *settings, obs_source_t *source)
{
    // DO NOT use obs_filter_get_parent() in this function (It'll return nullptr)
    obs_log(LOG_DEBUG, "%s: Filter creating", obs_source_get_name(source));
    obs_log(LOG_DEBUG, "filter_settings_json=%s", obs_data_get_json(settings));

    auto filter = (BranchOutputFilter *)bzalloc(sizeof(BranchOutputFilter));
    pthread_mutex_init(&filter->outputMutex, nullptr);

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

    obs_log(LOG_INFO, "%s: Filter created", obs_source_get_name(filter->filterSource));
    return filter;
}

void filterAdd(void *data, obs_source_t *source)
{
    // Register to output status dock
    auto filter = (BranchOutputFilter *)data;

    // Do not start timer for private sources
    // Do not register private sources to status dock
    if (sourceIsPrivate(source)) {
        obs_log(
            LOG_DEBUG, "%s: Ignore adding to private source '%s'", obs_source_get_name(filter->filterSource),
            obs_source_get_name(source)
        );
        return;
    }

    obs_log(
        LOG_DEBUG, "%s: Filter adding to '%s'", obs_source_get_name(filter->filterSource), obs_source_get_name(source)
    );

    // Start interval timer here
    filter->intervalTimer = new QTimer();
    filter->intervalTimer->setInterval(TASK_INTERVAL_MS);
    filter->intervalTimer->start();
    QObject::connect(filter->intervalTimer, &QTimer::timeout, [filter]() { intervalTask(filter); });

    if (statusDock) {
        // Show in status dock (Thread-safe way)
        QMetaObject::invokeMethod(statusDock, "addFilter", Qt::QueuedConnection, Q_ARG(BranchOutputFilter *, filter));
    }

    obs_log(
        LOG_INFO, "%s: Filter added to '%s'", obs_source_get_name(filter->filterSource), obs_source_get_name(source)
    );
}

void update(void *data, obs_data_t *settings)
{
    auto filter = (BranchOutputFilter *)data;
    auto parent = obs_filter_get_parent(filter->filterSource);

    // Do not save settings for private sources
    if (sourceIsPrivate(parent)) {
        obs_log(
            LOG_DEBUG, "%s: Ignore updating in private source '%s'", obs_source_get_name(filter->filterSource),
            obs_source_get_name(parent)
        );
        return;
    }

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

void videoRender(void *data, gs_effect_t *)
{
    auto filter = (BranchOutputFilter *)data;
    obs_source_skip_video_filter(filter->filterSource);
}

void filterRemove(void *data, obs_source_t *source)
{
    auto filter = (BranchOutputFilter *)data;

    obs_log(
        LOG_DEBUG, "%s: Filter removing from '%s'", obs_source_get_name(filter->filterSource),
        obs_source_get_name(source)
    );

    if (statusDock) {
        // Unregister from output status dock (Thread-safe way)
        QMetaObject::invokeMethod(statusDock, "removeFilter", Qt::QueuedConnection, Q_ARG(BranchOutputFilter *, filter));
    }

    obs_log(
        LOG_INFO, "%s: Filter removed from '%s'", obs_source_get_name(filter->filterSource), obs_source_get_name(source)
    );
}

void destroy(void *data)
{
    auto filter = (BranchOutputFilter *)data;
    auto source = filter->filterSource;
    obs_log(LOG_DEBUG, "%s: Filter destroying", obs_source_get_name(source));

    if (filter->intervalTimer) {
        // Stop interval timer (in proper thread)
        filter->intervalTimer->deleteLater();
    }

    stopOutput(filter);
    pthread_mutex_destroy(&filter->outputMutex);
    bfree(filter);

    obs_log(LOG_INFO, "%s: Filter destroyed", obs_source_get_name(source));
}

obs_source_info createFilterInfo()
{
    obs_source_info filter_info = {0};

    filter_info.id = FILTER_ID;
    filter_info.type = OBS_SOURCE_TYPE_FILTER;
    filter_info.output_flags = OBS_SOURCE_VIDEO; // Didn't work OBS_SOURCE_DO_NOT_DUPLICATE for filter

    filter_info.get_name = [](void *) {
        return "Branch Output";
    };

    filter_info.create = create;
    filter_info.filter_add = filterAdd;
    filter_info.update = update;
    filter_info.video_render = videoRender;
    filter_info.filter_remove = filterRemove;
    filter_info.destroy = destroy;

    filter_info.get_properties = getProperties;
    filter_info.get_defaults = getDefaults;

    filter_info.filter_audio = audioFilterCallback;

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
    qRegisterMetaType<BranchOutputFilter *>();

    statusDock = createOutputStatusDock();
}

void obs_module_unload()
{
    obs_log(LOG_INFO, "Plugin unloaded");
}
