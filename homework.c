/*
 * file:        homework.c
 * description: skeleton file for CS 5600 file system
 *
 * CS 5600, Computer Systems, Northeastern CCIS
 * Peter Desnoyers, Fall 2020
 */

#define FUSE_USE_VERSION 27
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "fs5600.h"

#define MAX_PATH_LEN 10
#define MAX_NAME_LEN 27
#define MAX_DIR_ENTRIES_PER_BLOCK 128

/* if you don't understand why you can't use these system calls here, 
 * you need to read the assignment description another time
 */
#define stat(a, b) error do not use stat()
#define open(a, b) error do not use open()
#define read(a, b, c) error do not use read()
#define write(a, b, c) error do not use write()

/* disk access. All access is in terms of 4KB blocks; read and
 * write functions return 0 (success) or -EIO.
 */
extern int block_read(void *buf, int lba, int nblks);
extern int block_write(void *buf, int lba, int nblks);

/* bitmap functions
 */
void bit_set(unsigned char *map, int i)
{
    map[i / 8] |= (1 << (i % 8));
}
void bit_clear(unsigned char *map, int i)
{
    map[i / 8] &= ~(1 << (i % 8));
}
int bit_test(unsigned char *map, int i)
{
    return map[i / 8] & (1 << (i % 8));
}

struct fs_super superblock;
struct fs_inode rootInode;
unsigned char bitmap[FS_BLOCK_SIZE] = {0};
struct statvfs statVfs;
/* init - this is called once by the FUSE framework at startup. Ignore
 * the 'conn' argument.
 * recommended actions:
 *   - read superblock
 *   - allocate memory, block allocation bitmap
 */
void *fs_init(struct fuse_conn_info *conn)
{
    /* your code here */
    uint64_t status;
    if ((status = block_read(&superblock, 0, 1)) < 0)
    {
        printf("ERROR: Failed to load superblock\n");
        return (void *)status;
    }
    if ((status = block_read(&bitmap, 1, 1)) < 0)
    {
        printf("ERROR: Failed to load bitmap\n");
        return (void *)status;
    }
    if ((status = block_read(&rootInode, 2, 1)) < 0)
    {
        printf("ERROR: Failed to load rootInode\n");
        return (void *)status;
    }

    statVfs.f_bsize = FS_BLOCK_SIZE;
    statVfs.f_blocks = superblock.disk_size - 2;
    unsigned int blocksFree = 0;
    for (int i = 0; i < superblock.disk_size; i++)
    {
        blocksFree += ((bit_test(bitmap, i) == 0) ? 1 : 0);
    }
    statVfs.f_bfree = blocksFree;
    statVfs.f_bavail = statVfs.f_bfree;
    statVfs.f_namemax = MAX_NAME_LEN;

    printf("INFO: Loaded filesystem with the following proprties:\n");
    printf("INFO: Block Size: %u\n", FS_BLOCK_SIZE);
    printf("INFO: Disk MAGIC: %u\n", superblock.magic);
    printf("INFO: Disk Size: %u\n", superblock.disk_size);
    printf("INFO: Blocks Used: %u\n", superblock.disk_size - blocksFree);
    printf("INFO: Blocks Available: %lu\n", statVfs.f_bfree);
    printf("INFO: Blocks Free: %lu\n", statVfs.f_bfree);

    return NULL;
}

/* Note on path translation errors:
 * In addition to the method-specific errors listed below, almost
 * every method can return one of the following errors if it fails to
 * locate a file or directory corresponding to a specified path.
 *
 * ENOENT - a component of the path doesn't exist.
 * ENOTDIR - an intermediate component of the path (e.g. 'b' in
 *           /a/b/c) is not a directory
 */
int translate(int pathc, char **pathv, int depth)
{
    int status;
    struct fs_inode curInode;
    // MAX 128 entries in a directory assumption
    struct fs_dirent curDir[MAX_DIR_ENTRIES_PER_BLOCK];
    int inodeIndex = 2;
    depth = (depth <= pathc) ? depth : pathc;
    for (int pathToken = 0; pathToken < pathc - depth; pathToken++)
    {
        if ((status = block_read(&curInode, inodeIndex, 1)) < 0)
        {
            return status;
        }
        if (!S_ISDIR(curInode.mode))
        {
            return -ENOTDIR;
        }
        // MAX 128 entries in a directory assumption
        if ((status = block_read(&curDir, curInode.ptrs[0], 1)) < 0)
        {
            return status;
        }
        for (int dirEntry = 0; dirEntry < MAX_DIR_ENTRIES_PER_BLOCK; dirEntry++)
        {
            if (curDir[dirEntry].valid && strcmp(pathv[pathToken], curDir[dirEntry].name) == 0)
            {
                inodeIndex = curDir[dirEntry].inode;
                break;
            }
            else
            {
                inodeIndex = -1;
            }
        }
        if (inodeIndex == -1)
        {
            return -ENOENT;
        }
    }
    return inodeIndex;
}

