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
#include <obs.hpp>

#include "plugin-support.h"
#include "plugin-main.hpp"
#include "utils.hpp"

// Hotkey base names and locale keys. Base names become part of the persisted hotkey name
// ("<base>.<uuid>"), which OBS and HOTKEY_BINDINGS_KEY use to match saved key bindings.
struct HotkeyText {
    const char *base;
    const char *textKey;
};

static const HotkeyText HK_ENABLE_FILTER{"EnableFilter", "EnableHotkey"};
static const HotkeyText HK_DISABLE_FILTER{"DisableFilter", "DisableHotkey"};
static const HotkeyText HK_ENABLE_ALL_STREAMING{"EnableAllStreaming", "EnableAllStreamingHotkey"};
static const HotkeyText HK_DISABLE_ALL_STREAMING{"DisableAllStreaming", "DisableAllStreamingHotkey"};
static const HotkeyText HK_ENABLE_STREAMING_SERVICE{"EnableStreamingService", "EnableStreamingServiceHotkey"};
static const HotkeyText HK_DISABLE_STREAMING_SERVICE{"DisableStreamingService", "DisableStreamingServiceHotkey"};
static const HotkeyText HK_SPLIT_RECORDING{"SplitRecordingFile", "SplitRecordingFileHotkey"};
static const HotkeyText HK_PAUSE_RECORDING{"PauseRecording", "PauseRecordingHotkey"};
static const HotkeyText HK_UNPAUSE_RECORDING{"UnpauseRecording", "UnpauseRecordingHotkey"};
static const HotkeyText HK_ADD_CHAPTER_TO_RECORDING{"AddChapterToRecordingFile", "AddChapterToRecordingFileHotkey"};
static const HotkeyText HK_ENABLE_RECORDING{"EnableRecordingIndividual", "EnableRecordingIndividualHotkey"};
static const HotkeyText HK_DISABLE_RECORDING{"DisableRecordingIndividual", "DisableRecordingIndividualHotkey"};
static const HotkeyText HK_SAVE_REPLAY_BUFFER{"SaveReplayBuffer", "SaveReplayBufferHotkey"};
static const HotkeyText HK_ENABLE_REPLAY_BUFFER{"EnableReplayBufferIndividual", "EnableReplayBufferIndividualHotkey"};
static const HotkeyText HK_DISABLE_REPLAY_BUFFER{"DisableReplayBufferIndividual", "DisableReplayBufferIndividualHotkey"};

// Caller must hold the libobs hotkey mutex.
static void captureHotkeyBindings(obs_data_t *cache, obs_hotkey_id id, const QString &fullName)
{
    if (id == OBS_INVALID_HOTKEY_ID) {
        return;
    }

    OBSDataArrayAutoRelease live = obs_hotkey_save(id);
    if (live) {
        obs_data_set_array(cache, qUtf8Printable(fullName), live);
    }
}

// Caller must hold the libobs hotkey mutex.
static void
captureHotkeyPairBindings(obs_data_t *cache, obs_hotkey_pair_id id, const QString &fullName0, const QString &fullName1)
{
    if (id == OBS_INVALID_HOTKEY_PAIR_ID) {
        return;
    }

    // obs_hotkey_pair_save() leaves the output pointer of a side it cannot find untouched.
    obs_data_array_t *rawLive0 = nullptr;
    obs_data_array_t *rawLive1 = nullptr;
    obs_hotkey_pair_save(id, &rawLive0, &rawLive1);
    OBSDataArrayAutoRelease live0 = rawLive0;
    OBSDataArrayAutoRelease live1 = rawLive1;

    if (live0) {
        obs_data_set_array(cache, qUtf8Printable(fullName0), live0);
    }
    if (live1) {
        obs_data_set_array(cache, qUtf8Printable(fullName1), live1);
    }
}

QString BranchOutputFilter::buildHotkeyName(const char *base) const
{
    return QString("%1.%2").arg(base).arg(obs_source_get_uuid(filterSource));
}

QString BranchOutputFilter::buildStreamingSlotHotkeyName(const char *base, size_t index) const
{
    return QString("%1%2.%3").arg(base).arg(index).arg(obs_source_get_uuid(filterSource));
}

QString BranchOutputFilter::buildHotkeyDescription(const char *textKey) const
{
    return QString(obs_module_text(textKey)).arg(name);
}

