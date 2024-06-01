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
#include <linux/slab.h>
#include "aesdchar.h"
#include "aesd-circular-buffer.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Mengjia Wang"); 
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

static struct aesd_circular_buffer circularBuffer;
static struct rw_semaphore circularBufferLock;

static struct aesd_circular_buffer incompleteWriteBuffer;
static struct rw_semaphore incompleteWriteBufferLock; 

int aesd_open(struct inode *inode, struct file *filp)
{
    if(NULL == inode || NULL == filp)
    {
        PDEBUG("aesd_open empty pointers.");
        return 0;
    }
     
    filp->private_data = container_of(inode->i_cdev, struct aesd_dev, cdev);

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
    // we need an offset and a buffer
    if(NULL == f_pos || NULL == buf) 
    {
        PDEBUG("aesd_read null f_pos or buf");
        return 0; 
    }

    ssize_t retval = 0;
    PDEBUG("aesd_read %zu bytes with offset %lld",count,*f_pos);


    // read the item in the circular buffer out.     
    down_read(&circularBufferLock);

    size_t accumlatedCopied = 0;
    size_t entryOffset = 0 ; 
    size_t copyToOffset = 0; 
    int index = 0;
    struct aesd_buffer_entry * entryptr;

    AESD_CIRCULAR_BUFFER_FOREACH_A(entryptr,&circularBuffer,index)
    {
        // user' buf is used up. 
        if (count <= 0) 
            break;

        PDEBUG("aesd_read current index = %d, circularBuffer.out_offs = %d, circularBuffer.in_offs = %d \n",
            index, circularBuffer.out_offs, circularBuffer.in_offs);
        PDEBUG("aesd_read *f_pos:%ld, entryOffset:%ld, size:%ld ", *f_pos, entryOffset, entryptr->size); 

        // if f_pos in part of an item.
        if(*f_pos >= entryOffset && *f_pos < entryOffset + entryptr->size)
        {

            // copy partial of the items
            size_t copyFromOffset = *f_pos - entryOffset;
            size_t copyLength = 0;
            if(entryptr->size - copyFromOffset >= count)
                copyLength = count;
            else
                copyLength = entryptr->size - copyFromOffset;

            unsigned long result = copy_to_user(
                buf + copyToOffset , entryptr->buffptr + copyFromOffset, copyLength);
            if(0 != result)
               PDEBUG("aesd_read copy_to_user 1 result:%ld ", result); 

            PDEBUG("aesd_read 1 %zu bytes from offset %lld, from (%lld-%ld) ",
                count,*f_pos, entryOffset + copyFromOffset, copyLength );

            // next time copy to 
            copyToOffset += copyLength;

            // how much space left in user's buf 
            count -= copyLength;

            // how many copied in total.
            accumlatedCopied += copyLength;
        }
        else if(*f_pos < entryOffset)
        {
            // copy the whole item.
            size_t copyFromOffset = 0 ;
            size_t copyLength = entryptr->size; 
            if(copyLength > count)
                copyLength = count;

            unsigned long result = copy_to_user(
                buf + copyToOffset , entryptr->buffptr + copyFromOffset, copyLength);
            if(0 != result)
               PDEBUG("aesd_read copy_to_user 2 result:%ld ", result); 

            PDEBUG("aesd_read 1 %zu bytes from offset %lld, from (%lld-%ld) ",
                count,*f_pos, entryOffset + copyFromOffset, copyLength );

            count -= copyLength; 
            copyToOffset += copyLength; 
            accumlatedCopied += copyLength;
        }
    
        entryOffset += entryptr->size;
    }

   /* 
    struct aesd_buffer_entry * pEntry = 
        aesd_circular_buffer_find_entry_offset_for_fpos(
            &circularBuffer, *f_pos, &entryOffset);

    if(NULL != pEntry)
    {
        size_t countToCopy = pEntry->size - entryOffset ;
        if(countToCopy > count)
            countToCopy = count;
        unsigned long result = copy_to_user(buf, pEntry->buffptr + entryOffset, countToCopy);
        if(0 != result)
           PDEBUG("aesd_read copy_to_user result:%ld ", result); 

        PDEBUG("aesd_read offset %lld, returned: %s : %ld",*f_pos, pEntry->buffptr + entryOffset, (unsigned long)countToCopy);
    }
    */

    up_read(&circularBufferLock);

    *f_pos += accumlatedCopied;
    return accumlatedCopied;
}

void * aesd_malloc(size_t count, char * log)
{
    void * vp = kmalloc(count, GFP_KERNEL);
    PDEBUG("aesd memory log kmalloc %ld -- %p at %s.\n",(unsigned long)count, vp, log);
    return vp;
}

