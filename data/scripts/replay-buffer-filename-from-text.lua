--[[
OBS Lua Script: Replay Buffer Filename from Text Source
========================================================
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

Known limitation (Windows + "Read from file" mode):
  Lua's io.open() ultimately calls the CRT fopen(), which on Windows
  interprets the path in the current system ANSI code page (e.g. CP932 on
  Japanese locale). OBS stores settings as UTF-8, so a "Read from file"
  path that contains characters not representable in the ANSI code page
  (e.g. some Japanese / Chinese / Korean characters in a non-matching
  locale) may fail to open from this Lua script. In that case the script
  logs a "Failed to read text file" warning and clears the override.
  The Python variant (replay-buffer-filename-from-text.py) is not affected
  because CPython uses wide-character Windows APIs internally — use the
  Python variant if you need full UTF-8 path support on Windows.

Usage:
  1. Add this script via OBS > Tools > Scripts
  2. Select the text source and the Branch Output filter from the dropdown lists
  3. Set the base filename format (OBS date/time specifiers like %CCYY-%MM-%DD are supported)
  4. The replay buffer filename will update whenever the text source content changes
--]]

local obs = obslua

-- Proc and filter constants
local OVERRIDE_PROC = "override_replay_buffer_filename_format"
local BRANCH_OUTPUT_FILTER_ID = "osi_branch_output"
local LOG_LABEL = "Replay buffer filename format"
local MAX_READ_SIZE = 4096 -- 4 KB read limit to prevent performance issues on large files
-- Truncate to 200 bytes to stay within NTFS filename component (255) /
-- MAX_PATH (260) limits, leaving room for the base format and extension.
local MAX_FILENAME_BYTES = 200

-- Script settings
local text_source_uuid = ""
local selected_filter = "" -- "source_uuid::filter_uuid" format
local base_format = "%CCYY-%MM-%DD %hh-%mm-%ss"
local last_text = nil
local override_cleared = false

-- Forward declaration so update_replay_buffer_format() can call clear_override()
local clear_override

local function get_branch_output_filters()
    -- Get list of Branch Output filters via global proc handler.
    local filters = {}
    local ph = obs.obs_get_proc_handler()
    local cd = obs.calldata_create()

    if obs.proc_handler_call(ph, "osi_branch_output_get_filter_list", cd) then
        local json_str = obs.calldata_string(cd, "json")
        if json_str and json_str ~= "" then
            local data = obs.obs_data_create_from_json(json_str)
            if data == nil then
                obs.script_log(obs.LOG_WARNING, "Failed to parse filter list JSON")
            else
                local array = obs.obs_data_get_array(data, "filters")
                if array ~= nil then
                    local count = obs.obs_data_array_count(array)
                    for i = 0, count - 1 do
                        local item = obs.obs_data_array_item(array, i)
                        if item ~= nil then
                            local source_uuid = obs.obs_data_get_string(item, "source_uuid")
                            local filter_uuid = obs.obs_data_get_string(item, "filter_uuid")
                            if source_uuid ~= "" and filter_uuid ~= "" then
                                table.insert(filters, {
                                    source_name = obs.obs_data_get_string(item, "source_name"),
                                    source_uuid = source_uuid,
                                    filter_name = obs.obs_data_get_string(item, "filter_name"),
                                    filter_uuid = filter_uuid,
                                })
                            end
                            obs.obs_data_release(item)
                        end
                    end
                    obs.obs_data_array_release(array)
                end
                obs.obs_data_release(data)
            end
        end
    end

    obs.calldata_free(cd)
    return filters
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

