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
 * @file globus_gram_job_manager_request.c
 * @brief Globus Job Management Request
 */

/*
 * Include header files
 */
#include "globus_common.h"
#include "globus_gram_protocol.h"
#include "globus_gram_job_manager.h"
#include "globus_rsl_assist.h"
#include "globus_scheduler_event_generator_app.h"

#include <string.h>
#include <syslog.h>
#include <unistd.h>

enum
{
    GRAM_JOB_MANAGER_COMMIT_TIMEOUT=60
};

static
int
globus_l_gram_symboltable_add(
    globus_symboltable_t *              symbol_table,
    const char *                        symbol,
    const char *                        value);

static
int
globus_l_gram_generate_id(
    globus_gram_jobmanager_request_t *  request,
    char **                             jm_restart,
    uint64_t *                          uniq1p,
    uint64_t *                          uniq2p);

static
int
globus_l_gram_init_cache(
    globus_gram_jobmanager_request_t *  request,
    char **                             cache_locationp,
    globus_gass_cache_t  *              cache_handlep);

static
int
globus_l_gram_restart(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_jobmanager_request_t ** old_job_request);

static
int
globus_l_gram_populate_environment(
    globus_gram_jobmanager_request_t *  request);

static
int
globus_l_gram_add_environment(
    globus_rsl_t *                      rsl,
    const char *                        variable,
    const char *                        value);

static
int
globus_l_gram_init_scratchdir(
    globus_gram_jobmanager_request_t *  request,
    globus_rsl_t *                      rsl,
    const char *                        scratch_dir_base,
    char **                             scratchdir);

static
int
globus_l_gram_validate_rsl(
    globus_gram_jobmanager_request_t *  request);

static
int
globus_l_gram_remote_io_url_file_create(
    globus_gram_jobmanager_request_t *  request,
    const char *                        remote_io_url,
    const char *                        job_dir,
    char **                             remote_io_url_filep);

static
int
globus_l_gram_export_cred(
    globus_gram_jobmanager_request_t *  request,
    gss_cred_id_t                       cred,
    const char *                        job_directory,
    char **                             proxy_filename);

static
int
globus_l_gram_make_job_dir(
    globus_gram_jobmanager_request_t *  request,
    char **                             job_directory);

static
void
globus_l_gram_event_destroy(void *datum);

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

/**
 * Allocate and initialize a request.
 *
 * This function allocates a new request structure and clears all of the
 * values in the structure. It also creates a script argument file which
 * will be used when the job request is submitted.
 *
 * @param request
 *     A pointer to a globus_gram_jobmanager_request_t pointer. This
 *     will be modified to point to a freshly allocated request structure.
 * @param manager
 *     Job manager state and configuration.
 * @param rsl
 *     Job request or restart RSL string
 * @param delegated_credential
 *     Credential delegated with the job request.
 * @param response_ctx
 *     Security context to use for sending the job request response, may be
 *     GSS_C_NO_CONTEXT if the job RSL came from the command-line.
 * @param reinit
 *     Boolean value indicating whether this is an internally-generated
 *     reinitialization of an existing job or a new job request from a 
 *     client or command-line.
 * @param old_job_contact
 *     Pointer to a string to be set to the old job contact if
 *     GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE. Set to NULL otherwise.
 * @param old_job_request
 *     Pointer to a job request structure that will be set to an existing
 *     one if the return value is GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE. Set
 *     to NULL otherwise. If non-null, the caller must release a reference
 *     when done processing this.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL
 *     Bad RSL
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_RSL_EVALUATION_FAILED
 *     RSL evaluation failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_RSL_SCRATCH
 *     Invalid scratchdir RSL attribute
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRATCH
 *     Invalid scratchdir path
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PARAMETER_NOT_SUPPORTED
 *     RSL attribute not supported.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SUBMIT_ATTRIBUTE
 *     Invalid submit RSL attribute.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_RESTART_ATTRIBUTE
 *     Invalid restart RSL attribute.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_STDIO_UPDATE_ATTRIBUTE
 *     Invalid stdio_update RSL attribute.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE
 *     Old Job Manager is still alive.
 */
