# Branch Output API Reference

[日本語版はこちら](./API_ja.md)

## Recording Filename Override

### Overview

Recording Filename Override is a feature that allows you to override the filename format of stream recordings and replay buffer saves at runtime, using Branch Output's public procedures.

The overridden filename format is stored separately from the filter's property settings, and remains active until it is reset via procedure or OBS is closed.

This feature was implemented in response to production requirements where recorded files need to be organized according to the current scene, the value of a text input, or other external data — by overriding the filename at runtime.

### Threading and Call Context

All procedures in this API follow these threading rules:

- **Thread safety**: Callable from any thread. The call is not guaranteed to be constant-time (a recording restart or file split may occur), so avoid latency-sensitive hot paths.
- **Callback safety**: Do not call from OBS signal callbacks (`obs_source_signal`, `obs_output_signal`, etc.) or frontend event callbacks (`obs_frontend_event_callback`) — a deadlock may occur. Safe callers: script timers, hotkey handlers, UI event handlers. When calling from a hotkey callback, ensure your own code does not hold a Branch Output lock at the call site. The same restriction applies to `osi_branch_output_get_filter_list` below.
- **Deferred application**: If recording is transitioning (pending, split, or restart in progress), the override is stored and applied with up to about 1 second of delay rather than synchronously. The proc call returns immediately and there is no notification when the new format becomes active.

### Obtaining the Filter Source

The per-filter override procedures (`override_recording_filename_format`, `override_replay_buffer_filename_format`) are registered on **each individual Branch Output filter source** — not on the parent source/scene. To call them, you need a reference to the filter source itself, not its parent.

The typical acquisition flow is:

1. Call the global procedure `osi_branch_output_get_filter_list` to obtain the UUIDs of all Branch Output filters currently loaded.
2. Use `obs_get_source_by_uuid(filter_uuid)` to get a reference to the filter source.
3. Call `obs_source_get_proc_handler(filter_source)` to get the proc handler.
4. Always release the source with `obs_source_release()` after use.

Alternatively, if you already have a reference to the parent source, you can use `obs_source_get_filter_by_name(parent, filter_name)`.

### Overriding the Stream Recording Filename Format

A procedure registered on the Branch Output filter source that overrides the output filename format for stream recording at runtime.

| Item | Description |
|------|-------------|
| Procedure name | `override_recording_filename_format` |
| Signature | `void override_recording_filename_format(in string format)` |
| Registered on | Branch Output filter source (`obs_source_get_proc_handler(filter_source)`) |
| Parameters | `format` (string) — The new filename format. OBS date/time specifiers (e.g., `%CCYY-%MM-%DD %hh-%mm-%ss`) are supported. **Passing an empty string clears the override and reverts to the filename format configured in the filter properties.** |
| Returns | None |

**Behavior during recording**

- Before recording starts: The overridden filename format is used when recording begins
- During recording (with file splitting enabled): A file split is triggered immediately when the filename format changes
- During recording (without file splitting): Recording is restarted with the new filename format

**Python sample code**

```python
import obspython as obs

# Get the target Branch Output filter by UUID. Pass "" as format to clear.
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
```

**Lua sample code**

```lua
local obs = obslua

-- Get the target Branch Output filter by UUID. Pass "" as format to clear.
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

### Overriding the Replay Buffer Save Filename Format

A procedure registered on the Branch Output filter source that overrides the output filename format used when the replay buffer is saved.

| Item | Description |
|------|-------------|
| Procedure name | `override_replay_buffer_filename_format` |
| Signature | `void override_replay_buffer_filename_format(in string format)` |
| Registered on | Branch Output filter source (`obs_source_get_proc_handler(filter_source)`) |
| Parameters | `format` (string) — The new filename format. OBS date/time specifiers (e.g., `%CCYY-%MM-%DD %hh-%mm-%ss`) are supported. **Passing an empty string clears the override and reverts to the filename format configured in the filter properties.** |
| Returns | None |

The overridden filename format takes effect on the next replay buffer save. The override is applied immediately even while the replay buffer is running, without restarting the replay buffer itself.

**Python sample code**

```python
import obspython as obs

# Get the target Branch Output filter by UUID. Pass "" as format to clear.
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
```

**Lua sample code**

```lua
local obs = obslua

