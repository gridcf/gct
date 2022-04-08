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
 * @file globus_gram_job_manager_staging.c
 * @brief GRAM Job Manager Staging Tracking
 */

#include "globus_gram_job_manager.h"
#include "globus_rsl_assist.h"

#include <string.h>

static
int
globus_l_gram_job_manager_staging_add_pair(
    globus_gram_jobmanager_request_t *  request,
    globus_rsl_value_t *                from,
    globus_rsl_value_t *                to,
    const char *                        type);

static
globus_bool_t
globus_l_gram_job_manager_staging_match(
    void *                              datum,
    void *                              arg);

static
int
globus_l_gram_staging_list_read_state(
    globus_gram_jobmanager_request_t *  request,
    FILE *                              fp,
    char *                              buffer,
    globus_gram_job_manager_staging_type_t
                                        staging_type,
    globus_list_t **                    staging_list);

static
void
globus_l_gram_staging_list_free(
    globus_list_t **                    staging_list);

static
int
globus_l_staging_replace_stream(
    globus_gram_jobmanager_request_t *  request,
    char *                              parameter,
    char *                              cached_destination);

#endif

int
globus_gram_job_manager_staging_create_list(
    globus_gram_jobmanager_request_t *  request)
{
    int                                 i;
    int                                 rc;
    globus_rsl_value_t *                from;
    globus_rsl_value_t *                to;
    globus_list_t *                     list;
    globus_list_t *                     pairs;
    char *                              can_stage_list[] =
    {
        GLOBUS_GRAM_PROTOCOL_FILE_STAGE_IN_PARAM,
        GLOBUS_GRAM_PROTOCOL_FILE_STAGE_IN_SHARED_PARAM,
        GLOBUS_GRAM_PROTOCOL_FILE_STAGE_OUT_PARAM,
        NULL
    };
    int                                 errors_list[] =
    {
        GLOBUS_GRAM_PROTOCOL_ERROR_RSL_FILE_STAGE_IN,
        GLOBUS_GRAM_PROTOCOL_ERROR_RSL_FILE_STAGE_IN_SHARED,
        GLOBUS_GRAM_PROTOCOL_ERROR_RSL_FILE_STAGE_OUT,
        0
    };

    if(request->jm_restart)
    {
        return GLOBUS_SUCCESS;
    }

    for(i = 0; can_stage_list[i] != NULL; i++)
    {
        list = globus_rsl_param_get_values(request->rsl, can_stage_list[i]);

        if(!list)
        {
            continue;
        }

        while(!globus_list_empty(list))
        {
            pairs = globus_rsl_value_sequence_get_value_list(
                    globus_list_first(list));
            list = globus_list_rest(list);

            if(globus_list_size(pairs) != 2)
            {
                rc = errors_list[i];
                goto failed_adding_exit;
            }

            from = globus_list_first(pairs);
            to = globus_list_first(globus_list_rest(pairs));

            rc = globus_l_gram_job_manager_staging_add_pair(
                    request,
                    from,
                    to,
                    can_stage_list[i]);

            if(rc != GLOBUS_SUCCESS)
            {
                goto failed_adding_exit;
                
            }
        }
    }

    rc = globus_gram_job_manager_streaming_list_replace(request);

failed_adding_exit:
    if (rc != GLOBUS_SUCCESS)
    {
        globus_gram_job_manager_staging_free_all(request);
    }
    return rc;
}
/* globus_gram_job_manager_staging_create_list() */

