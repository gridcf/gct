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
#include "globus_scheduler_event_generator.h"
#include "globus_scheduler_event_generator_app.h"
#include "globus_gram_protocol.h"
#include "version.h"

enum
{
    GLOBUS_SEG_INPUT_BUFFER_SIZE = 256
};

static
globus_result_t
globus_l_seg_register_write(
    globus_byte_t *                     buf);

static
globus_result_t
globus_l_stdout_scheduler_event(
    const char *                        format,
    ...);

static
void
globus_l_seg_writev_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

static
void
globus_l_xio_read_eof_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

static
int
globus_l_seg_stdout_activate(void);

static
int
globus_l_seg_stdout_deactivate(void);

static globus_mutex_t                   globus_l_seg_mutex;
static globus_cond_t                    globus_l_seg_cond;
static globus_xio_handle_t              globus_l_seg_output_handle;
static globus_xio_handle_t              globus_l_seg_input_handle;
static globus_xio_stack_t               globus_l_seg_file_stack;
static globus_xio_driver_t              globus_l_seg_file_driver;
static globus_byte_t *                  globus_l_seg_input_buffer;
static size_t                           globus_l_seg_input_buffer_size;
static time_t                           globus_l_seg_timestamp;
static globus_fifo_t                    globus_l_seg_buffers;
static globus_bool_t                    globus_l_seg_write_registered;
static int                              globus_l_seg_shutdown;

globus_module_descriptor_t
globus_i_scheduler_event_generator_stdout_module =
{
    "globus_scheduler_event_generator_stdout",
    globus_l_seg_stdout_activate,
    globus_l_seg_stdout_deactivate,
    NULL,
    NULL,
    &local_version,
    NULL
};

globus_result_t
globus_scheduler_event_generator_stdout_handler(
    void *                              user_arg,
    const globus_scheduler_event_t *    event)
{
    switch (event->event_type)
    {
    case GLOBUS_SCHEDULER_EVENT_PENDING:
        return globus_l_stdout_scheduler_event(
                "001;%lld;%s;%d;%d\n",
                (long long) event->timestamp,
                event->job_id,
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING,
                0);

    case GLOBUS_SCHEDULER_EVENT_ACTIVE:
        return globus_l_stdout_scheduler_event(
                "001;%lld;%s;%d;%d\n",
                (long long) event->timestamp,
                event->job_id,
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE,
                0);

    case GLOBUS_SCHEDULER_EVENT_DONE:
        return globus_l_stdout_scheduler_event(
                "001;%lld;%s;%d;%d\n",
                (long long) event->timestamp,
                event->job_id,
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE,
                event->exit_code);

    case GLOBUS_SCHEDULER_EVENT_FAILED:
        return globus_l_stdout_scheduler_event(
                "001;%lld;%s;%d;%d\n",
                (long long) event->timestamp,
                event->job_id,
                GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED,
                event->failure_code);

    case GLOBUS_SCHEDULER_EVENT_RAW:
        return globus_l_stdout_scheduler_event(
                event->raw_event);
    }
    return GLOBUS_FAILURE;
}
/* globus_scheduler_event_generator_stdout_handler() */

