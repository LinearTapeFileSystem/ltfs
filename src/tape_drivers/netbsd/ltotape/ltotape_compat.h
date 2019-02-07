#ifndef __ltotape_compat_h
#define __ltotape_compat_h

/* From HPE ltfs/rc/libltfs/tape_ops.h */

#include <stdbool.h>

struct tc_drive_param { 
        unsigned int max_blksize;           /* maximum block size    */
        bool         write_protect;         /* Write Protect         */
        bool         logical_write_protect; /* Logical Write Protect */
};  

#define TC_MAX_DENSITY_REPORTS (8)

struct tc_density_code{
        unsigned char primary;
        unsigned char secondary;
}; 
  
struct tc_density_report {
        int size;
        struct tc_density_code density[TC_MAX_DENSITY_REPORTS]; 
};

/* From HPE ltfs/src/libltfs/ltfs.h */

#define GENERIC_OEM_BUILD

typedef enum {
    UNLOCKED_MAM    = 0x00,
    LOCKED_MAM      = 0x01,
    PWE_MAM          = 0x02,
    PERMLOCKED_MAM  = 0x03,
    DPPWE_MAM       = 0x04,
    IPPWE_MAM       = 0x05,
    DP_IP_PWE_MAM   = 0x06,
         NOLOCK_MAM          = 0x80  // HPE MD 25.09.2017 This used to be set to 0x04.  Not sure why but as 0x04 is now
                                 // used in the spec have changed to a larger value.
} mam_lockval;

/* From HPE ltfs/src/libltfs/ltfs_error.h */
#define TC_MAM_PAGE_ATTRIBUTE_ALL   0 /* Page code for all the attribute passed while formatting and mounting the volume */

#define LTFS_POS_SUSPECT_BOP           1147  /* HPE MD 24/11/17 If a write FM is attempted at BOP partition 0 */


/* From src/tape_drivers/tape_drivers.h */
#define REDPOS_EXT_LEN            (32)

/* From src/tape_drivers/ssc_op_codes.h */
#define READ_POSITION (0x34)

static const char*          lsn = "Generic LTFS                    ";

#endif /* __ltotape_compat_h */
