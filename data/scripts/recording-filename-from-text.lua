--[[
OBS Lua Script: Recording Filename from Text Source
====================================================
Overrides the Branch Output recording filename format
based on the content of a Text (GDI+) source.

This script overrides the filename format at runtime via proc handler.
The Branch Output's own property settings are NOT modified.
When this script is unloaded, the override is cleared and
the Branch Output reverts to its original filename format setting.

The filename format is applied in three ways depending on recording state:
  - Before recording: stored and used when recording starts
  - During recording with file splitting: triggers immediate file split
  - During recording without file splitting: recording is restarted with new filename

Requirements:
  - Branch Output plugin v1.0.9+ (with override_recording_filename_format proc)
  - Branch Output plugin v1.1.0+ to list Program Outputs (earlier versions list filters only)
  - A Text (GDI+) source whose "text" property will be used as the filename prefix

Known limitation (Windows + "Read from file" mode):
  Lua's io.open() ultimately calls the CRT fopen(), which on Windows
  interprets the path in the current system ANSI code page (e.g. CP932 on
  Japanese locale). OBS stores settings as UTF-8, so a "Read from file"
  path that contains characters not representable in the ANSI code page
  (e.g. some Japanese / Chinese / Korean characters in a non-matching
  locale) may fail to open from this Lua script. In that case the script
  logs a "Failed to read text file" warning and clears the override.
  The Python variant (recording-filename-from-text.py) is not affected
  because CPython uses wide-character Windows APIs internally — use the
  Python variant if you need full UTF-8 path support on Windows.

Usage:
  1. Add this script via OBS > Tools > Scripts
  2. Select the text source and the Branch Output (a filter or a Program Output)
     from the dropdown lists
  3. Set the base filename format (OBS date/time specifiers like %CCYY-%MM-%DD are supported)
  4. The recording filename will update whenever the text source content changes
--]]

local obs = obslua

-- Proc and filter constants
local OVERRIDE_PROC = "override_recording_filename_format"
local BRANCH_OUTPUT_FILTER_ID = "osi_branch_output"
local BRANCH_OUTPUT_PROGRAM_ID = "osi_branch_output_program"
local LOG_LABEL = "Recording filename format"
local MAX_READ_SIZE = 4096 -- 4 KB read limit to prevent performance issues on large files
-- Truncate to 200 bytes to stay within NTFS filename component (255) /
-- MAX_PATH (260) limits, leaving room for the base format and extension.
local MAX_FILENAME_BYTES = 200

-- Script settings
local text_source_uuid = ""
-- "source_uuid::filter_uuid" for a filter, "::output_uuid" for a Program Output
local selected_filter = ""
local base_format = "%CCYY-%MM-%DD %hh-%mm-%ss"
local last_text = nil
local last_applied_text = nil
local last_applied_time = 0
local override_cleared = false
local THROTTLE_SECONDS = 30

-- Program Output instance the last non-empty format was sent to, and its UUID
local applied_target_weak = nil
local applied_target_uuid = ""
local missing_target_warned = false

-- Forward declaration so update_recording_format() can call clear_override()
local clear_override

local function call_global_list_proc(proc_name)
    -- Returns (found, json_str). found is false when the proc is not registered.
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

local function for_each_list_item(json_str, array_key, list_label, callback)
    if json_str == nil or json_str == "" then
        return
    end
    local data = obs.obs_data_create_from_json(json_str)
    if data == nil then
        obs.script_log(obs.LOG_WARNING, "Failed to parse " .. list_label .. " JSON")
        return
    end
    local array = obs.obs_data_get_array(data, array_key)
    if array ~= nil then
        local count = obs.obs_data_array_count(array)
        for i = 0, count - 1 do
            local item = obs.obs_data_array_item(array, i)
            if item ~= nil then
                callback(item)
                obs.obs_data_release(item)
            end
        end
        obs.obs_data_array_release(array)
    end
    obs.obs_data_release(data)