QString BranchOutputFilter::buildStreamingSlotHotkeyDescription(const char *textKey, size_t index) const
{
    return QString(obs_module_text(textKey)).arg(name).arg(index + 1);
}

// Caller must hold the libobs hotkey mutex.
obs_hotkey_id BranchOutputFilter::registerHotkeyWithRestore(
    obs_source_t *parent, const QString &fullName, const QString &description, obs_hotkey_func func
)
{
    auto id = obs_hotkey_register_source(parent, qUtf8Printable(fullName), qUtf8Printable(description), func, this);
    if (id == OBS_INVALID_HOTKEY_ID) {
        return id;
    }

    // obs_hotkey_save() reports what libobs restored from the scene collection on registration.
    // The parent's saved data is only refreshed when the parent is saved, so it can be older
    // than the cache.
    bool owned = hotkeyCacheOwnedNames.contains(fullName);
    OBSDataArrayAutoRelease live = obs_hotkey_save(id);
    if (!owned && obs_data_array_count(live) > 0) {
        return id;
    }

    OBSDataArrayAutoRelease cached = obs_data_get_array(hotkeyBindingsCache, qUtf8Printable(fullName));
    if (!owned && obs_data_array_count(cached) == 0) {
        return id;
    }

    // An owned name loads even an empty cache, which clears the bindings the user unassigned.
    obs_hotkey_load(id, cached);
    obs_log(
        LOG_DEBUG, "%s: Restored hotkey bindings for '%s' from cache", qUtf8Printable(name), qUtf8Printable(fullName)
    );

    return id;
}

// Caller must hold the libobs hotkey mutex.
obs_hotkey_pair_id BranchOutputFilter::registerHotkeyPairWithRestore(
    obs_source_t *parent, const QString &fullName0, const QString &description0, const QString &fullName1,
    const QString &description1, obs_hotkey_active_func func0, obs_hotkey_active_func func1
)
{
    auto id = obs_hotkey_pair_register_source(
        parent, qUtf8Printable(fullName0), qUtf8Printable(description0), qUtf8Printable(fullName1),
        qUtf8Printable(description1), func0, func1, this, this
    );
    if (id == OBS_INVALID_HOTKEY_PAIR_ID) {
        return id;
    }

    obs_data_array_t *rawLive0 = nullptr;
    obs_data_array_t *rawLive1 = nullptr;
    obs_hotkey_pair_save(id, &rawLive0, &rawLive1);
    OBSDataArrayAutoRelease live0 = rawLive0;
    OBSDataArrayAutoRelease live1 = rawLive1;

    OBSDataArrayAutoRelease cached0 = obs_data_get_array(hotkeyBindingsCache, qUtf8Printable(fullName0));
    OBSDataArrayAutoRelease cached1 = obs_data_get_array(hotkeyBindingsCache, qUtf8Printable(fullName1));
    // obs_hotkey_pair_load() ignores the call when both arrays are null.
    if (!cached0) {
        cached0 = obs_data_array_create();
    }
    if (!cached1) {
        cached1 = obs_data_array_create();
    }

    // An owned name restores even an empty cache, which clears the bindings the user unassigned.
    bool restore0 = hotkeyCacheOwnedNames.contains(fullName0) ||
                    (obs_data_array_count(live0) == 0 && obs_data_array_count(cached0) > 0);
    bool restore1 = hotkeyCacheOwnedNames.contains(fullName1) ||
                    (obs_data_array_count(live1) == 0 && obs_data_array_count(cached1) > 0);
    if (!restore0 && !restore1) {
        return id;
    }

    // obs_hotkey_pair_load() clears both sides before loading, so the side that is not restored
    // has to be handed back its current bindings.
    obs_data_array_t *loadData0 = restore0 ? cached0 : live0;
    obs_data_array_t *loadData1 = restore1 ? cached1 : live1;
    obs_hotkey_pair_load(id, loadData0, loadData1);

    if (restore0) {
        obs_log(
            LOG_DEBUG, "%s: Restored hotkey bindings for '%s' from cache", qUtf8Printable(name),
            qUtf8Printable(fullName0)
        );
    }
    if (restore1) {
        obs_log(
            LOG_DEBUG, "%s: Restored hotkey bindings for '%s' from cache", qUtf8Printable(name),
            qUtf8Printable(fullName1)
        );
    }

    return id;
}

