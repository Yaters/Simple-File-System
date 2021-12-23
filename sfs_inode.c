#include "sfs_inode.h"
#include "disk_emu.h"

#include <stdio.h>
#include <string.h>


// Private helper method to add an entry to the table - returns the index
static int addFDTEntry(iNode node, int inode_idx) {
    // Find an open fdt slot
    int fdt_index = -1;
    for(int i = 0; i < fdt.allocated; i++) {
        if(fdt.table[i].inode_idx < 0) { 
            fdt_index = i;
            break;
        }
    }

    // No slot found
    if(fdt_index < 0) {
        fdt.allocated += 3;

        iNode* new_inodes = realloc(fdt.inodes, fdt.allocated * sizeof(iNode));
        FDTEntry* new_fdt = realloc(fdt.table, fdt.allocated * sizeof(FDTEntry));
        if(new_inodes == NULL || new_fdt == NULL) {
            fprintf(stderr, "ERROR: Unable to allocate File Descriptor Table cache memory!\n");
            return -1;
        }  else {
            fdt.inodes = new_inodes;
            fdt.table = new_fdt;
                // I went for manually, we created 3 new entries
            fdt.table[fdt.allocated - 3].inode_idx = -1;
            fdt.table[fdt.allocated - 2].inode_idx = -1;
            fdt.table[fdt.allocated - 1].inode_idx = -1;
            fdt_index = fdt.size;
        }
    }
    // Set values
    fdt.inodes[fdt_index] = node;
    fdt.table[fdt_index] = (FDTEntry) {.inode_idx = inode_idx, .readPointer = 0, .writePointer = node.size};
    fdt.size++;
    return fdt_index;
}

// Helper - saves the iNode of the given fdt entry back to disk
static void saveFDTNode(int fdt_index) {
    int block_location  = fdt.table[fdt_index].inode_idx / INODES_PER_BLOCK;
    int local_block_loc = fdt.table[fdt_index].inode_idx % INODES_PER_BLOCK;

    iNode* node_list = malloc(super_block.block_size);
    read_blocks(1 + block_location, 1, node_list); // +1 to pass super block
    node_list[local_block_loc] = fdt.inodes[fdt_index];
    write_blocks(1 + block_location, 1, node_list);
    free(node_list);
}

// Load (or find) an inode from disk into the file descriptor table - returns FDT index
int openFDTNode(int inode_index) {
    // First check if we already have it in the fdt - avoid re-opening
    int fdt_index = -1;
    for(int i = 0; i < fdt.allocated; i++) {
        if(fdt.table[i].inode_idx == inode_index) {
            return i;
        }
    }

    // Read node from disk
    int block_location  = inode_index / INODES_PER_BLOCK;
    int local_block_loc = inode_index % INODES_PER_BLOCK;
    iNode* node_list = (iNode*) malloc(super_block.block_size);
    read_blocks(block_location + 1, 1, node_list); //+1 to pass super block
    fdt_index = addFDTEntry(node_list[local_block_loc], inode_index);
    free(node_list);
    
    return fdt_index;
}

// DOES NOT SAVE - saving should be done continuously over program
void closeFDTNode(int fdt_index) {
    fdt.size--;
    fdt.table[fdt_index].inode_idx = -1;

    // Seeing if we can repackage (close up some mem) on FDT
    int last_index = fdt.allocated - 1;
    for(; last_index >= fdt.size; last_index--) {
        if(fdt.table[last_index].inode_idx >= 0) break; // Break on first uncleared FDT entry
    }
    int dec_count = (fdt.allocated-1 - last_index) / 3; // Number of free spaces (in chunks of 3)
    if (dec_count > 0 ) {
        fdt.allocated -= 3*dec_count;
        iNode* new_inodes = realloc(fdt.inodes, fdt.allocated * sizeof(iNode));
        FDTEntry* new_fdt = realloc(fdt.table, fdt.allocated * sizeof(FDTEntry));
        if(new_inodes == NULL || new_fdt == NULL) {
            fprintf(stderr, "ERROR: Unable to deallocate iNode cache memory!\n");
        } else {
            fdt.inodes = new_inodes;
            fdt.table = new_fdt;
        }
    }
}

