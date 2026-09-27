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
#include <obs.hpp>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "plugin-support.h"
#include "plugin-program.hpp"
#include "video/main-texture-proxy.hpp"
#include "utils.hpp"

// Keys written outside the properties view; they never affect the outputs
static const char *const nonPropertyKeys[] = {
    "preview_crop_rect_rel",
    "preview_crop_rect_abs",
    "replay_buffer_estimate",
    "recording_output_enabled",
    "replay_buffer_output_enabled",
    HOTKEY_BINDINGS_KEY,
    "undo_uuid",
};

static QJsonObject toComparableJson(obs_data_t *settings)
{
    OBSDataAutoRelease copy = obs_data_create();
    obs_data_apply(copy, settings);

    for (auto key : nonPropertyKeys) {
        obs_data_erase(copy, key);
    }
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        auto key = QString("streaming_output_enabled_%1").arg(i);
        obs_data_erase(copy, qUtf8Printable(key));
    }

    const char *switchKeys[] = {"streaming_enabled", "stream_recording", "replay_buffer"};
    for (auto key : switchKeys) {
        obs_data_set_bool(copy, key, obs_data_get_bool(settings, key));
    }

    auto json = obs_data_get_json(copy);
    return QJsonDocument::fromJson(QByteArray(json ? json : "{}")).object();
}

// Compares the user values of a and b (the output type switches by their effective value),
// ignoring the non-property keys and the key order.
static bool settingsEquivalent(obs_data_t *a, obs_data_t *b)
{
    return toComparableJson(a) == toComparableJson(b);
}

// The properties dialog reports unsaved changes against the settings at open, so these keys are
// kept as false user values while no dialog is open.
static void resetTransientCheckboxes(obs_data_t *settings)
{
    obs_data_set_bool(settings, "preview_crop_rect_rel", false);
    obs_data_set_bool(settings, "preview_crop_rect_abs", false);
    obs_data_set_bool(settings, "replay_buffer_estimate", false);
}

//--- BranchOutputProgram class ---//

BranchOutputProgram::BranchOutputProgram(obs_data_t *settings, obs_source_t *source, QObject *parent)
    : BranchOutput(settings, source, parent),
      intervalTimer(nullptr),
      attached(false),
      suspended(false),
      openProperties(0)
{
    obs_log(LOG_DEBUG, "%s: BranchOutputProgram creating", qUtf8Printable(name));

    const char *json = obs_data_get_json(settings);
    bool initialCreation = json && !strcmp(json, "{}");
    // The crop in recently.json is in the coordinates of a filter's parent source, not the canvas
    initializeSettings(settings, initialCreation, false);
    resetTransientCheckboxes(settings);

    // No bindings saved by an earlier version exist to harvest
    hotkeyHarvestPending = false;

    contextWeak = obs_source_get_weak_source(contextSource);

    renamedSignal.Connect(
        obs_source_get_signal_handler(contextSource), "rename",
        [](void *data, calldata_t *cd) {
            auto program = fromProgramCallbackData(data);
            program->updateHotkeyDescriptions(QString::fromUtf8(calldata_string(cd, "new_name")));
            emit program->persistRequested();
        },
        toCallbackData()
    );
    enabledSignal.Connect(
        obs_source_get_signal_handler(contextSource), "enable",
        [](void *data, calldata_t *) {
            auto program = fromProgramCallbackData(data);
            emit program->persistRequested();
        },
        toCallbackData()
    );

    obs_log(LOG_INFO, "%s: BranchOutputProgram created", qUtf8Printable(name));
}

bool BranchOutputProgram::validateInput()
{
    if (suspended) {
        obs_log(LOG_DEBUG, "%s: Ignore while the scene collection is switching", qUtf8Printable(name));
        return false;
    }

    return true;
}

bool BranchOutputProgram::isInputAvailable() const
{
    return !suspended;
}

QString BranchOutputProgram::getInputName() const
{
    return QTStr("MainOutput");
}

