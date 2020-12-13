/*
 * file:        unittest-2.c
 * description: libcheck test skeleton, part 2
 */

#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 26

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>
#include <zlib.h>
#include <fuse.h>
#include <stdlib.h>
#include <errno.h>

// vscode issue
#define MY_S_IFREG 0100000

/* mockup for fuse_get_context. you can change ctx.uid, ctx.gid in 
 * tests if you want to test setting UIDs in mknod/mkdir
 */
struct fuse_context ctx = {.uid = 500, .gid = 500};
struct fuse_context *fuse_get_context(void)
{
    return &ctx;
}

extern struct fuse_operations fs_ops;
extern void block_init(char *file);

struct dir_test_data
{
    char *name;
    int seen;
    int isDir;
    int wasDir;
};
/*
    Helper "filler" file for testing readdir.  Updates the tables above based on the filename its given.
*/
int test_dir(void *ptr, const char *name, const struct stat *st, off_t off)
{
    struct dir_test_data *thisTable = (struct dir_test_data *)ptr;
    int notseen = 1;
    while (thisTable->name[0] != '\0')
    {
        if (strcmp(thisTable->name, name) == 0)
        {
            notseen = 0;
            thisTable->seen = 1;
            thisTable->wasDir = (S_ISDIR(st->st_mode) ? 1 : 0);
        }
        thisTable++;
    }
    // ok, we're at the last entry in the ref table set, mark if we haven't seen this entry
    // ideally this should remain at 0 - no unseen entries
    thisTable->seen += notseen;
    return 0;
}
void validate_directory(const char *dir, struct dir_test_data *dir_data)
{
    ck_assert_int_eq(fs_ops.readdir(dir, dir_data, test_dir, 0, NULL), 0);
    int i = 0;
    for (; dir_data[i].name[0] != '\0'; i++)
    {
        ck_assert(dir_data[i].seen && dir_data[i].isDir == dir_data[i].wasDir);
    }
    ck_assert_int_eq(dir_data[i].seen, 0); // make sure there were no unexpected entries
}

void init_test_data(char *b, int size, int pattern, int cycle)
{
    for (int i = 0; i < size; i++)
        b[i] = (char)((cycle < 0 ? i : (i % cycle)) % pattern);
}

/* note that your tests will call:
 *  fs_ops.getattr(path, struct stat *sb)
 *  fs_ops.readdir(path, NULL, filler_function, 0, NULL)
 *  fs_ops.read(path, buf, len, offset, NULL);
 *  fs_ops.statfs(path, struct statvfs *sv);
 */
