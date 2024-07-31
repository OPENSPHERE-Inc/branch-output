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
#include "plugin-main.hpp"
#include "plugin-support.h"

inline void push_audio_to_buffer(void *param, obs_audio_data *audio_data)
{
    auto filter = (filter_t *)param;

#ifdef NO_AUDIO
    return;
#endif

    if (!filter->output_active || filter->audio_source_type == AUDIO_SOURCE_TYPE_SILENCE || !audio_data->frames) {
        return;
    }

    pthread_mutex_lock(&filter->audio_buffer_mutex);
    {
        if (filter->audio_buffer_frames + audio_data->frames > MAX_AUDIO_BUFFER_FRAMES) {
            obs_log(LOG_WARNING, "%s: The audio buffer is full", obs_source_get_name(filter->source));
            deque_free(&filter->audio_buffer);
            deque_init(&filter->audio_buffer);
            filter->audio_buffer_frames = 0;
        }

        // Compute header
        audio_buffer_chunk_header_t header = {0};
        header.frames = audio_data->frames;
        header.timestamp = audio_data->timestamp;
        for (int ch = 0; ch < filter->audio_channels; ch++) {
            if (!audio_data->data[ch]) {
                continue;
            }
            header.data_idx[ch] = sizeof(audio_buffer_chunk_header_t) + header.channels * audio_data->frames * 4;
            header.channels++;
        }

        // Push audio data to buffer
        deque_push_back(&filter->audio_buffer, &header, sizeof(audio_buffer_chunk_header_t));
        for (int ch = 0; ch < filter->audio_channels; ch++) {
            if (!audio_data->data[ch]) {
                continue;
            }
            deque_push_back(&filter->audio_buffer, audio_data->data[ch], audio_data->frames * 4);
        }

        // Ensure allocation of audio_conv_buffer
        size_t data_size = sizeof(audio_buffer_chunk_header_t) + header.channels * audio_data->frames * 4;
        if (data_size > filter->audio_conv_buffer_size) {
            obs_log(
                LOG_INFO, "%s: Expand audio_conv_buffer from %zu to %zu bytes", obs_source_get_name(filter->source),
                filter->audio_conv_buffer_size, data_size
            );
            filter->audio_conv_buffer = (uint8_t *)brealloc(filter->audio_conv_buffer, data_size);
            filter->audio_conv_buffer_size = data_size;
        }

        // Increment buffer usage
        filter->audio_buffer_frames += audio_data->frames;
    }
    pthread_mutex_unlock(&filter->audio_buffer_mutex);
}

// Callback from filter audio
obs_audio_data *audio_filter_callback(void *param, obs_audio_data *audio_data)
{
    auto filter = (filter_t *)param;

    if (filter->audio_source_type != AUDIO_SOURCE_TYPE_FILTER) {
        // Omit filter's audio
        return audio_data;
    }

    push_audio_to_buffer(filter, audio_data);

    return audio_data;
}

inline void convert_audio_data(obs_audio_data *dest, const audio_data *src)
{
    memcpy(dest->data, src->data, sizeof(audio_data::data));
    dest->frames = src->frames;
    dest->timestamp = src->timestamp;
}

// Callback from source's audio capture
void audio_capture_callback(void *param, obs_source_t *, const audio_data *audio_data, bool muted)
{
    auto filter = (filter_t *)param;

    if (muted || !filter->audio_source) {
        return;
    }

    obs_audio_data filter_audio_data = {0};
    convert_audio_data(&filter_audio_data, audio_data);
    push_audio_to_buffer(filter, &filter_audio_data);
}

// Callback from master audio output
void master_audio_callback(void *param, size_t, audio_data *audio_data)
{
    auto filter = (filter_t *)param;

    obs_audio_data filter_audio_data = {0};
    convert_audio_data(&filter_audio_data, audio_data);
    push_audio_to_buffer(filter, &filter_audio_data);
}

// Callback from audio output
bool audio_input_callback(
    void *param, uint64_t start_ts_in, uint64_t, uint64_t *out_ts, uint32_t mixers, audio_output_data *mixes
)
{
    auto filter = (filter_t *)param;

    obs_audio_info audio_info;
    if (!filter->output_active || filter->audio_source_type == AUDIO_SOURCE_TYPE_SILENCE ||
        !obs_get_audio_info(&audio_info)) {
        // Silence
        *out_ts = start_ts_in;
        return true;
    }

    // TODO: Shorten the critical section to reduce audio delay
    pthread_mutex_lock(&filter->audio_buffer_mutex);
    {
        if (filter->audio_buffer_frames < AUDIO_OUTPUT_FRAMES) {
            // Wait until enough frames are receved.
            if (!filter->audio_skip) {
                obs_log(LOG_DEBUG, "%s: Wait for frames...", obs_source_get_name(filter->source));
            }
            filter->audio_skip++;
            pthread_mutex_unlock(&filter->audio_buffer_mutex);

            // DO NOT stall audio output pipeline
            *out_ts = start_ts_in;
            return true;
        } else {
            filter->audio_skip = 0;
        }

        size_t max_frames = AUDIO_OUTPUT_FRAMES;

        while (max_frames > 0 && filter->audio_buffer_frames) {
            // Peek header of first chunk
            deque_peek_front(&filter->audio_buffer, filter->audio_conv_buffer, sizeof(audio_buffer_chunk_header_t));
            auto header = (audio_buffer_chunk_header_t *)filter->audio_conv_buffer;
            size_t data_size = sizeof(audio_buffer_chunk_header_t) + header->channels * header->frames * 4;

            // Read chunk data
            deque_peek_front(&filter->audio_buffer, filter->audio_conv_buffer, data_size);

            size_t chunk_frames = header->frames - header->offset;
            size_t frames = (chunk_frames > max_frames) ? max_frames : chunk_frames;
            size_t out_offset = AUDIO_OUTPUT_FRAMES - max_frames;

            for (size_t mix_idx = 0; mix_idx < MAX_AUDIO_MIXES; mix_idx++) {
                if ((mixers & (1 << mix_idx)) == 0) {
                    continue;
                }
                for (size_t ch = 0; ch < filter->audio_channels; ch++) {
                    auto out = mixes[mix_idx].data[ch] + out_offset;
                    if (!header->data_idx[ch]) {
                        continue;
                    }
                    auto in = (float *)(filter->audio_conv_buffer + header->data_idx[ch]) + header->offset;

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

            if (frames == chunk_frames) {
                // Remove fulfilled chunk from buffer
                deque_pop_front(&filter->audio_buffer, NULL, data_size);
            } else {
                // Chunk frames are remaining -> modify chunk header
                header->offset += frames;
                deque_place(&filter->audio_buffer, 0, header, sizeof(audio_buffer_chunk_header_t));
            }

            max_frames -= frames;

            // Decrement buffer usage
            filter->audio_buffer_frames -= frames;
        }
    }
    pthread_mutex_unlock(&filter->audio_buffer_mutex);

    *out_ts = start_ts_in;
    return true;
}
