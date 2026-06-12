#!/bin/sh
# Verify the FUSE request sizes that reach the daemon. FUSE 3 builds
# negotiate 1 MiB requests (max_write/max_pages); FUSE 2 is limited to
# 128 KiB with big_writes.
. "${top_srcdir}/tests/lib/harness.sh"

# DEBUG3 logging prints "FUSE write '...' (offset=..., count=...)"
LTFS_MOUNT_OPTS="-o verbose=6"

ltfs_setup

dd if=/dev/zero of="$MNT/big" bs=1M count=8 conv=fsync status=none

# O_DIRECT reads bypass the readahead window, so the application's
# request size reaches the daemon (split at the negotiated maximum)
dd if="$MNT/big" of=/dev/null bs=1M iflag=direct status=none \
	|| skip "O_DIRECT reads not supported on this kernel"

ltfs_finish

max_req() {
	grep "FUSE $1" "$WORK/ltfs.log" | grep -o 'count=[0-9]*' | \
		cut -d= -f2 | sort -n | tail -1
}

write_max=$(max_req write)
read_max=$(max_req read)
echo "largest write request: ${write_max:-none}, largest read request: ${read_max:-none}"

[ -n "$write_max" ] || fail "no write requests logged"

if ltfs_is_fuse3; then
	[ "$write_max" -ge 524288 ] || fail "write requests capped at $write_max bytes"
	[ "$read_max" -ge 524288 ] || fail "read requests capped at $read_max bytes"
else
	# big_writes raises the FUSE 2 limit to 128 KiB
	[ "$write_max" -ge 65536 ] || fail "write requests capped at $write_max bytes"
fi

echo "PASS"
