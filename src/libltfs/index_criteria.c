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
** FILE NAME:       index_criteria.c
**
** DESCRIPTION:     Implements routines that deal with the index partition criteria.
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/

#ifdef __APPLE_MAKEFILE__
#include <ICU/unicode/ubrk.h>
#include <ICU/unicode/ustring.h>
#else
#include <unicode/ubrk.h>
#include <unicode/ustring.h>
#endif


#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#endif

#include "ltfs.h"
#include "libltfs/ltfslogging.h"
#include "index_criteria.h"
#include "pathname.h"

typedef struct ustack {
	int32_t cr_bnd[3];
	int32_t fi_bnd[3];
	struct ustack *next;
} filename_ustack_t;

/* Forward declaration of private functions */
int _prepare_glob_cache(struct index_criteria *ic);
int _matches_name_criteria_caseless(const UChar *criteria, int32_t cr_len,
	const UChar *filename, int32_t fi_len);
void _next_char(const UChar *str, UBreakIterator *it, int32_t *pos);
int _char_compare(const UChar *str1, int32_t *pos1, const UChar *str2, int32_t *pos2);
void _destroy_ustack(filename_ustack_t **stack);
int _push_ustack(filename_ustack_t **stack, filename_ustack_t *element);
filename_ustack_t *_pop_ustack(filename_ustack_t **stack);
bool _ustack_empty(filename_ustack_t *stack);

/**
 * Search for invalid index criteria options in a given string.
 * @param str string containing the user-provided options.
 * @return true if string contains invalid contents, false otherwise
 */
bool index_criteria_contains_invalid_options(const char *str)
{
	const char *options[] = { "name=", "size=", NULL };
	const char *ptr = str;
	bool valid_option;
	bool error = true;
	int i;

	if (! str)
		return !error;
	else if (strlen(str) < 5) {
		ltfsmsg(LTFS_ERR, "11146E", str);
		return error;
	}

	/* Check the beginning of the string */
	for (valid_option = false, i=0; options[i]; ++i)
		if (! strncasecmp(options[i], str, strlen(options[i]))) {
			valid_option = true;
			break;
		}
	if (! valid_option) {
		ltfsmsg(LTFS_ERR, "11146E", str);
		return error;
	}

	/* Check which options come after each separator */
	while (true) {
		ptr = strstr(ptr+1, "/");
		if (! ptr)
			break;
		for (valid_option = false, i=0; options[i]; ++i)
			if (! strncasecmp(options[i], ptr+1, strlen(options[i]))) {
				valid_option = true;
				break;
			}
		if (! valid_option) {
			ltfsmsg(LTFS_ERR, "11146E", ptr+1);
			return error;
		}
	}
	return !error;
}

/**
 * Search for an index criteria option in a given string.
 * @param str string containing the user-provided options
 * @param substr option to look for
 * @param start output pointer to the start of the match
 * @param end output pointer to the end of the match
 * @param error output flag if a syntax error was found while parsing the options
 * @return true on success or false if the option could not be found
 */
bool index_criteria_find_option(const char *str, const char *substr,
	const char **start, const char **end, bool *error)
{
	const char *str_start = NULL, *str_end = NULL;
	const char *next_start = NULL, *next_end = NULL;
	int substr_len = strlen(substr);
	bool next_error, found = false;

	if (strlen(str) < 5)
		return false;

	if (! strncasecmp(str, substr, substr_len)) {
		/* Match at the start of the string */
		str_start = str;
	} else {
		/* Need to walk the string to find the first valid match */
		str_start = str + 1;
		found = false;
		while (! found) {
			str_start = strcasestr(str_start, substr);
			if (! str_start)
				break;
			else if (*(str_start-1) == '/')
				found = true;
			else
				++str_start;
		}
		if (! found)
			return false;
	}

	/* Find end of option */
	for (str_end=str_start; *str_end; ++str_end) {
		if (*str_end == '/')
			break;
	}

	if (index_criteria_find_option(str_end, substr, &next_start, &next_end, &next_error) == true) {
		ltfsmsg(LTFS_ERR, "11147E", substr);
		*error = true;
		return false;
	}

	*start = str_start;
	*end   = str_end;
	*error = false;
	return true;
}

