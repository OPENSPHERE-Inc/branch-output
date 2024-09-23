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
#include <util/deque.h>
#include <util/threading.h>

#include "UI/output-status-dock.hpp"

enum AudioSourceType {
    AUDIO_SOURCE_TYPE_SILENCE,
    AUDIO_SOURCE_TYPE_FILTER,
    AUDIO_SOURCE_TYPE_MASTER,
    AUDIO_SOURCE_TYPE_CAPTURE,
};

enum InterlockType {
    INTERLOCK_TYPE_ALWAYS_ON,
    INTERLOCK_TYPE_STREAMING,
    INTERLOCK_TYPE_RECORDING,
    INTERLOCK_TYPE_STREAMING_RECORDING,
    INTERLOCK_TYPE_VIRTUAL_CAM,
};

// TODO: Convert to Object
struct BranchOutputFilter {
    bool initialized; // Activate after first "Apply" click
    bool outputActive;
    bool recordingActive;
    uint32_t storedSettingsRev;
    uint32_t activeSettingsRev;
    QTimer *intervalTimer;

    // Filter source
    obs_source_t *filterSource;

    // User choosed encoder
    obs_encoder_t *videoEncoder;
    obs_encoder_t *audioEncoder;

    obs_view_t *view;
    video_t *videoOutput;
    audio_t *audioOutput;
    obs_output_t *streamOutput;
    obs_output_t *recordingOutput;
    obs_service_t *service;

    // Video context
    uint32_t width;
    uint32_t height;

    // Audio context
    obs_weak_source_t *audioSource; // NULL means using filter's audio
    AudioSourceType audioSourceType;
    deque audioBuffer;
    size_t audioBufferFrames;
    pthread_mutex_t audioBufferMutex;
    uint8_t *audioConvBuffer;
    size_t audioConvBufferSize;
    size_t audioMixIdx;
    speaker_layout audioChannels;
    uint32_t samplesPerSec;
    uint64_t audioSkip;

    // Stream context
    pthread_mutex_t outputMutex;
    uint64_t connectAttemptingAt;
};

struct AudioBufferChunkHeader {
    size_t data_idx[MAX_AUDIO_CHANNELS]; // Zero means unused channel
    uint32_t frames;
    uint64_t timestamp;
    size_t offset;
    size_t channels;
};

void update(void *data, obs_data_t *settings);
void getDefaults(obs_data_t *defaults);
obs_properties_t *getProperties(void *data);
void audioCaptureCallback(void *param, obs_source_t *, const audio_data *audio_data, bool muted);
void masterAudioCallback(void *param, size_t, audio_data *audio_data);
obs_audio_data *audioFilterCallback(void *param, obs_audio_data *audio_data);
bool audioInputCallback(
    void *param, uint64_t start_ts_in, uint64_t, uint64_t *out_ts, uint32_t mixers, audio_output_data *mixes
);
BranchOutputStatusDock *createOutputStatusDock();
