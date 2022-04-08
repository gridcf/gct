/*
 * Copyright 1999-2014 University of Chicago
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

#include "globus_i_xio.h"
#include "globus_xio_driver.h"
#include "globus_xio.h"
#include "globus_xio_load.h"
#include "globus_common.h"
#include "globus_xio_wrapblock.h"

typedef struct xio_l_wrapblock_handle_s
{
    void *                              driver_handle;
    struct xio_l_wrapblock_driver_s *   wrapblock_driver;
} xio_l_wrapblock_handle_t;

typedef struct xio_l_wrapblock_wrapper_s
{
    globus_xio_contact_t                ci;
    globus_i_xio_driver_t *             driver;
    struct xio_l_wrapblock_driver_s *   wrapblock_driver;
    xio_l_wrapblock_handle_t *          wrapblock_handle;
    void *                              attr;
    void *                              link;
    globus_xio_iovec_t *                iovec;
    int                                 iovec_count;
    globus_xio_operation_t              op;
} xio_l_wrapblock_wrapper_t;

typedef struct xio_l_wrapblock_driver_s
{
    globus_xio_wrapblock_open_func_t    open_func;
    globus_xio_wrapblock_close_func_t   close_func;
    globus_xio_wrapblock_read_func_t    read_func;
    globus_xio_wrapblock_write_func_t   write_func;
    globus_xio_wrapblock_accept_func_t  accept_func;
} xio_l_wrapblock_driver_t;

static
void
xio_l_wrapblock_wrapper_destroy(
    xio_l_wrapblock_wrapper_t *               wrapper)
{
    globus_xio_contact_destroy(&wrapper->ci);
    if(wrapper->iovec != NULL)
    {
        globus_free(wrapper->iovec);
    }
    if(wrapper->attr && wrapper->driver && wrapper->driver->attr_destroy_func)
    {
        wrapper->driver->attr_destroy_func(wrapper->attr);
    }
    globus_free(wrapper);
}
/************************************************************************
 *                  iface functions
 *                  ---------------
 *  
 ***********************************************************************/


static
void
globus_l_xio_wrapblock_accept_kickout(
    void *                              user_arg)
{
    void *                              link;
    globus_result_t                     res;
    xio_l_wrapblock_wrapper_t *         wrapper;

    globus_thread_blocking_will_block();

    wrapper = (xio_l_wrapblock_wrapper_t *) user_arg;

    res = wrapper->wrapblock_driver->accept_func(
        wrapper->attr,
        &link);

    globus_xio_driver_finished_accept(
        wrapper->op, link, res);

    xio_l_wrapblock_wrapper_destroy(wrapper);
}

static
globus_result_t
globus_l_xio_wrapblock_server_accept(
    void *                              driver_server,
    globus_xio_operation_t              op)
{
    xio_l_wrapblock_wrapper_t *         wrapper;
    globus_xio_driver_t                 driver;
    globus_i_xio_op_t *                 xio_op;

    xio_op = (globus_i_xio_op_t *) op;
    driver = xio_op->_op_server->entry[op->ndx - 1].driver;

    wrapper = (xio_l_wrapblock_wrapper_t *)
        globus_calloc(1, sizeof(xio_l_wrapblock_wrapper_t));
    wrapper->wrapblock_driver = driver->wrap_data;
    wrapper->attr = driver_server;
    wrapper->op = op;

    globus_callback_register_oneshot(
        NULL,
        NULL,
        globus_l_xio_wrapblock_accept_kickout,
        wrapper);

    return GLOBUS_SUCCESS;
}

static
void
globus_l_xio_wrapblock_open_kickout(
    void *                              user_arg)
{
    globus_result_t                     res;
    xio_l_wrapblock_wrapper_t *         wrapper;

    globus_thread_blocking_will_block();

    wrapper = (xio_l_wrapblock_wrapper_t *) user_arg;

    res = wrapper->wrapblock_handle->wrapblock_driver->open_func(
        &wrapper->ci,
        wrapper->link,
        wrapper->attr,
        &wrapper->wrapblock_handle->driver_handle);

    globus_xio_driver_finished_open(
        wrapper->wrapblock_handle, wrapper->op, res);

    xio_l_wrapblock_wrapper_destroy(wrapper);
}

