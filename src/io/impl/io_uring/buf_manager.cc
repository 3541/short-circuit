/*
 * URING BUFFER MANAGER -- io_uring buffer ring management.
 *
 * Copyright (c) 2023, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <utility>

#include <liburing.h>

#include <a3/log.h>
#include <a3/util.h>

module sc.io.impl.uring.buf;

import sc.config;
import sc.io.alloc;
import sc.io.buf;
import sc.lib.cast;

namespace sc::io::impl::uring {

BufManager::BufManager(io_uring& uring) : m_uring{uring} {}

BufManager::~BufManager() {
    for (std::size_t i = 0; i < m_rings.size(); ++i) {
        ::io_uring_free_buf_ring(&m_uring, &m_rings[i].get(), config::IO_BUFFER_GROUP_SIZE,
                                 static_cast<int>(i));
    }
}

::io_uring_buf_ring& BufManager::ring(Allocator::Group group) {
    for (std::size_t i = m_rings.size(); i <= std::to_underlying(group); ++i) {
        int   status;
        auto* ring = ::io_uring_setup_buf_ring(&m_uring, config::IO_BUFFER_GROUP_SIZE,
                                               static_cast<int>(i), 0, &status);

        if (!ring) {
            A3_ERRNO(-status, "Failed to allocate buffer ring");
            A3_PANIC("Allocating buffer ring failed.");
        }

        m_rings.push_back(*ring);
    }

    return m_rings[std::to_underlying(group)];
}

void BufManager::submit(Buf buf) {
    auto [id, data] = std::move(buf).release();
    auto& r         = ring(Allocator::group(id));

    ::io_uring_buf_ring_add(&r, data.data(), lib::narrow_cast<unsigned>(data.size()),
                            std::to_underlying(id),
                            ::io_uring_buf_ring_mask(config::IO_BUFFER_GROUP_SIZE), 0);
    ::io_uring_buf_ring_advance(&r, 1);
}

} // namespace sc::io::impl::uring
