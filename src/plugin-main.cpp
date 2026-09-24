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
#include <util/config-file.h>
#include <util/deque.h>
#include <util/threading.h>
#include <util/platform.h>
#include <obs.hpp>

#include <atomic>
#include <memory>
#include <utility>

#include <QRegularExpression>
#include <QStringList>

#include "audio/audio-capture.hpp"
#include "video/filter-video-capture.hpp"
#include "plugin-support.h"
#include "plugin-main.hpp"
#include "utils.hpp"

#define FILTER_ID "osi_branch_output"
#define AVAILAVILITY_CHECK_INTERVAL_NS 1000000000ULL
#define TASK_INTERVAL_MS 1000

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// Atomic so addCallback/updateCallback/removeCallback can safely read the
// pointer when dispatching QMetaObject::invokeMethod from worker threads.
//
// FIXME: A worker that loads the pointer before obs_module_unload() runs
// statusDock.exchange(nullptr) can still reach QMetaObject::invokeMethod
// after the dock QObject is destroyed. This also covers workers already
// inside onGetFilterList that continue calling obs_data_* after the module
// is unloaded. Needs an in-flight counter or QPointer-based UI-thread
// marshalling.
static std::atomic<BranchOutputStatusDock *> statusDock{nullptr};

// Set by obs_module_unload() before module-owned state is torn down. Procs
// registered via obs_get_proc_handler() outlive the module (libobs has no
// proc_handler_remove API), so they must early-return when this is true.
static std::atomic<bool> moduleUnloading{false};

// Filter list snapshot read by onGetFilterList without touching the dock QObject.
// Mutex guards the shared_ptr swap; the published QList itself is immutable. Statically
// initialized and never destroyed: the proc handler that reads it outlives obs_module_unload().
static pthread_mutex_t filterListSnapshotMutex = PTHREAD_MUTEX_INITIALIZER;
static std::shared_ptr<const QList<BranchOutputFilterInfo>> filterListSnapshot;

static std::shared_ptr<const QList<BranchOutputFilterInfo>> loadFilterListSnapshot()
{
    pthread_mutex_lock(&filterListSnapshotMutex);
    OBSMutexAutoUnlock locked(&filterListSnapshotMutex);
    return filterListSnapshot;
}

void publishFilterListSnapshot(QList<BranchOutputFilterInfo> snapshot)
{
    auto shared = std::make_shared<const QList<BranchOutputFilterInfo>>(std::move(snapshot));
    pthread_mutex_lock(&filterListSnapshotMutex);
    {
        OBSMutexAutoUnlock locked(&filterListSnapshotMutex);
        filterListSnapshot = std::move(shared);
    }
}

BranchOutputStatusDock *loadStatusDock()
{
    return statusDock.load();
}

pthread_mutex_t pluginMutex;

//--- BranchOutputFilter class ---//