int 
globus_gram_job_manager_request_init(
    globus_gram_jobmanager_request_t ** request,
    globus_gram_job_manager_t *         manager,
    char *                              rsl,
    gss_cred_id_t                       delegated_credential,
    gss_ctx_id_t                        response_ctx,
    const char *                        peer_address,
    globus_gsi_cred_handle_t            peer_cred,
    globus_bool_t                       reinit,
    char **                             old_job_contact,
    globus_gram_jobmanager_request_t ** old_job_request,
    char **                             gt3_failure_message)
{
    globus_gram_jobmanager_request_t *  r;
    uint64_t                            uniq1, uniq2;
    int                                 rc;
    const char *                        tmp_string;
    int                                 count;
    int                                 proxy_timeout;

    if (old_job_contact)
    {
        *old_job_contact = NULL;
    }
    if (old_job_request)
    {
        *old_job_request = NULL;
    }
    r = calloc(1, sizeof(globus_gram_jobmanager_request_t));
    if (r == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto request_malloc_failed;
    }
    r->job_log_level = -1;
    r->config = manager->config;
    r->manager = manager;

    GlobusTimeAbstimeGetCurrent(r->job_stats.unsubmitted_timestamp);
    r->status = GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED;
    r->status_update_time = 0;
    r->failure_code = 0;
    r->exit_code = 0;
    r->stop_reason = 0;
    /* Won't be set until job has been submitted to the LRM */
    r->job_id_string = NULL;
    r->original_job_id_string = NULL;
    r->poll_frequency = 10;
    r->commit_extend = 0;
    r->scratchdir = NULL;
    r->creation_time = time(NULL);
    r->queued_time = time(NULL);
    r->cache_tag = NULL;
    r->gateway_user = NULL;
    r->expected_terminal_state = GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE;
    r->gt3_failure_type = NULL;
    r->gt3_failure_message = NULL;
    r->gt3_failure_source = NULL;
    r->gt3_failure_destination = NULL;
    r->seg_last_timestamp = 0;

    rc = globus_symboltable_init(
            &r->symbol_table,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto symboltable_init_failed;
    }
    rc = globus_symboltable_create_scope(&r->symbol_table);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto symboltable_create_scope_failed;
    }
    rc = globus_i_gram_symbol_table_populate(
            r->config,
            &r->symbol_table);
    if (rc != GLOBUS_SUCCESS)
    {
        goto symboltable_populate_failed;
    }

    r->rsl = globus_rsl_parse(rsl);
    if (r->rsl == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        goto rsl_parse_failed;
    }

    rc = globus_rsl_assist_attributes_canonicalize(r->rsl);
    if(rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        goto rsl_canonicalize_failed;
    }
    r->rsl_spec = globus_rsl_unparse(r->rsl);
    if (r->rsl_spec == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto rsl_unparse_failed;
    }

    /* If this is a restart job, the id will come from the restart RSL
     * value; otherwise, it will be generated from current pid and time
     */
    rc = globus_l_gram_generate_id(
            r,
            &r->jm_restart,
            &uniq1,
            &uniq2);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_generate_id;
    }

    /* Unique ID is used to have a handle to a job that has its state saved
     * and then the job is later restarted
     */
    r->uniq_id = globus_common_create_string(
            "%"PRIu64".%"PRIu64,
            uniq1,
            uniq2);
    if (r->uniq_id == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto failed_set_uniq_id;
    }
    /* The job contact is how the client is able to send signals or cancel this 
     * job.
     */
    r->job_contact = globus_common_create_string(
            "%s%"PRIu64"/%"PRIu64"/",
            r->manager->url_base,
            uniq1,
            uniq2);

    if (r->job_contact == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto failed_set_job_contact;
    }
    rc = globus_l_gram_symboltable_add(
            &r->symbol_table,
            "GLOBUS_GRAM_JOB_CONTACT",
            r->job_contact);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_add_contact_to_symboltable;
    }

    rc = globus_l_gram_symboltable_add(
            &r->symbol_table,
            "GLOBUS_GRAM_JOB_ID",
            r->uniq_id);

    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_add_contact_to_symboltable;
    }
    r->job_contact_path = globus_common_create_string(
            "/%"PRIu64"/%"PRIu64"/",
            uniq1,
            uniq2);
    if (r->job_contact_path == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto failed_set_job_contact_path;
    }

    rc = globus_l_gram_make_job_dir(r, &r->job_dir);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_make_job_dir;
    }

    r->cached_stdout = globus_common_create_string(
            "%s/%s",
            r->job_dir,
            "stdout");
    if (r->cached_stdout == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto cached_stdout_malloc_failed;
    }
    rc = globus_symboltable_insert(
            &r->symbol_table,
            "GLOBUS_CACHED_STDOUT",
            r->cached_stdout);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto cached_stdout_symboltable_failed;
    }
    r->cached_stderr = globus_common_create_string(
            "%s/%s",
            r->job_dir,
            "stderr");
    if (r->cached_stderr == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto cached_stderr_malloc_failed;
    }

    rc = globus_symboltable_insert(
            &r->symbol_table,
            "GLOBUS_CACHED_STDERR",
            r->cached_stderr);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto cached_stderr_symboltable_failed;
    }


    rc = globus_gram_job_manager_state_file_set(
        r,
        &r->job_state_file);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_state_file_set;
    }

    r->client_contacts = NULL;

    r->stage_in_todo = NULL;
    r->stage_in_shared_todo = NULL;
    r->stage_out_todo = NULL;
    r->stage_stream_todo = NULL;

    if (r->jm_restart)
    {
        rc = globus_l_gram_restart(r, old_job_request);

        if (rc != GLOBUS_SUCCESS)
        {
            goto failed_restart;
        }
        
        manager->usagetracker->count_restarted++;
    }
    else
    {
        r->cache_tag = strdup(r->job_contact);
        if (r->cache_tag == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            goto cache_tag_alloc_failed;
        }
        rc = globus_i_gram_get_tg_gateway_user(
                response_ctx,
                peer_cred,
                &r->gateway_user);
        if(rc != GLOBUS_SUCCESS)
        {
            goto get_gateway_user_failed;
        }
    }

    rc = globus_gram_job_manager_rsl_eval_string(
            &r->symbol_table,
            r->config->scratch_dir_base,
            &r->scratch_dir_base);
    if(rc != GLOBUS_SUCCESS)
    {
        goto failed_eval_scratch_dir_base;
    }

    rc = globus_l_gram_init_scratchdir(
            r,
            r->rsl,
            r->scratch_dir_base,
            &r->scratchdir);
    if(rc != GLOBUS_SUCCESS)
    {
        goto init_scratchdir_failed;
    }

    rc = globus_l_gram_init_cache(
            r,
            &r->cache_location,
            &r->cache_handle);
    if (rc != GLOBUS_SUCCESS)
    {
        goto init_cache_failed;
    }

    /* At this point, all of the RSL substitutions have been populated,
     * including those based on runtime values, so we can validate the RSL
     */
    rc = globus_l_gram_validate_rsl(r);
    if(rc != GLOBUS_SUCCESS)
    {
        goto validate_rsl_failed;
    }
    rc = globus_gram_job_manager_rsl_attribute_get_int_value(
            r->rsl,
            GLOBUS_GRAM_PROTOCOL_COUNT_PARAM,
            &count);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_COUNT;
        goto invalid_count;
    }

    rc = globus_gram_job_manager_rsl_attribute_get_boolean_value(
            r->rsl,
            GLOBUS_GRAM_PROTOCOL_DRY_RUN_PARAM,
            &r->dry_run);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_DRYRUN;
        goto get_dry_run_failed;
    }

    /* Some clients send (two_phase_commit = yes), others send
     * (two_phase_commit = timeout)
     */
    rc = globus_gram_job_manager_rsl_attribute_get_int_value(
            r->rsl,
            GLOBUS_GRAM_PROTOCOL_TWO_PHASE_COMMIT_PARAM,
            &r->two_phase_commit);
    if (rc == GLOBUS_GRAM_PROTOCOL_ERROR_UNDEFINED_ATTRIBUTE)
    {
        r->two_phase_commit = 0;
    }
    else if (rc != GLOBUS_SUCCESS)
    {
        globus_bool_t tmp_bool;

        rc = globus_gram_job_manager_rsl_attribute_get_boolean_value(
                r->rsl,
                GLOBUS_GRAM_PROTOCOL_TWO_PHASE_COMMIT_PARAM,
                &tmp_bool);

        if (rc == GLOBUS_SUCCESS)
        {
            r->two_phase_commit  =
                    tmp_bool ? GRAM_JOB_MANAGER_COMMIT_TIMEOUT : 0;
        }
        else
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_TWO_PHASE_COMMIT;
            goto get_two_phase_commit_failed;
        }

        globus_gram_job_manager_rsl_remove_attribute(
                r->rsl,
                GLOBUS_GRAM_PROTOCOL_TWO_PHASE_COMMIT_PARAM);
    }

    rc = globus_gram_job_manager_rsl_attribute_get_string_value(
            r->rsl,
            GLOBUS_GRAM_PROTOCOL_REMOTE_IO_URL_PARAM,
            &tmp_string);
        
    switch (rc)
    {
    case GLOBUS_GRAM_PROTOCOL_ERROR_UNDEFINED_ATTRIBUTE:
        r->remote_io_url = NULL;
        r->remote_io_url_file = NULL;
        break;
    case GLOBUS_SUCCESS:
        if (tmp_string != NULL)
        {
            r->remote_io_url = strdup(tmp_string);
            if (r->remote_io_url == NULL)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
                goto get_remote_io_url_failed;
            }
        }
        else
        {
    default:
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_REMOTE_IO_URL;
            goto get_remote_io_url_failed;
        }
    }
    rc = globus_l_gram_remote_io_url_file_create(
            r,
            r->remote_io_url,
            r->job_dir,
            &r->remote_io_url_file);

    if (rc != GLOBUS_SUCCESS)
    {
        goto make_remote_io_url_file_failed;
    }

    /* TODO: Check that stdout and stderr, if a local files, can be written
     * to
     */

    rc = globus_gram_job_manager_rsl_attribute_get_int_value(
            r->rsl,
            GLOBUS_GRAM_PROTOCOL_PROXY_TIMEOUT_PARAM,
            &proxy_timeout);

    if (rc == GLOBUS_GRAM_PROTOCOL_ERROR_UNDEFINED_ATTRIBUTE)
    {
        rc = GLOBUS_SUCCESS;
    }
    else if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_PROXY_TIMEOUT;
        goto bad_proxy_timeout;
    }

    rc = globus_gram_job_manager_rsl_attribute_get_string_value(
        r->rsl,
        "loglevel",
        &tmp_string);
    switch (rc)
    {
    case GLOBUS_GRAM_PROTOCOL_ERROR_UNDEFINED_ATTRIBUTE:
        r->job_log_level = r->config->log_levels;
        break;

    case GLOBUS_SUCCESS:
        if (tmp_string != NULL)
        {
            rc = globus_i_gram_parse_log_levels(
                    tmp_string,
                    &r->job_log_level,
                    &r->gt3_failure_message);

            if (rc != GLOBUS_SUCCESS)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR;
                goto get_job_log_levels_failed;
            }
        }
        else
        {
    default:
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR;
            goto get_job_log_levels_failed;
        }
    }

    rc = globus_gram_job_manager_rsl_attribute_get_string_value(
        r->rsl,
        "logpattern",
        &tmp_string);
    switch (rc)
    {
    case GLOBUS_GRAM_PROTOCOL_ERROR_UNDEFINED_ATTRIBUTE:
        r->log_pattern = NULL;
        break;

    case GLOBUS_SUCCESS:
        if (tmp_string != NULL)
        {
            r->log_pattern = strdup(tmp_string);
        }
        else
        {
    default:
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_ATTR;
            goto get_job_log_pattern_failed;
        }
    }

    rc = globus_mutex_init(&r->mutex, NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;
        goto mutex_init_failed;
    }
    rc = globus_cond_init(&r->cond, NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;
        goto cond_init_failed;
    }
    rc = globus_gram_job_manager_staging_create_list(r);
    if(rc != GLOBUS_SUCCESS)
    {
        goto staging_list_create_failed;
    }

    if (reinit && r->jm_restart)
    {
        r->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_COMMITTED;
    }
    else
    {
        r->jobmanager_state = GLOBUS_GRAM_JOB_MANAGER_STATE_START;
    }

    if (r->jm_restart == NULL)
    {
        r->restart_state = GLOBUS_GRAM_JOB_MANAGER_STATE_START;
    }

    r->unsent_status_change = GLOBUS_FALSE;
    r->poll_timer = GLOBUS_NULL_HANDLE;
    rc = globus_fifo_init(&r->pending_queries);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;

        goto pending_queries_init_failed;
    }
    rc = globus_l_gram_export_cred(
            r,
            delegated_credential,
            r->job_dir,
            &r->x509_user_proxy);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_export_cred;
    }

    rc = globus_l_gram_populate_environment(r);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failed_populate_environment;
    }
    r->streaming_requested = GLOBUS_FALSE;

    rc = globus_gram_job_manager_history_file_set(r);
    if (rc != GLOBUS_SUCCESS)
    {
        goto history_file_set_failed;
    }
    r->job_history_status = GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED;

    r->response_context = response_ctx;

    rc = globus_fifo_init(&r->seg_event_queue);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto seg_event_queue_init_failed;
    }

    if (r->job_stats.client_address == NULL && peer_address != NULL)
    {
        r->job_stats.client_address = strdup(peer_address);
        if (r->job_stats.client_address == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto client_addr_strdup_failed;
        }
    }
    if (r->job_stats.user_dn == NULL && peer_cred != NULL)
    {
        globus_result_t id_result;
        char * id = NULL;

        id_result = globus_gsi_cred_get_identity_name(
                peer_cred,
                &id);

        if (id_result == GLOBUS_SUCCESS && id != NULL)
        {
            r->job_stats.user_dn = strdup(id);
            OPENSSL_free(id);
        }
    }
    if (r->jm_restart == NULL)
    {
        rc = globus_gram_job_manager_state_file_write(r);
    }
    
    if (rc != GLOBUS_SUCCESS)
    {
client_addr_strdup_failed:
        globus_fifo_destroy(&r->seg_event_queue);
seg_event_queue_init_failed:
        if (r->job_history_file)
        {
            free(r->job_history_file);
            r->job_history_file = NULL;
        }
history_file_set_failed:
failed_populate_environment:
        if (r->x509_user_proxy)
        {
            free(r->x509_user_proxy);
            r->x509_user_proxy = NULL;
        }
failed_export_cred:
pending_queries_init_failed:
staging_list_create_failed:
        globus_cond_destroy(&r->cond);
cond_init_failed:
        globus_mutex_destroy(&r->mutex);
mutex_init_failed:
        if (r->log_pattern)
        {
            free(r->log_pattern);
        }
get_job_log_pattern_failed:
get_job_log_levels_failed:
bad_proxy_timeout:
        if (r->remote_io_url_file)
        {
            remove(r->remote_io_url_file);
            free(r->remote_io_url_file);
        }
make_remote_io_url_file_failed:
        if (r->remote_io_url)
        {
            free(r->remote_io_url);
        }
get_remote_io_url_failed:
get_two_phase_commit_failed:
get_dry_run_failed:
invalid_count:
validate_rsl_failed:
        globus_gass_cache_close(&r->cache_handle);
        free(r->cache_location);
init_cache_failed:
        if (r->scratchdir)
        {
            globus_gram_job_manager_destroy_directory(r, r->scratchdir);
            free(r->scratchdir);
            r->scratchdir = NULL;
        }
init_scratchdir_failed:
        if (r->scratch_dir_base)
        {
            free(r->scratch_dir_base);
        }
failed_eval_scratch_dir_base:
        if (r->gateway_user)
        {
            free(r->gateway_user);
        }
get_gateway_user_failed:
        free(r->cache_tag);
cache_tag_alloc_failed:
failed_restart:
        free(r->job_state_file);
failed_state_file_set:
cached_stderr_symboltable_failed:
        free(r->cached_stderr);
cached_stderr_malloc_failed:
cached_stdout_symboltable_failed:
        free(r->cached_stdout);
cached_stdout_malloc_failed:
        if (rc != GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE)
        {
            globus_gram_job_manager_destroy_directory(r, r->job_dir);
        }
        free(r->job_dir);
failed_make_job_dir:
        free(r->job_contact_path);
failed_set_job_contact_path:
failed_add_contact_to_symboltable:
        if (r->job_contact)
        {
            free(r->job_contact);
        }
failed_set_job_contact:
        free(r->uniq_id);
failed_set_uniq_id:
        if (r->jm_restart)
        {
            free(r->jm_restart);
        }
failed_generate_id:
        free(r->rsl_spec);
rsl_unparse_failed:
rsl_canonicalize_failed:
        globus_rsl_free_recursive(r->rsl);
rsl_parse_failed:
symboltable_populate_failed:
symboltable_create_scope_failed:
        globus_symboltable_destroy(&r->symbol_table);
        if (r->gt3_failure_message)
        {
            if (gt3_failure_message != NULL)
            {
                *gt3_failure_message = r->gt3_failure_message;
            }
            else
            {
                free(r->gt3_failure_message);
            }
        }
        if (r->gt3_failure_type)
        {
            free(r->gt3_failure_type);
        }
        if (r->gt3_failure_source)
        {
            free(r->gt3_failure_source);
        }
        if (r->gt3_failure_destination)
        {
            free(r->gt3_failure_destination);
        }
symboltable_init_failed:
        free(r);
        r = NULL;
    }
request_malloc_failed:
    *request = r;

    return rc;
}
/* globus_gram_job_manager_request_init() */

/**
 * Load request and security context, initialize structures, and send reply.
 *
 * @param manager
 *     Job manager state
 * @param http_body_fd
 *     File descriptor of the HTTP body file
 * @param context_fd
 *     File descriptor pointing to the GSSAPI security context to use for the
 *     response.
 * @param request
 *     Pointer to be set to the new job request
 * @param context
 *     Pointer to be set to the security context of the initial job request
 * @param contact
 *     Pointer to be set to the job contact of the client to send job state
 *     change notifications to.
 * @param job_state_mask
 *     Pointer to be set to the job state mask for which job state changes
 *     the client is interested in.
 * @param old_job_contact
 *     Pointer to a string to be set to the old job contact if
 *     GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE. Set to NULL otherwise.
 * @param old_job_request
 *     Pointer to a job request structure that will be set to an existing
 *     one if the return value is GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE. Set
 *     to NULL otherwise. If non-null, the caller must release a reference
 *     when done processing this.
 * @param gt3_failure_message
 *     Pointer to be set to an extended failure message to explain why the
 *     initialization failed.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_GATEKEEPER_MISCONFIGURED
 *     Unable to import security context
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Unable to read job request from http_body_fd
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL
 *     Bad RSL
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_RSL_EVALUATION_FAILED
 *     RSL evaluation failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_RSL_SCRATCH
 *     Invalid scratchdir RSL attribute
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRATCH
 *     Invalid scratchdir path
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PARAMETER_NOT_SUPPORTED
 *     RSL attribute not supported.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SUBMIT_ATTRIBUTE
 *     Invalid submit RSL attribute.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_RESTART_ATTRIBUTE
 *     Invalid restart RSL attribute.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_STDIO_UPDATE_ATTRIBUTE
 *     Invalid stdio_update RSL attribute.
 *
 * @note Even if this function returns a failure code, the @a context 
 * may still be initialized to the security context. If it points to a
 * value other than GSS_C_NO_CONTEXT then it can be used by the caller
 * to send the failure response.
 */
