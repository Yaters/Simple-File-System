#include "sfs_free_bit_map.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "disk_emu.h"

// 'private' - limit scope to current file
static unsigned char* free_bit_map = NULL; // One free bit map for [inode_bits, data_bits]
static int data_num_bytes = 0;
static int inode_num_bytes = 0;
static int total_num_bytes = 0;

void saveFreeBitMapToDisk() {
    char* buff = malloc(super_block.block_size);
    memcpy(buff, free_bit_map, total_num_bytes);
    write_blocks(super_block.file_system_size - 1, 1, buff);
    free(buff);
}

// expects free bit to be alloced (and config vars = num_bytes per section)
static void readFreeBitMapFromDisk() {
    char* buff = malloc(super_block.block_size);
    read_blocks(super_block.file_system_size - 1, 1, buff);
    memcpy(free_bit_map, buff, total_num_bytes);
    free(buff);
}

void createFreeBitMap() {
    // Discount inodes, super block, and free bit block
    int data_blocks = super_block.file_system_size - super_block.inode_table_length - 2; 
    int inodes = super_block.inode_table_length * INODES_PER_BLOCK;

    inode_num_bytes = inodes / 8;
    int extra_inode_bits = inodes % 8;
    inode_num_bytes += (extra_inode_bits != 0);

    data_num_bytes =  data_blocks / 8;
    int extra_data_bits = data_blocks % 8; 
    data_num_bytes += (extra_data_bits != 0);
 
    total_num_bytes = data_num_bytes + inode_num_bytes;

    free(free_bit_map);
    free_bit_map = malloc(total_num_bytes);
    memset(free_bit_map, 255UL, total_num_bytes); // Init all freed 

    // zero the extra bits in the map - the excess when allocating 8 more
    if(extra_inode_bits != 0) free_bit_map[inode_num_bytes - 1] &= (255UL << (8-extra_inode_bits));
    if(extra_data_bits != 0) free_bit_map[total_num_bytes - 1] &= (255UL <<  (8-extra_data_bits));

    saveFreeBitMapToDisk();
}

void loadFreeBitMap() {
    // Discount inodes, super block, and free bit block
    int data_blocks = super_block.file_system_size - super_block.inode_table_length - 2; 
    int inodes = super_block.inode_table_length * INODES_PER_BLOCK;
    
    int inode_num_bytes = inodes / 8 +  (inodes % 8 != 0);
    int data_num_bytes  = data_blocks / 8 +  (data_blocks % 8 != 0);

    total_num_bytes = data_num_bytes + inode_num_bytes;

    free(free_bit_map);
    free_bit_map = malloc(total_num_bytes);
    readFreeBitMapFromDisk();
}

// Lock bit & return index relative to its bit array start (inode start = 0, data start = inode_num_bytes)
static int grab_bit(bool is_inode) {
    int start_index = is_inode ? 0 : inode_num_bytes;
    int max = is_inode ? inode_num_bytes : total_num_bytes;
    
    for(int i = start_index; i < max; i++) {
        if (free_bit_map[i] == 0) continue;
        for (int j = 0; j < 8; j++) {
            // 128UL = 0b10000000, this reads bits left to right
            if(free_bit_map[i] & (128UL >> j)) {
                free_bit_map[i] &= ~(128UL >> j);
                return (i - start_index)*8 + j; // zero-indexed relative to start
            }
        }
    }

    // No open bit found
    return -1;
}


// Free bit at index relative to its bit array start (inode start = 0, data start = inode_num_bytes)
static void free_bit(int idx, bool is_inode) {
    int char_pos = idx / 8; int bit_pos = idx % 8;
    if(!is_inode) {
        char_pos += inode_num_bytes;
    }
    // Caller must ensure range is correct
    free_bit_map[char_pos] |= (128UL >> bit_pos);
}

// data grab returns open block index in global disk position
int grab_data_bit() {
    int idx = grab_bit(false);
    int block_idx = (idx < 0) ? idx : idx + 1 + super_block.inode_table_length;
    return block_idx; 
}

// iNode grab returns open node index in iNode table position (first iNode = 0)
int grab_inode_bit() {
    return grab_bit(true);
}

// free block index (in global disk position)
void free_data_bit(int block_index) {
    int rel_idx = block_index - 1 - super_block.inode_table_length;
    if (block_index >= (super_block.file_system_size-1) || rel_idx < 0) {
        fprintf(stderr, "Freeing block %d out of system range\n", block_index);
        return;
    }
    free_bit(rel_idx, false);
}

// free node index (in iNode table position | first iNode = 0)
void free_inode_bit(int inode_index) {
    if (inode_index >= super_block.inode_table_length*INODES_PER_BLOCK || inode_index < 0) {
        fprintf(stderr, "Freeing inode %d out of system range\n", inode_index);
        return;
    }
    free_bit(inode_index, true);
}

// Method to find the number of files being used in the system
int find_number_files() {
    // Loop over inodes list and count the number of 1s (free spaces)
    int sum = 0;
    for(int i = 0; i < inode_num_bytes; i++) {
            unsigned char byt = free_bit_map[i];
            while (byt != 0) {
                sum++;
                byt = byt & (byt - 1);
        }
    }

    // total files possible - spaces open
    return super_block.inode_table_length - sum;
}