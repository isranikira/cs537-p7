// this program mounts the filesystem to a mount point which are specified by thhe args

// attaching fuse filesystem to a specific directory
// when within the fused dir and do mkdir it will intercept the cal and use the new function
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

void *mapped_mem = NULL;
size_t start_of_logs;

struct wfs_log_entry *get_entry_from_inode(int curr_inode)
{
    struct wfs_log_entry *entry_match = NULL;
    char *curr_mmap_loc = (char *)mapped_mem + sizeof(struct wfs_sb);
    while (curr_mmap_loc < (char *)mapped_mem + ((struct wfs_sb *)mapped_mem)->head)
    {
        struct wfs_inode inode_comp = ((struct wfs_log_entry *)curr_mmap_loc)->inode;
        if (inode_comp.inode_number == curr_inode && inode_comp.deleted == 0)
        {
            entry_match = (struct wfs_log_entry *)curr_mmap_loc;
            // new_inode_found = 1;
        }
        curr_mmap_loc = curr_mmap_loc + sizeof(struct wfs_inode) + inode_comp.size;
    }

    return entry_match;
}

unsigned long new_inode_number()
{
    unsigned long new_inode = 0;
    char *curr_mmap_loc = (char *)mapped_mem + sizeof(struct wfs_sb);
    while (curr_mmap_loc < (char *)mapped_mem + ((struct wfs_sb *)mapped_mem)->head)
    {
        struct wfs_inode inode_comp = ((struct wfs_log_entry *)curr_mmap_loc)->inode;
        if (inode_comp.inode_number > new_inode && inode_comp.deleted == 0)
        {
            new_inode = inode_comp.inode_number;
        }
        curr_mmap_loc = curr_mmap_loc + sizeof(struct wfs_inode) + inode_comp.size;
    }
    return new_inode + 1;
}

// once you have that add the superblock size to the void pointer
// need to parse the path from '/' to find  the next
// then able to cast the pointer to a log entry and find if the path is equal to path you are looking for
// if it is keep track of it and continue else just contiue
// increament by tge log entry size as well as the sive of its data
//  the next location should either be a log entry or the end of the inputs of the file
// handle either finding the file and going to the most recent use or returning the correct error code
// for finding the next file/dir in the path start at the  beginning as a dir and its file do not need to be sequential in the mmap
struct wfs_inode *get_inode_by_path(const char *path)
{
    // start at the root node
    struct wfs_log_entry *entry_match = get_entry_from_inode(0);
    unsigned int curr_inode = 0;

    char temp_path[MAX_FILE_NAME_LEN];
    strncpy(temp_path, path, MAX_FILE_NAME_LEN);
    temp_path[MAX_FILE_NAME_LEN - 1] = '\0';

    int found = 0;
    char *token = strtok(temp_path, "/");

    // Loop through each token in the path
    while (token != NULL)
    {
        found = 0;
        // how many entries are int eh curr path
        int entries_amt = entry_match->inode.size / sizeof(struct wfs_dentry);
        if (entries_amt == 0)
        {
            return NULL;
        }
        struct wfs_dentry *curr_dentry = (struct wfs_dentry *)entry_match->data;
        for (int i = 0; i < entries_amt; i++)
        {
            if (strcmp(curr_dentry->name, token) == 0)
            {
                found = 1;
                curr_inode = curr_dentry->inode_number;
                break;
            }
            curr_dentry++;
        }
        if (found == 0)
        {
            return NULL;
        }
        token = strtok(NULL, "/");
        entry_match = get_entry_from_inode(curr_inode);
    }
    return &(get_entry_from_inode(curr_inode)->inode);
}

int copyDentries(struct wfs_log_entry *orig, struct wfs_log_entry *new)
{ // returns num of dentries present
    int numDentries = orig->inode.size / sizeof(struct wfs_dentry);
    struct wfs_dentry *currDentry = (struct wfs_dentry *)orig->data;
    struct wfs_dentry *newCurrDentry = (struct wfs_dentry *)new->data;
    for (int i = 0; i < numDentries; i++)
    {
        memcpy((void *)newCurrDentry, (void *)currDentry, sizeof(struct wfs_dentry));
        currDentry++;
        newCurrDentry++;
    }
    return numDentries;
}

int copyDentriesExcept(struct wfs_log_entry *orig, struct wfs_log_entry *new, int except)
{
    int numDentries = orig->inode.size / sizeof(struct wfs_dentry);
    struct wfs_dentry *currDentry = (struct wfs_dentry *)orig->data;
    struct wfs_dentry *newCurrDentry = (struct wfs_dentry *)new->data;
    int foundExcept = 0;
    for (int i = 0; i < numDentries; i++)
    {
        if (currDentry->inode_number != except)
        {
            memcpy((void *)newCurrDentry, (void *)currDentry, sizeof(struct wfs_dentry));
            newCurrDentry++;
            currDentry++;
        }
        else
        {
            foundExcept = 1;
            currDentry++;
        }
    }
    if (foundExcept == 0)
    {
        return -1;
    }
    return numDentries - 1;
}

