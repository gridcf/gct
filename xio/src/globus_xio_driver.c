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

#include "globus_xio.h"
#include "globus_i_xio.h"

static
globus_bool_t
globus_l_xio_server_timeout_always(
    globus_xio_server_t                 server,
    globus_xio_operation_type_t         type)
{
    return GLOBUS_TRUE;
}


static
globus_bool_t
globus_l_xio_timeout_always(
    globus_xio_handle_t                 handle,
    globus_xio_operation_type_t         type,
    void *                              user_arg)
{
    return GLOBUS_TRUE;
}

void
globus_l_xio_op_restarted(
    globus_i_xio_op_t *                 op)
{
    globus_bool_t                       destroy_handle = GLOBUS_FALSE;
    globus_i_xio_context_t *            context;
    globus_i_xio_handle_t *             handle;
    GlobusXIOName(globus_l_xio_op_restarted);

    GlobusXIODebugInternalEnter();

    context = op->_op_context;
    handle = op->_op_handle;
    globus_mutex_lock(&context->mutex);
    {
        GlobusXIOOpDec(op);
        if(op->ref == 0)
        {
            globus_i_xio_op_destroy(op, &destroy_handle);
        }
    }
    globus_mutex_unlock(&context->mutex);

    if(destroy_handle)
    {
        globus_i_xio_handle_destroy(handle);
    }
                                                                                
    GlobusXIODebugInternalExit();
}

globus_result_t
globus_i_xio_repass_write(
    globus_i_xio_op_t *                 op)
{
    globus_i_xio_op_entry_t *           my_op;
    globus_i_xio_context_entry_t *      next_context;
    globus_result_t                     res;
    globus_xio_iovec_t *                tmp_iovec;
    int                                 iovec_count;
    GlobusXIOName(globus_i_xio_repass_write);

    GlobusXIODebugInternalEnter();

    my_op = &op->entry[op->ndx - 1];
    next_context = &op->_op_context->entry[op->ndx - 1];

    /* allocate tmp iovec to the bigest it could ever be */
    if(my_op->_op_ent_fake_iovec == NULL)
    {
        my_op->_op_ent_fake_iovec = (globus_xio_iovec_t *)
            globus_malloc(sizeof(globus_xio_iovec_t) *
                my_op->_op_ent_iovec_count);
    }
    tmp_iovec = my_op->_op_ent_fake_iovec;

    GlobusIXIOUtilTransferAdjustedIovec(
        tmp_iovec, iovec_count,
        my_op->_op_ent_iovec, my_op->_op_ent_iovec_count,
        my_op->_op_ent_nbytes);

    /* repass the operation down */
    res = next_context->driver->write_func(
            next_context->driver_handle,
            tmp_iovec,
            iovec_count,
            op);

    GlobusXIODebugInternalExit();

    return res;
}

globus_result_t
globus_i_xio_repass_read(
    globus_i_xio_op_t *                 op)
{
    globus_i_xio_op_entry_t *           my_op;
    globus_i_xio_context_entry_t *      next_context;
    globus_result_t                     res;
    globus_xio_iovec_t *                tmp_iovec;
    int                                 iovec_count;
    GlobusXIOName(globus_i_xio_repass_read);

    GlobusXIODebugInternalEnter();

    my_op = &op->entry[op->ndx - 1];
    next_context = &op->_op_context->entry[op->ndx - 1];

    /* allocate tmp iovec to the bigest it could ever be */
    if(my_op->_op_ent_fake_iovec == NULL)
    {
        my_op->_op_ent_fake_iovec = (globus_xio_iovec_t *)
            globus_malloc(sizeof(globus_xio_iovec_t) *
                my_op->_op_ent_iovec_count);
    }
    tmp_iovec = my_op->_op_ent_fake_iovec;

    GlobusIXIOUtilTransferAdjustedIovec(
        tmp_iovec, iovec_count,
        my_op->_op_ent_iovec, my_op->_op_ent_iovec_count,
        my_op->_op_ent_nbytes);

    /* repass the operation down */
    res = next_context->driver->read_func(
            next_context->driver_handle,
            tmp_iovec,
            iovec_count,
            op);

    GlobusXIODebugInternalExit();

    return res;
}

void 
globus_i_xio_pass_failed(
    globus_i_xio_op_t *                 op,
    globus_i_xio_context_entry_t *      my_context,
    globus_bool_t *                     close,
    globus_bool_t *                     destroy_handle)
{
    GlobusXIOName(globus_i_xio_pass_failed);

    GlobusXIODebugInternalEnter();

    my_context->outstanding_operations--;
    /*there is an off chance that we could need to close here*/
    if((my_context->state == GLOBUS_XIO_CONTEXT_STATE_CLOSING ||
        my_context->state ==
        GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED_AND_CLOSING) &&
        my_context->outstanding_operations == 0 &&
        !my_context->close_started)
    {
        globus_assert(my_context->close_op != NULL);
        *close = GLOBUS_TRUE;
    }

    op->ndx = op->entry[op->ndx - 1].prev_ndx;

    GlobusXIOOpDec(op);
    if(op->ref == 0)
    {
        globus_i_xio_op_destroy(op, destroy_handle);
    }

    GlobusXIODebugInternalExit();
}

void
globus_i_xio_handle_destroy(
    globus_i_xio_handle_t *             handle)
{
    globus_bool_t                       destroy_context = GLOBUS_FALSE;
    GlobusXIOName(globus_i_xio_handle_destroy);

    GlobusXIODebugInternalEnter();

    globus_mutex_lock(&globus_i_xio_mutex);
    {
        globus_mutex_lock(&handle->context->mutex);
        {
            handle->context->ref--;
            if(handle->context->ref == 0)
            {
                GlobusXIODebugPrintf(
                    GLOBUS_XIO_DEBUG_INFO,
                    (_XIOSL("[globus_i_xio_handle_destroy] :: context->ref == 0.\n")));
                destroy_context = GLOBUS_TRUE;
            }

            if(handle->sd_monitor != NULL)
            {
                GlobusXIODebugPrintf(
                    GLOBUS_XIO_DEBUG_INFO,
                        (_XIOSL("[globus_i_xio_handle_destroy]"
                        " :: signalling handle unload.\n")));

                handle->sd_monitor->count--;
                if(handle->sd_monitor->count == 0)
                {
                    globus_cond_signal(&globus_i_xio_cond);
                }
            }
            else
            {
                globus_list_remove(&globus_i_xio_outstanding_handles_list,
                    globus_list_search(
                        globus_i_xio_outstanding_handles_list, handle));
            }
        }
        globus_mutex_unlock(&handle->context->mutex);

    }
    globus_mutex_unlock(&globus_i_xio_mutex);

    if(destroy_context)
    {
        globus_i_xio_context_destroy(handle->context);
    }
    globus_assert(handle->ref == 0);
    globus_callback_space_destroy(handle->space);
    globus_free(handle);

    GlobusXIODebugInternalExit();
}

/* 
 *  called in the context lock
 */
void
globus_i_xio_handle_dec(
    globus_i_xio_handle_t *             handle,
    globus_bool_t *                     destroy_handle)
{
    globus_result_t                     res;
    globus_i_xio_context_t *            context;
    globus_i_xio_space_info_t *         space_info;
    GlobusXIOName(globus_i_xio_handle_dec);

    GlobusXIODebugInternalEnter();

    context = handle->context;

    *destroy_handle = GLOBUS_FALSE;

    handle->ref--; 
    GlobusXIODebugPrintf(
        GLOBUS_XIO_DEBUG_INFO_VERBOSE,
        (_XIOSL("[globus_i_xio_handle_dec] :: handle ref at %d.\n"), handle->ref));
    if(handle->ref == 0)
    {
        GlobusXIODebugPrintf(
            GLOBUS_XIO_DEBUG_INFO,
            (_XIOSL("[globus_i_xio_handle_dec] :: handle ref at 0.\n")));
        globus_assert(handle->state == GLOBUS_XIO_HANDLE_STATE_CLOSED);
        *destroy_handle = GLOBUS_TRUE;
        /* purge the ch list */
        while(!globus_list_empty(handle->cb_list))
        {
            space_info = (globus_i_xio_space_info_t *)
                globus_list_remove(&handle->cb_list, handle->cb_list);
            res = globus_callback_unregister(
                    space_info->ch,
                    NULL,
                    NULL,
                    NULL);
            if(res != GLOBUS_SUCCESS)
            {
                globus_panic(GLOBUS_XIO_MODULE, res, _XIOSL("failed to unregister"));
            }
        }
    }

    GlobusXIODebugInternalExit();
}

/* 
 * called locked 
 */
