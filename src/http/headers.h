#pragma once

#include <a3/ht.h>
#include <a3/str.h>

HT_DEFINE_STRUCTS(CString, CString);

typedef struct HttpHeaders {
    HT(CString, CString) headers;
} HttpHeaders;
