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

#include "globus_xio.h"
#include "globus_xio_tcp_driver.h"
#include "globus_i_gridftp_server.h"
#include "globus_gridftp_server_embed.h"

static globus_xio_driver_t              globus_l_gfs_tcp_driver = NULL;

typedef struct globus_l_gfs_embed_handle_s
{
    globus_mutex_t                      mutex;
    globus_bool_t                       terminated;
    unsigned int                        outstanding;
    globus_xio_server_t                 xio_server;
    globus_bool_t                       xio_server_accepting;
    globus_bool_t                       stopped;
    globus_gfs_embed_event_cb_t         event_cb;
    void *                              event_arg;
} globus_l_gfs_embed_handle_t;




static
void
globus_l_gfs_reject_close_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_gfs_embed_handle_t           handle;
    
    handle = (globus_gfs_embed_handle_t) user_arg;
    
    globus_mutex_lock(&handle->mutex);
    {
        handle->outstanding--;
    }
    globus_mutex_unlock(&handle->mutex);
}

static
void
globus_l_gfs_reject_write_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_gfs_embed_handle_t           handle;
    
    handle = (globus_gfs_embed_handle_t) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        result = globus_xio_register_close(
            xio_handle,
            NULL,
            globus_l_gfs_reject_close_cb,
            handle);
        if(result != GLOBUS_SUCCESS)
        {
            handle->outstanding--;
        }
    }
    globus_mutex_unlock(&handle->mutex);
}


static
void
globus_l_gfs_reject_open_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    char * sorry_msg = "421 Service not available, closing control connection\r\n";
    globus_gfs_embed_handle_t           handle;
    
    handle = (globus_gfs_embed_handle_t) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }

        result = globus_xio_register_write(
            xio_handle,
            (globus_byte_t *) sorry_msg,
            strlen(sorry_msg),
            strlen(sorry_msg),
            NULL,
            globus_l_gfs_reject_write_cb,
            handle);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }
    globus_mutex_unlock(&handle->mutex);

    return;

error:

    result = globus_xio_register_close(
        xio_handle,
        NULL,
        globus_l_gfs_reject_close_cb,
        handle);
    if(result != GLOBUS_SUCCESS)
    {
        handle->outstanding--;
    }
    globus_mutex_unlock(&handle->mutex);
}


static
void
globus_l_gfs_server_close_cb(
    globus_xio_server_t                 xio_server,
    void *                              user_arg)
{
    globus_gfs_embed_handle_t           handle;
    
    handle = (globus_gfs_embed_handle_t) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        handle->outstanding--;
        handle->xio_server = GLOBUS_NULL;
    }
    globus_mutex_unlock(&handle->mutex);

    if(handle->event_cb)
    {
        handle->event_cb(
            handle, 
            GLOBUS_SUCCESS, 
            GLOBUS_GFS_EMBED_EVENT_STOPPED,
            handle->event_arg);
    }
}



static
void
globus_i_gfs_connection_closed(
    globus_gfs_embed_handle_t           handle)
{
    GlobusGFSName(globus_i_gfs_connection_closed);
    GlobusGFSDebugEnter();
    
    if(handle->event_cb)
    {
        handle->event_cb(
            handle, 
            GLOBUS_SUCCESS, 
            GLOBUS_GFS_EMBED_EVENT_CONNECTION_CLOSED,
            handle->event_arg);
    }
    globus_gfs_config_inc_int("open_connections_count", -1);
    if(handle->terminated || globus_i_gfs_config_bool("single"))
    {
        if(globus_gfs_config_get_int("open_connections_count") == 0)
        {
            handle->terminated = GLOBUS_TRUE;
            if(handle->event_cb)
            {
                handle->event_cb(
                    handle, 
                    GLOBUS_SUCCESS, 
                    GLOBUS_GFS_EMBED_EVENT_STOPPED,
                    handle->event_arg);
            }
        }
    }
 
    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_server_closed_cb(
    void *                              user_arg,
    globus_object_t *                   error)
{
    globus_gfs_embed_handle_t           handle;
    GlobusGFSName(globus_l_gfs_server_closed_cb);
    GlobusGFSDebugEnter();
    
    handle = (globus_gfs_embed_handle_t) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        globus_i_gfs_connection_closed(handle);
    }
    globus_mutex_unlock(&handle->mutex);

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

static
void
globus_l_gfs_close_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_gfs_embed_handle_t           handle;
    GlobusGFSName(globus_l_gfs_close_cb);
    GlobusGFSDebugEnter();
    
    handle = (globus_gfs_embed_handle_t) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        globus_i_gfs_connection_closed(handle);
    }
    globus_mutex_unlock(&handle->mutex);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_ipc_closed(
    void *                              user_arg,
    globus_result_t                     result)
{
    globus_xio_handle_t                 xio_handle;
    globus_gfs_embed_handle_t           handle;

    if(result != GLOBUS_SUCCESS)
    {
        /* XXX TODO log and error */
    }

    xio_handle = (globus_xio_handle_t) user_arg;
    globus_mutex_unlock(&handle->mutex);
    {
        result = globus_xio_register_close(
            xio_handle,
            NULL,
            globus_l_gfs_close_cb,
            handle);
    }
    globus_mutex_unlock(&handle->mutex);

    if(result != GLOBUS_SUCCESS)
    {
        globus_l_gfs_close_cb(xio_handle, result, handle);
    }
}