/**
 * Parse the index criteria size= option
 * @param criteria size= string
 * @param len length of the criteria string
 * @param ic pointer to the index criteria structure
 * @return 0 on success or a negative value on error
 */
int index_criteria_parse_size(const char *criteria, size_t len, struct index_criteria *ic)
{
	int ret = 0, multiplier = 1;
	char rule[len+1], last, *ptr;

	snprintf(rule, len-strlen("size="), "%s", criteria + strlen("size="));

	for (ptr=&rule[0]; *ptr; ptr++) {
		if (isalpha(*ptr) && *(ptr+1) && isalpha(*(ptr+1))) {
			ltfsmsg(LTFS_ERR, "11148E");
			return -LTFS_POLICY_INVALID;
		}
	}
	last = (rule[strlen(rule)-1]);

	if (isalpha(last)) {
		if (last == 'k' || last == 'K')
			multiplier = 1024;
		else if (last == 'm' || last == 'M')
			multiplier = 1024 * 1024;
		else if (last == 'g' || last == 'G')
			multiplier = 1024 * 1024 * 1024;
		else {
			ltfsmsg(LTFS_ERR, "11149E", last);
			return -LTFS_POLICY_INVALID;
		}
		rule[strlen(rule)-1] = '\0';
	}

	if (strlen(rule) == 0) {
		ltfsmsg(LTFS_ERR, "11150E");
		return -LTFS_POLICY_INVALID;
	} else if (!isdigit(rule[0])) {
		ltfsmsg(LTFS_ERR, "11151E");
		return -LTFS_POLICY_INVALID;
	}
	ic->max_filesize_criteria = strtoull(rule, NULL, 10) * multiplier;

	return ret;
}

/**
 * Parse the index criteria name= option
 * @param criteria name= string
 * @param len length of the criteria string
 * @param ic pointer to the index criteria structure
 * @return 0 on success or a negative value on error
 */
int index_criteria_parse_name(const char *criteria, size_t len, struct index_criteria *ic)
{
	char *delim, *rule, rulebuf[len+1];
	char **rule_ptr;
	int ret = 0, i = 0, num_names = 0;

	num_names = 1;
	snprintf(rulebuf, len, "%s", criteria);
	rule = rulebuf;

	/* Count the rules and check for empty ones */
	if (rule[5] == ':') {
		ltfsmsg(LTFS_ERR, "11305E", rule);
		return -LTFS_POLICY_EMPTY_RULE;
	}
	for (delim=rule+6; *delim; delim++) {
		if (*delim == ':') {
			if (*(delim-1) == ':' || *(delim+1) == '\0') {
				ltfsmsg(LTFS_ERR, "11305E", rule);
				return -LTFS_POLICY_EMPTY_RULE;
			}
			++num_names;
		}
	}

	ic->glob_patterns = calloc(num_names+1, sizeof(char*));
	if (! ic->glob_patterns) {
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}

	/* Assign rules to the glob_patterns[] array */
	rule = rule+5;
	for (i=0, delim=rule; *delim; delim++) {
		if (*delim == ':') {
			*delim = '\0';
			ic->glob_patterns[i++] = strdup(rule);
			rule = delim+1;
		} else if (*delim == '/') {
			*delim = '\0';
			ic->glob_patterns[i++] = strdup(rule);
		} else if (*(delim+1) == '\0')
			ic->glob_patterns[i++] = strdup(rule);
	}
	if (i == 0)
		ic->glob_patterns[i++] = strdup(rule);

	/* Validate rules */
	if (ret == 0) {
		rule_ptr = ic->glob_patterns;
		while (*rule_ptr && ret == 0) {
			ret = pathname_validate_file(*rule_ptr);
			if (ret == -LTFS_INVALID_PATH)
				ltfsmsg(LTFS_ERR, "11302E", *rule_ptr);
			else if (ret == -LTFS_NAMETOOLONG)
				ltfsmsg(LTFS_ERR, "11303E", *rule_ptr);
			else if (ret < 0)
				ltfsmsg(LTFS_ERR, "11304E", ret);
			++rule_ptr;
		}
	}

	return ret;
}

