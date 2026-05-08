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
#include <type_traits>

#include "plugin-main.hpp"
#include "plugin-support.h"
#include "plugin-websocket.hpp"

#define VENDOR_NAME "osi_branch_output"

// Vendor request type names. Shared between register, unregister, and any
// future dispatch sites so a rename cannot silently desynchronize them.
// Distinct from PROC_OVERRIDE_* constants, which name proc handler entries.
static constexpr char WS_REQUEST_GET_FILTER_LIST[] = "get_filter_list";
static constexpr char WS_REQUEST_OVERRIDE_RECORDING_FILENAME_FORMAT[] = "override_recording_filename_format";
static constexpr char WS_REQUEST_OVERRIDE_REPLAY_BUFFER_FILENAME_FORMAT[] = "override_replay_buffer_filename_format";

// DoS guard before reaching libobs's path expander. Caller-visible limit is
// documented in API.md "Validation rules"; keep the two in sync.
static constexpr size_t MAX_FORMAT_LENGTH = 1024;

// Sanity cap on the JSON payload returned by the get_filter_list proc handler.
// Each filter entry is roughly 200-400 bytes (uuid + name + parent uuid + flags),
// so 256 KiB accommodates well over a thousand filters while bounding the cost
// an authenticated obs-websocket client can induce by repeatedly invoking
// get_filter_list against a host that has accumulated many Branch Output filters.
static constexpr size_t MAX_FILTER_LIST_JSON_LENGTH = 256 * 1024;

