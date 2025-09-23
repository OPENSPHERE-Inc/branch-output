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
#include <util/platform.h>
#include <obs.hpp>

#include <QMainWindow>

#include "plugin-support.h"
#include "plugin-main.hpp"

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

// Imitate obs-studio/UI/window-basic-settings.cpp
inline QString makeFormatToolTip()
{
    static const char *format_list[][2] = {
        {"1", "FilenameFormatting.TT.1"},       {"2", "FilenameFormatting.TT.2"},
        {"CCYY", "FilenameFormatting.TT.CCYY"}, {"YY", "FilenameFormatting.TT.YY"},
        {"MM", "FilenameFormatting.TT.MM"},     {"DD", "FilenameFormatting.TT.DD"},
        {"hh", "FilenameFormatting.TT.hh"},     {"mm", "FilenameFormatting.TT.mm"},
        {"ss", "FilenameFormatting.TT.ss"},     {"%", "FilenameFormatting.TT.Percent"},
        {"a", "FilenameFormatting.TT.a"},       {"A", "FilenameFormatting.TT.A"},
        {"b", "FilenameFormatting.TT.b"},       {"B", "FilenameFormatting.TT.B"},
        {"d", "FilenameFormatting.TT.d"},       {"H", "FilenameFormatting.TT.H"},
        {"I", "FilenameFormatting.TT.I"},       {"m", "FilenameFormatting.TT.m"},
        {"M", "FilenameFormatting.TT.M"},       {"p", "FilenameFormatting.TT.p"},
        {"s", "FilenameFormatting.TT.s"},       {"S", "FilenameFormatting.TT.S"},
        {"y", "FilenameFormatting.TT.y"},       {"Y", "FilenameFormatting.TT.Y"},
        {"z", "FilenameFormatting.TT.z"},       {"Z", "FilenameFormatting.TT.Z"},
        {"FPS", "FilenameFormatting.TT.FPS"},   {"CRES", "FilenameFormatting.TT.CRES"},
        {"ORES", "FilenameFormatting.TT.ORES"}, {"VF", "FilenameFormatting.TT.VF"},
    };

    QString html = "<table>";

    for (auto f : format_list) {
        html += "<tr><th align='left'>%";
        html += f[0];
        html += "</th><td>";
        html += QTStr(f[1]);
        html += "</td></tr>";
    }

    html += "</table>";
    return html;
}

//--- BranchOutputFiilter class ---//

