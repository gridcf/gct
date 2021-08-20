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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_gram_job_manager_state.c
 * @brief Job Manager State Machine
 */
#include "globus_gram_job_manager.h"
#include "globus_rsl_assist.h"
#include "globus_gsi_system_config.h"
#include "globus_gsi_system_config_constants.h"
#include "version.h"

#include <string.h>


/* Module Specific Prototypes */

const char *                     globus_i_gram_job_manager_state_strings[] =
{
    "GLOBUS_GRAM_JOB_MANAGER_STATE_START",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_COMMITTED",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_IN",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_SUBMIT",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_CLOSE_OUTPUT",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_OUT",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END_COMMITTED",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_FILE_CLEAN_UP",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_SCRATCH_CLEAN_UP",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_CACHE_CLEAN_UP",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_DONE",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CLOSE_OUTPUT",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE_COMMITTED",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_FILE_CLEAN_UP",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_SCRATCH_CLEAN_UP",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CACHE_CLEAN_UP",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_DONE",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_STOP",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY1",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY2",
    NULL, /*"GLOBUS_GRAM_JOB_MANAGER_STATE_PROXY_REFRESH ,*/
    "GLOBUS_GRAM_JOB_MANAGER_STATE_PRE_CLOSE_OUTPUT",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY1",
    "GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY2",
    NULL /*"GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_PROXY_REFRESH" */
};

static
globus_bool_t
globus_l_gram_job_manager_set_restart_state(
    globus_gram_jobmanager_request_t *  request);

static
int
globus_l_gram_job_manager_add_cache_info(
    globus_gram_jobmanager_request_t *  request);

static
void
globus_l_gram_job_manager_cancel_queries(
    globus_gram_jobmanager_request_t *  request);

static
int
globus_l_gram_gss_send_fd(
    void *                              arg,
    void *                              buffer,
    size_t                              length);

static
void
globus_l_gram_file_cleanup(
    globus_gram_jobmanager_request_t *  request);

static
globus_bool_t
globus_l_gram_job_manager_state_machine(
    globus_gram_jobmanager_request_t *  request);

#endif

/*
 * Callback to enter the state machine from a timeout. Used to
 * handle two-phase commit timeouts, and delays between calls to the
 * poll script.
 */
void
globus_gram_job_manager_state_machine_callback(
    void *                              user_arg)
{
    globus_gram_jobmanager_request_t *  request;
    globus_bool_t                       event_registered;
    int                                 rc;

    request = user_arg;

    GlobusGramJobManagerRequestLock(request);

    /*
     * If nobody tried to cancel this callback, then we need to unregister
     * it to free memory in the callback code.
     */
    if(request->poll_timer != GLOBUS_HANDLE_TABLE_NO_HANDLE)
    {
        globus_callback_unregister(request->poll_timer, NULL, NULL, NULL);
        request->poll_timer = GLOBUS_HANDLE_TABLE_NO_HANDLE;
    }
    
    do
    {
        event_registered = globus_l_gram_job_manager_state_machine(request);
    }
    while(!event_registered);

    GlobusGramJobManagerRequestUnlock(request);
    rc = globus_gram_job_manager_remove_reference(
            request->manager,
            request->job_contact_path,
            "state machine");
    assert(rc == GLOBUS_SUCCESS);

}
/* globus_gram_job_manager_state_machine_callback() */


/*
 * Job Manager state machine.
 */
