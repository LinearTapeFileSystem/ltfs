/************************************************************************************
**
**  Hewlett Packard LTFS backend for LTO and DAT tape drives
**
** FILE:            ltotape_diag.c
**
** CONTENTS:        Diagnostic routines specifically for LTO drives
**
** (C) Copyright 2015 - 2017 Hewlett Packard Enterprise Development LP
** (c) Copyright 2010, 2011 Quantum Corporation
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
**   13/10/2017 Resolve a runtime error which causes an Abort trap
*************************************************************************************
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <dirent.h>
#include "../../../libltfs/ltfslogging.h"
#include "../../../ltfs_copyright.h"
#include "../../../ltfsprintf.h"
#include "../../../libltfs/ltfs.h"
#include "ltotape.h"
#include "ltotape_compat.h"
#include "ltotape_diag.h"

#ifdef HPE_BUILD
volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n"HPLTFS_COPYRIGHT"\n";
#elif defined QUANTUM_BUILD
volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n"QTMLTFS_COPYRIGHT"\n";
#elif defined GENERIC_OEM_BUILD
volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n"GENERICLTFS_COPYRIGHT"\n";
#endif
/*
 * External function declaration (found in ltotape.c)
 */
extern int ltotape_scsiexec (ltotape_scsi_io_type *scsi_io);
extern char *ltotape_printbytes(unsigned char *data, int num_bytes);

/*
 * Forward function declarations of internal (private) functions:
 */
static int ltotape_snapshot_now (void* device);
static int ltotape_snapshot_dump (void* device, int diagid);
static int ltotape_read_drivedump(void *device, const char *fname);
static long long ltotape_get_buffer_size(int buff_id, unsigned char *buffer);
static int ltotape_read_mini_drivedump(void *device, const char *fname);
static int ltotape_read_snapshot (void* device, char* fname);
static int ltotape_trim_logs (char* serialno);
static int ltotape_sort_oldest (const struct dirent ** pA, const struct dirent ** pB);
static int ltotape_select_logfiles (const struct dirent *entry);

/****************************************************************************
 * File-scope global variables, used in the directory manipulation functions
 *  (which don't allow other parameters).  Would need to add mutex protection
 *  if this ever became multi-threaded..
 */
static char dirname[256];
static char drivesn[32];

/****************************************************************************
 * Return default directory for storing snapshot logs
 * @return pointer to directory name
 */
char* ltotape_get_default_snapshotdir (void)
{
#ifdef __APPLE__
	struct stat statbuf;
	int ret = 0;

	ret = stat(MACOS_LOGFILE_DIR, &statbuf);
	if (ret < 0) {
		ret = mkdir_p(MACOS_LOGFILE_DIR, S_IRWXU | S_IRWXG | S_IRWXO);
		if (ret < 0) {
			/* Failed to create work directory */
			ltfsmsg(LTFS_WARN, 20102W, ret);
		}
	} else if (! S_ISDIR(statbuf.st_mode)) {
		/* Path exists but is not a directory */
		ltfsmsg(LTFS_WARN, 20103W, MACOS_LOGFILE_DIR);
	}

	sprintf (dirname, MACOS_LOGFILE_DIR);
#else
  sprintf (dirname, LINUX_LOGFILE_DIR);
#endif

  return (dirname);
}

/****************************************************************************
 * Set directory to use for storing snapshot logs
 * @param newdir directory to use for snapshot logs
 * @return pointer to directory name
 *
 * HPE 10/13/2017 changes made to resolve a runtime error
 */
char* ltotape_set_snapshotdir (char* newdir)
{
    int slen = sizeof(dirname) - 1;
    if (dirname == newdir) {
        //    fprintf (stderr, "Nothing to do, pointing at same area of memory already\n");
    } else {
        strncpy (dirname, newdir, slen);
        dirname[slen] = '\0';
        //    fprintf (stderr, "dirname is now %s\n", dirname);
    }
    
    return (dirname);
}

