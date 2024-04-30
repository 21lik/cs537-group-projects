#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include "wfs.h"
#include <fuse.h>
#include <sys/mman.h>
#include <wasm32-wasi/__mode_t.h>

int disk_fd;  			  // File descriptor
struct wfs_sb *superblock;  // Superblock

// TODO: finish methods

// Get the address to the inode with the given number.
struct wfs_inode *get_inode(int num) {
    return (struct wfs_inode*) (((char*) superblock) + superblock->i_blocks_ptr + num);
}

// Get the data block number for the given data block address.
// Returns the data block number, or -1 if the address is not in the DATA BLOCKS region.
int get_data_block_num(void *data_block_addr) {
	uintptr_t address_offset = (uintptr_t)data_block_addr - ((uintptr_t)superblock + superblock->d_blocks_ptr);
    if (address_offset >= superblock->num_data_blocks * BLOCK_SIZE) {
        return -1;  // The address is outside the range of data blocks
    }
    return address_offset / BLOCK_SIZE;
}

// Clear the data blocks stored in the given inode.
// This function also accounts for indirect pointer(s), if necessary.
// This function can be used for both regular files and directories.
int clear_data_blocks(struct wfs_inode *inode) {
    // TODO: implement
    // Direct pointers
    for (int i = 0; i < IND_BLOCK; i++) {
        if (inode->blocks[i] == 0) {
            return 0; // All data blocks cleared
        }

        // Clear data blocks, respective bits in data bitmap
        int this_datablock_num = get_data_block_num(superblock + inode->blocks[i]);
        int datablock_byte = this_datablock_num / 8;
        int datablock_offset = this_datablock_num % 8;
        char *this_bitmap = ((char*) superblock) + superblock->d_bitmap_ptr + datablock_byte;
        char bitmask = (char) (1 << (7 - datablock_offset));
        *this_bitmap &= ~bitmask;

        memset(((char*) superblock) + inode->blocks[i], 0, BLOCK_SIZE);
    }

    // Indirect pointer
    if (inode->blocks[IND_BLOCK] != 0) {
        off_t *pointer_block = (off_t*) (((char*) superblock) + inode->blocks[IND_BLOCK]);
        for (int i = 0; i < BLOCK_SIZE / sizeof(off_t); i++) {
            if (pointer_block[i] == 0) {
                break; // All data blocks cleared
            }

            // Clear data blocks, respective bits in data bitmap
            int this_datablock_num = get_data_block_num(superblock + pointer_block[i]);
            int datablock_byte = this_datablock_num / 8;
            int datablock_offset = this_datablock_num % 8;
            char *this_bitmap = ((char*) superblock) + superblock->d_bitmap_ptr + datablock_byte;
            char bitmask = (char) (1 << (7 - datablock_offset));
            *this_bitmap &= ~bitmask;

            memset(((char*) superblock) + pointer_block[i], 0, BLOCK_SIZE);
        }

        // Free indirect pointer block
        int this_datablock_num = get_data_block_num(pointer_block);
        int datablock_byte = this_datablock_num / 8;
        int datablock_offset = this_datablock_num % 8;
        char *this_bitmap = ((char*) superblock) + superblock->d_bitmap_ptr + datablock_byte;
        char bitmask = (char) (1 << (7 - datablock_offset));
        *this_bitmap &= ~bitmask;

        memset(pointer_block, 0, BLOCK_SIZE);
    }

    return 0;
}

struct wfs_inode *allocate_inode() {
	// TODO: test
    char *inode_bitmap = ((char*) superblock) + superblock->i_bitmap_ptr;
    size_t bitmap_size = superblock->num_inodes >> 3; // size in bytes
    int inode_num = 0;
    for (int i = 0; i < bitmap_size; i++) {
        if (*inode_bitmap == (char) -1) // 0b11111111, byte full
            continue;

        // Set available bit, return inode pointer
        for (int j = 0; j < 8; j++) {
            if ((*inode_bitmap << j) > 0) { // has 0 in bit j (left-to-right)
                char bitmask = (char) (1 << (7 - j));
                *inode_bitmap |= bitmask;
                inode_num = (i << 3) + j;
                break;
            }
        }
    }

