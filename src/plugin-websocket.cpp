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

#include <atomic>
#include <cstring>
#include <string>

#include "plugin-main.hpp"
#include "plugin-support.h"
#include "plugin-websocket.hpp"

#define VENDOR_NAME "osi_branch_output"

// DoS guard: recording filename templates are typically <100 chars.
// Reject pathologically long inputs before they reach libobs's path expander.
static constexpr size_t MAX_FORMAT_LENGTH = 1024;

// Reject formats that would let an authenticated obs-websocket client write
// outside the configured recording directory.
static bool isPathTraversalFormat(const char *format, size_t length)
{
    if (length == 0) {
        return false;
    }

    // Absolute path on POSIX or UNC/rooted path on Windows.
    if (format[0] == '/' || format[0] == '\\') {
        return true;
    }

    // Windows drive-letter prefix: "X:" optionally followed by a separator.
    if (length >= 2 && format[1] == ':') {
        char c = format[0];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            return true;
        }
    }

    // Reject any ".." segment regardless of separator style.
    for (size_t i = 0; i + 1 < length; i++) {
        if (format[i] != '.' || format[i + 1] != '.') {
            continue;
        }
        bool atStart = (i == 0) || format[i - 1] == '/' || format[i - 1] == '\\';
        if (!atStart) {
            continue;
        }
        size_t after = i + 2;
        bool atEnd = (after == length) || format[after] == '/' || format[after] == '\\';
        if (atEnd) {
            return true;
        }
    }

    return false;
}

// Validate canonical 8-4-4-4-12 lowercase-hex UUID with hyphens at fixed positions.
// Matches the form produced by obs_source_get_uuid(), which is what
// obs_get_source_by_uuid() compares against byte-for-byte.
// Caller must guarantee the buffer is at least 36 bytes long.
static bool isValidUuidString(const char *uuid)
{
    constexpr size_t UUID_LENGTH = 36;
    for (size_t i = 0; i < UUID_LENGTH; i++) {
        char c = uuid[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') {
                return false;
            }
        } else {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return false;
            }
        }
    }
    return true;
}

// Atomic so register/unregister cannot race even if obs-websocket ever calls
// these from a non-main thread; the early-return in registerWebSocketVendorRequests()
// reads `vendor` without any external lock.
static std::atomic<obs_websocket_vendor> vendor{nullptr};

static void onVendorGetFilterList(obs_data_t *, obs_data_t *response, void *)
{
    proc_handler_t *ph = obs_get_proc_handler();
    calldata_t cd = {};
    calldata_init(&cd);
    bool ok = proc_handler_call(ph, "osi_branch_output_get_filter_list", &cd);
    const char *json = calldata_string(&cd, "json");

    // obs_data_create_from_json() must run before calldata_free() — `json` points into cd's buffer.
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

// FIXME: vendor callbacks run on obs-websocket worker threads and block
// synchronously on either outputMutex (override_*_filename_format) or the
// UI thread via Qt::BlockingQueuedConnection (get_filter_list, see
// onGetFilterList in plugin-main.cpp). Either path can pin the ws worker
// pool and risks deadlock if the UI thread is itself waiting on a ws
// callback. Dispatch via a queued call with a timeout.
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

    // obs_get_source_by_uuid() does a case-sensitive compare against
    // the canonical lowercase form, so normalize before validating.
    std::string normalizedUuid(filterUuid, UUID_LENGTH);
    for (char &c : normalizedUuid) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (!isValidUuidString(normalizedUuid.c_str())) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "filter_uuid must be a UUID string");
        return;
    }

    OBSSourceAutoRelease source = obs_get_source_by_uuid(normalizedUuid.c_str());
    const char *sourceId = source ? obs_source_get_id(source) : nullptr;
    if (!sourceId || strcmp(sourceId, FILTER_ID) != 0 || obs_source_get_type(source) != OBS_SOURCE_TYPE_FILTER ||
        obs_source_removed(source)) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "UUID does not refer to a Branch Output filter");
        return;
    }

    if (format) {
        size_t formatLength = strnlen(format, MAX_FORMAT_LENGTH + 1);
        if (formatLength > MAX_FORMAT_LENGTH) {
            obs_data_set_bool(response, "success", false);
            obs_data_set_string(response, "error", "format too long");
            return;
        }
        // FIXME: embedded NUL detection is impossible via the
        // obs_data_get_string() C-string interface, which exposes only
        // the prefix up to the first NUL. If JSON-encoded NUL smuggling
        // becomes a concern, validate at the JSON layer before this point.
        if (isPathTraversalFormat(format, formatLength)) {
            obs_data_set_bool(response, "success", false);
            obs_data_set_string(response, "error", "format must not contain path traversal or absolute paths");
            return;
        }
    }

    proc_handler_t *ph = obs_source_get_proc_handler(source);
    calldata_t cd = {};
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
    overrideFilenameFormat(request, response, PROC_OVERRIDE_RECORDING_FILENAME_FORMAT);
}

