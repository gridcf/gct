/*
 * Copyright 1999-2010 University of Chicago
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
#include "globus_gram_job_manager.h"
#include "globus_scheduler_event_generator.h"
#include "globus_scheduler_event_generator_app.h"

#include <sys/types.h>
#include <utime.h>
#include <regex.h>

typedef struct globus_gram_seg_resume_s
{
    globus_gram_job_manager_t *         manager;
    globus_list_t *                     events;
}
globus_gram_seg_resume_t;

static globus_bool_t globus_l_condor_regexes_compiled = GLOBUS_FALSE;
static regex_t globus_l_condor_outer_re;
static regex_t globus_l_condor_inner_re;

globus_result_t
globus_l_gram_seg_event_callback(
    void *                              user_arg,
    const globus_scheduler_event_t *    event);

static
void
globus_l_gram_fork_poll_callback(
    void *                              user_arg);

static
int
globus_l_gram_deliver_event(
    globus_gram_jobmanager_request_t *  request,
    globus_scheduler_event_t *          event);

static
void
globus_l_seg_resume_callback(
    void *                              user_arg);


static
int
globus_l_condor_parse_log(
    const char *                        data,
    globus_gram_job_manager_ref_t *     ref,
    globus_fifo_t *                     events);

static
int
globus_l_condor_read_log(
    globus_gram_job_manager_t          *manager,
    const char                         *path,
    size_t                              last_size,
    char                              **data);

static
void
globus_l_gram_condor_poll_callback(
    void *                              user_arg);

globus_result_t
globus_gram_job_manager_init_seg(
    globus_gram_job_manager_t *         manager)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 rc;

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.seg.start level=TRACE module=%s\n",
            manager->config->seg_module ? manager->config->seg_module : "fork");

    GlobusGramJobManagerLock(manager);
    if (manager->config->seg_module == NULL &&
        strcmp(manager->config->jobmanager_type, "fork") == 0)
    {
        globus_reltime_t                delay;

        GlobusTimeReltimeSet(delay, 1, 0);

        result = globus_callback_register_periodic(
                &manager->fork_callback_handle,
                &delay,
                &delay,
                globus_l_gram_fork_poll_callback,
                manager);
        if (result != GLOBUS_SUCCESS)
        {
            char *                      errstr;
            char *                      errstr_escaped;
            errstr = globus_error_print_friendly(globus_error_peek(result));

            errstr_escaped = globus_gram_prepare_log_string(
                    errstr);

            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.seg.end level=WARN status=%d "
                    "reason=\"%s\"\n",
                    -1,
                    errstr_escaped ? errstr_escaped : "");

            if (errstr_escaped)
            {
                free(errstr_escaped);
            }
            if (errstr)
            {
                free(errstr);
            }
            goto failed_periodic;
        }
    }
    else if (strcmp(manager->config->jobmanager_type, "condor") == 0)
    {
        globus_reltime_t                delay;

        GlobusTimeReltimeSet(delay, 5, 0);

        result = globus_callback_register_periodic(
                &manager->fork_callback_handle,
                &delay,
                &delay,
                globus_l_gram_condor_poll_callback,
                manager);
        if (result != GLOBUS_SUCCESS)
        {
            char *                      errstr;
            char *                      errstr_escaped;
            errstr = globus_error_print_friendly(globus_error_peek(result));

            errstr_escaped = globus_gram_prepare_log_string(
                    errstr);

            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.seg.end level=WARN status=%d "
                    "reason=\"%s\"\n",
                    -1,
                    errstr_escaped ? errstr_escaped : "");

            if (errstr_escaped)
            {
                free(errstr_escaped);
            }
            if (errstr)
            {
                free(errstr);
            }
            goto failed_periodic;
        }
    }
    else
    {
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                "event=gram.seg.activate.start level=TRACE module=%s\n",
                manager->config->seg_module);

        rc = globus_module_activate(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE);
        if (rc != GLOBUS_SUCCESS)
        {
            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.seg.activate.end level=ERROR error=%d "
                    "reason=\"Error activating SEG\"\n",
                    rc);
            goto failed_activate;
        }

        globus_scheduler_event_generator_set_event_handler(
                globus_l_gram_seg_event_callback,
                manager);
        globus_scheduler_event_generator_set_timestamp(
                manager->seg_last_timestamp);
        setenv("JOB_MANAGER_SEG_SCHEDULER", manager->config->seg_module, 1);
        rc = globus_scheduler_event_generator_load_module(
                "job_manager");
        if (rc != GLOBUS_SUCCESS)
        {
            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.seg.end level=ERROR "
                    "error=%d "
                    "module=%s "
                    "reason=\"Error loading job_manager SEG "
                    "module\"\n",
                    rc,
                    manager->config->seg_module);
            free(manager->config->seg_module);
            manager->config->seg_module = NULL;
            goto failed_load;
        }
    }
    manager->seg_started = GLOBUS_TRUE;
failed_load:
failed_activate:
failed_periodic:
    GlobusGramJobManagerUnlock(manager);

    return result;
}
/* globus_gram_job_manager_init_seg() */

globus_result_t
globus_gram_job_manager_shutdown_seg(
    globus_gram_job_manager_t *         manager)
{
    if (! manager->seg_started)
    {
        return GLOBUS_SUCCESS;
    }

    if (manager->fork_callback_handle != GLOBUS_NULL_HANDLE)
    {
        globus_callback_unregister(
                manager->fork_callback_handle,
                NULL,
                NULL,
                NULL);
        manager->fork_callback_handle = GLOBUS_NULL_HANDLE;
    }
    else
    {
        globus_module_deactivate(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE);
    }
    manager->seg_started = GLOBUS_FALSE;
    return GLOBUS_SUCCESS;
}
/* globus_gram_job_manager_shutdown_seg() */

