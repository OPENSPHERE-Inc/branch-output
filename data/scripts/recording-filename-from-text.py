"""
OBS Python Script: Recording Filename from Text Source
======================================================
Overrides the Branch Output recording filename format
based on the content of a Text (GDI+) source.

This script overrides the filename format at runtime via proc handler.
The filter's own property settings are NOT modified.
When this script is unloaded, the override is cleared and
the filter reverts to its original filename format setting.

The filename format is applied in three ways depending on recording state:
  - Before recording: stored and used when recording starts
  - During recording with file splitting: triggers immediate file split
  - During recording without file splitting: recording is restarted with new filename

Requirements:
  - Branch Output plugin v1.0.9+ (with override_recording_filename_format proc)
  - A Text (GDI+) source whose "text" property will be used as the filename prefix

Usage:
  1. Add this script via OBS > Tools > Scripts
  2. Select the text source and the Branch Output filter from the dropdown lists
  3. Set the base filename format (OBS date/time specifiers like %CCYY-%MM-%DD are supported)
  4. The recording filename will update whenever the text source content changes
"""

import json
import time
import unicodedata
import obspython as obs

# Proc and filter constants
OVERRIDE_PROC = "override_recording_filename_format"
BRANCH_OUTPUT_FILTER_ID = "osi_branch_output"
LOG_LABEL = "Recording filename format"
MAX_READ_SIZE = 4096  # 4 KB read limit to prevent performance issues on large files
# Truncate to 200 bytes to stay within NTFS filename component (255) /
# MAX_PATH (260) limits, leaving room for the base format and extension.
MAX_FILENAME_BYTES = 200
THROTTLE_SECONDS = 30

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
last_applied_text = None
last_applied_time = 0.0
override_cleared = False