BranchOutputFilter::BranchOutputFilter(obs_data_t *settings, obs_source_t *source, QObject *parent)
    : BranchOutput(settings, source, parent),
      intervalTimer(nullptr),
      blankingOutputActive(false),
      blankingAudioMuted(false),
      useFilterInput(false),
      filterVideoCapture(nullptr),
      cropScene(nullptr),
      hotkeyRegistrationTarget(nullptr)
{
    // DO NOT use obs_filter_get_parent() in this function (It'll return nullptr)
    obs_log(LOG_DEBUG, "%s: BranchOutputFilter creating", qUtf8Printable(name));
    // obs_data_get_last_json() below reads the buffer that this obs_data_get_json() call fills.
    obs_log(LOG_DEBUG, "filter_settings_json=%s", obs_data_get_json(settings));

    if (!strcmp(obs_data_get_last_json(settings), "{}")) {
        // Maybe initial creation
        loadProfile(settings);
        loadRecently(settings);

        // Assit initial settings
        obs_data_set_bool(settings, "use_profile_recording_path", true);
    }

    hotkeyBindingsCache = obs_data_create();
    OBSDataAutoRelease savedHotkeyBindings = obs_data_get_obj(settings, HOTKEY_BINDINGS_KEY);
    if (savedHotkeyBindings) {
        obs_data_apply(hotkeyBindingsCache, savedHotkeyBindings);
    }
    hotkeyHarvestPending = !obs_data_has_user_value(settings, HOTKEY_BINDINGS_KEY);

    // Migrate audio_source schema
    auto audioSource = obs_data_get_string(settings, "audio_source");
    if (!strncmp(audioSource, "master_track_", strlen("master_track_"))) {
        // Separate out track number
        size_t trackNo = 0;
        sscanf(audioSource, "master_track_%zu", &trackNo);

        obs_data_set_string(settings, "audio_source", "master_track");
        obs_data_set_int(settings, "audio_track", trackNo);
    }

    // Migrate streaming_enabled (for pre-existing filters without this key)
    if (!obs_data_has_user_value(settings, "streaming_enabled")) {
        bool hasAnyServer = countEnabledStreamings(settings) > 0;
        obs_data_set_bool(settings, "streaming_enabled", hasAnyServer);
    }

    // FIXME: obs_save_source() / obs_source_duplicate() persist the live settings, so edits never
    // applied in the properties dialog arrive here and get published as applied. Store the
    // snapshot under a dedicated settings key in AppliedSettings::replace() and prefer it here; the
    // live object is shared with the properties view, so saveCallback() must not rewrite its keys.
    appliedSettings.replace(settings);

    // Fiter activate immediately when "server" or "stream_recording" or "replay_buffer" is exists.
    initialized = isStreamingGroupEnabled(settings) || obs_data_get_bool(settings, "stream_recording") ||
                  obs_data_get_bool(settings, "replay_buffer");

    // Register proc handlers for external script access. These handlers
    // live on contextSource and die with it. A well-behaved script acquires a
    // strong ref via obs_get_source_by_uuid() before calling; the weak-ref CAS
    // refuses to bump a count of 0, so the filter cannot be destroyed mid-call.
    // A misbehaving script that caches a proc_handler_t * past source release
    // and calls it without holding the strong ref triggers a UAF that the
    // plugin cannot prevent — that is a script-side bug.
    //
    // FIXME: libobs has no proc_handler_remove(). If it gains one, pair
    // unregistration with ~BranchOutputFilter().
    proc_handler_t *ph = obs_source_get_proc_handler(contextSource);
    proc_handler_add(
        ph, "void override_replay_buffer_filename_format(in string format)", onOverrideReplayBufferFilenameFormat,
        toCallbackData()
    );
    proc_handler_add(
        ph, "void override_recording_filename_format(in string format)", onOverrideRecordingFilenameFormat,
        toCallbackData()
    );

    obs_log(LOG_INFO, "%s: BranchOutputFilter created", qUtf8Printable(name));
}

bool BranchOutputFilter::validateInput()
{
    // Retrieve filter source
    auto parent = obs_filter_get_parent(contextSource);
    if (!parent) {
        obs_log(LOG_ERROR, "%s: Filter source not found", qUtf8Printable(name));
        return false;
    }

    // Ignore private sources
    if (sourceIsPrivate(parent)) {
        obs_log(LOG_ERROR, "%s: Ignore private source", qUtf8Printable(name));
        return false;
    }

    return true;
}

bool BranchOutputFilter::isInputAvailable() const
{
    auto parent = obs_filter_get_parent(contextSource);
    return parent && sourceInFrontend(parent);
}

QString BranchOutputFilter::getInputName() const
{
    return obs_source_get_name(obs_filter_get_parent(contextSource));
}

// FIXME: releaseInputShowing() re-resolves the parent, which libobs has already cleared by the
// time destroyCallback() stops the outputs, so the reference taken here leaks until the parent is
// destroyed. Keep a weak reference to the parent at acquire time and release against it.
void BranchOutputFilter::acquireInputShowing()
{
    auto parent = obs_filter_get_parent(contextSource);
    if (parent) {
        obs_source_inc_showing(parent);
    }
}

void BranchOutputFilter::releaseInputShowing()
{
    auto parent = obs_filter_get_parent(contextSource);
    if (parent) {
        obs_source_dec_showing(parent);
    }
}

