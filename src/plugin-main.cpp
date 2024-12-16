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

//--- BranchOutputFilter class ---//

BranchOutputFilter::BranchOutputFilter(obs_data_t *settings, obs_source_t *source, QObject *parent)
    : QObject(parent),
      filterSource(source),
      initialized(false),
      outputActive(false),
      recordingActive(false),
      storedSettingsRev(0),
      activeSettingsRev(0),
      intervalTimer(nullptr),
      connectAttemptingAt(0),
      recordingOutput(nullptr),
      streamOutput(nullptr),
      service(nullptr),
      videoEncoder(nullptr),
      videoOutput(nullptr),
      view(nullptr),
      width(0),
      height(0),
      hotkeyPairId(OBS_INVALID_HOTKEY_PAIR_ID)
{
    // DO NOT use obs_filter_get_parent() in this function (It'll return nullptr)
    obs_log(LOG_DEBUG, "%s: BranchOutputFilter creating", obs_source_get_name(source));
    obs_log(LOG_DEBUG, "filter_settings_json=%s", obs_data_get_json(settings));

    // Do not use memset
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        audios[i] = {0};
    }

    pthread_mutex_init(&outputMutex, nullptr);

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
    initialized = !!strlen(obs_data_get_string(settings, "server")) || obs_data_get_bool(settings, "stream_recording");

    obs_log(LOG_INFO, "%s: BranchOutputFilter created", obs_source_get_name(source));
}

BranchOutputFilter::~BranchOutputFilter()
{
    obs_log(LOG_DEBUG, "%s: BranchOutputFilter destroying", obs_source_get_name(filterSource));

    if (intervalTimer) {
        // Stop interval timer (in proper thread)
        intervalTimer->deleteLater();
    }

    stopOutput();
    pthread_mutex_destroy(&outputMutex);

    obs_log(LOG_INFO, "%s: BranchOutputFilter destroyed", obs_source_get_name(filterSource));
}

void BranchOutputFilter::addCallback(obs_source_t *source)
{
    // Do not start timer for private sources
    // Do not register private sources to status dock
    if (sourceIsPrivate(source)) {
        obs_log(
            LOG_DEBUG, "%s: Ignore adding to private source '%s'", obs_source_get_name(filterSource),
            obs_source_get_name(source)
        );
        return;
    }

    obs_log(LOG_DEBUG, "%s: Filter adding to '%s'", obs_source_get_name(filterSource), obs_source_get_name(source));

    // Start interval timer here
    intervalTimer = new QTimer(this);
    intervalTimer->setInterval(TASK_INTERVAL_MS);
    intervalTimer->start();
    connect(intervalTimer, SIGNAL(timeout()), this, SLOT(onIntervalTimerTimeout()));

    // Register to status dock
    if (statusDock) {
        // Show in status dock (Thread-safe way)
        QMetaObject::invokeMethod(statusDock, "addFilter", Qt::QueuedConnection, Q_ARG(BranchOutputFilter *, this));
    }

    // Register hotkeys
    registerHotkey();
    // Track filter renames for hotkey settings
    filterRenamedSignal.Connect(
        obs_source_get_signal_handler(filterSource), "rename",
        [](void *_data, calldata_t *) {
            auto _filter = (BranchOutputFilter *)_data;
            _filter->registerHotkey();
        },
        this
    );

    obs_log(LOG_INFO, "%s: Filter added to '%s'", obs_source_get_name(filterSource), obs_source_get_name(source));
}