int
globus_gram_job_manager_request_load(
    globus_gram_job_manager_t *         manager,
    int                                 http_body_fd,
    int                                 context_fd,
    gss_cred_id_t                       cred,
    const char *                        peer_address,
    globus_gsi_cred_handle_t            peer_cred,
    size_t                              content_length,
    globus_gram_jobmanager_request_t ** request,
    gss_ctx_id_t *                      context,
    char **                             contact,
    int *                               job_state_mask,
    char **                             old_job_contact,
    globus_gram_jobmanager_request_t ** old_job_request,
    globus_bool_t *                     version_only,
    char **                             gt3_failure_message)
{
    int                                 rc;
    char *                              rsl;

    *request = NULL;
    *context = GSS_C_NO_CONTEXT;
    *contact = NULL;
    *job_state_mask = 0;
    *version_only = GLOBUS_FALSE;

    if (context_fd != -1)
    {
        rc = globus_gram_job_manager_import_sec_context(
                manager,
                context_fd,
                context);
        if (rc != GLOBUS_SUCCESS)
        {
            goto import_context_failed;
        }
    }

    rc = globus_gram_job_manager_read_request(
            manager,
            http_body_fd,
            content_length,
            &rsl,
            contact,
            job_state_mask,
            version_only);
    if (rc != GLOBUS_SUCCESS)
    {
        goto read_request_failed;
    }
    if (! (*version_only))
    {
        rc = globus_gram_job_manager_request_init(
                request,
                manager,
                rsl,
                cred,
                *context,
                peer_address,
                peer_cred,
                GLOBUS_FALSE,
                old_job_contact,
                old_job_request,
                gt3_failure_message);
    }
    if (rc != GLOBUS_SUCCESS)
    {
        goto request_init_failed;
    }
request_init_failed:
    if (rsl)
    {
        free(rsl);
    }
read_request_failed:
import_context_failed:
    return rc;
}
/* globus_gram_job_manager_request_load() */

/** Verify that the job request is authorized, send a reply, and start the state machine for this request
 *
 * @param manager
 *     Job manager state
 * @param request
 *     Job request to start
 * @param response_fd
 *     Descriptor to write the response to
 * @param client_contact
 *     Client to send job state changes to (may be NULL)
 * @param job_state_mask
 *    Job state mask for sending state changes to the client
 */
int
globus_gram_job_manager_request_start(
    globus_gram_job_manager_t *         manager,
    globus_gram_jobmanager_request_t *  request,
    int                                 response_fd,
    const char *                        client_contact,
    int                                 job_state_mask)
{
    int                                 rc;
    int                                 rc2;
    int                                 response_code;
    char *                              job_contact;

    if (request == NULL)
    {
        /* Reply to a bad request */
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST;
        goto bad_request;
    }
    rc = globus_gram_job_manager_add_request(
        manager,
        request->job_contact_path,
        request);

    GlobusGramJobManagerRequestLock(request);
    if (rc != GLOBUS_SUCCESS)
    {
        goto add_request_failed;
    }

    rc = globus_gram_job_manager_call_authz_callout(
            request->config,
            request->response_context,
            request->response_context,
            request->uniq_id,
            request->rsl,
            "start");
    if (rc != GLOBUS_SUCCESS)
    {
        goto authz_denied;
    }

    rc = globus_gram_job_manager_validate_username(request);
    if (rc != 0)
    {
        goto username_denied;
    }

    if (client_contact != NULL)
    {
        rc = globus_gram_job_manager_contact_add(
                request,
                client_contact,
                job_state_mask);
        if (rc != GLOBUS_SUCCESS)
        {
            goto contact_add_failed;
        }
    }

    if (request->dry_run)
    {
        request->failure_code = GLOBUS_GRAM_PROTOCOL_ERROR_DRYRUN;
        rc = globus_gram_job_manager_request_set_status(
                request,
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED);
                
        manager->usagetracker->count_dryrun++;
    }
contact_add_failed:
username_denied:
authz_denied:
add_request_failed:
bad_request:
    /* Reply to request with unsubmitted (and optionally
     * two-phase-commit needed)
     */
    switch (request->failure_code)
    {
    case GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE:
        response_code = request->failure_code;
        job_contact = request->old_job_contact;
        break;

    case GLOBUS_SUCCESS:
        if (request->two_phase_commit)
        {
            response_code = GLOBUS_GRAM_PROTOCOL_ERROR_WAITING_FOR_COMMIT;
        }
        else
        {
            response_code = GLOBUS_SUCCESS;
        }
        job_contact = request->job_contact;
        break;

    default:
        response_code = request->failure_code;
        job_contact = NULL;
        break;
    }

    rc2 = globus_gram_job_manager_reply(
            request,
            request->manager,
            response_code,
            job_contact,
            response_fd,
            request->response_context,
            NULL);
    if (rc == GLOBUS_SUCCESS && rc2 == GLOBUS_SUCCESS)
    {
        globus_reltime_t                delay;

        GlobusTimeReltimeSet(delay, 0, 0);

        rc = globus_gram_job_manager_state_machine_register(
                manager,
                request,
                &delay);
    }
    else if (rc == GLOBUS_SUCCESS && rc2 != GLOBUS_SUCCESS)
    {
        rc = rc2;
    }
    GlobusGramJobManagerRequestUnlock(request);
    return rc;
}
/* globus_gram_job_manager_request_start() */

/**
 * Deallocate memory related to a request.
 *
 * This function frees the data within the request, also destroying
 * files associated with the request.
 * The caller must not access the request after this function has returned.
 *
 * @param request
 *        Job request to destroy.
 *
 * @return GLOBUS_SUCCESS
 */
void
globus_gram_job_manager_request_destroy(
    globus_gram_jobmanager_request_t *  request)
{
    if (!request)
    {
        return;
    }
    if (request->scratchdir)
    {
        globus_gram_job_manager_destroy_directory(
                request,
                request->scratchdir);
    }
    globus_gram_job_manager_request_free(request);
}
/* globus_gram_job_manager_request_destroy() */

/**
 * Deallocate memory related to a request.
 *
 * This function frees the data within the request.
 *
 * @param request
 *        Job request to destroy.
 *
 * @return GLOBUS_SUCCESS
 */
void
globus_gram_job_manager_request_free(
    globus_gram_jobmanager_request_t *  request)
{
    if (!request)
    {
        return;
    }
    if (request->job_id_string)
    {
        free(request->job_id_string);
    }
    if (request->original_job_id_string)
    {
        free(request->original_job_id_string);
    }
    if (request->uniq_id)
    {
        free(request->uniq_id);
    }
    if (request->cached_stdout)
    {
        free(request->cached_stdout);
    }
    if (request->cached_stderr)
    {
        free(request->cached_stderr);
    }
    if (request->jm_restart)
    {
        free(request->jm_restart);
    }
    if (request->scratch_dir_base)
    {
        free(request->scratch_dir_base);
    }
    if (request->scratchdir)
    {
        free(request->scratchdir);
    }
    if (request->cache_tag)
    {
        free(request->cache_tag);
    }
    if (request->symbol_table != NULL)
    {
        globus_symboltable_destroy(&request->symbol_table);
    }
    if (request->rsl_spec)
    {
        free(request->rsl_spec);
    }
    if (request->rsl)
    {
        globus_rsl_free_recursive(request->rsl);
    }
    if (request->remote_io_url)
    {
        free(request->remote_io_url);
    }
    if (request->remote_io_url_file)
    {
        free(request->remote_io_url_file);
    }
    if (request->x509_user_proxy)
    {
        free(request->x509_user_proxy);
    }
    if (request->job_state_file)
    {
        free(request->job_state_file);
    }
    if (request->gt3_failure_type)
    {
        free(request->gt3_failure_type);
    }
    if (request->gt3_failure_message)
    {
        free(request->gt3_failure_message);
    }
    if (request->gt3_failure_source)
    {
        free(request->gt3_failure_source);
    }
    if (request->gt3_failure_destination)
    {
        free(request->gt3_failure_destination);
    }
    globus_mutex_destroy(&request->mutex);
    globus_cond_destroy(&request->cond);
    globus_gram_job_manager_contact_list_free(request);
    globus_gram_job_manager_staging_free_all(request);
    globus_assert(request->poll_timer == GLOBUS_NULL_HANDLE);
    if (request->job_contact)
    {
        free(request->job_contact);
    }
    if (request->job_contact_path)
    {
        free(request->job_contact_path);
    }
    if (request->pending_queries)
    {
        globus_fifo_destroy(&request->pending_queries);
    }
    if (request->job_history_file)
    {
        free(request->job_history_file);
    }
    if (request->job_dir)
    {
        free(request->job_dir);
    }
    if (request->cache_location)
    {
        free(request->cache_location);
    }
    if (request->gateway_user)
    {
        free(request->gateway_user);
    }
    if (request->cache_handle)
    {
        globus_gass_cache_close(&request->cache_handle);
    }
    if (request->response_context != GSS_C_NO_CONTEXT)
    {
        OM_uint32 minor_status;
        gss_delete_sec_context(&minor_status, &request->response_context, NULL);
    }
    if (request->seg_event_queue)
    {
        globus_fifo_destroy_all(
                &request->seg_event_queue,
                globus_l_gram_event_destroy);
    }
    if (request->job_stats.client_address != NULL)
    {
        free(request->job_stats.client_address);
    }
    if (request->job_stats.user_dn != NULL)
    {
        free(request->job_stats.user_dn);
    }
}
/* globus_gram_job_manager_request_free() */

/**
 * Change the status associated with a job request
 *
 * Changes the status associated with a job request.
 * There is now additional tracking data associated with the
 * status that must be updated when the status is.  This function
 * handles managing it.  It is NOT recommended that you directly
 * change the status.
 *
 * @param request
 *        Job request to change status of.
 * @param status
 *        Status to set the job request to.
 *
 * @return GLOBUS_SUCCESS assuming valid input.
 *         If the request is null, returns GLOBUS_FAILURE.
 */
int
globus_gram_job_manager_request_set_status(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_job_state_t    status)
{
    switch (status)
    {
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING:
            GlobusTimeAbstimeGetCurrent(request->job_stats.pending_timestamp);
            break;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE:
            GlobusTimeAbstimeGetCurrent(request->job_stats.active_timestamp);
            break;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED:
            GlobusTimeAbstimeGetCurrent(request->job_stats.failed_timestamp);
            break;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE:
            GlobusTimeAbstimeGetCurrent(request->job_stats.done_timestamp);
            break;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED:
            GlobusTimeAbstimeGetCurrent(request->job_stats.unsubmitted_timestamp);
            break;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_IN:
            GlobusTimeAbstimeGetCurrent(request->job_stats.file_stage_in_timestamp);
            break;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_OUT:
            GlobusTimeAbstimeGetCurrent(request->job_stats.file_stage_out_timestamp);
            break;
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_SUSPENDED:
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_ALL:
            break;
    }
    return globus_gram_job_manager_request_set_status_time(
            request,
            status,
            time(0));
}
/* globus_gram_job_manager_request_set_status() */


