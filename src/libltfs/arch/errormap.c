/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2017 IBM Corp.
**
**  Redistribution and use in source and binary forms, with or without
**   modification, are permitted provided that the following conditions
**  are met:
**  1. Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**  2. Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in the
**  documentation and/or other materials provided with the distribution.
**  3. Neither the name of the copyright holder nor the names of its
**     contributors may be used to endorse or promote products derived from
**     this software without specific prior written permission.
**
**  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
**  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
**  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
**  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
**  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
**  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
**  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
**  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
**  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
**  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
**  POSSIBILITY OF SUCH DAMAGE.
**
**
**  OO_Copyright_END
**
*************************************************************************************
**
** COMPONENT NAME:  IBM Linear Tape File System
**
** FILE NAME:       errormap.c
**
** DESCRIPTION:     Platform-specific error code mapping implementation.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
*************************************************************************************
*/

#ifdef mingw_PLATFORM
#include "libltfs/arch/win/win_util.h"
#endif

#include <stdlib.h>
#include <errno.h>

#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_error.h"
#include "libltfs/uthash.h"
#include "libltfs/arch/errormap.h"

/* Use the Bernstein hash function, which has the best lookup performance for this
 * data set on amd64. */
#undef HASH_FCN
#define HASH_FCN HASH_BER

struct error_map {
	int ltfs_error;
	char *msd_id;
	int general_error;
	UT_hash_handle hh;
};

/* Hash table of libltfs -> FUSE error codes */
static struct error_map *fuse_errormap = NULL;

/** Map from libltfs error codes to appropriate FUSE errors.
 * This should be kept in sync with libltfs/ltfs_error.h.
 * TODO: define the corresponding error mapping for Windows.
 */