globus_result_t
globus_l_gram_seg_event_callback(
    void *                              user_arg,
    const globus_scheduler_event_t *    event)
{
    int                                 rc;
    globus_gram_job_manager_t *         manager = user_arg;
    globus_gram_jobmanager_request_t *  request;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_scheduler_event_t *          new_event;

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.seg.event.start level=TRACE segid=\"%s\" "
            "state=%d event_ts=%ld\n",
            event->job_id,
            (int) event->event_type,
            (long int) event->timestamp);
    if (event->event_type == GLOBUS_SCHEDULER_EVENT_RAW)
    {
        rc = GLOBUS_SUCCESS;
        goto raw_event;
    }

    result = globus_scheduler_event_copy(&new_event, event);
    if (result != GLOBUS_SUCCESS)
    {
        goto copy_failed;
    }

    GlobusGramJobManagerLock(manager);
    /* Find the job request associated by this job id */
    rc = globus_gram_job_manager_add_reference_by_jobid(
            manager,
            event->job_id,
            "SEG event",
            &request);

    if (rc != GLOBUS_SUCCESS)
    {
        if (manager->seg_pause_count > 0)
        {
            /* New submit script is running. Avoid race by adding this to the
             * manager-wide queue
             */
            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                    "event=gram.seg.event.queue level=TRACE segid=\"%s\"\n",
                    event->job_id);
            rc = globus_fifo_enqueue(&manager->seg_event_queue, new_event);
        }
        else
        {
            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                    "event=gram.seg.event.end level=TRACE segid=\"%s\" "
                    "reason=\"Event ID doesn't match known job id\"\n",
                    event->job_id);
        }
    }
    if (rc != GLOBUS_SUCCESS)
    {
        GlobusGramJobManagerUnlock(manager);
        goto manager_event_queue_failed;
    }
    else if (request == NULL)
    {
        /* Ignore unwanted event */
        GlobusGramJobManagerUnlock(manager);
        goto done;
    }
    else
    {
        if (event->timestamp > manager->seg_last_timestamp)
        {
            manager->seg_last_timestamp = event->timestamp;
        }
        GlobusGramJobManagerUnlock(manager);

        rc = globus_l_gram_deliver_event(
                request,
                new_event);
    }

    if (rc != GLOBUS_SUCCESS)
    {
        (void) globus_gram_job_manager_remove_reference(
                request->manager,
                request->job_contact_path,
                "SEG event");
manager_event_queue_failed:
        globus_scheduler_event_destroy(new_event);
copy_failed:
raw_event:
        ;
    }
done:
    result = GLOBUS_SUCCESS;
    return result;
}
/* globus_l_gram_seg_event_callback() */

void
globus_gram_job_manager_seg_handle_event(
    globus_gram_jobmanager_request_t *  request)
{
    globus_scheduler_event_t *          event;
    char *                              subjob_id_ptr = NULL;
    size_t                              subjob_id_len;
    globus_bool_t                       found_subjob_id;

    event = globus_fifo_dequeue(&request->seg_event_queue);

    if (event->timestamp > request->seg_last_timestamp)
    {
        /*
         * GRAM-145: GRAM5 Job Manager fails to save SEG timestamps in job
         * state files
         *
         * We'll update the SEG timestamp here so that if the job manager
         * is restarted it (potentially) ignore events that have already been
         * noticed in the job state file.
         */
        request->seg_last_timestamp = event->timestamp;
    }

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.handle_seg_event.start "
            "level=DEBUG "
            "state=%d "
            "gramid=%s "
            "jobid=\"%s\" "
            "\n",
            event->event_type,
            request->job_contact_path,
            event->job_id);

    found_subjob_id = GLOBUS_FALSE;
    subjob_id_len = strlen(event->job_id);
    while (!found_subjob_id)
    {
        subjob_id_ptr = strstr(request->job_id_string, event->job_id);
        if (subjob_id_ptr == NULL)
	{
	    break;
	}

        if (subjob_id_ptr == request->job_id_string ||
            (*(subjob_id_ptr - 1) == ','))
        {
            /* request->job_id_string starts with this subjob_id, or this
             * subjob_id happens after a comma. If it ends with a comma or
             * \0, then we've found a match.
             */
            if (subjob_id_ptr[subjob_id_len] == ',')
            {
                found_subjob_id = GLOBUS_TRUE;

                if (event->event_type == GLOBUS_SCHEDULER_EVENT_DONE ||
                    event->event_type == GLOBUS_SCHEDULER_EVENT_FAILED)
                {
                    /* Remove this sub job id from the list by moving
                     * after the comma up until \0 to subjob_id_ptr
                     */
                    memmove(subjob_id_ptr,
                            subjob_id_ptr + subjob_id_len + 1,
                            strlen(subjob_id_ptr + subjob_id_len + 1) + 1);
                }
            }
            else if (subjob_id_ptr[subjob_id_len] == 0)
            {
                /* This is the final subjob in the job_id_string */
                found_subjob_id = GLOBUS_TRUE;
                if (event->event_type == GLOBUS_SCHEDULER_EVENT_DONE ||
                    event->event_type == GLOBUS_SCHEDULER_EVENT_FAILED)
                {
                    /* Don't need to do memmove here, just null terminate at
                     * either the initial part of the string if subjob_id is
                     * the only one in the list, or at the comma otherwise
                     */
                    if (subjob_id_ptr != request->job_id_string)
                    {
                        *(subjob_id_ptr - 1) = '\0';
                    }
                    else
                    {
                        request->job_id_string[0] = '\0';
                    }
                }
            }
        }
    }

    /* If this is a terminal event (done or failed), we'll update the expected
     * terminal state (in the case of a multi-subjob case) and the exit code
     * if the job's exit code is currently 0
     *
     * Thus, if any subjob fails or exits with a non-0 exit code, we will
     * propogate that in the job state change notification.
     */
    if (event->event_type == GLOBUS_SCHEDULER_EVENT_DONE ||
        event->event_type == GLOBUS_SCHEDULER_EVENT_FAILED)
    {
        if (request->expected_terminal_state ==
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE && 
            event->event_type == GLOBUS_SCHEDULER_EVENT_FAILED)
        {
            request->expected_terminal_state =
                    GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED;
        }
        if (event->event_type == GLOBUS_SCHEDULER_EVENT_DONE &&
            request->exit_code == 0 &&
            event->exit_code != 0)
        {
            request->exit_code = event->exit_code;
        }
    }

    /* If the last job terminated or any job moved to active, we'll update the
     * job status and potentially send notifications.
     */
    if (event->event_type != GLOBUS_SCHEDULER_EVENT_DONE &&
         event->event_type != GLOBUS_SCHEDULER_EVENT_FAILED)
    {
        int protocol_event_type;

        switch (event->event_type)
        {
            case GLOBUS_SCHEDULER_EVENT_PENDING:
                protocol_event_type = GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING;
                break;
            case GLOBUS_SCHEDULER_EVENT_ACTIVE:
                protocol_event_type = GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE;
                break;
            default:
                protocol_event_type = -1;
        }

        if (protocol_event_type != -1)
        {
            if (globus_i_gram_job_manager_script_valid_state_change(
                        request,
                        protocol_event_type))
            {
                globus_gram_job_manager_request_set_status(
                        request,
                        protocol_event_type);
                request->unsent_status_change = GLOBUS_TRUE;
            }
        }
    }
    else if (*request->job_id_string == '\0')
    {
        if (globus_i_gram_job_manager_script_valid_state_change(
                    request,
                    request->expected_terminal_state))
        {
            if ((request->expected_terminal_state ==
                    GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE) &&
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
                        request->expected_terminal_state);
            }
            request->unsent_status_change = GLOBUS_TRUE;
        }
    }

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.handle_seg_event.end "
            "level=DEBUG "
            "state=%d "
            "gramid=%s "
            "jobid=\"%s\" "
            "\n",
            event->event_type,
            request->job_contact_path,
            event->job_id);

    globus_scheduler_event_destroy(event);

    (void) globus_gram_job_manager_remove_reference(
            request->manager,
            request->job_contact_path,
            "SEG event");
}
/* globus_gram_job_manager_seg_handle_event() */

