/*
 * PiScopinator
 * Copyright 2015, David Whipple <d@ae7pb.net>
 *
 * Based on the following works:
 *
 * Linux 2.6 and 3.0 'parrot' sample device driver
 * Copyright (c) 2011, Pete Batard <pete@akeo.ie>
 * http://pete.akeo.ie/2011/08/writing-linux-device-driver-for-kernels.html
 *
 * Low Level Programming of the Raspberry Pi in C
 * Pieter-Jan Van de Maele
 * http://www.pieter-jan.com/node/15
 *
 * Raspberry Pi as an Oscilloscope @ 10 MSPS
 * Daniel Pelikan
 * https://digibird1.wordpress.com/raspberry-pi-as-an-oscilloscope-10-msps/
 * and Magpi issue 24
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
#include <linux/string.h> // for strcat
#include <linux/uaccess.h> // for copy_to_user
#include "piScopinator.h"

/* Module information */
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");

/* Device variables */
static struct class* piScopinatorClass = NULL;
static struct device* piScopinatorDevice = NULL;
static int piScopinatorMajor;
/* Flag used with the one_shot mode */
static bool messageRead;
/* A mutex will ensure that only one process accesses our device */
static DEFINE_MUTEX(piScopinatorDeviceMutex);

/* Module parameters that can be provided on insmod */
static bool debug = false;	/* print extra debug info */
module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "enable debug info (default: false)");
static bool one_shot = true;	/* only read a single message after open() */
module_param(one_shot, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "disable the readout of multiple messages at once (default: true)");

// pins for the scope
static int piScopinatorCh1Pin1 = PISCOPINATOR_PIN_1;
static int piScopinatorCh1Pin2 = PISCOPINATOR_PIN_2;
static int piScopinatorCh1Pin3 = PISCOPINATOR_PIN_3;
static int piScopinatorCh1Pin4 = PISCOPINATOR_PIN_4;
static int piScopinatorCh1Pin5 = PISCOPINATOR_PIN_5;
static int piScopinatorCh1Pin6 = PISCOPINATOR_PIN_6;
static int piScopinatorSampleSize = PISCOPINATOR_SAMPLE_SIZE;

// Data that we collected
static int piscopinatorData[PISCOPINATOR_SAMPLE_SIZE];
static int *dataPointer;

static int piScopinatorDeviceOpen(struct inode* inode, struct file* filp)
{
    int x = 0;
    dbg("");
    // TODO: set up pins

	/* Our sample device does not allow write access */
	if ( ((filp->f_flags & O_ACCMODE) == O_WRONLY)
	  || ((filp->f_flags & O_ACCMODE) == O_RDWR) ) {
		warn("write access is prohibited\n");
		return -EACCES;
	}

	/* Ensure that only one process has access to our device at any one time
 	* For more info on concurrent accesses, see http://lwn.net/images/pdf/LDD3/ch05.pdf */
	if (!mutex_trylock(&piScopinatorDeviceMutex)) {
		warn("another process is accessing the device\n");
		return -EBUSY;
	}

    for(x = 0; x < 10000; x++) {
        piscopinatorData[x] = x;
    }


	messageRead = false;
	return 0;
}

static int piScopinatorDeviceClose(struct inode* inode, struct file* filp)
{
    // TODO: restore pins

	dbg("");
	mutex_unlock(&piScopinatorDeviceMutex);
	return 0;
}

static ssize_t piScopinatorDeviceRead(struct file* filp, char __user *buffer, size_t length, loff_t* offset)
{
    int bytesRead = 0;
    int lineBytesRead = 0;

    unsigned char messageOutput[70];
    unsigned int *messagePointer;
    int dataCount = 0;
    int columnCount = 0;
    unsigned char messageByte[10];

    /* The default from 'cat' is to issue multiple reads 
     * one_shot avoids that */
    if (messageRead) return 0;
    dbg("");
    dataPointer = (int*)&piscopinatorData;

    while (length && (dataCount < piScopinatorSampleSize)) {
        memset(messageOutput,0,70);
        lineBytesRead = 0;
        for(columnCount = 0; columnCount < 8; columnCount++) {
            if(dataCount <= piScopinatorSampleSize) {
                dataCount ++;
                snprintf(messageByte,9,"%08X",*dataPointer++);
                strcat(messageOutput,messageByte);
                bytesRead += 8;
                lineBytesRead += 8;
            }
        }
        strcat(messageOutput,"\n");
        bytesRead++;
        lineBytesRead++;
        messagePointer = (int*)&messageOutput;

        if(0!=copy_to_user(buffer, messageOutput, lineBytesRead)) {
            warn("Copy_to_user failed!");
        }
        buffer += 65;    
        length--;
    }
    messageRead = true;
    dbg("Bytes read:%d",bytesRead);
    return bytesRead;
}

