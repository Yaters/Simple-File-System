/* sfs_test3.c 
 * Written by Yanis Jallouli.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sfs_api.h"

// Use MAXFILENAME from sfs_api.c

/* The maximum number of subdirectories to attempt to open
 */
#define MAX_SUBDIR 100 

/* The maximum number of bytes we'll try to write to a file. If you
 * support much shorter or larger files for some reason, feel free to
 * reduce this value.
 */
#define MAX_BYTES 30000 /* Maximum file size I'll try to create */
#define MIN_BYTES 10000         /* Minimum file size */

/* rand_name() - return a randomly-generated, but legal, file name.
 *
 * This function creates a filename of the form xxxxxxxx.xxx, where
 * each 'x' is a random upper-case letter (A-Z). Feel free to modify
 * this function if your implementation requires shorter filenames, or
 * supports longer or different file name conventions.
 * 
 * The return value is a pointer to the new string, which may be
 * released by a call to free() when you are done using the string.
 */
 
char *rand_name() 
{
  char fname[MAXFILENAME];
  int i;

  for (i = 0; i < MAXFILENAME; i++) {
    if (i != 16) {
      fname[i] = 'A' + (rand() % 26);
    }
    else {
      fname[i] = '.';
    }
  }
  fname[i] = '\0';
  return (strdup(fname));
}


/* The main testing program
 */
