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

#pragma once

//#define NO_AUDIO

#include <obs-module.h>
#include <obs.hpp>
#include <util/deque.h>
#include <util/threading.h>

#include <atomic>

#include <QObject>

#include "UI/output-status-dock.hpp"
#include "audio/audio-capture.hpp"
#include "video/filter-video-capture.hpp"
#include "video/crop-rect-preview-renderer.hpp"
#include "utils.hpp"

#define MAX_SERVICES 8

// Defined in plugin-main.cpp. Guards plugin-wide state shared across filter instances:
// - Serializes OBS global API calls (obs_view, obs_encoder, obs_output creation/destruction)
// - Protects BranchOutputFilter instance lists and cross-instance coordination
// - Prevents concurrent infrastructure setup/teardown across multiple filter instances
// Lock ordering: pluginMutex -> outputMutex -> audioMutex
// All three mutexes are recursive — safe to re-lock from the same thread.
extern pthread_mutex_t pluginMutex;

class BranchOutputFilter : public QObject {
    Q_OBJECT

    friend class BranchOutputStatusDock;
    friend class OutputTableRow;

    enum InterlockType {
        INTERLOCK_TYPE_ALWAYS_ON,
        INTERLOCK_TYPE_STREAMING,
        INTERLOCK_TYPE_RECORDING,
        INTERLOCK_TYPE_STREAMING_RECORDING,
        INTERLOCK_TYPE_VIRTUAL_CAM,
        INTERLOCK_TYPE_REPLAY_BUFFER,
        INTERLOCK_TYPE_INDIVIDUAL,
        INTERLOCK_TYPE_ALWAYS_OFF = 9999,
    };

    struct BranchOutputAudioContext {
        AudioCapture *capture;
        OBSEncoderAutoRelease encoder;
        audio_t *audio;
        size_t mixIndex;
        bool streaming;
        bool recording;
        QString name;
    };

    struct BranchOutputStreamingContext {
        OBSOutputAutoRelease output;
        OBSServiceAutoRelease service;
        std::atomic<uint64_t> reconnectAttemptingAt{0};
        std::atomic<bool> outputStarting{false};
        bool active = false;
        bool stopping = false;
        OBSSignal outputStartingSignal;
        OBSSignal outputActivateSignal;
        OBSSignal outputReconnectSignal;
        OBSSignal outputStopSignal;
    };

    QString name;
    bool initialized; // Activate after first "Apply" click
    uint32_t storedSettingsRev;
    uint32_t activeSettingsRev;
    QTimer *intervalTimer;
    bool outputGracefullyStopping;
    bool streamingIndividualStopping;
    bool blankingOutputActive;
    bool blankingAudioMuted;

    // Per-output user intent flags (persisted via save callback).
    // Atomic because setters are called from UI thread while saveCallback()
    // may be called from OBS core on a different thread.
    std::atomic<bool> streamingUserEnabled[MAX_SERVICES];
    std::atomic<bool> recordingUserEnabled;
    std::atomic<bool> replayBufferUserEnabled;

    // Filter source (Do not use OBSSourceAutoRelease)
    obs_source_t *filterSource;

    // User choosed encoder
    OBSEncoderAutoRelease videoEncoder;

    // Video context
    OBSView view;
    video_t *videoOutput;
    uint32_t width;
    uint32_t height;

    // Crop context
    OBSSceneAutoRelease cropScene; // Source output mode crop scene
    CropRectPreviewRenderer cropPreview;

    // Filter input mode flag
    bool useFilterInput;

    // Filter input video capture (captures filter input and provides proxy source for obs_view)
    FilterVideoCapture *filterVideoCapture;

    // Audio context
    // Lock ordering: always acquire in order pluginMutex -> outputMutex -> audioMutex.
    // Never acquire a higher-order lock while holding a lower-order one.
    pthread_mutex_t audioMutex; // Recursive mutex — protects audios[] capture pointers against audioFilterCallback
    BranchOutputAudioContext audios[MAX_AUDIO_MIXES];

