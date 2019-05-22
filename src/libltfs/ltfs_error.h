/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2019 IBM Corp. All rights reserved.
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
** FILE NAME:       ltfs_error.h
**
** DESCRIPTION:     Error codes for libltfs.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __ltfs_error_h__
#define __ltfs_error_h__

#define LTFS_ERR_MIN              1000  /* First error value defined by this file */

#define LTFS_NULL_ARG             1000  /* Unexpected NULL function argument */
#define LTFS_NO_MEMORY            1001  /* Memory allocation failed */
#define LTFS_MUTEX_INVALID        1002  /* Attempted to lock or unlock an uninitialized mutex */
#define LTFS_MUTEX_UNLOCKED       1003  /* Attempted to unlock an already unlocked mutex */
#define LTFS_BAD_DEVICE_DATA      1004  /* Invalid device data structure */
#define LTFS_BAD_PARTNUM          1005  /* Invalid partition number requested */
#define LTFS_LIBXML2_FAILURE      1006  /* A libxml2 call failed */
#define LTFS_DEVICE_UNREADY       1007  /* Device is not ready */
#define LTFS_NO_MEDIUM            1008  /* No medium present in the device */
#define LTFS_LARGE_BLOCKSIZE      1009  /* Device does not support the formatted blocksize */
#define LTFS_BAD_LOCATE           1010  /* Locate resulted in an unexpected position */
#define LTFS_NOT_PARTITIONED      1011  /* Medium contains only one partition */
#define LTFS_LABEL_INVALID        1012  /* Invalid partition label */
#define LTFS_LABEL_MISMATCH       1013  /* Partition labels do not match */
#define LTFS_INDEX_INVALID        1014  /* Invalid XML index or index backpointer chain. Recovery might be possible. */
#define LTFS_INCONSISTENT         1015  /* Volume is inconsistent but recoverable */
#define LTFS_UNSUPPORTED_MEDIUM   1016  /* Medium cannot be partitioned or is otherwise incompatible */
#define LTFS_GENERATION_MISMATCH  1017  /* Index generation mismatch between cached copy and the tape */
#define LTFS_MAM_CACHE_INVALID    1018  /* MAM cache is invalid */
#define LTFS_INDEX_CACHE_INVALID  1019  /* Index Cache is invalid */
#define LTFS_POLICY_EMPTY_RULE    1020  /* Empty name rule encountered during policy parsing */
#define LTFS_MUTEX_INIT           1021  /* Mutex initialization failed */
#define LTFS_BAD_ARG              1022  /* Generic error for an invalid function argument */
#define LTFS_NAMETOOLONG          1023  /* A path name component was too long */
#define LTFS_NO_DENTRY            1024  /* No such file or directory */
#define LTFS_INVALID_PATH         1025  /* Path cannot be UTF-8 encoded or contains invalid characters */
#define LTFS_INVALID_SRC_PATH     1026  /* Like LTFS_INVALID_PATH, but for paths the caller expects to exist */
#define LTFS_DENTRY_EXISTS        1027  /* Target path exists and cannot remove it */
#define LTFS_DIRNOTEMPTY          1028  /* Cannot remove non-empty directory */
#define LTFS_UNLINKROOT           1029  /* Cannot remove the root directory */
#define LTFS_DIRMOVE              1030  /* Cannot move directory due to MacFUSE bug */
#define LTFS_RENAMELOOP           1031  /* Cannot rename directory underneath itself */
#define LTFS_SMALL_BLOCK          1032  /* Block read from tape is smaller than expected */
#define LTFS_ISDIRECTORY          1033  /* Operation is only valid on files */
#define LTFS_EOD_MISSING_MEDIUM   1034  /* Medium has no EOD in one partition */
#define LTFS_BOTH_EOD_MISSING     1035  /* Medium has no EOD in both partitions */
#define LTFS_UNEXPECTED_VALUE     1036  /* Detect Unexpected Value in LTFS in itself */
#define LTFS_UNSUPPORTED          1037  /* Call unsupported method */
#define LTFS_LABEL_POSSIBLE_VALID 1038  /* ANSI Label may be valid but length is wrong */
#define LTFS_CLOSE_FS_IF          1039  /* Not a valid tape remove cartridge directory */
#define LTFS_NO_XATTR             1040  /* No extended attribute found */
#define LTFS_SIG_HANDLER_ERR      1041  /* Failed to set signal handler */
#define LTFS_INTERRUPTED          1042  /* Catch signals to terminate process */
#define LTFS_UNSUPPORTED_INDEX_VERSION 1043  /* Unsupported version is detected into index */
#define LTFS_ICU_ERROR            1044  /* Received an unexpected error from ICU */
#define LTFS_PLUGIN_LOAD          1045  /* Error while loading a plug-in */
#define LTFS_PLUGIN_UNLOAD        1046  /* Error while unloading a plug-in */
#define LTFS_RDONLY_XATTR         1047  /* Cannot modify read-only extended attribute */
#define LTFS_XATTR_EXISTS         1048  /* EA exists, and the user asked not to overwrite it */
#define LTFS_SMALL_BUFFER         1049  /* User-provided buffer is too small */
#define LTFS_RDONLY_VOLUME        1050  /* Volume is in read-only mode */
#define LTFS_NO_SPACE             1051  /* Volume is full */
#define LTFS_LARGE_XATTR          1052  /* Extended attribute value is too large */
#define LTFS_NO_INDEX             1053  /* Index search failed */
#define LTFS_XATTR_NAMESPACE      1054  /* Requested EA namespace is not supported */
#define LTFS_CONFIG_INVALID       1055  /* Config file parsing failed */
#define LTFS_PLUGIN_INCOMPLETE    1056  /* Plug-in does not implement a required function */
#define LTFS_NO_PLUGIN            1057  /* Requested plug-in is not known */
#define LTFS_POLICY_INVALID       1058  /* Cannot parse policy string */
#define LTFS_ISFILE               1059  /* Operation is only valid on directories */
#define LTFS_UNRESOLVED_VOLUME    1060  /* Cannot find target tape volume */
#define LTFS_POLICY_IMMUTABLE     1061  /* Data placement policy cannot be changed */
#define LTFS_SMALL_BLOCKSIZE      1062  /* Block size is too small */
#define LTFS_BARCODE_LENGTH       1063  /* Bar code has the wrong length */
#define LTFS_BARCODE_INVALID      1064  /* Bar code contains invalid characters */
#define LTFS_RESOURCE_SHORTAGE    1065  /* Available drives are not enough to move between tapes */
#define LTFS_DEVICE_FENCED        1066  /* Device lock request denied */
#define LTFS_REVAL_RUNNING        1067  /* Medium revalidation in progress */
#define LTFS_REVAL_FAILED         1068  /* Medium revalidation failed */
#define LTFS_SLOT_FULL            1069  /* Library is full slot condition */
#define LTFS_SLOT_SHORTAGE        1070  /* Library is slot shortage condition */
#define LTFS_CHANGER_ERROR        1071  /* Library is under error state */
#define LTFS_UNEXPECTED_TAPE      1072  /* Unexpected tape medium was found */
#define LTFS_NO_HOMESLOT          1073  /* No home slot is assigned */
#define LTFS_MOVE_ACTIVE_CART     1074  /* Attempt to move active cartridge to IE slot */
#define LTFS_NO_IE_SLOT           1075  /* There are no IE slots available */
#define LTFS_INVALID_SLOT         1076  /* Invalid target is specified, cannot move tape to requested location */
#define LTFS_UNSUPPORTED_CART     1077  /* Unsupported cartridge in LTFS */
#define LTFS_CART_STUCKED         1078  /* Cartridge cannot be unload from drive */
#define LTFS_OP_NOT_ALLOWED       1079  /* The operation is now allowed */
#define LTFS_OP_TO_DUP            1080  /* This operation is not allowed to 'Duplicated' cartridge */
#define LTFS_OP_TO_NON_SUP        1081  /* This operation is not allowed to 'Non-supported' cartridge */
#define LTFS_OP_TO_INACC          1082  /* This operation is not allowed to 'Inaccessible' cartridge */
#define LTFS_OP_TO_UNFMT          1083  /* This operation is not allowed to 'Unformatted' cartridge */
#define LTFS_OP_TO_INV            1084  /* This operation is not allowed to 'Invalid' cartridge */
#define LTFS_OP_TO_ERR            1085  /* This operation is not allowed to 'Error' cartridge */
#define LTFS_OP_TO_CRIT           1086  /* This operation is not allowed to 'Critical' cartridge */
#define LTFS_OP_TO_CLN            1087  /* This operation is not allowed to 'Cleaning' cartridge */
#define LTFS_OP_TO_RO             1088  /* This operation is not allowed to read-only cartridge */
#define LTFS_ALREADY_FS_INC       1089  /* This cartridge is already included in filesystem */
#define LTFS_NOT_IN_FS            1090  /* This cartridge is not included in filesystem */
#define LTFS_FS_CART_TO_IE        1091  /* Failed to move the cartridge to IE slot: need to remove the cartridge from filesystem */
#define LTFS_OP_TO_UNKN           1092  /* This operation is not allowed to bar code-less cartridge*/
#define LTFS_DRV_LOCKED           1093  /* Failed to remove the drive: 'Critical' cartridge is loaded */
#define LTFS_DRV_ALRDY_ADDED      1094  /* Failed to remove the drive: drive is already added */
#define LTFS_FORCE_INVENTORY      1095  /* Unexpected inventory rebuild error (have to manage within LTFS itself) */
#define LTFS_INVENTORY_FAILED     1096  /* Library fails to get inventory. Try to unmount LTFS and re-mount LTFS again */
#define LTFS_RESTART_OPERATION    1097  /* Operation needs to be restarted */
#define LTFS_NO_TARGET_DRIVE      1098  /* No target drive is found */
#define LTFS_NO_DCACHE_FSTYPE     1099  /* No supported filesystem type for dcache in this system */
#define LTFS_IMAGE_EXISTED        1100  /* The disk image is already existed */
#define LTFS_IMAGE_MOUNTED        1101  /* The disk image is already mounted */
#define LTFS_IMAGE_NOT_MOUNTED    1102  /* The disk image is not mounted */
#define LTFS_MTAB_NOREGULAR       1103  /* /etc/mtab is not a regular file */
#define LTFS_MTAB_OPEN            1104  /* Failed to open /etc/mtab */
#define LTFS_MTAB_LOCK            1105  /* Failed to lock /etc/mtab */
#define LTFS_MTAB_SEEK            1106  /* Failed to seek /etc/mtab */
#define LTFS_MTAB_UPDATE          1107  /* Failed to update /etc/mtab */
#define LTFS_MTAB_FLUSH           1108  /* Failed to flush /etc/mtab */
#define LTFS_MTAB_UNLOCK          1109  /* Failed to unlock /etc/mtab */
#define LTFS_MTAB_CLOSE           1110  /* Failed to close /etc/mtab */
#define LTFS_MTAB_COPY            1111  /* Failed to copy /etc/mtab to temporary file */
#define LTFS_MTAB_TEMP_OPEN       1112  /* Failed to open the temporary file for /etc/mtab */
#define LTFS_MTAB_TEMP_SEEK       1113  /* Failed to seek the temporary file for /etc/mtab */
#define LTFS_DCACHE_CREATION_FAIL 1114  /* Failed to create a directory tree to the disk image */
#define LTFS_DCACHE_UNSUPPORTED   1115  /* Failed to cache dentry due to host filesystem's limitation */
#define LTFS_DCACHE_EXTRA_SPACE   1116  /* The disk image reached to maximum size. */
#define LTFS_KEY_NOT_FOUND        1117  /* Cannot find data key */
#define LTFS_INVALID_SEQUENCE     1118  /* A function is called in invalid sequence */
#define LTFS_RDONLY_ROOT          1119  /* Cannot update the root directory */
#define LTFS_SYMLINK_CONFLICT     1120  /* Conflict symlink tag and extent tag in xml */
#define LTFS_NETWORK_INIT_FAIL    1121  /* Failed to initialize the network connection for ltfsadmintool */
#define LTFS_DRIVE_SHORTAGE       1122  /* Insufficient number of drives available for the operation */
#define LTFS_INVALID_VOLSER       1123  /* Invalid volume serial number */
#define LTFS_LESS_SPACE           1124  /* Volume has no space to write a file */
#define LTFS_WRITE_PROTECT        1125  /* Volume is in physical and/or logical write protect mode */
#define LTFS_WRITE_ERROR          1126  /* Write error has previously occurred on this tape */
#define LTFS_UNEXPECTED_BARCODE   1127  /* Unexpected length of barcode label is set on the cartridge */
#define LTFS_STRING_CONVERSION    1128  /* Conversion from string to value is failed, may be invalid string */
#define LTFS_SESSION_INIT_FAIL    1129  /* Failed to initialize admin session */
#define LTFS_MESSAGE_INVALID      1130  /* Invalid message on admin_channel */
#define LTFS_PASSWORD_INVALID     1131  /* Invalid password to login via admin_channel */
#define LTFS_NOT_AUTHENTICATERD   1132  /* Not login yet */
#define LTFS_WORM_DEEP_RECOVERY   1133  /* Deep recovery cannot be performed to a WORM cartridge */
#define LTFS_WORM_ROLLBACK        1134  /* WORM cartridg cannot be rollbacked */
#define LTFS_NONWORM_SALVAGE      1135  /* Salvage list is valid only for WORM cartridge */
#define LTFS_FORMATTED            1136  /* Cartridge is already formatted */
#define LTFS_RULES_WORM           1137  /* Placement policy cannot be set to WORM cartridge */
#define LTFS_BAD_BLOCKSIZE        1138  /* Specified block size is not correct */
#define LTFS_BAD_VOLNAME          1139  /* Specified volume name is not correct */
#define LTFS_BAD_RULES            1140  /* Specified placement policy is not correct */
#define LTFS_GEN_NEEDED           1141  /* Need to specify a genaration to be rollbacked */
#define LTFS_BAD_GENERATION       1142  /* Specified generation is not correct */
#define LTFS_NO_ROLLBACK_TARGET   1143  /* Rollback target generation is not specified */
#define LTFS_MANY_INDEXES         1144  /* Multiple target indexes were found on the cartridge */
#define LTFS_SALVAGE_NOT_NEEDED   1145  /* Salvage list is not required for this cartridge */
#define LTFS_WORM_ENABLED         1146  /* This operation is not allowed for worm enabled entry */
#define LTFS_OUTSTANDING_REFS     1147  /* Specified cartridge has opened files */
#define LTFS_REBUILD_IN_PROGRESS  1148  /* Inventory is being rebuilt */
#define LTFS_MULTIPLE_START       1149  /* There is another LE to use library */
#define LTFS_CARTRIDGE_NOT_FOUND  1150  /* Cannot find cartridge */
#define LTFS_CACHE_LOCK_ERR       1151  /* Cannot lock cache files */
#define LTFS_CACHE_UNLOCK_ERR     1152  /* Cannot lock cache files */
#define LTFS_CREPO_FILE_ERR       1153  /* File operation error (cartridge repo) */
#define LTFS_CREPO_READ_ERR       1154  /* Read error (cartridge repo) */
#define LTFS_CREPO_WRITE_ERR      1155  /* Write error (cartridge repo) */
#define LTFS_CREPO_INVALID_OP     1156  /* Invalid operation(invalid arg, op, etc) */
#define LTFS_FILE_ERR             1157  /* Error in file operation (mkdir,stat,rename, etc) */
#define LTFS_CARTRIDGE_IN_USE     1158  /* Cannot use the cartridge */
#define LTFS_NO_LOCK_ENTRY        1159  /* Cannot find lock entry */
#define LTFS_MOUNT_ERR            1160  /* Cannot mount/unmount */
#define LTFS_NO_DEVICE            1161  /* Cannot find device */
#define LTFS_XATTR_ERR            1162  /* Failed to set/get Extended Attribute */
#define LTFS_FTW_ERR              1163  /* Failed to perform file tree walk */
#define LTFS_TIME_ERR             1164  /* Failed to update time stamp */
#define LTFS_NOT_BLOCK_DEVICE     1165  /* Block device is required */
#define LTFS_QUOTA_EXCEEDED       1166  /* Disk quota exceeded */
#define LTFS_TOO_MANY_OPEN_FILES  1167  /* Too many open files in system */
#define LTFS_LINKDIR_EXISTS       1168  /* Link dir exists */
#define LTFS_NO_DMAP_ENTRY        1169  /* No dmap entry */
#define LTFS_RECOVERABLE_FILE_ERR 1170  /* Recoverable Error in file operation (mkdir, stat, rename, etc) */
#define LTFS_NO_DCACHE_SPC        1171  /* Failed to expabd dcache space */
#define LTFS_POS_SUSPECT_BOP      1172  /* If a write FM is attempted at BOP partition 0 */
// 1173 unused
// 1174 unused
// 1175 unused
// 1176 unused
// 1177 unused
// 1178 unused
// 1179 unused
#define LTFS_CACHE_IO             1180  /* IO error on cache operation */
#define LTFS_CACHE_DISCARDED      1181  /* Cache is corrupted and discarded */
#define LTFS_LONG_WRITE_LOCK      1182  /* Long MRSW for write is aquired */
#define LTFS_INCOMPATIBLE_CACHE   1183  /* Incompatible cache file is detected */
#define LTFS_DCACHE_NOT_INITIALIZED 1184 /* Dcache is not initialized yet */
#define LTFS_CONFIG_FILE_WLOCKED  1185  /* The multinode config file is locked */
#define LTFS_CREATE_QUEUE         1186  /* Failed to create POSIX message pueue */
#define LTFS_FORK_ERROR           1187  /* Failed to fork process */
#define LTFS_NOACK                1188  /* No ack message is received from child process */
#define LTFS_NODE_DETECT_FAIL     1189  /* Node type detection is failed */
#define LTFS_INVALID_MESSAGE      1190  /* Invalid message is detected in node detection */
#define LTFS_NODE_DEGATE_FAIL     1191  /* Failed to degate other nodes */
#define LTFS_CLUSTER_MRSW_FAIL    1192  /* Failed to opetate against a cluster wide lock */
#define LTFS_CART_NOT_MOUNTED     1193  /* Got a request against an unmounted cartridge */
#define LTFS_RDONLY_DEN_DRV       1194  /* Read only combination of density code and drive generation */
#define LTFS_NEED_DRIVE_SELECTION 1195  /* Drive selection is needed again */
#define LTFS_MUTEX_ALREADY_LOCKED 1196  /* Mutex is already locked */
#define LTFS_TAPE_UNDER_PROCESS   1197  /* This tape is already under processing */
#define LTFS_TAPE_REMOVED         1198  /* The tape is already removed */
#define LTFS_NEED_MOVE            1199  /* Need to move tape to a storage slot first before the operation */
#define LTFS_NEED_START_OVER      1200  /* Need operation needs to be started again */

