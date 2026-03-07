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

#include <QDateTime>
#include <QRegularExpression>

#include "plugin-support.h"
#include "plugin-main.hpp"
#include "utils.hpp"

obs_data_t *BranchOutputFilter::createRecordingSettings(obs_data_t *settings, bool createFolder)
{
    auto recordingSettings = obs_data_create();
    auto config = obs_frontend_get_profile_config();

    // Recording filename format (override takes precedence)
    QString filenameFormat;
    if (!recordingFilenameFormatOverride.isEmpty()) {
        filenameFormat = recordingFilenameFormatOverride;
    } else {
        filenameFormat = obs_data_get_string(settings, "filename_formatting");
        if (filenameFormat.isEmpty()) {
            filenameFormat = config_get_string(config, "Output", "FilenameFormatting");
        }
    }

    // Sanitize filename
#ifdef __APPLE__
    filenameFormat.replace(QRegularExpression("[:]"), "");
#elif defined(_WIN32)
    filenameFormat.replace(QRegularExpression("[<>:\"\\|\\?\\*]"), "");
#else
    // TODO: Add filtering for other platforms
#endif

    auto useProfileRecordingPath = obs_data_get_bool(settings, "use_profile_recording_path");
    auto path = useProfileRecordingPath ? getProfileRecordingPath(config) : obs_data_get_string(settings, "path");
    auto recFormat = obs_data_get_string(settings, "rec_format");

    // Validate recording path
    if (!path || !path[0]) {
        obs_log(LOG_ERROR, "%s: Recording path is not set", qUtf8Printable(name));
        obs_data_release(recordingSettings);
        return nullptr;
    }

    if (createFolder) {
        int ret = os_mkdirs(path);
        if (ret == MKDIR_ERROR) {
            obs_log(LOG_ERROR, "%s: Failed to create recording directory: %s", qUtf8Printable(name), path);
            obs_data_release(recordingSettings);
            return nullptr;
        }
    }

    // Add filter name to filename format
    bool noSpace = obs_data_get_bool(settings, "no_space_filename");
    filenameFormat = applyFilenameFormatArgs(filenameFormat, noSpace);
    auto compositePath = getOutputFilename(path, recFormat, noSpace, false, qUtf8Printable(filenameFormat));

    if (compositePath.isEmpty()) {
        obs_log(LOG_ERROR, "%s: Recording path is not accessible: %s", qUtf8Printable(name), path);
        obs_data_release(recordingSettings);
        return nullptr;
    }

    obs_data_set_string(recordingSettings, "path", qUtf8Printable(compositePath));

    auto splitFile = obs_data_get_string(settings, "split_file");
    splitRecordingEnabled = strlen(splitFile) > 0;
    if (splitRecordingEnabled) {
        obs_data_set_string(recordingSettings, "directory", path);
        obs_data_set_string(recordingSettings, "format", qUtf8Printable(filenameFormat));
        obs_data_set_string(recordingSettings, "extension", qUtf8Printable(getFormatExt(recFormat)));
        obs_data_set_bool(recordingSettings, "allow_spaces", false);
        obs_data_set_bool(recordingSettings, "allow_overwrite", false);
        obs_data_set_bool(recordingSettings, "split_file", true);

        auto maxTimeSec = !strcmp(splitFile, "by_time") ? obs_data_get_int(settings, "split_file_time_mins") * 60 : 0;
        obs_data_set_int(recordingSettings, "max_time_sec", maxTimeSec);

        auto maxSizeMb = !strcmp(splitFile, "by_size") ? obs_data_get_int(settings, "split_file_size_mb") : 0;
        obs_data_set_int(recordingSettings, "max_size_mb", maxSizeMb);
    }

    // Apply fragmented MP4/MOV settings
    // Immitate https://github.com/obsproject/obs-studio/blob/d3c5d2ce0b15bac7a502f5aef4b3b5ec72ee8e09/frontend/utility/SimpleOutput.cpp#L820
    QString mux = obs_data_get_string(settings, "rec_muxer_custom");
    bool isFragmented = strncmp(recFormat, "fragmented", 10) == 0;

    if (isFragmented && (mux.isEmpty() || mux.indexOf("movflags") == -1)) {
        QString muxFrag = "movflags=frag_keyframe+empty_moov+delay_moov";
        if (!mux.isEmpty()) {
            muxFrag += " " + mux;
        }
        obs_data_set_string(recordingSettings, "muxer_settings", qUtf8Printable(muxFrag));
    } else {
        if (isFragmented) {
            obs_log(LOG_WARNING, "User enabled fragmented recording, but custom muxer settings contained movflags.");
        } else {
            obs_data_set_string(recordingSettings, "muxer_settings", qUtf8Printable(mux));
        }
    }

    return recordingSettings;
}

