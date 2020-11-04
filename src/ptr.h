#pragma once

#include <stddef.h>
#include <stdint.h>

#include "util.h"

// Fat pointers for strings and byte strings, to avoid null-termination
// shenanigans. In most cases, utility functions dealing with these types should
// make a best effort to _also_ null-terminate their data.

typedef struct ByteString {
    uint8_t* ptr;
    size_t   len;
} ByteString;

typedef struct CByteString {
    const uint8_t* ptr;
    size_t         len;
} CByteString;

typedef struct String {
    char*  ptr;
    size_t len;
} String;

typedef struct CString {
    const char* ptr;
    size_t      len;
} CString;

ByteString bstring_alloc(size_t len);
ByteString bstring_realloc(ByteString, size_t new_len);
void       bstring_free(ByteString);

String  string_alloc(size_t len);
CString string_from(const char*);
void    string_free(String);
