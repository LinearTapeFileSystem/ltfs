/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2025 IBM Corp. All rights reserved.
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
** FILE NAME:       xml.h
**
** DESCRIPTION:     Prototypes for XML read/write functions.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/

#ifndef __xml_libltfs_h
#define __xml_libltfs_h

#include <libxml/tree.h>
#include "ltfs.h"
#include "xml.h"

/*
 * Define dummy XML_PARSE_HUGE for old libxml2
 * Existance of XML_PARSE_HUGE is tested in configure script
 * and HAVE_XML_PARSE_HUGE shall be defined in config.h.
 */
#if !HAVE_XML_PARSE_HUGE
#define XML_PARSE_HUGE 0
#endif

/* A couple of tag names */
#define BACKUPTIME_TAGNAME "backuptime"
#define NEXTUID_TAGNAME    "highestfileuid"
#define UID_TAGNAME        "fileuid"
#define FILEOFFSET_TAGNAME "fileoffset"

/* Functions for writing XML files. See xml_writer_libltfs.c */
xmlBufferPtr xml_make_label(const char *creator, tape_partition_t partition,
							const struct ltfs_label *label);
xmlBufferPtr xml_make_schema(const char *creator, const struct ltfs_index *idx);
int xml_schema_to_file(const char *filename, const char *creator,
					   const char *reason, const struct ltfs_index *idx);
int xml_schema_to_tape(char *reason, struct ltfs_volume *vol);

/* Functions for reading XML files. See xml_reader_libltfs.c */
int xml_label_from_file(const char *filename, struct ltfs_label *label);
int xml_label_from_mem(const char *buf, int buf_size, struct ltfs_label *label);
int xml_schema_from_file(const char *filename, struct ltfs_index *idx, struct ltfs_volume *vol);
int xml_schema_from_tape(uint64_t eod_pos, struct ltfs_volume *vol);
int xml_extent_symlink_info_from_file(const char *filename, struct dentry *d);

#endif /* __xml_libltfs_h */
