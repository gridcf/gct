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

#include "globus_common.h"
#include "globus_gram_job_manager.h"
#include "globus_gsi_system_config.h"
#include "globus_callout.h"
#include "globus_callout_constants.h"
#include "globus_gram_jobmanager_callout_error.h"

#include <string.h>

globus_logging_handle_t                 globus_i_gram_job_manager_log_stdio;
globus_logging_handle_t                 globus_i_gram_job_manager_log_sys;
static globus_symboltable_t             globus_l_gram_log_symboltable;
static FILE *                           globus_l_gram_log_fp = NULL;
globus_thread_key_t                     globus_i_gram_request_key;
static globus_bool_t                    globus_l_gram_reopen_log;

static
void
globus_l_gram_logging_close(
    void *                              user_arg);

static
void
globus_l_gram_logging_write(
    globus_byte_t *                     buf,
    globus_size_t                       length,
    void *                              user_arg);

globus_logging_module_t                 globus_l_gram_logging_module =
{
    NULL,
    globus_l_gram_logging_write,
    globus_l_gram_logging_close,
    NULL
};

int
globus_gram_job_manager_logging_init(
    globus_gram_job_manager_config_t *  config)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 rc;

    if (config->syslog_enabled)
    {
        result = globus_logging_init(
                &globus_i_gram_job_manager_log_sys,
                NULL,
                0,
                config->log_levels|GLOBUS_LOGGING_INLINE,
                &globus_logging_syslog_ng_module,
                NULL);

        if (result != GLOBUS_SUCCESS)
        {
            char * errstr = globus_error_print_friendly(
                    globus_error_peek(result));

            fprintf(stderr, "Error initializing logging: %s\n", errstr);
            exit(1);
        }
    }
    else
    {
        globus_i_gram_job_manager_log_sys = NULL;
    }

    rc = globus_symboltable_init(
            &globus_l_gram_log_symboltable,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);
    if (rc != GLOBUS_SUCCESS)
    {
        fprintf(stderr, "Error initializing logging: symboltable_init\n");
        exit(1);
    }

    rc = globus_symboltable_create_scope(&globus_l_gram_log_symboltable);
    if (rc != GLOBUS_SUCCESS)
    {
        fprintf(stderr, "Error initializing logging: create scope\n");
        exit(1);
    }

    rc = globus_i_gram_symbol_table_populate(
            config,
            &globus_l_gram_log_symboltable);
    if (rc != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
                "Error initializing logging: symboltable_populate\n");
        exit(1);
    }

    globus_l_gram_logging_module.header_func =
            globus_logging_stdio_ng_module.header_func;

    result = globus_logging_init(
            &globus_i_gram_job_manager_log_stdio,
            NULL,
            0,
            config->log_levels|GLOBUS_LOGGING_INLINE,
            &globus_l_gram_logging_module,
            (void *) config);

    if (result != GLOBUS_SUCCESS)
    {
        char * errstr = globus_error_print_friendly(
                globus_error_peek(result));

        fprintf(stderr, "Error initializing logging: %s\n", errstr);
        exit(1);
    }
    return result;
}
/* globus_gram_job_manager_logging_init() */

void
globus_gram_job_manager_logging_destroy(void)
{
    if (globus_i_gram_job_manager_log_sys)
    {
        globus_logging_destroy(globus_i_gram_job_manager_log_sys);
	globus_i_gram_job_manager_log_sys = NULL;
    }
    if (globus_i_gram_job_manager_log_stdio)
    {
        globus_logging_destroy(globus_i_gram_job_manager_log_stdio);
	globus_i_gram_job_manager_log_stdio = NULL;
    }
    globus_symboltable_destroy(
            &globus_l_gram_log_symboltable);
    globus_l_gram_log_symboltable = NULL;
}

