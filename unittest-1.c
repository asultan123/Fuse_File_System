/*
 * file:        testing.c
 * description: libcheck test skeleton for file system project
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

extern struct fuse_operations fs_ops;
extern void block_init(char *file);

struct attr_test_data
{
    char *path;
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    uint32_t size;
    uint32_t ctime;
    uint32_t mtime;
};

struct attr_test_data attr_data[] = {
    {"/", 0, 0, 040777, 4096, 1565283152, 1565283167},
    {"/file.1k", 500, 500, 0100666, 1000, 1565283152, 1565283152},
    {"/file.10", 500, 500, 0100666, 10, 1565283152, 1565283167},
    {"/dir-with-long-name", 0, 0, 040777, 4096, 1565283152, 1565283167},
    {"/dir-with-long-name/file.12k+", 0, 500, 0100666, 12289, 1565283152, 1565283167},
    {"/dir2", 500, 500, 040777, 8192, 1565283152, 1565283167},
    {"/dir2/twenty-seven-byte-file-name", 500, 500, 0100666, 1000, 1565283152, 1565283167},
    {"/dir2/file.4k+", 500, 500, 0100777, 4098, 1565283152, 1565283167},
    {"/dir3", 0, 500, 040777, 4096, 1565283152, 1565283167},
    {"/dir3/subdir", 0, 500, 040777, 4096, 1565283152, 1565283167},
    {"/dir3/subdir/file.4k-", 500, 500, 0100666, 4095, 1565283152, 1565283167},
    {"/dir3/subdir/file.8k-", 500, 500, 0100666, 8190, 1565283152, 1565283167},
    {"/dir3/subdir/file.12k", 500, 500, 0100666, 12288, 1565283152, 1565283167},
    {"/dir3/file.12k-", 0, 500, 0100777, 12287, 1565283152, 1565283167},
    {"/file.12k+", 0, 500, 0100666, 12289, 1565283152, 1565283167},
    {"/file.8k+", 500, 500, 0100666, 8195, 1565283152, 1565283167},
    {"", 0, 0, 0, 0, 0, 0},
};

struct dir_test_data
{
    char *name;
    int seen;
    int isDir;
    int wasDir;
};
struct dir_test_data root_table[] = {
    {"dir2", 0, 1, 0},
    {"dir3", 0, 1, 0},
    {"dir-with-long-name", 0, 1, 0},
    {"file.1k", 0, 0, 1},
    {"file.10", 0, 0, 1},
    {"file.12k+", 0, 0, 1},
    {"file.8k+", 0, 0, 1},
    {"", 0, 0, 0}};
struct dir_test_data dir2_table[] = {
    {"twenty-seven-byte-file-name", 0, 0, 1},
    {"file.4k+", 0, 0, 1},
    {"", 0, 0, 0}};
struct dir_test_data dir3_table[] = {
    {"subdir", 0, 1, 0},
    {"file.12k-", 0, 0, 1},
    {"", 0, 0, 0}};
struct dir_test_data dirlongname_table[] = {
    {"file.12k+", 0, 0, 1},
    {"", 0, 0, 0}};
struct dir_test_data subdir_table[] = {
    {"file.4k-", 0, 0, 1},
    {"file.8k-", 0, 0, 1},
    {"file.12k", 0, 0, 1},
    {"", 0, 0, 0}};

struct file_test_data
{
    unsigned checksum;
    int length;
    char *path;
};

struct file_test_data file_data_table[] = {
    {1786485602, 1000, "/file.1k"},
    {855202508, 10, "/file.10"},
    {4101348955, 12289, "/dir-with-long-name/file.12k+"},
    {2575367502, 1000, "/dir2/twenty-seven-byte-file-name"},
    {799580753, 4098, "/dir2/file.4k+"},
    {4220582896, 4095, "/dir3/subdir/file.4k-"},
    {4090922556, 8190, "/dir3/subdir/file.8k-"},
    {3243963207, 12288, "/dir3/subdir/file.12k"},
    {2954788945, 12287, "/dir3/file.12k-"},
    {4101348955, 12289, "/file.12k+"},
    {2112223143, 8195, "/file.8k+"},
    {0, 0, ""}};

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

START_TEST(bad_attr_tests)
{
    struct stat fileStat;

    ck_assert_int_eq(fs_ops.getattr("/not-a-file", &fileStat), -ENOENT);
    ck_assert_int_eq(fs_ops.getattr("/file.1k/file.0", &fileStat), -ENOTDIR);
    ck_assert_int_eq(fs_ops.getattr("/not-a-dir/file.0", &fileStat), -ENOENT);
    ck_assert_int_eq(fs_ops.getattr("/dir2/not-a-file", &fileStat), -ENOENT);
}
END_TEST

START_TEST(attr_test)
{
    for (struct attr_test_data *s = attr_data; s->path[0] != '\0'; s++)
    {
        struct stat fileStat;
        ck_assert_int_eq(fs_ops.getattr(s->path, &fileStat), 0);
        ck_assert_int_eq(s->mode, fileStat.st_mode);
        ck_assert_int_eq(s->uid, fileStat.st_uid);
        ck_assert_int_eq(s->gid, fileStat.st_gid);
        ck_assert_int_eq(s->size, fileStat.st_size);
        ck_assert_int_eq(s->ctime, fileStat.st_ctime);
        ck_assert_int_eq(s->mtime, fileStat.st_mtime);
    }
}
END_TEST

START_TEST(bad_dir_tests)
{
    int inode = fs_ops.readdir("/fred/abc/def/file.txt", NULL, NULL, 0, NULL);
    ck_assert_int_eq(inode, -ENOENT);

    inode = fs_ops.readdir("/dir3/subdir/file.12k/something else", NULL, NULL, 0, NULL);
    ck_assert_int_eq(inode, -ENOTDIR);

    inode = fs_ops.readdir("/dir3/subdir/something else", NULL, NULL, 0, NULL);
    ck_assert_int_eq(inode, -ENOENT);

    inode = fs_ops.readdir("/dir3/subdir/file.12k", NULL, NULL, 0, NULL);
    ck_assert_int_eq(inode, -ENOTDIR);
}
END_TEST

START_TEST(dir_test)
{
    struct dir_test_data *test_tables[] = {root_table, dir2_table, dir3_table, dirlongname_table, subdir_table};
    char *test_names[] = {"/", "/dir2", "/dir3", "/dir-with-long-name", "/dir3/subdir"};

    for (int t = 0; t < 5; t++)
    {
        int inode = fs_ops.readdir(test_names[t], test_tables[t], test_dir, 0, NULL);
        ck_assert_int_eq(inode, 0);
        int i = 0;
        for (; test_tables[t][i].name[0] != '\0'; i++)
        {
            ck_assert(test_tables[t][i].seen && test_tables[t][i].isDir == test_tables[t][i].wasDir);
        }
        ck_assert_int_eq(test_tables[t][i].seen, 0); // make sure there were no unexpected entries
    }
}
END_TEST

char file_bfr[20000];

START_TEST(big_read_tests)
{
    for (struct file_test_data *f = file_data_table; f->path[0] != '\0'; f++)
    {
        int bytes_read = fs_ops.read(f->path, file_bfr, 20000, 0, NULL);
        unsigned cksum = crc32(0, (unsigned char *)file_bfr, bytes_read);

        ck_assert_int_eq(f->length, bytes_read);
        ck_assert_int_eq(f->checksum, cksum);
    }
}
END_TEST

START_TEST(small_read_tests)
{
    int incr[] = {17, 100, 1000, 1024, 1970, 3000, -1};

    for (int *inc = incr; *inc > 0; inc++) 
    {
        for (struct file_test_data *f = file_data_table; f->path[0] != '\0'; f++)
        {
            int total_bytes_read = 0;
            for (int this_loc = 0; this_loc < f->length; this_loc += *inc)
            {
                int bytes_read = fs_ops.read(f->path, &(file_bfr[this_loc]), *inc, this_loc, NULL);
                total_bytes_read += bytes_read;
            }
            unsigned cksum = crc32(0, (unsigned char *)file_bfr, total_bytes_read);

            ck_assert_int_eq(f->length, total_bytes_read);
            ck_assert_int_eq(f->checksum, cksum);
        }
    }
}
END_TEST

START_TEST(statfs_test)
{
    struct statvfs stats;
    fs_ops.statfs("/", &stats);
    ck_assert_int_eq(stats.f_bsize, 4096);
    ck_assert_int_eq(stats.f_blocks, 398);
    ck_assert_int_eq(stats.f_bfree, 355);
    ck_assert_int_eq(stats.f_namemax, 27);
}
END_TEST

START_TEST(bad_read_tests)
{
    ck_assert_int_eq( fs_ops.read( "/fred/abc/def/file.txt", file_bfr, 10, 0, NULL ), -ENOENT );
    ck_assert_int_eq( fs_ops.read( "/dir3/subdir/file.12k/something else", file_bfr, 10, 0, NULL ), -ENOTDIR );
    ck_assert_int_eq( fs_ops.read( "/dir3/subdir/something else", file_bfr, 10, 0, NULL ), -ENOENT );
    ck_assert_int_eq( fs_ops.read( "/dir3/subdir", file_bfr, 10, 0, NULL ), -EISDIR );
    ck_assert_int_eq( fs_ops.read( "/dir3/subdir/file.12k", file_bfr, 10, 1000000, NULL ), 0 );
}
END_TEST

START_TEST(chmod_test)
{
    // negative tests
    ck_assert_int_eq(fs_ops.chmod( "/dir2/dir3", 0777), -ENOENT);
    ck_assert_int_eq(fs_ops.chmod( "/dir1", 0777), -ENOENT);

    // positive tests
    struct stat fileStat;
    ck_assert_int_eq( fs_ops.getattr( "/dir2", &fileStat ), 0 );
    uint32_t mode = fileStat.st_mode;
    ck_assert( S_ISDIR(mode) );
    uint32_t newmode = mode ^ 0xFFFFFFFF;  // flips all bits, but chmod should only use the lower 9!
    ck_assert_int_eq(fs_ops.chmod( "/dir2", newmode), 0);
    ck_assert_int_eq( fs_ops.getattr( "/dir2", &fileStat ), 0 );
    uint32_t updated_mode = fileStat.st_mode;
    ck_assert( S_ISDIR(updated_mode) );
    ck_assert_int_eq( mode ^ 00777, updated_mode ); // compares by checking that only lower 9 bits actually changed
    ck_assert_int_eq( fs_ops.chmod( "/dir2", mode), 0);   // and reset it

    ck_assert_int_eq( fs_ops.getattr( "/dir2/file.4k+", &fileStat ), 0 );
    mode = fileStat.st_mode;
    ck_assert( !S_ISDIR(mode) );
    newmode = mode ^ 0xFFFFFFFF;  // flips all bits, but chmod should only use the lower 9!
    ck_assert_int_eq( fs_ops.chmod( "/dir2/file.4k+", newmode), 0);
    ck_assert_int_eq( fs_ops.getattr( "/dir2/file.4k+", &fileStat ), 0 );
    updated_mode = fileStat.st_mode;
    ck_assert( !S_ISDIR(updated_mode) );
    ck_assert_int_eq( mode ^ 00777, updated_mode ); // compares by checking that only lower 9 bits actually changed
    ck_assert_int_eq(fs_ops.chmod( "/dir2/file.4k+", mode), 0);   // and reset it
}
END_TEST

START_TEST(rename_test)
{
    // negative tests
    ck_assert_int_eq( fs_ops.rename( "/dir2", "/dir3"), -EEXIST );
    ck_assert_int_eq( fs_ops.rename( "/dir1", "/dir4"), -ENOENT );
    ck_assert_int_eq( fs_ops.rename( "/dir2/file.4k+", "/dir3/new.file.name"), -EINVAL );

    // positive tests
    ck_assert( fs_ops.read( "/dir2/file.4k+", file_bfr, 20000, 0, NULL ) > 0 );
    ck_assert_int_eq( fs_ops.rename( "/dir2/file.4k+", "/dir2/new.file.name"), 0 );
    ck_assert_int_eq( fs_ops.read( "/dir2/file.4k+", file_bfr, 20000, 0, NULL ), -ENOENT );
    ck_assert( fs_ops.read( "/dir2/new.file.name", file_bfr, 20000, 0, NULL ) > 0 );
    ck_assert_int_eq(fs_ops.rename( "/dir2", "/dirGone"), 0 );
    ck_assert_int_eq( fs_ops.read( "/dir2/new.file.name", file_bfr, 20000, 0, NULL ), -ENOENT );
    ck_assert( fs_ops.read( "/dirGone/new.file.name", file_bfr, 20000, 0, NULL ) > 0 );
    ck_assert_int_eq(fs_ops.rename( "/dirGone", "/dir2"), 0 );
    ck_assert_int_eq(fs_ops.rename( "/dir2/new.file.name", "/dir2/file.4k+"), 0 );
    ck_assert( fs_ops.read( "/dir2/file.4k+", file_bfr, 20000, 0, NULL ) > 0 );
}
END_TEST


/* note that your tests will call:
 *  fs_ops.getattr(path, struct stat *sb)
 *  fs_ops.readdir(path, NULL, filler_function, 0, NULL)
 *  fs_ops.read(path, buf, len, offset, NULL);
 *  fs_ops.statfs(path, struct statvfs *sv);
 */

int main(int argc, char **argv)
{
    system("python2 gen-disk.py -q disk1.in test.img");
    block_init("test.img");
    fs_ops.init(NULL);

    Suite *s = suite_create("fs5600");
    TCase *tc = tcase_create("read_mostly");

    tcase_add_test(tc, bad_attr_tests);   /* run fs_getattr against "bad" fs targets */
    tcase_add_test(tc, attr_test);        /* run fs_getattr to enumerate all values in reference image */
    tcase_add_test(tc, bad_dir_tests);    /* run fs_readdir against "bad" paths */
    tcase_add_test(tc, dir_test);         /* run fs_readdir to enumerate all values in reference image */
    tcase_add_test(tc, bad_read_tests);   /* read tests, all at once, validates by known cksum */
    tcase_add_test(tc, big_read_tests);   /* read tests, all at once, validates by known cksum */
    tcase_add_test(tc, small_read_tests); /* read tests, in varying block sizes, validates by known cksum */
    tcase_add_test(tc, statfs_test);      /* statvfs tests */
    tcase_add_test(tc, chmod_test);       /* chmod tests */
    tcase_add_test(tc, rename_test);       /* rename tests */

    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);

    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    printf("%d tests failed\n", n_failed);

    srunner_free(sr);

    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
