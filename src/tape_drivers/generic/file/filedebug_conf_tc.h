/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2018 IBM Corp. All rights reserved.
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
** FILE NAME:       filedebug_conf_tc.h
**
** DESCRIPTION:     Header file for the XML parser for changer file backend
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/
#ifndef __filedebug_conf_tc_h
#define __filedebug_conf_tc_h

#include <libltfs/xml.h>

#define DEFAULT_CAPACITY_MB (3 * (GB / MB))

/* emulation of delays due to tape seeks */
#define DEFAULT_WRAPS            (40)       /* 40 wraps (76MB/wrap by default) */
#define DEFAULT_EOT_TO_BOT       (12)       /* time to seek from begin of tape to end of tape, in seconds */
#define DEFAULT_CHANGE_DIRECTION (2000000)  /* time to change tape direction, in microseconds */
#define DEFAULT_CHANGE_TRACK     (10000)    /* time to change track, in microseconds */
#define DEFAULT_THREADING        (12)       /* time to threading/unthreading in sec */

enum {
	DELAY_NONE = 0, /**< No delay emulation */
	DELAY_CALC,     /**< Only calculate delay time, no wait */
	DELAY_EMULATE   /**< Calculate delay time and wait */
};

struct filedebug_conf_tc
{
	bool          dummy_io;            /**< Dummy IO mode to evaliate upper layer performance */
	bool          emulate_readonly;    /**< True to emulate a cartridge in read-only mode */
	uint64_t      capacity_mb;         /**< Configure cartridge capacity */
	unsigned char cart_type;           /**< Cartridge type defined in tape_drivers.h */
	unsigned char density_code;        /**< Density code */
	/*
	 * TODO: Following improvement is needed for delay emulation
	 *
	 *  1. blocks per wrap can be calculated from capacity and number of wraps
	 *  2. Improve wrap change for write, read and locate
	 *  3. Improve locate within the drive buffer
	 *  4. Support back hitch emulation
	 */
	int       delay_mode;          /**< Emulated delay mode */
	uint64_t  wraps;               /**< Number of wraps */
	uint64_t  eot_to_bot_sec;      /**< Locate time to BOT to EOT for emulate delay */
	uint64_t  change_direction_us; /**< Time to change direction for emulate delay */
	uint64_t  change_track_us;     /**< Time to change track for emulate delay */
	uint64_t  threading_sec;       /**< Time to mechanical threading/unthreading */
};

struct filedebug_tc_cart_type {
	char *name;
	char type_code;
};

extern struct filedebug_tc_cart_type cart_type[];
extern int cart_type_size;

int filedebug_conf_tc_write_xml(char *filename, const struct filedebug_conf_tc *conf);
int filedebug_conf_tc_read_xml(char *filename, struct filedebug_conf_tc *conf);


#endif /* __filedebug_conf_tc_h */
