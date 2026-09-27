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

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <obs.hpp>

#include <utility>

#include <QCoreApplication>
#include <QEvent>
#include <QSet>

#include "plugin-support.h"
#include "plugin-program.hpp"
#include "program-manager.hpp"
#include "UI/output-status-dock.hpp"
#include "utils.hpp"

BranchOutputProgramManager::BranchOutputProgramManager(BranchOutputStatusDock *dock, QObject *parent) : QObject(parent)
{
    obs_frontend_add_event_callback(onFrontendEvent, this);
    eventCallbackRegistered = true;

    if (dock) {
        connect(dock, &BranchOutputStatusDock::addMainOutputRequested, this, &BranchOutputProgramManager::addProgram);
    }
}

BranchOutputProgramManager::~BranchOutputProgramManager()
{
    if (eventCallbackRegistered) {
        obs_frontend_remove_event_callback(onFrontendEvent, this);
        eventCallbackRegistered = false;
    }

    releasePrograms(false);
}

QString BranchOutputProgramManager::programsJsonPath()
{
    OBSString profilePath = obs_frontend_get_current_profile_path();
    if (!profilePath) {
        return QString();
    }

    return QString("%1/%2").arg(QString(profilePath)).arg(PROGRAMS_JSON_NAME);
}

BranchOutputProgramManager::ReadResult BranchOutputProgramManager::readProgramsFile(OBSDataArrayAutoRelease &outputs
) const
{
    auto path = programsJsonPath();
    if (path.isEmpty()) {
        return ReadResult::Unreadable;
    }

    if (!os_file_exists(qUtf8Printable(path)) && !os_file_exists(qUtf8Printable(path + ".bak"))) {
        return ReadResult::Absent;
    }

    OBSDataAutoRelease doc = obs_data_create_from_json_file_safe(qUtf8Printable(path), "bak");
    if (!doc) {
        obs_log(LOG_ERROR, "Failed to read %s", qUtf8Printable(path));
        return ReadResult::Unreadable;
    }

    auto version = obs_data_get_int(doc, "version");
    if (version != PROGRAMS_JSON_VERSION) {
        obs_log(LOG_ERROR, "Unsupported version %lld of %s", version, qUtf8Printable(path));
        return ReadResult::Unreadable;
    }

    outputs = obs_data_get_array(doc, "outputs");
    return ReadResult::Loaded;
}

void BranchOutputProgramManager::loadPrograms()
{
    if (!programs.isEmpty()) {
        releasePrograms(false);
    }
    persistenceReady = false;

    OBSDataArrayAutoRelease outputs;
    auto result = readProgramsFile(outputs);
    if (result == ReadResult::Unreadable) {
        return;
    }

    if (result == ReadResult::Absent) {
        persistenceReady = true;
        return;
    }

    auto count = obs_data_array_count(outputs);
    for (size_t i = 0; i < count; i++) {
        OBSDataAutoRelease item = obs_data_array_item(outputs, i);
        auto id = obs_data_get_string(item, "id");
        if (strcmp(id, PROGRAM_SOURCE_ID)) {
            obs_log(LOG_WARNING, "Skip an entry of unknown type '%s' in %s", id, PROGRAMS_JSON_NAME);
            continue;
        }

        OBSSourceAutoRelease source = obs_load_private_source(item);
        if (!source) {
            obs_log(LOG_WARNING, "Failed to load main output '%s'", obs_data_get_string(item, "name"));
            continue;
        }

        adopt(source);
    }

    persistenceReady = true;
    obs_log(LOG_INFO, "Loaded %d main output(s)", static_cast<int>(programs.size()));
}

void BranchOutputProgramManager::savePrograms()
{
    auto path = programsJsonPath();
    if (!persistenceReady || path.isEmpty()) {
        obs_log(LOG_DEBUG, "Skip saving %s", PROGRAMS_JSON_NAME);
        return;
    }

    OBSDataAutoRelease doc = obs_data_create();
    obs_data_set_int(doc, "version", PROGRAMS_JSON_VERSION);

    OBSDataArrayAutoRelease outputs = obs_data_array_create();
    for (const auto &source : std::as_const(programs)) {
        // obs_save_source() embeds the live settings object, which must not be serialized itself
        OBSDataAutoRelease saved = obs_save_source(source);
        OBSDataAutoRelease copy = obs_data_create();
        obs_data_apply(copy, saved);
        obs_data_array_push_back(outputs, copy);
    }
    obs_data_set_array(doc, "outputs", outputs);

    if (!obs_data_save_json_safe(doc, qUtf8Printable(path), "tmp", "bak")) {
        obs_log(LOG_ERROR, "Failed to save %s", qUtf8Printable(path));
    }
}

