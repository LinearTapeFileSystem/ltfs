#
# Integration tests for LTFS using the file tape backend.
#
# Run with: make check
# The tests need a Linux host with /dev/fuse; elsewhere they are skipped.
# On macOS use tests/run-in-docker.sh to run them in a Linux container.
#

TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
	top_builddir='$(abs_top_builddir)' top_srcdir='$(abs_top_srcdir)'; \
	export top_builddir top_srcdir;

check_PROGRAMS = helpers/fsops_helper
helpers_fsops_helper_SOURCES = helpers/fsops_helper.c

# The test list is computed at configure time (TESTS_LIST in configure.ac)
# so feature branches can ship additional t/*.sh scripts without editing
# the build files; ctest registers them the same way. GNU make functions
# cannot be used here: automake runs with -Wall -Werror, which rejects
# them as non-POSIX.
TESTS = $(TESTS_LIST)

EXTRA_DIST = \
	lib/harness.sh \
	$(TESTS) \
	docker/Dockerfile \
	docker/icu-config \
	run-in-docker.sh
