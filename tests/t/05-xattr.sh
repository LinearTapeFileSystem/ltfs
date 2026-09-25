#!/bin/sh
# Extended attribute set/get/list/remove, persistence across remount.
. "${top_srcdir}/tests/lib/harness.sh"

ltfs_setup

command -v setfattr >/dev/null 2>&1 || skip "attr tools not installed"

echo content >"$MNT/file"

setfattr -n user.test1 -v value1 "$MNT/file"
setfattr -n user.test2 -v value2 "$MNT/file"

[ "$(getfattr --only-values -n user.test1 "$MNT/file")" = "value1" ] \
	|| fail "getxattr value"

listing=$(getfattr "$MNT/file" | grep '^user\.')
echo "$listing" | grep -q user.test1 || fail "listxattr missing user.test1"
echo "$listing" | grep -q user.test2 || fail "listxattr missing user.test2"

setfattr -x user.test2 "$MNT/file"
getfattr "$MNT/file" 2>/dev/null | grep -q user.test2 && fail "removexattr"

ltfs_remount
[ "$(getfattr --only-values -n user.test1 "$MNT/file")" = "value1" ] \
	|| fail "xattr lost after remount"

ltfs_finish
echo "PASS"