// Massive helper method - fill the int buffer w/ block location of the given node's data blocks
// Creates blocks if != exist. Returns number of already existing blocks.
static int getNodeDataBlockList(iNode* node, int start_block, int last_block, int* disk_data_idxs) {
    // Lots of initial variables to reuse allocated buffers. i = indirect, di= double indirect
    int* i_buff = NULL; bool first_i =  true;
    int* di_buff[2] = {NULL, NULL}; bool first_di[2] =  {true, true}; 
    int last_di_level_1 = -1; bool wrote_di_level[2] = {false, false}; // helpful for double indirect
    int num_existing  = __INT_MAX__;

    int i = 0; int cur = start_block;
    for(; cur <= last_block; i++, cur++) {

        // Need to grab a new data block
        if(cur >= node->blocks_allocated) {
            if(i < num_existing) num_existing = i;
            disk_data_idxs[i] = grab_data_bit();
            if(disk_data_idxs[i] < 0) break;
            if(cur < 12) {
                node->direct_pointer[cur] = disk_data_idxs[i];
            } else if (cur < 12 + POINTERS_PER_BLOCK) {
                if(first_i) {
                    i_buff = malloc(super_block.block_size);
                    // Create indirect pointer or load existing to buffer
                    if(cur == 12) {
                        node->indirect_pointer = grab_data_bit();
                        if(node->indirect_pointer < 0) break;
                    } else read_blocks(node->indirect_pointer, 1, i_buff);
                    first_i = false;
                }
                i_buff[cur - 12] = disk_data_idxs[i];
            } else {
                // Level 1 = block pointed to by double indirect (holds indirect block pointers)
                // Level 2 = indirect block (holds data block pointers)
                int level_1 = cur - 12 - POINTERS_PER_BLOCK; 
                int level_2 = level_1 % POINTERS_PER_BLOCK; // current index of level 2
                level_1 = level_1 / POINTERS_PER_BLOCK; // current index of level 1
                // Fill level-1 buffer
                if(first_di[0]) {
                    di_buff[0] = malloc(super_block.block_size);
                    // Either create the level 1 block or read it
                    if(level_1 == 0) {
                        node->double_indirect_pointer = grab_data_bit();
                        if(node->double_indirect_pointer < 0) break;
                    } else read_blocks(node->double_indirect_pointer, 1, di_buff[0]);
                    first_di[0] = false;
                }
                // Fill level-2 buffer (whenever level-1 index changes)
                if(level_1 != last_di_level_1) {
                    // First time = malloc. Otherwise save before overwrite local
                    if(first_di[1]) {
                        di_buff[1] = malloc(super_block.block_size);
                        first_di[1] = false;
                    } else if(wrote_di_level[1]) {
                        write_blocks(di_buff[0][last_di_level_1], 1, di_buff[1]);
                    }
                    // Either create level 2 block or read it
                    if(level_2 == 0) {
                        di_buff[0][level_1] = grab_data_bit(); 
                        if(di_buff[0][level_1] < 0) break;
                        wrote_di_level[0] = true; // Updated the level 1 block
                    } else read_blocks(di_buff[0][level_1], 1, di_buff[1]);
                    last_di_level_1 = level_1;
                }
                wrote_di_level[1] = true;
                di_buff[1][level_2] = disk_data_idxs[i];
            }

        // We have an data block already
        } else {
            if(cur < 12 ) {
                disk_data_idxs[i] = node->direct_pointer[cur];
            } else if (cur < 12 + POINTERS_PER_BLOCK) {
                if(first_i) {
                    i_buff = malloc(super_block.block_size);
                    read_blocks(node->indirect_pointer, 1, i_buff);
                    first_i = false;
                }
                disk_data_idxs[i] = i_buff[cur - 12];
            } else {
                int level_1 = cur - 12 - POINTERS_PER_BLOCK;
                int level_2 = level_1 % POINTERS_PER_BLOCK;
                level_1 = level_1 / POINTERS_PER_BLOCK;
                // Fill level-1 buffer
                if(first_di[0]) {
                    di_buff[0] = malloc(super_block.block_size);
                    read_blocks(node->double_indirect_pointer, 1, di_buff[0]);
                    first_di[0] = false;
                }
                // Fill level-2 buffer (whenever level-1 index changes)
                if(level_1 != last_di_level_1) {
                    if(first_di[1]) {
                        di_buff[1] = malloc(super_block.block_size);
                        first_di[1] = false;
                    }
                    read_blocks(di_buff[0][level_1], 1, di_buff[1]);
                    last_di_level_1 = level_1;
                }
                disk_data_idxs[i] = di_buff[1][level_2];
            }
        }
    }

    // If we wrote new data blocks
    if(cur >= node->blocks_allocated) {
        if(!first_i) write_blocks(node->indirect_pointer, 1, i_buff); 
        if(wrote_di_level[0]) write_blocks(node->double_indirect_pointer, 1, di_buff[0]);
        if(wrote_di_level[1]) write_blocks(di_buff[0][last_di_level_1], 1, di_buff[1]);
        saveFreeBitMapToDisk();
        node->blocks_allocated = cur;
    }

    // Broke out early
    if (cur <= last_block) {
        // Must communicate the problem. If ==0, blocks allocated indicate cancel write anyways
        if(num_existing == 0) num_existing = -1;
        else num_existing *= -1;
        disk_data_idxs[i] = -1;
    }

    free(i_buff);
    free(di_buff[0]);
    free(di_buff[1]);

    return num_existing;
}


