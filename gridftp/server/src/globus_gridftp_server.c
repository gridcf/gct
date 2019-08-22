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
#include "globus_io.h"
#include "globus_xio_tcp_driver.h"
#include "globus_i_gridftp_server.h"
#include "version.h"
#include "globus_i_gfs_ipc.h"

#ifndef TARGET_ARCH_WIN32
#include <grp.h>
#include <sys/wait.h>
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


#include <signal.h>

#include "globus_preload.h"

static globus_cond_t                    globus_l_gfs_cond;
static globus_mutex_t                   globus_l_gfs_mutex;
static globus_bool_t                    globus_l_gfs_terminated = GLOBUS_FALSE;
static unsigned int                     globus_l_gfs_outstanding = 0;
static globus_xio_driver_t              globus_l_gfs_tcp_driver = GLOBUS_NULL;
static globus_xio_server_t              globus_l_gfs_xio_server = GLOBUS_NULL;
static globus_bool_t                    globus_l_gfs_xio_server_accepting;
static globus_xio_attr_t                globus_l_gfs_xio_attr;
static globus_bool_t                    globus_l_gfs_exit = GLOBUS_FALSE;
static globus_bool_t                    globus_l_gfs_sigint_caught = GLOBUS_FALSE;

static char **                          globus_l_gfs_child_argv = NULL;
static int                              globus_l_gfs_child_argc = 0;


#ifndef BUILD_LITE
#define GLOBUS_L_GFS_SIGCHLD_DELAY 10
static globus_callback_handle_t         globus_l_gfs_sigchld_periodic_handle = 
                                            GLOBUS_NULL_HANDLE;
#endif

static
globus_result_t
globus_l_gfs_open_new_server(
    globus_xio_handle_t                 handle);

static
void
globus_l_gfs_server_closed(
    void *                              user_arg,
    globus_object_t *                   error);

static
void
globus_i_gfs_connection_closed();

static
void
globus_l_gfs_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg);
    
static
void
globus_l_gfs_server_accept_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg);

static
void 
globus_l_gfs_sigchld(
    void *                              user_arg);

static
void
globus_l_gfs_bad_signal_handler(
    int                                 signum)
{
    GlobusGFSName(globus_l_gfs_bad_signal_handler);
    GlobusGFSDebugEnter();

    /*
    globus_gfs_log_message(
        GLOBUS_GFS_LOG_ERR, 
        _GSSL("an unexpected signal occured: %d\n"), 
        signum);
    */    
    if(!globus_l_gfs_exit)
    {
        signal(signum, SIG_DFL);
        raise(signum);
    }
    else
    {
        exit(signum);
    }
    
    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_server_close_cb(
    globus_xio_server_t                 server,
    void *                              user_arg)
{
    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        globus_l_gfs_outstanding--;
        globus_l_gfs_xio_server = GLOBUS_NULL;
        globus_cond_signal(&globus_l_gfs_cond);
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);
}



static
void 
globus_l_gfs_sigint(
    void *                              user_arg)
{
    globus_result_t                     res;
    GlobusGFSName(globus_l_gfs_sigint);
    GlobusGFSDebugEnter();

    globus_gfs_log_message(
        GLOBUS_GFS_LOG_ERR, 
        "Server is shutting down...\n");
    globus_gfs_log_event(
        GLOBUS_GFS_LOG_ERR,
        GLOBUS_GFS_LOG_EVENT_START,
        "cleanup",
        0,
        NULL);

    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        if(globus_l_gfs_sigint_caught)
        {
            globus_gfs_config_set_int(
                "open_connections_count", 0);
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_ERR, 
                "Forcing unclean shutdown.\n");
            globus_gfs_log_event(
                GLOBUS_GFS_LOG_ERR,
                GLOBUS_GFS_LOG_EVENT_END,
                "cleanup",
                0,
                "msg=\"Forcing unclean shutdown.\"");
        }
        if(globus_l_gfs_xio_server)
        {
            res = globus_xio_server_register_close(
                globus_l_gfs_xio_server, globus_l_gfs_server_close_cb, NULL);
            if(res == GLOBUS_SUCCESS)
            {
                globus_l_gfs_outstanding++;
            }
            else
            {
                globus_l_gfs_xio_server = GLOBUS_NULL;
            }
        }

        globus_l_gfs_sigint_caught = GLOBUS_TRUE;
        globus_l_gfs_terminated = GLOBUS_TRUE;

        if(globus_gfs_config_get_int("open_connections_count") == 0)
        {
            globus_cond_signal(&globus_l_gfs_cond);

            globus_gfs_log_event(
                GLOBUS_GFS_LOG_ERR,
                GLOBUS_GFS_LOG_EVENT_END,
                "cleanup",
                0,
                NULL);
        }
        else
        {
            if(!globus_i_gfs_config_bool("data_node"))
            {
                globus_i_gfs_control_stop();
            }
            globus_i_gfs_ipc_stop();
            if(globus_i_gfs_config_bool("daemon"))
            {
                globus_l_gfs_sigchld(user_arg);
            }
        }
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);

    GlobusGFSDebugExit();
}

static
void 
globus_l_gfs_sighup(
    void *                              user_arg)
{
    int                                 argc;
    char **                             argv;
    GlobusGFSName(globus_l_gfs_sighup);
    GlobusGFSDebugEnter();

    globus_gfs_log_message(
        GLOBUS_GFS_LOG_INFO, 
        "Reloading config...\n");         

    argv = (char **) globus_i_gfs_config_get("argv");
    argc = globus_i_gfs_config_int("argc");

    globus_i_gfs_config_init(argc, argv, GLOBUS_FALSE);
    globus_gfs_log_message(
        GLOBUS_GFS_LOG_INFO, 
        "Done reloading config.\n");           

    GlobusGFSDebugExit();
}

