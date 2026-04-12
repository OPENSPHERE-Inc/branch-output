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

-- Script settings
local text_source_uuid = ""
local selected_filter = "" -- "source_uuid::filter_uuid" format
local base_format = "%CCYY-%MM-%DD %hh-%mm-%ss"
local last_text = nil

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
            if data ~= nil then
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

local function sanitize_filename(text)
    -- Produce a filesystem-safe prefix:
    --   1. Replace all control characters (incl. CR/LF/TAB) with a single space
    --   2. Collapse runs of whitespace to a single space and trim
    --   3. Replace filesystem-unsafe characters with "-"
    --   4. Strip trailing dots/spaces (Windows disallows these at end of filename)
    --   5. Prefix an underscore if the result collides with a Windows reserved name
    if text == nil then
        return ""
    end
    local cleaned = text:gsub("%c", " ")
    cleaned = cleaned:gsub("%s+", " ")
    local trimmed = cleaned:match("^%s*(.-)%s*$") or ""
    local sanitized = trimmed:gsub('[<>:"|?*/\\]', "-")
    sanitized = sanitized:gsub("[%.%s]+$", "")
    if WINDOWS_RESERVED[sanitized:upper()] then
        sanitized = "_" .. sanitized
    end
    return sanitized
end

local function read_text_from_source(text_source)
    -- Returns (text, ok). ok = false means the caller should clear the override.
    local settings = obs.obs_source_get_settings(text_source)
    local result_text = nil
    local ok = true

    local read_from_file = obs.obs_data_get_bool(settings, "read_from_file")
    if read_from_file then
        local file_path = obs.obs_data_get_string(settings, "file")
        if file_path == "" then
            ok = false
        else
            -- Open in binary mode so BOM bytes are not translated.
            local f, err = io.open(file_path, "rb")
            if f then
                -- Limit read size to prevent performance issues on accidental large-file selection.
                local data = f:read(MAX_READ_SIZE) or ""
                f:close()
                -- UTF-8 BOM: strip it.
                if data:sub(1, 3) == "\239\187\191" then
                    data = data:sub(4)
                -- UTF-16 LE/BE BOM: not supported by this sample; warn and clear.
                elseif data:sub(1, 2) == "\255\254" or data:sub(1, 2) == "\254\255" then
                    obs.script_log(obs.LOG_WARNING,
                        "UTF-16 text files are not supported; please save the text file as UTF-8")
                    ok = false
                end
                if ok then
                    result_text = data
                end
            else
                obs.script_log(obs.LOG_WARNING, "Failed to read text file: " .. tostring(err))
                ok = false
            end
        end
    else
        result_text = obs.obs_data_get_string(settings, "text")
    end

    obs.obs_data_release(settings)
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

    local filter_id = obs.obs_source_get_id(bo_filter)
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
        clear_override()
        return
    end

    local current_text, ok = read_text_from_source(text_source)
    obs.obs_source_release(text_source)

    if not ok or current_text == nil then
        clear_override()
        return
    end

    -- Skip if text hasn't changed
    if current_text == last_text then
        return
    end
    last_text = current_text

    -- Build the new format string
    local sanitized = sanitize_filename(current_text)
    local new_format
    if sanitized ~= "" then
        new_format = sanitized .. " " .. base_format
    else
        new_format = base_format
    end

    if call_override_proc(filter_uuid, new_format) then
        obs.script_log(obs.LOG_INFO, LOG_LABEL .. " updated: " .. new_format)
    end
end

clear_override = function()
    -- Clear the filename format override by sending an empty string.
    if selected_filter == "" then
        return
    end

    local _, filter_uuid = parse_selected_filter(selected_filter)
    if filter_uuid == "" then
        return
    end

    if call_override_proc(filter_uuid, "") then
        obs.script_log(obs.LOG_INFO, LOG_LABEL .. " override cleared")
    end
end

local function timer_callback()
    update_replay_buffer_format()
end

-- --- OBS Script Interface ---

function script_description()
    return [[<b>Replay Buffer Filename from Text Source</b><br><br>
Overrides a Branch Output replay buffer's filename format based on the content of a Text (GDI+) source.<br><br>
The text content is prepended to the base format on every change.<br><br>
<b>Note:</b> This script overrides the filename format at runtime. The filter's own property settings are not modified. When this script is unloaded, the override is cleared and the filter reverts to its original filename format setting.]]
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
            if source_id == "text_gdiplus" or source_id == "text_gdiplus_v2"
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

    -- Reset last_text to force update on next tick
    last_text = nil
end

function script_load(settings)
    -- Defensive timer_remove in case of script reload.
    obs.timer_remove(timer_callback)
    -- Apply initial settings so the first tick has valid state.
    script_update(settings)
    obs.timer_add(timer_callback, 1000)
end

function script_unload()
    obs.timer_remove(timer_callback)
    clear_override()
end
