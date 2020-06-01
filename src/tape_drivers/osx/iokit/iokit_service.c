/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2020 IBM Corp. All rights reserved.
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
** FILE NAME:       tape_drivers/osx/iokit/iokit_service.c
**
** DESCRIPTION:     iokit raw service functions
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

#include <stdint.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOTypes.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <IOKit/storage/IOStorageProtocolCharacteristics.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <mach/mach.h>

#include "iokit_service.h"
#include "libltfs/ltfs_error.h"
#include "libltfs/ltfslogging.h"

/* Local functions */
static int _get_device_count(CFMutableDictionaryRef *matchingDict)
{
	int count = -1;
	kern_return_t kernelResult;

	mach_port_t masterPort = kIOMasterPortDefault;
	io_iterator_t serviceIterator = IO_OBJECT_NULL;

	if (matchingDict == NULL) {
		count = -100;
		return count;
	}

	// Search I/O Registry for matching devices
	kernelResult = IOServiceGetMatchingServices(masterPort, *matchingDict, &serviceIterator);

	if( (serviceIterator == IO_OBJECT_NULL) || (IOIteratorNext(serviceIterator) == 0) ) {
		count = -101;
		return count;
	}

	if(serviceIterator && kernelResult == kIOReturnSuccess) {
		io_service_t scsiDevice = IO_OBJECT_NULL;
		count = 0;

		IOIteratorReset(serviceIterator);

		if(! IOIteratorIsValid(serviceIterator)) {
			count = -102;
			return count;
		}

		// Count devices matching service class
		while( (scsiDevice = IOIteratorNext(serviceIterator)) ) {
			count++;
		}

		IOIteratorReset(serviceIterator);
		while( (scsiDevice = IOIteratorNext(serviceIterator)) ) {
			kernelResult = IOObjectRelease(scsiDevice); // Done with SCSI object from I/O Registry.
		}
	}

	return count;
}

static int _find_device(struct iokit_device *device, int device_number, CFMutableDictionaryRef *matchingDict)
{
	int ret = -1;
	kern_return_t kernelResult;

	device->masterPort = kIOMasterPortDefault;

	if (matchingDict == NULL) {
		/* TODO: Replace to better logic*/
		ret = -100;
		return ret;
	}

	io_iterator_t serviceIterator = IO_OBJECT_NULL;

	// Search I/O Registry for matching devices
	kernelResult = IOServiceGetMatchingServices(device->masterPort, *matchingDict, &serviceIterator);

	if( (serviceIterator == IO_OBJECT_NULL) || (IOIteratorNext(serviceIterator) == 0) ) {
		/* TODO: Replace to better logic*/
		ret = -101;
		return ret;
	}

	if(serviceIterator && kernelResult == kIOReturnSuccess) {
		io_service_t scsiDevice = IO_OBJECT_NULL;
		int count = 0;

		IOIteratorReset(serviceIterator);

		// Select N'th tape drive based on driveNumber value
		while( (scsiDevice = IOIteratorNext(serviceIterator)) ) {
			if(count == device_number) {
				break;
			} else {
				kernelResult = IOObjectRelease(scsiDevice); // Done with SCSI object from I/O Registry.
				count++;
			}
		}

		if(scsiDevice == IO_OBJECT_NULL) {
			/* TODO: Replace to better logic*/
			ret = -1;
			return ret;
		}

		device->ioservice = IO_OBJECT_NULL;
		device->ioservice = scsiDevice;

		// Create DeviceInterface and store in lto_osx_data struct.
		assert(device->ioservice != IO_OBJECT_NULL);
		IOCFPlugInInterface		**plugin_interface = NULL;
		SCSITaskDeviceInterface	**task_device_interface = NULL;
		HRESULT					plugin_query_result = S_OK;
		SInt32 score = 0;

		kernelResult = IOCreatePlugInInterfaceForService(device->ioservice,
														 kIOSCSITaskDeviceUserClientTypeID,
														 kIOCFPlugInInterfaceID,
														 &plugin_interface,
														 &score);
		if (kernelResult != kIOReturnSuccess) {
			/* TODO: Replace to better logic*/
			ret = -1;
			return ret;
		} else {
			// Query the base plugin interface for an instance of the specific SCSI device interface
			// object.
			plugin_query_result = (*plugin_interface)->QueryInterface(plugin_interface,
																	  CFUUIDGetUUIDBytes(kIOSCSITaskDeviceInterfaceID),
																	  (LPVOID *) &task_device_interface);

			if (plugin_query_result != S_OK) {
				/* TODO: Replace to better logic*/
				ret = -2;
				return ret;
			}
		}

		// Set the return values.
		device->plugInInterface = plugin_interface;
		device->scsiTaskInterface = task_device_interface;
		device->task = NULL;
		ret = 0;
	}

	return ret;
}