/* The file_operation scructure tells the kernel which device operations are handled.
 * For a list of available file operations, see http://lwn.net/images/pdf/LDD3/ch03.pdf */
static struct file_operations fops = {
	.read = piScopinatorDeviceRead,
	.open = piScopinatorDeviceOpen,
	.release = piScopinatorDeviceClose
};

/* Placing data into the read FIFO is done through sysfs */
static ssize_t piScopinatorSampleCount(struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	long sampleCount = 0;
	char messageIn[20];
	char *messageInPointer;
	dbg("");    
    // get the message. Make it a num and just return if it is invalid
    strncpy(messageIn,buf,19);
    messageInPointer = messageIn;
    
	if(0!=kstrtol(messageInPointer,10,&sampleCount)) {
		warn("Invalid sample count: %s",messageIn);
		return count;
	}
	if(sampleCount <=0 || sampleCount >= 10000) {
		warn("Invalid sample count %lu",sampleCount);
		return count;
	}
	piScopinatorSampleSize = sampleCount;

	return count;
}

/* This sysfs entry chooses which GPIO for the pins */
static ssize_t piScopinatorChannel1Pin1(struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	long pin = 0;
	char messageIn[20];
	char *messageInPointer;
	dbg("");    
    // get the message. Make it a num and just return if it is invalid
    strncpy(messageIn,buf,19);
    messageInPointer = messageIn;
    
	if(0!=kstrtol(messageInPointer,10,&pin)) {
		warn("Invalid pin value: %s",messageIn);
		return count;
	}
	if(pin <=0 || pin >= 33) {
		warn("Invalid pin value: %lu",pin);
		return count;
	}
	piScopinatorCh1Pin1	= pin;

	return count;
}

/* This sysfs entry chooses which GPIO for the pins */
static ssize_t piScopinatorChannel1Pin2(struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	long pin = 0;
	char messageIn[20];
	char *messageInPointer;
	dbg("");    
    // get the message. Make it a num and just return if it is invalid
    strncpy(messageIn,buf,19);
    messageInPointer = messageIn;
    
	if(0!=kstrtol(messageInPointer,10,&pin)) {
		warn("Invalid pin value: %s",messageIn);
		return count;
	}
	if(pin <=0 || pin >= 33) {
		warn("Invalid pin value: %lu",pin);
		return count;
	}
	piScopinatorCh1Pin2	= pin;

	return count;
}

/* This sysfs entry chooses which GPIO for the pins */
static ssize_t piScopinatorChannel1Pin3(struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	long pin = 0;
	char messageIn[20];
	char *messageInPointer;
	dbg("");    
    // get the message. Make it a num and just return if it is invalid
    strncpy(messageIn,buf,19);
    messageInPointer = messageIn;
    
	if(0!=kstrtol(messageInPointer,10,&pin)) {
		warn("Invalid pin value: %s",messageIn);
		return count;
	}
	if(pin <=0 || pin >= 33) {
		warn("Invalid pin value: %lu",pin);
		return count;
	}
	piScopinatorCh1Pin3	= pin;

	return count;
}

/* This sysfs entry chooses which GPIO for the pins */
static ssize_t piScopinatorChannel1Pin4(struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	long pin = 0;
	char messageIn[20];
	char *messageInPointer;
	dbg("");    
    // get the message. Make it a num and just return if it is invalid
    strncpy(messageIn,buf,19);
    messageInPointer = messageIn;
    
	if(0!=kstrtol(messageInPointer,10,&pin)) {
		warn("Invalid pin value: %s",messageIn);
		return count;
	}
	if(pin <=0 || pin >= 33) {
		warn("Invalid pin value: %lu",pin);
		return count;
	}
	piScopinatorCh1Pin4	= pin;

	return count;
}