static
globus_bool_t
globus_l_gram_job_manager_state_machine(
    globus_gram_jobmanager_request_t *  request)
{
    globus_bool_t                       event_registered = GLOBUS_FALSE;
    globus_reltime_t                    delay_time;
    int                                 rc = 0;
    int                                 save_status;
    int                                 save_jobmanager_state;
    globus_gram_job_manager_query_t *   query;
    globus_bool_t                       first_poll = GLOBUS_FALSE;
    globus_gram_jobmanager_state_t      next_state;
    char                                exit_code_string[] = "unknown";

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.state_machine.start "
            "level=TRACE "
            "gramid=%s "
            "jmstate=%s "
            "job_state=%d "
            "\n",
            request->job_contact_path,
            globus_i_gram_job_manager_state_strings[request->jobmanager_state],
            request->status);

    switch(request->jobmanager_state)
    {
      case GLOBUS_GRAM_JOB_MANAGER_STATE_START:
        request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE;
        /*
         * To do a two-phase commit, we need to send an error
         * message (WAITING_FOR_COMMIT) in the initial reply; otherwise,
         * we just return the current status code.
         * 
         * When doing a dry run, we don't send the reply until we would
         * submit the job (no state callbacks with a dry-run.)
         */
        if(!request->dry_run)
        {
            if(request->two_phase_commit != 0 && rc == GLOBUS_SUCCESS)
            {
                GlobusTimeReltimeSet(delay_time,
                                     request->two_phase_commit,
                                     0);

                rc = globus_gram_job_manager_state_machine_register(
                        request->manager,
                        request,
                        &delay_time);
                if (rc == GLOBUS_SUCCESS)
                {
                    event_registered = GLOBUS_TRUE;
                }
            }
        }
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE:
        if(request->two_phase_commit != 0 && request->commit_extend != 0)
        {
            GlobusTimeReltimeSet(delay_time,
                                 request->commit_extend,
                                 0);

            request->commit_extend = 0;
            rc = globus_gram_job_manager_state_machine_register(
                    request->manager,
                    request,
                    &delay_time);


            if (rc == GLOBUS_SUCCESS)
            {
                event_registered = GLOBUS_TRUE;
            }
        }
        else if(request->two_phase_commit == 0)
        {
            /* Nothing to do here if we are not doing the two-phase
             * commit protocol
             */
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_COMMITTED;
        }
        else if(request->jm_restart)
        {
            /*
             * commit didn't happen, but this was a restart of a job manager
             * so we'll just stop and leave the job manager state.
             */
            request->poll_timer = GLOBUS_HANDLE_TABLE_NO_HANDLE;
            request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_STOP;
            request->stop_reason = GLOBUS_GRAM_PROTOCOL_ERROR_COMMIT_TIMED_OUT;
        }
        else
        {
            request->poll_timer = GLOBUS_HANDLE_TABLE_NO_HANDLE;
            /* Send failed message later */
            request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED;
            request->failure_code = GLOBUS_GRAM_PROTOCOL_ERROR_COMMIT_TIMED_OUT;
            request->unsent_status_change = GLOBUS_TRUE;
        }
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_COMMITTED:
        if(request->jm_restart)
        {
            if(globus_l_gram_job_manager_set_restart_state(request))
            {
                break;
            }
        }
        request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_IN;

        if(globus_gram_job_manager_rsl_need_stage_in(request))
        {
            globus_gram_job_manager_request_set_status(request, GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_IN);

            if(!request->dry_run)
            {
                globus_gram_job_manager_contact_state_callback(
                        request,
                        GLOBUS_FALSE);
            }

            rc = globus_gram_job_manager_script_stage_in(request);

            if(rc != GLOBUS_SUCCESS)
            {
                request->failure_code = rc;
                request->jobmanager_state =
                    GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED;
            }
            else
            {
                event_registered = GLOBUS_TRUE;
            }
        }
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_IN:
        if((!globus_list_empty(request->stage_in_todo)) ||
           (!globus_list_empty(request->stage_in_shared_todo)))
        {
            /* Didn't successfully stage in everything. */
            request->jobmanager_state = 
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED;
            if(request->failure_code == GLOBUS_SUCCESS)
            {
                request->failure_code =
                    GLOBUS_GRAM_PROTOCOL_ERROR_STAGE_IN_FAILED;
            }
            break;
        }
        request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_SUBMIT;

        if(request->status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED && 
           request->dry_run)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE_COMMITTED;

            break;
        }
        else if(request->status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED)
        {
            request->jobmanager_state = 
                    GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED;
            break;
        }
        /* write submit state, so that condor job id recovery can work */
        request->restart_state = GLOBUS_GRAM_JOB_MANAGER_STATE_SUBMIT;
        globus_gram_job_manager_state_file_write(request);

        globus_gram_job_manager_seg_pause(request->manager);
        /*
         * GRAM-145: GRAM5 Job Manager fails to save SEG timestamps in job
         * state files
         *
         * The request state file needs to contain of the earliest time a SEG
         * event occurred for this job. The default value of seg_last_timestamp
         * is 0 which means that the job doesn't care about how far back
         * in time the SEG looks. We set it to the time the job is submitted so
         * that if a restart occurs, the job manager will be able to go back
         * as far as necessary for any of the jobs which are reloaded.
         */
        request->seg_last_timestamp = time(NULL);

        rc = globus_gram_job_manager_script_submit(request);

        if(rc != GLOBUS_SUCCESS)
        {
            request->failure_code = rc;
            globus_gram_job_manager_request_set_status(request, GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);

            if(!request->dry_run)
            {
                request->jobmanager_state = 
                        GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED;
                request->unsent_status_change = GLOBUS_TRUE;
            }
            globus_gram_job_manager_seg_resume(request->manager);
        }
        else
        {
            event_registered = GLOBUS_TRUE;
        }

        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_SUBMIT:
        if(request->status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED && 
           request->dry_run)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE_COMMITTED;

            globus_gram_job_manager_seg_resume(request->manager);
            break;
        }
        else if(request->job_id_string == NULL)
        {
            /* submission failed to generate a job id */
            if(request->failure_code == GLOBUS_SUCCESS)
            {
                request->failure_code =
                    GLOBUS_GRAM_PROTOCOL_ERROR_SUBMIT_UNKNOWN;
            }
            globus_gram_job_manager_request_set_status(request, GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);
            request->unsent_status_change = GLOBUS_TRUE;
            globus_gram_job_manager_seg_resume(request->manager);
        }
        else
        {
            rc = globus_gram_job_manager_register_job_id(
                    request->manager,
                    request->job_id_string,
                    request,
                    GLOBUS_FALSE);
            if (rc != GLOBUS_SUCCESS)
            {
                request->failure_code = rc;
                globus_gram_job_manager_request_set_status(request, GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);
                request->unsent_status_change = GLOBUS_TRUE;
            }
            globus_l_gram_job_manager_add_cache_info(request);
            globus_gram_job_manager_seg_resume(request->manager);
        }
        request->queued_time = time(NULL);
        globus_gram_job_manager_history_file_create(request);
        request->job_history_status = request->status;

        request->restart_state = request->jobmanager_state =
            GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1;
        globus_gram_job_manager_state_file_write(request);
        first_poll = GLOBUS_TRUE;
        
        /* FALLSTHROUGH so we can act on a job state change returned from
         * the submit script.
         */
      case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1:
        if (! globus_fifo_empty(&request->seg_event_queue))
        {
            /* A SEG event occurred recently. Let's update our job state
             */
            globus_gram_job_manager_seg_handle_event(request);

        }
        if(request->unsent_status_change)
        {
            request->restart_state = request->jobmanager_state;
            globus_gram_job_manager_state_file_write(request);
        }

        /* The request->job_history_status is used to save the last job status
         * that is stored in history file. If it is different with
         * request->status, we have to write history file.
         */ 
        if(request->unsent_status_change &&
                (request->job_history_status != request->status))
        {
            globus_gram_job_manager_history_file_create(request);
            request->job_history_status = request->status;
        }

        if (request->status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED;
        }
        else if (request->status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE ||
                 request->status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_OUT)
        {
            /* Job finished! start finalizing */
            request->jobmanager_state =
                    GLOBUS_GRAM_JOB_MANAGER_STATE_PRE_CLOSE_OUTPUT;
            break;
        }
        else
        {
            /* Send job state callbacks if necessary */
            if(request->unsent_status_change)
            {
                globus_gram_job_manager_contact_state_callback(request, GLOBUS_FALSE);
                request->unsent_status_change = GLOBUS_FALSE;
            }

            if(!globus_fifo_empty(&request->pending_queries))
            {
                request->jobmanager_state =
                    GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY1;
                break;
            }
            request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2;

            if (!first_poll && !request->manager->seg_started)
            {
                /* Register next poll of job state */
                GlobusTimeReltimeSet(
                        delay_time,
                        request->poll_frequency, 0);

                rc = globus_gram_job_manager_state_machine_register(
                        request->manager,
                        request,
                        &delay_time);
                if (rc == GLOBUS_SUCCESS)
                {
                    event_registered = GLOBUS_TRUE;
                }
            }
            else if (request->manager->seg_started)
            {
                /* SEG has been started. If there is an event pending,
                 * we should jump to POLL1 state and not set
                 * event_registered, so that we can process it.
                 *
                 * Otherwise, we'll set event_registered and the next
                 * query or SEG event will move the state machine.
                 */
                if (! globus_fifo_empty(&request->seg_event_queue))
                {

                    request->jobmanager_state =
                        GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1;
                }
                else
                {
                    event_registered = GLOBUS_TRUE;
                }
            }
        }
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2:
        /* timer expired since last poll. start polling again. */

        request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1;

        if (request->config->seg_module == NULL &&
            strcmp(request->config->jobmanager_type, "fork") != 0 &&
            strcmp(request->config->jobmanager_type, "condor") != 0)
        {
            rc = globus_gram_job_manager_script_poll(request);
        }
        else if (!globus_fifo_empty(&request->seg_event_queue))
        {
            /* We don't want to set event_registered in this case, because
             * we want to immediately process the event in the queue.
             */
            break;
        }
        else
        {
            /* Nothing in the SEG queue. We need to stay in this state to
             * get SEG code to reregister the state machine
             */
            request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2;
        }

        if(rc != GLOBUS_SUCCESS)
        {
            globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.state_machine.info "
                "level=WARN "
                "gramid=%s "
                "msg=\"%s\" "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                "Poll failed",
                -rc,
                globus_gram_protocol_error_string(rc));
        }
        else
        {
            event_registered = GLOBUS_TRUE;
        }
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY1:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY1:
        if (request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY1)
        {
            next_state = GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY2;
        }
        else
        {
            next_state = GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY2;
        }

        /*
         * timer cancelled since last poll, because we may have some
         * queries to process
         */
        query = globus_fifo_peek(&request->pending_queries);

        if (query->type == GLOBUS_GRAM_JOB_MANAGER_SIGNAL)
        {
            rc = globus_gram_job_manager_script_signal(
                    request,
                    query);
        }
        else if(query->type == GLOBUS_GRAM_JOB_MANAGER_CANCEL)
        {
            rc = globus_gram_job_manager_script_cancel(
                    request,
                    query);
        }
        if(rc == GLOBUS_SUCCESS)
        {
            request->jobmanager_state = next_state;

            event_registered = GLOBUS_TRUE;
        }
        else
        {
            globus_fifo_dequeue(&request->pending_queries);
            query->failure_code = rc;

            globus_gram_job_manager_query_reply(request, query);

            if(globus_fifo_empty(&request->pending_queries))
            {
                request->jobmanager_state =
                    GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2;
            }
        }
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY2:
        query = globus_fifo_dequeue(&request->pending_queries);

        /* Frees the query */
        globus_gram_job_manager_query_reply(
                request,
                query);

        if(globus_fifo_empty(&request->pending_queries))
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE;

            GlobusTimeReltimeSet(delay_time,
                                 request->two_phase_commit,
                                 0);

            rc = globus_gram_job_manager_state_machine_register(
                    request->manager,
                    request,
                    &delay_time);

            if (rc == GLOBUS_SUCCESS)
            {
                event_registered = GLOBUS_TRUE;
            }
        }
        else
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY1;
        }
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY2:
        query = globus_fifo_dequeue(&request->pending_queries);

        /* Frees the query */
        globus_gram_job_manager_query_reply(
                request,
                query);

        if(globus_fifo_empty(&request->pending_queries) &&
           (request->unsent_status_change ||
            !globus_fifo_empty(&request->seg_event_queue)))
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1;
        }
        else if(globus_fifo_empty(&request->pending_queries))
        {
            request->jobmanager_state = 
                GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2;
        }
        else
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY1;
        }
        break;
    
      case GLOBUS_GRAM_JOB_MANAGER_STATE_PRE_CLOSE_OUTPUT:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED:
        if(request->unsent_status_change)
        {
            request->restart_state = request->jobmanager_state;
            globus_gram_job_manager_state_file_write(request);

            if (request->status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_OUT)
            {
                globus_gram_job_manager_contact_state_callback(
                        request,
                        GLOBUS_FALSE);
                request->unsent_status_change = GLOBUS_FALSE;
            }
        }
        if(request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_PRE_CLOSE_OUTPUT)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_CLOSE_OUTPUT;
        }
        else
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CLOSE_OUTPUT;
        }
        save_status = request->status;
        save_jobmanager_state = request->jobmanager_state;

        /* Reply to any outstanding queries */
        while (!globus_fifo_empty(&request->pending_queries))
        {
            query = globus_fifo_dequeue(&request->pending_queries);

            query->failure_code = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_QUERY_DENIAL;
            /* Frees the query */
            globus_gram_job_manager_query_reply(
                    request,
                    query);
        }
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE:
        if (request->two_phase_commit != 0 &&
            request->commit_extend != 0 &&
            request->failure_code != GLOBUS_GRAM_PROTOCOL_ERROR_COMMIT_TIMED_OUT)
        {
            GlobusTimeReltimeSet(delay_time,
                                 request->commit_extend,
                                 0);
            request->commit_extend = 0;

            rc = globus_gram_job_manager_state_machine_register(
                    request->manager,
                    request,
                    &delay_time);

            if (rc == GLOBUS_SUCCESS)
            {
                event_registered = GLOBUS_TRUE;
            }
        }
        else if (request->two_phase_commit == 0 ||
                 !request->client_contacts ||
                 request->failure_code == GLOBUS_GRAM_PROTOCOL_ERROR_COMMIT_TIMED_OUT)
        {
            /* Nothing to do here if we are not doing the two-phase
             * commit protocol or if we have no client callbacks or this is
             * clean up from a failed two-phase commit
             */
            if(request->jobmanager_state ==
                    GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END)
            {
                request->jobmanager_state =
                    GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END_COMMITTED;
            }
            else
            {
                request->jobmanager_state =
                  GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE_COMMITTED;
            }
        }
        else
        {
            request->restart_state = request->jobmanager_state;
            request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_STOP;
            request->stop_reason = GLOBUS_GRAM_PROTOCOL_ERROR_COMMIT_TIMED_OUT;

            event_registered = GLOBUS_TRUE;
        }
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END_COMMITTED:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE_COMMITTED:
        if(request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END_COMMITTED)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_FILE_CLEAN_UP;
        }
        else
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_FILE_CLEAN_UP;
        }
        if(globus_gram_job_manager_rsl_need_file_cleanup(request) ||
            strcmp(request->config->jobmanager_type, "condor") == 0)
        {
            globus_l_gram_file_cleanup(request);
        }
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_FILE_CLEAN_UP:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_FILE_CLEAN_UP:
        if(request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_FILE_CLEAN_UP)
        {
            if(request->status != GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED)
            {
                request->jobmanager_state =
                    GLOBUS_GRAM_JOB_MANAGER_STATE_SCRATCH_CLEAN_UP;
            }
            else
            {
                request->jobmanager_state =
                    GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_SCRATCH_CLEAN_UP;
            }
        }
        else if(request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_FILE_CLEAN_UP)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_SCRATCH_CLEAN_UP;
        }
        else
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_SCRATCH_CLEAN_UP;
        }

        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_SCRATCH_CLEAN_UP:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_SCRATCH_CLEAN_UP:
        if(request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_SCRATCH_CLEAN_UP)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_CACHE_CLEAN_UP;
        }
        else
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CACHE_CLEAN_UP;
        }

        /* Cache cleanup script cleaned gass cache and jobdir */
        globus_gram_job_manager_destroy_directory(
                request,
                request->job_dir);
        globus_gass_cache_cleanup_tag_all(
                request->cache_handle,
                request->cache_tag);
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STATE_CACHE_CLEAN_UP:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CACHE_CLEAN_UP:
        if(request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_CACHE_CLEAN_UP)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_DONE;
        }
        else
        {
            request->jobmanager_state = 
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_DONE;
        }
        
        if(request->job_state_file)
        {
            remove(request->job_state_file);
        }
        globus_l_gram_job_manager_cancel_queries(request);

        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_STOP:
        /* This state is reached when the job manager decides to stop
         * between the time the job request reply is sent and the 
         * job manager has noticed stop or failed
         */
        globus_gram_job_manager_contact_state_callback(request, GLOBUS_FALSE);

        globus_gram_job_manager_state_file_write(request);

        event_registered = GLOBUS_TRUE;

        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_DONE:
        if(request->job_state_file)
        {
            remove(request->job_state_file);
        }
        globus_l_gram_job_manager_cancel_queries(request);
        /* Write auditing file if job is DONE or FAILED */
        if (request->jobmanager_state == GLOBUS_GRAM_JOB_MANAGER_STATE_DONE ||
            request->jobmanager_state == GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_DONE)
        {
            request->restart_state = request->jobmanager_state;
            globus_gram_job_manager_auditing_file_write(request);
        }

        /* Don't allow any new SEG events to enter the queue */
        (void) globus_gram_job_manager_unregister_job_id(
                request->manager,
                request->job_id_string);

        /* Clear any existing SEG events */
        while (! globus_fifo_empty(&request->seg_event_queue))
        {
            /* A SEG event occurred recently. Let's update our job state
             */
            globus_gram_job_manager_seg_handle_event(request);
        }
        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.job.end "
                "level=ERROR "
                "gramid=%s "
                "job_status=%d "
                "status=%d "
                "reason=\"%s\"\n",
                request->job_contact_path,
                request->status,
                -request->failure_code,
                globus_gram_protocol_error_string(request->failure_code));

        event_registered = GLOBUS_TRUE;
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_DONE:
        globus_l_gram_job_manager_cancel_queries(request);

        request->restart_state = request->jobmanager_state;
        globus_gram_job_manager_auditing_file_write(request);
        event_registered = GLOBUS_TRUE;

        /* Don't allow any new SEG events to enter the queue */
        (void) globus_gram_job_manager_unregister_job_id(
                request->manager,
                request->job_id_string);
        /* Clear any existing SEG events */
        while (! globus_fifo_empty(&request->seg_event_queue))
        {
            /* A SEG event occurred recently. Let's update our job state
             */
            globus_gram_job_manager_seg_handle_event(request);
        }
        if (request->config->seg_module && request->manager->seg_started)
        {
            snprintf(
                    exit_code_string,
                    sizeof(exit_code_string),
                    "%d",
                    (int) request->exit_code & 0xffff);
        }

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_INFO,
                "event=gram.job.end "
                "level=INFO "
                "gramid=%s "
                "job_status=%d "
                "exit_code=%s "
                "status=%d "
                "msg=\"%s\" "
                "\n",
                request->job_contact_path,
                request->status,
                exit_code_string,
                0,
                "Job complete");
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_CLOSE_OUTPUT:
        if (request->status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED && request->failure_code == GLOBUS_GRAM_PROTOCOL_ERROR_STAGE_OUT_FAILED)
        {
            request->jobmanager_state =
            GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CLOSE_OUTPUT;
            break;
        }

        request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_OUT;

        if ((!globus_list_empty(request->stage_stream_todo)) ||
            (!globus_list_empty(request->stage_out_todo)))
        {
            rc = globus_gram_job_manager_script_stage_out(request);
        
            if(rc != GLOBUS_SUCCESS)
            {
                request->failure_code = rc;
                request->jobmanager_state =
                        GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED;
            }
            else
            {
                event_registered = GLOBUS_TRUE;
                request->unsent_status_change = GLOBUS_TRUE;
            }
        }
        else
        {
            request->unsent_status_change = GLOBUS_TRUE;
        }
        break;

      case GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_OUT:
        if(!globus_list_empty(request->stage_stream_todo))
        {
            request->jobmanager_state
                    = GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CLOSE_OUTPUT;
            if(request->failure_code == GLOBUS_SUCCESS ||
               request->failure_code ==
                    GLOBUS_GRAM_PROTOCOL_ERROR_STAGE_OUT_FAILED)
            {
                request->failure_code =
                    GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDOUT;
            }
        }
        else if(!globus_list_empty(request->stage_out_todo))
        {
            request->jobmanager_state
                    = GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CLOSE_OUTPUT;
            if(request->failure_code == GLOBUS_SUCCESS)
            {
                request->failure_code =
                    GLOBUS_GRAM_PROTOCOL_ERROR_STAGE_OUT_FAILED;
            }
        }
      /* FALLSTHROUGH */
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CLOSE_OUTPUT:
        if(request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_OUT)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END;
            globus_gram_job_manager_request_set_status(request, GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE);
        }
        else if(request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CLOSE_OUTPUT)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE;
            globus_gram_job_manager_request_set_status(request, GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);
        }

        if(request->unsent_status_change)
        {
            if(request->job_history_status != request->status)
            {
                globus_gram_job_manager_history_file_create(request);
                request->job_history_status = request->status;
            }

            /*
             * If we are doing two-phase_commit, then we will have the 
             * state machine restarted after the job state callback message
             * is done being processed.
             */
            globus_gram_job_manager_contact_state_callback(
                    request,
                    (request->two_phase_commit != 0));
            request->unsent_status_change = GLOBUS_FALSE;

            if (request->two_phase_commit != 0)
            {
                event_registered = GLOBUS_TRUE;
            }
        }

        break;
    }

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.state_machine.end "
            "level=TRACE "
            "gramid=%s "
            "jmstate=%s "
            "job_state=%d "
            "event_registered=%s "
            "\n",
            request->job_contact_path,
            globus_i_gram_job_manager_state_strings[request->jobmanager_state],
            request->status,
            event_registered ? "true" : "false");

    return event_registered;
}
/* globus_gram_job_manager_state_machine() */

