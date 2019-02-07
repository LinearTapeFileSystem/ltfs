/************************************************************************************
**
**  Hewlett Packard LTFS backend for HP LTO and DAT tape drives
**
** FILE:            ltotape_supdevs.h
**
** CONTENTS:        Array of devices supported by this backend
**
** (C) Copyright 2015 - 2017 Hewlett Packard Enterprise Development LP
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
**   18 August 2010
**
*************************************************************************************
*/

#ifndef __ltotape_supdevs_h
#define __ltotape_supdevs_h

/*
 * Array of device types supported by this back end:
 */
const supported_device_type supported_devices[] = {
   { "Ultrium 5-SCSI  ", drivefamily_lto,     "HP LTO5",			      drive_lto5  	},
   { "Ultrium 6-SCSI  ", drivefamily_lto,     "HP LTO6",			      drive_lto6  	},
   { "Ultrium 7-SCSI  ", drivefamily_lto,     "HP LTO7",			      drive_lto7  	},
   { "Ultrium 8-SCSI  ", drivefamily_lto,     "HPE LTO8",			      drive_lto8  	},
   { "ULTRIUM 5       ", drivefamily_lto,     "Quantum LTO5",		   drive_lto5  	},
   { "ULTRIUM 6       ", drivefamily_lto,     "Quantum LTO6",      	drive_lto6		},
   { "ULTRIUM 7       ", drivefamily_lto,     "Quantum LTO7",      	drive_lto7		},
   { "ULTRIUM 8       ", drivefamily_lto,     "Quantum LTO8",      	drive_lto8		},
   { "LTO-5 HH        ", drivefamily_lto,     "TANDBERG DATA LTO5",  drive_lto5 		},
   { "LTO-6 HH        ", drivefamily_lto,     "TANDBERG DATA LTO6",	drive_lto6		},
   { "DAT320          ", drivefamily_dat,     "HP DAT320",			   drive_dat   	},
   { "DAT160          ", drivefamily_dat,     "HP DAT160",			   drive_dat   	},
   { (const char*)NULL,  drivefamily_unknown, "Unknown",			      drive_unknown	}
};

#endif // __ltotape_diag_h
