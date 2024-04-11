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
#include "globus_xio.h"
#include "globus_xio_file_driver.h"
#include "globus_gram_protocol.h"

/* waitpid-related headers */
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>

enum 
{
    GLOBUS_FORK_TASK_BUF_SIZE = 512
};

typedef struct
{
    char * task;
    char * tag;
    pid_t * pids;
    size_t tasklen;
    size_t taskbuflen;
    size_t pidcount;
    char * jobid_prefix;

    int count;

    char * directory;
    char ** environment;
    size_t environment_count;

    char * executable;
    char ** arguments;
    size_t argument_count;

    char * stdin_path;
    char ** stdout_path;
    size_t stdout_count;
    char ** stderr_path;
    size_t stderr_count;
}
globus_l_fork_task_t, *globus_fork_task_t;

/* Jobs which are running currently */
globus_hashtable_t globus_l_fork_active_tasks;
globus_bool_t globus_l_fork_stdin_closed = GLOBUS_FALSE;

globus_mutex_t globus_l_fork_lock;
globus_cond_t globus_l_fork_cond;

globus_bool_t globus_l_fork_signalled = GLOBUS_FALSE;
static char * globus_l_fork_logfile_path = NULL;

/* Callbacks */
static
void
globus_l_fork_sigchild_handler(void * arg);

static
void
globus_l_fork_task_read(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

static
void
globus_l_fork_close_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg);

/* Utilities */
static
int
globus_l_fork_task_init(
    globus_fork_task_t *                task);

static
void
globus_l_fork_task_free(
    globus_fork_task_t                  task);

static
void
globus_l_fork_task_increase_buffer(
    globus_fork_task_t                  task);

static
void
globus_l_fork_split_string(
    char *                              value,
    char ***                            result_ptr,
    size_t *                            result_cnt);

static
globus_result_t
globus_l_fork_unescape(
    char *                              value);

static
globus_result_t
globus_l_fork_parse_task(
    globus_fork_task_t                  task,
    globus_fork_task_t *                new_task);

static
int
globus_l_fork_start_task(
    globus_fork_task_t                  task);

static
void
globus_l_fork_log_state_change(
    pid_t                               pid,
    globus_fork_task_t                  task,
    globus_gram_protocol_job_state_t    job_state,
    int                                 exit_code);

static
globus_result_t
globus_l_fork_set_attribute(
    globus_fork_task_t                  task,
    char *                              attr,
    char *                              value);

static
void
globus_l_fork_error(
    globus_fork_task_t                  task,
    int                                 error,
    const char *                        fmt,
    ...);

static
int
globus_l_fork_log_open_and_lock(globus_fork_task_t task);

