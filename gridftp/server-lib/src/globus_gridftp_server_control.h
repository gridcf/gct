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

#if !defined GLOBUS_GRIDFTP_SERVER_CONTROL_H
#define GLOBUS_GRIDFTP_SERVER_CONTROL_H

#include "globus_xio.h"
#include "globus_common.h"
#include "globus_gss_assist.h"
#include "globus_xio_system.h"

#ifndef TARGET_ARCH_WIN32
#include <grp.h>

#else
typedef int gid_t;

struct passwd
{
    char *pw_name;
    char *pw_passwd;
    int   pw_uid;
    int   pw_gid;
    char *pw_comment;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};

struct group 
{
    char  *gr_name;
    char  *gr_passwd;
    gid_t gr_gid;
    char  **gr_mem;
};

#endif /* WIN32 */

typedef struct globus_i_gsc_server_handle_s * globus_gridftp_server_control_t;
typedef struct globus_i_gsc_attr_s *    globus_gridftp_server_control_attr_t;
typedef struct globus_i_gsc_op_s *      globus_gridftp_server_control_op_t;
typedef struct globus_i_gsc_op_s *      globus_gsc_959_op_t;
typedef time_t                          globus_time_t;

/***********************************************************************
 *                          error types
 *                          -----------
 **********************************************************************/
typedef enum globus_gsc_error_type_e
{
    GLOBUS_GRIDFTP_SERVER_CONTROL_ERROR_PANIC,
    GLOBUS_GRIDFTP_SERVER_CONTROL_ERROR_SYSTEM_RESOURCE,
    GLOBUS_GRIDFTP_SERVER_CONTROL_ERROR_AUTHENTICATION,
    GLOBUS_GRIDFTP_SERVER_CONTROL_ERROR_SYNTAX
} globus_gridftp_server_control_error_type_t;

typedef enum globus_gsc_response_e
{
    /* user command errors */
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACTION_FAILED,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_PATH_INVALID,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_INVALID_FILE_TYPE,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACCESS_DENIED,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_DATA_CONN_TERMINATED,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_DATA_CONN_FAILED,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_DATA_CONN_AUTH,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_PANIC,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_PARTIAL_SUCCESS,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_MIN_FTP_ERROR = 400,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_MAX_FTP_ERROR = 599
} globus_gridftp_server_control_response_t;

// For compatibility with old mispelled name - deprecated
#define GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACCESS_DENINED \
	GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACCESS_DENIED

#ifdef __GNUC__
#define GlobusGridFTPServerName(func) static const char * _gridftp_server_name __attribute__((__unused__)) = #func
#else
#define GlobusGridFTPServerName(func) static const char * _gridftp_server_name = #func
#endif

#define _FSCSL(s) globus_common_i18n_get_string( \
		     GLOBUS_GRIDFTP_SERVER_CONTROL_MODULE, \
		     s)

#define _FSMSL(s) globus_common_i18n_get_string_by_key( \
		     NULL, \
		     GLOBUS_GRIDFTP_SERVER_CONTROL_MODULE->module_name, \
		     s)


#define GlobusGridFTPServerErrorParameter(param_name)                       \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_GRIDFTP_SERVER_CONTROL_MODULE,                           \
            GLOBUS_NULL,                                                    \
            GLOBUS_GRIDFTP_SERVER_CONTROL_ERROR_PARAMETER,                  \
            __FILE__,                                                       \
            _gridftp_server_name,                                           \
            __LINE__,                                                       \
            "Bad parameter, %s",                                            \
            (param_name)))

#define GlobusGridFTPServerControlErrorSyntax()                             \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_GRIDFTP_SERVER_CONTROL_MODULE,                           \
            GLOBUS_NULL,                                                    \
            GLOBUS_GRIDFTP_SERVER_CONTROL_ERROR_SYNTAX,                     \
            __FILE__,                                                       \
            _gridftp_server_name,                                           \
            __LINE__,                                                       \
            "Syntax error"))

