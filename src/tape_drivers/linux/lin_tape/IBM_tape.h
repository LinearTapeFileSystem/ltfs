/*******************************************************************************
 *   IBM_tape.h
 *
 *  Copyright 2001, 2017 IBM Corp.
 *
 *  Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *  documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

#ifndef IBM_TAPE_H
#define IBM_TAPE_H

/* tape device id */
#define IBM_3580    1
#define IBM_3590    2
#define IBM_3592    3

/* changer device id */
#define IBM_3581    7
#define IBM_3582    8
#define IBM_3583    9
#define IBM_3584   10
#define BDT_3581   11
#define IBM_3576   12
#define IBM_3573   13
#define IBM_3577   14
#define IBM_3572   15

/*******************************************************************************
* IOCTL Structures                                                             *
*******************************************************************************/
#ifndef unchar
#define unchar unsigned char
#endif

#ifndef boolean
#define boolean unchar
#endif

#ifndef BYTE
#define BYTE unchar
#endif

/*******************************************************************************
* SCSI_PASS_THROUGH pass through function                                      *
*******************************************************************************/

#define SCSI_PASS_THROUGH _IOWR('P',0x01,SCSIPassThrough) /* Pass Through  */

typedef struct _SCSIPassThrough {
	unchar   CDB[12];                           /* Command Data Block    */
	unchar   CommandLength;                     /* Command Length        */
	unchar * Buffer ;                           /* Command Buffer        */
	ulong    BufferLength;                      /* Buffer Length         */
	unchar   DataDirection;                     /* Transfer Direction    */
	ushort   TimeOut;                           /* Time Out Value        */
	unchar   TargetStatus;                      /* Target Status         */
	unchar   MessageStatus;                     /* Msg from host adapter */
	unchar   HostStatus;                        /* Host status           */
	unchar   DriverStatus;                      /* Driver status         */
	unchar   SenseDataValid;                    /* Sense Data Valid      */
	unchar   ASC;                               /* ASC if SenseDataValid */
	unchar   ASCQ;                              /* ASCQ if SenseDataVld  */
	unchar   SenseKey;                          /* SK if SenseDataValid  */
} SCSIPassThrough, *PSCSIPassThrough;

#define SCSI_DATA_OUT  1
#define SCSI_DATA_IN   2

#ifndef SCSI_DATA_NONE
#define SCSI_DATA_NONE 3
#endif

/*******************************************************************************
* SCSI Tape Operations                                                         *
* STOFFL, STREW, STERASE, STWEOF, STFSF, STRSF, STFSR and STRSR perform as     *
* defined in the AIX manuals. For IBM tape products STRETEN is a NOP           *
* since they automatically perform retension on their own.                     *
*                                                                              *
* STTUR issues a SCSI Test Unit Ready to device.                               *
* STLOAD issues a SCSI LOAD command                                            *
* STSEOD issues a space forward to EOD command. After this command the         *
*   tape is positioned to begin writing.                                       *
* STFSSF issues a forward space sequential filemarks. Count is equal to        *
*   the number of contiguous fiemarks to search for.                           *
* STRSSF issues a reverse space sequential filemarks. Count is equal to        *
*   the number of contiguous fiemarks to search for.                           *
* IMPORTANT NOTE:                                                              *
* Any command that causes tape position to reverse. Will cause the close       *
* entry point to not write file-marks. If you must position the tape           *
* after writing data and before closing then you can manually write            *
* file-marks with STWEOF.                                                      *
*******************************************************************************/

#define STIOCTOP    _IOWR('P',0x02, struct stop)          /* tape commands */

struct stop {
	short   st_op;                            /* operations defined below */
	daddr_t st_count;                         /* Counter value            */
};

#define STOFFL  5                      /* rewind and unload tape              */
#define STREW   6                      /* rewind                              */
#define STERASE 7                      /* erase tape, leave at load point     */
#define STRETEN 8                      /* retension tape, leave at load point */
#define STWEOF  10                     /* write an end-of-file record         */
#define STFSF   11                     /* forward space file                  */
#define STRSF   12                     /* reverse space file                  */
#define STFSR   13                     /* forward space record                */
#define STRSR   14                     /* reverse space record                */
#define STINSRT 15                     /* load the tape                       */
#define STEJECT 16                     /* unload the tape                     */

/* additional st_op's */
#define STTUR   30                     /* test unit ready                     */
#define STLOAD  31                     /* load tape                           */
#define STSEOD  32                     /* space to end of data                */
#define STFSSF  33                     /* forward space sequential file marks */
#define STRSSF  34                     /* reverse space sequential file marks */

#define STIOCHGP _IOW('P',0x03, struct stchgp)    /* change drive parameters  */
struct  stchgp  {
	unchar  st_ecc;                              /* reserved              */
	int     st_blksize;                          /* change blocksize      */
};

/*******************************************************************************
* QUERY_DRIVER_VERSION returns the IBMtape driver version                      *
*******************************************************************************/

#define QUERY_DRIVER_VERSION \
	_IOR('P',0x04, struct QueryDriverVersion)  /* version */
struct QueryDriverVersion {
	uint MajorRelease;                            /* Major release number */
	uint MinorRelease;                            /* Minor release number */
	uint IncrementRelease;                        /* Incrs release num    */
};

/*******************************************************************************
* STIOCSETP changes device driver parameters                                   *
*   The structure "stchgp" is used to set the current values.                  *
* STIOCQRYP queries device driver parameters                                   *
*   The structure "stchgp" is filled in with the current values.               *
*******************************************************************************/

#define STIOCSETP _IOW('z',0x30,struct stchgp_s)         /* change parameters */
#define STIOCQRYP _IOR('z',0x31,struct stchgp_s)         /* query  parameters */

