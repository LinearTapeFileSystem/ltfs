#!/bin/sh
# rename(2) flag semantics, ftruncate on an open fd, and inode stability.
. "${top_srcdir}/tests/lib/harness.sh"

HELPER="$top_builddir/tests/helpers/fsops_helper"

ltfs_setup

[ -x "$HELPER" ] || fail "fsops_helper not built"

echo one >"$MNT/a"
echo two >"$MNT/b"

# RENAME_NOREPLACE with an existing target must fail and leave it intact
"$HELPER" noreplace "$MNT/a" "$MNT/b" && fail "RENAME_NOREPLACE overwrote target"
[ "$(cat "$MNT/b")" = "two" ] || fail "target changed by failed RENAME_NOREPLACE"
[ "$(cat "$MNT/a")" = "one" ] || fail "source changed by failed RENAME_NOREPLACE"

# RENAME_EXCHANGE is not supported by LTFS; it must fail without touching data
"$HELPER" exchange "$MNT/a" "$MNT/b" && fail "RENAME_EXCHANGE unexpectedly succeeded"
[ "$(cat "$MNT/a")" = "one" ] || fail "source changed by failed RENAME_EXCHANGE"
[ "$(cat "$MNT/b")" = "two" ] || fail "target changed by failed RENAME_EXCHANGE"

if ltfs_is_fuse3; then
	# With a free target, RENAME_NOREPLACE must succeed (fuse2 cannot
	# express rename flags at all, so this branch is fuse3-only)
	"$HELPER" noreplace "$MNT/a" "$MNT/c" || fail "RENAME_NOREPLACE to free target failed"
	[ "$(cat "$MNT/c")" = "one" ] || fail "content lost by RENAME_NOREPLACE"
	[ ! -e "$MNT/a" ] || fail "source still present after RENAME_NOREPLACE"
	mv "$MNT/c" "$MNT/a"
fi

# ftruncate through an open descriptor
dd if=/dev/urandom of="$MNT/file" bs=1M count=1 status=none
out=$("$HELPER" ftruncate "$MNT/file" 12345) || fail "ftruncate failed: $out"
[ "$out" = "12345" ] || fail "ftruncate reported size $out"
[ "$(stat -c %s "$MNT/file")" -eq 12345 ] || fail "size after ftruncate"

# Inode numbers come from the LTFS index (use_ino) and must survive a remount
ino_before=$(stat -c %i "$MNT/file")
ltfs_remount
ino_after=$(stat -c %i "$MNT/file")
[ "$ino_before" = "$ino_after" ] || fail "inode changed across remount ($ino_before -> $ino_after)"

ltfs_finish
echo "PASS"
