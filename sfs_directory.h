#ifndef SFS_DIRECTORY_H
#define SFS_DIRECTORY_H

#include "sfs_inode.h"
#include "sfs_super_block.h"
#include "sfs_api.h" // Need for MAXFILENAME


typedef struct DirectoryTableEntry {
    char name[MAXFILENAME + 1]; // Include null terminate
    int inode_index;
} DirectoryTableEntry;


struct Directory_s {
    int parent_inode_index; // Need on disk so a child can load its parent 
    DirectoryTableEntry* file_inode_map;

    // For cache only
    int file_number; // Number of directory entries
    int table_size;  // size allocated to map (in TableEntries)
    int fdt_index;   // file descriptor index of current directory (dir needs read/write pointer)
};
typedef struct Directory_s Directory;

extern Directory cur_directory;
extern FileDescriptorTable fdt;

// Loads the given path name into fdt table
// pathName = path to open (starting from file in root dir). Returns fdt index
int fdtOpenFullPathFile(const char* pathName);

// Opens the given file in the FDT and returns the FDT index
int openDirectoryFile(const char* fileName);

// UNUSED: Move directory file from current directory to given path (from a root file). Returns true on success
// If copy==true, a copy will be made in the new directory. Else the file will just be moved
bool moveDirectoryFile(const char* fileName, const char* moveToPath, bool copy);

// Adds a file with given name to the directory
int createDirectoryFile(const char* name, bool is_directory);

// NOTE: Avoid referencing 1 file in multiple directories for now, I have not worked out the remove logic

// Remove the file in directory and disk
DirectoryTableEntry removeDirectoryFile(const char* fileName);

// Replace the current directory table with that given by index
bool loadDirectory(int inode_index);

#endif