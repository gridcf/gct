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
 * @file globus_gram_job_manager_query.c
 * @brief Job Manager Query Handlers
 */
#include "globus_gram_job_manager.h"
#include "globus_callout.h"
#include "globus_callout_constants.h"
#include "globus_gsi_system_config.h"
#include "globus_gsi_system_config_constants.h"
#include "globus_gram_jobmanager_callout_error.h"
#include "globus_rsl_assist.h"
#include "version.h"

#include <string.h>
#endif

typedef struct globus_l_gram_renew_s
{
    globus_gram_protocol_handle_t       handle;
    globus_gram_jobmanager_request_t   *request;
}
globus_l_gram_renew_t;

static
globus_bool_t
globus_l_gram_job_manager_is_done(
    globus_gram_jobmanager_request_t *  request);

static
int
globus_l_gram_job_manager_cancel(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_handle_t       handle,
    globus_bool_t *                     reply);

static
int
globus_l_gram_job_manager_signal(
    globus_gram_jobmanager_request_t *  request,
    const char *                        args,
    globus_gram_protocol_handle_t       handle,
    globus_bool_t *                     reply);

static
int
globus_l_gram_job_manager_register(
    globus_gram_jobmanager_request_t *  request,
    const char *                        args);

static
int
globus_l_gram_job_manager_unregister(
    globus_gram_jobmanager_request_t *  request,
    const char *                        url,
    globus_gram_protocol_handle_t       handle);

static
int
globus_l_gram_job_manager_renew(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_handle_t       handle,
    globus_bool_t *                     reply);

static
void
globus_l_gram_job_manager_query_reply(
    globus_gram_job_manager_t *         manager,
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_handle_t       handle,
    int                                 status,
    int                                 query_failure_code,
    int                                 job_failure_code,
    int                                 exit_code);

static
globus_bool_t
globus_l_gram_job_manager_query_valid(
    globus_gram_jobmanager_request_t *  request);

static
void
globus_l_delegation_callback(
    void *                              arg,
    globus_gram_protocol_handle_t       handle,
    gss_cred_id_t                       credential,
    int                                 error_code);

static
int
globus_l_gram_job_manager_query_stop_manager(
    globus_gram_jobmanager_request_t *  request);

static
int
globus_l_gram_create_extensions(
    globus_gram_job_manager_t *         manager,
    globus_gram_jobmanager_request_t *  request,
    int                                 status,
    int                                 exit_code,
    globus_hashtable_t *                extensions);

static
const char *
globus_l_gram_get_job_contact_from_uri(
    const char *                        uri);

static
int
globus_l_gram_stdio_update_signal(
    globus_gram_jobmanager_request_t *  request,
    char *                              update_rsl_spec);

void
globus_gram_job_manager_query_callback(
    void *                              arg,
    globus_gram_protocol_handle_t       handle,
    globus_byte_t *                     buf,
    globus_size_t                       nbytes,
    int                                 errorcode,
    char *                              uri)
{
    globus_gram_job_manager_t *         manager = arg;
    globus_gram_jobmanager_request_t *  request = NULL;
    char *                              query           = GLOBUS_NULL;
    char *                              rest;
    int                                 rc = 0;
    globus_gram_protocol_job_state_t    status = 0;
    int                                 exit_code = 0;
    int                                 job_failure_code = 0;
    globus_bool_t                       reply           = GLOBUS_TRUE;
    const char *                        contact;

    globus_gram_job_manager_log(
        manager,
        GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
        "event=gram.query.start "
        "level=DEBUG "
        "uri=\"%s\" "
        "\n",
        uri);

    if (manager->config->log_levels & GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE)
    {
        char *                          querystring;

        querystring = globus_gram_prepare_log_string((char *) buf);

        globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.query.info "
            "level=TRACE "
            "uri=\"%s\" "
            "message=\"%s\" "
            "\n",
            uri,
            querystring ? querystring : "");

        if (querystring)
        {
            free(querystring);
        }
    }

    if (uri == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_CONTACT_NOT_FOUND;

        globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
            "event=gram.query.end "
            "level=ERROR "
            "status=%d "
            "uri=%s "
            "msg=\"%s\" "
            "reason=\"%s\" "
            "\n",
            -rc,
            "NULL",
            "Invalid URI for query",
            globus_gram_protocol_error_string(rc));

        goto invalid_query;
    }

    contact = globus_l_gram_get_job_contact_from_uri(uri);
    if (contact == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_CONTACT_NOT_FOUND;

        globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
            "event=gram.query.end "
            "level=ERROR "
            "status=%d "
            "uri=%s "
            "msg=\"%s\" "
            "reason=\"%s\" "
            "\n",
            -rc,
            uri,
            "Invalid URI for query",
            globus_gram_protocol_error_string(rc));

        goto invalid_query;
    }

    rc = globus_gram_protocol_unpack_status_request(buf, nbytes, &query);
    if (rc != GLOBUS_SUCCESS)
    {
        goto unpack_failed;
    }

    rc = globus_gram_job_manager_authz_query(
            manager,
            handle,
            contact,
            query);
    if (rc != GLOBUS_SUCCESS)
    {
        goto authz_failed;
    }

    rest = strchr(query,' ');
    if (rest)
    {
        *rest++ = '\0';
    }

    /* When status query occurs, skip reloading the job request. Use the cached
     * value in the ref.
     */
    if (strcmp(query, "status") == 0)
    {
        rc = globus_gram_job_manager_get_status(
                manager,
                contact,
                &status,
                &job_failure_code,
                &exit_code);
        if (rc != GLOBUS_SUCCESS)
        {
            globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.query.end "
                "level=ERROR "
                "status=%d "
                "uri=%s "
                "msg=\"%s\" "
                "reason=\"%s\" "
                "\n",
                -rc,
                contact,
                "Unable to find job for URI",
                globus_gram_protocol_error_string(rc));

        }
        goto status_done;
    }

    rc = globus_gram_job_manager_add_reference(
            manager,
            contact,
            "query",
            &request);

    if (rc != GLOBUS_SUCCESS)
    {
        globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
            "event=gram.query.end "
            "level=ERROR "
            "status=%d "
            "uri=%s "
            "msg=\"%s\" "
            "reason=\"%s\" "
            "\n",
            -rc,
            contact,
            "Unable to find job for URI",
            globus_gram_protocol_error_string(rc));

        goto invalid_query;
    }

    GlobusGramJobManagerRequestLock(request);
    job_failure_code = request->failure_code;
    status = request->status;

    if (strcmp(query,"cancel")==0)
    {
        rc = globus_l_gram_job_manager_cancel(request, handle, &reply);
    }
    else if (strcmp(query,"signal")==0)
    {
        rc = globus_l_gram_job_manager_signal(request, rest, handle, &reply);
        request->job_stats.signal_count++;
    }
    else if (strcmp(query,"register")==0)
    {
        rc = globus_l_gram_job_manager_register(request, rest);
        request->job_stats.register_count++;
    }
    else if (strcmp(query,"unregister")==0)
    {
        rc = globus_l_gram_job_manager_unregister(request, rest, handle);
        request->job_stats.unregister_count++;
    }
    else if (strcmp(query,"renew")==0)
    {
        rc = globus_l_gram_job_manager_renew(request, handle, &reply);
        request->job_stats.refresh_count++;
    }
    else
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_QUERY;
    }