/* This sysfs entry chooses which GPIO for the pins */
static ssize_t piScopinatorChannel1Pin5(struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	long pin = 0;
	char messageIn[20];
	char *messageInPointer;
	dbg("");    
    // get the message. Make it a num and just return if it is invalid
    strncpy(messageIn,buf,19);
    messageInPointer = messageIn;
    
	if(0!=kstrtol(messageInPointer,10,&pin)) {
		warn("Invalid pin value: %s",messageIn);
		return count;
	}
	if(pin <=0 || pin >= 33) {
		warn("Invalid pin value: %lu",pin);
		return count;
	}
	piScopinatorCh1Pin5	= pin;

	return count;
}

/* This sysfs entry chooses which GPIO for the pins */
static ssize_t piScopinatorChannel1Pin6(struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	long pin = 0;
	char messageIn[20];
	char *messageInPointer;
	dbg("");    
    // get the message. Make it a num and just return if it is invalid
    strncpy(messageIn,buf,19);
    messageInPointer = messageIn;
    
	if(0!=kstrtol(messageInPointer,10,&pin)) {
		warn("Invalid pin value: %s",messageIn);
		return count;
	}
	if(pin <=0 || pin >= 33) {
		warn("Invalid pin value: %lu",pin);
		return count;
	}
	piScopinatorCh1Pin6	= pin;

	return count;
}

/* This sysfs entry chooses which GPIO for the pins */
static ssize_t piScopinatorReadConfig(struct device* dev, struct device_attribute* attr, char* buf)
{
	
	return scnprintf(buf, PAGE_SIZE, "Pins for scope:\nPin 1: gpio%d\nPin 2: gpio%d\n"
		"Pin 3: gpio%d\nPin 4: gpio%d\nPin 5: gpio%d\nPin 6: gpio%d\nSample Count: %d\n", 
		piScopinatorCh1Pin1, piScopinatorCh1Pin2, piScopinatorCh1Pin3, piScopinatorCh1Pin4, piScopinatorCh1Pin5, 
		piScopinatorCh1Pin6, piScopinatorSampleSize);
}


/* Declare the sysfs entries. DEVICE_ATTR(name shown, permissions, read function, write function) */
static DEVICE_ATTR(sampleCount, S_IWUSR|S_IWGRP, NULL, piScopinatorSampleCount);
static DEVICE_ATTR(channel1Pin1, S_IWUSR|S_IWGRP, NULL, piScopinatorChannel1Pin1);
static DEVICE_ATTR(channel1Pin2, S_IWUSR|S_IWGRP, NULL, piScopinatorChannel1Pin2);
static DEVICE_ATTR(channel1Pin3, S_IWUSR|S_IWGRP, NULL, piScopinatorChannel1Pin3);
static DEVICE_ATTR(channel1Pin4, S_IWUSR|S_IWGRP, NULL, piScopinatorChannel1Pin4);
static DEVICE_ATTR(channel1Pin5, S_IWUSR|S_IWGRP, NULL, piScopinatorChannel1Pin5);
static DEVICE_ATTR(channel1Pin6, S_IWUSR|S_IWGRP, NULL, piScopinatorChannel1Pin6);
static DEVICE_ATTR(readConfig, S_IRUSR|S_IRGRP, piScopinatorReadConfig, NULL);

