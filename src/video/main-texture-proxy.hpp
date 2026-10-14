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

#include "../utils.hpp"

#define MAIN_TEXTURE_PROXY_SOURCE_ID "osi_branch_output_program_proxy"

// Rendering color space of the main canvas, by the rule libobs uses for its main textures.
enum gs_color_space getMainCanvasColorSpace();
// Draws the main texture with (offsetX, offsetY) at the origin. Graphics thread, inside a render pass.
void renderMainTexture(uint32_t offsetX, uint32_t offsetY);
obs_source_info createMainTextureProxySourceInfo();
// Private source drawing `crop` of the main texture, sized crop.width x crop.height. Null on failure.
OBSSourceAutoRelease createMainTextureProxy(const CropRect &crop);