START_TEST(create_subdir_test)
{
    struct dir_test_data root_table[] = {
        {"dir1", 0, 1, 0},
        {"dir2", 0, 1, 0},
        {"", 0, 0, 0}};
    struct dir_test_data dir1_table[] = {
        {"dir11", 0, 1, 0},
        {"file1.fil", 0, 0, 1},
        {"this-dir-name-is-way-too-lo", 0, 1, 0},
        {"", 0, 0, 0}};
    struct dir_test_data dir2_table[] = {
        {"dir21", 0, 1, 0},
        {"", 0, 0, 0}};

    struct statvfs fsstats;
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    int free_blocks = fsstats.f_bavail;
    // set up the dir structure we want

    ck_assert_int_eq(fs_ops.mkdir("/ins-dirs", 0777), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 2);

    ck_assert_int_eq(fs_ops.mkdir("/ins-dirs/dir1", 0777), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 4);

    ck_assert_int_eq(fs_ops.mkdir("/ins-dirs/dir2", 0777), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 6);

    ck_assert_int_eq(fs_ops.mkdir("/ins-dirs/dir1/dir11", 0777), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 8);

    ck_assert_int_eq(fs_ops.mkdir("/ins-dirs/dir2/dir21", 0777), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 10);

    // add a file so we can try to use it in a path
    ck_assert_int_eq(fs_ops.create("/ins-dirs/dir1/file1.fil", MY_S_IFREG | 0777, NULL), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 11); // it's an empty file, no data block reserved

    // test the negative cases first
    ck_assert_int_eq(fs_ops.mkdir("/ins-dirs/dir1/dir2/dir3", 0777), -ENOENT);
    ck_assert_int_eq(fs_ops.mkdir("/ins-dirs/dir1/file1.fil/dir3", 0777), -ENOTDIR);
    ck_assert_int_eq(fs_ops.mkdir("/ins-dirs/dir1/file1.fil", 0777), -EEXIST);
    ck_assert_int_eq(fs_ops.mkdir("/ins-dirs/dir1/dir11", 0777), -EEXIST);

    // long name truncation
    ck_assert_int_eq(fs_ops.mkdir("/ins-dirs/dir1/this-dir-name-is-way-too-long", 0777), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 13);

    // iterate over the newly created directories
    struct dir_test_data *test_tables[] = {root_table, dir1_table, dir2_table};
    char *test_names[] = {"/ins-dirs", "/ins-dirs/dir1", "/ins-dirs/dir2"};

    for (int t = 0; t < 3; t++)
    {
        validate_directory(test_names[t], test_tables[t]);
    }

    // TODO: DONT FORGET TO REMOVE THE LONG NAME DIRECTORY!!!
    // // cleanup...
    ck_assert_int_eq(fs_ops.unlink("/ins-dirs/dir1/file1.fil"), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 12);
    ck_assert_int_eq(fs_ops.rmdir("/ins-dirs/dir1/this-dir-name-is-way-too-lo"), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 10);
    ck_assert_int_eq(fs_ops.rmdir("/ins-dirs/dir1/dir11"), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 8);
    ck_assert_int_eq(fs_ops.rmdir("/ins-dirs/dir2/dir21"), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 6);
    ck_assert_int_eq(fs_ops.rmdir("/ins-dirs/dir1"), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 4);
    ck_assert_int_eq(fs_ops.rmdir("/ins-dirs/dir2"), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 2);
    ck_assert_int_eq(fs_ops.rmdir("/ins-dirs"), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks);
}
END_TEST

START_TEST(delete_subdir_test)
{
    struct dir_test_data root_table[] = {
        {"del-dir1", 0, 1, 0},
        {"del-dir2", 0, 1, 0},
        {"", 0, 0, 0}};
    struct dir_test_data dir1_table[] = {
        {"del-dir11", 0, 1, 0},
        {"file1.fil", 0, 0, 1},
        {"", 0, 0, 0}};
    struct dir_test_data dir2_table[] = {
        {"del-dir21", 0, 1, 0},
        {"", 0, 0, 0}};
    struct dir_test_data empty_del_root_table[] = {
        {"", 0, 0, 0}};

    struct statvfs fsstats;
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    int free_blocks = fsstats.f_bavail;

    // set up the dir structure we want
    ck_assert_int_eq(fs_ops.mkdir("/del-dirs", 0777), 0);
    ck_assert_int_eq(fs_ops.mkdir("/del-dirs/del-dir1", 0777), 0);
    ck_assert_int_eq(fs_ops.mkdir("/del-dirs/del-dir2", 0777), 0);
    ck_assert_int_eq(fs_ops.mkdir("/del-dirs/del-dir1/del-dir11", 0777), 0);
    ck_assert_int_eq(fs_ops.mkdir("/del-dirs/del-dir2/del-dir21", 0777), 0);

    // and add a file to keep things honest
    ck_assert_int_eq(fs_ops.create("/del-dirs/del-dir1/file1.fil", MY_S_IFREG | 0777, NULL), 0);

    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 11);

    // iterate over the newly created directories - to be sure they're there
    struct dir_test_data *test_tables[] = {root_table, dir1_table, dir2_table};
    char *test_names[] = {"/del-dirs", "/del-dirs/del-dir1", "/del-dirs/del-dir2"};

    for (int t = 0; t < 3; t++)
    {
        validate_directory(test_names[t], test_tables[t]);
    }
    // so the directories are set up correctly - test rmdir

    // test the negative cases first
    ck_assert_int_eq(fs_ops.rmdir("/del-dirs/del-dir1/dir2/dir3"), -ENOENT);
    ck_assert_int_eq(fs_ops.rmdir("/del-dirs/del-dir1/file1.fil/dir3"), -ENOTDIR);
    ck_assert_int_eq(fs_ops.rmdir("/del-dirs/del-dir1/dir2"), -ENOENT);
    ck_assert_int_eq(fs_ops.rmdir("/del-dirs/del-dir1/file1.fil"), -ENOTDIR);
    ck_assert_int_eq(fs_ops.rmdir("/del-dirs/del-dir1"), -ENOTEMPTY);

    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 11);

    ck_assert_int_eq(fs_ops.unlink("/del-dirs/del-dir1/file1.fil"), 0);
    ck_assert_int_eq(fs_ops.rmdir("/del-dirs/del-dir2/del-dir21"), 0);
    ck_assert_int_eq(fs_ops.rmdir("/del-dirs/del-dir1/del-dir11"), 0);
    ck_assert_int_eq(fs_ops.rmdir("/del-dirs/del-dir2"), 0);
    ck_assert_int_eq(fs_ops.rmdir("/del-dirs/del-dir1"), 0);

    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 2);

    // if this returns, the dir was found
    ck_assert_int_eq(fs_ops.readdir("/del-dirs", empty_del_root_table, test_dir, 0, NULL), 0);
    // the given table was empty, this confirms nothing was in the directory
    ck_assert_int_eq(empty_del_root_table[0].seen, 0); // make sure there were no unexpected entries

    ck_assert_int_eq(fs_ops.rmdir("/del-dirs"), 0);
    // if this returns, the root dir was found
    ck_assert_int_eq(fs_ops.readdir("/", empty_del_root_table, test_dir, 0, NULL), 0);
    // table was empty, this confirms the dir was deleted nothing was in the directory
    ck_assert_int_eq(empty_del_root_table[0].seen, 0); // make sure there were no unexpected entries

    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks);
}
END_TEST

