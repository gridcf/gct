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

#include "globus_ftp_control_test.h"
#include "test_common.h"

#define TEST_ITERATIONS                                     8
#define NAME1              "globus_ftp_control_test_file_name1"
#define NAME2              "globus_ftp_control_test_file_name2"
#define CHUNK_SIZE                                          2048 
#define CHUNK_COUNT                                         1000
#define ABORT_COUNT                                         5

void
test_result(
    globus_result_t                             res,
    char *                                      msg);

static void
abort_response_callback(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error,
    globus_ftp_control_response_t *             ftp_response);

static void
write_data_callback(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof);

static void
read_data_callback(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof);

static globus_byte_t *                          g_buffer = GLOBUS_NULL;
static ftp_test_monitor_t                       g_monitor;
static globus_bool_t                            g_abort_size;


/*
 *  make pasv blocking
 */
void
pasv_response_callback(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error,
    globus_ftp_control_response_t *             ftp_response)
{
    globus_result_t                             res;
    ftp_test_monitor_t *                        l_mon;
    globus_ftp_control_host_port_t              addr;

    l_mon = (ftp_test_monitor_t *)callback_arg;

    if(error != GLOBUS_NULL)
    {
        test_result(globus_error_put(error), 
                    "response callback()");
    }

    if(ftp_response->code == 227)
    {
        verbose_printf(2, "pasv buffer :%s:\n", ftp_response->response_buffer);
        pasv_to_host_port((char *)ftp_response->response_buffer, &addr);
        verbose_printf(2, "pasv port %d.%d.%d.%d:%d\n",
            addr.host[0],
            addr.host[1],
            addr.host[2],
            addr.host[3],
            addr.port);

        res = globus_ftp_control_local_port(handle, &addr);
        test_result(res, "globus_ftp_control_local_port()");
    }
    else
    {
        verbose_printf(1, "error, pasv reply was :%s:\n", ftp_response->response_buffer);
    }
    globus_mutex_lock(&l_mon->mutex);
    {
        l_mon->done = GLOBUS_TRUE;
        globus_cond_signal(&l_mon->cond);
    }
    globus_mutex_unlock(&l_mon->mutex);

    return;
}

static void
send_pasv_blocking(
    globus_ftp_control_handle_t *               handle)
{
    globus_result_t                             res;
    ftp_test_monitor_t                          l_mon;

    ftp_test_monitor_init(&l_mon);

    res = globus_ftp_control_send_command(
             handle,
             "PASV\r\n",
             pasv_response_callback,
             (void *)&l_mon);
    test_result(res, "globus_ftp_control_send_command()");

    globus_mutex_lock(&l_mon.mutex);
    {
        while(!l_mon.done)
        {
            globus_cond_wait(&l_mon.cond, &l_mon.mutex);
        } 
    }
    globus_mutex_unlock(&l_mon.mutex);
}


/*
 *  make type blocking
 */
void
type_response_callback(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error,
    globus_ftp_control_response_t *             ftp_response)
{
    ftp_test_monitor_t *                        l_mon;

    l_mon = (ftp_test_monitor_t *)callback_arg;

    if(error != GLOBUS_NULL)
    {
        test_result(globus_error_put(error), 
                    "response callback()");
    }
 
    globus_mutex_lock(&l_mon->mutex);
    {
        l_mon->done = GLOBUS_TRUE;
        globus_cond_signal(&l_mon->cond);
    }
    globus_mutex_unlock(&l_mon->mutex);

    return;
}

static void
send_type_blocking(
    globus_ftp_control_handle_t *               handle)
{
    globus_result_t                             res;
    ftp_test_monitor_t                          l_mon;

    ftp_test_monitor_init(&l_mon);

    res = globus_ftp_control_send_command(
             handle,
             "TYPE I\r\n",
             type_response_callback,
             (void *)&l_mon);
    test_result(res, "globus_ftp_control_send_command()");

    globus_mutex_lock(&l_mon.mutex);
    {
        while(!l_mon.done)
        {
            globus_cond_wait(&l_mon.cond, &l_mon.mutex);
        } 
    }
    globus_mutex_unlock(&l_mon.mutex);
    res = globus_ftp_control_local_type(
              handle, 
              GLOBUS_FTP_CONTROL_TYPE_IMAGE, 
              0);
    test_result(res, "local_type");
}

static void
send_dele_blocking(
    globus_ftp_control_handle_t *               handle,
    char *                                      fname)
{
    globus_result_t                             res;
    ftp_test_monitor_t                          l_mon;

    ftp_test_monitor_init(&l_mon);

    res = globus_ftp_control_send_command(
             handle,
             "DELE %s\r\n",
             type_response_callback,
             (void *)&l_mon,
             fname);
    test_result(res, "globus_ftp_control_send_command()");

    globus_mutex_lock(&l_mon.mutex);
    {
        while(!l_mon.done)
        {
            globus_cond_wait(&l_mon.cond, &l_mon.mutex);
        } 
    }
    globus_mutex_unlock(&l_mon.mutex);
}

