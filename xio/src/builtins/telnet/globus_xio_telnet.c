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

#include "globus_xio_driver.h"
#include "globus_xio_load.h"
#include "globus_common.h"
#include "globus_xio_telnet.h"
#include "globus_xio_util.h"

#define GLOBUS_L_XIO_TELNET_DEFAULT_BUFFER_SIZE    1024

enum
{
    GLOBUS_XIO_TELNET_NULL = 0,
    GLOBUS_XIO_TELNET_BELL = 7,
    GLOBUS_XIO_TELNET_BS = 8,
    GLOBUS_XIO_TELNET_HT = 9,
    GLOBUS_XIO_TELNET_LF = 10,
    GLOBUS_XIO_TELNET_VT = 11,
    GLOBUS_XIO_TELNET_FF = 12,
    GLOBUS_XIO_TELNET_CR = 13
};

enum
{
    GLOBUS_XIO_TELNET_SE = 240,
    GLOBUS_XIO_TELNET_NOP = 241,
    GLOBUS_XIO_TELNET_DM = 242,
    GLOBUS_XIO_TELNET_BRK = 243,
    GLOBUS_XIO_TELNET_IP = 244,
    GLOBUS_XIO_TELNET_AO = 245,
    GLOBUS_XIO_TELNET_AYT = 246,
    GLOBUS_XIO_TELNET_EC = 247,
    GLOBUS_XIO_TELNET_EL = 248,
    GLOBUS_XIO_TELNET_GA = 249,
    GLOBUS_XIO_TELNET_SB = 250,
    GLOBUS_XIO_TELNET_WILL = 251,
    GLOBUS_XIO_TELNET_WONT = 252,
    GLOBUS_XIO_TELNET_DO = 253,
    GLOBUS_XIO_TELNET_DONT = 254,
    GLOBUS_XIO_TELNET_IAC = 255
};

typedef struct globus_l_xio_telnet_handle_s
{
    globus_byte_t *                     write_buffer;
    globus_byte_t *                     read_buffer;
    globus_size_t                       read_buffer_length;
    globus_size_t                       read_buffer_ndx;
    globus_fifo_t                       write_q;
    globus_bool_t                       client;
    globus_size_t                       line_start_ndx;
    globus_bool_t                       create_buffer_mode;
    globus_mutex_t                      mutex;
    globus_xio_iovec_t *                user_read_iovec;
    int                                 user_read_iovec_count;
    globus_xio_iovec_t                  read_iovec;
    globus_xio_iovec_t                  write_iovec;
    unsigned char                       last_char;
    globus_bool_t                       finish;
    globus_result_t                     finish_res;
    globus_size_t                       finish_len;
} globus_l_xio_telnet_handle_t;

typedef struct
{
    globus_bool_t                       create_buffer_mode;
    globus_bool_t                       force_server;
} globus_l_xio_telnet_attr_t;

typedef struct globus_l_xio_telnet_q_ent_s
{
    globus_byte_t *                     start_buffer;
    globus_byte_t *                     buffer;
    globus_size_t                       length;
} globus_l_xio_telnet_q_ent_t;

/**************************************************************************
 *                    function prototypes
 *                    -------------------
 *************************************************************************/
static int
globus_l_xio_telnet_activate();

static int
globus_l_xio_telnet_deactivate();

static void
globus_l_xio_telnet_request_data(
    globus_l_xio_telnet_handle_t *      handle,
    globus_xio_operation_t              op);
/**************************************************************************
 *                    global data
 *                    -----------
 *************************************************************************/
#include "version.h"