/**
 * Change the status associated with a job request
 *
 * Changes the status associated with a job request.
 * There is now additional tracking data associated with the
 * status that must be updated when the status is.  This function
 * handles managing it.  It is NOT recommended that you directly
 * change the status.
 *
 * @param request
 *        Job request to change status of.
 * @param status
 *        Status to set the job request to.
 * @param valid_time
 *        The status is known good as of this time (seconds since epoch)
 *
 * @return GLOBUS_SUCCESS assuming valid input.
 *         If the request is null, returns GLOBUS_FAILURE.
 */
int
globus_gram_job_manager_request_set_status_time(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_protocol_job_state_t    status,
    time_t valid_time)
{
    if( ! request )
        return GLOBUS_FAILURE;

    request->status = status;
    request->status_update_time = valid_time;

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.job.info "
            "level=DEBUG "
            "gramid=%s "
            "job_status=%d "
            "\n",
            request->job_contact_path,
            status);

    if (request->manager != NULL)
    {
        globus_gram_job_manager_set_status(
                request->manager,
                request->job_contact_path,
                request->status,
                request->failure_code,
                request->exit_code);
    }
    return GLOBUS_SUCCESS;
}
/* globus_gram_job_manager_request_set_status() */

/**
 * Write data to the job manager log file
 *
 * This function writes data to the passed file, using a printf format
 * string. Data is prefixed with a timestamp when written.
 *
 * @param log_fp
 *        Log file to write to.
 * @param format
 *        Printf-style format string to be written.
 * @param ...
 *        Parameters substituted into the format string, if needed.
 *
 * @return This function returns the value returned by vfprintf.
 */
void
globus_gram_job_manager_request_log(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_log_level_t level,
    const char *                        format,
    ... )
{
    va_list ap;
    int stdio_level = level;

    /* Allow logging code to determine this thread's current job request to handle
     * per-job log configuration
     */
    globus_thread_setspecific(
            globus_i_gram_request_key,
            request);

    /* job_log_level is initialized to the global log level, but can be modified via
     * the log_level RSL attribute. If it masks the requested log level to zero, don't bother
     * calling the logging functions
     */
    if (request != NULL && request->job_log_level != -1)
    {
        stdio_level = level & request->job_log_level;

        if (stdio_level == 0)
        {
            goto skip_stdio;
        }

        /*
         * If we have a request-specific log level that differs from the global
         * config, we need to make sure the log level matches the global type
         * mask so that it won't get discarded by globus_logging_vwrite
         */
        if (request->config)
        {
            stdio_level = request->config->log_levels;
        }
    }
    va_start(ap, format);
    globus_logging_vwrite(
        globus_i_gram_job_manager_log_stdio,
        stdio_level,
        format,
        ap);
    va_end(ap);

skip_stdio:
    if (globus_i_gram_job_manager_log_sys != NULL)
    {
        va_start(ap, format);
        globus_logging_vwrite(
            globus_i_gram_job_manager_log_sys,
            level,
            format,
            ap);
        va_end(ap);
    }

    globus_thread_setspecific(
            globus_i_gram_request_key,
            NULL);
}
/* globus_gram_job_manager_request_log() */

/**
 * Write data to the job manager accounting file.
 * Also use syslog() to allow for easy central collection.
 *
 * This function writes data to the passed file descriptor, if any,
 * using a printf format string.
 * Data is prefixed with a timestamp when written.
 *
 * @param format
 *        Printf-style format string to be written.
 * @param ...
 *        Parameters substituted into the format string, if needed.
 *
 * @return This function returns the value returned by write().
 */
int
globus_gram_job_manager_request_acct(
    globus_gram_jobmanager_request_t *  request,
    const char *                        format,
    ... )
{
    static const char *jm_syslog_id  = "gridinfo";
    static int         jm_syslog_fac = LOG_DAEMON;
    static int         jm_syslog_lvl = LOG_NOTICE;
    static int         jm_syslog_init;
    struct tm *curr_tm;
    time_t curr_time;
    va_list ap;
    int rc = -1;
    int fd;
    const char * gk_acct_fd_var = "GATEKEEPER_ACCT_FD";
    const char * gk_acct_fd;
    int n;
    int t;
    char buf[1024 * 128];

    time( &curr_time );
    curr_tm = localtime( &curr_time );

    n = t = sprintf( buf, "JMA %04d/%02d/%02d %02d:%02d:%02d ",
                curr_tm->tm_year + 1900,
                curr_tm->tm_mon + 1, curr_tm->tm_mday,
                curr_tm->tm_hour, curr_tm->tm_min,
                curr_tm->tm_sec );

    va_start( ap, format );

    /*
     * FIXME: we should use vsnprintf() here...
     */

    n += vsprintf( buf + t, format, ap );

    if (!jm_syslog_init)
    {
        const char *s;

        if ((s = getenv( "JOBMANAGER_SYSLOG_ID"  )) != 0)
        {
            jm_syslog_id = *s ? s : 0;
        }

        if ((s = getenv( "JOBMANAGER_SYSLOG_FAC" )) != 0)
        {
            if (sscanf( s, "%d", &jm_syslog_fac ) != 1 || jm_syslog_fac < 0)
            {
                jm_syslog_id = 0;
            }
        }

        if ((s = getenv( "JOBMANAGER_SYSLOG_LVL" )) != 0)
        {
            if (sscanf( s, "%d", &jm_syslog_lvl ) != 1 || jm_syslog_lvl < 0)
            {
                jm_syslog_id = 0;
            }
        }

        if (jm_syslog_id)
        {
            openlog( jm_syslog_id, LOG_PID, jm_syslog_fac );
        }

        jm_syslog_init = 1;
    }

    if (jm_syslog_id)
    {
        char *p, *q = buf;

        while ((p = q) < buf + n) {
            char c;

            while ((c = *q) != 0 && c != '\n') {
                q++;
            }

            *q = 0;

            syslog( jm_syslog_lvl, "%s", p );

            *q++ = c;
        }
    }

    if (!(gk_acct_fd = getenv( gk_acct_fd_var )))
    {
        return -1;
    }

    if (sscanf( gk_acct_fd, "%d", &fd ) != 1)
    {
        return -1;
    }

    fcntl( fd, F_SETFD, FD_CLOEXEC );

    if ((rc = write( fd, buf, n )) != n)
    {
        rc = -1;
    }

    return rc;
}
/* globus_gram_job_manager_request_acct() */

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * Populate the request symbol table with values from the job manager config
 *
 * @param request
 *     Request to update the symbol table of
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed
 */
int
globus_i_gram_symbol_table_populate(
    globus_gram_job_manager_config_t *  config,
    globus_symboltable_t *              symbol_table)
{
    int                                 rc = GLOBUS_SUCCESS;
    int                                 i;
    struct { char * symbol; char *value; } symbols[] =
    {
        { "HOME", config->home },
        { "LOGNAME", config->logname },
        { "GLOBUS_ID", config->subject },
        { "GLOBUS_HOST_MANUFACTURER",config->globus_host_manufacturer},
        { "GLOBUS_HOST_CPUTYPE",config->globus_host_cputype},
        { "GLOBUS_HOST_OSNAME",config->globus_host_osname},
        { "GLOBUS_HOST_OSVERSION",config->globus_host_osversion},
        { "GLOBUS_GATEKEEPER_HOST",config->globus_gatekeeper_host},
        { "GLOBUS_GATEKEEPER_PORT",config->globus_gatekeeper_port},
        { "GLOBUS_GATEKEEPER_SUBJECT",config->globus_gatekeeper_subject},
        { "GLOBUS_LOCATION", config->target_globus_location },
        { "GLOBUS_CONDOR_OS", config->condor_os } /* Deprecated */,
        { "GLOBUS_CONDOR_ARCH", config->condor_arch } /* Deprecated */,
        /* Others are job dependent values inserted after they are computed:
         * - GLOBUS_GRAM_JOB_CONTACT
         * - GLOBUS_CACHED_STDOUT
         * - GLOBUS_CACHED_STDERR
         * - SCRATCH_DIRECTORY
         */
        { NULL, NULL }
    };


    for (i = 0; symbols[i].symbol != NULL; i++)
    {
        if (symbols[i].value != NULL)
        {
            rc = globus_l_gram_symboltable_add(
                    symbol_table,
                    symbols[i].symbol,
                    symbols[i].value);
            if (rc != GLOBUS_SUCCESS)
            {
                goto failed_insert_symbol;
            }
        }
    }
    if (rc != GLOBUS_SUCCESS)
    {
failed_insert_symbol:
        for (--i; i >=0; i--)
        {
            globus_symboltable_remove(
                    symbol_table,
                    symbols[i].symbol);
        }
    }
    return rc;
}
/* globus_gram_symbol_table_populate() */

/**
 * Insert a symbol into the RSL evaluation symbol table
 * 
 * Also checks that the value is non-NULL and transforms the return
 * value to a GRAM error code.
 *
 * @param symboltable
 *     Symbol table to insert the value to.
 * @param symbol
 *     Symbol name to add to the table.
 * @pram value
 *     Symbol value to add to the table. If NULL nothing is inserted.
 * 
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed
 */
static
int
globus_l_gram_symboltable_add(
    globus_symboltable_t *              symbol_table,
    const char *                        symbol,
    const char *                        value)
{
    int                                 rc = GLOBUS_SUCCESS;

    if (value != NULL)
    {
        rc = globus_symboltable_insert(
            symbol_table,
            (void *) symbol,
            (void *) value);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        }
    }
    return rc;
}
/* globus_l_gram_symboltable_add() */

static
int
globus_l_gram_generate_id(
    globus_gram_jobmanager_request_t *  request,
    char **                             jm_restart,
    uint64_t *                          uniq1p,
    uint64_t *                          uniq2p)
{
    int                                 rc = GLOBUS_SUCCESS;

    *jm_restart = NULL;
    if(globus_gram_job_manager_rsl_need_restart(request))
    {
        /* Need to do this before unique id is set */
        rc = globus_gram_job_manager_rsl_eval_one_attribute(
                request,
                GLOBUS_GRAM_PROTOCOL_RESTART_PARAM,
                jm_restart);

        if (rc == GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_RESTART;
            goto failed_jm_restart_eval;
        }
        else if (rc != GLOBUS_SUCCESS)
        {
            goto failed_jm_restart_eval;
        }
        else if (*jm_restart == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_RESTART;
            goto failed_jm_restart_eval;
        }

        rc = sscanf(
                request->jm_restart,
                "https://%*[^:]:%*d/%"PRIu64"/%"PRIu64,
                uniq1p,
                uniq2p);
        if (rc < 2)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_RESTART;
            goto failed_jm_restart_scan;
        }
        rc = GLOBUS_SUCCESS;
    }
    else
    {
        globus_uuid_t                   uuid;

        rc = globus_uuid_create(&uuid);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        }

        request->jm_restart = NULL;

        memcpy(uniq1p, uuid.binary.bytes, 8);
        memcpy(uniq2p, uuid.binary.bytes+8, 8);
    }

    if (rc != GLOBUS_SUCCESS)
    {
failed_jm_restart_scan:
        if (request->jm_restart != NULL)
        {
            free(request->jm_restart);
        }
    }
failed_jm_restart_eval:
    return rc;
}
/* globus_l_gram_generate_id() */