static struct error_map fuse_error_list[] = {
	{ LTFS_NULL_ARG,                 "I1000E", EINVAL},
	{ LTFS_NO_MEMORY,                "I1001E", ENOMEM},
	{ LTFS_MUTEX_INVALID,            "I1002E", EINVAL},
	{ LTFS_MUTEX_UNLOCKED,           "I1003E", EINVAL},
	{ LTFS_BAD_DEVICE_DATA,          "I1004E", EINVAL},
	{ LTFS_BAD_PARTNUM,              "I1005E", EINVAL},
	{ LTFS_LIBXML2_FAILURE,          "I1006E", EINVAL},
	{ LTFS_DEVICE_UNREADY,           "I1007E", EAGAIN},
#ifdef ENOMEDIUM
	{ LTFS_NO_MEDIUM,                "I1008E", ENOMEDIUM},
#else
	{ LTFS_NO_MEDIUM,                "I1008E", EAGAIN},
#endif /* ENOMEDIUM */
	{ LTFS_LARGE_BLOCKSIZE,          "I1009E", EINVAL},
	{ LTFS_BAD_LOCATE,               "I1010E", EIO},
	{ LTFS_NOT_PARTITIONED,          "I1011E", EINVAL},
	{ LTFS_LABEL_INVALID,            "I1012E", EINVAL},
	{ LTFS_LABEL_MISMATCH,           "I1013E", EINVAL},
	{ LTFS_INDEX_INVALID,            "I1014E", EINVAL},
	{ LTFS_INCONSISTENT,             "I1015E", EINVAL},
	{ LTFS_UNSUPPORTED_MEDIUM,       "I1016E", EINVAL},
	{ LTFS_GENERATION_MISMATCH,      "I1017E", EINVAL},
	{ LTFS_MAM_CACHE_INVALID,        "I1018E", EINVAL},
	{ LTFS_INDEX_CACHE_INVALID,      "I1019E", EINVAL},
	{ LTFS_POLICY_EMPTY_RULE,        "I1020E", EINVAL},
	{ LTFS_MUTEX_INIT,               "I1021E", EINVAL},
	{ LTFS_BAD_ARG,                  "I1022E", EINVAL},
	{ LTFS_NAMETOOLONG,              "I1023E", ENAMETOOLONG},
	{ LTFS_NO_DENTRY,                "I1024E", ENOENT},
	{ LTFS_INVALID_PATH,             "I1025E", EINVAL},
	{ LTFS_INVALID_SRC_PATH,         "I1026E", ENOENT},
	{ LTFS_DENTRY_EXISTS,            "I1027E", EEXIST},
	{ LTFS_DIRNOTEMPTY,              "I1028E", ENOTEMPTY},
	{ LTFS_UNLINKROOT,               "I1029E", EBUSY},
	{ LTFS_DIRMOVE,                  "I1030E", EIO},
	{ LTFS_RENAMELOOP,               "I1031E", EINVAL},
	{ LTFS_SMALL_BLOCK,              "I1032E", EIO},
	{ LTFS_ISDIRECTORY,              "I1033E", EINVAL},
	{ LTFS_EOD_MISSING_MEDIUM,       "I1034E", EINVAL},
	{ LTFS_BOTH_EOD_MISSING,         "I1035E", EIO},
	{ LTFS_UNEXPECTED_VALUE,         "I1036E", EIO},
	{ LTFS_UNSUPPORTED,              "I1037E", EIO},
	{ LTFS_LABEL_POSSIBLE_VALID,     "I1038E", EIO},
	{ LTFS_CLOSE_FS_IF,              "I1039E", EIDRM},
#ifdef ENOATTR
	{ LTFS_NO_XATTR,                 "I1040E", ENOATTR},
#else
	{ LTFS_NO_XATTR,                 "I1040E", ENODATA},
#endif /* ENOATTR */
	{ LTFS_SIG_HANDLER_ERR,          "I1041E", EINVAL},
	{ LTFS_INTERRUPTED,              "I1042E", ECANCELED},
	{ LTFS_UNSUPPORTED_INDEX_VERSION,"I1043E", EINVAL},
	{ LTFS_ICU_ERROR,                "I1044E", EINVAL},
	{ LTFS_PLUGIN_LOAD,              "I1045E", EINVAL},
	{ LTFS_PLUGIN_UNLOAD,            "I1046E", EINVAL},
	{ LTFS_RDONLY_XATTR,             "I1047E", EACCES},
	{ LTFS_XATTR_EXISTS,             "I1048E", EEXIST},
	{ LTFS_SMALL_BUFFER,             "I1049E", ERANGE},
	{ LTFS_RDONLY_VOLUME,            "I1050E", EROFS},
	{ LTFS_NO_SPACE,                 "I1051E", ENOSPC},
	{ LTFS_LARGE_XATTR,              "I1052E", ENOSPC},
	{ LTFS_NO_INDEX,                 "I1053E", ENODATA},
	{ LTFS_XATTR_NAMESPACE,          "I1054E", EOPNOTSUPP},
	{ LTFS_CONFIG_INVALID,           "I1055E", EINVAL},
	{ LTFS_PLUGIN_INCOMPLETE,        "I1056E", EINVAL},
	{ LTFS_NO_PLUGIN,                "I1057E", ENOENT},
	{ LTFS_POLICY_INVALID,           "I1058E", EINVAL},
	{ LTFS_ISFILE,                   "I1059E", ENOTDIR},
	{ LTFS_UNRESOLVED_VOLUME,        "I1060E", EBUSY},
	{ LTFS_POLICY_IMMUTABLE,         "I1061E", EPERM},
	{ LTFS_SMALL_BLOCKSIZE,          "I1062E", EINVAL},
	{ LTFS_BARCODE_LENGTH,           "I1063E", EINVAL},
	{ LTFS_BARCODE_INVALID,          "I1064E", EINVAL},
	{ LTFS_RESOURCE_SHORTAGE,        "I1065E", EBUSY},
	{ LTFS_DEVICE_FENCED,            "I1066E", EAGAIN},
	{ LTFS_REVAL_RUNNING,            "I1067E", EAGAIN},
	{ LTFS_REVAL_FAILED,             "I1068E", EFAULT},
	{ LTFS_SLOT_FULL,                "I1069E", EFAULT},
	{ LTFS_SLOT_SHORTAGE,            "I1070E", EFAULT},
	{ LTFS_CHANGER_ERROR,            "I1071E", EIO},
	{ LTFS_UNEXPECTED_TAPE,          "I1072E", EINVAL},
	{ LTFS_NO_HOMESLOT,              "I1073E", EINVAL},
	{ LTFS_MOVE_ACTIVE_CART,         "I1074E", ECANCELED},
	{ LTFS_NO_IE_SLOT,               "I1075E", ECANCELED},
	{ LTFS_INVALID_SLOT,             "I1076E", EINVAL},
	{ LTFS_UNSUPPORTED_CART,         "I1077E", EINVAL},
	{ LTFS_CART_STUCKED,             "I1078E", EIO},
	{ LTFS_OP_NOT_ALLOWED,           "I1079E", EINVAL},
	{ LTFS_OP_TO_DUP,                "I1080E", EINVAL},
	{ LTFS_OP_TO_NON_SUP,            "I1081E", EINVAL},
	{ LTFS_OP_TO_INACC,              "I1082E", EINVAL},
	{ LTFS_OP_TO_UNFMT,              "I1083E", EINVAL},
	{ LTFS_OP_TO_INV,                "I1084E", EINVAL},
	{ LTFS_OP_TO_ERR,                "I1085E", EINVAL},
	{ LTFS_OP_TO_CRIT,               "I1086E", EINVAL},
	{ LTFS_OP_TO_CLN,                "I1087E", EINVAL},
	{ LTFS_OP_TO_RO,                 "I1088E", EINVAL},
	{ LTFS_ALREADY_FS_INC,           "I1089E", EINVAL},
	{ LTFS_NOT_IN_FS,                "I1090E", EINVAL},
	{ LTFS_FS_CART_TO_IE,            "I1091E", EINVAL},
	{ LTFS_OP_TO_UNKN,               "I1092E", EINVAL},
	{ LTFS_DRV_LOCKED,               "I1093E", EINVAL},
	{ LTFS_DRV_ALRDY_ADDED,          "I1094E", EINVAL},
	{ LTFS_FORCE_INVENTORY,          "I1095E", EIO},
	{ LTFS_INVENTORY_FAILED,         "I1096E", EFAULT},
	{ LTFS_RESTART_OPERATION,        "I1097E", EIO},
	{ LTFS_NO_TARGET_DRIVE,          "I1098E", EINVAL},
	{ LTFS_NO_DCACHE_FSTYPE,         "I1099E", EINVAL},
	{ LTFS_IMAGE_EXISTED,            "I1100E", EINVAL},
	{ LTFS_IMAGE_MOUNTED,            "I1101E", EIO},
	{ LTFS_IMAGE_NOT_MOUNTED,        "I1102E", EIO},
	{ LTFS_MTAB_NOREGULAR,           "I1103E", EIO},
	{ LTFS_MTAB_OPEN,                "I1104E", EIO},
	{ LTFS_MTAB_LOCK,                "I1105E", EIO},
	{ LTFS_MTAB_SEEK,                "I1106E", EIO},
	{ LTFS_MTAB_UPDATE,              "I1107E", EIO},
	{ LTFS_MTAB_FLUSH,               "I1108E", EIO},
	{ LTFS_MTAB_UNLOCK,              "I1109E", EIO},
	{ LTFS_MTAB_CLOSE,               "I1110E", EIO},
	{ LTFS_MTAB_COPY,                "I1111E", EIO},
	{ LTFS_MTAB_TEMP_OPEN,           "I1112E", EIO},
	{ LTFS_MTAB_TEMP_SEEK,           "I1113E", EIO},
	{ LTFS_DCACHE_CREATION_FAIL,     "I1114E", EIO},
	{ LTFS_DCACHE_UNSUPPORTED,       "I1115E", EINVAL},
	{ LTFS_DCACHE_EXTRA_SPACE,       "I1116E", EINVAL},
	{ LTFS_KEY_NOT_FOUND,            "I1117E", EINVAL},
	{ LTFS_INVALID_SEQUENCE,         "I1118E", EINVAL},
	{ LTFS_RDONLY_ROOT,              "I1119E", EACCES},
	{ LTFS_SYMLINK_CONFLICT,         "I1120E", EIO},
	{ LTFS_NETWORK_INIT_FAIL,        "I1121E", EINVAL},
	{ LTFS_DRIVE_SHORTAGE,           "I1122E", ENODEV},
	{ LTFS_INVALID_VOLSER,           "I1123E", EINVAL},
	{ LTFS_LESS_SPACE,               "I1124E", ENOSPC},
	{ LTFS_WRITE_PROTECT,            "I1125E", EROFS},
	{ LTFS_WRITE_ERROR,              "I1126E", EROFS},
	{ LTFS_UNEXPECTED_BARCODE,       "I1127E", EIO},
	{ LTFS_STRING_CONVERSION,        "I1128E", EINVAL},
	{ LTFS_SESSION_INIT_FAIL,        "I1129E", EIO},
	{ LTFS_MESSAGE_INVALID,          "I1130E", EINVAL},
	{ LTFS_PASSWORD_INVALID,         "I1131E", EPERM},
	{ LTFS_NOT_AUTHENTICATERD,       "I1132E", EINVAL},
	{ LTFS_WORM_DEEP_RECOVERY,       "I1133E", EINVAL},
	{ LTFS_WORM_ROLLBACK,            "I1134E", EINVAL},
	{ LTFS_NONWORM_SALVAGE,          "I1135E", EINVAL},
	{ LTFS_FORMATTED,                "I1136E", EPERM},
	{ LTFS_RULES_WORM,               "I1137E", EINVAL},
	{ LTFS_BAD_BLOCKSIZE,            "I1138E", EINVAL},
	{ LTFS_BAD_VOLNAME,              "I1139E", EINVAL},
	{ LTFS_BAD_RULES,                "I1140E", EINVAL},
	{ LTFS_GEN_NEEDED,               "I1141E", EINVAL},
	{ LTFS_BAD_GENERATION,           "I1142E", EINVAL},
	{ LTFS_NO_ROLLBACK_TARGET,       "I1143E", EINVAL},
	{ LTFS_MANY_INDEXES,             "I1144E", EINVAL},
	{ LTFS_SALVAGE_NOT_NEEDED,       "I1145E", EINVAL},
	{ LTFS_WORM_ENABLED,             "I1146E", EACCES},
	{ LTFS_OUTSTANDING_REFS,         "I1147E", EBUSY},
	{ LTFS_REBUILD_IN_PROGRESS,      "I1148E", EBUSY},
	{ LTFS_MULTIPLE_START,           "I1149E", EINVAL},
	{ LTFS_CARTRIDGE_NOT_FOUND,      "I1150E", EINVAL},
	{ LTFS_CACHE_LOCK_ERR,           "I1151E", EIO},
	{ LTFS_CACHE_UNLOCK_ERR,         "I1152E", EIO},
	{ LTFS_CREPO_FILE_ERR,           "I1153E", EIO},
	{ LTFS_CREPO_READ_ERR,           "I1154E", EIO},
	{ LTFS_CREPO_WRITE_ERR,          "I1155E", EIO},
	{ LTFS_CREPO_INVALID_OP,         "I1156E", EINVAL},
	{ LTFS_FILE_ERR,                 "I1157E", EIO},
	{ LTFS_CARTRIDGE_IN_USE,         "I1158E", EBUSY},
	{ LTFS_NO_LOCK_ENTRY,            "I1159E", ENOENT},
	{ LTFS_MOUNT_ERR,                "I1160E", EIO},
	{ LTFS_NO_DEVICE,                "I1161E", ENODEV},
	{ LTFS_XATTR_ERR,                "I1162E", EIO},
	{ LTFS_FTW_ERR,                  "I1163E", EIO},
	{ LTFS_TIME_ERR,                 "I1164E", EIO},
	{ LTFS_NOT_BLOCK_DEVICE,         "I1165E", ENOTBLK},
	{ LTFS_QUOTA_EXCEEDED,           "I1166E", EDQUOT},
	{ LTFS_TOO_MANY_OPEN_FILES,      "I1167E", ENFILE},
	{ LTFS_LINKDIR_EXISTS,           "I1168E", EEXIST},
	{ LTFS_NO_DMAP_ENTRY,            "I1169E", ENOENT},
	{ LTFS_RECOVERABLE_FILE_ERR,     "I1170E", EAGAIN},
	{ LTFS_NO_DCACHE_SPC,            "I1171E", ENOSPC},
	/* Unused 1175 - 1180 */
	{ LTFS_CACHE_DISCARDED,          "I1181E", ENOENT },
	{ LTFS_LONG_WRITE_LOCK,          "I1182E", EAGAIN },
	{ LTFS_INCOMPATIBLE_CACHE,       "I1183E", EINVAL },
	{ LTFS_DCACHE_NOT_INITIALIZED,   "I1184E", EIO },
	{ LTFS_CONFIG_FILE_WLOCKED,      "I1185E", EINVAL },
	{ LTFS_CREATE_QUEUE,             "I1186E", EIO },
	{ LTFS_FORK_ERROR,               "I1187E", EIO },
	{ LTFS_NOACK,                    "I1188E", EIO },
	{ LTFS_NODE_DETECT_FAIL,         "I1189E", EIO },
	{ LTFS_INVALID_MESSAGE,          "I1190E", EIO },
	{ LTFS_NODE_DEGATE_FAIL,         "I1191E", EIO },
	{ LTFS_CLUSTER_MRSW_FAIL,        "I1192E", EIO },
	{ LTFS_CART_NOT_MOUNTED,         "I1193E", EBUSY},
	{ LTFS_RDONLY_CART_DRV,          "I1194E", EINVAL},
	{ LTFS_NEED_DRIVE_SELECTION,     "I1195E", EINVAL},
	{ LTFS_MUTEX_ALREADY_LOCKED,     "I1196E", EINVAL},
	{ LTFS_TAPE_UNDER_PROCESS,       "I1197E", EBUSY},
	{ LTFS_TAPE_REMOVED,             "I1198E", EIDRM},
	{ LTFS_NEED_MOVE,                "I1199E", EINVAL},
	{ LTFS_NEED_START_OVER,          "I1200E", EINVAL},