void BranchOutputFilter::selectVideoInputMode(obs_data_t *settings)
{
    auto videoSourceType = obs_data_get_string(settings, "video_source_type");
    useFilterInput = videoSourceType && !strcmp(videoSourceType, "filter_input");
}

// Caller must hold outputMutex.
// On failure, everything created here has already been cleaned up.
bool BranchOutputFilter::setupVideoInput(obs_data_t *, obs_video_info *ovi, const CropRect &crop)
{
    auto parent = obs_filter_get_parent(contextSource);
    if (!parent) {
        obs_log(LOG_ERROR, "%s: Filter source not found", qUtf8Printable(name));
        return false;
    }

    if (useFilterInput) {
        // Filter input mode: capture via texrender + proxy source + obs_view.
        // The proxy source renders the captured texrender texture on the GPU.
        // obs_view creates a video_t* registered in OBS's mix list, allowing
        // GPU encoders (NVENC, QSV, AMF, etc.) to work directly.
        filterVideoCapture = new FilterVideoCapture(contextSource, parent, width, height);
        if (!filterVideoCapture->getProxySource()) {
            obs_log(LOG_ERROR, "%s: Filter video capture creation failed", qUtf8Printable(name));
            delete filterVideoCapture;
            filterVideoCapture = nullptr;
            return false;
        }

        if (crop.width != width || crop.height != height) {
            filterVideoCapture->setCrop(crop);
        }

        view = obs_view_create();
        videoOutputOwned = true;
        obs_view_set_source(view, 0, filterVideoCapture->getProxySource());

        videoOutput = obs_view_add2(view, ovi);
        if (!videoOutput) {
            obs_log(LOG_ERROR, "%s: Video output association failed", qUtf8Printable(name));
            // releaseInfrastructureIfIdle() safely handles partially-initialized state:
            // - OBS RAII wrappers accept nullptr assignment
            // - AudioCapture/filterVideoCapture pointers are nullptr-checked before delete
            // - FilterVideoCapture::setActive(false) on a never-activated instance is safe
            //   (simply stores false to atomic bool)
            releaseInfrastructureIfIdle();
            return false;
        }
        filterVideoCapture->setActive(true);
    } else {
        // Source output mode (default): use obs_view for the parent source
        view = obs_view_create();
        videoOutputOwned = true;

        if (crop.width != width || crop.height != height) {
            cropScene = obs_scene_create_private("branch_output_crop");
            obs_sceneitem_t *item = obs_scene_add(cropScene, parent);

            struct obs_sceneitem_crop itemCrop;
            itemCrop.left = (int)crop.left;
            itemCrop.top = (int)crop.top;
            itemCrop.right = (int)(width - crop.left - crop.width);
            itemCrop.bottom = (int)(height - crop.top - crop.height);
            obs_sceneitem_set_crop(item, &itemCrop);

            obs_view_set_source(view, 0, obs_scene_get_source(cropScene));
        } else {
            obs_view_set_source(view, 0, parent);
        }

        videoOutput = obs_view_add2(view, ovi);
        if (!videoOutput) {
            obs_log(LOG_ERROR, "%s: Video output association failed", qUtf8Printable(name));
            releaseInfrastructureIfIdle();
            return false;
        }
    }

    return true;
}

// Caller must hold outputMutex.
// On failure, everything created here has already been cleaned up.
bool BranchOutputFilter::setupDefaultAudio(const obs_audio_info &ai)
{
    // Filter pipeline's audio
    obs_log(LOG_INFO, "%s: Use filter audio for track 1", qUtf8Printable(name));
    auto audioContext = &audios[0];
    audioContext->capture = new FilterAudioCapture(qUtf8Printable(name), ai.samples_per_sec, ai.speakers, this);
    audioContext->audio = audioContext->capture->getAudio();
    audioContext->streaming = true;
    audioContext->recording = true;
    audioContext->name = audioContext->capture->getName();

    if (!audioContext->audio) {
        obs_log(LOG_ERROR, "%s: Audio creation failed", qUtf8Printable(name));
        delete audioContext->capture;
        audioContext->capture = nullptr;
        releaseInfrastructureIfIdle();
        return false;
    }

    return true;
}