void BranchOutputFilter::getDefaults(obs_data_t *defaults)
{
    obs_log(LOG_DEBUG, "Default settings applying.");

    auto config = obs_frontend_get_profile_config();

    const char *videoEncoderId;
    const char *audioEncoderId;
    uint64_t audioBitrate;
    const char *recFormat;
    bool recSplitFile = false;
    const char *recSplitFileType = "Time";
    uint64_t recSplitFileTimeMins = 15;
    uint64_t recSplitFileSizeMb = 2048;
    bool fileNameWithoutSpace = true;
    const char *mux;

    // Capture values from OBS settings
    if (isAdvancedMode(config)) {
        videoEncoderId = config_get_string(config, "AdvOut", "Encoder");
        audioEncoderId = config_get_string(config, "AdvOut", "AudioEncoder");
        audioBitrate = config_get_uint(config, "AdvOut", "FFABitrate");
        recFormat = config_get_string(config, "AdvOut", "RecFormat2");
        recSplitFile = config_get_bool(config, "AdvOut", "RecSplitFile");
        recSplitFileTimeMins = config_get_uint(config, "AdvOut", "RecSplitFileTime");
        recSplitFileSizeMb = config_get_uint(config, "AdvOut", "RecSplitFileSize");
        fileNameWithoutSpace = config_get_bool(config, "AdvOut", "RecFileNameWithoutSpace");
        mux = config_get_string(config, "AdvOut", "RecMuxerCustom");
    } else {
        videoEncoderId = getSimpleVideoEncoder(config_get_string(config, "SimpleOutput", "StreamEncoder"));
        audioEncoderId = getSimpleAudioEncoder(config_get_string(config, "SimpleOutput", "StreamAudioEncoder"));
        audioBitrate = config_get_uint(config, "SimpleOutput", "ABitrate");
        recFormat = config_get_string(config, "SimpleOutput", "RecFormat2");
        fileNameWithoutSpace = config_get_bool(config, "SimpleOutput", "FileNameWithoutSpace");
        mux = config_get_string(config, "SimpleOutput", "MuxerCustom");
    }

    const char *splitFileValue = "";
    if (recSplitFile && strcmp(recSplitFileType, "Manual")) {
        if (!strcmp(recSplitFileType, "Size")) {
            splitFileValue = "by_size";
        } else if (!strcmp(recSplitFileType, "Time")) {
            splitFileValue = "by_time";
        } else {
            splitFileValue = "manual";
        }
    }
    obs_data_set_default_string(defaults, "split_file", splitFileValue);

    obs_data_set_default_string(defaults, "audio_encoder", audioEncoderId);
    obs_data_set_default_string(defaults, "video_encoder", videoEncoderId);
    obs_data_set_default_int(defaults, "audio_bitrate", audioBitrate);
    obs_data_set_default_bool(defaults, "stream_recording", false);
    obs_data_set_default_bool(defaults, "use_profile_recording_path", false);
    obs_data_set_default_string(defaults, "audio_source", "master_track");
    obs_data_set_default_int(defaults, "audio_track", 1);
    obs_data_set_default_string(defaults, "audio_dest", "both");
    obs_data_set_default_string(defaults, "audio_source_2", "disabled");
    obs_data_set_default_int(defaults, "audio_track_2", 1);
    obs_data_set_default_string(defaults, "audio_dest_2", "both");
    obs_data_set_default_string(defaults, "audio_source_3", "disabled");
    obs_data_set_default_int(defaults, "audio_track_3", 1);
    obs_data_set_default_string(defaults, "audio_dest_3", "both");
    obs_data_set_default_string(defaults, "audio_source_4", "disabled");
    obs_data_set_default_int(defaults, "audio_track_4", 1);
    obs_data_set_default_string(defaults, "audio_dest_4", "both");
    obs_data_set_default_string(defaults, "audio_source_5", "disabled");
    obs_data_set_default_int(defaults, "audio_track_5", 1);
    obs_data_set_default_string(defaults, "audio_dest_5", "both");
    obs_data_set_default_string(defaults, "audio_source_6", "disabled");
    obs_data_set_default_int(defaults, "audio_track_6", 1);
    obs_data_set_default_string(defaults, "audio_dest_6", "both");
    obs_data_set_default_int(defaults, "custom_width", config_get_int(config, "Video", "OutputCX"));
    obs_data_set_default_int(defaults, "custom_height", config_get_int(config, "Video", "OutputCY"));
    obs_data_set_default_bool(defaults, "no_space_filename", fileNameWithoutSpace);
    obs_data_set_default_string(defaults, "rec_format", recFormat);
    obs_data_set_default_int(defaults, "split_file_time_mins", recSplitFileTimeMins);
    obs_data_set_default_int(defaults, "split_file_size_mb", recSplitFileSizeMb);
    obs_data_set_default_bool(defaults, "keep_output_base_resolution", false);
    obs_data_set_default_bool(defaults, "suspend_recording_when_source_collapsed", false);
    obs_data_set_default_string(defaults, "rec_muxer_custom", mux);

    auto path = getProfileRecordingPath(config);
    obs_data_set_default_string(defaults, "path", path);

    // Override filename_formatting
    auto filenameFormatting = QString("%1 %2 ") + QString(config_get_string(config, "Output", "FilenameFormatting"));
    obs_data_set_default_string(defaults, "filename_formatting", qUtf8Printable(filenameFormatting));

    obs_log(LOG_INFO, "Default settings applied.");
}

void BranchOutputFilter::addApplyButton(obs_properties_t *props, const char *propName)
{
    obs_properties_add_button2(
        props, propName, obs_module_text("Apply"),
        [](obs_properties_t *, obs_property_t *, void *param) {
            auto filter = static_cast<BranchOutputFilter *>(param);

            // Force filter activation
            filter->initialized = true;

            OBSDataAutoRelease settings = obs_source_get_settings(filter->filterSource);
            filter->updateCallback(settings);

            return true;
        },
        this
    );
}

