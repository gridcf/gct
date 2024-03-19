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
#include "version.h"

#include <string.h>

enum {
    JOB_STAT_NULL = 0x00,
    JOB_STAT_PEND = 0x01,
    JOB_STAT_PSUSP = 0x02,
    JOB_STAT_RUN = 0x04,
    JOB_STAT_SSUSP = 0x08,
    JOB_STAT_USUSP = 0x10,
    JOB_STAT_EXIT = 0x20,
    JOB_STAT_DONE = 0x40,
    JOB_STAT_PDONE = (0x80),
    JOB_STAT_PERR = (0x100),
    JOB_STAT_WAIT = (0x200),
    JOB_STAT_UNKWN = 0x10000
};

#define SEGLsfEnter() \
        SEGLsfDebug(SEG_LSF_DEBUG_INFO, ("Enter %s\n", _globus_func_name))

#define SEGLsfExit() \
        SEGLsfDebug(SEG_LSF_DEBUG_INFO, ("Exit %s\n", _globus_func_name))


/**
 * Debug levels:
 * If the environment variable SEG_LSF_DEBUG is set to a bitwise or
 * of these values, then a corresponding log message will be generated.
 */
typedef enum
{
    /**
     * Information of function calls and exits
     */
    SEG_LSF_DEBUG_INFO = (1<<0),
    /**
     * Warnings of things which may be bad.
     */
    SEG_LSF_DEBUG_WARN = (1<<1),
    /**
     * Fatal errors.
     */
    SEG_LSF_DEBUG_ERROR = (1<<2),
    /**
     * Details of function executions.
     */
    SEG_LSF_DEBUG_TRACE = (1<<3)
}
globus_l_seg_lsf_debug_level_t;

#ifdef BUILD_DEBUG
#define SEGLsfDebug(level, message) \
    GlobusDebugPrintf(SEG_LSF, level, ("%s", globus_l_seg_lsf_level_string(level))); \
    GlobusDebugPrintf(SEG_LSF, level, message)
#else
#define SEGLsfDebug(level, message) \
    if (level == SEG_LSF_DEBUG_ERROR) \
    { \
        fprintf(stderr, "%s", globus_l_seg_lsf_level_string(level)); \
        globus_l_seg_lsf_debug message; \
    }
