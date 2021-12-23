#ifndef DISK_H
#define DISK_H

int init_fresh_disk(char *filename, int block_size, int num_blocks);
int init_disk(char *filename, int block_size, int num_blocks);
int read_blocks(int start_address, int nblocks, void *read_buffer);
int write_blocks(int start_address, int nblocks, void *write_buffer);
int close_disk();

#endif