    // No free inode bit found
    if (inode_num == 0) {
        fprintf(stderr, "No free inodes available\n");
        return NULL;
    }

    // Set inode pointer, values
    struct wfs_inode *inode = (struct wfs_inode*) (((char*) superblock) + superblock->i_blocks_ptr + inode_num);
    inode->num = inode_num;
    inode->mode = 0;  // Default mode, should be set by caller
    inode->uid = getuid();  // Owner's user ID
    inode->gid = getgid();  // Owner's group ID
    inode->size = 0;  // Default empty, should be changed by caller if necessary
    inode->nlinks = 1;  // Initially one link // TODO: one or zero?
    time_t curr_time = time(NULL);
    inode->atim = curr_time;
    inode->mtim = curr_time;
    inode->ctim = curr_time;

    return inode;
}

struct wfs_inode *find_inode_by_path(const char *path) {
	// TODO: test
	struct wfs_inode* current_inode = get_inode(0);
	if (current_inode == NULL || path == NULL || path[0] == '\0' || path[0] != '/') {
	    return NULL; // Invalid path or failed to load root inode
	}
	char* path_copy = strdup(path);
	if (path_copy == NULL) return NULL;
	char* token = strtok(path_copy, "/");
	    while (token != NULL && current_inode != NULL) {
	        int found = 0;
	        // Iterate through directory entries in current inode
	        for (int i = 0; i < D_BLOCK; i++) {
	        	if (current_inode->blocks[i] == 0) {
	                continue; // Skip empty block pointers
	        	}
	        	struct wfs_dentry* entries = (struct wfs_dentry*)((char*)superblock + superblock->d_blocks_ptr + current_inode->blocks[i] * BLOCK_SIZE);
	        	for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
	                if (entries[j].num != 0 && strcmp(entries[j].name, token) == 0) {
	                    // Found the next inode in the path
	                    current_inode = get_inode(entries[j].num);
	                    found = 1;
	                    break;
	                }
	            }
	            if (found) break; // Stop searching if we've found the next inode
	        }
	        if (!found) { // If no entry matches token in current directory
	            free(path_copy);
	            return NULL;
	        }
	        token = strtok(NULL, "/");
	    }
	    free(path_copy);
	    return current_inode;		
}

int allocate_block() {
    printf("Allocating a new block\n");
    char *block_bitmap = ((char*) superblock) + superblock->d_bitmap_ptr;
    int num_blocks = superblock->num_data_blocks;

    for (int i = 0; i < num_blocks; i++) {
        int byte_index = i / 8;
        int bit_index = i % 8;

        // Check if the bit is set (block is used)
        if (!(block_bitmap[byte_index] & (1 << bit_index))) {
            // Mark the block as used
            block_bitmap[byte_index] |= (1 << bit_index);
            // Optionally, zero out the block (if not handled elsewhere)
            memset(((char*) superblock) + superblock->d_blocks_ptr + i * BLOCK_SIZE, 0, BLOCK_SIZE);
            return i; // Return the block index
        }
    }

    fprintf(stderr, "No free blocks available\n");
    return -1; // No free blocks available
}

struct wfs_dentry *allocate_dentry(struct wfs_inode *parent_inode) {
    printf("Allocating new directory entry block\n");

    // Check if there is room for a new block pointer in the inode's block array
    if (parent_inode->size / BLOCK_SIZE >= D_BLOCK) {
        fprintf(stderr, "No room for new directory block in inode\n");
        return NULL; // No more blocks can be added
    }

    // Allocate a new block for the directory entries
    int new_block_index = allocate_block(); // allocate_block() should return the block index or -1 if failed
    if (new_block_index == -1) {
        fprintf(stderr, "Failed to allocate block for new directory entries\n");
        return NULL;
    }

    // Calculate the physical address of the new block
    char *new_block_ptr = ((char*) superblock) + superblock->d_blocks_ptr + new_block_index * BLOCK_SIZE;
    memset(new_block_ptr, 0, BLOCK_SIZE); // Initialize the block to zero

    // Update the inode to point to the new block
    parent_inode->blocks[parent_inode->size / BLOCK_SIZE] = new_block_index;
    parent_inode->size += BLOCK_SIZE; // Update the size to reflect the addition of a new block

