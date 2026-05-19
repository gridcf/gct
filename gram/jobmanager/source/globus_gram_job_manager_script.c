/*
 * Copyright 1999-2009 University of Chicago
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

#include "globus_gram_job_manager.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <utime.h>

static const char * GLOBUS_GRAM_SCRIPT_NO_CLIENT = "noclient";

globus_xio_driver_t                     globus_i_gram_job_manager_popen_driver;
globus_xio_stack_t                      globus_i_gram_job_manager_popen_stack;
static uint64_t                         globus_l_gram_next_script_sequence = 0;

typedef struct globus_gram_script_handle_s
{
    globus_gram_job_manager_t *         manager;
    globus_gram_job_manager_scripts_t * scripts;
    globus_xio_handle_t                 handle;
    globus_byte_t                       return_buf[GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE];
    size_t                              return_buf_offset;
    globus_result_t                     result;
    int                                 pending_ops;
    time_t                              last_use;
}
*globus_gram_script_handle_t;

int
globus_gram_job_manager_script_handle_init(
    globus_gram_job_manager_t *         manager,
    globus_gram_job_manager_scripts_t * scripts,
    globus_gram_script_handle_t *       handle);

static
int
globus_l_gram_enqueue_description(
    globus_fifo_t *                     fifo,
    globus_gram_jobmanager_request_t *  request,
    va_list                             ap);

static
int
globus_l_gram_enqueue_string(
    globus_fifo_t *                     fifo,
    const char *                        format,
    ...);

static
int
globus_l_gram_fifo_to_iovec(
    globus_fifo_t *                     fifo,
    struct iovec **                     iovec,
    int *                               num_iovec);

/* Module Specific Types */
typedef void (*globus_gram_job_manager_script_callback_t)(
    void *                              arg,
    globus_gram_jobmanager_request_t *  request,
    int                                 failure_code,
    int                                 starting_state,
    const char *                        variable,
    const char *                        value);

typedef struct
{
    globus_gram_job_manager_script_callback_t
                                        callback;
    void *                              callback_arg;
    globus_gram_jobmanager_request_t *  request;
    int                                 starting_jobmanager_state;
    struct iovec *                      iov;
    int                                 iovcnt;
    globus_gram_script_handle_t         handle;
    globus_gram_script_priority_t       priority;
}
globus_gram_job_manager_script_context_t;

/* Module Specific Prototypes */
static
void
globus_l_gram_job_manager_script_read(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

static
void
globus_l_gram_job_manager_default_done(
    void *                              arg,
    globus_gram_jobmanager_request_t *  request,
    int                                 failure_code,
    int                                 starting_state,
    const char *                        variable,
    const char *                        value);

static
void
globus_l_gram_job_manager_query_done(
    void *                              arg,
    globus_gram_jobmanager_request_t *  request,
    int                                 failure_code,
    int                                 starting_jobmanager_state,
    const char *                        variable,
    const char *                        value);

static
int
globus_l_gram_request_validate(
    globus_gram_jobmanager_request_t *  request);

static
char *
globus_l_gram_job_manager_script_prepare_param(
    const char *                        param);

static
int
globus_l_gram_enqueue_staging_list(
    globus_gram_jobmanager_request_t *  request,
    globus_fifo_t *                     fifo,
    globus_gram_job_manager_staging_type_t
                                        type);

static
void
globus_l_gram_job_manager_script_staged_done(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_staging_type_t
                                        type,
    const char *                        value);

static
int
globus_l_gram_script_queue(
    globus_gram_job_manager_t *         manager,
    globus_gram_job_manager_script_context_t *
                                        context);

static
void
globus_l_gram_process_script_queue_locked(
    globus_gram_job_manager_t *         manager,
    globus_gram_job_manager_scripts_t * scripts);

static
void
globus_l_gram_job_manager_script_done(
    globus_gram_job_manager_t *         manager,
    globus_gram_job_manager_scripts_t * scripts,
    globus_gram_script_handle_t         handle);

static
void
globus_l_script_close_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg);

static
void
globus_l_script_writev_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

static
void
globus_l_gram_script_open_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg);

static
int
globus_l_gram_script_register_read_and_write(
    globus_gram_job_manager_script_context_t *
                                        script_context);

static
int
globus_l_gram_script_priority_cmp(
    void *                              priority_1,
    void *                              priority_2);

static
int
globus_l_match_script_client_addr(
    void *                              datum,
    void *                              arg);

/**
 * Begin execution of a job manager script
 */
static
int
globus_l_gram_job_manager_script_run(
    globus_gram_jobmanager_request_t *  request,
    const char *                        script_cmd,
    globus_gram_job_manager_script_callback_t
                                        callback,
    void *                              callback_arg,
    ...)
{
    globus_gram_job_manager_script_context_t *
                                        script_context;
    int                                 rc;
    globus_fifo_t                       fifo;
    va_list                             ap;

    rc = globus_fifo_init(&fifo);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto fifo_init_failed;
    }
    rc = globus_l_gram_enqueue_string(&fifo, "%s\n", script_cmd);
    if (rc != GLOBUS_SUCCESS)
    {
        goto enqueue_cmd_failed;
    }
    va_start(ap, callback_arg);
    rc = globus_l_gram_enqueue_description(
            &fifo,
            request,
            ap);
    va_end(ap);

    if (rc != GLOBUS_SUCCESS)
    {
        goto enqueue_description_failed;
    }

    rc = globus_l_gram_enqueue_string(&fifo, "\n");
    if (rc != GLOBUS_SUCCESS)
    {
        goto enqueue_end_of_cmd_failed;
    }

    script_context = malloc(
            sizeof(globus_gram_job_manager_script_context_t));
    if (script_context == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto script_context_malloc_failed;
    }

    rc = globus_gram_job_manager_add_reference(
            request->manager,
            request->job_contact_path,
            "script",
            NULL);
    globus_assert(rc == GLOBUS_SUCCESS);

    script_context->callback = callback;
    script_context->callback_arg = callback_arg;
    script_context->request = request;
    script_context->starting_jobmanager_state = request->jobmanager_state;

    if (strcmp(script_cmd, "poll") == 0)
    {
        script_context->priority.priority_level =
            GLOBUS_GRAM_SCRIPT_PRIORITY_LEVEL_POLL;
    }
    else if (strcmp(script_cmd, "cancel") == 0)
    {
        script_context->priority.priority_level =
            GLOBUS_GRAM_SCRIPT_PRIORITY_LEVEL_CANCEL;
    }
    else if (strcmp(script_cmd, "submit") == 0)
    {
        script_context->priority.priority_level =
            GLOBUS_GRAM_SCRIPT_PRIORITY_LEVEL_SUBMIT;
    }
    else if (strcmp(script_cmd, "stage_out") == 0)
    {
        script_context->priority.priority_level =
            GLOBUS_GRAM_SCRIPT_PRIORITY_LEVEL_STAGE_OUT;
    }
    else if (strcmp(script_cmd, "stage_in") == 0)
    {
        script_context->priority.priority_level =
            GLOBUS_GRAM_SCRIPT_PRIORITY_LEVEL_STAGE_IN;
    }
    else if (strcmp(script_cmd, "signal") == 0)
    {
        script_context->priority.priority_level =
            GLOBUS_GRAM_SCRIPT_PRIORITY_LEVEL_SIGNAL;
    }
    rc = globus_l_gram_fifo_to_iovec(
            &fifo,
            &script_context->iov,
            &script_context->iovcnt);

    rc = globus_l_gram_script_queue(
            request->manager,
            script_context);
    if (rc != GLOBUS_SUCCESS)
    {
        goto queue_failed;
    }
    if (rc != GLOBUS_SUCCESS)
    {
queue_failed:
        globus_gram_job_manager_remove_reference(
                request->manager,
                request->job_contact_path,
                "script");
        free(script_context);
    }
script_context_malloc_failed:
enqueue_end_of_cmd_failed:
enqueue_description_failed:
enqueue_cmd_failed:
    if (rc != GLOBUS_SUCCESS)
    {
        globus_fifo_destroy_all(&fifo, free);
    }
    else
    {
        globus_fifo_destroy(&fifo);
    }
fifo_init_failed:

    return rc;
}
/* globus_l_gram_job_manager_script_run() */