end

local function add_filter_choice(choices, item)
    local source_uuid = obs.obs_data_get_string(item, "source_uuid")
    local filter_uuid = obs.obs_data_get_string(item, "filter_uuid")
    if source_uuid ~= "" and filter_uuid ~= "" then
        table.insert(choices, {
            label = obs.obs_data_get_string(item, "source_name") .. " / " ..
                obs.obs_data_get_string(item, "filter_name"),
            value = source_uuid .. "::" .. filter_uuid,
        })
    end
end

local function get_branch_outputs()
    -- Returns a list of {label, value} choices. get_filter_list is used only
    -- when get_output_list is not registered (plugin older than v1.1.0).
    local choices = {}
    local found, json_str = call_global_list_proc("osi_branch_output_get_output_list")
    if found then
        for_each_list_item(json_str, "outputs", "output list", function(item)
            local item_type = obs.obs_data_get_string(item, "type")
            if item_type == "filter" then
                add_filter_choice(choices, item)
            elseif item_type == "program" then
                local output_uuid = obs.obs_data_get_string(item, "output_uuid")
                if output_uuid ~= "" then
                    table.insert(choices, {
                        label = "[Program] " .. obs.obs_data_get_string(item, "output_name"),
                        value = "::" .. output_uuid,
                    })
                end
            end
        end)
        return choices
    end

    found, json_str = call_global_list_proc("osi_branch_output_get_filter_list")
    if found then
        for_each_list_item(json_str, "filters", "filter list", function(item)
            add_filter_choice(choices, item)
        end)
    end
    return choices
end

local function utf8_validate(s)
    -- Returns true when every byte in s is part of a valid UTF-8 sequence.
    -- Returns false when an unexpected non-ASCII byte appears (e.g. CP932).
    local i = 1
    local len = #s
    while i <= len do
        local b = string.byte(s, i)
        local size
        if b < 0x80 then
            size = 1
        elseif b >= 0xC2 and b <= 0xDF and i + 1 <= len then
            local b2 = string.byte(s, i + 1)
            if b2 >= 0x80 and b2 <= 0xBF then
                size = 2
            end
        elseif b >= 0xE0 and b <= 0xEF and i + 2 <= len then
            local b2 = string.byte(s, i + 1)
            local b3 = string.byte(s, i + 2)
            if b2 >= 0x80 and b2 <= 0xBF and b3 >= 0x80 and b3 <= 0xBF
                and (b ~= 0xE0 or b2 >= 0xA0) and (b ~= 0xED or b2 <= 0x9F) then
                size = 3
            end
        elseif b >= 0xF0 and b <= 0xF4 and i + 3 <= len then
            local b2 = string.byte(s, i + 1)
            local b3 = string.byte(s, i + 2)
            local b4 = string.byte(s, i + 3)
            if b2 >= 0x80 and b2 <= 0xBF and b3 >= 0x80 and b3 <= 0xBF
                and b4 >= 0x80 and b4 <= 0xBF
                and (b ~= 0xF0 or b2 >= 0x90) and (b ~= 0xF4 or b2 <= 0x8F) then
                size = 4
            end
        end
        if not size then
            return false
        end
        i = i + size
    end
    return true
end

local function parse_selected_filter(value)
    -- Parse 'source_uuid::filter_uuid' into (source_uuid, filter_uuid).
    if value == nil then
        return "", ""
    end
    local sep = string.find(value, "::", 1, true)
    if sep then
        return string.sub(value, 1, sep - 1), string.sub(value, sep + 2)
    end
    return "", ""
end

-- Windows reserved device names (case-insensitive).
local WINDOWS_RESERVED = {
    CON = true, PRN = true, AUX = true, NUL = true,
    COM1 = true, COM2 = true, COM3 = true, COM4 = true, COM5 = true,
    COM6 = true, COM7 = true, COM8 = true, COM9 = true,
    LPT1 = true, LPT2 = true, LPT3 = true, LPT4 = true, LPT5 = true,
    LPT6 = true, LPT7 = true, LPT8 = true, LPT9 = true,
}