void
globus_i_xio_op_destroy(
    globus_i_xio_op_t *                 op,
    globus_bool_t *                     destroy_handle)
{
    globus_i_xio_handle_t *             handle;
    globus_i_xio_context_t *            context;
    int                                 ctr;
    GlobusXIOName(globus_i_xio_op_destroy);

    GlobusXIODebugInternalEnter();

    context = op->_op_context;
    handle = op->_op_handle;

    globus_assert(op->ref == 0);

    for(ctr = 0; ctr < op->stack_size; ctr++)
    {
        if(op->entry[ctr].dd != NULL)
        {
            op->_op_context->entry[ctr].driver->attr_destroy_func(
                op->entry[ctr].dd);
        }
        if(op->_op_context->entry[ctr].driver->attr_destroy_func != NULL &&
            op->entry[ctr].open_attr != NULL)
        {
            op->_op_context->entry[ctr].driver->attr_destroy_func(
                op->entry[ctr].open_attr);
            op->entry[ctr].open_attr = NULL;
        }
        if(op->_op_context->entry[ctr].driver->attr_destroy_func != NULL &&
            op->entry[ctr].close_attr != NULL)
        {
            op->_op_context->entry[ctr].driver->attr_destroy_func(
                op->entry[ctr].close_attr);
            op->entry[ctr].close_attr = NULL;
        }
    }
    
    if(op->user_open_sbj)
    {
        globus_free(op->user_open_sbj);
    }
    if(op->user_open_username)
    {
        globus_free(op->user_open_username);
    }
    if(op->user_open_pw)
    {
        globus_free(op->user_open_pw);
    }

    globus_memory_push_node(&context->op_memory, op);

    if(handle != NULL)
    {
        globus_i_xio_handle_dec(handle, destroy_handle);
    }
    else
    {
        *destroy_handle = GLOBUS_FALSE;
    }
    GlobusXIODebugInternalExit();
}

void
globus_i_xio_driver_resume_op(
    globus_i_xio_op_t *                 op)
{
    GlobusXIOName(globus_i_xio_driver_resume_op);

    GlobusXIODebugInternalEnter();

    switch(op->entry[op->ndx - 1].type)
    {
        case GLOBUS_XIO_OPERATION_TYPE_OPEN:
            globus_l_xio_driver_open_op_kickout(op);
            break;

        case GLOBUS_XIO_OPERATION_TYPE_READ:
            globus_l_xio_driver_op_read_kickout(op);
            break;

        case GLOBUS_XIO_OPERATION_TYPE_WRITE:
            globus_l_xio_driver_op_write_kickout(op);
            break;
        
        case GLOBUS_XIO_OPERATION_TYPE_CLOSE:
            globus_l_xio_driver_op_close_kickout(op);
            break;

        default:
            globus_assert(0 &&
                "Unexpected state in globus_i_xio_driver_resume_op");
            break;
    }
    GlobusXIODebugInternalExit();
}

void
globus_i_xio_driver_deliver_op(
    globus_i_xio_op_t *                 op,
    int                                 ndx,
    globus_xio_operation_type_t         deliver_type)
{
    GlobusXIOName(globus_i_xio_driver_deliver_op);

    GlobusXIODebugInternalEnter();

    switch(deliver_type)
    {
        case GLOBUS_XIO_OPERATION_TYPE_OPEN:
            globus_xio_driver_open_delivered(op, ndx, &deliver_type);
            break;

        case GLOBUS_XIO_OPERATION_TYPE_READ:
            globus_xio_driver_read_delivered(op, ndx, &deliver_type);
            break;

        case GLOBUS_XIO_OPERATION_TYPE_WRITE:
            globus_xio_driver_write_delivered(op, ndx, &deliver_type);
            break;

        default:
            globus_assert(0);
            break;
    }
    GlobusXIODebugInternalExit();
}

void
globus_i_xio_will_block_cb(
    globus_thread_callback_index_t      wb_ndx,
    globus_callback_space_t             space,
    void *                              user_args)
{
    globus_xio_operation_type_t         deliver_type;
    globus_i_xio_op_t *                 op;
    globus_i_xio_context_t *            context;
    int                                 ndx;
    GlobusXIOName(globus_i_xio_will_block_cb);

    GlobusXIODebugInternalEnter();

    op = (globus_i_xio_op_t *) user_args;

    globus_thread_blocking_callback_disable(&wb_ndx);

    context = op->_op_context;
    op->restarted = GLOBUS_TRUE;
    globus_assert(op->ndx == 0);
    ndx = op->ndx;

    do
    {
        globus_mutex_lock(&context->mutex);
        {
            if(op->entry[ndx].deliver_type != NULL)
            {
                GlobusXIOOpInc(op);
                deliver_type = *op->entry[ndx].deliver_type;
                *op->entry[ndx].deliver_type = 
                        GLOBUS_XIO_OPERATION_TYPE_FINISHED;
                op->entry[ndx].deliver_type = NULL;
            }
            else
            {
                deliver_type = GLOBUS_XIO_OPERATION_TYPE_FINISHED;
            }
        }
        globus_mutex_unlock(&context->mutex);

        switch(deliver_type)
        {
            case GLOBUS_XIO_OPERATION_TYPE_OPEN:
                globus_xio_driver_open_delivered(op, ndx, &deliver_type);
                break;

            case GLOBUS_XIO_OPERATION_TYPE_READ:
                globus_xio_driver_read_delivered(op, ndx, &deliver_type);
                break;

            case GLOBUS_XIO_OPERATION_TYPE_WRITE:
                globus_xio_driver_write_delivered(op, ndx, &deliver_type);
                break;

            /* none happens if a driver finishes without passing*/
            case GLOBUS_XIO_OPERATION_TYPE_NONE:
                GlobusXIODebugPrintf(
                    GLOBUS_XIO_DEBUG_INFO_VERBOSE,
                    (_XIOSL("[%s:%d] :: type none, exiting\n"), _xio_name, __LINE__));
                goto exit;

            /* finishe state means the operation was already delivered */
            case GLOBUS_XIO_OPERATION_TYPE_FINISHED:
                break;

            case GLOBUS_XIO_OPERATION_TYPE_CLOSE:
                break;

            default:
                globus_assert(0);
                break;
        }

        ndx = op->entry[ndx].next_ndx;
        GlobusXIODebugPrintf(
            GLOBUS_XIO_DEBUG_INFO_VERBOSE,
           (_XIOSL("[%s:%d] :: Index = %d\n"), _xio_name, __LINE__, ndx));
    }
    while(ndx != op->stack_size && ndx != 0);

  exit:
    GlobusXIODebugInternalExit();
}

void
globus_l_xio_driver_op_write_kickout(
    void *                              user_arg)
{
    globus_xio_operation_type_t         deliver_type;
    globus_xio_operation_type_t         op_type;
    int                                 ndx;
    int                                 wb_ndx;
    globus_i_xio_handle_t *             handle;
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_context_t *            context;
    globus_i_xio_op_entry_t *           my_op;
    globus_i_xio_op_t *                 op;
    GlobusXIOName(globus_l_xio_driver_op_write_kickout);

    GlobusXIODebugInternalEnter();
    op = (globus_i_xio_op_t *) user_arg;

    my_op = &op->entry[op->ndx - 1];
    op->entry[my_op->prev_ndx].next_ndx = op->ndx;
    op->ndx = my_op->prev_ndx;
    ndx = op->ndx;
    my_context = &op->_op_context->entry[ndx];
    handle = op->_op_handle;
    context = op->_op_context;

    GlobusIXIOClearCancel(op);

    /*
     *  before releasing the op back to the user we can safely set this 
     *  outside of a mutex.  Once the users callbcak is called the value
     *  on the local stack may be changed, theus the magic.
     */
    deliver_type = my_op->type;
    op_type = my_op->type;
    my_op->deliver_type = &deliver_type;

    if(ndx == 0)
    {
        /* at top level the callback should never be null */
        globus_assert(my_op->_op_ent_data_cb != NULL);
        globus_thread_blocking_space_callback_push(
            globus_i_xio_will_block_cb,
            (void *) op,
            op->blocking ? GLOBUS_CALLBACK_GLOBAL_SPACE: handle->space,
            &wb_ndx);

        my_op->_op_ent_data_cb(op, GlobusXIOObjToResult(op->cached_obj),
            my_op->_op_ent_nbytes, my_op->user_arg);
    
        globus_thread_blocking_callback_pop(&wb_ndx);
    }
    else
    {
        if(my_op->_op_ent_data_cb == NULL)
        {
            globus_xio_driver_finished_write(op, 
                GlobusXIOObjToResult(op->cached_obj), 
                my_op->_op_ent_nbytes);
        }
        else
        {
            my_op->_op_ent_data_cb(op, 
                GlobusXIOObjToResult(op->cached_obj),
                my_op->_op_ent_nbytes, my_op->user_arg);
        }
    }

    globus_xio_driver_write_delivered(op, ndx, &deliver_type);

    GlobusXIODebugInternalExit();
}   
   