/****************************************************************************
 * Request, retrieve and store a drive log snapshot
 * @param device a pointer to the ltotape backend tape device
 * @param minidump TRUE, to generate mini dump or FALSE, to generate full dump
 * @return 0 on success or negative value on error
 */
int ltotape_log_snapshot (void *device, int minidump)
{
	char       fname[1024];
	time_t     now;
	struct tm *tm_now;
	int        status;

	ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;

/*
 * Snapshots are only available for LTO devices
 */
	if (sio->family != drivefamily_lto) {
	   ltfsmsg(LTFS_DEBUG, 20017D, (sio->family == drivefamily_dat)?"DAT":"Unknown");
	   return -1;
	}

/*
 * Check whether we need to trim the number of log snapshots for this drive.  If the
 *  logfile directory struct member has not been set, then we won't do anything else.
 */
	if (sio->logdir == NULL) {
	   return 0;
	}

	ltotape_set_snapshotdir(sio->logdir);
	if (ltotape_trim_logs (sio->serialno) < 0) {
	  ltfsmsg(LTFS_INFO, 20099I, dirname);
	  return -1;
	}

/*
 * Make a base filename
 */
	time(&now);
	tm_now = localtime(&now);

#ifdef QUANTUM_BUILD
	if ( sio->drive_vendor_id == drivevendor_hp )
	{
		sprintf (fname, "%s/ltfs_%04d%02d%02d_%02d%02d%02d_%s.ltd", 
			 dirname,
			 tm_now->tm_year + 1900,
			 tm_now->tm_mon + 1,
			 tm_now->tm_mday,
			 tm_now->tm_hour,
			 tm_now->tm_min,
			 tm_now->tm_sec,
			 sio->serialno);
	}
	else if ( sio->drive_vendor_id == drivevendor_quantum ) 
	{
		sprintf (fname, "%s/ltfs_%04d%02d%02d_%02d%02d%02d_%s.svm", 
			 dirname,
			 tm_now->tm_year + 1900,
			 tm_now->tm_mon + 1,
			 tm_now->tm_mday,
			 tm_now->tm_hour,
			 tm_now->tm_min,
			 tm_now->tm_sec,
			 sio->serialno);
	}
	else // Drive vendor unknown
	{
	  /* "Unable to save drive dump to file"
          */
	  ltfsmsg(LTFS_WARN, 20079W );
	  return -2;
	}
#else
	sprintf (fname, "%s/ltfs_%04d%02d%02d_%02d%02d%02d_%s.ltd", 
		 dirname,
		 tm_now->tm_year + 1900,
		 tm_now->tm_mon + 1,
		 tm_now->tm_mday,
		 tm_now->tm_hour,
		 tm_now->tm_min,
		 tm_now->tm_sec,
		 sio->serialno);
#endif

/*
 * Trigger a log snapshot, then read and store the log:
 */
	ltfsmsg(LTFS_INFO, 20076I);
	if ((sio->type == drive_lto7)||(sio->type == drive_lto8)) {
		if(minidump) {
			status = ltotape_snapshot_dump (device, 0x63);
		}else {
			status = ltotape_snapshot_dump (device, 0x60);
		}
	}else {
		status = ltotape_snapshot_now (device);
	}
	if (status == -1) {
           ltfsmsg(LTFS_WARN, 20077W, status);

	} else {
		if ((sio->type == drive_lto7)||(sio->type == drive_lto8)) {
			if (minidump) {
				status = ltotape_read_mini_drivedump(device, fname);
			} else {
				status = ltotape_read_drivedump(device, fname);
			}

		} else {
			status = ltotape_read_snapshot(device, fname);
		}
	   if (status == -1) {         /* -1 = SCSI problem */
	     ltfsmsg(LTFS_WARN, 20078W, status);
		 
	   } else if (status == -2) {  /* -2 = file saving problem */
		  ltfsmsg(LTFS_WARN, 20079W);

	   } else if (status == -3) {  /* -3 = malloc problem */
		  ltfsmsg(LTFS_WARN, 20078W, status);

	   } else {
		  ltfsmsg(LTFS_DEBUG, 20080D, fname);
	   }
	}

	ltfsmsg(LTFS_INFO, 20096I);
	return status;
}

