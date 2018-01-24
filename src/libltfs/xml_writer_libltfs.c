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
** FILE NAME:       xml_writer.c
**
** DESCRIPTION:     XML writer routines for Indexes and Labels.
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

#include "ltfs.h"
#include "xml_libltfs.h"
#include "fs.h"
#include "tape.h"
#include "pathname.h"
#include "arch/time_internal.h"

/* Structure to control EE's file offset cache and sync file list */
struct ltfsee_cache
{
	FILE*    fp;    /* File pointer */
	uint64_t count; /* File count to write */
};

/**************************************************************************************
 * Local Functions
 **************************************************************************************/

static int encode_entry_name(char **new_name, const char *name)
{
	int len;
	UChar32 c;

	/* Printable ASCII characters
	 * !\"#$%&`'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
	 *
	 * In this encoding, only `:` is encoded in this printable character set
	 */
	static char plain_chars[] = "!\"#$%&`'()*+,-./0123456789;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
	char *tmp_name;
	char buf_encode[3];
	int i=0, count=0, prev=0, j=0;

	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);

	len = strlen(name);

	tmp_name = malloc(len * 3 * sizeof(UChar));
	buf_encode[2] = '\0';

	while (i < len) {
		count = 0;
		prev = i;

		U8_NEXT(name, i, len, c);
		if (c < 0) {
			ltfsmsg(LTFS_ERR, 11235E);
			free(tmp_name);
			return -LTFS_ICU_ERROR;
		}

        if (strchr(plain_chars, name[prev])) {
			// encode is not needed.
			tmp_name[j] = name[prev];
			j++;
			continue;
		}

		while (count < i - prev) {
			sprintf(buf_encode, "%02X", name[prev+count] & 0xFF);
			tmp_name[j] = '%';
			tmp_name[j+1] = buf_encode[0];
			tmp_name[j+2] = buf_encode[1];
			j += 3;
			count++;
		}
	}

	tmp_name[j] = '\0';

	*new_name = strdup(tmp_name);
	free(tmp_name);

	return 0;
}

/**
 * Write nametype(in LTFS format spec) into an XML stream.
 * @param write output pointer
 * @param tag tag name to print
 * @param n pointer to ltfs_name structure
 * @return 0 on success or a negative value on error.
 */
static int _xml_write_nametype(xmlTextWriterPtr writer, const char *tag, struct ltfs_name *n)
{
	char *encoded_name = NULL;

	if (n->percent_encode) {
		encode_entry_name(&encoded_name, n->name);
		xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST tag), -1);
		xml_mktag(xmlTextWriterWriteAttribute(writer, BAD_CAST "percentencoded", BAD_CAST "true"), -1);
		xml_mktag(xmlTextWriterWriteString(writer, BAD_CAST encoded_name), -1);
		xml_mktag(xmlTextWriterEndElement(writer), -1);
		free(encoded_name);
	} else {
		xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST tag, BAD_CAST n->name), -1);
	}

	return 0;
}

/**
 * Write time info into an XML stream.
 * @param write output pointer
 * @param d dentry to get times from
 * @return 0 on success or a negative value on error.
 */
static int _xml_write_dentry_times(xmlTextWriterPtr writer, const struct dentry *d)
{
	int ret;
	char *mtime;

	ret = xml_format_time(d->creation_time, &mtime);
	if (!mtime)
		return -1;
	else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, 17225W, "creationtime", (unsigned long long)d->creation_time.tv_sec);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "creationtime", BAD_CAST mtime), -1);
	free(mtime);

	ret = xml_format_time(d->change_time, &mtime);
	if (!mtime)
		return -1;
	else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, 17225W, "changetime", (unsigned long long)d->change_time.tv_sec);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "changetime", BAD_CAST mtime), -1);
	free(mtime);

	ret = xml_format_time(d->modify_time, &mtime);
	if (!mtime)
		return -1;
	else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, 17225W, "modifytime", (unsigned long long)d->modify_time.tv_sec);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "modifytime", BAD_CAST mtime), -1);
	free(mtime);

	ret = xml_format_time(d->access_time, &mtime);
	if (!mtime)
		return -1;
	else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, 17225W, "accesstime", (unsigned long long)d->access_time.tv_sec);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "accesstime", BAD_CAST mtime), -1);
	free(mtime);

	ret = xml_format_time(d->backup_time, &mtime);
	if (!mtime)
		return -1;
	else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, 17225W, "backuptime", (unsigned long long)d->backup_time.tv_sec);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "backuptime", BAD_CAST mtime), -1);
	free(mtime);

	return 0;
}

