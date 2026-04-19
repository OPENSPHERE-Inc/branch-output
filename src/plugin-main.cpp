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

#include <atomic>

#include <QRegularExpression>
#include <QThread>

#include "audio/audio-capture.hpp"
#include "video/filter-video-capture.hpp"
#include "plugin-support.h"
#include "plugin-main.hpp"
#include "utils.hpp"

#define SETTINGS_JSON_NAME "recently.json"
#define FILTER_ID "osi_branch_output"
#define AVAILAVILITY_CHECK_INTERVAL_NS 1000000000ULL
#define TASK_INTERVAL_MS 1000

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// Atomic so proc-handler callers on worker threads can load without a race,
// and obs_module_unload() can publish nullptr with well-defined ordering
// before obs_frontend_remove_dock() destroys the widget.
std::atomic<BranchOutputStatusDock *> statusDock{nullptr};
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
      outputGracefullyStopping(false),
      streamingIndividualStopping(false),
      blankingOutputActive(false),
      blankingAudioMuted(false),
      recordingUserEnabled(obs_data_get_bool(settings, "recording_output_enabled")),
      replayBufferUserEnabled(obs_data_get_bool(settings, "replay_buffer_output_enabled")),
      recordingOutput(nullptr),
      videoEncoder(nullptr),
      videoOutput(nullptr),
      view(nullptr),
      useFilterInput(false),
      filterVideoCapture(nullptr),
      width(0),
      height(0),
      cropScene(nullptr),
      toggleEnableHotkeyPairId(OBS_INVALID_HOTKEY_PAIR_ID),
      splitRecordingHotkeyId(OBS_INVALID_HOTKEY_ID),
      togglePauseRecordingHotkeyPairId(OBS_INVALID_HOTKEY_PAIR_ID),
      addChapterToRecordingHotkeyId(OBS_INVALID_HOTKEY_ID),
      splitRecordingEnabled(false),
      recordingSettingsOverridden(false),
      replayBufferActive(false),
      saveReplayBufferHotkeyId(OBS_INVALID_HOTKEY_ID),
      enableAllStreamingHotkeyId(OBS_INVALID_HOTKEY_ID),
      disableAllStreamingHotkeyId(OBS_INVALID_HOTKEY_ID),
      toggleRecordingHotkeyPairId(OBS_INVALID_HOTKEY_PAIR_ID),
      toggleReplayBufferHotkeyPairId(OBS_INVALID_HOTKEY_PAIR_ID)
{
    // DO NOT use obs_filter_get_parent() in this function (It'll return nullptr)
    obs_log(LOG_DEBUG, "%s: BranchOutputFilter creating", qUtf8Printable(name));
    obs_log(LOG_DEBUG, "filter_settings_json=%s", obs_data_get_json(settings));

    // Per-stream user-enabled flags and hotkey IDs
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        auto key = QString("streaming_output_enabled_%1").arg(i);
        streamingUserEnabled[i].store(obs_data_get_bool(settings, qUtf8Printable(key)), std::memory_order_relaxed);
        toggleStreamingServiceHotkeyPairIds[i] = OBS_INVALID_HOTKEY_PAIR_ID;
    }

    // Do not use memset
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        audios[i] = {0};
    }

    pthread_mutex_init_recursive(&outputMutex);
    pthread_mutex_init_recursive(&audioMutex);

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

    // Migrate streaming_enabled (for pre-existing filters without this key)
    if (!obs_data_has_user_value(settings, "streaming_enabled")) {
        bool hasAnyServer = countEnabledStreamings(settings) > 0;
        obs_data_set_bool(settings, "streaming_enabled", hasAnyServer);
    }

    // Fiter activate immediately when "server" or "stream_recording" or "replay_buffer" is exists.
    initialized = isStreamingGroupEnabled(settings) || obs_data_get_bool(settings, "stream_recording") ||
                  obs_data_get_bool(settings, "replay_buffer");

    // Register proc handlers for external script access. These handlers
    // live on filterSource and die with it. Scripts keep us alive while a
    // call is in flight by holding a strong ref via obs_get_source_by_uuid()
    // (weak-ref CAS refuses to bump a strong count of 0).
    //
    // FIXME: libobs has no proc_handler_remove(). If it gains one, pair
    // unregistration with ~BranchOutputFilter().
    proc_handler_t *ph = obs_source_get_proc_handler(filterSource);
    proc_handler_add(
        ph, "void override_replay_buffer_filename_format(in string format)", onOverrideReplayBufferFilenameFormat, this
    );
    proc_handler_add(
        ph, "void override_recording_filename_format(in string format)", onOverrideRecordingFilenameFormat, this
    );

    obs_log(LOG_INFO, "%s: BranchOutputFilter created", qUtf8Printable(name));
}

BranchOutputFilter::~BranchOutputFilter()
{
    pthread_mutex_destroy(&outputMutex);
    pthread_mutex_destroy(&audioMutex);
}

void BranchOutputFilter::unregisterAllHotkeys()
{
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
    if (enableAllStreamingHotkeyId != OBS_INVALID_HOTKEY_ID) {
        obs_hotkey_unregister(enableAllStreamingHotkeyId);
        enableAllStreamingHotkeyId = OBS_INVALID_HOTKEY_ID;
    }
    if (disableAllStreamingHotkeyId != OBS_INVALID_HOTKEY_ID) {
        obs_hotkey_unregister(disableAllStreamingHotkeyId);
        disableAllStreamingHotkeyId = OBS_INVALID_HOTKEY_ID;
    }
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        if (toggleStreamingServiceHotkeyPairIds[i] != OBS_INVALID_HOTKEY_PAIR_ID) {
            obs_hotkey_pair_unregister(toggleStreamingServiceHotkeyPairIds[i]);
            toggleStreamingServiceHotkeyPairIds[i] = OBS_INVALID_HOTKEY_PAIR_ID;
        }
    }
    if (toggleRecordingHotkeyPairId != OBS_INVALID_HOTKEY_PAIR_ID) {
        obs_hotkey_pair_unregister(toggleRecordingHotkeyPairId);
        toggleRecordingHotkeyPairId = OBS_INVALID_HOTKEY_PAIR_ID;
    }
    if (toggleReplayBufferHotkeyPairId != OBS_INVALID_HOTKEY_PAIR_ID) {
        obs_hotkey_pair_unregister(toggleReplayBufferHotkeyPairId);
        toggleReplayBufferHotkeyPairId = OBS_INVALID_HOTKEY_PAIR_ID;
    }
}