/**
 * Read buffer
 * @param device a pointer to the ltotape backend
 * @param id
 * @param buf
 * @param offset
 * @param len
 * @param type
 * @return 0 on success or negative value on error
 */
int ltotape_readbuffer(void *device, int id, unsigned char *buf, size_t offset, size_t len,
					   int type)
{
	ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*) device;
	int           status;

	/* Prepare Data Buffer */
	sio->data_length = len;
	sio->data = (unsigned char *) buf;
	memset(sio->data, 0, sio->data_length);

	/* Prepare CDB */
	sio->cdb_length = 10;
	sio->cdb[0] = CMDread_buffer;			/* SCSI ReadBuffer(10) Code */
	sio->cdb[1] = type;
	sio->cdb[2] = id;
	sio->cdb[3] = (unsigned char) (offset >> 16);
	sio->cdb[4] = (unsigned char) (offset >> 8);
	sio->cdb[5] = (unsigned char) (offset & 0xFF);
	sio->cdb[6] = (unsigned char) (len >> 16);
	sio->cdb[7] = (unsigned char) (len >> 8);
	sio->cdb[8] = (unsigned char) (len & 0xFF);
	sio->cdb[9] = 0x00;

	sio->data_direction = HOST_READ;
	/*
	 * Set the timeout then execute:
	 */
	sio->timeout_ms = LTO_DEFAULT_TIMEOUT;

	status = ltotape_scsiexec(sio);

	return status;
}


/****************************************************************************
 * Instruct drive to generate a log snapshot
 * @param device a pointer to the ltotape backend tape device
 * @return 0 on success or negative value on error
 */
static int ltotape_snapshot_now (void* device)
{
	ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;

/*
 * Set up the cdb:
 */
	sio->cdb[0]  = CMDmaintenance_out;
	sio->cdb[1]  = 0x1F;   /* Service Action = vendor-specific  */
	sio->cdb[2]  = 0x0C;   /* Service Action Qualifier          */
	sio->cdb[3]  = 0;
	sio->cdb[4]  = 0;
	sio->cdb[5]  = 0;
	sio->cdb[6]  = 0;
	sio->cdb[7]  = 0;
	sio->cdb[8]  = 0;
	sio->cdb[9]  = 0;
	sio->cdb[10] = 0;
	sio->cdb[11] = 0;

	sio->cdb_length = 12;		/* Twelve-byte cdb */

/*
 * Set up the data part:
 */
	sio->data = (unsigned char *) NULL;
	sio->data_length = 0;
	sio->data_direction = NO_TRANSFER;

/*
 * Set the timeout then execute:
 */
	sio->timeout_ms = LTO_DEFAULT_TIMEOUT;
	return ltotape_scsiexec (sio);
}

/**
 * Take drive dump
 * @param device a pointer to the ltotape backend
 * @param fname a file name of dump
 * @return 0 on success or negative value on error
 */
#define DUMP_HEADER_SIZE   (4)
#ifdef __NetBSD__
#define DUMP_TRANSFER_SIZE MAXPHYS
#else
#define DUMP_TRANSFER_SIZE (512 * KB)
#endif

