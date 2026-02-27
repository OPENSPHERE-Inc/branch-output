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

#include <QDateTime>

#include "audio/audio-capture.hpp"
#include "video/filter-video-capture.hpp"
#include "plugin-support.h"
#include "plugin-main.hpp"
#include "utils.hpp"

#define SETTINGS_JSON_NAME "recently.json"
#define FILTER_ID "osi_branch_output"
#define OUTPUT_MAX_RETRIES 7
#define OUTPUT_RETRY_DELAY_SECS 1
#define RECONNECT_ATTEMPTING_TIMEOUT_NS 2000000000ULL
#define AVAILAVILITY_CHECK_INTERVAL_NS 1000000000ULL
#define TASK_INTERVAL_MS 1000

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

BranchOutputStatusDock *statusDock = nullptr;
pthread_mutex_t pluginMutex;

//--- BranchOutputFilter class ---//

BranchOutputFilter::BranchOutputFilter(obs_data_t *settings, obs_source_t *source, QObject *parent)
    : QObject(parent),
      name(obs_source_get_name(source)),
      filterSource(source),
      initialized(false),
      recordingActive(false),
      recordingPending(false),
      storedSettingsRev(0),
      activeSettingsRev(0),
      intervalTimer(nullptr),
      streamingStopping(false),
      blankingOutputActive(false),
      blankingAudioMuted(false),
      recordingOutput(nullptr),
      videoEncoder(nullptr),
      videoOutput(nullptr),
      view(nullptr),
      useFilterInput(false),
      filterVideoCapture(nullptr),
      width(0),
      height(0),
      toggleEnableHotkeyPairId(OBS_INVALID_HOTKEY_PAIR_ID),
      splitRecordingHotkeyId(OBS_INVALID_HOTKEY_ID),
      splitRecordingEnabled(false),
      replayBufferActive(false),
      saveReplayBufferHotkeyId(OBS_INVALID_HOTKEY_ID)
{
    // DO NOT use obs_filter_get_parent() in this function (It'll return nullptr)
    obs_log(LOG_DEBUG, "%s: BranchOutputFilter creating", qUtf8Printable(name));
    obs_log(LOG_DEBUG, "filter_settings_json=%s", obs_data_get_json(settings));

    // Do not use memset
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        streamings[i] = {0};
    }

    // Do not use memset
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        audios[i] = {0};
    }

    pthread_mutex_init(&outputMutex, nullptr);

    if (!strcmp(obs_data_get_last_json(settings), "{}")) {
        // Maybe initial creation
        loadProfile(settings);
        loadRecently(settings);

        // Assit initial settings
        obs_data_set_bool(settings, "use_profile_recording_path", true);
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

    // Fiter activate immediately when "server" or "stream_recording" or "replay_buffer" is exists.
    initialized = countEnabledStreamings(settings) > 0 || obs_data_get_bool(settings, "stream_recording") ||
                  obs_data_get_bool(settings, "replay_buffer");

    obs_log(LOG_INFO, "%s: BranchOutputFilter created", qUtf8Printable(name));
}

BranchOutputFilter::~BranchOutputFilter()
{
    pthread_mutex_destroy(&outputMutex);
}

void BranchOutputFilter::addCallback(obs_source_t *source)
{
    // Do not start timer for private sources
    // Do not register private sources to status dock
    if (sourceIsPrivate(source)) {
        obs_log(
            LOG_DEBUG, "%s: Ignore adding to private source '%s'", qUtf8Printable(name), obs_source_get_name(source)
        );
        return;
    }

    obs_log(LOG_DEBUG, "%s: Filter adding to '%s'", qUtf8Printable(name), obs_source_get_name(source));

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
    // Track filter renames for name and hotkey settings
    filterRenamedSignal.Connect(
        obs_source_get_signal_handler(filterSource), "rename",
        [](void *_data, calldata_t *cd) {
            auto _filter = static_cast<BranchOutputFilter *>(_data);
            _filter->name = calldata_string(cd, "new_name");
            _filter->registerHotkey();
        },
        this
    );

    obs_log(LOG_INFO, "%s: Filter added to '%s'", qUtf8Printable(name), obs_source_get_name(source));
}

void BranchOutputFilter::updateCallback(obs_data_t *settings)
{
    auto source = obs_filter_get_parent(filterSource);

    // Do not save settings for private sources
    if (sourceIsPrivate(source)) {
        obs_log(
            LOG_DEBUG, "%s: Ignore updating in private source '%s'", qUtf8Printable(name), obs_source_get_name(source)
        );
        return;
    }

    obs_log(LOG_DEBUG, "%s: Filter updating", qUtf8Printable(name));

    // It's unwelcome to do stopping output during attempting connect to service.
    // So we just count up revision (Settings will be applied on videoTick())
    storedSettingsRev++;

    // Save settings as default
    OBSString config_dir_path = obs_module_get_config_path(obs_current_module(), "");
    os_mkdirs(config_dir_path);

    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    obs_data_save_json_safe(settings, path, "tmp", "bak");

    // Update status dock
    if (statusDock) {
        // Show in status dock (Thread-safe way)
        QMetaObject::invokeMethod(statusDock, "addFilter", Qt::QueuedConnection, Q_ARG(BranchOutputFilter *, this));
    }

    obs_log(LOG_INFO, "%s: Filter updated", qUtf8Printable(name));
}

void BranchOutputFilter::videoTickCallback(float)
{
    // Reset capture flag at the start of each frame so that renderTexture()
    // can detect whether captureFilterInput() was already called by the
    // normal rendering path (scene active).  video_tick runs before
    // output_frames in the graphics thread loop, guaranteeing correct ordering.
    if (useFilterInput && filterVideoCapture) {
        filterVideoCapture->resetCapturedFlag();
    }
}

void BranchOutputFilter::videoRenderCallback(gs_effect_t *)
{
    if (useFilterInput && filterVideoCapture) {
        // Optimized filter input mode:
        // 1. Capture upstream rendering to texrender (one render of the source tree)
        // 2. Draw captured texture to current render target (main output passthrough)
        //    This replaces obs_source_skip_video_filter to avoid rendering the
        //    source tree a second time.
        if (filterVideoCapture->captureFilterInput()) {
            filterVideoCapture->drawCapturedTexture();
        } else {
            // Fallback: capture failed, pass through normally
            obs_source_skip_video_filter(filterSource);
        }
    } else {
        // Source output mode: pass through the filter chain as usual
        obs_source_skip_video_filter(filterSource);
    }
}

// This method possibly called in different thread from UI thread
void BranchOutputFilter::removeCallback()
{
    obs_log(LOG_DEBUG, "%s: Filter removing", qUtf8Printable(name));

    if (intervalTimer) {
        // Stop interval timer (In proper thread)
        QMetaObject::invokeMethod(intervalTimer, "stop", Qt::QueuedConnection);
    }

    // Do not call stopOutput() here as this will cause a crash.

    if (statusDock) {
        // Unregister from output status dock (In proper thread)
        QMetaObject::invokeMethod(statusDock, "removeFilter", Qt::QueuedConnection, Q_ARG(BranchOutputFilter *, this));
    }

    // Unregsiter hotkeys
    if (toggleEnableHotkeyPairId != OBS_INVALID_HOTKEY_PAIR_ID) {
        obs_hotkey_pair_unregister(toggleEnableHotkeyPairId);
        toggleEnableHotkeyPairId = OBS_INVALID_HOTKEY_PAIR_ID;
    }
    if (splitRecordingHotkeyId != OBS_INVALID_HOTKEY_ID) {
        obs_hotkey_unregister(splitRecordingHotkeyId);
        splitRecordingHotkeyId = OBS_INVALID_HOTKEY_ID;
    }
    if (togglePauseRecordingHotkeyPairId != OBS_INVALID_HOTKEY_PAIR_ID) {
        obs_hotkey_pair_unregister(togglePauseRecordingHotkeyPairId);
        togglePauseRecordingHotkeyPairId = OBS_INVALID_HOTKEY_PAIR_ID;
    }
    if (addChapterToRecordingHotkeyId != OBS_INVALID_HOTKEY_ID) {
        obs_hotkey_unregister(addChapterToRecordingHotkeyId);
        addChapterToRecordingHotkeyId = OBS_INVALID_HOTKEY_ID;
    }
    if (saveReplayBufferHotkeyId != OBS_INVALID_HOTKEY_ID) {
        obs_hotkey_unregister(saveReplayBufferHotkeyId);
        saveReplayBufferHotkeyId = OBS_INVALID_HOTKEY_ID;
    }

    obs_log(LOG_INFO, "%s: Filter removed", qUtf8Printable(name));
}

