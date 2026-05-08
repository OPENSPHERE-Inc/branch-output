# Branch Output API Reference

[日本語版はこちら](./API_ja.md)

## Overview

Branch Output exposes a small public API for **overriding the recording / replay buffer
filename format at runtime**, on a per-filter basis. Two transports are available:

1. **proc handler** — for in-process callers (OBS Script, plugins).
2. **obs-websocket vendor request** — for out-of-process callers (external tools, bots,
   Stream Deck integrations). Requires obs-websocket to be installed.

Both transports expose the same three operations:

| Operation | Purpose |
|-----------|---------|
| Get filter list | Enumerate the Branch Output filters currently loaded (returns UUIDs) |
| Override recording filename format | Per filter; affects stream recording |
| Override replay buffer filename format | Per filter; affects replay buffer save |

The override is held separately from the filter's own property setting and stays in effect
until cleared (pass an empty string) or OBS exits.

Typical use cases: organizing recorded files by current scene, by the value of a text input,
or by other external state.

## proc handler API

### Procedure summary

| Procedure name | Registered on | Signature |
|----------------|---------------|-----------|
| `osi_branch_output_get_filter_list` | Global (`obs_get_proc_handler()`) | `(out string json)` |
| `override_recording_filename_format` | Filter source (`obs_source_get_proc_handler(filter_source)`) | `(in string format)` |
| `override_replay_buffer_filename_format` | Filter source (`obs_source_get_proc_handler(filter_source)`) | `(in string format)` |

For both override procedures, an empty `format` clears the override and reverts to the value
configured in the filter properties. The `format` string supports OBS date/time specifiers
(e.g. `%CCYY-%MM-%DD %hh-%mm-%ss`).

### Acquiring the filter source

The override procedures are registered on each individual Branch Output filter, not on its
parent source. Typical flow:

1. Call `osi_branch_output_get_filter_list` to obtain UUIDs of all loaded Branch Output filters.
2. `obs_get_source_by_uuid(filter_uuid)` → filter source reference.
3. `obs_source_get_proc_handler(filter_source)` → proc handler.
4. Release the source with `obs_source_release()` after use.

If you already have a parent source reference, `obs_source_get_filter_by_name(parent,
filter_name)` is an alternative.

### Returned JSON of `osi_branch_output_get_filter_list`

```json
{
  "filters": [
    {
      "source_name": "Main Scene",
      "source_uuid": "12345678-1234-1234-1234-123456789abc",
      "filter_name": "Branch Output 1",
      "filter_uuid": "87654321-4321-4321-4321-cba987654321"
    }
  ]
}
```

| Field | Description |
|-------|-------------|
| `source_name` | Parent source/scene name |
| `source_uuid` | Parent source/scene UUID |
| `filter_name` | Branch Output filter name |
| `filter_uuid` | Branch Output filter UUID (used by the override procedures) |

Filters on private sources (those not visible in the OBS frontend) are excluded, matching the
Status Dock visibility rules. The list is a snapshot taken at call time; poll or refresh on
demand if you need to follow filter additions/removals.

### Behavior of `override_recording_filename_format`

| Recording state | Behavior |
|-----------------|----------|
| Not started | Format applied at the next recording start |
| Recording, file split enabled | A file split is triggered immediately |
| Recording, file split disabled | Recording is restarted |

### Behavior of `override_replay_buffer_filename_format`

The new format is used on the next replay buffer save. The replay buffer itself is **not**
restarted, even if it is currently running.

### Sample code: override recording filename format

**Python**

```python
import obspython as obs

# Pass "" as format to clear the override.
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
```

**Lua**

```lua
local obs = obslua

-- Pass "" as format to clear the override.
local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
end
```

For replay buffer, swap the procedure name to `override_replay_buffer_filename_format`.

### Sample code: get filter list

**Python**

```python
import json
import obspython as obs

def get_branch_output_filters():
    filters = []
    ph = obs.obs_get_proc_handler()
    cd = obs.calldata_create()

    if obs.proc_handler_call(ph, "osi_branch_output_get_filter_list", cd):
        json_str = obs.calldata_string(cd, "json")
        if json_str:
            try:
                data = json.loads(json_str)
                for item in data.get("filters", []):
                    filters.append((
                        item.get("source_name", ""),
                        item.get("source_uuid", ""),
                        item.get("filter_name", ""),
                        item.get("filter_uuid", ""),
                    ))
            except json.JSONDecodeError:
                obs.script_log(obs.LOG_WARNING, "Failed to parse filter list JSON")

    obs.calldata_free(cd)
    return filters
```

**Lua** — Lua has no built-in JSON parser, so we use OBS's `obs_data_create_from_json()`.

