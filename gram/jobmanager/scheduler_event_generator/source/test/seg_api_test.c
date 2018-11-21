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

#include "globus_scheduler_event_generator.h"
#include "globus_scheduler_event_generator_app.h"
#include "globus_scheduler_event_generator_stdout.h"
#include "globus_gram_protocol_constants.h"

/**
 * @page seg_api_test
 * @brief API Test
 * 
 * Test event portions of the SEG API. Passed as an argument,
 * the name of a file containing SEG messages. These are parsed and passed
 * to the event api. The standard output should be identical to the input file.
 */

int main(int argc, char *argv[])
{
    int rc;
    FILE * testfile;
    char *p;
    char line[256];
    globus_result_t result;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s testfile\n", argv[0]);

        return 1;
    }

    
    rc = globus_module_activate(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE);

    if (rc != GLOBUS_SUCCESS)
    {
        goto error;
    }
    rc = globus_module_activate(GLOBUS_SCHEDULER_EVENT_GENERATOR_STDOUT_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto deactivate_error;
    }

    testfile = fopen(argv[1], "r");
    if (testfile == NULL)
    {
        fprintf(stderr, "error openeing %s\n", argv[1]);
        goto deactivate_error;
    }

    result = globus_scheduler_event_generator_set_event_handler(
        globus_scheduler_event_generator_stdout_handler,
        NULL);
    if (result != GLOBUS_SUCCESS)
    {
        goto deactivate_error;
    }


    while ((p = fgets(line, sizeof(line)-1, testfile)) != NULL)
    {
        int protocol_msg_type;
        time_t stamp;
        char jobid[80];
        int state;
        int exit_code;

        rc = sscanf(line, "%d;%ld;%[^;];%d;%d\n", 
            &protocol_msg_type,
            &stamp,
            jobid,
            &state,
            &exit_code);
        if (rc != 5)
        {
            goto close_error;
        }

        if (protocol_msg_type != 1)
        {
            fprintf(stderr, "unknown msg type %d\n", protocol_msg_type);

            goto close_error;
        }

        switch(state)
        {
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING:
            result = globus_scheduler_event_pending(stamp, jobid);
            if (result != GLOBUS_SUCCESS)
            {
                fprintf(stderr, "Error sending PENDING notification\n");

                goto close_error;
            }
            break;

        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE:
            result = globus_scheduler_event_active(stamp, jobid);
            if (result != GLOBUS_SUCCESS)
            {
                fprintf(stderr, "Error sending ACTIVE notification\n");

                goto close_error;
            }
            break;

        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE:
            result = globus_scheduler_event_done(stamp, jobid, exit_code);
            if (result != GLOBUS_SUCCESS)
            {
                fprintf(stderr, "Error sending DONE notification\n");

                goto close_error;
            }
            break;

        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED:
            result = globus_scheduler_event_failed(stamp, jobid, exit_code);
            if (result != GLOBUS_SUCCESS)
            {
                fprintf(stderr, "Error sending FAILED notification\n");

                goto close_error;
            }
            break;

        default:
            fprintf(stderr, "unknown job state\n");
            goto close_error;
        }
    }
    fclose(testfile);
    globus_module_deactivate_all();
    return 0;

close_error:
    fclose(testfile);
deactivate_error:
    globus_module_deactivate_all();
error:
    return 1;
}
