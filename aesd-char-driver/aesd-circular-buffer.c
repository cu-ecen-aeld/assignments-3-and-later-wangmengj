/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/cdev.h>
#include "aesdchar.h"
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    if(NULL == buffer || NULL == entry_offset_byte_rtn)
        return NULL;

    PDEBUG("looking for: offset %d \n",(int)char_offset);

    int currentOffset = 0;
    int i;
    for(i = 0 ; i < buffer->in_offs; ++i)
    {
        if(char_offset >= currentOffset && char_offset < currentOffset + buffer->entry[i].size) 
        {
            PDEBUG("found for: item no. %d, %s \n", i, buffer->entry[i].buffptr);
            *entry_offset_byte_rtn  = char_offset - currentOffset;
            return &(buffer->entry[i]);
        }

        currentOffset+= buffer->entry[i].size;
    }

    return NULL;
}


void aesd_circular_buffer_remove_entry(struct aesd_circular_buffer *buffer, struct aesd_buffer_entry * entry)
{
    if(NULL == buffer )
        return ;

    // empty
    if(0 >= buffer->in_offs)
        return ;

    struct aesd_buffer_entry tmp;
    if(NULL == entry) 
        entry = &tmp;

    entry->buffptr = buffer->entry[0].buffptr;
    entry->size = buffer->entry[0].size;

    int i;
    for( i = 0 ; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1; ++i)
        buffer->entry[i] = buffer->entry[i+1];

    buffer->in_offs--; 
    buffer->full = false;
}


/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    if(NULL == buffer || NULL == add_entry)
        return;

    // if it is already full, make a space first.
    if(buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) 
    {
        buffer->full = false;

        int i;
        for(i = 0 ; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1; ++i)
            buffer->entry[i] = buffer->entry[i+1];

        buffer->in_offs = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1;
    }

    buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;
    buffer->entry[buffer->in_offs].size = add_entry->size;
    buffer->in_offs++;

    if(buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) 
        buffer->full = true;

    PDEBUG("adding: %s, size:%d , buffer->in_offs = %d \n", 
        add_entry->buffptr, (int)add_entry->size, (int)buffer->in_offs );
}


    
/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