START_TEST(write_empty_test)
{
    struct statvfs fsstats;
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    int free_blocks = fsstats.f_bavail;

    // set up the dir structure we want
    ck_assert_int_eq(fs_ops.mkdir("/create-dirs", 0777), 0);
    ck_assert_int_eq(fs_ops.mkdir("/create-dirs/dir1", 0777), 0);
    ck_assert_int_eq(fs_ops.mkdir("/create-dirs/dir2", 0777), 0);
    ck_assert_int_eq(fs_ops.create("/create-dirs/dir1/file1.fil", MY_S_IFREG | 0777, NULL), 0);

    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 7);

    // test the negative cases first
    ck_assert_int_eq(fs_ops.create("/create-dirs/dir1/dir2/dir3", MY_S_IFREG | 0777, NULL), -ENOENT);
    ck_assert_int_eq(fs_ops.create("/create-dirs/dir1/file1.fil/dir3", MY_S_IFREG | 0777, NULL), -ENOTDIR);
    ck_assert_int_eq(fs_ops.create("/create-dirs/dir1/file1.fil", MY_S_IFREG | 0777, NULL), -EEXIST);
    ck_assert_int_eq(fs_ops.create("/create-dirs/dir1", MY_S_IFREG | 0777, NULL), -EEXIST);
    ck_assert_int_eq(fs_ops.create("/create-dirs/dir1/this-filename-is-way-too-long", MY_S_IFREG | 0777, NULL), -EINVAL);

    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 7);

    // regular tests
    struct dir_test_data root_table[] = {
        {"create-dirs", 0, 1, 0},
        {"", 0, 0, 0}};
    struct dir_test_data create_root_table[] = {
        {"dir1", 0, 1, 0},
        {"dir2", 0, 1, 0},
        {"file5.fil", 0, 0, 1},
        {"file6.fil", 0, 0, 1},
        {"", 0, 0, 0}};
    struct dir_test_data dir1_table[] = {
        {"file1.fil", 0, 0, 1},
        {"file2.fil", 0, 0, 1},
        {"", 0, 0, 0}};
    struct dir_test_data dir2_table[] = {
        {"file3.fil", 0, 0, 1},
        {"file4.fil", 0, 0, 1},
        {"", 0, 0, 0}};

    ck_assert_int_eq(fs_ops.create("/create-dirs/dir1/file2.fil", MY_S_IFREG | 0777, NULL), 0);
    ck_assert_int_eq(fs_ops.create("/create-dirs/dir2/file3.fil", MY_S_IFREG | 0777, NULL), 0);
    ck_assert_int_eq(fs_ops.create("/create-dirs/dir2/file4.fil", MY_S_IFREG | 0777, NULL), 0);
    ck_assert_int_eq(fs_ops.create("/create-dirs/file5.fil", MY_S_IFREG | 0777, NULL), 0);
    ck_assert_int_eq(fs_ops.create("/create-dirs/file6.fil", MY_S_IFREG | 0777, NULL), 0);

    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 12);

    // iterate over the newly created directories - to be sure they're there
    struct dir_test_data *test_tables[] = {root_table, create_root_table, dir1_table, dir2_table};
    char *test_names[] = {"/", "/create-dirs", "/create-dirs/dir1", "/create-dirs/dir2"};

    for (int t = 0; t < 4; t++)
    {
        validate_directory(test_names[t], test_tables[t]);
    }

    // cleanup
    ck_assert_int_eq(fs_ops.unlink("/create-dirs/dir1/file1.fil"), 0);
    ck_assert_int_eq(fs_ops.unlink("/create-dirs/dir1/file2.fil"), 0);
    ck_assert_int_eq(fs_ops.unlink("/create-dirs/dir2/file3.fil"), 0);
    ck_assert_int_eq(fs_ops.unlink("/create-dirs/dir2/file4.fil"), 0);
    ck_assert_int_eq(fs_ops.unlink("/create-dirs/file5.fil"), 0);
    ck_assert_int_eq(fs_ops.unlink("/create-dirs/file6.fil"), 0);
    ck_assert_int_eq(fs_ops.rmdir("/create-dirs/dir2"), 0);
    ck_assert_int_eq(fs_ops.rmdir("/create-dirs/dir1"), 0);
    ck_assert_int_eq(fs_ops.rmdir("/create-dirs"), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks);
}
END_TEST