// OBS hotkey persistence note:
// OBS persists hotkey keybindings in source->context.hotkey_data using the hotkey **name string**
// as the dictionary key (NOT the numeric obs_hotkey_id / obs_hotkey_pair_id).
// When a hotkey is unregistered and re-registered with the same name string,
// obs_hotkey_register_internal() automatically restores saved keybindings from context.hotkey_data.
// Therefore, unregister→re-register cycles do NOT lose user keybindings.
// See: libobs/obs-hotkey.c — obs_hotkey_register_internal(), load_bindings(), enum_save_hotkey().
//
// This function conditionally registers hotkeys based on which output types are currently enabled
// in settings, keeping OBS's hotkey settings UI clean. It is called from:
//   - addCallback() — initial registration when filter is added
//   - filterRenamedSignal handler — re-register with updated description text
//   - updateCallback() — re-register when settings change (output types enabled/disabled)
void BranchOutputFilter::registerHotkey()
{
    auto parent = obs_filter_get_parent(filterSource);
    if (!parent) {
        return;
    }

    // Unregister all previous hotkeys
    unregisterAllHotkeys();

    auto uuid = obs_source_get_uuid(filterSource);
    OBSDataAutoRelease settings = obs_source_get_settings(filterSource);

    // Register enable/disable filter hotkey (always registered)
    auto enableFilterName = QString("EnableFilter.%1").arg(uuid);
    auto enableFilterDescription = QString(obs_module_text("EnableHotkey")).arg(name);
    auto disableFilterName = QString("DisableFilter.%1").arg(uuid);
    auto disableFilterDescription = QString(obs_module_text("DisableHotkey")).arg(name);

    toggleEnableHotkeyPairId = obs_hotkey_pair_register_source(
        parent, qUtf8Printable(enableFilterName), qUtf8Printable(enableFilterDescription),
        qUtf8Printable(disableFilterName), qUtf8Printable(disableFilterDescription), onEnableFilterHotkeyPressed,
        onDisableFilterHotkeyPressed, this, this
    );

    // --- Streaming hotkeys (only when streaming is enabled) ---
    if (isStreamingGroupEnabled(settings)) {
        // Enable/disable all streaming hotkeys
        auto enableAllStreamingName = QString("EnableAllStreaming.%1").arg(uuid);
        auto enableAllStreamingDesc = QString(obs_module_text("EnableAllStreamingHotkey")).arg(name);

        enableAllStreamingHotkeyId = obs_hotkey_register_source(
            parent, qUtf8Printable(enableAllStreamingName), qUtf8Printable(enableAllStreamingDesc),
            onEnableAllStreamingHotkeyPressed, this
        );

        auto disableAllStreamingName = QString("DisableAllStreaming.%1").arg(uuid);
        auto disableAllStreamingDesc = QString(obs_module_text("DisableAllStreamingHotkey")).arg(name);

        disableAllStreamingHotkeyId = obs_hotkey_register_source(
            parent, qUtf8Printable(disableAllStreamingName), qUtf8Printable(disableAllStreamingDesc),
            onDisableAllStreamingHotkeyPressed, this
        );

        // Per-slot streaming enable/disable hotkeys (only for configured slots)
        for (size_t i = 0; i < MAX_SERVICES; i++) {
            if (!isStreamingEnabled(settings, i)) {
                continue;
            }

            auto enableName = QString("EnableStreamingService%1.%2").arg(i).arg(uuid);
            auto enableDesc = QString(obs_module_text("EnableStreamingServiceHotkey")).arg(name).arg(i + 1);
            auto disableName = QString("DisableStreamingService%1.%2").arg(i).arg(uuid);
            auto disableDesc = QString(obs_module_text("DisableStreamingServiceHotkey")).arg(name).arg(i + 1);

            toggleStreamingServiceHotkeyPairIds[i] = obs_hotkey_pair_register_source(
                parent, qUtf8Printable(enableName), qUtf8Printable(enableDesc), qUtf8Printable(disableName),
                qUtf8Printable(disableDesc), onEnableStreamingServiceHotkeyPressed,
                onDisableStreamingServiceHotkeyPressed, this, this
            );
        }
    }

    // --- Recording hotkeys (only when recording is enabled) ---
    if (isRecordingEnabled(settings)) {
        auto splitName = QString("SplitRecordingFile.%1").arg(uuid);
        auto splitDescription = QString(obs_module_text("SplitRecordingFileHotkey")).arg(name);

        splitRecordingHotkeyId = obs_hotkey_register_source(
            parent, qUtf8Printable(splitName), qUtf8Printable(splitDescription), onSplitRecordingFileHotkeyPressed, this
        );

        auto pauseRecordingName = QString("PauseRecording.%1").arg(uuid);
        auto pauseRecordingDescription = QString(obs_module_text("PauseRecordingHotkey")).arg(name);
        auto unpauseRecordingName = QString("UnpauseRecording.%1").arg(uuid);
        auto unpauseRecordingDescription = QString(obs_module_text("UnpauseRecordingHotkey")).arg(name);

        togglePauseRecordingHotkeyPairId = obs_hotkey_pair_register_source(
            parent, qUtf8Printable(pauseRecordingName), qUtf8Printable(pauseRecordingDescription),
            qUtf8Printable(unpauseRecordingName), qUtf8Printable(unpauseRecordingDescription),
            onPauseRecordingHotkeyPressed, onUnpauseRecordingHotkeyPressed, this, this
        );

        auto addChapterName = QString("AddChapterToRecordingFile.%1").arg(uuid);
        auto addChapterDescription = QString(obs_module_text("AddChapterToRecordingFileHotkey")).arg(name);
        addChapterToRecordingHotkeyId = obs_hotkey_register_source(
            parent, qUtf8Printable(addChapterName), qUtf8Printable(addChapterDescription),
            onAddChapterToRecordingFileHotkeyPressed, this
        );

        // Enable/disable recording hotkey
        auto enableRecName = QString("EnableRecordingIndividual.%1").arg(uuid);
        auto enableRecDesc = QString(obs_module_text("EnableRecordingIndividualHotkey")).arg(name);
        auto disableRecName = QString("DisableRecordingIndividual.%1").arg(uuid);
        auto disableRecDesc = QString(obs_module_text("DisableRecordingIndividualHotkey")).arg(name);

        toggleRecordingHotkeyPairId = obs_hotkey_pair_register_source(
            parent, qUtf8Printable(enableRecName), qUtf8Printable(enableRecDesc), qUtf8Printable(disableRecName),
            qUtf8Printable(disableRecDesc), onEnableRecordingHotkeyPressed, onDisableRecordingHotkeyPressed, this, this
        );
    }

    // --- Replay buffer hotkeys (only when replay buffer is enabled) ---
    if (isReplayBufferEnabled(settings)) {
        auto saveReplayName = QString("SaveReplayBuffer.%1").arg(uuid);
        auto saveReplayDescription = QString(obs_module_text("SaveReplayBufferHotkey")).arg(name);
        saveReplayBufferHotkeyId = obs_hotkey_register_source(
            parent, qUtf8Printable(saveReplayName), qUtf8Printable(saveReplayDescription),
            onSaveReplayBufferHotkeyPressed, this
        );

        // Enable/disable replay buffer hotkey
        auto enableReplayName = QString("EnableReplayBufferIndividual.%1").arg(uuid);
        auto enableReplayDesc = QString(obs_module_text("EnableReplayBufferIndividualHotkey")).arg(name);
        auto disableReplayName = QString("DisableReplayBufferIndividual.%1").arg(uuid);
        auto disableReplayDesc = QString(obs_module_text("DisableReplayBufferIndividualHotkey")).arg(name);

        toggleReplayBufferHotkeyPairId = obs_hotkey_pair_register_source(
            parent, qUtf8Printable(enableReplayName), qUtf8Printable(enableReplayDesc),
            qUtf8Printable(disableReplayName), qUtf8Printable(disableReplayDesc), onEnableReplayBufferHotkeyPressed,
            onDisableReplayBufferHotkeyPressed, this, this
        );
    }
}

