#include "ptr.h"
#include "ptr_util.h"

#include <assert.h>
#include <stdarg.h>
#include <string.h>

ByteString bstring_alloc(size_t len) {
    return (ByteString){ .ptr = calloc(len, sizeof(uint8_t)), .len = len };
}

ByteString bstring_realloc(ByteString this, size_t new_len) {
    return (ByteString){ .ptr = realloc(this.ptr, new_len), .len = new_len };
}

void bstring_concat(ByteString str, size_t count, ...) {
    va_list args;
    va_start(args, count);

    size_t offset = 0;
    for (size_t i = 0; i < count; i++) {
        CByteString arg = va_arg(args, CByteString);
        bstring_copy(bstring_offset(str, offset), arg);
        offset += arg.len;
    }
}

void bstring_free(ByteString this) {
    assert(this.ptr);
    free(this.ptr);
}

String string_alloc(size_t len) {
    return (String){ .ptr = calloc(len, sizeof(char)), .len = len };
}

CString string_from(const char* str) {
    return (CString){ .ptr = str, .len = strlen(str) };
}

void string_free(String this) {
    assert(this.ptr);
    free(this.ptr);
}
