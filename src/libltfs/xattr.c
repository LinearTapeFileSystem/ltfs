/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2022 IBM Corp. All rights reserved.
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
** FILE NAME:       xattr.c
**
** DESCRIPTION:     Implements extended attribute routines.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
**                  Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#endif
#include "ltfs.h"
#include "ltfs_fsops.h"
#include "xattr.h"
#include "fs.h"
#include "xml_libltfs.h"
#include "pathname.h"
#include "tape.h"
#include "ltfs_internal.h"
#include "arch/time_internal.h"

/* Helper functions for formatting virtual EA output */
static int _xattr_get_cartridge_health(cartridge_health_info *h, int64_t *val, char **outval,
	const char *msg, struct ltfs_volume *vol)
{
	int ret = ltfs_get_cartridge_health(h, vol);
	if (ret == 0) {
		ret = asprintf(outval, "%"PRId64, *val);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 10001E, msg);
			*outval = NULL;
			return -LTFS_NO_MEMORY;
		}
	} else
		*outval = NULL;
	return ret;
}

static int _xattr_get_cartridge_health_u64(cartridge_health_info *h, uint64_t *val, char **outval,
										   const char *msg, struct ltfs_volume *vol)
{
	int ret = ltfs_get_cartridge_health(h, vol);
	if (ret == 0 && (int64_t)(*val) != UNSUPPORTED_CARTRIDGE_HEALTH) {
		ret = asprintf(outval, "%"PRIu64, *val);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 10001E, msg);
			*outval = NULL;
			ret = -LTFS_NO_MEMORY;
		}
	} else if (ret == 0) {
		ret = asprintf(outval, "%"PRId64, UNSUPPORTED_CARTRIDGE_HEALTH);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 10001E, msg);
			*outval = NULL;
			ret = -LTFS_NO_MEMORY;
		}
	} else
		*outval = NULL;
	return ret;
}

static int _xattr_get_cartridge_capacity(struct device_capacity *cap, unsigned long *val,
										 char **outval, const char *msg, struct ltfs_volume *vol)
{
	double scale = vol->label->blocksize / 1048576.0;
	int ret = ltfs_capacity_data_unlocked(cap, vol);
	if (ret == 0) {
		ret = asprintf(outval, "%lu", (unsigned long)((*val) * scale));
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 10001E, msg);
			*outval = NULL;
			return -LTFS_NO_MEMORY;
		}
	} else
		*outval = NULL;
	return ret;
}

static int _xattr_get_time(struct ltfs_timespec *val, char **outval, const char *msg)
{
	int ret;

	ret = xml_format_time(*val, outval);
	if (! (*outval)) {
		ltfsmsg(LTFS_ERR, 11145E, msg);
		return -LTFS_NO_MEMORY;
	}

	return ret;
}

static int _xattr_get_dentry_time(struct dentry *d, struct ltfs_timespec *val, char **outval,
								  const char *msg)
{
	int ret;
	acquireread_mrsw(&d->meta_lock);
	ret = _xattr_get_time(val, outval, msg);
	releaseread_mrsw(&d->meta_lock);
	return ret;
}

static int _xattr_get_string(const char *val, char **outval, const char *msg)
{
	if (! val)
		return 0;
	*outval = strdup(val);
	if (! (*outval)) {
		ltfsmsg(LTFS_ERR, 10001E, msg);
		return -LTFS_NO_MEMORY;
	}
	return 0;
}

static int _xattr_get_u64(uint64_t val, char **outval, const char *msg)
{
	int ret = asprintf(outval, "%"PRIu64, val);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10001E, msg);
		*outval = NULL;
		ret = -LTFS_NO_MEMORY;
	}
	return ret;
}

static int _xattr_get_tapepos(struct tape_offset *val, char **outval, const char *msg)
{
	int ret = asprintf(outval, "%c:%"PRIu64, val->partition, val->block);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10001E, msg);
		return -LTFS_NO_MEMORY;
	}
	return 0;
}

static int _xattr_get_partmap(struct ltfs_label *label, char **outval, const char *msg)
{
	int ret = asprintf(outval, "I:%c,D:%c", label->partid_ip, label->partid_dp);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10001E, msg);
		return -LTFS_NO_MEMORY;
	}
	return 0;
}

static int _xattr_get_version(int version, char **outval, const char *msg)
{
	int ret;
	if (version == 10000) {
		*outval = strdup("1.0");
		if (! (*outval)) {
			ltfsmsg(LTFS_ERR, 10001E, msg);
			return -LTFS_NO_MEMORY;
		}
	} else {
		ret = asprintf(outval, "%d.%d.%d", version/10000, (version % 10000)/100, version % 100);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 10001E, msg);
			return -LTFS_NO_MEMORY;
		}
	}
	return 0;
}

static int _xattr_set_time(struct dentry *d, struct ltfs_timespec *out, const char *value,
						   size_t size, const char *msg, struct ltfs_volume *vol)
{
	int ret;
	struct ltfs_timespec t;
	char *value_null_terminated;

	value_null_terminated = malloc(size + 1);
	if (! value_null_terminated) {
		ltfsmsg(LTFS_ERR, 10001E, msg);
		return -LTFS_NO_MEMORY;
	}
	memcpy(value_null_terminated, value, size);
	value_null_terminated[size] = '\0';

	ret = xml_parse_time(false, value_null_terminated, &t);
	free(value_null_terminated);
	if (ret < 0)
		return -LTFS_BAD_ARG;

	acquirewrite_mrsw(&d->meta_lock);
	*out = t;
	d->dirty = true;
	releasewrite_mrsw(&d->meta_lock);

	ltfs_set_index_dirty(true, false, vol->index);
	return ret;
}

static int _xattr_get_vendorunique_xattr(char **outval, const char *msg, struct ltfs_volume *vol)
{
	int ret;

	ret = ltfs_get_vendorunique_xattr(msg, outval, vol);
	if (ret != 0)
		*outval = NULL;

	return ret;
}

static int _xattr_set_vendorunique_xattr(const char *name, const char *value, size_t size,
										 struct ltfs_volume *vol)
{
	int ret;

	ret = ltfs_set_vendorunique_xattr(name, value, size, vol);

	return ret;
}

/**
 * Check xattr name is WORM related one or not
 *
 * @param name EA name to check
 * @return true if name is related WORM. Otherwise false.
 */
static inline bool _xattr_is_worm_ea(const char *name)
{
	if (!strcmp(name, "ltfs.vendor.IBM.immutable") || !strcmp(name, "ltfs.vendor.IBM.appendonly")) {
		/* WORM related xattr */
		return true;
	}
	return false;
}

/**
 * Check xattr name is stored VEA or not
 *
 * Extended attribute starts from 'ltfs.' is reserved and treated as Virtual Extended Attribute (VEA)
 * and they aren't stored into index. But 'ltfs.permissions.*' and 'ltfs.hash.*' is introduced from
 * LTFS Format Spec 2.4 to store values into index.
 * This function checks provided name shall be stored VEA or not.
 *
 * @param name EA name to check
 * @return true if name is stored VEA. Otherwise false.
 */
static inline bool _xattr_is_stored_vea(const char *name)
{
	if (strcmp(name, "ltfs.spannedFileOffset") &&
		strcmp(name, "ltfs.mediaPool.name") &&
		strcasestr(name, "ltfs.permissions.") != name &&
		strcasestr(name, "ltfs.hash.") != name)
	{
		return false;
	}

	return true;
}

/* Local functions */

/**
 * Search for an xattr with the given name. Must call this function with a lock on
 * the dentry's meta_lock.
 * @param out On success, points to the xattr that was found.
 * @param d Dentry to search.
 * @param name Name to search for.
 * @return 1 if xattr was found, 0 if not, or a negative value on error.
 */
static int _xattr_seek(struct xattr_info **out, struct dentry *d, const char *name)
{
	struct xattr_info *entry;

	*out = NULL;
	TAILQ_FOREACH(entry, &d->xattrlist, list) {
		if (! strcmp(entry->key.name, name)) {
			*out = entry;
			break;
		}
	}

	if (*out)
		return 1;
	else
		return 0;
}