void BranchOutputFilter::updateCallback(obs_data_t *settings)
{
    auto source = obs_filter_get_parent(filterSource);

    // Do not save settings for private sources
    if (sourceIsPrivate(source)) {
        obs_log(
            LOG_DEBUG, "%s: Ignore updating in private source '%s'", obs_source_get_name(filterSource),
            obs_source_get_name(source)
        );
        return;
    }

    obs_log(LOG_DEBUG, "%s: Filter updating", obs_source_get_name(filterSource));

    // It's unwelcome to do stopping output during attempting connect to service.
    // So we just count up revision (Settings will be applied on videoTick())
    storedSettingsRev++;

    // Save settings as default
    OBSString config_dir_path = obs_module_get_config_path(obs_current_module(), "");
    os_mkdirs(config_dir_path);

    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    obs_data_save_json_safe(settings, path, "tmp", "bak");

    obs_log(LOG_INFO, "%s: Filter updated", obs_source_get_name(filterSource));
}

void BranchOutputFilter::videoRenderCallback(gs_effect_t *)
{
    obs_source_skip_video_filter(filterSource);
}

void BranchOutputFilter::removeCallback(obs_source_t *source)
{
    obs_log(LOG_DEBUG, "%s: Filter removing from '%s'", obs_source_get_name(filterSource), obs_source_get_name(source));

    if (statusDock) {
        // Unregister from output status dock (Thread-safe way)
        QMetaObject::invokeMethod(statusDock, "removeFilter", Qt::QueuedConnection, Q_ARG(BranchOutputFilter *, this));
    }

    if (hotkeyPairId != OBS_INVALID_HOTKEY_PAIR_ID) {
        // Unregsiter hotkeys
        obs_hotkey_pair_unregister(hotkeyPairId);
    }

    obs_log(LOG_INFO, "%s: Filter removed from '%s'", obs_source_get_name(filterSource), obs_source_get_name(source));
}

void BranchOutputFilter::stopOutput()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        obs_source_t *parent = obs_filter_get_parent(filterSource);
        connectAttemptingAt = 0;

        if (recordingOutput) {
            if (recordingActive) {
                obs_source_dec_showing(parent);
                obs_output_stop(recordingOutput);
            }
        }
        recordingOutput = nullptr;

        if (streamOutput) {
            if (outputActive) {
                obs_source_dec_showing(parent);
                obs_output_stop(streamOutput);
            }
        }
        streamOutput = nullptr;
        service = nullptr;

        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &audios[i];
            audioContext->encoder = nullptr;

            if (audioContext->capture) {
                delete audioContext->capture;
                audioContext->capture = nullptr;
            }
        }

        videoEncoder = nullptr;

        if (view) {
            obs_view_set_source(view, 0, nullptr);
            obs_view_remove(view);
        }

        view = nullptr;

        if (recordingActive) {
            recordingActive = false;
            obs_log(LOG_INFO, "%s: Stopping recording output succeeded", obs_source_get_name(filterSource));
        }

        if (outputActive) {
            outputActive = false;
            obs_log(LOG_INFO, "%s: Stopping stream output succeeded", obs_source_get_name(filterSource));
        }
    }
}

obs_data_t *BranchOutputFilter::createRecordingSettings(obs_data_t *settings)
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
    QString sourceName = obs_source_get_name(obs_filter_get_parent(filterSource));
    QString filterName = obs_source_get_name(filterSource);
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

