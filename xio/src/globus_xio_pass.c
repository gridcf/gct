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

#include "globus_i_xio.h"
#include "globus_xio_util.h"

/************************************************************************
 *                              open
 *                              ----
 ***********************************************************************/


globus_result_t
globus_xio_driver_pass_open(
    globus_xio_operation_t              in_op,
    const globus_xio_contact_t *        contact_info,
    globus_xio_driver_callback_t        in_cb,
    void *                              in_user_arg)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_handle_t *             handle;
    globus_i_xio_context_t *            context;
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_op_entry_t *           my_op;
    int                                 prev_ndx;
    globus_result_t                     res;
    globus_bool_t                       destroy_handle = GLOBUS_FALSE;
    globus_bool_t                       close = GLOBUS_FALSE;
    globus_xio_driver_t                 driver;
    GlobusXIOName(globus_xio_driver_pass_open);

    GlobusXIODebugInternalEnter();
    op = (in_op);
    globus_assert(op->ndx < op->stack_size);
    handle = op->_op_handle;
    context = op->_op_context;
    op->progress = GLOBUS_TRUE;
    op->block_timeout = GLOBUS_FALSE;
    
    
    if(op->canceled)
    {
        GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE,
            (_XIOSL("[%s] :Operation canceled\n"), _xio_name));
        res = GlobusXIOErrorCanceled();
    }
    else
    {
        my_context = &context->entry[op->ndx];
        GlobusXIOContextStateChange(my_context,
            GLOBUS_XIO_CONTEXT_STATE_OPENING);
        my_context->outstanding_operations++;
        context->ref++;
        prev_ndx = op->ndx;

        do
        {
            driver = context->entry[op->ndx].driver;
            op->ndx++;
        }
        while(driver->transport_open_func == NULL &&
              driver->transform_open_func == NULL);

        op->entry[prev_ndx].next_ndx = op->ndx;
        op->entry[prev_ndx].type = GLOBUS_XIO_OPERATION_TYPE_OPEN;
        my_op = &op->entry[op->ndx - 1];

        my_op->cb = (in_cb);
        my_op->user_arg = (in_user_arg);
        my_op->prev_ndx = prev_ndx;
        my_op->type = GLOBUS_XIO_OPERATION_TYPE_OPEN;
        /* at time that stack is built this will be varified */
        globus_assert(op->ndx <= context->stack_size);

        /* ok to do this unlocked because no one else has it yet */
        op->ref += 2; /* 1 for the pass, and one until finished */
        my_op->in_register = GLOBUS_TRUE;
        if(op->ndx == op->stack_size)
        {
            res = driver->transport_open_func(
                        contact_info,
                        my_op->link,
                        my_op->open_attr,
                        op);
        }
        else
        {
            res = driver->transform_open_func(
                        contact_info,
                        my_op->link,
                        my_op->open_attr,
                        op);
        }
        my_op->in_register = GLOBUS_FALSE;
        
        if(driver->attr_destroy_func != NULL && my_op->open_attr != NULL)
        {
            driver->attr_destroy_func(my_op->open_attr);
            my_op->open_attr = NULL;
        }

        if(res == GLOBUS_SUCCESS && prev_ndx == 0)
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
            if(res != GLOBUS_SUCCESS)
            {
                globus_i_xio_pass_failed(op, my_context, &close,
                    &destroy_handle);
                context->ref--;
                /* cant possibly be 0 while there is still this op */
                globus_assert(context->ref > 0);
                globus_assert(!destroy_handle);
            }
            GlobusXIOOpDec(op); /* for the pass */
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
    }
    GlobusXIODebugInternalExit();

    return res;
}


void
globus_xio_driver_finished_open(
    void *                              in_dh,
    globus_xio_operation_t              in_op,
    globus_result_t                     in_res)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_context_t *            context;
    globus_i_xio_op_entry_t *           my_op;
    globus_result_t                     res;
    globus_callback_space_t             space =
                            GLOBUS_CALLBACK_GLOBAL_SPACE;
    GlobusXIOName(globus_xio_driver_finished_open);

    GlobusXIODebugInternalEnter();
    res = (in_res);
    op = (globus_i_xio_op_t *)(in_op);
    globus_assert(op->ndx > 0);
    op->progress = GLOBUS_TRUE;
    op->block_timeout = GLOBUS_FALSE;

    context = op->_op_context;
    context->entry[op->ndx - 1].driver_handle = (in_dh);
    my_op = &op->entry[op->ndx - 1];
    my_context = &context->entry[my_op->prev_ndx];
    /* no operation can happen while in OPENING state so no need to lock */

    switch(my_context->state)
    {
        case GLOBUS_XIO_CONTEXT_STATE_OPENING:
            if(res == GLOBUS_SUCCESS)
            {
                GlobusXIOContextStateChange(my_context,
                    GLOBUS_XIO_CONTEXT_STATE_OPEN);
            }
            else
            {
                GlobusXIOContextStateChange(my_context,
                    GLOBUS_XIO_CONTEXT_STATE_OPEN_FAILED);
            }
            break;

        /* if user has already called close */
        case GLOBUS_XIO_CONTEXT_STATE_OPENING_AND_CLOSING:
                GlobusXIOContextStateChange(my_context,
                    GLOBUS_XIO_CONTEXT_STATE_CLOSING);
            break;

        default:
            globus_assert(0);
    }

    if(my_op->prev_ndx == 0 && !op->blocking && op->_op_handle)
    {
        space = op->_op_handle->space;
    }
    op->cached_obj = GlobusXIOResultToObj(res);
    if(my_op->in_register || space != GLOBUS_CALLBACK_GLOBAL_SPACE)
    {
        /* if this is a blocking op, we avoid the oneshot by delaying the
         * finish until the stack unwinds
         */
        if(op->blocking && 
            GlobusXIOBlockedThreadMatchesCurrentThread(op->blocked_thread))
        {
            GlobusXIODebugDelayedFinish();
            op->finished_delayed = GLOBUS_TRUE;
        }
        else
        {
            GlobusXIODebugInregisterOneShot();
            globus_i_xio_register_oneshot(
                op->_op_handle,
                globus_l_xio_driver_open_op_kickout,
                (void *)op,
                space);
        }
    }
    else
    {
        globus_l_xio_driver_open_op_kickout(op);
    }
    GlobusXIODebugInternalExit();
}