static
int
globus_l_seg_stdout_activate(void)
{
    globus_result_t                     result;
    globus_xio_attr_t                   out_attr;
    globus_xio_attr_t                   in_attr;
    int                                 rc;

    globus_l_seg_output_handle = NULL;
    globus_l_seg_input_handle = NULL;
    globus_l_seg_file_stack = NULL;
    globus_l_seg_file_driver = NULL;
    globus_l_seg_timestamp = 0;
    globus_l_seg_write_registered = GLOBUS_FALSE;
    globus_l_seg_shutdown = 0;

    rc = globus_module_activate(GLOBUS_COMMON_MODULE);

    if (rc != GLOBUS_SUCCESS)
    {
        goto error;
    }

    rc = globus_module_activate(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE);

    if (rc != GLOBUS_SUCCESS)
    {
        goto deactivate_common_error;
    }

    rc = globus_fifo_init(&globus_l_seg_buffers);
    if (rc != GLOBUS_SUCCESS)
    {
        goto deactivate_seg_error;
    }

    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto destroy_fifo_error;
    }

    result = globus_xio_driver_load("file", &globus_l_seg_file_driver);
    if (result != GLOBUS_SUCCESS)
    {
        goto deactivate_xio_error;
    }
    result = globus_xio_stack_init(&globus_l_seg_file_stack, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        goto unload_driver_error;
    }
    result = globus_xio_stack_push_driver(globus_l_seg_file_stack,
            globus_l_seg_file_driver);
    if (result != GLOBUS_SUCCESS)
    {
        goto destroy_stack_error;
    }

    result = globus_xio_attr_init(&out_attr);
    if (result != GLOBUS_SUCCESS)
    {
        goto destroy_stack_error;
    }

    result = globus_xio_attr_cntl(
            out_attr,
            globus_l_seg_file_driver,
            GLOBUS_XIO_FILE_SET_FLAGS,
            GLOBUS_XIO_FILE_WRONLY);
    if (result != GLOBUS_SUCCESS)
    {
        goto destroy_out_attr_error;
    }

    result = globus_xio_attr_cntl(
            out_attr,
            globus_l_seg_file_driver,
            GLOBUS_XIO_FILE_SET_HANDLE,
            fileno(stdout));

    if (result != GLOBUS_SUCCESS)
    {
        goto destroy_out_attr_error;
    }

    result = globus_xio_attr_init(&in_attr);
    if (result != GLOBUS_SUCCESS)
    {
        goto destroy_out_attr_error;
    }

    result = globus_xio_attr_cntl(
            in_attr,
            globus_l_seg_file_driver,
            GLOBUS_XIO_FILE_SET_FLAGS,
            GLOBUS_XIO_FILE_RDONLY);

    if (result != GLOBUS_SUCCESS)
    {
        goto destroy_in_attr_error;
    }
    result = globus_xio_attr_cntl(
            in_attr,
            globus_l_seg_file_driver,
            GLOBUS_XIO_FILE_SET_HANDLE,
            fileno(stdin));

    if (result != GLOBUS_SUCCESS)
    {
        goto destroy_in_attr_error;
    }

    result = globus_xio_handle_create(
            &globus_l_seg_output_handle,
            globus_l_seg_file_stack);
    if (result != GLOBUS_SUCCESS)
    {
        goto destroy_in_attr_error;
    }

    result = globus_xio_open(globus_l_seg_output_handle, "", out_attr);
    if (result != GLOBUS_SUCCESS)
    {
        goto close_out_handle_error;
    }

    result = globus_xio_handle_create(
            &globus_l_seg_input_handle,
            globus_l_seg_file_stack);
    if (result != GLOBUS_SUCCESS)
    {

        goto close_out_handle_error;
    
    }

    result = globus_xio_open(globus_l_seg_input_handle, "", in_attr);
    if (result != GLOBUS_SUCCESS)
    {
        goto close_in_handle_error;
    }
    rc = globus_mutex_init(&globus_l_seg_mutex, NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        goto close_in_handle_error;
    }
    rc = globus_cond_init(&globus_l_seg_cond, NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        goto destroy_mutex_error;
    }

    globus_l_seg_input_buffer_size = GLOBUS_SEG_INPUT_BUFFER_SIZE;
    globus_l_seg_input_buffer = malloc(globus_l_seg_input_buffer_size);

    result = globus_xio_register_read(
            globus_l_seg_input_handle,
            globus_l_seg_input_buffer,
            globus_l_seg_input_buffer_size,
            1,
            NULL,
            globus_l_xio_read_eof_callback,
            NULL);

    if (result != GLOBUS_SUCCESS)
    {
        goto destroy_cond_error;
    }

    globus_xio_attr_destroy(in_attr);
    globus_xio_attr_destroy(out_attr);

    return 0;

destroy_cond_error:
    globus_cond_destroy(&globus_l_seg_cond);
destroy_mutex_error:
    globus_mutex_destroy(&globus_l_seg_mutex);
close_in_handle_error:
    globus_xio_close(globus_l_seg_input_handle, NULL);
