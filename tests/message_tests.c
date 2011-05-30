#include <CUnit/CUnit.h>
#include <CUnit/Console.h>

#include <sys/types.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

#include "supports.h"

#include "message.h"


void test_message_1();
void test_message_2();

int
main(int argc, char** argv)
{
    CU_pSuite suite;

    CU_initialize_registry();
    suite = CU_add_suite("all", NULL, NULL);
    CU_add_test(suite, "test_001", test_message_1);
    CU_add_test(suite, "test_002", test_message_2);
    CU_console_run_tests();
    CU_cleanup_registry();
    return 0;
}

void
test_message_1()
{
    char *p = NULL;
    sc_message msg;

    msg.code = SCM_MSG_INIT;
    strcpy(msg.displayName, "hogehoge");
    msg.length = 5;
    msg.body = "abcde";

    p = sc_message_pack(&msg);
    CU_ASSERT(strcmp(p, "INIT hogehoge\r\n5\r\nabcde\r\n") == 0);
    free(p);
}

void
test_message_2()
{
    char *p[] = { "INIT hogehoge\r\n", "5\r\n", "abcde\r\n" };
    sc_message msg;

    sc_message_unpack(&msg, p);

    CU_ASSERT(msg.code == SCM_MSG_INIT);
    CU_ASSERT(strcmp(msg.displayName, "hogehoge") == 0);
    CU_ASSERT(msg.length == 5);
    CU_ASSERT(memcmp(msg.body, "abcde", msg.length) == 0);
}