START_TEST(write_append_test)
{
    int block_size = 4096;
    int data_repeat_seed = 119;
    int incr[] = {17, 100, 1000, 1024, 1970, 3000, -1};
    int file_size[] = {block_size / 2, block_size, block_size * 1.5, block_size * 2, block_size * 2.5, block_size * 3.0, -1};

    struct stat filestat;
    struct statvfs fsstats;
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    int free_blocks = fsstats.f_bavail;

    char *fn = "/append-dir/file1.fil";

    // set up the dir structure we want
    ck_assert_int_eq(fs_ops.mkdir("/append-dir", 0777), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 2);

    // negative write tests here
    // create an empty file for testing
    char test_buffer[100];
    init_test_data(test_buffer, 100, 100, 100);
    ck_assert_int_eq(fs_ops.create(fn, MY_S_IFREG | 0777, NULL), 0);
    ck_assert_int_eq(fs_ops.write("/xxx", test_buffer, 100, 0, NULL), -ENOENT);
    ck_assert_int_eq(fs_ops.write("/append-dir", test_buffer, 100, 0, NULL), -EISDIR);
    ck_assert_int_eq(fs_ops.write(fn, test_buffer, 100, 100, NULL), -EINVAL);
    ck_assert_int_eq(fs_ops.unlink(fn), 0); // clean up after yourself, and verify disk is ok
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 2);

    // and on to the real tests - iterate over file size and incremental write size
    // create a file, fill it up (by appending one block after another), then validate that it was written correctly, then remove the file
    for (int *fs = file_size; *fs > 0; fs++)
    {

        // append data up to this file size, in various increments
        for (int *is = incr; *is > 0; is++)
        {
            // create an empty file
            ck_assert_int_eq(fs_ops.create(fn, MY_S_IFREG | 0777, NULL), 0);
            ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
            ck_assert_int_eq(fsstats.f_bavail, free_blocks - 3);

            char src_buffer[*is];
            int act_seed = (*is < data_repeat_seed ? *is : data_repeat_seed);
            init_test_data(src_buffer, *is, act_seed, -1);

            for (int cur_len = 0; cur_len < *fs; cur_len += *is)
            {
                int act_len = (cur_len + *is > *fs ? *fs - cur_len : *is);
                ck_assert_int_eq(fs_ops.write(fn, src_buffer, act_len, cur_len, NULL), 0);
            }

            ck_assert_int_eq(fs_ops.getattr(fn, &filestat), 0);
            ck_assert_int_eq(filestat.st_size, *fs);
            int data_blocks = (*fs - 1) / block_size + 1;
            //printf( "fs append:%d incr:%d blk:%d, ok!\n", *fs, *is, data_blocks );
            ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
            ck_assert_int_eq(fsstats.f_bavail, free_blocks - 3 - data_blocks);

            // read and validate the file just written
            char ref_buffer[*fs];
            init_test_data(ref_buffer, *fs, act_seed, *is);
            char read_buffer[*fs];
            ck_assert_int_eq(fs_ops.read(fn, read_buffer, *fs, 0, NULL), *fs);

            for (int i = 0; 0 && i < 20; i++)
            {
                printf("%02d:%03d:%03d ", i, (int)ref_buffer[i], (int)read_buffer[i]);
            }
            //printf("\n");

            ck_assert_int_eq(memcmp(read_buffer, ref_buffer, *fs), 0);

            // cleanup for next run
            ck_assert_int_eq(fs_ops.unlink(fn), 0);
            ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
            ck_assert_int_eq(fsstats.f_bavail, free_blocks - 2);
        }
    }

    ck_assert_int_eq(fs_ops.rmdir("/append-dir"), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks);
}
END_TEST

