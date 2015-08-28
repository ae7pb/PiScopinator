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


#ifndef PISCOPINATOR_H
#define PISCOPINATOR_H
// Define which Raspberry Pi board are you using. Take care to have defined only one at time.
//#define RPI
#define RPI2

#define PISCOPINATOR_SAMPLE_SIZE 10000
#define piScopinatorMessageFIFOSize 1024
#define piScopinatorMessageFIFOMax  128

#define DEVICE_NAME "device"
#define CLASS_NAME "piscopinator"

#define AUTHOR "David Whipple <d@ae7pb.net>"
#define DESCRIPTION "PiScopinator raspberry pi oscilloscope"
#define VERSION "0.0"

/* We'll use our own macros for printk */
#define dbg(format, arg...) do { if (debug) pr_info(CLASS_NAME ": %s: " format "\n" , __FUNCTION__ , ## arg); } while (0)
#define err(format, arg...) pr_err(CLASS_NAME ": " format "\n" , ## arg)
#define info(format, arg...) pr_info(CLASS_NAME ": " format "\n" , ## arg)
#define warn(format, arg...) pr_warn(CLASS_NAME ": " format "\n" , ## arg)

#ifdef RPI
#define RPI_PERIPHERAL_BASE  0x20000000
#define GPIO_BASE            (RPI_PERIPHERAL_BASE + 0x200000)  // GPIO controller 
#define BSC0_BASE            (RPI_PERIPHERAL_BASE + 0x205000)  // I2C controller   
#endif

#ifdef RPI2
#define RPI_PERIPHERAL_BASE  0x3F000000
#define GPIO_BASE            (RPI_PERIPHERAL_BASE + 0x200000)  // GPIO controller. Maybe wrong. Need to be tested.
#define BSC0_BASE            (RPI_PERIPHERAL_BASE + 0x804000)  // I2C controller   
#endif  

// Default values for the pins.  You can changes these at echo (gpio#)>/sys/devices/virtual/piscopinator/piscopinator_device
#define PISCOPINATOR_PIN_1	17
#define PISCOPINATOR_PIN_2	18
#define PISCOPINATOR_PIN_3	27
#define PISCOPINATOR_PIN_4	22
#define PISCOPINATOR_PIN_5	23
#define PISCOPINATOR_PIN_6	24



// IO Acces
struct rpiPeripheral {
        unsigned long addr_p;
        int mem_fd;
        void *map;
        volatile unsigned int *addr;
};

//extern struct rpiPeripheral gpio;  // They have to be found somewhere, but can't be in the header
//extern struct rpiPeripheral bsc0;  // so use extern!!


// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g)     *(gpio.addr + ((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g)     *(gpio.addr + ((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio.addr + (((g)/10))) |= (((a)<=3?(a) + 4:(a)==4?3:2)<<(((g)%10)*3))

#define GPIO_SET    *(gpio.addr + 7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR    *(gpio.addr + 10) // clears bits which are 1 ignores bits which are 0

#define GPIO_READ(g)    *(gpio.addr + 13) &= (1<<(g))
#define GPIO_READ_ALL	*(gpio.addr + 13)

// I2C macros
#define BSC0_CONFIG     *(bsc0.addr + 0x00)
#define BSC0_STATUS     *(bsc0.addr + 0x01)
#define BSC0_DLEN       *(bsc0.addr + 0x02)
#define BSC0_A          *(bsc0.addr + 0x03)
#define BSC0_FIFO       *(bsc0.addr + 0x04)

#define BSC_CONFIG_I2CEN     (1 << 15)
#define BSC_CONFIG_INTR      (1 << 10)
#define BSC_CONFIG_INTT      (1 << 9)
#define BSC_CONFIG_INTD      (1 << 8)
#define BSC_CONFIG_ST        (1 << 7)
#define BSC_CONFIG_CLEAR     (1 << 4)
#define BSC_CONFIG_READ      1

#define START_READ      BSC_CONFIG_I2CEN|BSC_C_ST|BSC_C_CLEAR|BSC_C_READ
#define START_WRITE     BSC_CONFIG_I2CEN|BSC_C_ST

#define BSC_STATUS_CLKT      (1 << 9)
#define BSC_STATUS_ERR       (1 << 8)
#define BSC_STATUS_RXF       (1 << 7)
#define BSC_STATUS_TXE       (1 << 6)
#define BSC_STATUS_RXD       (1 << 5)
#define BSC_STATUS_TXD       (1 << 4)
#define BSC_STATUS_RXR       (1 << 3)
#define BSC_STATUS_TXW       (1 << 2)
#define BSC_STATUS_DONE      (1 << 1)
#define BSC_STATUS_TA        1

#define CLEAR_STATUS    BSC_STATUS_CLKT|BSC_S_ERR|BSC_S_DONE



#endif
