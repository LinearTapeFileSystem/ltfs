![](https://img.shields.io/github/issues/lineartapefilesystem/ltfs.svg)
[![Build Status](https://travis-ci.org/LinearTapeFileSystem/ltfs.svg?branch=master)](https://travis-ci.org/LinearTapeFileSystem/ltfs)
[![CodeFactor](https://www.codefactor.io/repository/github/lineartapefilesystem/ltfs/badge)](https://www.codefactor.io/repository/github/lineartapefilesystem/ltfs)
[![BSD License](http://img.shields.io/badge/license-BSD-blue.svg?style=flat)](LICENSE)

# Linear Tape File System (LTFS)

Linear Tape File System (LTFS) is a filesystem to mount a LTFS formatted tape in a tape drive. Once LTFS mounts a LTFS formatted tape as filesystem, user can access to the tape via filesystem API.

Objective of this project is being the reference implementation of the LTFS format Specifications in [SNIA](https://www.snia.org/tech_activities/standards/curr_standards/ltfs).

At this time, the target of this project to meet is the LTFS format specifications 2.4. (https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_2.4.0_TechPosition.pdf).

## LTFS Format Specifications

LTFS Format Specification is specified data placement, shape of index and names of extended attributes for LTFS. This specification is defined in [SNIA](https://www.snia.org/tech_activities/standards/curr_standards/ltfs) first and then it is forwarded to [ISO](https://www.iso.org/home.html) as ISO/IEC 20919 from version 2.2.

The table below show status of the LTFS format Specification 

  | Version | Status of SNIA                   | Status of ISO   |
  |:-:      |:-:                               |:-:              |
  | 2.2     | [Published the Technical Position](http://snia.org/sites/default/files/LTFS_Format_2.2.0_Technical_Position.pdf) | Published       |
  | 2.3.1   | [Published the Technical Position](https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_2.3.1_TechPosition.pdf) | -               |
  | 2.4     | [Published the Technical Position](https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_2.4.0_TechPosition.pdf) | On going        |
  | 2.5     | [Published the Technical Position](https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_v2.5_Technical_Position.pdf) | Not started yet | 

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

  | Vendor | Drive Type | Minimum F/W Level |
  |:-:     |:-:         |:-:                |
  | IBM    | LTO5       | B170              |
  | IBM    | LTO6       | None              |
  | IBM    | LTO7       | None              |
  | IBM    | LTO8       | HB81              |
  | IBM    | TS1140     | 3694              |
  | IBM    | TS1150     | None              |
  | IBM    | TS1155     | None              |
  | IBM    | TS1160     | None              |

## Installing

### Build and install on Linux

```
./autogen.sh
./configure
make
make install
```

`./configure --help` shows various options for build and install.

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

##### Note for the lpfc driver (Emulex Fibre HBAs)

In the lpfc driver (for Emulex Fibre HBAs), the table size of the scutter-gather is 64 by default. This cofiguration may cause I/O errors intermittenly when `allow_dio=1` is set and scutter-gather table cannot be reserved. To avouid this error, you need to change the parameter `lpfc_sg_seg_cnt` to 256 or larger like below.

```
options lpfc lpfc_sg_seg_cnt=256 
```

In some versions of the lpfc driver (for Emulex Fibre HBAs), the table size of the scutter-gather cannot be changed correctly. You can check the value is changed or not in `sg_tablesize` value in `/proc/scsi/sg/debug`. If you don't have a correct value (256 or later) in `sg_tablesize`, removing `allow_dio=1` configuration of the sg driver is strongly recommended. 

##### Performnce improvement of the sg device

On some HBA's, you can improve reliability to change parameters of the sg driver below. But this option may cause IO error reported as #144.

```
allow_dio=1
```

In RHEL7, you can put following file as `/etc/modprobe.d/sg.conf`.

```
options sg allow_dio=1
```

At this time, following HBA's may work correctly.

- QLogic 8Gb FC HBAs

And following HBA's doen't work correctly.

- ATTO ExpressSAS H680
- Emulex FC HBAs

#### IBM lin_tape driver support

You need to add `--enable-lintape` as an argument of ./configure script if you want to build the backend for lin_tape. You also need to add `DEFAULT_TAPE=lin_tape` if you set the lin_tape backend as default backend.

#### Buildable distributions

  | Dist                          | Arch    | Status      | 
  |:-:                            |:-:      |:-:          | 
  | RHEL 7                        | x86_64  | OK          |
  | RHEL 7                        | ppc64le | OK          |
  | CentOS 7                      | x86_64  | OK          |
  | CentOS 7                      | ppc64le | Probably OK |  
  | Fedora 28                     | x86_64  | OK          |
  | Ubuntu 16.04 LTS              | x86_64  | [![Build Status](https://travis-ci.org/LinearTapeFileSystem/ltfs.svg?branch=master)](https://travis-ci.org/LinearTapeFileSystem/ltfs)|
  | Ubuntu 18.04 LTS              | x86_64  | OK          |
  | Debian 9.10                   | x86_64  | OK          |
  | ArchLinux 2018.08.01          | x86_64  | OK          |
  | ArchLinux 2018.12.31 (rolling)| x86_64  | OK          |

Currently, automatic build checking is working on Travis CI with Ubuntu 16.04 LTS.

### Build and install on OSX (macOS)

Before build on OSX (macOS), some include path adjustment is required.

```
brew link --force icu4c
brew link --force libxml2
```

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
  | macOS 10.13   |  9.4.1 | Homebrew       | OK          |
  | macOS 10.14.5 | 10.2.1 | Homebrew       | OK          |

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