#define GlobusGridFTPServerControlErrorPanic()                              \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_GRIDFTP_SERVER_CONTROL_MODULE,                           \
            GLOBUS_NULL,                                                    \
            GLOBUS_GRIDFTP_SERVER_CONTROL_ERROR_PANIC,                      \
            __FILE__,                                                       \
            _gridftp_server_name,                                           \
            __LINE__,                                                       \
            "Panic error"))

#define GlobusGridFTPServerControlErrorSystem()                             \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_GRIDFTP_SERVER_CONTROL_MODULE,                           \
            GLOBUS_NULL,                                                    \
            GLOBUS_GRIDFTP_SERVER_CONTROL_ERROR_SYSTEM_RESOURCE,            \
            __FILE__,                                                       \
            _gridftp_server_name,                                           \
            __LINE__,                                                       \
            "System resource error"))

// For compatibility with old mispelled macro - deprecated
#define GlobusGridFTPServerControlErrorSytem()                              \
    GlobusGridFTPServerControlErrorSystem()

#define GlobusGridFTPServerControlErrorAuthentication()                     \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_GRIDFTP_SERVER_CONTROL_MODULE,                           \
            GLOBUS_NULL,                                                    \
            GLOBUS_GRIDFTP_SERVER_CONTROL_ERROR_AUTHENTICATION,             \
            __FILE__,                                                       \
            _gridftp_server_name,                                           \
            __LINE__,                                                       \
            "Authentication error"))


/*
 *  globus_gridftp_server_control_security_type_t
 *  ---------------------------------------------
 *  The allowed security modes.  Can be a mask of more than 1 selection.
 */
typedef enum globus_gsc_security_type_e
{
    GLOBUS_GRIDFTP_SERVER_LIBRARY_NONE = 0x01,
    GLOBUS_GRIDFTP_SERVER_LIBRARY_GSSAPI = 0x02,
    GLOBUS_GRIDFTP_SERVER_LIBRARY_TLS = 0x04
} globus_gridftp_server_control_security_type_t;

typedef enum globus_gsc_layout_e
{
    GLOBUS_GSC_LAYOUT_TYPE_NONE = 0,
    GLOBUS_GSC_LAYOUT_TYPE_PARTITIONED,
    GLOBUS_GSC_LAYOUT_TYPE_BLOCKED
} globus_gsc_layout_t;

typedef enum globus_gridftp_server_control_stat_error_e 
{
    GLOBUS_GRIDFTP_SERVER_CONTROL_STAT_SUCCESS = 0,
    GLOBUS_GRIDFTP_SERVER_CONTROL_STAT_OPENFAILED,
    GLOBUS_GRIDFTP_SERVER_CONTROL_STAT_INVALIDLINK,
} globus_gridftp_server_control_stat_error_t;

typedef enum globus_gfs_traversal_options_e {
    GLOBUS_GFS_TRAVERSAL_CONTINUE = 1,
    GLOBUS_GFS_TRAVERSAL_FOLLOW_SYMLINKS = 2
} globus_gfs_traversal_options_t;


/**
 *  stat structure
 *  --------------
 *
 *  This structure is exposed to the user.  The user must populate an array
 *  of these structures when a resource query is made to them.  The structure
 *  is very similar to the posix strust stat.
 */
typedef struct globus_gridftp_server_control_stat_s
{
    int                                     mode;
    int                                     nlink;
    char *                                  name;
    char *                                  symlink_target;
    uid_t                                   uid;
    gid_t                                   gid;
    globus_off_t                            size;
    globus_time_t                           atime;
    globus_time_t                           ctime;
    globus_time_t                           mtime;
    int                                     dev;
    int                                     ino;
    int                                         link_mode;
    globus_gridftp_server_control_stat_error_t  error;
} globus_gridftp_server_control_stat_t;

/**
 *  net protocol type
 *  -----------------
 *
 *  This enumeration defines what network protocol is being used.  At this
 *  point there are only 2 available, ipv4 and ipv6.
 */
typedef enum globus_gridftp_server_control_network_protocol_e
{
    GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV4 = 1,
    GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV6
} globus_gridftp_server_control_network_protocol_t;

