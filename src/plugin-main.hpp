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

//#define NO_AUDIO

#include <obs-module.h>
#include <obs.hpp>
#include <util/deque.h>
#include <util/threading.h>

#include <atomic>

#include <QObject>
#include <QSet>

#include "UI/output-status-dock.hpp"
#include "audio/audio-capture.hpp"
#include "video/filter-video-capture.hpp"
#include "video/crop-rect-preview-renderer.hpp"
#include "utils.hpp"
#include "branch-output.hpp"

class BranchOutputFilter : public BranchOutput {
    Q_OBJECT

    QTimer *intervalTimer;
    bool blankingOutputActive;
    bool blankingAudioMuted;

    // Crop context
    OBSSceneAutoRelease cropScene; // Source output mode crop scene
    CropRectPreviewRenderer cropPreview;

    // Filter input mode flag
    bool useFilterInput;

    // Filter input video capture (captures filter input and provides proxy source for obs_view)
    FilterVideoCapture *filterVideoCapture;

    // Source the current sync pass registers its hotkeys against (not owned). Non-null only
    // between beginHotkeyRegistration() and endHotkeyRegistration(), under the hotkey mutex.
    obs_source_t *hotkeyRegistrationTarget;

    OBSSignal filterRenamedSignal;
    OBSSignal parentRenamedSignal;

    // Input: the parent source this filter is attached to
    bool validateInput() override;
    bool isInputAvailable() const override;
    QString getInputName() const override;
    QString getInputUuid() const override;
    void acquireInputShowing() override;
    void releaseInputShowing() override;
    void getSourceResolution(uint32_t &outWidth, uint32_t &outHeight) override;

    // Infrastructure parts that depend on the input
    void selectVideoInputMode(obs_data_t *settings) override;
    bool setupVideoInput(obs_data_t *settings, obs_video_info *ovi, const CropRect &crop) override;
    void teardownVideoInput() override;
    bool setupDefaultAudio(const obs_audio_info &ai) override;
    bool evaluateBlanking(obs_data_t *settings) override;
    BlankingState getBlankingState() const override;

    // Hotkey registration against the parent source
    bool canRegisterHotkeys() override;
    bool beginHotkeyRegistration() override;
    void endHotkeyRegistration() override;
    obs_hotkey_id registerHotkey(const QString &fullName, const QString &description, obs_hotkey_func func) override;
    obs_hotkey_pair_id registerHotkeyPair(
        const QString &fullName0, const QString &description0, const QString &fullName1, const QString &description1,
        obs_hotkey_active_func func0, obs_hotkey_active_func func1
    ) override;

    void openSettings() override;
    void updateCallback(obs_data_t *settings) override;

    void setBlankingActive(bool active, bool muteAudio, obs_source_t *parent);

    // Implemented in plugin-ui.cpp
    void addVideoEncoderGroup(obs_properties_t *props);

    void addCallback(obs_source_t *source);
    void videoTickCallback(float seconds);
    void videoRenderCallback(gs_effect_t *effect);
    void destroyCallback();
    obs_properties_t *getProperties();

    static obs_audio_data *audioFilterCallback(void *param, obs_audio_data *audioData);

    // Valid only for callbacks registered with the toCallbackData() of a BranchOutputFilter
    static BranchOutputFilter *fromFilterCallbackData(void *data)
    {
        return static_cast<BranchOutputFilter *>(fromCallbackData(data));
    }

private slots:
    void removeCallback();

public:
    explicit BranchOutputFilter(obs_data_t *settings, obs_source_t *source, QObject *parent = nullptr);

    static obs_source_info createFilterInfo();
};