/* note on splitting the 'path' variable:
 * the value passed in by the FUSE framework is declared as 'const',
 * which means you can't modify it. The standard mechanisms for
 * splitting strings in C (strtok, strsep) modify the string in place,
 * so you have to copy the string and then free the copy when you're
 * done. One way of doing this:
 *
 *    char *_path = strdup(path);
 *    int inum = translate(_path);
 *    free(_path);
 */
int parse(char *path, char **argv)
{
    int i;
    for (i = 0; i < MAX_PATH_LEN; i++)
    {
        if ((argv[i] = strtok(path, "/")) == NULL)
        {
            break;
        }
        if (strlen(argv[i]) > MAX_NAME_LEN)
        {
            argv[i][MAX_NAME_LEN] = 0; // truncate to 27 characters
        }
        path = NULL;
    }
    return i;
}

void inode_to_stat(struct fs_inode *inode, struct stat *sb)
{
    sb->st_mode = inode->mode;
    sb->st_uid = inode->uid;
    sb->st_gid = inode->gid;
    sb->st_size = inode->size;
    sb->st_ctime = inode->ctime; // weird comment below
    sb->st_mtime = inode->mtime;
    sb->st_atime = inode->mtime;
    sb->st_nlink = 1;
}

/**
 * @brief Returns inode containing last entry in path. Last entry can be path or
 * file.
 * 
 * @param path 
 * @param inode 
 * @return int 
 */
int path_to_inode(const char *path, struct fs_inode **inode, int depth)
{
    char *_path = strdup(path);
    char *argv[MAX_PATH_LEN];
    int pathc = parse(_path, argv);
    int inum;
    if ((inum = translate(pathc, argv, depth)) < 0)
    {
        free(_path);
        return inum;
    }
    *inode = malloc(sizeof(struct fs_inode));
    int status;
    if ((status = block_read(*inode, inum, 1)) < 0)
    {
        free(_path);
        free(inode);
        return status;
    }
    free(_path);
    return inum;
}

/* getattr - get file or directory attributes. For a description of
 *  the fields in 'struct stat', see 'man lstat'.
 *
 * Note - for several fields in 'struct stat' there is no corresponding
 *  information in our file system:
 *    st_nlink - always set it to 1
 *    st_atime, st_ctime - set to same value as st_mtime
 *
 * success - return 0
 * errors - path translation, ENOENT
 * hint - factor out inode-to-struct stat conversion - you'll use it
 *        again in readdir
 */
int fs_getattr(const char *path, struct stat *sb)
{
    /* your code here */
    struct fs_inode *inode;
    int status;
    if ((status = path_to_inode(path, &inode, 0)) < 0)
    {
        return status;
    }
    inode_to_stat(inode, sb);
    free(inode);
    return 0;
}

/* readdir - get directory contents.
 *
 * call the 'filler' function once for each valid entry in the 
 * directory, as follows:
 *     filler(buf, <name>, <statbuf>, 0)
 * where <statbuf> is a pointer to a struct stat
 * success - return 0
 * errors - path resolution, ENOTDIR, ENOENT
 * 
 * hint - check the testing instructions if you don't understand how
 *        to call the filler function
 */
int fs_readdir(const char *path, void *ptr, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi)
{
    /* your code here */
    struct fs_inode *inode;
    struct stat fileStat;
    int status;
    if ((status = path_to_inode(path, &inode, 0)) < 0)
    {
        return status;
    }
    if (!(S_ISDIR(inode->mode)))
    {
        free(inode);
        return -ENOTDIR;
    }
    struct fs_dirent curDir[MAX_DIR_ENTRIES_PER_BLOCK];
    if ((status = block_read(curDir, inode->ptrs[0], 1) < 0))
    {
        free(inode);
        return status;
    }
    for (int dirEntry = 0; dirEntry < MAX_DIR_ENTRIES_PER_BLOCK; dirEntry++)
    {
        if (curDir[dirEntry].valid)
        {
            if ((status = block_read(inode, curDir[dirEntry].inode, 1)) < 0)
            {
                free(inode);
                return status;
            }
            inode_to_stat(inode, &fileStat);
            filler(ptr, curDir[dirEntry].name, &fileStat, offset);
        }
    }
    free(inode);
    return 0;
}