unpack_failed:
authz_failed:
    if (rc != GLOBUS_SUCCESS)
    {
        status = GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED;
        job_failure_code = 0;
    }


    globus_gram_job_manager_log(
            manager,
            rc ? GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR
                    : GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.query.end "
            "level=%s "
            "%s%s%s"
            "uri=\"%s\" "
            "msg=\"%s\" "
            "status=%d "
            "%s%s "
            "\n",
            rc ? "ERROR" : "DEBUG",
            request ? "gramid=" : "",
            request ? request->job_contact_path : "",
            request ? " " : "",
            uri,
            rc ? "Error processing query" : "Done processing query" ,
            rc ? -rc : 0,
            rc ? "reason=\"" : "",
            rc ? globus_gram_protocol_error_string(rc) : "",
            rc ? "\"" : "");

status_done:
invalid_query:
    if(reply)
    {
        globus_l_gram_job_manager_query_reply(
                manager,
                request,
                handle,
                status,
                rc,
                job_failure_code,
                exit_code);

    }
    if (request)
    {
        GlobusGramJobManagerRequestUnlock(request);
        rc = globus_gram_job_manager_remove_reference(
                request->manager,
                request->job_contact_path,
                "query");
    }

    if(query)
    {
        free(query);
    }

    return;
}
/* globus_gram_job_manager_query_callback() */

void
globus_gram_job_manager_query_reply(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_query_t *   query)
{
    if(query->type == GLOBUS_GRAM_JOB_MANAGER_CANCEL ||
       query->signal == GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_CANCEL)
    {
        if(query->failure_code == GLOBUS_GRAM_PROTOCOL_ERROR_USER_CANCELLED)
        {
            query->failure_code = GLOBUS_SUCCESS;
        }
    }
    globus_l_gram_job_manager_query_reply(request->manager,
                                          request,
                                          query->handle,
                                          request->status,
                                          query->failure_code,
                                          query->failure_code
                                              ? 0
                                              : request->failure_code,
                                          request->exit_code);
    if(query->signal_arg)
    {
        free(query->signal_arg);
    }
    free(query);
}
/* globus_gram_job_manager_query_reply() */

static
void
globus_l_gram_job_manager_query_reply(
    globus_gram_job_manager_t *         manager,
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_handle_t       handle,
    int                                 status,
    int                                 query_failure_code,
    int                                 job_failure_code,
    int                                 exit_code)
{
    int                                 rc;
    int                                 code;
    globus_size_t                       replysize;
    globus_byte_t *                     reply             = GLOBUS_NULL;
    globus_hashtable_t                  extensions = NULL;

    rc = query_failure_code;

    if (rc != GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED)
    {
        globus_l_gram_create_extensions(manager, request, status, exit_code, &extensions);

        if (extensions != NULL)
        {
            rc = globus_gram_protocol_pack_status_reply_with_extensions(
                status,
                rc,
                job_failure_code,
                &extensions,
                &reply,
                &replysize );
        }
        else
        {
            rc = globus_gram_protocol_pack_status_reply(
                status,
                rc,
                job_failure_code,
                &reply,
                &replysize );
        }
    }
    if (rc == GLOBUS_SUCCESS)
    {
        code = 200;
    }
    else
    {
        code = 400;

        free(reply);
        reply = GLOBUS_NULL;
        replysize = 0;
    }
    if (request == NULL ||
            request->job_log_level & GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE)
    {
        char *                          replystring;

        replystring = globus_gram_prepare_log_string((char *) reply);

        globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.query.info "
            "%s%s%s"
            "level=TRACE "
            "reply=\"%s\" "
            "\n",
            request ? "gramid=" : "",
            request ? request->job_contact_path : "",
            request ? " " : "",
            replystring ? replystring : "");

        if (replystring)
        {
            free(replystring);
        }
    }

    globus_gram_protocol_reply(handle,
                               code,
                               reply,
                               replysize);

    if(reply)
    {
        free(reply);
    }
    if (extensions)
    {
        globus_gram_protocol_hash_destroy(&extensions);
    }
}
/* globus_l_gram_job_manager_query_reply() */

