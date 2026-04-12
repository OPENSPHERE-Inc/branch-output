# Branch Output API Reference

[日本語版はこちら](./API_ja.md)

## Recording Filename Override

### Overview

Recording Filename Override is a feature that allows you to override the filename format of stream recordings and replay buffer saves at runtime, using Branch Output's public procedures.

The overridden filename format is stored separately from the filter's property settings, and remains active until it is reset via procedure or OBS is closed.

This feature was implemented in response to production requirements where recorded files need to be organized according to the current scene, the value of a text input, or other external data — by overriding the filename at runtime.

### Threading and Call Context

All procedures in this API follow these threading rules:

- **Thread safety**: The override procedures may be called from any thread. Internally they acquire the filter's `outputMutex` and, depending on the current recording state, may call `obs_output_update()` (when file splitting is enabled) or defer work to the next interval-timer tick (which performs a recording restart). The call is therefore not guaranteed to be constant-time — avoid calling it on latency-sensitive hot paths.
- **Callback safety**: These procedures **must not be called from OBS signal callbacks (`obs_source_signal`, `obs_output_signal`, etc.) or from frontend event callbacks (`obs_frontend_event_callback`)**. Such callbacks may already hold — or may indirectly acquire — the filter's `outputMutex` or a libobs output lock, which would cause a deadlock against the locks taken inside the override procedure. Prefer calling them from script timer callbacks, hotkey handlers, or UI event handlers instead. The "hotkey handlers" guidance here assumes **scripting hotkeys registered via `obslua` / `obspython`** (which dispatch from a safe context without holding any filter-side locks). If you are calling these procedures from a **native plugin** hotkey callback registered with `obs_hotkey_register_*`, be aware that such callbacks may be dispatched from threads that already hold locks unrelated to Branch Output; verify that your hotkey-dispatch context does not hold a lock that the Branch Output `outputMutex` or the Qt UI thread might need. The same restriction applies to `osi_branch_output_get_filter_list` below, which synchronizes with the Qt UI thread via a blocking queued connection and will deadlock if the caller holds a lock that the UI thread is waiting on.
- **Deferred application during pending states**: When recording is in `recordingPending` or a split/restart is in progress, the override is stored and applied on the next interval tick (via the internal 1-second `intervalTimer`) rather than taking effect synchronously. The proc call still returns immediately; there is no indication when the new filename format becomes active.

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

# Get the target Branch Output filter by UUID
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)

# Clear the override (pass an empty string)
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
```

**Lua sample code**

```lua
local obs = obslua

-- Get the target Branch Output filter by UUID
local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
end

-- Clear the override (pass an empty string)
local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "")
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

# Get the target Branch Output filter by UUID
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)

# Clear the override (pass an empty string)
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
```

**Lua sample code**

```lua
local obs = obslua

-- Get the target Branch Output filter by UUID
local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
end

-- Clear the override (pass an empty string)
local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "")
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

- The procedure is registered during `obs_module_post_load()`. Calling it before this point (e.g., during early OBS Studio module load) returns an empty list.
- Filters applied to **private sources** (sources not visible in the OBS frontend) are intentionally excluded from the returned list, matching the Status Dock's visibility rules.
- The returned list is a snapshot taken at call time. Callers that need to react to filter additions/removals should poll periodically or refresh on demand.
- **Thread safety**: This procedure may be called from any thread. Internally, the implementation reads the Status Dock's filter table, which must be accessed from the Qt UI thread. When called from a non-UI thread (e.g., an obs-websocket worker), the read is dispatched to the UI thread via a blocking queued connection. When called from the UI thread itself (e.g., a frontend plugin or a hotkey handler), the read is performed directly to avoid self-deadlock. Callers do not need to worry about the distinction.

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

#### recording-filename-from-text.py / recording-filename-from-text.lua

A script that reads the value of a text input and applies it to the stream recording filename format.

1. Open OBS menu → Tools → Scripts
2. Click the plus (+) button at the bottom of the Scripts dialog
3. Select the script file — either the `.py` or the `.lua` variant.
4. When you select the script in Loaded Scripts, various settings can be configured in the Description panel:
   - **Text Source** — Select the text input
   - **Branch Output Filter** — Select the Branch Output filter to override
   - **Base Filename Format** — Specify the base filename format. This format is appended to the end of the filename.
5. The override becomes active as soon as you configure these settings. Click Script Log to see the script's activity.
   Example: `[recording-filename-from-text.lua] Recording filename format updated: test %CCYY-%MM-%DD %hh-%mm-%ss`
6. When recording with the override active, the overridden filename takes precedence.
7. To disable the override, remove the script from Loaded Scripts using the trash button.

> **Behavior during recording**
>
> - Before recording starts: The overridden filename format is used when recording begins
> - During recording (with file splitting enabled): A file split is triggered immediately when the filename format changes
> - During recording (without file splitting): Recording is restarted with the new filename format

**Note:** While the override is active, the filename setting in the filter properties is not used.

**Throttling:** To avoid excessive file splits or recording restarts when the text source changes rapidly, the sample script throttles updates to at most one apply per 30 seconds per distinct text value. As a result, a change in the text source may take up to 30 seconds to be reflected in the recording filename. Edit the `THROTTLE_SECONDS` constant at the top of the script to tune this interval.

**Windows + "Read from file" — Lua limitation:** When the text source is configured to read from a file, the Lua variant uses `io.open()`, which on Windows calls the CRT `fopen()` and interprets the path in the system ANSI code page (e.g. CP932 on Japanese locale). Paths containing characters that are not representable in the ANSI code page may therefore fail to open from the Lua script. The Python variant is not affected because CPython uses wide-character Windows APIs internally. Use the Python variant when you need full UTF-8 path support on Windows.

#### replay-buffer-filename-from-text.py / replay-buffer-filename-from-text.lua

A script that reads the value of a text input and applies it to the replay buffer save filename format.

1. Open OBS menu → Tools → Scripts
2. Click the plus (+) button at the bottom of the Scripts dialog
3. Select the script file — either the `.py` or the `.lua` variant.
4. When you select the script in Loaded Scripts, various settings can be configured in the Description panel:
   - **Text Source** — Select the text input
   - **Branch Output Filter** — Select the Branch Output filter to override
   - **Base Filename Format** — Specify the base filename format. This format is appended to the end of the filename.
5. The override becomes active as soon as you configure these settings. Click Script Log to see the script's activity.
   Example: `[replay-buffer-filename-from-text.lua] Replay buffer filename format updated: test %CCYY-%MM-%DD %hh-%mm-%ss`
6. When saving with the override active, the overridden filename takes precedence.
7. To disable the override, remove the script from Loaded Scripts using the trash button.

**Note:** While the override is active, the filename setting in the filter properties is not used.

**Windows + "Read from file" — Lua limitation:** The same limitation noted for `recording-filename-from-text.lua` applies here. When the text source reads from a file whose path contains non-ANSI characters, the Lua variant may fail to open the file on Windows. Use the Python variant if full UTF-8 path support is required.
