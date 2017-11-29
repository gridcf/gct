/*
 * Copyright 1999-2006 University of Chicago
 * Copyright 2017 Grid Community Forum
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
#include "globus_gridftp_server_control.h"
#include "globus_xio_tcp_driver.h"
#include "globus_xio_gssapi_ftp.h"

/*
#define FTP_DRIVER_NAME "ftp_cmd"
#define MODE GLOBUS_XIO_DRIVER_FTP_CMD_BUFFER
*/

#define FTP_DRIVER_NAME "gssapi_ftp"
#define MODE GLOBUS_XIO_GSSAPI_ATTR_TYPE_SUPER_MODE

#define REPLY_220 "220 Hello there from simple ftp.\r\n"
#define FTP_USER_ARG (void*)0x15

#define USER_DATA_HANDLE    ((void *) 0xFF)

char *  CONTACT_STRINGS[]     = {"127.0.0.1:2", NULL};

#define CONTACT_STRINGS_COUNT   1

static globus_mutex_t                       gs_l_mutex;
static globus_cond_t                        gs_l_cond;

static void
logging_func(
    globus_gridftp_server_control_t     server_handle,
    const char *                        full_command,
    int                                 cls,
    void *                              user_arg)
{
    time_t tm = time(NULL);
    char * tm_str = ctime(&tm);
    int len = strlen(tm_str);

    tm_str[len - 1] = '\0';

    fprintf(stdout, "%s::  %s", tm_str, full_command);
}

static void
globus_gs_cmd_site(
    globus_gridftp_server_control_op_t      op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    if(strcmp(cmd_a[1], "MINE") == 0)
    {
        globus_gsc_959_finished_command(
            op, "200 successful sire interception\r\n");
    }
    else
    {
        globus_assert(0);
    }
}


static globus_mutex_t                       globus_l_mutex;
static globus_cond_t                        globus_l_cond;
static globus_bool_t                        globus_l_done = GLOBUS_FALSE;

void
test_res(
    globus_result_t                         res,
    int                                     line)
{
    if(res == GLOBUS_SUCCESS)
    {
        printf(" ok: %d\n", line);
        return;
    }
                                                                                
    fprintf(stderr, "ERROR at line: %d: %s\n", 
        line,
        globus_error_print_chain(
            globus_error_get(res)));
                                                                                
    globus_assert(0);
}

static void
globus_l_done_cb(
    globus_gridftp_server_control_t         server,
    globus_result_t                         res,
    void *                                  user_arg)
{
    fprintf(stdout, "Done callback received\n");
    test_res(res, __LINE__);

    globus_mutex_lock(&globus_l_mutex);
    {
        globus_l_done = GLOBUS_TRUE;
        globus_cond_signal(&globus_l_cond);
    }
    globus_mutex_unlock(&globus_l_mutex);
}


static void
globus_l_resource_cb(
    globus_gridftp_server_control_op_t      op,
    const char *                            path,
    globus_gridftp_server_control_resource_mask_t mask,
    void *                                  user_arg)
{
    globus_result_t                         res;
    globus_gridftp_server_control_response_t response_type;
    globus_gridftp_server_control_stat_t    stat_info;
    struct stat                             st;
    static int                              x = 0;
    char *                                  msg;
    int uid,gid;

    if(stat(path, &st) < 0)
    {
        globus_gridftp_server_control_finished_resource(
            op,
            NULL,
            0,
            0,
            0,
            0,
            GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED,
            NULL);
        return;
    }

    stat_info.mode = st.st_mode;
    stat_info.nlink = st.st_nlink;
    stat_info.uid = st.st_uid;
    stat_info.gid = st.st_gid;
    stat_info.size = st.st_size;
    stat_info.atime = st.st_atime;
    stat_info.ctime = st.st_ctime;
    stat_info.mtime = st.st_mtime;
    strcpy(stat_info.name, path);

    switch(x % 4)
    {
        case 0:
            msg = "GOOD";
            response_type = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS;
            break;
        case 1:
            msg = "Pretend bad";
            response_type = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_PATH_INVALID;
            break;
        case 2:
            msg = "Mr. Rogers";
            response_type = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_INVALID_FILE_TYPE;
            break;
        case 3:
            msg = "Ice Cream.";
            response_type = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACCESS_DENIED;
            break;
    }
    uid = stat_info.uid;
    gid = stat_info.gid;

    res = globus_gridftp_server_control_finished_resource(
        op,
        &stat_info,
        1,
        uid,
        1,
        &gid,
        response_type,
        msg);
    globus_assert(res == GLOBUS_SUCCESS);

    x++;
}