int validate_directory_and_entry(const char *path, mode_t mode, struct fuse_file_info *fi, struct fs_inode *dirInode, struct fs_dirent **dirBlock)
{
    if (!S_ISDIR(dirInode->mode))
    {
        return -ENOTDIR;
    }
    int dirBlockInum = dirInode->ptrs[0];
    *dirBlock = malloc(sizeof(struct fs_dirent) * MAX_DIR_ENTRIES_PER_BLOCK);
    int status;
    if ((status = block_read(*dirBlock, dirBlockInum, 1)) < 0)
    {
        free(*dirBlock);
        return status;
    }

    char *_path = strdup(path);
    char *argv[MAX_PATH_LEN];
    int pathc = parse(_path, argv);
    char *filename = argv[pathc - 1];

    for (int entryIdx = 0; entryIdx < MAX_DIR_ENTRIES_PER_BLOCK; entryIdx++)
    {
        if ((*dirBlock)[entryIdx].valid)
        {
            if (strcmp((*dirBlock)[entryIdx].name, filename) == 0)
            {
                free(_path);
                free(*dirBlock);
                return -EEXIST;
            }
        }
    }
    free(_path);
    return 0;
}

int find_free_dir_entry(struct fs_dirent *dirBlock, int startIdx, int *firstAvailableEntry)
{
    *firstAvailableEntry = -1;
    for (int entryIdx = startIdx; entryIdx < MAX_DIR_ENTRIES_PER_BLOCK; entryIdx++)
    {
        if (!dirBlock[entryIdx].valid)
        {
            *firstAvailableEntry = entryIdx;
            break;
        }
    }

    return (*firstAvailableEntry != -1) ? 0 : -ENOSPC;
}

int find_first_nfree_blocks(int startIdx, int n, int **allocatableBlkInum)
{
    int requestedBlockCount = n;
    *allocatableBlkInum = malloc(sizeof(int) * superblock.disk_size); // dynamic structure would be better here

    int allocatableBlkIdx = 0;
    for (int blkIdx = startIdx; blkIdx < superblock.disk_size && requestedBlockCount > 0; blkIdx++)
    {
        if (bit_test(bitmap, blkIdx) == 0)
        {
            (*allocatableBlkInum)[allocatableBlkIdx++] = blkIdx;
            requestedBlockCount--;
        }
    }

    if (requestedBlockCount > 0)
    {
        free(*allocatableBlkInum);
        return -ENOSPC;
    }

    return 0;
}

struct fs_inode inode_from_mode(mode_t mode)
{
    struct fs_inode inode;
    struct fuse_context *ctx = fuse_get_context();

    inode.uid = ctx->uid;
    inode.gid = ctx->gid;
    inode.mode = mode;
    inode.ctime = time(NULL);
    inode.mtime = time(NULL);
    inode.size = (S_ISDIR(mode) ? 4096 : 0);
    memset(inode.ptrs, 0, sizeof(inode.ptrs));

    return inode;
}

char *get_entry_name_from_path(const char *path)
{
    char *_dpath = strdup(path);
    char *dargv[MAX_PATH_LEN];
    int dpathc = parse(_dpath, dargv);
    if (strlen(dargv[dpathc - 1]) > MAX_NAME_LEN)
    {
        return NULL;
    }
    char *filename = calloc(1, sizeof(char) * MAX_NAME_LEN + 1);
    strncpy(filename, dargv[dpathc - 1], MAX_NAME_LEN);
    free(_dpath);
    return filename;
}

int modify_bitmap_and_writeback_to_disk(int *allocatedBlockInums, int n, int setFlag)
{
    for (int allocationIdx = 0; allocationIdx < n; allocationIdx++)
    {
        int allocatedInum = allocatedBlockInums[allocationIdx];
        if (setFlag)
        {
            bit_set(bitmap, allocatedInum);
        }
        else
        {
            bit_clear(bitmap, allocatedInum);
        }
    }
    int status;
    if ((status = block_write(bitmap, 1, 1)) < 0)
    {
        return status;
    }
    return 0;
}

