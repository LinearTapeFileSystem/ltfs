#!/bin/sh
# Writing and closing files while a large directory is listed must not deadlock.
. "${top_srcdir}/tests/lib/harness.sh"

NENTRIES=1000
NWRITES=300

# stop_deadlocked <what>: stop the workers, detach the mount and fail
stop_deadlocked() {
	kill "$LS_PID" "$WR_PID" 2>/dev/null || true
	# A stat of the mount point would block on the deadlocked daemon
	# too; detach the mount so that the cleanup goes on to kill it.
	_fusermount -u -z "$MNT" 2>/dev/null || umount -l "$MNT" 2>/dev/null || true
	fail "$1 did not finish within 120 s (deadlock)"
}

# wait_pid <pid> <what>: wait up to 120 s, in 0.2 s steps, for the process to end
wait_pid() {
	n=0
	while kill -0 "$1" 2>/dev/null; do
		[ $n -lt 600 ] || stop_deadlocked "$2"
		sleep 0.2
		n=$((n + 1))
	done
}

ltfs_setup

mkdir "$MNT/big" "$MNT/w"
i=0
while [ $i -lt $NENTRIES ]; do
	: >"$MNT/big/e$i"
	i=$((i + 1))
done

while [ ! -e "$WORK/stop" ]; do
	ls -l "$MNT/big" >/dev/null
done &
LS_PID=$!

(
	i=0
	while [ $i -lt $NWRITES ]; do
		head -c 100000 /dev/urandom >"$MNT/w/f$i"
		i=$((i + 1))
	done
) &
WR_PID=$!

wait_pid "$WR_PID" "writing files while listing"
: >"$WORK/stop"
wait_pid "$LS_PID" "listing after the writes"
wait "$LS_PID" || fail "listing failed while files were written"
wait "$WR_PID" || fail "writing files failed while listing"

ltfs_finish
echo "PASS"