static
void
globus_l_gram_job_manager_script_read(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_gram_jobmanager_request_t *  request;
    globus_gram_job_manager_script_context_t *
                                        script_context;
    globus_gram_script_handle_t         script_handle;
    char *                              script_variable;
    char *                              script_variable_end;
    unsigned char *                     script_value;
    globus_bool_t                       eof = GLOBUS_FALSE;
    char *                              p;
    int                                 failure_code = 0;
    int                                 i;
    globus_gram_job_manager_scripts_t * scripts;

    script_context = user_arg;
    request = script_context->request;
    script_handle = script_context->handle;
    script_handle->return_buf_offset += nbytes;

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.script_read.start "
            "level=DEBUG "
            "gramid=%s "
            "result=%d "
            "nbytes=%d "
            "\n",
            request->job_contact_path,
            result,
            (int) nbytes);
    if ((script_handle->return_buf_offset == sizeof(script_handle->return_buf))
        && (memchr(script_handle->return_buf, '\n', script_handle->return_buf_offset) == NULL))
    {
        result = GLOBUS_FAILURE;
        failure_code = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRIPT_STATUS;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.script_read.end "
                "level=ERROR "
                "gramid=%s "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                (int) -failure_code,
                "script response doesn't contain newline");
        eof = GLOBUS_TRUE;
        script_handle->result = GLOBUS_FAILURE;
    }
    else if (result)
    {
        eof = GLOBUS_TRUE;
        if (!globus_xio_error_is_eof(result))
        {
            char *                      errstr;
            char *                      errstr_escaped;

            errstr = globus_error_print_friendly(globus_error_peek(result));
            errstr_escaped = globus_gram_prepare_log_string(errstr);

            failure_code =
                GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRIPT_STATUS;

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.script_read.end "
                    "level=ERROR "
                    "gramid=%s "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    (int) -failure_code,
                    errstr_escaped ? errstr_escaped : "");
            if (errstr)
            {
                free(errstr);
            }
            if (errstr_escaped)
            {
                free(errstr_escaped);
            }
        }
        script_handle->result = result;
    }

    while((p = memchr(script_handle->return_buf, '\n', script_handle->return_buf_offset)) != NULL)
    {
        char *                          escaped;
        size_t                          newline_offset;

        *p = '\0';
        newline_offset = p - (char *) &script_handle->return_buf[0];

        if (request->job_log_level & GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE)
        {
            escaped = globus_gram_prepare_log_string(
                    (char *) &script_handle->return_buf[0]);

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                    "event=gram.script_read.info "
                    "level=TRACE "
                    "gramid=%s "
                    "response=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    escaped ? escaped : "");

            if (escaped)
            {
                free(escaped);
            }
        }

        script_variable = (char *) script_handle->return_buf;

        if (*script_variable == 0)
        {
            /* End of input */
            eof = GLOBUS_TRUE;
            script_handle->return_buf_offset -= newline_offset + 1;
            break;
        }

        while(*script_variable && isspace(*script_variable))
        {
            script_variable++;
        }
        script_variable_end = script_variable;

        while(*script_variable_end && *script_variable_end != ':')
        {
            script_variable_end++;
        }
        *script_variable_end = '\0';

        script_value = (unsigned char *) script_variable_end+1;

        script_context->callback(
                script_context->callback_arg,
                request,
                failure_code,
                script_context->starting_jobmanager_state,
                script_variable,
                (char *) script_value);

        /*
         * We need to log the batch job ID to the accounting file.
         */

        if(strcmp(script_variable, "GRAM_SCRIPT_JOB_ID") == 0)
        {
            const char * gk_jm_id_var = "GATEKEEPER_JM_ID";
            const char * gk_jm_id  = getenv(gk_jm_id_var);
            const char * gk_peer   = getenv("GATEKEEPER_PEER");
            const char * globus_id = getenv("GLOBUS_ID");
            uid_t uid = getuid();
            gid_t gid = getgid();
            const char *user = request->config->logname;

            globus_gram_job_manager_request_acct(
                request, "%s %s for %s on %s\n", gk_jm_id_var,
                gk_jm_id  ? gk_jm_id  : "none",
                globus_id ? globus_id : "unknown",
                gk_peer   ? gk_peer   : "unknown");

            globus_gram_job_manager_request_acct(
                request, "%s %s mapped to %s (%u, %u)\n", gk_jm_id_var,
                gk_jm_id  ? gk_jm_id  : "none",
                user, uid, gid);

            globus_gram_job_manager_request_acct(
                request, "%s %s has %s %s manager type %s\n", gk_jm_id_var,
                gk_jm_id  ? gk_jm_id  : "none",
                script_variable, script_value,
                request->config->jobmanager_type);
        }

        if(script_handle->return_buf_offset > newline_offset)
        {
            memmove(&script_handle->return_buf[0],
                    &script_handle->return_buf[newline_offset+1],
                    script_handle->return_buf_offset - newline_offset - 1);
            script_handle->return_buf_offset -= newline_offset + 1;
        }
        else
        {
            script_handle->return_buf[0] = '\0';
            script_handle->return_buf_offset = 0;
        }
    }

    if(! eof)
    {
        if (sizeof(script_handle->return_buf) >
                script_handle->return_buf_offset)
        {
                globus_gram_job_manager_request_log(
                        script_context->request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                        "event=gram.script_read.info "
                        "level=TRACE "
                        "message=\"registering read\" "
                        "return_buf_offset=%d "
                        "read_len=%d "
                        "\n",
                        (int) script_handle->return_buf_offset,
                        (int) (sizeof(script_handle->return_buf) - script_handle->return_buf_offset),
                        p);
            result = globus_xio_register_read(
                    script_handle->handle,
                    &script_handle->return_buf[script_handle->return_buf_offset],
                    sizeof(script_handle->return_buf) - script_handle->return_buf_offset,
                    1,
                    NULL,
                    globus_l_gram_job_manager_script_read,
                    script_context);
            if(result == GLOBUS_SUCCESS)
            {
                /* New callback registered successfully */
                return;
            }
        }
        else
        {
            result = GLOBUS_FAILURE;
        }


        {
            char *                      errstr;
            char *                      errstr_escaped;

            errstr = globus_error_print_friendly(globus_error_peek(result));
            errstr_escaped = globus_gram_prepare_log_string(errstr);

            failure_code = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRIPT_STATUS;

            globus_gram_job_manager_request_log(
                    script_context->request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.script_read.end "
                    "level=ERROR "
                    "gramid=%s "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    -failure_code,
                    errstr_escaped ? errstr_escaped : "");

            if (errstr)
            {
                free(errstr);
            }

            if (errstr_escaped)
            {
                free(errstr_escaped);
            }
        }
    }

    script_handle = script_context->handle;

    GlobusGramJobManagerLock(request->manager);
    script_handle->pending_ops--;

    scripts = globus_list_first(
            globus_list_search_pred(
                    request->manager->scripts_per_client,
                    globus_l_match_script_client_addr,
                    request->job_stats.client_address
                        ? request->job_stats.client_address
                        : (void *) GLOBUS_GRAM_SCRIPT_NO_CLIENT));

    globus_l_gram_job_manager_script_done(request->manager, scripts, script_handle);
    GlobusGramJobManagerUnlock(request->manager);

    script_context->callback(
            script_context->callback_arg,
            request,
            (result == GLOBUS_SUCCESS)
                ? GLOBUS_SUCCESS
                : GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRIPT_STATUS,
            script_context->starting_jobmanager_state,
            NULL,
            NULL);

    if (result == GLOBUS_SUCCESS)
    {
        globus_gram_job_manager_request_log(
                script_context->request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                "event=gram.script_read.end "
                "level=TRACE "
                "gramid=%s "
                "status=%d "
                "\n",
                request->job_contact_path,
                0);
    }
    globus_gram_job_manager_remove_reference(
            request->manager,
            request->job_contact_path,
            "script");

    for (i = 0; i < script_context->iovcnt; i++)
    {
        free(script_context->iov[i].iov_base);
    }
    free(script_context->iov);
    free(script_context);
}
/* globus_l_gram_job_manager_script_read() */

/**
 * Submit a job request to a local scheduler.
 *
 * This function submits the passed job request to the local scheduler
 * script.
 *
 * @param request
 *        The request containing the job description and related information.
 *
 * @return
 * This function returns GLOBUS_SUCCESS or a failure code if the
 * job could not be submitted. If successful, this function will call
 * into the state machine once the job submission result has happened.
 */
int
globus_gram_job_manager_script_submit(
    globus_gram_jobmanager_request_t *  request)
{
    char * script_cmd = "submit";
    int rc;

    rc = globus_l_gram_request_validate(request);
    if (rc != GLOBUS_SUCCESS)
    {
        return rc;
    }

    /*
     * used to test job manager functionality without actually submitting
     * job
     */
    if (request->dry_run)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_DRYRUN;
    }

    if (request->config->seg_module &&
        strcmp(request->config->seg_module, "condor") == 0)
    {
        rc = globus_l_gram_job_manager_script_run(
                    request,
                    script_cmd,
                    globus_l_gram_job_manager_default_done,
                    NULL,
                    "emitcondorprocesses", 'd', 1,
                    NULL);
    }
    else if (request->config->seg_module &&
        strcmp(request->config->seg_module, "fork") == 0)
    {
        rc = globus_l_gram_job_manager_script_run(
                    request,
                    script_cmd,
                    globus_l_gram_job_manager_default_done,
                    NULL,
                    "useforkstarter", 'd', 1,
                    NULL);
    }
    else
    {
        rc = globus_l_gram_job_manager_script_run(
                    request,
                    script_cmd,
                    globus_l_gram_job_manager_default_done,
                    NULL,
                    NULL);
    }

    return rc;
}
/* globus_gram_job_manager_script_submit() */



/**
 * Set job request status and fire callback so it registers
 */
static
int
local_globus_set_status(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_job_state_t    status)
{
    globus_reltime_t                    delay;
    int                                 rc;

    if(request->status != status)
    {
        globus_gram_job_manager_request_set_status(request, status);
        request->unsent_status_change = GLOBUS_TRUE;
    }

    GlobusTimeReltimeSet(delay, 0, 0);

    rc = globus_gram_job_manager_state_machine_register(
            request->manager,
            request,
            &delay);

    return rc;
}
/* local_globus_set_status() */


/**
 * Modified job_contact in place to remove the port.
 */
static void job_contact_strip_port(
    char * job_contact)
{
    char * first_end;
    char * second_begin;

    if( job_contact == 0 )
        return;

    first_end = strrchr( job_contact, ':' );
    if( first_end == 0 ) /* malformed job_contact? */
        return;

    second_begin = strchr( first_end, '/' );
    if( second_begin == 0 ) /* malformed job_contact? */
        return;

    memmove(first_end, second_begin, strlen(second_begin) + 1);
}



