#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include "wfs.h"
#include <fuse.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

char* disk; // file backed mmap
struct wfs_sb* sb;
int dentry_loc;

int find_dentry(off_t offset, char *name) {
  struct wfs_dentry *dentry;
  int found = 0;
  for (int i = 0; i < sizeof(struct wfs_dentry); i++) {
    // memcpy(&dentry, disk + offset + i * sizeof(struct wfs_dentry), sizeof(struct wfs_dentry));
    dentry = ((struct wfs_dentry*) (disk + offset)) + i;
    // find this name
    if (strcmp(dentry->name, name) == 0) {
      dentry_loc = i;
      return dentry->num;
    }
    if(dentry->num > 0) found = 1;
  }
  if(found != 1) return -2;
  return -1;
}

void find_inode_number_by_path(char *path, int *parent_num, int *inode_num) {
    char *name;
    int prev = 0, curr = 0; // Starting from root inode
    name = strtok(path, "/");

    while (name != NULL) {
        struct wfs_inode *inode;
        // memcpy(&inode, disk + sb->i_blocks_ptr + curr * BLOCK_SIZE, sizeof(struct wfs_inode));
        inode = (struct wfs_inode*) (disk + sb->i_blocks_ptr + curr * BLOCK_SIZE);

        int found = -1;
        for (int i = 0; i <= D_BLOCK; i++) {
            int i_num = find_dentry(inode->blocks[i], name);
            if (i_num > 0) {
                found = i_num;
                break;
            }
        }
        prev = curr;
        curr = found;
        if (curr == -1) { // File not found
            *parent_num = prev;
            *inode_num = -1;
            return;
        }

        name = strtok(NULL, "/");
    }

    *parent_num = prev;
    *inode_num = curr;
}

int allocate_block(size_t addr, int size) {
  int row = 0;
  while (row < size / 32) {
    int old_bit;
    memcpy(&old_bit, disk + addr + row * sizeof(int), sizeof(int));
    if (~old_bit == 0) {
      row++;
      continue;
    }
    for (int col = 0; col < 32; col++) {
      if (((1 << col) & ~old_bit) != 0) {
        return row * 32 + col;
      }
    }
    row++;
  }
  return -1;
}

int flip_bit_in_bitmap(off_t offset, int block_num) {
      int row = block_num / 32;
      int col = block_num % 32;
      int old, new;
      memcpy(&old, disk + offset + row * sizeof(int), sizeof(int));
      new = old ^ (1 << col);
      memcpy(disk + offset + row * sizeof(int), &new, sizeof(int));
      return 0;
}

static int wfs_getattr(const char *path, struct stat *stbuf) {
  printf("getattr called\n");

  char tmp_path[50];
  strcpy(tmp_path, path);
  int inode_num = 0;
  int parent_num = 0;

  find_inode_number_by_path(tmp_path, &parent_num, &inode_num);

  // check if file exists
  if (inode_num < 0) return -ENOENT;

  struct wfs_inode inode;
  memcpy(&inode, disk + sb->i_blocks_ptr + inode_num * BLOCK_SIZE, sizeof(struct wfs_inode));

  // fill in the stbuf
  stbuf->st_ino = inode_num;
  stbuf->st_mode = inode.mode;
  stbuf->st_nlink = inode.nlinks;
  stbuf->st_uid = inode.uid;
  stbuf->st_gid = inode.gid;
  stbuf->st_size = inode.size;
  stbuf->st_atime = inode.atim;
  stbuf->st_mtime = inode.mtim;
  stbuf->st_ctime = inode.ctim;
  stbuf->st_blocks = (inode.size % BLOCK_SIZE == 0 ? inode.size / BLOCK_SIZE : inode.size / BLOCK_SIZE + 1);
  stbuf->st_blksize = BLOCK_SIZE;

  return 0; // Return 0 on success
}

