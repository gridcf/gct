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
#include <ctype.h>

/*
 *  note:
 *
 *  Cancel Process
 *  --------------
 *  The only exception to when the accept can finish before the callback occurs
 *  is when a cancel happens.  In the case the driver should stop what it is
 *  doing and finish with a canceled error.  All drivers above it will get
 *  this error and should finish in the same manner, by cleaning up resources
 *  involved with this accpet and the calling finish with the cancel error.
 *  Once the error reaches the top xio will find all drivers were not notified
 *  of the cancel and ask them to destroy their links.
 *
 *  Errors
 *  ------
 *  There are two times when an error can happens.  The first is in the 
 *  callback.  The process for dealing with this is exctly as a canceled
 *  accept...  The top callback comes back with an error and all successfully
 *  created driver links are destroyed.
 *
 *  The second case is if the Pass fails.  Once a driver calls pass it must
 *  only return the error code that pass returns.  If that error code is 
 *  GLOBUS_SUCESS then the driver should expect a callback, if it is not 
 *  the driver will not receive the callback for which it registered.  This
 *  rule allows the framework to know that if it receives an error from
 *  pass at the top level that no driver has an outstanding callback.
 *  
 */

#define GlobusIXIOServerDec(free, _in_s)                                \
do                                                                      \
{                                                                       \
    globus_i_xio_server_t *             _s;                             \
                                                                        \
    _s = (_in_s);                                                       \
    _s->ref--;                                                          \
    if(_s->ref == 0)                                                    \
    {                                                                   \
        /* if the handle ref gets down to zero we must be in one        \
         * of the followninf staes.  The statement is that the handle   \
         * only goes away when it is closed or a open fails             \
         */                                                             \
        globus_assert(_s->state == GLOBUS_XIO_SERVER_STATE_CLOSED);     \
        free = GLOBUS_TRUE;                                             \
    }                                                                   \
    else                                                                \
    {                                                                   \
        free = GLOBUS_FALSE;                                            \
    }                                                                   \
} while (0)

globus_list_t *                         globus_i_xio_outstanding_servers_list;

/**************************************************************************
 *                       Internal functions
 *                       ------------------
 *************************************************************************/
static
globus_result_t
globus_l_xio_close_server(
    globus_i_xio_server_t *             xio_server);

static
void
globus_l_xio_server_destroy(
    globus_i_xio_server_t *             xio_server)
{
    GlobusXIOName(globus_l_xio_server_destroy);

    GlobusXIODebugInternalEnter();
    
    globus_mutex_lock(&globus_i_xio_mutex);
    {
        globus_mutex_lock(&xio_server->mutex);
        {
            if(xio_server->sd_monitor != NULL)
            {
                GlobusXIODebugPrintf(
                    GLOBUS_XIO_DEBUG_INFO,
                        (_XIOSL("[globus_i_xio_handle_destroy]"
                        " :: signalling handle unload.\n")));

                xio_server->sd_monitor->count--;
                if(xio_server->sd_monitor->count == 0)
                {
                    globus_cond_signal(&globus_i_xio_cond);
                }
            }
            else
            {
                globus_list_remove(
                    &globus_i_xio_outstanding_servers_list,
                    globus_list_search(
                        globus_i_xio_outstanding_servers_list, xio_server));
            }
        }
        globus_mutex_unlock(&xio_server->mutex);

    }
    globus_mutex_unlock(&globus_i_xio_mutex);
    
    globus_callback_space_destroy(xio_server->space);
    globus_mutex_destroy(&xio_server->mutex);
    if(xio_server->contact_string)
    {
        globus_free(xio_server->contact_string);
    }
    
    globus_free(xio_server);
    
    GlobusXIODebugInternalExit();
}

static
void
globus_i_xio_server_post_accept(
    globus_i_xio_op_t *                 xio_op);

static
void
globus_i_xio_server_will_block_cb(
    globus_thread_callback_index_t      wb_ndx,
    globus_callback_space_t             space,
    void *                              user_args)
{
    globus_i_xio_op_t *                     xio_op;
    GlobusXIOName(globus_i_xio_server_will_block_cb);

    GlobusXIODebugInternalEnter();

    xio_op = (globus_i_xio_op_t *) user_args;

    xio_op->restarted = GLOBUS_TRUE;
    GlobusXIOOpInc(xio_op);

    globus_thread_blocking_callback_disable(&wb_ndx);

    globus_i_xio_server_post_accept(xio_op);

    GlobusXIODebugInternalExit();
}

static
globus_result_t
globus_l_xio_server_handle_create(
    globus_xio_handle_t *               handle,
    globus_xio_server_t                 server)
{
    globus_i_xio_handle_t *             ihandle;
    globus_i_xio_context_t *            context;
    int                                 ndx;
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusXIOName(globus_l_xio_server_handle_create);
    
    GlobusXIODebugInternalEnter();
    
    if(handle == NULL)
    {
        res = GlobusXIOErrorParameter("handle");
        goto error_context;
    }
    /* allocate and initialize context */
    context = globus_i_xio_context_create(server->stack_size);
    if(context == NULL)
    {
        res = GlobusXIOErrorMemory("context");
        goto error_context;
    }
    
    /* allocate and intialize the handle structure */
    ihandle = (globus_i_xio_handle_t *)
        globus_calloc(1, sizeof(globus_i_xio_handle_t));
    if(ihandle == NULL)
    {
        res = GlobusXIOErrorMemory("ihandle");
        goto error_handle;
    }
    /* initialize the handle */
    ihandle->ref = 1; /* itself */
    ihandle->context = context;
    ihandle->state = GLOBUS_XIO_HANDLE_STATE_ACCEPTED;
    ihandle->space = GLOBUS_CALLBACK_GLOBAL_SPACE;
    
    for(ndx = 0; ndx < server->stack_size; ndx++)
    {
        context->entry[ndx].driver = server->entry[ndx].driver;
    }
    
    *handle = ihandle;
    
    GlobusXIODebugInternalExit();
    return GLOBUS_SUCCESS;

error_handle:
    globus_i_xio_context_destroy(context);
    
error_context:
    GlobusXIODebugInternalExitWithError();
    return res;
}

/*
 *  this is the only mechanism for delivering a callback to the user 
 */