void
globus_xio_driver_open_delivered(
    globus_xio_operation_t              in_op,
    int                                 in_ndx,
    globus_xio_operation_type_t *       deliver_type)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_op_t *                 close_op = NULL;
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_context_t *            context;
    globus_bool_t                       close_kickout = GLOBUS_FALSE;
    globus_bool_t                       destroy_handle = GLOBUS_FALSE;
    globus_bool_t                       destroy_context = GLOBUS_FALSE;
    globus_i_xio_handle_t *             handle;
    globus_callback_space_t             space =
                            GLOBUS_CALLBACK_GLOBAL_SPACE;
    GlobusXIOName(globus_xio_driver_open_delivered);

    GlobusXIODebugInternalEnter();
    op = (in_op);
    context = op->_op_context;
    handle = op->_op_handle;
    my_context = &context->entry[in_ndx];

    /* LOCK */
    globus_mutex_lock(&context->mutex);
    {
        /* make sure it only gets delivered once */
        if(deliver_type == NULL ||
            *deliver_type == GLOBUS_XIO_OPERATION_TYPE_FINISHED)
        {
            GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE,
                (_XIOSL("[%s] : Already delivered\n"), _xio_name));
            GlobusXIOOpDec(op);
            if(op->ref == 0)
            {
                globus_i_xio_op_destroy(op, &destroy_handle);
            }
            globus_mutex_unlock(&context->mutex);
            goto exit;
        }
        *deliver_type = GLOBUS_XIO_OPERATION_TYPE_FINISHED;
        op->entry[in_ndx].deliver_type = NULL;

        GlobusXIOOpDec(op);
        if(op->ref == 0)
        {
            globus_i_xio_op_destroy(op, &destroy_handle);
        }
        globus_assert(my_context->outstanding_operations > 0);
        my_context->outstanding_operations--;
        switch(my_context->state)
        {
            /* open failed and user didn't try and close */
            case GLOBUS_XIO_CONTEXT_STATE_OPEN_FAILED:
                GlobusXIOContextStateChange(my_context,
                    GLOBUS_XIO_CONTEXT_STATE_CLOSED);
                context->ref--;
                break;

            /* this happens when the open fails and the user calls close */
            case GLOBUS_XIO_CONTEXT_STATE_OPENING_AND_CLOSING:
                GlobusXIOContextStateChange(my_context,
                    GLOBUS_XIO_CONTEXT_STATE_CLOSING);
                if(!my_context->close_started &&
                    my_context->outstanding_operations == 0 &&
                    my_context->close_op != NULL)
                {
                    close_kickout = GLOBUS_TRUE;
                    my_context->close_started = GLOBUS_TRUE;
                    close_op = my_context->close_op;
                    context->ref--;
                }
                break;

            case GLOBUS_XIO_CONTEXT_STATE_OPEN:
            case GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED:
            case GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED:
            case GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING:
                break;
            
            case GLOBUS_XIO_CONTEXT_STATE_CLOSING:
            case GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED_AND_CLOSING:
                if(!my_context->close_started &&
                    my_context->outstanding_operations == 0 &&
                    my_context->close_op != NULL)
                {
                    my_context->close_started = GLOBUS_TRUE;
                    close_op = my_context->close_op;
                }
                break;

            default:
                globus_assert(0);
                break;
        }
        
        if(context->ref == 0)
        {
            destroy_context = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&context->mutex);

    if(close_op != NULL)
    {
        /* if closed before fully opened and open was successful we need
           to start the regular close process */
        if(!close_kickout)
        {
            globus_i_xio_driver_start_close(close_op, GLOBUS_FALSE);
        }
        /* if open failed then just kickout the close */
        else
        {
            if(close_op->entry[close_op->ndx - 1].prev_ndx == 0 &&
                    !close_op->blocking &&
                close_op->_op_handle != NULL)
            {
                space = close_op->_op_handle->space;
            }
            globus_i_xio_register_oneshot(
                handle,
                globus_l_xio_driver_op_close_kickout,
                (void *)close_op,
                space);
        }
    }

  exit:
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
}


/************************************************************************
 *                          close
 *                          -----
 ***********************************************************************/