static
void
globus_l_seg_lsf_debug(const char * fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
#endif

static
char *
globus_l_seg_lsf_level_string(globus_l_seg_lsf_debug_level_t level)
{
    switch (level)
    {
        case SEG_LSF_DEBUG_INFO:
            return "[INFO] ";
        case SEG_LSF_DEBUG_WARN:
            return "[WARN] ";
        case SEG_LSF_DEBUG_ERROR:
            return "[ERROR] ";
        case SEG_LSF_DEBUG_TRACE:
            return "[TRACE] ";
        default:
            return "";
    }
}

enum
{
    SEG_LSF_ERROR_UNKNOWN = 1,
    SEG_LSF_ERROR_OUT_OF_MEMORY,
    SEG_LSF_ERROR_BAD_PATH,
    SEG_LSF_ERROR_LOG_PERMISSIONS,
    SEG_LSF_ERROR_LOG_NOT_PRESENT
};

/**
 * State of the LSF log file parser.
 */
typedef struct 
{
    /**
     * Path to the LSF logdir
     */
    char *                              log_dir;

    /**
     * Last known status for the lsb.events.1 file. This will only
     * change when a log file is being rotated. When this happens, we
     * may need to be very careful about what is going on with our latest
     * read.
     */
    struct stat                         event_idx_stat;

    /**
     * Path to lsb.events.1 file
     */
    char *                              event_idx_path;

    /**
     * Current historical log file we are looking at if we are replaying
     * older events.
     */
    unsigned int                        event_idx;


    /**
     * If non-zero, the earliest event timestamp we are interested in reading
     * about from the currently opened logfile
     */
    time_t                              start_timestamp;

    /**
     * If non-zero the newest event in the current log file we are reading
     * if it is an historical one. This may be larger than the final timestamp
     * in the file for some reason.
     */
    time_t                              end_of_file_timestamp;

    /**
     * Path of the currently opened log file.
     */
    char *                              path;
    /**
     * True if the current log file is the lsb.events file and not
     * one of the rotated files.
     */
    globus_bool_t                       is_current_file;
    /**
     * Stdio file handle of the log file we are currently reading
     */
    FILE *                              fp;

    /** Buffer of log file data */
    char *                              buffer;
    /** Length of the buffer */
    size_t                              buffer_length;
    /** Starting offset of valid data in the buffer. */
    size_t                              buffer_point;
    /** Amount of valid data in the buffer */
    size_t                              buffer_valid;
} globus_l_lsf_logfile_state_t;

static globus_mutex_t                   globus_l_lsf_mutex;
static globus_cond_t                    globus_l_lsf_cond;
static globus_bool_t                    shutdown_called;
static int                              callback_count;


GlobusDebugDefine(SEG_LSF);

static
int
globus_l_lsf_module_activate(void);

static
int
globus_l_lsf_module_deactivate(void);

static
void
globus_l_lsf_read_callback(
    void *                              user_arg);

static
int
globus_l_lsf_parse_events(
    globus_l_lsf_logfile_state_t *      state);

static
int
globus_l_lsf_clean_buffer(
    globus_l_lsf_logfile_state_t *      state);

static
int
globus_l_lsf_increase_buffer(
    globus_l_lsf_logfile_state_t *      state);

static
int
globus_l_lsf_find_logfile(
    globus_l_lsf_logfile_state_t *      state);

GlobusExtensionDefineModule(globus_seg_lsf) =
{
    "globus_seg_lsf",
    globus_l_lsf_module_activate,
    globus_l_lsf_module_deactivate,
    NULL,
    NULL,
    &local_version
};

static
int
globus_l_lsf_module_activate(void)
{
    globus_l_lsf_logfile_state_t *      logfile_state;
    int                                 rc;
    globus_reltime_t                    delay;
    globus_result_t                     result;
    char *                              config_path;
    GlobusFuncName(globus_l_lsf_module_activate);

    rc = globus_module_activate(GLOBUS_COMMON_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        fprintf(stderr, "Fatal error activating GLOBUS_COMMON_MODULE\n");
        goto error;
    }
    if (globus_module_getenv("SEG_LSF_DEBUG") == NULL)
    {
        globus_module_setenv("SEG_LSF_DEBUG", "ERROR");
    }
    GlobusDebugInit(SEG_LSF, INFO WARN ERROR TRACE);

    SEGLsfEnter();

    rc = globus_mutex_init(&globus_l_lsf_mutex, NULL);

    if (rc != GLOBUS_SUCCESS)
    {
        SEGLsfDebug(SEG_LSF_DEBUG_ERROR,
                ("Fatal error initializing mutex\n"));
        goto deactivate_common_error;
    }
    rc = globus_cond_init(&globus_l_lsf_cond, NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        SEGLsfDebug(SEG_LSF_DEBUG_ERROR,
                ("Fatal error initializing cond\n"));
        goto destroy_mutex_error;
    }
    shutdown_called = GLOBUS_FALSE;
    callback_count = 0;

    logfile_state = calloc(
            1,
            sizeof(globus_l_lsf_logfile_state_t));

    if (logfile_state == NULL)
    {
        SEGLsfDebug(SEG_LSF_DEBUG_ERROR,
                ("Fatal error: out of memory\n"));
        goto destroy_cond_error;
    }

    rc = globus_l_lsf_increase_buffer(logfile_state);
    if (rc != GLOBUS_SUCCESS)
    {
        SEGLsfDebug(SEG_LSF_DEBUG_ERROR,
            ("Fatal error (out of memory)\n"));
        goto free_logfile_state_error;
    }

    /* Configuration info */
    result = globus_scheduler_event_generator_get_timestamp(
            &logfile_state->start_timestamp);

    if (result != GLOBUS_SUCCESS)
    {
        SEGLsfDebug(SEG_LSF_DEBUG_ERROR,
                ("Fatal error (unable to parse timestamp)\n"));

        goto free_logfile_state_buffer_error;
    }

    if (logfile_state->start_timestamp == 0)
    {
        logfile_state->start_timestamp = time(NULL);
    }

    result = globus_eval_path("${sysconfdir}/globus/globus-lsf.conf", &config_path);
    if (result != GLOBUS_SUCCESS)
    {
        SEGLsfDebug(SEG_LSF_DEBUG_ERROR,
                ("Fatal error (unable to determine config file path)\n"));

        goto free_logfile_state_buffer_error;
    }

    result = globus_common_get_attribute_from_config_file(
            "",
            config_path,
            "log_path",
            &logfile_state->log_dir);
    if (result != GLOBUS_SUCCESS)
    {
        SEGLsfDebug(SEG_LSF_DEBUG_ERROR,
                ("Error retrieving log_path attribute from " 
                 "${sysconfdir}/globus/globus-lsf.conf\n"));
                
        goto free_config_path_error;
    }
    free(config_path);
    config_path = NULL;
    /* Convert timestamp to filename */
    rc = globus_l_lsf_find_logfile(logfile_state);

    if (rc == GLOBUS_SUCCESS)
    {
        logfile_state->fp = fopen(logfile_state->path, "r");

        if (logfile_state->fp == NULL)
        {
            SEGLsfDebug(SEG_LSF_DEBUG_ERROR,
                    ("Error opening %s: %s\n",
                    logfile_state->path,
                    strerror(errno)));
            rc = SEG_LSF_ERROR_OUT_OF_MEMORY;

            goto free_logfile_state_path_error;
        }
    }
    else
    {
        goto free_logfile_state_path_error;
    }
    GlobusTimeReltimeSet(delay, 0, 0);

    result = globus_callback_register_oneshot(
            NULL,
            &delay,
            globus_l_lsf_read_callback,
            logfile_state);

    if (result != GLOBUS_SUCCESS)
    {
        SEGLsfDebug(SEG_LSF_DEBUG_ERROR,
                ("Error registering oneshot: %s\n",
                globus_error_print_friendly(globus_error_peek(result))));
        goto free_logfile_state_path_error;
    }
    callback_count++;

    return 0;

free_logfile_state_path_error:
    if (logfile_state->path)
    {
        free(logfile_state->path);
    }
    if (logfile_state->log_dir)
    {
        free(logfile_state->log_dir);
    }
free_config_path_error:
    if (config_path)
    {
        free(config_path);
    }
free_logfile_state_buffer_error:
    free(logfile_state->buffer);
free_logfile_state_error:
    free(logfile_state);
destroy_cond_error:
    globus_cond_destroy(&globus_l_lsf_cond);
destroy_mutex_error:
    globus_mutex_destroy(&globus_l_lsf_mutex);
deactivate_common_error:
    globus_module_deactivate(GLOBUS_COMMON_MODULE);
error:
    return 1;
}
/* globus_l_lsf_module_activate() */

static
int
globus_l_lsf_module_deactivate(void)
{
    GlobusFuncName(globus_l_lsf_module_deactivate);

    SEGLsfEnter();

    globus_mutex_lock(&globus_l_lsf_mutex);
    shutdown_called = GLOBUS_TRUE;

    while (callback_count > 0)
    {
        globus_cond_wait(&globus_l_lsf_cond, &globus_l_lsf_mutex);
    }
    globus_mutex_unlock(&globus_l_lsf_mutex);

    SEGLsfExit();
    GlobusDebugDestroy(SEG_LSF);

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return 0;
}

/**
 * read_cb:
 *  check to see if lsb.events.index file changed---if so, we must relocate
 *  our position in the appropriate log file.
 *  
 *  if we're ok---parse events buffer
 *
 *  if (!eof)
 *      register read (read_cb)
 *  else
 *      if (it's an old logfile)
 *          register_close(old_close_cb)
 *      else
 *          register poll (wakeup_cb)
 */
static
void
globus_l_lsf_read_callback(
    void *                              user_arg)
{
    int                                 rc;
    globus_l_lsf_logfile_state_t *      state = user_arg;
    size_t                              max_to_read;
    globus_bool_t                       eof_hit = GLOBUS_FALSE;
    globus_reltime_t                    delay;
    globus_result_t                     result;
    struct stat                         s;
    GlobusFuncName(globus_l_lsf_read_callback);

    SEGLsfEnter();

    globus_mutex_lock(&globus_l_lsf_mutex);
    if (shutdown_called)
    {
        SEGLsfDebug(SEG_LSF_DEBUG_INFO, ("polling while deactivating"));

        globus_mutex_unlock(&globus_l_lsf_mutex);
        goto error;
    }
    globus_mutex_unlock(&globus_l_lsf_mutex);

    rc = stat(state->event_idx_path, &s);

    if ((rc == 0 && state->fp != NULL &&
            state->event_idx_stat.st_mtime != s.st_mtime)
        || (rc != 0 && errno != ENOENT))
    {
        SEGLsfDebug(SEG_LSF_DEBUG_INFO,
                ("Log file was rotated since last read\n"));
        /* Log was rotated since we started our read, so we need to
         * figure out what we need to read
         */
        fclose(state->fp);
        state->is_current_file = GLOBUS_FALSE;

        rc = globus_l_lsf_find_logfile(state);

        if (rc == GLOBUS_SUCCESS)
        {
            state->fp = fopen(state->path, "r");

            GlobusTimeReltimeSet(delay, 0, 0);

            result = globus_callback_register_oneshot(
                    NULL,
                    &delay,
                    globus_l_lsf_read_callback,
                    state);
        }
        /* ERROR? */
        return;
    }

    while (state->fp != NULL && !eof_hit)
    {
        /* Read data */
        max_to_read = state->buffer_length - state->buffer_valid
                - state->buffer_point;

        SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                ("reading a maximum of %u bytes\n", max_to_read));

        rc = fread(state->buffer + state->buffer_point + state->buffer_valid,
                1, max_to_read, state->fp);
        
        SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                ("read %d bytes\n", rc));

        if (rc < max_to_read)
        {
            if (feof(state->fp))
            {
                SEGLsfDebug(SEG_LSF_DEBUG_TRACE, ("hit eof\n"));
                eof_hit = GLOBUS_TRUE;
                clearerr(state->fp);
            }
            else
            {
                SEGLsfDebug(SEG_LSF_DEBUG_TRACE, ("read error: log rotated?\n"));
                /* Log was rotated? */
                eof_hit = GLOBUS_TRUE;
            }
        }

        state->buffer_valid += rc;

        if (rc > 0)
        {
            /* Parse data */
            rc = globus_l_lsf_parse_events(state);

            rc = globus_l_lsf_clean_buffer(state);
        }
    }

    /* If end of log and it's not the current log, find a new logfile
     */
    if (eof_hit && !state->is_current_file)
    {
        if ((!ferror(state->fp) &&
            state->start_timestamp <= state->end_of_file_timestamp))
        {
            state->start_timestamp = state->end_of_file_timestamp;
        }
        fclose(state->fp);
        rc = globus_l_lsf_find_logfile(state);

        if (rc == GLOBUS_SUCCESS)
        {
            state->fp = fopen(state->path, "r");

            GlobusTimeReltimeSet(delay, 0, 0);
        }
    }
    else
    {
        GlobusTimeReltimeSet(delay, 2, 0);
    }

    result = globus_callback_register_oneshot(
            NULL,
            &delay,
            globus_l_lsf_read_callback,
            state);
    if (result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    SEGLsfExit();
    return;
error:
    globus_mutex_lock(&globus_l_lsf_mutex);
    if (shutdown_called)
    {
        callback_count--;

        if (callback_count == 0)
        {
            globus_cond_signal(&globus_l_lsf_cond);
        }
    }
    globus_mutex_unlock(&globus_l_lsf_mutex);

    SEGLsfExit();
    return;

}
/* globus_l_lsf_read_callback() */

