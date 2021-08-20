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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_ftp_control_server.c
 * @brief FTP Server-side Control Connection Management
 */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

#include "globus_ftp_control.h"
#include "globus_i_ftp_control.h"
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

/* Local variable declarations */

static globus_list_t * globus_l_ftp_server_handle_list=GLOBUS_NULL;
static globus_mutex_t  globus_l_ftp_server_handle_list_mutex;
static globus_hashtable_t globus_l_ftp_control_parse_table;

#ifndef GLOBUS_SEPARATE_DOCS

typedef globus_result_t (*globus_l_ftp_control_parse_command_t)(
    globus_ftp_control_command_t *      command);

typedef struct globus_ftp_l_command_hash_entry_s
{
    globus_l_ftp_control_parse_command_t    parse_func;
    globus_ftp_control_command_code_t       code;
}
globus_ftp_l_command_hash_entry_t;

static void 
globus_l_ftp_control_listen_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result);

static void 
globus_l_ftp_control_accept_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result);

static void 
globus_l_ftp_control_stop_server_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result);

static void
globus_l_ftp_control_read_command_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result,
    globus_byte_t *                           buf, 
    globus_size_t                             nbytes);

static void 
globus_l_ftp_control_send_response_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result,
    globus_byte_t *                           buf, 
    globus_size_t                             nbytes);

static void 
globus_l_ftp_control_auth_write_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result,
    globus_byte_t *                           buf, 
    globus_size_t                             nbytes);


static void 
globus_l_ftp_control_auth_read_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result,
    globus_byte_t *                           buf, 
    globus_size_t                             nbytes);

globus_result_t globus_l_ftp_control_parse_sbuf_cmd(
    globus_ftp_control_command_t *      command);

globus_result_t globus_l_ftp_control_parse_allo_cmd(
    globus_ftp_control_command_t *      command);

globus_result_t globus_l_ftp_control_parse_port_cmd(
    globus_ftp_control_command_t *      command);

globus_result_t globus_l_ftp_control_parse_spor_cmd(
    globus_ftp_control_command_t *      command);

globus_result_t globus_l_ftp_control_parse_type_cmd(
    globus_ftp_control_command_t *      command);

globus_result_t globus_l_ftp_control_parse_stru_cmd(
    globus_ftp_control_command_t *      command);

globus_result_t globus_l_ftp_control_parse_auth_cmd(
    globus_ftp_control_command_t *      command);

globus_result_t globus_l_ftp_control_parse_mode_cmd(
    globus_ftp_control_command_t *      command);

globus_result_t globus_l_ftp_control_parse_opts_cmd(
    globus_ftp_control_command_t *      command);       

globus_result_t globus_l_ftp_control_parse_string_arg(
    globus_ftp_control_command_t *      command);

globus_result_t globus_l_ftp_control_parse_no_arg(
    globus_ftp_control_command_t *      command);

#endif

/* 
 * Hardcoded replies used int the accept/authentication process 
 */

const char * globus_i_ftp_server_220_reply=
"220 Service ready for new user.\r\n";
const char * globus_i_ftp_server_235_reply=
"235 GSSAPI Authentication succeeded\r\n";
const char * globus_i_ftp_server_331_reply=
"331 User name okay, need password.\r\n";
const char * globus_i_ftp_server_332_reply=
"332 Need account for login.\r\n";
const char * globus_i_ftp_server_334_reply=
"334 Using authentication type GSSAPI; ADAT must follow.\r\n";

/**
 * @brief Initialize a GridFTP server handle
 * @ingroup globus_ftp_control_server
 * @details
 * This function will set up (i.e. initialize all mutexes and
 * variables) a globus ftp server handle. It will also enter the
 * handle in a list used by the module activation/deactivation functions. 
 *
 * @param handle
 *        The handle to initialize.
 * @return
 *        - GLOBUS_SUCCESS
 *        - invalid handle
 */
globus_result_t 
globus_ftp_control_server_handle_init(
    globus_ftp_control_server_t *           handle)
{
    if(handle == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_handle_init: handle argument is NULL"))
            );
    }

    handle->state=GLOBUS_FTP_CONTROL_SERVER_DEAF;
    handle->callback=GLOBUS_NULL;
    handle->callback_arg=GLOBUS_NULL;
    globus_mutex_init(&(handle->mutex),GLOBUS_NULL);

    globus_mutex_lock(&globus_l_ftp_server_handle_list_mutex);
    {
        globus_list_insert(&globus_l_ftp_server_handle_list,
                           handle);
        handle->list_elem=globus_l_ftp_server_handle_list;
    }
    globus_mutex_unlock(&globus_l_ftp_server_handle_list_mutex);

    return GLOBUS_SUCCESS;
}
/* globus_ftp_control_server_handle_init() */

/**
 * @brief Destroy a GridFTP server handle
 * @ingroup globus_ftp_control_server
 * @details
 * This function will free up all dynamically allocated  memory
 * associated with a given  globus ftp server handle. It will also
 * remove the handle from a list used by the module activation/deactivation
 * functions. This function should only be called after a call to
 * globus_ftp_control_server_stop.
 *
 * @param handle
 *        The handle to destroy.
 * @return
 *        - success
 *        - invalid handle
 *        - handle is still in listening state
 */


globus_result_t 
globus_ftp_control_server_handle_destroy(
    globus_ftp_control_server_t *          handle)
{
    if(handle == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_handle_destroy: handle argument is NULL"))
            );
    }

    if(handle->state == 
       GLOBUS_FTP_CONTROL_SERVER_LISTENING)
    {
        
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_handle_destroy: handle is still listening"))
            );
    }
    
    globus_mutex_lock(&globus_l_ftp_server_handle_list_mutex);
    {
        globus_mutex_destroy(&(handle->mutex));
        globus_list_remove(&globus_l_ftp_server_handle_list,
                           handle->list_elem);
    }
    globus_mutex_unlock(&globus_l_ftp_server_handle_list_mutex);

    handle->callback=GLOBUS_NULL;
    handle->callback_arg=GLOBUS_NULL;

    return GLOBUS_SUCCESS;
}

/**
 * @brief Listen on for FTP Client Connections
 * @ingroup globus_ftp_control_server
 * @details
 * This function starts the listening on *port for connections
 * from ftp clients.  When a connection request is made callback is
 * called and passed callback_arg.  Upon return from this function
 * the server_handle structure is initialized.
 *
 *  @param server_handle
 *         A pointer to a initialized server handle.
 *  @param port
 *         A pointer to the port to listen on.  If the initial value
 *         is zero it will be set to the default value.
 *  @param callback
 *         The callback function called when connection requests
 *         are made.
 *  @param callback_arg
 *         The user argument passed to the callback function when 
 *         connection requests are made.
 *
 *  @note I'm not providing any mechanism for making sure that this
 *        function is only called once. Is this needed?
 */

globus_result_t
globus_ftp_control_server_listen(
    globus_ftp_control_server_t *               server_handle,
    unsigned short *                            port,
    globus_ftp_control_server_callback_t        callback,
    void *                                      callback_arg)
{
    globus_result_t                             result = GLOBUS_SUCCESS;
    globus_io_attr_t                            attr;
    globus_io_tcpattr_init(&attr);

    result = globus_ftp_control_server_listen_ex(
        server_handle,
        &attr,
        port,
        callback,
        callback_arg);

    globus_io_tcpattr_destroy(&attr);

    return result;
}

/**
 * @brief Listen on for FTP Client Connections
 * @ingroup globus_ftp_control_server
 * @details
 * This function starts the listening on *port for connections
 * from ftp clients.  When a connection request is made callback is
 * called and passed callback_arg.  Upon return from this function
 * the server_handle structure is initialized.
 *
 * This is an extendend version of globus_ftp_control_server_listen()
 * that provides additional control over the listening socket.
 *
 *  @param server_handle
 *         A pointer to a initialized server handle.
 *  @param attr
 *         A pointer to a globus_io_attr_t providing additional attributes
 *         for the listening socket.
 *  @param port
 *         A pointer to the port to listen on.  If the initial value
 *         is zero it will be set to the default value.
 *  @param callback
 *         The callback function called when connection requests
 *         are made.
 *  @param callback_arg
 *         The user argument passed to the callback function when 
 *         connection requests are made.
 *
 *  @note I'm not providing any mechanism for making sure that this
 *        function is only called once. Is this needed?
 */

globus_result_t
globus_ftp_control_server_listen_ex(
    globus_ftp_control_server_t *               server_handle,
    globus_io_attr_t *                          attr,
    unsigned short *                            port,
    globus_ftp_control_server_callback_t        callback,
    void *                                      callback_arg)
{
    globus_result_t                             rc;
    int                                         backlog;

    if(server_handle == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_listen: handle argument is NULL"))
            );
    }

    if(attr == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_listen: attr argument is NULL"))
            );
    }

    if(port == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_listen: port argument is NULL"))
            );
    }

    if(callback == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_listen: Argument callback is NULL"))
            );
    }

    globus_mutex_lock(&(server_handle->mutex));
    {
        if(server_handle->callback == GLOBUS_NULL)
        {
            server_handle->callback=callback;
            server_handle->callback_arg=callback_arg;

        }
        else
        {
            globus_mutex_unlock(&(server_handle->mutex));
            return globus_error_put(
                globus_error_construct_string(
                    GLOBUS_FTP_CONTROL_MODULE,
                    GLOBUS_NULL,
                    _FCSL("globus_ftp_control_server_listen: Other operation already in progress"))
                );
        }
    }
    globus_mutex_unlock(&(server_handle->mutex));

    backlog=-1;
    
    globus_io_attr_set_socket_oobinline(attr, GLOBUS_TRUE);
    globus_io_attr_set_tcp_nodelay(attr, GLOBUS_TRUE);

    rc=globus_io_tcp_create_listener(port,
                                     backlog,
                                     attr,
                                     &server_handle->io_handle);
    

    if(rc != GLOBUS_SUCCESS)
    {
        return rc;
    }
    
    globus_mutex_lock(&(server_handle->mutex));
    {
        server_handle->state=GLOBUS_FTP_CONTROL_SERVER_LISTENING;
    }
    globus_mutex_unlock(&(server_handle->mutex));
    
    rc=globus_io_tcp_register_listen(&server_handle->io_handle,
                                     globus_l_ftp_control_listen_cb,
                                     (void *) server_handle);
    
    if(rc != GLOBUS_SUCCESS)
    {
        return rc;
    }
        
    return GLOBUS_SUCCESS;
}

#ifdef GLOBUS_INTERNAL_DOC

/**
 * Internal callback for the globus_io_tcp_register_listen function.    
 * 
 * This is a internal callback used with the
 * globus_io_tcp_register_listen function, which in this library is
 * used for detecting connections to a listening "server" socket.
 * If this function is called with a successful result the correct
 * user callback is called, followed by another call to
 * globus_io_tcp_register_listen.
 *
 * @param arg
 *        The callback argument.
 * @param handle
 *        The globus_io handle for the connection. In practice this
 *        represents the socket fd for the connection.
 * @param result
 *        The result of the listen operation
 *
 * @return void
 *
 * @note If a error is detected in this function the user callback is
 *       called with an appropriate error object and the function
 *       returns. 
 */

#endif


static void 
globus_l_ftp_control_listen_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result)
{
    globus_object_t *                         error;
    globus_ftp_control_server_t *             server_handle;
    globus_result_t                           rc;

    server_handle=(globus_ftp_control_server_t *) arg;

    if(result != GLOBUS_SUCCESS)
    {
        error=globus_error_get(result);
        (server_handle->callback)(server_handle->callback_arg,
                                  server_handle,
                                  error);
        globus_object_free(error);
        return;
    }

    (server_handle->callback)(server_handle->callback_arg,
                              server_handle,
                              GLOBUS_NULL);
    
    rc=globus_io_tcp_register_listen(&server_handle->io_handle,
                                     globus_l_ftp_control_listen_cb,
                                     arg);
    
    if(rc != GLOBUS_SUCCESS)
    {
        error=globus_error_get(rc);
        (server_handle->callback)(server_handle->callback_arg,
                                  server_handle,
                                  error);
        globus_object_free(error);
        return;
    }


    return;
}


/**
 * @brief Initialize a GridFTP command
 * @ingroup globus_ftp_control_server
 * @details
 * This function initializes a command structure based on a null
 * terminated string representing one line of input from the
 * client. The command structure is used as a convenience to determine 
 * what command the client issued.  This function parses a command
 * string sent by a client and populates the command argument
 * appropriately. In the GSSAPI case it will also decode and unwrap
 * the command before parsing it.  
 *
 * @param command
 *        A pointer to the command structure to be initialized
 * @param raw_command
 *        A null terminated line of client input. Should contain one
 *        command.
 * @param auth_info
 *        Authentication information needed for unwrapping a command
 */
globus_result_t
globus_ftp_control_command_init(
    globus_ftp_control_command_t *              command,
    char *                                      raw_command,
    globus_ftp_control_auth_info_t *            auth_info)
{
    int                                         i;
    int                                         j;
    int                                         length;
    char                                        cmd[5];
    char *                                      decoded_cmd = GLOBUS_NULL;
    globus_result_t                             rc;
    globus_ftp_l_command_hash_entry_t *         command_hash_entry;
    
    length=strlen(raw_command);

    command->noop.raw_command=
        (char *) globus_libc_malloc(length+1);
    command->noop.string_arg = NULL;

    if(command->noop.raw_command == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_command_init: malloc failed")));
    }

    j=0;

    for(i=0;i<length;i++)
    {
        if(raw_command[i] == GLOBUS_I_TELNET_IAC)
        {
            i++;
        }
        else
        {
            command->noop.raw_command[j]=raw_command[i];  
            j++;
        }
    }
    command->noop.raw_command[j]='\0';

    if(auth_info->authenticated == GLOBUS_TRUE)
    {
        rc=globus_i_ftp_control_decode_command(command->noop.raw_command,
                                               &decoded_cmd,
                                               auth_info);
        if(rc != GLOBUS_SUCCESS)
        {
            globus_libc_free(command->noop.raw_command);
            return rc;
        }
    }
    
    if(decoded_cmd != GLOBUS_NULL)
    {
        globus_libc_free(command->noop.raw_command);
        command->noop.raw_command = decoded_cmd;
    }

    /* convert command to upper case */

    cmd[0]='\0';

    sscanf(command->noop.raw_command,"%4s",cmd);

    i=0;
    
    while(cmd[i] != '\0')
    {
        cmd[i]=toupper(cmd[i]);
        i++;
    }

    command_hash_entry = (globus_ftp_l_command_hash_entry_t *)
        globus_hashtable_lookup(&globus_l_ftp_control_parse_table,
                                (void *) cmd);

    if(command_hash_entry != GLOBUS_NULL)
    {
        command->code = command_hash_entry->code;
        rc = command_hash_entry->parse_func(command);
    }
    else
    {
        command->code = GLOBUS_FTP_CONTROL_COMMAND_UNKNOWN;
        rc = GLOBUS_SUCCESS;
    }
        
    return rc;
}

