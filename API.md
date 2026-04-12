# Branch Output API Reference

[日本語版はこちら](./API_ja.md)

## Recording Filename Override

### Overview

Recording Filename Override is a feature that allows you to override the filename format of stream recordings and replay buffer saves at runtime, using Branch Output's public procedures.

The overridden filename format is stored separately from the filter's property settings, and remains active until it is reset via procedure or OBS is closed.

This feature was implemented in response to production requirements where recorded files need to be organized according to the current scene, the value of a text input, or other external data — by overriding the filename at runtime.

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

### Retrieving the Branch Output Filter List

A global procedure that returns the list of Branch Output filters currently loaded in OBS. Since the override procedures above require the target filter's UUID, this procedure is typically used to present the filter list to the user for selection.

| Item | Description |
|------|-------------|
| Procedure name | `osi_branch_output_get_filter_list` |
| Signature | `void osi_branch_output_get_filter_list(out string json)` |
| Registered on | Global procedure handler (`obs_get_proc_handler()`) |
| Parameters | `json` (out string) — JSON string representing the list of Branch Output filters |
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

### Using the Sample Scripts

The sample scripts are written in Python. Before using them, make sure that Python Settings are properly configured in OBS under Tools → Scripts.

#### recording-filename-from-text.py

A script that reads the value of a text input and applies it to the stream recording filename format.

1. Open OBS menu → Tools → Scripts
2. Click the plus (+) button at the bottom of the Scripts dialog
3. Select the script file.
   On Windows, it is typically installed at `data\obs-plugins\osi-branch-output/scripts/recording-filename-from-text.py` under the OBS installation path.
4. When you select the script in Loaded Scripts, various settings can be configured in the Description panel:
   - **Text Source** — Select the text input
   - **Branch Output Filter** — Select the Branch Output filter to override
   - **Base Filename Format** — Specify the base filename format. This format is appended to the end of the filename.
5. The override becomes active as soon as you configure these settings. Click Script Log to see the script's activity.
   Example: `[recording-filename-from-text.py] Recording filename format updated: test %CCYY-%MM-%DD %hh-%mm-%ss`
6. When recording with the override active, the overridden filename takes precedence.
7. To disable the override, remove the script from Loaded Scripts using the trash button.

> **Behavior during recording**
>
> - Before recording starts: The overridden filename format is used when recording begins
> - During recording (with file splitting enabled): A file split is triggered immediately when the filename format changes
> - During recording (without file splitting): Recording is restarted with the new filename format

**Note:** While the override is active, the filename setting in the filter properties is not used.

#### replay-buffer-filename-from-text.py

A script that reads the value of a text input and applies it to the replay buffer save filename format.

1. Open OBS menu → Tools → Scripts
2. Click the plus (+) button at the bottom of the Scripts dialog
3. Select the script file.
   On Windows, it is typically installed at `data\obs-plugins\osi-branch-output/scripts/replay-buffer-filename-from-text.py` under the OBS installation path.
4. When you select the script in Loaded Scripts, various settings can be configured in the Description panel:
   - **Text Source** — Select the text input
   - **Branch Output Filter** — Select the Branch Output filter to override
   - **Base Filename Format** — Specify the base filename format. This format is appended to the end of the filename.
5. The override becomes active as soon as you configure these settings. Click Script Log to see the script's activity.
   Example: `[replay-buffer-filename-from-text.py] Replay buffer filename format updated: test %CCYY-%MM-%DD %hh-%mm-%ss`
6. When saving with the override active, the overridden filename takes precedence.
7. To disable the override, remove the script from Loaded Scripts using the trash button.

**Note:** While the override is active, the filename setting in the filter properties is not used.
