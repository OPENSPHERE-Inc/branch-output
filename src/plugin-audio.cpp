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

#include "plugin-support.h"
#include "plugin-main.hpp"

#define MAX_AUDIO_BUFFER_FRAMES 131071

inline void pushAudioToBuffer(void *param, obs_audio_data *audioData)
{
    auto filter = (BranchOutputFilter *)param;

#ifdef NO_AUDIO
    return;
#endif

    auto outputAlive = filter->outputActive || filter->recordingActive;
    if (!outputAlive || filter->audioSourceType == AUDIO_SOURCE_TYPE_SILENCE || !audioData->frames) {
        return;
    }

    pthread_mutex_lock(&filter->audioBufferMutex);
    {
        OBSMutexAutoUnlock locked(&filter->audioBufferMutex);

        if (filter->audioBufferFrames + audioData->frames > MAX_AUDIO_BUFFER_FRAMES) {
            obs_log(LOG_WARNING, "%s: The audio buffer is full", obs_source_get_name(filter->filterSource));
            deque_free(&filter->audioBuffer);
            deque_init(&filter->audioBuffer);
            filter->audioBufferFrames = 0;
        }

        // Compute header
        AudioBufferChunkHeader header = {0};
        header.frames = audioData->frames;
        header.timestamp = audioData->timestamp;
        for (int ch = 0; ch < filter->audioChannels; ch++) {
            if (!audioData->data[ch]) {
                continue;
            }
            header.data_idx[ch] = sizeof(AudioBufferChunkHeader) + header.channels * audioData->frames * 4;
            header.channels++;
        }

        // Push audio data to buffer
        deque_push_back(&filter->audioBuffer, &header, sizeof(AudioBufferChunkHeader));
        for (int ch = 0; ch < filter->audioChannels; ch++) {
            if (!audioData->data[ch]) {
                continue;
            }
            deque_push_back(&filter->audioBuffer, audioData->data[ch], audioData->frames * 4);
        }

        // Ensure allocation of audioConvBuffer
        size_t data_size = sizeof(AudioBufferChunkHeader) + header.channels * audioData->frames * 4;
        if (data_size > filter->audioConvBufferSize) {
            obs_log(
                LOG_INFO, "%s: Expand audioConvBuffer from %zu to %zu bytes", obs_source_get_name(filter->filterSource),
                filter->audioConvBufferSize, data_size
            );
            filter->audioConvBuffer = (uint8_t *)brealloc(filter->audioConvBuffer, data_size);
            filter->audioConvBufferSize = data_size;
        }

        // Increment buffer usage
        filter->audioBufferFrames += audioData->frames;
    }
}

// Callback from filter audio
obs_audio_data *audioFilterCallback(void *param, obs_audio_data *audioData)
{
    auto filter = (BranchOutputFilter *)param;

    if (filter->audioSourceType != AUDIO_SOURCE_TYPE_FILTER) {
        // Omit filter's audio
        return audioData;
    }

    pushAudioToBuffer(filter, audioData);

    return audioData;
}

inline void convert_audio_data(obs_audio_data *dest, const audio_data *src)
{
    memcpy(dest->data, src->data, sizeof(audio_data::data));
    dest->frames = src->frames;
    dest->timestamp = src->timestamp;
}

// Callback from source's audio capture
void audioCaptureCallback(void *param, obs_source_t *, const audio_data *audioData, bool muted)
{
    auto filter = (BranchOutputFilter *)param;

    if (muted || !filter->audioSource) {
        return;
    }

    obs_audio_data filterAudioData = {0};
    convert_audio_data(&filterAudioData, audioData);
    pushAudioToBuffer(filter, &filterAudioData);
}

// Callback from master audio output
void masterAudioCallback(void *param, size_t, audio_data *audioData)
{
    auto filter = (BranchOutputFilter *)param;

    obs_audio_data filterAudioData = {0};
    convert_audio_data(&filterAudioData, audioData);
    pushAudioToBuffer(filter, &filterAudioData);
}

// Callback from audio output
bool audioInputCallback(
    void *param, uint64_t startTsIn, uint64_t, uint64_t *outTs, uint32_t mixers, audio_output_data *mixes
)
{
    auto filter = (BranchOutputFilter *)param;

    obs_audio_info audioInfo;
    auto outputAlive = filter->outputActive || filter->recordingActive;
    if (!outputAlive || filter->audioSourceType == AUDIO_SOURCE_TYPE_SILENCE ||
        !obs_get_audio_info(&audioInfo)) {
        // Silence
        *outTs = startTsIn;
        return true;
    }

    // TODO: Shorten the critical section to reduce audio delay
    pthread_mutex_lock(&filter->audioBufferMutex);
    {
        OBSMutexAutoUnlock locked(&filter->audioBufferMutex);

        if (filter->audioBufferFrames < AUDIO_OUTPUT_FRAMES) {
            // Wait until enough frames are receved.
            if (!filter->audioSkip) {
                obs_log(LOG_DEBUG, "%s: Wait for frames...", obs_source_get_name(filter->filterSource));
            }
            filter->audioSkip++;

            // DO NOT stall audio output pipeline
            *outTs = startTsIn;
            return true;
        } else {
            filter->audioSkip = 0;
        }

        size_t maxFrames = AUDIO_OUTPUT_FRAMES;

        while (maxFrames > 0 && filter->audioBufferFrames) {
            // Peek header of first chunk
            deque_peek_front(&filter->audioBuffer, filter->audioConvBuffer, sizeof(AudioBufferChunkHeader));
            auto header = (AudioBufferChunkHeader *)filter->audioConvBuffer;
            size_t dataSize = sizeof(AudioBufferChunkHeader) + header->channels * header->frames * 4;

            // Read chunk data
            deque_peek_front(&filter->audioBuffer, filter->audioConvBuffer, dataSize);

            size_t chunkFrames = header->frames - header->offset;
            size_t frames = (chunkFrames > maxFrames) ? maxFrames : chunkFrames;
            size_t outOffset = AUDIO_OUTPUT_FRAMES - maxFrames;

            for (size_t mixIdx = 0; mixIdx < MAX_AUDIO_MIXES; mixIdx++) {
                if ((mixers & (1 << mixIdx)) == 0) {
                    continue;
                }
                for (size_t ch = 0; ch < filter->audioChannels; ch++) {
                    auto out = mixes[mixIdx].data[ch] + outOffset;
                    if (!header->data_idx[ch]) {
                        continue;
                    }
                    auto in = (float *)(filter->audioConvBuffer + header->data_idx[ch]) + header->offset;

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
                deque_pop_front(&filter->audioBuffer, nullptr, dataSize);
            } else {
                // Chunk frames are remaining -> modify chunk header
                header->offset += frames;
                deque_place(&filter->audioBuffer, 0, header, sizeof(AudioBufferChunkHeader));
            }

            maxFrames -= frames;

            // Decrement buffer usage
            filter->audioBufferFrames -= frames;
        }
    }

    *outTs = startTsIn;
    return true;
}