void
globus_l_xio_driver_op_read_kickout(
    void *                              user_arg)
{
    globus_xio_operation_type_t         deliver_type;
    globus_xio_operation_type_t         op_type;
    int                                 ndx;
    int                                 wb_ndx;
    globus_i_xio_handle_t *             handle;
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_context_t *            context;
    globus_i_xio_op_entry_t *           my_op;
    globus_i_xio_op_t *                 op;
    GlobusXIOName(globus_l_xio_driver_op_read_kickout);

    GlobusXIODebugInternalEnter();
    op = (globus_i_xio_op_t *) user_arg;

    my_op = &op->entry[op->ndx - 1];
    op->entry[my_op->prev_ndx].next_ndx = op->ndx;
    op->ndx = my_op->prev_ndx;
    ndx = op->ndx;
    my_context = &op->_op_context->entry[ndx];
    handle = op->_op_handle;
    context = op->_op_context;

    GlobusIXIOClearCancel(op);
    
    /*
     *  before releasing the op back to the user we can safely set this 
     *  outside of a mutex.  Once the users callbcak is called the value
     *  on the local stack may be changed, theus the magic.
     */
    deliver_type = my_op->type;
    op_type = my_op->type;
    my_op->deliver_type = &deliver_type;

    if(ndx == 0)
    {
        /* at top level the callback should never be null */
        globus_assert(my_op->_op_ent_data_cb != NULL);
        globus_thread_blocking_space_callback_push(
            globus_i_xio_will_block_cb,
            (void *) op,
            op->blocking ? GLOBUS_CALLBACK_GLOBAL_SPACE: handle->space,
            &wb_ndx);

        my_op->_op_ent_data_cb(op, GlobusXIOObjToResult(op->cached_obj),
            my_op->_op_ent_nbytes, my_op->user_arg);
    
        globus_thread_blocking_callback_pop(&wb_ndx);
    }
    else
    {
        if(my_op->_op_ent_data_cb == NULL)
        {
            globus_xio_driver_finished_read(op, 
                GlobusXIOObjToResult(op->cached_obj), 
                my_op->_op_ent_nbytes);
        }
        else
        {
            my_op->_op_ent_data_cb(op, 
                GlobusXIOObjToResult(op->cached_obj),
                my_op->_op_ent_nbytes, my_op->user_arg);
        }
    }

    globus_xio_driver_read_delivered(op, ndx, &deliver_type);

    GlobusXIODebugInternalExit();
}   
   
void
globus_l_xio_driver_purge_read_eof(
    globus_i_xio_context_entry_t *      my_context)
{
    globus_i_xio_op_t *                 tmp_op;
    GlobusXIOName(globus_l_xio_driver_purge_read_eof);

    GlobusXIODebugInternalEnter();
    while(!globus_list_empty(my_context->eof_op_list))
    {
        /* we can only get here if a eof has been received */ 
        globus_assert(my_context->state ==
            GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED ||
            my_context->state ==
                GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED ||
            my_context->state ==
                GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING ||
            my_context->state ==
                GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED_AND_CLOSING);

        tmp_op = (globus_i_xio_op_t *)
                    globus_list_remove(&my_context->eof_op_list,
                        my_context->eof_op_list);

        globus_assert(tmp_op->entry[tmp_op->ndx - 1].type ==
            GLOBUS_XIO_OPERATION_TYPE_READ);
        globus_i_xio_register_oneshot(
            tmp_op->_op_handle,
            globus_l_xio_driver_op_read_kickout,
           (void *)tmp_op,
            (tmp_op->blocking || !tmp_op->_op_handle)
                ? GLOBUS_CALLBACK_GLOBAL_SPACE
                : tmp_op->_op_handle->space);
    }
    GlobusXIODebugInternalExit();
}

globus_result_t
globus_i_xio_driver_start_close(
    globus_i_xio_op_t *                 op,
    globus_bool_t                       can_fail)
{
    globus_result_t                     res;
    globus_i_xio_handle_t *             handle;
    globus_i_xio_op_entry_t *           my_op;
    globus_i_xio_context_t *            context;
    globus_i_xio_context_entry_t *      my_context;
    globus_bool_t                       destroy_handle = GLOBUS_FALSE;
    globus_bool_t                       destroy_context = GLOBUS_FALSE;
    GlobusXIOName(globus_i_xio_driver_start_close);

    GlobusXIODebugInternalEnter();
    op->progress = GLOBUS_TRUE;
    op->block_timeout = GLOBUS_FALSE;
    my_op = &op->entry[op->ndx - 1];
    context = op->_op_context;
    handle = op->_op_handle;
    my_context = &context->entry[op->ndx - 1];

    globus_mutex_lock(&context->mutex);
    {
        GlobusXIOOpInc(op);
        /* make sure the context lives past this op */
        context->ref++;
    }
    globus_mutex_unlock(&context->mutex);

    GlobusXIODebugPrintf(
        GLOBUS_XIO_DEBUG_INFO,
       (_XIOSL("[%s:%d] :: Index = %d\n"), _xio_name, __LINE__, op->ndx));
    my_op->in_register = GLOBUS_TRUE;
    res = my_context->driver->close_func(
                    my_context->driver_handle,
                    my_op->close_attr,
                    op);
    my_op->in_register = GLOBUS_FALSE;

    if(res != GLOBUS_SUCCESS && !can_fail)
    {
        my_op->in_register = GLOBUS_TRUE;
        globus_xio_driver_finished_close(op, res);
        my_op->in_register = GLOBUS_FALSE;
    }
    
    if((res == GLOBUS_SUCCESS || !can_fail) && my_op->prev_ndx == 0)
    {
        while(op->finished_delayed)
        {
            /* reuse this blocked thread to finish the operation */
            op->finished_delayed = GLOBUS_FALSE;
            globus_i_xio_driver_resume_op(op);
        }
    }

    globus_mutex_lock(&context->mutex);
    {
        GlobusXIOOpDec(op);
        if(op->ref == 0)
        {
            globus_i_xio_op_destroy(op, &destroy_handle);
        }
        
        context->ref--;
        if(context->ref == 0)
        {
            destroy_context = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&context->mutex);

    if(destroy_handle)
    {
        globus_i_xio_handle_destroy(handle);
    }
    if(destroy_context)
    {
        /* the only way we'll be destroying the context is if this was a 
         * driver op and the handle no longer exists
         */
        globus_assert(!destroy_handle);
        globus_i_xio_context_destroy(context);
    }

    GlobusXIODebugInternalExit();
    return res;
}

/*
 *  driver callback kickout
 *
 *  when in a register the finish function kicks this out as a oneshot
 */
void
globus_l_xio_driver_op_close_kickout(
    void *                              user_arg)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_op_entry_t *           my_op;
    GlobusXIOName(globus_l_xio_driver_op_close_kickout);

    GlobusXIODebugInternalEnter();
    op = (globus_i_xio_op_t *) user_arg;

    my_op = &op->entry[op->ndx - 1];
    op->ndx = my_op->prev_ndx;

    GlobusIXIOClearCancel(op);

    if(my_op->cb != NULL)
    {
        my_op->cb(
            op,
            GlobusXIOObjToResult(op->cached_obj),
            my_op->user_arg);
    }
    else
    {
        globus_xio_driver_finished_close(
            op, GlobusXIOObjToResult(op->cached_obj));
    }
    GlobusXIODebugInternalExit();
}

/*
 *  driver callback mickout
 *
 *  when in a register the finish function kicks this out as a oneshot
 */
void
globus_l_xio_driver_op_accept_kickout(
    void *                              user_arg)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_op_entry_t *           my_op;
    GlobusXIOName(globus_l_xio_driver_op_accept_kickout);
                                                                                
    GlobusXIODebugInternalEnter();
    op = (globus_i_xio_op_t *) user_arg;
                                                                                
    my_op = &op->entry[op->ndx - 1];
    op->ndx = my_op->prev_ndx;
                      
    /* driver's can't cancel accept ops yet, dont need this
     * this call only works for regular ops, anyway
     *                                                                            
    GlobusIXIOClearCancel(op);
     */
     
    if(my_op->cb != NULL)
    {
        my_op->cb(
            op,
            GlobusXIOObjToResult(op->cached_obj),
            my_op->user_arg);
    }
    else
    {
        globus_xio_driver_finished_accept(
            op, NULL, GlobusXIOObjToResult(op->cached_obj));
    }
    GlobusXIODebugInternalExit();
}