static int wfs_mknod(const char *path, mode_t mode, dev_t rdev) {
  printf("mknod called\n");

  char tmp_path[50];
  char name[50];
  struct wfs_inode inode = {0};
  struct wfs_inode* parent_inode;
  int parent_num = 0;
  int inode_num = 0;

  strcpy(tmp_path, path);
  find_inode_number_by_path(tmp_path, &parent_num, &inode_num);
  if (inode_num != -1) return -EEXIST;
  strcpy(tmp_path, path);
  char *new_tmp_path = strtok(tmp_path, "/");
  while (new_tmp_path != NULL){
      strcpy(name, new_tmp_path);
      new_tmp_path = strtok(NULL, "/");
  }

  // update parent
  parent_inode = (struct wfs_inode*) (disk + sb->i_blocks_ptr + parent_num * BLOCK_SIZE);
  for (int i = 0; i <= D_BLOCK; i++) {
    if(parent_inode->blocks[i] == 0) {
      int new_block = allocate_block(sb->d_bitmap_ptr, sb->num_data_blocks);
      if (new_block == -1) return -ENOSPC;
      parent_inode->blocks[i] = sb->d_blocks_ptr + new_block * BLOCK_SIZE;

      flip_bit_in_bitmap(sb->d_bitmap_ptr, new_block);
    }

    for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
      struct wfs_dentry* dentry = (struct wfs_dentry*) (disk + parent_inode->blocks[i] + j * sizeof(struct wfs_dentry));
      if (dentry->num == 0) {

        // create inode // TODO: here or previous place?
        int inode_number = allocate_block(sb->i_bitmap_ptr, sb->num_inodes);
        if (inode_number == -1) return -ENOSPC;

        time_t seconds = time(NULL);
        inode.num = inode_number;
        inode.mode = mode | S_IFREG;
        inode.uid = getuid();
        inode.gid = getgid();
        inode.size = 0;
        inode.nlinks = 1;
        inode.atim = inode.mtim = inode.ctim = seconds;

        strcpy(dentry->name, name);
        dentry->num = inode_number;

        memcpy(disk + sb->i_blocks_ptr + inode_number * BLOCK_SIZE, &inode, sizeof(struct wfs_inode));
        flip_bit_in_bitmap(sb->i_bitmap_ptr, inode_number);

        parent_inode->nlinks++;
        return 0;
      }
    }
  }

  return -ENOSPC;
}

static int wfs_mkdir(const char *path, mode_t mode) {
  printf("mkdir called\n");

  char tmp_path[50];
  char name[50];
  struct wfs_inode inode = {0};
  struct wfs_inode* parent_inode;
  int parent_num = 0;
  int inode_num = 0;

  strcpy(tmp_path, path);
  find_inode_number_by_path(tmp_path, &parent_num, &inode_num);
  if (inode_num != -1) return -EEXIST;

  strcpy(tmp_path, path);
  char *new_tmp_path = strtok(tmp_path, "/");
  while (new_tmp_path != NULL){
      strcpy(name, new_tmp_path);
      new_tmp_path = strtok(NULL, "/");
  }

  // update parent
  parent_inode = (struct wfs_inode*) (disk + sb->i_blocks_ptr + parent_num * BLOCK_SIZE);
  for (int i = 0; i <= D_BLOCK; i++) {
    if(parent_inode->blocks[i] == 0) {
      int new_block = allocate_block(sb->d_bitmap_ptr, sb->num_data_blocks);
      if (new_block == -1) return -ENOSPC;
      parent_inode->blocks[i] = sb->d_blocks_ptr + new_block * BLOCK_SIZE;

      flip_bit_in_bitmap(sb->d_bitmap_ptr, new_block);
    }

    for (int j = 0; j < (int)(BLOCK_SIZE / sizeof(struct wfs_dentry)); j++) {
      struct wfs_dentry* dentry = (struct wfs_dentry*) (disk + parent_inode->blocks[i] + j * sizeof(struct wfs_dentry));
      if (dentry->num == 0) {

        // create inode // TODO: here or previous place?
        int inode_number = allocate_block(sb->i_bitmap_ptr, sb->num_inodes);
        if (inode_number == -1) return -ENOSPC;

        time_t seconds = time(NULL);
        inode.num = inode_number;
        inode.mode = mode | S_IFDIR;
        inode.uid = getuid();
        inode.gid = getgid();
        inode.size = 0;
        inode.nlinks = 1;
        inode.atim = inode.mtim = inode.ctim = seconds;

        strcpy(dentry->name, name);
        dentry->num = inode_number;
        memcpy(disk + sb->i_blocks_ptr + inode_number * BLOCK_SIZE, &inode, sizeof(struct wfs_inode));

        //flip_bit(inode_number, sb->i_bitmap_ptr);
        flip_bit_in_bitmap(sb->i_bitmap_ptr, inode_number);

        parent_inode->nlinks++;
        return 0;
      }
    }
  }

  return -ENOSPC;
}