/**
 * Parse a string containing the index partition criteria, populating the internal
 * members of struct ltfs_volume accordingly.
 * @param filterrules input string containing the desired index partition criteria
 * @param vol LTFS volume
 * @return 0 if parsing the index partition criteria succeeds or a negative value if not.
 */
int index_criteria_parse(const char *filterrules, struct ltfs_volume *vol)
{
	const char *start = NULL, *end = NULL;
	struct index_criteria *ic;
	bool has_name = false, error = false;
	int ret = 0;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	if (! filterrules) {
		vol->index->index_criteria.have_criteria = false;
		return 0;
	}

	ic = &vol->index->index_criteria;
	index_criteria_free(ic);
	ic->have_criteria = true;

	/* Sanity checks */
	if (index_criteria_contains_invalid_options(filterrules)) {
		ltfsmsg(LTFS_ERR, "11152E");
		return -LTFS_POLICY_INVALID;
	}

	/* Process name= criteria */
	if (index_criteria_find_option(filterrules, "name=", &start, &end, &error)) {
		ret = index_criteria_parse_name(start, end-start+1, ic);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "11153E", ret);
			return ret;
		}
		has_name = true;
	} else if (error) {
		ltfsmsg(LTFS_ERR, "11154E");
		return -LTFS_POLICY_INVALID;
	}

	/* Process size= criteria */
	ic->max_filesize_criteria = 0;
	if (index_criteria_find_option(filterrules, "size=", &start, &end, &error)) {
		ret = index_criteria_parse_size(start, end-start+1, ic);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "11155E", ret);
			return ret;
		}
	} else if (error) {
		ltfsmsg(LTFS_ERR, "11156E");
		return -LTFS_POLICY_INVALID;
	} else if (has_name) {
		ltfsmsg(LTFS_ERR, "11157E");
		return -LTFS_POLICY_INVALID;
	}

	return ret;
}

/**
 * Set the override flag. Typically used by mkltfs.
 * @param allow value to set the override flag to
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int index_criteria_set_allow_update(bool allow, struct ltfs_volume *vol)
{
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol->index, -LTFS_NULL_ARG);

	vol->index->criteria_allow_update = allow;
	return 0;
}

/**
 * Duplicate an index_criteria structure.
 * @param dest_ic destination index criteria
 * @param src_ic Index criteria to duplicate.
 * @return 0 on success or a negative value on error
 */
