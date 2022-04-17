/*
 * SHORT CIRCUIT: O_PATH SHIM -- Cross-platform definitions for O_PATH.
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU Affero General Public License as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#ifdef SC_HAVE_O_PATH
#define SC_O_PATH O_PATH
#elif defined(SC_HAVE_O_SEARCH)
#define SC_O_PATH O_SEARCH
#else
#define SC_O_PATH 0
#endif
