// this program mounts the filesystem to a mount point which are specified by thhe args

//attaching fuse filesystem to a specific directory
//when within the fused dir and do mkdir it will intercept the cal and use the new function
#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h> 
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "wfs.h"

static const char *logfs_path = "/";
uint32_t start_of_logs;
void *curr_mmap_loc;

// Filesystem methods

//have a global var for the size of the superblock
//have a global void pointer for where you are currently in the mmap
//attach the mmap very similar to how you do it in the mkfs file 
//once you have that add the superblock size to the void pointer
//then able to cast the pointer to a log entry and find if the path is equal to path you are looking for
//if it is keep track of it and continue else just contiue
//increament by tge log entry size as well as the sive of its data
// the next location should either be a log entry or the end of the inputs of the file
//handle either finding the file and going to the most recent use or returning the correct error code
//for finding the next file/dir in the path start at the  beginning as a dir and its file do not need to be sequential in the mmap
struct wfs_log_entry *get_log_entry(const char *path)
{
    //void *
    //TODO
}


static int wfs_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) 
    {
        stbuf->st_mode = __S_IFDIR;
        stbuf->st_nlink = 2;
        return 0;
    }
    stbuf->st_mode = __S_IFREG;
    stbuf->st_nlink = 1;
    stbuf->st_uid = getuid();
	stbuf->st_gid = getgid();
	stbuf->st_mtime = time( NULL );
    stbuf->st_size = sizeof(struct wfs_log_entry); //might need to change this later
    return 0;
}

// File methods

// Create an empty file
static int wfs_mknod(const char *path, struct fuse_file_info *fi)
{

    return 0;
}

// Create an empty directory
static int wfs_mkdir(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    return 0;
}

// Write to an existing file
// The file isn't truncated, implying that it preserves the existing content
// as if the file is open in r+ mode. Note that the offset for this operation may not be 0.
static int wfs_write(const char *path, const char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    return size;
}

// Read an existing file
// Note that the offset for this operation may not be 0.
static int wfs_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{

    return 0;
}

// Read a directory
static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    return 0;
}

// Remove an existing file
static int wfs_unlink(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

// FUSE operations
static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .mknod = wfs_mknod,
    .mkdir = wfs_mkdir,
    .read = wfs_read,
    .write = wfs_write,
    .readdir = wfs_readdir,
    .unlink = wfs_unlink,
};

int main(int argc, char *argv[])
{
    if(argc <3)
    {
        fprintf(stderr, "Usage: %s [FUSE options] disk_path mount_point\n", argv[0]);
        return 1;
    }

    char *disk_path = argv[argc-1];

    int fd = open(disk_path, O_CREAT | O_RDWR, 0644);
    if(fd == -1)
    {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    //size_t file_size = sizeof(struct wfs_sb)  + sizeof(struct wfs_log_entry);
    size_t file_size = lseek(fd, 0, SEEK_END);
    if(file_size < 0)
    {
        perror("Error finding file size");
        exit(EXIT_FAILURE);
    }
    //reset the offset to the beginning for the mmap
    start_of_logs = lseek(fd, sizeof(struct wfs_sb), SEEK_SET);
    lseek(fd, 0, SEEK_SET);
    void *mapped_mem = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_mem == MAP_FAILED) {
        perror("Error mapping file into memory");
        exit(EXIT_FAILURE);
    }

    char *mount_point = argv[argc-2];
    //note that the call to fuse_main wil have argv stil inclue the disk_path and mount_point
    return fuse_main(argc-2, argv, &ops, NULL);
}
