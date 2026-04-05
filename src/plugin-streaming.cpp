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

bool BranchOutputFilter::createSreamingOutput(obs_data_t *settings, size_t index)
{
    auto count = (size_t)obs_data_get_int(settings, "service_count");
    if (index >= count || index >= MAX_SERVICES) {
        return false;
    }
    if (!isStreamingEnabled(settings, index)) {
        return false;
    }

    OBSDataAutoRelease streamingSettings = createStreamingSettings(settings, index);
    auto &ctx = streamings[index];

    // Create service - We always use "rtmp_custom" as service
    ctx.service = obs_service_create("rtmp_custom", qUtf8Printable(name), streamingSettings, nullptr);
    if (!ctx.service) {
        obs_log(LOG_ERROR, "%s: Streaming %zu service creation failed", qUtf8Printable(name), index);
        return false;
    }
    obs_service_apply_encoder_settings(ctx.service, streamingSettings, nullptr);

    // Determine output type
    auto type = obs_service_get_preferred_output_type(ctx.service);
    if (!type) {
        type = "rtmp_output";
        auto url = obs_service_get_connect_info(ctx.service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
        if (url != nullptr && !strncmp(url, FTL_PROTOCOL, strlen(FTL_PROTOCOL))) {
            type = "ftl_output";
        } else if (url != nullptr && strncmp(url, RTMP_PROTOCOL, strlen(RTMP_PROTOCOL))) {
            type = "ffmpeg_mpegts_muxer";
        }
    }

    // Create streaming output
    ctx.output =
        obs_output_create(type, qUtf8Printable(QString("%1 (%2)").arg(name).arg(index)), streamingSettings, nullptr);
    if (!ctx.output) {
        obs_log(LOG_ERROR, "%s (%zu): Streaming output creation failed", qUtf8Printable(name), index);
        return false;
    }
    obs_output_set_reconnect_settings(ctx.output, OUTPUT_MAX_RETRIES, OUTPUT_RETRY_DELAY_SECS);
    obs_output_set_service(ctx.output, ctx.service);

    return true;
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
    auto attemptingAt = streamings[index].reconnectAttemptingAt.load();
    return attemptingAt && os_gettime_ns() - attemptingAt > RECONNECT_ATTEMPTING_TIMEOUT_NS;
}

void BranchOutputFilter::setStreamingUserEnabled(size_t index, bool enabled)
{
    if (index < MAX_SERVICES)
        streamingUserEnabled[index].store(enabled, std::memory_order_relaxed);
}

bool BranchOutputFilter::isStreamingUserEnabled(size_t index) const
{
    return index < MAX_SERVICES ? streamingUserEnabled[index].load(std::memory_order_relaxed) : false;
}

bool BranchOutputFilter::isAnyStreamingUserEnabled() const
{
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        if (streamingUserEnabled[i].load(std::memory_order_relaxed))
            return true;
    }
    return false;
}

bool BranchOutputFilter::isAnyStreamingUserEnabled(obs_data_t *settings)
{
    auto serviceCount = (size_t)obs_data_get_int(settings, "service_count");
    for (size_t i = 0; i < MAX_SERVICES && i < serviceCount; i++) {
        if (isStreamingEnabled(settings, i) && streamingUserEnabled[i].load(std::memory_order_relaxed))
            return true;
    }
    return false;
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

bool BranchOutputFilter::isStreamingGroupEnabled(obs_data_t *settings)
{
    return obs_data_get_bool(settings, "streaming_enabled") && countEnabledStreamings(settings) > 0;
}

bool BranchOutputFilter::isStreamingEnabled(obs_data_t *settings, size_t index)
{
    auto propNameFormat = getIndexedPropNameFormat(index);
    return !!strlen(obs_data_get_string(settings, qUtf8Printable(propNameFormat.arg("server"))));
}

// Internal helper: caller must hold outputMutex.
// Caller must call ensureInfrastructure() before this function to set up
// the view, video/audio encoders, and related infrastructure.
// Note: stopStreamingOutput() sets streamings[i].output to nullptr, so stopped slots
// are always recreated with fresh settings via createSreamingOutput().
bool BranchOutputFilter::createAndStartStreamingOutputs(obs_data_t *settings)
{
    if (!isStreamingGroupEnabled(settings)) {
        return false;
    }
    if (countActiveStreamings() > 0) {
        return true;
    }

    auto serviceCount = (size_t)obs_data_get_int(settings, "service_count");
    for (size_t i = 0; i < MAX_SERVICES && i < serviceCount; i++) {
        if (!streamings[i].output && isStreamingUserEnabled(i)) {
            createSreamingOutput(settings, i);
        }
    }

    for (size_t i = 0; i < MAX_SERVICES; i++) {
        if (isStreamingUserEnabled(i) && streamings[i].output) {
            startStreamingOutput(i);
        }
    }

    return countActiveStreamings() > 0;
}

// Internal helper: caller must hold outputMutex.
// Returns true if all streamings have stopped.
// Note: stopStreamingOutput() called within assumes outputMutex is already held.
bool BranchOutputFilter::stopAllStreamingOutputsGracefully()
{
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        stopSingleStreamingOutputGracefully(i);
    }
    return countActiveStreamings() == 0;
}

