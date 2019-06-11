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

#ifndef GLOBUS_I_GFS_DATA_H
#define GLOBUS_I_GFS_DATA_H

#include "globus_i_gridftp_server.h"

typedef globus_gfs_finished_info_t      globus_gfs_data_reply_t;
typedef globus_gfs_event_info_t         globus_gfs_data_event_reply_t;

typedef void
(*globus_i_gfs_data_callback_t)(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg);

typedef void
(*globus_i_gfs_data_event_callback_t)(
    globus_gfs_data_event_reply_t *     reply,
    void *                              user_arg);

void
globus_i_gfs_data_init();

void
globus_i_gfs_data_request_stat(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_stat_info_t *            stat_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg);

void
globus_i_gfs_data_request_recv(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_transfer_info_t *        recv_info,
    globus_i_gfs_data_callback_t        cb,
    globus_i_gfs_data_event_callback_t  event_cb,
    void *                              user_arg);

void
globus_i_gfs_data_request_send(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_transfer_info_t *        send_info,
    globus_i_gfs_data_callback_t        cb,
    globus_i_gfs_data_event_callback_t  event_cb,
    void *                              user_arg);

void
globus_i_gfs_data_request_list(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_transfer_info_t *        list_info,
    globus_i_gfs_data_callback_t        cb,
    globus_i_gfs_data_event_callback_t  event_cb,
    void *                              user_arg);

void
globus_i_gfs_data_request_command(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_command_info_t *         command_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg);

void
globus_i_gfs_data_request_passive(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_data_info_t *            data_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg);

void
globus_i_gfs_data_request_active(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    int                                 id,
    globus_gfs_data_info_t *            data_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg);

void
globus_i_gfs_data_request_handle_destroy(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    void *                              data_arg);

void
globus_i_gfs_data_request_transfer_event(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    globus_gfs_event_info_t *           event_info);

void
globus_i_gfs_data_request_set_cred(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    gss_cred_id_t                       del_cred);

void
globus_i_gfs_data_request_buffer_send(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg,
    globus_byte_t *                     buffer,
    int                                 buffer_type,
    globus_size_t                       buffer_len);
    
void
globus_i_gfs_data_session_start(
    globus_gfs_ipc_handle_t             ipc_handle,
    const gss_ctx_id_t                  context,
    globus_gfs_session_info_t *         session_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg);

void
globus_i_gfs_data_session_stop(
    globus_gfs_ipc_handle_t             ipc_handle,
    void *                              session_arg);

globus_result_t
globus_i_gfs_data_check_path(
    void *                              session_arg,
    char *                              in_path,
    char **                             ret_path,
    int                                 access_type,
    globus_bool_t                       is_virtual);

globus_result_t
globus_i_gfs_data_virtualize_path(
    void *                              session_arg,
    char *                              in_string,
    char **                             ret_string);
    
char *
globus_i_gfs_kv_getval(
    char *                              kvstring,
    const char *                        key,
    globus_bool_t                       urldecode);

char *
globus_i_gfs_kv_replaceval(
    char *                              kvstring,
    char *                              key,
    char *                              new_val,
    globus_bool_t                       encode);

char *
globus_i_gfs_data_dsi_version();

const char *
globus_i_gfs_data_dsi_checksum_support(
    void *                              session_arg);

#define GFS_L_READ  1<<0
#define GFS_L_WRITE 1<<1
#define GFS_L_LIST  1<<2
#define GFS_L_DIR   1<<3
#define GFS_L_NONE  1<<4

#endif