/**
 * Take the volume lock and dentry contents_lock as appropriate for the EA and the access type.
 * @param name EA name being read or written.
 * @param modify True if EA will be set or deleted, false if it will be read.
 * @param d Dentry under consideration.
 * @param vol LTFS volume.
 * @return 0 on success or -LTFS_REVAL_FAILED if the medium is invalid.
 */
static int _xattr_lock_dentry(const char *name, bool modify, struct dentry *d, struct ltfs_volume *vol)
{
	/* EAs that read the extent list need to take the contents_lock */
	if (! strcmp(name, "ltfs.startblock")
		|| ! strcmp(name, "ltfs.partition")) {
		acquireread_mrsw(&d->contents_lock);
	}

	/* Other EAs either need no additional locks, or they need the meta_lock.
	 * The caller is responsible for taking the meta_lock as necessary. */
	return 0;
}

/**
 * Undo locking performed in _xattr_lock_dentry.
 * @param name EA name being read or written.
 * @param modify True if EA was set or deleted, false if it was read.
 * @param d Dentry under consideration.
 * @param vol LTFS volume.
 */
static void _xattr_unlock_dentry(const char *name, bool modify, struct dentry *d, struct ltfs_volume *vol)
{
	/* EAs that read the extent list need to take the contents_lock */
	if (! strcmp(name, "ltfs.startblock")
		|| ! strcmp(name, "ltfs.partition")) {
		releaseread_mrsw(&d->contents_lock);
	}
}

/**
 * List real extended attributes for a dentry. Must be called with a read lock held on
 * the dentry's meta_lock.
 * @param d Dentry to list.
 * @param list Output buffer.
 * @param size Output buffer size, may be 0.
 * @return Number of bytes in listed extended attributes, or a negative value on error.
 */
static int _xattr_list_physicals(struct dentry *d, char *list, size_t size)
{
	struct xattr_info *entry;
	char *prefix = "\0", *new_name;
	int prefixlen = 0, namelen;
	int ret = 0, nbytes = 0;

#if ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM)))
	ret = pathname_unformat("user.", &prefix);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11141E, ret);
		return ret;
	}
	prefixlen = strlen(prefix);
#endif /* (!defined (__APPLE__)) && (!defined (mingw_PLATFORM)) */

	TAILQ_FOREACH(entry, &d->xattrlist, list) {
		ret = pathname_unformat(entry->key.name, &new_name);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 11142E, ret);
			goto out;
		}

		if(strncmp(new_name, LTFS_LIVELINK_EA_NAME, strlen(LTFS_LIVELINK_EA_NAME) + 1)) {
			namelen = strlen(new_name);

			nbytes += prefixlen + namelen + 1;
			if (size && (size_t)nbytes <= size) {
				memcpy(list, prefix, prefixlen);
				list += prefixlen;
				memcpy(list, new_name, namelen);
				list += namelen + 1;
			}
		}
		free(new_name);
	}

out:
#if ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM)))
	free(prefix);
#endif /* (!defined (__APPLE__)) && (!defined (mingw_PLATFORM)) */
	if (ret < 0)
		return ret;
	return nbytes;
}

/**
 * Determine whether an extended attribute name exists and is virtual for a given dentry.
 * @param d Dentry to check.
 * @param name Name to check.
 * @param vol LTFS volume to which the dentry belongs.
 * @return true if the name exists and is virtual, false otherwise.
 */
static bool _xattr_is_virtual(struct dentry *d, const char *name, struct ltfs_volume *vol)
{
	/* xattrs on all dentries */
	if (! strcmp(name, "ltfs.createTime")
		|| ! strcmp(name, "ltfs.modifyTime")
		|| ! strcmp(name, "ltfs.accessTime")
		|| ! strcmp(name, "ltfs.changeTime")
		|| ! strcmp(name, "ltfs.backupTime")
		|| ! strcmp(name, "ltfs.fileUID")
		|| ! strcmp(name, "ltfs.volumeUUID")
		|| ! strcmp(name, "ltfs.volumeName")
		|| ! strcmp(name, "ltfs.driveCaptureDump")
		|| ! strcmp(name, "ltfs.softwareVersion")
		|| ! strcmp(name, "ltfs.softwareFormatSpec")
		|| ! strcmp(name, "ltfs.softwareVendor")
		|| ! strcmp(name, "ltfs.softwareProduct")
		|| ! strcmp(name, "ltfs.mamBarcode")
		|| ! strcmp(name, "ltfs.mamApplicationVendor")
		|| ! strcmp(name, "ltfs.mamApplicationVersion")
		|| ! strcmp(name, "ltfs.mamApplicationFormatVersion")
		|| ! strcmp(name, "ltfs.volumeLockState")
		)
		return true;

	if (_xattr_is_worm_ea(name)) {
		/* Treat WORM related EA as real EA */
		return false;
	}

	/* xattrs on files */
	if (! d->isdir) {
		if (! TAILQ_EMPTY(&d->extentlist)
			&& (! strcmp(name, "ltfs.partition") || ! strcmp(name, "ltfs.startblock")))
			return true;
	}

	/* xattrs on the root dentry */
	if (d == vol->index->root) {
		if (vol->index->index_criteria.have_criteria && ! strcmp(name, "ltfs.policyMaxFileSize"))
			return true;
		if (! strcmp(name, "ltfs.commitMessage")
			|| ! strcmp(name, "ltfs.indexVersion")
			|| ! strcmp(name, "ltfs.labelVersion")
			|| ! strcmp(name, "ltfs.sync")
			|| ! strcmp(name, "ltfs.indexGeneration")
			|| ! strcmp(name, "ltfs.indexTime")
			|| ! strcmp(name, "ltfs.policyExists")
			|| ! strcmp(name, "ltfs.policyAllowUpdate")
			|| ! strcmp(name, "ltfs.volumeFormatTime")
			|| ! strcmp(name, "ltfs.volumeBlocksize")
			|| ! strcmp(name, "ltfs.volumeCompression")
			|| ! strcmp(name, "ltfs.indexLocation")
			|| ! strcmp(name, "ltfs.indexPrevious")
			|| ! strcmp(name, "ltfs.indexCreator")
			|| ! strcmp(name, "ltfs.labelCreator")
			|| ! strcmp(name, "ltfs.partitionMap")
			|| ! strcmp(name, "ltfs.volumeSerial")
			|| ! strcmp(name, "ltfs.mediaLoads")
			|| ! strcmp(name, "ltfs.mediaRecoveredWriteErrors")
			|| ! strcmp(name, "ltfs.mediaPermanentWriteErrors")
			|| ! strcmp(name, "ltfs.mediaRecoveredReadErrors")
			|| ! strcmp(name, "ltfs.mediaPermanentReadErrors")
			|| ! strcmp(name, "ltfs.mediaPreviousPermanentWriteErrors")
			|| ! strcmp(name, "ltfs.mediaPreviousPermanentReadErrors")
			|| ! strcmp(name, "ltfs.mediaBeginningMediumPasses")
			|| ! strcmp(name, "ltfs.mediaMiddleMediumPasses")
			|| ! strcmp(name, "ltfs.mediaEfficiency")
			|| ! strcmp(name, "ltfs.mediaStorageAlert")
			|| ! strcmp(name, "ltfs.mediaDatasetsWritten")
			|| ! strcmp(name, "ltfs.mediaDatasetsRead")
			|| ! strcmp(name, "ltfs.mediaMBWritten")
			|| ! strcmp(name, "ltfs.mediaMBRead")
			|| ! strcmp(name, "ltfs.mediaDataPartitionTotalCapacity")
			|| ! strcmp(name, "ltfs.mediaDataPartitionAvailableSpace")
			|| ! strcmp(name, "ltfs.mediaIndexPartitionTotalCapacity")
			|| ! strcmp(name, "ltfs.mediaIndexPartitionAvailableSpace")
			|| ! strcmp(name, "ltfs.mediaEncrypted")
			|| ! strcmp(name, "ltfs.mediaPool.additionalInfo")
			|| ! strcmp(name, "ltfs.driveEncryptionState")
			|| ! strcmp(name, "ltfs.driveEncryptionMethod")
			/* Vendor specific EAs */
			|| ! strcmp(name, "ltfs.vendor.IBM.referencedBlocks")
			|| ! strcmp(name, "ltfs.vendor.IBM.trace")
			|| ! strcmp(name, "ltfs.vendor.IBM.totalBlocks")
			|| ! strcmp(name, "ltfs.vendor.IBM.cartridgeMountNode")
			|| ! strcmp(name, "ltfs.vendor.IBM.logLevel")
			|| ! strcmp(name, "ltfs.vendor.IBM.syslogLevel")
			|| ! strcmp(name, "ltfs.vendor.IBM.rao")
			|| ! strcmp(name, "ltfs.vendor.IBM.logPage")
			|| ! strcmp(name, "ltfs.vendor.IBM.mediaMAM")
			|| ! strncmp(name, "ltfs.vendor", strlen("ltfs.vendor")))
			return true;
	}

	return false;
}