/**
 *  data direction type
 *  -------------------
 *
 *  This type indicates to the library in what direction a user data object
 *  can be used for transfer.  STOR is for receiving data, RETR is for
 *  sending data, and BI is for either direction.  When connecting a data
 *  object the user will use pass in one of these 3 values.  see
 *  globus_gridftp_server_control_finished_transfer().  
 */
typedef enum globus_i_gsc_data_dir_e
{
    GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_RECV = 0x01,
    GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_SEND = 0x02,
    GLOBUS_GRIDFTP_SERVER_CONTROL_DATA_DIR_BI = 0x03
} globus_gridftp_server_control_data_dir_t;

/**
 *  event type
 *  ----------
 *
 *  During a data transfer the user can send events back to the client.
 *  The accepted event types are defined here.
 */
typedef enum globus_gridftp_server_control_event_type_e
{
    GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_PERF = 0x01,
    GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_RESTART = 0x02,
    GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_ABORT = 0x04,
    GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_TRANSFER_COMPLETE = 0x08
} globus_gridftp_server_control_event_type_t;

/**
 *  authentication callback type
 *  ---------------------------
 *
 *  This funciton is called to tell the user a client is
 *  trying to authenticate with the creditails supplied as parameters.
 *  The user can decided whether or not to accept the user and then call
 *  globus_gridftp_server_control_finished_auth() with the appropriate values.
 */
typedef void
(*globus_gridftp_server_control_auth_cb_t)(
    globus_gridftp_server_control_op_t      op,
    globus_gridftp_server_control_security_type_t secure_type,
    gss_ctx_id_t                            context,
    const char *                            subject,
    const char *                            user_name,
    const char *                            pw,
    void *                                  user_arg);

/**
 *  globus_gridftp_server_control_finished_auth()
 *
 *  Once the user decides to the accept the client or not they call this 
 *  function.  The value of result determines if the user is accepted or not.
 */
globus_result_t
globus_gridftp_server_control_finished_auth(
    globus_gridftp_server_control_op_t  op,
    const char *                        username,
    globus_gridftp_server_control_response_t response_code,
    const char *                        msg);
    
/**
 *  mask type.
 *  ----------
 *
 *  This tells the user how the server expects it to query the resource.
 *  Either it is looking for the entire directory listing or simply the 
 *  stat of the file only.
 */
typedef enum globus_gridftp_server_control_resource_mask_e
{
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESOURCE_DIRECTORY_LIST = 1,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESOURCE_FILE_ONLY      = 2,
    GLOBUS_GRIDFTP_SERVER_CONTROL_RESOURCE_USER_DEFINED   = 3
} globus_gridftp_server_control_resource_mask_t;

/**
 *  generic server callback
 *
 *  used for stop, done, error
 */
typedef void
(*globus_gridftp_server_control_cb_t)(
    globus_gridftp_server_control_t         server,
    globus_result_t                         res,
    void *                                  user_arg);

/**
 *  resource query callback
 *  -----------------------
 *
 *  This function is called when the server needs informatioon about a 
 *  given resource.  The resource is typically a file.  Once the user
 *  has determined the needed information about the rsource they call
 *  globus_gridftp_server_control_finished_resource() with the appropriate
 *  parameters.
 */
typedef void
(*globus_gridftp_server_control_resource_cb_t)(
    globus_gridftp_server_control_op_t      op,
    const char *                            path,
    globus_gridftp_server_control_resource_mask_t mask,
    void *                                  user_arg);

/*
 *  this function is called to tell the user that a data transfer 
 *  has been requested by the client.
 */
typedef void
(*globus_gridftp_server_control_transfer_cb_t)(
    globus_gridftp_server_control_op_t      op,
    void *                                  data_handle,
    const char *                            local_target,
    const char *                            mod_name,
    const char *                            mod_parms,
    globus_range_list_t                     range_list,
    void *                                  user_arg);

typedef void
(*globus_gridftp_server_control_list_cb_t)(
    globus_gridftp_server_control_op_t      op,
    void *                                  data_handle,
    const char *                            path,
    const char *                            fact_str,
    int                                     depth,
    int                                     traversal_options,
    void *                                  user_arg);