static int ltotape_read_drivedump(void *device, const char *fname)
{
	long long data_length, buf_offset;
	FILE* dumpfd;
	int transfer_size, num_transfers, excess_transfer;
	int rc = 0;
	int i, bytes;
	int buf_id;
	unsigned char cap_buf[DUMP_HEADER_SIZE];
	unsigned char *dump_buf;
	bool updated_header = FALSE;
	time_t               now;
	int                  j;

#ifdef HPE_BUILD
	const char*          lsn = "HPE LTFS                         ";
#elif defined QUANTUM_BUILD
	const char*          lsn = "Quantum LTFS                    ";
#elif defined GENERIC_OEM_BUILD
	const char*          lsn = "Generic LTFS                    ";
#endif


	/* Set transfer size */
	transfer_size = DUMP_TRANSFER_SIZE;
	dump_buf = (unsigned char*)calloc(1, DUMP_TRANSFER_SIZE);
	if (dump_buf == (unsigned char*)NULL) {
		ltfsmsg(LTFS_ERR, 10001E, "drive log snapshot");
		rc = -3;
		return rc;
	}

	/* Set buffer ID */
	//buf_id = 0x00;

	buf_id = 0x01;

	memset(cap_buf, 0, sizeof(cap_buf));
	/* Get buffer capacity */
	rc = ltotape_readbuffer(device, buf_id, cap_buf, 0, sizeof(cap_buf), 0x03);
	if (rc) {
		free(dump_buf);
		return rc;
	}
	data_length = (cap_buf[1] << 16) + (cap_buf[2] << 8) + (int) cap_buf[3];

	/* Open dump file for write and append mode only */
	dumpfd = (FILE*) fopen(fname, "ab");
	if (dumpfd == (FILE*) NULL) {
		rc = -2;
		free(dump_buf);
		ltfsmsg(LTFS_WARN, 20090W, fname, strerror(errno));
		return rc;
	}

	/* get the total number of transfers */
	num_transfers = data_length / transfer_size;
	excess_transfer = data_length % transfer_size;
	if (excess_transfer)
		num_transfers += 1;

	/* start to transfer data */
	buf_offset = 0;
	i = 0;
	while (num_transfers) {
		int length;

		i++;

		/* Allocation Length is transfer_size or excess_transfer */
		if (excess_transfer && num_transfers == 1)
			length = excess_transfer;
		else
			length = transfer_size;
		memset(dump_buf, 0, DUMP_TRANSFER_SIZE);
		rc = ltotape_readbuffer(device, buf_id, dump_buf, buf_offset, DUMP_TRANSFER_SIZE,
				0x02);
		if (rc) {
			free(dump_buf);
			fclose(dumpfd);
			return rc;
		}

		/* Update the header */
		if (updated_header == FALSE) {
			dump_buf [ LTOTAPE_TIMESTAMP_TYPE_OFFSET   ] = 0x00;
			dump_buf [ LTOTAPE_TIMESTAMP_TYPE_OFFSET+1 ] = 0x02; /* type2=UTC */

			time (&now);
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET   ] = 0x00;
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+1 ] = 0x00;
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+2 ] = 0x00;
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+3 ] = 0x00;
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+4 ] = (unsigned char)(((int)now) >> 24);
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+5 ] = (unsigned char)(((int)now) >> 16);
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+6 ] = (unsigned char)(((int)now) >>  8);
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+7 ] = (unsigned char)(((int)now)      );

			for (j = 0; j < LTOTAPE_LIBSN_LENGTH; j++) {
				dump_buf [ LTOTAPE_LIBSN_OFFSET+j ] = (unsigned char) *(lsn+j);
			}
			updated_header = TRUE;
		}

		/* write buffer data into dump file */
		bytes = fwrite (dump_buf, sizeof(unsigned char), length, dumpfd);
		if (bytes == -1) {
			rc = -2;
			free(dump_buf);
			fclose(dumpfd);
			return rc;
		}

		if (bytes != length) {
			ltfsmsg(LTFS_WARN, 20081W, bytes, length);
			free(dump_buf);
			fclose(dumpfd);
			rc = -2;
			return rc;
		}

		/* update offset and num_transfers, free buffer */
		buf_offset += transfer_size;
		num_transfers -= 1;

	}							/* end of while(num_transfers) */

	free(dump_buf);
	if (fclose(dumpfd) != 0) {
		ltfsmsg(LTFS_WARN, 20082W, fname);
		rc = -2;
	}


	return rc;
}

/**
 * Get the buffer size
 * @param buff_id, buffer id
 * @param buffer, buffer
 * @return 0 or size of buffer on success or negative value on error
 */
