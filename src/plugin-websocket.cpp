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

#include <cstring>

#include "plugin-support.h"
#include "plugin-websocket.hpp"

#define VENDOR_NAME "osi_branch_output"
// Must match FILTER_ID in plugin-main.cpp.
#define BRANCH_OUTPUT_FILTER_ID "osi_branch_output"

// DoS guard: recording filename templates are typically <100 chars.
// Reject pathologically long inputs before they reach libobs's path expander.
static constexpr size_t MAX_FORMAT_LENGTH = 1024;

static obs_websocket_vendor vendor = nullptr;

static void onVendorGetFilterList(obs_data_t *, obs_data_t *response, void *)
{
    proc_handler_t *ph = obs_get_proc_handler();
    calldata_t cd = {0};
    calldata_init(&cd);
    bool ok = proc_handler_call(ph, "osi_branch_output_get_filter_list", &cd);
    const char *json = calldata_string(&cd, "json");

    OBSDataAutoRelease parsed = ok ? obs_data_create_from_json(json ? json : "{}") : nullptr;
    calldata_free(&cd);

    if (!ok) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "Internal error: filter list proc handler unavailable");
        return;
    }

    if (!parsed) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "Internal error: failed to parse filter list");
        return;
    }

    OBSDataArrayAutoRelease filters = obs_data_get_array(parsed, "filters");
    if (!filters) {
        filters = obs_data_array_create();
    }
    obs_data_set_array(response, "filters", filters);
    obs_data_set_bool(response, "success", true);
}

// FIXME: vendor callbacks run on obs-websocket worker threads and block on
// outputMutex via proc_handler_call. Long-running recording updates pin the
// ws worker pool; consider dispatching via a queued call with a timeout.
static void overrideFilenameFormat(obs_data_t *request, obs_data_t *response, const char *procName)
{
    const char *filterUuid = obs_data_get_string(request, "filter_uuid");
    const char *format = obs_data_get_string(request, "format");

    if (!filterUuid || !filterUuid[0]) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "filter_uuid is required");
        return;
    }

    constexpr size_t UUID_LENGTH = 36;
    if (strnlen(filterUuid, UUID_LENGTH + 1) != UUID_LENGTH) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "filter_uuid must be a UUID string");
        return;
    }

    OBSSourceAutoRelease source = obs_get_source_by_uuid(filterUuid);
    const char *sourceId = source ? obs_source_get_id(source) : nullptr;
    if (!sourceId || strcmp(sourceId, BRANCH_OUTPUT_FILTER_ID) != 0) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "Filter not found");
        return;
    }

    if (format && strnlen(format, MAX_FORMAT_LENGTH + 1) > MAX_FORMAT_LENGTH) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "format too long");
        return;
    }

    proc_handler_t *ph = obs_source_get_proc_handler(source);
    calldata_t cd = {0};
    calldata_init(&cd);
    calldata_set_string(&cd, "format", format ? format : "");
    bool ok = proc_handler_call(ph, procName, &cd);
    calldata_free(&cd);

    obs_data_set_bool(response, "success", ok);
    if (!ok) {
        obs_data_set_string(response, "error", "Internal error: filter proc handler unavailable");
    }
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
    if (vendor) {
        return;
    }

    vendor = obs_websocket_register_vendor(VENDOR_NAME);
    if (!vendor) {
        obs_log(LOG_WARNING, "Failed to register obs-websocket vendor. obs-websocket not installed?");
        return;
    }

    if (!obs_websocket_vendor_register_request(vendor, "get_filter_list", onVendorGetFilterList, nullptr)) {
        obs_log(LOG_WARNING, "Failed to register vendor request: get_filter_list");
    }
    if (!obs_websocket_vendor_register_request(
            vendor, "override_recording_filename_format", onVendorOverrideRecordingFilenameFormat, nullptr
        )) {
        obs_log(LOG_WARNING, "Failed to register vendor request: override_recording_filename_format");
    }
    if (!obs_websocket_vendor_register_request(
            vendor, "override_replay_buffer_filename_format", onVendorOverrideReplayBufferFilenameFormat, nullptr
        )) {
        obs_log(LOG_WARNING, "Failed to register vendor request: override_replay_buffer_filename_format");
    }

    obs_log(LOG_INFO, "obs-websocket vendor requests registered");
}

void unregisterWebSocketVendorRequests()
{
    if (!vendor) {
        return;
    }

    // FIXME: obs_websocket_vendor_unregister_request() does not block until
    // in-flight callbacks return. The same worker race as the statusDock
    // FIXME in obs_module_unload() applies to vendor callbacks too.
    if (!obs_websocket_vendor_unregister_request(vendor, "get_filter_list")) {
        obs_log(LOG_DEBUG, "Failed to unregister vendor request: get_filter_list");
    }
    if (!obs_websocket_vendor_unregister_request(vendor, "override_recording_filename_format")) {
        obs_log(LOG_DEBUG, "Failed to unregister vendor request: override_recording_filename_format");
    }
    if (!obs_websocket_vendor_unregister_request(vendor, "override_replay_buffer_filename_format")) {
        obs_log(LOG_DEBUG, "Failed to unregister vendor request: override_replay_buffer_filename_format");
    }
    vendor = nullptr;
}
