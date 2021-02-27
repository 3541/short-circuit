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
#include "file_handle.h"

#include <assert.h>
#include <fcntl.h>
#include <liburing.h>
#include <linux/stat.h>
#include <stdint.h>
#include <stdlib.h>

#include <a3/cache.h>
#include <a3/log.h>
#include <a3/sll.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "event.h"
#include "event/handle.h"
#include "forward.h"

#define FILE_HANDLE_WAITING (-4242)

typedef FileHandle* FileHandlePtr;

A3_CACHE_DEFINE_STRUCTS(A3CString, FileHandlePtr);
A3_CACHE_DECLARE_METHODS(A3CString, FileHandlePtr);
A3_CACHE_DEFINE_METHODS(A3CString, FileHandlePtr, A3_CS_PTR, A3_S_LEN, a3_string_cmp);
typedef A3_CACHE(A3CString, FileHandlePtr) FileCache;

static FileCache FILE_CACHE;

static void file_evict_callback(void* uring, A3CString* key, FileHandle** value) {
    assert(uring);
    assert(key);
    assert(key->ptr);
    assert(value);

    a3_log_fmt(LOG_TRACE, "Evicting file " A3_S_F ".", A3_S_FA(*key));
    file_handle_close(*value, uring);
}

void file_cache_init() {
    A3_CACHE_INIT(A3CString, FileHandlePtr)
    (&FILE_CACHE, FD_CACHE_SIZE, file_evict_callback);
}

static void file_handle_wait(EventTarget* target, FileHandle* handle) {
    assert(target);
    assert(handle);

    A3_REF(handle);
    Event* event = event_create(target, EVENT_OPENAT_SYNTH);
    A3_SLL_PUSH(Event)(&handle->waiting, event);
}

static EventTarget* file_handle_target(FileHandle* handle) {
    assert(handle);

    A3_REF(handle);
    return EVT(handle);
}

FileHandle* file_open(EventTarget* target, struct io_uring* uring, A3CString path, int32_t flags) {
    return file_openat(target, uring, NULL, path, flags);
}

FileHandle* file_openat(EventTarget* target, struct io_uring* uring, FileHandle* dir,
                        A3CString name, int32_t flags) {
    assert(target);
    assert(uring);
    assert(name.ptr);
    assert(flags == O_RDONLY);

    A3String path = A3_S_NULL;

    if (dir) {
        path = a3_string_alloc(dir->path.len + name.len + 1);
        if (dir->path.ptr[dir->path.len - 1] == '/')
            a3_string_concat(path, 2, dir->path, name);
        else
            a3_string_concat(path, 3, dir->path, A3_CS("/"), name);
    } else {
        path = a3_string_clone(name);
    }

    FileHandle** handle_ptr =
        A3_CACHE_FIND(A3CString, FileHandlePtr)(&FILE_CACHE, A3_S_CONST(path));
    if (handle_ptr && (*handle_ptr)->flags == flags) {
        FileHandle* handle = *handle_ptr;

        a3_log_fmt(LOG_TRACE, "File cache hit (openat) on " A3_S_F ".", A3_S_FA(path));
        a3_string_free(&path);

        // The handle is not ready, but an open request is in flight. Synthesize
        // an event so the caller is notified when the file is opened.
        if (file_handle_waiting(handle)) {
            a3_log_msg(LOG_TRACE, "  Open in-flight. Waiting.");
            file_handle_wait(target, handle);
            return handle;
        }

        A3_REF(handle);
        return handle;
    }

    a3_log_fmt(LOG_TRACE, "File cache miss (openat) on " A3_S_F ".", A3_S_FA(path));
    FileHandle* handle = NULL;
    A3_UNWRAPN(handle, calloc(1, sizeof(FileHandle)));
    A3_REF_INIT(handle);
    handle->path = A3_S_CONST(path);
    handle->file = FILE_HANDLE_WAITING;

    if (!event_stat_submit(file_handle_target(handle), uring, handle->path, FILE_STATX_MASK,
                           &handle->stat, IOSQE_IO_LINK) ||
        !event_openat_submit(file_handle_target(handle), uring, dir ? file_handle_fd(dir) : -1,
                             handle->path, flags, 0)) {
        a3_log_msg(LOG_WARN, "Unable to submit OPENAT event.");
        a3_string_free(&path);
        free(handle);
        return NULL;
    }

    file_handle_wait(target, handle);
    A3_CACHE_INSERT(A3CString, FileHandlePtr)
    (&FILE_CACHE, handle->path, handle, uring);

    return handle;
}

fd file_handle_fd(FileHandle* handle) {
    assert(handle);
    assert(handle->file >= 0);
    return handle->file;
}

fd file_handle_fd_unchecked(FileHandle* handle) { return handle->file; }

struct statx* file_handle_stat(FileHandle* handle) {
    assert(handle);
    return &handle->stat;
}

A3CString file_handle_path(FileHandle* handle) {
    assert(handle);
    return handle->path;
}

bool file_handle_waiting(FileHandle* handle) {
    assert(handle);

    return handle->file == FILE_HANDLE_WAITING;
}

// Returns true if the handle was freed.
bool file_handle_close(FileHandle* handle, struct io_uring* uring) {
    assert(handle);
    assert(A3_REF_COUNT(handle));
    assert(uring);

    A3_UNREF(handle);
    if (A3_REF_COUNT(handle))
        return false; // Other users remain.

    if (handle->file >= 0)
        event_close_submit(NULL, uring, handle->file, 0, EVENT_FALLBACK_ALLOW);
    a3_string_free((A3String*)&handle->path);
    free(handle);
    return true;
}

void file_cache_destroy(struct io_uring* uring) {
    assert(uring);

    A3_CACHE_CLEAR(A3CString, FileHandlePtr)(&FILE_CACHE, uring);
}

void file_handle_event_handle(FileHandle* handle, struct io_uring* uring, int32_t status,
                              uint32_t flags) {
    assert(handle);
    assert(file_handle_waiting(handle));
    assert(uring);

    // Unref.
    if (file_handle_close(handle, uring))
        return;

    if (flags & EVENT_FLAG_CHAIN)
        return;

    handle->file = status;

    event_synth_deliver(&handle->waiting, uring, status);
}