static
void 
globus_l_gfs_sigchld(
    void *                              user_arg)
{
    int                                 child_pid;
    int                                 child_status;
    int                                 child_rc;
    GlobusGFSName(globus_l_gfs_sigchld);
    GlobusGFSDebugEnter();
#ifndef TARGET_ARCH_WIN32
    while(globus_gfs_config_get_int("open_connections_count") > 0 &&
        (child_pid = waitpid(-1, &child_status, WNOHANG)) > 0)
    {
        if(WIFEXITED(child_status))
        {
            child_rc = WEXITSTATUS(child_status);
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_INFO, 
                "Child process %d ended with rc = %d\n", 
                child_pid, 
                child_rc);
        globus_gfs_log_event(
            GLOBUS_GFS_LOG_INFO,
            GLOBUS_GFS_LOG_EVENT_END,
            "child",
            0,
            "c.id=%d exitcode=%d", 
            child_pid,
            child_rc);

        }
        else if(WIFSIGNALED(child_status))
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_INFO, 
                "Child process %d killed by signal %d\n",
                child_pid, 
                WTERMSIG(child_status));
            globus_gfs_log_event(
                GLOBUS_GFS_LOG_INFO,
                GLOBUS_GFS_LOG_EVENT_END,
                "child",
                0,
                "c.id=%d signal=%d", 
                child_pid,
                WTERMSIG(child_status));

        }
    
        globus_mutex_lock(&globus_l_gfs_mutex);
        {
            globus_i_gfs_connection_closed();
        }
        globus_mutex_unlock(&globus_l_gfs_mutex);   
    }