void
globus_l_xio_server_accept_kickout(
    void *                              user_arg)
{
    int                                 ctr;
    int                                 wb_ndx;
    globus_i_xio_server_t *             xio_server;
    globus_i_xio_op_t *                 xio_op;
    globus_xio_handle_t                 accepted_handle = NULL;
    globus_result_t                     res;
    globus_bool_t                       destroy_server = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_server_accept_kickout);

    GlobusXIODebugInternalEnter();

    xio_op = (globus_i_xio_op_t *) user_arg;
    xio_server = xio_op->_op_server;
    
    /* create the structure if successful, otherwise the handle is null */
    res = GlobusXIOObjToResult(xio_op->cached_obj);
    if(res == GLOBUS_SUCCESS)
    {
        res = globus_l_xio_server_handle_create(&accepted_handle, xio_server);
        if(res == GLOBUS_SUCCESS)
        {
            /* stick the link in the new handle's context */
            for(ctr = 0;  ctr < xio_op->stack_size; ctr++)
            {
                accepted_handle->context->entry[ctr].driver_handle =
                    xio_op->entry[ctr].link;
            }
            
            globus_mutex_lock(&globus_i_xio_mutex);
            {
                globus_list_insert(
                    &globus_i_xio_outstanding_handles_list, accepted_handle);
            }
            globus_mutex_unlock(&globus_i_xio_mutex);
        }
    }
    
    /* if failed clean up the operation */
    if(res != GLOBUS_SUCCESS)
    {
        for(ctr = 0; ctr < xio_op->stack_size; ctr++)
        {
            if(xio_op->entry[ctr].link != NULL)
            {
                globus_assert(
                    xio_server->entry[ctr].driver->link_destroy_func != NULL
                    && "If the driver link is non-NULL the link_destroy_func must be defined");
                /* ignore result code.  user should be more interested in
                    result from callback */
                xio_server->entry[ctr].driver->link_destroy_func(
                    xio_op->entry[ctr].link);
            }
        }
    }

    globus_thread_blocking_space_callback_push(
        globus_i_xio_server_will_block_cb,
        (void *) xio_op,
        xio_op->blocking ? GLOBUS_CALLBACK_GLOBAL_SPACE: xio_server->space,
        &wb_ndx);

    /* call the users callback */
    xio_op->_op_accept_cb(
        xio_server,
        accepted_handle,
        res,
        xio_op->user_arg);
    globus_thread_blocking_callback_pop(&wb_ndx);
    if(xio_op->restarted)
    {
        globus_mutex_lock(&xio_server->mutex);
        {
            GlobusXIOOpDec(xio_op);
            if(xio_op->ref == 0)
            {
                /* remove the reference for the op on the server */
                GlobusIXIOServerDec(destroy_server, xio_server);
                globus_free(xio_op);
            }
        }
        globus_mutex_unlock(&xio_server->mutex);
        if(destroy_server)
        {
            globus_l_xio_server_destroy(xio_server);
        }
        return;
    }

    globus_i_xio_server_post_accept(xio_op);

    GlobusXIODebugInternalExit();
}

void
globus_i_xio_server_post_accept(
    globus_i_xio_op_t *                 xio_op)
{
    globus_bool_t                       destroy_server = GLOBUS_FALSE;
    globus_i_xio_server_t *             xio_server;
    GlobusXIOName(globus_i_xio_server_post_accept);

    GlobusXIODebugInternalEnter();

    xio_server = xio_op->_op_server;
    /* if user called register accept in the callback we will be back from
        the completeing state and into the accepting state */
 
    /* lock up and do some clean up */
    globus_mutex_lock(&xio_server->mutex);
    {
        globus_assert(xio_op->state == GLOBUS_XIO_OP_STATE_FINISH_WAITING);

        xio_server->outstanding_operations--;

        switch(xio_server->state)
        {
            case GLOBUS_XIO_SERVER_STATE_COMPLETING:
                xio_server->op = NULL;
                xio_server->state = GLOBUS_XIO_SERVER_STATE_OPEN;
                break;

            case GLOBUS_XIO_SERVER_STATE_CLOSE_PENDING:
                if(xio_server->outstanding_operations == 0)
                {
                    xio_server->state = GLOBUS_XIO_SERVER_STATE_CLOSING;
                    globus_l_xio_close_server(xio_server);
                }
                break;

            case GLOBUS_XIO_SERVER_STATE_CLOSED:
            case GLOBUS_XIO_SERVER_STATE_CLOSING:
                xio_server->op = NULL;
                break;

            /* This can happen if when the callback is called the user
                registers another accept and that accept callback is
                called before the first callback returns.  */
            case GLOBUS_XIO_SERVER_STATE_OPEN:
                break;

            case GLOBUS_XIO_SERVER_STATE_ACCEPTING:
                break;

            default:
                globus_assert(0 && "Unexpected state after accept callback");
                break;
        }
        /* decrement reference for the callback if timeout has happened or
            isn't registered this will go to zero */
        GlobusXIOOpDec(xio_op);
        if(xio_op->ref == 0)
        {
            GlobusIXIOServerDec(destroy_server, xio_server);
            globus_free(xio_op);
        }
    }
    globus_mutex_unlock(&xio_server->mutex);

    if(destroy_server)
    {
        globus_l_xio_server_destroy(xio_server);
    }

    GlobusXIODebugInternalExit();
}

/*
 *  internal top level accept callback
 */
void
globus_i_xio_server_accept_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_i_xio_server_t *             xio_server;
    globus_i_xio_op_t *                 xio_op;
    globus_bool_t                       accept = GLOBUS_TRUE;
    GlobusXIOName(globus_i_xio_server_accept_callback);

    GlobusXIODebugInternalEnter();

    xio_op = op;
    xio_server = xio_op->_op_server;

    globus_mutex_lock(&xio_server->mutex);
    {
        /* if in this state it means that the user either has or is about to
           get a cancel callback.  we must delay the delivery of this
           callback until that returns */
        xio_op->cached_obj = GlobusXIOResultToObj(result);
        if(xio_op->state == GLOBUS_XIO_OP_STATE_TIMEOUT_PENDING)
        {
            accept = GLOBUS_FALSE;
        }
        else
        {
            /* if there is an outstanding accept callback */
            if(xio_op->_op_server_timeout_cb != NULL)
            {
                if(globus_i_xio_timer_unregister_timeout(
                        &globus_i_xio_timeout_timer, xio_op))
                {
                    GlobusXIOOpDec(xio_op);
                    globus_assert(xio_op->ref > 0);
                }
            }
        }
        xio_op->state = GLOBUS_XIO_OP_STATE_FINISH_WAITING;

        switch(xio_server->state)
        {
            case GLOBUS_XIO_SERVER_STATE_ACCEPTING:
                xio_server->state = GLOBUS_XIO_SERVER_STATE_COMPLETING;
                break;

            /* nothing to do for this case */
            case GLOBUS_XIO_SERVER_STATE_CLOSE_PENDING:
                break;

            default:
                globus_assert(0);
        }
    }
    globus_mutex_unlock(&xio_server->mutex);

    /* we may be delaying the callback until cancel returns */
    if(accept)
    {
        globus_l_xio_server_accept_kickout((void *)xio_op);
    }

    GlobusXIODebugInternalExit();
}

