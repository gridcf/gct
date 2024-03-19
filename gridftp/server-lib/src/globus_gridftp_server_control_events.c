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

#include "globus_i_gridftp_server_control.h"

static void
globus_l_gsc_send_perf_marker_cb(
    void *                              user_arg);

static void
globus_l_gsc_unreg_perf_marker(
    void *                              user_arg);

static void
globus_l_gsc_send_restart_marker_cb(
    void *                              user_arg);

static void
globus_l_gsc_unreg_restart_marker(
    void *                              user_arg);

static void
globus_l_gsc_send_perf(
    globus_gridftp_server_control_op_t      op,
    int                                     stripe_ndx,
    int                                     stripe_count,
    globus_off_t                            nbytes);

static void
globus_l_gsc_send_restart(
    globus_gridftp_server_control_op_t  op,
    globus_range_list_t                 range_list);

/************************************************************************
 *  event handlers
 *  --------------
 ***********************************************************************/
void
globus_i_gsc_reverse_restart(
    globus_range_list_t                 in_range,
    globus_range_list_t                 out_range)
{
    globus_off_t                        offset;
    globus_off_t                        length;

    globus_range_list_insert(out_range, 0, GLOBUS_RANGE_LIST_MAX);

    if(in_range != NULL)
    {
        while(globus_range_list_size(in_range))
        {
            globus_range_list_remove_at(in_range, 0, &offset, &length);
            
            globus_range_list_remove(out_range, offset, length);
        }
    }
}

void
globus_i_gsc_event_start_perf_restart(
    globus_i_gsc_op_t *                 op)
{
    globus_result_t                     res;
    globus_reltime_t                    delay;
    globus_i_gsc_event_data_t *         event;

    event = &op->event;

    if(op->type != GLOBUS_L_GSC_OP_TYPE_RECV && op->type != GLOBUS_L_GSC_OP_TYPE_SEND)
    {
        return;
    }

    /* performance markers */
    if(((op->type == GLOBUS_L_GSC_OP_TYPE_RECV && 
            op->server_handle->opts.perf_frequency > 0) || 
        (op->type == GLOBUS_L_GSC_OP_TYPE_SEND && 
            op->server_handle->opts.retr_perf_frequency > 0)) &&
        event->event_mask & GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_PERF)
    {
        event->stripe_count = op->server_handle->stripe_count;
        event->stripe_total = (globus_off_t *)globus_calloc(
            sizeof(globus_off_t) * event->stripe_count, 1);

        /* register periodic for events */
        if(op->type == GLOBUS_L_GSC_OP_TYPE_SEND)
        {
            GlobusTimeReltimeSet(
                delay, op->server_handle->opts.retr_perf_frequency, 0);
        }
        else
        {
            GlobusTimeReltimeSet(
                delay, op->server_handle->opts.perf_frequency, 0);
        }
        event->perf_running = GLOBUS_TRUE;
        res = globus_callback_register_periodic(
            &event->periodic_handle,
            &delay,
            &delay,
            globus_l_gsc_send_perf_marker_cb,
            op);
        if(res != GLOBUS_SUCCESS)
        {
            globus_panic(&globus_i_gsc_module, res, "one shot failed.");
        }
    }

    /* restart markers */
    if(op->server_handle->opts.restart_frequency > 0 &&
        event->event_mask & GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_RESTART &&
        op->type == GLOBUS_L_GSC_OP_TYPE_RECV)
    {
        GlobusTimeReltimeSet(
            delay, op->server_handle->opts.restart_frequency, 0);
        event->restart_running = GLOBUS_TRUE;
        res = globus_callback_register_periodic(
            &event->restart_handle,
            &delay,
            &delay,
            globus_l_gsc_send_restart_marker_cb,
            op);
        if(res != GLOBUS_SUCCESS)
        {
            globus_panic(&globus_i_gsc_module, res, "one shot failed.");
        }
    }
}

void
globus_i_gsc_event_start(
    globus_i_gsc_op_t *                 op,
    int                                 event_mask,
    globus_gridftp_server_control_event_cb_t event_cb,
    void *                              user_arg)
{
    globus_i_gsc_event_data_t *         event;

    event = &op->event;

    event->user_cb = event_cb;
    event->user_arg = user_arg;
    event->event_mask = event_mask;

    /* abort called locked */
    if(op->aborted &&
        event->event_mask & GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_ABORT)
    {
        op->aborted = GLOBUS_FALSE;
        event->user_cb(op, GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_ABORT, user_arg);
    }

    op->ref++;  /* until transfer finsihed event happens */
}

