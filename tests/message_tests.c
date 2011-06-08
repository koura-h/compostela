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
}

void
test_message_2()
{
}
