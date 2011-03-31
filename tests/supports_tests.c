#include <CUnit/CUnit.h>
#include <CUnit/Console.h>

#include <sys/types.h>
#include <strings.h>

#include "supports.h"


void test_strdup_pathcat();

int
main(int argc, char** argv)
{
    CU_pSuite suite;

    CU_initialize_registry();
    suite = CU_add_suite("all", NULL, NULL);
    CU_add_test(suite, "test_001", test_strdup_pathcat);
    CU_console_run_tests();
    CU_cleanup_registry();
    return 0;
}

void
test_strdup_pathcat()
{
    char *x = NULL;
    x = strdup_pathcat("aaa", "bbb");
    CU_ASSERT(strcmp(x, "aaa/bbb") == 0);
    free(x);

    x = strdup_pathcat("aaa/", "bbb");
    CU_ASSERT(strcmp(x, "aaa/bbb") == 0);
    free(x);

    x = strdup_pathcat("aaa", "bbb/");
    CU_ASSERT(strcmp(x, "aaa/bbb/") == 0);
    free(x);

    x = strdup_pathcat("aaa/", "bbb/");
    CU_ASSERT(strcmp(x, "aaa/bbb/") == 0);
    free(x);
}
