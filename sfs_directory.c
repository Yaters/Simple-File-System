#include "sfs_directory.h"

#include <string.h>
#include <stdio.h>

// NOTE: The root directory is called "root"
#define DIRECTORY_SEPARATOR '\\'

// NOTE: loadDirectory could fail on realloc. Properly catching everywhere this could happen
// is not simple, and was not done here.

// TODO: Stop cycles in move/copy, and check recursive works well


// Helper for debugging, lists the files in current directory
// static void printDirectoryTable() {
//     for(int i = 0; i < cur_directory.file_number; i++) {
//         fprintf(stderr, "NAME: %s \t\t\t\t INODE INDEX: %d \n", cur_directory.file_inode_map[i].name, cur_directory.file_inode_map[i].inode_index);
//     }
// }

// Private method to get index within directory table of given file name
static int getDirectoryIndex(const char* fileName) {
    DirectoryTableEntry cur_file;
    for (int i = 0; i < cur_directory.file_number; i++) {
        cur_file = cur_directory.file_inode_map[i];
        if (strcmp(fileName, cur_file.name) == 0) return i;
    }
    return -1;
}

// TODO: How to communicate to moveDirectoryFile if it's a subdirectory?

// Method to convert a pathname into an fdt index that can be manipulated. Pathname should be from root.
// ex. "root//two//hello//abc.txt" should be passed "two//hello//abc.txt" 
// Note: it is up to the programmer to close this fdt entry after use
int fdtOpenFullPathFile(const char* pathName) {
    int cur_dir_inode = fdt.table[cur_directory.fdt_index].inode_idx;
    //bool is_cur_subdir = false;
    loadDirectory(super_block.root_directory, true);
    char* hold_buffer = malloc(MAXFILENAME + 1); // allow null character end
    int i, j;
    for(i=0, j=0; i <= strlen(pathName); i++, j++) {
        // Should be impossible
        if (j > MAXFILENAME) {
            free(hold_buffer);
            loadDirectory(cur_dir_inode, true);
            return -1;

        // load in the new subdirectory we were reading in hold_buffer
        } else if(pathName[i] == DIRECTORY_SEPARATOR) {
            hold_buffer[j] = '\0';
            int dir_index = getDirectoryIndex(hold_buffer);
            if (dir_index < 0) {
                free(hold_buffer);
                loadDirectory(cur_dir_inode, true);
                return -1;
            }
            int inode_index = cur_directory.file_inode_map[dir_index].inode_index;
            //if(inode_index == cur_dir_inode) is_cur_subdir = true;
            if(!loadDirectory(inode_index, true)) {
                free(hold_buffer);
                loadDirectory(cur_dir_inode, true);
                return -1;
            }
            j = -1; // for loop will increment

        // Update with newest character
        } else {
            hold_buffer[j] = pathName[i];
        }
    }
    // We now have the correct directory loaded and can retrieve the name in hold_buffer
    int fdt_idx = openDirectoryFile(hold_buffer);
    free(hold_buffer);

    // Restore condition. In future could make this go on a boolean flag
    loadDirectory(cur_dir_inode, true);

    return fdt_idx;
}

// Opens the file with the given name (in the current directory) in our FDT and returns the fdt index
int openDirectoryFile(const char* fileName) {
    // Find inode # of file with fileName using cur_directory
    //int dir_idx = getDirectoryIndex
    int inode_idx = -1;
    DirectoryTableEntry cur_file;
    for (int i = 0; i < cur_directory.file_number; i++) {
        cur_file = cur_directory.file_inode_map[i];
        if (strcmp(fileName, cur_file.name) == 0) {
            inode_idx = cur_file.inode_index;
            break;
        }
    }
    if(inode_idx < 0) return -1;
    return openFDTNode(inode_idx);
}


