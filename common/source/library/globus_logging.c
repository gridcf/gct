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

#include "globus_i_common_config.h"
#include "globus_logging.h"
#include "globus_common.h"

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#define GLOBUS_L_LOGGING_MAX_MESSAGE  2048

#ifdef __GNUC__
#define GlobusLoggingName(func) static const char * _globus_logging_name __attribute__((__unused__)) = #func
#else
#define GlobusLoggingName(func) static const char * _globus_logging_name = #func
#endif
static int                              globus_l_logging_pid;
/*
 *  error types
 */
#define GlobusLoggingErrorParameter(param_name)                       \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_COMMON_MODULE,                                           \
            GLOBUS_NULL,                                                    \
            GLOBUS_LOGGING_ERROR_PARAMETER,                                 \
            __FILE__,                                                       \
            _globus_logging_name,                                           \
            __LINE__,                                                       \
            "Bad parameter, %s",                                            \
            (param_name)))

#define GlobusLoggingMemory()                                         \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_COMMON_MODULE,                                           \
            GLOBUS_NULL,                                                    \
            GLOBUS_LOGGING_ERROR_ALLOC,                                     \
            __FILE__,                                                       \
            _globus_logging_name,                                           \
            __LINE__,                                                       \
            "Out of memory"))

typedef struct globus_l_logging_handle_s
{
    globus_mutex_t                      mutex;
    int                                 type_mask;
    globus_size_t                       buffer_length;
    globus_size_t                       used_length;
    void *                              user_arg;
    globus_callback_handle_t            callback_handle;
    globus_logging_module_t             module;
    globus_bool_t                       periodic_running;
    globus_byte_t                       buffer[1];
} globus_l_logging_handle_t;

/*
 *  flush the buffer
 */
static void
globus_l_logging_flush(
    globus_l_logging_handle_t *         handle)
{
    if(handle->used_length > 0)
    {
        handle->module.write_func(
            handle->buffer, handle->used_length, handle->user_arg);
    }
    handle->used_length = 0;
}

/*
 *  unregister callback.  clean up happens here
 */
static void
globus_l_logging_unregister(
    void *                              user_arg)
{
    globus_l_logging_handle_t *         handle;

    handle = (globus_l_logging_handle_t *) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        if(handle->module.close_func != NULL)
        {
            handle->module.close_func(handle->user_arg);
        }
    }
    globus_mutex_unlock(&handle->mutex);

    globus_mutex_destroy(&handle->mutex);
    globus_free(handle);
}

/*
 *  callback for delayed logging, just lock and flush.
 */
static void
globus_l_logging_periodic(
    void *                              user_arg)
{
    globus_l_logging_handle_t *         handle;

    handle = (globus_l_logging_handle_t *) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        globus_l_logging_flush(handle);
    }
    globus_mutex_unlock(&handle->mutex);
}

/**
 * Reset the cached version of the pid used for logging. Call this after
 * fork() to keep logging working in a child process
 */
void
globus_logging_update_pid(void)
{
    globus_l_logging_pid = getpid();
}
/* globus_logging_update_pid() */

/*
 *  external functions
 */