#define LTFS_ERR_MAX              19999

/*
 * Tape drive (or lower driver) errors (20000 - 29999)
 * Following codes should be returned only from tape backend functions,
 * and also tape backend functions must return the following codes instead of general errors.
 */
#define DEVICE_GOOD                    0     /* Command succeeded */

#define EDEV_ERR_MIN                 20000  /* First tape drive error value defined by this file */

/* Sense Key 0 No Sense (Actually not error) */
#define EDEV_NO_SENSE                20000  /* 00/0000 No sense */
#define EDEV_OVERRUN                 20002  /* 00/0000 Read with overrun condition. (when sili bit is off) */
#define EDEV_UNDERRUN                20003  /* 00/0000 Read with underrun condition. (when sili bit is off) */
#define EDEV_FILEMARK_DETECTED       20004  /* 00/0001 File mark is detected */
#define EDEV_EARLY_WARNING           20005  /* 00/0002 Early warning condition is detected. (Used only internal) */
#define EDEV_BOP_DETECTED            20006  /* 00/0004 Beginning-of-Partition is detected */
#define EDEV_PROG_EARLY_WARNING      20007  /* 00/0007 Programmable early warning condition detected */
#define EDEV_CLEANING_CART           20008  /* 00/3003 00/8311 ITD_RES Media is cleaning cartridge*/
#define EDEV_VOLTAG_NOT_READABLE     20009  /* 00/8301 ITD_RES Voltag not readable*/
#define EDEV_LOCATION_NOT_PRESENT    20010  /* 00/8303 ITD_RES Media presence is not determined*/
#define EDEV_MEDIA_PRESENSE_UNKNOWN  20011  /* 00/8303 ITD_RES Media presence is not determined */
#define EDEV_SLOT_UNKNOWN_STATE      20012  /* 00/8100 (TS3500) Slot state is unknown */
#define EDEV_DRIVE_NOT_PRESENT       20013  /* 00/8200 (TS3500) The drive is not present */
#define EDEV_RECORD_NOT_FOUND        20014  /* 00/1400 Record not found (string search) */
#define EDEV_INSUFFICIENT_TIME       20015  /* 00/2E00 Insufficient time for operation (string search) */
#define EDEV_CLEANING_REQUIRED       20098  /* (IBM LTO 00/8282) Drive requests cleaning */