    return (struct wfs_dentry *)new_block_ptr;
}

static int wfs_getattr(const char *path, struct stat *stbuf) {
    printf("Running wfs_getattr\n");
    // Implementation of getattr function to retrieve file attributes
    // Fill stbuf structure with the attributes of the file/directory indicated by path
    memset(stbuf, 0, sizeof(struct stat));
    struct wfs_inode *inode = find_inode_by_path(path);
    if (inode == NULL) return -ENOENT;  // No such file or directory
    // Populate the stat structure
    stbuf->st_ino = inode->num;
    stbuf->st_mode = inode->mode;
    stbuf->st_nlink = inode->nlinks;
    stbuf->st_uid = inode->uid;
    stbuf->st_gid = inode->gid;
    stbuf->st_size = inode->size;
    stbuf->st_atime = inode->atim;
    stbuf->st_mtime = inode->mtim;
    stbuf->st_ctime = inode->ctim;
    stbuf->st_blksize = BLOCK_SIZE;
    stbuf->st_blocks = (inode->size + 511) / 512;
    return 0;
}

static int wfs_mknod(const char* path, mode_t mode, dev_t rdev) {
    printf("Running wfs_mknod\n");
    // TODO: test
    struct wfs_inode *parent_inode;
    char *new_name = rindex(path, '/');
    if (new_name == NULL) {
        // Parent is root directory
        parent_inode = get_inode(0);
    }
    else {
        // Parent is a non-root directory, split path into parent path and new file name
        *new_name = '\0';
        new_name++;
        parent_inode = find_inode_by_path(path, NULL); // TODO: be sure to revise end of function above, don't copy inode into argument memory if NULL
        if (parent_inode == NULL) {
            return -ENOENT; // No such parent directory
        }
    }

    // Add entry to parent directory
    struct wfs_dentry *this_dentry = NULL;
    int new_block_index = parent_inode->size / sizeof(struct wfs_dentry);
    for (int i = 0; i < new_block_index; i++) {
        struct wfs_dentry *dblock_ptr = ((char*) superblock) + parent_inode->blocks[i];
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            struct wfs_dentry *curr_dentry = dblock_ptr + j;
            if (curr_dentry->num == 0) {
                // Blank dentry found
                this_dentry = curr_dentry;
                break;
            }
            else if (strcmp(new_name, curr_dentry->name) == 0) {
                return -EEXIST; // File or directory with same name exists // TODO: can a directory and a file share the same name?
            }
        }
        if (this_dentry != NULL)
            break;
    }
    if (this_dentry == NULL) {
        // Need to allocate new directory block
        this_dentry = allocate_dentry(); // TODO: implement
        if (this_dentry == NULL) {
            return -ENOSPC; // Insufficient disk space
        }
        parent_inode->blocks[new_block_index] = ((char*) this_dentry) - superblock;
        parent_inode->size += BLOCK_SIZE;
    }

    // Update parent inode
    parent_inode->nlinks++;
    time_t curr_time = time(NULL);
    parent_inode->atim = curr_time;
    parent_inode->mtim = curr_time;
    parent_inode->ctim = curr_time;

    // Fill this directory entry, allocate and fill inode
    strcpy(this_dentry->name, new_name);
    struct wfs_inode *this_inode = allocate_inode();
    this_dentry->num = this_inode->num;
    this_inode->mode = S_IFREG | mode; // TODO: do we want S_IRWXU | S_IRWXG | S_IRWXO even if it's not specified in the mode parameter?

    return 0; // Return 0 on success
}