```lua
local obs = obslua

function get_branch_output_filters()
    local filters = {}
    local ph = obs.obs_get_proc_handler()
    local cd = obs.calldata_create()

    if obs.proc_handler_call(ph, "osi_branch_output_get_filter_list", cd) then
        local json_str = obs.calldata_string(cd, "json")
        if json_str and json_str ~= "" then
            local data = obs.obs_data_create_from_json(json_str)
            local array = obs.obs_data_get_array(data, "filters")
            local count = obs.obs_data_array_count(array)
            for i = 0, count - 1 do
                local item = obs.obs_data_array_item(array, i)
                table.insert(filters, {
                    source_name = obs.obs_data_get_string(item, "source_name"),
                    source_uuid = obs.obs_data_get_string(item, "source_uuid"),
                    filter_name = obs.obs_data_get_string(item, "filter_name"),
                    filter_uuid = obs.obs_data_get_string(item, "filter_uuid"),
                })
                obs.obs_data_release(item)
            end
            obs.obs_data_array_release(array)
            obs.obs_data_release(data)
        end
    end

    obs.calldata_free(cd)
    return filters
end
```

### Bundled sample scripts

Two ready-to-use OBS Scripts ship with the plugin, in both Python (`.py`) and Lua (`.lua`)
variants. Both variants implement the same logic — pick whichever language fits your setup.
Python requires Tools → Scripts → Python Settings to be configured; Lua works out of the box.

Install path:

- Windows: `data\obs-plugins\osi-branch-output\scripts\` under the OBS install directory
- macOS / Linux: standard OBS plugin data directory

Setup:

1. OBS → Tools → Scripts → click `+`
2. Pick the `.py` or `.lua` variant
3. In the Description panel, set:
   - **Text Source** — the text input to read from
   - **Branch Output Filter** — the filter to override
   - **Base Filename Format** — appended to the end of the filename
4. The override applies immediately. Check Script Log for activity.
5. While the script is loaded, the override takes precedence over the filter's own setting.
6. Remove the script (trash button) to restore the filter's own setting.

| Script | Reads | Applies to |
|--------|-------|------------|
| `recording-filename-from-text` | text input value | stream recording filename format |
| `replay-buffer-filename-from-text` | text input value | replay buffer save filename format |

The recording variant throttles updates to **one apply per 30 s per distinct value** to avoid
file-split / recording-restart storms. Adjust `THROTTLE_SECONDS` at the top of the script to
tune. The replay buffer variant has no such throttle since it does not restart the buffer.

## obs-websocket vendor request

The same three operations are also exposed as obs-websocket 5.x vendor requests, so external
tools can drive the plugin without an in-process script.

- **Vendor name:** `osi_branch_output`
- **Transport:** `CallVendorRequest`
- **Requirement:** obs-websocket must be installed.

### Security

Authentication is delegated entirely to obs-websocket. The plugin adds no separate
authentication layer. When you expose obs-websocket beyond `localhost`, enable its password
authentication and front the connection with TLS (e.g., terminate `wss://` at nginx or
Caddy). Treat the obs-websocket password as a credential equivalent to OBS user authority —
it grants full access to every request below.

### Request reference

#### `get_filter_list`

Enumerates every Branch Output filter currently loaded.

| Item | Value |
|------|-------|
| Request type | `get_filter_list` |
| Request data | *(none)* |
| Response | `{ "success": bool, "error"?: string, "filters": array }` |

`filters[]` element:

| Field | Description |
|-------|-------------|
| `source_name` | Parent source/scene name |
| `source_uuid` | Parent source/scene UUID |
| `filter_name` | Branch Output filter name |
| `filter_uuid` | Branch Output filter UUID (used by the override requests below) |

Filters on private sources are excluded.

#### `override_recording_filename_format` / `override_replay_buffer_filename_format`

Override the filename format on a specific Branch Output filter.

| Item | Value |
|------|-------|
| Request type | `override_recording_filename_format` or `override_replay_buffer_filename_format` |
| Request data | `{ "filter_uuid": string, "format": string }` |
| Response | `{ "success": bool, "error"?: string }` |
| Clear the override | Pass `""` for `format` |

