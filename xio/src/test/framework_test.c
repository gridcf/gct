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

#include "globus_xio.h"
#include "globus_common.h"
#include "test_common.h"
#include "globus_xio_test_transport.h"

static globus_mutex_t                       globus_l_mutex;
static globus_cond_t                        globus_l_cond;
static globus_bool_t                        globus_l_closed = GLOBUS_FALSE;
static globus_bool_t                        globus_l_accepted = GLOBUS_FALSE;

static void
accept_cb(
    globus_xio_server_t                         server,
    globus_xio_handle_t                         handle,
    globus_result_t                             result,
    void *                                      user_arg)
{
    globus_xio_handle_t *                       h;

    test_res(GLOBUS_XIO_TEST_FAIL_FINISH_ACCEPT, result, __LINE__, __FILE__);
    h = (globus_xio_handle_t *) user_arg;

    globus_mutex_lock(&globus_l_mutex);
    {
        *h = handle;
        globus_l_accepted = GLOBUS_TRUE;
        globus_cond_signal(&globus_l_cond);
    }
    globus_mutex_unlock(&globus_l_mutex);
}


static void
close_cb(
    globus_xio_handle_t                         handle,
    globus_result_t                             result,
    void *                                      user_arg)
{
    globus_mutex_lock(&globus_l_mutex);
    {
        globus_l_closed = GLOBUS_TRUE;
        globus_cond_signal(&globus_l_cond);
    }
    globus_mutex_unlock(&globus_l_mutex);
}

static void
read_cb(
    globus_xio_handle_t                         handle,
    globus_result_t                             result,
    globus_byte_t *                             buffer,
    globus_size_t                               len,
    globus_size_t                               nbytes,
    globus_xio_data_descriptor_t                data_desc,
    void *                                      user_arg)
{
    test_info_t *                               info;
    globus_result_t                             res;

    if(!globus_xio_error_is_eof(result) &&
       !globus_xio_error_is_canceled(result))
    {
        test_res(GLOBUS_XIO_TEST_FAIL_FINISH_READ, result, __LINE__, __FILE__);
    }

    info = (test_info_t *) user_arg;

    globus_mutex_lock(&info->mutex);
    {
        if(len < nbytes)
        {
            failed_exit("read wait for has failed");
        }
        else if(nbytes > len)
        {
            failed_exit("too many bytes were read.");
        }

        info->nread += nbytes;

        if(info->nread >= info->total_read_bytes && !info->read_done)
        {
            info->closed++;
            info->read_done = GLOBUS_TRUE;
            if(info->closed == 2 || info->write_count == 0)
            {
                res = globus_xio_register_close(
                        handle,
                        NULL,
                        close_cb,
                        user_arg);
                test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);
            }
        }
        else if(!info->read_done)
        {
            res = globus_xio_register_read(
                    handle,
                    info->buffer,
                    info->buffer_length,
                    info->buffer_length,
                    NULL,
                    read_cb,
                    user_arg);
            test_res(GLOBUS_XIO_TEST_FAIL_PASS_READ, res, __LINE__, __FILE__);
        }
    }
    globus_mutex_unlock(&info->mutex);
}

static void
write_cb(
    globus_xio_handle_t                         handle,
    globus_result_t                             result,
    globus_byte_t *                             buffer,
    globus_size_t                               len,
    globus_size_t                               nbytes,
    globus_xio_data_descriptor_t                data_desc,
    void *                                      user_arg)
{
    test_info_t *                               info;
    globus_result_t                             res;

    if(!globus_xio_error_is_canceled(result))
    {
        test_res(GLOBUS_XIO_TEST_FAIL_FINISH_WRITE, result, __LINE__, __FILE__);
    }

    info = (test_info_t *) user_arg;

    globus_mutex_lock(&info->mutex);
    {
        if(len < nbytes)
        {
            failed_exit("write wait for has failed");
        }
        else if(nbytes > len)
        {
            failed_exit("too many bytes were written.");
        }

        info->nwritten += nbytes;

        if(info->nwritten >= info->total_write_bytes && !info->write_done)
        {
            info->closed++;
            info->write_done = GLOBUS_TRUE;
            if(info->closed == 2 || info->read_count == 0)
            {
                res = globus_xio_register_close(
                        handle,
                        NULL,
                        close_cb,
                        user_arg);
                test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);
            }
        }
        else if(!info->write_done)
        {
            res = globus_xio_register_write(
                    handle,
                    info->buffer,
                    info->buffer_length,
                    info->buffer_length,
                    NULL,
                    write_cb,
                    user_arg);
            test_res(GLOBUS_XIO_TEST_FAIL_PASS_WRITE, res, __LINE__, __FILE__);
        }
    }
    globus_mutex_unlock(&info->mutex);
}

