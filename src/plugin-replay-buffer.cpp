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
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/threading.h>
#include <util/platform.h>
#include <obs.hpp>

#include <QRegularExpression>

#include "plugin-support.h"
#include "plugin-main.hpp"
#include "utils.hpp"

void BranchOutputFilter::stopReplayBufferOutput()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (replayBufferOutput) {
            if (replayBufferActive) {
                obs_source_t *parent = obs_filter_get_parent(filterSource);
                if (parent) {
                    obs_source_dec_showing(parent);
                }
                obs_output_stop(replayBufferOutput);
            }
        }
        replayBufferSavedSignal.Disconnect();
        replayBufferOutput = nullptr;

        if (replayBufferActive) {
            replayBufferActive = false;
            obs_log(LOG_INFO, "%s: Stopping replay buffer succeeded", qUtf8Printable(name));
        }
    }
}

obs_data_t *BranchOutputFilter::createReplayBufferSettings(obs_data_t *settings)
{
    auto replaySettings = obs_data_create();
    auto config = obs_frontend_get_profile_config();

    // Use replay buffer specific path settings
    auto useProfilePath = obs_data_get_bool(settings, "replay_buffer_use_profile_path");
    auto path = useProfilePath ? getProfileRecordingPath(config) : obs_data_get_string(settings, "replay_buffer_path");
    auto rbFormat = obs_data_get_string(settings, "replay_buffer_format");

    // Validate path
    if (!path || !path[0]) {
        obs_log(LOG_ERROR, "%s: Replay buffer path is not set", qUtf8Printable(name));
        obs_data_release(replaySettings);
        return nullptr;
    }

    // Create directory
    int ret = os_mkdirs(path);
    if (ret == MKDIR_ERROR) {
        obs_log(LOG_ERROR, "%s: Failed to create replay buffer directory: %s", qUtf8Printable(name), path);
        obs_data_release(replaySettings);
        return nullptr;
    }

    // Replay buffer specific filename format
    QString filenameFormat = obs_data_get_string(settings, "replay_buffer_filename_formatting");
    if (filenameFormat.isEmpty()) {
        filenameFormat = config_get_string(config, "Output", "FilenameFormatting");
    }

    // Sanitize filename
#ifdef __APPLE__
    filenameFormat.replace(QRegularExpression("[:]"), "");
#elif defined(_WIN32)
    filenameFormat.replace(QRegularExpression("[<>:\"\\|\\?\\*]"), "");
#else
    // TODO: Add filtering for other platforms
#endif

    QString sourceName = obs_source_get_name(obs_filter_get_parent(filterSource));
    QString filterName = qUtf8Printable(name);
    bool noSpace = obs_data_get_bool(settings, "replay_buffer_no_space_filename");
    auto re = noSpace ? QRegularExpression("[\\s/\\\\.:;*?\"<>|&$,]") : QRegularExpression("[/\\\\.:;*?\"<>|&$,]");
    filenameFormat = filenameFormat.arg(sourceName.replace(re, "-")).arg(filterName.replace(re, "-"));

    obs_data_set_string(replaySettings, "directory", path);
    obs_data_set_string(replaySettings, "format", qUtf8Printable(filenameFormat));
    obs_data_set_string(replaySettings, "extension", qUtf8Printable(getFormatExt(rbFormat)));
    obs_data_set_bool(replaySettings, "allow_spaces", !noSpace);
    obs_data_set_int(replaySettings, "max_time_sec", obs_data_get_int(settings, "replay_buffer_duration"));
    obs_data_set_int(replaySettings, "max_size_mb", 512);

    // Fragmented MP4/MOV support
    bool isFragmented = strncmp(rbFormat, "fragmented", 10) == 0;
    if (isFragmented) {
        obs_data_set_string(replaySettings, "muxer_settings", "movflags=frag_keyframe+empty_moov+delay_moov");
    }

    return replaySettings;
}

void BranchOutputFilter::createAndStartReplayBuffer(obs_data_t *settings)
{
    if (!videoEncoder) {
        return;
    }

    OBSDataAutoRelease rbSettings = createReplayBufferSettings(settings);
    if (!rbSettings) {
        obs_log(LOG_ERROR, "%s: Replay buffer settings creation failed", qUtf8Printable(name));
        return;
    }

    replayBufferOutput = obs_output_create("replay_buffer", qUtf8Printable(name), rbSettings, nullptr);
    if (!replayBufferOutput) {
        obs_log(LOG_ERROR, "%s: Replay buffer output creation failed", qUtf8Printable(name));
        return;
    }

    // Bind audio encoders (same logic as recording)
    size_t encIndex = 0;
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        auto audioContext = &audios[i];
        if (!audioContext->encoder || !audioContext->recording) {
            continue;
        }

        obs_output_set_audio_encoder(replayBufferOutput, audioContext->encoder, encIndex++);
    }

    if (!encIndex) {
        // No audio encoder -> fallback first available encoder
        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &audios[i];
            if (audioContext->encoder) {
                obs_log(
                    LOG_WARNING, "%s: No audio encoder selected for replay buffer, using track %d",
                    qUtf8Printable(name), i + 1
                );
                obs_output_set_audio_encoder(replayBufferOutput, audioContext->encoder, encIndex++);
                break;
            }
        }
        if (!encIndex) {
            obs_log(LOG_ERROR, "%s: No audio encoder for replay buffer", qUtf8Printable(name));
            return;
        }
    }

    obs_output_set_video_encoder(replayBufferOutput, videoEncoder);

    // Connect "saved" signal
    auto handler = obs_output_get_signal_handler(replayBufferOutput);
    replayBufferSavedSignal.Connect(handler, "saved", onReplayBufferSaved, this);

    // Start replay buffer output
    if (obs_output_start(replayBufferOutput)) {
        replayBufferActive = true;
        auto parent = obs_filter_get_parent(filterSource);
        if (parent) {
            obs_source_inc_showing(parent);
        }
        obs_log(LOG_INFO, "%s: Starting replay buffer succeeded", qUtf8Printable(name));
    } else {
        obs_log(LOG_ERROR, "%s: Starting replay buffer failed", qUtf8Printable(name));
    }
}

bool BranchOutputFilter::isReplayBufferEnabled(obs_data_t *settings)
{
    return obs_data_get_bool(settings, "replay_buffer");
}

bool BranchOutputFilter::saveReplayBuffer()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (!replayBufferActive || !replayBufferOutput) {
            return false;
        }

        auto ph = obs_output_get_proc_handler(replayBufferOutput);
        calldata_t cd = {0};
        proc_handler_call(ph, "save", &cd);
        calldata_free(&cd);

        obs_log(LOG_INFO, "%s: Replay buffer save triggered", qUtf8Printable(name));
        return true;
    }
}

void BranchOutputFilter::onReplayBufferSaved(void *data, calldata_t *)
{
    auto filter = static_cast<BranchOutputFilter *>(data);
    obs_log(LOG_INFO, "%s: Replay buffer saved", qUtf8Printable(filter->name));
}

void BranchOutputFilter::onSaveReplayBufferHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey *, bool pressed)
{
    if (!pressed) {
        return;
    }

    auto filter = static_cast<BranchOutputFilter *>(data);
    filter->saveReplayBuffer();
}