int
globus_gram_job_manager_streaming_list_replace(
    globus_gram_jobmanager_request_t *  request)
{
    globus_list_t *                     old_list;
    int                                 rc;

    /* We'll restore to the old list if this fails */
    old_list = request->stage_stream_todo;
    request->stage_stream_todo = NULL;

    rc = globus_l_staging_replace_stream(
            request,
            GLOBUS_GRAM_PROTOCOL_STDOUT_PARAM,
            request->cached_stdout);
    if (rc != GLOBUS_SUCCESS)
    {
        goto bad_stdout;
    }

    rc = globus_l_staging_replace_stream(
            request,
            GLOBUS_GRAM_PROTOCOL_STDERR_PARAM,
            request->cached_stderr);
    if (rc != GLOBUS_SUCCESS)
    {
        goto bad_stderr;
    }

    if (rc == GLOBUS_SUCCESS)
    {
        globus_l_gram_staging_list_free(&old_list);
        old_list = NULL;
    }
    else
    {
bad_stderr:
bad_stdout:
        globus_l_gram_staging_list_free(&request->stage_stream_todo);
        request->stage_stream_todo = old_list;
    }
    return rc;
}
/* globus_gram_job_manager_streaming_list_replace() */

static
int
globus_l_staging_replace_one_stream(
    globus_gram_jobmanager_request_t *  request,
    char *                              parameter,
    char *                              cached_destination,
    globus_list_t *                     list,
    globus_bool_t                       single)
{
    globus_rsl_value_t                  from_cached;
    globus_rsl_value_t                  *to = NULL;
    globus_rsl_value_t                  *tag = NULL;
    char                                *evaled_to = NULL;
    char                                *evaled_tag = NULL;
    char                                *fname = NULL;
    unsigned long                       timestamp = GLOBUS_GASS_CACHE_TIMESTAMP_UNKNOWN;
    int                                 rc = GLOBUS_SUCCESS;
    static const char                   gass_cache_scheme[] = "x-gass-cache://";

    from_cached.type = GLOBUS_RSL_VALUE_LITERAL;
    from_cached.value.literal.string = cached_destination;
    /*
     * First element of the list is the destination, the second is the
     * (optional) tag. Both (if present) must be something that 
     * evaluates to a string and not a sequence.
     */
    to = globus_list_first(list);
    list = globus_list_rest(list);
    if (!globus_list_empty(list))
    {
        tag = globus_list_first(list);
        list = globus_list_rest(list);
    }

    if (globus_rsl_value_is_sequence(to) || 
        ((tag != NULL) && globus_rsl_value_is_sequence(tag)) ||
        list != NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_STDOUT;

        goto bad_value;
    }

    rc = globus_gram_job_manager_rsl_evaluate_value(
            &request->symbol_table,
            to,
            &evaled_to);
    if (rc != GLOBUS_SUCCESS)
    {
        goto bad_value;
    }

    /* If it evaluates to a string, and is not an x-gass-cache URL,
     * then tag must be NULL
     */
    if (strncmp(evaled_to,
                gass_cache_scheme,
                sizeof(gass_cache_scheme)-1) != 0 &&
        tag != NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_STDOUT;
        free(evaled_to);
        goto bad_value;
    }

    if (tag != NULL)
    {
        /* If there's a tag, evaluate it and add it to the cache file
         * so that the file won't get erased when the job terminates
         */
        rc = globus_gram_job_manager_rsl_evaluate_value(
                &request->symbol_table,
                tag,
                &evaled_tag);
        if (rc != GLOBUS_SUCCESS)
        {
            goto bad_value;
        }
        rc = globus_gass_cache_add(
                request->cache_handle,
                evaled_to,
                evaled_tag,
                GLOBUS_TRUE,
                &timestamp,
                &fname);
        if (rc != GLOBUS_GASS_CACHE_ADD_NEW &&
            rc != GLOBUS_GASS_CACHE_ADD_EXISTS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDOUT;
            goto bad_value;
        }
        free(fname);
        rc = globus_gass_cache_add_done(
                request->cache_handle,
                evaled_to,
                evaled_tag,
                timestamp);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDOUT;
            goto bad_value;
        }
    }

    /* If there is more than one output destination, or this one is
     * a non-local destination, then add it to the streamout list
     */
    if ((!single) ||
            (strstr(evaled_to, "://") != NULL &&
             strncmp(evaled_to, gass_cache_scheme,
                    sizeof(gass_cache_scheme)-1) != 0))
    {
        rc = globus_l_gram_job_manager_staging_add_pair(
                request,
                &from_cached,
                to,
                "filestreamout");
        free(evaled_to);
        evaled_to = NULL;
        if (rc != GLOBUS_SUCCESS)
        {
            goto bad_value;
        }
    }
    else if (strstr(evaled_to, "://") == NULL)
    {
        /* If it's a local file, check that it is writable */
        int tmpfd;

        tmpfd = open(evaled_to, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
        if (tmpfd < 0)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDOUT;
            free(evaled_to);
            goto bad_value;
        }
        else
        {
            close(tmpfd);
        }
    }
    if (evaled_to)
    {
        free(evaled_to);
    }

    if (rc != GLOBUS_SUCCESS)
    {
bad_value:
        /* Normalize error types to match the RSL attribute that we are
         * processing
         */
        if (strcmp(parameter, GLOBUS_GRAM_PROTOCOL_STDERR_PARAM) == 0)
        {
            switch (rc)
            {
                case GLOBUS_GRAM_PROTOCOL_ERROR_RSL_STDOUT:
                    rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_STDERR;
                    break;
                case GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDOUT:
                    rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDERR;
                    break;
                default:
                    break;
            }
        }
    }
    return rc;
}
/* globus_l_staging_replace_one_stream() */