int index_criteria_dup_rules(struct index_criteria *dest_ic, struct index_criteria *src_ic)
{
	int i, counter = 0;

	CHECK_ARG_NULL(dest_ic, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(src_ic, -LTFS_NULL_ARG);

	index_criteria_free(dest_ic);

	memcpy(dest_ic, src_ic, sizeof(*src_ic));
	dest_ic->glob_cache = NULL; /* regenerate glob cache lazily */
	if (src_ic->have_criteria && src_ic->glob_patterns) {
		while (src_ic->glob_patterns[counter])
			counter++;
		dest_ic->glob_patterns = calloc(counter+1, sizeof(char *));
		if (! dest_ic->glob_patterns) {
			ltfsmsg(LTFS_ERR, "10001E", "index_criteria_dup_rules: glob pattern array");
			return -LTFS_NO_MEMORY;
		}
		for (i=0; i<counter; ++i) {
			dest_ic->glob_patterns[i] = strdup(src_ic->glob_patterns[i]);
			if (! dest_ic->glob_patterns[i]) {
				ltfsmsg(LTFS_ERR, "10001E", "index_criteria_dup_rules: glob pattern");
				while (--i >= 0)
					free(dest_ic->glob_patterns[i]);
				free(dest_ic->glob_patterns);
				return -LTFS_NO_MEMORY;
			}
		}
	}

	return 0;
}

/**
 * Free members from the index_criteria structure.
 * @param ic index criteria
 */
void index_criteria_free(struct index_criteria *ic)
{
	if (! ic) {
		ltfsmsg(LTFS_WARN, "10006W", "ic", __FUNCTION__);
		return;
	} else if (! ic->have_criteria)
		return;

	if (ic->glob_patterns) {
		char **globptr = ic->glob_patterns;
		while (*globptr && **globptr) {
			free(*globptr);
			++globptr;
		}
		free(ic->glob_patterns);
		ic->glob_patterns = NULL;
	}
	if (ic->glob_cache) {
		UChar **globptr = ic->glob_cache;
		while (*globptr && **globptr) {
			free(*globptr);
			++globptr;
		}
		free(ic->glob_cache);
		ic->glob_cache = NULL;
	}
	ic->max_filesize_criteria = 0;
	ic->have_criteria = false;
}

/**
 * Return the maximum file size criteria set for the index partition.
 * @param vol LTFS volume
 * @return the maximum file size criteria
 */
size_t index_criteria_get_max_filesize(struct ltfs_volume *vol)
{
	struct index_criteria *ic;

	CHECK_ARG_NULL(vol, 0);
	CHECK_ARG_NULL(vol->index, 0);

	ic = &vol->index->index_criteria;
	if (ic->have_criteria)
		return ic->max_filesize_criteria;
	else
		return 0; /* if no policy specified, don't put anything on the index partition */
}

/**
 * Return the NULL-terminated list of file name criteria for the index partition.
 * @param vol LTFS volume
 * @return the file name criteria for the index partition, which may be NULL if one
 *  was not specified by the user.
 */
const char **index_criteria_get_glob_patterns(struct ltfs_volume *vol)
{
	struct index_criteria *ic;

	CHECK_ARG_NULL(vol, 0);

	ic = &vol->index->index_criteria;
	if (ic->have_criteria)
		return (const char **) ic->glob_patterns;
	else
		return NULL; /* if no policy specified, don't store anything on the index partition */
}


/**
 * Returns true if a given file name matches the criteria set in the index file or false if not.
 *
 * Also, if the maximum file size criteria is 0 and no file name criteria has been set, we
 * simply return false. Finally, if no file name criteria is set then we just return true,
 * meaning that the caching will be performed based on the file size only.
 *
 */
bool index_criteria_match(struct dentry *d, struct ltfs_volume *vol)
{
	int ret;
	struct index_criteria *ic;
	UChar **glob_cache;
	int match, i;
	UChar *dname;
	int32_t dname_len, glob_len;

	CHECK_ARG_NULL(vol, false);
	CHECK_ARG_NULL(d, false);

	ic = &vol->index->index_criteria;
	if (! ic->have_criteria || ic->max_filesize_criteria == 0) {
		/* Disable writing to the index partition if not bound by a maximum cache size */
		return false;
	} else if (! ic->glob_patterns) {
		/* Criteria is set on file size only */
		return true;
	}

	if (! ic->glob_cache) {
		ret = _prepare_glob_cache(ic);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "11158E", ret);
			return ret;
		}
	}
	glob_cache = ic->glob_cache;

	/* Prepare the dentry's name for caseless matching. */
	ret = pathname_prepare_caseless(d->name, &dname, false);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "11159E", ret);
		return ret;
	}
	dname_len = u_strlen(dname);

	for (i=0; glob_cache[i]; ++i) {
		glob_len = u_strlen(glob_cache[i]);
		match = _matches_name_criteria_caseless(glob_cache[i], glob_len, dname, dname_len);
		if (match > 0) {
			free(dname);
			return true;
		} else if (match < 0) {
			ltfsmsg(LTFS_ERR, "11161E", match);
		}
	}

	free(dname);
	return false;
}

/**
 * Prepare a caseless glob cache for the given index criteria. The glob patterns
 * must not be empty. It recomputes the glob cache if there's one already present.
 */
