/*
 * SHORT CIRCUIT: URI -- URI parsing and decoding.
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

#include <a3/str.h>
#include <a3/types.h>

A3_H_BEGIN

#define SC_URI_SCHEME_ENUM                                                                         \
    SCHEME(SC_URI_SCHEME_UNSPECIFIED, "")                                                          \
    SCHEME(SC_URI_SCHEME_HTTP, "http")                                                             \
    SCHEME(SC_URI_SCHEME_HTTPS, "https")                                                           \
    SCHEME(SC_URI_SCHEME_INVALID, "")

typedef enum ScUriScheme {
#define SCHEME(T, S) T,
    SC_URI_SCHEME_ENUM
#undef SCHEME
} ScUriScheme;

typedef enum ScUriParseResult {
    SC_URI_PARSE_ERROR,
    SC_URI_PARSE_BAD_URI,
    SC_URI_PARSE_TOO_LONG,
    SC_URI_PARSE_OK
} ScUriParseResult;

typedef enum ScUriForm {
    SC_URI_FORM_ORIGIN,
    SC_URI_FORM_ABSOLUTE,
    SC_URI_FORM_AUTHORITY,
    SC_URI_FORM_ASTERISK
} ScUriForm;

typedef struct ScUri {
    ScUriScheme scheme;
    ScUriForm   form;
    A3String    data;
    A3CString   authority;
    A3CString   path;
    A3CString   query;
} ScUri;

A3_EXPORT ScUriParseResult sc_uri_parse(ScUri*, A3String);

A3_H_END
