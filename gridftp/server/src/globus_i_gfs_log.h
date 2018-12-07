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

#ifndef GLOBUS_I_GFS_LOG_H
#define GLOBUS_I_GFS_LOG_H

void
globus_i_gfs_log_open(void);

void
globus_i_gfs_log_close(void);

typedef enum globus_gfs_log_event_type_e
{
    GLOBUS_GFS_LOG_EVENT_START = 1,
    GLOBUS_GFS_LOG_EVENT_END,
    GLOBUS_GFS_LOG_EVENT_MESSAGE,
    GLOBUS_GFS_LOG_EVENT_ERROR
} globus_gfs_log_event_type_t;


void 
globus_i_gfs_log_tr(
    char *                              msg,
    char                                from,
    char                                to);

void
globus_gfs_log_exit_message(
    const char *                        format,
    ...);
    
void
globus_gfs_log_exit_result(
    const char *                        lead,
    globus_result_t                     result);

void
globus_gfs_log_event(
    globus_gfs_log_type_t               type,
    globus_gfs_log_event_type_t         event_type,
    const char *                        event_name,
    globus_result_t                     result,
    const char *                        format,
    ...);

void
globus_i_gfs_log_transfer(
    int                                 stripe_count,
    int                                 stream_count, 
    struct timeval *                    start_gtd_time,
    struct timeval *                    end_gtd_time,
    char *                              dest_ip,
    globus_size_t                       blksize,
    globus_size_t                       tcp_bs,
    const char *                        fname,
    globus_off_t                        nbytes,
    int                                 code,
    char *                              volume,
    char *                              type,
    char *                              username,
    char *                              retrans,
    char *                              taskid);

char *
globus_i_gfs_log_create_transfer_event_msg(
    int                                 stripe_count,
    int                                 stream_count, 
    char *                              dest_ip,
    globus_size_t                       blksize,
    globus_size_t                       tcp_bs,
    const char *                        fname,
    globus_off_t                        nbytes,
    char *                              type,
    char *                              username,
    char *                              retrans,
    char *                              taskid);

#endif