void BranchOutputFilter::destroyCallback()
{
    obs_log(LOG_DEBUG, "%s: BranchOutputFilter destroying", qUtf8Printable(name));

    // Release all handles
    stopOutput();

    // Delete self in proper thread
    deleteLater();

    obs_log(LOG_INFO, "%s: BranchOutputFilter destroyed", qUtf8Printable(name));
}

void BranchOutputFilter::stopRecordingOutput()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (recordingOutput) {
            if (recordingActive) {
                obs_source_t *parent = obs_filter_get_parent(filterSource);
                if (parent) {
                    obs_source_dec_showing(parent);
                }
                obs_output_stop(recordingOutput);
            }
        }
        recordingOutput = nullptr;

        if (recordingActive) {
            recordingActive = false;
            obs_log(LOG_INFO, "%s: Stopping recording output succeeded", qUtf8Printable(name));
        }

        recordingPending = false;
        addChapterToRecordingEnabled = false;
        splitRecordingEnabled = false;
    }
}

void BranchOutputFilter::stopOutput()
{
    stopRecordingOutput();
    stopReplayBufferOutput();

    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        for (size_t i = 0; i < MAX_SERVICES; i++) {
            stopStreamingOutput(i);
        }

        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &audios[i];
            audioContext->encoder = nullptr;

            if (audioContext->capture) {
                delete audioContext->capture;
                audioContext->capture = nullptr;
            }
        }

        videoEncoder = nullptr;

        if (filterVideoCapture) {
            filterVideoCapture->setActive(false);
            delete filterVideoCapture;
            filterVideoCapture = nullptr;
        }

        if (view) {
            obs_view_set_source(view, 0, nullptr);
            obs_view_remove(view);
        }

        view = nullptr;
        videoOutput = nullptr;
        useFilterInput = false;
        blankSource = nullptr;
    }

    blankingOutputActive = false;
    blankingAudioMuted = false;
}

obs_data_t *BranchOutputFilter::createRecordingSettings(obs_data_t *settings, bool createFolder)
{
    auto recordingSettings = obs_data_create();
    auto config = obs_frontend_get_profile_config();
    QString filenameFormat = obs_data_get_string(settings, "filename_formatting");
    if (filenameFormat.isEmpty()) {
        // Fallback to profile if empty
        filenameFormat = config_get_string(config, "Output", "FilenameFormatting");
    }

    // Sanitize filename
#ifdef __APPLE__
    filenameFormat.replace(QRegularExpression("[:]"), "");
#elif defined(_WIN32)
    filenameFormat.replace(QRegularExpression("[<>:\"\\|\\?\\*]"), "");
#else
    // TODO: Add filtering for other platforms
#endif

    auto useProfileRecordingPath = obs_data_get_bool(settings, "use_profile_recording_path");
    auto path = useProfileRecordingPath ? getProfileRecordingPath(config) : obs_data_get_string(settings, "path");
    auto recFormat = obs_data_get_string(settings, "rec_format");

    // Validate recording path
    if (!path || !path[0]) {
        obs_log(LOG_ERROR, "%s: Recording path is not set", qUtf8Printable(name));
        obs_data_release(recordingSettings);
        return nullptr;
    }

    if (createFolder) {
        int ret = os_mkdirs(path);
        if (ret == MKDIR_ERROR) {
            obs_log(LOG_ERROR, "%s: Failed to create recording directory: %s", qUtf8Printable(name), path);
            obs_data_release(recordingSettings);
            return nullptr;
        }
    }

    // Add filter name to filename format
    QString sourceName = obs_source_get_name(obs_filter_get_parent(filterSource));
    QString filterName = qUtf8Printable(name);
    bool noSpace = obs_data_get_bool(settings, "no_space_filename");
    auto re = noSpace ? QRegularExpression("[\\s/\\\\.:;*?\"<>|&$,]") : QRegularExpression("[/\\\\.:;*?\"<>|&$,]");
    filenameFormat = filenameFormat.arg(sourceName.replace(re, "-")).arg(filterName.replace(re, "-"));
    auto compositePath = getOutputFilename(path, recFormat, noSpace, false, qUtf8Printable(filenameFormat));

    if (compositePath.isEmpty()) {
        obs_log(LOG_ERROR, "%s: Recording path is not accessible: %s", qUtf8Printable(name), path);
        obs_data_release(recordingSettings);
        return nullptr;
    }

    obs_data_set_string(recordingSettings, "path", qUtf8Printable(compositePath));

    auto splitFile = obs_data_get_string(settings, "split_file");
    splitRecordingEnabled = strlen(splitFile) > 0;
    if (splitRecordingEnabled) {
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

    // Apply fragmented MP4/MOV settings
    // Immitate https://github.com/obsproject/obs-studio/blob/d3c5d2ce0b15bac7a502f5aef4b3b5ec72ee8e09/frontend/utility/SimpleOutput.cpp#L820
    QString mux = obs_data_get_string(settings, "rec_muxer_custom");
    bool isFragmented = strncmp(recFormat, "fragmented", 10) == 0;

    if (isFragmented && (mux.isEmpty() || mux.indexOf("movflags") == -1)) {
        QString muxFrag = "movflags=frag_keyframe+empty_moov+delay_moov";
        if (!mux.isEmpty()) {
            muxFrag += " " + mux;
        }
        obs_data_set_string(recordingSettings, "muxer_settings", qUtf8Printable(muxFrag));
    } else {
        if (isFragmented) {
            obs_log(LOG_WARNING, "User enabled fragmented recording, but custom muxer settings contained movflags.");
        } else {
            obs_data_set_string(recordingSettings, "muxer_settings", qUtf8Printable(mux));
        }
    }

    return recordingSettings;
}

obs_data_t *BranchOutputFilter::createStreamingSettings(obs_data_t *settings, size_t index)
{
    auto streamingSettings = obs_data_create();
    auto propNameFormat = getIndexedPropNameFormat(index);

    // Copy all settings
    obs_data_apply(streamingSettings, settings);

    if (index > 0) {
        // Apply indexed properties (Only on index >= 1 )
        obs_data_set_string(
            streamingSettings, "server", obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("server")))
        );
        obs_data_set_string(
            streamingSettings, "key", obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("key")))
        );
        obs_data_set_bool(
            streamingSettings, "use_auth", obs_data_get_bool(settings, qUtf8Printable(propNameFormat.arg("use_auth")))
        );
        obs_data_set_string(
            streamingSettings, "username", obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("username")))
        );
        obs_data_set_string(
            streamingSettings, "password", obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("password")))
        );
    }

    return streamingSettings;
}

