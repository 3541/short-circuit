#include "file.h"

#include <assert.h>
#include <stdint.h>

#include <a3/cache.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"

typedef struct FileKey {
    int32_t      flags;
    InlineString path;
} FileKey;

typedef FileKey* FileKeyPtr;

typedef struct FileHandle {
    fd       file;
    uint32_t open_count;
} FileHandle;

static const uint8_t* file_key_bytes(FileKey* key) {
    return (const uint8_t*)key;
}

static size_t file_key_size(FileKey* key) {
    return sizeof(FileKey) - 1 + key->path.len * sizeof(uint8_t);
}

static int8_t file_key_compare(FileKey* lhs, FileKey* rhs) {
    if (lhs->flags != rhs->flags)
        return 1;
    return string_cmp(S_CONST(SI(lhs->path)), S_CONST(SI(rhs->path))) != 0;
}

CACHE_DEFINE_STRUCTS(FileKeyPtr, FileHandle);
CACHE_DECLARE_METHODS(FileKeyPtr, FileHandle);
CACHE_DEFINE_METHODS(FileKeyPtr, FileHandle, file_key_bytes, file_key_size,
                     file_key_compare);
typedef CACHE(FileKeyPtr, FileHandle) FileCache;

static void file_close_callback(FileKey** key, FileHandle* value) {
    assert(key);
    assert(*key);
    assert(value);

    free(*key);
    if (value->open_count == 0) {
        // URING
        return;
    }
}

static FileCache file_cache;

void file_cache_init() {
    CACHE_INIT(FileKeyPtr, FileHandle)
    (&file_cache, FD_CACHE_SIZE, file_close_callback);
}

fd file_open(CString path, int32_t flags) {
    assert(path.ptr);
    (void)flags;
    PANIC("TODO");
    return -1;
}

void file_close(fd file) { assert(file >= 0); }
