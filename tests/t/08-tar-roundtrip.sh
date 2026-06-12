#!/bin/sh
# Copy a small tree onto the volume, tar it back, and compare checksums.
. "${top_srcdir}/tests/lib/harness.sh"

ltfs_setup

# Build a source tree with a few subdirectories and binary files
mkdir -p "$WORK/tree/sub1/sub2"
dd if=/dev/urandom of="$WORK/tree/a.bin" bs=100K count=1 status=none
dd if=/dev/urandom of="$WORK/tree/sub1/b.bin" bs=300K count=1 status=none
dd if=/dev/urandom of="$WORK/tree/sub1/sub2/c.bin" bs=700K count=1 status=none
echo "text file" >"$WORK/tree/readme.txt"

cp -r "$WORK/tree" "$MNT/tree"

( cd "$WORK/tree" && find . -type f -exec sha256sum {} + | sort ) >"$WORK/src.sums"
( cd "$MNT/tree" && find . -type f -exec sha256sum {} + | sort ) >"$WORK/dst.sums"
cmp -s "$WORK/src.sums" "$WORK/dst.sums" || fail "tree copy checksum mismatch"

# tar from the volume and extract elsewhere
tar -C "$MNT" -cf "$WORK/vol.tar" tree
mkdir "$WORK/extract"
tar -C "$WORK/extract" -xf "$WORK/vol.tar"
( cd "$WORK/extract/tree" && find . -type f -exec sha256sum {} + | sort ) >"$WORK/tar.sums"
cmp -s "$WORK/src.sums" "$WORK/tar.sums" || fail "tar roundtrip checksum mismatch"

ltfs_finish
echo "PASS"