static
globus_bool_t
globus_l_xio_accept_timeout_callback(
    void *                              user_arg)
{
    globus_i_xio_op_t *                 xio_op;
    globus_i_xio_server_t *             xio_server;
    globus_bool_t                       rc;
    globus_bool_t                       cancel;
    globus_bool_t                       accept = GLOBUS_FALSE;
    globus_bool_t                       timeout = GLOBUS_FALSE;
    globus_bool_t                       destroy_server = GLOBUS_FALSE;
    globus_callback_space_t             space =
        GLOBUS_CALLBACK_GLOBAL_SPACE;
    GlobusXIOName(globus_l_xio_accept_timeout_callback);

    GlobusXIODebugInternalEnter();

    xio_op = (globus_i_xio_op_t *) user_arg;
    xio_server = xio_op->_op_server;

    globus_mutex_lock(&xio_server->mutex);
    {
        switch(xio_op->state)
        {
            /* 
             * this case happens when a serverwas successfully created but 
             * we were unable to unregister the callback and when the first
             * pass fails and we are unable to cancel the timeout callback
             */
            case GLOBUS_XIO_OP_STATE_FINISHED:
            case GLOBUS_XIO_OP_STATE_FINISH_WAITING:

                /* decerement the reference for the timeout callback */
                GlobusXIOOpDec(xio_op);
                if(xio_op->ref == 0)
                {
                    /* remove the reference for the op on the server */
                    GlobusIXIOServerDec(destroy_server, xio_server);
                    globus_free(xio_op);
                }

                /* remove it from the timeout list */
                rc = GLOBUS_TRUE;
                break;

            /* this case happens when we actually want to cancel the operation
                The timeout code should insure that prograess is false if this
                gets called in this state */
            case GLOBUS_XIO_OP_STATE_OPERATING:
                /* it is up to the timeout callback to set this to true */
                rc = GLOBUS_FALSE;
                /* cancel the sucker */
                globus_assert(xio_op->_op_server_timeout_cb != NULL);

                if(!xio_op->block_timeout)
                {
                    timeout = GLOBUS_TRUE;
                    /* we don't need to cache the server object in local stack
                       because state insures that it will not go away */
                    /* put in canceling state to delay the accept callback */
                    xio_op->state = GLOBUS_XIO_OP_STATE_TIMEOUT_PENDING;
                }
                break;

            /* fail on any ohter case */
            default:
                globus_assert(0);
        }
    }
    globus_mutex_unlock(&xio_server->mutex);

    /* if in cancel state, verfiy with user that they want to cancel */
    if(timeout)
    {
        cancel = xio_op->_op_server_timeout_cb(
                    xio_server, xio_op->type);
    }
    /* all non time out casses can just return */
    else
    {
        if(destroy_server)
        {
            globus_l_xio_server_destroy(xio_server);
        }
        goto exit;
    }

    globus_mutex_lock(&xio_server->mutex);
    {
        /* if canceling set the res and we will remove this timer event */
        if(cancel)
        {
            rc = GLOBUS_TRUE;
            /* Assume all timeouts originate from user */
            xio_op->canceled = 1;
            if(xio_op->cancel_cb)
            {
                globus_i_xio_op_entry_t * my_op;
                my_op = &xio_op->entry[xio_op->ndx - 1];
                my_op->in_register = GLOBUS_TRUE;
                xio_op->cancel_cb(
                    xio_op, xio_op->cancel_arg, GLOBUS_XIO_ERROR_TIMEOUT);
                my_op->in_register = GLOBUS_FALSE;
            }            
        }

        /* if an accept callback has already arriverd set flag to later
            call accept callback and set rc to remove timed event */
        if(xio_op->state == GLOBUS_XIO_OP_STATE_FINISH_WAITING)
        {
            accept = GLOBUS_TRUE;
            rc = GLOBUS_TRUE;
        }
        /* if no accept is waiting, set state back to operating */
        else
        {
            xio_op->state = GLOBUS_XIO_OP_STATE_OPERATING;
        }

        /* if we are remvoing the timed event */
        if(rc)
        {
            /* decremenet the op reference count and insist that it is
               not zero yet */
            xio_op->_op_handle_timeout_cb = NULL;
            GlobusXIOOpDec(xio_op);
            globus_assert(xio_op->ref > 0);
        }
    }
    globus_mutex_unlock(&xio_server->mutex);

    /* if the accpet was pending we must call it */
    if(accept)
    {
        if(!xio_op->blocking)
        {
            space = xio_server->space;
        }
        if(space != GLOBUS_CALLBACK_GLOBAL_SPACE)
        {
            /* register a oneshot callback */
            globus_i_xio_register_oneshot(
                NULL,
                globus_l_xio_server_accept_kickout,
                (void *)xio_op,
                space);
        }
        /* in all other cases we can just call callback */
        else
        {
            globus_l_xio_server_accept_kickout((void *)xio_op);
        }
    }

  exit:

    GlobusXIODebugInternalExit();
    return rc;
}



void
globus_l_xio_server_close_kickout(
    void *                              user_arg)
{
    globus_i_xio_server_t *             xio_server;
    globus_bool_t                       destroy_server = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_server_close_kickout);
    
    GlobusXIODebugInternalEnter();
    xio_server = (globus_i_xio_server_t *) user_arg;
    
    if(xio_server->cb)
    {
        xio_server->cb(xio_server, xio_server->user_arg);
    }

    globus_mutex_lock(&xio_server->mutex);
    {
        xio_server->state = GLOBUS_XIO_SERVER_STATE_CLOSED;
        /* dec reference count then free.  this makes sure we don't
           free while in a user callback */
        GlobusIXIOServerDec(destroy_server, xio_server);
    }
    globus_mutex_unlock(&xio_server->mutex);

    if(destroy_server)
    {
        globus_l_xio_server_destroy(xio_server);
    }
    
    GlobusXIODebugInternalExit();
}
/*
 *  this is called locked
 */
globus_result_t
globus_l_xio_close_server(
    globus_i_xio_server_t *             xio_server)
{
    int                                 ctr;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_result_t                     tmp_res;
    globus_callback_space_t             space = 
                                                GLOBUS_CALLBACK_GLOBAL_SPACE;
    GlobusXIOName(globus_l_xio_close_server);
    GlobusXIODebugInternalEnter();
    
    for(ctr = 0; ctr < xio_server->stack_size; ctr++)
    {
        if(xio_server->entry[ctr].driver->server_destroy_func != NULL)
        {
            /* possible to lose a driver res, but you know.. so what? */
            tmp_res = xio_server->entry[ctr].driver->server_destroy_func(
                        xio_server->entry[ctr].server_handle);
            if(tmp_res != GLOBUS_SUCCESS)
            {
                res = GlobusXIOErrorWrapFailed("server_destroy", tmp_res);
            }
        }
    }

    if(!xio_server->blocking)
    {
        space = xio_server->space;
    }
    globus_i_xio_register_oneshot(
        NULL,
        globus_l_xio_server_close_kickout,
        (void *)xio_server,
        space);
    
    GlobusXIODebugInternalExit();
    return res;
}

void
globus_l_xio_server_close_cb(
    globus_xio_server_t                 server,
    void *                              user_arg)
{
    globus_i_xio_blocking_t *           info;

    info = (globus_i_xio_blocking_t *) user_arg;

    globus_mutex_lock(&info->mutex);
    {
        info->done = GLOBUS_TRUE;
        globus_cond_signal(&info->cond);
    }
    globus_mutex_unlock(&info->mutex);
}

