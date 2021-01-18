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

static UriScheme uri_scheme_parse(CString name) {
#define _SCHEME(SCHEME, S) { SCHEME, CS(S) },
    static const struct {
        UriScheme scheme;
        CString   name;
    } URI_SCHEMES[] = { URI_SCHEME_ENUM };
#undef _SCHEME
    assert(name.ptr && *name.ptr);

    TRYB_MAP(name.ptr && string_isascii(name), URI_SCHEME_INVALID);

    for (size_t i = 0; i < sizeof(URI_SCHEMES) / sizeof(URI_SCHEMES[0]); i++) {
        if (string_cmpi(name, URI_SCHEMES[i].name) == 0)
            return URI_SCHEMES[i].scheme;
    }

    return URI_SCHEME_INVALID;
}

static bool uri_decode(String str) {
    assert(str.ptr);
    const uint8_t* end = S_END(S_CONST(str));

    for (uint8_t *wp, *rp = wp = str.ptr; wp < end && rp < end && *wp && *rp;
         wp++) {
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

static void uri_collapse_dot_segments(String str) {
    assert(str.ptr);
    assert(*str.ptr == '/');

    size_t segments = 0;
    for (size_t i = 0; i < str.len; segments += str.ptr[i++] == '/')
        ;
    if (!segments)
        return;
    size_t* segment_indices = calloc(segments, sizeof(size_t));
    UNWRAPND(segment_indices);

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

static bool uri_normalize_path(String str) {
    assert(str.ptr);

    TRYB(uri_decode(str));
    uri_collapse_dot_segments(str);

    return true;
}

UriParseResult uri_parse(Uri* ret, String str) {
    assert(ret);
    assert(str.ptr);

    Buffer buf_ = {
        .data = str, .tail = str.len, .head = 0, .max_cap = str.len
    };
    Buffer* buf = &buf_;

    memset(ret, 0, sizeof(Uri));
    ret->scheme = URI_SCHEME_UNSPECIFIED;

    // [scheme]://[authority]<path>[query][fragment]
    if (buf_memmem(buf, CS("://")).ptr) {
        ret->scheme = uri_scheme_parse(S_CONST(buf_token_next(buf, CS("://"))));
        if (ret->scheme == URI_SCHEME_INVALID)
            return URI_PARSE_BAD_URI;
    }

    // [authority]<path>[query][fragment]
    if (buf->data.ptr[buf->head] != '/' &&
        ret->scheme != URI_SCHEME_UNSPECIFIED) {
        ret->authority = string_clone(S_CONST(buf_token_next(buf, CS("/"))));
        TRYB_MAP(ret->authority.ptr, URI_PARSE_BAD_URI);
        buf->data.ptr[--buf->head] = '/';
    }

    // <path>[query][fragment]
    ret->path = buf_token_next_copy(buf, CS("#?\r\n"));
    TRYB_MAP(ret->path.ptr, URI_PARSE_BAD_URI);
    if (ret->path.len == 0)
        return URI_PARSE_BAD_URI;
    TRYB_MAP(uri_normalize_path(ret->path), URI_PARSE_BAD_URI);
    if (buf_len(buf) == 0)
        return URI_PARSE_SUCCESS;

    // [query][fragment]
    ret->query = buf_token_next_copy(buf, CS("?"));
    TRYB_MAP(ret->query.ptr, URI_PARSE_BAD_URI);
    TRYB_MAP(uri_decode(ret->query), URI_PARSE_BAD_URI);
    if (buf_len(buf) == 0)
        return URI_PARSE_SUCCESS;

    // [fragment]
    ret->fragment = buf_token_next_copy(buf, CS(""));
    TRYB_MAP(ret->fragment.ptr, URI_PARSE_BAD_URI);
    uri_decode(ret->fragment);
    assert(buf_len(buf) == 0);

    return URI_PARSE_SUCCESS;
}

// Return the path to the pointed-to file if it is a child of the given root
// path.
String uri_path_if_contained(Uri* this, CString real_root) {
    assert(this);
    assert(real_root.ptr && *real_root.ptr);

    // Ensure there are no directory escaping shenanigans. This occurs after
    // decoding, so ".." should be the only way such a thing can occur.
    //
    // TODO: This only makes sense for static files since parts of the path
    // which are used by an endpoint are perfectly allowed to contain "..".
    for (size_t i = 0; i < this->path.len - 1; i++)
        if (this->path.ptr[i] == '.' && this->path.ptr[i + 1] == '.')
            return S_NULL;

    if (this->path.len == 1 && *this->path.ptr == '/')
        return string_clone(real_root);

    String ret = string_alloc(real_root.len + this->path.len);
    string_concat(ret, 2, real_root, this->path);
    return ret;
}

bool uri_is_initialized(Uri* this) {
    assert(this);

    return this->path.ptr;
}

void uri_free(Uri* this) {
    assert(uri_is_initialized(this));

    if (this->authority.ptr)
        string_free(&this->authority);
    if (this->path.ptr)
        string_free(&this->path);
    if (this->query.ptr)
        string_free(&this->query);
    if (this->fragment.ptr)
        string_free(&this->fragment);
}
