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
#include <obs-frontend-api.h>
#include <util/threading.h>
#include <util/config-file.h>
#include <util/dstr.h>

#include <QString>
#include <QWidget>
#include <QVariant>

QString getOutputFilename(const char *path, const char *container, bool noSpace, bool overwrite, const char *format);
QString getFormatExt(const char *container);

using OBSProperties = OBSPtr<obs_properties_t *, obs_properties_destroy>;
using OBSAudio = OBSPtr<audio_t *, audio_output_close>;

inline void strFree(char *ptr)
{
    bfree(ptr);
}
using OBSString = OBSPtr<char *, strFree>;

inline void pthreadMutexUnlock(pthread_mutex_t *mutex)
{
    pthread_mutex_unlock(mutex);
}
using OBSMutexAutoUnlock = OBSPtr<pthread_mutex_t *, pthreadMutexUnlock>;

// Imitate UI/window-basic-stats.cpp
// themeID: until OBS 30
// themeClasses: since OBS 31
inline void setThemeID(QWidget *widget, const QString &themeID, const QString &themeClasses)
{
    bool changed = false;
    if (widget->property("themeID").toString() != themeID) {
        widget->setProperty("themeID", themeID);
        changed = true;
    }

    if (widget->property("class").toString() != themeClasses) {
        widget->setProperty("class", themeClasses);
        changed = true;
    }

    if (changed) {
        /* force style sheet recalculation */
        QString qss = widget->styleSheet();
        widget->setStyleSheet("/* */");
        widget->setStyleSheet(qss);
    }
}

inline QString QTStr(const char *lookupVal)
{
    return QString::fromUtf8(obs_module_text(lookupVal));
}

// Decide source/scene is displayed in frontend or not
inline bool sourceInFrontend(obs_source_t *source)
{
    if (!source) {
        return false;
    }

    auto found = false;

    obs_frontend_source_list lsit = {0};
    obs_frontend_get_scenes(&lsit);
    {
        for (size_t i = 0; i < lsit.sources.num && !found; i++) {
            if (lsit.sources.array[i] == source) {
                found = true;
                break;
            }
            obs_scene_t *scene = obs_scene_from_source(lsit.sources.array[i]);
            found = !!obs_scene_find_source_recursive(scene, obs_source_get_name(source));
        }
    }
    obs_frontend_source_list_free(&lsit);

    return found;
}

// Decide source/scene is private or not
inline bool sourceIsPrivate(obs_source_t *source)
{
    auto finder = source;
    auto callback = [](void *param, obs_source_t *_source) {
        auto _finder = (obs_source_t **)param;
        if (_source == *_finder) {
            *_finder = nullptr;
            return false;
        }
        return true;
    };

    obs_enum_scenes(callback, &finder);
    if (finder != nullptr) {
        obs_enum_sources(callback, &finder);
    }

    return finder != nullptr;
}

// Return value must be obs_data_release() after use
inline obs_data_t *loadHotkeyData(const char *name)
{
    auto config = obs_frontend_get_profile_config();
    auto info = config_get_string(config, "Hotkeys", name);
    if (!info) {
        return nullptr;
    }

    return obs_data_create_from_json(info);
}

inline void loadHotkey(obs_hotkey_id id, const char *name)
{
    OBSDataAutoRelease data = loadHotkeyData(name);
    if (data) {
        OBSDataArrayAutoRelease array = obs_data_get_array(data, "bindings");
        obs_hotkey_load(id, array);
    }
}

inline QString getIndexedPropNameFormat(size_t index, size_t base = 0)
{
    return index == base ? QString("%1") : QString("%%1_%1").arg(index);
}

inline bool encoderAvailable(const char *encoder)
{
    const char *val;
    int i = 0;

    while (obs_enum_encoder_types(i++, &val)) {
        if (strcmp(val, encoder) == 0) {
            return true;
        }
    }

    return false;
}

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

