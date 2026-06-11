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
#include "globus_common.h"
#include "gssapi.h"

static globus_ftp_control_test_monitor_t        connect_monitor;

static    globus_ftp_control_auth_info_t              auth;

static gss_cred_id_t                            g_cred;
static char *                                   g_user_name;
static char *                                   g_password;
static char *                                   g_base_dir;
static char *                                   g_host;
static gss_name_t                               g_name;
static char *                                   g_subject;
static unsigned short                           g_port;

void
connect_response_callback(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error,
    globus_ftp_control_response_t *             ftp_response)
{
    globus_result_t                             result;

    if(ftp_response == GLOBUS_NULL)
    {
        globus_mutex_lock(&connect_monitor.mutex);
        {
            connect_monitor.rc = GLOBUS_FALSE;
            connect_monitor.done = GLOBUS_TRUE;
            verbose_printf(2, "signal condition error:%s\n",
                 globus_object_printable_to_string(error));
            globus_cond_signal(&connect_monitor.cond);
        }
        globus_mutex_unlock(&connect_monitor.mutex);

        return;
    }

    verbose_printf(2, "connect_response_callback() : start: %s\n",
                   ftp_response->response_buffer);
    if(ftp_response->code == 530)
    {
        verbose_printf(1, "not logged in: %s\n", ftp_response->response_buffer);
        globus_mutex_lock(&connect_monitor.mutex);
        {
            connect_monitor.rc = GLOBUS_FALSE;
            connect_monitor.done = GLOBUS_TRUE;
            verbose_printf(2, "signal condition\n");
            globus_cond_signal(&connect_monitor.cond);
        }
        globus_mutex_unlock(&connect_monitor.mutex);
    }
    else if(ftp_response->code == 220)
    {
        memset(&auth, '\0', sizeof(auth));
        {
            OM_uint32 min;
            gss_buffer_desc buffer;

            gss_acquire_cred(
                    &min,
                    GSS_C_NO_NAME,
                    0,
                    GSS_C_NO_OID_SET,
                    GSS_C_BOTH,
                    &g_cred,
                    NULL,
                    NULL);

            gss_inquire_cred(&min,
                    g_cred,
                    &g_name,
                    NULL,
                    NULL,
                    NULL);
            gss_display_name(
                    &min,
                    g_name,
                    &buffer,
                    NULL);
            g_subject = buffer.value;
        }

        result = globus_ftp_control_auth_info_init(
                         &auth,
                         g_cred,
                         GLOBUS_FALSE,
                         g_user_name,
                         g_password,
                         NULL,
                         g_subject);
        assert(result==GLOBUS_SUCCESS);

        result = globus_ftp_control_authenticate(
                     handle,
                     &auth,
                     g_cred != GSS_C_NO_CREDENTIAL,
                     connect_response_callback,
                     NULL);
if(result != GLOBUS_SUCCESS)
{
printf("#####-> %s\n", globus_object_printable_to_string(globus_error_get(result)));
}
        assert(result==GLOBUS_SUCCESS);
    }
    else if(ftp_response->code == 230)
    {
        result = globus_ftp_control_send_command(
            handle,
            "CWD %s\r\n",
            connect_response_callback,
            NULL,
            g_base_dir);
        if(result != GLOBUS_SUCCESS)
        {
            verbose_printf(1, "failed to send cd\n");
            exit(1);
        }
    }
    else if(ftp_response->code == 250)
    {
        globus_mutex_lock(&connect_monitor.mutex);
        {
            connect_monitor.rc = GLOBUS_TRUE;
            connect_monitor.done = GLOBUS_TRUE;
            globus_cond_signal(&connect_monitor.cond);
        }
        globus_mutex_unlock(&connect_monitor.mutex);
    }
    else if(ftp_response->code == 221)
    {
        globus_mutex_lock(&connect_monitor.mutex);
        {
            connect_monitor.rc = GLOBUS_TRUE;
            connect_monitor.done = GLOBUS_TRUE;
            globus_cond_signal(&connect_monitor.cond);
        }
        globus_mutex_unlock(&connect_monitor.mutex);
    }
    else if(ftp_response->code >= 500)
    {
        globus_mutex_lock(&connect_monitor.mutex);
        {
            connect_monitor.rc = GLOBUS_FALSE;
            connect_monitor.done = GLOBUS_FALSE;
            globus_cond_signal(&connect_monitor.cond);
        }
        globus_mutex_unlock(&connect_monitor.mutex);
    }
}

globus_bool_t
connect_control_handle(
    globus_ftp_control_handle_t *               control_handle,
    char *                                      user_name,
    char *                                      password,
    char *                                      base_dir,
    char *                                      hostname,
    unsigned short                              port)
{
    globus_result_t                             result;

    if (user_name && user_name[0] != 0)
    {
        g_user_name = user_name;
    }
    if (password && password[0] != 0)
    {
        g_password = password;
    }
    if (base_dir && base_dir[0] != 0)
    {
        g_base_dir = base_dir;
    }
    g_host = hostname;
    g_port = port;

    globus_mutex_init(&connect_monitor.mutex, GLOBUS_NULL);
    globus_cond_init(&connect_monitor.cond, GLOBUS_NULL);
    connect_monitor.done = GLOBUS_FALSE;
    connect_monitor.rc = GLOBUS_TRUE;

    result = globus_ftp_control_connect(
                 control_handle,
                 g_host,
                 g_port,
                 connect_response_callback,
                 GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        verbose_printf(1, "error: could not connect\n");
        return GLOBUS_FALSE;
    }

    globus_mutex_lock(&connect_monitor.mutex);
    {
        verbose_printf(2, "waiting for signal\n");
        while(!connect_monitor.done)
        {
            globus_cond_wait(&connect_monitor.cond, &connect_monitor.mutex);
        }
    }
    globus_mutex_unlock(&connect_monitor.mutex);

    verbose_printf(2, "connect_control_handle() : test %d\n",
                   connect_monitor.rc);

    return connect_monitor.rc;
}


globus_bool_t
disconnect_control_handle(
    globus_ftp_control_handle_t *               control_handle)
{
    globus_result_t                             result;

    connect_monitor.done = GLOBUS_FALSE;
    connect_monitor.rc = GLOBUS_TRUE;

    result = globus_ftp_control_quit(
                 control_handle,
                 connect_response_callback,
                 GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        verbose_printf(1, "globus_ftp_control_quit() failed\n");
        return GLOBUS_FALSE;
    }

    globus_mutex_lock(&connect_monitor.mutex);
    {
        while(!connect_monitor.done)
        {
            globus_cond_wait(&connect_monitor.cond, &connect_monitor.mutex);
        }
    }
    globus_mutex_unlock(&connect_monitor.mutex);

    globus_mutex_destroy(&connect_monitor.mutex);
    globus_cond_destroy(&connect_monitor.cond);
    connect_monitor.done = GLOBUS_FALSE;

    return connect_monitor.rc;
}

