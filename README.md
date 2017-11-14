# Linear Tape File System (LTFS)

Linear Tape File System (LTFS) is a filesystem to mount a LTFS formatted tape in a tape drive. Once LTFS mounts a LTFS formatted tape as filesystem, user can access to the tape via filesystem API.

Objective of this project is being the reference implementation of the LTFS format Specifications in SNIA (https://www.snia.org/tech_activities/standards/curr_standards/ltfs).

At this time, the LTFS format specifications 2.4 is the target. The LTFS format specification 2.4 is now under public review (https://www.snia.org/tech_activities/publicreview#ltfs).

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

## Installing

### Build and install on Linux

```
./autogen.sh
./configure
make
make install
```

`./configure --help` shows various options for build and install.

#### IBM lin_tape driver support

You need to add `--enable-lintappe` as an argument of ./configure script if you want to build the backend for lin_tape. You also need to add `DEFAULT_TAPE=lin_tape` if you set the lin_tape backend as default backend.

### Build and install on OSX (macOS)

Before build on OSX (macOS), some include path adjustment is required.

```
brew link --force icu4c
brew link --force libxml2
```

On OSX (macOS), snmp cannot be supported. Need to disable it on configure script.

```
./autogen.sh
./configure --disable-snmp
make
make install
```

`./configure --help` shows various options for build and install.

## Contributing

Please read [CONTRIBUTING.md](.github/CONTRIBUTING.md) for details on our code of conduct, and the process for submitting pull requests to us.

## License

This project is licensed under the BSD License - see the [LICENSE](LICENSE) file for details
