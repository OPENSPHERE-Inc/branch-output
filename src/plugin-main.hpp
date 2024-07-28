/*
Source Output Plugin
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

#define MAX_AUDIO_BUFFER_FRAMES 131071
#define SETTINGS_JSON_NAME "recently.json"
#define OUTPUT_MAX_RETRIES 7
#define OUTPUT_RETRY_DELAY_SECS 1
#define CONNECT_ATTEMPTING_TIMEOUT_NS 15000000000ULL

enum AudioSourceType {
    AUDIO_SOURCE_TYPE_SILENCE,
    AUDIO_SOURCE_TYPE_FILTER,
    AUDIO_SOURCE_TYPE_MASTER,
    AUDIO_SOURCE_TYPE_CAPTURE,
};

struct filter_t {
    bool filter_active; // Activate after first "Apply" click
    bool output_active;
    uint32_t stored_settings_rev;
    uint32_t active_settings_rev;

    // Filter source
    obs_source_t *source;
    obs_weak_source_t *audio_source; // NULL means using filter's audio

    // User choosed encoder
    obs_encoder_t *video_encoder;
    obs_encoder_t *audio_encoder;

    obs_view_t *view;
    video_t *video_output;
    audio_t *audio_output;
    obs_output_t *stream_output;
    obs_service_t *service;

    // Video context
    uint32_t width;
    uint32_t height;

    // Audio context
    AudioSourceType audio_source_type;
    deque audio_buffer;
    size_t audio_buffer_frames;
    pthread_mutex_t audio_buffer_mutex;
    uint8_t *audio_conv_buffer;
    size_t audio_conv_buffer_size;
    size_t audio_mix_idx;
    speaker_layout audio_channels;
    uint32_t samples_per_sec;

    // Stream context
    uint64_t connect_attempting_at;
};

struct audio_buffer_chunk_header_t {
    size_t data_idx[MAX_AUDIO_CHANNELS]; // Zero means unused channel
    uint32_t frames;
    uint64_t timestamp;
    size_t offset;
    size_t channels;
};

void update(void *data, obs_data_t *settings);
void get_defaults(obs_data_t *defaults);
obs_properties_t *get_properties(void *data);
