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

#define PROXY_SOURCE_ID "osi_branch_output_proxy"

// FilterVideoCapture: Captures filter input via gs_texrender and provides
// a private proxy source for obs_view binding.
//
// The proxy source renders the captured texrender texture directly on the GPU,
// avoiding any CPU roundtrip. When added to an obs_view, the view's video_t*
// is registered in OBS's internal video mix list, allowing GPU encoders
// (NVENC, QSV, AMF, etc.) to work without the obs_encoder_video_tex_active crash.
//
// Flow:
//   1. Filter's video_render → captureFilterInput() → texrender captures filter input
//   2. obs_view renders proxy source → renderTexture() → draws texrender texture
//   3. obs_view's video_t* → encoder → output
class FilterVideoCapture {
    // Filter source reference (non-owning)
    obs_source_t *filterSource;

    // Private proxy source for obs_view binding
    OBSSourceAutoRelease proxySource;

    // GPU texrender for capturing filter input
    gs_texrender_t *texrender;

    // Capture dimensions (filter target resolution)
    uint32_t captureWidth;
    uint32_t captureHeight;

    // State
    std::atomic_bool active;
    std::atomic_bool textureReady;

public:
    explicit FilterVideoCapture(obs_source_t *_filterSource, uint32_t _width, uint32_t _height);
    ~FilterVideoCapture();

    // Called from video_render callback (graphics thread) to capture filter input
    // Returns true if capture succeeded, false if capture was skipped/failed
    bool captureFilterInput();

    // Called from video_render callback to draw captured texture to current render target
    // (replaces obs_source_skip_video_filter for the main output passthrough)
    void drawCapturedTexture();

    // Called from proxy source's video_render (graphics thread) to render the captured texture
    void renderTexture();

    // Get the proxy source for obs_view binding
    inline obs_source_t *getProxySource() const { return proxySource; }

    // Enable/disable capture
    inline void setActive(bool enable) { active.store(enable); }
    inline bool isActive() const { return active.load(); }

    // Get capture dimensions
    inline uint32_t getCaptureWidth() const { return captureWidth; }
    inline uint32_t getCaptureHeight() const { return captureHeight; }

    // Create obs_source_info for the proxy source type (register at module load)
    static obs_source_info createProxySourceInfo();
};
