#pragma once

#include <stdint.h>

#include <a3/str.h>

#include "forward.h"

void file_cache_init(void);
fd file_open(CString path, int32_t flags);
void file_close(fd);