void
globus_gram_job_manager_seg_pause(
    globus_gram_job_manager_t *         manager)
{
    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.seg_pause.start "
            "level=TRACE "
            "count=%d "
            "\n",
            manager->seg_pause_count+1);

    GlobusGramJobManagerLock(manager);
    manager->seg_pause_count++;
    GlobusGramJobManagerUnlock(manager);

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.seg_pause.end "
            "level=TRACE "
            "count=%d "
            "\n",
            manager->seg_pause_count);
}
/* globus_gram_job_manager_seg_pause() */

void
globus_gram_job_manager_seg_resume(
    globus_gram_job_manager_t *         manager)
{
    globus_result_t                     result;
    globus_scheduler_event_t *          event;
    globus_gram_seg_resume_t *          resume;

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.seg_resume.start "
            "level=TRACE "
            "count=%d "
            "\n",
            manager->seg_pause_count-1);

    GlobusGramJobManagerLock(manager);
    manager->seg_pause_count--;

    if (manager->seg_pause_count == 0 &&
        !globus_fifo_empty(&manager->seg_event_queue))
    {
        resume = malloc(sizeof(globus_gram_seg_resume_t));
        if (resume != NULL)
        {
            globus_reltime_t            delay;

            GlobusTimeReltimeSet(delay, 0, 0);

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                "event=gram.seg_resume.info "
                "level=TRACE "
                "message=\"%s\" "
                "event_count=%d "
                "\n",
                "Creating resume callback struct",
                globus_fifo_size(&manager->seg_event_queue));

            resume->manager = manager;
            resume->events = globus_fifo_convert_to_list(
                    &manager->seg_event_queue);

            result = globus_callback_register_oneshot(
                    NULL,
                    &delay,
                    globus_l_seg_resume_callback,
                    resume);
            if (result != GLOBUS_SUCCESS)
            {
                while (!globus_list_empty(resume->events))
                {
                    event = globus_list_remove(&resume->events, resume->events);

                    globus_scheduler_event_destroy(event);
                }
            }
        }
    }
    GlobusGramJobManagerUnlock(manager);
    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.seg_resume.end "
            "level=TRACE "
            "count=%d "
            "\n",
            manager->seg_pause_count);
}
/* globus_gram_job_manager_seg_resume() */

static
void
globus_l_seg_resume_callback(
    void *                              user_arg)
{
    globus_gram_seg_resume_t *          resume = user_arg;
    globus_scheduler_event_t *          event;
    globus_gram_jobmanager_request_t *  request;
    int                                 rc;

    globus_gram_job_manager_log(
            NULL,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.seg.resume_callback.start "
            "level=TRACE "
            "\n");

    while (!globus_list_empty(resume->events))
    {
        event = globus_list_remove(&resume->events, resume->events);

        GlobusGramJobManagerLock(resume->manager);
        rc = globus_gram_job_manager_add_reference_by_jobid(
                resume->manager,
                event->job_id,
                "SEG event",
                &request);
        if (rc != GLOBUS_SUCCESS)
        {
            globus_gram_job_manager_log(
                    NULL,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                    "event=gram.seg.resume_callback.end "
                    "level=TRACE "
                    "status=%d "
                    "msg=\"%s\" "
                    "\n",
                    0,
                    "Ignoring unknown job id");
            GlobusGramJobManagerUnlock(resume->manager);
            globus_scheduler_event_destroy(event);
        }
        else
        {
            if (event->timestamp > request->manager->seg_last_timestamp)
            {
                request->manager->seg_last_timestamp = event->timestamp;
            }
            GlobusGramJobManagerUnlock(resume->manager);
            rc = globus_l_gram_deliver_event(
                    request,
                    event);

            globus_gram_job_manager_log(
                    NULL,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                    "event=gram.seg.resume_callback.end "
                    "level=TRACE "
                    "status=%d "
                    "msg=\"%s\" "
                    "\n",
                    -rc,
                    "Delivered event");
        }
    }
}
/* globus_l_seg_resume_callback() */