/**
 * Get the value of a virtual extended attribute.
 * @param d Dentry to check.
 * @param buf Output buffer.
 * @param buf_size Output buffer size, may be zero.
 * @param name Name to check for.
 * @param vol LTFS volume
 * @return Number of bytes in output buffer (or if buf_size==0, number of bytes needed for output),
 *         -LTFS_NO_XATTR if no such readable virtual xattr exists,
 *         -LTFS_RDONLY_XATTR for write-only virtual EAs, or another negative value on error.
 */
static int _xattr_get_virtual(struct dentry *d, char *buf, size_t buf_size, const char *name,
					   struct ltfs_volume *vol)
{
	int ret = -LTFS_NO_XATTR;
	char *val = NULL;
	struct index_criteria *ic = &vol->index->index_criteria;
	cartridge_health_info h = {
		.mounts           = UNSUPPORTED_CARTRIDGE_HEALTH,
		.written_ds       = UNSUPPORTED_CARTRIDGE_HEALTH,
		.write_temps      = UNSUPPORTED_CARTRIDGE_HEALTH,
		.write_perms      = UNSUPPORTED_CARTRIDGE_HEALTH,
		.read_ds          = UNSUPPORTED_CARTRIDGE_HEALTH,
		.read_temps       = UNSUPPORTED_CARTRIDGE_HEALTH,
		.read_perms       = UNSUPPORTED_CARTRIDGE_HEALTH,
		.write_perms_prev = UNSUPPORTED_CARTRIDGE_HEALTH,
		.read_perms_prev  = UNSUPPORTED_CARTRIDGE_HEALTH,
		.written_mbytes   = UNSUPPORTED_CARTRIDGE_HEALTH,
		.read_mbytes      = UNSUPPORTED_CARTRIDGE_HEALTH,
		.passes_begin     = UNSUPPORTED_CARTRIDGE_HEALTH,
		.passes_middle    = UNSUPPORTED_CARTRIDGE_HEALTH,
		.tape_efficiency  = UNSUPPORTED_CARTRIDGE_HEALTH,
	};
	uint64_t tape_alert = 0;
	uint64_t append_pos = 0;
	struct device_capacity cap;

