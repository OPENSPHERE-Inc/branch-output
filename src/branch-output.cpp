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
#include <util/threading.h>
#include <util/platform.h>
#include <obs.hpp>

#include <utility>

#include <QRegularExpression>

#include "audio/audio-capture.hpp"
#include "plugin-support.h"
#include "branch-output.hpp"
#include "utils.hpp"

BranchOutput::BranchOutput(obs_data_t *settings, obs_source_t *source, QObject *parent)
    : QObject(parent),
      name(obs_source_get_name(source)),
      contextSource(source),
      initialized(false),
      recordingActive(false),
      recordingPending(false),
      outputGracefullyStopping(false),
      streamingIndividualStopping(false),
      recordingUserEnabled(obs_data_get_bool(settings, "recording_output_enabled")),
      replayBufferUserEnabled(obs_data_get_bool(settings, "replay_buffer_output_enabled")),
      recordingOutput(nullptr),
      videoEncoder(nullptr),
      videoOutput(nullptr),
      view(nullptr),
      infrastructureReady(false),
      videoOutputOwned(false),
      width(0),
      height(0),
      splitRecordingEnabled(false),
      addChapterToRecordingEnabled(false),
      toggleEnableHotkeyPairId(OBS_INVALID_HOTKEY_PAIR_ID),
      splitRecordingHotkeyId(OBS_INVALID_HOTKEY_ID),
      togglePauseRecordingHotkeyPairId(OBS_INVALID_HOTKEY_PAIR_ID),
      addChapterToRecordingHotkeyId(OBS_INVALID_HOTKEY_ID),
      recordingSettingsOverridden(false),
      replayBufferActive(false),
      saveReplayBufferHotkeyId(OBS_INVALID_HOTKEY_ID),
      enableAllStreamingHotkeyId(OBS_INVALID_HOTKEY_ID),
      disableAllStreamingHotkeyId(OBS_INVALID_HOTKEY_ID),
      toggleRecordingHotkeyPairId(OBS_INVALID_HOTKEY_PAIR_ID),
      toggleReplayBufferHotkeyPairId(OBS_INVALID_HOTKEY_PAIR_ID)
{
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
}

BranchOutput::~BranchOutput()
{
    pthread_mutex_destroy(&outputMutex);
    pthread_mutex_destroy(&audioMutex);
}

BranchOutput::AppliedSettings::AppliedSettings()
{
    pthread_mutex_init_recursive(&mutex);
}

BranchOutput::AppliedSettings::~AppliedSettings()
{
    pthread_mutex_destroy(&mutex);
}

// The live settings object keeps edits of the (deferred-update) properties dialog before
// Apply, so output start and configuration must read this copy instead.
void BranchOutput::AppliedSettings::replace(obs_data_t *settings)
{
    OBSDataAutoRelease copy = duplicateSettings(settings);

    pthread_mutex_lock(&mutex);
    {
        OBSMutexAutoUnlock locked(&mutex);
        std::swap(data, copy);
    }
    // "copy" now holds the previous snapshot and is released outside the lock.
}

OBSDataAutoRelease BranchOutput::AppliedSettings::get()
{
    pthread_mutex_lock(&mutex);
    OBSMutexAutoUnlock locked(&mutex);

    obs_data_addref(data);
    return OBSDataAutoRelease(data.Get());
}

