#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "wfs.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <wasm32-wasi/__mode_t.h>

/**
 * Initialize a file to an empty filesystem.
*/
int main(int argc, char *argv[]) {
    // TODO: test
    char *d = NULL;
    int i = 0, b = 0;
    for (int j = 0; j < argc; j++) {
        if (strcmp(argv[j], "-d") == 0)
            d = argv[++j];
        else if (strcmp(argv[j], "-i") == 0)
            i = atoi(argv[++j]);
        else if (strcmp(argv[j], "-b") == 0)
            b = atoi(argv[++j]);
    }

    // Ensure at least one inode and data block exist for root directory
    if (i <= 0 || d <= 0) {
        printf("Must have at least one inode and data block for root directory\n");
        exit(1); // TODO: return or exit?
    }

    // Round number of blocks up to nearest multiple of 32
    if (i % 32 != 0)
        i += 32 - (i & 31);
    if (b % 32 != 0)
        b += 32 - (b & 31);
    
    printf("inode count: %d\ndata block count: %d\n", i, b); // TODO: debug

    // Open disk image file, mmap onto memory
    int fd = open(d, O_RDWR);
    off_t sb_size = BLOCK_SIZE, ibitmap_size = i >> 3, dbitmap_size = b >> 3, inodes_size = i * BLOCK_SIZE, data_blocks_size = b * BLOCK_SIZE; // Use bit operations for bitmaps
    size_t filesystem_size = sb_size + ibitmap_size + dbitmap_size + inodes_size + data_blocks_size;
    struct stat statbuf;
    fstat(fd, &statbuf);
    size_t disk_img_size = statbuf.st_size;
    printf("file system size: %ld\n", filesystem_size); // TODO: debug
    printf("disk image size: %ld\n", disk_img_size); // TODO: debug
    if (disk_img_size < filesystem_size) {
        printf("Error: disk image file too small to accomodate number of blocks\n");
        exit(1); // TODO: return or exit?
    }

    void *addr = mmap(NULL, filesystem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) {
        exit(1); // TODO: return or exit?
    }

    memset(addr, 0, filesystem_size); // Clear disk image file // TODO: is this necessary?

    // Write superblock
    struct wfs_sb *sb = addr;
    sb->num_inodes = i;
    sb->num_data_blocks = b;
    sb->i_bitmap_ptr = sb_size;
    sb->d_bitmap_ptr = sb_size + ibitmap_size;
    sb->i_blocks_ptr = sb->d_bitmap_ptr + dbitmap_size;
    sb->d_blocks_ptr = sb->i_blocks_ptr + inodes_size;

    // Write root inode
    struct wfs_inode *root_inode = (struct wfs_inode*) (((char*) sb) + sb->i_blocks_ptr); // TODO: implement allocate_inode() (in shared file, so wfs.c can use as well), make sure it looks for inode numbers in order (for this one, must get inode 0)
    root_inode->num = 0;
    root_inode->mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
    root_inode->uid = getuid();
    root_inode->gid = getgid();
    root_inode->size = 0; // TODO: when creating new file/directory, be sure to revise parent directory size (similarly, update file size when writing to file)
    root_inode->nlinks = 1; // TODO: one or zero?
    time_t curr_time = time(NULL);
    root_inode->atim = curr_time;
    root_inode->mtim = curr_time;
    root_inode->ctim = curr_time;
    *(((char*) sb) + sb->i_bitmap_ptr) = (char) 128; // 0b10000000

    // Free memory space
    munmap(addr, filesystem_size);

    return 0;
}