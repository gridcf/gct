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

#if !defined(GLOBUS_I_GRIDFTP_SERVER_CONTROL_H)
#define GLOBUS_I_GRIDFTP_SERVER_CONTROL_H 1

#include "globus_gridftp_server_control.h"
#include "globus_xio.h"
#include "globus_xio_system.h"
#include "globus_xio_tcp_driver.h"
#include "globus_xio_pipe_driver.h"
#include "globus_xio_telnet.h"
#include "globus_xio_gssapi_ftp.h"

#define GLOBUS_GRIDFTP_VERSION_CTL              1

GlobusDebugDeclare(GLOBUS_GRIDFTP_SERVER_CONTROL);

#define GlobusGSDebugPrintf(level, message)                                \
    GlobusDebugPrintf(GLOBUS_GRIDFTP_SERVER_CONTROL, level, message)

#define GlobusGridFTPServerDebugEnter()                                     \
    GlobusGSDebugPrintf(                                                    \
        GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_TRACE,                          \
        ("[%s] Entering\n", _gridftp_server_name))

#define GlobusGridFTPServerDebugExit()                                      \
    GlobusGSDebugPrintf(                                                    \
        GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_TRACE,                          \
        ("[%s] Exiting\n", _gridftp_server_name))
    
#define GlobusGridFTPServerDebugExitWithError()                             \
    GlobusGSDebugPrintf(                                                    \
        GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_TRACE,                          \
        ("[%s] Exiting with error\n", _gridftp_server_name))
    
#define GlobusGridFTPServerDebugInternalEnter()                             \
    GlobusGSDebugPrintf(                                                    \
        GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_INTERNAL_TRACE,                 \
        ("[%s] I Entering\n", _gridftp_server_name))

#define GlobusGridFTPServerDebugInternalExit()                              \
    GlobusGSDebugPrintf(                                                    \
        GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_INTERNAL_TRACE,                 \
        ("[%s] I Exiting\n", _gridftp_server_name))
    
#define GlobusGridFTPServerDebugInternalExitWithError()                     \
    GlobusGSDebugPrintf(                                                    \
        GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_INTERNAL_TRACE,                 \
        ("[%s] I Exiting with error\n", _gridftp_server_name))

#define GlobusGridFTPServerDebugVerboseEnter()                              \
    GlobusGSDebugPrintf(                                                    \
        GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_VERBOSE,                        \
        ("[%s] V Entering\n", _gridftp_server_name))

#define GlobusGridFTPServerDebugVerboseExit()                               \
    GlobusGSDebugPrintf(                                                    \
        GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_VERBOSE,                        \
        ("[%s] V Exiting\n", _gridftp_server_name))
    
#define GlobusGridFTPServerDebugVerboseExitWithError()                      \
    GlobusGSDebugPrintf(                                                    \
        GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_VERBOSE,                        \
        ("[%s] V Exiting with error\n", _gridftp_server_name))

#define GlobusGridFTPServerDebugCommand(cmd)                                \
    GlobusGSDebugPrintf(                                                    \
        GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_COMMANDS,                       \
        ("### [%s] Received command: %s\n", _gridftp_server_name, cmd))

struct globus_i_gs_attr_s;

typedef enum globus_i_gsc_debug_levels_e
{ 
    GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_ERROR = 1,
    GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_WARNING = 2,
    GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_TRACE = 4,
    GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_INTERNAL_TRACE = 8,
    GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_COMMANDS = 16,
    GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_VERBOSE = 32,
    GLOBUS_GRIDFTP_SERVER_CONTROL_DEBUG_STATE = 64
} globus_i_gsc_debug_levels_t;

typedef enum globus_i_gsc_error_type_e
{
    GLOBUS_GRIDFTP_SERVER_CONTROL_ERROR_PARAMETER,
    GLOBUS_GRIDFTP_SERVER_CONTROL_ERROR_STATE,
    GLOBUS_GRIDFTP_SERVER_CONTROL_ERROR_MEMORY,
    GLOBUS_GRIDFTP_SERVER_CONTROL_NO_AUTH,
    GLOBUS_GRIDFTP_SERVER_CONTROL_POST_AUTH,
    GLOBUS_GRIDFTP_SERVER_CONTROL_NO_COMMAND,
    GLOBUS_GRIDFTP_SERVER_CONTROL_MALFORMED_COMMAND
} globus_i_gsc_error_type_t;