GlobusXIODefineModule(telnet) =
{
    "globus_xio_telnet",
    globus_l_xio_telnet_activate,
    globus_l_xio_telnet_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

/************************************************************************
 *                  utility functions
 *                  ------------------
 *
 *  A variety of operations use these function.
 ***********************************************************************/

/*
 *  In coming buffer may shrink, if there are telnet commands in the
 *  buffer write commands are added to the write queue.
 */ 
static globus_bool_t
globus_l_xio_telnet_check_data(
    globus_l_xio_telnet_handle_t *      handle,
    globus_size_t *                     length)
{
    globus_bool_t                       done = GLOBUS_FALSE;
    globus_size_t                       len;
    int                                 ndx;
    globus_byte_t *                     buffer;

    len = handle->read_buffer_ndx;
    buffer = handle->read_buffer;

    ndx = 0;
    while(ndx < len && !done)
    {
        if(handle->last_char == GLOBUS_XIO_TELNET_IAC)
        {
            switch(buffer[ndx])
            {
                case GLOBUS_XIO_TELNET_WILL:
                    handle->write_buffer = globus_malloc(3);
                    handle->write_buffer[0] = GLOBUS_XIO_TELNET_IAC;
                    handle->write_buffer[1] = GLOBUS_XIO_TELNET_DONT;
                    break;

                case GLOBUS_XIO_TELNET_DO:
                    handle->write_buffer = globus_malloc(3);
                    handle->write_buffer[0] = GLOBUS_XIO_TELNET_IAC;
                    handle->write_buffer[1] = GLOBUS_XIO_TELNET_WONT;
                    break;

                /* 2 byte commands */
                default:
                    /* do not copy a byte */
                    break;
            }
            handle->last_char = buffer[ndx];
            len--;
            if(ndx < len)
            {
                memmove(&buffer[ndx], &buffer[ndx + 1], len - ndx);
            }
        }
        else if(handle->last_char == GLOBUS_XIO_TELNET_WILL ||
            handle->last_char == GLOBUS_XIO_TELNET_DO)
        {
            if(handle->write_buffer != NULL)
            {
                handle->write_buffer[2] = buffer[ndx];
                globus_fifo_enqueue(&handle->write_q, handle->write_buffer);
                handle->write_buffer = NULL;
            }
            handle->last_char = buffer[ndx];
            len--;
            if(ndx < len)
            {
                memmove(&buffer[ndx], &buffer[ndx + 1], len - ndx);
            }
        }
        else if(handle->last_char == GLOBUS_XIO_TELNET_WONT ||
            handle->last_char == GLOBUS_XIO_TELNET_DO)
        {
            handle->last_char = buffer[ndx];
            len--;
            if(ndx < len)
            {
                memmove(&buffer[ndx], &buffer[ndx + 1], len - ndx);
            }
        }
        else if((buffer[ndx] >= 32 && buffer[ndx] <= 126) || 
            buffer[ndx] == GLOBUS_XIO_TELNET_NULL ||
            buffer[ndx] == GLOBUS_XIO_TELNET_BELL ||
            buffer[ndx] == GLOBUS_XIO_TELNET_BS ||
            buffer[ndx] == GLOBUS_XIO_TELNET_HT ||
            buffer[ndx] == GLOBUS_XIO_TELNET_CR ||
            buffer[ndx] == GLOBUS_XIO_TELNET_VT ||
            buffer[ndx] == GLOBUS_XIO_TELNET_FF)
        {
            handle->last_char = buffer[ndx];
            ndx++;
        }
        else if(buffer[ndx] == GLOBUS_XIO_TELNET_LF &&
            handle->last_char == GLOBUS_XIO_TELNET_CR)
        {
            if(handle->client) /* do special ftp checking here */
            {
                if(ndx < handle->line_start_ndx + 5 || 
                    handle->read_buffer[handle->line_start_ndx+3] == ' ')
                {
                    handle->line_start_ndx = 0;
                    handle->last_char = '\0';
                    done = GLOBUS_TRUE;
                }
                else
                {
                    handle->line_start_ndx = ndx+1;
                    handle->last_char = buffer[ndx];
                }
            }
            else
            {
                handle->last_char = '\0';
                done = GLOBUS_TRUE;
            }
            ndx++;
        }
        else
        {
            handle->last_char = buffer[ndx];
            len--;
            if(ndx < len)
            {
                memmove(&buffer[ndx], &buffer[ndx + 1], len - ndx);
            }
        }
    }
    handle->read_buffer_ndx = len;
    *length = ndx;

    return done;
}


static void
globus_l_xio_telnet_cmd_write_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_xio_telnet_handle_t *      handle;

    handle = (globus_l_xio_telnet_handle_t *) user_arg;

    if(result != GLOBUS_SUCCESS)
    {
        globus_xio_driver_finished_read(op, result, 0);
        return;
    }
    globus_mutex_lock(&handle->mutex);
    {
        globus_free(handle->write_iovec.iov_base);
        globus_l_xio_telnet_request_data(handle, op);
    }
    globus_mutex_unlock(&handle->mutex);
    if(handle->finish)
    {
        handle->finish = GLOBUS_FALSE;
        globus_xio_driver_finished_read(
            op, handle->finish_res, handle->finish_len);       
    }
}

