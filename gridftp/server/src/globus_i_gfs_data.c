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
#include "globus_gsi_credential.h"
/* provides local_extensions */
#include "extensions.h"
#include <unistd.h>
#include <openssl/des.h>

#ifndef TARGET_ARCH_WIN32
#include <pwd.h>
#include <grp.h>
#include <fnmatch.h>
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
#define fnmatch(a,b,c) -1
#endif

#ifdef TARGET_ARCH_WIN32

#define lstat(x,y) stat(x,y)
#define S_ISLNK(x) 0

#define getuid() 1
#define getgid() 1
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

#include "globus_io.h"
#include "globus_xio.h"
#include <openssl/hmac.h>

#include "globus_xio_http.h"

#define FTP_SERVICE_NAME "file"
#define USER_NAME_MAX   64

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#define GFSDataOpDec(_op, _d_op, _d_s)                                  \
do                                                                      \
{                                                                       \
    _op->ref--;                                                         \
    if(_op->ref == 0)                                                   \
    {                                                                   \
        _d_op = GLOBUS_TRUE;                                            \
        _op->session_handle->ref--;                                     \
        if(_op->session_handle->ref == 0)                               \
        {                                                               \
            _d_s = GLOBUS_TRUE;                                         \
        }                                                               \
    }                                                                   \
} while(0)

#ifdef WIN32
#define DriveLetterToWin(_p_path)                                       \
    do                                                                  \
    {                                                                   \
        if(_p_path)                                                     \
        {                                                               \
            if(isalpha(_p_path[1]) && _p_path[2] == '/')                \
            {                                                           \
                _p_path[0] = _p_path[1];                                \
                _p_path[1] = ':';                                       \
            }                                                           \
            else if(isalpha(_p_path[1]) && _p_path[2] == '\0')          \
            {                                                           \
                char                        _driveletter = _p_path[1];  \
                free(_p_path);                                          \
                _p_path = globus_malloc(4);                             \
                _p_path[0] = _driveletter;                              \
                _p_path[1] = ':';                                       \
                _p_path[2] = '/';                                       \
                _p_path[3] = '\0';                                      \
            }                                                           \
        }                                                               \
    } while(0)
#else
#define DriveLetterToWin(_p_path)
#endif


struct passwd *                         globus_l_gfs_data_pwent = NULL;
static globus_gfs_storage_iface_t *     globus_l_gfs_dsi = NULL;
static globus_gfs_storage_iface_t *     globus_l_gfs_dsi_hybrid = NULL;
globus_extension_registry_t             globus_i_gfs_dsi_registry;
static char *                           globus_l_gfs_active_dsi_name = NULL;
static globus_extension_handle_t        globus_l_gfs_active_dsi_handle = NULL;
static globus_bool_t                    globus_l_gfs_data_is_remote_node = GLOBUS_FALSE;
globus_off_t                            globus_l_gfs_bytes_transferred;
globus_mutex_t                          globus_l_gfs_global_counter_lock;
globus_extension_registry_t             globus_i_gfs_acl_registry;

static globus_mutex_t                   gfs_l_data_brain_mutex;
static globus_list_t *                  gfs_l_data_brain_ready_list = NULL;
static globus_bool_t                    gfs_l_data_brain_ready = GLOBUS_FALSE;

static globus_hashtable_t               gfs_l_data_net_allowed_drivers;
static globus_hashtable_t               gfs_l_data_disk_allowed_drivers;
static globus_list_t *                  globus_l_gfs_path_alias_list_base = NULL;
static globus_list_t *                  globus_l_gfs_path_alias_list_sharing = NULL;
static int                              globus_l_gfs_op_info_ctr = 1;
static globus_xio_driver_t              globus_l_gfs_udt_driver_preload = NULL;
static globus_xio_driver_t              globus_l_gfs_netmgr_driver = NULL;
static int                              globus_l_gfs_watchdog_limit = 0;
static globus_xio_driver_t              gfs_l_tcp_driver = NULL;
static globus_xio_driver_t              gfs_l_gsi_driver = NULL;
static globus_xio_driver_t              gfs_l_q_driver = NULL;

typedef enum
{
    GLOBUS_L_GFS_DATA_REQUESTING = 1,
    GLOBUS_L_GFS_DATA_CONNECTING,
    GLOBUS_L_GFS_DATA_CONNECT_CB,
    GLOBUS_L_GFS_DATA_CONNECTED,
    GLOBUS_L_GFS_DATA_ABORTING,
    GLOBUS_L_GFS_DATA_ABORT_CLOSING,
    GLOBUS_L_GFS_DATA_FINISH,
    GLOBUS_L_GFS_DATA_FINISH_WITH_ERROR,
    GLOBUS_L_GFS_DATA_COMPLETING,
    GLOBUS_L_GFS_DATA_COMPLETE
} globus_l_gfs_data_state_t;

typedef enum
{
    GLOBUS_L_GFS_DATA_HANDLE_VALID = 1,
    GLOBUS_L_GFS_DATA_HANDLE_INUSE,
    GLOBUS_L_GFS_DATA_HANDLE_CLOSING,
    GLOBUS_L_GFS_DATA_HANDLE_TE_VALID,
    GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED,
    GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_AND_DESTROYED,
    GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED,
    GLOBUS_L_GFS_DATA_HANDLE_CLOSED_AND_DESTROYED,
    GLOBUS_L_GFS_DATA_HANDLE_CLOSED
} globus_l_gfs_data_handle_state_t;

typedef enum
{
    GLOBUS_L_GFS_DATA_INFO_TYPE_COMMAND = 1,
    GLOBUS_L_GFS_DATA_INFO_TYPE_PASSIVE,
    GLOBUS_L_GFS_DATA_INFO_TYPE_ACTIVE,
    GLOBUS_L_GFS_DATA_INFO_TYPE_STAT,
    GLOBUS_L_GFS_DATA_INFO_TYPE_SEND,
    GLOBUS_L_GFS_DATA_INFO_TYPE_RECV,
    GLOBUS_L_GFS_DATA_INFO_TYPE_LIST
} globus_l_gfs_data_info_type_t;

typedef struct globus_l_gfs_data_path_list_s
{
    char*                                   pathname;
    char*                                   subpath;
    globus_bool_t                           has_cycle;
    struct globus_l_gfs_data_path_list_s*   next;
} globus_l_gfs_data_path_list_t;

typedef struct
{
    globus_gfs_operation_t   op;

    union
    {
        globus_gridftp_server_write_cb_t write;
        globus_gridftp_server_read_cb_t  read;
    } callback;
    void *                              user_arg;
    globus_gfs_finished_info_t *        finished_info;
    globus_byte_t *                     list_response;
    globus_bool_t                       free_buffer;
    globus_bool_t                       final;
} globus_l_gfs_data_bounce_t;

typedef struct 
{
    char *                              all;
    
    char *                              modify;
    globus_bool_t                       modify_seen;
    char *                              checksum_md5;
    globus_bool_t                       checksum_md5_seen;
} globus_l_gfs_storattr_t;

typedef struct
{
    globus_i_gfs_acl_handle_t           acl_handle;

    gss_cred_id_t                       del_cred;
    gss_ctx_id_t                        context;
    char *                              subject;
    char *                              client_ip;
    char *                              username;
    char *                              real_username;
    char *                              home_dir;
    char *                              true_home;
    char *                              chroot_path;
    uid_t                               uid;
    gid_t                               gid;
    int                                 gid_count;
    gid_t *                             gid_array;

    globus_gfs_session_info_t *         session_info_copy;
    globus_bool_t                       hybrid;

    void *                              session_arg;
    void *                              data_handle;
    globus_mutex_t                      mutex;
    int                                 ref;
    globus_gfs_storage_iface_t *        dsi;
    globus_extension_handle_t           dsi_handle;
    char *                              dsi_data;
    char *                              dsi_data_global;

    char *                              mod_dsi_name;
    globus_gfs_storage_iface_t *        mod_dsi;
    globus_extension_handle_t           mod_dsi_handle;

    globus_handle_table_t               handle_table;
    int                                 node_ndx;
    globus_list_t *                     net_stack_list;
    globus_list_t *                     disk_stack_list;
    char *                              client_appname;
    char *                              client_appver;
    char *                              client_scheme;
    gss_cred_id_t                       dcsc_cred;
    
    globus_bool_t                       upas;
    globus_ftp_control_handle_t         udt_data_channel;
    globus_bool_t                       udt_data_channel_inuse;
    
    globus_list_t **                    active_rp_list;
    globus_list_t *                     rp_list;
    
    globus_bool_t                       sharing;
    char *                              sharing_state_dir;
    char *                              sharing_id;
    char *                              sharing_sharee;
    
    char *                              taskid;
    
    char *                              s3id;
    char *                              s3key;
    gss_cred_id_t                       http_cred;
    char *                              http_ca_certs;
    globus_bool_t                       http_config_called;
    globus_xio_handle_t                 http_handle;
    globus_xio_stack_t                  http_stack;
    globus_xio_stack_t                  https_stack;
    globus_xio_driver_t                 http_driver;

    char *                              storattr_str;
    
    globus_bool_t                       order_data;
    
    int                                 last_active;
    globus_off_t                        watch_updates;
    globus_bool_t                       watch;
    globus_bool_t                       watch_aborted;
    char *                              watch_op;
    globus_callback_handle_t            watch_handle;
    
    globus_hashtable_t                  custom_cmd_table;
} globus_l_gfs_data_session_t;

typedef struct
{
    globus_l_gfs_data_session_t *       session_handle;
    globus_l_gfs_data_handle_state_t    state;
    globus_gfs_data_info_t              info;
    globus_ftp_control_handle_t         data_channel;
    void *                              remote_data_arg;
    globus_bool_t                       is_mine;
    globus_gfs_operation_t              outstanding_op;
    globus_bool_t                       destroy_requested;
    globus_bool_t                       use_interface;
    globus_xio_handle_t                 http_handle;
    globus_xio_attr_t                   xio_attr;
    globus_off_t                        http_length;
    globus_off_t                        http_transferred;
    char *                              http_response_str;
    globus_callback_handle_t            perf_handle;

} globus_l_gfs_data_handle_t;

typedef struct globus_l_gfs_data_operation_s
{
    globus_l_gfs_data_state_t           state;
    globus_bool_t                       writing;
    globus_l_gfs_data_handle_t *        data_handle;
    void *                              data_arg;
    struct timeval                      start_timeval;
    char *                              remote_ip;

    globus_l_gfs_data_session_t *       session_handle;
    void *                              info_struct;
    globus_l_gfs_data_info_type_t       type;

    int                                 id;
    int                                 op_info_id;
    globus_gfs_ipc_handle_t             ipc_handle;

    uid_t                               uid;
    /* transfer stuff */
    globus_range_list_t                 range_list;
    globus_off_t                        partial_offset;
    globus_off_t                        partial_length;
    const char *                        list_type;
    int                                 list_depth;
    int                                 traversal_options;
    globus_result_t                     delayed_error;
    
    char *                              user_msg;
    int                                 user_code;

    globus_off_t                        bytes_transferred;
    globus_off_t                        max_offset;
    globus_off_t                        recvd_bytes;
    globus_range_list_t                 recvd_ranges;
    int                                 retr_markers;
    
    globus_l_gfs_data_path_list_t *     path_list;
    globus_l_gfs_data_path_list_t *     current_path;
    globus_l_gfs_data_path_list_t *     root_paths;

    int                                 nstreams;
    int                                 stripe_count;
    int *                               eof_count;
    globus_bool_t                       eof_ready;
    int                                 node_count;
    int                                 node_ndx;
    int                                 write_stripe;

    int                                 stripe_connections_pending;

    /* used to shift the offset from the dsi due to partial/restart */
    globus_off_t                        write_delta;
    /* used to shift the offset from the dsi due to partial/restart */
    globus_off_t                        transfer_delta;
    int                                 stripe_chunk;
    globus_range_list_t                 stripe_range_list;

    /* command stuff */
    globus_gfs_command_type_t           command;
    char *                              pathname;
    globus_off_t                        cksm_offset;
    globus_off_t                        cksm_length;
    char *                              cksm_alg;
    char *                              cksm_response;
    mode_t                              chmod_mode;
    char *                              chgrp_group;
    time_t                              utime_time;
    char *                              from_pathname;
    /**/

    globus_l_gfs_storattr_t *           storattr;
    
    char *                              http_response_str;

    int                                 update_interval;
    
    void *                              event_arg;
    int                                 event_mask;

    globus_i_gfs_data_callback_t        callback;
    globus_i_gfs_data_event_callback_t  event_callback;
    void *                              user_arg;

    int                                 ref;
    globus_result_t                     cached_res;

    globus_gfs_storage_iface_t *        dsi;
    int                                 sent_partial_eof;

    void *                              stat_wrapper;
    globus_bool_t                       final_stat;
    globus_bool_t                       begin_called;
    globus_off_t                        list_buffer_offset;
    globus_mutex_t                      stat_lock;

    void *                              hybrid_op;
    /* sort of a state cheat.  for case where:
        start_abort
            -- connecting to abort_closing
            -- waiting on abort_cb
        globus_gridftp_server_finished_transfer
            -- connecting to finished
        begin_cb
            -- since in finished we kickout atransfer end
        abort_cb
            -- kicks out transfer end since in finished
    */
    globus_bool_t                       finished_delayed;
    globus_bool_t                       connect_failed;

    globus_bool_t                       order_data;
    globus_off_t                        order_data_start;
} globus_l_gfs_data_operation_t;

typedef struct
{
    globus_l_gfs_data_operation_t *     op;
    int                                 event_type;
} globus_l_gfs_data_trev_bounce_t;

typedef struct
{
    globus_l_gfs_data_operation_t *     op;
    globus_gfs_finished_info_t          reply;
} globus_l_gfs_data_cmd_bounce_t;

typedef struct
{
    globus_result_t                     result;
    globus_gfs_ipc_handle_t             ipc_handle;
    int                                 id;
    globus_l_gfs_data_handle_t *        handle;
    globus_bool_t                       bi_directional;
    globus_i_gfs_data_callback_t        callback;
    void *                              user_arg;
} globus_l_gfs_data_active_bounce_t;

typedef struct
{
    globus_gfs_ipc_handle_t             ipc_handle;
    int                                 id;
    globus_l_gfs_data_handle_t *        handle;
    globus_bool_t                       bi_directional;
    char *                              contact_string;
    globus_i_gfs_data_callback_t        callback;
    void *                              user_arg;
    globus_result_t                     result;
} globus_l_gfs_data_passive_bounce_t;

typedef struct
{
    globus_l_gfs_data_operation_t *     op;
    globus_object_t *                   error;
    int                                 stat_count;
    globus_gfs_stat_t *                 stat_array;
    globus_byte_t *                     list_response;
    globus_bool_t                       free_buffer;
    globus_bool_t                       custom_list;
    globus_bool_t                       final_stat;
} globus_l_gfs_data_stat_bounce_t;

typedef struct
{
    globus_l_gfs_data_operation_t *     op;
    globus_l_gfs_data_handle_t *        data_handle;
    globus_xio_handle_t                 http_handle;
    globus_xio_data_descriptor_t        http_dd;
    globus_byte_t                       buffer[1];
} globus_l_gfs_data_http_bounce_t;

typedef struct
{
    char *                              alias;
    size_t                              alias_len;
    char *                              realpath;
    size_t                              realpath_len;
    int                                 access;
} globus_l_gfs_alias_ent_t;

static
void
globus_l_gfs_data_end_transfer_kickout(
    void *                              user_arg);

static
void
globus_l_gfs_data_start_abort(
    globus_l_gfs_data_operation_t *     op);

static
void
globus_l_gfs_data_handle_free(
    globus_l_gfs_data_handle_t *    data_handle);

static
void
globus_l_gfs_free_session_handle(
    globus_l_gfs_data_session_t *       session_handle);

static
void
globus_l_gfs_data_write_eof_cb(
    void *                              user_arg,
    globus_ftp_control_handle_t *       handle,
    globus_object_t *                   error,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_off_t                        offset,
    globus_bool_t                       eof);

static
void
globus_l_gfs_data_list_stat_cb(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg);

static
void
globus_l_gfs_data_end_read_kickout(
    void *                              user_arg);

static
void
globus_l_gfs_data_abort_fc_cb(
    void *                              callback_arg,
    globus_ftp_control_handle_t *       ftp_handle,
    globus_object_t *                   error);

static
void
globus_l_gfs_data_abort_kickout(
    void *                              user_arg);

static
void
globus_l_gfs_data_active_kickout(
    void *                              user_arg);
    
static
void
globus_l_gfs_data_passive_kickout(
    void *                              user_arg);

static
void
globus_l_gfs_data_operation_destroy(
    globus_l_gfs_data_operation_t *     op);

static
void
globus_l_gfs_data_update_restricted_paths_symlinks(
    globus_l_gfs_data_session_t *   session_handle,
    globus_list_t **                rp_list);

static
void
globus_l_gfs_data_brain_ready_delay_cb(
    void *                              user_arg);

void
globus_i_gfs_data_http_read_cb(
    globus_xio_handle_t                 xio_handle, 
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_size_t                       nbytes, 
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);
void
globus_i_gfs_data_http_write_cb(
    globus_xio_handle_t                 xio_handle, 
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_size_t                       nbytes, 
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

globus_result_t 
globus_i_gfs_data_http_get(
    globus_l_gfs_data_operation_t *     op,
    char *                              path,
    char *                              request,
    globus_off_t                        offset,
    globus_off_t                        length,
    globus_bool_t                       do_retry);

globus_result_t
globus_i_gfs_data_http_put(
    globus_l_gfs_data_operation_t *     op,
    char *                              path,
    char *                              request,
    globus_off_t                        offset,
    globus_off_t                        length,
    globus_bool_t                       do_retry);

globus_result_t
globus_i_gfs_data_http_parse_args(
    char *                              argstring,
    char **                             path,
    char **                             request,
    globus_off_t *                      offset,
    globus_off_t *                      length);

static globus_result_t
globus_l_gfs_base64_encode(
    const unsigned char *               inbuf,
    globus_size_t                       in_len,
    globus_byte_t *                     outbuf,
    globus_size_t *                     out_len);

static
globus_result_t
globus_i_gfs_data_http_print_response(
    int                                 response_code,
    globus_hashtable_t *                header_table,
    globus_byte_t *                     body,
    char **                             out_msg)
{
    globus_list_t *                     header_list = NULL;
    char *                              header_str = NULL;
    char *                              header_tmp;
    globus_result_t                     result;
    int                                 rc;
    globus_xio_http_header_t *          header;
    char *                              b64_header = NULL;
    char *                              b64_body = NULL;
    int                                 body_len = 0;
    
    rc = globus_hashtable_to_list(header_table, &header_list);

    header_str = strdup("");
    while(!globus_list_empty(header_list))
    {
        header = globus_list_remove(&header_list, header_list);
        header_tmp = globus_common_create_string(
            "%s%s: %s\r\n", header_str, header->name, header->value);
        globus_free(header_str);
        header_str = header_tmp;
    }

    if(body && (body_len = strlen((char *) body)) > 0)
    {
        b64_body = malloc(body_len * 4 / 3 + 4);
        result = globus_l_gfs_base64_encode(
            body, body_len, (globus_byte_t *) b64_body, NULL);
        globus_assert(result == GLOBUS_SUCCESS);
    }
    
    b64_header = malloc(strlen(header_str) * 4 / 3 + 4);
    result = globus_l_gfs_base64_encode(
        (globus_byte_t *) header_str, strlen(header_str), 
        (globus_byte_t *) b64_header, NULL);
    globus_assert(result == GLOBUS_SUCCESS);

    if(b64_body)
    {
        *out_msg = globus_common_create_string(
            "\n"
            "{\n"
            "  \"http.response.code\": %d,\n"
            "  \"http.response.headers\": \"%s\",\n"
            "  \"http.response.body\": \"%s\"\n"
            "}\n",
            response_code,
            b64_header,
            b64_body);
    }
    else
    {
        *out_msg = globus_common_create_string(
            "\n"
            "{\n"
            "  \"http.response.code\": %d,\n"
            "  \"http.response.headers\": \"%s\"\n"
            "}\n",
            response_code,
            b64_header);
    }
            
    globus_free(header_str);        
    globus_free(b64_header);
    if(b64_body)
    {
        globus_free(b64_body);
    }

    return GLOBUS_SUCCESS;
}


static char                             globus_l_gfs_base64_pad = '=';
static char *                           globus_l_gfs_base64_n =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static 
globus_result_t
globus_l_gfs_base64_decode(
    const unsigned char  *              inbuf,
    globus_byte_t *                     outbuf,
    globus_size_t *                     out_len)
{
    int                                 i;
    int                                 j;
    int                                 D;
    char *                              p;
    GlobusGFSName(globus_l_gfs_base64_decode);
    GlobusGFSDebugEnter();

    for(i=0, j=0; inbuf[i] && inbuf[i] != globus_l_gfs_base64_pad; i++)
    {
        if((p = strchr(globus_l_gfs_base64_n, inbuf[i])) == NULL)
        {
	    goto err;
        }
        D = p - globus_l_gfs_base64_n;
        switch(i&3)
        {
            case 0:
                outbuf[j] = D<<2;
                break;
            case 1:
                outbuf[j++] |= D>>4;
                outbuf[j] = (D&15)<<4;
                break;
            case 2:
                outbuf[j++] |= D>>2;
                outbuf[j] = (D&3)<<6;
                break;
            case 3:
                outbuf[j++] |= D;
                break;
            default:
                break;
        }
    }

    switch(i&3)
    {
        case 1:
	    goto err;
 
       case 2:
            if(D&15)
            {
	        goto err;
            }
            if(strcmp((char *) &inbuf[i], "=="))
            {
	        goto err;
            }
            break;

        case 3:
            if(D&3)
            {
	        goto err;
            }
            if(strcmp((char *) &inbuf[i], "="))
            {
	        goto err;
            }
            break;

        default:
            break;
    }
    *out_len = j;

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

err:
    GlobusGFSDebugExitWithError();
    return GlobusGFSErrorGeneric("Invalid base64 input");

}

char *
globus_l_gfs_defaulthome()
{
    char *                              home_dir;
#ifdef WIN32   
    char *                              ptr;

    if(getenv("HOMEDRIVE") && getenv("HOMEPATH"))
    {
        home_dir = globus_common_create_string(
            "%s%s", getenv("HOMEDRIVE"), getenv("HOMEPATH"));
    }
    else if(getenv("USERPROFILE"))
    {
        home_dir = globus_common_create_string("%s", getenv("USERPROFILE"));
    }
    else
    {
         home_dir = globus_libc_strdup("C:/");
    }

    ptr = home_dir;
    while(ptr && *ptr)
    {
        if(*ptr == '\\')
        {
            *ptr = '/';
        }
        ptr++;
    }
#else
    home_dir = globus_libc_strdup("/");
#endif

    return home_dir;
}

/* Returns GLOBUS_SUCCESS if pwent passes various sanity checks
 * for disabled accounts.  Returns an error globus_result_t otherwise.
 * Assumes caller has checked value of pw (i.e. pw is not NULL).
 */
 
static
globus_result_t
globus_l_gfs_validate_pwent(
    struct passwd *                     pw)
{
    struct stat                         statbuf;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_validate_pwent);
    GlobusGFSDebugEnter();

    /* shell exists? */
    if(pw->pw_shell[0] == '\0' || stat(pw->pw_shell,&statbuf) != 0)
    {
        result = GlobusGFSErrorGeneric("shell does not exist");
        goto err;
    }

    /* shell is a regular file? */
    if(!S_ISREG(statbuf.st_mode))
    {
        GlobusGFSErrorGenericStr(result,
            ("shell is not a regular file: %s", pw->pw_shell));
        goto err;
    }
    
    /* shell executable? */
    if((statbuf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0)
    {
        GlobusGFSErrorGenericStr(result,
            ("shell is not executable: %s", pw->pw_shell));
        goto err;
    }

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;
    
err:
    GlobusGFSDebugExitWithError();
    return result;
}

void
globus_l_gfs_data_brain_ready(
    void *                              user_arg)
{
    void *                              arg;
    globus_list_t *                     list;

    globus_mutex_lock(&gfs_l_data_brain_mutex);
    {
        gfs_l_data_brain_ready = GLOBUS_TRUE;
        list = gfs_l_data_brain_ready_list;
        gfs_l_data_brain_ready_list = NULL;
    }
    globus_mutex_unlock(&gfs_l_data_brain_mutex);

    while(!globus_list_empty(list))
    {
        arg = globus_list_remove(&list, list);

        globus_l_gfs_data_brain_ready_delay_cb(arg);
    }
}

static
void *
globus_l_gfs_data_check(
    globus_l_gfs_data_session_t *       session_handle,
    globus_l_gfs_data_handle_t *        data_handle)
{
    void *                              remote_data_arg = NULL;

    if(data_handle == NULL)
    {
        return NULL;
    }

    /* need to hold something to make sure that it is safe to even check
        so far we are setting to CLOSED too soon

        XXX race debug note */

            /* there is a problem here.  if i dec the session_handle count
                and it DOES NOT reach 0, that means that once i unlock the
                session_stop call could happen.  this would bump it down to
                zero and destroy it, which is a big problem because i am
                going to use the mutex again later in this function.

                in short the problem is that i am decing the count too early.
                however, i have to do it hear to get destroy_op and other
                values.  therefore to cheat i am going to just bump it back up.
                I know, i know.  ugly.  fixing it properly would involve
                re-evaulating the GFSDataOpDec() macro everwhere.
            */

    switch(data_handle->state)
    {
        case GLOBUS_L_GFS_DATA_HANDLE_INUSE:
        case GLOBUS_L_GFS_DATA_HANDLE_CLOSING:
        case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED:
        case GLOBUS_L_GFS_DATA_HANDLE_CLOSED:
            break;

        case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_AND_DESTROYED:
            data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED;
        case GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED:
            if(!data_handle->is_mine)
            {

                /* this provides the flag making it ok to destroy */
                remote_data_arg = data_handle->remote_data_arg;
            }
            if(remote_data_arg != NULL)
            {
                session_handle->ref++;
            }
            break;

        case GLOBUS_L_GFS_DATA_HANDLE_TE_VALID:
            break;
        case GLOBUS_L_GFS_DATA_HANDLE_VALID:
        default:
            /*globus_assert(0 && "possible memory corruption"); */
            break;
    }

    return remote_data_arg;
}

static
void
globus_l_gfs_data_watchdog_check(
    void *                              arg)
{
    globus_l_gfs_data_session_t *       session_handle;
    
    session_handle = (globus_l_gfs_data_session_t *) arg;
    
    if(!session_handle)
    {
        globus_gfs_log_message(GLOBUS_GFS_LOG_ERR,
            "Forcefully terminating process.  No exit after session stop.\n");
        exit(1);
    }
    else if(session_handle->watch && session_handle->watch_aborted)
    {
        if(time(NULL) > 
            session_handle->last_active + globus_l_gfs_watchdog_limit)
        {
            char * msg = globus_common_create_string(
                "421 Forcefully terminating process.  %s stalled after %"GLOBUS_OFF_T_FORMAT" updates.\n",
                session_handle->watch_op, session_handle->watch_updates);
            globus_i_gfs_control_end_421(msg);
            globus_gfs_log_message(GLOBUS_GFS_LOG_ERR, "%s", msg);
            exit(1);
        }
    }
}

static
void
globus_l_gfs_data_alive(
    globus_l_gfs_data_session_t *       session_handle)
{
    session_handle->last_active = time(NULL);
    session_handle->watch_updates++;
}

void
globus_l_gfs_data_reset_watchdog(
    globus_l_gfs_data_session_t *       session_handle,
    char *                              operation)
{
    if(globus_l_gfs_watchdog_limit)
    {
        session_handle->last_active = time(NULL);
        session_handle->watch = operation ? GLOBUS_TRUE : GLOBUS_FALSE;
        session_handle->watch_op = operation;
        session_handle->watch_updates = 0;
        session_handle->watch_aborted = GLOBUS_FALSE;
        
        if(session_handle->watch)
        {
            if(session_handle->watch_handle == 0)
            {
                globus_reltime_t                timer;
                GlobusTimeReltimeSet(timer, globus_l_gfs_watchdog_limit / 4, 0);
                globus_callback_register_periodic(
                    &session_handle->watch_handle,
                    &timer,
                    &timer,
                    globus_l_gfs_data_watchdog_check,
                    (void *) session_handle);
            }
        }
        else if(session_handle->watch_handle != 0)
        {
            globus_callback_unregister(session_handle->watch_handle, NULL, NULL, NULL);
            session_handle->watch_handle = 0;
        }
    }
}

static
void
globus_l_gfs_data_fire_cb(
    globus_l_gfs_data_operation_t *     op,
    void *                              remote_data_arg,
    globus_bool_t                       free_session)
{
    if(remote_data_arg != NULL)
    {
        /* if i have something to call i should not be freeing */
        globus_assert(!op->data_handle->is_mine);
        globus_assert(op->session_handle->ref > 0);
        globus_assert(!free_session);

        if(op->session_handle->dsi->data_destroy_func != NULL)
        {
            op->session_handle->dsi->data_destroy_func(
                remote_data_arg,
                op->session_handle->session_arg);
        }
        else
        {
            /* XXX dsi impl error, what to do? */
        }
        globus_l_gfs_data_handle_free(op->data_handle);

        globus_mutex_lock(&op->session_handle->mutex);
        {
            op->session_handle->ref--;
            if(op->session_handle->ref == 0)
            {
                free_session = GLOBUS_TRUE;
            }
        }
        globus_mutex_unlock(&op->session_handle->mutex);
    }

    if(free_session)
    {
        if(op->session_handle->dsi->destroy_func != NULL &&
            op->session_handle->session_arg)
        {
            op->session_handle->dsi->destroy_func(
                op->session_handle->session_arg);
        }

        if(op->session_handle->dsi != globus_l_gfs_dsi)
        {
            globus_extension_release(op->session_handle->dsi_handle);
        }
        globus_l_gfs_free_session_handle(op->session_handle);
    }

}

static
void *
globus_l_gfs_data_post_transfer_event_cb(
    globus_l_gfs_data_session_t *       session_handle,
    globus_l_gfs_data_handle_t *        data_handle)
{
    void *                              remote_data_arg = NULL;

    if(data_handle == NULL)
    {
        return NULL;
    }

    data_handle->outstanding_op = NULL;

    switch(data_handle->state)
    {
        /* occurs if the next transfer comand happens before this function
            is called to switch out of the TE_VALID state
        case GLOBUS_L_GFS_DATA_HANDLE_INUSE:
            break;
*/

        case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED:
            /* cant free until destroy cb, put in full closed state */
            data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_CLOSED;
            break;

        case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_AND_DESTROYED:
            /* can free it */
            data_handle->state =
                GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED;
            if(!data_handle->is_mine)
            {
                remote_data_arg = data_handle->remote_data_arg;
            }
            if(remote_data_arg != NULL)
            {
                session_handle->ref++;
            }
            break;

        case GLOBUS_L_GFS_DATA_HANDLE_TE_VALID:
            data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_VALID;
            break;

        case GLOBUS_L_GFS_DATA_HANDLE_CLOSING:
        case GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED:
        case GLOBUS_L_GFS_DATA_HANDLE_VALID:
            break;

                /* havent even gotten a close, how did this happen? */
        case GLOBUS_L_GFS_DATA_HANDLE_CLOSED:
        case GLOBUS_L_GFS_DATA_HANDLE_CLOSED_AND_DESTROYED:
            /* these shouldnt be possible */
            globus_assert(0);
            break;
    }

    return remote_data_arg;
}    

globus_result_t
globus_i_gfs_data_virtualize_path(
    void *                              session_arg,
    char *                              in_string,
    char **                             ret_string)
{
    globus_l_gfs_data_session_t *       session_handle;
    char *                              tmp_ptr;
    session_handle = (globus_l_gfs_data_session_t *) session_arg;
    
    if(!session_handle->chroot_path)
    {
        *ret_string = NULL;
        return GLOBUS_SUCCESS;
    }
    
    tmp_ptr = strstr(in_string, session_handle->chroot_path);
    
    if(!tmp_ptr)
    {
        *ret_string = NULL;
        return GLOBUS_SUCCESS;
    }
    
    *ret_string = globus_malloc(
        strlen(in_string) + strlen(session_handle->chroot_path));
        
    strncpy(*ret_string, in_string, tmp_ptr - in_string);
    strcpy(*ret_string + (tmp_ptr - in_string), 
        tmp_ptr + strlen((session_handle->chroot_path)));
    
    return GLOBUS_SUCCESS;
}

/* turn /a/./b/../c///d/ into /a/c/d
 * path must begin with / */
globus_result_t
globus_l_gfs_normalize_path(
    const char *                        path,
    char **                             normalized_path)
{
    const char *                        in_ptr;
    const char *                        end;
    const char *                        next_sep;
    char *                              out_ptr;
    char *                              out_path;
    globus_result_t                     res;
    GlobusGFSName(globus_l_gfs_normalize_path);
    GlobusGFSDebugEnter();

    if(!path || path[0] != '/')
    {
        res = GlobusGFSErrorParameter("path");
        goto end;
    }

    out_path = globus_malloc(strlen(path) + 4);
    if(!out_path)
    {
        res = GlobusGFSErrorMemory("normalized path");
        goto end;
    }

    out_path[0] = '/';
    out_path[1] = '\0';
    out_ptr = out_path;

    end = path + strlen(path);

    for(in_ptr = path + 1; in_ptr < end; in_ptr = next_sep + 1)
    {
        int                             len;

        next_sep = strchr(in_ptr, '/');
        if(next_sep == NULL)
        {
            next_sep = end;
        }
        len = next_sep - in_ptr;

        switch(len)
        {
            case 0:
                continue;
                break;

            case 1:
                if(in_ptr[0] == '.')
                {
                    continue;
                }
                break;

            case 2:
                if(in_ptr[0] == '.' && in_ptr[1] == '.')
                {
                    while(out_ptr > out_path && *out_ptr != '/')
                    {
                        out_ptr--;
                    }
                    if(out_ptr == out_path)
                    {
                        out_path[1] = '\0';
                    }
                    else
                    {
                       *out_ptr = '\0';
                    }

                    continue;
                }
                break;

            default:
                break;
        }
        *out_ptr++ = '/';
        strncpy(out_ptr, in_ptr, len);
        out_ptr += len;
        *out_ptr = '\0';
    }

    *normalized_path = out_path;
    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

end:
    GlobusGFSDebugExitWithError();
    return res;
}


globus_result_t
globus_i_gfs_get_full_path(
    const char *                            home_dir,
    const char *                            server_cwd,
    void *                                  session_arg,
    const char *                            in_path,
    char **                                 ret_path,
    int                                     access_type)
{
    globus_result_t                         result;
    char                                    path[MAXPATHLEN];
    char *                                  cwd = GLOBUS_NULL;
    int                                     cwd_len;
    int                                     sc;
    char *                                  slash = "/";
    char *                                  tmp_path;
    char *                                  norm_path;
    GlobusGFSName(globus_i_gfs_get_full_path);
    GlobusGFSDebugEnter();

    *ret_path = NULL;
    if(!in_path)
    {
        result = GlobusGFSErrorGeneric("invalid pathname");
        goto done;
    }
    
#ifdef WIN32
#define WIN_CHARS_NOT_ALLOWED ":*?\"<>|"
    tmp_path = in_path;
    while(*tmp_path)
    {
        if(*tmp_path == '\\')
        {
            *tmp_path = '/';
        }
        tmp_path++;
    }
    if(strcspn(in_path, WIN_CHARS_NOT_ALLOWED) != strlen(in_path))
    {
        result = GlobusGFSErrorGeneric(
            "A filename cannot contain any of the following characters: "
            "\\ / : * ? \" < > |");
            goto done;  
    }          
#endif
 
    if(*in_path == '/')
    {
        strncpy(path, in_path, sizeof(path));
    }
    else if(*in_path == '~')
    {
        if(home_dir == NULL)
        {
            result = GlobusGFSErrorGeneric(
                "No home directory, cannot expand ~");
            goto done;            
        }
        in_path++;
        if(*in_path == '/')
        {
            in_path++;
            cwd = globus_libc_strdup(home_dir);
        }
        else if(*in_path == '\0')
        {
            slash = "";
            cwd = globus_libc_strdup(home_dir);
        }
        else
        {
            char workbuf[MAXPATHLEN];
            char  * hd_name = strdup(in_path);
            char * tmp_ptr = strchr(hd_name, '/');
            struct passwd  l_pwd;
            struct passwd * res_pwd;

            in_path = strchr(in_path, '/');
            if(tmp_ptr != NULL)
            {
                *tmp_ptr = '\0';
            }
            else
            {
                in_path = "";
            }

            sc = globus_libc_getpwnam_r(hd_name, &l_pwd,
                workbuf,
                MAXPATHLEN,
                &res_pwd);
            free(hd_name);
            if(sc != 0 || res_pwd == NULL)
            {
                /* XXX expand other usernames here */
                result = GlobusGFSErrorGeneric(
                    "Cannot expand ~");
                goto done;  
            }
                      
            cwd = globus_libc_strdup(res_pwd->pw_dir);
        } 
        cwd_len = strlen(cwd);
        if(cwd_len > 1 && cwd[cwd_len - 1] == '/')
        {
            cwd[--cwd_len] = '\0';
        }
        snprintf(path, sizeof(path), "%s%s%s", cwd, slash, in_path);
        globus_free(cwd);
    }
    else
    {
        cwd = globus_libc_strdup(server_cwd);
        if(cwd == NULL)
        {
            result = GlobusGFSErrorGeneric("invalid cwd");
            goto done;
        }
        cwd_len = strlen(cwd);
        if(cwd[cwd_len - 1] == '/')
        {
            cwd[--cwd_len] = '\0';
        }
        snprintf(path, sizeof(path), "%s/%s", cwd, in_path);
        globus_free(cwd);
    }
    path[MAXPATHLEN - 1] = '\0';

    result = globus_l_gfs_normalize_path(path, &norm_path);
    if(result != GLOBUS_SUCCESS)
    {
        goto done;
    }

    DriveLetterToWin(norm_path);
    
    result = globus_i_gfs_data_check_path(
        session_arg, norm_path, ret_path, access_type, 1);
    if(result != GLOBUS_SUCCESS)
    {
        goto check_done;
    }

    DriveLetterToWin(*ret_path);

    if(*ret_path == NULL)
    {
        *ret_path = norm_path;
    }
    else
    {
        free(norm_path);
    }

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

check_done:
    free(norm_path);
done:
    GlobusGFSDebugExitWithError();
    return result;
}

globus_result_t
globus_i_gfs_data_check_path(
    void *                              session_arg,
    char *                              in_path,
    char **                             ret_path,
    int                                 access_type,
    globus_bool_t                       is_virtual)
{
    globus_bool_t                       allowed = GLOBUS_FALSE;
    globus_bool_t                       disallowed = GLOBUS_FALSE;
    char                                path[MAXPATHLEN];
    globus_list_t *                     list;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_gfs_alias_ent_t *          alias_ent;
    char *                              true_path = GLOBUS_NULL;
    char *                              start_path;
    int                                 in_path_len;
    globus_l_gfs_data_session_t *       session_handle;
    char *                              tmp_ptr;
    int                                 rc = 0;
    char *                              check_path;
    globus_bool_t                       check_again = GLOBUS_FALSE;
    globus_list_t *                     rp_list;
    GlobusGFSName(globus_i_gfs_data_check_path);
    GlobusGFSDebugEnter();
    
    session_handle = (globus_l_gfs_data_session_t *) session_arg;

    if(globus_list_empty(*session_handle->active_rp_list) &&
        globus_list_empty(session_handle->rp_list))
    {
        allowed = GLOBUS_TRUE;
    }
    
    if(is_virtual && session_handle->chroot_path)
    {
        if(in_path[0] == '/' && in_path[1] == '\0')
        {
            start_path = globus_libc_strdup(session_handle->chroot_path);
        }
        else
        {
            start_path = globus_common_create_string(
                "%s%s", session_handle->chroot_path, in_path);
        }
    }
    else
    {
        start_path = in_path;
    }
        
    if(!allowed)
    {
        if(session_handle->dsi->descriptor & GLOBUS_GFS_DSI_DESCRIPTOR_HAS_REALPATH &&
            !globus_i_gfs_config_bool("rp_follow_symlinks") && 
            strcmp(start_path, "/") != 0 && 
            session_handle->dsi->realpath_func != NULL)
        {            
            result = session_handle->dsi->realpath_func(
                start_path, &true_path, session_handle->session_arg);
            if(result != GLOBUS_SUCCESS)
            {
                char *                  true_base;
                char *                  end_slash = "";
                int                     path_len;
                
                strncpy(path, start_path, sizeof(path));
                path[MAXPATHLEN - 1] = '\0';
                
                path_len = strlen(path);
                if(path[path_len - 1] == '/')
                {
                    path[--path_len] = '\0';
                    end_slash = "/";
                }
                
                tmp_ptr = strrchr(path, '/');
                *(tmp_ptr++) = '\0';
                
                if(*path == '\0')
                {
                    result = GLOBUS_FAILURE;
                }
                else
                {
                    result = session_handle->dsi->realpath_func(
                        path, &true_base, session_handle->session_arg);
                }
                
                if(result != GLOBUS_SUCCESS)
                {
                    result = GLOBUS_SUCCESS;
                    true_path = globus_libc_strdup(start_path);
                }
                else
                {
                    true_path = globus_common_create_string(
                        "%s/%s%s", true_base, tmp_ptr, end_slash);
                    globus_free(true_base);
                }
            }
        }
        else
        {
            true_path = globus_libc_strdup(start_path);
        }
    }
    else if(ret_path)
    {
        if(session_handle->chroot_path)
        {
            *ret_path = globus_libc_strdup(start_path);
        }
        else
        {
            *ret_path = NULL;
        }
    }
        
    if(true_path)
    {

#ifdef WIN32
        for(tmp_ptr = true_path; *tmp_ptr != '\0'; tmp_ptr++)
        {
            *tmp_ptr = tolower(*tmp_ptr);
        }
#endif

        if(!globus_list_empty(*session_handle->active_rp_list))
        {
            rp_list = *session_handle->active_rp_list;
        }
        else
        {
            rp_list = session_handle->rp_list;
        }

        
        while(!globus_list_empty(rp_list))
        {
            check_path = true_path;
            do
            {
                in_path_len = strlen(check_path);
                
                for(list = rp_list;
                    !globus_list_empty(list) && !allowed && !disallowed;
                    list = globus_list_rest(list))
                {            
                    alias_ent = globus_list_first(list);
                    
                    /* disallow if this a dir check and any contents are denied */
                    if(access_type & GFS_L_DIR && alias_ent->access & GFS_L_NONE)
                    {
                        if(strncmp(check_path, alias_ent->alias, in_path_len) == 0 &&
                            (check_path[in_path_len - 1] == '/' ||
                                alias_ent->alias[in_path_len] == '\0' || 
                                alias_ent->alias[in_path_len] == '/'))
                        {
                            disallowed = GLOBUS_TRUE;
                            continue;
                        }
                        else if(fnmatch(alias_ent->alias, check_path, 0) == 0)
                        {
                            disallowed = GLOBUS_TRUE;
                            continue;
                        }
                    }
        
                    /* check if we have an exact match */
                    if(strcspn(alias_ent->alias, "[*?") != alias_ent->alias_len)
                    {
                        rc = fnmatch(alias_ent->alias, check_path, 0);
                        if(rc == 0)
                        {
                            if(alias_ent->access & access_type)
                            {
                                allowed = GLOBUS_TRUE;
                            }
                            else
                            {
                                disallowed = GLOBUS_TRUE;
                            }
                        }
                    }
                    else if(strncmp(check_path, alias_ent->alias, alias_ent->alias_len) == 0 &&
                        (alias_ent->alias[alias_ent->alias_len - 1] == '/' ||
                            check_path[alias_ent->alias_len] == '\0' || 
                            check_path[alias_ent->alias_len] == '/'))
                    {
                        if(alias_ent->access & access_type)
                        {
                            allowed = GLOBUS_TRUE;
                        }
                        else
                        {
                            disallowed = GLOBUS_TRUE;
                        }
                    }
                    
                    /* check if we are a parent of an exact match */
                    if(!allowed && !disallowed && access_type & GFS_L_LIST)
                    {
                        if(strncmp(check_path, alias_ent->alias, in_path_len) == 0 &&
                            (check_path[in_path_len - 1] == '/' ||
                                alias_ent->alias[in_path_len] == '\0' || 
                                alias_ent->alias[in_path_len] == '/'))
                        {
                            if(alias_ent->access & access_type)
                            {
                                allowed = GLOBUS_TRUE;
                            }
                            else
                            {
                               /* disallowed = GLOBUS_TRUE; */
                            }
                        }
                    }
                }
                if(!check_again)
                {
                    if(allowed && strcmp(start_path, true_path))
                    {
                        check_again = GLOBUS_TRUE;
                        check_path = start_path;
                        allowed = GLOBUS_FALSE;
                    }
                }
                else
                {
                    if(!disallowed)
                    {
                        allowed = GLOBUS_TRUE;
                    }
                    check_again = GLOBUS_FALSE;
                }
            } while(check_again);
    
            if(!allowed)
            {
                result = GlobusGFSErrorGeneric(
                    "Path not allowed.");
                    
                rp_list = NULL;
            }
            else
            {
                if(rp_list == *session_handle->active_rp_list && 
                    !globus_list_empty(session_handle->rp_list))
                {
                    rp_list = session_handle->rp_list;
                    allowed = GLOBUS_FALSE;
                }
                else
                {
                    rp_list = NULL;
                }
                
                if(!rp_list && ret_path)
                {
                    if(alias_ent->realpath)
                    {
                        strncpy(path, alias_ent->realpath, alias_ent->realpath_len);
                        strcpy(path + alias_ent->realpath_len, 
                            true_path + alias_ent->alias_len);
            
                        *ret_path = globus_libc_strdup(path);
                    }
                    else if(session_handle->chroot_path)
                    {
                        *ret_path = globus_libc_strdup(start_path);
                    }
                    else
                    {
                        *ret_path = NULL;
                    }
                }
            }
        }
        
        globus_free(true_path);
    }

    
    if(is_virtual && session_handle->chroot_path)
    {
        globus_free(start_path);
    }

    GlobusGFSDebugExit();
    return result;
}

static
void
globus_l_gfs_pw_free(
    struct passwd *                     pw)
{
    if(pw->pw_name != NULL)
    {
        free(pw->pw_name);
    }
    if(pw->pw_passwd != NULL)
    {
        free(pw->pw_passwd);
    }
    if(pw->pw_gecos != NULL)
    {
        free(pw->pw_gecos);
    }
    if(pw->pw_dir != NULL)
    {
        free(pw->pw_dir);
    }
    if(pw->pw_shell != NULL)
    {
        free(pw->pw_shell);
    }

    free(pw);
}

static
struct passwd *
globus_l_gfs_pw_copy(
    struct passwd *                     pw)
{
    struct passwd *                     out_pw;

    if(pw == NULL)
    {
        return NULL;
    }
    out_pw = (struct passwd *) malloc(sizeof(struct passwd));
    if(out_pw == NULL)
    {
        return NULL;
    }

    out_pw->pw_name = pw->pw_name == NULL ? NULL : strdup(pw->pw_name);
    out_pw->pw_passwd = pw->pw_passwd == NULL ? NULL : strdup(pw->pw_passwd);
    out_pw->pw_uid = pw->pw_uid;
    out_pw->pw_gid = pw->pw_gid;
    out_pw->pw_gecos = pw->pw_gecos == NULL ? NULL : strdup(pw->pw_gecos);
    out_pw->pw_dir = pw->pw_dir == NULL ? NULL : strdup(pw->pw_dir);
    out_pw->pw_shell = pw->pw_shell == NULL ? NULL : strdup(pw->pw_shell);

    return out_pw;
}

static
void
globus_l_gfs_gr_free(
    struct group *                      gr)
{
    int                                 i;

    if(gr->gr_name != NULL)
    {
        free(gr->gr_name);
    }
    if(gr->gr_passwd != NULL)
    {
        free(gr->gr_passwd);
    }
    if(gr->gr_mem != NULL)
    {
        for(i = 0; gr->gr_mem[i] != NULL; i++)
        {
            free(gr->gr_mem[i]);
        }
        free(gr->gr_mem);
    }

    free(gr);
}

static
void
globus_l_gfs_free_session_handle(
    globus_l_gfs_data_session_t *       session_handle)
{
    if(session_handle->dsi != globus_l_gfs_dsi)
    {
        globus_extension_release(session_handle->dsi_handle);
    }
    if(session_handle->username)
    {
        globus_free(session_handle->username);
    }
    if(session_handle->subject)
    {
        globus_free(session_handle->subject);
    }
    if(session_handle->client_ip)
    {
        globus_free(session_handle->client_ip);
    }
    if(session_handle->client_appname)
    {
        globus_free(session_handle->client_appname);
    }
    if(session_handle->client_appver)
    {
        globus_free(session_handle->client_appver);
    }
    if(session_handle->client_scheme)
    {
        globus_free(session_handle->client_scheme);
    }
    if(session_handle->real_username)
    {
        globus_free(session_handle->real_username);
    }
    if(session_handle->home_dir)
    {
        globus_free(session_handle->home_dir);
    }
    if(session_handle->true_home)
    {
        globus_free(session_handle->true_home);
    }
    if(session_handle->chroot_path)
    {
        globus_free(session_handle->chroot_path);
    }
    if(session_handle->sharing_state_dir)
    {
        globus_free(session_handle->sharing_state_dir);
    }
    if(session_handle->sharing_id)
    {
        globus_free(session_handle->sharing_id);
    }
    if(session_handle->sharing_sharee)
    {
        globus_free(session_handle->sharing_sharee);
    }
    if(session_handle->dsi_data)
    {
        globus_free(session_handle->dsi_data);
    }
    if(session_handle->dsi_data_global)
    {
        globus_free(session_handle->dsi_data_global);
    }
    if(session_handle->taskid)
    {
        globus_free(session_handle->taskid);
    }
    if(session_handle->storattr_str)
    {
        globus_free(session_handle->storattr_str);
    }
    if(session_handle->gid_array)
    {
        globus_free(session_handle->gid_array);
    }
    if(session_handle->net_stack_list)
    {
        globus_xio_driver_list_destroy(
            session_handle->net_stack_list, GLOBUS_FALSE);
    }
    if(session_handle->disk_stack_list)
    {
        globus_xio_driver_list_destroy(
            session_handle->disk_stack_list, GLOBUS_FALSE);
    }
    if(session_handle->dcsc_cred != GSS_C_NO_CREDENTIAL)
    {
        OM_uint32   min_rc;
        gss_release_cred(
            &min_rc, &session_handle->dcsc_cred);
    }
    if(session_handle->udt_data_channel_inuse)
    {
        globus_ftp_control_handle_destroy(
            &session_handle->udt_data_channel);
        session_handle->udt_data_channel_inuse = GLOBUS_FALSE;
    }
    if(session_handle->session_info_copy)
    {
        if(session_handle->session_info_copy->username)
        {
            globus_free(session_handle->session_info_copy->username);
        }
        if(session_handle->session_info_copy->password)
        {
            globus_free(session_handle->session_info_copy->password);
        }
        if(session_handle->session_info_copy->subject)
        {
            globus_free(session_handle->session_info_copy->subject);
        }
        if(session_handle->session_info_copy->cookie)
        {
            globus_free(session_handle->session_info_copy->cookie);
        }
        if(session_handle->session_info_copy->host_id)
        {
            globus_free(session_handle->session_info_copy->host_id);
        }
        globus_free(session_handle->session_info_copy);
    }
    
    if(session_handle->custom_cmd_table)
    {
        globus_list_t *                 list;
        globus_i_gfs_cmd_ent_t *        cmd_ent;
        globus_hashtable_to_list(&session_handle->custom_cmd_table, &list);
        while(!globus_list_empty(list))
        {
            cmd_ent = (globus_i_gfs_cmd_ent_t *)
            globus_list_remove(&list, list);
            if(cmd_ent)
            {
                if(cmd_ent->cmd_name)
                {
                    globus_free(cmd_ent->cmd_name);
                }
                if(cmd_ent->help_str)
                {
                    globus_free(cmd_ent->help_str);
                }
                globus_free(cmd_ent);
            }
        }
        globus_hashtable_destroy(&session_handle->custom_cmd_table);
    }
        
    if(session_handle->https_stack)
    {
        globus_xio_stack_destroy(session_handle->https_stack);
        session_handle->https_stack = NULL;
    }
    if(session_handle->http_stack)
    {
        globus_xio_stack_destroy(session_handle->http_stack);
        session_handle->http_stack = NULL;
    }
    if(session_handle->http_driver)
    {
        globus_xio_driver_unload(session_handle->http_driver);
        session_handle->http_driver = NULL;
    }
    if(session_handle->http_ca_certs)
    {
        globus_free(session_handle->http_ca_certs);
    }

    globus_handle_table_destroy(&session_handle->handle_table);
    globus_i_gfs_acl_destroy(&session_handle->acl_handle);
    globus_free(session_handle);
}

static
struct group *
globus_l_gfs_gr_copy(
    struct group *                      gr)
{
    int                                 i;
    int                                 count;
    struct group *                      gr_copy = NULL;

    gr_copy = (struct group *) malloc(sizeof(struct group));
    if(gr_copy == NULL)
    {
        return NULL;
    }
    gr_copy->gr_name = gr->gr_name == NULL ? NULL : strdup(gr->gr_name);
    gr_copy->gr_passwd = gr->gr_passwd == NULL ? NULL : strdup(gr->gr_passwd);
    gr_copy->gr_gid = gr->gr_gid;
    /* I don't think we use this one so we are better off not
        allocating memory for it */
    if(gr->gr_mem != NULL)
    {
        for(i = 0; gr->gr_mem[i] != NULL; i++)
        {
        }
        count = i+1;
        gr_copy->gr_mem = (char **)malloc(sizeof(char*)*count);
        for(i = 0; i < count-1; i++)
        {
            gr_copy->gr_mem[i] = strdup(gr->gr_mem[i]);
        }
        gr_copy->gr_mem[i] = NULL;
    }

    return gr_copy;
}

static
struct group *
globus_l_gfs_getgrnam(
    const char *                        name)
{
    struct group *                      grent;
    struct group *                      grent_copy = NULL;

    globus_libc_lock();
    grent = getgrnam(name);
    if(grent != NULL)
    {
        grent_copy = globus_l_gfs_gr_copy(grent);
    }
    globus_libc_unlock();

    return grent_copy;
}

static
struct group *
globus_l_gfs_getgrgid(
    gid_t                               gid)
{
    struct group *                      grent;
    struct group *                      grent_copy = NULL;

    globus_libc_lock();
    grent = getgrgid(gid);
    if(grent != NULL)
    {
        grent_copy = globus_l_gfs_gr_copy(grent);
    }
    globus_libc_unlock();

    return grent_copy;
}

struct passwd *
globus_l_gfs_getpwuid(
    uid_t                               uid)
{
    int                                 rc;
    int                                 pw_buflen;
    char *                              pw_buffer;
    struct passwd                       pwent_mem;
    struct passwd *                     pw_result;
    struct passwd *                     pwent = NULL;

#ifdef _SC_GETPW_R_SIZE_MAX
    pw_buflen = sysconf(_SC_GETPW_R_SIZE_MAX) + 1;
    if(pw_buflen < 1)
    {
        pw_buflen = 1024;
    }
#else
    pw_buflen = 1024;
#endif
    pw_buffer = globus_malloc(pw_buflen);
    if(!pw_buffer)
    {
        return NULL;
    }

    rc = globus_libc_getpwuid_r(getuid(), &pwent_mem, pw_buffer,
                                pw_buflen, &pw_result);
    if(rc != 0 || pw_result == NULL)
    {
        globus_free(pw_buffer);
        return NULL;
    }

    pwent = globus_l_gfs_pw_copy(pw_result);
    globus_free(pw_buffer);

    return pwent;
}

static
struct passwd *
globus_l_gfs_getpwnam(
    const char *                        name)
{
    int                                 rc;
    int                                 pw_buflen;
    char *                              pw_buffer;
    struct passwd                       pwent_mem;
    struct passwd *                     pw_result;
    struct passwd *                     pwent = NULL;

#ifdef _SC_GETPW_R_SIZE_MAX
    pw_buflen = sysconf(_SC_GETPW_R_SIZE_MAX) + 1;
    if(pw_buflen < 1)
    {
        pw_buflen = 1024;
    }
#else
    pw_buflen = 1024;
#endif
    pw_buffer = globus_malloc(pw_buflen);
    if(!pw_buffer)
    {
        return NULL;
    }

    rc = globus_libc_getpwnam_r(
        (char *)name, &pwent_mem, pw_buffer, pw_buflen, &pw_result);
    if(rc != 0 || pw_result == NULL)
    {
        globus_free(pw_buffer);
        return NULL;
    }

    pwent = globus_l_gfs_pw_copy(pw_result);
    globus_free(pw_buffer);

    return pwent;
}

char *
globus_i_gfs_kv_getval(
    char *                              kvstring,
    const char *                        key,
    globus_bool_t                       urldecode)
{
    char *                              keystart;
    char *                              keyend;
    char *                              valstart;
    char *                              valend;
    int                                 keylen;
    char *                              tmp_val = NULL;
    globus_bool_t                       done = GLOBUS_FALSE;
    
    keylen = strlen(key);
    keystart = kvstring;
    keyend = strchr(kvstring, '=');
    while(keyend && keystart && !done)
    {
        if((keylen == keyend - keystart) && 
            strncasecmp(key, keystart, keyend - keystart) == 0)
        {
            done = GLOBUS_TRUE;
        }
        else
        {
            keystart = strchr(keyend, ';');
            if(keystart)
            {
                keystart++;
                keyend = strchr(keystart, '=');
            }
        }
    }
    if(done)
    {
        valstart = keyend + 1;
        valend = strchr(valstart, ';');
        if(valend && valend != valstart)
        {
            tmp_val = malloc(valend - valstart + 1);
            if(tmp_val)
            {
                strncpy(tmp_val, valstart, valend - valstart);
                tmp_val[valend - valstart] = '\0';
                
                if(urldecode)
                {
                    globus_url_string_hex_decode(tmp_val);
                }
            }
        }
    }
    
    return tmp_val;
}

char *
globus_i_gfs_kv_replaceval(
    char *                              kvstring,
    char *                              key,
    char *                              new_val,
    globus_bool_t                       encode)
{
    char *                              keystart;
    char *                              keyend;
    char *                              valstart;
    char *                              valend;
    int                                 keylen;
    char *                              new_kvstring = NULL;
    char                                save;
    char *                              enc_val;
    globus_bool_t                       done = GLOBUS_FALSE;
        
    if(encode)
    {
        enc_val = globus_url_string_hex_encode(new_val, ";");
    }
    else
    {
        if(strchr(new_val, ';'))
        {
            return NULL;
        }
        enc_val = new_val;
    }
            
    keylen = strlen(key);
    keystart = kvstring;
    keyend = strchr(keystart, '=');
    while(keyend && keystart && !done)
    {
        if((keylen == keyend - keystart) && 
            strncasecmp(key, keystart, keyend - keystart) == 0)
        {
            done = GLOBUS_TRUE;
        }
        else
        {
            keystart = strchr(keyend, ';');
            if(keystart)
            {
                keystart++;
                keyend = strchr(keystart, '=');
            }
        }
    }
    if(done)
    {
        valstart = keyend + 1;
        valend = strchr(valstart, ';');
        if(valend && valend != valstart)
        {
            save = *valstart;
            *valstart = 0;
            new_kvstring = globus_common_create_string(
                "%s%s%s", kvstring, enc_val, valend);
            *valstart = save;
        }
    }
    
    if(encode)
    {
        globus_free(enc_val);
    }
    return new_kvstring;
}



static
void
globus_l_gfs_blocking_dispatch_kickout(
    void *                              user_arg)
{
    globus_l_gfs_data_operation_t *     op;
    globus_gfs_transfer_info_t *        transfer_info;
    globus_gfs_command_info_t *         cmd_info;
    globus_gfs_data_info_t *            data_info;
    globus_gfs_stat_info_t *            stat_info;
    GlobusGFSName(globus_l_gfs_blocking_dispatch_kickout);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) user_arg;

    if(op->session_handle->dsi->descriptor & GLOBUS_GFS_DSI_DESCRIPTOR_BLOCKING)
    {
        globus_thread_blocking_will_block();
    }

    switch(op->type)
    {
        case GLOBUS_L_GFS_DATA_INFO_TYPE_COMMAND:
            cmd_info = (globus_gfs_command_info_t *) op->info_struct;
            if (cmd_info->command == GLOBUS_GFS_CMD_SITE_AUTHZ_ASSERT)
            {
                globus_gridftp_server_finished_command(
                                                op, GLOBUS_SUCCESS, NULL);
            }
            else
            {
                op->session_handle->dsi->command_func(
                    op, cmd_info, op->session_handle->session_arg);
            }
            break;

        case GLOBUS_L_GFS_DATA_INFO_TYPE_STAT:
            stat_info = op->info_struct;
            op->session_handle->dsi->stat_func(
                op, stat_info, op->session_handle->session_arg);
            break;

        case GLOBUS_L_GFS_DATA_INFO_TYPE_PASSIVE:
            data_info = (globus_gfs_data_info_t *) op->info_struct;
            op->session_handle->dsi->passive_func(
                op, data_info, op->session_handle->session_arg);
            break;

        case GLOBUS_L_GFS_DATA_INFO_TYPE_ACTIVE:
            data_info = (globus_gfs_data_info_t *) op->info_struct;
            op->session_handle->dsi->active_func(
                op, data_info, op->session_handle->session_arg);
            break;

        case GLOBUS_L_GFS_DATA_INFO_TYPE_SEND:
            transfer_info = (globus_gfs_transfer_info_t *) op->info_struct;
            op->dsi->send_func(
                op, transfer_info, op->session_handle->session_arg);
            break;

        case GLOBUS_L_GFS_DATA_INFO_TYPE_RECV:
            transfer_info = (globus_gfs_transfer_info_t *) op->info_struct;
            op->dsi->recv_func(
                op, transfer_info, op->session_handle->session_arg);
            break;

        case GLOBUS_L_GFS_DATA_INFO_TYPE_LIST:
            transfer_info = (globus_gfs_transfer_info_t *) op->info_struct;
            op->session_handle->dsi->list_func(
                op, transfer_info, op->session_handle->session_arg);
            break;

        default:
            globus_assert(0 && "possible memory corruption");
            break;
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_authorize_cb(
    globus_gfs_acl_object_desc_t *      object,
    globus_gfs_acl_action_t             action,
    void *                              user_arg,
    globus_result_t                     result)
{
    void *                              remote_data_arg = NULL;
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    GlobusGFSName(globus_l_gfs_authorize_cb);
    GlobusGFSDebugEnter();

    if(user_arg != NULL)
    {
        if(action == GFS_ACL_ACTION_COMMIT)
        {
            globus_l_gfs_data_end_transfer_kickout(user_arg);
        }
        else if(result == GLOBUS_SUCCESS)
        {
            globus_l_gfs_blocking_dispatch_kickout(user_arg);
        }
        else
        {
            globus_gfs_finished_info_t      finished_info;
            globus_l_gfs_data_operation_t * op;
    
            op = (globus_l_gfs_data_operation_t *) user_arg;
            memset(&finished_info, '\0', sizeof(globus_gfs_finished_info_t));
    
            result = GlobusGFSErrorWrapFailed(
                "authorization", result);
            finished_info.result = result;
            finished_info.type = op->type;
            finished_info.id = op->id;
    
            if(op->callback == NULL)
            {
                globus_gfs_ipc_reply_finished(
                    op->ipc_handle, &finished_info);
            }
            else
            {
                op->callback(
                    &finished_info,
                    op->user_arg);
            }
            
            globus_mutex_lock(&op->session_handle->mutex);
            {
                if(op->data_handle != NULL)
                {
                    switch(op->data_handle->state)
                    {
                        case GLOBUS_L_GFS_DATA_HANDLE_INUSE:
                            op->data_handle->state =
                                GLOBUS_L_GFS_DATA_HANDLE_VALID;
                            break;
    
                        case GLOBUS_L_GFS_DATA_HANDLE_CLOSING:
                            break;
    
                        case GLOBUS_L_GFS_DATA_HANDLE_CLOSED:
                        case GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED:
                            if(!op->data_handle->is_mine)
                            {
                                remote_data_arg = 
                                    op->data_handle->remote_data_arg;
                            }
                            break;
    
                        case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED:
                        case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_AND_DESTROYED:
                        case GLOBUS_L_GFS_DATA_HANDLE_VALID:
                        default:
                            globus_assert(0 && "possible memory corruption");
                            break;
                    }
                }
                GFSDataOpDec(op, destroy_op, destroy_session);
            }
            globus_mutex_unlock(&op->session_handle->mutex);
            globus_assert(destroy_op);
    
            globus_l_gfs_data_fire_cb(op, remote_data_arg, destroy_session);
    
            globus_l_gfs_data_operation_destroy(op);
        }
    }

    GlobusGFSDebugExit();
}

static
globus_result_t
globus_l_gfs_data_decode_passed_cred(
    char *                              encoded_cred,
    gss_cred_id_t *                     out_cred)
{
    OM_uint32                           major_status; 
    OM_uint32                           minor_status;
    gss_buffer_desc                     buf; 
    gss_cred_id_t                       cred;
    globus_result_t                     res;
    GlobusGFSName(globus_l_gfs_data_decode_passed_cred);
    GlobusGFSDebugEnter();
    
    buf.value = globus_libc_strdup(encoded_cred);
    
    res = globus_l_gfs_base64_decode(
        (globus_byte_t *) encoded_cred, buf.value, &buf.length);
    if(res != GLOBUS_SUCCESS)
    {
        globus_free(buf.value);
        res = GlobusGFSErrorGeneric(
            "Invalid base64 input for credential.");
    }
    else
    {                            
        major_status = gss_import_cred(
            &minor_status,
            &cred,
            GSS_C_NO_OID,
            0,
            &buf,
            0,
            NULL);
        globus_free(buf.value);
        if(major_status != GSS_S_COMPLETE)
        {
            res = GlobusGFSErrorWrapFailed(
                "Credential import", minor_status);
        }
        else
        {            
            *out_cred = cred;
        }
    }

    GlobusGFSDebugExit();

    return res;
}


/*
 *  this is called when writing.  if file exists it is a write
 *  request, if it does not exists it is a create request
 */
static
void
globus_l_gfs_data_auth_stat_cb(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg)
{
    void *                              stat_wrapper;
    globus_result_t                     res;
    globus_gfs_acl_action_t             action;
    int                                 rc;
    globus_l_gfs_data_operation_t *     op;
    globus_gfs_transfer_info_t *        recv_info;
    globus_gfs_acl_object_desc_t        object;
    GlobusGFSName(globus_l_gfs_data_auth_stat_cb);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) user_arg;
    recv_info = (globus_gfs_transfer_info_t *) op->info_struct;

    globus_l_gfs_data_alive(op->session_handle);

    /* if the file does not exist */
    if(reply->info.stat.stat_count == 0)
    {
        action = GFS_ACL_ACTION_CREATE;
    }
    /* if the file does exist */
    else
    {
        action = GFS_ACL_ACTION_WRITE;
    }
    object.name = recv_info->pathname;
    object.size = recv_info->alloc_size;
    stat_wrapper = op->stat_wrapper;
    rc = globus_gfs_acl_authorize(
        &op->session_handle->acl_handle,
        action,
        &object,
        &res,
        globus_l_gfs_authorize_cb,
        op);
    if(rc == GLOBUS_GFS_ACL_COMPLETE)
    {
        globus_l_gfs_authorize_cb(&object, action, op, res);
    }
    globus_free(stat_wrapper);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_send_stat_cb(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg)
{
    void *                              stat_wrapper;
    globus_result_t                     res;
    int                                 rc;
    globus_l_gfs_data_operation_t *     op;
    globus_gfs_transfer_info_t *        send_info;
    globus_gfs_acl_object_desc_t        object;
    GlobusGFSName(globus_l_gfs_data_recv_stat_cb);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) user_arg;

    globus_l_gfs_data_alive(op->session_handle);

    send_info = (globus_gfs_transfer_info_t *) op->info_struct;
    if(reply->info.stat.stat_count == 1)
    {
        send_info->alloc_size = reply->info.stat.stat_array[0].size;
    }

    object.name = send_info->pathname;

    stat_wrapper = op->stat_wrapper;
    rc = globus_gfs_acl_authorize(
        &op->session_handle->acl_handle,
        GFS_ACL_ACTION_READ,
        &object,
        &res,
        globus_l_gfs_authorize_cb,
        op);
    if(rc == GLOBUS_GFS_ACL_COMPLETE)
    {
        globus_l_gfs_authorize_cb(
            &object, GFS_ACL_ACTION_READ, op, res);
    }
    globus_free(stat_wrapper);

    GlobusGFSDebugExit();
}

void
globus_i_gfs_monitor_init(
    globus_i_gfs_monitor_t *            monitor)
{
    GlobusGFSName(globus_i_gfs_monitor_init);
    GlobusGFSDebugEnter();

    globus_mutex_init(&monitor->mutex, NULL);
    globus_cond_init(&monitor->cond, NULL);
    monitor->done = GLOBUS_FALSE;

    GlobusGFSDebugExit();
}


static
globus_result_t
globus_i_gfs_data_new_dsi(
    globus_extension_handle_t *         dsi_handle,
    const char *                        dsi_name,
    globus_gfs_storage_iface_t **       dsi_iface,
    globus_bool_t                       check_name)
{
    globus_gfs_storage_iface_t *        new_dsi;
    const char *                        module_name;
    int                                 rc;
    globus_result_t                     result = GLOBUS_SUCCESS;
    GlobusGFSName(globus_i_gfs_data_new_dsi);
    GlobusGFSDebugEnter();
    
    if(check_name)
    {
        module_name = globus_i_gfs_config_get_module_name(dsi_name);
        if(module_name == NULL)
        {
            GlobusGFSErrorGenericStr(result,
                ("DSI '%s' is not allowed.", dsi_name));
            goto err;
        }
    }
    else
    {
        module_name = dsi_name;
    }

    /* see if we already have this module loaded, if so use it */
    new_dsi = (globus_gfs_storage_iface_t *) globus_extension_lookup(
        dsi_handle, GLOBUS_GFS_DSI_REGISTRY, (void *) module_name);
    if(new_dsi == NULL)
    {
        /* otherwise load the dll */
        char                            buf[256];

        snprintf(buf, 256, "globus_gridftp_server_%s", module_name);
        buf[255] = 0;

        rc = globus_extension_activate(buf);
        if(rc != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed("DSI activation", rc);
            goto err;
        }
    }
    
    /* check again */
    new_dsi = (globus_gfs_storage_iface_t *) globus_extension_lookup(
        dsi_handle, GLOBUS_GFS_DSI_REGISTRY, (void *) module_name);
    if(new_dsi == NULL)
    {
        GlobusGFSErrorGenericStr(result,
            ("DSI '%s' is not available in the module.", dsi_name));
        goto err;
    }

    *dsi_iface = new_dsi;
    return GLOBUS_SUCCESS;
    
err:
    *dsi_iface = NULL;
    GlobusGFSDebugExit();
    return result;
}

static
globus_gfs_storage_iface_t *
globus_l_gfs_data_new_dsi(
    globus_l_gfs_data_session_t *       session_handle,
    const char *                        in_module_name)
{
    const char *                        module_name;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_data_new_dsi);
    GlobusGFSDebugEnter();

    if(in_module_name == NULL || *in_module_name == '\0')
    {
        GlobusGFSDebugExit();
        return session_handle->dsi;
    }
    if(!(session_handle->dsi->descriptor & GLOBUS_GFS_DSI_DESCRIPTOR_SENDER))
    {
        goto type_error;
    }

    module_name = globus_i_gfs_config_get_module_name(in_module_name);
    if(module_name == NULL)
    {
        goto type_error;
    }
    /* if there was a prevous loaded module */
    if(session_handle->mod_dsi_name != NULL)
    {
        /* if there was a last module and it is this one use it */
        if(strcmp(module_name, session_handle->mod_dsi_name) != 0)
        {
            globus_free(session_handle->mod_dsi_name);
            globus_extension_release(session_handle->mod_dsi_handle);

            session_handle->mod_dsi_name = globus_libc_strdup(module_name);
            result = globus_i_gfs_data_new_dsi(
                &session_handle->mod_dsi_handle,
                session_handle->mod_dsi_name,
                &session_handle->mod_dsi,
                GLOBUS_FALSE);
            if(session_handle->mod_dsi == NULL)
            {
                goto error;
            }
        }
    }
    else
    {
        session_handle->mod_dsi_name =  globus_libc_strdup(module_name);
        result = globus_i_gfs_data_new_dsi(
            &session_handle->mod_dsi_handle,
            session_handle->mod_dsi_name,
            &session_handle->mod_dsi,
            GLOBUS_FALSE);
        if(session_handle->mod_dsi == NULL)
        {
            goto error;
        }
    }

    GlobusGFSDebugExit();
    return session_handle->mod_dsi;

error:
    globus_free(session_handle->mod_dsi_name);
type_error:
    GlobusGFSDebugExitWithError();
    return NULL;
}

static
void
globus_l_gfs_data_brain_ready_delay_cb(
    void *                              user_arg)
{
    void *                              remote_data_arg = NULL;
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    globus_l_gfs_data_operation_t *     op;
    globus_gfs_session_info_t *         session_info;
    globus_gfs_finished_info_t          finished_info;
    globus_xio_contact_t                parsed_contact;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_data_brain_ready_delay_cb);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) user_arg;
    session_info = (globus_gfs_session_info_t *) op->info_struct;

    memset(&finished_info, '\0', sizeof(globus_gfs_finished_info_t));

    if(!op->session_handle->username)
    {
        op->session_handle->username = globus_libc_strdup(session_info->username);
    }
    if(!op->session_handle->subject)
    {
        op->session_handle->subject = globus_libc_strdup(session_info->subject);
    }

    if(!op->session_handle->client_ip && globus_xio_contact_parse(
        &parsed_contact, session_info->host_id) == GLOBUS_SUCCESS)
    {
        char                            ipaddr[100];

        result = globus_i_gfs_config_hostname_to_address_string(
            parsed_contact.host, ipaddr, sizeof(ipaddr));
        if(result == GLOBUS_SUCCESS)
        {
            op->session_handle->client_ip =
                globus_libc_strdup(ipaddr);
        }

        globus_xio_contact_destroy(&parsed_contact);
    }

    if(op->session_handle->dsi->init_func != NULL)
    {
        op->session_handle->dsi->init_func(op, session_info);
    }
    else
    {
        finished_info.result = GLOBUS_SUCCESS;
        finished_info.info.session.session_arg = op->session_handle;
        finished_info.info.session.username = session_info->username;

        /* update home dir based on restricted paths */
        globus_l_gfs_data_update_restricted_paths_symlinks(
            op->session_handle, &globus_l_gfs_path_alias_list_base);
        globus_l_gfs_data_update_restricted_paths_symlinks(
            op->session_handle, &globus_l_gfs_path_alias_list_sharing);

        if(globus_i_gfs_data_check_path(op->session_handle,
               op->session_handle->home_dir, NULL, GFS_L_LIST, 1) != GLOBUS_SUCCESS)
        {
            if(op->session_handle->home_dir)
            {
                globus_free(op->session_handle->home_dir);
            }
            op->session_handle->home_dir = strdup("/");
        }  

        finished_info.info.session.home_dir = op->session_handle->home_dir;

        if(op->callback == NULL)
        {
            globus_gfs_ipc_reply_session(
                op->ipc_handle, &finished_info);
        }
        else
        {

            op->callback(
                &finished_info,
                op->user_arg);
        }
        globus_l_gfs_data_reset_watchdog(op->session_handle, NULL);
        
        globus_mutex_lock(&op->session_handle->mutex);
        {
            GFSDataOpDec(op, destroy_op, destroy_session);

            remote_data_arg = globus_l_gfs_data_check(
                op->session_handle,
                op->data_handle);
        }
        globus_mutex_unlock(&op->session_handle->mutex);
        globus_assert(destroy_op);

        globus_l_gfs_data_fire_cb(op, remote_data_arg, destroy_session);

        globus_l_gfs_data_operation_destroy(op);
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_auth_init_cb(
    globus_gfs_acl_object_desc_t *      object,
    globus_gfs_acl_action_t             action,
    void *                              user_arg,
    globus_result_t                     result)
{
    void *                              remote_data_arg = NULL;
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    globus_l_gfs_data_operation_t *     op;
    globus_gfs_session_info_t *         session_info;
    globus_gfs_finished_info_t          finished_info;
    globus_bool_t                       ready = GLOBUS_FALSE;
    GlobusGFSName(globus_l_gfs_data_auth_init_cb);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) user_arg;
    session_info = (globus_gfs_session_info_t *) op->info_struct;

    memset(&finished_info, '\0', sizeof(globus_gfs_finished_info_t));
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    globus_mutex_lock(&gfs_l_data_brain_mutex);
    {
        ready = gfs_l_data_brain_ready;
        if(!gfs_l_data_brain_ready)
        {
            globus_list_insert(&gfs_l_data_brain_ready_list, user_arg);
        }
    }
    globus_mutex_unlock(&gfs_l_data_brain_mutex);

    if(ready)
    {
        globus_l_gfs_data_brain_ready_delay_cb(user_arg);
    }

    GlobusGFSDebugExit();
    return;

error:
    finished_info.result = result;
    finished_info.info.session.session_arg = NULL;

    if(op->callback == NULL)
    {
        globus_gfs_ipc_reply_session(
            op->ipc_handle, &finished_info);
    }
    else
    {
        op->callback(
            &finished_info,
            op->user_arg);
    }
    globus_mutex_lock(&op->session_handle->mutex);
    {
        /* dec session handle now since we won't get a stop_session */
        op->session_handle->ref--;
        GFSDataOpDec(op, destroy_op, destroy_session);
        remote_data_arg = globus_l_gfs_data_check(
            op->session_handle, op->data_handle);
    }
    globus_mutex_unlock(&op->session_handle->mutex);
    globus_assert(destroy_op);
    globus_l_gfs_data_fire_cb(op, remote_data_arg, destroy_session);
    globus_l_gfs_data_operation_destroy(op);
    GlobusGFSDebugExitWithError();
}



typedef struct
{
    gid_t                               gid;
    char *                              username;
    globus_mutex_t                      mutex;
    globus_cond_t                       cond;
    int                                 count;
    int                                 rc;
} globus_l_libc_initgroups_info_t;

static
void
globus_l_libc_initgroups_cb(
    void *                              user_arg)
{
    globus_l_libc_initgroups_info_t *   info;
    int                                 rc;
    
    info = (globus_l_libc_initgroups_info_t *) user_arg;
    
    globus_mutex_lock(&info->mutex);
    {
        rc = initgroups(info->username, info->gid);
        if(rc)
        {
            info->rc = rc;
        }
        
        info->count--;
        if(info->count == 0)
        {
            globus_cond_signal(&info->cond);
        }
    }
    globus_mutex_unlock(&info->mutex);
}

int
globus_libc_initgroups(
    char *                              username,
    gid_t                               gid)
{
    int                                 rc;
    int                                 tmprc = 0;

#ifndef BUILD_LITE

    globus_l_libc_initgroups_info_t *   info;
    int                                 i;
    int                                 threads;
    char *                              tmp;

    threads = 2; /* XXX include globus_callback_threads.c to get #define? */    
    if((tmp = globus_module_getenv("GLOBUS_CALLBACK_POLLING_THREADS")) != NULL)
    {
        rc = atoi(tmp);
        if(rc > 0)
        {
            threads = rc;
        }
    }

    info = (globus_l_libc_initgroups_info_t *) 
        globus_malloc(sizeof(globus_l_libc_initgroups_info_t));

    globus_mutex_init(&info->mutex, NULL);
    globus_cond_init(&info->cond, NULL);

    info->count = 0;
    info->username = username;
    info->gid = gid;
    info->rc = 0;
    globus_mutex_lock(&info->mutex);
    {
        for(i = 0; i < threads; i++)
        {
            globus_callback_register_oneshot(
                NULL,
                NULL,
                globus_l_libc_initgroups_cb,
                info);
            info->count++;
        }
    
        while(info->count > 0)
        {
            globus_cond_wait(&info->cond, &info->mutex);
        }
    }
    globus_mutex_unlock(&info->mutex);
    
    tmprc = info->rc;
    globus_mutex_destroy(&info->mutex);
    globus_cond_destroy(&info->cond);
    globus_free(info);

#endif

    rc = initgroups(username, gid);
    if(!rc && tmprc)
    {
        rc = tmprc;
    }
    
    return rc;
}

int
globus_list_cmp_alias_ent(
    void *                              a, 
    void *                              b,
    void *                              arg)
{
    globus_l_gfs_alias_ent_t            dummy = {.alias = NULL, .alias_len=0};
    globus_l_gfs_alias_ent_t *          a_ent = (a != NULL) ? a : &dummy;
    globus_l_gfs_alias_ent_t *          b_ent = (b != NULL) ? b : &dummy;
    char                                a_tmp[a_ent->alias_len+1];
    char                                b_tmp[b_ent->alias_len+1];
    size_t                              a_i;
    size_t                              b_i;
    

    strcpy(a_tmp, a_ent->alias ? a_ent->alias : "");
    strcpy(b_tmp, b_ent->alias ? b_ent->alias : "");

    /* check for wildcard chars and replace the first one with a low value */
    /* we want the reverse sorted order to be a, [abc], ?, *           */
    a_i = strcspn(a_tmp, "[*?");
    if(a_i < a_ent->alias_len)
    {
        switch(a_tmp[a_i])
        {
            case '*':
                a_tmp[a_i] = 1;
                break;
            case '?': 
                a_tmp[a_i] = 2;
                break;
            case '[': 
                a_tmp[a_i] = 3;
                break;
        }
    } 

    b_i = strcspn(b_tmp, "[*?");
    if(b_i < b_ent->alias_len)
    {
        switch(b_tmp[b_i])
        {
            case '*':
                b_tmp[b_i] = 1;
                break;
            case '?': 
                b_tmp[b_i] = 2;
                break;
            case '[': 
                b_tmp[b_i] = 3;
                break;
        }
    }
    
    return strcmp(a_tmp, b_tmp) >= 0;
}

static
char *
globus_l_gfs_data_update_var_path(
    globus_l_gfs_data_session_t *           session_handle,
    char *                                  in_path)
{
    char *                              path;
    char *                              var;
    char *                              new_path = NULL;
    GlobusGFSName(globus_l_gfs_data_update_var_path);
    GlobusGFSDebugEnter();

    path = globus_libc_strdup(in_path);
    
    if((var = strstr(path, "$USER")) != NULL)
    {
        *var = '\0';
        var += 5;
        new_path = globus_common_create_string(
            "%s%s%s", path, session_handle->username, var);
        globus_free(path);
        path = new_path;
    }
    if((var = strstr(path, "$HOME")) != NULL)
    {
        *var = '\0';
        var += 5;
        new_path = globus_common_create_string(
            "%s%s%s", path, session_handle->true_home, var);
        globus_free(path);
        path = new_path;
    }
    if((var = strstr(path, "~")) != NULL)
    {
        char *                          tmp_home; 
        
        tmp_home = session_handle->home_dir ? 
            session_handle->home_dir : session_handle->true_home;
        *var = '\0';
        var += 1;
        new_path = globus_common_create_string(
            "%s%s%s", path, tmp_home, var);
        globus_free(path);
        path = new_path;
    }
        
    return path;
}    
   
static
void
globus_l_gfs_data_update_restricted_paths(
    globus_l_gfs_data_session_t *   session_handle,
    globus_list_t **                rp_list)
{
    globus_list_t *                     list;
    globus_l_gfs_alias_ent_t *          alias_ent;
    globus_bool_t                       resort = GLOBUS_FALSE;
    char *                              var_path;
    GlobusGFSName(globus_l_gfs_data_update_restricted_paths);
    GlobusGFSDebugEnter();
    
    
    if(!globus_list_empty(*rp_list))
    {   
        for(list = *rp_list;
            !globus_list_empty(list);
            list = globus_list_rest(list))
        {            
            alias_ent = globus_list_first(list);
            
            if(alias_ent->alias[0] == '~' || strchr(alias_ent->alias, '$'))
            {
                var_path = globus_l_gfs_data_update_var_path(
                    session_handle, alias_ent->alias);
                    
                globus_free(alias_ent->alias);
                alias_ent->alias = var_path;               
                alias_ent->alias_len = strlen(alias_ent->alias);

                resort = GLOBUS_TRUE;
                
#ifdef WIN32
                for(var_path = alias_ent->alias; *var_path != '\0'; var_path++)
                {
                    *var_path = tolower(*var_path);
                }
#endif
            }

            if(alias_ent->realpath && 
                (alias_ent->realpath[0] == '~' || strchr(alias_ent->realpath, '$')))
            {
                var_path = globus_l_gfs_data_update_var_path(
                    session_handle, alias_ent->realpath);
                    
                globus_free(alias_ent->realpath);
                alias_ent->realpath = var_path;               
                alias_ent->realpath_len = strlen(alias_ent->realpath); 

#ifdef WIN32
                for(var_path = alias_ent->realpath; *var_path != '\0'; var_path++)
                {
                    *var_path = tolower(*var_path);
                }
#endif
            }
        }
        
        if(resort)
        {
            *rp_list = globus_list_sort_destructive(
                *rp_list, globus_list_cmp_alias_ent, NULL);
        }
    }
}

static
void
globus_l_gfs_data_update_restricted_paths_symlinks(
    globus_l_gfs_data_session_t *   session_handle,
    globus_list_t **                rp_list)
{
    globus_list_t *                     list;
    globus_list_t *                     new_list = NULL;
    globus_l_gfs_alias_ent_t *          alias_ent;
    globus_bool_t                       resort = GLOBUS_FALSE;
    GlobusGFSName(globus_l_gfs_data_update_restricted_paths);
    GlobusGFSDebugEnter();
    
    if(!globus_i_gfs_config_bool("rp_follow_symlinks") && session_handle &&
        session_handle->dsi->descriptor & GLOBUS_GFS_DSI_DESCRIPTOR_HAS_REALPATH &&
        session_handle->dsi->realpath_func)
    {
        if(!globus_list_empty(*rp_list))
        {   
            for(list = *rp_list;
                !globus_list_empty(list);
                list = globus_list_rest(list))
            {        
                char *                  real_path = NULL;
                globus_bool_t           result;
                
                alias_ent = globus_list_first(list);
                
                result = session_handle->dsi->realpath_func(
                    alias_ent->alias, &real_path, session_handle->session_arg);
                
                if(result == GLOBUS_SUCCESS && real_path)
                {
                    if(strcmp(real_path, alias_ent->alias) != 0)
                    {
                        globus_l_gfs_alias_ent_t *      new_ent;
                        
                        new_ent = (globus_l_gfs_alias_ent_t *)
                            globus_calloc(1, sizeof(globus_l_gfs_alias_ent_t));
                        new_ent->access = alias_ent->access;
                        new_ent->alias = real_path;
                        new_ent->alias_len = strlen(real_path);
                        if(alias_ent->realpath)
                        {
                            new_ent->realpath = strdup(alias_ent->realpath);
                            new_ent->realpath_len = alias_ent->realpath_len;
                        }
                        
                        globus_list_insert(&new_list, new_ent);
                        
                        resort = GLOBUS_TRUE;
                    }
                    else
                    {
                        globus_free(real_path);
                        real_path = NULL;
                    }
                }    
            }
            
            if(resort)
            {
                while(!globus_list_empty(new_list))
                {
                    globus_list_insert(
                        rp_list, 
                        globus_list_remove(&new_list, new_list));
                }

                *rp_list = globus_list_sort_destructive(
                    *rp_list, globus_list_cmp_alias_ent, NULL);
            }
        }
    }
}

#define GFS_RP_LEAD_CHARS "/~$*[?"

static
globus_result_t
globus_l_gfs_data_parse_restricted_paths(
    globus_l_gfs_data_session_t *       session_handle,
    char *                              restrict_paths,
    globus_list_t **                    out_list,
    globus_bool_t                       is_virtual)
{
    globus_list_t *                     list;
    globus_list_t *                     tmp_list = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;
    char *                              chroot_path_esc = NULL;
    GlobusGFSName(globus_l_gfs_data_parse_restricted_paths);
    GlobusGFSDebugEnter();
                                        
    list = globus_list_from_string(restrict_paths, ',', NULL);
    
    /* escape glob chars in chroot */
    if(is_virtual && session_handle && session_handle->chroot_path &&
        strcspn(session_handle->chroot_path, "[*?") != 
            strlen(session_handle->chroot_path))
    {
        char * ptr;
        char * ptr_esc;
        
        chroot_path_esc = 
            (char *) malloc(2 * strlen(session_handle->chroot_path) + 1);
        
        ptr = session_handle->chroot_path;
        ptr_esc = chroot_path_esc;
        while(*ptr)
        {
            switch(*ptr)
            {
                case '*':
                case '?':
                case '[':
                case ']':
                case '\\':
                    *ptr_esc++ = '\\';
                
                default:
                    *ptr_esc++ = *ptr++;
                    break;
            }
        }
        *ptr_esc = '\0';
    }
    else if(session_handle && session_handle->chroot_path)
    {
        chroot_path_esc = session_handle->chroot_path;
    }

    while(!globus_list_empty(list))
    {
        globus_l_gfs_alias_ent_t *      ent;
        char *                          ent_str;
        char *                          ptr;
        char *                          alias;
        globus_bool_t                   done;
        
        ent = (globus_l_gfs_alias_ent_t *)
            globus_malloc(sizeof(globus_l_gfs_alias_ent_t));
        ent->access = 0;
        
        ent_str = (char *) globus_list_remove(&list, list);    
                
        ptr = ent_str;
        done = GLOBUS_FALSE;
        while(*ptr && !done)
        {
            switch(*ptr)
            {
                case 'r':
                case 'R':
                    ent->access |= GFS_L_READ;
                    ptr++;
                    break;
                    
                case 'w':
                case 'W':
                    ent->access |= GFS_L_WRITE;
                    ptr++;
                    break;

                case 'n':
                case 'N':
                    ent->access |= GFS_L_NONE;
                    ptr++;
                    break;
                    
                case '/':
                case '~':
                case '$':
                case '*':
                case '?':
                case '[':
                    if(ent->access == 0)
                    {
                       ent->access |= GFS_L_READ | GFS_L_WRITE;
                    }
                    done = GLOBUS_TRUE;
                    break;
                    
                default:
                    done = GLOBUS_TRUE;
                    GlobusGFSErrorGenericStr(result,
                        ("Path restriction entries must be full paths, "
                        "prefixed only with R, W, or N."
                        "The entry '%s' is invalid.\n", ent_str));
                    return result;
                    break;
            }                
        }
        ent->access |= GFS_L_LIST;
        
        if(ent->access & GFS_L_NONE)
        {
            ent->access = GFS_L_NONE;
        }
        
        if((alias = strchr(ptr, ':')) != NULL)
        {
            *alias = '\0';
            alias++;
            
            ent->alias = globus_libc_strdup(alias);
            globus_url_string_hex_decode(ent->alias);
            ent->alias_len = strlen(ent->alias);
            
            ent->realpath = globus_libc_strdup(ptr);
            globus_url_string_hex_decode(ent->realpath);
            ent->realpath_len = strlen(ent->realpath);
        }
        else
        {
            ent->alias = globus_libc_strdup(ptr);
            globus_url_string_hex_decode(ent->alias);
            ent->alias_len = strlen(ent->alias);
            
            ent->realpath = NULL;
            ent->realpath_len = 0;
        }
        globus_free(ent_str);

        if(strchr(GFS_RP_LEAD_CHARS, *ent->alias) == NULL ||
            (ent->realpath && strchr(GFS_RP_LEAD_CHARS, *ent->alias) == NULL))
        {                
            globus_free(ent->alias);
            if(ent->realpath)
            {
                globus_free(ent->realpath);
            }
            globus_free(ent);
        }
        else
        {
            if(is_virtual)
            {   
                char *                  tmp_path;
                
                if(ent->realpath)
                {
                    globus_free(ent->realpath);
                    ent->realpath = NULL;
                    ent->realpath_len = 0;
                }
                
                if(ent->alias[0] == '~')
                {
                    tmp_path = globus_common_create_string(
                        "%s%s", 
                        chroot_path_esc ? 
                            "/" : session_handle->home_dir, 
                        ent->alias + 1);
                    globus_free(ent->alias);
                    ent->alias = tmp_path;
                    ent->alias_len = strlen(ent->alias);
                }
                else
                {
                    if(chroot_path_esc)
                    {   
                        tmp_path = globus_common_create_string("%s%s", 
                            chroot_path_esc, ent->alias);
                        globus_free(ent->alias);
                        ent->alias = tmp_path;
                        ent->alias_len = strlen(ent->alias);
                    }
                }
                
                if(ent->alias_len > 1 && ent->alias[ent->alias_len - 1] == '/')
                {
                    ent->alias[ent->alias_len - 1] = '\0';
                    ent->alias_len--;
                }
            }
            {
#ifdef WIN32
                globus_l_gfs_alias_ent_t *  new_ent;
                char *                      tmp_ptr;
    
                for(tmp_ptr = ent->alias; *tmp_ptr != '\0'; tmp_ptr++)
                {
                    *tmp_ptr = tolower(*tmp_ptr);
                }
                
                if(ent->realpath)
                {
                    for(tmp_ptr = ent->realpath; *tmp_ptr != '\0'; tmp_ptr++)
                    {
                        *tmp_ptr = tolower(*tmp_ptr);
                    }
                }
#endif
                globus_list_insert(&tmp_list, ent);
                
#ifdef WIN32                
                /* now store driveletter:/path form of that ent */
                new_ent = (globus_l_gfs_alias_ent_t *)
                    globus_malloc(sizeof(globus_l_gfs_alias_ent_t));
                memcpy(new_ent, ent, sizeof(globus_l_gfs_alias_ent_t));
                new_ent->alias = globus_libc_strdup(ent->alias);
                new_ent->realpath = globus_libc_strdup(ent->realpath);
                ent = new_ent;
    
                if(!ent->realpath && ent->alias[0] == '/' &&
                    isalpha(ent->alias[1]))
                {
                    ent->alias[0] = ent->alias[1];
                    ent->alias[1] = ':';
                    if(ent->alias[2] == 0)
                    {
                        ent->alias = globus_realloc(ent->alias, 4);
                        ent->alias[2] = '/';
                        ent->alias[3] = '\0';
                        ent->alias_len = 3;
                    }
                }
                
                if(ent->realpath && ent->realpath[0] == '/' &&
                    isalpha(ent->realpath[1]))
                {
                    ent->realpath[0] = ent->realpath[1];
                    ent->realpath[1] = ':';
                    if(ent->realpath[2] == 0)
                    {
                        ent->realpath = globus_realloc(ent->realpath, 4);
                        ent->realpath[2] = '/';
                        ent->realpath[3] = '\0';
                        ent->realpath_len = 3;
                    }
                }
                globus_list_insert(&tmp_list, ent);
#endif 
            }              

        }
    }
    
    if(!globus_list_empty(tmp_list))
    {
        *out_list = globus_list_sort_destructive(
            tmp_list, globus_list_cmp_alias_ent, NULL);
    }
    else
    {
         result = GlobusGFSErrorGeneric("No valid paths added to restricted list.");
    }
    
    GlobusGFSDebugExit();
    return result;
}

#define GLOBUS_L_GFS_LINEBUFLEN 1024

globus_result_t
globus_l_gfs_data_read_share_file(
    char *                              filename,
    char **                             share_path)
{
    FILE *                              fptr;
    char *                              linebuf;
    char *                              optionbuf;
    char *                              valuebuf;
    int                                 linebuflen = GLOBUS_L_GFS_LINEBUFLEN;
    int                                 rc;
    int                                 line_num;
    int                                 optlen;
    char *                              p;
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusGFSName(globus_l_gfs_data_read_share_file);
    GlobusGFSDebugEnter();

    fptr = fopen(filename, "r");
    if(fptr == NULL)
    {
        res = GlobusGFSErrorGeneric("Problem opening share file.\n");
        goto error_param;
    }

    line_num = 0;
    linebuf = globus_malloc(linebuflen);
    if(!linebuf)
    {
        res = GlobusGFSErrorGeneric("Problem allocating memory.\n");
        goto error_mem;
    }
    optionbuf = globus_malloc(linebuflen);
    if(!optionbuf)
    {
        res = GlobusGFSErrorGeneric("Problem allocating memory.\n");
        goto error_mem;
    }
    valuebuf = globus_malloc(linebuflen);
    if(!valuebuf)
    {
        res = GlobusGFSErrorGeneric("Problem allocating memory.\n");
        goto error_mem;
    }
    
    while(fgets(linebuf, linebuflen, fptr) != NULL)
    {
        p = linebuf;
        while(p && linebuf[strlen(linebuf) - 1] != '\n')
        {
            char                        part_line[GLOBUS_L_GFS_LINEBUFLEN];

            p = fgets(part_line, GLOBUS_L_GFS_LINEBUFLEN, fptr);
            if(p != NULL)
            {
                linebuflen += GLOBUS_L_GFS_LINEBUFLEN;
                linebuf = globus_realloc(linebuf, linebuflen);
                if(!linebuf)
                {
                    res = GlobusGFSErrorGeneric("Problem allocating memory.\n");
                    goto error_mem;
                }
                strncat(linebuf, part_line, linebuflen);
                
                optionbuf = globus_realloc(optionbuf, linebuflen);
                if(!optionbuf)
                {
                    res = GlobusGFSErrorGeneric("Problem allocating memory.\n");
                    goto error_mem;
                }
                valuebuf = globus_realloc(valuebuf, linebuflen);
                if(!valuebuf)
                {
                    res = GlobusGFSErrorGeneric("Problem allocating memory.\n");
                    goto error_mem;
                }
            }
        }
        line_num++;
        p = linebuf;
        optlen = 0;               
        while(*p && isspace(*p))
        {
            p++;
        }
        if(*p == '\0')
        {
            continue;
        }
        if(*p == '#')
        {
            continue;
        }        

        if(*p == '"')
        {
            rc = sscanf(p, "\"%[^\"]\"", optionbuf);
            optlen = 2;
        }
        else
        {
            rc = sscanf(p, "%s", optionbuf);
        }        
        if(rc != 1)
        {   
            goto error_parse;
        }
        optlen += strlen(optionbuf);
        p = p + optlen;
               
        optlen = 0;
        while(*p && isspace(*p))
        {
            p++;
        }
        if(*p == '"')
        {
            rc = sscanf(p, "\"%[^\"]\"", valuebuf);
            optlen = 2;
        }
        else
        {
            rc = sscanf(p, "%s", valuebuf);
        }        
        if(rc != 1)
        {   
            goto error_parse;
        }        
        optlen += strlen(valuebuf);
        p = p + optlen;        
        while(*p && isspace(*p))
        {
            p++;
        }
        if(*p && !isspace(*p))
        {
            goto error_parse;
        }
        
        if(!strcmp(optionbuf, "share_path"))
        {
            *share_path = strdup(valuebuf);
            globus_url_string_hex_decode(*share_path);
        }
    }

    fclose(fptr);
    
    globus_free(linebuf);
    globus_free(valuebuf);
    globus_free(optionbuf);
    
    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error_parse:
    fclose(fptr);
    GlobusGFSErrorGenericStr(res,
        ("Problem parsing share file %s: line %d.\n", filename, line_num));
error_param:
error_mem:

    GlobusGFSDebugExitWithError();
    return res;
}

/* check sharing_state_dir perms, return 0 if allowed, -1 if not. */
static
int
globus_l_gfs_data_check_sharing_perms(
    globus_l_gfs_data_session_t *       session_handle)
{
    struct stat                         statbuf;
    int                                 rc = -1;
    char *                              dir = session_handle->sharing_state_dir;

    if(stat(dir, &statbuf) == 0)
    {
        /* case 1: personal state directory
           owned by user; must have no group and world perms */
        if(statbuf.st_uid == session_handle->uid)
        {
            if((statbuf.st_mode & 
                (S_IRGRP | S_IXGRP | S_IWGRP | S_IROTH | S_IWOTH | S_IXOTH)) == 0)
            {
                rc = 0;
            }
            else
            {
                globus_gfs_log_message(
                    GLOBUS_GFS_LOG_ERR,
                    "Sharing error. Sharing state dir %s is owned by "
                    "authenticated user but has group or world permissions.\n",
                    dir);
            }
        }
    
        /* case 2: community state directory
           world or group writable
           not world or group readable
           owned by root, must have sticky bit set. */
#ifndef WIN32
        else if((statbuf.st_mode & (S_IWGRP | S_IWOTH)) != 0)
        {
            if(statbuf.st_uid == 0 && ((statbuf.st_mode & S_ISVTX) != 0) && 
                ((statbuf.st_mode & (S_IRGRP | S_IROTH)) == 0))
            {
                rc = 0;
            }
            else
            {
                globus_gfs_log_message(
                    GLOBUS_GFS_LOG_ERR,
                    "Sharing error. Sharing state dir %s is group or "
                    "world-writable; is not owned by root, is group or "
                    "world readable, or does not have the sticky bit set.\n",
                    dir);
            }
        }
#endif
        /* permissions not supported */
        else
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_ERR,
                "Sharing error. Sharing state dir %s has unsafe ownership or "
                "permissions.\n",
                dir);
        }
    }
    else
    {
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_ERR,
            "Sharing error. Sharing state dir %s doesn't exist or can't be "
            "accessed.\n",
            dir);
    }
    
    return rc;
}

/* check current username and groups against whitelist and blacklist */
static
globus_bool_t
globus_l_gfs_data_check_sharing_allowed(
    globus_l_gfs_data_session_t *       session_handle)
{
    GlobusGFSName(globus_l_gfs_data_check_sharing_allowed);
    GlobusGFSDebugEnter();
    char *                              user_allow;
    char *                              user_deny;
    char *                              group_allow;
    char *                              group_deny;
    struct group *                      grent;
    char *                              user;
    char *                              group;
    char *                              ptr;
    int                                 i;
    char *                              match_user;
    globus_bool_t                       allowed = GLOBUS_FALSE;
    globus_bool_t                       explicitly = GLOBUS_FALSE;

    match_user = session_handle->username;

    user_deny = globus_libc_strdup(
        globus_gfs_config_get_string("sharing_users_deny"));
    user_allow = globus_libc_strdup(
        globus_gfs_config_get_string("sharing_users_allow"));

    /* check user against allow/deny list.
     * if the deny list includes the user, he is immediately denied.
     * if the allow list includes the user, he is immediately allowed.
     * if the allow list is unset, he is allowed pending the group check.
     * if the allow list is set and does not include the user, he is denied 
       pending the group check.
     */
    if(user_allow == NULL)
    {
        allowed = GLOBUS_TRUE;
        explicitly = GLOBUS_FALSE;
    }
    else
    {
        allowed = GLOBUS_FALSE;
        explicitly = GLOBUS_FALSE;
        
        user = user_allow;
        while((ptr = strchr(user, ',')) != NULL && !allowed)
        {
            *ptr = '\0';
            if(strncmp(match_user, user, strlen(match_user)) == 0)
            {
                allowed = GLOBUS_TRUE;
                explicitly = GLOBUS_TRUE;
            }
            user = ptr + 1;
        }
        if(ptr == NULL && !allowed)
        {
           if(strncmp(match_user, user, strlen(match_user)) == 0)
            {
                allowed = GLOBUS_TRUE;
                explicitly = GLOBUS_TRUE;
            }
        }
        globus_free(user_allow);
    }
    if(user_deny != NULL)
    {
        user = user_deny;
        while((ptr = strchr(user, ',')) != NULL && allowed)
        {
            *ptr = '\0';
            if(strncmp(match_user, user, strlen(match_user)) == 0)
            {
                allowed = GLOBUS_FALSE;
                explicitly = GLOBUS_TRUE;
            }
            user = ptr + 1;
        }
        if(ptr == NULL && allowed)
        {
           if(strncmp(match_user, user, strlen(match_user)) == 0)
            {
                allowed = GLOBUS_FALSE;
                explicitly = GLOBUS_TRUE;
            }
        }
        globus_free(user_deny);
    }            

    if(explicitly)
    {
        goto finish;
    }
    

    group_deny = globus_libc_strdup(
        globus_gfs_config_get_string("sharing_groups_deny"));
    group_allow = globus_libc_strdup(
        globus_gfs_config_get_string("sharing_groups_allow"));

    /* check groups against allow/deny list.
     * if the deny list includes a group it is immediately denied.
     * if the allow list includes a group, it is allowed.
     * if the allow list is set and does not include a group, it is denied.
     * if the allow list is unset, the pending user list result stands.
     */
    if(group_allow != NULL)
    {
        group = group_allow;
        while((ptr = strchr(group, ',')) != NULL && !allowed)
        {
            *ptr = '\0';
            
            grent = getgrnam(group);
            if(grent)
            {
                if(session_handle->gid == grent->gr_gid)
                {
                    allowed = GLOBUS_TRUE;
                }
                for(i = 0; i < session_handle->gid_count && !allowed; i++)
                {
                    if(session_handle->gid_array[i] == grent->gr_gid)
                    {
                        allowed = GLOBUS_TRUE;
                    }
                }
            }
            group = ptr + 1;
        }
        if(ptr == NULL && !allowed)
        {
            grent = getgrnam(group);
            if(grent)
            {
                if(session_handle->gid == grent->gr_gid)
                {
                    allowed = GLOBUS_TRUE;
                }
                for(i = 0; i < session_handle->gid_count && !allowed; i++)
                {
                    if(session_handle->gid_array[i] == grent->gr_gid)
                    {
                        allowed = GLOBUS_TRUE;
                    }
                }
            }
        }
        globus_free(group_allow);
    }
    if(allowed && group_deny != NULL)
    {
        group = group_deny;
        while((ptr = strchr(group, ',')) != NULL && allowed)
        {
            *ptr = '\0';
            grent = getgrnam(group);
            if(grent)
            {
                if(session_handle->gid == grent->gr_gid)
                {
                    allowed = GLOBUS_FALSE;
                }
                for(i = 0; i < session_handle->gid_count && allowed; i++)
                {
                    if(session_handle->gid_array[i] == grent->gr_gid)
                    {
                        allowed = GLOBUS_FALSE;
                    }
                }
            }
            group = ptr + 1;
        }
        if(ptr == NULL && allowed)
        {
            grent = getgrnam(group);
            if(grent)
            {
                if(session_handle->gid == grent->gr_gid)
                {
                    allowed = GLOBUS_FALSE;
                }
                for(i = 0; i < session_handle->gid_count && allowed; i++)
                {
                    if(session_handle->gid_array[i] == grent->gr_gid)
                    {
                        allowed = GLOBUS_FALSE;
                    }
                }
            }
        }
    }    
    if(group_deny)
    {
        globus_free(group_deny);
    }

finish:

    GlobusGFSDebugExit();
    return allowed;
}


#define GLOBUS_SHARING_PREFIX ":globus-sharing:"

static
void
globus_l_gfs_data_authorize(
    globus_l_gfs_data_operation_t *     op,
    const gss_ctx_id_t                  context,
    globus_gfs_session_info_t *         session_info)
{
    void *                              remote_data_arg = NULL;
    int                                 rc;
    globus_result_t                     res;
    int                                 gid;
    char *                              pw_file;
    char *                              usr;
    char *                              grp;
    char *                              pw_hash;
    char                                authz_usr[USER_NAME_MAX];
    struct passwd *                     pwent = NULL;
    struct group *                      grent = NULL;
    int                                 auth_level;
    char *                              chroot_dir = NULL;
    char *                              custom_home_dir;
    char *                              sharing_dn = NULL;
    char *                              shared_user_str = NULL;
    globus_bool_t                       sharing_attempted = GLOBUS_FALSE;
    char *                              desired_user_cert = NULL;
    char *                              share_file = NULL;
    char *                              process_username = NULL;
    GlobusGFSName(globus_l_gfs_data_authorize);
    GlobusGFSDebugEnter();

    globus_gfs_log_event(
        GLOBUS_GFS_LOG_INFO,
        GLOBUS_GFS_LOG_EVENT_START,
        "session.authz",
        0,
        "user=%s DN=\"%s\"",
        session_info->username,
        session_info->subject ? session_info->subject : "");

    auth_level = globus_i_gfs_config_int("auth_level");
    pw_file = (char *) globus_i_gfs_config_string("pw_file");
    process_username = globus_i_gfs_config_string("process_user");
    /* if there is a subject or del cred we are using gsi, 
        look it up in the gridmap */
    sharing_dn = globus_i_gfs_config_string("sharing_dn");
    op->session_handle->sharing = (sharing_dn != NULL);
    
    if(session_info->subject != NULL || session_info->del_cred != NULL)
    {
        if(sharing_dn && !strcmp(sharing_dn, session_info->subject))
        {
            if(session_info->username && 
                strncmp(session_info->username, 
                    GLOBUS_SHARING_PREFIX, 
                    strlen(GLOBUS_SHARING_PREFIX)) == 0)
            {
                char *                  usr_tmp;
                char *                  sub_tmp;
                char *                  crt_tmp;
                char *                  ptr;
                globus_size_t           cert_len = 0;
                char *                  tmp;
                globus_gsi_cred_handle_t tmp_cred_handle;
                                                        
                shared_user_str = 
                    session_info->username + strlen(GLOBUS_SHARING_PREFIX);
                ptr = shared_user_str;
                
                if((usr_tmp = globus_i_gfs_kv_getval(ptr, "USER", 1)) != NULL)
                {   
                    session_info->map_user = GLOBUS_FALSE;
                }
                else
                {
                    usr_tmp = globus_libc_strdup(GLOBUS_MAPPING_STRING);
                    session_info->map_user = GLOBUS_TRUE;
                }

                if((op->session_handle->sharing_id = 
                    globus_i_gfs_kv_getval(ptr, "ID", 1)) == NULL)
                {
                    res = GlobusGFSErrorGeneric(
                        "Invalid format for " GLOBUS_SHARING_PREFIX ". Missing ID.");
                    goto pwent_error;
                }
                tmp = op->session_handle->sharing_id;
                while(*tmp)
                {
                    *tmp = tolower(*tmp);
                    if(!isxdigit(*tmp) && *tmp != '-')
                    {
                        res = GlobusGFSErrorGeneric(
                            "Invalid format for " GLOBUS_SHARING_PREFIX ". Invalid character in ID.");
                        goto pwent_error;
                    }
                    tmp++;
                }

                if((crt_tmp = globus_i_gfs_kv_getval(ptr, "CERT", 0)) != NULL)
                {   
                    desired_user_cert = malloc(strlen(crt_tmp + 1));
                    res = globus_l_gfs_base64_decode(
                        (globus_byte_t *) crt_tmp,
                        (globus_byte_t *) desired_user_cert, &cert_len);
                    desired_user_cert[cert_len] = 0;
                    globus_free(crt_tmp);
                }
                else
                {
                    res = GlobusGFSErrorGeneric(
                        "Invalid format for " GLOBUS_SHARING_PREFIX ". Missing CERT.");
                }
                if(res != GLOBUS_SUCCESS)
                {
                    goto pwent_error;
                }
                res = globus_gsi_cred_read_cert_buffer(
                    desired_user_cert, &tmp_cred_handle, NULL, NULL, &sub_tmp);
                if(res != GLOBUS_SUCCESS)
                {
                    goto pwent_error;
                }
                
                globus_gfs_log_message(
                    GLOBUS_GFS_LOG_INFO,
                    "DN %s has provided sharing credentials for DN %s.\n",
                    session_info->subject, sub_tmp);

                globus_free(session_info->subject);
                session_info->subject = sub_tmp;

                res = globus_gsi_cred_verify_cert_chain_when(
                    tmp_cred_handle, NULL, 0);
                globus_gsi_cred_handle_destroy(tmp_cred_handle);
                if(res != GLOBUS_SUCCESS)
                {
                    goto pwent_error;
                }

                if((op->session_handle->sharing_sharee =
                    globus_i_gfs_kv_getval(ptr, "SHAREE", 1)) == NULL)
                {
                    res = GlobusGFSErrorGeneric(
                        "Invalid arguments for " GLOBUS_SHARING_PREFIX ". Missing SHAREE.");
                    goto pwent_error;
                }
                
                globus_free(session_info->username);
                session_info->username = usr_tmp;

                sharing_attempted = GLOBUS_TRUE;
            }   
        }

        if(!sharing_attempted && session_info->username && 
                strncmp(session_info->username, 
                    GLOBUS_SHARING_PREFIX, 
                    strlen(GLOBUS_SHARING_PREFIX)) == 0)
        {
            GlobusGFSErrorGenericStr(res,
                ("Sharing not allowed with DN %s", session_info->subject));
            goto pwent_error;
        }
                
        if(!(auth_level & GLOBUS_L_GFS_AUTH_NOGRIDMAP))
        {
            if(globus_i_gfs_config_bool("cas") && 
                (context || sharing_attempted))
            {
                globus_bool_t           free_usr = GLOBUS_FALSE;
                
                if(session_info->map_user)
                {
                    usr = NULL;
                }
                else
                {
                    usr = session_info->username;
                }

                *authz_usr = '\0';
                
                if(sharing_attempted)
                {
                    res = globus_gss_assist_map_and_authorize_sharing(
                        desired_user_cert,
                        context,
                        usr,
                        authz_usr,
                        USER_NAME_MAX);
                    
                    globus_free(desired_user_cert);
                    desired_user_cert = NULL;
                }
                else
                {
                    res = globus_gss_assist_map_and_authorize(
                        context,
                        FTP_SERVICE_NAME,
                        usr,
                        authz_usr,
                        USER_NAME_MAX);
                }
                if(free_usr)
                {
                    globus_free(usr);
                }
                if(res != GLOBUS_SUCCESS)
                {
                    goto pwent_error;
                }
                /* if res=success and authz_usr is empty, assume usr is ok
                 * and some callout just didn't copy it to authz_usr */
                if(*authz_usr != '\0')
                {
                    usr = authz_usr;

                    if(session_info->username)
                    {
                        globus_free(session_info->username);
                    }
                    session_info->username = globus_libc_strdup(usr);
                }
            }
            else
            {
                if(session_info->map_user)
                {
                    rc = globus_gss_assist_gridmap(
                        (char *) session_info->subject, &usr);
                    if(rc != 0)
                    {
                        GlobusGFSErrorGenericStr(res,
                            ("Gridmap lookup failure: unable to map '%s'.",
                            session_info->subject));
                        goto pwent_error;
                    }
                }
                else
                {
                    rc = globus_gss_assist_userok(
                        session_info->subject, session_info->username);
                    usr = globus_libc_strdup(session_info->username);
                    if(rc != 0)
                    {
                        GlobusGFSErrorGenericStr(res,
                            ("Gridmap lookup failure: "
                            "unable to map '%s' to '%s'.",
                            session_info->subject,
                            session_info->username));
                        goto pwent_error;
                    }
                }
                if(session_info->username)
                {
                    globus_free(session_info->username);
                }
                session_info->username = usr;
            }
        }
        else
        {
            if(session_info->map_user == GLOBUS_TRUE ||
                session_info->username == NULL)
            {
                pwent = globus_l_gfs_getpwuid(getuid());
                if(pwent == NULL)
                {
                    res = GlobusGFSErrorGeneric(
                        "Invalid passwd entry for current user.");
                    goto pwent_error;
                }
                if(session_info->username)
                {
                    globus_free(session_info->username);
                }
                session_info->username = globus_libc_strdup(pwent->pw_name);
            }
        }
#ifndef WIN32
        if(pwent == NULL)
        {
            if(auth_level & GLOBUS_L_GFS_AUTH_NOSETUID)
            {
                pwent = globus_l_gfs_getpwuid(getuid());
                if(pwent == NULL)
                {
                    res = GlobusGFSErrorGeneric(
                        "Invalid passwd entry for current user.");
                    goto pwent_error;
                }
            }
            else if (process_username)
            {
                pwent = globus_l_gfs_getpwnam(process_username);
                if(pwent == NULL)
                {
                    res = GlobusGFSErrorGeneric(
                        "Configured process user is invalid.");
                    goto pwent_error;
                }
            }
            else
            {
                pwent = globus_l_gfs_getpwnam(session_info->username);
                if(pwent == NULL)
                {
                    GlobusGFSErrorGenericStr(res,
                        ("Mapped user '%s' is invalid.",
                        session_info->username));
                    goto pwent_error;
                }
            }
        }
#endif
        if(pwent != NULL)
        {
            gid = pwent->pw_gid;
            grent = globus_l_gfs_getgrgid(gid);
        }
        if(grent == NULL && !(auth_level & GLOBUS_L_GFS_AUTH_NOSETUID))
        {
            GlobusGFSErrorGenericStr(res,
                ("Invalid group id assigned to user '%s'.",
                session_info->username));
            goto pwent_error;
        }
    }
    /* if anonymous use and we are allowing it */
    else if(globus_i_gfs_config_bool("allow_anonymous") &&
        globus_i_gfs_config_is_anonymous(session_info->username))
    {
        /* if we are root, set to anon user */
        if(getuid() == 0)
        {
            usr = globus_i_gfs_config_string("anonymous_user");
            if(usr == NULL)
            {
                res = GlobusGFSErrorGeneric("No anonymous user set.");
                goto pwent_error;
            }
            pwent = globus_l_gfs_getpwnam(usr);
            if(pwent == NULL)
            {
                res = GlobusGFSErrorGeneric("Invalid anonymous user set.");
                goto pwent_error;
            }
            grp = globus_i_gfs_config_string("anonymous_group");
            if(grp == NULL)
            {
                grent = globus_l_gfs_getgrgid(pwent->pw_gid);
                if(grent == NULL)
                {
                    res = GlobusGFSErrorGeneric(
                        "Invalid group id assigned to anonymous user.");
                    goto pwent_error;
                }
            }
            else
            {
                grent = globus_l_gfs_getgrnam(grp);
                if(grent == NULL)
                {
                    res = GlobusGFSErrorGeneric(
                        "Invalid anonymous group set.");
                    goto pwent_error;
                }
            }
            gid = grent->gr_gid;
        }
        /* if not root, just run as is */
        else
        {
#ifndef WIN32
            pwent = globus_l_gfs_getpwuid(getuid());
            if(pwent == NULL)
            {
                res = GlobusGFSErrorGeneric(
                    "Invalid passwd entry for current user.");
                goto pwent_error;
            }
            gid = pwent->pw_gid;
            grent = globus_l_gfs_getgrgid(pwent->pw_gid);
            if(grent == NULL)
            {
                res = GlobusGFSErrorGeneric(
                    "Invalid group id assigned to current user.");
                goto pwent_error;
            }
#endif
        }
#ifndef WIN32

        /* since this is anonymous change the user name */
        op->session_handle->real_username = globus_libc_strdup(pwent->pw_name);
        if(pwent->pw_name != NULL)
        {
            globus_free(pwent->pw_name);
        }
        pwent->pw_name = strdup("anonymous");
        if(grent->gr_name != NULL)
        {
            globus_free(grent->gr_name);
        }
        grent->gr_name = strdup("anonymous");
#endif
    }
    else if(pw_file != NULL)
    {
        /* if we have not yet looked it up for this user */
        if(globus_l_gfs_data_pwent == NULL)
        {
#           ifdef HAVE_FGETPWENT
            {
                FILE * pw = fopen(pw_file, "r");
                if(pw == NULL)
                {
                    res = GlobusGFSErrorGeneric(
                        "Invalid passwd file set.");
                    goto pwent_error;
                }

                globus_libc_lock();
                do
                {
                    pwent = fgetpwent(pw);
                }
                while(pwent != NULL &&
                    strcmp(pwent->pw_name, session_info->username) != 0);
                fclose(pw);

                if(pwent == NULL)
                {
                    globus_libc_unlock();
                    res = GlobusGFSErrorGeneric("Invalid user.");
                    goto pwent_error;
                }
                globus_l_gfs_data_pwent = globus_l_gfs_pw_copy(pwent);
                globus_libc_unlock();
            }
#           else
            {
                res = GlobusGFSErrorGeneric("Passwd file not supported.");
                goto pwent_error;
            }
#endif
        }
        else
        {
            /* if already looked up (and setuid()) use global value */
            if(strcmp(pwent->pw_name, session_info->username) != 0)
            {
                res = GlobusGFSErrorGeneric(
                    "Invalid user for current session.");
                goto pwent_error;
            }
        }
        pwent = globus_l_gfs_pw_copy(globus_l_gfs_data_pwent);
        grent = globus_l_gfs_getgrgid(pwent->pw_gid);
        if(grent == NULL)
        {
            GlobusGFSErrorGenericStr(res,
                ("Invalid group id assigned to user '%s'.",
                session_info->username));
            goto pwent_error;
        }
        pw_hash = DES_crypt(session_info->password, pwent->pw_passwd);
        if(strcmp(pw_hash, pwent->pw_passwd) != 0)
        {
            res = GlobusGFSErrorGeneric("Invalid user.");
            goto pwent_error;
        }
        gid = pwent->pw_gid;
    }
    else
    {
        res = GlobusGFSErrorGeneric("Access denied by configuration.");
        goto pwent_error;
    }
    
    /* check that account is not disabled */
    if(pwent && !globus_i_gfs_config_bool("allow_disabled_login"))
    {
        res = globus_l_gfs_validate_pwent(pwent);
        if(res != GLOBUS_SUCCESS)
        {
            char *                     errmsg;
            errmsg = globus_error_print_friendly(globus_error_peek(res));
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_ERR,
                "Access denied for user '%s': %s\n", 
                session_info->username, errmsg);
            globus_free(errmsg);
            res = GlobusGFSErrorGeneric(
                "Access denied, user's system account is disabled.");
            goto pwent_error;
        }
    }


    /* change process ids */
    if(!(auth_level & GLOBUS_L_GFS_AUTH_NOSETUID))
    {
        rc = setgid(gid);
        if(rc != 0)
        {
            res = GlobusGFSErrorSystemError(
                "Unable to set the gid of the server process.", errno);
            goto uid_error;
        }
        if(getuid() == 0)
        {
            char *                          name;
            if(op->session_handle->real_username == NULL)
            {
                name = pwent->pw_name;
            }
            else
            {
                name = op->session_handle->real_username;
            }
            rc = globus_libc_initgroups(name, gid);
            if(rc != 0)
            {
                res = GlobusGFSErrorGeneric(
                    "Unable to set the supplemental groups of the server "
                    "process.");
                goto uid_error;
            }
        }
        

        if(pwent->pw_uid == 0 && !globus_i_gfs_config_bool("allow_root"))
        {
            res = GlobusGFSErrorGeneric(
                "User was mapped as root but root is not allowed.");
            goto uid_error;
        }

        if((chroot_dir = globus_i_gfs_config_string("chroot_path")) != NULL)
        {
            chdir(chroot_dir);
            rc = chroot(chroot_dir);
            if(rc != 0)
            {
                res = GlobusGFSErrorSystemError(
                    "Unable to chroot.", errno);
                    goto uid_error;
            }
        }
        rc = setuid(pwent->pw_uid);
        if(rc != 0)
        {
            res = GlobusGFSErrorSystemError(
                "Unable to set the uid of the server process.", errno);
            goto uid_error;
        }
    }
#ifndef WIN32
    op->session_handle->uid = pwent->pw_uid;
    op->session_handle->gid = pwent->pw_gid;
    op->session_handle->gid_count = getgroups(0, NULL);
    op->session_handle->gid_array = (gid_t *) globus_malloc(
        op->session_handle->gid_count * sizeof(gid_t));
    getgroups(op->session_handle->gid_count, op->session_handle->gid_array);
#endif

    op->session_handle->username = globus_libc_strdup(session_info->username);

    if(pwent && pwent->pw_dir)
    {
        op->session_handle->true_home = globus_libc_strdup(pwent->pw_dir);
    }
    else
    {
        op->session_handle->true_home = globus_l_gfs_defaulthome();
    }

    custom_home_dir = globus_i_gfs_config_string("home_dir");
    if(custom_home_dir)
    {
        char *                          var_dir;

        var_dir = globus_l_gfs_data_update_var_path(
            op->session_handle, custom_home_dir);
            
        op->session_handle->home_dir = var_dir;
    }
    else
    {
        op->session_handle->home_dir = 
            globus_libc_strdup(op->session_handle->true_home);
    }
    
    if(sharing_dn)
    {
        char *                      sharing_state;
        
        sharing_state = globus_i_gfs_config_string("sharing_state_dir");
        op->session_handle->sharing_state_dir = 
            globus_l_gfs_data_update_var_path(
                op->session_handle, 
                sharing_state ? sharing_state : "$HOME/.globus/sharing");

        if(!op->session_handle->sharing_state_dir)
        {
            res = GlobusGFSErrorMemory("sharing state dir");
            goto pwent_error;
        }
        
        if(sharing_attempted)
        {
            char *                      share_path = NULL;
            char *                      tmp_restrict;
            globus_list_t *             tmp_list;
            struct stat                 statbuf;

            if(!globus_l_gfs_data_check_sharing_allowed(op->session_handle))
            {
                GlobusGFSErrorGenericStr(res,
                    ("Sharing not allowed for user '%s'.",
                    session_info->username));
                goto pwent_error;
            }
     
            share_file = globus_common_create_string(
                "%s/share-%s",
                op->session_handle->sharing_state_dir,
                op->session_handle->sharing_id);

            rc = access(share_file, F_OK);

            /* check share_file ownership and perms */
            if(rc == 0 && stat(share_file, &statbuf) == 0)
            {
                /* ownership */
                if(statbuf.st_uid != op->session_handle->uid)
                {
                    globus_gfs_log_message(
                        GLOBUS_GFS_LOG_ERR,
                        "Sharing error. Share file %s not owned by "
                        "authenticated user %s UID %d.\n",
                        share_file, session_info->username,
                        op->session_handle->uid);

                    rc = -1;
                }

                /* regular file */
                if(!S_ISREG(statbuf.st_mode))
                {
                    globus_gfs_log_message(
                        GLOBUS_GFS_LOG_ERR,
                        "Sharing error. Share file %s is not a regular file.\n",
                        share_file);

                    rc = -1;
                }

                /* no group/world permissions */
                if((statbuf.st_mode &
                    (S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH )) != 0)
                {
                    globus_gfs_log_message(
                        GLOBUS_GFS_LOG_ERR,
                        "Sharing error. Share file %s has group or world permissions set.\n",
                        share_file);

                    rc = -1;
                }
            }

            if(rc == 0)
            {
                rc = globus_l_gfs_data_check_sharing_perms(op->session_handle);
            }

            if(rc != 0)
            {
                GlobusGFSErrorGenericStr(res,
                    ("Sharing not enabled for user '%s' from share id '%s'.",
                    session_info->username, op->session_handle->sharing_id));
                goto pwent_error;
            }

            res = globus_l_gfs_data_read_share_file(share_file, &share_path);
            if(res == GLOBUS_SUCCESS && share_path && share_path[0] == '/')
            {
                op->session_handle->chroot_path = share_path;
            }
            else
            {
                globus_gfs_log_message(
                    GLOBUS_GFS_LOG_ERR,
                    "Sharing error.  Invalid share_path or problem parsing "
                    "share file %s.\n",
                    share_file);
                GlobusGFSErrorGenericStr(res,
                    ("Sharing error for user '%s' from share id '%s'.",
                    session_info->username, op->session_handle->sharing_id));
                goto pwent_error;
            }
            globus_free(share_file);
            share_file = NULL;
            
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_INFO,
                "Access allowed for sharing of user '%s' from share id '%s'.  "
                "Sharee '%s' is restricted to '%s'.\n",
                session_info->username, 
                op->session_handle->sharing_id, 
                op->session_handle->sharing_sharee, 
                op->session_handle->chroot_path);
            
            if(op->session_handle->chroot_path[1] == '\0')
            {
                globus_free(op->session_handle->chroot_path);
                op->session_handle->chroot_path = NULL;
            }


            /* add state dir to sharing rp list */
            if(!globus_list_empty(globus_l_gfs_path_alias_list_sharing))
            {
                tmp_restrict = globus_common_create_string(
                    "N%s,N~/.*", op->session_handle->sharing_state_dir);
            }
            else
            {
                tmp_restrict = globus_common_create_string(
                    "RW/,N%s,N~/.*", op->session_handle->sharing_state_dir);
            }    
#ifdef WIN32
            if(strstr(tmp_restrict, ":/"))
            {
                char *                  tp;
                tp = strstr(tmp_restrict, ":/");
                tp--; 
                tp[1] = tp[0];
                tp[0] = '/';
            }
#endif
            res = globus_l_gfs_data_parse_restricted_paths(
                NULL, tmp_restrict, &tmp_list, 0);
            while(!globus_list_empty(tmp_list))
            {
                globus_list_insert(
                    &globus_l_gfs_path_alias_list_sharing, 
                    globus_list_remove(&tmp_list, tmp_list));
            }
            globus_free(tmp_restrict);
            
            if(res != GLOBUS_SUCCESS)
            {
                goto pwent_error;
            }
        }
    }
    
    globus_l_gfs_data_update_restricted_paths(
        op->session_handle, &globus_l_gfs_path_alias_list_base);
    globus_l_gfs_data_update_restricted_paths(
        op->session_handle, &globus_l_gfs_path_alias_list_sharing);

    if(sharing_attempted)
    {
        op->session_handle->active_rp_list = &globus_l_gfs_path_alias_list_sharing;
        if(op->session_handle->chroot_path)
        {
            if(op->session_handle->home_dir)
            {
                globus_free(op->session_handle->home_dir);
            }
            op->session_handle->home_dir = strdup("/");
        }
    }
    else
    {
        op->session_handle->active_rp_list = &globus_l_gfs_path_alias_list_base;
    }
       
    if(!globus_i_gfs_config_bool("use_home_dirs") || 
        op->session_handle->home_dir == NULL)
    {
        if(op->session_handle->home_dir)
        {
            globus_free(op->session_handle->home_dir);
        }
        op->session_handle->home_dir = strdup("/");
    }  

    if(op->session_handle->real_username == NULL)
    {
	if(pwent)
        {
            op->session_handle->real_username = 
                globus_libc_strdup(pwent->pw_name);
        }
        else
        {
            op->session_handle->real_username = 
                globus_libc_strdup(op->session_handle->username);
        }
    }
    	
    globus_gfs_log_event(
        GLOBUS_GFS_LOG_INFO,
        GLOBUS_GFS_LOG_EVENT_END,
        "session.authz",
        0,
        "localuser=%s DN=\"%s\"%s",
        op->session_handle->real_username,
        session_info->subject ? session_info->subject : "",
        sharing_attempted ? " sharing=yes" : "");

    rc = globus_i_gfs_acl_init(
        &op->session_handle->acl_handle,
        context,
        session_info->subject,
        session_info->username,
        session_info->password,
        session_info->host_id,
        &res,
        globus_l_gfs_data_auth_init_cb,
        op);
    if(rc < 0)
    {
        res = GlobusGFSErrorGeneric(
            "ACL initialization error.");
        goto acl_error;
    }
    else if(rc == GLOBUS_GFS_ACL_COMPLETE)
    {
        globus_l_gfs_data_auth_init_cb(NULL, GFS_ACL_ACTION_INIT, op, res);
    }

    if(pwent)
    {
        globus_l_gfs_pw_free(pwent);
    }
    if(grent)
    {
        globus_l_gfs_gr_free(grent);
    }

    GlobusGFSDebugExit();
    return;

acl_error:
uid_error:
pwent_error:

    globus_gfs_log_event(
        GLOBUS_GFS_LOG_INFO,
        GLOBUS_GFS_LOG_EVENT_END,
        "session.authz",
        res,
        "DN=\"%s\"",
        session_info->subject ? session_info->subject : "");

    if(share_file)
    {
        globus_free(share_file);
    }
    if(desired_user_cert)
    {
        globus_free(desired_user_cert);
    }
    if(pwent != NULL)
    {
        globus_l_gfs_pw_free(pwent);
    }
    if(grent)
    {
        globus_l_gfs_gr_free(grent);
    }
    {
        globus_bool_t                   destroy_session = GLOBUS_FALSE;
        globus_bool_t                   destroy_op = GLOBUS_FALSE;
        globus_gfs_finished_info_t      finished_info;
        memset(&finished_info, '\0', sizeof(globus_gfs_finished_info_t));

        finished_info.result = res;
        finished_info.info.session.session_arg = NULL;

        if(op->callback == NULL)
        {
            globus_gfs_ipc_reply_session(
                op->ipc_handle, &finished_info);
        }
        else
        {
            op->callback(
                &finished_info,
                op->user_arg);
        }

        globus_mutex_lock(&op->session_handle->mutex);
        {
            /* dec session handle now since we won't get a stop_session */
            op->session_handle->ref--;
            GFSDataOpDec(op, destroy_op, destroy_session);
            remote_data_arg = globus_l_gfs_data_check(
                op->session_handle, op->data_handle);
        }
        globus_mutex_unlock(&op->session_handle->mutex);
        globus_assert(destroy_op);
        globus_l_gfs_data_fire_cb(op, remote_data_arg, destroy_session);
        globus_l_gfs_data_operation_destroy(op);
    }
    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_load_safe(
    const char *                        conf_name,
    const char *                        defaults,
    globus_hashtable_t *                safe_table)
{
    globus_xio_driver_list_ent_t *      ent;
    char *                              value;
    char *                              driver_desc;
    globus_list_t *                     list;
    globus_result_t                     result;

    value = globus_i_gfs_config_string(conf_name);
    if(value == NULL)
    {
        value = globus_common_create_string(defaults);
    }
    list = globus_list_from_string(value, ',', NULL);
    globus_free(value);

    while(!globus_list_empty(list))
    {
        driver_desc = (char *) globus_list_remove(&list, list);

        result = globus_xio_driver_list_create_ent(
            driver_desc,
            NULL,
            GLOBUS_TRUE,
            &ent);
        if(result != GLOBUS_SUCCESS)
        {
            char *                      msg = NULL;
            /* just log the error */
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_ERR,
                "Some network stack drivers failed to load: %s\n",
                msg = globus_error_print_friendly(globus_error_peek(result)));
                
            if(msg)
            {
                globus_free(msg);
            }
        }
        else
        {
            /* add to the table */
            globus_hashtable_insert(safe_table, ent->driver_name, ent);
        }
        globus_free(driver_desc);
    }
}

static globus_result_t
globus_l_gfs_base64_encode(
    const unsigned char *               inbuf,
    globus_size_t                       in_len,
    globus_byte_t *                     outbuf,
    globus_size_t *                     out_len)
{
    int                                 i;
    int                                 j;
    unsigned char                       c;
    for (i=0,j=0; i < in_len; i++)
    {
        switch (i%3)
        {
            case 0:
                outbuf[j++] = globus_l_gfs_base64_n[inbuf[i]>>2];
                c = (inbuf[i]&3)<<4;
                break;
            case 1:
                outbuf[j++] = globus_l_gfs_base64_n[c|inbuf[i]>>4];
                c = (inbuf[i]&15)<<2;
                break;
            case 2:
                outbuf[j++] = globus_l_gfs_base64_n[c|inbuf[i]>>6];
                outbuf[j++] = globus_l_gfs_base64_n[inbuf[i]&63];
                c = 0;
                break;
            default:
                globus_assert(0);
                break;
        }
    }
    if (i%3)
    {
        outbuf[j++] = globus_l_gfs_base64_n[c];
    }
    switch (i%3)
    {
        case 1:
            outbuf[j++] = globus_l_gfs_base64_pad;
        case 2:
            outbuf[j++] = globus_l_gfs_base64_pad;
    }

    outbuf[j] = '\0';
    if(out_len)
    {
        *out_len = j;
    }

    return GLOBUS_SUCCESS;
}


char *    
globus_i_gfs_data_dsi_version()
{
    int                                 rc;
    globus_version_t                    version;
    char *                              str = NULL;
    GlobusGFSName(globus_i_gfs_data_init);
    GlobusGFSDebugEnter();

    
    rc = globus_extension_get_module_version(
        globus_l_gfs_active_dsi_handle, &version);
    if(rc == GLOBUS_SUCCESS)
    {
        str = globus_common_create_string(
            "%s-%d.%d", 
            globus_l_gfs_active_dsi_name, 
            version.major, 
            version.minor);
    }

    return str;

    GlobusGFSDebugExit();
}

void
globus_i_gfs_data_init()
{
    char *                              restrict_path;
    int                                 rc;
    globus_result_t                     result;
    char *                              driver;
    GlobusGFSName(globus_i_gfs_data_init);
    GlobusGFSDebugEnter();

    globus_extension_register_builtins(local_extensions);

    globus_l_gfs_active_dsi_name = 
        globus_i_gfs_config_string("load_dsi_module");

    result = globus_i_gfs_data_new_dsi(
        &globus_l_gfs_active_dsi_handle, 
        globus_l_gfs_active_dsi_name,
        &globus_l_gfs_dsi, 
        GLOBUS_FALSE);

    if(result != GLOBUS_SUCCESS)
    {
        globus_gfs_log_exit_message(
           "Couldn't load '%s'. %s\n", globus_l_gfs_active_dsi_name, 
                globus_error_print_friendly(globus_error_peek(result)));
        exit(1);
    }

    globus_mutex_init(&gfs_l_data_brain_mutex, NULL);

    globus_l_gfs_data_is_remote_node = globus_i_gfs_config_bool("data_node");

    {
        char *                          str_transferred;

        str_transferred = (char *) globus_calloc(1, 256);
        sprintf(str_transferred, "0 bytes");
        globus_mutex_init(&globus_l_gfs_global_counter_lock, NULL);
        globus_gfs_config_set_ptr("byte_transfer_count", str_transferred);
    }

    /* initialize hashtable */
    globus_hashtable_init(
        &gfs_l_data_net_allowed_drivers,
        64,
        globus_hashtable_string_hash,
        globus_hashtable_string_keyeq);
    globus_l_gfs_load_safe(
        "dc_whitelist", "gsi,tcp", &gfs_l_data_net_allowed_drivers);

    globus_hashtable_init(
        &gfs_l_data_disk_allowed_drivers,
        64,
        globus_hashtable_string_hash,
        globus_hashtable_string_keyeq);
    globus_l_gfs_load_safe(
        "fs_whitelist", "file", &gfs_l_data_disk_allowed_drivers);

    if((restrict_path = globus_gfs_config_get_string("sharing_rp")) != NULL)
    {
        result = globus_l_gfs_data_parse_restricted_paths(
            NULL, restrict_path, &globus_l_gfs_path_alias_list_sharing, 0);
            
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_exit_result("Error parsing sharing restricted paths", result);
            exit(1);
        }
    }
    else
    {
        if((restrict_path = globus_gfs_config_get_string("restrict_paths")) != NULL)
        {
            result = globus_l_gfs_data_parse_restricted_paths(
                NULL, restrict_path, &globus_l_gfs_path_alias_list_sharing, 0);
                
            if(result != GLOBUS_SUCCESS)
            {
                globus_gfs_log_exit_result("Error parsing restricted paths", result);
                exit(1);
            }
        }
    }

    if((restrict_path = globus_gfs_config_get_string("restrict_paths")) != NULL)
    {
        result = globus_l_gfs_data_parse_restricted_paths(
            NULL, restrict_path, &globus_l_gfs_path_alias_list_base, 0);
            
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_exit_result("Error parsing restricted paths", result);
            exit(1);
        }
    }
    
    if(globus_i_gfs_config_bool("inetd"))
    {
        globus_l_gfs_watchdog_limit = globus_i_gfs_config_int("control_idle_timeout");
        if(globus_l_gfs_watchdog_limit < 300)
        {
            globus_l_gfs_watchdog_limit = 300;
        }
    }
    
    if(globus_i_gfs_config_bool("allow_udt"))
    {
        result = globus_xio_driver_load("udt", &globus_l_gfs_udt_driver_preload);
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_result(
                GLOBUS_GFS_LOG_INFO, 
                "Unable to load UDT driver", result);
            globus_gfs_config_set_bool("allow_udt", GLOBUS_FALSE);
        }
    }
    
    if((globus_i_gfs_config_string("netmgr")) != NULL)
    {
        result = globus_xio_driver_load("net_manager", &globus_l_gfs_netmgr_driver);
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_result(
                GLOBUS_GFS_LOG_INFO, 
                "Unable to load Network Manager driver", result);
        }
    }    
    
    GlobusGFSDebugExit();
}


static
globus_result_t
globus_l_gfs_data_operation_init(
    globus_l_gfs_data_operation_t **    u_op,
    globus_l_gfs_data_session_t *       session_handle)
{
    globus_l_gfs_data_operation_t *     op;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_data_operation_init);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *)
        globus_calloc(1, sizeof(globus_l_gfs_data_operation_t));
    if(!op)
    {
        result = GlobusGFSErrorMemory("op");
        goto error_alloc;
    }

    op->session_handle = session_handle;
    op->ref = 1;
    globus_mutex_lock(&op->session_handle->mutex);
    {
        op->session_handle->ref++;
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    globus_range_list_init(&op->recvd_ranges);
    globus_range_list_init(&op->stripe_range_list);
    op->recvd_bytes = 0;
    op->max_offset = -1;
    op->order_data = session_handle->order_data;
    globus_mutex_init(&op->stat_lock, NULL);

    *u_op = op;

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error_alloc:
    GlobusGFSDebugExitWithError();
    return result;
}

static
void
globus_l_gfs_data_operation_destroy(
    globus_l_gfs_data_operation_t *     op)
{
    GlobusGFSName(globus_l_gfs_data_operation_destroy);
    GlobusGFSDebugEnter();

    globus_range_list_destroy(op->recvd_ranges);
    globus_range_list_destroy(op->stripe_range_list);
    if(op->pathname)
    {
        globus_free(op->pathname);
    }
    if(op->cksm_response)
    {
        globus_free(op->cksm_response);
    }
    if(op->user_msg)
    {
        globus_free(op->user_msg);
    }
    if(op->remote_ip)
    {
        globus_free(op->remote_ip);
    }
    if(op->list_type)
    {
        globus_free((char *) op->list_type);
    }
    if(op->eof_count != NULL)
    {
        globus_free(op->eof_count);
    }
    if(op->storattr)
    {
        globus_free(op->storattr->all);
        globus_free(op->storattr->modify);
        globus_free(op->storattr->checksum_md5);
        globus_free(op->storattr);
    }
    globus_mutex_destroy(&op->stat_lock);

    globus_free(op);

    GlobusGFSDebugExit();
}


void
globus_i_gfs_data_request_stat(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_stat_info_t *            stat_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg)
{
    globus_result_t                     res;
    int                                 rc;
    globus_l_gfs_data_operation_t *     op;
    globus_result_t                     result;
    globus_l_gfs_data_session_t *       session_handle;
    globus_gfs_acl_object_desc_t        object;
    GlobusGFSName(globus_i_gfs_data_request_stat);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *) session_arg;
    globus_l_gfs_data_reset_watchdog(session_handle, NULL);

    result = globus_l_gfs_data_operation_init(&op, session_handle);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_l_gfs_data_operation_init", result);
        goto error_op;
    }

    op->ipc_handle = ipc_handle;
    op->id = id;
    op->uid = getuid();

    op->state = GLOBUS_L_GFS_DATA_REQUESTING;
    op->callback = cb;
    op->user_arg = user_arg;
    op->session_handle = session_handle;
    op->info_struct = stat_info;
    op->type = GLOBUS_L_GFS_DATA_INFO_TYPE_STAT;

    object.name = stat_info->pathname;

    if(stat_info->internal)
    {
        res = GLOBUS_SUCCESS;
        rc = GLOBUS_GFS_ACL_COMPLETE;
    }
    else
    {
        if (globus_i_gfs_config_bool("data_node") &&
            globus_i_gfs_config_int("auth_level")&GLOBUS_L_GFS_AUTH_DATA_NODE_PATH)
        {
            char *                          chdir_to;
            char *                          full_pathname = NULL;

            chdir_to = globus_i_gfs_config_string("chdir_to");

            result = globus_i_gfs_get_full_path(
                session_handle->home_dir,
                chdir_to ? chdir_to : "/", // XXX
                session_handle,
                stat_info->pathname,
                &full_pathname,
                GFS_L_LIST);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusGFSErrorWrapFailed(
                    "globus_l_gfs_data_operation_init", result);
                goto error_op;
            }
            if (full_pathname)
            {
                free(stat_info->pathname);
                stat_info->pathname = full_pathname;

                object.name = stat_info->pathname;
            }
        }


        rc = globus_gfs_acl_authorize(
            &session_handle->acl_handle,
            GFS_ACL_ACTION_LOOKUP,
            &object,
            &res,
            globus_l_gfs_authorize_cb,
            op);
    }
    if(rc == GLOBUS_GFS_ACL_COMPLETE)
    {
        /* this should possibly be a one shot */
        globus_l_gfs_authorize_cb(
            &object, GFS_ACL_ACTION_LOOKUP, op, res);
    }

    GlobusGFSDebugExit();
    return;

error_op:
    globus_l_gfs_authorize_cb(
        &object, GFS_ACL_ACTION_LOOKUP, op, result);
    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_data_stat_kickout(
    void *                              user_arg)
{
    void *                              remote_data_arg = NULL;
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    globus_l_gfs_data_stat_bounce_t *   bounce_info;
    globus_gfs_finished_info_t          reply;
    int                                 i;
    int                                 code;
    GlobusGFSName(globus_l_gfs_data_stat_kickout);
    GlobusGFSDebugEnter();

    bounce_info = (globus_l_gfs_data_stat_bounce_t *) user_arg;

    memset(&reply, '\0', sizeof(globus_gfs_finished_info_t));

    globus_mutex_lock(&bounce_info->op->stat_lock);

    if(!bounce_info->final_stat)
    {
        reply.code = 100;
    }
        
    reply.type = GLOBUS_GFS_OP_STAT;
    reply.id = bounce_info->op->id;
    reply.result = bounce_info->error ?
        globus_error_put(bounce_info->error) : GLOBUS_SUCCESS;
    reply.info.stat.stat_array =  bounce_info->stat_array;
    reply.info.stat.stat_count =  bounce_info->stat_count;
    reply.info.stat.uid = bounce_info->op->session_handle->uid;
    reply.info.stat.gid_count = bounce_info->op->session_handle->gid_count;
    reply.info.stat.gid_array = bounce_info->op->session_handle->gid_array;

    /* pull response code from error */
    if(bounce_info->final_stat && reply.result != GLOBUS_SUCCESS && 
        (code = globus_gfs_error_get_ftp_response_code(
            globus_error_peek(reply.result))) != 0)
    {
        reply.code = code;
        reply.msg = globus_error_print_friendly(
            globus_error_peek(reply.result));
    }

    if(bounce_info->op->callback != NULL)
    {
        bounce_info->op->callback(
            &reply,
            bounce_info->op->user_arg);
    }
    else
    {
        globus_gfs_ipc_reply_finished(
            bounce_info->op->ipc_handle,
            &reply);
    }

    globus_mutex_unlock(&bounce_info->op->stat_lock);

    if(bounce_info->final_stat)
    {
        globus_mutex_lock(&bounce_info->op->session_handle->mutex);
        {
            GFSDataOpDec(bounce_info->op, destroy_op, destroy_session);
            remote_data_arg = globus_l_gfs_data_check(
                bounce_info->op->session_handle, bounce_info->op->data_handle);
        }
        globus_mutex_unlock(&bounce_info->op->session_handle->mutex);
    
        globus_assert(destroy_op);
        globus_l_gfs_data_fire_cb(
            bounce_info->op, remote_data_arg, destroy_session);
        globus_l_gfs_data_operation_destroy(bounce_info->op);
    }
    
    if(bounce_info->stat_array)
    {
        for(i = 0; i < bounce_info->stat_count; i++)
        {
            if(bounce_info->stat_array[i].name != NULL)
            {
                globus_free(bounce_info->stat_array[i].name);
            }
            if(bounce_info->stat_array[i].symlink_target != NULL)
            {
                globus_free(bounce_info->stat_array[i].symlink_target);
            }
        }
        globus_free(bounce_info->stat_array);
    }
    globus_free(bounce_info);

    GlobusGFSDebugExit();
}

static
globus_result_t
globus_l_gfs_data_approve_popen(
    char *                              in_cmd,
    char **                             out_cmd)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_list_t *                     list;
    char                                delim;
    char                                end_delim;
    char *                              start;
    char *                              cmd = NULL;
    char *                              end;
    char *                              ptr;
    char *                              alias;
    char *                              prog;
    char *                              tmp;
    char *                              out = NULL;
    globus_bool_t                       found;
    int                                 size;
    char                                endchars[] = "#,;";

    GlobusGFSName(globus_l_gfs_data_approve_popen);
    GlobusGFSDebugEnter();

    if(strstr(in_cmd, "popen:") == NULL)
    {
        out = globus_libc_strdup(in_cmd);
    }
    else
    {
        cmd = globus_libc_strdup(in_cmd);
        if(cmd == NULL)
        {
            result = GlobusGFSErrorGeneric("small malloc failure.");
            goto error;
        }
        
        end = cmd;
        while(end && (ptr = strstr(end, "popen:")))
        {
            start = end;
            ptr = strstr(ptr, "argv=");
            if(ptr == NULL)
            {
                result = GlobusGFSErrorGeneric("popen argv not supplied.");
                goto error;
            }
                
            ptr += 5;
    
            delim = *ptr;
            *ptr = '\0';        
            ptr++;
        
            endchars[0] = delim;
            end = strpbrk(ptr, endchars);
              
            if(end != NULL)
            {
                end_delim = *end;
                *end = '\0';
                end++;
            }
    
            for(found = GLOBUS_FALSE,
                list = (globus_list_t *) globus_i_gfs_config_get("popen_list");
                !globus_list_empty(list) && !found;
                list = globus_list_rest(list))
            {
                /* parse out prog name from <prog> or <alias>:<prog> */
                alias = (char *) globus_list_first(list);
                prog = strchr(alias, ':');
                if(prog != NULL)
                {
                    size = prog - alias;
                    prog++;
                }
                else
                {
                    size = strlen(alias);
                    prog = alias;
                    
                }
                if((strncmp(alias, ptr, size) == 0 && strlen(ptr) == size) ||
                    strcmp(prog, ptr) == 0)
                {
                    found = GLOBUS_TRUE;
                }
            } 
            if(found)
            {
                if(access(prog, R_OK | X_OK) < 0)
                {
                    tmp = globus_common_create_string(
                        "access check of popen program '%s'", ptr);
                    result = GlobusGFSErrorSystemError(tmp, errno);
                    globus_free(tmp);
                    goto error;
                }

                if(end)
                {
                    tmp = globus_common_create_string("%s%s%c%s%c",
                        out ? out : "", start, delim, prog, end_delim);
                    if(out)
                    {
                        globus_free(out);
                    }
                    out = tmp;
                }
                else
                {
                    tmp = globus_common_create_string("%s%s%c%s", 
                        out ? out : "", start, delim, prog);
                    if(out)
                    {
                        globus_free(out);
                    }
                    out = tmp;
                }
            }
            else
            {
                GlobusGFSErrorGenericStr(result,
                    ("program '%s' not whitelisted.", ptr));
                goto error;
            }
        }
        if(end)
        {
            tmp = globus_common_create_string("%s%s",
                out ? out : "", end);
            if(out)
            {
                globus_free(out);
            }
            out = tmp;
        }

        globus_free(cmd);
    }
    
    *out_cmd = out;
    
    GlobusGFSDebugExit();
    return result;

error:
    if(cmd)
    {
        globus_free(cmd);
    }
    if(out)
    {
        globus_free(out);
    }
    GlobusGFSDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_gfs_data_load_stack(
    char *                              driver_string_in,
    globus_list_t **                    driver_list_out,
    globus_hashtable_t *                allowed_drivers,
    char *                              default_stack,
    globus_bool_t                       subst_io_drivers)
{
    char *                              parsed_driver_string;
    char *                              driver_string;
    globus_result_t                     result = GLOBUS_SUCCESS;
    GlobusGFSName(globus_l_gfs_data_load_stack);
    GlobusGFSDebugEnter();
    
    if(*driver_list_out)
    {
        globus_xio_driver_list_destroy(*driver_list_out, GLOBUS_FALSE);
        *driver_list_out = NULL;
    }

    if(strcasecmp(driver_string_in, "default") == 0)
    {        
        driver_string = default_stack;
    }
    else
    {
        driver_string = driver_string_in;
    }
    
    if(driver_string)
    {
        result = globus_l_gfs_data_approve_popen(
            driver_string, &parsed_driver_string);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "Approving popen arguments", result);
        }
        else
        {
            result = globus_xio_driver_list_from_string(
                parsed_driver_string,
                driver_list_out,
                allowed_drivers);
            
            globus_free(parsed_driver_string);
            
            if(subst_io_drivers)
            {
                globus_xio_driver_list_ent_t *      ent;

                ent = globus_xio_driver_list_find_driver(*driver_list_out, "tcp");
                if(ent)
                {
                    if(ent->loaded)
                    {
                        globus_xio_driver_unload(ent->driver);
                        ent->loaded = GLOBUS_FALSE;
                    }
                    ent->driver = globus_io_compat_get_tcp_driver();
                }

                ent = globus_xio_driver_list_find_driver(*driver_list_out, "gsi");
                if(ent)
                {
                    if(ent->loaded)
                    {
                        globus_xio_driver_unload(ent->driver);
                        ent->loaded = GLOBUS_FALSE;
                    }
                    ent->driver = globus_io_compat_get_gsi_driver();
                }
            }
        }
    }
    
    GlobusGFSDebugExit();
    return result;
}

void
globus_i_gfs_data_request_command(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_command_info_t *         cmd_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg)
{
    globus_result_t                     res;
    int                                 rc;
    globus_gfs_acl_action_t             action;
    globus_bool_t                       call = GLOBUS_TRUE;
    globus_l_gfs_data_operation_t *     op;
    globus_result_t                     result;
    globus_extension_handle_t           new_dsi_handle;
    globus_gfs_storage_iface_t *        new_dsi;
    globus_l_gfs_data_session_t *       session_handle;
    char *                              dsi_name;
    globus_gfs_acl_object_desc_t        object;
    char *                              tmp;
    char *                              starttag;
    GlobusGFSName(globus_i_gfs_data_request_command);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *) session_arg;
    globus_l_gfs_data_reset_watchdog(session_handle, NULL);

    result = globus_l_gfs_data_operation_init(&op, session_handle);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_l_gfs_data_operation_init", result);
        goto error_op;
    }
    op->ipc_handle = ipc_handle;
    op->id = id;
    op->state = GLOBUS_L_GFS_DATA_REQUESTING;
    op->command = cmd_info->command;
    op->pathname = globus_libc_strdup(cmd_info->pathname);
    op->callback = cb;
    op->user_arg = user_arg;
    op->info_struct = cmd_info;
    op->type = GLOBUS_L_GFS_DATA_INFO_TYPE_COMMAND;
    dsi_name = cmd_info->pathname;
    object.name = op->pathname;

    switch(cmd_info->command)
    {
        case GLOBUS_GFS_CMD_SITE_DSI:
            if(session_handle->dsi->descriptor & GLOBUS_GFS_DSI_DESCRIPTOR_SENDER)
            {
                result = globus_i_gfs_data_new_dsi(
                    &new_dsi_handle, dsi_name, &new_dsi, GLOBUS_TRUE);

                /* if we couldn't load it, error */
                if(new_dsi == NULL)
                {
                    result = GlobusGFSErrorGeneric("no such DSI");
                }
                /* if it is the wrong type release and error */
                else if(
                    !(new_dsi->descriptor & GLOBUS_GFS_DSI_DESCRIPTOR_SENDER))
                {
                    result = GlobusGFSErrorGeneric("DSI isn't a sender.");
                }
                /* if all is well */
                else
                {
                    /* if not the global default release the reference */
                    if(session_handle->dsi != globus_l_gfs_dsi)
                    {
                        globus_extension_release(session_handle->dsi_handle);
                    }
                    /* set to new dsi */
                    session_handle->dsi_handle = new_dsi_handle;
                    op->session_handle->dsi = new_dsi;
                    result = GLOBUS_SUCCESS;
                }
                call = GLOBUS_FALSE;

                globus_gridftp_server_finished_command(op, result, NULL);
            }
            break;

        case GLOBUS_GFS_CMD_DCSC:
            if(strcasecmp(cmd_info->cksm_alg, "D") == 0)
            {
                if(op->session_handle->dcsc_cred != GSS_C_NO_CREDENTIAL)
                {
                    OM_uint32           min_rc;
                    gss_release_cred(
                        &min_rc, &op->session_handle->dcsc_cred);
                }
            }
            else if(strcasecmp(cmd_info->cksm_alg, "P") == 0)
            {
                globus_size_t           dcsc_len;
                OM_uint32               major_status; 
                OM_uint32               minor_status;
                gss_buffer_desc         buf; 
                gss_cred_id_t           cred;
                
                dcsc_len = strlen(cmd_info->pathname);
                buf.value = calloc(1, dcsc_len);
                
                rc = globus_l_gfs_base64_decode(
                    (globus_byte_t *) cmd_info->pathname, buf.value, &dcsc_len);
                if(rc != GLOBUS_SUCCESS)
                {
                    globus_free(buf.value);
                    result = GlobusGFSErrorGeneric(
                        "Invalid base64 input for credential type P.");
                }
                else
                {                            
                    buf.length = strlen(buf.value);
                    major_status = gss_import_cred(
                        &minor_status,
                        &cred,
                        GSS_C_NO_OID,
                        0,
                        &buf,
                        0,
                        NULL);
                    globus_free(buf.value);
                    if(major_status != GSS_S_COMPLETE)
                    {
                        result = GlobusGFSErrorWrapFailed(
                            "Credential import", minor_status);
                    }
                    else
                    {
                        if(op->session_handle->dcsc_cred != GSS_C_NO_CREDENTIAL)
                        {
                            OM_uint32   min_rc;
                            gss_release_cred(
                                &min_rc, &op->session_handle->dcsc_cred);
                        }
                        
                        op->session_handle->dcsc_cred = cred;
                    }
                }
            }
            else
            {
                result = GlobusGFSErrorGeneric("Unsupported credential type.");
            }
            call = GLOBUS_FALSE;
            globus_gridftp_server_finished_command(op, result, NULL);
            break;

        case GLOBUS_GFS_CMD_HTTP_CONFIG:
          {
            globus_byte_t *                     tmp_val;
            globus_byte_t *                     ca_pem = NULL;
            globus_size_t                       ca_pem_len = 0;
            
            op->session_handle->http_config_called = GLOBUS_TRUE;
            if((tmp_val = (globus_byte_t *)
                        globus_i_gfs_kv_getval(
                            cmd_info->pathname, "CA_CERTS", 0)) != NULL)
            {
                ca_pem = malloc(strlen((char *) tmp_val));
                rc = globus_l_gfs_base64_decode(
                    tmp_val, ca_pem, &ca_pem_len);
                globus_free(tmp_val);
                if(rc != GLOBUS_SUCCESS)
                {
                    globus_free(ca_pem);
                    result = GlobusGFSErrorGeneric(
                        "Invalid base64 input for CA_CERTS.");
                }
                else
                {               
                    result = GLOBUS_SUCCESS;     
                    if(op->session_handle->http_ca_certs)
                    {
                        globus_free(op->session_handle->http_ca_certs);
                    }
                    op->session_handle->http_ca_certs = (char *) ca_pem;
                }
            }
            
            call = GLOBUS_FALSE;
            globus_gridftp_server_finished_command(op, result, NULL);
            break;

          }
          
        case GLOBUS_GFS_CMD_HTTP_PUT:
          { 
            char *                  request;
            char *                  path;
            globus_off_t            offset;
            globus_off_t            length;                
            
            result = globus_i_gfs_data_http_parse_args(
                cmd_info->pathname, &path, &request, &offset, &length);
            if(result == GLOBUS_SUCCESS)
            {
                result = globus_i_gfs_data_http_put(
                    op, path, request, offset, length, GLOBUS_TRUE);
            }
            if(result != GLOBUS_SUCCESS)
            {
                globus_gridftp_server_finished_command(op, result, NULL);
            }
            call = GLOBUS_FALSE;
          }
            break;

        case GLOBUS_GFS_CMD_HTTP_GET:
          { 
            char *                  request;
            char *                  path;
            globus_off_t            offset;
            globus_off_t            length;                
            
            result = globus_i_gfs_data_http_parse_args(
                cmd_info->pathname, &path, &request, &offset, &length);
            if(result == GLOBUS_SUCCESS)
            {
                result = globus_i_gfs_data_http_get(
                    op, path, request, offset, length, GLOBUS_TRUE);
            }
            if(result != GLOBUS_SUCCESS)
            {
                globus_gridftp_server_finished_command(op, result, NULL);
            }
            call = GLOBUS_FALSE;
          }
            break;

        case GLOBUS_GFS_CMD_SITE_SETNETSTACK:
            if(session_handle->dsi->descriptor &
                GLOBUS_GFS_DSI_DESCRIPTOR_SENDER)
            {
                result = globus_l_gfs_data_load_stack(
                    cmd_info->pathname,
                    &op->session_handle->net_stack_list,
                    &gfs_l_data_net_allowed_drivers,
                    globus_i_gfs_config_string("dc_default"),
                    GLOBUS_TRUE);
                if(result != GLOBUS_SUCCESS)
                {
                    result = GlobusGFSErrorWrapFailed(
                        "Setting data channel driver stack", result);
                }
                globus_gridftp_server_finished_command(op, result, NULL);
            }
            else
            {
                op->session_handle->dsi->command_func(
                    op, cmd_info, op->session_handle->session_arg);
            }

            call = GLOBUS_FALSE;
            break;

        case GLOBUS_GFS_CMD_SITE_SETDISKSTACK:
            if(session_handle->dsi->descriptor & 
                GLOBUS_GFS_DSI_DESCRIPTOR_SENDER)
            {
                 result = globus_l_gfs_data_load_stack(
                    cmd_info->pathname,
                    &op->session_handle->disk_stack_list,
                    &gfs_l_data_disk_allowed_drivers,
                    globus_i_gfs_config_string("fs_default"),
                    GLOBUS_FALSE);
                if(result != GLOBUS_SUCCESS)
                {
                    result = GlobusGFSErrorWrapFailed(
                        "Setting filesystem driver stack", result);
                }
                globus_gridftp_server_finished_command(op, result, NULL);
            }
            else
            {
                op->session_handle->dsi->command_func(
                    op, cmd_info, op->session_handle->session_arg);
            }

            call = GLOBUS_FALSE;
            break;

        case GLOBUS_GFS_CMD_SITE_SHARING:
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_INFO,
                "Processing SITE SHARING %s\n", cmd_info->pathname);
            if(!op->session_handle->sharing)
            {
                result = GlobusGFSErrorGeneric("Sharing not enabled.");
            }
            else if(op->session_handle->sharing_sharee)
            {
                result = GlobusGFSErrorGeneric(
                    "Sharing control is not allowed.");
            }
            
            else if(!strncasecmp(cmd_info->pathname, "CREATE", 6))
            {
                char *                  share_args;
                char *                  share_path = NULL;
                char *                  share_id = NULL;
                char *                  share_file = NULL;
                
                share_args = strchr(cmd_info->pathname, ' ');
                if(!share_args)
                {
                    result = GlobusGFSErrorGeneric("Missing share parameters.");
                    goto share_create_error;
                }
                share_args++;
                if((share_id = 
                    globus_i_gfs_kv_getval(share_args, "ID", 1)) == NULL)
                {
                    result = GlobusGFSErrorGeneric(
                        "Invalid arguments for CREATE. Missing ID.");
                    goto share_create_error;
                }
                tmp = share_id;
                while(*tmp)
                {
                    *tmp = tolower(*tmp);
                    if(!isxdigit(*tmp) && *tmp != '-')
                    {
                        result = GlobusGFSErrorGeneric(
                            "Invalid character in share ID.");
                        goto share_create_error;
                    }
                    tmp++;
                }
                if((share_path = 
                    globus_i_gfs_kv_getval(share_args, "PATH", 1)) == NULL)
                {
                    result = GlobusGFSErrorGeneric(
                        "Invalid arguments for CREATE. Missing PATH.");
                    goto share_create_error;
                }

                /* check allow/deny config */
                if(!globus_l_gfs_data_check_sharing_allowed(op->session_handle))
                {
                    result = GlobusGFSErrorGeneric(
                        "Sharing is not allowed for the current user.");
                    goto share_create_error;
                }
                
                share_file = globus_common_create_string(
                    "%s/share-%s",
                    op->session_handle->sharing_state_dir,
                    share_id);
                
                /* check if path will be accessible */
                if(result == GLOBUS_SUCCESS)
                {
                    globus_list_t **    save_list;
                    save_list = op->session_handle->active_rp_list;
                    op->session_handle->active_rp_list = &globus_l_gfs_path_alias_list_sharing;

                    result = globus_i_gfs_data_check_path(op->session_handle,
                        share_path, NULL, GFS_L_LIST, 0);

                    op->session_handle->active_rp_list = save_list;
                    if(result != GLOBUS_SUCCESS)
                    {
                        result = GlobusGFSErrorGeneric(
                            "Requested path can not be accessed via sharing.");
                        goto share_create_error;
                    }
                }

                /* creation disabled, success only if share file exists */
                if(!globus_i_gfs_config_bool("sharing_control"))
                {
                    if(access(share_file, F_OK) < 0)
                    {
                        result = GlobusGFSErrorGeneric(
                            "Sharing control is not enabled.");
                    }
                    else
                    {
                        result = GLOBUS_SUCCESS;
                    }
                }
                /* create if doesn't exist */
                else
                {
                    result = GLOBUS_SUCCESS;
                    if(access(share_file, F_OK) < 0)
                    {
                        int                 sharingfd;
                        
                        /* create state dir if default and doesn't exist */
                        if(strstr(op->session_handle->sharing_state_dir, "/.globus/sharing") && 
                            access(op->session_handle->sharing_state_dir, F_OK) < 0)
                        {
                            char *      tmp_dir;
                            char *      ptr;
                            
                            tmp_dir = strdup(op->session_handle->sharing_state_dir);
                            ptr = strrchr(tmp_dir, '/');
                            if(ptr)
                            {
                                *ptr = '\0';
                                mkdir(tmp_dir, 0700);
                                *ptr = '/';
                            }
                            mkdir(tmp_dir, 0700);
                            free(tmp_dir);
                        }
                        
                        if(globus_l_gfs_data_check_sharing_perms(op->session_handle) != 0)
                        {
                            result = GlobusGFSErrorGeneric(
                                "Sharing state dir has invalid permissions.");
                            goto share_create_error;
                        }                            

                        sharingfd = 
                            open(share_file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
                        if(sharingfd < 0)
                        {
                            result = GlobusGFSErrorSystemError(
                                "Enabling sharing (open)", errno);
                        }
                        else
                        {
                            char *      tmp_content;
                            char *      tmp_enc;
                            int         save_rc = 0;
#ifdef WIN32                            
                            if(isalpha(share_path[0]) && share_path[1] == ':')
                            {
                                share_path[1] = share_path[0];
                                share_path[0] = '/';
                                if(share_path[2] == '/' && share_path[3] == 0)
                                {
                                    share_path[2] = 0;
                                }
                            }
#endif
                            tmp_enc = globus_url_string_hex_encode(share_path, "\"");
                            
                            tmp_content = globus_common_create_string( 
                                "#\n# This file is required in order to enable GridFTP file sharing.\n"
                                "# If you remove this file, file sharing will no longer work.\n#\n\n"
                                "share_path \"%s\"\n", tmp_enc);
                            rc = write(sharingfd, tmp_content, strlen(tmp_content));
                            if(rc < 0)
                            {
                                result = GlobusGFSErrorSystemError(
                                    "Enabling sharing (write)", errno);
                                save_rc = rc;
                            }

                            rc = close(sharingfd);
                            if(rc < 0 && !save_rc)
                            {
                                result = GlobusGFSErrorSystemError(
                                    "Enabling sharing (close)", errno);
                                save_rc = rc;
                            }
                            
                            rc = chmod(share_file, S_IRUSR);
                            if(rc < 0 && !save_rc)
                            {
                                result = GlobusGFSErrorSystemError(
                                    "Enabling sharing (chmod)", errno);
                            }
                            
                            globus_free(tmp_enc);
                            globus_free(tmp_content);
                        }
                    }
                    else
                    {
                        result = GlobusGFSErrorGeneric(
                            "Share ID already exists.");
                        goto share_create_error;
                    }
                }
share_create_error:
                if(share_id)
                {
                    globus_free(share_id);
                }
                if(share_path)
                {
                    globus_free(share_path);
                }
                if(share_file)
                {
                    globus_free(share_file);
                }
            }
            else if(!strncasecmp(cmd_info->pathname, "DELETE", 6))
            {
                char *                  share_file = NULL;
                char *                  share_id = NULL;
                
                share_id = strchr(cmd_info->pathname, ' ');
                if(!share_id)
                {
                    result = GlobusGFSErrorGeneric("Missing share ID.");
                    goto share_delete_error;
                }
                share_id++;
                tmp = share_id;
                while(*tmp)
                {
                    *tmp = tolower(*tmp);
                    if(!isxdigit(*tmp) && *tmp != '-')
                    {
                        result = GlobusGFSErrorGeneric(
                            "Invalid character in share ID.");
                        goto share_delete_error;
                    }
                    tmp++;
                }
                
                share_file = globus_common_create_string(
                    "%s/share-%s",
                    op->session_handle->sharing_state_dir,
                    share_id);

                /* control disabled, success only if file doesn't exist */
                if(!globus_i_gfs_config_bool("sharing_control"))
                {
                    if(access(share_file, F_OK) < 0)
                    {
                        result = GLOBUS_SUCCESS;
                    }
                    else
                    {
                        result = GlobusGFSErrorGeneric(
                            "Sharing control is not enabled.");
                    }
                }
                /* delete the file */
                else
                {
                    result = GLOBUS_SUCCESS;
                    if(access(share_file, F_OK) == 0)
                    {
                        rc = unlink(share_file);
                        if(rc < 0)
                        {
                            result = GlobusGFSErrorSystemError(
                                "Disabling sharing", errno);
                        }
                    }
                }
share_delete_error:
                if(share_file)
                {
                    globus_free(share_file);
                }
            }
            else if(!strncasecmp(cmd_info->pathname, "TESTPATH", 8))
            {
                char *                  share_path;
                char *                  tmp_share_path = NULL;

                share_path = strchr(cmd_info->pathname, ' ');
                if(!share_path)
                {
                    result = GlobusGFSErrorGeneric("Missing path argument.");
                    goto share_test_error;
                }
                
                share_path++;
                
                /** check if sharing can be enabled **/
                /* creation disabled */
                if(!globus_i_gfs_config_bool("sharing_control"))
                {
                    result = GlobusGFSErrorGeneric(
                        "Sharing control is not enabled.");
                }
                
                /* check allow/deny config */
                else if(!globus_l_gfs_data_check_sharing_allowed(op->session_handle))
                {
                    result = GlobusGFSErrorGeneric(
                        "Sharing is not allowed for the current user.");
                }

                /* check if we can write to share state dir */
                else
                {
                    /* create state dir if default and doesn't exist */
                    if(strstr(op->session_handle->sharing_state_dir, "/.globus/sharing") && 
                        access(op->session_handle->sharing_state_dir, F_OK) < 0)
                    {
                        char *      tmp_dir;
                        char *      ptr;
                        
                        tmp_dir = strdup(op->session_handle->sharing_state_dir);
                        ptr = strrchr(tmp_dir, '/');
                        if(ptr)
                        {
                            *ptr = '\0';
                            mkdir(tmp_dir, 0700);
                            *ptr = '/';
                        }
                        mkdir(tmp_dir, 0700);
                        free(tmp_dir);
                    }
                   
                    if(globus_l_gfs_data_check_sharing_perms(op->session_handle) != 0)
                    {
                        result = GlobusGFSErrorGeneric(
                            "Sharing state dir has invalid permissions.");
                        goto share_test_error;
                    }                            
                    
                    if(access(op->session_handle->sharing_state_dir, W_OK) == 0)
                    {
                        result = GLOBUS_SUCCESS;
                    }
                    else
                    {
                        result = GlobusGFSErrorGeneric(
                            "Attempting to enable sharing will fail.");
                    }
                }

                /* sharing can be enabled. now check if path will be accessible */
                if(result == GLOBUS_SUCCESS)
                {
                    globus_list_t **    save_list;
                    save_list = op->session_handle->active_rp_list;
                    op->session_handle->active_rp_list = &globus_l_gfs_path_alias_list_sharing;

                    result = globus_i_gfs_data_check_path(op->session_handle,
                        share_path, NULL, GFS_L_LIST, 0);

                    op->session_handle->active_rp_list = save_list;
                    if(result != GLOBUS_SUCCESS)
                    {
                        result = GlobusGFSErrorGeneric(
                            "Requested path can not be accessed via sharing.");
                    }
                }
share_test_error:
                if(tmp_share_path)
                {
                    globus_free(tmp_share_path);
                }
            }
            else
            {
                result = GlobusGFSErrorGeneric("Command not supported.");
            }
            globus_gridftp_server_finished_command(op, result, NULL);
            call = GLOBUS_FALSE;
            
            break;
                            
        case GLOBUS_GFS_CMD_SITE_RESTRICT:
            if(!globus_list_empty(session_handle->rp_list))
            {
                result = GlobusGFSErrorGeneric(
                    "Session restricted paths list already set.");
            }
            else
            {
                result = globus_l_gfs_data_parse_restricted_paths(
                    session_handle, cmd_info->pathname, &session_handle->rp_list, 1);
            }
            
            globus_l_gfs_data_update_restricted_paths_symlinks(
                session_handle, &session_handle->rp_list);
                

            globus_gridftp_server_finished_command(op, result, NULL);

            call = GLOBUS_FALSE;
            break;

        case GLOBUS_GFS_CMD_SITE_CHROOT:
            if(session_handle->chroot_path)
            {
                result = GlobusGFSErrorGeneric(
                    "Root directory is already set.");
            }
            else if(strcmp(cmd_info->pathname, "/") == 0)
            {
                result = GLOBUS_SUCCESS;
            }
            else
            {
                session_handle->chroot_path = 
                    globus_libc_strdup(cmd_info->pathname);
                    result = GLOBUS_SUCCESS;
            }
            
            globus_gridftp_server_finished_command(op, result, NULL);

            call = GLOBUS_FALSE;
            break;


        case GLOBUS_GFS_CMD_UPAS:
            if(*cmd_info->pathname != '0' && *cmd_info->pathname != '1')
            {
                result = GlobusGFSErrorGeneric(
                    "Controller parameter must be 0 or 1.");
                    
                globus_gridftp_server_finished_command(op, result, NULL);

            }
            else
            {
                char *                          candidates = NULL;
                char *                          stunserver;
                globus_xio_driver_list_ent_t *  ent;
                globus_xio_attr_t               xio_attr;
                                
                if(session_handle->net_stack_list == NULL)
                {
                    result = globus_l_gfs_data_load_stack(
                        op->session_handle->subject ? "udt,gsi" : "udt",
                        &op->session_handle->net_stack_list,
                        globus_i_gfs_config_bool("allow_udt") ? 
                            NULL : &gfs_l_data_net_allowed_drivers,
                        NULL,
                        GLOBUS_TRUE);
                    if(result != GLOBUS_SUCCESS)
                    {
                        result = GlobusGFSErrorWrapFailed(
                            "Setting data channel driver stack", result);
                    }
                }
                
                ent = globus_xio_driver_list_find_driver(
                    session_handle->net_stack_list, "udt");
                
                if(ent)
                {
                    if(session_handle->udt_data_channel_inuse)
                    {
                        globus_ftp_control_handle_destroy(
                            &session_handle->udt_data_channel);
                        session_handle->udt_data_channel_inuse = GLOBUS_FALSE;
                    }
                    
                    result = globus_ftp_control_handle_init(
                        &session_handle->udt_data_channel);
                    if(result != GLOBUS_SUCCESS)
                    {
                        result = GlobusGFSErrorWrapFailed(
                            "globus_ftp_control_handle_init", result);
                        goto error_upas;
                    }
                    session_handle->udt_data_channel_inuse = GLOBUS_TRUE;
                    
                    result = globus_i_ftp_control_data_get_attr(
                        &session_handle->udt_data_channel,
                        &xio_attr);
                    if(result != GLOBUS_SUCCESS)
                    {
                        goto error_upas;
                    }

                    stunserver = strchr(cmd_info->pathname, ' ');
                    if(stunserver)
                    {
                        stunserver++;
                    }
                    result = globus_xio_attr_cntl(
                        xio_attr,
                        ent->driver,
                        17 /* GLOBUS_XIO_UDT_GET_LOCAL_CANDIDATES*/,
                        *cmd_info->pathname == '1' ? 1 : 0,
                        stunserver,
                        &candidates);
                }
                else
                {
                    result = GlobusGFSErrorWrapFailed(
                        "Setting data channel driver stack", result);
                }
error_upas:
                globus_gridftp_server_finished_command(op, result, candidates);
            }
            
            call = GLOBUS_FALSE;
            break;

        case GLOBUS_GFS_CMD_UPRT:
            {
                globus_xio_driver_list_ent_t *  ent;
                globus_xio_attr_t               xio_attr;
                
                ent = globus_xio_driver_list_find_driver(
                    session_handle->net_stack_list, "udt");
                if(ent)
                {
                    result = globus_i_ftp_control_data_get_attr(
                        &session_handle->udt_data_channel,
                        &xio_attr);
                    if(result != GLOBUS_SUCCESS)
                    {
                        goto error_uprt;
                    }
    
                    result = globus_xio_attr_cntl(
                        xio_attr,
                        ent->driver,
                        18 /* GLOBUS_XIO_UDT_SET_REMOTE_CANDIDATES */,
                        cmd_info->pathname);
                    
                    session_handle->upas = GLOBUS_TRUE;
                }
                else
                {
                    result = GlobusGFSErrorWrapFailed(
                        "Setting data channel driver stack", result);
                }                    
error_uprt:
                globus_gridftp_server_finished_command(op, result, NULL);

            }
            
            call = GLOBUS_FALSE;
            break;

        case GLOBUS_GFS_CMD_SITE_CLIENTINFO:
            tmp = globus_malloc(strlen(cmd_info->pathname) + 1);

            if((starttag = strstr(cmd_info->pathname, "appname=")) != NULL)
            {
                starttag += strlen("appname=");;
                if(strchr(starttag, ';'))
                {
                    if(*starttag == '"')
                    {
                        rc = sscanf(starttag, "\"%[^\"]\";", tmp);
                    }
                    else
                    {
                        rc = sscanf(starttag, "%[^;];", tmp);
                    }
                    if(rc == 1)
                    {
                        if(session_handle->client_appname)
                        {
                            globus_free(session_handle->client_appname);
                        }
                        session_handle->client_appname =
                            globus_libc_strdup(tmp);
                    }
                }
            }

            if((starttag = strstr(cmd_info->pathname, "appver=")) != NULL)
            {
                starttag += strlen("appver=");
                if(strchr(starttag, ';'))
                {
                    if(*starttag == '"')
                    {
                        rc = sscanf(starttag, "\"%[^\"]\";", tmp);
                    }
                    else
                    {
                        rc = sscanf(starttag, "%[^;];", tmp);
                    }
                    if(rc == 1)
                    {
                        if(session_handle->client_appver)
                        {
                            globus_free(session_handle->client_appver);
                        }
                        session_handle->client_appver =
                            globus_libc_strdup(tmp);
                    }
                }
            }

            if((starttag = strstr(cmd_info->pathname, "scheme=")) ||
                (starttag = strstr(cmd_info->pathname, "schema=")))
            {
                starttag += strlen("scheme=");
                if(strchr(starttag, ';'))
                {
                    if(*starttag == '"')
                    {
                        rc = sscanf(starttag, "\"%[^\"]\";", tmp);
                    }
                    else
                    {
                        rc = sscanf(starttag, "%[^;];", tmp);
                    }
                    if(rc == 1)
                    {
                        if(session_handle->client_scheme)
                        {
                            globus_free(session_handle->client_scheme);
                        }
                        session_handle->client_scheme =
                            globus_libc_strdup(tmp);
                    }
                }
            }
            globus_free(tmp);

            call = GLOBUS_FALSE;
            globus_gridftp_server_finished_command(op, result, NULL);
            break;
            
        case GLOBUS_GFS_CMD_SITE_TASKID:
            if(op->session_handle->taskid)
            {
                globus_free(op->session_handle->taskid);
            }
            op->session_handle->taskid = globus_libc_strdup(cmd_info->pathname);

            call = GLOBUS_FALSE;
            globus_gridftp_server_finished_command(op, result, NULL);
            break;

        case GLOBUS_GFS_CMD_STORATTR:
            if(op->session_handle->storattr_str)
            {
                globus_free(op->session_handle->storattr_str);
            }
            op->session_handle->storattr_str = 
                globus_libc_strdup(cmd_info->pathname);

            call = GLOBUS_FALSE;
            globus_gridftp_server_finished_command(op, result, NULL);
            break;

        case GLOBUS_GFS_CMD_WHOAMI:
            call = GLOBUS_FALSE;
            globus_gridftp_server_finished_command(
                op, result, op->session_handle->username);
            break;

        case GLOBUS_GFS_CMD_TRNC:
            action = GFS_ACL_ACTION_WRITE;
            break;

        case GLOBUS_GFS_CMD_DELE:
        case GLOBUS_GFS_CMD_SITE_RDEL:
            action = GFS_ACL_ACTION_DELETE;
            break;

        case GLOBUS_GFS_CMD_RNTO:
            action = GFS_ACL_ACTION_WRITE;
            break;

        case GLOBUS_GFS_CMD_RMD:
            action = GFS_ACL_ACTION_DELETE;
            break;

        case GLOBUS_GFS_CMD_RNFR:
            action = GFS_ACL_ACTION_DELETE;
            break;

        case GLOBUS_GFS_CMD_CKSM:
            op->update_interval = cmd_info->chmod_mode;
            action = GFS_ACL_ACTION_READ;
            break;

        case GLOBUS_GFS_CMD_MKD:
            action = GFS_ACL_ACTION_CREATE;
            break;

        case GLOBUS_GFS_CMD_SITE_CHMOD:
            action = GFS_ACL_ACTION_WRITE;
            break;

        case GLOBUS_GFS_CMD_SITE_CHGRP:
            action = GFS_ACL_ACTION_WRITE;
            break;
        
        case GLOBUS_GFS_CMD_SITE_UTIME:
            action = GFS_ACL_ACTION_WRITE;
            break;
        
        case GLOBUS_GFS_CMD_SITE_SYMLINK:
            action = GFS_ACL_ACTION_CREATE;
            break;
            
        case GLOBUS_GFS_CMD_SITE_AUTHZ_ASSERT:
            /*
             * A new action to provide authorization assertions received
             * over the control channel to the authorization callout
             */
            object.name = cmd_info->authz_assert;
            action = GFS_ACL_ACTION_AUTHZ_ASSERT;
            rc = globus_gfs_acl_authorize(
                &session_handle->acl_handle,
                action,
                &object,
                &res,
                globus_l_gfs_authorize_cb,
                op);
            if(rc == GLOBUS_GFS_ACL_COMPLETE)
            {
                globus_l_gfs_authorize_cb(
                    &object, action, op, res);
            }
            call = GLOBUS_FALSE;
            break;

        default:
            if(cmd_info->command >= GLOBUS_GFS_MIN_CUSTOM_CMD)
            {
                if(cmd_info->op_info && cmd_info->op_info->cmd_ent && 
                    cmd_info->op_info->cmd_ent->access_type)
                {
                    action = cmd_info->op_info->cmd_ent->access_type;
                }
                else
                {
                    call = GLOBUS_FALSE;
                    globus_l_gfs_authorize_cb(
                        &object, action, op, GLOBUS_SUCCESS);
                }
            }
            break;
    }

    if(call)
    {
        rc = globus_gfs_acl_authorize(
            &session_handle->acl_handle,
            action,
            &object,
            &res,
            globus_l_gfs_authorize_cb,
            op);
        if(rc == GLOBUS_GFS_ACL_COMPLETE)
        {
            globus_l_gfs_authorize_cb(&object, action, op, res);
        }
    }

    GlobusGFSDebugExit();
    return;

error_op:
    globus_l_gfs_authorize_cb(&object, action, op, result);
    GlobusGFSDebugExitWithError();
}

void
globus_gfs_data_get_file_stack_list(
    globus_gfs_operation_t              in_op,
    globus_list_t **                    out_list)
{
    globus_l_gfs_data_operation_t *     op;

    op = (globus_l_gfs_data_operation_t *) in_op;

    if(op->session_handle->disk_stack_list == NULL)
    {
        *out_list = NULL;
    }
    else
    {
        *out_list = globus_list_copy(op->session_handle->disk_stack_list);
    }
}

char *
globus_gfs_data_get_cmd_string(
    globus_gfs_operation_t              op)
{
    char *                              cmd = NULL;

    cmd = globus_i_gsc_get_cmd_string(op->user_arg);

    return cmd;
}

static
globus_result_t
globus_l_gfs_data_handle_init(
    globus_l_gfs_data_handle_t **       u_handle,
    globus_gfs_data_info_t *            data_info,
    globus_list_t *                     net_stack_list,
    globus_l_gfs_data_session_t *       session_handle)
{
    int                                 tcp_mem_limit;
    globus_l_gfs_data_handle_t *        handle;
    globus_result_t                     result;
    globus_ftp_control_dcau_t           dcau;
    char *                              interface;
    globus_bool_t                       use_interface = GLOBUS_FALSE;
    gss_cred_id_t                       cred;
    GlobusGFSName(globus_l_gfs_data_handle_init);
    GlobusGFSDebugEnter();

    handle = (globus_l_gfs_data_handle_t *)
        globus_calloc(1, sizeof(globus_l_gfs_data_handle_t));
    if(!handle)
    {
        result = GlobusGFSErrorMemory("handle");
        goto error_alloc;
    }

    if((interface = globus_i_gfs_config_string("data_interface")) != NULL)
    {
        if(data_info->interface != NULL)
        {
            globus_free(data_info->interface);
        }
        data_info->interface = globus_libc_strdup(interface);
        use_interface = GLOBUS_TRUE;
    }
    memcpy(&handle->info, data_info, sizeof(globus_gfs_data_info_t));

    if(session_handle->udt_data_channel_inuse)
    {
        memcpy(&handle->data_channel, &session_handle->udt_data_channel, 
            sizeof(globus_ftp_control_handle_t));
        session_handle->udt_data_channel_inuse = GLOBUS_FALSE;
    }
    else
    {        
        result = globus_ftp_control_handle_init(&handle->data_channel);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_ftp_control_handle_init", result);
            goto error_data;
        }
    }

    handle->state = GLOBUS_L_GFS_DATA_HANDLE_VALID;
    handle->outstanding_op = NULL;
    handle->use_interface = use_interface;

    if(0 && !globus_l_gfs_data_is_remote_node)
    {
        /* this is too restrictive... if they connect to the server via ipv6
         * doesnt mean they know about ipv6 servers... this ends up requiring
         * that they use ipv6 commands
         */
        result = globus_ftp_control_data_set_interface(
            &handle->data_channel, handle->info.interface);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_ftp_control_data_set_interface", result);
            goto error_control;
        }
    }

    result = globus_ftp_control_local_mode(
        &handle->data_channel, handle->info.mode);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_ftp_control_local_mode", result);
        goto error_control;
    }

    result = globus_ftp_control_local_type(
        &handle->data_channel, handle->info.type, 0);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_ftp_control_local_type", result);
        goto error_control;
    }

    if(handle->info.tcp_bufsize > 0)
    {
        globus_ftp_control_tcpbuffer_t  tcpbuffer;

        tcpbuffer.mode = GLOBUS_FTP_CONTROL_TCPBUFFER_FIXED;
        tcpbuffer.fixed.size = handle->info.tcp_bufsize;

        result = globus_ftp_control_local_tcp_buffer(
            &handle->data_channel, &tcpbuffer);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_ftp_control_local_tcp_buffer", result);
            goto error_control;
        }
    }

    if(session_handle->upas)
    {
        handle->info.nstreams = 1;
    }
    
    if(handle->info.mode == 'S')
    {
        handle->info.nstreams = 1;
    }
    else
    {
        globus_ftp_control_parallelism_t  parallelism;

        globus_assert(handle->info.mode == 'E');

        parallelism.mode = GLOBUS_FTP_CONTROL_PARALLELISM_FIXED;
        parallelism.fixed.size = handle->info.nstreams;

        result = globus_ftp_control_local_parallelism(
            &handle->data_channel, &parallelism);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_ftp_control_local_parallelism", result);
            goto error_control;
        }

        result = globus_ftp_control_local_send_eof(
            &handle->data_channel, GLOBUS_FALSE);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_ftp_control_local_send_eof", result);
            goto error_control;
        }
    }

    tcp_mem_limit = globus_gfs_config_get_int("tcp_mem_limit");
    if(tcp_mem_limit > 0)
    {
        /* if they want too much */
        if(tcp_mem_limit < handle->info.tcp_bufsize)
        {
            globus_ftp_control_tcpbuffer_t  tcpbuffer;

            globus_gfs_log_message(
                GLOBUS_GFS_LOG_WARN,
                "Limiting TCP memory to: %d on %d\n",
                tcp_mem_limit, handle->info.nstreams);
            tcpbuffer.mode = GLOBUS_FTP_CONTROL_TCPBUFFER_FIXED;
            tcpbuffer.fixed.size = tcp_mem_limit / handle->info.nstreams;

            result = globus_ftp_control_local_tcp_buffer(
                &handle->data_channel, &tcpbuffer);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusGFSErrorWrapFailed(
                    "globus_ftp_control_local_tcp_buffer", result);
                goto error_control;
            }
        }
        /* if they don't use it all */
        else
        {
            globus_gfs_config_set_int(
                "tcp_mem_limit", handle->info.tcp_bufsize);
        }
    }

    dcau.mode = handle->info.dcau;
    dcau.subject.mode = handle->info.dcau;
    dcau.subject.subject = handle->info.subject;
    
    if(session_handle->dcsc_cred != GSS_C_NO_CREDENTIAL)
    {
        globus_xio_attr_t               xio_attr;

        cred = session_handle->dcsc_cred;

        result = globus_i_ftp_control_data_get_attr(
            &handle->data_channel,
            &xio_attr);
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_WARN,
                "couldn't access data channel attr: %s\n",
                globus_error_print_friendly(globus_error_peek(result)));
            goto error_control;
        }
        result = globus_xio_attr_cntl(
            xio_attr,
            globus_io_compat_get_gsi_driver(),
            GLOBUS_XIO_GSI_SET_ALLOW_MISSING_SIGNING_POLICY,
            GLOBUS_TRUE);
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_WARN,
                "unable to allow missing signing polcies: %s\n",
                globus_error_print_friendly(globus_error_peek(result)));
            goto error_control;
        }
    }
    else
    {
        cred = handle->info.del_cred;
    }
    
    result = globus_ftp_control_local_dcau(
        &handle->data_channel, &dcau, cred);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_ftp_control_local_dcau", result);
        goto error_control;
    }
    if(handle->info.dcau != 'N')
    {
        result = globus_ftp_control_local_prot(
            &handle->data_channel, handle->info.prot);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_ftp_control_local_prot", result);
            goto error_control;
        }
    }
    if(handle->info.ipv6)
    {
        result = globus_ftp_control_ipv6_allow(
            &handle->data_channel, GLOBUS_TRUE);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_ftp_control_ipv6_allow", result);
            goto error_control;
        }
    }
    
    /* create a default stack that we can add the netmgr driver to */
    if(globus_l_gfs_netmgr_driver && globus_list_empty(net_stack_list))
    {
        globus_xio_driver_list_ent_t *  ent;
        globus_list_t **                tailp = &net_stack_list;
        
        ent = malloc(sizeof(globus_xio_driver_list_ent_t));
        ent->driver = globus_io_compat_get_tcp_driver();
        ent->driver_name = strdup("tcp");
        ent->opts = NULL;
        ent->user_arg = NULL;
        ent->loaded = GLOBUS_TRUE;
        globus_list_insert(tailp, ent);
        tailp = globus_list_rest_ref(*tailp);
        
        ent = malloc(sizeof(globus_xio_driver_list_ent_t));
        ent->driver = globus_io_compat_get_gsi_driver();
        ent->driver_name = strdup("gsi");
        ent->opts = NULL;
        ent->user_arg = NULL;
        ent->loaded = GLOBUS_TRUE;
        globus_list_insert(tailp, ent);
        tailp = globus_list_rest_ref(*tailp);
    }
    
    if(!globus_list_empty(net_stack_list))
    {
        globus_xio_stack_t              stack;
        globus_xio_attr_t               xio_attr;
        globus_list_t *                 p;
        globus_list_t *                 new_net_stack_list = NULL;
        globus_list_t **                tailp = &new_net_stack_list;

        result = globus_i_ftp_control_data_get_attr(
            &handle->data_channel,
            &xio_attr);
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_WARN,
                "set stack failed: %s\n",
                globus_error_print_friendly(globus_error_peek(result)));
            goto error_control;
        }
        

        for (p = net_stack_list; !globus_list_empty(p); p = globus_list_rest(p))
        {
            globus_xio_driver_list_ent_t *  ent;
            ent = globus_list_first(p);

            /* Add drivers to driver list, but when dcau N, skip gsi */
            if (handle->info.dcau != 'N' || strcmp(ent->driver_name, "gsi"))
            {
                globus_list_insert(tailp, ent);
                tailp = globus_list_rest_ref(*tailp);
            }
        }

        globus_xio_stack_init(&stack, NULL);
        
        result = globus_xio_driver_list_to_stack_attr(
            new_net_stack_list, stack, xio_attr);
        globus_list_free(new_net_stack_list);

        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_WARN,
                "set stack failed: %s\n",
                globus_error_print_friendly(globus_error_peek(result)));
            goto error_control;
        }

        if(globus_l_gfs_netmgr_driver)
        {
            char *                  opt_str;

            globus_xio_stack_push_driver(stack, globus_l_gfs_netmgr_driver);
    
            opt_str = globus_common_create_string(
                "service=gridftp-data;task-id=%s;%s", 
                session_handle->taskid ? session_handle->taskid : "none", 
                globus_i_gfs_config_string("netmgr"));

            result = globus_xio_attr_cntl(
                xio_attr,
                globus_l_gfs_netmgr_driver,
                GLOBUS_XIO_SET_STRING_OPTIONS,
                opt_str);
            if(result != GLOBUS_SUCCESS)
            {
                globus_gfs_log_message(
                    GLOBUS_GFS_LOG_WARN,
                    "Setting network manager options \"%s\": %s\n",
                    opt_str,
                    globus_error_print_friendly(globus_error_peek(result)));
                globus_free(opt_str);    
                goto error_control;
            }

            globus_free(opt_str);
        }
        
        result = globus_i_ftp_control_data_set_stack(
            &handle->data_channel, stack);
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_WARN,
                "set stack failed: %s\n",
                globus_error_print_friendly(globus_error_peek(result)));
            goto error_control;
        }

        globus_xio_stack_destroy(stack);
    }

    *u_handle = handle;

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error_control:
    globus_ftp_control_handle_destroy(&handle->data_channel);

error_data:
    globus_free(handle);

error_alloc:
    GlobusGFSDebugExitWithError();
    return result;
}

static
void
globus_l_gfs_data_abort_kickout(
    void *                              user_arg)
{
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    globus_bool_t                       start_finish = GLOBUS_FALSE;
    globus_l_gfs_data_operation_t *     op;
    globus_gfs_event_info_t             event_info;
    GlobusGFSName(globus_l_gfs_data_abort_kickout);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) user_arg;

    globus_mutex_lock(&op->session_handle->mutex);
    {
        switch(op->state)
        {
            /* if finished was called while waiting for this */
            case GLOBUS_L_GFS_DATA_FINISH:
                start_finish = GLOBUS_TRUE;
                break;

            case GLOBUS_L_GFS_DATA_ABORT_CLOSING:
                op->state = GLOBUS_L_GFS_DATA_ABORTING;
                break;

            case GLOBUS_L_GFS_DATA_CONNECTING:
            case GLOBUS_L_GFS_DATA_CONNECTED:
            case GLOBUS_L_GFS_DATA_REQUESTING:
            case GLOBUS_L_GFS_DATA_ABORTING:
            case GLOBUS_L_GFS_DATA_COMPLETING:
            case GLOBUS_L_GFS_DATA_COMPLETE:
            default:
                globus_assert(0 && "bad state, possible memory corruption");
                break;
        }
        GFSDataOpDec(op, destroy_op, destroy_session);
        globus_assert(!destroy_op && !destroy_session);
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    if(start_finish)
    {
        globus_l_gfs_data_end_transfer_kickout(op);
    }
    else
    {
        if(op->session_handle->dsi->trev_func != NULL &&
            op->event_mask & GLOBUS_GFS_EVENT_TRANSFER_ABORT)
        {
            event_info.type = GLOBUS_GFS_EVENT_TRANSFER_ABORT;
            event_info.event_arg = op->event_arg;
            op->session_handle->dsi->trev_func(
                &event_info,
                op->session_handle->session_arg);
        }
    }


    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_fc_return(
    globus_l_gfs_data_operation_t *     op)
{
    GlobusGFSName(globus_l_gfs_data_fc_return);

    GlobusGFSDebugEnter();

    switch(op->data_handle->state)
    {
        case GLOBUS_L_GFS_DATA_HANDLE_CLOSING:
            op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED;
            break;

        case GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED:
            /* ok free it */
            globus_l_gfs_data_handle_free(op->data_handle);
            op->data_handle = NULL;
            break;

        case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED:
        case GLOBUS_L_GFS_DATA_HANDLE_VALID:
        case GLOBUS_L_GFS_DATA_HANDLE_INUSE:
        case GLOBUS_L_GFS_DATA_HANDLE_CLOSED:
        default:
            globus_assert(0 && "possible memory corruption");
            break;
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_complete_fc_cb(
    void *                              callback_arg,
    globus_ftp_control_handle_t *       ftp_handle,
    globus_object_t *                   error)
{
    void *                              remote_data_arg = NULL;
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    globus_gfs_event_info_t             event_info;
    globus_l_gfs_data_operation_t *     op;
    GlobusGFSName(globus_l_gfs_data_complete_fc_cb);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) callback_arg;

    memset(&event_info, '\0', sizeof(globus_gfs_event_info_t));

    globus_mutex_lock(&op->session_handle->mutex);
    {
        globus_l_gfs_data_fc_return(callback_arg);

        GFSDataOpDec(op, destroy_op, destroy_session);
        if(destroy_op)
        {
            globus_assert(op->state == GLOBUS_L_GFS_DATA_COMPLETING);
/* XXX XXX &&
                op->data_handle != NULL);
*/
            remote_data_arg = globus_l_gfs_data_check(
                op->session_handle, op->data_handle);
        }
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    if(destroy_op)
    {
        if(op->session_handle->dsi->trev_func &&
            op->event_mask & GLOBUS_GFS_EVENT_TRANSFER_COMPLETE)
        {   /* XXX should make our own */

            /* AAAA */
            event_info.type = GLOBUS_GFS_EVENT_TRANSFER_COMPLETE;
            event_info.event_arg = op->event_arg;
            op->session_handle->dsi->trev_func(
                &event_info,
                op->session_handle->session_arg);

        }
            globus_mutex_lock(&op->session_handle->mutex);
            {
                remote_data_arg = globus_l_gfs_data_post_transfer_event_cb(
                    op->session_handle, op->data_handle);
            }
            globus_mutex_unlock(&op->session_handle->mutex);
        /* destroy the op */
        globus_l_gfs_data_fire_cb(op, remote_data_arg, destroy_session);
        globus_l_gfs_data_operation_destroy(op);
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_finish_fc_cb(
    void *                              callback_arg,
    globus_ftp_control_handle_t *       ftp_handle,
    globus_object_t *                   error)
{
    globus_l_gfs_data_operation_t *     op;
    GlobusGFSName(globus_l_gfs_data_finish_fc_cb);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) callback_arg;

    globus_mutex_lock(&op->session_handle->mutex);
    {
        globus_l_gfs_data_fc_return(callback_arg);
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    globus_l_gfs_data_end_transfer_kickout(callback_arg);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_abort_fc_cb(
    void *                              callback_arg,
    globus_ftp_control_handle_t *       ftp_handle,
    globus_object_t *                   error)
{
    globus_l_gfs_data_operation_t *     op;
    GlobusGFSName(globus_l_gfs_data_abort_fc_cb);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) callback_arg;

    globus_mutex_lock(&op->session_handle->mutex);
    {
        globus_l_gfs_data_fc_return(op);
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    globus_l_gfs_data_abort_kickout(callback_arg);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_destroy_cb(
    void *                              callback_arg,
    globus_ftp_control_handle_t *       ftp_handle,
    globus_object_t *                   error)
{
    globus_bool_t                       free_session = GLOBUS_FALSE;
    globus_bool_t                       free_data = GLOBUS_FALSE;
    globus_l_gfs_data_session_t *       session_handle;
    globus_l_gfs_data_handle_t *        data_handle;
    GlobusGFSName(globus_l_gfs_data_destroy_cb);
    GlobusGFSDebugEnter();

    data_handle = (globus_l_gfs_data_handle_t *) callback_arg;
    session_handle = data_handle->session_handle;

    globus_mutex_lock(&session_handle->mutex);
    {
        session_handle->ref--;
        if(session_handle->ref == 0)
        {
            free_session = GLOBUS_TRUE;
        }
        switch(data_handle->state)
        {
            /* destroy did come from server-lib so clean it up */
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED:
                free_data = GLOBUS_TRUE;
                break;

            /* someone else got to it first */
            case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_AND_DESTROYED:
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSING:
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSED:
                break;

            /* none of these are possible */
            case GLOBUS_L_GFS_DATA_HANDLE_INUSE:
            case GLOBUS_L_GFS_DATA_HANDLE_VALID:
            default:
                globus_assert(0 && "possible memory corruption");
                break;
        }

    }
    globus_mutex_unlock(&session_handle->mutex);

    if(free_data)
    {
        globus_l_gfs_data_handle_free(data_handle);
    }
    if(free_session)
    {
        if(session_handle->dsi->destroy_func != NULL &&
            session_handle->session_arg)
        {
            session_handle->dsi->destroy_func(
                session_handle->session_arg);
        }

        if(session_handle->dsi != globus_l_gfs_dsi)
        {
            globus_extension_release(session_handle->dsi_handle);
        }

        globus_l_gfs_free_session_handle(session_handle);
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_handle_free(
    globus_l_gfs_data_handle_t *    data_handle)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    GlobusGFSName(globus_l_gfs_data_handle_free);
    GlobusGFSDebugEnter();

    globus_assert(
        data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_CLOSED_AND_DESTROYED ||
        data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED);

/*    if(data_handle->outstanding_op == NULL)
*/    {
        if(data_handle->is_mine &&
            data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED)
        {
            result = globus_ftp_control_handle_destroy(
                &data_handle->data_channel);
            if(result != GLOBUS_SUCCESS)
            {
                /* assert maybe, this should never fail
                 * we're probably leaking the data channel mem */
            }
        }
        if(result == GLOBUS_SUCCESS)
        {
            globus_free(data_handle);
        }
    }

    GlobusGFSDebugExit();
}

/* don't pass unless requested */
/*
 */
void
globus_i_gfs_data_request_handle_destroy(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              in_session_arg,
    void *                              data_arg)
{
    globus_bool_t                       free_it = GLOBUS_FALSE;
    globus_bool_t                       rc;
    void *                              session_arg;
    globus_bool_t                       pass = GLOBUS_FALSE;
    globus_result_t                     result;
    globus_l_gfs_data_session_t *       session_handle;
    globus_l_gfs_data_handle_t *        data_handle;
    int                                 old_state_dbg;
    GlobusGFSName(globus_i_gfs_data_request_handle_destroy);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *) in_session_arg;

    session_handle->data_handle = NULL;

    globus_mutex_lock(&session_handle->mutex);
    {
        data_handle = (globus_l_gfs_data_handle_t *) globus_handle_table_lookup(
            &session_handle->handle_table, (int) (intptr_t) data_arg);
        if(data_handle == NULL)
        {
            globus_assert(0);
        }
        rc = globus_handle_table_decrement_reference(
            &session_handle->handle_table, (intptr_t) data_arg);
        globus_assert(!rc);

        data_handle->destroy_requested = GLOBUS_TRUE;

        old_state_dbg = data_handle->state;
        session_arg = session_handle->session_arg;
        switch(data_handle->state)
        {
            /* not being used in a transfer o jsut clean it up */
            case GLOBUS_L_GFS_DATA_HANDLE_TE_VALID:
            case GLOBUS_L_GFS_DATA_HANDLE_VALID:
                if(!data_handle->is_mine)
                {
                    if(data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_TE_VALID)
                    {
                        data_handle->state =
                            GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_AND_DESTROYED;
                    }
                    else
                    {
                        pass = GLOBUS_TRUE;
                        free_it = GLOBUS_TRUE;
                        data_handle->state =
                            GLOBUS_L_GFS_DATA_HANDLE_CLOSED_AND_DESTROYED;
                    }
                }
                else
                {
                    if(data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_TE_VALID)
                    {
                        data_handle->state =
                            GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_AND_DESTROYED;
                    }
                    else
                    {
                        data_handle->state =
                            GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED;
                    }

                    session_handle->ref++;
                    /* set directly to closed so that when callback
                        returns we clean it up */
                    result = globus_ftp_control_data_force_close(
                        &data_handle->data_channel,
                        globus_l_gfs_data_destroy_cb,
                        data_handle);
                    if(result != GLOBUS_SUCCESS)
                    {
                        data_handle->state =
                            GLOBUS_L_GFS_DATA_HANDLE_CLOSED_AND_DESTROYED;
                        session_handle->ref--;

                        /* we should be able to set it to free here, however
                            due to inadequecies in the control_data lib we
                            must let it leak. The control_data lib has no
                            way to know when it is safe to free the mem
                            associated with its handle unless force close
                            works, thus the leak.
                        free_it = GLOBUS_TRUE;
                        */
                    }
                }
                break;

            case GLOBUS_L_GFS_DATA_HANDLE_INUSE:
                globus_assert(data_handle->outstanding_op != NULL);
                globus_l_gfs_data_start_abort(data_handle->outstanding_op);
                break;

            case GLOBUS_L_GFS_DATA_HANDLE_CLOSING:
/*  closing means pending force_close, which will clean up in its callback
                data_handle->state =
                    GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED;
                free_it = GLOBUS_TRUE;
*/
                break;


            case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED:
                data_handle->state =
                    GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_AND_DESTROYED;
                break;

            case GLOBUS_L_GFS_DATA_HANDLE_CLOSED:
                if(!data_handle->is_mine)
                {
                    pass = GLOBUS_TRUE;
                    free_it = GLOBUS_TRUE;
                    data_handle->state =
                        GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED;
                }
                else
                {
                    data_handle->state =
                        GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED;
                    /* now i can free it */
                    free_it = GLOBUS_TRUE;
                }
                break;

            /* we shouldn't get this callback twice */
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED:
                globus_assert(0 && "got destroyed callback twice");
                break;
            default:
                globus_assert(0 && "likey memory corruption");
                break;
        }
    }
    globus_mutex_unlock(&session_handle->mutex);
    if(pass)
    {
        if(session_handle->dsi->data_destroy_func != NULL)
        {
            session_handle->dsi->data_destroy_func(
                data_handle->remote_data_arg, session_arg);
        }
        else
        {
            /* XXX dsi impl error, what to do? */
        }
    }
    if(free_it)
    {
        globus_l_gfs_data_handle_free(data_handle);
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_hybrid_session_start_cb(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg)
{
    globus_l_gfs_data_operation_t *     op;
    globus_l_gfs_data_operation_t *     hybrid_op;
    
    op = user_arg;
    if(op && op->hybrid_op)
    {
        hybrid_op = op->hybrid_op;
    }
    
    if(op->type == GLOBUS_L_GFS_DATA_INFO_TYPE_PASSIVE)
    {
        if(reply->result != GLOBUS_SUCCESS)
        {
            globus_l_gfs_data_passive_bounce_t * bounce_info;
            bounce_info = (globus_l_gfs_data_passive_bounce_t *)
                globus_calloc(1, sizeof(globus_l_gfs_data_passive_bounce_t));
            if(!bounce_info)
            {
                globus_panic(NULL, 0, "small malloc failure, no recovery");
            }
            bounce_info->ipc_handle = hybrid_op->ipc_handle;
            bounce_info->id = hybrid_op->id;
            bounce_info->callback = hybrid_op->callback;
            bounce_info->user_arg = hybrid_op->user_arg;
            bounce_info->result = reply->result;
            bounce_info->handle = NULL;
            globus_callback_register_oneshot(
                NULL,
                NULL,
                globus_l_gfs_data_passive_kickout,
                bounce_info);
        }
    
        else
        {
            globus_i_gfs_data_request_passive(
                hybrid_op->ipc_handle,
                hybrid_op->session_handle,
                hybrid_op->id,
                hybrid_op->info_struct,
                hybrid_op->callback,
                hybrid_op->user_arg);
        }
    }
    else
    {

        if(reply->result != GLOBUS_SUCCESS)
        {
            globus_l_gfs_data_active_bounce_t * bounce_info;
            bounce_info = (globus_l_gfs_data_active_bounce_t *)
                globus_malloc(sizeof(globus_l_gfs_data_active_bounce_t));
            if(!bounce_info)
            {
                globus_panic(NULL, 0, "small malloc failure, no recovery");
            }
            bounce_info->ipc_handle = hybrid_op->ipc_handle;
            bounce_info->id = hybrid_op->id;
            bounce_info->callback = hybrid_op->callback;
            bounce_info->user_arg = hybrid_op->user_arg;
            bounce_info->result = reply->result;
            bounce_info->handle = NULL;
            globus_callback_register_oneshot(
                NULL,
                NULL,
                globus_l_gfs_data_active_kickout,
                bounce_info);
        }
    
        else
        {
            globus_i_gfs_data_request_active(
                hybrid_op->ipc_handle,
                hybrid_op->session_handle,
                hybrid_op->id,
                hybrid_op->info_struct,
                hybrid_op->callback,
                hybrid_op->user_arg);
        }
    }
    if(hybrid_op)
    {
        globus_l_gfs_data_operation_destroy(hybrid_op);
    }


}

static
void
globus_l_gfs_data_passive_kickout(
    void *                              user_arg)
{
    globus_l_gfs_data_passive_bounce_t * bounce_info;
    globus_gfs_finished_info_t              reply;
    GlobusGFSName(globus_l_gfs_data_passive_kickout);
    GlobusGFSDebugEnter();

    bounce_info = (globus_l_gfs_data_passive_bounce_t *) user_arg;

    memset(&reply, '\0', sizeof(globus_gfs_finished_info_t));
    reply.type = GLOBUS_GFS_OP_PASSIVE;
    reply.id = bounce_info->id;
    reply.result = bounce_info->result;
    reply.info.data.contact_strings = (const char **)
        globus_calloc(1, sizeof(char *));
    reply.info.data.contact_strings[0] = bounce_info->contact_string;
    reply.info.data.bi_directional = bounce_info->bi_directional;
    reply.info.data.cs_count = 1;

    /* as soon as we finish the data handle can be in play, set its
        state appropriately.  if not success then we never created a
        handle */
    if(bounce_info->result == GLOBUS_SUCCESS)
    {
        bounce_info->handle->is_mine = GLOBUS_TRUE;
        bounce_info->handle->state = GLOBUS_L_GFS_DATA_HANDLE_VALID;

        reply.info.data.data_arg = (void *) (intptr_t) 
            globus_handle_table_insert(
                &bounce_info->handle->session_handle->handle_table,
                bounce_info->handle,
                1);
    }
    else
    {
        globus_assert(bounce_info->handle == NULL);
    }

    if(bounce_info->callback != NULL)
    {
        bounce_info->callback(
            &reply,
            bounce_info->user_arg);
    }
    else
    {
        globus_gfs_ipc_reply_finished(
            bounce_info->ipc_handle,
            &reply);
    }

    globus_free(reply.info.data.contact_strings);
    /* could be null on error */
    if(bounce_info->contact_string != NULL)
    {
        globus_free(bounce_info->contact_string);
    }
    globus_free(bounce_info);

    GlobusGFSDebugExit();
}

/*
 *
 *  NOTE: if bounce struct can't be allocated we fail. This should be
 *        corrected at some point, possibly by preaalocating a bunch in
 *        in a globus_memory_t.
 */
void
globus_i_gfs_data_request_passive(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_data_info_t *            data_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg)
{
    globus_l_gfs_data_handle_t *        handle = NULL;
    globus_result_t                     result;
    globus_ftp_control_host_port_t      address;
    globus_sockaddr_t                   addr;
    char *                              cs;
    globus_l_gfs_data_passive_bounce_t * bounce_info;
    globus_l_gfs_data_operation_t *     op;
    globus_l_gfs_data_session_t *       session_handle;
    globus_bool_t                       ipv6_addr = GLOBUS_FALSE;
    GlobusGFSName(globus_i_gfs_data_request_passive);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *) session_arg;
    globus_l_gfs_data_reset_watchdog(session_handle, NULL);

    if(session_handle->hybrid && data_info->max_cs != 1 && 
        session_handle->dsi != globus_l_gfs_dsi_hybrid)
    {
        globus_l_gfs_data_operation_t *     hybrid_op;
        result = globus_i_gfs_data_new_dsi(
            &globus_l_gfs_active_dsi_handle, 
            "remote", 
            &globus_l_gfs_dsi_hybrid, 
            GLOBUS_FALSE);
        
        if(!globus_l_gfs_dsi_hybrid)
        {
            goto error_op;
        }
        /* release old dsi's session */
        if(session_handle->dsi->destroy_func != NULL &&
            session_handle->session_arg)
        {
            session_handle->dsi->destroy_func(
                session_handle->session_arg);
            session_handle->session_arg = NULL;
        }

        session_handle->dsi = globus_l_gfs_dsi_hybrid;
        
        result = globus_l_gfs_data_operation_init(&op, session_handle);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_l_gfs_data_operation_init", result);
            goto error_op;
        }
        
        result = globus_l_gfs_data_operation_init(
            (globus_l_gfs_data_operation_t **) &op->hybrid_op, session_handle);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_l_gfs_data_operation_init", result);
            goto error_op;
        }

        hybrid_op = op->hybrid_op;
        hybrid_op->ipc_handle = ipc_handle;
        hybrid_op->id = id;
        hybrid_op->state = GLOBUS_L_GFS_DATA_REQUESTING;
        hybrid_op->callback = cb;
        hybrid_op->user_arg = user_arg;
        hybrid_op->session_handle = session_handle;
        hybrid_op->info_struct = data_info;

        
        
        op->callback = globus_l_gfs_data_hybrid_session_start_cb;
        op->user_arg = op;
        op->session_handle = session_handle;
        op->info_struct = session_handle->session_info_copy;
        op->ipc_handle = ipc_handle;
        op->type = GLOBUS_L_GFS_DATA_INFO_TYPE_PASSIVE;
        globus_l_gfs_data_auth_init_cb(
                NULL, GFS_ACL_ACTION_INIT, op, GLOBUS_SUCCESS);
        
        return;
    }
    
    if(session_handle->dsi->passive_func != NULL)
    {
        result = globus_l_gfs_data_operation_init(&op, session_handle);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_l_gfs_data_operation_init", result);
            goto error_op;
        }

        op->ipc_handle = ipc_handle;
        op->id = id;
        op->state = GLOBUS_L_GFS_DATA_REQUESTING;
        op->callback = cb;
        op->user_arg = user_arg;
        op->session_handle = session_handle;
        op->info_struct = data_info;
        op->type = GLOBUS_L_GFS_DATA_INFO_TYPE_PASSIVE;
        if(session_handle->dcsc_cred != GSS_C_NO_CREDENTIAL)
        {
            data_info->del_cred = session_handle->dcsc_cred;
        }
        if(session_handle->dsi->descriptor & GLOBUS_GFS_DSI_DESCRIPTOR_BLOCKING)
        {
            globus_callback_register_oneshot(
                NULL,
                NULL,
                globus_l_gfs_blocking_dispatch_kickout,
                op);
        }
        else
        {
            session_handle->dsi->passive_func(
                op, data_info, session_handle->session_arg);
        }
    }
    else
    {
        if(data_info->del_cred == NULL)
        {
            data_info->del_cred = session_handle->del_cred;
        }
        else
        {   
            session_handle->dcsc_cred = data_info->del_cred;
            data_info->del_cred = NULL;
        }

        result = globus_l_gfs_data_handle_init(
            &handle, data_info, session_handle->net_stack_list, session_handle);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_l_gfs_data_handle_init", result);
            goto error_handle;
        }
        handle->session_handle = session_handle;

        handle->info.cs_count = 1;
        /* prevent address lookup, we know what we want */
        address.host[0] = 1;
        address.port = 0;
        result = globus_ftp_control_local_pasv(&handle->data_channel, &address);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_ftp_control_local_pasv", result);
            goto error_control;
        }
        

        /* XXX This needs to be smarter.  The address should be the same one
         * the user is connected to on the control channel (at least when
         * operating as a normal standalone server)
         */
        /* its ok to use AF_INET here since we are requesting the LOCAL
         * address.  we just use AF_INET to store the port
         */
        if(handle->info.interface &&
            (!globus_l_gfs_data_is_remote_node || handle->use_interface))
        {
            ipv6_addr = (strchr(handle->info.interface, ':') != NULL);
        }

        if((globus_l_gfs_data_is_remote_node && !handle->use_interface) ||
            (ipv6_addr && !handle->info.ipv6) ||
            handle->info.interface == NULL)
        {
            GlobusLibcSockaddrSetFamily(addr, AF_INET);
            GlobusLibcSockaddrSetPort(addr, address.port);
            result = globus_libc_addr_to_contact_string(
                &addr,
                GLOBUS_LIBC_ADDR_LOCAL | GLOBUS_LIBC_ADDR_NUMERIC |
                    (handle->info.ipv6 ? 0 : GLOBUS_LIBC_ADDR_IPV4),
                &cs);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusGFSErrorWrapFailed(
                    "globus_libc_addr_to_contact_string", result);
                goto error_control;
            }
        }
        else
        {
            if(ipv6_addr)
            {
                cs = globus_common_create_string(
                    "[%s]:%d", handle->info.interface, (int) address.port);
            }
            else
            {
                cs = globus_common_create_string(
                    "%s:%d", handle->info.interface, (int) address.port);
            }
        }

        bounce_info = (globus_l_gfs_data_passive_bounce_t *)
            globus_calloc(1, sizeof(globus_l_gfs_data_passive_bounce_t));
        if(!bounce_info)
        {
            result = GlobusGFSErrorMemory("bounce_info");
            globus_panic(NULL, result, "small malloc failure, no recovery");
        }

        bounce_info->result = GLOBUS_SUCCESS;
        bounce_info->ipc_handle = ipc_handle;
        bounce_info->id = id;
        bounce_info->handle = handle;
        bounce_info->bi_directional = GLOBUS_TRUE; /* XXX MODE S only */
        bounce_info->contact_string = cs;
        bounce_info->callback = cb;
        bounce_info->user_arg = user_arg;

        session_handle->data_handle = handle;

        result = globus_callback_register_oneshot(
            NULL,
            NULL,
            globus_l_gfs_data_passive_kickout,
            bounce_info);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_callback_register_oneshot", result);
            globus_panic(NULL, result, "small malloc failure, no recovery");
        }
    }

    GlobusGFSDebugExit();
    return;

error_control:
    globus_ftp_control_handle_destroy(&handle->data_channel);
    globus_free(handle);
    handle = NULL;

error_handle:
error_op:

    bounce_info = (globus_l_gfs_data_passive_bounce_t *)
        globus_calloc(1, sizeof(globus_l_gfs_data_passive_bounce_t));
    if(!bounce_info)
    {
        result = GlobusGFSErrorMemory("bounce_info");
        globus_panic(NULL, result, "small malloc failure, no recovery");
    }
    bounce_info->ipc_handle = ipc_handle;
    bounce_info->id = id;
    bounce_info->handle = handle;
    bounce_info->bi_directional = GLOBUS_TRUE; /* XXX MODE S only */
    bounce_info->callback = cb;
    bounce_info->user_arg = user_arg;
    bounce_info->result = result;
    bounce_info->handle = NULL;
    result = globus_callback_register_oneshot(
        NULL,
        NULL,
        globus_l_gfs_data_passive_kickout,
        bounce_info);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_callback_register_oneshot", result);
        globus_panic(NULL, result, "small malloc failure, no recovery");
    }

    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_data_active_kickout(
    void *                              user_arg)
{
    globus_l_gfs_data_active_bounce_t * bounce_info;
    globus_gfs_finished_info_t              reply;
    GlobusGFSName(globus_l_gfs_data_active_kickout);
    GlobusGFSDebugEnter();

    bounce_info = (globus_l_gfs_data_active_bounce_t *) user_arg;

    memset(&reply, '\0', sizeof(globus_gfs_finished_info_t));
    reply.type = GLOBUS_GFS_OP_ACTIVE;
    reply.id = bounce_info->id;
    reply.result = bounce_info->result;
    reply.info.data.bi_directional = bounce_info->bi_directional;

    /* as soon as we finish the data handle can be in play, set its
        state appropriately.  if not success then we never created a
        handle */
    if(bounce_info->result == GLOBUS_SUCCESS)
    {
        bounce_info->handle->state = GLOBUS_L_GFS_DATA_HANDLE_VALID;
        bounce_info->handle->is_mine = GLOBUS_TRUE;

        reply.info.data.data_arg = (void *) (intptr_t)
            globus_handle_table_insert(
                &bounce_info->handle->session_handle->handle_table,
                bounce_info->handle,
                1);
    }
    else
    {
        globus_assert(bounce_info->handle == NULL);
    }

    if(bounce_info->callback != NULL)
    {
        bounce_info->callback(
            &reply,
            bounce_info->user_arg);
    }
    else
    {
        globus_gfs_ipc_reply_finished(
            bounce_info->ipc_handle,
            &reply);
    }

    globus_free(bounce_info);

    GlobusGFSDebugExit();
}

void
globus_i_gfs_data_request_active(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_data_info_t *            data_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg)
{
    globus_l_gfs_data_handle_t *        handle;
    globus_result_t                     result;
    globus_ftp_control_host_port_t *    addresses;
    int                                 i;
    globus_l_gfs_data_active_bounce_t * bounce_info;
    globus_l_gfs_data_operation_t *     op;
    globus_l_gfs_data_session_t *       session_handle;
    GlobusGFSName(globus_i_gfs_data_request_active);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *) session_arg;
    globus_l_gfs_data_reset_watchdog(session_handle, NULL);

    if(session_handle->hybrid && data_info->cs_count != 1 && 
        session_handle->dsi != globus_l_gfs_dsi_hybrid)
    {
        globus_l_gfs_data_operation_t *     hybrid_op;
        
        result = globus_i_gfs_data_new_dsi(
            &globus_l_gfs_active_dsi_handle,
            "remote",
            &globus_l_gfs_dsi_hybrid,
            GLOBUS_FALSE);
        
        if(!globus_l_gfs_dsi_hybrid)
        {
            goto error_op;
        }
        /* release old dsi's session */
        if(session_handle->dsi->destroy_func != NULL &&
            session_handle->session_arg)
        {
            session_handle->dsi->destroy_func(
                session_handle->session_arg);
            session_handle->session_arg = NULL;
        }

        session_handle->dsi = globus_l_gfs_dsi_hybrid;
        
        result = globus_l_gfs_data_operation_init(&op, session_handle);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_l_gfs_data_operation_init", result);
            goto error_op;
        }
        
        result = globus_l_gfs_data_operation_init(
            (globus_l_gfs_data_operation_t **) &op->hybrid_op, session_handle);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_l_gfs_data_operation_init", result);
            goto error_op;
        }

        hybrid_op = op->hybrid_op;
        hybrid_op->ipc_handle = ipc_handle;
        hybrid_op->id = id;
        hybrid_op->state = GLOBUS_L_GFS_DATA_REQUESTING;
        hybrid_op->callback = cb;
        hybrid_op->user_arg = user_arg;
        hybrid_op->session_handle = session_handle;
        hybrid_op->info_struct = data_info;

        
        
        op->callback = globus_l_gfs_data_hybrid_session_start_cb;
        op->user_arg = op;
        op->session_handle = session_handle;
        op->info_struct = session_handle->session_info_copy;
        op->ipc_handle = ipc_handle;
        op->type = GLOBUS_L_GFS_DATA_INFO_TYPE_ACTIVE;
        globus_l_gfs_data_auth_init_cb(
                NULL, GFS_ACL_ACTION_INIT, op, GLOBUS_SUCCESS);
        
        return;
    }
    
    if(session_handle->dsi->active_func != NULL)
    {
        result = globus_l_gfs_data_operation_init(&op, session_handle);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_l_gfs_data_operation_init", result);
            goto error_op;
        }

        op->ipc_handle = ipc_handle;
        op->id = id;
        op->state = GLOBUS_L_GFS_DATA_REQUESTING;
        op->callback = cb;
        op->user_arg = user_arg;
        op->session_handle = session_handle;
        op->info_struct = data_info;
        op->type = GLOBUS_L_GFS_DATA_INFO_TYPE_ACTIVE;
        if(session_handle->dcsc_cred != GSS_C_NO_CREDENTIAL)
        {
            data_info->del_cred = session_handle->dcsc_cred;
        }
        if(session_handle->dsi->descriptor & GLOBUS_GFS_DSI_DESCRIPTOR_BLOCKING)
        {
            globus_callback_register_oneshot(
                NULL,
                NULL,
                globus_l_gfs_blocking_dispatch_kickout,
                op);
        }
        else
        {
            session_handle->dsi->active_func(
                op, data_info, session_handle->session_arg);
        }
    }
    else
    {
        if(data_info->del_cred == NULL)
        {
            data_info->del_cred = session_handle->del_cred;
        }
        else
        {
            session_handle->dcsc_cred = data_info->del_cred;
            data_info->del_cred = NULL;
        }
        result = globus_l_gfs_data_handle_init(
            &handle, data_info, session_handle->net_stack_list, session_handle);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_l_gfs_data_handle_init", result);
            goto error_handle;
        }
        handle->session_handle = session_handle;
        addresses = (globus_ftp_control_host_port_t *)
            globus_malloc(sizeof(globus_ftp_control_host_port_t) *
                data_info->cs_count);
        if(!addresses)
        {
            result = GlobusGFSErrorMemory("addresses");
            goto error_addresses;
        }

        for(i = 0; i < data_info->cs_count; i++)
        {
            result = globus_libc_contact_string_to_ints(
                data_info->contact_strings[i],
                addresses[i].host,  &addresses[i].hostlen, &addresses[i].port);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusGFSErrorWrapFailed(
                    "globus_libc_contact_string_to_ints", result);
                goto error_format;
            }
        }

        if(data_info->cs_count == 1)
        {
            result = globus_ftp_control_local_port(
                &handle->data_channel, addresses);
        }
        else
        {
            result = globus_ftp_control_local_spor(
                &handle->data_channel, addresses, data_info->cs_count);
        }
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_ftp_control_local_port/spor", result);
            goto error_control;
        }

        bounce_info = (globus_l_gfs_data_active_bounce_t *)
            globus_malloc(sizeof(globus_l_gfs_data_active_bounce_t));
        if(!bounce_info)
        {
            result = GlobusGFSErrorMemory("bounce_info");
            globus_panic(NULL, result, "small malloc failure, no recovery");
        }

        bounce_info->ipc_handle = ipc_handle;
        bounce_info->id = id;
        bounce_info->handle = handle;
        bounce_info->bi_directional = GLOBUS_TRUE; /* XXX MODE S only */
        bounce_info->callback = cb;
        bounce_info->user_arg = user_arg;
        bounce_info->result = GLOBUS_SUCCESS;

        session_handle->data_handle = handle;

        result = globus_callback_register_oneshot(
            NULL,
            NULL,
            globus_l_gfs_data_active_kickout,
            bounce_info);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_callback_register_oneshot", result);
            globus_panic(NULL, result, "small malloc failure, no recovery");
        }

        globus_free(addresses);
    }

    GlobusGFSDebugExit();
    return;

error_control:
error_format:
    globus_free(addresses);
error_addresses:
    globus_ftp_control_handle_destroy(&handle->data_channel);
    globus_free(handle);
    handle = NULL;
error_handle:
error_op:
    bounce_info = (globus_l_gfs_data_active_bounce_t *)
        globus_malloc(sizeof(globus_l_gfs_data_active_bounce_t));
    if(!bounce_info)
    {
        result = GlobusGFSErrorMemory("bounce_info");
        globus_panic(NULL, result, "small malloc failure, no recovery");
    }
    bounce_info->ipc_handle = ipc_handle;
    bounce_info->id = id;
    bounce_info->handle = handle;
    bounce_info->bi_directional = GLOBUS_TRUE; /* XXX MODE S only */
    bounce_info->callback = cb;
    bounce_info->user_arg = user_arg;
    bounce_info->result = result;
    bounce_info->handle = NULL;
    result = globus_callback_register_oneshot(
        NULL,
        NULL,
        globus_l_gfs_data_active_kickout,
        bounce_info);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_callback_register_oneshot", result);
        globus_panic(NULL, result, "small malloc failure, no recovery");
    }

    GlobusGFSDebugExitWithError();
}


void
globus_i_gfs_data_request_recv(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_transfer_info_t *        recv_info,
    globus_i_gfs_data_callback_t        cb,
    globus_i_gfs_data_event_callback_t  event_cb,
    void *                              user_arg)
{
    globus_l_gfs_data_operation_t *     op;
    globus_result_t                     result;
    globus_l_gfs_data_handle_t *        data_handle = NULL;
    globus_gfs_stat_info_t *            stat_info;
    globus_l_gfs_data_session_t *       session_handle;
    GlobusGFSName(globus_i_gfs_data_request_recv);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *) session_arg;
    globus_l_gfs_data_reset_watchdog(session_handle, "RECV");
    
    /* YOU ARE ENTERING THE UGLY HACK ZONE */
    globus_mutex_lock(&session_handle->mutex);
    {
        data_handle = (globus_l_gfs_data_handle_t *) globus_handle_table_lookup(
            &session_handle->handle_table, (intptr_t) recv_info->data_arg);
        if(data_handle == NULL)
        {
            result = GlobusGFSErrorData("Data handle not found");
        globus_mutex_unlock(&session_handle->mutex);
            goto error_handle;
        }

        if(!data_handle->is_mine)
        {
            recv_info->data_arg = data_handle->remote_data_arg;
        }
    }
    globus_mutex_unlock(&session_handle->mutex);
    /* YOU ARE leaving THE UGLY HACK ZONE */

    result = globus_l_gfs_data_operation_init(&op, session_handle);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_l_gfs_data_operation_init", result);
        goto error_op;
    }
 
    op->ipc_handle = ipc_handle;
    op->session_handle = session_handle;
    op->type = GLOBUS_L_GFS_DATA_INFO_TYPE_RECV;
    op->info_struct = recv_info;
    op->id = id;
    op->state = GLOBUS_L_GFS_DATA_REQUESTING;
    op->writing = GLOBUS_FALSE;
    op->data_handle = data_handle;
    op->data_arg = recv_info->data_arg;
    op->range_list = recv_info->range_list;
    op->partial_offset = recv_info->partial_offset;
    op->callback = cb;
    op->event_callback = event_cb;
    op->user_arg = user_arg;
    op->node_ndx = recv_info->node_ndx;
    session_handle->node_ndx = recv_info->node_ndx;
    op->node_count = recv_info->node_count;
    op->stripe_count = recv_info->stripe_count;
    if(session_handle->storattr_str)
    {
        op->storattr = (globus_l_gfs_storattr_t *) 
            globus_calloc(1, sizeof(globus_l_gfs_storattr_t));
        op->storattr->all = session_handle->storattr_str;
        session_handle->storattr_str = NULL;

        op->storattr->modify = globus_i_gfs_kv_getval(
            op->storattr->all, "modify", 0);
            
        op->storattr->checksum_md5 = globus_i_gfs_kv_getval(
            op->storattr->all, "x.checksum.md5", 0);
        if(!op->storattr->checksum_md5)
        {
            op->storattr->checksum_md5 = globus_i_gfs_kv_getval(
                op->storattr->all, "checksum.md5", 0);
        }
        
        if(op->storattr->checksum_md5 && !recv_info->expected_checksum)
        {
            recv_info->expected_checksum_alg = globus_libc_strdup("md5");
            recv_info->expected_checksum = 
                globus_libc_strdup(op->storattr->checksum_md5);
        }
    }
    /* events and disconnects cannot happen while i am in this
        function */
    if(data_handle)
    {
        /* globus_assert(data_handle->outstanding_op == NULL); */
        data_handle->outstanding_op = op;
    globus_assert(data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_VALID || 
        data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_TE_VALID);
    data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_INUSE;
    }

    if(!data_handle->is_mine)
    {
        op->op_info_id = globus_l_gfs_op_info_ctr++;
        if(!recv_info->op_info)
        {
            recv_info->op_info = globus_calloc(1, sizeof(globus_i_gfs_op_info_t));
        }
        recv_info->op_info->id = op->op_info_id;
    }

    op->dsi = globus_l_gfs_data_new_dsi(session_handle, recv_info->module_name);
    if(op->dsi == NULL)
    {
        globus_gridftp_server_finished_transfer(
            op, GlobusGFSErrorGeneric("bad module"));
        goto error_module;
    }
    if (globus_i_gfs_config_bool("data_node") &&
        globus_i_gfs_config_int("auth_level")&GLOBUS_L_GFS_AUTH_DATA_NODE_PATH)
    {
        char *                          chdir_to;
        char *                          full_pathname = NULL;

        chdir_to = globus_i_gfs_config_string("chdir_to");

        result = globus_i_gfs_get_full_path(
            session_handle->home_dir,
            chdir_to ? chdir_to : "/", // XXX
            session_handle,
            recv_info->pathname,
            &full_pathname,
            GFS_L_READ);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_l_gfs_data_operation_init", result);
            goto error_op;
        }
        if (full_pathname)
        {
            free(recv_info->pathname);
            recv_info->pathname = full_pathname;
        }
    }
    if(op->dsi->stat_func != NULL)
    {
        stat_info = (globus_gfs_stat_info_t *)
            globus_calloc(1, sizeof(globus_gfs_stat_info_t));

        stat_info->pathname = recv_info->pathname;
        stat_info->file_only = GLOBUS_TRUE;
        stat_info->internal = GLOBUS_TRUE;

        op->info_struct = recv_info;
        op->stat_wrapper = stat_info;

        globus_i_gfs_data_request_stat(
            ipc_handle,
            session_handle,
            id,
            stat_info,
            globus_l_gfs_data_auth_stat_cb,
            op);
    }
    else
    {
        globus_gfs_acl_object_desc_t    object;
        object.name = recv_info->pathname;
        object.size = recv_info->alloc_size;
        result = GLOBUS_SUCCESS;
        globus_l_gfs_authorize_cb(
            &object, GFS_ACL_ACTION_WRITE, op, result);
    }
    GlobusGFSDebugExit();
    return;

error_module:
error_op:
error_handle:
    globus_gridftp_server_finished_transfer(op, result);
    GlobusGFSDebugExitWithError();
}


void
globus_i_gfs_data_request_send(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_transfer_info_t *        send_info,
    globus_i_gfs_data_callback_t        cb,
    globus_i_gfs_data_event_callback_t  event_cb,
    void *                              user_arg)
{
    globus_result_t                     res;
    int                                 rc;
    globus_l_gfs_data_operation_t *     op;
    globus_result_t                     result;
    globus_l_gfs_data_handle_t *        data_handle = NULL;
    globus_l_gfs_data_session_t *       session_handle;
    globus_gfs_acl_object_desc_t        object;
    GlobusGFSName(globus_i_gfs_data_send_request);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *) session_arg;

    /* YOU ARE ENTRERING THE UGLY HACK ZONE */
    globus_mutex_lock(&session_handle->mutex);
    {
        data_handle = (globus_l_gfs_data_handle_t *) globus_handle_table_lookup(
            &session_handle->handle_table, (intptr_t) send_info->data_arg);
        if(data_handle == NULL)
        {
            result = GlobusGFSErrorData(_FSSL("Data handle not found",NULL));
            globus_mutex_unlock(&session_handle->mutex);
            goto error_handle;
        }
        if(!data_handle->is_mine)
        {
            send_info->data_arg = data_handle->remote_data_arg;
        }
    }
    globus_mutex_unlock(&session_handle->mutex);
    /* YOU ARE leaving THE UGLY HACK ZONE */

    if(data_handle->is_mine)
    {
        globus_l_gfs_data_reset_watchdog(session_handle, "SEND");
    }
    else
    {
        globus_l_gfs_data_reset_watchdog(session_handle, NULL);
    }

    result = globus_l_gfs_data_operation_init(&op, session_handle);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_l_gfs_data_operation_init", result);
        goto error_op;
    }

    op->ipc_handle = ipc_handle;
    op->session_handle = session_handle;
    op->type = GLOBUS_L_GFS_DATA_INFO_TYPE_SEND;
    op->info_struct = send_info;

    op->id = id;
    op->state = GLOBUS_L_GFS_DATA_REQUESTING;
    op->writing = GLOBUS_TRUE;
    op->data_handle = data_handle;
    op->data_arg = send_info->data_arg;
    op->range_list = send_info->range_list;
    op->partial_length = send_info->partial_length;
    op->partial_offset = send_info->partial_offset;
    op->callback = cb;
    op->event_callback = event_cb;
    op->user_arg = user_arg;
    op->node_ndx = send_info->node_ndx;
    session_handle->node_ndx = send_info->node_ndx;
    op->write_stripe = 0;
    op->stripe_chunk = send_info->node_ndx;
    op->node_count = send_info->node_count;
    op->stripe_count = send_info->stripe_count;
    op->eof_count = (int *) globus_calloc(1, op->stripe_count * sizeof(int));

    /* events and disconnects cannot happen while i am in this
        function */
    if(data_handle)
    {
        /*globus_assert(data_handle->outstanding_op == NULL); */
        data_handle->outstanding_op = op;
    globus_assert(data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_VALID || 
        data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_TE_VALID);
    data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_INUSE;
    }

    if(!data_handle->is_mine)
    {
        op->op_info_id = globus_l_gfs_op_info_ctr++;
        if(!send_info->op_info)
        {
            send_info->op_info = globus_calloc(1, sizeof(globus_i_gfs_op_info_t));
        }
        send_info->op_info->id = op->op_info_id;
    }

    op->dsi = globus_l_gfs_data_new_dsi(session_handle, send_info->module_name);
    if (globus_i_gfs_config_bool("data_node") &&
        globus_i_gfs_config_int("auth_level")&GLOBUS_L_GFS_AUTH_DATA_NODE_PATH)
    {
        char *                          chdir_to;
        char *                          full_pathname = NULL;

        chdir_to = globus_i_gfs_config_string("chdir_to");
        
        result = globus_i_gfs_get_full_path(
            session_handle->home_dir,
            chdir_to ? chdir_to : "/", // XXX
            session_handle,
            send_info->pathname,
            &full_pathname,
            GFS_L_READ);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_l_gfs_data_operation_init", result);
            goto error_op;
        }
        if (full_pathname)
        {
            free(send_info->pathname);
            send_info->pathname = full_pathname;

        }
    }
    if(op->dsi == NULL)
    {
        globus_gridftp_server_finished_transfer(
            op, GlobusGFSErrorGeneric("bad module"));
        goto error_module;
    }
    if(op->dsi->stat_func != NULL &&
        op->data_handle->info.stripe_layout == GLOBUS_GFS_LAYOUT_PARTITIONED)
    {
        globus_gfs_stat_info_t *        stat_info;

        stat_info = (globus_gfs_stat_info_t *)
            globus_calloc(1, sizeof(globus_gfs_stat_info_t));

        stat_info->pathname = send_info->pathname;
        stat_info->file_only = GLOBUS_TRUE;
        stat_info->internal = GLOBUS_TRUE;

        op->info_struct = send_info;
        op->stat_wrapper = stat_info;

        globus_i_gfs_data_request_stat(
            ipc_handle,
            session_handle,
            id,
            stat_info,
            globus_l_gfs_data_send_stat_cb,
            op);
    }
    else
    {
        object.name = send_info->pathname;
        rc = globus_gfs_acl_authorize(
            &session_handle->acl_handle,
            GFS_ACL_ACTION_READ,
            &object,
            &res,
            globus_l_gfs_authorize_cb,
            op);
        if(rc == GLOBUS_GFS_ACL_COMPLETE)
        {
            globus_l_gfs_authorize_cb(
                &object, GFS_ACL_ACTION_READ, op, res);
        }
    }
    GlobusGFSDebugExit();
    return;

error_module:
error_op:
error_handle:
    globus_gridftp_server_finished_transfer(op, result);
    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_data_list_done(
    globus_l_gfs_data_operation_t*      op, 
    globus_result_t                     result)
{
    while(op->path_list)
    {
        globus_l_gfs_data_path_list_t* tofree = op->path_list;
        op->path_list = tofree->next;
        globus_free(tofree->pathname);
        globus_free(tofree);
    }

    while(op->root_paths)
    {
        globus_l_gfs_data_path_list_t* tofree = op->root_paths;
        op->root_paths = tofree->next;
        globus_free(tofree->pathname);
        globus_free(tofree);
    }
        
    op->current_path = NULL;
    globus_free(op->stat_wrapper);
    globus_gridftp_server_finished_transfer(op, result);
}

static
globus_result_t
globus_l_gfs_data_request_next_path(
    globus_l_gfs_data_operation_t *     orig_op)
{
    globus_l_gfs_data_operation_t *     op;
    globus_result_t                     result;
    globus_gfs_stat_info_t *            stat_info;
    globus_l_gfs_data_path_list_t*      nextpath;
    GlobusGFSName(globus_l_gfs_data_request_next_path);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(orig_op->session_handle);

    stat_info = (globus_gfs_stat_info_t*)orig_op->stat_wrapper;

    if (orig_op->current_path)
    {
        free(orig_op->current_path);
        orig_op->current_path = NULL;
        free(stat_info->pathname);
    }
 
    nextpath = orig_op->path_list;
    if(nextpath)
    {
        orig_op->path_list = nextpath->next;
        
        result = globus_l_gfs_data_operation_init(&op, orig_op->session_handle);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_op;
        }    
        
        op->uid = getuid();
        op->state = GLOBUS_L_GFS_DATA_REQUESTING;
        op->callback = globus_l_gfs_data_list_stat_cb;
        op->user_arg = orig_op;
        op->session_handle = orig_op->session_handle;
        op->info_struct = orig_op->stat_wrapper;
        op->type = GLOBUS_L_GFS_DATA_INFO_TYPE_STAT;
    
        stat_info->pathname = nextpath->pathname;
        
        globus_callback_register_oneshot(
            NULL,
            NULL,
            globus_l_gfs_blocking_dispatch_kickout,
            op);
            
        orig_op->current_path = nextpath;
    }

    GlobusGFSDebugExit();
    return (nextpath != NULL);

error_op:
    GlobusGFSDebugExitWithError();
    return 0;
}

static
void
globus_l_gfs_data_list_write_cb(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_gfs_data_stat_bounce_t *   bounce_info;
    globus_bool_t                       finish = GLOBUS_FALSE;

    GlobusGFSName(globus_l_gfs_data_list_write_cb);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    bounce_info = (globus_l_gfs_data_stat_bounce_t *) user_arg;

    if(!bounce_info->custom_list)
    {
        globus_gridftp_server_control_list_buffer_free(buffer);
    }
    else if(bounce_info->free_buffer)
    {
        globus_free(bounce_info->list_response);
    }
    
    globus_mutex_lock(&op->stat_lock);
    if(bounce_info->final_stat && !globus_l_gfs_data_request_next_path(op))
    {
        finish = GLOBUS_TRUE;
    }
    globus_mutex_unlock(&op->stat_lock);
    
    globus_free(bounce_info);

    if(op->delayed_error != GLOBUS_SUCCESS)
    {
        result = op->delayed_error;
    }
    
    if(finish)
    {
        globus_l_gfs_data_list_done(op, result);
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_list_stat_cb(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg)
{
    globus_gfs_operation_t   op;
    globus_byte_t *                     list_buffer;
    globus_size_t                       buffer_len;
    globus_l_gfs_data_stat_bounce_t *   bounce_info;
    globus_gfs_stat_info_t *            stat_info;
    globus_result_t                     result;
    globus_gfs_stat_t *                 stat_array;
    globus_gfs_stat_t                   stat_temp;
    int                                 stat_count;
    int                                 file_count;
    int                                 i;
    int                                 dirlen;
    int                                 baselen;
    
    GlobusGFSName(globus_l_gfs_data_list_stat_cb);
    GlobusGFSDebugEnter();

    op = (globus_gfs_operation_t) user_arg;

    globus_l_gfs_data_alive(op->session_handle);

    stat_info = (globus_gfs_stat_info_t *) op->stat_wrapper;

    if(reply->result != GLOBUS_SUCCESS)
    {
        if (!stat_info->include_path_stat || reply->info.stat.stat_count < 1)
        {
            /** MLSD or top-level error */
        result = reply->result;
        goto error;
    }
        if (!(op->traversal_options & GLOBUS_GFS_TRAVERSAL_CONTINUE)) 
        {
            /** MLSR failure.  We want to write out the information for
             *  this directory and then fail. */
            op->delayed_error = reply->result;
        }
    }

    stat_array = reply->info.stat.stat_array;
    stat_count = reply->info.stat.stat_count;
    file_count = stat_count;
    
    if(op->list_depth != 0)
    {
        /** Skip the first entry if we are including the directory itself in the listing */
        int base_entry = 0;
           
        /** Correct for path which pointed to a single file */     
        if(stat_count == 1 && op->current_path && op->current_path->subpath)
        {
            int plen = strlen(op->current_path->subpath);
            int nlen = strlen(stat_array[0].name);
            if(plen > nlen && strcmp(&op->current_path->subpath[plen-nlen], stat_array[0].name) == 0)
            {
                op->current_path->subpath[plen-nlen-1] = '\0';
            }    
        }

        if (stat_info->include_path_stat && stat_count > 0 && S_ISDIR(stat_array[0].mode))
        {
            if (op->current_path) 
            {
                stat_array[0].name[0] = '\0';
            }
            else
            {
                stat_array[0].name[0] = '.';
                stat_array[0].name[1] = '\0';
            }
            ++base_entry;
        }
        
        /** Move unwanted directory entries to the end and add them to the path */
        for (i = base_entry; i < file_count;)
        {
            if (S_ISDIR(stat_array[i].mode) && 
                (stat_info->include_path_stat ||
                    (stat_array[i].name[0] == '.' &&
                     (stat_array[i].name[1] == '\0' || (stat_array[i].name[1] == '.' && stat_array[i].name[2] == '\0')))))
            {
                /** Swap with item at end */
                /* XXX overlapping copy here */
                memcpy(&stat_temp, &stat_array[i], sizeof(globus_gfs_stat_t));
                memcpy(&stat_array[i], &stat_array[file_count-1], sizeof(globus_gfs_stat_t));
                memcpy(&stat_array[file_count-1], &stat_temp, sizeof(globus_gfs_stat_t));
                /** Decrement effective count */
                --file_count;    
            }
            else
            {
                ++i;
            }   
        }
        
        dirlen = strlen(stat_info->pathname);
        baselen = op->current_path ? 
            (strlen(op->current_path->pathname) - strlen(op->current_path->subpath)) : 
            (dirlen + 1);
        
        /** Add directories, minus the current (.) and parent (..), to the list
         *  of paths to be explored.  Paths should be added at the end of the list. */
        for (i = base_entry; i < stat_count; ++i)
        {
            globus_l_gfs_data_path_list_t* newpath = NULL;
            
            /** If we are to follow symbolic links, create a new traversal path for each */
            if ((op->traversal_options & GLOBUS_GFS_TRAVERSAL_FOLLOW_SYMLINKS) && 
                stat_array[i].symlink_target &&
                stat_array[i].error != GLOBUS_GRIDFTP_SERVER_CONTROL_STAT_INVALIDLINK) 
            {
                newpath = (globus_l_gfs_data_path_list_t*)globus_malloc(sizeof(globus_l_gfs_data_path_list_t));
                if (newpath)
                {
                    newpath->pathname = globus_libc_strdup(stat_array[i].symlink_target);
                    newpath->subpath = newpath->pathname;
                }
                else
                {
                    goto error;
                }                  
            } 
            /** Create new traversal paths for subdirectories (ignore . and ..) */
            else if (S_ISDIR(stat_array[i].mode) &&
                (stat_array[i].name[0] != '.' || 
                 (stat_array[i].name[1] != '\0' && (stat_array[i].name[1] != '.' || stat_array[i].name[2] != '\0'))))
            {
                newpath = (globus_l_gfs_data_path_list_t*)globus_malloc(sizeof(globus_l_gfs_data_path_list_t));
                if (newpath)
                {
                    newpath->pathname = (char *)globus_malloc(dirlen + strlen(stat_array[i].name) + 2);                    
                    sprintf(newpath->pathname, "%s/%s", stat_info->pathname, stat_array[i].name);
                    newpath->subpath = &newpath->pathname[baselen]; 
                }
                else
                {
                    goto error;
                }
            }

            if (newpath) 
            {
                /*
                 * This section does cycle and duplicate path detection.  We only need to do
                 * this when symbolic link traversal is on (shouldn't happen otherwise, unless
                 * someone's done something nasty with hard links).
                 * 
                 * We check symbolic link targets against a set of "root paths", which are either
                 * the base root the MLSR was started with, or other link targets.  If a root is 
                 * entirely contained in a target, we'll already catch it, so no need to add it.
                 * 
                 * If the target is entirely contained in a root, then we have a cycle, and we 
                 * mark the path as such.  
                 * 
                 * When expanding directories, if the parent is marked as having a cycle, we
                 * check the subdirectory.  If the subdirectory still has a cycle, it is marked
                 * as such--however, if it doesn't then we can avoid the cycle check for future
                 * directory expansions below that point.
                 *
                 * Symbolic link targets which need to be explored AND are not links to files
                 * are added to the root path list as well as the traveral queue.
                 */
                if (op->traversal_options & GLOBUS_GFS_TRAVERSAL_FOLLOW_SYMLINKS)
                {
                    globus_l_gfs_data_path_list_t* iter; 
                    int newpathlen = strlen(newpath->pathname);               
                                    
                    newpath->has_cycle = GLOBUS_FALSE;
                    
                    /** For symbolic links, validate the path--we don't want any duplicated paths */
                    if (stat_array[i].symlink_target)
                    {
                        for (iter = op->root_paths; iter && newpath; iter = iter->next)
                        {
                            int ipathlen = strlen(iter->pathname);
                            if (strncmp(iter->pathname, newpath->pathname, ipathlen) == 0 &&
                                (newpath->pathname[ipathlen] == '/' || newpath->pathname[ipathlen] == '\0'))
                            {
                                /** No good, we will/have already explored this path */
                                globus_free(newpath->pathname);
                                globus_free(newpath);
                                newpath = NULL;
                            }
                            else if(strncmp(iter->pathname, newpath->pathname, newpathlen) == 0 &&
                                (iter->pathname[newpathlen] == '/' || iter->pathname[newpathlen] == '\0'))
                            {
                                /** This target has a cycle */
                                newpath->has_cycle = GLOBUS_TRUE;                            
                            }
                        }
                    }
                    else if (op->current_path && op->current_path->has_cycle)
                    {
                        /** For directory expansion of a "dangerous" item, we need to check if we've hit a cycle */
                        for (iter = op->root_paths; iter && newpath; iter = iter->next)
                        {
                            if (strcmp(iter->pathname, newpath->pathname) == 0)
                            {
                                /** No good, we have already explored this path */
                                globus_free(newpath->pathname);
                                globus_free(newpath);
                                newpath = NULL;
                            } 
                            else if(strncmp(iter->pathname, newpath->pathname, newpathlen) == 0 &&
                                (iter->pathname[newpathlen] == '/' || iter->pathname[newpathlen] == '\0'))
                            {
                                newpath->has_cycle = GLOBUS_TRUE;
                            }
                        }
                    }
                }
                
                /** Add the path to the traversal list */
                if (newpath) 
                {
                    if (stat_array[i].symlink_target)
                    {
                        globus_l_gfs_data_path_list_t** ppiter;
                        
                        /** Append symlink targets to the end of the traversal queue */
                        newpath->next = NULL;                 
                        for (ppiter = &op->path_list; *ppiter; ppiter = &((*ppiter)->next)) ;
                        *ppiter = newpath;  
                         
                        /** Symbolic links to directories should be added to the root list. */
                        if (S_ISDIR(stat_array[i].link_mode))
                        {
                            globus_l_gfs_data_path_list_t* newroot = NULL;
                            newroot = (globus_l_gfs_data_path_list_t*)globus_malloc(sizeof(globus_l_gfs_data_path_list_t));
                            if (newroot)
                            {
                                newroot->pathname = globus_libc_strdup(newpath->pathname);
                                newroot->subpath = NULL;
                                newroot->next = op->root_paths;
                                op->root_paths = newroot; 
                            } 
                            else
                            {
                                goto error;
                            }
                        }
                    } 
                    else 
                    {
                        /** Prepend directory expansion targets to the head of the queue */
                        newpath->next = op->path_list;
                        op->path_list = newpath;
                    }
                }               
            }
        }
    }
    
    globus_mutex_lock(&op->stat_lock);
    if(!op->begin_called)
    {
        op->begin_called = GLOBUS_TRUE;
        globus_gridftp_server_begin_transfer(op, 0, NULL);
    }
    
    list_buffer = NULL;
    buffer_len = 0;
    
    if(file_count > 0) 
    { 
        result = globus_gridftp_server_control_list_buffer_alloc(
                op->list_type,
                op->uid,
                    (op->current_path ? op->current_path->subpath : NULL),
                    stat_array,
                    file_count,
                &list_buffer,
                &buffer_len);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
               "globus_gridftp_server_control_list_buffer_alloc", result);
            goto error;
        }
    } 
    else if(op->delayed_error != GLOBUS_SUCCESS || !globus_l_gfs_data_request_next_path(op))
    {
        list_buffer = (globus_byte_t *) globus_libc_strdup("\r");
        buffer_len = 1;
    }

    if(buffer_len > 0)
    {
        bounce_info = (globus_l_gfs_data_stat_bounce_t *)
            globus_calloc(sizeof(globus_l_gfs_data_stat_bounce_t), 1);
        if(bounce_info == NULL)
        {
            result = GlobusGFSErrorMemory("bounce_info");
            goto error;
        }
        
        if(reply->code != 100)
        {
            bounce_info->final_stat = GLOBUS_TRUE;
        }
    
        result = globus_gridftp_server_register_write(
            op,
            list_buffer,
            buffer_len,
            op->list_buffer_offset,
            -1,
            globus_l_gfs_data_list_write_cb,
            bounce_info);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_gridftp_server_register_write", result);
            free(bounce_info);
            globus_mutex_unlock(&op->stat_lock);
            goto error;
        }
        op->list_buffer_offset += buffer_len;
    } 
    
    globus_mutex_unlock(&op->stat_lock);

    GlobusGFSDebugExit();
    return;

error:
    globus_l_gfs_data_list_done(op, result);
    GlobusGFSDebugExitWithError();
}

void
globus_gridftp_server_finished_stat_custom_list(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    globus_byte_t *                     list_response,
    globus_size_t                       list_response_len,
    globus_bool_t                       free_buffer)
{
    globus_l_gfs_data_stat_bounce_t *   bounce_info;
    globus_l_gfs_data_operation_t *     data_op;
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    GlobusGFSName(globus_gridftp_server_finished_stat_custom_list);
    GlobusGFSDebugEnter();

    data_op = (globus_l_gfs_data_operation_t *) op->user_arg;

    globus_l_gfs_data_alive(op->session_handle);

    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    bounce_info = (globus_l_gfs_data_stat_bounce_t *)
        globus_calloc(sizeof(globus_l_gfs_data_stat_bounce_t), 1);
    if(bounce_info == NULL)
    {
        result = GlobusGFSErrorMemory("bounce_info");
        goto error;
    }
    
    bounce_info->custom_list = GLOBUS_TRUE;
    bounce_info->free_buffer = free_buffer;
    if(free_buffer)
    {
        bounce_info->list_response = list_response;
    }
    else
    {
        bounce_info->list_response = globus_malloc(list_response_len);
        memcpy(bounce_info->list_response, list_response, list_response_len);
    }

    globus_gridftp_server_begin_transfer(data_op, 0, NULL);

    result = globus_gridftp_server_register_write(
        data_op,
        bounce_info->list_response,
        list_response_len,
        0,
        -1,
        globus_l_gfs_data_list_write_cb,
        bounce_info);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_gridftp_server_register_write", result);
        goto error;
    }

    globus_mutex_lock(&op->session_handle->mutex);
    {
        GFSDataOpDec(op, destroy_op, destroy_session);
    }
    globus_mutex_unlock(&op->session_handle->mutex);
    globus_assert(destroy_op);
    globus_l_gfs_data_operation_destroy(op);

    globus_free(data_op->stat_wrapper);

    GlobusGFSDebugExit();
    return;

error:
    globus_gridftp_server_finished_transfer(data_op, result);
    GlobusGFSDebugExitWithError();
}

void
globus_i_gfs_data_request_list(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_transfer_info_t *        list_info,
    globus_i_gfs_data_callback_t        cb,
    globus_i_gfs_data_event_callback_t  event_cb,
    void *                              user_arg)
{
    int                                 rc;
    globus_result_t                     res;
    globus_l_gfs_data_operation_t *     data_op;
    globus_result_t                     result;
    globus_l_gfs_data_handle_t *        data_handle;
    globus_gfs_stat_info_t *            stat_info;
    globus_l_gfs_data_session_t *       session_handle;
    globus_gfs_acl_object_desc_t        object;
    GlobusGFSName(globus_i_gfs_data_request_list);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *) session_arg;
    globus_l_gfs_data_reset_watchdog(session_handle, NULL);

    data_handle = (globus_l_gfs_data_handle_t *)
        globus_handle_table_lookup(
            &session_handle->handle_table, (intptr_t) list_info->data_arg);
    if(data_handle == NULL)
    {
        result = GlobusGFSErrorData(_FSSL("Data handle not found",NULL));
        goto error_handle;
    }
    if(!data_handle->is_mine)
    {
        list_info->data_arg = data_handle->remote_data_arg;
    }

    result = globus_l_gfs_data_operation_init(&data_op, session_handle);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_l_gfs_data_operation_init", result);
        goto error_op;
    }
    /* globus_assert(data_handle->outstanding_op == NULL); */
    data_handle->outstanding_op = data_op;

    data_op->ipc_handle = ipc_handle;
    data_op->session_handle = session_handle;
    data_op->type = GLOBUS_L_GFS_DATA_INFO_TYPE_LIST;
    data_op->info_struct = list_info;

    data_op->id = id;
    data_op->state = GLOBUS_L_GFS_DATA_REQUESTING;
    data_op->writing = GLOBUS_TRUE;
    data_op->data_handle = data_handle;
    data_op->data_arg = list_info->data_arg;
    data_op->list_type = strdup(list_info->list_type);
    data_op->list_depth = list_info->list_depth;
    data_op->traversal_options = list_info->traversal_options;
    data_op->delayed_error = GLOBUS_SUCCESS;
    data_op->uid = getuid();
    /* XXX */
    data_op->callback = cb;
    data_op->event_callback = event_cb;
    data_op->user_arg = user_arg;
    data_op->node_ndx = list_info->node_ndx;
    data_op->write_stripe = 0;
    data_op->stripe_chunk = list_info->node_ndx;
    data_op->node_count = list_info->node_count;
    data_op->stripe_count = list_info->stripe_count;
    data_op->eof_count = (int *)
        globus_calloc(1, data_op->stripe_count * sizeof(int));

    /* events and disconnects cannot happen while i am in this
        function */
    globus_assert(data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_VALID);
/*
        || data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_TE_VALID);
*/
    data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_INUSE;

    if(!data_handle->is_mine)
    {
        data_op->op_info_id = globus_l_gfs_op_info_ctr++;
        if(!list_info->op_info)
        {
            list_info->op_info =  globus_calloc(1, sizeof(globus_i_gfs_op_info_t));
        }
        list_info->op_info->id = data_op->op_info_id;
    }

    if(session_handle->dsi->list_func != NULL)
    {
        object.name = list_info->pathname;
        rc = globus_gfs_acl_authorize(
            &session_handle->acl_handle,
            GFS_ACL_ACTION_LOOKUP,
            &object,
            &res,
            globus_l_gfs_authorize_cb,
            data_op);
        if(rc == GLOBUS_GFS_ACL_COMPLETE)
        {
            globus_l_gfs_authorize_cb(
                &object, GFS_ACL_ACTION_LOOKUP, data_op, res);
        }
    }
    else
    {
        stat_info = (globus_gfs_stat_info_t *)
            globus_calloc(1, sizeof(globus_gfs_stat_info_t));

        stat_info->pathname = list_info->pathname;
        stat_info->file_only = GLOBUS_FALSE;
        stat_info->use_symlink_info = data_op->list_depth != 0;
        stat_info->include_path_stat = data_op->list_depth != 0;        

        data_op->info_struct = list_info;
        data_op->stat_wrapper = stat_info;

        if(data_op->list_depth != 0)
        {
            int len;

            data_op->root_paths = (globus_l_gfs_data_path_list_t*)globus_malloc(sizeof(globus_l_gfs_data_path_list_t));
            if (!data_op->root_paths)
            {
                goto error_op;
            }

            /**
            *** Trim any trailing '/'
            **/            
            len = strlen(stat_info->pathname);
            if(len > 0 && stat_info->pathname[len - 1] == '/')
            {
                stat_info->pathname[len - 1] = '\0';
            }

            data_op->root_paths->pathname = globus_libc_strdup(stat_info->pathname);
            data_op->root_paths->next = NULL;
        }

        globus_i_gfs_data_request_stat(
            ipc_handle,
            session_handle,
            id,
            stat_info,
            globus_l_gfs_data_list_stat_cb,
            data_op);
    }

    GlobusGFSDebugExit();
    return;

error_handle:
error_op:
    globus_gridftp_server_finished_transfer(data_op, result);
    GlobusGFSDebugExitWithError();
}

/***********************************************************************
 *  finished transfer callbacks
 *  ---------------------------
 **********************************************************************/
static
void
globus_l_gfs_data_finish_connected(
    globus_l_gfs_data_operation_t *     op)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    GlobusGFSName(globus_l_gfs_data_finish_connected);
    GlobusGFSDebugEnter();

    if(op->data_handle->is_mine)
    {
        if(op->writing)
        {
            if(op->event_callback == NULL || op->retr_markers)
            {
                /* send bytes transferred as a writing backend, to sync up
                 * frontend for logging
                 */
                globus_gfs_event_info_t        event_reply;
                memset(&event_reply, '\0', sizeof(globus_gfs_event_info_t));
                event_reply.id = op->id;
                event_reply.recvd_bytes = op->recvd_bytes;
                event_reply.type = GLOBUS_GFS_EVENT_BYTES_RECVD;

                if(op->event_callback == NULL)
                {
                    globus_gfs_ipc_reply_event(
                        op->ipc_handle,
                        &event_reply);
                }
                else if(op->retr_markers)
                {
                    op->event_callback(
                        &event_reply,
                        op->user_arg);
                }
            }

            if(op->node_ndx != 0 ||
                op->stripe_count == 1 ||
                op->eof_ready)
            {
                result = globus_ftp_control_data_write(
                    &op->data_handle->data_channel,
                    (globus_byte_t *) "",
                    0,
                    0,
                    GLOBUS_TRUE,
                    globus_l_gfs_data_write_eof_cb,
                    op);
                if(result != GLOBUS_SUCCESS)
                {
                    globus_gfs_log_result(
                        GLOBUS_GFS_LOG_WARN, "write_eof error", result);
                    op->cached_res = result;
                    globus_callback_register_oneshot(
                        NULL,
                        NULL,
                        globus_l_gfs_data_end_transfer_kickout,
                        op);
                }
            }
        }
        else
        {
            globus_callback_register_oneshot(
                NULL,
                NULL,
                globus_l_gfs_data_end_read_kickout,
                op);
        }
    }
    else
    {
        if(op->data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_VALID)
        {
            op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_TE_VALID;
        }
        globus_callback_register_oneshot(
            NULL,
            NULL,
            globus_l_gfs_data_end_transfer_kickout,
            op);
    }

    GlobusGFSDebugExit();
}


static
void
globus_l_gfs_data_begin_cb(
    void *                              callback_arg,
    struct globus_ftp_control_handle_s * handle,
    unsigned int                        stripe_ndx,
    globus_bool_t                       reused,
    globus_object_t *                   error)
{
    globus_result_t                     res;
    int                                 rcvbuf;
    int                                 sndbuf;
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    globus_bool_t                       dec_op = GLOBUS_FALSE;
    globus_bool_t                       connect_event = GLOBUS_FALSE;
    globus_bool_t                       finish = GLOBUS_FALSE;
    globus_gfs_event_info_t        event_reply;
    globus_gfs_event_info_t             event_info;
    globus_l_gfs_data_operation_t *     op;
    globus_l_gfs_data_handle_state_t    last_state;
    void *                              remote_data_arg = NULL;
    GlobusGFSName(globus_l_gfs_data_begin_cb);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) callback_arg;

    globus_mutex_lock(&op->session_handle->mutex);
    {
        last_state = op->state;
        switch(op->state)
        {
            case GLOBUS_L_GFS_DATA_CONNECTING:
                op->stripe_connections_pending--;
                globus_assert(op->ref > 0);

                if(error != NULL)
                {
                    /* something wrong, start the abort process */
                    op->cached_res =
                        globus_error_put(globus_object_copy(error));
                    op->connect_failed = GLOBUS_TRUE;
                    goto err_lock;
                }
                if(!op->stripe_connections_pending)
                {
                    dec_op = GLOBUS_TRUE;
                    /* everything is well, send the begin event */
                    op->state = GLOBUS_L_GFS_DATA_CONNECT_CB;
                    connect_event = GLOBUS_TRUE;
                }
                break;
            /* this happens when a finished comes right after a begin,
                usually because 0 bytes were written.  we need to send
                the transfer_connected event and then finish. */
            case GLOBUS_L_GFS_DATA_FINISH_WITH_ERROR:
            case GLOBUS_L_GFS_DATA_FINISH:
                op->stripe_connections_pending--;
                if(!op->stripe_connections_pending)
                {
                    dec_op = GLOBUS_TRUE;
                    /* everything is well, send the begin event */
                    connect_event = GLOBUS_TRUE;

                    /* if the fished was delayed, and was not started
                        via abort process, the start the finish after
                        the unlock */
                    finish = op->finished_delayed;
                }

                globus_assert(op->ref > 1);

                break;

            /* this happens when a transfer is aborted before a connection
                is esstablished.  it could be in this state
                depending on how quickly the abort process happens.  */
            case GLOBUS_L_GFS_DATA_ABORTING:
            case GLOBUS_L_GFS_DATA_ABORT_CLOSING:
                dec_op = GLOBUS_TRUE;
                globus_assert(op->ref > 1);
                break;
                /* we need to dec the reference count and clean up if needed.
                also we ignore the error value here, it is likely canceled */
            case GLOBUS_L_GFS_DATA_COMPLETING:
                dec_op = GLOBUS_TRUE;
                if(op->ref == 1)
                {
                    op->state = GLOBUS_L_GFS_DATA_COMPLETE;
                }
                break;

            case GLOBUS_L_GFS_DATA_COMPLETE:
            case GLOBUS_L_GFS_DATA_CONNECTED:
            case GLOBUS_L_GFS_DATA_CONNECT_CB:
            case GLOBUS_L_GFS_DATA_REQUESTING:
            default:
                globus_assert(0 && "not possible state.  memory corruption");
                break;
        }
        if(connect_event && op->data_handle->is_mine)
        {
            res = globus_ftp_control_data_get_socket_buf(
                &op->data_handle->data_channel,
                &rcvbuf,
                &sndbuf);
            if(res != GLOBUS_SUCCESS)
            {
                char * tmp_err_str =
                    globus_error_print_friendly(globus_error_peek(res));
                globus_gfs_log_message(
                    GLOBUS_GFS_LOG_WARN,
                    "Request to get socket buffer size failed: %s\n",
                    tmp_err_str);
                free(tmp_err_str);
            }
            else
            {
                if(op->writing)
                {
                    if(op->data_handle->info.tcp_bufsize &&
                        rcvbuf != op->data_handle->info.tcp_bufsize)
                    {
                        globus_gfs_log_message(
                            GLOBUS_GFS_LOG_WARN,
                            "RECV buffer size may not be properly set.  "
                            "Requested size = %d, actualy size = %d\n",
                            op->data_handle->info.tcp_bufsize, rcvbuf);
                    }
                    op->data_handle->info.tcp_bufsize = rcvbuf;
                }
                else
                {
                    if(op->data_handle->info.tcp_bufsize &&
                        sndbuf != op->data_handle->info.tcp_bufsize)
                    {
                        globus_gfs_log_message(
                            GLOBUS_GFS_LOG_WARN,
                            "SEND buffer size may not be properly set.  "
                            "Requested size = %d, actualy size = %d\n",
                            op->data_handle->info.tcp_bufsize, sndbuf);
                    }
                    op->data_handle->info.tcp_bufsize = sndbuf;
                }
            }
            
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_INFO,
                "Starting to transfer \"%s\".\n",
                    ((globus_gfs_transfer_info_t *) op->info_struct)->pathname);
            globus_gfs_log_event(
                GLOBUS_GFS_LOG_INFO,
                GLOBUS_GFS_LOG_EVENT_START,
                "transfer",
                0,
                "file=\"%s\"",
                ((globus_gfs_transfer_info_t *) op->info_struct)->pathname);

        }
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    if(connect_event && op->data_handle->is_mine)
    {
        globus_ftp_control_host_port_t  remote_addr;
        int                             remote_addr_count = 1;

        memset(&remote_addr, '\0', sizeof(globus_ftp_control_host_port_t));
        globus_ftp_control_data_get_remote_hosts(
              &op->data_handle->data_channel,
              &remote_addr,
              &remote_addr_count);
        op->remote_ip = globus_libc_ints_to_contact_string(
            remote_addr.host, remote_addr.hostlen, 0);

        memset(&event_reply, '\0', sizeof(globus_gfs_event_info_t));
        event_reply.type = GLOBUS_GFS_EVENT_TRANSFER_CONNECTED;
        event_reply.id = op->id;
        event_reply.event_arg = op;
        
        if(op->event_callback != NULL)
        {
            op->event_callback(&event_reply, op->user_arg);
        }
        else
        {
            event_reply.op_info = globus_calloc(1, sizeof(globus_i_gfs_op_info_t));
            event_reply.op_info->remote_ip = globus_libc_strdup(op->remote_ip);

            globus_gfs_ipc_reply_event(op->ipc_handle, &event_reply);
            
            if(event_reply.op_info->remote_ip)
            {
                globus_free(event_reply.op_info->remote_ip);
            }
            globus_free(event_reply.op_info);
        }
        
        if((!op->writing || op->retr_markers)
            && (op->data_handle->info.mode == 'E' || 
            globus_i_gfs_config_bool("always_send_markers")))
        {
            /* send first 0 byte marker */
            event_reply.type = GLOBUS_GFS_EVENT_BYTES_RECVD;
            event_reply.recvd_bytes = 0;
            event_reply.node_ndx = op->node_ndx;
            if(op->event_callback != NULL)
            {
                op->event_callback(&event_reply, op->user_arg);
            }
            else
            {
                globus_gfs_ipc_reply_event(op->ipc_handle, &event_reply);
            }
        }
    }
    globus_mutex_lock(&op->session_handle->mutex);
    {
        if(op->state == GLOBUS_L_GFS_DATA_CONNECT_CB)
        {
            op->state = GLOBUS_L_GFS_DATA_CONNECTED;
        }
        finish = op->finished_delayed;
        if(dec_op)
        {
            /* must delay decrement otherwise the callback could result in
                destroing the op once the lock is released */
            GFSDataOpDec(op, destroy_op, destroy_session);

            if(destroy_op)
            {
                globus_assert(op->data_handle != NULL);
                remote_data_arg = globus_l_gfs_data_check(
                    op->session_handle, op->data_handle);
            }
        }
        if(finish)
        {
            globus_l_gfs_data_finish_connected(op);
        }
    }
    globus_mutex_unlock(&op->session_handle->mutex);


    if(destroy_op)
    {
        /* pass the complete event */
        if(op->session_handle->dsi->trev_func &&
            op->event_mask & GLOBUS_GFS_EVENT_TRANSFER_COMPLETE)
        {
            /* AAAA */
            event_info.type = GLOBUS_GFS_EVENT_TRANSFER_COMPLETE;
            event_info.event_arg = op->event_arg;
            op->session_handle->dsi->trev_func(
                &event_info,
                op->session_handle->session_arg);
        }
            globus_mutex_lock(&op->session_handle->mutex);
            {
                remote_data_arg = globus_l_gfs_data_post_transfer_event_cb(
                    op->session_handle, op->data_handle);
            }
            globus_mutex_unlock(&op->session_handle->mutex);

        globus_l_gfs_data_fire_cb(op, remote_data_arg, destroy_session);
        /* destroy the op */
        globus_l_gfs_data_operation_destroy(op);
    }

    GlobusGFSDebugExit();
    return;

  err_lock:
    /* start abort process */
    globus_l_gfs_data_start_abort(op);
    globus_mutex_unlock(&op->session_handle->mutex);
    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_data_begin_kickout(
    void *                              callback_arg)
{
    globus_l_gfs_data_operation_t *     op;
    GlobusGFSName(globus_l_gfs_data_begin_kickout);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) callback_arg;

    globus_l_gfs_data_begin_cb(
        callback_arg,
        &op->data_handle->data_channel,
        0,
        GLOBUS_TRUE,
        NULL);

    GlobusGFSDebugExit();
}

static
char *
globus_l_gfs_data_get_nl_msg(
    globus_l_gfs_data_operation_t *     op)
{
    globus_xio_driver_list_ent_t *      ent;
    globus_result_t                     result;
    char *                              tmp_ptr;
    char *                              uuid;
    char *                              msg_out;
    globus_xio_attr_t                   xio_attr;

    /* we assume the user had the same uuid on nl for disk and
        net.  if they did not then the results they get back will
        be just for the net stack */

    if(op->session_handle->net_stack_list == NULL)
    {
        return NULL;
    }

    ent = globus_xio_driver_list_find_driver(
        op->session_handle->disk_stack_list,
        "netlogger");
    if(ent == NULL || ent->opts == NULL)
    {
        return NULL;
    }

    tmp_ptr = strstr(ent->opts, "uuid=");
    if(tmp_ptr == NULL)
    {
        return NULL;
    }

    uuid = strdup(tmp_ptr);
    tmp_ptr = strchr(uuid, ';');
    if(tmp_ptr != NULL)
    {
        *tmp_ptr = '\0';
    }

    /* fake the attr cntl just to get into NL process space */
    globus_xio_attr_init(&xio_attr);
    result = globus_xio_attr_cntl(
        xio_attr,
        ent->driver,
        1024,
        uuid,
        &msg_out);
    globus_xio_attr_destroy(xio_attr);
    globus_free(uuid);
    if(result != GLOBUS_SUCCESS)
    {
        return NULL;
    }

    return msg_out;
}


static
void
globus_l_gfs_data_end_transfer_kickout(
    void *                              user_arg)
{
    globus_bool_t                       free_data = GLOBUS_FALSE;
    globus_l_gfs_data_handle_t *        data_handle;
    globus_l_gfs_data_operation_t *     op;
    globus_gfs_event_info_t        event_reply;
    globus_gfs_finished_info_t              reply;
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    globus_bool_t                       disconnect = GLOBUS_FALSE;
    void *                              remote_data_arg = NULL;
    globus_gfs_event_info_t             event_info;
    globus_result_t                     result = GLOBUS_SUCCESS;
    char *                              retransmit_str = NULL;
    GlobusGFSName(globus_l_gfs_data_end_transfer_kickout);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) user_arg;

    memset(&reply, '\0', sizeof(globus_gfs_finished_info_t));


    if(op->cached_res == GLOBUS_SUCCESS && 
        op->writing && op->data_handle->http_handle)
    {
        globus_xio_data_descriptor_t        descriptor;
        globus_xio_handle_t                 handle = op->data_handle->http_handle;
        globus_byte_t                       buffer[1];
        int                                 status_code;
        char *                              reason_phrase;
        char *                              err_str;
        char *                              header_str;
        globus_hashtable_t                  header_table = NULL;
        globus_bool_t                       eof = 0;

        
        globus_xio_handle_cntl(
            handle,
            op->session_handle->http_driver,
            GLOBUS_XIO_HTTP_HANDLE_SET_END_OF_ENTITY);
            
        result = globus_xio_data_descriptor_init(&descriptor, handle);
        globus_assert(result == GLOBUS_SUCCESS);

        /* read response, no data */
        result = globus_xio_read(
                handle,
                buffer,
                0,
                0,
                NULL,
                descriptor);
        eof = globus_xio_error_is_eof(result);
        if(eof)
        {
            result = GLOBUS_SUCCESS;
        }
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed("While reading HTTP response, connection", result);
            goto response_exit;
        }
        /* check response */
        result = globus_xio_data_descriptor_cntl(
                descriptor,
                op->session_handle->http_driver,
                GLOBUS_XIO_HTTP_GET_RESPONSE,
                &status_code,
                &reason_phrase,
                NULL,
                &header_table);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed("Reading HTTP Response", result);
        }
        else if(status_code > 299)
        {
            globus_byte_t *                 body_buffer;
            globus_size_t                   buflen = 64*1024;
            globus_size_t                   body_nbytes;
            globus_size_t                   total_nbytes = 0;
            globus_size_t                   waitfor = 0;

            body_buffer = malloc(buflen+1);
            do
            {   
                body_nbytes = 0;
                result = globus_xio_read(
                    handle,
                    body_buffer+total_nbytes,
                    buflen-total_nbytes,
                    waitfor,
                    &body_nbytes,
                    NULL);
                eof = globus_xio_error_is_eof(result);
                if(eof)
                {
                    result = GLOBUS_SUCCESS;
                }
                if(result != GLOBUS_SUCCESS)
                {
                    result = GLOBUS_SUCCESS;
                    eof = GLOBUS_TRUE;
                }
                total_nbytes += body_nbytes;
                if(body_nbytes == 0 || total_nbytes == buflen)
                {
                    eof = GLOBUS_TRUE;
                }
                else
                {
                    waitfor = GLOBUS_MIN(1024, buflen-total_nbytes);
                }           
            } while(!eof);
            
            body_buffer[total_nbytes] = '\0';
            globus_i_gfs_data_http_print_response(
                status_code, &header_table, body_buffer, &header_str);
                
            err_str = globus_common_create_string(
                "HTTP PUT failed with \"%03d %s\"\n%s",
                status_code,
                reason_phrase,
                header_str);

            result = GlobusGFSErrorGeneric(err_str);
                
            globus_free(err_str);
            globus_free(header_str);
            globus_free(body_buffer);
        }
        else
        {
            globus_byte_t *                 body_buffer;
            globus_size_t                   buflen = 64*1024;
            globus_size_t                   body_nbytes;
            globus_size_t                   total_nbytes = 0;
            globus_size_t                   waitfor = 0;

            body_buffer = malloc(buflen+1);
            eof = 0;
            while(!eof)
            {
                body_nbytes = 0;
                result = globus_xio_read(
                    handle,
                    body_buffer+total_nbytes,
                    buflen-total_nbytes,
                    waitfor,
                    &body_nbytes,
                    NULL);
                eof = globus_xio_error_is_eof(result);
                if(eof)
                {
                    result = GLOBUS_SUCCESS;
                }
                if(result != GLOBUS_SUCCESS)
                {
                    result = GLOBUS_SUCCESS;
                    eof = GLOBUS_TRUE;
                }
                total_nbytes += body_nbytes;
    
                if(body_nbytes == 0 || total_nbytes == buflen)
                {
                    eof = GLOBUS_TRUE;
                }
                else
                {
                    waitfor = GLOBUS_MIN(1024, buflen-total_nbytes);
                }           
            }
            result = GLOBUS_SUCCESS;
            globus_free(body_buffer);
            globus_i_gfs_data_http_print_response(
                status_code, &header_table, NULL, &op->data_handle->http_response_str);
        }
        
    }
response_exit:
    if(op->cached_res == GLOBUS_SUCCESS)
    {
        op->cached_res = result;
    }
    /* deal with the data handle */
    globus_mutex_lock(&op->session_handle->mutex);
    {
        globus_assert(op->data_handle != NULL);
        switch(op->data_handle->state)
        {
            case GLOBUS_L_GFS_DATA_HANDLE_TE_VALID:
                break;

            case GLOBUS_L_GFS_DATA_HANDLE_INUSE:
                op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_TE_VALID;
                break;

            case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED:
                disconnect = GLOBUS_TRUE;
                break;
            case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_AND_DESTROYED:
                disconnect = GLOBUS_TRUE;
                break;
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSING:
                disconnect = GLOBUS_TRUE;
                break;

            case GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED:
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSED:
            case GLOBUS_L_GFS_DATA_HANDLE_VALID:
            default:
                globus_assert(0 && "possible memory corruption");
                break;
        }
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    if(!op->data_handle->is_mine)
    {
        char *                          remote_ip = NULL;
        remote_ip = globus_i_gfs_ipc_query_op_info(op->op_info_id);
        if(remote_ip)
        {
            op->remote_ip = remote_ip;
        }
    }

    if(op->cached_res == GLOBUS_SUCCESS)
    {
        char *                          msg;
        char *                          type;
        globus_gfs_transfer_info_t *    info;

        info = (globus_gfs_transfer_info_t *) op->info_struct;

        if(op->writing)
        {
            if(info->list_type)
            {
                if(strncmp(info->list_type, "LIST:", 5) == 0)
                {
                    type = "LIST";
                }
                else if(strncmp(info->list_type, "NLST:", 5) == 0)
                {
                    type = "NLST";
                }
                else
                {
                    type = "MLSD";
                }
            }
            else if(info->module_name || info->partial_offset != 0 ||
                info->partial_length != -1)
            {
                type = "ERET";
            }
            else
            {
                type = "RETR";
            }
        }
        else
        {
            if(info->module_name || info->partial_offset != 0 ||
                 !info->truncate)
            {
                type = "ESTO";
            }
            else
            {
                type = "STOR";
            }
        }

        globus_gfs_log_message(
            GLOBUS_GFS_LOG_INFO,
            "Finished transferring \"%s\".\n",
                ((globus_gfs_transfer_info_t *) op->info_struct)->pathname);

        if(!op->data_handle->http_handle && op->data_handle->is_mine)
        {
            globus_ftp_control_data_get_retransmit_count(
                &op->data_handle->data_channel,
                &retransmit_str);
        }

        msg = globus_i_gfs_log_create_transfer_event_msg(
            op->node_count,
            op->data_handle->info.nstreams,
            op->remote_ip ? op->remote_ip : "0.0.0.0",
            op->data_handle->info.blocksize,
            op->data_handle->info.tcp_bufsize,
            info->pathname,
            op->bytes_transferred,
            type,
            op->session_handle->username,
            retransmit_str,
            op->session_handle->taskid);

        globus_gfs_log_event(
            GLOBUS_GFS_LOG_INFO,
            GLOBUS_GFS_LOG_EVENT_END,
            "transfer",
            0,
            "%s",
            msg);

        globus_free(msg);
    }
    else
    {
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_INFO,
            "Failure attempting to transfer \"%s\".\n",
                ((globus_gfs_transfer_info_t *) op->info_struct)->pathname);
        globus_gfs_log_result(
            GLOBUS_GFS_LOG_INFO,
            "Transfer failure",
            op->cached_res);

        globus_gfs_log_event(
            GLOBUS_GFS_LOG_INFO,
            GLOBUS_GFS_LOG_EVENT_ERROR,
            "transfer",
            op->cached_res,
            "file=\"%s\"",
            ((globus_gfs_transfer_info_t *) op->info_struct)->pathname);
    }
    if(disconnect && op->data_handle->is_mine)
    {
        memset(&event_reply, '\0', sizeof(globus_gfs_event_info_t));
        event_reply.id = op->id;
        event_reply.data_arg = op->data_arg;

        event_reply.type = GLOBUS_GFS_EVENT_DISCONNECTED;
        if(op->event_callback != NULL)
        {
            op->event_callback(
                &event_reply,
                op->user_arg);
        }
        else
        {
            globus_gfs_ipc_reply_event(
                op->ipc_handle,
                &event_reply);
        }
    }



    /* log transfer */
    if(op->node_ndx == 0 &&
        op->cached_res == GLOBUS_SUCCESS &&
        (globus_i_gfs_config_string("log_transfer") ||
        (!globus_l_gfs_data_is_remote_node &&
        !globus_i_gfs_config_bool("disable_usage_stats"))))
    {
        char *                          type;
        globus_gfs_transfer_info_t *    info;
        struct timeval                  end_timeval;

        info = (globus_gfs_transfer_info_t *) op->info_struct;

        if(op->writing)
        {
            if(info->list_type)
            {
                if(strncmp(info->list_type, "LIST:", 5) == 0)
                {
                    type = "LIST";
                }
                else if(strncmp(info->list_type, "NLST:", 5) == 0)
                {
                    type = "NLST";
                }
                else
                {
                    type = "MLSD";
                }
            }
            else if(info->module_name || info->partial_offset != 0 ||
                info->partial_length != -1)
            {
                type = "ERET";
            }
            else
            {
                type = "RETR";
            }
        }
        else
        {
            if(info->module_name || info->partial_offset != 0 ||
                 !info->truncate)
            {
                type = "ESTO";
            }
            else
            {
                type = "STOR";
            }
        }
        gettimeofday(&end_timeval, NULL);

        if(globus_i_gfs_config_string("log_transfer"))
        {
            globus_i_gfs_log_transfer(
                op->node_count,
                op->data_handle->info.nstreams,
                &op->start_timeval,
                &end_timeval,
                op->remote_ip ? op->remote_ip : "0.0.0.0",
                op->data_handle->info.blocksize,
                op->data_handle->info.tcp_bufsize,
                info->pathname,
                op->bytes_transferred,
                226,
                "/",
                type,
                op->session_handle->username,
                retransmit_str,
                op->session_handle->taskid);
        }
        if(!globus_l_gfs_data_is_remote_node &&
            !globus_i_gfs_config_string("disable_usage_stats"))
        {

            globus_i_gfs_log_usage_stats(
                &op->start_timeval,
                &end_timeval,
                op->node_count,
                op->data_handle->info.nstreams,
                op->data_handle->info.blocksize,
                op->data_handle->info.tcp_bufsize,
                op->bytes_transferred,
                226,
                type,
                info->pathname,
                op->remote_ip ? op->remote_ip : "0.0.0.0",
                op->session_handle->client_ip ?
                    op->session_handle->client_ip : "0.0.0.0",
                op->session_handle->username,
                op->session_handle->subject,
                op->session_handle->client_appname,
                op->session_handle->client_appver,
                op->session_handle->client_scheme, 
                op->session_handle->dsi == globus_l_gfs_dsi_hybrid ?
                    "remote" : globus_i_gfs_config_string("load_dsi_module"));
        }
    }
    if(retransmit_str)
    {
        globus_free(retransmit_str);
    }
    
    /* XXX sc process bytes transferred count */
    {
        char *                          names[5] =
            {"bytes", "kilobytes", "megabytes", "gigabytes", "terabytes"};
        char *                          str_transferred;
        globus_off_t                    tmp_bytes;
        double                          remainder = 0.0;
        int                             i = 0;

        globus_mutex_lock(&globus_l_gfs_global_counter_lock);
        {
            globus_l_gfs_bytes_transferred += op->bytes_transferred;
            tmp_bytes = globus_l_gfs_bytes_transferred;
        }
        globus_mutex_unlock(&globus_l_gfs_global_counter_lock);

        while((i < 5) && (tmp_bytes > 1024))
        {
            remainder = ((tmp_bytes % 1024) / 1024.0) + (remainder / 1024.0);
            tmp_bytes /= 1024;
            i++;
        }
        str_transferred = globus_gfs_config_get_string("byte_transfer_count");
        if(str_transferred)
        {
            sprintf(
                str_transferred,
                "%.2f %s",
                (double) tmp_bytes + remainder,
                names[i]);
        }
        globus_gfs_config_set_ptr("byte_transfer_count", str_transferred);
    }

/* RIGHT HERE I CAN GET ANOTHER SEND/RECV.  LEAVES IN TE STATE */
    globus_assert(!op->writing ||
        (op->sent_partial_eof == 1 || op->stripe_count == 1 ||
        (op->node_ndx == 0 && op->eof_ready)));

    reply.type = GLOBUS_GFS_OP_TRANSFER;
    reply.id = op->id;
    reply.result = op->cached_res;

    /* pull response code from error */
    if(reply.result != GLOBUS_SUCCESS && 
        (reply.code = globus_gfs_error_get_ftp_response_code(
            globus_error_peek(reply.result))) != 0)
    {
        reply.msg = globus_error_print_friendly(
            globus_error_peek(reply.result));
    }
    else
    {
        reply.code = op->user_code;

        /* DO NETLOGGER STUFF 
         * XXX unless user has a message -- will only be a problem if a DSI
         * is sending messages to the control channel, in which case they 
         * just aren't compatible with the netlogger bottleneck reporting
         */
        if(op->user_msg == NULL)
        {
            reply.msg = globus_l_gfs_data_get_nl_msg(op);
        }
        else
        {
            reply.msg = op->user_msg;
        }
    }

    /* tell the control side the finished was called */
    if(op->callback != NULL)
    {
        op->callback(&reply, op->user_arg);
    }
    else
    {
        globus_gfs_ipc_reply_finished(
            op->ipc_handle,
            &reply);
    }
    globus_l_gfs_data_reset_watchdog(op->session_handle, NULL);
    
    /* remove the refrence for this callback.  It is posible the before
        aquireing this lock the completing state occured and we are
        ready to finish */
    globus_mutex_lock(&op->session_handle->mutex);
    {
            switch(op->data_handle->state)
            {
                case GLOBUS_L_GFS_DATA_HANDLE_TE_VALID:
                    /* leave this state until after TC event? */
                    break;
                case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED:
                    op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_CLOSING;
                    break;
                case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_AND_DESTROYED:
                    op->data_handle->state =
                        GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED;
                    free_data = GLOBUS_TRUE;
                    data_handle = op->data_handle;
                    /* gotta free it here */
                    break;

                default:
                    break;
            }

        GFSDataOpDec(op, destroy_op, destroy_session);

        if(destroy_op)
        {
            globus_assert(op->data_handle != NULL);
            remote_data_arg = globus_l_gfs_data_check(
                op->session_handle, op->data_handle);
        }
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    if(destroy_op)
    {
        /* pass the complete event */
        if(op->session_handle->dsi->trev_func &&
            op->event_mask & GLOBUS_GFS_EVENT_TRANSFER_COMPLETE)
        {
            /* AAAA */
            event_info.type = GLOBUS_GFS_EVENT_TRANSFER_COMPLETE;
            event_info.event_arg = op->event_arg;
            op->session_handle->dsi->trev_func(
                &event_info,
                op->session_handle->session_arg);
        }
            globus_mutex_lock(&op->session_handle->mutex);
            {
                remote_data_arg = globus_l_gfs_data_post_transfer_event_cb(
                    op->session_handle, op->data_handle);
            }
            globus_mutex_unlock(&op->session_handle->mutex);

        globus_l_gfs_data_fire_cb(op, remote_data_arg, destroy_session);
        globus_l_gfs_data_operation_destroy(op);
    }
    if(free_data)
    {
        globus_l_gfs_data_handle_free(data_handle);
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_end_read_kickout(
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_bool_t                       end = GLOBUS_FALSE;
    globus_bool_t                       wait_for_commit = GLOBUS_FALSE;
    globus_l_gfs_data_operation_t *     op;

    globus_gfs_acl_action_t             action;
    int                                 rc;
    globus_result_t                     res;
    globus_gfs_transfer_info_t *        recv_info;
    globus_gfs_acl_object_desc_t        object;

    GlobusGFSName(globus_l_gfs_data_end_read_kickout);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) user_arg;

    if(op->data_handle->info.mode == 'E' || 
        globus_i_gfs_config_bool("always_send_markers"))
    {
        globus_gfs_event_info_t        event_reply;
        unsigned int num_channels;

        /* update actual number of streams that connected */
        globus_ftp_control_data_get_total_data_channels(
            &op->data_handle->data_channel,
            &num_channels,
            0);
        op->data_handle->info.nstreams = num_channels;

        memset(&event_reply, '\0', sizeof(globus_gfs_event_info_t));
        event_reply.id = op->id;
        event_reply.recvd_bytes = op->recvd_bytes;
        op->recvd_bytes = 0;
        event_reply.recvd_ranges = op->recvd_ranges;
        event_reply.node_ndx = op->node_ndx;
        event_reply.node_count = op->data_handle->info.nstreams;

        event_reply.type = GLOBUS_GFS_EVENT_BYTES_RECVD;
        if(op->event_callback != NULL)
        {
            op->event_callback(
                &event_reply,
                op->user_arg);
        }
        else
        {
            globus_gfs_ipc_reply_event(
                op->ipc_handle,
                &event_reply);
        }

        event_reply.type = GLOBUS_GFS_EVENT_RANGES_RECVD;
        if(op->event_callback != NULL)
        {
            op->event_callback(
                &event_reply,
                op->user_arg);
        }
        else
        {
            globus_gfs_ipc_reply_event(
                op->ipc_handle,
                &event_reply);
        }
    }
    else if(op->event_callback == NULL)
    {
        /* send bytes transferred to sync up frontend
         * for logging
         */
        globus_gfs_event_info_t        event_reply;
        memset(&event_reply, '\0', sizeof(globus_gfs_event_info_t));
        event_reply.id = op->id;
        event_reply.recvd_bytes = op->bytes_transferred;
        event_reply.node_count = 1;
        event_reply.type = GLOBUS_GFS_EVENT_BYTES_RECVD;

        globus_gfs_ipc_reply_event(
            op->ipc_handle,
            &event_reply);
    }

    globus_mutex_lock(&op->session_handle->mutex);
    {        
        switch(op->data_handle->state)
        {
            case GLOBUS_L_GFS_DATA_HANDLE_INUSE:
                /* if not mode e we need to close it */
                if(op->data_handle->info.mode != 'E')
                {
                    op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_CLOSING;
                    result = globus_ftp_control_data_force_close(
                        &op->data_handle->data_channel,
                        globus_l_gfs_data_finish_fc_cb,
                        op);
                    if(result != GLOBUS_SUCCESS)
                    {
                        op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED;
                        end = GLOBUS_TRUE;
                    }
                }
                else
                {
                    end = GLOBUS_TRUE;
                }
                break;

            default:
                break;
        }
        
        recv_info = op->info_struct;
        object.name = recv_info->pathname;
        object.size = op->bytes_transferred;
        object.final = GLOBUS_TRUE;
        action = GFS_ACL_ACTION_COMMIT;
        rc = globus_gfs_acl_authorize(
            &op->session_handle->acl_handle,
            action,
            &object,
            &res,
            globus_l_gfs_authorize_cb,
            end ? op : NULL);
        if(rc == GLOBUS_GFS_ACL_COMPLETE)
        {
            globus_l_gfs_authorize_cb(&object, action, NULL, res);
        }
        else
        {
            wait_for_commit = GLOBUS_TRUE;
        }

    }
    globus_mutex_unlock(&op->session_handle->mutex);
    if(end && !wait_for_commit)
    {
        globus_l_gfs_data_end_transfer_kickout(op);
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_cb_error(
    globus_l_gfs_data_handle_t *        data_handle)
{
    globus_l_gfs_data_operation_t *     op;
    GlobusGFSName(globus_l_gfs_data_cb_error);
    GlobusGFSDebugEnter();

    op = data_handle->outstanding_op;

    globus_mutex_lock(&op->session_handle->mutex);
    {
        switch(data_handle->state)
        {
            case GLOBUS_L_GFS_DATA_HANDLE_INUSE:
                data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED;
                /* XXX free it here ??? */
                break;

            case GLOBUS_L_GFS_DATA_HANDLE_CLOSING:
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED:
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSED:
                break;

            case GLOBUS_L_GFS_DATA_HANDLE_VALID:
            default:
                globus_assert(0 && "possible memory corruption");
                break;
        }
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_send_eof_cb(
    void *                              callback_arg,
    struct globus_ftp_control_handle_s * handle,
    globus_object_t *			error)
{
    globus_l_gfs_data_operation_t *     op;
    GlobusGFSName(globus_l_gfs_data_send_eof_cb);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) callback_arg;
    if(error != NULL)
    {
        /* XXX this should be thread safe see not in write_eof cb */
        globus_l_gfs_data_cb_error(op->data_handle);
        op->cached_res = globus_error_put(globus_object_copy(error));
    }
    globus_l_gfs_data_end_transfer_kickout(op);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_write_eof_cb(
    void *                              user_arg,
    globus_ftp_control_handle_t *       handle,
    globus_object_t *                   error,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_off_t                        offset,
    globus_bool_t                       eof)
{
    globus_bool_t                       end = GLOBUS_FALSE;
    globus_result_t                     result;
    globus_l_gfs_data_operation_t *     op;
    GlobusGFSName(globus_l_gfs_data_write_eof_cb);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) user_arg;

    GlobusGFSDebugState(op->state);
    GlobusGFSDebugState(op->data_handle->state);
    if(error != NULL)
    {
        /* XXX this should be thread safe since we only get this
            callback after a finsihed_transfer() from the user.  we
            could still get events or disconnects, but the abort process
            does not touch the data_handle->state */
        op->cached_res = globus_error_put(globus_object_copy(error));
        globus_gfs_log_result(
            GLOBUS_GFS_LOG_WARN, "write_eof_cb error", op->cached_res);
        globus_l_gfs_data_cb_error(op->data_handle);
        end = GLOBUS_TRUE;
    }
    else
    {
        globus_mutex_lock(&op->session_handle->mutex);
        {
            switch(op->data_handle->state)
            {
                case GLOBUS_L_GFS_DATA_HANDLE_INUSE:
                /* if not mode e we need to close it */
                if(op->data_handle->info.mode == 'E')
                {
                    result = globus_ftp_control_data_send_eof(
                        &op->data_handle->data_channel,
                        op->eof_count,
                        op->stripe_count,
                        (op->node_ndx == 0 || op->stripe_count == 1) ?
                            GLOBUS_TRUE : GLOBUS_FALSE,
                        globus_l_gfs_data_send_eof_cb,
                        op);
                    if(op->node_ndx != 0 && op->stripe_count > 1)
                    {
                       /* I think we want the eof event to kick off even
                        though we may have an error here since someone is
                        expecting it.  The transfer should still error out
                        normally */
                        globus_gfs_event_info_t        event_reply;
                        memset(&event_reply, '\0',
                            sizeof(globus_gfs_event_info_t));
                        event_reply.id = op->id;
                        event_reply.eof_count = op->eof_count;
                        event_reply.type = GLOBUS_GFS_EVENT_PARTIAL_EOF_COUNT;
                        event_reply.node_count = op->node_count;
                        if(op->event_callback != NULL)
                        {
                                op->event_callback(
                                &event_reply,
                                op->user_arg);
                        }
                        else
                        {
                            globus_gfs_ipc_reply_event(
                                op->ipc_handle,
                                &event_reply);
                        }
                        op->sent_partial_eof++;
                    }
                    if(result != GLOBUS_SUCCESS)
                    {
                        globus_gfs_log_result(
                            GLOBUS_GFS_LOG_WARN, "ERROR", result);
                        op->cached_res = result;
                        end = GLOBUS_TRUE;
                    }
                }
                else
                {
                    op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_CLOSING;
                    result = globus_ftp_control_data_force_close(
                        &op->data_handle->data_channel,
                        globus_l_gfs_data_finish_fc_cb,
                        op);
                    if(result != GLOBUS_SUCCESS)
                    {
                        op->data_handle->state =
                            GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED;
                        end = GLOBUS_TRUE;
                    }
                }

                default:
                    break;
            }
        }
        globus_mutex_unlock(&op->session_handle->mutex);
    }

    if(end)
    {
        globus_l_gfs_data_end_transfer_kickout(op);
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_send_eof(
    globus_l_gfs_data_operation_t *     op)
{
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_data_send_eof);
    GlobusGFSDebugEnter();

    globus_mutex_lock(&op->session_handle->mutex);
    {
        switch(op->state)
        {
            case GLOBUS_L_GFS_DATA_FINISH:
                op->eof_ready = GLOBUS_TRUE;
                result = globus_ftp_control_data_write(
                    &op->data_handle->data_channel,
                    (globus_byte_t *) "",
                    0,
                    0,
                    GLOBUS_TRUE,
                    globus_l_gfs_data_write_eof_cb,
                    op);
                if(result != GLOBUS_SUCCESS)
                {
                    globus_gfs_log_result(
                        GLOBUS_GFS_LOG_WARN, "send_eof error", result);
                    op->cached_res = result;

                if(op->data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_VALID)
                {
                    op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_TE_VALID;
                }
                globus_callback_register_oneshot(
                    NULL,
                    NULL,
                    globus_l_gfs_data_end_transfer_kickout,
                    op);
                }
                break;
            case GLOBUS_L_GFS_DATA_CONNECTED:
                op->eof_ready = GLOBUS_TRUE;
                break;
            default:
                /* figure out what needs to happen in other states */
                break;
        }
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_write_cb(
    void *                              user_arg,
    globus_ftp_control_handle_t *       handle,
    globus_object_t *                   error,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_off_t                        offset,
    globus_bool_t                       eof)
{
    globus_l_gfs_data_bounce_t *        bounce_info;
    GlobusGFSName(globus_l_gfs_data_write_cb);
    GlobusGFSDebugEnter();

    bounce_info = (globus_l_gfs_data_bounce_t *) user_arg;
    globus_l_gfs_data_alive(bounce_info->op->session_handle);

    bounce_info->op->bytes_transferred += length;
    bounce_info->op->recvd_bytes += length;

    bounce_info->callback.write(
        bounce_info->op,
        error ? globus_error_put(globus_object_copy(error)) : GLOBUS_SUCCESS,
        buffer,
        length,
        bounce_info->user_arg);

    globus_free(bounce_info);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_read_cb(
    void *                              user_arg,
    globus_ftp_control_handle_t *       handle,
    globus_object_t *                   error,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_off_t                        offset,
    globus_bool_t                       eof)
{
    globus_l_gfs_data_bounce_t *        bounce_info;
    GlobusGFSName(globus_l_gfs_data_read_cb);
    GlobusGFSDebugEnter();

    bounce_info = (globus_l_gfs_data_bounce_t *) user_arg;
    globus_l_gfs_data_alive(bounce_info->op->session_handle);

    bounce_info->op->bytes_transferred += length;

    bounce_info->callback.read(
        bounce_info->op,
        error ? globus_error_put(globus_object_copy(error)) : GLOBUS_SUCCESS,
        buffer,
        length,
        offset + bounce_info->op->write_delta,
        eof,
        bounce_info->user_arg);

    globus_free(bounce_info);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_trev_kickout(
    void *                              user_arg)
{
    globus_l_gfs_data_trev_bounce_t *   bounce_info;
    globus_gfs_event_info_t *           event_reply;
    void *                              remote_data_arg = NULL;
    globus_bool_t                       pass = GLOBUS_FALSE;
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    globus_gfs_event_info_t             event_info;

    globus_gfs_acl_action_t             action;
    int                                 rc;
    globus_result_t                     res;
    globus_gfs_transfer_info_t *        recv_info;
    globus_gfs_acl_object_desc_t        object;

    GlobusGFSName(globus_l_gfs_data_trev_kickout);
    GlobusGFSDebugEnter();

    bounce_info = (globus_l_gfs_data_trev_bounce_t *) user_arg;
    event_reply = (globus_gfs_event_info_t *)
        globus_calloc(1, sizeof(globus_gfs_event_info_t));

    event_reply->id = bounce_info->op->id;
    event_reply->node_ndx = bounce_info->op->node_ndx;
    globus_mutex_lock(&bounce_info->op->session_handle->mutex);
    {
        switch(bounce_info->op->state)
        {
            case GLOBUS_L_GFS_DATA_CONNECTING:
            case GLOBUS_L_GFS_DATA_CONNECTED:
            case GLOBUS_L_GFS_DATA_ABORTING:
            case GLOBUS_L_GFS_DATA_ABORT_CLOSING:
                pass = GLOBUS_TRUE;
                break;

            case GLOBUS_L_GFS_DATA_FINISH:
            case GLOBUS_L_GFS_DATA_FINISH_WITH_ERROR:
                pass = GLOBUS_FALSE;
                break;

            case GLOBUS_L_GFS_DATA_COMPLETING:
            case GLOBUS_L_GFS_DATA_COMPLETE:
            case GLOBUS_L_GFS_DATA_REQUESTING:
            default:
                globus_assert(0 && "possibly memory corruption");
                break;
        }
        if(pass)
        {
            switch(bounce_info->event_type)
            {
                case GLOBUS_GFS_EVENT_BYTES_RECVD:
                    event_reply->recvd_bytes = bounce_info->op->recvd_bytes;
                    bounce_info->op->recvd_bytes = 0;
                    event_reply->type = GLOBUS_GFS_EVENT_BYTES_RECVD;
                    break;
    
                case GLOBUS_GFS_EVENT_RANGES_RECVD:
                    event_reply->type = GLOBUS_GFS_EVENT_RANGES_RECVD;
                    globus_range_list_copy(
                        &event_reply->recvd_ranges,
                        bounce_info->op->recvd_ranges);
                    globus_range_list_remove(
                        bounce_info->op->recvd_ranges, 0, GLOBUS_RANGE_LIST_MAX);
                    break;
    
                default:
                    globus_assert(0 && "invalid state, not possible");
                    break;
            }
        }

        recv_info = bounce_info->op->info_struct;
        object.name = recv_info->pathname;
        object.size = bounce_info->op->bytes_transferred;
        object.final = GLOBUS_FALSE;
        action = GFS_ACL_ACTION_COMMIT;
        rc = globus_gfs_acl_authorize(
            &bounce_info->op->session_handle->acl_handle,
            action,
            &object,
            &res,
            globus_l_gfs_authorize_cb,
            NULL);
        if(rc == GLOBUS_GFS_ACL_COMPLETE)
        {
            globus_l_gfs_authorize_cb(&object, action, NULL, res);
        }

    }
    globus_mutex_unlock(&bounce_info->op->session_handle->mutex);

    if(globus_i_gfs_config_bool("sync_writes"))
    {
        sync();
    }


    if(pass)
    {
        if(bounce_info->op->event_callback != NULL)
        {
            bounce_info->op->event_callback(
                event_reply,
                bounce_info->op->user_arg);
        }
        else
        {
            globus_gfs_ipc_reply_event(
                bounce_info->op->ipc_handle,
                event_reply);
        }
    }

    globus_mutex_lock(&bounce_info->op->session_handle->mutex);
    {
        GFSDataOpDec(bounce_info->op, destroy_op, destroy_session);
        if(destroy_op)
        {
            globus_assert(
                bounce_info->op->state == GLOBUS_L_GFS_DATA_COMPLETING);
            globus_assert(bounce_info->op->data_handle != NULL);
            remote_data_arg = globus_l_gfs_data_check(
                bounce_info->op->session_handle, bounce_info->op->data_handle);
        }
    }
    globus_mutex_unlock(&bounce_info->op->session_handle->mutex);

    if(destroy_op)
    {
        /* pass the complete event */
        if(bounce_info->op->session_handle->dsi->trev_func &&
            bounce_info->op->event_mask & GLOBUS_GFS_EVENT_TRANSFER_COMPLETE)
        {
            /* AAAA */
            event_info.type = GLOBUS_GFS_EVENT_TRANSFER_COMPLETE;
            event_info.event_arg = bounce_info->op->event_arg;
            bounce_info->op->session_handle->dsi->trev_func(
                &event_info,
                bounce_info->op->session_handle->session_arg);
        }
            globus_mutex_lock(&bounce_info->op->session_handle->mutex);
            {
                remote_data_arg = globus_l_gfs_data_post_transfer_event_cb(
                    bounce_info->op->session_handle, bounce_info->op->data_handle);
            }
            globus_mutex_unlock(&bounce_info->op->session_handle->mutex);
        globus_l_gfs_data_fire_cb(
            bounce_info->op, remote_data_arg, destroy_session);
        globus_l_gfs_data_operation_destroy(bounce_info->op);
    }

    if(event_reply->recvd_ranges)
    {
        globus_range_list_destroy(event_reply->recvd_ranges);
    }
    globus_free(bounce_info);
    globus_free(event_reply);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_force_close(
    globus_l_gfs_data_operation_t *     op)
{
    GlobusGFSName(globus_l_gfs_data_force_close);
    GlobusGFSDebugEnter();

    /* handle the data_handle state machine */
    if(op->data_handle->is_mine)
    {
        switch(op->data_handle->state)
        {
            case GLOBUS_L_GFS_DATA_HANDLE_INUSE:
                op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_CLOSING;

                globus_callback_register_oneshot(
                    NULL,
                    NULL,
                    globus_l_gfs_data_end_transfer_kickout,
                    op);
                break;

            /* already started closing the handle */
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSED:
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSING:
                break;

            case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED:
            case GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_AND_DESTROYED:
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED:
            case GLOBUS_L_GFS_DATA_HANDLE_VALID:
            default:
                globus_assert(0 && "only should be called when inuse");
                break;
        }
    }
    else
    {
        switch(op->data_handle->state)
        {
            case GLOBUS_L_GFS_DATA_HANDLE_INUSE:
                op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_TE_PRE_CLOSED;
                globus_callback_register_oneshot(
                    NULL,
                    NULL,
                    globus_l_gfs_data_end_transfer_kickout,
                    op);
                break;

            /* already started closing the handle */
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSED:
            case GLOBUS_L_GFS_DATA_HANDLE_CLOSING:
                break;

            case GLOBUS_L_GFS_DATA_HANDLE_CLOSING_AND_DESTROYED:
            case GLOBUS_L_GFS_DATA_HANDLE_VALID:
            default:
                globus_assert(0 && "only should be called when inuse");
                break;
        }
    }

    GlobusGFSDebugExit();
}

/* must be called locked */
static
void
globus_l_gfs_data_start_abort(
    globus_l_gfs_data_operation_t *     op)
{
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_data_start_abort);
    GlobusGFSDebugEnter();

    if(op->session_handle->watch)
    {
        op->session_handle->watch_aborted = GLOBUS_TRUE;
    }

    switch(op->state)
    {
        case GLOBUS_L_GFS_DATA_REQUESTING:
            op->state = GLOBUS_L_GFS_DATA_ABORTING;
            break;

        case GLOBUS_L_GFS_DATA_CONNECTING:
        case GLOBUS_L_GFS_DATA_CONNECTED:
            if(op->data_handle->is_mine)
            {
                globus_assert(op->data_handle->state ==
                    GLOBUS_L_GFS_DATA_HANDLE_INUSE);
                op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_CLOSING;
                if(!op->connect_failed)
                {
                    GlobusGFSDebugInfo("globus_ftp_control_data_force_close");
                    result = globus_ftp_control_data_force_close(
                        &op->data_handle->data_channel,
                        globus_l_gfs_data_abort_fc_cb,
                        op);
                    if(result != GLOBUS_SUCCESS)
                    {
                        GlobusGFSDebugInfo("force_close failed");
                        globus_callback_register_oneshot(
                            NULL,
                            NULL,
                            globus_l_gfs_data_abort_kickout,
                            op);
                    }
                }
                else
                {
                    globus_callback_register_oneshot(
                        NULL,
                        NULL,
                        globus_l_gfs_data_abort_kickout,
                        op);
                }
            }
            else
            {
                globus_callback_register_oneshot(
                    NULL,
                    NULL,
                    globus_l_gfs_data_abort_kickout,
                    op);
            }
            op->state = GLOBUS_L_GFS_DATA_ABORT_CLOSING;
            op->ref++;
            break;

        /* everything post finished can ignore abort, because dsi is already
            done and connections should be torn down, or in the process
            of tearing down */
        case GLOBUS_L_GFS_DATA_FINISH:
        case GLOBUS_L_GFS_DATA_COMPLETING:
        case GLOBUS_L_GFS_DATA_COMPLETE:
            break;

        case GLOBUS_L_GFS_DATA_ABORT_CLOSING:
        case GLOBUS_L_GFS_DATA_ABORTING:
            /* do nothing cause it has already been done */
            break;

        default:
            break;
    }

    GlobusGFSDebugExit();
}

void
globus_i_gfs_data_request_transfer_event(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    globus_gfs_event_info_t *           event_info)
{
    void *                              remote_data_arg = NULL;
    globus_result_t                     result;
    globus_l_gfs_data_trev_bounce_t *   bounce_info;
    globus_l_gfs_data_session_t *       session_handle;
    globus_l_gfs_data_operation_t *     op;
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    globus_bool_t                       pass = GLOBUS_FALSE;
    GlobusGFSName(globus_i_gfs_data_request_transfer_event);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *) session_arg;
    op = (globus_l_gfs_data_operation_t *) 
        globus_handle_table_lookup(
            &session_handle->handle_table, (intptr_t) event_info->event_arg);
    if(op == NULL)
    {
        globus_assert(0 && "i wanna know when this happens");
    }
    globus_mutex_lock(&op->session_handle->mutex);
    {
        globus_assert(op->data_handle != NULL);

        /* this is the final event.  dec reference */
        switch(event_info->type)
        {
            /* if this event has been received we SHOULD be in complete state
                if we are not it is a bad message and we ignore it */
            case GLOBUS_GFS_EVENT_TRANSFER_COMPLETE:
                switch(op->state)
                {
                    case GLOBUS_L_GFS_DATA_FINISH:
                        /* even tho we are passing do not up the ref because
                           this is the barrier message */
                        op->state = GLOBUS_L_GFS_DATA_COMPLETING;
                        pass = GLOBUS_TRUE;
                        break;

                    case GLOBUS_L_GFS_DATA_FINISH_WITH_ERROR:
                        if(op->data_handle->is_mine)
                        {
                            op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_CLOSING;

                            result = globus_ftp_control_data_force_close(
                                &op->data_handle->data_channel,
                                globus_l_gfs_data_complete_fc_cb,
                                op);
                            if(result != GLOBUS_SUCCESS)
                            {
                                globus_gfs_log_result(
                                    GLOBUS_GFS_LOG_WARN,
                                    "force_close",
                                    result);
                                globus_l_gfs_data_fc_return(op);
                                pass = GLOBUS_TRUE;
                            }
                        }
                        else
                        {
                            pass = GLOBUS_TRUE;
                        }
                        op->state = GLOBUS_L_GFS_DATA_COMPLETING;
                        break;

                    default:
                        /* XXX log a bad message */
                        globus_assert(0 && "for now we assert");
                        pass = GLOBUS_FALSE;
                        break;
                }
                break;

            case GLOBUS_GFS_EVENT_FINAL_EOF_COUNT:
                /* XXX check state here, move send_eof call out of lock */
                op->eof_count = event_info->eof_count;
                globus_l_gfs_data_send_eof(op);
                break;

            case GLOBUS_GFS_EVENT_BYTES_RECVD:
            case GLOBUS_GFS_EVENT_RANGES_RECVD:
                /* we ignore these 2 events for everything except the
                    connected state */
                /* if finished already happened ignore these completely */
                if(op->state != GLOBUS_L_GFS_DATA_CONNECTED)
                {
                    pass = GLOBUS_FALSE;
                }
                else
                {
                    /* if the DSI is handling these events */
                    if(session_handle->dsi->trev_func != NULL &&
                        event_info->type & op->event_mask)
                    {
                        op->ref++;
                        pass = GLOBUS_TRUE;
                    }
                    /* if DSI not handling, take care of for them */
                    else
                    {
                        pass = GLOBUS_FALSE;
                        /* since this will be put in a callback we must up
                            ref */
                        op->ref++;

                        bounce_info = (globus_l_gfs_data_trev_bounce_t *)
                            globus_malloc(
                                sizeof(globus_l_gfs_data_trev_bounce_t));
                        if(!bounce_info)
                        {
                            result = GlobusGFSErrorMemory("bounce_info");
                        }

                        bounce_info->event_type = event_info->type;
                        bounce_info->op = op;
                        globus_callback_register_oneshot(
                            NULL,
                            NULL,
                            globus_l_gfs_data_trev_kickout,
                            bounce_info);
                    }
                }
                break;

            case GLOBUS_GFS_EVENT_TRANSFER_ABORT:
                /* start the abort process */
                globus_l_gfs_data_start_abort(op);
                break;

            /* only pass though if in connected state and the dsi wants
                the event */
            default:
                if(op->state != GLOBUS_L_GFS_DATA_CONNECTED ||
                    session_handle->dsi->trev_func == NULL ||
                    !(event_info->type & op->event_mask))
                {
                    pass = GLOBUS_FALSE;
                }
                else
                {
                    op->ref++;
                    pass = GLOBUS_TRUE;
                }
                break;
        }
        if(pass)
        {
            GFSDataOpDec(op, destroy_op, destroy_session);
            if(destroy_op)
            {
                globus_assert(op->state == GLOBUS_L_GFS_DATA_COMPLETING &&
                    op->data_handle != NULL);

                remote_data_arg = globus_l_gfs_data_check(
                    op->session_handle, op->data_handle);
            }
        }
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    /* if is possible that events slip through here after setting to
        GLOBUS_L_GFS_DATA_COMPLETE.  This is ok because the only
        guarantee made is that none will come after
        GLOBUS_GFS_EVENT_TRANSFER_COMPLETE.  This is guaranteed with
        the reference count. */
    if(pass)
    {
        /* if a TRANSFER_COMPLETE event we must respect the barrier */
        if(event_info->type != GLOBUS_GFS_EVENT_TRANSFER_COMPLETE)
        {
            /* XXX should copy here */
            event_info->event_arg = op->event_arg;
            session_handle->dsi->trev_func(
                event_info,
                session_handle->session_arg);
        }
        if(destroy_op)
        {
            if(session_handle->dsi->trev_func &&
                op->event_mask & GLOBUS_GFS_EVENT_TRANSFER_COMPLETE)
            {   /* XXX should make our own */
                /* AAAA */
                event_info->type = GLOBUS_GFS_EVENT_TRANSFER_COMPLETE;
                event_info->event_arg = op->event_arg;
                session_handle->dsi->trev_func(
                    event_info,
                    op->session_handle->session_arg);
            }
                globus_mutex_lock(&op->session_handle->mutex);
                {
                    remote_data_arg = globus_l_gfs_data_post_transfer_event_cb(
                        op->session_handle, op->data_handle);
                }
                globus_mutex_unlock(&op->session_handle->mutex);

/*
            if(event_info->type == GLOBUS_GFS_EVENT_TRANSFER_COMPLETE && 
                op->data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_TE_VALID)
            {
                op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_VALID;
            }
*/
            globus_l_gfs_data_fire_cb(op, remote_data_arg, destroy_session);
            /* destroy the op */
            globus_l_gfs_data_operation_destroy(op);
        }
    }

    GlobusGFSDebugExit();
}

void
globus_i_gfs_data_session_start(
    globus_gfs_ipc_handle_t             ipc_handle,
    const gss_ctx_id_t                  context,
    globus_gfs_session_info_t *         session_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg)
{
    globus_l_gfs_data_operation_t *     op;
    globus_result_t                     result;
    globus_l_gfs_data_session_t *       session_handle;
    GlobusGFSName(globus_i_gfs_data_session_start);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *)
        globus_calloc(1, sizeof(globus_l_gfs_data_session_t));
    if(session_handle == NULL)
    {
        /* XXX deal with this */
    }
    session_handle->dsi = globus_l_gfs_dsi;
    globus_handle_table_init(&session_handle->handle_table, NULL);
    globus_mutex_init(&session_handle->mutex, NULL);
    session_handle->ref = 1;
    session_handle->del_cred = session_info->del_cred;
    session_handle->context = context;
    session_handle->dcsc_cred = GSS_C_NO_CREDENTIAL;
    session_handle->order_data = session_handle->dsi->descriptor & 
        GLOBUS_GFS_DSI_DESCRIPTOR_REQUIRES_ORDERED_DATA;
    result = globus_l_gfs_data_operation_init(&op, session_handle);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_i_gfs_data_session_start", result);
        globus_assert(0);
    }
    op->session_handle = session_handle;
    op->ipc_handle = ipc_handle;
    op->uid = getuid();
    op->state = GLOBUS_L_GFS_DATA_REQUESTING;
    op->callback = cb;
    op->user_arg = user_arg;
    op->info_struct = session_info;
                
    result = globus_l_gfs_data_load_stack(
        "default",
        &op->session_handle->net_stack_list,
        &gfs_l_data_net_allowed_drivers,
        globus_i_gfs_config_string("dc_default"),
        GLOBUS_TRUE);
    if(result != GLOBUS_SUCCESS)
    {
        char *                          msg;
        msg = globus_error_print_friendly(globus_error_peek(result));
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_WARN,
            "Unable to set the default network stack: %s\n",
            msg);
        globus_free(msg);
    }

    result = globus_l_gfs_data_load_stack(
        "default",
        &op->session_handle->disk_stack_list,
        &gfs_l_data_disk_allowed_drivers,
        globus_i_gfs_config_string("fs_default"),
        GLOBUS_FALSE);
    if(result != GLOBUS_SUCCESS)
    {
        char *                          msg;
        msg = globus_error_print_friendly(globus_error_peek(result));
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_WARN,
            "Unable to set the default filesystem stack: %s\n",
            msg);
        globus_free(msg);
    }

    if(globus_i_gfs_config_int("auth_level") & GLOBUS_L_GFS_AUTH_IDENTIFY)
    {
        globus_l_gfs_data_authorize(op, context, session_info);
        
        if(globus_i_gfs_config_bool("hybrid"))
        {
            globus_gfs_session_info_t *     session_info_copy;
            
            session_info_copy = (globus_gfs_session_info_t *)
                globus_malloc(sizeof(globus_gfs_session_info_t));
            session_info_copy->del_cred = session_info->del_cred;
            session_info_copy->free_cred = GLOBUS_FALSE;
            session_info_copy->map_user = session_info->map_user;
            session_info_copy->username = globus_libc_strdup(session_info->username);
            session_info_copy->password = globus_libc_strdup(session_info->password);
            session_info_copy->subject = globus_libc_strdup(session_info->subject);
            session_info_copy->cookie = globus_libc_strdup(session_info->cookie);
            session_info_copy->host_id = globus_libc_strdup(session_info->host_id);
            
            session_handle->session_info_copy = session_info_copy;
            session_handle->hybrid = GLOBUS_TRUE;
        }
    }
    else
    {
        struct passwd *                 pwent;
        char *                          custom_home_dir;

        op->session_handle->uid = getuid();
        op->session_handle->gid = getgid();
        op->session_handle->gid_count = getgroups(0, NULL);
        op->session_handle->gid_array = (gid_t *) globus_malloc(
            op->session_handle->gid_count * sizeof(gid_t));
        getgroups(op->session_handle->gid_count, op->session_handle->gid_array);

        op->session_handle->username = 
            globus_libc_strdup(session_info->username);

        pwent = getpwuid(op->session_handle->uid);
        if(pwent && pwent->pw_dir)
        {
            op->session_handle->true_home = globus_libc_strdup(pwent->pw_dir);
        }
        else
        {
            op->session_handle->true_home = globus_l_gfs_defaulthome();
        }
    
        custom_home_dir = globus_i_gfs_config_string("home_dir");
        if(custom_home_dir)
        {
            char *                          var_dir;
    
            var_dir = globus_l_gfs_data_update_var_path(
                op->session_handle, custom_home_dir);
                
            op->session_handle->home_dir = var_dir;
        }
        else
        {
            op->session_handle->home_dir = 
                globus_libc_strdup(op->session_handle->true_home);
        }
        
        globus_l_gfs_data_update_restricted_paths(
            op->session_handle, &globus_l_gfs_path_alias_list_base);
        globus_l_gfs_data_update_restricted_paths(
            op->session_handle, &globus_l_gfs_path_alias_list_sharing);
        op->session_handle->active_rp_list = &globus_l_gfs_path_alias_list_base;
        
        if(!globus_i_gfs_config_bool("use_home_dirs") || 
            op->session_handle->home_dir == NULL)
        {
            if(op->session_handle->home_dir)
            {
                globus_free(op->session_handle->home_dir);
            }
            op->session_handle->home_dir = strdup("/");
        }  

        if(globus_i_gfs_config_bool("hybrid"))
        {
            globus_gfs_session_info_t *     session_info_copy;
            
            session_info_copy = (globus_gfs_session_info_t *)
                globus_malloc(sizeof(globus_gfs_session_info_t));
            session_info_copy->del_cred = session_info->del_cred;
            session_info_copy->free_cred = GLOBUS_FALSE;
            session_info_copy->map_user = session_info->map_user;
            session_info_copy->username = globus_libc_strdup(session_info->username);
            session_info_copy->password = globus_libc_strdup(session_info->password);
            session_info_copy->subject = globus_libc_strdup(session_info->subject);
            session_info_copy->cookie = globus_libc_strdup(session_info->cookie);
            session_info_copy->host_id = globus_libc_strdup(session_info->host_id);
            
            session_handle->session_info_copy = session_info_copy;
            session_handle->hybrid = GLOBUS_TRUE;
        }
        
        globus_l_gfs_data_auth_init_cb(
            NULL, GFS_ACL_ACTION_INIT, op, GLOBUS_SUCCESS);
    }

    GlobusGFSDebugExit();
}
            
void
globus_i_gfs_data_session_stop(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg)
{
    globus_bool_t                       free_session = GLOBUS_FALSE;
    globus_l_gfs_data_session_t *       session_handle;
    int                                 waitcnt = 0;
    int                                 maxwait = 100;
    GlobusGFSName(globus_i_gfs_data_session_stop);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *) session_arg;
    if(session_handle != NULL)
    {
        while(waitcnt < maxwait && !free_session)
        {
            globus_mutex_lock(&session_handle->mutex);
            {
                /* we must be the last ref */
                if(session_handle->ref == 1)
                {
                    free_session = GLOBUS_TRUE;
                }
            }
            globus_mutex_unlock(&session_handle->mutex);
            waitcnt++;
            if(!free_session)
            {
                /* delay up to .1 sec */
                globus_callback_poll(((const globus_abstime_t[]) {{0, 100000000L}}));
            }
        }
        if(session_handle->watch_handle != 0)
        {
            globus_callback_unregister(session_handle->watch_handle, NULL, NULL, NULL);
            session_handle->watch_handle = 0;
        }

        if(free_session)
        {
            if(session_handle->dsi->destroy_func != NULL &&
                session_handle->session_arg)
            {
                session_handle->dsi->destroy_func(session_handle->session_arg);
            }

            if(session_handle->dsi != globus_l_gfs_dsi)
            {
                globus_extension_release(session_handle->dsi_handle);
            }
            globus_l_gfs_free_session_handle(session_handle);
        }
        else
        {
            session_handle->ref--;
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_INFO,
                "Main thread was not able to call session_stop.\n");
        }
        if (waitcnt > 1)
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_INFO,
                "Called main session_stop after %d ticks.\n", waitcnt - 1);
        }    
    }

    if(globus_l_gfs_watchdog_limit)
    {
        globus_reltime_t                timer;
        GlobusTimeReltimeSet(timer, 120, 0);
        globus_callback_register_oneshot(
            NULL,
            &timer,
            globus_l_gfs_data_watchdog_check,
            NULL);
    }

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_finished_command_kickout(
    void *                              user_arg)
{
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    void *                              remote_data_arg = NULL;
    globus_l_gfs_data_operation_t *     op;
    globus_result_t                     result;
    globus_l_gfs_data_cmd_bounce_t *    bounce;

    bounce = (globus_l_gfs_data_cmd_bounce_t *) user_arg;
    op = bounce->op;
   
    if(op->callback != NULL)
    {
        op->callback(
            &bounce->reply,
            op->user_arg);
    }
    else
    {
        globus_gfs_ipc_reply_finished(
            op->ipc_handle,
            &bounce->reply);
    }
    
    if(bounce->reply.info.command.checksum)
    {
        globus_free(bounce->reply.info.command.checksum);
    }
    if(bounce->reply.msg)
        {
        globus_free(bounce->reply.msg);
        }
    if(bounce->reply.info.command.created_dir)
    {
        globus_free(bounce->reply.info.command.created_dir);
    }
    if((bounce->reply.code / 100) == 1)
    {
        globus_free(bounce);
        return;
    }
    else
    {
        globus_l_gfs_data_reset_watchdog(op->session_handle, NULL);
    }

    globus_mutex_lock(&op->session_handle->mutex);
    {
        GFSDataOpDec(op, destroy_op, destroy_session);
        remote_data_arg = globus_l_gfs_data_check(
            op->session_handle, op->data_handle);
    }
    globus_mutex_unlock(&op->session_handle->mutex);
    globus_assert(destroy_op);
    globus_l_gfs_data_fire_cb(op, remote_data_arg, destroy_session);
    globus_l_gfs_data_operation_destroy(op);
    
    globus_free(bounce);
}


/************************************************************************
 *
 * Public functions
 *
 ***********************************************************************/

void
globus_gridftp_server_finished_command(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    char *                              command_data)
{
    globus_l_gfs_data_cmd_bounce_t *    bounce;
    int                                 code;
    GlobusGFSName(globus_gridftp_server_finished_command);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    /* XXX gotta do a oneshot */
    switch(op->command)
    {
      case GLOBUS_GFS_CMD_CKSM:
        op->cksm_response = globus_libc_strdup(command_data);
        op->user_code = 0;
        break;
      case GLOBUS_GFS_CMD_UPAS:
        op->cksm_response = globus_libc_strdup(command_data);
        break;
      case GLOBUS_GFS_CMD_HTTP_PUT:
      case GLOBUS_GFS_CMD_HTTP_GET:
        op->user_code = 0;
        if(result == GLOBUS_SUCCESS && command_data)
        {
            op->user_msg = globus_libc_strdup(command_data);
        }
        break;
      case GLOBUS_GFS_CMD_WHOAMI:
        op->cksm_response = globus_libc_strdup(command_data);
        break;

      case GLOBUS_GFS_CMD_MKD:
      case GLOBUS_GFS_CMD_RMD:
      case GLOBUS_GFS_CMD_DELE:
      case GLOBUS_GFS_CMD_RNTO:
      case GLOBUS_GFS_CMD_SITE_CHMOD:
      case GLOBUS_GFS_CMD_SITE_CHGRP:
      case GLOBUS_GFS_CMD_SITE_UTIME:
      case GLOBUS_GFS_CMD_SITE_SYMLINK:
      default:
        if(op->command >= GLOBUS_GFS_MIN_CUSTOM_CMD)
        {
            op->user_msg = globus_libc_strdup(command_data);
        }
        break;
    }
    op->cached_res = result;

    bounce = (globus_l_gfs_data_cmd_bounce_t *) 
        globus_calloc(1, sizeof(globus_l_gfs_data_cmd_bounce_t));

    bounce->op = op;
    bounce->reply.type = GLOBUS_GFS_OP_COMMAND;
    bounce->reply.id = op->id;
    bounce->reply.result = op->cached_res;
    bounce->reply.info.command.command = op->command;
    bounce->reply.info.command.checksum = globus_libc_strdup(op->cksm_response);

    /* pull response code from error */
    if(result != GLOBUS_SUCCESS && 
        (code = globus_gfs_error_get_ftp_response_code(
            globus_error_peek(result))) != 0)
    {
        bounce->reply.code = code;
        bounce->reply.msg = globus_error_print_friendly(
            globus_error_peek(result));
    }
    else
    {
        bounce->reply.code = op->user_code;
        bounce->reply.msg = globus_libc_strdup(op->user_msg);
    }

    if(op->command == GLOBUS_GFS_CMD_MKD)
    {
        result = globus_i_gfs_data_virtualize_path(
            op->session_handle, op->pathname, &bounce->reply.info.command.created_dir);
        if(result != GLOBUS_SUCCESS || bounce->reply.info.command.created_dir == NULL)
        {
            bounce->reply.info.command.created_dir = globus_libc_strdup(op->pathname);
        }
    }
    
    result = globus_callback_register_oneshot(
        NULL,
        NULL,
        globus_l_gfs_finished_command_kickout,
        bounce);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_callback_register_oneshot", result);
        globus_panic(NULL, result, "oneshot failed, no recovery");
    }

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_finished_stat_partial(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    globus_gfs_stat_t *                 stat_array,
    int                                 stat_count)
{
    globus_l_gfs_data_stat_bounce_t *   bounce_info;
    globus_gfs_stat_t *                 stat_copy;
    int                                 i;
    char *                              base_path;
    globus_gfs_stat_info_t *            stat_info;
    GlobusGFSName(globus_gridftp_server_finished_stat_partial);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    globus_mutex_lock(&op->stat_lock);
    if(result == GLOBUS_SUCCESS)
    {        
        stat_info = (globus_gfs_stat_info_t *) op->info_struct;
        
        stat_copy = (globus_gfs_stat_t *)
            globus_malloc(sizeof(globus_gfs_stat_t) * stat_count);
        if(stat_copy == NULL)
        {
            result = GlobusGFSErrorMemory("stat_copy");
        }
    }
    
    if(result == GLOBUS_SUCCESS)
    {        
        base_path = stat_info->pathname;
        /* if we have explicit access on the base path, no need to prune */
        if(stat_info->file_only || globus_i_gfs_data_check_path(op->session_handle,
            base_path, NULL, GFS_L_READ | GFS_L_WRITE | GFS_L_DIR, 0) == GLOBUS_SUCCESS)
        {
            memcpy(
                stat_copy,
                stat_array,
                sizeof(globus_gfs_stat_t) * stat_count);
            for(i = 0; i < stat_count; i++)
            {
                if(stat_array[i].name != NULL)
                {
                    stat_copy[i].name = globus_libc_strdup(stat_array[i].name);
                }
                else
                {
                    /* XXX probably not acceptable to proceed */
                    stat_copy[i].name = globus_libc_strdup("(null)");
                }
                stat_copy[i].symlink_target =
                    globus_libc_strdup(stat_array[i].symlink_target);
            }
        }
        else
        {
            /* prune return based on restrictions */
            int                         pruned_stat_count = 0;
            char *                      nam;
            char *                      full_path;
            char *                      slash;
            
            if(base_path[strlen(base_path) - 1] != '/')
            {
                slash = "/";
            }
            else
            {
                slash = "";
            }
            for(i = 0; i < stat_count; i++)
            {
                nam = stat_array[i].name;
                full_path = globus_common_create_string(
                    "%s%s%s", base_path, slash, nam);
                if(nam && ((nam[0] == '.' && 
                    (nam[1] == '\0' || (nam[1] == '.' && nam[2] == '\0'))) ||
                    (globus_i_gfs_data_check_path(op->session_handle,
                    full_path, NULL, GFS_L_LIST, 0) == GLOBUS_SUCCESS)))
                {
                    memcpy(
                        &stat_copy[pruned_stat_count], 
                        &stat_array[i],
                        sizeof(globus_gfs_stat_t));
                        
                    stat_copy[pruned_stat_count].name = 
                        globus_libc_strdup(stat_array[i].name);
                    stat_copy[pruned_stat_count].symlink_target =
                        globus_libc_strdup(stat_array[i].symlink_target);
                    
                    pruned_stat_count++;
                }
                globus_free(full_path);
            }
            stat_count = pruned_stat_count;
            if(strcmp(stat_copy[0].name, ".") == 0)
            {
                stat_copy[0].nlink = pruned_stat_count;
            }
        }
            
    }
    else
    {
        stat_copy = NULL;
        stat_count = 0;
    }

    bounce_info = (globus_l_gfs_data_stat_bounce_t *)
        globus_calloc(sizeof(globus_l_gfs_data_stat_bounce_t), 1);
    if(bounce_info == NULL)
    {
        result = GlobusGFSErrorMemory("bounce_info");
        goto error_alloc;
    }

    bounce_info->op = op;
    bounce_info->error = result == GLOBUS_SUCCESS
        ? GLOBUS_NULL : globus_error_get(result);
    bounce_info->stat_count = stat_count;
    bounce_info->stat_array = stat_copy;
    bounce_info->final_stat = GLOBUS_FALSE;
    
    result = globus_callback_register_oneshot(
        GLOBUS_NULL,
        GLOBUS_NULL,
        globus_l_gfs_data_stat_kickout,
        bounce_info);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_callback_register_oneshot", result);
        goto error_oneshot;
    }
    globus_mutex_unlock(&op->stat_lock);

    globus_poll();
    
    GlobusGFSDebugExit();
    return;

error_oneshot:
error_alloc:
    globus_panic(
        GLOBUS_NULL,
        result,
        "[%s:%d] Unrecoverable error",
        _gfs_name,
        __LINE__);
    GlobusGFSDebugExitWithError();
}

void
globus_gridftp_server_intermediate_command(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    char *                              command_data)
{
    globus_l_gfs_data_cmd_bounce_t *    bounce;
    GlobusGFSName(globus_gridftp_server_finished_command);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    bounce = (globus_l_gfs_data_cmd_bounce_t *) 
        globus_calloc(1, sizeof(globus_l_gfs_data_cmd_bounce_t));

    switch(op->command)
    {
      case GLOBUS_GFS_CMD_CKSM:
        bounce->reply.info.command.checksum = globus_libc_strdup(command_data);
        bounce->reply.code = 113;
        break;
        
      case GLOBUS_GFS_CMD_HTTP_PUT:
      case GLOBUS_GFS_CMD_HTTP_GET:
        bounce->reply.info.command.checksum = globus_libc_strdup(command_data);
        bounce->reply.code = 112;
        break;

      case GLOBUS_GFS_CMD_MKD:
      case GLOBUS_GFS_CMD_RMD:
      case GLOBUS_GFS_CMD_DELE:
      case GLOBUS_GFS_CMD_RNTO:
      case GLOBUS_GFS_CMD_SITE_CHMOD:
      default:
        break;
    }
    op->cached_res = result;

    bounce->op = op;
    bounce->reply.type = GLOBUS_GFS_OP_COMMAND;
    bounce->reply.id = op->id;
    bounce->reply.result = op->cached_res;
    bounce->reply.info.command.command = op->command;

    result = globus_callback_register_oneshot(
        NULL,
        NULL,
        globus_l_gfs_finished_command_kickout,
        bounce);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_callback_register_oneshot", result);
        globus_panic(NULL, result, "oneshot failed, no recovery");
    }

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_finished_stat(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    globus_gfs_stat_t *                 stat_array,
    int                                 stat_count)
{
    globus_l_gfs_data_stat_bounce_t *   bounce_info;
    globus_gfs_stat_t *                 stat_copy;
    int                                 i;
    char *                              base_path;
    globus_gfs_stat_info_t *            stat_info;
    int                                 code;
    GlobusGFSName(globus_gridftp_server_finished_stat);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);
    
    globus_mutex_lock(&op->stat_lock);
    if(stat_array != NULL && stat_count > 0)
    {
        stat_info = (globus_gfs_stat_info_t *) op->info_struct;

        stat_copy = (globus_gfs_stat_t *)
            globus_malloc(sizeof(globus_gfs_stat_t) * stat_count);
        if(stat_copy == NULL)
        {
            result = GlobusGFSErrorMemory("stat_copy");
            goto error_alloc;
        }
        
        base_path = stat_info->pathname;
        /* if we have explicit access on the base path, no need to prune */
        if(stat_info->file_only || globus_i_gfs_data_check_path(op->session_handle,
            base_path, NULL, GFS_L_READ | GFS_L_WRITE | GFS_L_DIR, 0) == GLOBUS_SUCCESS)
        {
            memcpy(
                stat_copy,
                stat_array,
                sizeof(globus_gfs_stat_t) * stat_count);
            for(i = 0; i < stat_count; i++)
            {
                if(stat_array[i].name != NULL)
                {
                    stat_copy[i].name = globus_libc_strdup(stat_array[i].name);
                }
                else
                {
                    /* XXX probably not acceptable to proceed */
                    stat_copy[i].name = globus_libc_strdup("(null)");
                }
                stat_copy[i].symlink_target =
                    globus_libc_strdup(stat_array[i].symlink_target);
            }
        }
        else
        {
            /* prune return based on restrictions */
            int                         pruned_stat_count = 0;
            char *                      nam;
            char *                      full_path;
            char *                      slash;
            
            if(base_path[strlen(base_path) - 1] != '/')
            {
                slash = "/";
            }
            else
            {
                slash = "";
            }
            for(i = 0; i < stat_count; i++)
            {
                nam = stat_array[i].name;
                full_path = globus_common_create_string(
                    "%s%s%s", base_path, slash, nam);
                if(nam && ((nam[0] == '.' && 
                    (nam[1] == '\0' || (nam[1] == '.' && nam[2] == '\0'))) ||
                    (globus_i_gfs_data_check_path(op->session_handle,
                    full_path, NULL, GFS_L_LIST, 0) == GLOBUS_SUCCESS)))
                {
                    memcpy(
                        &stat_copy[pruned_stat_count], 
                        &stat_array[i],
                        sizeof(globus_gfs_stat_t));
                        
                    stat_copy[pruned_stat_count].name = 
                        globus_libc_strdup(stat_array[i].name);
                    stat_copy[pruned_stat_count].symlink_target =
                        globus_libc_strdup(stat_array[i].symlink_target);
                    
                    pruned_stat_count++;
                }
                globus_free(full_path);
            }
            stat_count = pruned_stat_count;
            if(strcmp(stat_copy[0].name, ".") == 0)
            {
                stat_copy[0].nlink = pruned_stat_count;
            }
        }
            
    }
    else
    {
        stat_copy = NULL;
        stat_count = 0;
    }

    bounce_info = (globus_l_gfs_data_stat_bounce_t *)
        globus_calloc(sizeof(globus_l_gfs_data_stat_bounce_t), 1);
    if(bounce_info == NULL)
    {
        result = GlobusGFSErrorMemory("bounce_info");
        goto error_alloc;
    }

    bounce_info->op = op;
    bounce_info->error = result == GLOBUS_SUCCESS
        ? GLOBUS_NULL : globus_error_get(result);
    bounce_info->stat_count = stat_count;
    bounce_info->stat_array = stat_copy;
    bounce_info->final_stat = GLOBUS_TRUE;
    
    result = globus_callback_register_oneshot(
        GLOBUS_NULL,
        GLOBUS_NULL,
        globus_l_gfs_data_stat_kickout,
        bounce_info);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_callback_register_oneshot", result);
        goto error_oneshot;
    }
    globus_mutex_unlock(&op->stat_lock);

    GlobusGFSDebugExit();
    return;

error_oneshot:
error_alloc:
    globus_panic(
        GLOBUS_NULL,
        result,
        "[%s:%d] Unrecoverable error",
        _gfs_name,
        __LINE__);
    GlobusGFSDebugExitWithError();
}

void
globus_gridftp_server_begin_transfer(
    globus_gfs_operation_t              op,
    int                                 event_mask,
    void *                              event_arg)
{
    void *                              remote_data_arg = NULL;
    globus_bool_t                       pass_abort = GLOBUS_FALSE;
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    globus_result_t                     result;
    globus_gfs_event_info_t             event_reply;
    globus_gfs_event_info_t             event_info;
    char *                              freq;
    GlobusGFSName(globus_gridftp_server_begin_transfer);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    gettimeofday(&op->start_timeval, NULL);
    op->event_mask = event_mask;
    op->event_arg = event_arg;

    /* increase refrence count for the events.  This gets decreased when
        the COMPLETE event occurs.  it is safe to increment outside of a
        lock because until we enable events there should be no
        contention
       increase the reference count a second time for this function.
       It is possible that after enabling events but before getting the lock
        that we: 1) get an abort, 2) get a finished() from dsi,
        3) get a complete, 4) free the op.  if this happens there will
        be no memory at op->mutex. we get around this with an extra
        reference count */
    op->ref += 2;
    memset(&event_reply, '\0', sizeof(globus_gfs_event_info_t));
    event_reply.type = GLOBUS_GFS_EVENT_TRANSFER_BEGIN;
    event_reply.id = op->id;
    event_reply.event_mask =
        GLOBUS_GFS_EVENT_TRANSFER_ABORT | GLOBUS_GFS_EVENT_TRANSFER_COMPLETE;

    if(op->data_handle->is_mine)
    {
        event_reply.node_count = op->node_count;
    }

    if(op->writing && (freq = getenv("GFS_RETR_MARKERS")) != NULL)
    {
        op->retr_markers = strtol(freq, NULL, 10);
    }

    if(!op->data_handle->is_mine || op->data_handle->info.mode == 'E' ||
        globus_i_gfs_config_bool("always_send_markers"))
    {
        event_reply.event_mask |=
            GLOBUS_GFS_EVENT_BYTES_RECVD | GLOBUS_GFS_EVENT_RANGES_RECVD;
    }

    event_reply.event_arg = (void *) (intptr_t) globus_handle_table_insert(
        &op->session_handle->handle_table, op, 1);
    if(op->event_callback != NULL)
    {
        op->event_callback(&event_reply, op->user_arg);
    }
    else
    {
        globus_gfs_ipc_reply_event(op->ipc_handle, &event_reply);
    }

    /* at this point events can happen that change the state before
        the lock is aquired */

    globus_mutex_lock(&op->session_handle->mutex);
    {
        switch(op->state)
        {
            /* if going according to plan */
            case GLOBUS_L_GFS_DATA_REQUESTING:
                op->state = GLOBUS_L_GFS_DATA_CONNECTING;
                if(op->data_handle->is_mine)
                {
                    if(op->writing)
                    {
                        GlobusGFSDebugInfo(
                            "globus_ftp_control_data_connect_write");
                        result = globus_ftp_control_data_connect_write(
                            &op->data_handle->data_channel,
                            globus_l_gfs_data_begin_cb,
                            op);
                    }
                    else
                    {
                        GlobusGFSDebugInfo(
                            "globus_ftp_control_data_connect_read");
                        
                        if(op->order_data && op->data_handle->info.mode == 'E')
                        {
                            result = globus_ftp_control_set_force_order(
                                &op->data_handle->data_channel,
                                GLOBUS_TRUE,
                                op->order_data_start);
                        }
                        result = globus_ftp_control_data_connect_read(
                            &op->data_handle->data_channel,
                            globus_l_gfs_data_begin_cb,
                            op);
                    }
                }
                else
                {
                    /* rarre case where we are willing to check return code
                        from oneshot, however, if it ever fail many many
                        other things are likely to break frist */
                    GlobusGFSDebugInfo(
                        "oneshot globus_l_gfs_data_begin_kickout");
                    result = globus_callback_register_oneshot(
                        NULL,
                        NULL,
                        globus_l_gfs_data_begin_kickout,
                        op);
                }
                if(result != GLOBUS_SUCCESS)
                {
                    GlobusGFSDebugInfo("Register failed");
                    op->state = GLOBUS_L_GFS_DATA_ABORTING;
                    /* if the connects fail tell the dsi to abort */
                    op->cached_res = result;
                    if(op->session_handle->dsi->trev_func != NULL &&
                        op->event_mask & GLOBUS_GFS_EVENT_TRANSFER_ABORT)
                    {
                        pass_abort = GLOBUS_TRUE;
                        op->ref++;
                    }
                }
                else
                {
                    GlobusGFSDebugInfo("Register successful");
                    /* for the begin callback on success */
                    if(op->writing && op->data_handle->is_mine)
                    {
                        op->ref++;
                        op->stripe_connections_pending =
                            op->data_handle->info.cs_count;
                    }
                    else
                    {
                        op->ref++;
                        op->stripe_connections_pending = 1;
                    }
                }
                break;

            /* if in this state we have delayed the pass to the dsi until
                after we know they have requested events */
            case GLOBUS_L_GFS_DATA_ABORTING:
                if(op->session_handle->dsi->trev_func != NULL &&
                    op->event_mask & GLOBUS_GFS_EVENT_TRANSFER_ABORT)
                {
                    pass_abort = GLOBUS_TRUE;
                    op->ref++;
                }
                break;

            /* we are waiting for the force close callback to return */
            case GLOBUS_L_GFS_DATA_ABORT_CLOSING:
                break;

            /* nothing to do here, finishing is in the works */
            case GLOBUS_L_GFS_DATA_FINISH:
                break;

            /* if this happens we went through all the step in the above
                doc box. */
            case GLOBUS_L_GFS_DATA_COMPLETING:
                break;

            /* the reference counting should make htis not possible */
            case GLOBUS_L_GFS_DATA_COMPLETE:
                globus_assert(0 &&
                    "reference counts are likely messed up");
                break;

            /* this could only happen if the dsi did something bad, like
                maybe call this function twice? */
            case GLOBUS_L_GFS_DATA_CONNECTING:
            case GLOBUS_L_GFS_DATA_CONNECTED:
                globus_assert(0 &&
                    "In connecting state before it should be possible");
                break;
            default:
                globus_assert(0 && "this should not be possible");
                break;
        }

        GFSDataOpDec(op, destroy_op, destroy_session);
        if(destroy_op)
        {
            globus_assert(op->state == GLOBUS_L_GFS_DATA_COMPLETING);
        }
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    if(pass_abort)
    {
        event_info.type = GLOBUS_GFS_EVENT_TRANSFER_ABORT;
        event_info.event_arg = op->event_arg;
        op->session_handle->dsi->trev_func(
            &event_info,
            op->session_handle->session_arg);
        globus_mutex_lock(&op->session_handle->mutex);
        {
            GFSDataOpDec(op, destroy_op, destroy_session);
            if(destroy_op)
            {
                globus_assert(op->state == GLOBUS_L_GFS_DATA_COMPLETING &&
                    op->data_handle != NULL);
                op->data_handle->outstanding_op = NULL;
                remote_data_arg = globus_l_gfs_data_check(
                    op->session_handle, op->data_handle);
            }
        }
        globus_mutex_unlock(&op->session_handle->mutex);
    }

    if(destroy_op)
    {
        if(op->session_handle->dsi->trev_func &&
            op->event_mask & GLOBUS_GFS_EVENT_TRANSFER_COMPLETE)
        {
            /* XXX does this call need to be in a oneshot? */

            /* AAAA */
            event_info.type = GLOBUS_GFS_EVENT_TRANSFER_COMPLETE;
            event_info.event_arg = op->event_arg;
            op->session_handle->dsi->trev_func(
                &event_info,
                op->session_handle->session_arg);
        }
            globus_mutex_lock(&op->session_handle->mutex);
            {
                remote_data_arg = globus_l_gfs_data_post_transfer_event_cb(
                    op->session_handle, op->data_handle);
            }
            globus_mutex_unlock(&op->session_handle->mutex);
        /* destroy the op */
        globus_l_gfs_data_fire_cb(op, remote_data_arg, destroy_session);
        globus_l_gfs_data_operation_destroy(op);
    }

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_finished_transfer(
    globus_gfs_operation_t              op,
    globus_result_t                     result)
{
    GlobusGFSName(globus_gridftp_server_finished_transfer);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    globus_mutex_lock(&op->session_handle->mutex);
    {
        /* move the data_handle state to VALID.  at first error if will
            be moved to invalid */
        switch(op->state)
        {
            /* this is the normal case */
            case GLOBUS_L_GFS_DATA_CONNECTED:
                if(result != GLOBUS_SUCCESS)
                {
                    GlobusGFSDebugInfo("passed error in CONNECTED state\n");
                    goto err_close;
                }
                globus_l_gfs_data_finish_connected(op);
                op->state = GLOBUS_L_GFS_DATA_FINISH;
                break;

            /* finishing in connecting state with no error likely means
                a zero byte transfer.  the finish will be kicked off in
                the connect_cb when it comes, here we just let it fall
                through and change state to finished
                XXX think we need another state here, what if we get
                an abort while waiting for connect_cb but we are finished */
            case GLOBUS_L_GFS_DATA_CONNECT_CB:
            case GLOBUS_L_GFS_DATA_CONNECTING:
                op->finished_delayed = GLOBUS_TRUE;
                op->state = GLOBUS_L_GFS_DATA_FINISH;
                if(result != GLOBUS_SUCCESS)
                {
                    GlobusGFSDebugInfo("passed error in CONNECTING state\n");
                    op->cached_res = result;
                    op->state = GLOBUS_L_GFS_DATA_FINISH_WITH_ERROR;
                }
                break;

            case GLOBUS_L_GFS_DATA_ABORTING:
            case GLOBUS_L_GFS_DATA_REQUESTING:
                if(result != GLOBUS_SUCCESS)
                {
                    op->cached_res = result;
                }
                op->state = GLOBUS_L_GFS_DATA_FINISH;

                /* XXX goto check the data handle state */
                if(op->data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_VALID)
                {
                    op->data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_TE_VALID;
                }
                globus_callback_register_oneshot(
                    NULL,
                    NULL,
                    globus_l_gfs_data_end_transfer_kickout,
                    op);
                break;

            /* waiting for a force close callback to return.  will switch
                to the finished state, when the force close callback comes
                back it will continue the finish process */
            case GLOBUS_L_GFS_DATA_ABORT_CLOSING:
                op->state = GLOBUS_L_GFS_DATA_FINISH;
                break;

            case GLOBUS_L_GFS_DATA_COMPLETING:
            case GLOBUS_L_GFS_DATA_COMPLETE:
            case GLOBUS_L_GFS_DATA_FINISH:
            default:
                globus_assert(0 && "Invalid state");
                break;
        }
        globus_gfs_config_inc_int("file_transfer_count", 1);
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    GlobusGFSDebugExit();
    return;

err_close:
    globus_l_gfs_data_force_close(op);
    op->cached_res = result;
    op->state = GLOBUS_L_GFS_DATA_FINISH_WITH_ERROR;
    globus_mutex_unlock(&op->session_handle->mutex);
    GlobusGFSDebugExitWithError();
}

static
void
globus_l_gfs_operation_finished_kickout(
    void *                              user_arg)
{
    globus_bool_t                       destroy_session = GLOBUS_FALSE;
    globus_bool_t                       destroy_op = GLOBUS_FALSE;
    globus_l_gfs_data_bounce_t *        bounce;
    void *                              remote_data_arg = NULL;
    globus_l_gfs_data_operation_t *     op;
    GlobusGFSName(globus_l_gfs_operation_finished_kickout);
    GlobusGFSDebugEnter();

    bounce = (globus_l_gfs_data_bounce_t *) user_arg;
    op = bounce->op;

    if(bounce->finished_info->type == GLOBUS_GFS_OP_SESSION_START)
    {
        /* update home dir based on restricted paths */
        
        /* reset home dir to / if chroot. */
        if(op->session_handle->chroot_path)
        {
            if(op->session_handle->home_dir)
            {
                globus_free(op->session_handle->home_dir);
            }
            op->session_handle->home_dir = strdup("/");

            bounce->finished_info->info.session.home_dir =
                op->session_handle->home_dir;
        }

        globus_l_gfs_data_update_restricted_paths_symlinks(
            op->session_handle, &globus_l_gfs_path_alias_list_base);
        globus_l_gfs_data_update_restricted_paths_symlinks(
            op->session_handle, &globus_l_gfs_path_alias_list_sharing);

        if(globus_i_gfs_data_check_path(op->session_handle,
               op->session_handle->home_dir, NULL, GFS_L_LIST, 1) != GLOBUS_SUCCESS)
        {
            if(op->session_handle->home_dir)
            {
                globus_free(op->session_handle->home_dir);
            }
            op->session_handle->home_dir = strdup("/");
            
            bounce->finished_info->info.session.home_dir =
                op->session_handle->home_dir;
        }
    }

    if(op->callback != NULL)
    {
        op->callback(
            bounce->finished_info,
            op->user_arg);
    }
    else
    {
        if(bounce->finished_info->type == GLOBUS_GFS_OP_SESSION_START)
        {
            globus_gfs_ipc_reply_session(
                op->ipc_handle,
                bounce->finished_info);
        }
        else
        {
            globus_gfs_ipc_reply_finished(
                op->ipc_handle,
                bounce->finished_info);
        }
    }
    globus_l_gfs_data_reset_watchdog(op->session_handle, NULL);

    globus_mutex_lock(&op->session_handle->mutex);
    {
        GFSDataOpDec(op, destroy_op, destroy_session);
        if(destroy_op)
        {
            remote_data_arg = globus_l_gfs_data_check(
                op->session_handle, op->data_handle);
        }
    }
    globus_mutex_unlock(&op->session_handle->mutex);

     /* globus_assert(destroy_op); this was wrong, there could stull
        be an event out there */
    if(destroy_op)
    {
        globus_l_gfs_data_fire_cb(op, remote_data_arg, destroy_session);
        globus_l_gfs_data_operation_destroy(op);
    }

    if(bounce->finished_info->op_info)
    {
        globus_free(bounce->finished_info->op_info);
        bounce->finished_info->op_info = NULL;
    }
    globus_free(bounce);

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_operation_finished(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    globus_gfs_finished_info_t *        finished_info)
{
    globus_l_gfs_data_bounce_t *        bounce;
    globus_l_gfs_data_handle_t *        data_handle;
    globus_bool_t                       kickout = GLOBUS_TRUE;
    GlobusGFSName(globus_gridftp_server_operation_finished);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    if(finished_info->code / 100 == 1)
    {
        switch(finished_info->type)
        {
          case GLOBUS_GFS_OP_COMMAND:
            if(op->command == GLOBUS_GFS_CMD_CKSM)
            {
                globus_gridftp_server_intermediate_command(
                    op, result, finished_info->info.command.checksum);
                return;
            }
            break;
          case GLOBUS_GFS_OP_STAT:
            globus_gridftp_server_finished_stat_partial(
                op, 
                result, 
                finished_info->info.stat.stat_array, 
                finished_info->info.stat.stat_count);
            return;
          default:
            break;
        }
    }
    bounce = (globus_l_gfs_data_bounce_t *)
        globus_malloc(sizeof(globus_l_gfs_data_bounce_t));
    if(bounce == NULL)
    {
        globus_panic(NULL, result, "small malloc failure, no recovery");
    }
    bounce->op = op;
    bounce->finished_info = finished_info;

    finished_info->id = op->id;
    finished_info->result = result;
    
    if(finished_info->msg != NULL)
    {
        op->user_msg = globus_libc_strdup(finished_info->msg);
    }
    op->user_code = finished_info->code;
    
    switch(finished_info->type)
    {
        case GLOBUS_GFS_OP_RECV:
        case GLOBUS_GFS_OP_SEND:
        case GLOBUS_GFS_OP_TRANSFER:
            globus_gridftp_server_finished_transfer(
                op, finished_info->result);
            kickout = GLOBUS_FALSE;
            break;

        case GLOBUS_GFS_OP_SESSION_START:
            if(finished_info->result != GLOBUS_SUCCESS)
            {
                finished_info->info.session.session_arg = NULL;
                /* we won't be getting a stop */
                op->session_handle->ref--;
                break;
            }
            op->session_handle->session_arg =
                (void *) finished_info->info.session.session_arg;
            finished_info->info.session.session_arg = op->session_handle;
            if(finished_info->info.session.username == NULL)
            {
                finished_info->info.session.username =
                    op->session_handle->username;
            }
            if(finished_info->info.session.home_dir == NULL)
            {
                finished_info->info.session.home_dir =
                    op->session_handle->home_dir;
            }
            else
            {
                if(op->session_handle->home_dir)
                {
                    globus_free(op->session_handle->home_dir);
                }
                op->session_handle->home_dir = 
                    strdup(finished_info->info.session.home_dir);
            }
            
            if(globus_hashtable_empty(&op->session_handle->custom_cmd_table))
            {
                finished_info->op_info = NULL;
            }
            if(op->callback != NULL && 
                !globus_hashtable_empty(
                    &op->session_handle->custom_cmd_table))
            {
                finished_info->op_info = globus_calloc(1, sizeof(globus_i_gfs_op_info_t));
                finished_info->op_info->custom_command_table = 
                    op->session_handle->custom_cmd_table;
            }
            
            break;

        case GLOBUS_GFS_OP_PASSIVE:
        case GLOBUS_GFS_OP_ACTIVE:

            if(finished_info->result == GLOBUS_SUCCESS)
            {
                data_handle = (globus_l_gfs_data_handle_t *)
                    globus_calloc(1, sizeof(globus_l_gfs_data_handle_t));
                if(data_handle == NULL)
                {
                    globus_panic(NULL, result,
                        "small malloc failure, no recovery");
                }

                memcpy(&data_handle->info, op->info_struct,
                    sizeof(globus_gfs_data_info_t));
                data_handle->session_handle = op->session_handle;
                data_handle->remote_data_arg = finished_info->info.data.data_arg;
                data_handle->is_mine = GLOBUS_FALSE;
                data_handle->info.mode  =
                    ((globus_gfs_data_info_t *)op->info_struct)->mode;
                data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_VALID;
                finished_info->info.data.data_arg =
                    (void *) (intptr_t) globus_handle_table_insert(
                        &data_handle->session_handle->handle_table,
                        data_handle,
                        1);
            }
            break;

        default:
            break;
    }
    if(kickout)
    {
        globus_l_gfs_operation_finished_kickout(bounce);
    }
    else
    {
        globus_free(bounce);
    }
/*
    result = globus_callback_register_oneshot(
        NULL,
        NULL,
        globus_l_gfs_operation_finished_kickout,
        bounce);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_callback_register_oneshot", result);
        globus_panic(NULL, result, "small malloc failure, no recovery");
    }
*/
    GlobusGFSDebugExit();
}

void
globus_gridftp_server_operation_event(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    globus_gfs_event_info_t *           event_info)
{
    globus_bool_t                       pass = GLOBUS_FALSE;
    GlobusGFSName(globus_gridftp_server_operation_event);
    GlobusGFSDebugEnter();

    event_info->id = op->id;

    /* XXX gotta do a onesot here ?? */
    switch(event_info->type)
    {
        case GLOBUS_GFS_EVENT_TRANSFER_BEGIN:
            op->node_count = event_info->node_count;
            globus_gridftp_server_begin_transfer(
                op, event_info->event_mask, event_info->event_arg);
            break;
        case GLOBUS_GFS_EVENT_BYTES_RECVD:
            if(op->event_callback != NULL)
            {
                if(event_info->node_count > op->data_handle->info.nstreams)
                {
                    op->data_handle->info.nstreams =
                        event_info->node_count;
                }
                op->bytes_transferred += event_info->recvd_bytes;
            }
            if(op->data_handle->info.mode == 'E' || 
                globus_i_gfs_config_bool("always_send_markers"))
            {
                pass = GLOBUS_TRUE;
            }
        default:
            pass = GLOBUS_TRUE;
            break;
    }

    if(pass)
    {
        if(op->event_callback != NULL)
        {
            op->event_callback(
                event_info,
                op->user_arg);
        }
        else
        {
            globus_gfs_ipc_reply_event(
                op->ipc_handle,
                event_info);
        }
    }

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_update_bytes_recvd(
    globus_gfs_operation_t              op,
    globus_off_t                        length)
{
    GlobusGFSName(globus_gridftp_server_update_bytes_recvd);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    globus_mutex_lock(&op->session_handle->mutex);
    {
        op->recvd_bytes += length;
            
        if(op->data_handle->http_handle)
        {
            op->data_handle->http_transferred += length;
        }
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_update_range_recvd(
    globus_gfs_operation_t              op,
    globus_off_t                        offset,
    globus_off_t                        length)
{
    GlobusGFSName(globus_gridftp_server_update_range_recvd);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    globus_mutex_lock(&op->session_handle->mutex);
    {
        globus_range_list_insert(
            op->recvd_ranges, offset + op->transfer_delta, length);
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_update_bytes_written(
    globus_gfs_operation_t              op,
    globus_off_t                        offset,
    globus_off_t                        length)
{
    GlobusGFSName(globus_gridftp_server_update_bytes_written);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    globus_mutex_lock(&op->session_handle->mutex);
    {
        op->recvd_bytes += length;
        globus_range_list_insert(
            op->recvd_ranges, offset + op->transfer_delta, length);
            
        if(op->data_handle->http_handle)
        {
            op->data_handle->http_transferred += length;
        }
    }
    globus_mutex_unlock(&op->session_handle->mutex);

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_get_optimal_concurrency(
    globus_gfs_operation_t              op,
    int *                               count)
{
    GlobusGFSName(globus_gridftp_server_get_optimal_concurrency);
    GlobusGFSDebugEnter();

    if(op->data_handle->http_handle)
    {
        *count = 1;
        return;
    }
    if(!op->writing)
    {    
        globus_mutex_lock(&op->session_handle->mutex);
        if(op->data_handle->state == GLOBUS_L_GFS_DATA_HANDLE_INUSE && 
            op->data_handle->is_mine)
        {
            unsigned int num_streams;
            /* query number of currently connected streams and update
            data_info (only if recieving) */
            globus_ftp_control_data_query_channels(
                &op->data_handle->data_channel,
                &num_streams,
                0);
            op->data_handle->info.nstreams = num_streams;
        }
        else
        {
            op->data_handle->info.nstreams = 1;
        }
        globus_mutex_unlock(&op->session_handle->mutex);

        if(op->data_handle->info.nstreams == 0)
        {
            op->data_handle->info.nstreams = 1;
        }
    }
    *count = op->data_handle->info.nstreams * op->stripe_count * 2;

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_get_block_size(
    globus_gfs_operation_t              op,
    globus_size_t *                     block_size)
{
    int                                 concur;
    int                                 tcp_mem_limit;
    GlobusGFSName(globus_gridftp_server_get_block_size);
    GlobusGFSDebugEnter();

    if(op && op->data_handle != NULL && op->data_handle->is_mine)
    {
        *block_size = op->data_handle->info.blocksize;

        tcp_mem_limit = globus_gfs_config_get_int("tcp_mem_limit");

        if(tcp_mem_limit > 0)
        {
            globus_gridftp_server_get_optimal_concurrency(op, &concur);
            tcp_mem_limit = tcp_mem_limit / concur;
            if(tcp_mem_limit < *block_size)
            {
                *block_size = tcp_mem_limit;
            }
        }
    }
    else
    {
        *block_size = (globus_size_t) globus_i_gfs_config_int("blocksize");
    }

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_get_stripe_block_size(
    globus_gfs_operation_t              op,
    globus_size_t *                     stripe_block_size)
{
    GlobusGFSName(globus_gridftp_server_get_stripe_block_size);
    GlobusGFSDebugEnter();
    if(op->data_handle != NULL && op->data_handle->is_mine)
    {
        *stripe_block_size = op->data_handle->info.stripe_blocksize;
    }
    else
    {
        *stripe_block_size =
            (globus_size_t) globus_i_gfs_config_int("stripe_blocksize");
    }
    GlobusGFSDebugExit();
}

void
globus_gridftp_server_get_update_interval(
    globus_gfs_operation_t              op,
    int *                               interval)
{
    *interval = op->update_interval;
}

void
globus_gridftp_server_get_session_uid(
    globus_gfs_operation_t              op,
    uid_t *                             uid)
{
    GlobusGFSName(globus_gridftp_server_get_session_uid);
    GlobusGFSDebugEnter();

    *uid = op->session_handle->uid;

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_get_session_username(
    globus_gfs_operation_t              op,
    char **                             username)
{
    GlobusGFSName(globus_gridftp_server_get_session_username);
    GlobusGFSDebugEnter();

    *username = globus_libc_strdup(op->session_handle->real_username);

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_get_delegated_cred(
    globus_gfs_operation_t              op,
    gss_cred_id_t *                     del_cred)
{
    GlobusGFSName(globus_gridftp_server_get_delegated_cred);
    GlobusGFSDebugEnter();

    *del_cred = op->session_handle->del_cred;

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_get_sec_context(
    globus_gfs_operation_t              op,
    gss_ctx_id_t *                      context)
{
    GlobusGFSName(globus_gridftp_server_get_sec_context);
    GlobusGFSDebugEnter();

    *context = op->session_handle->context;

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_get_config_string(
    globus_gfs_operation_t              op,
    char **                             config_string)
{
    GlobusGFSName(globus_gridftp_server_get_config_string);
    GlobusGFSDebugEnter();

    *config_string = globus_libc_strdup(
        globus_i_gfs_config_string("dsi_options"));

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_get_config_data(
    globus_gfs_operation_t              op,
    char *                              data_id,
    char **                             config_data)
{
    GlobusGFSName(globus_gridftp_server_get_config_data);
    GlobusGFSDebugEnter();

    globus_gfs_log_message(GLOBUS_GFS_LOG_ERR,
        "DSI config data is not supported.\n");

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_set_ordered_data(
    globus_gfs_operation_t              op,
    globus_bool_t                       ordered_data)
{
    GlobusGFSName(globus_gridftp_server_set_ordered_data);
    GlobusGFSDebugEnter();

    op->order_data = ordered_data;

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_get_ordered_data(
    globus_gfs_operation_t              op,
    globus_bool_t *                     ordered_data)
{
    GlobusGFSName(globus_gridftp_server_get_ordered_data);
    GlobusGFSDebugEnter();

    *ordered_data = op->order_data;

    GlobusGFSDebugExit();
}



globus_result_t
globus_gridftp_server_query_op_info(
    globus_gfs_operation_t              op,
    globus_gfs_op_info_t                op_info,
    globus_gfs_op_info_param_t          param,
    ...)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    va_list                             ap;
    
    char ***                            argv;
    int *                               argc;
    GlobusGFSName(globus_gridftp_server_query_op_info);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    if(op_info == NULL)
    {
        res = GlobusGFSErrorGeneric("Invalid op_info.");;
        goto err;
    }

    va_start(ap, param);

    switch(param)
    {
        case GLOBUS_GFS_OP_INFO_CMD_ARGS:
            argv = va_arg(ap, char ***);
            argc = va_arg(ap, int *);
            
            *argv = op_info->argv;
            *argc = op_info->argc;
            break;
        
            default:
                res = GlobusGFSErrorGeneric("Invalid query parameter.");
                break;
    }
    
    va_end(ap);

    if(res != GLOBUS_SUCCESS)
    {
        goto err;
    }

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

err:
    GlobusGFSDebugExitWithError();
    return res;
}        
    


globus_result_t
globus_gridftp_server_add_command(
    globus_gfs_operation_t              op,
    const char *                        command_name,
    int                                 cmd_id,
    int                                 min_args,
    int                                 max_args,
    const char *                        help_string,
    globus_bool_t                       has_pathname,
    int                                 access_type)
{
    globus_i_gfs_cmd_ent_t *            cmd_ent;
    int                                 rc;
    char *                              ptr;
    globus_result_t                     result;
    GlobusGFSName(globus_gridftp_server_add_command);
    GlobusGFSDebugEnter();

    if(cmd_id < GLOBUS_GFS_MIN_CUSTOM_CMD)
    {
        result = GlobusGFSErrorGeneric("Invalid cmd_id.");;
        goto err;
    }
    
    if(op->session_handle->custom_cmd_table == NULL)
    {
        globus_hashtable_init(
            &op->session_handle->custom_cmd_table,
            128,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);
    }
    
    cmd_ent = (globus_i_gfs_cmd_ent_t *)
        globus_calloc(1, sizeof(globus_i_gfs_cmd_ent_t));
    
    cmd_ent->cmd_name = globus_libc_strdup(command_name);
    ptr = cmd_ent->cmd_name;
    while(ptr && *ptr)
    {
        *ptr = toupper(*ptr);
        ptr++;
    }
    cmd_ent->cmd_id = cmd_id;
    cmd_ent->min_argc = min_args;
    cmd_ent->max_argc = max_args;
    cmd_ent->help_str = globus_libc_strdup(help_string);
    cmd_ent->has_pathname = has_pathname;
    cmd_ent->access_type = access_type;
    
    rc = globus_hashtable_insert(
        &op->session_handle->custom_cmd_table, cmd_ent->cmd_name, cmd_ent);

    if(rc != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorGeneric("Could not store command entry.");
        goto err;
    }
    
    GlobusGFSDebugExit();   
    return GLOBUS_SUCCESS;
    
err:
    GlobusGFSDebugExitWithError();   
    return result;
}
    

/* this is used to translate the restart and partial offset/lengths into
    a sets of ranges to transfer... storage interface shouldn't know about
    partial or restart semantics, it only needs to know which offsets to
    read from the data source, and what offset to write to data sink
    (dest offset only matters for mode e, but again, storage interface
    doesn't know about modes)
*/
void
globus_gridftp_server_get_write_range(
    globus_gfs_operation_t   op,
    globus_off_t *                      offset,
    globus_off_t *                      length)
{
    globus_off_t                        tmp_off = 0;
    globus_off_t                        tmp_len = -1;
    globus_off_t                        tmp_write = 0;
    globus_off_t                        tmp_transfer = 0;
    int                                 rc;
    GlobusGFSName(globus_gridftp_server_get_write_range);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    if(globus_range_list_size(op->range_list))
    {
        rc = globus_range_list_remove_at(
            op->range_list,
            0,
            &tmp_off,
            &tmp_len);
        op->order_data_start = tmp_off;
    }
    if(op->data_handle->info.mode == 'S')
    {
        tmp_write = tmp_off;
    }
    if(op->partial_offset > 0)
    {
        tmp_off += op->partial_offset;
        tmp_write += op->partial_offset;
        tmp_transfer = 0 - op->partial_offset;
    }
    if(offset)
    {
        *offset = tmp_off;
    }
    if(length)
    {
        *length = tmp_len;
    }
    op->write_delta = tmp_write;
    op->transfer_delta = tmp_transfer;

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_get_read_range(
    globus_gfs_operation_t              op,
    globus_off_t *                      offset,
    globus_off_t *                      length)
{
    globus_off_t                        tmp_off = 0;
    globus_off_t                        tmp_len = -1;
    globus_off_t                        tmp_write = 0;
    int                                 rc;
    globus_off_t                        start_offset;
    globus_off_t                        end_offset;
    globus_off_t                        stripe_block_size;
    int                                 size;
    int                                 i;
    globus_gfs_transfer_info_t *        info;
    globus_off_t                        part_size;
    GlobusGFSName(globus_gridftp_server_get_read_range);
    GlobusGFSDebugEnter();
 
    globus_l_gfs_data_alive(op->session_handle);
    
    /* this whole function is crazy ugly and inneffiecient...
       needs rethinking */
    globus_mutex_lock(&op->session_handle->mutex);
    {
        if(op->node_count > 1)
        {
            switch(op->data_handle->info.stripe_layout)
            {
                case GLOBUS_GFS_LAYOUT_PARTITIONED:
                    info = (globus_gfs_transfer_info_t *) op->info_struct;
                    if(op->partial_length > 0)
                    {
                        part_size = op->partial_length;
                    }
                    else
                    {
                        part_size = info->alloc_size;
                    }
                    stripe_block_size = part_size / op->node_count;
                    if(part_size <= 0)
                    {
                        stripe_block_size =
                            op->data_handle->info.stripe_blocksize;
                    }
                    else if(part_size % op->node_count)
                    {
                        stripe_block_size++;
                    }
                    break;

                case GLOBUS_GFS_LAYOUT_BLOCKED:
                default:
                    stripe_block_size = op->data_handle->info.stripe_blocksize;
                    break;
            }
            start_offset = op->stripe_chunk * stripe_block_size;
            end_offset = start_offset + stripe_block_size;

            if(globus_range_list_size(op->stripe_range_list))
            {
                rc = globus_range_list_remove_at(
                    op->stripe_range_list,
                    0,
                    &tmp_off,
                    &tmp_len);

                tmp_write = op->write_delta;
            }
            else if((size = globus_range_list_size(op->range_list)) != 0)
            {
                for(i = 0; i < size; i++)
                {
                    rc = globus_range_list_at(
                        op->range_list,
                        i,
                        &tmp_off,
                        &tmp_len);

                    if(op->partial_length != -1)
                    {
                        if(tmp_len == -1)
                        {
                            tmp_len = op->partial_length;
                        }
                        if(tmp_off + tmp_len > op->partial_length)
                        {
                            tmp_len = op->partial_length - tmp_off;
                            if(tmp_len < 0)
                            {
                                tmp_len = 0;
                            }
                        }
                    }

                    if(op->partial_offset > 0)
                    {
                        tmp_off += op->partial_offset;
                        tmp_write = 0 - op->partial_offset;
                    }

                    globus_range_list_insert(
                        op->stripe_range_list, tmp_off, tmp_len);
                    op->write_delta = tmp_write;
                }
                globus_range_list_remove(
                    op->stripe_range_list, 0, start_offset);
                globus_range_list_remove(
                    op->stripe_range_list, end_offset, GLOBUS_RANGE_LIST_MAX);
                op->stripe_chunk += op->node_count;

                if(globus_range_list_size(op->stripe_range_list))
                {
                    rc = globus_range_list_remove_at(
                        op->stripe_range_list,
                        0,
                        &tmp_off,
                        &tmp_len);

                    tmp_write = op->write_delta;
                }
                else
                {
                    tmp_len = 0;
                    tmp_off = 0;
                    tmp_write = 0;
                }
            }
            else
            {
                tmp_len = 0;
            }
        }
        else if(globus_range_list_size(op->range_list))
        {
            rc = globus_range_list_remove_at(
                op->range_list,
                0,
                &tmp_off,
                &tmp_len);

            if(op->partial_length != -1)
            {
                if(tmp_len == -1)
                {
                    tmp_len = op->partial_length;
                }
                if(tmp_off + tmp_len > op->partial_length)
                {
                    tmp_len = op->partial_length - tmp_off;
                    if(tmp_len < 0)
                    {
                        tmp_len = 0;
                    }
                }
            }

            if(op->partial_offset > 0)
            {
                tmp_off += op->partial_offset;
                if(op->data_handle->info.mode == 'E')
                {
                    tmp_write = 0 - op->partial_offset;
                }
            }
        }
        else
        {
            tmp_len = 0;
        }

    }
    globus_mutex_unlock(&op->session_handle->mutex);
    if(offset)
    {
        *offset = tmp_off;
    }
    if(length)
    {
        *length = tmp_len;
    }
    op->write_delta = tmp_write;

    GlobusGFSDebugExit();
}

globus_result_t
globus_gridftp_server_get_recv_modification_time(
    globus_gfs_operation_t              op,
    time_t *                            out_time)
{    
    globus_result_t                     result;
    time_t                              tmp_time = -1;
    GlobusGFSName(globus_gridftp_server_get_recv_modification_time);
    GlobusGFSDebugEnter();

    if(!op || !out_time)
    {
        result = GlobusGFSErrorGeneric("Invalid parameters.");
        goto error;
    }
    
    if(op->storattr && op->storattr->modify)
    {
        char* tz;
        struct tm modtime;
        memset(&modtime, 0, sizeof(modtime));
        if (sscanf(op->storattr->modify, "%4d%2d%2d%2d%2d%2d", 
                    &modtime.tm_year, &modtime.tm_mon, &modtime.tm_mday,
                    &modtime.tm_hour, &modtime.tm_min, &modtime.tm_sec) != 6)
        {
            result = GlobusGFSErrorGeneric("Invalid modification time.");
            goto error;
        }
        modtime.tm_year -= 1900;
        modtime.tm_mon  -= 1;
        /* This block converts the user-specified UTC time to a Unix time
         * value.  We have to do contortions here as there is no standard
         * inverse of the 'gmtime' function. */
        tz = getenv("TZ");
        globus_libc_setenv("TZ", "UTC", 1);
        tzset();
        tmp_time = mktime(&modtime);
        if (tz)
            globus_libc_setenv("TZ", tz, 1);
        else
            globus_libc_unsetenv("TZ");
        tzset();
                                
        op->storattr->modify_seen = GLOBUS_TRUE;
    }
    *out_time = tmp_time;
    
    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusGFSDebugExitWithError();
    *out_time = -1;
    return result;
}
    
    

globus_result_t
globus_gridftp_server_get_recv_attr_string(
    globus_gfs_operation_t              op,
    const char *                        requested_attr,
    char **                             out_value)
{
    globus_result_t                     result;
    char *                              tmp_val = NULL;
    char *                              tmp_req;
    GlobusGFSName(globus_gridftp_server_get_recv_attr_string);
    GlobusGFSDebugEnter();

    if(!op || !out_value)
    {
        result = GlobusGFSErrorGeneric("Invalid parameters.");
        goto error;
    }
    
    if(op->storattr)
    {
        if(requested_attr)
        {
            if(strcasecmp(requested_attr, "modify") == 0)
            {
                tmp_val = globus_libc_strdup(op->storattr->modify);
                op->storattr->modify_seen = GLOBUS_TRUE;
            }
            else if(strcasecmp(requested_attr, "checksum.md5") == 0)
            {
                tmp_val = globus_libc_strdup(op->storattr->checksum_md5);
                op->storattr->checksum_md5_seen = GLOBUS_TRUE;
            }
            else
            {
                tmp_val = globus_i_gfs_kv_getval(
                    op->storattr->all, requested_attr, 0);
                if(!tmp_val)
                {
                    tmp_req = globus_common_create_string("x.%s", requested_attr);
                    tmp_val = globus_i_gfs_kv_getval(
                        op->storattr->all, requested_attr, 0);
                    globus_free(tmp_req);
                }
            }
        }
        else
        {
            tmp_val = globus_libc_strdup(op->storattr->all);
        }
    }
    *out_value = tmp_val;
    
    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusGFSDebugExitWithError();
    return result;
}
    

globus_result_t
globus_gridftp_server_register_read(
    globus_gfs_operation_t              op,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_gridftp_server_read_cb_t     callback,
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_l_gfs_data_bounce_t *        bounce_info;
    GlobusGFSName(globus_gridftp_server_register_read);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);
    bounce_info = (globus_l_gfs_data_bounce_t *)
        globus_malloc(sizeof(globus_l_gfs_data_bounce_t));
    if(!bounce_info)
    {
        result = GlobusGFSErrorMemory("bounce_info");
        goto error_alloc;
    }

    bounce_info->op = op;
    bounce_info->callback.read = callback;
    bounce_info->user_arg = user_arg;

    if(op->data_handle->http_handle)
    {
        result = globus_xio_register_read(
            op->data_handle->http_handle,
            buffer,
            length,
            length,
            NULL,
            globus_i_gfs_data_http_read_cb,
            bounce_info);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_ftp_control_data_read", result);
            goto error_register;
        }
    }
    else
    {
    result = globus_ftp_control_data_read(
        &op->data_handle->data_channel,
        buffer,
        length,
        globus_l_gfs_data_read_cb,
        bounce_info);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_ftp_control_data_read", result);
        goto error_register;
    }
    }

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error_register:
    globus_free(bounce_info);

error_alloc:
    GlobusGFSDebugExitWithError();
    return result;
}


globus_result_t
globus_gridftp_server_register_write(
    globus_gfs_operation_t   op,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_off_t                        offset,
    int                                 stripe_ndx,
    globus_gridftp_server_write_cb_t    callback,
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_l_gfs_data_bounce_t *        bounce_info;
    GlobusGFSName(globus_gridftp_server_register_write);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);
    bounce_info = (globus_l_gfs_data_bounce_t *)
        globus_malloc(sizeof(globus_l_gfs_data_bounce_t));
    if(!bounce_info)
    {
        result = GlobusGFSErrorMemory("bounce_info");
        goto error_alloc;
    }

    bounce_info->op = op;
    bounce_info->callback.write = callback;
    bounce_info->user_arg = user_arg;

    if(op->data_handle->info.mode == 'E' && op->stripe_count > 1)
    {
        /* XXX not sure what this is all about */
        globus_mutex_lock(&op->session_handle->mutex);
        {
            if(stripe_ndx != -1)
            {
                op->write_stripe = stripe_ndx;
            }
            if(op->write_stripe >= op->stripe_count)
            {
                op->write_stripe %= op->stripe_count;
            }
            result = globus_ftp_control_data_write_stripe(
                &op->data_handle->data_channel,
                buffer,
                length,
                offset + op->write_delta,
                GLOBUS_FALSE,
                op->write_stripe,
                globus_l_gfs_data_write_cb,
                bounce_info);

            op->write_stripe++;
        }
        globus_mutex_unlock(&op->session_handle->mutex);
    }
    else
    {
        if(op->data_handle->http_handle)
        {
            result = globus_xio_register_write(
                op->data_handle->http_handle,
                buffer,
                length,
                length,
                NULL,
                globus_i_gfs_data_http_write_cb,
                bounce_info);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusGFSErrorWrapFailed(
                    "globus_ftp_control_data_read", result);
                goto error_register;
            }
        }
        else
        {
        result = globus_ftp_control_data_write(
            &op->data_handle->data_channel,
            buffer,
            length,
            offset + op->write_delta,
            GLOBUS_FALSE,
            globus_l_gfs_data_write_cb,
            bounce_info);
    }
    }
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_ftp_control_data_write", result);
        goto error_register;
    }

    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

error_register:
    globus_free(bounce_info);

error_alloc:
    GlobusGFSDebugExitWithError();
    return result;
}

void
globus_gridftp_server_finished_session_start(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    void *                              session_arg,
    char *                              username,
    char *                              home_dir)
{
    globus_gfs_finished_info_t          finished_info;
    int                                 code;
    GlobusGFSName(globus_gridftp_server_finished_session_start);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    memset(&finished_info, '\0', sizeof(globus_gfs_finished_info_t));
    finished_info.type = GLOBUS_GFS_OP_SESSION_START;
    finished_info.result = result;
    finished_info.info.session.session_arg = session_arg;
    finished_info.info.session.username = username;
    finished_info.info.session.home_dir = home_dir;

    /* pull response code from error */
    if(result != GLOBUS_SUCCESS && 
        (code = globus_gfs_error_get_ftp_response_code(
            globus_error_peek(result))) != 0)
    {
        finished_info.code = code;
        finished_info.msg = globus_error_print_friendly(
            globus_error_peek(result));
    }

    globus_gridftp_server_operation_finished(
        op,
        result,
        &finished_info);

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_finished_active_data(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    void *                              data_arg,
    globus_bool_t                       bi_directional)
{
    globus_gfs_finished_info_t          finished_info;
    int                                 code;
    GlobusGFSName(globus_gridftp_server_finished_active_data);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    memset(&finished_info, '\0', sizeof(globus_gfs_finished_info_t));
    finished_info.type = GLOBUS_GFS_OP_ACTIVE;
    finished_info.result = result;
    finished_info.info.data.data_arg = data_arg;
    finished_info.info.data.bi_directional = bi_directional;

    /* pull response code from error */
    if(result != GLOBUS_SUCCESS && 
        (code = globus_gfs_error_get_ftp_response_code(
            globus_error_peek(result))) != 0)
    {
        finished_info.code = code;
        finished_info.msg = globus_error_print_friendly(
            globus_error_peek(result));
    }

    globus_gridftp_server_operation_finished(
        op,
        result,
        &finished_info);

    GlobusGFSDebugExit();
}

void
globus_gridftp_server_finished_passive_data(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    void *                              data_arg,
    globus_bool_t                       bi_directional,
    const char **                       contact_strings,
    int                                 cs_count)
{
    globus_gfs_finished_info_t          finished_info;
    int                                 code;
    GlobusGFSName(globus_gridftp_server_finished_passive_data);
    GlobusGFSDebugEnter();

    globus_l_gfs_data_alive(op->session_handle);

    memset(&finished_info, '\0', sizeof(globus_gfs_finished_info_t));
    finished_info.type = GLOBUS_GFS_OP_PASSIVE;
    finished_info.result = result;
    finished_info.info.data.data_arg = data_arg;
    finished_info.info.data.bi_directional = bi_directional;
    finished_info.info.data.contact_strings = contact_strings;
    finished_info.info.data.cs_count = cs_count;

    /* pull response code from error */
    if(result != GLOBUS_SUCCESS && 
        (code = globus_gfs_error_get_ftp_response_code(
            globus_error_peek(result))) != 0)
    {
        finished_info.code = code;
        finished_info.msg = globus_error_print_friendly(
            globus_error_peek(result));
    }

    globus_gridftp_server_operation_finished(
        op,
        result,
        &finished_info);

    GlobusGFSDebugExit();
}

void
globus_i_gfs_data_request_set_cred(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    gss_cred_id_t                       del_cred)
{
    globus_l_gfs_data_session_t *       session_handle;
    GlobusGFSName(globus_i_gfs_data_request_set_cred);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *) session_arg;
    globus_l_gfs_data_reset_watchdog(session_handle, NULL);
    
    if(del_cred != NULL)
    {
        session_handle->del_cred = del_cred;
    }
    if(session_handle->dsi->set_cred_func != NULL)
    {
        session_handle->dsi->set_cred_func(
            del_cred, session_handle->session_arg);
    }

    GlobusGFSDebugExit();
    return;
}


/* this end receives the buffer */
void
globus_i_gfs_data_request_buffer_send(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    globus_byte_t *                     buffer,
    int                                 buffer_type,
    globus_size_t                       buffer_len)
{
    globus_l_gfs_data_session_t *       session_handle;
    GlobusGFSName(globus_i_gfs_data_request_buffer_send);
    GlobusGFSDebugEnter();

    session_handle = (globus_l_gfs_data_session_t *) session_arg;
    globus_l_gfs_data_reset_watchdog(session_handle, NULL);
    
    if(buffer_type & GLOBUS_GFS_BUFFER_SERVER_DEFINED)
    {
        switch(buffer_type)
        {
            case GLOBUS_GFS_BUFFER_EOF_INFO:
                break;
            default:
                break;
        }
    }

    if(session_handle->dsi->buffer_send_func != NULL)
    {
        session_handle->dsi->buffer_send_func(
            buffer_type, buffer, buffer_len, session_handle->session_arg);
    }

    GlobusGFSDebugExit();
    return;
}






void
globus_i_gfs_data_http_read_cb(
    globus_xio_handle_t                 xio_handle, 
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_size_t                       nbytes, 
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_off_t                        offset;
    globus_l_gfs_data_bounce_t *        bounce_info;
    globus_bool_t                       eof;
    GlobusGFSName(globus_l_gfs_data_http_read_cb);
    GlobusGFSDebugEnter();
    bounce_info = (globus_l_gfs_data_bounce_t *) user_arg;

    offset = bounce_info->op->bytes_transferred;
    bounce_info->op->bytes_transferred += nbytes;
    
    eof = globus_xio_error_is_eof(result);
    if(eof)
    {        
        if(bounce_info->op->bytes_transferred < 
            bounce_info->op->data_handle->http_length)
        {
            result = GlobusGFSErrorGeneric(
                "HTTP data length was shorter than expected.");
        }
        else
        {
            result = GLOBUS_SUCCESS;
        }
    }
    if(bounce_info->op->bytes_transferred > 
        bounce_info->op->data_handle->http_length)
    {
        result = GlobusGFSErrorGeneric(
            "HTTP data length was longer than expected.");
    }

    bounce_info->callback.read(
        bounce_info->op,
        result,
        buffer,
        nbytes,
        offset + bounce_info->op->write_delta,
        eof,
        bounce_info->user_arg);

    globus_free(bounce_info);

    GlobusGFSDebugExit();
}


void
globus_i_gfs_data_http_write_cb(
    globus_xio_handle_t                 xio_handle, 
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_size_t                       nbytes, 
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_l_gfs_data_bounce_t *        bounce_info;
    GlobusGFSName(globus_l_gfs_data_http_read_cb);
    GlobusGFSDebugEnter();
    bounce_info = (globus_l_gfs_data_bounce_t *) user_arg;

    globus_mutex_lock(&bounce_info->op->session_handle->mutex);
    {
        bounce_info->op->bytes_transferred += nbytes;
        bounce_info->op->recvd_bytes += nbytes;
        bounce_info->op->data_handle->http_transferred += nbytes;
    }
    globus_mutex_unlock(&bounce_info->op->session_handle->mutex);

    bounce_info->callback.write(
        bounce_info->op,
        result,
        buffer,
        nbytes,
        bounce_info->user_arg);

    globus_free(bounce_info);

    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_http_event_cb(
    globus_gfs_data_event_reply_t *     reply,
    void *                              user_arg);

static
void
globus_l_gfs_data_http_perf(
    void *                              arg)
{
    globus_l_gfs_data_operation_t *       op;
    
    op = (globus_l_gfs_data_operation_t *) arg;
    
    if(op->data_handle)
    {
        globus_gfs_event_info_t        event_reply;
        memset(&event_reply, '\0', sizeof(globus_gfs_event_info_t));
        event_reply.type = GLOBUS_GFS_EVENT_BYTES_RECVD;

        globus_l_gfs_data_http_event_cb(&event_reply, op);
    }
}
static
void
globus_l_gfs_data_http_transfer_cb(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg)
{
    globus_l_gfs_data_operation_t *     op;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GlobusGFSName(globus_l_gfs_data_http_transfer_cb);
    GlobusGFSDebugEnter();
    op = (globus_l_gfs_data_operation_t *) user_arg;

    {
        globus_bool_t                   destroy_op = GLOBUS_FALSE;
        globus_bool_t                   destroy_session = GLOBUS_FALSE;
        globus_mutex_lock(&op->session_handle->mutex);
        op->session_handle->ref--;
        GFSDataOpDec(op, destroy_op, destroy_session);
        globus_mutex_unlock(&op->session_handle->mutex);
        globus_assert(!destroy_op);
    }
    
    if(op->data_handle->perf_handle != GLOBUS_NULL_HANDLE)
    {
        globus_callback_unregister(
            op->data_handle->perf_handle, NULL, NULL, NULL);
        op->data_handle->perf_handle = GLOBUS_NULL_HANDLE;
    }
    
    globus_xio_close(op->data_handle->http_handle, NULL);
    globus_libc_unsetenv("GLOBUS_GFS_EXTRA_CA_CERTS");

    if(reply->result != GLOBUS_SUCCESS || result != GLOBUS_SUCCESS)
    {
        globus_gridftp_server_finished_command(op, reply->result, NULL);
    }
    else
    {
        char *                          count;
        globus_mutex_lock(&op->session_handle->mutex);
        {            
            count = globus_common_create_string("%"GLOBUS_OFF_T_FORMAT,
                op->data_handle->http_transferred);
            op->data_handle->http_transferred = 0;
        }
        globus_mutex_unlock(&op->session_handle->mutex);
        globus_gridftp_server_intermediate_command(op, result, count);
        globus_free(count);

        globus_gridftp_server_finished_command(
            op, reply->result, op->data_handle->http_response_str);
    }
    GlobusGFSDebugExit();
}

static
void
globus_l_gfs_data_http_event_cb(
    globus_gfs_data_event_reply_t *     reply,
    void *                              user_arg)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char *                              count;
    globus_l_gfs_data_operation_t *     op;
    
    GlobusGFSName(globus_l_gfs_data_http_event_cb);
    GlobusGFSDebugEnter();

    op = (globus_l_gfs_data_operation_t *) user_arg;

    switch(reply->type)
    {
        case GLOBUS_GFS_EVENT_TRANSFER_BEGIN:
            globus_gridftp_server_intermediate_command(op, result, "0");
       
            if(op->data_handle->perf_handle == GLOBUS_NULL_HANDLE)
            {
                globus_reltime_t                timer;
                GlobusTimeReltimeSet(timer, 5, 0);
                globus_callback_register_periodic(
                    &op->data_handle->perf_handle,
                    &timer,
                    &timer,
                    globus_l_gfs_data_http_perf,
                    (void *) op);
            }
            break;
        
        case GLOBUS_GFS_EVENT_TRANSFER_CONNECTED:
            break;
        
        case GLOBUS_GFS_EVENT_DISCONNECTED:
            break;
        
        case GLOBUS_GFS_EVENT_BYTES_RECVD:
            globus_mutex_lock(&op->session_handle->mutex);
            {            
                count = globus_common_create_string("%"GLOBUS_OFF_T_FORMAT,
                    op->data_handle->http_transferred);
            }
            globus_mutex_unlock(&op->session_handle->mutex);
            globus_gridftp_server_intermediate_command(
                op, result, count);
            globus_free(count);

            break;
        
        case GLOBUS_GFS_EVENT_RANGES_RECVD:
            break;
        
        default:
            globus_assert(0 && "Unexpected event type");
            break;
    }

    GlobusGFSDebugExit();
}


globus_result_t
globus_i_gfs_data_http_parse_args(
    char *                              argstring,
    char **                             path,
    char **                             request,
    globus_off_t *                      offset,
    globus_off_t *                      length)
{
    int                                 rc;
    globus_off_t                        result;
    char *                              tmp_val;
    globus_off_t                        tmp_len;
    char *                              tmp_req = NULL;
    char *                              tmp_path = NULL;
    globus_off_t                        tmp_off;
    int                                 tmp_consume;
    GlobusGFSName(globus_i_gfs_data_http_parse_args);
    GlobusGFSDebugEnter();
        
    if((tmp_val = globus_i_gfs_kv_getval(argstring, "OFFSET", 0)) == NULL)
    {
        result = GlobusGFSErrorGeneric(
            "Invalid arguments: Missing OFFSET.");
        goto err;
    }
    rc = globus_libc_scan_off_t(tmp_val, &tmp_off, &tmp_consume);
    if(rc < 1 || strlen(tmp_val) != tmp_consume || tmp_off < 0)
    {
        result = GlobusGFSErrorGeneric(
            "Invalid arguments: Invalid OFFSET.");
        goto err;
    }
                    
    if((tmp_val = globus_i_gfs_kv_getval(argstring, "LENGTH", 0)) == NULL)
    {
        result = GlobusGFSErrorGeneric(
            "Invalid arguments: Missing LENGTH.");
        goto err;
    }
    rc = globus_libc_scan_off_t(tmp_val, &tmp_len, &tmp_consume);
    if(rc < 1 || strlen(tmp_val) != tmp_consume || tmp_len < 0)
    {
        result = GlobusGFSErrorGeneric(
            "Invalid arguments: Invalid LENGTH.");
        goto err;
    }

    if((tmp_val = globus_i_gfs_kv_getval(argstring, "PATH", 1)) == NULL)
    {
        result = GlobusGFSErrorGeneric(
            "Invalid arguments: Missing PATH.");
        goto err;
    }
    tmp_path = tmp_val;

    if((tmp_val = globus_i_gfs_kv_getval(argstring, "REQUEST", 0)) == NULL)
    {
        result = GlobusGFSErrorGeneric(
            "Invalid arguments: Missing REQUEST.");
        goto err;
    }
    tmp_req = tmp_val;

    *offset = tmp_off;
    *length = tmp_len;
    *request = tmp_req;
    *path = tmp_path;
    
    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;

err:
    if(tmp_path)
    {
        globus_free(tmp_path);
    }
    if(tmp_req)
    {
        globus_free(tmp_req);
    }
    GlobusGFSDebugExitWithError();
    return result;
}

#define GFS_L_S3_ROOT_CA                                                    \
    "-----BEGIN CERTIFICATE-----\n"                                         \
    "MIICPDCCAaUCEDyRMcsf9tAbDpq40ES/Er4wDQYJKoZIhvcNAQEFBQAwXzELMAkG\n"    \
    "A1UEBhMCVVMxFzAVBgNVBAoTDlZlcmlTaWduLCBJbmMuMTcwNQYDVQQLEy5DbGFz\n"    \
    "cyAzIFB1YmxpYyBQcmltYXJ5IENlcnRpZmljYXRpb24gQXV0aG9yaXR5MB4XDTk2\n"    \
    "MDEyOTAwMDAwMFoXDTI4MDgwMjIzNTk1OVowXzELMAkGA1UEBhMCVVMxFzAVBgNV\n"    \
    "BAoTDlZlcmlTaWduLCBJbmMuMTcwNQYDVQQLEy5DbGFzcyAzIFB1YmxpYyBQcmlt\n"    \
    "YXJ5IENlcnRpZmljYXRpb24gQXV0aG9yaXR5MIGfMA0GCSqGSIb3DQEBAQUAA4GN\n"    \
    "ADCBiQKBgQDJXFme8huKARS0EN8EQNvjV69qRUCPhAwL0TPZ2RHP7gJYHyX3KqhE\n"    \
    "BarsAx94f56TuZoAqiN91qyFomNFx3InzPRMxnVx0jnvT0Lwdd8KkMaOIG+YD/is\n"    \
    "I19wKTakyYbnsZogy1Olhec9vn2a/iRFM9x2Fe0PonFkTGUugWhFpwIDAQABMA0G\n"    \
    "CSqGSIb3DQEBBQUAA4GBABByUqkFFBkyCEHwxWsKzH4PIRnN5GfcX6kb5sroc50i\n"    \
    "2JhucwNhkcV8sEVAbkSdjbCxlnRhLQ2pRdKkkirWmnWXbj9T/UWZYB2oK0z5XqcJ\n"    \
    "2HUw19JlYD1n1khVdWk/kfVIC0dpImmClr7JyDiGSnoscxlIaU5rfGW/D/xwzoiQ\n"    \
    "-----END CERTIFICATE-----"

globus_result_t
globus_i_gfs_data_http_init(
    globus_l_gfs_data_session_t *       session_handle,
    globus_bool_t                       https,
    globus_xio_handle_t *               handle,
    globus_xio_attr_t *                 attr, 
    globus_xio_data_descriptor_t *      descriptor)
{
    globus_result_t                     result;
    GlobusGFSName(globus_i_gfs_data_http_init);
    GlobusGFSDebugEnter();

    if(!gfs_l_tcp_driver)
    {
        result = globus_xio_driver_load("tcp", &gfs_l_tcp_driver);
        globus_assert(result == GLOBUS_SUCCESS);
    }
    if(!session_handle->http_driver)
    {
        result = globus_xio_driver_load("http", &session_handle->http_driver);
        globus_assert(result == GLOBUS_SUCCESS);
    }
    if(!gfs_l_gsi_driver)
    {
        result = globus_xio_driver_load("gsi", &gfs_l_gsi_driver);
        globus_assert(result == GLOBUS_SUCCESS);
    }
    if(!gfs_l_q_driver)
    {
        result = globus_xio_driver_load("queue", &gfs_l_q_driver);
        globus_assert(result == GLOBUS_SUCCESS);
    }
    if(!session_handle->http_stack)
    {
        globus_xio_stack_init(&session_handle->http_stack, NULL);
    
        result = globus_xio_stack_push_driver(
                session_handle->http_stack,
                gfs_l_tcp_driver);
        globus_assert(result == GLOBUS_SUCCESS);
        result = globus_xio_stack_push_driver(
                session_handle->http_stack,
                session_handle->http_driver);
        globus_assert(result == GLOBUS_SUCCESS);
        result = globus_xio_stack_push_driver(
                session_handle->http_stack,
                gfs_l_q_driver);
        globus_assert(result == GLOBUS_SUCCESS);
    }
    if(!session_handle->https_stack)
    {
        globus_xio_stack_init(&session_handle->https_stack, NULL);
    
        result = globus_xio_stack_push_driver(
                session_handle->https_stack,
                gfs_l_tcp_driver);
        globus_assert(result == GLOBUS_SUCCESS);
        result = globus_xio_stack_push_driver(
                session_handle->https_stack,
                gfs_l_gsi_driver);
        globus_assert(result == GLOBUS_SUCCESS);
        result = globus_xio_stack_push_driver(
                session_handle->https_stack,
                session_handle->http_driver);
        globus_assert(result == GLOBUS_SUCCESS);
        result = globus_xio_stack_push_driver(
                session_handle->https_stack,
                gfs_l_q_driver);
        globus_assert(result == GLOBUS_SUCCESS);
    }
    if(handle && *handle == NULL)
    {
        if(https)
        {
            result = globus_xio_handle_create(
                handle, session_handle->https_stack);
        }
        else
        {
            result = globus_xio_handle_create(
                handle, session_handle->http_stack);
        }
            
        globus_assert(result == GLOBUS_SUCCESS);
    }
    if(attr)
    {
        globus_xio_attr_init(attr);

        result = globus_xio_attr_cntl(
            *attr,
            gfs_l_tcp_driver,
            GLOBUS_XIO_TCP_SET_NODELAY,
            GLOBUS_TRUE);
        globus_assert(result == GLOBUS_SUCCESS);
        if(https)
        {
            result = globus_xio_attr_cntl(
                *attr, 
                gfs_l_gsi_driver, 
                GLOBUS_XIO_GSI_SET_SSL_COMPATIBLE,
                GLOBUS_TRUE);
            globus_assert(result == GLOBUS_SUCCESS);
            result = globus_xio_attr_cntl(
                *attr, 
                gfs_l_gsi_driver, 
                GLOBUS_XIO_GSI_SET_ANON);
            globus_assert(result == GLOBUS_SUCCESS);
            result = globus_xio_attr_cntl(
                *attr, 
                gfs_l_gsi_driver, 
                GLOBUS_XIO_GSI_SET_ALLOW_MISSING_SIGNING_POLICY,
                GLOBUS_TRUE);
            globus_assert(result == GLOBUS_SUCCESS);
            if(!session_handle->http_config_called)
            {
                globus_libc_setenv("GLOBUS_GFS_EXTRA_CA_CERTS", GFS_L_S3_ROOT_CA, 1);
            }
            else if(session_handle->http_ca_certs)
            {
                globus_libc_setenv("GLOBUS_GFS_EXTRA_CA_CERTS", 
                    session_handle->http_ca_certs, 1);
            }
        }
    }

    result = globus_xio_data_descriptor_init(descriptor, *handle);
    globus_assert(result == GLOBUS_SUCCESS);
    
    GlobusGFSDebugExit();
    return GLOBUS_SUCCESS;
}


globus_result_t
globus_i_gfs_http_data_parse_request(
    char *                              request,
    char **                             url,
    globus_bool_t *                     https,
    int *                               http_ver,
    char **                             method,
    globus_xio_http_header_t **         headers, 
    int *                               count)
{
    globus_result_t                     res;
    char *                              d_req = NULL;
    globus_size_t                       req_len;
    char *                              line;
    char *                              next_line;
    char *                              ptr;
    char *                              enc_path;
    char *                              enc_url;
    char *                              tmp_url;
    char *                              tmp_hname;
    char *                              tmp_hval;
    globus_xio_http_header_t *          tmp_headers;
    char *                              tmp_method;
    globus_bool_t                       tmp_ver = GLOBUS_XIO_HTTP_VERSION_1_0;
    int                                 cnt = 20;
    int                                 i;
    GlobusGFSName(globus_i_gfs_http_data_parse_request);
    GlobusGFSDebugEnter();

    d_req = malloc(strlen(request));
    res = globus_l_gfs_base64_decode(
            (const globus_byte_t *) request, (globus_byte_t *) d_req, &req_len);
    if(res != GLOBUS_SUCCESS)
    {
        res = GlobusGFSErrorGeneric("Could not decode.");
        goto err;
    }
    
    res = -1;
    
    /* 
    parse from 
      VERB {http|https}://hostname[:port]/url-encoded-path[?qs] HTTP/1.1\r\n
    */
    line = d_req;
    ptr = strstr(line, "\r\n");
    if(ptr)
    {
        *ptr = 0;
        next_line = ptr + 2;
    }
    ptr = strchr(line, ' ');
    if(!ptr)
    {
        res = GlobusGFSErrorGeneric("Invalid first line.");
        goto err;
    }
    *ptr++ = 0;
    tmp_method = strdup(line);    
    tmp_url = strdup(ptr);
    ptr = strchr(tmp_url, ' ');
    if(!ptr)
    {
        res = GlobusGFSErrorGeneric("Invalid first line.");
        goto err;
    }
    *ptr++ = 0;
    if(strstr(ptr, "1.1"))
    {
        tmp_ver = GLOBUS_XIO_HTTP_VERSION_1_1;
    }
    
    tmp_headers = globus_malloc(cnt * sizeof(globus_xio_http_header_t));
    /* parse headers
      Header-1: foo \r\n
      Header-2: baz \r\n
      \r\n
    */
    i = 0;
    line = next_line;
    while(line && line < d_req + req_len)
    {
        ptr = strstr(line, "\r\n");
        if(ptr)
        {
            if(ptr == line)
            {
                line = NULL;
                continue;
            }
            *ptr = 0;
            next_line = ptr + 2;
        }
        
        ptr = strchr(line, ':');
        if(!ptr)
        {
            GlobusGFSErrorGenericStr(res, ("Invalid header line %d", i));
            goto err;
        }
        *ptr = 0;
        ptr++;
        ptr++;
        tmp_hname = strdup(line);
        tmp_hval = strdup(ptr);
        
        if(i >= cnt)
        {
            cnt += 20;
            tmp_headers = globus_realloc(
                tmp_headers, cnt * sizeof(globus_xio_http_header_t));
        }
                    
        tmp_headers[i].name = tmp_hname;
        tmp_headers[i].value = tmp_hval;
        i++;
        
        
        line = next_line;
    }

    if(strncasecmp(tmp_url, "https://", 8) == 0)
    {
        *https = GLOBUS_TRUE;
    }
    else
    {
        *https = GLOBUS_FALSE;
    }
    
    /* find third / in scheme://host/ to get path */
    ptr = strchr(tmp_url, '/');
    if(!ptr)
    {
        res = GlobusGFSErrorGeneric("Invalid URI.");
        goto err;
    }
    ptr++;
    ptr = strchr(ptr, '/');
    if(!ptr)
    {
        res = GlobusGFSErrorGeneric("Invalid URI.");
        goto err;
    }
    ptr++;
    ptr = strchr(ptr, '/');
    if(!ptr)
    {
        res = GlobusGFSErrorGeneric("Invalid URI.");
        goto err;
    }
    
    /* urlencode path in because xio will decode it */
    enc_path = globus_url_string_hex_encode(ptr, "<>:@");
    *ptr = 0;
    enc_url = globus_common_create_string("%s%s", tmp_url, enc_path);

    *url = enc_url;
    *headers = tmp_headers;
    *count = i;
    *method = tmp_method;
    *http_ver = tmp_ver;
    
    globus_free(tmp_url);
    globus_free(d_req);

    GlobusGFSDebugExit();
    
    return GLOBUS_SUCCESS;
err:
    GlobusGFSDebugExitWithError();
    return res;   
}


globus_result_t 
globus_i_gfs_data_http_get(
    globus_l_gfs_data_operation_t *     op,
    char *                              path,
    char *                              request,
    globus_off_t                        offset,
    globus_off_t                        length,
    globus_bool_t                       do_retry)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char *                              err_str;
    char *                              header_str;
    int                                 i;
    globus_xio_http_header_t *          headers;
    globus_xio_data_descriptor_t        descriptor;
    globus_byte_t                       buffer[1];
    globus_xio_handle_t                 handle;
    int                                 status_code;
    char *                              reason_phrase;
    globus_xio_attr_t                   attr;
    char *                              url;
    globus_l_gfs_data_handle_t *        data_handle;
    globus_gfs_transfer_info_t *        recv_info;
    int                                 count;
    globus_bool_t                       https;
    globus_hashtable_t                  header_table = NULL;
    char *                              method;
    int                                 http_ver;
    globus_bool_t                       eof;
    globus_bool_t                       retry = GLOBUS_FALSE;
    GlobusGFSName(globus_l_gfs_data_http_get);
    GlobusGFSDebugEnter();
    
    /* parse url, headers */
    result = globus_i_gfs_http_data_parse_request(
        request, &url, &https, &http_ver, &method, &headers, &count);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("Invalid request. Parsing", result);
        goto response_exit;
    }
    
    handle = op->session_handle->http_handle;
    result = globus_i_gfs_data_http_init(op->session_handle,
        https, &handle, &attr, &descriptor);
    if(result != GLOBUS_SUCCESS)
    {
        goto response_exit;
    }
    
    result = globus_xio_attr_cntl(
        attr,
        op->session_handle->http_driver,
        GLOBUS_XIO_HTTP_ATTR_SET_REQUEST_HTTP_VERSION,
        http_ver);
    if(result != GLOBUS_SUCCESS)
    {
        goto response_exit;
    }

    result = globus_xio_attr_cntl(
        attr,
        op->session_handle->http_driver,
        GLOBUS_XIO_HTTP_ATTR_SET_REQUEST_METHOD,
        method);
    if(result != GLOBUS_SUCCESS)
    {
        goto response_exit;
    }
        
    /* set individual headers */    
    for (i = 0; i < count; i++)
    {
        result = globus_xio_attr_cntl(
                attr,
                op->session_handle->http_driver,
                GLOBUS_XIO_HTTP_ATTR_SET_REQUEST_HEADER,
                headers[i].name,
                headers[i].value);
        if(result != GLOBUS_SUCCESS)
        {
            goto response_exit;
        }
    }

    result = globus_xio_open(handle, url, attr);
    if(result != GLOBUS_SUCCESS)
    {
        retry = do_retry && (globus_error_get_type(globus_error_peek(result)) ==
                    GLOBUS_XIO_HTTP_ERROR_PERSISTENT_CONNECTION_DROPPED);
            
        if(!retry)
        {        
            result = GlobusGFSErrorWrapFailed("HTTP connection", result);
            goto response_exit;
        }
    }
    if(retry)
    {
        op->session_handle->http_handle = NULL;
        globus_xio_driver_unload(op->session_handle->http_driver);
        op->session_handle->http_driver = NULL;
        if(op->session_handle->https_stack)
        {
            globus_xio_stack_destroy(op->session_handle->https_stack);
            op->session_handle->https_stack = NULL;
        }
        if(op->session_handle->http_stack)
        {
            globus_xio_stack_destroy(op->session_handle->http_stack);
            op->session_handle->http_stack = NULL;
        }

        return globus_i_gfs_data_http_get(
            op, path, request, offset, length, GLOBUS_FALSE);
    }
        
    /* read response, no data */
    result = globus_xio_read(
            handle,
            buffer,
            0,
            0,
            NULL,
            descriptor);
    eof = globus_xio_error_is_eof(result);
    if(eof)
    {
        retry = do_retry && (globus_error_get_type(globus_error_peek(result)) ==
                    GLOBUS_XIO_HTTP_ERROR_PERSISTENT_CONNECTION_DROPPED);
        if(!retry)
        {
            result = GLOBUS_SUCCESS;
        }
    }
    
    if(retry)
    {
        globus_xio_close(op->session_handle->http_handle, NULL);
        op->session_handle->http_handle = NULL;
        globus_xio_driver_unload(op->session_handle->http_driver);
        op->session_handle->http_driver = NULL;
        if(op->session_handle->https_stack)
        {
            globus_xio_stack_destroy(op->session_handle->https_stack);
            op->session_handle->https_stack = NULL;
        }
        if(op->session_handle->http_stack)
        {
            globus_xio_stack_destroy(op->session_handle->http_stack);
            op->session_handle->http_stack = NULL;
        }

        return globus_i_gfs_data_http_get(
            op, path, request, offset, length, GLOBUS_FALSE);
    }
    
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("Before getting response, HTTP connection", result);
        goto open_exit;
    }

    /* check response */
    result = globus_xio_data_descriptor_cntl(
            descriptor,
            op->session_handle->http_driver,
            GLOBUS_XIO_HTTP_GET_RESPONSE,
            &status_code,
            &reason_phrase,
            NULL,
            &header_table);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("HTTP GET attempt", result);
        goto open_exit;
    }
    else if(status_code > 299)
    {
        globus_byte_t *                 body_buffer;
        globus_size_t                   buflen = 64*1024;
        globus_size_t                   body_nbytes;
        globus_size_t                   total_nbytes = 0;
        globus_bool_t                   eof;
        globus_size_t                   waitfor = 0;

        body_buffer = malloc(buflen+1);
        do
        {
            body_nbytes = 0;
            result = globus_xio_read(
                handle,
                body_buffer+total_nbytes,
                buflen-total_nbytes,
                waitfor,
                &body_nbytes,
                NULL);
            eof = globus_xio_error_is_eof(result);
            if(eof)
            {
                result = GLOBUS_SUCCESS;
            }
            if(result != GLOBUS_SUCCESS)
            {
                result = GLOBUS_SUCCESS;
                eof = GLOBUS_TRUE;
            }
            total_nbytes += body_nbytes;

            if(body_nbytes == 0 || total_nbytes == buflen)
            {
                eof = GLOBUS_TRUE;
            }
            else
            {
                waitfor = GLOBUS_MIN(1024, buflen-total_nbytes);
            }           
        } while(!eof);
        
        body_buffer[total_nbytes] = '\0';
        globus_i_gfs_data_http_print_response(
            status_code, &header_table, body_buffer, &header_str);

        err_str = globus_common_create_string(
            "HTTP GET failed with \"%03d %s\"\n%s",
            status_code,
            reason_phrase,
            header_str);

        result = GlobusGFSErrorGeneric(err_str);
            
        globus_free(err_str);
        globus_free(header_str);
        globus_free(body_buffer);

        goto open_exit;
    }
    else
    {
        globus_i_gfs_data_http_print_response(
            status_code, &header_table, NULL, &op->user_msg);
    }        
    
    
    /* set up internal file recv request */    
    recv_info = (globus_gfs_transfer_info_t *)
        globus_calloc(1, sizeof(globus_gfs_transfer_info_t));
    recv_info->alloc_size = length;
    recv_info->truncate = GLOBUS_FALSE;
    globus_range_list_init(&recv_info->range_list);
    globus_range_list_insert(recv_info->range_list, offset, length);
    recv_info->stripe_count = 1;
    recv_info->node_count = 1;
    recv_info->pathname = globus_libc_strdup(path);


    data_handle = (globus_l_gfs_data_handle_t *)
        globus_calloc(1, sizeof(globus_l_gfs_data_handle_t));
    if(data_handle == NULL)
    {
        globus_panic(NULL, result,
            "small malloc failure, no recovery");
    }

    data_handle->session_handle = op->session_handle;
    data_handle->is_mine = GLOBUS_FALSE;
    data_handle->info.mode  = 'S';
    data_handle->info.nstreams = 1;
    data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_VALID;
    recv_info->data_arg =
        (void *) (intptr_t) globus_handle_table_insert(
            &data_handle->session_handle->handle_table,
            data_handle,
            1);
            
    data_handle->http_handle = handle;
    data_handle->http_length = length;
    op->data_handle = data_handle;

    op->ref++;
    globus_i_gfs_data_request_recv(
        NULL,
        op->session_handle,
        0,
        recv_info,
        globus_l_gfs_data_http_transfer_cb,
        globus_l_gfs_data_http_event_cb,
        op);
        
    
    return GLOBUS_SUCCESS;

open_exit:
    globus_xio_close(handle, NULL);
response_exit:

    GlobusGFSDebugExit();
    return result;
    
}

globus_result_t 
globus_i_gfs_data_http_put(
    globus_l_gfs_data_operation_t *     op,
    char *                              path,
    char *                              request,
    globus_off_t                        offset,
    globus_off_t                        length,
    globus_bool_t                       do_retry)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char *                              err_str;
    char *                              header_str;
    int                                 i;
    globus_xio_http_header_t *          headers;
    globus_xio_data_descriptor_t        descriptor;
    globus_xio_handle_t                 handle;
    globus_byte_t                       buffer[1];
    globus_bool_t                       eof;
    int                                 status_code;
    char *                              reason_phrase;
    globus_xio_attr_t                   attr;
    char *                              url;
    globus_l_gfs_data_handle_t *        data_handle;
    globus_gfs_transfer_info_t *        send_info;
    int                                 count;
    globus_bool_t                       https;
    globus_hashtable_t                  header_table = NULL;
    char *                              method;
    int                                 http_ver;
    globus_bool_t                       retry = GLOBUS_FALSE;
    GlobusGFSName(globus_l_gfs_data_http_put);
    GlobusGFSDebugEnter();
    
    /* parse url, headers */
    result = globus_i_gfs_http_data_parse_request(
        request, &url, &https, &http_ver, &method, &headers, &count);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("Invalid request. Parsing", result);
        goto response_exit;
    }

    handle = op->session_handle->http_handle;
    result = globus_i_gfs_data_http_init(
        op->session_handle, https, &handle, &attr, &descriptor);
    if(result != GLOBUS_SUCCESS)
    {
        goto response_exit;
    }
    
    result = globus_xio_attr_cntl(
        attr,
        op->session_handle->http_driver,
        GLOBUS_XIO_HTTP_ATTR_SET_REQUEST_HTTP_VERSION,
        http_ver);
    if(result != GLOBUS_SUCCESS)
    {
        goto response_exit;
    }

    result = globus_xio_attr_cntl(
        attr,
        op->session_handle->http_driver,
        GLOBUS_XIO_HTTP_ATTR_SET_REQUEST_METHOD,
        method);
    if(result != GLOBUS_SUCCESS)
    {
        goto response_exit;
    }
       
    /* set individual headers */    
    for (i = 0; i < count; i++)
    {
        result = globus_xio_attr_cntl(
                attr,
                op->session_handle->http_driver,
                GLOBUS_XIO_HTTP_ATTR_SET_REQUEST_HEADER,
                headers[i].name,
                headers[i].value);
        if(result != GLOBUS_SUCCESS)
        {
            goto response_exit;
        }
    }


    result = globus_xio_attr_cntl(
            attr,
            op->session_handle->http_driver,
            GLOBUS_XIO_HTTP_ATTR_SET_REQUEST_HEADER,
            "Transfer-Encoding",
            "identity");
    if(result != GLOBUS_SUCCESS)
    {
        goto response_exit;
    }

    /* expect spec is unclear on content-length=0, makes 0 byte puts even
     * more unstable than they already are */
    if(length > 0)
    {
        result = globus_xio_attr_cntl(
                attr,
                op->session_handle->http_driver,
                GLOBUS_XIO_HTTP_ATTR_SET_REQUEST_HEADER,
                "Expect",
                "100-continue");
        if(result != GLOBUS_SUCCESS)
        {
            goto response_exit;
        }
    }
    
    result = globus_xio_open(handle, url, attr);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("HTTP connection", result);
        goto response_exit;
    }

    if(length > 0)
    {
        /* read response, no data */
        result = globus_xio_read(
                handle,
                buffer,
                0,
                0,
                NULL,
                descriptor);
        eof = globus_xio_error_is_eof(result);
        if(eof)
        {
            retry = do_retry && (globus_error_get_type(globus_error_peek(result)) ==
                        GLOBUS_XIO_HTTP_ERROR_PERSISTENT_CONNECTION_DROPPED);
            if(!retry)
            {
                result = GLOBUS_SUCCESS;
            }
        }
        
        if(retry)
        {
            globus_xio_close(op->session_handle->http_handle, NULL);
            op->session_handle->http_handle = NULL;
            globus_xio_driver_unload(op->session_handle->http_driver);
            op->session_handle->http_driver = NULL;
            if(op->session_handle->https_stack)
            {
                globus_xio_stack_destroy(op->session_handle->https_stack);
                op->session_handle->https_stack = NULL;
            }
            if(op->session_handle->http_stack)
            {
                globus_xio_stack_destroy(op->session_handle->http_stack);
                op->session_handle->http_stack = NULL;
            }
            return globus_i_gfs_data_http_put(
                op, path, request, offset, length, GLOBUS_FALSE);
        }
    
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed("Before getting response, HTTP connection", result);
            goto open_exit;
        }

        /* check response */
        result = globus_xio_data_descriptor_cntl(
                descriptor,
                op->session_handle->http_driver,
                GLOBUS_XIO_HTTP_GET_RESPONSE,
                &status_code,
                &reason_phrase,
                NULL,
                &header_table);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed("HTTP PUT attempt", result);
            goto open_exit;
        }
        else if(status_code > 299)
        {
            globus_byte_t *                 body_buffer;
            globus_size_t                   buflen = 64*1024;
            globus_size_t                   body_nbytes;
            globus_size_t                   total_nbytes = 0;
            globus_bool_t                   eof;
            globus_size_t                   waitfor = 0;
    
            body_buffer = malloc(buflen+1);
            do
            {
                body_nbytes = 0;
                result = globus_xio_read(
                    handle,
                    body_buffer+total_nbytes,
                    buflen-total_nbytes,
                    waitfor,
                    &body_nbytes,
                    NULL);
                eof = globus_xio_error_is_eof(result);
                if(eof)
                {
                    result = GLOBUS_SUCCESS;
                }
                if(result != GLOBUS_SUCCESS)
                {
                    result = GLOBUS_SUCCESS;
                    eof = GLOBUS_TRUE;
                }
                total_nbytes += body_nbytes;
    
                if(body_nbytes == 0 || total_nbytes == buflen)
                {
                    eof = GLOBUS_TRUE;
                }
                else
                {
                    waitfor = GLOBUS_MIN(1024, buflen-total_nbytes);
                }           
            } while(!eof);
            
            body_buffer[total_nbytes] = '\0';
            globus_i_gfs_data_http_print_response(
                status_code, &header_table, body_buffer, &header_str);
    
            err_str = globus_common_create_string(
                "HTTP PUT failed with \"%03d %s\"\n%s",
                status_code,
                reason_phrase,
                header_str);
    
            result = GlobusGFSErrorGeneric(err_str);
                
            globus_free(err_str);
            globus_free(header_str);
            globus_free(body_buffer);
    
            goto open_exit;
        }
    }

    /* set up internal file send request */
    send_info = (globus_gfs_transfer_info_t *)
        globus_calloc(1, sizeof(globus_gfs_transfer_info_t));
    globus_range_list_init(&send_info->range_list);
    globus_range_list_insert(send_info->range_list, offset, length);
    send_info->partial_offset = 0;
    send_info->partial_length = -1;
    send_info->stripe_count = 1;
    send_info->node_count = 1;
    send_info->pathname = globus_libc_strdup(path);


    data_handle = (globus_l_gfs_data_handle_t *)
        globus_calloc(1, sizeof(globus_l_gfs_data_handle_t));
    if(data_handle == NULL)
    {
        globus_panic(NULL, result,
            "small malloc failure, no recovery");
    }

    data_handle->session_handle = op->session_handle;
    data_handle->is_mine = GLOBUS_FALSE;
    data_handle->info.mode  = 'S';
    data_handle->info.nstreams = 1;
    data_handle->state = GLOBUS_L_GFS_DATA_HANDLE_VALID;
    send_info->data_arg =
        (void *) (intptr_t) globus_handle_table_insert(
            &data_handle->session_handle->handle_table,
            data_handle,
            1);
            
    data_handle->http_handle = handle;
    data_handle->http_length = length;
    op->data_handle = data_handle;
    
    op->ref++;
    globus_i_gfs_data_request_send(
        NULL,
        op->session_handle,
        0,
        send_info,
        globus_l_gfs_data_http_transfer_cb,
        globus_l_gfs_data_http_event_cb,
        op);
   
    
    return GLOBUS_SUCCESS;

open_exit:          
response_exit:
    GlobusGFSDebugExit();
    return result;
}