// Caller must hold outputMutex.
// Idempotent: if infrastructure already exists, return true.
// On failure after partial resource creation, all resources are cleaned up
// so that the next call can retry from a clean state.
bool BranchOutput::ensureInfrastructure(obs_data_t *settings)
{
    if (infrastructureReady) {
        return true;
    }

    // Abort when obs initializing or filter disabled.
    if (!obs_initialized() || !obs_source_enabled(contextSource)) {
        obs_log(LOG_ERROR, "%s: Ignore unavailable filter", qUtf8Printable(name));
        return false;
    }

    if (!validateInput()) {
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
    selectVideoInputMode(settings);

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

    // Record which snapshot this infrastructure is built from.
    activeSettings = settings;

    //--- Open video output ---//
    if (!setupVideoInput(settings, &ovi, *crop)) {
        return false;
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
        if (!setupDefaultAudio(ai)) {
            return false;
        }
    }

    //--- Setup video encoder ---//
    auto video_encoder_id = obs_data_get_string(settings, "video_encoder");

    // The encoder shares the passed settings object and writes into it (get_defaults, migrations).
    OBSDataAutoRelease encoderSettings = duplicateSettings(settings);
    videoEncoder = obs_video_encoder_create(video_encoder_id, qUtf8Printable(name), encoderSettings, nullptr);
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

    infrastructureReady = true;

    evaluateBlanking(settings);

    return true;
}

// Start every output type that is enabled in settings and toggled on in the status dock.
// In Individual interlock mode, only the types whose OBS counterpart output is active are started.
void BranchOutput::startOutput(obs_data_t *settings, int interlockType)
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

        bool streamingEligible = isStreamingGroupEnabled(settings) && isAnyStreamingUserEnabled(settings);
        bool recordingEligible = isRecordingEnabled(settings) && isRecordingUserEnabled();
        bool replayBufferEligible = isReplayBufferEnabled(settings) && isReplayBufferUserEnabled();
        if (interlockType == INTERLOCK_TYPE_INDIVIDUAL) {
            streamingEligible = streamingEligible && obs_frontend_streaming_active();
            recordingEligible = recordingEligible && obs_frontend_recording_active();
            replayBufferEligible = replayBufferEligible && obs_frontend_replay_buffer_active();
        }

        // Skip infrastructure setup when no output type is eligible, so that interlock modes
        // like ALWAYS_ON do not rebuild and immediately tear down infrastructure every tick.
        if (!streamingEligible && !recordingEligible && !replayBufferEligible) {
            return;
        }

        if (!ensureInfrastructure(settings)) {
            return;
        }

        bool anyStarted = false;
        if (recordingEligible) {
            anyStarted |= createAndStartRecordingOutputChecked(settings);
        }
        if (replayBufferEligible) {
            anyStarted |= createAndStartReplayBufferChecked(settings);
        }
        if (streamingEligible) {
            anyStarted |= createAndStartStreamingOutputs(settings);
        }

        // Release infrastructure if all outputs failed to start
        if (!anyStarted) {
            releaseInfrastructureIfIdle();
        }
    }
}

void BranchOutput::loadProfile(obs_data_t *settings)
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

void BranchOutput::loadRecently(obs_data_t *settings)
{
    obs_log(LOG_DEBUG, "Recently settings loading");
    OBSString path = obs_module_get_config_path(obs_current_module(), RECENTLY_SETTINGS_JSON_NAME);
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
        // Hotkey bindings are keyed by the owning filter's UUID and must not be inherited.
        obs_data_erase(recently_settings, HOTKEY_BINDINGS_KEY);
        obs_data_apply(settings, recently_settings);
    }

    obs_log(LOG_INFO, "Recently settings loaded");
}

