/*
Branch Output Plugin
Copyright (C) 2024 OPENSPHERE Inc. info@opensphere.co.jp

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <obs-module.h>
#include <obs.hpp>
#include <obs-frontend-api.h>
#include <util/threading.h>
#include <util/config-file.h>

#include <QString>
#include <QWidget>
#include <QVariant>

QString getOutputFilename(const char *path, const char *container, bool noSpace, bool overwrite, const char *format);
QString getFormatExt(const char *container);

inline void strFree(char *ptr)
{
    bfree(ptr);
}
using OBSString = OBSPtr<char *, strFree>;

inline void pthreadMutexUnlock(pthread_mutex_t *mutex)
{
    pthread_mutex_unlock(mutex);
}
using OBSMutexAutoUnlock = OBSPtr<pthread_mutex_t *, pthreadMutexUnlock>;

// Imitate UI/window-basic-stats.cpp
// themeID: until OBS 30
// themeClasses: since OBS 31
inline void setThemeID(QWidget *widget, const QString &themeID, const QString &themeClasses)
{
    bool changed = false;
    if (widget->property("themeID").toString() != themeID) {
        widget->setProperty("themeID", themeID);
        changed = true;
    }

    if (widget->property("class").toString() != themeClasses) {
        widget->setProperty("class", themeClasses);
        changed = true;
    }

    if (changed) {
        /* force style sheet recalculation */
        QString qss = widget->styleSheet();
        widget->setStyleSheet("/* */");
        widget->setStyleSheet(qss);
    }
}

inline QString QTStr(const char *lookupVal)
{
    return QString::fromUtf8(obs_module_text(lookupVal));
}

// Decide source/scene is displayed in frontend or not
inline bool sourceInFrontend(obs_source_t *source)
{
    if (!source) {
        return false;
    }

    auto found = false;

    obs_frontend_source_list lsit = {0};
    obs_frontend_get_scenes(&lsit);
    {
        for (size_t i = 0; i < lsit.sources.num && !found; i++) {
            if (lsit.sources.array[i] == source) {
                found = true;
                break;
            }
            obs_scene_t *scene = obs_scene_from_source(lsit.sources.array[i]);
            found = !!obs_scene_find_source_recursive(scene, obs_source_get_name(source));
        }
    }
    obs_frontend_source_list_free(&lsit);

    return found;
}

inline void setAudioSourceListName(char *name, size_t len, size_t track)
{
    if (track > 1) {
        snprintf(name, len, "audio_source_%zu", track % 10);
    } else {
        snprintf(name, len, "audio_source");
    }
}

inline void setAudioTrackListName(char *name, size_t len, size_t track)
{
    if (track > 1) {
        snprintf(name, len, "audio_track_%zu", track % 10);
    } else {
        snprintf(name, len, "audio_track");
    }
}

inline void setAudioDestListName(char *name, size_t len, size_t track)
{
    if (track > 1) {
        snprintf(name, len, "audio_dest_%zu", track % 10);
    } else {
        snprintf(name, len, "audio_dest");
    }
}

// Return value must be obs_data_release() after use
inline obs_data_t *loadHotkeyData(const char *name)
{
    auto config = obs_frontend_get_profile_config();
    auto info = config_get_string(config, "Hotkeys", name);
    if (!info) {
        return nullptr;
    }

    return obs_data_create_from_json(info);
}

inline void loadHotkey(obs_hotkey_id id, const char *name)
{
    OBSDataAutoRelease data = loadHotkeyData(name);
    if (data) {
        OBSDataArrayAutoRelease array = obs_data_get_array(data, "bindings");
        obs_hotkey_load(id, array);
    }
}
