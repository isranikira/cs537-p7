// initializes a file to an empty filesystem, proram recieves a path to
// the disk image file as an argument i.e. mkfs.wfs diskpath

// logfs.c

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "wfs.h"

struct wfs_sb *superblock;
struct wfs_log_entry *root_dir;

void init_superblock()
{

    //assign superblock 
    if(superblock == NULL)
    {
        perror("Error alocating memory for superblock");
        exit(EXIT_FAILURE);
    }

    //initialize the superblock
    superblock->magic = WFS_MAGIC;
    superblock->head = sizeof(struct wfs_sb) + sizeof(struct wfs_log_entry);
}

void init_root_dir()
{
    //const char *name = "/";
    if(root_dir == NULL)
    {
        perror("error allocating memory for root");
        exit(EXIT_FAILURE);
    }
    root_dir->inode.deleted = 0;
    root_dir->inode.mode = __S_IFDIR | 0755;
    root_dir->inode.links = 1;
    root_dir->inode.mtime = time(NULL);
    root_dir->inode.uid = getuid();
	root_dir->inode.gid = getgid();
    //root_dir->inode.flags = //not sure what this is suppose to do
    root_dir->inode.ctime = time(NULL);
    root_dir->inode.atime = time(NULL);
    root_dir->inode.inode_number = 0;
    root_dir->inode.size = 0; //might need to change to dentry
    
    // strncpy(root_dir->data, name, sizeof(MAX_FILE_NAME_LEN));
    // root_dir->data[MAX_FILE_NAME_LEN-1] = '\0'; //make sure that the last char is anull terminator
}

int main(int argc, char *argv[])
{
    //needs thhe call of the program and the disk to attach it to
    if(argc != 2)
    {
        fprintf(stderr, "Usage: %s disk_path\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *disk_path = argv[1];
    //open the file in read write more, create it if it doesn't exist
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
    lseek(fd, 0, SEEK_SET);
    void *mapped_mem = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_mem == MAP_FAILED) {
        perror("Error mapping file into memory");
        exit(EXIT_FAILURE);
    }

    superblock = (struct wfs_sb *)mapped_mem;
    root_dir = (struct wfs_log_entry *)((char *)mapped_mem + sizeof(struct wfs_sb));
    init_superblock();
    init_root_dir();

    if(msync(mapped_mem, file_size, MS_SYNC) == -1)
    {
        perror("Error synchronizing memory with file");
        exit(EXIT_FAILURE);
    }

    // Unmap the memory and close the file
    if (munmap(mapped_mem, file_size) == -1) {
        perror("Error unmapping memory");
        exit(EXIT_FAILURE);
    }

    if (close(fd) == -1) {
        perror("Error closing file");
        exit(EXIT_FAILURE);
    }

    return 0;


}