	/* EAs on all dentries */
	if (! strcmp(name, "ltfs.createTime")) {
		ret = _xattr_get_dentry_time(d, &d->creation_time, &val, name);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, 17222W, name, d->name.name, (unsigned long long)d->uid, (long long)d->creation_time.tv_sec);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.modifyTime")) {
		ret = _xattr_get_dentry_time(d, &d->modify_time, &val, name);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, 17222W, name, d->name.name, (unsigned long long)d->uid, (long long)d->modify_time.tv_sec);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.accessTime")) {
		ret = _xattr_get_dentry_time(d, &d->access_time, &val, name);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, 17222W, name, d->name.name, (unsigned long long)d->uid, (long long)d->access_time.tv_sec);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.changeTime")) {
		ret = _xattr_get_dentry_time(d, &d->change_time, &val, name);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, 17222W, name, d->name.name, (unsigned long long)d->uid, (long long)d->change_time.tv_sec);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.backupTime")) {
		ret = _xattr_get_dentry_time(d, &d->backup_time, &val, name);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, 17222W, name, d->name.name, (unsigned long long)d->uid, (long long)d->backup_time.tv_sec);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.driveCaptureDump")) {
		ret = tape_takedump_drive(vol->device, true);
	} else if (! strcmp(name, "ltfs.fileUID")) {
		ret = _xattr_get_u64(d->uid, &val, name);
	} else if (! strcmp(name, "ltfs.volumeUUID")) {
		ret = _xattr_get_string(vol->label->vol_uuid, &val, name);
	} else if (! strcmp(name, "ltfs.volumeName")) {
		ltfs_mutex_lock(&vol->index->dirty_lock);
		ret = _xattr_get_string(vol->index->volume_name.name, &val, name);
		ltfs_mutex_unlock(&vol->index->dirty_lock);
	} else if (! strcmp(name, "ltfs.softwareVersion")) {
		ret = _xattr_get_string(PACKAGE_VERSION, &val, name);
	} else if (! strcmp(name, "ltfs.softwareFormatSpec")) {
		ret = _xattr_get_string(LTFS_INDEX_VERSION_STR, &val, name);
	} else if (! strcmp(name, "ltfs.softwareVendor")) {
		ret = _xattr_get_string(LTFS_VENDOR_NAME, &val, name);
	} else if (! strcmp(name, "ltfs.softwareProduct")) {
		if ( strncmp( PACKAGE_VERSION, "1", 1 )==0 )
			ret = _xattr_get_string("LTFS SDE", &val, name);
		else if ( strncmp( PACKAGE_VERSION, "2", 1 )==0 )
			ret = _xattr_get_string("LTFS LE", &val, name);
		else
			ret = -LTFS_NO_XATTR;
	} else if (! strcmp(name, "ltfs.vendor.IBM.logLevel")) {
		ret = asprintf(&val, "%d", ltfs_log_level);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 10001E, name);
			val = NULL;
			ret = -LTFS_NO_MEMORY;
		}
	} else if (! strcmp(name, "ltfs.vendor.IBM.syslogLevel")) {
		ret = asprintf(&val, "%d", ltfs_syslog_level);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 10001E, name);
			val = NULL;
			ret = -LTFS_NO_MEMORY;
		}
	} else if (! strcmp(name, "ltfs.vendor.IBM.profiler")) {
		ret = ltfs_trace_get_offset(&val);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 10001E, name);
			val = NULL;
			ret = -LTFS_NO_MEMORY;
		}
	} else if (! strcmp(name, "ltfs.mamBarcode")) {
		ret = read_tape_attribute (vol, &val, name);
		if (ret < 0) {
			ltfsmsg(LTFS_DEBUG, 17198D, TC_MAM_BARCODE, "_xattr_get_virtual");
			val = NULL;
		}
	} else if (! strcmp(name, "ltfs.mamApplicationVendor")) {
		ret = read_tape_attribute (vol, &val, name);
		if (ret < 0) {
			ltfsmsg(LTFS_DEBUG, 17198D, TC_MAM_APP_VENDER, "_xattr_get_virtual");
			val = NULL;
		}
	} else if (! strcmp(name, "ltfs.mamApplicationVersion")) {
		ret = read_tape_attribute (vol, &val, name);
		if (ret < 0) {
			ltfsmsg(LTFS_DEBUG, 17198D, TC_MAM_APP_VERSION, "_xattr_get_virtual");
			val = NULL;
		}
	} else if (! strcmp(name, "ltfs.mamApplicationFormatVersion")) {
		ret = read_tape_attribute (vol, &val, name);
		if (ret < 0) {
			ltfsmsg(LTFS_DEBUG, 17198D, TC_MAM_APP_FORMAT_VERSION, "_xattr_get_virtual");
			val = NULL;
		}
	} else if (! strcmp(name, "ltfs.volumeLockState")) {
		if (vol->device) {
			unsigned int lock = 0;
			switch (vol->lock_status) {
				case LOCKED_MAM:
					lock |= VOL_LOCKED;
					break;
				case PWE_MAM:
					lock |= VOL_PERM_WRITE_ERR;
					break;
				case PERMLOCKED_MAM:
					lock |= VOL_PERM_LOCKED;
					break;
				case PWE_MAM_DP:
					lock |= VOL_PERM_WRITE_ERR;
					lock |= VOL_DP_PERM_ERR;
					break;
				case PWE_MAM_IP:
					lock |= VOL_PERM_WRITE_ERR;
					lock |= VOL_IP_PERM_ERR;
					break;
				case PWE_MAM_BOTH:
					lock |= VOL_PERM_WRITE_ERR;
					lock |= VOL_DP_PERM_ERR;
					lock |= VOL_IP_PERM_ERR;
					break;
				default:
					break;
			}
			asprintf(&val, "0x%08x", (uint32_t)(vol->device->write_protected | lock));
		} else {
			val = NULL;
			ret = -LTFS_CART_NOT_MOUNTED;
		}
	}

	/* EAs on non-empty files */
	if (ret == -LTFS_NO_XATTR && ! d->isdir && ! TAILQ_EMPTY(&d->extentlist)) {
		if (! strcmp(name, "ltfs.partition")) {
			ret = 0;
			val = malloc(2 * sizeof(char));
			if (! val) {
				ltfsmsg(LTFS_ERR, 10001E, name);
				ret = -LTFS_NO_MEMORY;
			} else {
				val[0] = TAILQ_FIRST(&d->extentlist)->start.partition;
				val[1] = '\0';
			}
		} else if (! strcmp(name, "ltfs.startblock")) {
			ret = _xattr_get_u64(TAILQ_FIRST(&d->extentlist)->start.block, &val, name);
		}
	}

	/* EAs on root dentry */
	if (ret == -LTFS_NO_XATTR && d == vol->index->root) {
		if (! strcmp(name, "ltfs.commitMessage")) {
			ltfs_mutex_lock(&vol->index->dirty_lock);
			ret = _xattr_get_string(vol->index->commit_message, &val, name);
			ltfs_mutex_unlock(&vol->index->dirty_lock);
		} else if (! strcmp(name, "ltfs.volumeSerial")) {
			ret = _xattr_get_string(vol->label->barcode, &val, name);
		} else if (! strcmp(name, "ltfs.volumeFormatTime")) {
			ret = _xattr_get_time(&vol->label->format_time, &val, name);
			if (ret == LTFS_TIME_OUT_OF_RANGE) {
				ltfsmsg(LTFS_WARN, 17222W, name, "root", (unsigned long long)0, (unsigned long long)vol->label->format_time.tv_sec);
				ret = 0;
			}
		} else if (! strcmp(name, "ltfs.volumeBlocksize")) {
			ret = _xattr_get_u64(vol->label->blocksize, &val, name);
		} else if (! strcmp(name, "ltfs.indexGeneration")) {
			ret = _xattr_get_u64(vol->index->generation, &val, name);
		} else if (! strcmp(name, "ltfs.indexTime")) {
			ret = _xattr_get_time(&vol->index->mod_time, &val, name);
			if (ret == LTFS_TIME_OUT_OF_RANGE) {
				ltfsmsg(LTFS_WARN, 17222W, name, "root", (unsigned long long)0, (unsigned long long)vol->label->format_time.tv_sec);
				ret = 0;
			}
		} else if (! strcmp(name, "ltfs.policyExists")) {
			ret = _xattr_get_string(ic->have_criteria ? "true" : "false", &val, name);
		} else if (! strcmp(name, "ltfs.policyAllowUpdate")) {
			ret = _xattr_get_string(vol->index->criteria_allow_update ? "true" : "false",
				&val, name);
		} else if (! strcmp(name, "ltfs.policyMaxFileSize") && ic->have_criteria) {
			ret = _xattr_get_u64(ic->max_filesize_criteria, &val, name);
		} else if (! strcmp(name, "ltfs.volumeCompression")) {
			ret = _xattr_get_string(vol->label->enable_compression ? "true" : "false", &val, name);
		} else if (! strcmp(name, "ltfs.indexLocation")) {
			ret = _xattr_get_tapepos(&vol->index->selfptr, &val, name);
		} else if (! strcmp(name, "ltfs.indexPrevious")) {
			ret = _xattr_get_tapepos(&vol->index->backptr, &val, name);
		} else if (! strcmp(name, "ltfs.indexCreator")) {
			ret = _xattr_get_string(vol->index->creator, &val, name);
		} else if (! strcmp(name, "ltfs.labelCreator")) {
			ret = _xattr_get_string(vol->label->creator, &val, name);
		} else if (! strcmp(name, "ltfs.indexVersion")) {
			ltfs_mutex_lock(&vol->index->dirty_lock);
			ret = _xattr_get_version(vol->index->version, &val, name);
			ltfs_mutex_unlock(&vol->index->dirty_lock);
		} else if (! strcmp(name, "ltfs.labelVersion")) {
			ret = _xattr_get_version(vol->label->version, &val, name);
		} else if (! strcmp(name, "ltfs.partitionMap")) {
			ret = _xattr_get_partmap(vol->label, &val, name);
		} else if (! strcmp(name, "ltfs.mediaLoads")) {
			ret = _xattr_get_cartridge_health(&h, &h.mounts, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaRecoveredWriteErrors")) {
			ret = _xattr_get_cartridge_health(&h, &h.write_temps, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaPermanentWriteErrors")) {
			ret = _xattr_get_cartridge_health(&h, &h.write_perms, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaRecoveredReadErrors")) {
			ret = _xattr_get_cartridge_health(&h, &h.read_temps, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaPermanentReadErrors")) {
			ret = _xattr_get_cartridge_health(&h, &h.read_perms, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaPreviousPermanentWriteErrors")) {
			ret = _xattr_get_cartridge_health(&h, &h.write_perms_prev, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaPreviousPermanentReadErrors")) {
			ret = _xattr_get_cartridge_health(&h, &h.read_perms_prev, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaBeginningMediumPasses")) {
			ret = _xattr_get_cartridge_health(&h, &h.passes_begin, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaMiddleMediumPasses")) {
			ret = _xattr_get_cartridge_health(&h, &h.passes_middle, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaEfficiency")) {
			ret = _xattr_get_cartridge_health(&h, &h.tape_efficiency, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaDatasetsWritten")) {
			ret = _xattr_get_cartridge_health_u64(&h, &h.written_ds, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaDatasetsRead")) {
			ret = _xattr_get_cartridge_health_u64(&h, &h.read_ds, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaMBWritten")) {
			ret = _xattr_get_cartridge_health_u64(&h, &h.written_mbytes, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaMBRead")) {
			ret = _xattr_get_cartridge_health_u64(&h, &h.read_mbytes, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaStorageAlert")) {
			ret = ltfs_get_tape_alert_unlocked(&tape_alert, vol);
			if (ret < 0)
				val = NULL;
			else {
				ret = asprintf(&val, "0x%016"PRIx64, tape_alert);
				if (ret < 0) {
					ltfsmsg(LTFS_ERR, 10001E, name);
					val = NULL;
					ret = -LTFS_NO_MEMORY;
				}
			}
		} else if (! strcmp(name, "ltfs.mediaDataPartitionTotalCapacity")) {
			ret = _xattr_get_cartridge_capacity(&cap, &cap.total_dp, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaDataPartitionAvailableSpace")) {
			ret = _xattr_get_cartridge_capacity(&cap, &cap.remaining_dp, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaIndexPartitionTotalCapacity")) {
			ret = _xattr_get_cartridge_capacity(&cap, &cap.total_ip, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaIndexPartitionAvailableSpace")) {
			ret = _xattr_get_cartridge_capacity(&cap, &cap.remaining_ip, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaEncrypted")) {
			ret = _xattr_get_string(tape_get_media_encrypted(vol->device), &val, name);
		} else if (! strcmp(name, "ltfs.mediaPool.additionalInfo")) {
			char *tmp=NULL;
			ret = tape_get_media_pool_info(vol, &tmp, &val);
			if (ret < 0 || !val)
				ret = -LTFS_NO_XATTR;
		} else if (! strcmp(name, "ltfs.driveEncryptionState")) {
			ret = _xattr_get_string(tape_get_drive_encryption_state(vol->device), &val, name);
		} else if (! strcmp(name, "ltfs.driveEncryptionMethod")) {
			ret = _xattr_get_string(tape_get_drive_encryption_method(vol->device), &val, name);
		} else if (! strcmp(name, "ltfs.vendor.IBM.referencedBlocks")) {
			ret = _xattr_get_u64(ltfs_get_valid_block_count_unlocked(vol), &val, name);
		} else if (! strcmp(name, "ltfs.vendor.IBM.trace")) {
			ret = ltfs_get_trace_status(&val);
		} else if (! strcmp(name, "ltfs.vendor.IBM.totalBlocks")) {
			ret = ltfs_get_append_position(&append_pos, vol);
			if (ret < 0)
				val = NULL;
			else
				ret = _xattr_get_u64(append_pos, &val, name);
		} else if (! strcmp(name, "ltfs.vendor.IBM.cartridgeMountNode")) {
			ret = asprintf(&val, "localhost");
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 10001E, name);
				val = NULL;
				ret = -LTFS_NO_MEMORY;
			}
		} else if ( (!strncmp(name, "ltfs.vendor.IBM.logPage.", strlen("ltfs.vendor.IBM.logPage."))) &&
					(strlen(name) == strlen("ltfs.vendor.IBM.logPage.XX.XX")) ) {
			char page_str[3]    = {0x00, 0x00, 0x00};
			char subpage_str[3] = {0x00, 0x00, 0x00};

			uint8_t page    = 0xFF;
			uint8_t subpage = 0xFF;

			char *endptr = NULL;

			page_str[0]    = name[24];
			page_str[1]    = name[25];
			subpage_str[0] = name[27];
			subpage_str[1] = name[28];

			page = (uint8_t)(strtoul(page_str, &endptr, 16));
			if (*endptr) return -LTFS_NO_XATTR;

			subpage = (uint8_t)(strtoul(subpage_str, &endptr, 16));
			if (*endptr) return -LTFS_NO_XATTR;

			ret = ltfs_logpage(page, subpage, (unsigned char *)buf, buf_size, vol);

		} else if ( (!strncmp(name, "ltfs.vendor.IBM.mediaMAM.", strlen("ltfs.vendor.IBM.mediaMAM."))) &&
					(strlen(name) == strlen("ltfs.vendor.IBM.mediaMAM.XX")) ) {
			char part_str[3] = {0x00, 0x00, 0x00};
			tape_partition_t part = 0;

			char *endptr = NULL;

			part_str[0] = name[25];
			part_str[1] = name[26];

			if (!strncmp(part_str, "IP", sizeof(part_str))) {
				part = ltfs_part_id2num(vol->label->partid_ip, vol);
			} else if (!strncmp(part_str, "DP", sizeof(part_str))) {
				part = ltfs_part_id2num(vol->label->partid_dp, vol);;
			} else {
				part = (uint8_t)(strtoul(part_str, &endptr, 16));
				if (*endptr) return -LTFS_NO_XATTR;
			}

			if (part > 1) return -LTFS_NO_XATTR;

			ret = ltfs_mam(part, (unsigned char *)buf, buf_size, vol);

		} else if (! strncmp(name, "ltfs.vendor", strlen("ltfs.vendor"))) {
			if (! strncmp(name + strlen("ltfs.vendor."), LTFS_VENDOR_NAME, strlen(LTFS_VENDOR_NAME))) {
				ret = _xattr_get_vendorunique_xattr(&val, name, vol);
			}
		} else if (! strcmp(name, "ltfs.sync")) {
			ret = ltfs_sync_index(SYNC_EA, false, vol);
		}
	}

	if (val) {
		ret = strlen(val);
		if (buf_size) {
			if (buf_size < (size_t)ret)
				ret = -LTFS_SMALL_BUFFER;
			else
				memcpy(buf, val, ret);
		}
		free(val);
	}

	return ret;
}

/**
 * Write user-supplied data to a virtual extended attribute for a given dentry.
 * The caller always has a write lock on vol-index->lock.
 * @param d Dentry to set the xattr on.
 * @param name Name to set.
 * @param value Value to set, may be binary, not necessarily null-terminated.
 * @param size Size of value in bytes.
 * @param vol LTFS volume
 * @return 0 on success, -LTFS_NO_XATTR if the xattr is not a settable virtual xattr,
 *         or another negative value on error.
 */
static int _xattr_set_virtual(struct dentry *d, const char *name, const char *value,
							  size_t size, struct ltfs_volume *vol)
{
	int ret = 0;

	if (! strcmp(name, "ltfs.sync") && d == vol->index->root)
		ret = ltfs_sync_index(SYNC_EA, false, vol);
	else if (! strcmp(name, "ltfs.commitMessage") && d == vol->index->root) {
		char *value_null_terminated, *new_value;

		if (size > INDEX_MAX_COMMENT_LEN) {
			ltfsmsg(LTFS_ERR, 11308E);
			ret = -LTFS_LARGE_XATTR;
		}

		ltfs_mutex_lock(&vol->index->dirty_lock);
		if (! value || ! size) {
			/* Clear the current comment field */
			if (vol->index->commit_message) {
				free(vol->index->commit_message);
				vol->index->commit_message = NULL;
			}
		} else {
			value_null_terminated = malloc(size + 1);
			if (! value_null_terminated) {
				ltfsmsg(LTFS_ERR, 10001E, "_xattr_set_virtual: commit_message");
				ltfs_mutex_unlock(&vol->index->dirty_lock);
				return -LTFS_NO_MEMORY;
			}
			memcpy(value_null_terminated, value, size);
			value_null_terminated[size] = '\0';

			ret = pathname_format(value_null_terminated, &new_value, false, true);
			free(value_null_terminated);
			if (ret < 0) {
				ltfs_mutex_unlock(&vol->index->dirty_lock);
				return ret;
			}
			ret = 0;

			/* Update the commit message in the index */
			if (vol->index->commit_message)
				free(vol->index->commit_message);
			vol->index->commit_message = new_value;
		}

		ltfs_set_index_dirty(false, false, vol->index);
		ltfs_mutex_unlock(&vol->index->dirty_lock);

	} else if (! strcmp(name, "ltfs.volumeName") && d == vol->index->root) {
		char *value_null_terminated, *new_value;

		ltfs_mutex_lock(&vol->index->dirty_lock);
		if (! value || ! size) {
			fs_clear_nametype(&vol->index->volume_name);
			/* Clear tape attribute(TC_MAM_USER_MEDIUM_LABEL) */
			ret =  update_tape_attribute (vol, NULL, TC_MAM_USER_MEDIUM_LABEL, 0);
			if ( ret < 0 ) {
				ltfsmsg(LTFS_WARN, 17199W, TC_MAM_USER_MEDIUM_LABEL, "_xattr_set_virtual");
			}
		} else {
			value_null_terminated = malloc(size + 1);
			if (! value_null_terminated) {
				ltfsmsg(LTFS_ERR, 10001E, "_xattr_set_virtual: volume name");
				ltfs_mutex_unlock(&vol->index->dirty_lock);
				return -LTFS_NO_MEMORY;
			}
			memcpy(value_null_terminated, value, size);
			value_null_terminated[size] = '\0';

			ret = pathname_format(value_null_terminated, &new_value, true, false);
			free(value_null_terminated);
			if (ret < 0) {
				ltfs_mutex_unlock(&vol->index->dirty_lock);
				return ret;
			}
			ret = 0;

			/* Update the volume name in the index */
			fs_clear_nametype(&vol->index->volume_name);
			fs_set_nametype(&vol->index->volume_name, new_value);

			/* Update tape attribute(TC_MAM_USER_MEDIUM_LABEL) */
			ret =  update_tape_attribute (vol, new_value, TC_MAM_USER_MEDIUM_LABEL, size);
			if ( ret < 0 ) {
				ltfsmsg(LTFS_WARN, 17199W, TC_MAM_USER_MEDIUM_LABEL, "_xattr_set_virtual");
				return ret;
			}
		}

		ltfs_set_index_dirty(false, false, vol->index);
		ltfs_mutex_unlock(&vol->index->dirty_lock);

	} else if (! strcmp(name, "ltfs.createTime")) {
		ret = _xattr_set_time(d, &d->creation_time, value, size, name, vol);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, 17221W, name, d->name.name, (unsigned long long)d->uid, value);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.modifyTime")) {
		get_current_timespec(&d->change_time);
		ret = _xattr_set_time(d, &d->modify_time, value, size, name, vol);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, 17221W, name, d->name.name, (unsigned long long)d->uid, value);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.changeTime")) {
		ret = _xattr_set_time(d, &d->change_time, value, size, name, vol);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, 17221W, name, d->name.name, (unsigned long long)d->uid, value);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.accessTime")) {
		ret = _xattr_set_time(d, &d->access_time, value, size, name, vol);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, 17221W, name, d->name.name, (unsigned long long)d->uid, value);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.backupTime")) {
		ret = _xattr_set_time(d, &d->backup_time, value, size, name, vol);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, 17221W, name, d->name.name, (unsigned long long)d->uid, value);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.driveCaptureDump")) {
		ret = tape_takedump_drive(vol->device, true);
	} else if (! strcmp(name, "ltfs.mediaStorageAlert")) {
		uint64_t tape_alert = 0;
		char *invalid_start, *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		/* ltfs.mediaStorageAlert shall be specified by hexadecimal text */
		tape_alert = strtoull(v, &invalid_start, 16);
		if( (*invalid_start == '\0') && v )
			ret = ltfs_clear_tape_alert(tape_alert, vol);
		else
			ret = -LTFS_STRING_CONVERSION;
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.logLevel")) {
		int level = 0;
		char *invalid_start, *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		/* ltfs.vendor.IBM.logLevel shall be specified by hexadecimal text */
		level = strtoul(v, &invalid_start, 0);
		if( (*invalid_start == '\0') && v ) {
			ret = 0;
			ltfs_set_log_level(level);
		} else
			ret = -LTFS_STRING_CONVERSION;
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.syslogLevel")) {
		int level = 0;
		char *invalid_start, *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		/* ltfs.vendor.IBM.syslogLevel shall be specified by hexadecimal text */
		level = strtoul(v, &invalid_start, 0);
		if( (*invalid_start == '\0') && v ) {
			ret = 0;
			ltfs_set_syslog_level(level);
		} else
			ret = -LTFS_STRING_CONVERSION;
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.rao")) {
		char *v;
		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}
		if (strlen(v) > PATH_MAX) return -LTFS_LARGE_XATTR; /* file path size check */
		ret = ltfs_get_rao_list(v, vol);
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.trace")) {
		char *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		ret = ltfs_set_trace_status(v);
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.dump")) {
		char *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		ret = ltfs_dump(v, vol->work_directory);
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.dumpTrace")) {
		char *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		ret = ltfs_trace_dump(v, vol->work_directory);
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.profiler")) {
		uint64_t source = 0;
		char *invalid_start, *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		source = strtoull(v, &invalid_start, 0);
		if( (*invalid_start == '\0') && v ) {
			/* Set request profiler */
			if (source & PROF_REQ)
				ret = ltfs_request_profiler_start(vol->work_directory);
			else
				ret = ltfs_request_profiler_stop();

			ret = ltfs_profiler_set(source, vol);
		} else
			ret = -LTFS_STRING_CONVERSION;
		free(v);
	} else if (! strncmp(name, "ltfs.vendor", strlen("ltfs.vendor"))) {
			if (! strncmp(name + strlen("ltfs.vendor."), LTFS_VENDOR_NAME, strlen(LTFS_VENDOR_NAME))) {
				ret = _xattr_set_vendorunique_xattr(name, value, size, vol);
			}
	} else if (! strcmp(name, "ltfs.mamBarcode")) {
		ret =  update_tape_attribute (vol, value, TC_MAM_BARCODE, size);
		if ( ret < 0 ) {
			ltfsmsg(LTFS_WARN, 17199W, TC_MAM_USER_MEDIUM_LABEL, "_xattr_set_virtual");
			return ret;
		}
	} else if (! strcmp(name, "ltfs.volumeLockState")) {
		unsigned int lock = 0;
		char *invalid_start, *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		lock = strtoull(v, &invalid_start, 0);
		if( (*invalid_start == '\0') && v ) {
			mam_lockval new = UNLOCKED_MAM;
			char status_mam[TC_MAM_LOCKED_MAM_SIZE];

			switch (vol->t_attr->vollock) {
				case PWE_MAM:
				case PWE_MAM_DP:
				case PWE_MAM_IP:
				case PWE_MAM_BOTH:
					/* Write perm tape cannot be updated */
					return -LTFS_XATTR_ERR;
					break;
				default:
					/* Fall through */
					break;
			}

			if (vol->index->vollock == PERMLOCKED_MAM) {
				/* Advisory perm-locked tape cannot be updated */
				return -LTFS_XATTR_ERR;
			}

			if ((lock & VOL_LOCKED) && (lock & VOL_PERM_LOCKED)) {
				/* Invalid value to specify */
				return -LTFS_XATTR_ERR;
			}

			if (lock & VOL_LOCKED)
				new = LOCKED_MAM;
			else if (lock & VOL_PERM_LOCKED)
				new = PERMLOCKED_MAM;
			else
				new = UNLOCKED_MAM;

			if (vol->file_open_count != 0) {
				ltfsmsg(LTFS_DEBUG, 10021D, "_xattr_set_virtual", "file open", vol->file_open_count, 0);
				return -LTFS_XATTR_ERR;
			}

			status_mam[0] = new;

			/* update MAM attribute */
			ret =  update_tape_attribute(vol, status_mam, TC_MAM_LOCKED_MAM, TC_MAM_LOCKED_MAM_SIZE);
			if ( ret < 0 ) {
				ltfsmsg(LTFS_WARN, 17199W, TC_MAM_LOCKED_MAM, "_xattr_set_virtual");
				return ret;
			}

			vol->index->vollock = new;
			vol->t_attr->vollock = new;
			vol->lock_status = new;

			ltfs_set_index_dirty(false, false, vol->index);
			ret = ltfs_sync_index(SYNC_ADV_LOCK, false, vol);
			ret = tape_device_lock(vol->device);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 12010E, __FUNCTION__);
				return ret;
			}
			ret = ltfs_write_index(ltfs_ip_id(vol), SYNC_EA, vol);
			tape_device_unlock(vol->device);
		} else
			ret = -LTFS_STRING_CONVERSION;

		free(v);
	} else if (! strcmp(name, "ltfs.mediaPool.additionalInfo")) {
		ret = tape_set_media_pool_info(vol, value, size, false);
	} else
		ret = -LTFS_NO_XATTR;

	return ret;
}

/**
 * "Remove" a virtual extended attribute. This is disallowed for many virtual xattrs,
 * but some have a meaningful removal operation.
 * @param d Dentry to remove xattr from.
 * @param name Attribute to remove.
 * @param vol LTFS volume.
 * @return 0 on success, -LTFS_NO_XATTR if the xattr is not a removable virtual xattr, or another
 *         negative value on error.
 */
static int _xattr_remove_virtual(struct dentry *d, const char *name, struct ltfs_volume *vol)
{
	int ret = 0;

	if (! strcmp(name, "ltfs.commitMessage") && d == vol->index->root) {
		ltfs_mutex_lock(&vol->index->dirty_lock);
		if (vol->index->commit_message) {
			free(vol->index->commit_message);
			vol->index->commit_message = NULL;
			ltfs_set_index_dirty(false, false, vol->index);
		}
		ltfs_mutex_unlock(&vol->index->dirty_lock);
	} else if (! strcmp(name, "ltfs.volumeName") && d == vol->index->root) {
		ltfs_mutex_lock(&vol->index->dirty_lock);
		if (vol->index->volume_name.name) {
			fs_clear_nametype(&vol->index->volume_name);
			ltfs_set_index_dirty(false, false, vol->index);
		}
		/* Clear tape attribute(TC_MAM_USER_MEDIUM_LABEL) */
		ret =  update_tape_attribute (vol, NULL, TC_MAM_USER_MEDIUM_LABEL, 0);
		if ( ret < 0 ) {
			ltfsmsg(LTFS_WARN, 17199W, TC_MAM_USER_MEDIUM_LABEL, "_xattr_set_virtual");
		}
		ltfs_mutex_unlock(&vol->index->dirty_lock);
	} else
		ret = -LTFS_NO_XATTR;

	return ret;
}

/* Global functions */

int xattr_do_set(struct dentry *d, const char *name, const char *value, size_t size,
	struct xattr_info *xattr)
{
	int ret = 0;

	/* clear existing xattr or set up new one */
	if (xattr) {
		if (xattr->value) {
			free(xattr->value);
			xattr->value = NULL;
		}
	} else {
		xattr = (struct xattr_info *) calloc(1, sizeof(struct xattr_info));
		if (! xattr) {
			ltfsmsg(LTFS_ERR, 10001E, "xattr_do_set: xattr");
			return -LTFS_NO_MEMORY;
		}
		xattr->key.name = strdup(name);
		if (! xattr->key.name) {
			ltfsmsg(LTFS_ERR, 10001E, "xattr_do_set: xattr key");
			ret = -LTFS_NO_MEMORY;
			goto out_free;
		}
		xattr->key.percent_encode = fs_is_percent_encode_required(xattr->key.name);
		TAILQ_INSERT_HEAD(&d->xattrlist, xattr, list);
	}

	/* copy new value */
	xattr->size = size;
	if (size > 0) {
		xattr->value = (char *)malloc(size);
		if (! xattr->value) {
			ltfsmsg(LTFS_ERR, 10001E, "xattr_do_set: xattr value");
			ret = -LTFS_NO_MEMORY;
			goto out_remove;
		}
		memcpy(xattr->value, value, size);
	}
	return 0;

out_remove:
	TAILQ_REMOVE(&d->xattrlist, xattr, list);
out_free:
	if (xattr->key.name)
		free(xattr->key.name);
	free(xattr);
	return ret;
}

/**
 * Set an extended attribute.
 * @param d File or directory to set the xattr on.
 * @param name Name to set.
 * @param value Value to set, may be binary, not necessarily null-terminated.
 * @param size Size of value in bytes.
 * @param flags XATTR_REPLACE to fail if xattr doesn't exist, XATTR_CREATE to fail if it does
 *              exist, or 0 to ignore any existing value.
 * @return 0 on success or a negative value on error.
 */
int xattr_set(struct dentry *d, const char *name, const char *value, size_t size,
	int flags, struct ltfs_volume *vol)
{
	struct xattr_info *xattr;
	bool replace, create;
	int ret;
	bool is_worm_cart = false;
	bool disable_worm_ea = false;
	char *new_value="1";
	bool write_idx = false;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(value, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	if (size > LTFS_MAX_XATTR_SIZE)
		return -LTFS_LARGE_XATTR; /* this is the error returned by ext3 when the xattr is too large */

	replace = flags & XATTR_REPLACE;
	create = flags & XATTR_CREATE;

	ret = _xattr_lock_dentry(name, true, d, vol);
	if (ret < 0)
		return ret;

	ret = tape_get_worm_status(vol->device, &is_worm_cart);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 17237E, "set xattr: cart stat");
		ret = -LTFS_XATTR_ERR;
		goto out_unlock;
	}

	if ((is_worm_cart && (d->is_immutable || (d->is_appendonly && strcmp(name, "ltfs.vendor.IBM.immutable"))))
		|| (!is_worm_cart && (d->is_immutable || d->is_appendonly) && !_xattr_is_worm_ea(name))) {
		/* EA cannot be set in case of immutable/appendonly */
		ltfsmsg(LTFS_ERR, 17237E, "set xattr: WORM entry");
		ret = -LTFS_RDONLY_XATTR;
		goto out_unlock;
	}

	/* Check if this is a user-writeable virtual xattr */
	if (_xattr_is_virtual(d, name, vol)) {
		ret = _xattr_set_virtual(d, name, value, size, vol);
		if (ret == -LTFS_NO_XATTR)
			ret = -LTFS_RDONLY_XATTR;
		goto out_unlock;
	}

	/* In the future, there could be user-writeable reserved xattrs. For now, just deny
	 * writes to all reserved xattrs not covered by the user-writeable virtual xattrs above. */
	if (strcasestr(name, "ltfs") == name && !_xattr_is_stored_vea(name) && !_xattr_is_worm_ea(name)) {
		ret = -LTFS_RDONLY_XATTR;
		goto out_unlock;
	}

	acquirewrite_mrsw(&d->meta_lock);

	/* Search for existing xattr with this name. */
	ret = _xattr_seek(&xattr, d, name);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11122E, ret);
		releasewrite_mrsw(&d->meta_lock);
		goto out_unlock;
	}
	if (create && xattr) {
		releasewrite_mrsw(&d->meta_lock);
		ret = -LTFS_XATTR_EXISTS;
		goto out_unlock;
	} else if (replace && ! xattr) {
		releasewrite_mrsw(&d->meta_lock);
		ret = -LTFS_NO_XATTR;
		goto out_unlock;
	}
	if (_xattr_is_worm_ea(name)) {
		disable_worm_ea = (strncmp(value, "0", size) == 0);

		if (is_worm_cart && disable_worm_ea) {
			ltfsmsg(LTFS_ERR, 17237E, "set xattr: clear WORM");
			releasewrite_mrsw(&d->meta_lock);
			ret = -LTFS_XATTR_ERR;
			goto out_unlock;
		}
		if (!disable_worm_ea) {
			/* All values other than 0 is treated as 1 */
			value = new_value;
			size = strlen(new_value);
		}
	}

	if (!strcmp(name, "ltfs.mediaPool.name")) {
		ret = tape_set_media_pool_info(vol, value, size, true);
		if (ret < 0) {
			releasewrite_mrsw(&d->meta_lock);
			goto out_unlock;
		}
		write_idx = true;
	}

	/* Set extended attribute */
	ret = xattr_do_set(d, name, value, size, xattr);
	if (ret < 0) {
		releasewrite_mrsw(&d->meta_lock);
		goto out_unlock;
	}

	/* update metadata */
	if (!strcmp(name, "ltfs.vendor.IBM.immutable")) {
		d->is_immutable = !disable_worm_ea;
		ltfsmsg(LTFS_INFO, 17238I, "immutable", d->is_immutable, d->name.name);
	}
	else if (!strcmp(name, "ltfs.vendor.IBM.appendonly")) {
		d->is_appendonly = !disable_worm_ea;
		ltfsmsg(LTFS_INFO, 17238I, "appendonly", d->is_appendonly, d->name.name);
	}

	get_current_timespec(&d->change_time);
	releasewrite_mrsw(&d->meta_lock);
	d->dirty = true;
	ltfs_set_index_dirty(true, false, vol->index);

	if (write_idx)
		ret = ltfs_sync_index(SYNC_EA, false, vol);
	else
		ret = 0;

out_unlock:
	_xattr_unlock_dentry(name, true, d, vol);
	return ret;
}

/**
 * Get an extended attribute. Returns an error if the provided buffer is not large enough
 * to contain the attribute value.
 * @param d File/directory to check
 * @param name Xattr name
 * @param value On success, contains xattr value
 * @param size Output buffer size in bytes
 * @param vol LTFS volume
 * @return if size is nonzero, number of bytes returned in the value buffer. if size is zero,
 *         number of bytes in the xattr value. returns a negative value on error. If the
 *         operation needs to be restarted, then -LTFS_RESTART_OPERATION is returned instead.
 */
int xattr_get(struct dentry *d, const char *name, char *value, size_t size,
	struct ltfs_volume *vol)
{
	struct xattr_info *xattr = NULL;
	int ret;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	if (size > 0 && ! value) {
		ltfsmsg(LTFS_ERR, 11123E);
		return -LTFS_BAD_ARG;
	}

	ret = _xattr_lock_dentry(name, false, d, vol);
	if (ret < 0)
		return ret;

	/* Try to get a virtual xattr first. */
	if (_xattr_is_virtual(d, name, vol)) {

		if (vol->mount_type == MOUNT_ROLLBACK_META) {
			_xattr_unlock_dentry(name, false, d, vol);
			return -LTFS_DEVICE_UNREADY;
		}

		ret = _xattr_get_virtual(d, value, size, name, vol);
		if (ret == -LTFS_DEVICE_FENCED) {
			_xattr_unlock_dentry(name, false, d, vol);
			ret = ltfs_wait_revalidation(vol);
			return (ret == 0) ? -LTFS_RESTART_OPERATION : ret;
		} else if (NEED_REVAL(ret)) {
			_xattr_unlock_dentry(name, false, d, vol);
			ret = ltfs_revalidate(false, vol);
			return (ret == 0) ? -LTFS_RESTART_OPERATION : ret;
		} else if (IS_UNEXPECTED_MOVE(ret)) {
			vol->reval = -LTFS_REVAL_FAILED;
			_xattr_unlock_dentry(name, false, d, vol);
			return ret;
		}else if (ret != -LTFS_NO_XATTR) {
			/* if ltfs.sync is specified, don't print any message */
			if (ret < 0 && ret != -LTFS_RDONLY_XATTR)
				ltfsmsg(LTFS_ERR, 11128E, ret);
			goto out_unlock;
		}
	}

	acquireread_mrsw(&d->meta_lock);

	/* Look for a real xattr. */
	ret = _xattr_seek(&xattr, d, name);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11129E, ret);
		releaseread_mrsw(&d->meta_lock);
		goto out_unlock;
	}

	/* Generate output. */
	ret = 0;
	if (! xattr) {
		/* There's no such extended attribute */
		ret = -LTFS_NO_XATTR;
	} else if (size && xattr->size > size) {
		/* There is no space to fill the buffer */
		ret = -LTFS_SMALL_BUFFER;
	} else if (size) {
		/* Copy the extended attribute to the requester */
		memcpy(value, xattr->value, xattr->size);
		ret = xattr->size;
	} else /* size is zero */ {
		/* Return how many bytes will be necessary to read this xattr */
		ret = xattr->size;
	}

	releaseread_mrsw(&d->meta_lock);

out_unlock:
	_xattr_unlock_dentry(name, false, d, vol);
	return ret;
}

/**
 * Copy a list of extended attribute names to a user-provided buffer.
 * @param d File/directory to get the list of extended attributes from
 * @param list Output buffer for xattr names
 * @param size Output buffer size in bytes
 * @param vol LTFS volume
 * @return number of bytes in buffer on success, or a negative value on error.
 */
int xattr_list(struct dentry *d, char *list, size_t size, struct ltfs_volume *vol)
{
	int ret, nbytes = 0;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	if (size > 0 && ! list) {
		ltfsmsg(LTFS_ERR, 11130E);
		return -LTFS_BAD_ARG;
	}

	acquireread_mrsw(&d->meta_lock);

	/* Fill the buffer with only real xattrs. */
	if (size)
		memset(list, 0, size);

	ret = _xattr_list_physicals(d, list, size);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11133E, ret);
		goto out;
	}
	nbytes += ret;

	/*
	 * There used to be an _xattr_list_virtuals function which was called here.
	 * Listing virtual xattrs causes problems with files copied from LTFS to another filesystem
	 * which are attempted to be brought back. Since the copy utility may also copy the
	 * reserved virtual extended attributes, the copy operation will fail with permission
	 * denied problems.
	 */

	/* Was the buffer large enough? */
	if (size && (size_t)nbytes > size)
		ret = -LTFS_SMALL_BUFFER;

out:
	releaseread_mrsw(&d->meta_lock);
	if (ret < 0)
		return ret;
	return nbytes;
}

