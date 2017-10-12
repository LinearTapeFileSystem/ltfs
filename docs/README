IBM Spectrum Archive Single Drive Edition Version 2.4.0.0 (10022)

Licensed Materials - Property of IBM

IBM Spectrum Archive Single Drive Edition Version 2.4.0.0 for Linux and Mac OS X

Copyright 2010, 2017 IBM Corp.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions, and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions, and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

-----------------------------------------
 README for Linux and Mac OS X platforms
------------------------------------------

This README file applies to IBM Spectrum Archive Single Drive Edition Version 2.4.0, and to all subsequent
releases, modifications, and service refreshes, until otherwise indicated in a
new README file.

This README file provides late-breaking information that may not have been incorporated
into the IBM Spectrum Archive Single Drive Edition Information Center.

Online user's guidance
======================
You can find information about IBM Spectrum Archive Single Drive Edition on IBM Knowledge Center:
  http://www.ibm.com/support/knowledgecenter/STQNYL

Usage example:
==============

1. Add /usr/local/bin to the command search path.

  The LTFS package is installed under /usr/local.
  Add /usr/local/bin to your command search path by issuing the following command:

  $ export PATH="$PATH:/usr/local/bin"

2. Format the tape cartridge in the LTFS format by issuing the following command for your platform:

   Linux
      $ mkltfs -d /dev/IBMtape0

   Mac OS X
      The device name can be confirmed by the system profiler.
      $ mkltfs -d 0

3. Mount the formatted tape cartridge by issuing the following commands for your platform:

   Linux
     $ mkdir /mnt/ltfs
     $ ltfs /mnt/ltfs -o devname=/dev/IBMtape0

   Mac OS X
     $ mkdir /mnt
     $ mkdir /mnt/ltfs
     $ ltfs /mnt/ltfs -o devname=0

4. Unmount the tape cartridge by issuing the following command.
   (This command empties the data buffer of the file system to the tape cartridge and prepares to eject the cartridge.)

  $ umount /mnt/ltfs
    OR
  $ fusermount -u /mnt/ltfs

Changes in this version (from 2.2.2.0 to 2.4.0.0):
=================================================
  - Supported LTFS format specifications 2.4.0.
  - New OS support:
    - RHEL 7 (ppc64le)
    - OSX 10.12
  - End of support:
    - RHEL 6
    - SLES 11
    - OSX 10.10
  - Problem fixes reported in the previous versions.

Change Log:
============================================
(from 2.2.1.1 to 2.2.2.0)
  - New OS support:
    - RHEL 7 (x86_64)
    - SLES 11.4
    - OSX 10.10, 10.11
  - End of support:
    - RHEL 5
    - SLES 11.3 or earlier
    - OSX 10.9 or earlier
  - Problem fixes reported in the previous versions.

(from 2.2.1.0 to 2.2.1.1):
  - Problem fixes reported in the previous versions.

(from 2.2.0.2 to 2.2.1.0)
  - Changed its program name from IBM Linear Tape File System (LTFS).
  - Improved the handling of a false write error.
  - Fixed the problem that the file creation time is incorrectly displayed on Mac OS X.
  - Support LTO7 tape drives.
  - Fixed the illegal instruction problem on OS X 10.7.

(from 2.2.0.1 to 2.2.0.2)
  - Support for TS1150 tape drives (Linux Only).
  - Support for CRC32C logical block protection for TS1150 tape drives with SSE 4.2 instruction set (Linux Only).

(from 2.2.0.0 to 2.2.0.1)
  - Supported USB LTO tape drives.

(from 1.3.0.2 to 2.2.0.0)
  - New OS support:
    - OS X 10.9 Mavericks
  - Change version number rule
    - First 3 digits indicate supported version of the LTFS format specifications.
  - Improved sparse file handling
    - Write sparse file data in order to improve read back performance.
  - Improved performance of writing index.
  - Improved performance of writing small files.
  - Problem fixes reported in the previous versions.
  - (Linux Only) Implemented the capability of SNMP.
  - Change OSXFUSE bundling from 2.5.6 to 2.6.2.

(from 1.3.0.1 to 1.3.0.2)
  - Problem fixes reported in 1.3.0.1.
  - Supported LTFS format specifications 2.2.0.
  - New OS support:
    - OS X 10.8 Mountain Lion (64 bit only)
  - New architecture support:
    - Power PC (64 bit only, RHEL 5.9 and 6.2 only)
  - Change OSXFUSE bundling from 2.3.9 to 2.5.6.

(From 1.3.0 to 1.3.0.1)
  - Improved memory footprint on the cartridge mount operation.
  - (Mac OS X only) Fix the handling of voice sound marks of Japanese characters in file names.