struct stchgp_s {
	int blksize;                      /* new block size                   */
	boolean trace;                    /* TRUE = message trace on          */
	uint hkwrd;                       /* trace hook word                  */
	int sync_count;                   /* obsolete - not used              */
	boolean autoload;                 /* on/off autoload feature          */
	boolean buffered_mode;            /* on/off buffered mode             */
	boolean compression;              /* on/off compression               */
	boolean trailer_labels;           /* on/off allow writing after EOM   */
	boolean rewind_immediate;         /* on/off immediate rewinds         */
	boolean bus_domination;           /* obsolete - not used              */
	boolean logging;                  /* enable or disable volume logging */
	boolean write_protect;            /* write_protected media            */
	uint min_blksize;                 /* minimum block size               */
	uint max_blksize;                 /* maximum block size               */
	uint max_scsi_xfer;               /* maximum scsi tranfer len         */
	char volid[16];                   /* volume id                        */
	unchar acf_mode;                  /* automatic cartridge facility mode*/
#define ACF_NONE             0
#define ACF_MANUAL           1
#define ACF_SYSTEM           2
#define ACF_AUTOMATIC        3
#define ACF_ACCUMULATE       4
#define ACF_RANDOM           5
	unchar record_space_mode;                     /* fsr/bsr space mode   */
#define SCSI_SPACE_MODE      1
#define AIX_SPACE_MODE       2
	unchar logical_write_protect;                 /* logical write protect*/
#define NO_PROTECT           0
#define ASSOCIATED_PROTECT   1
#define PERSISTENT_PROTECT   2
#define WORM_PROTECT         3
	unchar capacity_scaling;                      /* capacity scaling     */
#define SCALE_100            0
#define SCALE_75             1
#define SCALE_50             2
#define SCALE_25             3
#define SCALE_VALUE          4
	unchar retain_reservation;       /* retain reservation                */
	unchar alt_pathing;              /* alternate pathing active          */
	boolean emulate_autoloader;      /* emulate autoloader in random mode */
	unchar medium_type;              /* tape medium type                  */
	unchar density_code;             /* tape density code                 */
	boolean disable_sim_logging;     /* disable sim/mim error logging     */
	boolean read_sili_bit;           /* SILI bit setting for read commands*/
	unchar read_past_filemark;       /* fixed block read pass the filemark*/
	boolean disable_auto_drive_dump; /* disable auto drive dump logging   */
	unchar capacity_scaling_value;   /* hex value of capacity scaling     */
	boolean wfm_immediate;           /* buffer write file mark            */
	boolean limit_read_recov;        /* limit read recovery to 5 seconds  */
	boolean limit_write_recov;       /* limit write recovery to 5 seconds */
	boolean data_safe_mode;          /* turn data safe mode on/off        */
	unchar pews[2];                  /* programmable early warn zone size */
	unchar busy_retry;			     /* if set a retry will be tried on busy */
	unchar reserved[12];
};

/*******************************************************************************
* This has no arguments. The tape buffers are flushed to tape.                 *
*******************************************************************************/

#define STIOCSYNC _IO('z',0x37)           /* synchronize buffers with tape */

/*******************************************************************************
* STIOCDM displays a message on the 3490 display.                              *
*   dm_func is a flag that is built by oring the defines below.                *
*******************************************************************************/

#define STIOCDM _IOW('z',0x32,struct stdm_s)               /* display message */
#define MAXMSGLEN     8

struct stdm_s {
	char dm_func;                                      /* function code   */

/*Function Selection*/
#define DMSTATUSMSG 0x00    /* General Status Msg                             */
#define DMDVMSG     0x20    /* Demount/Verify Message                         */
#define DMMIMMED    0x40    /* Mount With Immediate Action indicator          */
#define DMDEMIMMED  0xE0    /* Demount/Mount With Immediate Action            */

/*Message Control*/
#define DMMSG0      0x00                /* display msg 0                      */
#define DMMSG1      0x04                /* display msg 1                      */
#define DMFLASHMSG0 0x08                /* Flash msg 0                        */
#define DMFLASHMSG1 0x0C                /* flash msg 1                        */
#define DMALTERNATE 0x10                /* alternate msg 0 and msg 1          */

	char dm_msg0[MAXMSGLEN];        /* message 1                          */
char dm_msg1[MAXMSGLEN];                /* message 2                          */
};

/*******************************************************************************
* STIOCQRYPOS is used to query the position of the tape device on the tape.    *
*   "block_type" indicates whether a logical position is wanted or             *
*   whether a vendor specific position is wanted. The vendor specific          *
*   position is a composite number that is fed into an STIOCSETPOS command     *
*   to provide a much faster method of positioning. Unless a SETPOS            *
*   operation is going to be used there is not much reason to use              *
*   QP_PHYSICAL.                                                               *
*   "eot" is true if the tape position is after early warning.                 *
*   "curpos" is the position where the next write or read would take place.    *
*   "lbot" is the number of the last block to be actually written from the     *
*   buffer onto the tape. If no blocks have been written to tape, ie, all      *
*   are still in the buffer then lbot=NONE                                     *
*   NOTE: lbot is only valid immediately following a write command. Any        *
*   tape positioning will cause this to become invalid.                        *
* STIOCSETPOS is used to position the tape directly to a particular block.     *
*   Only "block_type" and "curpos" are used. All other fields are undefined    *
*******************************************************************************/

#define STIOCQRYPOS _IOWR('z',0x33,struct stpos_s)     /* query tape position */
#define STIOCSETPOS _IOWR('z',0x34,struct stpos_s)     /* set tape position   */
typedef unsigned int blockid_t;
struct stpos_s {
	char block_type;                  /* format of block id information   */
#define QP_LOGICAL  0                     /* scsi logical blockid format      */
#define QP_PHYSICAL 1                     /* vendor specific blockid format   */
	boolean eot;                      /* leading end of partition         */
	blockid_t curpos;                 /* for qry/set pos                  */
	blockid_t lbot;                   /* last block written to tape       */
#define LBOT_NONE    0xFFFFFFFF           /* no blocks have been written      */
#define LBOT_UNKNOWN 0xFFFFFFFE           /* unable to determine information  */
	uint num_blocks;                  /* number of blocks in buffer       */
	uint num_bytes;                   /* number of bytes in buffer        */
	boolean bot;                      /* beginning of partition           */
	unchar partition_number;          /* current partition number on tape */
	unchar reserved1[2];
	blockid_t tapepos;                /* next block to be transferred     */
	unchar reserved2[48];
};

