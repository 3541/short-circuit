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

#include "route.h"

#include <assert.h>

#include <a3/util.h>

#include <sc/route.h>

ScRouter* sc_router_new(ScRouteHandler handler, ScRouteData data) {
    A3_UNWRAPNI(ScRouter*, ret, calloc(1, sizeof(*ret)));
    *ret = (ScRouter) { .handler = handler, .data = data };
    return ret;
}

void sc_router_free(ScRouter* router) {
    assert(router);

    free(router);
}

void sc_router_dispatch(ScRouter* router, void* ctx) {
    assert(router);
    assert(router->handler);

    router->handler(ctx, router->data);
}
