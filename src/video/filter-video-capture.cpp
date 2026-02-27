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

#include "filter-video-capture.hpp"

#include "plugin-support.h"

//--- Proxy source callbacks ---//

struct ProxySourceContext {
    FilterVideoCapture *owner;
};

static const char *proxy_source_get_name(void *)
{
    return "Branch Output Proxy";
}

static void *proxy_source_create(obs_data_t *settings, obs_source_t *)
{
    auto ctx = new ProxySourceContext();
    ctx->owner = reinterpret_cast<FilterVideoCapture *>(obs_data_get_int(settings, "owner_ptr"));
    return ctx;
}

static void proxy_source_destroy(void *data)
{
    delete static_cast<ProxySourceContext *>(data);
}

static uint32_t proxy_source_get_width(void *data)
{
    auto ctx = static_cast<ProxySourceContext *>(data);
    return (ctx && ctx->owner) ? ctx->owner->getCaptureWidth() : 0;
}

static uint32_t proxy_source_get_height(void *data)
{
    auto ctx = static_cast<ProxySourceContext *>(data);
    return (ctx && ctx->owner) ? ctx->owner->getCaptureHeight() : 0;
}

static void proxy_source_video_render(void *data, gs_effect_t *)
{
    auto ctx = static_cast<ProxySourceContext *>(data);
    if (ctx && ctx->owner) {
        ctx->owner->renderTexture();
    }
}

obs_source_info FilterVideoCapture::createProxySourceInfo()
{
    obs_source_info info = {};
    info.id = PROXY_SOURCE_ID;
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    info.get_name = proxy_source_get_name;
    info.create = proxy_source_create;
    info.destroy = proxy_source_destroy;
    info.get_width = proxy_source_get_width;
    info.get_height = proxy_source_get_height;
    info.video_render = proxy_source_video_render;
    return info;
}

//--- FilterVideoCapture implementation ---//

FilterVideoCapture::FilterVideoCapture(obs_source_t *_filterSource, obs_source_t *_parentSource, uint32_t _width,
                                       uint32_t _height)
    : filterSource(_filterSource),
      parentSource(_parentSource),
      proxySource(nullptr),
      texrender(nullptr),
      triggerTexrender(nullptr),
      captureWidth(_width),
      captureHeight(_height),
      active(false),
      textureReady(false),
      capturedThisFrame(false)
{
    // Create texrender on graphics thread
    obs_enter_graphics();
    texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    triggerTexrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    obs_leave_graphics();

    if (!texrender || !triggerTexrender) {
        obs_log(LOG_ERROR, "FilterVideoCapture: gs_texrender_create failed");
        return;
    }

    // Create private proxy source
    OBSDataAutoRelease settings = obs_data_create();
    obs_data_set_int(settings, "owner_ptr", reinterpret_cast<long long>(this));
    proxySource = obs_source_create_private(PROXY_SOURCE_ID, "BranchOutputProxy", settings);

    if (!proxySource) {
        obs_log(LOG_ERROR, "FilterVideoCapture: Failed to create proxy source");
    }
}

FilterVideoCapture::~FilterVideoCapture()
{
    active.store(false);
    textureReady.store(false);

    // Release proxy source first (stops any rendering references)
    proxySource = nullptr;

    // Destroy GPU resources
    obs_enter_graphics();
    if (texrender) {
        gs_texrender_destroy(texrender);
        texrender = nullptr;
    }
    if (triggerTexrender) {
        gs_texrender_destroy(triggerTexrender);
        triggerTexrender = nullptr;
    }
    obs_leave_graphics();
}

bool FilterVideoCapture::captureFilterInput()
{
    if (!active.load() || !texrender) {
        return false;
    }

    obs_source_t *target = obs_filter_get_target(filterSource);
    if (!target) {
        return false;
    }

    uint32_t cx = obs_source_get_base_width(target);
    uint32_t cy = obs_source_get_base_height(target);

    if (cx == 0 || cy == 0) {
        return false;
    }

    // Detect resolution change — BranchOutputFilter will restart output
    if (cx != captureWidth || cy != captureHeight) {
        return false;
    }

    // Render filter input to texrender
    gs_texrender_reset(texrender);
    if (!gs_texrender_begin(texrender, cx, cy)) {
        return false;
    }

    struct vec4 clear_color;
    vec4_zero(&clear_color);
    gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
    gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

    gs_blend_state_push();
    gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

    obs_source_video_render(target);

    gs_blend_state_pop();
    gs_texrender_end(texrender);

    textureReady.store(true);
    capturedThisFrame.store(true);
    return true;
}

void FilterVideoCapture::drawCapturedTexture()
{
    if (!textureReady.load() || !texrender) {
        return;
    }

    gs_texture_t *tex = gs_texrender_get_texture(texrender);
    if (!tex) {
        return;
    }

    // Draw the captured texrender content to the current render target
    // using OBS default effect (replaces obs_source_skip_video_filter)
    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
    gs_effect_set_texture(image, tex);

    while (gs_effect_loop(effect, "Draw")) {
        gs_draw_sprite(tex, 0, captureWidth, captureHeight);
    }
}

void FilterVideoCapture::renderTexture()
{
    if (!active.load() || !texrender) {
        return;
    }

    // When the scene is not being rendered by the main mix (scene inactive),
    // the filter's video_render callback is never called, so captureFilterInput()
    // is never invoked and the texrender is never updated.  We detect this via
    // capturedThisFrame (reset each frame from video_tick) and trigger-render
    // the parent scene ourselves to drive the filter chain.
    if (!capturedThisFrame.load() && parentSource && triggerTexrender) {
        // Use triggerTexrender as a disposable render target.
        // Rendering the parent scene drives its filter chain, which calls
        // BranchOutputFilter::videoRenderCallback() → captureFilterInput(),
        // updating `texrender` with fresh content.  The output written to
        // triggerTexrender (via drawCapturedTexture()) is discarded.
        gs_texrender_reset(triggerTexrender);
        if (gs_texrender_begin(triggerTexrender, captureWidth, captureHeight)) {
            struct vec4 clear_color;
            vec4_zero(&clear_color);
            gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
            gs_ortho(0.0f, (float)captureWidth, 0.0f, (float)captureHeight, -100.0f, 100.0f);

            gs_blend_state_push();
            gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

            obs_source_video_render(parentSource);

            gs_blend_state_pop();
            gs_texrender_end(triggerTexrender);
        }
    }

    if (!textureReady.load()) {
        return;
    }

    gs_texture_t *tex = gs_texrender_get_texture(texrender);
    if (!tex) {
        return;
    }

    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
    gs_effect_set_texture(image, tex);

    while (gs_effect_loop(effect, "Draw")) {
        gs_draw_sprite(tex, 0, captureWidth, captureHeight);
    }
}
