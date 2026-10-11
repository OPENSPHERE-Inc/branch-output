"""
OBS Python Script: Replay Buffer Filename from Text Source
==========================================================
Overrides the Branch Output replay buffer filename format
based on the content of a Text (GDI+) source, without
restarting the replay buffer output.

This script overrides the filename format at runtime via proc handler.
The Branch Output's own property settings are NOT modified.
When this script is unloaded, the override is cleared and
the Branch Output reverts to its original filename format setting.

Note on throttling:
  Unlike the recording variant, this script does NOT throttle updates.
  Replay buffer saves are on-demand (triggered by user), so the override
  only takes effect at the next save operation. Applying an override more
  frequently has no cost beyond a cheap proc call, so throttling would
  only add latency without benefit.

Requirements:
  - Branch Output plugin v1.0.9+ (with override_replay_buffer_filename_format proc)
  - Branch Output plugin v1.1.0+ to list main outputs (earlier versions list filters only)
  - A Text (GDI+) source whose "text" property will be used as the filename prefix

Usage:
  1. Add this script via OBS > Tools > Scripts
  2. Select the text source and the Branch Output (a filter or a main output)
     from the dropdown lists
  3. Set the base filename format (OBS date/time specifiers like %CCYY-%MM-%DD are supported)
  4. The replay buffer filename will update whenever the text source content changes
"""

import json
import threading
import unicodedata
import obspython as obs

# Proc and filter constants
OVERRIDE_PROC = "override_replay_buffer_filename_format"
BRANCH_OUTPUT_FILTER_ID = "osi_branch_output"
BRANCH_OUTPUT_PROGRAM_ID = "osi_branch_output_program"
LOG_LABEL = "Replay buffer filename format"
MAX_READ_SIZE = 4096  # 4 KB read limit to prevent performance issues on large files
# Truncate to 200 bytes to stay within NTFS filename component (255) /
# MAX_PATH (260) limits, leaving room for the base format and extension.
MAX_FILENAME_BYTES = 200

# Windows reserved device names (case-insensitive).
WINDOWS_RESERVED = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
}

# Script settings
text_source_uuid = ""
selected_filter = ""  # "source_uuid::filter_uuid" for a filter, "::output_uuid" for a main output
base_format = "%CCYY-%MM-%DD %hh-%mm-%ss"
last_text = None
override_cleared = False

# Main output instance that last received a non-empty format from the tick.
# The tick (graphics thread) and script_unload() (UI thread) can interleave, so
# applied_target_weak / applied_target_uuid / target_unloaded are accessed only
# under target_lock, which never covers a proc call or obs_source_release().
applied_target_weak = None
applied_target_uuid = ""
target_unloaded = False
target_lock = threading.Lock()
missing_target_warned = False


def call_global_list_proc(proc_name):
    """Call a global list proc. Returns (found, json_str)."""
    ph = obs.obs_get_proc_handler()
    cd = obs.calldata_create()
    try:
        found = obs.proc_handler_call(ph, proc_name, cd)
        json_str = obs.calldata_string(cd, "json") if found else None
    finally:
        obs.calldata_destroy(cd)
    return found, json_str


def filter_choice(source_name, source_uuid, filter_name, filter_uuid):
    return f"{source_name} / {filter_name}", f"{source_uuid}::{filter_uuid}"


def parse_output_list(json_str):
    """Parse a get_output_list response into (label, value) pairs."""
    if not json_str:
        return []
    try:
        data = json.loads(json_str)
    except json.JSONDecodeError as e:
        obs.script_log(obs.LOG_WARNING, f"Failed to parse output list JSON: {e}")
        return []

    choices = []
    for item in data.get("outputs", []):
        output_type = item.get("type", "")
        if output_type == "filter":
            source_uuid = item.get("source_uuid", "")
            filter_uuid = item.get("filter_uuid", "")
            if source_uuid and filter_uuid:
                choices.append(filter_choice(item.get("source_name", ""), source_uuid,
                                             item.get("filter_name", ""), filter_uuid))
        elif output_type == "program":
            output_uuid = item.get("output_uuid", "")
            if output_uuid:
                choices.append((f"[Main Output] {item.get('output_name', '')}", f"::{output_uuid}"))
    return choices