// Will place the iNode within the open file descriptor table
int createINode(bool is_directory) {
    // Allocate new inode block
    int idx = grab_inode_bit();
    if(idx < 0) return idx;
    saveFreeBitMapToDisk();


    int fdt_index = addFDTEntry((iNode) {0}, idx);
    fdt.inodes[fdt_index].is_directory = is_directory;
    fdt.inodes[fdt_index].file_id = ++MAX_FILE_ID;
    // Still unused = link_count, uid, gid
    saveFDTNode(fdt_index); // Save iNode block to disk

    return fdt_index;
}



// Returns deleted node
FDTEntry deleteINode(int fdt_index) {
    FDTEntry old = fdt.table[fdt_index];
    iNode old_node = fdt.inodes[fdt_index];

    // Loop through list of data block indices and free them in bit map
    int* list_buffer = malloc(sizeof(int) * old_node.blocks_allocated);
    getNodeDataBlockList(fdt.inodes + fdt_index, 0, old_node.blocks_allocated - 1, list_buffer);
    for(int i = 0; i < old_node.blocks_allocated; i++) {
        free_data_bit(list_buffer[i]); 
    }
    if(old_node.blocks_allocated > 12) free_data_bit(old_node.indirect_pointer);
    if(old_node.blocks_allocated > 12 + POINTERS_PER_BLOCK) {
        // Have to free all indirects & the double indirect
        int* double_buffer = malloc(super_block.block_size);
        read_blocks(old_node.double_indirect_pointer, 1, double_buffer);

        int num_indirects = old_node.blocks_allocated - 12 - POINTERS_PER_BLOCK;
        num_indirects = (num_indirects / POINTERS_PER_BLOCK) + 
                        (num_indirects % POINTERS_PER_BLOCK != 0); // Ceiling division
        for(int i = 0; i < num_indirects; i++) {
            free_data_bit(double_buffer[i]); 
        }
        free_data_bit(old_node.double_indirect_pointer);
        free(double_buffer);
    }
    free(list_buffer);
    free_inode_bit(old.inode_idx);
    
    // Update cache bitmap changes back
    saveFreeBitMapToDisk();

    // Update fdt
    closeFDTNode(fdt_index);

    return old;
}

