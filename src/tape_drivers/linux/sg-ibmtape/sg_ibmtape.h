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
** FILE NAME:       tape_drivers/osx/iokit/iokit_ibmtape.h
**
** DESCRIPTION:     Definitions of iokit ibmtape backend
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __sg_ibmtape_h
#define __sg_ibmtape_h

struct sg_ibmtape_data {
	struct sg_tape       dev;                  /**< device structure of sg */
	bool                 loaded;               /**< Is cartridge loaded? */
	bool                 loadfailed;           /**< Is load/unload failed? */
	bool                 is_reserved;          /**< Is reserved? */
	bool                 is_tape_locked;       /**< Is medium removal prevented? */
	bool                 is_reconnecting;      /**< Reconnecting, suppress nested reconnect */
	char                 drive_serial[255];    /**< serial number of device */
	long                 fetch_sec_acq_loss_w; /**< Sec to fetch Active CQs loss write */
	bool                 dirty_acq_loss_w;     /**< Is Active CQs loss write dirty */
	float                acq_loss_w;           /**< Active CQs loss write */
	uint64_t             tape_alert;           /**< Latched tape alert flag */
	unsigned char        dki[12];              /**< key-alias */
	bool                 use_sili;             /**< Default true, false for USB drives */
	int                  drive_type;           /**< drive type defined by ltfs */
	bool                 clear_by_pc;          /**< clear pseudo write perm by partition change */
	uint64_t             force_writeperm;      /**< pseudo write perm threshold */
	uint64_t             force_readperm;       /**< pseudo read perm threshold */
	uint64_t             write_counter;        /**< write call counter for pseudo write perm */
	uint64_t             read_counter;         /**< read call counter for pseudo write perm */
	int                  force_errortype;      /**< 0 is R/W Perm, otherwise no sense */
	char                 *devname;             /**< Identifier for drive on host */
	unsigned char        key[KEYLEN];          /**< Key for persistent reserve */
	bool                 is_worm;              /**< Is worm cartridge loaded? */
	unsigned char        cart_type;            /**< Cartridge type in CM */
	unsigned char        density_code;         /**< Density code */
	crc_enc              f_crc_enc;            /**< Pointer to CRC encode function */
	crc_check            f_crc_check;          /**< Pointer to CRC encode function */
	struct timeout_tape  *timeouts;            /**< Timeout table */
	FILE*                profiler;             /**< The file pointer for profiler */
};

struct sg_ibmtape_global_data {
	char     *str_crc_checking; /**< option string for crc_checking */
	unsigned crc_checking;      /**< Is crc checking enabled? */
	unsigned strict_drive;      /**< Is bar code length checked strictly? */
	unsigned disable_auto_dump; /**< Is auto dump disabled? */
	unsigned capacity_offset;   /**< Dummy capacity offset to create full tape earlier */
};

#endif // __sg_ibmtape_h
