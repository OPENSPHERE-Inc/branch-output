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

#include <obs.hpp>
#include <obs-frontend-api.h>

#include <QList>
#include <QObject>
#include <QString>

#define PROGRAMS_JSON_NAME "branchOutputPrograms.json"
#define PROGRAMS_JSON_VERSION 1

class BranchOutputStatusDock;

// Owns the main outputs of the current profile and persists them to PROGRAMS_JSON_NAME.
// Used from the UI thread only.
class BranchOutputProgramManager : public QObject {
    Q_OBJECT

    QList<OBSSource> programs;     // Strong references; the only owner of main outputs
    bool persistenceReady = false; // The current profile's file has been read and may be written
    bool collectionSwitching = false;
    bool eventCallbackRegistered = false;

    enum class ReadResult { Absent, Loaded, Unreadable };
    ReadResult readProgramsFile(OBSDataArrayAutoRelease &outputs) const;
    void loadPrograms();
    void onProfileChanged();
    void releasePrograms(bool drain);
    bool adopt(obs_source_t *source);
    QString nextDefaultName() const;
    static QString programsJsonPath();
    static void onFrontendEvent(enum obs_frontend_event event, void *param);

public:
    explicit BranchOutputProgramManager(BranchOutputStatusDock *dock, QObject *parent = nullptr);
    ~BranchOutputProgramManager();

public slots:
    void addProgram();
    void savePrograms();
};
