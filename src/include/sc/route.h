/*
 * SHORT CIRCUIT: ROUTE -- Request routing.
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

#include <sc/forward.h>

A3_H_BEGIN

typedef union ScRouteData {
    void* ptr;
    ScFd  fd;
} ScRouteData;

typedef void (*ScRouteHandler)(void* ctx, ScRouteData data);

ScRouter* sc_router_new(ScRouteHandler, ScRouteData);
void      sc_router_free(ScRouter*);
void      sc_router_dispatch(ScRouter*, void* ctx);

A3_H_END
