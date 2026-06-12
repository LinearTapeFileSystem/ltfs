#!/bin/sh
# mkdir/rmdir/unlink semantics, including unlink of an open file.
. "${top_srcdir}/tests/lib/harness.sh"

ltfs_setup

mkdir -p "$MNT/d1/d2/d3"
[ -d "$MNT/d1/d2/d3" ] || fail "nested mkdir"

echo data >"$MNT/d1/f"
rmdir "$MNT/d1" 2>/dev/null && fail "rmdir of non-empty directory succeeded"

rm "$MNT/d1/f"
[ ! -e "$MNT/d1/f" ] || fail "unlink"

rmdir "$MNT/d1/d2/d3" "$MNT/d1/d2" "$MNT/d1"
[ ! -e "$MNT/d1" ] || fail "rmdir chain"

# Unlink while open: file content must stay readable through the open fd,
# and no .fuse_hidden* litter may remain (hard_remove semantics).
echo openme >"$MNT/openfile"
exec 3<"$MNT/openfile"
rm "$MNT/openfile"
[ ! -e "$MNT/openfile" ] || fail "unlink of open file did not remove the name"
ls -a "$MNT" | grep -q '\.fuse_hidden' && fail ".fuse_hidden file left behind"
exec 3<&-

ltfs_finish
echo "PASS"
