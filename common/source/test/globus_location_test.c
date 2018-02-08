/*
 * Copyright 1999-2016 University of Chicago
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file globus_location_test.c
 * @brief Test the functionality of globus_location()
 */

#include "globus_common.h"
#include "globus_test_tap.h"

typedef void (*globus_test_func_t)(void);
typedef struct
{
    globus_test_func_t                  test;
    const char                         *name;
}
globus_test_case_t;

#define TEST(x) { x, #x }

void
globus_location_test(void)
{
    char                               *globus_location_macro = GLOBUS_LOCATION;
    char                               *globus_location_val = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_location(&globus_location_val);

    ok (result == GLOBUS_SUCCESS
        && strcmp(globus_location_val,
        globus_location_macro) == 0, "globus_location_test");
}

void
globus_location_reset_test(void)
{
    char                               *globus_location_new = "AD296085-0105-429A-B07A-A314D2F88FAD";
    char                               *globus_location_val = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    globus_libc_setenv("GLOBUS_LOCATION", globus_location_new, 1);

    result = globus_location(&globus_location_val);

    ok (result == GLOBUS_SUCCESS
        && strcmp(globus_location_val,
        globus_location_new) == 0, "globus_location_reset_test");
}

int 
main(int argc, char *argv[])
{
    globus_test_case_t                  tests[] =
    {
        TEST(globus_location_test),
        TEST(globus_location_reset_test)
    };
    size_t                              num_tests = sizeof(tests)/sizeof(tests[0]);
    globus_libc_unsetenv("GLOBUS_LOCATION");
    globus_module_activate(GLOBUS_COMMON_MODULE);
    printf("1..%zu\n", num_tests);

    for (size_t i = 0; i < num_tests; i++)
    {
        tests[i].test();
    }
    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return TEST_EXIT_CODE;
}