void
globus_l_xio_driver_open_op_kickout(
    void *                              user_arg)
{
    globus_i_xio_handle_t *             handle;
    globus_i_xio_context_t *            context;
    globus_i_xio_context_entry_t *      my_context;
    int                                 ndx = 0;
    int                                 wb_ndx;
    globus_i_xio_op_entry_t *           my_op;
    globus_i_xio_op_t *                 op;
    globus_xio_operation_type_t         deliver_type;
    GlobusXIOName(globus_l_xio_driver_open_op_kickout);
    
    GlobusXIODebugInternalEnter();

    op = (globus_i_xio_op_t *) user_arg;

    my_op = &op->entry[op->ndx - 1];
    op->ndx = my_op->prev_ndx;
    ndx = op->ndx;
    my_context = &op->_op_context->entry[ndx];
    handle = op->_op_handle;
    context = op->_op_context;

    deliver_type = my_op->type;
    my_op->deliver_type =&deliver_type;

    GlobusIXIOClearCancel(op);

    if(ndx == 0)
    {
        /* at top level the callback should never be null */
        globus_assert(my_op->cb != NULL);
        globus_thread_blocking_space_callback_push(
            globus_i_xio_will_block_cb,
            (void *) op,
            op->blocking ? GLOBUS_CALLBACK_GLOBAL_SPACE: handle->space,
            &wb_ndx);
        my_op->cb(op, GlobusXIOObjToResult(op->cached_obj), my_op->user_arg);
        globus_thread_blocking_callback_pop(&wb_ndx);
    }
    else
    {
        if(my_op->cb == NULL)
        {
            globus_xio_driver_finished_open(NULL, op, 
                GlobusXIOObjToResult(op->cached_obj));
        }
        else
        {
            my_op->cb(op, 
                GlobusXIOObjToResult(op->cached_obj), my_op->user_arg);
        }
    }

    globus_xio_driver_open_delivered(op, ndx, &deliver_type);

    GlobusXIODebugInternalExit();
}

/*-************************************************************************
 *                  context driver api funcitons
 *                  ----------------------------
 *************************************************************************/

void
globus_i_xio_context_destroy(
    globus_i_xio_context_t *            xio_context)
{
    int                                 ctr;
    GlobusXIOName(globus_i_xio_context_destroy);

    GlobusXIODebugInternalEnter();
    globus_assert(xio_context->ref == 0);

    GlobusXIODebugPrintf(
        GLOBUS_XIO_DEBUG_INFO_VERBOSE, 
        (_XIOSL("  context @ 0x%x: ref=%d size=%d\n"), 
            xio_context, xio_context->ref, xio_context->stack_size));
    
    for(ctr = 0; ctr < xio_context->stack_size; ctr++)
    {
        globus_fifo_destroy(&xio_context->entry[ctr].pending_read_queue);
    }
        
    globus_mutex_destroy(&xio_context->mutex);
    globus_mutex_destroy(&xio_context->cancel_mutex);
    globus_memory_destroy(&xio_context->op_memory);
    globus_free(xio_context);

    GlobusXIODebugInternalExit();
}

globus_i_xio_context_t *
globus_i_xio_context_create(
    int                                 stack_size)
{
    globus_i_xio_context_t *            xio_context;
    int                                 size;
    int                                 ctr;
    GlobusXIOName(globus_i_xio_context_create);

    GlobusXIODebugInternalEnter();

    size = sizeof(globus_i_xio_context_t) +
        (sizeof(globus_i_xio_context_entry_t) * (stack_size - 1));

    xio_context = (globus_i_xio_context_t *) globus_malloc(size);
    if(xio_context != NULL)
    {
        memset(xio_context, '\0', size);

        globus_mutex_init(&xio_context->mutex, NULL);
        globus_mutex_init(&xio_context->cancel_mutex, NULL);
        xio_context->stack_size = stack_size;
        globus_memory_init(&xio_context->op_memory,
            sizeof(globus_i_xio_op_t) +
                (sizeof(globus_i_xio_op_entry_t) *
                    (stack_size - 1)),
            GLOBUS_XIO_HANDLE_DEFAULT_OPERATION_COUNT);
        xio_context->ref++;
        for(ctr = 0; ctr < xio_context->stack_size; ctr++)
        {
            xio_context->entry[ctr].whos_my_daddy = xio_context;
            globus_fifo_init(&xio_context->entry[ctr].pending_read_queue);
        }
    }

    GlobusXIODebugInternalExit();

    return xio_context;
}

/*-************************************************************************
 *                  macro wrapper functions
 *                  -----------------------
 *
 *  this is mainly a compile test, but who knows, someone may want it
 *************************************************************************/
void *
globus_i_xio_attr_get_ds(
    globus_i_xio_attr_t *               attr,
    globus_xio_driver_t                 driver)
{
    void *                              rc;

    GlobusIXIOAttrGetDS(rc, attr, driver);

    return rc;
}

/*
 *  read ahead stuff
 */
void 
globus_xio_driver_operation_destroy(
    globus_xio_operation_t              operation)
{
    globus_i_xio_context_t *            context;
    globus_bool_t                       destroy_context = GLOBUS_FALSE;
    globus_i_xio_op_t *                 op;
    GlobusXIOName(globus_xio_driver_operation_destroy);

    GlobusXIODebugInternalEnter();

    op = operation;
    context = op->_op_context;

    globus_mutex_lock(&context->mutex);
    {
        GlobusXIOOpDec(op);
        if(op->ref == 0)
        {
            context->ref--;
            if(context->ref == 0)
            {
                GlobusXIODebugPrintf(
                    GLOBUS_XIO_DEBUG_INFO,
      (_XIOSL("[globus_xio_driver_operation_destroy] :: context->ref == 0.\n")));
                destroy_context = GLOBUS_TRUE;
            }
            globus_memory_push_node(&context->op_memory, op);
        }
    }
    globus_mutex_unlock(&context->mutex);

    if(destroy_context)
    {
        globus_i_xio_context_destroy(context);
    }
    GlobusXIODebugInternalExit();
}

globus_result_t
globus_xio_driver_operation_create(
    globus_xio_operation_t *            operation,
    globus_xio_driver_handle_t          driver_handle)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_op_entry_t *           my_op;
    globus_result_t                     res;
    globus_i_xio_context_t *            context;
    int                                 index;
    GlobusXIOName(globus_xio_driver_operation_create);

    GlobusXIODebugEnter();

    context = driver_handle->whos_my_daddy;
    for(index = 0;
        index < context->stack_size &&
            &context->entry[index] != driver_handle;
        index++)
    {
    }
    
    if(index == context->stack_size)
    {
        res = GlobusXIOErrorParameter("driver_handle");
        goto err;
    }
    
    /* driver_handles are to the drivers below the current one */
    index--;
    
    GlobusXIOOperationCreate(op, context);
    if(op == NULL)
    {
        res = GlobusXIOErrorMemory("op");
        goto err;
    }
    op->ndx = index + 1;

    op->type = GLOBUS_XIO_OPERATION_TYPE_DRIVER;
    op->state = GLOBUS_XIO_OP_STATE_OPERATING;
    op->ref = 1;
    op->_op_handle = NULL;
    op->_op_context = context;
    op->_op_handle_timeout_cb = NULL;

    my_op = &op->entry[index];
    my_op->_op_ent_nbytes = 0;
    my_op->_op_ent_wait_for = 0;
    my_op->prev_ndx = -1;
    my_op->type = GLOBUS_XIO_OPERATION_TYPE_DRIVER;
    
    globus_mutex_lock(&context->mutex);
    context->ref++;
    globus_mutex_unlock(&context->mutex);

    *operation = op;

    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusXIODebugExitWithError();
    return res;
}