-- Unicode code points to drop entirely (zero-width / BOM-like contaminants from
-- Word/Excel/SNS copy-paste in ja/zh/ko workflows).
local UNICODE_STRIP = {
    [0x200B] = true, -- ZERO WIDTH SPACE
    [0xFEFF] = true, -- BOM / ZWNBSP
}

local function is_unicode_space(cp)
    -- Whitespace-like code points folded to ASCII space so the trim/collapse
    -- pass below can remove them. Includes ASCII control chars and DEL.
    return cp < 0x20 or cp == 0x7F
        or cp == 0xA0            -- NBSP
        or (cp >= 0x2000 and cp <= 0x200A) -- en/em space family
        or cp == 0x2028 or cp == 0x2029    -- line / paragraph separator
        or cp == 0x3000          -- ideographic space
end

local function utf8_codepoints(s)
    -- Iterator yielding each Unicode code point from a UTF-8 encoded string.
    -- Invalid or incomplete byte sequences are silently skipped.
    -- Compatible with LuaJIT / Lua 5.1 (no utf8 library required).
    local i = 1
    local len = #s
    return function()
        while i <= len do
            local b = string.byte(s, i)
            local cp, size
            if b < 0x80 then
                cp, size = b, 1
            elseif b >= 0xC2 and b <= 0xDF and i + 1 <= len then
                local b2 = string.byte(s, i + 1)
                if b2 >= 0x80 and b2 <= 0xBF then
                    cp = (b - 0xC0) * 0x40 + (b2 - 0x80)
                    size = 2
                end
            elseif b >= 0xE0 and b <= 0xEF and i + 2 <= len then
                local b2 = string.byte(s, i + 1)
                local b3 = string.byte(s, i + 2)
                if b2 >= 0x80 and b2 <= 0xBF and b3 >= 0x80 and b3 <= 0xBF
                    and (b ~= 0xE0 or b2 >= 0xA0) and (b ~= 0xED or b2 <= 0x9F) then
                    cp = (b - 0xE0) * 0x1000 + (b2 - 0x80) * 0x40 + (b3 - 0x80)
                    size = 3
                end
            elseif b >= 0xF0 and b <= 0xF4 and i + 3 <= len then
                local b2 = string.byte(s, i + 1)
                local b3 = string.byte(s, i + 2)
                local b4 = string.byte(s, i + 3)
                if b2 >= 0x80 and b2 <= 0xBF and b3 >= 0x80 and b3 <= 0xBF
                    and b4 >= 0x80 and b4 <= 0xBF
                    and (b ~= 0xF0 or b2 >= 0x90) and (b ~= 0xF4 or b2 <= 0x8F) then
                    cp = (b - 0xF0) * 0x40000 + (b2 - 0x80) * 0x1000 + (b3 - 0x80) * 0x40 + (b4 - 0x80)
                    size = 4
                end
            end
            if cp then
                i = i + size
                return cp
            else
                i = i + 1 -- skip invalid byte
            end
        end
    end
end

local function cp_to_utf8(cp)
    -- Encode a Unicode code point to a UTF-8 byte string.
    -- Compatible with LuaJIT / Lua 5.1 (no utf8 library required).
    if cp >= 0xD800 and cp <= 0xDFFF then
        return "" -- surrogate halves are not valid UTF-8 scalars
    end
    if cp <= 0x7F then
        return string.char(cp)
    elseif cp <= 0x7FF then
        local hi = math.floor(cp / 0x40)
        return string.char(0xC0 + hi, 0x80 + cp % 0x40)
    elseif cp <= 0xFFFF then
        local hi = math.floor(cp / 0x1000)
        local mid = math.floor(cp / 0x40) % 0x40
        return string.char(0xE0 + hi, 0x80 + mid, 0x80 + cp % 0x40)
    elseif cp <= 0x10FFFF then
        local hi = math.floor(cp / 0x40000)
        local mid1 = math.floor(cp / 0x1000) % 0x40
        local mid2 = math.floor(cp / 0x40) % 0x40
        return string.char(0xF0 + hi, 0x80 + mid1, 0x80 + mid2, 0x80 + cp % 0x40)
    end
    return "" -- invalid code point