globus_result_t
globus_gridftp_server_control_list_buffer_alloc(
    const char *                            fact_str,
    uid_t                                   uid,
    const char *                            base_path,
    globus_gridftp_server_control_stat_t *  stat_info_array,
    int                                     stat_count,
    globus_byte_t **                        out_buf,
    globus_size_t *                         out_size);

void
globus_gridftp_server_control_list_buffer_free(
    globus_byte_t *                         buffer);

/**
 *  logging 
 *
 *  A user can set a logging function that will be called
 *  with the full command sent over the control connection.
 *  Logging commands are divided up into a subset of types
 *  enumerated here.
 */
enum
{
    GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SECURITY = 0x0001,
    GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER = 0x0004,
    GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE = 0x0008,
    GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS = 0x0010,
    GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_ERROR = 0x0020,
    GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE = 0x0040,
    GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_LIST = 0x0080,
    GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_OTHER = 0x0100,
    GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_REPLY = 0x0400,
    GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_ALL = 0xFFFF
};

typedef void
(*globus_gridftp_server_control_log_cb_t)(
    globus_gridftp_server_control_t     server_handle,
    const char *                        full_command,
    int                                 cls,
    void *                              user_arg);

/**
 *  create a passive data object
 *
 *  this function is called to tell the user to create a passively 
 *  connection data object.  When the user has completed this request
 *  it calls globus_gridftp_server_control_finished_passive_connect()
 *  with appropriate values.
 *
 *  note:  the user does not have to establish the data pathways at this
 *         point.  They only need provide a contact string upon which a
 *         remote client can connect.
 */
typedef void
(*globus_gridftp_server_control_passive_connect_cb_t)(
    globus_gridftp_server_control_op_t      op,
    globus_gridftp_server_control_network_protocol_t net_prt,
    int                                     max,
    const char *                            pathname,
    void *                                  user_arg);

/**
 *  create an active data object
 *
 *  this function is called to tell the user to create an actively
 *  connecting data object.  When the user has completed this request
 *  it calls globus_gridftp_server_control_finished_active_connect()
 *
 *  note:  the user does not have to establish the data pathways at this
 *         point.  They only need to store the information provided and 
 *         verify that they can connect to the contact point.  However if
 *         the user wishes to create the data pathways here that is accepted.
 */
typedef void
(*globus_gridftp_server_control_active_connect_cb_t)(
    globus_gridftp_server_control_op_t      op,
    globus_gridftp_server_control_network_protocol_t net_prt,
    const char **                           cs,
    int                                     cs_count,
    void *                                  user_arg);

/**
 *  data connection destroyed
 *
 *  The library notifies the user that it no longer needs the data connection
 *  by calling this interface function.  Upon returning from this function
 *  the data object is considered destroyed and the library will no reference
 *  it again.
 */
typedef void
(*globus_gridftp_server_control_data_destroy_cb_t)(
    void *                                  user_data_handle,
    void *                                  user_arg);

typedef void
(*globus_gridftp_server_control_event_cb_t)(
    globus_gridftp_server_control_op_t      op,
    int                                     event_type,
    void *                                  user_arg);

globus_result_t
globus_gridftp_server_control_events_enable(
    globus_gridftp_server_control_op_t  op,
    int                                 event_mask,
    globus_gridftp_server_control_event_cb_t event_cb,
    void *                              user_arg);

/*
globus_result_t
globus_gridftp_server_control_events_disable(
    globus_gridftp_server_control_op_t      op);
*/

/**
 *  finished resource query request
 *
 *  Once a user has determined information about a resource they call this
 *  function.  If result is passed != GLOBUS_SUCCESS the user is telling
 *  the library that the resource could not be queried.
 */
globus_result_t
globus_gridftp_server_control_finished_resource(
    globus_gridftp_server_control_op_t      op,
    globus_gridftp_server_control_stat_t *  stat_info_array,
    int                                     stat_count,
    int                                     uid,
    int                                     gid_count,
    int *                                   gid_array,
    globus_gridftp_server_control_response_t response_code,
    const char *                            msg);

