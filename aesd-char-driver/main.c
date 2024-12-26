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

#include "aesd-circular-buffer.h"
#include "aesdchar.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Your Name Here"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");









struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    size_t avail;
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */

	if (mutex_lock_interruptible(&aesd_device.lock))
		return -ERESTARTSYS;

    // Any data available? If not, return 0
    avail = aesd_circular_buffer_data_available(&aesd_device.cbuf);
    if (avail == 0)
        goto out;

// *** Could be partial buffer of data in aesd_device.part_entry
//      But we're ignoring that (just wait until a 'write' puts the full entry into circular buffer)
//      May have to rethink this if grader objects...


    // If first buffer contains amount of data requested or less, return entire buffer
    if (avail <= count)
    {
        // Return full entry, and remove it from buffer
        struct aesd_buffer_entry entry = aesd_circular_buffer_remove_entry(&aesd_device.cbuf);
        retval = avail;
    	if (copy_to_user(buf, entry.buffptr + entry.offset, avail)) {
		    retval = -EFAULT;
		    goto out;
	    }
        kfree(entry.buffptr);
    }

    // First entry has more data than we want, so read part of it (don't remove entry from buffer)
    else {
        struct aesd_buffer_entry entry = aesd_circular_buffer_read_partial(&aesd_device.cbuf, count);
        retval = count;
        if (copy_to_user(buf, entry.buffptr + entry.offset, count)) {
		    retval = -EFAULT;
		    goto out;
	    }
    }


  out:
	mutex_unlock(&aesd_device.lock);
	return retval;

}


ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{

	ssize_t retval = -ENOMEM; /* value used in "goto out" statements */
    size_t needed;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */



	if (mutex_lock_interruptible(&aesd_device.lock))
		return -ERESTARTSYS;


    /*
    For now, check if data ends in '\n'
    If it does, write a complete entry to circular buffer
    Else, add it to 'part_entry', a partially-filled entry
    */
    if (count == 0)                 // Prob not necessary...
        goto out;
    
    // If no partial-entry buffer exists it'll be created by krealloc()
    // if (aesd_device.part_entry.buffptr == NULL)
    // {
    //     // // Allocate a new parial entry
    //     // aesd_device.part_entry.buffptr = kmalloc(count, GFP_KERNEL);
	// 	// if (!aesd_device.part_entry.buffptr)
	// 	// 	goto errout;
    //     aesd_device.part_entry.size = count;
    //     aesd_device.part_entry.offset = 0;
    // }

    // Create or resize the partial-entry buffer for new data
    needed = aesd_device.part_entry.size + count;               // Total bytes we need space for
    // if (needed > aesd_device.part_entry.size)
    // {
    aesd_device.part_entry.buffptr = krealloc(aesd_device.part_entry.buffptr, needed, GFP_KERNEL);
    if (!aesd_device.part_entry.buffptr)
        goto errout;
    // }

    // Copy/append new data
	if (copy_from_user((char*)aesd_device.part_entry.buffptr + aesd_device.part_entry.size, buf, count)) {
		retval = -EFAULT;
		goto errout;
	}
    // aesd_device.part_entry.offset += count;
    aesd_device.part_entry.size = needed;


    // If data ends in '\n', add new entry to circular buffer
    // If not, we're done
    if (aesd_device.part_entry.buffptr[aesd_device.part_entry.size - 1] == '\n')
    {
         // Add to circ buf
        const char* removed_buf = aesd_circular_buffer_add_entry(&aesd_device.cbuf, &aesd_device.part_entry);
        kfree(removed_buf);     // Might be NULL...kfree doesn't care
        aesd_device.part_entry.buffptr = NULL;          // Partial entry is now empty
        aesd_device.part_entry.size = 0;  
    }

out:
    retval = count;
errout:
	mutex_unlock(&aesd_device.lock);
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

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.cbuf);
    mutex_init(&aesd_device.lock);
    

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    struct aesd_buffer_entry entry;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    while (true)
    {
        entry = aesd_circular_buffer_remove_entry(&aesd_device.cbuf);
        if (entry.buffptr == NULL)
            break;
        else
            kfree(entry.buffptr);
    }
    if (aesd_device.part_entry.buffptr != NULL)
        kfree(aesd_device.part_entry.buffptr);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