/**
 * Determine the next available LSF log file name from the 
 * timestamp stored in the logfile state structure.
 * 
 * @param state
 *     LSF log state structure. The path field of the structure may be
 *     modified by this function.
 *
 * @retval GLOBUS_SUCCESS
 *     Name of an log file name has been found and the file exists.
 * @retval 1
 *     Something bad occurred.
 */
static
int
globus_l_lsf_find_logfile(
    globus_l_lsf_logfile_state_t *      state)
{
    struct stat                         s;
    int                                 rc;
    const char                          lsf_log_prefix[] = "lsb.events.";
    const char                          lsf_idx_name[] = "lsb.events.1";
    int                                 i;
    time_t                              most_recent_event;
    long long                           tmp_most_recent_event;
    GlobusFuncName(globus_l_lsf_find_logfile);

    SEGLsfEnter();

    if (state->path == NULL)
    {
        SEGLsfDebug(SEG_LSF_DEBUG_TRACE, ("Allocating path\n"));
        state->path = malloc(strlen(state->log_dir) + sizeof(lsf_log_prefix)
                + 10);

        if (state->path == NULL)
        {
            SEGLsfDebug(SEG_LSF_DEBUG_WARN, ("Out of memory\n"));
            rc = SEG_LSF_ERROR_OUT_OF_MEMORY;
            goto error;
        }
    }
    if (state->event_idx_path == NULL)
    {
        state->event_idx_path = malloc(strlen(state->log_dir)
                + sizeof(lsf_idx_name) + 1);
        if (state->event_idx_path == NULL)
        {
            SEGLsfDebug(SEG_LSF_DEBUG_WARN, ("Out of memory\n"));
            rc = SEG_LSF_ERROR_OUT_OF_MEMORY;
            goto error;
        }
        sprintf(state->event_idx_path, "%s/%s", state->log_dir, lsf_idx_name);
    }

    do
    {
        /* To decide which log file to use, we'll read the 1st line of each
         * historical logfile, and stop when the most_recent_event is
         * before start_timestamp and use the previous one. If lsb.events.1
         * changes between the time we start and end, we'll need to
         * reevaluate
         */
        memset(&state->event_idx_stat, 0, sizeof(struct stat));
        stat(state->event_idx_path, &state->event_idx_stat);

        most_recent_event = (time_t) GLOBUS_I_TIME_INFINITY_SEC;
        for (i = 0; most_recent_event > state->start_timestamp; i++)
        {
            sprintf(state->path, "%s/lsb.events.%d", state->log_dir, i+1);

            state->fp = fopen(state->path, "r");

            if (state->fp == NULL)
            {
                /* We've passed the earliest log file */
                most_recent_event = 0;
            }
            else
            {
                fscanf(state->fp, "#%lld", &tmp_most_recent_event);
                most_recent_event = tmp_most_recent_event;
                fclose(state->fp);
                state->fp = NULL;
            }
        }

        memset(&s, 0, sizeof(struct stat));
        stat(state->event_idx_path, &s);
    } while (state->event_idx_stat.st_mtime != s.st_mtime);

    if (i <= 1)
    {
        /* The main lsb.events file starts before our start event,
         * so we'll use that instead of a historic file.
         */
        sprintf(state->path, "%s/lsb.events", state->log_dir);
        state->is_current_file = GLOBUS_TRUE;
    }
    else
    {
        sprintf(state->path, "%s/lsb.events.%d", state->log_dir, i-1);
        state->is_current_file = GLOBUS_FALSE;
    }

    SEGLsfDebug(SEG_LSF_DEBUG_INFO,
            ("globus_l_lsf_find_logfile() exits w/out error\n"));
    return 0;

error:
    SEGLsfDebug(SEG_LSF_DEBUG_WARN,
            ("globus_l_lsf_find_logfile() exits w/error\n"));
    return rc;
}
/* globus_l_lsf_find_logfile() */

