#!/bin/sh
# -o direct_io: data integrity without the page cache, large requests
# even for buffered application I/O, and graceful mmap failure.
. "${top_srcdir}/tests/lib/harness.sh"

HELPER="$top_builddir/tests/helpers/fsops_helper"

LTFS_MOUNT_OPTS="-o direct_io -o verbose=6"

ltfs_setup

# Data integrity through the direct path, including odd sizes
dd if=/dev/urandom of="$WORK/data" bs=37k count=9 status=none
cp "$WORK/data" "$MNT/data"
cmp -s "$WORK/data" "$MNT/data" || fail "data mismatch while mounted"

# mmap is not available on direct-I/O files; it must fail cleanly
out=$("$HELPER" mmap "$MNT/data" 4096) && fail "mmap unexpectedly succeeded"
echo "mmap failed as expected: $out"

# Buffered writes from the application reach the daemon at the
# application's block size (no page-cache splitting)
dd if=/dev/zero of="$MNT/big" bs=1M count=4 status=none

ltfs_remount
cmp -s "$WORK/data" "$MNT/data" || fail "data mismatch after remount"

ltfs_finish

if ltfs_is_fuse3; then
	write_max=$(grep "FUSE write" "$WORK/ltfs.log" | grep -o 'count=[0-9]*' | \
		cut -d= -f2 | sort -n | tail -1)
	echo "largest write request: $write_max"
	[ "$write_max" -ge 524288 ] || fail "direct writes capped at $write_max bytes"
fi

echo "PASS"