QString BranchOutputProgram::getInputUuid() const
{
    return QString();
}

void BranchOutputProgram::acquireInputShowing() {}

void BranchOutputProgram::releaseInputShowing() {}

void BranchOutputProgram::getSourceResolution(uint32_t &outWidth, uint32_t &outHeight)
{
    obs_video_info ovi = {};
    if (obs_get_video_info(&ovi)) {
        outWidth = ovi.base_width;
        outHeight = ovi.base_height;
    } else {
        outWidth = 0;
        outHeight = 0;
    }
    // Round down to a multiple of 2: the main texture has no pixels beyond the canvas
    outWidth &= ~1u;
    outHeight &= ~1u;
}

void BranchOutputProgram::selectVideoInputMode(obs_data_t *) {}

// Caller must hold outputMutex.
// On failure, everything created here has already been cleaned up.
bool BranchOutputProgram::setupVideoInput(obs_data_t *, obs_video_info *ovi, const CropRect &crop)
{
    outputProxy = createMainTextureProxy(crop);
    if (!outputProxy) {
        obs_log(LOG_ERROR, "%s: Main texture proxy creation failed", qUtf8Printable(name));
        return false;
    }

    view = obs_view_create();
    videoOutputOwned = true;
    obs_view_set_source(view, 0, outputProxy);

    videoOutput = obs_view_add2(view, ovi);
    if (!videoOutput) {
        obs_log(LOG_ERROR, "%s: Video output association failed", qUtf8Printable(name));
        releaseInfrastructureIfIdle();
        return false;
    }

    return true;
}

// Caller must hold outputMutex. Safe to call on a partially built video input.
void BranchOutputProgram::teardownVideoInput()
{
    outputProxy = nullptr;
}

// Caller must hold outputMutex.
// On failure, everything created here has already been cleaned up.
bool BranchOutputProgram::setupDefaultAudio(const obs_audio_info &)
{
    obs_log(LOG_INFO, "%s: Use master audio track No.1 for track 1", qUtf8Printable(name));
    auto audioContext = &audios[0];
    audioContext->mixIndex = 0;
    audioContext->audio = obs_get_audio();
    audioContext->streaming = true;
    audioContext->recording = true;
    audioContext->name = QTStr("MasterTrack%1").arg(1);

    if (!audioContext->audio) {
        obs_log(LOG_ERROR, "%s: Audio creation failed", qUtf8Printable(name));
        releaseInfrastructureIfIdle();
        return false;
    }

    return true;
}

bool BranchOutputProgram::evaluateBlanking(obs_data_t *)
{
    return false;
}

BranchOutput::BlankingState BranchOutputProgram::getBlankingState() const
{
    return BLANKING_STATE_NONE;
}

bool BranchOutputProgram::hasFilterPipeline() const
{
    return false;
}

bool BranchOutputProgram::canRegisterHotkeys()
{
    return false;
}

bool BranchOutputProgram::beginHotkeyRegistration()
{
    return false;
}

void BranchOutputProgram::endHotkeyRegistration() {}

obs_hotkey_id BranchOutputProgram::registerHotkey(const QString &, const QString &, obs_hotkey_func)
{
    return OBS_INVALID_HOTKEY_ID;
}

obs_hotkey_pair_id BranchOutputProgram::registerHotkeyPair(
    const QString &, const QString &, const QString &, const QString &, obs_hotkey_active_func, obs_hotkey_active_func
)
{
    return OBS_INVALID_HOTKEY_PAIR_ID;
}

void BranchOutputProgram::openSettings()
{
    // Reset to defaults in the dialog drops the pinned switches
    OBSDataAutoRelease settings = obs_source_get_settings(contextSource);
    pinOutputTypeSwitches(settings);

    obs_frontend_open_source_properties(contextSource);
}

