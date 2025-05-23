#include "filesys.h"

#include <string.h>
#include <assert.h>

#include "utility.h"
#include "debug.h"
#include <stdio.h>
#include <math.h>

#define INDIRECT_DBLOCK_INDEX_COUNT (DATA_BLOCK_SIZE / sizeof(dblock_index_t) - 1)
#define INDIRECT_DBLOCK_MAX_DATA_SIZE ( DATA_BLOCK_SIZE * INDIRECT_DBLOCK_INDEX_COUNT )

#define NEXT_INDIRECT_INDEX_OFFSET (DATA_BLOCK_SIZE - sizeof(dblock_index_t))

// ----------------------- UTILITY FUNCTION ----------------------- //

int min(int a, int b) {
    return (a < b) ? a : b;
}

// BOUNDS ARE INCLUSIVE
uint64_t combine_bytes_to_int(uint8_t *transfer, int start, int end, int size)
{
    if(size < end-start)
    {
        return 0;
    }
    
    uint64_t result = 0;
    // if the range includes the proper values mod 4
    // the result is combined from 8 bytes from the transfer array. We return result.
    if ((end - start + 1) % 8 == 0)
    {
        result |= (uint64_t) transfer[start];
        result |= ((uint64_t) transfer[start+1] << 8);
        result |= ((uint64_t) transfer[start+2] << 16);
        result |= ((uint64_t) transfer[start+3] << 24);
        result |= ((uint64_t) transfer[start+4] << 32);
        result |= ((uint64_t) transfer[start+5] << 40);
        result |= ((uint64_t) transfer[start+6] << 48);
        result |= ((uint64_t) transfer[start+7] << 56);
        return result;
    }
    return 0;
}

// ----------------------- CORE FUNCTION ----------------------- //

// write data will allocate new dblocks (if necessary) in the inode and copy data from void* data (an array) into the dblocks
// Assume we are appending to the existing data in the inode

// Writes n bytes of data from data to the data D-blocks of an inode. It allocates D-blocks as necessary.
// Data written is appended and does not modify any existing data in the inode.
// D-blocks are allocated as needed to store the data being written.
// Allocated D-blocks are stored in the direct D-blocks first before being stored indirectly via index D-blocks
// If there is not enough D-blocks available to store all the data, then the function should do nothing except return INSUFFICIENT_DBLOCKS. The state of the file system after the call to the function should be identical to the state of file system before the call.
// Will need to update inode->internal.file_size appropriately to reflect the new size of the file.
// This function should not claim any extra D-blocks than is needed.
// Possible return values are:
// If fs or inode is NULL, return INVALID_INPUT
// If there is not enough available D-blocks in the system to satisfy the request, return INSUFFICIENT_DBLOCKS
// If the data is successfully written, return SUCCESS

void free_dblock(filesystem_t *fs, dblock_index_t to_free)
{
    release_dblock(fs, &fs->dblocks[(to_free * 64)]);
}


void put_index_into_index_dblock(filesystem_t *fs, dblock_index_t address_of_claimed_block, int destination)
{
    byte first = address_of_claimed_block & 0xff;
    byte second = address_of_claimed_block & 0xff << 8;
    byte third = address_of_claimed_block & 0xff << 16;
    byte fourth = address_of_claimed_block & 0xff << 24;
    fs->dblocks[destination] = first;
    fs->dblocks[destination+1] = second;
    fs->dblocks[destination+2] = third;
    fs->dblocks[destination+3] = fourth;
    // //printf("dblocks: %2x %2x %2x %2x \n", first, second, third, fourth);
}

int write_to_dblock(filesystem_t *fs, int indx, byte *data, int start_index, size_t l)
{
    int real_indx_claimed = (int) indx;
    int n = (int) l;
    int count = 0;
    if(start_index < n)
    {
        int j = 0;
        for(int i = start_index; i < start_index+min(64, n-start_index); i++)
        {
            count++;
            fs->dblocks[real_indx_claimed + j] = data[i];
            j++;
        }
    }
    else
    {
        int j = 0;
        for(int i = 0; i < n - start_index; i++)
        {
            fs->dblocks[real_indx_claimed + j] = data[start_index + i];
            count++;
        }
    }

    return count;
}


