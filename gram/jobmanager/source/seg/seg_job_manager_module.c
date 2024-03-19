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
#include "globus_scheduler_event_generator.h"
#include "globus_gram_protocol_constants.h"
#include "version.h"

#include <string.h>

/**
 * @file seg_job_manager_module.c
 * @brief Job Manager SEG Module
 */
#define SEG_JOB_MANAGER_DEBUG(level, message) \
    GlobusDebugPrintf(SEG_JOB_MANAGER, level, message)

#define JOB_MANAGER_SEG_SCHEDULER "JOB_MANAGER_SEG_SCHEDULER"
#define JOB_MANAGER_SEG_LOG_PATH  "JOB_MANAGER_SEG_LOG_PATH"

/**
 * Debug levels:
 * If the environment variable SEG_JOB_MANAGER_DEBUG is set to a bitwise or
 * of these values, then a corresponding log message will be generated.
 */
typedef enum
{
    /**
     * Information of function calls and exits
     */
    SEG_JOB_MANAGER_DEBUG_INFO = (1<<0),
    /**
     * Warnings of things which may be bad.
     */
    SEG_JOB_MANAGER_DEBUG_WARN = (1<<1),
    /**
     * Fatal errors.
     */
    SEG_JOB_MANAGER_DEBUG_ERROR = (1<<2),
    /**
     * Details of function executions.
     */
    SEG_JOB_MANAGER_DEBUG_TRACE = (1<<3)
}
globus_l_seg_job_manager_debug_level_t;

enum
{
    SEG_JOB_MANAGER_ERROR_UNKNOWN = 1,
    SEG_JOB_MANAGER_ERROR_OUT_OF_MEMORY,
    SEG_JOB_MANAGER_ERROR_BAD_PATH,
    SEG_JOB_MANAGER_ERROR_LOG_PERMISSIONS,
    SEG_JOB_MANAGER_ERROR_LOG_NOT_PRESENT,
    SEG_JOB_MANAGER_ERROR_LOG_EOF
};

/**
 * State of the JOB_MANAGER log file parser.
 */
typedef struct 
{
    /** Path of the current log file being parsed */
    char *                              path;
    /** Timestamp of when to start generating events from */
    struct tm                           start_timestamp;
    /** Stdio file handle of the log file */
    FILE *                              fp;
    /** Callback for periodic file polling */
    globus_callback_handle_t            callback;
    /**
     * Flag inidicating that this logfile isn't the one corresponding to
     * today, so and EOF on it should require us to close and open a newer
     * one
     */
    globus_bool_t                       old_log;

    /**
     * Path to the directory where the JOB_MANAGER server log files are located
     */
    char *                              log_dir;
} globus_l_job_manager_logfile_state_t;

static globus_mutex_t                   globus_l_job_manager_mutex;
static globus_cond_t                    globus_l_job_manager_cond;
static globus_bool_t                    shutdown_called;
static int                              callback_count;


GlobusDebugDefine(SEG_JOB_MANAGER);

static
int
globus_l_job_manager_module_activate(void);

static
int
globus_l_job_manager_module_deactivate(void);

static
void
globus_l_job_manager_poll_callback(
    void *                              user_arg);

static
int
globus_l_job_manager_parse_events(
    globus_l_job_manager_logfile_state_t *      state);

static
void
globus_l_job_manager_normalize_date(
    struct tm *                         tm);

static
int
globus_l_job_manager_find_logfile(
    globus_l_job_manager_logfile_state_t *      state);

static
globus_bool_t
globus_l_time_is_newer(
    struct tm *                         value,
    struct tm *                         benchmark);

static
globus_bool_t
globus_l_next_file_exists(
    globus_l_job_manager_logfile_state_t *      state);

GlobusExtensionDefineModule(globus_seg_job_manager) =
{
    "globus_seg_job_manager",
    globus_l_job_manager_module_activate,
    globus_l_job_manager_module_deactivate,
    NULL,
    NULL,
    &local_version
};

