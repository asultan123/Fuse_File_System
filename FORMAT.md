File System Definition
======================

The file system uses a 4KB block size. It is simplified from the classic Unix file system by (a) using full blocks for inodes, and (b) putting all block pointers in the inode. This results in the following differences:

1. There is no need for a separate inode region or inode bitmap – an inode is just another block, marked off in the block bitmap
2. Limited file size – a 4KB inode can hold 1018 32-bit block pointers, for a max file size of about 4MB
3. Disk size – a single 4KB block (block 1) is reserved for the block bitmap; since this holds 32K bits, the biggest disk image is 32K * 4KB = 128MB

Although the file size and disk size limits would be serious problems in practice, they won't be any trouble for the assignment since you'll be dealing with disk sizes of 1MB or less. (and they limit the maximum file size you can accidentally check into Git...)

File System Format
------------------
The disk is divided into blocks of 4096 bytes, and into 3 regions: the superblock, the block bitmap, and file/inode blocks, with the first file/inode block (block 2) always holding the root directory.

```
	  +-------+--------+----------+------------------------+
	  | super | block  | root dir |     data blocks ...    |
	  | block | bitmap |   inode  |                        |
	  +-------+--------+----------+------------------------+
    block     0        1         2          3 ...
```

**Superblock:**
The superblock is the first block in the file system, and contains the  information needed to find the rest of the file system structures. 
The following C structure (found in `fs5600.h`) defines the superblock:

```C
struct fsx_superblock {
	uint32_t magic;             /* 0x30303635 - shows as "5600" in hex dump */
	uint32_t disk_size;         /* in 4096-byte blocks */
	char pad[4088];             /* to make size = 4096 */
};
```

Note that `uint32_t` is a standard C type found in the `<stdint.h>` header file, and refers to an unsigned 32-bit integer. (similarly, `uint16_t`, `int16_t` and `int32_t` are unsigned/signed 16-bit ints and signed 32-bit ints)

**Inodes:**
These are based on the standard Unix-style inode; however they're bigger and have no indirect block pointers. Each inode corresponds to a file or directory; in a sense the inode is that file or directory, which can be uniquely identified by its inode number, which is the same as its block number. The root directory is always found in inode 2; inode 0 is invalid and can be used as a 'null' value.

note: path lookup always begins with inode 2, the root directory, which (see above) is in block 2. 

```C
struct fs_inode {
    uint16_t uid;      /* file owner */
    uint16_t gid;      /* group */
    uint32_t mode;     /* type + permissions (see below) */
    uint32_t ctime;    /* creation time */
    uint32_t mtime;    /* modification time */
    int32_t  size;     /* size in bytes */
    uint32_t ptrs[FS_BLOCK_SIZE/4 - 5]; /* inode = 4096 bytes */
};
```

**"Mode":**
The FUSE API (and Linux internals in general) mash together the concept of object type (file/directory/device/symlink...) and permissions. The result is called the file "mode", and looks like this:

```
        |<-- S_IFMT --->|           |<-- user ->|<- group ->|<- world ->|
        +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
        | F | D |   |   |   |   |   | R | W | X | R | W | X | R | W | X |
        +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
```
Since it has multiple 3-bit fields, it is commonly displayed in base 8 (octal) - e.g. permissions allowing RWX for everyone (rwxrwxrwx) are encoded as '777'. (Hint - in C, which you'll be using for testing, octal numbers are indicated by a leading "0", e.g. 0777, so the expression 0777 == 511 is true) The F and D bits correspond to 0100000 and 040000.

side note: Since the demise of 36-bit computers in the early 70s, Unix permissions are about the only thing in the world that octal is used for.

There are a bunch of macros (in `<sys/stat.h>`) you can use for looking at the mode; you can see the official documentation with the command `man inode`, but the important ones are:

- `S_ISREG(m)`  - is it (i.e. the inode with mode `m`) a regular file?
- `S_ISDIR(m)` - is it a  directory?
- `S_IFMT` - bitmap for inode type bits.

You can get just the inode type with the expression `m & S_IFMT`, and just the permission bits with the expression `m & ~S_IFMT`. (note that `~` is bitwise NOT - i.e. 0s become 1s and 1s become 0s)

**Directories:**
Directories are a multiple of one block in length, holding an array of directory entries:
```C
struct fs_dirent {
	uint32_t valid : 1;
	uint32_t inode : 31;
	char name[28];       /* with trailing NUL */
};
```
Each "dirent" is 32 bytes, giving 4096/32 = 128 directory entries in each block. The directory size in the inode is always a multiple of 4096, and unused directory entries are indicated by setting the 'valid' flag to zero. The maximum name length is 27 bytes, allowing entries to always have a terminating 0 byte so you can use `strcmp` etc. without any complications.

**for this assignment you can assume directories are always one block in length - the test scripts will never create more than 128 entries in a directory**

**Storage allocation:**
Unlike the Unix file system discussed in lecture, inodes in this file system take up a full block, so there's no need for separate allocation of inodes and blocks. The file system has a single bitmap block, block 1; bit **i** in the bitmap is set if block **i** is in use.

The bits for blocks 0, 1 and 2 will be set to 1 when the file system is created, so you don't have to worry about excluding them when you search for a free block.

*side note: Using a single block for the bitmap means that the maximum total file system size is 4KBx8 4KB blocks, or 128MB, which is ridiculously small. It also limits the size of the file systems images you can accidentally check into Git.*

You will need to read this block into a 4KB buffer memory in order to access it (hint: make that buffer a global variable, and read it in your init function), and write it back after you allocate or free a block. (you can do this either at the end of any operations that might allocate/free, or in your allocate/free function itself - I don't care if you write it multiple times)

You're given the following functions to handle the bitmap in memory:

```C
bit_test((void*)map, int i);
void bit_set(unsigned char *map, int i);
void bit_clear(unsigned char *map, int i);
```

