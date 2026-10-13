#!/bin/sh
# Overwriting a file with shorter content must truncate it (O_TRUNC).
. "${top_srcdir}/tests/lib/harness.sh"

# check_file <name> <expected content> <when>
check_file() {
	size=$(stat -c %s "$MNT/$1")
	content=$(cat "$MNT/$1")
	[ "$size" -eq ${#2} ] && [ "$content" = "$2" ] \
		|| fail "$1 $3: expected '$2' (${#2} bytes), got '$content' ($size bytes)"
}

ltfs_setup

printf 0123456789 >"$MNT/redirect"
printf abc >"$MNT/redirect"

printf 0123456789 >"$WORK/long"
printf xyz >"$WORK/short"
cp "$WORK/long" "$MNT/copy"
cp "$WORK/short" "$MNT/copy"

printf 0123456789 >"$MNT/append"
printf abc >>"$MNT/append"

check_file redirect abc "after overwrite by redirection"
check_file copy xyz "after overwrite by cp"
check_file append 0123456789abc "after append"

ltfs_remount

check_file redirect abc "after overwrite by redirection and remount"
check_file copy xyz "after overwrite by cp and remount"
check_file append 0123456789abc "after append and remount"

ltfs_finish
echo "PASS"