#endif
    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_signal_init()
{
    GlobusGFSName(globus_l_gfs_signal_init);
    GlobusGFSDebugEnter();
   

#   ifdef SIGINT
    globus_callback_register_signal_handler(
        SIGINT,
        GLOBUS_TRUE,
        globus_l_gfs_sigint,
        NULL);
#   endif
#   ifdef SIGHUP
    if(globus_i_gfs_config_string("loaded_config"))
    {
        globus_callback_register_signal_handler(
            SIGHUP,
            GLOBUS_TRUE,
            globus_l_gfs_sighup,
            NULL);
    }
#   endif

#   ifdef SIGKILL
    {
    /* XXX    //signal(SIGKILL, globus_l_gfs_bad_signal_handler); */
    }
#   endif
#   ifdef SIGSEGV
    {
        signal(SIGSEGV, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGABRT
    {
        signal(SIGABRT, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGBUS
    {
        signal(SIGBUS, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGFPE
    {
        signal(SIGFPE, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGILL
    {
        signal(SIGILL, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGIOT
    {
        signal(SIGIOT, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGPIPE
    {
 /* XXX       //signal(SIGPIPE, globus_l_gfs_bad_signal_handler); */
    }
#   endif
#   ifdef SIGEMT
    {
        signal(SIGEMT, globus_l_gfs_bad_signal_handler);
    }

#   endif
#   ifdef SIGSYS
    {
        signal(SIGSYS, globus_l_gfs_bad_signal_handler);
    }

#   endif
#   ifdef SIGTRAP
    {
        signal(SIGTRAP, globus_l_gfs_bad_signal_handler);
    }

#   endif
#   ifdef SIGSTOP
    {
 /* XXX       //signal(SIGSTOP, globus_l_gfs_bad_signal_handler); */
    }

#   endif

    GlobusGFSDebugExit();
}

/* called locked */
static
globus_result_t
globus_l_gfs_spawn_child(
    globus_xio_handle_t                 handle)
{
    globus_result_t                     result;
    pid_t                               child_pid;
    globus_xio_system_socket_t          socket_handle;
    int                                 rc;
    GlobusGFSName(globus_l_gfs_spawn_child);

    GlobusGFSDebugEnter();

    result = globus_xio_handle_cntl(
        handle,
        globus_l_gfs_tcp_driver,
        GLOBUS_XIO_TCP_GET_HANDLE,
        &socket_handle);
    if(result != GLOBUS_SUCCESS)
    {
        globus_gfs_log_result(
            GLOBUS_GFS_LOG_ERR, 
            _GSSL("Could not get handle from daemon process"), 
            result);
        goto error;
    }

    child_pid = fork();
    if(child_pid == 0)
    { 
        if(globus_l_gfs_xio_server)
        {
            result = globus_xio_server_register_close(
                globus_l_gfs_xio_server, globus_l_gfs_server_close_cb, NULL);
            if(result == GLOBUS_SUCCESS)
            {
                globus_l_gfs_outstanding++;
            }
            else
            {
                globus_l_gfs_xio_server = GLOBUS_NULL;
            }
        }

        rc = dup2(socket_handle, STDIN_FILENO);
        if(rc == -1)
        {
            result = GlobusGFSErrorSystemError("dup2", errno);
            globus_gfs_log_result(
                GLOBUS_GFS_LOG_ERR, 
                _GSSL("Could not open new handle for child process"), 
                result);
            goto child_error;
        }
        close(socket_handle);

        /* exec the process */
        if(*globus_l_gfs_child_argv[0] == '/')
        {
            rc = execv(globus_l_gfs_child_argv[0], globus_l_gfs_child_argv);
        }
        else
        {
            rc = execvp(globus_l_gfs_child_argv[0], globus_l_gfs_child_argv);
        }
        if(rc == -1)
        {
            char *                      error_msg;

            error_msg = globus_common_create_string("%s\n%s\n%s\n%s",
                _GSSL("Could not exec child process."),
                _GSSL("Please verify that a gridftp server is located at: "),
                globus_l_gfs_child_argv[0],
                _GSSL("Or try the -exec flag."));
            result = GlobusGFSErrorSystemError("execv", errno);
            globus_gfs_log_result(GLOBUS_GFS_LOG_ERR, error_msg, result);

            goto child_close_error;
        }
    } 
    else if(child_pid == -1)
    {
        result = GlobusGFSErrorSystemError("fork", errno);
        goto child_error;
    }
    else
    { 
        
        globus_gfs_log_event(
            GLOBUS_GFS_LOG_INFO,
            GLOBUS_GFS_LOG_EVENT_START,
            "child",
            0,
            "c.id=%d", 
            child_pid);

        /* inc the connection count 2 here since we will dec it on this close
        and on the death of the child process */
        globus_gfs_config_inc_int("open_connections_count", 2);
        globus_mutex_unlock(&globus_l_gfs_mutex);
        result = globus_xio_register_close(
            handle,
            NULL,
            globus_l_gfs_close_cb,
            NULL);    
        if(result != GLOBUS_SUCCESS)
        {
            globus_l_gfs_close_cb(handle, result, NULL);
        }        
    }    

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

child_close_error:
    close(STDIN_FILENO);
    close(socket_handle);
child_error:
    exit(1);
error:
    GlobusGFSDebugExitWithError();
    return result;
}

static
void
globus_i_gfs_connection_closed()
{
    GlobusGFSName(globus_i_gfs_connection_closed);
    GlobusGFSDebugEnter();

    globus_gfs_config_inc_int("open_connections_count", -1);
    if(globus_l_gfs_terminated || globus_i_gfs_config_bool("single"))
    {
        if(globus_gfs_config_get_int("open_connections_count") == 0)
        {
            globus_l_gfs_terminated = GLOBUS_TRUE;
            globus_cond_signal(&globus_l_gfs_cond);
        }
    }
 
    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    GlobusGFSName(globus_l_gfs_close_cb);
    GlobusGFSDebugEnter();

    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        globus_i_gfs_connection_closed();
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_ipc_closed(
    void *                              user_arg,
    globus_result_t                     result)
{
    globus_xio_handle_t                 handle;

    if(result != GLOBUS_SUCCESS)
    {
        /* XXX TODO log and error */
    }

    handle = (globus_xio_handle_t) user_arg;
    globus_mutex_unlock(&globus_l_gfs_mutex);
    {
        result = globus_xio_register_close(
            handle,
            NULL,
            globus_l_gfs_close_cb,
            NULL);
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);

    if(result != GLOBUS_SUCCESS)
    {
        globus_l_gfs_close_cb(handle, result, NULL);
    }
}

static
void
globus_l_gfs_new_server_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_xio_system_socket_t          system_handle;
    char *                              remote_ip;
    char *                              remote_contact;
    char *                              local_contact;
    char *                              tmp_local_contact = NULL;
    GlobusGFSName(globus_l_gfs_new_server_cb);
    GlobusGFSDebugEnter();
    
    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        if(result != GLOBUS_SUCCESS || globus_l_gfs_terminated)
        {
            goto error;
        }
        
        result = globus_xio_handle_cntl(
            handle,
            globus_l_gfs_tcp_driver,
            GLOBUS_XIO_TCP_GET_REMOTE_NUMERIC_CONTACT,
            &remote_ip);
        if(result != GLOBUS_SUCCESS)
        {
            char *                      tmp;
            if(globus_i_gfs_config_bool("ssh") && 
                (tmp = getenv("SSH_CLIENT")) != NULL)
            {
                remote_ip = strdup(tmp);
                tmp = strchr(remote_ip, ' ');
                if(tmp)
                {
                    *tmp = ':';
                    tmp = strchr(remote_ip, ' ');
                    if(tmp)
                    {
                        *tmp = '\0';
                    }
                }
                remote_contact = strdup(remote_ip);
            }
            else
            {
                globus_gfs_log_message(
                    GLOBUS_GFS_LOG_INFO,
                    "Couldn't get remote IP address.  "
                    "Possibly using a non-tcp protocol.\n");
                remote_ip = strdup("0.0.0.0");
                remote_contact = strdup(remote_ip);
            }
        }        
        else
        {
            result = globus_xio_handle_cntl(
                handle,
                globus_l_gfs_tcp_driver,
                GLOBUS_XIO_TCP_GET_REMOTE_CONTACT,
                &remote_contact);
            if(result != GLOBUS_SUCCESS)
            {
                globus_gfs_log_result(
                    GLOBUS_GFS_LOG_INFO,
                    "Couldn't resolve hostname from remote IP address",
                    result);
                remote_contact = strdup(remote_ip);
            }
        }
        
        if(!globus_i_gfs_config_allow_addr(remote_ip, GLOBUS_FALSE))
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_WARN,
                "Connection disallowed by configuration from: %s\n", 
                remote_ip);
            goto error;
        }

        globus_gfs_log_message(
            GLOBUS_GFS_LOG_INFO,
            "New connection from: %s\n", remote_contact);
        globus_gfs_log_event(
            GLOBUS_GFS_LOG_INFO,
            GLOBUS_GFS_LOG_EVENT_START,
            "session",
            0,
            "remotehost=%s", remote_contact);

        if(globus_i_gfs_config_bool("epsv_match") && (remote_ip[0] != '[' || strncmp(remote_ip, "[::ffff:", 8) == 0))
            globus_gfs_config_set_bool("ipv6", GLOBUS_FALSE);

        result = globus_xio_handle_cntl(
            handle,
            globus_l_gfs_tcp_driver,
            GLOBUS_XIO_TCP_GET_LOCAL_NUMERIC_CONTACT,
            &local_contact);
        if(result != GLOBUS_SUCCESS)
        {
            char *                      tmp;
            if(globus_i_gfs_config_bool("ssh") && 
                (tmp = getenv("SSH_CONNECTION")) != NULL)
            {
                local_contact = NULL;
                tmp = strchr(tmp, ' ');
                if(tmp)
                {
                    tmp++;
                    tmp = strchr(tmp, ' ');
                    if(tmp)
                    {
                        tmp++;
                        local_contact = strdup(tmp);
                    }
                }
                if(local_contact && (tmp = strchr(local_contact, ' ')) != NULL)
                {
                    *tmp = ':';
                }
                else
                {
                    local_contact = strdup("0.0.0.0");
                }
            }
            else
            {
                globus_gfs_log_message(
                    GLOBUS_GFS_LOG_INFO,
                    "Couldn't get local contact.  "
                    "Possibly using a non-tcp protocol.\n");
                local_contact = strdup("0.0.0.0");
            }
        }
        
        if(globus_i_gfs_config_string("contact_string") == NULL)
        {
            /* GLOBUS_XIO_TCP_GET_LOCAL_NUMERIC_CONTACT result */
            if(result != GLOBUS_SUCCESS)
            {
                tmp_local_contact = strdup(local_contact);
            }
            else
            {
                result = globus_xio_handle_cntl(
                    handle,
                    globus_l_gfs_tcp_driver,
                    GLOBUS_XIO_TCP_GET_LOCAL_CONTACT,
                    &tmp_local_contact);
                if(result != GLOBUS_SUCCESS)
                {
                    globus_gfs_log_message(
                        GLOBUS_GFS_LOG_INFO,
                        "Couldn't get local contact.  "
                        "Possibly using a non-tcp protocol.\n");
                    tmp_local_contact = strdup(local_contact);
                }
            }

            globus_gfs_config_set_ptr("contact_string", tmp_local_contact);
        }

        result = globus_xio_handle_cntl(
            handle,
            globus_l_gfs_tcp_driver,
            GLOBUS_XIO_TCP_SET_NODELAY,
            GLOBUS_TRUE);
            
        result = globus_xio_handle_cntl(
            handle,
            globus_l_gfs_tcp_driver,
            GLOBUS_XIO_TCP_GET_HANDLE,
            &system_handle);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_start;
        }

        if(globus_i_gfs_config_bool("data_node"))
        {
            result = globus_gfs_ipc_handle_create(
                &globus_gfs_ipc_default_iface,
                system_handle,
                globus_l_gfs_ipc_closed,
                handle);
        }
        else
        {        
            result = globus_i_gfs_control_start(
                handle, 
                system_handle, 
                remote_contact,
                local_contact, 
                globus_l_gfs_server_closed,
                (void *) (intptr_t) system_handle);
        }
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_result(
                GLOBUS_GFS_LOG_ERR, "Connection failed", result);
            goto error_start;
        }
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);
    globus_free(remote_ip);
    globus_free(local_contact);
    globus_free(remote_contact);
    GlobusGFSDebugExit();
    return;

error_start:
    globus_free(remote_ip);
    globus_free(remote_contact);   
    globus_free(local_contact);
error:
    result = globus_xio_register_close(
        handle,
        NULL,
        globus_l_gfs_close_cb,
        NULL);
    globus_mutex_unlock(&globus_l_gfs_mutex);

    if(result != GLOBUS_SUCCESS)
    {
        globus_l_gfs_close_cb(handle, result, NULL);
    }

    GlobusGFSDebugExitWithError();
}



static
void
globus_l_gfs_reject_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        if(globus_i_gfs_config_bool("inetd"))
        {
            globus_i_gfs_connection_closed();
        }
        else
        {
            globus_l_gfs_outstanding--;
        }
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);
}

static
void
globus_l_gfs_reject_write_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        if(user_arg)
        {
            globus_free(user_arg);
        }
        
        result = globus_xio_register_close(
            handle,
            NULL,
            globus_l_gfs_reject_close_cb,
            NULL);
        if(result != GLOBUS_SUCCESS)
        {
            if(globus_i_gfs_config_bool("inetd"))
            {
                globus_i_gfs_connection_closed();
            }
            else
            {
                globus_l_gfs_outstanding--;
            }
        }
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);
}

static
void
globus_l_gfs_reject_open_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    char *                              sorry_msg;
    char *                              tmp_msg;
    int                                 len;
    
    if(globus_gfs_config_get_int("connections_max") == 0 || 
        globus_i_gfs_config_bool("connections_disabled"))
    {
        if((tmp_msg = globus_gfs_config_get_string("offline_msg")) != NULL)
        {
            sorry_msg = globus_common_create_string("422 %s\r\n", tmp_msg);
        }
        else
        {
            sorry_msg = globus_libc_strdup(
                "422 Service temporarily offline. Closing control connection.\r\n");
        }
    }
    else
    {
        sorry_msg = globus_libc_strdup(
            "421 Service busy: Connection limit exceeded. Please try again "
            "later. Closing control connection.\r\n");
    }
    
    len = strlen(sorry_msg);
    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }

        result = globus_xio_register_write(
            handle,
            (globus_byte_t *) sorry_msg,
            len,
            len,
            NULL,
            globus_l_gfs_reject_write_cb,
            sorry_msg);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);

    return;

error:
    globus_free(sorry_msg);
    result = globus_xio_register_close(
        handle,
        NULL,
        globus_l_gfs_reject_close_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        globus_l_gfs_outstanding--;
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);
}


