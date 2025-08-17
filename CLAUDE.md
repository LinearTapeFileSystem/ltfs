# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the reference implementation of the Linear Tape File System (LTFS) format specifications from SNIA, the LTFS format specification 2.5 (https://www.snia.org/sites/default/files/technical-work/ltfs/release/SNIA-LTFS-Format-v2.5-Technical-Position.pdf).

LTFS is a filesystem implementation that allows mounting LTFS-formatted tapes as regular filesystems. LTFS uses Filesystem in Userspace (FUSE) under the foot.

## Build Commands

### Linux Build Configuration
```bash
./autogen.sh
./configure --prefix=</path_to_install>
```

### Linux Build
```bash
make
make install
# May need: sudo ldconfig -v
```

### macOS Build Configuration
```bash
# Setup environment first:
export ICU_PATH="/usr/local/opt/icu4c/bin"
export LIBXML2_PATH="/usr/local/opt/libxml2/bin"
export PKG_CONFIG_PATH="/usr/local/opt/icu4c/lib/pkgconfig:/usr/local/opt/libxml2/lib/pkgconfig"
export PATH="$PATH:$ICU_PATH:$LIBXML2_PATH"

./autogen.sh
LDFLAGS="-framework CoreFoundation -framework IOKit" ./configure --disable-snmp --prefix=</path_to_install>
```

### macOS Build
```bash
make
make install
```

### Clean Build
```bash
make clean
```

### Clean up everything
```bash
make clean
make distclean
```

## Core Architecture

### Main Components

1. **libltfs** (`src/libltfs/`) - Core LTFS library
   - `ltfs.c/h` - Main LTFS data structures and operations
   - `ltfs_fsops.c` - Filesystem operations implementation
   - `tape.c/h` - Tape drive abstraction layer
   - `index_criteria.c` - Index management logic
   - `xml_reader.c/xml_writer.c` - XML index parsing/generation

2. **Tape Drivers** (`src/tape_drivers/`)
   - Platform-specific tape drive implementations
   - `linux/sg/` - Linux SCSI generic driver
   - `linux/lin_tape/` - IBM lin_tape driver
   - `osx/iokit/` - macOS IOKit driver
   - `freebsd/cam/` - FreeBSD CAM driver
   - `generic/file/` - Debug purpose tape emulation on a directory (for creating situations hard to recreate)

3. **I/O Schedulers** (`src/iosched/`)
   - `fcfs.c` - First-come-first-served scheduler (This is sample of I/O scheduler. Not used at all.)
   - `unified.c` - Unified I/O scheduler with optimization (This I/O scheduler can create 512KB block from chunked requests from FUSE layer)

4. **Key Management** (`src/kmi/`)
   - `simple.c` - Simple key management interface, receive keys from options
   - `flatfile.c` - Flat file-based key storage, receive keys specified unencrypted file

5. **Utilities** (`src/utils/`)
   - `mkltfs.c` - Format tapes for LTFS like `mkfs`
   - `ltfsck.c` - Check and repair LTFS volumes like `fsck`
   - `ltfsindextool.c` - Manipulate LTFS indexes outside of LTFS filesystem implementation
   - `ltfs_ordered_copy` - Python script for optimized file copying

6. **Entry point of `ltfs` process** (`src/main.c`)
   - This is the start point of LTFS filesystem process

7. **ltfs internal filesystem operations** (`src/libltfs/ltfs_fsops.c/h`)
   - This is the ltfs own filesystem operations implementation

8. **FUSE-LTFS bridge** (`ltfs_fuse.c`)
   - This layer converts request from FUSE to the ltfs own filesystem operations

### Key Design Patterns

- **Plugin Architecture**: Tape drivers, I/O schedulers, and key management are implemented as plugins loaded at runtime and called indirectly by pointer
- **FUSE Integration**: Uses FUSE (Filesystem in Userspace) for filesystem operations
- **XML Indexes**: Filesystem metadata stored as XML on tape (following LTFS format spec)
- **Dual Partition**: Uses tape partition feature - index on Index Partition, data on Data Partition

### Important Files to Understand

- `src/libltfs/ltfs_internal.c`  - This is low level function called from ltfs.c
- `src/libltfs/ltfs_fsops_raw.c` - This is low level function to handle LTFS format called sequence is FUSE->FUSE-LTFS bridge->ltfs internal filesystem operations->I/O scheduler->low level function to handle LTFS format

# Tech Stack
- Language: C99 with gcc

# Project Structure
- `contrib`:               Contribution code that is not a part of the project but is variable for this project
- `docs`:                  Documents and configuration files for system. See also docs/README.md
- `init.d`:                Service script for init
- `man`:                   man pages for the commands provided by the project
- `man/sgml`:              Original documents for man pages written by docbook 4.1 format
- `messages`:              Message files used in the code tree
- `src/libltfs`:           Source files for libltfs, the library that handles logical layer of the LTFS format
- `src/iosched`:           I/O scheduler plugins for libltfs. The libltfs makes indirect calls to an ioschead plugin loaded when a command is launched for creating 512KB block for tape R/W
- `src/tape_drivers`:      Tape drive handling plugins for libltfs. The libltfs makes indirect calls to an tape drive plugin loaded when a command is launched for issuing SCSI commands to different type of tape drives
- `src/kmi`:               Encryption key management plugins for libltfs
- `utils/mkltfs.c`:        Formatting tool for the LTFS like `mkfs`
- `utils/ltfsck.c`:        Recovery tool for the LTFS like `fsck`
- `utils/ltfsindextool.c`: Tools for capturing indexes on the tape
- `./main.c`:              Main function for `ltfs` command, it is the file system command for FUSE
- `./ltfs_fuse.*`:         FUSE - LTFS file operation glue layer
- `./ltfs_copyright.h`:    Copyright definition for injecting to compiled binaries

## Common Development Tasks

### Running LTFS Commands

```bash
# List available tape drives
ltfs -o device_list

# Format a tape
mkltfs -d <device_name>

# Mount a tape
ltfs -o devname=<device_name> <mount_point>

# Unmount
umount <mount_point>

# Check/repair LTFS volume
ltfsck -d <device_name>
```

### Debugging

- Use `--enable-debug` configure option for debug builds
- Set log level with `-o loglevel=<level>` (0-4)
- Check syslog for LTFS messages

## Platform-Specific Notes

- **Linux**: Supports both sg (SCSI generic) and lin_tape drivers
- **macOS**: Requires disabling SNMP support (`--disable-snmp`)
- **FreeBSD/NetBSD**: Uses platform-specific SCSI interfaces

## Message System

- Messages defined in `messages/` directory
- Message is constructed a number and sevirity (Error, Warning, Info, Debug)
- Number part of message must be unique
  - It means 11111W must not be used if 11111I is used
- Number part of message must not be reused once used
  - It means 11111[EWID] must not be used if 11111[EWID] is already commented out
- Message ID must be commented out when it is not required anymore
- Each component has its own message file
- Run `make_message_src.sh` to regenerate message headers

# Do Not Section
- Do not commit directly to the any branches
- Do not provide any changes which cannot be compiled
- Do not use duplicated messege number under the `messages` directory
- Do not reuse any message number which is commented out inder the `messages` directory