static
int
globus_l_job_manager_module_activate(void)
{
    time_t                              timestamp_val;
    globus_l_job_manager_logfile_state_t *      
                                        logfile_state;
    int                                 rc;
    globus_reltime_t                    delay;
    globus_result_t                     result;
    char *                              scheduler;

    rc = globus_module_activate(GLOBUS_COMMON_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto activate_common_failed;
    }
    rc = globus_mutex_init(&globus_l_job_manager_mutex, NULL);

    if (rc != GLOBUS_SUCCESS)
    {
        goto mutex_init_failed;
    }
    rc = globus_cond_init(&globus_l_job_manager_cond, NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        goto cond_init_failed;
    }
    shutdown_called = GLOBUS_FALSE;
    callback_count = 0;

    GlobusDebugInit(
        SEG_JOB_MANAGER,
        SEG_JOB_MANAGER_DEBUG_INFO
        SEG_JOB_MANAGER_DEBUG_WARN
        SEG_JOB_MANAGER_DEBUG_ERROR
        SEG_JOB_MANAGER_DEBUG_TRACE);

    logfile_state = calloc(1, sizeof(globus_l_job_manager_logfile_state_t));
    if (logfile_state == NULL)
    {
        goto calloc_state_failed;
    }

    /* Configuration info */
    result = globus_scheduler_event_generator_get_timestamp(&timestamp_val);
    if (result != GLOBUS_SUCCESS)
    {
        goto get_timestamp_failed;
    }

    if (timestamp_val != 0)
    {
        if (globus_libc_gmtime_r(
                &timestamp_val,
                &logfile_state->start_timestamp) == NULL)
        {
            goto gmtime_failed;
        }
    }
    scheduler = getenv(JOB_MANAGER_SEG_SCHEDULER);
    if (scheduler == NULL)
    {
        SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_ERROR,
            ("Error: %s not set\n", JOB_MANAGER_SEG_SCHEDULER));

        result = GLOBUS_FAILURE;
        goto get_scheduler_failed;
    }

    if (getenv(JOB_MANAGER_SEG_LOG_PATH))
    {
        logfile_state->log_dir = strdup(getenv(JOB_MANAGER_SEG_LOG_PATH));
    }
    else
    {
        char * log_dir_pattern = globus_common_create_string(
                "${localstatedir}/lib/globus/globus-seg-%s", scheduler);

        globus_eval_path(log_dir_pattern, &logfile_state->log_dir);
        free(log_dir_pattern);
    }

    if (logfile_state->log_dir == NULL)
    {
        SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_ERROR,
            ("Error: out of memory\n"));
        goto get_path_failed;
    }

    /* Convert timestamp to filename */
    rc = globus_l_job_manager_find_logfile(logfile_state);

    if (rc == GLOBUS_SUCCESS)
    {
        logfile_state->fp = fopen(logfile_state->path, "r");

        if (logfile_state->fp == NULL)
        {
            rc = SEG_JOB_MANAGER_ERROR_OUT_OF_MEMORY;

            goto fopen_failed;
        }
        GlobusTimeReltimeSet(delay, 0, 0);
    }
    else if(rc == SEG_JOB_MANAGER_ERROR_LOG_NOT_PRESENT)
    {
        GlobusTimeReltimeSet(delay, 1, 0);
    }
    else
    {
        goto bad_log_path;
    }

    result = globus_callback_register_oneshot(
            &logfile_state->callback,
            &delay,
            globus_l_job_manager_poll_callback,
            logfile_state);
    if (result != GLOBUS_SUCCESS)
    {
        goto oneshot_failed;
    }
    callback_count++;

    return 0;
oneshot_failed:
    if (logfile_state->fp)
    {
        fclose(logfile_state->fp);
    }
fopen_failed:
    if (logfile_state->path)
    {
        free(logfile_state->path);
    }
bad_log_path:
    free(logfile_state->log_dir);
get_path_failed:
get_scheduler_failed:
get_timestamp_failed:
gmtime_failed:
    free(logfile_state);
calloc_state_failed:
    globus_cond_destroy(&globus_l_job_manager_cond);
cond_init_failed:
    globus_mutex_destroy(&globus_l_job_manager_mutex);
mutex_init_failed:
    globus_module_deactivate(GLOBUS_COMMON_MODULE);
activate_common_failed:
    return 1;
}
/* globus_l_job_manager_module_activate() */