// Caller must hold outputMutex. Safe to call on a partially built video input.
void BranchOutputFilter::teardownVideoInput()
{
    if (filterVideoCapture) {
        filterVideoCapture->setActive(false);
        delete filterVideoCapture;
        filterVideoCapture = nullptr;
    }

    cropScene = nullptr;

    useFilterInput = false;
    blankingOutputActive = false;
    blankingAudioMuted = false;
}

void BranchOutputFilter::setBlankingActive(bool active, bool muteAudio, obs_source_t *parent)
{
    if (!parent) {
        parent = obs_filter_get_parent(contextSource);
    }

    if (!infrastructureReady) {
        blankingOutputActive = false;
        if (blankingAudioMuted) {
            setAudioCapturesActive(true);
            blankingAudioMuted = false;
        }
        return;
    }

    if (active) {
        if (!blankingOutputActive) {
            obs_view_set_source(view, 0, nullptr);
            if (muteAudio) {
                setAudioCapturesActive(false);
                blankingAudioMuted = true;
            } else {
                blankingAudioMuted = false;
            }
            blankingOutputActive = true;
            obs_log(LOG_INFO, "%s: Output blanked because source is not visible", qUtf8Printable(name));
        } else {
            if (muteAudio && !blankingAudioMuted) {
                setAudioCapturesActive(false);
                blankingAudioMuted = true;
            } else if (!muteAudio && blankingAudioMuted) {
                setAudioCapturesActive(true);
                blankingAudioMuted = false;
            }
        }
    } else {
        if (blankingOutputActive) {
            if (useFilterInput && filterVideoCapture) {
                obs_view_set_source(view, 0, filterVideoCapture->getProxySource());
            } else if (cropScene) {
                obs_view_set_source(view, 0, obs_scene_get_source(cropScene));
            } else if (parent) {
                obs_view_set_source(view, 0, parent);
            }
            blankingOutputActive = false;
            obs_log(LOG_INFO, "%s: Output resumed because source became visible", qUtf8Printable(name));
        }
        if (blankingAudioMuted) {
            setAudioCapturesActive(true);
            blankingAudioMuted = false;
        }
    }
}

// Returns true while the input is hidden from Program and the output is blanked.
bool BranchOutputFilter::evaluateBlanking(obs_data_t *settings)
{
    bool blankWhenHidden = obs_data_get_bool(settings, "blank_when_not_visible");
    if (!blankWhenHidden) {
        return false;
    }

    bool muteWhenHidden = obs_data_get_bool(settings, "mute_audio_when_blank");
    auto parent = obs_filter_get_parent(contextSource);
    bool visibleInProgram = sourceVisibleInProgram(parent);

    pthread_mutex_lock(&outputMutex);
    {
        OBSMutexAutoUnlock outputLocked(&outputMutex);
        setBlankingActive(!visibleInProgram, muteWhenHidden, parent);
    }

    return !visibleInProgram;
}

void BranchOutputFilter::getSourceResolution(uint32_t &outWidth, uint32_t &outHeight)
{
    if (useFilterInput) {
        obs_source_t *target = obs_filter_get_target(contextSource);
        if (target) {
            outWidth = obs_source_get_base_width(target);
            outHeight = obs_source_get_base_height(target);
        } else {
            outWidth = 0;
            outHeight = 0;
        }
    } else {
        obs_source_t *parent = obs_filter_get_parent(contextSource);
        outWidth = obs_source_get_width(parent);
        outHeight = obs_source_get_height(parent);
    }
    // Round up to a multiple of 2
    outWidth += (outWidth & 1);
    outHeight += (outHeight & 1);
}