/* begin new server, this is called locked and it is assumed that
   the application is not in the termintated state */
static
globus_result_t
globus_l_gfs_open_new_server(
    globus_xio_handle_t                 handle)
{
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_open_new_server);
    GlobusGFSDebugEnter();
    
    if(globus_gfs_config_get_int("connections_max") == 0 || 
        globus_i_gfs_config_bool("connections_disabled"))
    {
            result = globus_xio_register_open(
                handle,
                NULL,
                globus_l_gfs_xio_attr,
                globus_l_gfs_reject_open_cb,
                NULL);
            if(result != GLOBUS_SUCCESS)
            {
                goto error_open;
            }
        globus_gfs_config_inc_int("open_connections_count", 1);
    }
    else
    {
        /* don't need the handle here, will get it in callback too */
        result = globus_xio_register_open(
            handle,
            NULL,
            globus_l_gfs_xio_attr,
            globus_l_gfs_new_server_cb,
            NULL);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_open;
        }
        globus_gfs_config_inc_int("open_connections_count", 1);
    }
    
    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error_open:    
    GlobusGFSDebugExitWithError();
    return result;
}

static
globus_result_t
globus_l_gfs_prepare_stack(
    globus_xio_stack_t *                stack)
{
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_prepare_stack);
    GlobusGFSDebugEnter();
    
    result = globus_xio_stack_init(stack, NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_xio_stack_push_driver(*stack, globus_l_gfs_tcp_driver);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;
    
error:
    GlobusGFSDebugExitWithError();
    return result;
}

/* begin a server instance from the channel already connected on stdin,
    this IS called locked, it is assume that the server has not been
    terminated */
static
globus_result_t
globus_l_gfs_convert_inetd_handle(void)
{
    globus_result_t                     result;
    globus_xio_stack_t                  stack;
    globus_xio_handle_t                 handle;
    globus_xio_system_socket_t          stdin_socket = STDIN_FILENO;
    GlobusGFSName(globus_l_gfs_convert_inetd_handle);
    GlobusGFSDebugEnter();
    
    result = globus_l_gfs_prepare_stack(&stack);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

#ifdef WIN32
{
    WSAPROTOCOL_INFO sock_info;
    DWORD dwRead;
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    
    if (!ReadFile
         (hStdin, &(sock_info), sizeof(WSAPROTOCOL_INFO), &dwRead, NULL)
         || dwRead != sizeof(WSAPROTOCOL_INFO)) {
            printf("Error reading socket\n");
         goto error_stack;
     }

     if ((stdin_socket = WSASocket(FROM_PROTOCOL_INFO,
                              FROM_PROTOCOL_INFO,
                              FROM_PROTOCOL_INFO,
                              &(sock_info), 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET) {
          printf("Error in creating socket :%d\n",
                          WSAGetLastError());
          goto error_stack;
     }
}
#endif

    result = globus_xio_attr_cntl(
        globus_l_gfs_xio_attr,
        globus_l_gfs_tcp_driver,
        GLOBUS_XIO_TCP_SET_HANDLE,
        (globus_xio_system_socket_t) stdin_socket);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_stack;
    }

    result = globus_xio_handle_create(&handle, stack);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_stack;
    }
    
    result = globus_l_gfs_open_new_server(handle);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_stack;
    }
    
    globus_xio_stack_destroy(stack);
    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error_stack:
    globus_xio_stack_destroy(stack);