void BranchOutputFilter::getSourceResolution(uint32_t &outWidth, uint32_t &outHeight)
{
    if (useFilterInput) {
        obs_source_t *target = obs_filter_get_target(filterSource);
        if (target) {
            outWidth = obs_source_get_base_width(target);
            outHeight = obs_source_get_base_height(target);
        } else {
            outWidth = 0;
            outHeight = 0;
        }
    } else {
        obs_source_t *parent = obs_filter_get_parent(filterSource);
        outWidth = obs_source_get_width(parent);
        outHeight = obs_source_get_height(parent);
    }
    // Round up to a multiple of 2
    outWidth += (outWidth & 1);
    outHeight += (outHeight & 1);
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

    // Copy base resolution
    ovi->base_width = width;
    ovi->base_height = height;

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

BranchOutputFilter::BranchOutputStreamingContext
BranchOutputFilter::createSreamingOutput(obs_data_t *settings, size_t index)
{
    auto count = (size_t)obs_data_get_int(settings, "service_count");
    if (index >= count || index >= MAX_SERVICES) {
        return {0};
    }

    if (!isStreamingEnabled(settings, index)) {
        return {0};
    }

    OBSDataAutoRelease streamingSettings = createStreamingSettings(settings, index);
    BranchOutputStreamingContext context = {0};

    // Create service - We always use "rtmp_custom" as service
    context.service = obs_service_create("rtmp_custom", qUtf8Printable(name), streamingSettings, nullptr);
    if (!context.service) {
        obs_log(LOG_ERROR, "%s: Streaming %zu service creation failed", qUtf8Printable(name), index);
        return {0};
    }
    obs_service_apply_encoder_settings(context.service, streamingSettings, nullptr);

    // Determine output type
    auto type = obs_service_get_preferred_output_type(context.service);
    if (!type) {
        type = "rtmp_output";
        auto url = obs_service_get_connect_info(context.service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
        if (url != nullptr && !strncmp(url, FTL_PROTOCOL, strlen(FTL_PROTOCOL))) {
            type = "ftl_output";
        } else if (url != nullptr && strncmp(url, RTMP_PROTOCOL, strlen(RTMP_PROTOCOL))) {
            type = "ffmpeg_mpegts_muxer";
        }
    }

    // Create streaming output
    context.output =
        obs_output_create(type, qUtf8Printable(QString("%1 (%2)").arg(name).arg(index)), streamingSettings, nullptr);
    if (!context.output) {
        obs_log(LOG_ERROR, "%s (%zu): Streaming output creation failed", qUtf8Printable(name), index);
        return {0};
    }
    obs_output_set_reconnect_settings(context.output, OUTPUT_MAX_RETRIES, OUTPUT_RETRY_DELAY_SECS);
    obs_output_set_service(context.output, context.service);

    return context;
}

void BranchOutputFilter::startStreamingOutput(size_t index)
{
    if (!streamings[index].output) {
        return;
    }

    size_t encIndex = 0;
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        auto audioContext = &audios[i];
        if (!audioContext->encoder || !audioContext->streaming) {
            continue;
        }

        obs_output_set_audio_encoder(streamings[index].output, audioContext->encoder, encIndex++);
    }

    if (!encIndex) {
        // No audio encoder -> fallback first available encoder
        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &audios[i];
            if (audioContext->encoder) {
                obs_log(
                    LOG_WARNING, "%s (%zu): No audio encoder selected for streaming output, using track %d",
                    qUtf8Printable(name), index, i + 1
                );
                obs_output_set_audio_encoder(streamings[index].output, audioContext->encoder, encIndex++);
                break;
            }
        }
        if (!encIndex) {
            obs_log(LOG_ERROR, "%s (%zu): No audio encoder for streaming output", qUtf8Printable(name), index);
            return;
        }
    }

    obs_output_set_video_encoder(streamings[index].output, videoEncoder);

    streamings[index].outputStarting = true;

    // Track starting signal
    streamings[index].outputStartingSignal.Connect(
        obs_output_get_signal_handler(streamings[index].output), "starting",
        [](void *_data, calldata_t *) {
            auto context = static_cast<BranchOutputStreamingContext *>(_data);
            context->outputStarting = true;
            obs_log(LOG_DEBUG, "%s: Streaming output is starting", obs_output_get_name(context->output));
        },
        &streamings[index]
    );

    // Track activate signal
    streamings[index].outputActivateSignal.Connect(
        obs_output_get_signal_handler(streamings[index].output), "activate",
        [](void *_data, calldata_t *) {
            auto context = static_cast<BranchOutputStreamingContext *>(_data);
            context->outputStarting = false;
            obs_log(LOG_DEBUG, "%s: Streaming output has activated", obs_output_get_name(context->output));
        },
        &streamings[index]
    );

    // Track reconnect signal
    streamings[index].outputReconnectSignal.Connect(
        obs_output_get_signal_handler(streamings[index].output), "reconnect",
        [](void *_data, calldata_t *) {
            auto context = static_cast<BranchOutputStreamingContext *>(_data);
            context->reconnectAttemptingAt = os_gettime_ns();
            obs_log(LOG_DEBUG, "%s: Streaming output is reconnecting", obs_output_get_name(context->output));
        },
        &streamings[index]
    );

    // Track stop signal
    streamings[index].outputStopSignal.Connect(
        obs_output_get_signal_handler(streamings[index].output), "stop",
        [](void *_data, calldata_t *cd) {
            auto context = static_cast<BranchOutputStreamingContext *>(_data);
            context->outputStarting = false;
            auto code = calldata_int(cd, "code");
            obs_log(
                LOG_DEBUG, "%s: Streaming output has stopped with code=%lld", obs_output_get_name(context->output), code
            );
        },
        &streamings[index]
    );

    // Start streaming output
    if (obs_output_start(streamings[index].output)) {
        streamings[index].active = true;
        auto parent = obs_filter_get_parent(filterSource);
        if (parent) {
            obs_source_inc_showing(parent);
        }
        obs_log(LOG_INFO, "%s (%zu): Starting streaming output succeeded", qUtf8Printable(name), index);
    } else {
        obs_log(LOG_ERROR, "%s (%zu): Starting streaming output failed", qUtf8Printable(name), index);
    }
}

void BranchOutputFilter::stopStreamingOutput(size_t index)
{
    if (streamings[index].output && streamings[index].active) {
        obs_source_t *parent = obs_filter_get_parent(filterSource);
        if (parent) {
            obs_source_dec_showing(parent);
        }
        obs_output_stop(streamings[index].output);
        obs_log(LOG_INFO, "%s (%zu): Stopping streaming output succeeded", qUtf8Printable(name), index);
    }

    // Ensure signals are disconnected to prevent leaks across restarts
    streamings[index].outputStartingSignal.Disconnect();
    streamings[index].outputActivateSignal.Disconnect();
    streamings[index].outputReconnectSignal.Disconnect();
    streamings[index].outputStopSignal.Disconnect();

    streamings[index].output = nullptr;
    streamings[index].service = nullptr;
    streamings[index].reconnectAttemptingAt = 0;
    streamings[index].outputStarting = false;
    streamings[index].active = false;
    streamings[index].stopping = false;
}

void BranchOutputFilter::createAndStartRecordingOutput(obs_data_t *settings)
{
    if (!videoEncoder) {
        return;
    }

    auto recFormat = obs_data_get_string(settings, "rec_format");
    const char *outputId = !strcmp(recFormat, "hybrid_mp4") ? "mp4_output" : "ffmpeg_muxer";

    // Dtermine chapter marker capability
    // Chapter maker is only available for hybrid MP4
    addChapterToRecordingEnabled = !strcmp(recFormat, "hybrid_mp4");

    // Ensure base path exists
    OBSDataAutoRelease recordingSettings = createRecordingSettings(settings, true);
    if (!recordingSettings) {
        obs_log(LOG_ERROR, "%s: Recording settings creation failed (path unavailable?)", qUtf8Printable(name));
        return;
    }
    recordingOutput = obs_output_create(outputId, qUtf8Printable(name), recordingSettings, nullptr);
    if (!recordingOutput) {
        obs_log(LOG_ERROR, "%s: Recording output creation failed", qUtf8Printable(name));
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
                    LOG_WARNING, "%s: No audio encoder selected for recording, using track %d", qUtf8Printable(name),
                    i + 1
                );
                obs_output_set_audio_encoder(recordingOutput, audioContext->encoder, encIndex++);
                break;
            }
        }
        if (!encIndex) {
            obs_log(LOG_ERROR, "%s: No audio encoder for recording", qUtf8Printable(name));
            return;
        }
    }

    obs_output_set_video_encoder(recordingOutput, videoEncoder);

    // Start recording output
    if (obs_output_start(recordingOutput)) {
        recordingActive = true;
        recordingPending = false;
        auto parent = obs_filter_get_parent(filterSource);
        if (parent) {
            obs_source_inc_showing(parent);
        }
        obs_log(LOG_INFO, "%s: Starting recording output succeeded", qUtf8Printable(name));
    } else {
        obs_log(LOG_ERROR, "%s: Starting recording output failed", qUtf8Printable(name));
    }
}