    // Recording context
    bool recordingActive;
    OBSOutputAutoRelease recordingOutput;
    bool recordingPending; // Pending due to collapsed source resolution
    bool splitRecordingEnabled;
    bool addChapterToRecordingEnabled;
    QString recordingFilenameFormatOverride;
    bool recordingSettingsOverridden;

    // Replay buffer context
    bool replayBufferActive;
    OBSOutputAutoRelease replayBufferOutput;
    OBSSignal replayBufferSavedSignal;
    QString replayBufferFilenameFormatOverride;

    // Streaming context
    pthread_mutex_t outputMutex; // Recursive mutex — safe to re-lock from same thread
    BranchOutputStreamingContext streamings[MAX_SERVICES];

    // Hotkey context
    obs_hotkey_pair_id toggleEnableHotkeyPairId;
    obs_hotkey_id splitRecordingHotkeyId;
    obs_hotkey_pair_id togglePauseRecordingHotkeyPairId;
    obs_hotkey_id addChapterToRecordingHotkeyId;
    obs_hotkey_id saveReplayBufferHotkeyId;

    OBSSignal filterRenamedSignal;

    void startOutput(obs_data_t *settings);
    void stopOutput();
    bool ensureInfrastructure(obs_data_t *settings);
    void releaseInfrastructureIfIdle();

    // Internal helpers (caller must hold outputMutex, and must call ensureInfrastructure() first)
    // Returns true if any output was actually started.
    bool createAndStartStreamingOutputs(obs_data_t *settings);
    bool createAndStartRecordingOutputChecked(obs_data_t *settings);
    bool createAndStartReplayBufferChecked(obs_data_t *settings);
    bool stopAllStreamingOutputsGracefully();

    bool startStreamingIndividual();
    bool stopStreamingIndividual();
    bool startSingleStreamingIndividual(size_t index);
    bool stopSingleStreamingIndividual(size_t index);
    bool startRecordingIndividual();
    bool stopRecordingIndividual();
    bool startReplayBufferIndividual();
    bool stopReplayBufferIndividual();
    void getSourceResolution(uint32_t &outWidth, uint32_t &outHeight);
    void determineOutputResolution(obs_data_t *settings, obs_video_info *ovi, const CropRect &crop);
    void loadProfile(obs_data_t *settings);
    void loadRecently(obs_data_t *settings);
    void restartOutput();
    void stopOutputGracefully();
    void registerHotkey();
    void setBlankingActive(bool active, bool muteAudio, obs_source_t *parent);
    void setAudioCapturesActive(bool active);
    void saveCallback(obs_data_t *settings);

    // Per-output user intent setters/getters (thread-safe via std::atomic)
    // Streaming variants are implemented in plugin-streaming.cpp
    void setStreamingUserEnabled(size_t index, bool enabled);
    bool isStreamingUserEnabled(size_t index) const;
    bool isAnyStreamingUserEnabled() const;
    bool isAnyStreamingUserEnabled(obs_data_t *settings);
    void setRecordingUserEnabled(bool enabled) { recordingUserEnabled.store(enabled, std::memory_order_relaxed); }
    void setReplayBufferUserEnabled(bool enabled) { replayBufferUserEnabled.store(enabled, std::memory_order_relaxed); }
    bool isRecordingUserEnabled() const { return recordingUserEnabled.load(std::memory_order_relaxed); }
    bool isReplayBufferUserEnabled() const { return replayBufferUserEnabled.load(std::memory_order_relaxed); }
    std::optional<CropRect> calculateCrop(uint32_t srcWidth, uint32_t srcHeight, obs_data_t *settings);
    QString applyFilenameFormatArgs(const QString &format, bool noSpace);