/* Main */
int main(int argc, char *argv[])
{
    globus_result_t                     result;
    int                                 rc;
    globus_xio_handle_t                 stdin_handle;
    globus_fork_task_t                  new_task;
    pid_t                               pid;
    int                                 status;
    globus_xio_driver_t                 file_driver;
    globus_xio_stack_t                  file_stack;
    char *                              errstr;
    int                                 i;

    rc = globus_module_activate(GLOBUS_COMMON_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_l_fork_error(
            NULL,
            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
            "Unable to activate GLOBUS_COMMON_MODULE");

        rc = 1;
        goto out;
    }

    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_l_fork_error(
            NULL,
            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
            "Unable to activate GLOBUS_XIO_MODULE");
        rc = 1;
        goto deactivate_common_out;
    }
    
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "-help") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-usage") == 0 ||
            strcmp(argv[i], "--usage") == 0)
        {
            printf("Usage: globus-fork-starter [LOG-PATH]\n");
            exit(EXIT_SUCCESS);
        }
        else if (argv[i][0] == '-')
        {
            printf("Unknown option: %s\n", argv[1]);
            exit(EXIT_FAILURE);
        }
        else if (access(argv[1], W_OK) != 0)
        {
            int save_errno = errno;
            printf("Unable to write to fork log file %s (%s)\n", argv[1],
                    strerror(save_errno));
            exit(EXIT_FAILURE);
        }
        else if (globus_l_fork_logfile_path == NULL)
        {
            globus_l_fork_logfile_path = strdup(argv[i]);
        }
        else
        {
            fprintf(stderr, "Unexpected command-line string %s\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }
    if (globus_l_fork_logfile_path == NULL)
    {
        char *confpath = NULL;

        result = globus_eval_path(
            "${sysconfdir}/globus/globus-fork.conf",
            &confpath);
        if (result != GLOBUS_SUCCESS || confpath == NULL)
        {
            errstr = globus_error_print_friendly(globus_error_peek(result));
            globus_l_fork_error(
                NULL,
                GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
                "Error determining log_path: %s", errstr);

            exit(EXIT_FAILURE);
        }
        result = globus_common_get_attribute_from_config_file(
                "", confpath, "log_path",
                &globus_l_fork_logfile_path);

        if (result != GLOBUS_SUCCESS)
        {
            errstr = globus_error_print_friendly(globus_error_peek(result));
            globus_l_fork_error(
                NULL,
                GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
                "Error determining log_path: %s",
                errstr);
            free(errstr);
            exit(1);
        }
        else if (globus_l_fork_logfile_path == NULL)
        {
            globus_l_fork_error(
                NULL,
                GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
                "Unable to read log_path attribute from $GLOBUS_LOCATION/"
                "etc/globus-fork.conf\n");
            exit(1);
        }
        free(confpath);
    }

    rc = globus_l_fork_log_open_and_lock(NULL);
    if (rc < 0)
    {
        exit(1);
    }
    close(rc);

    result = globus_xio_driver_load("file", &file_driver);
    if (result != GLOBUS_SUCCESS)
    {
        errstr = globus_error_print_friendly(globus_error_peek(result));
        globus_l_fork_error(
            NULL,
            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
            "Unable to load xio file driver: %s\n",
            errstr);
        free(errstr);

        rc = 2;

        goto deactivate_xio_out;
    }
    result = globus_xio_stack_init(&file_stack, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        errstr = globus_error_print_friendly(globus_error_peek(result));
        globus_l_fork_error(
            NULL,
            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
            "Unable to initialize xio stack: %s\n",
            errstr);
        free(errstr);

        rc = 2;
        goto unload_file_driver_out;
    }

    result = globus_xio_stack_push_driver(file_stack, file_driver);
    if (result != GLOBUS_SUCCESS)
    {
        errstr = globus_error_print_friendly(globus_error_peek(result));
        globus_l_fork_error(
            NULL,
            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
            "Unable to push file driver onto xio stack: %s\n",
            errstr);
        free(errstr);

        rc = 2;
        goto destroy_stack_out;
    }
    result = globus_xio_handle_create(&stdin_handle, file_stack);
    if (result != GLOBUS_SUCCESS)
    {
        errstr = globus_error_print_friendly(globus_error_peek(result));
        globus_l_fork_error(
            NULL,
            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
            "Unable to create xio handle: %s\n",
            errstr);
        free(errstr);

        rc = 2;
        goto destroy_stack_out;
    }
    result = globus_xio_open(stdin_handle, "stdin://", NULL);
    if (result != GLOBUS_SUCCESS)
    {
        errstr = globus_error_print_friendly(globus_error_peek(result));
        globus_l_fork_error(
            NULL,
            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
            "Unable to convert stdin to xio handle: %s\n",
            errstr);
        free(errstr);

        rc = 2;
        goto destroy_stdin_handle_out;
    }

    errno = 0;
    rc = globus_mutex_init(&globus_l_fork_lock, NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_l_fork_error(
            NULL,
            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
            "Unable to create lock: %s\n",
            strerror(errno));
        rc = 3;
        goto close_stdin_handle_out;
    }

    errno = 0;
    rc = globus_cond_init(&globus_l_fork_cond, NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_l_fork_error(
            NULL,
            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
            "Unable to create cond: %s\n",
            strerror(errno));
        rc = 3;
        goto destroy_mutex_out;
    }
    errno = 0;
    rc = setpgid(0, 0);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_l_fork_error(
            NULL,
            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
            "Unable to create process group: %s\n",
            strerror(errno));
        rc = 3;

        goto destroy_cond_out;
    }

    globus_hashtable_init(&globus_l_fork_active_tasks,
            16,
            globus_hashtable_int_hash,
            globus_hashtable_int_keyeq);

    globus_mutex_lock(&globus_l_fork_lock);

    result = globus_callback_register_signal_handler(
        SIGCHLD,
        GLOBUS_TRUE,
        globus_l_fork_sigchild_handler,
        NULL);

    if (result != GLOBUS_SUCCESS)
    {
        errstr = globus_error_print_friendly(globus_error_peek(result));
        globus_l_fork_error(
            NULL,
            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
            "Unable to register SIGCHILD handler: %s\n",
            errstr);
        free(errstr);
        rc = 4;
        goto unregister_handler_out;
    }

    result = globus_l_fork_task_init(&new_task);

    if (result != GLOBUS_SUCCESS)
    {
        errstr = globus_error_print_friendly(globus_error_peek(result));
        globus_l_fork_error(
            NULL,
            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
            "Unable to allocate task structure: %s\n",
            errstr);
        rc = 5;
        goto unregister_handler_out;
    }

    result = globus_xio_register_read(
            stdin_handle,
            (globus_byte_t *) new_task->task,
            new_task->taskbuflen - 1,
            1,
            NULL,
            globus_l_fork_task_read,
            new_task);

    fclose(stderr); /* not used, save the FD */

    /*
     * Main loop ends when the parent closes stdin and all jobs are finished
     */
    while (! (globus_l_fork_stdin_closed &&
            globus_hashtable_empty(&globus_l_fork_active_tasks)))
    {
        /* Does this work with signal handlers? */
        globus_cond_wait(&globus_l_fork_cond, &globus_l_fork_lock);

        while (globus_l_fork_signalled)
        {
            globus_l_fork_signalled = GLOBUS_FALSE;

            do
            {
                /* Nonblocking peek at child pid list */
                pid = waitpid(-1, &status, WNOHANG);

                if (pid > 0)
                {
                    new_task = globus_hashtable_remove(
                        &globus_l_fork_active_tasks,
                        (void *) (intptr_t) pid);

                    /* Some process completed */
                    if (WIFEXITED(status) && new_task)
                    {
                        globus_l_fork_log_state_change(
                            pid,
                            new_task,
                            GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE,
                            WEXITSTATUS(status));
                    }
                    else if (new_task)
                    {
                        globus_l_fork_log_state_change(
                            pid,
                            new_task,
                            GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED,
                            0);
                    }

                    if (new_task != NULL)
                    {
                        new_task->pidcount--;
                        if (new_task->pidcount == 0)
                        {
                            /* Entire job is finished, free the task struct */
                            globus_l_fork_task_free(new_task);
                        }
                    }
                }
            } while (pid > 0);
        }
    }
    globus_mutex_unlock(&globus_l_fork_lock);

unregister_handler_out:
    result = globus_callback_unregister_signal_handler(
        SIGCHLD,
        NULL,
        NULL);
destroy_cond_out:
    globus_cond_destroy(&globus_l_fork_cond);
destroy_mutex_out:
    globus_mutex_destroy(&globus_l_fork_lock);
close_stdin_handle_out:
destroy_stdin_handle_out:
    /*globus_xio_close(stdin_handle, NULL);*/
destroy_stack_out:
    globus_xio_stack_destroy(file_stack);
unload_file_driver_out:
    globus_xio_driver_unload(file_driver);
deactivate_xio_out:
    globus_module_deactivate(GLOBUS_XIO_MODULE);
deactivate_common_out:
    globus_module_deactivate(GLOBUS_COMMON_MODULE);
out:
    return rc;
}
/* main() */

