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

struct wfs_inode *find_inode_by_path(const char *path, struct wfs_inode *inode) {
	// TODO: test
    char *path_copy = strdup(path); // Create a mutable copy of the path
    char *token;
    char *rest = path_copy;

    struct wfs_inode current_inode;
    // Start with the root inode
    if (pread(disk_fd, &current_inode, sizeof(struct wfs_inode), superblock->i_blocks_ptr) != sizeof(struct wfs_inode)) {
        free(path_copy);
        perror("Error reading root inode\n");
        return NULL; // Error reading root inode
    }

    token = strtok_r(rest, "/", &rest);
    while (token != NULL) {
        int found = 0;
        // Read all entries in the current directory
        for (int i = 0; i < D_BLOCK; i++) { // Assume D_BLOCK is the count of direct blocks
            if (current_inode.blocks[i] == 0) continue; // No data block assigned

            // Read the directory block
            struct wfs_dentry entries[BLOCK_SIZE / sizeof(struct wfs_dentry)];
            if (pread(disk_fd, entries, BLOCK_SIZE, current_inode.blocks[i]) != BLOCK_SIZE) {
                free(path_copy);
                perror("Error reading block\n");
                return NULL; // Error reading block
            }

            // Search for the token in the directory entries
            for (size_t j = 0; j < (BLOCK_SIZE / sizeof(struct wfs_dentry)); j++) {
                if (strcmp(entries[j].name, token) == 0) {
                    // Load the inode for this entry
                    off_t inode_pos = superblock->i_blocks_ptr + entries[j].num * sizeof(struct wfs_inode);
                    if (pread(disk_fd, &current_inode, sizeof(struct wfs_inode), inode_pos) != sizeof(struct wfs_inode)) {
                        free(path_copy);
                        perror("Error reading inode\n");
                        return NULL; // Error reading inode
                    }
                    found = 1;
                    break;
                }
            }

            if (found) break;
        }

        if (!found) {
            free(path_copy);
            perror("Path component not found\n");
            return NULL; // Path component not found
        }
        token = strtok_r(NULL, "/", &rest);
    }

    free(path_copy);
    *inode = current_inode; // Copy the found inode to the output parameter // TODO: do we need/want this?
    return inode;
}

static int wfs_getattr(const char *path, struct stat *stbuf) {
    printf("Running wfs_getattr\n");
    // Implementation of getattr function to retrieve file attributes
    // Fill stbuf structure with the attributes of the file/directory indicated by path
    // ...
    memset(stbuf, 0, sizeof(struct stat));

    struct wfs_inode inode;
    if (find_inode_by_path(path, &inode) == NULL) {
        return -ENOENT; // No such file or directory
    }

    stbuf->st_ino = inode.num;
    stbuf->st_mode = inode.mode;
    stbuf->st_nlink = inode.nlinks;
    stbuf->st_uid = inode.uid;
    stbuf->st_gid = inode.gid;
    stbuf->st_size = inode.size;
    stbuf->st_blksize = BLOCK_SIZE;
    stbuf->st_atime = inode.atim;
    stbuf->st_mtime = inode.mtim;
    stbuf->st_ctime = inode.ctim;

    return 0;
}

static int wfs_mknod(const char* path, mode_t mode, dev_t rdev) {
    printf("Running wfs_mknod\n");
    // TODO: test
    struct wfs_inode *parent_inode;
    char *new_name = rindex(path, '/');
    if (new_name == NULL) {
        // Parent is root directory
        parent_inode = (struct wfs_inode*) (((char*) superblock) + superblock->i_blocks_ptr);
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
        parent_inode = (struct wfs_inode*) (((char*) superblock) + superblock->i_blocks_ptr);
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
    // TODO: implement

    return 0; // Return 0 on success
}

static int wfs_rmdir(const char *path) {
    printf("Running wfs_rmdir\n");
    // TODO: finish, test
    struct wfs_inode *parent_inode;
    char *dir_name = rindex(path, '/');
    if (dir_name == NULL) {
        // Parent is root directory
        parent_inode = (struct wfs_inode*) (((char*) superblock) + superblock->i_blocks_ptr);
    }
    else {
        // Parent is a non-root directory, split path into parent path and new directory name
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
                // Found directory, clear inode and bits, remove directory entry
                struct wfs_inode *this_inode = (struct wfs_inode*) (((char*) superblock) + superblock->i_blocks_ptr + curr_dentry->num);
                // We assume that the directory is already empty, so we can free all the dentries stored
                int this_block_count = this_inode->size / sizeof(struct wfs_dentry);
                for (int k = 0; k < this_block_count; k++) {
                    int this_datablock_num; // TODO: get datablock number of this_inode->blocks[k] (it's not the dentry num)
                    
                    int datablock_byte = this_datablock_num / 8;
                    int datablock_offset = this_datablock_num % 8;
                    char *this_bitmap = ((char*) superblock) + superblock->d_bitmap_ptr + datablock_byte;
                    char bitmask = (char) (1 << (7 - datablock_offset));
                    *this_bitmap &= ~bitmask;

                    memset(((char*) superblock) + this_inode->blocks[k], 0, BLOCK_SIZE);
                }
                
                // Clear bits in inode/data bitmaps
                int inode_byte = this_inode->num / 8;
                int inode_offset = this_inode->num % 8;
                char *this_bitmap = ((char*) superblock) + superblock->i_bitmap_ptr + inode_byte;
                char bitmask = (char) (1 << (7 - inode_offset));
                *this_bitmap &= ~bitmask;

                int this_datablock_num; // TODO: get datablock number of this_inode->blocks[k] (it's not the dentry num)

                int datablock_byte = curr_dentry->num / 8;
                int datablock_offset = curr_dentry->num % 8;
                this_bitmap = ((char*) superblock) + superblock->d_bitmap_ptr + datablock_byte;
                bitmask = (char) (1 << (7 - datablock_offset));
                *this_bitmap &= ~bitmask;

                // Clear inode, directory entry
                memset(this_inode, 0, sizeof(struct wfs_inode));
                memset(curr_dentry->name, 0, MAX_NAME);
                
                return 0; // Return 0 on success
            }
        }
    }

    return -ENOENT; // File or directory with same name exists // TODO: can a directory and a file share the same name?
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