static
int
globus_l_gram_deliver_event(
    globus_gram_jobmanager_request_t *  request,
    globus_scheduler_event_t *          event)
{
    int                                 rc;
    globus_reltime_t                    delay_time;

    GlobusGramJobManagerRequestLock(request);

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.seg_deliver_event.start "
            "level=TRACE "
            "gramid=%s "
            "jobid=\"%s\" "
            "state=%d "
            "jmstate=%s\n",
            request->job_contact_path,
            event->job_id,
            event->event_type,
            globus_i_gram_job_manager_state_strings[
                request->jobmanager_state]);

    /* Keep the state file's timestamp up to date so that
     * anything scrubbing the state files of old and dead
     * processes leaves it alone */
    if(request->job_state_file)
    {
        utime(request->job_state_file, NULL);
    }

    rc = globus_fifo_enqueue(&request->seg_event_queue, event);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.seg_deliver_event.end "
                "level=ERROR "
                "gramid=%s "
                "jobid=\"%s\" "
                "state=%d "
                "jmstate=%s "
                "status=%d "
                "msg=\"%s\" "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                event->job_id,
                event->event_type,
                globus_i_gram_job_manager_state_strings[
                        request->jobmanager_state],
                -rc,
                "Fifo enqueue failed",
                globus_gram_protocol_error_string(rc));

        goto event_enqueue_failed;
    }

    if (event->event_type == GLOBUS_SCHEDULER_EVENT_DONE ||
        event->event_type ==  GLOBUS_SCHEDULER_EVENT_FAILED)
    {
        (void) globus_gram_job_manager_unregister_job_id(
                request->manager,
                event->job_id);
    }

    if (request->jobmanager_state == GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2)
    {
        GlobusTimeReltimeSet(delay_time, 0, 0); 

        request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1;

        rc = globus_gram_job_manager_state_machine_register(
                request->manager,
                request,
                &delay_time);

        if (rc != GLOBUS_SUCCESS)
        {
            request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2;
        }
    }
    rc = GLOBUS_SUCCESS;

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.seg_deliver_event.end "
            "level=TRACE "
            "gramid=%s "
            "jobid=\"%s\" "
            "state=%d "
            "jmstate=%s "
            "status=%d "
            "\n",
            request->job_contact_path,
            event->job_id,
            event->event_type,
            globus_i_gram_job_manager_state_strings[request->jobmanager_state],
            0);

event_enqueue_failed:
    GlobusGramJobManagerRequestUnlock(request);

    return rc;
}
/* globus_l_gram_deliver_event() */

static
void
globus_l_gram_fork_poll_callback(
    void *                              user_arg)
{
    int                                 rc;
    globus_gram_job_manager_t *         manager = user_arg;
    globus_list_t *                     l;
    globus_scheduler_event_t *          event;
    globus_list_t *                     events = NULL;
    globus_gram_jobmanager_request_t *  request;
    int                                 pid_count = 0;
    int                                 done_count = 0;
    globus_list_t *                     job_id_list;
    globus_list_t *                     tmp;

    /* Walk the job id list, checking to see if the process has completed */
    rc = globus_gram_job_manager_get_job_id_list(
            manager,
            &job_id_list);

    for (tmp = job_id_list; tmp != NULL; tmp = globus_list_rest(tmp))
    {
        char *                          tok_end = NULL;
        char *                          pid_string;
        char *                          job_id_string;
        char *                          job_id_string_copy;

        job_id_string = globus_list_first(tmp);
        job_id_string_copy = strdup(job_id_string);
        if (job_id_string_copy == NULL)
        {
            continue;
        }

        pid_count = 0;
        done_count = 0;
        for (tok_end = NULL,
                    pid_string = strtok_r(job_id_string, ",", &tok_end);
             pid_string != NULL;
             pid_string = strtok_r(NULL, ",", &tok_end))
        {
            char *                      end = NULL;
            unsigned long               pid;

            pid_count++;
            errno = 0;
            pid = strtoul(pid_string, &end, 10);
            if ((pid == ULONG_MAX && errno != 0) || strlen(end) != 0)
            {
                continue;
            }

            if (kill((pid_t) pid, 0) < 0)
            {
                done_count++;
            }
        }

        if (pid_count == done_count && pid_count > 0)
        {
            /* Synthesize done event */
            event = malloc(sizeof(globus_scheduler_event_t));

            event->event_type = GLOBUS_SCHEDULER_EVENT_DONE;
            event->job_id = job_id_string_copy;
            event->timestamp = time(NULL);
            event->exit_code = 0;
            event->failure_code = 0;
            event->raw_event = NULL;

            globus_list_insert(&events, event);
        }
        else
        {
            free(job_id_string_copy);
        }
        free(job_id_string);
    }
    globus_list_free(job_id_list);

    /* Queue events in the request-specific SEG event queue */
    for (l = events; l != NULL; l = globus_list_rest(l))
    {

        event = globus_list_first(l);

        GlobusGramJobManagerLock(manager);
        rc = globus_gram_job_manager_add_reference_by_jobid(
                manager,
                event->job_id,
                "SEG event",
                &request);
        GlobusGramJobManagerUnlock(manager);

        if (rc == GLOBUS_SUCCESS)
        {
            rc = globus_l_gram_deliver_event(
                    request,
                    event);

            if (rc != GLOBUS_SUCCESS)
            {
                goto destroy_event;
            }
        }

        if (rc != GLOBUS_SUCCESS)
        {
destroy_event:
            globus_scheduler_event_destroy(event);
        }
    }
    globus_list_free(events);
}
/* globus_l_gram_fork_poll_callback() */

