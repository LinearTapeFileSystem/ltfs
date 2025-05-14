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
** FILE NAME:       filedebug_conf_tc.c
**
** DESCRIPTION:     XML parser for tape file backend configuration
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#include "libltfs/ltfs.h"
#include "libltfs/ltfs_error.h"
#include "libltfs/ltfslogging.h"

#include "tape_drivers/tape_drivers.h"
#include "tape_drivers/vendor_compat.h"

#include "filedebug_conf_tc.h"

static int _filedebug_tc_write_schema(xmlTextWriterPtr writer, const struct filedebug_conf_tc *conf)
{
	int ret;

	/* Create XML document */
	ret = xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 30150E, ret);
		return -1;
	}

	xmlTextWriterSetIndent(writer, 1);
	xmlTextWriterSetIndentString(writer, BAD_CAST "    ");

	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "filedebug_cartridge_config"), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "dummy_io",
											  "%s", conf->dummy_io ? "true" : "false"), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "emulate_readonly",
											  "%s", conf->emulate_readonly ? "true" : "false"), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "capacity_mb",
											  "%"PRIu64, conf->capacity_mb), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "cart_type",
											  "%s", ibm_tape_assume_cart_name(conf->cart_type)), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "density_code",
											  "%x", conf->density_code), -1);

	switch(conf->delay_mode) {
		case DELAY_CALC:
			xml_mktag(xmlTextWriterWriteFormatElement(writer, BAD_CAST "delay_mode", "Calculate"), -1);
			break;
		case DELAY_EMULATE:
			xml_mktag(xmlTextWriterWriteFormatElement(writer, BAD_CAST "delay_mode", "Emulate"), -1);
			break;
		default:
			xml_mktag(xmlTextWriterWriteFormatElement(writer, BAD_CAST "delay_mode", "None"), -1);
			break;
	}

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "wraps",
											  "%"PRIu64, conf->wraps), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "eot_to_bot_sec",
											  "%"PRIu64, conf->eot_to_bot_sec), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "change_direction_us",
											  "%"PRIu64, conf->change_direction_us), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "change_track_us",
											  "%"PRIu64, conf->change_track_us), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "threading_sec",
											  "%"PRIu64, conf->threading_sec), -1);


	xml_mktag(xmlTextWriterEndElement(writer), -1);

	ret = xmlTextWriterEndDocument(writer);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 30151E, ret);
		return -1;
	}

	return ret;
}

int filedebug_conf_tc_write_xml(char *filename, const struct filedebug_conf_tc *conf)
{
	int ret;
	xmlTextWriterPtr writer;

	/* Create XML writer. */
	writer = xmlNewTextWriterFilename(filename, 0);
	if (! writer) {
		ltfsmsg(LTFS_ERR, 30152E);
		return -1;
	}

	ret = _filedebug_tc_write_schema(writer, conf);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 30153E);
	} else if (ret == 0) {
		ltfsmsg(LTFS_WARN, 30154W);
		ret = -1;
	}

	xmlFreeTextWriter(writer);

	return ret;
}

static int _filedebug_parser_init(xmlTextReaderPtr reader, const char *top_name)
{
	const char *name, *encoding;
	int type;

	if (xml_next_tag(reader, "", &name, &type) < 0)
		return -1;
	if (strcmp(name, top_name)) {
		ltfsmsg(LTFS_ERR, 30155E, name);
		return -1;
	}

	/* reject this XML file if it isn't UTF-8 */
	encoding = (const char *)xmlTextReaderConstEncoding(reader);
	if (! encoding || strcmp(encoding, "UTF-8")) {
		ltfsmsg(LTFS_ERR, 30156E, encoding);
		return -1;
	}

	return 0;
}

