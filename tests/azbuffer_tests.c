#include <CUnit/CUnit.h>
#include <CUnit/Console.h>

#include <sys/types.h>
#include <strings.h>
#include <stdlib.h>
#include <fcntl.h>

#include "azbuffer.h"


void test_azbuffer();
void test_azbuffer_2();
void test_azbuffer_3();
void test_azbuffer_4();

int
main(int argc, char** argv)
{
    CU_pSuite suite;

    CU_initialize_registry();
    suite = CU_add_suite("all", NULL, NULL);
    CU_add_test(suite, "test_001", test_azbuffer);
    CU_add_test(suite, "test_002", test_azbuffer_2);
    CU_add_test(suite, "test_003", test_azbuffer_3);
    CU_add_test(suite, "test_004", test_azbuffer_4);
    CU_console_run_tests();
    CU_cleanup_registry();
    return 0;
}

void
test_azbuffer()
{
    char cb[2];

    az_buffer* buf = az_buffer_new(10);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 10);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 0);

    az_buffer_fetch_bytes(buf, "test", 4);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 6);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 4);

    az_buffer_read(buf, 2, cb, sizeof(cb));
    CU_ASSERT(az_buffer_unused_bytes(buf) == 6);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 2);
    CU_ASSERT(cb[0] == 't');
    CU_ASSERT(cb[1] == 'e');

    az_buffer_destroy(buf);
}

void
test_azbuffer_2()
{
    char cb[10];
    size_t n = 0;
    int ret = 0;

    az_buffer* buf = az_buffer_new(10);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 10);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 0);

    CU_ASSERT(az_buffer_fetch_bytes(buf, "tes", 3) == 3);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 7);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 3);

    memset(cb, 0, sizeof(cb));
    CU_ASSERT(az_buffer_read_line(buf, cb, sizeof(cb), &n) == 2);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 7);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 0);
    CU_ASSERT(n == 3);
    CU_ASSERT(strcmp(cb, "tes") == 0);

    az_buffer_fetch_bytes(buf, "t\naa", 4);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 3);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 4);

    memset(cb, 0, sizeof(cb));
    CU_ASSERT(az_buffer_read_line(buf, cb, sizeof(cb), &n) == 0);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 3);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 2);
    CU_ASSERT(strcmp(cb, "t\n") == 0);

    az_buffer_reset(buf);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 10);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 0);

    az_buffer_destroy(buf);
}

void
test_azbuffer_3()
{
    char cb[10];
    int f = open("./test.txt", O_RDONLY);
    CU_ASSERT(f > 0);

    az_buffer* buf = az_buffer_new(10);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 10);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 0);

    az_buffer_fetch_file(buf, f, 5);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 5);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 5);
    CU_ASSERT(strcmp(buf->cursor, "abcde") == 0);

    memset(cb, 0, sizeof(cb));
    az_buffer_read(buf, 3, cb, sizeof(cb));
    CU_ASSERT(az_buffer_unused_bytes(buf) == 5);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 2);
    CU_ASSERT(strcmp(buf->cursor, "de") == 0);
    CU_ASSERT(strcmp(cb, "abc") == 0);

    az_buffer_push_back(buf, "0123", 4);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 4);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 6);
    CU_ASSERT(strcmp(buf->cursor, "0123de") == 0);

    memset(cb, 0, sizeof(cb));
    az_buffer_read(buf, 5, cb, sizeof(cb));
    CU_ASSERT(az_buffer_unused_bytes(buf) == 4);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 1);
    CU_ASSERT(strcmp(buf->cursor, "e") == 0);
    CU_ASSERT(strcmp(cb, "0123d") == 0);

    az_buffer_push_back(buf, "0123456789", 10);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 19);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 11);
    CU_ASSERT(strcmp(buf->cursor, "0123456789e") == 0);

    az_buffer_destroy(buf);
    close(f);
}

void
test_azbuffer_4()
{
    char cb[10];
    int f = open("./test.txt", O_RDONLY);
    CU_ASSERT(f > 0);

    az_buffer* buf = az_buffer_new(10);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 10);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 0);

    az_buffer_fetch_file(buf, f, 5);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 5);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 5);
    CU_ASSERT(strcmp(buf->cursor, "abcde") == 0);

    memset(cb, 0, sizeof(cb));
    az_buffer_read(buf, 5, cb, sizeof(cb));
    CU_ASSERT(az_buffer_unused_bytes(buf) == 5);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 0);
    CU_ASSERT(strcmp(buf->cursor, "") == 0);
    CU_ASSERT(strcmp(cb, "abcde") == 0);

    az_buffer_fetch_file(buf, f, 5);
    CU_ASSERT(az_buffer_unused_bytes(buf) == 0);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 5);
    CU_ASSERT(strcmp(buf->cursor, "fghij") == 0);

    memset(cb, 0, sizeof(cb));
    az_buffer_read(buf, 5, cb, sizeof(cb));
    CU_ASSERT(az_buffer_unused_bytes(buf) == 0);
    CU_ASSERT(az_buffer_unread_bytes(buf) == 0);
    CU_ASSERT(strcmp(buf->cursor, "") == 0);
    CU_ASSERT(strncmp(buf->buffer, "abcdefghij", 10) == 0);
    CU_ASSERT(strcmp(cb, "fghij") == 0);

    az_buffer_destroy(buf);
    close(f);
}
