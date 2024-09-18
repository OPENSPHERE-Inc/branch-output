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
#include <util/platform.h>

#include <QString>

#include "utils.hpp"

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

QString generateSpecifiedFilename(const char *extension, bool noSpace, const char *format)
{
    OBSString filename = os_generate_formatted_filename(extension, !noSpace, format);
    return QString(filename);
}

void ensureDirectoryExists(QString path)
{
    path.replace('\\', '/');

    // Remove file part (also remove trailing slash)
	size_t last = path.lastIndexOf('/');
	if (last < 0) {
		return;
    }

	QString directory = path.left(last);
	os_mkdirs(qUtf8Printable(directory));
}

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