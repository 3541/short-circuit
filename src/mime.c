/*
 * SHORT CIRCUIT: MIME -- MIME types.
 *
 * Copyright (c) 2020-2022, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <assert.h>

#include <a3/str.h>

#include <sc/mime.h>

ScMimeType sc_mime_from_path(A3CString path) {
    assert(path.ptr);

    static struct {
        A3CString  extension;
        ScMimeType type;
    } const EXTENSIONS[] = {
        { A3_CS("bmp"), SC_MIME_TYPE_IMAGE_BMP },
        { A3_CS("gif"), SC_MIME_TYPE_IMAGE_GIF },
        { A3_CS("ico"), SC_MIME_TYPE_IMAGE_ICO },
        { A3_CS("jpg"), SC_MIME_TYPE_IMAGE_JPEG },
        { A3_CS("jpeg"), SC_MIME_TYPE_IMAGE_JPEG },
        { A3_CS("json"), SC_MIME_TYPE_APPLICATION_JSON },
        { A3_CS("pdf"), SC_MIME_TYPE_APPLICATION_PDF },
        { A3_CS("png"), SC_MIME_TYPE_IMAGE_PNG },
        { A3_CS("svg"), SC_MIME_TYPE_IMAGE_SVG },
        { A3_CS("webp"), SC_MIME_TYPE_IMAGE_WEBP },
        { A3_CS("css"), SC_MIME_TYPE_TEXT_CSS },
        { A3_CS("js"), SC_MIME_TYPE_TEXT_JAVASCRIPT },
        { A3_CS("md"), SC_MIME_TYPE_TEXT_MARKDOWN },
        { A3_CS("txt"), SC_MIME_TYPE_TEXT_PLAIN },
        { A3_CS("htm"), SC_MIME_TYPE_TEXT_HTML },
        { A3_CS("html"), SC_MIME_TYPE_TEXT_HTML },
    };

    // If there is no extension, default to application/octet-stream.
    A3CString last_dot = a3_string_rchr(path, '.');
    if (!last_dot.ptr || last_dot.len < 2)
        return SC_MIME_TYPE_APPLICATION_OCTET_STREAM;

    // If the last slash is after the last dot, there is no extension.
    A3CString last_slash = a3_string_rchr(path, '/');
    if (last_slash.ptr && last_slash.ptr > last_dot.ptr)
        return SC_MIME_TYPE_APPLICATION_OCTET_STREAM;

    A3CString extension = a3_cstring_new(last_dot.ptr + 1, last_dot.len - 1);
    for (size_t i = 0; i < sizeof(EXTENSIONS) / sizeof(EXTENSIONS[0]); i++) {
        if (a3_string_cmpi(extension, EXTENSIONS[i].extension) == 0)
            return EXTENSIONS[i].type;
    }

    return SC_MIME_TYPE_APPLICATION_OCTET_STREAM;
}
