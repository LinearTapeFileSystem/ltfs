;;
;;
;;  OO_Copyright_BEGIN
;;
;;
;;  Copyright 2010, 2018 IBM Corp. All rights reserved.
;;
;;  Redistribution and use in source and binary forms, with or without
;;   modification, are permitted provided that the following conditions
;;  are met:
;;  1. Redistributions of source code must retain the above copyright
;;     notice, this list of conditions and the following disclaimer.
;;  2. Redistributions in binary form must reproduce the above copyright
;;     notice, this list of conditions and the following disclaimer in the
;;  documentation and/or other materials provided with the distribution.
;;  3. Neither the name of the copyright holder nor the names of its
;;     contributors may be used to endorse or promote products derived from
;;     this software without specific prior written permission.
;;
;;  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
;;  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
;;  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
;;  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
;;  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
;;  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
;;  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
;;  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
;;  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
;;  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
;;  POSSIBILITY OF SUCH DAMAGE.
;;
;;
;;  OO_Copyright_END
;;

dnl
dnl LTFS configure.ac.
dnl
AC_INIT([LTFS], [2.4.0.2 (10071)], IBM corporation.)
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_HOST
AC_CANONICAL_BUILD
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC_C99
AM_PROG_CC_C_O
AM_PROG_AR
AC_PROG_LIBTOOL

dnl
dnl Detecting OS
dnl
host_linux=no
host_mac=no

case "${host_os}" in
	linux*)
		host_linux=yes
		;;
	darwin*)
		host_mac=yes
		;;
	*)
		AC_MSG_ERROR(["OS $host_os is not supported"])
		;;
esac

dnl
dnl Handle --enable-debug (default:no)
dnl
AC_MSG_CHECKING([whether to compile in debug mode])
AC_ARG_ENABLE([debug],
	[AS_HELP_STRING([--enable-debug],[compile with extra debugging output])],
	[use_debug=$enableval],
	[use_debug=no]
)
AC_MSG_RESULT([$use_debug])

dnl
dnl Handle --enable-message-checker (default:no)
dnl
AC_MSG_CHECKING([whether to compile in message checking mode])
AC_ARG_ENABLE([message-checker],
	[AS_HELP_STRING([--enable-message-checker],[compile in message checking mode (may be binary is defunct)])],
	[use_msg_check=$enableval],
	[use_msg_check=no]
)
AC_MSG_RESULT([$use_msg_check])

dnl
dnl Handle --enable-fast (default:no)
dnl
AC_MSG_CHECKING([whether to enable optimizations])
AC_ARG_ENABLE([fast],
	[AS_HELP_STRING([--enable-fast],[compile with optimization enabled])],
	[use_fast=$enableval],
	[use_fast=no]
)
AC_MSG_RESULT([$use_fast])

if test "x${use_fast}" != "xno"
then
	if test "x${use_debug}" != "xno"
	then
		AC_MSG_ERROR([Cannot specify --enable-fast and --enable-debug at the same time.])
	fi
fi

dnl
dnl Handle --enable-livelink (default:yes)
dnl
AC_MSG_CHECKING([whether to enable livelink mode support [default=yes] ])
AC_ARG_ENABLE([livelink],
	[AS_HELP_STRING([--enable-livelink],[compile with livelink mode support [default=yes]])],
	[livelink=$enableval],
	[livelink=yes]
)
AC_MSG_RESULT([$livelink])

dnl
dnl Handle --enable-snmp (default:yes)
dnl
AC_MSG_CHECKING([whether to enable snmp or not])
AC_ARG_ENABLE([snmp],
	[AS_HELP_STRING([--enable-snmp],[Support SNMP or not])],
	[snmp=$enableval],
	[snmp=yes]
)
AC_MSG_RESULT([$snmp])