void BranchOutputFilter::determineOutputResolution(obs_data_t *settings, obs_video_info *ovi)
{
    auto resolution = obs_data_get_string(settings, "resolution");
    if (!strcmp(resolution, "custom")) {
        // Custom resolution
        ovi->output_width = (uint32_t)obs_data_get_int(settings, "custom_width");
        ovi->output_height = (uint32_t)obs_data_get_int(settings, "custom_height");

    } else if (!strcmp(resolution, "output")) {
        // Nothing to do

    } else if (!strcmp(resolution, "canvas")) {
        // Copy canvas resolution
        ovi->output_width = ovi->base_width;
        ovi->output_height = ovi->base_height;

    } else if (!strcmp(resolution, "three_quarters")) {
        // Rescale source resolution
        ovi->output_width = width * 3 / 4;
        ovi->output_height = height * 3 / 4;

    } else if (!strcmp(resolution, "half")) {
        // Rescale source resolution
        ovi->output_width = width / 2;
        ovi->output_height = height / 2;

    } else if (!strcmp(resolution, "quarter")) {
        // Rescale source resolution
        ovi->output_width = width / 4;
        ovi->output_height = height / 4;

    } else {
        // Copy source resolution
        ovi->output_width = width;
        ovi->output_height = height;
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

void BranchOutputFilter::startOutput(obs_data_t *settings)
{
    // Force release references
    stopOutput();

    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        // Abort when obs initializing or filter disabled.
        if (!obs_initialized() || !obs_source_enabled(filterSource) || outputActive || recordingActive) {
            obs_log(LOG_ERROR, "%s: Ignore unavailable filter", obs_source_get_name(filterSource));
            return;
        }

        // Retrieve filter source
        auto parent = obs_filter_get_parent(filterSource);
        if (!parent) {
            obs_log(LOG_ERROR, "%s: Filter source not found", obs_source_get_name(filterSource));
            return;
        }

        // Ignore private sources
        if (sourceIsPrivate(parent)) {
            obs_log(LOG_ERROR, "%s: Ignore private source", obs_source_get_name(filterSource));
            return;
        }

        // Mandatory paramters
        if (!strlen(obs_data_get_string(settings, "server")) && !obs_data_get_string(settings, "stream_recording")) {
            obs_log(LOG_ERROR, "%s: Nothing to do", obs_source_get_name(filterSource));
            return;
        }

        obs_video_info ovi = {0};
        if (!obs_get_video_info(&ovi)) {
            // Abort when no video situation
            obs_log(LOG_ERROR, "%s: No video", obs_source_get_name(filterSource));
            return;
        }

        obs_video_info encvi = ovi;

        // Round up to a multiple of 2
        width = obs_source_get_width(parent);
        width += (width & 1);
        // Round up to a multiple of 2
        height = obs_source_get_height(parent);
        height += (height & 1);

        ovi.base_width = width;
        ovi.base_height = height;
        ovi.output_width = encvi.base_width = width;
        ovi.output_height = encvi.base_height = height;

        if (width == 0 || height == 0 || ovi.fps_den == 0 || ovi.fps_num == 0) {
            // Abort when invalid video parameters situation
            obs_log(LOG_ERROR, "%s: Invalid video spec", obs_source_get_name(filterSource));
            return;
        }

        // Update active revision with stored settings.
        activeSettingsRev = storedSettingsRev;

        //--- Create service and open stream output ---//
        if (!!strlen(obs_data_get_string(settings, "server"))) {
            // Create service - We always use "rtmp_custom" as service
            service = obs_service_create("rtmp_custom", obs_source_get_name(filterSource), settings, nullptr);
            if (!service) {
                obs_log(LOG_ERROR, "%s: Service creation failed", obs_source_get_name(filterSource));
                return;
            }
            obs_service_apply_encoder_settings(service, settings, nullptr);

            // Determine output type
            auto type = obs_service_get_preferred_output_type(service);
            if (!type) {
                type = "rtmp_output";
                auto url = obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
                if (url != nullptr && !strncmp(url, FTL_PROTOCOL, strlen(FTL_PROTOCOL))) {
                    type = "ftl_output";
                } else if (url != nullptr && strncmp(url, RTMP_PROTOCOL, strlen(RTMP_PROTOCOL))) {
                    type = "ffmpeg_mpegts_muxer";
                }
            }

            // Create stream output
            streamOutput = obs_output_create(type, obs_source_get_name(filterSource), settings, nullptr);
            if (!streamOutput) {
                obs_log(LOG_ERROR, "%s: Stream output creation failed", obs_source_get_name(filterSource));
                return;
            }
            obs_output_set_reconnect_settings(streamOutput, OUTPUT_MAX_RETRIES, OUTPUT_RETRY_DELAY_SECS);
            obs_output_set_service(streamOutput, service);
            connectAttemptingAt = os_gettime_ns();
        }

        //--- Open video output ---//
        // Create view and associate it with filter source
        view = obs_view_create();

        obs_view_set_source(view, 0, parent);
        videoOutput = obs_view_add2(view, &ovi);
        if (!videoOutput) {
            obs_log(LOG_ERROR, "%s: Video output association failed", obs_source_get_name(filterSource));
            return;
        }

        //--- Open audio output(s) ---//
        // Do not use memset
        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            audios[i] = {0};
        }

        obs_audio_info ai = {0};
        if (!obs_get_audio_info(&ai)) {
            obs_log(LOG_ERROR, "%s: Failed to get audio info", obs_source_get_name(filterSource));
            return;
        }

        if (obs_data_get_bool(settings, "custom_audio_source")) {
            // Apply custom audio source
            bool multitrack = obs_data_get_bool(settings, "multitrack_audio");

            for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
                auto audioContext = &audios[i];
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
                    obs_log(LOG_INFO, "%s: Track %d is disabled", obs_source_get_name(filterSource), track);
                    continue;

                } else if (!strcmp(audioSourceUuid, "no_audio")) {
                    // Silence audio
                    obs_log(
                        LOG_INFO, "%s: Use silence for track %d (%s)", obs_source_get_name(filterSource), track,
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
                            obs_source_get_name(filterSource), masterTrack, track
                        );
                        return;
                    }
                    obs_log(
                        LOG_INFO, "%s: Use master audio track No.%d for track %d (%s)",
                        obs_source_get_name(filterSource), masterTrack, track, audioDest
                    );

                    audioContext->mixIndex = masterTrack - 1;
                    audioContext->audio = obs_get_audio();
                    audioContext->name = QTStr("MasterTrack%1").arg(masterTrack);

                } else if (!strcmp(audioSourceUuid, "filter")) {
                    // Filter pipline's audio
                    obs_log(
                        LOG_INFO, "%s: Use filter audio for track %d (%s)", obs_source_get_name(filterSource), track,
                        audioDest
                    );

                    audioContext->capture =
                        new FilterAudioCapture(obs_source_get_name(filterSource), ai.samples_per_sec, ai.speakers);
                    audioContext->audio = audioContext->capture->getAudio();
                    audioContext->name = audioContext->capture->getName();

                } else {
                    // Specific source's audio
                    OBSSourceAutoRelease source = obs_get_source_by_uuid(audioSourceUuid);
                    if (!source) {
                        // Non-stopping error
                        obs_log(
                            LOG_WARNING, "%s: Ignore audio source for track %d (%s)", obs_source_get_name(filterSource),
                            track, audioDest
                        );
                        continue;
                    }

                    // Use custom audio source
                    obs_log(
                        LOG_INFO, "%s: Use %s audio for track %d", obs_source_get_name(filterSource),
                        obs_source_get_name(source), track
                    );

                    audioContext->capture = new SourceAudioCapture(source, ai.samples_per_sec, ai.speakers);
                    audioContext->audio = audioContext->capture->getAudio();
                    audioContext->name = audioContext->capture->getName();
                }

                if (!audioContext->audio) {
                    obs_log(
                        LOG_ERROR, "%s: Audio creation failed for track %d (%s)", obs_source_get_name(filterSource),
                        track, audioDest
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
            obs_log(LOG_INFO, "%s: Use filter audio for track 1", obs_source_get_name(filterSource));
            auto audioContext = &audios[0];
            audioContext->capture =
                new FilterAudioCapture(obs_source_get_name(filterSource), ai.samples_per_sec, ai.speakers);
            audioContext->audio = audioContext->capture->getAudio();
            audioContext->streaming = true;
            audioContext->recording = true;
            audioContext->name = audioContext->capture->getName();

            if (!audioContext->audio) {
                obs_log(LOG_ERROR, "%s: Audio creation failed", obs_source_get_name(filterSource));
                delete audioContext->capture;
                audioContext->capture = nullptr;
                return;
            }
        }

        //--- Setup video encoder ---//
        auto video_encoder_id = obs_data_get_string(settings, "video_encoder");
        videoEncoder = obs_video_encoder_create(video_encoder_id, obs_source_get_name(filterSource), settings, nullptr);
        if (!videoEncoder) {
            obs_log(LOG_ERROR, "%s: Video encoder creation failed", obs_source_get_name(filterSource));
            return;
        }

        determineOutputResolution(settings, &encvi);

        if (encvi.base_width == encvi.output_width && encvi.base_height == encvi.output_height) {
            // No scaling
            obs_encoder_set_scaled_size(videoEncoder, 0, 0);
        } else {
            obs_log(
                LOG_DEBUG, "%s: Output resolution is %dx%d (scaling=%d)", obs_source_get_name(filterSource),
                encvi.output_width, encvi.output_height, encvi.scale_type
            );
            obs_encoder_set_scaled_size(videoEncoder, encvi.output_width, encvi.output_height);
            obs_encoder_set_gpu_scale_type(videoEncoder, encvi.scale_type);
        }
        obs_encoder_set_video(videoEncoder, videoOutput);

        //--- Setup audio encoder ---//
        auto audio_encoder_id = obs_data_get_string(settings, "audio_encoder");
        auto audio_bitrate = obs_data_get_int(settings, "audio_bitrate");
        OBSDataAutoRelease audio_encoder_settings = obs_encoder_defaults(audio_encoder_id);
        obs_data_set_int(audio_encoder_settings, "bitrate", audio_bitrate);

        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &audios[i];
            if (!audioContext->audio) {
                continue;
            }

            audioContext->encoder = obs_audio_encoder_create(
                audio_encoder_id, qUtf8Printable(audioContext->name), audio_encoder_settings, audioContext->mixIndex,
                nullptr
            );
            if (!audioContext->encoder) {
                obs_log(
                    LOG_ERROR, "%s: Audio encoder creation failed for track %d", obs_source_get_name(filterSource),
                    i + 1
                );
                return;
            }
            obs_encoder_set_audio(audioContext->encoder, audioContext->audio);
        }

        //--- Create recording output (if requested) ---//
        if (obs_data_get_bool(settings, "stream_recording")) {
            auto recFormat = obs_data_get_string(settings, "rec_format");
            const char *outputId = !strcmp(recFormat, "hybrid_mp4") ? "mp4_output" : "ffmpeg_muxer";

            OBSDataAutoRelease recordingSettings = createRecordingSettings(settings);
            recordingOutput =
                obs_output_create(outputId, obs_source_get_name(filterSource), recordingSettings, nullptr);
            if (!recordingOutput) {
                obs_log(LOG_ERROR, "%s: Recording output creation failed", obs_source_get_name(filterSource));
                return;
            }

            size_t encIndex = 0;
            for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
                auto audioContext = &audios[i];
                if (!audioContext->encoder || !audioContext->recording) {
                    continue;
                }

                obs_output_set_audio_encoder(recordingOutput, audioContext->encoder, encIndex++);
            }

            if (!encIndex) {
                // No audio encoder -> fallback first available encoder
                for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
                    auto audioContext = &audios[i];
                    if (audioContext->encoder) {
                        obs_log(
                            LOG_WARNING, "%s: No audio encoder selected for recording, using track %d",
                            obs_source_get_name(filterSource), i + 1
                        );
                        obs_output_set_audio_encoder(recordingOutput, audioContext->encoder, encIndex++);
                        break;
                    }
                }
                if (!encIndex) {
                    obs_log(LOG_ERROR, "%s: No audio encoder for recording", obs_source_get_name(filterSource));
                    return;
                }
            }

            obs_output_set_video_encoder(recordingOutput, videoEncoder);

            // Start recording output
            if (obs_output_start(recordingOutput)) {
                recordingActive = true;
                obs_source_inc_showing(obs_filter_get_parent(filterSource));
                obs_log(LOG_INFO, "%s: Starting recording output succeeded", obs_source_get_name(filterSource));
            } else {
                obs_log(LOG_ERROR, "%s: Starting recording output failed", obs_source_get_name(filterSource));
            }
        }

        //--- Start streaming output (if requested) ---//
        if (streamOutput) {
            size_t encIndex = 0;
            for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
                auto audioContext = &audios[i];
                if (!audioContext->encoder || !audioContext->streaming) {
                    continue;
                }

                obs_output_set_audio_encoder(streamOutput, audioContext->encoder, encIndex++);
            }

            if (!encIndex) {
                // No audio encoder -> fallback first available encoder
                for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
                    auto audioContext = &audios[i];
                    if (audioContext->encoder) {
                        obs_log(
                            LOG_WARNING, "%s: No audio encoder selected for streaming, using track %d",
                            obs_source_get_name(filterSource), i + 1
                        );
                        obs_output_set_audio_encoder(streamOutput, audioContext->encoder, encIndex++);
                        break;
                    }
                }
                if (!encIndex) {
                    obs_log(LOG_ERROR, "%s: No audio encoder for streaming", obs_source_get_name(filterSource));
                    return;
                }
            }

            obs_output_set_video_encoder(streamOutput, videoEncoder);

            // Start streaming output
            if (obs_output_start(streamOutput)) {
                outputActive = true;
                obs_source_inc_showing(obs_filter_get_parent(filterSource));
                obs_log(LOG_INFO, "%s: Starting streaming output succeeded", obs_source_get_name(filterSource));
            } else {
                obs_log(LOG_ERROR, "%s: Starting streaming output failed", obs_source_get_name(filterSource));
            }
        }
    }
}