static void
open_cb(
    globus_xio_handle_t                         handle,
    globus_result_t                             result,
    void *                                      user_arg)
{
    globus_result_t                             res;
    int                                         ctr;
    test_info_t *                               info;
    globus_bool_t                               data = GLOBUS_FALSE;

    info = (test_info_t *) user_arg;

    test_res(GLOBUS_XIO_TEST_FAIL_FINISH_OPEN, result, __LINE__, __FILE__);

    globus_mutex_lock(&info->mutex);
    {
        for(ctr = 0; ctr < info->write_count; ctr++)
        {
            res = globus_xio_register_write(
                    handle,
                    info->buffer,
                    info->buffer_length,
                    info->buffer_length,
                    NULL,
                    write_cb,
                    user_arg);
            test_res(GLOBUS_XIO_TEST_FAIL_PASS_WRITE, res, __LINE__, __FILE__);
            data = GLOBUS_TRUE;
        }

        for(ctr = 0; ctr < info->read_count; ctr++)
        {
            res = globus_xio_register_read(
                    handle,
                    info->buffer,
                    info->buffer_length,
                    info->buffer_length,
                    NULL,
                    read_cb,
                    user_arg);
            test_res(GLOBUS_XIO_TEST_FAIL_PASS_READ, res, __LINE__, __FILE__);
            data = GLOBUS_TRUE;
        }

        if(!data)
        {
            res = globus_xio_register_close(
                    handle,
                    NULL,
                    close_cb,
                    user_arg);
                    test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);
        }
    }
    globus_mutex_unlock(&info->mutex);
}

int
framework_main(
    int                                     argc,
    char **                                 argv)
{
    int                                     rc;
    globus_xio_stack_t                      stack;
    globus_xio_handle_t                     handle;
    globus_xio_attr_t                       attr;
    globus_result_t                         res;
    globus_xio_server_t                     server;

    globus_l_closed = GLOBUS_FALSE;
    globus_l_accepted = GLOBUS_FALSE;

    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    globus_assert(rc == 0);

    globus_mutex_init(&globus_l_mutex, NULL);    
    globus_cond_init(&globus_l_cond, NULL);    

    res = globus_xio_attr_init(&attr);
    test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);

    res = globus_xio_stack_init(&stack, NULL);
    test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);

    parse_parameters(argc, argv, stack, attr);

    if(globus_l_test_info.server)
    {
        res = globus_xio_server_create(&server, attr, stack);
        test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);

        res = globus_xio_server_register_accept(
                server,
                accept_cb,
                &handle);
        test_res(GLOBUS_XIO_TEST_FAIL_PASS_ACCEPT, res, __LINE__, __FILE__);

        globus_mutex_lock(&globus_l_mutex);
        {
            while(!globus_l_accepted)
            {
                globus_cond_wait(&globus_l_cond, &globus_l_mutex);
            }
        }
        globus_mutex_unlock(&globus_l_mutex);
        
    }
    else
    {
        res = globus_xio_handle_create(&handle, stack);
        test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);
    }

    res = globus_xio_register_open(
            handle,
            "whatever",
            attr,
            open_cb,
            (void *)&globus_l_test_info);
    test_res(GLOBUS_XIO_TEST_FAIL_PASS_OPEN, res, __LINE__, __FILE__);

    globus_mutex_lock(&globus_l_mutex);
    {
        while(!globus_l_closed)
        {
            globus_cond_wait(&globus_l_cond, &globus_l_mutex);
        }
    }
    globus_mutex_unlock(&globus_l_mutex);

    globus_xio_attr_destroy(attr);
    globus_xio_stack_destroy(stack);

    if(globus_l_test_info.server)
    {
        res = globus_xio_server_close(server);
        test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);
    }

    test_common_end();

    rc = globus_module_deactivate(GLOBUS_XIO_MODULE);
    globus_assert(rc == 0);

    fprintf(stdout, "Success.\n");

    return 0;
}