/**
 * @brief Destroy a GridFTP command
 * @ingroup globus_ftp_control_server
 * @details
 * This function frees up the memory allocated to the command
 * argument.
 * 
 * @param command
 *        The command structure whose associated memory is to be freed
 */
globus_result_t
globus_ftp_control_command_destroy(
    globus_ftp_control_command_t *           command)
{
    if(command == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_command_destroy: command argument is NULL"))
            );
    }
    
    globus_libc_free(command->noop.raw_command);
    
    switch(command->code)
    {
    case GLOBUS_FTP_CONTROL_COMMAND_SITE:
    case GLOBUS_FTP_CONTROL_COMMAND_DELE:  
    case GLOBUS_FTP_CONTROL_COMMAND_RMD:
    case GLOBUS_FTP_CONTROL_COMMAND_MKD:
    case GLOBUS_FTP_CONTROL_COMMAND_NLST:
    case GLOBUS_FTP_CONTROL_COMMAND_HELP:
    case GLOBUS_FTP_CONTROL_COMMAND_STAT:
    case GLOBUS_FTP_CONTROL_COMMAND_STOU:
    case GLOBUS_FTP_CONTROL_COMMAND_ACCT:
    case GLOBUS_FTP_CONTROL_COMMAND_CWD:
    case GLOBUS_FTP_CONTROL_COMMAND_PASS:
    case GLOBUS_FTP_CONTROL_COMMAND_PASV:
    case GLOBUS_FTP_CONTROL_COMMAND_SPAS:
    case GLOBUS_FTP_CONTROL_COMMAND_USER:
    case GLOBUS_FTP_CONTROL_COMMAND_SMNT:
    case GLOBUS_FTP_CONTROL_COMMAND_LIST:
    case GLOBUS_FTP_CONTROL_COMMAND_RETR:
    case GLOBUS_FTP_CONTROL_COMMAND_STOR:
    case GLOBUS_FTP_CONTROL_COMMAND_APPE:
    case GLOBUS_FTP_CONTROL_COMMAND_RNFR:
    case GLOBUS_FTP_CONTROL_COMMAND_RNTO:
    {
        if(command->noop.string_arg != NULL)
        {
            globus_libc_free(command->noop.string_arg);
        }
        break;
    }
    case GLOBUS_FTP_CONTROL_COMMAND_SPOR:
    {
        globus_libc_free(command->spor.host_port);
        break;  
    }
    default:
        break;
    }
    return GLOBUS_SUCCESS;
}
/* globus_ftp_control_command_destroy() */

/**
 * @brief Copy of GridFTP command
 * @ingroup globus_ftp_control_server
 * @details
 * This function should be called when the user needs to make a 
 * copy of a command structure.
 *
 * @param dest
 *         The area of memory that the command structure is copied to.
 * @param src
 *         The command structure to be copied.
 */
globus_result_t
globus_ftp_control_command_copy(
    globus_ftp_control_command_t *           dest,
    globus_ftp_control_command_t *           src)
{
    if(dest == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_command_copy: dest argument is NULL"))
            ); 
    }

    if(src == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_command_copy: src argument is NULL"))
            ); 
    }

    dest->code=src->code;

    dest->noop.raw_command=globus_libc_strdup(src->noop.raw_command);
 
    if(dest->noop.raw_command == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_command_copy: strdup failed"))
            ); 
    }
   
    switch(dest->code)
    {
    case GLOBUS_FTP_CONTROL_COMMAND_UNKNOWN:
    case GLOBUS_FTP_CONTROL_COMMAND_SITE:
    case GLOBUS_FTP_CONTROL_COMMAND_DELE:  
    case GLOBUS_FTP_CONTROL_COMMAND_RMD:
    case GLOBUS_FTP_CONTROL_COMMAND_MKD:
    case GLOBUS_FTP_CONTROL_COMMAND_NLST:
    case GLOBUS_FTP_CONTROL_COMMAND_HELP:
    case GLOBUS_FTP_CONTROL_COMMAND_STAT:
    case GLOBUS_FTP_CONTROL_COMMAND_STOU:
    case GLOBUS_FTP_CONTROL_COMMAND_ACCT:
    case GLOBUS_FTP_CONTROL_COMMAND_CWD:
    case GLOBUS_FTP_CONTROL_COMMAND_PASS:
    case GLOBUS_FTP_CONTROL_COMMAND_PASV:
    case GLOBUS_FTP_CONTROL_COMMAND_SPAS:
    case GLOBUS_FTP_CONTROL_COMMAND_USER:
    case GLOBUS_FTP_CONTROL_COMMAND_SMNT:
    case GLOBUS_FTP_CONTROL_COMMAND_LIST:
    case GLOBUS_FTP_CONTROL_COMMAND_RETR:
    case GLOBUS_FTP_CONTROL_COMMAND_STOR:
    case GLOBUS_FTP_CONTROL_COMMAND_APPE:
    case GLOBUS_FTP_CONTROL_COMMAND_RNFR:
    case GLOBUS_FTP_CONTROL_COMMAND_RNTO:
    case GLOBUS_FTP_CONTROL_COMMAND_REST:
    case GLOBUS_FTP_CONTROL_COMMAND_QUIT:

        dest->noop.string_arg = GLOBUS_NULL;
        
        if(src->noop.string_arg != GLOBUS_NULL)
        {
            dest->noop.string_arg=
                globus_libc_strdup(src->noop.string_arg);
         
            if(dest->noop.string_arg == GLOBUS_NULL)
            {
                globus_libc_free(dest->noop.raw_command);
                return globus_error_put(
                    globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_ftp_control_command_copy: strdup failed"))
                    ); 
            }
        }
        break;
    case GLOBUS_FTP_CONTROL_COMMAND_PORT:
        dest->port.host_port.host[0]=src->port.host_port.host[0];
        dest->port.host_port.host[1]=src->port.host_port.host[1];
        dest->port.host_port.host[2]=src->port.host_port.host[2];
        dest->port.host_port.host[3]=src->port.host_port.host[3];
        dest->port.host_port.port=src->port.host_port.port;
        dest->port.host_port.hostlen=4;
        break;
    case GLOBUS_FTP_CONTROL_COMMAND_SPOR:
        dest->spor.num_args=src->spor.num_args;
        dest->spor.host_port = (globus_ftp_control_host_port_t *)
            globus_libc_malloc(src->spor.num_args * 
                               sizeof(globus_ftp_control_host_port_t));
        if(dest->spor.host_port == GLOBUS_NULL)
        {
            globus_libc_free(dest->noop.raw_command);
            return globus_error_put(
                globus_error_construct_string(
                    GLOBUS_FTP_CONTROL_MODULE,
                    GLOBUS_NULL,
                    _FCSL("globus_ftp_control_command_copy: malloc failed"))
                ); 
        }
        
        memcpy(dest->spor.host_port,
               src->spor.host_port,
               src->spor.num_args * 
               sizeof(globus_ftp_control_host_port_t));
        break;
    case GLOBUS_FTP_CONTROL_COMMAND_TYPE:
        dest->type.type=src->type.type;
        dest->type.option=src->type.option;
        dest->type.bytesize=src->type.bytesize;
        break;
    case GLOBUS_FTP_CONTROL_COMMAND_STRU:
        dest->stru.structure=src->stru.structure;
        break;
    case GLOBUS_FTP_CONTROL_COMMAND_MODE:
        dest->mode.mode=src->mode.mode;
        break;
    case GLOBUS_FTP_CONTROL_COMMAND_ALLO:
        dest->allo.size=src->allo.size;
        dest->allo.record_size=src->allo.record_size;
        break;
    default:
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_command_copy: Unknown command"))
            ); 
    }
    
    return GLOBUS_SUCCESS;
}

/**
 * @brief Stop listening for GridFTP client connections
 * @ingroup globus_ftp_control_server
 * @details
 * This function stops listening on the given listener object
 * for client connections.  All existing client connections are 
 * left open.
 *
 * @param listener 
 *        the globus_ftp_control_server_t object that should
 *        no longer listen for connections.
 * @param callback
 *        The user callback that will be called when the server
 *        structure is no longer listening.
 * @param callback_arg
 *        The user argument that is passed into callback.
 */
globus_result_t
globus_ftp_control_server_stop(
    globus_ftp_control_server_t *               listener,
    globus_ftp_control_server_callback_t        callback,
    void *                                      callback_arg)
{
    globus_result_t                             rc;
    globus_i_ftp_server_passthru_cb_arg_t *     cb_arg;
    

    if(listener == GLOBUS_NULL)
    { 
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_stop: listener argument is NULL"))
            );
    }

    if(callback == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_stop: callback argument is NULL"))
            );
    }
    
    globus_mutex_lock(&(listener->mutex));
    {
        if(listener->state != 
           GLOBUS_FTP_CONTROL_SERVER_LISTENING)
        {
            globus_mutex_unlock(&(listener->mutex));
            return GLOBUS_SUCCESS;
        }
        listener->state=GLOBUS_FTP_CONTROL_SERVER_DEAF;
    }
    globus_mutex_unlock(&(listener->mutex));

    cb_arg = (globus_i_ftp_server_passthru_cb_arg_t *)
        globus_libc_malloc(
            sizeof(globus_i_ftp_server_passthru_cb_arg_t));
    
    if(cb_arg == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_stop: malloc failed"))
            );
    }

    cb_arg->callback=callback;
    cb_arg->callback_arg=callback_arg;
    cb_arg->server_handle=listener;

    rc=globus_io_register_close(&listener->io_handle,
                                globus_l_ftp_control_stop_server_cb,
                                (void *) cb_arg);
    if(rc != GLOBUS_SUCCESS)
    {
        globus_libc_free(cb_arg);
        return rc;
    }
    
    return GLOBUS_SUCCESS;

}

#ifdef GLOBUS_INTERNAL_DOC

/**
 * Internal callback for the globus_io_register_close function.    
 * 
 * This is an internal callback used as part of the
 * globus_ftp_control_server_stop function. It checks the result of
 * the close and calls the user callback.
 *
 * @param arg
 *        The callback argument, which in this case is used to pass
 *        the original user callback and argument.
 * @param handle
 *        The globus_io handle for the connection. In practice this
 *        represents the socket fd for the connection.
 * @param result
 *        The result of the close operation 
 *
 * @return void
 *
 * @note If a error is detected in this function the user callback is
 *       called with an appropriate error object or ftp response and
 *       the function returns. 
 */

#endif

static void 
globus_l_ftp_control_stop_server_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result)
{
    globus_i_ftp_server_passthru_cb_arg_t *   cb_arg;
    globus_object_t *                         error;

    cb_arg = (globus_i_ftp_server_passthru_cb_arg_t *) arg;
    
    if(result != GLOBUS_SUCCESS){
        error=globus_error_get(result);
        (cb_arg->callback)(cb_arg->callback_arg,
                           cb_arg->server_handle,
                           error);
        globus_object_free(error);
    }
    else
    {
        (cb_arg->callback)(cb_arg->callback_arg,
                           cb_arg->server_handle,
                           GLOBUS_NULL);
    }
    globus_libc_free(cb_arg);
    return;
}


/**
 * @brief Accept a Client Connection
 * @ingroup globus_ftp_control_server
 * @details
 * This function is called to accept a connection request from
 * a client.
 *
 * When the listen callback is called (see
 * globus_ftp_control_server_listen) a client has requested a
 * connection.  This function must be called to accept that user
 * connection request.  Once the connection is established or if
 * a error occurs, the callback function is called.
 * 
 * @param listener
 *        The server object that received the connection request. 
 * @param handle
 *        The control connection object.  This structure will be populated
 *        and passed to the callback when the client is authorized.  This
 *        structure represents the control connection between the server
 *        and client.  It will be used to read commands from the client
 *        and send responses to the client.]
 * @param callback
 *        The function called when the client connection has been
 *        accepted.
 * @param callback_arg
 *        The user argument passed to the callback.
 *
 * @note This functions assumes the server and control handles
 *       have been initialized prior to calling this function.
 */
globus_result_t
globus_ftp_control_server_accept(
    globus_ftp_control_server_t *               listener,
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_callback_t               callback,
    void *                                      callback_arg)
{
    globus_io_attr_t                            listener_attr = NULL;
    globus_result_t                             rc;
    globus_bool_t                               call_close_cb = GLOBUS_FALSE;

    if(handle == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_accept: handle argument is NULL"))
            );
    }

    if(listener == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_accept: listener argument is NULL"))
            ); 
    }

    globus_mutex_lock(&(listener->mutex));
    {
        if(listener->state != 
           GLOBUS_FTP_CONTROL_SERVER_LISTENING)
        {
            globus_mutex_unlock(&(listener->mutex));
            return globus_error_put(
                globus_error_construct_string(
                    GLOBUS_FTP_CONTROL_MODULE,
                    GLOBUS_NULL,
                    _FCSL("globus_ftp_control_server_accept: server not listening"))
                );
        }
    }
    globus_mutex_unlock(&(listener->mutex));

    globus_mutex_lock(&(handle->cc_handle.mutex));
    {
        if(handle->cc_handle.auth_cb == GLOBUS_NULL &&
           handle->cc_handle.cc_state == GLOBUS_FTP_CONTROL_UNCONNECTED)
        {
            handle->cc_handle.accept_cb=callback;
            handle->cc_handle.accept_cb_arg=callback_arg;
            handle->cc_handle.cb_count++;
        }
        else
        {
            globus_mutex_unlock(&(handle->cc_handle.mutex));
            return globus_error_put(
                globus_error_construct_string(
                    GLOBUS_FTP_CONTROL_MODULE,
                    GLOBUS_NULL,
                    _FCSL("globus_ftp_control_server_accept: Other operation already in progress"))
                );
        }
    }
    globus_mutex_unlock(&(handle->cc_handle.mutex));

    rc = globus_io_tcp_get_attr(
        &listener->io_handle,
        &listener_attr);

    rc=globus_io_tcp_register_accept(&(listener->io_handle),
                                     &listener_attr,
                                     &(handle->cc_handle.io_handle),
                                     globus_l_ftp_control_accept_cb,
                                     (void *) handle);
    globus_io_tcpattr_destroy(&listener_attr);
    
    if(rc != GLOBUS_SUCCESS)
    {
        globus_mutex_lock(&(handle->cc_handle.mutex));
        {
            handle->cc_handle.cb_count--;
            if(!handle->cc_handle.cb_count &&
               handle->cc_handle.cc_state == GLOBUS_FTP_CONTROL_CLOSING)
            {
                call_close_cb = GLOBUS_TRUE;
            }
        }
        globus_mutex_unlock(&(handle->cc_handle.mutex));

        if(call_close_cb == GLOBUS_TRUE)
        {
            globus_i_ftp_control_call_close_cb(handle);
        }

        return rc;
    }

    return GLOBUS_SUCCESS;

}

