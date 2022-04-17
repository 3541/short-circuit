/*
 * SHORT CIRCUIT: FORWARD -- Forward declarations.
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
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

#include <a3/cpp.h>

A3_H_BEGIN

typedef int ScFd;

typedef struct ScEventLoop ScEventLoop;

typedef struct ScCoroutine ScCoroutine;
typedef struct ScCoCtx     ScCoCtx;
typedef struct ScCoMain    ScCoMain;

typedef struct ScListener       ScListener;
typedef struct ScConnection     ScConnection;
typedef struct ScHttpConnection ScHttpConnection;
typedef struct ScHttpRequest    ScHttpRequest;
typedef struct ScHttpResponse   ScHttpResponse;
typedef struct ScRouter         ScRouter;

A3_H_END