/*
 *  this is called with a connected handle
 */
globus_bool_t
abort_test(
    globus_ftp_control_handle_t *               handle)
{
    globus_result_t                             res;
    ftp_test_monitor_t                          l_mon;
    int                                         ctr;


    ftp_test_monitor_init(&l_mon);
    ftp_test_monitor_init(&g_monitor);

    g_buffer = globus_malloc(CHUNK_SIZE);
    memset(g_buffer, 0xff, CHUNK_SIZE);

    verbose_printf(2, "set to type i\n");
    send_type_blocking(handle);

 for(ctr = 0; ctr < TEST_ITERATIONS; ctr++)
 {
    /*
     *  put a file on the server
     */
    verbose_printf(2, "sending\n");
    g_abort_size = -1;
    verbose_printf(2, "put in pasv mode\n");
    send_pasv_blocking(handle);

    ftp_test_monitor_reset(&l_mon);
    ftp_test_monitor_reset(&g_monitor);
    res = globus_ftp_control_send_command(
             handle,
             "STOR %s\r\n",
             abort_response_callback,
             (void *)&l_mon,
             NAME1);
    test_result(res, "send command");
   
    res = globus_ftp_control_data_connect_write(
              handle,
              GLOBUS_NULL,
              GLOBUS_NULL);
    test_result(res, "connect_write()");
    
    ftp_test_monitor_done_wait(&l_mon);

    ftp_test_monitor_reset(&l_mon);
    res = globus_ftp_control_data_write(
              handle,
              g_buffer,
              CHUNK_SIZE,
              0,
              GLOBUS_FALSE,
              write_data_callback,
              (void *) &l_mon);
    test_result(res, "globus_ftp_control_data_write()");

    ftp_test_monitor_count_wait(&l_mon, 2);

    /*
     *  send and abort
     */
    verbose_printf(2, "sending and aborting\n");
    ftp_test_monitor_reset(&g_monitor);
    ftp_test_monitor_reset(&l_mon);
    g_abort_size = ABORT_COUNT;
    verbose_printf(2, "setting abort size to %d\n", g_abort_size);
    verbose_printf(2, "put in pasv mode\n");
    send_pasv_blocking(handle);

    res = globus_ftp_control_send_command(
             handle,
             "STOR %s\r\n",
             abort_response_callback,
             (void *)&l_mon,
             NAME2);
    test_result(res, "send command");
    res = globus_ftp_control_data_connect_write(
              handle,
              GLOBUS_NULL,
              GLOBUS_NULL);
    test_result(res, "connect_write()");

    ftp_test_monitor_done_wait(&l_mon);

    ftp_test_monitor_reset(&l_mon);
    res = globus_ftp_control_data_write(
              handle,
              g_buffer,
              CHUNK_SIZE,
              0,
              GLOBUS_FALSE,
              write_data_callback,
              (void *) &l_mon);
    test_result(res, "globus_ftp_control_data_write()");

    ftp_test_monitor_count_wait(&l_mon, 3);

    /*
     *  receive and abort
     */
    verbose_printf(2, "recieving and aborting\n");
    ftp_test_monitor_reset(&g_monitor);
    ftp_test_monitor_reset(&l_mon);
    g_abort_size = ABORT_COUNT;
    verbose_printf(2, "put in pasv mode\n");
    send_pasv_blocking(handle);

    verbose_printf(2, "sending RETR\n");
    res = globus_ftp_control_send_command(
             handle,
             "RETR %s\r\n",
             abort_response_callback,
             (void *)&l_mon,
             NAME1);
    test_result(res, "send command");

    verbose_printf(2, "calling connect read\n");
    res = globus_ftp_control_data_connect_read(
              handle,
              GLOBUS_NULL,
              GLOBUS_NULL);
    test_result(res, "connect_read()");

    ftp_test_monitor_done_wait(&l_mon);

    ftp_test_monitor_reset(&l_mon);
    verbose_printf(2, "calling data_read\n");
    res = globus_ftp_control_data_read(
              handle,
              g_buffer,
              CHUNK_SIZE,
              read_data_callback,
              (void *)&l_mon);
    test_result(res, "globus_ftp_control_data_write()");

    ftp_test_monitor_count_wait(&l_mon, 3);

    /*
     *  receive the file
     */
    verbose_printf(2, "recieving\n");
    ftp_test_monitor_reset(&g_monitor);
    ftp_test_monitor_reset(&l_mon);
    g_abort_size = -1;
    verbose_printf(2, "put in pasv mode\n");
    send_pasv_blocking(handle);

    res = globus_ftp_control_send_command(
             handle,
             "RETR %s\r\n",
             abort_response_callback,
             (void *)&l_mon,
             NAME1);
    test_result(res, "send command");

    res = globus_ftp_control_data_connect_read(
              handle,
              GLOBUS_NULL,
              GLOBUS_NULL);
    test_result(res, "connect_read()");

    ftp_test_monitor_done_wait(&l_mon);

    ftp_test_monitor_reset(&l_mon);
    res = globus_ftp_control_data_read(
              handle,
              g_buffer,
              CHUNK_SIZE,
              read_data_callback,
              (void *)&l_mon);
    test_result(res, "globus_ftp_control_data_write()");

    ftp_test_monitor_count_wait(&l_mon, 2);
    /*
     *  remove the file
     */
    send_dele_blocking(handle, NAME1);
  }

    return GLOBUS_TRUE;
}