(From 1.2.5 to 1.3.0)
  - Supported IBM TS1140 tape drive.
  - Supported IBM LTO6 tape drive.
  - Supported LTFS format specifications 2.1.0.
    - Supported symbolic link.
    - Added VEAs to the ltfs.software category.
    - Supported vendor unique VEAs.
  - Supported rollback mounts.
  - Supported an eject when ltfs is unmounted.
  - Supported to list available devices.
  - Supported to release a reserved device to recover from an unexpected shutdown.
  - Improved file creation time when a directory has many files.
  - Enhanced the ability to recover from a sudden cable pull.
  - Supported the logical block protection feature on IBM tape drives.
  - Add the I/F to pass a data encryption key to the tape drive with AME.
  - Changed OSXFUSE bundling from 2.3.8 to 2.3.9.

(From 1.2.0 to 1.2.5)
  New Platform Support:
    - Added support for Mac OS X 10.7 Lion, either in 32-bit or 64-bit kernel mode,
      by bundling OSXFUSE 2.3.8 as the replacement of MacFUSE.

  RAS enhancements:
    - Improved the handling of a cartridge that was not unmounted properly.
    - Improved the handling of a full data cartridge.
    - Improved the process for storing a file on the index partition when the data placement
      policy is specified.

  Usability enhancements:
    - Added support for the -f option with the mkltfs command to prevent the user from overwriting
      the LTFS-formatted cartridge by accident.
    - Added four new virtual extended attributes (VEAs) for identifying the software version
      information from the command line.
       - ltfs.softwareVendor, ltfs.softwareProduct, ltfs.softwareVersion, ltfs.softwareFormatSpec
    - Modified to update the change time of a file or directory when ltfs.modifyTime VEA is written.
    - (Mac OS X only) Changed to display the cartridge's volume name below its icon, as specified
      by the -n option of the mkltfs command.
    - (Mac OS X only) Added the ability for LTFS to run in the background, the same as the Linux version,
      if -f option is not specified.

  Fixes:
  - Fixed the problem that ltfs.mediaStorageAlert VEA is cleared when other VEA is read.

(From 1.0.1 to 1.2.0)
  - Changed its program name from Linear Tape File System
    to Linear Tape File System.
  - Enhanced the ability to recover the tapes in the inconsistent state,
    which was caused by an unexpected power outage.
  - Added a new function to empty the in-memory user data to the tape medium
    at pre-defined timing; either sync at file close or sync periodically (default).
  - Conforms with the new LTFS Format Specification 2.0.0.
    - Backward compatibility with Version 1.0 formatted tapes, and a built-in
      automatic migration mechanism to the Version 2.0.0 format.
    - Improved the parsing and handling of the index information
    - Supports standardized virtual extended attribute (VEA) names.
      VEA names used in previous releases of LTFS become obsolete and
      no longer available.
  - Changed the default block size to 512KB.
    - Backward read/write compatibility with 1MB blocksize tapes.
  - Changed the command line syntax of ltfs and mkltfs commands for
    specifying the data placement rules.

(From 1.00 to 1.0.1)
    - Support virtual extended attributes.
    - Added a new eject-on-unmount option (-o eject) to the ltfs command for ejecting
      the cartridge automatically after completing the unmount operation.
    - Improved the format time by reducing the number of partition switches during
      volume format in mkltfs.
    - Improved the mount time by swapping the physical IDs of the index and
      data partitions.
    - Improved the backward read access performance for the files smaller than 1MB.
    - Improved the index parse time.
    - Changed to not load the tape cartridge into the LTO drive when an
      LTO-4 drive or older is attached (Mac OS X only).
    - Corrected error messages for the partition full condition.
    - Corrected error messages when a drive encounters errors during mount.
    - Fixed ltfsck to list rollback points when the drive returns an unexpected
      error.
    - Fixed to store files to an index partition when the files stored into an
      index partition are overwritten.
    - Removed an incorrect syntax example for rules from a mkltfs help message.

Note to users:
==============

  General Notice
  --------------
  - Refer to the documents distributed with the tape drive and IBM Spectrum Archive.
  - The IBM LTO and enterprise tape drives provide the hardware for data compression. The amount of
    written data on a tape can vary from the original data size.


Known Issues:
==============

  1. On Mac OS X: IBM Spectrum Archive does not support moving a folder within an LTFS-formatted cartridge.
     Any attempt to move a folder in such a manner will result in an "operation not permitted" error,
     and the operation will be ignored.  The user can still move a file within an LTFS-formatted cartridge,
     and the user can also move a folder from an LTFS-formatted cartridge to a non-LTFS file system or vice versa.
  2. On Mac OS X: Mac OS X's Spotlight will not display the files on an LTFS-formatted cartridge as a result of a search.