/**
 * @brief
 * Condor SEG-like periodic callback
 *
 * @details
 * This function is called periodically to check for condor state changes by
 * polling the condor log files for the jobs. This code assumes that
 * - The condor log files can be located in $job_state_file_dir/condor.$uniq_id
 * - The condor log files are in (pseudo) XML format
 * - The condor log files are owned by the user whose job is being logged
 * - The condor log files are removed when the job is cleaned up
 *
 * This function uses this algorithm to process the logs:
 * - Note current poll timestamp, last poll timestamp
 * - For each file that matches the file pattern
 * -- Check ownership, if not owned by user, skip file
 * -- Check if modified since last poll timestamp, if not changed, skip file
 * -- Lock File
 * -- Parse log file to generate SEG events (see globus_l_condor_parse_log())
 * - set last poll timestamp to current poll timestamp
 */
static
void
globus_l_gram_condor_poll_callback(
    void *                              user_arg)
{
    int                                 rc;
    time_t                              last_poll_time;
    time_t                              poll_time;
    double                              poll_length;
    globus_reltime_t                    delay;
    globus_gram_job_manager_t *         manager = user_arg;
    globus_scheduler_event_t *          event;
    globus_fifo_t                       events;
    char *                              condor_log_data;
    globus_gram_job_manager_ref_t *     ref;
    uint64_t                            uniq1, uniq2;
    char *                              path = NULL;

    GlobusGramJobManagerLock(manager);
    poll_time = time(NULL);
    last_poll_time = manager->seg_last_timestamp;

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.condor_poll.start "
            "level=TRACE "
            "poll_time=%d "
            "last_poll=%d "
            "\n",
            poll_time,
            last_poll_time);

    rc = globus_fifo_init(&events);
    if (rc != GLOBUS_SUCCESS)
    {
        poll_time = last_poll_time;
    }


    for (ref = globus_hashtable_first(&manager->request_hash);
         ref != NULL;
         ref = globus_hashtable_next(&manager->request_hash))
    {
        if (ref->request &&
            ref->request->job_id_string &&
            *ref->request->job_id_string == 0)
        {
            /* Skip jobs which have no outstanding subjobs to poll */
            continue;
        }
        rc = sscanf(ref->key, "/%" SCNu64 "/%" SCNu64 "/", &uniq1, &uniq2);
        if (rc != 2)
        {
            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.condor_poll.info "
                    "level=WARN "
                    "msg=\"%s\" "
                    "key=\"%s\" "
                    "\n",
                    "Unexpected key format",
                    ref->key);
            continue;
        }

        path = globus_common_create_string("%s/condor.%"PRIu64".%"PRIu64,
                manager->config->job_state_file_dir,
                uniq1, uniq2);
        if (path == NULL)
        {
            continue;
        }

        rc = globus_l_condor_read_log(
                manager,
                path,
                ref->seg_last_size,
                &condor_log_data);

        /* condor_log_data is null if the file hasn't changed since
         * seg_last_size or an error happened.
         */
        if (rc != GLOBUS_SUCCESS || condor_log_data == NULL)
        {
            goto read_failed;
        }

        rc = globus_l_condor_parse_log(
                condor_log_data,
                ref,
                &events);

        free(condor_log_data);
read_failed:
        free(path);
        path = NULL;
    }

    /*
     * Adjust poll interval based on polling time. If things are going slowly,
     * wait for a multiple of the poll time, otherwise reset the clock to
     * 5 seconds to avoid globus_callback scheduling this to run fewer than 5
     * seconds from now.
     */
    poll_length = difftime(time(NULL), poll_time);
    if (poll_length > 1.0)
    {
        GlobusTimeReltimeSet(delay, (time_t) (poll_length * 5), 0);
    }
    else
    {
        GlobusTimeReltimeSet(delay, (time_t) 5, 0);
    }
    globus_callback_adjust_period(manager->fork_callback_handle, &delay);
    GlobusGramJobManagerUnlock(manager);

    while (!globus_fifo_empty(&events))
    {
        event = globus_fifo_dequeue(&events);

        globus_l_gram_seg_event_callback(manager, event);

        globus_scheduler_event_destroy(event);
    }
    globus_fifo_destroy(&events);

    GlobusGramJobManagerLock(manager);
    if (poll_time > manager->seg_last_timestamp)
    {
        manager->seg_last_timestamp = poll_time;
    }
    if (globus_l_condor_regexes_compiled)
    {
        regfree(&globus_l_condor_outer_re);
        regfree(&globus_l_condor_inner_re);
        globus_l_condor_regexes_compiled = GLOBUS_FALSE;
    }
    GlobusGramJobManagerUnlock(manager);
    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.condor_poll.end "
            "level=TRACE "
            "\n");
}
/* globus_i_gram_condor_poll_callback() */

/**
 * @brief Generate SEG events for condor log events in a data buffer
 * 
 * @details
 * This function uses a couple of regular expressions to pull out the
 * data from a (pseudo)XML condor log. This parser is adapted from the
 * condor SEG implement from GT4. The log messages look something like this
 * &lt;c>
 *   &lt;<a n="ATTRIBUTE-NAME">&lt;b v="t|f"/>|&lt;s>STRING&lt;/s>|&lt;i>INTEGER&lt;/i>|&lt;r>REAL&lt;/r>
 * &lt;/c>
 *
 * We are only interested in attributes directly related to SEG events:
 * - EventTypeNumber
 * - EventTime
 * - Cluster
 * - Proc
 * - Subproc
 * - TerminatedNormally
 * - ReturnValue
 *
 * The parser pulls out values for all of the children of a c element, then
 * creates an event from it and pushes it onto the events fifo.
 */
