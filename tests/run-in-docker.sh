#!/bin/sh
# Build LTFS and run the test suite inside a Linux container.
# FUSE mounts require /dev/fuse and CAP_SYS_ADMIN in the container.
#
# Usage: tests/run-in-docker.sh [configure-options...]
#   e.g. tests/run-in-docker.sh --enable-fast
# Environment:
#   LTFS_DOCKER_SHELL=1   drop into an interactive shell instead of building
#                         (the source tree is mounted read-only at /src)

set -eu

top_srcdir=$(cd "$(dirname "$0")/.." && pwd)
image=ltfs-test

docker build -q -t "$image" "$top_srcdir/tests/docker" >/dev/null

run_flags="--rm --device /dev/fuse --cap-add SYS_ADMIN --security-opt apparmor=unconfined"

if [ "${LTFS_DOCKER_SHELL:-0}" = "1" ]; then
    # shellcheck disable=SC2086
    exec docker run $run_flags -it -v "$top_srcdir:/src:ro" "$image" bash
fi

# Build from a copy of the source files so the host tree stays untouched.
# shellcheck disable=SC2086
cd "$top_srcdir" && git ls-files -z --cached --others --exclude-standard \
    | COPYFILE_DISABLE=1 tar --null -T - -cf - \
    | docker run $run_flags -i "$image" sh -ec '
        mkdir -p /build && cd /build && tar -xf -
        ./autogen.sh
        ./configure "$@"
        make -j"$(nproc)"
        make check VERBOSE=1
    ' sh "$@"