bool BranchOutputFilter::startStreamingIndividual()
{
    OBSDataAutoRelease settings = obs_source_get_settings(filterSource);

    pthread_mutex_lock(&pluginMutex);
    {
        OBSMutexAutoUnlock pluginLocked(&pluginMutex);

        pthread_mutex_lock(&outputMutex);
        {
            OBSMutexAutoUnlock outputLocked(&outputMutex);

            if (!ensureInfrastructure(settings)) {
                return false;
            }

            if (!createAndStartStreamingOutputs(settings)) {
                releaseInfrastructureIfIdle();
                return false;
            }
        }
    }
    return true;
}

bool BranchOutputFilter::stopStreamingIndividual()
{
    pthread_mutex_lock(&pluginMutex);
    {
        OBSMutexAutoUnlock pluginLocked(&pluginMutex);

        pthread_mutex_lock(&outputMutex);
        {
            OBSMutexAutoUnlock outputLocked(&outputMutex);

            streamingIndividualStopping = true;

            if (!stopAllStreamingOutputsGracefully()) {
                return true;
            }

            streamingIndividualStopping = false;

            releaseInfrastructureIfIdle();
        }
    }
    return true;
}

bool BranchOutputFilter::startSingleStreamingIndividual(size_t index)
{
    OBSDataAutoRelease settings = obs_source_get_settings(filterSource);

    pthread_mutex_lock(&pluginMutex);
    {
        OBSMutexAutoUnlock pluginLocked(&pluginMutex);

        pthread_mutex_lock(&outputMutex);
        {
            OBSMutexAutoUnlock outputLocked(&outputMutex);

            if (!ensureInfrastructure(settings)) {
                return false;
            }

            if (!isStreamingGroupEnabled(settings) || !isStreamingEnabled(settings, index)) {
                releaseInfrastructureIfIdle();
                return false;
            }

            if (streamings[index].active) {
                return false;
            }

            if (!streamings[index].output) {
                createSreamingOutput(settings, index);
            }

            startStreamingOutput(index);

            if (!streamings[index].active) {
                releaseInfrastructureIfIdle();
                return false;
            }
        }
    }
    return true;
}

// Returns true to indicate that a stop operation was attempted (regardless of whether
// the stream has fully stopped yet). The caller uses this to track whether any action
// was taken during the current tick via `anyStopped |= stopSingleStreamingIndividual(i)`.
bool BranchOutputFilter::stopSingleStreamingIndividual(size_t index)
{
    pthread_mutex_lock(&pluginMutex);
    {
        OBSMutexAutoUnlock pluginLocked(&pluginMutex);

        pthread_mutex_lock(&outputMutex);
        {
            OBSMutexAutoUnlock outputLocked(&outputMutex);

            if (!stopSingleStreamingOutputGracefully(index)) {
                return true; // reconnecting, will retry next tick
            }

            releaseInfrastructureIfIdle();
        }
    }
    return true;
}

// Internal helper: caller must hold outputMutex.
// Returns true if the stream at the given index has stopped.
// Returns false if the stream is still waiting for reconnect timeout.
bool BranchOutputFilter::stopSingleStreamingOutputGracefully(size_t index)
{
    if (index >= MAX_SERVICES) {
        return true;
    }

    if (streamings[index].output && streamings[index].active) {
        if (streamings[index].stopping) {
            if (reconnectAttemptingTimedOut(index)) {
                stopStreamingOutput(index);
            } else {
                return false;
            }
        } else if (obs_output_reconnecting(streamings[index].output)) {
            streamings[index].stopping = true;
            return false;
        } else {
            stopStreamingOutput(index);
        }
    }

    return true;
}