static
globus_result_t
globus_l_xio_wrapblock_open(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_result_t                     result;
    xio_l_wrapblock_wrapper_t *         wrapper;
    xio_l_wrapblock_handle_t *          wrapblock_handle;
    globus_xio_driver_t                 driver;
    globus_i_xio_op_t *                 xio_op;

    xio_op = (globus_i_xio_op_t *) op;
    driver = xio_op->_op_context->entry[op->ndx - 1].driver;

    wrapblock_handle = globus_calloc(1, sizeof(xio_l_wrapblock_handle_t));
    wrapblock_handle->wrapblock_driver = driver->wrap_data;

    if(globus_xio_driver_operation_is_blocking(op))
    {
        result = wrapblock_handle->wrapblock_driver->open_func(
            contact_info,
            driver_link,
            driver_attr,
            &wrapblock_handle->driver_handle);

        globus_xio_driver_finished_open(wrapblock_handle, op, result);
    }
    else
    {
        wrapper = (xio_l_wrapblock_wrapper_t *)
            globus_calloc(1, sizeof(xio_l_wrapblock_wrapper_t));
        wrapper->wrapblock_handle = wrapblock_handle;
        wrapper->link = driver_link;
        wrapper->op = op;
        wrapper->driver = driver;

        if(driver_attr != NULL && driver->attr_copy_func != NULL)
        {
            driver->attr_copy_func(&wrapper->attr, driver_attr);
        }
        /* gotta copy contact info the hard way */
        globus_xio_contact_copy(&wrapper->ci, contact_info);

        globus_callback_register_oneshot(
            NULL,
            NULL,
            globus_l_xio_wrapblock_open_kickout,
            wrapper);
    }

    return GLOBUS_SUCCESS;
}

static
void
globus_l_xio_wrapblock_write_kickout(
    void *                              user_arg)
{
    globus_size_t                       nbytes;
    globus_result_t                     result;
    xio_l_wrapblock_wrapper_t *         wrapper;

    globus_thread_blocking_will_block();

    wrapper = (xio_l_wrapblock_wrapper_t *) user_arg;

    result = wrapper->wrapblock_handle->wrapblock_driver->write_func(
        wrapper->wrapblock_handle->driver_handle,
        wrapper->iovec,
        wrapper->iovec_count,
        &nbytes);
    globus_xio_driver_finished_write(wrapper->op, result, nbytes);

    xio_l_wrapblock_wrapper_destroy(wrapper);
}

static
globus_result_t
globus_l_xio_wrapblock_write(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_size_t                       nbytes;
    globus_result_t                     result;
    int                                 i;
    xio_l_wrapblock_wrapper_t *         wrapper;
    xio_l_wrapblock_handle_t *          wrapblock_handle;

    wrapblock_handle = (xio_l_wrapblock_handle_t *) driver_specific_handle;
    if(globus_xio_driver_operation_is_blocking(op))
    {
        result = wrapblock_handle->wrapblock_driver->write_func(
            wrapblock_handle->driver_handle,
            iovec,
            iovec_count,
            &nbytes);
        globus_xio_driver_finished_write(op, result, nbytes);
    }
    else
    {
        wrapper = (xio_l_wrapblock_wrapper_t *)
            globus_calloc(1, sizeof(xio_l_wrapblock_wrapper_t));
        wrapper->iovec = (globus_xio_iovec_t *)
            globus_calloc(iovec_count, sizeof(globus_xio_iovec_t));
        wrapper->iovec_count = iovec_count;
        wrapper->op = op;
        wrapper->wrapblock_handle = driver_specific_handle;

        for(i = 0; i < iovec_count; i++)
        {
            wrapper->iovec[i].iov_base = iovec[i].iov_base;
            wrapper->iovec[i].iov_len = iovec[i].iov_len;
        }

        globus_callback_register_oneshot(
            NULL,
            NULL,
            globus_l_xio_wrapblock_write_kickout,
            wrapper);
    }
    return GLOBUS_SUCCESS;
}

static
void
globus_l_xio_wrapblock_read_kickout(
    void *                              user_arg)
{
    globus_size_t                       nbytes;
    globus_result_t                     result;
    xio_l_wrapblock_wrapper_t *         wrapper;

    globus_thread_blocking_will_block();

    wrapper = (xio_l_wrapblock_wrapper_t *) user_arg;

    result = wrapper->wrapblock_handle->wrapblock_driver->read_func(
        wrapper->wrapblock_handle->driver_handle,
        wrapper->iovec,
        wrapper->iovec_count,
        &nbytes);
    globus_xio_driver_finished_read(wrapper->op, result, nbytes);

    xio_l_wrapblock_wrapper_destroy(wrapper);
}