globus_result_t
globus_xio_driver_pass_close(
    globus_xio_operation_t              in_op,
    globus_xio_driver_callback_t        in_cb,
    void *                              in_ua)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_context_t *            context;
    globus_i_xio_context_entry_t *      my_context;
    globus_bool_t                       pass;
    globus_i_xio_op_entry_t *           my_op;
    int                                 prev_ndx;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_xio_driver_t                 driver;
    globus_xio_operation_type_t         deliver_type = 
        GLOBUS_XIO_OPERATION_TYPE_FINISHED;
    GlobusXIOName(globus_xio_driver_pass_close);

    GlobusXIODebugInternalEnter();
    op = (in_op);
    globus_assert(op->ndx < op->stack_size);
    context = op->_op_context;
    op->progress = GLOBUS_TRUE;
    op->block_timeout = GLOBUS_FALSE;

    my_context = &context->entry[op->ndx];

    if(op->canceled && op->type != GLOBUS_XIO_OPERATION_TYPE_OPEN)
    {
        GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE,
            (_XIOSL("[%s] :Operation canceled\n"), _xio_name));
        res = GlobusXIOErrorCanceled();
    }
    else
    {
        prev_ndx = op->ndx;

        do
        {
            driver = context->entry[op->ndx].driver;
            op->ndx++;
        }
        while(driver->close_func == NULL);
        my_op = &op->entry[op->ndx - 1];
        my_op->type = GLOBUS_XIO_OPERATION_TYPE_CLOSE;

        /* deal with context state */
        globus_mutex_lock(&context->mutex);
        {
            switch(my_context->state)
            {
                case GLOBUS_XIO_CONTEXT_STATE_OPEN:
                    GlobusXIOContextStateChange(my_context,
                        GLOBUS_XIO_CONTEXT_STATE_CLOSING);
                    break;

                case GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED:
                    GlobusXIOContextStateChange(my_context,
                        GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING);
                    break;

                case GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED:
                    GlobusXIOContextStateChange(my_context,
                        GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED_AND_CLOSING);
                    break;

                case GLOBUS_XIO_CONTEXT_STATE_OPEN_FAILED:
                case GLOBUS_XIO_CONTEXT_STATE_OPENING:
                    GlobusXIOContextStateChange(my_context,
                        GLOBUS_XIO_CONTEXT_STATE_OPENING_AND_CLOSING);
                    break;

                default:
                    globus_assert(0);
            }
            /* a barrier will never happen if the level above already did th
                close barrier and this level has not created any driver ops.
                in this case outstanding_operations is garentueed to be zero
            */
            globus_assert(!my_context->close_started);
            if(my_context->outstanding_operations == 0)
            {
                pass = GLOBUS_TRUE;
                my_context->close_started = GLOBUS_TRUE;
            }
            /* cache the op for close barrier */
            else
            {
                pass = GLOBUS_FALSE;
                my_context->close_op = op;
            }
            if(op->entry[prev_ndx].deliver_type != NULL)
            {
                /* make local copy */
                deliver_type = *op->entry[prev_ndx].deliver_type;
                /* set copy in finished to null, thus preventing Delived 
                    from being called twice */
                *op->entry[prev_ndx].deliver_type = 
                    GLOBUS_XIO_OPERATION_TYPE_FINISHED;
                /* set the op ppinter to NULL for completeness */
                op->entry[prev_ndx].deliver_type = NULL;

                /* op ref count so that op stays around long enough to check
                    that it was restarted */
                GlobusXIOOpInc(op);
            }
        }
        globus_mutex_unlock(&context->mutex);

        my_op->cb = (in_cb);
        my_op->user_arg = (in_ua);
        my_op->prev_ndx = prev_ndx;

        if(deliver_type != GLOBUS_XIO_OPERATION_TYPE_FINISHED)
        {
            globus_i_xio_driver_deliver_op(op, prev_ndx, deliver_type);
        }

        /* op can be checked outside of lock */
        if(pass)
        {
            res = globus_i_xio_driver_start_close(op, GLOBUS_TRUE);
        }
    }
    if(res != GLOBUS_SUCCESS)
    {
        globus_mutex_lock(&context->mutex);
        {
            GlobusXIOContextStateChange(my_context,
                GLOBUS_XIO_CONTEXT_STATE_CLOSED);
            context->ref--;
            /* cant possibly be 0 if there is still an op */
            globus_assert(context->ref > 0);
        }
        globus_mutex_unlock(&context->mutex);
    }
    GlobusXIODebugInternalExit();

    return res;
}

void
globus_xio_driver_finished_close(
    globus_xio_operation_t              in_op,
    globus_result_t                     in_res)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_context_t *            context;
    globus_i_xio_op_entry_t *           my_op;
    globus_result_t                     res;
    globus_callback_space_t             space =
                            GLOBUS_CALLBACK_GLOBAL_SPACE;
    GlobusXIOName(globus_xio_driver_finished_close);

    GlobusXIODebugInternalEnter();
    res = (in_res);
    op = (globus_i_xio_op_t *)(in_op);
    globus_assert(op->ndx > 0);
    op->progress = GLOBUS_TRUE;
    op->block_timeout = GLOBUS_FALSE;

    context = op->_op_context;
    my_op = &op->entry[op->ndx - 1];
    my_context = &context->entry[my_op->prev_ndx];

    globus_mutex_lock(&context->mutex);
    {
        GlobusXIOContextStateChange(my_context,
            GLOBUS_XIO_CONTEXT_STATE_CLOSED);
        context->ref--;
    }
    globus_mutex_unlock(&context->mutex);

    globus_assert(op->ndx >= 0); /* otherwise we are not in bad memory */
    op->cached_obj = GlobusXIOResultToObj(res);
    if(my_op->prev_ndx == 0 && !op->blocking && op->_op_handle)
    {
        space = op->_op_handle->space;
    }
    if(my_op->in_register || space != GLOBUS_CALLBACK_GLOBAL_SPACE)
    {
        /* if this is a blocking op, we avoid the oneshot by delaying the
         * finish until the stack unwinds
         */
        if(op->blocking && 
            GlobusXIOBlockedThreadMatchesCurrentThread(op->blocked_thread))
        {
            GlobusXIODebugDelayedFinish();
            op->finished_delayed = GLOBUS_TRUE;
        }
        else
        {
            GlobusXIODebugInregisterOneShot();
            globus_i_xio_register_oneshot(
                op->_op_handle,
                globus_l_xio_driver_op_close_kickout,
                (void *)op,
                space);
        }
    }
    else
    {
        globus_l_xio_driver_op_close_kickout(op);
    }
    
    GlobusXIODebugInternalExit();
}


/************************************************************************
 *                              write
 *                              -----
 ***********************************************************************/