static
void
globus_l_fork_sigchild_handler(void * arg)
{
    /* Some child terminated, wake up main to do a wait */
    globus_mutex_lock(&globus_l_fork_lock);
    globus_l_fork_signalled = GLOBUS_TRUE;
    globus_cond_signal(&globus_l_fork_cond);
    globus_mutex_unlock(&globus_l_fork_lock);
}
/* globus_l_fork_sigchild_handler() */

static
void
globus_l_fork_task_read(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    char *                              p;
    globus_fork_task_t                  task;
    globus_fork_task_t                  new_task;
    globus_bool_t                       eof;
    int 				rc;

    task = user_arg;

    if ((eof = globus_xio_error_is_eof(result)) == GLOBUS_TRUE)
    {
        result = GLOBUS_SUCCESS;
    }

    task->tasklen += nbytes;

    /* Parse any task requests we have in our input */
    while ((task != NULL) && (p = memchr(buffer, '\n', nbytes)) != NULL)
    {
        /* Start any completed tasks we have read from stdin */
        *p = '\0';

        /* New task will point to a task struct containing any residual
         * information in its buffers
         */
        result = globus_l_fork_parse_task(task, &new_task);

        if (result != GLOBUS_SUCCESS)
        {
            /* Parse error for this task. */
            if (task->tag != NULL)
            {
                /* Maybe other errors handled here too? */
                fprintf(stdout,
                    "102;%s;%d;%s\n",
                    task->tag,
                    GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED,
                    globus_gram_protocol_error_string(
                        GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED));
                result = GLOBUS_SUCCESS;
                task->tag = NULL;
            }
            task = new_task;

            if (task == NULL)
            {
                goto error;
            }
            continue;
        }
        /*
         * When globus_l_fork_start_task() returns successfully, the task
         * structure is stored in the active task hashtable.
         * Otherwise, we must free it here.
         */
        rc = globus_l_fork_start_task(task);

        if (rc != GLOBUS_SUCCESS)
        {
            /* Maybe other errors handled here too? */
            fprintf(stdout,
                "102;%s;%d;%s\n",
                task->tag,
                rc,
                globus_gram_protocol_error_string(rc));

            globus_l_fork_task_free(task);
        }
        task = new_task;

        if (task == NULL)
        {
            goto error;
        }
        p = task->task;
        nbytes = task->tasklen;
        result = GLOBUS_SUCCESS;
        fflush(stdout);
    }
    
    if (!eof)
    {
        /* Will need to reregister the read */
        if (task->tasklen == task->taskbuflen)
        {
            globus_l_fork_task_increase_buffer(task);
        }

        if (task->tasklen != task->taskbuflen)
        {
            /* Room in the task buffer to read more data */
            result = globus_xio_register_read(
                    handle,
                    (globus_byte_t *) task->task + task->tasklen,
                    task->taskbuflen - task->tasklen,
                    1,
                    NULL,
                    globus_l_fork_task_read,
                    task);
        }
    }

    /* We hit eof, failed to resize, or failed to reregister the read.
     * Any residual data in the task is incomplete
     */
    if (eof || task->tasklen == task->taskbuflen || result != GLOBUS_SUCCESS)
    {
        /* Unable to read tag, ignore the task request */
        globus_l_fork_task_free(task);

error:
        result = globus_xio_register_close(
                handle,
                NULL,
                globus_l_fork_close_callback,
                NULL);

        if (result != GLOBUS_SUCCESS)
        {
            globus_l_fork_close_callback(handle, GLOBUS_SUCCESS, NULL);
        }
    }
}
/* globus_l_fork_task_read() */