/**************************************************************************
 *  attr functions.
 *
 *  self explaintory for now
 *************************************************************************/
globus_result_t
globus_gridftp_server_control_attr_init(
    globus_gridftp_server_control_attr_t *  in_attr);

globus_result_t
globus_gridftp_server_control_attr_destroy(
    globus_gridftp_server_control_attr_t    in_attr);

globus_result_t
globus_gridftp_server_control_attr_copy(
    globus_gridftp_server_control_attr_t *  dst,
    globus_gridftp_server_control_attr_t    src);

globus_result_t
globus_gridftp_server_control_attr_set_resource(
    globus_gridftp_server_control_attr_t    in_attr,
    globus_gridftp_server_control_resource_cb_t resource_cb,
    void *                                  user_arg);

globus_result_t
globus_gridftp_server_control_attr_set_auth(
    globus_gridftp_server_control_attr_t    in_attr,
    globus_gridftp_server_control_auth_cb_t auth_cb,
    void *                                  user_arg);

globus_result_t
globus_gridftp_server_control_attr_set_list(
    globus_gridftp_server_control_attr_t    in_attr,
    globus_gridftp_server_control_list_cb_t list_cb,
    void *                                  user_arg);

globus_result_t
globus_gridftp_server_control_attr_set_banner(
    globus_gridftp_server_control_attr_t    in_attr,
    char *                                  banner);

globus_result_t
globus_gridftp_server_control_attr_set_message(
    globus_gridftp_server_control_attr_t    in_attr,
    char *                                  message);

globus_result_t
globus_gridftp_server_control_attr_set_security(
    globus_gridftp_server_control_attr_t    in_attr,
    globus_gridftp_server_control_security_type_t sec);

globus_result_t
globus_gridftp_server_control_attr_set_idle_time(
    globus_gridftp_server_control_attr_t    in_attr,
    int                                     idle_timeout,
    int                                     preauth_timeout);

globus_result_t
globus_gridftp_server_control_attr_set_epsv_ip(
    globus_gridftp_server_control_attr_t    in_attr,
    globus_bool_t                           epsv_ip);

globus_result_t
globus_gridftp_server_control_attr_set_ipv6(
    globus_gridftp_server_control_attr_t    in_attr,
    globus_bool_t                           ipv6);

/*
 *  if module name is NULL then it is the default handler
 */
globus_result_t
globus_gridftp_server_control_attr_add_recv(
    globus_gridftp_server_control_attr_t    in_attr,
    const char *                            module_name,
    globus_gridftp_server_control_transfer_cb_t recv_func,
    void *                                  user_arg);

globus_result_t
globus_gridftp_server_control_attr_add_send(
    globus_gridftp_server_control_attr_t    in_attr,
    const char *                            module_name,
    globus_gridftp_server_control_transfer_cb_t send_func,
    void *                                  user_arg);

globus_result_t
globus_gridftp_server_control_attr_data_functions(
    globus_gridftp_server_control_attr_t                server_attr,
    globus_gridftp_server_control_active_connect_cb_t   active_cb,
    void *                                              active_arg,
    globus_gridftp_server_control_passive_connect_cb_t  passive_cb,
    void *                                              passive_arg,
    globus_gridftp_server_control_data_destroy_cb_t     destroy_cb,
    void *                                              destroy_arg);

globus_result_t
globus_gridftp_server_control_attr_set_log(
    globus_gridftp_server_control_attr_t    server_attr,
    globus_gridftp_server_control_log_cb_t  log_func,
    int                                     log_mask,
    void *                                  user_arg);

/**
 *  initialize the server
 *
 *  Initialize a server object.  This function allocates resources to 
 *  the server object.  globus_gridftp_server_control_destroy()
 *  needs to be called to free up these resources.
 */
globus_result_t
globus_gridftp_server_control_init(
    globus_gridftp_server_control_t *       server);

/**
 *  destroy the server
 *
 *  free up the resources associated with the server object.  The object is
 *  no longer valid once this returns with GLOBUS_SUCCESS.
 */
globus_result_t
globus_gridftp_server_control_destroy(
    globus_gridftp_server_control_t         server);