static
int
globus_l_job_manager_module_deactivate(void)
{
    globus_mutex_lock(&globus_l_job_manager_mutex);
    shutdown_called = GLOBUS_TRUE;

    while (callback_count > 0)
    {
        globus_cond_wait(&globus_l_job_manager_cond, &globus_l_job_manager_mutex);
    }
    globus_mutex_unlock(&globus_l_job_manager_mutex);

    GlobusDebugDestroy(SEG_JOB_MANAGER);

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return 0;
}

/**
 * Periodic poll of file to act like tail -f
 *
 * @param user_arg
 *     Log file parsing state
 */
static
void
globus_l_job_manager_poll_callback(
    void *                              user_arg)
{
    int                                 rc;
    globus_l_job_manager_logfile_state_t *
                                        state = user_arg;
    globus_bool_t                       eof_hit = GLOBUS_FALSE;
    globus_reltime_t                    delay;
    globus_result_t                     result;
    time_t                              poll_time = time(NULL);
    struct tm                           poll_tm, *tm_result;
    struct stat                         stat;
    char *                              today;

    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_INFO,
            ("globus_l_job_manager_poll_callback()\n"));

    globus_mutex_lock(&globus_l_job_manager_mutex);
    if (shutdown_called)
    {
        SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_INFO,
                ("polling while deactivating"));

        globus_mutex_unlock(&globus_l_job_manager_mutex);
        goto error;
    }
    globus_mutex_unlock(&globus_l_job_manager_mutex);

    if (state->fp != NULL)
    {
        /* Parse data */
        SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_TRACE,
                ("parsing events\n"));
        rc = globus_l_job_manager_parse_events(state);
        if (rc == SEG_JOB_MANAGER_ERROR_LOG_EOF)
        {
            eof_hit = GLOBUS_TRUE;
        }
    }

    if (eof_hit)
    {
        tm_result = globus_libc_gmtime_r(&poll_time, &poll_tm);
        if (tm_result == NULL)
        {
            SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_WARN,
                    ("Couldn't convert to gmtime\n"));
        }
        else
        {
            today = globus_common_create_string(
                    "%s/%4d%02d%02d",
                    state->log_dir,
                    tm_result->tm_year+1900,
                    tm_result->tm_mon+1,
                    tm_result->tm_mday);
            if (today && (strcmp(today, state->path) != 0))
            {
                /* New day... if new file exists and the old one hasn't changed since our
                 * last poll, mark it as old
                 */
                if (globus_l_next_file_exists(state))
                {
                    rc = fstat(fileno(state->fp), &stat);
                    if (rc != -1)
                    {
                        if (ftello(state->fp) == stat.st_size)
                        {
                            state->old_log = GLOBUS_TRUE;
                        }
                    }
                }
            }
            if (today)
            {
                free(today);
            }
        }
    }

    /* If end of log, close this logfile and look for a new one. Also, if
     * the current day's log doesn't exist yet, check for it
     */
    if ((eof_hit && state->old_log) || state->fp == NULL)
    {
        if (state->fp)
        {
            fclose(state->fp);
            state->fp = NULL;
            state->start_timestamp.tm_mday++;
            state->start_timestamp.tm_hour = 0;
            state->start_timestamp.tm_min = 0;
            state->start_timestamp.tm_sec = 0;
            globus_l_job_manager_normalize_date(&state->start_timestamp);
        }

        rc = globus_l_job_manager_find_logfile(state);

        if (rc == GLOBUS_SUCCESS)
        {
            /* Opening a new logfile, run w/out delay */
            state->fp = fopen(state->path, "r");
            if (state->fp == NULL)
            {
                goto error;
            }
            eof_hit = GLOBUS_FALSE;

            GlobusTimeReltimeSet(delay, 0, 0);
        }
        else if (rc == SEG_JOB_MANAGER_ERROR_LOG_NOT_PRESENT)
        {
            /* Current day's logfile not present, wait a bit longer for
             * it to show up
             */
            GlobusTimeReltimeSet(delay, 30, 0);
            eof_hit = GLOBUS_TRUE;
        }
        else
        {
            goto error;
        }
    }
    else if(eof_hit)
    {
        /* eof on current logfile, wait for new data */
        GlobusTimeReltimeSet(delay, 2, 0);
    }
    else
    {
        /* still data available in current file, hurry up! */
        GlobusTimeReltimeSet(delay, 0, 0);
    }

    result = globus_callback_register_oneshot(
            &state->callback,
            &delay,
            globus_l_job_manager_poll_callback,
            state);
    if (result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_INFO,
            ("globus_l_job_manager_poll_callback() exited with/success\n"));
    return;