int create_directory_entry(const char *path, mode_t mode, struct fuse_file_info *fi, int dirflag)
{
    struct fs_inode *dirInode;
    int status;
    if ((status = path_to_inode(path, &dirInode, 1)) < 0)
    {
        return status;
    }
    struct fs_dirent *dirBlock;
    if ((status = validate_directory_and_entry(path, mode, fi, dirInode, &dirBlock)) < 0)
    {
        free(dirInode);
        return status;
    }
    int dirBlockInum = dirInode->ptrs[0];
    free(dirInode);
    int freeDirEntry;
    if ((status = find_free_dir_entry(dirBlock, 0, &freeDirEntry)) < 0)
    {
        free(dirBlock);
        return status;
    }

    // 1 block for inode + (optional) 1 block for directory entries
    int allocationBlockCount = (dirflag) ? 2 : 1;
    int *allocatableBlocksInums;
    if ((status = find_first_nfree_blocks(0, allocationBlockCount, &allocatableBlocksInums)) < 0)
    {
        free(dirBlock);
        return status;
    }
    int newEntryInodeInum = allocatableBlocksInums[0];
    int dirEntryBlockInum = allocatableBlocksInums[1];

    // Create file inode
    mode = (dirflag) ? mode | __S_IFDIR : mode;
    struct fs_inode newEntryInode = inode_from_mode(mode);

    // set inode ptr to allocated direntry block (optional)
    newEntryInode.ptrs[0] = (dirflag) ? dirEntryBlockInum : 0;

    // writeback file inode
    if ((status = block_write(&newEntryInode, newEntryInodeInum, 1)) < 0)
    {
        free(allocatableBlocksInums);
        free(dirBlock);
        return status;
    }

    // modify entry in dirblock
    char *filename = get_entry_name_from_path(path);

    dirBlock[freeDirEntry].valid = 1;
    strncpy(dirBlock[freeDirEntry].name, filename, MAX_NAME_LEN);
    free(filename);
    dirBlock[freeDirEntry].inode = newEntryInodeInum;

    // writeback updated dir block
    if ((status = block_write(dirBlock, dirBlockInum, 1)) < 0)
    {
        free(dirBlock);
        free(allocatableBlocksInums);
        return status;
    }
    free(dirBlock);

    // zero out dir entries
    if (dirflag)
    {
        char zeros[FS_BLOCK_SIZE] = {0};
        if ((status = block_write(zeros, dirEntryBlockInum, 1)) < 0)
        {
            free(allocatableBlocksInums);
            return status;
        }
    }

    // modify bitmap and writeback bitmap
    statVfs.f_bfree = (dirflag) ? statVfs.f_bfree - 2 : statVfs.f_bfree - 1;
    statVfs.f_bavail = (dirflag) ? statVfs.f_bavail - 2 : statVfs.f_bavail - 1;
    if ((status = modify_bitmap_and_writeback_to_disk(allocatableBlocksInums, allocationBlockCount, 1)) < 0)
    {
        free(allocatableBlocksInums);
        return status;
    }

    free(allocatableBlocksInums);
    return 0;
}

/* create - create a new file with specified permissions
 *
 * success - return 0
 * errors - path resolution, EEXIST
 *          in particular, for create("/a/b/c") to succeed,
 *          "/a/b" must exist, and "/a/b/c" must not.
 *
 * Note that 'mode' will already have the S_IFREG bit set, so you can
 * just use it directly. Ignore the third parameter.
 *
 * If a file or directory of this name already exists, return -EEXIST.
 * If there are already 128 entries in the directory (i.e. it's filled an
 * entire block), you are free to return -ENOSPC instead of expanding it.
 */
int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    return create_directory_entry(path, mode, fi, 0);
}

/* mkdir - create a directory with the given mode.
 *
 * WARNING: unlike fs_create, @mode only has the permission bits. You
 * have to OR it with S_IFDIR before setting the inode 'mode' field.
 *
 * success - return 0
 * Errors - path resolution, EEXIST
 * Conditions for EEXIST are the same as for create. 
 */
int fs_mkdir(const char *path, mode_t mode)
{
    return create_directory_entry(path, mode, NULL, 1);
}

int find_index_in_dir(char *filename, struct fs_dirent *dirBlock, int *dirIdx)
{
    for (int entryIdx = 0; entryIdx < MAX_DIR_ENTRIES_PER_BLOCK; entryIdx++)
    {
        if (dirBlock[entryIdx].valid)
        {
            if (strcmp(dirBlock[entryIdx].name, filename) == 0)
            {
                *dirIdx = entryIdx;
                return 0;
            }
        }
    }
    return -ENOENT;
}

int unlink_directory_entry(const char *path)
{
    // remove file entry in parent directory
    struct fs_inode *dirInode;
    int status;
    if ((status = path_to_inode(path, &dirInode, 1)) < 0)
    {
        return status;
    }
    int dirBlockInum = dirInode->ptrs[0];
    struct fs_dirent dirBlock[MAX_DIR_ENTRIES_PER_BLOCK];
    if ((status = block_read(dirBlock, dirBlockInum, 1)) < 0)
    {
        free(dirInode);
        return status;
    }
    free(dirInode);

    char *entryname = get_entry_name_from_path(path);
    int fileIdx;
    if ((status = find_index_in_dir(entryname, dirBlock, &fileIdx)) < 0)
    {
        return status;
    }
    free(entryname);

    // invalidate entry
    dirBlock[fileIdx].valid = 0;

    // writeback updated dirBlock
    if ((status = block_write(dirBlock, dirBlockInum, 1)) < 0)
    {
        return status;
    }

    return 0;
}