// Caller must hold the libobs hotkey mutex.
void BranchOutputFilter::unregisterHotkeyWithCapture(obs_hotkey_id &id, const QString &fullName)
{
    if (id == OBS_INVALID_HOTKEY_ID) {
        return;
    }

    captureHotkeyBindings(hotkeyBindingsCache, id, fullName);
    hotkeyCacheOwnedNames.insert(fullName);
    obs_hotkey_unregister(id);
    id = OBS_INVALID_HOTKEY_ID;
}

// Caller must hold the libobs hotkey mutex.
void BranchOutputFilter::unregisterHotkeyPairWithCapture(
    obs_hotkey_pair_id &id, const QString &fullName0, const QString &fullName1
)
{
    if (id == OBS_INVALID_HOTKEY_PAIR_ID) {
        return;
    }

    captureHotkeyPairBindings(hotkeyBindingsCache, id, fullName0, fullName1);
    hotkeyCacheOwnedNames.insert(fullName0);
    hotkeyCacheOwnedNames.insert(fullName1);
    obs_hotkey_pair_unregister(id);
    id = OBS_INVALID_HOTKEY_PAIR_ID;
}

// Caller must hold the libobs hotkey mutex.
void BranchOutputFilter::syncFilterToggleHotkeys(obs_source_t *parent, bool desired)
{
    bool registered = toggleEnableHotkeyPairId != OBS_INVALID_HOTKEY_PAIR_ID;
    if (desired == registered) {
        return;
    }

    auto enableName = buildHotkeyName(HK_ENABLE_FILTER.base);
    auto disableName = buildHotkeyName(HK_DISABLE_FILTER.base);

    if (desired) {
        toggleEnableHotkeyPairId = registerHotkeyPairWithRestore(
            parent, enableName, buildHotkeyDescription(HK_ENABLE_FILTER.textKey), disableName,
            buildHotkeyDescription(HK_DISABLE_FILTER.textKey), onEnableFilterHotkeyPressed, onDisableFilterHotkeyPressed
        );
        if (toggleEnableHotkeyPairId == OBS_INVALID_HOTKEY_PAIR_ID) {
            obs_log(LOG_WARNING, "%s: Filter toggle hotkey registration failed", qUtf8Printable(name));
            return;
        }
    } else {
        unregisterHotkeyPairWithCapture(toggleEnableHotkeyPairId, enableName, disableName);
    }

    obs_log(LOG_DEBUG, "%s: Filter toggle hotkeys %s", qUtf8Printable(name), desired ? "registered" : "unregistered");
}

// Caller must hold the libobs hotkey mutex.
void BranchOutputFilter::syncStreamingAllHotkeys(obs_source_t *parent, bool desired)
{
    bool registered = enableAllStreamingHotkeyId != OBS_INVALID_HOTKEY_ID;
    if (desired == registered) {
        return;
    }

    auto enableName = buildHotkeyName(HK_ENABLE_ALL_STREAMING.base);
    auto disableName = buildHotkeyName(HK_DISABLE_ALL_STREAMING.base);

    if (desired) {
        enableAllStreamingHotkeyId = registerHotkeyWithRestore(
            parent, enableName, buildHotkeyDescription(HK_ENABLE_ALL_STREAMING.textKey),
            onEnableAllStreamingHotkeyPressed
        );
        if (enableAllStreamingHotkeyId == OBS_INVALID_HOTKEY_ID) {
            obs_log(LOG_WARNING, "%s: Streaming hotkey registration failed", qUtf8Printable(name));
            return;
        }
        disableAllStreamingHotkeyId = registerHotkeyWithRestore(
            parent, disableName, buildHotkeyDescription(HK_DISABLE_ALL_STREAMING.textKey),
            onDisableAllStreamingHotkeyPressed
        );
    } else {
        unregisterHotkeyWithCapture(enableAllStreamingHotkeyId, enableName);
        unregisterHotkeyWithCapture(disableAllStreamingHotkeyId, disableName);
    }

    obs_log(LOG_DEBUG, "%s: Streaming hotkeys %s", qUtf8Printable(name), desired ? "registered" : "unregistered");
}

