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


/**
 * @file globus_gridftp_server.h
 * @brief DSI interface
 * 
 * If you are interested in writing a module for this server and want to
 * discuss it's design, or are already writing one and would like support,
 * please subscribe to gridftp-dev@globus.org:
 * https://lists.globus.org/mailman/listinfo/gridftp-dev
 * In fact, we'd like to hear from you even if you don't need any assistance.
 */


#ifndef GLOBUS_GRIDFTP_SERVER_H
#define GLOBUS_GRIDFTP_SERVER_H

#include "globus_common.h"
#include "globus_gridftp_server_control.h"

#ifdef TARGET_ARCH_WIN32
#ifdef interface
#undef interface
#endif
#endif

#ifndef TARGET_ARCH_WIN32
#include <grp.h>
#endif

#define GLOBUS_MAPPING_STRING ":globus-mapping:"

extern globus_module_descriptor_t      globus_i_gfs_module;
#define GLOBUS_GRIDFTP_SERVER_MODULE (&globus_i_gfs_module)

extern globus_extension_registry_t      globus_i_gfs_dsi_registry;
#define GLOBUS_GFS_DSI_REGISTRY         &globus_i_gfs_dsi_registry

extern globus_extension_registry_t      globus_i_gfs_acl_registry;
#define GLOBUS_GFS_ACL_REGISTRY         &globus_i_gfs_acl_registry

/*
 *  globus_gfs_error_type_t
 *
 */
typedef enum globus_gfs_error_type_e
{
    GLOBUS_GFS_ERROR_MEMORY = 1,
    GLOBUS_GFS_ERROR_PARAMETER,
    GLOBUS_GFS_ERROR_SYSTEM_ERROR,
    GLOBUS_GFS_ERROR_WRAPPED,
    GLOBUS_GFS_ERROR_DATA,
    GLOBUS_GFS_ERROR_GENERIC
} globus_gfs_error_type_t;

/*
 *  globus_gfs_operation_type_t
 * 
 * Server operations.
 */
typedef enum globus_gfs_operation_type_e
{
    GLOBUS_GFS_OP_FINAL_REPLY = 1,
    GLOBUS_GFS_OP_EVENT_REPLY,
    GLOBUS_GFS_OP_EVENT,
    GLOBUS_GFS_OP_SESSION_START,
    GLOBUS_GFS_OP_SESSION_STOP,
    GLOBUS_GFS_OP_RECV,
    GLOBUS_GFS_OP_SEND,
    GLOBUS_GFS_OP_LIST,
    GLOBUS_GFS_OP_COMMAND,
    GLOBUS_GFS_OP_PASSIVE,
    GLOBUS_GFS_OP_ACTIVE,
    GLOBUS_GFS_OP_DESTROY,
    GLOBUS_GFS_OP_TRANSFER,
    GLOBUS_GFS_OP_STAT,
    GLOBUS_GFS_OP_BUFFER_SEND,
    GLOBUS_GFS_OP_HANDSHAKE,
    GLOBUS_GFS_OP_SESSION_START_REPLY,
    GLOBUS_GFS_OP_INTERMEDIATE_REPLY
} globus_gfs_operation_type_t;

#define GLOBUS_GFS_OP_STAT_PARTIAL GLOBUS_GFS_OP_INTERMEDIATE_REPLY

/*
 *  globus_gfs_command_type_t
 * 
 * Command types.  Commands are generally simple filesystem operations
 * that only return success/failure and at most a single string.
 */
typedef enum globus_gfs_command_type_e
{
    GLOBUS_GFS_CMD_MKD = 1,
    GLOBUS_GFS_CMD_RMD,
    GLOBUS_GFS_CMD_DELE,
    GLOBUS_GFS_CMD_SITE_AUTHZ_ASSERT,
    GLOBUS_GFS_CMD_SITE_RDEL,
    GLOBUS_GFS_CMD_RNTO,
    GLOBUS_GFS_CMD_RNFR,
    GLOBUS_GFS_CMD_CKSM,
    GLOBUS_GFS_CMD_SITE_CHMOD,
    GLOBUS_GFS_CMD_SITE_DSI,
    GLOBUS_GFS_CMD_SITE_SETNETSTACK,
    GLOBUS_GFS_CMD_SITE_SETDISKSTACK,
    GLOBUS_GFS_CMD_SITE_CLIENTINFO,
    GLOBUS_GFS_CMD_DCSC,
    GLOBUS_GFS_CMD_SITE_CHGRP,
    GLOBUS_GFS_CMD_SITE_UTIME,
    GLOBUS_GFS_CMD_SITE_SYMLINKFROM,
    GLOBUS_GFS_CMD_SITE_SYMLINK,
    GLOBUS_GFS_CMD_HTTP_PUT,
    GLOBUS_GFS_CMD_HTTP_GET,
    GLOBUS_GFS_CMD_HTTP_CONFIG,
    GLOBUS_GFS_CMD_TRNC,
    GLOBUS_GFS_CMD_SITE_TASKID,
    
    /* handled internally */
    GLOBUS_GFS_CMD_SITE_RESTRICT = 3072,
    GLOBUS_GFS_CMD_SITE_CHROOT,
    GLOBUS_GFS_CMD_SITE_SHARING,
    GLOBUS_GFS_CMD_UPAS,
    GLOBUS_GFS_CMD_UPRT,
    GLOBUS_GFS_CMD_STORATTR,
    GLOBUS_GFS_CMD_WHOAMI,
    
    GLOBUS_GFS_MIN_CUSTOM_CMD = 4096
} globus_gfs_command_type_t;

/**
 * @brief Event types
 * @details
 * [Request] types are passed as parameters to the DSI's 
 * globus_gfs_storage_trev_t func.  Supported events must be specified
 * in the event_mask of globus_gridftp_server_begin_transfer().
 * 
 * [Reply] types are passed back via globus_gridftp_server_operation_event()
 * with any associated data.  globus_gridftp_server_begin_transfer() is a 
 * convience wrapper which accomplishes the same as 
 * globus_gridftp_server_operation_event() with an event type of a
 * GLOBUS_GFS_EVENT_TRANSFER_BEGIN.
 * 
 * In a multi-node configuration, generally request types must be passed on
 * to all nodes (multiplexed), and reply types must be counted and passed
 * back as one event (demultiplexed).  Some events can be passed back multiple
 * times as noted.
 * 
 */
typedef enum globus_gfs_event_type_e
{
    /** [Reply] Data will start to transfer.  Only ONE of these events may
     *  be passed back per globus_gfs_operation_t. */
    GLOBUS_GFS_EVENT_TRANSFER_BEGIN = 0x0001,
    
    /** [Request] abort of a transfer that is between BEGIN and COMPLETE. */
    GLOBUS_GFS_EVENT_TRANSFER_ABORT = 0x0002,
    
    /** [Request] Requesting side is completely done with transfer and any 
     *  related references have been destroyed. */
    GLOBUS_GFS_EVENT_TRANSFER_COMPLETE = 0x0004,
    
    /** [Reply] Data connection has been disconnected.  Only ONE of these 
     *  events may be passed forward per globus_gfs_operation_t. */
    GLOBUS_GFS_EVENT_DISCONNECTED = 0x0008,
    
    /** [Request] Ask for a 'bytes recieved' transfer update. 
     *  [Reply]   Report bytes recieved since last update.  This event may be 
     *  passed back multiple times per globus_gfs_operation_t. */
    GLOBUS_GFS_EVENT_BYTES_RECVD = 0x0010,
    
    /** [Request] Ask for a 'ranges recieved' transfer update. 
     *  [Reply]   Report ranges recieved since last update.  This event may be 
     *  passed back multiple times per globus_gfs_operation_t. */
    GLOBUS_GFS_EVENT_RANGES_RECVD = 0x0020,
    
    /** [Reply]  Data connection to be used for this transfer is 
     *  (or already has been) established.  Only ONE of these events may 
     *  be back forward per globus_gfs_operation_t. */
    GLOBUS_GFS_EVENT_TRANSFER_CONNECTED = 0x0040,
    
    /** unused */
    GLOBUS_GFS_EVENT_PARTIAL_EOF_COUNT = 0x0100,
    
    /** unused */
    GLOBUS_GFS_EVENT_FINAL_EOF_COUNT = 0x0200,

    /** bitmask catchall */
    GLOBUS_GFS_EVENT_ALL = 0xFFFF
} globus_gfs_event_type_t;

/*
 *  globus_gfs_buffer_type_t
 *
 */
typedef enum globus_gfs_buffer_type_e
{
    GLOBUS_GFS_BUFFER_EOF_INFO = 0x0001,
    GLOBUS_GFS_BUFFER_SERVER_DEFINED = 0xFFFF
    /* user defined types will start at 0x00010000 */
} globus_gfs_buffer_type_t;

/*
 *  globus_gfs_layout_type_t
 * 
 * Striped layout types.
 */
typedef enum globus_gfs_layout_type_e
{
    GLOBUS_GFS_LAYOUT_PARTITIONED = 1,
    GLOBUS_GFS_LAYOUT_BLOCKED
} globus_gfs_layout_type_t;

/*
 *  globus_gfs_stat_t
 * 
 * Similar to a posix struct stat.  Defined in the server-lib.
 *
 * (this comment should not be relied upon, so check the
 *   definition in globus_gridftp_server_control.h to be sure)
 *  
 * typedef struct globus_gridftp_server_control_stat_s                    
 * {                                                                      
 *     int                                     mode;                      
 *     int                                     nlink;                     
 *     char *                                  name;          
 *     char *                                  symlink_target;
 *     uid_t                                   uid;                       
 *     gid_t                                   gid;                       
 *     globus_off_t                            size;                      
 *     globus_time_t                           atime;                     
 *     globus_time_t                           ctime;                     
 *     globus_time_t                           mtime;                     
 *     int                                     dev;                       
 *     int                                     ino;                       
 * } globus_gridftp_server_control_stat_t;                                
 */
