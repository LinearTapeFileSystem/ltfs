#!/bin/sh
# Format, mount, unmount, fsck: the volume must be consistent and empty.
. "${top_srcdir}/tests/lib/harness.sh"

ltfs_setup

mountpoint -q "$MNT" || fail "mountpoint not active"
[ -z "$(ls -A "$MNT")" ] || fail "freshly formatted volume is not empty"

df -P "$MNT" | grep -q "$MNT" || fail "statfs does not report the volume"

ltfs_finish
echo "PASS"