/*******************************************************************************
* STIOCQRYSENSE returns raw sense data from device.                            *
*******************************************************************************/

#define MAXSENSE        255
#define STIOCQRYSENSE _IOWR('z',0x35,struct stsense_s)   /* get sense data */

struct stsense_s {
	/*INPUT*/
	char sense_type;        /* fresh(new sense) or error(last error) data */

#define FRESH     1             /* initiate a new sense command               */
#define LASTERROR 2             /* return sense from last scsi sense command  */

	/*OUTPUT*/
	unchar sense[MAXSENSE];  /* actual sense data                         */
	int len;                 /* length of valid sense data returned       */
	int residual_count;      /* residual count from last operation        */
	unchar reserved[60];
};

#define MAX_INQ_LEN 255            /* max data xfer for inquiry page ioctl */

/*******************************************************************************
* STIOCINQUIRY returns raw inquiry data from device.                           *
*******************************************************************************/

#define STIOCQRYINQUIRY _IOWR('z',0x36,struct st_inquiry)   /* get inquiry */
struct inq_data_s {
	BYTE b0;
/*macros for accessing fields of byte 0 */
#define PERIPHERAL_QUALIFIER(x)   ((x->b0 & 0xE0)>>5)
#define PERIPHERAL_CONNECTED          0x00
#define PERIPHERAL_NOT_CONNECTED      0x01
#define LUN_NOT_SUPPORTED             0x03
#define PERIPHERAL_DEVICE_TYPE(x) (x->b0 & 0x1F)
#define DIRECT_ACCESS                 0x00
#define SEQUENTIAL_DEVICE             0x01
#define PRINTER_DEVICE                0x02
#define PROCESSOR_DEVICE              0x03
#define CD_ROM_DEVICE                 0x05
#define OPTICAL_MEMORY_DEVICE         0x07
#define MEDIUM_CHANGER_DEVICE         0x08
#define UNKNOWN                       0x1F

	BYTE b1;
/*macros for accessing fields of byte 1 */
#define RMB(x) ((x->b1 & 0x80)>>7)                /*removable media bit  */
#define FIXED     0
#define REMOVABLE 1
#define device_type_qualifier(x) (x->b1 & 0x7F)   /* vendor specific     */

	BYTE b2;
/*macros for accessing fields of byte 2 */
#define ISO_Version(x)  ((x->b2 & 0xC0)>>6)
#define ECMA_Version(x) ((x->b2 & 0x38)>>3)
#define ANSI_Version(x) (x->b2 & 0x07)
#define NONSTANDARD     0
#define SCSI1           1
#define SCSI2           2
#define SCSI3           3

	BYTE b3;
/*macros for accessing fields of byte 3 */
#define AENC(x)    ((x->b3 & 0x80)>>7) /*asynchronous event notification */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define TrmIOP(x)  ((x->b3 & 0x40)>>6) /* support terminate I/O process?      */
#define Response_Data_Format(x)  (x->b3 & 0x0F)
#define SCSI1INQ      0          /* scsi1 standard inquiry data format        */
#define CCSINQ        1          /* CCS standard inquiry data format          */
#define SCSI2INQ      2          /* scsi2 standard inquiry data format        */

	BYTE additional_length;  /* bytes following this field minus 4        */
	BYTE res5;
	BYTE b6;
#define MChngr(x)    ((x->b6 & 0x08)>>3)

	BYTE b7;
/* macros for accessing fields of byte 7  */
/* the following fields are true or false */
#define RelAdr(x)    ((x->b7 & 0x80)>>7)
#define WBus32(x)    ((x->b7 & 0x40)>>6)
#define WBus16(x)    ((x->b7 & 0x20)>>5)
#define Sync(x)      ((x->b7 & 0x10)>>4)
#define Linked(x)    ((x->b7 & 0x08)>>3)
#define CmdQue(x)    ((x->b7 & 0x02)>>1)
#define SftRe(x)     (x->b7 & 0x01)

	char vendor_identification[8];
	char product_identification[16];
	char product_revision_level[4];
};

struct st_inquiry {
	struct inq_data_s standard;
	BYTE vendor_specific[MAX_INQ_LEN - sizeof(struct inq_data_s)];
};

/*******************************************************************************
* Log sense page ioctl data structure                                          *
*******************************************************************************/

#define LOGSENSEPAGE 1024        /* max data xfer for log sense page ioctl    */

struct log_sense_page {
	unchar page_code;
	unsigned short len;       /* log_page_header size + parameter length  */
	unsigned short parm_pointer;
	char data[LOGSENSEPAGE];
};

struct log_sense10_page {
	unchar page_code;
	unchar subpage_code;
	unchar reserved[2];
	unsigned short len;     /* [IN] specific allocation length for the data */
							/* [OUT] number of valid bytes in data          *
							 * (log_page_header size + parameter length)    */
	unsigned short parm_pointer;
	char data[LOGSENSEPAGE];
};

#define SIOC_LOG_SENSE10_PAGE _IOR('z', 0x51, struct log_sense10_page)

/*******************************************************************************
* Mode sense page ioctl data structure                                         *
*******************************************************************************/

#define MAX_MDSNS_LEN 255       /* max data xfer for mode sense page ioctl */
struct mode_sense_page {
	unchar page_code;
	char data[MAX_MDSNS_LEN];
};

struct mode_sense {
	unchar page_code;
	unchar subpage_code;
	unchar reserved[6];
	unchar cmd_code;
	char data[MAX_MDSNS_LEN];
};

/*******************************************************************************
* Inquiry page ioctl data structure                                            *
*******************************************************************************/

struct inquiry_page {
	unchar page_code;
	char data[MAX_INQ_LEN];
};

/*******************************************************************************
* Report Density Support ioctl data structures and defines                     *
*******************************************************************************/

#define ALL_MEDIA_DENSITY      0
#define CURRENT_MEDIA_DENSITY  1

#define MAX_DENSITY_REPORTS    8