static
void
globus_l_fork_close_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_mutex_lock(&globus_l_fork_lock);
    globus_l_fork_stdin_closed = GLOBUS_TRUE;
    globus_cond_signal(&globus_l_fork_cond);
    globus_mutex_unlock(&globus_l_fork_lock);
}
/* globus_l_fork_close_callback() */

static
globus_result_t
globus_l_fork_parse_task(
    globus_fork_task_t                  task,
    globus_fork_task_t *                new_task)
{
    char *                              p;
    char *                              q;
    char *                              attr;
    char *                              value;
    int                                 fieldno=0;
    size_t                              buflen;
    globus_result_t                     result;

    buflen = strlen(task->task) + 1;
    p = task->task;
    q = task->task;

    while (*p != '\0')
    {
        if (*p == ';')
        {
            *(p++) = '\0';

            switch (fieldno++)
            {
            case 0:
                if (strcmp(q, "100") != 0)
                {
                    goto bad_task;
                }
                break;
            case 1:
                task->tag = q;
                break;
            default:
                attr = q;
                value = NULL;

                while (*q != '\0')
                {
                    if (*q == '=')
                    {
                        *(q++) = '\0';
                        value = q;
                        break;
                    }
                    else if (*q == '\\')
                    {
                        q+=2;
                    }
                    else
                    {
                        q++;
                    }
                }

                if (*attr != '\0')
                {
                    if (value == NULL)
                    {
                        goto bad_task;
                    }

                    result = globus_l_fork_set_attribute(task, attr, value);

                    if (result != GLOBUS_SUCCESS)
                    {
                        goto bad_task;
                    }
                }
            }
            q = p;
        }
        else if (*p == '\\')
        {
            p+=2;
        }
        else
        {
            p++;
        }
    }

    attr = q;
    value = NULL;

    while (*q != '\0')
    {
        if (*q == '=')
        {
            *(q++) = '\0';
            value = q;
            break;
        }
        else if (*q == '\\')
        {
            q+=2;
        }
        else
        {
            q++;
        }
    }
    if (*attr != '\0')
    {
        if (value == NULL)
        {
            goto bad_task;
        }

        result = globus_l_fork_set_attribute(task, attr, value);
        if (result != GLOBUS_SUCCESS)
        {
            goto bad_task;
        }
    }

    result = globus_l_fork_task_init(new_task);

    if (result == GLOBUS_SUCCESS)
    {
        memcpy((*new_task)->task,
            task->task + buflen,
            task->tasklen - buflen);

        (*new_task)->tasklen = task->tasklen - buflen;
    }
    else
    {
        *new_task = NULL;
    }
    return result;


bad_task:
    /* Bad task request */
    memmove(task->task,
        task->task + buflen,
        task->tasklen - buflen);
    task->tasklen -= buflen;

    /* Zero-out all parts of the task except for the tag */
    task->directory = NULL;

    if (task->environment)
    {
        globus_libc_free(task->environment);
    }
    task->environment_count = 0;

    task->pidcount = 0;
    task->executable = NULL;

    if (task->arguments)
    {
        globus_libc_free(task->arguments);
    }
    task->stdin_path = NULL;
    task->stdout_path = NULL;
    task->stderr_path = NULL;

    *new_task = task;

    return globus_error_put(GLOBUS_ERROR_NO_INFO);
}
/* globus_l_fork_parse_task() */