// Caller must hold outputMutex.
// Releases shared infrastructure (view, encoders, audio) if all outputs are idle.
void BranchOutput::releaseInfrastructureIfIdle()
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

            // Close the audio_t (joins its worker thread) before releasing the
            // encoder. The worker may still be inside receive_audio() for this
            // encoder; the encoder (and its pause.mutex) must outlive that final
            // iteration, otherwise the worker unlocks a destroyed mutex.
            if (audioContext->capture) {
                delete audioContext->capture;
                audioContext->capture = nullptr;
            }

            // audio is a borrowed pointer (capture-owned or obs_get_audio());
            // clear it so it does not dangle until the next startOutput().
            audioContext->audio = nullptr;
            audioContext->encoder = nullptr;
        }
    }

    // Stop the private video_t (joins its worker thread) before releasing the encoder, for
    // the same reason as the audio_t above. obs_view_remove() only flags the mix for removal
    // on the graphics thread, so it is not a synchronization point.
    // FIXME: This covers the raw video worker only. A GPU video encoder is driven from libobs'
    // GPU encode thread via the mix's gpu_encoders array, which only obs_encoder_stop() detaches.
    // obs_output_active() turning false is no boundary: an output whose start is unresolved already
    // reports false. Resolve every output's start before releasing infrastructure (issue #161).
    if (videoOutput && videoOutputOwned) {
        video_output_stop(videoOutput);
    }

    videoEncoder = nullptr;

    // FIXME: The view keeps rendering the proxy source until its source is cleared below, after
    // FilterVideoCapture is destroyed here. Clear the view's source before this call.
    teardownVideoInput();

    if (view && videoOutputOwned) {
        obs_view_set_source(view, 0, nullptr);
        obs_view_remove(view);
    }

    view = nullptr;
    videoOutput = nullptr;
    videoOutputOwned = false;
    infrastructureReady = false;
}

void BranchOutput::stopOutput()
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

void BranchOutput::restartOutput(int interlockType)
{
    if (countActiveStreamings() > 0 || recordingActive || recordingPending || replayBufferActive) {
        stopOutput();
    }

    auto applied = appliedSettings.get();
    if (isStreamingGroupEnabled(applied) || isRecordingEnabled(applied) || isReplayBufferEnabled(applied)) {
        startOutput(applied, interlockType);
    }
}

void BranchOutput::setAudioCapturesActive(bool active)
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

void BranchOutput::saveCallback(obs_data_t *settings)
{
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        auto key = QString("streaming_output_enabled_%1").arg(i);
        obs_data_set_bool(settings, qUtf8Printable(key), isStreamingUserEnabled(i));
    }
    obs_data_set_bool(settings, "recording_output_enabled", isRecordingUserEnabled());
    obs_data_set_bool(settings, "replay_buffer_output_enabled", isReplayBufferUserEnabled());

    snapshotHotkeyBindings(settings);
}