// Caller must hold outputMutex.
// Idempotent: if infrastructure already exists, return true.
// On failure after partial resource creation, all resources are cleaned up
// so that the next call can retry from a clean state.
bool BranchOutputFilter::ensureInfrastructure(obs_data_t *settings)
{
    if (view) {
        return true;
    }

    // Abort when obs initializing or filter disabled.
    if (!obs_initialized() || !obs_source_enabled(filterSource)) {
        obs_log(LOG_ERROR, "%s: Ignore unavailable filter", qUtf8Printable(name));
        return false;
    }

    // Retrieve filter source
    auto parent = obs_filter_get_parent(filterSource);
    if (!parent) {
        obs_log(LOG_ERROR, "%s: Filter source not found", qUtf8Printable(name));
        return false;
    }

    // Ignore private sources
    if (sourceIsPrivate(parent)) {
        obs_log(LOG_ERROR, "%s: Ignore private source", qUtf8Printable(name));
        return false;
    }

    // Mandatory parameters
    if (!isStreamingGroupEnabled(settings) && !isRecordingEnabled(settings) && !isReplayBufferEnabled(settings)) {
        obs_log(LOG_ERROR, "%s: Nothing to do", qUtf8Printable(name));
        return false;
    }

    bool blankWhenHidden = obs_data_get_bool(settings, "blank_when_not_visible");
    bool muteWhenHidden = obs_data_get_bool(settings, "mute_audio_when_blank");

    obs_video_info ovi = {0};
    if (!obs_get_video_info(&ovi)) {
        // Abort when no video situation
        obs_log(LOG_ERROR, "%s: No video", qUtf8Printable(name));
        return false;
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

    // Treat 0x0 crop result as source collapse — abort silently so that
    // the interval timer can retry when the resolution becomes compatible.
    auto crop = calculateCrop(width, height, settings);
    if (!crop) {
        // Abort when crop produces invalid resolution (0x0)
        obs_log(LOG_DEBUG, "%s: Crop produces invalid resolution, treat as collapsed source", qUtf8Printable(name));
        return false;
    }

    determineOutputResolution(settings, &ovi, *crop);

    if (ovi.output_width == 0 || ovi.output_height == 0 || ovi.fps_den == 0 || ovi.fps_num == 0) {
        // Abort when invalid video parameters situation
        obs_log(LOG_DEBUG, "%s: Invalid video spec", qUtf8Printable(name));
        return false;
    }

    // Update active revision with stored settings.
    activeSettingsRev = storedSettingsRev;

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
            return false;
        }

        if (crop->width != width || crop->height != height) {
            filterVideoCapture->setCrop(*crop);
        }

        view = obs_view_create();
        obs_view_set_source(view, 0, filterVideoCapture->getProxySource());

        videoOutput = obs_view_add2(view, &ovi);
        if (!videoOutput) {
            obs_log(LOG_ERROR, "%s: Video output association failed", qUtf8Printable(name));
            // releaseInfrastructureIfIdle() safely handles partially-initialized state:
            // - OBS RAII wrappers accept nullptr assignment
            // - AudioCapture/filterVideoCapture pointers are nullptr-checked before delete
            // - FilterVideoCapture::setActive(false) on a never-activated instance is safe
            //   (simply stores false to atomic bool)
            releaseInfrastructureIfIdle();
            return false;
        }
        filterVideoCapture->setActive(true);
    } else {
        // Source output mode (default): use obs_view for the parent source
        view = obs_view_create();

        if (crop->width != width || crop->height != height) {
            cropScene = obs_scene_create_private("branch_output_crop");
            obs_sceneitem_t *item = obs_scene_add(cropScene, parent);

            struct obs_sceneitem_crop itemCrop;
            itemCrop.left = (int)crop->left;
            itemCrop.top = (int)crop->top;
            itemCrop.right = (int)(width - crop->left - crop->width);
            itemCrop.bottom = (int)(height - crop->top - crop->height);
            obs_sceneitem_set_crop(item, &itemCrop);

            obs_view_set_source(view, 0, obs_scene_get_source(cropScene));
        } else {
            obs_view_set_source(view, 0, parent);
        }

        videoOutput = obs_view_add2(view, &ovi);
        if (!videoOutput) {
            obs_log(LOG_ERROR, "%s: Video output association failed", qUtf8Printable(name));
            releaseInfrastructureIfIdle();
            return false;
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
        releaseInfrastructureIfIdle();
        return false;
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

            auto audioSourceUuid = obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("audio_source")));
            if (!strcmp(audioSourceUuid, "disabled")) {
                // Disabled track
                obs_log(LOG_INFO, "%s: Track %d is disabled", qUtf8Printable(name), track);
                continue;

            } else if (!strcmp(audioSourceUuid, "no_audio")) {
                // Silence audio
                obs_log(LOG_INFO, "%s: Use silence for track %d (%s)", qUtf8Printable(name), track, audioDest);

                audioContext->capture =
                    new AudioCapture("Silence", ai.samples_per_sec, ai.speakers, AudioCapture::silenceCapture, this);
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
                    releaseInfrastructureIfIdle();
                    return false;
                }
                obs_log(
                    LOG_INFO, "%s: Use master audio track No.%d for track %d (%s)", qUtf8Printable(name), masterTrack,
                    track, audioDest
                );

                if (blankWhenHidden && muteWhenHidden) {
                    audioContext->capture =
                        new MasterAudioCapture(masterTrack - 1, ai.samples_per_sec, ai.speakers, this);
                    audioContext->audio = audioContext->capture->getAudio();
                    audioContext->mixIndex = 0;
                    audioContext->name = audioContext->capture->getName();
                } else {
                    audioContext->mixIndex = masterTrack - 1;
                    audioContext->audio = obs_get_audio();
                    audioContext->name = QTStr("MasterTrack%1").arg(masterTrack);
                }

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
                        LOG_WARNING, "%s: Ignore audio source for track %d (%s)", qUtf8Printable(name), track, audioDest
                    );
                    continue;
                }

                // Use custom audio source
                obs_log(
                    LOG_INFO, "%s: Use %s audio for track %d", qUtf8Printable(name), obs_source_get_name(source), track
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
                releaseInfrastructureIfIdle();
                return false;
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
            releaseInfrastructureIfIdle();
            return false;
        }
    }

    //--- Setup video encoder ---//
    auto video_encoder_id = obs_data_get_string(settings, "video_encoder");

    videoEncoder = obs_video_encoder_create(video_encoder_id, qUtf8Printable(name), settings, nullptr);
    if (!videoEncoder) {
        obs_log(LOG_ERROR, "%s: Video encoder creation failed", qUtf8Printable(name));
        releaseInfrastructureIfIdle();
        return false;
    }

    // Apply frame rate divisor
    auto fpsDivider = (uint32_t)obs_data_get_int(settings, "fps_divider");
    if (fpsDivider > 1) {
        // Validate: fps_num must be evenly divisible by the divisor
        // This ensures clean frame rate for both integer (60, 30, 24) and
        // NTSC (60000/1001, 30000/1001) source rates.
        bool valid = (ovi.fps_num % fpsDivider) == 0;

        if (!valid) {
            // Fallback: search downward for nearest valid divisor
            uint32_t originalDivider = fpsDivider;
            fpsDivider = 1;
            for (uint32_t d = originalDivider - 1; d > 1; d--) {
                if ((ovi.fps_num % d) == 0) {
                    fpsDivider = d;
                    break;
                }
            }
            obs_log(
                LOG_WARNING, "%s: Frame rate divider 1/%u invalid for %u/%u fps, falling back to 1/%u",
                qUtf8Printable(name), originalDivider, ovi.fps_num, ovi.fps_den, fpsDivider
            );
        }

        if (fpsDivider > 1) {
            if (!obs_encoder_set_frame_rate_divisor(videoEncoder, fpsDivider)) {
                obs_log(LOG_WARNING, "%s: Failed to set frame rate divisor to %u", qUtf8Printable(name), fpsDivider);
            } else {
                obs_log(LOG_INFO, "%s: Frame rate divisor set to 1/%u", qUtf8Printable(name), fpsDivider);
            }
        }
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
            releaseInfrastructureIfIdle();
            return false;
        }
        obs_encoder_set_audio(audioContext->encoder, audioContext->audio);
    }

    if (blankWhenHidden) {
        bool visibleInProgram = sourceVisibleInProgram(parent);
        setBlankingActive(!visibleInProgram, muteWhenHidden, parent);
    }

    return true;
}