// Caller must hold the libobs hotkey mutex.
void BranchOutputFilter::syncStreamingSlotHotkeys(obs_source_t *parent, size_t index, bool desired)
{
    if (index >= MAX_SERVICES) {
        return;
    }

    bool registered = toggleStreamingServiceHotkeyPairIds[index] != OBS_INVALID_HOTKEY_PAIR_ID;
    if (desired == registered) {
        return;
    }

    auto enableName = buildStreamingSlotHotkeyName(HK_ENABLE_STREAMING_SERVICE.base, index);
    auto disableName = buildStreamingSlotHotkeyName(HK_DISABLE_STREAMING_SERVICE.base, index);

    if (desired) {
        toggleStreamingServiceHotkeyPairIds[index] = registerHotkeyPairWithRestore(
            parent, enableName, buildStreamingSlotHotkeyDescription(HK_ENABLE_STREAMING_SERVICE.textKey, index),
            disableName, buildStreamingSlotHotkeyDescription(HK_DISABLE_STREAMING_SERVICE.textKey, index),
            onEnableStreamingServiceHotkeyPressed, onDisableStreamingServiceHotkeyPressed
        );
        if (toggleStreamingServiceHotkeyPairIds[index] == OBS_INVALID_HOTKEY_PAIR_ID) {
            obs_log(LOG_WARNING, "%s: Streaming slot %zu hotkey registration failed", qUtf8Printable(name), index);
            return;
        }
    } else {
        unregisterHotkeyPairWithCapture(toggleStreamingServiceHotkeyPairIds[index], enableName, disableName);
    }

    obs_log(
        LOG_DEBUG, "%s: Streaming slot %zu hotkeys %s", qUtf8Printable(name), index,
        desired ? "registered" : "unregistered"
    );
}

// Caller must hold the libobs hotkey mutex.
void BranchOutputFilter::syncRecordingHotkeys(obs_source_t *parent, bool desired)
{
    bool registered = splitRecordingHotkeyId != OBS_INVALID_HOTKEY_ID;
    if (desired == registered) {
        return;
    }

    auto splitName = buildHotkeyName(HK_SPLIT_RECORDING.base);
    auto pauseName = buildHotkeyName(HK_PAUSE_RECORDING.base);
    auto unpauseName = buildHotkeyName(HK_UNPAUSE_RECORDING.base);
    auto addChapterName = buildHotkeyName(HK_ADD_CHAPTER_TO_RECORDING.base);
    auto enableName = buildHotkeyName(HK_ENABLE_RECORDING.base);
    auto disableName = buildHotkeyName(HK_DISABLE_RECORDING.base);

    if (desired) {
        splitRecordingHotkeyId = registerHotkeyWithRestore(
            parent, splitName, buildHotkeyDescription(HK_SPLIT_RECORDING.textKey), onSplitRecordingFileHotkeyPressed
        );
        if (splitRecordingHotkeyId == OBS_INVALID_HOTKEY_ID) {
            obs_log(LOG_WARNING, "%s: Recording hotkey registration failed", qUtf8Printable(name));
            return;
        }
        togglePauseRecordingHotkeyPairId = registerHotkeyPairWithRestore(
            parent, pauseName, buildHotkeyDescription(HK_PAUSE_RECORDING.textKey), unpauseName,
            buildHotkeyDescription(HK_UNPAUSE_RECORDING.textKey), onPauseRecordingHotkeyPressed,
            onUnpauseRecordingHotkeyPressed
        );
        addChapterToRecordingHotkeyId = registerHotkeyWithRestore(
            parent, addChapterName, buildHotkeyDescription(HK_ADD_CHAPTER_TO_RECORDING.textKey),
            onAddChapterToRecordingFileHotkeyPressed
        );
        toggleRecordingHotkeyPairId = registerHotkeyPairWithRestore(
            parent, enableName, buildHotkeyDescription(HK_ENABLE_RECORDING.textKey), disableName,
            buildHotkeyDescription(HK_DISABLE_RECORDING.textKey), onEnableRecordingHotkeyPressed,
            onDisableRecordingHotkeyPressed
        );
    } else {
        unregisterHotkeyWithCapture(splitRecordingHotkeyId, splitName);
        unregisterHotkeyPairWithCapture(togglePauseRecordingHotkeyPairId, pauseName, unpauseName);
        unregisterHotkeyWithCapture(addChapterToRecordingHotkeyId, addChapterName);
        unregisterHotkeyPairWithCapture(toggleRecordingHotkeyPairId, enableName, disableName);
    }

    obs_log(LOG_DEBUG, "%s: Recording hotkeys %s", qUtf8Printable(name), desired ? "registered" : "unregistered");
}

