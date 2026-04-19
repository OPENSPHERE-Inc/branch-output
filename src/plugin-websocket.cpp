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
#include <obs.hpp>

#include <obs-websocket-api.h>

#include "plugin-support.h"
#include "plugin-websocket.hpp"

#define VENDOR_NAME "osi_branch_output"

static obs_websocket_vendor vendor = nullptr;

static void onVendorGetFilterList(obs_data_t *, obs_data_t *response, void *)
{
    proc_handler_t *ph = obs_get_proc_handler();
    calldata_t cd = {0};
    calldata_init(&cd);
    proc_handler_call(ph, "osi_branch_output_get_filter_list", &cd);
    const char *json = calldata_string(&cd, "json");

    OBSDataAutoRelease parsed = obs_data_create_from_json(json ? json : "{}");
    OBSDataArrayAutoRelease filters = obs_data_get_array(parsed, "filters");
    if (filters) {
        obs_data_set_array(response, "filters", filters);
    }
    calldata_free(&cd);
}

static void overrideFilenameFormat(obs_data_t *request, obs_data_t *response, const char *procName)
{
    const char *filterUuid = obs_data_get_string(request, "filter_uuid");
    const char *format = obs_data_get_string(request, "format");

    if (!filterUuid || !filterUuid[0]) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "filter_uuid is required");
        return;
    }

    OBSSourceAutoRelease source = obs_get_source_by_uuid(filterUuid);
    if (!source) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "Filter not found");
        return;
    }

    proc_handler_t *ph = obs_source_get_proc_handler(source);
    calldata_t cd = {0};
    calldata_init(&cd);
    calldata_set_string(&cd, "format", format ? format : "");
    proc_handler_call(ph, procName, &cd);
    calldata_free(&cd);

    obs_data_set_bool(response, "success", true);
}

static void onVendorOverrideRecordingFilenameFormat(obs_data_t *request, obs_data_t *response, void *)
{
    overrideFilenameFormat(request, response, "override_recording_filename_format");
}

static void onVendorOverrideReplayBufferFilenameFormat(obs_data_t *request, obs_data_t *response, void *)
{
    overrideFilenameFormat(request, response, "override_replay_buffer_filename_format");
}

void registerWebSocketVendorRequests()
{
    vendor = obs_websocket_register_vendor(VENDOR_NAME);
    if (!vendor) {
        obs_log(LOG_WARNING, "Failed to register obs-websocket vendor. obs-websocket not installed?");
        return;
    }

    if (!obs_websocket_vendor_register_request(vendor, "get_filter_list", onVendorGetFilterList, nullptr)) {
        obs_log(LOG_WARNING, "Failed to register vendor request: get_filter_list");
    }
    if (!obs_websocket_vendor_register_request(vendor, "override_recording_filename_format",
                                               onVendorOverrideRecordingFilenameFormat, nullptr)) {
        obs_log(LOG_WARNING, "Failed to register vendor request: override_recording_filename_format");
    }
    if (!obs_websocket_vendor_register_request(vendor, "override_replay_buffer_filename_format",
                                               onVendorOverrideReplayBufferFilenameFormat, nullptr)) {
        obs_log(LOG_WARNING, "Failed to register vendor request: override_replay_buffer_filename_format");
    }

    obs_log(LOG_INFO, "obs-websocket vendor requests registered");
}

void unregisterWebSocketVendorRequests()
{
    if (!vendor) {
        return;
    }

    obs_websocket_vendor_unregister_request(vendor, "get_filter_list");
    obs_websocket_vendor_unregister_request(vendor, "override_recording_filename_format");
    obs_websocket_vendor_unregister_request(vendor, "override_replay_buffer_filename_format");
    vendor = nullptr;
}