typedef enum globus_i_gsc_mlsx_fact_e
{
    GLOBUS_GSC_MLSX_FACT_TYPE = 'T',
    GLOBUS_GSC_MLSX_FACT_MODIFY = 'M',
    GLOBUS_GSC_MLSX_FACT_CHARSET = 'C',
    GLOBUS_GSC_MLSX_FACT_SIZE = 'S',
    GLOBUS_GSC_MLSX_FACT_PERM = 'P',
    GLOBUS_GSC_MLSX_FACT_UNIXMODE = 'U',
    GLOBUS_GSC_MLSX_FACT_UNIXOWNER = 'O',
    GLOBUS_GSC_MLSX_FACT_UNIXGROUP = 'G',
    GLOBUS_GSC_MLSX_FACT_UNIQUE = 'Q',
    GLOBUS_GSC_MLSX_FACT_UNIXSLINK = 'L',
    GLOBUS_GSX_MLSX_FACT_UNIXUID = 'I',
    GLOBUS_GSX_MLSX_FACT_UNIXGID = 'D',
    GLOBUS_GSC_MLSX_FACT_XCOUNT = 'N'
} globus_i_gsc_mlsx_fact_t;

typedef enum
{
    GLOBUS_L_GSC_DATA_OBJ_READY = 1,
    GLOBUS_L_GSC_DATA_OBJ_DESTROY_WAIT,
    GLOBUS_L_GSC_DATA_OBJ_DESTROYING,
    GLOBUS_L_GSC_DATA_OBJ_INUSE
} globus_l_gsc_data_obj_state_t;

typedef void
(*globus_i_gsc_auth_cb_t)(
    struct globus_i_gsc_op_s *              op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    void *                                  user_arg);

typedef void
(*globus_i_gsc_resource_cb_t)(
    struct globus_i_gsc_op_s *              op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    char *                                  path,
    globus_gridftp_server_control_stat_t *  stat_info,
    int                                     stat_count,
    uid_t                                   uid,
    void *                                  user_arg);

typedef void
(*globus_i_gsc_passive_cb_t)(
    struct globus_i_gsc_op_s *              op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    const char **                           cs,
    int                                     addr_count,
    void *                                  user_arg);

typedef void
(*globus_i_gsc_port_cb_t)(
    struct globus_i_gsc_op_s *              op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    void *                                  user_arg);

typedef void
(*globus_i_gsc_transfer_cb_t)(
    struct globus_i_gsc_op_s *              op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    void *                                  user_arg);

typedef struct globus_i_gsc_data_s
{
    globus_l_gsc_data_obj_state_t           state;
    struct globus_i_gsc_server_handle_s *   server_handle;
    int                                     stripe_count;
    void *                                  user_handle;
    globus_gridftp_server_control_data_dir_t dir;
    globus_bool_t                           first_use;
} globus_i_gsc_data_t;

typedef enum globus_i_gsc_op_type_e
{
    GLOBUS_L_GSC_OP_TYPE_AUTH,
    GLOBUS_L_GSC_OP_TYPE_RESOURCE,
    GLOBUS_L_GSC_OP_TYPE_CREATE_PASV,
    GLOBUS_L_GSC_OP_TYPE_CREATE_PORT,
    GLOBUS_L_GSC_OP_TYPE_SEND,
    GLOBUS_L_GSC_OP_TYPE_RECV,
    GLOBUS_L_GSC_OP_TYPE_DESTROY,
    GLOBUS_L_GSC_OP_TYPE_LIST,
    GLOBUS_L_GSC_OP_TYPE_NLST,
    GLOBUS_L_GSC_OP_TYPE_MLSD,
    GLOBUS_L_GSC_OP_TYPE_MLSR
} globus_i_gsc_op_type_t;

typedef struct globus_i_gsc_event_data_s
{
    globus_l_gsc_data_obj_state_t           state;
    globus_callback_handle_t                periodic_handle;
    int                                     stripe_count;
    globus_bool_t                           perf_running;

    globus_callback_handle_t                restart_handle;
    globus_bool_t                           restart_running;

    int                                     event_mask;
    globus_gridftp_server_control_event_cb_t user_cb;
    void *                                  user_arg;

    globus_off_t *                          stripe_total;
} globus_i_gsc_event_data_t;