static
int
globus_l_gram_job_manager_cancel(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_handle_t       handle,
    globus_bool_t *                     reply)
{
    int                                 rc              = GLOBUS_SUCCESS;
    globus_result_t                     result;
    globus_gram_job_manager_query_t *   query;
    globus_reltime_t                    delay;

    switch(request->jobmanager_state)
    {
    case GLOBUS_GRAM_JOB_MANAGER_STATE_START:
        request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED;
        globus_gram_job_manager_request_set_status(
                request,
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);
        request->failure_code = GLOBUS_GRAM_PROTOCOL_ERROR_USER_CANCELLED;

        return GLOBUS_SUCCESS;

    case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE:
        request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED;
        globus_gram_job_manager_request_set_status(
                request,
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);
        request->failure_code = GLOBUS_GRAM_PROTOCOL_ERROR_USER_CANCELLED;
        request->unsent_status_change = GLOBUS_TRUE;
        if(request->poll_timer != GLOBUS_HANDLE_TABLE_NO_HANDLE)
        {
            GlobusTimeReltimeSet(delay, 0, 0);
            result = globus_callback_adjust_oneshot(
                    request->poll_timer,
                    &delay);
        }

        return GLOBUS_SUCCESS;

    case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_COMMITTED:
    case GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_IN:
        request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED;
        globus_gram_job_manager_request_set_status(
                request,
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);
        request->failure_code = GLOBUS_GRAM_PROTOCOL_ERROR_USER_CANCELLED;
        request->unsent_status_change = GLOBUS_TRUE;
        return GLOBUS_SUCCESS;

    case GLOBUS_GRAM_JOB_MANAGER_STATE_SUBMIT:
    case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1:
    case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2:
    case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY1:
    case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY2:
        query = calloc(1, sizeof(globus_gram_job_manager_query_t));
  
        query->type = GLOBUS_GRAM_JOB_MANAGER_CANCEL;
        query->handle = handle;
        query->signal = 0;
        query->signal_arg = NULL;
  
        globus_fifo_enqueue(&request->pending_queries, query);
        *reply = GLOBUS_FALSE;
  
        if(request->jobmanager_state == GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2)
        {
            request->jobmanager_state =
                    GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY1;
            if(request->poll_timer != GLOBUS_HANDLE_TABLE_NO_HANDLE)
            {
                GlobusTimeReltimeSet(delay, 0, 0);
                result = globus_callback_adjust_oneshot(
                        request->poll_timer,
                        &delay);
            }
            else
            {
                globus_gram_job_manager_state_machine_register(
                        request->manager,
                        request,
                        NULL);
            }
        }
        return GLOBUS_SUCCESS;
    default:
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_QUERY_DENIAL;
        *reply = GLOBUS_TRUE;
        return rc;
    }
}
/* globus_l_gram_job_manager_cancel() */

static
int
globus_l_gram_job_manager_register(
    globus_gram_jobmanager_request_t *  request,
    const char *                        args)
{
    int                                 rc = GLOBUS_SUCCESS;
    char *                              url = NULL;
    int                                 mask;

    url = malloc(strlen(args));

    if (globus_l_gram_job_manager_is_done(request))
    {
       rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_QUERY_DENIAL;
    }
    else if(sscanf(args, "%d %s", &mask, url) != 2)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
    }
    else
    {
        rc = globus_gram_job_manager_contact_add(request, url, mask);

    }
    free(url);

    return rc;
}
/* globus_l_gram_job_manager_register() */

static
int
globus_l_gram_job_manager_unregister(
    globus_gram_jobmanager_request_t *  request,
    const char *                        url,
    globus_gram_protocol_handle_t       handle)

{
    int rc;

    if (globus_l_gram_job_manager_is_done(request))
    {
       rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_QUERY_DENIAL;
    }
    else if (!url || strlen(url) == 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
    }
    else
    {
        rc = globus_gram_job_manager_contact_remove(request, url);

        /* Incase we unregister the last callback and we're waiting
         * for TWO_PHASE_END commit, fake the COMMIT_END signal
         */

        if (!request->client_contacts &&
             request->jobmanager_state == GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END)
        {
            globus_bool_t reply=GLOBUS_TRUE;
            char buf[32];

            snprintf(buf,sizeof(buf),"%d",GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_COMMIT_END);
            globus_l_gram_job_manager_signal(request,buf,handle,&reply);
            globus_assert(reply == GLOBUS_TRUE);
        }
    }
    return rc;
}
/* globus_l_gram_job_manager_unregister() */

static
int
globus_l_gram_job_manager_renew(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_handle_t       handle,
    globus_bool_t *                     reply)
{
    int                                 rc = 0;
    globus_l_gram_renew_t *             renew;
    char                               *msg = "Success";

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.proxyrenew.start "
            "level=DEBUG "
            "gramid=%s "
            "\n",
            request->job_contact_path);

    renew = malloc(sizeof(globus_l_gram_renew_t));
    if(renew == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_QUERY_DENIAL;
        msg = "Malloc failed";

        goto renew_malloc_failed;
    }

    rc = globus_gram_job_manager_add_reference(
            request->manager,
            request->job_contact_path,
            "renew",
            &renew->request);
    renew->handle = handle;

    if (rc != GLOBUS_SUCCESS)
    {
        msg = "Add reference failed";
        goto add_reference_failed;
    }

    rc = globus_gram_protocol_accept_delegation(
            renew->handle,
            GSS_C_NO_OID_SET,
            GSS_C_NO_BUFFER_SET,
            GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG |
                GSS_C_GLOBUS_SSL_COMPATIBLE,
            0,
            globus_l_delegation_callback,
            renew);
    if (rc == GLOBUS_SUCCESS)
    {
        *reply = GLOBUS_FALSE;
    }
    else
    {
        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.proxyrenew.end "
                "level=ERROR "
                "gramid=%s "
                "status=%d "
                "msg=\"%s\" "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                -rc,
                msg,
                globus_gram_protocol_error_string(rc));
        
        globus_gram_job_manager_remove_reference(
                request->manager,
                request->job_contact_path,
                "renew");
