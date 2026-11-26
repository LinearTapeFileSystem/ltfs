#!/bin/sh
# Truncate by path (shrink and extend) and verify content and size.
. "${top_srcdir}/tests/lib/harness.sh"

ltfs_setup

dd if=/dev/urandom of="$WORK/source" bs=1M count=2 status=none
head -c 1048576 "$WORK/source" >"$WORK/head"
cp "$WORK/source" "$MNT/file"

truncate -s 1M "$MNT/file"
[ "$(stat -c %s "$MNT/file")" -eq 1048576 ] || fail "shrink truncate size"
cmp -s "$WORK/head" "$MNT/file" || fail "content changed by shrink truncate"

truncate -s 3M "$MNT/file"
[ "$(stat -c %s "$MNT/file")" -eq 3145728 ] || fail "extend truncate size"
head -c 1048576 "$MNT/file" | cmp -s "$WORK/head" - || fail "content changed by extend truncate"

# Extended region must read back as zeros
tail -c 2097152 "$MNT/file" >"$WORK/tail"
head -c 2097152 /dev/zero >"$WORK/zeros"
cmp -s "$WORK/tail" "$WORK/zeros" || fail "extended region not zero-filled"

truncate -s 0 "$MNT/file"
[ "$(stat -c %s "$MNT/file")" -eq 0 ] || fail "truncate to zero"

ltfs_finish
echo "PASS"