/**
 * Move any data in the state buffer to the beginning, to enable reusing 
 * buffer space which has already been parsed.
 */
static
int
globus_l_lsf_clean_buffer(
    globus_l_lsf_logfile_state_t *      state)
{
    int                                 rc = 0;
    SEGLsfDebug(SEG_LSF_DEBUG_INFO,
            ("globus_l_lsf_clean_buffer() called\n"));

    /* move data to head of buffer */
    if (state->buffer != NULL)
    {
        if(state->buffer_point > 0)
        {
            if (state->buffer_valid > 0)
            {
                memmove(state->buffer,
                        state->buffer+state->buffer_point,
                        state->buffer_valid);
            }
            state->buffer_point = 0;
        }
        rc = globus_l_lsf_increase_buffer(state);
    }
    SEGLsfDebug(SEG_LSF_DEBUG_INFO,
            ("globus_l_lsf_clean_buffer() exits\n"));
    return rc;
}
/* globus_l_lsf_clean_buffer() */

/**
 * Reduce unused space in the log buffer, increasing the size of the buffer
 * if it is full.
 *
 * @param state
 *     LSF log state structure. The buffer-related fields of the structure
 *     may be modified by this function.
 */
static
int
globus_l_lsf_increase_buffer(
    globus_l_lsf_logfile_state_t *      state)
{
    char *                              save = state->buffer;
    const size_t                        GLOBUS_LSF_READ_BUFFER_SIZE = 4096;
    int                                 rc;

    SEGLsfDebug(SEG_LSF_DEBUG_INFO,
            ("globus_l_lsf_increase_buffer() called\n"));
    /* If the buffer is full, resize */
    if (state->buffer_valid == state->buffer_length)
    {
        state->buffer = realloc(state->buffer,
                    state->buffer_length + GLOBUS_LSF_READ_BUFFER_SIZE);
        if (state->buffer == NULL)
        {
            SEGLsfDebug(SEG_LSF_DEBUG_ERROR, ("realloc() failed\n"));

            rc = SEG_LSF_ERROR_OUT_OF_MEMORY;
            goto error;
        }
        state->buffer_length += GLOBUS_LSF_READ_BUFFER_SIZE;
    }

    SEGLsfDebug(SEG_LSF_DEBUG_INFO,
            ("globus_l_lsf_increase_buffer() exits w/success\n"));
    return 0;

error:
    SEGLsfDebug(SEG_LSF_DEBUG_WARN,
            ("globus_l_lsf_increase_buffer() exits w/failure\n"));
    state->buffer = save;
    return rc;
}
/* globus_l_lsf_increase_buffer() */