static
int
globus_l_staging_replace_stream(
    globus_gram_jobmanager_request_t *  request,
    char *                              parameter,
    char *                              cached_destination)
{
    globus_list_t *                     list;
    globus_rsl_value_t                  *to;
    globus_bool_t                       single;
    int                                 rc = GLOBUS_SUCCESS;

    list = globus_rsl_param_get_values(
            request->rsl,
            parameter);

    if (list == NULL)
    {
        /* Attempting to replace something that was never in the job
         * RSL---too bad
         */
        return GLOBUS_SUCCESS;
    }

    /* The stdout and stderr attributes can occur in two forms:
     * - stdout = destination [tag]
     * - stdout = (destination [tag])+
     * That is, either as a sequence of 1 or 2 values, or as a sequence of
     * sequences.
     *
     * In either form, if there is only one destination, and it's a local file
     * or x-gass-cache URL, we can safely write directly to that file and don't
     * need it to be staged after the job completes. Otherwise, we'll have to
     * write to the stdout (stderr) file in the job directory and copy it
     * during the STAGE_OUT state.
     */
    if (! globus_rsl_value_is_sequence(globus_list_first(list)))
    {
        rc = globus_l_staging_replace_one_stream(
                request,
                parameter,
                cached_destination,
                list,
                GLOBUS_TRUE);
    }
    else
    {
        single = (globus_list_size(list) == 1);

        while (!globus_list_empty(list))
        {
            globus_list_t                   *sequence_list;

            to = globus_list_first(list);
            list = globus_list_rest(list);

            if (!globus_rsl_value_is_sequence(to))
            {
                /* Bare value instead of a sequence */
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_STDOUT;
                goto bad_value;
            }

            sequence_list = globus_rsl_value_sequence_get_value_list(to);

            rc = globus_l_staging_replace_one_stream(
                    request,
                    parameter,
                    cached_destination,
                    sequence_list,
                    single);

            if (rc != GLOBUS_SUCCESS)
            {
                goto bad_value;
            }

        }
    }

    if (rc != GLOBUS_SUCCESS)
    {
bad_value:
        /* Normalize error types to match the RSL attribute that we are
         * processing
         */
        if (strcmp(parameter, GLOBUS_GRAM_PROTOCOL_STDERR_PARAM) == 0)
        {
            switch (rc)
            {
                case GLOBUS_GRAM_PROTOCOL_ERROR_RSL_STDOUT:
                    rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_STDERR;
                    break;
                case GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDOUT:
                    rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDERR;
                    break;
                default:
                    break;
            }
        }
    }
    return rc;
}
/* globus_gram_job_manager_staging_create_list() */


