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

BASENAME=`echo $1 | sed -e 's/_dat\.o$//'`

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
		*)
			mv ${BASENAME}_dat.o ../../
			;;
	esac

	# Clean up
	cd ..
	rm -rf work
}

# Check whether we need to do anything
if [ -f "../$1" ]; then
	for file in *; do
		if [ "$file" -nt "../$1" ]; then
			make_obj
			exit 0
		fi
	done
else
	make_obj
fi