static
void
globus_l_gfs_new_server_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_xio_system_socket_t          system_handle;
    char *                              remote_contact;
    char *                              local_contact;
    globus_gfs_embed_handle_t           handle;
    GlobusGFSName(globus_l_gfs_new_server_cb);
    GlobusGFSDebugEnter();
    
    handle = (globus_gfs_embed_handle_t) user_arg;
    
    globus_mutex_lock(&handle->mutex);
    {
        if(result != GLOBUS_SUCCESS || handle->terminated)
        {
            goto error;
        }
    
        result = globus_xio_handle_cntl(
            xio_handle,
            globus_l_gfs_tcp_driver,
            GLOBUS_XIO_TCP_GET_REMOTE_NUMERIC_CONTACT,
            &remote_contact);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
        if(!globus_i_gfs_config_allow_addr(remote_contact, GLOBUS_FALSE))
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_WARN,
                "Connection disallowed by configuration from: %s\n", 
                remote_contact);
            goto error;
        }
        globus_free(remote_contact);       
        result = globus_xio_handle_cntl(
            xio_handle,
            globus_l_gfs_tcp_driver,
            GLOBUS_XIO_TCP_GET_REMOTE_CONTACT,
            &remote_contact);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
        
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_INFO,
            "New connection from: %s\n", remote_contact);

        if(handle->event_cb)
        {
            handle->event_cb(
                handle, 
                GLOBUS_SUCCESS, 
                GLOBUS_GFS_EMBED_EVENT_CONNECTION_OPENED,
                handle->event_arg);
        }

        result = globus_xio_handle_cntl(
            xio_handle,
            globus_l_gfs_tcp_driver,
            GLOBUS_XIO_TCP_GET_LOCAL_NUMERIC_CONTACT,
            &local_contact);
        if(result != GLOBUS_SUCCESS)
        {
            goto error2;
        }

        result = globus_xio_handle_cntl(
            xio_handle,
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
                xio_handle);
        }
        else
        {        
            result = globus_i_gfs_control_start(
                xio_handle, 
                system_handle, 
                remote_contact,
                local_contact, 
                globus_l_gfs_server_closed_cb,
                handle);
        }
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_result(
                GLOBUS_GFS_LOG_ERR, "Connection failed", result);
            goto error_start;
        }
    }
    globus_mutex_unlock(&handle->mutex);

    globus_free(local_contact);
    globus_free(remote_contact);
    GlobusGFSDebugExit();
    return;

error_start:
    globus_free(remote_contact);   
error2:
    globus_free(local_contact);
error:
    result = globus_xio_register_close(
        xio_handle,
        NULL,
        globus_l_gfs_close_cb,
        handle);
    globus_mutex_unlock(&handle->mutex);

    if(result != GLOBUS_SUCCESS)
    {
        globus_l_gfs_close_cb(xio_handle, result, handle);
    }

    GlobusGFSDebugExitWithError();
}

/* begin new server, this is called locked and it is assumed that
   the application is not in the termintated state */
static
globus_result_t
globus_l_gfs_open_new_server(
    globus_gfs_embed_handle_t           handle,
    globus_xio_handle_t                 xio_handle)
{
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_open_new_server);
    GlobusGFSDebugEnter();
    
    /* don't need the handle here, will get it in callback too */
    result = globus_xio_register_open(
        xio_handle,
        NULL,
        NULL,
        globus_l_gfs_new_server_cb,
        handle);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_open;
    }
    globus_gfs_config_inc_int("open_connections_count", 1);
    
    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error_open:    
    GlobusGFSDebugExitWithError();
    return result;
}