int
globus_gram_job_manager_staging_remove(
    globus_gram_jobmanager_request_t *  request,
    globus_gram_job_manager_staging_type_t
                                        type,
    char *                              from,
    char *                              to)
{
    globus_gram_job_manager_staging_info_t 
                                        query;
    globus_gram_job_manager_staging_info_t *
                                        item;
    globus_list_t **                    list = NULL;
    globus_list_t *                     node;
    const char *                        typestr = "";

    switch(type)
    {
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_IN:
          typestr = "file_stage_in";
          break;
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_IN_SHARED:
          typestr = "file_stage_in_shared";
          break;
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_OUT:
          typestr = "file_stage_out";
          break;
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_STREAMS:
          typestr = "file_stream_out";
          break;
    }

    globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.staging_remove.start "
            "level=DEBUG "
            "gramid=%s "
            "src=\"%s\" "
            "dst=\"%s\" "
            "type=%s "
            "\n",
            request->job_contact_path,
            from,
            to,
            typestr);

    query.evaled_from = from;
    query.evaled_to = to;
    query.type = type;

    switch(type)
    {
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_IN:
        list = &request->stage_in_todo;
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_IN_SHARED:
        list = &request->stage_in_shared_todo;
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_OUT:
        list = &request->stage_out_todo;
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_STREAMS:
        list = &request->stage_stream_todo;
        break;
    }

    node = globus_list_search_pred(
            *list,
            globus_l_gram_job_manager_staging_match,
            &query);

    if(node)
    {
        item = globus_list_remove(list, node);

        globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.staging_remove.end "
            "level=TRACE "
            "gramid=%s "
            "msg=\"%s\" "
            "src=\"%s\" "
            "dst=\"%s\" "
            "type=%s "
            "status=%d "
            "\n",
            request->job_contact_path,
            "File staged",
            from,
            to,
            typestr,
            0);

        if (item->type == GLOBUS_GRAM_JOB_MANAGER_STAGE_IN)
        {
            if (strncmp(item->evaled_from, "http://", 7) == 0)
            {
                request->job_stats.file_stage_in_http_count++;
            }
            else if (strncmp(item->evaled_from, "https://", 8) == 0)
            {
                request->job_stats.file_stage_in_https_count++;
            }
            else if (strncmp(item->evaled_from, "ftp://", 6) == 0)
            {
                request->job_stats.file_stage_in_ftp_count++;
            }
            else if (strncmp(item->evaled_from, "gsiftp://", 6) == 0)
            {
                request->job_stats.file_stage_in_gsiftp_count++;
            }
        }
        else if (item->type == GLOBUS_GRAM_JOB_MANAGER_STAGE_IN_SHARED)
        {
            if (strncmp(item->evaled_from, "http://", 7) == 0)
            {
                request->job_stats.file_stage_in_shared_http_count++;
            }
            else if (strncmp(item->evaled_from, "https://", 8) == 0)
            {
                request->job_stats.file_stage_in_shared_https_count++;
            }
            else if (strncmp(item->evaled_from, "ftp://", 6) == 0)
            {
                request->job_stats.file_stage_in_shared_ftp_count++;
            }
            else if (strncmp(item->evaled_from, "gsiftp://", 6) == 0)
            {
                request->job_stats.file_stage_in_shared_gsiftp_count++;
            }
        }
        else if (item->type == GLOBUS_GRAM_JOB_MANAGER_STAGE_OUT ||
                 item->type == GLOBUS_GRAM_JOB_MANAGER_STAGE_STREAMS)
        {
            if (strncmp(item->evaled_to, "http://", 7) == 0)
            {
                request->job_stats.file_stage_out_http_count++;
            }
            else if (strncmp(item->evaled_to, "https://", 8) == 0)
            {
                request->job_stats.file_stage_out_https_count++;
            }
            else if (strncmp(item->evaled_to, "ftp://", 6) == 0)
            {
                request->job_stats.file_stage_out_ftp_count++;
            }
            else if (strncmp(item->evaled_to, "gsiftp://", 6) == 0)
            {
                request->job_stats.file_stage_out_gsiftp_count++;
            }
        }
        globus_rsl_value_free_recursive(item->from);
        globus_rsl_value_free_recursive(item->to);
        free(item->evaled_from);
        free(item->evaled_to);
        free(item);
    }
    else
    {
        globus_gram_job_manager_request_log(
            request,
            GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
            "event=gram.staging_remove.end "
            "level=WARN "
            "gramid=%s "
            "msg=\"%s\" "
            "src=\"%s\" "
            "dst=\"%s\" "
            "type=%s "
            "status=%d "
            "msg=\"%s\" "
            "\n",
            request->job_contact_path,
            "File staged",
            from,
            to,
            typestr,
            0,
            "Unexpected staging completion");
    }
    return GLOBUS_SUCCESS;
}
/* globus_gram_job_manager_staging_remove() */