void BranchOutputFilter::startOutput(obs_data_t *settings)
{
    // Force release references
    stopOutput();

    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        // Abort when obs initializing or filter disabled.
        if (!obs_initialized() || !obs_source_enabled(filterSource) || countActiveStreamings() > 0 || recordingActive ||
            replayBufferActive) {
            obs_log(LOG_ERROR, "%s: Ignore unavailable filter", qUtf8Printable(name));
            return;
        }

        // Retrieve filter source
        auto parent = obs_filter_get_parent(filterSource);
        if (!parent) {
            obs_log(LOG_ERROR, "%s: Filter source not found", qUtf8Printable(name));
            return;
        }

        // Ignore private sources
        if (sourceIsPrivate(parent)) {
            obs_log(LOG_ERROR, "%s: Ignore private source", qUtf8Printable(name));
            return;
        }

        // Mandatory paramters
        if (countEnabledStreamings(settings) == 0 && !obs_data_get_string(settings, "stream_recording")) {
            obs_log(LOG_ERROR, "%s: Nothing to do", qUtf8Printable(name));
            return;
        }

        bool blankWhenHidden = obs_data_get_bool(settings, "blank_when_not_visible");
        bool muteWhenHidden = obs_data_get_bool(settings, "mute_audio_when_blank");

        obs_video_info ovi = {0};
        if (!obs_get_video_info(&ovi)) {
            // Abort when no video situation
            obs_log(LOG_ERROR, "%s: No video", qUtf8Printable(name));
            return;
        }

        // Determine video source type first to choose correct resolution source
        auto videoSourceType = obs_data_get_string(settings, "video_source_type");
        useFilterInput = videoSourceType && !strcmp(videoSourceType, "filter_input");

        // Resolve input resolution based on video source type
        // sourceWidth/sourceHeight represent the actual input resolution for this filter,
        // used both for video capture and for collapsed-source detection (recording pending).
        uint32_t sourceWidth;
        uint32_t sourceHeight;
        getSourceResolution(sourceWidth, sourceHeight);

        width = sourceWidth;
        height = sourceHeight;

        if (width == 0 || height == 0) {
            // Default to canvas size
            width = ovi.base_width;
            height = ovi.base_height;
        }

        determineOutputResolution(settings, &ovi);

        if (ovi.output_width == 0 || ovi.output_height == 0 || ovi.fps_den == 0 || ovi.fps_num == 0) {
            // Abort when invalid video parameters situation
            obs_log(LOG_ERROR, "%s: Invalid video spec", qUtf8Printable(name));
            return;
        }

        // Update active revision with stored settings.
        activeSettingsRev = storedSettingsRev;

        //--- Create service and open streaming output ---//
        auto serviceCount = (size_t)obs_data_get_int(settings, "service_count");
        for (size_t i = 0; i < MAX_SERVICES && i < serviceCount; i++) {
            streamings[i] = createSreamingOutput(settings, i);
        }

        //--- Open video output ---//
        if (useFilterInput) {
            // Filter input mode: capture via texrender + proxy source + obs_view.
            // The proxy source renders the captured texrender texture on the GPU.
            // obs_view creates a video_t* registered in OBS's mix list, allowing
            // GPU encoders (NVENC, QSV, AMF, etc.) to work directly.
            filterVideoCapture = new FilterVideoCapture(filterSource, parent, width, height);
            if (!filterVideoCapture->getProxySource()) {
                obs_log(LOG_ERROR, "%s: Filter video capture creation failed", qUtf8Printable(name));
                delete filterVideoCapture;
                filterVideoCapture = nullptr;
                return;
            }

            view = obs_view_create();
            obs_view_set_source(view, 0, filterVideoCapture->getProxySource());

            videoOutput = obs_view_add2(view, &ovi);
            if (!videoOutput) {
                obs_log(LOG_ERROR, "%s: Video output association failed", qUtf8Printable(name));
                delete filterVideoCapture;
                filterVideoCapture = nullptr;
                return;
            }
            filterVideoCapture->setActive(true);
        } else {
            // Source output mode (default): use obs_view for the parent source
            view = obs_view_create();
            obs_view_set_source(view, 0, parent);

            videoOutput = obs_view_add2(view, &ovi);
            if (!videoOutput) {
                obs_log(LOG_ERROR, "%s: Video output association failed", qUtf8Printable(name));
                return;
            }
        }

        //--- Open audio output(s) ---//
        // Do not use memset
        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            audios[i] = {0};
        }

        obs_audio_info ai = {0};
        if (!obs_get_audio_info(&ai)) {
            obs_log(LOG_ERROR, "%s: Failed to get audio info", qUtf8Printable(name));
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
                auto propNameFormat = getIndexedPropNameFormat(track, 1);

                auto audioDest = obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("audio_dest")));
                audioContext->streaming = !strcmp(audioDest, "streaming") || !strcmp(audioDest, "both");
                audioContext->recording = !strcmp(audioDest, "recording") || !strcmp(audioDest, "both");

                auto audioSourceUuid =
                    obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("audio_source")));
                if (!strcmp(audioSourceUuid, "disabled")) {
                    // Disabled track
                    obs_log(LOG_INFO, "%s: Track %d is disabled", qUtf8Printable(name), track);
                    continue;

                } else if (!strcmp(audioSourceUuid, "no_audio")) {
                    // Silence audio
                    obs_log(LOG_INFO, "%s: Use silence for track %d (%s)", qUtf8Printable(name), track, audioDest);

                    audioContext->capture = new AudioCapture(
                        "Silence", ai.samples_per_sec, ai.speakers, AudioCapture::silenceCapture, this
                    );
                    audioContext->audio = audioContext->capture->getAudio();
                    audioContext->name = audioContext->capture->getName();

                } else if (!strcmp(audioSourceUuid, "master_track")) {
                    // Master audio
                    auto masterTrack = obs_data_get_int(settings, qUtf8Printable(propNameFormat.arg("audio_track")));
                    if (masterTrack < 1 || masterTrack > MAX_AUDIO_MIXES) {
                        obs_log(
                            LOG_ERROR, "%s: Invalid master audio track No.%d for track %d", qUtf8Printable(name),
                            masterTrack, track
                        );
                        return;
                    }
                    obs_log(
                        LOG_INFO, "%s: Use master audio track No.%d for track %d (%s)", qUtf8Printable(name),
                        masterTrack, track, audioDest
                    );

                    audioContext->mixIndex = masterTrack - 1;
                    audioContext->audio = obs_get_audio();
                    audioContext->name = QTStr("MasterTrack%1").arg(masterTrack);

                } else if (!strcmp(audioSourceUuid, "filter")) {
                    // Filter pipline's audio
                    obs_log(LOG_INFO, "%s: Use filter audio for track %d (%s)", qUtf8Printable(name), track, audioDest);

                    audioContext->capture =
                        new FilterAudioCapture(qUtf8Printable(name), ai.samples_per_sec, ai.speakers, this);
                    audioContext->audio = audioContext->capture->getAudio();
                    audioContext->name = audioContext->capture->getName();

                } else {
                    // Specific source's audio
                    OBSSourceAutoRelease source = obs_get_source_by_uuid(audioSourceUuid);
                    if (!source) {
                        // Non-stopping error
                        obs_log(
                            LOG_WARNING, "%s: Ignore audio source for track %d (%s)", qUtf8Printable(name), track,
                            audioDest
                        );
                        continue;
                    }

                    // Use custom audio source
                    obs_log(
                        LOG_INFO, "%s: Use %s audio for track %d", qUtf8Printable(name), obs_source_get_name(source),
                        track
                    );

                    audioContext->capture = new SourceAudioCapture(source, ai.samples_per_sec, ai.speakers, this);
                    audioContext->audio = audioContext->capture->getAudio();
                    audioContext->name = audioContext->capture->getName();
                }

                if (!audioContext->audio) {
                    obs_log(
                        LOG_ERROR, "%s: Audio creation failed for track %d (%s)", qUtf8Printable(name), track, audioDest
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
            obs_log(LOG_INFO, "%s: Use filter audio for track 1", qUtf8Printable(name));
            auto audioContext = &audios[0];
            audioContext->capture = new FilterAudioCapture(qUtf8Printable(name), ai.samples_per_sec, ai.speakers, this);
            audioContext->audio = audioContext->capture->getAudio();
            audioContext->streaming = true;
            audioContext->recording = true;
            audioContext->name = audioContext->capture->getName();

            if (!audioContext->audio) {
                obs_log(LOG_ERROR, "%s: Audio creation failed", qUtf8Printable(name));
                delete audioContext->capture;
                audioContext->capture = nullptr;
                return;
            }
        }

        //--- Setup video encoder ---//
        auto video_encoder_id = obs_data_get_string(settings, "video_encoder");

        videoEncoder = obs_video_encoder_create(video_encoder_id, qUtf8Printable(name), settings, nullptr);
        if (!videoEncoder) {
            obs_log(LOG_ERROR, "%s: Video encoder creation failed", qUtf8Printable(name));
            return;
        }

        obs_encoder_set_scaled_size(videoEncoder, 0, 0); // No scaling
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
                obs_log(LOG_ERROR, "%s: Audio encoder creation failed for track %d", qUtf8Printable(name), i + 1);
                return;
            }
            obs_encoder_set_audio(audioContext->encoder, audioContext->audio);
        }

        if (blankWhenHidden) {
            // Pre-create blank source so that setBlankingActive() never has to
            // allocate during a live stream.  The color_source is lightweight and
            // cleaned up in stopOutput().
            if (!blankSource) {
                OBSDataAutoRelease blankSettings = obs_data_create();
                obs_data_set_int(blankSettings, "color", 0xFF000000);
                if (width > 0 && height > 0) {
                    obs_data_set_int(blankSettings, "width", width);
                    obs_data_set_int(blankSettings, "height", height);
                }
                blankSource = obs_source_create_private("color_source", "Branch Output Blank", blankSettings);
                if (!blankSource) {
                    obs_log(LOG_WARNING, "%s: Failed to pre-create blank color source", qUtf8Printable(name));
                }
            }

            bool visibleInProgram = sourceVisibleInProgram(parent);
            setBlankingActive(!visibleInProgram, muteWhenHidden, parent);
        }

        //--- Start recording output (if requested) ---//
        if (isRecordingEnabled(settings)) {
            recordingPending = (sourceWidth == 0 || sourceHeight == 0) &&
                               obs_data_get_bool(settings, "suspend_recording_when_source_collapsed");
            if (!recordingPending) {
                createAndStartRecordingOutput(settings);
            } else {
                obs_log(LOG_INFO, "%s: The recording output pending until source is uncollapsed", qUtf8Printable(name));
            }
        }

        //--- Start replay buffer (if requested) ---//
        if (isReplayBufferEnabled(settings)) {
            createAndStartReplayBuffer(settings);
        }

        //--- Start streaming output (if requested) ---//
        for (size_t i = 0; i < MAX_SERVICES; i++) {
            startStreamingOutput(i);
        }
    }
}

