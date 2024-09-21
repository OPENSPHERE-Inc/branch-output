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
#include <util/dstr.h>
#include <obs.hpp>

#include "plugin-support.h"
#include "plugin-main.hpp"

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

inline void applyDefaults(obs_data_t *dest, obs_data_t *src)
{
    for (auto item = obs_data_first(src); item; obs_data_item_next(&item)) {
        auto name = obs_data_item_get_name(item);
        auto type = obs_data_item_gettype(item);

        switch (type) {
        case OBS_DATA_STRING:
            obs_data_set_default_string(dest, name, obs_data_item_get_string(item));
            break;
        case OBS_DATA_NUMBER: {
            auto numtype = obs_data_item_numtype(item);
            if (numtype == OBS_DATA_NUM_DOUBLE) {
                obs_data_set_default_double(dest, name, obs_data_item_get_double(item));
            } else if (numtype == OBS_DATA_NUM_INT) {
                obs_data_set_default_int(dest, name, obs_data_item_get_int(item));
            }
            break;
        }
        case OBS_DATA_BOOLEAN:
            obs_data_set_default_bool(dest, name, obs_data_item_get_bool(item));
            break;
        case OBS_DATA_OBJECT: {
            OBSDataAutoRelease value = obs_data_item_get_obj(item);
            obs_data_set_default_obj(dest, name, value);
            break;
        }
        case OBS_DATA_ARRAY: {
            OBSDataArrayAutoRelease value = obs_data_item_get_array(item);
            obs_data_set_default_array(dest, name, value);
            break;
        }
        case OBS_DATA_NULL:
            break;
        }
    }
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

// Hardcoded in obs-studio/UI/window-basic-settings.cpp
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
        return encoderAvailable("jim_nvenc") ? "jim_nvenc" : "ffmpeg_nvenc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_NVENC_HEVC)) {
        return encoderAvailable("jim_hevc_nvenc") ? "jim_hevc_nvenc" : "ffmpeg_hevc_nvenc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_NVENC_AV1)) {
        return "jim_av1_nvenc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_APPLE_H264)) {
        return "com.apple.videotoolbox.videoencoder.ave.avc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_APPLE_HEVC)) {
        return "com.apple.videotoolbox.videoencoder.ave.hevc";
    }

    return "obs_x264";
}

void getDefaults(obs_data_t *defaults)
{
    obs_log(LOG_DEBUG, "Default settings applying.");

    auto config = obs_frontend_get_profile_config();
    auto mode = config_get_string(config, "Output", "Mode");
    bool advancedOut = !strcmp(mode, "Advanced") || strcmp(mode, "advanced");

    const char *videoEncoderId;
    const char *audioEncoderId;
    uint64_t audioBitrate;
    const char *recFormat;
    bool recSplitFile = false;
    const char *recSplitFileType = "Time";
    uint64_t recSplitFileTimeMins = 15;
    uint64_t recSplitFileSizeMb = 2048;
    const char *path;

    // Capture values from OBS settings
    if (advancedOut) {
        videoEncoderId = config_get_string(config, "AdvOut", "Encoder");
        audioEncoderId = config_get_string(config, "AdvOut", "AudioEncoder");
        audioBitrate = config_get_uint(config, "AdvOut", "FFABitrate");
        recFormat = config_get_string(config, "AdvOut", "RecFormat2");
        recSplitFile = config_get_bool(config, "AdvOut", "RecSplitFile");
        recSplitFileTimeMins = config_get_uint(config, "AdvOut", "RecSplitFileTime");
        recSplitFileSizeMb = config_get_uint(config, "AdvOut", "RecSplitFileSize");

        const char *recType = config_get_string(config, "AdvOut", "RecType");
        bool ffmpegRecording = !astrcmpi(recType, "ffmpeg") && config_get_bool(config, "AdvOut", "FFOutputToFile");
        path = config_get_string(config, "AdvOut", ffmpegRecording ? "FFFilePath" : "RecFilePath");
    } else {
        videoEncoderId = getSimpleVideoEncoder(config_get_string(config, "SimpleOutput", "StreamEncoder"));
        audioEncoderId = getSimpleAudioEncoder(config_get_string(config, "SimpleOutput", "StreamAudioEncoder"));
        audioBitrate = config_get_uint(config, "SimpleOutput", "ABitrate");
        recFormat = config_get_string(config, "SimpleOutput", "RecFormat2");
        path = config_get_string(config, "SimpleOutput", "FilePath");
    }

    obs_data_set_default_string(defaults, "audio_encoder", audioEncoderId);
    obs_data_set_default_string(defaults, "video_encoder", videoEncoderId);
    obs_data_set_default_int(defaults, "audio_bitrate", audioBitrate);
    obs_data_set_default_bool(defaults, "stream_recording", false);
    obs_data_set_default_string(defaults, "path", path);
    obs_data_set_default_string(defaults, "rec_format", recFormat);

    const char *splitFileValue = "";
    if (recSplitFile && strcmp(recSplitFileType, "Manual")) {
        if (!strcmp(recSplitFileType, "Size")) {
            splitFileValue = "by_size";
        } else {
            splitFileValue = "by_time";
        }
    }
    obs_data_set_default_string(defaults, "split_file", splitFileValue);

    obs_data_set_default_int(defaults, "split_file_time_mins", recSplitFileTimeMins);
    obs_data_set_default_int(defaults, "split_file_size_mb", recSplitFileSizeMb);
    obs_data_set_default_string(defaults, "audio_source", "master_track");
    obs_data_set_default_int(defaults, "audio_track", 1);

    obs_log(LOG_INFO, "Default settings applied.");
}

