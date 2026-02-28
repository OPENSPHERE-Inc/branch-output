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

#include <QMutexLocker>

#include "audio-capture.hpp"
#include "../plugin-support.h"
#include "../utils.hpp"

#define MAX_AUDIO_BUFFER_FRAMES 131071

//--- AudioCapture class ---//

AudioCapture::AudioCapture(
    const char *_name, uint32_t _samplesPerSec, speaker_layout _speakers, audio_input_callback_t _captureCallback,
    QObject *parent
)
    : QObject(parent),
      name(_name),
      samplesPerSec(_samplesPerSec),
      speakers(_speakers),
      audio(nullptr),
      audioBuffer({0}),
      audioBufferFrames(0),
      audioConvBuffer(nullptr),
      audioConvBufferSize(0),
      active(true)
{
    audio_output_info aoi = {0};
    aoi.name = _name;
    aoi.samples_per_sec = _samplesPerSec;
    aoi.speakers = _speakers;
    aoi.format = AUDIO_FORMAT_FLOAT_PLANAR;
    aoi.input_param = this;
    aoi.input_callback = _captureCallback;

    if (audio_output_open(&audio, &aoi) < 0) {
        audio = nullptr;
        return;
    }
}

AudioCapture::~AudioCapture()
{
    active.store(false);

    if (audio) {
        audio_output_close(audio);
    }

    deque_free(&audioBuffer);
    bfree(audioConvBuffer);
}

uint64_t AudioCapture::popAudio(uint64_t startTsIn, uint32_t mixers, audio_output_data *audioData)
{
    if (!active.load()) {
        return startTsIn;
    }

    QMutexLocker locker(&audioBufferMutex);
    {
        if (audioBufferFrames < AUDIO_OUTPUT_FRAMES) {
            // Wait until enough frames are receved.
            // DO NOT stall audio output pipeline
            return startTsIn;
        }

        size_t maxFrames = AUDIO_OUTPUT_FRAMES;

        while (maxFrames > 0 && audioBufferFrames) {
            // Peek header of first chunk
            deque_peek_front(&audioBuffer, audioConvBuffer, sizeof(AudioBufferHeader));
            auto header = (AudioBufferHeader *)audioConvBuffer;
            auto dataSize = sizeof(AudioBufferHeader) + header->speakers * header->frames * 4;

            // Read chunk data
            deque_peek_front(&audioBuffer, audioConvBuffer, dataSize);

            auto chunkFrames = header->frames - header->offset;
            auto frames = (chunkFrames > maxFrames) ? maxFrames : chunkFrames;
            auto outOffset = AUDIO_OUTPUT_FRAMES - maxFrames;

            for (auto tr = 0; tr < MAX_AUDIO_MIXES; tr++) {
                if ((mixers & (1 << tr)) == 0) {
                    continue;
                }
                for (auto ch = 0; ch < header->speakers; ch++) {
                    auto out = audioData[tr].data[ch] + outOffset;
                    if (!header->data_idx[ch]) {
                        continue;
                    }
                    auto in = (float *)(audioConvBuffer + header->data_idx[ch]) + header->offset;

                    for (size_t i = 0; i < frames; i++) {
                        *out += *(in++);
                        if (*out > 1.0f) {
                            *out = 1.0f;
                        } else if (*out < -1.0f) {
                            *out = -1.0f;
                        }
                        out++;
                    }
                }
            }

            if (frames == chunkFrames) {
                // Remove fulfilled chunk from buffer
                deque_pop_front(&audioBuffer, NULL, dataSize);
            } else {
                // Chunk frames are remaining -> modify chunk header
                header->offset += frames;
                deque_place(&audioBuffer, 0, header, sizeof(AudioBufferHeader));
            }

            maxFrames -= frames;

            // Decrement buffer usage
            audioBufferFrames -= frames;
        }
    }
    locker.unlock();

    return startTsIn;
}