typedef globus_gridftp_server_control_stat_t    globus_gfs_stat_t;


/*
 *  globus_gfs_operation_info_t
 * 
 * Internal operation info.  This handle is passed with the info structs
 * Its data should not be accessed.
 */
typedef struct globus_i_gfs_op_info_s *  globus_gfs_op_info_t;

/*
 *  globus_gfs_data_finished_info_t
 * 
 * Contains specific result info for an active or passive data connection.
 * Note that in most cases this info will simply be passed.
 */
typedef struct globus_gfs_data_finished_info_s
{
    /** unique key for the data_handle */
    void *                              data_arg;
    /** false if the direction of data flow is restricted */
    globus_bool_t                       bi_directional;
    /** is the connection using ipv6? */
    globus_bool_t                       ipv6;
    /** number of contact strings */
    int                                 cs_count;
    /** array of contact strings */
    const char **                       contact_strings;
} globus_gfs_data_finished_info_t;

/*
 *  globus_gfs_cmd_finshed_info_t
 * 
 * Contains specific result info for commands.
 */
typedef struct globus_gfs_cmd_finshed_info_s
{
    /** type of command that has finished */
    globus_gfs_command_type_t           command;
    /** checksum string, for CKSM only */
    char *                              checksum;
    /** full path of the created directory, for MKD only */
    char *                              created_dir;
} globus_gfs_cmd_finshed_info_t;

/*
 *  globus_gfs_stat_finished_info_t
 * 
 * Contains specific result info for a stat.
 */
typedef struct globus_gfs_stat_finished_info_s
{
    /** uid of the user that performed the stat */
    int                                 uid;
    /** count of gids in gid_array */
    int                                 gid_count;
    /** array of gids of which user that performed the stat is a member */
    int *                               gid_array;
    /** number of stat objects in the array */
    int                                 stat_count;
    /** array of stat objects */
    globus_gfs_stat_t *                 stat_array;
} globus_gfs_stat_finished_info_t;

/*
 *  globus_gfs_session_finished_info_t
 * 
 * Contains specific result info for a stat.
 */
typedef struct globus_gfs_session_finished_info_s
{
    /** arg to pass back with each request */
    void *                              session_arg;
    /** local username of authenticated user */
    char *                              username;
    /** home directory of authenticated user */
    char *                              home_dir;
} globus_gfs_session_finished_info_t;

/*
 *  globus_gfs_session_finished_info_t
 * 
 * Contains specific result info for a stat.
 */
typedef struct globus_gfs_transfer_finished_info_s
{
    /* total bytes transferred for this operation */
    globus_off_t                        bytes_transferred;
    
} globus_gfs_transfer_finished_info_t;

/*
 *  globus_gfs_finished_info_t
 * 
 * Final result info for an operation.
 */
typedef struct globus_gfs_finished_info_s
{
    /** type of operation that has completed */
    globus_gfs_operation_type_t         type;
    /** unique key for the op */
    int                                 id;
    /** result code for success or failure of the op */
    int                                 code;
    /** additional message to be appended to the control channel
     ** response or sent over the ipc when result is success.  */
    char *                              msg;
    /** result_t */
    globus_result_t                     result;

    union
    {
        globus_gfs_session_finished_info_t session;
        globus_gfs_data_finished_info_t data;
        globus_gfs_cmd_finshed_info_t   command;
        globus_gfs_stat_finished_info_t stat;
        globus_gfs_transfer_finished_info_t transfer;
    } info;

    /** op info */
    globus_gfs_op_info_t                op_info;
} globus_gfs_finished_info_t;

/*
 *  globus_gfs_event_info_t
 * 
 * Event info.
 */
typedef struct globus_gfs_event_info_s
{
    /** type of event */
    globus_gfs_event_type_t             type;

    /** arg supplied with the BEGIN_TRANSFER event, 
        will be passed back with each transfer event */
    void *                              event_arg;
    
    /* reply data */
    /** node that event is from */
    int                                 node_ndx;
    /** unique key of transfer request that event is related to */
    int                                 id;
    /** mask of events that should be passed in */
    int                                 event_mask;
    /** number of bytes received for current transfer */
    globus_off_t                        recvd_bytes;
    /** ranges of bytes received for current transfer */
    globus_range_list_t                 recvd_ranges;
    /** arg representing data handle that event is related to */    
    void *                              data_arg;
    
    /* request data */
    /** array of eof counts */    
    int *                               eof_count;
    /** number of nodes (size of eof_count array) */    
    int                                 node_count;

    /** op info */
    globus_gfs_op_info_t                op_info;
} globus_gfs_event_info_t;

/*
 *  globus_gfs_transfer_info_t
 * 
 * Info needed for transfer operations (list, send, recv).
 */
typedef struct globus_gfs_transfer_info_s
{
    /** pathname being transferred or listed */
    char *                              pathname;    
    /** module name and arguments */
    char *                              module_name;
    char *                              module_args;
    /** type of list requested */
    char *                              list_type;
    
    /** offset of partial transfer */
    globus_off_t                        partial_offset;
    /** length of partial transfer */
    globus_off_t                        partial_length;
    /** list or ranges for a restart */
    globus_range_list_t                 range_list;
    /** length of partial transfer */
    globus_bool_t                       truncate;
    
    /** unique key that identifies the associated data_handle */
    void *                              data_arg;
    /** number of eof that sender should send  xxx might need to be array here */
    int                                 eof_count;
    /** total number of local stripes that will be involved */
    int                                 stripe_count;    
    /** total number of nodes that will be involved */
    int                                 node_count;    
    /** node index */
    int                                 node_ndx;

    /** client requested storage space size */
    globus_off_t                        alloc_size;
    
    /** expected checksum */
    char *                              expected_checksum;
    /** expected checksum algorithm */
    char *                              expected_checksum_alg;
    
    /** levels to descend for listing (0 = no descent) */
    int                                 list_depth;
    /** directory traversal options */
    int                                 traversal_options;

    /** op info */
    globus_gfs_op_info_t                op_info;
} globus_gfs_transfer_info_t;

 
/*
* maintain backward source compatibility after member rename
*/
#define rnfr_pathname from_pathname

/*
 *  globus_gfs_command_info_t
 * 
 * Info needed for a command operation.
 */
typedef struct globus_gfs_command_info_s
{
    /** command type requested */
    globus_gfs_command_type_t           command; 
    /** pathname to execute the command on */
    char *                              pathname;

    /** offset for cksm command */
    globus_off_t                        cksm_offset;
    /** length of data to read for cksm command   -1 means full file */
    globus_off_t                        cksm_length;
    /** checksum algorithm requested (md5 only currently) */
    char *                              cksm_alg;
    
    /** mode argument to the chmod command */
    int                                 chmod_mode;
    
    /** pathname to rename from (to the above pathname) OR 
     *  pathname to link to  */
    char *                              from_pathname;    

    /** Authorization assertion */
    char *                              authz_assert; 

    /** group argument to the chgrp command */
    char *                              chgrp_group;
    
    /** time argument to the utime command */
    time_t                              utime_time;
    
    /** op info */
    globus_gfs_op_info_t                op_info;
} globus_gfs_command_info_t;

/*
 *  globus_gfs_data_info_t
 * 
 * Info needed for data operations (active, passive).
 */
typedef struct globus_gfs_data_info_s
{
    /** should this be ipv6? */
    globus_bool_t                       ipv6;
    /** number of parallel streams */
    int                                 nstreams;
    /** data channel mode */
    char                                mode;
    /** data channel type */
    char                                type;
    /** tcp buffersize to use */
    globus_size_t                       tcp_bufsize;
    /** blocksize to use */
    globus_size_t                       blocksize;
    /** blocksize to use for stripe layout */
    globus_size_t                       stripe_blocksize;
    /** stripe layout to use */
    int                                 stripe_layout;

    /** protection mode */
    char                                prot;
    /** dcau mode */
    char                                dcau;
    /** client DN */
    char *                              subject;
    /** pathname that will be transferred (or NULL if not delayed PASV) */
    char *                              pathname;

    /** max number of contact strings to return (for PASV) */
    int                                 max_cs;
    /** number of contact strings (PORT) */
    int                                 cs_count;
    /** array of contact strings (PORT) */
    const char **                       contact_strings;
    /** interface that should be used for data connections */
    char *                              interface;

    /* if this is set, the data channel will use it instead
        of the default session credential */
    gss_cred_id_t                       del_cred;

    /** op info */
    globus_gfs_op_info_t                op_info;
} globus_gfs_data_info_t;

/*
 *  globus_gfs_stat_info_t
 * 
 * Info needed for a stat operation.
 */
typedef struct globus_gfs_stat_info_s
{
    /** if pathname is a directory, should stat report its info or its contents */
    globus_bool_t                       file_only;
    /** this stat is requested internally -- bypasses authorization checks */
    globus_bool_t                       internal;
    /** pathname to stat */
    char *                              pathname;
    /** whether to return symbolic link info or target info */
    globus_bool_t                       use_symlink_info;
    /** if pathname is a directory, should its stat info be included? */
    globus_bool_t                       include_path_stat;

    /** op info */
    globus_gfs_op_info_t                op_info;
} globus_gfs_stat_info_t;

typedef struct globus_gfs_session_info_s
{
    gss_cred_id_t                       del_cred;
    globus_bool_t                       free_cred;
    globus_bool_t                       map_user;
    char *                              username;
    char *                              password;
    char *                              subject;
    char *                              cookie;
    char *                              host_id;

    /** op info */
    globus_gfs_op_info_t                op_info;
} globus_gfs_session_info_t;