/* Module initialization and release */
static int __init piScopinatorModuleInit(void)
{
	int retval;
	dbg("");

	/* First, see if we can dynamically allocate a major for our device */
	piScopinatorMajor = register_chrdev(0, DEVICE_NAME, &fops);
	if (piScopinatorMajor < 0) {
		err("failed to register device: error %d\n", piScopinatorMajor);
		retval = piScopinatorMajor;
		goto failed_chrdevreg;
	}

	/* We can either tie our device to a bus (existing, or one that we create)
	 * or use a "virtual" device class. For this example, we choose the latter */
	piScopinatorClass = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(piScopinatorClass)) {
		err("failed to register device class '%s'\n", CLASS_NAME);
		retval = PTR_ERR(piScopinatorClass);
		goto failed_classreg;
	}

	/* With a class, the easiest way to instantiate a device is to call device_create() */
	piScopinatorDevice = device_create(piScopinatorClass, NULL, MKDEV(piScopinatorMajor, 0), NULL, CLASS_NAME "_" DEVICE_NAME);
	if (IS_ERR(piScopinatorDevice)) {
		err("failed to create device '%s_%s'\n", CLASS_NAME, DEVICE_NAME);
		retval = PTR_ERR(piScopinatorDevice);
		goto failed_devreg;
	}

	/* Now we can create the sysfs endpoints (don't care about errors).
	 * dev_attr_fifo and dev_attr_reset come from the DEVICE_ATTR(...) earlier */
	retval = device_create_file(piScopinatorDevice, &dev_attr_sampleCount);
	if (retval < 0) {
		warn("failed to create write /sys endpoint - continuing without\n");
	}
	retval = device_create_file(piScopinatorDevice, &dev_attr_channel1Pin1);
	if (retval < 0) {
		warn("failed to create channel1Pin1 /sys endpoint - continuing without\n");
	}
	retval = device_create_file(piScopinatorDevice, &dev_attr_channel1Pin2);
	if (retval < 0) {
		warn("failed to create channel1Pin2 /sys endpoint - continuing without\n");
	}
	retval = device_create_file(piScopinatorDevice, &dev_attr_channel1Pin3);
	if (retval < 0) {
		warn("failed to create channel1Pin3 /sys endpoint - continuing without\n");
	}
	retval = device_create_file(piScopinatorDevice, &dev_attr_channel1Pin4);
	if (retval < 0) {
		warn("failed to create channel1Pin4 /sys endpoint - continuing without\n");
	}
	retval = device_create_file(piScopinatorDevice, &dev_attr_channel1Pin5);
	if (retval < 0) {
		warn("failed to create channel1Pin5 /sys endpoint - continuing without\n");
	}
	retval = device_create_file(piScopinatorDevice, &dev_attr_channel1Pin6);
	if (retval < 0) {
		warn("failed to create channel1Pin6 /sys endpoint - continuing without\n");
	}
	retval = device_create_file(piScopinatorDevice, &dev_attr_readConfig);
	if (retval < 0) {
		warn("failed to create channel1Pin6 /sys endpoint - continuing without\n");
	}

	mutex_init(&piScopinatorDeviceMutex);
	/* This device uses a Kernel FIFO for its read operation */

	return 0;

failed_devreg:
	class_unregister(piScopinatorClass);
	class_destroy(piScopinatorClass);
failed_classreg:
	unregister_chrdev(piScopinatorMajor, DEVICE_NAME);
failed_chrdevreg:
	return -1;
}

static void __exit piScopinatorModuleExit(void)
{
	dbg("");
	device_remove_file(piScopinatorDevice, &dev_attr_sampleCount);
	device_remove_file(piScopinatorDevice, &dev_attr_channel1Pin1);
	device_remove_file(piScopinatorDevice, &dev_attr_channel1Pin2);
	device_remove_file(piScopinatorDevice, &dev_attr_channel1Pin3);
	device_remove_file(piScopinatorDevice, &dev_attr_channel1Pin4);
	device_remove_file(piScopinatorDevice, &dev_attr_channel1Pin5);
	device_remove_file(piScopinatorDevice, &dev_attr_channel1Pin6);
	device_remove_file(piScopinatorDevice, &dev_attr_readConfig);
	device_destroy(piScopinatorClass, MKDEV(piScopinatorMajor, 0));
	class_unregister(piScopinatorClass);
	class_destroy(piScopinatorClass);
	unregister_chrdev(piScopinatorMajor, DEVICE_NAME);
}

/* Let the kernel know the calls for module init and exit */
module_init(piScopinatorModuleInit);
module_exit(piScopinatorModuleExit);