// Remove a file
static int wfs_unlink(const char *path) {
    printf("unlink called\n");
    char tmp_path[50], name[50];
    int inode_num = 0;
    int parent_num = 0;
    strcpy(tmp_path, path);

    // get the inode
    find_inode_number_by_path(tmp_path, &parent_num, &inode_num);
    if (inode_num == -1) return -EEXIST;

    // update the parent
    size_t addr = sb->i_blocks_ptr + parent_num * BLOCK_SIZE;
    struct wfs_inode* parent_inode = (struct wfs_inode*)(disk + addr);

    strcpy(tmp_path, path);
    char *new_tmp_path = strtok(tmp_path, "/");
    while (new_tmp_path != NULL){
        strcpy(name, new_tmp_path);
        new_tmp_path = strtok(NULL, "/");
    }

    for (int i = 0; i <= D_BLOCK; i++) {
        if(parent_inode->blocks[i] == 0) continue;
        int j = find_dentry(parent_inode->blocks[i], name);
        if (j > -1) {
            struct wfs_dentry* dentry = ((struct wfs_dentry*) (disk + parent_inode->blocks[i])) + dentry_loc;
            memset(dentry, 0, sizeof(struct wfs_dentry));
            break;
        }
    }

    parent_inode->nlinks--;

    // update inode
    addr = sb->i_blocks_ptr + inode_num * BLOCK_SIZE;
    struct wfs_inode* inode = (struct wfs_inode*)(disk + addr);
    inode->nlinks--;
    if (inode->nlinks > 0) return 0;

    // remove the file
    for (int i = 0; i <= inode->size / BLOCK_SIZE; i++) {
        int data_block_addr = inode->blocks[i];
        if (i > D_BLOCK) { // beyond direct blocks
            if (inode->blocks[IND_BLOCK] == 0) {
                continue;
            }
            memcpy(&data_block_addr, disk + inode->blocks[IND_BLOCK] + (i - IND_BLOCK) * sizeof(int), sizeof(int));
        }

        if(data_block_addr == 0) continue;
        memset(disk + data_block_addr, 0, BLOCK_SIZE);
        
        int data_block_number = ((data_block_addr - sb->d_blocks_ptr) / BLOCK_SIZE);
        flip_bit_in_bitmap(sb->d_bitmap_ptr, data_block_number);
    }

    memset(inode, 0, BLOCK_SIZE);
    
    flip_bit_in_bitmap(sb->i_bitmap_ptr, inode_num);

    return 0; // Return 0 on success
}

// Remove a directory
static int wfs_rmdir(const char *path) {
  printf("rmdir called\n");

  char tmp_path[50];
  char name[50];
  struct wfs_inode* inode;
  size_t addr;
  strcpy(tmp_path, path);

  // get the inode of the to-be-removed-file
  int inode_num;
  int parent_num;
  find_inode_number_by_path(tmp_path, &parent_num, &inode_num);
  if (inode_num == -1) return -EEXIST;

  strcpy(tmp_path, path);
  char *new_tmp_path = strtok(tmp_path, "/");
  while (new_tmp_path != NULL){
      strcpy(name, new_tmp_path);
      new_tmp_path = strtok(NULL, "/");
  }

  // get inode
  addr = sb->i_blocks_ptr + inode_num * BLOCK_SIZE;
  inode = (struct wfs_inode*) (disk + addr);

  // check if the directory is empty
  for (int i = 0; i <= D_BLOCK; i++) {
    if(inode->blocks[i] == 0) continue;

    // iterate over dentries in data block
    for (int j = 0; j < (int)(BLOCK_SIZE / sizeof(struct wfs_dentry)); j++) {
      struct wfs_dentry* dentry = (struct wfs_dentry*) (disk + inode->blocks[i] + j * sizeof(struct wfs_dentry));
      if (dentry->num > 0) return -ENOTEMPTY;
    }
    memset(disk + inode->blocks[i], 0, BLOCK_SIZE);
    
    int data_block_number = (inode->blocks[i] - sb->d_blocks_ptr) / BLOCK_SIZE;
    flip_bit_in_bitmap(sb->d_bitmap_ptr, data_block_number);
  }

  // load parent inode
  addr = sb->i_blocks_ptr + parent_num * BLOCK_SIZE;
  struct wfs_inode* parent_inode = (struct wfs_inode*) (disk + addr);

  // now update parent inode
  for (int i = 0; i <= D_BLOCK; i++) {
    if(parent_inode->blocks[i] == 0) continue;

    // search for the dentry of the deleted file
    int j = find_dentry(parent_inode->blocks[i], name);
    if (j > -1) {
      struct wfs_dentry* dentry = (struct wfs_dentry*) (disk + parent_inode->blocks[i] + dentry_loc * sizeof(struct wfs_dentry));
      memset(dentry, 0, sizeof(struct wfs_dentry));
      break;
    }
  }
  parent_inode->nlinks--;

  // zero-out inode block, and flip the bit in bitmap
  memset(inode, 0, BLOCK_SIZE);
  
  // flip_bit(inode_numbers[1], sb->i_bitmap_ptr);
  flip_bit_in_bitmap(sb->i_bitmap_ptr, inode_num);

  return 0; // Return 0 on success
}

