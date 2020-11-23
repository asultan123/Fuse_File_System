Implementation advice
---------------------

### **1** subdirectory depth

It's fine to assume a maximum depth for subdirectories, e.g. 10 in the example below.

### **2** copying the path

Note that FUSE declares the `path` argument to all your methods as "const char *", not "char *", which is really annoying. It means that you need to copy the path before you can split it using `strtok`, typically using the `strdup` function. E.g.:

```C
int fs_xyz(const char *c_path, ...)
{
	char *path = strdup(c_path);
	int inum = translate(path);
	free(path);
	...
}
```

### **3** path splitting

The first thing you're probably going to have to do with that path is to split it into components - here's some code to do that, using the `strtok` library function:

```C
#define MAX_PATH_LEN 10
#define MAX_NAME_LEN 27

int parse(char *path, char **argv)
{
    int i;
    for (i = 0; i < MAX_PATH_LEN; i++) {
        if ((argv[i] = strtok(path, "/")) == NULL)
            break;
        if (strlen(argv[i]) > MAX_NAME_LEN)
            argv[i][MAX_NAME_LEN] = 0;        // truncate to 27 characters
        path = NULL;
    }
    return i;
}
```

### **4**  factoring

**PLEASE** factor out the code that you use for translating paths into inodes. Nothing good can come of duplicating the same code in every one of your functions, and you'll find that you've fixed one bug in some of the copies and a different bug in other copies.

I would suggest factoring out a function which translates a path in count,array form into an inode number. That way you can return an error (negative integer) if it's not found, and accessing the inode itself is just a matter of reading that block number into a `struct fs_inode`. Note that accessing the parent directory is easy using count/array format - it's just (count-1)/array.

### **5** Efficiency

Don't worry about efficiency. In the simplest implementation, every single operation will read every single directory from the root down to the file being accessed. That's OK.

### **6** Path translation

Assume you've split your path, and you now have a count and an array of strings:

```C
int    pathc;
char **pathv;
```
e.g. for "/home/pjd" pathc=2, pathv={"home","pjd"}.

The logic to translate this into the inode number for "/home/pjd" is:
```C
inum = 2              // root inode
for i = 0..pathc-1 :
read inum -> _in
if _in.mode isn't a directory:
    return -ENOTDIR
read the directory entries
search for names[i]
found:
  inum = dirent.inum
not found:
  return -ENOENT
```
This will return the inode number of the file/directory corresponding to that path, or -ENOENT/-ENOTDIR if there was an error, with the standard convention that a negative number is an error.

The logic above gives you a function with the signature:
```C
int translate(int pathc, char **pathv);
```

### **7** translation for mkdir/create/rename/rmdir/unlink

For these functions you need to operate on the containing directory as well as the file/directory indicated by the path. E.g.:
```
mkdir /dir1/dir2/parent/leaf
```
will not only create a new inode for /dir1/dir2/parent/leaf, but will modify /dir1/dir2/parent to add an entry for it.

If you translate your path into count/array form this is really easy - finding the parent is just `translate(pathc-1, pathv)`, and the name of the leaf is `pathv[pathc-1]`.

(and for e.g. `mkdir` you'll want to verify that the destination **doesn't** exist, i.e. that translate returns an error)

### **8** `read-img.py`

This utility will print out lots of information about a disk image, which may help figure out what you messed up.
```
$ python read-img.py test.img 
blocks used:  0  1  2  21  22  [...]

inodes found:  2  21  55  [...]

inode 2:
  "/" (0,0) 40777 4096 
  block 399 
    [2] "file.1k" -> 389
    [3] "file.10" -> 268
    [4] "dir-with-long-name" -> 253
    [5] "dir2" -> 213
    [6] "dir3" -> 238
    [7] "file.12k+" -> 327
    [8] "file.8k+" -> 59

inode 389:
  "/file.1k" (500,500) 100666 1000 
  blocks:  365
```
The "blocks used" line shows the blocks (including inodes) marked as used in the bitmap; the "inodes found" line shows the inodes found by recursively descending from the root. It's useful for debugging some things; not useful for others.

### **9** UID/GID, timestamps in `mkdir`, `create`

The "correct" way to get the UID and GID when you're creating a file or directory is this:
```C
struct fuse_context *ctx = fuse_get_context();
uint16_t uid = ctx->uid;
uint16_t gid = ctx->gid;
```

Since we're not running FUSE as root, you're only going to see your own UID and GID, so although using `getuid()` and `getgid()` is technically wrong, it's going to give the correct answer in all the test cases.

You can get the timestamp in the proper format with `time(NULL)`. Although the function returns a 64-byte integer, you can assign it to the unsigned 32-bit timestamps in the inode without worrying about overflow for another 85 years or so.

### **10** `mode` and `mode`

As mentioned in the source code comments and TESTING-2.md:
The `mode` value passed to `fs_create` will have the inode type bits set - e.g. 0100777 - so you can just put it in the inode mode field.
The `mode` value passed to `fs_mkdir` does **not** have the inode type bits set - e.g. 0777 - so you have to OR it with S_IFDIR (giving e.g. 040777).

No, this doesn't make sense.