// Write data into inode_e's iNode, overwriting based on write pointer
long overwriteData(int fdt_index, const void* data_buffer, long data_size) {
    char* data = (char*) data_buffer;
    FDTEntry* fdt_e = fdt.table + fdt_index;
    iNode* node = fdt.inodes + fdt_index;

    // Check for writing past max file size (clamp to max file size if so)
    int last_write_block = (int) ((fdt_e->writePointer + data_size - 1) / super_block.block_size); // ex wP=0, data_size = block_size
    if(last_write_block >= MAX_FILE_BLOCKS) {
        data_size = ((long) MAX_FILE_BLOCKS)*super_block.block_size - fdt_e->writePointer;
        last_write_block = MAX_FILE_BLOCKS - 1; // zero-index
    }
    if(data_size <= 0) return 0;

    // Fill an indices array of data blocks written
    int cur_write_block = (int) (fdt_e->writePointer / super_block.block_size);
    int blocks_written = last_write_block - cur_write_block + 1;
    int* disk_data_idxs = malloc(sizeof(int) * blocks_written);
    int num_existing = getNodeDataBlockList(node, cur_write_block, last_write_block, disk_data_idxs);
    
    if(num_existing < 0) {
        // Not all blocks could be created. See how many were created & adjust
        num_existing *= -1;
        data_size = ((long) node->blocks_allocated)*super_block.block_size - fdt_e->writePointer;
    }

    if(data_size <= 0) return 0;
    long bytes_added = (fdt_e->writePointer + data_size) - node->size; // bytes appended to end of file
    if(bytes_added <= 0) bytes_added = 0;
    node->size += bytes_added;

    // Use data block indices array to fill data blocks with data (pure overwrite)
    int write_block_local = fdt_e->writePointer % super_block.block_size; // offset in block
    int left_to_write = data_size; // Amount of data left to be written in
    int remaining_size = super_block.block_size - write_block_local; // space left in block to write
    if(data_size < remaining_size) remaining_size = data_size;
    char* block_buffer = malloc(super_block.block_size);

    // Actual write operation
    for(int i = 0; left_to_write > 0; i++) {
            // Save existing if unwritten data in block
            if(i < num_existing && (write_block_local != 0 || left_to_write < super_block.block_size)) {
                read_blocks(disk_data_idxs[i], 1, block_buffer);
            }
            memcpy(block_buffer + write_block_local, data, remaining_size);
            write_blocks(disk_data_idxs[i], 1, block_buffer);

            // Update internal vals
            data += remaining_size; left_to_write -= remaining_size;
            write_block_local = 0;
            remaining_size = (left_to_write < super_block.block_size) ? left_to_write : super_block.block_size;
    }

    free(block_buffer);
    free(disk_data_idxs);

    // Update iNode
    fdt_e->writePointer += data_size;
    saveFDTNode(fdt_index);
    return data_size;
}


// UNUSED: appends data without overwriting existing
// If not enough space in FILE, will limit amount written
// If not enough space in FILE SYSTEM, data will be deleted from the end to fit appended
long appendData(int fdt_index, const void* data_buffer, long data_size) {
    if(data_size <= 0) return 0;
    int new_blocks_alloc = (int) ((fdt.inodes[fdt_index].size + data_size) / super_block.block_size);
    if(new_blocks_alloc >= MAX_FILE_BLOCKS) {
        data_size =  ((long)MAX_FILE_BLOCKS*super_block.block_size) - fdt.inodes[fdt_index].size;
    }

    // If we're writing to the end, act the same as overwrite
    if(fdt.table[fdt_index].writePointer == fdt.inodes[fdt_index].size) {
        return overwriteData(fdt_index, data_buffer, data_size);
    }

    // Otherwise, we have data to save
    long save_size = fdt.inodes[fdt_index].size - fdt.table[fdt_index].writePointer;
    char* save_data = malloc(save_size);

    long old_read_pointer = fdt.table[fdt_index].readPointer;
    fdt.table[fdt_index].readPointer = fdt.table[fdt_index].writePointer;
    readData(fdt_index, save_data, save_size);
    fdt.table[fdt_index].readPointer = old_read_pointer;

    // If entire file system is full, this approach gives new data priority, deleting old data
    overwriteData(fdt_index, data_buffer, data_size);
    overwriteData(fdt_index, save_data, save_size);
    fdt.table[fdt_index].writePointer -= save_size;
    free(save_data);

    return data_size;
}