void aesd_free(void * vp, char * log)
{
    PDEBUG("aesd memory log kfree %p at %s .", vp, log);
    kfree(vp);
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_buffer_entry entryToIncompleteWrite; 
    struct aesd_buffer_entry entryToCircularBuffer; 

    // adding 1 extra for '\0' char for debug purpose.
    entryToIncompleteWrite.buffptr = aesd_malloc(count + 1, "loc 1" );
    char * pchar = (char*)entryToIncompleteWrite.buffptr;
    pchar[count] = '\0';
    ssize_t retval = count;

    entryToIncompleteWrite.size = count;
    entryToCircularBuffer.buffptr = NULL;
    entryToCircularBuffer.size = 0;

    if(NULL == entryToIncompleteWrite.buffptr)
    {
        PDEBUG("write: kmalloc %ld failed.", (unsigned long)count);
        return -ENOMEM;
    }

    void *pVoid = entryToIncompleteWrite.buffptr;
    unsigned long result = copy_from_user(pVoid, buf, count);
    if(0 != result)
    {
        PDEBUG("write: copy_from_user returned %ld, need to copy %ld", result, (unsigned long)count);
        aesd_free(entryToIncompleteWrite.buffptr, "loc 2");
        return -ENOMEM;
    }
     
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    down_write(&incompleteWriteBufferLock);
    
    // if incomplete buffer is full, free the first item.
    if(incompleteWriteBuffer.full)
    { 
        struct aesd_buffer_entry aEntry; 
        aesd_circular_buffer_remove_entry(&incompleteWriteBuffer, &aEntry);
        aesd_free(aEntry.buffptr, "loc 3");
    }

    PDEBUG("write to incompleteWriteBuffer first \n");
    // regardless this item is end with a '\n', add them to the incomplete buffer first.
    aesd_circular_buffer_add_entry(&incompleteWriteBuffer, &entryToIncompleteWrite);

    // if last char is a 'line return char' : we transfer everything in the 
    // incomplete buffer to the circularBuffer
    if('\n' == entryToIncompleteWrite.buffptr[count-1])
    {
        PDEBUG("write there is a line return char\n");

        // we need to make a buffer big enough for everything in the incomplete buffer 
        int iTotalSize = 0 ;
        int index = 0;
        struct aesd_buffer_entry * entryptr;

        // search all items and find the total size;
        AESD_CIRCULAR_BUFFER_FOREACH_A(entryptr,&incompleteWriteBuffer,index)
            iTotalSize += entryptr->size;

        PDEBUG("write allocate total size: %d\n", iTotalSize);
        entryToCircularBuffer.buffptr = aesd_malloc(iTotalSize + 1, "loc 4");
        pchar[iTotalSize] = '\0';
        entryToCircularBuffer.size = iTotalSize;

        if(NULL == entryToCircularBuffer.buffptr)
        {
            PDEBUG("write: kmalloc for circular buffer %d failed.", iTotalSize);
            up_write(&incompleteWriteBufferLock);
            return -ENOMEM;
        }
        
        // copy everything from pIncompleteWriteBufferLock to newEntry
        int i = 0 ;
        AESD_CIRCULAR_BUFFER_FOREACH_A(entryptr,&incompleteWriteBuffer,index)
        {
            char * pVoid = (void*)(entryToCircularBuffer.buffptr + i);
            memcpy(pVoid, entryptr->buffptr, entryptr->size);
            i += entryptr->size;

            // free each buffptr
            aesd_free(entryptr->buffptr, "loc 5");
            entryptr->buffptr=NULL;
            entryptr->size=0;
        }

        // empty the incomplete buffer
        aesd_circular_buffer_init(&incompleteWriteBuffer);
    }
    
    up_write(&incompleteWriteBufferLock);

    if(NULL != entryToCircularBuffer.buffptr)
    {
        down_write(&circularBufferLock);  

        // if the circular buffer is full, we remove the first item.
        if(circularBuffer.full)
        {
            struct aesd_buffer_entry aEntry; 
            aesd_circular_buffer_remove_entry(&circularBuffer, &aEntry);
            aesd_free(aEntry.buffptr, "loc 6");
        }

        aesd_circular_buffer_add_entry(&circularBuffer, &entryToCircularBuffer);
        up_write(&circularBufferLock);
    }

    /**
     * TODO: handle write
     */
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
    init_rwsem(&circularBufferLock);
    init_rwsem(&incompleteWriteBufferLock);

    aesd_circular_buffer_init(&circularBuffer);
    aesd_circular_buffer_init(&incompleteWriteBuffer);
    
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

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    unregister_chrdev_region(devno, 1);


    // kfree all buffer 
    int index;
    struct aesd_buffer_entry * entryptr;
    AESD_CIRCULAR_BUFFER_FOREACH_A(entryptr,&incompleteWriteBuffer,index)
    {
        // free each buffptr
        aesd_free(entryptr->buffptr, "loc 7");
        entryptr->buffptr=NULL;
        entryptr->size=0;
    }

    AESD_CIRCULAR_BUFFER_FOREACH_A(entryptr,&circularBuffer,index)
    {
        // free each buffptr
        aesd_free(entryptr->buffptr, "loc 8");
        entryptr->buffptr=NULL;
        entryptr->size=0;
    }
 
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