typedef enum globus_gfs_brain_reason_e
{
    GLOBUS_GFS_BRAIN_REASON_ERROR = 1,
    GLOBUS_GFS_BRAIN_REASON_COMPLETE
} globus_gfs_brain_reason_t;

typedef struct globus_i_gfs_brain_node_s
{
    char *                              host_id;
    char *                              repo_name;
    void *                              brain_arg;
    int                                 max_connection;
    int                                 current_connection;
    float                               load;
} globus_i_gfs_brain_node_t;

/**************************************************************************
 *  Storage Module API
 * 
 * The storage module API is made up of the interface definition,
 * notification functions, and helper functions below.
 *************************************************************************/

/*
 *  globus_gfs_operation_t
 * 
 * Operation handle.  This handle is passed to and from the storage
 * module.  Its internal data should not be used.
 */
typedef struct globus_l_gfs_data_operation_s *  globus_gfs_operation_t;


/**
 * Interface Definition
 **/
 
/*
 *  init/destroy
 *
 * This will be called upon a new client session.  Any persistent 
 * data that will be needed should be initialized and stored in a
 * user-defined object which should be assigned to out_user_arg.  This
 * object pointer will then be passed back to the module with any other
 * interface call.
 */
typedef void
(*globus_gfs_storage_init_t)(
    globus_gfs_operation_t              op,
    globus_gfs_session_info_t *         session_info);
    
/*
 * This will be called when the client session ends.  Final cleanup 
 * should be done here.
 */
typedef void
(*globus_gfs_storage_destroy_t)(
    void *                              user_arg);

/*
 *  transfer
 *
 * This defines the functions that will be called for list, send, and recv.
 */
typedef void
(*globus_gfs_storage_transfer_t)(
    globus_gfs_operation_t              op,
    globus_gfs_transfer_info_t *        transfer_info,
    void *                              user_arg);

/*
 *  command
 *
 * This defines the function that will be called for commands.  The type
 * member of command_info specifies which command to carry out. 
 */
typedef void
(*globus_gfs_storage_command_t)(
    globus_gfs_operation_t              op,
    globus_gfs_command_info_t *         command_info,
    void *                              user_arg);

/*
 *  stat
 *
 * This defines the function that will be called for a stat lookup. 
 */
typedef void
(*globus_gfs_storage_stat_t)(
    globus_gfs_operation_t              op,
    globus_gfs_stat_info_t *            stat_info,
    void *                              user_arg);

/*
 *  data connection
 *
 * This defines the functions that will be called for active and passive
 * data connection creation. 
 */
typedef void
(*globus_gfs_storage_data_t)(
    globus_gfs_operation_t              op,
    globus_gfs_data_info_t *            data_info,
    void *                              user_arg);

/*
 *  data_destroy
 *
 * This defines the function that will be called to signal that a data
 * connection should be destroyed.  Note that there is no corresponding
 * finished notification for data destroy requests.
 */
typedef void
(*globus_gfs_storage_data_destroy_t)(
    void *                              data_arg,
    void *                              user_arg);

/*
 *  data_destroy
 *
 * This defines the function that will be called to signal that a transfer
 * event should occur.  Note that there is no corresponding finished 
 * notification for transfer event requests.
 */
typedef void
(*globus_gfs_storage_trev_t)(
    globus_gfs_event_info_t *           event_info,
    void *                              user_arg);

/*
 *  set cred
 *
 * This defines the function that will be called to pass delegated credentials.
 * XXX more here later XXX
 */
typedef void
(*globus_gfs_storage_set_cred_t)(
    gss_cred_id_t                       del_cred,
    void *                              user_arg);

/*
 *  send user buffer
 *
 * This defines the function that will be called to send a user defined buffer.
 * XXX more here later XXX
 */
typedef void
(*globus_gfs_storage_buffer_send_t)(
    int                                 buffer_type,
    globus_byte_t *                     buffer,
    globus_size_t                       buffer_len,
    void *                              user_arg);


/*
 *  realpath
 *
 * This defines the function that will be called to determine a true path
 * free of symlinks or other obsfucation.  
 * if you implement this, add GLOBUS_GFS_DSI_DESCRIPTOR_HAS_REALPATH to your 
 * globus_gfs_storage_iface_t interface definition.
 */

typedef globus_result_t
(*globus_gfs_storage_realpath_t)(
    const char *                        in_path,
    char **                             out_realpath,
    void *                              user_arg);


#define GLOBUS_GFS_DSI_DESCRIPTOR_SENDER                        (1 << 0)
#define GLOBUS_GFS_DSI_DESCRIPTOR_BLOCKING                      (1 << 1)
#define GLOBUS_GFS_DSI_DESCRIPTOR_HAS_REALPATH                  (1 << 2)
#define GLOBUS_GFS_DSI_DESCRIPTOR_REQUIRES_ORDERED_DATA         (1 << 3)
#define GLOBUS_GFS_DSI_DESCRIPTOR_SETS_ERROR_RESPONSES          (1 << 4)
#define GLOBUS_GFS_DSI_DESCRIPTOR_SAFE_RDEL                     (1 << 5)

/*
 *  globus_gfs_storage_iface_t
 * 
 * Storage interface function pointers.  Only define functions that are
 * implemented.  If a function is not defined, the server will either fail
 * for that particular operation, or in the case of list, data, cred, and 
 * trev funcs, the server will act on those operations itself. 
 */
typedef struct globus_gfs_storage_iface_s
{
    int                                 descriptor;

    /* session initiating functions */
    globus_gfs_storage_init_t           init_func;
    globus_gfs_storage_destroy_t        destroy_func;

    /* transfer functions */
    globus_gfs_storage_transfer_t       list_func;
    globus_gfs_storage_transfer_t       send_func;
    globus_gfs_storage_transfer_t       recv_func;
    globus_gfs_storage_trev_t           trev_func;

    /* data conn funcs */
    globus_gfs_storage_data_t           active_func;
    globus_gfs_storage_data_t           passive_func;
    globus_gfs_storage_data_destroy_t   data_destroy_func;

    globus_gfs_storage_command_t        command_func;
    globus_gfs_storage_stat_t           stat_func;

    globus_gfs_storage_set_cred_t       set_cred_func;
    globus_gfs_storage_buffer_send_t    buffer_send_func;
    globus_gfs_storage_realpath_t       realpath_func;
} globus_gfs_storage_iface_t;

/**
 * Notification Functions
 **/
 
/*
 *  operation finished
 *
 * This is a generic finished notification function.  Either this *or* a
 * specific finished function below must be called upon completion of an 
 * operation with the appropriate data set in the finished_info struct, 
 * including error info if the operation failed.
 */
void
globus_gridftp_server_operation_finished(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    globus_gfs_finished_info_t *        finished_info);

/*
 *  operation event
 *
 * This is a generic event notification function.  Either this *or* a
 * specific event function below must be called upon completion of an 
 * operation with the appropriate event data set in the event_info struct.
 */
void
globus_gridftp_server_operation_event(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    globus_gfs_event_info_t *           event_info);

/*
 * begin transfer event
 * 
 * Speficic event notification for the start of a transfer.
 */ 
void
globus_gridftp_server_begin_transfer(
    globus_gfs_operation_t              op,
    int                                 event_mask,
    void *                              event_arg);

/*
 * finished transfer
 * 
 * Speficic finished notification for completion of a transfer.
 */ 
void
globus_gridftp_server_finished_transfer(
    globus_gfs_operation_t              op, 
    globus_result_t                     result);

/*
 * finished session_start
 * 
 * Specific finished notification for session start completion.
 * 
 */ 
void
globus_gridftp_server_finished_session_start(
    globus_gfs_operation_t              op, 
    globus_result_t                     result,
    void *                              session_arg,
    char *                              username,
    char *                              home_dir);
    
/*
 * finished command
 * 
 * Speficic finished notification for completion of a command.
 * command_response should be NULL if not used (currently only
 *  used in MKD and CKSM)
 */ 
void
globus_gridftp_server_finished_command(
    globus_gfs_operation_t              op, 
    globus_result_t                     result,
    char *                              command_response);
void
globus_gridftp_server_intermediate_command(
    globus_gfs_operation_t              op, 
    globus_result_t                     result,
    char *                              command_response);
    
/*
 * finished stat
 * 
 * Speficic finished notification for completion of a stat.
 */ 
void
globus_gridftp_server_finished_stat(
    globus_gfs_operation_t              op, 
    globus_result_t                     result,
    globus_gfs_stat_t *                 stat_array,
    int                                 stat_count);

void
globus_gridftp_server_finished_stat_partial(
    globus_gfs_operation_t              op, 
    globus_result_t                     result,
    globus_gfs_stat_t *                 stat_array,
    int                                 stat_count);

void
globus_gridftp_server_finished_stat_custom_list(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    globus_byte_t *                     list_response,
    globus_size_t                       list_response_len,
    globus_bool_t                       free_buffer);

/*
 * finished active data
 * 
 * Speficic finished notification for completion of a active data creation.
 */ 
void
globus_gridftp_server_finished_active_data(
    globus_gfs_operation_t              op, 
    globus_result_t                     result,
    void *                              data_arg,
    globus_bool_t                       bi_directional);

/*
 * finished passive data
 * 
 * Speficic finished notification for completion of a passive data creation.
 */ 
void
globus_gridftp_server_finished_passive_data(
    globus_gfs_operation_t              op, 
    globus_result_t                     result,
    void *                              data_arg,
    globus_bool_t                       bi_directional,
    const char **                       contact_strings,
    int                                 cs_count);



/**
 * Data Read and Write Functions
 **/

/*
 * write
 * 
 * Register a write of specified buffer to the server.  You should use 
 * globus_gridftp_server_get_block_size() 
 * and globus_gridftp_server_get_optimal_concurrency() to determine the 
 * buffer size of each write and the number of writes you should have 
 * pending at all times. (pending meaning you are waiting for the callback).
 */     
