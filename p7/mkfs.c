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
    if (argc < 7) {
        fprintf(stderr, "Usage: %s -d <disk_img> -i <num_inodes> -b <num_blocks>\n", argv[0]);
        return 1;
    }

    char *d = NULL;
    int i = 0, b = 0;

    for (int j = 1; j < argc; j++) {
        if (strcmp(argv[j], "-d") == 0){
            d = argv[j+1];
        }
        else if (strcmp(argv[j], "-i") == 0){
            i = atoi(argv[j+1]);
        }
        else if (strcmp(argv[j], "-b") == 0){
            b = atoi(argv[j+1]);
        }
    }

    // Ensure at least one inode and data block exist for root directory
    if (!d || i <= 0 || b <= 0) {
        printf("Must have at least one inode and data block for root directory\n");
        return 1;
    }

    // Round number of blocks up to nearest multiple of 32
    if (i % 32 != 0)
        i = i + 32 - (i % 32);
    if (b % 32 != 0)
        b = b + 32 - (b % 32);

    printf("inode count: %d\ndata block count: %d\n", i, b); // TODO: debug

    // Open disk image file, mmap onto memory
    int fd = open(d, O_RDWR | O_CREAT, 0666);
    size_t sb_size = sizeof(struct wfs_sb);
    size_t ibitmap_size = (i + 7) / 8;
    size_t dbitmap_size = (b + 7) / 8;
    size_t inodes_size = i * BLOCK_SIZE;
    size_t data_blocks_size = b * BLOCK_SIZE; // Use bit operations for bitmaps
    size_t filesystem_size = sb_size + ibitmap_size + dbitmap_size + inodes_size + data_blocks_size;


    struct stat statbuf;
    fstat(fd, &statbuf);
    size_t disk_img_size = statbuf.st_size;
    printf("file system size: %ld\n", filesystem_size); // TODO: debug
    printf("disk image size: %ld\n", disk_img_size); // TODO: debug
    if (disk_img_size < filesystem_size) {
        printf("Error: disk image file too small to accomodate number of blocks\n");
        return 1; // TODO: return or exit?
    }

    char *addr = mmap(NULL, filesystem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) {
        return 1; // TODO: return or exit?
    }

    memset(addr, 0, filesystem_size); // Clear disk image file

    // Write superblock
    struct wfs_sb *sb = (struct wfs_sb *) addr;
    sb->num_inodes = i;
    sb->num_data_blocks = b;
    sb->i_bitmap_ptr = sb_size;
    sb->d_bitmap_ptr = sb->i_bitmap_ptr + ibitmap_size;
    sb->i_blocks_ptr = sb->d_bitmap_ptr + dbitmap_size;
    sb->d_blocks_ptr = sb->i_blocks_ptr + inodes_size;

    // Write root inode
    struct wfs_inode *root_inode = (struct wfs_inode*) (addr + sb->i_blocks_ptr); // TODO: implement allocate_inode() (in shared file, so wfs.c can use as well), make sure it looks for inode numbers in order (for this one, must get inode 0)
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
    // root_inode->blocks[0] = sb->d_blocks_ptr; // 0b10000000
    *((int*) (addr + sb->i_bitmap_ptr)) |= 0x01; // Root inode bitmap
    // ((char *)(addr + sb->d_bitmap_ptr))[0] |= 0x01; // Root block bitmap

    // Free memory space
    munmap(addr, filesystem_size);

    return 0;
}
