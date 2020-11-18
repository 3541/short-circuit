#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "util.h"

// Fat pointers for strings and byte strings, to avoid null-termination
// shenanigans. Care should be taken not to assume that these are
// null-terminated.

typedef struct String {
    uint8_t* ptr;
    size_t   len;
} String;

typedef struct CString {
    const uint8_t* ptr;
    size_t         len;
} CString;

#define CS(S)                                                                  \
    (CString) { .ptr = (uint8_t*)S, .len = (sizeof(S) - 1) }
#define CS_NULL (CString){ .ptr = NULL, .len = 0 };
#define S_NULL  (String){ .ptr = NULL, .len = 0 };
ALWAYS_INLINE String CS_MUT(CString s) {
    return (String){ .ptr = (uint8_t*)s.ptr, .len = s.len };
}
ALWAYS_INLINE CString S_CONST(String s) {
    return (CString){ .ptr = s.ptr, .len = s.len };
}
ALWAYS_INLINE String S_OF(char* str) {
    if (!str)
        return S_NULL;
    return (String){ .ptr = (uint8_t*)str, .len = strlen(str) };
}
ALWAYS_INLINE CString CS_OF(const char* str) {
    return S_CONST(S_OF((char*)str));
}
ALWAYS_INLINE String S_OFFSET(String s, size_t offset) {
    return (String){ .ptr = s.ptr + offset, .len = s.len - offset };
}
ALWAYS_INLINE const uint8_t* S_END(CString s) { return s.ptr + s.len; }

String string_alloc(size_t len);
String string_realloc(String, size_t new_len);
String string_clone(CString);
void   string_free(String*);

void string_copy(String dst, CString src);
void string_concat(String dst, size_t count, ...);

void   string_reverse(String);
String string_itoa(String dst, size_t);

bool    string_isascii(CString);
int     string_cmpi(CString lhs, CString rhs);
CString string_rchr(CString, uint8_t c);