/**
 * Send the response to the initial job request to the GRAM client
 *
 * @param request
 *     Job request
 * @param response_code
 *     GLOBUS_SUCCESS, or a GRAM protocol failure code.
 * @param job_contact
 *     Job contact
 * @param response_fd
 *     Descriptor to send response to
 * @param response_context
 *     GSSAPI context to use to wrap the response message
 * @param gt3_failure_message
 *     Error information to include in a GRAM extension
 */
int
globus_gram_job_manager_reply(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_t *         manager,
    int                                 response_code,
    const char *                        job_contact,
    int                                 response_fd,
    gss_ctx_id_t                        response_context,
    const char *                        gt3_failure_message)
{
    int                                 rc;
    globus_byte_t *                     reply = NULL;
    globus_size_t                       replysize;
    globus_byte_t *                     sendbuf;
    globus_size_t                       sendsize;
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    int                                 token_status;
    globus_hashtable_t                  extensions = NULL;
    globus_gram_protocol_extension_t *  extension = NULL;

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.reply.start "
            "level=DEBUG "
            "gramid=%s "
            "job_contact=\"%s\" "
            "response_code=%d "
            "\n",
            request ? request->job_contact_path : "",
            job_contact ? job_contact : "",
            response_code);

    rc = globus_hashtable_init(
            &extensions,
            3,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);

    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto hashtable_init_failed;
    }

    if (manager != NULL)
    {
        extension = globus_gram_protocol_create_extension(
                "toolkit-version",
                "%s",
                manager->config->globus_version);

        if (extension == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto extension_create_failed;
        }

        rc = globus_hashtable_insert(
                &extensions,
                extension->attribute,
                extension);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto extension_insert_failed;
        }
        extension = NULL;
    }

    extension = globus_gram_protocol_create_extension(
            "version",
            "%d.%d (%d-%d)",
            local_version.major,
            local_version.minor,
            local_version.timestamp,
            local_version.branch_id);

    if (extension == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto extension_create_failed;
    }

    rc = globus_hashtable_insert(&extensions, extension->attribute, extension);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto extension_insert_failed;
    }
    extension = NULL;

    if (gt3_failure_message != NULL)
    {
        extension = globus_gram_protocol_create_extension(
                "gt3-failure-message",
                "%s",
                gt3_failure_message);

        if (extension == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto extension_create_failed;
        }
        rc = globus_hashtable_insert(
                &extensions,
                extension->attribute,
                extension);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto extension_insert_failed;
        }
        extension = NULL;
    }

    /* Response to initial job request. */
    rc = globus_gram_protocol_pack_job_request_reply_with_extensions(
            response_code,
            job_contact,
            &extensions,
            &reply,
            &replysize);

    if(rc == GLOBUS_SUCCESS)
    {
        rc = globus_gram_protocol_frame_reply(
                200,
                reply,
                replysize,
                &sendbuf,
                &sendsize);
    }
    else
    {
        rc = globus_gram_protocol_frame_reply(
                400,
                NULL,
                0,
                &sendbuf,
                &sendsize);
    }
    if(reply)
    {
        free(reply);
    }

    if (! response_context)
    {
        /*
         * No response context, the gatekeeper will supply HTTP response line,
         * so we skip the first line of the framed reply
         */
        reply = (globus_byte_t *) strstr((char *) sendbuf, "\r\n");

        reply += 2;
        sendsize -= (reply - sendbuf);

        memmove(sendbuf, reply, sendsize);
    }

    if(rc == GLOBUS_SUCCESS)
    {
        if (response_context != GSS_C_NO_CONTEXT)
        {
            major_status = globus_gss_assist_wrap_send(
                    &minor_status,
                    response_context,
                    (void *) sendbuf,
                    sendsize,
                    &token_status,
                    globus_l_gram_gss_send_fd,
                    (void *) (intptr_t) response_fd,
                    NULL);

            if (GSS_ERROR(major_status))
            {
                char *                  error_string = NULL;
                char *                  escaped_error_string;

                globus_gss_assist_display_status_str(
                        &error_string,
                        "",
                        major_status,
                        minor_status,
                        0);

                escaped_error_string =
                        globus_gram_prepare_log_string(error_string);

                globus_gram_job_manager_log(
                        manager,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                        "event=gram.reply.end "
                        "level=ERROR "
                        "gramid=%s "
                        "status=%d "
                        "major_status=%d "
                        "msg=\"%s\" "
                        "reason=\"%s\" "
                        "\n",
                        request->job_contact_path,
                        -GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED,
                        major_status,
                        "Error sending wrapped response",
                        escaped_error_string ? escaped_error_string : "");

                if (error_string)
                {
                    free(error_string);
                }
                if (escaped_error_string)
                {
                    free(escaped_error_string);
                }
            }
            else
            {
                globus_gram_job_manager_log(
                        manager,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                        "event=gram.reply.end "
                        "level=DEBUG "
                        "gramid=%s "
                        "status=%d "
                        "\n",
                        request ? request->job_contact_path : "",
                        0);
            }

            if ((!request) ||
                    (request && request->response_context != response_context))
            {
                /* In the case of a job restart via RSL, the response context
                 * will not be the same as the request context, but still must
                 * be freed
                 */
                gss_delete_sec_context(
                        &minor_status,
                        &response_context,
                        GSS_C_NO_BUFFER);
            }
            if (request && !manager->config->enable_callout)
            {
                /* Save a some memory by freeing this while the job runs */
                gss_delete_sec_context(
                        &minor_status,
                        &request->response_context,
                        GSS_C_NO_BUFFER);
                request->response_context = GSS_C_NO_CONTEXT;
            }
        }
        else
        {
            ssize_t written = 0, rc = 0;

            do
            {
                errno = 0;
                rc = write(response_fd, sendbuf + written, sendsize - written);
                if (rc < 0 && errno != EINTR)
                {
                    break;
                }
                written += rc;
            }
            while (written < sendsize);

            major_status = GSS_S_COMPLETE;
        }

        free(sendbuf);

        if (request && major_status != GSS_S_COMPLETE)
        {
            globus_gram_job_manager_request_set_status(request, GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);
            request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED;
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
        }
    }
    else
    {
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.reply.end "
                "level=ERROR "
                "gramid=%s "
                "status=%d "
                "msg=\"%s\" "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                -rc,
                major_status,
                "Error preparing response",
                globus_gram_protocol_error_string(rc));

        globus_gram_job_manager_request_set_status(
                request,
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);
        request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED;
    }

    if (request && rc != GLOBUS_SUCCESS)
    {
        request->failure_code = rc;
    }