struct density_report {
	unchar  primary_density_code;  /* primary density code                */
	unchar  secondary_density_code;/* secondary densuty code              */
	uint   wrtok : 1,              /* write ok, dvc can write this format */
		dup : 1,               /* zero if density only reported once  */
		deflt : 1,             /* current density is default format   */
		: 5;                   /* reserved                            */
	char   reserved[2];            /* reserved                            */
	uint   bits_per_mm : 24;       /* bits per mm                         */
	ushort media_width;            /* media width in millimeters          */
	ushort tracks;                 /* tracks                              */
	uint   capacity;               /* capacity in megabytes               */
	char   assigning_org[8];       /* assigning organization in ASCII     */
	char   density_name[8];        /* density name in ASCII               */
	char   description[20];        /* description in ASCII                */
};

struct report_density_support {
	unchar  media;           /* report all or current media as defd above */
	ushort number_reports;   /* num of density reports returned in array  */
	struct density_report reports[MAX_DENSITY_REPORTS];
};


/*******************************************************************************
* 3494 Library Device Number ioctl definition                                  *
*******************************************************************************/
#ifndef MTDEVICE
#define MTDEVICE               _IOR('m', 0x45, int)
#endif

#ifndef MTWEOFI /* included in kernel 3.x */
#define MTWEOFI (35)
#endif

/* Define iocinfo devtype field for medium changer devices */
#define DD_MEDIUM_CHANGER       'j'

/* Define extended parameters for openx, ioctlx and readx */
#define SC_NO_RESERVE     0x08          /* Don't reserve on open            */
#define SC_PASSTHRU       0x00010000    /* SC_DIAGNOSTIC w/o root authority */
#define SC_FEL            0x00020000    /* Force Error Logging on           */
#define SC_SERVICE        0x00040000    /* Service aid mode                 */
#define SC_NO_ERRORLOG    0x00080000    /* Disable AIX error logging        */
#define SC_TMCP           0x00100000    /* Tape Management Control Point    */
#define ATAPE_DIAGNOSTIC  SC_PASSTHRU
#define TAPE_READ_REVERSE 0x00010000    /* readx parm for SCSI Read Reverse */

/* 7332 DDS2 ioctls */
#define INIT_ELEMENT            0x07
#define MOVE_MEDIUM_LOAD        0xb4
#define MOVE_MEDIUM_UNLOAD      0xb5
#define READ_ELEMENT_INFO       0xb7
#define READ_ELEMENT_STATUS     0xb8

/* For locate, DDS2_LOCATE is opcode and arg is (uint) block number */
#define DDS2_LOCATE             0x2b

/* For read position, DDS2_READ_POS is opcode and arg is address of (unit)  */
/* to store current block position                                          */
#define DDS2_READ_POS                0x34

/* The load/unload medium ioctls default to the first empty or first full   */
/* slot when the argument value is 0.  In order to explicitely use slot 0   */
/* it is defined to be -1 passed as the argument.                           */
#define LOAD_UNLOAD_SLOT_0  ((ushort)-1)
#define SMCIOC_LOAD_MEDIUM      MOVE_MEDIUM_LOAD
#define SMCIOC_UNLOAD_MEDIUM    MOVE_MEDIUM_UNLOAD
#define STIOC_READ_POSITION     DDS2_READ_POS
#define STIOC_LOCATE            DDS2_LOCATE

#define SIOC_INQUIRY                  _IOR ('C',0x01,struct inquiry_data)
#define SIOC_REQSENSE                 _IOR ('C',0x02,struct request_sense)
#define SMCIOC_ELEMENT_INFO           _IOR ('C',0x03,struct element_info)
#define SMCIOC_MOVE_MEDIUM            _IOW ('C',0x04,struct move_medium)
#define SMCIOC_POS_TO_ELEM            _IOW ('C',0x05,struct pos_to_elem)
#define SMCIOC_INIT_ELEM_STAT         _IO  ('C',0x06)
#define SMCIOC_INVENTORY              _IOW ('C',0x07,struct inventory)
#define SIOC_RESERVE                  _IO  ('C',0x08)
#define SIOC_RELEASE                  _IO  ('C',0x09)
#define SIOC_TEST_UNIT_READY          _IO  ('C',0x0A)
// STIOC_LOG_SENSE no supported
//#define STIOC_LOG_SENSE              _IOR ('C',0x0B,struct log_sense)
#define SIOC_MODE_SENSE               _IOR ('C', 0x0D, struct mode_sense)
#define SIOC_MODE_SENSE_PAGE          _IOR ('C',0x0E,struct mode_sense_page)
#define SMCIOC_PREVENT_MEDIUM_REMOVAL _IO  ('C',0x0F)
#define SMCIOC_ALLOW_MEDIUM_REMOVAL   _IO  ('C',0x10)
#define STIOC_RESET_DRIVE             _IO  ('C',0x14)
#define SMCIOC_EXCHANGE_MEDIUM        _IOW ('C',0x19,struct exchange_medium)
#define SMCIOC_INIT_ELEM_STAT_RANGE   _IOW ('C',0x1C,struct element_range)
#define SIOC_INQUIRY_PAGE             _IOWR('C',0x1E,struct inquiry_page)
#define STIOC_REPORT_DENSITY_SUPPORT  \
	_IOWR('C',0x1F,struct report_density_support)
#define STIOC_PREVENT_MEDIUM_REMOVAL  _IO  ('C',0x20)
#define STIOC_ALLOW_MEDIUM_REMOVAL   _IO  ('C',0x21)
#define SMCIOC_READ_ELEMENT_DEVIDS    \
	_IOR ('C',0x22,struct read_element_devids)
#define SIOC_LOG_SENSE_PAGE           _IOR ('C',0x23,struct log_sense_page)
#define SMCIOC_READ_CARTRIDGE_LOCATION \
	_IOR('C',0x2A,struct read_cartridge_location)

/* failover ioctls for paths */
#define PRIMARY_SCSI_PATH    1
#define ALTERNATE_SCSI_PATH  2
#define MAX_SCSI_PATH       16

/* If no more than 2 paths, use SIOC_QUERY_PATH ioctl for the primary and first
*  alternate path info.  If more than 2 paths are configured, use the
*  SIOC_DEVICE_PATHS ioctl for path info
*/