typedef void
(*globus_gridftp_server_write_cb_t)(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       nbytes,
    void *                              user_arg);

globus_result_t
globus_gridftp_server_register_write(
    globus_gfs_operation_t              op,
    globus_byte_t *                     buffer,  
    globus_size_t                       length,  
    globus_off_t                        offset,  
    int                                 stripe_ndx,  
    globus_gridftp_server_write_cb_t    callback,  
    void *                              user_arg);

/*
 * read
 * 
 * Register a read of data from the server.  You should use 
 * globus_gridftp_server_get_block_size()  
 * and globus_gridftp_server_get_optimal_concurrency() to determine the 
 * buffer size you should use and the number of reads you should have 
 * pending at all times. (pending meaning you are waiting for the callback).
 */     
typedef void
(*globus_gridftp_server_read_cb_t)(
    globus_gfs_operation_t              op,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       nbytes,
    globus_off_t                        offset,
    globus_bool_t                       eof,
    void *                              user_arg);
 
globus_result_t
globus_gridftp_server_register_read(
    globus_gfs_operation_t              op,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_gridftp_server_read_cb_t     callback,  
    void *                              user_arg);


/*
 * register a custom command
 * 
 * This must be called during the DSI session_start_func() function.
 * When a command is triggered, command_func() will be called with a 
 * command_info->command equal to cmd_id.  Responses are handled as with 
 * any other command. Call globus_gridftp_server_finished_command() with 
 * a valid FTP response string in 'command_response' to customize your response
 * i.e. "250 The command was successful\r\n"
 * 
 * cmd_id must be >= GLOBUS_GFS_MIN_CUSTOM_CMD.
 *
 * If a command takes a pathname, it must be the final argument, and has_pathname
 * must be set to GLOBUS_TRUE.  commands should not take multiple pathnames.
 *
 * If the command takes a pathname, set access_type to an globus_gfs_acl_action_t
 * like one of: GFS_ACL_ACTION_READ, GFS_ACL_ACTION_WRITE, 
 * GFS_ACL_ACTION_CREATE, GFS_ACL_ACTION_DELETE, GFS_ACL_ACTION_LOOKUP.
 *
 * The last argument will always be passed in command_info->pathname, whether
 * it is a pathname or not.
 * Other args can be obtained by querying command_info->op_info for 
 * GLOBUS_GFS_OP_INFO_CMD_ARGS.   See globus_gridftp_server_query_op_info().
 *
 * Note for min_args and max_args, that the command itself counts as a argument 
 * (or 2, in the case of SITE commands).
 * 
 * A non-SITE command name must be exactly 4 characters long,
 * A SITE command (command name = "SITE SOMETHING") can be any length 
 * 
 * help_string should be of the form "COMMAND <sp> arg1 <sp> arg2 <sp> pathname"
 * 
 */ 

globus_result_t
globus_gridftp_server_add_command(
    globus_gfs_operation_t              op,
    const char *                        command_name,
    int                                 cmd_id,
    int                                 min_args,
    int                                 max_args,
    const char *                        help_string,
    globus_bool_t                       has_pathname,
    int                                 access_type);

/** Register support for checksum algorithms
*
* cksm_str is a string in the form of alg1:metric1;alg2:metric2;[...]
* example: SHA1:10;MD5:10:ADLER32:10;
*
* Range of metrics should be 1-20, and should be set to indicate not only
* a relative preference within the connector, but a general preference in
* terms of how expensive an algorithm is.
* General guidelines:
* 1-5: checksum is available with inexpensive metadata reads
* 6-9: checksum is often available with metadata reads
* 10-15: checksum is often recomputed, but data is readily available
* 15-20: checksum is often recomputed, data may be costly to read
*/
globus_result_t
globus_gridftp_server_set_checksum_support(
    globus_gfs_operation_t              op,
    const char *                        cksm_str);

/**
 * Helper Functions
 **/
 
typedef enum
{
    /* return argv and argc for the current command.  usually called when
     * handling custom commands.
     * 
     * char ***                         argv,
     * int *                            argc
     */
    GLOBUS_GFS_OP_INFO_CMD_ARGS = 1
} globus_gfs_op_info_param_t;

/* query op_info for parameters
 * query parameters listed in the globus_gfs_op_info_param_t enum.
 * the varargs should be populated with variables of the correct type to hold
 * the returning parameters for the requested param type.
 * 
*/
globus_result_t
globus_gridftp_server_query_op_info(
    globus_gfs_operation_t              op,
    globus_gfs_op_info_t                op_info,
    globus_gfs_op_info_param_t          param,
    ...);

/* check for attributes applicable to the current recv operation.
 * requested_attr is a case-insensitive string indicating the attribute 
 * whose value will returned in out_value.  requested_attr may be NULL, in 
 * which case the full attr string will be returned in out_value.
 * 
 * the format of the full attr string is attr1=value;attr2=value;...
 *
 * it is the caller's responsibility to free() out_value after a succesful return.
*/

globus_result_t
globus_gridftp_server_get_recv_attr_string(
    globus_gfs_operation_t              op,
    const char *                        requested_attr,
    char **                             out_value);

/* get intended modification time for the file being received.  out_time
 * will be the same as if a UTIME/MDTM command had been issued.  if the modify
 * time has not been requested by the client, this will return GLOBUS_SUCCESS
 * but out_time will be -1.
 */

globus_result_t
globus_gridftp_server_get_recv_modification_time(
    globus_gfs_operation_t              op,
    time_t *                            out_time);

/*
 * update bytes written to storage
 * 
 * This should be called during a recv(), after each successful write
 * to the storage system.
 * 
 * Use EITHER globus_gridftp_server_update_bytes_written() OR 
 * both globus_gridftp_server_update_bytes_recvd() and 
 * globus_gridftp_server_update_range_recvd() for a given range.
 */ 
void
globus_gridftp_server_update_bytes_written(
    globus_gfs_operation_t              op,
    globus_off_t                        offset,
    globus_off_t                        length);

/*
 * update bytes recieved, but not yet written to storage
 * use this when there may be a delay between reciving data 
 * and writing to storage.  this will ensure accurate performance
 * markers, but will not cause range/restart markers to be sent.
 * 
 * Use EITHER globus_gridftp_server_update_bytes_written() OR 
 * both globus_gridftp_server_update_bytes_recvd() and 
 * globus_gridftp_server_update_range_recvd() for a given range.
 */
void
globus_gridftp_server_update_bytes_recvd(
    globus_gfs_operation_t              op,
    globus_off_t                        length);

/*
 * update bytes written to storage
 * use this when there may be a delay between reciving data 
 * and writing to storage.  this will cause range/restart markers 
 * to be sent.
 * 
 * Use EITHER globus_gridftp_server_update_bytes_written() OR 
 * both globus_gridftp_server_update_bytes_recvd() and 
 * globus_gridftp_server_update_range_recvd() for a given range.
 */
void
globus_gridftp_server_update_range_recvd(
    globus_gfs_operation_t              op,
    globus_off_t                        offset,
    globus_off_t                        length);

/*
 * get concurrency
 * 
 * This should be called during a recv() and send() in order to know the
 * number of pending reads or writes you should have at once.
 */ 
void
globus_gridftp_server_get_optimal_concurrency(
    globus_gfs_operation_t              op,
    int *                               count);

/*
 * get blocksize
 * 
 * This should be called during a recv() and send() in order to know the
 * size of buffers that you should be passing to the server for reads and
 * writes.
 */ 
void
globus_gridftp_server_get_block_size(
    globus_gfs_operation_t              op,
    globus_size_t *                     block_size);

/*
 * get stripe blocksize
 * 
 * This can be called during a recv() and send() in modules that wish to 
 * deal with striping.
 */
void
globus_gridftp_server_get_stripe_block_size(
    globus_gfs_operation_t              op,
    globus_size_t *                     stripe_block_size);
    
/*
 * get session username
 * 
 * This should can be called to get the username that the process is running
 * under, which may be different from the username supplied in the
 * session_start call in some cases.  
 */ 
void
globus_gridftp_server_get_session_username(
    globus_gfs_operation_t              op,
    char **                             username);
   
/*
 * get delegated cred
 * 
 * This can can be called to get the delegated credential.  This may be called
 * with any op after session_start() and and the credential pointer will be
 * valid until at least session_stop.  del_cred will be NULL if it is not 
 * available.
 */
void
globus_gridftp_server_get_delegated_cred(
    globus_gfs_operation_t              op,
    gss_cred_id_t *                     del_cred);

/*
 * get security context (unsupported)
 * 
 * This can can be called to get the gssapi security context of the client 
 * connection.  This may be called with any op after session_start() and 
 * the context pointer will be valid until at least session_stop.  context 
 * will be NULL if it is not available.  This is needed for accessing proxy
 * extensions or other gssapi operations where the delegated credential is 
 * not sufficient.  The caller should not attempt operations that modify the 
 * context without fully understanding the effects. 
 */
void
globus_gridftp_server_get_sec_context(
    globus_gfs_operation_t              op,
    gss_ctx_id_t *                      context);


/* 
 * get/set ordered data requirement
 * 
 * The DSI must call this before globus_gridftp_server_begin_transfer()
 * to set the ordered_data flag.  This will ensure that the offsets read
 * in each data callback are in order, even when multiple streams are used. 
 * This will result in the transfer slowing down to match the speed of the 
 * slowest stream.  Note: in cases where the data source intentionally sends 
 * data out of order, this will result in an aborted transfer.  However, 
 * a DSI that needs ordered data would probably fail in such a scenario anyway.
 *
 * Instead of calling these functions, you can enable this setting for all
 * transfers by setting GLOBUS_GFS_DSI_DESCRIPTOR_REQUIRES_ORDERED_DATA
 * in the globus_gfs_storage_iface_t interface definition.
 */
