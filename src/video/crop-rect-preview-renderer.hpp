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

#include <optional>

#include "../utils.hpp"

// CropRectPreviewRenderer: Renders a crop preview rectangle overlay
// on the main mix (not encoded in the branch output).
//
// Threading: show()/hide() are called from the UI thread,
// render()/update() are called from the graphics thread.
// This follows the same pattern as blankingOutputActive (simple POD writes
// from UI thread, reads from graphics thread).
class CropRectPreviewRenderer {
    std::optional<CropRect> crop;
    uint32_t srcWidth;
    uint32_t srcHeight;

public:
    CropRectPreviewRenderer();

    // Show the preview rectangle with the given crop and source dimensions
    void show(const std::optional<CropRect> &cropRect, uint32_t sourceWidth, uint32_t sourceHeight);

    // Hide the preview rectangle
    void hide();

    // Whether the preview is currently visible
    bool isVisible() const;

    // Check if the source resolution has changed from the cached values
    bool resolutionChanged(uint32_t sourceWidth, uint32_t sourceHeight) const;

    // Update the cached resolution and crop rectangle.
    // Call only after resolutionChanged() returns true.
    void updateResolution(uint32_t sourceWidth, uint32_t sourceHeight, const std::optional<CropRect> &newCrop);

    // Update the crop rectangle (e.g. from UI value changes) without changing cached resolution
    void setCrop(const std::optional<CropRect> &cropRect);

    // Render the crop preview rectangle (call from video_render, graphics thread)
    void render();
};