#ifdef GLOBUS_INTERNAL_DOC

/**
 * Internal callback for the globus_io_register_accept function.    
 * 
 * This is an internal callback used as part of the
 * globus_ftp_control_accept function. It checks the result of
 * the accept, and sends a initial response to the user.
 *
 * @param arg
 *        The callback argument, which in this case is the control
 *        handle.
 * @param handle
 *        The globus_io handle for the connection. In practice this
 *        represents the socket fd for the connection.
 * @param result
 *        The result of the accept operation 
 *
 * @return void
 *
 * @note If a error is detected in this function the user callback is
 *       called with an appropriate error object or ftp response and
 *       the function returns. 
 */

#endif

static void 
globus_l_ftp_control_accept_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result)
{
    globus_object_t *                         error = GLOBUS_NULL;
    globus_ftp_cc_handle_t *                  cc_handle;
    globus_ftp_control_handle_t *             c_handle;
    globus_bool_t                             call_close_cb = GLOBUS_FALSE;

    
    c_handle=(globus_ftp_control_handle_t *) arg;
    cc_handle=&(c_handle->cc_handle);

    if(result != GLOBUS_SUCCESS)
    {
        error=globus_error_get(result);
    }

    globus_mutex_lock(&(cc_handle->mutex));
    {
        if(cc_handle->cc_state == GLOBUS_FTP_CONTROL_UNCONNECTED)
        {
            cc_handle->cc_state=GLOBUS_FTP_CONTROL_CONNECTED;
        }
    }
    globus_mutex_unlock(&(cc_handle->mutex));

    /*
     *    call the users callback
     */
    (cc_handle->accept_cb)(cc_handle->accept_cb_arg,
                           c_handle,
                           error);

    if(error != GLOBUS_NULL)
    {
        globus_object_free(error);
    }

    globus_mutex_lock(&(cc_handle->mutex));
    {
        cc_handle->cb_count--;
        
        if(!cc_handle->cb_count &&
           cc_handle->cc_state == GLOBUS_FTP_CONTROL_CLOSING)
        {
            call_close_cb = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&(cc_handle->mutex));

    if(call_close_cb == GLOBUS_TRUE)
    {
        globus_i_ftp_control_call_close_cb(c_handle);
    }
}

/**
 * @brief Authenticate a GridFTP Client Connection
 * @ingroup globus_ftp_control_server
 * @details
 * This function is called to authenticate a connection from
 * a client.
 *
 * After a client connection has been accepted (using the
 * globus_ftp_control_server_accept call), this function should be called
 * to authenticate the client. The caller of this function may specify
 * certain authentication requirements using the auth_requirements parameter.
 * 
 * @param handle
 *         The control connection object.  This structure will be populated
 *         and passed to the callback when the client is authorized.  This
 *         structure represents the control connection between the server
 *         and client.  It will be used to read commands from the client
 *         and send responses to the client.]
 * @param auth_requirements
 *         This structure represents the authentication requirements that
 *         the user has for a given connection.  For example GridFTP
 *         user name, password, and account.
 * @param callback
 *         The function called when the client authentication has been
 *         accepted or rejected.
 * @param callback_arg
 *         The user argument passed to the callback.
 *
 * @note It is up to the user of this function to send the reply to
 *       the last command of the authentication sequence.
 * @note This functions assumes the server and control handles
 *       have been initialized prior to calling this function.
 */
globus_result_t
globus_ftp_control_server_authenticate( 
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_auth_requirements_t      auth_requirements,
    globus_ftp_control_auth_callback_t          callback,
    void *                                      callback_arg)
{
    globus_object_t *                         error = GLOBUS_NULL;
    globus_ftp_cc_handle_t *                  cc_handle;
    globus_result_t                           rc;
    globus_result_t                           rc2;
    globus_bool_t                             call_close_cb = GLOBUS_FALSE;

    cc_handle=&(handle->cc_handle);

    if(handle == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_server_authenticate: handle argument is NULL"))
            );
    }

    globus_mutex_lock(&(handle->cc_handle.mutex));
    {
        if(handle->cc_handle.auth_cb == GLOBUS_NULL &&
           handle->cc_handle.cc_state == GLOBUS_FTP_CONTROL_CONNECTED)
        {
            handle->cc_handle.auth_cb=callback;
            handle->cc_handle.auth_cb_arg=callback_arg;
            handle->cc_handle.auth_requirements=auth_requirements;
            handle->cc_handle.cb_count++;
        }
        else
        {
            globus_mutex_unlock(&(handle->cc_handle.mutex));
            return globus_error_put(
                globus_error_construct_string(
                    GLOBUS_FTP_CONTROL_MODULE,
                    GLOBUS_NULL,
                    _FCSL("globus_ftp_control_server_accept: Other operation already in progress"))
                );
        }
    }
    globus_mutex_unlock(&(handle->cc_handle.mutex));

    rc = globus_i_ftp_control_auth_info_init(
        &(cc_handle->auth_info),GLOBUS_NULL);
    if(rc != GLOBUS_SUCCESS)
    {
        error=globus_error_get(rc);
        goto error_std;
    }

    if (auth_requirements & GLOBUS_FTP_CONTROL_AUTH_REQ_TLS)
    {
        globus_xio_handle_t             xio_handle = NULL;
        OM_uint32                       maj_stat = GSS_S_COMPLETE;
        OM_uint32                       min_stat = GSS_S_COMPLETE;
        OM_uint32                       ctx_flags = 0;
        gss_name_t                      src_name = GSS_C_NO_NAME;
        gss_buffer_desc                 subject_buf = GSS_C_EMPTY_BUFFER;

        rc = globus_io_handle_get_xio_handle(
            &handle->cc_handle.io_handle,
            &xio_handle);

        if (rc != GLOBUS_SUCCESS)
        {
            error = globus_error_get(rc);
            goto error_std;
        }

        rc = globus_xio_handle_cntl(
            xio_handle,
            globus_io_compat_get_gsi_driver(),
            GLOBUS_XIO_GSI_GET_CONTEXT,
            &cc_handle->auth_info.auth_gssapi_context);

        if (rc != GLOBUS_SUCCESS
            || cc_handle->auth_info.auth_gssapi_context
                == GSS_C_NO_CONTEXT)
        {
            error = globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                (rc != GLOBUS_SUCCESS) ? globus_error_get(rc) : NULL,
                _FCSL("TLS control channel not established"));

            goto error_std;
        }

        cc_handle->use_auth = GLOBUS_FALSE;
        /*
         * Boolean overload to indicate that authenticated but not use
         * ENC/MIC commands
         */
        cc_handle->auth_info.authenticated = GLOBUS_TRUE + 1;

        maj_stat = gss_inquire_context(
            &min_stat,
            cc_handle->auth_info.auth_gssapi_context,
            &cc_handle->auth_info.target_name,
            NULL,
            NULL,
            NULL,
            &ctx_flags,
            NULL,
            NULL);

        if (maj_stat != GSS_S_COMPLETE)
        {
            error = globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                globus_error_get(min_stat),
                _FCSL("gss_inquire_context failed"));
            goto error_std;
        }

        if (ctx_flags & GSS_C_CONF_FLAG)
        {
            cc_handle->auth_info.encrypt = GLOBUS_TRUE;
        }

        maj_stat = gss_display_name(
            &min_stat,
            cc_handle->auth_info.target_name,
            &subject_buf,
            NULL);

        if (maj_stat != GSS_S_COMPLETE)
        {
            error = globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                NULL,
                _FCSL("gss_display_name failed"));
            goto error_std;
        }
        cc_handle->auth_info.auth_gssapi_subject
            = malloc(subject_buf.length + 1);
    
        if (cc_handle->auth_info.auth_gssapi_subject == NULL)
        {
            gss_release_buffer(&min_stat, &subject_buf);
           
            error = globus_error_construct_string(
                    GLOBUS_FTP_CONTROL_MODULE,
                    GLOBUS_NULL,
                    _FCSL("globus_l_ftp_control_auth_read_cb: malloc failed"));
            goto error_std;
        }

        snprintf(
            cc_handle->auth_info.auth_gssapi_subject,
            subject_buf.length + 1,
            "%s",
            subject_buf.value);

        gss_release_buffer(&min_stat, &subject_buf);
    }

    rc=globus_io_register_read(&cc_handle->io_handle,
                               cc_handle->read_buffer,
                               GLOBUS_FTP_CONTROL_READ_BUFFER_SIZE,
                               1, 
                               globus_l_ftp_control_auth_read_cb,
                               handle);
    if(rc != GLOBUS_SUCCESS)
    {
        error=globus_error_get(rc);
        goto error_std;
    }
 
    return GLOBUS_SUCCESS;
    
error_std:
    
    rc2 = globus_i_ftp_control_auth_info_destroy(
        &(cc_handle->auth_info));
    globus_assert(rc2 == GLOBUS_SUCCESS);

    globus_mutex_lock(&(cc_handle->mutex));
    {
        cc_handle->cb_count--;
        
        if(!cc_handle->cb_count &&
           cc_handle->cc_state == GLOBUS_FTP_CONTROL_CLOSING)
        {
            call_close_cb = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&(cc_handle->mutex));

    if(call_close_cb == GLOBUS_TRUE)
    {
        globus_i_ftp_control_call_close_cb(handle);
    }
    
    return rc;
}

#ifdef GLOBUS_INTERNAL_DOC

/**
 * Internal callback for the globus_io_register_write function.    
 * 
 * This is an internal callback used as part of the
 * globus_ftp_control_authenticate function. It checks the result of
 * the write (which was used to send a response to the client), and
 * if the authentication requirements are set to NONE calls the user
 * callback and returns or registers a read in anticipation of further
 * client commands.
 *
 * @param arg
 *        The callback argument, which in this case is the control
 *        handle.
 * @param handle
 *        The globus_io handle for the connection. In practice this
 *        represents the socket fd for the connection.
 * @param result
 *        The result of the write operation 
 *
 * @return void
 *
 * @note If a error is detected in this function the user callback is
 *       called with an appropriate error object or ftp response and
 *       the function returns. 
 */

#endif

static void 
globus_l_ftp_control_auth_write_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result,
    globus_byte_t *                           buf, 
    globus_size_t                             nbytes)
{
    globus_ftp_cc_handle_t *                  cc_handle;
    globus_ftp_control_handle_t *             c_handle;
    globus_object_t *                         error;
    globus_result_t                           rc;
    globus_bool_t                             call_close_cb = GLOBUS_FALSE;
    void *                                    callback_arg;
    globus_ftp_control_auth_callback_t        callback;

    c_handle=(globus_ftp_control_handle_t *) arg;
    cc_handle=&(c_handle->cc_handle);

    globus_libc_free(buf);

    if(result != GLOBUS_SUCCESS)
    {
        error=globus_error_get(result);
        goto error_auth_destroy;
    }

    if(cc_handle->auth_requirements & 
       GLOBUS_FTP_CONTROL_AUTH_REQ_NONE)
    {
        callback=cc_handle->auth_cb;
        callback_arg=cc_handle->auth_cb_arg;

        globus_mutex_lock(&(cc_handle->mutex));
        {
            cc_handle->auth_cb=GLOBUS_NULL;
            cc_handle->auth_cb_arg=GLOBUS_NULL;
        }
        globus_mutex_unlock(&(cc_handle->mutex));

        (callback)(callback_arg,
                   c_handle,
                   GLOBUS_NULL,
                   &(cc_handle->auth_info));

        globus_mutex_lock(&(cc_handle->mutex));
        {
            cc_handle->cb_count--;

            if(!cc_handle->cb_count &&
               cc_handle->cc_state == GLOBUS_FTP_CONTROL_CLOSING) 
            { 
                call_close_cb = GLOBUS_TRUE; 
            } 
        }
        globus_mutex_unlock(&(cc_handle->mutex));

        if(call_close_cb == GLOBUS_TRUE) 
        { 
            globus_i_ftp_control_call_close_cb(c_handle); 
        } 

        return;
    }

    /* call register_read with 0 byte minimum because we may already
     * have read the command
     */

    rc=globus_io_register_read(handle,
                               cc_handle->read_buffer,
                               GLOBUS_FTP_CONTROL_READ_BUFFER_SIZE,
                               0,
                               globus_l_ftp_control_auth_read_cb,
                               arg);

    if(rc != GLOBUS_SUCCESS)
    {
        error=globus_error_get(rc);
        goto error_auth_destroy;
    }
    
    return;

error_auth_destroy:
    rc = globus_i_ftp_control_auth_info_destroy(
        &(cc_handle->auth_info));
    globus_assert(rc == GLOBUS_SUCCESS);

    (cc_handle->auth_cb)(cc_handle->auth_cb_arg,
                         c_handle,
                         error,
                         GLOBUS_NULL);

    globus_object_free(error);

    globus_mutex_lock(&(cc_handle->mutex));
    {
        cc_handle->cb_count--;

        if(!cc_handle->cb_count &&
           cc_handle->cc_state == GLOBUS_FTP_CONTROL_CLOSING) 
        { 
            call_close_cb = GLOBUS_TRUE; 
        } 
    }
    globus_mutex_unlock(&(cc_handle->mutex));
    
    if(call_close_cb == GLOBUS_TRUE) 
    { 
        globus_i_ftp_control_call_close_cb(c_handle); 
    } 

    return;
}

#ifdef GLOBUS_INTERNAL_DOC

