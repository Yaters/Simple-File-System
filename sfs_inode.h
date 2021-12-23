#ifndef SFS_INODE_H
#define SFS_INODE_H

#include "sfs_free_bit_map.h"
#include "sfs_super_block.h"
#include <stdbool.h>
#include <stdlib.h> 

// NOTE: uid, gid, file_id, link_count currently aren't used (though file_id is set).
// They are there in case I want to expand on this system at some point.
typedef struct iNode {
  bool is_directory;                // Directory (1) or file (0)
  int file_id;                      //Hold an internal id of the file itself

  int link_count;                   // Hard links to this iNode (number of directory entries)
  int uid;                          // User ID of file owner
  int gid;                          // Group ID of file owner
  long size;                        // Size of file/directory (in exact bytes)
  int blocks_allocated;             // Size of file/directory (in blocks)

  int direct_pointer[12];           // Direct block pointers
  int indirect_pointer;             // Pointer to block holding direct block pointers
  int double_indirect_pointer;      // Pointer to block holding indirect block pointers
} iNode;


typedef struct FDTEntry {
    int inode_idx;
    long readPointer;
    long writePointer;
} FDTEntry;


struct FileDescriptorTable_s {
    FDTEntry* table; // Holds indices & read/write pointers
    iNode* inodes; // This is the cached iNode table

    // For cache only
    int size; // Number of FDT entries (open iNodes)
    int allocated;  // size allocated to map (in entries)
};
typedef struct FileDescriptorTable_s FileDescriptorTable;

extern SuperBlock super_block;
extern FileDescriptorTable fdt;
extern int INODES_PER_BLOCK, MAX_FILE_ID, MAX_FILE_BLOCKS, POINTERS_PER_BLOCK;

// Load an inode from disk into the file descriptor table - returns index
int openFDTNode(int inode_index);

// DOES NOT SAVE - saving should be done continuously over program
void closeFDTNode(int fdt_index);

// Create empty iNode and return the fdt index
int createINode(bool is_directory);

// Returns deleted node - clears disk data - removes from fdt
FDTEntry deleteINode(int fdt_index);

// Write data into inode_e's iNode, overwriting based on write pointer (returns bytes written)
long overwriteData(int fdt_index, const void* data_buffer, long data_size);

// UNUSED: insert data at writePointer location (not overwriting existing data)
long appendData(int fdt_index, const void* data_buffer, long data_size);

// Fill the data in inode_e into buffer according to the read pointer (returns bytes read)
long readData(int fdt_index,  void* data_buffer, long data_size);

// Deletes data_size (in bytes) before write pointer pointer (non-inclusive) (returns # bytes deleted)
long deleteData(int fdt_index, long data_size);


#endif