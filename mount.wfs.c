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
    //check if the path is already there
    struct wfs_inode *check_inode = get_inode_by_path(path);
    if (check_inode != NULL) {
        return -EEXIST;
    }
    
    //make spcace to add a log entry
    struct wfs_log_entry* child_file = (struct wfs_log_entry*)((char*)mapped_mem + ((struct wfs_sb*)mapped_mem)->head);
    struct wfs_inode* new_inode = &child_file->inode;
    int next_inode = new_inode_number();

    new_inode->inode_number = next_inode;
    new_inode->deleted = 0;
    new_inode->mode = mode;
    new_inode->uid = getuid();
    new_inode->gid = getgid();
    new_inode->flags = 0;
    new_inode->size = 0;
    new_inode->atime = time(NULL);
    new_inode->mtime = time(NULL);
    new_inode->ctime = time(NULL);
    new_inode->links = 1;

    //update the location of the head and logs
    ((struct wfs_sb*)mapped_mem)->head += sizeof(struct wfs_inode);
    start_of_logs += sizeof(struct wfs_inode);

    char* path_copy = malloc(sizeof(char) * MAX_PATH_LEN);
    memset(path_copy, 0, MAX_FILE_NAME_LEN);
    char* parent_file_name = malloc(sizeof(char*) * MAX_PATH_LEN);
    memset(parent_file_name, 0, MAX_FILE_NAME_LEN);
    memcpy(path_copy, path, MAX_FILE_NAME_LEN);
    char* token = strtok(path_copy, "/");
    char* child_file_name = malloc(sizeof(char) * MAX_FILE_NAME_LEN);
    memset(child_file_name, 0, MAX_FILE_NAME_LEN);
    memcpy(child_file_name, token, MAX_FILE_NAME_LEN);
    int char_index = 1;
    parent_file_name[0] = '/';
    while(token) {
        char* temp = strdup(token);
        memset(child_file_name, 0, MAX_FILE_NAME_LEN);
        memcpy(child_file_name, token, MAX_FILE_NAME_LEN);
        token = strtok(NULL, "/");
        if(token != NULL) {
            for(int i=0; i<strlen(temp); i++) {
                parent_file_name[char_index++] = temp[i];
            }
            parent_file_name[char_index++] = '/';
        }
        free(temp);
    }
    if(char_index > 1) {
        parent_file_name[char_index - 1] = '\0';
    }
    
    //now should have the corect names availible first look for the directory
    int old_parent_inode_num = get_inode_by_path(parent_file_name)->inode_number;
    struct wfs_log_entry* old_parent_entry = get_entry_from_inode(old_parent_inode_num);

    struct wfs_inode* old_parent_inode = &old_parent_entry->inode;
    old_parent_inode->deleted = 1;

    //make a new entry for parent
    struct wfs_log_entry* new_parent_entry = (struct wfs_log_entry*)((char*)mapped_mem + ((struct wfs_sb*)mapped_mem)->head);
    struct wfs_inode* new_parent_inode = &new_parent_entry->inode;

    //copy over data from old to new entry
    new_parent_inode->inode_number = old_parent_inode->inode_number;
    new_parent_inode->deleted = 0;
    new_parent_inode->mode = old_parent_inode->mode;
    new_parent_inode->uid = old_parent_inode->uid;
    new_parent_inode->gid = old_parent_inode->gid;
    new_parent_inode->flags = old_parent_inode->flags;
    new_parent_inode->size = old_parent_inode->size + sizeof(struct wfs_dentry);
    new_parent_inode->atime = time(NULL);
    new_parent_inode->mtime = time(NULL);
    new_parent_inode->ctime = time(NULL); 
    new_parent_inode->links = 1;

    //copy over all the entries to after the new parent entry
    int num_entries = old_parent_entry->inode.size / sizeof(struct wfs_dentry);
    struct wfs_dentry *old_dentry = (struct wfs_dentry *)old_parent_entry->data;
    struct wfs_dentry *new_dentry = (struct wfs_dentry *)new_parent_entry->data;
    for (int i = 0; i < num_entries; i++)
    {
        memcpy((void *)new_dentry, (void *)old_dentry, sizeof(struct wfs_dentry));
        old_dentry++; //add one because it adds by the size
        new_dentry++;
    }

    struct wfs_dentry* add_dentry = (struct wfs_dentry*)new_parent_entry->data;
    add_dentry += num_entries;
    memcpy(add_dentry->name, child_file_name, MAX_FILE_NAME_LEN);
    add_dentry->inode_number = next_inode;

    //update to pointers and cleanup
    ((struct wfs_sb*)mapped_mem)->head += new_parent_inode->size + sizeof(struct wfs_log_entry);
    start_of_logs += new_parent_inode->size + sizeof(struct wfs_log_entry);
    free(child_file_name);
    free(path_copy);

    return 0; 
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