/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2020 IBM Corp. All rights reserved.
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
** FILE NAME:       tape.h
**
** DESCRIPTION:     Prototypes for a backend-independent tape drive interface.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/

#ifndef __tape_h__
#define __tape_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_locking.h"
#include "tape_ops.h"
#include "ltfs.h"

#define KB   (1024)
#define MB   (KB * 1024)
#define GB   (MB * 1024)

#define NEED_REVAL(ret) (ret == -EDEV_POR_OR_BUS_RESET	\
						 || ret == -EDEV_MEDIUM_MAY_BE_CHANGED	\
						 || ret == -EDEV_RESERVATION_PREEMPTED	\
						 || ret == -EDEV_REGISTRATION_PREEMPTED \
						 || ret == -EDEV_REAL_POWER_ON_RESET	\
		                 || ret == -EDEV_NEED_FAILOVER)

#define IS_UNEXPECTED_MOVE(ret) (ret == -EDEV_MEDIUM_REMOVAL_REQ)

struct device_data {
	struct tc_position position;          /**< Current head position */
	tape_block_t append_pos[2];           /**< Append positions, 0 means append at EOD */
	ltfs_mutex_t append_pos_mutex;        /**< Mutex to control append_pos access */

	uint32_t max_block_size;              /**< Maximum block size, in bytes */
	int partition_space[2];               /**< Remaining space status per partition */
	unsigned int write_protected;         /**< Write protect status of the tape (use bit field of volumelock_status) */
	bool write_error;                     /**< Did a write error happen in the current mount? */
	bool device_reserved;                 /**< Do we have exclusive use of the device? */
	bool medium_locked;                   /**< Is the medium locked in the device? */
	bool fence;                           /**< Are device lock requests blocked? */
	bool append_only_mode;                /**< Is in the append only mode? */
	bool is_worm;                         /**< Is WORM tape? */
	bool is_encrypted;                    /**< Is tape encrypted? */
	struct ltfs_timespec previous_exist;  /**< Previous time to be confirm drive connection presence */

	struct tape_ops *backend;             /**< Backend functions */
	void *backend_data;                   /**< Backend private data */
	ltfs_mutex_t backend_mutex;           /**< Mutex to control backend access */
	ltfs_mutex_t read_only_flag_mutex;    /**< Mutex to control read_only access */
	char *serial_number;                  /**< Serial number for identification */
};

int tape_device_alloc(struct device_data **device);
void tape_device_free(struct device_data **device, void * const kmi_handle, bool force);

int tape_device_open(struct device_data *dev, const char *devname, struct tape_ops *ops,
	void * const kmi_handle);
int tape_device_reopen(struct device_data *device, const char *devname);

void _tape_device_close(struct device_data *device, void * const kmi_handle,
						bool skip_aom_setting, bool force_release);
void tape_device_close_raw(struct device_data *dev);
#define tape_device_close(dev, kmi_handle, force) _tape_device_close(dev, kmi_handle, false, force)

int tape_device_is_connected(struct device_data *dev, struct tape_ops *ops);
int tape_get_info(struct device_data *dev, struct tc_drive_info *info);
int tape_reserve_device(struct device_data *dev);
void tape_release_device(struct device_data *dev);
const char *tape_default_device_name(struct tape_ops *ops);
int tape_get_device_list(struct tape_ops *ops, struct tc_drive_info *buf, int count);

int tape_load_tape(struct device_data *dev, void * const kmi_handle, bool force);
int tape_unload_tape(bool keep_on_drive, struct device_data *dev);
int tape_prevent_medium_removal(struct device_data *dev);
void tape_allow_medium_removal(struct device_data *dev, bool force_release);
int tape_test_unit_ready(struct device_data *dev);

int tape_device_lock(struct device_data *dev);
int tape_device_unlock(struct device_data *dev);
int tape_start_fence(struct device_data *dev);
int tape_release_fence(struct device_data *dev);

int tape_get_capacity(struct device_data *dev, struct tc_remaining_cap *cap);
int tape_set_compression(struct device_data *dev, bool use_compression);
int tape_get_append_position(struct device_data *dev, tape_partition_t prt, tape_block_t *pos);
int tape_set_ip_append_position(struct device_data *dev, tape_partition_t prt, tape_block_t block);
int tape_set_append_position(struct device_data *dev, tape_partition_t prt, tape_block_t block);
int tape_get_params(struct device_data *dev, struct tc_drive_param *param);
int tape_get_max_blocksize(struct device_data *dev, unsigned int *size);
int tape_read_only(struct device_data *dev, tape_partition_t partition);
int tape_force_read_only(struct device_data *dev);

int tape_rewind(struct device_data *dev);
int tape_get_position(struct device_data *dev, struct tc_position *pos);
int tape_update_position(struct device_data *dev, struct tc_position *pos);

int tape_seek(struct device_data *dev, struct tc_position *pos);
int tape_seek_eod(struct device_data *dev, tape_partition_t partition);
int tape_seek_append_position(struct device_data *dev, tape_partition_t prt, bool unlock_write);
int tape_get_physical_block_position(struct device_data *dev, struct tc_position *pos);

