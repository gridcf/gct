/*
 * Copyright 1999-2006 University of Chicago
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
 * @file error_test.c
 * @brief Error Object Tests
 */

#include "globus_common.h"
#include "globus_test_tap.h"

#define MAX_ERROR_NUM 33

const globus_object_type_t *
switch_type(int class)
{
    const globus_object_type_t         *type;

#define num_type(n,t) case n: type = GLOBUS_ERROR_TYPE_ ## t; break;

    switch (class)
    {
        num_type(0, BASE);
        num_type(1, NO_AUTHENTICATION);
        num_type(2, NO_CREDENTIALS);
        num_type(3, NO_TRUST);
        num_type(4, INVALID_CREDENTIALS);
        num_type(5, ACCESS_FAILED);
        num_type(6, NO_AUTHORIZATION);
        num_type(7, NOT_AVAILABLE);
        num_type(8, DEPLETED);
        num_type(9, QUOTA_DEPLETED);
        num_type(10, OFFLINE);
        num_type(11, NAME_UNKNOWN);
        num_type(12, ABORTED);
        num_type(13, USER_CANCELLED);
        num_type(14, INTERNAL_ERROR);
        num_type(15, SYSTEM_ABORTED);
        num_type(16, BAD_DATA);
        num_type(17, NULL_REFERENCE);
        num_type(18, TYPE_MISMATCH);
        num_type(19, BAD_FORMAT);
        num_type(21, OUT_OF_RANGE);
        num_type(22, TOO_LARGE);
        num_type(23, TOO_SMALL);
        num_type(24, COMMUNICATION_FAILED);
        num_type(25, UNREACHABLE);
        num_type(26, PROTOCOL_MISMATCH);
        num_type(27, PROTOCOL_VIOLATED);
        num_type(28, INVALID_USE);
        num_type(29, ALREADY_DONE);
        num_type(30, ALREADY_REGISTERED);
        num_type(31, ALREADY_CANCELLED);
        num_type(32, NOT_INITIALIZED);

    default:
        type = GLOBUS_ERROR_TYPE_BASE;
        break;
    }

    return type;
}

globus_result_t
throw_error(int class)
{
    const globus_object_type_t         *type;
    globus_object_t                    *error;

    if (class == 0)
        return GLOBUS_SUCCESS;

    type = switch_type(class);
    error = globus_object_construct(type);

    return globus_error_put(error);
}




int
main()
{
    int                                 i;

    printf("1..231\n");
    globus_module_activate(GLOBUS_COMMON_MODULE);

    for (i = 0; i <= MAX_ERROR_NUM; i++)
    {
        globus_result_t                     result;

        result = throw_error(i);
    }

    for (i = 1; i <= MAX_ERROR_NUM; i++)
    {
        globus_result_t                     result;

        result = throw_error(i);

        ok(result != GLOBUS_SUCCESS, "create_error_%d", i);
        {
            char                               *errstring;
            globus_object_t                    *error,
                                               *error2;

            error = globus_error_get(result);
            ok(error != NULL, "error_get_%d", i);
            errstring =globus_object_printable_to_string(error);
            ok(errstring != NULL, "error_printable_to_string_%d", i);
            printf("    %s\n", errstring);
            free(errstring);

            error2 = globus_object_upcast(error, GLOBUS_ERROR_TYPE_BASE);
            ok(error2 != NULL, "error_upcast_to_base_%d", i);
            errstring = globus_object_printable_to_string(error2);
            ok(errstring != NULL, "error_printable_to_string_%d_upcast", i);
            free(errstring);
            globus_object_free(error);
            error = NULL;

            error = globus_error_get(result);
            ok(error == GLOBUS_ERROR_NO_INFO, "error_get_after_get_%d", i);
            errstring = globus_object_printable_to_string(error);
            ok(errstring != NULL, "error_printable_to_string_%d_no_info", i);
            free(errstring);
            globus_object_free(error);
            error = NULL;
        }
    }

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return TEST_EXIT_CODE;
}