static
int
globus_l_condor_parse_log(
    const char *                        data,
    globus_gram_job_manager_ref_t *     ref,
    globus_fifo_t *                     events)
{
    regmatch_t                          matches[8];
    const char *                        p;
    int                                 event_type_number = 0;
    const char *                        event_time;
    int                                 cluster = 0;
    int                                 proc = 0;
    int                                 subproc = 0;
    globus_bool_t                       terminated_normally = GLOBUS_FALSE;
    int                                 return_value = 0;
    struct tm                           event_tm;
    time_t                              event_stamp = 0;
    int                                 rc;
    globus_off_t                        parsed_length = 0;
    globus_scheduler_event_t *          event;

    enum condor_attr_e
    {
        DONTCARE,
        EVENT_TYPE_NUMBER,
        EVENT_TIME,
        CLUSTER,
        PROC,
        SUBPROC,
        TERMINATED_NORMALLY,
        RETURN_VALUE
    } condor_attr;
    typedef enum
    {
        CONDOR_STRING,
        CONDOR_INTEGER,
        CONDOR_BOOLEAN,
        CONDOR_REAL
    } condor_parse_type_t;
    union
    {
        condor_parse_type_t type;

        struct
        {
            condor_parse_type_t type;
            const char * s;
            size_t len;
        } s;

        struct
        {
            condor_parse_type_t type;
            int i;
        } i;

        struct
        {
            condor_parse_type_t type;
            globus_bool_t b;
        } b;

        struct
        {
            condor_parse_type_t type;
            float r;
        } r;
    } pu;

    p = data;
    parsed_length = 0;

    if (!globus_l_condor_regexes_compiled)
    {
        rc = regcomp(&globus_l_condor_outer_re,
            "^([[:space:]]*<c>((<[^/]|</[^c]>|</[^>]{2,}>|[^<])*)</c>[[:space:]]*)",
            REG_EXTENDED);

        assert (rc == 0);

        rc = regcomp(&globus_l_condor_inner_re,
            "^([[:space:]]*"
            "<a n=\"([[:alpha:]]+)\">[[:space:]]*"
            "(<(b) v=\"([tf])\"/>|<([sire])>([^<]*)</[sire]>)"
            "</a>[[:space:]]*)",
            REG_EXTENDED);

        assert(rc == 0);
        globus_l_condor_regexes_compiled = GLOBUS_TRUE;
    }
    while ((rc = regexec(
                &globus_l_condor_outer_re, p, (int) (sizeof(matches)/sizeof(matches[0])),
                matches, 0)) == 0)
    {
        const char * e = p + matches[0].rm_eo;
        regoff_t event_length = matches[0].rm_eo - matches[0].rm_so;

        p = p + matches[2].rm_so;

        while ((rc = regexec(&globus_l_condor_inner_re, p,
                    (int) (sizeof(matches)/sizeof(matches[0])),
                    matches, 0)) == 0)
        {
            size_t matchlen;
            const char * match;
            /* Regular expression match indices as xpath strings
             * 1: a
             * 2: a/@n
             * 3: a/b|a/s/|a/i|a/r
             * 4: a/b
             * 5: a/b/@v
             * 6: a/s/local-name()|a/i/local-name()|a/r/local-name()
             * 7: a/s/text()|a/i/text()|a/r/text()
             */

            matchlen = (size_t) (matches[2].rm_eo - matches[2].rm_so);
            match = p + matches[2].rm_so;
            if (strncmp(match, "EventTypeNumber", matchlen) == 0)
            {
                condor_attr = EVENT_TYPE_NUMBER;
            }
            else if (strncmp(match, "EventTime", matchlen) == 0)
            {
                condor_attr = EVENT_TIME;
            }
            else if (strncmp(match, "Cluster", matchlen) == 0)
            {
                condor_attr = CLUSTER;
            }
            else if (strncmp(match, "Proc", matchlen) == 0)
            {
                condor_attr = PROC;
            }
            else if (strncmp(match, "Subproc", matchlen) == 0)
            {
                condor_attr = SUBPROC;
            }
            else if (strncmp(match, "TerminatedNormally", matchlen) == 0)
            {
                condor_attr = TERMINATED_NORMALLY;
            }
            else if (strncmp(match, "ReturnValue", matchlen) == 0)
            {
                condor_attr = RETURN_VALUE;
            }
            else
            {
                condor_attr = DONTCARE;
            }

            matchlen = (size_t) (matches[4].rm_eo - matches[4].rm_so);
            match = p + matches[4].rm_so;
            if (matches[4].rm_so != -1)
            {
                if (strncmp(match, "b", matchlen) == 0)
                {
                    pu.type = CONDOR_BOOLEAN;

                    matchlen = (size_t) (matches[5].rm_eo - matches[5].rm_so);
                    match = p + matches[5].rm_so;

                    if (strncmp(match, "t", matchlen) == 0)
                    {
                        pu.b.b = GLOBUS_TRUE;
                    }
                    else
                    {
                        pu.b.b = GLOBUS_FALSE;
                    }
                }
            }

            matchlen = (size_t) (matches[6].rm_eo - matches[6].rm_so);
            match = p + matches[6].rm_so;
            if (matches[6].rm_so != -1)
            {
                if (strncmp(match, "s", matchlen) == 0)
                {
                    pu.type = CONDOR_STRING;
                    pu.s.s = p + matches[7].rm_so;
                    pu.s.len = (size_t) (matches[7].rm_eo - matches[7].rm_so);
                }
                else if (strncmp(match, "i", matchlen) == 0)
                {
                    pu.type = CONDOR_INTEGER;
                    pu.i.i = atoi(p + matches[7].rm_so);
                }
                else if (strncmp(match, "r", matchlen) == 0)
                {
                    pu.type = CONDOR_REAL;
                    sscanf(p + matches[7].rm_so, "%f", &pu.r.r);
                }
                else if (strncmp(match, "e", matchlen) == 0)
                {
                    /* ? */
                    pu.type = CONDOR_STRING;
                    pu.s.s = p + matches[7].rm_so;
                    pu.s.len = (size_t) (matches[7].rm_eo - matches[7].rm_so);
                }
            }
            switch (condor_attr)
            {
            case EVENT_TYPE_NUMBER:
                globus_assert (pu.type == CONDOR_INTEGER);
                event_type_number = pu.i.i;
                break;
            case EVENT_TIME:
                globus_assert (pu.type == CONDOR_STRING);
                event_time = pu.s.s;

                globus_strptime(
                        (char *) event_time,
                        "%Y-%m-%dT%H:%M:%S",
                        &event_tm); 

                event_stamp = mktime(&event_tm);
                break;
            case CLUSTER:
                globus_assert (pu.type == CONDOR_INTEGER);
                cluster = pu.i.i;
                break;
            case PROC:
                globus_assert (pu.type == CONDOR_INTEGER);
                proc = pu.i.i;
                break;
            case SUBPROC:
                globus_assert (pu.type == CONDOR_INTEGER);
                subproc = pu.i.i;
                break;
            case TERMINATED_NORMALLY:
                globus_assert (pu.type == CONDOR_BOOLEAN);
                terminated_normally = pu.b.b;
                break;
            case RETURN_VALUE:
                globus_assert (pu.type == CONDOR_INTEGER);
                return_value = pu.i.i;
                break;
            case DONTCARE:
            default:
                break;
            }
            p = p + matches[1].rm_eo;
        }
        p = e;

        parsed_length += event_length;

        event = NULL;
        switch (event_type_number)
        {
        case 0: /* SubmitEvent */
            event = calloc(1, sizeof(globus_scheduler_event_t));
            event->event_type = GLOBUS_SCHEDULER_EVENT_PENDING;
            event->job_id = globus_common_create_string("%03d.%03d.%03d",
                cluster, proc, subproc);
            event->timestamp = event_stamp;

            globus_fifo_enqueue(events, event);
            break;
        case 1: /* ExecuteEvent */
            event = calloc(1, sizeof(globus_scheduler_event_t));
            event->event_type = GLOBUS_SCHEDULER_EVENT_ACTIVE;
            event->job_id = globus_common_create_string("%03d.%03d.%03d",
                cluster, proc, subproc);
            event->timestamp = event_stamp;

            globus_fifo_enqueue(events, event);
            break;

        case 5: /* JobTerminatedEvent */
            if (terminated_normally)
            {
                event = calloc(1, sizeof(globus_scheduler_event_t));
                event->event_type = GLOBUS_SCHEDULER_EVENT_DONE;
                event->job_id = globus_common_create_string("%03d.%03d.%03d",
                    cluster, proc, subproc);
                event->timestamp = event_stamp;
                event->exit_code = return_value;

                globus_fifo_enqueue(events, event);
            }
            else
            {
        case 9: /* JobAbortedEvent */
                event = calloc(1, sizeof(globus_scheduler_event_t));
                event->event_type = GLOBUS_SCHEDULER_EVENT_FAILED;
                event->job_id = globus_common_create_string("%03d.%03d.%03d",
                    cluster, proc, subproc);
                event->timestamp = event_stamp;
                event->failure_code = return_value;

                globus_fifo_enqueue(events, event);
            }
            break;
        }
        if (event && event->timestamp > ref->seg_last_timestamp)
        {
            ref->seg_last_timestamp = event->timestamp;
        }
    }
    ref->seg_last_size += parsed_length;
    return 0;
}
/* globus_l_condor_parse_log() */