/* unlink - delete a file
 *  success - return 0
 *  errors - path resolution, ENOENT, EISDIR
 */
int fs_unlink(const char *path)
{
    /* your code here */
    // Collect allocated file block inums
    struct fs_inode *fileInode;
    int status;
    if ((status = path_to_inode(path, &fileInode, 0)) < 0)
    {
        return status;
    }
    int fileInodeInum = status;
    int fileBlocksAllocated = (fileInode->size / FS_BLOCK_SIZE) + ((fileInode->size % FS_BLOCK_SIZE > 0) ? 1 : 0);

    // possible file allocations + file inode
    int *allocatedBlockInums = calloc(fileBlocksAllocated + 1, sizeof(int));

    //file block allocation assumed sequential from 0 with no holes
    for (int blkIdx = 0; blkIdx < fileBlocksAllocated; blkIdx++)
    {
        allocatedBlockInums[blkIdx] = fileInode->ptrs[blkIdx];
    }
    allocatedBlockInums[fileBlocksAllocated] = fileInodeInum;
    free(fileInode);

    if ((status = unlink_directory_entry(path)) < 0)
    {
        free(allocatedBlockInums);
        return status;
    }

    // writeback deletions in bitmap
    if ((status = modify_bitmap_and_writeback_to_disk(allocatedBlockInums, fileBlocksAllocated + 1, 0)) < 0)
    {
        free(allocatedBlockInums);
        return status;
    }

    statVfs.f_bfree = statVfs.f_bfree + fileBlocksAllocated + 1;
    statVfs.f_bavail = statVfs.f_bavail + fileBlocksAllocated + 1;

    free(allocatedBlockInums);
    return 0;
}

int check_dir_empty(const char *path, int *dirInodeInum, int *dirBlockInum)
{
    struct fs_inode *dirInode;
    int status;
    if ((status = path_to_inode(path, &dirInode, 0)) < 0)
    {
        return status;
    }
    *dirInodeInum = status; // path_to_inode returns inum on success
    if (!S_ISDIR(dirInode->mode))
    {
        free(dirInode);
        return -ENOTDIR;
    }
    *dirBlockInum = dirInode->ptrs[0];
    free(dirInode);
    struct fs_dirent dirBlock[MAX_DIR_ENTRIES_PER_BLOCK];
    if ((status = block_read(dirBlock, *dirBlockInum, 1)) < 0)
    {
        return status;
    }
    int freeDirEntry;
    if ((status = find_free_dir_entry(dirBlock, 0, &freeDirEntry)) < 0)
    {
        return status;
    }
    return (freeDirEntry == 0) ? 0 : -ENOTEMPTY;
}

/* rmdir - remove a directory
 *  success - return 0
 *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
 */
int fs_rmdir(const char *path)
{
    /* your code here */
    int dirInodeInum, dirBlockInum;
    int status;
    if ((status = check_dir_empty(path, &dirInodeInum, &dirBlockInum)) < 0)
    {
        return status;
    }
    if ((status = unlink_directory_entry(path)) < 0)
    {
        return status;
    }

    int allocatedBlocks[2] = {dirInodeInum, dirBlockInum};
    if ((status = modify_bitmap_and_writeback_to_disk(allocatedBlocks, 2, 0)))
    {
        return status;
    }
    statVfs.f_bfree = statVfs.f_bfree + 2;
    statVfs.f_bavail = statVfs.f_bavail + 2;
    return 0;
}

int file_dir(struct fs_dirent **dir, struct fs_dirent **resolvedEntry, const char *file_path)
{
    int status;
    struct fs_inode *fileDirInode;
    if ((status = path_to_inode(file_path, &fileDirInode, 1)) < 0)
    {
        return status;
    }
    if (!S_ISDIR(fileDirInode->mode))
    {
        free(fileDirInode);
        return -ENOTDIR;
    }
    *dir = malloc(sizeof(struct fs_dirent) * MAX_DIR_ENTRIES_PER_BLOCK);
    int fileDirBlockLBA = fileDirInode->ptrs[0];
    if ((status = block_read((*dir), fileDirBlockLBA, 1)) < 0)
    {
        free(fileDirInode);
        free(*dir);
        *dir = NULL;
        return status;
    }

    char *_path = strdup(file_path);
    char *argv[MAX_PATH_LEN];
    int pathc = parse(_path, argv);
    char *filename = argv[pathc - 1];
    *resolvedEntry = NULL;
    int dirEntry;
    for (dirEntry = 0; dirEntry < MAX_DIR_ENTRIES_PER_BLOCK; dirEntry++)
    {
        if ((*dir)[dirEntry].valid)
        {
            if (strcmp((*dir)[dirEntry].name, filename) == 0)
            {
                *resolvedEntry = &((*dir)[dirEntry]);
                break;
            }
        }
    }
    free(_path);
    free(fileDirInode);
    if (resolvedEntry == NULL)
    {
        free(*dir);
        *dir = NULL;
        return -ENOENT;
    }
    else
    {
        return fileDirBlockLBA;
    }
}