error:
    globus_mutex_lock(&globus_l_job_manager_mutex);
    if (shutdown_called)
    {
        callback_count--;

        if (callback_count == 0)
        {
            globus_cond_signal(&globus_l_job_manager_cond);
        }
    }
    globus_mutex_unlock(&globus_l_job_manager_mutex);

    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_WARN,
            ("globus_l_job_manager_poll_callback() exited with/error\n"));
    return;
}
/* globus_l_job_manager_poll_callback() */

/**
 * Determine the next available JOB_MANAGER log file name from the 
 * timestamp stored in the logfile state structure.
 * 
 * @param state
 *     JOB_MANAGER log state structure. The path field of the structure may be
 *     modified by this function.
 *
 * @retval GLOBUS_SUCCESS
 *     Name of an log file name has been found and the file exists.
 * @retval 1
 *     Something bad occurred.
 */
static
int
globus_l_job_manager_find_logfile(
    globus_l_job_manager_logfile_state_t *      state)
{
    struct tm *                         tm_result;
    struct tm                           tm_val;
    struct tm                           tm_now;
    globus_bool_t                       user_timestamp = GLOBUS_TRUE;
    time_t                              now;
    struct stat                         s;
    int                                 rc;

    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_INFO,
            ("globus_l_job_manager_find_logfile()\n"));

    if (state->path == NULL)
    {
        SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_TRACE,
                ("allocating path\n"));
        state->path = malloc(strlen(state->log_dir) + 10);

        if (state->path == NULL)
        {
            rc = SEG_JOB_MANAGER_ERROR_OUT_OF_MEMORY;
            goto error;
        }
    }

    now = time(NULL);

    tm_result = globus_libc_gmtime_r(&now, &tm_now);
    if (tm_result == NULL)
    {
        SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_WARN,
                ("localtime_r failed\n"));
        rc = SEG_JOB_MANAGER_ERROR_OUT_OF_MEMORY;

        goto error;
    }
    else
    {
        /* Get the first log message of the day */
        tm_now.tm_sec = 0;
        tm_now.tm_min = 0;
        tm_now.tm_hour = 0;
    }

    if (state->start_timestamp.tm_sec == 0 &&
        state->start_timestamp.tm_min == 0 &&
        state->start_timestamp.tm_hour == 0 &&
        state->start_timestamp.tm_mday == 0 &&
        state->start_timestamp.tm_mon == 0 &&
        state->start_timestamp.tm_year == 0)
    {
        SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_TRACE,
                ("no timestamp set, using current time\n"));
        memcpy(&state->start_timestamp, &tm_now, sizeof(struct tm));
        user_timestamp = GLOBUS_FALSE;
    }

    memcpy(&tm_val, &state->start_timestamp, sizeof(struct tm));

    tm_result = &tm_val;

    do
    {
        if (tm_result == NULL)
        {
            SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_WARN,
                ("couldn't get tm from timestmap\n"));

            rc = SEG_JOB_MANAGER_ERROR_OUT_OF_MEMORY;
            goto error;
        }
        if (tm_val.tm_year < tm_now.tm_year ||
            (tm_val.tm_year == tm_now.tm_year &&
             tm_val.tm_mon < tm_now.tm_mon) ||
            (tm_val.tm_year == tm_now.tm_year &&
             tm_val.tm_mon == tm_now.tm_mon &&
             tm_val.tm_mday < tm_now.tm_mday))
        {
            state->old_log = GLOBUS_TRUE;
        }
        else
        {
            state->old_log = GLOBUS_FALSE;
        }

        rc = sprintf(state->path,
                "%s/%4d%02d%02d",
                state->log_dir,
                tm_val.tm_year+1900,
                tm_val.tm_mon+1,
                tm_val.tm_mday);

        if (rc < 0)
        {
            SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_WARN,
                ("couldn't format date to string\n"));
            rc = SEG_JOB_MANAGER_ERROR_OUT_OF_MEMORY;
            goto error;
        }
        rc = stat(state->path, &s);

        if (rc < 0)
        {
            switch (errno)
            {
                case ENOENT:
                    /* Doesn't exist, advance to the next day's log
                     * for next try if we're not looking to the future.
                     */
                    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_ERROR,
                        ("file %s doesn't exist\n", state->path));

                    /* Increment day by 1, then normalize to be a proper
                     * struct tm without having tm_mday exceed what is valid
                     * for the month.
                     */
                    tm_val.tm_mday++;

                    globus_l_job_manager_normalize_date(&tm_val);

                    if (globus_l_time_is_newer(&tm_val, &tm_now))
                    {
                        SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_ERROR,
                            ("looking for file in the future!\n"));
                        rc = SEG_JOB_MANAGER_ERROR_LOG_NOT_PRESENT;
                        goto error;
                    }

                    /* Starting new log, get all messages in that file */
                    tm_val.tm_sec = 0;
                    tm_val.tm_min = 0;
                    tm_val.tm_hour = 0;

                    memcpy(&state->start_timestamp,
                            &tm_val,
                            sizeof(struct tm));

                    break;

                case EACCES:
                    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_ERROR,
                        ("permissions needed to access logfile %s\n",
                        state->path));
                    /* Permission problem (fatal) */
                    rc = SEG_JOB_MANAGER_ERROR_LOG_PERMISSIONS;
                    goto error;

                case ENOTDIR:
#ifdef ELOOP
                case ELOOP:
#endif
                case ENAMETOOLONG:
                    /* broken path (fatal) */
                    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_ERROR,
                        ("broken path to logfile %s\n",
                        state->path));
                    rc = SEG_JOB_MANAGER_ERROR_BAD_PATH;
                    goto error;

                case EFAULT:
                    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_ERROR,
                        ("bad pointer\n"));
                    globus_assert(errno != EFAULT);

                case EINTR:
                case ENOMEM: /* low kernel mem */
                    /* try again later */
                    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_WARN,
                        ("going to have to retry stat()\n"));
                    continue;

                default:
                    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_ERROR,
                        ("unexpected errno\n"));
                    rc = SEG_JOB_MANAGER_ERROR_UNKNOWN;
                    goto error;
            }
        }
    }
    while ((rc != 0) && user_timestamp);

    if (rc != 0)
    {
        goto error;
    }

    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_INFO,
            ("globus_l_job_manager_find_logfile() exits w/out error\n"));
    return 0;

error:
    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_WARN,
            ("globus_l_job_manager_find_logfile() exits w/error\n"));
    return rc;
}
/* globus_l_job_manager_find_logfile() */

static
int
globus_l_job_manager_parse_events(
    globus_l_job_manager_logfile_state_t *      state)
{
    int                                 rc;
    int                                 protocol_msg_type;
    time_t                              stamp;
    long long                           tmp_stamp;
    char                                jobid[129];
    char                                nl[2];
    int                                 job_state;
    int                                 exit_code;
    struct tm                           gmstamp, *gmstampp;
    fpos_t                              pos;

    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_INFO,
            ("globus_l_job_manager_parse_events() called\n"));


    fgetpos(state->fp, &pos);
    while ((rc = fscanf(state->fp, "%d;%lld;%128[^;];%d;%d%1[\n]",
                    &protocol_msg_type,
                    &tmp_stamp,
                    jobid,
                    &job_state,
                    &exit_code,
                    nl)) > 4)
    {
        stamp = tmp_stamp;
        if (rc == 4 && fscanf(state->fp, "%1[\n]", nl) != 1)
        {
            goto bad_line;
        }

        if (protocol_msg_type != 1)
        {
            goto bad_line;
        }

        gmstampp = globus_libc_gmtime_r(&stamp, &gmstamp);

        if (gmstampp == NULL ||
            globus_l_time_is_newer(&state->start_timestamp, &gmstamp))
        {
            /* Ignore events that occur before our start timestamp */
            goto bad_line;
        }

        switch(job_state)
        {
        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING:
            globus_scheduler_event_pending(stamp, jobid);
            break;

        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE:
            globus_scheduler_event_active(stamp, jobid);
            break;

        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE:
            globus_scheduler_event_done(stamp, jobid, exit_code);
            break;

        case GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED:
            globus_scheduler_event_failed(stamp, jobid, exit_code);
            break;

        default:
            goto bad_line;
        }
bad_line:
        fgetpos(state->fp, &pos);
    }
    if (feof(state->fp))
    {
        clearerr(state->fp);
        rc = SEG_JOB_MANAGER_ERROR_LOG_EOF;
    }
    else
    {
        rc = 0;
    }
    fsetpos(state->fp, &pos);

    SEG_JOB_MANAGER_DEBUG(SEG_JOB_MANAGER_DEBUG_INFO,
            ("globus_l_job_manager_parse_events() exits\n"));
    return rc;
}
/* globus_l_job_manager_parse_events() */