-- Get the target Branch Output filter by UUID. Pass "" as format to clear.
local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
end
```

### Retrieving the Branch Output Filter List

A global procedure that returns the list of Branch Output filters currently loaded in OBS. Since the override procedures above require the target filter's UUID, this procedure is typically used to present the filter list to the user for selection.

| Item | Description |
|------|-------------|
| Procedure name | `osi_branch_output_get_filter_list` |
| Signature | `void osi_branch_output_get_filter_list(out string json)` |
| Registered on | Global procedure handler (`obs_get_proc_handler()`) |
| Parameters | `json` (out string) — JSON string representing the list of Branch Output filters |
| Returns | None |

**Notes**

- The procedure is registered during `obs_module_post_load()`. Calling it before registration completes fails: `proc_handler_call()` returns `false` and the `out string json` parameter is not written. Always check the return value before reading `json`.
- Filters applied to **private sources** (sources not visible in the OBS frontend) are intentionally excluded from the returned list, matching the Status Dock's visibility rules.
- The returned list is a snapshot taken at call time. Poll periodically or refresh on demand if you need to react to filter additions/removals.
- **Thread safety**: Callable from any thread.
- **Lifetime**: Do not call after `obs_module_unload()` — behavior is undefined.

**Returned JSON structure**

```json
{
  "filters": [
    {
      "source_name": "Main Scene",
      "source_uuid": "12345678-1234-1234-1234-123456789abc",
      "filter_name": "Branch Output 1",
      "filter_uuid": "87654321-4321-4321-4321-cba987654321"
    },
    ...
  ]
}
```

| Field | Description |
|-------|-------------|
| `source_name` | Name of the parent source/scene to which the Branch Output filter is applied |
| `source_uuid` | UUID of the parent source/scene |
| `filter_name` | Name of the Branch Output filter |
| `filter_uuid` | UUID of the Branch Output filter (used when calling the override procedures) |

**Python sample code**

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

**Lua sample code**

Since Lua does not have a built-in JSON parser, we use OBS's `obs_data_create_from_json()` to parse the returned JSON string.

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

### Using the Sample Scripts

Two variants of each sample script are bundled with the plugin: **Python** (`.py`) and **Lua** (`.lua`). Both variants implement the same functionality; choose whichever language you prefer.

- Python scripts require Python Settings to be properly configured in OBS under **Tools → Scripts**.
- Lua scripts do not require any additional configuration — Lua support is built into OBS.

On Windows, the scripts are installed at `data\obs-plugins\osi-branch-output\scripts\` under the OBS installation path. On macOS and Linux, the path follows the standard OBS plugin data directory convention.

#### Common setup

Both sample scripts share the same setup flow:

1. Open OBS menu → Tools → Scripts
2. Click the plus (+) button at the bottom of the Scripts dialog
3. Select the script file — either the `.py` or the `.lua` variant.
4. In the Description panel, configure:
   - **Text Source** — Select the text input
   - **Branch Output Filter** — Select the Branch Output filter to override
   - **Base Filename Format** — The base format appended to the end of the filename.
5. The override becomes active immediately. Check **Script Log** to confirm activity.
6. The overridden filename takes precedence over the filter's own setting while the script is loaded; the filter property value is not used.
7. To disable the override, remove the script via the trash button.

**Windows + "Read from file" — Lua limitation:** When the text source reads from a file, the Lua variant uses `io.open()`, which on Windows interprets the path in the system ANSI code page (e.g. CP932 on Japanese locale). Paths containing characters outside that code page may therefore fail to open. The Python variant is not affected (CPython uses wide-character Windows APIs). Use the Python variant if full UTF-8 path support is required on Windows.

#### recording-filename-from-text

Reads the value of a text input and applies it to the stream recording filename format. See [behavior during recording](#overriding-the-stream-recording-filename-format) above for how the override is applied in each recording state.

**Throttling:** To avoid excessive file splits or recording restarts when the text source changes rapidly, the script throttles updates to one apply per 30 seconds per distinct text value. A text change may therefore take up to 30 seconds to be reflected. Adjust `THROTTLE_SECONDS` at the top of the script to tune this interval.

#### replay-buffer-filename-from-text

Reads the value of a text input and applies it to the replay buffer save filename format. The override takes effect on the next save; the replay buffer itself is not restarted.