static
void
globus_l_gsc_event_done_cb(
    void *                              user_arg)
{
    globus_i_gsc_op_t *                 op;
    globus_i_gsc_event_data_t *         event;
    globus_i_gsc_server_handle_t *      server_handle;

    op = (globus_i_gsc_op_t *) user_arg;
    event = &op->event;
    server_handle = op->server_handle;

    event->user_cb(
        op,
        GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_TRANSFER_COMPLETE,
        event->user_arg);

    if(event->stripe_total != NULL)
    {
        globus_free(event->stripe_total);
    }

    globus_mutex_lock(&server_handle->mutex);
    {
        if(op->data_destroy_obj)
        {
            globus_i_guc_data_object_destroy(
                op->server_handle, op->data_destroy_obj);
        }
        globus_i_gsc_op_destroy(op);
    }
    globus_mutex_unlock(&server_handle->mutex);
}

void
globus_i_gsc_event_end(
    globus_i_gsc_op_t *                 op)
{
    globus_i_gsc_event_data_t *         event;

    event = &op->event;

    if(event->event_mask == 0)
    {
        return;
    }

    event->event_mask = 0;

    if(event->perf_running)
    {
        /* cancel callback send last one */
        event->perf_running = GLOBUS_FALSE;
        globus_callback_unregister(
            op->event.periodic_handle,
            globus_l_gsc_unreg_perf_marker,
            op,
            NULL);
    }
    else if(event->restart_running)
    {
        event->restart_running = GLOBUS_FALSE;
        globus_callback_unregister(
            op->event.restart_handle,
            globus_l_gsc_unreg_restart_marker,
            op,
            NULL);
    }
    else
    {
        globus_callback_register_oneshot(
            NULL,
            NULL,
            globus_l_gsc_event_done_cb,
            op);
    }
}

static void
globus_l_gsc_unreg_perf_marker(
    void *                                  user_arg)
{
    globus_i_gsc_op_t *                     op;
    globus_i_gsc_event_data_t *             event;

    op = (globus_i_gsc_op_t *) user_arg;
    event = &op->event;

    if(event->restart_running)
    {
        event->restart_running = GLOBUS_FALSE;
        globus_callback_unregister(
            op->event.restart_handle,
            globus_l_gsc_unreg_restart_marker,
            op,
            NULL);
    }
    else
    {
        globus_l_gsc_event_done_cb(op);
    }
}

static void
globus_l_gsc_send_perf_marker_cb(
    void *                                  user_arg)
{
    globus_i_gsc_op_t *                     op;
    globus_i_gsc_event_data_t *             event;

    op = (globus_i_gsc_op_t *) user_arg;
    event = &op->event;

    globus_mutex_lock(&op->server_handle->mutex);
    {
        if(event->perf_running)
        {
            event->user_cb(
                op, GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_PERF, event->user_arg);
        }
    }
    globus_mutex_unlock(&op->server_handle->mutex);
}

static void
globus_l_gsc_unreg_restart_marker(
    void *                                  user_arg)
{
    globus_i_gsc_op_t *                     op;

    op = (globus_i_gsc_op_t *) user_arg;

    globus_l_gsc_event_done_cb(op);
}

static void
globus_l_gsc_send_restart_marker_cb(
    void *                              user_arg)
{
    globus_i_gsc_op_t *                 op;
    globus_i_gsc_event_data_t *         event;

    op = (globus_i_gsc_op_t *) user_arg;
    event = &op->event;

    globus_mutex_lock(&op->server_handle->mutex);
    {
        if(event->restart_running)
        {
            event->user_cb(
                op, 
                GLOBUS_GRIDFTP_SERVER_CONTROL_EVENT_RESTART, 
                event->user_arg);
        }
    }
    globus_mutex_unlock(&op->server_handle->mutex);
}

