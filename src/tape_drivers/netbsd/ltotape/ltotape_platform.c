/*************************************************************************************
**
**  Hewlett Packard LTFS backend for LTO and DAT tape drives
**
** FILE:            ltotape_platform.c
**
** CONTENTS:        Platform-specific portion of ltotape LTFS backend for Linux
**
** (C) Copyright 2015, 2016 Hewlett Packard Enterprise Development LP
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

#define __ltotape_platform_c

#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <assert.h>
#include <string.h>
#include <sys/scsiio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "../../../libltfs/ltfs_error.h"
#include "../../../ltfsprintf.h"
#include "../../../libltfs/ltfslogging.h"
#include "ltotape.h"
#include "ltotape_diag.h"
#include "ltotape_supdevs.h"

/*
 * Max transfer size to ask the SG driver to handle (1MB):
 */
#define REQUESTED_MAX_SG_LENGTH   1048576

/*
 * Platform-specific implementation functions contained in this file:
 */
int ltotape_open(const char *devname, void **handle);
int ltotape_reopen(const char *devname, void *handle);
int ltotape_close(void *device);
int ltotape_scsiexec (ltotape_scsi_io_type *scsi_io);

/*
 * Backend functions used below in ltotape_open:
 */
extern int ltotape_inquiry(void *device, struct tc_inq *inq);
extern int ltotape_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage, unsigned char *buf, const size_t size);
extern int ltotape_evpd_inquiry(void *device, int vpdpage, unsigned char* idata, int ilen);
extern int ltotape_rewind(void *device, struct tc_position *pos);
extern int ltotape_test_unit_ready (void *device);

/*
 * Default tape device
 */
const char *ltotape_default_device = "/dev/nst0";

/*****************************************************************************
 * Utility function to generate a hex representation of some data.
 *  Caller must free returned pointer after use.
 *****************************************************************************/
char *ltotape_printbytes(unsigned char *data, int num_bytes)
{
	int		i = 0, len = 0;
	char	*print_string = NULL;

	print_string = (char*) calloc(num_bytes * 4, sizeof(char));
	if (print_string == (char *) NULL) {
		ltfsmsg(LTFS_ERR, 10001E, "ltotape_printbytes: temp string data");
		return NULL;

	} else {
		for (i = 0, len = 0; i < num_bytes; i++, len += 3) {
			sprintf(print_string + len, "%2.2X ", *(data + i));
		}
		return print_string;
	}
}

/**
 * Set up and execute the SCSI command indicated by scsi_io.
 *
 * @param scsi_io Will contain the cdb which will indicate the command to be executed.
 * @return int -1 on failure, 0 on success, >0 (# of bytes transferred) if a read/write command is
 * successful.
 */