// Read data from a file
static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  printf("read called\n");
  char tmp_path[50];
  struct wfs_inode* this_inode;
  int inode_num = 0;
  int parent_num = 0;
  strcpy(tmp_path, path);

  // get the inode
  find_inode_number_by_path(tmp_path, &parent_num, &inode_num);
  if (inode_num == -1) return -EEXIST;
  this_inode = (struct wfs_inode*) (disk + sb->i_blocks_ptr + inode_num * BLOCK_SIZE);

  // Read data blocks in file
  int block_num = offset / BLOCK_SIZE, bytes_read = 0;
  size_t effective_file_size = offset >= this_inode->size ? 0 : this_inode->size - offset;

  for (size_t bytes_left = MIN(size, effective_file_size); bytes_left > 0; block_num++) {
    int bytes_to_read = MIN(BLOCK_SIZE, bytes_left);
    char *this_data_block;
    if (block_num >= IND_BLOCK) {
      // Indirect pointer
      off_t *offset_address = (((off_t*) (disk + this_inode->blocks[IND_BLOCK])) + (block_num - IND_BLOCK));
      this_data_block = disk + *offset_address;
    }
    else {
      // Direct pointer
      this_data_block = disk + this_inode->blocks[block_num];
    }
    memcpy(buf + bytes_read, this_data_block, bytes_to_read);
    bytes_read += bytes_to_read;
    bytes_left -= bytes_to_read;
  }

  // Update inode access/change times
  time_t curr_time = time(NULL);
  this_inode->atim = curr_time;
  this_inode->ctim = curr_time;

  return bytes_read;
}

