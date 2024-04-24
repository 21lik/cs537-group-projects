#include <stdio.h>
#include <wfs.h>
#include <sys/mman.h>
#include <fcntl.h>

/**
 * Initialize a file to an empty filesystem.
*/
int main(int argc, char *argv[]) {

    char *d = NULL;
    int i = 0, b = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0)
            d = argv[++i];
        else if (strcmp(argv[i], "-i") == 0)
            i = atoi(argv[++i]);
        else if (strcmp(argv[i], "-b") == 0)
            b = atoi(argv[++i]);
    }

    // Round number of blocks up to nearest multiple of 32
    if (i % 32 != 0)
        i += 32 - (i % 32);
    if (b % 32 != 0)
        b += 32 - (b % 32);

    
    // Open disk image file, mmap onto memory
    int fd = open(d, O_RDWR);
    int sb_size = BLOCK_SIZE, ibitmap_size = i, dbitmap_size = b, inodes_size = i * BLOCK_SIZE, data_blocks_size = b * BLOCK_SIZE; // Use bit operations for bitmaps
    int filesystem_size = sb_size + ibitmap_size + dbitmap_size + inodes_size + data_blocks_size;
    void *addr = mmap(NULL, filesystem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) {
        return 1; // TODO: return or exit?
    }

    // TODO: write superblock and root inode to disk image
    

    // TODO: finish
}