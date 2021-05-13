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
**                  Atsushi Abe
**                  IBM Tokyo Lab., Japan
**               	piste@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __xml_h
#define __xml_h

#include <libxml/xmlstring.h>
#include <libxml/xmlwriter.h>
#include <libxml/xmlreader.h>
#include <libxml/tree.h>
#include "ltfs.h"

/*
 *  Definitions for utility functions for XML writer (xml_writer.c)
 */

/* Time format in the XML file. be sure to change this if the schema changes */
#define XML_TIME_FORMAT "0000-00-00T00:00:00.000000000Z"

/**
 * This structure is used to store state data when writing XML directly to tape using the libxml2
 * I/O callback method.
 */
struct xml_output_tape {
	struct device_data *device;  /**< Tape device data to out */
	int                err_code; /**< Error code from tape backend */
	int                fd;       /**< File Descriptor for index cache if fd > 0 */
	int                errno_fd; /**< errno from the index cache */
	char               *buf;     /**< 1-block output buffer. */
	uint32_t           buf_size; /**< Output buffer size. */
	uint32_t           buf_used; /**< Current output buffer usage. */
};
int xml_output_tape_write_callback(void *context, const char *buffer, int len);
int xml_output_tape_close_callback(void *context);

int xml_acquire_file_lock(const char *file, int *fd, int *bk_fd, bool is_write);
int xml_release_file_lock(const char *file, int fd, int bk_fd, bool revert);

struct xml_output_fd {
	int      fd;              /**< file descriptor to out */
};
int xml_output_fd_write_callback(void *context, const char *buffer, int len);
int xml_output_fd_close_callback(void *context);

/* value formatter */
int xml_format_time(struct ltfs_timespec t, char** out);

/*
 *  Definitions for utility functions for XML reader (xml_reader.c)
 */

/* provide error handling when writing XML tags */
#define xml_mktag(val, retval) \
	do { \
		if ((val) < 0) { \
			ltfsmsg(LTFS_ERR, 17042E, __FUNCTION__); \
			return (retval); \
		} \
	} while (0)

/* standard parser variables */
#define declare_parser(toptag) \
	const char *name, *parent_tag = (toptag); \
	int i, type, empty;

#define declare_parser_vars_noloop(toptag) \
	const char *name, *value, *parent_tag = (toptag); \
	int type, empty;

#define declare_parser_vars(toptag) \
	const char *name, *value, *parent_tag = (toptag); \
	int i, type, empty;

#define declare_parser_vars_symlinknode(toptag) \
	const char *name, *parent_tag = (toptag); \
	int type;

#define declare_parser_vars_symlink(toptag) \
	const char *name, *parent_tag = (toptag); \
	int type;

/* parser variables for extent */
#define declare_extent_parser_vars(toptag) \
	const char *name, *parent_tag = (toptag); \
	int type;

/* generate required/optional tag tracking arrays for the parser */
#define declare_tracking_arrays(num_req, num_opt) \
	const int ntags_req = (num_req), ntags_opt = (num_opt); \
	bool have_required_tags[ntags_req], have_optional_tags[ntags_opt]; \
	if (ntags_req > 0) memset(have_required_tags, 0, sizeof(have_required_tags)); \
	if (ntags_opt > 0) memset(have_optional_tags, 0, sizeof(have_optional_tags));

/* grab the next tag inside the given tag. It breaks if the end of the given tag is detected.
 * NOTE: in order for break to work correctly, this macro is not wrapped in a do { ... } while (0)
 * loop. So be careful when using it! */
#define get_next_tag() \
	if (xml_next_tag(reader, parent_tag, &name, &type) < 0) \
		return -1; \
	if (type == XML_ELEMENT_DECL) \
		break

/* check standard tracking array for required tags which are not present. */
#define check_required_tags() do { \
	for (i=0; i<ntags_req; ++i) { \
		if (! have_required_tags[i]) { \
			ltfsmsg(LTFS_ERR, 17000E, parent_tag); \
			return -1; \
		} \
	} \
} while (0)

/* used for detecting missing and duplicated required tags during parsing */
#define check_required_tag(i) do { \
	if (have_required_tags[i]) { \
		ltfsmsg(LTFS_ERR, 17001E, name); \
		return -1; \
	} \
	have_required_tags[i] = true; \
} while (0)

/* used for detecting missing and duplicated optional tags during parsing */
#define check_optional_tag(i) do { \
	if (have_optional_tags[i]) { \
		ltfsmsg(LTFS_ERR, 17002E, name); \
		return -1; \
	} \
	have_optional_tags[i] = true; \
} while (0)