int ltotape_scsiexec (ltotape_scsi_io_type *scsi_io)
{
  int		 status;
  scsireq_t      screq;
  int            scsi_status;
  int            driver_status;
  char          *pString;
  int            retry_count = 0;
  
start:
  memset ((void*)&screq, 0, sizeof (screq));

/*
 * Set up required fields:
 */
  if (scsi_io->data_direction == HOST_READ)
    screq.flags |= SCCMD_READ;
  if (scsi_io->data_direction == HOST_WRITE)
    screq.flags |= SCCMD_WRITE;
  
  screq.timeout     = (unsigned long) scsi_io->timeout_ms;
  screq.cmdlen      = (unsigned char) scsi_io->cdb_length;
  assert(screq.cmdlen <= sizeof(screq.cmd));

  memcpy(screq.cmd, scsi_io->cdb, screq.cmdlen);

  screq.senselen    = sizeof(screq.sense);
  assert(screq.senselen <= sizeof(screq.sense));

  screq.datalen    =  scsi_io->data_length;
  screq.databuf    =  (void*) scsi_io->data;

  pString = ltotape_printbytes (scsi_io->cdb, scsi_io->cdb_length);
  ltfsmsg(LTFS_DEBUG, 20010D, pString, scsi_io->data_length);
  if (pString != (char*)NULL) {
     free (pString);
  }

/*
 * Here's the actual command execution:
 */
  status = ioctl (scsi_io->fd, SCIOCCOMMAND, &screq);

/*
 * Now determine the outcome:
 */
  scsi_status = S_NO_STATUS;  /* until proven otherwise    */

/*
 * the command requested was not accepted by the driver
 */
  if ((status < 0) || (screq.retsts == SCCMD_UNKNOWN)) {
    driver_status = DS_ILLEGAL;
    
/*
 * Unit didn't respond to selection
 */
  } else if (screq.retsts == SCCMD_BUSY) {
    driver_status = DS_SELECTION_TIMEOUT;

/*
 * Unit timed out
 */
  } else if (screq.retsts == SCCMD_TIMEOUT) {
    /*
     * Restart timed out read or write
     */
    if ((screq.cmd[0] == CMDread || screq.cmd[0] == CMDwrite)) {
      if (!retry_count) {
        ltfsmsg(LTFS_ERR, 20046E, screq.cmd[0] == CMDread ? "read" : "write");
        retry_count++;
        goto start;
      }
    }

    driver_status = DS_TIMEOUT;
    errno = ETIMEDOUT;
    
/*
 * Good drive status
 */
  } else if ((screq.retsts == SCCMD_OK) || (screq.retsts = SCCMD_SENSE)){
    driver_status = DS_GOOD;
    scsi_status = screq.status;

/*
 * For anything else, create a composite value of "driver status" to help with debug:
 */
  } else {
    driver_status = (DS_FAILED << 16) | (screq.retsts & 0xFF);
  }

  scsi_io->actual_data_length = screq.datalen_used;
  scsi_io->sense_length       = screq.senselen_used;
  memcpy(scsi_io->sensedata, screq.sense, screq.senselen_used);
  
/*
 * A driver error is always bad news:
 */
  if (driver_status != DS_GOOD) {
    status = -1;

    ltfsmsg(LTFS_DEBUG, 20089D, "errno", errno);
    ltfsmsg(LTFS_DEBUG, 20089D, "host_status", screq.retsts);
    ltfsmsg(LTFS_DEBUG, 20089D, "driver_status", screq.retsts);
    ltfsmsg(LTFS_DEBUG, 20089D, "status", screq.status);
    
/*
 * A SCSI error is bad, UNLESS:
 *  a) we were doing a read AND the only problem was an ILI condition.. OR
 *  b) we were doing a write/writeFM AND the only problem was EWEOM..
 * in which case all was well really!
 *
 * Note that "real" EOM has sense key 0xD (VOLUME OVERFLOW); EWEOM has sense key 0x0 (NO SENSE).
 * For early warning we pretend all was well but make a note to report it on the NEXT write
 * For Real EOM, we must report EIO because there is physically no more space on tape
 */
  } else if (scsi_status != S_GOOD) {

     if (scsi_status == S_CHECK_CONDITION) {

	 if ((scsi_io->cdb[0] == CMDread) && (SENSE_HAS_ILI_SET(scsi_io->sensedata))) {
            int resid = ((int)scsi_io->sensedata[3] << 24) + 
		        ((int)scsi_io->sensedata[4] << 16) +
	                ((int)scsi_io->sensedata[5] <<  8) + 
			((int)scsi_io->sensedata[6]      );
            scsi_io->actual_data_length = scsi_io->data_length - resid;
            status = scsi_io->actual_data_length;

	 } else if (((scsi_io->cdb[0] == CMDwrite) || (scsi_io->cdb[0] == CMDwrite_filemarks))  &&
		    (SENSE_IS_EARLY_WARNING_EOM(scsi_io->sensedata))) {
	     scsi_io->actual_data_length = scsi_io->data_length;
	     status = scsi_io->actual_data_length;

	     if (scsi_io->eweomstate == before_eweom) {
		scsi_io->eweomstate = report_eweom; /* Already written the data, so set flag to report next time */
	     }

	 } else if (((scsi_io->cdb[0] == CMDwrite) || (scsi_io->cdb[0] == CMDwrite_filemarks))  &&
		    (SENSE_IS_END_OF_MEDIA(scsi_io->sensedata))) {
             scsi_io->actual_data_length = 0;
	     status = -1;
	     errno = EIO;

	 } else {
	     status = -1;
	 }

     } else {
        status = -1;  /* Not GOOD and not CHECK CONDITION = BAD */
     }

/*
 * For successful read/write commands, return transferred length:
 */
  } else if ((scsi_io->cdb[0] == CMDread) || (scsi_io->cdb[0] == CMDwrite)) {
     status = scsi_io->actual_data_length;

/*
 * For everything else, return 0:
 */
  } else {
     status = 0;
  }

  ltfsmsg(LTFS_DEBUG, 20011D, driver_status, scsi_status, scsi_io->actual_data_length);
  if (scsi_status == S_CHECK_CONDITION) {
     pString = ltotape_printbytes (scsi_io->sensedata, scsi_io->sense_length);
     ltfsmsg(LTFS_DEBUG, 20012D, pString);
     if (pString != (char*)NULL) {
        free (pString);
     }
  }

  return (status);
}