static void
event_cb(
    globus_gridftp_server_control_op_t      op,
    int                                     event_type,
    void *                                  user_arg)
{
    static globus_size_t                    nbytes = 0;

    nbytes += 1048576;

    globus_gridftp_server_control_event_send_perf(op, 0, nbytes);
}

static void
list_cb(
    globus_gridftp_server_control_op_t      op,
    void *                                  data_handle,
    const char *                            path,
    const char *                            fact_str,
    int                                     depth,
    int                                     traversal_option,
    void *                                  user_arg)
{
    globus_gridftp_server_control_begin_transfer(op);
    globus_gridftp_server_control_finished_transfer(
        op, GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS, NULL);
}

static void
passive_connect(
    globus_gridftp_server_control_op_t      op,
    globus_gridftp_server_control_network_protocol_t net_prt,
    int                                     max,
    const char *                            pathname,
    void *                                  user_arg)
{
    globus_gridftp_server_control_finished_passive_connect(
        op,
        USER_DATA_HANDLE,
        GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_BI,
        (const char **)CONTACT_STRINGS,
        CONTACT_STRINGS_COUNT,
        GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS,
        NULL);
}

static void
active_connect(
    globus_gridftp_server_control_op_t      op,
    globus_gridftp_server_control_network_protocol_t net_prt,
    const char **                           cs,
    int                                     cs_count,
    void *                                  user_arg)
{
    globus_gridftp_server_control_finished_active_connect(
        op,
        USER_DATA_HANDLE,
        GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_BI,
        GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS,
        NULL);
}

static void
data_destroy_cb(
    void *                                  user_data_handle,
    void *                                  user_arg)
{
    fprintf(stdout, "data_destroy_cb()\n");
    globus_assert(user_data_handle == USER_DATA_HANDLE);
}

void
abort_cb(
    globus_gridftp_server_control_op_t      op,
    int                                     event_type,
    void *                                  user_arg)
{
    globus_mutex_lock(&gs_l_mutex);
    {
        globus_l_done = GLOBUS_TRUE;
        globus_cond_signal(&gs_l_cond);
    }
    globus_mutex_unlock(&gs_l_mutex);
}

static void
transfer(
    globus_gridftp_server_control_op_t      op,
    void *                                  data_handle,
    const char *                            local_target,
    const char *                            mod_name,
    const char *                            mod_parms,
    globus_range_list_t                     range_list,
    void *                                  user_arg)
{
    int                                     ctr;
    globus_size_t                           off = 0;
    globus_size_t                           len = 256;
    globus_abstime_t                        abstime;

    globus_gridftp_server_control_events_enable(
        op,
        GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_ABORT,
        abort_cb,
        NULL);

    globus_gridftp_server_control_events_enable(
        op,
        GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_PERF |
                GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_RESTART,

        event_cb,
        NULL);

    globus_gridftp_server_control_begin_transfer(op);

    globus_mutex_lock(&gs_l_mutex);
    {
        for(ctr = 0; ctr < 500 && !globus_l_done; ctr++)
        {
            GlobusTimeAbstimeSet(abstime, 0, 50000);
            globus_cond_timedwait(&gs_l_cond, &gs_l_mutex, &abstime);
            off += len;
        }
    }
    globus_mutex_unlock(&gs_l_mutex);

    globus_gridftp_server_control_finished_transfer(
        op, GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS, NULL);



    globus_l_done = GLOBUS_FALSE;
}

void
auth_func(
    globus_gridftp_server_control_op_t      op,
    globus_gridftp_server_control_security_type_t type,
    gss_ctx_id_t                            context,
    const char *                            subject,
    const char *                            user_name,
    const char *                            pw,
    void *                                  user_arg)
{
    fprintf(stderr, "User: %s Pass: %s: type = %d\n", user_name, pw, type);

    if(strcmp(user_name, "failme") == 0)
    {
        globus_gridftp_server_control_finished_auth(
            op, 0, GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED, NULL);
    }
    else
    {
        globus_gridftp_server_control_finished_auth(
            op, "username", GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS, NULL);
    }
}