/* a new client has connected */
static
void
globus_l_gfs_server_accept_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_gfs_embed_handle_t           handle;
    GlobusGFSName(globus_l_gfs_server_accept_cb);
    GlobusGFSDebugEnter();

    handle = (globus_gfs_embed_handle_t) user_arg;
    
    globus_mutex_lock(&handle->mutex);
    {
        handle->outstanding--;
        handle->xio_server_accepting = GLOBUS_FALSE;
        if(result != GLOBUS_SUCCESS)
        {
            goto error_accept;
        }

        /* if too many already open */
        if(globus_gfs_config_get_int("connections_max") != 0 &&
            globus_gfs_config_get_int("open_connections_count")
                 >= globus_gfs_config_get_int("connections_max"))
        {
            result = globus_xio_register_open(
                xio_handle,
                NULL,
                NULL,
                globus_l_gfs_reject_open_cb,
                handle);
            if(result != GLOBUS_SUCCESS)
            {
                goto error_accept;
            }
            handle->outstanding++;
        }
        else
        {
            result = globus_l_gfs_open_new_server(handle, xio_handle);
            if(result != GLOBUS_SUCCESS)
            {
                globus_gfs_log_result(
                    GLOBUS_GFS_LOG_ERR, 
                    _GSSL("Could not open new handle"), 
                    result);
                result = GLOBUS_SUCCESS;
            }
        }

        if(globus_i_gfs_config_bool("single"))
        {
            result = globus_xio_server_register_close(
                handle->xio_server, globus_l_gfs_server_close_cb, handle);
            if(result == GLOBUS_SUCCESS)
            {
                handle->outstanding++;
            }
            else
            {
                handle->xio_server = GLOBUS_NULL;
            }
        }
        else if(!handle->terminated &&
            !globus_i_gfs_config_bool("connections_disabled"))
        {
            result = globus_xio_server_register_accept(
                handle->xio_server,
                globus_l_gfs_server_accept_cb,
                handle);
            if(result != GLOBUS_SUCCESS)
            {
                goto error_register_accept;
            }
            handle->outstanding++;
            handle->xio_server_accepting = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&handle->mutex);
    
    GlobusGFSDebugExit();
    return;

error_register_accept:
    
error_accept:
    handle->terminated = GLOBUS_TRUE;
    if(globus_gfs_config_get_int("open_connections_count") == 0)
    {
        if(handle->event_cb)
        {
            handle->event_cb(
                handle, 
                result, 
                GLOBUS_GFS_EMBED_EVENT_STOPPED,
                handle->event_arg);
        }
    }
    globus_mutex_unlock(&handle->mutex);

    GlobusGFSDebugExitWithError();
}




static
globus_result_t
globus_l_gfs_be_daemon(
    globus_gfs_embed_handle_t           handle)
{
    char *                              contact_string;
    char *                              interface;
    globus_result_t                     result;
    globus_xio_stack_t                  stack;
    globus_xio_attr_t                   attr;
    GlobusGFSName(globus_l_gfs_be_daemon);
    GlobusGFSDebugEnter();

    result = globus_xio_driver_load("tcp", &globus_l_gfs_tcp_driver);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_xio_stack_init(&stack, NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    result = globus_xio_stack_push_driver(stack, globus_l_gfs_tcp_driver);
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
    
    result = globus_xio_server_create(&handle->xio_server, attr, stack);
    if(result != GLOBUS_SUCCESS)
    {
        goto attr_error;
    }

    result = globus_xio_server_get_contact_string(
        handle->xio_server,
        &contact_string);
    if(result != GLOBUS_SUCCESS)
    {
        goto server_error;
    }
    globus_gfs_config_set_ptr("contact_string", contact_string);

    result = globus_xio_server_register_accept(
        handle->xio_server,
        globus_l_gfs_server_accept_cb,
        handle);
    if(result != GLOBUS_SUCCESS)
    {
        goto contact_error;
    }
    handle->outstanding++;

    handle->xio_server_accepting = GLOBUS_TRUE;
    globus_xio_stack_destroy(stack);
    globus_xio_attr_destroy(attr);

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

contact_error:
    globus_free(contact_string);
server_error:
    globus_xio_server_close(handle->xio_server);
attr_error:
    globus_xio_attr_destroy(attr);
stack_error:
    globus_xio_stack_destroy(stack);
error:
    GlobusGFSDebugExitWithError();
    return result;
}


globus_result_t
globus_gridftp_server_embed_init(
    globus_gfs_embed_handle_t *         out_handle,
    char *                              args[])
{
    globus_l_gfs_embed_handle_t *       handle;
    globus_result_t                     result;
    int                                 arg_count;
    int                                 rc = 0;
    GlobusGFSName(globus_gridftp_server_embed_init);
    GlobusGFSDebugEnter();    

    /* activte globus stuff */    
    if((rc = globus_module_activate(GLOBUS_COMMON_MODULE)) != GLOBUS_SUCCESS ||
        (rc = globus_module_activate(GLOBUS_XIO_MODULE)) != GLOBUS_SUCCESS ||
        (rc = globus_module_activate(
            GLOBUS_GRIDFTP_SERVER_MODULE)) != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
            "Error: Failed to initialize:\n%s",
            globus_error_print_friendly(globus_error_peek(rc)));
        goto error_activate;
    }

    arg_count = 0;
    if(args != NULL)
    {
        while(args[arg_count] != NULL)
        {
            arg_count++;
        }
    }
        
    /* init all the server modules */
    globus_i_gfs_config_init(arg_count, args, GLOBUS_TRUE);
    globus_i_gfs_log_open();
    globus_i_gfs_data_init();
    globus_gfs_ipc_init(!globus_i_gfs_config_bool("data_node"));
    globus_i_gfs_control_init();
    globus_i_gfs_brain_init(globus_l_gfs_data_brain_ready, NULL);

    /* initialize handle */
    handle = (globus_l_gfs_embed_handle_t *) 
        globus_calloc(1, sizeof(globus_l_gfs_embed_handle_t));

    globus_mutex_init(&handle->mutex, GLOBUS_NULL);
    
    *out_handle = handle;

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;
    
error_activate:
    GlobusGFSDebugExitWithError();
    return result;
}    