inline void addApplyButton(BranchOutputFilter *filter, obs_properties_t *props)
{
    obs_properties_add_button2(
        props, "apply", obs_module_text("Apply"),
        [](obs_properties_t *, obs_property_t *, void *param) {
            auto _filter = (BranchOutputFilter *)param;

            // Force filter activation
            _filter->initialized = true;

            OBSDataAutoRelease settings = obs_source_get_settings(_filter->filterSource);
            update(_filter, settings);

            return true;
        },
        filter
    );
}

inline void addPluginInfo(obs_properties_t *props)
{
    char plugin_info_format[] = "<a href=\"https://github.com/OPENSPHERE-Inc/branch-output\">Branch Output</a> (v%s) "
                                "developed by <a href=\"https://opensphere.co.jp\">OPENSPHERE Inc.</a>";

    size_t buffer_size = sizeof(plugin_info_format) + strlen(PLUGIN_VERSION);
    auto plugin_info_text = (char *)bzalloc(buffer_size);
    snprintf(plugin_info_text, buffer_size, plugin_info_format, PLUGIN_VERSION);

    obs_properties_add_text(props, "plugin_info", plugin_info_text, OBS_TEXT_INFO);

    bfree(plugin_info_text);
}

inline void addStreamGroup(obs_properties_t *props)
{
    auto streamGroup = obs_properties_create();
    obs_properties_add_text(streamGroup, "server", obs_module_text("Server"), OBS_TEXT_DEFAULT);
    obs_properties_add_text(streamGroup, "key", obs_module_text("Key"), OBS_TEXT_PASSWORD);

    auto useAuth = obs_properties_add_bool(streamGroup, "use_auth", obs_module_text("UseAuthentication"));
    auto username = obs_properties_add_text(streamGroup, "username", obs_module_text("Username"), OBS_TEXT_DEFAULT);
    obs_property_set_visible(username, false);
    auto password = obs_properties_add_text(streamGroup, "password", obs_module_text("Password"), OBS_TEXT_PASSWORD);
    obs_property_set_visible(password, false);

    obs_property_set_modified_callback2(
        useAuth,
        [](void *, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
            auto _useAuth = obs_data_get_bool(settings, "use_auth");
            obs_property_set_visible(obs_properties_get(_props, "username"), _useAuth);
            obs_property_set_visible(obs_properties_get(_props, "password"), _useAuth);
            return true;
        },
        nullptr
    );

    auto streamRecording = obs_properties_add_bool(streamGroup, "stream_recording", obs_module_text("StreamRecording"));

    auto streamRecordingChangeHandler = [](void *, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
        auto _streamRecording = obs_data_get_bool(settings, "stream_recording");
        obs_property_set_visible(obs_properties_get(_props, "path"), _streamRecording);
        obs_property_set_visible(obs_properties_get(_props, "rec_format"), _streamRecording);
        obs_property_set_visible(obs_properties_get(_props, "split_file"), _streamRecording);

        auto splitFile = obs_data_get_string(settings, "split_file");
        obs_property_set_visible(
            obs_properties_get(_props, "split_file_time_mins"), _streamRecording && !strcmp(splitFile, "by_time")
        );
        obs_property_set_visible(
            obs_properties_get(_props, "split_file_size_mb"), _streamRecording && !strcmp(splitFile, "by_size")
        );
        return true;
    };

    obs_property_set_modified_callback2(streamRecording, streamRecordingChangeHandler, nullptr);

    //--- Recording options (initially hidden) ---//
    obs_properties_add_path(streamGroup, "path", obs_module_text("Path"), OBS_PATH_DIRECTORY, nullptr, nullptr);

    // Only support limited formats
    auto fileFormatList = obs_properties_add_list(
        streamGroup, "rec_format", obs_module_text("VideoFormat"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_set_visible(fileFormatList, false);
    obs_property_list_add_string(fileFormatList, obs_module_text("MKV"), "mkv");
    obs_property_list_add_string(fileFormatList, obs_module_text("hMP4"), "hybrid_mp4"); // beta
    obs_property_list_add_string(fileFormatList, obs_module_text("MP4"), "mp4");
    obs_property_list_add_string(fileFormatList, obs_module_text("MOV"), "mov");
    obs_property_list_add_string(fileFormatList, obs_module_text("TS"), "mpegts");

    auto splitFileList = obs_properties_add_list(
        streamGroup, "split_file", obs_module_text("SplitFile"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(splitFileList, obs_module_text("SplitFile.NoSplit"), "");
    obs_property_list_add_string(splitFileList, obs_module_text("SplitFile.ByTime"), "by_time");
    obs_property_list_add_string(splitFileList, obs_module_text("SplitFile.BySize"), "by_size");

    obs_property_set_modified_callback2(splitFileList, streamRecordingChangeHandler, nullptr);

    obs_properties_add_int(streamGroup, "split_file_time_mins", obs_module_text("SplitFile.Time"), 1, 525600, 1);
    obs_properties_add_int(streamGroup, "split_file_size_mb", obs_module_text("SplitFile.Size"), 1, 1073741824, 1);

    obs_properties_add_group(props, "stream", obs_module_text("Stream"), OBS_GROUP_NORMAL, streamGroup);
}

inline void addAudioGroup(obs_properties_t *props)
{
    auto audioGroup = obs_properties_create();
    auto audioSourceList = obs_properties_add_list(
        audioGroup, "audio_source", obs_module_text("Source"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );

    obs_property_list_add_string(audioSourceList, obs_module_text("NoAudio"), "no_audio");
    obs_property_list_add_string(audioSourceList, obs_module_text("MasterTrack"), "master_track");
    obs_enum_sources(
        [](void *param, obs_source_t *source) {
            auto prop = (obs_property_t *)param;
            const uint32_t flags = obs_source_get_output_flags(source);
            if (flags & OBS_SOURCE_AUDIO) {
                obs_property_list_add_string(prop, obs_source_get_name(source), obs_source_get_uuid(source));
            }
            return true;
        },
        audioSourceList
    );
    obs_property_set_modified_callback2(
        audioSourceList,
        [](void *, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
            auto audioSource = obs_data_get_string(settings, "audio_source");
            obs_property_set_enabled(obs_properties_get(_props, "audio_track"), !strcmp(audioSource, "master_track"));
            return true;
        },
        nullptr
    );

    auto audioTrackList = obs_properties_add_list(
        audioGroup, "audio_track", obs_module_text("Track"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT
    );
    for (int i = 1; i <= MAX_AUDIO_MIXES; i++) {
        char trackNo[] = "Track1";
        snprintf(trackNo, sizeof(trackNo), "Track%d", i);
        obs_property_list_add_int(audioTrackList, obs_module_text(trackNo), i);
    }
    obs_property_set_enabled(audioTrackList, false); // Initially disabled

    obs_properties_add_group(
        props, "custom_audio_source", obs_module_text("CustomAudioSource"), OBS_GROUP_CHECKABLE, audioGroup
    );
}

inline void addAudioEncoderGroup(BranchOutputFilter *filter, obs_properties_t *props)
{
    auto audioEncoderGroup = obs_properties_create();
    auto audioEncoderList = obs_properties_add_list(
        audioEncoderGroup, "audio_encoder", obs_module_text("AudioEncoder"), OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING
    );
    // The bitrate list is empty initially.
    obs_properties_add_list(
        audioEncoderGroup, "audio_bitrate", obs_module_text("AudioBitrate"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT
    );

    // Enum audio encoders
    const char *encoderId = nullptr;
    size_t i = 0;
    while (obs_enum_encoder_types(i++, &encoderId)) {
        auto caps = obs_get_encoder_caps(encoderId);
        if (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)) {
            // Ignore deprecated and internal
            continue;
        }

        auto name = obs_encoder_get_display_name(encoderId);

        if (obs_get_encoder_type(encoderId) == OBS_ENCODER_AUDIO) {
            obs_property_list_add_string(audioEncoderList, name, encoderId);
        }
    }

    obs_properties_add_group(
        props, "audio_encoder_group", obs_module_text("AudioEncoder"), OBS_GROUP_NORMAL, audioEncoderGroup
    );

    obs_property_set_modified_callback2(
        audioEncoderList,
        [](void *param, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
            auto _filter = (BranchOutputFilter *)param;
            obs_log(LOG_DEBUG, "%s: Audio encoder chainging.", obs_source_get_name(_filter->filterSource));

            const auto encoder_id = obs_data_get_string(settings, "audio_encoder");
            const auto encoder_props = obs_get_encoder_properties(encoder_id);
            const auto encoder_bitrate_prop = obs_properties_get(encoder_props, "bitrate");
            obs_properties_destroy(encoder_props);

            auto audio_encoder_group = obs_property_group_content(obs_properties_get(_props, "audio_encoder_group"));
            auto audio_bitrate_prop = obs_properties_get(audio_encoder_group, "audio_bitrate");

            obs_property_list_clear(audio_bitrate_prop);

            const auto type = obs_property_get_type(encoder_bitrate_prop);
            auto result = true;
            switch (type) {
            case OBS_PROPERTY_INT: {
                const auto max_value = obs_property_int_max(encoder_bitrate_prop);
                const auto step_value = obs_property_int_step(encoder_bitrate_prop);

                for (int val = obs_property_int_min(encoder_bitrate_prop); val <= max_value; val += step_value) {
                    char bitrateTitle[6];
                    snprintf(bitrateTitle, sizeof(bitrateTitle), "%d", val);
                    obs_property_list_add_int(audio_bitrate_prop, bitrateTitle, val);
                }

                break;
            }

            case OBS_PROPERTY_LIST: {
                const auto format = obs_property_list_format(encoder_bitrate_prop);
                if (format != OBS_COMBO_FORMAT_INT) {
                    obs_log(
                        LOG_ERROR, "%s: Invalid bitrate property given by encoder: %s",
                        obs_source_get_name(_filter->filterSource), encoder_id
                    );
                    result = false;
                    break;
                }

                const auto count = obs_property_list_item_count(encoder_bitrate_prop);
                for (size_t idx = 0; idx < count; idx++) {
                    if (obs_property_list_item_disabled(encoder_bitrate_prop, idx)) {
                        continue;
                    }
                    const auto bitrate = obs_property_list_item_int(encoder_bitrate_prop, idx);
                    char bitrateTitle[6];
                    snprintf(bitrateTitle, sizeof(bitrateTitle), "%lld", bitrate);
                    obs_property_list_add_int(audio_bitrate_prop, bitrateTitle, bitrate);
                }
                break;
            }

            default:
                break;
            }

            obs_log(LOG_INFO, "%s: Audio encoder changed.", obs_source_get_name(_filter->filterSource));
            return result;
        },
        filter
    );
}

inline void addVideoEncoderGroup(BranchOutputFilter *filter, obs_properties_t *props)
{
    auto videoEncoderGroup = obs_properties_create();

    // "Video Encoder" prop
    auto videoEncoderList = obs_properties_add_list(
        videoEncoderGroup, "video_encoder", obs_module_text("VideoEncoder"), OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING
    );

    // Enum video encoders
    const char *encoderId = nullptr;
    size_t i = 0;
    while (obs_enum_encoder_types(i++, &encoderId)) {
        auto caps = obs_get_encoder_caps(encoderId);
        if (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)) {
            // Ignore deprecated and internal
            continue;
        }

        auto name = obs_encoder_get_display_name(encoderId);

        if (obs_get_encoder_type(encoderId) == OBS_ENCODER_VIDEO) {
            obs_property_list_add_string(videoEncoderList, name, encoderId);
        }
    }

    obs_property_set_modified_callback2(
        videoEncoderList,
        [](void *param, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
            auto _filter = (BranchOutputFilter *)param;
            obs_log(LOG_DEBUG, "%s: Video encoder chainging.", obs_source_get_name(_filter->filterSource));

            auto video_encoder_group = obs_property_group_content(obs_properties_get(_props, "video_encoder_group"));
            auto encoder_id = obs_data_get_string(settings, "video_encoder");

            obs_properties_remove_by_name(video_encoder_group, "video_encoder_settings_group");

            auto encoder_props = obs_get_encoder_properties(encoder_id);
            if (encoder_props) {
                obs_properties_add_group(
                    video_encoder_group, "video_encoder_settings_group", obs_encoder_get_display_name(encoder_id),
                    OBS_GROUP_NORMAL, encoder_props
                );
            }

            // Apply encoder's defaults
            OBSDataAutoRelease encoder_defaults = obs_encoder_defaults(encoder_id);
            applyDefaults(settings, encoder_defaults);

            obs_log(LOG_INFO, "%s: Video encoder changed.", obs_source_get_name(_filter->filterSource));
            return true;
        },
        filter
    );

    //--- "Video Encoder Settings" group (Initially empty) ---//
    auto video_encoder_settings_group = obs_properties_create();
    obs_properties_add_group(
        videoEncoderGroup, "video_encoder_settings_group", obs_module_text("VideoEncoderSettings"), OBS_GROUP_NORMAL,
        video_encoder_settings_group
    );

    obs_properties_add_group(
        props, "video_encoder_group", obs_module_text("VideoEncoder"), OBS_GROUP_NORMAL, videoEncoderGroup
    );
}

obs_properties_t *getProperties(void *data)
{
    auto filter = (BranchOutputFilter *)data;

    auto props = obs_properties_create();
    obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

    //--- "Stream" group ---//
    addStreamGroup(props);

    //--- "Audio" gorup ---//
    addAudioGroup(props);

    //--- "Audio Encoder" group ---//
    addAudioEncoderGroup(filter, props);

    //--- "Video Encoder" group ---//
    addVideoEncoderGroup(filter, props);

    addApplyButton(filter, props);
    addPluginInfo(props);

    return props;
}

BranchOutputStatusDock *createOutputStatusDock()
{
    auto dock = new BranchOutputStatusDock();

    obs_frontend_add_dock_by_id("BranchOutputStatusDock", obs_module_text("BranchOutputStatus"), dock);

    return dock;
}
