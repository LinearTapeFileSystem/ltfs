#!/bin/sh
# Symlink creation, readlink, and persistence.
. "${top_srcdir}/tests/lib/harness.sh"

ltfs_setup

echo target-content >"$MNT/target"
ln -s target "$MNT/link"

[ -L "$MNT/link" ] || fail "symlink not created"
[ "$(readlink "$MNT/link")" = "target" ] || fail "readlink value"
[ "$(cat "$MNT/link")" = "target-content" ] || fail "read through symlink"

ln -s /nonexistent/absolute "$MNT/dangling"
[ "$(readlink "$MNT/dangling")" = "/nonexistent/absolute" ] || fail "dangling symlink"

ltfs_remount
[ "$(readlink "$MNT/link")" = "target" ] || fail "symlink lost after remount"

ltfs_finish
echo "PASS"
