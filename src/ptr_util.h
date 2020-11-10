#pragma once

#include <assert.h>
#include <string.h>
#include <sys/param.h>

#include "ptr.h"

#define CS(S)                                                                  \
    (CString) { .ptr = S, .len = (sizeof(S) - 1) / sizeof(char) }
#define CS_NULL (CString){ .ptr = NULL, .len = 0 };
#define S_NULL  (String){ .ptr = NULL, .len = 0 };
ALWAYS_INLINE String CS_MUT(CString s) {
    return (String){ .ptr = (char*)s.ptr, .len = s.len };
}
ALWAYS_INLINE CString S_CONST(String s) {
    return (CString){ .ptr = s.ptr, .len = s.len };
}

#define CBS(S)                                                                 \
    (CByteString) { .ptr = (uint8_t*)S, .len = (sizeof(S) - 1) / sizeof(char) }
#define BS_NULL (ByteString){ .ptr = NULL, .len = 0 };
ALWAYS_INLINE ByteString CBS_MUT(CByteString s) {
    return (ByteString){ .ptr = (uint8_t*)s.ptr, .len = s.len };
}
ALWAYS_INLINE CByteString BS_CONST(ByteString s) {
    return (CByteString){ .ptr = s.ptr, .len = s.len };
}

INLINE const uint8_t* bstring_end(CByteString string) {
    return string.ptr + string.len;
}

INLINE bool bstring_is_string(CByteString string) {
    if (!string.ptr)
        return false;
    for (size_t i = 0; i < string.len; i++)
        TRYB(isascii(string.ptr[i]));
    return true;
}

INLINE ByteString bstring_offset(ByteString string, size_t offset) {
    assert(offset < string.len);
    return (ByteString){ .ptr = string.ptr + offset,
                         .len = string.len - offset };
}

INLINE String bstring_as_string(ByteString bstring) {
    if (!bstring_is_string(BS_CONST(bstring)))
        return S_NULL;
    return (String){ .ptr = (char*)bstring.ptr, .len = bstring.len };
}

INLINE CString cbstring_as_cstring(CByteString bstring) {
    return S_CONST(bstring_as_string(CBS_MUT(bstring)));
}

INLINE CByteString cstring_as_cbstring(CString string) {
    return (CByteString){ .ptr = (uint8_t*)string.ptr, .len = string.len };
}

INLINE ByteString string_as_bstring(String string) {
    return (ByteString) { .ptr = (uint8_t*)string.ptr, .len = string.len };
}

INLINE void bstring_copy(ByteString dest, CByteString src) {
    if (!dest.ptr || !src.ptr)
        return;

    memcpy(dest.ptr, src.ptr, MIN(dest.len, src.len));
}

INLINE ByteString bstring_clone(CByteString other) {
    if (!other.ptr)
        return BS_NULL;

    ByteString ret = bstring_alloc(other.len);
    bstring_copy(ret, other);
    return ret;
}

INLINE void bstring_reverse(ByteString str) {
    for (size_t i = 0; i <= str.len / 2; i++) {
        uint8_t tmp = str.ptr[i];
        str.ptr[i] = str.ptr[str.len - 1 - i];
        str.ptr[str.len - 1 - i] = tmp;
    }
}

INLINE String string_itoa(String str, size_t v) {
    size_t i = 0;
    for (; i < str.len && v; i++, v /= 10)
        str.ptr[i] = "0123456789"[v % 10];
    String ret = { .ptr = str.ptr, .len = i };
    bstring_reverse(string_as_bstring(ret));
    return ret;
}


INLINE String string_clone(CString other) {
    ByteString ret_bytes = bstring_clone(cstring_as_cbstring(other));
    return (String){ .ptr = (char*)ret_bytes.ptr, .len = ret_bytes.len };
}

INLINE int string_cmpi(CString s1, CString s2) {
    if (s1.len != s2.len)
        return -1;
    return strncasecmp(s1.ptr, s2.ptr, s1.len);
}

INLINE CString cstring_rchr(CString str, char c) {
    for (size_t i = str.len - 1;; i--) {
        if (str.ptr[i] == c)
            return (CString){ .ptr = &str.ptr[i], .len = str.len - i };

        if (i == 0)
            break;
    }

    return CS_NULL;
}

void bstring_concat(ByteString str, size_t count, ...);
