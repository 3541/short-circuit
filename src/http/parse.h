/*
 * SHORT CIRCUIT: HTTP PARSE -- Incremental HTTP request parser.
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

#pragma once

#include "forward.h"
#include "http/types.h"

HttpRequestStateResult http_request_first_line_parse(HttpConnection*,
                                                     struct io_uring*);
HttpRequestStateResult http_request_headers_parse(HttpConnection*,
                                                  struct io_uring*);