static
globus_result_t
globus_l_fork_set_attribute(
    globus_fork_task_t                  task,
    char *                              attr,
    char *                              value)
{
    globus_result_t                     result;
    int                                 i;

    if (strcmp(attr, "directory") == 0)
    {
        task->directory = value;
    }
    else if (strcmp(attr, "environment") == 0)
    {
        globus_l_fork_split_string(
                value,
                &task->environment,
                &task->environment_count);
    }
    else if (strcmp(attr, "count") == 0)
    {
        result = globus_l_fork_unescape(value);

        if (result == GLOBUS_SUCCESS)
        {
            task->count = atoi(value);
        }
    }
    else if (strcmp(attr, "executable") == 0)
    {
        result = globus_l_fork_unescape(value);

        if (result == GLOBUS_SUCCESS)
        {
            task->executable = value;
        }
    }
    else if (strcmp(attr, "arguments") == 0)
    {
        globus_l_fork_split_string(
            value,
            &task->arguments,
            &task->argument_count);

        if (task->arguments)
        {
            char ** tmp;
            tmp = globus_libc_realloc(task->arguments,
                sizeof(char *) * (task->argument_count+1));

            if (tmp == NULL)
            {
                goto bad_task;
            }

            for (i = task->argument_count-1; i >= 0; i--)
            {
                if (tmp[i])
                {

                    result = globus_l_fork_unescape(tmp[i]);
                    /* result? */
                }
                tmp[i+1] = tmp[i];
            }

            task->arguments = tmp;
            /* task->arguments[0] will be set to executable
             * before execve is called
             */
        }
    }
    else if (strcmp(attr, "stdin") == 0)
    {
        result = globus_l_fork_unescape(value);

        if (result == GLOBUS_SUCCESS)
        {
            task->stdin_path = value;
        }
    }
    else if (strcmp(attr, "stdout") == 0)
    {
        globus_l_fork_split_string(
            value,
            &task->stdout_path,
            &task->stdout_count);

        for (i = 0; i < task->stdout_count; i++)
        {
            value = task->stdout_path[i];

            result = globus_l_fork_unescape(value);

            if (result == GLOBUS_SUCCESS)
            {
                task->stdout_path[i] = value;
            }
            else
            {
                task->stdout_path[i] = NULL;
            }
        }
    }
    else if (strcmp(attr, "stderr") == 0)
    {
        globus_l_fork_split_string(
            value,
            &task->stderr_path,
            &task->stderr_count);

        for (i = 0; i < task->stderr_count; i++)
        {
            value = task->stderr_path[i];

            result = globus_l_fork_unescape(value);

            if (result == GLOBUS_SUCCESS)
            {
                task->stderr_path[i] = value;
            }
            else
            {
                task->stderr_path[i] = NULL;
            }
        }
    }
    else
    {
bad_task:
        return globus_error_put(GLOBUS_ERROR_NO_INFO);
    }
    return GLOBUS_SUCCESS;
}
/* globus_l_fork_set_attribute() */

