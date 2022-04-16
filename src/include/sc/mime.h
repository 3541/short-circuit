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

#pragma once

#include <a3/cpp.h>
#include <a3/str.h>

A3_H_BEGIN

typedef A3CString ScMimeType;

#define SC_MIME_TYPE_APPLICATION_OCTET_STREAM A3_CS("application/octet-stream")
#define SC_MIME_TYPE_APPLICATION_JSON         A3_CS("application/json")
#define SC_MIME_TYPE_APPLICATION_PDF          A3_CS("application/pdf")
#define SC_MIME_TYPE_IMAGE_BMP                A3_CS("image/bmp")
#define SC_MIME_TYPE_IMAGE_GIF                A3_CS("image/gif")
#define SC_MIME_TYPE_IMAGE_ICO                A3_CS("image/x-icon")
#define SC_MIME_TYPE_IMAGE_JPEG               A3_CS("image/jpeg")
#define SC_MIME_TYPE_IMAGE_PNG                A3_CS("image/png")
#define SC_MIME_TYPE_IMAGE_SVG                A3_CS("image/svg+xml")
#define SC_MIME_TYPE_IMAGE_WEBP               A3_CS("image/webp")
#define SC_MIME_TYPE_TEXT_CSS                 A3_CS("text/css")
#define SC_MIME_TYPE_TEXT_JAVASCRIPT          A3_CS("text/javascript")
#define SC_MIME_TYPE_TEXT_MARKDOWN            A3_CS("text/markdown")
#define SC_MIME_TYPE_TEXT_PLAIN               A3_CS("text/plain")
#define SC_MIME_TYPE_TEXT_HTML                A3_CS("text/html")

ScMimeType sc_mime_from_path(A3CString path);

A3_H_END
