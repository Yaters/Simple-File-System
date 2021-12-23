#ifndef SFS_API_H
#define SFS_API_H

#define MAXFILENAME 20

/* Formats the disk emulator virtual disk, and creates the simple file system 
*  instance on it.
*  Parameters:
*      fresh (bool): create file system from scratch (reset). 1 for new disk, 0 to load existing.
*/
void mksfs(int fresh);


/* Find next directory file. Used to loop through files in directory.
*  Return non-zero if new file was saved in fname. 
*  Parameters:
*      fname  (char*): buffer to save file name in
*  Return:
*      success (bool): Whether file name was saved (vs. reached end of directory)
*/
int sfs_getnextfilename(char* fname);


/* Return the size of a file.
*  Parameters:
*      path (char*): Path of file to find size of
*  Return:
*      size   (int): Size of the file, in bytes
*/
int sfs_getfilesize(const char* path);


/* Open a file (load iNode to cache) and return index of file descriptor table entry.
*  If a file does not exist, it will be created with size 0.
*  File by default opens with the write pointer at the end of the file (writes will append).
*  Parameters:
*      name (char*): Name of file to open
*  Return:
*      fileID (int): File descriptor table index of the opened file (negative on error)
*/
int sfs_fopen(char* name);


/* Close a file, removing it from the file descriptor table.
*  Parameters:
*      fileID  (int): Index of file descriptor table entry to remove
*  Return:
*      success (int): 0 if succesful, negative if error
*/
int sfs_fclose(int fileID);


/* Write to an opened file (at the current location of the read/write pointer).
*  Parameters:
*      fileID   (int): File index in file descriptor table
*      buffer (char*): Buffer of data to be written
*      length   (int): Number of bytes to write (size of buffer)
*  Return:
*      length   (int): Number of bytes written
*/
int sfs_fwrite(int fileID, const char* buf, int length);

/* Delete data from an opened file (data just before the current location of the read/write pointer).
*  Parameters:
*      fileID   (int): File index in file descriptor table
*      length   (int): Number of bytes to delete
*  Return:
*      length   (int): Number of bytes deleted
*/
int sfs_fdelete(int fileID, int length);


/* Read an opened file (at the current location of the read/write pointer).
*  Parameters:
*      fileID   (int): File index in file descriptor table
*      buffer (char*): Buffer to save read data within
*      length   (int): Number of bytes to read
*  Return:
*      length   (int): Number of bytes read
*/
int sfs_fread(int fileID, char* buf, int length);


/* Move read/write pointer to the given location.
*  Parameters:
*      fileID  (int): File index in file descriptor table
*      loc     (int): Location to move read/write pointer to (byte location)
*  Return:
*      success (int): 0 if succesful, negative if error
*/
int sfs_fseek(int fileID, int loc);


/* Delete a file from the current SFS directory.
*  Parameters:
*      file (char*): Name of file to be deleted
*  Return:
*      success (int): 0 if succesful, negative if error
*/
int sfs_remove(char* file);

#endif
