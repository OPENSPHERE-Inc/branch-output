"""
OBS Python Script: Replay Buffer Filename from Text Source
==========================================================
Overrides the Branch Output replay buffer filename format
based on the content of a Text (GDI+) source, without
restarting the replay buffer output.

This script overrides the filename format at runtime via proc handler.
The filter's own property settings are NOT modified.
When this script is unloaded, the override is cleared and
the filter reverts to its original filename format setting.

Requirements:
  - Branch Output plugin v1.1.0+ (with override_replay_buffer_filename_format proc)
  - A Text (GDI+) source whose "text" property will be used as the filename prefix

Usage:
  1. Add this script via OBS > Tools > Scripts
  2. Select the text source and the Branch Output filter from the dropdown lists
  3. Set the base filename format (OBS date/time specifiers like %CCYY-%MM-%DD are supported)
  4. The replay buffer filename will update whenever the text source content changes
"""

import json
import obspython as obs

# Script settings
text_source_uuid = ""
selected_filter = ""  # "source_uuid::filter_uuid" format
base_format = "%CCYY-%MM-%DD %hh-%mm-%ss"
last_text = None


def get_branch_output_filters():
    """Get list of Branch Output filters via global proc handler."""
    filters = []
    ph = obs.obs_get_proc_handler()
    cd = obs.calldata_create()

    if obs.proc_handler_call(ph, "osi_branch_output_get_filter_list", cd):
        json_str = obs.calldata_string(cd, "json")
        if json_str:
            try:
                data = json.loads(json_str)
                for item in data.get("filters", []):
                    source_name = item.get("source_name", "")
                    source_uuid = item.get("source_uuid", "")
                    filter_name = item.get("filter_name", "")
                    filter_uuid = item.get("filter_uuid", "")
                    if source_uuid and filter_uuid:
                        filters.append((source_name, source_uuid, filter_name, filter_uuid))
            except json.JSONDecodeError:
                obs.script_log(obs.LOG_WARNING, "Failed to parse filter list JSON")

    obs.calldata_free(cd)
    return filters


def parse_selected_filter(value):
    """Parse 'source_uuid::filter_uuid' into (source_uuid, filter_uuid)."""
    if "::" in value:
        parts = value.split("::", 1)
        return parts[0], parts[1]
    return "", ""


def update_replay_buffer_format():
    """Read text source and update Branch Output replay buffer filename format."""
    global last_text

    if not text_source_uuid or not selected_filter:
        return

    source_uuid, filter_uuid = parse_selected_filter(selected_filter)
    if not source_uuid or not filter_uuid:
        return

    # Get text from the text source
    text_source = obs.obs_get_source_by_uuid(text_source_uuid)
    if not text_source:
        return

    settings = obs.obs_source_get_settings(text_source)
    current_text = obs.obs_data_get_string(settings, "text")
    obs.obs_data_release(settings)
    obs.obs_source_release(text_source)

    # Skip if text hasn't changed
    if current_text == last_text:
        return
    last_text = current_text

    # Sanitize the text for use in filenames
    sanitized = current_text.strip()
    for ch in ['<', '>', ':', '"', '|', '?', '*', '/', '\\']:
        sanitized = sanitized.replace(ch, '-')

    # Build the new format string
    if sanitized:
        new_format = f"{sanitized} {base_format}"
    else:
        new_format = base_format

    # Get the Branch Output filter by UUID
    video_source = obs.obs_get_source_by_uuid(source_uuid)
    if not video_source:
        obs.script_log(obs.LOG_WARNING, f"Video source (uuid: {source_uuid}) not found")
        return

    bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
    if not bo_filter:
        obs.script_log(obs.LOG_WARNING, f"Filter (uuid: {filter_uuid}) not found")
        obs.obs_source_release(video_source)
        return

    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", new_format)
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)

    obs.obs_source_release(bo_filter)
    obs.obs_source_release(video_source)

    obs.script_log(obs.LOG_INFO, f"Replay buffer filename format updated: {new_format}")


def timer_callback():
    update_replay_buffer_format()


# --- OBS Script Interface ---

def script_description():
    return (
        "<b>Replay Buffer Filename from Text Source</b><br><br>"
        "Overrides a Branch Output replay buffer's filename format "
        "based on the content of a Text (GDI+) source.<br><br>"
        "The text content is prepended to the base format on every change.<br><br>"
        "<b>Note:</b> This script overrides the filename format at runtime. "
        "The filter's own property settings are not modified. "
        "When this script is unloaded, the override is cleared and "
        "the filter reverts to its original filename format setting."
    )


def script_properties():
    props = obs.obs_properties_create()

    # Text source selector
    p = obs.obs_properties_add_list(
        props, "text_source", "Text Source",
        obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING,
    )
    obs.obs_property_list_add_string(p, "(Select a text source)", "")
    sources = obs.obs_enum_sources()
    if sources:
        for source in sources:
            source_id = obs.obs_source_get_unversioned_id(source)
            if source_id in ("text_gdiplus", "text_gdiplus_v2", "text_gdiplus_v3",
                             "text_ft2_source", "text_ft2_source_v2"):
                name = obs.obs_source_get_name(source)
                uuid = obs.obs_source_get_uuid(source)
                obs.obs_property_list_add_string(p, name, uuid)
        obs.source_list_release(sources)

    # Branch Output filter selector (populated from global proc handler)
    p = obs.obs_properties_add_list(
        props, "selected_filter", "Branch Output Filter",
        obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING,
    )
    obs.obs_property_list_add_string(p, "(Select a filter)", "")
    for source_name, source_uuid, filter_name, filter_uuid in get_branch_output_filters():
        label = f"{filter_name}  ({source_name})"
        value = f"{source_uuid}::{filter_uuid}"
        obs.obs_property_list_add_string(p, label, value)

    # Base filename format
    obs.obs_properties_add_text(
        props, "base_format", "Base Filename Format",
        obs.OBS_TEXT_DEFAULT,
    )

    return props


def script_defaults(settings):
    obs.obs_data_set_default_string(settings, "base_format", "%CCYY-%MM-%DD %hh-%mm-%ss")


def script_update(settings):
    global text_source_uuid, selected_filter, base_format, last_text

    text_source_uuid = obs.obs_data_get_string(settings, "text_source")
    selected_filter = obs.obs_data_get_string(settings, "selected_filter")
    base_format = obs.obs_data_get_string(settings, "base_format")

    # Reset last_text to force update on next tick
    last_text = None


def script_load(settings):
    obs.timer_add(timer_callback, 1000)


def clear_override():
    """Clear the filename format override by sending empty string."""
    if not selected_filter:
        return

    source_uuid, filter_uuid = parse_selected_filter(selected_filter)
    if not filter_uuid:
        return

    bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
    if not bo_filter:
        return

    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)

    obs.obs_source_release(bo_filter)

    obs.script_log(obs.LOG_INFO, "Replay buffer filename format override cleared")


def script_unload():
    obs.timer_remove(timer_callback)
    clear_override()