/**
 * Write extended attributes from the given file or directory.
 * @param writer output pointer
 * @param file the dentry to take xattrs from
 * @return 0 on success or -1 on failure
 */
static int _xml_write_xattr(xmlTextWriterPtr writer, const struct dentry *file)
{
	int ret;
	struct xattr_info *xattr;

	if (! TAILQ_EMPTY(&file->xattrlist)) {
		xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "extendedattributes"), -1);
		TAILQ_FOREACH(xattr, &file->xattrlist, list) {
			xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "xattr"), -1);

			xml_mktag(_xml_write_nametype(writer, "key", &xattr->key), -1);

			if (xattr->value) {
				ret = pathname_validate_xattr_value(xattr->value, xattr->size);
				if (ret < 0) {
					ltfsmsg(LTFS_ERR, 17059E, ret);
					return -1;
				} else if (ret > 0) {
					xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "value"), -1);
					xml_mktag(
						xmlTextWriterWriteAttribute(writer, BAD_CAST "type", BAD_CAST "base64"),
						-1);
					xml_mktag(xmlTextWriterWriteBase64(writer, xattr->value, 0, xattr->size), -1);
					xml_mktag(xmlTextWriterEndElement(writer), -1);
				} else {
					xml_mktag(xmlTextWriterWriteFormatElement(
						writer, BAD_CAST "value", "%.*s", (int)xattr->size, xattr->value), -1);
				}
			} else { /* write empty value tag */
				xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "value"), -1);
				xml_mktag(xmlTextWriterEndElement(writer), -1);
			}
			xml_mktag(xmlTextWriterEndElement(writer), -1);
		}
		xml_mktag(xmlTextWriterEndElement(writer), -1);
	}

	return 0;
}

/**
 * Write file info to an XML stream.
 * @param writer output pointer
 * @param file the file to write
 * @return 0 on success or -1 on failure
 */
