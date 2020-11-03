#include "uri.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "buffer.h"
#include "config.h"
#include "util.h"

enum UriScheme uri_scheme_parse(const uint8_t* name) {
#define _SCHEME(SCHEME, S) { SCHEME, S },
    static const struct {
        enum UriScheme scheme;
        const char*    name;
    } URI_SCHEMES[] = { URI_SCHEME_ENUM };
#undef _SCHEME
    assert(name && *name);

    TRYB_MAP(bytes_are_string(name), URI_SCHEME_INVALID);

    const char* name_str = (const char*)name;
    for (size_t i = 0; i < sizeof(URI_SCHEMES) / sizeof(URI_SCHEMES[0]); i++) {
        if (strcasecmp(name_str, URI_SCHEMES[i].name) == 0)
            return URI_SCHEMES[i].scheme;
    }

    return URI_SCHEME_INVALID;
}

static bool uri_decode(uint8_t* str) {
    assert(str);

    for (uint8_t *wp, *rp = wp = str; *wp && *rp; wp++) {
        switch (*rp) {
        case '%':
            if (isxdigit(rp[1]) && isxdigit(rp[2])) {
                uint8_t n = 0;
                for (uint8_t i = 0; i < 2; i++) {
                    n *= 16;
                    uint8_t c = rp[2 - i];
                    if ('0' <= c && c <= '9')
                        n += c - '0';
                    else
                        n += toupper(c) - 'A' + 10;
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

static void uri_collapse_dot_segments(uint8_t* str) {
    assert(str);
    assert(*str == '/');

    size_t segments = 0;
    for (uint8_t* sp = str; *sp; sp++, segments += *sp == '/')
        ;
    uint8_t** segment_ptrs = calloc(segments, sizeof(uint8_t*));
    UNWRAPND(segment_ptrs);

    size_t segment_index = 0;
    for (uint8_t *rp, *wp = rp = str; *wp;) {
        if (!*rp) {
            *wp = '\0';
        } else if (*rp == '/') {
            if (rp[1] && rp[1] == '.') {
                if (rp[2] && rp[2] == '.') {
                    rp += 3;
                    // Go back a segment after "/..".
                    if (segment_index > 0) {
                        wp = segment_ptrs[--segment_index] + 1;
                        if (rp && *rp == '/')
                            wp--;
                    } else {
                        memcpy(wp, "/..", 3);
                        wp += 3;
                    }
                } else {
                    // Skip "/.".
                    rp += 2;
                }
            } else {
                // Create a segment at "/".
                *wp                           = *rp++;
                segment_ptrs[segment_index++] = wp++;
            }
        } else {
            *wp++ = *rp++;
        }
    }

    free(segment_ptrs);
}

static bool uri_normalize_path(uint8_t* str) {
    assert(str);

    TRYB(uri_decode(str));
    uri_collapse_dot_segments(str);

    return true;
}

int8_t uri_parse(struct Uri* ret, uint8_t* str) {
    assert(ret);
    assert(str);

    size_t len = strlen((char*)str);
    if (len > HTTP_REQUEST_URI_MAX_LENGTH)
        return URI_PARSE_TOO_LONG;

    struct Buffer  buf_ = { .data = str };
    struct Buffer* buf  = &buf_;
    buf_init(buf, len, len);

    memset(ret, 0, sizeof(struct Uri));
    ret->scheme = URI_SCHEME_UNSPECIFIED;

    // [scheme]://[authority]<path>[query][fragment]
    if (buf_memmem(buf, "://")) {
        ret->scheme = uri_scheme_parse(buf_token_next(buf, "://"));
        if (ret->scheme == URI_SCHEME_INVALID)
            return URI_PARSE_BAD_URI;
    }

    // [authority]<path>[query][fragment]
    if (buf->data[buf->head] != '/' && ret->scheme != URI_SCHEME_UNSPECIFIED) {
        TRYB_MAP(ret->authority = strndup(buf_token_next_str(buf, "/"),
                                          HTTP_REQUEST_URI_MAX_LENGTH),
                 URI_PARSE_BAD_URI);
        buf->data[--buf->head] = '/';
    }

    // <path>[query][fragment]
    TRYB_MAP(ret->path = buf_token_next_copy(buf, "#?"), URI_PARSE_BAD_URI);
    TRYB_MAP(uri_normalize_path(ret->path), URI_PARSE_BAD_URI);
    TRYB_MAP(buf_len(buf), URI_PARSE_SUCCESS);

    // [query][fragment]
    TRYB_MAP(ret->query = buf_token_next_copy(buf, "?"), URI_PARSE_BAD_URI);
    TRYB_MAP(uri_decode(ret->query), URI_PARSE_BAD_URI);
    TRYB_MAP(buf_len(buf), URI_PARSE_SUCCESS);

    // [fragment]
    TRYB_MAP(ret->fragment = buf_token_next_copy(buf, ""), URI_PARSE_BAD_URI);
    uri_decode(ret->fragment);
    assert(buf_len(buf) == 0);

    return URI_PARSE_SUCCESS;
}

bool uri_path_is_contained(struct Uri* this, const char* real_root,
                           size_t path_length) {
    assert(this);
    assert(real_root && *real_root);

    size_t buf_len = path_length + strlen((char*)this->path) + 2;
    char*  buf     = malloc(buf_len);
    snprintf(buf, buf_len, "%s/%s", real_root, (char*)this->path);

    for (size_t i = 0; i < path_length; i++)
        TRYB(real_root[i] == buf[i]);
    return true;
}

bool uri_is_initialized(struct Uri* this) {
    assert(this);

    return this->path;
}

void uri_free(struct Uri* this) {
    assert(uri_is_initialized(this));

    if (this->authority)
        free(this->authority);
    if (this->path)
        free(this->path);
    if (this->query)
        free(this->query);
    if (this->fragment)
        free(this->fragment);
}
