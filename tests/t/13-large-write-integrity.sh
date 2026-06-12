#!/bin/sh
# Data integrity for single write() calls larger than the tape block size.
#
# A single large write exercises the I/O scheduler's request-splitting: the
# write must be broken into block-sized cache entries without dropping the
# tail. dd with a large bs issues one write() syscall per block, so with the
# FUSE 3 default max_write of 1 MiB (> the 512 KiB tape block) the daemon
# receives multi-block writes in a single request. Content is random and
# verified byte-for-byte, before and after a remount.
. "${top_srcdir}/tests/lib/harness.sh"

ltfs_setup

# One write() each, all larger than the block size, at offset 0 and beyond
for mb in 1 4 8; do
	dd if=/dev/urandom of="$WORK/src$mb" bs=${mb}M count=1 status=none
	dd if="$WORK/src$mb" of="$MNT/f$mb" bs=${mb}M count=1 conv=fsync status=none
	cmp -s "$WORK/src$mb" "$MNT/f$mb" || fail "${mb} MiB single write corrupted while mounted"
done

# Overwrite the second half of the 4 MiB file with one 2 MiB write at a
# block-aligned non-zero offset (exercises the insert-into-list path).
dd if=/dev/urandom of="$WORK/mid" bs=2M count=1 status=none
dd if="$WORK/mid" of="$MNT/f4" bs=2M count=1 seek=1 conv=fsync,notrunc status=none
head -c 2097152 "$WORK/src4" >"$WORK/f4.expect"
cat "$WORK/mid" >>"$WORK/f4.expect"
cmp -s "$WORK/f4.expect" "$MNT/f4" || fail "large write at non-zero offset corrupted"

ltfs_remount

cmp -s "$WORK/src8" "$MNT/f8" || fail "8 MiB write corrupted after remount"
cmp -s "$WORK/f4.expect" "$MNT/f4" || fail "offset write corrupted after remount"

ltfs_finish
echo "PASS"
