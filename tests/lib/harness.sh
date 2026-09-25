# Common helpers for LTFS integration tests.
#
# Tests run against the build tree (no installation needed) using the
# file tape backend, which emulates a tape drive in a plain directory.
#
# A test script sources this file, calls ltfs_setup, performs its checks
# under $MNT, then calls ltfs_finish (unmount + ltfsck). Cleanup of
# mounts and temporary files is handled by an EXIT trap.

set -eu

SKIP=77

: "${top_builddir:?top_builddir must be set (run via make check)}"
: "${top_srcdir:?top_srcdir must be set (run via make check)}"

LTFS_BIN="$top_builddir/src/ltfs"
MKLTFS_BIN="$top_builddir/src/utils/mkltfs"
LTFSCK_BIN="$top_builddir/src/utils/ltfsck"

# Extra mount options for the ltfs invocation; tests may override.
LTFS_MOUNT_OPTS="${LTFS_MOUNT_OPTS:-}"

WORK=
LTFS_PID=
MNT=
TAPE=

skip() {
	echo "SKIP: $*"
	exit "$SKIP"
}

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

ltfs_check_env() {
	[ "$(uname -s)" = "Linux" ] || skip "FUSE integration tests only run on Linux"
	[ -e /dev/fuse ] || skip "/dev/fuse is not available"
	command -v fusermount3 >/dev/null 2>&1 || command -v fusermount >/dev/null 2>&1 \
		|| [ "$(id -u)" = "0" ] || skip "fusermount is not available"
	[ -x "$LTFS_BIN" ] || fail "ltfs binary not found at $LTFS_BIN"
	[ -x "$MKLTFS_BIN" ] || fail "mkltfs binary not found at $MKLTFS_BIN"
	[ -x "$LTFSCK_BIN" ] || fail "ltfsck binary not found at $LTFSCK_BIN"
}

_fusermount() {
	if command -v fusermount3 >/dev/null 2>&1; then
		fusermount3 "$@"
	elif command -v fusermount >/dev/null 2>&1; then
		fusermount "$@"
	else
		# root can unmount directly
		shift  # drop -u
		umount "$@"
	fi
}

ltfs_cleanup() {
	status=$?
	set +e
	if [ -n "$MNT" ] && mountpoint -q "$MNT" 2>/dev/null; then
		_fusermount -u "$MNT" 2>/dev/null || umount "$MNT" 2>/dev/null
	fi
	if [ -n "$LTFS_PID" ] && kill -0 "$LTFS_PID" 2>/dev/null; then
		# Give the daemon time to flush the index after unmount.
		for _ in $(seq 50); do
			kill -0 "$LTFS_PID" 2>/dev/null || break
			sleep 0.2
		done
		kill "$LTFS_PID" 2>/dev/null
	fi
	[ -n "$WORK" ] && rm -rf "$WORK"
	exit "$status"
}

# Locate a built plugin .so, accepting both the autotools (libtool .libs/)
# and the CMake (plain subdir) layouts.
_find_plugin() {
	subdir=$1
	base=$2
	for cand in \
		"$top_builddir/src/$subdir/.libs/$base.so" \
		"$top_builddir/src/$subdir/$base.so"; do
		if [ -f "$cand" ]; then
			echo "$cand"
			return 0
		fi
	done
	fail "plugin $base.so not found under $top_builddir/src/$subdir"
}

# Generate an ltfs.conf pointing at the plugins in the build tree.
_write_config() {
	cat >"$WORK/ltfs.conf" <<EOF
plugin tape file $(_find_plugin tape_drivers/generic/file libtape-file)
plugin iosched unified $(_find_plugin iosched libiosched-unified)
plugin iosched fcfs $(_find_plugin iosched libiosched-fcfs)
plugin kmi flatfile $(_find_plugin kmi libkmi-flatfile)
plugin kmi simple $(_find_plugin kmi libkmi-simple)
default tape file
default iosched unified
default kmi none
EOF
}

# ltfs_setup: format a file-backend tape and mount it on $MNT.
ltfs_setup() {
	ltfs_check_env

	WORK=$(mktemp -d "${TMPDIR:-/tmp}/ltfstest.XXXXXX")
	trap ltfs_cleanup EXIT INT TERM
	MNT="$WORK/mnt"
	TAPE="$WORK/tape"
	mkdir -p "$MNT" "$TAPE"

	_write_config

	"$MKLTFS_BIN" -i "$WORK/ltfs.conf" -e file -d "$TAPE" -n testvol -f \
		>"$WORK/mkltfs.log" 2>&1 || {
		cat "$WORK/mkltfs.log" >&2
		fail "mkltfs failed"
	}

	# Run in the foreground so we keep the pid and the log.
	# shellcheck disable=SC2086
	"$LTFS_BIN" "$MNT" -o config_file="$WORK/ltfs.conf" -o tape_backend=file \
		-o devname="$TAPE" $LTFS_MOUNT_OPTS -f >"$WORK/ltfs.log" 2>&1 &
	LTFS_PID=$!

	for _ in $(seq 150); do
		mountpoint -q "$MNT" && return 0
		kill -0 "$LTFS_PID" 2>/dev/null || break
		sleep 0.2
	done
	cat "$WORK/ltfs.log" >&2
	fail "ltfs did not mount within timeout"
}

# ltfs_umount: unmount and wait for the daemon to flush and exit.
ltfs_umount() {
	_fusermount -u "$MNT"
	for _ in $(seq 150); do
		kill -0 "$LTFS_PID" 2>/dev/null || { LTFS_PID=; return 0; }
		sleep 0.2
	done
	cat "$WORK/ltfs.log" >&2
	fail "ltfs daemon did not exit after unmount"
}

# ltfs_fsck: verify the volume is consistent.
# Exit codes follow fsck conventions: 0 = clean, 1 = corrected/treat as
# success (e.g. MAM coherency update); anything else is a failure.
ltfs_fsck() {
	rc=0
	"$LTFSCK_BIN" -i "$WORK/ltfs.conf" -e file "$TAPE" >"$WORK/ltfsck.log" 2>&1 || rc=$?
	if [ "$rc" -gt 1 ]; then
		cat "$WORK/ltfsck.log" >&2
		fail "ltfsck reported errors (exit $rc)"
	fi
	grep -q "Volume is consistent" "$WORK/ltfsck.log" || {
		cat "$WORK/ltfsck.log" >&2
		fail "ltfsck did not report a consistent volume"
	}
}

# ltfs_finish: standard end of test (unmount + consistency check).
ltfs_finish() {
	ltfs_umount
	ltfs_fsck
}

# ltfs_is_fuse3: true when the ltfs binary is linked against libfuse 3.
ltfs_is_fuse3() {
	{ ldd "$LTFS_BIN" 2>/dev/null || ldd "$top_builddir/src/.libs/ltfs" 2>/dev/null; } \
		| grep -q libfuse3
}

# ltfs_remount: unmount and mount again (e.g. to verify persistence).
ltfs_remount() {
	ltfs_umount
	# shellcheck disable=SC2086
	"$LTFS_BIN" "$MNT" -o config_file="$WORK/ltfs.conf" -o tape_backend=file \
		-o devname="$TAPE" $LTFS_MOUNT_OPTS -f >"$WORK/ltfs-remount.log" 2>&1 &
	LTFS_PID=$!
	for _ in $(seq 150); do
		mountpoint -q "$MNT" && return 0
		kill -0 "$LTFS_PID" 2>/dev/null || break
		sleep 0.2
	done
	cat "$WORK/ltfs-remount.log" >&2
	fail "ltfs did not remount within timeout"
}
