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





// I readily admit I'm not a kernel programmer so if you read this don't be too harsh






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
#include <linux/kobject.h> // kobject stuff
#include "piScopinator.h"


/* Module information */
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");


/* A mutex will ensure that only one process accesses our device */
static DEFINE_MUTEX(piScopinatorDeviceMutex);

/* Module parameters that can be provided on insmod */
static bool debug = false;	/* print extra debug info */
module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "enable debug info (default: false)");
static bool one_shot = true;	/* only read a single message after open() */
module_param(one_shot, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "disable the readout of multiple messages at once (default: true)");

// Data that we collected
static int collected_data[PISCOPINATOR_SAMPLE_SIZE];
static unsigned long collection_times[PISCOPINATOR_SAMPLE_SIZE];
//static int *dataPointer;
static int data_pointer_count = 0;
static long collected_dataTime = 0;
static int piScopinatorSampleSize = PISCOPINATOR_SAMPLE_SIZE;
static char data_ready = 0;

static struct rpiPeripheral gpio = {GPIO_BASE};


/*     Oscilloscope functions and hardware related    */

static void read_gpio_fast (void) {

    int x = 0;
    struct timespec start_time, end_time;


    dbg("");	


    // IRQs will mess up our sample times so turn them off.
    local_irq_disable();
    local_fiq_disable();

    // time this bad boy
    getnstimeofday(&start_time);

    // get the data for the whole first 32 gpio pins & figure out what we want later
    for(x = 0; x < piScopinatorSampleSize; x++) {
        collected_data[x] = GPIO_READ_ALL;
    }

    // end time
    getnstimeofday(&end_time);

    // even though the functions say nano seconds it won't really be nano second resolution since our pi 
    // isn't that fast.  Oh well

    collected_dataTime = timespec_to_ns(&end_time) - timespec_to_ns(&start_time);

    // don't forget to reactivate IRQ
    local_fiq_enable();
    local_irq_enable();    

    // We are going to be outputting in pages so setting up a pointer and a 
    // counter so we know when we are done.
    //dataPointer = (int*)&collected_data;
    data_pointer_count = 0;

    data_ready = 1;
}

static void read_gpio_accurate (void) {

    int x = 0;
    struct timespec start_time, end_time, current_time;


    dbg("");	


    // IRQs will mess up our sample times so turn them off.
    local_irq_disable();
    local_fiq_disable();

    // time this bad boy
    getnstimeofday(&start_time);

    // get the data for the whole first 32 gpio pins & figure out what we want later
    for(x = 0; x < piScopinatorSampleSize; x++) {
        collected_data[x] = GPIO_READ_ALL;
        getnstimeofday(&current_time);
        collection_times[x] = timespec_to_ns(&current_time);
    }

    // end time
    getnstimeofday(&end_time);

    // even though the functions say nano seconds it won't really be nano second resolution since our pi 
    // isn't that fast.  Oh well

    collected_dataTime = timespec_to_ns(&end_time) - timespec_to_ns(&start_time);

    // don't forget to reactivate IRQ
    local_fiq_enable();
    local_irq_enable();    

    // We are going to be outputting in pages so setting up a pointer and a 
    // counter so we know when we are done.
    //dataPointer = (int*)&collected_data;
    data_pointer_count = 0;

    data_ready = 1;
}

// Exposes the physical address defined in the passed structure using mmap on /dev/mem
int mapPeripheral(struct rpiPeripheral *periph)
{
    periph->addr=(uint32_t *)ioremap(GPIO_BASE, 41*4); //41 GPIO register with 32 bit (4*8)
    return 0;
}

void unmapPeripheral(struct rpiPeripheral *periph) {
    iounmap(periph->addr);//unmap the address
}


/*  sysfs related /sys/device/virtual/piscopinator */

#define PISC_ATTR_RO(_name) \
    static struct kobj_attribute _name##_attr = __ATTR_RO(_name)
#define PISC_ATTR(_name) \
    static struct kobj_attribute _name##_attr = \
