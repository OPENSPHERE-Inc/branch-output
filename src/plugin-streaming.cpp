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
#include <util/threading.h>
#include <util/platform.h>
#include <obs.hpp>

#include "plugin-support.h"
#include "plugin-main.hpp"
#include "utils.hpp"

#define FTL_PROTOCOL "ftl"
#define RTMP_PROTOCOL "rtmp"
#define OUTPUT_MAX_RETRIES 7
#define OUTPUT_RETRY_DELAY_SECS 1
#define RECONNECT_ATTEMPTING_TIMEOUT_NS 2000000000ULL

obs_data_t *BranchOutputFilter::createStreamingSettings(obs_data_t *settings, size_t index)
{
    auto streamingSettings = obs_data_create();
    auto propNameFormat = getIndexedPropNameFormat(index);

    // Copy all settings
    obs_data_apply(streamingSettings, settings);

    if (index > 0) {
        // Apply indexed properties (Only on index >= 1 )
        obs_data_set_string(
            streamingSettings, "server", obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("server")))
        );
        obs_data_set_string(
            streamingSettings, "key", obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("key")))
        );
        obs_data_set_bool(
            streamingSettings, "use_auth", obs_data_get_bool(settings, qUtf8Printable(propNameFormat.arg("use_auth")))
        );
        obs_data_set_string(
            streamingSettings, "username", obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("username")))
        );
        obs_data_set_string(
            streamingSettings, "password", obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("password")))
        );
    }

    return streamingSettings;
}

BranchOutputFilter::BranchOutputStreamingContext
BranchOutputFilter::createSreamingOutput(obs_data_t *settings, size_t index)
{
    auto count = (size_t)obs_data_get_int(settings, "service_count");
    if (index >= count || index >= MAX_SERVICES) {
        return {0};
    }
    if (!isStreamingEnabled(settings, index)) {
        return {0};
    }

    OBSDataAutoRelease streamingSettings = createStreamingSettings(settings, index);
    BranchOutputStreamingContext context = {0};

    // Create service - We always use "rtmp_custom" as service
    context.service = obs_service_create("rtmp_custom", qUtf8Printable(name), streamingSettings, nullptr);
    if (!context.service) {
        obs_log(LOG_ERROR, "%s: Streaming %zu service creation failed", qUtf8Printable(name), index);
        return {0};
    }
    obs_service_apply_encoder_settings(context.service, streamingSettings, nullptr);

    // Determine output type
    auto type = obs_service_get_preferred_output_type(context.service);
    if (!type) {
        type = "rtmp_output";
        auto url = obs_service_get_connect_info(context.service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
        if (url != nullptr && !strncmp(url, FTL_PROTOCOL, strlen(FTL_PROTOCOL))) {
            type = "ftl_output";
        } else if (url != nullptr && strncmp(url, RTMP_PROTOCOL, strlen(RTMP_PROTOCOL))) {
            type = "ffmpeg_mpegts_muxer";
        }
    }

    // Create streaming output
    context.output =
        obs_output_create(type, qUtf8Printable(QString("%1 (%2)").arg(name).arg(index)), streamingSettings, nullptr);
    if (!context.output) {
        obs_log(LOG_ERROR, "%s (%zu): Streaming output creation failed", qUtf8Printable(name), index);
        return {0};
    }
    obs_output_set_reconnect_settings(context.output, OUTPUT_MAX_RETRIES, OUTPUT_RETRY_DELAY_SECS);
    obs_output_set_service(context.output, context.service);

    return context;
}

void BranchOutputFilter::startStreamingOutput(size_t index)
{
    if (!streamings[index].output) {
        return;
    }

    size_t encIndex = 0;
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        auto audioContext = &audios[i];
        if (!audioContext->encoder || !audioContext->streaming) {
            continue;
        }

        obs_output_set_audio_encoder(streamings[index].output, audioContext->encoder, encIndex++);
    }

    if (!encIndex) {
        // No audio encoder -> fallback first available encoder
        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &audios[i];
            if (audioContext->encoder) {
                obs_log(
                    LOG_WARNING, "%s (%zu): No audio encoder selected for streaming output, using track %d",
                    qUtf8Printable(name), index, i + 1
                );
                obs_output_set_audio_encoder(streamings[index].output, audioContext->encoder, encIndex++);
                break;
            }
        }
        if (!encIndex) {
            obs_log(LOG_ERROR, "%s (%zu): No audio encoder for streaming output", qUtf8Printable(name), index);
            return;
        }
    }

    obs_output_set_video_encoder(streamings[index].output, videoEncoder);

    streamings[index].outputStarting = true;

    // Track starting signal
    streamings[index].outputStartingSignal.Connect(
        obs_output_get_signal_handler(streamings[index].output), "starting",
        [](void *_data, calldata_t *) {
            auto context = static_cast<BranchOutputStreamingContext *>(_data);
            context->outputStarting = true;
            obs_log(LOG_DEBUG, "%s: Streaming output is starting", obs_output_get_name(context->output));
        },
        &streamings[index]
    );

    // Track activate signal
    streamings[index].outputActivateSignal.Connect(
        obs_output_get_signal_handler(streamings[index].output), "activate",
        [](void *_data, calldata_t *) {
            auto context = static_cast<BranchOutputStreamingContext *>(_data);
            context->outputStarting = false;
            obs_log(LOG_DEBUG, "%s: Streaming output has activated", obs_output_get_name(context->output));
        },
        &streamings[index]
    );

    // Track reconnect signal
    streamings[index].outputReconnectSignal.Connect(
        obs_output_get_signal_handler(streamings[index].output), "reconnect",
        [](void *_data, calldata_t *) {
            auto context = static_cast<BranchOutputStreamingContext *>(_data);
            context->reconnectAttemptingAt = os_gettime_ns();
            obs_log(LOG_DEBUG, "%s: Streaming output is reconnecting", obs_output_get_name(context->output));
        },
        &streamings[index]
    );

    // Track stop signal
    streamings[index].outputStopSignal.Connect(
        obs_output_get_signal_handler(streamings[index].output), "stop",
        [](void *_data, calldata_t *cd) {
            auto context = static_cast<BranchOutputStreamingContext *>(_data);
            context->outputStarting = false;
            auto code = calldata_int(cd, "code");
            obs_log(
                LOG_DEBUG, "%s: Streaming output has stopped with code=%lld", obs_output_get_name(context->output), code
            );
        },
        &streamings[index]
    );

    // Start streaming output
    if (obs_output_start(streamings[index].output)) {
        streamings[index].active = true;
        auto parent = obs_filter_get_parent(filterSource);
        if (parent) {
            obs_source_inc_showing(parent);
        }
        obs_log(LOG_INFO, "%s (%zu): Starting streaming output succeeded", qUtf8Printable(name), index);
    } else {
        obs_log(LOG_ERROR, "%s (%zu): Starting streaming output failed", qUtf8Printable(name), index);
    }
}