static int _filedebug_tc_parse_schema(xmlTextReaderPtr reader, struct filedebug_conf_tc *conf)
{
	int ret;
	unsigned long long value_ll;
	declare_parser_vars_noloop("filedebug_cartridge_config");

	/* start the parser: find top-level "index" tag, check version and encoding */
	ret = _filedebug_parser_init(reader, parent_tag);
	if (ret < 0)
		return ret;

	/* parse index file contents */
	while (true) {
		get_next_tag();

		if (! strcmp(name, "dummy_io")) {
			get_tag_text();
			if (xml_parse_bool(&conf->dummy_io, value) < 0)
				return -1;
			check_tag_end("dummy_io");

		} else if (! strcmp(name, "emulate_readonly")) {
			get_tag_text();
			if (xml_parse_bool(&conf->emulate_readonly, value) < 0)
				return -1;
			check_tag_end("emulate_readonly");

		} else if (! strcmp(name, "capacity_mb")) {
			get_tag_text();

			if (xml_parse_ull(&value_ll, value) < 0)
				return -1;

			if (value_ll > 0)
				conf->capacity_mb = value_ll;
			else
				conf->capacity_mb = DEFAULT_CAPACITY_MB;

			check_tag_end("capacity_mb");

		} else if (! strcmp(name, "cart_type")) {
			get_tag_text();
			conf->cart_type = ibm_tape_assume_cart_type(value);
			check_tag_end("cart_type");

		} else if (! strcmp(name, "density_code")) {
			get_tag_text();

			if (xml_parse_xll(&value_ll, value) < 0)
				return -1;
			conf->density_code = (char)value_ll;
			check_tag_end("density_code");

		} else if (! strcmp(name, "delay_mode")) {
			get_tag_text();
			if (!strcmp(value, "Calculate"))
				conf->delay_mode = DELAY_CALC;
			else if (!strcmp(value, "Emulate"))
				conf->delay_mode = DELAY_EMULATE;
			else
				conf->delay_mode = DELAY_NONE;

			check_tag_end("delay_mode");

		} else if (! strcmp(name, "wraps")) {
			get_tag_text();

			if (xml_parse_ull(&value_ll, value) < 0)
				return -1;

			if (value_ll > 0)
				conf->wraps = value_ll;
			else
				conf->wraps = DEFAULT_WRAPS;

		} else if (! strcmp(name, "eot_to_bot_sec")) {
			get_tag_text();

			if (xml_parse_ull(&value_ll, value) < 0)
				return -1;

			if (value_ll > 0)
				conf->eot_to_bot_sec = value_ll;
			else
				conf->eot_to_bot_sec = DEFAULT_EOT_TO_BOT;

		} else if (! strcmp(name, "change_direction_us")) {
			get_tag_text();

			if (xml_parse_ull(&value_ll, value) < 0)
				return -1;

			if (value_ll > 0)
				conf->change_direction_us = value_ll;
			else
				conf->change_direction_us = DEFAULT_CHANGE_DIRECTION;

		} else if (! strcmp(name, "change_track_us")) {
			get_tag_text();

			if (xml_parse_ull(&value_ll, value) < 0)
				return -1;

			if (value_ll > 0)
				conf->change_track_us = value_ll;
			else
				conf->change_track_us = DEFAULT_CHANGE_TRACK;

		} else if (! strcmp(name, "threading_sec")) {
			get_tag_text();

			if (xml_parse_ull(&value_ll, value) < 0)
				return -1;

			if (value_ll > 0)
				conf->threading_sec = value_ll;
			else
				conf->threading_sec = DEFAULT_CHANGE_TRACK;

		}
	}

	return 0;
}

int filedebug_conf_tc_read_xml(char *filename, struct filedebug_conf_tc *conf)
{
	int ret;
	xmlTextReaderPtr reader;
	xmlDocPtr doc;

	reader = xmlReaderForFile(filename, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (! reader) {
		ltfsmsg(LTFS_ERR, 30157E);
		return -1;
	}

	/* Workaround for old libxml2 version on OS X 10.5: the method used to preserve
	 * unknown tags modifies the behavior of xmlFreeTextReader so that an additional
	 * xmlDocFree call is required to free all memory. */
	doc = xmlTextReaderCurrentDoc(reader);
	ret = _filedebug_tc_parse_schema(reader, conf);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 30158E);
	}
	if (doc)
		xmlFreeDoc(doc);
	xmlFreeTextReader(reader);

	return ret;
}
