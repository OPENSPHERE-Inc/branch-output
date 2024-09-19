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
#include <util/threading.h>

#include <QString>
#include <QWidget>

using OBSString = OBSPtr<char *, (void (*)(char *))bfree>;
QString getOutputFilename(const char *path, const char *container, bool noSpace, bool overwrite, const char *format);
QString getFormatExt(const char *container);
using OBSMutexAutoUnlock = OBSPtr<pthread_mutex_t *, (void (*)(pthread_mutex_t *))pthread_mutex_unlock>;

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