error:
    GlobusGFSDebugExitWithError();
    return result;
}


/* a new client has connected */
static
void
globus_l_gfs_server_accept_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    GlobusGFSName(globus_l_gfs_server_accept_cb);
    GlobusGFSDebugEnter();

    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        globus_l_gfs_outstanding--;
        globus_l_gfs_xio_server_accepting = GLOBUS_FALSE;
        if(result != GLOBUS_SUCCESS)
        {
            goto error_accept;
        }

        /* if too many already open */
        if((globus_gfs_config_get_int("connections_max") != -1 &&
            globus_gfs_config_get_int("open_connections_count")
                 >= globus_gfs_config_get_int("connections_max")) ||
            globus_i_gfs_config_bool("connections_disabled"))
        {
            result = globus_xio_register_open(
                handle,
                NULL,
                NULL,
                globus_l_gfs_reject_open_cb,
                NULL);
            if(result != GLOBUS_SUCCESS)
            {
                goto error_accept;
            }
            globus_l_gfs_outstanding++;
        }
        else
        {
            /* if we fail to actually open a connection with either method we
                do not fail, just log that the connection failed */
            if(globus_i_gfs_config_bool("daemon"))
            {
                result = globus_l_gfs_spawn_child(handle);
                if(result != GLOBUS_SUCCESS)
                {
                    globus_gfs_log_result(
                        GLOBUS_GFS_LOG_ERR, "Could not spawn a child", result);
                    result = GLOBUS_SUCCESS;
                    /* if fork fails (cygwin sometimes),
                     * attempt to run non-forked */
                    if(globus_i_gfs_config_bool("fork_fallback"))
                    {
                        result = globus_l_gfs_open_new_server(handle);
                        if(result != GLOBUS_SUCCESS)
                        {
                            globus_gfs_log_result(
                                GLOBUS_GFS_LOG_ERR, 
                                _GSSL("Could not open new handle"),
                                result);
                            result = GLOBUS_SUCCESS;
                        }
                    }
                }
            }
            else
            {
                result = globus_l_gfs_open_new_server(handle);
                if(result != GLOBUS_SUCCESS)
                {
                    globus_gfs_log_result(
                        GLOBUS_GFS_LOG_ERR, 
                        _GSSL("Could not open new handle"),
                        result);
                    result = GLOBUS_SUCCESS;
                }
            }
        }
        /* be sure to close handle on server proc and close server on 
         * client proc (close on exec)
         *
         * need to handle proc exits and decrement the open server count
         * to keep the limit in effect.
         * 
         * win32 will have to simulate fork/exec... should i just do that
         * for unix too?
         */

        if(globus_i_gfs_config_bool("single"))
        {
            result = globus_xio_server_register_close(
                globus_l_gfs_xio_server, globus_l_gfs_server_close_cb, NULL);
            if(result == GLOBUS_SUCCESS)
            {
                globus_l_gfs_outstanding++;
            }
            else
            {
                globus_l_gfs_xio_server = GLOBUS_NULL;
            }
        }
        else if(!globus_l_gfs_terminated)
        {
            result = globus_xio_server_register_accept(
                globus_l_gfs_xio_server,
                globus_l_gfs_server_accept_cb,
                NULL);
            if(result != GLOBUS_SUCCESS)
            {
                goto error_register_accept;
            }
            globus_l_gfs_outstanding++;
            globus_l_gfs_xio_server_accepting = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);
    
    GlobusGFSDebugExit();
    return;

error_register_accept:
    
error_accept:
    globus_l_gfs_terminated = GLOBUS_TRUE;
    if(globus_gfs_config_get_int("open_connections_count") == 0)
    {
        globus_cond_signal(&globus_l_gfs_cond);
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);

    GlobusGFSDebugExitWithError();
}