/* Sense Key 1 Recovered Error */
#define EDEV_RECOVERED_ERROR         20100  /* 01/xxxx Recovered Error (Should be ignored) */
#define EDEV_MODE_PARAMETER_ROUNDED  20101  /* 01/3700 Mode select parameter is rounded by the drive (Should be ignored)*/
#define EDEV_DEGRADED_MEDIA          20198  /* (IBM LTO 01/8252) Degraded medium is detected. */

/* Sense Key 2 Not Ready */
#define EDEV_NOT_READY               20200  /* 02/xxxx Drive is not ready state */
#define EDEV_NOT_REPORTABLE          20201  /* 02/0400 Cause not reportable */
#define EDEV_BECOMING_READY          20202  /* 02/0401 Device is becoming ready */
#define EDEV_NEED_INITIALIZE         20203  /* 02/0402 Initialize command is needed */
#define EDEV_MANUAL_INTERVENTION     20204  /* 02/0403 Manual intervention is required */
#define EDEV_OPERATION_IN_PROGRESS   20205  /* 00/0016 02/0407 Operation in progress */
#define EDEV_OFFLINE                 20206  /* 02/0412 Device is off-line */
#define EDEV_DOOR_OPEN               20207  /* 02/0418 Door Open*/
#define EDEV_OVER_TEMPERATURE        20208  /* 02/0B01 08/0B01 Device is too hot. */
#define EDEV_NO_MEDIUM               20209  /* 02/3A00 Drive has no medium */
#define EDEV_NOT_SELF_CONFIGURED_YET 20210  /* 02/3E00 Device is not self configured yet */
#define EDEV_PARAMETER_VALUE_REJECTED 20211 /* 02/7411 SA creation parameter value rejected */
#define EDEV_CLEANING_IN_PROGRESS    20297  /* 02/3003 Cleaning in progress*/
#define EDEV_IE_OPEN                 20298  /* 02/0484 (TS3500) IO slot is opened */