end

local function sanitize_filename(text)
    -- Produce a filesystem-safe prefix. Order matches the Python variant:
    --   1. Walk as UTF-8 code points, drop strip-list points, fold whitespace-
    --      like points to a single ASCII space.
    --   2. Collapse runs of whitespace and trim.
    --   3. Replace filesystem-unsafe characters with "-".
    --   4. Strip trailing dots/spaces (Windows disallows these at end).
    --   5. Truncate to 200 bytes on UTF-8 codepoint boundaries.
    --   6. Re-strip trailing dots/spaces that may appear at the new end.
    --   7. Prefix an underscore if the result collides with a Windows reserved name.
    if text == nil then
        return ""
    end
    local out = {}
    for cp in utf8_codepoints(text) do
        if UNICODE_STRIP[cp] then
            -- drop
        elseif is_unicode_space(cp) then
            out[#out + 1] = " "
        else
            out[#out + 1] = cp_to_utf8(cp)
        end
    end
    local cleaned = table.concat(out)
    cleaned = cleaned:gsub("%s+", " ")
    local trimmed = cleaned:match("^%s*(.-)%s*$") or ""
    local sanitized = trimmed:gsub('[<>:"|?*/\\]', "-")
    -- Precondition: upstream is_unicode_space() folds all Unicode whitespace
    -- to ASCII space and gsub("%s+", " ") collapses runs, so a literal-space
    -- pattern is sufficient here. Weakening either upstream step would leave
    -- non-ASCII trailing whitespace untrimmed.
    sanitized = sanitized:gsub("[%. ]+$", "")
    -- Respect UTF-8 codepoint boundaries when truncating to MAX_FILENAME_BYTES.
    if #sanitized > MAX_FILENAME_BYTES then
        local parts = {}
        local total = 0
        for cp in utf8_codepoints(sanitized) do
            local encoded = cp_to_utf8(cp)
            if total + #encoded > MAX_FILENAME_BYTES then
                break
            end
            parts[#parts + 1] = encoded
            total = total + #encoded
        end
        sanitized = table.concat(parts)
        -- Re-strip trailing dots/spaces that may appear at the new end.
        sanitized = sanitized:gsub("[%. ]+$", "")
    end
    -- Windows treats reserved device names as reserved even when followed by
    -- an extension (e.g. "CON.txt"). Since this prefix will have the base
    -- format appended after a dot+space, check the portion before the first
    -- dot as well.
    local base_before_dot = sanitized:match("^([^%.]*)") or sanitized
    if WINDOWS_RESERVED[sanitized:upper()] or WINDOWS_RESERVED[base_before_dot:upper()] then
        sanitized = "_" .. sanitized
    end
    return sanitized
end

local function read_text_from_source(text_source)
    -- Returns (text, ok). ok = false means the caller should clear the override.
    -- pcall guards the read path so obs_data_release() always runs even if the
    -- inner logic raises a Lua runtime error (e.g. unexpected non-string value).
    -- GDI+ stores read-from-file as (read_from_file, file); FreeType2 uses
    -- (from_file, text_file). Branch on the unversioned source id so both
    -- source families honor their "read from file" mode correctly.
    local source_id = obs.obs_source_get_unversioned_id(text_source)
    local from_file_key, file_path_key
    if source_id == "text_ft2_source" or source_id == "text_ft2_source_v2" then
        from_file_key = "from_file"
        file_path_key = "text_file"
    else
        from_file_key = "read_from_file"
        file_path_key = "file"
    end
    local settings = obs.obs_source_get_settings(text_source)
    local function read_inner()
        local read_from_file = obs.obs_data_get_bool(settings, from_file_key)
        if not read_from_file then
            return obs.obs_data_get_string(settings, "text"), true
        end
        local file_path = obs.obs_data_get_string(settings, file_path_key)
        if file_path == "" then
            obs.script_log(obs.LOG_WARNING,
                "Text source is set to 'read from file' but no file path is configured; clearing override")
            return nil, false
        end
        -- Open in binary mode so BOM bytes are not translated.
        local f, err = io.open(file_path, "rb")
        if not f then
            obs.script_log(obs.LOG_WARNING, "Failed to read text file: " .. tostring(err))
            return nil, false
        end
        -- Limit read size to prevent performance issues on accidental large-file selection.
        -- Read one extra byte so #data > MAX_READ_SIZE unambiguously
        -- signals truncation even when the file is exactly MAX_READ_SIZE
        -- bytes (where a separate f:read(1) would return nil at EOF).
        local data = f:read(MAX_READ_SIZE + 1) or ""
        local close_ok, close_err = f:close()
        if not close_ok then
            obs.script_log(obs.LOG_WARNING,
                "Failed to close text file: " .. tostring(close_err))
        end
        local truncated = #data > MAX_READ_SIZE
        if truncated then
            obs.script_log(obs.LOG_WARNING,
                "Text file exceeds " .. MAX_READ_SIZE ..
                    " bytes; only the first " .. MAX_READ_SIZE .. " bytes are used")
            data = data:sub(1, MAX_READ_SIZE)
        end
        -- UTF-8 BOM: strip it.
        if data:sub(1, 3) == "\239\187\191" then
            data = data:sub(4)
        -- UTF-32 LE/BE BOM: must be checked before UTF-16 because UTF-32 LE
        -- begins with 0xFF 0xFE which also matches the UTF-16 LE BOM prefix.
        elseif data:sub(1, 4) == "\255\254\0\0" or data:sub(1, 4) == "\0\0\254\255" then
            obs.script_log(obs.LOG_WARNING,
                "UTF-32 text files are not supported; please save the text file as UTF-8")
            return nil, false
        -- UTF-16 LE/BE BOM: not supported by this sample; warn and clear.
        elseif data:sub(1, 2) == "\255\254" or data:sub(1, 2) == "\254\255" then
            obs.script_log(obs.LOG_WARNING,
                "UTF-16 text files are not supported; please save the text file as UTF-8")
            return nil, false
        end
        -- Skip strict UTF-8 validation when truncated, since the cut may
        -- have landed inside a multibyte sequence.
        if not truncated and not utf8_validate(data) then
            obs.script_log(obs.LOG_WARNING,
                "Text file is not valid UTF-8 (e.g. CP932); please save as UTF-8")
            return nil, false
        end
        return data, true
    end

    local pcall_ok, result_text, ok = pcall(read_inner)
    obs.obs_data_release(settings)
    if not pcall_ok then
        obs.script_log(obs.LOG_WARNING, "read_text_from_source error: " .. tostring(result_text))
        return nil, false
    end
    return result_text, ok
end

local function call_override_on_source(target, format_value)
    -- Returns true when the override proc call on target succeeded.
    local ph = obs.obs_source_get_proc_handler(target)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", format_value)
    local result = obs.proc_handler_call(ph, OVERRIDE_PROC, cd)
    obs.calldata_destroy(cd)
    return result
end

local function invoke_override(target_uuid, format_value)
    -- Call the override proc on the specified Branch Output filter or Program Output.
    -- Returns (ok, weak, missing). weak is a weak reference to the Program Output
    -- the proc was called on, or nil for a filter or on failure; the caller
    -- releases it. missing is true when no source has target_uuid.
    if target_uuid == nil or target_uuid == "" then
        return false, nil, false
    end

    local target = obs.obs_get_source_by_uuid(target_uuid)
    if target == nil then
        obs.script_log(obs.LOG_WARNING, "Branch Output (uuid: " .. target_uuid .. ") not found")
        return false, nil, true
    end

    local target_id = obs.obs_source_get_unversioned_id(target)
    if target_id ~= BRANCH_OUTPUT_FILTER_ID and target_id ~= BRANCH_OUTPUT_PROGRAM_ID then
        obs.script_log(obs.LOG_WARNING,
            "Source (uuid: " .. target_uuid .. ") is not a Branch Output (id: " ..
                tostring(target_id) .. ")")
        obs.obs_source_release(target)
        return false, nil, false
    end

    local result = call_override_on_source(target, format_value)
    local weak = nil
    if result and target_id == BRANCH_OUTPUT_PROGRAM_ID then
        weak = obs.obs_source_get_weak_source(target)
    end
    obs.obs_source_release(target)

    if not result then
        obs.script_log(obs.LOG_WARNING,
            "proc_handler_call for " .. OVERRIDE_PROC ..
                " failed — is the Branch Output plugin up to date?")
        return false, nil, false
    end
    return true, weak, false
end

local function call_override_proc(filter_uuid, format_value)
    -- Returns true on success, false otherwise.
    local ok, weak = invoke_override(filter_uuid, format_value)
    if weak ~= nil then
        obs.obs_weak_source_release(weak)
    end
    return ok
end

local function reset_apply_state()
    last_text = nil
    last_applied_text = nil
    last_applied_time = 0
    override_cleared = false
end

local function release_applied_target()
    if applied_target_weak ~= nil then
        obs.obs_weak_source_release(applied_target_weak)
        applied_target_weak = nil
    end
    applied_target_uuid = ""
end

local function track_program_target(target_uuid)
    -- Returns false while the Program Output cannot be found. A Program Output
    -- instance replaced under the same UUID (e.g. on a profile switch) drops
    -- the override, so the apply state is reset to resend it.
    -- A Program Output whose UUID was reassigned in place (a scene collection
    -- duplicate on OBS 30.x) cannot be reached by UUID, so its override is
    -- cleared through the weak reference and it is no longer tracked.
    if applied_target_weak ~= nil then
        if applied_target_uuid ~= target_uuid then
            release_applied_target()
        else
            local applied = obs.obs_weak_source_get_source(applied_target_weak)
            if applied == nil then
                release_applied_target()
                reset_apply_state()
            elseif obs.obs_source_get_uuid(applied) ~= target_uuid then
                call_override_on_source(applied, "")
                obs.obs_source_release(applied)
                obs.script_log(obs.LOG_WARNING,
                    "Program Output (uuid: " .. target_uuid ..
                        ") was assigned a new UUID; override cleared, select it again")
                release_applied_target()
                reset_apply_state()
                missing_target_warned = true
                return false
            else
                obs.obs_source_release(applied)
            end
        end
    end
    if applied_target_weak ~= nil then
        return true
    end

    local target = obs.obs_get_source_by_uuid(target_uuid)
    if target == nil then
        reset_apply_state()
        if not missing_target_warned then
            obs.script_log(obs.LOG_INFO,
                "Program Output (uuid: " .. target_uuid .. ") not found; waiting for it to load")
            missing_target_warned = true
        end
        return false
    end
    obs.obs_source_release(target)
    missing_target_warned = false
    return true
end

local function update_recording_format()
    -- Read text source and update Branch Output recording filename format.
    if text_source_uuid == "" or selected_filter == "" then
        return
    end

    local parent_uuid, filter_uuid = parse_selected_filter(selected_filter)
    if filter_uuid == "" then
        return
    end

    if parent_uuid == "" and not track_program_target(filter_uuid) then
        return
    end

    -- Get text from the text source
    local text_source = obs.obs_get_source_by_uuid(text_source_uuid)
    if text_source == nil then
        -- Text source not available: clear override (only once per state change)
        if not override_cleared then
            clear_override()
        end
        return
    end

    local current_text, ok = read_text_from_source(text_source)
    obs.obs_source_release(text_source)

    if not ok or current_text == nil then
        if not override_cleared then
            clear_override()
        end
        return
    end

    -- Build the new format string
    local sanitized = sanitize_filename(current_text)
    local new_format
    if sanitized ~= "" and base_format ~= "" then
        new_format = sanitized .. " " .. base_format
    elseif sanitized ~= "" then
        new_format = sanitized
    elseif base_format ~= "" then
        new_format = base_format
    else
        -- Both text and base format are empty; clear the override
        -- rather than sending an ambiguous empty string.
        if not override_cleared then
            clear_override()
        end
        return
    end

    -- Cache the resolved new_format (not the raw current_text) so two raw
    -- inputs differing only in control chars / trailing whitespace — which
    -- collapse to the same sanitized prefix — do not trigger a redundant
    -- proc re-call (which would force a file split / recording restart).
    if new_format == last_text then
        return
    end

    -- Throttle: skip if the same format was already applied within THROTTLE_SECONDS.
    -- last_text is updated only after a successful apply so a throttled tick
    -- is not swallowed by the equality early-return on the next tick.
    local now = os.time()
    if new_format == last_applied_text and (now - last_applied_time) < THROTTLE_SECONDS then
        return
    end

    local ok, weak, missing = invoke_override(filter_uuid, new_format)
    if weak ~= nil then
        release_applied_target()
        applied_target_weak = weak
        applied_target_uuid = filter_uuid
    end
    if ok then
        last_text = new_format
        last_applied_text = new_format
        last_applied_time = now
        override_cleared = false
        obs.script_log(obs.LOG_INFO, LOG_LABEL .. " updated: " .. new_format)
    elseif missing and parent_uuid == "" then
        -- The next tick waits in track_program_target() until the Program Output
        -- is loaded again, then resends the format.
        reset_apply_state()
    else
        -- Filter missing or proc call failed: cache new_format so the
        -- "same format" early-return suppresses retries on subsequent ticks
        -- until the text or selection actually changes. Without this the
        -- timer would re-issue the warning every tick at 1 Hz.
        last_text = new_format
        override_cleared = true
    end
end

clear_override = function()
    -- Postcondition: override_cleared = true on every exit path so subsequent
    -- ticks do not re-issue the clear.
    -- last_applied_time = 0 intentionally bypasses THROTTLE_SECONDS so a
    -- vanish-and-return cycle is not forced to wait out the throttle.
    last_text = nil
    last_applied_text = nil
    last_applied_time = 0

    if selected_filter == "" then
        override_cleared = true
        return
    end

    local _, filter_uuid = parse_selected_filter(selected_filter)
    if filter_uuid == "" then
        override_cleared = true
        return
    end

    if call_override_proc(filter_uuid, "") then
        obs.script_log(obs.LOG_INFO, LOG_LABEL .. " override cleared")
    end
    override_cleared = true
end

local function timer_callback()
    update_recording_format()
end

-- --- OBS Script Interface ---

function script_description()
    return [[<b>Recording Filename from Text Source</b><br><br>
Overrides a Branch Output recording filename format based on the content of a Text (GDI+) source.<br><br>
The text content is prepended to the base format.<br><br>
<b>Note:</b> This script overrides the filename format at runtime. The Branch Output's own property settings are not modified. When this script is unloaded, the override is cleared and the Branch Output reverts to its original filename format setting.<br><br>
<b>Behavior:</b><ul>
<li>Before recording: filename is applied when recording starts</li>
<li>During recording (split enabled): triggers immediate file split</li>
<li>During recording (no split): recording restarts with new filename</li>
</ul>
<b>Limitation:</b> Text sources inside Groups are not listed in the dropdown. Only top-level sources are shown.]]
end

function script_properties()
    local props = obs.obs_properties_create()

    -- Text source selector
    local p = obs.obs_properties_add_list(
        props, "text_source", "Text Source",
        obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING
    )
    obs.obs_property_list_add_string(p, "(Select a text source)", "")
    local sources = obs.obs_enum_sources()
    if sources ~= nil then
        for _, source in ipairs(sources) do
            local source_id = obs.obs_source_get_unversioned_id(source)
            -- Explicit allowlist (rather than a "text_" prefix match) to
            -- avoid picking up unrelated future source types. _v3 etc. are
            -- forward-compat reservations.
            if source_id == "text_gdiplus" or source_id == "text_gdiplus_v2"
                or source_id == "text_gdiplus_v3"
                or source_id == "text_ft2_source" or source_id == "text_ft2_source_v2" then
                local name = obs.obs_source_get_name(source)
                local uuid = obs.obs_source_get_uuid(source)
                obs.obs_property_list_add_string(p, name, uuid)
            end
        end
        obs.source_list_release(sources)
    end

    -- Branch Output selector (filters and Program Outputs)
    p = obs.obs_properties_add_list(
        props, "selected_filter", "Branch Output",
        obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING
    )
    obs.obs_property_list_add_string(p, "(Select a Branch Output)", "")
    for _, choice in ipairs(get_branch_outputs()) do
        obs.obs_property_list_add_string(p, choice.label, choice.value)
    end

    -- Base filename format
    obs.obs_properties_add_text(
        props, "base_format", "Base Filename Format",
        obs.OBS_TEXT_DEFAULT
    )

    return props
end

function script_defaults(settings)
    obs.obs_data_set_default_string(settings, "base_format", "%CCYY-%MM-%DD %hh-%mm-%ss")
end

function script_update(settings)
    -- Capture previous identifiers before overwriting them so we can detect
    -- selection changes (clear old target override, reset throttle window).
    local prev_text_source_uuid = text_source_uuid
    local prev_selected_filter = selected_filter
    local _, old_filter_uuid = parse_selected_filter(prev_selected_filter)

    text_source_uuid = obs.obs_data_get_string(settings, "text_source")
    selected_filter = obs.obs_data_get_string(settings, "selected_filter")
    base_format = obs.obs_data_get_string(settings, "base_format")

    if prev_selected_filter ~= selected_filter then
        missing_target_warned = false
    end

    local _, new_filter_uuid = parse_selected_filter(selected_filter)
    if old_filter_uuid ~= "" and old_filter_uuid ~= new_filter_uuid then
        -- Clear override on the previously selected Branch Output so it does
        -- not remain overridden after the user switched to a different one.
        if call_override_proc(old_filter_uuid, "") then
            obs.script_log(obs.LOG_INFO,
                LOG_LABEL .. " override cleared on previous Branch Output (uuid: " ..
                    old_filter_uuid .. ")")
        end
    end

    -- If text source was deselected, clear the override on the current
    -- Branch Output so it reverts to its own setting.
    if text_source_uuid == "" then
        clear_override()
        return
    end

    -- Force re-apply on next tick (text or Branch Output selection changed,
    -- or base_format was edited).
    last_text = nil
    override_cleared = false  -- reset to "needs re-apply"
    -- Preserve last_applied_text / last_applied_time when only base_format
    -- changed, so dragging a slider does not bypass THROTTLE_SECONDS.
    local selection_changed = (prev_text_source_uuid ~= text_source_uuid
        or prev_selected_filter ~= selected_filter)
    if selection_changed then
        last_applied_text = nil
        last_applied_time = 0
    end
end

function script_load(settings)
    -- Defensive timer_remove in case of script reload.
    obs.timer_remove(timer_callback)
    -- script_update(settings) is called automatically by OBS after
    -- script_load, so we don't need to invoke it explicitly here.
    obs.timer_add(timer_callback, 1000)
end

function script_unload()
    obs.timer_remove(timer_callback)
    clear_override()
    release_applied_target()
end
