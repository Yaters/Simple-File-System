#ifndef SFS_SUPER_BLOCK_H
#define SFS_SUPER_BLOCK_H

#define SUPPORTED_SYSTEM 0xACBD0005

struct _SuperBlock {
    unsigned int magic_number;    // Unique file_system ID #
    int block_size;               // In bytes
    int file_system_size;         // In blocks
    int inode_table_length;       // In blocks
    int root_directory;           // iNode # of root directory
};
typedef struct _SuperBlock SuperBlock;

// Global super_block, defined in sfs_api
extern SuperBlock super_block;

// Read super block from memory
void readSuperBlock();

// Save super block after changes
void saveSuperBlock();

#endif
