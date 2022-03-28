# Short Circuit

[![.github/workflows/build.yml](https://github.com/3541/short-circuit/actions/workflows/build.yml/badge.svg)](https://github.com/3541/short-circuit/actions/workflows/build.yml)

A lightweight and performant web server for Linux, built on top of io_uring. Capable of ~~60,000~~
~~80,000~~ ~~120,000~~ ~~135,000 requests per second on static files~~. Suffice it to say, it is
fast, and getting faster. Actual, more rigorous benchmarks will come once the project gets closer to
completion.

[io_uring](https://kernel.dk/io_uring.pdf?source=techstories.org) is a new asynchronous I/O system
on Linux which offers a significant performance advantage (and, subjectively, a much nicer
interface) over its competitors (POSIX `aio`, `epoll`, etc...).

## Building
Dependencies:
* A C compiler supporting C11.
* Meson 0.55 or later.
* [liburing](https://github.com/axboe/liburing).
* Linux 5.7 or later.

To build, first ensure all submodules have been downloaded (`git submodule update --init
--recursive`), and then run `meson setup <BUILDDIR>` to configure the build system in `BUILDDIR`.
Alternatively, a script to generate various build configurations is provided (`./configure`).

Some versions of Meson (before 0.56) may refuse to pull in transitive dependencies, and produce error messages of
the form `WARNING: Dependency highwayhash not found but it is available in a sub-subproject.`. If
this occurs, simply run the command `meson wrap promote
subprojects/a3/subprojects/highwayhash.wrap`.

After, run `meson compile -C <BUILDDIR>` to build the project. This produces a binary `sc`, which
can be run directly. By default, the server listens on port `8000`. `sc --help` will show the
available options and parameters.

Note: on most Linux distributions, you may see warnings about the locked memory and open file
resource limits. See [here](#queue-size) for more information.

## Notes and disclaimer
This is _very_ new software, which is nowhere near feature complete, let alone stable. There are
critical bugs, known and unknown. At this time it should not under any circumstances be used in
production or for anything which matters even a little bit.

### Queue size
On most distributions, the locked memory limit is too low to open an `io_uring` queue of the size
that Short Circuit does by default. This can be fixed either (preferably) by increasing this limit
(usually in `/etc/security/limits.conf`), or by lowering `URING_ENTRIES` in `config.h`. It probably
needs to be at least halved to work with the default limit.

### Open file limit
To serve static files, Short Circuit uses `IO_URING_OP_PIPE`. For performance purposes, these pipes
are cached across requests. As a result, under high load (particularly when it is generated by many
simultaneous connections), many systems will hit the open file limit. This can be fixed by raising
the open file limit, or by decreasing `CONNECTION_POOL_SIZE` in `config.h`. The former can be done
in `/etc/security/limits.conf`. A good number is a bit over three times the maximum expected number
of concurrent connections, since each connection requires an open file for the socket and two for
the pipe.

### `ulimit`s
Both of these only require that the hard limit be changed, as Short Circuit will automatically raise
its own soft limits at runtime.

## Licensing

Short Circuit is licensed under the GNU Affero GPL, the terms of which are described
[here](https://github.com/3541/short-circuit/blob/trunk/LICENSE).

Short Circuit depends on the following other projects:

### liba3
[liba3](https://github.com/3541/liba3) is licensed under the [3-clause BSD
license](https://github.com/3541/liba3/blob/trunk/LICENSE).

`liba3` also links with and otherwise uses other software projects, as detailed
[here](https://github.com/3541/liba3/blob/trunk/README.md#licensing).

### liburing
[liburing](https://github.com/axboe/liburing) is licensed under the [MIT
license](https://github.com/axboe/liburing/blob/master/LICENSE).