// Adds a file with given name to the directory - returns file descriptor table index
int createDirectoryFile(const char* name, bool is_directory) {
    // Create new iNode on disk and cache
    int fdt_index = createINode(is_directory);
    if (fdt_index < 0) return fdt_index;
    fdt.inodes[fdt_index].link_count = 1;
    int curDirIdx = cur_directory.fdt_index;

    // Space check
    if(fdt_index < 0) {
        fprintf(stderr, "Directory file addition failed: too many files on system\n");
        return fdt_index;
    } 

    // Pre-fill parent-index if creating directory iNode (may be useful if I extend directories) 
    // curDirIdx<0 checks if a parent exists (we may be adding a root)
    if (is_directory) { 
        int* par_idx = malloc(sizeof(cur_directory.parent_inode_index));
        if(curDirIdx < 0) *par_idx = -1;
        else *par_idx = fdt.table[curDirIdx].inode_idx;
        overwriteData(fdt_index, par_idx, sizeof(cur_directory.parent_inode_index));
        free(par_idx);
    }
    if(curDirIdx < 0) return fdt_index;

    // Adding the new entry to current directory data block
    
    // Allocate more cache memory as needed
    if(cur_directory.file_number == cur_directory.table_size) {
        cur_directory.table_size += 5;
        DirectoryTableEntry* new_dir_mem = realloc(cur_directory.file_inode_map, cur_directory.table_size * sizeof(DirectoryTableEntry));
        if(new_dir_mem == NULL) {
            fprintf(stderr, "ERROR  (create file): Unable to allocate directory cache memory!\n");
            return -1;
        }
        cur_directory.file_inode_map = new_dir_mem;
    }

    // Fill cache directory table with data
    cur_directory.file_inode_map[cur_directory.file_number].inode_index = fdt.table[fdt_index].inode_idx;
    strcpy(cur_directory.file_inode_map[cur_directory.file_number].name, name);

    // Write cache directory to disk (new entry)
    overwriteData(curDirIdx, cur_directory.file_inode_map + cur_directory.file_number, sizeof(DirectoryTableEntry));
    cur_directory.file_number++;
    return fdt_index;
}


// Does the actual work of deleting data from directory. Hidden to hide details like stopping iNode deletion
static DirectoryTableEntry _removeDirectoryFile(int directory_index, bool delete_data) {

    // Get info about node to remove
    DirectoryTableEntry remove = cur_directory.file_inode_map[directory_index];
    int fdt_index = openFDTNode(remove.inode_index);
    iNode remove_inode = fdt.inodes[fdt_index];

    // Delete the iNodes data if needed, recursive delete for subdirectories
    if(delete_data) {
        if(remove_inode.is_directory) {
            loadDirectory(remove.inode_index, false); // Don't close going down
            while(cur_directory.file_number > 0) {
                // Removing last saves time
                _removeDirectoryFile(cur_directory.file_number - 1, delete_data);
            }
            loadDirectory(cur_directory.parent_inode_index, false); // Leave open for deleteINode
        }

        // Delete the file's iNode - closes the FDT entry too
        deleteINode(fdt_index);
    }

    // Update current directory (data)

    cur_directory.file_number--;
    // Update the directory cache
    // swap last directory entry into removed & update disk
    long old_write_pointer = fdt.table[cur_directory.fdt_index].writePointer;
    if(directory_index != cur_directory.file_number) {
        cur_directory.file_inode_map[directory_index] = cur_directory.file_inode_map[cur_directory.file_number];
        // directory is [parent index, DirectoryTableEntry1, DirectoryTableEntry2, ...]
        fdt.table[cur_directory.fdt_index].writePointer =  sizeof(cur_directory.parent_inode_index)
                                                           + directory_index * sizeof(DirectoryTableEntry);
        overwriteData(cur_directory.fdt_index, cur_directory.file_inode_map + directory_index, sizeof(DirectoryTableEntry));
    }
    fdt.table[cur_directory.fdt_index].writePointer = fdt.inodes[cur_directory.fdt_index].size;
    deleteData(cur_directory.fdt_index, sizeof(DirectoryTableEntry));
    // Reset write pointer if it was less than current (which is at updated size)
    if(old_write_pointer < fdt.table[cur_directory.fdt_index].writePointer) {
        fdt.table[cur_directory.fdt_index].writePointer = old_write_pointer;
    }
    

    // Realloc cache size if needed (so we aren't endlessly filling)
    if(cur_directory.file_number <= cur_directory.table_size - 5 && cur_directory.table_size > 5) {
        cur_directory.table_size -= 5;
        DirectoryTableEntry* new_dir_mem = realloc(cur_directory.file_inode_map, cur_directory.table_size * sizeof(DirectoryTableEntry));
        if(new_dir_mem == NULL) {
            fprintf(stderr, "ERROR: Unable to deallocate directory cache memory!\n");
        } else {
            cur_directory.file_inode_map = new_dir_mem;
        }
    }
    return remove;
}