/**
 * Internal callback for the globus_io_register_read function.    
 * 
 * This is an internal callback used as part of the
 * globus_ftp_control_authenticate function. It checks the result of
 * the read (which was used to read commands from the client), and
 * if a full command was received, parses it and uses it as part of the
 * specified authentication process. If authentication is complete the
 * user callback is called and the function returns. If authentication
 * is not complete a reply to the received command is sent to then
 * client using the auth_write callbacks which will continue to read
 * commands. If no full command was received a new register_read is
 * called. 
 *
 * @param arg
 *        The callback argument, which in this case is the control
 *        handle.
 * @param handle
 *        The globus_io handle for the connection. In practice this
 *        represents the socket fd for the connection.
 * @param result
 *        The result of the accept operation 
 *
 * @return void
 *
 * @note If a error is detected in this function the user callback is
 *       called with an appropriate error object or ftp response and
 *       the function returns. 
 */

#endif

static void 
globus_l_ftp_control_auth_read_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result,
    globus_byte_t *                           buf, 
    globus_size_t                             nbytes)
{
    globus_ftp_cc_handle_t *                  cc_handle;
    globus_ftp_control_handle_t *             c_handle;
    globus_byte_t *                           new_buf;
    globus_object_t *                         error;
    globus_result_t                           rc;
    globus_ftp_control_command_t              command;
    globus_bool_t                             call_close_cb = GLOBUS_FALSE;
    int                                       i;
    int                                       j;
    int                                       length;
    void *                                    callback_arg;
    globus_ftp_control_auth_callback_t        callback;
    OM_uint32                                 maj_stat;
    OM_uint32                                 min_stat;
    OM_uint32                                 ret_flags=0;
    gss_buffer_desc                           recv_tok=GSS_C_EMPTY_BUFFER;
    gss_buffer_desc                           send_tok=GSS_C_EMPTY_BUFFER;
    gss_buffer_desc                           subject_buf=GSS_C_EMPTY_BUFFER;
    gss_OID                                   mech_type;
    char *                                    reply = NULL;
    char *                                    encoded_reply;

    c_handle=(globus_ftp_control_handle_t *) arg;
    cc_handle=&(c_handle->cc_handle);
    callback=cc_handle->auth_cb;
    callback_arg=cc_handle->auth_cb_arg;

    if(result != GLOBUS_SUCCESS)
    {
        error=globus_error_get(result);
        goto error_auth_destroy;
    }
   
    cc_handle->bytes_read += nbytes;

    for(i = 1;i<cc_handle->bytes_read;i++)
    {
        if(cc_handle->read_buffer[i-1] == '\r' &&
           cc_handle->read_buffer[i] == '\n')
        {
            cc_handle->read_buffer[i-1]='\0';

            rc=globus_ftp_control_command_init(
                &command,
                (char *) cc_handle->read_buffer,
                &cc_handle->auth_info);

            if(rc != GLOBUS_SUCCESS)
            {
                error=globus_error_get(rc);
                goto error_auth_destroy;
            }
            
            for(j=i+1;j<cc_handle->bytes_read;j++)
            {
                cc_handle->read_buffer[j-(i+1)]=
                    cc_handle->read_buffer[j];
            }
            
            cc_handle->bytes_read -= (i+1);
            
            switch(command.code)
            {
            case GLOBUS_FTP_CONTROL_COMMAND_AUTH:
                if(command.auth.type != 
                   GLOBUS_FTP_CONTROL_AUTH_GSSAPI)
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: Authentication mechanism not supported"));
                    goto error_cmd_destroy;
                }
                
                if( !(cc_handle->auth_requirements & 
                      GLOBUS_FTP_CONTROL_AUTH_REQ_GSSAPI))
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: GSSAPI authentication not allowed"));
                    goto error_cmd_destroy;
                }
                
                if(cc_handle->auth_info.prev_cmd != 
                   GLOBUS_FTP_CONTROL_COMMAND_UNKNOWN)
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: AUTH must be the first command in the authentication sequence"));
                    goto error_cmd_destroy;
                }
                
                maj_stat=globus_gss_assist_acquire_cred(
                    &min_stat,
                    GSS_C_ACCEPT,
                    &(cc_handle->auth_info.credential_handle)); 

                if(maj_stat != GSS_S_COMPLETE)
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: globus_gss_assist_acquire_cred failed"));
                    goto error_cmd_destroy;  
                }
                else
                {
                    cc_handle->auth_info.locally_acquired_credential = GLOBUS_TRUE;
                }

                cc_handle->auth_info.prev_cmd=command.code;
                
                reply=globus_libc_strdup(
                    globus_i_ftp_server_334_reply);
                
                if(reply == GLOBUS_NULL)
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: strdup failed"));
                    goto error_cmd_destroy;   
                }

                rc=globus_io_register_write(
                    handle,
                    (globus_byte_t *) reply,
                    (globus_size_t) strlen(reply),
                    globus_l_ftp_control_auth_write_cb,
                    arg);
                
                if(rc != GLOBUS_SUCCESS)
                {
                    globus_libc_free(reply);
                    error=globus_error_get(rc);
                    goto error_cmd_destroy;
                }
                
                break;
                
            case GLOBUS_FTP_CONTROL_COMMAND_ADAT:
                if((cc_handle->auth_info.prev_cmd != 
                    GLOBUS_FTP_CONTROL_COMMAND_AUTH) &&
                   (cc_handle->auth_info.prev_cmd != 
                    GLOBUS_FTP_CONTROL_COMMAND_ADAT))
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: ADAT must be preceded by either AUTH or ADAT"));
                    goto error_cmd_destroy;
                }

                cc_handle->auth_info.prev_cmd=command.code;
                
                recv_tok.value= 
                    globus_libc_malloc(
                        (strlen(command.adat.string_arg) + 3) * 6 / 8);

                if(recv_tok.value == GLOBUS_NULL)
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: malloc failed"));
                    goto error_cmd_destroy;
                }
                
                rc=globus_i_ftp_control_radix_decode(
                    (globus_byte_t *) command.adat.string_arg, 
                    recv_tok.value, 
                    &length);
                if(rc != GLOBUS_SUCCESS)
                {
                    globus_libc_free(recv_tok.value);
                    error=globus_error_get(rc);
                    goto error_cmd_destroy;
                }
                
                recv_tok.length = length;

                if(cc_handle->auth_info.encrypt)
                {
                    ret_flags |= GSS_C_CONF_FLAG;
                }
                maj_stat = gss_accept_sec_context(
                    &min_stat,
                    /* context_handle */
                    &(cc_handle->auth_info.auth_gssapi_context),
                    /* verifier_cred_handle */
                    cc_handle->auth_info.credential_handle, 
                    /* input_token */
                    &recv_tok, 
                    /* channel bindings */
                    GSS_C_NO_CHANNEL_BINDINGS,
                    /* src_name */
                    &cc_handle->auth_info.target_name,
                    /* mech_type */
                    &mech_type,
                    /* output_token */
                    &send_tok,
                    &ret_flags,
                    /* ignore time_rec */
                    GLOBUS_NULL, 
                    &(cc_handle->auth_info.delegated_credential_handle)
                    );
                
                globus_libc_free(recv_tok.value);

                switch(maj_stat)
                {

                case GSS_S_COMPLETE:
  
                    cc_handle->use_auth = GLOBUS_TRUE;
                    cc_handle->auth_info.authenticated = GLOBUS_TRUE;

                    if(ret_flags & GSS_C_CONF_FLAG)
                    {
                        cc_handle->auth_info.encrypt = GLOBUS_TRUE;
                    }
                    
                    maj_stat = gss_display_name(
                        &min_stat,
                        cc_handle->auth_info.target_name,
                        &subject_buf,
                        GLOBUS_NULL);

                    cc_handle->auth_info.auth_gssapi_subject =
                        malloc(sizeof(char)*
                                           (subject_buf.length + 1));
                    
                    if(cc_handle->auth_info.auth_gssapi_subject == GLOBUS_NULL)
                    {
                        gss_release_buffer(&min_stat, &subject_buf);
                        
                        error=globus_error_construct_string(
                            GLOBUS_FTP_CONTROL_MODULE,
                            GLOBUS_NULL,
                            _FCSL("globus_l_ftp_control_auth_read_cb: malloc failed"));
                        goto error_cmd_destroy;
                    }

                    memcpy(cc_handle->auth_info.auth_gssapi_subject,
                           subject_buf.value,
                           subject_buf.length);

                    cc_handle->auth_info.auth_gssapi_subject[
                        subject_buf.length] = '\0';
                    
                    gss_release_buffer(&min_stat, &subject_buf);

                    if(cc_handle->auth_requirements & 
                       GLOBUS_FTP_CONTROL_AUTH_REQ_USER)
                    {
                        if(send_tok.length == 0)
                        { 
                            reply=globus_libc_strdup(
                                globus_i_ftp_server_235_reply);
                            
                            if(reply == GLOBUS_NULL)
                            {
                                error=globus_error_construct_string(
                                    GLOBUS_FTP_CONTROL_MODULE,
                                    GLOBUS_NULL,
                                    _FCSL("globus_l_ftp_control_auth_read_cb: strdup failed"));
                                goto error_cmd_destroy;   
                            }
                        }
                        else
                        {
                            reply= (char *) globus_libc_malloc(
                                send_tok.length * 8 / 6 + 16);
                            
                            if(reply == GLOBUS_NULL)
                            {
                                gss_release_buffer(&min_stat, &send_tok);
                                error=globus_error_construct_string(
                                    GLOBUS_FTP_CONTROL_MODULE,
                                    GLOBUS_NULL,
                                    _FCSL("globus_l_ftp_control_auth_read_cb: malloc failed"));
                                goto error_cmd_destroy;
                            }
                    
                            strcpy(reply,"235 ADAT=");
                    
                            length=send_tok.length;
                            
                            rc = globus_i_ftp_control_radix_encode(send_tok.value, 
                                                                   (globus_byte_t *) &(reply[9]), 
                                                                   &length);
                            gss_release_buffer(&min_stat, &send_tok);
                            
                            if(rc != GLOBUS_SUCCESS)
                            {
                                globus_libc_free(reply);
                                error=globus_error_get(rc);
                                goto error_cmd_destroy;
                            }

                            reply[length+9]='\r';
                            reply[length+10]='\n';
                        }
                        
                        rc=globus_io_register_write(
                            handle,
                            (globus_byte_t *) reply,
                            (globus_size_t) strlen(reply),
                            globus_l_ftp_control_auth_write_cb,
                            arg);
                        
                        if(rc != GLOBUS_SUCCESS)
                        {
                            globus_libc_free(reply);
                            error=globus_error_get(rc);
                            goto error_cmd_destroy;
                        }
                    }
                    else
                    {
                        globus_mutex_lock(&(cc_handle->mutex));
                        {
                            cc_handle->auth_cb=GLOBUS_NULL;
                            cc_handle->auth_cb_arg=GLOBUS_NULL;
                        }
                        globus_mutex_unlock(&(cc_handle->mutex));
                        
                        (callback)(callback_arg,
                                   c_handle,
                                   GLOBUS_NULL,
                                   &(cc_handle->auth_info));

                        globus_mutex_lock(&(cc_handle->mutex));
                        {
                            cc_handle->cb_count--;
                            
                            if(!cc_handle->cb_count &&
                               cc_handle->cc_state == 
                               GLOBUS_FTP_CONTROL_CLOSING) 
                            { 
                                call_close_cb = GLOBUS_TRUE; 
                            } 
                        }
                        globus_mutex_unlock(&(cc_handle->mutex));
                        
                        if(call_close_cb == GLOBUS_TRUE) 
                        { 
                            globus_i_ftp_control_call_close_cb(c_handle); 
                        } 
                    }
                    break;
                case GSS_S_CONTINUE_NEEDED:
                    reply= (char *) globus_libc_malloc(
                        send_tok.length * 8 / 6 + 16);
                    
                    if(reply == GLOBUS_NULL)
                    {
                        gss_release_buffer(&min_stat, &send_tok);
                        error=globus_error_construct_string(
                            GLOBUS_FTP_CONTROL_MODULE,
                            GLOBUS_NULL,
                            _FCSL("globus_l_ftp_control_auth_read_cb: malloc failed"));
                        goto error_cmd_destroy;
                    }
                    
                    strcpy(reply,"335 ADAT=");
                    
                    length=send_tok.length;
                    
                    rc = globus_i_ftp_control_radix_encode(send_tok.value, 
                                                           (globus_byte_t *) &(reply[9]), 
                                                           &length);
                    gss_release_buffer(&min_stat, &send_tok);

                    if(rc != GLOBUS_SUCCESS)
                    {
                        globus_libc_free(reply);
                        error=globus_error_get(rc);
                        goto error_cmd_destroy;
                    }

                    reply[length+9]='\r';
                    reply[length+10]='\n';
                    
                    rc=globus_io_register_write(
                        handle,
                        (globus_byte_t *) reply,
                        (globus_size_t) length+11,
                        globus_l_ftp_control_auth_write_cb,
                        arg);

                    if(rc != GLOBUS_SUCCESS)
                    {
                        globus_libc_free(reply);
                        error=globus_error_get(rc);
                        goto error_cmd_destroy;
                    }
                    break;
                default:
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: gss_accept_sec_context failed"));
                    goto error_cmd_destroy;
                }
                break;
            case GLOBUS_FTP_CONTROL_COMMAND_USER:
                if((cc_handle->auth_info.prev_cmd != 
                    GLOBUS_FTP_CONTROL_COMMAND_ADAT) &&
                   (cc_handle->auth_info.prev_cmd != 
                    GLOBUS_FTP_CONTROL_COMMAND_UNKNOWN))
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: USER must either be preceded by ADAT or be the first command"));
                    goto error_cmd_destroy;
                }
                
                cc_handle->auth_info.prev_cmd=command.code;

                if((cc_handle->auth_requirements & 
                    GLOBUS_FTP_CONTROL_AUTH_REQ_GSSAPI) &&
                   (cc_handle->use_auth == GLOBUS_FALSE))
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: GSSAPI authentication required"));
                    goto error_cmd_destroy; 
                }
                
                cc_handle->auth_info.user=globus_libc_strdup(
                    command.user.string_arg);

                if(cc_handle->auth_info.user == GLOBUS_NULL)
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: strdup failed"));
                    goto error_cmd_destroy;
                }
                
                if(cc_handle->auth_requirements & 
                   GLOBUS_FTP_CONTROL_AUTH_REQ_USER)
                {

                    if(!(cc_handle->auth_requirements &
                         (GLOBUS_FTP_CONTROL_AUTH_REQ_PASS |
                          GLOBUS_FTP_CONTROL_AUTH_REQ_ACCT)))
                    {
                        globus_mutex_lock(&(cc_handle->mutex));
                        {
                            cc_handle->auth_cb=GLOBUS_NULL;
                            cc_handle->auth_cb_arg=GLOBUS_NULL;
                        }
                        globus_mutex_unlock(&(cc_handle->mutex));
                        
                        (callback)(callback_arg,
                                   c_handle,
                                   GLOBUS_NULL,
                                   &(cc_handle->auth_info));

                        globus_mutex_lock(&(cc_handle->mutex));
                        {
                            cc_handle->cb_count--;

                            if(!cc_handle->cb_count &&
                               cc_handle->cc_state == 
                               GLOBUS_FTP_CONTROL_CLOSING) 
                            { 
                                call_close_cb = GLOBUS_TRUE; 
                            } 
                        }
                        globus_mutex_unlock(&(cc_handle->mutex));

                        if(call_close_cb == GLOBUS_TRUE) 
                        { 
                            globus_i_ftp_control_call_close_cb(c_handle); 
                        } 
                        
                        break;
                    }

                    if(cc_handle->auth_requirements &
                       GLOBUS_FTP_CONTROL_AUTH_REQ_PASS)
                    {
                        reply= globus_libc_strdup(
                            globus_i_ftp_server_331_reply);
                    }

                    if(cc_handle->auth_requirements &
                       GLOBUS_FTP_CONTROL_AUTH_REQ_ACCT)
                    {
                        reply= globus_libc_strdup(
                            globus_i_ftp_server_332_reply);
                    }
                    
                    
                    if(reply == GLOBUS_NULL)
                    {
                        error=globus_error_construct_string(
                            GLOBUS_FTP_CONTROL_MODULE,
                            GLOBUS_NULL,
                            _FCSL("globus_l_ftp_control_auth_read_cb: strdup failed"));
                        goto error_cmd_destroy;
                    }
                    
                    if(cc_handle->auth_info.authenticated == GLOBUS_TRUE)
                    {
                        rc=globus_i_ftp_control_encode_reply(
                            reply,
                            &encoded_reply,
                            &(cc_handle->auth_info));
                        
                        globus_libc_free(reply);

                        if(rc != GLOBUS_SUCCESS)
                        {
                            error=globus_error_get(rc);
                            goto error_cmd_destroy;
                        }
                        
                        reply=encoded_reply;
                    }
                    

                    rc=globus_io_register_write(
                        handle,
                        (globus_byte_t *) reply,
                        (globus_size_t) strlen(reply),
                        globus_l_ftp_control_auth_write_cb,
                        arg);
                    
                    if(rc != GLOBUS_SUCCESS)
                    {
                        globus_libc_free(reply);
                        error=globus_error_get(rc);
                        goto error_cmd_destroy;
                    }
                    
                }
                else
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: USER command not allowed"));
                    goto error_cmd_destroy;
                }
                break;
            case GLOBUS_FTP_CONTROL_COMMAND_PASS:

                if(cc_handle->auth_info.prev_cmd != 
                   GLOBUS_FTP_CONTROL_COMMAND_USER)
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: PASS must be preceded by USER"));
                    goto error_cmd_destroy;
                }

                cc_handle->auth_info.prev_cmd=command.code;

                cc_handle->auth_info.password=globus_libc_strdup(
                    command.pass.string_arg);

                if(cc_handle->auth_info.password == GLOBUS_NULL)
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: No password given"));
                    goto error_cmd_destroy;
                }
                
                if(!(cc_handle->auth_requirements & 
                     GLOBUS_FTP_CONTROL_AUTH_REQ_ACCT))
                {
                    globus_mutex_lock(&(cc_handle->mutex));
                    {
                        cc_handle->auth_cb=GLOBUS_NULL;
                        cc_handle->auth_cb_arg=GLOBUS_NULL;
                    }
                    globus_mutex_unlock(&(cc_handle->mutex));
                    
                    (callback)(callback_arg,
                               c_handle,
                               GLOBUS_NULL,
                               &(cc_handle->auth_info));
                    globus_mutex_lock(&(cc_handle->mutex));
                    {
                        cc_handle->cb_count--;
                        if(!cc_handle->cb_count &&
                           cc_handle->cc_state == GLOBUS_FTP_CONTROL_CLOSING) 
                        { 
                            call_close_cb = GLOBUS_TRUE; 
                        } 
                    }
                    globus_mutex_unlock(&(cc_handle->mutex));
                    
                    if(call_close_cb == GLOBUS_TRUE) 
                    { 
                        globus_i_ftp_control_call_close_cb(c_handle); 
                    } 
                }
                else
                {
                    reply=globus_libc_strdup(
                        globus_i_ftp_server_332_reply);
                    
                    if(reply == GLOBUS_NULL)
                    {
                        error=globus_error_construct_string(
                            GLOBUS_FTP_CONTROL_MODULE,
                            GLOBUS_NULL,
                            _FCSL("globus_l_ftp_control_auth_read_cb: strdup failed"));
                        goto error_cmd_destroy;
                    }
                    
                    if(cc_handle->auth_info.authenticated == GLOBUS_TRUE)
                    {
                        rc=globus_i_ftp_control_encode_reply(
                            reply,
                            &encoded_reply,
                            &(cc_handle->auth_info));
                        
                        globus_libc_free(reply);
                        
                        if(rc != GLOBUS_SUCCESS)
                        {
                            error=globus_error_get(rc);
                            goto error_cmd_destroy;
                        }
                        
                        reply=encoded_reply;
                    }
                    
                    rc=globus_io_register_write(
                        handle,
                        (globus_byte_t *) reply,
                        (globus_size_t) strlen(reply),
                        globus_l_ftp_control_auth_write_cb,
                        arg);

                    if(rc != GLOBUS_SUCCESS)
                    {
                        globus_libc_free(reply);
                        error=globus_error_get(rc);
                        goto error_cmd_destroy;
                    }
                }
                break;
            case GLOBUS_FTP_CONTROL_COMMAND_ACCT:
                if(((cc_handle->auth_requirements & 
                     GLOBUS_FTP_CONTROL_AUTH_REQ_PASS) &&
                    (cc_handle->auth_info.prev_cmd != 
                     GLOBUS_FTP_CONTROL_COMMAND_PASS)) ||
                   (cc_handle->auth_info.prev_cmd != 
                    GLOBUS_FTP_CONTROL_COMMAND_USER))
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: ACCT must be preceded by either USER or PASS"));
                    goto error_cmd_destroy;
                }

                cc_handle->auth_info.account=globus_libc_strdup(
                    command.acct.string_arg);

                if(cc_handle->auth_info.account == GLOBUS_NULL)
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: no account given"));
                    goto error_cmd_destroy;
                }
                
                if(cc_handle->auth_requirements & 
                   GLOBUS_FTP_CONTROL_AUTH_REQ_ACCT)
                {
                    globus_mutex_lock(&(cc_handle->mutex));
                    {
                        cc_handle->auth_cb=GLOBUS_NULL;
                        cc_handle->auth_cb_arg=GLOBUS_NULL;
                    }
                    globus_mutex_unlock(&(cc_handle->mutex));
                    
                    (callback)(callback_arg,
                               c_handle,
                               GLOBUS_NULL,
                               &(cc_handle->auth_info));
                    globus_mutex_lock(&(cc_handle->mutex));
                    {
                        cc_handle->cb_count--;

                        if(!cc_handle->cb_count &&
                           cc_handle->cc_state == GLOBUS_FTP_CONTROL_CLOSING) 
                        { 
                            call_close_cb = GLOBUS_TRUE; 
                        } 

                    }
                    globus_mutex_unlock(&(cc_handle->mutex));

                    if(call_close_cb == GLOBUS_TRUE) 
                    { 
                        globus_i_ftp_control_call_close_cb(c_handle); 
                    } 
                }
                else
                {
                    error=globus_error_construct_string(
                        GLOBUS_FTP_CONTROL_MODULE,
                        GLOBUS_NULL,
                        _FCSL("globus_l_ftp_control_auth_read_cb: ACCT not allowed"));
                    goto error_cmd_destroy;
                }
                break;
            default:
                error=globus_error_construct_string(
                    GLOBUS_FTP_CONTROL_MODULE,
                    GLOBUS_NULL,
                    _FCSL("globus_l_ftp_control_auth_read_cb: Command not part of authentication process"));
                goto error_cmd_destroy;
            }
            
            globus_ftp_control_command_destroy(&command);
            return;
        }
    }
    
    if(cc_handle->bytes_read == cc_handle->read_buffer_size)
    {
        new_buf= (globus_byte_t *) globus_libc_malloc(
            cc_handle->read_buffer_size +
            GLOBUS_I_FTP_CONTROL_BUF_INCR);
        
        if(new_buf == GLOBUS_NULL)
        {
            error=globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_l_ftp_control_auth_read_cb: malloc failed"));
            goto error_auth_destroy;
        }
        
        memcpy(new_buf,
               cc_handle->read_buffer,
               cc_handle->bytes_read);
        
        cc_handle->read_buffer_size += GLOBUS_I_FTP_CONTROL_BUF_INCR;
        globus_libc_free(cc_handle->read_buffer);
        cc_handle->read_buffer=new_buf;
    }
    
    rc=globus_io_register_read(&(cc_handle->io_handle),
                               &(cc_handle->read_buffer[
                                     cc_handle->bytes_read]),
                               cc_handle->read_buffer_size - 
                               cc_handle->bytes_read,
                               1,
                               globus_l_ftp_control_auth_read_cb,
                               arg);
    
    if(rc != GLOBUS_SUCCESS)
    {
        error=globus_error_get(rc);
        goto error_auth_destroy;
    }
    
    return;