dblock_index_t get_next_idx_dblock(filesystem_t *fs, dblock_index_t curr)
{
    int result = 0;
    result |= fs->dblocks[curr * 64 + 60];
    result |= fs->dblocks[curr * 64 + 61] << 8;
    result |= fs->dblocks[curr * 64 + 62] << 16;
    result |= fs->dblocks[curr * 64 + 63] << 24;
    return result;
}
dblock_index_t get_ith_indirect_dblock(filesystem_t *fs, dblock_index_t curr, int i)
{
    dblock_index_t result = 0;
    result |= fs->dblocks[curr * 64 + i*4];
    result |= fs->dblocks[curr * 64 + i*4+1];
    result |= fs->dblocks[curr * 64 + i*4+2];
    result |= fs->dblocks[curr * 64 + i*4+3];
    return result;
}

dblock_index_t travel_i_dblocks(filesystem_t *fs, int num_dblocks_curr, inode_t *inode, int offset)
{
    dblock_index_t curr = inode->internal.indirect_dblock;
    int num_indx_dblocks = calculate_index_dblock_amount(inode->internal.file_size) - 1;
    while(num_indx_dblocks > 0)
    {
        curr = get_next_idx_dblock(fs, curr);
        num_indx_dblocks--;
    }
    return get_ith_indirect_dblock(fs, curr, offset);
}
fs_retcode_t inode_write_data(filesystem_t *fs, inode_t *inode, void *data, size_t n)
{    
    if(!fs || !inode) return INVALID_INPUT;
    if(available_dblocks(fs) == 0) return INSUFFICIENT_DBLOCKS;

    size_t existing_size = inode->internal.file_size;
    if(inode->internal.file_size <= 256)
    {
        // pretend as if you're writing the existing data to the 4 direct datablocks 
        int num_dblocks_used = calculate_necessary_dblock_amount(existing_size);
        dblock_index_t idx_of_last_dblock_used = 0;
        
        if(num_dblocks_used <= 4)
        {
            // // // // // // // // // // // // // // display_filesystem(fs, DISPLAY_ALL);
            // printf("name: %s\n", inode->internal.file_name);
            if(num_dblocks_used == 0)
                idx_of_last_dblock_used = 0;
            else
                idx_of_last_dblock_used = num_dblocks_used-1;
            int where_written_data_ends = existing_size % 64;
            // if it evenly divides into 64 bytes, then that means that the whole block is fully written to.
            if(where_written_data_ends == 0 && num_dblocks_used > 0)
                where_written_data_ends = 64;
            uint64_t i = 0;
            if(existing_size == 0)
            {
                dblock_index_t *address_of_claimed_block = cast_dblock_ptr(malloc(sizeof(dblock_index_t)));
                claim_available_dblock(fs, address_of_claimed_block);
                inode->internal.direct_data[0] = *address_of_claimed_block;
            }
            while(where_written_data_ends < 64 && i < n)
            {
                fs->dblocks[inode->internal.direct_data[idx_of_last_dblock_used] * 64 + where_written_data_ends] = (byte)((byte *)data)[i];
                i++;
                where_written_data_ends++;
                // // // // // // // // // // // // // display_filesystem(fs, DISPLAY_ALL);

            }

            idx_of_last_dblock_used++;
            // // // // // // // // // // // // // // display_filesystem(fs, DISPLAY_ALL);

            while(idx_of_last_dblock_used < 4 && i < n)
            {
                dblock_index_t *address_of_claimed_block = cast_dblock_ptr(malloc(sizeof(dblock_index_t)));
                claim_available_dblock(fs, address_of_claimed_block);
                // i am writing to a dblock like a fucking idiot, the error is here, otherwise everything works well
                write_to_dblock(fs, *address_of_claimed_block * 64, data, i, n);
                i+= 64;
                inode->internal.direct_data[idx_of_last_dblock_used] = *address_of_claimed_block;
                idx_of_last_dblock_used++;
                // // // // // // // // // // // // // display_filesystem(fs, DISPLAY_ALL);
            }
            // // // // // // // // // // // // // // display_filesystem(fs, DISPLAY_ALL);
            // now we are going to allocate this index datablock
            if (i < n)
            {
                // // // // // // // // // // // // // // display_filesystem(fs, DISPLAY_ALL);
                dblock_index_t *address_of_claimed_index_block = cast_dblock_ptr(malloc(sizeof(dblock_index_t)));
                claim_available_dblock(fs, address_of_claimed_index_block);
                inode->internal.indirect_dblock = *address_of_claimed_index_block;
                while(i < n) //while i still have data to write
                {
                    // // // // // // // // // // // // // // display_filesystem(fs, DISPLAY_ALL);
                    for(int count = 0; count < 16; count++)
                    {
                        dblock_index_t *address_of_claimed_block = cast_dblock_ptr(malloc(sizeof(dblock_index_t)));
                        claim_available_dblock(fs, address_of_claimed_block);
                        put_index_into_index_dblock(fs, *address_of_claimed_block, *address_of_claimed_index_block*64+count*4);
                        write_to_dblock(fs, *address_of_claimed_block * 64, data, i, n);
                        i+=64;
                        // // // // // // // // // // // // // // display_filesystem(fs, DISPLAY_ALL);
                        if(i >= n) break;
                    }
                }
            }
            
        }
    }
    else
    {
        int num_of_idx_dblocks = calculate_index_dblock_amount(inode->internal.file_size);
        dblock_index_t curr = inode->internal.indirect_dblock;
        num_of_idx_dblocks--;
        while(num_of_idx_dblocks > 0)
        {
            curr = get_next_idx_dblock(fs, curr);
            printf("Curr: %d\n Num Index remaining: %d\n", curr, num_of_idx_dblocks);
            num_of_idx_dblocks--;
        }
        // // // // // // // // // // // // // display_filesystem(fs, DISPLAY_ALL);

        // this is the idx of the last datablock in the current index datablock we are using. IT IS AN INT, NOT A DBLOCK INDEX
        // THESE IMAGINARY INDICES RANGE FROM 0 TO 16
        int offset_within_curr = (calculate_necessary_dblock_amount(inode->internal.file_size) - 4 - calculate_index_dblock_amount(inode->internal.file_size)) % 16;
        int offset_within_dblock = inode->internal.file_size % 64;
        if(offset_within_dblock == 0)
        {
            offset_within_dblock = 64;
        }
        uint64_t i = 0;
        while(offset_within_dblock < 64 && i < n)
        {
            fs->dblocks[curr * 64 + offset_within_curr * 4] = (byte)((byte *)data)[i];
            i++;
            offset_within_dblock++;
            // // // // // // // // // // // // // display_filesystem(fs, DISPLAY_ALL);
        }
        // if i still have more data to write:
        offset_within_curr++;
        while(i < n)
        {
            // // // // // // // // // // // // display_filesystem(fs, DISPLAY_ALL);
            // at this point, my last datablock that i used must be filled in
            int count = offset_within_curr;
            for(; count < 15; count++)
            {
                dblock_index_t *address_of_claimed_block = cast_dblock_ptr(malloc(sizeof(dblock_index_t)));
                claim_available_dblock(fs, address_of_claimed_block);
                put_index_into_index_dblock(fs, *address_of_claimed_block, curr*64+count*4);
                write_to_dblock(fs, *address_of_claimed_block * 64, data, i, n);
                i+=64;
                if(i >= n) break;
            }
            if (i < n)
            {
                dblock_index_t *address_of_claimed_index_block = cast_dblock_ptr(malloc(sizeof(dblock_index_t)));
                claim_available_dblock(fs, address_of_claimed_index_block);
                put_index_into_index_dblock(fs, *address_of_claimed_index_block, curr*64+count*4);
                offset_within_curr = 0;
                curr = *address_of_claimed_index_block;
            }
            count = 0;
        }
    }
    inode->internal.file_size += n;
    return SUCCESS;
}


