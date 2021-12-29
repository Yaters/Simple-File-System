The Simple File System (sfs) is a UNIX file system to oversee data accesses to "disk" memory, the
disk being emulated with a file called fs.sfs and accessed in blocks. It was developed as a programming 
assignment for an Operating Systems course, and as such follows the specifications detailed there.

To Run:
Download C files from github, use CMake to compile it (with gcc), and execute the file created (called sfs).
Currently, make is set to compile an executable that runs test case 3. To change this, within MakeFile (line 19), 
change sfs_test3.c to the C program you wish to execute (the one relient on functions in our file system).


git clone https://github.com/Yaters/Simple-File-System.git
make
./sfs

SFS API:

NOTE: Functions like read, write, seek, open/close, and delete can not be used on directories.
      Use specialized directory functions instead (mkdir, load, remove, etc.)

/* Formats the disk emulator virtual disk, and creates the simple file system 
*  instance on it.
*  Parameters:
*      fresh (bool): create file system from scratch (reset). 1 for new disk, 0 to load existing.
*/
void mksfs(int fresh)


/* Find next directory file. Used to loop through files in directory.
*  Return 1 if next file read, 0 on end of list. 
*  Parameters:
*      fname  (char*): buffer to save file name in
*  Return:
*      success (bool): Whether file name was saved (vs. reached end of directory)
*/
int sfs_getnextfilename(char* fname)


/* Return the size of a file.
*  Parameters:
*      path (char*): Path of file to find size of
*  Return:
*      size   (int): Size of the file, in bytes
*/
int sfs_getfilesize(const char* path)


/* Create a subdirectory in the currently loaded directory with the given name. Error if name is already taken. 
*  Parameters:
*      name (char*): Name of new subdirectory to create
*  Return:
*      success (int): 0 if succesful, negative if error
*/
int sfs_mkdir(char* name)


/* Load subdirectory of given name into cache, future file calls will affect this directory. 
*  Only takes one file at a time, and does not accept absolute paths, only relative.
*  Parameters:
*      name (char*): Name of new subdirectory to load, or ".." to load parent
*  Return:
*      success (int): 0 if succesful, negative if error
*/
int sfs_loaddir(char* name)


/* Open a file (load iNode to cache) and return index of file descriptor table entry.
*  If a file does not exist, it will be created with size 0.
*  File by default opens with the write pointer at the end of the file (writes will append).
*  Parameters:
*      name (char*): Name of file to open
*  Return:
*      fileID (int): File descriptor table index of the opened file (negative on error)
*/
int sfs_fopen(char* name)


/* Close a file, removing it from the file descriptor table.
*  Parameters:
*      fileID  (int): Index of file descriptor table entry to remove
*  Return:
*      success (int): 0 if succesful, negative if error
*/
int sfs_fclose(int fileID)


/* Write to an opened file (at the current location of the read/write pointer).
*  Parameters:
*      fileID   (int): File index in file descriptor table
*      buffer (char*): Buffer of data to be written
*      length   (int): Number of bytes to write (size of buffer)
*  Return:
*      length   (int): Number of bytes written
*/
int sfs_fwrite(int fileID, const char* buf, int length)

/* Delete data from an opened file (data just before the current location of the read/write pointer).
*  Parameters:
*      fileID   (int): File index in file descriptor table
*      length   (int): Number of bytes to delete
*  Return:
*      length   (int): Number of bytes deleted
*/
int sfs_fdelete(int fileID, int length)


/* Read an opened file (at the current location of the read/write pointer).
*  Parameters:
*      fileID   (int): File index in file descriptor table
*      buffer (char*): Buffer to save read data within
*      length   (int): Number of bytes to read
*  Return:
*      length   (int): Number of bytes read
*/
int sfs_fread(int fileID, char* buf, int length)


/* Move read/write pointer to the given location.
*  Parameters:
*      fileID  (int): File index in file descriptor table
*      loc     (int): Location to move read/write pointer to (byte location)
*  Return:
*      success (int): 0 if succesful, negative if error
*/
int sfs_fseek(int fileID, int loc)


/* Delete a file or directory from the current SFS directory.
*  Parameters:
*      file (char*): Name of file to be deleted
*  Return:
*      success (int): 0 if succesful, negative if error
*/
int sfs_remove(char* file)


Limitations:

1) The file system was developed to work in Linux (Ubuntu 18.04.5), written and run on a virtual machine to 
ensure compatibility. It is not guaranteed to be cross-compatible with other operating systems.

2) While work to support subdirectories has been made, some functions are still unavailable, mainly
multiple links to one file, moving files, and copying files.

3) An assignment constraint is that the read and write pointer on files be the same. This means, for example, 
that the write calls will update the read pointer, changing affecting future read calls.

4) There are a few features mentioned in this assignment but never developed, leaving space for progress. 
These include soft links (iNode link count), read/write permissions (User ID and Group ID), more metadata
(last access time, creation time, etc.), and more.

5) The size of the file system is determined by constants defined at the top of "sfs_api.c". These define 
block size, file system size (in blocks), iNode table length, and more.

6) File names are 20 characters max (including period) - defined in "sfs_api.h" with MAXFILENAME.

Project Structure:

api          - generally a wrapper for sfs file system internal functions, does most of general checks

directory    - holds directory structures (in header) and functions to modify a directory

inode        - holds iNode structures (in header) and functions to modify iNodes, including writing to, 
               reading from, creating, and deleting them.

free_bit_map - functions to edit the free bit map (for iNodes & data blocks), as well as creating or loading it

super_block  - likely unecessary to keep this separate from api, has functions to load & store the super block

All files are stored in a directory with name "root", though the name isn't used
(the root directory is not stored in any directory). 
The overall file system is stored (saved to) a file named "fs.sfs" that is used for persistent memory.

Given:

"disk_emu"    - a disk emulator that allows block access to storage by exposing function calls that read/write to
                a file ("fs.sfs" here).

"sfs_test<x>" - test files, provided to see whether the file system functions properly. These have been
                modified towards the end to check functions I personally implemented (not required) and
                to ensure nothing is missed. For example, test2 now has tests to see if delete data and 
                remove file (and deletes data) frees all the disk space. test1 now reads data back to ensure 
                single and double indirect block pointers function correctly. Other minor changes and bug fixes
                to these were made. I personally made test 3 (for subdirectories).

                Tests 4 and 5 are special, they were not required and I never focused on making them work.
                They are essentially to use the given wrapper files ("fuse_wrap_new.c" and 
                "fuse_wrap_old.c") to run Filesystem in User Space (FUSE) with our implementation. This 
                software allows us to create user-level file systems (without modifying the kernel) that
                'replace' the normal file editing functions with our own implementation that runs in user-space.