static void _create_matching_dictionary_for_device_class(CFMutableDictionaryRef *matchingDict,
														 SInt32 peripheralDeviceType )
{
	CFMutableDictionaryRef subDictionary;

	assert(matchingDict != NULL);

	// Create the matching dictionaries...
	*matchingDict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
													&kCFTypeDictionaryValueCallBacks);
	if(*matchingDict != NULL) {
		// Create a sub-dictionary to hold the required device patterns.
		subDictionary = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
													&kCFTypeDictionaryValueCallBacks);

		if (subDictionary != NULL) {
			// Set the "SCSITaskDeviceCategory" key so that we match
			// devices that understand SCSI commands.
			CFDictionarySetValue(subDictionary, CFSTR(kIOPropertySCSITaskDeviceCategory),
												 CFSTR(kIOPropertySCSITaskUserClientDevice));
			// Set the "PeripheralDeviceType" key so that we match
			// sequential storage (tape) devices.
			SInt32 deviceTypeNumber = peripheralDeviceType;
			CFNumberRef deviceTypeRef = NULL;
			deviceTypeRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &deviceTypeNumber);
			CFDictionarySetValue(subDictionary, CFSTR(kIOPropertySCSIPeripheralDeviceType), deviceTypeRef);
			CFRelease (deviceTypeRef);
		}

		// Add the sub-dictionary pattern to the main dictionary with the key "IOPropertyMatch" to
		// narrow the search to the above dictionary.
		CFDictionarySetValue(*matchingDict, CFSTR(kIOPropertyMatchKey), subDictionary);

		CFRelease(subDictionary);
	}
}


static void _create_matching_dictionary_for_ssc(CFMutableDictionaryRef *matchingDict)
{
	_create_matching_dictionary_for_device_class(matchingDict, kINQUIRY_PERIPHERAL_TYPE_SequentialAccessSSCDevice);
}


static void _create_matching_dictionary_for_smc(CFMutableDictionaryRef *matchingDict)
{
	_create_matching_dictionary_for_device_class(matchingDict, kINQUIRY_PERIPHERAL_TYPE_MediumChangerSMCDevice);
}

static void _release_scsitask(struct iokit_device *device)
{
	if(device != NULL) {
		// Release IOKit task interface
		if(device->scsiTaskInterface != NULL)
			(*device->scsiTaskInterface)->Release(device->scsiTaskInterface);
	}
}

/* Functions to be called from other files */

int iokit_obtain_exclusive_access(struct iokit_device *device)
{
	int ret = -EDEV_UNKNOWN;

	IOReturn scsiResult = IO_OBJECT_NULL;
	scsiResult = (*device->scsiTaskInterface)->ObtainExclusiveAccess(device->scsiTaskInterface);

	switch (scsiResult) {
		case kIOReturnSuccess:
			/* Got exclusive access */
			device->exclusive_lock = true;
			ret = DEVICE_GOOD;
			break;
		case kIOReturnBusy:
			ret = -EDEV_DEVICE_BUSY;
			break;
		default:
			ret = -EDEV_DEVICE_UNOPENABLE;
			break;
	}

	return ret;
}

int iokit_release_exclusive_access(struct iokit_device *device)
{
	int ret = -1;

	IOReturn scsiResult = IO_OBJECT_NULL;
	scsiResult = (*device->scsiTaskInterface)->ReleaseExclusiveAccess(device->scsiTaskInterface);

	switch (scsiResult) {
		case kIOReturnSuccess:
			/* Released exclusive access */
			device->exclusive_lock = false;
			ret = 0;
			break;
		default:
			ret = -1;
			break;
	}

	return ret;
}

int iokit_allocate_scsitask(struct iokit_device *device)
{
	if(device == NULL) {
		/* TODO: Replace to better logic */
		return -100;
	}

	if(device->task == NULL) {
		// Create a SCSI task for the device. This task will be re-used for all future SCSI operations.
		device->task = (*device->scsiTaskInterface)->CreateSCSITask(device->scsiTaskInterface);

		if (device->task == NULL) {
			/* TODO: Replace to better logic */
			return -101;
		}
	}

	return 0;
}

void iokit_release_scsitask(struct iokit_device *device)
{
	if(device != NULL) {
		// Release IOKit task interface
		if(device->scsiTaskInterface != NULL)
			(*device->scsiTaskInterface)->Release(device->scsiTaskInterface);
	}
}

int iokit_get_ssc_device_count(void)
{
	int count = -1;
	CFMutableDictionaryRef matchingDict = NULL;

	_create_matching_dictionary_for_ssc(&matchingDict);
	count = _get_device_count(&matchingDict);

	return count;
}


int iokit_get_smc_device_count(void)
{
	int count = -1;
	CFMutableDictionaryRef matchingDict = NULL;

	_create_matching_dictionary_for_smc(&matchingDict);
	count = _get_device_count(&matchingDict);

	return count;
}


int iokit_find_ssc_device(struct iokit_device *device, int drive_number)
{
	int ret = -1;
	CFMutableDictionaryRef matchingDict = NULL;

	_create_matching_dictionary_for_ssc(&matchingDict);
	ret = _find_device(device, drive_number, &matchingDict);
	return ret;
}


int iokit_find_smc_device(struct iokit_device *device, int changer_number)
{
	int ret = -1;
	CFMutableDictionaryRef matchingDict = NULL;

	_create_matching_dictionary_for_smc(&matchingDict);
	ret = _find_device(device, changer_number, &matchingDict);
	return ret;
}


int iokit_free_device(struct iokit_device *device)
{
	int ret = 0;
	kern_return_t kernelResult;

	if(device == NULL)
		return ret;

	_release_scsitask(device);

	// Release PlugInInterface
	if(device->plugInInterface != NULL) {
		kernelResult = IODestroyPlugInInterface(device->plugInInterface);
		if(kernelResult != kIOReturnSuccess) {
			ret = -100;
		}
	}

	// Release SCSI object from I/O Registry.
	kernelResult = IOObjectRelease(device->ioservice);
	if(kernelResult != kIOReturnSuccess) {
		ret = -101;
	}

	return ret;
}