/* assert that a tag is not empty. this only excludes true empty elements like <element/>. */
#define assert_not_empty() do { \
	empty = xmlTextReaderIsEmptyElement(reader); \
	if (empty < 0) { \
		ltfsmsg(LTFS_ERR, 17003E); \
		return -1; \
	} else if (empty > 0) { \
		ltfsmsg(LTFS_ERR, 17004E, name); \
		return -1; \
	} \
} while (0)

/* check whether a tag is empty. */
#define check_empty() do { \
	empty = xmlTextReaderIsEmptyElement(reader); \
	if (empty < 0) { \
		ltfsmsg(LTFS_ERR, 17003E); \
		return -1; \
	} \
} while (0)

/* consume the end of a tag, failing if there's extra content */
#define check_tag_end(tagname) do { \
	if (xml_next_tag(reader, (tagname), &name, &type) < 0 || type != XML_ELEMENT_DECL) { \
		ltfsmsg(LTFS_ERR, 17005E, (tagname)); \
		return -1; \
	} \
} while (0)

/* get text from a tag, failing if the tag is empty (like <element/>) or contains
 * the empty string (like <element></element>). if successful, it
 * reads the text into "value". It does not consume the remainder of the tag. */
#define get_tag_text() do { \
	assert_not_empty(); \
	if (xml_scan_text(reader, &value) < 0) \
		return -1; \
	if (strlen(value) == 0) { \
		ltfsmsg(LTFS_ERR, 17004E, name); \
		return -1; \
	} \
} while (0)

#define get_tag_text_allow_zero_length() do { \
	assert_not_empty(); \
	if (xml_scan_text(reader, &value) < 0) \
		return -1; \
} while (0)

/* get text from a tag. if successful, it reads the text into "value".
 * It does not consume the remainder of the tag. */
#define get_tag_text_allow_empty() do { \
	if (xml_scan_text(reader, &value) < 0) \
		return -1; \
} while (0)

/* issue a warning that the tag is unrecognized and will be ignored. */
#define ignore_unrecognized_tag() do { \
	ltfsmsg(LTFS_WARN, 17006W, name, parent_tag); \
	if (xml_skip_tag(reader) < 0) \
		return -1; \
} while (0)

/* store a tag in a list of unrecognized tags, to be written back to tape later */
#define preserve_unrecognized_tag(structure) do { \
	if (xml_save_tag(reader, &(structure)->tag_count, &(structure)->preserved_tags) < 0) \
		return -1; \
	if (xml_skip_tag(reader) < 0) \
		return -1; \
} while (0)

/**
 * This structure is used to store state data when reading XML directly from tape using
 * the libxml2 I/O callback method.
 */
struct xml_input_tape {
	struct ltfs_volume *vol;            /**< LTFS volume to read */
	int                err_code;        /**< Error code from tape backend */
	uint64_t           current_pos;     /**< Current block position of the drive. */
	uint64_t           eod_pos;         /**< EOD position of the current partition. */
	bool               saw_small_block; /**< Have we seen a small block yet? */
	bool               saw_file_mark;   /**< If we saw a small blilock, was it a file mark? */
	char               *buf;            /**< 1-block input buffer. */
	uint32_t           buf_size;        /**< Input buffer size. */
	uint32_t           buf_start;       /**< Offset of first valid byte in input buffer. */
	uint32_t           buf_used;        /**< Current input buffer usage. */
};
int xml_input_tape_read_callback(void *context, char *buffer, int len);
int xml_input_tape_close_callback(void *context);

/* Generic tag parsers */
int xml_scan_text(xmlTextReaderPtr reader, const char **value);
int xml_next_tag(xmlTextReaderPtr reader, const char *containing_name,
	const char **name, int *type);
int xml_skip_tag(xmlTextReaderPtr reader);
int xml_save_tag(xmlTextReaderPtr reader, size_t *tag_count, unsigned char ***tag_list);
int xml_reader_read(xmlTextReaderPtr reader);

/* Value parsers */
int xml_parse_uuid(char *out_val, const char *val);
int xml_parse_filename(char **out_val, const char *value);
int xml_parse_target(char **out_val, const char *value);
int xml_parse_ll(long long *out_val, const char *val);
int xml_parse_ull(unsigned long long *out_val, const char *val);
int xml_parse_xll(unsigned long long *out_val, const char *val);
int xml_parse_bool(bool *out_val, const char *value);
int xml_parse_time(bool msg, const char *fmt_time, struct ltfs_timespec *rawtime);

/* Call these to initialize or tear down the XML library. See xml_common.c */
void xml_init();
void xml_finish();

#endif /* __xml_h */