static void
globus_l_gsc_send_perf(
    globus_gridftp_server_control_op_t      op,
    int                                     stripe_ndx,
    int                                     stripe_count,
    globus_off_t                            nbytes)
{
    char *                                  msg;
    struct timeval                          now;
    GlobusGridFTPServerName(globus_l_gsc_send_perf);
                                                                                
    gettimeofday(&now, NULL);
    msg = globus_common_create_string(
        "112-Perf Marker\r\n"
        " Timestamp: %lld.%01d\r\n"
        " Stripe Index: %d\r\n"
        " Stripe Bytes Transferred: %"GLOBUS_OFF_T_FORMAT"\r\n"
        " Total Stripe Count: %d\r\n"
        "112 End.\r\n",
            (long long) now.tv_sec, (int) (now.tv_usec / 100000),
            stripe_ndx,
            nbytes,
            stripe_count);
    globus_i_gsc_intermediate_reply(op, msg);
    globus_free(msg);
}

static void
globus_l_gsc_send_restart(
    globus_gridftp_server_control_op_t  op,
    globus_range_list_t                 range_list)
{
    int                                 ctr;
    char *                              tmp_msg;
    char *                              msg;
    int                                 size;
    globus_off_t                        offset;
    globus_off_t                        length;
    globus_range_list_t                 new_range_list;

    globus_range_list_merge(
        &new_range_list, op->perf_range_list, range_list);
    globus_range_list_destroy(op->perf_range_list);
    op->perf_range_list = new_range_list;

    size = globus_range_list_size(range_list);
    if(size < 1)
    {
        /* sending 0-0 is useless, and it causes a problem with our client
            when markers are sent before the retr begins
        msg = globus_common_create_string("111 Range Marker 0-0\r\n"); */
    }
    else
    {    
        msg = globus_common_create_string("111 Range Marker");
        for(ctr = 0; ctr < size; ctr++)
        {
            globus_range_list_at(range_list, ctr, &offset, &length);
    
            tmp_msg = globus_common_create_string("%s%c%"
                GLOBUS_OFF_T_FORMAT"-%"GLOBUS_OFF_T_FORMAT,
                 msg, ctr ? ',' : ' ', offset, offset + length);
            globus_free(msg);
            msg = tmp_msg;
        }
        tmp_msg = globus_common_create_string("%s%s", msg, "\r\n");
        globus_free(msg);
        msg = tmp_msg;
        
        globus_i_gsc_intermediate_reply(op, msg);
        globus_free(msg);
    }    
}

/********************************************************************
 *  external functions
 *  ------------------
 *
 *******************************************************************/
globus_result_t
globus_gridftp_server_control_event_send_restart(
    globus_gridftp_server_control_op_t      op,
    globus_range_list_t                     restart)
{
    GlobusGridFTPServerName(globus_gridftp_server_control_event_send_restart);

    if(op == NULL)
    {
        return GlobusGridFTPServerErrorParameter("op");
    }
    if(restart == NULL)
    {
        return GlobusGridFTPServerErrorParameter("restart");
    }


    globus_mutex_lock(&op->server_handle->mutex);
    {
        if(op->event.restart_running)
        {
            globus_l_gsc_send_restart(op, restart);
        }
    }
    globus_mutex_unlock(&op->server_handle->mutex);

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_gridftp_server_control_event_send_perf(
    globus_gridftp_server_control_op_t      op,
    int                                     stripe_ndx,
    globus_off_t                            nbytes)
{
    GlobusGridFTPServerName(globus_gridftp_server_control_event_send_perf);

    if(op == NULL)
    {
        return GlobusGridFTPServerErrorParameter("op");
    }
    if(stripe_ndx < 0)
    {
        return GlobusGridFTPServerErrorParameter("stripe_ndx");
    }
    if(nbytes < 0)
    {
        return GlobusGridFTPServerErrorParameter("nbytes");
    }

    globus_mutex_lock(&op->server_handle->mutex);
    {
        if(op->event.stripe_total == NULL)
        {
            globus_mutex_unlock(&op->server_handle->mutex);
            return GlobusGridFTPServerErrorParameter("op");
        }
        op->event.stripe_total[stripe_ndx] += nbytes;
        if(op->event.perf_running)
        {
            globus_l_gsc_send_perf(
                op, 
                stripe_ndx, 
                op->event.stripe_count, 
                op->event.stripe_total[stripe_ndx]);
        }
    }
    globus_mutex_unlock(&op->server_handle->mutex);

    return GLOBUS_SUCCESS;
}