void BranchOutputFilter::reconnectStreamingOutput(size_t index)
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (streamings[index].active) {
            obs_output_stop(streamings[index].output);

            if (!obs_output_start(streamings[index].output)) {
                obs_log(LOG_ERROR, "%s (%zu): Reconnect streaming output failed", qUtf8Printable(name), index);
            }
        }
    }
}

void BranchOutputFilter::restartRecordingOutput()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (recordingActive) {
            obs_output_stop(recordingOutput);

            if (!obs_output_start(recordingOutput)) {
                obs_log(LOG_ERROR, "%s: Restart recording output failed", qUtf8Printable(name));
            }
        }
    }
}

void BranchOutputFilter::loadProfile(obs_data_t *settings)
{
    obs_log(LOG_DEBUG, "Profile settings loading");

    auto config = obs_frontend_get_profile_config();

    const char *videoEncoderId;
    const char *audioEncoderId;
    uint64_t audioBitrate;

    if (isAdvancedMode(config)) {
        videoEncoderId = config_get_string(config, "AdvOut", "Encoder");
        audioEncoderId = config_get_string(config, "AdvOut", "AudioEncoder");
        audioBitrate = config_get_uint(config, "AdvOut", "FFABitrate");

        OBSString profilePath = obs_frontend_get_current_profile_path();
        auto encoderJsonPath = QString("%1/%2").arg(QString(profilePath)).arg("streamEncoder.json");
        OBSDataAutoRelease encoderSettings = obs_data_create_from_json_file(qUtf8Printable(encoderJsonPath));

        if (encoderSettings) {
            // Include video bitrate
            obs_data_apply(settings, encoderSettings);
        }

    } else {
        videoEncoderId = getSimpleVideoEncoder(config_get_string(config, "SimpleOutput", "StreamEncoder"));
        audioEncoderId = getSimpleAudioEncoder(config_get_string(config, "SimpleOutput", "StreamAudioEncoder"));
        audioBitrate = config_get_uint(config, "SimpleOutput", "ABitrate");

        auto videoBitrate = config_get_uint(config, "SimpleOutput", "VBitrate");
        obs_data_set_int(settings, "bitrate", videoBitrate);

        auto preset = config_get_string(config, "SimpleOutput", "Preset");
        obs_data_set_string(settings, "preset", preset);

        auto preset2 = config_get_string(config, "SimpleOutput", "NVENCPreset2");
        obs_data_set_string(settings, "preset2", preset2);
    }

    obs_data_set_string(settings, "audio_encoder", audioEncoderId);
    obs_data_set_string(settings, "video_encoder", videoEncoderId);
    obs_data_set_int(settings, "audio_bitrate", audioBitrate);

    obs_log(LOG_INFO, "Profile settings loaded");
}