globus_result_t
globus_xio_driver_pass_write(
    globus_xio_operation_t              in_op,
    globus_xio_iovec_t *                in_iovec,
    int                                 in_iovec_count,
    globus_size_t                       in_wait_for,
    globus_xio_driver_data_callback_t   in_cb,
    void *                              in_user_arg)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_op_entry_t *           my_op;
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_context_entry_t *      next_context;
    globus_i_xio_context_t *            context;
    globus_bool_t                       close = GLOBUS_FALSE;
    int                                 prev_ndx;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_xio_driver_t                 driver;
    globus_xio_operation_type_t         deliver_type = 
        GLOBUS_XIO_OPERATION_TYPE_FINISHED;
    globus_i_xio_handle_t *             handle;
    globus_bool_t                       destroy_handle = GLOBUS_FALSE;
    GlobusXIOName(globus_xio_driver_pass_write);

    GlobusXIODebugInternalEnter();
    op = (in_op);
    context = op->_op_context;
    my_context = &context->entry[op->ndx];
    op->progress = GLOBUS_TRUE;
    op->block_timeout = GLOBUS_FALSE;
    handle = op->_op_handle;

    globus_assert(op->ndx < op->stack_size);

    /* error checking */
    globus_assert(my_context->state == GLOBUS_XIO_CONTEXT_STATE_OPEN ||
        my_context->state == GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED ||
        my_context->state == GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED);
    if(op->canceled)
    {
        GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE,
            (_XIOSL("[%s] :Operation canceled\n"), _xio_name));
        res = GlobusXIOErrorCanceled();
    }
    else
    {
        /* set up the entry */
        prev_ndx = op->ndx;
        do
        {
            next_context = &context->entry[op->ndx];
            driver = next_context->driver;
            op->ndx++;
        }
        while(driver->write_func == NULL);

        op->entry[prev_ndx].next_ndx = op->ndx;
        op->entry[prev_ndx].type = GLOBUS_XIO_OPERATION_TYPE_WRITE;
        my_op = &op->entry[op->ndx - 1];
        my_op->prev_ndx = prev_ndx;
        my_op->_op_ent_data_cb = (in_cb);
        my_op->user_arg = (in_user_arg);
        my_op->_op_ent_iovec = (in_iovec);
        my_op->_op_ent_iovec_count = (in_iovec_count);
        my_op->_op_ent_nbytes = 0;
        my_op->_op_ent_wait_for = (in_wait_for);
        my_op->type = GLOBUS_XIO_OPERATION_TYPE_WRITE;
        
        globus_mutex_lock(&context->mutex);
        {
            if(op->entry[prev_ndx].deliver_type != NULL)
            {
                /* make local copy */
                deliver_type = *op->entry[prev_ndx].deliver_type;
                /* set copy in finished to null, thus preventing Delived 
                    from being called twice */
                *op->entry[prev_ndx].deliver_type = 
                    GLOBUS_XIO_OPERATION_TYPE_FINISHED;
                /* set the op ppinter to NULL for completeness */
                op->entry[prev_ndx].deliver_type = NULL;
                /* op ref count so that op stays around long enough to check
                    that it was restarted */
                GlobusXIOOpInc(op);
            }
            my_context->outstanding_operations++;
            op->ref += 2; /* for pass and until finished */
        }
        globus_mutex_unlock(&context->mutex);

        if(deliver_type != GLOBUS_XIO_OPERATION_TYPE_FINISHED)
        {
            globus_i_xio_driver_deliver_op(op, prev_ndx, deliver_type);
        }
        
        /* set the callstack flag */
        my_op->in_register = GLOBUS_TRUE;

        res = driver->write_func(
                        next_context->driver_handle,
                        my_op->_op_ent_iovec,
                        my_op->_op_ent_iovec_count,
                        op);

        /* flip the callstack flag */
        my_op->in_register = GLOBUS_FALSE;
        if(res == GLOBUS_SUCCESS && prev_ndx == 0)
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

            if(res != GLOBUS_SUCCESS)
            {
                globus_i_xio_pass_failed(op, my_context, &close,
                    &destroy_handle);
                globus_assert(!destroy_handle);
            }
        }
        globus_mutex_unlock(&context->mutex);
    }

    if(close)
    {
        globus_i_xio_driver_start_close(my_context->close_op,
                GLOBUS_FALSE);
    }
    else if(destroy_handle)
    {
        globus_i_xio_handle_destroy(handle);
    }

    GlobusXIODebugInternalExit();

    return res;
}


void
globus_xio_driver_finished_write(
    globus_xio_operation_t              in_op,
    globus_result_t                     result,
    globus_size_t                       nbytes)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_op_entry_t *           my_op;
    globus_result_t                     res;
    globus_bool_t                       fire_cb = GLOBUS_TRUE;
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_context_t *            context;
    globus_callback_space_t             space =
                            GLOBUS_CALLBACK_GLOBAL_SPACE;
    GlobusXIOName(globus_xio_driver_finished_write);

    GlobusXIODebugInternalEnter();
    op = (globus_i_xio_op_t *)(in_op);
    res = (result);
    op->progress = GLOBUS_TRUE;
    op->block_timeout = GLOBUS_FALSE;

    context = op->_op_context;
    my_op = &op->entry[op->ndx - 1];
    my_context = &context->entry[my_op->prev_ndx];

    op->cached_obj = GlobusXIOResultToObj(res);

    globus_assert(my_context->state != GLOBUS_XIO_CONTEXT_STATE_OPENING &&
        my_context->state != GLOBUS_XIO_CONTEXT_STATE_CLOSED);

    my_op->_op_ent_nbytes += nbytes;
    /* if not all bytes were written */
    if(my_op->_op_ent_nbytes < my_op->_op_ent_wait_for &&
        res == GLOBUS_SUCCESS)
    {
        /* if not enough bytes read set the fire_cb default to false */
        fire_cb = GLOBUS_FALSE;
        /* repass the operation down */
        res = globus_i_xio_repass_write(op);
        if(res != GLOBUS_SUCCESS)
        {
            fire_cb = GLOBUS_TRUE;
        }
    }
    if(fire_cb)
    {
        if(my_op->_op_ent_fake_iovec != NULL)
        {
            globus_free(my_op->_op_ent_fake_iovec);
            my_op->_op_ent_fake_iovec = NULL;
        }
        if(my_op->prev_ndx == 0 && !op->blocking && op->_op_handle)
        {
            space = op->_op_handle->space;
        }

        globus_assert(my_op->type == GLOBUS_XIO_OPERATION_TYPE_WRITE);
        if(my_op->in_register || space != GLOBUS_CALLBACK_GLOBAL_SPACE)
        {
            /* if this is a blocking op, we avoid the oneshot by delaying the
             * finish until the stack unwinds
             */
            if(op->blocking && 
                GlobusXIOBlockedThreadMatchesCurrentThread(op->blocked_thread))
            {
                GlobusXIODebugDelayedFinish();
                op->finished_delayed = GLOBUS_TRUE;
            }
            else
            {
                GlobusXIODebugInregisterOneShot();
                globus_i_xio_register_oneshot(
                    op->_op_handle,
                    globus_l_xio_driver_op_write_kickout,
                    (void *)op,
                    space);
            }
        }
        else
        {
            globus_l_xio_driver_op_write_kickout(op);
        }
    }
    GlobusXIODebugInternalExit();
}