/* Sense Key 3 Medium Error */
#define EDEV_MEDIUM_MIN              20300  /* Minimum medium error value */
#define EDEV_MEDIUM_ERROR            20300  /* 03/xxxx Other medium Errors */
#define EDEV_RW_PERM                 20301  /* 03/0900 Read Perm or Write perm */
#define EDEV_CM_PERM                 20302  /* 03/4100 03/1112 CM RW error */
#define EDEV_MEDIUM_FORMAT_ERROR     20303  /* 03/3000 03/3001 03/3002 Incompatible or Unknown format*/
#define EDEV_MEDIUM_FORMAT_CORRUPTED 20304  /* 03/3100 Format corruption is detected */
#define EDEV_INTEGRITY_CHECK         20305  /* 03/100D Drive rejects read because of unexpected overwrite on WORM medium */
#define EDEV_LOAD_UNLOAD_ERROR       20306  /* 03/5300 03/5304 Load/Unload error */
#define EDEV_CLEANING_FALIURE        20307  /* 03/3007 Cleaning Failure */
#define EDEV_READ_PERM               20308  /* 03/1100 Read Perm */
#define EDEV_WRITE_PERM              20309  /* 03/0c00 Write perm */
#define EDEV_MEDIUM_MAX              20399  /* Maximum medium error value */