close_out_handle_error:
    globus_xio_close(globus_l_seg_output_handle, NULL);
destroy_in_attr_error:
    globus_xio_attr_destroy(in_attr);
destroy_out_attr_error:
    globus_xio_attr_destroy(out_attr);
destroy_stack_error:
    globus_xio_stack_destroy(globus_l_seg_file_stack);
unload_driver_error:
    globus_xio_driver_unload(globus_l_seg_file_driver);
deactivate_xio_error:
    globus_module_deactivate(GLOBUS_XIO_MODULE);
destroy_fifo_error:
    globus_fifo_destroy(&globus_l_seg_buffers);
deactivate_seg_error:
    globus_module_deactivate(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE);
deactivate_common_error:
    globus_module_deactivate(GLOBUS_COMMON_MODULE);
error:
    return 1;
}
/* globus_l_seg_activate() */

static
int
globus_l_seg_stdout_deactivate(void)
{
    globus_mutex_lock(&globus_l_seg_mutex);
    globus_l_seg_shutdown = 1;

    if (globus_l_seg_write_registered)
    {
        while (globus_l_seg_shutdown == 1)
        {
            globus_cond_wait(&globus_l_seg_cond, &globus_l_seg_mutex);
        }
    }
    globus_fifo_destroy(&globus_l_seg_buffers);
    globus_mutex_unlock(&globus_l_seg_mutex);

    globus_xio_close(globus_l_seg_output_handle, NULL);
    globus_xio_close(globus_l_seg_input_handle, NULL);

    globus_mutex_destroy(&globus_l_seg_mutex);
    globus_cond_destroy(&globus_l_seg_cond);

    globus_module_deactivate(GLOBUS_XIO_MODULE);
    globus_module_deactivate(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE);
    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return 0;
}
/* globus_l_seg_deactivate() */

/**
 * Send an arbitrary SEG notification.
 * @ingroup seg_api
 *
 * @param format
 *     Printf-style format of the SEG notification message
 * @param ...
 *     Varargs which will be interpreted as per format.
 *
 * @retval GLOBUS_SUCCESS
 *     Scheduler message sent or queued.
 * @retval GLOBUS_SEG_ERROR_NULL
 *     Null format.
 * @retval GLOBUS_SEG_ERROR_INVALID_FORMAT
 *     Unable to determine length of formatted string.
 */
static
globus_result_t
globus_l_stdout_scheduler_event(
    const char *                        format,
    ...)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char *                              buf;
    va_list                             ap;
    int                                 length;

    if (format == NULL)
    {
        result = GLOBUS_SEG_ERROR_NULL;

        goto error;
    }

    va_start(ap, format);
    length = globus_libc_vprintf_length(format, ap);
    va_end(ap);

    if (length <= 0)
    {
        result = GLOBUS_SEG_ERROR_INVALID_FORMAT(format);

        goto error;
    }

    buf = globus_libc_malloc(length+1);
    if (buf == NULL)
    {
        result = GLOBUS_SEG_ERROR_OUT_OF_MEMORY;

        goto error;
    }

    va_start(ap, format);
    vsprintf(buf, format, ap);
    va_end(ap);

    globus_mutex_lock(&globus_l_seg_mutex);
    result = globus_l_seg_register_write((globus_byte_t *) buf);
    globus_mutex_unlock(&globus_l_seg_mutex);

error:
    return result;
}
/* globus_scheduler_event() */

