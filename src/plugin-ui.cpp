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
#include <plugin-support.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/dstr.h>
#include <QMainWindow>
#include <QDockWidget>
#include "plugin-main.hpp"
#include "plugin-support.h"

inline bool encoder_available(const char *encoder)
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

inline void apply_defaults(obs_data_t *dest, obs_data_t *src)
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
            auto value = obs_data_item_get_obj(item);
            obs_data_set_default_obj(dest, name, value);
            obs_data_release(value);
            break;
        }
        case OBS_DATA_ARRAY: {
            auto value = obs_data_item_get_array(item);
            obs_data_set_default_array(dest, name, value);
            obs_data_array_release(value);
            break;
        }
        case OBS_DATA_NULL:
            break;
        }
    }
}

inline void add_apply_button(filter_t *filter, obs_properties_t *props)
{
    obs_properties_add_button2(
        props, "apply", obs_module_text("Apply"),
        [](obs_properties_t *, obs_property_t *, void *param) {
            auto param_filter = (filter_t *)param;

            // Force filter activation
            param_filter->filter_active = true;

            auto settings = obs_source_get_settings(param_filter->source);
            update(param_filter, settings);
            obs_data_release(settings);

            return true;
        },
        filter
    );
}

inline void add_plugin_info(obs_properties_t *props)
{
    char plugin_info_format[] = "<a href=\"https://github.com/OPENSPHERE-Inc/branch-output\">Branch Output</a> (v%s) "
                                "developed by <a href=\"https://opensphere.co.jp\">OPENSPHERE Inc.</a>";

    size_t buffer_size = sizeof(plugin_info_format) + strlen(PLUGIN_VERSION);
    auto plugin_info_text = (char *)bzalloc(buffer_size);
    snprintf(plugin_info_text, buffer_size, plugin_info_format, PLUGIN_VERSION);

    obs_properties_add_text(props, "plugin_info", plugin_info_text, OBS_TEXT_INFO);

    bfree(plugin_info_text);
}

bool audio_encoder_changed(void *param, obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
    auto filter = (filter_t *)param;
    obs_log(LOG_DEBUG, "%s: Audio encoder chainging.", obs_source_get_name(filter->source));

    const auto encoder_id = obs_data_get_string(settings, "audio_encoder");
    const auto encoder_props = obs_get_encoder_properties(encoder_id);
    const auto encoder_bitrate_prop = obs_properties_get(encoder_props, "bitrate");
    obs_properties_destroy(encoder_props);

    auto audio_encoder_group = obs_property_group_content(obs_properties_get(props, "audio_encoder_group"));
    auto audio_bitrate_prop = obs_properties_get(audio_encoder_group, "audio_bitrate");

    obs_property_list_clear(audio_bitrate_prop);

    const auto type = obs_property_get_type(encoder_bitrate_prop);
    auto result = true;
    switch (type) {
    case OBS_PROPERTY_INT: {
        const auto max_value = obs_property_int_max(encoder_bitrate_prop);
        const auto step_value = obs_property_int_step(encoder_bitrate_prop);

        for (int i = obs_property_int_min(encoder_bitrate_prop); i <= max_value; i += step_value) {
            char bitrateTitle[6];
            snprintf(bitrateTitle, sizeof(bitrateTitle), "%d", i);
            obs_property_list_add_int(audio_bitrate_prop, bitrateTitle, i);
        }

        break;
    }

    case OBS_PROPERTY_LIST: {
        const auto format = obs_property_list_format(encoder_bitrate_prop);
        if (format != OBS_COMBO_FORMAT_INT) {
            obs_log(
                LOG_ERROR, "%s: Invalid bitrate property given by encoder: %s", obs_source_get_name(filter->source),
                encoder_id
            );
            result = false;
            break;
        }

        const auto count = obs_property_list_item_count(encoder_bitrate_prop);
        for (size_t i = 0; i < count; i++) {
            if (obs_property_list_item_disabled(encoder_bitrate_prop, i)) {
                continue;
            }
            const auto bitrate = obs_property_list_item_int(encoder_bitrate_prop, i);
            char bitrateTitle[6];
            snprintf(bitrateTitle, sizeof(bitrateTitle), "%lld", bitrate);
            obs_property_list_add_int(audio_bitrate_prop, bitrateTitle, bitrate);
        }
        break;
    }

    default:
        break;
    }

    obs_log(LOG_INFO, "%s: Audio encoder changed.", obs_source_get_name(filter->source));
    return result;
}

