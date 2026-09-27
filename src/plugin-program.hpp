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

#include <atomic>

#include <QTimer>

#include "branch-output.hpp"

#define PROGRAM_SOURCE_ID "osi_branch_output_program"

// Branch Output fed by the OBS program output, backed by a private context source.
class BranchOutputProgram : public BranchOutput {
    Q_OBJECT

    QTimer *intervalTimer;                // UI thread. Exists only while attached.
    OBSWeakSourceAutoRelease contextWeak; // Weak reference to contextSource
    OBSSourceAutoRelease outputProxy;     // Guarded by outputMutex
    std::atomic<bool> attached;
    std::atomic<bool> suspended;
    std::atomic<int> openProperties; // Live obs_properties_t objects made by getProperties()
    OBSSignal renamedSignal;
    OBSSignal enabledSignal;

    // Input: the program output of OBS
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
    bool hasFilterPipeline() const override;

    // Hotkeys are not registered
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

    void videoTickCallback(float seconds);
    void videoRenderCallback(gs_effect_t *effect);
    void destroyCallback();
    obs_properties_t *getProperties();
    void onPropertiesDestroyed();

    // Valid only for callbacks registered with the toCallbackData() of a BranchOutputProgram
    static BranchOutputProgram *fromProgramCallbackData(void *data)
    {
        return static_cast<BranchOutputProgram *>(fromCallbackData(data));
    }
    static void getProgramDefaults(obs_data_t *defaults);

signals:
    void persistRequested();

public:
    explicit BranchOutputProgram(obs_data_t *settings, obs_source_t *source, QObject *parent = nullptr);

    void attach();                   // UI thread. Idempotent.
    void detach();                   // UI thread. Idempotent.
    void setSuspended(bool suspend); // UI thread.

    // Caller holds a strong reference to source. Null when source is not a main output.
    static BranchOutputProgram *fromSource(obs_source_t *source);
    static obs_source_info createProgramInfo();
};