// Fill the data in inode_e into buffer according to the read pointer
long readData(int fdt_index,  void* data_buffer, long data_size) {
    if (data_size <= 0) return 0;
    char* data = (char*) data_buffer;
    FDTEntry fdt_e = fdt.table[fdt_index];
    iNode* node = fdt.inodes + fdt_index;
    if(data_size + fdt_e.readPointer > node->size) data_size = node->size - fdt_e.readPointer;
    long bytes_read = data_size;


    // Grab indices of data blocks to read
    int cur_block = (int) (fdt_e.readPointer / super_block.block_size);
    int last_block = (int) ((fdt_e.readPointer + data_size - 1) / super_block.block_size);
    int* disk_data_idxs = malloc(sizeof(int) * (last_block - cur_block + 1));
    getNodeDataBlockList(node, cur_block, last_block, disk_data_idxs);

    // init current location variables
    int cur_block_local = fdt_e.readPointer % super_block.block_size;
    int remaining_size = super_block.block_size - cur_block_local; // remaining data to be read in a block
    if( data_size < remaining_size) remaining_size = data_size;

    char* block_buffer = malloc(super_block.block_size); // Holds block read data from disk
    for(int i = 0; data_size > 0; i++) {
        read_blocks(disk_data_idxs[i], 1, block_buffer);
        memcpy(data, block_buffer + cur_block_local, remaining_size);
        data += remaining_size; data_size -= remaining_size;
        cur_block_local = 0; 
        remaining_size = (data_size < super_block.block_size) ? data_size : super_block.block_size;
    }
    free(block_buffer);
    free(disk_data_idxs);
    fdt.table[fdt_index].readPointer += bytes_read;
    // No modifications = no saves needed
    return bytes_read;
}