int
globus_gram_job_manager_staging_write_state(
    globus_gram_jobmanager_request_t *  request,
    FILE *                              fp)
{
    globus_list_t *                     tmp_list;
    globus_gram_job_manager_staging_info_t *
                                        info;
    char *                              tmp_str;
    int                                 rc;

    rc = fprintf(fp, "%d\n", globus_list_size(request->stage_in_todo));

    if (rc < 0)
    {
        return GLOBUS_FAILURE;
    }

    tmp_list = request->stage_in_todo;
    while(!globus_list_empty(tmp_list))
    {
        info = globus_list_first(tmp_list);
        tmp_list = globus_list_rest(tmp_list);

        tmp_str = globus_rsl_value_unparse(info->from);
        rc = fprintf(fp, "%s\n", tmp_str);
        free(tmp_str);
        if (rc < 0)
        {
            return GLOBUS_FAILURE;
        }

        tmp_str = globus_rsl_value_unparse(info->to);
        rc = fprintf(fp, "%s\n", tmp_str);
        free(tmp_str);
        if (rc < 0)
        {
            return GLOBUS_FAILURE;
        }
    }
    rc = fprintf(fp, "%d\n", globus_list_size(request->stage_in_shared_todo));
    if (rc < 0)
    {
        return GLOBUS_FAILURE;
    }
    tmp_list = request->stage_in_shared_todo;
    while(!globus_list_empty(tmp_list))
    {
        info = globus_list_first(tmp_list);
        tmp_list = globus_list_rest(tmp_list);

        tmp_str = globus_rsl_value_unparse(info->from);
        rc = fprintf(fp, "%s\n", tmp_str);
        free(tmp_str);
        if (rc < 0)
        {
            return GLOBUS_FAILURE;
        }

        tmp_str = globus_rsl_value_unparse(info->to);
        rc = fprintf(fp, "%s\n", tmp_str);
        free(tmp_str);
        if (rc < 0)
        {
            return GLOBUS_FAILURE;
        }
    }
    rc = fprintf(fp, "%d\n", globus_list_size(request->stage_out_todo));
    if (rc < 0)
    {
        return GLOBUS_FAILURE;
    }
    tmp_list = request->stage_out_todo;
    while(!globus_list_empty(tmp_list))
    {
        info = globus_list_first(tmp_list);
        tmp_list = globus_list_rest(tmp_list);

        tmp_str = globus_rsl_value_unparse(info->from);
        rc = fprintf(fp, "%s\n", tmp_str);
        free(tmp_str);

        if (rc < 0)
        {
            return GLOBUS_FAILURE;
        }

        tmp_str = globus_rsl_value_unparse(info->to);
        rc = fprintf(fp, "%s\n", tmp_str);
        free(tmp_str);
        if (rc < 0)
        {
            return GLOBUS_FAILURE;
        }
    }
    rc = fprintf(fp, "%d\n", globus_list_size(request->stage_stream_todo));
    tmp_list = request->stage_stream_todo;
    while(!globus_list_empty(tmp_list))
    {
        info = globus_list_first(tmp_list);
        tmp_list = globus_list_rest(tmp_list);

        tmp_str = globus_rsl_value_unparse(info->from);
        rc = fprintf(fp, "%s\n", tmp_str);
        free(tmp_str);

        if (rc < 0)
        {
            return GLOBUS_FAILURE;
        }

        tmp_str = globus_rsl_value_unparse(info->to);
        rc = fprintf(fp, "%s\n", tmp_str);
        free(tmp_str);
        if (rc < 0)
        {
            return GLOBUS_FAILURE;
        }
    }
    return GLOBUS_SUCCESS;
}
/* globus_gram_job_manager_staging_write_state() */