void
globus_gridftp_server_embed_destroy(
    globus_gfs_embed_handle_t           handle)
{
    GlobusGFSName(globus_gridftp_server_embed_destroy);
    GlobusGFSDebugEnter(); 
    
    if(handle != NULL)
    {
        globus_free(handle);
    }
    
    GlobusGFSDebugExit();
    return;    
}   


globus_result_t
globus_gridftp_server_embed_start(
    globus_gfs_embed_handle_t           handle,
    globus_gfs_embed_event_cb_t         event_cb,
    void *                              user_arg)
{
    globus_result_t                     result;
    GlobusGFSName(globus_gridftp_server_embed_start);
    GlobusGFSDebugEnter();    
    
    handle->event_cb = event_cb;
    handle->event_arg = user_arg;
    
    result = globus_l_gfs_be_daemon(handle);

    GlobusGFSDebugExit();
    return result;
}


void
globus_gridftp_server_embed_stop(
    globus_gfs_embed_handle_t           handle)
{
    globus_result_t                     result;
    globus_bool_t                       callback;
    GlobusGFSName(globus_gridftp_server_embed_stop);
    GlobusGFSDebugEnter();    

    globus_gfs_log_message(
        GLOBUS_GFS_LOG_ERR, 
        "Server is shutting down...\n");

    globus_mutex_lock(&handle->mutex);
    {
        if(handle->stopped)
        {
            globus_gfs_config_set_int(
                "open_connections_count", 0);
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_ERR, 
                "Forcing unclean shutdown.\n");
        }
        if(handle->xio_server)
        {
            result = globus_xio_server_register_close(
                handle->xio_server, globus_l_gfs_server_close_cb, handle);
            if(result == GLOBUS_SUCCESS)
            {
                handle->outstanding++;
            }
            else
            {
                handle->xio_server = GLOBUS_NULL;
            }
        }

        handle->stopped = GLOBUS_TRUE;
        handle->terminated = GLOBUS_TRUE;

        if(globus_gfs_config_get_int("open_connections_count") == 0)
        {
            callback = GLOBUS_TRUE;
        }
        else
        {
            if(!globus_i_gfs_config_bool("data_node"))
            {
                globus_i_gfs_control_stop();
            }
            else
            {
                globus_i_gfs_ipc_stop();
            }
        }
    }
    globus_mutex_unlock(&handle->mutex);
    
    if(callback && handle->event_cb)
    {
        handle->event_cb(
            handle, 
            GLOBUS_SUCCESS, 
            GLOBUS_GFS_EMBED_EVENT_STOPPED,
            handle->event_arg);
    }
    
    GlobusGFSDebugExit();
    return;
}


int
globus_gridftp_server_embed_config_get_int(
    globus_gfs_embed_handle_t           handle,
    const char *                        option_name)
{
    return globus_gfs_config_get_int(option_name);
}

void *
globus_gridftp_server_embed_config_get_ptr(
    globus_gfs_embed_handle_t           handle,
    const char *                        option_name)
{
    return globus_gfs_config_get(option_name);
}
    
void
globus_gridftp_server_embed_config_set_int(
    globus_gfs_embed_handle_t           handle,
    char *                              option_name,
    int                                 int_value)
{
    globus_gfs_config_set_int(option_name, int_value);
    
    return;
}

void
globus_gridftp_server_embed_config_set_ptr(
    globus_gfs_embed_handle_t           handle,
    char *                              option_name,
    void *                              ptr_value)
{
    globus_gfs_config_set_ptr(option_name, ptr_value);
    
    return;
}