/* rename - rename a file or directory
 * success - return 0
 * Errors - path resolution, ENOENT, EINVAL, EEXIST
 *
 * ENOENT - source does not exist
 * EEXIST - destination already exists
 * EINVAL - source and destination are not in the same directory
 *
 * Note that this is a simplified version of the UNIX rename
 * functionality - see 'man 2 rename' for full semantics. In
 * particular, the full version can move across directories, replace a
 * destination file, and replace an empty directory with a full one.
 */
int fs_rename(const char *src_path, const char *dst_path)
{
    /* your code here */
    struct fs_inode *sinode;
    int sinum;
    if ((sinum = path_to_inode(src_path, &sinode, 0)) < 0)
    {
        return sinum;
    }
    struct fs_inode *dinode;
    int dinum;
    if ((dinum = path_to_inode(dst_path, &dinode, 0)) >= 0)
    {
        free(dinode);
        free(sinode);
        return -EEXIST;
    }

    char *_spath = strdup(src_path);
    char *sargv[MAX_PATH_LEN];
    int spathc = parse(_spath, sargv);

    char *_dpath = strdup(dst_path);
    char *dargv[MAX_PATH_LEN];
    int dpathc = parse(_dpath, dargv);

    if (dpathc != spathc)
    {
        free(sinode);
        free(_spath);
        free(_dpath);
        return -EINVAL;
    }

    for (int pathToken = 0; pathToken < spathc - 1; pathToken++)
    {
        if (strcmp(sargv[pathToken], dargv[pathToken]) != 0)
        {
            free(sinode);
            free(_spath);
            free(_dpath);
            return -EINVAL;
        }
    }

    struct fs_dirent *sfileDirEntry;
    struct fs_dirent *sfileDirBlock;
    int sfileDirLBA;

    if ((sfileDirLBA = file_dir(&sfileDirBlock, &sfileDirEntry, src_path)) < 0)
    {
        free(sinode);
        free(_spath);
        free(_dpath);
        return sfileDirLBA;
    }

    memset(sfileDirEntry->name, 0, sizeof(sfileDirEntry->name));
    strncpy(sfileDirEntry->name, dargv[dpathc - 1], MAX_NAME_LEN);

    int status;
    if ((status = block_write(sfileDirBlock, sfileDirLBA, 1)) < 0)
    {
        return status;
    }

    free(_spath);
    free(_dpath);
    free(sfileDirBlock);
    free(sinode);
    return 0;
}

/* chmod - change file permissions
 *
 * success - return 0
 * Errors - path resolution, ENOENT.
 */
int fs_chmod(const char *path, mode_t mode)
{
    /* your code here */
    struct fs_inode *finode;
    int inum;
    if ((inum = path_to_inode(path, &finode, 0)) < 0)
    {
        return inum;
    }
    uint32_t permissionsMask = 0b111111111;
    finode->mode = (finode->mode & ~permissionsMask) | (mode & permissionsMask);
    int status;
    if ((status = block_write(finode, inum, 1)) < 0)
    {
        return status;
    }
    free(finode);
    return 0;
}

/* utime - change access and modification times
 *         (for definition of 'struct utimebuf', see 'man utime')
 *
 * success - return 0
 * Errors - path resolution, ENOENT.
 */
int fs_utime(const char *path, struct utimbuf *ut)
{
    /* your code here */
    struct fs_inode *finode;
    int status;
    if ((status = path_to_inode(path, &finode, 0)) < 0)
    {
        return status;
    }
    if (!S_ISREG(finode->mode))
    {
        free(finode);
        return -EISDIR;
    }
    int finodeInum = status;

    finode->mtime = ut->modtime;

    if((status = block_write(finode, finodeInum, 1)) < 0)
    {
        free(finode);
        return status;
    }

    return 0;
}

/* truncate - truncate file to exactly 'len' bytes
 * success - return 0
 * Errors - path resolution, ENOENT, EISDIR, EINVAL
 *    return EINVAL if len > 0.
 */