char *
globus_gram_prepare_log_string(
    const char *                        instr)
{
    char *                              outstr;
    int                                 i = 0;
    if (instr == NULL)
    {
        return NULL;
    }
    outstr = malloc(2*strlen(instr) + 1);
    if (outstr == NULL)
    {
        return NULL;
    }

    while (*instr != 0)
    {
        if (*instr == '\n')
        {
            outstr[i++] = '\\';
            outstr[i++] = 'n';
            instr++;
        }
        else if (*instr == '\r')
        {
            outstr[i++] = '\\';
            outstr[i++] = 'r';
            instr++;
        }
        else if (*instr == '\\')
        {
            outstr[i++] = '\\';
            outstr[i++] = '\\';
            instr++;
        }
        else if (*instr == '"')
        {
            outstr[i++] = '\\';
            outstr[i++] = '"';
            instr++;
        }
        else
        {
            outstr[i++] = *(instr++);
        }
    }
    outstr[i++] = '\0';

    return outstr;
}
/* globus_gram_prepare_log_string() */

void
globus_i_job_manager_log_rotate(int sig)
{
    globus_l_gram_reopen_log = GLOBUS_TRUE;
}

static
void
globus_l_gram_logging_write(
    globus_byte_t *                     buf,
    globus_size_t                       length,
    void *                              user_arg)
{
    static char *                       DATE_SYMBOL = "DATE";
    globus_gram_job_manager_config_t *  config = user_arg;
    const char *                        log_pattern = NULL;
    time_t                              now;
    struct tm *                         now_tm;
    char                                now_str[9];
    char *                              path = NULL;
    static char *                       last_path = NULL;
    int                                 fd;
    int                                 flags;
    int                                 rc;
    globus_gram_jobmanager_request_t *  request;
    globus_symboltable_t *              symboltable;

    request = globus_thread_getspecific(globus_i_gram_request_key);
    if (request != NULL)
    {
        symboltable = &request->symbol_table;
        log_pattern = request->log_pattern;
    }
    else
    {
        symboltable = &globus_l_gram_log_symboltable;
    }

    if (log_pattern == NULL)
    {
        log_pattern = config->log_pattern;
    }

    if (log_pattern == NULL)
    {
        /* stdio logging was not enabled for the system or via the job rsl */
        return;
    }

    now = time(NULL);
    now_tm = gmtime(&now);

    snprintf(now_str, 9, "%04d%02d%02d",
            now_tm->tm_year + 1900,
            now_tm->tm_mon + 1,
            now_tm->tm_mday);


    /* Create a new scope for this so that job RSL variables which conflict with
     * DATE_SYMBOL won't get clobbered and leak
     */
    globus_symboltable_create_scope(symboltable);

    globus_symboltable_insert(
            symboltable,
            DATE_SYMBOL,
            now_str);
            
    globus_gram_job_manager_rsl_eval_string(
            symboltable,
            log_pattern,
            &path);

    globus_symboltable_remove_scope(
            symboltable);

    if (path == NULL)
    {
        /* Bad RSL Substitution? */
        return;
    }

    if (last_path == NULL ||
        globus_l_gram_reopen_log ||
        strcmp(path, last_path) != 0)
    {
        globus_l_gram_reopen_log = GLOBUS_FALSE;

        if (last_path)
        {
            free(last_path);
        }

        last_path = path;
        path = NULL;

        if (globus_l_gram_log_fp != NULL)
        {
            freopen(last_path, "a", globus_l_gram_log_fp);
        }
        else
        {
            globus_l_gram_log_fp = fopen(last_path, "a");
            if (globus_l_gram_log_fp)
            {
                setvbuf(globus_l_gram_log_fp, NULL, _IONBF, 0);
            }
        }

        if (globus_l_gram_log_fp)
        {
            fd = fileno(globus_l_gram_log_fp);
            flags = fcntl(fd, F_GETFD);
            if (flags >= 0)
            {
                flags |= FD_CLOEXEC;
                rc = fcntl(fd, F_SETFD, flags);
            }
        }
    }
    else
    {
        free(path);
        path = NULL;
    }

    if (globus_l_gram_log_fp)
    {
        fwrite(buf, length, 1, globus_l_gram_log_fp);
    }
}
/* globus_l_gram_logging_write() */

static
void
globus_l_gram_logging_close(
    void *                              user_arg)
{
    if (globus_l_gram_log_fp != NULL)
    {
        fclose(globus_l_gram_log_fp);
        globus_l_gram_log_fp = NULL;
    }
}
/* globus_l_gram_logging_close() */