globus_result_t
globus_i_xio_driver_attr_cntl(
    globus_i_xio_attr_t *               attr,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    va_list                             ap)
{
    char *                              tmp_str;
    globus_result_t                     res = GLOBUS_SUCCESS;
    void *                              ds;
    globus_xio_attr_cmd_t               general_cmd;
    globus_xio_timeout_server_callback_t server_timeout_cb;
    globus_xio_timeout_callback_t       timeout_cb;
    globus_reltime_t *                  delay_time;
    globus_callback_space_t             space;
    GlobusXIOName(globus_i_xio_driver_attr_cntl);

    GlobusXIODebugEnter();

    if(driver != NULL)
    {
        GlobusIXIOAttrGetDS(ds, attr, driver);
        if(ds == NULL)
        {
            res = driver->attr_init_func(&ds);
            if(res != GLOBUS_SUCCESS)
            {
                goto err;
            }
            if(attr->ndx >= attr->max)
            {
                attr->max *= 2;
                attr->entry = (globus_i_xio_attr_ent_t *)
                    globus_realloc(attr->entry, attr->max *
                            sizeof(globus_i_xio_attr_ent_t));
            }
            attr->entry[attr->ndx].driver = driver;
            attr->entry[attr->ndx].driver_data = ds;
            attr->ndx++;
        }

        /* if the driver is capable of parsing the strings 
            and this is the string parser command, if there is
            no table defined the SET_STRING command will be passed
            to the driver.  this is fine.  they may want to parse it
            in their own way */
        if(driver->string_table != NULL && 
            cmd == GLOBUS_XIO_SET_STRING_OPTIONS)
        {
            char *                      opt_str;

            opt_str = va_arg(ap, char *);
            res = globus_i_xio_string_cntl_parser(
                opt_str,
                driver->string_table,
                ds,
                driver->attr_cntl_func);
        }
        else if (cmd == GLOBUS_XIO_GET_DRIVER_NAME)
        {
            const char **               conststropt;
            conststropt = va_arg(ap, const char **);
            *conststropt = driver->name;
        }
        else
        {
            res = driver->attr_cntl_func(ds, cmd, ap);
        }
        
        if(res != GLOBUS_SUCCESS)
        {
            goto err;
        }
    }
    else
    {
        general_cmd = cmd;

        switch(general_cmd)
        {
            case GLOBUS_XIO_ATTR_SET_TIMEOUT_ALL:
                timeout_cb = va_arg(ap, globus_xio_timeout_callback_t);
                delay_time = va_arg(ap, globus_reltime_t *);
                attr->timeout_arg = va_arg(ap, void *);
                if(timeout_cb == NULL)
                {
                    timeout_cb = globus_l_xio_timeout_always;
                }

                attr->open_timeout_cb = timeout_cb;
                attr->close_timeout_cb = timeout_cb;
                attr->read_timeout_cb = timeout_cb;
                attr->write_timeout_cb = timeout_cb;

                GlobusTimeReltimeCopy(attr->open_timeout_period, *delay_time);
                GlobusTimeReltimeCopy(attr->close_timeout_period, *delay_time);
                GlobusTimeReltimeCopy(attr->read_timeout_period, *delay_time);
                GlobusTimeReltimeCopy(attr->write_timeout_period, *delay_time);
                break;

            case GLOBUS_XIO_ATTR_SET_TIMEOUT_OPEN:
                timeout_cb = va_arg(ap, globus_xio_timeout_callback_t);
                delay_time = va_arg(ap, globus_reltime_t *);
                attr->timeout_arg = va_arg(ap, void *);
                if(timeout_cb == NULL)
                {
                    timeout_cb = globus_l_xio_timeout_always;
                }

                attr->open_timeout_cb = timeout_cb;
                GlobusTimeReltimeCopy(attr->open_timeout_period, *delay_time);
                break;

            case GLOBUS_XIO_ATTR_SET_TIMEOUT_CLOSE:
                timeout_cb = va_arg(ap, globus_xio_timeout_callback_t);
                delay_time = va_arg(ap, globus_reltime_t *);
                attr->timeout_arg = va_arg(ap, void *);
                if(timeout_cb == NULL)
                {
                    timeout_cb = globus_l_xio_timeout_always;
                }

                attr->close_timeout_cb = timeout_cb;
                GlobusTimeReltimeCopy(attr->close_timeout_period, *delay_time);
                break;

            case GLOBUS_XIO_ATTR_SET_TIMEOUT_READ:
                timeout_cb = va_arg(ap, globus_xio_timeout_callback_t);
                delay_time = va_arg(ap, globus_reltime_t *);
                attr->timeout_arg = va_arg(ap, void *);
                if(timeout_cb == NULL)
                {
                    timeout_cb = globus_l_xio_timeout_always;
                }

                attr->read_timeout_cb = timeout_cb;
                GlobusTimeReltimeCopy(attr->read_timeout_period, *delay_time);
                break;

            case GLOBUS_XIO_ATTR_SET_TIMEOUT_WRITE:
                timeout_cb = va_arg(ap, globus_xio_timeout_callback_t);
                delay_time = va_arg(ap, globus_reltime_t *);
                attr->timeout_arg = va_arg(ap, void *);
                if(timeout_cb == NULL)
                {
                    timeout_cb = globus_l_xio_timeout_always;
                }

                attr->write_timeout_cb = timeout_cb;
                GlobusTimeReltimeCopy(attr->write_timeout_period, *delay_time);
                break;

            case GLOBUS_XIO_ATTR_SET_TIMEOUT_ACCEPT:
                server_timeout_cb =
                    va_arg(ap, globus_xio_timeout_server_callback_t);
                delay_time = va_arg(ap, globus_reltime_t *);
                attr->timeout_arg = va_arg(ap, void *);
                if(server_timeout_cb == NULL)
                {
                    server_timeout_cb = globus_l_xio_server_timeout_always;
                }

                attr->accept_timeout_cb = server_timeout_cb;
                GlobusTimeReltimeCopy(attr->accept_timeout_period, *delay_time);
                break;
            case GLOBUS_XIO_ATTR_SET_SPACE:
                space = va_arg(ap, globus_callback_space_t);
                res = globus_callback_space_reference(space);
                if(res != GLOBUS_SUCCESS)
                {
                    goto err;
                }
                globus_callback_space_destroy(attr->space);
                attr->space = space;
                break;

            case GLOBUS_XIO_ATTR_CLOSE_NO_CANCEL:
                attr->no_cancel = va_arg(ap, globus_bool_t);
                break;

            case GLOBUS_XIO_ATTR_SET_CREDENTIAL:
                attr->user_open_cred = va_arg(ap, gss_cred_id_t);
                tmp_str = va_arg(ap, char *);
                attr->user_open_sbj = globus_libc_strdup(tmp_str);
                tmp_str = va_arg(ap, char *);
                attr->user_open_username = globus_libc_strdup(tmp_str);
                tmp_str = va_arg(ap, char *);
                attr->user_open_pw = globus_libc_strdup(tmp_str);
                break;
    
            default:
                res = GlobusXIOErrorInvalidCommand(general_cmd);
                goto err;
        }
    }

    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;

  err:
    GlobusXIODebugExitWithError();
    return res;
}

globus_result_t
globus_i_xio_driver_dd_cntl(
    globus_i_xio_op_t *                 op,
    globus_xio_driver_t                 driver,
    globus_xio_operation_type_t         type,
    int                                 cmd,
    va_list                             ap)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    int                                 ndx;
    int                                 ctr;
    void *                              in_attr = NULL;
    globus_xio_driver_attr_cntl_t       attr_cntl_func;

    GlobusXIOName(globus_i_xio_driver_dd_cntl);

    GlobusXIODebugEnter();

    if(driver != NULL)
    {
        ndx = -1;
        for(ctr = 0; ctr < op->stack_size && ndx == -1; ctr++)
        {
            if (op->type == GLOBUS_XIO_OPERATION_TYPE_SERVER_INIT)
            {
                if (driver == op->_op_server->entry[ctr].driver)
                {
                    if(op->entry[ctr].open_attr == NULL)
                    {
                        res = 
                        op->_op_server->entry[ctr].driver->attr_init_func(
                            &op->entry[ctr].open_attr);
                    }
                    in_attr = op->entry[ctr].open_attr;
                    ndx = ctr;
                    break;
                }
            }
            else if(driver == op->_op_context->entry[ctr].driver)
            {
                switch(type)
                {
                    case GLOBUS_XIO_OPERATION_TYPE_OPEN:
                        if(op->entry[ctr].open_attr == NULL)
                        {
                            res = 
                            op->_op_context->entry[ctr].driver->attr_init_func(
                                &op->entry[ctr].open_attr);
                        }
                        in_attr = op->entry[ctr].open_attr;
                        break;

                    case GLOBUS_XIO_OPERATION_TYPE_CLOSE:
                        if(op->entry[ctr].close_attr == NULL)
                        {
                            res = 
                            op->_op_context->entry[ctr].driver->attr_init_func(
                                &op->entry[ctr].close_attr);
                        }
                        in_attr = op->entry[ctr].close_attr;
                        break;

                    default:
                        if(op->entry[ctr].dd == NULL)
                        {
                            res = 
                            op->_op_context->entry[ctr].driver->attr_init_func(
                                &op->entry[ctr].dd);
                        }
                        in_attr = op->entry[ctr].dd;
                        break;
                }

                if(res != GLOBUS_SUCCESS)
                {
                    goto err;
                }
                ndx = ctr;
                break;
            }
        }
        if(ndx == -1)
        {
            res = GlobusXIOErrorInvalidDriver("not found");
            goto err;
        }

        if (op->type == GLOBUS_XIO_OPERATION_TYPE_SERVER_INIT)
        {
            attr_cntl_func = op->_op_server->entry[ndx].driver->attr_cntl_func;
        }
        else
        {
            attr_cntl_func = op->_op_context->entry[ndx].driver->attr_cntl_func;
        }

        if (attr_cntl_func)
        {
            /* if the driver is capable of parsing the strings 
                and this is the string parser command, if there is
                no table defined the SET_STRING command will be passed
                to the driver.  this is fine.  they may want to parse it
                in their own way */
            if(driver->string_table != NULL && 
                cmd == GLOBUS_XIO_SET_STRING_OPTIONS)
            {
                char *                      opt_str;
    
                opt_str = va_arg(ap, char *);
                res = globus_i_xio_string_cntl_parser(
                    opt_str,
                    driver->string_table,
                    in_attr,
                    attr_cntl_func);
            }
            else if (cmd == GLOBUS_XIO_GET_DRIVER_NAME)
            {
                const char **               conststropt;
                conststropt = va_arg(ap, const char **);
                *conststropt = driver->name;
                res = GLOBUS_SUCCESS;
            }
            else
            {
                res = attr_cntl_func(
                        in_attr,
                        cmd,
                        ap);
                if(res != GLOBUS_SUCCESS)
                {
                    goto err;
                }
            }
        }
        else
        {
            res = GlobusXIOErrorInvalidDriver(_XIOSL("driver doesn't support dd cntl"));
            goto err;
        }
    }
    else
    {
        globus_off_t *                  out_offt;
        
        /* could end up here with non-dd attr cntls... none supported at driver
         * level yet, so no biggie
         */
        switch(cmd)
        {
          case GLOBUS_XIO_DD_SET_OFFSET:
            op->_op_ent_offset = va_arg(ap, globus_off_t);
            break;
            
          case GLOBUS_XIO_DD_GET_OFFSET:
            out_offt = va_arg(ap, globus_off_t *);
            *out_offt = op->_op_ent_offset;
            break;
            
          default:
            res = GlobusXIOErrorInvalidCommand(cmd);
            goto err;
        }
    }
    GlobusXIODebugExit();

    return GLOBUS_SUCCESS;
  err:

    GlobusXIODebugExitWithError();
    return res;
}

