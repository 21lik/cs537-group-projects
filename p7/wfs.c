#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include "wfs.h"
#include <fuse.h>
#include <sys/mman.h>

int disk_fd;  			  // File descriptor
struct wfs_sb *superblock;  // Superblock

// TODO: finish methods
struct wfs_inode *allocate_inode() {
	// TODO: test
    

    
    // Calculate the size of the inode bitmap in bytes
    size_t bitmap_size = (superblock->num_inodes + 7) / 8;
    char *inode_bitmap = malloc(bitmap_size);
    if (!inode_bitmap) {
        perror("Failed to allocate memory for inode bitmap\n");
        return NULL;
    }

    // Read the inode bitmap from disk
    ssize_t bytes_read = pread(disk_fd, inode_bitmap, bitmap_size, superblock->i_bitmap_ptr);
    if (bytes_read == -1 || (size_t)bytes_read != bitmap_size) {
        perror("Failed to read inode bitmap from disk\n");
        free(inode_bitmap);
        return NULL;
    }

    size_t inode_index = (size_t)-1;
    for (size_t i = 0; i < superblock->num_inodes; i++) {
        size_t byte_index = i / 8;
        size_t bit_index = i % 8;

        if (!(inode_bitmap[byte_index] & (1 << bit_index))) {
            inode_index = i;
            inode_bitmap[byte_index] |= (1 << bit_index); // Mark the inode as used
            break;
        }
    }

    // Write the updated inode bitmap back to the disk
    ssize_t bytes_written = pwrite(disk_fd, inode_bitmap, bitmap_size, superblock->i_bitmap_ptr);
    if (bytes_written == -1 || (size_t)bytes_written != bitmap_size) {
        perror("Failed to write updated inode bitmap to disk\n");
        free(inode_bitmap);
        return NULL;
    }
    free(inode_bitmap);

    if (inode_index == (size_t)-1) {
        fprintf(stderr, "No free inodes available\n");
        return NULL;
    }

    // Allocate and initialize the inode structure
    struct wfs_inode *inode = malloc(sizeof(struct wfs_inode));
    if (!inode) {
        perror("Failed to allocate memory for inode\n");
        return NULL;
    }
    memset(inode, 0, sizeof(struct wfs_inode));
    inode->num = inode_index;
    inode->mode = 0;  // Default mode, should be set by caller
    inode->uid = getuid();  // Owner's user ID
    inode->gid = getgid();  // Owner's group ID
    inode->nlinks = 1;  // Initially one link
    inode->atim = time(NULL);
    inode->mtim = time(NULL);
    inode->ctim = time(NULL);

    // Write the new inode to the correct place on the disk
    off_t inode_pos = superblock->i_blocks_ptr + inode_index * sizeof(struct wfs_inode);
    bytes_written = pwrite(disk_fd, inode, sizeof(struct wfs_inode), inode_pos);
    if (bytes_written == -1 || (size_t)bytes_written != sizeof(struct wfs_inode)) {
        perror("Failed to write inode to disk");
        free(inode);
        return NULL;
    }

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
    *inode = current_inode; // Copy the found inode to the output parameter
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
    // TODO: implement

    return 0; // Return 0 on success
}

static int wfs_mkdir(const char* path, mode_t mode) {
    printf("Running wfs_mkdir\n");
    // TODO: implement

    return 0; // Return 0 on success
}

static int wfs_unlink(const char* path) {
    printf("Running wfs_unlink\n");
    // TODO: implement

    return 0; // Return 0 on success
}

static int wfs_rmdir(const char *path) {
    printf("Running wfs_rmdir\n");
    // TODO: implement

    return 0; // Return 0 on success
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
		printf("Failed to get shared mmap memory\n");
        return 1;
    }
    superblock = addr;

    // Adjust the arguments for fuse_main
    argc -= 1; // Remove disk_path from argc
    argv[1] = argv[0]; // Shift all arguments to the left
    memmove(&argv[0], &argv[1], sizeof(char *) * argc);
    printf("Calling fuse_main\n"); // TODO: debug
    return fuse_main(argc, argv, &ops, NULL); // TODO: make sure argc and argv passed are correct
}