add_reference_failed:
        free(renew);
renew_malloc_failed:
        *reply = GLOBUS_TRUE;
    }

    return rc;
}
/* globus_l_gram_job_manager_renew() */

static
int
globus_l_gram_job_manager_signal(
    globus_gram_jobmanager_request_t *  request,
    const char *                        args,
    globus_gram_protocol_handle_t       handle,
    globus_bool_t *                     reply)
{
    int                                 rc = GLOBUS_SUCCESS;
    int                                 signal;
    char *                              after_signal;
    globus_off_t                        out_size = -1;
    globus_off_t                        err_size = -1;
    globus_reltime_t                    delay;
    globus_gram_job_manager_query_t *   query;
    globus_result_t                     result;

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.signal.start "
            "level=DEBUG "
            "gramid=%s "
            "signal=\"%s\" "
            "msg=\"%s\" "
            "jmstate=%s "
            "\n",
            request->job_contact_path,
            args,
            "GRAM signal",
            globus_i_gram_job_manager_state_strings[request->jobmanager_state]);

    *reply = GLOBUS_TRUE;
    if(args == NULL || sscanf(args, "%d", &signal) != 1)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
    }
    after_signal = strchr(args,' ');
    if (after_signal)
        *after_signal++ = '\0';

    switch(signal)
    {
    case GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_STDIO_UPDATE:
        if(!after_signal || strlen(after_signal) == 0)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.signal.end "
                    "level=WARN "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Missing signal argument",
                    -rc,
                    globus_gram_protocol_error_string(rc));
            break;
        }
        if(!globus_l_gram_job_manager_query_valid(request))
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_QUERY_DENIAL;

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.signal.end "
                    "level=WARN "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Invalid query",
                    -rc,
                    globus_gram_protocol_error_string(rc));
            break;
        }
        rc = globus_l_gram_stdio_update_signal(request, after_signal);
        break;
    case GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_CANCEL:
    case GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_SUSPEND:
    case GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_RESUME:
    case GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_PRIORITY:
        if(!after_signal || strlen(after_signal) == 0)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.signal.end "
                    "level=WARN "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Missing signal argument",
                    -rc,
                    globus_gram_protocol_error_string(rc));
            break;
        }
        if(!globus_l_gram_job_manager_query_valid(request))
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_QUERY_DENIAL;

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.signal.end "
                    "level=WARN "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Invalid query",
                    -rc,
                    globus_gram_protocol_error_string(rc));
            break;
        }
        query = calloc(1, sizeof(globus_gram_job_manager_query_t));

        query->type = GLOBUS_GRAM_JOB_MANAGER_SIGNAL;
        query->handle = handle;
        query->signal = signal;
        if(after_signal)
        {
            query->signal_arg = strdup(after_signal);
            if (query->signal_arg == NULL)
            {
                free(query);
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

                globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                        "event=gram.signal.end "
                        "level=WARN "
                        "gramid=%s "
                        "signal=\"%s\" "
                        "jmstate=%s "
                        "msg=\"%s\" "
                        "status=%d "
                        "reason=\"%s\" "
                        "\n",
                        request->job_contact_path,
                        args,
                        globus_i_gram_job_manager_state_strings[
                                request->jobmanager_state],
                        "malloc failed",
                        -rc,
                        globus_gram_protocol_error_string(rc));
                break;
            }
        }

        globus_fifo_enqueue(&request->pending_queries, query);
        *reply = GLOBUS_FALSE;

        if(request->jobmanager_state == GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY1;

        }
        else if (request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY1;
        }
        else
        {
            break;
        }
        if(request->poll_timer != GLOBUS_HANDLE_TABLE_NO_HANDLE)
        {
            GlobusTimeReltimeSet(delay, 0, 0);
            result = globus_callback_adjust_oneshot(
                    request->poll_timer,
                    &delay);
        }
        else
        {
            globus_gram_job_manager_state_machine_register(
                    request->manager,
                    request,
                    NULL);
        }
        break;

    case GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_COMMIT_REQUEST:
        if(request->two_phase_commit == 0)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_COMMIT;

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.signal.end "
                    "level=WARN "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Two-phase commit signal when job doesn't have two_phase timeout",
                    -rc,
                    globus_gram_protocol_error_string(rc));
            break;
        }
        else if(request->jobmanager_state ==
                    GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE ||
		request->jobmanager_state == 
                    GLOBUS_GRAM_JOB_MANAGER_STATE_START)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_COMMITTED;
        }
        else if(request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY1 ||
                request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY2)
        {
            request->jobmanager_state =
                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_COMMITTED;
        }
        else if (request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_STOP)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_QUERY_DENIAL;

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                    "event=gram.signal.info "
                    "level=DEBUG "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Unnecessary two-phase commit signal",
                    -rc,
                    globus_gram_protocol_error_string(rc));
            break;
        }
        else
        {
            /* GRAM-103: Ease two phase end commit timeout
             * In some cases, Condor-G decides to restart a job where
             * the job manager is already running. When this happens,
             * the job can be in pretty much any job manager state. We'll
             * ignore any error here, and assume things are working just
             * fine in the state machine.
             */
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                    "event=gram.signal.info "
                    "level=DEBUG "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Unnecessary two-phase commit signal",
                    -rc,
                    globus_gram_protocol_error_string(rc));
            break;
        }
        if(request->poll_timer != GLOBUS_HANDLE_TABLE_NO_HANDLE)
        {
            GlobusTimeReltimeSet(delay, 0, 0);
            result = globus_callback_adjust_oneshot(
                    request->poll_timer,
                    &delay);
        }
        else
        {
            globus_gram_job_manager_state_machine_register(
                    request->manager,
                    request,
                    NULL);
        }
        break;

    case GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_COMMIT_END:
        if(request->two_phase_commit == 0)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_COMMIT;

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.signal.end "
                    "level=WARN "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Two-phase commit signal when job doesn't have two_phase timeout",
                    -rc,
                    globus_gram_protocol_error_string(rc));
            break;
        }
        else if(request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END ||
                request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_STOP)
        {
            request->jobmanager_state =
                    GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END_COMMITTED;
        }
        else if(request->jobmanager_state ==
                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE)
        {
            request->jobmanager_state =
                    GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE_COMMITTED;
        }
        else
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_QUERY_DENIAL;

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.signal.end "
                    "level=WARN "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Two-phase commit signal in invalid jobmanager state",
                    -rc,
                    globus_gram_protocol_error_string(rc));
            break;
        }
        if(request->poll_timer != GLOBUS_HANDLE_TABLE_NO_HANDLE)
        {
            GlobusTimeReltimeSet(delay, 0, 0);
            result = globus_callback_adjust_oneshot(
                    request->poll_timer,
                    &delay);
        }
        else
        {
            globus_gram_job_manager_state_machine_register(
                    request->manager,
                    request,
                    NULL);
        }
        break;

    case GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_COMMIT_EXTEND:
        if ((!after_signal) || (strlen(after_signal) == 0))
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.signal.end "
                    "level=WARN "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Missing argument to commit extend signal",
                    -rc,
                    globus_gram_protocol_error_string(rc));
        }
        else if(request->two_phase_commit == 0)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_COMMIT;

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.signal.end "
                    "level=WARN "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Two-phase commit extend signal when job doesn't have two_phase timeout",
                    -rc,
                    globus_gram_protocol_error_string(rc));
        }
        else if((request->jobmanager_state ==
                 GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE) ||
                (request->jobmanager_state ==
                 GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END) ||
                (request->jobmanager_state ==
                 GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE) ||
                (request->jobmanager_state ==
                 GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY1) ||
                (request->jobmanager_state ==
                 GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY2))
        {
            request->commit_extend += atoi(after_signal);
        }
        break;

    case GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_STDIO_SIZE:
        if (after_signal &&
                sscanf(after_signal,
                       "%"GLOBUS_OFF_T_FORMAT" %"GLOBUS_OFF_T_FORMAT,
                       &out_size, &err_size) > 0)
        {
            struct stat st;
            globus_off_t local_size_stdout = 0;
            globus_off_t local_size_stderr = 0;
            const char * local_stdout;
            const char * local_stderr;

            rc = globus_gram_job_manager_rsl_attribute_get_string_value(
                request->rsl,
                GLOBUS_GRAM_PROTOCOL_STDOUT_PARAM,
                &local_stdout);
            if (rc != GLOBUS_SUCCESS)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_QUERY;
                globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                        "event=gram.signal.end "
                        "level=WARN "
                        "gramid=%s "
                        "signal=\"%s\" "
                        "jmstate=%s "
                        "msg=\"%s\" "
                        "status=%d "
                        "reason=\"%s\" "
                        "\n",
                        request->job_contact_path,
                        args,
                        globus_i_gram_job_manager_state_strings[
                                request->jobmanager_state],
                        "Stdio size signal when stdout not in RSL",
                        -rc,
                        globus_gram_protocol_error_string(rc));
                break;
            }
            if (local_stdout == NULL || strstr(local_stdout, "://"))
            {
                local_stdout = request->cached_stdout;
            }
            rc = globus_gram_job_manager_rsl_attribute_get_string_value(
                request->rsl,
                GLOBUS_GRAM_PROTOCOL_STDERR_PARAM,
                &local_stderr);
            if (rc != GLOBUS_SUCCESS)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_QUERY;
                globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                        "event=gram.signal.end "
                        "level=WARN "
                        "gramid=%s "
                        "signal=\"%s\" "
                        "jmstate=%s "
                        "msg=\"%s\" "
                        "status=%d "
                        "reason=\"%s\" "
                        "\n",
                        request->job_contact_path,
                        args,
                        globus_i_gram_job_manager_state_strings[
                                request->jobmanager_state],
                        "Stdio size signal when stderr not in RSL",
                        -rc,
                        globus_gram_protocol_error_string(rc));
                break;
            }
            if (local_stderr == NULL || strstr(local_stderr, "://"))
            {
                local_stderr = request->cached_stderr;
            }
            if (!globus_list_empty(request->stage_stream_todo))
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_STILL_STREAMING;
                globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                        "event=gram.signal.end "
                        "level=WARN "
                        "gramid=%s "
                        "signal=\"%s\" "
                        "jmstate=%s "
                        "msg=\"%s\" "
                        "status=%d "
                        "reason=\"%s\" "
                        "\n",
                        request->job_contact_path,
                        args,
                        globus_i_gram_job_manager_state_strings[
                                request->jobmanager_state],
                        "Stdio size signal when output still streaming",
                        -rc,
                        globus_gram_protocol_error_string(rc));
                break;
            }
            if (strcmp(local_stdout, request->cached_stdout) == 0)
            {
                /* fakestreaming is likely to happen */
                rc = stat(local_stdout, &st);
                if (rc < 0)
                {
                    rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_QUERY;
                    globus_gram_job_manager_request_log(
                            request,
                            GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                            "event=gram.signal.end "
                            "level=WARN "
                            "gramid=%s "
                            "signal=\"%s\" "
                            "jmstate=%s "
                            "msg=\"%s\" "
                            "status=%d "
                            "errno=%d "
                            "reason=\"%s\" "
                            "\n",
                            request->job_contact_path,
                            args,
                            globus_i_gram_job_manager_state_strings[
                                    request->jobmanager_state],
                            "Unable get stdout file size",
                            -rc,
                            errno,
                            strerror(errno));
                    break;
                }
                local_size_stdout = st.st_size;
            }

            if (strcmp(local_stderr, request->cached_stderr) == 0)
            {
                /* fakestreaming is likely to happen */
                rc = stat(local_stderr, &st);
                if (rc < 0)
                {
                    rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_QUERY;
                    globus_gram_job_manager_request_log(
                            request,
                            GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                            "event=gram.signal.end "
                            "level=WARN "
                            "gramid=%s "
                            "signal=\"%s\" "
                            "jmstate=%s "
                            "msg=\"%s\" "
                            "status=%d "
                            "errno=%d "
                            "reason=\"%s\" "
                            "\n",
                            request->job_contact_path,
                            args,
                            globus_i_gram_job_manager_state_strings[
                                    request->jobmanager_state],
                            "Unable get stderr file size",
                            -rc,
                            errno,
                            strerror(errno));
                    break;
                }
                local_size_stderr = st.st_size;
            }

            if (out_size >= 0 && out_size != local_size_stdout)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_STDIO_SIZE;
                globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                        "event=gram.signal.end "
                        "level=WARN "
                        "gramid=%s "
                        "signal=\"%s\" "
                        "jmstate=%s "
                        "msg=\"%s\" "
                        "status=%d "
                        "stdout_signal_size=%ld "
                        "stdout_actual_size=%ld "
                        "reason=\"%s\" "
                        "\n",
                        request->job_contact_path,
                        args,
                        globus_i_gram_job_manager_state_strings[
                                request->jobmanager_state],
                        "Stdout size mismatch",
                        -rc,
                        (long int) out_size,
                        (long int) local_size_stdout,
                        globus_gram_protocol_error_string(rc));
            }
            else if (err_size >= 0 && err_size != local_size_stderr)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_STDIO_SIZE;
                globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                        "event=gram.signal.end "
                        "level=WARN "
                        "gramid=%s "
                        "signal=\"%s\" "
                        "jmstate=%s "
                        "msg=\"%s\" "
                        "status=%d "
                        "stderr_signal_size=%ld "
                        "stderr_actual_size=%ld "
                        "reason=\"%s\" "
                        "\n",
                        request->job_contact_path,
                        args,
                        globus_i_gram_job_manager_state_strings[
                                request->jobmanager_state],
                        "Stderr size mismatch",
                        -rc,
                        (long int) err_size,
                        (long int) local_size_stderr,
                        globus_gram_protocol_error_string(rc));
            }
            else
            {
                rc = GLOBUS_SUCCESS;
            }
        }
        else
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.signal.end "
                    "level=WARN "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Missing argument to stdio_size signal",
                    -rc,
                    globus_gram_protocol_error_string(rc));
        }
        break;

    case GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_STOP_MANAGER:
        rc = globus_l_gram_job_manager_query_stop_manager(request);

        if (rc != GLOBUS_SUCCESS)
        {
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.signal.end "
                    "level=WARN "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Error stopping manager",
                    -rc,
                    globus_gram_protocol_error_string(rc));
        }

        break;
    default:
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_UNKNOWN_SIGNAL_TYPE;
        if (rc != GLOBUS_SUCCESS)
        {
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.signal.end "
                    "level=WARN "
                    "gramid=%s "
                    "signal=\"%s\" "
                    "jmstate=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    args,
                    globus_i_gram_job_manager_state_strings[
                            request->jobmanager_state],
                    "Unknown signal",
                    -rc,
                    globus_gram_protocol_error_string(rc));
        }
    }

    if (rc == GLOBUS_SUCCESS)
    {
        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                "event=gram.signal.end "
                "level=DEBUG "
                "gramid=%s "
                "signal=\"%s\" "
                "jmstate=%s "
                "msg=\"%s\" "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                args,
                globus_i_gram_job_manager_state_strings[
                        request->jobmanager_state],
                "Signal processed or queued",
                0,
                globus_gram_protocol_error_string(rc));
    }
    return rc;
}
/* globus_l_gram_job_manager_signal() */

