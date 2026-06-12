#!/bin/sh
# Truncate by path (shrink and extend) and verify content and size.
. "${top_srcdir}/tests/lib/harness.sh"

ltfs_setup

dd if=/dev/urandom of="$MNT/file" bs=1M count=2 status=none

truncate -s 1M "$MNT/file"
[ "$(stat -c %s "$MNT/file")" -eq 1048576 ] || fail "shrink truncate size"

truncate -s 3M "$MNT/file"
[ "$(stat -c %s "$MNT/file")" -eq 3145728 ] || fail "extend truncate size"

# Extended region must read back as zeros
tail -c 2097152 "$MNT/file" >"$WORK/tail"
head -c 2097152 /dev/zero >"$WORK/zeros"
cmp -s "$WORK/tail" "$WORK/zeros" || fail "extended region not zero-filled"

truncate -s 0 "$MNT/file"
[ "$(stat -c %s "$MNT/file")" -eq 0 ] || fail "truncate to zero"

ltfs_finish
echo "PASS"