// Start all enabled outputs. User intent flags (streamingUserEnabled, etc.)
// are intentionally respected: if the user has disabled a specific output type
// via the status dock checkbox, it stays disabled even after a restartOutput()
// triggered by settings changes.
void BranchOutputFilter::startOutput(obs_data_t *settings)
{
    // Force release references
    stopOutput();

    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        // Abort if outputs already active
        if (countActiveStreamings() > 0 || recordingActive || replayBufferActive) {
            obs_log(LOG_ERROR, "%s: Ignore unavailable filter", qUtf8Printable(name));
            return;
        }

        // Skip infrastructure setup if the user has disabled all output types
        // via the status dock checkboxes. Without this check, interlock modes like
        // ALWAYS_ON would rebuild and immediately tear down infrastructure every tick.
        // Note: isAnyStreamingUserEnabled(settings) only checks configured service
        // slots (service_count), not all MAX_SERVICES, because unconfigured slots
        // are not visible in the status dock and their checkboxes cannot be toggled.
        if (!isAnyStreamingUserEnabled(settings) && !isRecordingUserEnabled() && !isReplayBufferUserEnabled()) {
            return;
        }

        if (!ensureInfrastructure(settings)) {
            return;
        }

        bool anyStarted = false;
        if (isRecordingUserEnabled()) {
            anyStarted |= createAndStartRecordingOutputChecked(settings);
        }
        if (isReplayBufferUserEnabled()) {
            anyStarted |= createAndStartReplayBufferChecked(settings);
        }
        if (isAnyStreamingUserEnabled(settings)) {
            anyStarted |= createAndStartStreamingOutputs(settings);
        }

        // Release infrastructure if all outputs failed to start
        if (!anyStarted) {
            releaseInfrastructureIfIdle();
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
        obs_data_erase(recently_settings, "streaming_enabled");
        obs_data_erase(recently_settings, "replay_buffer");
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
        obs_data_erase(recently_settings, "fps_divider");
        obs_data_apply(settings, recently_settings);
    }

    obs_log(LOG_INFO, "Recently settings loaded");
}