void
globus_xio_driver_write_delivered(
    globus_xio_operation_t              in_op,
    int                                 in_ndx,
    globus_xio_operation_type_t *       deliver_type)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_op_t *                 close_op;
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_context_t *            context;
    globus_bool_t                       close = GLOBUS_FALSE;
    globus_bool_t                       destroy_handle = GLOBUS_FALSE;
    globus_i_xio_handle_t *             handle;
    GlobusXIOName(globus_xio_driver_write_delivered);

    GlobusXIODebugInternalEnter();
    op = (in_op);
    context = op->_op_context;
    my_context = &context->entry[in_ndx];
    handle = op->_op_handle;

    /* LOCK */
    globus_mutex_lock(&context->mutex);
    {
        /* make sure it only gets delivered once */
        if(deliver_type == NULL ||
            *deliver_type == GLOBUS_XIO_OPERATION_TYPE_FINISHED)
        {
            GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE,
                (_XIOSL("[%s] : Already delivered\n"), _xio_name));
            GlobusXIOOpDec(op);
            if(op->ref == 0)
            {
                globus_i_xio_op_destroy(op, &destroy_handle);
            }
            globus_mutex_unlock(&context->mutex);
            goto exit;
        }
        op->entry[in_ndx].deliver_type = NULL;
        *deliver_type = GLOBUS_XIO_OPERATION_TYPE_FINISHED;

        GlobusXIOOpDec(op);
        if(op->ref == 0)
        {
            globus_i_xio_op_destroy(op, &destroy_handle);
        }
        globus_assert(my_context->outstanding_operations > 0);
        my_context->outstanding_operations--;

        GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE,
            (_XIOSL("[%s] : Context @ 0x%x State=%d Count=%d close_start=%d\n"),
            _xio_name, my_context, my_context->state,
            my_context->outstanding_operations,
            my_context->close_started));

        /* if we have a close delayed */
        if((my_context->state == GLOBUS_XIO_CONTEXT_STATE_CLOSING ||
            my_context->state ==
                GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED_AND_CLOSING) &&
            my_context->outstanding_operations == 0 &&
            !my_context->close_started)
        {
            globus_assert(my_context->close_op != NULL);
            close = GLOBUS_TRUE;
            close_op = my_context->close_op;
            my_context->close_started = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&context->mutex);
    if(close)
    {
        globus_i_xio_driver_start_close(close_op, GLOBUS_FALSE);
    }

  exit:
    if(destroy_handle)
    {
        globus_i_xio_handle_destroy(handle);
    }
    GlobusXIODebugInternalExit();
}

/************************************************************************
 *                           read
 *                           ----
 ***********************************************************************/

globus_result_t
globus_xio_driver_pass_read(
    globus_xio_operation_t              in_op,
    globus_xio_iovec_t *                in_iovec,
    int                                 in_iovec_count,
    globus_size_t                       in_wait_for,
    globus_xio_driver_data_callback_t   in_cb,
    void *                              in_user_arg)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_op_entry_t *           my_op;
    globus_i_xio_context_entry_t *      next_context;
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_context_t *            context;
    int                                 prev_ndx;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_bool_t                       close = GLOBUS_FALSE;
    globus_xio_driver_t                 driver;
    globus_bool_t                       destroy_handle = GLOBUS_FALSE;
    globus_bool_t                       pass = GLOBUS_TRUE;
    globus_xio_operation_type_t         deliver_type = 
        GLOBUS_XIO_OPERATION_TYPE_FINISHED;
    GlobusXIOName(globus_xio_driver_pass_read);

    GlobusXIODebugInternalEnter();
    op = (in_op);
    context = op->_op_context;
    my_context = &context->entry[op->ndx];
    op->progress = GLOBUS_TRUE;
    op->block_timeout = GLOBUS_FALSE;
    prev_ndx = op->ndx;

    globus_assert(op->ndx < op->stack_size);

    /* error checking */
    globus_assert(my_context->state == GLOBUS_XIO_CONTEXT_STATE_OPEN ||
        my_context->state == GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED ||
        my_context->state == GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED);
    if(op->canceled)
    {
        GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE,
            (_XIOSL("[%s] :Operation canceled\n"), _xio_name));
        res = GlobusXIOErrorCanceled();
    }
    else
    {
        /* find next slot. start on next and find first interseted */
        do
        {
            next_context = &context->entry[op->ndx];
            driver = next_context->driver;
            op->ndx++;
        }
        while(driver->read_func == NULL);

        op->entry[prev_ndx].next_ndx = op->ndx;
        op->entry[prev_ndx].type = GLOBUS_XIO_OPERATION_TYPE_READ;
        my_op = &op->entry[op->ndx - 1];
        my_op->prev_ndx = prev_ndx;
        my_op->_op_ent_data_cb = (in_cb);
        my_op->user_arg = (in_user_arg);
        my_op->_op_ent_iovec = (in_iovec);
        my_op->_op_ent_iovec_count = (in_iovec_count);
        my_op->_op_ent_nbytes = 0;
        my_op->_op_ent_wait_for = (in_wait_for);
        my_op->type = GLOBUS_XIO_OPERATION_TYPE_READ;
        
        globus_mutex_lock(&context->mutex);
        {
            if(op->entry[prev_ndx].deliver_type != NULL)
            {
                /* make local copy */
                deliver_type = *op->entry[prev_ndx].deliver_type;
                /* set copy in finished to null, thus preventing Delived 
                    from being called twice */
                *op->entry[prev_ndx].deliver_type = 
                    GLOBUS_XIO_OPERATION_TYPE_FINISHED;
                /* set the op ppinter to NULL for completeness */
                op->entry[prev_ndx].deliver_type = NULL;
                /* op ref count so that op stays around long enough to check
                    that it was restarted */
                GlobusXIOOpInc(op);
            }
            
            if(my_context->state == GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED)
            {
                GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO,
                    (_XIOSL("[%s]: Queuing read on eof list\n"), _xio_name));
                
                op->cached_obj = GlobusXIOErrorObjEOF();
                globus_list_insert(&my_context->eof_op_list, op);
                op->ref++;
                my_context->eof_operations++;
                pass = GLOBUS_FALSE;
            }
            else if(
                my_context->state == GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED ||
                my_context->pending_reads > 0)
            {
                GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO,
                    (_XIOSL("[%s]: Queuing read on pending queue\n"), _xio_name));
                    
                /* add this to the pending queue */
                my_context->pending_reads++;
                globus_fifo_enqueue(&my_context->pending_read_queue, op);
                op->ref++;
                pass = GLOBUS_FALSE;
            }
            else
            {
                my_context->read_operations++;
                op->ref += 2; /* 1 for pass, 1 until finished */
            }
            my_context->outstanding_operations++;
        }
        globus_mutex_unlock(&context->mutex);

        if(deliver_type != GLOBUS_XIO_OPERATION_TYPE_FINISHED)
        {
            globus_i_xio_driver_deliver_op(op, prev_ndx, deliver_type);
        }
        
        if(pass)
        {
            /* set the callstack flag */
            my_op->in_register = GLOBUS_TRUE;
    
            res = driver->read_func(
                            next_context->driver_handle,
                            my_op->_op_ent_iovec,
                            my_op->_op_ent_iovec_count,
                            op);
    
            /* flip the callstack flag */
            my_op->in_register = GLOBUS_FALSE;
            if(res == GLOBUS_SUCCESS && prev_ndx == 0)
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
                    globus_assert(!destroy_handle);
                }
    
                if(res != GLOBUS_SUCCESS)
                {
                    globus_i_xio_pass_failed(op, my_context, &close,
                        &destroy_handle);
                    globus_assert(!destroy_handle);
                    
                    my_context->read_operations--;
                    if(my_context->read_operations == 0 &&
                        (my_context->state ==
                            GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED ||
                        my_context->state ==
                            GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING))
                    {
                        globus_l_xio_driver_purge_read_eof(my_context);
                    }
                }
            }
            globus_mutex_unlock(&context->mutex);
        }
    }

    if(close)
    {
        globus_i_xio_driver_start_close(my_context->close_op,
                GLOBUS_FALSE);
    }

    GlobusXIODebugInternalExit();

    return res;
}


