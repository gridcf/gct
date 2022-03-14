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

#include "globus_common.h"
#include "globus_gram_client.h"
#include "globus_preload.h"
#include "globus_gass_server_ez.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STDOUT_SIZE "18" /*strlen("hello\nhello again\n")*/


typedef struct
{
    char * callback_contact;
    char * job_contact;
    globus_mutex_t mutex;
    globus_cond_t cond;
    int job_status;
    int failure_code;
    int stdout_size;
}
monitor_t;

static 
void
globus_l_state_callback(void * callback_arg, char * job_contact, int state,
        int errorcode);

int main(int argc, char *argv[])
{
    char *rm_contact;
    int rc = 1;
    monitor_t monitor;
    char * gass_url;
    char * rsl;
    char * old_job_contact;
    globus_gass_transfer_listener_t listener;
    int ignore_job_status = 0;
    int ignore_failure_code = 0;

    LTDL_SET_PRELOADED_SYMBOLS();
    printf("1..1\n");

    rm_contact = getenv("CONTACT_STRING");

    if (argc == 2)
    {
        rm_contact = argv[1];
    }

    if (rm_contact == NULL)
    {
        fprintf(stderr,
                "Usage: %s RM-CONTACT\n"
                "    RM-CONTACT: resource manager contact\n",
                argv[0]);
        goto args_error;
    }

    rc = globus_module_activate(GLOBUS_GRAM_CLIENT_MODULE);

    if (rc != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
                "failure activating GLOBUS_GRAM_CLIENT_MODULE: %s\n",
                globus_gram_client_error_string(rc));
        goto activate_common_failed;
    }
    rc = globus_module_activate(GLOBUS_GASS_SERVER_EZ_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
                "failure activating GLOBUS_GASS_SERVER_EZ_MODULE: %d\n",
                rc);
        goto activate_server_ez_failed;
    }

    rc = globus_gass_server_ez_init(
            &listener,
            NULL,
            "https",
            NULL,
            GLOBUS_GASS_SERVER_EZ_WRITE_ENABLE,
            NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
                "failure inititializing gass server %d\n",
                rc);
        goto gass_server_ez_init_failed;
    }
    gass_url = globus_gass_transfer_listener_get_base_url(listener);       
    if (gass_url == NULL)
    {
        fprintf(stderr,
                "failure getting gass url\n");

        goto gass_server_get_url_failed;
    }

    globus_mutex_init(&monitor.mutex, NULL);
    globus_cond_init(&monitor.cond, NULL);
    monitor.job_contact = NULL;
    monitor.callback_contact = NULL;

    rc = globus_gram_client_callback_allow(
            globus_l_state_callback,
            &monitor,
            &monitor.callback_contact);

    if (rc != GLOBUS_SUCCESS || monitor.callback_contact == NULL)
    {
        fprintf(stderr,
                "failure allowing callbacks\n");
        rc = -1;
        goto allow_callback_failed;
    }
    rsl = globus_common_create_string(
            "&(executable=/bin/sh)"
            "(rsl_substitution = (GASS https://bad.example.org))"
            "(arguments=-c 'echo hello')"
            "(file_stage_out= (/etc/group $(GASS)/dev/null))"
            "(save_state = yes)"
            "(two_phase=60)");
    if (rsl == NULL)
    {
        fprintf(stderr, "Error creating rsl\n");
        goto malloc_rsl_failed;
    }

    globus_mutex_lock(&monitor.mutex);
    rc = globus_gram_client_job_request(
            rm_contact,
            rsl,
            GLOBUS_GRAM_PROTOCOL_JOB_STATE_ALL,
            monitor.callback_contact,
            &monitor.job_contact);
    fprintf(stderr, "job request result %d\n", rc);

    if (rc != GLOBUS_GRAM_PROTOCOL_ERROR_WAITING_FOR_COMMIT)
    {
        if (rc == GLOBUS_SUCCESS)
        {
            fprintf(stderr,
                    "job manager did not return "
                    "GLOBUS_GRAM_PROTOCOL_ERROR_WAITING_FOR_COMMIT\n");
            rc = -1;
        }
        else
        {
            fprintf(stderr,
                    "failure submitting job request [%d]: %s\n",
                    rc,
                    globus_gram_client_error_string(rc));
        }

        goto job_request_failed;
    }
    rc = 0;

    rc = globus_gram_client_job_signal(
            monitor.job_contact,
            GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_COMMIT_REQUEST,
            NULL,
            &ignore_job_status,
            &ignore_failure_code);
    fprintf(stderr, "job signal result %d\n", rc);

    if (rc != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
                "failure sending commit signal: %s\n",
                globus_gram_client_error_string(rc));
        goto commit_request_failed;
    }
    while (monitor.job_status != GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE &&
           monitor.job_status != GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }

    if (!(monitor.job_status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED &&
         monitor.failure_code == GLOBUS_GRAM_PROTOCOL_ERROR_STAGE_OUT_FAILED))
    {
        fprintf(stderr,
            "expected job to be failed with stage out failed\n");
        goto unexpected_job_state;
    }

    rc = globus_gram_client_job_signal(
        monitor.job_contact,
        GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_STOP_MANAGER,
        NULL,
        &monitor.job_status,
        &monitor.failure_code);
    fprintf(stderr, "stop manager result %d\n", rc);

    if (rc != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
                "Got %d trying to stop manager (status=%d, failure_code=%d)\n",
                rc,
                monitor.job_status, monitor.failure_code);
        goto unexpected_job_state;
    }

    free(rsl);
    old_job_contact = monitor.job_contact;
    monitor.job_contact = NULL;
    monitor.job_status = 0;
    monitor.failure_code = 0;
    rsl = globus_common_create_string(
            "&(restart=%s)"
            "(rsl_substitution = (GASS \"%s\"))",
            old_job_contact,
            gass_url);

    rc = globus_gram_client_job_request(
            rm_contact,
            rsl,
            GLOBUS_GRAM_PROTOCOL_JOB_STATE_ALL,
            monitor.callback_contact,
            &monitor.job_contact);
    fprintf(stderr, "restart request result %d\n", rc);

    if (rc != GLOBUS_GRAM_PROTOCOL_ERROR_WAITING_FOR_COMMIT)
    {
        if (rc == GLOBUS_SUCCESS)
        {
            fprintf(stderr,
                    "job manager did not return "
                    "GLOBUS_GRAM_PROTOCOL_ERROR_WAITING_FOR_COMMIT\n");
            rc = -1;
        }
        else
        {
            fprintf(stderr,
                    "failure submitting job request [%d]: %s\n",
                    rc,
                    globus_gram_client_error_string(rc));
        }

        goto job_request_failed;
    }
    rc = 0;

    monitor.job_status = 0;
    monitor.failure_code = 0;
    rc = globus_gram_client_job_signal(
            monitor.job_contact,
            GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_COMMIT_REQUEST,
            NULL,
            &ignore_job_status,
            &ignore_failure_code);
    fprintf(stderr, "commit request result %d\n", rc);

    if (rc != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
                "failure sending commit signal: %s\n",
                globus_gram_client_error_string(rc));
        goto commit_request_failed;
    }

    while (monitor.job_status != GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE &&
           monitor.job_status != GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }
    printf("monitor.job_status=%d\nmonitor.failure_code=%d\n",
            monitor.job_status, monitor.failure_code);
    rc = globus_gram_client_job_signal(
            monitor.job_contact,
            GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_COMMIT_END,
            NULL,
            &ignore_job_status,
            &ignore_failure_code);
    fprintf(stderr, "commit request result %d\n", rc);