int _prepare_glob_cache(struct index_criteria *ic)
{
	int i, ret, num_patterns;

	if (ic->glob_cache) {
		UChar **globptr = ic->glob_cache;
		while (*globptr && **globptr) {
			free(*globptr);
			++globptr;
		}
		free(ic->glob_cache);
	}

	num_patterns = 0;
	while (ic->glob_patterns[num_patterns])
		++num_patterns;

	ic->glob_cache = calloc(num_patterns + 1, sizeof(UChar *));
	if (! ic->glob_cache) {
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}
	for (i=0; ic->glob_patterns[i]; ++i) {
		ret = pathname_prepare_caseless(ic->glob_patterns[i], &ic->glob_cache[i], false);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "11160E", ret);
			return ret;
		}
	}

	return 0;
}

/**
 * Check whether a string matches the given criteria. Matching is performed using
 * filename globbing ("*" and "?" are supported), and it is performed by grapheme cluster
 * rather than by code point.
 */
int _matches_name_criteria_caseless(const UChar *criteria, int32_t cr_len,
	const UChar *filename, int32_t fi_len)
{
	UBreakIterator *ub_criteria, *ub_filename;
	UErrorCode err = U_ZERO_ERROR;
	int32_t cr_bnd[3] = {0, 0, 0}, fi_bnd[3] = {0, 0, 0};
	filename_ustack_t *stack = NULL, *element;
	bool acceptany, have_asterisk;
	int match;

	/* Early exit conditions. */
	CHECK_ARG_NULL(criteria, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(filename, -LTFS_NULL_ARG);
	if (criteria[0] == 0 && filename[0] == 0) {
		return 1;
	} else if (criteria[0] == 0) {
		return 0;
	}

	/* Open text boundary iterators. */
	ub_criteria = ubrk_open(UBRK_CHARACTER, uloc_getDefault(), criteria, cr_len, &err);
	if (U_FAILURE(err)) {
		ltfsmsg(LTFS_ERR, "11162E", err);
		return -LTFS_ICU_ERROR;
	}
	ub_filename = ubrk_open(UBRK_CHARACTER, uloc_getDefault(), filename, fi_len, &err);
	if (U_FAILURE(err)) {
		ltfsmsg(LTFS_ERR, "11163E", err);
		ubrk_close(ub_criteria);
		return -LTFS_ICU_ERROR;
	}

	/* Perform matching. */
	_next_char(criteria, ub_criteria, cr_bnd);
	_next_char(filename, ub_filename, fi_bnd);
	have_asterisk = false;
	for (;;) {
		while (cr_bnd[0] != cr_bnd[1]) {

			/* Consume sequential asterisks. */
			while (criteria[cr_bnd[0]] == 0x2a && cr_bnd[2] == 1) {
				_next_char(criteria, ub_criteria, cr_bnd);
				if (cr_bnd[0] == cr_bnd[1]) {
					/* End of state machine, accept input. */
					match = 1;
					goto out;
				}
				have_asterisk = true;
			}

			/* If we got this far, the file name is done and the pattern doesn't end with
			 * an asterisk. No match. */
			if (fi_bnd[0] == fi_bnd[1])
				break;

			/* Question mark: accept any character. */
			if (criteria[cr_bnd[0]] == 0x3f && cr_bnd[2] == 1)
				acceptany = true;
			else
				acceptany = false;

			/* Try to consume a file name character. */
			if (have_asterisk) {
				if (acceptany || ! _char_compare(criteria, cr_bnd, filename, fi_bnd)) {
					/* Push this position onto the stack and consume the character. */
					element = (filename_ustack_t *) calloc(1, sizeof(filename_ustack_t));
					if (! element) {
						ltfsmsg(LTFS_ERR, "10001E", "_matches_name_criteria_caseless: filename stack");
						match = 0;
						goto out;
					}
					memcpy(element->cr_bnd, cr_bnd, 3*sizeof(int32_t));
					memcpy(element->fi_bnd, fi_bnd, 3*sizeof(int32_t));
					_push_ustack(&stack, element);
					_next_char(criteria, ub_criteria, cr_bnd);
					_next_char(filename, ub_filename, fi_bnd);
					have_asterisk = false;
				} else {
					/* This character must be subsumed by the asterisk. */
					_next_char(filename, ub_filename, fi_bnd);
				}
			} else if (acceptany || ! _char_compare(criteria, cr_bnd, filename, fi_bnd)) {
				_next_char(criteria, ub_criteria, cr_bnd);
				_next_char(filename, ub_filename, fi_bnd);
			} else {
				/* Try to pop an element off the stack. */
				if (_ustack_empty(stack)) {
					match = 0;
					goto out;
				} else {
					element = _pop_ustack(&stack);
					memcpy(cr_bnd, element->cr_bnd, 3*sizeof(int32_t));
					memcpy(fi_bnd, element->fi_bnd, 3*sizeof(int32_t));
					free(element);
					ubrk_following(ub_criteria, cr_bnd[0]);
					ubrk_following(ub_filename, fi_bnd[0]);
					_next_char(filename, ub_filename, fi_bnd);
					have_asterisk = true;
				}
			}
		}

		if (cr_bnd[0] == cr_bnd[1] && fi_bnd[0] == fi_bnd[1]) {
			/* Reached the end of the pattern and the name at the same time. */
			match = 1;
			goto out;
		} else if (_ustack_empty(stack)) {
			match = 0;
			goto out;
		} else {
			/* Pop an element off the stack. */
			element = _pop_ustack(&stack);
			memcpy(cr_bnd, element->cr_bnd, 3*sizeof(int32_t));
			memcpy(fi_bnd, element->fi_bnd, 3*sizeof(int32_t));
			free(element);
			ubrk_following(ub_criteria, cr_bnd[0]);
			ubrk_following(ub_filename, fi_bnd[0]);
			_next_char(filename, ub_filename, fi_bnd);
			have_asterisk = true;
		}
	}

out:
	_destroy_ustack(&stack);
	ubrk_close(ub_criteria);
	ubrk_close(ub_filename);
	return match;
}

/**
 * Advance the given character break iterator by one character position.
 */
void _next_char(const UChar *str, UBreakIterator *it, int32_t *pos)
{
	pos[0] = pos[1];
	pos[1] = ubrk_next(it);
	if (pos[1] == -1) {
		pos[1] = pos[0];
		while(str[pos[1]] != 0)
			++pos[1];
	}
	pos[2] = pos[1] - pos[0];
}

/**
 * @return 0 if characters are equal, nonzero otherwise.
 */
int _char_compare(const UChar *str1, int32_t *pos1, const UChar *str2, int32_t *pos2)
{
	const UChar *c1, *c1_end, *c2;
	if (pos1[2] != pos2[2])
		return 1;
	c1 = str1 + pos1[0];
	c1_end = str1 + pos1[1];
	c2 = str2 + pos2[0];
	while (c1 < c1_end) {
		if (*c1 != *c2)
			return 1;
		++c1;
		++c2;
	}
	return 0;
}

void _destroy_ustack(filename_ustack_t **stack)
{
	filename_ustack_t *ptr, *next;
	if (! stack)
		return;
	ptr = *stack;
	while (ptr) {
		next = ptr->next;
		free(ptr);
		ptr = next;
	}
}

int _push_ustack(filename_ustack_t **stack, filename_ustack_t *element)
{
	if (! stack) {
		ltfsmsg(LTFS_ERR, "11164E");
		return -1;
	}
	if (! *stack)
		*stack = element;
	else
		(*stack)->next = element;
	element->next = NULL;
	return 0;
}

filename_ustack_t *_pop_ustack(filename_ustack_t **stack)
{
	filename_ustack_t *prev = NULL, *top;
	if (! stack) {
		ltfsmsg(LTFS_ERR, "11165E");
		return NULL;
	}
	for (top=*stack; top->next; top=top->next)
		prev = top;
	if (! prev) {
		/* Removing base */
		*stack = NULL;
		return top;
	} else
		prev->next = NULL;
	return top;
}

bool _ustack_empty(filename_ustack_t *stack)
{
	if (! stack)
		return true;
	return false;
}
