# Branch Output API Reference

[日本語版はこちら](./API_ja.md)

## Recording Filename Override

### Overview

Recording Filename Override lets you override the filename format of stream recordings and replay buffer saves at runtime via Branch Output's public procedures.

The overridden format is stored separately from the filter's property settings and stays active until reset via procedure or until OBS exits.

The feature exists for production setups that need to organize recorded files by current scene, the value of a text input, or other external data.

For caveats that apply to every procedure below (thread/callback safety, latency, deferred application, registration timing, module unload), see [Known Limitations](#known-limitations).

### Obtaining the Filter Source

The per-filter override procedures (`override_recording_filename_format`, `override_replay_buffer_filename_format`) are registered on each Branch Output filter source — not on the parent source/scene. To call them you need a reference to the filter source itself.

Typical acquisition flow:

1. Call `osi_branch_output_get_filter_list` (described below) to obtain UUIDs of all loaded Branch Output filters.
2. Get a source reference via `obs_get_source_by_uuid(filter_uuid)`.
3. Get the proc handler via `obs_source_get_proc_handler(filter_source)`.
4. Release the source with `obs_source_release()` after use.

If you already have a parent source reference, `obs_source_get_filter_by_name(parent, filter_name)` works too.

### Overriding the Stream Recording Filename Format

A procedure on the Branch Output filter source that overrides the stream recording filename format at runtime.

| Item | Description |
|------|-------------|
| Procedure name | `override_recording_filename_format` |
| Signature | `void override_recording_filename_format(in string format)` |
| Registered on | Branch Output filter source (`obs_source_get_proc_handler(filter_source)`) |
| Parameters | `format` (string) — New filename format. OBS date/time specifiers (e.g., `%CCYY-%MM-%DD %hh-%mm-%ss`) are supported. **Passing an empty string clears the override and reverts to the filename format configured in the filter properties.** |
| Returns | None |

Behavior by recording state:

- Before recording starts: the overridden format is used when recording begins.
- Recording in progress, file splitting enabled: a file split is triggered immediately on format change.
- Recording in progress, file splitting disabled: recording is restarted with the new format.

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

A procedure on the Branch Output filter source that overrides the replay buffer save filename format.

| Item | Description |
|------|-------------|
| Procedure name | `override_replay_buffer_filename_format` |
| Signature | `void override_replay_buffer_filename_format(in string format)` |
| Registered on | Branch Output filter source (`obs_source_get_proc_handler(filter_source)`) |
| Parameters | `format` (string) — New filename format. OBS date/time specifiers (e.g., `%CCYY-%MM-%DD %hh-%mm-%ss`) are supported. **Passing an empty string clears the override and reverts to the filename format configured in the filter properties.** |
| Returns | None |

The new format takes effect on the next replay buffer save. The replay buffer itself is not restarted.

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

A global procedure that returns the list of Branch Output filters currently loaded. Used to obtain target filter UUIDs for the override procedures above.

| Item | Description |
|------|-------------|
| Procedure name | `osi_branch_output_get_filter_list` |
| Signature | `void osi_branch_output_get_filter_list(out string json)` |
| Registered on | Global procedure handler (`obs_get_proc_handler()`) |
| Parameters | `json` (out string) — JSON string with the filter list |
| Returns | None |

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
| `source_name` | Parent source/scene name |
| `source_uuid` | Parent source/scene UUID |
| `filter_name` | Branch Output filter name |
| `filter_uuid` | Branch Output filter UUID (used for the override procedures) |

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

Lua has no built-in JSON parser, so we use OBS's `obs_data_create_from_json()` to parse the returned string.

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

Two variants are bundled: **Python** (`.py`) and **Lua** (`.lua`). Both implement the same functionality — choose whichever you prefer.

- Python scripts require Python Settings to be configured under **Tools → Scripts**.
- Lua scripts need no extra configuration.

Install path: `data\obs-plugins\osi-branch-output\scripts\` under the OBS install directory on Windows; the standard OBS plugin data directory on macOS / Linux.

#### Common setup

1. OBS menu → **Tools → Scripts**.
2. Click the **+** button at the bottom.
3. Select the script file (`.py` or `.lua`).
4. In the Description panel, configure:
   - **Text Source** — text input to read.
   - **Branch Output Filter** — filter to override.
   - **Base Filename Format** — base format appended to the filename.
5. The override is active immediately. Check **Script Log** for activity.
6. While the script is loaded, its override takes precedence over the filter property value.
7. Remove the script via the trash button to disable.

#### recording-filename-from-text

Reads the text input value and applies it to the stream recording filename format. See [behavior by recording state](#overriding-the-stream-recording-filename-format) above.

To avoid excessive splits/restarts on rapid text changes, updates are throttled to one apply per 30 seconds per distinct text value. Adjust `THROTTLE_SECONDS` at the top of the script to tune.

#### replay-buffer-filename-from-text

Reads the text input value and applies it to the replay buffer save filename format. The override takes effect on the next save; the replay buffer is not restarted.

## Known Limitations

The following caveats apply to all procedures and sample scripts above.

- **Thread safety**: Procedures are callable from any thread.
- **Callback safety**: Do not call from OBS signal callbacks (`obs_source_signal`, `obs_output_signal`, etc.) or frontend event callbacks (`obs_frontend_event_callback`) — deadlock may occur. Safe callers: script timers, hotkey handlers, UI event handlers. Ensure no Branch Output lock is held at the call site when calling from a hotkey callback.
- **Latency**: Calls are not constant-time (a recording restart or file split may be triggered). Avoid latency-sensitive hot paths.
- **Deferred application**: If recording is transitioning (pending, split, or restart in progress), the override is stored and applied with up to ~1 second of delay. The proc call returns immediately; there is no notification when the new format becomes active.
- **Registration timing**: `osi_branch_output_get_filter_list` is registered during `obs_module_post_load()`. Earlier calls return `false` from `proc_handler_call()` and leave the `out` parameter unwritten — always check the return value before reading.
- **Module unload**: Do not call any procedure after `obs_module_unload()` — behavior is undefined.
- **Private sources excluded**: `osi_branch_output_get_filter_list` omits filters on sources not visible in the OBS frontend, matching the Status Dock's visibility rules.
- **Snapshot semantics**: The filter list is a snapshot at call time. Poll or refresh on demand to detect additions/removals.
- **Windows + "Read from file" — Lua sample only**: When the text source reads from a file on Windows, the Lua sample uses `io.open()`, which interprets the path in the system ANSI code page (e.g., CP932 on Japanese locales). Paths containing characters outside that code page may fail to open. On open failure the script clears the override and writes a `Failed to read text file` warning to the OBS Script Log. The Python sample is unaffected (CPython uses wide-character Windows APIs); use it if full UTF-8 path support is required.
