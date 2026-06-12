#!/bin/sh
# Build LTFS and run the test suite inside a Linux container.
# FUSE mounts require /dev/fuse and CAP_SYS_ADMIN in the container.
#
# Usage: tests/run-in-docker.sh [configure-options...]
#   e.g. tests/run-in-docker.sh --with-fuse2
# Environment:
#   LTFS_DOCKER_SHELL=1   drop into an interactive shell instead of building

set -eu

top_srcdir=$(cd "$(dirname "$0")/.." && pwd)
image=ltfs-test

docker build -q -t "$image" "$top_srcdir/tests/docker" >/dev/null

run_flags="--rm --device /dev/fuse --cap-add SYS_ADMIN --security-opt apparmor:unconfined"

if [ "${LTFS_DOCKER_SHELL:-0}" = "1" ]; then
    # shellcheck disable=SC2086
    exec docker run $run_flags -it -v "$top_srcdir:/ltfs" "$image" bash
fi

# shellcheck disable=SC2086
exec docker run $run_flags -v "$top_srcdir:/ltfs" "$image" sh -ec "
    ./autogen.sh
    ./configure --enable-icu-6x $*
    make -j\$(nproc)
    make check VERBOSE=1
"