void BranchOutputFilter::addPluginInfo(obs_properties_t *props)
{
    char plugin_info_format[] = "<a href=\"https://github.com/OPENSPHERE-Inc/branch-output\">Branch Output</a> (v%s) "
                                "developed by <a href=\"https://opensphere.co.jp\">OPENSPHERE Inc.</a>";

    size_t buffer_size = sizeof(plugin_info_format) + strlen(PLUGIN_VERSION);
    auto plugin_info_text = (char *)bzalloc(buffer_size);
    snprintf(plugin_info_text, buffer_size, plugin_info_format, PLUGIN_VERSION);

    obs_properties_add_text(props, "plugin_info", plugin_info_text, OBS_TEXT_INFO);

    bfree(plugin_info_text);
}

// index 0: No prefix on prop names (for compatibility)
// index 1~: "service_x." prefix on prop names
void BranchOutputFilter::createServiceProperties(obs_properties_t *props, size_t index, bool visible)
{
    QString propNameFormat = getIndexedPropNameFormat(index);

    // Add gap line
    auto gap = obs_properties_add_text(props, qUtf8Printable(propNameFormat.arg("service_group")), "", OBS_TEXT_INFO);
    obs_property_set_visible(gap, visible);

    auto server = obs_properties_add_text(
        props, qUtf8Printable(propNameFormat.arg("server")), qUtf8Printable(QTStr("Server%1").arg(index + 1)),
        OBS_TEXT_DEFAULT
    );
    obs_property_set_visible(server, visible);

    auto key = obs_properties_add_text(
        props, qUtf8Printable(propNameFormat.arg("key")), obs_module_text("Key"), OBS_TEXT_PASSWORD
    );
    obs_property_set_visible(key, visible);

    auto useAuth = obs_properties_add_bool(
        props, qUtf8Printable(propNameFormat.arg("use_auth")), obs_module_text("UseAuthentication")
    );
    obs_property_set_visible(useAuth, visible);

    auto username = obs_properties_add_text(
        props, qUtf8Printable(propNameFormat.arg("username")), obs_module_text("Username"), OBS_TEXT_DEFAULT
    );
    obs_property_set_visible(username, false);

    auto password = obs_properties_add_text(
        props, qUtf8Printable(propNameFormat.arg("password")), obs_module_text("Password"), OBS_TEXT_PASSWORD
    );
    obs_property_set_visible(password, false);

    obs_property_set_modified_callback2(
        useAuth,
        [](void *, obs_properties_t *_props, obs_property_t *_prop, obs_data_t *settings) {
            size_t _index = 0;
            auto useAuthPropName = obs_property_name(_prop);
            sscanf(useAuthPropName, "use_auth_%zu", &_index);

            auto _useAuth = obs_data_get_bool(settings, useAuthPropName);
            auto count = (size_t)obs_data_get_int(settings, "service_count");

            QString _propNameFormat = getIndexedPropNameFormat(_index);
            obs_property_set_visible(
                obs_properties_get(_props, qUtf8Printable(_propNameFormat.arg("username"))), _useAuth && _index < count
            );
            obs_property_set_visible(
                obs_properties_get(_props, qUtf8Printable(_propNameFormat.arg("password"))), _useAuth && _index < count
            );
            return true;
        },
        nullptr
    );
}