static int wfs_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));
    struct wfs_inode *curr_inode = get_inode_by_path(path);

    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR;
        stbuf->st_nlink = 2;
    }
    else
    {
        if (curr_inode == NULL)
        {
            return -ENOENT; // No such file or directory
        }
        stbuf->st_mode = curr_inode->mode;
        stbuf->st_nlink = curr_inode->links;
    }
    stbuf->st_uid = curr_inode->uid;
    stbuf->st_gid = curr_inode->gid;
    stbuf->st_mtime = curr_inode->mtime;
    stbuf->st_size = curr_inode->size; // might need to change this later

    return 0;
}

// need to change the inode which it updates, not always going to be root
static int wfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    struct wfs_inode *inode = get_inode_by_path(path);
    if (inode != NULL) {
        return -EEXIST;
    }
    // If not, create a new inode and log entry for the file
    else {
        struct wfs_sb* sb = (struct wfs_sb*)mapped_mem;
        struct wfs_log_entry* newLogEntry = (struct wfs_log_entry*)((char*)mapped_mem + sb->head);
        struct wfs_inode* newInode = &newLogEntry->inode;
        int nextInode = new_inode_number();
        printf("NEXT FREE INODE: %d\n", nextInode);

        newInode->inode_number = nextInode;
        newInode->deleted = 0;
        newInode->mode = mode;
        struct fuse_context * curr_fuse = fuse_get_context();
        newInode->uid = curr_fuse->uid;
        newInode->gid = curr_fuse->gid;
        newInode->flags = 0;
        newInode->size = 0;
        newInode->atime = time(NULL);
        newInode->mtime = time(NULL);
        newInode->ctime = time(NULL);
        newInode->links = 1;

        //printInode(&newLogEntry->inode);
        sb->head += sizeof(struct wfs_inode);
        start_of_logs += sizeof(struct wfs_inode);


        //find the correct directory entry
        char* pathCopy = malloc(sizeof(char) * MAX_PATH_LEN);
        char* dirPath = malloc(sizeof(char*) * MAX_PATH_LEN);
        memset(pathCopy, 0, MAX_FILE_NAME_LEN);
        memset(dirPath, 0, MAX_FILE_NAME_LEN);
        memcpy(pathCopy, path, MAX_FILE_NAME_LEN);
        char* token = strtok(pathCopy, "/");
        char* last = malloc(sizeof(char) * MAX_FILE_NAME_LEN);
        memset(last, 0, MAX_FILE_NAME_LEN);
        memcpy(last, token, MAX_FILE_NAME_LEN);
        int dirIndex = 1;
        dirPath[0] = '/';
        while(token) {
            char* temp = strdup(token);
            memset(last, 0, MAX_FILE_NAME_LEN);
            memcpy(last, token, MAX_FILE_NAME_LEN);
            token = strtok(NULL, "/");
            if(token != NULL) {
                for(int i=0; i<strlen(temp); i++) {
                    dirPath[dirIndex++] = temp[i];
                }
                dirPath[dirIndex++] = '/';
            }
            free(temp);
        }
        if(dirIndex > 1) {
            dirPath[dirIndex - 1] = '\0';
        }
        //printf("RECONSTRUCTED DIR PATH: %s\n", dirPath);
        struct wfs_log_entry* oldDirEntry;
        if(strcmp(dirPath, "/") == 0) {
            //printf("ROOT ENTRY FOR LOG\n");
            oldDirEntry = get_entry_from_inode(0);
        }
        //END ERROR ZONE
        else {
            int oldDirInodeNumber = get_inode_by_path(dirPath)->inode_number;
            oldDirEntry = get_entry_from_inode(oldDirInodeNumber);
        }
        struct wfs_inode* oldDirInode = &oldDirEntry->inode;
        oldDirInode->deleted = 1;
    
        struct wfs_log_entry* newDirEntry = (struct wfs_log_entry*)((char*)mapped_mem + sb->head);
        struct wfs_inode* newDirInode = &newDirEntry->inode;

        //copy over some stuff
        newDirInode->inode_number = oldDirInode->inode_number;
        newDirInode->deleted = 0;
        newDirInode->mode = oldDirInode->mode;
        newDirInode->uid = oldDirInode->uid;
        newDirInode->gid = oldDirInode->gid;
        newDirInode->flags = oldDirInode->flags;
        newDirInode->size = oldDirInode->size + sizeof(struct wfs_dentry);
        newDirInode->atime = time(NULL);
        newDirInode->mtime = time(NULL); //IDK IF THESE 3 SHOULD ALL BE MODIFIED BUT I THINK C FOR SURE
        newDirInode->ctime = time(NULL); 
        newDirInode->links = 1;

        int numCurrentDentries = copyDentries(oldDirEntry, newDirEntry);
        struct wfs_dentry* newDentry = (struct wfs_dentry*)newDirEntry->data;
        newDentry += numCurrentDentries;
        memcpy(newDentry->name, last, MAX_FILE_NAME_LEN);
        free(last);
        free(pathCopy);
        newDentry->inode_number = nextInode;

        sb->head += newDirInode->size + sizeof(struct wfs_log_entry);
        start_of_logs += newDirInode->size + sizeof(struct wfs_log_entry);
    }

    //printf("Filesystem after calling mknod on path %s:\n", path);
    //printFilesystemContent(path);

    // Append the log entry to the disk
    // Update the parent directory's log entry to include this new file
    return 0; // or appropriate error code
}