#define IS_MEDIUM_ERROR(e)           ((e>=EDEV_MEDIUM_MIN)&&(e<=EDEV_MEDIUM_MAX))
#define IS_READ_PERM(e)              ((e==EDEV_RW_PERM)||(e==EDEV_READ_PERM)||(e==EDEV_MEDIUM_FORMAT_CORRUPTED))
#define IS_WRITE_PERM(e)             ((e==EDEV_RW_PERM)||(e==EDEV_WRITE_PERM)||(e==EDEV_MEDIUM_FORMAT_CORRUPTED))

/* Sense Key 4 Hardware or Firmware Error */
#define EDEV_HARDWARE_MIN            20400  /* Minimum hardware error value */
#define EDEV_HARDWARE_ERROR          20400  /* 04/xxxx Other H/W errors */
#define EDEV_LBP_WRITE_ERROR         20401  /* 04/1001 Logical Block Guard Check Failed */
#define EDEV_LBP_READ_ERROR          20402  /* ------- Logical Block Protection read error */
#define EDEV_NO_CONNECTION           20403  /* There is no connection to the device */
#define EDEV_HARDWARE_MAX            20499  /* Maximum hardware error value */

#define IS_HARDWARE_ERROR(e)         ((e>=EDEV_HARDWARE_MIN)&&(e<=EDEV_HARDWARE_MAX))