#define SIOC_QUERY_PATH               _IOR ('C', 0x24,struct scsi_path)
#define SIOC_DEVICE_PATHS             _IOR ('C', 0x25,struct device_paths)
#define SIOC_ENABLE_PATH              _IO ('C', 0x26)
#define SIOC_DISABLE_PATH             _IO ('C', 0x27)

/******************************************************************************/
struct scsi_path {
	char primary_name[30];          /* primary logical device name        */
	char primary_parent[30];        /* primary SCSI parent name, hostname */
	unchar primary_id;              /* primary target address             */
	unchar primary_lun;             /* primary LUN                        */
	unchar primary_bus;             /* primary SCSI bus, "Channel" value  */
	unsigned long long primary_fcp_scsi_id;  /* primary FCP id            */
	unsigned long long primary_fcp_lun_id;   /* primary FCP LUN           */
	unsigned long long primary_fcp_ww_name;  /* primary FCP ww name       */
	unchar primary_enabled;         /* primary path enabled               */
	unchar primary_id_valid;        /* primary id/lun/bus fields valid    */
	unchar primary_fcp_id_valid;    /* primary FCP scsi/lun/id fields     */
	unchar alternate_configured;    /* alternate path configured          */
	char alternate_name[30];        /* alternate logical device name      */
	char alternate_parent[30];      /* alternate SCSI parent name         */
	unchar alternate_id;            /* alternate target address           */
	unchar alternate_lun;           /* alternate logical unit             */
	unchar alternate_bus;           /* alternate SCSI bus                 */
	unsigned long long alternate_fcp_scsi_id; /* alternate FCP SCSI id    */
	unsigned long long alternate_fcp_lun_id;  /* alternate FCP LUN        */
	unsigned long long alternate_fcp_ww_name; /* alternate FCP ww name    */
	unchar alternate_enabled;       /* alternate path enabled             */
	unchar alternate_id_valid;      /* alternate id/lun/bus fields valid  */
	unchar alternate_fcp_id_valid;  /* alternate FCP scsi/lun/id fields   */
	unchar primary_drive_port_valid;/* primary drive port field valid     */
	unchar primary_drive_port;      /* primary drive port number          */
	unchar alternate_drive_port_valid; /* alternate drive port valid      */
	unchar alternate_drive_port;    /* alternate drive port               */
	unchar primary_fenced;          /* primary fenced by disable path ioc */
	unchar alternate_fenced;        /* altern. fenced by disable path ioc */
	unchar primary_host;            /* primary host bus adapter id        */
	unchar alternate_host;          /* alternate host bus adapter id      */
	char reserved[56];
};

struct device_path_t {
	char name[30];                  /* logical device name                */
	char parent[30];                /* logical parent name                */
	unchar id_valid;                /* SCSI id/lun/bus fields valid       */
	unchar id;                      /* SCSI target address of device      */
	unchar lun;                     /* SCSI logical unit of device        */
	unchar bus;                     /* SCSI bus for device                */
	unchar fcp_id_valid;            /* FCP scsi/lun/id fields valid       */
	unsigned long long fcp_scsi_id; /* FCP SCSi id of device              */
	unsigned long long fcp_lun_id;  /* FCP logical unit of device         */
	unsigned long long fcp_ww_name; /* FCP world wide name                */
	unchar enabled;                 /* path enabled                       */
	unchar drive_port_valid;        /* drive port field valid             */
	unchar drive_port;              /* drive port number                  */
	unchar fenced;                  /* path fenced by diable path ioctl   */
	unchar host;                    /* host bus adapter id                */
	char reserved[62];
};

struct device_paths {
	int number_paths;               /* number of paths configured         */
	struct device_path_t path[MAX_SCSI_PATH];
};


/******************************************************************************/
#define INQ_HEADER_LEN (4)
#define VEND_ID_LEN (8)
#define PROD_ID_LEN (16)
#define REV_LEN (4)

struct inquiry_data {
	uint  qual : 3,                 /* peripheral qualifier               */
		type : 5;               /* device type                        */
	uint  rm : 1,                   /* removable medium                   */
		mod : 7;                /* device type modifier               */
	uint  iso : 2,                  /* ISO version                        */
		ecma : 3,               /* EMCA version                       */
		ansi : 3;               /* ANSI version                       */
	uint  aenc : 1,                 /* asynchronous event notification    */
		trmiop : 1,             /* terminate I/O process message      */
		: 2,                    /* reserved                           */
		rdf : 4;                /* response data format               */
	unchar len;                     /* additional length                  */
	unchar resvd1;                  /* reserved                           */
	uint  : 4,                      /* reserved                           */
		mchngr : 1,             /* medium changer mode (SCSI 3 only)  */
		: 3;                    /* reserved                           */
	uint  reladr : 1,               /* relative addressing                */
		wbus32 : 1,             /* 32 bit wide data transfers         */
		wbus16 : 1,             /* 16 bit wide data transfers         */
		sync : 1,               /* synchronous data transfers         */
		linked : 1,             /* linked commands                    */
		: 1,                    /* reserved                           */
		cmdque : 1,             /* command queueing                   */
		sftre : 1;              /* soft reset                         */
	unchar vid[VEND_ID_LEN];        /* vendor ID                          */
	unchar pid[PROD_ID_LEN];        /* product ID                         */
	unchar revision[REV_LEN];       /* product revision level             */
	unchar vendor1[20];             /* vendor specific                    */
	unchar resvd2[40];              /* reserved                           */
	unchar vendor2[31];             /* vendor specific (padded to 127)    */
};

struct request_sense {
	uint valid : 1,                 /* information field is valid         */
		err_code : 7;           /* error code                         */
	unchar segnum;                  /* segment number                     */
	uint fm : 1,                    /* file mark detected                 */
		eom : 1,                /* end of medium                      */
		ili : 1,                /* incorrect length indicator         */
		resvd1 : 1,             /* reserved                           */
		key : 4;                /* sense key                          */
	int info;                       /* information bytes                  */
	unchar addlen;                  /* additional sense length            */
	uint cmdinfo;                   /* command specific information       */
	unchar asc;                     /* additional sense code              */
	unchar ascq;                    /* additional sense code qualifier    */
	unchar fru;                     /* field replaceable unit code        */
	uint sksv : 1,                  /* sense key specific valid           */
		cd : 1,                 /* control/data                       */
		resvd2 : 2,             /* reserved                           */
		bpv : 1,                /* bit pointer valid                  */
		sim : 3;                /* system information message         */
	unchar field[2];                /* field pointer                      */
	unchar vendor[109];             /* vendor specific (padded to 127)    */
};

