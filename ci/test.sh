#!/usr/bin/env sh
#
# This script is intended to be called from a CI pipeline (either directly or from all.sh). It
# assumes the library has already been built in the given directory, and runs the tests.

. ci/common.sh

dir="$1"

if [ ! -d "$dir" ]; then
    echo "Build directory $dir does not exist." >&2
    exit 1
fi

meson_wrap=""
if [ "$meson_san" = "-Db_sanitize=none" ] && command -v valgrind > /dev/null 2>&1; then
    meson_wrap="--wrapper=valgrind"
fi

meson test -C "$dir" "$meson_wrap"