globus_result_t
globus_logging_init(
    globus_logging_handle_t *           out_handle,
    globus_reltime_t *                  flush_period,
    int                                 buffer_length_in,
    int                                 log_type,
    globus_logging_module_t *           module,
    void *                              user_arg)
{
    globus_result_t                     res;
    globus_l_logging_handle_t *         handle;
    globus_size_t                       buffer_length;
    globus_reltime_t                    zero;
    GlobusLoggingName(globus_logging_init);

    if(out_handle == NULL)
    {
        res = GlobusLoggingErrorParameter("out_handle");
        goto err;
    }
    if(buffer_length_in < 0)
    {
        res = GlobusLoggingErrorParameter("buffer_length");
        goto err;
    }

    buffer_length = buffer_length_in;
    if(buffer_length_in < GLOBUS_L_LOGGING_MAX_MESSAGE)
    {
        buffer_length = GLOBUS_L_LOGGING_MAX_MESSAGE;
    }

    if(module == NULL || module->write_func == NULL)
    {
        res = GlobusLoggingErrorParameter("module");
        goto err;
    }

    handle = (globus_l_logging_handle_t *)
        globus_malloc(sizeof(globus_l_logging_handle_t) + buffer_length - 1);
    if(handle == NULL)
    {
        res = GlobusLoggingMemory();
        goto err;
    }

    globus_l_logging_pid = getpid();

    handle->module.open_func = module->open_func;
    handle->module.write_func = module->write_func;
    handle->module.close_func = module->close_func;
    handle->module.header_func = module->header_func;

    globus_mutex_init(&handle->mutex, NULL);
    handle->type_mask = log_type;
    handle->buffer_length = buffer_length;
    handle->used_length = 0;
    handle->user_arg = user_arg;

    if(handle->module.open_func)
    {
        handle->module.open_func(handle->user_arg);
    }

    GlobusTimeReltimeSet(zero, 0, 0);
    if(flush_period != NULL && globus_reltime_cmp(flush_period, &zero) != 0)
    {
        res = globus_callback_register_periodic(
            &handle->callback_handle,
            flush_period,
            flush_period,
            globus_l_logging_periodic,
            handle);
        if(res != GLOBUS_SUCCESS)
        {
            goto err;
        }
        handle->periodic_running = GLOBUS_TRUE;
    }
    else
    {
        /* insist that all are inline */
        handle->type_mask |= GLOBUS_LOGGING_INLINE;
        handle->periodic_running = GLOBUS_FALSE;
    }
    *out_handle = handle;

    return GLOBUS_SUCCESS;

  err:

    return res;
}
globus_result_t
globus_logging_vwrite(
    globus_logging_handle_t             handle,
    int                                 type,
    const char *                        fmt,
    va_list                             ap)
{
    globus_result_t                     res;
    globus_size_t                       remain;
    globus_size_t                       nbytes;
    int                                 rc;
    GlobusLoggingName(globus_logging_write);

    if(handle == NULL)
    {
        res = GlobusLoggingErrorParameter("handle");
        goto err;
    }
    if(fmt == NULL)
    {
        res = GlobusLoggingErrorParameter("fmt");
        goto err;
    }

    globus_mutex_lock(&handle->mutex);
    {
        if(type & handle->type_mask)
        {
            remain = handle->buffer_length - handle->used_length;
            if(remain < GLOBUS_L_LOGGING_MAX_MESSAGE)
            {
                globus_l_logging_flush(handle);
                remain = handle->buffer_length;
            }
            if(handle->module.header_func != NULL)
            {
                nbytes = remain;
                handle->module.header_func(
                    (char *) &handle->buffer[handle->used_length],
                    &nbytes);
                handle->used_length += nbytes;
                remain -= nbytes;
            }
            rc = vsnprintf((char *) &handle->buffer[handle->used_length],
                remain, fmt, ap);
            if (rc < 0)
            {
                nbytes = 0;
            }
            else
            {
                nbytes = rc;
            }
            if(nbytes > remain)
            {
                char                    suffix[64];

                globus_libc_snprintf(
                    suffix,
                    sizeof(suffix),
                    " *** TRUNCATED %lu bytes\n",
                    (unsigned long) (nbytes - remain + sizeof(suffix)));

                memcpy(
                    &handle->buffer[handle->buffer_length - sizeof(suffix)],
                    suffix,
                    sizeof(suffix));

                nbytes = remain - sizeof(suffix) + strlen(suffix);
            }
            handle->used_length += nbytes;
            remain -= nbytes;

            if(type & GLOBUS_LOGGING_INLINE ||
                handle->type_mask & GLOBUS_LOGGING_INLINE ||
                remain < GLOBUS_L_LOGGING_MAX_MESSAGE)
            {
                globus_l_logging_flush(handle);
            }
        }
    }
    globus_mutex_unlock(&handle->mutex);

    return GLOBUS_SUCCESS;

  err:
    return res;
}

globus_result_t
globus_logging_write(
    globus_logging_handle_t             handle,
    int                                 type,
    const char *                        fmt,
    ...)
{
    va_list                             ap;
    globus_result_t                     res;
    va_start(ap, fmt);
    res = globus_logging_vwrite(handle, type, fmt, ap);
    va_end(ap);

    return res;
}