static
int
globus_l_condor_read_log(
    globus_gram_job_manager_t          *manager,
    const char                         *path,
    size_t                              last_size,
    char                              **data)
{
    int                                 condor_log_fd;
    char                               *condor_log_data;
    struct stat                         st;
    struct flock                        flock_data;
    int                                 rc = GLOBUS_SUCCESS;

    *data = NULL;

    condor_log_fd = open(path, O_RDONLY);
    if (condor_log_fd < 0)
    {
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                "event=gram.condor_poll.info "
                "level=TRACE "
                "message=\"%s\" "
                "errno=%d "
                "errstr=\"%s\" "
                "\n",
                "open failed",
                errno,
                strerror(errno));
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_STATE_FILE;

        goto open_failed;
    }
    rc = fstat(condor_log_fd, &st);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                "event=gram.condor_poll.info "
                "level=TRACE "
                "message=\"%s\" "
                "errno=%d "
                "errstr=\"%s\" "
                "\n",
                "fstat failed",
                errno,
                strerror(errno));

        rc = GLOBUS_GRAM_PROTOCOL_ERROR_READING_STATE_FILE;
        goto fstat_failed;
    }

    if (st.st_uid != getuid())
    {
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                "event=gram.condor_poll.info "
                "level=TRACE "
                "message=\"%s\" "
                "uid.me=%ld "
                "uid.file=%ld "
                "\n",
                "uid mismatch",
                (long) getuid(),
                (long) st.st_uid);
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_READING_STATE_FILE;
        goto uid_mismatch;
    }

    if (st.st_size <= last_size)
    {
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                "event=gram.condor_poll.info "
                "level=TRACE "
                "message=\"%s\" "
                "file=\"%s\" "
                "size.last_poll=%lld "
                "size.file=%lld "
                "\n",
                "file hasn't grown since last poll",
                path,
                (long long) last_size,
                (long long) st.st_size);

        goto not_grown;
    }

    flock_data.l_type = F_RDLCK;
    flock_data.l_whence = SEEK_SET;
    flock_data.l_start = 0;
    flock_data.l_len = 0;
    flock_data.l_pid = getpid();

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.condor_poll.info "
            "level=TRACE "
            "message=\"%s\" "
            "file=\"%s\" "
            "\n",
            "Checking file for new events",
            path);

    do
    {
        rc = fcntl(condor_log_fd, F_SETLKW, &flock_data);
        if (rc != 0 && errno != EINTR)
        {
            goto fcntl_lock_failed;
        }
    } while (rc == -1);

    {
        ssize_t read_res;
        size_t amt_to_read = st.st_size - last_size;
        size_t amt_read = 0;
        off_t off_rc;

        condor_log_data = malloc(amt_to_read + 1);
        if (condor_log_data == NULL)
        {
            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.condor_poll.info "
                    "level=WARN "
                    "message=\"%s\" "
                    "filename=\"%s\" "
                    "size=%llu "
                    "errno=%d "
                    "reason=%s\n",
                    "Error allocating memory for condor log",
                    path,
                    (unsigned long long) st.st_size,
                    errno,
                    strerror(errno));
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            goto malloc_data_failed;
        }

        condor_log_data[amt_to_read] = 0;

        off_rc = lseek(condor_log_fd, (off_t) last_size, SEEK_SET);
        if (off_rc < 0)
        {
            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.condor_poll.info "
                    "level=WARN "
                    "message=\"%s\" "
                    "filename=\"%s\" "
                    "size=%llu "
                    "errno=%d "
                    "reason=%s\n",
                    "Error seeking in condor log",
                    path,
                    (unsigned long long) st.st_size,
                    errno,
                    strerror(errno));
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_TEMP_SCRIPT_FILE_FAILED;
            goto seek_failed;
        }

        while (amt_to_read > amt_read)
        {
            read_res = read(
                    condor_log_fd,
                    condor_log_data + amt_read,
                    amt_to_read - amt_read);

            if (read_res < 0 && errno == EINTR)
            {
                continue;
            }
            else if (read_res > 0)
            {
                amt_read += read_res;
            }
            else
            {
                /* Some other error or short read */
                break;
            }
        }

        if (amt_to_read != amt_read)
        {
            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.condor_poll.info "
                    "level=WARN "
                    "message=\"%s\" "
                    "filename=\"%s\" "
                    "size=%llu "
                    "amt_read=%llu "
                    "errno=%d "
                    "reason=%s\n",
                    "Error reading condor log",
                    path,
                    (unsigned long long) st.st_size,
                    (unsigned long long) amt_read,
                    errno,
                    strerror(errno));
            goto read_failed;
        }
        *data = condor_log_data;
        rc = GLOBUS_SUCCESS;
    }

    if (rc != GLOBUS_SUCCESS)
    {
read_failed:
seek_failed:
        free(condor_log_data);
    }