void
globus_gridftp_server_set_ordered_data(
    globus_gfs_operation_t              op,
    globus_bool_t                       ordered_data);

void
globus_gridftp_server_get_ordered_data(
    globus_gfs_operation_t              op,
    globus_bool_t *                     ordered_data);

/*
 * get config string
 * 
 * This can be called to get the dsi specific configuration string 
 * that is defined in the global configuration. 
 */ 
void
globus_gridftp_server_get_config_string(
    globus_gfs_operation_t              op,
    char **                             config_string);

/*
 * get config data
 * 
 * This can be called to get the configuration data managed by the server.
 * data_id can be NULL, or can be used to specify a specific set of data.
 * config_data will always return NULL.
 */ 
void
globus_gridftp_server_get_config_data(
    globus_gfs_operation_t              op,
    char *                              data_id,
    char **                             config_data);

void
globus_gfs_data_get_file_stack_list(
    globus_gfs_operation_t              in_op,
    globus_list_t **                    out_list);

char *
globus_gfs_data_get_cmd_string(
    globus_gfs_operation_t              op);

void
globus_gridftp_server_get_update_interval(
    globus_gfs_operation_t              op,
    int *                               interval);
    
/*
 * get read_range
 * 
 * This should be called during send() in order to know the specific
 * offset and length of the file to read from the storage system
 * You should continue calling this and transferring the speficied data
 * until it returns a length of 0.
 */ 
void
globus_gridftp_server_get_read_range(
    globus_gfs_operation_t              op,
    globus_off_t *                      offset,
    globus_off_t *                      length);


/*
 * get write_range
 * 
 * This could be called during recv() in order to get hints on the specific
 * offset and length that the data will be expected to come from 
 * globus_gridftp_server_register_read() callbacks.  Note that this is 
 * only a hint, and not necessarily the exact data ranges that will come.
 * You would continue calling this until it returns a length of 0.
 */ 
void
globus_gridftp_server_get_write_range(
    globus_gfs_operation_t              op,
    globus_off_t *                      offset,
    globus_off_t *                      length);


/* END Storage Interface API */

typedef enum
{
    GLOBUS_GFS_LOG_ERR = 0x01,
    GLOBUS_GFS_LOG_WARN = 0x02,
    GLOBUS_GFS_LOG_TRANSFER = 0x04,
    GLOBUS_GFS_LOG_INFO = 0x08,
    GLOBUS_GFS_LOG_DUMP = 0x10,
    
    GLOBUS_GFS_LOG_ALL = 0xFF
} globus_gfs_log_type_t;

void
globus_gfs_log_message(
    globus_gfs_log_type_t               type,
    const char *                        format,
    ...);

void
globus_gfs_log_result(
    globus_gfs_log_type_t               type,
    const char *                        lead,
    globus_result_t                     result);

/** Error and result object helper macros */   
enum
{
    GLOBUS_GFS_DEBUG_TRACE = 8,
    GLOBUS_GFS_DEBUG_INFO = 16,
    GLOBUS_GFS_DEBUG_STATE = 32
};

#ifdef __GNUC__
#define GlobusGFSName(func) static const char * _gfs_name __attribute__((__unused__)) = #func
#else
#define GlobusGFSName(func) static const char * _gfs_name = #func
#endif

GlobusDebugDeclare(GLOBUS_GRIDFTP_SERVER);

#define GlobusGFSDebugPrintf(level, message)                                \
    GlobusDebugPrintf(GLOBUS_GRIDFTP_SERVER, level, message)

#define GlobusGFSDebugInfo(_msg)                                            \
    GlobusGFSDebugPrintf(                                                   \
        GLOBUS_GFS_DEBUG_INFO,                                              \
        ("[%s] %s\n", __func__, _msg))

#define GlobusGFSDebugEnter()                                               \
    GlobusGFSDebugPrintf(                                                   \
        GLOBUS_GFS_DEBUG_TRACE,                                             \
        ("[%s] Entering\n", __func__))
        
#define GlobusGFSDebugExit()                                                \
    GlobusGFSDebugPrintf(                                                   \
        GLOBUS_GFS_DEBUG_TRACE,                                             \
        ("[%s] Exiting\n", __func__))

#define GlobusGFSDebugState(_state)                                         \
    GlobusGFSDebugPrintf(                                                   \
        GLOBUS_GFS_DEBUG_INFO,                                             \
        ("[%s] State: %d\n", __func__, _state))

#define GlobusGFSDebugExitWithError()                                       \
    GlobusGFSDebugPrintf(                                                   \
        GLOBUS_GFS_DEBUG_TRACE,                                             \
        ("[%s] Exiting with error\n", __func__))

#define GlobusGFSErrorParameter(mem_name)                                   \
    globus_error_put(GlobusGFSErrorObjParameter(mem_name)) 

#define GlobusGFSErrorIPC()                                                 \
    globus_error_put(GlobusGFSErrorObjIPC())

#define GlobusGFSErrorObjIPC()                                              \
    globus_error_construct_error(                                           \
        NULL,                                                        \
        NULL,                                                        \
        GLOBUS_GFS_ERROR_MEMORY,                                            \
        __FILE__,                                                           \
        __func__,                                                           \
        __LINE__,                                                           \
        "IPC Communication error.")
                                                                            
#define GlobusGFSErrorObjParameter(param_name)                              \
    globus_error_construct_error(                                           \
        NULL,                                                        \
        NULL,                                                        \
        GLOBUS_GFS_ERROR_PARAMETER,                                         \
        __FILE__,                                                           \
        __func__,                                                           \
        __LINE__,                                                           \
        "invalid parameter: %s",                                            \
        (param_name))                               
                                                                            
#define GlobusGFSErrorSystemError(system_func, system_errno)                \
    globus_error_put(GlobusGFSErrorObjSystemError(\
            (system_func), (system_errno)))
#define GlobusGFSErrorObjSystemError(system_func, system_errno)             \
        globus_i_gfs_error_system(                                          \
                0, (system_errno),                                          \
                "System error%s%s",                                         \
                (system_func) != NULL ? " in " : "",                        \
                (system_func) != NULL ? (system_func) : "")
                                                                            
#define GlobusGFSErrorWrapFailed(failed_func, result)                       \
    globus_error_put(GlobusGFSErrorObjWrapFailed(failed_func, result))

#define GlobusGFSErrorObjWrapFailed(failed_func, result)                    \
    globus_error_construct_error(                                           \
        NULL,                                                        \
        globus_error_get((result)),                                         \
        GLOBUS_GFS_ERROR_WRAPPED,                                           \
        __FILE__,                                                           \
        __func__,                                                           \
        __LINE__,                                                           \
        "%s failed.",                                                       \
        (failed_func))

#define GlobusGFSErrorData(reason)                                          \
    globus_error_put(GlobusGFSErrorObjData(reason))                               

#define GlobusGFSErrorObjData(reason)                                       \
    globus_error_construct_error(                                           \
        NULL,                                                        \
        NULL,                                                        \
        GLOBUS_GFS_ERROR_DATA,                                              \
        __FILE__,                                                           \
        __func__,                                                           \
        __LINE__,                                                           \
        "%s",                                                               \
        (reason))
 
#define GlobusGFSErrorGeneric(reason)                                       \
    globus_error_put(GlobusGFSErrorObjGeneric(reason))                               

#define GlobusGFSErrorObjGeneric(reason)                                    \
    globus_error_construct_error(                                           \
        NULL,                                                        \
        NULL,                                                        \
        GLOBUS_GFS_ERROR_GENERIC,                                           \
        __FILE__,                                                           \
        __func__,                                                           \
        __LINE__,                                                           \
        "%s",                                                               \
        (reason))                             
 
globus_object_t *
globus_gfs_ftp_response_error_construct(
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    int                                 response_code,
    const char                         *response_error_code,
    const char                         *fmt,
    ...);
 
globus_object_t *
globus_gfs_ftp_response_error_v_initialize(
    globus_object_t *                   error,
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    int                                 response_code,
    const char *                        response_error_code,
    const char *                        fmt,
    va_list                             ap);
globus_object_t *
globus_gfs_ftp_response_error_initialize(
    globus_object_t *                   error,
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    int                                 response_code,
    const char *                        response_error_code,
    const char *                        fmt,
    ...);

int
globus_gfs_error_get_ftp_response_code(
    globus_object_t *                   error);

const char *
globus_gfs_error_get_ftp_response_error_code(
    globus_object_t *                   error);

globus_bool_t
globus_gfs_error_match_response_error_code(
    globus_object_t *                   error,
    const char *                        response_error_code);

extern const globus_object_type_t
        GLOBUS_GFS_ERROR_FTP_RESPONSE_TYPE_DEFINITION;

#define GLOBUS_GFS_ERROR_FTP_RESPONSE_TYPE \
        (&GLOBUS_GFS_ERROR_FTP_RESPONSE_TYPE_DEFINITION)


#define GlobusGFSErrorFtpResponse(cause, code, response_error_code, ...)    \
    globus_error_put(GlobusGFSErrorObjFtpResponse(                          \
            cause, code, response_error_code, __VA_ARGS__))

#define GlobusGFSErrorObjFtpResponse(cause, code, response_error_code, ...) \
    globus_gfs_ftp_response_error_construct(                                \
            NULL,                                                           \
            cause,                                                          \
            code,                                                           \
            response_error_code,                                            \
            __VA_ARGS__)

globus_object_t *
globus_i_gfs_error_system(int ftp_code, int system_errno, const char *fmt, ...);

#define GlobusGFSErrorMemory(mem)                                           \
    globus_error_put(GlobusGFSErrorObjMemory(mem))
#define GlobusGFSErrorObjMemory(mem)                                        \
    GlobusGFSErrorObjSystemError("malloc", errno)