struct element_info {
	ushort robot_addr;              /* first robot address                */
	ushort robots;                  /* number medium transport elements   */
	ushort slot_addr;               /* first medium storage element addr  */
	ushort slots;                   /* number medium storage elements     */
	ushort ie_addr;                 /* first import/export element addr   */
	ushort ie_stations;             /* number import-export elements      */
	ushort drive_addr;              /* first data transfer element addr   */
	ushort drives;                  /* number data transfer elements      */
};

struct move_medium {
	ushort robot;                   /* robot address                      */
	ushort source;                  /* move from location                 */
	ushort destination;             /* move to location                   */
	char invert;                    /* invert before placement bit        */
};

struct pos_to_elem {
	ushort robot;                   /* robot address                      */
	ushort destination;             /* move to location                   */
	char invert;                    /* invert before placement bit        */
};

struct exchange_medium {
	ushort robot;                   /* robot address                      */
	ushort source;                  /* move from location                 */
	ushort destination1;            /* move to location                   */
	ushort destination2;            /* move to location                   */
	char invert1;                   /* invert before placement into dest1 */
	char invert2;                   /* invert before placement into dest2 */
};

struct element_status {
	ushort address;                 /* element address                    */
	uint   : 2,                     /* reserved                           */
		inenab : 1,             /* media into changer's scope         */
		exenab : 1,             /* media out of changer's scope       */
		access : 1,             /* robot access allowed               */
		except : 1,             /* abnormal element state             */
		impexp : 1,             /* import/export bit                  */
		full : 1;               /* element contains medium            */
	unchar resvd1;                  /* reserved                           */
	unchar asc;                     /* additional sense code              */
	unchar ascq;                    /* additional sense code qualifier    */
	uint   notbus : 1,              /* element not on same bus as robot   */
		: 1,                    /* reserved                           */
		idvalid : 1,            /* element address valid              */
		luvalid : 1,            /* logical unit valid                 */
		: 1,                    /* reserved                           */
		lun : 3;                /* logical unit number                */
	unchar scsi;                    /* scsi bus address                   */
	unchar resvd2;                  /* reserved                           */
	uint   svalid : 1,              /* element address valid              */
		invert : 1,             /* medium inverted                    */
		: 6;                    /* reserved                           */
	ushort source;                  /* source storage element address     */
	unchar volume[36];              /* primary volume tag                 */
	unchar resvd3[4];               /* reserved                           */
};

struct inventory {
	struct element_status *robot_status;  /* MTE pages                    */
	struct element_status *slot_status;   /* SE pages                     */
	struct element_status *ie_status;     /* IE pages                     */
	struct element_status *drive_status;  /* DTE pages                    */
};

struct element_range {
	ushort element_address;         /* starting element address           */
	ushort number_elements;         /* number of elements                 */
};

struct element_devid {
	ushort address;                 /* element address                    */
	uint   : 4,                     /* reserved                           */
		access : 1,             /* robot access allowed               */
		except : 1,             /* abnormal element state             */
		: 1,                    /* reserved                           */
		full : 1;               /* element contains medium            */
	unchar resvd1;                  /* reserved                           */
	unchar asc;                     /* additional sense code              */
	unchar ascq;                    /* additional sense code qualifier    */
	uint   notbus : 1,              /* element not on same bus as robot   */
		: 1,                    /* reserved                           */
		idvalid : 1,            /* element address valid              */
		luvalid : 1,            /* logical unit valid                 */
		: 1,                    /* reserved                           */
		lun : 3;                /* logical unit number                */
	unchar scsi;                    /* scsi bus address                   */
	unchar resvd2;                  /* reserved                           */
	uint   svalid : 1,              /* element address valid              */
		invert : 1,             /* medium inverted                    */
		: 6;                    /* reserved                           */
	ushort source;                  /* source storage element address     */
	uint   : 4,                     /* reserved                           */
		code_set : 4;           /* code set X'2' is all ASCII id      */
	uint   : 4,                     /* reserved                           */
		ident_type : 4;         /* identifier type                    */
	unchar resvd3;                  /* reserved                           */
	unchar ident_len;               /* identifier length                  */
	unchar identifier[36];          /* device identification              */
};

struct read_element_devids {
	ushort element_address;         /* starting element address           */
	ushort number_elements;         /* number of elements                 */
	struct element_devid *drive_devid; /* data transfer element pages     */
};

struct cartridge_location_data {
	ushort address;
	unchar : 4,
		access : 1,
		except : 1,
		: 1,
		full   : 1;
	unchar resvd1;
	unchar asc;
	unchar ascq;
	unchar resvd2[3];
	unchar svalid : 1,
		invert : 1,
		: 6;
	ushort source;
	unchar volume[36];
	unchar : 4,
		code_set   : 4;
	unchar : 4,
		ident_type : 4;
	unchar resvd3;
	unchar ident_len;
	unchar identifier[24];
};

struct read_cartridge_location {
	ushort element_address;               /* starting element address     */
	ushort number_elements;               /* number of elements           */
	struct cartridge_location_data* data; /* storage element pages        */
	char reserved[8];                     /* reserved                     */
};

/*******************************************************************************
* STIOCREADRESERVKEYS is used to show info on existing reservation Keys.
* STIOCREADRESERVAIONS is used to show info on existing reservations.
* STIOCCLEARREGISTRATION is used to clear registration and reservation.
*******************************************************************************/

#define STIOC_READ_RESERVEKEYS \
	_IOR('z',0x38,struct read_keys)        /* query reservation keys      */
#define STIOC_READ_RESERVATIONS \
	_IOR('z',0x39,struct read_reserves)    /* query reservations          */
#define STIOC_REGISTER_KEY \
	_IO('z',0x40)                          /* register a reservation key  */
