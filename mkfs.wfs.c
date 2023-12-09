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
#include <errno.h>
#include <stdlib.h>
#include "wfs.h"

uint32_t start_of_logs;
static char *mapped_mem = NULL;

// Filesystem methods

//get the indoe number by searching through all the inodes and returning the highest inode found + 1
unsigned long new_inode_number()
{
    unsigned long new_inode = 0;
    char *curr_mmap_loc = mapped_mem + sizeof(struct wfs_sb);
    while(curr_mmap_loc < mapped_mem + ((struct wfs_sb *)mapped_mem)->head)
    {
        struct wfs_inode inode_comp = ((struct wfs_log_entry*)curr_mmap_loc)->inode;
        if(inode_comp.inode_number > new_inode && inode_comp.deleted == 0)
        {
            new_inode = inode_comp.inode_number;
        }
        curr_mmap_loc = curr_mmap_loc + sizeof(struct wfs_inode) + inode_comp.size;
    }
    return new_inode + 1;
}
//have a global var for the size of the superblock
//have a global void pointer for where you are currently in the mmap
//attach the mmap very similar to how you do it in the mkfs file 

//once you have that add the superblock size to the void pointer
//need to parse the path from '/' to find  the next
//then able to cast the pointer to a log entry and find if the path is equal to path you are looking for
//if it is keep track of it and continue else just contiue
//increament by tge log entry size as well as the sive of its data
// the next location should either be a log entry or the end of the inputs of the file
//handle either finding the file and going to the most recent use or returning the correct error code
//for finding the next file/dir in the path start at the  beginning as a dir and its file do not need to be sequential in the mmap
struct wfs_log_entry *get_log_entry(const char *path)
{
    /* TODO: change the root logic so that it check if path equals exactly '/'
    */
    //can check if this is assigned to anything
    struct wfs_log_entry *entry_match = NULL;
    //curr_mmap_loc = start_of_logs; //TODO: change this assigned a int to a void pointer currently
    unsigned int curr_inode = 0; //start at the root node
    //set up strtok 
    char temp_path[MAX_FILE_NAME_LEN];
    strncpy(temp_path, path, MAX_FILE_NAME_LEN);
    temp_path[MAX_FILE_NAME_LEN-1] = '\0';
    int found = 0;
    //int new_inode_found = 0; //if iterate the first nested while loop and dont find ne
    
    char *token = strtok(temp_path, "/");
    //while the token is not null there is something left in the path
    //always starts with inode 0 comp token to find the next indoe to look for
    do
    {
        char *curr_mmap_loc = mapped_mem + sizeof(struct wfs_sb);
        found = 0;
        //iterate through the entire list to find a location of the most recent entry of what you are looking for
        while(curr_mmap_loc < mapped_mem + ((struct wfs_sb *)mapped_mem)->head)
        {
            struct wfs_inode inode_comp = ((struct wfs_log_entry*)curr_mmap_loc)->inode;
            if(inode_comp.inode_number ==  curr_inode && inode_comp.deleted == 0)
            {
                entry_match = (struct wfs_log_entry *) curr_mmap_loc;
                //new_inode_found = 1;
            }
            curr_mmap_loc = curr_mmap_loc + sizeof(struct wfs_inode) + inode_comp.size;
        }
        //first run and might need to exit if only root node 
        //so that you are not comparing  a null token
        if(token == NULL)
        {
            return entry_match;
        }
        
        //found a place where the log entry wass
        //now need to find next inode to look for or return the log entry if its the last one
        //do this by starting at the location of data and parsing through how many entries there are
        struct wfs_dentry *curr_dentry = (struct wfs_dentry *)entry_match->data;
        int data_offset = sizeof(struct wfs_inode); //the size if the wfs_inode struct plus data structs
        while(data_offset < entry_match->inode.size)
        {
            if(strcmp(curr_dentry->name, token) == 0)
            {
                found = 1;
                curr_inode = curr_dentry->inode_number;
                break; //data struct youre currently on matches the name therfore you have the new inode num to look for
            }
            data_offset = data_offset + sizeof(struct wfs_dentry);
            curr_dentry = curr_dentry + 1;
        } 
        //might need to check that somethings was found before going to next token
        token = strtok(NULL, "/");
    }while (token != NULL);

    if(found == 0)
    {
        return NULL;
    }  
    return entry_match;

}


static int wfs_getattr(const char *path, struct stat *stbuf)
{
    struct wfs_log_entry *curr_log_entry = get_log_entry(path);
    if(curr_log_entry == NULL)
    {
        return -ENOENT; //path was not found
    }
    stbuf->st_mode = curr_log_entry->inode.mode;
    stbuf->st_nlink = curr_log_entry->inode.links;
    stbuf->st_uid = curr_log_entry->inode.uid;
	stbuf->st_gid = curr_log_entry->inode.gid;
	stbuf->st_mtime = curr_log_entry->inode.mtime;
    stbuf->st_size = curr_log_entry->inode.size; //might need to change this later
    return 0;
}