def parse_filter_list(json_str):
    """Parse a get_filter_list response into (label, value) pairs."""
    if not json_str:
        return []
    try:
        data = json.loads(json_str)
    except json.JSONDecodeError as e:
        obs.script_log(obs.LOG_WARNING, f"Failed to parse filter list JSON: {e}")
        return []

    choices = []
    for item in data.get("filters", []):
        source_uuid = item.get("source_uuid", "")
        filter_uuid = item.get("filter_uuid", "")
        if source_uuid and filter_uuid:
            choices.append(filter_choice(item.get("source_name", ""), source_uuid,
                                         item.get("filter_name", ""), filter_uuid))
    return choices


def get_branch_outputs():
    """List Branch Outputs as (label, value) pairs.

    Falls back to get_filter_list (filters only) only when get_output_list
    is not registered, i.e. on plugins older than v1.1.0.
    """
    found, json_str = call_global_list_proc("osi_branch_output_get_output_list")
    if found:
        return parse_output_list(json_str)
    found, json_str = call_global_list_proc("osi_branch_output_get_filter_list")
    if found:
        return parse_filter_list(json_str)
    return []


def parse_selected_filter(value):
    """Parse 'source_uuid::filter_uuid' into (source_uuid, filter_uuid)."""
    if not value:
        return "", ""
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
    # Fold Unicode "Other" (Cc/Cf/Cs/Cn/Co) and "Separator"
    # (Zs/Zl/Zp, e.g. NBSP / IDEOGRAPHIC SPACE / LINE SEPARATOR) categories
    # to ASCII space so the following split()/join() collapses runs.
    # Keeps the literal space explicitly and avoids str.isprintable(), which
    # would leak ZWJ/BiDi marks. Folding Z* matches the Lua variant's
    # is_unicode_space() coverage.
    cleaned = "".join(
        ch if ch == " " or unicodedata.category(ch)[0] not in ("C", "Z") else " "
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
    """Read text from a Text (GDI+ or FreeType2) source.

    Returns (text, ok). ok = False means the caller should clear the override.
    """
    # GDI+ stores read-from-file as (read_from_file, file); FreeType2 uses
    # (from_file, text_file). Branch on the unversioned source id so both
    # source families honor their "read from file" mode correctly.
    source_id = obs.obs_source_get_unversioned_id(text_source)
    if source_id in ("text_ft2_source", "text_ft2_source_v2"):
        from_file_key = "from_file"
        file_path_key = "text_file"
    else:
        from_file_key = "read_from_file"
        file_path_key = "file"

    settings = obs.obs_source_get_settings(text_source)
    result_text = None
    ok = True

    try:
        read_from_file = obs.obs_data_get_bool(settings, from_file_key)
        if read_from_file:
            file_path = obs.obs_data_get_string(settings, file_path_key)
            if not file_path:
                obs.script_log(obs.LOG_WARNING,
                               "Text source is set to 'read from file' but no file path is configured; clearing override")
                ok = False
            else:
                try:
                    # Open in binary mode so BOM bytes are not translated.
                    # Limit read size to prevent performance issues on accidental large-file selection.
                    # Read one extra byte so len(data) > MAX_READ_SIZE
                    # unambiguously signals truncation even when the file
                    # is exactly MAX_READ_SIZE bytes.
                    with open(file_path, "rb") as f:
                        data = f.read(MAX_READ_SIZE + 1)
                    truncated = len(data) > MAX_READ_SIZE
                    if truncated:
                        obs.script_log(obs.LOG_WARNING,
                                       f"Text file exceeds {MAX_READ_SIZE} bytes; "
                                       "only the first chunk is used")
                        data = data[:MAX_READ_SIZE]
                    # UTF-8 BOM: strip it.
                    if data[:3] == b"\xef\xbb\xbf":
                        data = data[3:]
                    # UTF-16 LE/BE BOM: not supported by this sample; warn and clear.
                    elif data[:2] in (b"\xff\xfe", b"\xfe\xff"):
                        obs.script_log(obs.LOG_WARNING,
                                       "UTF-16 text files are not supported; please save the text file as UTF-8")
                        ok = False
                    if ok:
                        # When truncated, the cut may land inside a multibyte
                        # UTF-8 sequence; drop the trailing partial codepoint
                        # instead of failing the whole read. Mirrors the Lua
                        # variant which skips strict UTF-8 validation on
                        # truncation.
                        decode_errors = "ignore" if truncated else "strict"
                        try:
                            result_text = data.decode("utf-8", errors=decode_errors)
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


def invoke_override(target_uuid, format_value):
    """Call the override proc on the specified Branch Output filter or main output.

    Verifies that the target is actually a Branch Output and logs proc result.
    Returns (ok, weak). weak is a weak reference to the main output instance
    the proc was called on, or None for a filter or on failure; the caller
    owns and releases it.
    """
    if not target_uuid:
        return False, None

    target = obs.obs_get_source_by_uuid(target_uuid)
    if not target:
        obs.script_log(obs.LOG_WARNING, f"Branch Output (uuid: {target_uuid}) not found")
        return False, None

    try:
        target_id = obs.obs_source_get_unversioned_id(target)
        if target_id not in (BRANCH_OUTPUT_FILTER_ID, BRANCH_OUTPUT_PROGRAM_ID):
            obs.script_log(obs.LOG_WARNING,
                           f"Source (uuid: {target_uuid}) is not a Branch Output (id: {target_id})")
            return False, None

        ph = obs.obs_source_get_proc_handler(target)
        cd = obs.calldata_create()
        try:
            obs.calldata_set_string(cd, "format", format_value)
            result = obs.proc_handler_call(ph, OVERRIDE_PROC, cd)
        finally:
            obs.calldata_destroy(cd)

        if not result:
            obs.script_log(obs.LOG_WARNING,
                           f"proc_handler_call for {OVERRIDE_PROC} failed — is the Branch Output plugin up to date?")
            return False, None
        if target_id == BRANCH_OUTPUT_PROGRAM_ID:
            return True, obs.obs_source_get_weak_source(target)
        return True, None
    finally:
        obs.obs_source_release(target)


def call_override_proc(filter_uuid, format_value):
    """Call the override proc on the specified Branch Output.

    Returns True on success, False otherwise.
    """
    ok, weak = invoke_override(filter_uuid, format_value)
    if weak is not None:
        obs.obs_weak_source_release(weak)
    return ok


def reset_apply_state():
    """Forget the applied format so the next tick sends it again."""
    global last_text, override_cleared

    last_text = None
    override_cleared = False


def track_program_target(target_uuid):
    """Return True when the tick may continue with the main output target_uuid.

    The override lives on the main output instance, so an expired weak
    reference (the instance was replaced) resets the apply state to send the
    format again. A target that is not loaded yet is waited for.
    """
    global applied_target_weak, missing_target_warned

    expired = False
    with target_lock:
        if applied_target_weak is not None:
            if applied_target_uuid != target_uuid:
                obs.obs_weak_source_release(applied_target_weak)
                applied_target_weak = None
            elif obs.obs_weak_source_expired(applied_target_weak):
                obs.obs_weak_source_release(applied_target_weak)
                applied_target_weak = None
                expired = True
        target_alive = applied_target_weak is not None

    if expired:
        reset_apply_state()
    if target_alive:
        return True

    target = obs.obs_get_source_by_uuid(target_uuid)
    if not target:
        reset_apply_state()
        if not missing_target_warned:
            obs.script_log(obs.LOG_INFO,
                           f"Main Output (uuid: {target_uuid}) not found; waiting for it to load")
            missing_target_warned = True
        return False
    obs.obs_source_release(target)
    missing_target_warned = False
    return True


def store_applied_target(weak, target_uuid):
    """Keep weak as the main output instance that received the format. Takes ownership of weak."""
    global applied_target_weak, applied_target_uuid

    with target_lock:
        if target_unloaded:
            obs.obs_weak_source_release(weak)
            return
        if applied_target_weak is not None:
            obs.obs_weak_source_release(applied_target_weak)
        applied_target_weak = weak
        applied_target_uuid = target_uuid


def update_replay_buffer_format():
    """Read text source and update Branch Output replay buffer filename format."""
    global last_text, override_cleared

    if not text_source_uuid or not selected_filter:
        return

    source_uuid, filter_uuid = parse_selected_filter(selected_filter)
    if not filter_uuid:
        return

    # An empty source UUID selects a main output.
    if not source_uuid and not track_program_target(filter_uuid):
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

    # Cache the resolved new_format (not raw current_text) so two raw inputs
    # differing only in control chars / trailing whitespace — which collapse
    # to the same sanitized prefix — do not trigger a redundant proc re-call.
    if new_format == last_text:
        return

    ok, weak = invoke_override(filter_uuid, new_format)
    if weak is not None:
        store_applied_target(weak, filter_uuid)
    if ok:
        last_text = new_format
        override_cleared = False
        obs.script_log(obs.LOG_INFO, f"{LOG_LABEL} updated: {new_format}")
    else:
        # Branch Output missing or proc call failed: cache new_format so the
        # "same format" early-return suppresses retries on subsequent ticks
        # until the text or selection actually changes. Without this the
        # timer would re-issue the warning every tick at 1 Hz.
        last_text = new_format
        override_cleared = True


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
        "The Branch Output's own property settings are not modified. "
        "When this script is unloaded, the override is cleared and "
        "the Branch Output reverts to its original filename format setting.<br><br>"
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

    # Branch Output selector (populated from global proc handler)
    p = obs.obs_properties_add_list(
        props, "selected_filter", "Branch Output",
        obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING,
    )
    obs.obs_property_list_add_string(p, "(Select a Branch Output)", "")
    for label, value in get_branch_outputs():
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
    global missing_target_warned

    # Remember the previously selected Branch Output so we can clear its
    # override if the user changed the selection.
    prev_selected_filter = selected_filter
    _, old_filter_uuid = parse_selected_filter(prev_selected_filter)

    text_source_uuid = obs.obs_data_get_string(settings, "text_source")
    selected_filter = obs.obs_data_get_string(settings, "selected_filter")
    base_format = obs.obs_data_get_string(settings, "base_format")

    if prev_selected_filter != selected_filter:
        missing_target_warned = False

    _, new_filter_uuid = parse_selected_filter(selected_filter)
    if old_filter_uuid and old_filter_uuid != new_filter_uuid:
        # Clear override on the previously selected Branch Output so it does not
        # remain overridden after the user switched to a different one.
        if call_override_proc(old_filter_uuid, ""):
            obs.script_log(obs.LOG_INFO,
                           f"{LOG_LABEL} override cleared on previous Branch Output "
                           f"(uuid: {old_filter_uuid})")

    # If text source was deselected, clear the override on the current
    # Branch Output so it reverts to its own setting. clear_override() already
    # resets last_text.
    if not text_source_uuid:
        clear_override()
        return

    # Reset state to force update on next tick
    last_text = None
    override_cleared = False  # reset to "needs re-apply"


def script_load(settings):
    global target_unloaded, missing_target_warned

    # The module object, and with it the state left by script_unload(), is reused
    # when OBS creates the script again (e.g. on a scene collection switch).
    with target_lock:
        target_unloaded = False
    missing_target_warned = False

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
    global applied_target_weak, target_unloaded

    obs.timer_remove(timer_callback)
    clear_override()
    with target_lock:
        target_unloaded = True
        if applied_target_weak is not None:
            obs.obs_weak_source_release(applied_target_weak)
            applied_target_weak = None