extension_insert_failed:
    globus_gram_protocol_hash_destroy(&extensions);
extension_create_failed:
    if (extension)
    {
        free(extension->attribute);
        free(extension->value);
        free(extension);
    }
hashtable_init_failed:
    return rc;
}
/* globus_gram_job_manager_reply() */

int
globus_gram_job_manager_read_request(
    globus_gram_job_manager_t *         manager,
    int                                 fd,
    size_t                              content_length,
    char **                             rsl,
    char **                             client_contact,
    int *                               job_state_mask,
    globus_bool_t *                     version_only)
{
    int                                 rc;
    globus_hashtable_t                  extensions;
    globus_gram_protocol_extension_t *  entry;
    size_t                              amt_read = 0;
    globus_byte_t                       buffer[
                                            GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE];

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.read_request.start "
            "level=TRACE "
            "fd=%d\n",
            fd);

    *rsl = NULL;
    *client_contact = NULL;
    *job_state_mask = 0;
    *version_only = GLOBUS_FALSE;

    if (content_length > GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.read_request.end "
                "level=ERROR "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                -rc,
                "RSL too large");
        return rc;
    }
    do
    {
        errno = 0;

        rc = read(fd, buffer + amt_read, content_length - amt_read);

        if (rc < 0 && (errno == EINTR || errno == EAGAIN))
        {
            sleep(1);
            continue;
        }
        else if (rc < 0)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;

            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.read_request.end "
                    "level=ERROR "
                    "status=%d "
                    "msg=\"%s\" "
                    "errno=%d "
                    "reason=\"%s\"\n",
                    -rc,
                    "Error reading rsl",
                    errno,
                    strerror(errno));
            return rc;
        }
        else
        {
            amt_read += rc;
        }
    }
    while (amt_read < content_length);

    if (manager->config->log_levels & GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE)
    {
        char *                          message;

        message = globus_gram_prepare_log_string((char *) buffer);

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                "event=gram.read_request.info "
                "level=TRACE "
                "request_string=\"%s\" "
                "\n",
                message ? message : "");

        if (message != NULL)
        {
            free(message);
        }
    }

    rc = globus_gram_protocol_unpack_job_request(
            buffer,
            content_length,
            job_state_mask,
            client_contact,
            rsl);
    if(rc == GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED)
    {
        rc = globus_gram_protocol_unpack_message(
                (const char *) buffer,
                content_length,
                &extensions);

        if (rc == GLOBUS_SUCCESS)
        {
            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                    "event=gram.read_request.info "
                    "level=TRACE "
                    "msg=\"%s\" ",
                    "Message is not a job request, checking for version "
                    "request");

            entry = globus_hashtable_lookup(
                    &extensions,
                    "command");
            if (entry != NULL && strcmp(entry->value, "version") == 0)
            {
                *version_only = GLOBUS_TRUE;
            }
            else
            {
                char * buffer_escaped;

                buffer_escaped = globus_gram_prepare_log_string(
                        (const char *) buffer);

                rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
                globus_gram_job_manager_log(
                        manager,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                        "event=gram.read_request.end "
                        "level=ERROR "
                        "status=%d"
                        "request=\"%s\" "
                        "msg=\"%s\" "
                        "reason=\"%s\"\n",
                        -rc,
                        buffer_escaped ? buffer_escaped : "",
                        "Message is not a job or version request",
                        globus_gram_protocol_error_string(rc));
            }
            globus_gram_protocol_hash_destroy(&extensions);
        }
    }
    else if (rc != GLOBUS_SUCCESS)
    {
        char * buffer_escaped;

        buffer_escaped = globus_gram_prepare_log_string(
                (const char *) buffer);

        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.read_request.end level=ERROR status=%d"
                "request=\"%s\" "
                "msg=\"%s\" reason=\"%s\"\n",
                -rc,
                buffer_escaped ? buffer_escaped : "",
                "Error unpacking message",
                globus_gram_protocol_error_string(rc));
        return rc;
    }

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.read_request.end "
            "level=TRACE "
            "status=%d "
            "\n",
            0);
    return rc;
}
/* globus_gram_job_manager_read_request() */