/**
 * Actually remove an extended attribute.
 * @param dentry dentry to operate on
 * @param name xattr name to delete
 * @param force true to force removal, false to verify namespaces first
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error
 */
int xattr_do_remove(struct dentry *d, const char *name, bool force, struct ltfs_volume *vol)
{
	int ret;
	struct xattr_info *xattr;

	acquirewrite_mrsw(&d->meta_lock);

	/* Look for a real extended attribute. */
	ret = _xattr_seek(&xattr, d, name);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11140E, ret);
		releasewrite_mrsw(&d->meta_lock);
		return ret;
	} else if (! xattr) {
		releasewrite_mrsw(&d->meta_lock);
		return -LTFS_NO_XATTR;
	}

	if (! force) {
		/* If this xattr is in the reserved namespace, the user can't remove it. */
		/* TODO: in the future, there could be user-removable reserved xattrs. */
		if (strcasestr(name, "ltfs") == name && !_xattr_is_stored_vea(name) && !_xattr_is_worm_ea(name)) {
			releasewrite_mrsw(&d->meta_lock);
			return -LTFS_RDONLY_XATTR;
		}
	}

	/* Remove the xattr. */
	TAILQ_REMOVE(&d->xattrlist, xattr, list);
	get_current_timespec(&d->change_time);
	releasewrite_mrsw(&d->meta_lock);

	free(xattr->key.name);
	if (xattr->value)
		free(xattr->value);
	free(xattr);

	return 0;
}