void BranchOutputFilter::addServices(obs_properties_t *props)
{
    auto serviceCountList = obs_properties_add_list(
        props, "service_count", obs_module_text("ServiceCount"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT
    );

    for (int n = 1; n <= MAX_SERVICES; n++) {
        obs_property_list_add_int(serviceCountList, qUtf8Printable(QString("%1").arg(n)), n);
    }

    createServiceProperties(props, 0);
    for (size_t i = 1; i < MAX_SERVICES; i++) {
        createServiceProperties(props, i, false);
    }

    obs_property_set_modified_callback2(
        serviceCountList,
        [](void *, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
            auto count = obs_data_get_int(settings, "service_count");

            for (int i = 0; i < MAX_SERVICES; i++) {
                QString propNameFormat = getIndexedPropNameFormat(i);
                auto useAuth = obs_data_get_bool(settings, qUtf8Printable(propNameFormat.arg("use_auth")));

                obs_property_set_visible(
                    obs_properties_get(_props, qUtf8Printable(propNameFormat.arg("service_group"))), i < count
                );
                obs_property_set_visible(
                    obs_properties_get(_props, qUtf8Printable(propNameFormat.arg("server"))), i < count
                );
                obs_property_set_visible(
                    obs_properties_get(_props, qUtf8Printable(propNameFormat.arg("key"))), i < count
                );
                obs_property_set_visible(
                    obs_properties_get(_props, qUtf8Printable(propNameFormat.arg("use_auth"))), i < count
                );
                obs_property_set_visible(
                    obs_properties_get(_props, qUtf8Printable(propNameFormat.arg("username"))), useAuth && i < count
                );
                obs_property_set_visible(
                    obs_properties_get(_props, qUtf8Printable(propNameFormat.arg("password"))), useAuth && i < count
                );
            }

            return true;
        },
        nullptr
    );
}

void BranchOutputFilter::addStreamGroup(obs_properties_t *props)
{
    auto streamGroup = obs_properties_create();

    // Add multi services properties
    addServices(streamGroup);

    // Add gap line
    obs_properties_add_text(streamGroup, "stream_recording_group", "", OBS_TEXT_INFO);

    auto streamRecording = obs_properties_add_bool(streamGroup, "stream_recording", obs_module_text("StreamRecording"));

    auto streamRecordingChangeHandler = [](void *, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
        auto _streamRecording = obs_data_get_bool(settings, "stream_recording");
        obs_property_set_visible(obs_properties_get(_props, "use_profile_recording_path"), _streamRecording);
        obs_property_set_visible(obs_properties_get(_props, "path"), _streamRecording);
        obs_property_set_visible(obs_properties_get(_props, "no_space_filename"), _streamRecording);
        obs_property_set_visible(obs_properties_get(_props, "filename_formatting"), _streamRecording);
        obs_property_set_visible(obs_properties_get(_props, "rec_format"), _streamRecording);
        obs_property_set_visible(obs_properties_get(_props, "split_file"), _streamRecording);
        obs_property_set_visible(obs_properties_get(_props, "rec_muxer_custom"), _streamRecording);
        obs_property_set_visible(
            obs_properties_get(_props, "suspend_recording_when_source_collapsed"), _streamRecording
        );

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
    auto useProfilePath =
        obs_properties_add_bool(streamGroup, "use_profile_recording_path", obs_module_text("UseProfileRecordingPath"));

    auto useProfilePathChangeHandler = [](void *, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
        auto _useProfilePath = obs_data_get_bool(settings, "use_profile_recording_path");
        obs_property_set_enabled(obs_properties_get(_props, "path"), !_useProfilePath);
        return true;
    };
    obs_property_set_modified_callback2(useProfilePath, useProfilePathChangeHandler, nullptr);

    obs_properties_add_path(streamGroup, "path", obs_module_text("Path"), OBS_PATH_DIRECTORY, nullptr, nullptr);
    auto filenameFormatting = obs_properties_add_text(
        streamGroup, "filename_formatting", obs_module_text("FilenameFormatting"), OBS_TEXT_DEFAULT
    );
    obs_property_set_long_description(filenameFormatting, qUtf8Printable(makeFormatToolTip()));
    obs_properties_add_bool(streamGroup, "no_space_filename", obs_module_text("NoSpaceFileName"));

    // Only support limited formats
    auto fileFormatList = obs_properties_add_list(
        streamGroup, "rec_format", obs_module_text("VideoFormat"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(fileFormatList, obs_module_text("MKV"), "mkv");
    obs_property_list_add_string(fileFormatList, obs_module_text("MP4"), "mp4");
    obs_property_list_add_string(fileFormatList, obs_module_text("fMP4"), "fragmented_mp4");
    obs_property_list_add_string(fileFormatList, obs_module_text("hMP4"), "hybrid_mp4"); // beta
    obs_property_list_add_string(fileFormatList, obs_module_text("MOV"), "mov");
    obs_property_list_add_string(fileFormatList, obs_module_text("fMOV"), "fragmented_mov");
    obs_property_list_add_string(fileFormatList, obs_module_text("TS"), "mpegts");
    obs_property_set_long_description(fileFormatList, obs_module_text("VideoFormatNote"));

    auto splitFileList = obs_properties_add_list(
        streamGroup, "split_file", obs_module_text("SplitFile"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(splitFileList, obs_module_text("SplitFile.NoSplit"), "");
    obs_property_list_add_string(splitFileList, obs_module_text("SplitFile.ByTime"), "by_time");
    obs_property_list_add_string(splitFileList, obs_module_text("SplitFile.BySize"), "by_size");
    obs_property_list_add_string(splitFileList, obs_module_text("SplitFile.Manual"), "manual");
    obs_property_set_long_description(splitFileList, obs_module_text("SplitFileNote"));

    obs_property_set_modified_callback2(splitFileList, streamRecordingChangeHandler, nullptr);

    obs_properties_add_int(streamGroup, "split_file_time_mins", obs_module_text("SplitFile.Time"), 1, 525600, 1);
    obs_properties_add_int(streamGroup, "split_file_size_mb", obs_module_text("SplitFile.Size"), 1, 1073741824, 1);

    // Mux custom setting
    obs_properties_add_text(streamGroup, "rec_muxer_custom", obs_module_text("CustomMuxerSettings"), OBS_TEXT_DEFAULT);

    // Pausing settings
    auto suspendRecordingWhenSourceCollapsed = obs_properties_add_bool(
        streamGroup, "suspend_recording_when_source_collapsed", obs_module_text("SuspendRecordingWhenSourceCollapsed")
    );
    obs_property_set_long_description(
        suspendRecordingWhenSourceCollapsed, obs_module_text("SuspendRecordingWhenSourceCollapsedNote")
    );

    // Source resolution trackability
    auto keepOutputBaseResolution = obs_properties_add_bool(
        streamGroup, "keep_output_base_resolution", obs_module_text("KeepOutputBaseResolution")
    );
    obs_property_set_long_description(keepOutputBaseResolution, obs_module_text("KeepOutputBaseResolutionNote"));

    obs_properties_add_group(props, "stream", obs_module_text("Stream"), OBS_GROUP_NORMAL, streamGroup);
}

void BranchOutputFilter::createAudioTrackProperties(obs_properties_t *audioGroup, size_t track, bool visible)
{
    auto propNameFormat = getIndexedPropNameFormat(track, 1);

    if (track > 1) {
        // Add gap line (Except track 1)
        obs_properties_add_text(
            audioGroup, qUtf8Printable(propNameFormat.arg("multitrack_audio_group")), "", OBS_TEXT_INFO
        );
    }

    auto audioSourceList = obs_properties_add_list(
        audioGroup, qUtf8Printable(propNameFormat.arg("audio_source")),
        qUtf8Printable(QTStr("TrackSource%1").arg(track)), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_set_visible(audioSourceList, visible);
    obs_property_set_long_description(audioSourceList, obs_module_text("AudioSourceNote"));

    if (track > 1) {
        // Upper tracks can be disabled individually
        obs_property_list_add_string(audioSourceList, obs_module_text("TrackDisabled"), "disabled");
    }
    obs_property_list_add_string(audioSourceList, obs_module_text("NoAudio"), "no_audio");
    obs_property_list_add_string(audioSourceList, obs_module_text("MasterTrack"), "master_track");
    obs_property_list_add_string(audioSourceList, obs_module_text("FilterAudio"), "filter");
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
        [](void *, obs_properties_t *_props, obs_property_t *_audioSourceList, obs_data_t *settings) {
            size_t _track = 1;
            auto _audioSourceListName = obs_property_name(_audioSourceList);
            sscanf(_audioSourceListName, "audio_source_%zu", &_track);

            auto _propNameFormat = getIndexedPropNameFormat(_track, 1);

            auto audioSource = obs_data_get_string(settings, _audioSourceListName);
            obs_property_set_enabled(
                obs_properties_get(_props, qUtf8Printable(_propNameFormat.arg("audio_track"))),
                !strcmp(audioSource, "master_track")
            );
            obs_property_set_enabled(
                obs_properties_get(_props, qUtf8Printable(_propNameFormat.arg("audio_dest"))),
                !!strcmp(audioSource, "disabled")
            );

            return true;
        },
        nullptr
    );

    auto audioTrackList = obs_properties_add_list(
        audioGroup, qUtf8Printable(propNameFormat.arg("audio_track")), obs_module_text("Track"), OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_INT
    );
    for (int i = 1; i <= MAX_AUDIO_MIXES; i++) {
        char trackNo[] = "Track1";
        snprintf(trackNo, sizeof(trackNo), "Track%d", i);
        obs_property_list_add_int(audioTrackList, obs_module_text(trackNo), i);
    }
    obs_property_set_enabled(audioTrackList, false); // Initially disabled
    obs_property_set_visible(audioTrackList, visible);

    auto audioDestList = obs_properties_add_list(
        audioGroup, qUtf8Printable(propNameFormat.arg("audio_dest")), obs_module_text("AudioDestination"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(audioDestList, obs_module_text("StreamingAndRecording"), "both");
    obs_property_list_add_string(audioDestList, obs_module_text("Streaming"), "streaming");
    obs_property_list_add_string(audioDestList, obs_module_text("Recording"), "recording");
    obs_property_set_enabled(audioDestList, false); // Initially disabled
    obs_property_set_visible(audioDestList, visible);
}

void BranchOutputFilter::addAudioGroup(obs_properties_t *props)
{
    auto audioGroup = obs_properties_create();
    createAudioTrackProperties(audioGroup, 1);

    auto multitrackAudio = obs_properties_add_bool(audioGroup, "multitrack_audio", obs_module_text("MultitrackAudio"));

    for (size_t track = 2; track <= MAX_AUDIO_MIXES; track++) {
        createAudioTrackProperties(audioGroup, track, false);
    }

    obs_property_set_modified_callback2(
        multitrackAudio,
        [](void *, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
            auto _multitrackAudio = obs_data_get_bool(settings, "multitrack_audio");

            for (size_t track = 1; track <= MAX_AUDIO_MIXES; track++) {
                auto propNameFormat = getIndexedPropNameFormat(track, 1);

                if (track > 1) {
                    obs_property_set_visible(
                        obs_properties_get(_props, qUtf8Printable(propNameFormat.arg("multitrack_audio_group"))),
                        _multitrackAudio
                    );
                    obs_property_set_visible(
                        obs_properties_get(_props, qUtf8Printable(propNameFormat.arg("audio_source"))), _multitrackAudio
                    );
                    obs_property_set_visible(
                        obs_properties_get(_props, qUtf8Printable(propNameFormat.arg("audio_track"))), _multitrackAudio
                    );
                }

                obs_property_set_visible(
                    obs_properties_get(_props, qUtf8Printable(propNameFormat.arg("audio_dest"))), _multitrackAudio
                );
            }

            return true;
        },
        nullptr
    );

    obs_properties_add_group(
        props, "custom_audio_source", obs_module_text("CustomAudioSource"), OBS_GROUP_CHECKABLE, audioGroup
    );
}

void BranchOutputFilter::addAudioEncoderGroup(obs_properties_t *props)
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

        auto encoderName = obs_encoder_get_display_name(encoderId);

        if (obs_get_encoder_type(encoderId) == OBS_ENCODER_AUDIO) {
            obs_property_list_add_string(audioEncoderList, encoderName, encoderId);
        }
    }

    obs_properties_add_group(
        props, "audio_encoder_group", obs_module_text("AudioEncoder"), OBS_GROUP_NORMAL, audioEncoderGroup
    );

    obs_property_set_modified_callback2(
        audioEncoderList,
        [](void *param, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
            auto filter = static_cast<BranchOutputFilter *>(param);
            obs_log(LOG_DEBUG, "%s: Audio encoder chainging.", qUtf8Printable(filter->name));

            const auto encoder_id = obs_data_get_string(settings, "audio_encoder");
            const OBSProperties encoder_props = obs_get_encoder_properties(encoder_id);
            const auto encoder_bitrate_prop = obs_properties_get(encoder_props, "bitrate");

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
                        LOG_ERROR, "%s: Invalid bitrate property given by encoder: %s", qUtf8Printable(filter->name),
                        encoder_id
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

            obs_log(LOG_INFO, "%s: Audio encoder changed.", qUtf8Printable(filter->name));
            return result;
        },
        this
    );
}

void BranchOutputFilter::addVideoEncoderGroup(obs_properties_t *props)
{
    auto videoEncoderGroup = obs_properties_create();

    // Resolution prop
    auto resolutionList = obs_properties_add_list(
        videoEncoderGroup, "resolution", obs_module_text("Resolution"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(resolutionList, obs_module_text("Resolution.Source"), "");
    obs_property_list_add_string(resolutionList, obs_module_text("Resolution.Output"), "output");
    obs_property_list_add_string(resolutionList, obs_module_text("Resolution.Canvas"), "canvas");
    obs_property_list_add_string(resolutionList, obs_module_text("Resolution.ThreeQuarters"), "three_quarters");
    obs_property_list_add_string(resolutionList, obs_module_text("Resolution.Half"), "half");
    obs_property_list_add_string(resolutionList, obs_module_text("Resolution.Quarter"), "quarter");
    obs_property_list_add_string(resolutionList, obs_module_text("Resolution.Custom"), "custom");

    obs_property_set_modified_callback2(
        resolutionList,
        [](void *, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
            auto resolution = obs_data_get_string(settings, "resolution");

            auto _customResolutionGroup = obs_properties_get(_props, "custom_resolution_group");
            obs_property_set_visible(_customResolutionGroup, !strcmp(resolution, "custom"));

            auto _downscaleFilterList = obs_properties_get(_props, "downscale_filter");
            obs_property_set_enabled(_downscaleFilterList, strlen(resolution) != 0);

            return true;
        },
        nullptr
    );

    auto customResolutionGroup = obs_properties_create();
    obs_properties_add_int(customResolutionGroup, "custom_width", obs_module_text("Width"), 2, 8192, 2);
    obs_properties_add_int(customResolutionGroup, "custom_height", obs_module_text("Height"), 2, 8192, 2);

    obs_properties_add_group(
        videoEncoderGroup, "custom_resolution_group", obs_module_text("CustomResolution"), OBS_GROUP_NORMAL,
        customResolutionGroup
    );

    // "Downscale Filter" prop
    auto downscaleFilterList = obs_properties_add_list(
        videoEncoderGroup, "downscale_filter", obs_module_text("DownscaleFilter"), OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(downscaleFilterList, obs_module_text("DownscaleFilter.Global"), "");
    obs_property_list_add_string(downscaleFilterList, obs_module_text("DownscaleFilter.Bilinear"), "bilinear");
    obs_property_list_add_string(downscaleFilterList, obs_module_text("DownscaleFilter.Area"), "area");
    obs_property_list_add_string(downscaleFilterList, obs_module_text("DownscaleFilter.Bicubic"), "bicubic");
    obs_property_list_add_string(downscaleFilterList, obs_module_text("DownscaleFilter.Lanczos"), "lanczos");

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

        auto encoderName = obs_encoder_get_display_name(encoderId);

        if (obs_get_encoder_type(encoderId) == OBS_ENCODER_VIDEO) {
            obs_property_list_add_string(videoEncoderList, encoderName, encoderId);
        }
    }

    obs_property_set_modified_callback2(
        videoEncoderList,
        [](void *param, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
            auto filter = static_cast<BranchOutputFilter *>(param);
            obs_log(LOG_DEBUG, "%s: Video encoder chainging.", qUtf8Printable(filter->name));

            auto _videoEncoderGroup = obs_property_group_content(obs_properties_get(_props, "video_encoder_group"));
            auto _encoderId = obs_data_get_string(settings, "video_encoder");

            // Apply encoder's defaults
            OBSDataAutoRelease encoderEefaults = obs_encoder_defaults(_encoderId);
            applyDefaults(settings, encoderEefaults);

            obs_properties_remove_by_name(_videoEncoderGroup, "video_encoder_settings_group");

            auto encoderProps = obs_get_encoder_properties(_encoderId);
            if (encoderProps) {
                obs_properties_add_group(
                    _videoEncoderGroup, "video_encoder_settings_group", obs_encoder_get_display_name(_encoderId),
                    OBS_GROUP_NORMAL, encoderProps
                );

                // Do not apply to _videoEncoderGroup because it will cause memoryleak.
                obs_properties_apply_settings(encoderProps, settings);
            }

            obs_log(LOG_INFO, "%s: Video encoder changed.", qUtf8Printable(filter->name));
            return true;
        },
        this
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

obs_properties_t *BranchOutputFilter::getProperties()
{
    auto props = obs_properties_create();
    obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

    //--- "Stream" group ---//
    addStreamGroup(props);

    addApplyButton(props, "apply1");

    //--- "Audio" gorup ---//
    addAudioGroup(props);

    //--- "Audio Encoder" group ---//
    addAudioEncoderGroup(props);

    //--- "Video Encoder" group ---//
    addVideoEncoderGroup(props);

    addApplyButton(props, "apply2");
    addPluginInfo(props);

    return props;
}

BranchOutputStatusDock *BranchOutputFilter::createOutputStatusDock()
{
    auto mainWindow = (QMainWindow *)obs_frontend_get_main_window();
    if (!mainWindow) {
        return nullptr;
    }

    auto dock = new BranchOutputStatusDock(mainWindow);
    obs_frontend_add_dock_by_id("BranchOutputStatusDock", obs_module_text("BranchOutputStatus"), dock);
    return dock;
}
