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

Note on throttling:
  Unlike the recording variant, this script does NOT throttle updates.
  Replay buffer saves are on-demand (triggered by user), so the override
  only takes effect at the next save operation. Applying an override more
  frequently has no cost beyond a cheap proc call, so throttling would
  only add latency without benefit.

Requirements:
  - Branch Output plugin v1.0.9+ (with override_replay_buffer_filename_format proc)
  - A Text (GDI+) source whose "text" property will be used as the filename prefix

Usage:
  1. Add this script via OBS > Tools > Scripts
  2. Select the text source and the Branch Output filter from the dropdown lists
  3. Set the base filename format (OBS date/time specifiers like %CCYY-%MM-%DD are supported)
  4. The replay buffer filename will update whenever the text source content changes
"""

import json
import obspython as obs

# Proc and filter constants
OVERRIDE_PROC = "override_replay_buffer_filename_format"
BRANCH_OUTPUT_FILTER_ID = "osi_branch_output"
LOG_LABEL = "Replay buffer filename format"
MAX_READ_SIZE = 4096  # 4 KB read limit to prevent performance issues on large files

# Windows reserved device names (case-insensitive).
WINDOWS_RESERVED = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
}

# Script settings
text_source_uuid = ""
selected_filter = ""  # "source_uuid::filter_uuid" format
base_format = "%CCYY-%MM-%DD %hh-%mm-%ss"
last_text = None
override_cleared = False


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


def sanitize_filename(text):
    """Produce a filesystem-safe prefix.

    1. Replace all control characters (incl. CR/LF/TAB) with a single space
    2. Collapse runs of whitespace to a single space and trim
    3. Replace filesystem-unsafe characters with "-"
    4. Strip trailing dots/spaces (Windows disallows these at end of filename)
    5. Prefix an underscore if the result collides with a Windows reserved name
    """
    if text is None:
        return ""
    # Replace control characters with a space
    cleaned = "".join(ch if ch.isprintable() or ch == " " else " " for ch in text)
    # Collapse runs of whitespace and trim
    trimmed = " ".join(cleaned.split())
    # Replace filesystem-unsafe characters
    sanitized = trimmed
    for ch in ('<', '>', ':', '"', '|', '?', '*', '/', '\\'):
        sanitized = sanitized.replace(ch, '-')
    # Strip trailing dots/spaces
    sanitized = sanitized.rstrip(". ")
    # Prefix underscore for Windows reserved names. Windows treats reserved
    # device names as reserved even when followed by an extension
    # (e.g. "CON.txt"), so also check the portion before the first dot.
    base_before_dot = sanitized.split(".", 1)[0]
    if sanitized.upper() in WINDOWS_RESERVED or base_before_dot.upper() in WINDOWS_RESERVED:
        sanitized = "_" + sanitized
    return sanitized


def read_text_from_source(text_source):
    """Read text from a Text (GDI+) source.

    Returns (text, ok). ok = False means the caller should clear the override.
    """
    settings = obs.obs_source_get_settings(text_source)
    result_text = None
    ok = True

    try:
        read_from_file = obs.obs_data_get_bool(settings, "read_from_file")
        if read_from_file:
            file_path = obs.obs_data_get_string(settings, "file")
            if not file_path:
                obs.script_log(obs.LOG_WARNING,
                               "Text source is set to 'read from file' but no file path is configured; clearing override")
                ok = False
            else:
                try:
                    # Open in binary mode so BOM bytes are not translated.
                    # Limit read size to prevent performance issues on accidental large-file selection.
                    with open(file_path, "rb") as f:
                        data = f.read(MAX_READ_SIZE)
                    # UTF-8 BOM: strip it.
                    if data[:3] == b"\xef\xbb\xbf":
                        data = data[3:]
                    # UTF-16 LE/BE BOM: not supported by this sample; warn and clear.
                    elif data[:2] in (b"\xff\xfe", b"\xfe\xff"):
                        obs.script_log(obs.LOG_WARNING,
                                       "UTF-16 text files are not supported; please save the text file as UTF-8")
                        ok = False
                    if ok:
                        try:
                            result_text = data.decode("utf-8")
                        except UnicodeDecodeError as e:
                            obs.script_log(obs.LOG_WARNING, f"Failed to decode text file as UTF-8: {e}")
                            ok = False
                except OSError as e:
                    obs.script_log(obs.LOG_WARNING, f"Failed to read text file: {e}")
                    ok = False
        else:
            result_text = obs.obs_data_get_string(settings, "text")
    finally:
        obs.obs_data_release(settings)

    return result_text, ok


def call_override_proc(filter_uuid, format_value):
    """Call the override proc on the specified Branch Output filter.

    Verifies that the target is actually a Branch Output filter and logs proc result.
    Returns True on success, False otherwise.
    """
    if not filter_uuid:
        return False

    bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
    if not bo_filter:
        obs.script_log(obs.LOG_WARNING, f"Filter (uuid: {filter_uuid}) not found")
        return False

    try:
        filter_id = obs.obs_source_get_id(bo_filter)
        if filter_id != BRANCH_OUTPUT_FILTER_ID:
            obs.script_log(obs.LOG_WARNING,
                           f"Source (uuid: {filter_uuid}) is not a Branch Output filter (id: {filter_id})")
            return False

        ph = obs.obs_source_get_proc_handler(bo_filter)
        cd = obs.calldata_create()
        obs.calldata_set_string(cd, "format", format_value)
        result = obs.proc_handler_call(ph, OVERRIDE_PROC, cd)
        obs.calldata_free(cd)

        if not result:
            obs.script_log(obs.LOG_WARNING,
                           f"proc_handler_call for {OVERRIDE_PROC} failed — is the Branch Output plugin up to date?")
            return False
        return True
    finally:
        obs.obs_source_release(bo_filter)


def update_replay_buffer_format():
    """Read text source and update Branch Output replay buffer filename format."""
    global last_text, override_cleared

    if not text_source_uuid or not selected_filter:
        return

    _, filter_uuid = parse_selected_filter(selected_filter)
    if not filter_uuid:
        return

    # Get text from the text source
    text_source = obs.obs_get_source_by_uuid(text_source_uuid)
    if not text_source:
        # Text source not available: clear override (only once per state change)
        if not override_cleared:
            clear_override()
        return

    try:
        current_text, ok = read_text_from_source(text_source)
    finally:
        obs.obs_source_release(text_source)

    if not ok or current_text is None:
        if not override_cleared:
            clear_override()
        return

    # Skip if text hasn't changed
    if current_text == last_text:
        return
    last_text = current_text

    # Build the new format string
    sanitized = sanitize_filename(current_text)
    if sanitized:
        new_format = f"{sanitized} {base_format}"
    else:
        new_format = base_format

    if call_override_proc(filter_uuid, new_format):
        override_cleared = False
        obs.script_log(obs.LOG_INFO, f"{LOG_LABEL} updated: {new_format}")


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
            # text_gdiplus_v3 / text_ft2_source_v2 are listed as a forward-
            # compatibility reserve; OBS 30.1.x currently only ships up to
            # text_gdiplus_v2 and text_ft2_source_v2. Kept as an explicit
            # allowlist (rather than a startswith("text_") match) so that new
            # unrelated source types cannot be picked up accidentally.
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
    global text_source_uuid, selected_filter, base_format, last_text, override_cleared

    # Remember the previously selected filter so we can clear its override
    # if the user changed the selection.
    _, old_filter_uuid = parse_selected_filter(selected_filter)

    text_source_uuid = obs.obs_data_get_string(settings, "text_source")
    selected_filter = obs.obs_data_get_string(settings, "selected_filter")
    base_format = obs.obs_data_get_string(settings, "base_format")

    _, new_filter_uuid = parse_selected_filter(selected_filter)
    if old_filter_uuid and old_filter_uuid != new_filter_uuid:
        # Clear override on the previously selected filter so it does not
        # remain overridden after the user switched to a different filter.
        if call_override_proc(old_filter_uuid, ""):
            obs.script_log(obs.LOG_INFO,
                           f"{LOG_LABEL} override cleared on previous filter (uuid: {old_filter_uuid})")

    # Reset state to force update on next tick
    last_text = None
    override_cleared = False


def script_load(settings):
    # Defensive timer_remove in case of script reload.
    obs.timer_remove(timer_callback)
    # script_update(settings) is called automatically by OBS after
    # script_load, so we don't need to invoke it explicitly here.
    obs.timer_add(timer_callback, 1000)


def clear_override():
    """Clear the filename format override by sending empty string.

    Sets override_cleared = True to suppress redundant proc calls on
    subsequent timer ticks until a new format is applied or the
    selection changes. Also reset last_text so that if the text source
    reappears later with the same content as before, the override is
    re-applied.
    """
    global override_cleared, last_text

    last_text = None

    if not selected_filter:
        override_cleared = True
        return

    _, filter_uuid = parse_selected_filter(selected_filter)
    if not filter_uuid:
        override_cleared = True
        return

    if call_override_proc(filter_uuid, ""):
        obs.script_log(obs.LOG_INFO, f"{LOG_LABEL} override cleared")
    override_cleared = True


def script_unload():
    obs.timer_remove(timer_callback)
    clear_override()
