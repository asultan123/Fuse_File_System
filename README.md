read-write file system
------------------------------------------------

Implementation of a read-write version of a Unix-like file system using the FUSE library as part of CS5600

**Implemented functions in `homework.c`:**

- `fs_init` - constructor (i.e. put your init code here)
- `fs_getattr` - get attributes of a file/directory
- `fs_readdir` - enumerate entries in a directory
- `fs_read` - read data from a file
- `fs_statfs` - report file system statistics
- `fs_rename` - rename a file
- `fs_chmod` - change file permissions
- `fs_utime` - change access and modification times
- `fs_create` - create a new (empty) file
- `fs_mkdir` - create new (empty) directory
- `fs_unlink` - remove a file
- `fs_rmdir` - remove a directory
- `fs_truncate` - delete the contents of a file
- `fs_write` - write to a file

**LIMITATIONS** 

1. Directories are not nested more than 10 deep
2. Directories are never bigger than 1 block
3. `rename` is only used within the same directory - e.g. `rename("/dir/f1", "/dir/f2")`
4. Truncate is only ever called with len=0, so that you delete all the data in the file

Code was run under two different frameworks - a C unit test framework (libcheck), and the FUSE library which ran the code as a real file system

Note that you will probably need to install several packages for the included code to work; in Ubuntu you can use the following commands:
```
sudo apt install check
sudo apt install libfuse-dev
sudo apt install zlib1g-dev
```