/**
 * Determine the cache location to use for this job
 *
 * If the gass_cache RSL attribute is present, it is evaluated and used.
 * Otherwise, if -cache-location was in the configuration, it used. Otherwise,
 * the GASS cache library default is used.
 *
 * @param request
 *     Request to use to find which value to use.
 * @param cache_locationp
 *     Pointer to set to the job-specific cache location.
 * @param cache_handlep
 *     Pointer to the GASS cache handle to initialize for this job
 *
 * @retval GLOBUS_SUCCESS
 *     Success.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_RSL_CACHE
 *     Invalid gass_cache RSL parameter.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_CACHE
 *     Invalid cache path.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_CACHE
 *     Error opening cache.
 */
static
int
globus_l_gram_init_cache(
    globus_gram_jobmanager_request_t *  request,
    char **                             cache_locationp,
    globus_gass_cache_t  *              cache_handlep)
{
    int                                 rc = GLOBUS_SUCCESS;
    int                                 gassrc = GLOBUS_SUCCESS;

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.gass_cache_init.start "
            "level=TRACE "
            "gramid=%s "
            "\n",
            request->job_contact_path);

    if (globus_gram_job_manager_rsl_attribute_exists(
                request->rsl,
                GLOBUS_GRAM_PROTOCOL_GASS_CACHE_PARAM))
    {
        /* If gass_cache is in RSL, we'll evaluate that and use it. */
        rc = globus_gram_job_manager_rsl_eval_one_attribute(
                request,
                GLOBUS_GRAM_PROTOCOL_GASS_CACHE_PARAM,
                cache_locationp);

        if (rc != GLOBUS_SUCCESS)
        {
            if (rc == GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_CACHE;
            }

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.gass_cache_init.end "
                    "level=ERROR "
                    "gramid=%s "
                    "status=%d "
                    "msg=\"%s\" "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    -rc,
                    "Error evaluating cache RSL attribute",
                    globus_gram_protocol_error_string(rc));

            goto failed_cache_eval;
        }

        /* cache location in rsl, but not a literal after eval */
        if ((*cache_locationp) == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_CACHE;

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.gass_cache_init.end "
                    "level=ERROR "
                    "gramid=%s "
                    "status=%d "
                    "msg=\"%s\" "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    -rc,
                    "Error evaluating cache RSL attribute",
                    globus_gram_protocol_error_string(rc));

            goto failed_cache_eval;
        }
    }
    else if (request->config->cache_location != NULL)
    {
        /* If -cache-location was on command-line or config file, then 
         * eval and use it
         */
        rc = globus_gram_job_manager_rsl_eval_string(
                &request->symbol_table,
                request->config->cache_location,
                cache_locationp);

        if(rc != GLOBUS_SUCCESS)
        {
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.gass_cache_init.end "
                    "level=ERROR "
                    "gramid=%s "
                    "status=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    -rc,
                    globus_gram_protocol_error_string(rc));

            goto failed_cache_eval;
        }
    }
    else
    {
        /* I'd like to use GASS-default location for the cache for this, 
         * but we can't rely on being able to set the environment and having
         * it remain valid if multiple jobs are being processed. Instead, we'll
         * force it to what the library would do anyway.
         */
        *cache_locationp = globus_common_create_string(
                "%s/.globus/.gass_cache",
                request->config->home);

    }

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.gass_cache_init.info "
            "level=TRACE "
            "gramid=%s "
            "path=%s\n",
            request->job_contact_path,
            *(cache_locationp));

    memset(cache_handlep, 0, sizeof(globus_gass_cache_t));
    gassrc = rc = globus_gass_cache_open(*cache_locationp, cache_handlep);
    if(rc != GLOBUS_SUCCESS)
    {
        if (*cache_locationp)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_CACHE;
        }
        else
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_CACHE;
        }
        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.gass_cache_init.end "
                "level=ERROR "
                "gramid=%s "
                "status=%d "
                "path=%s "
                "msg=\"%s\" "
                "gasserror=%d "
                "reason=\"%s\"\n",
                request->job_contact_path,
                -rc,
                *(cache_locationp),
                "Error opening GASS cache",
                gassrc,
                globus_gass_cache_error_string(gassrc));

        if (*cache_locationp)
        {
            free(*cache_locationp);
            *cache_locationp = NULL;
        }
    }
    else
    {
        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                "event=gram.gass_cache_init.end "
                "level=TRACE "
                "gramid=%s "
                "status=%d "
                "path=%s\n",
                request->job_contact_path,
                0,
                *(cache_locationp));
    }
failed_cache_eval:
    return rc;
}
/* globus_l_gram_init_cache() */

static
int
globus_l_gram_restart(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_jobmanager_request_t ** old_job_request)
{
    int                                 rc;
    globus_rsl_t *                      stdout_position;
    globus_rsl_t *                      stderr_position;
    globus_rsl_t *                      restart_rsl;
    globus_rsl_t *                      original_rsl;
    globus_rsl_t *                      restartcontacts;
    globus_bool_t                       restart_contacts = GLOBUS_FALSE;

    /* Evaluate the restart RSL, so that we can merge it with the original
     * job RSL
     */
    rc = globus_rsl_eval(request->rsl, &request->symbol_table);
    if(rc != GLOBUS_SUCCESS)
    {
        request->failure_code =
            GLOBUS_GRAM_PROTOCOL_ERROR_RSL_EVALUATION_FAILED;
        goto rsl_eval_failed;
    }

    rc = globus_gram_job_manager_validate_rsl(
            request,
            request->rsl,
            GLOBUS_GRAM_VALIDATE_JOB_MANAGER_RESTART);
    if(rc != GLOBUS_SUCCESS)
    {
        goto rsl_validate_failed;
    }
    /*
     * Eval after validating, as validation may insert
     * RSL substitions when processing default values of
     * RSL attributes
     */
    rc = globus_rsl_eval(request->rsl, &request->symbol_table);
    if(rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_EVALUATION_FAILED;
        goto post_validate_eval_failed;
    }

    /* Free the restart RSL spec. Make room for the job
     * request RSL which we'll read from the state file
     */
    free(request->rsl_spec);
    request->rsl_spec = NULL;

    /* Remove the restart parameter from the RSL spec. */
    globus_gram_job_manager_rsl_remove_attribute(
            request->rsl,
            GLOBUS_GRAM_PROTOCOL_RESTART_PARAM);

    if (globus_gram_job_manager_request_exists(
            request->manager,
            request->job_contact_path))
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE;
    }
    else
    {
        /* Read the job state file. This has all sorts of side-effects on
         * the request structure
         */
        rc = globus_gram_job_manager_state_file_read(request);
    }


    if (rc == GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE)
    {
        /* Something is handling this request already. We'll check if it is
         * this process. If so, we'll merge the RSLs (as if we had done a
         * stdio update.
         */
        rc = globus_gram_job_manager_add_reference(
                request->manager,
                request->job_contact_path,
                "restart",
                old_job_request);

        if (rc != GLOBUS_SUCCESS)
        {
            /* OK. It's alive, but not our job. Let it be */
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE;

            goto state_file_read_failed;
        }
        rc = globus_i_gram_request_stdio_update(
                (*old_job_request),
                request->rsl);
        if (rc == GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE;
        }
        else
        {
            /* Likely, we return GLOBUS_SUCCESS in this case, the wrong error value */
            rc = globus_gram_job_manager_remove_reference(
                    request->manager,
                    request->job_contact_path,
                    "restart");
            *old_job_request = NULL;
        }
        goto old_jm_alive;
    }
    else if(rc != GLOBUS_SUCCESS)
    {
        goto state_file_read_failed;
    }


    original_rsl = globus_rsl_parse(request->rsl_spec);
    if (!original_rsl)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
        goto parse_original_rsl_failed;
    }

    restart_rsl = request->rsl;
    request->rsl = NULL;

    rc = globus_gram_job_manager_rsl_attribute_get_boolean_value(
            restart_rsl,
            "restartcontacts",
            &restart_contacts);

    if (rc != GLOBUS_SUCCESS || !restart_contacts)
    {
        globus_gram_job_manager_contact_list_free(request);
        rc = GLOBUS_SUCCESS;
    }

    restartcontacts = globus_gram_job_manager_rsl_extract_relation(
            restart_rsl,
            "restartcontacts");
    if (restartcontacts != NULL)
    {
        globus_rsl_free_recursive(restartcontacts);
    }

    /*
     * Remove stdout_position and stderr_position. We don't do streaming
     * any more, so we will reject any restart where the positions
     * aren't 0 (validation file checks those).
     */
    stdout_position = globus_gram_job_manager_rsl_extract_relation(
            restart_rsl,
            GLOBUS_GRAM_PROTOCOL_STDOUT_POSITION_PARAM);
    if (stdout_position != NULL)
    {
        globus_rsl_free_recursive(stdout_position);
    }

    stderr_position = globus_gram_job_manager_rsl_extract_relation(
            restart_rsl,
            GLOBUS_GRAM_PROTOCOL_STDERR_POSITION_PARAM);
    if (stderr_position != NULL)
    {
        globus_rsl_free_recursive(stderr_position);
    }

    request->rsl = globus_gram_job_manager_rsl_merge(
                original_rsl,
                restart_rsl);

    if(request->rsl == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }
    request->job_stats.restart_count++;
    if (original_rsl)
    {
        globus_rsl_free_recursive(original_rsl);
    }
    if (restart_rsl)
    {
        globus_rsl_free_recursive(restart_rsl);
    }
parse_original_rsl_failed:
old_jm_alive:
state_file_read_failed:
post_validate_eval_failed:
rsl_validate_failed:
rsl_eval_failed:
    return rc;
}
/* globus_l_gram_restart() */