/**
 * Remove an extended attribute.
 * @param d File/directory to operate on
 * @param name Extended attribute name to delete
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error
 */
int xattr_remove(struct dentry *d, const char *name, struct ltfs_volume *vol)
{
	int ret;
	bool is_worm_cart = false;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	ret = _xattr_lock_dentry(name, true, d, vol);
	if (ret < 0)
		return ret;

	ret = tape_get_worm_status(vol->device, &is_worm_cart);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 17237E, "remove xattr: cart stat");
		ret = -LTFS_XATTR_ERR;
		goto out_dunlk;
	}

	if ((d->is_immutable || d->is_appendonly)
		&& (is_worm_cart || !_xattr_is_worm_ea(name))) {
		/* EA cannot be removed in case of immutable/appendonly */
		ltfsmsg(LTFS_ERR, 17237E, "remove xattr: WORM entry");
		ret = -LTFS_RDONLY_XATTR;
		goto out_dunlk;
	}

	/* If this xattr is virtual, try the virtual removal function. */
	if (_xattr_is_virtual(d, name, vol)) {
		ret = _xattr_remove_virtual(d, name, vol);
		if (ret == -LTFS_NO_XATTR)
			ret = -LTFS_RDONLY_XATTR; /* non-removable virtual xattr */
		goto out_dunlk;
	}

	ret = xattr_do_remove(d, name, false, vol);
	if (ret < 0)
		goto out_dunlk;

	if (!strcmp(name, "ltfs.vendor.IBM.immutable")) {
		d->is_immutable = false;
		ltfsmsg(LTFS_INFO, 17238I, "immutable", d->is_immutable, d->name.name);
	}
	else if (!strcmp(name, "ltfs.vendor.IBM.appendonly")) {
		d->is_appendonly = false;
		ltfsmsg(LTFS_INFO, 17238I, "appendonly", d->is_appendonly, d->name.name);
	}

	d->dirty = true;
	ltfs_set_index_dirty(true, false, vol->index);

