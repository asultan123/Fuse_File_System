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
unsigned char bitmap[FS_BLOCK_SIZE];
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
    char buf[FS_BLOCK_SIZE];
    if (block_read(&superblock, 0, FS_BLOCK_SIZE) == -EIO)
    {
        printf("ERROR: Failed to load superblock\n");
        return (void*)-EIO;
    }
    if (block_read(&bitmap, 1, FS_BLOCK_SIZE) == -EIO)
    {
        printf("ERROR: Failed to load bitmap\n");
        return (void*)-EIO;
    }
    if (block_read(&rootInode, 2, FS_BLOCK_SIZE) == -EIO)
    {
        printf("ERROR: Failed to load rootInode\n");
        return (void*)-EIO;
    }

    statVfs.f_bsize = FS_BLOCK_SIZE;
    statVfs.f_blocks = superblock.disk_size - 2;
    unsigned int blocksUsed = 0;
    for(int i = 0; i<FS_BLOCK_SIZE; i++)
    {
        blocksUsed += bit_test(bitmap, i);
    }
    statVfs.f_bfree = statVfs.f_blocks - blocksUsed;
    statVfs.f_bavail = statVfs.f_bfree;
    statVfs.f_namemax = MAX_NAME_LEN;

    printf("INFO: Loaded filesystem with the following proprties:\n");
    printf("INFO: Block Size: %u\n", FS_BLOCK_SIZE);
    printf("INFO: Disk MAGIC: %u\n", superblock.magic);
    printf("INFO: Disk Size: %u\n", superblock.disk_size);
    printf("INFO: Blocks Used: %u\n", superblock.disk_size);
    printf("INFO: Blocks Available: %u\n", superblock.disk_size);
    printf("INFO: Blocks Free: %u\n", superblock.disk_size);

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
int translate(int pathc, char **pathv)
{
    struct fs_inode curInode;
    // MAX 128 entries in a directory assumption
    struct fs_dirent curDir[128];
    int inodeIndex = 2;
    for (int pathToken = 0; pathToken < pathc - 1; pathToken++)
    {
        if(!block_read(&curInode, inodeIndex, 1))
        {
            return -EIO;
        }
        if(!S_ISDIR(curInode.mode))
        {
            return -ENOTDIR;
        }
        // MAX 128 entries in a directory assumption
        if(!block_read(&curDir, curInode.ptrs[0], 1))
        {
            return -EIO;
        }
        for(int dirEntry = 0; dirEntry < 128; dirEntry++)
        {
            if(curDir[dirEntry].valid && strcmp(pathv[pathToken], curDir[dirEntry].name))
            {
                inodeIndex = curDir[dirEntry].inode;
                break;
            }
            else
            {
                inodeIndex = -1;
            }
        }
        if(inodeIndex == -1)
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

int inode_to_stat(struct fs_inode* inode, struct stat *sb)
{
    
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
    return -EOPNOTSUPP;
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
    return -EOPNOTSUPP;
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
    /* your code here */
    return -EOPNOTSUPP;
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
    /* your code here */
    return -EOPNOTSUPP;
}

/* unlink - delete a file
 *  success - return 0
 *  errors - path resolution, ENOENT, EISDIR
 */
int fs_unlink(const char *path)
{
    /* your code here */
    return -EOPNOTSUPP;
}

/* rmdir - remove a directory
 *  success - return 0
 *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
 */
int fs_rmdir(const char *path)
{
    /* your code here */
    return -EOPNOTSUPP;
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
    return -EOPNOTSUPP;
}

/* chmod - change file permissions
 *
 * success - return 0
 * Errors - path resolution, ENOENT.
 */
int fs_chmod(const char *path, mode_t mode)
{
    /* your code here */
    return -EOPNOTSUPP;
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
    return -EOPNOTSUPP;
}

/* truncate - truncate file to exactly 'len' bytes
 * success - return 0
 * Errors - path resolution, ENOENT, EISDIR, EINVAL
 *    return EINVAL if len > 0.
 */
int fs_truncate(const char *path, off_t len)
{
    /* you can cheat by only implementing this for the case of len==0,
     * and an error otherwise.
     */
    if (len != 0)
        return -EINVAL; /* invalid argument */

    /* your code here */
    return -EOPNOTSUPP;
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
    return -EOPNOTSUPP;
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
    return -EOPNOTSUPP;
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
    /* your code here */
    return -EOPNOTSUPP;
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
