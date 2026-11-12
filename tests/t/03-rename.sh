#!/bin/sh
# Rename files and directories, including overwriting an existing target.
. "${top_srcdir}/tests/lib/harness.sh"

ltfs_setup

echo one >"$MNT/a"
echo two >"$MNT/b"
mkdir "$MNT/dir1"
echo nested >"$MNT/dir1/inner"

# Simple rename
mv "$MNT/a" "$MNT/a2"
[ "$(cat "$MNT/a2")" = "one" ] || fail "simple rename content"
[ ! -e "$MNT/a" ] || fail "source still present after rename"

# Overwriting rename
mv "$MNT/a2" "$MNT/b"
[ "$(cat "$MNT/b")" = "one" ] || fail "overwriting rename content"

# Directory rename
mv "$MNT/dir1" "$MNT/dir2"
[ "$(cat "$MNT/dir2/inner")" = "nested" ] || fail "directory rename content"

# mv -n must not overwrite (rename(2) with RENAME_NOREPLACE on fuse3)
echo keep >"$MNT/target"
echo other >"$MNT/source"
mv -n "$MNT/source" "$MNT/target" 2>/dev/null || true
[ "$(cat "$MNT/target")" = "keep" ] || fail "mv -n overwrote existing target"

ltfs_remount
[ "$(cat "$MNT/b")" = "one" ] || fail "rename result lost after remount"
[ "$(cat "$MNT/dir2/inner")" = "nested" ] || fail "dir rename lost after remount"

ltfs_finish
echo "PASS"
