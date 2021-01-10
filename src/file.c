/*
 * SHORT CIRCUIT: FILE -- Open file descriptor cache.
 *
 * Copyright (c) 2020-2021, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "file.h"

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>

#include <a3/cache.h>
#include <a3/ht.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "event.h"
#include "forward.h"

typedef struct FileHandle {
    CString  path;
    fd       file;
    uint32_t open_count;
    int32_t  flags;
} FileHandle;

typedef FileHandle* FileHandlePtr;

CACHE_DEFINE_STRUCTS(CString, FileHandlePtr);
CACHE_DECLARE_METHODS(CString, FileHandlePtr);
CACHE_DEFINE_METHODS(CString, FileHandlePtr, CS_PTR, S_LEN, string_cmp);
typedef CACHE(CString, FileHandlePtr) FileCache;

static FileCache FILE_CACHE;

static void file_evict_callback(void* uring, CString* key, FileHandle** value) {
    assert(uring);
    assert(key);
    assert(key->ptr);
    assert(value);

    log_fmt(TRACE, "Evicting file " S_F ".", S_FA(*key));
    string_free((String*)key);
    file_close(*value, uring);
}

void file_cache_init() {
    CACHE_INIT(CString, FileHandlePtr)
    (&FILE_CACHE, FD_CACHE_SIZE, file_evict_callback);
}

FileHandle* file_open(struct io_uring* uring, CString path, int32_t flags) {
    assert(uring);
    assert(path.ptr);
    (void)uring;

    FileHandle** handle_ptr =
        CACHE_FIND(CString, FileHandlePtr)(&FILE_CACHE, path);
    if (handle_ptr && (*handle_ptr)->flags == flags) {
        log_fmt(TRACE, "File cache hit on " S_F ".", S_FA(path));
        (*handle_ptr)->open_count++;
        return *handle_ptr;
    }

    log_fmt(TRACE, "File cache miss on " S_F ".", S_FA(path));
    FileHandle* handle = NULL;
    UNWRAPN(handle, calloc(1, sizeof(FileHandle)));
    // TODO: Work out a good way to do this via the uring.
    handle->file = open(S_AS_C_STR(path), flags);
    if (handle->file < 0) {
        free(handle);
        log_fmt(WARN, "Unable to open file " S_F ": %s.", S_FA(path),
                strerror(errno));
        return NULL;
    }

    handle->path       = S_CONST(string_clone(path));
    handle->open_count = 2; // Being in the cache counts for a reference.
    CACHE_INSERT(CString, FileHandlePtr)
    (&FILE_CACHE, S_CONST(string_clone(path)), handle, uring);

    return handle;
}

FileHandle* file_openat(struct io_uring* uring, FileHandle* dir, CString name,
                        int32_t flags) {
    assert(uring);
    assert(dir);
    assert(name.ptr);
    (void)uring;

    String full_path = string_alloc(dir->path.len + name.len + 1);
    if (dir->path.ptr[dir->path.len - 1] == '/')
        string_concat(full_path, 2, dir->path, name);
    else
        string_concat(full_path, 3, dir->path, CS("/"), name);

    FileHandle** handle_ptr =
        CACHE_FIND(CString, FileHandlePtr)(&FILE_CACHE, S_CONST(full_path));
    if (handle_ptr && (*handle_ptr)->flags == flags) {
        log_fmt(TRACE, "File cache hit (openat) on " S_F ".", S_FA(full_path));
        (*handle_ptr)->open_count++;
        string_free(&full_path);
        return *handle_ptr;
    }

    log_fmt(TRACE, "File cache miss (openat) on " S_F ".", S_FA(full_path));
    FileHandle* handle = NULL;
    UNWRAPN(handle, calloc(1, sizeof(FileHandle)));
    handle->file = openat(dir->file, S_AS_C_STR(name), flags);
    if (handle->file < 0) {
        free(handle);
        log_fmt(WARN, "Unable to open file " S_F ": %s.", S_FA(full_path),
                strerror(errno));
        string_free(&full_path);
        return NULL;
    }

    handle->path       = S_CONST(full_path);
    handle->open_count = 2;
    CACHE_INSERT(CString, FileHandlePtr)
    (&FILE_CACHE, S_CONST(string_clone(handle->path)), handle, uring);

    return handle;
}

fd file_handle_fd(FileHandle* handle) {
    assert(handle);
    assert(handle->file >= 0);
    return handle->file;
}

void file_close(FileHandle* handle, struct io_uring* uring) {
    assert(handle);
    assert(handle->file >= 0);
    assert(handle->open_count);
    assert(uring);

    if (--handle->open_count)
        return; // Other users remain.

    event_close_submit(NULL, uring, handle->file, 0, EVENT_FALLBACK_ALLOW);
    string_free((String*)&handle->path);
    free(handle);
}

void file_cache_destroy(struct io_uring* uring) {
    assert(uring);

    CACHE_CLEAR(CString, FileHandlePtr)(&FILE_CACHE, uring);
}