void
globus_l_server_accept_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_i_xio_blocking_t *           info;
    GlobusXIOName(globus_l_xio_server_register_accept);

    GlobusXIODebugInternalEnter();
    
    info = (globus_i_xio_blocking_t *) user_arg;

    globus_mutex_lock(&info->mutex);
    {
        info->error_obj = GlobusXIOResultToObj(result);
        info->accepted_handle = handle;
        info->done = GLOBUS_TRUE;
        globus_cond_signal(&info->cond);
    }
    globus_mutex_unlock(&info->mutex);
    
    GlobusXIODebugInternalExit();
}


globus_result_t
globus_l_xio_server_register_accept(
    globus_i_xio_op_t *                 xio_op)
{
    globus_i_xio_server_t *             xio_server;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_bool_t                       free_server = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_server_register_accept);

    GlobusXIODebugInternalEnter();

    xio_server = xio_op->_op_server;
    globus_mutex_lock(&xio_server->mutex);
    {
        if(xio_server->state != GLOBUS_XIO_SERVER_STATE_OPEN &&
           xio_server->state != GLOBUS_XIO_SERVER_STATE_COMPLETING)
        {
            res = GlobusXIOErrorInvalidState(xio_server->state);
            goto state_err;
        }

        xio_server->state = GLOBUS_XIO_SERVER_STATE_ACCEPTING;
        xio_server->outstanding_operations++;

        xio_op->type = GLOBUS_XIO_OPERATION_TYPE_ACCEPT;
        xio_op->state = GLOBUS_XIO_OP_STATE_OPERATING;
        xio_op->ref = 1;
        xio_op->cancel_cb = NULL;
        xio_op->canceled = 0;
        xio_op->progress = GLOBUS_TRUE;
        xio_op->_op_server_timeout_cb = xio_server->accept_timeout;
        xio_op->ndx = 0;
        xio_op->stack_size = xio_server->stack_size;
        xio_op->entry[0].prev_ndx = -1;

        xio_server->op = xio_op;

        /*i deal with timeout if there is one */
        if(xio_op->_op_server_timeout_cb != NULL)
        {
            GlobusXIOOpInc(xio_op);
            globus_i_xio_timer_register_timeout(
                &globus_i_xio_timeout_timer,
                xio_op,
                &xio_op->progress,
                globus_l_xio_accept_timeout_callback,
                &xio_server->accept_timeout_period);
        }

        /* add a reference to the server for the op */
        xio_server->ref++;
    }
    globus_mutex_unlock(&xio_server->mutex);

    /* add reference count for the pass.  does not need to be done locked
       since no one has op until it is passed  */
    GlobusXIOOpInc(xio_op);
    res = globus_xio_driver_pass_accept(xio_op,
            globus_i_xio_server_accept_callback, NULL);

    if(res != GLOBUS_SUCCESS)
    {
        goto err;
    }

    globus_mutex_lock(&xio_server->mutex);
    {
        GlobusXIOOpDec(xio_op);
        if(xio_op->ref == 0)
        {
            GlobusIXIOServerDec(free_server, xio_server);
            globus_assert(!free_server);
            globus_free(xio_op);
        }
    }
    globus_mutex_unlock(&xio_server->mutex);

    GlobusXIODebugInternalExit();
    return GLOBUS_SUCCESS;

  err:

    globus_mutex_lock(&xio_server->mutex);
    {
        GlobusXIOOpDec(xio_op); /* dec for the register */
        globus_assert(xio_op->ref > 0);

        /* set op to invalid type */
        xio_op->state = GLOBUS_XIO_OP_STATE_FINISHED;

        /* if a timeout was registered we must unregister it */
        if(xio_op->_op_server_timeout_cb != NULL)
        {
            if(globus_i_xio_timer_unregister_timeout(
                    &globus_i_xio_timeout_timer, xio_op))
            {
                GlobusXIOOpDec(xio_op);
                globus_assert(xio_op->ref > 0);
            }
        }
        
        /* we dont dec the op here because its owned by the caller */
        GlobusIXIOServerDec(free_server, xio_server);
        globus_assert(!free_server);
    }
  state_err:
    globus_mutex_unlock(&xio_server->mutex);


    GlobusXIODebugInternalExitWithError();
    return res;
}


/**************************************************************************
 *                         API functions
 *                         -------------
 *************************************************************************/

/*
 *  initialize a server structure
 */
globus_result_t
globus_xio_server_create(
    globus_xio_server_t *               server,
    globus_xio_attr_t                   server_attr,
    globus_xio_stack_t                  stack)
{
    globus_list_t *                     list;
    globus_i_xio_server_t *             xio_server = NULL;
    globus_result_t                     res;
    int                                 ctr;
    int                                 stack_size;
    void *                              ds_attr = NULL;
    globus_i_xio_op_t *                 xio_op = NULL;
    globus_xio_contact_t                contact_info;
    GlobusXIOName(globus_xio_server_create);

    GlobusXIODebugEnter();
    if(server == NULL)
    {
        res = GlobusXIOErrorParameter("server");
        goto err_parm;
    }
    if(stack == NULL)
    {
        res = GlobusXIOErrorParameter("stack");
        goto err_parm;
    }
    if(globus_list_empty(stack->driver_stack))
    {
        res = GlobusXIOErrorParameter("stack is empty");
        goto err_parm;
    }
    
    /* take what the user stack has at the time of registration */
    stack_size = globus_list_size(stack->driver_stack);
    xio_op = (globus_i_xio_op_t *)
        globus_calloc(1, sizeof(globus_i_xio_op_t) + 
            (sizeof(globus_i_xio_op_entry_t) * (stack_size - 1)));
    if(xio_op == NULL)
    {
        res = GlobusXIOErrorMemory("operation");
        goto err;
    }

    xio_server = (globus_i_xio_server_t *)
        globus_calloc(1, sizeof(globus_i_xio_server_t) +
            (sizeof(globus_i_xio_server_entry_t) * (stack_size - 1)));
    if(xio_server == NULL)
    {
        res = GlobusXIOErrorMemory("server");
        goto err;
    }

    xio_server->stack_size = globus_list_size(stack->driver_stack);
    xio_server->ref = 1;
    xio_server->state = GLOBUS_XIO_SERVER_STATE_OPEN;
    xio_server->space = GLOBUS_CALLBACK_GLOBAL_SPACE;
    globus_mutex_init(&xio_server->mutex, NULL);
    xio_server->accept_timeout = NULL;

    /* timeout handling */
    if(server_attr != NULL)
    {
        if(server_attr->accept_timeout_cb)
        {
            xio_server->accept_timeout = server_attr->accept_timeout_cb;
            GlobusTimeReltimeCopy(
                xio_server->accept_timeout_period,
                server_attr->accept_timeout_period);
        }
        
        xio_server->space = server_attr->space;
    }
    globus_callback_space_reference(xio_server->space);
    
    /* Only using this op for its index, really... things like state 
     * don't matter to me.  I will also be using the open_attr
     * field for the server attr.
     */
    xio_op->type = GLOBUS_XIO_OPERATION_TYPE_SERVER_INIT;
    xio_op->_op_server = xio_server;
    xio_op->stack_size = xio_server->stack_size;
    
    /* walk through the stack and add each entry to the array */
    ctr = 0;
    for(list = stack->driver_stack;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
        xio_server->entry[ctr].driver = (globus_xio_driver_t)
            globus_list_first(list);

        if(server_attr != NULL)
        {
            GlobusIXIOAttrGetDS(ds_attr, server_attr,               \
                xio_server->entry[ctr].driver);
            xio_op->entry[ctr].open_attr = ds_attr;
        }
        
        ctr++;
    }
    for (xio_op->ndx = 0; xio_op->ndx < xio_op->stack_size; xio_op->ndx++)
    {
        globus_i_xio_driver_t * dp = xio_op->_op_server->entry[xio_op->ndx].driver;
        if (dp->server_pre_init_func)
        {
            res = dp->server_pre_init_func(
                    xio_op->entry[xio_op->ndx].open_attr, NULL, xio_op);
            if (res != GLOBUS_SUCCESS)
            {
                goto err;
            }
        }
    }
    
    memset(&contact_info, 0, sizeof(contact_info));
    res = globus_xio_driver_pass_server_init(xio_op, &contact_info, NULL);
    if(res != GLOBUS_SUCCESS)
    {
        goto err;
    }
    globus_free(xio_op);
    
    globus_mutex_lock(&globus_i_xio_mutex);
    {
        globus_list_insert(&globus_i_xio_outstanding_servers_list, xio_server);
    }
    globus_mutex_unlock(&globus_i_xio_mutex);

    *server = xio_server;

    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;

  err:
    if(xio_op)
    {
        globus_free(xio_op);
    }
    if(xio_server)
    {
        globus_mutex_destroy(&xio_server->mutex);
        globus_callback_space_destroy(xio_server->space);
        globus_free(xio_server);
    }
    *server = NULL;
err_parm:

    GlobusXIODebugExitWithError();
    return res;
}