static int _xml_write_file(xmlTextWriterPtr writer, struct dentry *file, struct ltfsee_cache* offset_c, struct ltfsee_cache* sync_list)
{
	struct extent_info *extent;
	bool write_offset = false;
	size_t i;

	if (file->isdir) {
		ltfsmsg(LTFS_ERR, 17062E);
		return -1;
	}

	/* write standard attributes */
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "file"), -1);

	xml_mktag(_xml_write_nametype(writer, "name", &file->name), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "length", "%"PRIu64, file->size), -1);
	xml_mktag(xmlTextWriterWriteElement(
		writer, BAD_CAST "readonly", BAD_CAST (file->readonly ? "true" : "false")), -1);
	xml_mktag(_xml_write_dentry_times(writer, file), -1);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST UID_TAGNAME, "%"PRIu64, file->uid), -1);

	/* write extended attributes */
	xml_mktag(_xml_write_xattr(writer, file), -1);

	/* write extents */
    if (file->isslink) {
		xml_mktag(_xml_write_nametype(writer, "symlink", &file->target), -1);
    } else if (! TAILQ_EMPTY(&file->extentlist)) {
		xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "extentinfo"), -1);
		TAILQ_FOREACH(extent, &file->extentlist, list) {
			/* Write file offset cache */
			if (offset_c->fp && ! write_offset) {
				fprintf(offset_c->fp, "%s,%"PRIu64"\n", file->name.name, extent->start.block);
				write_offset = true;
				offset_c->count++;
			}
			xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "extent"), -1);
			xml_mktag(xmlTextWriterWriteFormatElement(
				writer, BAD_CAST "fileoffset", "%"PRIu64, extent->fileoffset), -1);
			xml_mktag(xmlTextWriterWriteFormatElement(
				writer, BAD_CAST "partition", "%c", extent->start.partition), -1);
			xml_mktag(xmlTextWriterWriteFormatElement(
				writer, BAD_CAST "startblock", "%"PRIu64, extent->start.block), -1);
			xml_mktag(xmlTextWriterWriteFormatElement(
				writer, BAD_CAST "byteoffset", "%"PRIu32, extent->byteoffset), -1);
			xml_mktag(xmlTextWriterWriteFormatElement(
				writer, BAD_CAST "bytecount", "%"PRIu64, extent->bytecount), -1);
			xml_mktag(xmlTextWriterEndElement(writer), -1);
		}
		xml_mktag(xmlTextWriterEndElement(writer), -1);
	} else {
		/* Write file offset cache */
		if (offset_c->fp) {
			fprintf(offset_c->fp, "%s,%"PRIu64"\n", file->name.name, (uint64_t)0);
			offset_c->count++;
		}
	}

	/* Save unrecognized tags */
	if (file->tag_count > 0) {
		for (i=0; i<file->tag_count; ++i) {
			if (xmlTextWriterWriteRaw(writer, file->preserved_tags[i]) < 0) {
				ltfsmsg(LTFS_ERR, 17092E, __FUNCTION__);
				return -1;
			}
		}
	}

	xml_mktag(xmlTextWriterEndElement(writer), -1);

	/* Write dirty file list */
	if (sync_list->fp && file->dirty) {
		fprintf(sync_list->fp, "%s,%"PRIu64"\n", file->name.name, file->size);
		file->dirty = false;
		sync_list->count++;
	}

	return 0;
}

/**
 * Write XML tags representing the current directory tree to the given destination.
 * @param writer output pointer
 * @param dir directory to process
 * @param idx pointer to ltfs index structure
 * @param offset_c file pointer to write offest cache
 * @param sync_list file pointer to write sync file list
 * @return 0 on success or negative on failure
 */