/**
 * Open LTO tape backend.
 *
 * @param devname Device name of the LTO tape driver.
 * @param handle A pointer to the ltotape backend on success or NULL on error.
 * @return int DEVICE_GOOD on success else a -ve value on error.
 */
int ltotape_open(const char *devname, void **handle)
{
	unsigned char			modepage[TC_MP_MEDIUM_PARTITION_SIZE];
	unsigned char			snvpdpage[32];
	int				ret = DEVICE_GOOD, i =0, res_sz = 0;
	struct tc_inq			inq_data;
	ltotape_scsi_io_type	*device = NULL;

	CHECK_ARG_NULL(handle, -LTFS_NULL_ARG);
	*handle = NULL;


	memset(&inq_data, 0, sizeof(struct tc_inq));
	device = (ltotape_scsi_io_type *) calloc(1, sizeof(ltotape_scsi_io_type));
	if (device == (ltotape_scsi_io_type *) NULL) {
		/* Memory allocation failed. Return failure. */
		ltfsmsg(LTFS_ERR, 20100E);
		return -EDEV_NO_MEMORY;
	}

	/* Open the device. */
	device->fd = open(devname, O_RDWR | O_NDELAY);
	if (device->fd < 0) {
		device->fd = open(devname, O_RDONLY | O_NDELAY);
		if (device->fd < 0) {
			if (errno == EAGAIN) {
				ltfsmsg(LTFS_ERR, 20086E, devname);
				ret = -EDEV_DEVICE_BUSY;
			} else {
				ltfsmsg(LTFS_ERR, 20087E, devname, errno);
				ret = -EDEV_DEVICE_UNOPENABLE;
			}
			free(device); device = NULL;
			return ret;
		}
		ltfsmsg(LTFS_WARN, 20088W, devname);
	}

	/* Lock the opened device. */
	if (flock(device->fd, LOCK_EX | LOCK_NB) != 0) {
		ltfsmsg(LTFS_ERR, 20035E, strerror(errno));
		close(device->fd);
		free(device);
		return -EDEV_DEVICE_BUSY;
	}

#ifdef SG_SET_RESERVED_SIZE
	/*
	 * Try to ensure the driver is set up for larger transfer sizes..
	 */
	res_sz = REQUESTED_MAX_SG_LENGTH;
	ioctl (device->fd, SG_SET_RESERVED_SIZE, &res_sz);
	ioctl (device->fd, SG_GET_RESERVED_SIZE, &res_sz);
	ltfsmsg(LTFS_DEBUG, 20020D, res_sz);
#endif /* SG_SET_RESERVED_SIZE */

	/* Default timeout, should be overwritten by each backend function: */
	device->timeout_ms = LTO_DEFAULT_TIMEOUT;
	/* Default Early Warning EOM state is that we're not yet at the warning point:
	 */
	device->eweomstate = before_eweom;
	/* Default logfile directory - initially NULL; will get set if/when we
	 * parse FUSE options. */
	device->logdir = NULL;

	/* Find out what we're dealing with. */
	ret = ltotape_inquiry (device, &inq_data);
	if (ret) {
		ltfsmsg(LTFS_ERR, 20083E, ret);
		close (device->fd);
		free(device);
		return ret;
	} else {
		device->family = drivefamily_unknown;
		device->type = drive_unknown;
		memset (device->serialno,  0, sizeof(device->serialno));
		memset (snvpdpage, 0, sizeof(snvpdpage));
		i = 0;

		ltfsmsg(LTFS_DEBUG, 20084D, inq_data.pid);

		while (supported_devices[i].product_family != drivefamily_unknown) {
			if ((strncmp((char *)inq_data.pid, (char *)supported_devices[i].product_id,
					strlen((char *)supported_devices[i].product_id)) == 0)) {
				device->family = supported_devices[i].product_family;
				device->type = supported_devices[i].drive_type;

				if (ltotape_evpd_inquiry(device, VPD_PAGE_SERIAL_NUMBER, snvpdpage, sizeof(snvpdpage)) < 0) {
					strcpy (device->serialno, "Unknown");
				} else {
					strncpy (device->serialno, (const char*)(snvpdpage+4), (size_t)snvpdpage[3]);
				}

				ltfsmsg(LTFS_INFO, 20013I, supported_devices[i].description, device->serialno);
				break;
			}
			i++;
		}

		if(device->family == drivefamily_unknown) {
			ltfsmsg(LTFS_ERR, 20085E, inq_data.pid);
			close (device->fd);
			free(device);
			device = NULL;
			return -EDEV_DEVICE_UNSUPPORTABLE;
		}
	}

#ifdef QUANTUM_BUILD
	/*
	 * Store the drive vendor
	 */
	if ( strncmp((char *)inq_data.vid, "HP      ", 8 ) == 0 ) {
		device->drive_vendor_id = drivevendor_hp;
	}
	else if ( strncmp((char *)inq_data.vid, "QUANTUM ", 8 ) == 0 ) {
		device->drive_vendor_id = drivevendor_quantum;
	}
	else {
		device->drive_vendor_id = drivevendor_unknown;
	}
#endif

	/*
	 * For an LTO drive, need to determine whether it is partition-capable or only partition-aware:
	 */
	if (device->family == drivefamily_lto) {
		ret = ltotape_test_unit_ready (device);

		if (SENSE_IS_UNIT_ATTENTION(device->sensedata)) {
			ret = ltotape_test_unit_ready (device);
		}

		ret = ltotape_modesense (device, TC_MP_MEDIUM_PARTITION, TC_MP_PC_CHANGEABLE, 0, modepage, sizeof(modepage));
		if (ret < 0) {
			close (device->fd);
			free (device); /* no need for ltfsmsg here since modesense will have done it already */
			return ret;

		} else if ((modepage [PARTTYPES_OFFSET] & PARTTYPES_MASK) != PARTTYPES_MASK) {
			ltfsmsg(LTFS_ERR, 20014E, inq_data.revision);
			close (device->fd);
			free (device);
			return ret;
		}
	}

	*handle = (void *) device;
	return DEVICE_GOOD;
}

/**
 * Reopen a device. If reopen is not needed, do nothing in this call.
 *
 * @param devname The name of the device to be opened.
 * @param handle Device handle returned by the backend's open()
 * @return DEVICE_GOOD on success, else a negative value indicating the status
 * is returned.
 */
int ltotape_reopen(const char *devname, void *handle)
{
	int		ret = DEVICE_GOOD;

	return ret;
}

/**
 * Close a previously opened device and clear the ltotape backend.
 *
 * @param device Pointer to the ltotape backend.
 * @return 0 on sucess, a negative value on error.
 */
int ltotape_close(void *device)
{
	struct tc_position		pos;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type *) device;

	CHECK_ARG_NULL(sio, -EDEV_INVALID_ARG);

	ltotape_rewind(sio, &pos);
	close (sio->fd);
	free(sio);

	return 0;
}

/**
 * Close only the device file descriptor.
 *
 * @param device The ltotape backend handle.
 * @return 0 on success, a negative value on error.
 */
int ltotape_close_raw(void *device)
{
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type *) device;

	CHECK_ARG_NULL(sio, -EDEV_INVALID_ARG);

	close(sio->fd);
	sio->fd = -1;

	return DEVICE_GOOD;
}

#undef __ltotape_platform_c
