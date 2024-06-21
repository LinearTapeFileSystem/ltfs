#!/bin/sh

source ./utils.sh

MOUNTPOINT='/tmp/mnt'
TAPE_PATH='/tmp/ltfstape'

if [ $# == 1 ]; then
    MOUNTPOINT=$1
elif [ $# -gt 2 ]; then
    MOUNTPOINT=$1
    TAPE_PATH=$2
fi

# Format LTFS
FormatLTFS ${TAPE_PATH}
if [ $? != 0 ]; then
    exit 1
fi

# Launch LTFS
LaunchLTFS ${MOUNTPOINT} ${TAPE_PATH}
if [ $? != 0 ]; then
    exit 1
fi

# 1. CREATE DIRS
# Create a few dirs and files but all objects are handles by new dirs
echo "1. CREATE DIRS"
mkdir -p ${MOUNTPOINT}/dir1/dir11
mkdir -p ${MOUNTPOINT}/dir1/dir12
mkdir -p ${MOUNTPOINT}/dir2/dir21
mkdir -p ${MOUNTPOINT}/dir2/dir22
echo "AAA" > ${MOUNTPOINT}/dir1/file11
echo "AAA" > ${MOUNTPOINT}/dir1/dir11/file111
echo "AAA" > ${MOUNTPOINT}/dir1/dir11/file112
IncrementalSync ${MOUNTPOINT} '1.CREATE_DIRS'
if [ $? != 0 ]; then
    exit 1
fi

# 2. CREATE FILES
# Create files for checking file creation and directory traverse
echo "2. CREATE FILES"
echo "AAA" > ${MOUNTPOINT}/dir1/dir11/file113
echo "AAA" > ${MOUNTPOINT}/dir1/dir12/file121
echo "AAA" > ${MOUNTPOINT}/dir2/dir22/file221
echo "AAA" > ${MOUNTPOINT}/dir2/file21
IncrementalSync ${MOUNTPOINT} '2.CREATE_FILES'
if [ $? != 0 ]; then
    exit 1
fi

# 3. MODIFY FILES
# Modify contents of files. Need to check  /dir2 doesn't have meta-data on the incremental index
echo "3. MODIFY FILES"
echo "BBB" > ${MOUNTPOINT}/dir1/dir11/file111
echo "BBB" > ${MOUNTPOINT}/dir2/dir22/file221
echo "BBB" > ${MOUNTPOINT}/dir1/file11
IncrementalSync ${MOUNTPOINT} '3.MODIFY_FILES'
if [ $? != 0 ]; then
    exit 1
fi

# 4. MODIFY DIRS
# Modify directory's meta-data. Need to check both /dir1 and /dir1/dir11 has meta-data
# on the incremental index
echo "4. MODIFY DIRS"
AddXattr ${MOUNTPOINT}/dir1 'ut-attr1' 'val1'
echo "CCC" > ${MOUNTPOINT}/dir1/dir11/file111
IncrementalSync ${MOUNTPOINT} '4.MODIFY_DIRS'
if [ $? != 0 ]; then
    exit 1
fi

# 5. DELETE FILES
echo "5. DELETE FILES"
rm ${MOUNTPOINT}/dir1/dir11/*
IncrementalSync ${MOUNTPOINT} '5.DELETE_FILES'
if [ $? != 0 ]; then
    exit 1
fi

# 6. DELETE DIR
echo "5. DELETE DIR"
rm -rf ${MOUNTPOINT}/dir1/dir11
IncrementalSync ${MOUNTPOINT} '6.DELETE_DIR'
if [ $? != 0 ]; then
    exit 1
fi

# Stop LTFS
StopLTFS ${MOUNTPOINT} ${TAPE_PATH}
if [ $? != 0 ]; then
    exit 1
fi
