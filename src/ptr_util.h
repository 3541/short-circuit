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

INLINE String string_clone(CString other) {
    ByteString ret_bytes = bstring_clone(cstring_as_cbstring(other));
    return (String){ .ptr = (char*)ret_bytes.ptr, .len = ret_bytes.len };
}

INLINE int string_cmpi(CString s1, CString s2) {
    if (s1.len != s2.len)
        return -1;
    return strncasecmp(s1.ptr, s2.ptr, s1.len);
}

void bstring_concat(ByteString str, size_t count, ...);