/**
 * Do the state transition for handling a job manager restart.
 *
 * @param request
 *        The request to changes states.
 *
 * @return
 *       Returns GLOBUS_TRUE if if the job manager's state was
 *       changed as a result of this call; GLOBUS_FALSE otherwise.
 *
 * @note This case statement MUST cover all cases where the
 *        state file can be written (where
 *        globus_gram_job_manager_state_file_write()
 *        is called).
 */
static
globus_bool_t
globus_l_gram_job_manager_set_restart_state(
    globus_gram_jobmanager_request_t *  request)
{
    globus_bool_t                       changed = GLOBUS_FALSE;

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.set_restart_state.start "
            "level=TRACE "
            "gramid=%s "
            "jmstate=%s "
            "restart_state=%s "
            "\n",
            request->job_contact_path,
            globus_i_gram_job_manager_state_strings[request->jobmanager_state],
            globus_i_gram_job_manager_state_strings[request->restart_state]);

    switch(request->restart_state)
    {
      case GLOBUS_GRAM_JOB_MANAGER_STATE_SUBMIT:
        if (strcmp(request->config->jobmanager_type, "condor") == 0)
        {
            int rc;
            /*
             * Check if the condor submit happened prior to writing the
             * job state. if so, we'll have a log we can bootstrap the
             * job from without resubmitting
             */
            rc = globus_gram_job_manager_seg_parse_condor_id(
                request,
                &request->original_job_id_string);

            if (rc == GLOBUS_SUCCESS &&
                request->original_job_id_string != NULL)
            {
                request->job_id_string = strdup(
                        request->original_job_id_string );

                globus_gram_job_manager_seg_pause(request->manager);
                request->status = GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING;
                request->jobmanager_state =
                        GLOBUS_GRAM_JOB_MANAGER_STATE_SUBMIT;
                changed = GLOBUS_TRUE;
            }
        }
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STATE_START:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_COMMITTED:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_IN:
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY1:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY2:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2:
        request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_SUBMIT;
        changed = GLOBUS_TRUE;
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE:
        if (request->failure_code ==
                    GLOBUS_GRAM_PROTOCOL_ERROR_STAGE_OUT_FAILED ||
            request->failure_code ==
                    GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDOUT ||
            request->failure_code == GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDERR)
        {
            /* These failure types can be (possibly) remedied in the restart
             * RSL changing stdout or stderr destinations, so these will
             * be non-fatal. We'll clear the job status and proceed.
             */
            request->failure_code = GLOBUS_SUCCESS;
            globus_gram_job_manager_request_set_status(
                request,
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_OUT);
            request->unsent_status_change = GLOBUS_TRUE;
            request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1;
            changed = GLOBUS_TRUE;
            break;
        }
        else if (request->failure_code ==
                    GLOBUS_GRAM_PROTOCOL_ERROR_STAGING_EXECUTABLE ||
                request->failure_code ==
                    GLOBUS_GRAM_PROTOCOL_ERROR_STAGING_STDIN ||
                request->failure_code ==
                    GLOBUS_GRAM_PROTOCOL_ERROR_STAGE_IN_FAILED)
        {
            /* These failure types can be (possibly) remedied in the restart
             * RSL changing stdin, staging, or executable destinations, so
             * these will be non-fatal. We'll clear the job status and proceed.
             */
            request->failure_code = GLOBUS_SUCCESS;
            globus_gram_job_manager_request_set_status(
                request,
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED);
            request->unsent_status_change = GLOBUS_TRUE;
            request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_COMMITTED;
            changed = GLOBUS_TRUE;
            break;
        }
        else
        {
            /* These types of failures can't be helped by a change in stdio,
             * destination so we continue with the failure
             */
            globus_gram_job_manager_request_set_status(
                request, GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);
            request->unsent_status_change = GLOBUS_TRUE;
            request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1;
            break;
        }
      case GLOBUS_GRAM_JOB_MANAGER_STATE_CLOSE_OUTPUT:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_PRE_CLOSE_OUTPUT:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_OUT:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END_COMMITTED:
        globus_gram_job_manager_request_set_status(request, GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE);
        request->unsent_status_change = GLOBUS_TRUE;
        request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1;
        changed = GLOBUS_TRUE;
        break;
    }
    /*request->restart_state = GLOBUS_GRAM_JOB_MANAGER_STATE_START;*/

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.set_restart_state.end "
            "level=TRACE "
            "gramid=%s "
            "jmstate=%s "
            "restart_state=%s "
            "changed=%s "
            "\n",
            request->job_contact_path,
            globus_i_gram_job_manager_state_strings[request->jobmanager_state],
            globus_i_gram_job_manager_state_strings[request->restart_state],
            changed ? "true" : "false");

    return changed;
}
/* globus_l_gram_job_manager_set_restart_state() */

