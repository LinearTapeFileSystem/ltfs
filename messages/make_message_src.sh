#!/bin/sh

set -e

KERNEL_NAME=`uname -s`
if [ "$KERNEL_NAME" = "Darwin" ]; then
	ICU_FRAMEWORK=/Library/Frameworks/ICU.framework

	if [ -d ${ICU_FRAMEWORK} ]; then
		export PATH=${PATH}:${ICU_FRAMEWORK}/Versions/Current/usr/bin
		export DYLD_LIBRARY_PATH=${ICU_FRAMEWORK}/Versions/Current/usr/lib
		GENRB=${ICU_FRAMEWORK}/Versions/Current/usr/bin/genrb
		PKGDATA=${ICU_FRAMEWORK}/Versions/Current/usr/bin/pkgdata
	else
		GENRB=genrb
		PKGDATA=pkgdata
	fi
elif [ "$KERNEL_NAME" = "FreeBSD" ]; then
	GENRB=genrb
	PKGDATA=/usr/local/bin/pkgdata
else
	if [ -x /usr/bin/genrb ]; then
		GENRB=/usr/bin/genrb
	else
		GENRB=genrb
	fi
	if [ -x /usr/bin/pkgdata ]; then
		PKGDATA=/usr/bin/pkgdata
	else
		PKGDATA=pkgdata
	fi
fi

if [ "$#" -ne "1" ]; then
	echo "Usage: $0 object_file"
	exit 1
fi

case $KERNEL_NAME in
	MINGW32_NT*)
		BASENAME=`echo $1 | sed -e 's/_dat\.o$//'`
		;;
	*)
		BASENAME=`echo $1 | sed -e 's/_dat\.a$//'`
		BASENAME=`echo $BASENAME | sed -e 's/^lib//'`
		;;
esac

cd ${BASENAME}

make_obj() {
	echo "Processing ${BASENAME}"

	# Create a fresh work directory
	if [ -d work ]; then
		rm -rf work
	fi
	mkdir work

	# Generate files
	${GENRB} -d work -q *.txt
	cd work
	ls *.res >packagelist.txt
	${PKGDATA} -p ${BASENAME} -m static -q packagelist.txt >/dev/null

	case $KERNEL_NAME in
		MINGW32_NT*)
			mv ${BASENAME}.dat ../../
			;;
		FreeBSD)
			# pkgdata with -m static generates an ar(1) archive
			# with several object files on FreeBSD.  To avoid
			# reworking the makefiles for all OSes, just rename
			# the archive to match the regular convention.  The
			# linker handles it without a problem.
			mv lib${BASENAME}.a ../../lib${BASENAME}_dat.a
			;;
		*)
			mv ${BASENAME}_dat.o ../../lib${BASENAME}_dat.a
			;;
	esac

	# Clean up
	cd ..
	rm -rf work
}

# Check whether we need to do anything
if [ -f "../$1" ]; then
	for file in *.txt; do
		if [ "$file" -nt "../$1" ]; then
			make_obj
			exit 0
		fi
	done
else
	make_obj
fi