// Caller must hold outputMutex.
// Releases shared infrastructure (view, encoders, audio) if all outputs are idle.
void BranchOutputFilter::releaseInfrastructureIfIdle()
{
    // Only release if all outputs are stopped
    if (countActiveStreamings() > 0 || recordingActive || recordingPending || replayBufferActive) {
        return;
    }

    pthread_mutex_lock(&audioMutex);
    {
        OBSMutexAutoUnlock audioLocked(&audioMutex);

        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &audios[i];
            audioContext->encoder = nullptr;

            if (audioContext->capture) {
                delete audioContext->capture;
                audioContext->capture = nullptr;
            }
        }
    }

    videoEncoder = nullptr;

    if (filterVideoCapture) {
        filterVideoCapture->setActive(false);
        delete filterVideoCapture;
        filterVideoCapture = nullptr;
    }

    cropScene = nullptr;

    if (view) {
        obs_view_set_source(view, 0, nullptr);
        obs_view_remove(view);
    }

    view = nullptr;
    videoOutput = nullptr;
    useFilterInput = false;
    blankingOutputActive = false;
    blankingAudioMuted = false;
}

void BranchOutputFilter::stopOutput()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        stopRecordingOutput();
        stopReplayBufferOutput();

        for (size_t i = 0; i < MAX_SERVICES; i++) {
            stopStreamingOutput(i);
        }

        // Reset individual stopping flag so it does not persist across
        // full stop/restart cycles (e.g., filter eye-icon toggle while
        // streamingIndividualStopping is still true).
        streamingIndividualStopping = false;

        releaseInfrastructureIfIdle();
    }
}

void BranchOutputFilter::restartOutput()
{
    if (countActiveStreamings() > 0 || recordingActive || replayBufferActive) {
        stopOutput();
    }

    OBSDataAutoRelease settings = obs_source_get_settings(filterSource);
    if (isStreamingGroupEnabled(settings) || isRecordingEnabled(settings) || isReplayBufferEnabled(settings)) {
        startOutput(settings);
    }
}

void BranchOutputFilter::setAudioCapturesActive(bool active)
{
    pthread_mutex_lock(&audioMutex);
    {
        OBSMutexAutoUnlock locked(&audioMutex);

        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &audios[i];
            if (audioContext->capture) {
                audioContext->capture->setActive(active);
            }
        }
    }
}