/**
 * Handle a STOP_MANAGER signal.
 *
 * This signal causes the job manager to stop monitoring the job and exit,
 * without killing the job. We want this stop to happen pretty quickly, so
 * we'll unregister any poll_timer events (either the intra-poll delay or
 * the two_phase_commit delay) and reregister as a oneshot.
 */
static
int
globus_l_gram_job_manager_query_stop_manager(
    globus_gram_jobmanager_request_t *  request)
{
    int                                 rc = GLOBUS_SUCCESS;
    globus_gram_jobmanager_state_t      state;
    globus_reltime_t                    delay;

    state = request->jobmanager_state;

    if(state == GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2)
    {
        if(request->poll_timer != GLOBUS_HANDLE_TABLE_NO_HANDLE)
        {
            GlobusTimeReltimeSet(delay, 0, 0);
            globus_callback_adjust_oneshot(
                request->poll_timer,
                &delay);
        }
        else
        {
            globus_gram_job_manager_state_machine_register(
                  request->manager,
                  request,
                  NULL);
        }
    }

    switch(state)
    {
        case GLOBUS_GRAM_JOB_MANAGER_STATE_STOP:
          request->unsent_status_change = GLOBUS_TRUE;
          request->stop_reason = GLOBUS_GRAM_PROTOCOL_ERROR_JM_STOPPED;
          request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_STOP;
          break;
        case GLOBUS_GRAM_JOB_MANAGER_STATE_START:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY1:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY2:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_COMMITTED:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_IN:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_SUBMIT:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY1:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY2:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_PRE_CLOSE_OUTPUT:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_CLOSE_OUTPUT:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_OUT:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END_COMMITTED:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE_COMMITTED:
          request->unsent_status_change = GLOBUS_TRUE;
          request->stop_reason = GLOBUS_GRAM_PROTOCOL_ERROR_JM_STOPPED;
          request->restart_state = request->jobmanager_state;
          request->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_STOP;
          break;
        case GLOBUS_GRAM_JOB_MANAGER_STATE_FILE_CLEAN_UP:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_SCRATCH_CLEAN_UP:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_CACHE_CLEAN_UP:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_DONE:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_DONE:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_FILE_CLEAN_UP:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_SCRATCH_CLEAN_UP:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CACHE_CLEAN_UP:
        case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CLOSE_OUTPUT:
          rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_QUERY_DENIAL;
          break;
    }
    return rc;
}
/* globus_l_gram_job_manager_query_stop_manager() */