#define GlobusGFSErrorObj(cause, response_code, ...)                        \
    GlobusGFSErrorObjFtpResponse(cause, response_code, __VA_ARGS__)

#define GlobusGFSErrorPathNotFound(p)                                       \
        globus_error_put(GlobusGFSErrorObjPathNotFound(NULL, p))
#define GlobusGFSErrorObjPathNotFound(cause, p)                             \
        GlobusGFSErrorObj((cause), 550, "PATH_NOT_FOUND",                   \
                "%s%s%s",                                                   \
                ((p) != NULL) ? "GridFTP-Path: \"" : "",                    \
                ((p) != NULL) ? (p) : "",                                   \
                ((p) != NULL) ? "\"" : "")

#define GlobusGFSErrorIncorrectChecksum(computed, expected)                 \
        globus_error_put(GlobusGFSErrorObjIncorrectChecksum(                \
                NULL, computed, expected))
#define GlobusGFSErrorObjIncorrectChecksum(cause, computed, expected)       \
        GlobusGFSErrorObj(                                                  \
                (cause),                                                    \
                550,                                                        \
                "INCORRECT_CHECKSUM",                                       \
                "GridFTP-Computed-Checksum: %s\n"                           \
                "GridFTP-Expected-Checksum: %s", (computed), (expected))

#define GlobusGFSErrorMultipartUploadNotFound()                             \
        globus_error_put(GlobusGFSErrorObjMultipartUploadNotFound(NULL))
#define GlobusGFSErrorObjMultipartUploadNotFound(cause)                     \
        GlobusGFSErrorObj(cause, 553, "MULTI_PART_UPLOAD_NOT_FOUND", NULL)

#define GlobusGFSErrorAppendNotSupported()                                  \
        globus_error_put(GlobusGFSErrorObjAppendNotSupported(NULL))
#define GlobusGFSErrorObjAppendNotSupported(cause)                          \
        GlobusGFSErrorObj((cause), 553, "APPEND_NOT_SUPPORTED", NULL)

#define GlobusGFSErrorAmbiguousPath(ambiguity)                              \
        globus_error_put(GlobusGFSErrorObjAmbiguousPath(NULL, ambiguity))
#define GlobusGFSErrorObjAmbiguousPath(cause, ambiguity)                    \
        GlobusGFSErrorObj(                                                  \
                (cause),                                                    \
                553,                                                        \
                "AMBIGUOUS_PATH",                                           \
                "GridFTP-Path: %s",                                         \
                (ambiguity))

#define GlobusGFSErrorTooBusy()                                             \
        globus_error_put(GlobusGFSErrorObjTooBusy(NULL))
#define GlobusGFSErrorObjTooBusy(cause)                                     \
        GlobusGFSErrorObj((cause), 451, "TOO_BUSY", NULL)

#define GlobusGFSErrorDataChannelAuthenticationFailure()                    \
        globus_error_put(GlobusGFSErrorObjDataChannelAuthenticationFailure( \
                NULL))
#define GlobusGFSErrorObjDataChannelAuthenticationFailure(cause)            \
        GlobusGFSErrorObj((cause), 425,                                     \
                "DATA_CHANNEL_AUTHENTICATION_FAILURE", NULL)

#define GlobusGFSErrorDataChannelCommunicationFailure()                     \
        globus_error_put(GlobusGFSErrorObjDataChannelCommunicationFailure(  \
                NULL))
#define GlobusGFSErrorObjDataChannelCommunicationFailure(cause)             \
        GlobusGFSErrorObj((cause), 425,                                     \
                "DATA_CHANNEL_COMMUNICATION_FAILURE", NULL)

#define GlobusGFSErrorLoginDenied()                                         \
        globus_error_put(GlobusGFSErrorObjLoginDenied(NULL))
#define GlobusGFSErrorObjLoginDenied(cause) \
        GlobusGFSErrorObj((cause), 530, "LOGIN_DENIED", NULL)

#define GlobusGFSErrorPermissionDenied()                                    \
        globus_error_put(GlobusGFSErrorObjPermissionDenied(NULL))
#define GlobusGFSErrorObjPermissionDenied(cause)                            \
        GlobusGFSErrorObj((cause), 550, "PERMISSION_DENIED", NULL)

#define GlobusGFSErrorQuotaExceeded()                                       \
        globus_error_put(GlobusGFSErrorObjQuotaExceeded(NULL))
#define GlobusGFSErrorObjQuotaExceeded(cause)                               \
        GlobusGFSErrorObj((cause), 451, "QUOTA_EXCEEDED", NULL)

#define GlobusGFSErrorNoSpaceLeft()                                         \
        globus_error_put(GlobusGFSErrorObjNoSpaceLeft(NULL))
#define GlobusGFSErrorObjNoSpaceLeft(cause)                                 \
        GlobusGFSErrorObj((cause), 451, "NO_SPACE_LEFT", NULL)

#define GlobusGFSErrorInvalidPathName(name)                                 \
        globus_error_put(GlobusGFSErrorObjInvalidPathName(NULL, name))
#define GlobusGFSErrorObjInvalidPathName(cause, name)                       \
        GlobusGFSErrorObj((cause), 553, "INVALID_PATH_NAME",                \
                "GridFTP-Path: %s", name)

#define GlobusGFSErrorPathExists(name)                                      \
        globus_error_put(GlobusGFSErrorObjPathExists(NULL, name))
#define GlobusGFSErrorObjPathExists(cause, name)                            \
        GlobusGFSErrorObj((cause), 553, "PATH_EXISTS",                      \
                "GridFTP-Path: %s", name)

#define GlobusGFSErrorIsADirectory(name)                                    \
        globus_error_put(GlobusGFSErrorObjIsADirectory(NULL, name))
#define GlobusGFSErrorObjIsADirectory(cause, name)                          \
        GlobusGFSErrorObj((cause), 553, "IS_A_DIRECTORY",                   \
                "GridFTP-Path: %s", name)

#define GlobusGFSErrorNotADirectory(name)                                   \
        globus_error_put(GlobusGFSErrorObjNotADirectory(NULL, name))
#define GlobusGFSErrorObjNotADirectory(cause, name)                         \
        GlobusGFSErrorObj((cause), 553, "NOT_A_DIRECTORY",                  \
                "GridFTP-Path: %s", name)

#define GlobusGFSErrorCRLError()                                            \
        globus_error_put(GlobusGFSErrorObjCRLError(NULL))
#define GlobusGFSErrorObjCRLError(cause)                                    \
        GlobusGFSErrorObj((cause), 530, "CRL_ERROR", NULL)

#define GlobusGFSErrorInternalError(generic_string)                         \
        globus_error_put(GlobusGFSErrorObjInternalError(                    \
                NULL, (generic_string)))
#define GlobusGFSErrorObjInternalError(cause, generic_string)               \
        GlobusGFSErrorObj((cause), 500, "INTERNAL_ERROR",                   \
                "%s%s",                                                     \
                ((generic_string) != NULL) ? "GridFTP-Error: " : "",        \
                ((generic_string) != NULL) ? generic_string : "")

#define GlobusGFSErrorNotImplemented()                                      \
        globus_error_put(GlobusGFSErrorObjNotImplemented(NULL))
#define GlobusGFSErrorObjNotImplemented(cause)                              \
        GlobusGFSErrorObj((cause), 500, "NOT_IMPLEMETED", NULL)

#define GlobusGFSErrorNotImplementedFeature(feature)                        \
        globus_error_put(GlobusGFSErrorObjNotImplementedFeature(NULL, feature))
#define GlobusGFSErrorObjNotImplementedFeature(cause, feature)              \
        GlobusGFSErrorObj((cause), 500,                                     \
        "NOT_IMPLEMETED", "GridFTP-Feature: %s", (feature))

#define GlobusGFSErrorConfigurationError()                                  \
        globus_error_put(GlobusGFSErrorObjConfigurationError(NULL))
#define GlobusGFSErrorObjConfigurationError(cause)                          \
        GlobusGFSErrorObj((cause), 500, "CONFIGURATION_ERROR", NULL)
/* 
 * 
 * IPC 
 * 
 */ 
 
typedef struct globus_i_gfs_ipc_handle_s * globus_gfs_ipc_handle_t;

/*
 *  callbacks
 *
 *  all functions have the same callback, they examine the
 *  globus_gfs_finished_info_t() structure for their specific info
 *
 *  error_cb
 *  can be called at anytime.  typically means the ipc connection broke
 *  in an irrecoverable way.  Even tho this is called all outstanding
 *  callbacks will still be called (but with an error)
 */
 
 /*
 *  replying
 *
 *  every comman requires a reply and comes with a reply id.  to reply
 *  the requested side must fill in the globus_gfs_finished_info_t
 *  structure and then pass it
 *  to the function: globus_gfs_ipc_reply();  That call will result in
 *  the ipc communication that will untilimately call the callback
 *  on the callers side.
 */
typedef void
(*globus_gfs_ipc_callback_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_result_t                     result,
    globus_gfs_finished_info_t *            reply,
    void *                              user_arg);

typedef void
(*globus_gfs_ipc_event_callback_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_result_t                     result,
    globus_gfs_event_info_t *      reply,
    void *                              user_arg);

typedef void
(*globus_gfs_ipc_close_callback_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_result_t                     result,
    void *                              user_arg);

typedef void
(*globus_gfs_ipc_open_callback_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_result_t                     result,
    globus_gfs_finished_info_t *            reply,
    void *                              user_arg);

typedef void
(*globus_gfs_ipc_error_callback_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_result_t                     result,
    void *                              user_arg);

globus_result_t
globus_gfs_ipc_reply_finished(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_finished_info_t *            reply);

globus_result_t
globus_gfs_ipc_reply_event(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_event_info_t *      reply);

globus_result_t
globus_gfs_ipc_reply_session(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_finished_info_t *            reply);

