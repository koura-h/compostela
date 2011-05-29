#include <CUnit/CUnit.h>
#include <CUnit/Console.h>

#include <sys/types.h>
#include <strings.h>
#include <stdlib.h>

#include "supports.h"
#include "azmalloc.h"

void test_malloc();

int
main(int argc, char** argv)
{
    CU_pSuite suite;

    CU_initialize_registry();
    suite = CU_add_suite("all", NULL, NULL);
    CU_add_test(suite, "test_001", test_malloc);
    CU_console_run_tests();
    CU_cleanup_registry();
    return 0;
}

void
test_malloc()
{
    char *x = NULL;
    x = az_malloc(8);
    az_release(x);
}