// Caller must hold the libobs hotkey mutex.
void BranchOutputFilter::syncReplayBufferHotkeys(obs_source_t *parent, bool desired)
{
    bool registered = saveReplayBufferHotkeyId != OBS_INVALID_HOTKEY_ID;
    if (desired == registered) {
        return;
    }

    auto saveName = buildHotkeyName(HK_SAVE_REPLAY_BUFFER.base);
    auto enableName = buildHotkeyName(HK_ENABLE_REPLAY_BUFFER.base);
    auto disableName = buildHotkeyName(HK_DISABLE_REPLAY_BUFFER.base);

    if (desired) {
        saveReplayBufferHotkeyId = registerHotkeyWithRestore(
            parent, saveName, buildHotkeyDescription(HK_SAVE_REPLAY_BUFFER.textKey), onSaveReplayBufferHotkeyPressed
        );
        if (saveReplayBufferHotkeyId == OBS_INVALID_HOTKEY_ID) {
            obs_log(LOG_WARNING, "%s: Replay buffer hotkey registration failed", qUtf8Printable(name));
            return;
        }
        toggleReplayBufferHotkeyPairId = registerHotkeyPairWithRestore(
            parent, enableName, buildHotkeyDescription(HK_ENABLE_REPLAY_BUFFER.textKey), disableName,
            buildHotkeyDescription(HK_DISABLE_REPLAY_BUFFER.textKey), onEnableReplayBufferHotkeyPressed,
            onDisableReplayBufferHotkeyPressed
        );
    } else {
        unregisterHotkeyWithCapture(saveReplayBufferHotkeyId, saveName);
        unregisterHotkeyPairWithCapture(toggleReplayBufferHotkeyPairId, enableName, disableName);
    }

    obs_log(LOG_DEBUG, "%s: Replay buffer hotkeys %s", qUtf8Printable(name), desired ? "registered" : "unregistered");
}

// Caller must hold the libobs hotkey mutex.
// registerAll ignores the settings and treats every group as desired.
void BranchOutputFilter::syncHotkeyGroups(obs_source_t *parent, obs_data_t *settings, bool registerAll)
{
    syncFilterToggleHotkeys(parent, true);

    bool streaming = registerAll || isStreamingGroupEnabled(settings);
    syncStreamingAllHotkeys(parent, streaming);
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        syncStreamingSlotHotkeys(parent, i, registerAll || (streaming && isStreamingEnabled(settings, i)));
    }

    syncRecordingHotkeys(parent, registerAll || isRecordingEnabled(settings));
    syncReplayBufferHotkeys(parent, registerAll || isReplayBufferEnabled(settings));
}

// Caller must hold the libobs hotkey mutex.
void BranchOutputFilter::captureRegisteredHotkeyBindings()
{
    captureHotkeyPairBindings(
        hotkeyBindingsCache, toggleEnableHotkeyPairId, buildHotkeyName(HK_ENABLE_FILTER.base),
        buildHotkeyName(HK_DISABLE_FILTER.base)
    );
    captureHotkeyBindings(
        hotkeyBindingsCache, enableAllStreamingHotkeyId, buildHotkeyName(HK_ENABLE_ALL_STREAMING.base)
    );
    captureHotkeyBindings(
        hotkeyBindingsCache, disableAllStreamingHotkeyId, buildHotkeyName(HK_DISABLE_ALL_STREAMING.base)
    );
    for (size_t i = 0; i < MAX_SERVICES; i++) {
        captureHotkeyPairBindings(
            hotkeyBindingsCache, toggleStreamingServiceHotkeyPairIds[i],
            buildStreamingSlotHotkeyName(HK_ENABLE_STREAMING_SERVICE.base, i),
            buildStreamingSlotHotkeyName(HK_DISABLE_STREAMING_SERVICE.base, i)
        );
    }
    captureHotkeyBindings(hotkeyBindingsCache, splitRecordingHotkeyId, buildHotkeyName(HK_SPLIT_RECORDING.base));
    captureHotkeyPairBindings(
        hotkeyBindingsCache, togglePauseRecordingHotkeyPairId, buildHotkeyName(HK_PAUSE_RECORDING.base),
        buildHotkeyName(HK_UNPAUSE_RECORDING.base)
    );
    captureHotkeyBindings(
        hotkeyBindingsCache, addChapterToRecordingHotkeyId, buildHotkeyName(HK_ADD_CHAPTER_TO_RECORDING.base)
    );
    captureHotkeyPairBindings(
        hotkeyBindingsCache, toggleRecordingHotkeyPairId, buildHotkeyName(HK_ENABLE_RECORDING.base),
        buildHotkeyName(HK_DISABLE_RECORDING.base)
    );
    captureHotkeyBindings(hotkeyBindingsCache, saveReplayBufferHotkeyId, buildHotkeyName(HK_SAVE_REPLAY_BUFFER.base));
    captureHotkeyPairBindings(
        hotkeyBindingsCache, toggleReplayBufferHotkeyPairId, buildHotkeyName(HK_ENABLE_REPLAY_BUFFER.base),
        buildHotkeyName(HK_DISABLE_REPLAY_BUFFER.base)
    );
}