START_TEST(write_overwrite_test)
{
    int block_size = 4096;
    int data_repeat_seed = 119;
    int incr[] = {17, 100, 1000, 1024, 1970, 3000, -1};
    int file_size[] = {block_size / 2, block_size, block_size * 1.5, block_size * 2, block_size * 2.5, block_size * 3.0, -1};

    struct stat filestat;
    struct statvfs fsstats;
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    int free_blocks = fsstats.f_bavail;

    char *fn = "/append-dir/file1.fil";

    // set up the dir structure we want
    ck_assert_int_eq(fs_ops.mkdir("/append-dir", 0777), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 2);

    // negative tests for write are in write_append_test

    // and on to the real tests - iterate over file size and incremental write size
    // create a file, fill it up (overwrite by starting the new write 10 bytes after the last), ok since smallest write block size tested is 17 - no holes :)
    // then validate that it was written correctly, then remove the file
    for (int *fs = file_size; *fs > 0; fs++)
    {

        // add data data up to this file size, in various increments
        for (int *is = incr; *is > 0; is++)
        {
            // create an empty file
            ck_assert_int_eq(fs_ops.create(fn, MY_S_IFREG | 0777, NULL), 0);
            ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
            ck_assert_int_eq(fsstats.f_bavail, free_blocks - 3);

            char src_buffer[*is];
            int act_seed = (*is < data_repeat_seed ? *is : data_repeat_seed);
            init_test_data(src_buffer, *is, act_seed, -1);

            for (int cur_len = 0; cur_len < *fs; cur_len += 10)
            {
                int act_len = (cur_len + *is > *fs ? *fs - cur_len : *is);
                ck_assert_int_eq(fs_ops.write(fn, src_buffer, act_len, cur_len, NULL), 0);
            }

            ck_assert_int_eq(fs_ops.getattr(fn, &filestat), 0);
            ck_assert_int_eq(filestat.st_size, *fs);
            int data_blocks = (*fs - 1) / block_size + 1;
            //printf( "fs overwrite:%d incr:%d blk:%d, ok!\n", *fs, *is, data_blocks );
            ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
            ck_assert_int_eq(fsstats.f_bavail, free_blocks - 3 - data_blocks);

            // read and validate the file just written
            char ref_buffer[*fs];
            init_test_data(ref_buffer, *fs, act_seed, 10);
            char read_buffer[*fs];
            ck_assert_int_eq(fs_ops.read(fn, read_buffer, *fs, 0, NULL), *fs);

            for (int i = 0; 0 && i < 20; i++)
            {
                printf("%02d:%03d:%03d ", i, (int)ref_buffer[i], (int)read_buffer[i]);
            }
            //printf("\n");

            ck_assert_int_eq(memcmp(read_buffer, ref_buffer, *fs), 0);

            // cleanup for next run
            ck_assert_int_eq(fs_ops.unlink(fn), 0);
            ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
            ck_assert_int_eq(fsstats.f_bavail, free_blocks - 2);
        }
    }

    ck_assert_int_eq(fs_ops.rmdir("/append-dir"), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks);
}
END_TEST