/* Sense Key 5 Illegal Request */
#define EDEV_ILLEGAL_REQUEST         20500  /* 05/XXXX All illegal request errors */
#define EDEV_INVALID_FIELD_CDB       20501  /* 05/2400 Invalid Field in CDB */
#define EDEV_DEST_FULL               20502  /* 05/3B0D Medium destination element full */
#define EDEV_SRC_EMPTY               20503  /* 05/3B0E Medium source element empty */
#define EDEV_MAGAZINE_INACCESSIBLE   20504  /* 05/3B11 Medium magazine not accessible */
#define EDEV_INVALID_ADDRESS         20505  /* 05/2101 Invalid element address */
#define EDEV_MEDIUM_LOCKED           20506  /* 05/5301 A drive did not unload a cartridge
											   05/5302 Medium removal prevented
											   05/5303 Drive media removal prevented state set */

/* Sense Key 6 Unit Attention */
#define EDEV_UA_MIN                  20600  /* Minimum UA error value */
#define EDEV_UNIT_ATTENTION          20600  /* 06/XXXX Other UA conditions */
#define EDEV_MEDIUM_MAY_BE_CHANGED   20601  /* 06/2800 Not ready to ready transition, medium may have changed */
#define EDEV_IE_ACCESSED             20602  /* 06/2801 IE slot is accessed */
#define EDEV_POR_OR_BUS_RESET        20603  /* 06/2900 POR/Bus reset occurred */
#define EDEV_CONFIGURE_CHANGED       20604  /* 06/3F03 06/3F0E Drive configuration is changed */
#define EDEV_COMMAND_CLEARED         20605  /* 06/2F00 Command is cleared by another initiator */
#define EDEV_MEDIUM_REMOVAL_REQ      20606  /* 06/5A01 Operator medium removal request  */
#define EDEV_MEDIA_REMOVAL_PREV      20607  /* 06/5302 Media removal prevented */
#define EDEV_DOOR_CLOSED             20608  /* 06/3B13 Medium magazine inserted */
#define EDEV_TIME_STAMP_CHANGED      20609  /* 06/2A01 Drive configuration is changed */
#define EDEV_RESERVATION_PREEMPTED   20610  /* 06/2A03 Reservations preempted */
#define EDEV_RESERVATION_RELEASED    20611  /* 06/2A04 Reservations released */
#define EDEV_REGISTRATION_PREEMPTED  20612  /* 06/2A05 Registrations preempted */
#define EDEV_UA_MAX                  20699  /* Minimum UA error value */
#define IS_UNIT_ATTENTION(e)         ((e>=EDEV_UA_MIN)&&(e<=EDEV_UA_MAX))