static long long ltotape_get_buffer_size(int buff_id, unsigned char *buffer)
{
	long long data_length, buf_offset, size, temp_size;
    bool not_found = TRUE;

    size = -1;
    buf_offset = 32;
    temp_size = 0;

    data_length = (buffer[30] << 8) + (int) buffer[31];

    while(not_found && (temp_size < data_length)) {
    	if(buff_id == buffer[buf_offset]) {
    		not_found = FALSE;
    		size = (buffer[buf_offset+4] << 24) + (buffer[buf_offset+5] << 16) + (buffer[buf_offset+6] << 8) + (int) buffer[buf_offset+7];
    	}else {
    		buf_offset = buf_offset + 8;
    		temp_size = temp_size + 8;
    	}
    }

	return size;
}

/**
 * Take mini drive dump
 * @param device a pointer to the ltotape backend
 * @param fname a file name of dump
 * @return 0 on success or negative value on error
 */
#define MINI_DUMP_HEADER_SIZE   (256)
#ifdef __NetBSD__
#define MINI_DUMP_TRANSFER_SIZE MAXPHYS
#else
#define MINI_DUMP_TRANSFER_SIZE (256 * KB)
#endif

static int ltotape_read_mini_drivedump(void *device, const char *fname)
{
	long long data_length, buf_offset;
	FILE* dumpfd;
	int transfer_size, num_transfers, excess_transfer;
	int rc = 0;
	int i, bytes;
	int buf_id;
	unsigned char cap_buf[MINI_DUMP_HEADER_SIZE];
	unsigned char *dump_buf;
	bool updated_header = FALSE;
	time_t               now;
	int                  j;

#ifdef HPE_BUILD
	const char*          lsn = "HPE LTFS                         ";
#elif defined QUANTUM_BUILD
	const char*          lsn = "Quantum LTFS                    ";
#elif defined GENERIC_OEM_BUILD
	const char*          lsn = "Generic LTFS                    ";
#endif


	/* Set transfer size */
	transfer_size = MINI_DUMP_TRANSFER_SIZE;
	dump_buf = (unsigned char*)calloc(1, MINI_DUMP_TRANSFER_SIZE);
	if (dump_buf == (unsigned char*)NULL) {
		ltfsmsg(LTFS_ERR, 10001E, "drive log snapshot");
		rc = -3;
		return rc;
	}

	/* Set buffer ID */
	//buf_id = 0x00;
	buf_id = 0x02;

	memset(cap_buf, 0, sizeof(cap_buf));
	/* Get buffer capacity */
	rc = ltotape_readbuffer(device, buf_id, cap_buf, 0, sizeof(cap_buf), 0x1C);
	if (rc) {
		free(dump_buf);
		return rc;
	}

	/* Get the size of the buffer */
	data_length =  ltotape_get_buffer_size(0x11, cap_buf);
	if (data_length <= 0) {
		free(dump_buf);
		return -1;
	}

	/* Open dump file for write and append mode only */
	dumpfd = (FILE*) fopen(fname, "ab");
	if (dumpfd == (FILE*) NULL) {
		rc = -2;
		free(dump_buf);
		ltfsmsg(LTFS_WARN, 20090W, fname, strerror(errno));
		return rc;
	}

	/* get the total number of transfers */
	num_transfers = data_length / transfer_size;
	excess_transfer = data_length % transfer_size;
	if (excess_transfer)
		num_transfers += 1;

	/* start to transfer data */
	buf_offset = 0;
	i = 0;
	while (num_transfers) {
		int length;

		i++;

		/* Allocation Length is transfer_size or excess_transfer */
		if (excess_transfer && num_transfers == 1)
			length = excess_transfer;
		else
			length = transfer_size;

		rc = ltotape_readbuffer(device, 0x11, dump_buf, buf_offset, MINI_DUMP_TRANSFER_SIZE,
				0x1C);
		if (rc) {
			free(dump_buf);
			fclose(dumpfd);
			return rc;
		}

		/* Update the header */
		if (updated_header == FALSE) {
			dump_buf [ LTOTAPE_TIMESTAMP_TYPE_OFFSET   ] = 0x00;
			dump_buf [ LTOTAPE_TIMESTAMP_TYPE_OFFSET+1 ] = 0x02; /* type2=UTC */

			time (&now);
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET   ] = 0x00;
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+1 ] = 0x00;
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+2 ] = 0x00;
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+3 ] = 0x00;
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+4 ] = (unsigned char)(((int)now) >> 24);
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+5 ] = (unsigned char)(((int)now) >> 16);
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+6 ] = (unsigned char)(((int)now) >>  8);
			dump_buf [ LTOTAPE_TIMESTAMP_OFFSET+7 ] = (unsigned char)(((int)now)      );

			for (j = 0; j < LTOTAPE_LIBSN_LENGTH; j++) {
				dump_buf [ LTOTAPE_LIBSN_OFFSET+j ] = (unsigned char) *(lsn+j);
			}
			updated_header = TRUE;
		}

		/* write buffer data into dump file */
		bytes = fwrite (dump_buf, sizeof(unsigned char), length, dumpfd);
		if (bytes == -1) {
			rc = -2;
			free(dump_buf);
			fclose(dumpfd);
			return rc;
		}

		if (bytes != length) {
			ltfsmsg(LTFS_WARN, 20081W, bytes, length);
			free(dump_buf);
			fclose(dumpfd);
			rc = -2;
			return rc;
		}

		/* update offset and num_transfers, free buffer */
		buf_offset += transfer_size;
		num_transfers -= 1;

	}							/* end of while(num_transfers) */

	free(dump_buf);
	if (fclose(dumpfd) != 0) {
		ltfsmsg(LTFS_WARN, 20082W, fname);
		rc = -2;
	}


	return rc;
}