void BranchOutputFilter::addCallback(obs_source_t *source)
{
    // Do not start timer for private sources
    // Do not register private sources to status dock
    if (sourceIsPrivate(source)) {
        obs_log(
            LOG_DEBUG, "%s: Ignore adding to private source '%s'", qUtf8Printable(name), obs_source_get_name(source)
        );
        return;
    }

    obs_log(LOG_DEBUG, "%s: Filter adding to '%s'", qUtf8Printable(name), obs_source_get_name(source));

    // Start interval timer here
    intervalTimer = new QTimer(this);
    intervalTimer->setInterval(TASK_INTERVAL_MS);
    intervalTimer->start();
    connect(intervalTimer, SIGNAL(timeout()), this, SLOT(onIntervalTimerTimeout()));

    // Register to status dock
    if (auto *dock = statusDock.load()) {
        // Show in status dock (Thread-safe way)
        QMetaObject::invokeMethod(dock, "addFilter", Qt::QueuedConnection, Q_ARG(BranchOutputFilter *, this));
    }

    OBSDataAutoRelease settings = obs_source_get_settings(contextSource);
    syncHotkeys(settings);
    // Track filter renames for name and hotkey settings
    filterRenamedSignal.Connect(
        obs_source_get_signal_handler(contextSource), "rename",
        [](void *_data, calldata_t *cd) {
            auto _filter = fromFilterCallbackData(_data);
            _filter->updateHotkeyDescriptions(calldata_string(cd, "new_name"));
        },
        toCallbackData()
    );

    obs_log(LOG_INFO, "%s: Filter added to '%s'", qUtf8Printable(name), obs_source_get_name(source));
}

void BranchOutputFilter::updateCallback(obs_data_t *settings)
{
    // Restarting here could interrupt a connection attempt, so only the snapshot advances;
    // the interval timer restarts the output once it sees a newer snapshot.
    // FIXME: the deferred update runs this on the graphics thread while the properties view edits
    // the same live settings on the UI thread; obs_data has no lock, so replace() and the JSON save
    // below walk the object unsynchronized. Confine live-settings traversal to the UI thread.
    appliedSettings.replace(settings);

    auto source = obs_filter_get_parent(contextSource);

    // Do not save settings for private sources
    if (sourceIsPrivate(source)) {
        obs_log(
            LOG_DEBUG, "%s: Ignore updating in private source '%s'", qUtf8Printable(name), obs_source_get_name(source)
        );
        return;
    }

    obs_log(LOG_DEBUG, "%s: Filter updating", qUtf8Printable(name));

    // Save settings as default
    OBSString config_dir_path = obs_module_get_config_path(obs_current_module(), "");
    os_mkdirs(config_dir_path);

    // Serializing swaps the JSON buffer of the serialized object without a lock, and this callback
    // runs on both the graphics thread and the UI thread for the same settings object.
    OBSDataAutoRelease snapshot = obs_data_create();
    obs_data_apply(snapshot, settings);

    OBSString path = obs_module_get_config_path(obs_current_module(), RECENTLY_SETTINGS_JSON_NAME);
    obs_data_save_json_safe(snapshot, path, "tmp", "bak");

    // The registered hotkey set depends on which outputs are enabled in settings.
    syncHotkeys(settings);

    // Update status dock
    if (auto *dock = statusDock.load()) {
        // Show in status dock (Thread-safe way)
        QMetaObject::invokeMethod(dock, "addFilter", Qt::QueuedConnection, Q_ARG(BranchOutputFilter *, this));
    }

    obs_log(LOG_INFO, "%s: Filter updated", qUtf8Printable(name));
}

void BranchOutputFilter::videoTickCallback(float)
{
    // Reset capture flag at the start of each frame so that renderTexture()
    // can detect whether captureFilterInput() was already called by the
    // normal rendering path (scene active).  video_tick runs before
    // output_frames in the graphics thread loop, guaranteeing correct ordering.
    if (useFilterInput && filterVideoCapture) {
        filterVideoCapture->resetCapturedFlag();
    }

    // Update crop preview when source resolution changes
    if (cropPreview.isVisible()) {
        uint32_t curW, curH;
        getSourceResolution(curW, curH);
        if (curW > 0 && curH > 0 && cropPreview.resolutionChanged(curW, curH)) {
            OBSDataAutoRelease settings = obs_source_get_settings(contextSource);
            cropPreview.updateResolution(curW, curH, calculateCrop(curW, curH, settings));
        }
    }
}