/* start up a daemon which will spawn server instances */
static
globus_result_t
globus_l_gfs_be_daemon()
{
    char *                              contact_string;
    char *                              interface;
    globus_result_t                     result;
    globus_xio_stack_t                  stack;
    globus_xio_attr_t                   attr;
    GlobusGFSName(globus_l_gfs_be_daemon);
    GlobusGFSDebugEnter();

#ifndef TARGET_ARCH_WIN32
    result = globus_callback_register_signal_handler(
        SIGCHLD,
        GLOBUS_TRUE,
        globus_l_gfs_sigchld,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

#ifndef BUILD_LITE
/* when threaded add a periodic callback to simulate the SIGCHLD signal, since 
 * many versions of LinuxThreads don't seem to pass that to right thread */
    {
        globus_reltime_t                delay;
        
        GlobusTimeReltimeSet(
            delay, GLOBUS_L_GFS_SIGCHLD_DELAY, 0);
        result = globus_callback_register_periodic(
            &globus_l_gfs_sigchld_periodic_handle,
            &delay,
            &delay,
            globus_l_gfs_sigchld,
            NULL);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }
#endif

#endif /* !WIN32 */

    result = globus_l_gfs_prepare_stack(&stack);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_xio_attr_init(&attr);
    if(result != GLOBUS_SUCCESS)
    {
        goto stack_error;
    }
    if((interface = globus_i_gfs_config_string("control_interface")) != NULL)
    {
        result = globus_xio_attr_cntl(
            attr,
            globus_l_gfs_tcp_driver,
            GLOBUS_XIO_TCP_SET_INTERFACE,
            interface);
        if(result != GLOBUS_SUCCESS)
        {
            goto attr_error;
        }
    }
    result = globus_xio_attr_cntl(
        attr,
        globus_l_gfs_tcp_driver,
        GLOBUS_XIO_TCP_SET_PORT,
        globus_i_gfs_config_int("port"));
    if(result != GLOBUS_SUCCESS)
    {
        goto attr_error;
    }
    
    result = globus_xio_attr_cntl(
        attr,
        globus_l_gfs_tcp_driver,
        GLOBUS_XIO_TCP_SET_REUSEADDR,
        GLOBUS_TRUE);
    if(result != GLOBUS_SUCCESS)
    {
        goto attr_error;
    }
    
    result = globus_xio_server_create(&globus_l_gfs_xio_server, attr, stack);
    if(result != GLOBUS_SUCCESS)
    {
        goto attr_error;
    }

    if(globus_i_gfs_config_bool("chdir"))
    {
        char *                          chdir_to;
        chdir_to = globus_i_gfs_config_string("chdir_to");
        if(chdir_to != NULL)
        {
            chdir(chdir_to);
        }
        else
        {
            chdir("/");
        }
    }

    result = globus_xio_server_get_contact_string(
        globus_l_gfs_xio_server,
        &contact_string);
    if(result != GLOBUS_SUCCESS)
    {
        goto server_error;
    }
    globus_gfs_config_set_ptr("contact_string", contact_string);
    
    if(globus_i_gfs_config_int("port") == 0)
    {
        globus_libc_printf(_GSSL("Server listening at %s\n"), contact_string);
        fflush(stdout);
    }

    result = globus_xio_server_register_accept(
        globus_l_gfs_xio_server,
        globus_l_gfs_server_accept_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto contact_error;
    }
    globus_l_gfs_outstanding++;

    globus_l_gfs_xio_server_accepting = GLOBUS_TRUE;
    globus_xio_stack_destroy(stack);
    globus_xio_attr_destroy(attr);

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

contact_error:
    globus_free(contact_string);
server_error:
    globus_xio_server_close(globus_l_gfs_xio_server);
attr_error:
    globus_xio_attr_destroy(attr);
stack_error:
    globus_xio_stack_destroy(stack);
error:
    GlobusGFSDebugExitWithError();
    return result;
}

static
void
globus_l_gfs_server_build_child_args(
    globus_bool_t                       detach)
{
    int                                 i;
    int                                 j;
    char **                             prog_argv;
    GlobusGFSName(globus_l_gfs_server_build_child_args);

    /* exec the process */
    prog_argv = (char **) globus_i_gfs_config_get("argv");
    for(i = 0; prog_argv[i] != NULL; i++)
    {
    }
    globus_l_gfs_child_argv = (char **)
        globus_calloc(1, sizeof(char *) * (i + 10));
    if(globus_l_gfs_child_argv == NULL)
    {
        globus_result_t             result;
        char *                      error_msg;

        error_msg = globus_common_create_string("%s",
            _GSSL("Small malloc failed.  Unrecoverable error."));
        result = GlobusGFSErrorSystemError("malloc", errno);
        globus_gfs_log_result(GLOBUS_GFS_LOG_ERR, error_msg, result);

        exit(1);
    }

    if((globus_l_gfs_child_argv[0]=globus_i_gfs_config_string("exec")) == NULL)
    {
        globus_l_gfs_child_argv[0] = globus_i_gfs_config_string("exec_name");
    }
    for(i = 1, j = 1; prog_argv[i] != NULL; i++)
    {
        char *                          arg = prog_argv[i];
        
        while(*arg == '-')
        {
            arg++;
        }
        if(!detach)
        {
            if(!strcmp(arg, "s") || !strcmp(arg, "daemon"))
            {
                continue;
            }
        }
        if(!strcmp(arg, "S") || !strcmp(arg, "detach"))
        {
            continue;
        }
        if(!strcmp(arg, "config_base_path"))
        {
            continue;
        }

        globus_l_gfs_child_argv[j++] = prog_argv[i];
    }
    
    if(detach)
    {
        globus_l_gfs_child_argv[j++] = "-no-detach";
    }
    else
    {
        globus_l_gfs_child_argv[j++] = "-inetd";
    }
    
    if(globus_i_gfs_config_string("config_base_path"))
    {
        globus_l_gfs_child_argv[j++] = "-config-base-path";
        globus_l_gfs_child_argv[j++] = 
            globus_i_gfs_config_string("config_base_path");
    }
    
    globus_l_gfs_child_argv[j] = NULL;
    globus_l_gfs_child_argc = j;
}

static
void
globus_l_gfs_server_detached()
{
    int                                 rc;
    pid_t                               pid;
    GlobusGFSName(globus_l_gfs_server_detached);

    globus_l_gfs_server_build_child_args(GLOBUS_TRUE);

    pid = fork();
    if(pid < 0)
    {
    }
    /* if parent just end */
    else if(pid != 0)
    {
        char * pidfile = globus_i_gfs_config_string("pidfile");

        if(pidfile != NULL)
        {
            globus_result_t result;
            char * pidfile_evaled;

            result = globus_eval_path(pidfile, &pidfile_evaled);

            if(result == GLOBUS_SUCCESS)
            {
                mode_t                  oldmask;
                oldmask = umask(022);
                FILE * fh = fopen(pidfile_evaled, "w");
                umask(oldmask);
                
                if (fh != NULL)
                {
                    fprintf(fh, "%ld\n", (long) pid);
                    fclose(fh);
                }
            }
        }
        exit(0);
    }
    /* if child */
    else
    {
        setsid();
        freopen("/dev/null", "w+", stdin);
        freopen("/dev/null", "w+", stdout);
        freopen("/dev/null", "w+", stderr);

       /* exec the process */
        if(*globus_l_gfs_child_argv[0] == '/')
        {
            rc = execv(globus_l_gfs_child_argv[0], globus_l_gfs_child_argv);
        }
        else
        {
            rc = execvp(globus_l_gfs_child_argv[0], globus_l_gfs_child_argv);
        }
        if(rc == -1)
        {
            globus_result_t             result;
            char *                      error_msg;

            error_msg = globus_common_create_string("%s\n%s\n%s\n%s",
                _GSSL("Could not exec process."),
                _GSSL("Please verify that a gridftp server is located at: "),
                globus_l_gfs_child_argv[0],
                _GSSL("Or try the -exec flag."));
            result = GlobusGFSErrorSystemError("execv", errno);
            globus_gfs_log_result(GLOBUS_GFS_LOG_ERR, error_msg, result);

            exit(1);
        }
    }

}

static
void
gfs_l_add_acls()
{
    globus_gfs_acl_module_t *           mod;
    char *                              value;
    char *                              name;
    globus_bool_t                       cas_found = GLOBUS_FALSE;
    globus_extension_handle_t           ext_handle;
    globus_list_t *                     list = NULL;
    int                                 rc;

    value = globus_i_gfs_config_string("acl");
    if(value != NULL)
    {
        list = globus_list_from_string(value, ',', NULL);
        while(!globus_list_empty(list))
        {
            name = (char *) globus_list_remove(&list, list);
            if(strncmp(name, "cas", 3) == 0)
            {
                cas_found = GLOBUS_TRUE;
            }
            else
            {
                mod = (globus_gfs_acl_module_t *)
                    globus_extension_lookup(
                    &ext_handle,
                    GLOBUS_GFS_ACL_REGISTRY,
                    (void *) name);
                if(mod != NULL)
                {
                    globus_gfs_acl_add_module(mod);
                }
                else
                {
                    char                            buf[256];

                    snprintf(buf, 256, "globus_gridftp_server_acl_%s", name);
                    buf[255] = 0;
                    rc = globus_extension_activate(buf);
                    if(rc != 0)
                    {
                        /* log error */
                    }
                    else
                    {
                        mod = (globus_gfs_acl_module_t *)
                            globus_extension_lookup(
                            &ext_handle,
                            GLOBUS_GFS_ACL_REGISTRY,
                            (void *) name);
                        if(mod != NULL)
                        {
                            globus_gfs_acl_add_module(mod);
                        }
                    }
                }
            }
        }
    }

    /* maintain older parameter */
    if(cas_found || globus_i_gfs_config_bool("cas"))
    {
        globus_gfs_acl_add_module(&globus_gfs_acl_cas_module);
    }
}

static
char *
globus_l_gfs_get_inetd_contact()
{
    globus_result_t                     result;
    globus_sockaddr_t                   sock_name;
    globus_socklen_t                    sock_len;
    char *                              contact_string;
    GlobusGFSName(globus_l_xio_tcp_contact_string);
    
    sock_len = sizeof(sock_name);
    
    result = globus_xio_system_socket_getsockname(
        STDIN_FILENO, (struct sockaddr *) &sock_name, &sock_len);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_libc_addr_to_contact_string(
        &sock_name,
        0,
        &contact_string);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    return contact_string;

error:
    return NULL;
}

int
main(
    int                                 argc,
    char **                             argv)
{
    char *                              tmp_s;
    int                                 rc = 0;
    char *                              config;
    globus_bool_t                       inetd;
    char *                              cs;
    globus_result_t                     result;
    GlobusGFSName(main);

    LTDL_SET_PRELOADED_SYMBOLS();

  /* drop supplemental groups */
    if(getuid() == 0 && setgroups(0, NULL))
    {
        fprintf(stderr, 
            "Error: unable to drop supplemental group privileges.\n");
        return 1;
    }
    /* seed for libraries (xio random port range) */
    srand(time(NULL) * (getpid() + 1));
    
    /* parse and set envs from config file before loading modules */
    rc = globus_i_gfs_config_init_envs(argc, argv);
    if(rc != 0)
    {
        fprintf(stderr, "Failed to initialize configuration. Exiting.\n");
        return 2;
    }
    /* activate globus stuff */    
    if((rc = globus_module_activate(GLOBUS_COMMON_MODULE)) != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
            "Error: Failed to initialize GLOBUS_COMMON:\n%s",
            globus_error_print_friendly(globus_error_peek(rc)));
        goto error_activate;
    }
    if ((rc = globus_module_activate(GLOBUS_XIO_MODULE)) != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
            "Error: Failed to initialize GLOBUS_XIO:\n%s",
            globus_error_print_friendly(globus_error_peek(rc)));
        goto error_activate;
    }
    if ((rc = globus_module_activate(
            GLOBUS_GRIDFTP_SERVER_MODULE)) != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
            "Error: Failed to initialize GLOBUS_GRIDFTP_SERVER:\n%s",
            globus_error_print_friendly(globus_error_peek(rc)));
        goto error_activate;
    }
        
    /* init all the server modules */
    rc = globus_i_gfs_config_init(argc, argv, GLOBUS_FALSE);
    if(rc != 0)
    {
        fprintf(stderr, "Failed to initialize configuration. Exiting.\n");
        return 2;
    }

    globus_i_gfs_log_open();
    globus_i_gfs_config_post_init();
    globus_l_gfs_signal_init();
    globus_i_gfs_data_init();
    globus_gfs_ipc_init(!globus_i_gfs_config_bool("data_node"));
    globus_i_gfs_control_init();
    globus_i_gfs_brain_init(globus_l_gfs_data_brain_ready, NULL);

    /* initialize global variables */
    globus_mutex_init(&globus_l_gfs_mutex, GLOBUS_NULL);
    globus_cond_init(&globus_l_gfs_cond, GLOBUS_NULL);

    globus_gfs_config_set_int("open_connections_count", 0);
    globus_l_gfs_exit = globus_i_gfs_config_int("bad_signal_exit");
    globus_l_gfs_xio_server = NULL;

    /* if all the want is version info print and exit */
    if(globus_i_gfs_config_bool("help"))
    {
        globus_i_gfs_config_display_usage();
        rc = 0;
        goto error_ver;
    }
    else if(globus_i_gfs_config_bool("version"))
    {
        globus_version_print(
            local_package_name,
            &local_version,
            stderr,
            GLOBUS_TRUE);
        rc = 0;
        goto error_ver;
    }
    else if(globus_i_gfs_config_bool("versions"))
    {
        globus_version_print(
            local_package_name,
            &local_version,
            stderr,
            GLOBUS_TRUE);

        globus_module_print_activated_versions(
            stderr,
            GLOBUS_TRUE);
        rc = 0;
        goto error_ver;
    }