/**
 *  start dispatching callbacks.
 *
 *  This function will start reading commands on a server object.
 *  As soon as this command is called the user can expect to get whatever
 *  callbacks they have associated with the attr.  If the function returns
 *  successfully the user will have to call globus_gridftp_server_control_stop()
 *  before destroying the server.  The attr parameter to this function
 *  provides the means for setting up all callbacks.  The attr overrides
 *  any previously set values.
 */ 
globus_result_t
globus_gridftp_server_control_start(
    globus_gridftp_server_control_t         server,
    globus_gridftp_server_control_attr_t    attr,
    globus_xio_system_socket_t              system_handle,
    globus_gridftp_server_control_cb_t      done_cb,
    void *                                  user_arg);

/**
 *  stop dispatching callbacks
 *
 *  This function asynchrounsly stops the server.  When the
 *  user receives the done_callback the server object is destroyed and
 *  no more callbacks will come associated with that server object.  Once
 *  The callback is called the user is free to call destroy.
 */
globus_result_t
globus_gridftp_server_control_stop(
    globus_gridftp_server_control_t         server);

/*
 *  setters and getters
 */
globus_result_t
globus_gridftp_server_control_get_allocated(
    globus_gridftp_server_control_op_t      op,
    globus_off_t *                          out_allo);

globus_result_t
globus_gridftp_server_control_get_layout(
    globus_gridftp_server_control_op_t      op,
    globus_gsc_layout_t *                   layout_type,
    globus_size_t *                         block_size);

globus_result_t
globus_gridftp_server_control_get_buffer_size(
    globus_gridftp_server_control_op_t      op,
    globus_size_t *                         out_recv_bs,
    globus_size_t *                         out_send_bs);

globus_result_t
globus_gridftp_server_control_get_parallelism(
    globus_gridftp_server_control_op_t      op,
    int *                                   out_parallelism);

globus_result_t
globus_gridftp_server_control_get_mode(
    globus_gridftp_server_control_op_t      op,
    char *                                  out_mode);

globus_result_t
globus_gridftp_server_control_get_type(
    globus_gridftp_server_control_op_t      op,
    char *                                  out_type);

globus_result_t
globus_gridftp_server_control_get_cwd(
    globus_gridftp_server_control_t         server,
    char **                                 cwd_string);

globus_result_t
globus_gridftp_server_control_set_cwd(
    globus_gridftp_server_control_t         server,
    const char *                            cwd_string);

globus_result_t
globus_gridftp_server_control_get_data_auth(
    globus_gridftp_server_control_op_t      op,
    char **                                 subject,
    char *                                  dcau,
    char *                                  prot,
    gss_cred_id_t *                         del_cred);

globus_bool_t
globus_gridftp_server_control_authenticated(
    globus_gridftp_server_control_t         server);

char *
globus_gridftp_server_control_get_cmd_string(
    globus_gridftp_server_control_op_t      op);

/***************************************************************************
 *  data object
 *
 *  The server uses the following interface functions for communication
 *  with the user data connection needs.  The user is responisible for
 *  bringing up the data connection.  This library will manage when a new
 *  data connection must be created, but it relies on the user to actually
 *  make the connection.
 **************************************************************************/

/**
 *  finished active connect request
 *
 *  After a receving notification that a connection should be made via 
 *  the interface function globus_gridftp_server_control_active_connect_t().
 *  The user then tells the library that a connection
 *  has been made by calling this function.  The user may also (and will
 *  likely want to) associate its on memory with this data object here.
 *  If the user cannot make a connection they indicate this with the res
 *  parameter.
 */
globus_result_t
globus_gridftp_server_control_finished_active_connect(
    globus_gridftp_server_control_op_t      op,
    void *                                  user_data_handle,
    globus_gridftp_server_control_data_dir_t data_dir,
    globus_gridftp_server_control_response_t response_code,
    const char *                            msg);

/**
 *  finished passive connect request
 *
 *  After a receving notification that a connection should be made via 
 *  the interface function globus_gridftp_server_control_passive_connect_t().
 *  The user then tells the library that a connection
 *  has been made by calling this function.  The user may also (and will
 *  likely want to) associate its on memory with this data object here.
 *  If the user cannot make a connection they indicate this with the res
 *  parameter.
 */