void BranchOutputFilter::loadRecently(obs_data_t *settings)
{
    obs_log(LOG_DEBUG, "Recently settings loading");
    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    OBSDataAutoRelease recently_settings = obs_data_create_from_json_file(path);

    if (recently_settings) {
        for (size_t i = 0; i < MAX_SERVICES; i++) {
            auto propNameFormat = getIndexedPropNameFormat(i);
            obs_data_erase(recently_settings, qUtf8Printable(propNameFormat.arg("server")));
            obs_data_erase(recently_settings, qUtf8Printable(propNameFormat.arg("key")));
            obs_data_erase(recently_settings, qUtf8Printable(propNameFormat.arg("use_auth")));
            obs_data_erase(recently_settings, qUtf8Printable(propNameFormat.arg("username")));
            obs_data_erase(recently_settings, qUtf8Printable(propNameFormat.arg("password")));
        }

        obs_data_erase(recently_settings, "stream_recording");
        obs_data_erase(recently_settings, "custom_audio_source");
        obs_data_erase(recently_settings, "multitrack_audio");

        for (size_t n = 1; n <= MAX_AUDIO_MIXES; n++) {
            auto propNameFormat = getIndexedPropNameFormat(n, 1);
            obs_data_erase(recently_settings, qUtf8Printable(propNameFormat.arg("audio_source")));
            obs_data_erase(recently_settings, qUtf8Printable(propNameFormat.arg("audio_track")));
            obs_data_erase(recently_settings, qUtf8Printable(propNameFormat.arg("audio_dest")));
        }

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
    if (countActiveStreamings() > 0 || recordingActive || replayBufferActive) {
        stopOutput();
    }

    OBSDataAutoRelease settings = obs_source_get_settings(filterSource);
    if (countEnabledStreamings(settings) > 0 || isRecordingEnabled(settings) || isReplayBufferEnabled(settings)) {
        startOutput(settings);
    }
}

bool BranchOutputFilter::reconnectAttemptingTimedOut(size_t index)
{
    return streamings[index].reconnectAttemptingAt &&
           os_gettime_ns() - streamings[index].reconnectAttemptingAt > RECONNECT_ATTEMPTING_TIMEOUT_NS;
}

bool BranchOutputFilter::someStreamingsStarting()
{
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        if (streamings[i].outputStarting) {
            return true;
        }
    }
    return false;
}

int BranchOutputFilter::countEnabledStreamings(obs_data_t *settings)
{
    int count = 0;
    auto serviceCount = (size_t)obs_data_get_int(settings, "service_count");
    for (size_t i = 0; i < MAX_SERVICES && i < serviceCount; i++) {
        if (isStreamingEnabled(settings, i)) {
            count++;
        }
    }
    return count;
}

int BranchOutputFilter::countAliveStreamings()
{
    int count = 0;
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        if (streamings[i].output && obs_output_active(streamings[i].output)) {
            count++;
        }
    }
    return count;
}

int BranchOutputFilter::countActiveStreamings()
{
    int count = 0;
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        if (streamings[i].active) {
            count++;
        }
    }
    return count;
}

bool BranchOutputFilter::hasEnabledStreamings(obs_data_t *settings)
{
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (isStreamingEnabled(settings, i)) {
            return true;
        }
    }
    return false;
}

bool BranchOutputFilter::isStreamingEnabled(obs_data_t *settings, size_t index)
{
    auto propNameFormat = getIndexedPropNameFormat(index);
    return !!strlen(obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("server"))));
}

bool BranchOutputFilter::isRecordingEnabled(obs_data_t *settings)
{
    return obs_data_get_bool(settings, "stream_recording");
}

bool BranchOutputFilter::isSplitRecordingEnabled(obs_data_t *settings)
{
    // Split mode will be disabled when some streamings are enabled.
    return isRecordingEnabled(settings) && !!strlen(obs_data_get_string(settings, "split_file"));
}

bool BranchOutputFilter::canPauseRecording()
{
    // Pausing recording will be disabled when some streamings are active.
    return countActiveStreamings() == 0 && !recordingPending;
}

bool BranchOutputFilter::canAddChapterToRecording()
{
    return recordingActive && recordingOutput && addChapterToRecordingEnabled && !obs_output_paused(recordingOutput);
}

bool BranchOutputFilter::canSplitRecording()
{
    return recordingActive && recordingOutput && splitRecordingEnabled && !obs_output_paused(recordingOutput);
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

    if (streamingStopping) {
        onStopOutputGracefully();
        return;
    }

    auto interlockType = statusDock ? statusDock->getInterlockType() : INTERLOCK_TYPE_ALWAYS_ON;
    auto sourceEnabled = obs_source_enabled(filterSource);
    auto streamingActive = countActiveStreamings() > 0;

    if (!streamingActive && !recordingActive && !recordingPending && !replayBufferActive) {
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
        auto streamingAlive = countAliveStreamings() > 0;
        auto recordingAlive = recordingOutput && obs_output_active(recordingOutput);

        if (sourceEnabled) {
            if (someStreamingsStarting()) {
                return;
            }

            OBSDataAutoRelease settings = obs_source_get_settings(filterSource);
            bool blankWhenHidden = obs_data_get_bool(settings, "blank_when_not_visible");
            bool muteWhenHidden = obs_data_get_bool(settings, "mute_audio_when_blank");

            // Check interlock condition
            if (interlockType == INTERLOCK_TYPE_STREAMING) {
                if (!obs_frontend_streaming_active()) {
                    // Stop output when streaming is not active
                    onStopOutputGracefully();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_RECORDING) {
                if (!obs_frontend_recording_active()) {
                    // Stop output when recording is not active
                    onStopOutputGracefully();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_STREAMING_RECORDING) {
                if (!obs_frontend_streaming_active() && !obs_frontend_recording_active()) {
                    // Stop output when streaming and recording are not active
                    onStopOutputGracefully();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_VIRTUAL_CAM) {
                if (!obs_frontend_virtualcam_active()) {
                    // Stop output when virtual cam is not active
                    onStopOutputGracefully();
                    return;
                }
            }

            if (activeSettingsRev < storedSettingsRev) {
                // Settings has been changed
                obs_log(LOG_INFO, "%s: Settings change detected, Attempting restart", qUtf8Printable(name));
                restartOutput();
                return;
            }

            if (streamingAlive || recordingAlive || recordingPending || replayBufferActive) {
                // Monitoring source
                auto parent = obs_filter_get_parent(filterSource);

                // Resolve input resolution based on video source type
                uint32_t sourceWidth;
                uint32_t sourceHeight;
                getSourceResolution(sourceWidth, sourceHeight);

                if (!sourceInFrontend(parent)) {
                    // Stop output when source had been removed
                    onStopOutputGracefully();
                    return;
                }

                bool visibleInProgram = true;
                if (blankWhenHidden) {
                    visibleInProgram = sourceVisibleInProgram(parent);
                    pthread_mutex_lock(&outputMutex);
                    {
                        OBSMutexAutoUnlock outputLocked(&outputMutex);
                        setBlankingActive(!visibleInProgram, muteWhenHidden, parent);
                    }
                }

                // When blanking because the source is not visible, some sources report unstable base sizes.
                // Avoid restart storms while hidden; resolution will be re-evaluated when visible again.
                bool skipResolutionRestart = blankWhenHidden && !visibleInProgram;

                if (!skipResolutionRestart && (width != sourceWidth || height != sourceHeight)) {
                    // Source resolution was changed
                    if (sourceWidth > 0 && sourceHeight > 0) {
                        if (!obs_data_get_bool(settings, "keep_output_base_resolution")) {
                            // Restart output when source resolution was changed.
                            obs_log(LOG_INFO, "%s: Attempting restart the streaming output", qUtf8Printable(name));
                            startOutput(settings);
                            return;
                        }
                    } else if (sourceWidth == 0 || sourceHeight == 0) {
                        // The source is collapsed
                        if (!recordingPending && recordingActive &&
                            obs_data_get_bool(settings, "suspend_recording_when_source_collapsed")) {
                            if (!streamingActive) {
                                // Recording only -> Pause the recording
                                if (!obs_output_paused(recordingOutput)) {
                                    // Don't pause when already paused manually
                                    obs_log(
                                        LOG_INFO,
                                        "%s: The source resolution is corrupted, Attempting pause the recording output",
                                        qUtf8Printable(name)
                                    );
                                    pauseRecording();
                                    recordingPending = true;
                                    return;
                                }
                            } else {
                                // There are some streamings -> Suspend recording output
                                obs_log(
                                    LOG_INFO,
                                    "%s: The source resolution is corrupted, Attempting suspend the recording output",
                                    qUtf8Printable(name)
                                );
                                stopRecordingOutput();
                                recordingPending = true;
                                return;
                            }
                        } else {
                            // Ignore source collapse
                        }
                    }
                }

                if (recordingPending && sourceWidth > 0 && sourceHeight > 0) {
                    // Source is uncollapsed
                    // When recording output was pending
                    if (recordingActive) {
                        // If the recording output has already been created
                        // Unpause recording
                        obs_log(LOG_INFO, "%s: Attempting unpause the recording output", qUtf8Printable(name));
                        unpauseRecording();
                        return;
                    } else {
                        // If the output has not yet been created.
                        // Create and start recording when recording output was pending.
                        obs_log(LOG_INFO, "%s: Attempting resume the recording output", qUtf8Printable(name));
                        OBSDataAutoRelease pendingSettings = obs_source_get_settings(filterSource);
                        createAndStartRecordingOutput(pendingSettings);
                        return;
                    }
                }
            }

            if (recordingActive && !recordingAlive) {
                // Restart recording
                obs_log(LOG_INFO, "%s: Attempting reactivate the recording output", qUtf8Printable(name));
                restartRecordingOutput();
            }

            for (size_t i = 0; i < MAX_SERVICES; i++) {
                if (streamings[i].active && streamings[i].output && !obs_output_active(streamings[i].output) &&
                    !obs_output_reconnecting(streamings[i].output)) {
                    // Restart streaming
                    obs_log(LOG_INFO, "%s (%zu): Attempting reactivate the streaming output", qUtf8Printable(name), i);
                    reconnectStreamingOutput(i);
                }
            }

        } else {
            if (streamingActive || recordingActive || recordingPending || replayBufferActive) {
                // Clicked filter's "Eye" icon (Hide)
                onStopOutputGracefully();
                return;
            }
        }
    }
}

void BranchOutputFilter::onStopOutputGracefully()
{
    // Stop recording and replay buffer immediately first
    stopRecordingOutput();
    stopReplayBufferOutput();

    // Lock out other output thread to prevent crash
    pthread_mutex_lock(&pluginMutex);
    {
        OBSMutexAutoUnlock pluginLocked(&pluginMutex);

        pthread_mutex_lock(&outputMutex);
        {
            OBSMutexAutoUnlock outputLocked(&outputMutex);

            streamingStopping = true;

            for (size_t i = 0; i < MAX_SERVICES; i++) {
                if (streamings[i].output && streamings[i].active) {
                    if (streamings[i].stopping) {
                        if (reconnectAttemptingTimedOut(i)) {
                            // stop streaming safely
                            stopStreamingOutput(i);
                        }
                    } else if (obs_output_reconnecting(streamings[i].output)) {
                        // Waiting few seconds to avoid crash due to stoppoing during reconnecting
                        streamings[i].stopping = true;
                    } else {
                        // Stop immediately
                        stopStreamingOutput(i);
                    }
                }
            }

            if (countActiveStreamings() > 0) {
                return;
            }

            // All streaming has been stopped
            streamingStopping = false;
        }
    }

    // Finalize termination
    stopOutput();
}

bool BranchOutputFilter::splitRecording()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (!splitRecordingEnabled || !recordingActive || !recordingOutput) {
            return false;
        }

        if (obs_output_paused(recordingOutput)) {
            // Can't split when recording was paused.
            return false;
        }

        // Immitate obs_frontend_recording_split_file()
        proc_handler_t *ph = obs_output_get_proc_handler(recordingOutput);
        uint8_t stack[128];
        calldata cd;
        calldata_init_fixed(&cd, stack, sizeof(stack));
        proc_handler_call(ph, "split_file", &cd);
        return calldata_bool(&cd, "split_file_enabled");
    }
}

bool BranchOutputFilter::pauseRecording()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (!recordingActive || !recordingOutput || countActiveStreamings() > 0) {
            // Can't pause when some streamings are active
            return false;
        }

        if (obs_output_paused(recordingOutput)) {
            // Already paused
            return false;
        }

        obs_output_pause(recordingOutput, true);
        return true;
    }
}