void
globus_xio_driver_finished_read(
    globus_xio_operation_t              in_op,
    globus_result_t                     result,
    globus_size_t                       nbytes)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_op_entry_t *           my_op;
    globus_result_t                     res;
    globus_bool_t                       fire_cb = GLOBUS_TRUE;
    globus_i_xio_context_entry_t *      my_context;
    globus_i_xio_context_t *            context;
    globus_callback_space_t             space =
                            GLOBUS_CALLBACK_GLOBAL_SPACE;
    GlobusXIOName(globus_xio_driver_finished_read);

    GlobusXIODebugInternalEnter();
    op = (globus_i_xio_op_t *)(in_op);
    res = (result);
    op->progress = GLOBUS_TRUE;
    op->block_timeout = GLOBUS_FALSE;

    context = op->_op_context;
    my_op = &op->entry[op->ndx - 1];
    my_context = &context->entry[my_op->prev_ndx];

    globus_assert(op->ndx > 0);
    globus_assert(my_context->state != GLOBUS_XIO_CONTEXT_STATE_OPENING &&
        my_context->state != GLOBUS_XIO_CONTEXT_STATE_CLOSED);

    my_op->_op_ent_nbytes += nbytes;

    if(res != GLOBUS_SUCCESS && globus_xio_error_is_eof(res))
    {
        globus_mutex_lock(&context->mutex);
        {
            switch(my_context->state)
            {
                case GLOBUS_XIO_CONTEXT_STATE_OPEN:
                    GlobusXIOContextStateChange(my_context,
                        GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED);
                    break;

                case GLOBUS_XIO_CONTEXT_STATE_CLOSING:
                    GlobusXIOContextStateChange(my_context,
                        GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING);
                    break;

                case GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING:
                case GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED:
                    break;

                default:
                    globus_assert(0);
                    break;
            }
            my_context->read_operations--;
            my_context->eof_operations++;
            if(my_context->read_operations > 0)
            {
                op->cached_obj = GlobusXIOResultToObj(res);
                globus_list_insert(&my_context->eof_op_list, op);
                fire_cb = GLOBUS_FALSE;
            }
        }
        globus_mutex_unlock(&context->mutex);
    }
    /* if not all bytes were read */
    else if(my_op->_op_ent_nbytes < my_op->_op_ent_wait_for &&
        res == GLOBUS_SUCCESS)
    {
        /* if not enough bytes read set the fire_cb deafult to false */
        fire_cb = GLOBUS_FALSE;
        res = globus_i_xio_repass_read(op);
        if(res != GLOBUS_SUCCESS)
        {
            fire_cb = GLOBUS_TRUE;
        }
    }

    if(fire_cb)
    {
        /* if a temp iovec struct was used for fullfulling waitfor,
          we can free it now */
        if(my_op->_op_ent_fake_iovec != NULL)
        {
            globus_free(my_op->_op_ent_fake_iovec);
            my_op->_op_ent_fake_iovec = NULL;
        }

        if(my_op->prev_ndx == 0 && !op->blocking && op->_op_handle)
        {
            space = op->_op_handle->space;
        }
        op->cached_obj = GlobusXIOResultToObj(res);
        globus_assert(my_op->type == GLOBUS_XIO_OPERATION_TYPE_READ);
        if(my_op->in_register || space != GLOBUS_CALLBACK_GLOBAL_SPACE)
        {
            /* if this is a blocking op, we avoid the oneshot by delaying the
             * finish until the stack unwinds
             */
            if(op->blocking && 
                GlobusXIOBlockedThreadMatchesCurrentThread(op->blocked_thread))
            {
                GlobusXIODebugDelayedFinish();
                op->finished_delayed = GLOBUS_TRUE;
            }
            else
            {
                GlobusXIODebugInregisterOneShot();
                globus_i_xio_register_oneshot(
                    op->_op_handle,
                    globus_l_xio_driver_op_read_kickout,
                    (void *)op,
                    space);
            }
        }
        else
        {
            globus_l_xio_driver_op_read_kickout(op);
        }
    }
    GlobusXIODebugInternalExit();
}