static
globus_bool_t
globus_l_gram_job_manager_is_done(
    globus_gram_jobmanager_request_t *  request)
{
    if(request->jobmanager_state == GLOBUS_GRAM_JOB_MANAGER_STATE_DONE ||
       request->jobmanager_state
           == GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_DONE)
    {
        return GLOBUS_TRUE;
    }

    return GLOBUS_FALSE;
}
/* globus_l_gram_job_manager_is_done() */

static
globus_bool_t
globus_l_gram_job_manager_query_valid(
    globus_gram_jobmanager_request_t *  request)
{
    switch(request->jobmanager_state)
    {
      case GLOBUS_GRAM_JOB_MANAGER_STATE_START:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_COMMITTED:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY1:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_QUERY2:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_IN:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_SUBMIT:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL1:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL2:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY1:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_POLL_QUERY2:
          return GLOBUS_TRUE;
      case GLOBUS_GRAM_JOB_MANAGER_STATE_PRE_CLOSE_OUTPUT:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_CLOSE_OUTPUT:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_STAGE_OUT:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END_COMMITTED:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FILE_CLEAN_UP:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_CACHE_CLEAN_UP:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_SCRATCH_CLEAN_UP:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_DONE:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CLOSE_OUTPUT:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE_COMMITTED:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_FILE_CLEAN_UP:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_CACHE_CLEAN_UP:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_SCRATCH_CLEAN_UP:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_DONE:
      case GLOBUS_GRAM_JOB_MANAGER_STATE_STOP:
          return GLOBUS_FALSE;
    }
    return GLOBUS_FALSE;
}
/* globus_l_gram_job_manager_query_valid() */