// Reads n bytes of data starting from offset bytes from the beginning of the contents of inode. Stores this data in buffer.
// If there are not n bytes of data starting from offset, only read the number of bytes until the end of the inode.
// Set bytes_read to the number of bytes actually read by the function. This should be the number of bytes written to buffer as well.
// Possible Return values are:
// If fs, inode, or bytes_read is NULL, return INVALID_INPUT
// If the read operation was successful, return SUCCESS
fs_retcode_t inode_read_data(filesystem_t *fs, inode_t *inode, size_t offset, void *buffer, size_t n, size_t *bytes_read)
{
    //first get to the offset of bytes, then read from there
    byte *transfer = (byte *)buffer;
    if(!fs || !bytes_read || !inode) return INVALID_INPUT;
    int read = 0;
    if (n >= inode->internal.file_size - offset) n = inode->internal.file_size - offset;
    
    if(offset <= 256)
    {
        int num_dblocks_used = calculate_necessary_dblock_amount(offset);
        int index_in_dblock_array = num_dblocks_used-1;
        if (offset == 0)
            index_in_dblock_array = 0;
        
        int offset_within_dblock = offset % 64;
        if(offset_within_dblock == 0 && num_dblocks_used > 0)
            offset_within_dblock = 64;
        
        while(read < min(n, inode->internal.file_size) && index_in_dblock_array < 4)
        {
            while(read < min(n, inode->internal.file_size) && offset_within_dblock < 64)
            {
                transfer[read] = fs->dblocks[inode->internal.direct_data[index_in_dblock_array] * 64 + offset_within_dblock];
                offset_within_dblock++;
                read++;
            }
            offset_within_dblock = 0;
            index_in_dblock_array++;
        }
        if((unsigned) read < n)
        {
            dblock_index_t curr = inode->internal.indirect_dblock;
            int i = 0;
            // do this until youve read what you needed to read
            while((unsigned) read < n)
            {
                // read every indirect block in the index dblock
                while(i < 16)
                {
                    // get the indirect datablock
                    dblock_index_t ith_indirect = get_ith_indirect_dblock(fs, curr, i);
                    // loop through it and copy all of the data
                    int offset_bc_of_terrible_implementation = read;
                    // error here!
                    for(int j = offset_bc_of_terrible_implementation; j < offset_bc_of_terrible_implementation + min(64, n - offset_bc_of_terrible_implementation); j++)
                    {
                        transfer[j] = fs->dblocks[ith_indirect*64 + j-offset_bc_of_terrible_implementation];
                        read++;
                    }
                    // if everything is read, break
                    if(read>=min(n, inode->internal.file_size))
                        break;
                    // increment
                    i++;
                }
                // increment current to the next index block
                curr = get_ith_indirect_dblock(fs, curr, 16);
                i=0;
            }
        }

    }
    else
    {
        offset-=256;
        n = min(n, inode->internal.file_size);
        dblock_index_t curr = inode->internal.indirect_dblock;
        int num_of_idx_dblocks = calculate_index_dblock_amount(offset) - 1;

        while(num_of_idx_dblocks > 0)
        {
            curr = get_next_idx_dblock(fs, curr);
            num_of_idx_dblocks--;
        }

        int offset_within_dblock = offset % 64;
        if(offset_within_dblock == 0)
            offset_within_dblock = 64;
        int offset_within_curr = (calculate_necessary_dblock_amount(offset) - calculate_index_dblock_amount(offset)) % 16;
        if (offset_within_curr != 0)
            offset_within_curr-=1;
        curr = get_ith_indirect_dblock(fs, curr, offset_within_curr);
        while((unsigned) read < n && offset_within_dblock < 64)
        {
            transfer[read] = fs->dblocks[curr * 64 + offset_within_dblock];
            offset_within_dblock++;
            read++;
        }
        offset_within_dblock = 0;
        offset_within_curr++;
        while( (unsigned) read < n)
        {
            while((unsigned) read < n && offset_within_curr < 15)
            {
                dblock_index_t ith_indirect = get_ith_indirect_dblock(fs, curr, offset_within_curr);
                while((unsigned) read < n && offset_within_dblock < 64)
                {
                    transfer[read] = fs->dblocks[ith_indirect * 64 + offset_within_dblock];
                    offset_within_dblock++;
                    read++;
                }
                offset_within_dblock = 0;
                offset_within_curr++;
            }
            curr = get_ith_indirect_dblock(fs, curr, 16);
        }
        // if(read < min(n, inode->internal.file_size))
        // {
        //     dblock_index_t curr = inode->internal.indirect_dblock;
        //     int i = (calculate_necessary_dblock_amount(offset) - calculate_index_dblock_amount(offset)) % 16;
        //     int offset_within_dblock = offset % 64;
        //     // do this until youve read what you needed to read
        //     while(read < min(n, inode->internal.file_size))
        //     {
        //         // read every indirect block in the index dblock
        //         while(i < 16)
        //         {
        //             // get the indirect datablock
        //             dblock_index_t ith_indirect = get_ith_indirect_dblock(fs, curr, i);
        //             // loop through it and copy all of the data
        //             int offset_bc_of_terrible_implementation = read;
        //             // error here!
        //             for(int j = offset_bc_of_terrible_implementation; j < offset_bc_of_terrible_implementation + offset_within_dblock + min(64, n - offset_bc_of_terrible_implementation); j++)
        //             {
        //                 transfer[j] = fs->dblocks[ith_indirect*64 + j-offset_bc_of_terrible_implementation + offset_within_dblock];
        //                 read++;
        //             }
        //             printf("\n\n");
        //             for(int j = 0; j < read; j++)
        //                 printf("%x ", transfer[j]);
        //             // if everything is read, break
        //             if(read>=min(n, inode->internal.file_size))
        //                 break;
        //             // increment
        //             i++;
        //         }
        //         // increment current to the next index block
        //         curr = get_ith_indirect_dblock(fs, curr, 16);
        //         i=0;
        //     }
        // }
    }
    buffer = transfer;
    *bytes_read = read;
    return SUCCESS;
    
    //check to make sure inputs are valid

    //for 0 to n, use the helper function to read and copy 1 byte at a time
}