bool video_encoder_changed(void *param, obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
    auto filter = (filter_t *)param;
    obs_log(LOG_DEBUG, "%s: Video encoder chainging.", obs_source_get_name(filter->source));

    auto video_encoder_group = obs_property_group_content(obs_properties_get(props, "video_encoder_group"));
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
    auto encoder_defaults = obs_encoder_defaults(encoder_id);
    apply_defaults(settings, encoder_defaults);
    obs_data_release(encoder_defaults);

    obs_log(LOG_INFO, "%s: Video encoder changed.", obs_source_get_name(filter->source));
    return true;
}

// Hardcoded in obs-studio/UI/window-basic-main-outputs.cpp
inline const char *get_simple_audio_encoder(const char *encoder)
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
inline const char *get_simple_video_encoder(const char *encoder)
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
        return encoder_available("jim_nvenc") ? "jim_nvenc" : "ffmpeg_nvenc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_NVENC_HEVC)) {
        return encoder_available("jim_hevc_nvenc") ? "jim_hevc_nvenc" : "ffmpeg_hevc_nvenc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_NVENC_AV1)) {
        return "jim_av1_nvenc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_APPLE_H264)) {
        return "com.apple.videotoolbox.videoencoder.ave.avc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_APPLE_HEVC)) {
        return "com.apple.videotoolbox.videoencoder.ave.hevc";
    }

    return "obs_x264";
}

void get_defaults(obs_data_t *defaults)
{
    obs_log(LOG_DEBUG, "Default settings applying.");

    auto config = obs_frontend_get_profile_config();
    auto mode = config_get_string(config, "Output", "Mode");
    bool advanced_out = strcmp(mode, "Advanced") == 0 || strcmp(mode, "advanced");

    const char *video_encoder_id;
    const char *audio_encoder_id;
    uint64_t audio_bitrate;
    if (advanced_out) {
        video_encoder_id = config_get_string(config, "AdvOut", "Encoder");
        audio_encoder_id = config_get_string(config, "AdvOut", "AudioEncoder");
        audio_bitrate = config_get_uint(config, "AdvOut", "FFABitrate");
    } else {
        video_encoder_id = get_simple_video_encoder(config_get_string(config, "SimpleOutput", "StreamEncoder"));
        audio_encoder_id = get_simple_audio_encoder(config_get_string(config, "SimpleOutput", "StreamAudioEncoder"));
        audio_bitrate = config_get_uint(config, "SimpleOutput", "ABitrate");
    }
    obs_data_set_default_string(defaults, "audio_encoder", audio_encoder_id);
    obs_data_set_default_string(defaults, "video_encoder", video_encoder_id);
    obs_data_set_default_int(defaults, "audio_bitrate", audio_bitrate);

    obs_log(LOG_INFO, "Default settings applied.");
}