static
void
globus_l_delegation_callback(
    void *                              arg,
    globus_gram_protocol_handle_t       handle,
    gss_cred_id_t                       credential,
    int                                 error_code)
{
    globus_l_gram_renew_t *             renew;
    globus_gram_jobmanager_request_t *  request;
    globus_gram_job_manager_t *         manager;
    int                                 renew_code, failure_code, exit_code;

    renew = arg;
    request = renew->request;
    manager = request->manager;

    GlobusGramJobManagerRequestLock(request);
    GlobusGramJobManagerLock(manager);
    (void) globus_gram_job_manager_gsi_update_credential(
            manager,
            request,
            credential);
    GlobusGramJobManagerUnlock(manager);

    renew_code = (credential == GSS_C_NO_CREDENTIAL)
                ? GLOBUS_GRAM_PROTOCOL_ERROR_DELEGATION_FAILED
                : 0;
    failure_code = (credential == GSS_C_NO_CREDENTIAL)
                ? 0
                : request->failure_code,
    exit_code = request->exit_code;

    globus_l_gram_job_manager_query_reply(
            manager,
            request,
            renew->handle,
            request->status,
            renew_code,
            failure_code,
            exit_code);

    GlobusGramJobManagerRequestUnlock(request);

    globus_gram_job_manager_request_log(
            request,
            (renew_code == GLOBUS_SUCCESS)
                ? GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG
                : GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
            "event=gram.proxyrenew.end "
            "level=%s "
            "gramid=%s "
            "status=%d "
            "reason=\"%s\" "
            "\n",
            (renew_code == GLOBUS_SUCCESS) ? "DEBUG" : "ERROR",
            request->job_contact_path,
            -renew_code,
            globus_gram_protocol_error_string(renew_code));

    (void) globus_gram_job_manager_remove_reference(
            request->manager,
            request->job_contact_path,
            "renew");
    free(renew);
}
/* globus_l_delegation_callback() */