static void
globus_l_xio_telnet_read_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_xio_telnet_handle_t *      handle;

    handle = (globus_l_xio_telnet_handle_t *) user_arg;

    if(result != GLOBUS_SUCCESS)
    {
        globus_xio_driver_finished_read(op, result, nbytes);
        return;
    }
    globus_mutex_lock(&handle->mutex);
    {
        handle->read_buffer_ndx += nbytes;
        globus_l_xio_telnet_request_data(handle, op);
    }
    globus_mutex_unlock(&handle->mutex);
    if(handle->finish)
    {
        handle->finish = GLOBUS_FALSE;
        globus_xio_driver_finished_read(
            op, handle->finish_res, handle->finish_len);       
    }
}

static void
globus_l_xio_telnet_request_data(
    globus_l_xio_telnet_handle_t *      handle,
    globus_xio_operation_t              op)
{
    globus_bool_t                       complete;
    globus_size_t                       end;
    globus_size_t                       len;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_size_t                       remainder;
    globus_size_t                       diff;

    if(!globus_fifo_empty(&handle->write_q))
    {
        handle->write_iovec.iov_base = globus_fifo_dequeue(&handle->write_q);
        handle->write_iovec.iov_len = 3; 
        res = globus_xio_driver_pass_write(
            op,
            &handle->write_iovec,
            1,
            3,
            globus_l_xio_telnet_cmd_write_cb,
            handle);
        if(res != GLOBUS_SUCCESS)
        {
            goto err;
        }
        return;
    }

    /* is there a full command in there, updates read_buffer_ndx */
    complete = globus_l_xio_telnet_check_data(handle, &end);
    if(complete)
    {
        remainder = handle->read_buffer_ndx - end;
        if(handle->create_buffer_mode)
        {
            len = end;
            handle->user_read_iovec->iov_base = globus_malloc(len);
            memcpy(handle->user_read_iovec->iov_base, handle->read_buffer, len);
            handle->user_read_iovec->iov_len = len;
        }
        else
        {
            if(handle->user_read_iovec->iov_len >= end)
            {
                len = end;
            }
            else
            {
                diff = end - handle->user_read_iovec->iov_len;
                len = handle->user_read_iovec->iov_len;
                end -= diff;
                remainder += diff;
            }
            memcpy(handle->user_read_iovec->iov_base, handle->read_buffer, len);
        }

        /* move remainder to the begining of the buffer */
        if(remainder > 0)
        {
            memmove(handle->read_buffer, &handle->read_buffer[end], remainder);
        }
        handle->read_buffer_ndx = remainder;

        handle->finish = GLOBUS_TRUE;
        handle->finish_len = len;
        handle->finish_res = GLOBUS_SUCCESS;
    }
    else
    {
        if(handle->read_buffer_ndx + 1 >= handle->read_buffer_length)
        {
            handle->read_buffer_length *= 2;
            handle->read_buffer = globus_libc_realloc(
                handle->read_buffer, handle->read_buffer_length);
        }
        handle->read_iovec.iov_base = 
            &handle->read_buffer[handle->read_buffer_ndx];
        handle->read_iovec.iov_len = 
            handle->read_buffer_length - handle->read_buffer_ndx;
        res = globus_xio_driver_pass_read(
            op,
            &handle->read_iovec,
            1,
            1,
            globus_l_xio_telnet_read_cb,
            handle);
        if(res != GLOBUS_SUCCESS)
        {
            goto err;
        }
    }

    return;

  err:
    handle->finish = GLOBUS_TRUE;
    handle->finish_len = 0;
    handle->finish_res = res;
}