static
void
globus_l_xio_pass_pending_reads(
    globus_i_xio_context_entry_t *      my_context)
{
    globus_i_xio_context_t *            context;
    globus_bool_t                       destroy_context;
    globus_i_xio_op_t *                 op;
    GlobusXIOName(globus_l_xio_pass_pending_reads);

    GlobusXIODebugInternalEnter();
    
    context = my_context->whos_my_daddy;
    destroy_context = GLOBUS_FALSE;
    
    globus_mutex_lock(&context->mutex);
    
    /* this holds a reference on read operations to prevent any eofs from
     * being delivered before I can dump this pending queue into the eof
     * queue
     */
    my_context->read_operations++;
    context->ref++;
    while(my_context->pending_reads > 0)
    {
        /* I hold an outstanding read operation so it shouldn't be possible
         * to enter EOF_DELIVERED here
         */
        globus_assert(my_context->state !=
            GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED);
        
        if(my_context->state == GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED ||
            my_context->state == 
                GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING)
        {
            /* one of the reads we just dispatched caused an eof, dump
             * the remaining in the eof queue
             */
            GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO,
                (_XIOSL("[%s]: Dumping pending queue into eof list\n"), _xio_name));
            do
            {
                my_context->pending_reads--;
                op = (globus_i_xio_op_t *)
                    globus_fifo_dequeue(&my_context->pending_read_queue);
                op->cached_obj = GlobusXIOErrorObjEOF();
                globus_list_insert(&my_context->eof_op_list, op);
                my_context->eof_operations++;
            } while(my_context->pending_reads > 0);
            
            op = NULL;
        }
        else
        {
            my_context->pending_reads--;
            op = (globus_i_xio_op_t *)
                globus_fifo_dequeue(&my_context->pending_read_queue);
            
            my_context->read_operations++;
            op->ref++; /* for the pass */
        }
        
        if(op)
        {
            globus_i_xio_op_entry_t *   my_op;
            globus_i_xio_context_entry_t * next_context;
            globus_result_t             res;
            globus_bool_t               destroy_handle;
            
            globus_mutex_unlock(&context->mutex);
            
            my_op = &op->entry[op->ndx - 1];
            next_context = &context->entry[op->ndx - 1];
            
            my_op->in_register = GLOBUS_TRUE;
                
            if(op->canceled)
            {
                res = GlobusXIOErrorCanceled();
            }
            else
            {
                res = next_context->driver->read_func(
                    next_context->driver_handle,
                    my_op->_op_ent_iovec,
                    my_op->_op_ent_iovec_count,
                    op);
            }
            
            if(res != GLOBUS_SUCCESS)
            {
                GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO,
                    (_XIOSL("[%s]: Pending read failed, finishing now\n"), _xio_name));
                
                globus_xio_driver_finished_read(op, res, 0);
            }
            
            my_op->in_register = GLOBUS_FALSE;
            
            globus_mutex_lock(&context->mutex);
                
            GlobusXIOOpDec(op);
            if(op->ref == 0)
            {
                globus_i_xio_op_destroy(op, &destroy_handle);
                globus_assert(!destroy_handle);
            }
        }
    }
    
    /* remove read operation reference and purge eofs if there are no
     * outstanding reads
     */
    my_context->read_operations--;
    if(my_context->read_operations == 0 &&
        (my_context->state == GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED ||
            my_context->state ==
                GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING))
    {
        globus_l_xio_driver_purge_read_eof(my_context);
    }
    
    context->ref--;
    if(context->ref == 0)
    {
        destroy_context = GLOBUS_TRUE;
    }
    globus_mutex_unlock(&context->mutex);
    
    if(destroy_context)
    {
        globus_i_xio_context_destroy(context);
    }
    
    GlobusXIODebugInternalExit();
}

void
globus_xio_driver_read_delivered(
    globus_xio_operation_t              op,
    int                                 in_ndx,
    globus_xio_operation_type_t *       deliver_type)
{
    globus_i_xio_context_entry_t *      my_context;
    globus_bool_t                       purge_eof;
    globus_bool_t                       dispatch_pending = GLOBUS_FALSE;
    globus_bool_t                       close = GLOBUS_FALSE;
    globus_i_xio_context_t *            context;
    globus_bool_t                       destroy_handle = GLOBUS_FALSE;
    globus_i_xio_handle_t *             handle;
    GlobusXIOName(globus_xio_driver_read_delivered);

    GlobusXIODebugInternalEnter();

    context = op->_op_context;
    my_context = &context->entry[in_ndx];
    handle = op->_op_handle;

    globus_mutex_lock(&context->mutex);
    {
        /* make sure it only gets delivered once */
        if(deliver_type == NULL || 
            *deliver_type == GLOBUS_XIO_OPERATION_TYPE_FINISHED)
        {
            GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE,
                (_XIOSL("[%s] : Already delivered\n"), _xio_name));
            GlobusXIOOpDec(op);
            if(op->ref == 0)
            {
                globus_i_xio_op_destroy(op, &destroy_handle);
            }
            globus_mutex_unlock(&context->mutex);
            goto exit;
        }
        *deliver_type = GLOBUS_XIO_OPERATION_TYPE_FINISHED;
        op->entry[in_ndx].deliver_type = NULL;

        GlobusXIOOpDec(op);
        if(op->ref == 0)
        {
            globus_i_xio_op_destroy(op, &destroy_handle);
        }
        purge_eof = GLOBUS_FALSE;
        if(my_context->read_operations == 0)
        {
            /* just delivered an eof op */
            switch(my_context->state)
            {
                case GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED:
                    purge_eof = GLOBUS_TRUE;
                    GlobusXIOContextStateChange(my_context,
                        GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED);
                    break;

                case GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING:
                    purge_eof = GLOBUS_TRUE;
                    GlobusXIOContextStateChange(my_context,
                        GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED_AND_CLOSING);
                    break;

                case GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED_AND_CLOSING:
                case GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED:
                    break;

                default:
                    globus_assert(0);
            }
            
            my_context->eof_operations--;
            if(my_context->eof_operations == 0)
            {
                GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO,
                    (_XIOSL("[%s]: All eof ops delivered\n"), _xio_name));
                    
                if(my_context->state == GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED)
                {
                    GlobusXIOContextStateChange(my_context,
                        GLOBUS_XIO_CONTEXT_STATE_OPEN);
                }
                
                if(my_context->pending_reads > 0)
                {
                    dispatch_pending = GLOBUS_TRUE;
                }
            }
        }
        else
        {
            my_context->read_operations--;
            /* if no more read operations are outstanding and we are waiting
             * on EOF, purge eof list */
            if(my_context->read_operations == 0 &&
               (my_context->state ==
                    GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED ||
                my_context->state ==
                    GLOBUS_XIO_CONTEXT_STATE_EOF_RECEIVED_AND_CLOSING))
            {
                purge_eof = GLOBUS_TRUE;
            }
        }

        my_context->outstanding_operations--;
        if(purge_eof)
        {
             globus_l_xio_driver_purge_read_eof(my_context);
        }

        GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE,
            (_XIOSL("[%s] : Context @ 0x%x State=%d Count=%d close_start=%d\n"),
            _xio_name, my_context, my_context->state,
            my_context->outstanding_operations,
            my_context->close_started));
        globus_assert(my_context->outstanding_operations >= 0);

        if((my_context->state == GLOBUS_XIO_CONTEXT_STATE_CLOSING ||
            my_context->state ==
                GLOBUS_XIO_CONTEXT_STATE_EOF_DELIVERED_AND_CLOSING) &&
            my_context->outstanding_operations == 0 &&
            !my_context->close_started)
        {
            close = GLOBUS_TRUE;
            my_context->close_started = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&context->mutex);
    
    if(dispatch_pending)
    {
        globus_l_xio_pass_pending_reads(my_context);
    }
    
    if(close)
    {
        globus_i_xio_driver_start_close(my_context->close_op, GLOBUS_FALSE);
    }

  exit:
    if(destroy_handle)
    {
        globus_i_xio_handle_destroy(handle);
    }

    GlobusXIODebugInternalExit();
}


