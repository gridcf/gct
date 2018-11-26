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
 * @page seg_timestamp_test
 * @brief SEG Timestamp Test
 *
 * Test that the SEG passes a timestamp to the module when requested to do so.
 */

#include "globus_common.h"
#include "globus_scheduler_event_generator.h"
#include "globus_scheduler_event_generator_app.h"

int main(int argc, char *argv[])
{
    int                                 rc;
    globus_result_t                     result;
    char                                modname[] =
                                        "timestamp_test_module";
    time_t                              test_stamp=42;
    char                                test_stamp_str[] = "42";
    char *                              globus_loc = NULL;
    int                                 notok=2;

    printf("1..2\n");

    rc = globus_module_activate(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_location(&globus_loc);
    if (result != GLOBUS_SUCCESS)
    {
        rc = 1;
        goto deactivate_error;
    }

    globus_libc_setenv("TEST_MODULE_TIMESTAMP", "0", GLOBUS_TRUE);

    result = globus_scheduler_event_generator_load_module(modname);

    if (result != GLOBUS_SUCCESS)
    {
        rc = 1;
        fprintf(stderr, "load_module failed: %s\n", globus_object_printable_to_string(globus_error_peek(result)));
        goto deactivate_error;
    }
    notok--;
    globus_module_deactivate_all();
    rc = globus_module_activate(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_scheduler_event_generator_set_timestamp(test_stamp);
    if (result != GLOBUS_SUCCESS)
    {
        rc = 1;
        goto deactivate_error;
    }
    globus_libc_setenv("TEST_MODULE_TIMESTAMP", test_stamp_str, GLOBUS_TRUE);
    result = globus_scheduler_event_generator_load_module(modname);

    if (result != GLOBUS_SUCCESS)
    {
        rc = 1;
        fprintf(stderr, "load_module failed: %s\n", globus_object_printable_to_string(globus_error_peek(result)));
        goto deactivate_error;
    }
    notok--;

deactivate_error:
    globus_module_deactivate_all();
error:
    if (rc != 0)
    {
        while (notok-- > 0)
        {
            printf("not ok\n");
        }
    }
    return rc;
}
/* main() */
