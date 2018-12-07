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

#include "globus_i_gridftp_server.h"
#include "version.h"

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#ifdef TARGET_ARCH_WIN32
#define S_ISLNK(x) 0
#define lstat(x,y) stat(x,y)
#define mkdir(x,y) mkdir(x)
#define chown(x,y,z) -1
#define symlink(x,y) -1
#define readlink(x,y,z) 0
#define realpath(x,y) strcpy(y,x)
#define scandir(a,b,c,d) 0
#define alphasort(x,y) 0
#endif

#ifdef TARGET_ARCH_WIN32

#define lstat(x,y) stat(x,y)
#define S_ISLNK(x) 0

#define getuid() 1
#define getpwuid(x) 0
#define initgroups(x,y) -1
#define getgroups(x,y) -1
#define setgroups(x,y) 0
#define setgid(x) 0
#define setuid(x) 0
#define sync() 0
#define fork() -1
#define setsid() -1
#define chroot(x) -1
#define globus_libc_getpwnam_r(a,b,c,d,e) -1
#define globus_libc_getpwuid_r(a,b,c,d,e) -1
#endif

#ifdef TARGET_ARCH_WIN32

#define getpwnam(x) 0

#define getgrgid(x) 0
#define getgrnam(x) 0

#define lstat(x,y) stat(x,y)
#define S_ISLNK(x) 0

#endif


struct passwd *
globus_l_gfs_getpwuid(
    uid_t                               uid);

GlobusDebugDefine(GLOBUS_GRIDFTP_SERVER);

typedef struct
{
    globus_xio_handle_t                 xio_handle;
    char *                              remote_contact;
    char *                              local_contact;

    char *                              scks_alg;
    char *                              scks_val;
    char *                              rnfr_pathname;
    char *                              slfr_pathname;

    globus_i_gfs_server_close_cb_t      close_func;
    void *                              close_arg;

    void *                              session_arg;
    char *                              home_dir;
    char *                              username;
    globus_gridftp_server_control_t     server_handle;
    globus_object_t *                   close_error;
    
    globus_hashtable_t                  custom_cmd_table;
} globus_l_gfs_server_instance_t;

typedef struct
{
    globus_l_gfs_server_instance_t *    instance;
    globus_gridftp_server_control_op_t  control_op;
    void *                              event_arg;
    void *                              info;
    globus_bool_t                       transfer_events;

    globus_gfs_operation_type_t         bounce_type;
    globus_i_gfs_data_callback_t        bounce_cb;
    void *                              bounce_info; 
} globus_l_gfs_request_info_t;

typedef struct globus_l_gfs_auth_info_s
{
    globus_l_gfs_server_instance_t *    instance;
    globus_gridftp_server_control_op_t  control_op;
    globus_gfs_session_info_t *         session_info;
} globus_l_gfs_auth_info_t;

static globus_bool_t                    globus_l_gfs_control_active = GLOBUS_FALSE;
static globus_list_t *                  globus_l_gfs_server_handle_list;
static globus_mutex_t                   globus_l_gfs_control_mutex;
static globus_bool_t                    globus_l_gfs_control_should_be_gone = GLOBUS_FALSE;

char *
globus_i_gsc_string_to_959(
    int                                 code,
    const char *                        in_str, 
    const char *                        preline);

static
void
globus_l_gfs_control_log(
    globus_gridftp_server_control_t     server_handle,
    const char *                        message,
    int                                 type,
    void *                              user_arg);
    
static
void
globus_l_gfs_request_custom_command(
    globus_gsc_959_op_t                 op,
    const char *                        full_command,
    char **                             cmd_array,
    int                                 argc,
    void *                              user_arg);

static int
globus_l_gfs_activate()
{
    int                                 rc = 0;
        
    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    if(rc != 0)
    {
        return rc;
    }
    rc = globus_module_activate(GLOBUS_FTP_CONTROL_MODULE);
    if(rc != 0)
    {
        return rc;
    }
    rc = globus_module_activate(GLOBUS_GRIDFTP_SERVER_CONTROL_MODULE);
    if(rc != 0)
    {
        return rc;
    }
    rc = globus_module_activate(GLOBUS_GSI_AUTHZ_MODULE);
    if(rc != 0)
    {
        return rc;
    }
    
    GlobusDebugInit(GLOBUS_GRIDFTP_SERVER,
        ERROR WARNING TRACE INTERNAL_TRACE INFO STATE INFO_VERBOSE);

    return rc;
}

static int
globus_l_gfs_deactivate()
{
    int                                 rc;

    rc = globus_module_deactivate_all();

    return rc;
}

globus_module_descriptor_t              globus_i_gfs_module =
{
    "globus_gridftp_server",
    globus_l_gfs_activate,
    globus_l_gfs_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

static
void
globus_l_gfs_conn_max_change_cb(
    const char *                        opt_name,
    int                                 val,
    void *                              user_arg)
{
    int                                 i;
    int                                 kill_count;
    globus_list_t *                     list;
    globus_l_gfs_server_instance_t *    instance;
    GlobusGFSName(globus_l_gfs_conn_max_change_cb);
    GlobusGFSDebugEnter();

    /* if set to mac value there is nothing to do */
    if(val <= 0)
    {
        return;
    }
    globus_mutex_lock(&globus_l_gfs_control_mutex);
    {
        kill_count = globus_list_size(globus_l_gfs_server_handle_list) - val;

        for(i = 0, list = globus_l_gfs_server_handle_list;
            i < kill_count && !globus_list_empty(list);
            i++, list = globus_list_rest(list))
        {
            instance = (globus_l_gfs_server_instance_t *)
                globus_list_first(list);
            globus_gridftp_server_control_stop(instance->server_handle);
        }
    }
    globus_mutex_unlock(&globus_l_gfs_control_mutex);

    GlobusGFSDebugExit();
}

void
globus_i_gfs_control_init()
{
    globus_i_gfs_config_option_cb_ent_t * cb_handle;
    GlobusGFSName(globus_i_gfs_control_init);
    GlobusGFSDebugEnter();

    globus_l_gfs_server_handle_list = NULL;
    globus_mutex_init(&globus_l_gfs_control_mutex, NULL);
    globus_l_gfs_control_active = GLOBUS_TRUE;

    globus_gfs_config_add_cb(
        &cb_handle,
        "connections_max",
        globus_l_gfs_conn_max_change_cb,
        NULL);
         
    GlobusGFSDebugExit();
}

void
globus_i_gfs_control_stop()
{
    globus_list_t *                     list;
    globus_l_gfs_server_instance_t *    instance;
    GlobusGFSName(globus_i_gfs_control_stop);
    GlobusGFSDebugEnter();

    globus_mutex_lock(&globus_l_gfs_control_mutex);
    {
        globus_l_gfs_control_active = GLOBUS_FALSE;
        globus_l_gfs_control_should_be_gone = GLOBUS_TRUE;
        
        for(list = globus_l_gfs_server_handle_list;
            !globus_list_empty(list);
            list = globus_list_rest(list))
        {
            instance = (globus_l_gfs_server_instance_t *) 
                globus_list_first(list);

            globus_gridftp_server_control_stop(instance->server_handle);
        }
    }
    globus_mutex_unlock(&globus_l_gfs_control_mutex);

    GlobusGFSDebugExit();
}

static
globus_result_t
globus_l_gfs_request_info_init(
    globus_l_gfs_request_info_t **      u_request,
    globus_l_gfs_server_instance_t *    instance,
    globus_gridftp_server_control_op_t  control_op,
    void *                              info_struct)
{
    globus_result_t                     result;
    globus_l_gfs_request_info_t *       request;
    GlobusGFSName(globus_l_gfs_request_info_init);
    GlobusGFSDebugEnter();

    request = (globus_l_gfs_request_info_t *)
        globus_calloc(1, sizeof(globus_l_gfs_request_info_t));
    if(request == NULL)
    {
        result = GlobusGFSErrorMemory("request");
        goto error;
    }
    
    request->control_op = control_op;
    request->instance = instance;
    request->info = info_struct;
    request->transfer_events = GLOBUS_FALSE;

    *u_request = request;

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusGFSDebugExitWithError();
    return result;
}

static
void
globus_l_gfs_request_info_destroy(
    globus_l_gfs_request_info_t *       request)
{
    GlobusGFSName(globus_l_gfs_request_info_destroy);
    GlobusGFSDebugEnter();

    globus_free(request);

    GlobusGFSDebugExit();
}


static
globus_result_t
globus_l_gfs_get_full_path(
    globus_l_gfs_server_instance_t *        instance,
    const char *                            in_path,
    char **                                 ret_path,
    int                                     access_type)
{
    globus_result_t                         result = GLOBUS_SUCCESS;
    char *                                  cwd = NULL;
    GlobusGFSName(globus_l_gfs_get_full_path);
    GlobusGFSDebugEnter();

    result = globus_gridftp_server_control_get_cwd(
        instance->server_handle, &cwd);
    if(result != GLOBUS_SUCCESS || cwd == GLOBUS_NULL)
    {
        result = GlobusGFSErrorGeneric("invalid cwd");
        goto done;
    }

    result = globus_i_gfs_get_full_path(
            instance->home_dir,
            cwd,
            instance->session_arg,
            in_path,
            ret_path,
            access_type);
    if (cwd)
    {
        free(cwd);
    }

    if (result)
    {
        goto done;
    }

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

done:
    GlobusGFSDebugExitWithError();
    return result;
}

static
globus_result_t
globus_l_gfs_kvstr_path(
    globus_l_gfs_server_instance_t *    instance,
    int                                 access,
    char *                              kvstr,
    char **                             out_kvstr)
{
    char *                              chk_path = NULL;
    char *                              real_path = NULL;
    char *                              new_kvstr = NULL;
    globus_result_t                     result = GLOBUS_FAILURE;

    chk_path = globus_i_gfs_kv_getval(kvstr, "PATH", 1);
    if(!chk_path)
    {
        goto err;
    }
    result = globus_l_gfs_get_full_path(instance, chk_path, &real_path, access);
    if(result != GLOBUS_SUCCESS || real_path == NULL)
    {
        goto err;
    }
    if(strcmp(real_path, chk_path) != 0)
    {
        new_kvstr = globus_i_gfs_kv_replaceval(kvstr, "PATH", real_path, 1);
        if(!new_kvstr)
        {
            goto err;
        }
    }
    else
    {
        new_kvstr = globus_libc_strdup(kvstr);
    }
    
    globus_free(chk_path);
    globus_free(real_path);
    
    *out_kvstr = new_kvstr;
    return GLOBUS_SUCCESS;
    
err:
    if(chk_path)
    {
        globus_free(chk_path);
    }
    if(real_path)
    {
        globus_free(real_path);
    }
        
    return result;    
}


static
void
globus_l_gfs_channel_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_gfs_server_instance_t *    instance;
    GlobusGFSName(globus_l_gfs_channel_close_cb);
    GlobusGFSDebugEnter();

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    globus_gfs_log_message(
        GLOBUS_GFS_LOG_INFO,
        _FSSL("Closed connection from %s\n",NULL),
        instance->remote_contact);
    globus_gfs_log_event(
        GLOBUS_GFS_LOG_INFO,
        GLOBUS_GFS_LOG_EVENT_END,
        "session",
        0,
        "remotehost=%s", instance->remote_contact);


    if(instance->session_arg != NULL)
    {
        globus_i_gfs_data_session_stop(NULL, instance->session_arg);
    }
    if(instance->close_func)
    {
        instance->close_func(instance->close_arg, instance->close_error);
    }
    
    if(instance->home_dir)
    {
        globus_free(instance->home_dir);
    }
    if(instance->username)
    {
        globus_free(instance->username);
    }
    if(instance->scks_alg)
    {
        globus_free(instance->scks_alg);
    }
    if(instance->scks_val)
    {
        globus_free(instance->scks_val);
    }
    globus_free(instance->local_contact);
    globus_free(instance->remote_contact);
    globus_free(instance);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_done_cb(
    globus_gridftp_server_control_t     server,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_gfs_server_instance_t *    instance;
    GlobusGFSName(globus_l_gfs_done_cb);
    GlobusGFSDebugEnter();

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    globus_mutex_lock(&globus_l_gfs_control_mutex);
    {
        globus_l_gfs_control_should_be_gone = GLOBUS_TRUE;
        
        globus_list_remove(&globus_l_gfs_server_handle_list,
            globus_list_search(globus_l_gfs_server_handle_list, instance));
    }
    globus_mutex_unlock(&globus_l_gfs_control_mutex);

    globus_gridftp_server_control_destroy(instance->server_handle);

    instance->close_error = 
        (result == GLOBUS_SUCCESS ? NULL : globus_error_get(result));
    result = globus_xio_register_close(
        instance->xio_handle,
        GLOBUS_NULL,
        globus_l_gfs_channel_close_cb,
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        globus_l_gfs_channel_close_cb(
            instance->xio_handle,
            GLOBUS_SUCCESS,
            instance);
    }

    GlobusGFSDebugExit();
}


static
void
globus_l_gfs_auth_session_cb(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg)
{
    globus_l_gfs_auth_info_t *          auth_info;
    char *                              tmp_str;
    GlobusGFSName(globus_l_gfs_auth_session_cb);
    GlobusGFSDebugEnter();

    auth_info = (globus_l_gfs_auth_info_t *) user_arg;

    if(reply->result != GLOBUS_SUCCESS)
    {
        tmp_str = globus_error_print_friendly(
            globus_error_peek(reply->result));
        globus_gridftp_server_control_finished_auth(
            auth_info->control_op,
            NULL,
            GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED,
            tmp_str);
        globus_free(tmp_str);
    }
    else
    {
        auth_info->instance->session_arg = reply->info.session.session_arg;
        if(auth_info->session_info->subject != NULL)
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_INFO,
                "DN %s successfully authorized.\n",
                auth_info->session_info->subject);
        }

        globus_gfs_log_message(
            GLOBUS_GFS_LOG_INFO,
            "User %s successfully authorized.\n",
            reply->info.session.username);

        if(reply->info.session.home_dir != NULL && 
            globus_i_gfs_config_bool("use_home_dirs"))
        {
#ifdef WIN32            
            if(isalpha(reply->info.session.home_dir[0]) && 
                reply->info.session.home_dir[1] == ':')
            {
                reply->info.session.home_dir[1] = reply->info.session.home_dir[0];
                reply->info.session.home_dir[0] = '/';
            } 
#endif            
            globus_gridftp_server_control_set_cwd(
                auth_info->instance->server_handle,
                reply->info.session.home_dir);
        }
        
        if(reply->info.session.home_dir)
        {
            auth_info->instance->home_dir = 
                globus_libc_strdup(reply->info.session.home_dir);
        }
        /*      
        else
        {
            auth_info->instance->home_dir = globus_libc_strdup("/");
        }
        */
        auth_info->instance->username = 
            globus_libc_strdup(reply->info.session.username);
        
        if(reply->op_info && 
            !globus_hashtable_empty(&reply->op_info->custom_command_table))
        {
            globus_list_t *             list;
            int                         rc;
            globus_result_t             result;
            globus_i_gfs_cmd_ent_t *    cmd_ent;
            
            auth_info->instance->custom_cmd_table = 
                reply->op_info->custom_command_table;
                
            rc = globus_hashtable_to_list(
                &reply->op_info->custom_command_table, &list);
            
            while(!globus_list_empty(list))
            {
                cmd_ent = (globus_i_gfs_cmd_ent_t *) 
                    globus_list_remove(&list, list);
                    
                result = globus_gsc_959_command_add(
                    auth_info->instance->server_handle,
                    cmd_ent->cmd_name,
                    globus_l_gfs_request_custom_command,
                    GLOBUS_GSC_COMMAND_POST_AUTH,
                    cmd_ent->min_argc,
                    cmd_ent->max_argc,
                    cmd_ent->help_str,
                    auth_info->instance);
                if(result != GLOBUS_SUCCESS)
                {
                    char *              tmp_msg;
                    tmp_msg = globus_error_print_friendly(
                        globus_error_peek(result));
                        
                    globus_gfs_log_message(
                        GLOBUS_GFS_LOG_ERR,
                        "Could not register command '%s':\n%s",
                        cmd_ent->cmd_name,
                        tmp_msg);
                    globus_free(tmp_msg);
                }
            }
        }
        globus_gridftp_server_control_finished_auth(
            auth_info->control_op,
            reply->info.session.username,
            GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS,
            NULL);
    }
    globus_free(auth_info->session_info->username);
    if(auth_info->session_info->password != NULL)
    {
        globus_free(auth_info->session_info->password);
    }
    if(auth_info->session_info->subject != NULL)
    {
        globus_free(auth_info->session_info->subject);
    }
    if(auth_info->session_info->host_id != NULL)
    {
        globus_free(auth_info->session_info->host_id);
    }
    globus_free(auth_info->session_info);
    globus_free(auth_info);

    GlobusGFSDebugExit();
}