#define STIOC_REMOVE_REGISTRATION \
	_IO('z',0x41)                          /* remove current host's reg   */
#define STIOC_CLEAR_ALL_REGISTRATIONS \
	_IO('z',0x42)                          /* clear all registration keys */

#define RESERVE_KEY_LENGTH          8          /* each key is 8-byte-long     */

struct read_keys {
    uint  generation;                          /* counter for PR-out requests */
    uint  length;                              /* bytes in the Res. Key list  */
    char *reserve_key_list;                    /* list of reservation keys    */
};

struct reserve_descriptor {
	char key[RESERVE_KEY_LENGTH];          /* reservation key             */
	uint scope_spec_addr;                  /* scope-specific address      */
	BYTE reserved;
	BYTE scope : 4,                        /* persistent res. scope       */
		type : 4;                      /* reservation type            */
	ushort ext_length;                     /* extent length               */
};

struct read_reserves {
	uint            generation;            /* counter for PR-out requests */
	uint            length;                /* bytes in the Res. list      */
	struct reserve_descriptor* reserve_list;  /* list of res. key descs   */
};


/******************************************************************************/

struct density_data_t {
	char density_code;                  /* mode sense header density code */
	char default_density;               /* default write density          */
	char pending_density;               /* pending write density          */
	char reserved[9];
};

#define STIOC_GET_DENSITY              _IOR('C',0x28,struct density_data_t)
#define STIOC_SET_DENSITY              _IOWR('C',0x29,struct density_data_t)

/*******************************************************************************
* GET_ENCRYPITON_STATE is used to query the current drive's encryption status.
* SET_ENCRYPTION_STATE is used to set application-managed encrption state.
* SET_DATA_KEY is used for application to set the encryption key.
*******************************************************************************/

struct encryption_status {
	unchar encryption_capable;
	unchar encryption_method;
#define METHOD_NONE 0
#define METHOD_LIBRARY 1
#define METHOD_SYSTEM 2
#define METHOD_APPLICATION 3
#define METHOD_CUSTOM 4
#define METHOD_UNKNOWN 5
	unchar encryption_state;
#define STATE_OFF 0
#define STATE_ON 1
#define STATE_NA 2
#define STATE_UNKNOWN 3
	unchar reserved[13];
};

#define GET_ENCRYPTION_STATE           _IOR('C',0x30,struct encryption_status)
#define SET_ENCRYPTION_STATE           _IOWR('C',0x31,struct encryption_status)

struct data_key {
	unchar data_key_index[12];
	unchar data_key_index_length;
	unchar reserved1[15];
	unchar data_key[32];
	unchar rserved2[48];
};

#define SET_DATA_KEY	                 _IOWR('C',0x32,struct data_key)

/******************************************************************************/
/* Data Encryption diagnostic ioctl data structure and defines                */
/******************************************************************************/
#define SERVER_PING_DIAG         1
#define BASIC_ENCRYPTION_DIAG    2
#define FULL_ENCRYPTION_DIAG     3

struct encryption_diagnostics {
	int diag_type;           /* diagtype defined above to be invoked      */
	int diag_errno;          /* errno return code for failures or 0       */
	int diag_cc;             /* diag completion code for diag type        */
	unchar reserved[8];
};

#define DATA_ENCRYPTION_DIAGNOSTICS \
	_IOWR('C',0x33,struct encryption_diagnostics)

/******************************************************************************/
/* Partition IOCTLs                                                           */
/******************************************************************************/

#define IDP_PARTITION    (1)
#define SDP_PARTITION    (2)
#define FDP_PARTITION    (3)

#define UNKNOWN_PAR_TYPE (0)
#define WRAP_WISE_PART   (1)
#define LONGITUDE_PART   (2)

#define SIZE_UNIT_BYTES  (0)
#define SIZE_UNIT_KBYTES (3)
#define SIZE_UNIT_MBYTES (6)
#define SIZE_UNIT_GBYTES (9)
#define SIZE_UNIT_TBYTES (12)

#define MAX_PARTITIONS   (255)
#define MAX_SUPPORTED_PARTITIONS (4)

struct query_partition {
	unchar max_partitions;
	unchar active_partition;
	unchar number_of_partitions;
	unchar size_unit;
	ushort size[MAX_PARTITIONS];
	unchar partition_method; /* for 3592-E07 and later */
	char reserved[31];
};

#define DEVICE_CONFIG_MODE_PAGE    (0x10)
#define MEDIUM_PARTITION_MODE_PAGE (0x11)

#define STIOC_QUERY_PARTITION _IOR('z', 0x43, struct query_partition)

struct tape_partition {
	unchar type;
	unchar number_of_partitions;
	unchar size_unit;
	ushort size[MAX_PARTITIONS];
	unchar partition_method; /* for 3592-E07 and later */
	char reserved[31];
};

#define STIOC_CREATE_PARTITION _IOW('z', 0x44, struct tape_partition)

struct set_active_partition {
	unchar partition_number;
	unsigned long long logical_block_id;
	char reserved[32];
};

#define STIOC_SET_ACTIVE_PARTITION _IOW('z', 0x45, struct set_active_partition)

struct allow_data_overwrite {
	unchar partition_number;
	unsigned long long logical_block_id;
	unchar allow_format_overwrite;
	char reserved[32];
};

#define STIOC_ALLOW_DATA_OVERWRITE _IOW('z', 0x46, struct allow_data_overwrite)

/******************************************************************************/
/* Enhanced Position IOCTLs                                                   */
/******************************************************************************/

/* Extended READ_POSITION */

#define RP_SHORT_FORM (0x00)
#define RP_LONG_FORM (0x06)
#define RP_EXTENDED_FORM (0x08)

