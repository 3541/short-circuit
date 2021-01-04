# Short Circuit
A lightweight and performant web server for Linux, built on top of io_uring.
Capable of ~~60,000~~ ~~80,000~~ ~~120,000~~ 135,000 requests per second on
static files.

## Building
Dependencies:
* A C compiler supporting C11.
* A C++ compiler supporting C++11.
* CMake 3.9 or later.

To build, create a build directory and change into it. Then run `cmake ..
[-DCMAKE_BUILD_TYPE=___]` to set up the build system, and `cmake --build .` to
build the project. This produces a binary `sc`, which can be run directly. By
default, the server listens on port `8000`.

Note: on most Linux distributions, the queue may fail to open due to the locked
memory limit. See [here](#queue-size) for more information.

## Notes and disclaimer
This is _very_ new software, which is nowhere near feature complete, let alone
stable. There are critical bugs, known and unknown. At this time it should not
under any circumstances be used in production or for anything which matters even
a little bit.

### Queue size
On most distributions, the locked memory limit is too low to open an `io_uring`
queue of the size that Short Circuit does by default. This can be fixed either
(preferably) by increasing this limit (usually in `/etc/security/limits.conf`),
or by lowering `URING_ENTRIES` in `config.h`. It probably needs to be at least
halved to work with the default limit.

### Open file limit
To serve static files, Short Circuit uses `IO_URING_OP_PIPE`. For performance
purposes, these pipes are cached across requests. As a result, under high load,
many systems will hit the open file limit. This can be fixed by raising the open
file limit, or by decreasing `CONNECTION_POOL_SIZE` in `config.h`. The former
can be done with `ulimit -n [new value]` (since the soft limit is often
significantly lower than the hard limit), or in `/etc/security/limits.conf` for
a permanent change.