/* Sense Key 7 Data Protect */
#define EDEV_DATA_PROTECT            20700  /* 07/XXXX Other data protect conditions */
#define EDEV_WRITE_PROTECTED         20701  /* 07/2700 Write Protected */
#define EDEV_WRITE_PROTECTED_WORM    20702  /* 07/3000 07/300D 07/7400 WORM cartridge */
#define EDEV_WRITE_PROTECTED_OPERATOR 20703  /* 07/5A02 Attempt to overwrite in append only mode */

/* Sense Key 8 Blank Check */
#define EDEV_BLANK_CHECK             20800  /* 08/XXXX Other blank check conditions */
#define EDEV_EOD_DETECTED            20801  /* 08/0005 EOD detected */
#define EDEV_EOD_NOT_FOUND           20802  /* 08/1403 EOD not found */

/* Sense Key B Aborted Command */
#define EDEV_ABORTED_COMMAND         21100  /* 0B/XXXX Other aborted command conditions */
#define EDEV_OVERLAPPED              21101  /* 0B/4E00 Overlapped commands */
#define EDEV_TIMEOUT                 21102  /* 0B/4B06 Initiator response timeout */

/* Sense Key D Volume Overflow */
#define EDEV_OVERFLOW                21300  /* 0D/XXXX The medium is overflowed */

/* Crypto Errors on the tape drive */
#define EDEV_CRYPTO_ERROR            21600  /* XX/EEXX 07/EFXX 07/74XX Other crypto related errors */
#define EDEV_KEY_SERVICE_ERROR       21601  /* 04/EE0E 04/EE0F 07/EE0E 07/EE0F Key service timeout or failure */
#define EDEV_KEY_CHANGE_DETECTED     21602  /* 06/EE12 06/EE18 06/EE19 Detect key change */
#define EDEV_KEY_REQUIRED            21603  /* 07/EF10 Detect key change */