/*
 *  sending
 *
 *  every command has a corresponding iface function.  A call to a
 *  command function results in a call to the correspoding iface
 *  function on the other side of the channel.
 *
 *  all parmeters are wrapped in a structure corresponding to
 *  each function call type.  those structures are defined below
 */

typedef void
(*globus_i_gfs_ipc_data_callback_t)(
    globus_gfs_finished_info_t *            reply,
    void *                              user_arg);

typedef void
(*globus_i_gfs_ipc_data_event_callback_t)(
    globus_gfs_event_info_t *      reply,
    void *                              user_arg);

typedef void
(*globus_i_gfs_ipc_done_callback_t)(
    void *                              user_arg,
    globus_result_t                     result);

/*************************************************************************
 *  interface function
 *  ------------------
 *
 ************************************************************************/
/* works with handle get */
typedef void
(*globus_gfs_ipc_iface_session_start_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    const gss_ctx_id_t                  context,
    globus_gfs_session_info_t *         session_info,
    globus_i_gfs_ipc_data_callback_t    cb,
    void *                              user_arg);

globus_result_t
globus_gfs_ipc_start_session(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_session_info_t *         session_info,
    globus_gfs_ipc_callback_t           cb,
    void *                              user_arg);

/* works with release */
typedef void
(*globus_gfs_ipc_iface_session_stop_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle);

globus_result_t
globus_gfs_ipc_iface_session_stop(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle);

typedef void
(*globus_gfs_ipc_iface_set_cred_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle,
    gss_cred_id_t                       del_cred);

globus_result_t
globus_gfs_ipc_set_cred(
    globus_gfs_ipc_handle_t             ipc_handle,
    gss_cred_id_t                       del_cred);

typedef void
(*globus_gfs_ipc_iface_buffer_send_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle,
    globus_byte_t *                     buffer,
    int                                 buffer_type,
    globus_size_t                       buffer_len);

globus_result_t
globus_gfs_ipc_request_buffer_send(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_byte_t *                     buffer,
    int                                 buffer_type,
    globus_size_t                       buffer_len);

/*
 *  receive
 *
 *  tell the remote process to receive a file
 */
typedef void
(*globus_gfs_ipc_iface_recv_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle,
    int                                 id,
    globus_gfs_transfer_info_t *        recv_info,
    globus_i_gfs_ipc_data_callback_t          cb,
    globus_i_gfs_ipc_data_event_callback_t    event_cb,
    void *                              user_arg);

globus_result_t
globus_gfs_ipc_request_recv(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_transfer_info_t *        recv_info,
    globus_gfs_ipc_callback_t           cb,
    globus_gfs_ipc_event_callback_t     event_cb,
    void *                              user_arg);

/*
 *  send
 *  
 *  tell remote process to send a file
 */
typedef void
(*globus_gfs_ipc_iface_send_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle,
    int                                 id,
    globus_gfs_transfer_info_t *        send_info,
    globus_i_gfs_ipc_data_callback_t          cb,
    globus_i_gfs_ipc_data_event_callback_t    event_cb,
    void *                              user_arg);

globus_result_t
globus_gfs_ipc_request_send(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_transfer_info_t *        send_info,
    globus_gfs_ipc_callback_t           cb,
    globus_gfs_ipc_event_callback_t     event_cb,
    void *                              user_arg);

typedef void
(*globus_gfs_ipc_iface_list_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle,
    int                                 id,
    globus_gfs_transfer_info_t *        list_info,
    globus_i_gfs_ipc_data_callback_t    cb,
    globus_i_gfs_ipc_data_event_callback_t    event_cb,
    void *                              user_arg);

globus_result_t
globus_gfs_ipc_request_list(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_transfer_info_t *        data_info,
    globus_gfs_ipc_callback_t           cb,
    globus_gfs_ipc_event_callback_t     event_cb,
    void *                              user_arg);

/*
 *  command
 *
 *  tell remote side to execute the given command
 */
typedef void
(*globus_gfs_ipc_iface_command_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle,
    int                                 id,
    globus_gfs_command_info_t *         cmd_info,
    globus_i_gfs_ipc_data_callback_t    cb,
    void *                              user_arg);

globus_result_t
globus_gfs_ipc_request_command(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_command_info_t *         cmd_info,
    globus_gfs_ipc_callback_t           cb,
    void *                              user_arg);

/*
 *  active data
 *
 *  tell remote side to create an active data connection
 */
typedef void
(*globus_gfs_ipc_iface_active_data_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle,
    int                                 id,
    globus_gfs_data_info_t *            data_info,
    globus_i_gfs_ipc_data_callback_t    cb,
    void *                              user_arg);

globus_result_t
globus_gfs_ipc_request_active_data(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_data_info_t *            data_info,
    globus_gfs_ipc_callback_t           cb,
    void *                              user_arg);

/*
 *  passive data
 *
 *  tell remote side to do passive data connection
 */
typedef void
(*globus_gfs_ipc_iface_passive_data_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle,
    int                                 id,
    globus_gfs_data_info_t *            data_info,
    globus_i_gfs_ipc_data_callback_t    cb,
    void *                              user_arg);

globus_result_t
globus_gfs_ipc_request_passive_data(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_data_info_t *            data_info,
    globus_gfs_ipc_callback_t           cb,
    void *                              user_arg);

/*
 *  send stat request
 */
typedef void
(*globus_gfs_ipc_iface_stat_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle,
    int                                 id,
    globus_gfs_stat_info_t *            stat_info,
    globus_i_gfs_ipc_data_callback_t    cb,
    void *                              user_arg);

globus_result_t
globus_gfs_ipc_request_stat(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_stat_info_t *            stat_info,
    globus_gfs_ipc_callback_t           cb,
    void *                              user_arg);

/*
 * poke transfer event request
 */
typedef void
(*globus_gfs_ipc_iface_transfer_event_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle,
    globus_gfs_event_info_t *           event_info);


globus_result_t
globus_gfs_ipc_request_transfer_event(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_event_info_t *           event_info);


/*
 *  destroy a data connection associated with the given ID
 */
typedef void
(*globus_gfs_ipc_iface_data_destroy_t)(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_handle,
    void *                              data_arg);

globus_result_t
globus_gfs_ipc_request_data_destroy(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              data_arg);

typedef struct globus_i_gfs_ipc_iface_s
{
    globus_gfs_ipc_iface_session_start_t    session_start_func;
    globus_gfs_ipc_iface_session_stop_t     session_stop_func;
    globus_gfs_ipc_iface_recv_t             recv_func;
    globus_gfs_ipc_iface_send_t             send_func;
    globus_gfs_ipc_iface_command_t          command_func;
    globus_gfs_ipc_iface_active_data_t      active_func;
    globus_gfs_ipc_iface_passive_data_t     passive_func;
    globus_gfs_ipc_iface_data_destroy_t     data_destroy_func;
    globus_gfs_ipc_iface_stat_t             stat_func;
    globus_gfs_ipc_iface_list_t             list_func;
    globus_gfs_ipc_iface_transfer_event_t   transfer_event_func;
    globus_gfs_ipc_iface_set_cred_t         set_cred;
    globus_gfs_ipc_iface_buffer_send_t      buffer_send;
} globus_gfs_ipc_iface_t;

/* 
 *  getting an IPC handle
 */

/* 
 *  create an IPC handle from a xio system handle, can be used
 *  imediately, is not in handle table
 */
globus_result_t
globus_gfs_ipc_handle_create(
    globus_gfs_ipc_iface_t *            iface,
    globus_xio_system_socket_t          system_handle,
    globus_i_gfs_ipc_done_callback_t    done_cb,
    void *                              user_arg);

/*
 *  actually close the handle
 */
globus_result_t
globus_gfs_ipc_close(
    globus_gfs_ipc_handle_t             ipc_handle,
    globus_gfs_ipc_close_callback_t     cb,
    void *                              user_arg);

globus_result_t
globus_gfs_ipc_reply_close(
    globus_gfs_ipc_handle_t             ipc_handle);

globus_result_t
globus_gfs_ipc_session_stop(
    globus_gfs_ipc_handle_t             ipc_handle);

globus_result_t
globus_gfs_ipc_handle_connect(
    globus_gfs_session_info_t *         session_info,
    globus_gfs_ipc_open_callback_t      cb,
    void *                              user_arg,
    globus_gfs_ipc_error_callback_t     error_cb,
    void *                              error_user_arg);

globus_result_t
globus_gfs_ipc_handle_connect_ex(
    globus_gfs_session_info_t *         session_info,
    globus_gfs_ipc_open_callback_t      cb,
    void *                              user_arg,
    globus_gfs_ipc_error_callback_t     error_cb,
    void *                              error_user_arg,
    globus_bool_t                       secure_ipc,
    gss_cred_id_t                       cred,
    const char                         *auth_mode,
    const char                         *subject,
    time_t                              connect_timeout,
    time_t                              idle_timeout,
    globus_bool_t                       inetd);

globus_result_t
globus_gfs_ipc_handle_obtain(
    globus_gfs_session_info_t *         session_info,
    globus_gfs_ipc_iface_t *            iface,
    globus_gfs_ipc_open_callback_t      cb,
    void *                              user_arg,
    globus_gfs_ipc_error_callback_t     error_cb,
    void *                              error_user_arg);

/*
 *  the brain bit
 */
#define BRAIN_SYMBOL_NAME (void*)"gridftp_brain"
extern globus_extension_registry_t      brain_i_registry;

typedef globus_result_t
(*globus_i_gfs_brain_select_nodes_func_t)(
    globus_i_gfs_brain_node_t ***       out_node_array,
    int *                               out_array_length,
    const char *                        repo_name,
    globus_off_t                        filesize,
    int                                 min_count,
    int                                 max_count);