static int _xml_write_dirtree(xmlTextWriterPtr writer, struct dentry *dir,
					   const struct ltfs_index *idx, struct ltfsee_cache* offset_c, struct ltfsee_cache* sync_list)
{
	size_t i;
	char *offset_name, *sync_name;
	struct ltfsee_cache *offset = offset_c, *sync = sync_list;
	struct name_list *list_ptr, *list_tmp;
	int ret;

	if (!dir)
		return 0; /* nothing to do */

	/* write standard attributes */
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "directory"), -1);
	if (dir == idx->root) {
		if (idx->volume_name.name) {
			xml_mktag(_xml_write_nametype(writer, "name", (struct ltfs_name*)(&idx->volume_name)), -1);
		} else {
			xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "name"), -1);
			xml_mktag(xmlTextWriterEndElement(writer), -1);
		}
	} else
		xml_mktag(_xml_write_nametype(writer, "name", &dir->name), -1);

	xml_mktag(xmlTextWriterWriteElement(
		writer, BAD_CAST "readonly", BAD_CAST (dir->readonly ? "true" : "false")), -1);
	xml_mktag(_xml_write_dentry_times(writer, dir), -1);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST UID_TAGNAME, "%"PRIu64, dir->uid), -1);

	/* write extended attributes */
	xml_mktag(_xml_write_xattr(writer, dir), -1);

	/* write children */
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "contents"), -1);
	/* Sort dentries by UID before generating xml */
	HASH_SORT(dir->child_list, fs_hash_sort_by_uid);

	HASH_ITER(hh, dir->child_list, list_ptr, list_tmp) {
		if (list_ptr->d->isdir) {

			if (list_ptr->d->vol->index_cache_path && !strcmp(list_ptr->d->name.name, ".LTFSEE_DATA")) {
				ret = asprintf(&offset_name, "%s.%s", list_ptr->d->vol->index_cache_path, "offsetcache");
				if (ret > 0) {
					offset->fp = fopen(offset_name, "w");
					free(offset_name);
					if (!offset->fp)
						ltfsmsg(LTFS_WARN, 17248W, "offset cache", list_ptr->d->vol->index_cache_path);
				} else
					ltfsmsg(LTFS_WARN, 17247W, "offset cache", list_ptr->d->vol->index_cache_path);

				ret = asprintf(&sync_name, "%s.%s", list_ptr->d->vol->index_cache_path, "synclist");
				if (ret > 0) {
					sync->fp = fopen(sync_name, "w");
					free(sync_name);
					if (!sync->fp)
						ltfsmsg(LTFS_WARN, 17248W, "sync list", list_ptr->d->vol->index_cache_path);
				} else
					ltfsmsg(LTFS_WARN, 17247W, "sync list", list_ptr->d->vol->index_cache_path);
			}

			xml_mktag(_xml_write_dirtree(writer, list_ptr->d, idx, offset, sync), -1);

			if (offset->fp) {
				fflush(offset->fp);
				fsync(fileno(offset->fp));
				fclose(offset->fp);
				offset->fp = NULL;
			}
			if (sync->fp) {
				fflush(sync->fp);
				fsync(fileno(sync->fp));
				fclose(sync->fp);
				sync->fp = NULL;
			}

		} else
			xml_mktag(_xml_write_file(writer, list_ptr->d, offset_c, sync_list), -1);
	}

	xml_mktag(xmlTextWriterEndElement(writer), -1);

	/* Save unrecognized tags */
	if (dir->tag_count > 0) {
		for (i=0; i<dir->tag_count; ++i) {
			if (xmlTextWriterWriteRaw(writer, dir->preserved_tags[i]) < 0) {
				ltfsmsg(LTFS_ERR, 17092E, __FUNCTION__);
				return -1;
			}
		}
	}

	xml_mktag(xmlTextWriterEndElement(writer), -1);

	return 0;
}

/**
 * Generate an XML schema, sending it to a user-provided output (memory or file).
 * Note: this function does very little input validation; any user-provided information
 * must be verified by the caller.
 * @param writer the XML writer to send output to
 * @param priv LTFS data
 * @param pos position on tape where the schema will be written
 * @return 0 on success, negative on failure
 */
static int _xml_write_schema(xmlTextWriterPtr writer, const char *creator,
	const struct ltfs_index *idx)
{
	int ret;
	size_t i;
	char *update_time;
	struct ltfs_name *name_criteria;
	char *offset_name = NULL, *sync_name = NULL;
	struct ltfsee_cache offset = {NULL, 0};  /* Cache structure for file offset cache */
	struct ltfsee_cache list = {NULL, 0};    /* Cache structure for sync list */

	ret = xml_format_time(idx->mod_time, &update_time);
	if (!update_time)
		return -1;
	else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, 17224W, "modifytime", (unsigned long long)idx->mod_time.tv_sec);

	ret = xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 17057E, ret);
		return -1;
	}

	xmlTextWriterSetIndent(writer, 1);
	/* Define INDENT_INDEXES to write Indexes to tape with full indentation.
	 * This is normally a waste of space, but it may be useful for debugging. */
#ifdef INDENT_INDEXES
	xmlTextWriterSetIndentString(writer, BAD_CAST "    ");
#else
	xmlTextWriterSetIndentString(writer, BAD_CAST "");