void BranchOutputFilter::videoRenderCallback(gs_effect_t *)
{
    if (useFilterInput && filterVideoCapture) {
        // Optimized filter input mode:
        // 1. Capture upstream rendering to texrender (one render of the source tree)
        // 2. Draw captured texture to current render target (main output passthrough)
        //    This replaces obs_source_skip_video_filter to avoid rendering the
        //    source tree a second time.
        if (filterVideoCapture->captureFilterInput()) {
            filterVideoCapture->drawCapturedTexture();
        } else {
            // Fallback: capture failed, pass through normally
            obs_source_skip_video_filter(contextSource);
        }
    } else {
        // Source output mode: pass through the filter chain as usual
        obs_source_skip_video_filter(contextSource);
    }

    // Draw crop preview rectangle overlay (main mix only, not encoded in branch output)
    cropPreview.render();
}

// This method possibly called in different thread from UI thread
void BranchOutputFilter::removeCallback()
{
    obs_log(LOG_DEBUG, "%s: Filter removing", qUtf8Printable(name));

    if (intervalTimer) {
        // Stop interval timer (In proper thread)
        QMetaObject::invokeMethod(intervalTimer, "stop", Qt::QueuedConnection);
    }

    // Do not call stopOutput() here as this will cause a crash.

    if (auto *dock = statusDock.load()) {
        // Unregister from output status dock (In proper thread)
        QMetaObject::invokeMethod(dock, "removeFilter", Qt::QueuedConnection, Q_ARG(BranchOutputFilter *, this));
    }

    // Unregister hotkeys
    unregisterAllHotkeys();

    obs_log(LOG_INFO, "%s: Filter removed", qUtf8Printable(name));
}

void BranchOutputFilter::destroyCallback()
{
    obs_log(LOG_DEBUG, "%s: BranchOutputFilter destroying", qUtf8Printable(name));

    // FIXME: Hotkeys are unregistered only in removeCallback(); a sync pass racing the removal
    // re-registers them and they outlive this instance. Serialize hotkey sync with filter removal.

    // Release all handles
    stopOutput();

    // Delete self in proper thread
    deleteLater();

    obs_log(LOG_INFO, "%s: BranchOutputFilter destroyed", qUtf8Printable(name));
}

// Callback from filter audio
obs_audio_data *BranchOutputFilter::audioFilterCallback(void *param, obs_audio_data *audioData)
{
    auto filter = fromFilterCallbackData(param);

    pthread_mutex_lock(&filter->audioMutex);
    {
        OBSMutexAutoUnlock locked(&filter->audioMutex);

        for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
            auto audioContext = &filter->audios[i];
            if (audioContext->capture && !audioContext->capture->hasSource()) {
                audioContext->capture->pushAudio(audioData);
            }
        }
    }

    return audioData;
}

obs_source_info BranchOutputFilter::createFilterInfo()
{
    obs_source_info info = {0};

    info.id = FILTER_ID;
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_VIDEO; // Didn't work OBS_SOURCE_DO_NOT_DUPLICATE for filter

    info.get_name = [](void *) {
        return "Branch Output";
    };

    info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
        auto filter = new BranchOutputFilter(settings, source);
        return filter->toCallbackData();
    };
    info.filter_add = [](void *data, obs_source_t *source) {
        auto filter = fromFilterCallbackData(data);
        filter->addCallback(source);
    };
    info.update = [](void *data, obs_data_t *settings) {
        auto filter = fromFilterCallbackData(data);
        filter->updateCallback(settings);
    };
    info.video_render = [](void *data, gs_effect_t *effect) {
        auto filter = fromFilterCallbackData(data);
        filter->videoRenderCallback(effect);
    };
    info.video_tick = [](void *data, float seconds) {
        auto filter = fromFilterCallbackData(data);
        filter->videoTickCallback(seconds);
    };
    info.filter_remove = [](void *data, obs_source_t *) {
        auto filter = fromFilterCallbackData(data);
        filter->removeCallback();
    };
    info.destroy = [](void *data) {
        auto filter = fromFilterCallbackData(data);
        filter->destroyCallback();
    };

    info.save = [](void *data, obs_data_t *settings) {
        auto filter = fromFilterCallbackData(data);
        filter->saveCallback(settings);
    };
    info.get_properties = [](void *data) -> obs_properties_t * {
        auto filter = fromFilterCallbackData(data);
        return filter->getProperties();
    };
    info.get_defaults = BranchOutputFilter::getDefaults;

    info.filter_audio = BranchOutputFilter::audioFilterCallback;

    return info;
}

