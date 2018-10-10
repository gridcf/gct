/*
 * Copyright 1999-2008 University of Chicago
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

#include "globus_common.h"
#include "gssapi.h"
#include "globus_i_gsi_gss_utils.h"

#define TEST_ASSERT(assertion) if (!(assertion)) { fprintf(stderr, "%s:%d:%s %s\n", __FILE__, __LINE__, __func__, #assertion); return 1; }

static
int
malloc_error_test(void)
{
    globus_result_t                     result;
    globus_object_t                    *error;

    errno = ENOMEM;
    GLOBUS_GSI_GSSAPI_MALLOC_ERROR(&result);

    TEST_ASSERT(result != GLOBUS_SUCCESS);

    error = globus_error_get(result);

    TEST_ASSERT(error != NULL);
    TEST_ASSERT(globus_error_get_source(error) == GLOBUS_GSI_GSSAPI_MODULE);
    TEST_ASSERT(globus_error_get_type(error) ==
                GLOBUS_GSI_GSSAPI_ERROR_OUT_OF_MEMORY);

    globus_object_free(error);
    return 0;
}

static
int
gssapi_error_test(void)
{
    globus_result_t                     result;
    globus_object_t *                   error;

    GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            &result,
            GLOBUS_GSI_GSSAPI_ERROR_NO_GLOBUSID,
            ("no globusid"));

    TEST_ASSERT(result != GLOBUS_SUCCESS);

    error = globus_error_get(result);

    TEST_ASSERT(error != NULL);
    TEST_ASSERT(globus_error_get_source(error) == GLOBUS_GSI_GSSAPI_MODULE);
    TEST_ASSERT(globus_error_get_type(error) ==
                        GLOBUS_GSI_GSSAPI_ERROR_NO_GLOBUSID);

    globus_object_free(error);
    return 0;
}

static
int
error_join_chains_test(void)
{
    globus_result_t                     result1, result2;
    globus_result_t                     chain1, chain2, chain3;

    errno = ENOMEM;
    GLOBUS_GSI_GSSAPI_MALLOC_ERROR(&result1);
    TEST_ASSERT(result1 != GLOBUS_SUCCESS);

    chain1 = globus_i_gsi_gssapi_error_join_chains_result(
            result1,
            GLOBUS_SUCCESS);
    TEST_ASSERT(chain1 != GLOBUS_SUCCESS);
    result1 = GLOBUS_SUCCESS;

    TEST_ASSERT(globus_error_get_type(globus_error_peek(chain1)) ==
                        GLOBUS_GSI_GSSAPI_ERROR_CREATING_ERROR_OBJ);


    GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            &result2,
            GLOBUS_GSI_GSSAPI_ERROR_NO_GLOBUSID,
            ("no globusid"));
    TEST_ASSERT(result2 != GLOBUS_SUCCESS);

    chain2 = globus_i_gsi_gssapi_error_join_chains_result(
            GLOBUS_SUCCESS,
            result2);
    TEST_ASSERT(chain2 != GLOBUS_SUCCESS);
    result2 = GLOBUS_SUCCESS;

    TEST_ASSERT(globus_error_get_type(globus_error_peek(chain2)) ==
                GLOBUS_GSI_GSSAPI_ERROR_NO_GLOBUSID);

    errno = ENOMEM;
    GLOBUS_GSI_GSSAPI_MALLOC_ERROR(&result1);
    TEST_ASSERT(result1 != GLOBUS_SUCCESS);

    GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            &result2,
            GLOBUS_GSI_GSSAPI_ERROR_NO_GLOBUSID,
            ("no globusid"));

    chain3 = globus_i_gsi_gssapi_error_join_chains_result(
            result1,
            result2);
    result1 = result2 = GLOBUS_SUCCESS;
    TEST_ASSERT(chain3 != GLOBUS_SUCCESS);

    TEST_ASSERT(globus_error_match(
            globus_error_peek(chain3),
            GLOBUS_GSI_GSSAPI_MODULE,
            GLOBUS_GSI_GSSAPI_ERROR_OUT_OF_MEMORY));
    TEST_ASSERT(globus_error_match(
            globus_error_peek(chain3),
            GLOBUS_GSI_GSSAPI_MODULE,
            GLOBUS_GSI_GSSAPI_ERROR_NO_GLOBUSID));

    return 0;
}

struct test_case
{
    char                             *name;
    int                               (*func)(void); 
};

#define TEST_CASE(t) { .name = #t,  .func = t }

int
main(int argc, char *argv[])
{
    struct test_case                    tests[] =
    {
        TEST_CASE(malloc_error_test),
        TEST_CASE(gssapi_error_test),
        TEST_CASE(error_join_chains_test)
    };
    size_t                              test_count;
    int                                 failed = 0;
    
    test_count = sizeof(tests)/sizeof(tests[0]);

    printf("1..%zd\n", test_count);
    globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE);

    for (size_t i = 0; i < test_count; i++)
    {
        int rc = tests[i].func();
        
        printf("%s - %s\n", rc ? "not ok" : "ok", tests[i].name);

        if (rc != 0)
        {
            failed++;
        }
    }
    globus_module_deactivate(GLOBUS_GSI_GSSAPI_MODULE);

    return failed;
}
/* main() */
