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

#ifndef GLOBUS_I_GFS_CONTROL_H
#define GLOBUS_I_GFS_CONTROL_H

#include "globus_xio.h"

globus_result_t
globus_i_gfs_control_start(
    globus_xio_handle_t                 handle,
    globus_xio_system_socket_t          system_handle,
    const char *                        remote_contact,
    const char *                        local_contact,
    globus_i_gfs_server_close_cb_t      close_func,
    void *                              user_arg);

char *
globus_i_gsc_get_cmd_string(
    void *                              user_arg);

#endif