#define SENDDIAG_BUF_LEN (8)
/****************************************************************************
 * Instruct drive to generate a log snapshot
 * @param device a pointer to the ltotape backend tape device
 * @param diagid diagnostic id
 * @return 0 on success or negative value on error
 */
static int ltotape_snapshot_dump(void* device, int diagid) {
	ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*) device;

	unsigned char buf[SENDDIAG_BUF_LEN];
	unsigned char sense[MAXSENSE];
	int           status;
	char *sense_string = NULL;

	memset(sense, 0, sizeof(sense));


	/* Prepare Data Buffer */
	sio->data_length = SENDDIAG_BUF_LEN;
	sio->data = buf;
	memset(sio->data, 0, sio->data_length);

	/*
	 * Set up the cdb:
	 */
	sio->cdb_length = 6;
	sio->cdb[0] = CMDsend_diagnostic; /* SCSI Send Diag Code */
	sio->cdb[1] = 0x11; /* PF bit is set to 1 */
	sio->cdb[2] = 0x00;
	sio->cdb[3] = 0x00;
	sio->cdb[4] = 0x08; /* parameter length is 0x0008 */
	sio->cdb[5] = 0x00;

	/* Prepare payload */
	sio->data[0] = 0x80; /* page code */
	sio->data[2] = 0x00;
	sio->data[3] = 0x04; /* page length */
	sio->data[4] = 0x01;
	sio->data[5] = diagid; /* diagnostic id */

	sio->data_direction = HOST_WRITE;

	sense_string = ltotape_printbytes(sio->data, sio->data_length);
	ltfsmsg(LTFS_DEBUG, 20010D, sense_string, sio->data_length);
	if (sense_string != (char *) NULL)
		free(sense_string);

	/*
	 * Set the timeout then execute:
	 */
	sio->timeout_ms = LTO_DEFAULT_TIMEOUT;
	status = ltotape_scsiexec(sio);

	return status;
}

/****************************************************************************
 * Retrieve and store a drive log snapshot
 * @param device a pointer to the ltotape backend tape device
 * @param fname name of the file to be used for storing the snapshot
 * @return 0 on success or negative value on error
 */