obs_properties_t *get_properties(void *data)
{
    auto filter = (filter_t *)data;

    auto props = obs_properties_create();
    obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

    // "Stream" group
    auto stream_group = obs_properties_create();
    obs_properties_add_text(stream_group, "server", obs_module_text("Server"), OBS_TEXT_DEFAULT);
    obs_properties_add_text(stream_group, "key", obs_module_text("Key"), OBS_TEXT_PASSWORD);
    obs_properties_add_group(props, "stream", obs_module_text("Stream"), OBS_GROUP_NORMAL, stream_group);

    // "Audio" gorup
    auto audio_group = obs_properties_create();
    auto audio_source_list = obs_properties_add_list(
        audio_group, "audio_source", obs_module_text("Source"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );

    obs_property_list_add_string(audio_source_list, obs_module_text("NoAudio"), "no_audio");

    obs_enum_sources(
        [](void *param, obs_source_t *source) {
            auto prop = (obs_property_t *)param;
            const uint32_t flags = obs_source_get_output_flags(source);
            if (flags & OBS_SOURCE_AUDIO) {
                obs_property_list_add_string(prop, obs_source_get_name(source), obs_source_get_uuid(source));
            }
            return true;
        },
        audio_source_list
    );

    for (int i = 1; i <= MAX_AUDIO_MIXES; i++) {
        char trackTitle[] = "MasterTrack1";
        char trackId[] = "master_track_1";

        snprintf(trackTitle, sizeof(trackTitle), "MasterTrack%d", i);
        snprintf(trackId, sizeof(trackId), "master_track_%d", i);

        obs_property_list_add_string(audio_source_list, obs_module_text(trackTitle), trackId);
    }

    obs_properties_add_group(
        props, "custom_audio_source", obs_module_text("CustomAudioSource"), OBS_GROUP_CHECKABLE, audio_group
    );

    // "Audio Encoder" group
    auto audio_encoder_group = obs_properties_create();
    auto audio_encoder_list = obs_properties_add_list(
        audio_encoder_group, "audio_encoder", obs_module_text("AudioEncoder"), OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING
    );
    // The bitrate list is empty initially.
    obs_properties_add_list(
        audio_encoder_group, "audio_bitrate", obs_module_text("AudioBitrate"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT
    );

    obs_properties_add_group(
        props, "audio_encoder_group", obs_module_text("AudioEncoder"), OBS_GROUP_NORMAL, audio_encoder_group
    );

    // "Video Encoder" group
    auto video_encoder_group = obs_properties_create();

    // "Video Encoder" prop
    auto video_encoder_list = obs_properties_add_list(
        video_encoder_group, "video_encoder", obs_module_text("VideoEncoder"), OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING
    );

    // Enum audio and video encoders
    const char *encoder_id = NULL;
    size_t i = 0;
    while (obs_enum_encoder_types(i++, &encoder_id)) {
        auto caps = obs_get_encoder_caps(encoder_id);
        if (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)) {
            // Ignore deprecated and internal
            continue;
        }

        auto name = obs_encoder_get_display_name(encoder_id);

        if (obs_get_encoder_type(encoder_id) == OBS_ENCODER_VIDEO) {
            obs_property_list_add_string(video_encoder_list, name, encoder_id);
        } else if (obs_get_encoder_type(encoder_id) == OBS_ENCODER_AUDIO) {
            obs_property_list_add_string(audio_encoder_list, name, encoder_id);
        }
    }

    obs_property_set_modified_callback2(audio_encoder_list, audio_encoder_changed, data);
    obs_property_set_modified_callback2(video_encoder_list, video_encoder_changed, data);

    // "Video Encoder Settings" group (Initially empty)
    auto video_encoder_settings_group = obs_properties_create();
    obs_properties_add_group(
        video_encoder_group, "video_encoder_settings_group", obs_module_text("VideoEncoderSettings"), OBS_GROUP_NORMAL,
        video_encoder_settings_group
    );

    obs_properties_add_group(
        props, "video_encoder_group", obs_module_text("VideoEncoder"), OBS_GROUP_NORMAL, video_encoder_group
    );

    add_apply_button(filter, props);
    add_plugin_info(props);

    return props;
}

void create_dock()
{
	auto myWidget = new QWidget();

	obs_frontend_add_dock_by_id("BranchOutputStatusDock", obs_module_text("OutputStatus"), myWidget);
}
