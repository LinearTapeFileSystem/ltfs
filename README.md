![](https://img.shields.io/github/issues/lineartapefilesystem/ltfs.svg)
![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/CentOS7%20Build%20Job/badge.svg?branch=master)
[![BSD License](http://img.shields.io/badge/license-BSD-blue.svg?style=flat)](LICENSE)

# Linear Tape File System (LTFS)

Linear Tape File System (LTFS) is a filesystem to mount a LTFS formatted tape in a tape drive. Once LTFS mounts a LTFS formatted tape as filesystem, user can access to the tape via filesystem API.

Objective of this project is being the reference implementation of the LTFS format Specifications in [SNIA](https://www.snia.org/tech_activities/standards/curr_standards/ltfs).

At this time, the target of this project to meet is the LTFS format specifications 2.4. (https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_2.4.0_TechPosition.pdf).

## LTFS Format Specifications

LTFS Format Specification is specified data placement, shape of index and names of extended attributes for LTFS. This specification is defined in [SNIA](https://www.snia.org/tech_activities/standards/curr_standards/ltfs) first and then it is forwarded to [ISO](https://www.iso.org/home.html) as ISO/IEC 20919 from version 2.2.

The table below show status of the LTFS format Specification

  | Version | Status of SNIA                                                                                               | Status of ISO             |
  |:-------:|:------------------------------------------------------------------------------------------------------------:|:-------------------------:|
  | 2.2     | [Published](http://snia.org/sites/default/files/LTFS_Format_2.2.0_Technical_Position.pdf)                    | Published as `20919:2016` |
  | 2.3.1   | [Published](https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_2.3.1_TechPosition.PDF) | -                         |
  | 2.4     | [Published](https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_2.4.0_TechPosition.pdf) | -                         |

## How to use the LTFS (Quick start)

This section is for person who already have a machine the LTFS is installed.

Instruction how to use the LTFS is on [Wiki](https://github.com/LinearTapeFileSystem/ltfs/wiki). Please take a look!

## Getting Started from GitHub project

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes.

## Prerequisites

- Linux
  * automake 1.13.4 or later
  * autoconf 2.69 or later
  * libtool 2.4.2 or later
  * fuse 2.6.0 or later
  * uuid 1.36 or later (Linux)
  * libxml-2.0 2.6.16 or later
  * net-snmp 5.3 or later
  * icu4c 4.8 or later

- OSX (macOS)

  Following packages on homebrew

  * automake
  * autoconf
  * libtool
  * osxfuse (brew cask install osxfuse)
  * ossp-uuid
  * libxml2
  * icu4c
  * gnu-sed

- FreeBSD:
  * FreeBSD 10.2 or 11.0 or later (for sa(4) driver changes)
  * automake
  * autoconf
  * libtool
  * fusefs-libs
  * net-snmp
  * e2fsprogs-libuuid
  * libxml2
  * icu

- NetBSD:
  * NetBSD 7.0 or higher (for FUSE support)
  * automake
  * autoconf
  * libtool
  * libfuse
  * net-snmp
  * libuuid
  * libxml2
  * icu

## Supported Tape Drives

  | Vendor  | Drive Type              | Minimum F/W Level |
  |:-------:|:-----------------------:|:-----------------:|
  | IBM     | LTO5                    | B170              |
  | IBM     | LTO6                    | None              |
  | IBM     | LTO7                    | None              |
  | IBM     | LTO8                    | HB81              |
  | IBM     | LTO9                    | None              |
  | IBM     | TS1140                  | 3694              |
  | IBM     | TS1150                  | None              |
  | IBM     | TS1155                  | None              |
  | IBM     | TS1160                  | None              |
  | HP      | LTO5                    | T.B.D.            |
  | HP      | LTO6                    | T.B.D.            |
  | HP      | LTO7                    | T.B.D.            |
  | HP      | LTO8                    | T.B.D.            |
  | HP      | LTO9                    | T.B.D.            |  
  | Quantum | LTO5 (Only Half Height) | T.B.D.            |
  | Quantum | LTO6 (Only Half Height) | T.B.D.            |
  | Quantum | LTO7 (Only Half Height) | T.B.D.            |
  | Quantum | LTO8 (Only Half Height) | T.B.D.            |
  | Quantum | LTO9 (Only Half Height) | T.B.D.            |  

## Installing

### Build and install on Linux

```
./autogen.sh
./configure
make
make install
```

`./configure --help` shows various options for build and install.

In some systems, you might need `sudo ldconfig -v` after `make install` to load the shared libraries correctly.

#### Parameter settings of the sg driver

LTFS uses the sg driver by default. You can improve reliability to change parameters of the sg driver below.

```
def_reserved_size=1048576
```

In RHEL7, you can put following file as `/etc/modprobe.d/sg.conf`.

```
options sg def_reserved_size=1048576
```

You can check current configuration of sg driver to see the file `/proc/scsi/sg/debug` like

```
$ cat /proc/scsi/sg/debug
max_active_device=44  def_reserved_size=32768
 >>> device=sg25 1:0:10:0   em=0 sg_tablesize=1024 excl=0 open_cnt=1
   FD(1): timeout=60000ms bufflen=524288 (res)sgat=16 low_dma=0
   cmd_q=1 f_packid=0 k_orphan=0 closed=0
     No requests active
 >>> device=sg26 1:0:10:1   em=0 sg_tablesize=1024 excl=0 open_cnt=1
   FD(1): timeout=60000ms bufflen=524288 (res)sgat=16 low_dma=0
   cmd_q=1 f_packid=0 k_orphan=0 closed=0
     No requests active
```

##### Performance improvement of the sg device

You can improve performance to change parameters of the sg driver below. But this option may cause I/O error reported as #144 in some HBAs.

```
allow_dio=1
```

In RHEL7, you can put following file as `/etc/modprobe.d/sg.conf`.

```
options sg allow_dio=1
```

At this time, we know following HBA's works correctly.

- QLogic 8Gb FC HBAs

And following HBA's doesn't work correctly.

- ATTO ExpressSAS H680
- Emulex FC HBAs (Some drivers work but some drivers dont work, see this [section](#note-for-the-lpfc-driver-emulex-fibre-hbas))

##### Note for the lpfc driver (Emulex Fibre HBAs)

In the lpfc driver (for Emulex Fibre HBAs), the table size of the scatter-gather is 64 by default. This configuration may cause I/O errors intermittently when `allow_dio=1` is set and scatter-gather table cannot be reserved. To avoid this error, you need to change the parameter `lpfc_sg_seg_cnt` to 256 or greater like below.

```
options lpfc lpfc_sg_seg_cnt=256
```

In some versions of the lpfc driver (for Emulex Fibre HBAs), the table size of the scatter-gather cannot be changed correctly. You can check the value is changed or not in `sg_tablesize` value in `/proc/scsi/sg/debug`. If you don't have a correct value (256 or greater) in `sg_tablesize`, removing `allow_dio=1` configuration of the sg driver is strongly recommended.

##### Note for buggy HBAs

LTFS doesn't support the HBAs which doesn't handle the transfer length of SCSI data by default. The reason is because the safety of the data but LTFS provides a option to relax this limitation.

You can use such kind of HBAs if you run the `configure` script with `--enable-buggy-ifs` option and build.

List of the HBA `--enable-buggy-ifs` is needed is below.

[HBA list require `--enable-buggy-ifs`](https://github.com/LinearTapeFileSystem/ltfs/wiki/HBA-info)

#### IBM lin_tape driver support

You need to add `--enable-lintape` as an argument of ./configure script if you want to build the backend for lin_tape. You also need to add `DEFAULT_TAPE=lin_tape` if you set the lin_tape backend as default backend.

#### Buildable distributions

  | Dist                              | Arch    | Status      |
  |:-:                                |:-:      |:-:          |
  | RHEL 8                            | x86_64  | OK          |
  | RHEL 8                            | ppc64le | OK          |
  | RHEL 7                            | x86_64  | OK          |
  | RHEL 7                            | ppc64le | OK          |
  | CentOS 8                          | x86_64  | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/CentOS8%20Build%20Job/badge.svg?branch=master)|
  | CentOS 8                          | ppc64le | Probably OK |
  | CentOS 7                          | x86_64  | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/CentOS7%20Build%20Job/badge.svg?branch=master)|
  | CentOS 7                          | ppc64le | Probably OK |
  | Fedora 28                         | x86_64  | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/Fedora28%20Build%20Job/badge.svg?branch=master)|
  | Ubuntu 16.04 LTS                  | x86_64  | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/Ubuntu%2016.04%20Build%20Job/badge.svg?branch=master)|
  | Ubuntu 16.04 LTS                  | ppc64le | Probably OK |
  | Ubuntu 18.04 LTS                  | x86_64  | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/Ubuntu%2018.04%20Build%20Job/badge.svg?branch=master)|
  | Ubuntu 18.04 LTS                  | ppc64le | Probably OK |
  | Ubuntu 20.04 LTS (Need icu-config)| x86_64  | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/Ubuntu%2020.04%20Build%20Job/badge.svg?branch=master)|
  | Debian 9                          | x86_64  | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/Debian9%20Build%20Job/badge.svg?branch=master)|
  | Debian 10 (Need icu-config)       | x86_64  | ![GH Action status](https://github.com/LinearTapeFileSystem/ltfs/workflows/Debian10%20Build%20Job/badge.svg?branch=master)|
  | ArchLinux 2018.08.01              | x86_64  | Not checked automatically |
  | ArchLinux 2018.12.31 (rolling)    | x86_64  | Not checked automatically|

Currently, automatic build checking is working on GitHub Actions and Travis CI.

For Ubuntu20.04 and Debian10, dummy `icu-config` is needed in the build machine. See Issue [#153](https://github.com/LinearTapeFileSystem/ltfs/issues/153).

### Build and install on OSX (macOS)

#### Recent Homedrew system setup

Before build on macOS, you need to configure the environment like below.

```
export ICU_PATH="/usr/local/opt/icu4c/bin"
export LIBXML2_PATH="/usr/local/opt/libxml2/bin"
export PKG_CONFIG_PATH="/usr/local/opt/icu4c/lib/pkgconfig:/usr/local/opt/libxml2/lib/pkgconfig"
export PATH="$PATH:$ICU_PATH:$LIBXML2_PATH"
```

#### Old Homedrew system setup
Before build on OSX (macOS), some include path adjustment is required.

```
brew link --force icu4c
brew link --force libxml2
```

#### Building LTFS
On OSX (macOS), snmp cannot be supported, you need to disable it on configure script. And may be, you need to specify LDFLAGS while running configure script to link some required frameworks, CoreFundation and IOKit.

```
./autogen.sh
LDFLAGS="-framework CoreFoundation -framework IOKit" ./configure --disable-snmp
make
make install
```

`./configure --help` shows various options for build and install.

#### Buildable systems

  | OS            | Xcode  | Package system | Status      |
  |:-:            |:-:     |:-:             |:-:          |
  | macOS 10.14.6 | 11.3   | Homebrew       | Probably OK |
  
### Build and install on FreeBSD

Note that on FreeBSD, the usual 3rd party man directory is /usr/local/man. Configure defaults to using /usr/local/share/man.  So, override it on the command line to avoid having man pages put in the wrong place.

```
./autogen.sh
./configure --prefix=/usr/local --mandir=/usr/local/man
make
make install
```

#### Buildable versions

  | Version | Arch    | Status      |
  |:-:      |:-:      |:-:          |
  | 11      | x86_64  | OK          |

### Build and install on NetBSD

```
./autogen.sh
./configure
make
make install
```

#### Buildable versions

  | Version | Arch    | Status      |
  |:-:      |:-:      |:-:          |
  | 8.1     | amd64   | OK          |
  | 8.0     | i386    | OK          |
  | 7.2     | amd64   | OK          |

## Contributing

Please read [CONTRIBUTING.md](.github/CONTRIBUTING.md) for details on our code of conduct, and the process for submitting pull requests to us.

## License

This project is licensed under the BSD License - see the [LICENSE](LICENSE) file for details