/* Leap year is year divisible by 4, unless divisibly by 100 and not by 400 */
#define IS_LEAP_YEAR(Y) \
     (!(Y % 4)) && ((Y % 100) || !(Y % 400))
static
void
globus_l_job_manager_normalize_date(
    struct tm *                         tm)
{
    int                                 test_year;
    int                                 overflow_days = 0;
    static int                          mday_max[] =
    {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 
    };
    static int                          mday_leap_max[] =
    {
        31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 
    };

    do
    {
        if (overflow_days > 0)
        {
            tm->tm_mday = overflow_days;
            tm->tm_mon++;
        }

        /* skipped to the next year */
        if (tm->tm_mon == 12)
        {
            tm->tm_year++;
            tm->tm_mon = 0;
        }

        test_year = tm->tm_year + 1900;
        overflow_days = IS_LEAP_YEAR(test_year) 
                ? tm->tm_mday - mday_leap_max[tm->tm_mon]
                : tm->tm_mday - mday_max[tm->tm_mon];
    } while (overflow_days > 0);
}

static
globus_bool_t
globus_l_time_is_newer(
    struct tm *                         value,
    struct tm *                         benchmark)
{
    if (value->tm_year < benchmark->tm_year)
    {
        return GLOBUS_FALSE;
    }
    else if (value->tm_year > benchmark->tm_year)
    {
        return GLOBUS_TRUE;
    }
    else if (value->tm_mon < benchmark->tm_mon)
    {
        return GLOBUS_FALSE;
    }
    else if (value->tm_mon > benchmark->tm_mon)
    {
        return GLOBUS_TRUE;
    }
    else if (value->tm_mday < benchmark->tm_mday)
    {
        return GLOBUS_FALSE;
    }
    else if (value->tm_mday > benchmark->tm_mday)
    {
        return GLOBUS_TRUE;
    }
    else if (value->tm_hour < benchmark->tm_hour)
    {
        return GLOBUS_FALSE;
    }
    else if (value->tm_hour > benchmark->tm_hour)
    {
        return GLOBUS_TRUE;
    }
    else if (value->tm_min < benchmark->tm_min)
    {
        return GLOBUS_FALSE;
    }
    else if (value->tm_min > benchmark->tm_min)
    {
        return GLOBUS_TRUE;
    }
    else if (value->tm_sec < benchmark->tm_sec)
    {
        return GLOBUS_FALSE;
    }
    else if (value->tm_sec > benchmark->tm_sec)
    {
        return GLOBUS_TRUE;
    }
    else
    {
        return GLOBUS_FALSE;
    }
}
/* globus_l_time_is_newer() */

static
globus_bool_t
globus_l_next_file_exists(
    globus_l_job_manager_logfile_state_t *      state)
{
    struct tm                           next_day;
    char *                              next_log;
    globus_bool_t                       file_exists = GLOBUS_FALSE;

    next_day = state->start_timestamp;
    next_day.tm_mday++;
    globus_l_job_manager_normalize_date(&next_day);
    next_day.tm_sec = 0;
    next_day.tm_min = 0;
    next_day.tm_hour = 0;

    next_log = globus_common_create_string(
                "%s/%4d%02d%02d",
                state->log_dir,
                next_day.tm_year+1900,
                next_day.tm_mon+1,
                next_day.tm_mday);
    if (access(next_log, R_OK) == 0)
    {
        file_exists = GLOBUS_TRUE;
    }
    free(next_log);

    return file_exists;
}
/* globus_l_next_file_exists() */
