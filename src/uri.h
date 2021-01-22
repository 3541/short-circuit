/*
 * SHORT CIRCUIT: URI -- URI parsing and decoding.
 *
 * Copyright (c) 2020-2021, Alex O'Brien <3541ax@gmail.com>
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

#include <stdbool.h>

#include <a3/str.h>

#define URI_SCHEME_ENUM                                                        \
    _SCHEME(URI_SCHEME_UNSPECIFIED, "")                                        \
    _SCHEME(URI_SCHEME_HTTP, "http")                                           \
    _SCHEME(URI_SCHEME_HTTPS, "https")                                         \
    _SCHEME(URI_SCHEME_INVALID, "")

typedef enum UriScheme {
#define _SCHEME(T, S) T,
    URI_SCHEME_ENUM
#undef _SCHEME
} UriScheme;

typedef struct Uri {
    UriScheme scheme;
    A3String  authority;
    A3String  path;
    A3String  query;
    A3String  fragment;
} Uri;

typedef enum UriParseResult {
    URI_PARSE_ERROR,
    URI_PARSE_BAD_URI,
    URI_PARSE_TOO_LONG,
    URI_PARSE_SUCCESS
} UriParseResult;

UriParseResult uri_parse(Uri*, A3String);
A3String       uri_path_if_contained(Uri*, A3CString real_root);
bool           uri_is_initialized(Uri*);
void           uri_free(Uri*);