void BranchOutputProgramManager::onProfileChanged()
{
    if (programs.isEmpty()) {
        loadPrograms();
        return;
    }

    OBSDataArrayAutoRelease outputs;
    if (readProgramsFile(outputs) == ReadResult::Loaded) {
        QSet<QString> fileUuids;
        auto count = obs_data_array_count(outputs);
        for (size_t i = 0; i < count; i++) {
            OBSDataAutoRelease item = obs_data_array_item(outputs, i);
            if (!strcmp(obs_data_get_string(item, "id"), PROGRAM_SOURCE_ID)) {
                fileUuids.insert(QString::fromUtf8(obs_data_get_string(item, "uuid")));
            }
        }

        QSet<QString> heldUuids;
        for (const auto &source : std::as_const(programs)) {
            heldUuids.insert(QString::fromUtf8(obs_source_get_uuid(source)));
        }

        // The profile was duplicated or renamed: its file mirrors the running outputs
        if (fileUuids == heldUuids) {
            obs_log(LOG_INFO, "Keep %d main output(s) across the profile change", static_cast<int>(programs.size()));
            persistenceReady = true;
            savePrograms();
            return;
        }
    }

    // FIXME: OBS 31+ activates a new profile without PROFILE_CHANGING, so obs_reset_video() runs while
    // main outputs still hold their views. Stop them before the reset once OBS offers a hook for it.
    releasePrograms(false);
    loadPrograms();
}

void BranchOutputProgramManager::releasePrograms(bool drain)
{
    for (const auto &source : std::as_const(programs)) {
        if (auto *program = BranchOutputProgram::fromSource(source)) {
            program->detach();
        }
    }
    programs.clear();
    persistenceReady = false;

    // Let the graphics and destroy threads drop the released sources, so their UUIDs are free
    // for an immediate reload. After EXIT no event loop runs, so the deferred deletes are sent here.
    if (drain) {
        do {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        } while (obs_wait_for_destroy_queue());
    } else {
        obs_wait_for_destroy_queue();
    }
}

bool BranchOutputProgramManager::adopt(obs_source_t *source)
{
    auto *program = BranchOutputProgram::fromSource(source);
    if (!program) {
        obs_log(LOG_WARNING, "Ignore '%s': not a main output", obs_source_get_name(source));
        return false;
    }

    programs.append(OBSSource(source));

    connect(
        program, &BranchOutputProgram::persistRequested, this, &BranchOutputProgramManager::savePrograms,
        Qt::QueuedConnection
    );
    connect(
        program, &BranchOutput::outputUserEnabledChanged, this, &BranchOutputProgramManager::savePrograms,
        Qt::QueuedConnection
    );

    program->setSuspended(collectionSwitching);
    program->attach();
    return true;
}

QString BranchOutputProgramManager::nextDefaultName() const
{
    QSet<QString> names;
    for (const auto &source : programs) {
        names.insert(QString::fromUtf8(obs_source_get_name(source)));
    }

    int number = 1;
    while (names.contains(QTStr("MainOutput.DefaultName").arg(number))) {
        number++;
    }

    return QTStr("MainOutput.DefaultName").arg(number);
}

void BranchOutputProgramManager::addProgram()
{
    if (!persistenceReady) {
        obs_log(LOG_WARNING, "Cannot add a main output: %s of the current profile is not loaded", PROGRAMS_JSON_NAME);
        return;
    }

    auto defaultName = nextDefaultName();
    OBSSourceAutoRelease source = obs_source_create_private(PROGRAM_SOURCE_ID, qUtf8Printable(defaultName), nullptr);
    if (!source) {
        obs_log(LOG_ERROR, "Failed to create main output '%s'", qUtf8Printable(defaultName));
        return;
    }

    if (!adopt(source)) {
        return;
    }
    savePrograms();

    obs_frontend_open_source_properties(source);
}

void BranchOutputProgramManager::onFrontendEvent(enum obs_frontend_event event, void *param)
{
    auto *manager = static_cast<BranchOutputProgramManager *>(param);

    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
        manager->collectionSwitching = false;
        manager->loadPrograms();
        break;
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
        manager->collectionSwitching = true;
        for (const auto &source : std::as_const(manager->programs)) {
            BranchOutputProgram::fromSource(source)->setSuspended(true);
        }
        break;
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
        manager->collectionSwitching = false;
        for (const auto &source : std::as_const(manager->programs)) {
            BranchOutputProgram::fromSource(source)->setSuspended(false);
        }
        // Duplicating a scene collection reassigns the UUIDs of every source, private ones included
        manager->savePrograms();
        break;
    case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
        manager->savePrograms();
        manager->releasePrograms(false);
        break;
    case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
        manager->onProfileChanged();
        break;
    case OBS_FRONTEND_EVENT_EXIT:
        manager->savePrograms();
        manager->releasePrograms(true);
        obs_frontend_remove_event_callback(onFrontendEvent, manager);
        manager->eventCallbackRegistered = false;
        break;
    default:
        break;
    }
}