error_cmd_destroy:
    globus_ftp_control_command_destroy(&command);
error_auth_destroy:
    rc = globus_i_ftp_control_auth_info_destroy(
        &(cc_handle->auth_info));
    globus_assert(rc == GLOBUS_SUCCESS);
    
    (cc_handle->auth_cb)(cc_handle->auth_cb_arg,
                         c_handle,
                         error,
                         GLOBUS_NULL);
    globus_object_free(error);

    globus_mutex_lock(&(cc_handle->mutex));
    {
        cc_handle->cb_count--;
        if(!cc_handle->cb_count &&
           cc_handle->cc_state == GLOBUS_FTP_CONTROL_CLOSING) 
        { 
            call_close_cb = GLOBUS_TRUE; 
        } 
    }
    globus_mutex_unlock(&(cc_handle->mutex));

    if(call_close_cb == GLOBUS_TRUE) 
    { 
        globus_i_ftp_control_call_close_cb(c_handle); 
    } 
 
    return;
}

/**
 * @brief Read GridFTP commands
 * @ingroup globus_ftp_control_server
 * @details
 * This function begins reading control commands on a 
 * globus_ftp_control_handle_t.  When a command is read
 * the callback function is called.
 *
 *  @param handle
 *         The control connection handle that commands will be read
 *         from.  Prior to calling this the function 
 *         globus_ftp_control_handle_t must be populated via a
 *         call to globus_ftp_control_accept().
 *  @param callback
 *         The user callback that will be called when commands are read.
 *  @param callback_arg
 *         The user argument passed to the callback.
 */

globus_result_t
globus_ftp_control_read_commands(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_command_callback_t       callback,
    void *                                      callback_arg)
{
    globus_result_t                           rc;
    globus_bool_t                             call_close_cb = GLOBUS_FALSE;

    if(handle == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_read_commands: handle argument is NULL"))
            );
    }

    globus_mutex_lock(&(handle->cc_handle.mutex));
    {
        if(handle->cc_handle.command_cb == GLOBUS_NULL  &&
           handle->cc_handle.cc_state == GLOBUS_FTP_CONTROL_CONNECTED &&
           handle->cc_handle.auth_cb == GLOBUS_NULL )
        {
            handle->cc_handle.command_cb=callback;
            handle->cc_handle.command_cb_arg=callback_arg;
            handle->cc_handle.cb_count++;
        }
        else
        {
            globus_mutex_unlock(&(handle->cc_handle.mutex));
            return globus_error_put(
                globus_error_construct_string(
                    GLOBUS_FTP_CONTROL_MODULE,
                    GLOBUS_NULL,
                    _FCSL("globus_ftp_control_read_commands: handle is not connected or other operation is in progress"))
                );
        }
    }
    globus_mutex_unlock(&(handle->cc_handle.mutex));

    
    rc=globus_io_register_read(&(handle->cc_handle.io_handle),
                               &(handle->cc_handle.read_buffer[
                                     handle->cc_handle.bytes_read]),
                               handle->cc_handle.read_buffer_size - 
                               handle->cc_handle.bytes_read,
                               0,
                               globus_l_ftp_control_read_command_cb,
                               (void *) handle);

    if(rc != GLOBUS_SUCCESS)
    {
        globus_i_ftp_control_auth_info_destroy(
            &(handle->cc_handle.auth_info));
        
        globus_mutex_lock(&(handle->cc_handle.mutex));
        {
            handle->cc_handle.cb_count--;
            if(!handle->cc_handle.cb_count &&
               handle->cc_handle.cc_state == GLOBUS_FTP_CONTROL_CLOSING) 
            { 
                call_close_cb = GLOBUS_TRUE; 
            } 
        }
        globus_mutex_unlock(&(handle->cc_handle.mutex));

        if(call_close_cb == GLOBUS_TRUE) 
        { 
            globus_i_ftp_control_call_close_cb(handle); 
        } 

        return rc;
    }
    
    return GLOBUS_SUCCESS;
}

#ifdef GLOBUS_INTERNAL_DOC

/**
 * Internal callback for the globus_io_register_read function.    
 * 
 * This is an internal callback used as part of the
 * globus_ftp_control_read_commands function. It checks the result of 
 * the read (which was used to read commands from the client), and
 * if a full command was received, parses it and returns the resulting
 * command structure to the user through the user callback. If no full
 * command was received a new register_read is called. 
 *
 * @param arg
 *        The callback argument, which in this case is the control
 *        handle.
 * @param handle
 *        The globus_io handle for the connection. In practice this
 *        represents the socket fd for the connection.
 * @param result
 *        The result of the accept operation 
 *
 * @return void
 *
 * @note If a error is detected in this function the user callback is
 *       called with an appropriate error object or ftp response and
 *       the function returns. 
 */

#endif

