#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sfs_api.h"
#include "disk_emu.h"

#include "sfs_directory.h"
#include "sfs_inode.h"
#include "sfs_free_bit_map.h"
#include "sfs_super_block.h"


#define DISK_NAME "fs.sfs"
#define ROOT_DIR_NAME "root"
#define INODE_TABLE_LENGTH 48 // blocks allocated to iNodes
#define BLOCK_SIZE 1024       // bytes in a block
#define FILE_SYSTEM_SIZE 1024 // blocks in the file system (on disk)

// Cached values
FileDescriptorTable fdt;
Directory cur_directory;
SuperBlock super_block;


// Used in directory.c and inode.c
int POINTERS_PER_BLOCK;
int INODES_PER_BLOCK;
int MAX_FILE_BLOCKS;
int MAX_FILE_ID = 0;
static int directory_iterator_index = 0;



// Helper method to clear values. Will do nothing if uninitialized
static void closeSFS() {
    close_disk();
    free(fdt.table);
    free(fdt.inodes);
    free(cur_directory.file_inode_map);
}

// Initialize file system (cache and constants), either loading in or creating with defaults
void mksfs(int fresh) {
    closeSFS();

    // Just in case, createFreeBitMap needs super_block to have defaults at least
    super_block = (SuperBlock) {.magic_number=SUPPORTED_SYSTEM, 
                                .block_size=BLOCK_SIZE, 
                                .file_system_size=FILE_SYSTEM_SIZE, 
                                .inode_table_length=INODE_TABLE_LENGTH, 
                                .root_directory=-1};
    fdt = (FileDescriptorTable) {.table=NULL, .inodes=NULL, .size=0, .allocated=0};
    cur_directory =  (Directory) {.parent_inode_index=-1, 
                                .file_inode_map=NULL, 
                                .file_number=0, 
                                .table_size=0, 
                                .fdt_index=-1};

    if(fresh) {
        // Use defaults
        POINTERS_PER_BLOCK = super_block.block_size / sizeof(int);
        INODES_PER_BLOCK = super_block.block_size / sizeof(iNode);
        MAX_FILE_BLOCKS = 12 + POINTERS_PER_BLOCK + POINTERS_PER_BLOCK * POINTERS_PER_BLOCK;
        
        init_fresh_disk(DISK_NAME, super_block.block_size, super_block.file_system_size);
        createFreeBitMap();

        // Create root directory
        int root_fdt_ind = createDirectoryFile(ROOT_DIR_NAME, true);
        super_block.root_directory = fdt.table[root_fdt_ind].inode_idx;
        saveSuperBlock();
    } else {
        // Read super block
        init_disk(DISK_NAME, super_block.block_size, 1); 
        readSuperBlock();
        close_disk();

        if(super_block.magic_number != SUPPORTED_SYSTEM) {
            fprintf(stderr, "Existing file system not supported by sfs. Exiting...\n");
            return; // If I had full design perms I'd make this return false or -1
        }

        // Use super block to init constants
        POINTERS_PER_BLOCK = super_block.block_size / sizeof(int);
        INODES_PER_BLOCK = super_block.block_size / sizeof(iNode);
        MAX_FILE_BLOCKS = 12 + POINTERS_PER_BLOCK + POINTERS_PER_BLOCK * POINTERS_PER_BLOCK;
        

        init_disk(DISK_NAME, super_block.block_size, super_block.file_system_size);

        // Needed super_block loaded
        loadFreeBitMap();
        MAX_FILE_ID = find_number_files();
    }    
    loadDirectory(super_block.root_directory, true);
}


// Used by FUSE for iterator through directory - read into fname, return 1 on success, 0 on end of list
int sfs_getnextfilename(char* fname) {
    if(directory_iterator_index >= cur_directory.file_number) {
        directory_iterator_index = 0;
        return 0;
    }
    strcpy(fname, cur_directory.file_inode_map[directory_iterator_index].name);
    directory_iterator_index++;
    return 1;
}


// Return size of file in bytes - assumes the given path starts from root
// ex. if "a3" is the currently loaded directory, we need "a3\sfs_superblock", not "sfs_superblock"
int sfs_getfilesize(const char* path) {
    int old_fdt_size = fdt.size; // Need to see if we should close when we're done
    int fdt_idx = fdtOpenFullPathFile(path); // Will restore directory
    if(fdt_idx < 0) {
        return fdt_idx;
    }
    int size = fdt.inodes[fdt_idx].size;
    if(fdt.size != old_fdt_size) closeFDTNode(fdt_idx);
    return size;
}