/**
 * Try to poll status of job request using Condor grid_manager_monitor_agent
 *
 * If the Condor grid_manager_monitor_agent is running on the machine, this
 * function retrieve job request status using that, otherwise it fails.
 * Expected to be called exclusively from globus_gram_job_manager_script_poll.
 */
int
globus_gram_job_manager_script_poll_fast(
    globus_gram_jobmanager_request_t *    request)
{
    int i;
    char * grid_monitor_output = 0;
    char * grid_monitor_files[3] = { NULL, NULL, NULL };
                /* Path is $GLOBUS_LOCATION/GRID_MONITOR_LOCATION$UID */
    const char * GRID_MONITOR_LOCATION_1 = "/tmp/grid_manager_monitor_agent_log.";
    const char * GRID_MONITOR_LOCATION_2 = "/tmp/gram_job_state/grid_manager_monitor_agent_log.";
    const char * WHITESPACE = " \t";
    uid_t this_uid = geteuid();
    struct stat stat_results;
    FILE * grid_monitor_file = 0;
    int rc;
    time_t MAX_MONITOR_FILE_AGE = (60*5); /* seconds */
    char line[1024];
    char line_job_contact[1024];
    int return_val = GLOBUS_FAILURE;
    time_t status_file_last_update = 0;
    char * job_contact_match = 0;

    if( ! request ||
        !request->job_contact)
    {
        goto FAST_POLL_EXIT_FAILURE;
    }

    if(this_uid > 999999)
    {
    /* UIDs this large are unlikely, but if they occur the buffer
     * isn't large enough to handle it
     */
        goto FAST_POLL_EXIT_FAILURE;
    }

    /* The grid monitor's job status file can be in one of two places.
     * We want to check both.
     */
    grid_monitor_files[0] = globus_common_create_string(
            "%s%s%d",
            request->config->globus_location ?
            request->config->globus_location : "",
            GRID_MONITOR_LOCATION_1,
            (int)this_uid);
    if( ! grid_monitor_files[0])
    {
        goto FAST_POLL_EXIT_FAILURE;
    }

    grid_monitor_files[1] = globus_common_create_string(
            "%s%s%d",
            request->config->globus_location ?
            request->config->globus_location : "",
            GRID_MONITOR_LOCATION_2,
            (int)this_uid);
    if( ! grid_monitor_files[1])
    {
        goto FAST_POLL_EXIT_FAILURE;
    }

    for ( i = 0; grid_monitor_files[i]; i++ )
    {
        grid_monitor_output = grid_monitor_files[i];

        grid_monitor_file = fopen(grid_monitor_output, "r");

        if( ! grid_monitor_file )
        {
            /* No monitor file?  That's acceptable, silently fail */
            continue;
        }

        rc = stat(grid_monitor_output, &stat_results);
        if( rc != 0 ) {
            fclose(grid_monitor_file);
            grid_monitor_file = NULL;
            continue;
        }

        if(stat_results.st_uid != this_uid || !S_ISREG(stat_results.st_mode))
        {
            fclose(grid_monitor_file);
            grid_monitor_file = NULL;
            continue;
        }

        if( (stat_results.st_mtime + MAX_MONITOR_FILE_AGE) < time(NULL) )
        {
            fclose(grid_monitor_file);
            grid_monitor_file = NULL;
            continue;
        }

        break;
    }
    if ( grid_monitor_file == NULL ) {
        goto FAST_POLL_EXIT_FAILURE;
    }

    /* If we got this far, we've decided we trust the file */

    /* Read the first line, which is two timestamps as seconds since epoch.
     * The first one is start time of last query pass, the second is finish. */
    if( ! fgets(line, sizeof(line), grid_monitor_file) )
    {
        goto FAST_POLL_EXIT_FAILURE;
    }
    if( ! feof(grid_monitor_file) && line[strlen(line) - 1] != '\n')
    {
        goto FAST_POLL_EXIT_FAILURE;
    }

    status_file_last_update = atoi(line);
    if(status_file_last_update < request->status_update_time) {
        /* We somehow got a status update more recent than the status file.
         * Most likely we successfully executed a traditional poll faster than
         * the status script processed things.  This status file is fresh
         * enough, so we should switch over to using that, we want to avoid
         * firing off a traditional poll.  So, leave the existing status in
         * place and report a successful poll. */
        local_globus_set_status(request, request->status);
        return_val = GLOBUS_SUCCESS;
        goto FAST_POLL_EXIT;
    }

    job_contact_match = malloc(strlen(request->job_contact) + 1);
    strcpy(job_contact_match, request->job_contact);
    job_contact_strip_port(job_contact_match);

    /* TODO: First pass.  Improve with binary search of file to make
     * scanning large files fast. Still this is probably plenty fast
     * enough for fairly large runs. */
    while( 1 )
    {
        size_t len = 0;
        char * line_bit = line;
        int new_status = 0;
        if( ! fgets(line, sizeof(line), grid_monitor_file) )
        {
            /* end of file (or error), job isn't in file.  It might just not
             * have been noticed yet.  Silently skip */
            goto FAST_POLL_EXIT_FAILURE;
        }
        if( ! feof(grid_monitor_file) && line[strlen(line) - 1] != '\n')
        {
            goto FAST_POLL_EXIT_FAILURE;
        }

        len = strcspn(line_bit, WHITESPACE);
        if(len == 0)
        {
            goto FAST_POLL_EXIT_FAILURE;
        }

        /* So long as sizeof(line_job_contact) == sizeof(line),
         * this is safe */
        memcpy(line_job_contact, line, len);
        line_job_contact[len] = 0;
        job_contact_strip_port(line_job_contact);

        if( strcmp(line_job_contact, job_contact_match) != 0 )
        {
            continue;
        }

        line_bit += len;

        len = strspn(line_bit, WHITESPACE);
        if(len == 0)
        {
            goto FAST_POLL_EXIT_FAILURE;
        }

        line_bit += len;

        /* Found exact match, read status */
        len = strspn(line_bit, "0123456789");
        if(len == 0)
        {
            /* No digits!? */
            goto FAST_POLL_EXIT_FAILURE;
        }

        new_status = atoi(line_bit);
        if ( new_status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED ) {
            /* Condor's grid monitor doesn't report the GRAM failure code
             * that usually accompanies a FAILED status. Let's call the
             * poll function ourselves to get that status.
             */
            globus_gram_job_manager_request_log(request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "JMI: poll_fast: Monitoring file reports FAILED, "
                "Reverting to normal polling to get failure code.\n");
            goto FAST_POLL_EXIT_FAILURE;
        }
        local_globus_set_status(request, new_status);

        return_val = GLOBUS_SUCCESS;
        goto FAST_POLL_EXIT;
    }

FAST_POLL_EXIT_FAILURE:
    return_val = GLOBUS_FAILURE;

FAST_POLL_EXIT:
    if(grid_monitor_file)
        fclose(grid_monitor_file);
    for ( i = 0; grid_monitor_files[i]; i++ ) {
        free(grid_monitor_files[i]);
    }
    if( job_contact_match )
        free(job_contact_match);

    return return_val;
}
/* globus_gram_job_manager_script_poll_fast() */

/**
 * Poll the status of a job request.
 *
 * This function invokes a scheduler-specific program to determine
 * the current status of the job request. The job status field of
 * the requst structure will be updated with the new status.
 *
 * @param request
 *        The request containing the job description.
 * @return GLOBUS_GRAM_JOBMANAGER_STATUS_UNCHANGED or
 * GLOBUS_GRAM_JOBMANAGER_STATUS_CHANGED depending whether the job status
 * is the same as the result from the value of request's status field. This
 * field will be updated if the job's status has changed.
 */
int
globus_gram_job_manager_script_poll(
    globus_gram_jobmanager_request_t *  request)
{
    char *                              script_cmd = "poll";
    int                                 rc;

    rc = globus_l_gram_request_validate(request);
    if (rc != GLOBUS_SUCCESS)
    {
        return rc;
    }

    /* Keep the state file's timestamp up to date so that
     * anything scrubbing the state files of old and dead
     * processes leaves it alone
     */
    if(request->job_state_file)
    {
        utime(request->job_state_file, NULL);
    }


    if( globus_gram_job_manager_script_poll_fast(request) == GLOBUS_SUCCESS )
    {
        return(GLOBUS_SUCCESS);
    }

    rc = globus_l_gram_job_manager_script_run(
                request,
                script_cmd,
                globus_l_gram_job_manager_default_done,
                NULL,
                NULL);

    if (rc != GLOBUS_SUCCESS)
    {
        return rc;
    }

    return(GLOBUS_SUCCESS);
}
/* globus_gram_job_manager_script_poll() */

/**
 * Cancel a GRAM job.
 *
 * This function invokes a scheduler-specific program which cancels the
 * job.
 *
 * @param request
 *        The job request containing information about the job to be cancelled.
 */
int
globus_gram_job_manager_script_cancel(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_query_t *   query)
{
    char *                              script_cmd = "cancel";
    int                                 rc;

    rc = globus_l_gram_request_validate(request);
    if (rc != GLOBUS_SUCCESS)
    {
        return rc;
    }

    rc = globus_l_gram_job_manager_script_run(
                request,
                script_cmd,
                globus_l_gram_job_manager_query_done,
                query,
                NULL);

    if (rc != GLOBUS_SUCCESS)
    {
        return rc;
    }

    return(GLOBUS_SUCCESS);
}
/* globus_gram_job_manager_script_cancel() */