globus_result_t
globus_xio_server_get_contact_string(
    globus_xio_server_t                 server,
    char **                             contact_string)
{
    globus_result_t                     res;
    GlobusXIOName(globus_xio_server_get_contact_string);

    GlobusXIODebugEnter();
    if(contact_string == NULL)
    {
        res = GlobusXIOErrorParameter("contact_string");
        goto err;
    }
    *contact_string = NULL;
    if(server == NULL)
    {
        res = GlobusXIOErrorParameter("server");
        goto err;
    }
    
    if(server->contact_string)
    {
        *contact_string = globus_libc_strdup(server->contact_string);
        if(!*contact_string)
        {
            res = GlobusXIOErrorMemory("contact_string");
            goto err;
        }
    }
    
    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;
    
err:
    GlobusXIODebugExitWithError();
    return res;
}

/*
 *
 */
globus_result_t
globus_xio_server_cntl(
    globus_xio_server_t                 server,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    ...)
{
    globus_bool_t                       found = GLOBUS_FALSE;
    int                                 ctr;
    globus_result_t                     res = GLOBUS_SUCCESS;
    va_list                             ap;
    globus_i_xio_server_t *             xio_server;
    GlobusXIOName(globus_xio_server_cntl);

    GlobusXIODebugEnter();
    if(server == NULL)
    {
        res = GlobusXIOErrorParameter("server");
        goto err;
    }

    xio_server = (globus_i_xio_server_t *) server;

    globus_mutex_lock(&xio_server->mutex);
    {
        if(driver == NULL)
        {
            /* do general things */
        }
        else
        {
            for(ctr = 0; 
                !found && ctr < xio_server->stack_size; 
                ctr++)
            {
                if(xio_server->entry[ctr].driver == driver)
                {
                    found = GLOBUS_TRUE;

                    va_start(ap, cmd);
                    res = xio_server->entry[ctr].driver->server_cntl_func(
                            xio_server->entry[ctr].server_handle,
                            cmd,
                            ap);
                    va_end(ap);
                }
            }
            if(!found)
            {
                res = GlobusXIOErrorInvalidDriver(_XIOSL("not found"));
            }
        }
    }
    globus_mutex_unlock(&xio_server->mutex);

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

/*
 *  register an accept
 */
globus_result_t
globus_xio_server_register_accept(
    globus_xio_server_t                 server,
    globus_xio_accept_callback_t        cb,
    void *                              user_arg)
{
    int                                 tmp_size;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_i_xio_server_t *             xio_server;
    globus_i_xio_op_t *                 xio_op = NULL;
    GlobusXIOName(globus_xio_server_register_accept);

    GlobusXIODebugEnter();
    if(server == NULL)
    {
        return GlobusXIOErrorParameter("server");
    }
    
    xio_server = (globus_i_xio_server_t *) server;

    tmp_size = sizeof(globus_i_xio_op_t) + 
                (sizeof(globus_i_xio_op_entry_t) * 
                    (xio_server->stack_size - 1));
    xio_op = (globus_i_xio_op_t *) globus_malloc(tmp_size);

    if(xio_op == NULL)
    {
        res = GlobusXIOErrorMemory("operation");
        goto err;
    }
    memset(xio_op, '\0', tmp_size);

    xio_op->_op_accept_cb = cb;
    xio_op->user_arg = user_arg;
    xio_op->_op_server = xio_server;
    xio_op->stack_size = xio_server->stack_size;

    res = globus_l_xio_server_register_accept(xio_op);
    if(res != GLOBUS_SUCCESS)
    {
        goto err;
    }

    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;

  err:
    if(xio_op != NULL)
    {
        globus_free(xio_op);
    }

    GlobusXIODebugExitWithError();
    return res;
}

/*
 *  cancel the server
 */
globus_result_t
globus_xio_server_cancel_accept(
    globus_xio_server_t                 server)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_i_xio_server_t *             xio_server;
    GlobusXIOName(globus_xio_server_cancel_accept);

    GlobusXIODebugEnter();
    xio_server = (globus_i_xio_server_t *)  server;

    globus_mutex_lock(&xio_server->mutex);
    {
        if(xio_server->state != GLOBUS_XIO_SERVER_STATE_ACCEPTING &&
           xio_server->state != GLOBUS_XIO_SERVER_STATE_COMPLETING)
        {
            res = GlobusXIOErrorInvalidState(xio_server->state);
        }
        else if(xio_server->op->canceled)
        {
            res = GlobusXIOErrorCanceled();
        }
        else
        {
            /* the callback is called locked.  within it the driver is
                allowed limited functionality.  by calling this locked
                can more efficiently pass the operation down the stack */
            /* Cancel originates from user */
            xio_server->op->canceled = 1;
            if(xio_server->op->cancel_cb)
            {
                globus_i_xio_op_entry_t * my_op;
                my_op = &xio_server->op->entry[xio_server->op->ndx - 1];
                my_op->in_register = GLOBUS_TRUE;
                xio_server->op->cancel_cb(xio_server->op,
                    xio_server->op->cancel_arg, GLOBUS_XIO_ERROR_CANCELED);
                my_op->in_register = GLOBUS_FALSE;
            }            
        }
    }
    globus_mutex_unlock(&xio_server->mutex);

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
globus_xio_server_accept(
    globus_xio_handle_t *               out_handle,
    globus_xio_server_t                 server)
{
    int                                 tmp_size;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_i_xio_server_t *             xio_server;
    globus_i_xio_op_t *                 xio_op;
    globus_i_xio_blocking_t *           info;
    GlobusXIOName(globus_xio_server_accept);
    
    GlobusXIODebugEnter();
    if(out_handle == NULL)
    {
        res = GlobusXIOErrorParameter("out_handle");
        goto err;
    }
    *out_handle = NULL;
    if(server == NULL)
    {
        res = GlobusXIOErrorParameter("server");
        goto err;
    }
    
    xio_server = (globus_i_xio_server_t *) server;

    tmp_size = sizeof(globus_i_xio_op_t) +
                (sizeof(globus_i_xio_op_entry_t) *
                    (xio_server->stack_size - 1));
    xio_op = (globus_i_xio_op_t *) globus_malloc(tmp_size);
    if(xio_op == NULL)
    {
        res = GlobusXIOErrorMemory("operation");
        goto err;
    }
    memset(xio_op, '\0', tmp_size);
    
    info = globus_i_xio_blocking_alloc();
    if(info == NULL)
    {
        res = GlobusXIOErrorMemory("internal strucature");
        goto info_alloc_err;
    }

    xio_op->_op_accept_cb = globus_l_server_accept_cb;
    xio_op->user_arg = info;
    xio_op->_op_server = xio_server;
    xio_op->stack_size = xio_server->stack_size;
    xio_op->blocking = GLOBUS_TRUE;
    GlobusXIOCurrentBlockedThread(xio_op->blocked_thread);
    
    res = globus_l_xio_server_register_accept(xio_op);
    if(res != GLOBUS_SUCCESS)
    {
        goto register_error;
    }
    
    globus_mutex_lock(&info->mutex);
    {
        while(!info->done)
        {
            globus_cond_wait(&info->cond, &info->mutex);
        }
    }
    globus_mutex_unlock(&info->mutex);

    if(info->error_obj != NULL)
    {
        res = GlobusXIOObjToResult(info->error_obj);
        globus_i_xio_blocking_destroy(info);
        goto err;
    }
    
    *out_handle = info->accepted_handle;
    globus_i_xio_blocking_destroy(info);

    GlobusXIODebugExit();
    return GLOBUS_SUCCESS;

  register_error:
    globus_i_xio_blocking_destroy(info);
    
  info_alloc_err:
    globus_free(xio_op);

  err:
    GlobusXIODebugExitWithError();
    return res;
}

/* called locked */
globus_result_t
globus_i_xio_server_close(
    globus_xio_server_t                 xio_server,
    globus_xio_server_callback_t        cb,
    void *                              user_arg)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusXIOName(globus_i_xio_server_close);

    GlobusXIODebugInternalEnter();
    
    if(xio_server->state == GLOBUS_XIO_SERVER_STATE_CLOSE_PENDING ||
        xio_server->state == GLOBUS_XIO_SERVER_STATE_CLOSING ||
        xio_server->state == GLOBUS_XIO_SERVER_STATE_CLOSED)
    {
        res = GlobusXIOErrorInvalidState(xio_server->state);
    }
    else
    {
       /* the callback is called locked.  within it the driver is
            allowed limited functionality.  by calling this locked
            can more efficiently pass the operation down the stack */
        if(xio_server->op != NULL)
        {
            /* cancel originates from user */
            xio_server->op->canceled = 1;
            if(xio_server->op->cancel_cb)
            {
                globus_i_xio_op_entry_t * my_op;
                my_op = &xio_server->op->entry[xio_server->op->ndx - 1];
                my_op->in_register = GLOBUS_TRUE;
                xio_server->op->cancel_cb(xio_server->op,
                    xio_server->op->cancel_arg, GLOBUS_XIO_ERROR_CANCELED);
                my_op->in_register = GLOBUS_FALSE;
            }
        }

        xio_server->cb = cb;
        xio_server->user_arg = user_arg;
        switch(xio_server->state)
        {
            case GLOBUS_XIO_SERVER_STATE_ACCEPTING:
            case GLOBUS_XIO_SERVER_STATE_COMPLETING:
                xio_server->state = GLOBUS_XIO_SERVER_STATE_CLOSE_PENDING;
                break;

            case GLOBUS_XIO_SERVER_STATE_OPEN:
                xio_server->state = GLOBUS_XIO_SERVER_STATE_CLOSING;
                globus_l_xio_close_server(xio_server);
                break;

            default:
                globus_assert(0);
        }
    }
    
    GlobusXIODebugInternalExit();
    
    return res;
}

