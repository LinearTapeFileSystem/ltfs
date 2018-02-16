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
** FILE NAME:       tape_drivers/osx/iokit/iokit_service.h
**
** DESCRIPTION:     Defines API for iokit raw service functions
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
**                  Michael A. Richmond
**                  IBM Almaden Research Center
**                  mar@almaden.ibm.com
**
*************************************************************************************
*/

#ifndef __iokit_service_h
#define __iokit_service_h

#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOTypes.h>
#include <IOKit/scsi/SCSITaskLib.h>

struct iokit_device
{
	/* Equivalent to fd in other platforms */
	mach_port_t             masterPort;           /**< OS X master port to kernel for IOKit calls */
	io_service_t            ioservice;            /**< Service instance for the selected tape drive */
	IOCFPlugInInterface     **plugInInterface;    /**< IOKit plugin interface */
	SCSITaskDeviceInterface **scsiTaskInterface;  /**< IOKit task device interface */
	SCSITaskInterface       **task;               /**< Reusable task object */
	bool                    exclusive_lock;       /**< OS X exclusive lock held on device */
	/* */
	bool                    is_data_key_set;      /**< Is a valid data key set? */
};

/**
 * Obtain an IOKit exclusive lock on the SCSI device. This lock
 * prevents other applications from interacting with the device
 * at the OS layer.
 *
 * @param $device the SCSI device.
 *
 * @return 0 on success.
 */
int iokit_obtain_exclusive_access(struct iokit_device *device);

/**
 * Release the IOKit exclusive lock held on the SCSI device.
 *
 * @param $device the SCSI device.
 *
 * @return 0 on success.
 */
int iokit_release_exclusive_access(struct iokit_device *device);

/**
 * Allocate a SCSITask instance for the device.
 * The OS X IOKit framework uses the SCSITask structure
 * to issue CDB operations on SCSI devices.
 * The iokit_scsi_base API automatically creates a
 * SCSITask for the device when needed. This API also
 * re-uses any existing SCSITask structure when issuing
 * a CDB on a device.
 * This iokit_allocate_scsitask() method is intended for
 * situations where an application requires specific
 * control over SCSITask allocation and release.
 * The majority of applications will get optimal
 * performance from the default automatic allocation
 * strategy.
 *
 * @param $device the SCSI device.
 *
 * @return 0 on success.
 */
int iokit_allocate_scsitask(struct iokit_device *device);

/**
 * Release the SCSITask instance for the device.
 * The OS X IOKit framework uses the SCSITask structure
 * to issue CDB operations on SCSI devices.
 * The iokit_scsi_base API automatically creates a
 * SCSITask for the device when needed. This API also
 * re-uses any existing SCSITask structure when issuing
 * a CDB on a device.
 * This iokit_release_scsitask() method is intended for
 * situations where an application requires specific
 * control over SCSITask allocation and release.
 * The majority of applications will get optimal
 * performance from the default automatic allocation
 * strategy.
 *
 * @param $device the SCSI device.
 */
void iokit_release_scsitask(struct iokit_device *device);

/**
 * Frees up resources associated with the device.
 * After this function succeeds the device structure should
 * be disposed of.
 *
 * @param $device the SCSI device.
 *
 * @return 0 on success.
 */
int iokit_free_device(struct iokit_device *device);

/**
 * Provides an integer count of all medium changer devices connected
 * to the host.
 *
 * @return the number of smc devices connected to the host.
 */
int iokit_get_smc_device_count(void);

/**
 * Provides an integer count of all sequential access devices connected
 * to the host.
 *
 * @return the number of ssc devices connected to the host.
 */
int iokit_get_ssc_device_count(void);

/**
 * Finds all medium changer devices. Then selects the device changer_number
 * from that list and populates the device structure to allow access to that
 * device.
 * For example changer_number=0 will find the first smc device.
 *
 * @param $device the SCSI device.
 *
 * @param $changer_number the number of the smc device to find.
 *
 * @return 0 on success.
 */
int iokit_find_smc_device(struct iokit_device *device, int changer_number);

/**
 * Finds all sequential access devices. Then selects the device drive_number
 * from that list and populates the device structure to allow access to that
 * device.
 * For example drive_number=0 will find the first ssc device.
 *
 * @param $device the SCSI device.
 *
 * @param $changer_number the number of the ssc device to find.
 *
 * @return 0 on success.
 */
int iokit_find_ssc_device(struct iokit_device *device, int drive_number);

#endif // __iokit_service_h
