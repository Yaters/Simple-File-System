#ifndef SFS_FREE_BIT_MAP_H
#define SFS_FREE_BIT_MAP_H
#include "sfs_super_block.h"

extern SuperBlock super_block;
extern int INODES_PER_BLOCK;

void createFreeBitMap();
void loadFreeBitMap();

// Data grab returns open data block index in global disk position
int grab_data_bit();

// iNode grab returns open node index in iNode table position (first iNode = 0)
int grab_inode_bit();

// Free data block at block_index (in global disk position)
void free_data_bit(int block_index);

// Free iNode index (in iNode position | first iNode = 0)
void free_inode_bit(int inode_index);

// Method to find the number of files being used in the system
int find_number_files();


void saveFreeBitMapToDisk();
//void readFreeBitMapFromDisk(); // Won't make that one public

#endif