#if !defined(BUILD_LITE) && defined(TARGET_ARCH_LINUX) 
    {
        char                            buf[256];
        
        buf[0] = '\0';
#if defined(_CS_GNU_LIBPTHREAD_VERSION)        
        confstr(_CS_GNU_LIBPTHREAD_VERSION, buf, sizeof(buf));
#endif        
        if((strstr(buf, "linuxthreads") || buf[0] == '\0') &&
            (!globus_i_gfs_config_bool("ignore_bad_threads") &&
            getuid() == 0))
        {
            fprintf(stderr, 
                "For security reasons, running as root with LinuxThreads \n"
                "is not supported.  Please use a non-threaded flavor, update \n"
                "your libc libraries, and/or unset the LD_ASSUME_KERNEL\n"
                "environment variable. \n"
                "(confstr = %s)\n", buf); 
            rc = -1;
            goto error_ver;
        }
    }
#endif

    /* detach from the terminal if we need to */
    if(globus_i_gfs_config_bool("detach"))
    {
        globus_l_gfs_server_detached();
    }
    
    /* XXX we're missing this */
    if(globus_i_gfs_config_bool("detach"))
    {
        if(globus_i_gfs_config_bool("chdir"))
        {
            char *                  chdir_to;
            chdir_to = globus_i_gfs_config_string("chdir_to");
            if(chdir_to != NULL)
            {
                chdir(chdir_to);
            }
            else
            {
                chdir("/");
            }
       }
    }

    gfs_l_add_acls();

    tmp_s = globus_i_gfs_config_string("extension");
    if(tmp_s != NULL)
    {
        char *                          ext_name;
        char *                          ext_args = NULL;

        ext_name = strdup(tmp_s);
        tmp_s = strchr(ext_name, ':');
        if(tmp_s != NULL)
        {
            *tmp_s = '\0';
            tmp_s++;
            if(*tmp_s != '\0')
            {
                ext_args = strdup(tmp_s);
                globus_gfs_config_set_ptr("extension_args", ext_args);
            }
        }
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_INFO,
            "loading extension %s\n", ext_name);
        rc = globus_extension_activate(ext_name);
        if(rc != 0)
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_ERR,
                "failed to load loading extension %s\n", ext_name);
        }
        free(ext_name);
    }

    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        result = globus_xio_driver_load("tcp", &globus_l_gfs_tcp_driver);
        if(result != GLOBUS_SUCCESS)
        {
            rc = 1;
            goto error_lock;
        }

        result = globus_xio_attr_init(&globus_l_gfs_xio_attr);
        if(result != GLOBUS_SUCCESS)
        {
            rc = 1;
            goto error_lock;
        }

        /* in theory we could have gotten a terminated already */
        if(globus_l_gfs_terminated)
        {
            rc = 0;
            goto error_lock;
        }
        config = globus_i_gfs_config_string("loaded_config");
        inetd = globus_i_gfs_config_bool("inetd");
        
        if(inetd)
        {
            if(globus_i_gfs_config_bool("ssh"))
            {
                struct stat             statbuf;
            
                rc = fstat(STDIN_FILENO, &statbuf);
                if(rc == 0 && S_ISFIFO(statbuf.st_mode))
                {
                    /* using pipe driver */
                }
                else /* if(S_ISSOCK(statbuf.st_mode)) */
                {
                    freopen("/dev/null", "w+", stderr);
                }
            }
            #ifndef WIN32
            freopen("/dev/null", "w+", stderr);
            #endif
        }

        if(config)
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_INFO, 
                "Configuration read from %s.\n", config);
        }
        else
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_INFO, 
                "No configuration file found.\n");
        }                
        globus_gfs_log_event(
            GLOBUS_GFS_LOG_INFO,
            GLOBUS_GFS_LOG_EVENT_START,
            "configuration",
            0,
            NULL);
        globus_gfs_log_event(
            GLOBUS_GFS_LOG_INFO,
            GLOBUS_GFS_LOG_EVENT_END,
            "configuration",
            0,
            "configfile=%s",
            config ? config : "none");
           

        if(inetd)
        {
            result = globus_l_gfs_convert_inetd_handle();
            if(result != GLOBUS_SUCCESS)
            {
                rc = 1;
                goto error_lock;
            }
        }
        else
        {
            globus_l_gfs_server_build_child_args(GLOBUS_FALSE);
            result = globus_l_gfs_be_daemon();
            if(result != GLOBUS_SUCCESS)
            {
                rc = 1;
                goto error_lock;
            }
        }

        cs = globus_i_gfs_config_string("contact_string");
        if(cs == NULL)
        {
            cs = globus_l_gfs_get_inetd_contact();
        }
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_INFO,
            "Server started in %s mode.\n",
            inetd ? (globus_i_gfs_config_bool("ssh") ? "ssh" : "inetd") : "daemon");
        globus_gfs_log_event(
            GLOBUS_GFS_LOG_INFO,
            GLOBUS_GFS_LOG_EVENT_START,
            NULL,
            0,
            "mode=%s%s%s",
            inetd ? "inetd" : "daemon",
            cs ? " contact=" : "",
            cs ? cs : "");

        /* run until we are done */ 
        while(!globus_l_gfs_terminated || 
            globus_gfs_config_get_int("open_connections_count") > 0 ||
                globus_l_gfs_outstanding > 0)
        {
            globus_cond_wait(&globus_l_gfs_cond, &globus_l_gfs_mutex);
        }
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);

    globus_xio_attr_destroy(globus_l_gfs_xio_attr);
    globus_xio_driver_unload(globus_l_gfs_tcp_driver);
    
    globus_gfs_log_event(
        GLOBUS_GFS_LOG_ERR,
        GLOBUS_GFS_LOG_EVENT_END,
        NULL,
        0,
        NULL);

    globus_i_gfs_log_close();