globus_result_t
globus_xio_driver_attr_cntl(
    globus_xio_operation_t              op,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    ...)
{
    int                                 prev_ndx;
    globus_result_t                     res;
    va_list                             ap;
    GlobusXIOName(globus_xio_driver_data_descriptor_cntl);

    GlobusXIODebugEnter();

    if(op == NULL)
    {
        res = GlobusXIOErrorParameter("op");
        goto err;
    }

    va_start(ap, cmd);

    prev_ndx = op->entry[op->ndx - 1].prev_ndx;

    res = globus_i_xio_driver_dd_cntl(
        op, driver, op->entry[prev_ndx].type, cmd, ap);

    va_end(ap);

    if(res != GLOBUS_SUCCESS)
    {
        goto err;
    }

    GlobusXIODebugExit();

    return GLOBUS_SUCCESS;

  err:

    GlobusXIODebugExitWithError();
    return res;
}

globus_result_t
globus_xio_driver_data_descriptor_cntl(
    globus_xio_operation_t              op,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    ...)
{
    globus_result_t                     res;
    va_list                             ap;
    GlobusXIOName(globus_xio_driver_data_descriptor_cntl);

    GlobusXIODebugEnter();

    if(op == NULL)
    {
        res = GlobusXIOErrorParameter("op");
        goto err;
    }

    va_start(ap, cmd);

    res = globus_i_xio_driver_dd_cntl(
        op, driver, GLOBUS_XIO_OPERATION_TYPE_DD, cmd, ap);

    va_end(ap);

    if(res != GLOBUS_SUCCESS)
    {
        goto err;
    }

    GlobusXIODebugExit();

    return GLOBUS_SUCCESS;

  err:

    GlobusXIODebugExitWithError();
    return res;
}

globus_result_t
globus_i_xio_driver_handle_cntl(
    globus_i_xio_context_t *            context,
    int                                 start_ndx,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    va_list                             ap)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    int                                 ctr;
    globus_bool_t                       called;
    GlobusXIOName(globus_i_xio_driver_handle_cntl);

    GlobusXIODebugEnter();

    if(context == NULL)
    {
        res = GlobusXIOErrorParameter("context");
        goto err;
    }

    if(driver != NULL)
    {
        for(ctr = start_ndx; ctr < context->stack_size; ctr++)
        {
            called = GLOBUS_FALSE;
            
            if(driver == context->entry[ctr].driver ||
                driver == GLOBUS_XIO_QUERY)
            {
                if(context->entry[ctr].state == GLOBUS_XIO_CONTEXT_STATE_NONE
                    && context->entry[ctr].driver->link_cntl_func)
                {
                    /* This driver hasn't been opened yet, slot contains link
                     * object
                     */
                    res = context->entry[ctr].driver->link_cntl_func(
                            context->entry[ctr].driver_handle,
                            cmd,
                            ap);
                    called = GLOBUS_TRUE;
                }
                else if(context->entry[ctr].state != 
                    GLOBUS_XIO_CONTEXT_STATE_NONE &&
                    context->entry[ctr].driver->handle_cntl_func)
                {
                    res = context->entry[ctr].driver->handle_cntl_func(
                            context->entry[ctr].driver_handle,
                            cmd,
                            ap);
                    called = GLOBUS_TRUE;
                }
                
                if(called && res == GLOBUS_SUCCESS)
                {
                    break;
                }
                
                if(driver == GLOBUS_XIO_QUERY)
                {
                    if(called && res != GLOBUS_SUCCESS &&
                        globus_xio_error_match(res, GLOBUS_XIO_ERROR_COMMAND))
                    {
                        /* try again */
                        res = GLOBUS_SUCCESS;
                    }
                }
                else if(!called)
                {
                    res = GlobusXIOErrorInvalidDriver(
                        _XIOSL("handle_cntl not supported"));
                }
                
                if(res != GLOBUS_SUCCESS)
                {
                    goto err;
                }
            }
        }
        if(ctr == context->stack_size)
        {
            /* none found, throw error */
            if(driver != GLOBUS_XIO_QUERY)
            {
                res = GlobusXIOErrorInvalidDriver(_XIOSL("not found"));
            }
            else
            {
                res = GlobusXIOErrorInvalidCommand(cmd);
            }
            goto err;
        }
    }
    else
    {
        /* support XIO specific cntls at driver level?? */
    }

    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusXIODebugExitWithError();
    return res;
}

globus_result_t
globus_xio_driver_merge_handle(
    globus_xio_operation_t              op,
    globus_xio_driver_handle_t          src_driver_handle)
{
    int                                 ctr;
    globus_result_t                     res;
    globus_i_xio_context_t *            dst_context;
    globus_i_xio_context_t *            src_context;
    GlobusXIOName(globus_xio_driver_merge_handle);

    GlobusXIODebugEnter();
    if(op == NULL)
    {
        res = GlobusXIOErrorParameter("op");
        goto err;
    }
    if(src_driver_handle == NULL)
    {
        res = GlobusXIOErrorParameter("src_driver_handle");
        goto err;
    }


    dst_context = op->_op_context;
    src_context = src_driver_handle->whos_my_daddy;
    /*
     *  if they are the same just indicate success
     */
    if(dst_context == src_context)
    {
        return GLOBUS_SUCCESS;
    }

    if(dst_context->stack_size != src_context->stack_size)
    {
        res = GlobusXIOErrorParameter("src_driver_handle");
        goto err;
    }

    for(ctr = op->ndx; ctr < dst_context->stack_size; ctr++)
    {
        /* verify that the drivers are compatible */
        if(dst_context->entry[ctr].driver != src_context->entry[ctr].driver)
        {
            res = GlobusXIOErrorParameter("src_driver_handle");
            goto err;
        }
        dst_context->entry[ctr].whos_my_daddy = dst_context;
        dst_context->entry[ctr].driver_handle = 
            src_context->entry[ctr].driver_handle;

        GlobusXIOContextStateChange(&dst_context->entry[ctr],
            GLOBUS_XIO_CONTEXT_STATE_OPEN);
    }

/* XXX need to remove references on src context so it can be destroyed */
    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;
  err:

    GlobusXIODebugExitWithError();
    return res;
}

globus_result_t
globus_xio_driver_handle_cntl(
    globus_xio_driver_handle_t          driver_handle,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    ...)
{
    globus_result_t                     res;
    va_list                             ap;
    globus_i_xio_context_t *            context;
    int                                 start_ndx = 0;
    GlobusXIOName(globus_xio_driver_handle_cntl);

    GlobusXIODebugEnter();

    if(driver_handle == NULL)
    {
        res = GlobusXIOErrorParameter("driver_handle");
        goto err;
    }
    context = driver_handle->whos_my_daddy;
    if(context == NULL)
    {
        res = GlobusXIOErrorParameter("op");
        goto err;
    }
    va_start(ap, cmd);

    if(driver == GLOBUS_XIO_QUERY)
    {
        for(; start_ndx < context->stack_size &&
            driver_handle != &context->entry[start_ndx]; start_ndx++)
        {
        }
    }
    
    res = globus_i_xio_driver_handle_cntl(context, start_ndx, driver, cmd, ap);
    va_end(ap);
    if(res != GLOBUS_SUCCESS)
    {
        goto err;
    }

    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusXIODebugExitWithError();
    return res;
}

globus_result_t
globus_xio_driver_operation_cancel(
    globus_xio_driver_handle_t          driver_handle,
    globus_xio_operation_t              operation)
{
    globus_result_t                     res;
    globus_i_xio_context_t *            context;
    globus_i_xio_op_t *                 op;
    int                                 source_ndx;
    GlobusXIOName(globus_xio_driver_operation_cancel);

    GlobusXIODebugEnter();

    op = (globus_i_xio_op_t *) operation;
    if(op == NULL)
    {
        res = GlobusXIOErrorParameter("op");
        goto err;
    }

    context = op->_op_context;
    for(source_ndx = 0;
        source_ndx < context->stack_size &&
            &context->entry[source_ndx] != driver_handle;
        source_ndx++)
    {
    }
    
    if(source_ndx == context->stack_size)
    {
        res = GlobusXIOErrorParameter("driver_handle");
        goto err;
    }
    
    /* driver_handles are to the drivers below the current one */
    source_ndx--;
    
    globus_mutex_lock(&context->cancel_mutex);
    {
        res = globus_i_xio_operation_cancel(op, source_ndx);
    }
    globus_mutex_unlock(&context->cancel_mutex);
    
    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusXIODebugExitWithError();
    return res;
}