static int ltotape_read_snapshot (void* device, char* fname)
{
	ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;
	int                  status;
	unsigned char       *snapshot;
	int                  datalen;
	int                  writelength;
	int                  iteration;
	int                  j;
	FILE*                fp;
	time_t               now;
#ifdef HPE_BUILD
	const char*          lsn = "HPE LTFS                         ";
#elif defined QUANTUM_BUILD
	const char*          lsn = "Quantum LTFS                    ";
#elif defined GENERIC_OEM_BUILD
	const char*          lsn = "Generic LTFS                    ";
#endif

/*
 * Try to get some memory for the snapshot:
 */
	datalen = SNAPSHOT_LENGTH;
	snapshot = (unsigned char*)calloc (1, datalen);
	if (snapshot == (unsigned char*)NULL) {
	   ltfsmsg(LTFS_ERR, 10001E, "drive log snapshot");
	   return -3;
	}

/*
 * Set up the cdb:
 */
	sio->cdb[0]  = CMDmaintenance_in;
	sio->cdb[1]  = 0x1F;   /* Service Action = vendor-specific  */
	sio->cdb[2]  = 0x08;   /* Service Action Qualifier          */
	sio->cdb[3]  = 0;
	sio->cdb[4]  = 0;
	sio->cdb[5]  = 0;
	sio->cdb[6]  = (unsigned char)(datalen >> 16 );
	sio->cdb[7]  = (unsigned char)(datalen >> 8  );
	sio->cdb[8]  = (unsigned char)(datalen & 0xFF);
	sio->cdb[9]  = 0;
	sio->cdb[10] = 0;
	sio->cdb[11] = 0;

	sio->cdb_length = 12;		/* Twelve-byte cdb */

/*
 * Set up the data part:
 */
	sio->data = snapshot;
	sio->data_length = datalen;
	sio->data_direction = HOST_READ;

/*
 * Set the timeout then execute:
 */
	sio->timeout_ms = LTO_DEFAULT_TIMEOUT;
	iteration = 0;
	do {

		status = ltotape_scsiexec (sio);

/*
 * Sense key NO_SENSE and ASC/Q 0016h means the log is still being created, 
 *  so wait a second and then retry..
 */
	   if (status == -1) {
		  if (((sio->sensedata[2] & 0x0F) == 0x00) && (sio->sensedata[12] == 0x00) && (sio->sensedata[13] == 0x16)) {
			  ltfsmsg(LTFS_DEBUG, 20018D);
			  sleep (1);
			  iteration++;
/*
 * Any other reason for failing means there's a problem so force the loop to finish...
 */
		  } else {
			  iteration = 9999;
		  }
	   }

	} while ((status != 0) && (iteration < MAX_SNAPSHOT_RETRIES));

/*
 * If we successfully retrieved a log, add a few local fields and then try
 *   to store it in the specified file location:
 */
	if (status == 0) {
		snapshot [ LTOTAPE_TIMESTAMP_TYPE_OFFSET   ] = 0x00;
		snapshot [ LTOTAPE_TIMESTAMP_TYPE_OFFSET+1 ] = 0x02; /* type2=UTC */

		time (&now);
		snapshot [ LTOTAPE_TIMESTAMP_OFFSET   ] = 0x00;
		snapshot [ LTOTAPE_TIMESTAMP_OFFSET+1 ] = 0x00;
		snapshot [ LTOTAPE_TIMESTAMP_OFFSET+2 ] = 0x00;
		snapshot [ LTOTAPE_TIMESTAMP_OFFSET+3 ] = 0x00;
		snapshot [ LTOTAPE_TIMESTAMP_OFFSET+4 ] = (unsigned char)(((int)now) >> 24);
		snapshot [ LTOTAPE_TIMESTAMP_OFFSET+5 ] = (unsigned char)(((int)now) >> 16);
		snapshot [ LTOTAPE_TIMESTAMP_OFFSET+6 ] = (unsigned char)(((int)now) >>  8);
		snapshot [ LTOTAPE_TIMESTAMP_OFFSET+7 ] = (unsigned char)(((int)now)      );

		for (j = 0; j < LTOTAPE_LIBSN_LENGTH; j++) {
		  snapshot [ LTOTAPE_LIBSN_OFFSET+j ] = (unsigned char) *(lsn+j);
		}

		fp = (FILE*)fopen (fname, "wb");

		if (fp == (FILE*)NULL) {
		   ltfsmsg(LTFS_WARN, 20090W, fname, strerror(errno));
		   status = -2;

		} else {
		   writelength = fwrite (snapshot, sizeof(unsigned char), sio->actual_data_length, fp);

		   if (writelength != sio->actual_data_length) {
			   ltfsmsg(LTFS_WARN, 20081W, writelength, sio->actual_data_length);
			   status = -2;
		   }

		   if (fclose (fp) != 0) {
			   ltfsmsg(LTFS_WARN, 20082W, fname);
			   status = -2;
		   }
		}
	}
	
	free (snapshot);
	return (status);
}

