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

    QString name;
    bool initialized; // Activate after first "Apply" click
    bool outputActive;
    bool recordingActive;
    uint32_t storedSettingsRev;
    uint32_t activeSettingsRev;
    QTimer *intervalTimer;

    // Filter source (Do not use OBSSourceAutoRelease)
    obs_source_t *filterSource;

    // User choosed encoder
    OBSEncoderAutoRelease videoEncoder;

    OBSView view;
    video_t *videoOutput;
    OBSOutputAutoRelease streamOutput;
    OBSOutputAutoRelease recordingOutput;
    OBSServiceAutoRelease service;

    // Video context
    uint32_t width;
    uint32_t height;

    // Audio context
    BranchOutputAudioContext audios[MAX_AUDIO_MIXES];

    // Stream context
    pthread_mutex_t outputMutex;
    uint64_t connectAttemptingAt;

    // Hotkey context
    obs_hotkey_pair_id hotkeyPairId;
    OBSSignal filterRenamedSignal;

    void startOutput(obs_data_t *settings);
    void stopOutput();
    obs_data_t *createRecordingSettings(obs_data_t *settings);
    void determineOutputResolution(obs_data_t *settings, obs_video_info *ovi);
    void reconnectStreamOutput();
    void restartRecordingOutput();
    void loadRecently(obs_data_t *settings);
    void restartOutput();
    bool connectAttemptingTimedOut();
    void registerHotkey();

    // Implemented in plugin-ui.cpp
    void addApplyButton(obs_properties_t *props, const char* name = "apply");
    void addPluginInfo(obs_properties_t *props);
    void addStreamGroup(obs_properties_t *props);
    void createAudioTrackProperties(obs_properties_t *audioGroup, size_t track, bool visible = true);
    void addAudioGroup(obs_properties_t *props);
    void addAudioEncoderGroup(obs_properties_t *props);
    void addVideoEncoderGroup(obs_properties_t *props);

    // Callbacks from obs core
    static bool onEnableFilterHotkeyPressed(void *data, obs_hotkey_pair_id id, obs_hotkey *hotkey, bool pressed);
    static bool onDisableFilterHotkeyPressed(void *data, obs_hotkey_pair_id id, obs_hotkey *hotkey, bool pressed);

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

public:
    explicit BranchOutputFilter(obs_data_t *settings, obs_source_t *source, QObject *parent = nullptr);
    ~BranchOutputFilter();

    static obs_source_info createFilterInfo();

    // Implemented in plugin-ui.cpp
    static BranchOutputStatusDock *createOutputStatusDock();
};
