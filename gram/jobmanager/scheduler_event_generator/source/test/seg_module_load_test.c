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
 * @page seg_load_test
 * @brief SEG Load Test
 *
 * Test that the SEG 
 * - loads the appropriate module when the
 *   globus_scheduler_event_generator_load_module() function is called.
 * - fails reasonably when the module cannot be loaded.
 */

#include "globus_common.h"
#include "globus_scheduler_event_generator.h"
#include "globus_scheduler_event_generator_app.h"

int main(int argc, char *argv[])
{
    int                                 rc;
    globus_result_t                     result;
    char                                modname[] =
                                        "load_test_module";
    char *                              globus_loc = NULL;
    int                                 notok=3;

    printf("1..3\n");

    rc = globus_module_activate(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_location(&globus_loc);
    if (result != GLOBUS_SUCCESS)
    {
        rc = 1;
        goto error;
    }

    result = globus_scheduler_event_generator_load_module(modname);

    if (result != GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Load %s returned %s\n", modname, globus_error_print_friendly(globus_error_peek(result)));
        rc = 1;
        goto deactivate_error;
    }
    notok--;
    globus_module_deactivate_all();
    notok--;
    rc = globus_module_activate(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_scheduler_event_generator_load_module("bogus");

    if (result == GLOBUS_SUCCESS)
    {
        rc = 1;
        goto deactivate_error;
    }
    printf("ok - module load\n");
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