static void
globus_l_ftp_control_read_command_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result,
    globus_byte_t *                           buf, 
    globus_size_t                             nbytes)
{
    globus_ftp_cc_handle_t *                  cc_handle;
    globus_ftp_control_handle_t *             c_handle;
    globus_byte_t *                           new_buf;
    globus_object_t *                         error;
    globus_result_t                           rc;
    globus_bool_t                             call_close_cb = GLOBUS_FALSE;
    globus_ftp_control_command_t              command;
    globus_ftp_control_command_code_t         code =
        GLOBUS_FTP_CONTROL_COMMAND_UNKNOWN;
    int                                       last;
    int                                       i;

    c_handle=(globus_ftp_control_handle_t *) arg;
    cc_handle=&(c_handle->cc_handle);

    if(result != GLOBUS_SUCCESS)
    {
        error=globus_error_get(result);
        goto error_auth_destroy;
    }
    
    cc_handle->bytes_read += nbytes;

    last=0;
    
    for(i = 1;i < cc_handle->bytes_read; i++)
    {
        if(cc_handle->read_buffer[i-1] == '\r' &&
           cc_handle->read_buffer[i] == '\n')
        {
            cc_handle->read_buffer[i-1]='\0';

            rc=globus_ftp_control_command_init(
                &command,
                (char *) &(cc_handle->read_buffer[last]),
                &cc_handle->auth_info);

            if(rc != GLOBUS_SUCCESS)
            {
                error=globus_error_get(rc);
                goto error_auth_destroy;
            }
            
            (cc_handle->command_cb)(cc_handle->command_cb_arg,
                                    c_handle,
                                    GLOBUS_NULL,
                                    &command);
            code=command.code;
            globus_ftp_control_command_destroy(&command);
            last=i+1;
        }
    }
    
    if(last != 0)
    {
        for(i=last;i<cc_handle->bytes_read;i++)
        {
            cc_handle->read_buffer[i-last]=
                cc_handle->read_buffer[i];
        }
        
        cc_handle->bytes_read -= last;
    }
    else
    {
        if(cc_handle->bytes_read == cc_handle->read_buffer_size)
        {
            new_buf= (globus_byte_t *) globus_libc_malloc(
                cc_handle->read_buffer_size +
                GLOBUS_I_FTP_CONTROL_BUF_INCR);

            if(new_buf == GLOBUS_NULL)
            {
                error=globus_error_construct_string(
                    GLOBUS_FTP_CONTROL_MODULE,
                    GLOBUS_NULL,
                    _FCSL("globus_l_ftp_control_read_command_cb: malloc failed"));
                goto error_auth_destroy;
            }
            
            memcpy(new_buf,
                   cc_handle->read_buffer,
                   cc_handle->bytes_read);

            cc_handle->read_buffer_size += GLOBUS_I_FTP_CONTROL_BUF_INCR;
            globus_libc_free(cc_handle->read_buffer);
            cc_handle->read_buffer=new_buf;
        }
    }


    globus_mutex_lock(&(cc_handle->mutex));
    {
        if(cc_handle->cb_count == 1 &&
           cc_handle->cc_state == GLOBUS_FTP_CONTROL_CLOSING) 
        {
            cc_handle->cb_count--;
            call_close_cb = GLOBUS_TRUE; 
        }
	else if(code == GLOBUS_FTP_CONTROL_COMMAND_QUIT)
	{
            cc_handle->cb_count--;
	}
    }
    globus_mutex_unlock(&(cc_handle->mutex));
    
    if(call_close_cb == GLOBUS_TRUE) 
    { 
        globus_i_ftp_control_call_close_cb(c_handle);
        return;
    } 
    
    if(code != GLOBUS_FTP_CONTROL_COMMAND_QUIT)
    {
        rc=globus_io_register_read(&(cc_handle->io_handle),
                                   &(cc_handle->read_buffer[
                                         cc_handle->bytes_read]),
                                   cc_handle->read_buffer_size - 
                                   cc_handle->bytes_read,
                                   1,
                                   globus_l_ftp_control_read_command_cb,
                                   arg);
        
        if(rc != GLOBUS_SUCCESS)
        {
            error=globus_error_get(rc);
            goto error_auth_destroy;
        }
    }
    
    return;

error_auth_destroy:
    rc = globus_i_ftp_control_auth_info_destroy(
        &(cc_handle->auth_info));
    globus_assert(rc == GLOBUS_SUCCESS);
    
    (cc_handle->command_cb)(cc_handle->command_cb_arg,
                            c_handle,
                            error,
                            GLOBUS_NULL);
    globus_object_free(error);
    
    globus_mutex_lock(&(cc_handle->mutex));
    {
        cc_handle->cb_count--;

        if(!cc_handle->cb_count &&
           cc_handle->cc_state == GLOBUS_FTP_CONTROL_CLOSING) 
        { 
            call_close_cb = GLOBUS_TRUE; 
        } 
    }
    globus_mutex_unlock(&(cc_handle->mutex));
    
    if(call_close_cb == GLOBUS_TRUE) 
    { 
        globus_i_ftp_control_call_close_cb(c_handle); 
    } 

    return;
}


/**
 * @brief Send a GridFTP response
 * @ingroup globus_ftp_control_server
 * @details
 * This function sends a GridFTP formatted response to the client.  
 * When a command callback is received the user calls this function 
 * to respond to the clients request.
 *
 *  @param handle
 *         The control connection to send the response across.
 *  @param respspec
 *         A formatted string representing the users response.
 *  @param callback
 *         The user callback that will be called when the response has
 *         been sent.
 *  @param callback_arg
 *         The user argument passed to the callback.
 */

globus_result_t
globus_ftp_control_send_response(
    globus_ftp_control_handle_t *               handle,
    const char *                                respspec,
    globus_ftp_control_callback_t               callback,
    void *                                      callback_arg,
    ...)
{
    globus_ftp_control_rw_queue_element_t *     element;
    globus_bool_t                               queue_empty;
    globus_result_t                             rc;
    globus_result_t                             result;
    globus_byte_t *                             buf;
    globus_byte_t *                             encoded_buf;
    globus_bool_t                               call_close_cb = GLOBUS_FALSE;
    va_list                                     ap;
    int                                         arglength;


    if(handle == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_send_response: handle argument is NULL"))
            );
    }

    
    va_start(ap, callback_arg);
    
    arglength=globus_libc_vfprintf(globus_i_ftp_control_devnull,
                                   respspec,
                                   ap);

    va_end(ap);
    
    if(arglength < 1)
    {
        result=globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_send_response: Unable to determine total length of response string"))
            );
        goto return_error;
    }

    buf=(globus_byte_t *) globus_libc_malloc(sizeof(globus_byte_t)*
                                             (arglength+1));

    if(buf == GLOBUS_NULL)
    {
        result=globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_send_response: malloc failed"))
            );
        goto return_error;
    }

    va_start(ap, callback_arg);

    if(globus_libc_vsprintf((char *) buf, respspec,ap) < arglength)
    {
        globus_libc_free(buf);
        result=globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_send_response: Response string construction failed"))
        );
        va_end(ap);
        goto return_error;
    }

    va_end(ap);

    if(handle->cc_handle.auth_info.authenticated == GLOBUS_TRUE)
    {
        rc=globus_i_ftp_control_encode_reply((char *) buf,(char **) (void *) &encoded_buf,
                                             &(handle->cc_handle.auth_info));

        globus_libc_free(buf);

        if(rc != GLOBUS_SUCCESS)
        {
            result=rc;
            goto return_error;
        }
        
        buf=encoded_buf;
    }

    element = (globus_ftp_control_rw_queue_element_t *)
        globus_libc_malloc(sizeof(globus_ftp_control_rw_queue_element_t));
    
    if(element == GLOBUS_NULL)
    {
        result=globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_send_response: malloc failed"))
            );
        globus_libc_free(buf);
        goto return_error;
    }

    element->callback = GLOBUS_NULL;
    element->send_response_cb = callback;
    element->arg = callback_arg;
    element->write_flags = 0;
    element->write_buf = buf;
    element->write_callback = globus_l_ftp_control_send_response_cb;

    globus_mutex_lock(&(handle->cc_handle.mutex));
    {
        if(handle->cc_handle.cc_state == GLOBUS_FTP_CONTROL_CONNECTED &&
           handle->cc_handle.auth_cb == GLOBUS_NULL)
        {
            queue_empty=globus_fifo_empty(&handle->cc_handle.writers);
            globus_fifo_enqueue(&handle->cc_handle.writers,
                                element);
            handle->cc_handle.cb_count++;
        }
        else
        {
            globus_mutex_unlock(&(handle->cc_handle.mutex));
            globus_libc_free(buf);
            globus_libc_free(element);
            result = globus_error_put(
                globus_error_construct_string(
                    GLOBUS_FTP_CONTROL_MODULE,
                    GLOBUS_NULL,
                    _FCSL("globus_ftp_control_send_response: handle is not connected/authenticated"))
                );
            goto return_error;
        }
    }
    globus_mutex_unlock(&(handle->cc_handle.mutex));


    if(queue_empty == GLOBUS_TRUE)
    {
        rc = globus_io_register_write(&(handle->cc_handle.io_handle),
                                      buf,
                                      (globus_size_t) strlen((char *) buf),
                                      globus_l_ftp_control_send_response_cb,
                                      (void *) handle);
    
        if(rc != GLOBUS_SUCCESS)
        {
            globus_mutex_lock(&(handle->cc_handle.mutex));
            {
                globus_fifo_dequeue(&handle->cc_handle.writers);
                handle->cc_handle.cb_count--;
                queue_empty=globus_fifo_empty(&handle->cc_handle.writers);

                if(!handle->cc_handle.cb_count &&
                   handle->cc_handle.cc_state == GLOBUS_FTP_CONTROL_CLOSING) 
                { 
                    call_close_cb = GLOBUS_TRUE; 
                } 
            }
            globus_mutex_unlock(&(handle->cc_handle.mutex));

            if(call_close_cb == GLOBUS_TRUE) 
            { 
                globus_i_ftp_control_call_close_cb(handle); 
            }

            globus_libc_free(buf);
            globus_libc_free(element);  

            if(queue_empty == GLOBUS_FALSE)
            {
                globus_i_ftp_control_write_next(handle);
            }
            
            result=rc;
            goto return_error;
        }
    }

    return GLOBUS_SUCCESS;

return_error:
    return result;
}

#ifdef GLOBUS_INTERNAL_DOC

/**
 * Internal callback for the globus_io_register_write function.    
 * 
 * This is an internal callback used as part of the
 * globus_ftp_control_send_response function. It checks the result of
 * the write (which was used to send a response to the client), and
 * calls the user callback
 *
 * @param arg
 *        The callback argument, which in this case is the control
 *        handle.
 * @param handle
 *        The globus_io handle for the connection. In practice this
 *        represents the socket fd for the connection.
 * @param result
 *        The result of the write operation 
 *
 * @return void
 *
 * @note If a error is detected in this function the user callback is
 *       called with an appropriate error object or ftp response and
 *       the function returns. 
 */

#endif

static void 
globus_l_ftp_control_send_response_cb(
    void *                                    arg, 
    globus_io_handle_t *                      handle,
    globus_result_t                           result,
    globus_byte_t *                           buf, 
    globus_size_t                             nbytes)
{
    globus_ftp_cc_handle_t *                  cc_handle;
    globus_ftp_control_handle_t *             c_handle;
    globus_object_t *                         error;
    globus_ftp_control_rw_queue_element_t *   element;
    globus_bool_t                             queue_empty;
    globus_bool_t                             call_close_cb = GLOBUS_FALSE;


    c_handle = (globus_ftp_control_handle_t *) arg;
    cc_handle = &(c_handle->cc_handle);

    globus_libc_free(buf);

    globus_mutex_lock(&(cc_handle->mutex));
    {
        element = (globus_ftp_control_rw_queue_element_t *)
            globus_fifo_dequeue(&cc_handle->writers);
        queue_empty=globus_fifo_empty(&cc_handle->writers);
    }
    globus_mutex_unlock(&(cc_handle->mutex));
    
    if(queue_empty == GLOBUS_FALSE)
    {
        globus_i_ftp_control_write_next(c_handle);
    }
    
    if(result != GLOBUS_SUCCESS)
    {
        error=globus_error_get(result);
        goto return_error;
    }

    (element->send_response_cb)((element->arg),
                                c_handle,
                                GLOBUS_NULL);

    globus_libc_free(element);
    
    globus_mutex_lock(&(cc_handle->mutex));
    {
        cc_handle->cb_count--;

        if(!cc_handle->cb_count &&
           cc_handle->cc_state == GLOBUS_FTP_CONTROL_CLOSING) 
        { 
            call_close_cb = GLOBUS_TRUE; 
        }
    }
    globus_mutex_unlock(&(cc_handle->mutex));
    
    if(call_close_cb == GLOBUS_TRUE) 
    { 
        globus_i_ftp_control_call_close_cb(c_handle); 
    } 

    return;
    
return_error:

    (element->send_response_cb)((element->arg),
                                c_handle,
                                error);
    globus_libc_free(element);
    globus_object_free(error);

    globus_mutex_lock(&(cc_handle->mutex));
    {
        cc_handle->cb_count--;
        if(!cc_handle->cb_count &&
           cc_handle->cc_state == GLOBUS_FTP_CONTROL_CLOSING) 
        { 
            call_close_cb = GLOBUS_TRUE; 
        }
    }
    globus_mutex_unlock(&(cc_handle->mutex));

    if(call_close_cb == GLOBUS_TRUE) 
    { 
        globus_i_ftp_control_call_close_cb(c_handle); 
    } 

    return;
}

#ifdef GLOBUS_INTERNAL_DOC

/**
 * Internal helper function which base 64 decodes and gss unwraps a
 * MIC command
 * 
 * Internal helper function which base 64 decodes and gss unwraps a
 * MIC command
 *
 * @param cmd
 *        A string representing the command to decode.
 * @param encoded_cmd
 *        Used to return the decoded command. Memory for the encoded
 *        command is allocated in this function.
 * @param auth_info
 *        The auth_info structure to use for gss unwrapping the command
 *
 * @return 
 *        - error object
 *        - GLOBUS_SUCCESS
 *
 */

#endif

