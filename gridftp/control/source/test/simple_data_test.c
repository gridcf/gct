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

#define TEST_ITERATIONS                                     8
#define NAME              "globus_ftp_control_test_file_name"
#define DIRECTION_GET                                       0
#define DIRECTION_PUT                                       1
#define CHUNK_SIZE                                          256
#define CHUNK_COUNT                                         10

typedef struct get_put_info_s
{
    int                                         direction;
    int                                         buffer_size;
    globus_byte_t *                             buffer;
    globus_bool_t                               result;
    globus_ftp_control_fake_file_t *            fake_file;
} get_put_info_t;

static void
simple_data_response_callback(
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

static void
signal_exit();

static globus_ftp_control_test_monitor_t        monitor;

/*
 *  this is called with a connected handle
 */
globus_bool_t
simple_data_test(
    globus_ftp_control_handle_t *               handle)
{
    globus_result_t                             result;
    get_put_info_t                              get_put_info;
    globus_ftp_control_fake_file_t              fake_file;
    int                                         ctr;

    globus_mutex_init(&monitor.mutex, GLOBUS_NULL);
    globus_cond_init(&monitor.cond, GLOBUS_NULL);

    /* once in default, once in image */
    for(ctr = 0; ctr < 2; ctr++)
    {
        /* send pasv */
        verbose_printf(2, "sendind pasv command\n");
        monitor.done = GLOBUS_FALSE;
        result = globus_ftp_control_send_command(
                     handle,
                     "PASV\r\n",
                     simple_data_response_callback,
                     GLOBUS_NULL);
        if(result != GLOBUS_SUCCESS)
        {
            verbose_printf(1, "send_command PASV failed\n");
            return GLOBUS_FALSE;
        }
        /*  wait to get pasv back */
        globus_mutex_lock(&monitor.mutex);
        {
            while(!monitor.done)
            {
               globus_cond_wait(&monitor.cond, &monitor.mutex);
            } 
        }
        globus_mutex_unlock(&monitor.mutex);

        if(!monitor.rc)
        {
            verbose_printf(1, "pasv command failed\n");
            return GLOBUS_FALSE;
        }

        /* start writting */
        verbose_printf(2, "sendind stor command\n");
        get_put_info.direction = DIRECTION_PUT;
        monitor.rc = GLOBUS_TRUE;
        fake_file_init(&fake_file, CHUNK_SIZE * CHUNK_COUNT + 12, CHUNK_SIZE);
        get_put_info.fake_file = &fake_file;
        monitor.done = GLOBUS_FALSE;
        monitor.count = 0;
        result = globus_ftp_control_send_command(
                     handle,
                     "STOR %s\r\n",
                     simple_data_response_callback,
                     &get_put_info,
                     NAME);
        if(result != GLOBUS_SUCCESS)
        {
            verbose_printf(1, "send_command PASV failed\n");
            return GLOBUS_FALSE;
        }
        result = globus_ftp_control_data_connect_write(
                     handle, GLOBUS_NULL, GLOBUS_NULL);
        if(result != GLOBUS_SUCCESS)
        {
            verbose_printf(1, "connect_write failed\n");
            return GLOBUS_FALSE;
        }
        /*  wait to get put back */
        globus_mutex_lock(&monitor.mutex);
        {
            while(monitor.count < 2)
            {
                globus_cond_wait(&monitor.cond, &monitor.mutex);
            }
        }
        globus_mutex_unlock(&monitor.mutex);
        if(!monitor.rc)
        {
            verbose_printf(1, "STOR failed\n");
            return GLOBUS_FALSE;
        }

        verbose_printf(2, "data has been sent\n");

        /* new pasv */
        verbose_printf(2, "sendind new pasv command\n");
        monitor.done = GLOBUS_FALSE;
        monitor.rc = GLOBUS_TRUE;
        result = globus_ftp_control_send_command(
                     handle,
                     "PASV\r\n",
                     simple_data_response_callback,
                     GLOBUS_NULL);
        if(result != GLOBUS_SUCCESS)
        {
            verbose_printf(1, "send_command PASV failed\n");
            return GLOBUS_FALSE;
        }
        /*  wait to get pasv back */
        globus_mutex_lock(&monitor.mutex);
        {
            while(!monitor.done)
            {
                globus_cond_wait(&monitor.cond, &monitor.mutex);
            }
        }
        globus_mutex_unlock(&monitor.mutex);
        if(!monitor.rc)
        {
            verbose_printf(1, "PASV failed\n");
            return GLOBUS_FALSE;
        }

        /* start reading */
        fake_file_seek(get_put_info.fake_file, 0);
        verbose_printf(2, "sending retr command\n");
        get_put_info.direction = DIRECTION_GET;
        monitor.rc = GLOBUS_TRUE;
        get_put_info.fake_file = &fake_file;
        monitor.done = GLOBUS_FALSE;
        monitor.count = 0;
        result = globus_ftp_control_send_command(
                     handle,
                     "RETR %s\r\n",
                     simple_data_response_callback,
                     &get_put_info,
                     NAME);
        if(result != GLOBUS_SUCCESS)
        {
            verbose_printf(1, "send_command PASV failed\n");
            return GLOBUS_FALSE;
        }
        result = globus_ftp_control_data_connect_read(
                     handle, GLOBUS_NULL, GLOBUS_NULL);
        if(result != GLOBUS_SUCCESS)
        {
            verbose_printf(1, "connect_read failed\n");
            return GLOBUS_FALSE;
        }
        /*  wait to get put back */
        globus_mutex_lock(&monitor.mutex);
        {
            while(monitor.count < 2)
            {
                globus_cond_wait(&monitor.cond, &monitor.mutex);
            }
        }
        globus_mutex_unlock(&monitor.mutex);
        if(!monitor.rc)
        {
            verbose_printf(1, "RETR failed\n");
            return GLOBUS_FALSE;
        }

        verbose_printf(2, "data has been received\n");

        /* delete existing file */
        verbose_printf(2, "sending dele command\n");
        monitor.done = GLOBUS_FALSE;
        monitor.rc = GLOBUS_TRUE;
        result = globus_ftp_control_send_command(
                     handle,
                     "DELE %s\r\n",
                     simple_data_response_callback,
                     GLOBUS_NULL,
                     NAME);
        if(result != GLOBUS_SUCCESS)
        {
            verbose_printf(1, "send_command DELE failed\n");
            return GLOBUS_FALSE;
        }
        /*  wait to get pasv back */
        globus_mutex_lock(&monitor.mutex);
        {
            while(!monitor.done)
            {
                globus_cond_wait(&monitor.cond, &monitor.mutex);
            }
        }
        globus_mutex_unlock(&monitor.mutex);
        if(!monitor.rc)
        {
            verbose_printf(1, "DELE failed\n");
            return GLOBUS_FALSE;
        }
        fake_file_destroy(&fake_file);

        /* send type */
        if(ctr == 0)
        { 
            verbose_printf(2, "sending type command\n");
            monitor.done = GLOBUS_FALSE;
            monitor.count = 0;
            monitor.rc = GLOBUS_TRUE;
            result = globus_ftp_control_send_command(
                         handle,
                         "TYPE I\r\n",
                         simple_data_response_callback,
                         GLOBUS_NULL);
            if(result != GLOBUS_SUCCESS)
            {
                verbose_printf(1, "send_command TYPE failed\n");
                return GLOBUS_FALSE;
            }
            /*  wait to get pasv back */
            globus_mutex_lock(&monitor.mutex);
            {
                while(!monitor.done)
                {
                    globus_cond_wait(&monitor.cond, &monitor.mutex);
                }
            }
            globus_mutex_unlock(&monitor.mutex);
            if(!monitor.rc)
            {
                verbose_printf(1, "TYPE failed\n");
                return GLOBUS_FALSE;
            }
        }
    }

    return GLOBUS_TRUE;
}

void
simple_data_response_callback(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error,
    globus_ftp_control_response_t *             ftp_response)
{
    get_put_info_t *                            get_put_info;
    globus_result_t                             result;
    globus_ftp_control_host_port_t              addr;

    if(error != GLOBUS_NULL)
    {
        monitor.rc = GLOBUS_FALSE;
        signal_exit();
        return;        
    }

    if(ftp_response->code == 227)
    {
        pasv_to_host_port((char *)ftp_response->response_buffer, &addr);
        result = globus_ftp_control_local_port(handle, &addr);
        if(result != GLOBUS_SUCCESS)
        {
            monitor.rc = GLOBUS_FALSE;
        }
        else
        {
            monitor.rc = GLOBUS_TRUE;
        }
        signal_exit();
        return;
    }
    else if(ftp_response->code == 226)
    {
        get_put_info = (get_put_info_t *)callback_arg;

        if(get_put_info->direction == DIRECTION_PUT)
        {
            monitor.rc = GLOBUS_TRUE;
        }
        signal_exit();
    }
    else if(ftp_response->code == 150)
    {
        globus_byte_t *                         buf;
        int                                     buf_size;

        get_put_info = (get_put_info_t *)callback_arg;
 
        if(get_put_info->direction == DIRECTION_PUT)
        {
            buf = fake_file_get_chunk(get_put_info->fake_file, &buf_size);

            result = globus_ftp_control_data_write(
                 handle,
                 buf,
                 buf_size,
                 0,
                 fake_file_is_eof(get_put_info->fake_file),
                 write_data_callback,
                 (void *) get_put_info);
            if(result != GLOBUS_SUCCESS)
            {
                verbose_printf(1, "intial write register failed\n");
                monitor.rc = GLOBUS_FALSE;
                signal_exit();
                return;
            }
        }
        else if(get_put_info->direction == DIRECTION_GET)
        {
            buf = globus_malloc(CHUNK_SIZE);

            result = globus_ftp_control_data_read(
                 handle,
                 buf,
                 CHUNK_SIZE,
                 read_data_callback,
                 (void *) get_put_info);
            if(result != GLOBUS_SUCCESS)
            {
                verbose_printf(1, "intial read register failed\n");
                monitor.rc = GLOBUS_FALSE;
                signal_exit();
                return;
            }
        }
    }
    else if(ftp_response->code == 200)
    {
        result = globus_ftp_control_local_type(
                     handle, 
                     GLOBUS_FTP_CONTROL_TYPE_IMAGE, 
                     0);
        if(result == GLOBUS_SUCCESS)
        {
            monitor.rc = GLOBUS_TRUE;
        }
        else
        {
            verbose_printf(1, "local_type failed\n");
            monitor.rc = GLOBUS_FALSE;
        }
        signal_exit();
    }
    else if(ftp_response->code == 250)
    {
        verbose_printf(2, "delete succeded\n");
        signal_exit();
    }
    else if(ftp_response->code >= 500)
    {
        verbose_printf(1, "error %s\n", ftp_response->response_buffer);
        signal_exit();
    }
    else
    {
        verbose_printf(1, "error %d %s\n", ftp_response->code, ftp_response->response_buffer);
        signal_exit();
    }
}

void
signal_exit()
{
    verbose_printf(2, "signal_exit() : start\n");
    globus_mutex_lock(&monitor.mutex);
    {
        monitor.done = GLOBUS_TRUE;
        monitor.count++;
        globus_cond_signal(&monitor.cond);
    }
    globus_mutex_unlock(&monitor.mutex);
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
    get_put_info_t *                            get_put_info;
    globus_byte_t *                             buf;
    int                                         buf_size;
    globus_result_t                             result;

    get_put_info = (get_put_info_t *)callback_arg;
    if(error != GLOBUS_NULL)
    {
        monitor.rc = GLOBUS_FALSE;
        signal_exit();
        signal_exit();
        return;        
    }

    if(!eof)
    {
        buf = fake_file_get_chunk(get_put_info->fake_file, &buf_size);
        result = globus_ftp_control_data_write(
                     handle,
                     buf,
                     buf_size,
                     offset + length,
                     fake_file_is_eof(get_put_info->fake_file),
                     write_data_callback,
                     (void *) get_put_info);
        if(result != GLOBUS_SUCCESS)
        {
            monitor.rc = GLOBUS_FALSE;
            signal_exit();
            signal_exit();
            return;
        }
    }
    else
    {
        signal_exit();
    }
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
    get_put_info_t *                            get_put_info;
    globus_result_t                             result;

    get_put_info = (get_put_info_t *)callback_arg;

    if(error != GLOBUS_NULL)
    {
        monitor.rc = GLOBUS_FALSE;
        signal_exit();
        signal_exit();
        return;        
    }

    if(!fake_file_cmp(get_put_info->fake_file, buffer, offset, length))
    {
        monitor.rc = GLOBUS_FALSE;
        verbose_printf(1, "file compare failed\n");
        signal_exit();
        signal_exit();
        return;        
    }

    if(eof)
    {
        verbose_printf(2, "read eof hit\n");
        signal_exit();
        globus_free(buffer);
    }
    else
    {
        verbose_printf(2, "register next read\n");
        get_put_info = (get_put_info_t *)callback_arg;
        result = globus_ftp_control_data_read(
             handle,
             buffer,
             CHUNK_SIZE,
             read_data_callback,
             (void *) get_put_info);
        if(result != GLOBUS_SUCCESS)
        {
            verbose_printf(1, "intial read register failed\n");
            monitor.rc = GLOBUS_FALSE;
            signal_exit();
            return;
        }
    }
}