static
globus_result_t
globus_l_xio_wrapblock_read(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_size_t                       nbytes;
    globus_result_t                     result;
    int                                 i;
    xio_l_wrapblock_wrapper_t *         wrapper;
    xio_l_wrapblock_handle_t *          wrapblock_handle;

    wrapblock_handle = (xio_l_wrapblock_handle_t *) driver_specific_handle;
    if(globus_xio_driver_operation_is_blocking(op))
    {
        result = wrapblock_handle->wrapblock_driver->read_func(
            wrapblock_handle->driver_handle,
            iovec,
            iovec_count,
            &nbytes);
        globus_xio_driver_finished_read(op, result, nbytes);
    }
    else
    {
        wrapper = (xio_l_wrapblock_wrapper_t *)
            globus_calloc(1, sizeof(xio_l_wrapblock_wrapper_t));
        wrapper->iovec = (globus_xio_iovec_t *)
            globus_calloc(iovec_count, sizeof(globus_xio_iovec_t));
        wrapper->iovec_count = iovec_count;
        wrapper->op = op;
        wrapper->wrapblock_handle = driver_specific_handle;

        for(i = 0; i < iovec_count; i++)
        {
            wrapper->iovec[i].iov_base = iovec[i].iov_base;
            wrapper->iovec[i].iov_len = iovec[i].iov_len;
        }

        globus_callback_register_oneshot(
            NULL,
            NULL,
            globus_l_xio_wrapblock_read_kickout,
            wrapper);
    }
    return GLOBUS_SUCCESS;
}

static
void
globus_l_xio_wrapblock_close_kickout(
    void *                              user_arg)
{
    xio_l_wrapblock_handle_t *          wrapblock_handle;
    globus_result_t                     result;
    xio_l_wrapblock_wrapper_t *         wrapper;

    globus_thread_blocking_will_block();

    wrapper = (xio_l_wrapblock_wrapper_t *) user_arg;
    wrapblock_handle = wrapper->wrapblock_handle;

    result = wrapper->wrapblock_handle->wrapblock_driver->close_func(
        wrapblock_handle->driver_handle,
        wrapper->attr);
    globus_xio_driver_finished_close(wrapper->op, result);

    xio_l_wrapblock_wrapper_destroy(wrapper);
    globus_free(wrapblock_handle);
}

static
globus_result_t
globus_l_xio_wrapblock_close(
    void *                              driver_specific_handle,
    void *                              attr,
    globus_xio_operation_t              op)
{
    globus_result_t                     result;
    xio_l_wrapblock_wrapper_t *         wrapper;
    xio_l_wrapblock_handle_t *          wrapblock_handle;

    wrapblock_handle = (xio_l_wrapblock_handle_t *) driver_specific_handle;
    if(globus_xio_driver_operation_is_blocking(op))
    {
        result = wrapblock_handle->wrapblock_driver->close_func(
            wrapblock_handle->driver_handle,
            attr);
        globus_xio_driver_finished_close(op, result);
        globus_free(wrapblock_handle);
    }
    else
    {
        wrapper = (xio_l_wrapblock_wrapper_t *)
            globus_calloc(1, sizeof(xio_l_wrapblock_wrapper_t));
        wrapper->attr = attr;
        wrapper->op = op;
        wrapper->wrapblock_handle = driver_specific_handle;

        globus_callback_register_oneshot(
            NULL,
            NULL,
            globus_l_xio_wrapblock_close_kickout,
            wrapper);
    }
    return GLOBUS_SUCCESS;
}


globus_result_t
globus_xio_wrapblock_init(
    globus_xio_driver_t                 driver,
    globus_xio_wrapblock_open_func_t    open_func,
    globus_xio_wrapblock_close_func_t   close_func,
    globus_xio_wrapblock_read_func_t    read_func,
    globus_xio_wrapblock_write_func_t   write_func,
    globus_xio_wrapblock_accept_func_t  accept_func)
{
    xio_l_wrapblock_driver_t *          wrapblock_driver;

    if(open_func != NULL)
    {
        driver->transport_open_func = globus_l_xio_wrapblock_open;
    }
    if(close_func != NULL)
    {
        driver->close_func = globus_l_xio_wrapblock_close;
    }
    if(read_func != NULL)
    {
        driver->read_func = globus_l_xio_wrapblock_read;
    }
    if(write_func != NULL)
    {
        driver->write_func = globus_l_xio_wrapblock_write;
    }
    if(accept_func != NULL)
    {
        driver->server_accept_func = globus_l_xio_wrapblock_server_accept;
    }

    wrapblock_driver = (xio_l_wrapblock_driver_t *)
        globus_calloc(1, sizeof(xio_l_wrapblock_driver_t));
    wrapblock_driver->open_func = open_func;
    wrapblock_driver->close_func = close_func;
    wrapblock_driver->read_func = read_func;
    wrapblock_driver->write_func = write_func;
    wrapblock_driver->accept_func = accept_func;
    
    driver->wrap_data = wrapblock_driver;

    return GLOBUS_SUCCESS;
}
