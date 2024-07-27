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

#include <obs-module.h>
#include <plugin-support.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include "plugin-main.hpp"

bool encoder_available(const char *encoder)
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

void add_apply_button(obs_properties_t *props)
{
	obs_properties_add_button(
		props, "apply", obs_module_text("Apply"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			auto filter = (filter_t *)data;

			// Force filter activation
			filter->filter_active = true;

			auto settings = obs_source_get_settings(filter->source);
			update(filter, settings);
			obs_data_release(settings);
			return true;
		});
}

bool encoder_changed(void *, obs_properties_t *props, obs_property_t *,
		     obs_data_t *settings)
{
	obs_properties_remove_by_name(props, "encoder_group");
	obs_properties_remove_by_name(props, "apply");

	const char *encoder_id = obs_data_get_string(settings, "encoder");
	;
	obs_properties_t *encoder_props =
		obs_get_encoder_properties(encoder_id);
	if (encoder_props) {
		obs_properties_add_group(
			props, "encoder_group",
			obs_encoder_get_display_name(encoder_id),
			OBS_GROUP_NORMAL, encoder_props);
	}

	add_apply_button(props);

	return true;
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
const char *get_simple_output_encoder(const char *encoder)
{
	if (strcmp(encoder, SIMPLE_ENCODER_X264) == 0) {
		return "obs_x264";
	} else if (strcmp(encoder, SIMPLE_ENCODER_X264_LOWCPU) == 0) {
		return "obs_x264";
	} else if (strcmp(encoder, SIMPLE_ENCODER_QSV) == 0) {
		return "obs_qsv11_v2";
	} else if (strcmp(encoder, SIMPLE_ENCODER_QSV_AV1) == 0) {
		return "obs_qsv11_av1";
	} else if (strcmp(encoder, SIMPLE_ENCODER_AMD) == 0) {
		return "h264_texture_amf";
	} else if (strcmp(encoder, SIMPLE_ENCODER_AMD_HEVC) == 0) {
		return "h265_texture_amf";
	} else if (strcmp(encoder, SIMPLE_ENCODER_AMD_AV1) == 0) {
		return "av1_texture_amf";
	} else if (strcmp(encoder, SIMPLE_ENCODER_NVENC) == 0) {
		return encoder_available("jim_nvenc") ? "jim_nvenc"
						      : "ffmpeg_nvenc";
	} else if (strcmp(encoder, SIMPLE_ENCODER_NVENC_HEVC) == 0) {
		return encoder_available("jim_hevc_nvenc")
			       ? "jim_hevc_nvenc"
			       : "ffmpeg_hevc_nvenc";
	} else if (strcmp(encoder, SIMPLE_ENCODER_NVENC_AV1) == 0) {
		return "jim_av1_nvenc";
	} else if (strcmp(encoder, SIMPLE_ENCODER_APPLE_H264) == 0) {
		return "com.apple.videotoolbox.videoencoder.ave.avc";
	} else if (strcmp(encoder, SIMPLE_ENCODER_APPLE_HEVC) == 0) {
		return "com.apple.videotoolbox.videoencoder.ave.hevc";
	}

	return "obs_x264";
}

void apply_defaults(obs_data_t *dest, obs_data_t *src)
{
	for (auto item = obs_data_first(src); item; obs_data_item_next(&item)) {
		auto name = obs_data_item_get_name(item);
		auto type = obs_data_item_gettype(item);

		switch (type) {
		case OBS_DATA_STRING:
			obs_data_set_default_string(
				dest, name, obs_data_item_get_string(item));
			break;
		case OBS_DATA_NUMBER: {
			auto numtype = obs_data_item_numtype(item);
			if (numtype == OBS_DATA_NUM_DOUBLE) {
				obs_data_set_default_double(
					dest, name,
					obs_data_item_get_double(item));
			} else if (numtype == OBS_DATA_NUM_INT) {
				obs_data_set_default_int(
					dest, name,
					obs_data_item_get_int(item));
			}
		} break;
		case OBS_DATA_BOOLEAN:
			obs_data_set_default_bool(dest, name,
						  obs_data_item_get_bool(item));
			break;
		case OBS_DATA_OBJECT: {
			auto value = obs_data_item_get_obj(item);
			obs_data_set_default_obj(dest, name, value);
			obs_data_release(value);
		} break;
		case OBS_DATA_ARRAY: {
			auto value = obs_data_item_get_array(item);
			obs_data_set_default_array(dest, name, value);
			obs_data_array_release(value);
		}

		break;
		case OBS_DATA_NULL:
			break;
		}
	}
}

void get_defaults(obs_data_t *defaults)
{
	auto config = obs_frontend_get_profile_config();
	auto mode = config_get_string(config, "Output", "Mode");
	bool advanced_out = strcmp(mode, "Advanced") == 0 ||
			    strcmp(mode, "advanced");

	const char *encoder_id;
	if (advanced_out) {
		encoder_id = config_get_string(config, "AdvOut", "Encoder");
	} else {
		encoder_id = get_simple_output_encoder(config_get_string(
			config, "SimpleOutput", "StreamEncoder"));
	}
	obs_data_set_default_string(defaults, "encoder", encoder_id);

	// Load recent.json and apply to defaults
	auto path = obs_module_get_config_path(obs_current_module(),
					       SETTINGS_JSON_NAME);
	auto recently_settings = obs_data_create_from_json_file(path);

	if (recently_settings) {
		obs_data_erase(recently_settings, "server");
		obs_data_erase(recently_settings, "key");
		obs_data_erase(recently_settings, "custom_audio_source");
		obs_data_erase(recently_settings, "audio_source");
		apply_defaults(defaults, recently_settings);
	}

	bfree(path);
	obs_data_release(recently_settings);
}

obs_properties_t *get_properties(void *data)
{
	auto props = obs_properties_create();
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	// "Stream" group
	auto stream_group = obs_properties_create();
	obs_properties_add_text(stream_group, "server",
				obs_module_text("Server"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(stream_group, "key", obs_module_text("Key"),
				OBS_TEXT_PASSWORD);
	obs_properties_add_group(props, "stream", obs_module_text("Stream"),
				 OBS_GROUP_NORMAL, stream_group);

	// "Audio" gorup
	auto audio_group = obs_properties_create();
	auto audio_source_list = obs_properties_add_list(
		audio_group, "audio_source", obs_module_text("Source"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(audio_source_list,
				     obs_module_text("NoAudio"), "no_audio");

	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto prop = (obs_property_t *)param;
			const uint32_t flags =
				obs_source_get_output_flags(source);
			if (flags & OBS_SOURCE_AUDIO) {
				obs_property_list_add_string(
					prop, obs_source_get_name(source),
					obs_source_get_uuid(source));
			}
			return true;
		},
		audio_source_list);

	obs_properties_add_group(props, "custom_audio_source",
				 obs_module_text("CustomAudioSource"),
				 OBS_GROUP_CHECKABLE, audio_group);

	// "Encoder" prop
	auto encoder_prop = obs_properties_add_list(props, "encoder",
						    obs_module_text("Encoder"),
						    OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_STRING);

	const char *encoder_id = NULL;
	size_t i = 0;
	while (obs_enum_encoder_types(i++, &encoder_id)) {
		if (obs_get_encoder_type(encoder_id) != OBS_ENCODER_VIDEO) {
			continue;
		}

		auto caps = obs_get_encoder_caps(encoder_id);
		if ((caps & (OBS_ENCODER_CAP_DEPRECATED |
			     OBS_ENCODER_CAP_INTERNAL)) != 0) {
			continue;
		}

		auto name = obs_encoder_get_display_name(encoder_id);
		obs_property_list_add_string(encoder_prop, name, encoder_id);
	}
	obs_property_set_modified_callback2(encoder_prop, encoder_changed,
					    data);

	// "Encoder" group (Initially empty)
	auto encoder_group = obs_properties_create();
	obs_properties_add_group(props, "encoder_group",
				 obs_module_text("Encoder"), OBS_GROUP_NORMAL,
				 encoder_group);

	add_apply_button(props);

	return props;
}