#endif

	/* write index properties */
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "ltfsindex"), -1);
	xml_mktag(xmlTextWriterWriteAttribute(writer, BAD_CAST "version",
		BAD_CAST LTFS_INDEX_VERSION_STR), -1);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "creator", BAD_CAST creator), -1);
	if (idx->commit_message && strlen(idx->commit_message)) {
		xml_mktag(xmlTextWriterWriteFormatElement(writer, BAD_CAST "comment",
			"%s", BAD_CAST (idx->commit_message)), -1);
	}
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "volumeuuid", BAD_CAST idx->vol_uuid), -1);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "generationnumber", "%u", idx->generation), -1);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "updatetime", BAD_CAST update_time), -1);
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "location"), -1);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "partition", "%c", idx->selfptr.partition), -1);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "startblock", "%"PRIu64, idx->selfptr.block), -1);
	xml_mktag(xmlTextWriterEndElement(writer), -1);
	if (idx->backptr.block) {
		xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "previousgenerationlocation"), -1);
		xml_mktag(xmlTextWriterWriteFormatElement(
			writer, BAD_CAST "partition", "%c", idx->backptr.partition), -1);
		xml_mktag(xmlTextWriterWriteFormatElement(
			writer, BAD_CAST "startblock", "%"PRIu64, idx->backptr.block), -1);
		xml_mktag(xmlTextWriterEndElement(writer), -1);
	}
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "allowpolicyupdate",
		BAD_CAST (idx->criteria_allow_update ? "true" : "false")), -1);
	if (idx->original_criteria.have_criteria) {
		xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "dataplacementpolicy"), -1);
		xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "indexpartitioncriteria"), -1);
		xml_mktag(xmlTextWriterWriteFormatElement(writer, BAD_CAST "size", "%"PRIu64,
			idx->original_criteria.max_filesize_criteria), -1);
		if (idx->original_criteria.glob_patterns) {
			name_criteria = idx->original_criteria.glob_patterns;
			while (name_criteria && name_criteria->name) {
				xml_mktag(_xml_write_nametype(writer, "name", name_criteria), -1);
				++name_criteria;
			}
		}
		xml_mktag(xmlTextWriterEndElement(writer), -1);
		xml_mktag(xmlTextWriterEndElement(writer), -1);
	}
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST NEXTUID_TAGNAME, "%"PRIu64, idx->uid_number), -1);

	{
		char *value = NULL;

		switch (idx->vollock) {
			case VOLUME_LOCKED:
				asprintf(&value, "locked");
				break;
			case VOLUME_PERM_LOCKED:
				asprintf(&value, "permlocked");
				break;
			default:
				asprintf(&value, "unlocked");
				break;
		}

		if (value)
			xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "volumelockstate", BAD_CAST value), -1);

		free(value);
	}

	/* unlink offset cache and sync list before rewriting index */
	ret = asprintf(&offset_name, "%s.%s", idx->root->vol->index_cache_path, "offsetcache");
	if (ret > 0) {
		unlink(offset_name);
		free(offset_name);
	}

	ret = asprintf(&sync_name, "%s.%s", idx->root->vol->index_cache_path, "synclist");
	if (ret > 0) {
		unlink(sync_name);
		free(sync_name);
	}

	xml_mktag(_xml_write_dirtree(writer, idx->root, idx, &offset, &list), -1);
	if (offset.count)
		ltfsmsg(LTFS_INFO, 17249I, (unsigned long long)offset.count);
	if (list.count)
		ltfsmsg(LTFS_INFO, 17250I, (unsigned long long)list.count);

	/* Save unrecognized tags */
	if (idx->tag_count > 0) {
		for (i=0; i<idx->tag_count; ++i) {
			if (xmlTextWriterWriteRaw(writer, idx->preserved_tags[i]) < 0) {
				ltfsmsg(LTFS_ERR, 17092E, __FUNCTION__);
				return -1;
			}
		}
	}

	xml_mktag(xmlTextWriterEndElement(writer), -1);
	ret = xmlTextWriterEndDocument(writer);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 17058E, ret);
		return -1;
	}

	free(update_time);
	return 0;
}

/**************************************************************************************
 * Global Functions
 **************************************************************************************/

/**
 * Generate an XML tape label.
 * @param partition the partition number to which the label will be written
 * @param label data structure containing format parameters
 * @return buffer containing the label, which the caller should free using xmlBufferFree
 */
