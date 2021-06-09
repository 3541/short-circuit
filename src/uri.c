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

#include "uri.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <a3/buffer.h>
#include <a3/str.h>
#include <a3/util.h>

static UriScheme uri_scheme_parse(A3CString name) {
#define _SCHEME(SCHEME, S) { SCHEME, A3_CS(S) },
    static const struct {
        UriScheme scheme;
        A3CString name;
    } URI_SCHEMES[] = { URI_SCHEME_ENUM };
#undef _SCHEME
    assert(name.ptr && *name.ptr);

    A3_TRYB_MAP(name.ptr && a3_string_isascii(name), URI_SCHEME_INVALID);

    for (size_t i = 0; i < sizeof(URI_SCHEMES) / sizeof(URI_SCHEMES[0]); i++) {
        if (a3_string_cmpi(name, URI_SCHEMES[i].name) == 0)
            return URI_SCHEMES[i].scheme;
    }

    return URI_SCHEME_INVALID;
}

static bool uri_decode(A3String str) {
    assert(str.ptr);
    const uint8_t* end = A3_S_END(A3_S_CONST(str));

    for (uint8_t *wp, *rp = wp = str.ptr; wp < end && rp < end && *wp && *rp; wp++) {
        switch (*rp) {
        case '%':
            if (isxdigit(rp[1]) && isxdigit(rp[2])) {
                uint8_t n = 0;
                for (uint8_t i = 0; i < 2; i++) {
                    n *= 16;
                    uint8_t c = rp[2 - i];
                    if ('0' <= c && c <= '9')
                        n += (uint8_t)(c - '0');
                    else
                        n += (uint8_t)(toupper(c) - 'A' + 10);
                }

                if (n == 0)
                    return false;

                *wp = n;
                rp += 3;
            } else {
                return false;
            }
            break;
        case '+':
            *wp = ' ';
            rp++;
            break;
        default:
            *wp = *rp++;
            break;
        }
    }

    return true;
}

static void uri_collapse_dot_segments(A3String str) {
    assert(str.ptr);
    assert(*str.ptr == '/');

    size_t segments = 0;
    for (size_t i = 0; i < str.len; segments += str.ptr[i++] == '/')
        ;
    if (!segments)
        return;
    size_t* segment_indices = calloc(segments, sizeof(size_t));
    A3_UNWRAPND(segment_indices);

    size_t segment_index = 0;
    for (size_t ri, wi = ri = 0; ri < str.len && wi < str.len;) {
        if (!str.ptr[ri])
            break;
        if (str.ptr[ri] == '/') {
            if (ri + 1 < str.len && str.ptr[ri + 1] == '.') {
                if (ri + 2 < str.len && str.ptr[ri + 2] == '.') {
                    ri += 3;
                    // Go back a segment.
                    if (segment_index > 0) {
                        wi = segment_indices[--segment_index] + 1;
                        // Drop extra slashes.
                        if (ri < str.len && str.ptr[ri] == '/')
                            wi--;
                    } else {
                        memcpy(&str.ptr[wi], "/..", 3);
                        wi += 3;
                    }
                } else {
                    // Skip "/.".
                    ri += 2;
                }
            } else {
                // Create a segment.
                wi                               = ri++;
                segment_indices[segment_index++] = wi++;
            }
        } else {
            str.ptr[wi++] = str.ptr[ri++];
        }
    }

    free(segment_indices);
}

static bool uri_normalize_path(A3String str) {
    assert(str.ptr);

    A3_TRYB(uri_decode(str));
    uri_collapse_dot_segments(str);

    return true;
}

UriParseResult uri_parse(Uri* ret, A3String str) {
    assert(ret);
    assert(str.ptr);

    A3Buffer  buf_ = { .data = str, .tail = str.len, .head = 0, .max_cap = str.len };
    A3Buffer* buf  = &buf_;

    memset(ret, 0, sizeof(Uri));
    ret->scheme = URI_SCHEME_UNSPECIFIED;

    // [<scheme>://][authority]<path>[query][fragment]
    if (a3_buf_memmem(buf, A3_CS("://")).ptr) {
        ret->scheme =
            uri_scheme_parse(A3_S_CONST(a3_buf_token_next(buf, A3_CS("://"), A3_PRES_END_NO)));
        if (ret->scheme == URI_SCHEME_INVALID)
            return URI_PARSE_BAD_URI;
    }

    // [authority]<path>[query][fragment]
    if (buf->data.ptr[buf->head] != '/' && ret->scheme != URI_SCHEME_UNSPECIFIED) {
        ret->authority =
            a3_string_clone(A3_S_CONST(a3_buf_token_next(buf, A3_CS("/"), A3_PRES_END_NO)));
        A3_TRYB_MAP(ret->authority.ptr, URI_PARSE_BAD_URI);
        buf->data.ptr[--buf->head] = '/';
    }

    // <path>[query][fragment]
    ret->path = a3_buf_token_next_copy(buf, A3_CS("#?\r\n"), A3_PRES_END_NO);
    A3_TRYB_MAP(ret->path.ptr, URI_PARSE_BAD_URI);
    if (ret->path.len == 0)
        return URI_PARSE_BAD_URI;
    A3_TRYB_MAP(uri_normalize_path(ret->path), URI_PARSE_BAD_URI);
    if (a3_buf_len(buf) == 0)
        return URI_PARSE_SUCCESS;

    // [query][fragment]
    ret->query = a3_buf_token_next_copy(buf, A3_CS("#"), A3_PRES_END_NO);
    A3_TRYB_MAP(ret->query.ptr, URI_PARSE_BAD_URI);
    A3_TRYB_MAP(uri_decode(ret->query), URI_PARSE_BAD_URI);
    if (a3_buf_len(buf) == 0)
        return URI_PARSE_SUCCESS;

    // [fragment]
    ret->fragment = a3_buf_token_next_copy(buf, A3_CS(""), A3_PRES_END_NO);
    A3_TRYB_MAP(ret->fragment.ptr, URI_PARSE_BAD_URI);
    uri_decode(ret->fragment);
    assert(a3_buf_len(buf) == 0);

    return URI_PARSE_SUCCESS;
}

// Return the path to the pointed-to file if it is a child of the given root
// path.
A3String uri_path_if_contained(Uri* uri, A3CString real_root) {
    assert(uri);
    assert(real_root.ptr && *real_root.ptr);

    // Ensure there are no directory escaping shenanigans. This occurs after
    // decoding, so ".." should be the only way such a thing can occur.
    //
    // TODO: This only makes sense for static files since parts of the path
    // which are used by an endpoint are perfectly allowed to contain "..".
    for (size_t i = 0; i < uri->path.len - 1; i++)
        if (uri->path.ptr[i] == '.' && uri->path.ptr[i + 1] == '.')
            return A3_S_NULL;

    if (uri->path.len == 1 && *uri->path.ptr == '/')
        return a3_string_clone(real_root);

    A3String ret = a3_string_alloc(real_root.len + uri->path.len);
    a3_string_concat(ret, 2, real_root, uri->path);
    return ret;
}

bool uri_is_initialized(Uri* uri) {
    assert(uri);

    return uri->path.ptr;
}

void uri_free(Uri* uri) {
    assert(uri_is_initialized(uri));

    if (uri->authority.ptr)
        a3_string_free(&uri->authority);
    if (uri->path.ptr)
        a3_string_free(&uri->path);
    if (uri->query.ptr)
        a3_string_free(&uri->query);
    if (uri->fragment.ptr)
        a3_string_free(&uri->fragment);
}