// Create a subdirectory with the given name. Return 0 on success, negative on failure
int sfs_mkdir(char* name) {
    if(strlen(name) > MAXFILENAME) {
        return -1;
    }
    int old_size = fdt.size;
    int idx = openDirectoryFile(name);
    // If found, error. Otherwise create new
    if(idx >= 0) {
        if(fdt.size > old_size) closeFDTNode(idx);
        return -1;
    } else {
        idx = createDirectoryFile(name, true);
    }
    if(idx < 0) return -1;
    closeFDTNode(idx); // Cleanup, don't keep created directory in FDT
    return 0;
}


// Changes current directory to subdirectory with the given name. Return 0 on success, negative on failure
int sfs_loaddir(char* name) {
    if(strlen(name) > MAXFILENAME) {
        return 0;
    }

    int inode_idx = cur_directory.parent_inode_index; // Default parent

    // If not loading parent directory - load new into fdt and extract iNode index
    if(strcmp(name, "..") != 0) {
        int fdt_idx = openDirectoryFile(name);
        if(fdt_idx < 0) return fdt_idx;
        inode_idx = fdt.table[fdt_idx].inode_idx;
    }

    if(inode_idx < 0 || !loadDirectory(inode_idx, true)) return -1;
    directory_iterator_index = 0; // Restart any iterator
    return 0;
}

// TODO: Load absolute path method (loaddir is relative, and only takes one at a time)
// TODO: Make loaddir take "...\...", and make sure file names don't have "\"
// TODO: Organize file names to have 3 letter extension max


// Open a file with name (create if doesn't exist). Return index in file descriptor table, or negative on error
int sfs_fopen(char* name) {
    if(strlen(name) > MAXFILENAME) {
        return -1;
    }
    int idx = openDirectoryFile(name);

    // If doesn't already exist
    if (idx < 0) {
        idx = createDirectoryFile(name, false);
    
    // Use specific directory functions (mkdir, loaddir, etc.) for directories.
    } else if (fdt.inodes[idx].is_directory){ 
        return -1;
    }
    return idx;
}


// Remove file from file descriptor table. Return 0 on success, negative on error
int sfs_fclose(int fileID) {
    if(fileID < 0 || fileID >= fdt.allocated || fdt.table[fileID].inode_idx < 0  || fdt.inodes[fileID].is_directory) {
        return -1;
    }
    closeFDTNode(fileID);
    return 0;
}


// Use write pointer to write to file. Return bytes written
int sfs_fwrite(int fileID, const char* buf, int length) {
    if(fileID < 0 || fileID >= fdt.allocated || fdt.table[fileID].inode_idx < 0 || fdt.inodes[fileID].is_directory) {
        return -1;
    }
    if (length < 1) return 0;
    int bytes_written = overwriteData(fileID, buf, length);
    fdt.table[fileID].readPointer = fdt.table[fileID].writePointer; // Assignment required 1 read/write pointer
    return bytes_written;
}

// Use write pointer to delete from a file. Returns bytes deleted
int sfs_fdelete(int fileID, int length) {
    if(fileID < 0 || fileID >= fdt.allocated || fdt.table[fileID].inode_idx < 0 || fdt.inodes[fileID].is_directory) {
        return -1;
    }
    if (length < 1) return 0;
    int bytes_deleted = deleteData(fileID, length);
    fdt.table[fileID].readPointer = fdt.table[fileID].writePointer; // Assignment required 1 read/write pointer
    return bytes_deleted;
}

// Use read pointer to read from file. Return bytes read
int sfs_fread(int fileID, char* buf, int length) {
    if(fileID < 0 || fileID >= fdt.allocated || fdt.table[fileID].inode_idx < 0 || fdt.inodes[fileID].is_directory) {
        return -1;
    }
    if (length < 1) return 0;
    int bytes_read = readData(fileID, buf, length);
    fdt.table[fileID].writePointer = fdt.table[fileID].readPointer; // Assignment required 1 read/write pointer
    return bytes_read;
}


// Move read/write pointer to given loc. Return 0 on success, negative on error
int sfs_fseek(int fileID, int loc) {
    if(fileID < 0 || fileID >= fdt.allocated || fdt.table[fileID].inode_idx < 0 || fdt.inodes[fileID].is_directory) {
        return -1;
    }
    if (loc < 0 || loc >= fdt.inodes[fileID].size) {
        return -1;
    }
    fdt.table[fileID].readPointer = loc;
    fdt.table[fileID].writePointer = loc;
    return 0;
}

// Delete a file or directory. Return 0 on success, negative on error
int sfs_remove(char* file) {
    DirectoryTableEntry old_file = removeDirectoryFile(file);
    if(old_file.inode_index < 0) return -1;
    return 0;
}

