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

#include "plugin-support.h"
#include "plugin-main.hpp"

// Callback from filter audio
obs_audio_data *audioFilterCallback(void *param, obs_audio_data *audioData)
{
    auto filter = (BranchOutputFilter *)param;

    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
        auto audioContext = &filter->audios[i];
        if (audioContext->capture && !audioContext->capture->hasSource()) {
            audioContext->capture->pushAudio(audioData);
        }
    }

    return audioData;
}