void BranchOutputFilter::createAndStartRecordingOutput(obs_data_t *settings)
{
    if (!videoEncoder) {
        return;
    }

    auto recFormat = obs_data_get_string(settings, "rec_format");
    const char *outputId = !strcmp(recFormat, "hybrid_mp4") ? "mp4_output" : "ffmpeg_muxer";

    // Dtermine chapter marker capability
    // Chapter maker is only available for hybrid MP4
    addChapterToRecordingEnabled = !strcmp(recFormat, "hybrid_mp4");

    // Ensure base path exists
    OBSDataAutoRelease recordingSettings = createRecordingSettings(settings, true);
    if (!recordingSettings) {
        obs_log(LOG_ERROR, "%s: Recording settings creation failed (path unavailable?)", qUtf8Printable(name));
        return;
    }
    recordingOutput = obs_output_create(outputId, qUtf8Printable(name), recordingSettings, nullptr);
    if (!recordingOutput) {
        obs_log(LOG_ERROR, "%s: Recording output creation failed", qUtf8Printable(name));
        return;
    }

    size_t encIndex = 0;
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        auto audioContext = &audios[i];
        if (!audioContext->encoder || !audioContext->recording) {
            continue;
        }

        obs_output_set_audio_encoder(recordingOutput, audioContext->encoder, encIndex++);
    }

    if (!encIndex) {
        // No audio encoder -> fallback first available encoder
        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &audios[i];
            if (audioContext->encoder) {
                obs_log(
                    LOG_WARNING, "%s: No audio encoder selected for recording, using track %d", qUtf8Printable(name),
                    i + 1
                );
                obs_output_set_audio_encoder(recordingOutput, audioContext->encoder, encIndex++);
                break;
            }
        }
        if (!encIndex) {
            obs_log(LOG_ERROR, "%s: No audio encoder for recording", qUtf8Printable(name));
            return;
        }
    }

    obs_output_set_video_encoder(recordingOutput, videoEncoder);

    // Start recording output
    if (obs_output_start(recordingOutput)) {
        recordingActive = true;
        recordingPending = false;
        auto parent = obs_filter_get_parent(filterSource);
        if (parent) {
            obs_source_inc_showing(parent);
        }
        obs_log(LOG_INFO, "%s: Starting recording output succeeded", qUtf8Printable(name));
    } else {
        obs_log(LOG_ERROR, "%s: Starting recording output failed", qUtf8Printable(name));
    }
}

void BranchOutputFilter::stopRecordingOutput(bool pending)
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (recordingOutput) {
            if (recordingActive) {
                obs_source_t *parent = obs_filter_get_parent(filterSource);
                if (parent) {
                    obs_source_dec_showing(parent);
                }
                obs_output_stop(recordingOutput);
            }
        }
        recordingOutput = nullptr;

        if (recordingActive) {
            recordingActive = false;
            obs_log(LOG_INFO, "%s: Stopping recording output succeeded", qUtf8Printable(name));
        }

        recordingPending = pending;
        recordingSettingsOverridden = false;
        addChapterToRecordingEnabled = false;
        splitRecordingEnabled = false;
    }
}

void BranchOutputFilter::restartRecordingOutput()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (recordingActive && recordingOutput) {
            // Re-create recording settings with current override values
            OBSDataAutoRelease filterSettings = obs_source_get_settings(filterSource);
            OBSDataAutoRelease newSettings = createRecordingSettings(filterSettings);
            if (newSettings) {
                obs_output_update(recordingOutput, newSettings);
            }

            obs_output_stop(recordingOutput);

            if (!obs_output_start(recordingOutput)) {
                obs_log(LOG_ERROR, "%s: Restart recording output failed", qUtf8Printable(name));
            }
        }
    }
}

bool BranchOutputFilter::isRecordingEnabled(obs_data_t *settings)
{
    return obs_data_get_bool(settings, "stream_recording");
}

bool BranchOutputFilter::isSplitRecordingEnabled(obs_data_t *settings)
{
    // Split mode will be disabled when some streamings are enabled.
    return isRecordingEnabled(settings) && !!strlen(obs_data_get_string(settings, "split_file"));
}

bool BranchOutputFilter::canPauseRecording()
{
    // Pausing recording will be disabled when some streamings are active.
    return countActiveStreamings() == 0 && !recordingPending;
}