/* Internal errors */
#define EDEV_INTERNAL_ERROR          21700  /* Internal logic error */
#define EDEV_DRIVER_ERROR            21701  /* Driver reports error */
#define EDEV_HOST_ERROR              21702  /* Host reports error */
#define EDEV_TARGET_ERROR            21703  /* Target reports error */
#define EDEV_NO_MEMORY               21704  /* Memory allocation failure */
#define EDEV_UNSUPPORTED_FUNCTION    21705  /* Unsupported function */
#define EDEV_PARAMETER_NOT_FOUND     21706  /* Parameter code of log page is not found */
#define EDEV_CANNOT_GET_SENSE        21707  /* Cannot get the sense data from lower layer */
#define EDEV_INVALID_ARG             21708  /* Invalid argument */
#define EDEV_DUMP_EIO                21709  /* IO error on saving dump */
#define EDEV_DEVICE_BUSY             21710  /* Medium is already mounted or in use */
#define EDEV_DEVICE_UNOPENABLE       21711  /* Device cannot be opened */
#define EDEV_DEVICE_UNSUPPORTABLE    21712  /* Unsupportable device */
#define EDEV_INVALID_LICENSE         21713  /* Cannot confirm IBM logo'd device */
#define EDEV_UNSUPPORTED_FIRMWARE    21714  /* Unsupported firmware */
#define EDEV_UNSUPPORETD_COMMAND     21715  /* Unsupported SCSI command */
#define EDEV_LENGTH_MISMATCH         21716  /* Actual transfer length and residual length is not matched */
#define EDEV_BUFFER_OVERFLOW         21717  /* Detect buffer overrun */
#define EDEV_DRIVES_MISMATCH         21718  /* Number of drives are not correct */
#define EDEV_RESERVATION_CONFLICT    21719  /* Reservation Conflict */
#define EDEV_CONNECTION_LOST         21720  /* Connection lost while executing a command */
#define EDEV_NO_RESERVATION_HOLDER   21721  /* No reservation holder */
#define EDEV_NEED_FAILOVER           21722  /* Path switch happens, need to reissue the command */
#define EDEV_REAL_POWER_ON_RESET     21723  /* Real power on reset is detected */
#define EDEV_BUFFER_ALLOCATE_ERROR   21724  /* Buffer allocation error in a driver */
#define EDEV_RETRY                   21725  /* Retry the command again */

/* Vendor Unique codes */
#define EDEV_UNKNOWN                 29998  /* Unknown sense code */
#define EDEV_VENDOR_UNIQUE           29999  /* Vendor unique code. ASC >= 0x80 or ASCQ >= 0x80 */

/* 2xxxx are reserved for device error codes */

/* Common status code for external program
 * The maximum return code from the program is 0xFF.
 */
#define PROG_NO_ERRORS            0x00 /* Success */
#define PROG_TREAT_SUCCESS        0x01 /* Treat as success */
#define PROG_REBOOT_REQUIRED      0x02 /* Reboot required */
#define PROG_UNCORRECTED          0x04 /* Cannot recover, the cartridge is modified */
#define PROG_OPERATIONAL_ERROR    0x08 /* Get device error while processing, the cartridge may be modified */
#define PROG_USAGE_SYNTAX_ERROR   0x10 /* Wrong argument */
#define PROG_CANCELED_BY_USER     0x20 /* Canceled by user */
#define PROG_SHARED_LIB_ERROR     0x40 /* Library error */

/* Status code for ltfsck (Same as fsck)
 * The maximum return code from the program is 0xFF.
 */
#define LTFSCK_NO_ERRORS          PROG_NO_ERRORS          /* No error and the cartridge is not modified */
#define LTFSCK_CORRECTED          PROG_TREAT_SUCCESS      /* Recover correctly, the cartridge is modified */
#define LTFSCK_REBOOT_REQUIRED    PROG_REBOOT_REQUIRED    /* Reboot required */
#define LTFSCK_UNCORRECTED        PROG_UNCORRECTED        /* Cannot recover, the cartridge is modified */
#define LTFSCK_OPERATIONAL_ERROR  PROG_OPERATIONAL_ERROR  /* Get device error while processing, the cartridge may be modified */
#define LTFSCK_USAGE_SYNTAX_ERROR PROG_USAGE_SYNTAX_ERROR /* Wrong argument */
#define LTFSCK_CANCELED_BY_USER   PROG_CANCELED_BY_USER   /* Canceled by user */
#define LTFSCK_SHARED_LIB_ERROR   PROG_SHARED_LIB_ERROR   /* Library error */

/* Status code for mkltfs
 * The maximum return code from the program is 0xFF.
 */
#define MKLTFS_NO_ERRORS          PROG_NO_ERRORS          /* No error and the cartridge is formatted */
#define MKLTFS_UNFORMATTED        PROG_TREAT_SUCCESS      /* No error and the cartridge is unformatted */
#define MKLTFS_OPERATIONAL_ERROR  PROG_OPERATIONAL_ERROR  /* Get device error while processing, the cartridge may be modified */
#define MKLTFS_USAGE_SYNTAX_ERROR PROG_USAGE_SYNTAX_ERROR /* Wrong argument */
#define MKLTFS_CANCELED_BY_USER   PROG_CANCELED_BY_USER   /* Canceled by user */

#endif /* __ltfs_error_h__ */
