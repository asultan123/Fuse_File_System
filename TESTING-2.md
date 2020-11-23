Testing part 2
=========

**First, make sure you create a new disk image each time you test**

When you run buggy code and write to your disk, it's going to mess up the disk image. When you fix the bug, your code might not work if you don't replace the corrupted disk image. You'll be unhappy. Don't do that.

You can make an empty disk named `test2.img` with the following command:
```
$ python gen-disk.py -q disk2.in test2.img
```
(because I haven't written `mkfs` yet...)

To test your write logic (including mkdir, create, etc.) you need to be able to trust your `readdir` and `read` implementations, so that you can verify the results. That's the primary reason why the assignment is explicitly split into two parts.

`fuse_getcontext` mocking
-------------------------

We're using a test strategy called "mocking" to deal with the `fuse_getcontext` call - we provide a fake version of the function in our test code, and in each test we can fill in any information we want it to return.

create/mkdir/unlink/rmdir
----------------------

**WARNING:** The `mode` parameter passed to `create` must have the inode type bits set properly for a file - e.g. 0100777 (or S_IFREG | 0777)
The `mode` parameter passed to `mkdir` **does not have those bit set** - e.g. 0777. 

**Non-error cases** - these are fairly simple:

- create multiple subdirectories (with `mkdir`) in a directory, verify that they show up in `readdir`
- delete them with `rmdir`, verify they don't show up anymore
- create multiple files in a directory (with `mknod`), verify that they show up in `readdir`
- delete them (`unlink`), verify they don't show up anymore

Now run these tests (a) in the root directory, (b) in a subdirectory (e.g. "/dir1") and (c) in a sub-sub-directory (e.g. "/dir1/dir2")

Once you've written the tests to run in the root directory, it should be simple (either a loop or copy and paste) to add cases (b) and (c).

Once you've implemented `write`, add an additional set of unlink tests where you write data into the files first. You might want to use `statvfs` to verify that unlink freed the blocks afterwards. (the grading tests will do that)

**Error cases** - you should test the following

mknod:
-  bad path /a/b/c - b doesn't exist (should return -ENOENT)
-  bad path /a/b/c - b isn't directory (ENOTDIR)
-  bad path /a/b/c - c exists, is file (EEXIST)
-  bad path /a/b/c - c exists, is directory (EEXIST)
-  too-long name (more than 27 characters)

For the long name case, you're allowed to either return -EINVAL or truncate the name.

mkdir:
-  bad path /a/b/c - b doesn't exist (ENOENT)
-  bad path /a/b/c - b isn't directory (ENOTDIR)
-  bad path /a/b/c - c exists, is file (EEXIST)
-  bad path /a/b/c - c exists, is directory (EEXIST)
-  too-long name

unlink:
-  bad path /a/b/c - b doesn't exist (ENOENT)
-  bad path /a/b/c - b isn't directory (ENOTDIR)
-  bad path /a/b/c - c doesn't exist (ENOENT)
-  bad path /a/b/c - c is directory (EISDIR)

rmdir:
-  bad path /a/b/c - b doesn't exist (ENOENT)
-  bad path /a/b/c - b isn't directory (ENOTDIR)
-  bad path /a/b/c - c doesn't exist (ENOENT)
-  bad path /a/b/c - c is file (ENOTDIR)
-  directory not empty (ENOTEMPTY)

`write` - test data
-------

To test `write` you'll need test data. For debugging it you'll want to be able to figure out what went wrong, so I'm going to suggest you put a repeating pattern into your data - here's an example, where I want to write 4000 bytes:
```
    char *ptr, *buf = malloc(4010); // allocate a bit extra
    int i;
    for (i=0, ptr = buf; ptr < buf+4000; i++)
        ptr += sprintf(ptr, "%d ", i);
    fs_ops.write("/path", buf, 4000, 0, NULL);  // 4000 bytes, offset=0
```

So you get something that looks like:
```
(gdb) l
5	{
6	    char *ptr, *buf = malloc(4000);
7	    int i;
8	    for (i=0, ptr = buf; ptr < buf+4000; i++)
9	        ptr += sprintf(ptr, "%d ", i);
10	    printf("breakpoint here\n");
11	}
(gdb) p buf
$1 = 0x5555555592a0 "0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 "...
(gdb) 
```

Two other stragegies:
- fill in each byte with a random number using `random()`
- fill in bytes with 0, 1, 2, ...

The first strategy is great for testing someone else's code (I use it for the grding tests) but not so good for debugging. The second approach isn't so great, because you're going to be making mistakes with 4KB blocks of data, so using a pattern that wraps around every 256 bytes might not show the problems.

`write` - tests
---------------

**append tests**
You should create files of various sizes (at a minimum: <1 block, 1 block, <2 blocks, 2 blocks, <3 blocks, 3 blocks), writing to them with the same buffer sizes that you used in your read tests. Note that (like read) you'll have to calculate the offset yourself - there's no file pointer in the FUSE interface.

You'll have to read the data back into a different buffer, then you can verify that the original data and the data you read back are the same. You can use checksums to do this, or the `memcmp` function, or something else if you want.

Now unlink them, and use `statvfs` to verify that the number of free blocks when you're done is the same as the number of free blocks before you created the files and wrote to them.

**overwrite tests**
For this you'll need two buffers, with different contents. (hint - if you use the code above to generate your data, just start at a different number)

Write the first buffer to the file, then write the second buffer, but starting over at offset 0. Now read the file back and verify that the contents match the second buffer.

Oh, I assume you're checking the return value from `write` and other FUSE functions each time you use them, and verifying that they =0 the times they should succeed, right?

`truncate`
----------
Verify that you can truncate files of the same set of sizes that you use in the write tests. Use `statvfs` to verify that the blocks have been freed.