// Remove the file from current directory and disk (public method)
DirectoryTableEntry removeDirectoryFile(const char* fileName) {
    // Find file
    int directory_index = getDirectoryIndex(fileName);
    if(directory_index < 0 ||  directory_index >= cur_directory.file_number) {
        return (DirectoryTableEntry) {.inode_index = -1, .name = ""};
    }

    // If no more references in file system, remove permanently
    int old_fdt_size = fdt.size;
    int fdt_id = openFDTNode(cur_directory.file_inode_map[directory_index].inode_index);
    fdt.inodes[fdt_id].link_count--;
    DirectoryTableEntry removed;
    if(fdt.inodes[fdt_id].link_count <= 0) {
        removed = _removeDirectoryFile(directory_index, true);  // Delete iNode, closes fdt too
    } else {
        removed = _removeDirectoryFile(directory_index, false); // Keep iNode, remove directory entry
        if(fdt.size > old_fdt_size) closeFDTNode(fdt_id);
    }
    return removed;
}


// Replace the current directory table with that given by index - returns success status
bool loadDirectory(int inode_index, bool close_current_directory) {
    if(cur_directory.fdt_index > -1 && inode_index == fdt.table[cur_directory.fdt_index].inode_idx) return true;
    // add new directory to FDT
    int old_fdt_size = fdt.size;
    int fdt_index = openFDTNode(inode_index);
    iNode new_dir = fdt.inodes[fdt_index];
    if(!new_dir.is_directory) {
        fprintf(stderr, "Attempting to load data file as a directory, load cancelled\n");
        if(fdt.size > old_fdt_size) closeFDTNode(fdt_index); // If the new wasn't already opened, close
        return false;
    }

    // Close the current directory in FDT
    if(cur_directory.fdt_index >= 0 && close_current_directory) closeFDTNode(cur_directory.fdt_index);

    int new_table_size = new_dir.size - sizeof(cur_directory.parent_inode_index);
    int new_num_files = new_table_size / sizeof(DirectoryTableEntry);
    
    // Change cur_directory cache values
    if(new_table_size > 0) {
        DirectoryTableEntry* new_dir_mem = realloc(cur_directory.file_inode_map, new_table_size);
        if(new_dir_mem == NULL) {
            fprintf(stderr, "ERROR (Load directory): Unable to allocate directory cache memory (size %d)!\n", new_table_size);
            return false;
        }
        cur_directory.file_inode_map = new_dir_mem;
    }

    cur_directory.file_number = new_num_files;
    cur_directory.table_size = new_num_files;
    cur_directory.fdt_index = fdt_index;

    // Update cur_directory cache values with iNode data block (parent index and name/index map)
    char* info_buffer = malloc(new_dir.size);
    readData(fdt_index, info_buffer, new_dir.size);
    memcpy(&(cur_directory.parent_inode_index), info_buffer, sizeof(cur_directory.parent_inode_index));
    memcpy(cur_directory.file_inode_map, info_buffer + sizeof(cur_directory.parent_inode_index), new_table_size);
    fdt.table[fdt_index].readPointer = 0;
    fdt.table[fdt_index].writePointer = new_dir.size;
    free(info_buffer);

    return true;
}