void
globus_xio_driver_set_eof_received(
    globus_xio_operation_t              op)
{
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_context_t *            context;
    GlobusXIOName(globus_xio_driver_set_eof_received);

    GlobusXIODebugEnter();
    
    context = op->_op_context;
    my_context = &context->entry[op->entry[op->ndx - 1].prev_ndx];
    
    globus_mutex_lock(&context->mutex);
    {
        globus_assert(
            my_context->read_operations > 0 &&
            _XIOSL("Must be called on behalf of read operations"));
        globus_assert(
            my_context->state == GLOBUS_XIO_CONTEXT_STATE_OPEN ||
            my_context->state == GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED ||
            my_context->state == 
                GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING);
                
        if(my_context->state == GLOBUS_XIO_CONTEXT_STATE_OPEN)
        {
            GlobusXIOContextStateChange(my_context,
                GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED);
        }
    }
    globus_mutex_unlock(&context->mutex);
    
    GlobusXIODebugExit();
}

globus_bool_t
globus_xio_driver_eof_received(
    globus_xio_operation_t              op)
{
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_context_t *            context;
    globus_bool_t                       received = GLOBUS_FALSE;
    GlobusXIOName(globus_xio_driver_eof_received);

    GlobusXIODebugEnter();
    
    context = op->_op_context;
    my_context = &context->entry[op->entry[op->ndx - 1].prev_ndx];
    
    globus_mutex_lock(&context->mutex);
    {
        globus_assert(
            my_context->read_operations > 0 &&
            _XIOSL("Must be called on behalf of read operations"));
        globus_assert(
            my_context->state == GLOBUS_XIO_CONTEXT_STATE_OPEN ||
            my_context->state == GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED ||
            my_context->state == 
                GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING);
                
        if(my_context->state == GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED ||
            my_context->state == 
                GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING)
        {
            received = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&context->mutex);
    
    GlobusXIODebugExit();
    
    return received;
}

/*-*************************************************************************
 *                      driver setup functions
 *                      ----------------------
 **************************************************************************/
globus_result_t
globus_xio_driver_init(
    globus_xio_driver_t *               out_driver,
    const char *                        driver_name,
    void *                              user_data)
{
    globus_i_xio_driver_t *             driver;
    globus_result_t                     res;
    GlobusXIOName(globus_xio_driver_init);

    GlobusXIODebugEnter();

    driver = (globus_i_xio_driver_t *)
            globus_malloc(sizeof(globus_i_xio_driver_t));
    if(driver == NULL)
    {
        res = GlobusXIOErrorMemory("driver");
        goto err;
    }
    memset(driver, '\0', sizeof(globus_i_xio_driver_t));
    
    driver->name = globus_libc_strdup(driver_name);
    if(!driver->name)
    {
        globus_free(driver);
        res = GlobusXIOErrorMemory("driver->name");
        goto err;
    }
    
    driver->user_data = user_data;

    *out_driver = driver;

    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusXIODebugExitWithError();
    return res;
}

globus_result_t
globus_xio_driver_get_user_data(
    globus_xio_driver_t                 in_driver,
    void **                             out_user_data)
{
    globus_result_t                     res;
    globus_i_xio_driver_t *             driver;
    GlobusXIOName(globus_xio_driver_get_user_data);

    GlobusXIODebugEnter();
    if(in_driver == NULL)
    {
        res = GlobusXIOErrorMemory("in_driver");
        goto err;
    }
    if(out_user_data == NULL)
    {
        res = GlobusXIOErrorMemory("out_user_data");
        goto err;
    }

    driver = in_driver;

    *out_user_data = driver->user_data;

    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusXIODebugExitWithError();
    return res;
}

globus_result_t
globus_xio_driver_destroy(
    globus_xio_driver_t                 driver)
{
    GlobusXIOName(globus_xio_driver_destroy);

    GlobusXIODebugEnter();
    globus_free(driver->name);
    globus_free(driver);
    GlobusXIODebugExit();

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_xio_driver_set_transport(
    globus_xio_driver_t                 driver,
    globus_xio_driver_transport_open_t  transport_open_func,
    globus_xio_driver_close_t           close_func,
    globus_xio_driver_read_t            read_func,
    globus_xio_driver_write_t           write_func,
    globus_xio_driver_handle_cntl_t     handle_cntl_func)
{
    GlobusXIOName(globus_xio_driver_set_transport);

    GlobusXIODebugEnter();
    driver->transport_open_func = transport_open_func;
    driver->close_func = close_func;
    driver->read_func = read_func;
    driver->write_func = write_func;
    driver->handle_cntl_func = handle_cntl_func;
    GlobusXIODebugExit();

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_xio_driver_set_transform(
    globus_xio_driver_t                 driver,
    globus_xio_driver_transform_open_t  transform_open_func,
    globus_xio_driver_close_t           close_func,
    globus_xio_driver_read_t            read_func,
    globus_xio_driver_write_t           write_func,
    globus_xio_driver_handle_cntl_t     handle_cntl_func,
    globus_xio_driver_push_driver_t     push_driver_func)
{
    GlobusXIOName(globus_xio_driver_set_transform);

    GlobusXIODebugEnter();
    driver->transform_open_func = transform_open_func;
    driver->close_func = close_func;
    driver->read_func = read_func;
    driver->write_func = write_func;
    driver->handle_cntl_func = handle_cntl_func;
    driver->push_driver_func = push_driver_func;
    GlobusXIODebugExit();

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_xio_driver_set_server(
    globus_xio_driver_t                 driver,
    globus_xio_driver_server_init_t     server_init_func,
    globus_xio_driver_server_accept_t   server_accept_func,
    globus_xio_driver_server_destroy_t  server_destroy_func,
    globus_xio_driver_server_cntl_t     server_cntl_func,
    globus_xio_driver_link_cntl_t       link_cntl_func,
    globus_xio_driver_link_destroy_t    link_destroy_func)
{
    GlobusXIOName(globus_xio_driver_set_server);

    GlobusXIODebugEnter();
    driver->server_init_func = server_init_func;
    driver->server_accept_func = server_accept_func;
    driver->server_destroy_func = server_destroy_func;
    driver->server_cntl_func = server_cntl_func;
    driver->link_cntl_func = link_cntl_func;
    driver->link_destroy_func = link_destroy_func;
    GlobusXIODebugExit();

    return GLOBUS_SUCCESS;
}


/**
 * @brief Server Pre-Init
 * @ingroup globus_xio_driver
 * @details
 * This function adds a callback to a driver that will be called before
 * a server handle is created by XIO. This function has the same signature
 * as the server_init_func in the driver, but is always called with a 
 * NULL contact string. There is no support for calling a pass() or finished()
 * function for this interface. It may inspect and modify its attributes
 * and operation, but can not directly return any data or set a driver-specific
 * server handle value. If this function returns an error result, the server
 * create will be aborted.
 *
 * @param driver
 *     Driver to associate the function with
 * @param server_pre_init_func
 *     Function to call prior to creating a server
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_XIO_ERROR_PARAMETER
 *     Invalid parameter
 */
globus_result_t
globus_xio_driver_set_server_pre_init(
    globus_xio_driver_t                 driver,
    globus_xio_driver_server_init_t     server_pre_init_func)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusXIOName(globus_xio_driver_set_server_pre_init);

    GlobusXIODebugEnter();
    if (!driver)
    {
        res = GlobusXIOErrorParameter("driver");
        goto error;
    }
    driver->server_pre_init_func = server_pre_init_func;
    GlobusXIODebugExit();

    return res;
error:
    GlobusXIODebugExitWithError();
    return res;
}

globus_result_t
globus_xio_driver_set_attr(
    globus_xio_driver_t                 driver,
    globus_xio_driver_attr_init_t       attr_init_func,
    globus_xio_driver_attr_copy_t       attr_copy_func,
    globus_xio_driver_attr_cntl_t       attr_cntl_func,
    globus_xio_driver_attr_destroy_t    attr_destroy_func)
{
    GlobusXIOName(globus_xio_driver_set_attr);

    GlobusXIODebugEnter();

    if(driver == NULL)
    {
        return GlobusXIOErrorParameter("driver");
    }
    if(attr_init_func == NULL)
    {
        return GlobusXIOErrorParameter("attr_init_func");
    }
    if(attr_copy_func == NULL)
    {
        return GlobusXIOErrorParameter("attr_copy_func");
    }
    if(attr_destroy_func == NULL)
    {
        return GlobusXIOErrorParameter("attr_destroy_func");
    }

    driver->attr_init_func = attr_init_func;
    driver->attr_copy_func = attr_copy_func;
    driver->attr_cntl_func = attr_cntl_func;
    driver->attr_destroy_func = attr_destroy_func;

    GlobusXIODebugExit();

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_xio_driver_string_cntl_set_table(
    globus_xio_driver_t                 driver,
    globus_xio_string_cntl_table_t *    table)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_driver_set_string_table);

    GlobusXIODebugEnter();

    if(driver == NULL)
    {
        result = GlobusXIOErrorParameter("driver");
        goto error;
    }
    if(table == NULL)
    {
        result = GlobusXIOErrorParameter("table");
        goto error;
    }
    driver->string_table = table;

    GlobusXIODebugExit();

    return GLOBUS_SUCCESS;
error:
    GlobusXIODebugExitWithError();
    return result;
}

globus_result_t
globus_xio_driver_handle_string_cntl_set_table(
    globus_xio_driver_t                driver,
    globus_xio_string_cntl_table_t *   table)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_driver_string_cntl_set_table);

    GlobusXIODebugEnter();

    if(driver == NULL)
    {
        result = GlobusXIOErrorParameter("driver");
        goto error;
    }
    if(table == NULL)
    {
        result = GlobusXIOErrorParameter("table");
        goto error;
    }
    driver->handle_string_table = table;

    GlobusXIODebugExit();

    return GLOBUS_SUCCESS;
error:
    GlobusXIODebugExitWithError();
    return result;
}