static
int
globus_l_fork_start_task(
    globus_fork_task_t                  task)
{
    pid_t                               pgid = 0;
    int                                 pipefds[2];
    int                                 x;
    int                                 i;
    int                                 rc;
    
    task->pidcount = 0;

    task->pids = globus_libc_calloc(task->count, sizeof(pid_t));

    if (task->pids == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto error;
    }

    /* task->std*_count arrays are 1 larger than needed for terminating
     * null
     */
    if (task->stdout_count - 1 != task->count)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDOUT;

        goto error;
    }
    else if (task->stderr_count - 1 != task->count)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDERR;

        goto error;
    }

    if (task->executable == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDERR;

        goto error;
    }

    if (task->arguments == NULL)
    {
        task->arguments = globus_libc_calloc(2, sizeof(char *));
    }

    /*
     * Fork processes, chdir, set pgid, exec("/bin/sudo", "-u", username,
     * args, env).
     *
     * If successful, stick in hashtable for all pids, otherwise, free task
     * and return an error
     */
    for (i = 0; i < task->count; i++)
    {
        rc = pipe(pipefds);
        rc = fcntl(pipefds[0], F_SETFD, 1);
        rc = fcntl(pipefds[1], F_SETFD, 1);

        task->pids[i] = globus_libc_fork();

        if (task->pids[i] < 0)
        {
            /* Fork error, kill other procs */
        }
        else if (task->pids[i] > 0)
        {
            /* Parent */
            close(pipefds[1]);

	    do {
		    rc = read(pipefds[0], &x, sizeof(int));
	    } while (rc == -1 && errno == EINTR);

            close(pipefds[0]);

            if (rc == 0)
            {
                /* started ok */
                if (i == 0)
                {
                    pgid = task->pids[0];
                }
                globus_hashtable_insert(
                        &globus_l_fork_active_tasks,
                        (void *) (intptr_t) task->pids[i],
                        task);
                task->pidcount++;
            }
            else if (rc == sizeof(int))
            {
                /* bad */
                rc = x;

                goto kill_procs;
            }
            else
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED;

                goto kill_procs;
            }
        }
        else
        {
            /* Child */
            close(pipefds[0]);
            close(0);
            close(1);
            close(2);

            rc = chdir(task->directory);

            if (rc != 0)
            {
                x = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_DIRECTORY;

                write(pipefds[1], &x, sizeof(int));

                _exit(x);
            }
            rc = open(task->stdin_path, O_RDONLY);
            if (rc != 0)
            {
                x = GLOBUS_GRAM_PROTOCOL_ERROR_STDIN_NOT_FOUND;

                write(pipefds[1], &x, sizeof(int));

                _exit(x);
            }
            rc = open(task->stdout_path[i], O_WRONLY|O_APPEND|O_CREAT, 0666);
            if (rc != 1)
            {
                x = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDOUT;

                write(pipefds[1], &x, sizeof(int));

                _exit(x);
            }
            rc = open(task->stderr_path[i], O_WRONLY|O_APPEND|O_CREAT, 0666);
            if (rc != 2)
            {
                x = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_STDERR;

                write(pipefds[1], &x, sizeof(int));

                _exit(x);
            }

            rc = setpgid(0, pgid);
            if (rc != 0)
            {
                x = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED;

                write(pipefds[1], &x, sizeof(int));

                _exit(x);
            }
            task->arguments[0] = task->executable;

            rc = execve(task->executable, task->arguments, task->environment);

            if (rc != 0)
            {
                x = GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED;

                write(pipefds[1], &x, sizeof(int));

                _exit(x);
            }
            /* Never reach this code */
            globus_assert(0);
        }
    }

    for (i = 0; i < task->count; i++)
    {
        globus_l_fork_log_state_change(
                task->pids[i],
                task,
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE,
                0);
    }
    printf("101;%s;", task->tag);

    for (i = 0; i < task->count; i++)
    {
        printf("%s:%lu", task->jobid_prefix, (unsigned long) task->pids[i]);

        if (i + 1 < task->count)
        {
            printf(",");
        }
    }
    printf("\n");
    return GLOBUS_SUCCESS;