static
int
globus_l_lsf_parse_events(
    globus_l_lsf_logfile_state_t *      state)
{
    char *                              eol;
    time_t                              event_timestamp;
    long long                           tmp_timestamp;
    char                                event_type_buffer[64];
    char                                job_id_buffer[32];
    int                                 rc;
    int                                 job_status;
    int                                 exit_status;
    long                                offset;
    SEGLsfDebug(SEG_LSF_DEBUG_INFO,
            ("globus_l_lsf_parse_events() called\n"));

    while ((eol = memchr(state->buffer + state->buffer_point,
                '\n',
                state->buffer_valid)) != NULL)
    {
        *eol = '\0';

        SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                ("parsing line %s\n", state->buffer + state->buffer_point));

        if (state->buffer[state->buffer_point] == '#')
        {
            /* Parse first line of log file */
            if (state->is_current_file)
            {
                /* If this is lsb.events, then the first line contains
                 * the offset to where events not in an old log file are
                 * to be found in this file
                 */
                sscanf(state->buffer + state->buffer_point + 1, "%ld", &offset);
                fseek(state->fp, offset, SEEK_SET);

                /* don't bother parsing the rest of our read, it's junk */
                state->buffer_point = 0;
                state->buffer_valid = 0;
                break;
            }
            else
            {
                /* If this is one of the lsb.events.N files, then the first
                 * line is the last timestamp covered by this file... it might
                 * be bigger than the last event in the file, so we will
                 * the parsing timestamp to match that when we hit EOF.
                 */
                sscanf(state->buffer + state->buffer_point + 1,
                    "%lld", &tmp_timestamp);
                state->end_of_file_timestamp = tmp_timestamp;
                goto next_line;
            }
        }
        sscanf(state->buffer + state->buffer_point,
                "\"%[^\"]\" \"%*[^\"]\" %lld %s",
                event_type_buffer,
                &tmp_timestamp,
                job_id_buffer);
        event_timestamp = tmp_timestamp;

        if (event_timestamp < state->start_timestamp)
        {
            goto next_line;
        }
        if (!strcmp(event_type_buffer, "JOB_NEW"))
        {
            if (event_timestamp >= state->start_timestamp)
            {
                rc = globus_scheduler_event_pending(event_timestamp,
                        job_id_buffer);
                state->start_timestamp = event_timestamp;
            }
        }
        else if (!strcmp(event_type_buffer, "JOB_START"))
        {
            if (event_timestamp >= state->start_timestamp)
            {
                rc = globus_scheduler_event_active(event_timestamp,
                        job_id_buffer);

                state->start_timestamp = event_timestamp;
            }
        }
        else if (!strcmp(event_type_buffer, "JOB_STATUS"))
        {
            sscanf(state->buffer + state->buffer_point,
                    "\"JOB_STATUS\" \"%*[^\"]\" %*d %*s %d",
                    &job_status);

            switch (job_status)
            {
            case JOB_STAT_PEND: /* IGNORE */
                /*
                 * The job is pending, that is, it has not yet been started.
                 */

                SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                        ("ignoring JOB_STATUS: job %s in PEND state (%lld)\n",
                        job_id_buffer, (long long) event_timestamp));
                break;
            case JOB_STAT_PSUSP:
                /*
                 * The  job  has  been  suspended,  either  by its owner or
                 * the LSF administrator, while pending.
                 */
                SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                        ("ignoring JOB_STATUS: job %s in PSUSP state (%lld)\n",
                        job_id_buffer, (long long) event_timestamp));
                break;
            case JOB_STAT_RUN:
                /*
                 * the job is currently running.
                 */
                SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                        ("ignoring JOB_STATUS: job %s in RUN state (%lld)\n",
                        job_id_buffer, (long long) event_timestamp));
                break;

            case JOB_STAT_SSUSP:
                /*
                 * The job has been suspended by LSF. The job has been
                 * suspended by LSF due to either of the following two
                 * causes:
                 * - The load conditions on the execution host or hosts have
                 *   exceeded  a threshold  according  to the loadStop vector
                 *   defined for the host or queue.
                 * - The run window  of  the  job's  queue  is  closed.  See
                 *   bqueues(1),
                 */
                SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                        ("ignoring JOB_STATUS: job %s in SSUSP state (%lld)\n",
                        job_id_buffer, (long long) event_timestamp));
                break;
            case JOB_STAT_USUSP:
                /*
                 * The job has been suspended, either  by  its  owner  or
                 * the  LSF administrator, while running.
                 */
                SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                        ("ignoring JOB_STATUS: job %s in SSUSP state (%lld)\n",
                        job_id_buffer, (long long) event_timestamp));
                break;
            case JOB_STAT_EXIT:
                /*
                 * The  job  has terminated with a non-zero status - it may
                 * have been aborted due to an error in its execution, or
                 * killed  by its owner or the LSF administrator.
                 */

                if (event_timestamp >= state->start_timestamp)
                {
                    char * tmp = eol-1;
                    while (isdigit(*tmp))
                    {
                        tmp--;
                    }
                    sscanf(tmp, " %d", &exit_status);
                    tmp--;
                    while (isdigit(*tmp))
                    {
                        tmp--;
                    }
                    tmp--;
                    while (isdigit(*tmp))
                    {
                        tmp--;
                    }

                    if (exit_status == 0)
                    {
                        sscanf(tmp, " %d", &exit_status);
                        exit_status = (exit_status & 0xff00) >> 8;
                        rc = globus_scheduler_event_done(event_timestamp,
                                job_id_buffer,
                                exit_status);
                    }
                    else
                    {
                        rc = globus_scheduler_event_failed(event_timestamp,
                                job_id_buffer,
                                exit_status);
                    }
                    state->start_timestamp = event_timestamp;
                }
                break;
            case JOB_STAT_DONE:
                /*
                 * The job has terminated with status of 0.
                 */
                if (event_timestamp >= state->start_timestamp)
                {
                    rc = globus_scheduler_event_done(event_timestamp,
                            job_id_buffer,
                            0);
                    state->start_timestamp = event_timestamp;
                }
                break;
            case JOB_STAT_PDONE:
                /*
                 * Post job process done successfully
                 */
                SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                        ("ignoring JOB_STATUS: job %s in PDONE state (%lld)\n",
                        job_id_buffer, (long long) event_timestamp));
                break;
            case JOB_STAT_PERR:
                /*
                 * Post job process has error
                 */
                SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                        ("ignoring JOB_STATUS: job %s in PERR state (%lld)\n",
                        job_id_buffer, (long long) event_timestamp));
                break;
            case JOB_STAT_WAIT:
                /*
                 * For  jobs  submitted to a chunk job queue, members of a
                 * chunk job that are waiting to run.
                 */
                SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                        ("ignoring JOB_STATUS: job %s in WAIT state (%lld)\n",
                        job_id_buffer, (long long) event_timestamp));
                break;
            case JOB_STAT_UNKWN:
                SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                        ("ignoring JOB_STATUS: job %s in UNKNWN state "
                                "(%lld)\n",
                        job_id_buffer, (long long) event_timestamp));
                break;
            case JOB_STAT_NULL:
                SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                        ("ignoring JOB_STATUS: job %s in NULL state (%lld)\n",
                        job_id_buffer, (long long) event_timestamp));
                break;
            }
        }
        else
        {
            SEGLsfDebug(SEG_LSF_DEBUG_TRACE,
                    ("ignoring line: %s",
                    state->buffer + state->buffer_point));
        }
next_line:
        state->buffer_valid -= eol + 1 - state->buffer - state->buffer_point;
        state->buffer_point = eol + 1 - state->buffer;
        if (event_timestamp >= state->start_timestamp)
        {
            state->start_timestamp = event_timestamp;
        }
    }

    SEGLsfDebug(SEG_LSF_DEBUG_INFO,
            ("globus_l_lsf_parse_events() exits\n"));
    return 0;
}
/* globus_l_lsf_parse_events() */