static globus_result_t
globus_l_xio_telnet_attr_init(
    void **                             out_driver_attr)
{
    globus_l_xio_telnet_attr_t *        attr;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_telnet_attr_init);
    
    attr = (globus_l_xio_telnet_attr_t *)
        globus_calloc(sizeof(globus_l_xio_telnet_attr_t), 1);
    if(!attr)
    {
        res = GlobusXIOErrorMemory("attr");
        goto error;
    }

    *out_driver_attr = attr;
    
    return GLOBUS_SUCCESS;

error:
    return res;
}

static globus_result_t
globus_l_xio_telnet_attr_copy(
    void **                             out_driver_attr,
    void *                              src_driver_attr)
{
    globus_l_xio_telnet_attr_t *        src_attr;
    globus_l_xio_telnet_attr_t *        dest_attr = NULL;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_telnet_attr_init);

    if(src_driver_attr == NULL)
    {
        *out_driver_attr = NULL;
        return GLOBUS_SUCCESS;
    }

    res = globus_l_xio_telnet_attr_init((void **) (void *) &dest_attr);
    if(res == GLOBUS_SUCCESS)
    {
        src_attr = (globus_l_xio_telnet_attr_t *) src_driver_attr;
        dest_attr->create_buffer_mode = src_attr->create_buffer_mode;
        dest_attr->force_server = src_attr->force_server;
        
        *out_driver_attr = dest_attr;
    }
    
    return res;
}

static globus_result_t
globus_l_xio_telnet_attr_destroy(
    void *                              driver_attr)
{
    globus_free(driver_attr);
    return GLOBUS_SUCCESS;
}

static globus_result_t
globus_l_xio_telnet_attr_cntl(
    void *                              driver_attr,
    int                                 cmd,
    va_list                             ap)
{
    globus_l_xio_telnet_attr_t *        attr;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_telnet_attr_cntl);

    attr = (globus_l_xio_telnet_attr_t *) driver_attr;

    switch(cmd)
    {
        case GLOBUS_XIO_TELNET_BUFFER:
            attr->create_buffer_mode = va_arg(ap, globus_bool_t);
            break;

        case GLOBUS_XIO_TELNET_FORCE_SERVER:
            attr->force_server = va_arg(ap, globus_bool_t);
            break;

        default:
            res = GlobusXIOErrorInvalidCommand(cmd);
            return res;
    }

    return GLOBUS_SUCCESS;
}

static
globus_result_t
globus_l_xio_telnet_server_destroy(
    void *                              driver_server)
{
    return globus_l_xio_telnet_attr_destroy(driver_server);
}

static
globus_result_t
globus_l_xio_telnet_server_init(
    void *                              driver_attr,
    const globus_xio_contact_t *        contact_info,
    globus_xio_operation_t              op)
{
    globus_result_t                     res;
    globus_l_xio_telnet_attr_t *        attr;
    GlobusXIOName(globus_l_xio_telnet_server_init);

    res = globus_l_xio_telnet_attr_copy((void **) (void *) &attr, driver_attr);
    if(res != GLOBUS_SUCCESS)
    {
        goto error;
    }
    res = globus_xio_driver_pass_server_init(op, contact_info, attr);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_pass;
    }
    return GLOBUS_SUCCESS;