kill_procs:
    for(i = task->pidcount - 1; i >= 0; i++)
    {
        kill(task->pids[i], SIGTERM);

        globus_hashtable_remove(
            &globus_l_fork_active_tasks,
            (void *) (intptr_t) task->pids[i]);
        task->pidcount--;
    }
error:
    return rc;
}
/* globus_l_fork_start_task() */

static
void
globus_l_fork_log_state_change(
    pid_t                               pid,
    globus_fork_task_t                  task,
    globus_gram_protocol_job_state_t    job_state,
    int                                 exit_code)
{
    FILE *                              logfile;
    int                                 logfd;
    time_t                              now = time(NULL);

    logfd = globus_l_fork_log_open_and_lock(task);
    if (logfd < 0)
    {
        return;
    }

    logfile = fdopen(logfd, "a");
    if (logfile == NULL)
    {
        perror("Error converting log file descriptor to FILE *");
        close(logfd);
        return;
    }
    fprintf(logfile,
            "001;%lld;%s:%lu;%d;%d\n",
            (long long) now,
            task->jobid_prefix,
            (unsigned long) pid,
            (int) job_state,
            (int) exit_code);

    fclose(logfile); /* will release the lock */
}
/* globus_l_fork_log_state_change() */

static
int
globus_l_fork_log_open_and_lock(
    globus_fork_task_t                  task)
{
    int                                 logfd;
    struct flock                        lock;
    int                                 rc;
    int                                 tries = 0;

    do
    {
        errno = 0;
        logfd = open(globus_l_fork_logfile_path, O_WRONLY|O_APPEND);
    } while (logfd < 0 && errno == EINTR);

    if (logfd < 0)
    {
        globus_l_fork_error(
                task,
                GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
                "Error opening \"%s\" (configured in %s): %s\n",
                globus_l_fork_logfile_path,
                "$GLOBUS_LOCATION/etc/globus-fork.conf",
                strerror(errno));
        return logfd;
    }

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_END;
    lock.l_start = 0;
    lock.l_len = 0;

    do
    {
        errno = 0;
        rc = fcntl(logfd, F_SETLKW, &lock);

        if (rc < 0)
        {
            switch (errno)
            {
                case ENOLCK:
                    /* The argument cmd is F_SETLK or F_SETLKW and satisfying
                     * the lock or unlock request would result in the number of
                     * locked regions in the system exceeding a system-imposed
                     * limit.
                     */
                    if (tries++ < 10)
                    {
                        sleep(5);
                    }
                    else
                    {
                        globus_l_fork_error(
                                task,
                                GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
                                "Error locking \"%s\" (configured in %s): %s\n",
                                globus_l_fork_logfile_path,
                                "$GLOBUS_LOCATION/etc/globus-fork.conf",
                                strerror(errno));
                        close(logfd);
                        logfd = -1;
                    }
                    break;

                case EINTR:
                    break;

                case EOVERFLOW:
                    /* l_start or l_len is out of range, should never happen */
                    globus_assert(errno != EOVERFLOW);

                case EBADF:
                    /* File is open for writing, should never happen */
                    globus_assert(errno != EBADF);

                case EDEADLK:
                    /* trying to acquire this lock would deadlock, should never
                     * happen */
                    globus_assert(errno != EDEADLK);

                default:
                    globus_l_fork_error(
                            task,
                            GLOBUS_GRAM_PROTOCOL_ERROR_JOB_EXECUTION_FAILED,
                            "Error locking \"%s\" (configured in %s): %s\n",
                            globus_l_fork_logfile_path,
                            "$GLOBUS_LOCATION/etc/globus-fork.conf",
                            strerror(errno));
                    close(logfd);
                    logfd = -1;
                    break;
            }
        }
    }
    while (rc < 0 && logfd >= 0);

    return logfd;
}
/* globus_l_fork_log_open_and_lock() */