int
globus_i_gram_request_stdio_update(
    globus_gram_jobmanager_request_t *  request,
    globus_rsl_t *                      update_rsl)
{
    int                                 rc = GLOBUS_SUCCESS;
    const char *                        tmp_string;
    globus_rsl_t *                      tmp_rsl;
    globus_rsl_t *                      stdout_position;
    globus_rsl_t *                      stderr_position;
    globus_rsl_t *                      original_rsl;

    /* TODO: We should almost certainly validate RSL here
    rc = globus_gram_job_manager_validate_rsl(
            request,
            tmp_rsl,
            GLOBUS_GRAM_VALIDATE_STDIO_UPDATE);
    if(rc != GLOBUS_SUCCESS)
    {
        goto parse_original_rsl_failed;
    }
    */

    /*
     * Remove stdout_position and stderr_position. We don't do streaming
     * any more, so we will reject any restart where the positions
     * aren't 0.
     */
    stdout_position = globus_gram_job_manager_rsl_extract_relation(
            update_rsl,
            GLOBUS_GRAM_PROTOCOL_STDOUT_POSITION_PARAM);
    if (stdout_position != NULL)
    {
        globus_rsl_free_recursive(stdout_position);
    }

    stderr_position = globus_gram_job_manager_rsl_extract_relation(
            update_rsl,
            GLOBUS_GRAM_PROTOCOL_STDERR_POSITION_PARAM);
    if (stderr_position != NULL)
    {
        globus_rsl_free_recursive(stderr_position);
    }

    original_rsl = globus_rsl_parse(request->rsl_spec);
    if (!original_rsl)
    {           
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
        goto parse_original_rsl_failed;
    }           

    /* TODO: it appears tmp_rsl leaks if a failure occurs */
    tmp_rsl = globus_gram_job_manager_rsl_merge(
        original_rsl,
        update_rsl);

    if (tmp_rsl == GLOBUS_NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
        goto failed_rsl_merge;
    }

    /* The update_rsl, for job restarts, contains a "restartcontacts"
     * attribute.  This must be removed from the merged RSL prior to
     * saving it to disk; otherwise, it will cause submits to choke.
     * TODO: There should be a smarter way to do this with RSL validation.
     */
    if (globus_gram_job_manager_rsl_remove_attribute(tmp_rsl, "restartcontacts"))
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
        goto failed_rsl_merge;
    }

    char * tmp_rsl_spec;
    if (!(tmp_rsl_spec = globus_rsl_unparse(tmp_rsl))) {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
        goto failed_rsl_merge;
    }

    rc = globus_rsl_eval(tmp_rsl, &request->symbol_table);
    if(rc != GLOBUS_SUCCESS)
    {   
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_EVALUATION_FAILED;
        goto failed_rsl_merge;
    }
    rc = globus_gram_job_manager_validate_rsl(
            request,
            tmp_rsl,
            GLOBUS_GRAM_VALIDATE_JOB_SUBMIT);
    if(rc != GLOBUS_SUCCESS)
    {
        goto failed_rsl_merge;
    }
    rc = globus_rsl_eval(tmp_rsl, &request->symbol_table);
    if(rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_EVALUATION_FAILED;
        goto failed_rsl_merge;
    }

    globus_rsl_free_recursive(request->rsl);
    request->rsl = tmp_rsl;
    free(request->rsl_spec);
    request->rsl_spec = tmp_rsl_spec;

    rc = globus_gram_job_manager_streaming_list_replace(request);
    if (rc != GLOBUS_SUCCESS)
    {
        goto staging_list_replace_failed;
    }

    rc = globus_gram_job_manager_rsl_attribute_get_string_value(
            request->rsl,
            GLOBUS_GRAM_PROTOCOL_REMOTE_IO_URL_PARAM,
            &tmp_string);
        
    switch (rc)
    {
    case GLOBUS_GRAM_PROTOCOL_ERROR_UNDEFINED_ATTRIBUTE:
        rc = GLOBUS_SUCCESS;
        break;
    case GLOBUS_SUCCESS:
        if (tmp_string != NULL)
        {
            if (request->remote_io_url)
            {
                free(request->remote_io_url);
            }
            request->remote_io_url = strdup(tmp_string);
            if (request->remote_io_url == NULL)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
                goto get_remote_io_url_failed;
            }
        }
        else
        {
    default:
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_REMOTE_IO_URL;
            goto get_remote_io_url_failed;
        }
    }

    if (request->remote_io_url)
    {
        rc = globus_i_gram_remote_io_url_update(request);
    }

    /* Now that we've recreated the stdio, redo the staging list. */
    globus_gram_job_manager_staging_free_all(request);
    /*
    globus_gram_jobmanager_request_t *  request)
    request->stage_in_todo = NULL;
    request->stage_in_shared_todo = NULL;
    request->stage_out_todo = NULL;
    request->stage_stream_todo = NULL;
    */
    rc = globus_gram_job_manager_staging_create_list(request);
    if (rc != GLOBUS_SUCCESS) {
        globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                        "event=gram.restart.info "
                        "level=ERROR "
                        "gramid=%s "
                        "status=%d "
                        "msg=\"%s\" "
                        "reason=\"%s\" "
                        "\n",
                        request->job_contact_path,
                        -rc,
                        "Unable to recreate staging list",
                        globus_gram_protocol_error_string(rc));
        goto staging_list_replace_failed;
    }

    globus_gram_job_manager_state_file_write(request);

get_remote_io_url_failed:
staging_list_replace_failed:
failed_rsl_merge:
    if (original_rsl) {
        globus_rsl_free_recursive(original_rsl);
    }
parse_original_rsl_failed:
    return rc;
}
/* globus_i_gram_request_stdio_update() */

/**
 * Add default environment variables to the job environment
 *
 * Adds GLOBUS_GASS_CACHE_DEFAULT, LOGNAME, HOME, and anything that
 * is defined from the -extra-envar command-line option.
 * 
 * @param request
 *     Request to modify
 *
 * @retval GLOBUS_SUCCESS
 *     Success.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
 *     Malloc failed.
 */
static
int
globus_l_gram_populate_environment(
    globus_gram_jobmanager_request_t *  request)
{
    int                                 rc;

    rc = globus_l_gram_add_environment(
            request->rsl,
            "GLOBUS_GASS_CACHE_DEFAULT",
            request->cache_location);

    if (rc != GLOBUS_SUCCESS)
    {
        goto add_cache_default_failed;
    }

    rc = globus_l_gram_add_environment(
            request->rsl,
            "LOGNAME",
            request->config->logname);
    if (rc != GLOBUS_SUCCESS)
    {
        goto add_logname_failed;
    }

    rc = globus_l_gram_add_environment(
            request->rsl,
            "HOME",
            request->config->home);
    if (rc != GLOBUS_SUCCESS)
    {
        goto add_home_failed;
    }

    if (request->config->x509_cert_dir)
    {
        rc = globus_l_gram_add_environment(
                request->rsl,
                "X509_CERT_DIR",
                request->config->x509_cert_dir);
        if (rc != GLOBUS_SUCCESS)
        {
            goto add_x509_cert_dir_failed;
        }
    }

    rc = globus_l_gram_add_environment(
            request->rsl,
            "GLOBUS_GRAM_JOB_CONTACT",
            request->job_contact);
    if (rc != GLOBUS_SUCCESS)
    {
        goto add_gram_job_contact_failed;
    }

    if (request->config->target_globus_location)
    {
        rc = globus_l_gram_add_environment(
                request->rsl,
                "GLOBUS_LOCATION",
                request->config->target_globus_location);
        if (rc != GLOBUS_SUCCESS)
        {
            goto add_globus_location_failed;
        }
    }

    if (request->config->tcp_port_range)
    {
        rc = globus_l_gram_add_environment(
                request->rsl,
                "GLOBUS_TCP_PORT_RANGE",
                request->config->tcp_port_range);
        if (rc != GLOBUS_SUCCESS)
        {
            goto add_tcp_port_range_failed;
        }
    }

    if (request->config->tcp_source_range)
    {
        rc = globus_l_gram_add_environment(
                request->rsl,
                "GLOBUS_TCP_SOURCE_RANGE",
                request->config->tcp_source_range);
        if (rc != GLOBUS_SUCCESS)
        {
            goto add_tcp_source_range_failed;
        }
    }

    if (request->remote_io_url_file)
    {
        rc = globus_l_gram_add_environment(
                request->rsl,
                "GLOBUS_REMOTE_IO_URL",
                request->remote_io_url_file);
        if (rc != GLOBUS_SUCCESS)
        {
            goto add_remote_io_url_file;
        }
    }

    if (request->x509_user_proxy)
    {
        rc = globus_l_gram_add_environment(
                request->rsl,
                "X509_USER_PROXY",
                request->x509_user_proxy);
        if (rc != GLOBUS_SUCCESS)
        {
            goto add_x509_user_proxy_failed;
        }
    }
    if (request->config->extra_envvars)
    {
        globus_list_t *l = request->config->extra_envvars;

        while (l)
        {
            char *p = globus_list_first(l);
            char *q;
            char *var, *val;
            l = globus_list_rest(l);

            if ((q = strchr(p, '=')) != NULL)
            {
                
                var = globus_common_create_string("%.*s",
                        (int) (q-p), p);
                val = q+1;
            }
            else
            {
                var = strdup(p);
                val = getenv(var);
            }

            if (var && val)
            {
                rc = globus_l_gram_add_environment(
                        request->rsl,
                        var,
                        val);

                if (rc != GLOBUS_SUCCESS)
                {
                    free(var);
                    goto add_extra_envvar_failed;
                }
            }
            if (var)
            {
                free(var);
            }
        }
    }

add_extra_envvar_failed:
add_x509_user_proxy_failed:
add_remote_io_url_file:
add_tcp_port_range_failed:
add_tcp_source_range_failed:
add_globus_location_failed:
add_gram_job_contact_failed:
add_x509_cert_dir_failed:
add_home_failed:
add_logname_failed:
add_cache_default_failed:
    return rc;
}
/* globus_l_gram_populate_environment() */

/**
 * Add an environment variable to the job environment
 * 
 * @param rsl
 *     RSL to modify
 * @param variable
 *     Environment variable name
 * @param value
 *     Environment variable value. If NULL, this variable is ignored.
 *
 * @retval GLOBUS_SUCCESS
 *     Success.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
 *     Malloc failed.
 */
static
int
globus_l_gram_add_environment(
    globus_rsl_t *                      rsl,
    const char *                        variable,
    const char *                        value)
{
    int                                 rc = GLOBUS_SUCCESS;
    if (value != NULL)
    {
        rc = globus_gram_job_manager_rsl_env_add(
                rsl,
                variable,
                value);
        if (rc != 0)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        }
    }
    return rc;
}
/* globus_l_gram_add_environment() */

/**
 * Initialize the scratchdir member of the job request if needed
 *
 * As a side effect, the path named by scratchdir will be created and
 * variable SCRATCH_DIRECTORY will be added to both the symbol table and
 * the job environment.
 *
 * @param request
 *     Request to act on.
 * @param rsl
 *     RSL to check for "scratchdir" attribute
 * @param scratch_dir_base
 *     Job-specific scratch_dir_base
 * @param scratchdir
 *     Pointer to set to the new scratchdir
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_RSL_SCRATCH
 *     Invalid scratchdir RSL attribute
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRATCH
 *     Invalid scratchdir path
 */
static
int
globus_l_gram_init_scratchdir(
    globus_gram_jobmanager_request_t *  request,
    globus_rsl_t *                      rsl,
    const char *                        scratch_dir_base,
    char **                             scratchdir)
{
    int                                 rc = GLOBUS_SUCCESS;
    char *                              dir = NULL;
    char *                              template = NULL;
    int                                 i;
    int                                 created = 0;
    enum { GLOBUS_GRAM_MKDIR_TRIES = 100 };

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.init_scratchdir.start "
            "level=DEBUG "
            "gramid=%s "
            "base=\"%s\" "
            "\n",
            request->job_contact_path,
            scratch_dir_base);

    /* In the case of a restart, this might have already been done */
    if (request->jm_restart && request->scratchdir != NULL)
    {
        goto skip_mkdir;
    }
    if (! globus_gram_job_manager_rsl_attribute_exists(
            rsl,
            GLOBUS_GRAM_PROTOCOL_SCRATCHDIR_PARAM))
    {
        *scratchdir = NULL;
        goto no_scratch;
    }

    rc = globus_gram_job_manager_rsl_eval_one_attribute(
            request,
            GLOBUS_GRAM_PROTOCOL_SCRATCHDIR_PARAM,
            &dir);
    if(rc != GLOBUS_SUCCESS)
    {
        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.init_scratchdir.end "
                "level=ERROR "
                "gramid=%s "
                "msg=\"%s\" "
                "attribute=%s "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                "RSL evaluation failed",
                GLOBUS_GRAM_PROTOCOL_SCRATCHDIR_PARAM,
                -rc,
                globus_gram_protocol_error_string(rc));
        if (rc == GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_SCRATCH;
        }
        goto eval_scratchdir_failed;
    }
    else if (dir == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_SCRATCH;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.init_scratchdir.end "
                "level=ERROR "
                "gramid=%s "
                "msg=\"%s\" "
                "attribute=%s "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                "RSL evaluation didn't yield a string",
                GLOBUS_GRAM_PROTOCOL_SCRATCHDIR_PARAM,
                -rc,
                globus_gram_protocol_error_string(rc));
        goto eval_scratchdir_failed;
    }

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.init_scratchdir.info "
            "level=TRACE "
            "gramid=%s "
            "dir=\"%s\" "
            "\n",
            request->job_contact_path,
            dir);

    if (dir[0] == '/')
    {
        template = globus_common_create_string(
                "%s/gram_scratch_XXXXXX",
                dir);
    }
    else 
    {
        template = globus_common_create_string(
                "%s/%s/gram_scratch_XXXXXX",
                scratch_dir_base,
                dir);
    }
    if (template == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.init_scratchdir.end "
                "level=ERROR "
                "gramid=%s "
                "msg=\"%s\" "
                "status=%d "
                "errno=%d "
                "reason=\"%s\"\n",
                request->job_contact_path,
                "Directory template allocation failed",
                -rc,
                errno,
                strerror(errno));

        goto template_malloc_failed;
    }

    for (i = 0, created = 0; i < GLOBUS_GRAM_MKDIR_TRIES && !created; i++)
    {
        char *                          scratchname;
        
        scratchname = strdup(template);

        if (scratchname == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.init_scratchdir.end "
                    "level=ERROR "
                    "gramid=%s "
                    "msg=\"%s\" "
                    "status=%d "
                    "errno=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    "Directory template allocation failed",
                    -rc,
                    errno,
                    strerror(errno));

            goto scratchname_strdup_failed;
        }
        *scratchdir = mktemp(scratchname);

        if (mkdir(*scratchdir, S_IRWXU) != 0)
        {
            if (errno != EEXIST && errno != EINTR)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRATCH;
                globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                        "event=gram.init_scratchdir.end "
                        "level=ERROR "
                        "gramid=%s "
                        "msg=\"%s\" "
                        "status=%d "
                        "errno=%d "
                        "reason=\"%s\" "
                        "\n",
                        request->job_contact_path,
                        "Error creating directory",
                        -rc,
                        errno,
                        strerror(errno));

                goto fatal_mkdir_err;
            }
            else
            {
                free(*scratchdir);
                *scratchdir = NULL;
            }
        }
        else
        {
            created = 1;
        }
    }

    if (*scratchdir == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SCRATCH;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.init_scratchdir.end "
                "level=ERROR "
                "gramid=%s "
                "msg=\"%s\" "
                "status=%d "
                "\n",
                request->job_contact_path,
                "Error creating directory",
                -rc);
        goto fatal_mkdir_err;
    }

