# Short Circuit
A lightweight and performant web server for Linux, built on top of io_uring.

## Building
Dependencies:
* A C compiler supporting C11.
* A C++ compiler supporting C++11.
* CMake 3.9 or later.

To build, create a build directory and change into it. Then run `cmake ..
[-DCMAKE_BUILD_TYPE=___]` to set up the build system, and `cmake --build .` to
build the project. This produces a binary `sc`, which can be run directly. By
default, the server listens on port `8000`.

## Notes and disclaimer
This is _very_ new software, which is nowhere near feature complete, let alone
stable. There are critical bugs, known and unknown. At this time it should not
under any circumstances be used in production or for anything which matters even
a little bit.
