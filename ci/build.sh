#!/usr/bin/env sh
#
# This script is intended to be called from a CI pipeline (either directly or from all.sh). It
# assumes the library has already been configured in the given directory, and then builds it.

. ci/common.sh

dir="$1"

if [ ! -d "$dir" ]; then
    echo "Build directory $dir does not exist." >&2
    exit 1
fi

meson compile -C "$dir"
