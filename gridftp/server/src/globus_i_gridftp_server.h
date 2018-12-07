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

#ifndef GLOBUS_I_GRIDFTP_SERVER_H
#define GLOBUS_I_GRIDFTP_SERVER_H

#include "globus_i_gridftp_server_config.h"
#include "globus_gridftp_server.h"
#include "globus_gridftp_server_control.h"
#include "globus_i_gfs_acl.h"
#include "globus_xio.h"
#include "globus_xio_system.h"
#include "globus_xio_tcp_driver.h"
#include "globus_xio_gsi.h"
#include "globus_ftp_control.h"
#include "globus_gsi_authz.h"

#define _GSSL(s) globus_common_i18n_get_string_by_key(\
		    NULL, \
		    "globus_gridftp_server", \
		    s)

#define _FSSL(s,p) globus_common_i18n_get_string_by_key(\
		     p, \
		    "globus_gridftp_server", \
		    s)

typedef void
(*globus_i_gfs_server_close_cb_t)(
    void *                              user_arg,
    globus_object_t *                   error);

typedef struct globus_i_gfs_monitor_s
{
    globus_bool_t                       done;
    globus_cond_t                       cond;
    globus_mutex_t                      mutex;
} globus_i_gfs_monitor_t;

typedef struct gfs_i_stack_entry_s
{
    globus_xio_driver_t                 driver;
    char *                              driver_name;
    char *                              opts;
} gfs_i_stack_entry_t;

typedef struct
{
    int                                 cmd_id;
    char *                              cmd_name;
    char *                              help_str;
    int                                 max_argc;
    int                                 min_argc;
    globus_bool_t                       has_pathname;
    int                                 access_type;
} globus_i_gfs_cmd_ent_t;        

typedef struct globus_i_gfs_op_info_s
{
    int                                 id;
    globus_hashtable_t                  custom_command_table;
    
    char **                             argv;
    int                                 argc;
    globus_i_gfs_cmd_ent_t *            cmd_ent;
    
    char *                              remote_ip;
} globus_i_gfs_op_info_t;

void
globus_i_gfs_monitor_init(
    globus_i_gfs_monitor_t *            monitor);

void
globus_i_gfs_monitor_destroy(
    globus_i_gfs_monitor_t *            monitor);

void
globus_i_gfs_monitor_wait(
    globus_i_gfs_monitor_t *            monitor);

void
globus_i_gfs_monitor_signal(
    globus_i_gfs_monitor_t *            monitor);

void
globus_i_gfs_ipc_stop();

void
globus_i_gfs_control_stop();

void
globus_i_gfs_control_init();

globus_result_t
globus_i_gfs_brain_init(
    globus_callback_func_t              ready_cb,
    void *                              ready_cb_arg);

void
globus_i_gfs_control_end_421(
    const char *                        msg);

void
globus_l_gfs_data_brain_ready(
    void *                              user_arg);

globus_result_t
globus_i_gfs_get_full_path(
    const char *                            home_dir,
    const char *                            server_cwd,
    void *                                  session_arg,
    const char *                            in_path,
    char **                                 ret_path,
    int                                     access_type);

#define GlobusGFSErrorGenericStr(_res, _fmt)                           \
do                                                                     \
{                                                                      \
        char *                          _tmp_str;                      \
        _tmp_str = globus_common_create_string _fmt;                   \
        _res = globus_error_put(                                       \
            globus_error_construct_error(                              \
                GLOBUS_NULL,                                           \
                GLOBUS_NULL,                                           \
                GLOBUS_GFS_ERROR_GENERIC,                              \
                __FILE__,                                              \
                _gfs_name,                                             \
                __LINE__,                                              \
                "%s",                                                  \
                _tmp_str));                                            \
        globus_free(_tmp_str);                                         \
                                                                       \
} while(0)
 
extern globus_gfs_acl_module_t          globus_gfs_acl_cas_module;
extern globus_gfs_acl_module_t          globus_gfs_acl_test_module;

typedef enum globus_l_gfs_auth_level_e
{
    GLOBUS_L_GFS_AUTH_NONE = 0x00,
    GLOBUS_L_GFS_AUTH_IDENTIFY = 0x01,
    GLOBUS_L_GFS_AUTH_ACTION = 0x02,
    GLOBUS_L_GFS_AUTH_NOSETUID = 0x04,
    GLOBUS_L_GFS_AUTH_NOGRIDMAP = 0x08,
    GLOBUS_L_GFS_AUTH_DATA_NODE_PATH = 0x10,
    GLOBUS_L_GFS_AUTH_ALL = 0xFF
} globus_l_gfs_auth_level_t;

#include "globus_i_gfs_log.h"
#include "globus_i_gfs_control.h"
#include "globus_i_gfs_ipc.h"
#include "globus_i_gfs_data.h"
#include "globus_i_gfs_config.h"

#endif
