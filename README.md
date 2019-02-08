![](https://img.shields.io/github/issues/lineartapefilesystem/ltfs.svg)
[![Build Status](https://travis-ci.org/LinearTapeFileSystem/ltfs.svg?branch=master)](https://travis-ci.org/LinearTapeFileSystem/ltfs)
[![CodeFactor](https://www.codefactor.io/repository/github/lineartapefilesystem/ltfs/badge)](https://www.codefactor.io/repository/github/lineartapefilesystem/ltfs)
[![BSD License](http://img.shields.io/badge/license-BSD-blue.svg?style=flat)](LICENSE)

# Linear Tape File System (LTFS)

Linear Tape File System (LTFS) is a filesystem to mount a LTFS formatted tape in a tape drive. Once LTFS mounts a LTFS formatted tape as filesystem, user can access to the tape via filesystem API.

Objective of this project is being the reference implementation of the LTFS format Specifications in SNIA (https://www.snia.org/tech_activities/standards/curr_standards/ltfs).

At this time, the LTFS format specifications 2.4 is the target (https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_2.4.0_TechPosition.pdf).

## Getting Started

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
  * NetBSD 7.0 or higher (ofor FUSE support)
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

LTFS uses the sg driver by default. You can improve performnce and reliability to change parameters of the sg driver below.

```
allow_dio=1
def_reserved_size=1048576
```

In RHEL7, you can put following file as `/etc/modprobe.d/sg.conf`.

```
options sg allow_dio=1
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
  | Debian 9.5                    | x86_64  | NG          |
  | ArchLinux 2018.08.01          | x86_64  | OK          |
  | ArchLinux 2018.12.31 (rolling)| x86_64  | OK          |

Currently, automatic build checking is working on Travis CI with Ubuntu 16.04 LTS.

In Debian9 (stretch), ICU 57.1 is used but the command `genrb` in this version causes crash while creating a resource bundle of LTFS. It is clearly a bug of ICU package and this problem was fixed into ICU 60 at least. May be we need to wait Debian10 (buster) because ICU 60 is used in Debian10 at this time (in the test phase of Debian10).

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

  | OS          | Xcode | Package system | Status      | 
  |:-:          |:-:    |:-:             |:-:          |
  | macOS 10.13 | 9.4.1 | Homebrew       | OK          |

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
  | 8.0     | i386    | OK          |

## How to use the LTFS

Goto [Wiki](https://github.com/LinearTapeFileSystem/ltfs/wiki) and enjoy the LTFS.

## Contributing

Please read [CONTRIBUTING.md](.github/CONTRIBUTING.md) for details on our code of conduct, and the process for submitting pull requests to us.

## License

This project is licensed under the BSD License - see the [LICENSE](LICENSE) file for details