xmlBufferPtr xml_make_label(const char *creator, tape_partition_t partition,
							const struct ltfs_label *label)
{
	int ret;
	char *fmt_time;
	xmlBufferPtr buf = NULL;
	xmlTextWriterPtr writer;

	CHECK_ARG_NULL(creator, NULL);
	CHECK_ARG_NULL(label, NULL);

	buf = xmlBufferCreate();
	if (!buf) {
		ltfsmsg(LTFS_ERR, 17047E);
		return NULL;
	}

	writer = xmlNewTextWriterMemory(buf, 0);
	if (!writer) {
		ltfsmsg(LTFS_ERR, 17043E);
		return NULL;
	}

	ret = xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 17044E, ret);
		return NULL;
	}

	xmlTextWriterSetIndent(writer, 1);
	xmlTextWriterSetIndentString(writer, BAD_CAST "    ");

	/* write tags */
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "ltfslabel"), NULL);
	xml_mktag(xmlTextWriterWriteAttribute(writer, BAD_CAST "version",
		BAD_CAST LTFS_LABEL_VERSION_STR), NULL);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "creator", BAD_CAST creator), NULL);

	ret = xml_format_time(label->format_time, &fmt_time);
	if (!fmt_time) {
		ltfsmsg(LTFS_ERR, 17045E);
		return NULL;
	} else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, 17223W, "formattime", (unsigned long long)label->format_time.tv_sec);

	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "formattime", BAD_CAST fmt_time), NULL);
	free(fmt_time);
	xml_mktag(xmlTextWriterWriteElement(
		writer, BAD_CAST "volumeuuid", BAD_CAST label->vol_uuid), NULL);
    xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "location"), NULL);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "partition", "%c", label->part_num2id[partition]), NULL);
	xml_mktag(xmlTextWriterEndElement(writer), NULL);
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "partitions"), NULL);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "index", "%c", label->partid_ip), NULL);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "data", "%c", label->partid_dp), NULL);
	xml_mktag(xmlTextWriterEndElement(writer), NULL);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "blocksize", "%ld", label->blocksize), NULL);
	xml_mktag(xmlTextWriterWriteElement(
		writer, BAD_CAST "compression", BAD_CAST (label->enable_compression ? "true" : "false")),
		NULL);
	xml_mktag(xmlTextWriterEndElement(writer), NULL);

	ret = xmlTextWriterEndDocument(writer);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 17046E, ret);
		return NULL;
	}

	xmlFreeTextWriter(writer);
	return buf;
}

/**
 * Create an XML schema in memory.
 * @param priv LTFS data
 * @return buffer containing the index, which the caller should free using xmlBufferFree
 */
xmlBufferPtr xml_make_schema(const char *creator, const struct ltfs_index *idx)
{
	xmlBufferPtr buf = NULL;
	xmlTextWriterPtr writer;

	CHECK_ARG_NULL(creator, NULL);
	CHECK_ARG_NULL(idx, NULL);

	buf = xmlBufferCreate();
	if (!buf) {
		ltfsmsg(LTFS_ERR, 17048E);
		return NULL;
	}

	writer = xmlNewTextWriterMemory(buf, 0);
	if (!writer) {
		ltfsmsg(LTFS_ERR, 17049E);
		return NULL;
	}

	if (_xml_write_schema(writer, creator, idx) < 0) {
		ltfsmsg(LTFS_ERR, 17050E);
		xmlBufferFree(buf);
		buf = NULL;
	}
	xmlFreeTextWriter(writer);
	return buf;
}

/**
 * Generate an XML schema file based on the priv->root directory tree.
 * @param filename output XML file
 * @param priv ltfs private data
 * @return 0 on success or a negative value on error.
 */