// Controlling output status here.
// Start / Stop should only heppen in this function as possible because rapid manipulation caused crash easily.
// NOTE: Becareful this function is called so offen.
void BranchOutput::onIntervalTimerTimeout()
{
    // Block output initiation until filter is active.
    if (!initialized) {
        return;
    }

    if (outputGracefullyStopping) {
        stopOutputGracefully();
        return;
    }

    auto *dock = loadStatusDock();
    auto interlockType = dock ? dock->getInterlockType() : INTERLOCK_TYPE_ALWAYS_ON;
    auto sourceEnabled = obs_source_enabled(contextSource);
    auto streamingActive = countActiveStreamings() > 0;

    if (!streamingActive && !recordingActive && !recordingPending && !replayBufferActive) {
        // Evaluate start condition
        if (!isInputAvailable()) {
            // Ignore when source in no longer exists in frontend
            return;
        }

        if (sourceEnabled) {
            // Check interlock condition
            if (interlockType == INTERLOCK_TYPE_ALWAYS_OFF) {
                // Never start output
            } else if (interlockType == INTERLOCK_TYPE_STREAMING) {
                if (obs_frontend_streaming_active()) {
                    restartOutput(interlockType);
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_RECORDING) {
                if (obs_frontend_recording_active()) {
                    restartOutput(interlockType);
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_STREAMING_RECORDING) {
                if (obs_frontend_streaming_active() || obs_frontend_recording_active()) {
                    restartOutput(interlockType);
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_VIRTUAL_CAM) {
                if (obs_frontend_virtualcam_active()) {
                    restartOutput(interlockType);
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_REPLAY_BUFFER) {
                if (obs_frontend_replay_buffer_active()) {
                    restartOutput(interlockType);
                    return;
                }
            } else if (interlockType == INTERLOCK_TYPE_INDIVIDUAL) {
                // Individual start: follow OBS frontend state per output type.
                // Check both the user toggle (dock checkbox) and the filter setting
                // (whether the output type is configured) to avoid blocking subsequent
                // outputs when an unconfigured type matches first.
                auto applied = appliedSettings.get();
                bool anyStarted = false;
                if (isAnyStreamingUserEnabled(applied) && obs_frontend_streaming_active() &&
                    isStreamingGroupEnabled(applied)) {
                    anyStarted |= startStreamingIndividual(applied);
                }
                if (isRecordingUserEnabled() && obs_frontend_recording_active() && isRecordingEnabled(applied)) {
                    anyStarted |= startRecordingIndividual(applied);
                }
                if (isReplayBufferUserEnabled() && obs_frontend_replay_buffer_active() &&
                    isReplayBufferEnabled(applied)) {
                    anyStarted |= startReplayBufferIndividual(applied);
                }
                if (anyStarted) {
                    return;
                }
            } else {
                restartOutput(interlockType);
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

            // One snapshot per tick: every read below, the restart check, the individual start
            // helpers and startOutput() use the same copy.
            auto applied = appliedSettings.get();
            obs_data_t *settings = applied;

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
                        if (startSingleStreamingIndividual(settings, i)) {
                            anyStarted = true;
                        }
                    }
                }
                return anyStarted;
            };

            // Decide the restart before any start below: an individual start reuses the running
            // infrastructure, which leaves activeSettings at the snapshot it was built from.
            bool settingsChanged;
            pthread_mutex_lock(&outputMutex);
            {
                OBSMutexAutoUnlock outputLocked(&outputMutex);
                settingsChanged = activeSettings.Get() != applied.Get();
            }

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

                // Individual start for additional outputs while some are already active.
                if (!settingsChanged) {
                    bool anyStarted = false;
                    if (obs_frontend_streaming_active()) {
                        anyStarted |= startEligibleStreamings();
                    }
                    if (isRecordingUserEnabled() && obs_frontend_recording_active() && !recordingActive &&
                        !recordingPending && isRecordingEnabled(settings)) {
                        anyStarted |= startRecordingIndividual(settings);
                    }
                    if (isReplayBufferUserEnabled() && obs_frontend_replay_buffer_active() && !replayBufferActive &&
                        isReplayBufferEnabled(settings)) {
                        anyStarted |= startReplayBufferIndividual(settings);
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

            if (settingsChanged) {
                // Settings has been changed
                obs_log(LOG_INFO, "%s: Settings change detected, Attempting restart", qUtf8Printable(name));
                // FIXME: someStreamingsStarting() only gates a slot's initial start ("starting" ->
                // "activate"). libobs' reconnect_thread() re-enters obs_output_actual_start() without
                // "starting", so this restart reaches stopStreamingOutput() -> obs_output_stop() on a
                // reconnecting output. Gate on obs_output_reconnecting() or route the stop through
                // stopAllStreamingOutputsGracefully().
                restartOutput(interlockType);
                return;
            }

            if (interlockType != INTERLOCK_TYPE_INDIVIDUAL) {
                // Per-output toggle re-enable check. The interlock checks above have returned unless
                // the interlock condition holds, so no recheck is needed here.
                bool anyStarted = false;
                anyStarted |= startEligibleStreamings();
                if (isRecordingUserEnabled() && !recordingActive && !recordingPending && isRecordingEnabled(settings)) {
                    anyStarted |= startRecordingIndividual(settings);
                }
                if (isReplayBufferUserEnabled() && !replayBufferActive && isReplayBufferEnabled(settings)) {
                    anyStarted |= startReplayBufferIndividual(settings);
                }
                if (anyStarted) {
                    return;
                }
            }

            if (streamingAlive || recordingAlive || recordingPending || replayBufferActive) {
                // Monitoring source
                // Resolve input resolution based on video source type
                uint32_t sourceWidth;
                uint32_t sourceHeight;
                getSourceResolution(sourceWidth, sourceHeight);

                if (!isInputAvailable()) {
                    // Stop output when source had been removed
                    stopOutputGracefully();
                    return;
                }

                bool inputHidden = evaluateBlanking(settings);

                // When blanking because the source is not visible, some sources report unstable base sizes.
                // Avoid restart storms while hidden; resolution will be re-evaluated when visible again.
                bool skipResolutionRestart = inputHidden;

                if (!skipResolutionRestart && (width != sourceWidth || height != sourceHeight)) {
                    // Source resolution was changed
                    bool sourceCollapsed = (sourceWidth == 0 || sourceHeight == 0);
                    bool cropCollapse = !calculateCrop(sourceWidth, sourceHeight, settings);

                    if (!sourceCollapsed && !cropCollapse) {
                        if (!obs_data_get_bool(settings, "keep_output_base_resolution")) {
                            // Restart output when source resolution was changed.
                            obs_log(LOG_INFO, "%s: Attempting restart the streaming output", qUtf8Printable(name));
                            startOutput(settings, interlockType);
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
                        pthread_mutex_lock(&outputMutex);
                        {
                            OBSMutexAutoUnlock outputLocked(&outputMutex);
                            createAndStartRecordingOutput(settings);
                        }
                        return;
                    }
                }
            }

            OBSOutputAutoRelease splitOutputRef;
            QString splitFormatOverride;

            pthread_mutex_lock(&outputMutex);
            {
                OBSMutexAutoUnlock outputLocked(&outputMutex);

                if (recordingSettingsOverridden) {
                    if (recordingActive && recordingAlive && recordingOutput && obs_output_paused(recordingOutput)) {
                        // Recording is paused and alive: keep flag, apply it on unpause
                    } else if (recordingActive && recordingAlive && !hasRecordingWrittenSinceStart()) {
                        // Apply once written: a stop or split before the first packet leaves an empty file
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
                            } else if (canSplitRecording()) {
                                obs_log(
                                    LOG_INFO, "%s: Splitting recording for filename format change", qUtf8Printable(name)
                                );
                                // Keep raised until applied below so that a concurrent override
                                // proc defers to the next tick.
                                recordingSettingsOverridden = true;
                                splitFormatOverride = recordingFilenameFormatOverride;
                                splitOutputRef = obs_output_get_ref(recordingOutput);
                            } else {
                                obs_log(
                                    LOG_INFO, "%s: Restarting recording for filename format change",
                                    qUtf8Printable(name)
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
            }

            if (splitOutputRef) {
                updateRecordingFormatAndSplit(splitOutputRef, splitFormatOverride);

                pthread_mutex_lock(&outputMutex);
                {
                    OBSMutexAutoUnlock outputLocked(&outputMutex);

                    if (recordingOutput.Get() == splitOutputRef.Get() &&
                        recordingFilenameFormatOverride == splitFormatOverride) {
                        recordingSettingsOverridden = false;
                    }
                }
            }

            // Guard per-slot streamings[i].output access against concurrent nulling in
            // stopStreamingOutput() / releaseInfrastructureIfIdle(). Lock order:
            // pluginMutex -> outputMutex (matches stopOutputGracefully() and the
            // Individual stop functions). Inner calls re-enter these recursive mutexes.
            pthread_mutex_lock(&pluginMutex);
            {
                OBSMutexAutoUnlock pluginLocked(&pluginMutex);

                pthread_mutex_lock(&outputMutex);
                {
                    OBSMutexAutoUnlock outputLocked(&outputMutex);

                    for (size_t i = 0; i < MAX_SERVICES; i++) {
                        if (!streamings[i].active || !streamings[i].output) {
                            continue;
                        }
                        if (!obs_output_active(streamings[i].output) &&
                            !obs_output_reconnecting(streamings[i].output)) {
                            // Restart streaming
                            obs_log(
                                LOG_INFO, "%s (%zu): Attempting reactivate the streaming output", qUtf8Printable(name),
                                i
                            );
                            reconnectStreamingOutput(i);
                        } else if (obs_output_reconnecting(streamings[i].output) && reconnectStallDetected(i)) {
                            // OBS internal reconnect is stalled (TCP connect or RTMP handshake hung
                            // with no progress). obs_output_stop() must not be called directly while
                            // reconnecting (crashes OBS), so route recovery through the crash-safe
                            // graceful stop path. This only stops the slot; restart is performed by
                            // a later tick's startEligibleStreamings(). Limit to one slot per tick
                            // to avoid rapid state transitions.
                            obs_log(
                                LOG_WARNING, "%s (%zu): Reconnect stalled, forcing graceful restart",
                                qUtf8Printable(name), i
                            );
                            // Latch "stopping" first so the graceful stop path evaluates its
                            // reconnect-timeout gate in this tick instead of only latching. A
                            // detected stall implies reconnectAttemptingTimedOut(), so the gate is
                            // open.
                            streamings[i].stopping = true;
                            // FIXME: obs_output_stop() on a reconnecting output reaches the output
                            // implementation's stop callback, which joins the in-flight connect
                            // thread (rtmp_stream_stop() -> pthread_join(connect_thread)) with no
                            // bound other than the OS connect / handshake timeout, blocking this
                            // timer thread while pluginMutex and outputMutex are held. libobs' own
                            // reconnect-thread join is not the blocker; reconnect_stop_event
                            // releases it. Root-cause fix (bounded / non-joining stop) needs a
                            // separate PR.
                            stopSingleStreamingIndividual(i);
                            if (streamings[i].active) {
                                // The gate stayed closed: reconnectAttemptingAt changed after
                                // detection, because the attempt moved on to the next retry or the
                                // reconnect is succeeding. Nothing retries this stop, so do not
                                // leave the latch behind.
                                streamings[i].stopping = false;
                                obs_log(
                                    LOG_DEBUG, "%s (%zu): Reconnect moved on, stall stop canceled",
                                    qUtf8Printable(name), i
                                );
                            }
                            return;
                        }
                    }
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

void BranchOutput::stopOutputGracefully()
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

std::optional<CropRect> BranchOutput::calculateCrop(uint32_t srcWidth, uint32_t srcHeight, obs_data_t *settings)
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

void BranchOutput::determineOutputResolution(obs_data_t *settings, obs_video_info *ovi, const CropRect &crop)
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

QString BranchOutput::applyFilenameFormatArgs(const QString &format, bool noSpace)
{
    QString sourceName = getInputName();
    QString filterName = qUtf8Printable(name);
    auto re = noSpace ? QRegularExpression("[\\s/\\\\.:;*?\"<>|&$,]") : QRegularExpression("[/\\\\.:;*?\"<>|&$,]");
    return QString(format).arg(sourceName.replace(re, "-")).arg(filterName.replace(re, "-"));
}

QString BranchOutput::resolveFilenameFormat(
    const QString &formatOverride, obs_data_t *settings, const char *formatKey, bool noSpace
)
{
    QString format = formatOverride;
    if (format.isEmpty()) {
        format = obs_data_get_string(settings, formatKey);
        if (format.isEmpty()) {
            format = config_get_string(obs_frontend_get_profile_config(), "Output", "FilenameFormatting");
        }
    }

    // Sanitize filename
#ifdef __APPLE__
    format.replace(QRegularExpression("[:]"), "");
#elif defined(_WIN32)
    format.replace(QRegularExpression("[<>:\"\\|\\?\\*]"), "");
#else
    // TODO: Add filtering for other platforms
#endif

    return applyFilenameFormatArgs(format, noSpace);
}