/**
 * Send a signal to a job scheduler
 *
 * @param request
 *        The job request containing information about the job to
 *        signal. The signal and signal_arg data are used by
 *        this function.
 */
int
globus_gram_job_manager_script_signal(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_query_t *   query)
{
    char *                              script_cmd = "signal";
    int                                 rc;

    rc = globus_l_gram_request_validate(request);

    if (rc != GLOBUS_SUCCESS)
    {
        return rc;
    }

    /*
     * add the signal and signal_arg to the script arg file
     */
    rc = globus_l_gram_job_manager_script_run(
                request,
                script_cmd,
                globus_l_gram_job_manager_query_done,
                query,
                "signal", 'd', query->signal,
                "signalarg", 's', query->signal_arg,
                NULL);

    if(rc != GLOBUS_SUCCESS)
    {
        return rc;
    }
    return(GLOBUS_SUCCESS);
}
/* globus_gram_job_manager_script_signal() */

int
globus_gram_job_manager_script_stage_in(
    globus_gram_jobmanager_request_t *  request)
{
    char *                              script_cmd = "stage_in";
    int                                 rc;

    rc = globus_l_gram_request_validate(request);
    if (rc != GLOBUS_SUCCESS)
    {
        return rc;
    }

    rc = globus_l_gram_job_manager_script_run(
                request,
                script_cmd,
                globus_l_gram_job_manager_default_done,
                NULL,
                NULL);

    if (rc != GLOBUS_SUCCESS)
    {
        return rc;
    }

    return(GLOBUS_SUCCESS);
}
/* globus_gram_job_manager_script_stage_in() */

int
globus_gram_job_manager_script_stage_out(
    globus_gram_jobmanager_request_t *  request)
{
    char *                              script_cmd = "stage_out";
    int                                 rc;

    rc = globus_l_gram_request_validate(request);
    if (rc != GLOBUS_SUCCESS)
    {
        return rc;
    }

    rc = globus_l_gram_job_manager_script_run(
                request,
                script_cmd,
                globus_l_gram_job_manager_default_done,
                NULL,
                NULL);

    return rc;
}
/* globus_gram_job_manager_script_stage_out() */

/**
 * Completion callback for done and poll scripts.
 *
 * This is called when a line of output containing a variable:value
 * pair is read from the script's execution.
 */
static
void
globus_l_gram_job_manager_default_done(
    void *                              arg,
    globus_gram_jobmanager_request_t *  request,
    int                                 failure_code,
    int                                 starting_jobmanager_state,
    const char *                        variable,
    const char *                        value)
{
    int                                 script_status;

    GlobusGramJobManagerRequestLock(request);

    if(failure_code)
    {
        request->failure_code = failure_code;
    }
    if(!variable)
    {
        globus_reltime_t delay;
        GlobusTimeReltimeSet(delay, 0, 0);
        globus_gram_job_manager_state_machine_register(
                request->manager,
                request,
                &delay);
    }
    else if(strcmp(variable, "GRAM_SCRIPT_JOB_STATE") == 0)
    {
        script_status = atoi(value);

        if(script_status < 0)
        {
            request->failure_code =
                GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRIPT_STATUS;
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.script_read.info "
                    "level=ERROR "
                    "gramid=%s "
                    "msg=\"%s\" "
                    "value=\"%s\" "
                    "script_status=%d "
                    "\n",
                    request->job_contact_path,
                    "Invalid GRAM_SCRIPT_JOB_STATE",
                    value,
                    script_status);
        }
        else if(globus_i_gram_job_manager_script_valid_state_change(
                    request, script_status))
        {
            if ((script_status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE) &&
                    globus_gram_job_manager_rsl_need_stage_out(request))
            {
                globus_gram_job_manager_request_set_status(
                        request,
                        GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_OUT);
            }
            else
            {
                globus_gram_job_manager_request_set_status(
                        request,
                        script_status);
            }
            request->unsent_status_change = GLOBUS_TRUE;
        }
    }
    else if(strcmp(variable, "GRAM_SCRIPT_ERROR") == 0)
    {
        script_status = atoi(value);

        if(request->jobmanager_state == starting_jobmanager_state)
        {
            globus_gram_job_manager_request_set_status(request, GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);
            if(script_status <= 0)
            {
                request->failure_code =
                    GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRIPT_STATUS;
                globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                        "event=gram.script_read.info "
                        "level=ERROR "
                        "gramid=%s "
                        "msg=\"%s\" "
                        "value=\"%s\" "
                        "script_status=%d "
                        "\n",
                        request->job_contact_path,
                        "Invalid GRAM_SCRIPT_ERROR",
                        value,
                        script_status);
            }
            else
            {
                request->failure_code = script_status;
            }
            request->unsent_status_change = GLOBUS_TRUE;
        }
    }
    else if(strcmp(variable, "GRAM_SCRIPT_JOB_ID") == 0)
    {
        if(value != NULL && strlen(value) > 0)
        {
            request->job_id_string = strdup(value);
            request->original_job_id_string = strdup(value);

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_INFO,
                    "event=gram.job.info "
                    "level=INFO "
                    "msg=\"%s\" "
                    "gramid=%s "
                    "lrm_id=\"%s\" "
                    "\n",
                    "job submitted to lrm",
                    request->job_contact_path,
                    value);
        }
    }
    else if(strcmp(variable, "GRAM_SCRIPT_JOB_ACCT_INFO") == 0)
    {
        if(value != NULL && strlen(value) > 0)
        {
            const char *gk_jm_id_var = "GATEKEEPER_JM_ID";
            const char *gk_jm_id = getenv(gk_jm_id_var);
            const char *v = value;
            char *buf = malloc(strlen(value) + 1);
            char *b = buf;
            char c;

            while ((*b++ = ((c = *v++) != '\\') ? c :
                           ((c = *v++) != 'n' ) ? c : '\n'))
            {
            }

            globus_gram_job_manager_request_acct(
                request, "%s %s summary:\n%s\nJMA -- end of summary\n", gk_jm_id_var,
                gk_jm_id ? gk_jm_id : "none", buf);

            free(buf);
        }
    }
    else if(strcmp(variable, "GRAM_SCRIPT_SCRATCH_DIR") == 0)
    {
        request->scratchdir = strdup(value);
    }
    else if(strcmp(variable, "GRAM_SCRIPT_STAGED_IN") == 0)
    {
        globus_l_gram_job_manager_script_staged_done(
                request,
                GLOBUS_GRAM_JOB_MANAGER_STAGE_IN,
                value);
    }
    else if(strcmp(variable, "GRAM_SCRIPT_STAGED_IN_SHARED") == 0)
    {
        globus_l_gram_job_manager_script_staged_done(
                request,
                GLOBUS_GRAM_JOB_MANAGER_STAGE_IN_SHARED,
                value);
    }
    else if(strcmp(variable, "GRAM_SCRIPT_STAGED_OUT") == 0)
    {
        if(request->jobmanager_state == starting_jobmanager_state)
        {
            globus_l_gram_job_manager_script_staged_done(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_STAGE_OUT,
                    value);
        }
    }
    else if (strcmp(variable, "GRAM_SCRIPT_STAGED_STREAM") == 0)
    {
        if(request->jobmanager_state == starting_jobmanager_state)
        {
            globus_l_gram_job_manager_script_staged_done(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_STAGE_STREAMS,
                    value);
        }
    }
    else if(strncmp(variable, "GRAM_SCRIPT_GT3", 15) == 0)
    {
        if (request->gt3_failure_type == NULL &&
            strcmp(variable, "GRAM_SCRIPT_GT3_FAILURE_TYPE") == 0)
        {
            request->gt3_failure_type = strdup(value);
        }
        else if (request->gt3_failure_message == NULL &&
            strcmp(variable, "GRAM_SCRIPT_GT3_FAILURE_MESSAGE") == 0)
        {
            request->gt3_failure_message = strdup(value);
        }
        else if (request->gt3_failure_source == NULL &&
            strcmp(variable, "GRAM_SCRIPT_GT3_FAILURE_SOURCE") == 0)
        {
            request->gt3_failure_source = strdup(value);
        }
        else if (request->gt3_failure_destination == NULL &&
            strcmp(variable, "GRAM_SCRIPT_GT3_FAILURE_DESTINATION") == 0)
        {
            request->gt3_failure_destination = strdup(value);
        }
    }
    else if(strcmp(variable, "GRAM_SCRIPT_LOG") == 0)
    {
        if (request->job_log_level & GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG)
        {
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                    "event=gram.script.log "
                    "level=DEBUG "
                    "%s "
                    "\n",
                    value);
        }
    }
    else if(request->jobmanager_state == starting_jobmanager_state)
    {
        globus_gram_job_manager_request_set_status(request, GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);
        request->failure_code =
            GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRIPT_STATUS;
        request->unsent_status_change = GLOBUS_TRUE;
        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.script_read.info "
                "level=ERROR "
                "gramid=%s "
                "msg=\"%s\" "
                "variable=\"%s\" "
                "value=\"%s\" "
                "\n",
                request->job_contact_path,
                "unknown variable",
                variable,
                value);
    }

    GlobusGramJobManagerRequestUnlock(request);
}
/* globus_l_gram_job_manager_default_done() */

/**
 * Completion callback for query-initiated scripts
 */