int fs_truncate(const char *path, off_t len)
{
    if(len < 0)
    {
        return -EINVAL;
    }

    struct fs_inode *finode;
    int status;
    if ((status = path_to_inode(path, &finode, 0)) < 0)
    {
        return status;
    }
    if (!S_ISREG(finode->mode))
    {
        free(finode);
        return -EISDIR;
    }
    int finodeInum = status;

    int fileSizeInBlocks = (finode->size / FS_BLOCK_SIZE) + ((finode->size % FS_BLOCK_SIZE > 0) ? 1 : 0);
    int targetFilesize = (len / FS_BLOCK_SIZE) + ((len % FS_BLOCK_SIZE > 0) ? 1 : 0);

    if(targetFilesize > fileSizeInBlocks)
    {
        free(finode);
        return -EINVAL;
    }

    int blockRemovalCount = fileSizeInBlocks - targetFilesize;
    int* allocatedBlockInodes = calloc(blockRemovalCount, sizeof(int));

    // unlink blocks after targetSize and log inodes for bitmap removal
    int allocationIdx = 0;
    for(int blkIdx = targetFilesize; blkIdx < fileSizeInBlocks; blkIdx++)
    {
        allocatedBlockInodes[allocationIdx++] = finode->ptrs[blkIdx];
        finode->ptrs[blkIdx] = -1;
    }

    finode->size = len;

    if((status = block_write(finode, finodeInum, 1)) < 0)
    {
        free(finode);
        free(allocatedBlockInodes);
        return status;
    }

    free(finode);
    if((status = modify_bitmap_and_writeback_to_disk(allocatedBlockInodes, blockRemovalCount, 0)) < 0)
    {
        free(allocatedBlockInodes);
        return status;
    }
 
    statVfs.f_bfree = statVfs.f_bfree + blockRemovalCount; 
    statVfs.f_bavail = statVfs.f_bavail + blockRemovalCount; 

    free(allocatedBlockInodes);
    /* your code here */
    return 0;
}

/* read - read data from an open file.
 * success: should return exactly the number of bytes requested, except:
 *   - if offset >= file len, return 0
 *   - if offset+len > file len, return #bytes from offset to end
 *   - on error, return <0
 * Errors - path resolution, ENOENT, EISDIR
 */
int fs_read(const char *path, char *buf, size_t len, off_t offset,
            struct fuse_file_info *fi)
{
    /* your code here */
    struct fs_inode *finode;
    int status;
    if ((status = path_to_inode(path, &finode, 0)) < 0)
    {
        return status;
    }
    if (!S_ISREG(finode->mode))
    {
        free(finode);
        return -EISDIR;
    }
    int fileLen = finode->size;
    if (offset >= fileLen)
    {
        free(finode);
        return 0;
    }

    int readStartBlock = offset / FS_BLOCK_SIZE;
    int readStartOffset = offset % FS_BLOCK_SIZE;
    int readEndBlock = (offset + len - 1) / FS_BLOCK_SIZE;
    int fileSizeInBlocks = (finode->size / FS_BLOCK_SIZE) + ((finode->size % FS_BLOCK_SIZE > 0) ? 1 : 0);

    if (offset + len > fileLen)
    {
        readEndBlock = fileSizeInBlocks;
        len = fileLen - offset;
    }

    int readBlockCount = readEndBlock - readStartBlock + 1;
    char *blkBuf = calloc(1, sizeof(char) * FS_BLOCK_SIZE * readBlockCount);
    if (blkBuf == NULL)
    {
        free(finode);
        return -ENOMEM;
    }

    int blkIdx = 0;
    for (int pIdx = readStartBlock; pIdx <= readEndBlock; pIdx++)
    {
        if ((status = block_read(blkBuf + (blkIdx++ * FS_BLOCK_SIZE), finode->ptrs[pIdx], 1)) < 0)
        {
            free(finode);
            free(blkBuf);
            return status;
        }
    }

    memcpy(buf, blkBuf + readStartOffset, len);

    free(blkBuf);
    free(finode);
    return len;
}

/* write - write data to a file
 * success - return number of bytes written. (this will be the same as
 *           the number requested, or else it's an error)
 * Errors - path resolution, ENOENT, EISDIR
 *  return EINVAL if 'offset' is greater than current file length.
 *  (POSIX semantics support the creation of files with "holes" in them, 
 *   but we don't)
 */
