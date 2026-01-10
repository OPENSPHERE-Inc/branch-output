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

#include <obs-module.h>
#include <obs.hpp>
#include <util/deque.h>

#include <atomic>
#include <QObject>
#include <QMutex>

// Base audio capture (default silence)
class AudioCapture : public QObject {
    Q_OBJECT

    audio_t *audio;

protected:
    QString name;
    uint32_t samplesPerSec;
    speaker_layout speakers;

    deque audioBuffer;
    size_t audioBufferFrames;
    uint8_t *audioConvBuffer;
    size_t audioConvBufferSize;
    QMutex audioBufferMutex;
    std::atomic_bool active;

public:
    struct AudioBufferHeader {
        size_t data_idx[MAX_AV_PLANES]; // Zero means unused channel
        uint32_t frames;
        speaker_layout speakers;
        audio_format format;
        uint32_t samples_per_sec;
        uint64_t timestamp;
        size_t offset;
    };

    explicit AudioCapture(
        const char *_name, uint32_t _samplesPerSec, speaker_layout _speakers,
        audio_input_callback_t _captureCallback = silenceCapture, QObject *parent = nullptr
    );
    ~AudioCapture();

    inline audio_t *getAudio() { return audio; }
    uint64_t popAudio(uint64_t startTsIn, uint32_t mixers, audio_output_data *audioData);
    void pushAudio(const audio_data *audioData);
    void pushAudio(const obs_audio_data *audioData);
    void setActive(bool enable);
    inline bool isActive() const { return active.load(); }
    virtual bool hasSource() { return true; }
    inline QString getName() { return name; }

    static bool audioCapture(
        void *param, uint64_t startTsIn, uint64_t, uint64_t *outTs, uint32_t mixers, audio_output_data *audioData
    );
    static bool silenceCapture(
        void *param, uint64_t startTsIn, uint64_t, uint64_t *outTs, uint32_t mixers, audio_output_data *audioData
    );
};

// Audio capture from source
class SourceAudioCapture : public AudioCapture {
    Q_OBJECT

    OBSWeakSourceAutoRelease weakSource;

    static void sourceAudioCallback(void *param, obs_source_t *, const audio_data *audioData, bool muted);

public:
    explicit SourceAudioCapture(
        obs_source_t *source, uint32_t _samplesPerSec, speaker_layout _speakers, QObject *parent = nullptr
    );
    ~SourceAudioCapture();
};

// Audio capture from filter (Audio will be pushed externally)
class FilterAudioCapture : public AudioCapture {
    Q_OBJECT

public:
    explicit FilterAudioCapture(
        const char *_name, uint32_t _samplesPerSec, speaker_layout _speakers, QObject *parent = nullptr
    )
        : AudioCapture(_name, _samplesPerSec, _speakers, audioCapture, parent)
    {
    }
    ~FilterAudioCapture() {}

    bool hasSource() { return false; }
};
