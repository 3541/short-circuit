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

#include <assert.h>
#include <stdbool.h>

#include <a3/buffer.h>
#include <a3/str.h>

#include <sc/uri.h>

static ScUriScheme sc_uri_scheme_parse(A3CString scheme) {
    assert(scheme.ptr);

#define SCHEME(T, S) { T, A3_CS(S) },
    static struct {
        ScUriScheme scheme;
        A3CString   name;
    } const URI_SCHEMES[] = { SC_URI_SCHEME_ENUM };
#undef SCHEME

    for (size_t i = 0; i < sizeof(URI_SCHEMES) / sizeof(URI_SCHEMES[0]); i++) {
        if (a3_string_cmpi(scheme, URI_SCHEMES[i].name) == 0)
            return URI_SCHEMES[i].scheme;
    }

    return SC_URI_SCHEME_INVALID;
}

static bool sc_uri_decode(A3String* path) {
    assert(path);
    assert(path->ptr && path->len);

    size_t removed = 0;
    for (size_t r = 0, w = 0; r < path->len;) {
        if (path->ptr[r] != '%') {
            path->ptr[w++] = path->ptr[r++];
            continue;
        }

        if (path->len - r < 3 || !isxdigit(path->ptr[r + 1]) || !isxdigit(path->ptr[r + 2]))
            return false;

        uint8_t n = 0;
        for (uint8_t i = 1; i <= 2; i++) {
            n *= 16;
            uint8_t c = path->ptr[r + i];
            if ('0' <= c && c <= '9')
                n += (uint8_t)(c - '0');
            else
                n += (uint8_t)(toupper(c) - 'A' + 10);
        }
        A3_TRYB(n);

        path->ptr[w++] = n;
        path->ptr[w]   = '\0';
        removed += 2;
        r += 3;
    }
    path->len -= removed;

    return true;
}

static void sc_uri_path_collapse(A3String path) {
    assert(path.ptr && path.len);
    assert(*path.ptr == '/');

    uint8_t* SEGMENTS[10] = { 0 };

    size_t segment_count = 0;
    for (size_t i = 0; i < path.len; segment_count += path.ptr[i++] == '/')
        ;
    if (!segment_count)
        return;

    uint8_t** segments = SEGMENTS;
    if (segment_count > sizeof(SEGMENTS) / sizeof(SEGMENTS[0]))
        A3_UNWRAPN(segments, calloc(segment_count, sizeof(*segments)));

    uint8_t const* end           = a3_string_end(A3_S_CONST(path));
    uint8_t*       w             = path.ptr;
    uint8_t*       r             = w;
    size_t         segment_index = 0;

    while (*r && r < end) {
        if (*r != '/') {
            *w++ = *r++;
            continue;
        }

        if (r + 1 < end && r[1] == '.') {
            if (r + 2 < end && r[2] == '.') {
                r += 3;
                // Go back a segment.
                if (segment_index > 0) {
                    w = segments[--segment_index] + 1;
                    // Drop extra slashes.
                    if (r < end && *r == '/')
                        w--;
                } else {
                    memcpy(w, "/..", 3);
                    w += 3;
                }
            } else if (r + 2 >= end || r[2] == '/') {
                // Skip "/.".
                r += 2;
            } else {
                *w++ = *r++;
            }
        } else {
            w                         = r++;
            segments[segment_index++] = w++;
        }
    }

    if (segments != SEGMENTS)
        free(segments);
}

static bool sc_uri_path_normalize(A3String* path) {
    assert(path);
    assert(path->ptr && path->len);

    A3_TRYB(sc_uri_decode(path));
    sc_uri_path_collapse(*path);

    // After collapse of legal '..' segments, there should be none remaining. Anything else is a
    // directory escape.
    for (size_t i = 0; i < path->len - 1; i++) {
        if (path->ptr[i] == '.' && path->ptr[i + 1] == '.' &&
            (i + 1 >= path->len || path->ptr[i + 2] == '/'))
            return false;
    }

    return true;
}

ScUriParseResult sc_uri_parse(ScUri* uri, A3String str) {
    assert(uri);
    assert(str.ptr);

    A3Buffer buf = { .data = str, .tail = str.len, .head = 0, .max_cap = str.len };

    *uri = (ScUri) {
        .scheme    = SC_URI_SCHEME_UNSPECIFIED,
        .form      = SC_URI_FORM_ORIGIN,
        .data      = A3_S_NULL,
        .authority = A3_CS_NULL,
        .path      = A3_CS_NULL,
        .query     = A3_CS_NULL,
    };

    // [<scheme>://][authority]<path>[?<query>][#<fragment>]
    if (a3_buf_memmem(&buf, A3_CS("://")).ptr) {
        uri->form = SC_URI_FORM_ABSOLUTE;
        uri->scheme =
            sc_uri_scheme_parse(A3_S_CONST(a3_buf_token_next(&buf, A3_CS("://"), A3_PRES_END_NO)));
        A3_TRYB_MAP(uri->scheme != SC_URI_SCHEME_INVALID, SC_URI_PARSE_BAD_URI);
    }

    // [authority]<path>[?<query>][#<fragment>]
    if (buf.data.ptr[buf.head] != '/') {
        if (uri->form != SC_URI_FORM_ABSOLUTE)
            uri->form = SC_URI_FORM_AUTHORITY;
        uri->authority = A3_S_CONST(a3_buf_token_next(&buf, A3_CS("/"), A3_PRES_END_YES));
    }

    // <path>[?<query>][#<fragment>]
    uri->path = A3_S_CONST(a3_buf_token_next(&buf, A3_CS("?\r\n"), A3_PRES_END_YES));
    A3_TRYB_MAP(uri->path.ptr && uri->path.len, SC_URI_PARSE_BAD_URI);
    A3_TRYB_MAP(sc_uri_path_normalize((A3String*)&uri->path), SC_URI_PARSE_BAD_URI);

    // [?<query>][#<fragment>]
    if (a3_buf_consume(&buf, A3_CS("?"))) {
        uri->query = A3_S_CONST(a3_buf_token_next(&buf, A3_CS("#"), A3_PRES_END_YES));
        A3_TRYB_MAP(uri->query.ptr, SC_URI_PARSE_BAD_URI);
        A3_TRYB_MAP(sc_uri_decode((A3String*)&uri->query), SC_URI_PARSE_BAD_URI);
    }

    // Fragment need not be parsed.
    return SC_URI_PARSE_OK;
}

bool sc_uri_is_initialized(ScUri* uri) {
    assert(uri);

    return uri->data.ptr;
}

A3CString sc_uri_path_relative(ScUri* uri) {
    assert(uri);

    return uri->path.ptr ? A3_S_CONST(a3_string_offset(A3_CS_MUT(uri->path), 1)) : A3_CS_NULL;
}