// Reject formats that would let a client write outside the recording directory.
// Caller-visible rules are in API.md "Validation rules"; this function inspects
// the raw template only.
// FIXME: silently bypassed if libobs ever adds an expansion token whose value
// can contain path separators or ".." segments (e.g. profile-name passthrough).
// Re-validate the post-expansion result inside applyFilenameFormatArgs().
static bool isPathTraversalFormat(const char *format, size_t length)
{
    if (length == 0) {
        return false;
    }

    // Leading ASCII control chars (< 0x20) or space can be silently stripped by
    // downstream path handling, letting " /etc/x" bypass the absolute-path check
    // below; rejecting the full class also narrows log injection surface.
    {
        unsigned char c = static_cast<unsigned char>(format[0]);
        if (c < 0x20 || c == ' ') {
            return true;
        }
    }

    // Reject leading '~' (some path expanders treat it as $HOME).
    if (format[0] == '~') {
        return true;
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

    // Reject trailing space or ASCII control char: Win32 path normalization
    // strips trailing spaces/dots, which can produce a different file than
    // the template requested. Unicode whitespace (e.g., U+00A0) is left to
    // os_generate_formatted_filename()'s sanitization rather than parsed here.
    {
        unsigned char c = static_cast<unsigned char>(format[length - 1]);
        if (c < 0x20 || c == ' ') {
            return true;
        }
    }

    // Reject any '..' segment, and reject any ASCII control char (< 0x20)
    // anywhere in the buffer. Embedded CR/LF would otherwise enable log
    // injection and confuse line-oriented downstream consumers.
    for (size_t i = 0; i < length; i++) {
        unsigned char c = static_cast<unsigned char>(format[i]);
        if (c < 0x20) {
            return true;
        }
        if (i + 1 >= length || format[i] != '.' || format[i + 1] != '.') {
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

// Canonical 8-4-4-4-12 UUID byte length, matching obs_source_get_uuid()'s output.
static constexpr size_t UUID_LENGTH = 36;

// Validate canonical UUID form (see API.md "Validation rules"). length is a
// parameter so callers can pass a strnlen()-bounded value without scanning.
static bool isValidUuidString(const char *uuid, size_t length)
{
    if (!uuid || length != UUID_LENGTH) {
        return false;
    }
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

// Atomic so the early-return in registerWebSocketVendorRequests() can read
// `vendor` without an external lock. In-flight callback safety is a separate
// concern (see FIXME near unregisterWebSocketVendorRequests()).
static std::atomic<obs_websocket_vendor> vendor{nullptr};
static_assert(
    std::is_pointer_v<obs_websocket_vendor>,
    "obs_websocket_vendor must remain a pointer typedef for nullptr / atomic semantics to be sound"
);
static_assert(
    decltype(vendor)::is_always_lock_free,
    "obs_websocket_vendor (void*) must be lock-free for register/unregister synchronization"
);

// Sticky flag: set once the vendor has been successfully registered in this
// process. obs-websocket exposes no unregister_vendor API, so a subsequent
// register attempt failing implies the orphaned handle from a prior load of
// this same plugin (self-collision via module reload), not a third-party
// claim. Used only to disambiguate the diagnostic message.
static std::atomic<bool> previouslyRegistered{false};

static void onVendorGetFilterList(obs_data_t *, obs_data_t *response, void *)
{
    proc_handler_t *ph = obs_get_proc_handler();
    calldata_t cd = {};
    calldata_init(&cd);
    bool ok = proc_handler_call(ph, "osi_branch_output_get_filter_list", &cd);
    const char *json = calldata_string(&cd, "json");

    // Probe with strnlen(cap + 1) so the cap branch fires without scanning the
    // whole buffer when the response exceeds the limit.
    size_t jsonLength = json ? strnlen(json, MAX_FILTER_LIST_JSON_LENGTH + 1) : 0;
    bool tooLarge = ok && jsonLength > MAX_FILTER_LIST_JSON_LENGTH;

    // obs_data_create_from_json() must run before calldata_free() — `json` points into cd's buffer.
    OBSDataAutoRelease parsed = (ok && !tooLarge) ? obs_data_create_from_json(json ? json : "{}") : nullptr;
    calldata_free(&cd);

    if (!ok) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "Internal error: filter list proc handler unavailable");
        return;
    }

    if (tooLarge) {
        obs_log(
            LOG_WARNING, "Vendor request get_filter_list: response payload exceeds %zu byte cap; rejecting",
            MAX_FILTER_LIST_JSON_LENGTH
        );
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "Internal error: filter list response too large");
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

// FIXME: vendor callbacks run on ws worker threads and block on outputMutex
// (override_*) or the UI thread via BlockingQueuedConnection (get_filter_list).
// User-visible deadlock risk is documented in API.md "Known Limitations" — Do
// not call from OBS signal or frontend callbacks. Internal fix: replace the
// blocking dispatch with a queued call + bounded wait_for() timeout so neither
// path can pin the ws worker pool. Applies to onVendorGetFilterList above too.
//
// FIXME: same-format spam is absorbed at the proc handler boundary
// (onOverrideRecordingFilenameFormat / onOverrideReplayBufferFilenameFormat),
// but distinct-format flooding is unbounded — see API.md "Known Limitations" —
// Server-side throttling is not implemented. Add a per-filter timestamp gate.
static void overrideFilenameFormat(obs_data_t *request, obs_data_t *response, const char *procName)
{
    // obs_data_get_string() returns "" for both missing key and explicit ""; see
    // API.md "Known Limitations" for the caller-side consequence.
    const char *filterUuid = obs_data_get_string(request, "filter_uuid");
    const char *format = obs_data_get_string(request, "format");

    if (!filterUuid[0]) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "filter_uuid is required");
        return;
    }

    size_t filterUuidLength = strnlen(filterUuid, UUID_LENGTH + 1);
    if (!isValidUuidString(filterUuid, filterUuidLength)) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "filter_uuid must be a lowercase canonical UUID string");
        return;
    }

    OBSSourceAutoRelease source = obs_get_source_by_uuid(filterUuid);
    const char *sourceId = source ? obs_source_get_id(source) : nullptr;
    if (!sourceId || strcmp(sourceId, FILTER_ID) != 0 || obs_source_get_type(source) != OBS_SOURCE_TYPE_FILTER) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "UUID does not refer to a Branch Output filter");
        return;
    }

    size_t formatLength = strnlen(format, MAX_FORMAT_LENGTH + 1);
    if (formatLength > MAX_FORMAT_LENGTH) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "format too long");
        return;
    }
    // FIXME: a JSON-escaped NUL inside `format` truncates the C-string view
    // returned by obs_data_get_string(), so this check validates only the
    // prefix while trailing bytes may still reach the OS path layer.
    // Fix direction: compare strnlen(format) against the JSON-decoded byte
    // length (via obs_data_get_json or obs_data_item) and reject mismatches.
    if (isPathTraversalFormat(format, formatLength)) {
        obs_data_set_bool(response, "success", false);
        obs_data_set_string(response, "error", "format must not contain path traversal or absolute paths");
        return;
    }

    proc_handler_t *ph = obs_source_get_proc_handler(source);
    calldata_t cd = {};
    calldata_init(&cd);
    calldata_set_string(&cd, "format", format);
    bool ok = proc_handler_call(ph, procName, &cd);
    calldata_free(&cd);

    obs_data_set_bool(response, "success", ok);
    if (!ok) {
        obs_log(LOG_WARNING, "Vendor request: proc handler '%s' unavailable on filter %s", procName, filterUuid);
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
    // FIXME: obs-websocket exposes no unregister_vendor API, so once
    // obs_websocket_register_vendor() succeeds we can never reclaim that handle
    // — a subsequent module reload in the same process will collide.
    if (vendor.load(std::memory_order_acquire)) {
        return;
    }

    obs_websocket_vendor v = obs_websocket_register_vendor(VENDOR_NAME);
    if (!v) {
        // Disambiguate "obs-websocket not loaded" from "vendor name already
        // claimed": probe the proc_handler the vendor API itself looks up.
        bool obsWebSocketAvailable = obs_websocket_get_ph() != nullptr;
        if (!obsWebSocketAvailable) {
            obs_log(LOG_WARNING, "Failed to register obs-websocket vendor: obs-websocket not installed");
        } else if (previouslyRegistered.load(std::memory_order_acquire)) {
            obs_log(
                LOG_WARNING,
                "Failed to register obs-websocket vendor '%s': name still held by a prior load of this plugin "
                "(obs-websocket exposes no unregister_vendor API; restart OBS to recover)",
                VENDOR_NAME
            );
        } else {
            obs_log(
                LOG_WARNING, "Failed to register obs-websocket vendor '%s': name already claimed by another plugin",
                VENDOR_NAME
            );
        }
        return;
    }

    bool getFilterListOk =
        obs_websocket_vendor_register_request(v, WS_REQUEST_GET_FILTER_LIST, onVendorGetFilterList, nullptr);
    bool overrideRecordingOk = obs_websocket_vendor_register_request(
        v, WS_REQUEST_OVERRIDE_RECORDING_FILENAME_FORMAT, onVendorOverrideRecordingFilenameFormat, nullptr
    );
    bool overrideReplayBufferOk = obs_websocket_vendor_register_request(
        v, WS_REQUEST_OVERRIDE_REPLAY_BUFFER_FILENAME_FORMAT, onVendorOverrideReplayBufferFilenameFormat, nullptr
    );

    if (!getFilterListOk || !overrideRecordingOk || !overrideReplayBufferOk) {
        // Roll back any successful registrations so the vendor handle is left in a
        // clean, fully-unregistered state. Do not publish `vendor` so future calls
        // to unregisterWebSocketVendorRequests() short-circuit safely.
        if (getFilterListOk) {
            obs_websocket_vendor_unregister_request(v, WS_REQUEST_GET_FILTER_LIST);
        }
        if (overrideRecordingOk) {
            obs_websocket_vendor_unregister_request(v, WS_REQUEST_OVERRIDE_RECORDING_FILENAME_FORMAT);
        }
        if (overrideReplayBufferOk) {
            obs_websocket_vendor_unregister_request(v, WS_REQUEST_OVERRIDE_REPLAY_BUFFER_FILENAME_FORMAT);
        }
        obs_log(
            LOG_ERROR,
            "Failed to register obs-websocket vendor requests "
            "(get_filter_list=%d, override_recording_filename_format=%d, "
            "override_replay_buffer_filename_format=%d); rolling back",
            getFilterListOk, overrideRecordingOk, overrideReplayBufferOk
        );
        return;
    }

    // Publish the vendor handle only after every request has been registered
    // successfully, so unregisterWebSocketVendorRequests() never observes a
    // partially populated vendor.
    vendor.store(v, std::memory_order_release);
    previouslyRegistered.store(true, std::memory_order_release);

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
    if (!obs_websocket_vendor_unregister_request(v, WS_REQUEST_GET_FILTER_LIST)) {
        obs_log(LOG_DEBUG, "Failed to unregister vendor request: get_filter_list");
    }
    if (!obs_websocket_vendor_unregister_request(v, WS_REQUEST_OVERRIDE_RECORDING_FILENAME_FORMAT)) {
        obs_log(LOG_DEBUG, "Failed to unregister vendor request: override_recording_filename_format");
    }
    if (!obs_websocket_vendor_unregister_request(v, WS_REQUEST_OVERRIDE_REPLAY_BUFFER_FILENAME_FORMAT)) {
        obs_log(LOG_DEBUG, "Failed to unregister vendor request: override_replay_buffer_filename_format");
    }
    vendor.store(nullptr, std::memory_order_release);
}