skip_mkdir:
    rc = globus_symboltable_insert(
            &request->symbol_table,
            "SCRATCH_DIRECTORY",
            *scratchdir);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.init_scratchdir.end "
                "level=ERROR "
                "gramid=%s "
                "msg=\"%s\" "
                "status=%d "
                "\n",
                request->job_contact_path,
                "Error inserting scratch directory into RSL symbol table",
                -rc);

        goto insert_symbol_failed;
    }

    rc = globus_l_gram_add_environment(
            request->rsl,
            "SCRATCH_DIRECTORY",
            *scratchdir);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_symboltable_remove(
                &request->symbol_table,
                "SCRATCH_DIRECTORY");

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.init_scratchdir.end "
                "level=ERROR "
                "gramid=%s "
                "msg=\"%s\" "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                "Error inserting scratch directory into job environment",
                -rc,
                globus_gram_protocol_error_string(rc));
insert_symbol_failed:
        rmdir(*scratchdir);
fatal_mkdir_err:
        free(*scratchdir);
        *scratchdir = NULL;
    }
    else
scratchname_strdup_failed:
    if (template)
    {
        free(template);
    }
template_malloc_failed:
    if (dir)
    {
        free(dir);
    }
eval_scratchdir_failed:
no_scratch:
    if (rc == GLOBUS_SUCCESS)
    {
        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                "event=gram.init_scratchdir.end "
                "level=DEBUG "
                "gramid=%s "
                "status=%d "
                "%s=\"%s\" "
                "\n",
                request->job_contact_path,
                0,
                *scratchdir ? "path" : "reason",
                *scratchdir ? *scratchdir : "scratch_dir not in RSL");
    }
    return rc;
}
/* globus_l_gram_init_scratchdir() */

/**
 * Remove a directory and all of its contents
 * 
 * @param request
 *     Request related to this directory.
 * @param directory
 *     Directory to remove.
 */
void
globus_gram_job_manager_destroy_directory(
    globus_gram_jobmanager_request_t *  request,
    const char *                        directory)
{
    int                                 rc;
    char *                              path;
    char *                              new_path;
    DIR *                               dir;
    struct dirent *                     entry;
    struct stat                         st;
    globus_list_t *                     unchecked_dir_list = NULL;
    globus_list_t *                     dir_list = NULL;
    int                                 failures = 0;

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.directory_destroy.start "
            "level=TRACE "
            "gramid=%s "
            "path=\"%s\" "
            "\n",
            request->job_contact_path,
            directory);

    path = strdup(directory);
    if (path == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.directory_destroy.end "
                "level=WARN "
                "gramid=%s "
                "path=\"%s\" "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                directory,
                -rc,
                globus_gram_protocol_error_string(rc));

        goto path_strdup_failed;
    }
    rc = globus_list_insert(&unchecked_dir_list, path);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.directory_destroy.end "
                "level=WARN "
                "gramid=%s "
                "path=\"%s\" "
                "status=%d "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                directory,
                -rc,
                errno,
                strerror(errno));

        free(path);

        goto unchecked_dir_insert_failed;
    }

    while (!globus_list_empty(unchecked_dir_list))
    {
        /* We walk the directory structure once, removing all non-directory
         * entries. We store the directories in a list for a second pass.
         *
         * Note, we don't do much error handling here. If we fail, we'll leave
         * a dropping and note it in the log, but there's not much we can do.
         */
        path = globus_list_remove(&unchecked_dir_list, unchecked_dir_list);
        if (path == NULL)
        {
            continue;
        }
        rc = globus_list_insert(&dir_list, path);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.directory_destroy.info "
                    "level=WARN "
                    "gramid=%s "
                    "path=\"%s\" "
                    "msg=\"%s\" "
                    "status=%d "
                    "errno=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    path,
                    "List insert failed",
                    -rc,
                    errno,
                    strerror(errno));
            failures++;
            continue;
        }

        dir = opendir(path);
        if (dir == NULL)
        {
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.directory_destroy.info "
                    "level=WARN "
                    "gramid=%s "
                    "path=\"%s\" "
                    "msg=\"%s\" "
                    "status=%d "
                    "errno=%d "
                    "reason=\"%s\"\n",
                    request->job_contact_path,
                    path,
                    "opendir failed",
                    -1,
                    errno,
                    strerror(errno));
            failures++;
            continue;
        }

        while (globus_libc_readdir_r(dir, &entry) == 0 && entry != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0)
            {
                free(entry);
                continue;
            }
            new_path = globus_common_create_string(
                    "%s/%s",
                    path,
                    entry->d_name);
            if (new_path == NULL)
            {
                globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                        "event=gram.directory_destroy.info "
                        "level=WARN "
                        "gramid=%s "
                        "path=\"%s\" "
                        "file=\"%s\" "
                        "msg=\"%s\" "
                        "status=%d "
                        "errno=%d "
                        "reason=\"%s\" "
                        "\n",
                        request->job_contact_path,
                        path,
                        entry->d_name,
                        "Malloc failed",
                        -1,
                        errno,
                        strerror(errno));
                failures++;
                free(entry);
                continue;
            }
            rc = lstat(new_path, &st);
            if (rc < 0)
            {
                globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                        "event=gram.directory_destroy.info "
                        "level=WARN "
                        "gramid=%s "
                        "path=\"%s\" "
                        "msg=\"%s\" "
                        "status=%d "
                        "errno=%d "
                        "reason=\"%s\" "
                        "\n",
                        request->job_contact_path,
                        new_path,
                        "lstat failed",
                        -1,
                        errno,
                        strerror(errno));

                failures++;
                free(entry);
                continue;
            }

            if (st.st_mode & S_IFDIR)
            {
                rc = globus_list_insert(&unchecked_dir_list, new_path);
                if (rc != GLOBUS_SUCCESS)
                {
                    globus_gram_job_manager_request_log(
                            request,
                            GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                            "event=gram.directory_destroy.info "
                            "level=WARN "
                            "gramid=%s "
                            "path=\"%s\" "
                            "msg=\"%s\" "
                            "status=%d "
                            "errno=%d "
                            "reason=\"%s\" "
                            "\n",
                            request->job_contact_path,
                            new_path,
                            "List insert failed",
                            -1,
                            errno,
                            strerror(errno));

                    free(new_path);
                    failures++;
                    free(entry);
                    continue;
                }
            }
            else
            {
                globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                        "event=gram.directory_destroy.info "
                        "level=TRACE "
                        "gramid=%s "
                        "path=\"%s\" "
                        "msg=\"About to unlink\" "
                        "\n",
                        request->job_contact_path,
                        new_path);
                rc = unlink(new_path);
                if (rc < 0)
                {
                    globus_gram_job_manager_request_log(
                            request,
                            GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                            "event=gram.directory_destroy.info "
                            "level=WARN "
                            "gramid=%s "
                            "path=\"%s\" "
                            "msg=\"%s\" "
                            "status=%d "
                            "errno=%d "
                            "reason=\"%s\" "
                            "\n",
                            request->job_contact_path,
                            new_path,
                            "Unlink failed",
                            -1,
                            errno,
                            strerror(errno));

                    failures++;
                }
                free(new_path);
                new_path = NULL;
                free(entry);
            }
        }
        closedir(dir);
    }

    while (!globus_list_empty(dir_list))
    {
        /* Second pass removes (should be empty) subdirectories */
        path = globus_list_remove(&dir_list, dir_list);

        rc = rmdir(path);
        if (rc < 0)
        {
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.directory_destroy.info "
                    "level=WARN "
                    "gramid=%s "
                    "path=\"%s\" "
                    "msg=\"%s\" "
                    "status=%d "
                    "errno=%d "
                    "reason=\"%s\" "
                    "\n",
                    request->job_contact_path,
                    path,
                    "rmdir failed",
                    -1,
                    errno,
                    strerror(errno));
            failures++;
        }
        free(path);
    }

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.directory_destroy.end "
            "level=DEBUG "
            "gramid=%s "
            "path=\"%s\" "
            "failures=%d "
            "status=%d "
            "\n",
            request->job_contact_path,
            directory,
            failures,
            failures == 0 ? 0 : -1);

unchecked_dir_insert_failed:
path_strdup_failed:
    return;
}
/* globus_gram_job_manager_destroy_directory() */

/**
 * Evaluate and validate the job RSL
 * 
 *
 * @param request
 *     Job request to validate
 *
 * @retval GLOBUS_SUCCESS
 *     Success.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_RSL_EVALUATION_FAILED
 *     RSL evaluation failed.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL
 *     Invalid RSL.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PARAMETER_NOT_SUPPORTED
 *     RSL attribute not supported.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_SUBMIT_ATTRIBUTE
 *     Invalid submit RSL attribute.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_RESTART_ATTRIBUTE;
 *     Invalid restart RSL attribute.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_STDIO_UPDATE_ATTRIBUTE;
 *     Invalid stdio_update RSL attribute.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
 *     Malloc failed.
 */
static
int
globus_l_gram_validate_rsl(
    globus_gram_jobmanager_request_t *  request)
{
    int                                 rc = GLOBUS_SUCCESS;

    rc = globus_rsl_eval(request->rsl, &request->symbol_table);
    if(rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_EVALUATION_FAILED;
        goto rsl_eval_failed;
    }
    rc = globus_gram_job_manager_validate_rsl(
            request,
            request->rsl,
            GLOBUS_GRAM_VALIDATE_JOB_SUBMIT);
    if(rc != GLOBUS_SUCCESS)
    {
        goto validate_rsl_failed;
    }

    rc = globus_rsl_eval(request->rsl, &request->symbol_table);
    if(rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_EVALUATION_FAILED;
        goto rsl_eval_failed2;
    }

rsl_eval_failed2:
validate_rsl_failed:
rsl_eval_failed:
    return rc;
}
/* globus_l_gram_validate_rsl() */