int
globus_gram_job_manager_staging_read_state(
    globus_gram_jobmanager_request_t *  request,
    FILE *                              fp)
{
    int                                 rc = GLOBUS_SUCCESS;
    char *                              buffer;
    size_t                              buffer_len;
    long                                offset;

    offset = ftell(fp);
    if (fseek(fp, 0, SEEK_END) < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_READING_STATE_FILE;

        goto out;
    }

    buffer_len = ftell(fp) - offset;

    if (fseek(fp, offset, SEEK_SET) < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_READING_STATE_FILE;

        goto out;
    }

    buffer = malloc(buffer_len+1);
    if (buffer == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto out;
    }

    rc = globus_l_gram_staging_list_read_state(
        request,
        fp,
        buffer,
        GLOBUS_GRAM_JOB_MANAGER_STAGE_IN,
        &request->stage_in_todo);

    if (rc != GLOBUS_SUCCESS)
    {
        goto free_buffer_out;
    }

    rc = globus_l_gram_staging_list_read_state(
        request,
        fp,
        buffer,
        GLOBUS_GRAM_JOB_MANAGER_STAGE_IN_SHARED,
        &request->stage_in_shared_todo);

    if (rc != GLOBUS_SUCCESS)
    {
        goto free_buffer_out;
    }

    rc = globus_l_gram_staging_list_read_state(
        request,
        fp,
        buffer,
        GLOBUS_GRAM_JOB_MANAGER_STAGE_OUT,
        &request->stage_out_todo);

    if (rc != GLOBUS_SUCCESS)
    {
        goto free_buffer_out;
    }


    rc = globus_l_gram_staging_list_read_state(
        request,
        fp,
        buffer,
        GLOBUS_GRAM_JOB_MANAGER_STAGE_STREAMS,
        &request->stage_stream_todo);

free_buffer_out:
    free(buffer);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_gram_job_manager_staging_free_all(request);
    }
out:
    return rc;
}
/* globus_gram_job_manager_staging_read_state() */

