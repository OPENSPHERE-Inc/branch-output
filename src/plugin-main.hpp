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

#include <QObject>

#include "UI/output-status-dock.hpp"
#include "audio/audio-capture.hpp"
#include "video/filter-video-capture.hpp"

#define MAX_SERVICES 8

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
        uint64_t reconnectAttemptingAt;
        bool outputStarting;
        bool active;
        bool stopping;
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
    bool streamingStopping;
    bool blankingOutputActive;
    bool blankingAudioMuted;

    // Filter source (Do not use OBSSourceAutoRelease)
    obs_source_t *filterSource;

    // Private solid-color source used for blanking
    OBSSourceAutoRelease blankSource;

    // User choosed encoder
    OBSEncoderAutoRelease videoEncoder;

    // Video context
    OBSView view;
    video_t *videoOutput;
    uint32_t width;
    uint32_t height;

    // Filter input mode flag
    bool useFilterInput;

    // Filter input video capture (captures filter input and provides proxy source for obs_view)
    FilterVideoCapture *filterVideoCapture;

    // Audio context
    BranchOutputAudioContext audios[MAX_AUDIO_MIXES];

    // Recording context
    bool recordingActive;
    OBSOutputAutoRelease recordingOutput;
    bool recordingPending; // Pending due to collapsed source resolution
    bool splitRecordingEnabled;
    bool addChapterToRecordingEnabled;

    // Streaming context
    pthread_mutex_t outputMutex;
    BranchOutputStreamingContext streamings[MAX_SERVICES];

    // Hotkey context
    obs_hotkey_pair_id toggleEnableHotkeyPairId;
    obs_hotkey_id splitRecordingHotkeyId;
    obs_hotkey_pair_id togglePauseRecordingHotkeyPairId;
    obs_hotkey_id addChapterToRecordingHotkeyId;

    OBSSignal filterRenamedSignal;

    void startOutput(obs_data_t *settings);
    void stopOutput();
    obs_data_t *createRecordingSettings(obs_data_t *settings, bool createFolder = false);
    obs_data_t *createStreamingSettings(obs_data_t *settings, size_t index = 0);
    void getSourceResolution(uint32_t &outWidth, uint32_t &outHeight);
    void determineOutputResolution(obs_data_t *settings, obs_video_info *ovi);
    BranchOutputStreamingContext createSreamingOutput(obs_data_t *settings, size_t index = 0);
    void startStreamingOutput(size_t index = 0);
    void stopStreamingOutput(size_t index = 0);
    void createAndStartRecordingOutput(obs_data_t *settings);
    void stopRecordingOutput();
    void reconnectStreamingOutput(size_t index = 0);
    void restartRecordingOutput();
    void loadProfile(obs_data_t *settings);
    void loadRecently(obs_data_t *settings);
    void restartOutput();
    bool reconnectAttemptingTimedOut(size_t index = 0);
    bool someStreamingsStarting();
    int countEnabledStreamings(obs_data_t *settings);
    int countAliveStreamings();
    int countActiveStreamings();
    bool hasEnabledStreamings(obs_data_t *settings);
    bool isStreamingEnabled(obs_data_t *settings, size_t index = 0);
    bool isRecordingEnabled(obs_data_t *settings);
    bool isSplitRecordingEnabled(obs_data_t *settings);
    bool canPauseRecording();
    bool canAddChapterToRecording();
    bool canSplitRecording();
    void registerHotkey();
    bool splitRecording();
    bool pauseRecording();
    bool unpauseRecording();
    bool addChapterToRecording(QString chapterName = QString());
    void setBlankingActive(bool active, bool muteAudio, obs_source_t *parent);
    void setAudioCapturesActive(bool active);

    // Implemented in plugin-ui.cpp
    void addApplyButton(obs_properties_t *props, const char *propName = "apply");
    void addPluginInfo(obs_properties_t *props);
    void addStreamGroup(obs_properties_t *props);
    void addServices(obs_properties_t *props);
    void createServiceProperties(obs_properties_t *props, size_t index, bool visible = true);
    void createAudioTrackProperties(obs_properties_t *audioGroup, size_t track, bool visible = true);
    void addAudioGroup(obs_properties_t *props);
    void addAudioEncoderGroup(obs_properties_t *props);
    void addVideoEncoderGroup(obs_properties_t *props);

    // Callbacks from obs core
    static bool onEnableFilterHotkeyPressed(void *data, obs_hotkey_pair_id id, obs_hotkey *hotkey, bool pressed);
    static bool onDisableFilterHotkeyPressed(void *data, obs_hotkey_pair_id id, obs_hotkey *hotkey, bool pressed);
    static void onSplitRecordingFileHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);
    static bool onPauseRecordingHotkeyPressed(void *data, obs_hotkey_pair_id id, obs_hotkey *hotkey, bool pressed);
    static bool onUnpauseRecordingHotkeyPressed(void *data, obs_hotkey_pair_id id, obs_hotkey *hotkey, bool pressed);
    static void
    onAddChapterToRecordingFileHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);

    void addCallback(obs_source_t *source);
    void updateCallback(obs_data_t *settings);
    void videoRenderCallback(gs_effect_t *effect);
    void destroyCallback();
    obs_properties_t *getProperties();

    static obs_audio_data *audioFilterCallback(void *param, obs_audio_data *audioData);
    static void getDefaults(obs_data_t *settings);

private slots:
    void onIntervalTimerTimeout();
    void removeCallback();
    void onStopOutputGracefully();

public:
    explicit BranchOutputFilter(obs_data_t *settings, obs_source_t *source, QObject *parent = nullptr);
    ~BranchOutputFilter();

    static obs_source_info createFilterInfo();

    // Implemented in plugin-ui.cpp
    static BranchOutputStatusDock *createOutputStatusDock();
};