static
void
globus_l_gfs_request_auth(
    globus_gridftp_server_control_op_t  control_op,
    globus_gridftp_server_control_security_type_t secure_type,
    gss_ctx_id_t                        context,
    const char *                        subject,
    const char *                        user_name,
    const char *                        pw,
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_gfs_session_info_t *         session_info;
    globus_l_gfs_auth_info_t *          auth_info;
    globus_l_gfs_server_instance_t *    instance;
    GlobusGFSName(globus_l_gfs_request_auth);
    GlobusGFSDebugEnter();

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    session_info = (globus_gfs_session_info_t *)
        calloc(1, sizeof(globus_gfs_session_info_t));
    if(session_info == NULL)
    {
        goto session_error;
    }

    globus_gfs_log_event(
        GLOBUS_GFS_LOG_INFO,
        GLOBUS_GFS_LOG_EVENT_END,
        "session.authn",
        0,
        "user=%s DN=\"%s\"",
        user_name, 
        subject ? subject : "");

    result = globus_gridftp_server_control_get_data_auth(
        control_op,
        NULL,
        NULL,
        NULL,
        &session_info->del_cred);
    if(result != GLOBUS_SUCCESS)
    {
        goto del_error;
    }
    session_info->username = strdup(user_name);
    if(session_info->username == NULL)
    {
        goto del_error;
    }
    if(strcmp(session_info->username, GLOBUS_MAPPING_STRING) == 0)
    {
        session_info->map_user = GLOBUS_TRUE;
    }
    if(pw != NULL)
    {
        session_info->password = strdup(pw);
        if(session_info->password == NULL)
        {
            goto user_error;
        }
    }
    if(subject != NULL)
    {
        session_info->subject = strdup(subject);
        if(session_info->subject == NULL)
        {
            goto user_error;
        }
    }
    if(instance->remote_contact != NULL)
    {
        session_info->host_id = strdup(instance->remote_contact);
        if(session_info->host_id == NULL)
        {
            goto user_error;
        }
    }
       
    auth_info = (globus_l_gfs_auth_info_t *) calloc(1,
        sizeof(globus_l_gfs_auth_info_t));
    if(auth_info == NULL)
    {
        goto pw_error;
    }
    auth_info->instance = instance;
    auth_info->control_op = control_op;
    auth_info->session_info = session_info;

    globus_i_gfs_data_session_start(
        NULL,
        context,
        session_info,
        globus_l_gfs_auth_session_cb,
        auth_info);

    GlobusGFSDebugExit();
    return;

pw_error:
    globus_free(session_info->password);
user_error:
    globus_free(session_info->username);
del_error:
    globus_free(session_info);
session_error:
    globus_gridftp_server_control_finished_auth(
        control_op,
        NULL,
        GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_PANIC,
        _FSSL("internal error: session_cb",NULL));

    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_data_stat_cb(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg)
{
    char *                              tmp_str;
    globus_gridftp_server_control_op_t  op;
    globus_l_gfs_request_info_t *       request;
    globus_gfs_stat_info_t *            info;
    GlobusGFSName(globus_l_gfs_data_stat_cb);
    GlobusGFSDebugEnter();

    request = (globus_l_gfs_request_info_t *) user_arg;
    op = request->control_op;

    globus_assert(op != NULL);
    if(reply->result != GLOBUS_SUCCESS)
    {
        int                             ftp_code;
        
        /* use code and message from reply if set */
        if(reply->code && reply->msg)
        {
            ftp_code = reply->code;
            tmp_str = strdup(reply->msg);
        }
        else
        {
            ftp_code = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED;
            tmp_str = globus_error_print_friendly(
                globus_error_peek(reply->result));
        }

        globus_gridftp_server_control_finished_resource(
            op,
            NULL,
            0,
            0,
            0,
            NULL,
            ftp_code,
            tmp_str);
        globus_free(tmp_str);
    }
    else if(reply->code / 100 == 1)
    {
        globus_gridftp_server_control_finished_resource(
            op,
            reply->info.stat.stat_array,
            reply->info.stat.stat_count,
            reply->info.stat.uid,
            reply->info.stat.gid_count,
            reply->info.stat.gid_array,
            GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_PARTIAL_SUCCESS,
            GLOBUS_NULL);
    }
    else
    {
        globus_gridftp_server_control_finished_resource(
            op,
            reply->info.stat.stat_array,
            reply->info.stat.stat_count,
            reply->info.stat.uid,
            reply->info.stat.gid_count,
            reply->info.stat.gid_array,
            GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS,
            GLOBUS_NULL);
    }
    
    if(reply->code / 100 != 1)
    {
    info = (globus_gfs_stat_info_t *) request->info;
    if(info)
    {
        if(info->pathname)
        {
            globus_free(info->pathname);
        }
        globus_free(info);
    }
    globus_l_gfs_request_info_destroy(request);
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_request_stat(
    globus_gridftp_server_control_op_t  op,
    const char *                        path,
    globus_gridftp_server_control_resource_mask_t mask,
    void *                              user_arg)
{
    char *                              tmp_str;
    globus_l_gfs_server_instance_t *    instance;
    globus_gfs_stat_info_t *            stat_info;
    globus_l_gfs_request_info_t *       request;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_request_stat);
    GlobusGFSDebugEnter();

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    stat_info = (globus_gfs_stat_info_t *) 
        globus_calloc(1, sizeof(globus_gfs_stat_info_t));

    result = globus_l_gfs_request_info_init(
        &request, instance, op, stat_info);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }

    result = globus_l_gfs_get_full_path(
        instance, path, &stat_info->pathname, GFS_L_LIST);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }
    stat_info->file_only =
        (mask & GLOBUS_GRIDFTP_SERVER_CONTROL_RESOURCE_FILE_ONLY) ?
            GLOBUS_TRUE : GLOBUS_FALSE;

    globus_i_gfs_data_request_stat(
        NULL,
        instance->session_arg,
        0,
        stat_info,
        globus_l_gfs_data_stat_cb,
        request);

    GlobusGFSDebugExit();
    return;
    
error_init:
    {
        int                             ftp_code;
        
        /* pull response code from error */
        if((ftp_code = globus_gfs_error_get_ftp_response_code(
            globus_error_peek(result))) == 0)
        {
            ftp_code = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED;
        }
        tmp_str = globus_error_print_friendly(globus_error_peek(result));
        globus_gridftp_server_control_finished_resource(
            op,
            NULL,
            0,
            0,
            0,
            NULL,
            ftp_code,
            tmp_str);
        globus_free(tmp_str);
    }
    GlobusGFSDebugExitWithError();
}

char *
globus_i_gsc_get_cmd_string(
    void *                              user_arg)
{
    globus_l_gfs_request_info_t *       request;
    char *                              cmd = NULL;
    GlobusGFSName(globus_i_gsc_get_cmd_string);

    request = (globus_l_gfs_request_info_t *) user_arg;
    cmd = globus_gridftp_server_control_get_cmd_string(request->control_op);

    return cmd;
}