//--- OBS Plugin Callbacks ---//

obs_source_info filterInfo;
obs_source_info proxySourceInfo;

bool obs_module_load()
{
    filterInfo = BranchOutputFilter::createFilterInfo();
    obs_register_source(&filterInfo);

    proxySourceInfo = FilterVideoCapture::createProxySourceInfo();
    obs_register_source(&proxySourceInfo);

    pthread_mutex_init_recursive(&pluginMutex);

    obs_log(LOG_INFO, "Plugin loaded successfully (version %s)", PLUGIN_VERSION);
    return true;
}

static void onGetFilterList(void *, calldata_t *cd)
{
    if (moduleUnloading.load(std::memory_order_acquire)) {
        calldata_set_string(cd, "json", "{\"filters\":[]}");
        return;
    }

    OBSDataAutoRelease wrapper = obs_data_create();
    OBSDataArrayAutoRelease array = obs_data_array_create();

    // Read the UI-thread-published snapshot. The shared_ptr pins the QList
    // for the duration of this call, so the worker never touches the dock
    // QObject and cannot race obs_module_unload().
    if (auto snapshot = loadFilterListSnapshot()) {
        for (const auto &info : *snapshot) {
            OBSDataAutoRelease entry = obs_data_create();
            obs_data_set_string(entry, "source_name", qUtf8Printable(info.sourceName));
            obs_data_set_string(entry, "source_uuid", qUtf8Printable(info.sourceUuid));
            obs_data_set_string(entry, "filter_name", qUtf8Printable(info.filterName));
            obs_data_set_string(entry, "filter_uuid", qUtf8Printable(info.filterUuid));
            obs_data_array_push_back(array, entry);
        }
    }

    obs_data_set_array(wrapper, "filters", array);

    // calldata_set_string copies the buffer (via bstrdup); json pointer
    // lifetime need not extend beyond this call.
    // obs_data_get_json() may return NULL on allocation failure; fall back
    // to a schema-consistent empty payload so clients can rely on the
    // "filters" array being present regardless of error mode.
    const char *json = obs_data_get_json(wrapper);
    calldata_set_string(cd, "json", json ? json : "{\"filters\":[]}");
}

void obs_module_post_load()
{
    qRegisterMetaType<BranchOutputFilter *>();

    // Publish an empty snapshot so onGetFilterList sees a defined empty state
    // before the first addFilter republish, distinguishing pre-init from
    // genuinely-no-filters.
    publishFilterListSnapshot(QList<BranchOutputFilterInfo>{});

    statusDock.store(BranchOutputFilter::createOutputStatusDock());

    // Register global proc handler for script access (obs-websocket style).
    // data=nullptr: onGetFilterList reads the filter-list snapshot directly.
    //
    // FIXME: libobs has no removal API for the global proc table, so the
    // function pointer outlives obs_module_unload(). The moduleUnloading
    // atomic gates the proc body; callers should still avoid dispatching
    // after unload as a defence-in-depth measure.
    proc_handler_t *ph = obs_get_proc_handler();
    proc_handler_add(ph, "void osi_branch_output_get_filter_list(out string json)", onGetFilterList, nullptr);
}

void obs_module_unload()
{
    // Publish the unload flag before tearing down module state so onGetFilterList
    // (which outlives the module via the global proc handler) early-returns.
    moduleUnloading.store(true, std::memory_order_release);

    // Release the snapshot before destroying the dock so no dangling
    // references to FilterInfo objects remain once the dock rows are freed.
    pthread_mutex_lock(&filterListSnapshotMutex);
    {
        OBSMutexAutoUnlock locked(&filterListSnapshotMutex);
        filterListSnapshot.reset();
    }

    if (statusDock.exchange(nullptr) != nullptr) {
        obs_frontend_remove_dock("BranchOutputStatusDock");
    }

    pthread_mutex_destroy(&pluginMutex);

    obs_log(LOG_INFO, "Plugin unloaded");
}