static
void
globus_l_xio_read_eof_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_byte_t *                     tmp;
    ptrdiff_t                           offset;
    long long                           stamp;
    globus_byte_t                      *next, *prev, *end;

    if (nbytes == len)
    {
        offset = buffer - globus_l_seg_input_buffer;

        len += globus_l_seg_input_buffer_size;
        globus_l_seg_input_buffer_size *= 2;
        tmp = realloc(globus_l_seg_input_buffer,
                      globus_l_seg_input_buffer_size * 2);
        globus_assert (tmp != NULL);
        globus_l_seg_input_buffer = tmp;
        
        buffer = globus_l_seg_input_buffer + offset;
    }

    end = buffer + nbytes;
    *(end+1) = 0;

    prev = globus_l_seg_input_buffer;
    next = globus_l_seg_input_buffer;

    while (NULL != (next = (globus_byte_t *) strchr((char *) next, '\n')))
    {
        *next = '\0';
        if (sscanf((char *) prev, "200 %lld", &stamp) == 1)
        {
            globus_scheduler_event_generator_set_timestamp((time_t) stamp);
        }
        next++;
        prev = next;
    }

    if (prev != globus_l_seg_input_buffer)
    {
        memmove(globus_l_seg_input_buffer,
                prev,
                end - prev); 
    }

    if (result == GLOBUS_SUCCESS)
    {
        result = globus_xio_register_read(
                globus_l_seg_input_handle,
                globus_l_seg_input_buffer + (end - prev),
                globus_l_seg_input_buffer_size - (end - prev),
                1,
                NULL,
                globus_l_xio_read_eof_callback,
                NULL);
        return;
    }

    globus_scheduler_event_generator_fault(result);
}
/* globus_l_xio_read_eof_callback() */

static
globus_result_t
globus_l_seg_register_write(
    globus_byte_t *                     buf)
{
    globus_result_t                     result;
    size_t                              cnt;
    globus_xio_iovec_t *                iov;
    size_t                              nbytes=0;
    int                                 i;

    if (buf)
    {
        globus_fifo_enqueue(&globus_l_seg_buffers, buf);
    }

    cnt = globus_fifo_size(&globus_l_seg_buffers);
    if ((!globus_l_seg_write_registered) && cnt > 0)
    {

        iov = globus_libc_calloc(cnt, sizeof(globus_xio_iovec_t));
        if (iov == NULL)
        {
            result = GLOBUS_SEG_ERROR_OUT_OF_MEMORY;

            goto call_fault_handler;
        }

        for (i = 0; i < cnt; i++)
        {
            iov[i].iov_base = globus_fifo_dequeue(&globus_l_seg_buffers);
            iov[i].iov_len = strlen((char *)iov[i].iov_base);
            nbytes += iov[i].iov_len;
        }

        result = globus_xio_register_writev(
                globus_l_seg_output_handle,
                iov,
                cnt,
                nbytes,
                NULL,
                globus_l_seg_writev_callback,
                NULL);

        if (result != GLOBUS_SUCCESS)
        {
            goto call_fault_handler;
        }
        globus_l_seg_write_registered = GLOBUS_TRUE;
    }

    return GLOBUS_SUCCESS;

call_fault_handler:
    globus_scheduler_event_generator_fault(result);

    return result;
}
/* globus_l_seg_register_write() */

/**
 * Callback for writing event to scheduler.
 *
 * @param handle
 * @param result
 * @param iovec
 * @param count
 * @param nbytes
 * @param data_desc
 * @param user_arg
 */
static
void
globus_l_seg_writev_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    int                                 i;
    globus_bool_t                       trigger_fault = GLOBUS_FALSE;
    globus_bool_t                       reregister_write = GLOBUS_FALSE;
    int                                 do_shutdown = 0;

    for (i = 0; i < count; i++)
    {
        globus_libc_free(iovec[i].iov_base);
    }
    globus_libc_free(iovec);

    globus_mutex_lock(&globus_l_seg_mutex);
    globus_l_seg_write_registered = GLOBUS_FALSE;

    if (result != GLOBUS_SUCCESS)
    {
        trigger_fault = GLOBUS_TRUE;
    }
    else if (!globus_fifo_empty(&globus_l_seg_buffers))
    {
        reregister_write = GLOBUS_TRUE;
    }
    else if (globus_l_seg_shutdown)
    {
        do_shutdown = 1;
    }
    if (trigger_fault)
    {
        globus_scheduler_event_generator_fault(result);
    }
    if (reregister_write)
    {
        globus_l_seg_register_write(NULL);
    }

    if (do_shutdown)
    {
        globus_l_seg_shutdown = 2;
        globus_cond_signal(&globus_l_seg_cond);
    }
    globus_mutex_unlock(&globus_l_seg_mutex);
}
/* globus_l_seg_writev_callback() */
