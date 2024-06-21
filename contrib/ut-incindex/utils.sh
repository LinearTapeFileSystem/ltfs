#!/bin/sh

PLATFORM=`uname`
ECHO='/bin/echo'

if [ "x${LTFS_BIN_PATH}" == 'x' ]; then
    LTFS_BIN_PATH='/usr/local/bin'
fi

AddXattr()
{
    if [ "x$1" == "x" ]; then
        "Need to a target to set xattr"
        return 1
    else
        TARGET=$1
    fi

    if [ "x$2" == "x" ]; then
        "Need to a name to set xattr"
        return 1
    else
        NAME=$2
    fi

    if [ "x$3" == "x" ]; then
        "Need to a value to set xattr"
        return 1
    else
        VAL=$2
    fi

    ${ECHO} -n "Setting a xattr ${NAME} to ${TARGET} ... "
    if [ "$PLATFORM" == "Darwin" ]; then
        /usr/bin/xattr -w ${NAME} ${VAL} ${TARGET}
    else
        /usr/bin/attr -s ${NAME} -V ${VAL} ${TARGET}
    fi

    if [ $? == 0 ]; then
        ${ECHO} "Done"
        return 0
    else
        ${ECHO} "Failed"
        return 1
    fi

}

IncrementalSync()
{
    if [ "x$1" == "x" ]; then
        MOUNTPOINT='/tmp/mnt'
    else
        MOUNTPOINT=$1
    fi

    if [ "x$2" == "x" ]; then
        MSG='Incremental Sync'
    else
        MSG=$2
    fi

    ${ECHO} -n "Syncing LTFS (Incremental) ... "
    if [ "$PLATFORM" == "Darwin" ]; then
        /usr/bin/xattr -w ltfs.vendor.IBM.IncrementalSync ${MSG} ${MOUNTPOINT}
    else
        /usr/bin/attr -s ltfs.vendor.IBM.IncrementalSync -V ${MSG} ${MOUNTPOINT}
    fi

    if [ $? == 0 ]; then
        ${ECHO} "Done"
        return 0
    else
        ${ECHO} "Failed"
        return 1
    fi
}

FullSync()
{
    if [ "x$1" == "x" ]; then
        MOUNTPOINT='/tmp/mnt'
    else
        MOUNTPOINT=$1
    fi

    if [ "x$2" == "x" ]; then
        MSG='Full Sync'
    else
        MSG=$2
    fi

    ${ECHO} "Syncing LTFS (Full) ... "
    if [ "$PLATFORM" == "Darwin" ]; then
        /usr/bin/xattr -w ltfs.vendor.IBM.FullSync ${MSG} ${MOUNTPOINT}
    else
        /usr/bin/attr -s ltfs.vendor.IBM.FullSync -V ${MSG} ${MOUNTPOINT}
    fi

    if [ $? == 0 ]; then
        ${ECHO} "Done"
        return 0
    else
        ${ECHO} "Failed"
        return 1
    fi
}

FormatLTFS()
{
    if [ "x$1" == "x" ]; then
        TAPE_PATH='/tmp/ltfstape'
    else
        TAPE_PATH=$1
    fi

    if [ ! -d ${TAPE_PATH} ]; then
        ${ECHO} "Creating tape directory for file backend: ${TAPE_PATH}"
        mkdir -p ${TAPE_PATH}
        if [ $? != 0 ]; then
            ${ECHO} "Failed to create a tape path: ${TAPE_PATH}"
            return 1
        fi
    fi

    ${ECHO} "Formatting tape directory with the file backend on ${TAPE_PATH} ... "
    ${LTFS_BIN_PATH}/mkltfs -f -e file -d ${TAPE_PATH}
    if [ $? != 0 ]; then
        ${ECHO} "Failed to format a tape path: ${TAPE_PATH}"
        return 1
    fi

    ${ECHO} "Formatted the file backend on ${TAPE_PATH}"
    return 0
}

LaunchLTFS()
{
    if [ "x$1" == "x" ]; then
        MOUNTPOINT='/tmp/mnt'
    else
        MOUNTPOINT=$1
    fi

    if [ "x$2" == "x" ]; then
        TAPE_PATH='/tmp/ltfstape'
    else
        TAPE_PATH=$2
    fi

    if [ ! -d ${MOUNTPOINT} ]; then
        ${ECHO} "Creating mount point for LTFS: ${MOUNTPOINT}"
        mkdir -p ${MOUNTPOINT}
        if [ $? != 0 ]; then
            ${ECHO} "Failed to create a mount point"
            return 1
        fi
    fi

    if [ ! -d ${TAPE_PATH} ]; then
        ${ECHO} "Creating tape directory for file backend: ${TAPE_PATH}"
        mkdir -p ${TAPE_PATH}
        if [ $? != 0 ]; then
            ${ECHO} "Failed to create a tape path: ${TAPE_PATH}"
            return 1
        fi

        ${ECHO} "Formatting tape directory with the file backend"
        ${LTFS_BIN_PATH}/mkltfs -f -e file -d ${TAPE_PATH}
        if [ $? != 0 ]; then
            ${ECHO} "Failed to format a tape path: ${TAPE_PATH}"
            return 1
        fi
    fi

    ${ECHO} "Launching LTFS with the file backend"
    ${LTFS_BIN_PATH}/ltfs -o tape_backend=file -o sync_type=unmount -o devname=${TAPE_PATH} ${MOUNTPOINT}
    if [ $? != 0 ]; then
        ${ECHO} "Failed to launch LTFS on ${MOUNTPOINT}"
        return 1
    fi

    ${ECHO} "LTFS is launched on ${MOUNTPOINT}"
    return 0
}

StopLTFS()
{
    if [ "x$1" == "x" ]; then
        MOUNTPOINT='/tmp/mnt'
    else
        MOUNTPOINT=$1
    fi

    sudo umount ${MOUNTPOINT}
}