unexpected_job_state:
commit_request_failed:
    if (monitor.job_contact != NULL)
    {
        globus_gram_client_job_contact_free(monitor.job_contact);
    }
job_request_failed:
    free(rsl);
malloc_rsl_failed:
allow_callback_failed:
    globus_mutex_unlock(&monitor.mutex);
    globus_gram_client_callback_disallow(monitor.callback_contact);
    if (monitor.callback_contact != NULL)
    {
        free(monitor.callback_contact);
    }
    globus_mutex_destroy(&monitor.mutex);
    globus_cond_destroy(&monitor.cond);
gass_server_get_url_failed:
    globus_gass_server_ez_shutdown(listener);
gass_server_ez_init_failed:
activate_server_ez_failed:
activate_common_failed:
    globus_module_deactivate_all();
args_error:
    printf("%s # stdio-update-after-failure-test\n", rc == 0 ? "ok" : "not ok");
    return rc;
}

static 
void
globus_l_state_callback(
    void * callback_arg,
    char * job_contact,
    int state,
    int errorcode)
{
    monitor_t * monitor = callback_arg;

    globus_mutex_lock(&monitor->mutex);
    if (! strcmp(monitor->job_contact, job_contact))
    {
        fprintf(stderr, "callback:\nstate=%d\nerrorcode=%d\n\n",
                state, errorcode);
        monitor->job_status = state;
        monitor->failure_code = errorcode;
        globus_cond_signal(&monitor->cond);
    }
    globus_mutex_unlock(&monitor->mutex);
}