static
void
globus_l_gram_job_manager_cancel_queries(
    globus_gram_jobmanager_request_t *  request)
{
    globus_gram_job_manager_query_t *   query;

    while(!globus_fifo_empty(&request->pending_queries))
    {
        query = globus_fifo_dequeue(&request->pending_queries);
        query->failure_code = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_QUERY_DENIAL;

        /* Frees the query */
        globus_gram_job_manager_query_reply(
                request,
                query);

    }
}
/* globus_l_gram_job_manager_cancel_queries() */

/**
 * Validate that the job manager is running as the username specified in the
 * RSL if it is present.
 *
 * @param request
 *     Request which contains information about the job. We'll only look at
 *     the RSL in the request to check for presence of the username attribute.
 *
 * @retval GLOBUS_SUCCESS
 *     Either the username RSL attribute was not present, or it was present
 *     and its value matched the account this process is running as.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Some system call failed when we tried to look up the user id.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_USER_NAME
 *     This process is not running as the desired user.
 */
int
globus_gram_job_manager_validate_username(
    globus_gram_jobmanager_request_t *  request)
{
    char *                              tmp_str = NULL;
    char *                              buffer = NULL;
    struct passwd                       pwd;
    struct passwd *                     pwd_result = NULL;
    int                                 rc = GLOBUS_SUCCESS;
    uid_t                               uid;

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.validate_user.start "
            "level=TRACE "
            "gramid=%s "
            "\n",
            request->job_contact_path);

    /* Validate username RSL attribute if present */
    rc = globus_gram_job_manager_rsl_eval_one_attribute(
            request,
            GLOBUS_GRAM_PROTOCOL_USER_NAME,
            &tmp_str);

    if (rc != 0)
    {
        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.validate_user.end "
                "level=ERROR "
                "gramid=%s "
                "msg=\"%s\" "
                "attribute=%s "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                "Error evaluating attribute",
                GLOBUS_GRAM_PROTOCOL_USER_NAME,
                -rc,
                globus_gram_protocol_error_string(rc));

        return rc;
    }

    if (tmp_str != NULL)
    {
        buffer = malloc(1024);

        if (buffer == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.validate_user.end "
                    "level=ERROR "
                    "gramid=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "errno=%d "
                    "reason=\"%s\"\n",
                    request->job_contact_path,
                    "Malloc failed",
                    -rc,
                    errno,
                    strerror(errno));

            goto free_tmp_str_exit;
        }

        rc = globus_libc_getpwnam_r(
                tmp_str,
                &pwd,
                buffer,
                1024,
                &pwd_result);

        if (rc != 0 || pwd_result == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.validate_user.end "
                    "level=ERROR "
                    "gramid=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "errno=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    "getpwnam_r failed",
                    -rc,
                    errno,
                    strerror(errno));

            goto free_buffer_exit;
        }

        uid = getuid();

        if (pwd.pw_uid != uid)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_USER_NAME;

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.validate_user.end "
                    "level=ERROR "
                    "gramid=%s "
                    "msg=\"%s\" "
                    "uid=%u "
                    "desired_uid=%u "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    "Job manager not running as desired user",
                    uid,
                    pwd.pw_uid,
                    -rc,
                    globus_gram_protocol_error_string(rc));

            goto free_buffer_exit;
        }
    }
    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.validate_user.end "
            "level=TRACE "
            "gramid=%s "
            "status=%d "
            "\n",
            request->job_contact_path,
            0);