malloc_data_failed:
fcntl_lock_failed:
not_grown:
uid_mismatch:
fstat_failed:
    close(condor_log_fd);
open_failed:
    return rc;
}

int
globus_gram_job_manager_seg_parse_condor_id(
    globus_gram_jobmanager_request_t *  request,
    char **                             condor_idp)
{
    char *                              condor_name;
    char *                              condor_data;
    globus_fifo_t                       events;
    int                                 rc = GLOBUS_SUCCESS;
    time_t                              old_last_timestamp;
    globus_off_t                        old_last_size;
    char *                              condor_id;
    globus_gram_job_manager_ref_t *     ref;
    globus_scheduler_event_t *          event;

    *condor_idp = NULL;
    GlobusGramJobManagerLock(request->manager);
    ref = globus_hashtable_lookup(
            &request->manager->request_hash,
            request->job_contact_path);
    if (!ref)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_CONTACT_NOT_FOUND;

        goto no_ref;
    }
    condor_name = globus_common_create_string(
            "%s/condor.%s",
            request->config->job_state_file_dir,
            request->uniq_id);

    if (condor_name == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto name_malloc_fail;
    }

    rc = globus_l_condor_read_log(
        request->manager,
        condor_name,
        0,
        &condor_data);

    if (rc != GLOBUS_SUCCESS || condor_data == NULL)
    {
        goto read_failed;
    }

    rc = globus_fifo_init(&events);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto fifo_init_failed;
    }

    /* Don't want to affect the ref timestamp for these events, just
     * pull out the jobid value
     */
    old_last_timestamp = ref->seg_last_timestamp;
    old_last_size = ref->seg_last_size;
    globus_l_condor_parse_log(
        condor_data,
        ref,
        &events);
    ref->seg_last_timestamp = old_last_timestamp;
    ref->seg_last_size = old_last_size;

    /* If there's any event in this file, then we'll assume that's the
     * job id base for this job and construct the subjob ids based on the
     * rsl count attribute
     */
    if (!globus_fifo_empty(&events))
    {
        event = globus_fifo_peek(&events);
        if (event->job_id != NULL)
        {
            int cluster;
            int count;
            int i;
            char *p;
            size_t subjob_len;

            rc = globus_gram_job_manager_rsl_attribute_get_int_value(
                request->rsl,
                GLOBUS_GRAM_PROTOCOL_COUNT_PARAM,
                &count);
            if (rc != GLOBUS_SUCCESS)
            {
                goto bad_count;
            }
            sscanf(event->job_id, "%d", &cluster);

            subjob_len = globus_libc_printf_length(
                    "%03d.%03d.%03d,",
                    cluster,
                    count,
                    0);
            condor_id = malloc(subjob_len * count + 1);
            if (condor_id == NULL)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
                goto condor_id_malloc_failed;
            }
            condor_id[0] = 0;
            p = condor_id;

            for (i = 0; i < count; i++)
            {
                int chars;
                chars = sprintf(p, "%03d.%03d.%03d,",
                        cluster, i, 0);
                p += chars;
            }
            *(p-1) = 0;
            *condor_idp = condor_id;
        }
    }

condor_id_malloc_failed:
bad_count:
    /*
     * Should probably put these directly into the request's SEG event queue,
     * but for simplicity, just discard these here and let the regular poll
     * callback handle them.
     */
    while (!globus_fifo_empty(&events))
    {
        event = globus_fifo_dequeue(&events);

        globus_scheduler_event_destroy(event);
    }
    globus_fifo_destroy(&events);
fifo_init_failed:
    free(condor_data);
read_failed:
    free(condor_name);
name_malloc_fail:
no_ref:
    GlobusGramJobManagerUnlock(request->manager);
    return rc;
}
/* globus_gram_job_manager_seg_parse_condor_id() */