void BranchOutputFilter::stopStreamingOutput(size_t index)
{
    if (streamings[index].output && streamings[index].active) {
        obs_source_t *parent = obs_filter_get_parent(filterSource);
        if (parent) {
            obs_source_dec_showing(parent);
        }
        obs_output_stop(streamings[index].output);
        obs_log(LOG_INFO, "%s (%zu): Stopping streaming output succeeded", qUtf8Printable(name), index);
    }

    // Ensure signals are disconnected to prevent leaks across restarts
    streamings[index].outputStartingSignal.Disconnect();
    streamings[index].outputActivateSignal.Disconnect();
    streamings[index].outputReconnectSignal.Disconnect();
    streamings[index].outputStopSignal.Disconnect();

    streamings[index].output = nullptr;
    streamings[index].service = nullptr;
    streamings[index].reconnectAttemptingAt = 0;
    streamings[index].outputStarting = false;
    streamings[index].active = false;
    streamings[index].stopping = false;
}

void BranchOutputFilter::reconnectStreamingOutput(size_t index)
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (streamings[index].active) {
            obs_output_stop(streamings[index].output);

            if (!obs_output_start(streamings[index].output)) {
                obs_log(LOG_ERROR, "%s (%zu): Reconnect streaming output failed", qUtf8Printable(name), index);
            }
        }
    }
}

bool BranchOutputFilter::reconnectAttemptingTimedOut(size_t index)
{
    return streamings[index].reconnectAttemptingAt &&
           os_gettime_ns() - streamings[index].reconnectAttemptingAt > RECONNECT_ATTEMPTING_TIMEOUT_NS;
}

bool BranchOutputFilter::someStreamingsStarting()
{
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        if (streamings[i].outputStarting) {
            return true;
        }
    }
    return false;
}

int BranchOutputFilter::countEnabledStreamings(obs_data_t *settings)
{
    int count = 0;
    auto serviceCount = (size_t)obs_data_get_int(settings, "service_count");
    for (size_t i = 0; i < MAX_SERVICES && i < serviceCount; i++) {
        if (isStreamingEnabled(settings, i)) {
            count++;
        }
    }
    return count;
}

bool BranchOutputFilter::isStreamingGroupEnabled(obs_data_t *settings)
{
    return obs_data_get_bool(settings, "streaming_enabled") && countEnabledStreamings(settings) > 0;
}

int BranchOutputFilter::countAliveStreamings()
{
    int count = 0;
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        if (streamings[i].output && obs_output_active(streamings[i].output)) {
            count++;
        }
    }
    return count;
}

int BranchOutputFilter::countActiveStreamings()
{
    int count = 0;
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        if (streamings[i].active) {
            count++;
        }
    }
    return count;
}

bool BranchOutputFilter::hasEnabledStreamings(obs_data_t *settings)
{
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (isStreamingEnabled(settings, i)) {
            return true;
        }
    }
    return false;
}

bool BranchOutputFilter::isStreamingEnabled(obs_data_t *settings, size_t index)
{
    auto propNameFormat = getIndexedPropNameFormat(index);
    return !!strlen(obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("server"))));
}
