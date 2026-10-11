# Branch Output API Reference

[日本語版はこちら](./API_ja.md)

## Recording Filename Override

### Overview

Recording Filename Override lets you override the filename format of stream recordings and replay buffer saves at runtime via Branch Output's public procedures.

The overridden format is stored in the instance of the Branch Output's source, separately from its
property settings. It stays active until reset via procedure, until the source is recreated (for
example, when a deleted filter is restored with undo, or when the profile of a main output is loaded
again), or until OBS exits.

The feature exists for production setups that need to organize recorded files by current scene, the value of a text input, or other external data.

For caveats that apply to every procedure below (thread/callback safety, latency, deferred
application, registration timing, module unload, main outputs), see
[Known Limitations](#known-limitations).

### Obtaining the Target Source

The override procedures (`override_recording_filename_format`,
`override_replay_buffer_filename_format`) are registered on the source of each Branch Output.
To call them you need a reference to that source.

- Branch Output filter: the filter source itself — not the parent source/scene.
- Main output (1.1.0 or later): the main output's source.
  - It is a private source that does not appear in the OBS frontend.

Typical acquisition flow:

1. Obtain the UUID of the target source with one of the list procedures described below.
   - `output_uuid` from `osi_branch_output_get_output_list` (1.1.0 or later): filters and main
     outputs.
   - `filter_uuid` from `osi_branch_output_get_filter_list`: filters only.
2. Get a source reference via `obs_get_source_by_uuid(uuid)`.
3. Get the proc handler via `obs_source_get_proc_handler(source)`.
4. Release the source with `obs_source_release()` after use.

For a filter, if you already have a parent source reference,
`obs_source_get_filter_by_name(parent, filter_name)` works too.

### Overriding the Stream Recording Filename Format

A procedure on the Branch Output source (filter or main output) that overrides the stream recording filename format at runtime.

| Item | Description |
|------|-------------|
| Procedure name | `override_recording_filename_format` |
| Signature | `void override_recording_filename_format(in string format)` |
| Registered on | Branch Output filter source, or main output source (1.1.0 or later) (`obs_source_get_proc_handler(source)`) |
| Parameters | `format` (string) — New filename format. OBS date/time specifiers (e.g., `%CCYY-%MM-%DD %hh-%mm-%ss`) are supported. **Passing an empty string clears the override and reverts to the filename format configured in the Branch Output's properties.** |
| Returns | None |

Behavior by recording state:

- Before recording starts: the overridden format is used when recording begins.
- Recording in progress, file splitting enabled: a file split is triggered immediately on format
  change. If the recording has not written any data yet (for example, right after it starts), the
  split is deferred until it has written data.
- Recording in progress, file splitting disabled: recording is restarted with the new format.

**Python sample code**

```python
import obspython as obs

# Get the target Branch Output source (filter or main output) by UUID.
# Pass "" as format to clear.
bo_source = obs.obs_get_source_by_uuid(target_uuid)
if bo_source:
    ph = obs.obs_source_get_proc_handler(bo_source)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_destroy(cd)
    obs.obs_source_release(bo_source)
```

**Lua sample code**

```lua
local obs = obslua

-- Get the target Branch Output source (filter or main output) by UUID.
-- Pass "" as format to clear.
local bo_source = obs.obs_get_source_by_uuid(target_uuid)
if bo_source ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_source)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_destroy(cd)
    obs.obs_source_release(bo_source)
end
```

### Overriding the Replay Buffer Save Filename Format

A procedure on the Branch Output source (filter or main output) that overrides the replay buffer save filename format.

| Item | Description |
|------|-------------|
| Procedure name | `override_replay_buffer_filename_format` |
| Signature | `void override_replay_buffer_filename_format(in string format)` |
| Registered on | Branch Output filter source, or main output source (1.1.0 or later) (`obs_source_get_proc_handler(source)`) |
| Parameters | `format` (string) — New filename format. OBS date/time specifiers (e.g., `%CCYY-%MM-%DD %hh-%mm-%ss`) are supported. **Passing an empty string clears the override and reverts to the filename format configured in the Branch Output's properties.** |
| Returns | None |

The new format takes effect on the next replay buffer save. The replay buffer itself is not restarted.

**Python sample code**

```python
import obspython as obs

# Get the target Branch Output source (filter or main output) by UUID.
# Pass "" as format to clear.
bo_source = obs.obs_get_source_by_uuid(target_uuid)
if bo_source:
    ph = obs.obs_source_get_proc_handler(bo_source)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_destroy(cd)
    obs.obs_source_release(bo_source)
```

**Lua sample code**

```lua
local obs = obslua

-- Get the target Branch Output source (filter or main output) by UUID.
-- Pass "" as format to clear.
local bo_source = obs.obs_get_source_by_uuid(target_uuid)
if bo_source ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_source)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_destroy(cd)
    obs.obs_source_release(bo_source)
end
```

### Retrieving the Branch Output Filter List

A global procedure that returns the list of Branch Output filters currently loaded. Used to obtain target filter UUIDs for the override procedures above.

Main outputs are not included.
To list both filters and main outputs, use `osi_branch_output_get_output_list` (1.1.0 or later,
see [Retrieving the Branch Output List](#retrieving-the-branch-output-list)).

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

    obs.calldata_destroy(cd)
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

    obs.calldata_destroy(cd)
    return filters
end
```

### Retrieving the Branch Output List

A global procedure that returns the list of Branch Outputs currently loaded: both filters and main
outputs. Available in 1.1.0 or later. Used to obtain target UUIDs for the override procedures above.

| Item | Description |
|------|-------------|
| Procedure name | `osi_branch_output_get_output_list` |
| Signature | `void osi_branch_output_get_output_list(out string json)` |
| Registered on | Global procedure handler (`obs_get_proc_handler()`) |
| Parameters | `json` (out string) — JSON string with the Branch Output list |
| Returns | None |

**Returned JSON structure**

```json
{
  "outputs": [
    {
      "type": "filter",
      "output_uuid": "87654321-4321-4321-4321-cba987654321",
      "source_name": "Main Scene",
      "source_uuid": "12345678-1234-1234-1234-123456789abc",
      "filter_name": "Branch Output 1",
      "filter_uuid": "87654321-4321-4321-4321-cba987654321"
    },
    {
      "type": "program",
      "output_uuid": "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0",
      "output_name": "Main Output 1"
    },
    ...
  ]
}
```

| Field | Present for `type` | Description |
|-------|--------------------|-------------|
| `type` | All | `filter` for a Branch Output filter, `program` for a main output |
| `output_uuid` | All | UUID of the source the override procedures are registered on (used for the override procedures) |
| `source_name` | `filter` | Parent source/scene name |
| `source_uuid` | `filter` | Parent source/scene UUID |
| `filter_name` | `filter` | Branch Output filter name |
| `filter_uuid` | `filter` | Branch Output filter UUID (same value as `output_uuid`) |
| `output_name` | `program` | Main output name |

- New `type` values may be added in later versions. Skip entries whose `type` you do not recognize.
- The order of the entries is not guaranteed.
- On a plugin older than 1.1.0 this procedure is not registered, so `proc_handler_call()` returns
  `false`. Use `osi_branch_output_get_filter_list` in that case.

**Python sample code**

Returns `(label, uuid)` tuples, falling back to `osi_branch_output_get_filter_list` on a plugin older
than 1.1.0.

```python
import json
import obspython as obs

def call_global_list_proc(proc_name):
    ph = obs.obs_get_proc_handler()
    cd = obs.calldata_create()
    try:
        found = obs.proc_handler_call(ph, proc_name, cd)
        json_str = obs.calldata_string(cd, "json") if found else None
    finally:
        obs.calldata_destroy(cd)
    return found, json_str

def parse_list(json_str, list_key):
    outputs = []
    if not json_str:
        return outputs
    try:
        data = json.loads(json_str)
    except json.JSONDecodeError:
        obs.script_log(obs.LOG_WARNING, "Failed to parse Branch Output list JSON")
        return outputs

    for item in data.get(list_key, []):
        # get_filter_list entries have no "type"
        item_type = item.get("type", "") if list_key == "outputs" else "filter"
        if item_type == "filter":
            label = f'{item.get("source_name", "")} / {item.get("filter_name", "")}'
            outputs.append((label, item.get("filter_uuid", "")))
        elif item_type == "program":
            label = f'[Main Output] {item.get("output_name", "")}'
            outputs.append((label, item.get("output_uuid", "")))
        # Entries of any other type are skipped
    return outputs

def get_branch_outputs():
    found, json_str = call_global_list_proc("osi_branch_output_get_output_list")
    if found:
        return parse_list(json_str, "outputs")
    # Plugin older than 1.1.0: filters only
    found, json_str = call_global_list_proc("osi_branch_output_get_filter_list")
    if found:
        return parse_list(json_str, "filters")
    return []
```

**Lua sample code**

Returns `{label = ..., uuid = ...}` tables, falling back to `osi_branch_output_get_filter_list` on a
plugin older than 1.1.0.

```lua
local obs = obslua

local function call_global_list_proc(proc_name)
    local ph = obs.obs_get_proc_handler()
    local cd = obs.calldata_create()
    local found = obs.proc_handler_call(ph, proc_name, cd)
    local json_str = nil
    if found then
        json_str = obs.calldata_string(cd, "json")
    end
    obs.calldata_destroy(cd)
    return found, json_str
end

local function parse_list(json_str, list_key)
    local outputs = {}
    if json_str == nil or json_str == "" then
        return outputs
    end
    local data = obs.obs_data_create_from_json(json_str)
    if data == nil then
        obs.script_log(obs.LOG_WARNING, "Failed to parse Branch Output list JSON")
        return outputs
    end

    local array = obs.obs_data_get_array(data, list_key)
    local count = obs.obs_data_array_count(array)
    for i = 0, count - 1 do
        local item = obs.obs_data_array_item(array, i)
        -- get_filter_list entries have no "type"
        local item_type = "filter"
        if list_key == "outputs" then
            item_type = obs.obs_data_get_string(item, "type")
        end
        if item_type == "filter" then
            table.insert(outputs, {
                label = obs.obs_data_get_string(item, "source_name") .. " / "
                    .. obs.obs_data_get_string(item, "filter_name"),
                uuid = obs.obs_data_get_string(item, "filter_uuid"),
            })
        elseif item_type == "program" then
            table.insert(outputs, {
                label = "[Main Output] " .. obs.obs_data_get_string(item, "output_name"),
                uuid = obs.obs_data_get_string(item, "output_uuid"),
            })
        end
        -- Entries of any other type are skipped
        obs.obs_data_release(item)
    end
    obs.obs_data_array_release(array)
    obs.obs_data_release(data)
    return outputs
end

function get_branch_outputs()
    local found, json_str = call_global_list_proc("osi_branch_output_get_output_list")
    if found then
        return parse_list(json_str, "outputs")
    end
    -- Plugin older than 1.1.0: filters only
    found, json_str = call_global_list_proc("osi_branch_output_get_filter_list")
    if found then
        return parse_list(json_str, "filters")
    end
    return {}
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
   - **Branch Output** — filter or main output to override.
     - Filters are labeled `<source name> / <filter name>`, and main outputs
       `[Main Output] <output name>`.
     - With a plugin older than 1.1.0, only filters are listed.
   - **Base Filename Format** — base format appended to the filename.
5. The override is active immediately. Check **Script Log** for activity.
6. While the script is loaded, its override takes precedence over the Branch Output's property value.
   - For a main output, the script applies the override again after the main output's source is
     recreated (for example, when switching back to its profile), and waits for the main output to
     load at OBS startup.
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
- **Deferred application**: If recording is transitioning (just started, pending, split, or restart
  in progress) or paused, the override is stored and applied later. It is typically applied within
  about 1 second, but right after recording starts it waits until the recording has written its
  first data, and while recording is paused it waits until recording is unpaused. The proc call
  returns immediately; there is no notification when the new format becomes active.
- **Registration timing**: `osi_branch_output_get_filter_list` and
  `osi_branch_output_get_output_list` are registered during `obs_module_post_load()`. Earlier calls
  return `false` from `proc_handler_call()` and leave the `out` parameter unwritten — always check
  the return value before reading.
- **Module unload**: Do not call any procedure after `obs_module_unload()` — behavior is undefined.
- **Private sources excluded**: Both list procedures omit filters on sources not visible in the OBS
  frontend, matching the Status Dock's visibility rules. Main outputs are implemented as private
  sources but are included in `osi_branch_output_get_output_list`.
- **Snapshot semantics**: The lists are snapshots at call time. Poll or refresh on demand to detect
  additions/removals. While a scene collection or profile is being switched, an output may be
  temporarily missing from the lists.
- **Main outputs**:
  - Main outputs belong to the current profile and are saved with it. Switching to another profile
    replaces them with the main outputs of that profile, so a script finds a selected main output
    only while its profile is current.
  - Unlike a filter, a main output's source is kept across scene collection switches. It is
    recreated when its profile is loaded again, which clears the override.
  - The UUID can stay the same across the recreation, and a profile switch can also keep the
    source. Detect the replacement of the instance (for example, with a weak reference) and apply
    the override again.
  - Resending the override on events alone can split or restart a recording in progress. The
    bundled sample scripts detect the replacement with a weak reference.
  - Hold a strong reference to a main output's source only during a call, and keep a weak
    reference otherwise. A reference still held when the main output is loaded again keeps the old
    instance alive: its weak reference does not expire, and the loaded main output gets a new UUID.
    An open properties dialog of the main output has the same effect; select the main output again
    in that case.
  - A script may not find a main output for a while after OBS starts. Poll until it is found
    instead of relying on a frontend event, as the bundled sample scripts do.
  - On OBS 30.x, duplicating a scene collection reassigns the UUIDs of the main outputs too. A main
    output selected by a script in any scene collection, including the new one, is no longer found
    and must be selected again.
- **Windows + "Read from file" — Lua sample only**: When the text source reads from a file on Windows, the Lua sample uses `io.open()`, which interprets the path in the system ANSI code page (e.g., CP932 on Japanese locales). Paths containing characters outside that code page may fail to open. On open failure the script clears the override and writes a `Failed to read text file` warning to the OBS Script Log. The Python sample is unaffected (CPython uses wide-character Windows APIs); use it if full UTF-8 path support is required.