error_pass:
    globus_l_xio_telnet_server_destroy(attr);
error:
    return res;
}

static void
globus_l_xio_telnet_accept_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_xio_driver_finished_accept(op, user_arg, result);
}

static globus_result_t
globus_l_xio_telnet_accept(
    void *                              driver_server,
    globus_xio_operation_t              accept_op)
{
    globus_result_t                     res;
    globus_l_xio_telnet_attr_t *        attr;
    GlobusXIOName(globus_l_xio_telnet_accept);
   
    res = globus_l_xio_telnet_attr_copy((void **) (void *) &attr, driver_server);
    if(res != GLOBUS_SUCCESS)
    {
        goto error;
    }
    res = globus_xio_driver_pass_accept(
        accept_op, globus_l_xio_telnet_accept_cb, attr);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_pass;
    }
    return GLOBUS_SUCCESS;
error_pass:
    globus_l_xio_telnet_server_destroy(attr);
error:
    return res;
}

static
globus_result_t
globus_l_xio_telnet_link_destroy(
    void *                              driver_link)
{
    return globus_l_xio_telnet_server_destroy(driver_link);
}

/************************************************************************
 *                  iface functions
 *                  ---------------
 *  
 ***********************************************************************/
static void
globus_l_xio_telnet_open_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_telnet_handle_t *      handle;

    handle = (globus_l_xio_telnet_handle_t *) user_arg;

    if(result != GLOBUS_SUCCESS)
    {
        globus_fifo_destroy(&handle->write_q);
        globus_free(handle->read_buffer);
        globus_mutex_destroy(&handle->mutex);
        globus_free(handle);
    }

    globus_xio_driver_finished_open(handle, op, result);
}

static globus_result_t
globus_l_xio_telnet_open(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_result_t                     res;
    globus_l_xio_telnet_attr_t *       attr;
    globus_l_xio_telnet_handle_t *     handle;
    GlobusXIOName(globus_l_xio_telnet_open);

    /* decide what attr to use */
    if(driver_attr != NULL)
    {
        attr = (globus_l_xio_telnet_attr_t *) driver_attr;
    }
    else if(driver_link != NULL)
    {
        attr = (globus_l_xio_telnet_attr_t *) driver_link;
    }
    else
    {
        /* default */
        attr = NULL;
    }

    handle = (globus_l_xio_telnet_handle_t *) globus_calloc(
        sizeof(globus_l_xio_telnet_handle_t), 1);
    if(handle == NULL)
    {
        res = GlobusXIOErrorMemory("handle");
        goto error_handle_alloc;
    }

    if(attr != NULL && attr->force_server)
    {
        handle->client = GLOBUS_FALSE;
    }
    else
    {       
        handle->client = driver_link ? GLOBUS_FALSE : GLOBUS_TRUE;
    }

    handle->read_buffer_length = GLOBUS_L_XIO_TELNET_DEFAULT_BUFFER_SIZE;
    handle->read_buffer = globus_malloc(handle->read_buffer_length);
    if(handle->read_buffer == NULL)
    {
        res = GlobusXIOErrorMemory("buffer");
        goto error_buffer_alloc;
    }
    globus_mutex_init(&handle->mutex, NULL);
    globus_fifo_init(&handle->write_q);

    handle->create_buffer_mode = attr 
        ? attr->create_buffer_mode : GLOBUS_FALSE;

    res = globus_xio_driver_pass_open(
        op,
        contact_info,
        globus_l_xio_telnet_open_cb,
        handle);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_pass;
    }

    return GLOBUS_SUCCESS;
error_pass:
    globus_free(handle->read_buffer);
    globus_mutex_destroy(&handle->mutex);
    globus_fifo_destroy(&handle->write_q);
error_buffer_alloc:
    globus_free(handle);
error_handle_alloc:
    return res;
}


static void
globus_l_xio_telnet_write_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_xio_driver_finished_write(op, result, nbytes);
}
                                                                                