void
globus_xio_operation_block_timeout(
    globus_xio_operation_t              op)
{
    op->block_timeout = GLOBUS_TRUE;
}

void
globus_xio_operation_unblock_timeout(
    globus_xio_operation_t              op)
{
    op->block_timeout = GLOBUS_FALSE;
}

/* note, this is called from win32 threads, therefore, it can never use
 * globus mutex calls
 */
void
globus_xio_operation_refresh_timeout(
    globus_xio_operation_t              op)
{
    op->progress = GLOBUS_TRUE;
}

/* returns true if operation already canceled */
globus_bool_t
globus_xio_operation_enable_cancel(
    globus_xio_operation_t              op,
    globus_xio_driver_cancel_callback_t cb,
    void *                              user_arg)
{
    globus_bool_t                       already_canceled;
    globus_mutex_t *                    mutex;

    if(op->type == GLOBUS_XIO_OPERATION_TYPE_ACCEPT)
    {
        mutex = &op->_op_server->mutex;
    }
    else
    {
        mutex = &op->_op_context->cancel_mutex;
    }
    
    globus_mutex_lock(mutex);
    {
        already_canceled = op->canceled != 0;
        if(op->canceled == 0)
        {
            op->cancel_cb = cb;
            op->cancel_arg = user_arg;
        }
    }
    globus_mutex_unlock(mutex);
    
    return already_canceled;
}

void
globus_xio_operation_disable_cancel(
    globus_xio_operation_t              op)
{
    globus_mutex_t *                    mutex;

    if(op->type == GLOBUS_XIO_OPERATION_TYPE_ACCEPT)
    {
        mutex = &op->_op_server->mutex;
    }
    else
    {
        mutex = &op->_op_context->cancel_mutex;
    }
    
    globus_mutex_lock(mutex);
    {
        op->cancel_cb = NULL;
        op->cancel_arg = NULL;
    }
    globus_mutex_unlock(mutex);
}

/* this is intended to only be used with a lock that a user also holds in the
 * cancel callback.  I have not thought of the validity outside of that use
 */
globus_bool_t
globus_xio_operation_is_canceled(
    globus_xio_operation_t              op)
{
    return op->canceled != 0;
}

globus_bool_t
globus_xio_driver_operation_is_blocking(
    globus_xio_operation_t              op)
{
    return op->blocking;
}

globus_size_t
globus_xio_operation_get_wait_for(
    globus_xio_operation_t              op)
{
    return op->entry[op->ndx - 1]._op_ent_wait_for;
}

void *
globus_xio_operation_get_driver_specific(
    globus_xio_operation_t              op)
{
    return op->_op_context->entry[op->ndx - 1].driver_handle;
}

globus_xio_driver_t
globus_xio_operation_get_user_driver(
    globus_xio_operation_t              op)
{
    return op->_op_context->entry[op->ndx - 1].driver;
}

/**
 * @brief Get the XIO transport driver associated with an op
 * @ingroup globus_xio_driver_programming
 */
globus_xio_driver_t
globus_xio_operation_get_transport_user_driver(
    globus_xio_operation_t              op)
{
    globus_xio_driver_t driver;

    switch (op->type)
    {

        case GLOBUS_XIO_OPERATION_TYPE_SERVER_INIT:
        case GLOBUS_XIO_OPERATION_TYPE_ACCEPT:
            driver = op->_op_server->entry[op->stack_size-1].driver;
            break;
        default:
            driver = op->_op_context->entry[op->stack_size-1].driver;
            break;
    }

    return driver;
}

globus_xio_driver_handle_t
globus_xio_operation_get_driver_handle(
    globus_xio_operation_t              op)
{
    if (op->type == GLOBUS_XIO_OPERATION_TYPE_SERVER_INIT ||
        op->type == GLOBUS_XIO_OPERATION_TYPE_ACCEPT)
    {
        return NULL;
    }
    return &op->_op_context->entry[op->ndx];
}

globus_xio_driver_handle_t
globus_xio_operation_get_driver_self_handle(
    globus_xio_operation_t              op)
{
    return &op->_op_context->entry[op->ndx - 1];
}

globus_result_t
globus_xio_operation_attr_cntl(
    globus_xio_operation_t              op,
    globus_xio_attr_cmd_t               cmd,
    ...)
{
    char **                             tmp_ptr;
    globus_result_t                     result;
    gss_cred_id_t *                     out_cred;
    va_list                             ap;
    GlobusXIOName(globus_xio_operation_attr_cntl);
 
    if(op == NULL)
    {
        result = GlobusXIOErrorParameter("op");
        goto error_param;
    }

    va_start(ap, cmd);

    switch(cmd)
    {
        case GLOBUS_XIO_ATTR_GET_CREDENTIAL:
            out_cred = va_arg(ap, gss_cred_id_t *);
            if(out_cred != NULL)
            {
                *out_cred = op->user_open_cred;
            }
            tmp_ptr = va_arg(ap, char **);
            if(tmp_ptr != NULL)
            {
                *tmp_ptr = globus_libc_strdup(op->user_open_sbj);
            }
            tmp_ptr = va_arg(ap, char **);
            if(tmp_ptr != NULL)
            {
                *tmp_ptr = globus_libc_strdup(op->user_open_username);
            }
            tmp_ptr = va_arg(ap, char **);
            if(tmp_ptr != NULL)
            {
                *tmp_ptr = globus_libc_strdup(op->user_open_pw);
            }
            break;

        default:
            result = GlobusXIOErrorParameter("cmd");
            goto error_cmd;
    }
    va_end(ap);


    return GLOBUS_SUCCESS;

error_cmd:
error_param:
    va_end(ap);

    return result;
}


void *
globus_xio_operation_get_data_descriptor(
    globus_xio_operation_t              op,
    globus_bool_t                       force_create)
{
    if(op->entry[op->ndx - 1].dd == NULL && (op->is_user_dd || force_create))
    {
        /* need to create a dd */
        if(op->_op_context->entry[op->ndx - 1].driver->attr_init_func(
            &op->entry[op->ndx - 1].dd) != GLOBUS_SUCCESS)
        {
            op->entry[op->ndx - 1].dd = NULL;
        }
    }
    
    return op->entry[op->ndx - 1].dd;
}

globus_result_t
globus_xio_operation_copy_stack(
    globus_xio_operation_t              op,
    globus_xio_stack_t *                stack)
{
    globus_result_t                     result;
    globus_i_xio_server_t *             server;
    globus_i_xio_context_t *            context;
    globus_i_xio_stack_t *              istack;
    int                                 ndx;
    GlobusXIOName(globus_xio_operation_copy_stack);

    GlobusXIODebugEnter();

    result = globus_xio_stack_init(stack, NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_init;
    }
    
    istack = *stack;
    
    switch(op->type)
    {
      case GLOBUS_XIO_OPERATION_TYPE_SERVER_INIT:
        server = op->_op_server;
        
        for(ndx = op->stack_size - 1; ndx > op->ndx; ndx--)
        {
            istack->size++;
            globus_list_insert(
                &istack->driver_stack, server->entry[ndx].driver);
        }
        break;
      
      case GLOBUS_XIO_OPERATION_TYPE_ACCEPT:
        server = op->_op_server;
        
        for(ndx = op->stack_size - 1; ndx >= op->ndx; ndx--)
        {
            istack->size++;
            globus_list_insert(
                &istack->driver_stack, server->entry[ndx].driver);
        }
        break;
        
      default:
        context = op->_op_context;
        
        for(ndx = op->stack_size - 1; ndx >= op->ndx; ndx--)
        {
            istack->size++;
            globus_list_insert(
                &istack->driver_stack, context->entry[ndx].driver);
        }
        break;
    }
    
    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;
    
error_init:
    GlobusXIODebugExitWithError();
    return result;
}