void BranchOutputProgram::updateCallback(obs_data_t *settings)
{
    initialized = true;

    // FIXME: the deferred update runs this on the graphics thread while the properties view edits
    // the same live settings on the UI thread; obs_data has no lock, so the comparison and replace()
    // below walk the object unsynchronized. Confine live-settings traversal to the UI thread.
    OBSDataAutoRelease current = appliedSettings.get();
    if (current && settingsEquivalent(settings, current)) {
        obs_log(LOG_DEBUG, "%s: Main output settings unchanged", qUtf8Printable(name));
        return;
    }

    // The interval timer restarts the output once it sees the new snapshot.
    appliedSettings.replace(settings);

    syncHotkeys(settings);

    if (attached) {
        if (auto *dock = loadStatusDock()) {
            QMetaObject::invokeMethod(dock, "addOutput", Qt::QueuedConnection, Q_ARG(BranchOutput *, this));
        }
    }

    emit persistRequested();

    obs_log(LOG_INFO, "%s: Main output updated", qUtf8Printable(name));
}

void BranchOutputProgram::videoTickCallback(float)
{
    // Update crop preview when canvas resolution changes
    if (cropPreview.isVisible()) {
        uint32_t curW, curH;
        getSourceResolution(curW, curH);
        if (curW > 0 && curH > 0 && cropPreview.resolutionChanged(curW, curH)) {
            OBSDataAutoRelease settings = obs_source_get_settings(contextSource);
            cropPreview.updateResolution(curW, curH, calculateCrop(curW, curH, settings));
        }
    }
}

// Draws the properties dialog preview only; the outputs render through outputProxy.
void BranchOutputProgram::videoRenderCallback(gs_effect_t *)
{
    renderMainTexture(0, 0);
    cropPreview.render();
}

void BranchOutputProgram::destroyCallback()
{
    obs_log(LOG_DEBUG, "%s: BranchOutputProgram destroying", qUtf8Printable(name));

    renamedSignal.Disconnect();
    enabledSignal.Disconnect();

    // Release all handles
    stopOutput();

    obs_log(LOG_INFO, "%s: BranchOutputProgram destroyed", qUtf8Printable(name));

    // The only place QObject destruction starts. The UI thread may delete this object at once.
    deleteLater();
}

obs_properties_t *BranchOutputProgram::getProperties()
{
    openProperties++;

    auto props = obs_properties_create();
    obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

    // Ensure transient checkboxes start unchecked
    OBSDataAutoRelease settings = obs_source_get_settings(contextSource);
    resetTransientCheckboxes(settings);

    obs_properties_set_param(props, toCallbackData(), [](void *param) {
        auto program = fromProgramCallbackData(param);
        program->cropPreview.hide();
        program->onPropertiesDestroyed();
    });

    addStreamingGroup(props);
    addRecordingGroup(props);
    addReplayBufferGroup(props);
    addAudioGroup(props);
    addAudioEncoderGroup(props);
    addVideoEncoderGroup(props);
    addAdvancedSettingsGroup(props);

    addApplyButton(props, "applyLast");
    addPluginInfo(props);

    return props;
}

// The standard properties dialog restores the live settings on Cancel without calling update, so
// they can differ from the applied snapshot after an in-dialog Apply. Once the last properties
// object is gone the live settings are final: apply them if they differ, and persist them anyway
// because a save made while the dialog was open may have written unapplied edits.
void BranchOutputProgram::onPropertiesDestroyed()
{
    // Reloading the properties creates the new object before destroying the old one.
    if (--openProperties > 0) {
        return;
    }

    OBSSourceAutoRelease source = obs_weak_source_get_source(contextWeak);
    if (!source) {
        return;
    }

    OBSDataAutoRelease live = obs_source_get_settings(source);
    resetTransientCheckboxes(live);
    updateCallback(live);
    emit persistRequested();
}