int
main(
    int                                     argc,
    char **                                 argv)
{
    globus_xio_driver_t                     tcp_driver;
    globus_xio_stack_t                      stack;
    globus_xio_handle_t                     xio_handle;
    globus_xio_server_t                     xio_server;
    globus_result_t                         res;
    char *                                  cs;
    globus_gridftp_server_control_attr_t    ftp_attr;
    globus_gridftp_server_control_t         ftp_server;
    globus_xio_system_socket_t              system_handle;

    globus_module_activate(GLOBUS_XIO_MODULE);
    globus_module_activate(GLOBUS_GRIDFTP_SERVER_CONTROL_MODULE);

    globus_mutex_init(&gs_l_mutex, NULL);
    globus_cond_init(&gs_l_cond, NULL);

    /*
     *  set up the xio handle
     */
    res = globus_xio_driver_load("tcp", &tcp_driver);
    test_res(res, __LINE__);

    res = globus_xio_stack_init(&stack, NULL);
    test_res(res, __LINE__);

    res = globus_xio_stack_push_driver(stack, tcp_driver);
    test_res(res, __LINE__);

    res = globus_xio_server_create(&xio_server, NULL, stack);
    test_res(res, __LINE__);

    globus_xio_stack_destroy(stack);

    res = globus_xio_server_get_contact_string(xio_server, &cs);
    test_res(res, __LINE__);
    fprintf(stdout, "%s\n", cs);
    globus_free(cs);

    res = globus_xio_server_accept(&xio_handle, xio_server);
    test_res(res, __LINE__);

    fprintf(stdout, "xio connection esstablished.\n");
    /*
     *  server connection is all set up, hand it to server_lib
     */
    res = globus_gridftp_server_control_init(&ftp_server);
    test_res(res, __LINE__);

    res = globus_gridftp_server_control_attr_init(&ftp_attr);
    test_res(res, __LINE__);

    globus_xio_server_close(xio_server);

    if(argc > 1)
    {
        globus_gridftp_server_control_attr_set_security(
            ftp_attr, GLOBUS_GRIDFTP_SERVER_LIBRARY_GSSAPI | 
                GLOBUS_GRIDFTP_SERVER_LIBRARY_NONE);
    }
    else
    {
        globus_gridftp_server_control_attr_set_security(
            ftp_attr, GLOBUS_GRIDFTP_SERVER_LIBRARY_NONE);
    }

    res = globus_gridftp_server_control_attr_set_auth(
        ftp_attr, auth_func, NULL);
    test_res(res, __LINE__);

    res = globus_gridftp_server_control_attr_set_resource(
        ftp_attr, globus_l_resource_cb, NULL);
    test_res(res, __LINE__);

    res = globus_gridftp_server_control_attr_set_list(
        ftp_attr, list_cb, NULL);
    test_res(res, __LINE__);

    res = globus_gridftp_server_control_attr_set_idle_time(
        ftp_attr, 900, 60);
    test_res(res, __LINE__);

    res = globus_gridftp_server_control_attr_set_banner(
        ftp_attr, "This is 1 line of banner\nthis is line 2\nline 3");
    test_res(res, __LINE__);

    res = globus_gridftp_server_control_attr_set_message(
        ftp_attr, "Setting the message after login, 1 line\n");
    test_res(res, __LINE__);

    res = globus_gridftp_server_control_attr_set_log(
        ftp_attr, logging_func, GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_ALL, NULL);
    test_res(res, __LINE__);

    res = globus_gridftp_server_control_attr_data_functions(
        ftp_attr, active_connect, NULL, 
        passive_connect, NULL, data_destroy_cb, NULL);

    res = globus_gridftp_server_control_attr_add_send(
        ftp_attr, NULL, transfer, NULL);
    test_res(res, __LINE__);

    res = globus_gridftp_server_control_attr_add_recv(
        ftp_attr, NULL, transfer, NULL);
    test_res(res, __LINE__);

    res = globus_xio_handle_cntl(xio_handle, tcp_driver,
            GLOBUS_XIO_TCP_GET_HANDLE, &system_handle);
    test_res(res, __LINE__);

    globus_gsc_959_command_add(
        ftp_server,
        "SITE MINE",
        globus_gs_cmd_site,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "SITE <sp> MINE!!!!",
        NULL);

    globus_mutex_lock(&globus_l_mutex);
    {
        res = globus_gridftp_server_control_start(
            ftp_server, ftp_attr, system_handle, 
            globus_l_done_cb, FTP_USER_ARG);
        test_res(res, __LINE__);

        while(!globus_l_done)
        {
            globus_cond_wait(&globus_l_cond, &globus_l_mutex);
        }
    }
    globus_mutex_unlock(&globus_l_mutex);

    globus_xio_close(xio_handle, NULL);
    fprintf(stdout, "Ending...\n");
    res = globus_gridftp_server_control_attr_destroy(ftp_attr);
    test_res(res, __LINE__);
    res = globus_gridftp_server_control_destroy(ftp_server);
    test_res(res, __LINE__);

    globus_module_deactivate(GLOBUS_GRIDFTP_SERVER_CONTROL_MODULE);
    globus_module_deactivate(GLOBUS_XIO_MODULE);

    return 0;
}
