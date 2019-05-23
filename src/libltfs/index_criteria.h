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
** FILE NAME:       index_criteria.h
**
** DESCRIPTION:     Header file for the routines that deal with the index partition 
**                  criteria.
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/
#ifndef __index_criteria_h
#define __index_criteria_h

#ifdef __cplusplus
extern "C" {
#endif

int index_criteria_parse(const char *filterrules, struct ltfs_volume *vol);
int index_criteria_dup_rules(struct index_criteria *dest_ic, struct index_criteria *src_ic);
int index_criteria_set_allow_update(bool allow, struct ltfs_volume *vol);
bool index_criteria_match(struct dentry *d, struct ltfs_volume *vol);
size_t index_criteria_get_max_filesize(struct ltfs_volume *vol);
const char **index_criteria_get_glob_patterns(struct ltfs_volume *vol);
void index_criteria_free(struct index_criteria *ic);

#ifdef __cplusplus
}
#endif

#endif /* __index_criteria_h */