// Create an empty directory
static int wfs_mkdir(const char *path, mode_t mode)
{
    return wfs_mknod(path, S_IFDIR | (mode & 0777), 0);
}

// Write to an existing file
// The file isn't truncated, implying that it preserves the existing content as if the file is open in r+ mode.
// Note that the offset for this operation may not be 0.
static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{

    struct wfs_inode *curr_inode = get_inode_by_path(path);
    if (curr_inode == NULL)
    {
        return -ENOENT;
    }
    struct wfs_log_entry *curr_log = get_entry_from_inode(curr_inode->inode_number);
    // need to create a new one that is updated
    curr_inode->deleted = 1;
    // add memory for the new log
    struct wfs_log_entry *new_entry = (struct wfs_log_entry *)((char *)mapped_mem + ((struct wfs_sb *)mapped_mem)->head);
    struct wfs_inode *new_inode = &new_entry->inode;

    // give the new data to the node and copy the memory over
    new_inode->inode_number = curr_inode->inode_number;
    new_inode->deleted = 0;
    new_inode->mode = curr_inode->mode;
    new_inode->uid = curr_inode->uid;
    new_inode->gid = curr_inode->gid;
    new_inode->flags = curr_inode->flags;
    new_inode->atime = time(NULL);
    new_inode->ctime = time(NULL);
    new_inode->mtime = time(NULL);
    char *updated_data = new_entry->data;
    updated_data = updated_data + offset;

    memcpy((void *)new_entry->data, (void *)curr_log->data, curr_inode->size);
    memcpy(updated_data, buf, size);

    if (size > curr_inode->size)
    {
        new_inode->size = size;
    }
    else
    {
        new_inode->size = curr_inode->size;
    }

    ((struct wfs_sb *)mapped_mem)->head += sizeof(struct wfs_inode) + new_inode->size;
    start_of_logs += sizeof(struct wfs_inode) + new_inode->size;

    return size;
}

// Read an existing file
// Note that the offset for this operation may not be 0.
static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct wfs_inode *curr_inode = get_inode_by_path(path);
    if (curr_inode == NULL)
    {
        return -ENOENT;
    }
    struct wfs_log_entry *curr_log = get_entry_from_inode(curr_inode->inode_number);
    size = curr_log->inode.size;

    // add into buf
    memcpy(buf + offset, curr_log->data, size);

    return size;
}

//Read a directory
static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct wfs_inode *curr_inode = get_inode_by_path(path);
    if (curr_inode == NULL)
    {
        return -ENOENT;
    }
    struct wfs_log_entry *curr_log = get_entry_from_inode(curr_inode->inode_number);
    //iterate through each name in the dir and put it into the buffer
    struct wfs_dentry *curr_dentry = (struct wfs_dentry *)curr_log->data;
    int num_dirs = curr_inode->size / sizeof(struct wfs_dentry);
    for (int i = 0; i < num_dirs; i++)
    {
        filler(buf, curr_dentry->name, NULL, 0);
        curr_dentry++;
    }
    return 0;
}

static int wfs_unlink(const char *path)
{
    struct wfs_inode *curr_inode = get_inode_by_path(path);
    if (curr_inode == NULL)
    {
        return -ENOENT;
    }
    //when looking for a node if its deleted it will not be reutrned from the get helper methods
    curr_inode->deleted = 1;
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
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s [FUSE options] disk_path mount_point\n", argv[0]);
        return 1;
    }

    char *disk_path = argv[argc - 2];
    argv[argc - 2] = argv[argc - 1];
    argv[argc - 1] = NULL;
    argc--;

    int fd = open(disk_path, O_CREAT | O_RDWR, 0644);
    if (fd == -1)
    {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    // size_t file_size = sizeof(struct wfs_sb)  + sizeof(struct wfs_log_entry);
    size_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size < 0)
    {
        perror("Error finding file size");
        exit(EXIT_FAILURE);
    }
    // reset the offset to the beginning for the mmap
    start_of_logs = lseek(fd, sizeof(struct wfs_sb), SEEK_END);
    lseek(fd, 0, SEEK_SET);
    mapped_mem = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_mem == MAP_FAILED)
    {
        perror("Error mapping file into memory");
        exit(EXIT_FAILURE);
    }

    // char *mount_point = argv[argc-2];
    // note that the call to fuse_main wil have argv stil inclue the disk_path and mount_point
    return fuse_main(argc, argv, &ops, NULL);
}