    // Implemented in plugin-streaming.cpp
    obs_data_t *createStreamingSettings(obs_data_t *settings, size_t index = 0);
    bool createStreamingOutput(obs_data_t *settings, size_t index = 0);
    void startStreamingOutput(size_t index = 0);
    void stopStreamingOutput(size_t index = 0);
    void reconnectStreamingOutput(size_t index = 0);
    bool reconnectAttemptingTimedOut(size_t index = 0);
    bool someStreamingsStarting();
    int countEnabledStreamings(obs_data_t *settings);
    int countAliveStreamings();
    int countActiveStreamings();
    bool hasEnabledStreamings(obs_data_t *settings);
    bool isStreamingGroupEnabled(obs_data_t *settings);
    bool isStreamingEnabled(obs_data_t *settings, size_t index = 0);
    bool stopSingleStreamingOutputGracefully(size_t index);

    // Implemented in plugin-stream-recording.cpp
    obs_data_t *createRecordingSettings(obs_data_t *settings, bool createFolder = false);
    void createAndStartRecordingOutput(obs_data_t *settings);
    void stopRecordingOutput(bool pending = false);
    void restartRecordingOutput();
    bool isRecordingEnabled(obs_data_t *settings);
    bool isSplitRecordingEnabled(obs_data_t *settings);
    bool canPauseRecording();
    bool canAddChapterToRecording();
    bool canSplitRecording();
    bool splitRecording();
    bool pauseRecording();
    bool unpauseRecording();
    bool addChapterToRecording(QString chapterName = QString());

    // Implemented in plugin-replay-buffer.cpp
    void createAndStartReplayBuffer(obs_data_t *settings);
    void stopReplayBufferOutput();
    obs_data_t *createReplayBufferSettings(obs_data_t *settings);
    bool isReplayBufferEnabled(obs_data_t *settings);
    bool saveReplayBuffer();

    // Implemented in plugin-ui.cpp
    void addApplyButton(obs_properties_t *props, const char *propName = "apply");
    void addPluginInfo(obs_properties_t *props);
    void addStreamingGroup(obs_properties_t *props);
    void addRecordingGroup(obs_properties_t *props);
    void addServices(obs_properties_t *props);
    void createServiceProperties(obs_properties_t *props, size_t index, bool visible = true);
    void createAudioTrackProperties(obs_properties_t *audioGroup, size_t track, bool visible = true);
    void addAudioGroup(obs_properties_t *props);
    void addAudioEncoderGroup(obs_properties_t *props);
    void addVideoEncoderGroup(obs_properties_t *props);
    void addAdvancedSettingsGroup(obs_properties_t *props);
    void addReplayBufferGroup(obs_properties_t *props);

    // Callbacks from obs core
    static bool onEnableFilterHotkeyPressed(void *data, obs_hotkey_pair_id id, obs_hotkey *hotkey, bool pressed);
    static bool onDisableFilterHotkeyPressed(void *data, obs_hotkey_pair_id id, obs_hotkey *hotkey, bool pressed);
    static void onSplitRecordingFileHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);
    static bool onPauseRecordingHotkeyPressed(void *data, obs_hotkey_pair_id id, obs_hotkey *hotkey, bool pressed);
    static bool onUnpauseRecordingHotkeyPressed(void *data, obs_hotkey_pair_id id, obs_hotkey *hotkey, bool pressed);
    static void
    onAddChapterToRecordingFileHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);
    static void onSaveReplayBufferHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);
    static void onReplayBufferSaved(void *data, calldata_t *cd);
    static void onOverrideReplayBufferFilenameFormat(void *data, calldata_t *cd);
    static void onOverrideRecordingFilenameFormat(void *data, calldata_t *cd);

    void addCallback(obs_source_t *source);
    void updateCallback(obs_data_t *settings);
    void videoTickCallback(float seconds);
    void videoRenderCallback(gs_effect_t *effect);
    void destroyCallback();
    obs_properties_t *getProperties();

    static obs_audio_data *audioFilterCallback(void *param, obs_audio_data *audioData);
    static void getDefaults(obs_data_t *settings);

private slots:
    void onIntervalTimerTimeout();
    void removeCallback();

public:
    explicit BranchOutputFilter(obs_data_t *settings, obs_source_t *source, QObject *parent = nullptr);
    ~BranchOutputFilter();

    static obs_source_info createFilterInfo();

    // Implemented in plugin-ui.cpp
    static BranchOutputStatusDock *createOutputStatusDock();
};
