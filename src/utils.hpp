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
inline void setThemeID(QWidget *widget, const QString &themeID)
{
    if (widget->property("themeID").toString() != themeID) {
        widget->setProperty("themeID", themeID);

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
