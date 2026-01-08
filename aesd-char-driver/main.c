/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Nada");
MODULE_LICENSE("Dual BSD/GPL");

// #define AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED 10
#include "aesd-circular-buffer.h"

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0;
    size_t entry_offset = 0;
    const struct aesd_buffer_entry *entry;
    // size_t bytes_available;
    size_t bytes_to_copy;

    if (count == 0)
        return 0;

    mutex_lock(&dev->lock);

    while (count > 0) {

        entry = aesd_circular_buffer_find_entry_offset_for_fpos(
            &dev->buffer, *f_pos, &entry_offset);

        if (!entry)
            break; /* EOF */

        bytes_to_copy = entry->size - entry_offset;
        if (bytes_to_copy > count)
            bytes_to_copy = count;

        if (copy_to_user(buf + retval,
                         entry->buffptr + entry_offset,
                         bytes_to_copy)) {
            retval = -EFAULT;
            goto out;
        }

        *f_pos += bytes_to_copy;
        retval += bytes_to_copy;
        count -= bytes_to_copy;
    }

out:
    mutex_unlock(&dev->lock);
    return retval;
}


ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    char *kbuf;
    ssize_t retval = count;

    if (count == 0)
        return 0;

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    if (copy_from_user(kbuf, buf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    mutex_lock(&dev->lock);

    /* Append to pending */
    {
        char *newpending = kmalloc(dev->pending_size + count, GFP_KERNEL);
        if (!newpending) {
            retval = -ENOMEM;
            goto out;
        }

        if (dev->pending)
            memcpy(newpending, dev->pending, dev->pending_size);

        memcpy(newpending + dev->pending_size, kbuf, count);

        kfree(dev->pending);
        dev->pending = newpending;
        dev->pending_size += count;
    }

    /* Process complete commands */
    while (1) {
        size_t i, cmd_len = 0;
        char *cmd;
        const char *oldptr = NULL;

        for (i = 0; i < dev->pending_size; i++) {
            if (dev->pending[i] == '\n') {
                cmd_len = i + 1;
                break;
            }
        }

        if (cmd_len == 0)
            break;

        cmd = kmalloc(cmd_len, GFP_KERNEL);
        if (!cmd) {
            retval = -ENOMEM;
            goto out;
        }

        memcpy(cmd, dev->pending, cmd_len);

        /* free overwritten entry if buffer full */
        if (dev->buffer.full)
            oldptr = dev->buffer.entry[dev->buffer.in_offs].buffptr;

        {
            struct aesd_buffer_entry entry = {
                .buffptr = cmd,
                .size = cmd_len
            };
            aesd_circular_buffer_add_entry(&dev->buffer, &entry);
        }

        if (oldptr)
            kfree(oldptr);

        /* remove command from pending */
        memmove(dev->pending,
                dev->pending + cmd_len,
                dev->pending_size - cmd_len);

        dev->pending_size -= cmd_len;

        if (dev->pending_size == 0) {
            kfree(dev->pending);
            dev->pending = NULL;
        }
    }

out:
    mutex_unlock(&dev->lock);
    kfree(kbuf);
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));
    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.buffer);
    aesd_device.pending = NULL;
    aesd_device.pending_size = 0;

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    mutex_lock(&aesd_device.lock);

    kfree(aesd_device.pending);
    aesd_device.pending = NULL;
    aesd_device.pending_size = 0;

    {
        int i;
        for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
            if (aesd_device.buffer.entry[i].buffptr) {
                kfree((void *)aesd_device.buffer.entry[i].buffptr);
                aesd_device.buffer.entry[i].buffptr = NULL;
                aesd_device.buffer.entry[i].size = 0;
            }
        }
    }

    mutex_unlock(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