__ATTR(_name, 0644, _name##_show, _name##_store)



// This outputs the data
static ssize_t read_data_fast_show(struct kobject *kobj, struct kobj_attribute *attr, char* buf)
{
    // max we can put in the scnprintf is 1024 bytes (compiler warning)
    int frame_size = 1024;
    char messagePageOut[frame_size];
    char messageByte[10];
    int x = 0;

    dbg("");

    if(data_ready == 0) {
        return scnprintf(buf, 20, "No data\n");
    }

    memset(messagePageOut,0,(1024));


    // check if we have data
    if(data_pointer_count >= piScopinatorSampleSize) {
		data_ready = 0;
        return 0;
    }


    for(x = 0; x < ((frame_size/8)-1); x++) { 
        if(data_pointer_count >= piScopinatorSampleSize) {
			data_ready = 0;
            break;
        }
		snprintf(messageByte,9,"%08X",collected_data[data_pointer_count]);
		strcat(messagePageOut,messageByte);
		data_pointer_count++;
    }
    return scnprintf(buf, frame_size, "%s\n", messagePageOut);
}

PISC_ATTR_RO(read_data_fast);

// This outputs the data
static ssize_t read_data_show(struct kobject *kobj, struct kobj_attribute *attr, char* buf)
{
    // max we can put in the scnprintf is 1024 bytes (compiler warning)
    int frame_size = 1024;
    char messagePageOut[frame_size];
    char messageByte[30];
    int x = 0;

    dbg("");

    if(data_ready == 0) {
        return scnprintf(buf, 20, "No data\n");
    }

    memset(messagePageOut,0,(1024));


    // check if we have data
    if(data_pointer_count >= piScopinatorSampleSize) {
		data_ready = 0;
        return 0;
    }

	// these are 20 chars so we can do 50 at a time and be less than 1024
    for(x = 0; x < (50); x++) { 
        if(data_pointer_count >= piScopinatorSampleSize) {
			data_ready = 0;
            break;
        }
		snprintf(messageByte,22,"%09lX,%08X\n",collection_times[data_pointer_count], collected_data[data_pointer_count]);
		strcat(messagePageOut,messageByte);
		data_pointer_count++;
    }
    return scnprintf(buf, frame_size, "%s", messagePageOut);
}

PISC_ATTR_RO(read_data);

// This will return the approximate nanoseconds it took for the reading
static ssize_t data_remaining_show(struct kobject *kobj, struct kobj_attribute *attr, char* buf) 
{
	int data_remaining = 0;
	
    dbg("");
    if(data_ready == 0) {
        return scnprintf(buf, 20, "0\n");
    }

	data_remaining = piScopinatorSampleSize - data_pointer_count;

    return scnprintf(buf,10,"%d\n",data_remaining);
}

PISC_ATTR_RO(data_remaining);


// If you read from this function it will trigger the readings.
static ssize_t trigger_reading_fast_show(struct kobject *kobj, struct kobj_attribute *attr, char* buf) 
{
    dbg("");
    read_gpio_fast();    
    return scnprintf(buf, 16, "Done.\n");
}

// If you write to this function it will trigger the readings.
static ssize_t trigger_reading_fast_store(struct kobject *kobj, struct kobj_attribute *attr,
        const char *buf, size_t count)
{
    dbg("");

    read_gpio_fast();
    return count;
}

PISC_ATTR(trigger_reading_fast);

// If you read from this function it will trigger the readings.
static ssize_t trigger_reading_show(struct kobject *kobj, struct kobj_attribute *attr, char* buf) 
{
    dbg("");
    read_gpio_accurate();    
    return scnprintf(buf, 16, "Done.\n");
}

// If you write to this function it will trigger the readings.
static ssize_t trigger_reading_store(struct kobject *kobj, struct kobj_attribute *attr,
        const char *buf, size_t count)
{
    dbg("");

    read_gpio_accurate();
    return count;
}

PISC_ATTR(trigger_reading);

// This will return the approximate nanoseconds it took for the reading
static ssize_t read_time_show(struct kobject *kobj, struct kobj_attribute *attr, char* buf) 
{
    dbg("");

    return scnprintf(buf,10,"%lu\n",collected_dataTime);
}

PISC_ATTR_RO(read_time);

static ssize_t sample_size_show(struct kobject *kobj, struct kobj_attribute *attr, char* buf) 
{
    dbg("");

    return scnprintf(buf,10,"%u\n", piScopinatorSampleSize);
}


static ssize_t sample_size_store(struct kobject *kobj, struct kobj_attribute *attr,
        const char *buf, size_t count)
{
    int sample_count = 0;
    sscanf(buf, "%du\n", &sample_count);
    if (sample_count > 0 && sample_count < PISCOPINATOR_SAMPLE_SIZE) {
        piScopinatorSampleSize = sample_count;
    }

    return count;
}

PISC_ATTR(sample_size);

static struct attribute *pisc_attrs[] = {
    &read_data_fast_attr.attr,
    &read_data_attr.attr,
    &data_remaining_attr.attr,
    &trigger_reading_attr.attr,
    &trigger_reading_fast_attr.attr,
    &read_time_attr.attr,
    &sample_size_attr.attr,
    NULL,
};


static struct attribute_group pisc_attr_group = {
    .attrs = pisc_attrs,
};

static struct kobject *piscoperator_kobj;


/* Module initialization and release */
static int __init piScopinatorModuleInit(void)
{
    int retval;

    dbg("");
    piscoperator_kobj = kobject_create_and_add("piscopinator", kernel_kobj);
    if (!piscoperator_kobj) {
        err("Cannot create kobject!");
        goto crap_error;
    }

    retval = sysfs_create_group(piscoperator_kobj, &pisc_attr_group);
    if (retval)
        kobject_put(piscoperator_kobj);

    mutex_init(&piScopinatorDeviceMutex);
    /* This device uses a Kernel FIFO for its read operation */

    // need to map the memory of the gpio registers
    mapPeripheral(&gpio);

    return retval;

crap_error:
    return -1;

}

static void __exit piScopinatorModuleExit(void)
{
    dbg("");
    sysfs_remove_group(piscoperator_kobj, &pisc_attr_group);
    kobject_put(piscoperator_kobj);
    unmapPeripheral(&gpio);
}

/* Let the kernel know the calls for module init and exit */
module_init(piScopinatorModuleInit);
module_exit(piScopinatorModuleExit);