static
void
globus_l_gram_job_manager_query_done(
    void *                              arg,
    globus_gram_jobmanager_request_t *  request,
    int                                 failure_code,
    int                                 starting_jobmanager_state,
    const char *                        variable,
    const char *                        value)
{
    int                                 script_status;
    globus_gram_job_manager_query_t *   query;
    globus_reltime_t                    delay;

    query = arg;

    GlobusGramJobManagerRequestLock(request);

    if(failure_code)
    {
        request->failure_code = failure_code;
    }
    if(!variable)
    {
        GlobusTimeReltimeSet(delay, 0, 0);
        globus_gram_job_manager_state_machine_register(
                request->manager,
                request,
                &delay);
    }
    else if(strcmp(variable, "GRAM_SCRIPT_ERROR") == 0)
    {
        script_status = atoi(value);

        if(script_status <= 0)
        {
            query->failure_code =
                GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRIPT_STATUS;
        }
        else
        {
            query->failure_code = script_status;
        }
    }
    else if(strcmp(variable, "GRAM_SCRIPT_JOB_STATE") == 0)
    {
        script_status = atoi(value);

        if(script_status <= 0)
        {
            query->failure_code =
                GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRIPT_STATUS;
        }
        else if((query->type == GLOBUS_GRAM_JOB_MANAGER_CANCEL ||
                query->signal == GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_CANCEL) &&
                (globus_i_gram_job_manager_script_valid_state_change(
                    request, script_status)))
        {
            request->unsent_status_change = GLOBUS_TRUE;
            globus_gram_job_manager_request_set_status(request, script_status);
            if(request->status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED)
            {
                request->failure_code =
                    GLOBUS_GRAM_PROTOCOL_ERROR_USER_CANCELLED;
                query->failure_code =
                    GLOBUS_GRAM_PROTOCOL_ERROR_USER_CANCELLED;
            }
        }
        else if((query->signal == GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_SUSPEND ||
                query->signal == GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_RESUME) &&
                globus_i_gram_job_manager_script_valid_state_change(
                                                 request,
                                                 script_status))

        {
            globus_gram_job_manager_request_set_status(request, script_status);
            request->unsent_status_change = GLOBUS_TRUE;
        }
    }
    else if(strcmp(variable, "GRAM_SCRIPT_LOG") == 0)
    {
        if (request->job_log_level & GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG)
        {
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                    "event=gram.script.log "
                    "level=DEBUG "
                    "%s "
                    "\n",
                    value);
        }
    }
    else
    {
        query->failure_code =
            GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRIPT_STATUS;
    }

    GlobusGramJobManagerRequestUnlock(request);
}
/* globus_l_gram_job_manager_default_done() */

static
int
globus_l_gram_enqueue_rsl_value(
    globus_fifo_t *                     fifo,
    globus_rsl_value_t *                globus_rsl_value_ptr)
{
    globus_rsl_value_t *                tmp_rsl_value_ptr;
    globus_list_t *                     tmp_rsl_list;
    char *                              prepared;
    char *                              tmp;
    int                                 rc = GLOBUS_SUCCESS;

    if (globus_rsl_value_ptr==NULL) return(0);

    switch (globus_rsl_value_ptr->type)
    {
        case GLOBUS_RSL_VALUE_LITERAL:
            rc = globus_l_gram_enqueue_string(fifo, "'");
            tmp = globus_rsl_value_literal_get_string(globus_rsl_value_ptr);
            prepared = globus_l_gram_job_manager_script_prepare_param(
                        tmp);
            if (!prepared)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

                goto prepare_failed;
            }

            rc = globus_fifo_enqueue(fifo, prepared);
            if (rc != GLOBUS_SUCCESS)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
                free(prepared);

                goto enqueue_failed;
            }
            rc = globus_l_gram_enqueue_string(fifo, "'");

            break;

        case GLOBUS_RSL_VALUE_SEQUENCE:

            tmp_rsl_list = globus_rsl_value_sequence_get_value_list(
                    globus_rsl_value_ptr);

            rc = globus_l_gram_enqueue_string(fifo, "[");
            if (rc != GLOBUS_SUCCESS)
            {
                goto enqueue_failed;
            }

            while (! globus_list_empty(tmp_rsl_list))
            {
                tmp_rsl_value_ptr = (globus_rsl_value_t *) globus_list_first
                     (tmp_rsl_list);
                globus_l_gram_enqueue_rsl_value(fifo, tmp_rsl_value_ptr);

                tmp_rsl_list = globus_list_rest(tmp_rsl_list);
                if(!globus_list_empty(tmp_rsl_list))
                {
                    rc = globus_l_gram_enqueue_string(fifo, ",");

                    if (rc != GLOBUS_SUCCESS)
                    {
                        goto enqueue_failed;
                    }
                }
            }
            rc = globus_l_gram_enqueue_string(fifo, "] ");

            if (rc != GLOBUS_SUCCESS)
            {
                goto enqueue_failed;
            }

            break;

        case GLOBUS_RSL_VALUE_VARIABLE:
        case GLOBUS_RSL_VALUE_CONCATENATION:
        default:
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
            break;
    }

enqueue_failed:
prepare_failed:
    return rc;
}
/* globus_l_gram_enqueue_rsl_value() */

static
int
globus_l_gram_enqueue_rsl(
    globus_fifo_t *                     fifo,
    globus_rsl_t *                      ast_node)
{
    globus_list_t *                     tmp_rsl_list;
    globus_rsl_t *                      tmp_rsl_ptr;
    int                                 rc;

    if (globus_rsl_is_boolean(ast_node))
    {
        tmp_rsl_list = globus_rsl_boolean_get_operand_list(ast_node);

        while (! globus_list_empty(tmp_rsl_list))
        {
            tmp_rsl_ptr = (globus_rsl_t *) globus_list_first
                 (tmp_rsl_list);
            rc = globus_l_gram_enqueue_rsl(fifo, tmp_rsl_ptr);

            tmp_rsl_list = globus_list_rest(tmp_rsl_list);

            if(!globus_list_empty(tmp_rsl_list))
            {
                rc = globus_l_gram_enqueue_string(fifo, ",\n");
            }
            if(rc != GLOBUS_SUCCESS)
            {
                return rc;
            }
        }
    }
    else
    {
        /* Skip these, as they will be over-ridden by the todo lists */
        if((strcmp(globus_rsl_relation_get_attribute(ast_node),
                    "filestagein") == 0) ||
           (strcmp(globus_rsl_relation_get_attribute(ast_node),
                    "filestageinshared") == 0) ||
           (strcmp(globus_rsl_relation_get_attribute(ast_node),
                    "filestageout") == 0) ||
           (strcmp(globus_rsl_relation_get_attribute(ast_node),
                    "filestreamout") == 0))
        {
            return 0;
        }

        rc = globus_l_gram_enqueue_string(
                fifo,
                "    '%s' => ",
                globus_rsl_relation_get_attribute(ast_node));

        rc = globus_l_gram_enqueue_rsl_value(
                fifo,
                globus_rsl_relation_get_value_sequence(ast_node));
        if(rc != GLOBUS_SUCCESS)
        {
            return rc;
        }
    }
    return 0;
}
/* globus_l_gram_enqueue_rsl() */

static
int
globus_l_gram_enqueue_description(
    globus_fifo_t *                     fifo,
    globus_gram_jobmanager_request_t *  request,
    va_list                             ap)
{
    char *                              attribute;
    char                                format;
    char *                              string_value;
    int                                 int_value;
    char *                              prepared;
    int                                 rc;

    rc = globus_l_gram_enqueue_string(
            fifo,
            "$ENV{X509_USER_PROXY} = '%s';\n"
            "$ENV{GLOBUS_GRAM_JOB_CONTACT} = '%s';\n",
            request->x509_user_proxy,
            request->job_contact);

    rc = globus_l_gram_enqueue_string(
            fifo,
            "$description =\n{\n");

    globus_l_gram_enqueue_rsl(fifo, request->rsl);

    /* Other non-rsl or rsl-override attributes */
    for (attribute = va_arg(ap, char *);
         attribute != NULL;
          attribute = va_arg(ap, char *))
    {
        format = (char) va_arg(ap, int);

        switch(format)
        {
          case 's':
            string_value = va_arg(ap, char *);
            if(string_value)
            {
                prepared = globus_l_gram_job_manager_script_prepare_param(
                        string_value);

                rc = globus_l_gram_enqueue_string(
                        fifo,
                        ",\n    '%s' => [ '%s' ]",
                        attribute,
                        prepared);
                free(prepared);
            }
            break;

          case 'i':
          case 'd':
            int_value = va_arg(ap, int);
            rc = globus_l_gram_enqueue_string(
                    fifo,
                    ",\n    '%s' => [ '%d' ]",
                    attribute,
                    int_value);
            break;
        }
    }

    if(request->uniq_id)
    {
        rc = globus_l_gram_enqueue_string(
                fifo,
                ",\n    'uniqid' => [ '%s' ]",
                request->uniq_id);
    }
    if(request->job_id_string)
    {
        rc = globus_l_gram_enqueue_string(
                fifo,
                ",\n    'jobid' => [ '%s' ]",
                request->job_id_string);
    }
    if(request->cache_tag)
    {
        rc = globus_l_gram_enqueue_string(
                fifo,
                ",\n    'cachetag' => [ '%s' ]",
                request->cache_tag);
    }
    if(request->config->condor_os)
    {
        rc = globus_l_gram_enqueue_string(
                fifo,
                ",\n    'condoros' => [ '%s' ]",
                request->config->condor_os);
    }
    if(request->config->condor_arch)
    {
        rc = globus_l_gram_enqueue_string(
                fifo,
                ",\n    'condorarch' => [ '%s' ]",
                request->config->condor_arch);
    }
    if (request->job_dir)
    {
        rc = globus_l_gram_enqueue_string(
                fifo,
                ",\n    'jobdir' => [ '%s' ]",
                request->job_dir);
    }

    rc = globus_l_gram_enqueue_string(
            fifo,
            ",\n    'streamingdisabled' => [ %d ]",
            request->config->streaming_disabled);
    rc = globus_l_gram_enqueue_string(
            fifo,
            ",\n    'streamingrequested' => [ %d ]",
            request->streaming_requested );

    rc = globus_l_gram_enqueue_string(
            fifo,
            ",\n    'statefile' => [ '%s' ]",
            request->job_state_file);
    rc = globus_l_gram_enqueue_staging_list(
            request,
            fifo,
            GLOBUS_GRAM_JOB_MANAGER_STAGE_IN);
    rc = globus_l_gram_enqueue_staging_list(
            request,
            fifo,
            GLOBUS_GRAM_JOB_MANAGER_STAGE_IN_SHARED);
    rc = globus_l_gram_enqueue_staging_list(
            request,
            fifo,
            GLOBUS_GRAM_JOB_MANAGER_STAGE_OUT);
    rc = globus_l_gram_enqueue_staging_list(
            request,
            fifo,
            GLOBUS_GRAM_JOB_MANAGER_STAGE_STREAMS);

    rc = globus_l_gram_enqueue_string(
            fifo,
            "\n};\n");

    return rc;
}
/* globus_l_gram_enqueue_description() */