static
int
globus_l_gram_job_manager_staging_add_pair(
    globus_gram_jobmanager_request_t *  request,
    globus_rsl_value_t *                from,
    globus_rsl_value_t *                to,
    const char *                        type)
{
    int                                 rc;
    globus_gram_job_manager_staging_info_t *
                                        info;

    info = calloc(
            1,
            sizeof(globus_gram_job_manager_staging_info_t));
    if(!info)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto info_calloc_failed;
    }

    info->from = globus_rsl_value_copy_recursive(from);
    info->to = globus_rsl_value_copy_recursive(to);

    if(strcmp(type, GLOBUS_GRAM_PROTOCOL_FILE_STAGE_IN_PARAM) == 0)
    {
        info->type = GLOBUS_GRAM_JOB_MANAGER_STAGE_IN;
    }
    else if(strcmp(type, GLOBUS_GRAM_PROTOCOL_FILE_STAGE_IN_SHARED_PARAM)== 0)
    {
        info->type = GLOBUS_GRAM_JOB_MANAGER_STAGE_IN_SHARED;

    }
    else if(strcmp(type, GLOBUS_GRAM_PROTOCOL_FILE_STAGE_OUT_PARAM) == 0)
    {
        info->type = GLOBUS_GRAM_JOB_MANAGER_STAGE_OUT;
    }
    else if (strcmp(type, "filestreamout") == 0)
    {
        info->type = GLOBUS_GRAM_JOB_MANAGER_STAGE_STREAMS;
    }

    rc = globus_gram_job_manager_rsl_evaluate_value(
            &request->symbol_table,
            info->from,
            &info->evaled_from);

    if(!info->evaled_from)
    {
        if(rc == GLOBUS_SUCCESS)
        {
            /* Not a literal after a successful eval */
            switch(info->type)
            {
              case GLOBUS_GRAM_JOB_MANAGER_STAGE_IN:
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_FILE_STAGE_IN;
                break;
              case GLOBUS_GRAM_JOB_MANAGER_STAGE_IN_SHARED:
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_FILE_STAGE_IN_SHARED;
                break;
              case GLOBUS_GRAM_JOB_MANAGER_STAGE_OUT:
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_FILE_STAGE_OUT;
                break;
              case GLOBUS_GRAM_JOB_MANAGER_STAGE_STREAMS:
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_STDOUT;
                break;
            }
        }

        goto eval_from_failed;
    }
    rc = globus_gram_job_manager_rsl_evaluate_value(
            &request->symbol_table,
            info->to,
            &info->evaled_to);

    if(!info->evaled_to)
    {
        if(rc == GLOBUS_SUCCESS)
        {
            /* Not a literal after a successful eval */
            switch(info->type)
            {
              case GLOBUS_GRAM_JOB_MANAGER_STAGE_IN:
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_FILE_STAGE_IN;
                break;
              case GLOBUS_GRAM_JOB_MANAGER_STAGE_IN_SHARED:
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_FILE_STAGE_IN_SHARED;
                break;
              case GLOBUS_GRAM_JOB_MANAGER_STAGE_OUT:
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_FILE_STAGE_OUT;
                break;
              case GLOBUS_GRAM_JOB_MANAGER_STAGE_STREAMS:
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_STDOUT;
                break;
            }
        }

        goto eval_to_failed;
    }

    switch(info->type)
    {
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_IN:
        globus_list_insert(&request->stage_in_todo, info);
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_IN_SHARED:
        globus_list_insert(&request->stage_in_shared_todo, info);
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_OUT:
        globus_list_insert(&request->stage_out_todo, info);
        break;
      case GLOBUS_GRAM_JOB_MANAGER_STAGE_STREAMS:
        if (strcmp(info->evaled_to, "/dev/null") == 0)
        {
            globus_rsl_value_free_recursive(info->from);
            globus_rsl_value_free_recursive(info->to);
            free(info->evaled_from);
            free(info->evaled_to);
            free(info);
        }
        else
        {
            globus_list_insert(&request->stage_stream_todo, info);
        }
        break;
    }

    return GLOBUS_SUCCESS;

eval_to_failed:
    free(info->evaled_from);
eval_from_failed:
    free(info);
info_calloc_failed:
    return rc;
}
/* globus_l_gram_job_manager_staging_add_url() */

static
globus_bool_t
globus_l_gram_job_manager_staging_match(
    void *                              datum,
    void *                              arg)
{
    globus_gram_job_manager_staging_info_t *
                                        item;
    globus_gram_job_manager_staging_info_t *
                                        query;

    item = datum;
    query = arg;

    globus_assert(item->type == query->type);

    if((strcmp(item->evaled_from, query->evaled_from) == 0) &&
       (strcmp(item->evaled_to, query->evaled_to) == 0))
    {
        return GLOBUS_TRUE;
    }
    else
    {
        return GLOBUS_FALSE;
    }
}
/* globus_l_gram_job_manager_staging_match() */