globus_result_t
globus_i_ftp_control_decode_command(
    char *                                    cmd,
    char **                                   decoded_cmd,
    globus_ftp_control_auth_info_t *          auth_info)
{
    int                                       length;
    int                                       i;
    char *                                    tmp;
    globus_result_t                           rc;
    gss_buffer_desc                           wrapped_token;
    gss_buffer_desc                           unwrapped_token;
    OM_uint32                                 maj_stat;
    OM_uint32                                 min_stat;
    int                                       conf_state;
    gss_qop_t                                 qop_state;
    
    if(cmd == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_i_ftp_control_decode_command: cmd argument is NULL"))
            );
    }
    
    length=strlen(cmd);
    
    tmp=(char *) globus_libc_malloc(length+1);
    
    if(tmp == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_decode_command: malloc failed"))
            );
    }
    
    if(sscanf(cmd,"%4s",tmp) < 1)
    {
        rc = globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_decode_command: parse error"))
            );
        goto decode_error;
    }
    
    i=0;
    
    while(tmp[i] != '\0')
    {
        tmp[i]=toupper(tmp[i]);
        i++;
    }
    
    
    if(strcmp(tmp,"MIC") &&
       strcmp(tmp,"ENC"))
    {
        rc = globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_decode_command: parse error"))
            );
        goto decode_error;
    }

    if((!strcmp(tmp,"ENC")) && auth_info->encrypt == GLOBUS_FALSE)
    {
        /* if command is ENC and encryption isn't turned on in
           sec context */
        
        rc = globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_decode_command: encryption not supported"))
            );
        goto decode_error;
    }
       
    if(sscanf(cmd,"%*s %s",tmp) < 1)
    {
        rc = globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_decode_command: parse error"))
            );
        goto decode_error;
    }
    
    *decoded_cmd = (char *) globus_libc_malloc((length+3) * 6/8);
        
    if(*decoded_cmd == GLOBUS_NULL)
    {
        rc = globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_decode_command: malloc failed"))
            );
        goto decode_error;
    }
        
    rc=globus_i_ftp_control_radix_decode(
        (globus_byte_t *) tmp,
        (globus_byte_t *) *decoded_cmd,&length);
        
    if(rc != GLOBUS_SUCCESS)
    {
        globus_libc_free(*decoded_cmd);
        goto decode_error;
    }
    
    wrapped_token.value = *decoded_cmd;
    wrapped_token.length = length;

    maj_stat = gss_unwrap(&min_stat, 
                          auth_info->auth_gssapi_context,
                          &wrapped_token, 
                          &unwrapped_token,
                          &conf_state, 
                          &qop_state);
    
    if(maj_stat != GSS_S_COMPLETE)
    {
        rc = globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_decode_command: failed to unwrap command"))
            );
        
        globus_libc_free(*decoded_cmd);
        goto decode_error;
    }
    
    globus_assert(strlen(cmd) > unwrapped_token.length);
                    
    memcpy(tmp,
           unwrapped_token.value,
           unwrapped_token.length);

    tmp[unwrapped_token.length] = '\0';
    
    gss_release_buffer(&min_stat, &unwrapped_token);
    
    globus_libc_free(*decoded_cmd);
    
    *decoded_cmd=tmp;

    return GLOBUS_SUCCESS;

decode_error:

    *decoded_cmd=GLOBUS_NULL;
    
    globus_libc_free(tmp);
    
    return rc;
}

#ifdef GLOBUS_INTERNAL_DOC

/**
 * Internal helper function which gss wraps, base 64 encodes and puts
 * a 635 in front of the supplied response
 * 
 * Internal helper function which gss wraps, base 64 encodes and puts
 * a 635 in front of the supplied response
 *
 * @param reply
 *        A string representing the response to encode.
 * @param encoded_reply
 *        Used to return the encoded reply. Memory for the encoded
 *        reply is allocated in this function.
 * @param auth_info
 *        The auth_info structure to use for gss wrapping the reply.
 *
 * @return 
 *        - error object
 *        - GLOBUS_SUCCESS
 *
 */

#endif


globus_result_t
globus_i_ftp_control_encode_reply(
    char *                                    reply,
    char **                                   encoded_reply,
    globus_ftp_control_auth_info_t *          auth_info)
{
    gss_buffer_desc                        in_buf;
    gss_buffer_desc                        out_buf;
    OM_uint32                              maj_stat;
    OM_uint32                              min_stat;
    int                                    conf_state;
    int                                    length;
    
    if(auth_info == GLOBUS_NULL ||
       reply == GLOBUS_NULL ||
       encoded_reply == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_i_ftp_control_encode_reply: NULL argument detected"))
            );
    }
    
    in_buf.value = reply;
    in_buf.length = strlen(reply)+1;

    maj_stat = gss_wrap(&min_stat,
                        auth_info->auth_gssapi_context,
                        0,
                        GSS_C_QOP_DEFAULT,
                        &in_buf, 
                        &conf_state,
                        &out_buf);
    
    if(maj_stat != GSS_S_COMPLETE) 
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_i_ftp_control_encode_reply: gss_wrap failed"))
            );
    }

    *encoded_reply = (char *) globus_libc_malloc(
        (out_buf.length + 3) * 8 / 6 + 9);

    if(*encoded_reply == GLOBUS_NULL)
    {
        gss_release_buffer(&min_stat, &out_buf);
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_i_ftp_control_encode_reply: malloc failed"))
            );
    }

    (*encoded_reply)[0]='\0';

    if(auth_info->encrypt == GLOBUS_TRUE)
    {
        strcat(*encoded_reply,"632 ");
    }
    else
    {
        strcat(*encoded_reply,"631 ");
    }
    
    length = out_buf.length;
    globus_i_ftp_control_radix_encode(
        out_buf.value,
        (globus_byte_t *) &((*encoded_reply)[4]), 
        &length);

    (*encoded_reply)[length+4]='\r';
    (*encoded_reply)[length+5]='\n';
    (*encoded_reply)[length+6]='\0';

    gss_release_buffer(&min_stat, &out_buf);
    
    return GLOBUS_SUCCESS;
}




#ifdef GLOBUS_INTERNAL_DOC

/**
 * Internal helper function which sets up a list for keeping track of
 * server handles.
 * 
 * Internal helper function which sets up a list for keeping track of
 * server handles.
 *
 *
 * @return 
 *        - GLOBUS_SUCCESS
 *
 */

#endif


globus_result_t
globus_i_ftp_control_server_activate(void)
{
    globus_ftp_l_command_hash_entry_t * entries;
        
    globus_mutex_init(
        &(globus_l_ftp_server_handle_list_mutex), GLOBUS_NULL);

    globus_hashtable_init(&globus_l_ftp_control_parse_table,
                          64,
                          globus_hashtable_string_hash,
                          globus_hashtable_string_keyeq);

    /* the size of this array needs to be adjusted */
        
    entries = (globus_ftp_l_command_hash_entry_t *)
        globus_libc_malloc(44 * sizeof(globus_ftp_l_command_hash_entry_t));

    entries[0].code = GLOBUS_FTP_CONTROL_COMMAND_SBUF;
    entries[0].parse_func = globus_l_ftp_control_parse_sbuf_cmd;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "SBUF",
                            &entries[0]);

    entries[1].code = GLOBUS_FTP_CONTROL_COMMAND_SIZE;
    entries[1].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "SIZE",
                            &entries[1]);

    entries[2].code = GLOBUS_FTP_CONTROL_COMMAND_STOR;
    entries[2].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "STOR",
                            &entries[2]);

    entries[3].code = GLOBUS_FTP_CONTROL_COMMAND_ADAT;
    entries[3].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "ADAT",
                            &entries[3]);

    entries[4].code = GLOBUS_FTP_CONTROL_COMMAND_RETR;
    entries[4].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "RETR",
                            &entries[4]);

    entries[5].code = GLOBUS_FTP_CONTROL_COMMAND_ERET;
    entries[5].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "ERET",
                            &entries[5]);

    entries[6].code = GLOBUS_FTP_CONTROL_COMMAND_ESTO;
    entries[6].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "ESTO",
                            &entries[6]);

    entries[7].code = GLOBUS_FTP_CONTROL_COMMAND_USER;
    entries[7].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "USER",
                            &entries[7]);

    entries[8].code = GLOBUS_FTP_CONTROL_COMMAND_STOU;
    entries[8].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "STOU",
                            &entries[8]);

    entries[9].code = GLOBUS_FTP_CONTROL_COMMAND_DELE;
    entries[9].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "DELE",
                            &entries[9]);

    entries[10].code = GLOBUS_FTP_CONTROL_COMMAND_ACCT;
    entries[10].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "ACCT",
                            &entries[10]);

    entries[11].code = GLOBUS_FTP_CONTROL_COMMAND_SITE;
    entries[11].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "SITE",
                            &entries[11]);

    entries[12].code = GLOBUS_FTP_CONTROL_COMMAND_RNFR;
    entries[12].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "RNFR",
                            &entries[12]);

    entries[13].code = GLOBUS_FTP_CONTROL_COMMAND_RNTO;
    entries[13].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "RNTO",
                            &entries[13]);

    entries[14].code = GLOBUS_FTP_CONTROL_COMMAND_APPE;
    entries[14].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "APPE",
                            &entries[14]);

    entries[15].code = GLOBUS_FTP_CONTROL_COMMAND_REST;
    entries[15].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "REST",
                            &entries[15]);

    entries[16].code = GLOBUS_FTP_CONTROL_COMMAND_ALLO;
    entries[16].parse_func = globus_l_ftp_control_parse_allo_cmd;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "ALLO",
                            &entries[16]);

    entries[17].code = GLOBUS_FTP_CONTROL_COMMAND_SMNT;
    entries[17].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "SMNT",
                            &entries[17]);
        
    entries[18].code = GLOBUS_FTP_CONTROL_COMMAND_OPTS;
    entries[18].parse_func = globus_l_ftp_control_parse_opts_cmd;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "OPTS",
                            &entries[18]);

    entries[19].code = GLOBUS_FTP_CONTROL_COMMAND_PORT;
    entries[19].parse_func = globus_l_ftp_control_parse_port_cmd;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "PORT",
                            &entries[19]);

    entries[20].code = GLOBUS_FTP_CONTROL_COMMAND_SPOR;
    entries[20].parse_func = globus_l_ftp_control_parse_spor_cmd;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "SPOR",
                            &entries[20]);

    entries[21].code = GLOBUS_FTP_CONTROL_COMMAND_TYPE;
    entries[21].parse_func = globus_l_ftp_control_parse_type_cmd;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "TYPE",
                            &entries[21]);

    entries[22].code = GLOBUS_FTP_CONTROL_COMMAND_STRU;
    entries[22].parse_func = globus_l_ftp_control_parse_stru_cmd;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "STRU",
                            &entries[22]);

    entries[23].code = GLOBUS_FTP_CONTROL_COMMAND_AUTH;
    entries[23].parse_func = globus_l_ftp_control_parse_auth_cmd;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "AUTH",
                            &entries[23]);

    entries[24].code = GLOBUS_FTP_CONTROL_COMMAND_MODE;
    entries[24].parse_func = globus_l_ftp_control_parse_mode_cmd;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "MODE",
                            &entries[24]);

    entries[25].code = GLOBUS_FTP_CONTROL_COMMAND_CWD;
    entries[25].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "CWD",
                            &entries[25]);

    entries[26].code = GLOBUS_FTP_CONTROL_COMMAND_PASS;
    entries[26].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "PASS",
                            &entries[26]);
        
    entries[27].code = GLOBUS_FTP_CONTROL_COMMAND_RMD;
    entries[27].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "RMD",
                            &entries[27]);

    entries[28].code = GLOBUS_FTP_CONTROL_COMMAND_MKD;
    entries[28].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "MKD",
                            &entries[28]);

    entries[29].code = GLOBUS_FTP_CONTROL_COMMAND_CDUP;
    entries[29].parse_func = globus_l_ftp_control_parse_no_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "CDUP",
                            &entries[29]);

    entries[30].code = GLOBUS_FTP_CONTROL_COMMAND_QUIT;
    entries[30].parse_func = globus_l_ftp_control_parse_no_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "QUIT",
                            &entries[30]);

    entries[31].code = GLOBUS_FTP_CONTROL_COMMAND_REIN;
    entries[31].parse_func = globus_l_ftp_control_parse_no_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "REIN",
                            &entries[31]);

    entries[32].code = GLOBUS_FTP_CONTROL_COMMAND_PASV;
    entries[32].parse_func = globus_l_ftp_control_parse_no_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "PASV",
                            &entries[32]);

    entries[33].code = GLOBUS_FTP_CONTROL_COMMAND_SPAS;
    entries[33].parse_func = globus_l_ftp_control_parse_no_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "SPAS",
                            &entries[33]);

    entries[34].code = GLOBUS_FTP_CONTROL_COMMAND_ABOR;
    entries[34].parse_func = globus_l_ftp_control_parse_no_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "ABOR",
                            &entries[34]);

    entries[35].code = GLOBUS_FTP_CONTROL_COMMAND_SYST;
    entries[35].parse_func = globus_l_ftp_control_parse_no_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "SYST",
                            &entries[35]);

    entries[36].code = GLOBUS_FTP_CONTROL_COMMAND_NOOP;
    entries[36].parse_func = globus_l_ftp_control_parse_no_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "NOOP",
                            &entries[36]);

    entries[37].code = GLOBUS_FTP_CONTROL_COMMAND_FEAT;
    entries[37].parse_func = globus_l_ftp_control_parse_no_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "FEAT",
                            &entries[37]);

    entries[38].code = GLOBUS_FTP_CONTROL_COMMAND_PWD;
    entries[38].parse_func = globus_l_ftp_control_parse_no_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "PWD",
                            &entries[38]);

    entries[39].code = GLOBUS_FTP_CONTROL_COMMAND_LIST;
    entries[39].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "LIST",
                            &entries[39]);

    entries[40].code = GLOBUS_FTP_CONTROL_COMMAND_NLST;
    entries[40].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "NLST",
                            &entries[40]);

    entries[41].code = GLOBUS_FTP_CONTROL_COMMAND_STAT;
    entries[41].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "STAT",
                            &entries[41]);

    entries[42].code = GLOBUS_FTP_CONTROL_COMMAND_HELP;
    entries[42].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "HELP",
                            &entries[42]);

    entries[43].code = GLOBUS_FTP_CONTROL_COMMAND_LANG;
    entries[43].parse_func = globus_l_ftp_control_parse_string_arg;
    globus_hashtable_insert(&globus_l_ftp_control_parse_table,
                            "LANG",
                            &entries[43]);
    return GLOBUS_SUCCESS;
}

#ifdef GLOBUS_INTERNAL_DOC

/**
 *
 * Internal helper function which deactivates any server handles
 *
 * Internal helper function which goes through a list of server
 * handles and closes any open connections associated with the
 * handles. 
 * 
 * @return 
 *        - GLOBUS_SUCCESS
 *
 */

#endif