int fs_write(const char *path, const char *buf, size_t len,
             off_t offset, struct fuse_file_info *fi)
{
    /* your code here */
    struct fs_inode *finode;
    int status;
    if ((status = path_to_inode(path, &finode, 0)) < 0)
    {
        return status;
    }
    int finodeInum = status;
    if (!S_ISREG(finode->mode))
    {
        free(finode);
        return -EISDIR;
    }
    int fileLen = finode->size;
    if (offset > fileLen)
    {
        free(finode);
        return -EINVAL;
    }

    int writeStartBlock = offset / FS_BLOCK_SIZE;
    int writeStartOffset = offset % FS_BLOCK_SIZE;
    int writeEndBlock = (offset + len - 1) / FS_BLOCK_SIZE;
    int fileSizeInBlocks = (finode->size / FS_BLOCK_SIZE) + ((finode->size % FS_BLOCK_SIZE > 0) ? 1 : 0);

    int writeBlockCount = writeEndBlock - writeStartBlock + 1;

    // allocate additional blocks if necessary
    int blksNeeded = writeEndBlock - fileSizeInBlocks + 1;
    if(blksNeeded > 0)
    {
        // update bitmap first to reserve and prevent unintended access to data
        int* allocatedBlockNums;
        if((status = find_first_nfree_blocks(0, blksNeeded, &allocatedBlockNums)) < 0)
        {
            free(finode);
            return status;
        }
        if((status = modify_bitmap_and_writeback_to_disk(allocatedBlockNums, blksNeeded, 1)))
        {
            free(finode);
            return status;            
        }
        int allocationIdx = 0;
        for(int blkIdx = fileSizeInBlocks; blkIdx < fileSizeInBlocks + blksNeeded; blkIdx++)
        {
            finode->ptrs[blkIdx] = allocatedBlockNums[allocationIdx++];
        }
        free(allocatedBlockNums);
    }

    // update finode with size and new inums in ptrs
    if (offset + len > fileLen)
    {
        finode->size = offset + len;
    }

    finode->mtime = time(NULL);
    if ((status = block_write(finode, finodeInum, 1)) < 0)
    {
        free(finode);
        return status;
    }

    char *blkBuf;
    if ((blkBuf = calloc(FS_BLOCK_SIZE * writeBlockCount, sizeof(char))) == NULL)
    {
        return -ENOMEM;
    }

    if(fileSizeInBlocks > 0)
    {
        char startBlk[FS_BLOCK_SIZE];
        if ((status = block_read(startBlk, finode->ptrs[writeStartBlock], 1)) < 0)
        {
            free(blkBuf);
            free(finode);
            return status;
        }
        memcpy(blkBuf, startBlk, FS_BLOCK_SIZE);
    }

    if(fileSizeInBlocks > 1)
    {
        char endBlk[FS_BLOCK_SIZE];
        if ((status = block_read(endBlk, finode->ptrs[writeEndBlock], 1)) < 0)
        {
            free(blkBuf);
            free(finode);
            return status;
        }
        memcpy(blkBuf + (FS_BLOCK_SIZE * (writeBlockCount - 1)), endBlk, FS_BLOCK_SIZE);
    }

    memcpy(blkBuf + writeStartOffset, buf, len);

    int blkIdx = 0;
    for (int pIdx = writeStartBlock; pIdx <= writeEndBlock; pIdx++)
    {
        if ((status = block_write(blkBuf + (blkIdx++ * FS_BLOCK_SIZE), finode->ptrs[pIdx], 1)) < 0)
        {
            free(blkBuf);
            free(finode);
            return status;
        }
    }

    statVfs.f_bavail = statVfs.f_bavail -  blksNeeded;
    statVfs.f_bfree = statVfs.f_bfree -  blksNeeded;

    free(blkBuf);
    free(finode);
    return len;
}

/* statfs - get file system statistics
 * see 'man 2 statfs' for description of 'struct statvfs'.
 * Errors - none. Needs to work.
 */
int fs_statfs(const char *path, struct statvfs *st)
{
    /* needs to return the following fields (set others to zero):
     *   f_bsize = BLOCK_SIZE
     *   f_blocks = total image - (superblock + block map)
     *   f_bfree = f_blocks - blocks used
     *   f_bavail = f_bfree
     *   f_namemax = <whatever your max namelength is>
     *
     * it's OK to calculate this dynamically on the rare occasions
     * when this function is called.
     */
    memcpy(st, &statVfs, sizeof(struct statvfs));
    /* your code here */
    return 0;
}

/* operations vector. Please don't rename it, or else you'll break things
 */
struct fuse_operations fs_ops = {
    .init = fs_init, /* read-mostly operations */
    .getattr = fs_getattr,
    .readdir = fs_readdir,
    .rename = fs_rename,
    .chmod = fs_chmod,
    .read = fs_read,
    .statfs = fs_statfs,

    .create = fs_create, /* write operations */
    .mkdir = fs_mkdir,
    .unlink = fs_unlink,
    .rmdir = fs_rmdir,
    .utime = fs_utime,
    .truncate = fs_truncate,
    .write = fs_write,
};