fs_retcode_t inode_modify_data(filesystem_t *fs, inode_t *inode, size_t offset, void *buffer, size_t n)
{
    byte *transfer = (byte *)buffer;
    if(!fs || !inode || !buffer) return INVALID_INPUT;
    if (offset > inode->internal.file_size) return INVALID_INPUT;
    int num_to_reserve = (int) n - ((int) inode->internal.file_size - (int) offset);
    if (num_to_reserve < 0)
        num_to_reserve = 0;
        
    if (available_dblocks(fs) < calculate_necessary_dblock_amount(num_to_reserve)) 
        return INSUFFICIENT_DBLOCKS;

    // display_filesystem(fs, DISPLAY_ALL);

    if(offset <= 256)
    {
        uint64_t i = 0;
        int num_dblocks_used = calculate_necessary_dblock_amount(offset);
        int num_dblocks_reserved = calculate_necessary_dblock_amount(inode->internal.file_size);
        int index_in_dblock_array = num_dblocks_used-1;
        if (offset == 0)
            index_in_dblock_array = 0;
        
        int offset_within_dblock = offset % 64;
        if(offset_within_dblock == 0 && num_dblocks_used > 0)
            offset_within_dblock = 64;
        
        dblock_index_t curr = inode->internal.direct_data[index_in_dblock_array];

        while(offset_within_dblock < 64 && i < n)
        {
            fs->dblocks[curr * 64 + offset_within_dblock] = (byte)((byte *)transfer)[i];
            i++;
            offset_within_dblock++;
            // display_filesystem(fs, DISPLAY_ALL);
        }
        index_in_dblock_array++;
        offset_within_dblock = 0;
        // ive now filled in the datablock that was last used.
        while(index_in_dblock_array < 4 && i < n)
        {
            curr = inode->internal.direct_data[index_in_dblock_array];
            while(offset_within_dblock < 64 && i < n)
            {
                fs->dblocks[curr * 64 + offset_within_dblock] = (byte)((byte *)transfer)[i];
                i++;
                offset_within_dblock++;
                // display_filesystem(fs, DISPLAY_ALL);
            }
            index_in_dblock_array++;
            offset_within_dblock = 0;
            if (index_in_dblock_array >= 4)
                break;
            // if need to reserve direct data blocks, reserve them
            if(index_in_dblock_array > num_dblocks_reserved - 1)
            {
                dblock_index_t *address_of_claimed_block = cast_dblock_ptr(malloc(sizeof(dblock_index_t)));
                claim_available_dblock(fs, address_of_claimed_block);
                inode->internal.direct_data[index_in_dblock_array] = *address_of_claimed_block;
                i+= write_to_dblock(fs, *address_of_claimed_block * 64, transfer, i, n);
                if(i >= n) break;
                // display_filesystem(fs, DISPLAY_ALL);
            }
        }

        //now, if i still have more data to write, i just do the same exact thing as in the else branch
        if (i < n)
        {
            // int num_of_idx_dblocks = calculate_index_dblock_amount(offset);
            if(inode->internal.file_size <= 256)
            {
                dblock_index_t *address_of_claimed_index_block = cast_dblock_ptr(malloc(sizeof(dblock_index_t)));
                claim_available_dblock(fs, address_of_claimed_index_block);
                inode->internal.indirect_dblock = *address_of_claimed_index_block;
            }
            dblock_index_t curr = inode->internal.indirect_dblock;
            dblock_index_t curr_indx_block = curr;
            // display_filesystem(fs, DISPLAY_ALL);
            // printf("curr: %d\n", curr);

            int num_indirect_dblocks = calculate_necessary_dblock_amount(offset) - 4 - calculate_index_dblock_amount(offset);
            if(num_indirect_dblocks < 0)
                num_indirect_dblocks = 0;

            int offset_within_curr = num_indirect_dblocks % 15;

            if (offset_within_curr != 0)
                offset_within_curr-=1;

            curr = get_ith_indirect_dblock(fs, curr_indx_block, offset_within_curr);
            int num_allocated = (calculate_necessary_dblock_amount(inode->internal.file_size) - 4 - calculate_index_dblock_amount(inode->internal.file_size));
            int blocks_used_by_offset = (calculate_necessary_dblock_amount(offset) - 4 - calculate_index_dblock_amount(offset));
            if(blocks_used_by_offset < 0)
                blocks_used_by_offset = 0;
            while(i < n && blocks_used_by_offset < num_allocated)
            {
                for(; offset_within_curr < 15; offset_within_curr++)
                {
                    if(blocks_used_by_offset >= num_allocated) break;
                    curr = get_ith_indirect_dblock(fs, curr_indx_block, offset_within_curr);
                    i += write_to_dblock(fs, curr * 64, transfer, i, n);
                    blocks_used_by_offset++;
                    // display_filesystem(fs, DISPLAY_ALL);
                }

                if (i < n && blocks_used_by_offset < num_allocated)
                {
                    curr_indx_block = get_next_idx_dblock(fs, curr_indx_block);
                    offset_within_curr = 0;
                    curr = get_ith_indirect_dblock(fs, curr_indx_block, offset_within_curr);
                }
                else break;
            }

            // now ive used all my allocated blocks, i must start allocating new data
            while(i < n)
            {
                for(; offset_within_curr < 15; offset_within_curr++)
                {
                    // i reserve the space and write at most 64 bytes
                    dblock_index_t *address_of_claimed_block = cast_dblock_ptr(malloc(sizeof(dblock_index_t)));
                    claim_available_dblock(fs, address_of_claimed_block);
                    put_index_into_index_dblock(fs, *address_of_claimed_block, curr_indx_block*64 + offset_within_curr*4);
                    i += write_to_dblock(fs, *address_of_claimed_block * 64, transfer, i, n);
                    if(i >= n) break;
                    // display_filesystem(fs, DISPLAY_ALL);
                }
                if (i < n)
                {
                    dblock_index_t *address_of_claimed_index_block = cast_dblock_ptr(malloc(sizeof(dblock_index_t)));
                    claim_available_dblock(fs, address_of_claimed_index_block);
                    put_index_into_index_dblock(fs, *address_of_claimed_index_block, curr_indx_block*64 + offset_within_curr*4);
                    offset_within_curr = 0;
                    curr_indx_block = *address_of_claimed_index_block;
                    // display_filesystem(fs, DISPLAY_ALL);
                }
                offset_within_curr = 0;
                // display_filesystem(fs, DISPLAY_ALL);
            }
        }
    }
    else
    {
        int num_of_idx_dblocks = calculate_index_dblock_amount(offset);
        dblock_index_t curr = inode->internal.indirect_dblock;
        num_of_idx_dblocks--;
        while(num_of_idx_dblocks > 0)
        {
            curr = get_next_idx_dblock(fs, curr);
            num_of_idx_dblocks--;
        }
        dblock_index_t curr_indx_block = curr;
        // display_filesystem(fs, DISPLAY_ALL);
        // printf("curr: %d\n", curr);

        int num_indirect_dblocks = calculate_necessary_dblock_amount(offset) - 4 - calculate_index_dblock_amount(offset);
        int offset_within_curr = num_indirect_dblocks % 15;
        if (offset_within_curr != 0)
            offset_within_curr-=1;

        int offset_within_dblock = offset % 64;
        if(offset_within_dblock == 0)
            offset_within_dblock = 64;
        
        uint64_t i = 0;
        curr = get_ith_indirect_dblock(fs, curr_indx_block, offset_within_curr);
        while(offset_within_dblock < 64 && i < n)
        {
            fs->dblocks[curr * 64 + offset_within_dblock] = (byte)((byte *)transfer)[i];
            i++;
            offset_within_dblock++;
            // display_filesystem(fs, DISPLAY_ALL);
        }
        // now i filled in my last indirect dblock


        offset_within_curr++;
        int num_allocated = (calculate_necessary_dblock_amount(inode->internal.file_size) - 4 - calculate_index_dblock_amount(inode->internal.file_size));
        int blocks_used_by_offset = (calculate_necessary_dblock_amount(offset) - 4 - calculate_index_dblock_amount(offset));

        while(i < n && blocks_used_by_offset < num_allocated)
        {
            for(; offset_within_curr < 15; offset_within_curr++)
            {
                if(blocks_used_by_offset >= num_allocated) break;
                curr = get_ith_indirect_dblock(fs, curr_indx_block, offset_within_curr);
                i += write_to_dblock(fs, curr * 64, transfer, i, n);
                blocks_used_by_offset++;
                // display_filesystem(fs, DISPLAY_ALL);
            }

            if (i < n && blocks_used_by_offset < num_allocated)
            {
                curr_indx_block = get_next_idx_dblock(fs, curr_indx_block);
                offset_within_curr = 0;
                curr = get_ith_indirect_dblock(fs, curr_indx_block, offset_within_curr);
            }
            else break;
        }

        // now ive used all my allocated blocks, i must start allocating new data
        while(i < n)
        {
            for(; offset_within_curr < 15; offset_within_curr++)
            {
                // i reserve the space and write at most 64 bytes
                dblock_index_t *address_of_claimed_block = cast_dblock_ptr(malloc(sizeof(dblock_index_t)));
                claim_available_dblock(fs, address_of_claimed_block);
                put_index_into_index_dblock(fs, *address_of_claimed_block, curr_indx_block*64 + offset_within_curr*4);
                i += write_to_dblock(fs, *address_of_claimed_block * 64, transfer, i, n);
                if(i >= n) break;
                // display_filesystem(fs, DISPLAY_ALL);
            }
            if (i < n)
            {
                dblock_index_t *address_of_claimed_index_block = cast_dblock_ptr(malloc(sizeof(dblock_index_t)));
                claim_available_dblock(fs, address_of_claimed_index_block);
                put_index_into_index_dblock(fs, *address_of_claimed_index_block, curr_indx_block*64 + offset_within_curr*4);
                offset_within_curr = 0;
                curr_indx_block = *address_of_claimed_index_block;
                // display_filesystem(fs, DISPLAY_ALL);
            }
            offset_within_curr = 0;
            // display_filesystem(fs, DISPLAY_ALL);
        }
    }
    // (void)fs;
    inode->internal.file_size += num_to_reserve;
    // (void)offset;
    // (void)buffer;
    // (void)n;
    return SUCCESS;

    //check to see if the input is valid

    //calculate the final filesize and verify there are enough blocks to support it
    //use calculate_necessary_dblock_amount and available_dblocks


    //Write to existing data in your inode

    //For the new data, call "inode_write_data" and return
}