/*
 *  destroy the server
 */
globus_result_t
globus_xio_server_register_close(
    globus_xio_server_t                 server,
    globus_xio_server_callback_t        cb,
    void *                              user_arg)
{
    globus_i_xio_server_t *             xio_server;
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusXIOName(globus_xio_server_register_close);

    GlobusXIODebugEnter();
    if(server == NULL)
    {
        res = GlobusXIOErrorParameter("server");
        goto err;
    }

    xio_server = (globus_i_xio_server_t *) server;

    globus_mutex_lock(&xio_server->mutex);
    {
        if(xio_server->sd_monitor != NULL)
        {
            res = GlobusXIOErrorUnloaded();
        }
        else
        {
            res = globus_i_xio_server_close(xio_server, cb, user_arg);
        }
    }
    globus_mutex_unlock(&xio_server->mutex);

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

/*  
 *  destroy the server
 */ 
globus_result_t
globus_xio_server_close(
    globus_xio_server_t                 server)
{
    globus_i_xio_server_t *             xio_server;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_i_xio_blocking_t *           info;
    GlobusXIOName(globus_xio_server_close);

    GlobusXIODebugEnter();
    if(server == NULL) 
    {
        res = GlobusXIOErrorParameter("server");
        goto err;
    }

    info = globus_i_xio_blocking_alloc();
    if(info == GLOBUS_NULL)
    {
        res = GlobusXIOErrorMemory("internal");
        goto err;
    }
    
    xio_server = (globus_i_xio_server_t *) server;

    globus_mutex_lock(&info->mutex);
    {
        xio_server->blocking = GLOBUS_TRUE;
        
        res = globus_xio_server_register_close(xio_server, 
                globus_l_xio_server_close_cb, info);
        if(res == GLOBUS_SUCCESS)
        {
            while(!info->done)
            {
                globus_cond_wait(&info->cond, &info->mutex);
            }
        }
    }
    globus_mutex_unlock(&info->mutex);
    
    globus_i_xio_blocking_destroy(info);
    
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

void
globus_xio_contact_destroy(
    globus_xio_contact_t *              contact_info)
{
    GlobusXIOName(globus_xio_contact_destroy);

    GlobusXIODebugInternalEnter();

    if (contact_info)
    {
        free(contact_info->unparsed);
        free(contact_info->resource);
        free(contact_info->host);
        free(contact_info->port);
        free(contact_info->scheme);
        free(contact_info->user);
        free(contact_info->pass);
        free(contact_info->subject);
    }
    
    GlobusXIODebugInternalExit();
}

static
void
globus_l_xio_decode_hex(
    char *                              s)
{
    char *                              d;
    char                                t;
    
    if(!s || !(s = strchr(s, '%')))
    {
        return;
    }
    d = s;
    
    while(*s)
    {
        t = *s;
	if(t == '%')
	{
	    if(*(s + 1) == '%')
	    {
	        s++;
	    }
	    else if(isxdigit(*(s + 1)) && isxdigit(*(s + 2)))
	    {
		char                    hexstring[3];
                
		hexstring[0] = *(++s);
		hexstring[1] = *(++s);
		hexstring[2] = 0;

		t = (char) strtol(hexstring, NULL, 16);
	    }
	}
	
	*d = t;
	s++;
	d++;
    }

    *d = 0;
}

/*
 * if enc is NULL, no encoding will be done, string will just be duplicated.
 * If it is non-NULL (but possibly empty) all characters in the
 * string, the '%', and 01-1F and 7F-FF will be encoded with the %hh method.
 * 
 * every string returned by this func must be freed
 */
static
char *
globus_l_xio_encode_hex(
    const char *                        s,
    const char *                        enc)
{
    char *                              e;
    char *                              encoded;
    static char                         hexchars[] = "0123456789ABCDEF";

    if(!s)
    {
        return NULL;
    }
    
    if(!enc)
    {
        return globus_libc_strdup(s);
    }
    
    
    encoded = (char *) globus_malloc((3 * strlen(s) + 1) * sizeof(char));
    if(encoded)
    {
        e = encoded;
        while(*s)
        {
            unsigned                    n = (unsigned) *s;
            
            if(n <= 0x1F || n >= 0x7F || *s == '%' || (*enc && strchr(enc, n)))
            {
                *(e++) = '%';
                *(e++) = hexchars[n >> 4];
                *(e++) = hexchars[n & 15];
                s++;
            }
            else
            {
                *(e++) = *(s++);
            }
        }
        
        *e = 0;
    }
    
    return encoded;
}

/*
 *
 * -unparsed (up till end)
 * 
 * if("://" before "/")
 *     -scheme (up till "://")
 *     if(^ "/")
 *         -path (up till end)
 *     else
 *         if("@" before "/" or "<")
 *             -user (up till ":" or "@")
 *             if(^ ":")
 *                 -pass (up till "@")
 *         if(^ "<")
 *             -subject (up till ">:")
 *         if(^ "[")
 *             -host (up till "]")
 *         else
 *             -host (up till ":" or "/")
 *         if(^ ":")
 *             -port (up till "/")
 *         if(^ "/")
 *             -path (up till end)
 * else
 *     if(^ "file:")
 *         -path (up till end)
 *     else if(":" before end and no invalid host chars)
 *         -host (up till ":")
 *         -port (up till end)
 *     else
 *         -path (up till end)
 * 
 * XXX if its possible for plain filenames to contain a :, it really should be
 * preceded with file:
 */

globus_result_t
globus_xio_contact_parse(
    globus_xio_contact_t *              contact_info,
    const char *                        contact_string)
{
    char *                              working;
    char *                              save = NULL;
    char *                              s;
    char *                              p;
    globus_bool_t                       decode = GLOBUS_TRUE;
    globus_result_t                     result;
    GlobusXIOName(globus_xio_contact_parse);

    GlobusXIODebugInternalEnter();
    
    memset(contact_info, 0, sizeof(globus_xio_contact_t));
    if(contact_string && *contact_string)
    {
        contact_info->unparsed = globus_libc_strdup(contact_string);
        if(!contact_info->unparsed)
        {
            goto error_alloc;
        }
        
        save = globus_libc_strdup(contact_string);
        if(!save)
        {
            goto error_alloc;
        }
        working = save;
        
        /* look for scheme */
        for(s = working; *s && *s != ':' && *s != '/'; s++);
        if(*s == ':' && *(s + 1) == '/' && *(s + 2) == '/')
        {
            *s = 0;
            contact_info->scheme = globus_libc_strdup(working);
            if(!contact_info->scheme)
            {
                goto error_alloc;
            }
            working = s + 3;
            
            if(*working != '/')
            {
                /* look for user:pass */
                for(s = working;    
                    *s && *s != '@' && *s != '<' && *s != '/';
                    s++);
                
                if(*s == '@')
                {
                    p = s + 1;
                    *s = 0;
                    if((s = strchr(working, ':')))
                    {
                        *(s++) = 0;
                        if(*s)
                        {
                            contact_info->pass = globus_libc_strdup(s);
                            if(!contact_info->pass)
                            {
                                goto error_alloc;
                            }
                        }
                    }
                    if(*working)
                    {
                        contact_info->user = globus_libc_strdup(working);
                        if(!contact_info->user)
                        {
                            goto error_alloc;
                        }
                    }
                    working = p;
                }
                
                /* look for subject */
                if(*working == '<')
                {
                    working++;
                    s = strchr(working, '>');
                    if(!s)
                    {
                        result = GlobusXIOErrorContactString(_XIOSL("expecting >"));
                        goto error_format;
                    }
                    *s = 0;
                    
                    if(*working)
                    {
                        contact_info->subject = globus_libc_strdup(working);
                        if(!contact_info->subject)
                        {
                            goto error_alloc;
                        }
                    }
                    
                    working = s + 1;
                    if(*working == ':')
                    {
                        working++;
                    }
                }
                
                /* find host:port */
                if(*working == '[')
                {
                    working++;
                    s = strchr(working, ']');
                    if(!s)
                    {
                        result = GlobusXIOErrorContactString("expecting ]");
                        goto error_format;
                    }
                    *(s++) = 0;
                }
                else
                {
                    for(s = working; *s && *s != ':' && *s != '/'; s++);
                }
                
                if(*s == ':')
                {
                    *(s++) = 0;
                    if((p = strchr(s, '/')))
                    {
                        *p = 0;
                    }
                    
                    if(*s)
                    {
                        contact_info->port = globus_libc_strdup(s);
                        if(!contact_info->port)
                        {
                            goto error_alloc;
                        }
                    }
                    
                    if(p)
                    {
                        s = p + 1;
                    }
                    else
                    {
                        /* no path, just end it here */
                        *s = 0;
                    }
                }
                else if(*s == '/')
                {
                    *(s++) = 0;
                }
                else if(*s)
                {
                    result = GlobusXIOErrorContactString(_XIOSL("expecting : or /"));
                    goto error_format;
                }
                
                if(*working)
                {
                    contact_info->host = globus_libc_strdup(working);
                    if(!contact_info->host)
                    {
                        goto error_alloc;
                    }
                }
                
                working = s;
            }
            else
            {
                working++;
            }
            
            /* copy path portion */
            if(*working)
            {
                contact_info->resource = globus_libc_strdup(working);
                if(!contact_info->resource)
                {
                    goto error_alloc;
                }
            }
        }
        else
        {
            /* see if its file or host:port form */
            if(strncmp(working, "file:", 5) == 0)
            {
                working += 5;
                if(*working)
                {
                    contact_info->resource = globus_libc_strdup(working);
                    if(!contact_info->resource)
                    {
                        goto error_alloc;
                    }
                }
                
                contact_info->scheme = globus_libc_strdup("file");
                if(!contact_info->scheme)
                {
                    goto error_alloc;
                }
            }
            else if((s = strrchr(working, ':')) && !strpbrk(working, "\\/"))
            {
                *(s++) = 0;
                if(*s)
                {
                    contact_info->port = globus_libc_strdup(s);
                    if(!contact_info->port)
                    {
                        goto error_alloc;
                    }
                }
                if(*working == '[')
                {
                    working++;
                    s = strchr(working, ']');
                    if(!s)
                    {
                        result = GlobusXIOErrorContactString(_XIOSL("expecting ]"));
                        goto error_format;
                    }
                    
                    *s = 0;
                }
                if(*working)
                {
                    contact_info->host = globus_libc_strdup(working);
                    if(!contact_info->host)
                    {
                        goto error_alloc;
                    }
                }
            }
            else
            {
                decode = GLOBUS_FALSE;
                contact_info->resource = globus_libc_strdup(working);
                if(!contact_info->resource)
                {
                    goto error_alloc;
                }
                contact_info->scheme = globus_libc_strdup("file");
                if(!contact_info->scheme)
                {
                    goto error_alloc;
                }
            }
        }

        /* don't decode anything that wasn't passed in as part of a url */
        if(decode)
        {
            globus_l_xio_decode_hex(contact_info->resource);
            globus_l_xio_decode_hex(contact_info->host);   
            globus_l_xio_decode_hex(contact_info->port);   
            globus_l_xio_decode_hex(contact_info->scheme); 
            globus_l_xio_decode_hex(contact_info->user);   
            globus_l_xio_decode_hex(contact_info->pass);   
            globus_l_xio_decode_hex(contact_info->subject);
        }

        /* XXX validate some of the fields */
        
        globus_free(save);
    }
    
    GlobusXIODebugInternalExit();
    return GLOBUS_SUCCESS;  
                            
error_alloc:                
    result = GlobusXIOErrorMemory("contact_info");

error_format:
    if(save)
    {
        globus_free(save);
    }
    globus_xio_contact_destroy(contact_info);
    GlobusXIODebugInternalExitWithError();
    return result;
}

/* this is more than enough for the 7 possible fields now
 * be sure to update this if it can get bigger
 */
#define GLOBUS_L_XIO_LAYOUT_SIZE        20

/*
 * if an element within encode_chars is NULL, no encoding will be done for that
 * element.  If it is non-NULL (but possibly empty) all characters in the
 * string, the '%', and 00-1F and 7F-FF will be encoded with the %hh method.   
 */
globus_result_t
globus_xio_contact_info_to_encoded_string(
    const globus_xio_contact_t *        contact_info,
    const globus_xio_contact_t *        encode_chars,
    char **                             contact_string)
{
    globus_bool_t                       path_only = GLOBUS_FALSE;
    globus_bool_t                       host_port_only = GLOBUS_FALSE;
    globus_xio_contact_t                local_info;
    const char *                        layout[GLOBUS_L_XIO_LAYOUT_SIZE];
    int                                 i = GLOBUS_L_XIO_LAYOUT_SIZE;
    GlobusXIOName(globus_xio_contact_info_to_string);

    GlobusXIODebugInternalEnter();
    
    if(encode_chars)
    {
        memset(&local_info, 0, sizeof(local_info));
        local_info.resource = globus_l_xio_encode_hex(
            contact_info->resource, encode_chars->resource);
        local_info.host = globus_l_xio_encode_hex(
            contact_info->host, encode_chars->host);   
        local_info.port = globus_l_xio_encode_hex(
            contact_info->port, encode_chars->port);   
        local_info.scheme = globus_l_xio_encode_hex(
            contact_info->scheme, encode_chars->scheme); 
        local_info.user = globus_l_xio_encode_hex(
            contact_info->user, encode_chars->user);   
        local_info.pass = globus_l_xio_encode_hex(
            contact_info->pass, encode_chars->pass);   
        local_info.subject = globus_l_xio_encode_hex(
            contact_info->subject, encode_chars->subject);
        contact_info = &local_info;
    }

    if(contact_info->resource &&
        !contact_info->scheme && 
        !contact_info->host)
    {
        path_only = GLOBUS_TRUE;
    }
    
    if(contact_info->host && contact_info->port && !contact_info->scheme && 
        !contact_info->resource && !contact_info->user &&
        !contact_info->subject)
    {
        host_port_only = GLOBUS_TRUE;
    }
    
    if(contact_info->resource)
    {
        if(path_only)
        {
            layout[--i] = contact_info->resource;
        }
        else
        {
            if(*contact_info->resource == '/')
            {
                layout[--i] = contact_info->resource + 1;
                layout[--i] = "/%2F";
            }
            else
            {
                layout[--i] = contact_info->resource;
                layout[--i] = "/";
            }
        }
    }
    
    if(contact_info->host)
    {
        if(contact_info->port)
        {
            layout[--i] = contact_info->port;
            layout[--i] = ":";
        }
    
        if(strchr(contact_info->host, ':'))
        {
            layout[--i] = "]";
            layout[--i] = contact_info->host;
            layout[--i] = "[";
        }
        else
        {
            layout[--i] = contact_info->host;
        }
        
        if(contact_info->subject)
        {
            layout[--i] = ">";
            layout[--i] = contact_info->subject;
            layout[--i] = "<";
        }
        
        if(contact_info->user)
        {
            layout[--i] = "@";
            
            if(contact_info->pass)
            {
                layout[--i] = contact_info->pass;
                layout[--i] = ":";
            }
            
            layout[--i] = contact_info->user;
        }
    }
    
    if(contact_info->scheme)
    {
        layout[--i] = "://";
        layout[--i] = contact_info->scheme;
    }
    else if(!path_only && !host_port_only)
    {
        layout[--i] = "xio://";
    }
    
    *contact_string = globus_libc_join(
        &layout[i], GLOBUS_L_XIO_LAYOUT_SIZE - i);
    
    if(encode_chars)
    {
        globus_xio_contact_destroy(&local_info);
    }
    
    GlobusXIODebugInternalExit();
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_xio_contact_info_to_string(
    const globus_xio_contact_t *        contact_info,
    char **                             contact_string)
{
    return globus_xio_contact_info_to_encoded_string(
        contact_info, NULL, contact_string);
}

globus_result_t
globus_xio_contact_info_to_url(
    const globus_xio_contact_t *        contact_info,
    char **                             contact_string)
{
    globus_xio_contact_t                encode_chars;
    
    memset(&encode_chars, 0, sizeof(encode_chars));
    encode_chars.resource = " \"#$&+,:;<=>?@[\\]^`{|}~!*";
    encode_chars.user = "<> @:/\"'#";
    encode_chars.pass = "<> @:/\"'#";
    encode_chars.subject = "<> \"'#";
    
    return globus_xio_contact_info_to_encoded_string(
        contact_info, &encode_chars, contact_string);
}