static void onVendorOverrideReplayBufferFilenameFormat(obs_data_t *request, obs_data_t *response, void *)
{
    overrideFilenameFormat(request, response, PROC_OVERRIDE_REPLAY_BUFFER_FILENAME_FORMAT);
}

void registerWebSocketVendorRequests()
{
    // FIXME: obs-websocket exposes no unregister_vendor API, so once `vendor`
    // is non-null we can never recover from a partial registration failure
    // below — a subsequent module reload in the same process will early-return
    // here and the failed requests stay missing for the lifetime of OBS.
    if (vendor.load(std::memory_order_acquire)) {
        return;
    }

    obs_websocket_vendor v = obs_websocket_register_vendor(VENDOR_NAME);
    if (!v) {
        obs_log(LOG_WARNING, "Failed to register obs-websocket vendor. obs-websocket not installed?");
        return;
    }

    if (!obs_websocket_vendor_register_request(v, "get_filter_list", onVendorGetFilterList, nullptr)) {
        obs_log(LOG_ERROR, "Failed to register vendor request 'get_filter_list'; clients will get UnknownRequestType");
    }
    if (!obs_websocket_vendor_register_request(
            v, "override_recording_filename_format", onVendorOverrideRecordingFilenameFormat, nullptr
        )) {
        obs_log(
            LOG_ERROR,
            "Failed to register vendor request 'override_recording_filename_format'; clients will get UnknownRequestType"
        );
    }
    if (!obs_websocket_vendor_register_request(
            v, "override_replay_buffer_filename_format", onVendorOverrideReplayBufferFilenameFormat, nullptr
        )) {
        obs_log(
            LOG_ERROR,
            "Failed to register vendor request 'override_replay_buffer_filename_format'; clients will get UnknownRequestType"
        );
    }

    // Publish the vendor handle only after all request registrations have been
    // attempted, so unregisterWebSocketVendorRequests() never sees a partially
    // populated vendor.
    vendor.store(v, std::memory_order_release);

    obs_log(LOG_INFO, "obs-websocket vendor requests registered");
}

void unregisterWebSocketVendorRequests()
{
    obs_websocket_vendor v = vendor.load(std::memory_order_acquire);
    if (!v) {
        return;
    }

    // FIXME: obs_websocket_vendor_unregister_request() does not block until
    // in-flight callbacks return. The same worker race as the statusDock
    // FIXME in obs_module_unload() applies to vendor callbacks too.
    if (!obs_websocket_vendor_unregister_request(v, "get_filter_list")) {
        obs_log(LOG_DEBUG, "Failed to unregister vendor request: get_filter_list");
    }
    if (!obs_websocket_vendor_unregister_request(v, "override_recording_filename_format")) {
        obs_log(LOG_DEBUG, "Failed to unregister vendor request: override_recording_filename_format");
    }
    if (!obs_websocket_vendor_unregister_request(v, "override_replay_buffer_filename_format")) {
        obs_log(LOG_DEBUG, "Failed to unregister vendor request: override_replay_buffer_filename_format");
    }
    vendor.store(nullptr, std::memory_order_release);
}