free_buffer_exit:
    if (buffer != NULL)
    {
        free(buffer);
    }

free_tmp_str_exit:
    if (tmp_str != NULL)
    {
        free(tmp_str);
    }

    return rc;
}
/* globus_gram_job_manager_validate_username() */

static
int
globus_l_gram_job_manager_add_cache_info(
    globus_gram_jobmanager_request_t *  request)
{
    char *                              out_file;
    char *                              fname;
    unsigned long                       timestamp;
    FILE *                              file;
    char *                              gk_id;
    int                                 rc;
 
    out_file = globus_common_create_string(
            "x-gass-cache://%s/%s/cache-info",
            request->config->hostname,
            request->uniq_id);

    rc = globus_gass_cache_add(
                request->cache_handle,
                out_file,
                request->cache_tag,
                GLOBUS_TRUE,
                &timestamp,
                &fname);   
    if(rc != GLOBUS_GASS_CACHE_ADD_NEW &&
       rc != GLOBUS_GASS_CACHE_ADD_EXISTS)
    {
        return GLOBUS_FAILURE;
    }

    file = fopen(fname,"w");
    if (file != NULL)
    {
        gk_id = getenv("GATEKEEPER_JM_ID");
        if (gk_id == NULL)
        {
            gk_id = "-";
        }
        fprintf(file,"%s\n%s\n%s\n%s\n",request->uniq_id,request->job_id_string,
                                        request->config->jobmanager_type,gk_id);
        fclose(file);
        time((time_t *)&timestamp);
        globus_gass_cache_add_done(
                request->cache_handle,
                out_file,   
                request->cache_tag,
                timestamp);
    }
    else
    {
        globus_gass_cache_delete(
                request->cache_handle,
                out_file,
                request->cache_tag,
                timestamp,
                GLOBUS_TRUE);
    }

    free(fname);
    free(out_file);
    return 0;
}
/* globus_l_gram_job_manager_add_cache_info() */