void
abort_response_callback(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error,
    globus_ftp_control_response_t *             ftp_response)
{
    ftp_test_monitor_t *                        l_mon;

    l_mon = (ftp_test_monitor_t *)callback_arg;

    verbose_printf(2, "abort_response_callback() : %d %s\n", 
                   ftp_response->code,
                   ftp_response->response_buffer);
    if(error != GLOBUS_NULL)
    {
        test_result(globus_error_put(error), "abort_response_callback error");
    }

    if(ftp_response->code == 226)
    {
        ftp_test_monitor_signal(l_mon);
    }
    if(ftp_response->code == 225)
    {
        ftp_test_monitor_signal(l_mon);
    }
    else if(ftp_response->code == 426)
    {
        ftp_test_monitor_signal(l_mon);
    } 
    else if(ftp_response->code == 150)
    {
        ftp_test_monitor_signal(l_mon);
    }
}

void
write_data_callback(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof)
{
    globus_bool_t                               send_eof = GLOBUS_FALSE;
    ftp_test_monitor_t *                        l_mon;
    globus_result_t                             res;

    l_mon = (ftp_test_monitor_t *)callback_arg;
            verbose_printf(3, "write_data_callback():start\n");
    globus_mutex_lock(&g_monitor.mutex);
    {
        g_monitor.count++;

        if(g_monitor.count == CHUNK_COUNT)
        {
            verbose_printf(2, "sending eof\n");
            send_eof = GLOBUS_TRUE;
        }
        if(g_abort_size == g_monitor.count)
        {
            verbose_printf(2, "sending abort message");
            res = globus_ftp_control_abort(
                      handle,
                      abort_response_callback,
                      (void *)l_mon);
            test_result(res, "globus_ftp_control_abort()");
        }

        if(eof)
        {
            verbose_printf(2, "signaling eof\n");
            ftp_test_monitor_signal(l_mon);
        }
        else
        {
            res = globus_ftp_control_data_write(
                      handle,
                      g_buffer,
                      CHUNK_SIZE,
                      offset+length,
                      send_eof,
                      write_data_callback,
                      (void *) callback_arg);
            test_result(res, "globus_ftp_control_data_write()");
        }
    }
    globus_mutex_unlock(&g_monitor.mutex);
}

void
read_data_callback(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof)
{
    globus_result_t                             res;
    ftp_test_monitor_t *                        l_mon;

    verbose_printf(3, "read_data_callback() : start\n");

    l_mon = (ftp_test_monitor_t *)callback_arg;

    if(error != GLOBUS_NULL)
    {
        verbose_printf(1, "read_data_callback():error in callback\n");
    }
    globus_mutex_lock(&g_monitor.mutex);
    {
        g_monitor.count++;
        if(g_abort_size == g_monitor.count)
        {
            verbose_printf(2, "@@@@@@ sending read abort message");
            res = globus_ftp_control_abort(
                      handle,
                      abort_response_callback,
                      (void *)l_mon);
            test_result(res, "globus_ftp_control_abort()");
        }

        if(!eof)
        {
            verbose_printf(3, "registering next read\n");
            res = globus_ftp_control_data_read(
                      handle,
                      g_buffer,
                      CHUNK_SIZE,
                      read_data_callback,
                      callback_arg);
            test_result(res, "globus_ftp_control_data_write()");
        }
        else
        {
            verbose_printf(2, "read_data_callback() : eof received\n");
            l_mon = (ftp_test_monitor_t *)callback_arg;
            globus_mutex_lock(&l_mon->mutex);
            {
                l_mon->count++;
                globus_cond_signal(&l_mon->cond);
            }
            globus_mutex_unlock(&l_mon->mutex);
        }
    }
    globus_mutex_unlock(&g_monitor.mutex);
}


void
test_result(
    globus_result_t                             res,
    char *                                      msg)
{
    if(res != GLOBUS_SUCCESS)
    {
        verbose_printf(1, "error:%s\n",
            globus_object_printable_to_string(globus_error_get(res)));
        verbose_printf(1, "%s\n", msg);
        assert(GLOBUS_FALSE);
    }
}

