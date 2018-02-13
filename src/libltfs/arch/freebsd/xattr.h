#ifndef LIBLTFS_ARCH_FREEBSD_XATTR_H
#define LIBLTFS_ARCH_FREEBSD_XATTR_H

enum
{
  XATTR_CREATE = 1,     /* set value, fail if attr already exists.  */
#define XATTR_CREATE    XATTR_CREATE
  XATTR_REPLACE = 2     /* set value, fail if attr does not exist.  */
#define XATTR_REPLACE   XATTR_REPLACE
};

#endif /* LIBLTFS_ARCH_FREEBSD_XATTR_H */