local function call_override_proc(filter_uuid, format_value)
    -- Call the override proc on the specified Branch Output filter.
    -- Verifies that the target is actually a Branch Output filter and logs proc result.
    -- Returns true on success, false otherwise.
    if filter_uuid == nil or filter_uuid == "" then
        return false
    end

    local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
    if bo_filter == nil then
        obs.script_log(obs.LOG_WARNING, "Filter (uuid: " .. filter_uuid .. ") not found")
        return false
    end

    local filter_id = obs.obs_source_get_unversioned_id(bo_filter)
    if filter_id ~= BRANCH_OUTPUT_FILTER_ID then
        obs.script_log(obs.LOG_WARNING,
            "Source (uuid: " .. filter_uuid .. ") is not a Branch Output filter (id: " ..
                tostring(filter_id) .. ")")
        obs.obs_source_release(bo_filter)
        return false
    end

    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", format_value)
    local result = obs.proc_handler_call(ph, OVERRIDE_PROC, cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)

    if not result then
        obs.script_log(obs.LOG_WARNING,
            "proc_handler_call for " .. OVERRIDE_PROC ..
                " failed — is the Branch Output plugin up to date?")
        return false
    end
    return true
end

local function update_replay_buffer_format()
    -- Read text source and update Branch Output replay buffer filename format.
    if text_source_uuid == "" or selected_filter == "" then
        return
    end

    local _, filter_uuid = parse_selected_filter(selected_filter)
    if filter_uuid == "" then
        return
    end

    -- Get text from the text source
    local text_source = obs.obs_get_source_by_uuid(text_source_uuid)
    if text_source == nil then
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
    -- proc re-call.
    if new_format == last_text then
        return
    end

    if call_override_proc(filter_uuid, new_format) then
        last_text = new_format
        override_cleared = false
        obs.script_log(obs.LOG_INFO, LOG_LABEL .. " updated: " .. new_format)
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
    -- Clear the override (empty string) and reset cached last_text so that
    -- if the text source reappears later with identical content, the
    -- override is re-applied instead of being swallowed by the "same text"
    -- early-return in update_replay_buffer_format().
    last_text = nil

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
    update_replay_buffer_format()
end

-- --- OBS Script Interface ---

function script_description()
    return [[<b>Replay Buffer Filename from Text Source</b><br><br>
Overrides a Branch Output replay buffer's filename format based on the content of a Text (GDI+) source.<br><br>
The text content is prepended to the base format on every change.<br><br>
<b>Note:</b> This script overrides the filename format at runtime. The filter's own property settings are not modified. When this script is unloaded, the override is cleared and the filter reverts to its original filename format setting.<br><br>
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

    -- Branch Output filter selector (populated from global proc handler)
    p = obs.obs_properties_add_list(
        props, "selected_filter", "Branch Output Filter",
        obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING
    )
    obs.obs_property_list_add_string(p, "(Select a filter)", "")
    for _, f in ipairs(get_branch_output_filters()) do
        local label = f.filter_name .. "  (" .. f.source_name .. ")"
        local value = f.source_uuid .. "::" .. f.filter_uuid
        obs.obs_property_list_add_string(p, label, value)
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
    -- Remember the previously selected filter so we can clear its override
    -- if the user changed the selection.
    local _, old_filter_uuid = parse_selected_filter(selected_filter)

    text_source_uuid = obs.obs_data_get_string(settings, "text_source")
    selected_filter = obs.obs_data_get_string(settings, "selected_filter")
    base_format = obs.obs_data_get_string(settings, "base_format")

    local _, new_filter_uuid = parse_selected_filter(selected_filter)
    if old_filter_uuid ~= "" and old_filter_uuid ~= new_filter_uuid then
        -- Clear override on the previously selected filter so it does not
        -- remain overridden after the user switched to a different filter.
        if call_override_proc(old_filter_uuid, "") then
            obs.script_log(obs.LOG_INFO,
                LOG_LABEL .. " override cleared on previous filter (uuid: " .. old_filter_uuid .. ")")
        end
    end

    -- If text source was deselected, clear the override on the current
    -- filter so it reverts to its own setting.
    if text_source_uuid == "" then
        clear_override()
        return
    end

    -- Reset last_text and override_cleared so the next tick re-applies the override
    last_text = nil
    override_cleared = false
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
end
