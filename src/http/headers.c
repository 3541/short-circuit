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

HT_DEFINE_STRUCTS(CString, CString);
HT_DECLARE_METHODS(CString, CString);
HT_DEFINE_METHODS(CString, CString, CS_PTR, S_LEN, string_cmp);

struct HttpHeaders {
    HT(CString, CString) headers;
};

bool http_header_add(HttpHeaders* headers, CString name, CString value) {
    assert(headers);
    assert(name.ptr);
    assert(value.ptr);

    if (HT_FIND(CString, CString)(&headers->headers, name))
        return false;
    HT_INSERT(CString, CString)
    (&headers->headers, S_CONST(string_clone(name)),
     S_CONST(string_clone(value)));
    return true;
}