`success: true` means the value was **stored** on the filter. It does **not** indicate that
the new format has already been applied to a recording file (see [Behavior of
`override_recording_filename_format`](#behavior-of-override_recording_filename_format) and
[Behavior of `override_replay_buffer_filename_format`](#behavior-of-override_replay_buffer_filename_format)
above).

### Validation rules

| Field | Rule |
|-------|------|
| `filter_uuid` | 36-char lowercase canonical UUID (8-4-4-4-12 with lowercase hex) — matches `obs_source_get_uuid()` output |
| `filter_uuid` | Must resolve to an existing Branch Output filter source |
| `format` | At most 1024 bytes |
| `format` | Relative path expression only — absolute paths (`/...`, `\...`, `X:\...`), `..` segments, and a leading `~` are rejected |

Validation failures return `{ "success": false, "error": "<message>" }` with a short
human-readable error string.

### Example

```json
{
  "requestType": "CallVendorRequest",
  "requestData": {
    "vendorName": "osi_branch_output",
    "requestType": "override_recording_filename_format",
    "requestData": {
      "filter_uuid": "87654321-4321-4321-4321-cba987654321",
      "format": "%CCYY-%MM-%DD %hh-%mm-%ss MyScene"
    }
  }
}
```

Successful response:

```json
{ "success": true }
```

Failure response:

```json
{ "success": false, "error": "filter_uuid must be a lowercase canonical UUID string" }
```

## Known Limitations

These items affect callers in practice but are not part of the API contract; they are listed
together here so the main API reference stays scannable.

### Do not call from OBS signal or frontend callbacks

Calling any of the proc handler procedures (or invoking the corresponding vendor requests
synchronously from the same callback) from `obs_source_signal`, `obs_output_signal`, or
`obs_frontend_event_callback` may deadlock. Safe callers: script timers, hotkey handlers, UI
event handlers. When calling from a hotkey, ensure your own code does not hold a Branch
Output lock at the call site.

### Deferred application during recording transitions

If a recording is mid-transition (pending, splitting, or restarting), the override is stored
and applied with up to about 1 s of delay rather than synchronously. The proc / vendor call
returns immediately; there is no notification when the new format becomes active. Bursting
requests during a transition can also cause individual requests to block until the transition
completes.

### Lifetime

Do not call any of these procedures or vendor requests after `obs_module_unload()` —
behavior is undefined. The procedures and vendor requests are registered during
`obs_module_post_load()`; callers that race the early startup window must check the proc
return value before reading `out` parameters.

### Server-side throttling is not implemented

The plugin does not rate-limit `override_recording_filename_format` /
`override_replay_buffer_filename_format`. Each accepted distinct `format` while a recording
is active triggers either a file split or a recording restart, both of which have non-trivial
A/V cost (see below). **Implement throttling on the client side** — at minimum, suppress
duplicate `format` values, and rate-limit distinct updates (≥ 30 s is a safe starting point).

The bundled `recording-filename-from-text` script enforces this throttle internally, but
vendor requests bypass that script. The bundled `replay-buffer-filename-from-text` script
does not throttle because it does not restart the replay buffer.

### A/V cost of frequent override changes

Each accepted distinct `format` triggers one of two paths:

- **Split-enabled path** (encoder kept alive, file boundary only):
  - Cut aligned to the next encoder keyframe; latency to the cut is bounded by the configured
    keyframe interval (typically ~2 s; with `keyint_sec=0` the GOP length follows the encoder
    backend's native default and can extend to several seconds).
  - New container header per file. Reordered B-frames preceding the cut IDR are flushed into
    the previous file, so no encoded frames are lost across the boundary, but PTS continuity
    across files depends on each file's container `start_time` — naive concatenation can
    leave a small gap at the boundary for streams using B-frames.
  - AAC encoder priming samples appear at the start of each new file (AAC-LC: 2112 samples =
    2048 + 64 per ISO/IEC 14496-3; HE-AAC adds a further 481 samples of SBR delay). Naive
    concatenation can therefore produce an audible click or short gap at each cut.
- **Recording-restart path** (encoder torn down and recreated):
  - Hardware encoder backends (NVENC, QSV, AMF, VideoToolbox) recreate session/GPU contexts.
    Typical stall: 100 ms – 1 s. Cold-start cases (first session after process launch, macOS
    VideoToolbox HEVC, certain NVENC driver states) can exceed 1 s.
  - Latency is dominated by session creation, not by the keyframe interval.

Choose a client-side throttle interval that comfortably exceeds these costs.

### `obs_data_get_string()` cannot distinguish missing key from empty value

By libobs convention, `obs_data_get_string()` returns `""` both for a missing key and for an
explicitly empty string, so the plugin cannot tell the two cases apart:

- A missing `filter_uuid` is treated as `""` and rejected with `"filter_uuid is required"`.
- A missing `format` is treated as `""`, which **clears** any active override.

Send keys explicitly when the distinction matters, and validate request shapes on the client
side.

### Lua sample script + non-ANSI paths on Windows

When the `recording-filename-from-text` / `replay-buffer-filename-from-text` Lua variant
reads a text source whose mode is "Read from file", it opens the file with `io.open()`. On
Windows, `io.open()` interprets the path in the system ANSI code page (e.g., CP932 on a
Japanese locale), so paths containing characters outside that code page may fail to open.
The Python variant uses CPython's wide-character Windows APIs and is unaffected — switch to
the Python variant if full UTF-8 path support is required on Windows.
