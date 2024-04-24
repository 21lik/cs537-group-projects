#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

// TODO: finish methods

static int wfs_getattr(const char *path, struct stat *stbuf) {
    printf("Running wfs_getattr\n");
    // Implementation of getattr function to retrieve file attributes
    // Fill stbuf structure with the attributes of the file/directory indicated by path
    // ...


    return 0; // Return 0 on success
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

static int wfs_write(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi) {
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
    .readdir = wfs_readdir, // TODO: fix
};

int main(int argc, char *argv[]) {
    // Initialize FUSE with specified operations
    // Filter argc and argv here and then pass it to fuse_main
    return fuse_main(argc - 1, argv + 1, &ops, NULL); // TODO: make sure argc and argv passed are correct
}