def get_branch_output_filters():
    """Get list of Branch Output filters via global proc handler."""
    filters = []
    ph = obs.obs_get_proc_handler()
    cd = obs.calldata_create()

    try:
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
                except json.JSONDecodeError as e:
                    obs.script_log(obs.LOG_WARNING, f"Failed to parse filter list JSON: {e}")
    finally:
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

    1. Replace all Unicode "Other" category characters (Cc/Cf/Cs/Cn/Co),
       including CR/LF/TAB and zero-width format controls, with a single space
    2. Collapse runs of whitespace to a single space and trim
    3. Replace filesystem-unsafe characters with "-"
    4. Strip trailing dots/spaces (Windows disallows these at end of filename)
    5. Prefix an underscore if the result collides with a Windows reserved name
    """
    if text is None:
        return ""
    # Fold Unicode "Other" category (Cc/Cf/Cs/Cn/Co) to space. Keeps the
    # literal space explicitly so the following split()/join() collapses
    # runs, and avoids str.isprintable() which would leak ZWJ/BiDi marks.
    cleaned = "".join(
        ch if ch == " " or unicodedata.category(ch)[0] != "C" else " "
        for ch in text
    )
    # Collapse runs of whitespace and trim
    trimmed = " ".join(cleaned.split())
    # Replace filesystem-unsafe characters
    sanitized = trimmed
    for ch in ('<', '>', ':', '"', '|', '?', '*', '/', '\\'):
        sanitized = sanitized.replace(ch, '-')
    # Strip trailing dots/spaces
    sanitized = sanitized.rstrip(". ")
    # Truncate to MAX_FILENAME_BYTES, respecting UTF-8 codepoint boundaries.
    if len(sanitized.encode('utf-8')) > MAX_FILENAME_BYTES:
        truncated = sanitized.encode('utf-8')[:MAX_FILENAME_BYTES]
        sanitized = truncated.decode('utf-8', errors='ignore')
        # Re-strip trailing dots/spaces that may appear at the new end.
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
                        if f.read(1):
                            obs.script_log(obs.LOG_WARNING,
                                           f"Text file exceeds {MAX_READ_SIZE} bytes; "
                                           "only the first chunk is used")
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
        try:
            obs.calldata_set_string(cd, "format", format_value)
            result = obs.proc_handler_call(ph, OVERRIDE_PROC, cd)
        finally:
            obs.calldata_free(cd)

        if not result:
            obs.script_log(obs.LOG_WARNING,
                           f"proc_handler_call for {OVERRIDE_PROC} failed — is the Branch Output plugin up to date?")
            return False
        return True
    finally:
        obs.obs_source_release(bo_filter)


def update_recording_format():
    """Read text source and update Branch Output recording filename format."""
    global last_text, last_applied_text, last_applied_time, override_cleared

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

    # Throttle: skip if the same text was already applied within THROTTLE_SECONDS
    now = time.time()
    if current_text == last_applied_text and (now - last_applied_time) < THROTTLE_SECONDS:
        return

    # Build the new format string
    sanitized = sanitize_filename(current_text)
    if sanitized and base_format:
        new_format = f"{sanitized} {base_format}"
    elif sanitized:
        new_format = sanitized
    elif base_format:
        new_format = base_format
    else:
        # Both text and base format are empty; clear the override
        # rather than sending an ambiguous empty string.
        if not override_cleared:
            clear_override()
        return

    if call_override_proc(filter_uuid, new_format):
        last_text = current_text
        last_applied_text = current_text
        last_applied_time = now
        override_cleared = False
        obs.script_log(obs.LOG_INFO, f"{LOG_LABEL} updated: {new_format}")


def timer_callback():
    update_recording_format()


# --- OBS Script Interface ---

def script_description():
    return (
        "<b>Recording Filename from Text Source</b><br><br>"
        "Overrides a Branch Output recording filename format "
        "based on the content of a Text (GDI+) source.<br><br>"
        "The text content is prepended to the base format.<br><br>"
        "<b>Note:</b> This script overrides the filename format at runtime. "
        "The filter's own property settings are not modified. "
        "When this script is unloaded, the override is cleared and "
        "the filter reverts to its original filename format setting.<br><br>"
        "<b>Behavior:</b><ul>"
        "<li>Before recording: filename is applied when recording starts</li>"
        "<li>During recording (split enabled): triggers immediate file split</li>"
        "<li>During recording (no split): recording restarts with new filename</li>"
        "</ul>"
        "<b>Limitation:</b> Text sources inside Groups are not listed in the dropdown. "
        "Only top-level sources are shown."
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
            # Explicit allowlist (rather than startswith("text_")) to avoid
            # picking up unrelated future source types. _v3 etc. are
            # forward-compat reservations.
            if source_id in ("text_gdiplus", "text_gdiplus_v2", "text_gdiplus_v3",
                             "text_ft2_source", "text_ft2_source_v2"):
                name = obs.obs_source_get_name(source)
                uuid = obs.obs_source_get_uuid(source)
                obs.obs_property_list_add_string(p, name, uuid)
        obs.source_list_release(sources)

    # Branch Output filter selector
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
    global last_applied_text, last_applied_time, override_cleared

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

    # If text source was deselected, clear the override on the current
    # filter so it reverts to its own setting.
    if not text_source_uuid:
        clear_override()
        return

    # Reset state to force update on next tick
    last_text = None
    last_applied_text = None
    last_applied_time = 0.0
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
    selection changes. Also reset the cached text state so that if the
    text source reappears later with the same content as before, the
    override is re-applied rather than being suppressed by the early-
    return "same text" check in update_recording_format().

    Note: resetting last_applied_time to 0.0 intentionally bypasses the
    THROTTLE_SECONDS window on the next successful apply. This is the
    desired behavior so that a source that disappears and returns can
    re-apply its override immediately rather than waiting out the
    throttle. If the text source flaps rapidly, the throttle will not
    suppress those transitions.
    """
    global override_cleared, last_text, last_applied_text, last_applied_time

    last_text = None
    last_applied_text = None
    last_applied_time = 0.0

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
