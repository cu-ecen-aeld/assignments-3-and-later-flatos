/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer implementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
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
    /**
    * TODO: implement per description
    */
    uint8_t entry;                                      // Next buffer entry to examine
    size_t offset = 0;                                  // Offset from start of concatenated string segments in buffer
   if ((buffer->in_offs == buffer->out_offs) && (!buffer->full))
        return NULL;            // Empty

    entry = buffer->out_offs;                           // Next buffer entry to examine
    do {
        offset += buffer->entry[entry].size;            // String len including this buffer
        if (char_offset < offset)                       // Break if we've got enough data
            break;
        entry = (entry + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } while (entry != buffer->in_offs);

    if (char_offset < offset)
    {
        *entry_offset_byte_rtn = char_offset - (offset - buffer->entry[entry].size);        // Offset within this buffer
        return (buffer->entry + entry);                            // Pointer to this buffer
    }
    return NULL;
    
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
const char* aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /**
    * TODO: implement per description
    */
    const char *retval = NULL;           // If we're overwriting an entry, pass buffer to caller for freeing
    if (buffer->full)
        retval = buffer->entry[buffer->in_offs].buffptr;

    buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;
    buffer->entry[buffer->in_offs].size = add_entry->size;
    buffer->entry[buffer->in_offs].offset = add_entry->offset;
    buffer->in_offs += 1;
    if (buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) 
        buffer->in_offs = 0;
    if (buffer->full) 
        buffer->out_offs = buffer->in_offs;

    if (buffer->in_offs == buffer->out_offs)
        buffer->full = true;

    return retval;
}


/*
    Remove a aesd_buffer_entry from the buffer and return it
    
    Returns a aesd_buffer_entry with buffer=Null, size=0 if buffer is empty
*/
struct aesd_buffer_entry  aesd_circular_buffer_remove_entry(struct aesd_circular_buffer *buffer)
{
    struct aesd_buffer_entry retval = {.buffptr=NULL, .size=0, .offset=0};
    if ((buffer->out_offs == buffer->in_offs) && !buffer->full)             // Empty
        return retval;

    retval = buffer->entry[buffer->out_offs];
    buffer->entry[buffer->out_offs].buffptr = NULL;         // Don't want to double-free memory on cleanup
    buffer->out_offs += 1;
    if (buffer->out_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) 
        buffer->out_offs = 0;
    
    buffer->full = false;

    return retval;
}


/*
    Return size of data in first-available buffer for reading
    
*/
size_t aesd_circular_buffer_data_available(struct aesd_circular_buffer *buffer)
{
    // struct aesd_buffer_entry *next;
    if ((buffer->out_offs == buffer->in_offs) && !buffer->full)             // Empty
        return 0;

    // next = &buffer->entry[buffer->out_offs];
    // return (next->size - next->offset);

    return ((buffer->entry[buffer->out_offs]).size - (buffer->entry[buffer->out_offs]).offset);
}


/*
    Read part of data from next available buffer entry
    Entry is _not_ removed from buffer - only start pointer is updated
    Assumes that you've checked that enough data is available...
    
    Returns a copy of aesd_buffer_entry; _don't_ free the buffer memory after use!
*/
struct aesd_buffer_entry  aesd_circular_buffer_read_partial(struct aesd_circular_buffer *buffer, size_t count)
{
    struct aesd_buffer_entry retval = buffer->entry[buffer->out_offs];
    buffer->entry[buffer->out_offs].offset += count;

    return retval;
}


const char* aesd_circular_buffer_read(struct aesd_circular_buffer* buffer, size_t f_pos, size_t* avail)
{
    size_t buf_start_pos;
    uint8_t curbuf;
    
    if ((buffer->out_offs == buffer->in_offs) && !buffer->full)             // Empty
    {
        *avail = 0;
        return NULL;
    } 

    // Find buffer that contains requested offset
    buf_start_pos = 0;
    curbuf = buffer->out_offs;
    while (true)
    {
        if (f_pos < (buf_start_pos + buffer->entry[curbuf].size))
        {
            *avail = buffer->entry[curbuf].size - (f_pos - buf_start_pos);
            return buffer->entry[curbuf].buffptr + (f_pos - buf_start_pos);
        }

        buf_start_pos += buffer->entry[curbuf].size;
        curbuf += 1;
        if (curbuf == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) 
            curbuf = 0;

        if (curbuf == buffer->in_offs)              // Reached end
        {
            *avail = 0;
            return NULL;
        } 

    }








}








/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