START_TEST(write_truncate_test)
{
    int block_size = 4096;
    int data_repeat_seed = 119;
    int incr[] = {100, -1};
    int file_size[] = {block_size / 2, block_size, block_size * 1.5, block_size * 2, block_size * 2.5, block_size * 3.0, -1};

    struct stat filestat;
    struct statvfs fsstats;
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    int free_blocks = fsstats.f_bavail;

    char *fn = "/truncate-dir/file1.fil";

    // set up the dir structure we want
    ck_assert_int_eq(fs_ops.mkdir("/truncate-dir", 0777), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 2);

    // negative write tests here
    ck_assert_int_eq(fs_ops.create(fn, MY_S_IFREG | 0777, NULL), 0);
    ck_assert_int_eq(fs_ops.truncate("/xxx", 0), -ENOENT);
    ck_assert_int_eq(fs_ops.truncate("/truncate-dir", 0), -EISDIR);
    ck_assert_int_eq(fs_ops.truncate(fn, -10), -EINVAL);
    ck_assert_int_eq(fs_ops.unlink(fn), 0); // clean up after yourself, and verify disk is ok
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks - 2);

    // on to real tests - use append and one write block increment (although the struucture allows more than 1) to create variouus file sizes
    // create / write to file, validate the blocks allocated, truncate, validate size / blocks freed, then delete the file
    for (int *fs = file_size; *fs > 0; fs++)
    {

        // append data up to this file size, in various increments
        for (int *is = incr; *is > 0; is++)
        {
            // create an empty file
            ck_assert_int_eq(fs_ops.create(fn, MY_S_IFREG | 0777, NULL), 0);
            ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
            ck_assert_int_eq(fsstats.f_bavail, free_blocks - 3);

            char src_buffer[*is];
            int act_seed = (*is < data_repeat_seed ? *is : data_repeat_seed);
            init_test_data(src_buffer, *is, act_seed, -1);

            for (int cur_len = 0; cur_len < *fs; cur_len += *is)
            {
                int act_len = (cur_len + *is > *fs ? *fs - cur_len : *is);
                ck_assert_int_eq(fs_ops.write(fn, src_buffer, act_len, cur_len, NULL), 0);
            }

            // verify that the right number of blocks are used
            ck_assert_int_eq(fs_ops.getattr(fn, &filestat), 0);
            ck_assert_int_eq(filestat.st_size, *fs);
            int data_blocks = (*fs - 1) / block_size + 1;
            //printf( "fs overwrite:%d incr:%d blk:%d, ok!\n", *fs, *is, data_blocks );
            ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
            ck_assert_int_eq(fsstats.f_bavail, free_blocks - 3 - data_blocks);

            ck_assert_int_eq(fs_ops.truncate(fn, 0), 0);
            ck_assert_int_eq(fs_ops.getattr(fn, &filestat), 0);
            ck_assert_int_eq(filestat.st_size, 0);
            ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
            ck_assert_int_eq(fsstats.f_bavail, free_blocks - 3);

            // cleanup for next run
            ck_assert_int_eq(fs_ops.unlink(fn), 0);
            ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
            ck_assert_int_eq(fsstats.f_bavail, free_blocks - 2);
        }
    }

    // you should always clean up after yourself...
    ck_assert_int_eq(fs_ops.rmdir("/truncate-dir"), 0);
    ck_assert_int_eq(fs_ops.statfs("/", &fsstats), 0);
    ck_assert_int_eq(fsstats.f_bavail, free_blocks);
}
END_TEST

int main(int argc, char **argv)
{
    system("python2 gen-disk.py -q disk2.in test2.img");
    block_init("test2.img");
    fs_ops.init(NULL);

    Suite *s = suite_create("fs5600");
    TCase *tc = tcase_create("write_mostly");

    /* add more tests here */
    tcase_add_test(tc, create_subdir_test); /* in root, in sub, and in sub/sub */
    tcase_add_test(tc, delete_subdir_test);           /* these also require the ability to create/unlink a file */
    // tcase_add_test(tc, write_empty_test);             /* as above, but include files with data written to them */
    // tcase_add_test(tc, write_append_test);            /* as above, ensure blocks are freed appropriately */
    // tcase_add_test(tc, write_overwrite_test);
    // tcase_add_test(tc, write_truncate_test);

    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);

    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    printf("%d tests failed\n", n_failed);

    srunner_free(sr);
    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