static
void
globus_l_fork_split_string(
    char *                              value,
    char ***                            result_ptr,
    size_t *                            result_cnt)
{
    char * p, *q;
    size_t count = 1;
    int i,j;

    p = value;

    while (*p)
    {
        if (*p == '\\')
        {
            p+=2;
        }
        else if (*p == ',')
        {
            count++;
            p++;
        }
        else
        {
            p++;
        }
    }

    if (count == 0)
    {
        *result_ptr = NULL;
        *result_cnt = 0;

        return;
    }

    p = q = value;

    *result_ptr = globus_libc_calloc(count + 1, sizeof(char*));

    if (*result_ptr == NULL)
    {
        *result_cnt = 0;

        return;
    }

    *result_cnt = count+1;
    j = 0;

    for (i = 1; i < count; i++)
    {
        while (*p)
        {
            if (*p == '\\')
            {
                p+=2;
            }
            else if (*p == ',')
            {
                *p = '\0';
                (*result_ptr)[j++] = q;
                q = ++p;
                break;
            }
            else
            {
                p++;
            }
        }
    }
    (*result_ptr)[j++] = q;
}

static
int
globus_l_fork_task_init(
    globus_fork_task_t *                task)
{
    globus_uuid_t                       uuid;
    int                                 rc;

    rc = globus_uuid_create(&uuid);
    if (rc != GLOBUS_SUCCESS)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }
    (*task) = globus_libc_calloc(1, sizeof(globus_l_fork_task_t));

    if ((*task) == NULL)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }

    (*task)->jobid_prefix = globus_common_create_string("%s", uuid.text);

    if ((*task)->jobid_prefix == NULL)
    {
        free(*task);
        *task = NULL;
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }
    (*task)->taskbuflen = GLOBUS_FORK_TASK_BUF_SIZE;

    (*task)->task = globus_libc_malloc((*task)->taskbuflen);
    if ((*task)->task == NULL)
    {
        globus_libc_free(*task);
        *task = NULL;
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }

    return GLOBUS_SUCCESS;
}
/* globus_l_fork_task_init() */

static
void
globus_l_fork_task_free(
    globus_fork_task_t                  task)
{
    if (task == NULL)
    {
        return;
    }

    if (task->task)
    {
        globus_libc_free(task->task);
    }

    if (task->environment)
    {
        globus_libc_free(task->environment);
    }

    if (task->arguments)
    {
        globus_libc_free(task->arguments);
    }
    if (task->jobid_prefix)
    {
        free(task->jobid_prefix);
    }

    globus_libc_free(task);
}
/* globus_l_fork_task_free() */

static
void
globus_l_fork_task_increase_buffer(
    globus_fork_task_t                  task)
{
    char * tmp;

    tmp = globus_libc_realloc(task->task,
        task->taskbuflen + GLOBUS_FORK_TASK_BUF_SIZE);
    if (tmp == NULL)
    {
        return;
    }
    task->taskbuflen += GLOBUS_FORK_TASK_BUF_SIZE;
    task->task = tmp;

    return;
}
/* globus_l_fork_task_increase_buffer() */

static
globus_result_t
globus_l_fork_unescape(
    char *                              value)
{
    size_t len;
    int i;

    if (value == NULL)
    {
        return globus_error_put(GLOBUS_ERROR_NO_INFO);
    }

    len = strlen(value);

    for (i = 0; i < len; i++)
    {
        if (value[i] == '\\')
        {
            if (value[i+1] == '\\' ||
                value[i+1] == ';' ||
                value[i+1] == ',' ||
                value[i+1] == 'n' ||
                value[i+1] == '=')
            {
                memmove(&value[i], &value[i+1], len - i);
            }

            if (value[i] == 'n')
            {
                value[i] = '\n';
            }
            len--;
        }
    }
    return GLOBUS_SUCCESS;
}
/* globus_l_fork_unescape() */

static
void
globus_l_fork_error(
    globus_fork_task_t                  task,
    int                                 error,
    const char *                        fmt,
    ...)
{
    va_list                             ap;

    va_start(ap, fmt);

    vfprintf(stderr, fmt, ap);
    
    va_end(ap);
}
/* globus_l_fork_error() */