out_dunlk:
	_xattr_unlock_dentry(name, true, d, vol);
	return ret;
}

/**
 * Strip a Linux namespace prefix from the given xattr name and return the position of the suffix.
 * If the name is "user.X", return the "X" portion. Otherwise, return an error.
 * This function does nothing on Mac OS X.
 * @param name Name to strip.
 * @return A pointer to the name suffix, or NULL to indicate an invalid name. On Mac OS X,
 *         always returns @name.
 */
const char *xattr_strip_name(const char *name)
{
#if (defined (__APPLE__) || defined (mingw_PLATFORM))
	return name;
#else
	if (strstr(name, "user.") == name)
		return name + 5;
	else
		return NULL;
#endif
}

/**
 * set LTFS_LIVELINK_EA_NAME
 * @param path file path
 * @param d File operate on
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error
 */
int xattr_set_mountpoint_length(struct dentry *d, const char* value, size_t size )
{
#ifdef POSIXLINK_ONLY
	return 0;
#else
	int ret=0;
	struct xattr_info *xattr;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(value, -LTFS_NULL_ARG);

	acquireread_mrsw(&d->meta_lock);
	ret = _xattr_seek(&xattr, d, LTFS_LIVELINK_EA_NAME);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11129E, ret);
		releaseread_mrsw(&d->meta_lock);
		goto out_set;
	}
	ret = xattr_do_set(d, LTFS_LIVELINK_EA_NAME, value, size, xattr);
	releaseread_mrsw(&d->meta_lock);

out_set:
	return ret;
#endif
}
