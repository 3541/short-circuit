/*
 * SHORT CIRCUIT: HTTP HEADERS -- HTTP header parsing and storage.
 *
 * Copyright (c) 2021, Alex O'Brien <3541ax@gmail.com>
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

#include "http/headers.h"

#include <a3/ht.h>
#include <a3/str.h>

A3_HT_DEFINE_STRUCTS(A3CString, A3CString);
A3_HT_DECLARE_METHODS(A3CString, A3CString);
A3_HT_DEFINE_METHODS(A3CString, A3CString, A3_CS_PTR, A3_S_LEN, a3_string_cmp);

struct HttpHeaders {
    A3_HT(A3CString, A3CString) headers;
};

bool http_header_add(HttpHeaders* headers, A3CString name, A3CString value) {
    assert(headers);
    assert(name.ptr);
    assert(value.ptr);

    if (A3_HT_FIND(A3CString, A3CString)(&headers->headers, name))
        return false;
    A3_HT_INSERT(A3CString, A3CString)
    (&headers->headers, A3_S_CONST(a3_string_clone(name)),
     A3_S_CONST(a3_string_clone(value)));
    return true;
}