static
int
globus_l_gram_create_extensions(
    globus_gram_job_manager_t *         manager,
    globus_gram_jobmanager_request_t *  request,
    int                                 status,
    int                                 exit_code,
    globus_hashtable_t *                extensions)
{
    globus_gram_protocol_extension_t *  entry = NULL;
    int                                 rc;

    *extensions = NULL;
    rc = globus_hashtable_init(
            extensions,
            3,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto hashtable_init_failed;
    }

    if ((manager->config->seg_module != NULL ||
         strcmp(manager->config->jobmanager_type, "condor") == 0) &&
        status == GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE)
    {
        entry = globus_gram_protocol_create_extension(
                "exit-code",
                "%d",
                exit_code);
        if (entry == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto extension_create_failed;
        }
        rc = globus_hashtable_insert(
                extensions,
                entry->attribute,
                entry);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto extension_insert_failed;
        }
    }

    entry = globus_gram_protocol_create_extension(
            "toolkit-version",
            "%s",
            manager->config->globus_version);
    if (entry == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto extension_create_failed;
    }
    rc = globus_hashtable_insert(
            extensions,
            entry->attribute,
            entry);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto extension_insert_failed;
    }

    entry = globus_gram_protocol_create_extension(
            "version",
            "%d.%d (%d-%d)",
            local_version.major,
            local_version.minor,
            local_version.timestamp,
            local_version.branch_id);
    if (entry == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto extension_create_failed;
    }
    rc = globus_hashtable_insert(
            extensions,
            entry->attribute,
            entry);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto extension_insert_failed;
    }

    entry = NULL;
    if (entry)
    {
extension_insert_failed:
        free(entry->value);
        free(entry->attribute);
        free(entry);
    }
extension_create_failed:
    if (rc != GLOBUS_SUCCESS)
    {
        globus_gram_protocol_hash_destroy(extensions);
        *extensions = NULL;
    }
hashtable_init_failed:
    return rc;
}
/* globus_l_gram_create_extensions() */

static
const char *
globus_l_gram_get_job_contact_from_uri(
    const char *                        uri)
{
    int                                 n;
    int                                 rc;
    if (uri[0] == '/')
    {
        return uri;
    }
    else
    {
        rc = sscanf(uri, "https://%*[^:]%*[:0-9]%n", &n);
        if (rc < 0)
        {
            return NULL;
        }
        return uri+n;
    }
}
/* globus_l_gram_get_job_contact_from_uri() */

static
int
globus_l_gram_stdio_update_signal(
    globus_gram_jobmanager_request_t *  request,
    char *                              update_rsl_spec)
{
    globus_rsl_t *                      rsl;
    globus_rsl_t *                      original_rsl;
    globus_rsl_t *                      position;
    globus_rsl_t *                      new_unevaluated_rsl;
    char *                              new_rsl_spec;
    int                                 rc = GLOBUS_SUCCESS;


    rsl = globus_rsl_parse(update_rsl_spec);

    if(!rsl)
    {
        char * tmp_str;

        tmp_str = globus_gram_prepare_log_string(update_rsl_spec);

        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
            "event=gram.state_machine.info "
            "level=ERROR "
            "gramid=%s "
            "query_type=%s "
            "rsl=\"%s\" "
            "msg=%s "
            "status=%d "
            "reason=\"%s\" "
            "\n",
            request->job_contact_path,
            "stdio_update",
            tmp_str ? tmp_str : "",
            "Error parsing query rsl",
            -rc,
            globus_gram_protocol_error_string(rc));

        if (tmp_str)
        {
            free(tmp_str);
        }
        goto error_out;
    }
    rc = globus_rsl_assist_attributes_canonicalize(rsl);
    if(rc != GLOBUS_SUCCESS)
    {
        char * tmp_str;

        tmp_str = globus_gram_prepare_log_string(update_rsl_spec);

        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
            "event=gram.state_machine.end"
            "level=ERROR "
            "query_type=%s "
            "gramid=%s "
            "rsl=\"%s\" "
            "msg=\"%s\" "
            "status=%d "
            "reason=\"%s\" "
            "\n",
            "stdio_update",
            request->job_contact_path,
            tmp_str ? tmp_str : "",
            "Error canonicalizing RSL",
            -rc,
            globus_gram_protocol_error_string(rc));

        if (tmp_str)
        {
            free(tmp_str);
        }

        goto free_rsl_out;
    }

    rc = globus_gram_job_manager_validate_rsl(
            request,
            rsl,
            GLOBUS_GRAM_VALIDATE_STDIO_UPDATE);
    if(rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        goto free_rsl_out;
    }
    /* Remove unsupported stdout and stderr position attributes */
    position = globus_gram_job_manager_rsl_extract_relation(
            rsl,
            GLOBUS_GRAM_PROTOCOL_STDOUT_POSITION_PARAM);
    if (position != NULL)
    {
        globus_rsl_free_recursive(position);
    }
    position = globus_gram_job_manager_rsl_extract_relation(
            rsl,
            GLOBUS_GRAM_PROTOCOL_STDERR_POSITION_PARAM);
    if (position != NULL)
    {
        globus_rsl_free_recursive(position);
    }

    /* Replace the string RSL representation for this job with the
     * merge of the original and this update. The new RSL unparsed
     * into a string and stored in request->rsl_spec
     */
    original_rsl = globus_rsl_parse(request->rsl_spec);
    if (original_rsl == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        goto free_rsl_out;
    }
    new_unevaluated_rsl = globus_gram_job_manager_rsl_merge(
            original_rsl,
            rsl);
    if (new_unevaluated_rsl == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        goto free_original_rsl_out;
    }
    new_rsl_spec = globus_rsl_unparse(new_unevaluated_rsl);
    if (new_rsl_spec == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        goto free_new_unevaluated_rsl_out;
    }
    free(request->rsl_spec);
    request->rsl_spec = new_rsl_spec;
    new_rsl_spec = NULL;

    rc = globus_rsl_eval(rsl, &request->symbol_table);
    if(rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_EVALUATION_FAILED;

        goto free_new_unevaluated_rsl_out;
    }

    rc = globus_i_gram_request_stdio_update(
            request,
            rsl);
free_new_unevaluated_rsl_out:
    globus_rsl_free_recursive(new_unevaluated_rsl);
free_original_rsl_out:
    globus_rsl_free_recursive(original_rsl);
free_rsl_out:
    globus_rsl_free_recursive(rsl);
error_out:
    return rc;
}
/* globus_l_gram_stdio_update_signal() */