bool BranchOutputFilter::unpauseRecording()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (!recordingActive || !recordingOutput) {
            return false;
        }

        if (!obs_output_paused(recordingOutput)) {
            // Already unpaused
            return false;
        }

        obs_output_pause(recordingOutput, false);
        recordingPending = false; // Force clear pending flag
        return true;
    }
}

bool BranchOutputFilter::addChapterToRecording(QString chapterName)
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (!addChapterToRecordingEnabled || !recordingActive || !recordingOutput) {
            return false;
        }

        if (obs_output_paused(recordingOutput)) {
            // Can't add chapter when paused
            return false;
        }

        // Immitate obs_frontend_recording_add_chapter()
        proc_handler_t *ph = obs_output_get_proc_handler(recordingOutput);
        calldata cd;
        calldata_init(&cd);
        // Use current date-time when name is empty
        calldata_set_string(
            &cd, "chapter_name",
            chapterName.isEmpty() ? qUtf8Printable(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"))
                                  : qUtf8Printable(chapterName)
        );
        bool result = proc_handler_call(ph, "add_chapter", &cd);
        calldata_free(&cd);
        return result;
    }
}

void BranchOutputFilter::setAudioCapturesActive(bool active)
{
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        auto audioContext = &audios[i];
        if (audioContext->capture) {
            audioContext->capture->setActive(active);
        }
    }
}

void BranchOutputFilter::setBlankingActive(bool active, bool muteAudio, obs_source_t *parent)
{
    if (!parent) {
        parent = obs_filter_get_parent(filterSource);
    }

    if (!view) {
        blankingOutputActive = false;
        if (blankingAudioMuted) {
            setAudioCapturesActive(true);
            blankingAudioMuted = false;
        }
        return;
    }

    if (active) {
        if (!blankingOutputActive) {
            // blankSource is pre-created in startOutput().
            if (blankSource) {
                obs_view_set_source(view, 0, blankSource);
            } else {
                obs_log(
                    LOG_WARNING, "%s: Blank source not available; leaving original source active", qUtf8Printable(name)
                );
            }
            if (muteAudio) {
                setAudioCapturesActive(false);
                blankingAudioMuted = true;
            } else {
                blankingAudioMuted = false;
            }
            blankingOutputActive = true;
            obs_log(LOG_INFO, "%s: Output blanked because source is not visible", qUtf8Printable(name));
        } else {
            if (muteAudio && !blankingAudioMuted) {
                setAudioCapturesActive(false);
                blankingAudioMuted = true;
            } else if (!muteAudio && blankingAudioMuted) {
                setAudioCapturesActive(true);
                blankingAudioMuted = false;
            }
        }
    } else {
        if (blankingOutputActive) {
            if (useFilterInput && filterVideoCapture) {
                obs_view_set_source(view, 0, filterVideoCapture->getProxySource());
            } else if (parent) {
                obs_view_set_source(view, 0, parent);
            }
            blankingOutputActive = false;
            obs_log(LOG_INFO, "%s: Output resumed because source became visible", qUtf8Printable(name));
        }
        if (blankingAudioMuted) {
            setAudioCapturesActive(true);
            blankingAudioMuted = false;
        }
    }
}

bool BranchOutputFilter::onEnableFilterHotkeyPressed(void *data, obs_hotkey_pair_id, obs_hotkey *, bool pressed)
{
    if (!pressed) {
        return false;
    }

    BranchOutputFilter *filter = static_cast<BranchOutputFilter *>(data);
    if (obs_source_enabled(filter->filterSource)) {
        // Already enabled
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

    BranchOutputFilter *filter = static_cast<BranchOutputFilter *>(data);
    if (!obs_source_enabled(filter->filterSource)) {
        // Already disabled
        return false;
    }

    obs_source_set_enabled(filter->filterSource, false);
    return true;
}

void BranchOutputFilter::onSplitRecordingFileHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey *, bool pressed)
{
    if (!pressed) {
        return;
    }

    BranchOutputFilter *filter = static_cast<BranchOutputFilter *>(data);
    filter->splitRecording();
}