fs_retcode_t inode_shrink_data(filesystem_t *fs, inode_t *inode, size_t new_size)
{
    
    if(!fs || !inode) return INVALID_INPUT;
    if(new_size > inode->internal.file_size) return INVALID_INPUT;
    if(inode->internal.file_size == 0) return SUCCESS;
    int num_dblocks_new = calculate_necessary_dblock_amount(new_size);
    int num_dblocks_curr = calculate_necessary_dblock_amount(inode->internal.file_size);
    if(num_dblocks_curr == 1)
    {

        inode->internal.file_size = new_size;
        release_dblock(fs, &fs->dblocks[inode->internal.direct_data[0] * 64]);
        return SUCCESS;
    }

    // int to_remove = num_dblocks_curr - num_dblocks_new;
    // first need to get to last dblock of the data
    while(num_dblocks_curr > 4 && num_dblocks_curr != num_dblocks_new)
    {
        int offset = ((calculate_necessary_dblock_amount(inode->internal.file_size) - 4 - calculate_index_dblock_amount(inode->internal.file_size)) % 16);
        
        dblock_index_t curr = travel_i_dblocks(fs, num_dblocks_curr, inode, offset);
        free_dblock(fs, curr);
        inode->internal.file_size -= 64;
        num_dblocks_curr--;
        if (num_dblocks_curr <= 4)
            free_dblock(fs, inode->internal.indirect_dblock);
    }
    if(inode->internal.file_size == 0)
    {
        free_dblock(fs, inode->internal.direct_data[0]);
        inode->internal.file_size = 0;
        return SUCCESS;
    }
    num_dblocks_curr--;
    // display_filesystem(fs, DISPLAY_ALL);
    if(num_dblocks_new != 0)
        num_dblocks_new--;
    int curr = inode->internal.direct_data[num_dblocks_curr];

    while(num_dblocks_new != num_dblocks_curr)
    {
        free_dblock(fs, curr);
        num_dblocks_curr--;
        curr = inode->internal.direct_data[num_dblocks_curr];
    }
    
    inode->internal.file_size = new_size;
    if (inode->internal.file_size == 0)
    {
        // //display_filesystem(fs, DISPLAY_INODES);
        free_dblock(fs, inode->internal.direct_data[0]);
        // display_filesystem(fs, DISPLAY_INODES);

    }
    return SUCCESS;
    //check to see if inputs are in valid range

    //Calculate how many blocks to remove

    //helper function to free all indirect blocks

    //remove the remaining direct dblocks

    //update filesize and return


}

// make new_size to 0
fs_retcode_t inode_release_data(filesystem_t *fs, inode_t *inode)
{

    return inode_shrink_data(fs, inode, 0);
    
    //shrink to size 0
}