typedef struct globus_i_gsc_handle_opts_s
{
    char                                    mlsx_fact_str[16];
    int                                     parallelism;
    globus_size_t                           send_buf;
    globus_size_t                           receive_buf;
    globus_bool_t                           refresh;
    globus_size_t                           packet_size;
    globus_bool_t                           delayed_passive;
    globus_bool_t                           passive_only;
    int                                     perf_frequency;
    int                                     retr_perf_frequency;
    int                                     restart_frequency;
    globus_gsc_layout_t                     layout;
    globus_size_t                           block_size;
    int                                     mlsr_traversal_options;
} globus_i_gsc_handle_opts_t;

typedef struct globus_i_gsc_module_func_s
{
    char *                                              key;
    globus_gridftp_server_control_transfer_cb_t         func;
    void *                                              user_arg;
} globus_i_gsc_module_func_t;

typedef struct globus_i_gsc_user_funcs_s
{
    globus_hashtable_t                                  send_cb_table;
    globus_hashtable_t                                  recv_cb_table;
    globus_gridftp_server_control_transfer_cb_t         default_send_cb;
    void *                                              default_send_arg;
    globus_gridftp_server_control_transfer_cb_t         default_recv_cb;
    void *                                              default_recv_arg;
    globus_gridftp_server_control_auth_cb_t             auth_cb;
    void *                                              auth_arg;
    globus_gridftp_server_control_passive_connect_cb_t  passive_cb;
    void *                                              passive_arg;
    globus_gridftp_server_control_active_connect_cb_t   active_cb;
    void *                                              active_arg;
    globus_gridftp_server_control_data_destroy_cb_t     data_destroy_cb;
    void *                                              data_destroy_arg;
    globus_gridftp_server_control_list_cb_t             list_cb;
    void *                                              list_arg;
    globus_gridftp_server_control_resource_cb_t         resource_cb;
    void *                                              resource_arg;
    globus_gridftp_server_control_cb_t                  done_cb;
    void *                                              done_arg;
    globus_gridftp_server_control_log_cb_t              log_func;
    int                                                 log_mask;
    void *                                              log_arg;
} globus_i_gsc_user_funcs_t;

typedef struct globus_i_gsc_op_s
{
    globus_i_gsc_op_type_t                  type;

    int                                     ref;
    struct globus_i_gsc_server_handle_s *   server_handle;

    globus_gridftp_server_control_response_t response_type;
    char *                                  response_msg;

    globus_list_t *                         cmd_list;
    globus_bool_t                           done;

    /* stuff for auth */
    globus_bool_t                           authenticated;
    char *                                  username;
    char *                                  password;
    globus_i_gsc_auth_cb_t                  auth_cb;
    globus_i_gsc_resource_cb_t              stat_cb;
    globus_i_gsc_transfer_cb_t              list_cb;

    globus_gridftp_server_control_stat_t *  stat_info;
    int                                     stat_count;
    globus_mutex_t                          stat_lock;
    
    /* stuff for resource */
    int                                     uid;
    int                                     gid_count;
    int *                                   gid_array;
    char *                                  path;
    char *                                  glob_match_str;
    globus_gridftp_server_control_resource_mask_t mask;

    /* stuff for port/pasv */
    char **                                 cs;
    int                                     max_cs;
    int                                     net_prt;
    globus_i_gsc_passive_cb_t               passive_cb;
    globus_i_gsc_port_cb_t                  port_cb;
    globus_i_gsc_transfer_cb_t              transfer_cb;

    char *                                  command;

    /* stuff for transfer */
    char *                                  mod_name;
    char *                                  mod_parms;
    globus_gridftp_server_control_transfer_cb_t user_data_cb;
    globus_bool_t                           transfer_started;

    globus_range_list_t                     range_list;
    globus_range_list_t                     perf_range_list;
    globus_i_gsc_event_data_t               event;

    globus_bool_t                           aborted;
    void *                                  abort_user_arg;
    void *                                  user_arg;
    
    globus_i_gsc_data_t *                   data_destroy_obj;
} globus_i_gsc_op_t;

typedef struct globus_i_gsc_attr_s
{
    int                                     version_ctl;
    char *                                  modes;
    char *                                  types;
    char *                                  base_dir;
    char *                                  post_auth_banner;
    char *                                  pre_auth_banner;
    globus_gridftp_server_control_security_type_t   security;
    globus_bool_t                           epsv_ip;

    int                                     idle_timeout;
    int                                     preauth_timeout;

    globus_i_gsc_user_funcs_t               funcs;
} globus_i_gsc_attr_t;