void BranchOutputProgram::attach()
{
    if (!intervalTimer) {
        intervalTimer = new QTimer(this);
        intervalTimer->setInterval(TASK_INTERVAL_MS);
        connect(intervalTimer, SIGNAL(timeout()), this, SLOT(onIntervalTimerTimeout()));
        intervalTimer->start();
    }

    attached = true;

    if (auto *dock = loadStatusDock()) {
        QMetaObject::invokeMethod(dock, "addOutput", Qt::QueuedConnection, Q_ARG(BranchOutput *, this));
    }

    OBSDataAutoRelease settings = obs_source_get_settings(contextSource);
    syncHotkeys(settings);
}

void BranchOutputProgram::detach()
{
    if (intervalTimer) {
        intervalTimer->stop();
        delete intervalTimer;
        intervalTimer = nullptr;
    }

    attached = false;

    // Synchronous: the caller may reset the video (obs_reset_video()) right after this returns,
    // so the view must be gone by then.
    stopOutput();

    if (auto *dock = loadStatusDock()) {
        QMetaObject::invokeMethod(dock, "removeOutput", Qt::QueuedConnection, Q_ARG(BranchOutput *, this));
    }

    unregisterAllHotkeys();
}

void BranchOutputProgram::setSuspended(bool suspend)
{
    bool wasSuspended = suspended.exchange(suspend);

    if (suspend) {
        // A pending stop is continued by the interval timer, which never starts while suspended.
        stopOutputGracefully();
    } else if (wasSuspended && attached) {
        // The dock drops the rows of an unavailable input, so register again.
        if (auto *dock = loadStatusDock()) {
            QMetaObject::invokeMethod(dock, "addOutput", Qt::QueuedConnection, Q_ARG(BranchOutput *, this));
        }
    }
}

BranchOutputProgram *BranchOutputProgram::fromSource(obs_source_t *source)
{
    auto id = obs_source_get_unversioned_id(source);
    if (!id || strcmp(id, PROGRAM_SOURCE_ID)) {
        return nullptr;
    }

    return fromProgramCallbackData(obs_obj_get_data(source));
}

void BranchOutputProgram::getProgramDefaults(obs_data_t *defaults)
{
    getDefaults(defaults);
    obs_data_set_default_string(defaults, "resolution", "output");
}

static uint32_t getCanvasWidth()
{
    obs_video_info ovi = {};
    return obs_get_video_info(&ovi) ? ovi.base_width : 0;
}

static uint32_t getCanvasHeight()
{
    obs_video_info ovi = {};
    return obs_get_video_info(&ovi) ? ovi.base_height : 0;
}

obs_source_info BranchOutputProgram::createProgramInfo()
{
    obs_source_info info = {};

    info.id = PROGRAM_SOURCE_ID;
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_CAP_DISABLED;

    info.get_name = [](void *) {
        return "Branch Output Main Output";
    };

    info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
        auto program = new BranchOutputProgram(settings, source);
        return program->toCallbackData();
    };
    info.destroy = [](void *data) {
        auto program = fromProgramCallbackData(data);
        program->destroyCallback();
    };
    info.update = [](void *data, obs_data_t *settings) {
        auto program = fromProgramCallbackData(data);
        program->updateCallback(settings);
    };
    info.save = [](void *data, obs_data_t *settings) {
        auto program = fromProgramCallbackData(data);
        program->saveCallback(settings);
    };
    info.get_properties = [](void *data) -> obs_properties_t * {
        if (!data) {
            return obs_properties_create();
        }
        auto program = fromProgramCallbackData(data);
        return program->getProperties();
    };
    info.get_defaults = BranchOutputProgram::getProgramDefaults;

    info.video_tick = [](void *data, float seconds) {
        auto program = fromProgramCallbackData(data);
        program->videoTickCallback(seconds);
    };
    info.video_render = [](void *data, gs_effect_t *effect) {
        auto program = fromProgramCallbackData(data);
        program->videoRenderCallback(effect);
    };
    info.get_width = [](void *) {
        return getCanvasWidth();
    };
    info.get_height = [](void *) {
        return getCanvasHeight();
    };
    info.video_get_color_space = [](void *, size_t, const enum gs_color_space *) {
        return getMainCanvasColorSpace();
    };

    return info;
}