bool BranchOutputFilter::onPauseRecordingHotkeyPressed(void *data, obs_hotkey_pair_id, obs_hotkey *, bool pressed)
{
    if (!pressed) {
        return false;
    }

    BranchOutputFilter *filter = static_cast<BranchOutputFilter *>(data);
    return filter->pauseRecording();
}

bool BranchOutputFilter::onUnpauseRecordingHotkeyPressed(void *data, obs_hotkey_pair_id, obs_hotkey *, bool pressed)
{
    if (!pressed) {
        return false;
    }

    BranchOutputFilter *filter = static_cast<BranchOutputFilter *>(data);

    if (filter->recordingPending) {
        // Block unpausing when recording is pending
        return false;
    }

    return filter->unpauseRecording();
}

void BranchOutputFilter::onAddChapterToRecordingFileHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey *, bool pressed)
{
    if (!pressed) {
        return;
    }

    BranchOutputFilter *filter = static_cast<BranchOutputFilter *>(data);
    filter->addChapterToRecording();
}

void BranchOutputFilter::registerHotkey()
{
    if (toggleEnableHotkeyPairId != OBS_INVALID_HOTKEY_PAIR_ID) {
        // Unregsiter previous
        obs_hotkey_pair_unregister(toggleEnableHotkeyPairId);
    }
    if (splitRecordingHotkeyId != OBS_INVALID_HOTKEY_ID) {
        // Unregsiter previous
        obs_hotkey_unregister(splitRecordingHotkeyId);
    }
    if (togglePauseRecordingHotkeyPairId != OBS_INVALID_HOTKEY_PAIR_ID) {
        // Unregsiter previous
        obs_hotkey_pair_unregister(togglePauseRecordingHotkeyPairId);
    }
    if (addChapterToRecordingHotkeyId != OBS_INVALID_HOTKEY_ID) {
        // Unregsiter previous
        obs_hotkey_unregister(addChapterToRecordingHotkeyId);
    }
    if (saveReplayBufferHotkeyId != OBS_INVALID_HOTKEY_ID) {
        // Unregsiter previous
        obs_hotkey_unregister(saveReplayBufferHotkeyId);
    }

    // Register enable/disable hotkeys
    auto enableFilterName = QString("EnableFilter.%1").arg(obs_source_get_uuid(filterSource));
    auto enableFilterDescription = QString(obs_module_text("EnableHotkey")).arg(name);
    auto disableFilterName = QString("DisableFilter.%1").arg(obs_source_get_uuid(filterSource));
    auto disableFilterDescription = QString(obs_module_text("DisableHotkey")).arg(name);

    toggleEnableHotkeyPairId = obs_hotkey_pair_register_source(
        obs_filter_get_parent(filterSource), qUtf8Printable(enableFilterName), qUtf8Printable(enableFilterDescription),
        qUtf8Printable(disableFilterName), qUtf8Printable(disableFilterDescription), onEnableFilterHotkeyPressed,
        onDisableFilterHotkeyPressed, this, this
    );

    // Register split recording hotkey
    auto splitName = QString("SplitRecordingFile.%1").arg(obs_source_get_uuid(filterSource));
    auto splitDescription = QString(obs_module_text("SplitRecordingFileHotkey")).arg(name);

    splitRecordingHotkeyId = obs_hotkey_register_source(
        obs_filter_get_parent(filterSource), qUtf8Printable(splitName), qUtf8Printable(splitDescription),
        onSplitRecordingFileHotkeyPressed, this
    );

    // Register pause/unpause recording hotkey
    auto pauseRecordingName = QString("PauseRecording.%1").arg(obs_source_get_uuid(filterSource));
    auto pauseRecordingDescription = QString(obs_module_text("PauseRecordingHotkey")).arg(name);
    auto unpauseRecordingName = QString("UnpauseRecording.%1").arg(obs_source_get_uuid(filterSource));
    auto unpauseRecordingDescription = QString(obs_module_text("UnpauseRecordingHotkey")).arg(name);

    togglePauseRecordingHotkeyPairId = obs_hotkey_pair_register_source(
        obs_filter_get_parent(filterSource), qUtf8Printable(pauseRecordingName),
        qUtf8Printable(pauseRecordingDescription), qUtf8Printable(unpauseRecordingName),
        qUtf8Printable(unpauseRecordingDescription), onPauseRecordingHotkeyPressed, onUnpauseRecordingHotkeyPressed,
        this, this
    );

    // Register add chapter to recording hotkey
    auto addChapterName = QString("AddChapterToRecordingFile.%1").arg(obs_source_get_uuid(filterSource));
    auto addChapterDescription = QString(obs_module_text("AddChapterToRecordingFileHotkey")).arg(name);
    addChapterToRecordingHotkeyId = obs_hotkey_register_source(
        obs_filter_get_parent(filterSource), qUtf8Printable(addChapterName), qUtf8Printable(addChapterDescription),
        onAddChapterToRecordingFileHotkeyPressed, this
    );

    // Register save replay buffer hotkey
    auto saveReplayName = QString("SaveReplayBuffer.%1").arg(obs_source_get_uuid(filterSource));
    auto saveReplayDescription = QString(obs_module_text("SaveReplayBufferHotkey")).arg(name);
    saveReplayBufferHotkeyId = obs_hotkey_register_source(
        obs_filter_get_parent(filterSource), qUtf8Printable(saveReplayName), qUtf8Printable(saveReplayDescription),
        onSaveReplayBufferHotkeyPressed, this
    );
}

// Callback from filter audio
obs_audio_data *BranchOutputFilter::audioFilterCallback(void *param, obs_audio_data *audioData)
{
    auto filter = static_cast<BranchOutputFilter *>(param);

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
        auto filter = static_cast<BranchOutputFilter *>(data);
        filter->addCallback(source);
    };
    info.update = [](void *data, obs_data_t *settings) {
        auto filter = static_cast<BranchOutputFilter *>(data);
        filter->updateCallback(settings);
    };
    info.video_render = [](void *data, gs_effect_t *effect) {
        auto filter = static_cast<BranchOutputFilter *>(data);
        filter->videoRenderCallback(effect);
    };
    info.video_tick = [](void *data, float seconds) {
        auto filter = static_cast<BranchOutputFilter *>(data);
        filter->videoTickCallback(seconds);
    };
    info.filter_remove = [](void *data, obs_source_t *) {
        auto filter = static_cast<BranchOutputFilter *>(data);
        filter->removeCallback();
    };
    info.destroy = [](void *data) {
        auto filter = static_cast<BranchOutputFilter *>(data);
        filter->destroyCallback();
    };

    info.get_properties = [](void *data) -> obs_properties_t * {
        auto filter = static_cast<BranchOutputFilter *>(data);
        return filter->getProperties();
    };
    info.get_defaults = BranchOutputFilter::getDefaults;

    info.filter_audio = BranchOutputFilter::audioFilterCallback;

    return info;
}

//--- OBS Plugin Callbacks ---//

obs_source_info filterInfo;
obs_source_info proxySourceInfo;

bool obs_module_load()
{
    filterInfo = BranchOutputFilter::createFilterInfo();
    obs_register_source(&filterInfo);

    proxySourceInfo = FilterVideoCapture::createProxySourceInfo();
    obs_register_source(&proxySourceInfo);

    pthread_mutex_init(&pluginMutex, nullptr);

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
    // Remove and destroy status dock to avoid leaks (hotkeys, timers, widgets)
    if (statusDock) {
        obs_frontend_remove_dock("BranchOutputStatusDock");
    }

    pthread_mutex_destroy(&pluginMutex);

    obs_log(LOG_INFO, "Plugin unloaded");
}
