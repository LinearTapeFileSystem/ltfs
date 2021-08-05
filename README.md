![](https://img.shields.io/github/issues/lineartapefilesystem/ltfs.svg)
![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/CentOS7%20Build%20Job/badge.svg?branch=master)
[![CodeFactor](https://www.codefactor.io/repository/github/lineartapefilesystem/ltfs/badge)](https://www.codefactor.io/repository/github/lineartapefilesystem/ltfs)
[![BSD License](http://img.shields.io/badge/license-BSD-blue.svg?style=flat)](LICENSE)

# About this branch

This is the `master` branch of the LTFS project. At this time, this branch is used for version 2.5 development. So it wouldn't be stable a little. Please consider to follow the tree on `v2.4-stable` branch if you want to use stable codes.

# What is the Linear Tape File System (LTFS)

The Linear Tape File System (LTFS) is a filesystem to mount a LTFS formatted tape in a tape drive. Once LTFS mounts a LTFS formatted tape as filesystem, user can access to the tape via filesystem API.

Objective of this project is being the reference implementation of the LTFS format Specifications in [SNIA](https://www.snia.org/tech_activities/standards/curr_standards/ltfs).

At this time, the target of this project to meet is the [LTFS format specifications 2.5](https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_v2.5_Technical_Position.pdf).

## Supported Tape Drives

  | Vendor  | Drive Type              | Minimum F/W Level |
  |:-------:|:-----------------------:|:-----------------:|
  | IBM     | LTO5                    | B170              |
  | IBM     | LTO6                    | None              |
  | IBM     | LTO7                    | None              |
  | IBM     | LTO8                    | HB81              |
  | IBM     | TS1140                  | 3694              |
  | IBM     | TS1150                  | None              |
  | IBM     | TS1155                  | None              |
  | IBM     | TS1160                  | None              |
  | HP      | LTO5                    | T.B.D.            |
  | HP      | LTO6                    | T.B.D.            |
  | HP      | LTO7                    | T.B.D.            |
  | HP      | LTO8                    | T.B.D.            |
  | Quantum | LTO5 (Only Half Height) | T.B.D.            |
  | Quantum | LTO6 (Only Half Height) | T.B.D.            |
  | Quantum | LTO7 (Only Half Height) | T.B.D.            |
  | Quantum | LTO8 (Only Half Height) | T.B.D.            |

## LTFS Format Specifications

LTFS Format Specification is specified data placement, shape of index and names of extended attributes for LTFS. This specification is defined in [SNIA](https://www.snia.org/tech_activities/standards/curr_standards/ltfs) first and then it is forwarded to [ISO](https://www.iso.org/home.html) as ISO/IEC 20919 from version 2.2.

The table below show status of the LTFS format Specification

  | Version | Status of SNIA                                                                                               | Status of ISO             |
  |:-------:|:------------------------------------------------------------------------------------------------------------:|:-------------------------:|
  | 2.2     | [Published](http://snia.org/sites/default/files/LTFS_Format_2.2.0_Technical_Position.pdf)                    | Published as `20919:2016` |
  | 2.3.1   | [Published](https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_2.3.1_TechPosition.PDF) | -                         |
  | 2.4     | [Published](https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_2.4.0_TechPosition.pdf) | -                         |
  | 2.5.1   | [Published](https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_2%205%201_Standard.pdf) | Published as `20919:2021` |

# How to use the LTFS (Quick start)

This section is for person who already have a machine the LTFS is installed. Instruction how to use the LTFS is also available on [Wiki](https://github.com/LinearTapeFileSystem/ltfs/wiki).

## Step1: List tape drives

`# ltfs -o device_list`

The output is like follows. You can have 3 drives in this example and you can use "Device Name" field, like `/dev/sg43` in this case, as the argument of ltfs command to mount the tape drive.

```
50c4 LTFS14000I LTFS starting, LTFS version 2.4.0.0 (10022), log level 2.
50c4 LTFS14058I LTFS Format Specification version 2.4.0.
50c4 LTFS14104I Launched by "/home/piste/ltfsoss/bin/ltfs -o device_list".
50c4 LTFS14105I This binary is built for Linux (x86_64).
50c4 LTFS14106I GCC version is 4.8.5 20150623 (Red Hat 4.8.5-11).
50c4 LTFS17087I Kernel version: Linux version 3.10.0-514.10.2.el7.x86_64 (mockbuild@x86-039.build.eng.bos.redhat.com) (gcc version 4.8.5 20150623 (Red Hat 4.8.5-11) (GCC) ) #1 SMP Mon Feb 20 02:37:52 EST 2017 i386.
50c4 LTFS17089I Distribution: NAME="Red Hat Enterprise Linux Server".
50c4 LTFS17089I Distribution: Red Hat Enterprise Linux Server release 7.3 (Maipo).
50c4 LTFS17089I Distribution: Red Hat Enterprise Linux Server release 7.3 (Maipo).
50c4 LTFS17085I Plugin: Loading "sg" tape backend.
Tape Device list:.
Device Name = /dev/sg43, Vender ID = IBM    , Product ID = ULTRIUM-TD5    , Serial Number = 9A700L0077, Product Name = [ULTRIUM-TD5] .
Device Name = /dev/sg38, Vender ID = IBM    , Product ID = ULT3580-TD6    , Serial Number = 00013B0119, Product Name = [ULT3580-TD6] .
Device Name = /dev/sg37, Vender ID = IBM    , Product ID = ULT3580-TD7    , Serial Number = 00078D00C2, Product Name = [ULT3580-TD7] .
```

## Step2: Format a tape

As described into the LTFS format specifications, LTFS uses the partition feature of the tape drive. It means you can't use a tape just after you purchase a tape. You need format the tape before using int on LTFS.

To format a tape, you can use `mkltfs` command like

`# mkltfs -d 9A700L0077`

In this case, `mkltfs` tries to format a tape in the tape drive `9A700L0077`. You can use a device name `/dev/sg43` instead.

## Step3: Mount a tape through a tape drive

After you prepared a formatted tape, you can mount it through a tape drive like

`# ltfs -o devname=9A700L0077 /ltfs`

In this command, the ltfs command try to mount the tape in the tape drive `9A700L0077` to `/ltfs` directory. Of cause, you can use a device name `/dev/sg43` instead.

If mount process is successfully done, you can access to the LTFS tape through `/ltfs` directory.

You must not touch any `st` devices while ltfs is mounting a tape.

## Step4: Unmount the tape drive

You can use following command when you want to unmount the tape. The ltfs command try to write down the current meta-data to the tape and close the tape cleanly.

`# umount /ltfs`

One thing you need to pay attention here is it is not a unmount completion when umount command is returned. It just a finish of trigger to notify the unmount request to the ltfs command. Actual unmount is completed when the ltfs command is finished.

## The `ltfsee_ordered_copy` utility

The [`ltfsee_ordered_copy`](https://github.com/LinearTapeFileSystem/ltfs/wiki/ltfs_ordered_copy) is a program to copy files from source to destination with LTFS  order  optimization.

It is written by python and it can work on both python2 and python3 (Python 2.7 or later is strongly recommended). You need to install the `pyxattr` module for both python2 and python3.

# Building the LTFS from this GitHub project

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes.

## Prerequisites for build

Please refer [this page](https://github.com/LinearTapeFileSystem/ltfs/wiki/Build-Environments).

## Build and install on Linux

```
./autogen.sh
./configure
make
make install
```

`./configure --help` shows various options for build and install.

In some systems, you might need `sudo ldconfig -v` after `make install` to load the shared libraries correctly.

### Buildable Linux distributions

  | Dist                               | Arch    | Status                                                                                                                                |
  |:----------------------------------:|:-------:|:-------------------------------------------------------------------------------------------------------------------------------------:|
  | RHEL 8                             | x86\_64 | OK - Not checked automatically                                                                                                        |
  | RHEL 8                             | ppc64le | OK - Not checked automatically                                                                                                        |
  | RHEL 7                             | x86\_64 | OK - Not checked automatically                                                                                                        |
  | RHEL 7                             | ppc64le | OK - Not checked automatically                                                                                                        |
  | CentOS 8                           | x86\_64 | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/CentOS8%20Build%20Job/badge.svg?branch=master)             |
  | CentOS 8                           | ppc64le | OK - Not checked automatically                                                                                                        |
  | CentOS 7                           | x86\_64 | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/CentOS7%20Build%20Job/badge.svg?branch=master)             |
  | CentOS 7                           | ppc64le | OK - Not checked automatically                                                                                                        |
  | Fedora 28                          | x86\_64 | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/Fedora28%20Build%20Job/badge.svg?branch=master)            |
  | Ubuntu 16.04 LTS                   | x86\_64 | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/Ubuntu%2016.04%20Build%20Job/badge.svg?branch=master)      |
  | Ubuntu 16.04 LTS                   | ppc64le | [![Build Status](https://travis-ci.org/LinearTapeFileSystem/ltfs.svg?branch=master)](https://travis-ci.org/LinearTapeFileSystem/ltfs) |
  | Ubuntu 18.04 LTS                   | x86\_64 | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/Ubuntu%2018.04%20Build%20Job/badge.svg?branch=master)      |
  | Ubuntu 18.04 LTS                   | ppc64le | [![Build Status](https://travis-ci.org/LinearTapeFileSystem/ltfs.svg?branch=master)](https://travis-ci.org/LinearTapeFileSystem/ltfs) |
  | Ubuntu 20.04 LTS (Need icu-config) | x86\_64 | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/Ubuntu%2020.04%20Build%20Job/badge.svg?branch=master)      |
  | Debian 9                           | x86\_64 | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/Debian9%20Build%20Job/badge.svg?branch=master)             |
  | Debian 10 (Need icu-config)        | x86\_64 | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/Debian10%20Build%20Job/badge.svg?branch=master)            |
  | ArchLinux 2018.08.01               | x86\_64 | OK - Not checked automatically                                                                                                        |
  | ArchLinux 2018.12.31 (rolling)     | x86\_64 | OK - Not checked automatically                                                                                                        |

Currently, automatic build checking is working on GitHub Actions and Travis CI.

For Ubuntu20.04 and Debian10, dummy `icu-config` is needed in the build machine. See Issue [#153](https://github.com/LinearTapeFileSystem/ltfs/issues/153).

## Build and install on OSX (macOS)

### Recent Homedrew system setup

Before build on macOS, you need to configure the environment like below.

```
export ICU_PATH="/usr/local/opt/icu4c/bin"
export LIBXML2_PATH="/usr/local/opt/libxml2/bin"
export PKG_CONFIG_PATH="/usr/local/opt/icu4c/lib/pkgconfig:/usr/local/opt/libxml2/lib/pkgconfig"
export PATH="$PATH:$ICU_PATH:$LIBXML2_PATH"
```

### Old Homedrew system setup
Before build on OSX (macOS), some include path adjustment is required.

```
brew link --force icu4c
brew link --force libxml2
```

### Building LTFS
On OSX (macOS), snmp cannot be supported, you need to disable it on configure script. And may be, you need to specify LDFLAGS while running configure script to link some required frameworks, CoreFundation and IOKit.

```
./autogen.sh
LDFLAGS="-framework CoreFoundation -framework IOKit" ./configure --disable-snmp
make
make install
```

`./configure --help` shows various options for build and install.

#### Buildable macOS systems

  | OS            | Xcode | Package system | Status                                                                                                                                |
  |:-------------:|:-----:|:--------------:|:-------------------------------------------------------------------------------------------------------------------------------------:|
  | macOS 10.14.6 | 11.3  | Homebrew       | [![Build Status](https://travis-ci.org/LinearTapeFileSystem/ltfs.svg?branch=master)](https://travis-ci.org/LinearTapeFileSystem/ltfs) |
  | macOS 10.15   | 12.4  | Homebrew       | OK - Not checked automatically                                                                                                        |
  | macOS 11      | 12.4  | Homebrew       | OK - Not checked automatically                                                                                                        |

## Build and install on FreeBSD

Note that on FreeBSD, the usual 3rd party man directory is /usr/local/man. Configure defaults to using /usr/local/share/man.  So, override it on the command line to avoid having man pages put in the wrong place.

```
./autogen.sh
./configure --prefix=/usr/local --mandir=/usr/local/man
make
make install
```

#### Buildable versions

  | Version | Arch    | Status                         |
  |:-------:|:-------:|:------------------------------:|
  | 11      | x86\_64 | OK - Not checked automatically |
  | 12      | x86\_64  | OK - Not checked automatically |

### Build and install on NetBSD

```
./autogen.sh
./configure
make
make install
```

#### Buildable versions

  | Version | Arch  | Status                         |
  |:-------:|:-----:|:------------------------------:|
  | 8.1     | amd64 | OK - Not checked automatically |
  | 8.0     | i386  | OK - Not checked automatically |
  | 7.2     | amd64 | OK - Not checked automatically |

## Contributing

Please read [CONTRIBUTING.md](.github/CONTRIBUTING.md) for details on our code of conduct, and the process for submitting pull requests to us.

## License

This project is licensed under the BSD License - see the [LICENSE](LICENSE) file for details