globus_result_t
globus_i_gsc_cmd_intermediate_reply(
    globus_gridftp_server_control_op_t  op,
    char *                              reply_msg);

static
void
globus_l_gfs_data_command_cb(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg)
{
    globus_gridftp_server_control_op_t  op;
    char *                              msg;
    char *                              tmp_msg;
    globus_l_gfs_request_info_t *       request;
    globus_gfs_command_info_t *         info;
    int                                 ctr;
    GlobusGFSName(globus_l_gfs_data_command_cb);
    GlobusGFSDebugEnter();

    request = (globus_l_gfs_request_info_t *) user_arg;
    info = (globus_gfs_command_info_t *) request->info;
    op = request->control_op;

    if(reply->result == GLOBUS_SUCCESS)
    {
        switch(reply->info.command.command)
        {
          case GLOBUS_GFS_CMD_MKD:
            msg = globus_common_create_string(
                "257 Directory \"%s\" created successfully.\r\n",
                reply->info.command.created_dir);
            globus_gsc_959_finished_command(op, msg);
            globus_free(msg);
            break;
            
          case GLOBUS_GFS_CMD_SITE_CHROOT:
            if(request->instance->home_dir)
            {
                globus_free(request->instance->home_dir);
            }
            request->instance->home_dir = globus_libc_strdup("/");

            globus_gridftp_server_control_set_cwd(
                request->instance->server_handle, request->instance->home_dir);
            globus_gridftp_server_control_set_cwd(
                request->instance->server_handle, NULL);
            globus_gsc_959_finished_command(op, "250 OK.\r\n");
            break;
          case GLOBUS_GFS_CMD_UPAS:
                msg = globus_common_create_string(
                    "200 %s\r\n", reply->info.command.checksum);
                globus_gsc_959_finished_command(op, msg);

            break;
          case GLOBUS_GFS_CMD_RNFR:
            request->instance->rnfr_pathname = info->pathname;
            info->pathname = NULL;
            globus_gsc_959_finished_command(op,
                "350 OK. Send RNTO with destination name.\r\n");
            break;
          case GLOBUS_GFS_CMD_SITE_SYMLINKFROM:
            request->instance->slfr_pathname = info->pathname;
            info->pathname = NULL;
            globus_gsc_959_finished_command(op,
                "350 OK. Send SITE SYMLINKTO with symlink name.\r\n");
            break;
          case GLOBUS_GFS_CMD_CKSM:
            if(reply->code / 100 == 1)
            {
                struct timeval                          now;
                gettimeofday(&now, NULL);
                msg = globus_common_create_string(
                    "%d-Status Marker\r\n"
                    " Timestamp: %ld.%01ld\r\n"
                    " Bytes Processed: %s\r\n"
                    "%d End.\r\n",
                    reply->code,
                    now.tv_sec, now.tv_usec / 100000,
                    reply->info.command.checksum,
                    reply->code);
                globus_i_gsc_cmd_intermediate_reply(op, msg);
            }
            else
            {
                msg = globus_common_create_string(
                    "213 %s\r\n", reply->info.command.checksum);
                globus_gsc_959_finished_command(op, msg);
            }
            globus_free(msg);
            break;

          case GLOBUS_GFS_CMD_WHOAMI:
            msg = globus_common_create_string(
                "200 %s\r\n", reply->info.command.checksum);
            globus_gsc_959_finished_command(op, msg);
            globus_free(msg);
            break;
            
          case GLOBUS_GFS_CMD_HTTP_PUT:
          case GLOBUS_GFS_CMD_HTTP_GET:
            if(reply->code / 100 == 1)
            {
                struct timeval                          now;
                gettimeofday(&now, NULL);
                
                switch(reply->code)
                {
                  case 112:
                    msg = globus_common_create_string(
                        "112-Perf Marker\r\n"
                        " Timestamp:  %ld.%01ld\r\n"
                        " Stripe Index: 0\r\n"
                        " Stripe Bytes Transferred: %s\r\n"
                        " Total Stripe Count: 1\r\n"
                        "112 End.\r\n",
                        now.tv_sec, now.tv_usec / 100000,
                        reply->info.command.checksum);
                        break;
                  default:
                    return;
                }
                        
                globus_i_gsc_cmd_intermediate_reply(op, msg);
                globus_free(msg);
            }
            else
            {
                if(reply->msg != NULL)
                {
                    char *                  _tmp;
                    _tmp = globus_common_create_string(
                        "OK.\n%s", reply->msg);
                    msg = globus_gsc_string_to_959(200, _tmp, NULL);
                } 
                else
                {
                    msg = strdup("200 OK.\r\n");
                }
                globus_gsc_959_finished_command(op, msg);
                globus_free(msg);
            }

            break;

          default:
            if(reply->info.command.command >= GLOBUS_GFS_MIN_CUSTOM_CMD)
            {
                if(reply->msg != NULL)
                {
                    globus_gsc_959_finished_command(op, reply->msg);
                }
            }
            else
            {                    
                globus_gsc_959_finished_command(op, "250 OK.\r\n");
            }
            break;
        }
    }
    else
    {
        int                             ftp_code;
        globus_result_t                 result;

        /* use code and message from reply if set */
        if(reply->code && reply->msg)
        {
            ftp_code = reply->code;
            msg = strdup(reply->msg);
        }
        else
        {
            ftp_code = 500;
            msg = globus_error_print_friendly(globus_error_peek(reply->result));
        }

        result = globus_i_gfs_data_virtualize_path(
            request->instance->session_arg, msg, &tmp_msg);
        if(result == GLOBUS_SUCCESS && tmp_msg != NULL)
        {
            globus_free(msg);
            msg = tmp_msg;
        }

        tmp_msg = globus_gsc_string_to_959(ftp_code, msg, NULL);
        globus_gsc_959_finished_command(op, tmp_msg);
        globus_free(tmp_msg);
        globus_free(msg);
    }
    
    if(reply->code / 100 == 1)
        return;
        
    if(info)
    {
        if(info->pathname)
        {
            globus_free(info->pathname);
        }
        if(info->cksm_alg)
        {
            globus_free(info->cksm_alg);
        }
        if(info->from_pathname)
        {
            globus_free(info->from_pathname);
        }
        if(info->chgrp_group)
        {
            globus_free(info->chgrp_group);
        }
        if(info->authz_assert)
        {
            globus_free(info->authz_assert);
        }
        if(info->op_info != NULL)
        {
            if(info->op_info->argv)
            {
                for(ctr = 0; ctr < info->op_info->argc; ctr++)
                {
                    globus_free(info->op_info->argv[ctr]);
                }
                globus_free(info->op_info->argv);
            }           
            globus_free(info->op_info);
        }
        globus_free(info);
    }
    globus_l_gfs_request_info_destroy(request);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_internal_stat_cb(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg)
{
    globus_l_gfs_request_info_t *       request;
    globus_gfs_stat_info_t *            info;
    GlobusGFSName(globus_l_gfs_data_internal_stat_cb);
    GlobusGFSDebugEnter();

    request = (globus_l_gfs_request_info_t *) user_arg;

    globus_assert(request->bounce_cb && "Invalid internal stat");

    info = (globus_gfs_stat_info_t *) request->bounce_info;
    if(info)
    {
        if(info->pathname)
        {
            globus_free(info->pathname);
        }
        globus_free(info);
    }
    request->bounce_info = NULL;

    switch(request->bounce_type)
    {
        case GLOBUS_GFS_OP_COMMAND:
            {
                globus_gfs_command_info_t * command_info;
                globus_gfs_data_reply_t     command_reply;
                
                memset(&command_reply, 0, sizeof(globus_gfs_data_reply_t));
                command_info = (globus_gfs_command_info_t *) request->info;
                command_reply.info.command.command = command_info->command;
                command_reply.result = reply->result;
                
                request->bounce_cb(&command_reply, request);
            }
            break;
            
        default:
            break;
    }
    
    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_request_custom_command(
    globus_gsc_959_op_t                 op,
    const char *                        full_command,
    char **                             cmd_array,
    int                                 argc,
    void *                              user_arg)
{
    char *                              msg_for_log;
    int                                 type;
    globus_l_gfs_server_instance_t *    instance;
    globus_gfs_command_info_t *         command_info;
    globus_l_gfs_request_info_t *       request;
    globus_result_t                     result;
    globus_bool_t                       done = GLOBUS_FALSE;
    globus_i_gfs_cmd_ent_t *            cmd_ent;
    int                                 i;
    int                                 acc;
    GlobusGFSName(globus_l_gfs_request_custom_command);
    GlobusGFSDebugEnter();

    msg_for_log = strdup(full_command);

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    command_info = (globus_gfs_command_info_t *) 
        globus_calloc(1, sizeof(globus_gfs_command_info_t));

    result = globus_l_gfs_request_info_init(
        &request, instance, op, command_info);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }

    if(strcmp(cmd_array[0], "SITE") == 0)
    {
        char                            key[1024];
        
        snprintf(key, sizeof(key), "%s %s", cmd_array[0], cmd_array[1]);
        cmd_ent = globus_hashtable_lookup(
            &instance->custom_cmd_table, key);
    }
    else
    {    
        cmd_ent = globus_hashtable_lookup(
            &instance->custom_cmd_table, cmd_array[0]);
    }
    
    if(cmd_ent)
    {
        command_info->command = cmd_ent->cmd_id;
        if(cmd_ent->has_pathname)
        {   
            switch(cmd_ent->access_type)
            {
                case GFS_ACL_ACTION_READ:
                    acc = GFS_L_READ;
                    break;
                case GFS_ACL_ACTION_LOOKUP:
                    acc = GFS_L_LIST;
                    break;
                case GFS_ACL_ACTION_WRITE:
                case GFS_ACL_ACTION_DELETE:
                case GFS_ACL_ACTION_CREATE:
                default:
                    acc = GFS_L_WRITE;
                    break;
            }                
            result = globus_l_gfs_get_full_path(
                instance, 
                cmd_array[argc - 1], 
                &command_info->pathname, 
                acc);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
        }
        else
        {
            command_info->pathname = globus_libc_strdup(cmd_array[argc - 1]);
        }

        command_info->op_info = 
            globus_calloc(1, sizeof(globus_i_gfs_op_info_t));

        command_info->op_info->cmd_ent = cmd_ent;
        command_info->op_info->argc = argc;
        command_info->op_info->argv = globus_calloc(argc, sizeof(char *));
        for(i = 0; i < argc; i++)
        {
            command_info->op_info->argv[i] = 
                globus_libc_strdup(cmd_array[i]);
        }

        type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
    }
    else
    {
        goto err;
    }

    if(!done)
    {
        globus_i_gfs_data_request_command(
            NULL,
            instance->session_arg,
            0,
            command_info,
            globus_l_gfs_data_command_cb,
            request);
    }
    globus_l_gfs_control_log(instance->server_handle, msg_for_log,
        type, instance);
    free(msg_for_log);
    
    GlobusGFSDebugExit();
    return;

err:   
error_init:

    globus_l_gfs_control_log(instance->server_handle, msg_for_log,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_ERROR, instance);
    free(msg_for_log);
    
    if(result != GLOBUS_SUCCESS)
    {
        char *                          ftp_str;
        char *                          tmp_str;
        int                             ftp_code;
        
        /* pull response code from error */
        if((ftp_code = globus_gfs_error_get_ftp_response_code(
            globus_error_peek(result))) == 0)
        {
            ftp_code = 500;
        }
        tmp_str = globus_error_print_friendly(globus_error_peek(result));
        ftp_str = globus_gsc_string_to_959(ftp_code, tmp_str, NULL);
        globus_gsc_959_finished_command(op, ftp_str);
        globus_free(tmp_str);
        globus_free(ftp_str);
    }
    else
    {
        globus_gsc_959_finished_command(op,
            "501 Invalid command or arguments.\r\n");
    }

    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_request_command(
    globus_gsc_959_op_t                 op,
    const char *                        full_command,
    char **                             cmd_array,
    int                                 argc,
    void *                              user_arg)
{
    char *                              msg_for_log;
    int                                 type;
    globus_l_gfs_server_instance_t *    instance;
    globus_gfs_command_info_t *         command_info;
    globus_l_gfs_request_info_t *       request;
    globus_result_t                     result;
    int                                 rc;
    globus_bool_t                       done = GLOBUS_FALSE;
    GlobusGFSName(globus_l_gfs_request_command);
    GlobusGFSDebugEnter();

    msg_for_log = strdup(full_command);

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    command_info = (globus_gfs_command_info_t *) 
        globus_calloc(1, sizeof(globus_gfs_command_info_t));

    result = globus_l_gfs_request_info_init(
        &request, instance, op, command_info);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }

    if(strcmp(cmd_array[0], "MKD") == 0)
    {
        command_info->command = GLOBUS_GFS_CMD_MKD;
        result = globus_l_gfs_get_full_path(
            instance, cmd_array[1], &command_info->pathname, GFS_L_WRITE);
        if(command_info->pathname == NULL)
        {
            goto err;
        }
        type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
    }
    else if(strcmp(cmd_array[0], "RMD") == 0)
    {
        command_info->command = GLOBUS_GFS_CMD_RMD;
        result = globus_l_gfs_get_full_path(
            instance, cmd_array[1], &command_info->pathname, GFS_L_WRITE);
        if(command_info->pathname == NULL)
        {
            goto err;
        }
        type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
    }
    else if(strcmp(cmd_array[0], "DELE") == 0)
    {
        command_info->command = GLOBUS_GFS_CMD_DELE;
        result = globus_l_gfs_get_full_path(
            instance, cmd_array[1], &command_info->pathname, GFS_L_WRITE);
        if(command_info->pathname == NULL)
        {
            goto err;
        }
        type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
    }
    else if(strcmp(cmd_array[0], "RNFR") == 0)
    {
        globus_gfs_stat_info_t *            stat_info;
    
        command_info->command = GLOBUS_GFS_CMD_RNFR;
        result = globus_l_gfs_get_full_path(
            instance, cmd_array[1], &command_info->pathname, GFS_L_WRITE);
        if(command_info->pathname == NULL)
        {
            goto err;
        }
        
        stat_info = (globus_gfs_stat_info_t *) 
            globus_calloc(1, sizeof(globus_gfs_stat_info_t));

        stat_info->file_only = GLOBUS_TRUE;
        stat_info->pathname = globus_libc_strdup(command_info->pathname);
        request->bounce_info = stat_info;
        request->bounce_type = GLOBUS_GFS_OP_COMMAND;
        request->bounce_cb = globus_l_gfs_data_command_cb;
        globus_i_gfs_data_request_stat(
            NULL,
            instance->session_arg,
            0,
            stat_info,
            globus_l_gfs_data_internal_stat_cb,
            request);
        done = GLOBUS_TRUE;
        type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
    }
    else if(strcmp(cmd_array[0], "RNTO") == 0)
    {
        command_info->command = GLOBUS_GFS_CMD_RNTO;
        result = globus_l_gfs_get_full_path(
            instance, cmd_array[1], &command_info->pathname, GFS_L_WRITE);
        if(command_info->pathname == NULL)
        {
            goto err;
        }
        if(instance->rnfr_pathname == GLOBUS_NULL)
        {
            goto err;
        }
        command_info->from_pathname = instance->rnfr_pathname;
        instance->rnfr_pathname = GLOBUS_NULL;
        type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
    }
    else if(strcmp(cmd_array[0], "DCSC") == 0)
    {
        command_info->command = GLOBUS_GFS_CMD_DCSC;
        command_info->cksm_alg = globus_libc_strdup(cmd_array[1]);
        command_info->pathname = globus_libc_strdup(cmd_array[2]);
        if(command_info->pathname == NULL && 
            strcasecmp(command_info->cksm_alg, "d") != 0)
        {
            goto err;
        }
        type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SECURITY;
    }
    else if(strcmp(cmd_array[0], "CKSM") == 0)
    {
        char *                          freq;
        int                             consumed;
        
        command_info->command = GLOBUS_GFS_CMD_CKSM;
        result = globus_l_gfs_get_full_path(
            instance, cmd_array[4], &command_info->pathname, GFS_L_READ);
        if(command_info->pathname == NULL)
        {
            goto err;
        }
        command_info->cksm_alg = globus_libc_strdup(cmd_array[1]);

        rc = globus_libc_scan_off_t(
            cmd_array[2],
            &command_info->cksm_offset,
            &consumed);
        if(rc < 1 || *(cmd_array[2] + consumed) != '\0' || 
            command_info->cksm_offset < 0)
        {
            result = GlobusGFSErrorGeneric("Invalid offset.");
            goto err;
        }

        rc = globus_libc_scan_off_t(
            cmd_array[3],
            &command_info->cksm_length,
            &consumed);
        if(rc < 1 || *(cmd_array[3] + consumed) != '\0' || 
            command_info->cksm_length < -1)
        {
            result = GlobusGFSErrorGeneric("Invalid length.");
            goto err;
        }

        type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
        
        if((freq = getenv("GFS_CKSM_MARKERS")) != NULL)
        {
            command_info->chmod_mode = strtol(freq, NULL, 10);
        }
    }
    else if(strcmp(cmd_array[0], "SCKS") == 0)
    {
        if(instance->scks_alg)
        {
            globus_free(instance->scks_alg);
        }
        if(instance->scks_val)
        {
            globus_free(instance->scks_val);
        }
        instance->scks_alg = globus_libc_strdup(cmd_array[1]);
        instance->scks_val = globus_libc_strdup(cmd_array[2]);

        globus_gsc_959_finished_command(op, "200 OK.\r\n");
        globus_l_gfs_request_info_destroy(request);
        globus_free(command_info);
        done = GLOBUS_TRUE;

        type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
    }
    else if(strcmp(cmd_array[0], "MFMT") == 0)
    {
        command_info->command = GLOBUS_GFS_CMD_SITE_UTIME;
        result = globus_l_gfs_get_full_path(
            instance, cmd_array[2], &command_info->pathname, GFS_L_WRITE);
        if(command_info->pathname == NULL)
        {
            goto err;
        }
        if (strlen(cmd_array[1]) < 14)
        {
            goto err;   
        }
        {
            char* tz;
            struct tm modtime;
            memset(&modtime, 0, sizeof(modtime));
            if (sscanf(cmd_array[1], "%4d%2d%2d%2d%2d%2d", 
                        &modtime.tm_year, &modtime.tm_mon, &modtime.tm_mday,
                        &modtime.tm_hour, &modtime.tm_min, &modtime.tm_sec) != 6)
            {
                goto err;
            }
            modtime.tm_year -= 1900;
            modtime.tm_mon  -= 1;
            /* This block converts the user-specified UTC time to a Unix time
             * value.  We have to do contortions here as there is no standard
             * inverse of the 'gmtime' function. */
            tz = getenv("TZ");
            globus_libc_setenv("TZ", "UTC", 1);
            tzset();
            command_info->utime_time = mktime(&modtime);
            if (tz)
                globus_libc_setenv("TZ", tz, 1);
            else
                globus_libc_unsetenv("TZ");
            tzset();
            if (command_info->utime_time < 0)
            {
                goto err;
            }                        
        }
        type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
    }
    else if(strcmp(cmd_array[0], "SITE") == 0)
    {
        if(strcmp(cmd_array[1], "CHMOD") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_CHMOD;
            result = globus_l_gfs_get_full_path(
                instance, cmd_array[3], &command_info->pathname, GFS_L_WRITE);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            command_info->chmod_mode = strtol(cmd_array[2], NULL, 8);
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }
        else if(strcmp(cmd_array[1], "DSI") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_DSI;
            command_info->pathname = strdup(cmd_array[2]);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }
        else if(strcmp(cmd_array[1], "AUTHZ_ASSERT") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_AUTHZ_ASSERT;
            command_info->authz_assert = strdup(cmd_array[2]);
            if(command_info->authz_assert == NULL)
            {
                goto err;
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }
        else if(strcmp(cmd_array[1], "RDEL") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_RDEL;
            result = globus_l_gfs_get_full_path(
                instance, cmd_array[2], &command_info->pathname, GFS_L_WRITE);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }
        else if(strcmp(cmd_array[1], "VERSION") == 0)
        {
            char                            version_string[1024];
    
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
            snprintf(version_string, sizeof(version_string),
                "200 %s\r\n", globus_i_gfs_config_string("version_string"));
            globus_gsc_959_finished_command(op, version_string);
            globus_l_gfs_request_info_destroy(request);
            globus_free(command_info);
            done = GLOBUS_TRUE;
        }
        else if(strcmp(cmd_array[1], "SETNETSTACK") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_SETNETSTACK;
            command_info->pathname = strdup(cmd_array[2]);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }
        else if(strcmp(cmd_array[1], "SETDISKSTACK") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_SETDISKSTACK;
            command_info->pathname = strdup(cmd_array[2]);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }
        else if(strcmp(cmd_array[1], "CLIENTINFO") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_CLIENTINFO;
            command_info->pathname = strdup(cmd_array[2]);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }
        else if(strcmp(cmd_array[1], "CHGRP") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_CHGRP;
            result = globus_l_gfs_get_full_path(
                instance, cmd_array[3], &command_info->pathname, GFS_L_WRITE);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            command_info->chgrp_group = globus_libc_strdup(cmd_array[2]);
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }
        else if(strcmp(cmd_array[1], "UTIME") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_UTIME;
            result = globus_l_gfs_get_full_path(
                instance, cmd_array[3], &command_info->pathname, GFS_L_WRITE);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            if (strlen(cmd_array[2]) < 14)
            {
                goto err;   
            }
            {
                char* tz;
                struct tm modtime;
                memset(&modtime, 0, sizeof(modtime));
                if (sscanf(cmd_array[2], "%4d%2d%2d%2d%2d%2d", 
                            &modtime.tm_year, &modtime.tm_mon, &modtime.tm_mday,
                            &modtime.tm_hour, &modtime.tm_min, &modtime.tm_sec) != 6)
                {
                    goto err;
                }
                modtime.tm_year -= 1900;
                modtime.tm_mon  -= 1;
                /* This block converts the user-specified UTC time to a Unix time
                 * value.  We have to do contortions here as there is no standard
                 * inverse of the 'gmtime' function. */
                tz = getenv("TZ");
                globus_libc_setenv("TZ", "UTC", 1);
                tzset();
                command_info->utime_time = mktime(&modtime);
                if(tz)
                {
                    globus_libc_setenv("TZ", tz, 1);
                }
                else
                {
                    globus_libc_unsetenv("TZ");
                }
                tzset();
                if (command_info->utime_time < 0)
                {
                    goto err;
                }                        
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }
        else if(strcmp(cmd_array[1], "SYMLINKFROM") == 0)
        {            
            globus_gfs_stat_info_t *            stat_info;
        
            command_info->command = GLOBUS_GFS_CMD_SITE_SYMLINKFROM;
            result = globus_l_gfs_get_full_path(
                instance, cmd_array[2], &command_info->pathname, GFS_L_READ);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            
            stat_info = (globus_gfs_stat_info_t *) 
                globus_calloc(1, sizeof(globus_gfs_stat_info_t));
    
            stat_info->file_only = GLOBUS_TRUE;
            stat_info->pathname = globus_libc_strdup(command_info->pathname);
            request->bounce_info = stat_info;
            request->bounce_type = GLOBUS_GFS_OP_COMMAND;
            request->bounce_cb = globus_l_gfs_data_command_cb;
            globus_i_gfs_data_request_stat(
                NULL,
                instance->session_arg,
                0,
                stat_info,
                globus_l_gfs_data_internal_stat_cb,
                request);
            done = GLOBUS_TRUE;
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
        }
        else if(strcmp(cmd_array[1], "SYMLINKTO") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_SYMLINK;
            result = globus_l_gfs_get_full_path(
                instance, cmd_array[2], &command_info->pathname, GFS_L_WRITE);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            if(instance->slfr_pathname == NULL)
            {
                goto err;
            }
            command_info->from_pathname = instance->slfr_pathname;
            instance->slfr_pathname = GLOBUS_NULL;
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
        }
        else if(strcmp(cmd_array[1], "RESTRICT") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_RESTRICT;
            command_info->pathname = globus_libc_strdup(cmd_array[2]);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
        }
        else if(strcmp(cmd_array[1], "CHROOT") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_CHROOT;
            result = globus_l_gfs_get_full_path(
                instance, cmd_array[2], &command_info->pathname, GFS_L_LIST);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
        }
        else if(strcmp(cmd_array[1], "SHARING") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_SHARING;
            if(strcasecmp(cmd_array[2], "TESTPATH") == 0)
            {
                char *                  tmp_path;
                result = globus_l_gfs_get_full_path(
                    instance, cmd_array[3], &tmp_path, GFS_L_LIST);
                if(result != GLOBUS_SUCCESS || !tmp_path)
            {
                    goto err;
                }
                command_info->pathname = globus_common_create_string(
                    "%s %s", cmd_array[2], tmp_path);
                globus_free(tmp_path);
            }
            else if(strcasecmp(cmd_array[2], "CREATE") == 0) 
            {
                char *                  tmp_argstr;
                result = globus_l_gfs_kvstr_path(
                    instance, GFS_L_LIST, cmd_array[3], &tmp_argstr);
                if(result != GLOBUS_SUCCESS || !tmp_argstr)
                {
                    goto err;
                }
                command_info->pathname = globus_common_create_string(
                    "%s %s", cmd_array[2], tmp_argstr);
                globus_free(tmp_argstr);
            }
            else if(strcasecmp(cmd_array[2], "DELETE") == 0) 
            {
                command_info->pathname = globus_common_create_string(
                    "%s %s", cmd_array[2], cmd_array[3]);
            }
                
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
        }
        else if(strcmp(cmd_array[1], "UPAS") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_UPAS;
            command_info->pathname = globus_libc_strdup(cmd_array[2]);
    
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE;
        }
        else if(strcmp(cmd_array[1], "UPRT") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_UPRT;
            command_info->pathname = globus_libc_strdup(cmd_array[2]);
    
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE;
        }
        else if(strcmp(cmd_array[1], "HTTP") == 0)
        {
            if(strcasecmp(cmd_array[2], "UPLOAD") == 0)
            {
                command_info->command = GLOBUS_GFS_CMD_HTTP_PUT;
                result = globus_l_gfs_kvstr_path(
                    instance, GFS_L_READ, cmd_array[3], &command_info->pathname);
                if(result != GLOBUS_SUCCESS || !command_info->pathname)
                {
                    goto err;
                }
            } 
            else if(strcasecmp(cmd_array[2], "DOWNLOAD") == 0)   
            {
                command_info->command = GLOBUS_GFS_CMD_HTTP_GET;
                result = globus_l_gfs_kvstr_path(
                    instance, GFS_L_WRITE, cmd_array[3], &command_info->pathname);
                if(result != GLOBUS_SUCCESS || !command_info->pathname)
                {
                    goto err;
                }
            }
            else if(strcasecmp(cmd_array[2], "CONFIG") == 0)   
            {
                command_info->command = GLOBUS_GFS_CMD_HTTP_CONFIG;
                command_info->pathname = globus_libc_strdup(cmd_array[3]);
            }
            else
            {
                goto err;
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS;
        }
        else if(strcmp(cmd_array[1], "TRNC") == 0)
        {
            int                         consumed;
            command_info->command = GLOBUS_GFS_CMD_TRNC;
            result = globus_l_gfs_get_full_path(
                instance, cmd_array[3], &command_info->pathname, GFS_L_WRITE);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            
            rc = globus_libc_scan_off_t(
                cmd_array[2],
                &command_info->cksm_offset,
                &consumed);
            if(rc < 1 || *(cmd_array[2] + consumed) != '\0' || 
                command_info->cksm_offset < 0)
            {
                result = GlobusGFSErrorGeneric("Invalid length.");
                goto err;
            }

            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }
        else if(strcmp(cmd_array[1], "STORATTR") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_STORATTR;
            command_info->pathname = strdup(cmd_array[2]);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }
        else if(strcmp(cmd_array[1], "TASKID") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_SITE_TASKID;
            command_info->pathname = strdup(cmd_array[2]);
            if(command_info->pathname == NULL)
            {
                goto err;
            }
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }
        else if(strcmp(cmd_array[1], "WHOAMI") == 0)
        {
            command_info->command = GLOBUS_GFS_CMD_WHOAMI;
            type = GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE;
        }

        else
        {
            goto err;
        }
    }
    else
    {
        goto err;
    }

    if(!done)
    {
        globus_i_gfs_data_request_command(
            NULL,
            instance->session_arg,
            0,
            command_info,
            globus_l_gfs_data_command_cb,
            request);
    }
    globus_l_gfs_control_log(instance->server_handle, msg_for_log,
        type, instance);
    free(msg_for_log);
    
    GlobusGFSDebugExit();
    return;

err:
    globus_l_gfs_request_info_destroy(request);
    globus_free(command_info);
error_init:

    globus_l_gfs_control_log(instance->server_handle, msg_for_log,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_ERROR, instance);
    free(msg_for_log);
    
    if(result != GLOBUS_SUCCESS)
    {
        char *                          ftp_str;
        char *                          tmp_str;
        int                             ftp_code;
        
        /* pull response code from error */
        if((ftp_code = globus_gfs_error_get_ftp_response_code(
            globus_error_peek(result))) == 0)
        {
            ftp_code = 500;
        }
        tmp_str = globus_error_print_friendly(globus_error_peek(result));        
        ftp_str = globus_gsc_string_to_959(ftp_code, tmp_str, NULL);
        globus_gsc_959_finished_command(op, ftp_str);
        globus_free(tmp_str);
        globus_free(ftp_str);
    }
    else
    {
        globus_gsc_959_finished_command(op,
            "501 Invalid command arguments.\r\n");
    }

    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_request_transfer_event(
    globus_gridftp_server_control_op_t  op,
    int                                 event_type,
    void *                              user_arg)
{
    globus_l_gfs_request_info_t *       request;
    globus_gfs_event_info_t             event_info;
    GlobusGFSName(globus_l_gfs_request_transfer_event);
    GlobusGFSDebugEnter();

    request = (globus_l_gfs_request_info_t *) user_arg;

    memset(&event_info, '\0', sizeof(globus_gfs_event_info_t));
    event_info.event_arg = request->event_arg;

    switch(event_type)
    {
        case GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_PERF:
            event_info.type = GLOBUS_GFS_EVENT_BYTES_RECVD;
            break;
        case GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_RESTART:
            event_info.type = GLOBUS_GFS_EVENT_RANGES_RECVD;
            break;
        case GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_ABORT:
            event_info.type = GLOBUS_GFS_EVENT_TRANSFER_ABORT;
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_INFO,
                "Requesting abort...\n");
            break;
        case GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_TRANSFER_COMPLETE:
            event_info.type = GLOBUS_GFS_EVENT_TRANSFER_COMPLETE;
            break;
        default:
            goto error;
            break;
    }

    globus_i_gfs_data_request_transfer_event(
        NULL,
        request->instance->session_arg,
        &event_info);

    if(event_info.type == GLOBUS_GFS_EVENT_TRANSFER_COMPLETE)
    {
        globus_gfs_transfer_info_t *    info;
        info = (globus_gfs_transfer_info_t *) request->info;
        if(info)
        {
            if(info->pathname)
            {
                globus_free(info->pathname);
            }
            if(info->list_type)
            {
                globus_free(info->list_type);
            }
            if(info->module_name)
            {
                globus_free(info->module_name);
            }
            if(info->module_args)
            {
                globus_free(info->module_args);
            }
            if(info->expected_checksum_alg)
            {
                globus_free(info->expected_checksum_alg);
            }
            if(info->expected_checksum)
            {
                globus_free(info->expected_checksum);
            }
            if(info->op_info)
            {
                globus_free(info->op_info);
            }
            globus_free(info);
        }
        globus_l_gfs_request_info_destroy(request);
    }

    GlobusGFSDebugExit();
    return;

error:
    GlobusGFSDebugExitWithError();
}

static
int
globus_l_gfs_get_event_mask(
    int                                 in_event_mask)
{
    int                                 out_event_mask = 0;
    GlobusGFSName(globus_l_gfs_get_event_mask);
    GlobusGFSDebugEnter();

    if(in_event_mask & GLOBUS_GFS_EVENT_BYTES_RECVD)
    {
        out_event_mask |= GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_PERF;
    }
    if(in_event_mask & GLOBUS_GFS_EVENT_RANGES_RECVD)
    {
        out_event_mask |= GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_RESTART;
    }
    if(in_event_mask & GLOBUS_GFS_EVENT_TRANSFER_ABORT)
    {
        out_event_mask |= GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_ABORT;
    }
    if(in_event_mask & GLOBUS_GFS_EVENT_TRANSFER_COMPLETE)
    {
        out_event_mask |= 
            GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_TRANSFER_COMPLETE;
    }

    GlobusGFSDebugExit();
    return out_event_mask;
}

static
void
globus_l_gfs_data_event_cb(
    globus_gfs_data_event_reply_t *     reply,
    void *                              user_arg)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gridftp_server_control_op_t  op;
    globus_l_gfs_request_info_t *       request;
    int                                 event_mask;
    GlobusGFSName(globus_l_gfs_data_event_cb);
    GlobusGFSDebugEnter();

    request = (globus_l_gfs_request_info_t *) user_arg;
    op = request->control_op;
    switch(reply->type)
    {
        case GLOBUS_GFS_EVENT_TRANSFER_BEGIN:
            request->event_arg = reply->event_arg;
            
            request->transfer_events = GLOBUS_TRUE;
            event_mask = globus_l_gfs_get_event_mask(reply->event_mask);
            result = globus_gridftp_server_control_events_enable(
                op,
                event_mask,
                globus_l_gfs_request_transfer_event,
                request);
            if(result != GLOBUS_SUCCESS)
            {
                request->transfer_events = GLOBUS_FALSE;
                /* TODO: can we ignore this */
            }
            break;
        
        case GLOBUS_GFS_EVENT_TRANSFER_CONNECTED:
            globus_gridftp_server_control_begin_transfer(op);
            break;
        
        case GLOBUS_GFS_EVENT_DISCONNECTED:
            globus_gridftp_server_control_disconnected(
                request->instance->server_handle, 
                reply->data_arg);
            break;
        
        case GLOBUS_GFS_EVENT_BYTES_RECVD:
            globus_gridftp_server_control_event_send_perf(
                op, reply->node_ndx, reply->recvd_bytes);
            break;
        
        case GLOBUS_GFS_EVENT_RANGES_RECVD:
            globus_gridftp_server_control_event_send_restart(
               op, reply->recvd_ranges);
            break;
        
        default:
            globus_assert(0 && "Unexpected event type");
            break;
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_transfer_cb(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg)
{
    globus_gridftp_server_control_op_t  op;
    globus_l_gfs_request_info_t *       request;
    char *                              tmp_str;
    globus_bool_t                       destroy_req;
    GlobusGFSName(globus_l_gfs_data_transfer_cb);
    GlobusGFSDebugEnter();

    request = (globus_l_gfs_request_info_t *) user_arg;
    op = request->control_op;
    

    destroy_req = !request->transfer_events;
    if(reply->result != GLOBUS_SUCCESS)
    {
        char *                          msg;
        globus_result_t                 result;
        int                             ftp_code;
        
        /* use code and message from reply if set */
        if(reply->code && reply->msg)
        {
            ftp_code = reply->code;
            tmp_str = strdup(reply->msg);
        }
        else
        {
            ftp_code = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED;
            tmp_str = globus_error_print_friendly(
                globus_error_peek(reply->result));
        }

        result = globus_i_gfs_data_virtualize_path(
            request->instance->session_arg, tmp_str, &msg);
        if(result == GLOBUS_SUCCESS && msg != NULL)
        {
            globus_free(tmp_str);
            tmp_str = msg;
        }

        globus_gridftp_server_control_finished_transfer(
            op, ftp_code, tmp_str);
        globus_free(tmp_str);
    }
    else
    {
        globus_gridftp_server_control_finished_transfer(
            op,
            GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS,
            reply->msg);
    }
    if(destroy_req)
    {
        globus_gfs_transfer_info_t *    info;
        info = (globus_gfs_transfer_info_t *) request->info;
        if(info)
        {
            if(info->pathname)
            {
                globus_free(info->pathname);
            }
            if(info->list_type)
            {
                globus_free(info->list_type);
            }
            if(info->module_name)
            {
                globus_free(info->module_name);
            }
            if(info->module_args)
            {
                globus_free(info->module_args);
            }
            if(info->expected_checksum_alg)
            {
                globus_free(info->expected_checksum_alg);
            }
            if(info->expected_checksum)
            {
                globus_free(info->expected_checksum);
            }
            if(info->op_info)
            {
                globus_free(info->op_info);
            }
            globus_free(info);

        }
        globus_l_gfs_request_info_destroy(request);
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_request_send(
    globus_gridftp_server_control_op_t  op,
    void *                              data_handle,
    const char *                        path,
    const char *                        mod_name,
    const char *                        mod_parms,
    globus_range_list_t                 range_list,
    void *                              user_arg)
{
    char *                              tmp_str;
    globus_l_gfs_server_instance_t *    instance;
    globus_gfs_transfer_info_t *        send_info;
    int                                 args;
    globus_l_gfs_request_info_t *       request;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_request_send);
    GlobusGFSDebugEnter();

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    send_info = (globus_gfs_transfer_info_t *)
        globus_calloc(1, sizeof(globus_gfs_transfer_info_t));

    result = globus_l_gfs_request_info_init(
        &request, instance, op, send_info);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }

    if(mod_name && strcmp("P", mod_name) == 0)
    {
        args = sscanf(
            mod_parms,
            "%"GLOBUS_OFF_T_FORMAT" %"GLOBUS_OFF_T_FORMAT,
            &send_info->partial_offset,
            &send_info->partial_length);

        globus_assert(args == 2);
    }
    else
    {
        send_info->partial_offset = 0;
        send_info->partial_length = -1;
        if(mod_name != NULL)
        {
            send_info->module_name = globus_libc_strdup(mod_name);
        }
        if(mod_parms != NULL)
        {
            send_info->module_args = globus_libc_strdup(mod_parms);
        }
    }

    result = globus_l_gfs_get_full_path(
        instance, path, &send_info->pathname, GFS_L_READ);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }
    send_info->range_list = range_list;
    send_info->stripe_count = 1;
    send_info->node_count = 1;
    send_info->data_arg = data_handle;

    globus_i_gfs_data_request_send(
        NULL,
        instance->session_arg,
        0,
        send_info,
        globus_l_gfs_data_transfer_cb,
        globus_l_gfs_data_event_cb,
        request);

    GlobusGFSDebugExit();
    return;
    
error_init:
    {
        int                             ftp_code;
        
        /* pull response code from error */
        if((ftp_code = globus_gfs_error_get_ftp_response_code(
            globus_error_peek(result))) == 0)
        {
            ftp_code = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED;
        }
        tmp_str = globus_error_print_friendly(globus_error_peek(result));
        globus_gridftp_server_control_finished_transfer(
            op,
            ftp_code,
            tmp_str);
        globus_free(tmp_str);
    }
    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_request_recv(
    globus_gridftp_server_control_op_t  op,
    void *                              data_handle,
    const char *                        path,
    const char *                        mod_name,
    const char *                        mod_parms,
    globus_range_list_t                 range_list,
    void *                              user_arg)
{
    char *                              tmp_str;
    globus_l_gfs_server_instance_t *    instance;
    int                                 args;
    globus_gfs_transfer_info_t *        recv_info;
    globus_l_gfs_request_info_t *       request;
    globus_result_t                     result;
    globus_off_t                        length = GLOBUS_RANGE_LIST_MAX;
    globus_off_t                        offset = 0;
    GlobusGFSName(globus_l_gfs_request_recv);
    GlobusGFSDebugEnter();

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    recv_info = (globus_gfs_transfer_info_t *)
        globus_calloc(1, sizeof(globus_gfs_transfer_info_t));

    result = globus_l_gfs_request_info_init(
        &request, instance, op, recv_info);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }

    result = globus_gridftp_server_control_get_allocated(
        op, &recv_info->alloc_size);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }
    
    /* if restart range is anything but 0-MAX then we don't trunc the file */
    if(globus_range_list_size(range_list))
    {
        globus_range_list_at(range_list, 0, &offset, &length);
    }
    if(offset == 0 && length == GLOBUS_RANGE_LIST_MAX)
    {
        recv_info->truncate = GLOBUS_TRUE;
    }

    if(mod_name && strcmp("A", mod_name) == 0)
    {
        args = sscanf(
            mod_parms,
            "%"GLOBUS_OFF_T_FORMAT,
            &recv_info->partial_offset);
        recv_info->partial_length = -1;
        
        /*  ESTO A 0 /file is not the same as STOR /file
            ESTO doesn't truncate the file. 
        */
        recv_info->truncate = GLOBUS_FALSE;

        globus_assert(args == 1);
    }
    else
    {
        recv_info->partial_offset = 0;
        recv_info->partial_length = -1;
        
        if(mod_name != NULL)
        {
            recv_info->module_name = globus_libc_strdup(mod_name);
        }
        if(mod_parms != NULL)
        {
            recv_info->module_args = globus_libc_strdup(mod_parms);
        }
    if(instance->scks_val != NULL)
    {
        recv_info->expected_checksum_alg = instance->scks_alg;
        recv_info->expected_checksum = instance->scks_val;
        instance->scks_alg = NULL;
        instance->scks_val = NULL;
    }

    }
        
    result = globus_l_gfs_get_full_path(
        instance, path, &recv_info->pathname, GFS_L_WRITE);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }
    recv_info->range_list = range_list;
    recv_info->stripe_count = 1;
    recv_info->node_count = 1;
    recv_info->data_arg = data_handle;
        
    globus_i_gfs_data_request_recv(
        NULL,
        instance->session_arg,
        0,
        recv_info,
        globus_l_gfs_data_transfer_cb,
        globus_l_gfs_data_event_cb,
        request);

    GlobusGFSDebugExit();
    return;
    
error_init:
    {
        int                             ftp_code;
        
        /* pull response code from error */
        if((ftp_code = globus_gfs_error_get_ftp_response_code(
            globus_error_peek(result))) == 0)
        {
            ftp_code = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED;
        }
        tmp_str = globus_error_print_friendly(globus_error_peek(result));
        globus_gridftp_server_control_finished_transfer(
            op,
            ftp_code,
            tmp_str);
        globus_free(tmp_str);
    }
    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_request_list(
    globus_gridftp_server_control_op_t  op,
    void *                              data_handle,
    const char *                        path,
    const char *                        list_type,
    int                                 list_depth,
    int                                 traversal_options,
    void *                              user_arg)
{
    char *                              tmp_str;
    globus_l_gfs_server_instance_t *    instance;
    globus_gfs_transfer_info_t *        list_info;
    globus_l_gfs_request_info_t *       request;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_request_list);
    GlobusGFSDebugEnter();

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    list_info = (globus_gfs_transfer_info_t *)
        globus_calloc(1, sizeof(globus_gfs_transfer_info_t));

    result = globus_l_gfs_request_info_init(
        &request, instance, op, list_info);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }

    list_info->list_depth = list_depth;
    list_info->traversal_options = traversal_options;
    result = globus_l_gfs_get_full_path(
        instance, path, &list_info->pathname, GFS_L_LIST);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }
    list_info->list_type = globus_libc_strdup(list_type);
    list_info->data_arg = data_handle;
    list_info->stripe_count = 1;
    list_info->node_count = 1;

    globus_i_gfs_data_request_list(
        NULL,
        instance->session_arg,
        0,
        list_info,
        globus_l_gfs_data_transfer_cb,
        globus_l_gfs_data_event_cb,
        request);

    GlobusGFSDebugExit();
    return;
    
error_init:
    {
        int                             ftp_code;
        
        /* pull response code from error */
        if((ftp_code = globus_gfs_error_get_ftp_response_code(
            globus_error_peek(result))) == 0)
        {
            ftp_code = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED;
        }
        tmp_str = globus_error_print_friendly(globus_error_peek(result));
        globus_gridftp_server_control_finished_transfer(
            op,
            ftp_code,
            tmp_str);
        globus_free(tmp_str);
    }
    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_data_passive_data_cb(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg)
{
    globus_gridftp_server_control_op_t  op;
    globus_l_gfs_request_info_t *       request;
    globus_gfs_data_info_t *            info;
    char *                              tmp_str;
    GlobusGFSName(globus_l_gfs_data_passive_data_cb);
    GlobusGFSDebugEnter();

    request = (globus_l_gfs_request_info_t *) user_arg;
    op = request->control_op;

    if(reply->result != GLOBUS_SUCCESS)
    {
        tmp_str = globus_error_print_friendly(
            globus_error_peek(reply->result));
        globus_gridftp_server_control_finished_passive_connect(
            op,
            reply->info.data.data_arg,
            reply->info.data.bi_directional
                ? GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_BI
                : GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_SEND,
            reply->info.data.contact_strings,
            reply->info.data.cs_count,
            GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED,
            tmp_str);
        globus_free(tmp_str);
    }
    else
    {
        globus_gridftp_server_control_finished_passive_connect(
            op,
            reply->info.data.data_arg,
            reply->info.data.bi_directional
                ? GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_BI
                : GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_SEND,
            reply->info.data.contact_strings,
            reply->info.data.cs_count,
            GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS,
            GLOBUS_NULL);
    }

    info = (globus_gfs_data_info_t *) request->info;
    if(info)
    {
        if(info->interface)
        {
            globus_free(info->interface);
        }
        if(info->pathname)
        {
            globus_free(info->pathname);
        }
        globus_free(info);
    }
    globus_l_gfs_request_info_destroy(request);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_get_data_info(
    globus_gridftp_server_control_op_t  op,
    globus_gfs_data_info_t *            data_info,
    globus_gridftp_server_control_network_protocol_t net_prt)
{
    globus_result_t                     result;
    globus_size_t                       buf_size;
    GlobusGFSName(globus_l_gfs_get_data_info);
    GlobusGFSDebugEnter();

    if(net_prt == GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV6)
    {
        data_info->ipv6 = GLOBUS_TRUE;
    }
    else
    {
        data_info->ipv6 = GLOBUS_FALSE;
    }

    result = globus_gridftp_server_control_get_mode(op, &data_info->mode);
    globus_assert(result == GLOBUS_SUCCESS);

    result = globus_gridftp_server_control_get_type(op, &data_info->type);
    globus_assert(result == GLOBUS_SUCCESS);

    result = globus_gridftp_server_control_get_buffer_size(
        op, &data_info->tcp_bufsize, &buf_size);
    globus_assert(result == GLOBUS_SUCCESS);

    if(buf_size > data_info->tcp_bufsize)
    {
        data_info->tcp_bufsize = buf_size;
    }

    result = globus_gridftp_server_control_get_parallelism(
        op, &data_info->nstreams);
    globus_assert(result == GLOBUS_SUCCESS);

    result = globus_gridftp_server_control_get_data_auth(
        op,
        &data_info->subject,
        &data_info->dcau,
        &data_info->prot,
        NULL);
    globus_assert(result == GLOBUS_SUCCESS);

    result = globus_gridftp_server_control_get_layout(                                       
        op,
        (globus_gsc_layout_t *) &data_info->stripe_layout,
        &data_info->stripe_blocksize);
    globus_assert(result == GLOBUS_SUCCESS);
    
    if(data_info->stripe_blocksize == 0 || 
        globus_i_gfs_config_bool("stripe_blocksize_locked"))
    {
        data_info->stripe_blocksize = 
            globus_i_gfs_config_int("stripe_blocksize");
    }
    if(globus_i_gfs_config_int("stripe_layout_locked"))
    {
        data_info->stripe_layout = 
            globus_i_gfs_config_int("stripe_layout");
    }
    else
    {        
        switch(data_info->stripe_layout)
        {
            case GLOBUS_GSC_LAYOUT_TYPE_PARTITIONED:
                data_info->stripe_layout = GLOBUS_GFS_LAYOUT_PARTITIONED;
                break;
            case GLOBUS_GSC_LAYOUT_TYPE_BLOCKED:
                data_info->stripe_layout = GLOBUS_GFS_LAYOUT_BLOCKED;
                break;
            case GLOBUS_GSC_LAYOUT_TYPE_NONE:
            default:
                data_info->stripe_layout = 
                    globus_i_gfs_config_int("stripe_layout");
                break;
        }
    }
    
    data_info->blocksize = globus_i_gfs_config_int("blocksize");

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_request_passive_data(
    globus_gridftp_server_control_op_t  op,
    globus_gridftp_server_control_network_protocol_t net_prt,
    int                                 max,
    const char *                        pathname,
    void *                              user_arg)
{
    char *                              tmp_str = NULL;
    globus_l_gfs_server_instance_t *    instance;
    globus_gfs_data_info_t *            data_info;
    globus_l_gfs_request_info_t *       request;
    globus_result_t                     result;
    globus_xio_contact_t                parsed_contact;
    int                                 err_code;
    GlobusGFSName(globus_l_gfs_request_passive_data);
    GlobusGFSDebugEnter();

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    data_info = (globus_gfs_data_info_t *)
        globus_calloc(1, sizeof(globus_gfs_data_info_t));

    result = globus_l_gfs_request_info_init(
        &request, instance, op, data_info);
    if(result != GLOBUS_SUCCESS)
    {
        err_code = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED;
        goto error_init;
    }

    globus_l_gfs_get_data_info(op, data_info, net_prt);

    if(globus_i_gfs_config_bool("encrypt_data") && 
        (data_info->prot != 'P' || data_info->dcau == 'N'))
    {
        tmp_str = strdup("Encryption is required.");
        err_code = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_DATA_CONN_AUTH;
        goto error_postinit;
    }
    
    if(pathname)
    {
        /* delayed pasv, final perm check will happen on the stor/retr */ 
        result = globus_l_gfs_get_full_path(
            instance, pathname, &data_info->pathname, GFS_L_LIST);
        if(result != GLOBUS_SUCCESS)
        {
            err_code = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED;
            goto error_postinit;
        }        
    }
    globus_xio_contact_parse(&parsed_contact, instance->local_contact);
    data_info->max_cs = max;
    data_info->interface = globus_libc_strdup(parsed_contact.host);
    
    globus_xio_contact_destroy(&parsed_contact);

    globus_i_gfs_data_request_passive(
        NULL,
        instance->session_arg,
        0,
        data_info,
        globus_l_gfs_data_passive_data_cb,
        request);

    GlobusGFSDebugExit();
    return;
    
error_postinit:
    globus_l_gfs_request_info_destroy(request);
error_init:
    globus_free(data_info);
    if(!tmp_str)
    {
        tmp_str = globus_error_print_friendly(globus_error_peek(result));
    }
    globus_gridftp_server_control_finished_passive_connect(
        op,
        NULL,
        0,
        NULL,
        0,
        err_code,
        tmp_str);
    globus_free(tmp_str);
    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_data_active_data_cb(
    globus_gfs_data_reply_t *            reply,
    void *                              user_arg)
{
    globus_gridftp_server_control_op_t  op;
    globus_l_gfs_request_info_t *       request;
    globus_gfs_data_info_t *            info;
    char *                              tmp_str;
    GlobusGFSName(globus_l_gfs_data_active_data_cb);
    GlobusGFSDebugEnter();

    request = (globus_l_gfs_request_info_t *) user_arg;
    op = request->control_op;

    if(reply->result != GLOBUS_SUCCESS)
    {
        tmp_str = globus_error_print_friendly(
            globus_error_peek(reply->result));
        globus_gridftp_server_control_finished_active_connect(
            op,
            reply->info.data.data_arg,
            reply->info.data.bi_directional
                ? GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_BI
                : GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_SEND,
            GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED,
            tmp_str);
        globus_free(tmp_str);
    }
    else
    {
        globus_gridftp_server_control_finished_active_connect(
            op,
            reply->info.data.data_arg,
            reply->info.data.bi_directional
                ? GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_BI
                : GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_SEND,
            GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS,
            GLOBUS_NULL);
    }

    info = (globus_gfs_data_info_t *) request->info;
    if(info)
    {
        if(info->interface)
        {
            globus_free(info->interface);
        }
        if(info->pathname)
        {
            globus_free(info->pathname);
        }
        globus_free(info);
    }
    globus_l_gfs_request_info_destroy(request);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_request_active_data(
    globus_gridftp_server_control_op_t  op,
    globus_gridftp_server_control_network_protocol_t net_prt,
    const char **                       cs,
    int                                 cs_count,
    void *                              user_arg)
{
    char *                              tmp_str = NULL;
    globus_l_gfs_server_instance_t *    instance;
    globus_gfs_data_info_t *            data_info;
    globus_l_gfs_request_info_t *       request;
    globus_result_t                     result;
    globus_xio_contact_t                parsed_contact;
    int                                 err_code;
    GlobusGFSName(globus_l_gfs_request_active_data);
    GlobusGFSDebugEnter();

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    data_info = (globus_gfs_data_info_t *)
        globus_calloc(1, sizeof(globus_gfs_data_info_t));

    result = globus_l_gfs_request_info_init(
        &request, instance, op, data_info);
    if(result != GLOBUS_SUCCESS)
    {
        err_code = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED;
        goto error_init;
    }

    globus_l_gfs_get_data_info(op, data_info, net_prt);
    if(globus_i_gfs_config_bool("encrypt_data") && 
        (data_info->prot != 'P' || data_info->dcau == 'N'))
    {
        tmp_str = strdup("Encryption is required.");
        err_code = GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_DATA_CONN_AUTH;
        goto error_postinit;
    }

    globus_xio_contact_parse(&parsed_contact, instance->local_contact);

    data_info->contact_strings = cs;
    data_info->cs_count = cs_count;
    data_info->interface = globus_libc_strdup(parsed_contact.host);
    
    globus_xio_contact_destroy(&parsed_contact);
    
    globus_i_gfs_data_request_active(
        NULL,
        instance->session_arg,
        0,
        data_info,
        globus_l_gfs_data_active_data_cb,
        request);

    GlobusGFSDebugExit();
    return;
  
error_postinit:
    globus_l_gfs_request_info_destroy(request);
error_init:
    globus_free(data_info);
    if(!tmp_str)
    {
        tmp_str = globus_error_print_friendly(globus_error_peek(result));
    }
    globus_gridftp_server_control_finished_active_connect(
        op,
        NULL,
        0,
        err_code,
        tmp_str);
    globus_free(tmp_str);
    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_request_data_destroy(
    void *                              user_data_arg,
    void *                              user_arg)
{
    globus_l_gfs_server_instance_t *    instance;
    GlobusGFSName(globus_l_gfs_request_data_destroy);
    GlobusGFSDebugEnter();

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    globus_i_gfs_data_request_handle_destroy(
        NULL, instance->session_arg, user_data_arg);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_control_log(
    globus_gridftp_server_control_t     server_handle,
    const char *                        message,
    int                                 type,
    void *                              user_arg)
{
    globus_l_gfs_server_instance_t *    instance;
    char *                              msg;
    GlobusGFSName(globus_l_gfs_control_log);
    GlobusGFSDebugEnter();

    instance = (globus_l_gfs_server_instance_t *) user_arg;

    if(instance == GLOBUS_NULL)
    {
        goto error;
    }

    msg = globus_libc_strdup(message);
    globus_i_gfs_log_tr(msg, '\"', '\'');
    globus_i_gfs_log_tr(msg, '\r', ' ');
    
    switch(type)
    {
      case GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_REPLY:
        globus_gfs_log_message(GLOBUS_GFS_LOG_DUMP, "%s: [SERVER]: %s",
            instance->remote_contact, message);
        globus_gfs_log_event(
            GLOBUS_GFS_LOG_DUMP,
            GLOBUS_GFS_LOG_EVENT_MESSAGE,
            "session",
            GLOBUS_SUCCESS,
            "sender=server msg=\"%s\"",
            msg);
        break;

      case GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_ERROR:
        globus_gfs_log_message(GLOBUS_GFS_LOG_WARN, "%s: [CLIENT ERROR]: %s",
            instance->remote_contact, message);
        globus_gfs_log_event(
            GLOBUS_GFS_LOG_WARN,
            GLOBUS_GFS_LOG_EVENT_MESSAGE,
            "session",
            GLOBUS_SUCCESS,
            "sender=client msg=\"%s\"",
            msg);
        break;

      default:
        globus_gfs_log_message(GLOBUS_GFS_LOG_DUMP, "%s: [CLIENT]: %s",
            instance->remote_contact, message);
        globus_gfs_log_event(
            GLOBUS_GFS_LOG_DUMP,
            GLOBUS_GFS_LOG_EVENT_MESSAGE,
            "session",
            GLOBUS_SUCCESS,
            "sender=client msg=\"%s\"",
            msg);
        break;
    }
    
    globus_free(msg);

    GlobusGFSDebugExit();
    return;

error:
    GlobusGFSDebugExitWithError();
}


static
globus_result_t
globus_l_gfs_add_commands(
    globus_l_gfs_server_instance_t *    instance,
    globus_gridftp_server_control_t     control_handle)
{
    globus_result_t                     result;
    char *                              feat_str = NULL;
    char *                              dsi_ver = NULL;
    GlobusGFSName(globus_l_gfs_add_commands);
    GlobusGFSDebugEnter();

    result = globus_gsc_959_command_add(
        control_handle,
        "MKD",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "MKD <sp> pathname",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "RMD",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "RMD <sp> pathname",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "DELE",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "DELE <sp> pathname",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE AUTHZ_ASSERT",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE AUTHZ_ASSERT <sp> pathname",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gridftp_server_control_add_feature(
                                        control_handle, "AUTHZ_ASSERT");
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_gsc_959_command_add(
        control_handle,
        "SITE WHOAMI",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "SITE WHOAMI",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_gridftp_server_control_add_feature(control_handle, "WHOAMI");
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_gsc_959_command_add(
        control_handle,
        "SITE RDEL",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE RDEL <sp> pathname",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE CHMOD",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        4,
        4,
        "SITE CHMOD <sp> mode <sp> pathname",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE CHGRP",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        4,
        4,
        "SITE CHGRP <sp> group <sp> pathname",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "MFMT",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "MFMT <sp> YYYYMMDDHHMMSS <sp> pathname",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gridftp_server_control_add_feature(control_handle, "MFMT");
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE UTIME",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        4,
        4,
        "SITE UTIME <sp> YYYYMMDDHHMMSS <sp> pathname",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE SYMLINKFROM",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE SYMLINKFROM <sp> reference-path",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE SYMLINKTO",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE SYMLINKTO <sp> link-path",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "CKSM",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        5,
        5,
        "CKSM <sp> algorithm <sp> offset <sp> length <sp> pathname",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SCKS",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SCKS <sp> algorithm <sp> checksum",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "RNFR",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "RNFR <sp> pathname",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "RNTO",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "RNTO <sp> pathname",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "DCSC",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        3,
        "DCSC <sp> credential type [ <sp> encoded credential ]",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gridftp_server_control_add_feature(
        control_handle, "DCSC P,D");

    result = globus_gridftp_server_control_add_feature(
        control_handle, "HTTP");

    result = globus_gsc_959_command_add(
        control_handle,
        "SITE HTTP",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        4,
        4,
        "SITE HTTP <sp> operation <sp> operation-parameters",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_gsc_959_command_add(
        control_handle,
        "SITE DSI",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE DSI <sp> dsi name",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE VERSION",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "SITE VERSION",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE SETNETSTACK",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE SETNETSTACK <sp> comma separated list of xio drivers for the data channel",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE SETDISKSTACK",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE SETDISKSTACK <sp> comma separated list of xio drivers for the disk channel",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE CLIENTINFO",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE CLIENTINFO <sp> appname=\"<name of app>\";appver=\"<version string>\";scheme=\"<ftp,gsiftp,sshftp>\";anyother=\"<interesting client info>\";",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE RESTRICT",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE RESTRICT <sp> RP string",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE CHROOT",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE CHROOT <sp> new root path",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE SHARING",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        4,
        4,
        "SITE SHARING <sp> command",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_gsc_959_command_add(
        control_handle,
        "SITE UPAS",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE UPAS <0|1> [<sp> stunserver:stunport]",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gsc_959_command_add(
        control_handle,
        "SITE UPRT",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE UPRT <sp> <SITE UPAS response>",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    
    if(globus_i_gfs_config_bool("allow_udt"))
    {
        result = globus_gridftp_server_control_add_feature(
            control_handle, "UPAS");
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }

    result = globus_gsc_959_command_add(
        control_handle,
        "SITE TRNC",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        4,
        4,
        "SITE TRNC <sp> length <sp> path",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_gsc_959_command_add(
        control_handle,
        "SITE STORATTR",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE STORATTR <sp> attributes",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    result = globus_gridftp_server_control_add_feature(
        control_handle, "STORATTR");
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_gsc_959_command_add(
        control_handle,
        "SITE TASKID",
        globus_l_gfs_request_command,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE TASKID <sp> taskid",
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    dsi_ver = globus_i_gfs_data_dsi_version();
    if(dsi_ver)
    {
        feat_str = globus_common_create_string("DSI %s", dsi_ver);
        
        result = globus_gridftp_server_control_add_feature(
            control_handle, feat_str);
        globus_free(feat_str);
        globus_free(dsi_ver);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusGFSDebugExitWithError();
    return result;
}

void
globus_i_gfs_control_end_421(
    const char *                        msg)
{
    int                                 i;
    int                                 kill_count;
    globus_list_t *                     list;
    globus_l_gfs_server_instance_t *    instance;
    GlobusGFSName(globus_i_gfs_control_end_421);
    GlobusGFSDebugEnter();

    globus_mutex_lock(&globus_l_gfs_control_mutex);
    {
        globus_l_gfs_control_should_be_gone = GLOBUS_TRUE;
        kill_count = globus_list_size(globus_l_gfs_server_handle_list);

        for(i = 0, list = globus_l_gfs_server_handle_list;
            i < kill_count && !globus_list_empty(list);
            i++, list = globus_list_rest(list))
        {
            instance = (globus_l_gfs_server_instance_t *)
                globus_list_first(list);

            globus_gridftp_server_control_421_end(
                instance->server_handle,
                (char *) msg);
        }
    }
    globus_mutex_unlock(&globus_l_gfs_control_mutex);

    GlobusGFSDebugExit();
}



static
void
globus_l_gfs_control_watchdog_exit(
    void *                              arg)
{
    globus_gfs_log_message(
        GLOBUS_GFS_LOG_ERR, "Forcefully terminating process. No exit after control stop.\n");

    exit(1);
}


static
void
globus_l_gfs_control_watchdog_check(
    void *                              arg)
{
    globus_bool_t                       can_kill = GLOBUS_FALSE;
    char *                              str = NULL;
    globus_result_t                     res;

    globus_mutex_lock(&globus_l_gfs_control_mutex);
    {
        if(globus_l_gfs_control_should_be_gone)
        {
            can_kill = GLOBUS_TRUE;
        }
        else
        {
            res = globus_gridftp_server_control_get_cwd(
                (globus_gridftp_server_control_t) arg, &str);
            if(res == GLOBUS_SUCCESS && str != NULL)
            {
                if(strcmp(str, "##safetoexitnow##") == 0)
                {
                    can_kill = GLOBUS_TRUE;
                }
                globus_free(str);
            }
        }
    }
    globus_mutex_unlock(&globus_l_gfs_control_mutex);
    
    if(can_kill)
    {
        globus_reltime_t                    timer;
        
        GlobusTimeReltimeSet(timer, 60, 0);
        globus_callback_register_oneshot(
            NULL,
            &timer,
            globus_l_gfs_control_watchdog_exit,
            NULL);
    }

}


globus_result_t
globus_i_gfs_control_start(
    globus_xio_handle_t                 handle,
    globus_xio_system_socket_t          system_handle,
    const char *                        remote_contact,
    const char *                        local_contact,
    globus_i_gfs_server_close_cb_t      close_func,
    void *                              close_arg)
{
    char *                              value;
    globus_result_t                     result;
    globus_gridftp_server_control_attr_t attr;
    globus_l_gfs_server_instance_t *    instance;
    int                                 idle_timeout;
    int                                 preauth_timeout;
    char *                              banner;
    char *                              login_msg;
    globus_list_t *                     module_list;
    globus_list_t *                     list;
    char *                              alias;
    char *                              module;
    GlobusGFSName(globus_i_gfs_control_start);
    GlobusGFSDebugEnter();

    instance = (globus_l_gfs_server_instance_t *)
        globus_calloc(1, sizeof(globus_l_gfs_server_instance_t));
    if(!instance)
    {
        result = GlobusGFSErrorMemory("instance");
        goto error_malloc;
    }

    instance->close_func = close_func;
    instance->close_arg = close_arg;
    instance->xio_handle = handle;
    instance->rnfr_pathname = NULL;
    instance->slfr_pathname = NULL;
    instance->scks_alg = NULL;
    instance->scks_val = NULL;
    instance->remote_contact = globus_libc_strdup(remote_contact);
    if(!instance->remote_contact)
    {
        result = GlobusGFSErrorMemory("remote_contact");
        goto error_strdup;
    }
    instance->local_contact = globus_libc_strdup(local_contact);
    if(!instance->local_contact)
    {
        result = GlobusGFSErrorMemory("local_contact");
        goto error_strdup;
    }

    result = globus_gridftp_server_control_attr_init(&attr);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr;
    }

    result = globus_gridftp_server_control_attr_set_security(
        attr,
        GLOBUS_GRIDFTP_SERVER_LIBRARY_GSSAPI |
         ((globus_i_gfs_config_bool("allow_anonymous") ||
            globus_i_gfs_config_string("pw_file")!=NULL) ?
         GLOBUS_GRIDFTP_SERVER_LIBRARY_NONE : 0));
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_setup;
    }

    result = globus_gridftp_server_control_attr_set_epsv_ip(
        attr, globus_i_gfs_config_bool("epsv_ip"));
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_setup;
    }

    idle_timeout = globus_i_gfs_config_int("control_idle_timeout");
    preauth_timeout = globus_i_gfs_config_int("control_preauth_timeout");
    
    result = globus_gridftp_server_control_attr_set_idle_time(
        attr, idle_timeout, preauth_timeout);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_setup;
    }

    banner = globus_i_gfs_config_string("banner");
    if(banner)
    {
        result = globus_gridftp_server_control_attr_set_banner(
            attr, banner);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_attr_setup;
        }
    }

    login_msg = globus_i_gfs_config_string("login_msg");
    if(login_msg)
    {
        result = globus_gridftp_server_control_attr_set_message(
            attr, login_msg);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_attr_setup;
        }
    }

    result = globus_gridftp_server_control_attr_set_auth(
        attr, globus_l_gfs_request_auth, instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_setup;
    }

    result = globus_gridftp_server_control_attr_set_resource(
        attr, globus_l_gfs_request_stat, instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_setup;
    }

    result = globus_gridftp_server_control_attr_add_recv(
        attr, GLOBUS_NULL, globus_l_gfs_request_recv, instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_setup;
    }

    result = globus_gridftp_server_control_attr_add_recv(
        attr, "A", globus_l_gfs_request_recv, instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_setup;
    }

    result = globus_gridftp_server_control_attr_add_send(
        attr, GLOBUS_NULL, globus_l_gfs_request_send, instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_setup;
    }

    result = globus_gridftp_server_control_attr_add_send(
        attr, "P", globus_l_gfs_request_send, instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_setup;
    }

    module_list = (globus_list_t *) globus_i_gfs_config_get("module_list");  
    for(list = module_list;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
        /* parse out alias name from <alias> or <alias>:<module> */
        alias = globus_libc_strdup((char *) globus_list_first(list));
        module = strchr(alias, ':');
        if(module != NULL)
        {
            *module = '\0';
        }
        result = globus_gridftp_server_control_attr_add_recv(
            attr, alias, globus_l_gfs_request_recv, instance);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_attr_setup;
        }
        result = globus_gridftp_server_control_attr_add_send(
            attr, alias, globus_l_gfs_request_send, instance);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_attr_setup;
        }
        globus_free(alias);
    } 

    result = globus_gridftp_server_control_attr_set_list(
        attr, globus_l_gfs_request_list, instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_setup;
    }

    result = globus_gridftp_server_control_attr_data_functions(
        attr,
        globus_l_gfs_request_active_data,
        instance,
        globus_l_gfs_request_passive_data,
        instance,
        globus_l_gfs_request_data_destroy,
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_setup;
    }

    result = globus_gridftp_server_control_attr_set_log(
        attr,
        globus_l_gfs_control_log,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_ALL, /* XXX config what-to-log */
        instance);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_setup;
    }

    result = globus_gridftp_server_control_init(&instance->server_handle);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }

    result = globus_l_gfs_add_commands(instance, instance->server_handle);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_add_commands;
    }

    

    /* disable commands if the user says to */
    if((value = globus_i_gfs_config_string("disable_command_list")) != NULL)
    {
        char *                          cmd;
        globus_list_t *                 bad_list;

        bad_list = globus_list_from_string(value, ',', NULL);
        while(!globus_list_empty(bad_list))
        {
            cmd = (char *) globus_list_remove(&bad_list, bad_list);
            globus_gsc_959_command_remove(instance->server_handle, cmd);
            globus_free(cmd);
        }
    }

    globus_mutex_lock(&globus_l_gfs_control_mutex);
    {
        if(!globus_l_gfs_control_active)
        {
            goto error_start;
        }
        
        globus_gfs_log_event(
            GLOBUS_GFS_LOG_INFO,
            GLOBUS_GFS_LOG_EVENT_START,
            "session.authn",
            0,
            NULL);

        result = globus_gridftp_server_control_start(
            instance->server_handle,
            attr,
            system_handle,
            globus_l_gfs_done_cb,
            instance);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_start;
        }
        globus_list_insert(&globus_l_gfs_server_handle_list, instance);
    }
    globus_mutex_unlock(&globus_l_gfs_control_mutex);

    if(globus_i_gfs_config_bool("inetd"))
    {
        globus_reltime_t                timer;
        GlobusTimeReltimeSet(timer, 300, 0);
        globus_callback_register_periodic(
            NULL,
            &timer,
            &timer,
            globus_l_gfs_control_watchdog_check,
            (void *) instance->server_handle);
    }

    globus_gridftp_server_control_attr_destroy(attr);

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error_start:
    globus_mutex_unlock(&globus_l_gfs_control_mutex);
error_add_commands:
error_init:
    globus_gridftp_server_control_destroy(instance->server_handle);
error_attr_setup:
    globus_gridftp_server_control_attr_destroy(attr);

error_attr:
    globus_free(instance->remote_contact);
    globus_free(instance->local_contact);

error_strdup:
    globus_free(instance);
error_malloc:
    GlobusGFSDebugExitWithError();
    return result;
}
