# Sorry for the comment dump. This is the same makefile, I just took it as a learning opportunity

# FLAG EXPLANATIONS:
# -c = compile to .o files
# -g = produce debug info, reference core info to symbol info on core dump (forced or crash)
# -ansi = -std=c90 (or 89?)
# -pedantic = Issue all the warnings demanded by strict ISO C for -ansi mode = more warnings = more strict
# -Wall = enable (show) warning flags. Would need -Werror to make it throw errors
# -std = change C mode to our true mode of c99 (-ansi was for pedantic check?)
# LDFLAGS Load flags - compile flags appended

CFLAGS = -c -g -ansi -pedantic -Wall -std=gnu99 `pkg-config fuse --cflags --libs`

LDFLAGS = `pkg-config fuse --cflags --libs`

# Uncomment on of the following three lines to compile
# SOURCES= disk_emu.c sfs_api.c sfs_inode.c sfs_directory.c sfs_free_bit_map.c sfs_super_block.c sfs_test0.c
# SOURCES= disk_emu.c sfs_api.c sfs_inode.c sfs_directory.c sfs_free_bit_map.c sfs_super_block.c sfs_test1.c
SOURCES= disk_emu.c sfs_api.c sfs_inode.c sfs_directory.c sfs_free_bit_map.c sfs_super_block.c sfs_test2.c
# SOURCES= disk_emu.c sfs_api.c sfs_inode.c sfs_directory.c sfs_free_bit_map.c sfs_super_block.c fuse_wrap_old.c
# SOURCES= disk_emu.c sfs_api.c sfs_inode.c sfs_directory.c sfs_free_bit_map.c sfs_super_block.c fuse_wrap_new.c


# Make OBJECTS variable same as source name but with .o
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=sfs


# Just lists sources & headers & executable I think
all: $(SOURCES) $(HEADERS) $(EXECUTABLE)


# Linking objects with LDFLAGS into "sfs"
$(EXECUTABLE): $(OBJECTS)
	gcc $(OBJECTS) $(LDFLAGS) -o $@


# Compile first argument using CFLAGS, output into rule name (%.o): Note .c.o expands to "%.o : %.c"
.c.o:
	gcc $(CFLAGS) $< -o $@


# remove (rf = recursive & force) all object and executable files (~ is end of backup files text editors use)
clean:
	rm -rf *.o *~ $(EXECUTABLE) fs.sfs