globus_result_t
globus_i_ftp_control_server_deactivate(void)
{
    globus_ftp_control_server_t *       server_handle;
    globus_result_t                     rc;

    globus_mutex_lock(&globus_l_ftp_server_handle_list_mutex);
    {
        while(!globus_list_empty(globus_l_ftp_server_handle_list))
        {
            server_handle=(globus_ftp_control_server_t *)
                globus_list_first(globus_l_ftp_server_handle_list);
            globus_mutex_lock(&(server_handle->mutex));
            {
                if(server_handle->state == 
                   GLOBUS_FTP_CONTROL_SERVER_LISTENING)
                {
                    rc=globus_io_close(&(server_handle->io_handle));
                    globus_assert(rc == GLOBUS_SUCCESS);
                    server_handle->state = GLOBUS_FTP_CONTROL_SERVER_DEAF;
                }
            }
            globus_mutex_unlock(&(server_handle->mutex));
            
            globus_mutex_destroy(&(server_handle->mutex));
            globus_list_remove(&globus_l_ftp_server_handle_list,
                               globus_l_ftp_server_handle_list);
        }
    }
    globus_mutex_unlock(&globus_l_ftp_server_handle_list_mutex);

    globus_mutex_destroy(&globus_l_ftp_server_handle_list_mutex);


    /* free all command hash memory */
        
    globus_libc_free(globus_hashtable_lookup(
                         &globus_l_ftp_control_parse_table,
                         "SBUF"));

    globus_hashtable_destroy(&globus_l_ftp_control_parse_table);
        
    return GLOBUS_SUCCESS;
}

globus_result_t globus_l_ftp_control_parse_sbuf_cmd(
    globus_ftp_control_command_t *      command)        
{
    if(sscanf(command->noop.raw_command,
              "%*s %d", &command->sbuf.buffer_size) < 1)
    {
        command->code=GLOBUS_FTP_CONTROL_COMMAND_UNKNOWN;
    }
    return GLOBUS_SUCCESS;
}

globus_result_t globus_l_ftp_control_parse_string_arg(
    globus_ftp_control_command_t *      command)
{
    int                                 length;
    int                                 arg_start;
        
    length = strlen(command->noop.raw_command);
        
    command->size.string_arg =
        (char *) globus_libc_malloc(length);
        
    if(command->size.string_arg == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_command_init: malloc failed")));
    }
        
    sscanf(command->noop.raw_command,"%*s%n",
           &arg_start);
                
    while(isspace(command->noop.raw_command[arg_start]))
    {
        arg_start++;
    }

    while(isspace(command->noop.raw_command[length-1]))
    {
        length--;
    }

    command->noop.raw_command[length]='\0';     

    strcpy(command->eret.string_arg,
           &command->noop.raw_command[arg_start]);
        
    return GLOBUS_SUCCESS;

}

globus_result_t globus_l_ftp_control_parse_no_arg(
    globus_ftp_control_command_t *      command)
{
    command->cdup.string_arg=GLOBUS_NULL;       
    return GLOBUS_SUCCESS;
}


globus_result_t globus_l_ftp_control_parse_allo_cmd(
    globus_ftp_control_command_t *      command)        
{
    command->allo.record_size = 0;
    if(sscanf(command->noop.raw_command,"%*s %d R %d",
              &(command->allo.size),
              &(command->allo.record_size))<1)
    {
        command->allo.size = 0;
        command->allo.record_size = 0;
    }
        
    return GLOBUS_SUCCESS;
}

globus_result_t globus_l_ftp_control_parse_opts_cmd(
    globus_ftp_control_command_t *      command)        
{
    int                                 length;

    length = strlen(command->noop.raw_command);

    command->opts.cmd_name =
        (char *) globus_libc_malloc(length);
            
    if(command->opts.cmd_name == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_command_init: malloc failed")));
    }

    command->opts.cmd_opts = 
        (char *) globus_libc_malloc(length);

    if(command->opts.cmd_opts == GLOBUS_NULL)
    {
        globus_libc_free(command->opts.cmd_name);
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_command_init: malloc failed")));
    }
            
    if(sscanf(command->noop.raw_command,"%*s %s %s",
              command->opts.cmd_name, command->opts.cmd_opts) < 2)
    {
        globus_libc_free(command->opts.cmd_name);
        globus_libc_free(command->opts.cmd_opts);
        command->opts.cmd_name=GLOBUS_NULL;
        command->opts.cmd_opts=GLOBUS_NULL;
    }
    return GLOBUS_SUCCESS;
}


globus_result_t globus_l_ftp_control_parse_port_cmd(
    globus_ftp_control_command_t *      command)        
{
    int                                 port[2];
                
    if(sscanf(command->noop.raw_command,
              "%*s %u,%u,%u,%u,%u,%u",
              &(command->port.host_port.host[0]),
              &(command->port.host_port.host[1]),
              &(command->port.host_port.host[2]),
              &(command->port.host_port.host[3]),
              &port[0],
              &port[1]) < 6)
    {
        command->code=GLOBUS_FTP_CONTROL_COMMAND_UNKNOWN;
        return GLOBUS_SUCCESS;
    }
        
    if((command->port.host_port.host)[0] > 255 ||
       (command->port.host_port.host)[1] > 255 ||
       (command->port.host_port.host)[2] > 255 ||
       (command->port.host_port.host)[3] > 255 ||
       port[0] > 255 ||
       port[1] > 255)
    {
        command->code=GLOBUS_FTP_CONTROL_COMMAND_UNKNOWN;
        return GLOBUS_SUCCESS;
    }
        
    command->port.host_port.port= (short) 256*port[0]+port[1];
    command->port.host_port.hostlen = 4;
    return GLOBUS_SUCCESS;
}

globus_result_t globus_l_ftp_control_parse_spor_cmd(
    globus_ftp_control_command_t *      command)        
{
    char *                              start;
    int                                 i;
    int                                 j;
    int                                 arg_start;
    int                                 port[2];
        
    start=strstr(command->noop.raw_command,"SPOR");
            
    i=0;
            
    while(start != &(command->noop.raw_command[i]))
    {
        i++;
    }

    arg_start = i+4;
        
    i = arg_start;
            
    j = 0;
        
    /* figure out how many host/port arguments there are */
        
    while(command->noop.raw_command[i])
    {
        if(isspace(command->noop.raw_command[i]))
        {
            i++;
            while(command->noop.raw_command[i] &&
                  isspace(command->noop.raw_command[i]))
            {
                i++;
            }
                    
            if(command->noop.raw_command[i])
            {
                j++;
            }
        }
        else
        {
            i++;
        }
    }
        
    /* allocate memory for them */
        
    command->spor.host_port = (globus_ftp_control_host_port_t*)
        globus_libc_malloc(j*sizeof(globus_ftp_control_host_port_t));
        
    if(command->spor.host_port == GLOBUS_NULL)
    {
        return globus_error_put(
            globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("globus_ftp_control_command_init: malloc failed")));
    }
        
    command->spor.num_args = j;
        
    i = arg_start;
    j = 0;
        
    while(command->noop.raw_command[i])
    {
        if(!isspace(command->noop.raw_command[i]))
        {
            if(sscanf(&(command->noop.raw_command[i]),
                      "%u,%u,%u,%u,%u,%u",
                      &(command->spor.host_port[j].host[0]),
                      &(command->spor.host_port[j].host[1]),
                      &(command->spor.host_port[j].host[2]),
                      &(command->spor.host_port[j].host[3]),
                      &port[0],
                      &port[1]) < 6)
            {
                globus_libc_free(command->spor.host_port);
                command->code=GLOBUS_FTP_CONTROL_COMMAND_UNKNOWN;
                return GLOBUS_SUCCESS;
            }
                        
            if((command->spor.host_port[j].host)[0] > 255 ||
               (command->spor.host_port[j].host)[1] > 255 ||
               (command->spor.host_port[j].host)[2] > 255 ||
               (command->spor.host_port[j].host)[3] > 255 ||
               port[0] > 255 ||
               port[1] > 255)
            {
                globus_libc_free(command->spor.host_port);
                command->code=GLOBUS_FTP_CONTROL_COMMAND_UNKNOWN;
                return GLOBUS_SUCCESS;
            }
                    
            command->spor.host_port[j].port= 
                (short) 256*port[0]+port[1];
            command->spor.host_port[j].hostlen = 4;
            
            i++;
            while(command->noop.raw_command[i] &&
                  !isspace(command->noop.raw_command[i]))
            {
                i++;
            }
                        
            if(command->noop.raw_command[i])
            {
                j++;
            }
        }
        else
        {
            i++;
        }
    }
        
    return GLOBUS_SUCCESS;
}

globus_result_t globus_l_ftp_control_parse_type_cmd(
    globus_ftp_control_command_t *      command)        

{
    char                                tmp;
        
    command->type.option=GLOBUS_FTP_CONTROL_TYPE_NO_OPTION;
    command->type.bytesize=0;

    if(sscanf(command->noop.raw_command,"%*s %c",&tmp) < 1)
    {
        command->type.type=GLOBUS_FTP_CONTROL_TYPE_NONE;
    }
            
    switch(tmp)
    {
    case 'A':
    case 'a':
        command->type.type=GLOBUS_FTP_CONTROL_TYPE_ASCII;
                                
        if(sscanf(command->noop.raw_command,
                  "%*s %*c %c",&tmp) > 0)
        {
            switch(tmp)
            {
            case 'N':
                command->type.option=GLOBUS_FTP_CONTROL_TYPE_OPTION_N;
                return GLOBUS_SUCCESS;
            case 'T':
                command->type.option=GLOBUS_FTP_CONTROL_TYPE_OPTION_T;
                return GLOBUS_SUCCESS;
            case 'C':
                command->type.option=GLOBUS_FTP_CONTROL_TYPE_OPTION_C;
                return GLOBUS_SUCCESS;
            default:
                command->type.type=GLOBUS_FTP_CONTROL_TYPE_NONE;
                command->type.option=GLOBUS_FTP_CONTROL_TYPE_NO_OPTION;
                return GLOBUS_SUCCESS;
            }
        }
        return GLOBUS_SUCCESS;
                
    case 'E':
    case 'e':
        command->type.type=GLOBUS_FTP_CONTROL_TYPE_EBCDIC;
                
        if(sscanf(command->noop.raw_command,
                  "%*s %*c %c",&tmp) > 0)
        {
            switch(tmp)
            {
            case 'N':
                command->type.option=GLOBUS_FTP_CONTROL_TYPE_OPTION_N;
                return GLOBUS_SUCCESS;
            case 'T':
                command->type.option=GLOBUS_FTP_CONTROL_TYPE_OPTION_T;
                return GLOBUS_SUCCESS;
            case 'C':
                command->type.option=GLOBUS_FTP_CONTROL_TYPE_OPTION_C;
                return GLOBUS_SUCCESS;
            default:
                command->type.type=GLOBUS_FTP_CONTROL_TYPE_NONE;
                command->type.option=GLOBUS_FTP_CONTROL_TYPE_NO_OPTION;
                return GLOBUS_SUCCESS;
            }
        }
        return GLOBUS_SUCCESS;

    case 'I':
    case 'i':
        command->type.type=GLOBUS_FTP_CONTROL_TYPE_IMAGE;
        return GLOBUS_SUCCESS;
    case 'L':
    case 'l':
        command->type.type=GLOBUS_FTP_CONTROL_TYPE_LOCAL;
        if(sscanf(command->noop.raw_command,"%*s %*c %u",
                  &command->type.bytesize) < 1)
        {
            command->type.type=GLOBUS_FTP_CONTROL_TYPE_NONE;
            command->type.option=GLOBUS_FTP_CONTROL_TYPE_NO_OPTION;
            return GLOBUS_SUCCESS;
        }
        return GLOBUS_SUCCESS;
    default:
        command->type.type=GLOBUS_FTP_CONTROL_TYPE_NONE;
        return GLOBUS_SUCCESS;
    }
}


globus_result_t globus_l_ftp_control_parse_stru_cmd(
    globus_ftp_control_command_t *      command)        
{
    char                                tmp;
        
    command->stru.structure=
        GLOBUS_FTP_CONTROL_STRUCTURE_NONE;
    if(sscanf(command->noop.raw_command,"%*s %c",&tmp) < 1)
    {
        return GLOBUS_SUCCESS;
    }
            
    switch(tmp)
    {
    case 'F':
    case 'f':
        command->stru.structure=
            GLOBUS_FTP_CONTROL_STRUCTURE_FILE;
        return GLOBUS_SUCCESS;
    case 'R':
    case 'r':
        command->stru.structure=
            GLOBUS_FTP_CONTROL_STRUCTURE_RECORD;
        return GLOBUS_SUCCESS;
    case 'P':
    case 'p':
        command->stru.structure=
            GLOBUS_FTP_CONTROL_STRUCTURE_PAGE;
        return GLOBUS_SUCCESS;
    default:
        return GLOBUS_SUCCESS;
    }
}


globus_result_t globus_l_ftp_control_parse_auth_cmd(
    globus_ftp_control_command_t *      command)        
{
    char                                tmp[10];
    int                                 i;
        
    command->auth.type=GLOBUS_FTP_CONTROL_AUTH_UNKNOWN;
        
    if(sscanf(command->noop.raw_command,"%*s %7s",tmp) < 1)
    {
        return GLOBUS_SUCCESS;
    }
        
    i = 0;
        
    while(tmp[i] != '\0')
    {
        tmp[i] = toupper(tmp[i]);
        i++;
    }
        
    if(strcmp("GSSAPI",tmp) == 0)
    {
        command->auth.type=GLOBUS_FTP_CONTROL_AUTH_GSSAPI;
    }
        
    return GLOBUS_SUCCESS;
        
}

globus_result_t globus_l_ftp_control_parse_mode_cmd(
    globus_ftp_control_command_t *      command)        
{
    char                                tmp;
        
    command->code=GLOBUS_FTP_CONTROL_COMMAND_MODE;
        
    if(sscanf(command->noop.raw_command,"%*s %c",&tmp) < 1)
    {
        command->mode.mode=GLOBUS_FTP_CONTROL_MODE_NONE;
        return GLOBUS_SUCCESS;
    }
            
    switch(tmp)
    {
    case 'S':
    case 's':
        command->mode.mode=GLOBUS_FTP_CONTROL_MODE_STREAM;
        return GLOBUS_SUCCESS;
    case 'B':
    case 'b':
        command->mode.mode=GLOBUS_FTP_CONTROL_MODE_BLOCK;
        return GLOBUS_SUCCESS;
    case 'C':
    case 'c':
        command->mode.mode=GLOBUS_FTP_CONTROL_MODE_COMPRESSED;
        return GLOBUS_SUCCESS;
    case 'E':
    case 'e':
        command->mode.mode=GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK;
        return GLOBUS_SUCCESS;
    default:
        command->mode.mode=GLOBUS_FTP_CONTROL_MODE_NONE;
        return GLOBUS_SUCCESS;
    }
}
