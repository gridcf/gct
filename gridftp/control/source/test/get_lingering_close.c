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

#include "globus_ftp_control.h"
#include "gssapi.h"
#include "globus_preload.h"

globus_mutex_t the_lock;
globus_cond_t the_cond;
globus_bool_t done = GLOBUS_FALSE;

void
response_cb(
    void *                                      closure,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           err,
    globus_ftp_control_response_t *             response)
{
    globus_ftp_control_auth_info_t      auth_info;

    if (err)
    {
        int i;
        char *msg = globus_error_print_friendly(err);

        putchar('#');
        for (i = 0; msg[i] != '\0'; i++)
        {
            putchar(msg[i]);
            if (msg[i] == '\n' && msg[i+1] != '\0')
            {
                putchar('#');
            }
        }
        if (msg[i-1] != '\n')
        {
            putchar('\n');
        }
        globus_mutex_lock(&the_lock);
        done = -1;
        globus_cond_signal(&the_cond);
        globus_mutex_unlock(&the_lock);
    }

    if (response)
    {
        if(response->code == 220)
        {
            OM_uint32 min;
            gss_buffer_desc buffer;
            gss_cred_id_t g_cred;
            gss_name_t g_name;
            char * g_subject;

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

            globus_ftp_control_auth_info_init(
                    &auth_info,
                    g_cred,
                    GLOBUS_FALSE,
                    "anonymous",
                    "globus@",
                    0,
                    g_subject);

            globus_ftp_control_authenticate(handle,
                    &auth_info,
                    GLOBUS_TRUE,
                    response_cb,
                    0);
        }
        else
        {
            globus_ftp_control_quit(
                    handle,
                    response_cb,
                    0);
            globus_mutex_lock(&the_lock);
            done = 1;
            globus_cond_signal(&the_cond);
            globus_mutex_unlock(&the_lock);
        }
    }
}

int main(int                                    argc,
        char **                                 argv)
{
    globus_ftp_control_handle_t         handle;
    char * g_host = NULL;
    unsigned short g_port = 0;
    int i;
    int rc;

    LTDL_SET_PRELOADED_SYMBOLS();

    for (i = 1; i < argc; i++)
    {
        if ((strcmp(argv[i], "--host")==0) && ((i+2) < argc))
        {
            g_host = argv[++i];
            g_port = atoi(argv[++i]);
        }
    }

    if (g_host == NULL || g_port == 0)
    {
        fprintf(stderr, "Usage: %s --host HOST PORT\n", argv[0]);
        exit(99);
    }

    printf("1..1\n");

    globus_module_activate(GLOBUS_FTP_CONTROL_MODULE);

    globus_mutex_init(&the_lock, 0);
    globus_cond_init(&the_cond, 0);

    globus_ftp_control_handle_init(&handle);

    globus_ftp_control_connect(&handle,
            g_host,
            g_port,
            response_cb,
            0);
    globus_mutex_lock(&the_lock);
    while(!done)
    {
        globus_cond_wait(&the_cond, &the_lock);
    }
    globus_mutex_unlock(&the_lock);

    rc = globus_module_deactivate_all();
    printf("%s - get_linger_close\n", (done==1&&rc==0)?"ok":"not ok");
    return (done !=1 || rc != 0);
}