extern globus_hashtable_t               globus_i_gs_default_attr_command_hash;

/*
 *  internal functions for adding commands.
 */

/*
 *   959 Structures
 */
typedef enum globus_l_gsc_state_e
{
    GLOBUS_L_GSC_STATE_NONE,
    GLOBUS_L_GSC_STATE_OPENING,
    GLOBUS_L_GSC_STATE_OPEN,
    GLOBUS_L_GSC_STATE_PROCESSING,
    GLOBUS_L_GSC_STATE_ABORTING,
    GLOBUS_L_GSC_STATE_ABORTING_STOPPING,
    GLOBUS_L_GSC_STATE_STOPPING,
    GLOBUS_L_GSC_STATE_STOPPED
} globus_l_gsc_state_t;


typedef struct globus_i_gsc_cmd_wrapper_s
{
    globus_i_gsc_op_t *                     op;
    char *                                  strarg;
    char *                                  mod_name;
    char *                                  mod_parms;
    char *                                  path;

    globus_bool_t                           transfer_flag;
    int                                     dc_parsing_alg;
    int                                     max;
    globus_gridftp_server_control_network_protocol_t prt;

    globus_i_gsc_op_type_t                  type;
    int                                     cmd_ndx;

    char **                                 cs;
    int                                     cs_count;
    int                                     reply_code;
    
    struct globus_i_gsc_cmd_wrapper_s *     transfer_info;
} globus_i_gsc_cmd_wrapper_t;

/* the server handle */
typedef struct globus_i_gsc_server_handle_s
{
    int                                 version_ctl;

    globus_mutex_t                      mutex;

    /*
     *  authentication information
     */
    int                                 ref;
    globus_bool_t                       timeout;

    char *                              username;
    char *                              pw;
    char *                              subject;
    char                                dcau;
    char *                              dcau_subject;
    char                                prot;
    globus_bool_t                       authenticated;

    char *                              post_auth_banner;
    char *                              pre_auth_banner;

    gss_ctx_id_t                        context;
    gss_cred_id_t                       cred;
    gss_cred_id_t                       del_cred;
    globus_gridftp_server_control_security_type_t   security_type;

    /*
     *  state information  
     */
    char *                              cwd;
    char *                              default_cwd;    
    char                                type;
    char                                mode;
    char *                              modes;
    char *                              types;
    int                                 stripe_count;
    char *				lang;

    globus_off_t                        allocated_bytes;

    /* force failure on this command */
    char *                              fault_cmd;
    
    /* opts state */
    globus_i_gsc_handle_opts_t          opts;

    /*
     *  user function pointers
     */
    globus_range_list_t                 range_list;

    globus_i_gsc_user_funcs_t           funcs;

    globus_i_gsc_data_t *               data_object;

    globus_result_t                     cached_res;
    globus_list_t *                     feature_list;

    /* 
     *  read.c members 
     */
    globus_list_t *                     all_cmd_list;
    globus_bool_t                       reply_outstanding;
    globus_xio_handle_t                 xio_handle;
    globus_l_gsc_state_t                state;
    globus_fifo_t                       read_q;
    globus_fifo_t                       reply_q;
    int                                 abort_cnt;
    globus_hashtable_t                  cmd_table;
    globus_hashtable_t                  site_cmd_table;
    globus_hashtable_t                  data_object_table;
    struct globus_i_gsc_op_s *          outstanding_op;

    globus_bool_t                       terminating;

    int                                 idle_timeout;
    int                                 preauth_timeout;
    
    globus_i_gsc_cmd_wrapper_t *        pasv_info;
    globus_bool_t                       epsv_ip;

    globus_bool_t                       q_backup;
    int                                 max_q_len;
} globus_i_gsc_server_handle_t;


void
globus_i_gsc_reverse_restart(
    globus_range_list_t                 in_range,
    globus_range_list_t                 out_range);

void
globus_i_gsc_op_destroy(
    globus_i_gsc_op_t *                 op);

void
globus_i_gsc_event_start(
    globus_i_gsc_op_t *                 op,
    int                                 event_mask,
    globus_gridftp_server_control_event_cb_t event_cb,
    void *                              user_arg);