/**
 * Escape single quotes within a string
 *
 * @param param
 *        Original string to be escaped
 */
static
char *
globus_l_gram_job_manager_script_prepare_param(
    const char *                        param)
{
    int                                 i;
    int                                 j;
    char *                              new_param;

    if (param == NULL)
    {
        return NULL;
    }
    new_param = malloc(strlen(param)*2+1);

    for (i = 0, j = 0; param[i] != '\0'; i++)
    {
        if(param[i] == '\\' )
        {
           new_param[j++] = '\\';
           new_param[j++] = '\\';
        }
        else if (param[i] == '\'')
        {
           new_param[j++] = '\\';
           new_param[j++] = '\'';
        }
        else
        {
           new_param[j++] = param[i];
        }
    }
    new_param[j] = '\0';

    return new_param;
}
/* globus_l_gram_job_manager_script_prepare_param() */

/**
 * Validate that the job manager is properly configured.
 *
 * This function validates the job scripts needed to handle this job
 * request exist and are executable.
 *
 * @param request
 *        The job request we are submitting. This is used to check
 *        that the job manager type is supported by this installation
 *        of the job manager, and for logging.
 *
 * @retval GLOBUS_SUCCESS
 * The job manager is able to submit the job request to the appropriate
 * scripts.
 * @retval GLOBUS_FAILURE
 * The job manager is unable to submit the job request; the request
 * failure code will be updated with the reason why the job couldn't be
 * submitted.
 */
static
int
globus_l_gram_request_validate(
    globus_gram_jobmanager_request_t *  request)
{
    struct stat                         statbuf;
    char *                              script_path;
    char *                              script_path_pattern;
    int                                 rc = GLOBUS_SUCCESS;
    globus_result_t                     result = GLOBUS_SUCCESS;
    static globus_bool_t                first = GLOBUS_TRUE;

    //if (!first)
    {
        return GLOBUS_SUCCESS;
    }

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.request_validate.start "
            "level=TRACE "
            "gramid=%s "
            "\n",
            request->job_contact_path);

    globus_assert(request->config->jobmanager_type);
    if(request->rsl == NULL)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
    }

    /*
     * test that the scheduler script files exist and
     * that the user has permission to execute then.
     */

    /*---------------- job manager script -----------------*/
    result = globus_eval_path("${libexecdir}/globus-job-manager-script.pl", &script_path);
    if (result != GLOBUS_SUCCESS || script_path == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_JM_SCRIPT_NOT_FOUND;
        goto eval_script_path_failed;
    }

    if (stat(script_path, &statbuf) != 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_JM_SCRIPT_NOT_FOUND;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.request_validate.end "
                "level=ERROR "
                "gramid=%s "
                "path=\"%s\" "
                "msg=\"%s\" "
                "status=%d "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                script_path,
                "Script status failed",
                -rc,
                errno,
                strerror(errno));

        goto script_path_not_found;
    }

    if (access(script_path, X_OK) < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_JM_SCRIPT_PERMISSIONS;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.request_validate.end "
                "level=ERROR "
                "gramid=%s "
                "path=\"%s\" "
                "msg=\"%s\" "
                "status=%d "
                "\n",
                request->job_contact_path,
                script_path,
                "Script not executable",
                -rc);

        goto bad_script_permissions;
    }
    free(script_path);
    script_path = NULL;

    script_path_pattern = globus_common_create_string(
            "${perlmoduledir}/Globus/GRAM/JobManager/%s.pm",
            request->config->jobmanager_type);
    if (script_path_pattern == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto script_pattern_alloc_failed;
    }

    /* Verify existence of scheduler specific script.  */
    result = globus_eval_path(script_path_pattern, &script_path);
    if (result != GLOBUS_SUCCESS || script_path == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto lrm_script_path_failed;
    }

    if(stat(script_path, &statbuf) != 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_JM_SCRIPT_NOT_FOUND;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.request_validate.end "
                "level=ERROR "
                "gramid=%s "
                "path=\"%s\" "
                "msg=\"%s\" "
                "status=%d "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                script_path,
                "Module status failed",
                -rc,
                errno,
                strerror(errno));

        goto lrm_module_not_found;
    }

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.request_validate.end "
            "level=TRACE "
            "gramid=%s "
            "status=%d\n",
            request->job_contact_path,
            0);

lrm_module_not_found:
lrm_script_path_failed:
    if (script_path_pattern != NULL)
    {
        free(script_path_pattern);
        script_path_pattern = NULL;
    }
script_pattern_alloc_failed:
bad_script_permissions:
script_path_not_found:
    if (script_path != NULL)
    {
        free(script_path);
        script_path = NULL;
    }
eval_script_path_failed:
    if (rc == GLOBUS_SUCCESS)
    {
        first = GLOBUS_FALSE;
    }
    return rc;
}
/* globus_l_gram_request_validate() */

static
int
globus_l_gram_enqueue_string(
    globus_fifo_t *                     fifo,
    const char *                        format,
    ...)
{
    va_list                             ap;
    char *                              tmp;
    int                                 rc;

    va_start(ap, format);
    tmp = globus_common_v_create_string(format, ap);
    va_end(ap);

    if (!tmp)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto create_string_failed;
    }

    rc = globus_fifo_enqueue(fifo, tmp);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        free(tmp);

        goto fifo_enqueue_failed;
    }

fifo_enqueue_failed:
create_string_failed:
    return rc;
}
/* globus_l_gram_enqueue_string() */

static
int
globus_l_gram_enqueue_staging_list(
    globus_gram_jobmanager_request_t *  request,
    globus_fifo_t *                     fifo,
    globus_gram_job_manager_staging_type_t
                                        type)
{
    globus_list_t *                     tmp_list = NULL;
    char *                              attribute = NULL;
    char *                              from;
    char *                              to;
    globus_gram_job_manager_staging_info_t *
                                        info;
    int                                 rc;

    switch(type)
    {
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_IN:
        tmp_list = request->stage_in_todo;
        attribute = GLOBUS_GRAM_PROTOCOL_FILE_STAGE_IN_PARAM;
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_IN_SHARED:
        tmp_list = request->stage_in_shared_todo;
        attribute = GLOBUS_GRAM_PROTOCOL_FILE_STAGE_IN_SHARED_PARAM;
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_OUT:
        tmp_list = request->stage_out_todo;
        attribute = GLOBUS_GRAM_PROTOCOL_FILE_STAGE_OUT_PARAM;
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_STREAMS:
        tmp_list = request->stage_stream_todo;
        attribute = "filestreamout";
        break;
    }
    /* Always write the attribute to the script arg file, even if
     * it's empty---if we were restarted during staging, then we
     * may have files listed in the original RSL which have been staged
     * completely.
     */

    rc = globus_l_gram_enqueue_string( fifo, ",\n    '%s' => [", attribute);
    if (rc != GLOBUS_SUCCESS)
    {
        goto enqueue_string_failed;
    }

    while(!globus_list_empty(tmp_list))
    {
        info = globus_list_first(tmp_list);
        tmp_list = globus_list_rest(tmp_list);
        from = globus_l_gram_job_manager_script_prepare_param(
                info->evaled_from);
        to  = globus_l_gram_job_manager_script_prepare_param(
                info->evaled_to);

        rc = globus_l_gram_enqueue_string(
                fifo,
                " ['%s', '%s']%s",
                from,
                to,
                globus_list_empty(tmp_list) ? "\n" : ",\n");

        free(from);
        free(to);

        if (rc != GLOBUS_SUCCESS)
        {
            goto enqueue_string_failed;
        }

    }
    rc = globus_l_gram_enqueue_string(fifo, " ]");

enqueue_string_failed:
    return rc;
}
/* globus_l_gram_enqueue_staging_list() */


static
void
globus_l_gram_job_manager_script_staged_done(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_staging_type_t
                                        type,
    const char *                        value)
{
    char *                              from;
    char *                              to;

    from = malloc(strlen(value)+1);
    to = malloc(strlen(value)+1);
    sscanf(value, "%s %s", from, to);

    globus_gram_job_manager_staging_remove(
            request,
            type,
            from,
            to);

    if(request->jobmanager_state != GLOBUS_GRAM_JOB_MANAGER_STATE_STOP)
    {
        globus_gram_job_manager_state_file_write(
                request);
    }

    free(from);
    free(to);
}
/* globus_l_gram_job_manager_script_staged_done() */