// Write data to an OPEN file
static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("write called on path %s\n", path);

    int parent_num = 0, inode_num = 0;
    char tmp_path[50];
    strcpy(tmp_path, path);
    find_inode_number_by_path(tmp_path, &parent_num, &inode_num);

    if (inode_num == -1) {
        return -ENOENT;  // No such file
    }

    struct wfs_inode *inode = (struct wfs_inode*)(disk + sb->i_blocks_ptr + inode_num * BLOCK_SIZE);
    int bytes_written = 0;
    size_t block_index = offset / BLOCK_SIZE;
    int block_offset = offset % BLOCK_SIZE;

    // Direct blocks
    while (size > 0 && block_index < IND_BLOCK) {
        if (inode->blocks[block_index] == 0) {  // Block not allocated
            int new_block = allocate_block(sb->d_bitmap_ptr, sb->num_data_blocks);
            if (new_block == -1) {
                // No space left
                size_t file_end_offset = offset + bytes_written;
                if (bytes_written > 0) {
                    // Update file size if necessary
                    if (inode->size < file_end_offset) {
                        inode->size = file_end_offset;
                    }
                    return bytes_written;
                }
            }
            inode->blocks[block_index] = sb->d_blocks_ptr + new_block * BLOCK_SIZE;

            flip_bit_in_bitmap(sb->d_bitmap_ptr, new_block);
        }

        off_t block_physical_addr = inode->blocks[block_index];

        size_t space_in_block = BLOCK_SIZE - block_offset;
        size_t bytes_to_write = MIN(size, space_in_block);
        memcpy(disk + block_physical_addr + block_offset, buf + bytes_written, bytes_to_write);

        // Update counters
        bytes_written += bytes_to_write;
        size -= bytes_to_write;
        block_index++;
        block_offset = 0;  // After the first block, we write from the start of the next blocks
    }

    // Indirect block write
    if (size > 0) {
        if (inode->blocks[IND_BLOCK] == 0) {
            // Allocate indirect pointer block if necessary
            int new_block = allocate_block(sb->d_bitmap_ptr, sb->num_data_blocks);
            if (new_block == -1) {
                // No space left
                size_t file_end_offset = offset + bytes_written;
                if (bytes_written > 0) {
                    // Update file size if necessary
                    if (inode->size < file_end_offset) {
                      inode->size = file_end_offset;
                    }
                    return bytes_written;
                }
                return -ENOSPC;
            }
            inode->blocks[IND_BLOCK] = sb->d_blocks_ptr + new_block * BLOCK_SIZE;

            flip_bit_in_bitmap(sb->d_bitmap_ptr, new_block);
        }

        // Iterate through each pointer in the indirect pointer block until write complete or file is full.
        // Note that at the start of the for loop, we don't reset block_index to 0, but decrement it by IND_BLOCK.
        // This handles cases where we have a very large offset.
        for (block_index -= IND_BLOCK; size > 0 && block_index < BLOCK_SIZE / sizeof(off_t); block_index++) {
            off_t *offset_address = (((off_t*) (disk + inode->blocks[IND_BLOCK])) + block_index);
            if (*offset_address == 0) {  // Block not allocated
                int new_block = allocate_block(sb->d_bitmap_ptr, sb->num_data_blocks);
                if (new_block == -1) {
                    // No space left
                    size_t file_end_offset = offset + bytes_written;
                    if (bytes_written > 0) {
                        // Update file size if necessary
                        if (inode->size < file_end_offset) {
                          inode->size = file_end_offset;
                        }
                        return bytes_written;
                    }
                    return -ENOSPC;
                }
                *offset_address = sb->d_blocks_ptr + new_block * BLOCK_SIZE;

                flip_bit_in_bitmap(sb->d_bitmap_ptr, new_block);
            }
            
            off_t block_physical_addr = *offset_address;

            size_t space_in_block = BLOCK_SIZE - block_offset;
            size_t bytes_to_write = MIN(size, space_in_block);
            
            memcpy(disk + block_physical_addr + block_offset, buf + bytes_written, bytes_to_write);


            // Update counters
            bytes_written += bytes_to_write;
            size -= bytes_to_write;
            block_index++;
            block_offset = 0;  // After the first block, we write from the start of the next blocks
        }
    }

    if (size > 0) {
        // If there is still data to write but no blocks available, return the
        // number of bytes written, or an error if nothing is written
        size_t file_end_offset = offset + bytes_written;
        if (bytes_written > 0) {
            // Update file size if necessary
            if (inode->size < file_end_offset) {
              inode->size = file_end_offset;
            }
            return bytes_written;
        }
        return -ENOSPC;
    }

    // Update file size if necessary
    size_t file_end_offset = offset + bytes_written;
    if (inode->size < file_end_offset) {
        inode->size = file_end_offset;
    }

    // Update inode times
    time_t current_time = time(NULL);
    inode->atim = current_time;
    inode->mtim = current_time;
    inode->ctim = current_time;

    // Write the inode back to disk
    // memcpy(disk + sb->i_blocks_ptr + inode_num * BLOCK_SIZE, inode, sizeof(struct wfs_inode)); // TODO: shouldn't be necessary

    return bytes_written;  // Return the number of bytes written
}

// Read directory
static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
  printf("readdir called\n");
  struct wfs_dentry dentry;
  struct wfs_inode inode;
  char tmp_path[30];
  strcpy(tmp_path, path);

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  int inode_num;
  int parent_num;
  find_inode_number_by_path(tmp_path, &parent_num, &inode_num);
  memcpy(&inode, disk + sb->i_blocks_ptr + inode_num * BLOCK_SIZE, sizeof(struct wfs_inode));
  for (int i = 0; i <= D_BLOCK; i++) {
    if(inode.blocks[i] == 0) continue;
    for (int j = 0; j < (int)(BLOCK_SIZE / sizeof(struct wfs_dentry)); j++) {
      memcpy(&dentry, disk + inode.blocks[i] + j * sizeof(struct wfs_dentry), sizeof(struct wfs_dentry));
      if (dentry.num > 0) {
        filler(buf, dentry.name, NULL, 0);
      }
    }
  }

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
    if (argc < 3) {
        printf("Usage: ./wfs disk_path [FUSE options] mount_point\n");
        return 1;
    }

    // Open disk image
    int fd = open(argv[1], O_RDWR, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH);
    if (fd == -1) {
        printf("Failed to open disk image\n");
        return 1;
    }

    // Get the file size
    struct stat statbuf;
    stat(argv[1], &statbuf);

    // Get a pointer to the shared mmap memory
    disk = mmap(NULL, statbuf.st_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (disk == (void*) -1) {
        printf("Failed to mmap memory\n");
        return 1;
    }
    sb = (struct wfs_sb*) disk;

    // Adjust the arguments for fuse_main
    argv[1] = argv[argc - 1];
    argv[argc - 1] = NULL;
    argc--;

    return fuse_main(argc, argv, &ops, NULL);
}
