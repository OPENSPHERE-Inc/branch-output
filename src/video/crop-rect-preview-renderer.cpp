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

#include <graphics/graphics.h>

#include "crop-rect-preview-renderer.hpp"

#define PREVIEW_RECT_COLOR 0xFF00FF00 // Green (ABGR)

CropRectPreviewRenderer::CropRectPreviewRenderer() : srcWidth(0), srcHeight(0) {}

void CropRectPreviewRenderer::show(
    const std::optional<CropRect> &cropRect, uint32_t sourceWidth, uint32_t sourceHeight
)
{
    crop = cropRect;
    srcWidth = sourceWidth;
    srcHeight = sourceHeight;
}

void CropRectPreviewRenderer::hide()
{
    crop = std::nullopt;
}

bool CropRectPreviewRenderer::isVisible() const
{
    return crop.has_value();
}

bool CropRectPreviewRenderer::resolutionChanged(uint32_t sourceWidth, uint32_t sourceHeight) const
{
    return sourceWidth != srcWidth || sourceHeight != srcHeight;
}

void CropRectPreviewRenderer::updateResolution(
    uint32_t sourceWidth, uint32_t sourceHeight, const std::optional<CropRect> &newCrop
)
{
    srcWidth = sourceWidth;
    srcHeight = sourceHeight;
    crop = newCrop;
}

void CropRectPreviewRenderer::setCrop(const std::optional<CropRect> &cropRect)
{
    crop = cropRect;
}

void CropRectPreviewRenderer::render()
{
    if (!crop) {
        return;
    }

    // Offset by 0.5px inward to ensure edges at boundaries (top=0, left=0) are visible
    float left = (float)crop->left + 0.5f;
    float top = (float)crop->top + 0.5f;
    float right = (float)(crop->left + crop->width) - 0.5f;
    float bottom = (float)(crop->top + crop->height) - 0.5f;

    gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t *colorParam = gs_effect_get_param_by_name(solid, "color");
    gs_effect_set_color(colorParam, PREVIEW_RECT_COLOR);

    while (gs_effect_loop(solid, "Solid")) {
        gs_render_start(true);
        gs_vertex2f(left, top);
        gs_vertex2f(right, top);
        gs_vertex2f(right, bottom);
        gs_vertex2f(left, bottom);
        gs_vertex2f(left, top);
        gs_render_stop(GS_LINESTRIP);
    }
}
