Testing and Debugging
=====================

To test the read-only part of your implementation, the makefile will generate a predefined disk image in your directory, named `test.img`; it has known contents and the instructions below give details (e.g. names, sizes, checksums) to verify that your code is reading it properly. You will test your code in two different ways - using the libcheck unit test interface, and running as a FUSE file system.

Testing with libcheck
---------------------

Your tests will go in the file `unittest-1.c`, which has the #include files and a few other things that you'll need.

The `main` function already has the following parts written for you:
- calling `block_init("test.img")`, so `block_read` and `block_write` will work
- calling your init method
- setting up a test suite, running it, and printing the output

Individual tests are in separate functions that are declared with the START_TEST macro:
```C
START_TEST(test_name)
{
    code;
    code;
}
END_TEST
```

For each test you'll need a corresponding call to `suite_add_tcase` in `main`:
```C
tcase_add_test(tc, test_name);
```

In your test functions you'll verify results using libcheck macros. You'll probably want to develop your tests in stages, as any failing tests will cause the entire test run to abort. (If you want to be able to run through all of them you can comment out the line that sets NOFORK mode - that's what I do in the grading tests, but it means that you won't be able to use GDB to debug)
```C
ck_abort("message");      /* fail unconditionally */
ck_assert(expr);          /* fail if expression is false */
ck_assert_int_eq(i1, i2);
ck_assert_int_ne/lt/le/ge/gt(i1, i2); /* not eq, less than, etc. */
ck_assert_str_eq("string1", "string2");
ck_assert_ptr_eq(ptr, NULL);
ck_assert_ptr_ne(ptr, NULL); /* assert ptr is NOT null */
```

Constants
---------
The following are defined in the various include files - you'll need most of them.

File type constants - see 'man 2 stat':

* `S_IFMT` - mask for type bits
* `S_IFREG` - regular file
* `S_IFDIR` - directory

A lot of times it's easier to use:

* `S_IFDIR(mode)` - true if it's a directory
* `S_IFREG(mode)` - true if it's a regular file

Error number constants - note that the comments in homework.c will tell you which errors are legal to return from which methods.

* ENOENT - file/dir not found
* EIO - if `block_read` or `block_write` fail
* ENOMEM  - out of memory
* ENOTDIR - what it says
* EISDIR - what it says
* EINVAL - invalid parameter (see comments in homework.c)
* EOPNOTSUPP - not finished with the homework yet