bool BranchOutputFilter::canAddChapterToRecording()
{
    return recordingActive && recordingOutput && addChapterToRecordingEnabled && !obs_output_paused(recordingOutput);
}

bool BranchOutputFilter::canSplitRecording()
{
    return recordingActive && recordingOutput && splitRecordingEnabled;
}

bool BranchOutputFilter::splitRecording()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (!splitRecordingEnabled || !recordingActive || !recordingOutput) {
            return false;
        }

        // Immitate obs_frontend_recording_split_file()
        proc_handler_t *ph = obs_output_get_proc_handler(recordingOutput);
        uint8_t stack[128];
        calldata cd;
        calldata_init_fixed(&cd, stack, sizeof(stack));
        proc_handler_call(ph, "split_file", &cd);
        return calldata_bool(&cd, "split_file_enabled");
    }
}

bool BranchOutputFilter::pauseRecording()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (!recordingActive || !recordingOutput || countActiveStreamings() > 0) {
            // Can't pause when some streamings are active
            return false;
        }

        if (obs_output_paused(recordingOutput)) {
            // Already paused
            return false;
        }

        obs_output_pause(recordingOutput, true);
        return true;
    }
}

bool BranchOutputFilter::unpauseRecording()
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (!recordingActive || !recordingOutput) {
            return false;
        }

        if (!obs_output_paused(recordingOutput)) {
            // Already unpaused
            return false;
        }

        obs_output_pause(recordingOutput, false);
        recordingPending = false; // Force clear pending flag
        return true;
    }
}

bool BranchOutputFilter::addChapterToRecording(QString chapterName)
{
    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock locked(&outputMutex);

        if (!addChapterToRecordingEnabled || !recordingActive || !recordingOutput) {
            return false;
        }

        if (obs_output_paused(recordingOutput)) {
            // Can't add chapter when paused
            return false;
        }

        // Immitate obs_frontend_recording_add_chapter()
        proc_handler_t *ph = obs_output_get_proc_handler(recordingOutput);
        calldata cd;
        calldata_init(&cd);
        // Use current date-time when name is empty
        calldata_set_string(
            &cd, "chapter_name",
            chapterName.isEmpty() ? qUtf8Printable(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"))
                                  : qUtf8Printable(chapterName)
        );
        bool result = proc_handler_call(ph, "add_chapter", &cd);
        calldata_free(&cd);
        return result;
    }
}

void BranchOutputFilter::onOverrideRecordingFilenameFormat(void *data, calldata_t *cd)
{
    auto filter = static_cast<BranchOutputFilter *>(data);

    const char *format = calldata_string(cd, "format");
    bool needsSplit = false;

    pthread_mutex_lock(&filter->outputMutex);
    {
        OBSMutexAutoUnlock locked(&filter->outputMutex);

        if (!format || !format[0]) {
            // Empty format -> clear override (revert to filter settings)
            filter->recordingFilenameFormatOverride.clear();
            obs_log(LOG_INFO, "%s: Recording filename format override cleared", qUtf8Printable(filter->name));
        } else {
            filter->recordingFilenameFormatOverride = QString(format);
            obs_log(LOG_INFO, "%s: Recording filename format override set to: %s", qUtf8Printable(filter->name), format);
        }

        // If recording is not active, just store the override for next start
        if (!filter->recordingActive || !filter->recordingOutput) {
            return;
        }

        // Recording is active
        if (filter->splitRecordingEnabled && !filter->recordingPending) {
            // Split file enabled: update format setting, then trigger split outside mutex
            OBSDataAutoRelease filterSettings = obs_source_get_settings(filter->filterSource);
            bool noSpace = obs_data_get_bool(filterSettings, "no_space_filename");
            QString appliedFormat = filter->applyFilenameFormatArgs(
                filter->recordingFilenameFormatOverride.isEmpty()
                    ? QString(obs_data_get_string(filterSettings, "filename_formatting"))
                    : filter->recordingFilenameFormatOverride,
                noSpace
            );

            OBSDataAutoRelease settings = obs_data_create();
            obs_data_set_string(settings, "format", qUtf8Printable(appliedFormat));
            obs_output_update(filter->recordingOutput, settings);

            needsSplit = true;
            obs_log(
                LOG_INFO, "%s: Recording output format updated: %s", qUtf8Printable(filter->name),
                qUtf8Printable(appliedFormat)
            );
        } else {
            // Delegate to intervalTimer: stop (if pending) or restart
            filter->recordingSettingsOverridden = true;
        }
    }

    // Split can be called directly (safe from any thread)
    if (needsSplit) {
        filter->splitRecording();
    }
}