void
globus_i_gsc_event_end(
    globus_i_gsc_op_t *                 op);

char *
globus_i_gsc_get_help(
    globus_i_gsc_server_handle_t *      server_handle,
    const char *                        command_name);

globus_result_t
globus_i_gsc_intermediate_reply(
    globus_i_gsc_op_t *                 op,
    char *                              reply_msg);

globus_result_t
globus_i_gsc_authenticate(
    globus_i_gsc_op_t *                 op,
    const char *                        user,
    const char *                        pass,
    globus_i_gsc_auth_cb_t              cb,
    void *                              user_arg);

globus_result_t
globus_i_gsc_list(
    globus_i_gsc_op_t *                 op,
    const char *                        path,
    globus_gridftp_server_control_resource_mask_t mask,
    globus_i_gsc_op_type_t              type,
    globus_i_gsc_transfer_cb_t          list_cb,
    void *                              user_arg);

globus_result_t
globus_i_gsc_resource_query(
    globus_i_gsc_op_t *                 op,
    const char *                        path,
    globus_gridftp_server_control_resource_mask_t mask,
    globus_i_gsc_resource_cb_t          cb,
    void *                              user_arg);

globus_result_t
globus_i_gsc_passive(
    globus_i_gsc_op_t *                 op,
    int                                 max,
    int                                 net_prt,
    const char *                        pathname,
    globus_i_gsc_passive_cb_t           cb,
    void *                              user_arg);

globus_result_t
globus_i_gsc_port(
    globus_i_gsc_op_t *                 op,
    const char **                       contact_strings,
    int                                 stripe_count,
    int                                 net_prt,
    globus_i_gsc_port_cb_t              cb,
    void *                              user_arg);

globus_result_t
globus_i_gsc_send(
    globus_i_gsc_op_t *                 op,
    const char *                        path,
    const char *                        mod_name,
    const char *                        mod_parms,
    globus_i_gsc_transfer_cb_t          data_cb,
    void *                              user_arg);

globus_result_t
globus_i_gsc_recv(
    globus_i_gsc_op_t *                 op,
    const char *                        path,
    const char *                        mod_name,
    const char *                        mod_parms,
    globus_i_gsc_transfer_cb_t          data_cb,
    void *                              user_arg);

void
globus_i_gsc_add_commands(
    globus_i_gsc_server_handle_t *      server_handle);

globus_result_t
globus_i_gsc_command_panic(
    globus_i_gsc_op_t *                 op);

char *
globus_i_gsc_concat_path(
    globus_i_gsc_server_handle_t *      i_server,
    const char *                        in_path);

char *
globus_i_gsc_list_single_line(
    globus_gridftp_server_control_stat_t *  stat_info);

char *
globus_i_gsc_list_line(
    globus_gridftp_server_control_stat_t *  stat_info,
    int                                 stat_count,
    const char *                        glob_match_str);

char *
globus_i_gsc_nlst_line(
    globus_gridftp_server_control_stat_t *  stat_info,
    int                                 stat_count);

char *
globus_i_gsc_mlsx_line_single(
    const char *                        mlsx_fact_str,
    int                                 uid,
    globus_gridftp_server_control_stat_t *  stat_info,
    const char *                        base_path,
    globus_bool_t                       mlst);

char *
globus_i_gsc_mlsx_line(
    globus_gridftp_server_control_stat_t *  stat_info,
    int                                 stat_count,
    const char *                        mlsx_fact_string,
    uid_t                               uid,
    const char *                        base_path,
    globus_bool_t                       indent);

void
globus_i_guc_command_data_destroy(
    globus_i_gsc_server_handle_t *      server_handle);

void
globus_i_gsc_log(
    globus_i_gsc_server_handle_t *      server_handle,
    const char *                        command,
    int                                 mask);

globus_bool_t
globus_i_guc_data_object_destroy(
    globus_i_gsc_server_handle_t *      server_handle,
    globus_i_gsc_data_t *               data_object);

globus_result_t
globus_i_gsc_cmd_intermediate_reply(
    globus_i_gsc_op_t *                 op,
    char *                              reply_msg);

void
globus_i_gsc_event_start_perf_restart(
    globus_i_gsc_op_t *                 op);

void
globus_gsc_959_terminate(
    globus_i_gsc_op_t *                 op,
    char *                              reply_msg);

#endif