void BranchOutputFilter::reconnectStreamOutput()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (outputActive) {
            obs_output_force_stop(streamOutput);

            connectAttemptingAt = os_gettime_ns();

            if (!obs_output_start(streamOutput)) {
                obs_log(LOG_ERROR, "%s: Reconnect streaming output failed", obs_source_get_name(filterSource));
            }
        }
    }
}

void BranchOutputFilter::restartRecordingOutput()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        connectAttemptingAt = os_gettime_ns();

        if (recordingActive) {
            obs_output_force_stop(recordingOutput);

            if (!obs_output_start(recordingOutput)) {
                obs_log(LOG_ERROR, "%s: Restart recording output failed", obs_source_get_name(filterSource));
            }
        }
    }
}

void BranchOutputFilter::loadRecently(obs_data_t *settings)
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

void BranchOutputFilter::restartOutput()
{
    if (outputActive || recordingActive) {
        stopOutput();
    }

    OBSDataAutoRelease settings = obs_source_get_settings(filterSource);
    if (!!strlen(obs_data_get_string(settings, "server")) || obs_data_get_bool(settings, "stream_recording")) {
        startOutput(settings);
    }
}

bool BranchOutputFilter::connectAttemptingTimedOut()
{
    return connectAttemptingAt && os_gettime_ns() - connectAttemptingAt > CONNECT_ATTEMPTING_TIMEOUT_NS;
}

