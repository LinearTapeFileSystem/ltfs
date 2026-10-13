#!/bin/sh
# Write files of various sizes, verify contents before and after remount.
. "${top_srcdir}/tests/lib/harness.sh"

ltfs_setup

mkdir "$MNT/data"

# Small, block-sized, and multi-megabyte files
dd if=/dev/urandom of="$WORK/small" bs=1234 count=1 status=none
dd if=/dev/urandom of="$WORK/medium" bs=512K count=1 status=none
dd if=/dev/urandom of="$WORK/large" bs=1M count=8 status=none

# Expected contents after the overwrite and the append below
cp "$WORK/small" "$WORK/small.expect"
printf 'rewrite' | dd of="$WORK/small.expect" bs=1 seek=10 conv=notrunc status=none
cat "$WORK/medium" "$WORK/small" >"$WORK/medium.expect"

for f in small medium large; do
	cp "$WORK/$f" "$MNT/data/$f"
done

( cd "$WORK" && sha256sum small medium large >"$WORK/sums" )
( cd "$MNT/data" && sha256sum -c "$WORK/sums" >/dev/null ) \
	|| fail "checksum mismatch while mounted"

# Overwrite in place and append
printf 'rewrite' | dd of="$MNT/data/small" bs=1 seek=10 conv=notrunc status=none
cat "$WORK/small" >>"$MNT/data/medium"

ltfs_remount

cmp -s "$WORK/small.expect" "$MNT/data/small" || fail "small differs after remount"
cmp -s "$WORK/medium.expect" "$MNT/data/medium" || fail "medium differs after remount"
cmp -s "$WORK/large" "$MNT/data/large" || fail "large differs after remount"

ltfs_finish
echo "PASS"