struct short_data_format {
#if __BYTE_ORDER == __LITTLE_ENDIAN
	unchar bpew : 1;
	unchar perr : 1;
	unchar lolu : 1;
	unchar rsvd : 1;
	unchar bycu : 1;
	unchar locu : 1;
	unchar eop : 1;
	unchar bop : 1;
#elif __BYTE_ORDER == __BIG_ENDIAN
	unchar bop : 1;
	unchar eop : 1;
	unchar locu : 1;
	unchar bycu : 1;
	unchar rsvd : 1;
	unchar lolu : 1;
	unchar perr : 1;
	unchar bpew : 1;
#else
		error
#endif
	unchar active_partition;
	char reserved[2];
	unchar first_logical_obj_position[4];
	unchar last_logical_obj_position[4];
	unchar num_buffer_logical_obj[4];
	unchar num_buffer_bytes[4];
	char reserved1;
};

struct long_data_format {
#if __BYTE_ORDER == __LITTLE_ENDIAN
	unchar bpew : 1;
	unchar rsvd2 : 1;
	unchar lonu : 1;
	unchar mpu : 1;
	unchar rsvd1 : 2;
	unchar eop : 1;
	unchar bop : 1;
#elif __BYTE_ORDER == __BIG_ENDIAN
	unchar bop : 1;
	unchar eop : 1;
	unchar rsvd1 : 2;
	unchar mpu : 1;
	unchar lonu : 1;
	unchar rsvd2 : 1;
	unchar bpew : 1;
#else
		error
#endif
	char reserved[6];
	unchar active_partition;
	unchar logical_obj_number[8];
	unchar logical_file_id[8];
	unchar obsolete[8];
};

struct extended_data_format {
#if __BYTE_ORDER == __LITTLE_ENDIAN
	unchar bpew : 1;
	unchar perr : 1;
	unchar lolu : 1;
	unchar rsvd : 1;
	unchar bycu : 1;
	unchar locu : 1;
	unchar eop : 1;
	unchar bop : 1;
#elif __BYTE_ORDER == __BIG_ENDIAN
	unchar bop : 1;
	unchar eop : 1;
	unchar locu : 1;
	unchar bycu : 1;
	unchar rsvd : 1;
	unchar lolu : 1;
	unchar perr : 1;
	unchar bpew : 1;
#else
		error
#endif
	unchar active_partition;
	unchar additional_length[2];
	unchar num_buffer_logical_obj[4];
	unchar first_logical_obj_position[8];
	unchar last_logical_obj_position[8];
	unchar num_buffer_bytes[8];
	unchar reserved;
};

struct read_tape_position {
	unchar data_format;
	union {
		struct short_data_format rp_short;
		struct long_data_format rp_long;
		struct extended_data_format rp_extended;
	} rp_data;
};

#define STIOC_READ_POSITION_EX _IOWR('z', 0x47, struct read_tape_position)

/* LOCATE 16 */

#define LOGICAL_ID_BLOCK_TYPE (0x00)
#define LOGICAL_ID_FILE_TYPE (0x01)

struct set_tape_position {
	unchar logical_id_type;
	unsigned long long logical_id;
	char reserved[32];
};

#define STIOC_LOCATE_16 _IOW('z', 0x48, struct set_tape_position)

/******************************************************************************/
/* Logical Block Protection IOCTLs                                            */
/******************************************************************************/

#define LBP_DISABLE             (0x00)
#define REED_SOLOMON_CRC        (0x01)

struct logical_block_protection {
	unchar lbp_capable;
	unchar lbp_method;
	unchar lbp_info_length;
	unchar lbp_w;
	unchar lbp_r;
	unchar rbdp;
	unchar reserved[26];
};

#define EXTENDED_INQUIRY_PAGE   (0x86)

#define STIOC_QUERY_BLK_PROTECTION  _IOR('z', 0x49, \
	struct logical_block_protection)
#define STIOC_SET_BLK_PROTECTION _IOW('z', 0x50, \
	struct logical_block_protection)

/******************************************************************************/
/* EOT warning IOCTLs                                                         */
/******************************************************************************/

struct eot_warn {
	unchar warn;
	unchar reserved[7];
};

#define STIOC_QUERY_EOT_WARN _IOR('z', 0x52, struct eot_warn)
#define STIOC_SET_EOT_WARN _IOW('z', 0x53, struct eot_warn)

/******************************************************************************/
/* VERIFY_TAPE_DATA IOCTL                                                     */
/******************************************************************************/

struct verify_data {

#if __BYTE_ORDER == __LITTLE_ENDIAN
	unchar fixed     : 1;
	unchar bytcmp    : 1;
	unchar immed     : 1;
	unchar vbf       : 1;
	unchar vlbpm     : 1;
	unchar vte       : 1;
	unchar reserved1 : 2;
#elif __BYTE_ORDER == __BIG_ENDIAN
	unchar reserved1 : 2;
	unchar vte       : 1;
	unchar vlbpm     : 1;
	unchar vbf       : 1;
	unchar immed     : 1;
	unchar bytcmp    : 1;
	unchar fixed     : 1;
#else
	error
#endif
	unchar verify_length[3];
	unchar reserved2[15];
};

#define STIOC_VERIFY_TAPE_DATA _IOW('z', 0x54, struct verify_data)

/******************************************************************************/
/* New passthrough ioctl data structure and defines                           */
/******************************************************************************/

struct sioc_pass_through {
	unsigned char cmd_length;     /* Input: Length of SCSI command        */
	unsigned char *cdb;           /* Input: SCSI command descriptor block */
	uint buffer_length;           /* Input: Length of data buffer         */
                                      /* Output: bytes actually transferred   */
	unsigned char *buffer;        /* Input/Output: data transfer or NULL  */
	unsigned int data_direction;  /* Input: Data transfer direction       */
	uint timeout;                 /* Input: Timeout in seconds            */
	unsigned char sense_length;   /* Input/Output: sense data bytes       */
	unsigned char *sense;         /* Output: Sense when sense length > 0  */
	int resid;                    /* Output: resid bytes after transfer   */
	int32_t result;               /* Output: result from lower driver     */
	unsigned char msg_status;     /* Output: from SCSI transport layer    */
	unsigned char target_status;  /* Output: target device status         */
	unsigned short driver_status; /* Output: level driver status          */
	unsigned short host_status;   /* Output: host bus adapter status      */

	unsigned char reserved[64];
};

#define SIOC_PASS_THROUGH     _IOWR('C', 0x34, struct sioc_pass_through)

#define MaxScsiPath		16

#endif