globus_bool_t
globus_i_gram_job_manager_script_valid_state_change(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_job_state_t    new_state)
{
    switch(request->status)
    {
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING:
          if(new_state != GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING)
          {
              return GLOBUS_TRUE;
          }
          return GLOBUS_FALSE;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE:
          if(new_state != GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING &&
             new_state != GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE)
          {
              return GLOBUS_TRUE;
          }
          return GLOBUS_FALSE;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED:
          return GLOBUS_FALSE;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE:
          return GLOBUS_FALSE;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_SUSPENDED:
          if(new_state != GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING &&
             new_state != GLOBUS_GRAM_PROTOCOL_JOB_STATE_SUSPENDED)
          {
              return GLOBUS_TRUE;
          }
          return GLOBUS_FALSE;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED:
          if(new_state != GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED)
          {
              return GLOBUS_TRUE;
          }
          return GLOBUS_FALSE;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_IN:
          if(new_state != GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_IN)
          {
              return GLOBUS_TRUE;
          }
          return GLOBUS_FALSE;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_OUT:
          if(new_state == GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE ||
             new_state == GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED)
          {
              return GLOBUS_TRUE;
          }
          return GLOBUS_FALSE;
        default:
          return GLOBUS_FALSE;
    }
}
/* globus_l_gram_job_manager_script_valid_state_change() */

/**
 * Queue and perhaps start a job manager script
 *
 * If the job manager isn't currently running too many simultaneous scripts,
 * start the script described in the @a context parameter. Otherwise, queue
 * it in the manager's script fifo and it will start when another one finishes.
 *
 * @param manager
 *     Manager state
 * @param context
 *     Script context
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Error opening jobmanager script
 */
static
int
globus_l_gram_script_queue(
    globus_gram_job_manager_t *         manager,
    globus_gram_job_manager_script_context_t *
                                        context)
{
    int                                 rc;
    globus_list_t *                     tmp;
    globus_gram_job_manager_scripts_t * scripts = NULL;

    GlobusGramJobManagerLock(manager);
    context->priority.sequence = globus_l_gram_next_script_sequence++;

    tmp = globus_list_search_pred(
                manager->scripts_per_client,
                globus_l_match_script_client_addr,
                context->request->job_stats.client_address
                ? context->request->job_stats.client_address
                : (void *) GLOBUS_GRAM_SCRIPT_NO_CLIENT);

    if (tmp == NULL)
    {
        scripts = malloc(sizeof(globus_gram_job_manager_scripts_t));
        if (scripts == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto scripts_malloc_failed;
        }
        scripts->client_addr = strdup(
            context->request->job_stats.client_address
            ? context->request->job_stats.client_address
            : GLOBUS_GRAM_SCRIPT_NO_CLIENT);
        if (scripts->client_addr == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto scripts_contact_malloc_failed;
        }
        /* Remove port from the client addr */
        {
            char * colon = strrchr(scripts->client_addr, ':');
            if (colon)
            {
                *colon = '\0';
            }
        }

        rc = globus_priority_q_init(
                &scripts->script_queue,
                globus_l_gram_script_priority_cmp);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            goto script_queue_init_failed;
        }

        /* Default number of scripts which can be run simultaneously */
        scripts->script_slots_available = 5;

        rc = globus_fifo_init(&scripts->script_handles);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            goto script_handles_fifo_init_failed;
        }

        globus_list_insert(&manager->scripts_per_client, scripts);

        if (rc != GLOBUS_SUCCESS)
        {
script_handles_fifo_init_failed:
            globus_priority_q_destroy(&scripts->script_queue);
script_queue_init_failed:
            free(scripts->client_addr);
scripts_contact_malloc_failed:
            free(scripts);
            scripts = NULL;
        }
    }
    else
    {
        scripts = globus_list_first(tmp);
    }

    if (scripts)
    {
        rc = globus_priority_q_enqueue(
                &scripts->script_queue,
                context,
                &context->priority);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto fifo_enqueue_failed;
        }

        globus_l_gram_process_script_queue_locked(manager, scripts);
    }

scripts_malloc_failed:
fifo_enqueue_failed:
    GlobusGramJobManagerUnlock(manager);

    return rc;
}
/* globus_l_gram_script_queue() */

/**
 * Start processing queued script commands on XIO handles.
 *
 * For each script context queued in the script fifo, either write its command
 * to an existing XIO handle or create a new XIO handle to process the script,
 * provided there are slots available for running more scripts.
 *
 * The mutex associated with the @a manager parameter must be locked when this
 * procedure is called.
 *
 * @param manager
 *     Job manager state
 * @param scripts
 *     Client-specific script handle collection
 */
static
void
globus_l_gram_process_script_queue_locked(
    globus_gram_job_manager_t *         manager,
    globus_gram_job_manager_scripts_t * scripts)
{
    int                                 rc = GLOBUS_SUCCESS;
    globus_gram_job_manager_script_context_t *
                                        head = NULL;
    globus_result_t                     result;

    while ((!globus_priority_q_empty(&scripts->script_queue)) &&
           (scripts->script_slots_available > 0 ||
            !globus_fifo_empty(&scripts->script_handles)))
    {
        /* head may be non-null if we fail due to an I/O error below (see
         * the "continue" statements
         */
        if (head == NULL)
        {
            head = globus_priority_q_first(&scripts->script_queue);
        }

        /* Prefer to reuse a handle to the script */
        if (!globus_fifo_empty(&scripts->script_handles))
        {
            head->handle = globus_fifo_dequeue(&scripts->script_handles);
            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                    "event=gram.script.info "
                    "level=DEBUG "
                    "msg=\"%s\" "
                    "handle=%p "
                    "\n",
                    "Using script handle from fifo",
                    head->handle);
            rc = globus_l_gram_script_register_read_and_write(head);
            if (rc != GLOBUS_SUCCESS)
            {
                rc = GLOBUS_SUCCESS;
                continue;
            }
        }
        else
        {
            /* Create a new script if more slots are available */
            assert(scripts->script_slots_available > 0);
            rc = globus_gram_job_manager_script_handle_init(
                    manager,
                    scripts,
                    &head->handle);
            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                    "event=gram.script.info "
                    "level=DEBUG "
                    "msg=\"%s\" "
                    "handle=%p "
                    "rc=%d "
                    "\n",
                    "Created new script handle",
                    head->handle,
                    -rc);
            if (rc != GLOBUS_SUCCESS)
            {
                continue;
            }

            result = globus_xio_register_open(
                head->handle->handle,
                NULL,
                manager->script_attr,
                globus_l_gram_script_open_callback,
                head);

            if (result != GLOBUS_SUCCESS)
            {
                /* I think blocking call is safe here */
                globus_xio_close(head->handle->handle, NULL);
                head->handle->handle = NULL;
                continue;
            }

            head->handle->pending_ops++;
            scripts->script_slots_available--;
        }
        globus_priority_q_dequeue(&scripts->script_queue);
        head = NULL;
    }

    return;
}
/* globus_l_gram_process_script_queue_locked() */

static
void
globus_l_gram_script_open_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_gram_job_manager_script_context_t *
                                        context = user_arg;
    globus_gram_script_handle_t         script_handle = context->handle;
    globus_gram_jobmanager_request_t *  request = context->request;
    int                                 rc = GLOBUS_SUCCESS;
    int                                 i;
    globus_gram_job_manager_scripts_t * scripts;

    script_handle->pending_ops--;

    if (result == GLOBUS_SUCCESS)
    {
        rc = globus_l_gram_script_register_read_and_write(context);
    }
    else
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_JOBMANAGER_SCRIPT;
    }


    if (rc != GLOBUS_SUCCESS)
    {
        GlobusGramJobManagerLock(script_handle->manager);
        scripts = globus_list_first(
                globus_list_search_pred(
                        request->manager->scripts_per_client,
                        globus_l_match_script_client_addr,
                        request->job_stats.client_address
                        ? request->job_stats.client_address
                        : (void *) GLOBUS_GRAM_SCRIPT_NO_CLIENT));

        globus_xio_register_close(
                handle,
                NULL,
                globus_l_script_close_callback,
                script_handle);
        scripts->script_slots_available++;
        GlobusGramJobManagerUnlock(script_handle->manager);

        context->callback(
            context->callback_arg,
            request,
            GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRIPT_STATUS,
            context->starting_jobmanager_state,
            NULL,
            NULL);

        globus_gram_job_manager_remove_reference(
                request->manager,
                request->job_contact_path,
                "script");

        for (i = 0; i < context->iovcnt; i++)
        {
            free(context->iov[i].iov_base);
        }
        free(context->iov);
        free(context);
    }
}
/* globus_l_gram_script_open_callback() */

