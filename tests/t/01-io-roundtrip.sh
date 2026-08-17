#!/bin/sh
# Write files of various sizes, verify checksums before and after remount.
. "${top_srcdir}/tests/lib/harness.sh"

ltfs_setup

mkdir "$MNT/data"

# Small, block-sized, and multi-megabyte files
dd if=/dev/urandom of="$WORK/small" bs=1234 count=1 status=none
dd if=/dev/urandom of="$WORK/medium" bs=512K count=1 status=none
dd if=/dev/urandom of="$WORK/large" bs=1M count=8 status=none

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

( cd "$MNT/data" && sha256sum large ) | (cd "$WORK" && sha256sum -c >/dev/null) \
	|| fail "checksum mismatch after remount"
[ "$(stat -c %s "$MNT/data/medium")" -eq $((524288 + 1234)) ] \
	|| fail "appended size wrong after remount"

ltfs_finish
echo "PASS"