/****************************************************************************
 * See whether there are too many logs for this drive, and if so then
 *  delete the oldest..  Uses the (file-scoped) dirname global variable to
 *  select the directory to be checked.
 * @param serialno - the drive serial number
 * @return number of logs remaining, or -1 on error
 */
static int ltotape_trim_logs (char* serialno)
{
  struct dirent **logfiles;
  int             numlogs;
  int             i;
  char            finishedfile[1024];

/*
 * Have to make a copy of the serial number, because we can't pass arbitrary
 *  parameters to the select routine...  Would need mutex protection if wanted
 *  to be multi-thread safe..
 */
  strcpy (drivesn, serialno);

/*
 * Find the oldest logfile for this drive:
 */
  numlogs = scandir (dirname, &logfiles, ltotape_select_logfiles, ltotape_sort_oldest);
  if (numlogs < 0) {
    ltfsmsg(LTFS_INFO, 20091I, "directory", dirname, strerror(errno));
    return -1;

  } else {
    if (numlogs > 0) {
      ltfsmsg(LTFS_DEBUG, 20092D, numlogs, logfiles[0]->d_name);
      for (i = 0; i <= (numlogs - MAX_RETAINED_SNAPSHOTS); i++) {
        sprintf (finishedfile, "%s/%s", dirname, logfiles[i]->d_name);
        if (unlink (finishedfile) != 0) {
	  ltfsmsg(LTFS_ERR, 20093E, finishedfile, strerror(errno));
        } else {
	  ltfsmsg(LTFS_DEBUG, 20094D, finishedfile);
        }
      }
    }
/* 
 * Free all the pointers then the struct itself:
 */
    for (i = 0; i < numlogs; ++i) {
      free (logfiles[i]);
    }
    free (logfiles);
  }

  return numlogs;
}

/****************************************************************************
 * A file selector for use with scandir, chooses only files which are 
 *  LTFS log files for the current drive.
 * @param entry - a particular entry in the directory being scanned
 * @return 1 if file should be included, 0 if it should be excluded
 */
static int ltotape_select_logfiles (const struct dirent *entry)
{
  if ((strstr (entry->d_name, "ltfs_") != NULL) &&
      (strstr (entry->d_name, drivesn) != NULL)) {
      return 1;

  } else {
    return 0;
  }
}

/****************************************************************************
 * A sorting routine for use with scandir, works out which of the two entries
 *  is the oldest (earliest modification time).
 * @param pA - first entry to be compared
 * @param pB - second entry to be compared
 * @return 1 if A is newer, -1 if B is newer, 0 if happen to be same age
 */
static int ltotape_sort_oldest (const struct dirent ** pA, const struct dirent ** pB)
{
	time_t tA, tB;
	struct stat filstat;
	char path[1024];

	sprintf(path, "%s/%s", dirname, (*pA)->d_name);
	if (stat(path, &filstat) != 0) {
		ltfsmsg(LTFS_INFO, 20091I, "file", path, strerror(errno));
		tA = (time_t) NULL;
        } else {
		tA = filstat.st_mtime;
        }
    
	sprintf(path, "%s/%s", dirname, (*pB)->d_name);
	if (stat(path, &filstat) != 0) {
		ltfsmsg(LTFS_INFO, 20091I, "file", path, strerror(errno));
		tB = (time_t) NULL;
        } else {
		tB = filstat.st_mtime;
        }

        if (tA > tB) {
		return 1;
        } else if (tA < tB) {
		return -1;
        } else {
		return 0;
        }
}
