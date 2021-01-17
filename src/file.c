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
    file_close(*value, uring);
}

void file_cache_init() {
    CACHE_INIT(CString, FileHandlePtr)
    (&FILE_CACHE, FD_CACHE_SIZE, file_evict_callback);
}

static void file_handle_wait(EventTarget* target, FileHandle* handle) {
    assert(target);
    assert(handle);

    Event* event = event_create(target, EVENT_OPENAT_SYNTH);
    SLL_PUSH(Event)(&handle->waiting, event);
}

FileHandle* file_open(EventTarget* target, struct io_uring* uring, CString path,
                      int32_t flags) {
    assert(target);
    assert(uring);
    assert(path.ptr);
    assert(flags == O_RDONLY);

    FileHandle** handle_ptr =
        CACHE_FIND(CString, FileHandlePtr)(&FILE_CACHE, path);
    if (handle_ptr && (*handle_ptr)->flags == flags) {
        FileHandle* handle = *handle_ptr;

        log_fmt(TRACE, "File cache hit on " S_F ".", S_FA(path));
        handle->open_count++;

        // The handle is not ready, but an open request is in flight. Synthesize
        // an event so the caller is notified when the file is opened.
        if (file_handle_waiting(handle)) {
            log_msg(TRACE, "  Open in-flight. Waiting.");
            file_handle_wait(target, handle);
        }

        return handle;
    }

    log_fmt(TRACE, "File cache miss on " S_F ".", S_FA(path));
    FileHandle* handle = NULL;
    UNWRAPN(handle, calloc(1, sizeof(FileHandle)));
    handle->path       = S_CONST(string_clone(path));
    handle->open_count = 2;
    handle->file       = FILE_HANDLE_WAITING;

    if (!event_openat_submit(EVT(handle), uring, -1, handle->path, flags, 0)) {
        log_msg(WARN, "Unable to submit OPENAT event.");
        string_free((String*)&handle->path);
        free(handle);
        return NULL;
    }

    file_handle_wait(target, handle);
    CACHE_INSERT(CString, FileHandlePtr)
    (&FILE_CACHE, handle->path, handle, uring);

    return handle;
}

FileHandle* file_openat(EventTarget* target, struct io_uring* uring,
                        FileHandle* dir, CString name, int32_t flags) {
    assert(target);
    assert(uring);
    assert(dir);
    assert(name.ptr);
    assert(flags == O_RDONLY);

    String full_path = string_alloc(dir->path.len + name.len + 1);
    if (dir->path.ptr[dir->path.len - 1] == '/')
        string_concat(full_path, 2, dir->path, name);
    else
        string_concat(full_path, 3, dir->path, CS("/"), name);

    FileHandle** handle_ptr =
        CACHE_FIND(CString, FileHandlePtr)(&FILE_CACHE, S_CONST(full_path));
    if (handle_ptr && (*handle_ptr)->flags == flags) {
        FileHandle* handle = *handle_ptr;

        log_fmt(TRACE, "File cache hit (openat) on " S_F ".", S_FA(full_path));
        handle->open_count++;
        string_free(&full_path);

        file_handle_wait(target, handle);

        return handle;
    }

    log_fmt(TRACE, "File cache miss (openat) on " S_F ".", S_FA(full_path));
    FileHandle* handle = NULL;
    UNWRAPN(handle, calloc(1, sizeof(FileHandle)));
    handle->path       = S_CONST(full_path);
    handle->open_count = 2;
    handle->file       = FILE_HANDLE_WAITING;

    if (!event_openat_submit(EVT(handle), uring, file_handle_fd(dir),
                             handle->path, flags, 0)) {
        log_msg(WARN, "Unable to submit OPENAT event.");
        string_free(&full_path);
        free(handle);
        return NULL;
    }

    file_handle_wait(target, handle);
    CACHE_INSERT(CString, FileHandlePtr)
    (&FILE_CACHE, handle->path, handle, uring);

    return handle;
}

fd file_handle_fd(FileHandle* handle) {
    assert(handle);
    assert(handle->file >= 0);
    return handle->file;
}

fd file_handle_fd_unchecked(FileHandle* handle) { return handle->file; }

bool file_handle_waiting(FileHandle* handle) {
    assert(handle);

    return handle->file == FILE_HANDLE_WAITING;
}

void file_close(FileHandle* handle, struct io_uring* uring) {
    assert(handle);
    assert(handle->open_count);
    assert(uring);

    if (--handle->open_count)
        return; // Other users remain.

    if (handle->file >= 0)
        event_close_submit(NULL, uring, handle->file, 0, EVENT_FALLBACK_ALLOW);
    string_free((String*)&handle->path);
    free(handle);
}

void file_cache_destroy(struct io_uring* uring) {
    assert(uring);

    CACHE_CLEAR(CString, FileHandlePtr)(&FILE_CACHE, uring);
}

void file_handle_event_handle(FileHandle* handle, struct io_uring* uring,
                              int32_t status) {
    assert(handle);
    assert(file_handle_waiting(handle));
    assert(uring);

    handle->file = status;

    event_queue_handle_all(&handle->waiting, uring);
}