static
int
globus_l_gram_script_register_read_and_write(
    globus_gram_job_manager_script_context_t *
                                        script_context)
{
    int                                 i, total_iov_contents;
    globus_result_t                     result;
    globus_gram_job_manager_t *         manager;
    globus_gram_jobmanager_request_t *  request = script_context->request;
    globus_gram_job_manager_scripts_t * scripts;

    manager = script_context->handle->manager;

    scripts = globus_list_first(
            globus_list_search_pred(
                manager->scripts_per_client,
                globus_l_match_script_client_addr,
                request->job_stats.client_address
                ? request->job_stats.client_address
                : (void *) GLOBUS_GRAM_SCRIPT_NO_CLIENT));

    for (i = 0, total_iov_contents = 0; i < script_context->iovcnt; i++)
    {
        total_iov_contents += script_context->iov[i].iov_len;
    }
    result = globus_xio_register_writev(
            script_context->handle->handle,
            script_context->iov,
            script_context->iovcnt,
            total_iov_contents,
            NULL,
            globus_l_script_writev_callback,
            script_context->handle);
    if (result != GLOBUS_SUCCESS)
    {
        char *errstr = globus_error_print_friendly(
                globus_error_peek(result));
        char *escaped_errstr;

        escaped_errstr = globus_gram_prepare_log_string(errstr);

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.script.write.end level=ERROR "
                "msg=\"%s\" status=%d reason=\"%s\"\n",
                "error writing to script input",
                -1,
                escaped_errstr ? escaped_errstr : "");
        if (errstr)
        {
            free(errstr);
        }
        if (escaped_errstr)
        {
            free(escaped_errstr);
        }
        globus_xio_register_close(
                script_context->handle->handle,
                NULL,
                globus_l_script_close_callback,
                script_context->handle);
        script_context->handle = NULL;
        scripts->script_slots_available++;
        return GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_JOBMANAGER_SCRIPT;
    }
    script_context->handle->pending_ops++;

    result = globus_xio_register_read(
            script_context->handle->handle,
            script_context->handle->return_buf,
            sizeof(script_context->handle->return_buf),
            1,
            NULL,
            globus_l_gram_job_manager_script_read,
            script_context);
    if (result != GLOBUS_SUCCESS)
    {
        globus_xio_register_close(
                script_context->handle->handle,
                NULL,
                globus_l_script_close_callback,
                script_context->handle);
        script_context->handle = NULL;
        scripts->script_slots_available++;

        return GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_JOBMANAGER_SCRIPT;
    }
    script_context->handle->pending_ops++;

    return GLOBUS_SUCCESS;
}
/**
 * Convert a fifo of NULL-terminated strings into an array of iovec structs
 *
 * @param fifo
 *     Fifo of strings. All strings will be removed from this fifo, but it
 *     will not be destroyed.
 * @param iov
 *     Pointer to an array of iovec structs. A new array will be allocated
 *     to hold the values of @a fifo.
 * @param num_iov
 *     Pointer to the number of iovec structs in the resulting string.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed
 */
static
int
globus_l_gram_fifo_to_iovec(
    globus_fifo_t *                     fifo,
    struct iovec **                     iov,
    int *                               num_iov)
{
    globus_list_t                       *list;
    size_t                              len;
    char *                              str;
    int                                 i;
    int                                 rc = GLOBUS_SUCCESS;

    len = globus_fifo_size(fifo);
    list = globus_fifo_convert_to_list(fifo);

    *iov = malloc(len * sizeof(struct iovec));
    if (*iov == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto malloc_iov_failed;
    }
    *num_iov = len;

    i = 0;
    for (; list != NULL; i++)
    {
        str = globus_list_remove(&list, list);
        (*iov)[i].iov_base = str;
        (*iov)[i].iov_len = strlen(str);
    }

    if (rc != GLOBUS_SUCCESS)
    {
malloc_iov_failed:
        *iov = NULL;
        *num_iov = 0;
    }

    return rc;
}
/* globus_l_gram_fifo_to_iovec() */


/**
 * Finished processing a script, start another if one is queued
 *
 * @param manager
 *     Job manager state
 *
 * @return void
 */
static
void
globus_l_gram_job_manager_script_done(
    globus_gram_job_manager_t *         manager,
    globus_gram_job_manager_scripts_t * scripts,
    globus_gram_script_handle_t         handle)
{
    if (handle->pending_ops > 0)
    {
        return;
    }
    if (handle->result == GLOBUS_SUCCESS)
    {
        handle->last_use = time(NULL);
        globus_fifo_enqueue(&scripts->script_handles, handle);
    }
    else
    {
        globus_xio_register_close(
                handle->handle,
                NULL,
                globus_l_script_close_callback,
                handle);
        scripts->script_slots_available++;
    }

    globus_l_gram_process_script_queue_locked(manager, scripts);

    return;
}
/* globus_l_gram_job_manager_script_done() */


void
globus_gram_job_manager_script_close_all(
    globus_gram_job_manager_t *         manager)
{
    globus_gram_script_handle_t         handle;
    globus_list_t *                     tmp;
    globus_gram_job_manager_scripts_t * scripts;

    for (tmp = manager->scripts_per_client; tmp != NULL; tmp = globus_list_rest(tmp))
    {
        scripts = globus_list_first(tmp);

        while (!globus_fifo_empty(&scripts->script_handles))
        {
            handle = globus_fifo_dequeue(&scripts->script_handles);

            globus_xio_close(
                    handle->handle,
                    NULL);
            free(handle);
            scripts->script_slots_available++;
        }
    }
}
/* globus_gram_job_manager_script_close_all() */

int
globus_gram_job_manager_script_handle_init(
    globus_gram_job_manager_t *         manager,
    globus_gram_job_manager_scripts_t * scripts,
    globus_gram_script_handle_t *       handle)
{
    globus_result_t                     result;
    int                                 rc = GLOBUS_SUCCESS;

    *handle = malloc(sizeof(struct globus_gram_script_handle_s));
    if (*handle == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto fail;
    }
    (*handle)->scripts = scripts;
    (*handle)->return_buf[0] = 0;
    (*handle)->return_buf_offset = 0;
    (*handle)->result = GLOBUS_SUCCESS;
    (*handle)->manager = manager;
    (*handle)->pending_ops = 0;
    (*handle)->last_use = time(NULL);

    result = globus_xio_handle_create(
            &(*handle)->handle,
            globus_i_gram_job_manager_popen_stack);
    if (result != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_JOBMANAGER_SCRIPT;
        goto handle_create_failed;
    }

    if (rc != GLOBUS_SUCCESS)
    {
handle_create_failed:
        free(*handle);
    }
fail:
    if (rc != GLOBUS_SUCCESS)
    {
        *handle = NULL;
    }
    return rc;
}
/* globus_gram_job_manager_script_handle_init() */

static
void
globus_l_script_close_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    free(user_arg);
}
/* globus_l_script_close_callback() */

static
void
globus_l_script_writev_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_gram_script_handle_t         script_handle = user_arg;

    globus_gram_job_manager_log(
            script_handle->manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.script_write.end "
            "level=DEBUG "
            "msg=\"%s\" "
            "nbytes=%d "
            "result=%d "
            "pending_ops=%d "
            "\n ",
            "writev callback",
            (int) nbytes,
            (int) result,
            script_handle->pending_ops);
    GlobusGramJobManagerLock(script_handle->manager);
    if (script_handle->result == GLOBUS_SUCCESS)
    {
        script_handle->result = result;
    }
    script_handle->pending_ops--;
    globus_l_gram_job_manager_script_done(
            script_handle->manager,
            script_handle->scripts,
            script_handle);
    GlobusGramJobManagerUnlock(script_handle->manager);
}

/**
 * Periodic callback to close idle script handles
 *
 * If a script handle hasn't been used in over 30 seconds, and there are
 * no pending script events, it will be closed by this callback and removed
 * from the script handle fifo.
 */
void
globus_gram_script_close_idle(
    void *                              arg)
{
    globus_gram_job_manager_t *         manager = arg;
    globus_gram_script_handle_t         handle;
    time_t                              now = time(NULL);
    globus_list_t *                     tmp;
    globus_gram_job_manager_scripts_t * scripts;

    GlobusGramJobManagerLock(manager);

    for (tmp = manager->scripts_per_client; tmp != NULL; tmp = globus_list_rest(tmp))
    {
        scripts = globus_list_first(tmp);

        if (!globus_priority_q_empty(&scripts->script_queue))
        {
            goto nonempty_queue;
        }

        while (!globus_fifo_empty(&scripts->script_handles))
        {
            handle = globus_fifo_peek(&scripts->script_handles);

            if (difftime(now, handle->last_use) > 30.0)
            {
                globus_gram_job_manager_log(
                        manager,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                        "event=gram.script_idle.info "
                        "level=DEBUG "
                        "msg=\"closing idle script handle after %.1f seconds"
                        "\n ",
                        difftime(now, handle->last_use));

                globus_fifo_dequeue(&scripts->script_handles);
                globus_xio_close(
                        handle->handle,
                        NULL);
                free(handle);
                scripts->script_slots_available++;
            }
            else
            {
                break;
            }
        }
nonempty_queue:
        ;
    }
    GlobusGramJobManagerUnlock(manager);
}
/* globus_gram_script_close_idle() */

static
int
globus_l_gram_script_priority_cmp(
    void *                              priority_1,
    void *                              priority_2)
{
    globus_gram_script_priority_t      *p1 = priority_1, *p2 = priority_2;

    if (p1->priority_level > p2->priority_level)
    {
        return 1;
    }
    else if (p1->priority_level < p2->priority_level)
    {
        return -1;
    }
    else if (p1->sequence > p2->sequence)
    {
        return 2;
    }
    else
    {
        assert(p1->sequence < p2->sequence);
        return -2;
    }
}
/* globus_l_gram_script_priority_cmp() */

static
int
globus_l_match_script_client_addr(
    void *                              datum,
    void *                              arg)
{
    globus_gram_job_manager_scripts_t * scripts = datum;
    char *                              client_addr = arg;

    return strncmp(scripts->client_addr, client_addr, strlen(scripts->client_addr)) == 0;
}
/* globus_l_match_script_client_addr() */
