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

#include "main-texture-proxy.hpp"

//--- Proxy source callbacks ---//

struct MainTextureProxyContext {
    CropRect crop;
};

static const char *mainTextureProxyGetName(void *)
{
    return "Branch Output Program Proxy";
}

static void *mainTextureProxyCreate(obs_data_t *settings, obs_source_t *)
{
    auto ctx = new MainTextureProxyContext();
    ctx->crop.left = (uint32_t)obs_data_get_int(settings, "crop_x");
    ctx->crop.top = (uint32_t)obs_data_get_int(settings, "crop_y");
    ctx->crop.width = (uint32_t)obs_data_get_int(settings, "crop_width");
    ctx->crop.height = (uint32_t)obs_data_get_int(settings, "crop_height");
    return ctx;
}

static void mainTextureProxyDestroy(void *data)
{
    delete static_cast<MainTextureProxyContext *>(data);
}

static uint32_t mainTextureProxyGetWidth(void *data)
{
    return static_cast<MainTextureProxyContext *>(data)->crop.width;
}

static uint32_t mainTextureProxyGetHeight(void *data)
{
    return static_cast<MainTextureProxyContext *>(data)->crop.height;
}

static void mainTextureProxyVideoRender(void *data, gs_effect_t *)
{
    auto ctx = static_cast<MainTextureProxyContext *>(data);
    renderMainTexture(ctx->crop.left, ctx->crop.top);
}

static enum gs_color_space mainTextureProxyGetColorSpace(void *, size_t, const enum gs_color_space *)
{
    return getMainCanvasColorSpace();
}

obs_source_info createMainTextureProxySourceInfo()
{
    obs_source_info info = {};
    info.id = MAIN_TEXTURE_PROXY_SOURCE_ID;
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_CAP_DISABLED;
    info.get_name = mainTextureProxyGetName;
    info.create = mainTextureProxyCreate;
    info.destroy = mainTextureProxyDestroy;
    info.get_width = mainTextureProxyGetWidth;
    info.get_height = mainTextureProxyGetHeight;
    info.video_render = mainTextureProxyVideoRender;
    info.video_get_color_space = mainTextureProxyGetColorSpace;
    return info;
}

OBSSourceAutoRelease createMainTextureProxy(const CropRect &crop)
{
    OBSDataAutoRelease settings = obs_data_create();
    obs_data_set_int(settings, "crop_x", crop.left);
    obs_data_set_int(settings, "crop_y", crop.top);
    obs_data_set_int(settings, "crop_width", crop.width);
    obs_data_set_int(settings, "crop_height", crop.height);
    return obs_source_create_private(MAIN_TEXTURE_PROXY_SOURCE_ID, "BranchOutputProgramProxy", settings);
}

//--- Main texture ---//

// Same rule as obs_init_textures() in libobs.
enum gs_color_space getMainCanvasColorSpace()
{
    obs_video_info ovi = {};
    if (!obs_get_video_info(&ovi)) {
        return GS_CS_SRGB;
    }

    if (ovi.colorspace == VIDEO_CS_2100_PQ || ovi.colorspace == VIDEO_CS_2100_HLG) {
        return GS_CS_709_EXTENDED;
    }

    switch (ovi.output_format) {
    case VIDEO_FORMAT_I010:
    case VIDEO_FORMAT_P010:
    case VIDEO_FORMAT_P216:
    case VIDEO_FORMAT_P416:
        return GS_CS_SRGB_16F;
    default:
        return GS_CS_SRGB;
    }
}

// The right and bottom edges are cut by the render target, whose size follows the drawing
// source's get_width / get_height.
void renderMainTexture(uint32_t offsetX, uint32_t offsetY)
{
    if (!obs_get_main_texture()) {
        return;
    }

    gs_matrix_push();
    gs_matrix_translate3f(-(float)offsetX, -(float)offsetY, 0.0f);
    obs_render_main_texture();
    gs_matrix_pop();
}
