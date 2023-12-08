/*
 * OPTION -- Utility macros for std::optional.
 *
 * Copyright (c) 2023, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <optional>

#define SC_OPTION_IF(C, E) ((C) ? std::optional{E} : std::nullopt)