globus_result_t
globus_gridftp_server_control_finished_passive_connect(
    globus_gridftp_server_control_op_t      op,
    void *                                  user_data_handle,
    globus_gridftp_server_control_data_dir_t data_dir,
    const char **                           cs,
    int                                     cs_count,
    globus_gridftp_server_control_response_t response_code,
    const char *                            msg);

/**
 *  close a data object
 *
 *  When a data conenction goes away, either intentionally or due to 
 *  network error the user tells the library that it is gone with
 *  this function.  A call to this function will result in 
 *  globus_gridftp_server_data_destroy_t.  The data object is not considered
 *  destroyed until globus_gridftp_server_data_destroy_t is called.
 */
globus_result_t
globus_gridftp_server_control_disconnected(
    globus_gridftp_server_control_t         server,
    void *                                  user_data_handle);

/**
 *  begin a data transfer
 *
 *  once the user is notified that they are to begin a transfer for a given
 *  data module they need to verify the request.  The path and module
 *  parameters should be checked for validity.  If acceptable the user MUST
 *  call this function prior to transfering data along the data pathways.
 *  If it is not acceptable the user must call 
 *  globus_gridftp_server_control_finished_transfer() with the appropriate 
 *  error.
 */
globus_result_t
globus_gridftp_server_control_begin_transfer(
    globus_gridftp_server_control_op_t  op);

/**
 *  a data transfer requested has been completed.
 *
 *  when a transfer is completed, either successfully or due to
 *  failure the user must call this function to notify the user of
 *  completion.
 */
globus_result_t
globus_gridftp_server_control_finished_transfer(
    globus_gridftp_server_control_op_t      op,
    globus_gridftp_server_control_response_t response_code,
    const char *                            msg);

globus_result_t
globus_gridftp_server_control_set_paths(
    globus_gridftp_server_control_op_t      op,
    const char *                            absolute_path,
    const char *                            resolved_path);

globus_result_t
globus_gridftp_server_control_add_feature(
    globus_gridftp_server_control_t         server,
    const char *                            feature);


/* use with care, not *external* external */
typedef enum globus_gsc_959_command_desc_e
{
    GLOBUS_GSC_COMMAND_POST_AUTH = 0x01,
    GLOBUS_GSC_COMMAND_PRE_AUTH = 0x02
} globus_gsc_959_command_desc_t;

char *
globus_gsc_string_to_959(
    int                                 code,
    const char *                        in_str,
    const char *                        preline);

typedef void
(*globus_gsc_959_command_cb_t)(
    globus_gsc_959_op_t                     op,
    const char *                            full_command,
    char **                                 cmd_array,
    int                                     argc,
    void *                                  user_arg);

globus_result_t
globus_gsc_959_command_add(
    globus_gridftp_server_control_t         server,
    const char *                            command_name,
    globus_gsc_959_command_cb_t             command_cb,
    globus_gsc_959_command_desc_t           desc,
    int                                     min_argc,
    int                                     max_argc,
    const char *                            help,
    void *                                  user_arg);

globus_result_t
globus_gsc_959_command_remove(
    globus_gridftp_server_control_t         server,
    const char *                        command_name);

void
globus_gsc_959_finished_command(
    globus_gsc_959_op_t                     op,
    char *                                  reply_msg);

globus_result_t
globus_gridftp_server_control_event_send_perf(
    globus_gridftp_server_control_op_t      op,
    int                                     stripe_ndx,
    globus_off_t                            nbytes);

globus_result_t
globus_gridftp_server_control_event_send_restart(
    globus_gridftp_server_control_op_t      op,
    globus_range_list_t                     restart);


void
globus_gridftp_server_control_421_end(
    globus_gridftp_server_control_t     server,
    char *                              reply_msg);

extern globus_module_descriptor_t      globus_i_gsc_module;

#define GLOBUS_GRIDFTP_SERVER_CONTROL_MODULE (&globus_i_gsc_module)

#endif