/**
 * Create remote_io_url file in the job directory
 *
 * @param request
 *     Request to log messages for
 * @param remote_io_url
 *     Value to write to the remote_io_url file
 * @param remote_io_url_filep
 *     Pointer to be set to the remote_io_url_file path upon success.
 *
 * @retval GLOBUS_SUCCESS
 *     Success.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_RSL_REMOTE_IO_URL
 *     Unable to create remote_io_url file.
 */
static
int
globus_l_gram_remote_io_url_file_create(
    globus_gram_jobmanager_request_t *  request,
    const char *                        remote_io_url,
    const char *                        job_dir,
    char **                             remote_io_url_filep)
{
    int                                 rc = GLOBUS_SUCCESS;
    FILE *                              fp;

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.remote_io_url_file_create.start "
            "level=TRACE "
            "gramid=%s "
            "url=\"%s\" "
            "\n",
            request->job_contact_path,
            remote_io_url ? remote_io_url : "");

    if (!remote_io_url)
    {
        *remote_io_url_filep = NULL;
        goto out;
    }

    *remote_io_url_filep = globus_common_create_string(
                "%s/remote_io_file",
                job_dir);
    if (remote_io_url_filep == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.remote_io_url_file_create.end "
                "level=ERROR "
                "gramid=%s "
                "url=\"%s\" "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\"\n",
                request->job_contact_path,
                remote_io_url,
                -rc,
                "Error allocating path string",
                errno,
                strerror(errno));
        goto set_remote_io_url_file_failed;
    }
    fp = fopen(*remote_io_url_filep, "r");
    if (fp != NULL)
    {
        char * tmp;
        globus_bool_t skip_it = GLOBUS_FALSE;

        rc = fseek(fp, 0, SEEK_END);
        if (rc < 0)
        {
            goto close_readfp;
        }
        rc = ftell(fp);
        tmp = malloc(rc+1);
        if (tmp == NULL)
        {
            goto close_readfp;
        }
        if (fseek(fp, 0, SEEK_SET) < 0)
        {
            goto free_tmp;
        }
        tmp[rc] = 0;
        if (fgets(tmp, rc, fp) == NULL)
        {
            goto free_tmp;
        }
        /* trim trailing \n */
        rc = strlen(tmp)-1;
        if (tmp[rc] == '\n')
        {
            tmp[rc] = 0;
        }
        if (strcmp(tmp, remote_io_url) == 0)
        {
            skip_it = GLOBUS_TRUE;
        }
free_tmp:
        free(tmp);
close_readfp:
        fclose(fp);
        fp = NULL;

        rc = GLOBUS_SUCCESS;
        if (skip_it)
        {
            goto out;
        }
    }
    fp = fopen(*remote_io_url_filep, "w");
    if (fp == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_REMOTE_IO_URL;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.remote_io_url_file_create.end "
                "level=ERROR "
                "gramid=%s "
                "url=\"%s\" "
                "path=\"%s\" "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                remote_io_url,
                *remote_io_url_filep,
                -rc,
                "Error opening file",
                errno,
                strerror(errno));

        goto fopen_failed;
    }

    rc = fprintf(fp, "%s\n", remote_io_url);
    if (rc < (1+strlen(remote_io_url)))
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_REMOTE_IO_URL;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.remote_io_url_file_create.end "
                "level=ERROR "
                "gramid=%s "
                "url=\"%s\" "
                "path=\"%s\" "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                remote_io_url,
                *remote_io_url_filep,
                -rc,
                "Error writing remote_io file",
                errno,
                strerror(errno));

        goto write_failed;
    }
    rc = GLOBUS_SUCCESS;

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.remote_io_url_file_create.end "
            "level=TRACE "
            "gramid=%s "
            "url=\"%s\" "
            "path=\"%s\" "
            "status=0 "
            "\n",
            request->job_contact_path,
            remote_io_url,
            *remote_io_url_filep);

write_failed:
    fclose(fp);
    if (rc != GLOBUS_SUCCESS)
    {
fopen_failed:
        free(*remote_io_url_filep);
        *remote_io_url_filep = NULL;
    }
set_remote_io_url_file_failed:
out:
    return rc;
}
/* globus_l_gram_remote_io_url_file_create() */

static
int
globus_l_gram_export_cred(
    globus_gram_jobmanager_request_t *  request,
    gss_cred_id_t                       cred,
    const char *                        job_directory,
    char **                             proxy_filename)
{
    OM_uint32                           major_status, minor_status;
    char *                              filename = NULL;
    int                                 file;
    gss_buffer_desc                     buffer;
    int                                 rc = GLOBUS_SUCCESS;
    char *                              gt3_failure_message = NULL;
    int                                 save_errno;
    int                                 written;

    if (cred == GSS_C_NO_CREDENTIAL && !request->jm_restart)
    {
        goto no_cred;
    }

    if (cred != GSS_C_NO_CREDENTIAL)
    {
        major_status = gss_export_cred(
                &minor_status,
                cred,
                GSS_C_NO_OID,
                0,
                &buffer);

        if (GSS_ERROR(major_status))
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_CACHE_USER_PROXY;

            (void) globus_gss_assist_display_status_str(
                    &gt3_failure_message,
                    "Export proxy failed",
                    major_status,
                    minor_status,
                    0);

            goto export_cred_failed;
        }
    }

    filename = globus_common_create_string(
            "%s/x509_user_proxy",
            job_directory);
    if (filename == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto malloc_filename_failed;
    }

    if (cred == GSS_C_NO_CREDENTIAL && request->jm_restart)
    {
        goto jm_restart_done;
    }

    file = open(
            filename,
            O_WRONLY|O_CREAT|O_TRUNC,
            S_IRUSR|S_IWUSR);
    if (file < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_CACHE_USER_PROXY;
        save_errno = errno;
        gt3_failure_message = globus_common_create_string(
                "Error opening proxy file for writing: %s: %s (%d)",
                filename,
                strerror(save_errno),
                save_errno);
        goto fopen_failed;
    }

    written = 0;
    do
    {
        rc = write(
                file, 
                ((char *) buffer.value) + written,
                buffer.length - written);
        if (rc < 0)
        {
            save_errno = errno;
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_CACHE_USER_PROXY;
            gt3_failure_message = globus_common_create_string(
                    "Error writing proxy file: %s: %s (%d)",
                    filename,
                    strerror(save_errno),
                    save_errno);
            goto fwrite_failed;
        }
        else if (rc == 0)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_CACHE_USER_PROXY;
            gt3_failure_message = globus_common_create_string(
                    "Error writing proxy file: %s: %s",
                    filename,
                    "short write");
            goto fwrite_failed;
        }
        else
        {
            written += rc;
        }
    }
    while (written < buffer.length);
    rc = GLOBUS_SUCCESS;

fwrite_failed:
    rc = close(file);
    if (rc != 0)
    {
        save_errno = errno;
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_CACHE_USER_PROXY;
        gt3_failure_message = globus_common_create_string(
                "Error writing proxy file: %s: %s (%d)",
                filename,
                strerror(save_errno),
                save_errno);
    }
fopen_failed:
    if (rc != GLOBUS_SUCCESS)
    {
        free(filename);
        filename = NULL;
    }
malloc_filename_failed:
    gss_release_buffer(&minor_status, &buffer);
jm_restart_done:
export_cred_failed:
    if (request->gt3_failure_message == NULL)
    {
        request->gt3_failure_message = gt3_failure_message;
    }
    else
    {
        free(gt3_failure_message);
    }
no_cred:
    *proxy_filename = filename;

    return rc;
}
/* globus_l_gram_export_cred() */

/**
 * Create job directory
 * 
 * The job directory is used internally by the Job Manager to store various
 * pieces of job-specific data: stdout, stderr, proxy, and job scripts.
 *
 * @param request
 *     Request to process
 * @param job_directory
 *     Pointer to be set to the new value of the job directory.
 *
 * @retval GLOBUS_SUCCESS
 *     Success.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_ARG_FILE_CREATION_FAILED
 *     Error creating directory.
 */
static
int
globus_l_gram_make_job_dir(
    globus_gram_jobmanager_request_t *  request,
    char **                             job_directory)
{
    char *                              out_file = NULL;
    char *                              tmp;
    int                                 rc;
    struct stat                         statbuf;

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.make_job_dir.start "
            "level=TRACE "
            "gramid=%s "
            "\n",
            request->job_contact_path);

    out_file = globus_common_create_string(
                "%s/.globus/job/%s/%s",
                request->config->home,
                request->config->short_hostname,
                request->uniq_id);
    if (out_file == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        globus_gram_job_manager_request_log(
                request,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.make_job_dir.end "
                "level=ERROR "
                "gramid=%s "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                request->job_contact_path,
                -rc,
                "Error allocating path string",
                errno,
                strerror(errno));

        goto out;
    }

    if ((rc = stat(out_file, &statbuf)) < 0)
    {
        tmp = out_file;

        while (tmp != NULL)
        {
            tmp = strchr(tmp+1, '/');
            if (tmp != out_file)
            {
                if (tmp != NULL)
                {
                    *tmp = '\0';
                }
                if ((rc = stat(out_file, &statbuf)) < 0)
                {
                    /* Path component does not exist, try to make it */

                    errno = 0;
                    rc = mkdir(out_file, S_IRWXU);
                    if (rc == -1 && errno != EEXIST)
                    {
                        /* Error creating directory */
                        if (request->gt3_failure_message == NULL)
                        {
                            request->gt3_failure_message =
                                    globus_common_create_string(
                                        "mkdir failed: %s: %s",
                                        out_file,
                                        strerror(errno));
                        }
                    }
                }
                if ((rc = stat(out_file, &statbuf)) < 0)
                {
                    int save_errno = errno;

                    rc = GLOBUS_GRAM_PROTOCOL_ERROR_ARG_FILE_CREATION_FAILED;

                    if (request->gt3_failure_message == NULL)
                    {
                        request->gt3_failure_message =
                                globus_common_create_string(
                                        "stat failed: %s: %s",
                                        out_file,
                                        strerror(save_errno));
                    }

                    globus_gram_job_manager_request_log(
                            request,
                            GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                            "event=gram.make_job_dir.end "
                            "level=ERROR "
                            "gramid=%s "
                            "status=%d "
                            "path=%s "
                            "msg=\"%s\" "
                            "errno=%d "
                            "reason=\"%s\" "
                            "\n",
                            request->job_contact_path,
                            -rc,
                            out_file,
                            "Error creating directory",
                            save_errno,
                            strerror(save_errno));

                    goto error_exit;
                }
                if (tmp != NULL)
                {
                    *tmp = '/';
                }
            }
        }
    }

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.make_job_dir.end "
            "level=TRACE "
            "gramid=%s "
            "status=0 "
            "path=%s "
            "\n",
            request->job_contact_path,
            out_file);

error_exit:
    if (rc != GLOBUS_SUCCESS)
    {
        free(out_file);
        out_file = NULL;
    }
out:
    *job_directory = out_file;
    return rc;
}
/* globus_l_gram_make_job_dir() */

static
void
globus_l_gram_event_destroy(void *datum)
{
    globus_scheduler_event_destroy(datum);
}
/* globus_l_gram_event_destroy() */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