	{ EDEV_NO_SENSE,                 "D0000E", EIO},
	{ EDEV_OVERRUN,                  "D0002E", EIO},
	{ EDEV_UNDERRUN,                 "D0003E", ENODATA},
	{ EDEV_FILEMARK_DETECTED,        "D0004E", EIO},
	{ EDEV_EARLY_WARNING,            "D0005E", EIO},
	{ EDEV_BOP_DETECTED,             "D0006E", EIO},
	{ EDEV_PROG_EARLY_WARNING,       "D0007E", EIO},
	{ EDEV_CLEANING_CART,            "D0008E", EINVAL},
	{ EDEV_VOLTAG_NOT_READABLE,      "D0009E", EINVAL},
	{ EDEV_LOCATION_NOT_PRESENT,     "D0010E", EINVAL},
	{ EDEV_MEDIA_PRESENSE_UNKNOWN,   "D0011E", EINVAL},
	{ EDEV_SLOT_UNKNOWN_STATE,       "D0012E", EINVAL},
	{ EDEV_DRIVE_NOT_PRESENT,        "D0013E", EINVAL},
	{ EDEV_RECORD_NOT_FOUND,         "D0014E", ESPIPE},
	{ EDEV_INSUFFICIENT_TIME,        "D0015E", EIO},
#ifdef EUCLEAN
	{ EDEV_CLEANING_REQUIRED,        "D0098E", EUCLEAN},
#else
	{ EDEV_CLEANING_REQUIRED,        "D0098E", EAGAIN},
#endif
	{ EDEV_RECOVERED_ERROR,          "D0100E", EIO},
	{ EDEV_MODE_PARAMETER_ROUNDED,   "D0101E", EIO},
	{ EDEV_DEGRADED_MEDIA,           "D0198E", EIO},
	{ EDEV_NOT_READY,                "D0200E", EAGAIN},
	{ EDEV_NOT_REPORTABLE,           "D0201E", EAGAIN},
	{ EDEV_BECOMING_READY,           "D0202E", EAGAIN},
	{ EDEV_NEED_INITIALIZE,          "D0203E", EIO},
	{ EDEV_MANUAL_INTERVENTION,      "D0204E", EAGAIN},
	{ EDEV_OPERATION_IN_PROGRESS,    "D0205E", EAGAIN},
	{ EDEV_OFFLINE,                  "D0206E", EAGAIN},
	{ EDEV_DOOR_OPEN,                "D0207E", EAGAIN},
	{ EDEV_OVER_TEMPERATURE,         "D0208E", EAGAIN},
#ifdef ENOMEDIUM
	{ EDEV_NO_MEDIUM,                "D0209E", ENOMEDIUM},
#else
	{ EDEV_NO_MEDIUM,                "D0209E", EAGAIN},
#endif /* ENOMEDIUM */
	{ EDEV_NOT_SELF_CONFIGURED_YET,  "D0210E", EAGAIN},
	{ EDEV_PARAMETER_VALUE_REJECTED, "D0211E", EINVAL},
	{ EDEV_CLEANING_IN_PROGRESS,     "D0297E", EAGAIN},
	{ EDEV_IE_OPEN,                  "D0298E", EAGAIN},
	{ EDEV_MEDIUM_ERROR,             "D0300E", EIO},
	{ EDEV_RW_PERM,                  "D0301E", EIO},
	{ EDEV_CM_PERM,                  "D0302E", EIO},
	{ EDEV_MEDIUM_FORMAT_ERROR,      "D0303E", EIO},
	{ EDEV_MEDIUM_FORMAT_CORRUPTED,  "D0304E", EIO},
	{ EDEV_INTEGRITY_CHECK,          "D0305E", EILSEQ},
	{ EDEV_LOAD_UNLOAD_ERROR,        "D0306E", EIO},
	{ EDEV_CLEANING_FALIURE,         "D0307E", EIO},
	{ EDEV_READ_PERM,                "D0308E", EIO},
	{ EDEV_WRITE_PERM,               "D0309E", EIO},
	{ EDEV_HARDWARE_ERROR,           "D0400E", EIO},
	{ EDEV_LBP_WRITE_ERROR,          "D0401E", EIO},
	{ EDEV_LBP_READ_ERROR,           "D0402E", EIO},
	{ EDEV_ILLEGAL_REQUEST,          "D0500E", EILSEQ},
	{ EDEV_INVALID_FIELD_CDB,        "D0501E", EILSEQ},
	{ EDEV_DEST_FULL,                "D0502E", EIO},
	{ EDEV_SRC_EMPTY,                "D0503E", EIO},
	{ EDEV_MAGAZINE_INACCESSIBLE,    "D0504E", EIO},
	{ EDEV_INVALID_ADDRESS,          "D0505E", EIDRM},
	{ EDEV_MEDIUM_LOCKED,            "D0506E", EIO},
	{ EDEV_UNIT_ATTENTION,           "D0600E", EIO},
	{ EDEV_MEDIUM_MAY_BE_CHANGED,    "D0601E", EIO},
	{ EDEV_IE_ACCESSED,              "D0602E", EIO},
	{ EDEV_POR_OR_BUS_RESET,         "D0603E", EIO},
	{ EDEV_CONFIGURE_CHANGED,        "D0604E", EIO},
	{ EDEV_COMMAND_CLEARED,          "D0605E", EIO},
	{ EDEV_MEDIUM_REMOVAL_REQ,       "D0606E", EIO},
	{ EDEV_MEDIA_REMOVAL_PREV,       "D0607E", EIO},
	{ EDEV_DOOR_CLOSED,              "D0608E", EIO},
	{ EDEV_TIME_STAMP_CHANGED,       "D0609E", EIO},
	{ EDEV_RESERVATION_PREEMPTED,    "D0610E", EIO},
	{ EDEV_RESERVATION_RELEASED,     "D0611E", EIO},
	{ EDEV_REGISTRATION_PREEMPTED,   "D0612E", EIO},
	{ EDEV_DATA_PROTECT,             "D0700E", EIO},
	{ EDEV_WRITE_PROTECTED,          "D0701E", EIO},
	{ EDEV_WRITE_PROTECTED_WORM,     "D0702E", EIO},
	{ EDEV_WRITE_PROTECTED_OPERATOR, "D0703E", EIO},
	{ EDEV_BLANK_CHECK,              "D0800E", EIO},
	{ EDEV_EOD_DETECTED,             "D0801E", ESPIPE},
	{ EDEV_EOD_NOT_FOUND,            "D0802E", ESPIPE},
	{ EDEV_ABORTED_COMMAND,          "D1100E", EIO},
	{ EDEV_OVERLAPPED,               "D1101E", EIO},
	{ EDEV_TIMEOUT,                  "D1102E", ETIMEDOUT},
	{ EDEV_OVERFLOW,                 "D1300E", EIO},
	{ EDEV_CRYPTO_ERROR,             "D1600E", EIO},
	{ EDEV_KEY_SERVICE_ERROR,        "D1601E", EIO},
	{ EDEV_KEY_CHANGE_DETECTED,      "D1602E", EIO},
	{ EDEV_KEY_REQUIRED,             "D1603E", EIO},
	{ EDEV_INTERNAL_ERROR,           "D1700E", EIO},
	{ EDEV_DRIVER_ERROR,             "D1701E", EIO},
	{ EDEV_HOST_ERROR,               "D1702E", EIO},
	{ EDEV_TARGET_ERROR,             "D1703E", EIO},
	{ EDEV_DRIVER_ERROR,             "D1701E", EIO},
	{ EDEV_NO_MEMORY,                "D1704E", EIO},
	{ EDEV_UNSUPPORTED_FUNCTION,     "D1705E", EIO},
	{ EDEV_PARAMETER_NOT_FOUND,      "D1706E", EIO},
	{ EDEV_CANNOT_GET_SENSE,         "D1707E", EIO},
	{ EDEV_INVALID_ARG,              "D1708E", EINVAL},
	{ EDEV_DUMP_EIO,                 "D1709E", EIO},
	{ EDEV_UNKNOWN,                  "D9998E", EIO},
	{ EDEV_VENDOR_UNIQUE,            "D9999E", EIO},
	{ EDEV_DEVICE_BUSY,              "D1710E", EAGAIN},
	{ EDEV_DEVICE_UNOPENABLE,        "D1711E", EIO},
	{ EDEV_DEVICE_UNSUPPORTABLE,     "D1712E", EOPNOTSUPP},
	{ EDEV_INVALID_LICENSE,          "D1713E", EOPNOTSUPP},
	{ EDEV_UNSUPPORTED_FIRMWARE,     "D1714E", EOPNOTSUPP},
	{ EDEV_UNSUPPORETD_COMMAND,      "D1715E", EOPNOTSUPP},
	{ EDEV_LENGTH_MISMATCH,          "D1716E", EINVAL},
	{ EDEV_BUFFER_OVERFLOW,          "D1717E", EINVAL},
	{ EDEV_DRIVES_MISMATCH,          "D1718E", EINVAL},
	{ EDEV_RESERVATION_CONFLICT,     "D1719E", EIO},
	{ EDEV_CONNECTION_LOST,          "D1720E", EIO},
	{ EDEV_NO_RESERVATION_HOLDER,    "D1721E", EIO},
	{ EDEV_NEED_FAILOVER,            "D1722E", EIO},
	{ EDEV_REAL_POWER_ON_RESET,      "D1723E", EIO},
	{ -1, NULL, 0 }
};

int errormap_init()
{
	struct error_map *err;

	HASH_ADD_INT(fuse_errormap, ltfs_error, fuse_error_list);
	if (! fuse_errormap) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}
	for (err=fuse_error_list+1; err->ltfs_error!=-1; ++err)
		HASH_ADD_INT(fuse_errormap, ltfs_error, err);

	return 0;
}

void errormap_finish()
{
	HASH_CLEAR(hh, fuse_errormap);
}

int errormap_fuse_error(int val)
{
	struct error_map *out;

	val = -val;
	if (val < LTFS_ERR_MIN)
		return -val;

	HASH_FIND_INT(fuse_errormap, &val, out);
	if (out)
		return -out->general_error;

	return -EIO;
}

char* errormap_msg_id(int val)
{
	struct error_map *out;

	val = -val;
	if (val < LTFS_ERR_MIN)
		return NULL;

	HASH_FIND_INT(fuse_errormap, &val, out);
	if (out)
		return out->msd_id;

	return NULL;
}