// UNUSED: We didn't need subdirectories, and so it wasn't tested (likely has bugs)
// Copies a file from one directory to another. 'moveToPath' must be full path, see fdtOpenFullPathFile
bool copyDirectoryFile(const char* fileName, const char* moveToPath) {
    // Current directory info
    int old_dir_idx = getDirectoryIndex(fileName);
    int old_dir_inode = fdt.table[cur_directory.fdt_index].inode_idx; // To load back later
    if (old_dir_idx < 0) return false;
    // Curent file info
    int old_fdt_size = fdt.size; // Keep track of whether open imported new files (do we need to close)
    int old_file_inode = cur_directory.file_inode_map[old_dir_idx].inode_index;
    int old_file = openFDTNode(old_file_inode);
    bool close_old = fdt.size > old_fdt_size;

    // Not moving to directory check
    int new_file_dir = fdtOpenFullPathFile(moveToPath);
    if(!fdt.inodes[new_file_dir].is_directory) {
        closeFDTNode(new_file_dir);
        if(close_old) closeFDTNode(old_file);
        return false;
    }

    // First read data from current directory
    long save_data_size = fdt.inodes[old_file].size;
    char* copy_from_buf = malloc(fdt.inodes[old_file].size);
    long past_read_pointer = fdt.table[old_file].readPointer;
    fdt.table[old_file].readPointer = 0;
    readData(old_file, copy_from_buf, fdt.inodes[old_file].size);
    fdt.table[old_file].readPointer = past_read_pointer;
    if(close_old) closeFDTNode(old_file);

    // Loading new directory & add entry
    loadDirectory(fdt.table[new_file_dir].inode_idx, true);

    char new_name[MAXFILENAME + 1];
    strcpy(new_name, fileName);
    while(getDirectoryIndex(new_name) > 0) {
        if(strlen(new_name) + 2 > MAXFILENAME) {
            if (close_old) closeFDTNode(old_file);
            free(copy_from_buf);
            loadDirectory(old_dir_inode, true);
            return false;
        }
        strcat(new_name, "_c");
    }
    int new_file = createDirectoryFile(new_name, fdt.inodes[old_file].is_directory);
    
    // Write data to new directory file
    old_fdt_size = fdt.size;
    overwriteData(new_file, copy_from_buf, save_data_size);
    free(copy_from_buf);
    if(fdt.size > old_fdt_size) closeFDTNode(new_file);
    if (close_old) closeFDTNode(old_file);
    return true;
}

// TODO: Recursive move/copy

// UNUSED: We didn't need subdirectories, and so it wasn't tested (likely has bugs)
// Moves a file from one directory to another. 'moveToPath' must be full path, see fdtOpenFullPathFile
bool moveDirectoryFile(const char* fileName, const char* moveToPath, bool copy) {
    // Current directory info
    int old_dir_idx = getDirectoryIndex(fileName);
    int old_dir_inode = fdt.table[cur_directory.fdt_index].inode_idx; // To load back later
    if (old_dir_idx < 0) return false; 
    // Curent file info
    int old_fdt_size = fdt.size; // Keep track of whether open imported new files (do we need to close)
    int old_file_inode = cur_directory.file_inode_map[old_dir_idx].inode_index;
    int old_file = openFDTNode(old_file_inode);
    bool close_old = fdt.size > old_fdt_size;

    // Not moving to directory check
    // TODO: Must be sure new file dir != subdirectory of current
    int new_file_dir = fdtOpenFullPathFile(moveToPath);
    if(!fdt.inodes[new_file_dir].is_directory) {
        closeFDTNode(new_file_dir);
        if(close_old) closeFDTNode(old_file);
        return false;
    }

    // Load new directory & create entry
    DirectoryTableEntry new_entry = _removeDirectoryFile(old_dir_idx, false);
    loadDirectory(fdt.table[new_file_dir].inode_idx, true);
    strcpy(new_entry.name, fileName);
    while(getDirectoryIndex(new_entry.name) > 0) {
        if(strlen(new_entry.name) + 2 > MAXFILENAME) {
            if (close_old) closeFDTNode(old_file);
            loadDirectory(old_dir_inode, true);
            return false;
        }
        strcat(new_entry.name, "_c");
    }

    // Add a new directory entry pointing to old iNode index
    if(cur_directory.file_number == cur_directory.table_size) {
        cur_directory.table_size += 5;
        DirectoryTableEntry* new_dir_mem = realloc(cur_directory.file_inode_map, cur_directory.table_size * sizeof(DirectoryTableEntry));
        if(new_dir_mem == NULL) {
            fprintf(stderr, "ERROR  (move file): Unable to allocate directory cache memory!\n");
            if (close_old) closeFDTNode(old_file);
            loadDirectory(old_dir_inode, true);
            return false;
        }
        cur_directory.file_inode_map = new_dir_mem;
    }
    cur_directory.file_inode_map[cur_directory.file_number] = new_entry;
    overwriteData(cur_directory.fdt_index, cur_directory.file_inode_map + cur_directory.file_number, sizeof(DirectoryTableEntry));
    cur_directory.file_number++;

    // Cleanup
    loadDirectory(old_dir_inode, true);
    if(fdt.size > old_fdt_size) closeFDTNode(old_file);
    return true;
}