int
main(int argc, char **argv) {
    char *buffer;
    char fixedbuf[1024];
    int fds[MAX_SUBDIR];
    char *file_names[MAX_SUBDIR + 1];
    char *dir_names[MAX_SUBDIR];
    //int filesize[MAX_SUBDIR];
    int num_subdir_made = 0; // Number of subdirectories made
    int error_count = 0;

    mksfs(1);                     /* Initialize the file system. */

    char write_str_base[] = "Test string for writing in file: ";
    strcpy(fixedbuf, write_str_base);

    printf("Beginning subdirectory test, creating long branch of directories\n");

    // Create trees of directories (depth N/2)
    
    int i = 0;
    for(; i < MAX_SUBDIR / 2; i++) {
      file_names[i] = rand_name();
      dir_names[i] = rand_name();

      // We make a file at each directory level and write to it
      fds[i] = sfs_fopen(file_names[i]);
      if(fds[i] < 0) {
        fprintf(stderr, "Failed to open file in directory number %d named %s\n", i, dir_names[i]);
        error_count++;
      } else {
        int size = snprintf(fixedbuf, sizeof(write_str_base) + 10, "%s %d", write_str_base, i);
        size++; // To include null
        int bytes_written = sfs_fwrite(fds[i], fixedbuf, size);
        if(bytes_written != size) {
          fprintf(stderr, "Expected to write %d bytes to file, got %d\n", size, bytes_written);
          error_count++;
        }
      }

      if(sfs_mkdir(dir_names[i]) < 0) {
        fprintf(stderr, "Failed to create directory number %d named %s\n", i, dir_names[i]);
        error_count++;
        break;
      }
      if(sfs_loaddir(dir_names[i]) < 0) {
        fprintf(stderr, "Failed to load created directory number %d named %s\n", i, dir_names[i]);
        error_count++;
        break;
      }
      num_subdir_made++;
    }
    // Write the file in the last directory
    file_names[i] = rand_name();
    fds[i] = sfs_fopen(file_names[i]);
    int size = snprintf(fixedbuf, sizeof(write_str_base) + 5, "%s %d", write_str_base, i);
    sfs_fwrite(fds[i], fixedbuf, size);
    printf("Succesfully created %d directories with one file in each\n", num_subdir_made);
    printf("Ensuring files are open and can be read correctly\n");

    buffer = malloc(sizeof(write_str_base) + 10);

    // Check all the fids are still open, read correctly, then close
    for(i = 0; i < MAX_SUBDIR / 2; i++) {
      int size = snprintf(fixedbuf, sizeof(write_str_base) + 10, "%s %d", write_str_base, i);
      sfs_fseek(fds[i], 0);
      int bytes_read = sfs_fread(fds[i], buffer, size + 1000);
      if(bytes_read != size + 1) {
        fprintf(stderr, "ERROR: Read %d bytes from file, expected %d\n", bytes_read, size + 1);
        error_count++;
      }
      for(int j = 0; j < bytes_read; j++) {
        if(buffer[j] != fixedbuf[j]) {
          fprintf(stderr, "ERROR: Data error at offset %d in file %d, expected %c but got %c\n", j, i, fixedbuf[j], buffer[j]);
          error_count++;
          break;
        }
      }
      if(sfs_fclose(fds[i]) < 0) {
        fprintf(stderr, "ERROR: Failed to close file number %d\n", i);
        error_count++;
      }
    }

    char* new_mem = realloc(buffer, MAXFILENAME);
    if(new_mem == NULL) {
      fprintf(stderr, "ERROR: Failed to reallocate buffer memory, exiting test\n");
      exit(EXIT_FAILURE);
    }
    buffer = new_mem;

    printf("Read file test finished, checking directory structure is maintained (branch to parent and check names)\n");

    // Go current lowest subdir to root, ensure file names are correct

    // Start by checking last and going out one (this is simpler iteration of loop below)
    int found = sfs_getnextfilename(buffer);
    if(!found) {
      fprintf(stderr, "ERROR: Did not find a file in lowest subdirectory\n");
      error_count++;
    }
    if(strcmp(buffer, file_names[i]) != 0) {
      fprintf(stderr, "ERROR: Lowest subdirectory expected file %s, found file %s\n", file_names[i], buffer);
      error_count++;
    }
    found = sfs_getnextfilename(buffer);
    if(found) {
      fprintf(stderr, "ERROR: Found two files in lowest subdirectory\n");
      error_count++;
    }

    // Load the parent directory
    int loaded = sfs_loaddir("..") >= 0;
    if (i != 0 && !loaded) {
      fprintf(stderr, "ERROR: Failed to load parent directory of lowest subdirectory\n");
      error_count++;
    } else if (i == 0 && loaded) {
      fprintf(stderr, "ERROR: Succesfully loaded parent directory from root\n");
      error_count++;
    }
    i--;
    
    for(; i >= 0; i--) {
      found = sfs_getnextfilename(buffer);
      if(!found) {
        fprintf(stderr, "ERROR: Did not find a file OR subdirectory in subdirectory %d\n", i);
        error_count++;
        continue;
      }

      // Found file, look for directory next
      if(strcmp(file_names[i], buffer) == 0) {
        found = sfs_getnextfilename(buffer);
        if(!found || strcmp(buffer, dir_names[i]) != 0) {
          fprintf(stderr, "ERROR: Second file in directory %d is %s, expected subdirectory %s, found status is %d\n", i, buffer, dir_names[i], found);
          error_count++;
        }

      // Found directory, look for file next
      } else if (strcmp(dir_names[i], buffer) == 0) {
        sfs_getnextfilename(buffer);
        if(!found || strcmp(buffer, file_names[i]) != 0) {
          fprintf(stderr, "ERROR: Second file in directory %d is %s, expected file %s, found status is %d\n", i, buffer, file_names[i], found);
          error_count++;
        }

      // Did not find file or directory
      } else {
        fprintf(stderr, "ERROR: Given file name %s does not match expected directory (%s) or file (%s) name in subdirectory %d\n", buffer, dir_names[i], file_names[i], i);
        error_count++;
      }

      found = sfs_getnextfilename(buffer);
      if (found) {
        fprintf(stderr, "ERROR: Found a file named %s in subdirectory %d, expected only 2\n", buffer, i);
        error_count++;
      }

      // Load the parent directory
      loaded = sfs_loaddir("..") >= 0;
      if (i != 0 && !loaded) {
        fprintf(stderr, "ERROR: Failed to load parent directory\n");
        error_count++;
        break;
      } else if (i == 0 && loaded) {
        fprintf(stderr, "ERROR: Succesfully loaded parent directory from root\n");
        error_count++;
        break;
      }
    }


    mksfs(0); // Reload file system
    printf("Reloading file system, checking directory structure once more\n");

    // Go root to lowest subdir, ensure file names are correct
    for(i = 0; i < MAX_SUBDIR / 2; i++) {
      int found = sfs_getnextfilename(buffer);
      if(!found) {
        fprintf(stderr, "ERROR: Did not find a file OR subdirectory in subdirectory %d\n", i);
        error_count++;
        continue;
      }

      // Found file, look for directory next
      if(strcmp(file_names[i], buffer) == 0) {
        found = sfs_getnextfilename(buffer);
        if(!found || strcmp(buffer, dir_names[i]) != 0) {
          fprintf(stderr, "ERROR: Second file in directory %d is %s, expected subdirectory %s, found status is %d\n", i, buffer, dir_names[i], found);
          error_count++;
          continue;
        }

      // Found directory, look for file next
      } else if (strcmp(dir_names[i], buffer) == 0) {
        sfs_getnextfilename(buffer);
        if(!found || strcmp(buffer, file_names[i]) != 0) {
          fprintf(stderr, "ERROR: Second file in directory %d is %s, expected file %s, found status is %d\n", i, buffer, file_names[i], found);
          error_count++;
          continue;
        }

      // Did not find file or directory
      } else {
        fprintf(stderr, "ERROR: Given file name %s does not match expected directory (%s) or file (%s) name in subdirectory %d\n", buffer, dir_names[i], file_names[i], i);
        error_count++;
        continue;
      }

      found = sfs_getnextfilename(buffer);
      if (found) {
        fprintf(stderr, "ERROR: Found a file named %s in subdirectory %d, expected only 2\n", buffer, i);
        error_count++;
      }

      // Load the subdirectory
      if (sfs_loaddir(dir_names[i]) < 0) {
        fprintf(stderr, "ERROR: Failed to load subdirectory index %d, name %s\n", i, dir_names[i]);
        error_count++;
        break;
      }
    }

    // Test recursive delete (open small tree, fill the disk, go to root, delete top dir)
    
    // Create branches in a nested directory
    while(sfs_loaddir("..") >= 0);
    // Just to make sure, can't hurt
    if(sfs_loaddir(dir_names[1]) >= 0) {
      fprintf(stderr, "ERROR: Loaded directory not existing in current subdirectory\n");
      error_count++;
    }
    if(sfs_loaddir(dir_names[0]) < 0) {
      fprintf(stderr, "ERROR: Could not load existing subdirectory\n");
      error_count++;
    }
    if(sfs_loaddir(dir_names[1]) < 0) {
      fprintf(stderr, "ERROR: Could not load existing subdirectory\n");
      error_count++;
    }
    sfs_mkdir("TomatosAreFun.uks");
    sfs_mkdir("PotatosAreFun.uks");
    sfs_loaddir("TomatosAreFun.uks");
    // Create subdirectory file
    int ex_fd = sfs_fopen("TomatoVillage.txt");

    printf("Filling disk in preparation for removal test, may take a while...\n");

    // Fill with repeated writes
    if (ex_fd >= 0) {
      for (i = 0; i < 100000; i++) {
        int x;

        if ((i % 100) == 0) {
          fprintf(stderr, "%d\r", i);
        }

        memset(fixedbuf, (char)i, sizeof(fixedbuf));
        x = sfs_fwrite(ex_fd, fixedbuf, sizeof(fixedbuf));
        if (x != sizeof(fixedbuf)) {
          printf("Found that disk has %d bytes remaining for writing\n", i * (int)sizeof(fixedbuf) + x);
          break;
        }
      }
      sfs_fclose(ex_fd);
    } else {
      fprintf(stderr, "ERROR: failed to open file TomatoVillage.txt for repeated writes test\n");
    }

    // Delete top level subdirectory 
    // Leave "TomatosAreFun.uks", dir_names[1], and dir_names[0]
    if(sfs_loaddir("..") < 0) {
      fprintf(stderr, "ERROR: Failed to load parent directory 3\n");
      error_count++;
    }
    if(sfs_loaddir("..") < 0) {
      fprintf(stderr, "ERROR: Failed to load parent directory 2\n");
      error_count++;
    }
    if(sfs_loaddir("..") < 0) {
      fprintf(stderr, "ERROR: Failed to load parent directory 1\n");
      error_count++;
    }
    if(sfs_loaddir("..") >= 0) {
      fprintf(stderr, "ERROR: Loaded parent directory from root\n");
      error_count++;
    }
    if(sfs_remove(dir_names[0]) < 0) {
      fprintf(stderr, "Failed to remove directory file\n");
      error_count++;
    }
    if(!sfs_getnextfilename(buffer) || strcmp(buffer, file_names[0]) != 0) {
      fprintf(stderr, "ERROR: Directory deletion deleted file along with directory\n");
      error_count++;
    }
    if(sfs_getnextfilename(buffer)) {
      fprintf(stderr, "ERROR: Deleted directory (%s) still present in file system, found unexpected file %s in root\n", dir_names[0], buffer);
      error_count++;
    }

    printf("Ensuring space has been cleared with remove, filling new file with repeated writes. May take a while...\n");
    ex_fd = sfs_fopen("HotPocketVillage.txt");

    // Fill with repeated writes
    if (ex_fd >= 0) {
      for (i = 0; i < 100000; i++) {
        int x;

        if ((i % 100) == 0) {
          fprintf(stderr, "%d\r", i);
        }

        memset(fixedbuf, (char)i, sizeof(fixedbuf));
        //x = sfs_fwrite(fds[0], fixedbuf, sizeof(fixedbuf));
        x = sfs_fwrite(ex_fd, fixedbuf, sizeof(fixedbuf));
        if (x != sizeof(fixedbuf)) {
          int written = i * (int)sizeof(fixedbuf) + x;
          printf("Found that disk has %d bytes remaining for writing\n", written);
          // With all the directories deleted, should have most all disk space remaining
          if(written < sizeof(fixedbuf) * 100) {
            fprintf(stderr, "Wrote less than %d bytes, disk likely not cleared correctly\n", 100 * sizeof(fixedbuf));
            
            error_count++;
          }
          break;
        }
      }
      sfs_fclose(ex_fd);
    } else {
      fprintf(stderr, "ERROR: failed to open file HotPocketVillage for repeated writes (removal) test\n");
    }


    // NOTE: This is just testing delete. Testing a full tree structure integrity seems difficult.
    // At this stage, more black-box tests may be better, needs a GUI or CLI
 
    printf("Test program exiting with %d errors\n", error_count);
    return (error_count);
}