// Caller must hold the libobs hotkey mutex.
// Entries of another filter appear when a filter is duplicated together with its settings.
// An empty binding array and a missing key mean the same thing on restore.
void BranchOutputFilter::pruneHotkeyBindingsCache()
{
    auto suffix = QString(".%1").arg(obs_source_get_uuid(filterSource));
    QStringList staleKeys;

    for (auto item = obs_data_first(hotkeyBindingsCache); item; obs_data_item_next(&item)) {
        auto key = QString(obs_data_item_get_name(item));
        if (!key.endsWith(suffix) || obs_data_item_gettype(item) != OBS_DATA_ARRAY) {
            staleKeys.append(key);
            continue;
        }

        OBSDataArrayAutoRelease bindings = obs_data_item_get_array(item);
        if (obs_data_array_count(bindings) == 0) {
            staleKeys.append(key);
        }
    }

    for (const auto &key : staleKeys) {
        obs_data_erase(hotkeyBindingsCache, qUtf8Printable(key));
    }
}

// OBS replaces the parent source's hotkey_data with the bindings of the hotkeys that are
// registered at save time, so bindings of hotkeys this filter unregisters would be lost.
// Every hotkey call and every cache access runs inside obs_hotkey_update_atomic(): the description
// setters take no lock of their own, and the callers run on several threads.
void BranchOutputFilter::syncHotkeys(obs_data_t *settings)
{
    auto parent = obs_filter_get_parent(filterSource);
    // sourceIsPrivate() enumerates sources and must not run under the hotkey mutex.
    // Registering against a private parent is not allowed: obs_hotkey_pair_register_source()
    // accepts one while obs_hotkey_register_source() rejects it, which would leave a group
    // half registered.
    if (!parent || sourceIsPrivate(parent)) {
        return;
    }

    struct SyncContext {
        BranchOutputFilter *filter;
        obs_source_t *parent;
        obs_data_t *settings;
    };
    SyncContext context{this, parent, settings};

    obs_hotkey_update_atomic(
        [](void *param) {
            auto ctx = static_cast<SyncContext *>(param);
            auto filter = ctx->filter;

            if (filter->hotkeyHarvestPending) {
                filter->syncHotkeyGroups(ctx->parent, ctx->settings, true);
                filter->hotkeyHarvestPending = false;
            }

            filter->syncHotkeyGroups(ctx->parent, ctx->settings, false);
        },
        &context
    );
}

void BranchOutputFilter::unregisterAllHotkeys()
{
    obs_hotkey_update_atomic(
        [](void *param) {
            auto filter = static_cast<BranchOutputFilter *>(param);

            filter->syncFilterToggleHotkeys(nullptr, false);
            filter->syncStreamingAllHotkeys(nullptr, false);
            for (size_t i = 0; i < MAX_SERVICES; i++) {
                filter->syncStreamingSlotHotkeys(nullptr, i, false);
            }
            filter->syncRecordingHotkeys(nullptr, false);
            filter->syncReplayBufferHotkeys(nullptr, false);
        },
        this
    );
}