static globus_result_t
globus_l_xio_telnet_write(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_result_t                     res;

    res = globus_xio_driver_pass_write(
        op,
        (globus_xio_iovec_t *)iovec,
        iovec_count,
        globus_xio_operation_get_wait_for(op),
        globus_l_xio_telnet_write_cb,
        NULL);

    return res;
}

static globus_result_t
globus_l_xio_telnet_read(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_xio_telnet_handle_t *     handle;

    handle = (globus_l_xio_telnet_handle_t *) driver_specific_handle;

    globus_mutex_lock(&handle->mutex);
    {
        handle->user_read_iovec = (globus_xio_iovec_t *) iovec;
        handle->user_read_iovec_count = iovec_count;
        globus_l_xio_telnet_request_data(handle, op);
    }
    globus_mutex_unlock(&handle->mutex);    
    if(handle->finish)
    {
        handle->finish = GLOBUS_FALSE;
        globus_xio_driver_finished_read(
            op, handle->finish_res, handle->finish_len);      
    }
    return GLOBUS_SUCCESS;
}

static void
globus_l_xio_telnet_close_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_telnet_handle_t *     handle;

    handle = (globus_l_xio_telnet_handle_t *) user_arg;

    globus_xio_driver_finished_close(op, result);

    globus_free(handle->read_buffer);
    globus_fifo_destroy(&handle->write_q);
    globus_mutex_destroy(&handle->mutex);
    globus_free(handle);
}

static globus_result_t
globus_l_xio_telnet_close(
    void *                              driver_specific_handle,
    void *                              attr,
    globus_xio_operation_t              op)
{
    globus_result_t                     res;
    globus_l_xio_telnet_handle_t *     handle;

    handle = (globus_l_xio_telnet_handle_t *) driver_specific_handle;

    res = globus_xio_driver_pass_close(
        op, globus_l_xio_telnet_close_cb, handle);

    return res;
}

static globus_result_t
globus_l_xio_telnet_init(
    globus_xio_driver_t *               out_driver)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_telnet_init);

    res = globus_xio_driver_init(&driver, "telnet", NULL);
    if(res != GLOBUS_SUCCESS)
    {
        return res;
    }

    globus_xio_driver_set_transform(
        driver,
        globus_l_xio_telnet_open,
        globus_l_xio_telnet_close,
        globus_l_xio_telnet_read,
        globus_l_xio_telnet_write,
        NULL,
        NULL);

    globus_xio_driver_set_server(
        driver,
        globus_l_xio_telnet_server_init,
        globus_l_xio_telnet_accept,
        globus_l_xio_telnet_server_destroy,
        NULL,
        NULL,
        globus_l_xio_telnet_link_destroy);
    
    globus_xio_driver_set_attr(
        driver,
        globus_l_xio_telnet_attr_init,
        globus_l_xio_telnet_attr_copy,
        globus_l_xio_telnet_attr_cntl,
        globus_l_xio_telnet_attr_destroy);
    
    *out_driver = driver;

    return GLOBUS_SUCCESS;
}

static void
globus_l_xio_telnet_destroy(
    globus_xio_driver_t                 driver)
{
    GlobusXIOName(globus_l_xio_telnet_destroy);
    globus_xio_driver_destroy(driver);
}

GlobusXIODefineDriver(
    telnet,
    globus_l_xio_telnet_init,
    globus_l_xio_telnet_destroy);

static int
globus_l_xio_telnet_activate(void)
{
    int                                 rc;
    GlobusXIOName(globus_l_xio_telnet_activate);

    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    if(rc == GLOBUS_SUCCESS)
    {
        GlobusXIORegisterDriver(telnet);
    }
    return rc;
}

static int
globus_l_xio_telnet_deactivate(void)
{
    GlobusXIOName(globus_l_xio_telnet_deactivate);
    GlobusXIOUnRegisterDriver(telnet);
    return globus_module_deactivate(GLOBUS_XIO_MODULE);
}
