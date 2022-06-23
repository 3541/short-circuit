#!/usr/bin/env sh
#
# This script is intended to be called from a CI pipeline. It builds the library with
# maximally-aggressive warnings and runtime instrumentation, then runs the tests.

dir=$(mktemp -d)

. ci/common.sh

cleanup() {
    if [ -z "$dir" ]; then
        return
    fi

    rm -rf "$dir"
}
trap cleanup EXIT

ci/configure.sh "$dir" "$@"
ci/build.sh "$dir"
ci/test.sh "$dir"