static
void
globus_l_gram_staging_list_free(
    globus_list_t **                    staging_list)
{
    globus_gram_job_manager_staging_info_t *
                                        info;
    while (!globus_list_empty(*staging_list))
    {
        info = globus_list_remove(staging_list, *staging_list);
        globus_rsl_value_free_recursive(info->from);
        globus_rsl_value_free_recursive(info->to);
        free(info->evaled_from);
        free(info->evaled_to);
        free(info);
    }
}

void
globus_gram_job_manager_staging_free_all(
    globus_gram_jobmanager_request_t *  request)
{
    globus_l_gram_staging_list_free(&request->stage_in_todo);
    globus_l_gram_staging_list_free(&request->stage_in_shared_todo);
    globus_l_gram_staging_list_free(&request->stage_out_todo);
    globus_l_gram_staging_list_free(&request->stage_stream_todo);
}
/* globus_gram_job_manager_staging_free_all() */

/**
 * Read a list of staging pairs from the state file
 *
 * @param request
 *     Job request associated with the state file (used for
 *     RSL evaluation)
 * @param fp
 *     State file opened for reading
 * @param buffer
 *     Buffer containing the state file data
 * @param staging_type
 *     Type of staging list to read
 * @param staging_list
 *     List to insert the staging work into.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_READING_STATE_FILE
 *     Error reading state file
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 */
static
int
globus_l_gram_staging_list_read_state(
    globus_gram_jobmanager_request_t *  request,
    FILE *                              fp,
    char *                              buffer,
    globus_gram_job_manager_staging_type_t
                                        staging_type,
    globus_list_t **                    staging_list)
{
    int                                 rc = GLOBUS_SUCCESS;
    int                                 i, tmp_list_size;
    globus_gram_job_manager_staging_info_t *
                                        info;

    if (fscanf(fp, "%[^\n]%*c", buffer) < 1)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_READING_STATE_FILE;

        goto out;
    }
    tmp_list_size = atoi(buffer);

    for(i = 0; i < tmp_list_size; i++)
    {
        info = calloc(
                1,
                sizeof(globus_gram_job_manager_staging_info_t));
        if (info == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto out;
        }

        info->type = staging_type;

        if(fscanf(fp, "%[^\n]%*c", buffer) < 1)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_READING_STATE_FILE;

            goto free_info_out;
        }
        rc = globus_gram_job_manager_rsl_parse_value(
                buffer, &info->from);
        if (rc != GLOBUS_SUCCESS)
        {
            goto free_info_out;
        }

        if(fscanf(fp, "%[^\n]%*c", buffer) < 1)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_READING_STATE_FILE;

            goto free_info_from_out;
        }
        rc = globus_gram_job_manager_rsl_parse_value(
                buffer, &info->to);
        if (rc != GLOBUS_SUCCESS)
        {
            goto free_info_from_out;
        }

        rc = globus_gram_job_manager_rsl_evaluate_value(
                &request->symbol_table,
                info->from,
                &info->evaled_from);

        if (rc != GLOBUS_SUCCESS)
        {
            goto free_info_to_out;
        }

        rc = globus_gram_job_manager_rsl_evaluate_value(
                &request->symbol_table,
                info->to,
                &info->evaled_to);
        if (rc != GLOBUS_SUCCESS)
        {
            goto free_info_evaled_from_out;
        }

        globus_list_insert(staging_list, info);
    }

    if (rc != GLOBUS_SUCCESS)
    {
free_info_evaled_from_out:
        free(info->evaled_from);
free_info_to_out:
        free(info->to);
free_info_from_out:
        free(info->from);
free_info_out:
        free(info);
    }
out:
    return rc;
}
/* globus_l_gram_staging_list_read_state() */