static int wfs_mkdir(const char* path, mode_t mode) {
    printf("Running wfs_mkdir\n");
    // TODO: test
    struct wfs_inode *parent_inode;
    char *new_name = rindex(path, '/');
    if (new_name == NULL) {
        // Parent is root directory
        parent_inode = get_inode(0);
    }
    else {
        // Parent is a non-root directory, split path into parent path and new directory name
        *new_name = '\0';
        new_name++;
        parent_inode = find_inode_by_path(path, NULL); // TODO: be sure to revise end of function above, don't copy inode into argument memory if NULL
        if (parent_inode == NULL) {
            return -ENOENT; // No such parent directory
        }
    }

    // Add entry to parent directory
    struct wfs_dentry *this_dentry = NULL;
    int new_block_index = parent_inode->size / sizeof(struct wfs_dentry);
    for (int i = 0; i < new_block_index; i++) {
        struct wfs_dentry *dblock_ptr = ((char*) superblock) + parent_inode->blocks[i];
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            struct wfs_dentry *curr_dentry = dblock_ptr + j;
            if (curr_dentry->num == 0) {
                // Blank dentry found
                this_dentry = curr_dentry;
                break;
            }
            else if (strcmp(new_name, curr_dentry->name) == 0) {
                return -EEXIST; // File or directory with same name exists // TODO: can a directory and a file share the same name?
            }
        }
        if (this_dentry != NULL)
            break;
    }
    if (this_dentry == NULL) {
        // Need to allocate new directory block
        this_dentry = allocate_dentry(); // TODO: implement
        if (this_dentry == NULL) {
            return -ENOSPC; // Insufficient disk space
        }
        parent_inode->blocks[new_block_index] = ((char*) this_dentry) - superblock;
        parent_inode->size += BLOCK_SIZE;
    }

    // Update parent inode
    parent_inode->nlinks++;
    time_t curr_time = time(NULL);
    parent_inode->atim = curr_time;
    parent_inode->mtim = curr_time;
    parent_inode->ctim = curr_time;

    // Fill this directory entry, allocate and fill inode
    strcpy(this_dentry->name, new_name);
    struct wfs_inode *this_inode = allocate_inode();
    this_dentry->num = this_inode->num;
    this_inode->mode = S_IFDIR | mode; // TODO: do we want S_IRWXU | S_IRWXG | S_IRWXO even if it's not specified in the mode parameter?

    return 0; // Return 0 on success
}

static int wfs_unlink(const char* path) {
    printf("Running wfs_unlink\n");
    // TODO: test
    struct wfs_inode *parent_inode;
    char *file_name = rindex(path, '/');
    if (file_name == NULL) {
        // Parent is root directory
        parent_inode = get_inode(0);
    }
    else {
        // Parent is a non-root directory, split path into parent path and unwanted file name
        *file_name = '\0';
        file_name++;
        parent_inode = find_inode_by_path(path, NULL); // TODO: be sure to revise end of function above, don't copy inode into argument memory if NULL
        if (parent_inode == NULL) {
            return -ENOENT; // No such parent directory
        }
    }

    int block_count = parent_inode->size / sizeof(struct wfs_dentry);
    for (int i = 0; i < block_count; i++) {
        struct wfs_dentry *dblock_ptr = ((char*) superblock) + parent_inode->blocks[i];
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            struct wfs_dentry *curr_dentry = dblock_ptr + j;
            if (strcmp(file_name, curr_dentry->name) == 0) {
                // Found file, clear data blocks and bits in data bitmap
                struct wfs_inode *this_inode = get_inode(curr_dentry->num);
                clear_data_blocks(this_inode);

                // Clear bits in inode bitmap
                int inode_byte = this_inode->num / 8;
                int inode_offset = this_inode->num % 8;
                char *this_bitmap = ((char*) superblock) + superblock->i_bitmap_ptr + inode_byte;
                char bitmask = (char) (1 << (7 - inode_offset));
                *this_bitmap &= ~bitmask;

                // Clear inode
                memset(this_inode, 0, sizeof(struct wfs_inode));

                // Update parent inode
                parent_inode->nlinks--;
                time_t curr_time = time(NULL);
                parent_inode->atim = curr_time;
                parent_inode->mtim = curr_time;
                parent_inode->ctim = curr_time;

                return 0; // Return 0 on success
            }
        }
    }

    return -ENOENT; // No such file exists
}

