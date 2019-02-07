/************************************************************************************
**
**  Hewlett Packard LTFS backend for LTO and DAT tape drives
**
** FILE:            ltotape_timeout.h
**
** CONTENTS:        Timeout values for LTO and DAT drives
**
** (C) Copyright 2015-2017 Hewlett Packard Enterprise Development LP
**
** This program is free software; you can redistribute it and/or modify it
**  under the terms of version 2.1 of the GNU Lesser General Public License
**  as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful, but 
**  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
**  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
**  License for more details.
**
** You should have received a copy of the GNU General Public License along
**  with this program; if not, write to:
**    Free Software Foundation, Inc.
**    51 Franklin Street, Fifth Floor
**    Boston, MA 02110-1301, USA.
**
**   26 April 2010
**
*************************************************************************************
*/

#ifndef _ltotape_timeout_h
#define _ltotape_timeout_h

/*
 * Timeout values for various LTO drive SCSI operations
 *  measured in milliseconds
 */
#define LTO_DEFAULT_TIMEOUT            60000  
#define LTO_ERASE_TIMEOUT            1560000  /* NB for SHORT ERASE only */ 
#define LTO_INQUIRY_TIMEOUT            60000
#define LTO_LOAD_TIMEOUT              840000
#define LTO_LOCATE_TIMEOUT           2940000      
#define LTO_LOGSELECT_TIMEOUT          60000
#define LTO_LOGSENSE_TIMEOUT           60000
#define LTO_MODESELECT_TIMEOUT         60000
#define LTO_MODESENSE_TIMEOUT          60000
#define LTO_UNLOAD_TIMEOUT            840000
#define LTO_PREVENTALLOWMEDIA_TIMEOUT  60000
#define LTO_READ_TIMEOUT             2340000
#define LTO_READATTRIB_TIMEOUT         60000
#define LTO_READBLOCKLIMITS_TIMEOUT    60000
#define LTO_READBUFFER_TIMEOUT        480000
#define LTO_READPOSITION_TIMEOUT       60000
#define LTO_RELEASE_TIMEOUT            60000
#define LTO_REPORTDENSITY_TIMEOUT      60000
#define LTO_RESERVE_TIMEOUT            60000
#define LTO_REWIND_TIMEOUT            660000
#define LTO_SPACE_TIMEOUT            2940000
#define LTO_TESTUNITREADY_TIMEOUT      60000
#define LTO_WRITE_TIMEOUT            1560000
#define LTO_WRITEATTRIB_TIMEOUT        60000
#define LTO_WRITEFILEMARK_TIMEOUT    1680000
#define LTO_FORMAT_TIMEOUT           3240000

/*
 * Timeout values for various DAT drive SCSI operations
 *  measured in milliseconds
 */
#define DAT_DEFAULT_TIMEOUT           360000  
#define DAT_ERASE_TIMEOUT             360000  /* NB for SHORT ERASE only */ 
#define DAT_INQUIRY_TIMEOUT            60000
#define DAT_LOAD_TIMEOUT              900000
#define DAT_LOCATE_TIMEOUT            600000      
#define DAT_LOGSELECT_TIMEOUT          60000
#define DAT_LOGSENSE_TIMEOUT           60000
#define DAT_MODESELECT_TIMEOUT      28800000
#define DAT_MODESENSE_TIMEOUT          60000
#define DAT_UNLOAD_TIMEOUT            600000
#define DAT_PREVENTALLOWMEDIA_TIMEOUT  60000
#define DAT_READ_TIMEOUT             1200000
#define DAT_READATTRIB_TIMEOUT         60000
#define DAT_READBLOCKLIMITS_TIMEOUT    60000
#define DAT_READBUFFER_TIMEOUT         60000
#define DAT_READPOSITION_TIMEOUT       60000
#define DAT_RELEASE_TIMEOUT            60000
#define DAT_REPORTDENSITY_TIMEOUT      60000
#define DAT_RESERVE_TIMEOUT            60000
#define DAT_REWIND_TIMEOUT            600000
#define DAT_SPACE_TIMEOUT            1200000
#define DAT_TESTUNITREADY_TIMEOUT      60000
#define DAT_WRITE_TIMEOUT             300000
#define DAT_WRITEATTRIB_TIMEOUT        60000
#define DAT_WRITEFILEMARK_TIMEOUT     300000
#define DAT_FORMAT_TIMEOUT             60000

#endif /*ifndef _ltotape_timeout_h */