void BranchOutputFilter::saveCallback(obs_data_t *settings)
{
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        auto key = QString("streaming_output_enabled_%1").arg(i);
        obs_data_set_bool(settings, qUtf8Printable(key), isStreamingUserEnabled(i));
    }
    obs_data_set_bool(settings, "recording_output_enabled", isRecordingUserEnabled());
    obs_data_set_bool(settings, "replay_buffer_output_enabled", isReplayBufferUserEnabled());
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
            obs_view_set_source(view, 0, nullptr);
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
            } else if (cropScene) {
                obs_view_set_source(view, 0, obs_scene_get_source(cropScene));
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

// Controlling output status here.
// Start / Stop should only heppen in this function as possible because rapid manipulation caused crash easily.
// NOTE: Becareful this function is called so offen.
void BranchOutputFilter::onIntervalTimerTimeout()
{
    // Block output initiation until filter is active.
    if (!initialized) {
        return;
    }

    if (outputGracefullyStopping) {
        stopOutputGracefully();
        return;
    }

    auto *dock = statusDock.load();
    auto interlockType = dock ? dock->getInterlockType() : INTERLOCK_TYPE_ALWAYS_ON;
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
            // Check interlock condition
            if (interlockType == INTERLOCK_TYPE_ALWAYS_OFF) {
                // Never start output
            } else if (interlockType == INTERLOCK_TYPE_STREAMING) {
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
            } else if (interlockType == INTERLOCK_TYPE_REPLAY_BUFFER) {
                if (obs_frontend_replay_buffer_active()) {
                    restartOutput();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_INDIVIDUAL) {
                // Individual start: follow OBS frontend state per output type.
                // Check both the user toggle (dock checkbox) and the filter setting
                // (whether the output type is configured) to avoid blocking subsequent
                // outputs when an unconfigured type matches first.
                OBSDataAutoRelease settings = obs_source_get_settings(filterSource);
                bool anyStarted = false;
                if (isAnyStreamingUserEnabled(settings) && obs_frontend_streaming_active() &&
                    isStreamingGroupEnabled(settings)) {
                    anyStarted |= startStreamingIndividual();
                }
                if (isRecordingUserEnabled() && obs_frontend_recording_active() && isRecordingEnabled(settings)) {
                    anyStarted |= startRecordingIndividual();
                }
                if (isReplayBufferUserEnabled() && obs_frontend_replay_buffer_active() &&
                    isReplayBufferEnabled(settings)) {
                    anyStarted |= startReplayBufferIndividual();
                }
                if (anyStarted) {
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

            // Start all eligible streaming slots as a single output group.
            // Returns true if any slot was started.
            // Guarded by streamingIndividualStopping to prevent starting slots while
            // a graceful stop is still in progress.
            auto startEligibleStreamings = [&]() -> bool {
                if (streamingIndividualStopping) {
                    return false;
                }
                bool anyStarted = false;
                for (size_t i = 0; i < MAX_SERVICES; i++) {
                    if (isStreamingUserEnabled(i) && !streamings[i].active && isStreamingEnabled(settings, i) &&
                        isStreamingGroupEnabled(settings)) {
                        if (startSingleStreamingIndividual(i)) {
                            anyStarted = true;
                        }
                    }
                }
                return anyStarted;
            };
            bool blankWhenHidden = obs_data_get_bool(settings, "blank_when_not_visible");
            bool muteWhenHidden = obs_data_get_bool(settings, "mute_audio_when_blank");

            // Check interlock condition
            if (interlockType == INTERLOCK_TYPE_ALWAYS_OFF) {
                // Always OFF: Stop output immediately
                stopOutputGracefully();
                return;
            } else if (interlockType == INTERLOCK_TYPE_STREAMING) {
                if (!obs_frontend_streaming_active()) {
                    // Stop output when streaming is not active
                    stopOutputGracefully();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_RECORDING) {
                if (!obs_frontend_recording_active()) {
                    // Stop output when recording is not active
                    stopOutputGracefully();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_STREAMING_RECORDING) {
                if (!obs_frontend_streaming_active() && !obs_frontend_recording_active()) {
                    // Stop output when streaming and recording are not active
                    stopOutputGracefully();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_VIRTUAL_CAM) {
                if (!obs_frontend_virtualcam_active()) {
                    // Stop output when virtual cam is not active
                    stopOutputGracefully();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_REPLAY_BUFFER) {
                if (!obs_frontend_replay_buffer_active()) {
                    // Stop output when replay buffer is not active
                    stopOutputGracefully();
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_INDIVIDUAL) {
                // Individual stop: follow OBS frontend state per output type.
                // Only checks the OBS frontend state here; user toggle (per-output checkbox)
                // is handled separately in the common per-output toggle block below.
                {
                    bool anyStopped = false;
                    if (!obs_frontend_streaming_active() && streamingActive) {
                        anyStopped |= stopStreamingIndividual();
                    }
                    if (!obs_frontend_recording_active() && (recordingActive || recordingPending)) {
                        anyStopped |= stopRecordingIndividual();
                    }
                    if (!obs_frontend_replay_buffer_active() && replayBufferActive) {
                        anyStopped |= stopReplayBufferIndividual();
                    }
                    if (anyStopped) {
                        return;
                    }
                }

                // Individual start for additional outputs while some are already active.
                {
                    bool anyStarted = false;
                    if (obs_frontend_streaming_active()) {
                        anyStarted |= startEligibleStreamings();
                    }
                    if (isRecordingUserEnabled() && obs_frontend_recording_active() && !recordingActive &&
                        !recordingPending && isRecordingEnabled(settings)) {
                        anyStarted |= startRecordingIndividual();
                    }
                    if (isReplayBufferUserEnabled() && obs_frontend_replay_buffer_active() && !replayBufferActive &&
                        isReplayBufferEnabled(settings)) {
                        anyStarted |= startReplayBufferIndividual();
                    }
                    if (anyStarted) {
                        return;
                    }
                }
            }

            // Per-output toggle check (all interlock modes including non-Individual)
            // Users can explicitly enable/disable each output type (streaming, recording,
            // replay buffer) via the status dock's output column checkbox, regardless of
            // the current interlock mode. This allows, for example, stopping recording
            // while keeping streaming active even in "Always ON" mode.
            // Only one start/stop per tick to avoid crash from rapid state transitions.
            // Streaming slots are treated as a single output group.
            {
                bool anyStopped = false;
                for (size_t i = 0; i < MAX_SERVICES; i++) {
                    if (!isStreamingUserEnabled(i) && streamings[i].active) {
                        anyStopped |= stopSingleStreamingIndividual(i);
                    }
                }
                if (!isRecordingUserEnabled() && (recordingActive || recordingPending)) {
                    anyStopped |= stopRecordingIndividual();
                }
                if (!isReplayBufferUserEnabled() && replayBufferActive) {
                    anyStopped |= stopReplayBufferIndividual();
                }
                if (anyStopped) {
                    return;
                }
            }

            // Retry graceful streaming stop if still in progress.
            // Placed after per-output toggle stops so recording/replay buffer stops
            // are not blocked during streaming graceful stop.
            if (streamingIndividualStopping) {
                stopStreamingIndividual();
                return;
            }

            // Per-output toggle re-enable check.
            // Only re-enable an output if the current interlock condition is met.
            // For ALWAYS_OFF, we never reach here (returned above).
            // For INDIVIDUAL, per-output start is handled in the Individual block above.
            // For other modes, re-enable only when the interlock condition is currently satisfied.
            // Note: No explicit interlock recheck is needed here because this code is only
            // reachable within the "outputs active" block, which means the interlock condition
            // was already satisfied when outputs were started and has not been violated (the
            // interlock stop checks above would have returned before reaching this point).
            if (interlockType != INTERLOCK_TYPE_INDIVIDUAL) {
                bool anyStarted = false;
                anyStarted |= startEligibleStreamings();
                if (isRecordingUserEnabled() && !recordingActive && !recordingPending && isRecordingEnabled(settings)) {
                    anyStarted |= startRecordingIndividual();
                }
                if (isReplayBufferUserEnabled() && !replayBufferActive && isReplayBufferEnabled(settings)) {
                    anyStarted |= startReplayBufferIndividual();
                }
                if (anyStarted) {
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
                    stopOutputGracefully();
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
                    bool sourceCollapsed = (sourceWidth == 0 || sourceHeight == 0);
                    bool cropCollapse = !calculateCrop(sourceWidth, sourceHeight, settings);

                    if (!sourceCollapsed && !cropCollapse) {
                        if (!obs_data_get_bool(settings, "keep_output_base_resolution")) {
                            // Restart output when source resolution was changed.
                            obs_log(LOG_INFO, "%s: Attempting restart the streaming output", qUtf8Printable(name));
                            startOutput(settings);
                            return;
                        }
                    } else {
                        // The source is collapsed or crop would produce 0x0
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
                                stopRecordingOutput(true);
                                return;
                            }
                        } else {
                            // Ignore source collapse
                        }
                    }
                }

                if (recordingPending && sourceWidth > 0 && sourceHeight > 0 &&
                    !!calculateCrop(sourceWidth, sourceHeight, settings)) {
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

            if (recordingSettingsOverridden) {
                if (recordingActive && recordingAlive && recordingOutput && obs_output_paused(recordingOutput)) {
                    // Recording is paused and alive: keep flag, restart will be triggered on unpause
                } else {
                    recordingSettingsOverridden = false;
                    if (recordingActive) {
                        if (recordingPending) {
                            // Recording is pending (source collapsed): stop output so it will be
                            // re-created with new settings when the source is uncollapsed.
                            obs_log(
                                LOG_INFO, "%s: Stopping recording output for settings override (pending)",
                                qUtf8Printable(name)
                            );
                            stopRecordingOutput(true);
                        } else {
                            obs_log(
                                LOG_INFO, "%s: Restarting recording for filename format change", qUtf8Printable(name)
                            );
                            restartRecordingOutput();
                        }
                    }
                }
            } else if (recordingActive && !recordingAlive) {
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
                stopOutputGracefully();
                return;
            }
        }
    }
}

void BranchOutputFilter::stopOutputGracefully()
{
    outputGracefullyStopping = true;

    // Lock out other output thread to prevent crash.
    // Lock order: pluginMutex -> outputMutex (consistent with stopOutput() and Individual stop functions).
    pthread_mutex_lock(&pluginMutex);
    {
        OBSMutexAutoUnlock pluginLocked(&pluginMutex);

        pthread_mutex_lock(&outputMutex);
        {
            OBSMutexAutoUnlock outputLocked(&outputMutex);

            // Stop recording and replay buffer immediately first (under outputMutex)
            stopRecordingOutput();
            stopReplayBufferOutput();

            if (!stopAllStreamingOutputsGracefully()) {
                return;
            }
        }
    }

    // All streaming has been stopped
    outputGracefullyStopping = false;

    // Finalize termination
    stopOutput();
}

std::optional<CropRect> BranchOutputFilter::calculateCrop(uint32_t srcWidth, uint32_t srcHeight, obs_data_t *settings)
{
    auto cropType = obs_data_get_string(settings, "crop_type");

    if (!cropType || !strcmp(cropType, "none")) {
        return CropRect{0, 0, srcWidth, srcHeight};
    }

    CropRect crop = {};

    if (!strcmp(cropType, "relative")) {
        auto top = (uint32_t)obs_data_get_int(settings, "crop_rel_top");
        auto right = (uint32_t)obs_data_get_int(settings, "crop_rel_right");
        auto bottom = (uint32_t)obs_data_get_int(settings, "crop_rel_bottom");
        auto left = (uint32_t)obs_data_get_int(settings, "crop_rel_left");

        if (left + right >= srcWidth || top + bottom >= srcHeight) {
            return std::nullopt;
        }

        crop.left = left;
        crop.top = top;
        crop.width = srcWidth - left - right;
        crop.height = srcHeight - top - bottom;

    } else if (!strcmp(cropType, "absolute")) {
        auto x = (uint32_t)obs_data_get_int(settings, "crop_abs_x");
        auto y = (uint32_t)obs_data_get_int(settings, "crop_abs_y");
        auto w = (uint32_t)obs_data_get_int(settings, "crop_abs_width");
        auto h = (uint32_t)obs_data_get_int(settings, "crop_abs_height");

        if (x >= srcWidth || y >= srcHeight) {
            return std::nullopt;
        }

        crop.left = x;
        crop.top = y;
        crop.width = (x + w > srcWidth) ? (srcWidth - x) : w;
        crop.height = (y + h > srcHeight) ? (srcHeight - y) : h;

    } else {
        return CropRect{0, 0, srcWidth, srcHeight};
    }

    // Round to multiples of 2 (encoder requirement)
    crop.left += (crop.left & 1);
    crop.top += (crop.top & 1);
    crop.width &= ~1u;
    crop.height &= ~1u;

    // Rounding can reduce dimensions to 0
    if (crop.width == 0 || crop.height == 0) {
        return std::nullopt;
    }

    return crop;
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

void BranchOutputFilter::determineOutputResolution(obs_data_t *settings, obs_video_info *ovi, const CropRect &crop)
{
    uint32_t baseWidth = crop.width;
    uint32_t baseHeight = crop.height;

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
        ovi->output_width = baseWidth * 3 / 4;
        ovi->output_height = baseHeight * 3 / 4;

    } else if (!strcmp(resolution, "half")) {
        // Rescale source resolution
        ovi->output_width = baseWidth / 2;
        ovi->output_height = baseHeight / 2;

    } else if (!strcmp(resolution, "quarter")) {
        // Rescale source resolution
        ovi->output_width = baseWidth / 4;
        ovi->output_height = baseHeight / 4;

    } else {
        // Copy source resolution
        ovi->output_width = baseWidth;
        ovi->output_height = baseHeight;
    }

    // Round up to a multiple of 2
    ovi->output_width += (ovi->output_width & 1);
    ovi->output_height += (ovi->output_height & 1);

    // Copy base resolution
    ovi->base_width = baseWidth;
    ovi->base_height = baseHeight;

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

QString BranchOutputFilter::applyFilenameFormatArgs(const QString &format, bool noSpace)
{
    QString sourceName = obs_source_get_name(obs_filter_get_parent(filterSource));
    QString filterName = qUtf8Printable(name);
    auto re = noSpace ? QRegularExpression("[\\s/\\\\.:;*?\"<>|&$,]") : QRegularExpression("[/\\\\.:;*?\"<>|&$,]");
    return QString(format).arg(sourceName.replace(re, "-")).arg(filterName.replace(re, "-"));
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
    if (auto *dock = statusDock.load()) {
        // Show in status dock (Thread-safe way)
        QMetaObject::invokeMethod(dock, "addFilter", Qt::QueuedConnection, Q_ARG(BranchOutputFilter *, this));
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

    // Re-register hotkeys (they depend on which outputs are enabled in settings).
    registerHotkey();

    // Update status dock
    if (auto *dock = statusDock.load()) {
        // Show in status dock (Thread-safe way)
        QMetaObject::invokeMethod(dock, "addFilter", Qt::QueuedConnection, Q_ARG(BranchOutputFilter *, this));
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

    // Update crop preview when source resolution changes
    if (cropPreview.isVisible()) {
        uint32_t curW, curH;
        getSourceResolution(curW, curH);
        if (curW > 0 && curH > 0 && cropPreview.resolutionChanged(curW, curH)) {
            OBSDataAutoRelease settings = obs_source_get_settings(filterSource);
            cropPreview.updateResolution(curW, curH, calculateCrop(curW, curH, settings));
        }
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

    // Draw crop preview rectangle overlay (main mix only, not encoded in branch output)
    cropPreview.render();
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

    if (auto *dock = statusDock.load()) {
        // Unregister from output status dock (In proper thread)
        QMetaObject::invokeMethod(dock, "removeFilter", Qt::QueuedConnection, Q_ARG(BranchOutputFilter *, this));
    }

    // Unregister hotkeys
    unregisterAllHotkeys();

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

// Callback from filter audio
obs_audio_data *BranchOutputFilter::audioFilterCallback(void *param, obs_audio_data *audioData)
{
    auto filter = static_cast<BranchOutputFilter *>(param);

    pthread_mutex_lock(&filter->audioMutex);
    {
        OBSMutexAutoUnlock locked(&filter->audioMutex);

        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &filter->audios[i];
            if (audioContext->capture && !audioContext->capture->hasSource()) {
                audioContext->capture->pushAudio(audioData);
            }
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

    info.save = [](void *data, obs_data_t *settings) {
        auto filter = static_cast<BranchOutputFilter *>(data);
        filter->saveCallback(settings);
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

    pthread_mutex_init_recursive(&pluginMutex);

    obs_log(LOG_INFO, "Plugin loaded successfully (version %s)", PLUGIN_VERSION);
    return true;
}

static void onGetFilterList(void *, calldata_t *cd)
{
    OBSDataAutoRelease wrapper = obs_data_create();
    OBSDataArrayAutoRelease array = obs_data_array_create();

    // Dispatch the QWidget read to the UI thread so this proc is safe from
    // any thread. BlockingQueuedConnection would deadlock if the caller is
    // already on the target thread, so short-circuit in that case.
    //
    // See obs_module_unload() for the residual race against widget destruction.
    auto *dock = statusDock.load();
    if (dock) {
        QList<BranchOutputFilterInfo> filterList;
        if (QThread::currentThread() == dock->thread()) {
            filterList = dock->getFilterList();
        } else {
            // Qt 6 returns false if the receiver was destroyed mid-call;
            // log it so callers can tell this apart from an empty list.
            const bool dispatched = QMetaObject::invokeMethod(
                dock, "getFilterList", Qt::BlockingQueuedConnection,
                Q_RETURN_ARG(QList<BranchOutputFilterInfo>, filterList)
            );
            if (!dispatched) {
                obs_log(
                    LOG_WARNING, "osi_branch_output_get_filter_list: UI thread dispatch failed "
                                 "(dock may have been destroyed); returning empty list"
                );
            }
        }

        for (const auto &info : filterList) {
            OBSDataAutoRelease entry = obs_data_create();
            obs_data_set_string(entry, "source_name", qUtf8Printable(info.sourceName));
            obs_data_set_string(entry, "source_uuid", qUtf8Printable(info.sourceUuid));
            obs_data_set_string(entry, "filter_name", qUtf8Printable(info.filterName));
            obs_data_set_string(entry, "filter_uuid", qUtf8Printable(info.filterUuid));
            obs_data_array_push_back(array, entry);
        }
    }

    obs_data_set_array(wrapper, "filters", array);

    // Defensive "{}" fallback: obs_data_get_json is expected to return a
    // valid pointer here but guarantee a non-NULL result to callers.
    const char *json = obs_data_get_json(wrapper);
    calldata_set_string(cd, "json", json ? json : "{}");
}

void obs_module_post_load()
{
    // Pre-register on the main thread before the proc below is published.
    // QList<T> auto-registers lazily; doing it up front closes the race if
    // the first cross-thread invocation beats the lazy registration.
    qRegisterMetaType<BranchOutputFilter *>();
    qRegisterMetaType<BranchOutputFilterInfo>();
    qRegisterMetaType<QList<BranchOutputFilterInfo>>();

    statusDock.store(BranchOutputFilter::createOutputStatusDock());

    // Register global proc handler for script access (obs-websocket style).
    // data=nullptr: onGetFilterList resolves the dock via the statusDock
    // atomic so the unload sequence is the single source of truth.
    //
    // FIXME: libobs has no removal API for the global proc table, so the
    // function pointer outlives obs_module_unload(). Callers must not dispatch
    // after unload.
    proc_handler_t *ph = obs_get_proc_handler();
    proc_handler_add(ph, "void osi_branch_output_get_filter_list(out string json)", onGetFilterList, nullptr);
}

void obs_module_unload()
{
    // Publish nullptr before destroying the widget so subsequent loads in
    // onGetFilterList() bail out. A worker that already holds a non-null
    // pointer will either (a) take the cross-thread branch and be released
    // by Qt 6's ~QObject() cleanup with invokeMethod() returning false
    // (implementation detail, not a spec guarantee), or (b) take the
    // same-thread branch, which is only reachable on the UI thread and
    // therefore cannot race this unloader.
    //
    // FIXME: narrow worker race remains. Proper fixes are an in-flight
    // counter (wait for zero before remove_dock) or QPointer + deleteLater
    // + processEvents drain. Current code accepts the race because the
    // worst observable effect is a silent empty result.
    if (statusDock.exchange(nullptr) != nullptr) {
        obs_frontend_remove_dock("BranchOutputStatusDock");
    }

    pthread_mutex_destroy(&pluginMutex);

    obs_log(LOG_INFO, "Plugin unloaded");
}
