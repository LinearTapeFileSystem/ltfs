#!/bin/sh
# readdir on a directory with many entries; attributes must match stat.
. "${top_srcdir}/tests/lib/harness.sh"

ltfs_setup

mkdir "$MNT/big"
i=0
while [ $i -lt 500 ]; do
	printf '%d' "$i" >"$MNT/big/f$i"
	i=$((i + 1))
done

count=$(ls "$MNT/big" | wc -l)
[ "$count" -eq 500 ] || fail "expected 500 entries, got $count"

# Attributes from listing (readdirplus on fuse3) must match per-file stat
ls_size=$(ls -l "$MNT/big/f123" | awk '{print $5}')
stat_size=$(stat -c %s "$MNT/big/f123")
[ "$ls_size" = "$stat_size" ] || fail "listing size != stat size"
[ "$stat_size" -eq 3 ] || fail "unexpected file size"

ltfs_remount
count=$(ls "$MNT/big" | wc -l)
[ "$count" -eq 500 ] || fail "entries lost after remount"

ltfs_finish
echo "PASS"
