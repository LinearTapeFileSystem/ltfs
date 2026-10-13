#!/bin/sh
# touch must store the given date, or the current time, on new and existing
# files; an update of one time must keep the other.
. "${top_srcdir}/tests/lib/harness.sh"

DATE=2020-01-01T00:00:00
DATE_A=2021-02-03T04:05:06
DATE_M=2022-03-04T05:06:07

# check_now <name> <format> <what>: the stat field is within 5 minutes of $now
check_now() {
	t=$(stat -c "$2" "$MNT/$1")
	[ $((t - now)) -ge -300 ] && [ $((t - now)) -le 300 ] \
		|| fail "$1: $3 is $t, expected the current time $now"
}

# check_times <name> <atime> <mtime> <after>: the file has exactly these times
check_times() {
	atime=$(stat -c %X "$MNT/$1")
	mtime=$(stat -c %Y "$MNT/$1")
	[ "$atime" -eq "$2" ] && [ "$mtime" -eq "$3" ] \
		|| fail "$1: atime $atime, mtime $mtime after $4, expected $2 and $3"
}

ltfs_setup

date_epoch=$(date -d "$DATE" +%s)
date_a_epoch=$(date -d "$DATE_A" +%s)
date_m_epoch=$(date -d "$DATE_M" +%s)

printf data >"$MNT/dated"
printf data >"$MNT/existing"
touch -d "$DATE" "$MNT/dated" "$MNT/existing"
for f in dated existing; do
	check_times "$f" "$date_epoch" "$date_epoch" "touch -d $DATE"
done

now=$(date +%s)
touch "$MNT/existing"
check_now existing %Y "mtime after touch of an existing file"
check_now existing %X "atime after touch of an existing file"

touch "$MNT/new"
check_now new %Y "mtime of a file created by touch"

# Set one time and pass UTIME_OMIT for the other, which touch -a and -m do
# not always do. libfuse 2 drops such updates because LTFS does not set
# flag_utime_omit_ok.
if ltfs_is_fuse3; then
	[ -x "$HELPER" ] || fail "fsops_helper not built"
	printf data >"$MNT/partial"
	touch -d "$DATE" "$MNT/partial"
	"$HELPER" atime "$MNT/partial" "$date_a_epoch" || fail "partial: atime update failed"
	check_times partial "$date_a_epoch" "$date_epoch" "an atime-only update"
	"$HELPER" mtime "$MNT/partial" "$date_m_epoch" || fail "partial: mtime update failed"
	check_times partial "$date_a_epoch" "$date_m_epoch" "an mtime-only update"
fi

existing_mtime=$(stat -c %Y "$MNT/existing")
existing_atime=$(stat -c %X "$MNT/existing")
new_mtime=$(stat -c %Y "$MNT/new")

ltfs_remount

[ "$(stat -c %Y "$MNT/dated")" -eq "$date_epoch" ] || fail "dated: mtime changed by remount"
[ "$(stat -c %Y "$MNT/existing")" -eq "$existing_mtime" ] || fail "existing: mtime changed by remount"
[ "$(stat -c %X "$MNT/existing")" -eq "$existing_atime" ] || fail "existing: atime changed by remount"
[ "$(stat -c %Y "$MNT/new")" -eq "$new_mtime" ] || fail "new: mtime changed by remount"
if ltfs_is_fuse3; then
	check_times partial "$date_a_epoch" "$date_m_epoch" "remount"
fi

ltfs_finish
echo "PASS"