/************************************************************************
 *                          accept
 *                          ------
 ***********************************************************************/

globus_result_t
globus_xio_driver_pass_accept(
    globus_xio_operation_t              in_op,
    globus_xio_driver_callback_t        in_cb,
    void *                              in_user_arg)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_server_t *             server;
    globus_i_xio_server_entry_t *       my_server;
    globus_i_xio_op_entry_t *           my_op;
    int                                 prev_ndx;
    globus_result_t                     res;
    globus_xio_driver_t                 driver;
    GlobusXIOName(globus_xio_driver_pass_accept);

    GlobusXIODebugInternalEnter();
    op = (globus_i_xio_op_t *)(in_op);
    globus_assert(op->ndx < op->stack_size);
    server = op->_op_server;
    op->progress = GLOBUS_TRUE;
    op->block_timeout = GLOBUS_FALSE;

    if(op->canceled)
    {
        GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE,
            (_XIOSL("[%s] :Operation canceled\n"), _xio_name));
        res = GlobusXIOErrorCanceled();
    }
    else
    {
        prev_ndx = op->ndx;
        do
        {
            my_op = &op->entry[op->ndx];
            my_server = &server->entry[op->ndx];
            driver = my_server->driver;
            op->ndx++;
        }
        while(driver->server_accept_func == NULL);

        my_op->type = GLOBUS_XIO_OPERATION_TYPE_ACCEPT;
        my_op->cb = (in_cb);
        my_op->user_arg = (in_user_arg);
        my_op->prev_ndx = (prev_ndx);
        my_op->in_register = GLOBUS_TRUE;

        res = driver->server_accept_func(
                    my_server->server_handle,
                    op);
        my_op->in_register = GLOBUS_FALSE;
        
        if(res == GLOBUS_SUCCESS && prev_ndx == 0)
        {
            while(op->finished_delayed)
            {
                /* reuse this blocked thread to finish the operation */
                op->finished_delayed = GLOBUS_FALSE;
                globus_l_xio_driver_op_accept_kickout(op);
            }
        }
    }
    GlobusXIODebugInternalExit();

    return res;
}


void
globus_xio_driver_finished_accept(
    globus_xio_operation_t              in_op,
    void *                              in_link,
    globus_result_t                     in_res)
{
    globus_i_xio_op_t *                 op;
    globus_i_xio_op_entry_t *           my_op;
    globus_callback_space_t             space =
                            GLOBUS_CALLBACK_GLOBAL_SPACE;
    GlobusXIOName(globus_xio_driver_finished_accept);

    GlobusXIODebugInternalEnter();
    op = (globus_i_xio_op_t *)(in_op);
    globus_assert(op->ndx > 0);
    op->progress = GLOBUS_TRUE;
    op->block_timeout = GLOBUS_FALSE;

    my_op = &op->entry[op->ndx - 1];
    op->cached_obj = GlobusXIOResultToObj((in_res));

    my_op->link = (in_link);

    if(my_op->prev_ndx == 0 && !op->blocking)
    {
        space = op->_op_server->space;
    }
    if(my_op->in_register || space != GLOBUS_CALLBACK_GLOBAL_SPACE)
    {
        /* if this is a blocking op, we avoid the oneshot by delaying the
         * finish until the stack unwinds
         */
        if(op->blocking && 
            GlobusXIOBlockedThreadMatchesCurrentThread(op->blocked_thread))
        {
            GlobusXIODebugDelayedFinish();
            op->finished_delayed = GLOBUS_TRUE;
        }
        else
        {
            GlobusXIODebugInregisterOneShot();
            globus_i_xio_register_oneshot(
                NULL,
                globus_l_xio_driver_op_accept_kickout,
                (void *)op,
                space);
        }
    }
    else
    {
        globus_l_xio_driver_op_accept_kickout(op);
    }
    GlobusXIODebugInternalExit();
}

globus_result_t
globus_xio_driver_pass_server_init(
    globus_xio_operation_t              op,
    const globus_xio_contact_t *        contact_info,
    void *                              driver_server)
{
    globus_i_xio_server_t *             server;
    globus_result_t                     res;
    GlobusXIOName(globus_xio_driver_pass_server_init);
    
    GlobusXIODebugInternalEnter();
    server = op->_op_server;
    op->progress = GLOBUS_TRUE;
    op->block_timeout = GLOBUS_FALSE;
    if(op->ndx < op->stack_size)
    {
        server->entry[op->ndx].server_handle = driver_server;
    }
    
    while(--op->ndx >= 0 &&
        server->entry[op->ndx].driver->server_init_func == NULL)
    { }
    
    if(op->ndx >= 0)
    {
        res = server->entry[op->ndx].driver->server_init_func(
            op->entry[op->ndx].open_attr,
            contact_info,
            op);
    }
    else
    {
        res = globus_xio_contact_info_to_string(
            contact_info, &server->contact_string);
    }
    
    GlobusXIODebugInternalExit();
    
    return res;
}
