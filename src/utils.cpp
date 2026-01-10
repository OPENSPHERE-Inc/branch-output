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
#include <util/platform.h>
#include <obs.hpp>

#include <QWidget>

#include "utils.hpp"

// Origin: https://github.com/obsproject/obs-studio/blob/06642fdee48477ab85f89ff670f105affe402df7/UI/obs-app.cpp#L1871
QString getFormatExt(const char *container)
{
    QString ext = container;
    if (ext == "fragmented_mp4")
        ext = "mp4";
    if (ext == "hybrid_mp4")
        ext = "mp4";
    else if (ext == "fragmented_mov")
        ext = "mov";
    else if (ext == "hls")
        ext = "m3u8";
    else if (ext == "mpegts")
        ext = "ts";

    return ext;
}

// Origin: https://github.com/obsproject/obs-studio/blob/06642fdee48477ab85f89ff670f105affe402df7/UI/obs-app.cpp#L1771
QString generateSpecifiedFilename(const char *extension, bool noSpace, const char *format)
{
    OBSString filename = os_generate_formatted_filename(extension, !noSpace, format);
    return QString(filename);
}

// Origin: https://github.com/obsproject/obs-studio/blob/06642fdee48477ab85f89ff670f105affe402df7/UI/obs-app.cpp#L1809
void ensureDirectoryExists(QString path)
{
    path.replace('\\', '/');

    // Remove file part (also remove trailing slash)
    auto last = path.lastIndexOf('/');
    if (last < 0) {
        return;
    }

    QString directory = path.left(last);
    os_mkdirs(qUtf8Printable(directory));
}

// Origin: https://github.com/obsproject/obs-studio/blob/06642fdee48477ab85f89ff670f105affe402df7/UI/obs-app.cpp#L1779
void findBestFilename(QString &strPath, bool noSpace)
{
    int num = 2;

    if (!os_file_exists(qUtf8Printable(strPath))) {
        return;
    }

    size_t dotPos = strPath.lastIndexOf('.');
    for (;;) {
        QString testPath = strPath;
        QString numStr;

        if (noSpace) {
            numStr = QString("_%1").arg(num++);
        } else {
            numStr = QString(" (%1)").arg(num++);
        }

        testPath.insert(dotPos, numStr);

        if (!os_file_exists(qUtf8Printable(testPath))) {
            strPath = testPath;
            break;
        }
    }
}

// Origin: https://github.com/obsproject/obs-studio/blob/06642fdee48477ab85f89ff670f105affe402df7/UI/obs-app.cpp#L1888
QString getOutputFilename(const char *path, const char *container, bool noSpace, bool overwrite, const char *format)
{
    os_dir_t *dir = path && path[0] ? os_opendir(path) : nullptr;

    if (!dir) {
        return "";
    }

    os_closedir(dir);

    QString strPath;
    strPath += path;

    QChar lastChar = strPath.back();
    if (lastChar != '/' && lastChar != '\\')
        strPath += "/";

    QString ext = getFormatExt(container);
    strPath += generateSpecifiedFilename(qUtf8Printable(ext), noSpace, format);
    ensureDirectoryExists(strPath);
    if (!overwrite) {
        findBestFilename(strPath, noSpace);
    }

    return strPath;
}

// Recursively check whether a source is visible inside a scene (including nested scenes and groups)
static bool sceneHasVisibleSource(obs_scene_t *scene, obs_source_t *target)
{
    if (!scene || !target) {
        return false;
    }

    struct FindContext {
        obs_source_t *target;
        bool found;
    } context = {target, false};

    obs_scene_enum_items(
        scene,
        [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
            auto ctx = static_cast<FindContext *>(param);
            if (ctx->found) {
                return false;
            }

            if (!obs_sceneitem_visible(item)) {
                return true;
            }

            obs_source_t *itemSource = obs_sceneitem_get_source(item);
            if (itemSource == ctx->target) {
                ctx->found = true;
                return false;
            }

            if (obs_sceneitem_is_group(item)) {
                obs_scene_t *groupScene = obs_sceneitem_group_get_scene(item);
                if (sceneHasVisibleSource(groupScene, ctx->target)) {
                    ctx->found = true;
                    return false;
                }
            }

            if (obs_source_get_type(itemSource) == OBS_SOURCE_TYPE_SCENE) {
                obs_scene_t *subScene = obs_scene_from_source(itemSource);
                if (sceneHasVisibleSource(subScene, ctx->target)) {
                    ctx->found = true;
                    return false;
                }
            }

            return true;
        },
        &context
    );

    return context.found;
}

static bool sourceVisibleInSceneSource(obs_source_t *sceneSource, obs_source_t *target)
{
    if (!sceneSource || !target) {
        return false;
    }

    if (sceneSource == target) {
        return true;
    }

    obs_scene_t *scene = obs_scene_from_source(sceneSource);
    if (!scene) {
        return false;
    }

    return sceneHasVisibleSource(scene, target);
}

bool sourceVisibleInProgram(obs_source_t *source)
{
    if (!source) {
        return false;
    }

    // Prefer using OBS' actual program output source (transition), because in Studio Mode the
    // Program display may use duplicated/private scenes that differ from the "current scene".
    //
    // During transitions, the Program output can contain both Source A and Source B. Consider
    // the source visible if it's visible in either one, so we don't blank incorrectly mid-transition.
    OBSSourceAutoRelease output = obs_get_output_source(0);
    if (output) {
        OBSSourceAutoRelease a = obs_transition_get_source(output, OBS_TRANSITION_SOURCE_A);
        OBSSourceAutoRelease b = obs_transition_get_source(output, OBS_TRANSITION_SOURCE_B);

        if (a || b) {
            return sourceVisibleInSceneSource(a, source) || sourceVisibleInSceneSource(b, source);
        }

        OBSSourceAutoRelease active = obs_transition_get_active_source(output);
        if (active) {
            return sourceVisibleInSceneSource(active, source);
        }

        return sourceVisibleInSceneSource(output, source);
    }

    // Fallback: frontend API scene pointer.
    OBSSourceAutoRelease program = obs_frontend_get_current_scene();
    return sourceVisibleInSceneSource(program, source);
}