// Note: deletes data BEFORE write pointer (non-inclusive)
// data_size = amount of data to delete (in bytes)
long deleteData(int fdt_index, long data_size) {
    // Possible feature to add: a buffer parameter that this method will fill with the deleted data
    FDTEntry* fdt_e = fdt.table + fdt_index;
    iNode* node = fdt.inodes + fdt_index;

    // Clamp delete
    if(data_size > fdt_e->writePointer) data_size = fdt_e->writePointer;
    if(data_size <= 0) return 0;
    long save_size = node->size - fdt_e->writePointer;

    // Find indices of data blocks we need
    int last_block = node->blocks_allocated - 1;
    int start_read_block = (int) (fdt_e->writePointer / super_block.block_size);
    int start_write_block = (int) ((fdt_e->writePointer - data_size) / super_block.block_size);
    int blocks_affected = last_block - start_write_block + 1;
    int* disk_data_idxs = malloc(blocks_affected * sizeof(int));
    getNodeDataBlockList(node, start_write_block, last_block, disk_data_idxs);


    // First start by reading everything from write pointer (save_size) into int* saved_data
    int cur_block_local = fdt_e->writePointer % super_block.block_size;
    int remaining_size = super_block.block_size - cur_block_local; // remaining data to be read in a block
    if (save_size < remaining_size) remaining_size = save_size;
    char* block_buffer = malloc(super_block.block_size); 
    char* saved_data = malloc(save_size);

    // These variables are made for arithmetic, so we can save/free/restore the original
    int save_size_tmp = save_size;
    char* save_tmp = saved_data;
    
    int i = start_read_block - start_write_block;
    for(; save_size_tmp > 0; i++) {
        read_blocks(disk_data_idxs[i], 1, block_buffer);
        memcpy(save_tmp, block_buffer + cur_block_local, remaining_size);
        save_tmp += remaining_size; save_size_tmp -= remaining_size;
        cur_block_local = 0; 
        remaining_size = (save_size_tmp < super_block.block_size) ? save_size_tmp : super_block.block_size;
    }

    // Now we start writing save data from decremented write pointer
    fdt_e->writePointer -= data_size; 
    save_size_tmp = save_size;
    save_tmp = saved_data;
    cur_block_local = fdt_e->writePointer % super_block.block_size; // offset in block
    remaining_size = super_block.block_size - cur_block_local; // space left in block to write
    if (save_size < remaining_size) remaining_size = save_size;
    i = 0;
    for(; save_size_tmp > 0; i++) {
        // Overwriting whole file end, don't care about saving data at end of block
        if(cur_block_local != 0) {
            read_blocks(disk_data_idxs[i], 1, block_buffer);
        }
        memcpy(block_buffer + cur_block_local, save_tmp, remaining_size);
        write_blocks(disk_data_idxs[i], 1, block_buffer);

        // Update internal vals
        save_tmp += remaining_size; save_size_tmp -= remaining_size;
        cur_block_local = 0;
        remaining_size = (save_size_tmp < super_block.block_size) ? save_size_tmp : super_block.block_size;
    }

    // Deleting data may affect where read pointer should look, update it correctly (write pointer already updated)
    if (fdt_e->readPointer >= fdt_e->writePointer + data_size) {
        fdt_e->readPointer -= data_size; // read pointer shifted back by deleting chunk
    } else if (fdt_e->readPointer > fdt_e->writePointer) {
        fdt_e->readPointer = fdt_e->writePointer; // read pointer in deleted chunk, shift to end
    } // else read pointer unaffected by deleted chunk

    // We now free unwritten blocks
    node->size -= data_size;
    int old_blocks_alloc = node->blocks_allocated;
    node->blocks_allocated -= (blocks_affected - i); // i is 1 after the last disk_data_idxs index written to
    for(; i < blocks_affected; i++) {
        free_data_bit(disk_data_idxs[i]);
    }
    if (old_blocks_alloc > 12 && node->blocks_allocated <= 12) free_data_bit(node->indirect_pointer);
    if (old_blocks_alloc > 12 + POINTERS_PER_BLOCK && node->blocks_allocated < old_blocks_alloc) {

        // 0-indexed in double indirect
        int last_indirect = old_blocks_alloc - 12 - POINTERS_PER_BLOCK;
        last_indirect = (last_indirect / POINTERS_PER_BLOCK) + 
                        (last_indirect % POINTERS_PER_BLOCK != 0) - 1; // Ceiling division
        // If cur_indirect <= 12 + POINTERS_PER_BLOCK will be negative
        int cur_indirect = node->blocks_allocated - 12 - POINTERS_PER_BLOCK;
        cur_indirect = (cur_indirect / POINTERS_PER_BLOCK) + 
                       (cur_indirect % POINTERS_PER_BLOCK != 0) - 1; // Ceiling division
        // If we're in a new indirect index, free double buffer blocks pointed
        if(last_indirect > cur_indirect) {
            int* double_buffer = (int*) block_buffer;
            read_blocks(node->double_indirect_pointer, 1, double_buffer);
            for(int i = last_indirect; i > cur_indirect; i--) {
                free_data_bit(double_buffer[i]); 
            }
            if(node->blocks_allocated <= 12 + POINTERS_PER_BLOCK) free_data_bit(node->double_indirect_pointer);
        }
    }

    // Clean up our mallocs
    free(block_buffer);
    free(saved_data);
    free(disk_data_idxs);

    // Return amount deleted
    return data_size;
}
    