int
globus_i_gram_remote_io_url_update(
    globus_gram_jobmanager_request_t *  request)
{
    FILE *                              fp;
    int                                 rc = GLOBUS_SUCCESS;

    fp = fopen(request->remote_io_url_file, "w");
    if (fp == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_WRITING_REMOTE_IO_URL;

        goto fopen_failed;
    }
    rc = fprintf(fp, "%s\n", request->remote_io_url);
    if (rc < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_WRITING_REMOTE_IO_URL;

        goto fprintf_failed;

    }
fprintf_failed:
    rc = fclose(fp);
    if (rc == EOF)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_WRITING_REMOTE_IO_URL;
        goto fclose_failed;
    }

fclose_failed:
fopen_failed:
    return rc;
}
/* globus_i_gram_remote_io_url_update() */


/**
 * Write a GSSAPI token to a file descriptor
 *
 * Unlike the functions in globus_gss_assist, this uses file descriptors, not
 * FILE * values for @a arg. Writes a 4-byte header for the same conditions
 * as the function in globus_gss_assist.
 *
 * @param arg
 *     Void * cast of the file descriptor number
 * @param buffer
 *     GSSAPI token buffer to send
 * @param length
 *     Token length
 *     
 */
static
int
globus_l_gram_gss_send_fd(
    void *                              arg,
    void *                              buffer,
    size_t                              length)
{
    unsigned char                       lengthbuf[4];
    unsigned char *                     header = buffer;
    int                                 fd;
    int                                 rc;
    ssize_t                             written = 0;
    
    fd = (int) (intptr_t) arg;

    lengthbuf[0] = length >> 24;
    lengthbuf[1] = length >> 16;
    lengthbuf[2] = length >> 8;
    lengthbuf[3] = length;
    
    if (!(length > 5 && header[0] <= 26 && header[0] >= 20
          && ((header[1] == 3)
          || (header[1] == 2 && header[2] == 0))))
    {
        written = 0;
        do
        {
            rc = write((int) fd, lengthbuf + written, (size_t) (4 - written));
            if (rc < 0)
            {
                if (errno == EINTR)
                {
                    rc = 0;
                }
                else
                {
                    return GLOBUS_GSS_ASSIST_TOKEN_EOF;
                }
            }
            written += rc;
        }
        while (written < 4);
    }

    written = 0;
    do
    {
        rc = write((int) fd, header + written, (size_t) (length - written));
        if (rc < 0)
        {
            if (errno == EINTR)
            {
                rc = 0;
            }
            else
            {
                return GLOBUS_GSS_ASSIST_TOKEN_EOF;
            }
        }
        written += rc;
    }
    while (written < length);

    return 0;
}
/* globus_l_gram_gss_send_fd() */

static
void
globus_l_gram_file_cleanup(
    globus_gram_jobmanager_request_t *  request)
{
    globus_rsl_t *                      relation;
    globus_rsl_value_t *                value_sequence;
    globus_list_t *                     value_list;
    globus_rsl_value_t *                value;
    char *                              path;

    relation = globus_gram_job_manager_rsl_extract_relation(
            request->rsl,
            GLOBUS_GRAM_PROTOCOL_FILE_CLEANUP_PARAM);

    if (!relation)
    {
        goto not_found;
    }

    value_sequence = globus_rsl_relation_get_value_sequence(relation);
    if (!value_sequence)
    {
        goto no_sequence;
    }

    /*
     * GRAM-155: Leak in file_clean_up
     * Old code removed list elements and freed path in the loop, but
     * neglected to free value. Now all the data is freed in the
     * globus_rsl_free_recursive() call at the end.
     */
    value_list = globus_rsl_value_sequence_get_value_list(value_sequence);
    while (!globus_list_empty(value_list))
    {
        value = globus_list_first(value_list);
        value_list = globus_list_rest(value_list);

        path = globus_rsl_value_literal_get_string(value);

        if (path)
        {
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                    "event=gram.file_cleanup.info "
                    "level=TRACE "
                    "gramid=%s "
                    "path=\"%s\" "
                    "msg=\"About to unlink\" "
                    "\n",
                    request->job_contact_path,
                    path);
            remove(path);
        }
    }

no_sequence:
    globus_rsl_free_recursive(relation);
not_found:
    /*
     * GRAM-130: Individual Condor Logs per Job
     */
    if (strcmp(request->config->jobmanager_type, "condor") == 0)
    {
        char * condor_log = globus_common_create_string(
                "%s/condor.%s",
                request->config->job_state_file_dir,
                request->uniq_id);
        remove(condor_log);
        free(condor_log);
    }
    return;
}
/* globus_l_gram_file_cleanup() */

/**
 * Register the state machine callback callback for this request
 *
 * @param manager
 *     Job manager state
 * @param request
 *     Locked jobmanager request structure.
 * @param delay
 *     Oneshot delay
 */
int
globus_gram_job_manager_state_machine_register(
    globus_gram_job_manager_t *         manager,
    globus_gram_jobmanager_request_t *  request,
    globus_reltime_t *                  delay)
{
    int                                 rc = GLOBUS_SUCCESS;
    globus_result_t                     result;
    globus_reltime_t                    nodelay;
    if (delay == NULL)
    {
        GlobusTimeReltimeSet(nodelay, 0, 0);

        delay = &nodelay;
    }

    /* GRAM-128: Scalable reloading of requests at job manager restart.
     * It's possible now that the job manager has put this job id in the
     * pending_restarts list. When we add the reference here, if it is in that
     * list, the state machine will be registered for this job automatically,
     * with another new reference. So, we'll check if
     * (request->poll_timer == GLOBUS_NULL_HANDLE) below. If that is true,
     * then it wasn't in that list and we can proceed as we always had done;
     * otherwise, we'll need to remove the reference we just added.
     * 
     * See also globus_l_gram_process_pending_restarts() for the other case
     * where we might expect to add a reference to a partially-reloaded job.
     */
    rc = globus_gram_job_manager_add_reference(
            manager,
            request->job_contact_path,
            "state machine",
            NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_add_reference;
    }

    if (request->poll_timer == GLOBUS_NULL_HANDLE)
    {
        result = globus_callback_register_oneshot(
                &request->poll_timer,
                delay,
                globus_gram_job_manager_state_machine_callback,
                request);
        if (result != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            goto oneshot_failed;
        }
    }
    else
    {
        /* GRAM-128: Scalable reloading of requests at job manager restart.
         * If we get here, then the add_reference call added the state machine
         * callback already. We need to remove this duplicate reference to
         * avoid leaving the job in memory forever.
         */
        globus_gram_job_manager_remove_reference(
                manager,
                request->job_contact_path,
                "state machine");
    }

    if (rc != GLOBUS_SUCCESS)
    {
        /* Too bad, the state machine couldn't get registered. At least we
         * can drop the reference count to potentially free some memory.
         */
oneshot_failed:
        globus_gram_job_manager_remove_reference(
                manager,
                request->job_contact_path,
                "state machine");
failed_add_reference:
        ;
    }
    return rc;
}
/* globus_gram_job_manager_state_machine_register() */