int xml_schema_to_file(const char *filename, const char *creator
					   , const char *reason, const struct ltfs_index *idx)
{
	xmlTextWriterPtr writer;
	int ret;
	char *alt_creator = NULL;

	CHECK_ARG_NULL(creator, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(idx, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(filename, -LTFS_NULL_ARG);

	writer = xmlNewTextWriterFilename(filename, 0);
	if (! writer) {
		ltfsmsg(LTFS_ERR, 17051E, filename);
		return -1;
	}

	if (reason)
		asprintf(&alt_creator, "%s - %s", creator , reason);
	else
		alt_creator = strdup(creator);

	if (alt_creator) {
		ret = _xml_write_schema(writer, alt_creator, idx);
		if (ret < 0)
			ltfsmsg(LTFS_ERR, 17052E, ret, filename);
		xmlFreeTextWriter(writer);
		free(alt_creator);
	} else {
		ltfsmsg(LTFS_ERR, 10001E, "xml_schema_to_file: alt creator string");
		return -1;
	}

	return ret;
}

/**
 * Generate an XML Index based on the vol->index->root directory tree.
 * The generated data are written directly to the tape with the appropriate blocksize.
 * @param vol LTFS volume.
 * @return 0 on success or a negative value on error.
 */
int xml_schema_to_tape(char *reason, struct ltfs_volume *vol)
{
	int ret;
	xmlOutputBufferPtr write_buf;
	xmlTextWriterPtr writer;
	struct xml_output_tape *out_ctx;
	char *creator = NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(reason, -LTFS_NULL_ARG);

	/* Create output callback context data structure. */
	out_ctx = calloc(1, sizeof(struct xml_output_tape));
	if (! out_ctx) {
		ltfsmsg(LTFS_ERR, 10001E, "xml_schema_to_tape: output context");
		return -LTFS_NO_MEMORY;
	}
	out_ctx->buf = malloc(vol->label->blocksize + LTFS_CRC_SIZE);
	if (! out_ctx->buf) {
		ltfsmsg(LTFS_ERR, 10001E, "xml_schema_to_tape: output buffer");
		free(out_ctx);
		return -LTFS_NO_MEMORY;
	}

	out_ctx->fd = -1;

	if (vol->index_cache_path)
		out_ctx->fd = xml_acquire_file_lock(vol->index_cache_path, true);

	out_ctx->buf_size = vol->label->blocksize;
	out_ctx->buf_used = 0;
	out_ctx->device = vol->device;

	/* Create output buffer pointer. */
	write_buf = xmlOutputBufferCreateIO(xml_output_tape_write_callback,
										xml_output_tape_close_callback,
										out_ctx, NULL);
	if (! write_buf) {
		ltfsmsg(LTFS_ERR, 17053E);
		if (out_ctx->fd >= 0)
			xml_release_file_lock(out_ctx->fd);
		free(out_ctx->buf);
		free(out_ctx);
		return -1;
	}

	/* Create XML writer. */
	writer = xmlNewTextWriter(write_buf);
	if (! writer) {
		ltfsmsg(LTFS_ERR, 17054E);
		if (out_ctx->fd >= 0)
			xml_release_file_lock(out_ctx->fd);
		xmlOutputBufferClose(write_buf);
		return -1;
	}

	/* Generate the Index. */
	asprintf(&creator, "%s - %s", vol->creator, reason);
	if (creator) {
		ret = _xml_write_schema(writer, creator, vol->index);
		if (ret < 0)
			ltfsmsg(LTFS_ERR, 17055E, ret);

		xmlFreeTextWriter(writer);
		free(creator);

		/* Update the creator string */
		if (! vol->index->creator || strcmp(vol->creator, vol->index->creator)) {
			if (vol->index->creator)
				free(vol->index->creator);
			vol->index->creator = strdup(vol->creator);
			if (! vol->index->creator) {
				ltfsmsg(LTFS_ERR, 10001E, "xml_schema_to_tape: new creator string");
				ret = -1;
			}
		}
	} else {
		ltfsmsg(LTFS_ERR, 10001E, "xml_schema_to_tape: creator string");
		xmlFreeTextWriter(writer);
		ret = -1;
	}

	return ret;
}
