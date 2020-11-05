#pragma once

#include <stddef.h>
#include <stdint.h>

#include "util.h"

// Fat pointers for strings and byte strings, to avoid null-termination
// shenanigans. Care should be taken not to assume that these are
// null-terminated.

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
String  string_from(char*);
CString cstring_from(const char*);
void    string_free(String);
