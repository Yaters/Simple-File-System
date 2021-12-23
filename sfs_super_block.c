#include "sfs_super_block.h"
#include "disk_emu.h"
#include <stdlib.h>
#include <string.h>

// Read super block from memory - expects super block to be default
void readSuperBlock() {
    char* read_buff = malloc(super_block.block_size);
    read_blocks(0, 1, read_buff);
    memcpy(&super_block, read_buff, sizeof(SuperBlock));
    free(read_buff);
}

// Save super block after changes
void saveSuperBlock() {
    char* write_buff = malloc(super_block.block_size);
    memcpy(write_buff, &super_block, sizeof(SuperBlock));
    write_blocks(0, 1, write_buff);
    free(write_buff);
}