dnl
dnl Handle --enable-lin-tape (default:no, Linux Only)
dnl
AC_MSG_CHECKING([whether to enable lin_tape or not (Linux Only)])
AC_ARG_ENABLE([lintape],
	[AS_HELP_STRING([--enable-lintape],[Support IBM's lin_tape driver or not])],
	[lintape=$enableval],
	[lintape=no]
)
AC_MSG_RESULT([$lintape])

dnl
dnl Handle default backends
dnl
if test -z "$DEFAULT_TAPE"
then
	if test "x${host_linux}" = "xyes"
	then
		DEFAULT_TAPE=sg
	fi
	if test "x${host_mac}" = "xyes"
	then
		DEFAULT_TAPE=iokit
	fi
fi

if test "x${DEFAULT_TAPE}" = "xlin_tape"
then
	if test "x${lintape}" != "xyes"
	then
		AC_MSG_ERROR([lin_tape is not enabled --enable-lintape is required.])
	fi
fi

if test -z "$DEFAULT_IOSCHED" ; then
	DEFAULT_IOSCHED=unified
fi

if test -z "$DEFAULT_KMI" ; then
	DEFAULT_KMI=none
fi

AC_ARG_VAR([DEFAULT_TAPE], [default tape device plugin, e.g. sg])
AC_ARG_VAR([DEFAULT_IOSCHED], [default I/O scheduler plugin, e.g. unified])
AC_ARG_VAR([DEFAULT_KMI], [default key manager interface plugin, e.g. none])

dnl
dnl Check for pkg-config
dnl
if test -z "$PKG_CONFIG"
then
	AC_PATH_PROG(PKG_CONFIG, pkg-config, [no])
fi
if test "x${PKG_CONFIG}" = "xno"
then
	AC_MSG_ERROR([pkg-config was not found])
fi

dnl
dnl Check for genrb
dnl
AC_PATH_PROG(GENRB, genrb, no)
if test "x${GENRB}" = "xno"
then
	AC_MSG_ERROR([genrb was not found])
fi

dnl
dnl Check for pkgdata
dnl
AC_PATH_PROG(PKGDATA, pkgdata, no)
if test "x${PKGDATA}" = "xno"
then
	AC_MSG_ERROR([pkgdata was not found])
fi

dnl
dnl Check for FUSE, libuuid, and libxml2
dnl
PKG_CHECK_MODULES([FUSE_MODULE], [fuse >= 2.6.0])
PKG_CHECK_MODULES([LIBXML2_MODULE], [libxml-2.0 >= 2.6.16])

if test "x${host_mac}" = "xyes"
then
	PKG_CHECK_MODULES([UUID_MODULE], [uuid >= 1.6])
else
	PKG_CHECK_MODULES([UUID_MODULE], [uuid >= 1.36])
fi

dnl
dnl Check for ICU
dnl
ICU_MODULE_CFLAGS="`icu-config --cppflags 2> /dev/null`";
ICU_MODULE_LIBS="`icu-config --ldflags 2> /dev/null`";
if test -z "$ICU_MODULE_LIBS"
then
	PKG_CHECK_MODULES([ICU_MODULE], [icu >= 0.21])
fi

dnl
dnl Check for SNMP
dnl
SNMP_ENABLE=""
SNMP_MODULE_CFLAGS=""
SNMP_MODULE_LIBS_A=""
SNMP_MODULE_LIBS=""
if test "x${snmp}" != "xno"
then
	SNMP_ENABLE="-D ENABLE_SNMP"
	SNMP_MODULE_CFLAGS="`net-snmp-config --cflags 2> /dev/null`";
	SNMP_MODULE_LIBS_A="`net-snmp-config --agent-libs` 2> /dev/null";
	SNMP_MODULE_LIBS="`net-snmp-config --libs 2> /dev/null`";
	if test -z "$SNMP_MODULE_LIBS_A"
	then
		PKG_CHECK_MODULES([SNMP_MODULE], [net-snmp >= 5.3])
	fi

	if test -z "$SNMP_MODULE_LIBS"
	then
		PKG_CHECK_MODULES([SNMP_MODULE], [net-snmp >= 5.3])
	fi
fi

dnl
dnl Check for headers, types, structures, compiler characteristics
dnl
AC_CHECK_HEADERS([fcntl.h limits.h stddef.h stdint.h stdlib.h string.h sys/ioctl.h sys/mount.h sys/time.h termios.h unistd.h])
AC_HEADER_STDBOOL
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_CHECK_SIZEOF([time_t])
AC_STRUCT_ST_BLOCKS
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_C_INLINE

dnl
dnl Check for library functions
dnl
AC_FUNC_MALLOC
#AC_FUNC_MKTIME
AC_FUNC_STRNLEN
AC_CHECK_FUNCS([bzero memchr memset mkdir rmdir strcasecmp strdup strerror strndup strrchr strstr])

dnl
dnl Check xmlTextReaderSetup is existed
dnl
AC_CHECK_FUNC([xmlTextReaderSetup], [AC_DEFINE(HAVE_XML_READER_SETUP, [1], [Define to 1 if you have xmlTextReaderSetup])], [])
if test "x$ac_cv_func_xmlTextReaderSetup" = "xno"; then
   AC_CHECK_LIB(xml2, xmlTextReaderSetup, [AC_DEFINE(HAVE_XML_READER_SETUP, [1], [Define to 1 if you have xmlTextReaderSetup])], [])
fi

dnl
dnl Check XML_PARSE_HUGE is defined
dnl
SAVE_CFLAGS=$CFLAGS
CFLAGS="$CFLAGS ${LIBXML2_MODULE_CFLAGS}"
AC_MSG_CHECKING(for XML_PARSE_HUGE)
AC_CACHE_VAL(ac_cv_xml_huge,
	AC_TRY_COMPILE([#include <libxml/parser.h>],
				   [int foo = XML_PARSE_HUGE; return 0],
				   ac_cv_xml_huge=yes,
				   ac_cv_xml_huge=no
				   ))
AC_MSG_RESULT($ac_cv_xml_huge)
CFLAGS="$SAVE_CFLAGS"

if test "x$ac_cv_xml_huge" = "xyes"; then
   AC_DEFINE(HAVE_XML_PARSE_HUGE, [1], [Define to 1 if you have XML_PARSE_HUGE])
fi

dnl
dnl Update flags
dnl Sets CFLAGS to force optimization and debugging options, which isn't quite kosher
dnl
AM_CPPFLAGS="-D_GNU_SOURCE -I\$(top_srcdir)/src -DLTFS_CONFIG_FILE='\"${sysconfdir}/ltfs.conf\"' -DLTFS_BASE_DIR='\"${prefix}\"'"
AM_CFLAGS="-Wall -Wsign-compare -fsigned-char ${FUSE_MODULE_CFLAGS} ${UUID_MODULE_CFLAGS} ${LIBXML2_MODULE_CFLAGS} ${ICU_MODULE_CFLAGS} ${SNMP_ENABLE} ${SNMP_MODULE_CFLAGS}"

if test "x$use_fast" = "xyes"
then
	OPT_FLAGS="-D_FORTIFY_SOURCE=2 -O2 -g -fno-strict-aliasing"
else
	OPT_FLAGS="-D_FORTIFY_SOURCE=0 -O0 -ggdb"
	AM_CFLAGS="${AM_CFLAGS} -fkeep-inline-functions -rdynamic"
	if test "x$use_debug" = "xyes"
	then
		AM_CPPFLAGS="${AM_CPPFLAGS} -DDEBUG -DTRACE"
	fi
fi

if test "x$use_msg_check" = "xyes"
then
    AM_CPPFLAGS="${AM_CPPFLAGS} -DMSG_CHECK"
fi

if test "x$livelink" = "xno"
then
	AM_CPPFLAGS="${AM_CPPFLAGS} -DPOSIXLINK_ONLY"
fi

dnl
dnl Specify CPU specific optimizer options for CRC calculation
dnl
AC_MSG_CHECKING([for SSE4.2])
CRC_OPTIMIZE="-O2"

if test "x$GCC" = 'xyes'
then
	GCC_VERSION=`$CC -dumpversion`
	GCC_VERSION_MAJOR=$(echo $GCC_VERSION | cut -d'.' -f1)
	GCC_VERSION_MINOR=$(echo $GCC_VERSION | cut -d'.' -f2)
	SSE42=no

	if test ${GCC_VERSION_MAJOR} -ge 4 -a ${GCC_VERSION_MINOR} -ge 3
	then
		SSE42=yes
	fi

	if test "x${SSE42}" = "xyes"
	then
		case x"$target_cpu" in
			xx86_64)
				AC_MSG_RESULT([yes, x86_64])
				CRC_OPTIMIZE="-msse4.2 -O2 -D__SSE42__"
				;;
			xi*86)
				AC_MSG_RESULT([yes, x86])
				CRC_OPTIMIZE="-msse4.2 -O2 -D__SSE42__"
				;;
			*)
				AC_MSG_RESULT([no, unsupported cpu])
				;;
		esac
	else
		AC_MSG_RESULT([no, gcc version])
	fi
else
	AC_MSG_RESULT([no, non-gcc])
fi

dnl
dnl Configure standard options
dnl
if test -x /usr/local/bin/gsed
then
	SED='gsed'
else
	SED='sed'
fi

AM_CFLAGS=`echo ${AM_CFLAGS} | ${SED} 's|-D_FORTIFY_SOURCE=. ||g'`
AM_LDFLAGS="${AM_LDFLAGS} ${FUSE_MODULE_LIBS} ${UUID_MODULE_LIBS} ${LIBXML2_MODULE_LIBS} ${ICU_MODULE_LIBS} ${SNMP_MODULE_LIBS_A} ${SNMP_MODULE_LIBS}"
CFLAGS="${CFLAGS} ${OPT_FLAGS}"



dnl
dnl Define options
dnl
AM_CONDITIONAL([PLATFORM_LINUX], [test "x${host_linux}" = "xyes"])
AM_CONDITIONAL([ENABLE_LIN_TAPE], [test "x${lintape}" = "xyes"])
AM_CONDITIONAL([PLATFORM_MAC], [test "x${host_mac}" = "xyes"])
AM_CONDITIONAL([OSS], [true])
AM_CONDITIONAL([CHK_MESSAGE], [test "x${use_msg_check}" = "xyes"])

AC_SUBST(CFLAGS)
AC_SUBST(CRC_OPTIMIZE)
AC_SUBST(AM_CPPFLAGS)
AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_LDFLAGS)

dnl
dnl Output files
dnl
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
	messages/Makefile
	conf/Makefile
	man/Makefile
	init.d/Makefile
	src/Makefile
	src/libltfs/Makefile
	src/tape_drivers/generic/file/Makefile
	src/tape_drivers/generic/itdtimg/Makefile
	src/tape_drivers/linux/lin_tape/Makefile
	src/tape_drivers/linux/sg-ibmtape/Makefile
	src/tape_drivers/osx/iokit-ibmtape/Makefile
	src/iosched/Makefile
	src/kmi/Makefile
	src/utils/Makefile
	ltfs.pc:ltfs.pc.in
])

AC_OUTPUT
