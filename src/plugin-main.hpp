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

// Hardcoded in obs-studio/UI/window-basic-main.hpp
#define SIMPLE_ENCODER_X264 "x264"
#define SIMPLE_ENCODER_X264_LOWCPU "x264_lowcpu"
#define SIMPLE_ENCODER_QSV "qsv"
#define SIMPLE_ENCODER_QSV_AV1 "qsv_av1"
#define SIMPLE_ENCODER_NVENC "nvenc"
#define SIMPLE_ENCODER_NVENC_AV1 "nvenc_av1"
#define SIMPLE_ENCODER_NVENC_HEVC "nvenc_hevc"
#define SIMPLE_ENCODER_AMD "amd"
#define SIMPLE_ENCODER_AMD_HEVC "amd_hevc"
#define SIMPLE_ENCODER_AMD_AV1 "amd_av1"
#define SIMPLE_ENCODER_APPLE_H264 "apple_h264"
#define SIMPLE_ENCODER_APPLE_HEVC "apple_hevc"


#define MAX_AUDIO_BUFFER_FRAMES 131071


struct filter_t {
	bool output_active;

	// Filter source
	obs_source_t *source;
	obs_weak_source_t *audio_source;  // NULL means using filter's audio

	// User choosed encoder
	obs_encoder_t *video_encoder;
	obs_encoder_t *audio_encoder;
	
	obs_view_t *view;
	video_t *video_output;
	audio_t *audio_output;
	obs_output_t *stream_output;
	obs_service_t *service;

	// Audio context
	bool audio_enabled;
	deque audio_buffer;
	size_t audio_buffer_frames;
	pthread_mutex_t audio_buffer_mutex;
	uint8_t* audio_conv_buffer;
	size_t audio_conv_buffer_size;
};

struct audio_buffer_chunk_header_t {
	size_t data_idx[MAX_AUDIO_CHANNELS];  // Zero means unused channel
	uint32_t frames;
	uint64_t timestamp;
	size_t offset;
	size_t channels;
};


void update(void *data, obs_data_t *settings);
void get_defaults(obs_data_t *defaults);
obs_properties_t *get_properties(void *data);
