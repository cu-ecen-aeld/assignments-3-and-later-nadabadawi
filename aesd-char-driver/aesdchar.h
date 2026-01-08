/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#define AESD_DEBUG 1  //Remove comment on this line to enable debug

#include "aesd-circular-buffer.h"

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

struct aesd_dev
{
    /**
     * TODO: Add structure(s) and locks needed to complete assignment requirements
     */
//     char *data;           /* dynamically allocated buffer */
//     size_t size;
    struct mutex lock;
    struct cdev cdev;     /* Char device structure      */
     /* Circular buffer of the last 10 completed commands */
    struct aesd_circular_buffer buffer;

    /* “In-progress” command accumulation (no '\n' seen yet) */
    char *pending;
    size_t pending_size;
};


#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