void AudioCapture::pushAudio(const audio_data *audioData)
{
    if (!active.load()) {
        return;
    }

    QMutexLocker locker(&audioBufferMutex);
    {
        // Push audio data to buffer
        if (audioBufferFrames + audioData->frames > MAX_AUDIO_BUFFER_FRAMES) {
            obs_log(LOG_WARNING, "%s: The audio buffer is full", qUtf8Printable(name));
            deque_free(&audioBuffer);
            deque_init(&audioBuffer);
            audioBufferFrames = 0;
        }

        // Compute header
        AudioBufferHeader header = {0};
        header.frames = audioData->frames;
        header.timestamp = audioData->timestamp;
        header.samples_per_sec = samplesPerSec;
        header.speakers = speakers;
        header.format = AUDIO_FORMAT_FLOAT_PLANAR;

        for (auto i = 0, channels = 0; i < header.speakers; i++) {
            if (!audioData->data[i]) {
                continue;
            }
            header.data_idx[i] = sizeof(AudioBufferHeader) + channels * audioData->frames * 4;
            channels++;
        }

        // Push audio data to buffer
        deque_push_back(&audioBuffer, &header, sizeof(AudioBufferHeader));
        for (auto i = 0; i < header.speakers; i++) {
            if (!audioData->data[i]) {
                continue;
            }
            deque_push_back(&audioBuffer, audioData->data[i], audioData->frames * 4);
        }

        auto dataSize = sizeof(AudioBufferHeader) + header.speakers * header.frames * 4;
        if (dataSize > audioConvBufferSize) {
            obs_log(
                LOG_DEBUG, "%s: Expand audioConvBuffer from %zu to %zu bytes", qUtf8Printable(name),
                audioConvBufferSize, dataSize
            );
            audioConvBuffer = (uint8_t *)brealloc(audioConvBuffer, dataSize);
            audioConvBufferSize = dataSize;
        }

        audioBufferFrames += audioData->frames;
    }
    locker.unlock();
}

// Convert obs_audio_data to audio_data
void AudioCapture::pushAudio(const obs_audio_data *audioData)
{
    audio_data ad = {0};
    ad.frames = audioData->frames;
    ad.timestamp = audioData->timestamp;
    memcpy(ad.data, audioData->data, sizeof(audio_data::data));

    pushAudio(&ad);
}

void AudioCapture::setActive(bool enable)
{
    active.store(enable);
    if (!enable) {
        QMutexLocker locker(&audioBufferMutex);
        {
            deque_free(&audioBuffer);
            deque_init(&audioBuffer);
            audioBufferFrames = 0;
        }
        locker.unlock();
    }
}

// Callback from audio_output_open
bool AudioCapture::audioCapture(
    void *param, uint64_t start_ts_in, uint64_t, uint64_t *out_ts, uint32_t mixers, audio_output_data *mixes
)
{
    auto *audioSource = static_cast<AudioCapture *>(param);
    *out_ts = audioSource->popAudio(start_ts_in, mixers, mixes);
    return true;
}

// Callback from audio_output_open
bool AudioCapture::silenceCapture(void *, uint64_t startTsIn, uint64_t, uint64_t *outTs, uint32_t, audio_output_data *)
{
    *outTs = startTsIn;
    return true;
}

//--- SourceAudioCapture class ---//

SourceAudioCapture::SourceAudioCapture(
    obs_source_t *source, uint32_t _samplesPerSec, speaker_layout _speakers, QObject *parent
)
    : AudioCapture(obs_source_get_name(source), _samplesPerSec, _speakers, AudioCapture::audioCapture, parent),
      weakSource(obs_source_get_weak_source(source))
{
    obs_source_add_audio_capture_callback(source, sourceAudioCallback, this);
    obs_log(LOG_DEBUG, "%s: Source audio capture created.", obs_source_get_name(source));
}

SourceAudioCapture::~SourceAudioCapture()
{
    OBSSourceAutoRelease source = obs_weak_source_get_source(weakSource);
    if (source) {
        obs_source_remove_audio_capture_callback(source, sourceAudioCallback, this);
    }

    obs_log(LOG_DEBUG, "%s: Source audio capture destroyed.", obs_source_get_name(source));
}

// Callback from obs_source_add_audio_capture_callback
void SourceAudioCapture::sourceAudioCallback(void *param, obs_source_t *, const audio_data *audioData, bool muted)
{
    if (muted) {
        return;
    }

    auto sourceCapture = (SourceAudioCapture *)param;
    sourceCapture->pushAudio(audioData);
}

//--- MasterAudioCapture class ---//

MasterAudioCapture::MasterAudioCapture(
    size_t _mixIndex, uint32_t _samplesPerSec, speaker_layout _speakers, QObject *parent
)
    : AudioCapture(
          qUtf8Printable(QTStr("MasterTrack%1").arg(_mixIndex + 1)), _samplesPerSec, _speakers,
          AudioCapture::audioCapture, parent
      ),
      masterMixIndex(_mixIndex)
{
    obs_add_raw_audio_callback(masterMixIndex, nullptr, masterAudioCallback, this);
    obs_log(LOG_INFO, "%s: Master audio capture created (mix %zu)", qUtf8Printable(getName()), masterMixIndex);
}

MasterAudioCapture::~MasterAudioCapture()
{
    obs_remove_raw_audio_callback(masterMixIndex, masterAudioCallback, this);
    obs_log(LOG_DEBUG, "%s: Master audio capture destroyed.", qUtf8Printable(getName()));
}

// Callback from obs_add_raw_audio_callback
void MasterAudioCapture::masterAudioCallback(void *param, size_t, struct audio_data *audioData)
{
    auto *masterCapture = static_cast<MasterAudioCapture *>(param);
    masterCapture->pushAudio(audioData);
}
