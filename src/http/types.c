#include "http/types.h"

#include <assert.h>

HttpMethod http_request_method_parse(CString str) {
#define _METHOD(M, N) { M, CS(N) },
    static const struct {
        HttpMethod method;
        CString    name;
    } HTTP_METHOD_NAMES[] = { HTTP_METHOD_ENUM };
#undef _METHOD

    assert(str.ptr && *str.ptr);

    TRYB_MAP(str.ptr && string_isascii(str), HTTP_METHOD_INVALID);

    for (size_t i = 0;
         i < sizeof(HTTP_METHOD_NAMES) / sizeof(HTTP_METHOD_NAMES[0]); i++) {
        if (string_cmpi(str, HTTP_METHOD_NAMES[i].name) == 0)
            return HTTP_METHOD_NAMES[i].method;
    }

    return HTTP_METHOD_UNKNOWN;
}

#define _VERSION(V, S) [V] = CS(S),
static const CString HTTP_VERSION_STRINGS[] = { HTTP_VERSION_ENUM };
#undef _VERSION

CString http_version_string(HttpVersion version) {
    return HTTP_VERSION_STRINGS[version];
}

HttpVersion http_version_parse(CString str) {
    assert(str.ptr && *str.ptr);

    TRYB_MAP(str.ptr && string_isascii(str), HTTP_VERSION_INVALID);

    for (HttpVersion v = HTTP_VERSION_INVALID + 1; v < HTTP_VERSION_UNKNOWN;
         v++)
        if (string_cmpi(str, HTTP_VERSION_STRINGS[v]) == 0)
            return v;

    return HTTP_VERSION_UNKNOWN;
}

CString http_status_reason(HttpStatus status) {
#define _STATUS(CODE, TYPE, REASON) { CODE, CS(REASON) },
    static const struct {
        HttpStatus status;
        CString    reason;
    } HTTP_STATUS_REASONS[] = { HTTP_STATUS_ENUM };
#undef STATUS

    for (size_t i = 0;
         i < sizeof(HTTP_STATUS_REASONS) / sizeof(HTTP_STATUS_REASONS[0]);
         i++) {
        if (status == HTTP_STATUS_REASONS[i].status)
            return HTTP_STATUS_REASONS[i].reason;
    }

    return CS_NULL;
}

#define _CTYPE(T, S) { T, CS(S) },
static const struct {
    HttpContentType type;
    CString         str;
} HTTP_CONTENT_TYPE_NAMES[] = { HTTP_CONTENT_TYPE_ENUM };
#undef _CTYPE

CString http_content_type_name(HttpContentType type) {
    for (size_t i = 0; i < sizeof(HTTP_CONTENT_TYPE_NAMES) /
                               sizeof(HTTP_CONTENT_TYPE_NAMES[0]);
         i++) {
        if (type == HTTP_CONTENT_TYPE_NAMES[i].type)
            return HTTP_CONTENT_TYPE_NAMES[i].str;
    }

    return CS_NULL;
}

HttpContentType http_content_type_from_path(CString path) {
    assert(path.ptr);

    static struct {
        CString         ext;
        HttpContentType ctype;
    } EXTENSIONS[] = {
        { CS("bmp"), HTTP_CONTENT_TYPE_IMAGE_BMP },
        { CS("gif"), HTTP_CONTENT_TYPE_IMAGE_GIF },
        { CS("ico"), HTTP_CONTENT_TYPE_IMAGE_ICO },
        { CS("jpg"), HTTP_CONTENT_TYPE_IMAGE_JPEG },
        { CS("jpeg"), HTTP_CONTENT_TYPE_IMAGE_JPEG },
        { CS("json"), HTTP_CONTENT_TYPE_APPLICATION_JSON },
        { CS("pdf"), HTTP_CONTENT_TYPE_APPLICATION_PDF },
        { CS("png"), HTTP_CONTENT_TYPE_IMAGE_PNG },
        { CS("svg"), HTTP_CONTENT_TYPE_IMAGE_SVG },
        { CS("webp"), HTTP_CONTENT_TYPE_IMAGE_WEBP },
        { CS("css"), HTTP_CONTENT_TYPE_TEXT_CSS },
        { CS("js"), HTTP_CONTENT_TYPE_TEXT_JAVASCRIPT },
        { CS("txt"), HTTP_CONTENT_TYPE_TEXT_PLAIN },
        { CS("htm"), HTTP_CONTENT_TYPE_TEXT_HTML },
        { CS("html"), HTTP_CONTENT_TYPE_TEXT_HTML },
    };

    CString last_dot = string_rchr(path, '.');
    if (!last_dot.ptr || last_dot.len < 2)
        return HTTP_CONTENT_TYPE_APPLICATION_OCTET_STREAM;

    CString last_slash = string_rchr(path, '/');
    if (last_slash.ptr && last_slash.ptr > last_dot.ptr)
        return HTTP_CONTENT_TYPE_APPLICATION_OCTET_STREAM;

    CString ext = { .ptr = last_dot.ptr + 1, .len = last_dot.len - 1 };
    for (size_t i = 0; i < sizeof(EXTENSIONS) / sizeof(EXTENSIONS[0]); i++) {
        if (string_cmpi(ext, EXTENSIONS[i].ext) == 0)
            return EXTENSIONS[i].ctype;
    }

    return HTTP_CONTENT_TYPE_APPLICATION_OCTET_STREAM;
}

#define _TENCODING(E, S) { HTTP_##E, CS(S) },
static const struct {
    HttpTransferEncoding encoding;
    CString              value;
} HTTP_TRANSFER_ENCODING_VALUES[] = { HTTP_TRANSFER_ENCODING_ENUM };
#undef _TENCODING

HttpTransferEncoding http_transfer_encoding_parse(CString value) {
    assert(value.ptr && *value.ptr);

    TRYB_MAP(value.ptr && string_isascii(value),
             HTTP_TRANSFER_ENCODING_INVALID);

    for (size_t i = 0; i < sizeof(HTTP_TRANSFER_ENCODING_VALUES) /
                               sizeof(HTTP_TRANSFER_ENCODING_VALUES[0]);
         i++) {
        if (string_cmpi(value, HTTP_TRANSFER_ENCODING_VALUES[i].value) == 0)
            return HTTP_TRANSFER_ENCODING_VALUES[i].encoding;
    }

    return HTTP_TRANSFER_ENCODING_INVALID;
}