// Hardcoded in obs-studio/UI/window-basic-main-outputs.cpp
inline const char *getSimpleVideoEncoder(const char *encoder)
{
    if (!strcmp(encoder, SIMPLE_ENCODER_X264)) {
        return "obs_x264";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_X264_LOWCPU)) {
        return "obs_x264";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_QSV)) {
        return "obs_qsv11_v2";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_QSV_AV1)) {
        return "obs_qsv11_av1";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_AMD)) {
        return "h264_texture_amf";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_AMD_HEVC)) {
        return "h265_texture_amf";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_AMD_AV1)) {
        return "av1_texture_amf";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_NVENC)) {
        return encoderAvailable("obs_nvenc_h264_tex") ? "obs_nvenc_h264_tex" // Since OBS 31
               : encoderAvailable("jim_nvenc")        ? "jim_nvenc"          // Until OBS 30
                                                      : "ffmpeg_nvenc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_NVENC_HEVC)) {
        return encoderAvailable("obs_nvenc_hevc_tex") ? "obs_nvenc_hevc_tex" // Since OBS 31
               : encoderAvailable("jim_hevc_nvenc")   ? "jim_hevc_nvenc"     // Until OBS 30
                                                      : "ffmpeg_hevc_nvenc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_NVENC_AV1)) {
        return encoderAvailable("obs_nvenc_av1_tex") ? "obs_nvenc_av1_tex" // Since OBS 31
                                                     : "jim_av1_nvenc";    // Until OBS 30
    } else if (!strcmp(encoder, SIMPLE_ENCODER_APPLE_H264)) {
        return "com.apple.videotoolbox.videoencoder.ave.avc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_APPLE_HEVC)) {
        return "com.apple.videotoolbox.videoencoder.ave.hevc";
    }

    return "obs_x264";
}

// Hardcoded in obs-studio/UI/window-basic-main-outputs.cpp
inline const char *getSimpleAudioEncoder(const char *encoder)
{
    if (strcmp(encoder, "opus")) {
        return "ffmpeg_opus";
    } else {
        return "ffmpeg_aac";
    }
}

inline bool isAdvancedMode(config_t *config = obs_frontend_get_profile_config())
{
    auto mode = config_get_string(config, "Output", "Mode");
    return !strcmp(mode, "Advanced") || !strcmp(mode, "advanced");
}

// Return value must be obs_data_release() after use
inline obs_data_t *getProfileRecordingSettings(config_t *config = obs_frontend_get_profile_config())
{
    obs_data_t *settings = obs_data_create();

    const char *recFormat;
    bool recSplitFile = false;
    const char *recSplitFileType = "Time";
    uint64_t recSplitFileTimeMins = 15;
    uint64_t recSplitFileSizeMb = 2048;
    const char *path;
    bool fileNameWithoutSpace = true;

    if (isAdvancedMode(config)) {
        recFormat = config_get_string(config, "AdvOut", "RecFormat2");
        recSplitFile = config_get_bool(config, "AdvOut", "RecSplitFile");
        recSplitFileTimeMins = config_get_uint(config, "AdvOut", "RecSplitFileTime");
        recSplitFileSizeMb = config_get_uint(config, "AdvOut", "RecSplitFileSize");
        fileNameWithoutSpace = config_get_bool(config, "AdvOut", "RecFileNameWithoutSpace");

        const char *recType = config_get_string(config, "AdvOut", "RecType");
        bool ffmpegRecording = !astrcmpi(recType, "ffmpeg") && config_get_bool(config, "AdvOut", "FFOutputToFile");
        path = config_get_string(config, "AdvOut", ffmpegRecording ? "FFFilePath" : "RecFilePath");
    } else {
        recFormat = config_get_string(config, "SimpleOutput", "RecFormat2");
        path = config_get_string(config, "SimpleOutput", "FilePath");
        fileNameWithoutSpace = config_get_bool(config, "SimpleOutput", "FileNameWithoutSpace");
    }

    const char *splitFileValue = "";
    if (recSplitFile && strcmp(recSplitFileType, "Manual")) {
        if (!strcmp(recSplitFileType, "Size")) {
            splitFileValue = "by_size";
        } else {
            splitFileValue = "by_time";
        }
    }
    obs_data_set_string(settings, "split_file", splitFileValue);

    QString filenameFormatting = QString("%1 %2 ") + QString(config_get_string(config, "Output", "FilenameFormatting"));
    obs_data_set_string(settings, "filename_formatting", qUtf8Printable(filenameFormatting));

    obs_data_set_string(settings, "path", path);
    obs_data_set_bool(settings, "no_space_filename", fileNameWithoutSpace);
    obs_data_set_string(settings, "rec_format", recFormat);
    obs_data_set_int(settings, "split_file_time_mins", recSplitFileTimeMins);
    obs_data_set_int(settings, "split_file_size_mb", recSplitFileSizeMb);

    return settings;
}
