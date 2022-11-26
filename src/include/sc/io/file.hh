/*
 * SHORT CIRCUIT: FILE — File descriptor type.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

namespace sc::io {

struct FileRef {
private:
    int m_fd;

    explicit FileRef(int fd);

public:
    friend struct File;

    operator int() const;
};

struct File {
private:
    int  m_fd;
    bool m_owned;

    File(int fd, bool owned);

public:
    ~File();

    static File owned(int fd);
    static File unowned(int fd);

    File(File const&) = delete;
    File& operator=(File const&) = delete;

    File(File&&) noexcept;
    File& operator=(File&&) noexcept;

    int  fd();
    void set_owned(bool);

    operator FileRef() const;
};

} // namespace sc::io