Note that you can use `strerror(e)` to get a string representation of an error code. (you'll need to change the error code to a positive number first)

How to test
-----------
You'll call the methods in your `fs_ops` structure to get file attributes, read directories, and read files.

You'll need to verify that the data you read from the files is correct. To do that I've provided checksums for the data; once you read it into memory you can calculate your own checksum like this:
```C
unsigned cksum = crc32(0, buf, len);
```
then you can check it against the checksums below.

You can write individual tests for each file and directory, but it's a lot easier to loop through a data structure. Here's a suggested way to do that:
```C
struct {
    char *path;
    int  len;
    unsigned cksum;  /* UNSIGNED. TESTS WILL FAIL IF IT'S NOT */
    ... other attributes? ...
} table_1[] = {
    {"/this/is/a/file", 120, 1234567},
    {"/another/file", 17, 4567890},
    {NULL}
};

{
    for (int i = 0; table_1[i].path != NULL; i++) {
        do something with table_1[i].path
        check against table_1[i].len, cksum etc.
    }
}
```

The `readdir` method takes a callback function, called `filler` in the parameter list. Here's an example of how to use it; note that the second argument to `readdir` is a `void*` ptr that gets passed as the first argument to the filler. You don't have to worry about `off` or the two parameters to `readdir` that I set to 0 and NULL.
```C
int test_filler(void *ptr, const char *name,
    		const struct stat *st, off_t off)
{
    struct something *s = ptr;
    printf("file: %s, mode: %o\n", name, st->st_mode);
    return 0;
}

START_TEST(readdir)
{
    struct something s;
    int rv = fs_ops.readdir("/", &s, test_filler, 0, NULL);
    ck_assert(rv >= 0);
}
END_TEST
```

A suggested method for testing `readdir`: create a list of filenames, with a flag for each indicating if it was seen:
```C
struct {
    char *name;
    int   seen;
} dir1_table[] = {
    {"file.txt", 0},
    {"dir2", 0},
    {NULL}
};
```
Your filler function can loop through the table looking for a name; if it finds it, it can mark the `seen` flag, and when `readdir` is done you can check that all the `seen` flags have been set. If you want to use the table again (e.g. for multiple tests) you can go back and set all the `seen` flags to zero.
Oh, and your filler should probably flag an error if it doesn't find a name, or if the name is already marked "seen".

Contents of test.img
--------------------

First, the values returned by `getattr` in `struct stat`, for all files and directories:

# path uid gid mode size ctime mtime
"/", 0, 0, 040777, 4096, 1565283152, 1565283167
"/file.1k", 500, 500, 0100666, 1000, 1565283152, 1565283152
"/file.10", 500, 500, 0100666, 10, 1565283152, 1565283167
"/dir-with-long-name", 0, 0, 040777, 4096, 1565283152, 1565283167
"/dir-with-long-name/file.12k+", 0, 500, 0100666, 12289, 1565283152, 1565283167
"/dir2", 500, 500, 040777, 8192, 1565283152, 1565283167
"/dir2/twenty-seven-byte-file-name", 500, 500, 0100666, 1000, 1565283152, 1565283167
"/dir2/file.4k+", 500, 500, 0100777, 4098, 1565283152, 1565283167
"/dir3", 0, 500, 040777, 4096, 1565283152, 1565283167
"/dir3/subdir", 0, 500, 040777, 4096, 1565283152, 1565283167
"/dir3/subdir/file.4k-", 500, 500, 0100666, 4095, 1565283152, 1565283167
"/dir3/subdir/file.8k-", 500, 500, 0100666, 8190, 1565283152, 1565283167
"/dir3/subdir/file.12k", 500, 500, 0100666, 12288, 1565283152, 1565283167
"/dir3/file.12k-", 0, 500, 0100777, 12287, 1565283152, 1565283167
"/file.12k+", 0, 500, 0100666, 12289, 1565283152, 1565283167
"/file.8k+", 500, 500, 0100666, 8195, 1565283152, 1565283167

File lengths and checksums:
# file  length  checksum
1786485602, 1000, "/file.1k"
855202508, 10, "/file.10"
4101348955, 12289, "/dir-with-long-name/file.12k+"
2575367502, 1000, "/dir2/twenty-seven-byte-file-name"
799580753, 4098, "/dir2/file.4k+"
4220582896, 4095, "/dir3/subdir/file.4k-"
4090922556, 8190, "/dir3/subdir/file.8k-"
3243963207, 12288, "/dir3/subdir/file.12k"
2954788945, 12287, "/dir3/file.12k-"
4101348955, 12289, "/file.12k+"
2112223143, 8195, "/file.8k+"

Directory contents (for testing readdir):

"/" : "dir2", "dir3", "dir-with-long-name", "file.10",
      "file.12k+", "file.1k", "file.8k+"
"/dir2" : "twenty-seven-byte-file-name", "file.4k+"
"/dir3" : "subdir", "file.12k-"
"/dir3/subdir" : "file.4k-", "file.8k-", "file.12k"
"/dir-with-long-name" : "file.12k+"

Suggested tests (i.e. the grading tests)
----------------------------------------

`fs_getattr` - for each of the files and directories, call `getattr` and verify the results against values from the table above.

`fs_getattr` - path translation errors. Here's a list to test, giving error code and path:
* ENOENT - "/not-a-file"
* ENOTDIR - "/file.1k/file.0"
* ENOENT on a middle part of the path - "/not-a-dir/file.0"
* ENOENT in a subdirectory "/dir2/not-a-file"

`fs_readdir` - check that calling readdir on each directory in the table above returns the proper set of entries.

`fs_readdir` errors - call readdir on a file that exists, and on a path that doesn't exist, verify you get ENOTDIR and ENOENT respectively.

`fs_read` - single big read - The simplest test is to read the entire file in a single operation and make sure that it matches the correct value.

I would suggest using a buffer bigger than the largest file and passing the size of that buffer to `fs_read` - that way you can find out if `fs_read` sometimes returns too much data.

`fs_read` - multiple small reads - write a function to read a file N bytes at a time, and test that you can read each file in different sized chunks and that you get the right result. Note that there's no concept of a current position in the FUSE interface - you have to use the offset parameter. (e.g read (len=17,offset=0), then (len=17,offset=17), etc.)

(I'll test your code with N=17, 100, 1000, 1024, 1970, and 3000)

`fs_statvfs` - run 'man statvfs' to see a description of the structure. The values for the test image should be:
 * `f_bsize` - 4096 (block size, bytes)
 * `f_blocks` - 1024 (total number of blocks)
 * `f_bfree` -  731 (free blocks)
 * `f_namemax` - 27

**Methods that modify the disk image**
Both `chmod` and `rename` will (obviously) modify the disk image. Be sure to regenerate the test image each time you run your tests.

Hint - you'll never forget to do that if you put the statement `system("python gen-disk.py -q disk1.in test.img")'` at the beginning of your `main` function in `unittest-1.c`.

`fs_chmod` - change permissions for a file, check that (a) it's still a file, and (b) it has the new permissions. Do the same for a directory. (note that `chmod` should only change the bottom 9 bits of the mode word)

`fs_rename` - try renaming a file and then reading it, renaming a directory and then reading a file from the directory.

Running as a FUSE file system
-----------------------------
If you build and run the `hwfuse` executable you can run it with the following command:

    ./hwfuse -image test.img [dir]

and (assuming it doesn't crash) it will mount the file system in test.img on top of the specified directory and run in the background, letting us see the contents:

```
hw4$ mkdir dir
hw4$ ./hwfuse -image test.img dir
hw4$ ls dir
dir2  dir3  dir-with-long-name  file.10  file.12k+  file.1k  file.8k+
hw4$ ls -l dir
total 7
drwxrwxrwx 1  500  500  8192 Aug  8 12:52 dir2
drwxrwxrwx 1 root  500  4096 Aug  8 12:52 dir3
drwxrwxrwx 1 root root  4096 Aug  8 12:52 dir-with-long-name
-rw-rw-rw- 1  500  500    10 Aug  8 12:52 file.10
-rw-rw-rw- 1 root  500 12289 Aug  8 12:52 file.12k+
-rw-rw-rw- 1  500  500  1000 Aug  8 12:52 file.1k
-rw-rw-rw- 1  500  500  8195 Aug  8 12:52 file.8k+
hw4$ cd dir
hw4/dir$ ls -l dir2
total 2
-rwxrwxrwx 1 500 500 4098 Aug  8 12:52 file.4k+
-rw-rw-rw- 1 500 500 1000 Aug  8 12:52 twenty-seven-byte-file-name
hw4/dir$
```

To unmount the directory:
```
hw4$ fusermount -u dir
hw4$
```
Note that you have to unmount the directory even (especially?) if your program crashes.

`Transport endpoint is not connected`
This means your program crashed and you didn't use `fusermount -u` to clean up after it.

To use GDB in FUSE mode you'll want to specify two additional flags that get interpreted by the FUSE library: `-s` specifies single-threaded mode, and `-d` specifies debug mode so it runs in the foreground.
```
gdb --args ./hwfuse -s -d -image test.img dir
```