globus_result_t
globus_logging_flush(
    globus_logging_handle_t             handle)
{
    GlobusLoggingName(globus_logging_flush);

    globus_mutex_lock(&handle->mutex);
    {
        globus_l_logging_flush(handle);
    }
    globus_mutex_unlock(&handle->mutex);

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_logging_destroy(
    globus_logging_handle_t             handle)
{
    globus_result_t                     res;
    GlobusLoggingName(globus_logging_destroy);

    if(handle == NULL)
    {
        res = GlobusLoggingErrorParameter("handle");
        goto err;
    }

    globus_mutex_lock(&handle->mutex);
    {
        globus_l_logging_flush(handle);

        if(handle->periodic_running)
        {
            res = globus_callback_unregister(
                handle->callback_handle,
                globus_l_logging_unregister,
                handle,
                NULL);
            if(res != GLOBUS_SUCCESS)
            {
                globus_mutex_unlock(&handle->mutex);
                goto err;
            }
        }
    }
    globus_mutex_unlock(&handle->mutex);

    return GLOBUS_SUCCESS;

  err:
    return res;
}

void
globus_logging_stdio_write_func(
    globus_byte_t *                     buf,
    globus_size_t                       length,
    void *                              user_arg)
{
    FILE *                              fptr;

    fptr = (FILE *) user_arg;

    fwrite(buf, length, 1, fptr);
}

void
globus_logging_stdio_header_func(
    char *                              buf,
    globus_size_t *                     len)
{
    char                                str[256];
    char *                              tmp;
    time_t                              tm;
    globus_size_t                       str_len;
    int                                 nbytes;

    tm = time(NULL);
    tmp = globus_libc_ctime_r(&tm, str, sizeof(str));
    str_len = strlen(str);
    if(str[str_len - 1] == '\n')
    {
        str[str_len - 1] = '\0';
    }
    nbytes = snprintf(buf, *len, "[%d] %s :: ", globus_l_logging_pid, str);

    if(nbytes < 0)
    {
        *len = 0;
    }
    else if(nbytes < *len)
    {
        *len = nbytes;
    }
}

void
globus_logging_ng_header_func(
    char *                              buf,
    globus_size_t *                     len)
{
    struct timeval                      tv;
    struct tm                           tm;
    int                                 nbytes;

    if(gettimeofday(&tv, NULL) == 0)
    {
        globus_libc_gmtime_r(&tv.tv_sec, &tm);
        nbytes = snprintf(buf, *len, "ts=%04d-%02d-%02dT%02d:%02d:%02d.%06dZ id=%d ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec , (int) tv.tv_usec,
            globus_l_logging_pid);
    }
    else
    {
        nbytes = snprintf(buf, *len, "ts=0000-00-00T00:00:00.000000Z id=%d ",
            globus_l_logging_pid);
    }
    if(nbytes < 0)
    {
        *len = 0;
    }
    if(nbytes < *len)
    {
        *len = nbytes;
    }
}

#ifdef HAVE_SYSLOG_H
void
globus_logging_syslog_open_func(
    void *                              user_arg)
{
    openlog(NULL, LOG_PID, LOG_USER);
}

void
globus_logging_syslog_close_func(
    void *                              user_arg)
{
    closelog();
}

void
globus_logging_syslog_write_func(
    globus_byte_t *                     buf,
    globus_size_t                       length,
    void *                              user_arg)
{
    syslog(LOG_NOTICE, "%s", (char *) buf);
}
#endif

globus_logging_module_t                 globus_logging_stdio_module =
{
    NULL,
    globus_logging_stdio_write_func,
    NULL,
    globus_logging_stdio_header_func
};

globus_logging_module_t                 globus_logging_stdio_ng_module =
{
    NULL,
    globus_logging_stdio_write_func,
    NULL,
    globus_logging_ng_header_func
};

globus_logging_module_t                 globus_logging_syslog_module =
{
#ifdef HAVE_SYSLOG_H
    globus_logging_syslog_open_func,
    globus_logging_syslog_write_func,
    globus_logging_syslog_close_func,
    NULL
#else
    NULL,
    NULL,
    NULL,
    NULL
#endif
};

globus_logging_module_t                 globus_logging_syslog_ng_module =
{
#ifdef HAVE_SYSLOG_H
    globus_logging_syslog_open_func,
    globus_logging_syslog_write_func,
    globus_logging_syslog_close_func,
    globus_logging_ng_header_func
#else
    NULL,
    NULL,
    NULL,
    NULL
#endif
};