// Controlling output status here.
// Start / Stop should only heppen in this function as possible because rapid manipulation caused crash easily.
// NOTE: Becareful this function is called so offen.
void BranchOutputFilter::onIntervalTimerTimeout()
{
    // Block output initiation until filter is active.
    if (!initialized) {
        return;
    }

    auto interlockType = statusDock ? statusDock->getInterlockType() : INTERLOCK_TYPE_ALWAYS_ON;
    auto sourceEnabled = obs_source_enabled(filterSource);

    if (!outputActive && !recordingActive) {
        // Evaluate start condition
        auto parent = obs_filter_get_parent(filterSource);
        if (!parent || !sourceInFrontend(parent)) {
            // Ignore when source in no longer exists in frontend
            return;
        }

        if (sourceEnabled) {
            // Clicked filter's "Eye" icon (Show)
            // Check interlock condition
            if (interlockType == INTERLOCK_TYPE_STREAMING) {
                if (obs_frontend_streaming_active()) {
                    restartOutput();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_RECORDING) {
                if (obs_frontend_recording_active()) {
                    restartOutput();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_STREAMING_RECORDING) {
                if (obs_frontend_streaming_active() || obs_frontend_recording_active()) {
                    restartOutput();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_VIRTUAL_CAM) {
                if (obs_frontend_virtualcam_active()) {
                    restartOutput();
                    return;
                }
            } else {
                restartOutput();
                return;
            }
        }

    } else {
        // Evaluate stop or restart condition
        auto streamingAlive = streamOutput && obs_output_active(streamOutput);
        auto recordingAlive = recordingOutput && obs_output_active(recordingOutput);

        if (sourceEnabled) {
            if (outputActive && !connectAttemptingTimedOut()) {
                return;
            }

            // Check interlock condition
            if (interlockType == INTERLOCK_TYPE_STREAMING) {
                if (!obs_frontend_streaming_active()) {
                    // Stop output when streaming is not active
                    stopOutput();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_RECORDING) {
                if (!obs_frontend_recording_active()) {
                    // Stop output when recording is not active
                    stopOutput();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_STREAMING_RECORDING) {
                if (!obs_frontend_streaming_active() && !obs_frontend_recording_active()) {
                    // Stop output when streaming and recording are not active
                    stopOutput();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_VIRTUAL_CAM) {
                if (!obs_frontend_virtualcam_active()) {
                    // Stop output when virtual cam is not active
                    stopOutput();
                    return;
                }
            }

            if (activeSettingsRev < storedSettingsRev) {
                // Settings has been changed
                obs_log(LOG_INFO, "%s: Settings change detected, Attempting restart", obs_source_get_name(filterSource));
                restartOutput();
                return;
            }

            if (streamingAlive || recordingAlive) {
                // Monitoring source
                auto parent = obs_filter_get_parent(filterSource);
                auto sourceWidth = obs_source_get_width(parent);
                sourceWidth += (sourceWidth & 1);
                uint32_t sourceHeight = obs_source_get_height(parent);
                sourceHeight += (sourceHeight & 1);

                if (!sourceWidth || !sourceHeight || !sourceInFrontend(parent)) {
                    // Stop output when source resolution is zero or source had been removed
                    stopOutput();
                    return;
                }

                if (width != sourceWidth || height != sourceHeight) {
                    // Restart output when source resolution was changed.
                    obs_log(LOG_INFO, "%s: Attempting restart the stream output", obs_source_get_name(filterSource));
                    OBSDataAutoRelease settings = obs_source_get_settings(filterSource);
                    startOutput(settings);
                    return;
                }
            }

            if (recordingActive && !recordingAlive) {
                // Restart recording
                obs_log(LOG_INFO, "%s: Attempting reactivate the recording output", obs_source_get_name(filterSource));
                restartRecordingOutput();
                return;
            }

            if (outputActive && !streamingAlive) {
                // Reconnect streaming
                obs_log(LOG_INFO, "%s: Attempting reactivate the stream output", obs_source_get_name(filterSource));
                reconnectStreamOutput();
                return;
            }

        } else {
            if (streamingAlive || recordingAlive) {
                // Clicked filter's "Eye" icon (Hide)
                stopOutput();
                return;
            }
        }
    }
}

bool BranchOutputFilter::onEnableFilterHotkeyPressed(void *data, obs_hotkey_pair_id, obs_hotkey *, bool pressed)
{
    if (!pressed) {
        return false;
    }

    BranchOutputFilter *filter = (BranchOutputFilter *)data;
    if (obs_source_enabled(filter->filterSource)) {
        return false;
    }

    obs_source_set_enabled(filter->filterSource, true);
    return true;
}

bool BranchOutputFilter::onDisableFilterHotkeyPressed(void *data, obs_hotkey_pair_id, obs_hotkey *, bool pressed)
{
    if (!pressed) {
        return false;
    }

    BranchOutputFilter *filter = (BranchOutputFilter *)data;
    if (!obs_source_enabled(filter->filterSource)) {
        return false;
    }

    obs_source_set_enabled(filter->filterSource, false);
    return true;
}

void BranchOutputFilter::registerHotkey()
{
    if (hotkeyPairId != OBS_INVALID_HOTKEY_PAIR_ID) {
        // Unregsiter previous
        obs_hotkey_pair_unregister(hotkeyPairId);
    }

    auto name0 = QString("EnableFilter.%1").arg(obs_source_get_uuid(filterSource));
    auto description0 = QString(obs_module_text("EnableHotkey")).arg(obs_source_get_name(filterSource));
    auto name1 = QString("DisableFilter.%1").arg(obs_source_get_uuid(filterSource));
    auto description1 = QString(obs_module_text("DisableHotkey")).arg(obs_source_get_name(filterSource));

    hotkeyPairId = obs_hotkey_pair_register_source(
        obs_filter_get_parent(filterSource), qUtf8Printable(name0), qUtf8Printable(description0), qUtf8Printable(name1),
        qUtf8Printable(description1), onEnableFilterHotkeyPressed, onDisableFilterHotkeyPressed, this, this
    );
}

// Callback from filter audio
obs_audio_data *BranchOutputFilter::audioFilterCallback(void *param, obs_audio_data *audioData)
{
    auto filter = (BranchOutputFilter *)param;

    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        auto audioContext = &filter->audios[i];
        if (audioContext->capture && !audioContext->capture->hasSource()) {
            audioContext->capture->pushAudio(audioData);
        }
    }

    return audioData;
}

obs_source_info BranchOutputFilter::createFilterInfo()
{
    obs_source_info info = {0};

    info.id = FILTER_ID;
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_VIDEO; // Didn't work OBS_SOURCE_DO_NOT_DUPLICATE for filter

    info.get_name = [](void *) {
        return "Branch Output";
    };

    info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
        return new BranchOutputFilter(settings, source);
    };
    info.filter_add = [](void *data, obs_source_t *source) {
        auto filter = (BranchOutputFilter *)data;
        filter->addCallback(source);
    };
    info.update = [](void *data, obs_data_t *settings) {
        auto filter = (BranchOutputFilter *)data;
        filter->updateCallback(settings);
    };
    info.video_render = [](void *data, gs_effect_t *effect) {
        auto filter = (BranchOutputFilter *)data;
        filter->videoRenderCallback(effect);
    };
    info.filter_remove = [](void *data, obs_source_t *source) {
        auto filter = (BranchOutputFilter *)data;
        filter->removeCallback(source);
    };
    info.destroy = [](void *data) {
        auto filter = (BranchOutputFilter *)data;
        filter->deleteLater();
    };

    info.get_properties = [](void *data) -> obs_properties_t * {
        auto filter = (BranchOutputFilter *)data;
        return filter->getProperties();
    };
    info.get_defaults = BranchOutputFilter::getDefaults;

    info.filter_audio = BranchOutputFilter::audioFilterCallback;

    return info;
}

//--- OBS Plugin Callbacks ---//

obs_source_info filterInfo;

bool obs_module_load()
{
    filterInfo = BranchOutputFilter::createFilterInfo();
    obs_register_source(&filterInfo);

    obs_log(LOG_INFO, "Plugin loaded successfully (version %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_post_load()
{
    qRegisterMetaType<BranchOutputFilter *>();

    statusDock = BranchOutputFilter::createOutputStatusDock();
}

void obs_module_unload()
{
    obs_log(LOG_INFO, "Plugin unloaded");
}