#ifndef BUILD_LITE
    if(globus_l_gfs_sigchld_periodic_handle != GLOBUS_NULL_HANDLE)
    {
        globus_callback_unregister(
            globus_l_gfs_sigchld_periodic_handle,
            NULL,
            NULL,
            NULL);
        globus_l_gfs_sigchld_periodic_handle = GLOBUS_NULL_HANDLE;
    }     
#endif

    globus_module_deactivate_all();

    GlobusGFSDebugExit();
    return 0;

error_lock:
    globus_gfs_log_result(
        GLOBUS_GFS_LOG_ERR, _GSSL("Could not start server"), result);
    globus_mutex_unlock(&globus_l_gfs_mutex);
error_ver:
    globus_xio_attr_destroy(globus_l_gfs_xio_attr);
    globus_xio_driver_unload(globus_l_gfs_tcp_driver);
    globus_i_gfs_log_close();

error_activate:
    globus_module_deactivate_all();


    GlobusGFSDebugExitWithError();
    return rc;
}

static
void
globus_l_gfs_server_closed(
    void *                              user_arg,
    globus_object_t *                   error)
{
    GlobusGFSName(globus_l_gfs_server_closed);
    GlobusGFSDebugEnter();

#ifdef WIN32
    if(user_arg)
    {
        closesocket(user_arg);
    }
#endif
    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        globus_i_gfs_connection_closed();
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);

    if(error != NULL)
    {
        char *                          tmp_str;

        tmp_str = globus_error_print_friendly(error);
        /* XXX find out why we get (false) error here  */
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_WARN,
            "Control connection closed with error: %s\n",
             tmp_str);
        globus_free(tmp_str);
        globus_object_free(error);
    }

    GlobusGFSDebugExit();
}
