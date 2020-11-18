#include "ptr.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>

String string_alloc(size_t len) {
    return (String){ .ptr = calloc(len, sizeof(char)), .len = len };
}

String string_realloc(String this, size_t new_len) {
    return (String){ .ptr = realloc(this.ptr, new_len), .len = new_len };
}

String string_clone(CString other) {
    if (!other.ptr)
        return S_NULL;

    String ret = string_alloc(other.len);
    string_copy(ret, other);
    return ret;
}

void string_free(String* this) {
    assert(this->ptr);
    free(this->ptr);
    this->ptr = NULL;
    this->len = 0;
}

void string_copy(String dst, CString src) {
    if (!dst.ptr || !src.ptr)
        return;
    memcpy(dst.ptr, src.ptr, MIN(dst.len, src.len));
}

void string_concat(String str, size_t count, ...) {
    assert(str.ptr);

    va_list args;
    va_start(args, count);

    size_t offset = 0;
    for (size_t i = 0; i < count; i++) {
        CString arg = va_arg(args, CString);
        string_copy(S_OFFSET(str, offset), arg);
        offset += arg.len;
    }
}

void string_reverse(String str) {
    for (size_t i = 0; i < str.len / 2; i++) {
        uint8_t tmp              = str.ptr[i];
        str.ptr[i]               = str.ptr[str.len - 1 - i];
        str.ptr[str.len - 1 - i] = tmp;
    }
}

String string_itoa(String dst, size_t v) {
    size_t i = 0;
    for (; i < dst.len && v; i++, v /= 10)
        dst.ptr[i] = "0123456789"[v % 10];
    String ret = { .ptr = dst.ptr, .len = i };
    string_reverse(ret);
    return ret;
}

bool string_isascii(CString str) {
    assert(str.ptr);

    for (size_t i = 0; i < str.len; i++)
        if (!isascii(str.ptr[i]))
            return false;
    return true;
}

int string_cmpi(CString s1, CString s2) {
    assert(s1.ptr && s2.ptr);
    if (s1.len != s2.len)
        return -1;
    return strncasecmp((char*)s1.ptr, (char*)s2.ptr, s1.len);
}

CString string_rchr(CString str, uint8_t c) {
    assert(str.ptr);

    for (size_t i = str.len - 1;; i--) {
        if (str.ptr[i] == c)
            return (CString){ .ptr = &str.ptr[i], .len = str.len - i };

        if (i == 0)
            break;
    }

    return CS_NULL;
}