// Renaming only refreshes the descriptions: unregistering and re-registering would be visible in
// the OBS hotkey settings UI without changing anything the user configured.
void BranchOutputFilter::updateHotkeyDescriptions(const QString &newName)
{
    struct RenameContext {
        BranchOutputFilter *filter;
        const QString &newName;
    };
    RenameContext context{this, newName};

    obs_hotkey_update_atomic(
        [](void *param) {
            auto ctx = static_cast<RenameContext *>(param);
            auto filter = ctx->filter;
            filter->name = ctx->newName;

            auto setSingle = [filter](obs_hotkey_id id, const char *textKey) {
                if (id != OBS_INVALID_HOTKEY_ID) {
                    obs_hotkey_set_description(id, qUtf8Printable(filter->buildHotkeyDescription(textKey)));
                }
            };
            auto setPair = [filter](obs_hotkey_pair_id id, const char *textKey0, const char *textKey1) {
                if (id != OBS_INVALID_HOTKEY_PAIR_ID) {
                    obs_hotkey_pair_set_descriptions(
                        id, qUtf8Printable(filter->buildHotkeyDescription(textKey0)),
                        qUtf8Printable(filter->buildHotkeyDescription(textKey1))
                    );
                }
            };

            setPair(filter->toggleEnableHotkeyPairId, HK_ENABLE_FILTER.textKey, HK_DISABLE_FILTER.textKey);
            setSingle(filter->enableAllStreamingHotkeyId, HK_ENABLE_ALL_STREAMING.textKey);
            setSingle(filter->disableAllStreamingHotkeyId, HK_DISABLE_ALL_STREAMING.textKey);
            for (size_t i = 0; i < MAX_SERVICES; i++) {
                auto id = filter->toggleStreamingServiceHotkeyPairIds[i];
                if (id == OBS_INVALID_HOTKEY_PAIR_ID) {
                    continue;
                }
                auto enableDesc = filter->buildStreamingSlotHotkeyDescription(HK_ENABLE_STREAMING_SERVICE.textKey, i);
                auto disableDesc = filter->buildStreamingSlotHotkeyDescription(HK_DISABLE_STREAMING_SERVICE.textKey, i);
                obs_hotkey_pair_set_descriptions(id, qUtf8Printable(enableDesc), qUtf8Printable(disableDesc));
            }
            setSingle(filter->splitRecordingHotkeyId, HK_SPLIT_RECORDING.textKey);
            setPair(filter->togglePauseRecordingHotkeyPairId, HK_PAUSE_RECORDING.textKey, HK_UNPAUSE_RECORDING.textKey);
            setSingle(filter->addChapterToRecordingHotkeyId, HK_ADD_CHAPTER_TO_RECORDING.textKey);
            setPair(filter->toggleRecordingHotkeyPairId, HK_ENABLE_RECORDING.textKey, HK_DISABLE_RECORDING.textKey);
            setSingle(filter->saveReplayBufferHotkeyId, HK_SAVE_REPLAY_BUFFER.textKey);
            setPair(
                filter->toggleReplayBufferHotkeyPairId, HK_ENABLE_REPLAY_BUFFER.textKey,
                HK_DISABLE_REPLAY_BUFFER.textKey
            );
        },
        &context
    );
}

void BranchOutputFilter::snapshotHotkeyBindings(obs_data_t *settings)
{
    struct SnapshotContext {
        BranchOutputFilter *filter;
        obs_data_t *settings;
    };
    SnapshotContext context{this, settings};

    obs_hotkey_update_atomic(
        [](void *param) {
            auto ctx = static_cast<SnapshotContext *>(param);
            auto filter = ctx->filter;

            filter->captureRegisteredHotkeyBindings();
            filter->pruneHotkeyBindingsCache();

            // obs_data_set_obj() shares the object, so the settings must not alias the cache.
            OBSDataAutoRelease snapshot = obs_data_create();
            obs_data_apply(snapshot, filter->hotkeyBindingsCache);
            // Written even when empty: hotkeyHarvestPending is decided by whether this key exists.
            obs_data_set_obj(ctx->settings, HOTKEY_BINDINGS_KEY, snapshot);
        },
        &context
    );
}

bool BranchOutputFilter::onEnableFilterHotkeyPressed(void *data, obs_hotkey_pair_id, obs_hotkey *, bool pressed)
{
    if (!pressed) {
        return false;
    }

    BranchOutputFilter *filter = static_cast<BranchOutputFilter *>(data);
    if (obs_source_enabled(filter->filterSource)) {
        // Already enabled
        return false;
    }

    obs_source_set_enabled(filter->filterSource, true);
    return true;
}

bool BranchOutputFilter::onDisableFilterHotkeyPressed(void *data, obs_hotkey_pair_id, obs_hotkey *, bool pressed)
{
    if (!pressed) {
        return false;
    }

    BranchOutputFilter *filter = static_cast<BranchOutputFilter *>(data);
    if (!obs_source_enabled(filter->filterSource)) {
        // Already disabled
        return false;
    }

    obs_source_set_enabled(filter->filterSource, false);
    return true;
}