typedef globus_result_t
(*globus_i_gfs_brain_release_node_func_t)(
    globus_i_gfs_brain_node_t *         contact_node,
    globus_gfs_brain_reason_t           reason);

typedef globus_result_t
(*globus_i_gfs_brain_init_func_t)(
    globus_callback_func_t              ready_cb,
    void *                              ready_cb_arg);

typedef void
(*globus_i_gfs_brain_stop_func_t)();

typedef globus_result_t
(*globus_i_gfs_brain_get_available_func_t)(
    const char *                        user_id,
    const char *                        repo_name,
    int *                               count);


typedef struct globus_i_gfs_brain_module_s
{
    globus_i_gfs_brain_init_func_t          init_func;
    globus_i_gfs_brain_stop_func_t          stop_func;
    globus_i_gfs_brain_select_nodes_func_t  select_func;
    globus_i_gfs_brain_release_node_func_t  release_func;
    globus_i_gfs_brain_get_available_func_t available_func;
} globus_i_gfs_brain_module_t;

extern globus_i_gfs_brain_module_t globus_i_gfs_default_brain;

globus_result_t
globus_gfs_brain_select_nodes(
    globus_i_gfs_brain_node_t ***       out_node_array,
    int *                               out_array_length,
    const char *                        repo_name,
    globus_off_t                        filesize,
    int                                 min_count,
    int                                 max_count);

globus_result_t
globus_gfs_brain_release_node(
    globus_i_gfs_brain_node_t *         contact_node,
    globus_gfs_brain_reason_t           reason);

globus_result_t
globus_gfs_brain_get_available(
    const char *                        user_id,
    const char *                        repo_name,
    int *                               count);

globus_result_t
globus_gfs_ipc_handle_get_contact_string(
    globus_gfs_ipc_handle_t             ipc_handle,
    char **                             contact_string);

globus_result_t
globus_gfs_ipc_init(
    globus_bool_t                       requester);

/*
 *
 */
void
globus_gfs_ipc_add_server(
    globus_xio_server_t                 server_handle);

extern globus_gfs_ipc_iface_t  globus_gfs_ipc_default_iface;

/* end IPC */ 

/* ACL interface */

/*
 * interface implementation functions
 * see the globus_gridftp_server_acl_example package at 
 * gridftp/server/acl/example for an example implementation.
 */

/* acl handle object.  members are internal use only. */
typedef struct globus_i_gfs_acl_handle_s * globus_gfs_acl_handle_t;

/* supported actions, all authorization callouts will be of these types.
 * an authorization callout should return success for any actions that 
 * are not interesting. */
typedef enum globus_gfs_acl_action_e
{
    /* internal use only */
    GFS_ACL_ACTION_INIT = 1,
    /* the named object. will be deleted. */
    GFS_ACL_ACTION_DELETE,
    /* write to an existing object */
    GFS_ACL_ACTION_WRITE,
    /* create and write to a non-existant object */
    GFS_ACL_ACTION_CREATE,
    /* read an object */
    GFS_ACL_ACTION_READ,
    /* query metadata of an object (i.e. list) */
    GFS_ACL_ACTION_LOOKUP,
    /* speficy an authorization assertion.  client may submit data to 
     * influence future authorization decisions.  data is in an unspecified
     * format. */
    GFS_ACL_ACTION_AUTHZ_ASSERT,
    /* report data safely written to disk.  failure means data written has
     * overrun acceptable limits. */
    GFS_ACL_ACTION_COMMIT,
    /* increase previously requested write limits for an object */
    GFS_ACL_ACTION_GROW
} globus_gfs_acl_action_t;

/* user connection descriptor.  this provides info about the user
 * attempting the connection or action */
typedef struct globus_gfs_acl_info_s
{
    char *                              hostname;
    char *                              subject;
    char *                              username;
    char *                              password;
    char *                              ipaddr;
    gss_ctx_id_t                        context;
} globus_gfs_acl_info_t;

/* object descriptor.  this provides various info about the object of the 
 * action attempt. */
typedef struct globus_gfs_acl_object_desc_s
{
    /* ALL: name of the object.  commonly a filename. 
     * value is NULL when not known or not used. */
    char *                              name;

    /* WRITE/CREATE: size being requested to write.
     * COMMIT: amount of data already written safely.
     * GROW: new full size being requested to write. 
     * value is 0 when not known or not used. */
    globus_off_t                        size;

    /* AUTHZ_ASSERT: assertion data from the client. 
     * value is NULL when not known or not used. */
    char *                              data;
 
    /* COMMIT: all data has been safely written 
     * value is FALSE when not known or not used. */
    globus_bool_t                       final;

    /** op info */
    globus_gfs_op_info_t                op_info;
} globus_gfs_acl_object_desc_t;

/* return values for authorization functions */
typedef enum globus_gfs_acl_status_e
{
    /* decision is complete */
    GLOBUS_GFS_ACL_COMPLETE = 1,
    /* decision will be made in a seperate call to 
    globus_gfs_acl_authorized_finished() */
    GLOBUS_GFS_ACL_WOULD_BLOCK
} globus_gfs_acl_status_t;

/* initialization callout.  this is ususally necessary.  must be
 * implemented if:
 * 1) we need to set up some sort of internal state/handle that can be passed
 * back to us in all callouts
 * and/or
 * 2) we are interested in authorizing the gridftp session based on client
 * user information.
 * 
 * must return GLOBUS_GFS_ACL_COMPLETE or GLOBUS_GFS_ACL_WOULD_BLOCK, and
 * store GLOBUS_SUCCESS or an error result_t in out_res.  if returning 
 * GLOBUS_GFS_ACL_WOULD_BLOCK, the result must be returned in a call to 
 * globus_gfs_acl_authorized_finished().  optionally, a pointer may be stored
 * in out_handle.  this pointer will then be passed back in later callouts.
 */
typedef int
(*globus_gfs_acl_init_t)(
    void **                             out_handle,
    globus_gfs_acl_info_t *             acl_info,
    globus_gfs_acl_handle_t             acl_handle,
    globus_result_t *                   out_res);

/* authorization callout.  this is usually necessary.  here we will 
 * get called to authrorize all actions the client performs.  see the
 * globus_gfs_acl_action_t declaration for all of the supported actions.
 * 
 * must return GLOBUS_GFS_ACL_COMPLETE or GLOBUS_GFS_ACL_WOULD_BLOCK, and
 * store GLOBUS_SUCCESS or an error result_t in out_res.  If returning 
 * GLOBUS_GFS_ACL_WOULD_BLOCK, the result must be returned in a call to 
 * globus_gfs_acl_authorized_finished().
 */
typedef int
(*globus_gfs_acl_authorize_t)(
    void *                              out_handle,
    globus_gfs_acl_action_t             action,
    globus_gfs_acl_object_desc_t *      object,
    globus_gfs_acl_info_t *             acl_info,
    globus_gfs_acl_handle_t             acl_handle,
    globus_result_t *                   out_res);

/* destructor callout. clean up our session state if necessary */
typedef void
(*globus_gfs_acl_destroy_t)(
    void *                              out_handle);

/* audit callout.  informational callout only.  implement this if you would
 * like to be notified of activities, but don't need to allow/deny them. */
typedef void
(*globus_gfs_acl_audit_t)(
    void *                              out_handle,
    globus_gfs_acl_action_t             action,
    globus_gfs_acl_object_desc_t *      object,
    const char *                        message);

/* acl module descriptor.  
 * Only define the functions you implement, otherwise NULL */
typedef struct globus_gfs_acl_module_s
{
    globus_gfs_acl_init_t               init_func;
    globus_gfs_acl_authorize_t          authorize_func;
    globus_gfs_acl_destroy_t            destroy_func;
    globus_gfs_acl_audit_t              audit_func;
} globus_gfs_acl_module_t;

/* authorization finalization function.  this must be called when the 
 * initialization or authorization callouts return GLOBUS_GFS_ACL_WOULD_BLOCK.
 */ 
void
globus_gfs_acl_authorized_finished(
    globus_gfs_acl_handle_t             acl_handle,
    globus_result_t                     result);

/* helper function to get strings from action types. useful for log/error 
 * messages */
const char * 
globus_gfs_acl_action_to_string(
    globus_gfs_acl_action_t             action);

    
/* end ACL */


/* config locking functions */
typedef 
void
(*globus_i_gfs_config_set_string_cb_t)(
    const char *                        option_name,
    const char *                        val,
    void *                              user_arg);

typedef 
void
(*globus_i_gfs_config_set_int_cb_t)(
    const char *                        option_name,
    int                                 val,
    void *                              user_arg);

typedef struct
{
    void *                              user_arg;
    globus_bool_t                       enabled;
    void *                              cb;
} globus_i_gfs_config_option_cb_ent_t;

void
globus_gfs_config_enable_cb(
    globus_i_gfs_config_option_cb_ent_t * cb_handle,
    globus_bool_t                       enabled);

int
globus_gfs_config_add_cb(
    globus_i_gfs_config_option_cb_ent_t ** cb_handle,
    char *                              option_name,
    void *                              cb,
    void *                              user_arg);

globus_bool_t
globus_gfs_config_get_bool(
    const char *                        option_name);

char *
globus_gfs_config_get_string(
    const char *                        option_name);

globus_list_t *
globus_gfs_config_get_list(
    const char *                        option_name);

void *
globus_gfs_config_get(
    const char *                        option_name);

int
globus_gfs_config_get_int(
    const char *                        option_name);

int
globus_gfs_config_set_int(
    char *                              option_name,
    int                                 int_value);

int
globus_gfs_config_set_bool(
    char *                              option_name,
    int                                 int_value);

int
globus_gfs_config_set_ptr(
    char *                              option_name,
    void *                              ptr);

int
globus_gfs_config_inc_int(
    char *                              option_name,
    int                                 inc_val);

#endif