// File methods

// Create an empty file
static int wfs_mknod(const char* path, mode_t mode, dev_t rdev)
{
    //do this one first
    /*  Find the parent log_entry 
     *  - send the path of the parent which would be the path minus the last "token"
     *  - if its null throw an error
     *  generate a new inode number for the file entry
     * initialize log entry with the necessary data and the dentry struct
     * need to make the parents dir deleded then add the add the updated ddata struct and size to next availible addr
     * add the new log entry to the file mem
     * update the head of the superblock
    */
    const char *lastSlash = strrchr(path, '/');
    char *parent_dir = NULL;
    char *child_file_name = NULL;
    if (lastSlash != NULL) {
        //for the parent
        size_t length_parent = lastSlash - path;
        strncpy(parent_dir, path, length_parent);
        parent_dir[length_parent] = '\0';
        //for the child
        size_t length_child = strlen(lastSlash + 1);
        strncpy(child_file_name, lastSlash + 1, length_child);
        child_file_name[length_child] = '\0'; 
    } 
    else 
    {
        // If no '/' is found, the parent directory is the root directory
        strcpy(parent_dir, "/");
        strcpy(child_file_name, path);
    }
    struct wfs_log_entry *parent_log = get_log_entry(parent_dir);
    if(parent_log == NULL)
    {
        return -ENOENT;
    }
    struct wfs_log_entry *child_log = get_log_entry(path);
    if(child_log != NULL)
    {
        return -EEXIST;
    }
    unsigned long new_inode = new_inode_number();

    //assign the new indode info
    child_log->inode.inode_number = new_inode;
    child_log->inode.deleted = 0;
    child_log->inode.mode = mode;
    child_log->inode.uid = getuid();
    child_log->inode.gid = getgid();
    child_log->inode.size = sizeof(struct wfs_log_entry);
    child_log->inode.atime = time(NULL);
    child_log->inode.mtime = time(NULL);
    child_log->inode.ctime = time(NULL);
    child_log->inode.links = 1;

    //allocate new mem to the parent dir then update old deleted and new's data and size
    struct wfs_log_entry *new_parent = (struct wfs_log_entry *)((char *)mapped_mem + ((struct wfs_sb *)mapped_mem)->head);
    memcpy(new_parent, parent_log, parent_log->inode.size);

    //change the head to the head plus the size of the parent log
    ((struct wfs_sb *)mapped_mem)->head = (((struct wfs_sb *)mapped_mem)->head + parent_log->inode.size);
    parent_log->inode.deleted = 1;
    //increment the size by the dentry of the new file
    new_parent->inode.size = new_parent->inode.size + sizeof(struct wfs_dentry);
    //how do i update the data as an array
    //with the new head create a dentry there
    struct wfs_dentry *new_file_dentry = (struct wfs_dentry *)((char *)mapped_mem + ((struct wfs_sb *)mapped_mem)->head);
    new_file_dentry->inode_number = new_inode;
    strncpy(new_file_dentry->name, child_file_name, MAX_FILE_NAME_LEN - 1);
    new_file_dentry->name[MAX_FILE_NAME_LEN - 1] = '\0';

    //change the head to the head plus the size of the new dentry
    ((struct wfs_sb *)mapped_mem)->head = ((struct wfs_sb *)mapped_mem)->head + sizeof(struct wfs_dentry);
    //add the new log file
    struct wfs_log_entry *new_child = (struct wfs_log_entry *)((char *)mapped_mem + ((struct wfs_sb *)mapped_mem)->head);
    memcpy(new_child, child_log, child_log->inode.size);

    //change the head to the head plus the size of the new dentry
    ((struct wfs_sb *)mapped_mem)->head = ((struct wfs_sb *)mapped_mem)->head + sizeof(struct wfs_log_entry);

    return 0;
}

// Create an empty directory
static int wfs_mkdir(const char* path, mode_t mode)
{
    //make this one second
    return wfs_mknod(path, __S_IFDIR | (mode & 0777), 0);
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
    struct wfs_log_entry *curr_entry = get_log_entry(path);
    if(curr_entry == NULL)
    {
        return -ENOENT;
    }

    return 0;
}

// Read a directory
static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    return 0;
}

// Remove an existing file
static int wfs_unlink(const char* path)
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

    char *disk_path = argv[argc-2];
    argv[argc-2] = argv[argc-1];
    argv[argc -1] = NULL;
    argc--;    

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
    start_of_logs = lseek(fd, sizeof(struct wfs_sb), SEEK_END);
    lseek(fd, 0, SEEK_SET);
    mapped_mem = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_mem == MAP_FAILED) {
        perror("Error mapping file into memory");
        exit(EXIT_FAILURE);
    }

    //char *mount_point = argv[argc-2];
    //note that the call to fuse_main wil have argv stil inclue the disk_path and mount_point
    return fuse_main(argc, argv, &ops, NULL );
}