static int wfs_rmdir(const char *path) {
    printf("Running wfs_rmdir\n");
    // TODO: finish, test
    struct wfs_inode *parent_inode;
    char *dir_name = rindex(path, '/');
    if (dir_name == NULL) {
        // Parent is root directory
        parent_inode = get_inode(0);
    }
    else {
        // Parent is a non-root directory, split path into parent path and unwanted directory name
        *dir_name = '\0';
        dir_name++;
        parent_inode = find_inode_by_path(path, NULL); // TODO: be sure to revise end of function above, don't copy inode into argument memory if NULL
        if (parent_inode == NULL) {
            return -ENOENT; // No such parent directory
        }
    }

    int block_count = parent_inode->size / sizeof(struct wfs_dentry);
    for (int i = 0; i < block_count; i++) {
        struct wfs_dentry *dblock_ptr = ((char*) superblock) + parent_inode->blocks[i];
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            struct wfs_dentry *curr_dentry = dblock_ptr + j;
            if (strcmp(dir_name, curr_dentry->name) == 0) {
                // Found directory, free all the dentries stored (we safely assume the directory is empty)
                struct wfs_inode *this_inode = get_inode(curr_dentry->num);
                clear_data_blocks(this_inode);

                // Clear bits in inode bitmap
                int inode_byte = this_inode->num / 8;
                int inode_offset = this_inode->num % 8;
                char *this_bitmap = ((char*) superblock) + superblock->i_bitmap_ptr + inode_byte;
                char bitmask = (char) (1 << (7 - inode_offset));
                *this_bitmap &= ~bitmask;

                // Clear inode, parent dentry
                memset(this_inode, 0, sizeof(struct wfs_inode));
                memset(curr_dentry, 0, sizeof(struct wfs_dentry));

                // Update parent inode
                parent_inode->nlinks--;
                time_t curr_time = time(NULL);
                parent_inode->atim = curr_time;
                parent_inode->mtim = curr_time;
                parent_inode->ctim = curr_time;

                return 0; // Return 0 on success
            }
        }
    }

    return -ENOENT; // No such directory exists
}

static int wfs_read(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    printf("Running wfs_read\n");
    // TODO: implement

    return 0; // Return 0 on success
}

static int wfs_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    printf("Running wfs_write\n");
    // TODO: implement

    return 0; // Return 0 on success
}

static int wfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
    printf("Running wfs_readdir\n");
    // TODO: implement

    return 0; // Return 0 on success
}

static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .mknod   = wfs_mknod,
    .mkdir   = wfs_mkdir,
    .unlink  = wfs_unlink,
    .rmdir   = wfs_rmdir,
    .read    = wfs_read,
    .write   = wfs_write,
    .readdir = wfs_readdir,
};

int main(int argc, char *argv[]) {
    // Initialize FUSE with specified operations
    // Filter argc and argv here and then pass it to fuse_main
	// Usage: ./wfs disk_path [FUSE options] mount_point
    if (argc < 4) {
        printf("Usage: ./wfs disk_path [FUSE options] mount_point\n");
        return 1;
    }

    // Open disk image
    disk_fd = open(argv[1], O_RDWR);
    if (disk_fd == -1) {
        printf("Failed to open disk image\n");
        return 1;
    }

    // Get the file size
    struct stat statbuf;
    fstat(disk_fd, &statbuf);

    printf("statbuf.st_size: %ld\n", statbuf.st_size); // TODO: debug

    // Get a pointer to the shared mmap memory
	void *addr = mmap(NULL, statbuf.st_size, PROT_WRITE | PROT_READ, MAP_SHARED, disk_fd, 0);
	close(disk_fd);
	if (addr == (void*) -1) {
		printf("Failed to mmap memory\n");
        return 1;
    }
    superblock = addr;

    // Write superblock
    printf("num inodes in superblock: %ld\n", superblock->num_inodes); // TODO: debug
    printf("mode of root directory: %d\n", ((struct wfs_inode*) (((char*) superblock) + superblock->i_blocks_ptr))->mode); // TODO: debug

    // Adjust the arguments for fuse_main
    argc--; // Remove disk_path from argc
    for (int i = 1; i < argc; i++) {
        argv[i] = argv[i + 1];
    }
    printf("Calling fuse_main\n"); // TODO: debug
    int output = fuse_main(argc, argv, &ops, NULL); // TODO: make sure argc and argv passed are correct
    printf("output=%d\n", output);
    return output;
}