int tape_spacefm(struct device_data *dev, int count);
ssize_t tape_read(struct device_data *dev, char *buf, size_t count, const bool unusual_size,
	void * const kmi_handle);

int tape_erase(struct device_data *dev, bool long_erase);
int tape_reset_capacity(struct device_data *dev);
int tape_format(struct device_data *dev, tape_partition_t index_part, int density_code);
int tape_unformat(struct device_data *dev);
ssize_t tape_write(struct device_data *dev, const char *buf, size_t count, bool ignore_less, bool ignore_nospc);
int tape_write_filemark(struct device_data *dev, uint8_t count, bool ignore_less, bool ignore_nospc, bool immed);

int tape_get_volume_change_reference(struct device_data *dev, uint64_t *vwj);
int tape_get_cart_coherency(struct device_data *dev, const tape_partition_t part,
							   struct tc_coherency *coh);
int tape_set_cart_coherency(struct device_data *dev, const tape_partition_t part,
							   struct tc_coherency *coh);
int tape_get_cart_volume_lock_status(struct device_data *dev, int *status);
int tape_set_cart_volume_lock_status(struct ltfs_volume *vol, int status);
int tape_get_media_pool_info(struct ltfs_volume *vol, char **media_name, char **add_info);
int tape_set_media_pool_info(struct ltfs_volume *vol, const char *new_val, int size, bool is_name);
int tape_check_eod_status(struct device_data *dev, const tape_partition_t part);
int tape_recover_eod_status(struct device_data *dev, void * const kmi_handle);

void tape_print_help_message(const char *progname, struct tape_ops *ops);
int tape_parse_opts(struct device_data *dev, void *opt_args);
int tape_parse_library_backend_opts(void *opts, void *opt_args);

int tape_inquiry(struct device_data *dev, struct tc_inq *inq);
int tape_inquiry_page(struct device_data *dev, unsigned char page, struct tc_inq_page *inq);

int tape_locate_next_index(struct device_data *dev);
int tape_locate_previous_index(struct device_data *dev);
int tape_locate_first_index(struct device_data *dev, tape_partition_t partition);
int tape_locate_last_index(struct device_data *dev, tape_partition_t partition);
int tape_get_cartridge_health(struct device_data *dev, cartridge_health_info *hlt);
int tape_get_tape_alert(struct device_data *dev, uint64_t *tape_alert);
int tape_clear_tape_alert(struct device_data *dev, uint64_t tape_alert);
int tape_get_vendorunique_xattr(struct device_data *dev, const char *name, char **buf);
int tape_set_vendorunique_xattr(struct device_data *dev, const char *name, const char *value, size_t size);

int tape_set_pews(struct device_data *dev, bool set_value);
int tape_get_pews(struct device_data *dev, uint16_t *pews);
int tape_enable_append_only_mode(struct device_data *dev, bool enable);
int tape_get_append_only_mode_setting(struct device_data *dev, bool *enabled);

int tape_is_cartridge_loadable(struct device_data *dev);
int tape_logsense(struct device_data *dev, const uint8_t page, const uint8_t subpage,
				  unsigned char *buf, const size_t size);
int tape_wait_device_ready(struct device_data *dev, void * const kmi_handle);
int tape_set_key(struct device_data *dev, const unsigned char *keyalias, const unsigned char *key);
int tape_clear_key(struct device_data *device, void * const kmi_handle);
int tape_get_keyalias(struct device_data *dev, unsigned char **keyalias);
int tape_takedump_drive(struct device_data *dev, bool nonforced_dump);
char* tape_get_media_encrypted(struct device_data *dev);
char* tape_get_drive_encryption_state(struct device_data *dev);
char* tape_get_drive_encryption_method(struct device_data *dev);
int tape_get_worm_status(struct device_data *dev, bool *is_worm);

void set_tape_attribute(struct ltfs_volume *vol, struct tape_attr *t_attr);
int tape_set_attribute_to_cm(struct device_data *dev, struct tape_attr *t_attr, int type);
int tape_format_attribute_to_cm(struct device_data *dev, struct tape_attr *t_attr);
int tape_get_attribute_from_cm(struct device_data *dev, struct tape_attr *t_attr, int type);
void tape_load_all_attribute_from_cm(struct device_data *dev, struct tape_attr *t_attr);
int update_tape_attribute (struct ltfs_volume *vol, const char *new_value, int type, int size);
int read_tape_attribute (struct ltfs_volume *vol, char **val, const char *name);
int tape_read_attr(struct device_data *dev, const tape_partition_t part,
				   unsigned char *buf, const size_t size);
int tape_is_mountable(struct device_data *dev, char *barcode,
					  unsigned char cart_type, unsigned char density);
int tape_is_reformattable(struct device_data *dev, unsigned char cart_type, unsigned char density);
int tape_set_profiler(struct device_data *dev, char *work_dir, bool enable);

static inline char* tape_get_serialnumber(struct device_data *dev)
{
	return dev->serial_number;
}

#ifdef __cplusplus
}
#endif

#endif /* __tape_h__ */
