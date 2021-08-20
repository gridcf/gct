/*
 * Copyright 1999-2014 University of Chicago
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

#ifdef _WIN32
#include "globus_i_xio_win32.h"

typedef struct globus_l_xio_win32_socket_s
{
    win32_mutex_t                       lock;
    SOCKET                              socket;
    WSAEVENT                            event;
    long                                eventselect;
    /* ready_events is used only for select() functionality
     * note, select functionality is flawed due to winsock close notification
     * bug.  It is possible that a read won't be signaled on a short lived
     * connection.  To minimize the chance of being bit by this bug,
     * initialize ready_events with a read event
     */
    long                                ready_events;
    globus_i_xio_win32_event_entry_t    event_entry;
    globus_i_xio_system_op_info_t *     read_info;
    globus_i_xio_system_op_info_t *     write_info;
} globus_l_xio_win32_socket_t;

static
void
globus_l_xio_win32_socket_kickout(
    void *                              user_arg)
{
    globus_i_xio_system_op_info_t *     op_info;
    SOCKET                              fd;
    GlobusXIOName(globus_l_xio_win32_socket_kickout);

    op_info = (globus_i_xio_system_op_info_t *) user_arg;
    fd = op_info->handle->socket;
    
    GlobusXIOSystemDebugEnterFD(fd);

    if(op_info->op)
    {
        globus_xio_operation_disable_cancel(op_info->op);
    }
    
    switch(op_info->type)
    {
      case GLOBUS_I_XIO_SYSTEM_OP_CONNECT:
      case GLOBUS_I_XIO_SYSTEM_OP_ACCEPT:
        op_info->sop.non_data.callback(
            op_info->error ? globus_error_put(op_info->error) : GLOBUS_SUCCESS,
            op_info->user_arg);
        break;

      default:
        op_info->sop.data.callback(
            op_info->error ? globus_error_put(op_info->error) : GLOBUS_SUCCESS,
            op_info->nbytes,
            op_info->user_arg);
            
        GlobusIXIOSystemFreeIovec(
            op_info->sop.data.start_iovc,
            op_info->sop.data.start_iov);
        break;
    }
    
    GlobusIXIOSystemFreeOperation(op_info);
    GlobusXIOSystemDebugExitFD(fd);
}

static
void
globus_l_xio_win32_socket_complete(
    globus_i_xio_system_op_info_t *     op_info,
    globus_bool_t                       win32_thread)
{
    SOCKET                              fd;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_win32_socket_complete);

    fd = op_info->handle->socket;
    
    GlobusXIOSystemDebugEnterFD(fd);
    
    if(!op_info->op)
    {
        /* internal usage */
        globus_l_xio_win32_socket_kickout(op_info);
        result = GLOBUS_SUCCESS;
    }
    else if(win32_thread)
    {
        result = globus_i_xio_win32_complete(
            globus_l_xio_win32_socket_kickout, op_info);
    }
    else
    {
        result = globus_callback_register_oneshot(
            0, 0, globus_l_xio_win32_socket_kickout, op_info);
    }
    /* really cant do anything else */
    if(result != GLOBUS_SUCCESS)
    {
        globus_panic(
            GLOBUS_XIO_SYSTEM_MODULE,
            result,
            _XIOSL("[%s:%d] Couldn't register callback"),
            _xio_name,
            __LINE__);
    }
    
    GlobusXIOSystemDebugExitFD(fd);
}

/* must be safe to call from win32 thread
 */
static
void
globus_l_xio_win32_socket_handle_read(
    globus_l_xio_win32_socket_t *       handle,
    globus_i_xio_system_op_info_t *     read_info)
{
    globus_size_t                       nbytes;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_win32_socket_handle_read);
    
    GlobusXIOSystemDebugEnterFD(handle->socket);

    result = GLOBUS_SUCCESS;

    if(read_info->op)
    {
        globus_xio_operation_refresh_timeout(read_info->op);
    }
    
    GlobusXIOSystemDebugPrintf(
        GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
        ("[%s] In globus_l_xio_win32_socket_handle_read fd=%lu, read_info->type=%d\n",
             _xio_name,
             (unsigned long)handle->socket, (int) read_info->type));
    switch(read_info->type)
    {
      case GLOBUS_I_XIO_SYSTEM_OP_ACCEPT:
        {
            SOCKET                      new_fd;

            new_fd = accept(handle->socket, 0, 0);
            
            if(new_fd == INVALID_SOCKET)
            {
                int                     error = WSAGetLastError();
                
                if(error != WSAECONNRESET && error != WSAEWOULDBLOCK)
                {
                    char errbuf[64];
                    sprintf(errbuf, "accept, fd=%ld, error=%d",
                        (long) handle->socket, error);
                    result = GlobusXIOErrorSystemError(errbuf, error);
                    GlobusXIOSystemDebugPrintf(
                        GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                        ("[%s] Tried to accept new connection on fd=%lu, %s\n",
                             _xio_name,
                             (unsigned long)handle->socket, errbuf));

                }
                else
                {
                    char errbuf[64];
                    sprintf(errbuf, "accept, fd=%ld, error=%d",
                        (long) handle->socket, error);
                    GlobusXIOSystemDebugPrintf(
                        GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                        ("[%s] Tried to accept new connection on fd=%lu, %s\n",
                             _xio_name,
                             (unsigned long)handle->socket, errbuf));
                }
            }
            else
            {
                unsigned long           flag = 0;
                
                /* clear inherited attrs */
                WSAEventSelect(new_fd, 0, 0);
                ioctlsocket(new_fd, FIONBIO, &flag);
    
                *read_info->sop.non_data.out_fd = new_fd;
                read_info->nbytes++;
                GlobusXIOSystemDebugPrintf(
                    GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                    ("[%s] Accepted new connection on fd=%lu, fd=%lu\n",
                         _xio_name,
                         (unsigned long)handle->socket, (unsigned long)new_fd));
            }
        }
        break;

      case GLOBUS_I_XIO_SYSTEM_OP_READ:
        /* we loop repeatedly here to read all available data until
         * the read would return EWOULDBLOCK.  at that time, we'll get
         * another event to land us back here.
         * (This looping is necessary to work around a winsock bug where we
         * aren't notified of the peer closing the connection on short lived
         * connections)
         */
        do
        {
            result = globus_i_xio_system_socket_try_read(
                handle->socket,
                read_info->sop.data.iov,
                read_info->sop.data.iovc,
                read_info->sop.data.flags,
                read_info->sop.data.addr,
                &nbytes);
            if(result == GLOBUS_SUCCESS)
            {
                if(nbytes > 0)
                {
                    read_info->nbytes += nbytes;
                    GlobusIXIOUtilAdjustIovec(
                        read_info->sop.data.iov,
                        read_info->sop.data.iovc, nbytes);
                }
                else if(read_info->sop.data.iovc > 0 &&
                    read_info->sop.data.iov[0].iov_len > 0)
                {
                    /* we consumed read event */
                    handle->ready_events &= ~FD_READ;
                }
            }
        } while(nbytes > 0 && read_info->nbytes < read_info->waitforbytes);
        break;

      default:
        globus_assert(0 && "Unexpected type for read operation");
        return;
        break;
    }
    
    if(result != GLOBUS_SUCCESS)
    {
        read_info->error = globus_error_get(result);
    }
    
    /* always true for accept operations */
    if(read_info->nbytes >= read_info->waitforbytes ||
        result != GLOBUS_SUCCESS)
    {
        read_info->state = GLOBUS_I_XIO_SYSTEM_OP_COMPLETE;
    }

    GlobusXIOSystemDebugExitFD(handle->socket);
}

/* must be safe to call from win32 thread
 */
static
void
globus_l_xio_win32_socket_handle_write(
    globus_l_xio_win32_socket_t *       handle,
    globus_i_xio_system_op_info_t *     write_info)
{
    globus_size_t                       nbytes;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_win32_socket_handle_write);
    
    GlobusXIOSystemDebugEnterFD(handle->socket);

    result = GLOBUS_SUCCESS;

    if(write_info->op)
    {
        globus_xio_operation_refresh_timeout(write_info->op);
    }
    
    switch(write_info->type)
    {
      case GLOBUS_I_XIO_SYSTEM_OP_CONNECT:
        {
            int                         err;
            globus_socklen_t            errlen;

            errlen = sizeof(err);
            if(getsockopt(
                handle->socket, SOL_SOCKET, SO_ERROR, (char *)&err, &errlen)
                    == SOCKET_ERROR)
            {
                err = WSAGetLastError();
            }

            if(err)
            {
                result = GlobusXIOErrorSystemError("connect", err);
            }
        }
        break;

      case GLOBUS_I_XIO_SYSTEM_OP_WRITE:
        /* we loop repeatedly here to use up all available space until
         * the write would return EWOULDBLOCK.  at that time, we'll get
         * another event to land us back here
         */
        do
        {
            result = globus_i_xio_system_socket_try_write(
                handle->socket,
                write_info->sop.data.iov,
                write_info->sop.data.iovc,
                write_info->sop.data.flags,
                write_info->sop.data.addr,
                &nbytes);
            if(result == GLOBUS_SUCCESS)
            {
                if(nbytes > 0)
                {
                    write_info->nbytes += nbytes;
                    GlobusIXIOUtilAdjustIovec(
                        write_info->sop.data.iov,
                        write_info->sop.data.iovc, nbytes);
                }
                else if(write_info->sop.data.iovc > 0 &&
                    write_info->sop.data.iov[0].iov_len > 0)
                {
                    /* we consumed write event */
                    handle->ready_events &= ~FD_WRITE;
                }
            }
        } while(nbytes > 0 && write_info->nbytes < write_info->waitforbytes);
        break;

      default:
        globus_assert(0 && "Unexpected type for write operation");
        return;
        break;
    }
  
    if(result != GLOBUS_SUCCESS)
    {
        write_info->error = globus_error_get(result);
    }
    
    /* always true for connect operations */
    if(write_info->nbytes >= write_info->waitforbytes ||
        result != GLOBUS_SUCCESS)
    {
        write_info->state = GLOBUS_I_XIO_SYSTEM_OP_COMPLETE;
    }

    GlobusXIOSystemDebugExitFD(handle->socket);
}

static
void
globus_l_xio_win32_socket_cancel_cb(
    globus_xio_operation_t              op,
    void *                              user_arg,
    globus_xio_error_type_t             reason)
{
    globus_i_xio_system_op_info_t *     op_info;
    GlobusXIOName(globus_l_xio_win32_socket_cancel_cb);

    GlobusXIOSystemDebugEnter();

    op_info = (globus_i_xio_system_op_info_t *) user_arg;
    
    /* this access of the handle is not safe if users destroy it
     * with outstanding callbacks.  I don't think that is allowed, so we
     * should be ok.
     */
    win32_mutex_lock(&op_info->handle->lock);
    {
        if(op_info->state != GLOBUS_I_XIO_SYSTEM_OP_COMPLETE && 
            op_info->state != GLOBUS_I_XIO_SYSTEM_OP_CANCELED)
        {
            op_info->error = reason == GLOBUS_XIO_ERROR_TIMEOUT
                ? GlobusXIOErrorObjTimeout()
                : GlobusXIOErrorObjCanceled();
            
            if(op_info->state == GLOBUS_I_XIO_SYSTEM_OP_NEW)
            {
                op_info->state = GLOBUS_I_XIO_SYSTEM_OP_CANCELED;
                    
                GlobusXIOSystemDebugPrintf(
                    GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                    ("[%s] fd=%lu, Canceling NEW\n",
                        _xio_name, (unsigned long)op_info->handle->socket));
            }
            else
            {
                globus_result_t         result;

                op_info->state = GLOBUS_I_XIO_SYSTEM_OP_COMPLETE;
                
                GlobusXIOSystemDebugPrintf(
                    GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                    ("[%s] fd=%lu, Canceling Pending\n",
                        _xio_name, (unsigned long)op_info->handle->socket));
                
                if(op_info->handle->read_info == op_info)
                {
                    op_info->handle->read_info = 0;
                }
                else
                {
                    globus_assert(op_info->handle->write_info == op_info);
                    op_info->handle->write_info = 0;
                }
                
                /* unregister and kickout now */
                result = globus_callback_register_oneshot(
                    0,
                    0,
                    globus_l_xio_win32_socket_kickout,
                    op_info);
                /* really cant do anything else */
                if(result != GLOBUS_SUCCESS)
                {
                    globus_panic(
                        GLOBUS_XIO_SYSTEM_MODULE,
                        result,
                        _XIOSL("[%s:%d] Couldn't register callback"),
                        _xio_name,
                        __LINE__);
                }
            }
        }
    }
    win32_mutex_unlock(&op_info->handle->lock);

    GlobusXIOSystemDebugExit();
}

/* always called from win32 thread */
static
globus_bool_t
globus_l_xio_win32_socket_event_cb(
    void *                              user_arg)
{
    globus_l_xio_win32_socket_t *       handle;
    WSANETWORKEVENTS                    wsaevents;
    long                                events;
    long                                clearevents = 0;
    globus_i_xio_system_op_info_t *     read_info = 0;
    globus_i_xio_system_op_info_t *     write_info = 0;
    GlobusXIOName(globus_l_xio_win32_socket_event_cb);
    
    handle = (globus_l_xio_win32_socket_t *) user_arg;
    
    GlobusXIOSystemDebugEnterFD(handle->socket);
    
    if(WSAEnumNetworkEvents(
        handle->socket, handle->event, &wsaevents) == SOCKET_ERROR)
    {
        GlobusXIOSystemDebugSysError(
            "WSAEnumNetworkEvents error", WSAGetLastError());
        goto error_enum;
    }
    
    events = wsaevents.lNetworkEvents;
    win32_mutex_lock(&handle->lock);
    {
        GlobusXIOSystemDebugPrintf(
            GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
            ("[%s] Ready events: %s%s%s%s%s fd=%lu\n", _xio_name,
                events & FD_ACCEPT  ? "accept;"     : "",
                events & FD_CONNECT ? "connect;"    : "",
                events & FD_READ    ? "read;"       : "",
                events & FD_WRITE   ? "write;"      : "",
                events & FD_CLOSE   ? "close;"      : "",
                (unsigned long)handle->socket));
        
        if(events & (FD_ACCEPT|FD_READ|FD_CLOSE))
        {
            /* cache for select() like functionality */
            handle->ready_events |= FD_READ;
                
            if(handle->read_info)
            {
                globus_l_xio_win32_socket_handle_read(
                    handle, handle->read_info);
                    
                if(handle->read_info->state == GLOBUS_I_XIO_SYSTEM_OP_COMPLETE)
                {
                    read_info = handle->read_info;
                    handle->read_info = 0;
                }
            }
            else
            {
                clearevents |= FD_ACCEPT|FD_READ;
            }
        }
        
        if(events & (FD_CONNECT|FD_WRITE|FD_CLOSE))
        {
            /* cache for select() like functionality */
            handle->ready_events |= FD_WRITE;
                
            if(handle->write_info)
            {
                globus_l_xio_win32_socket_handle_write(
                    handle, handle->write_info);
                    
                if(handle->write_info->state ==
                    GLOBUS_I_XIO_SYSTEM_OP_COMPLETE)
                {
                    write_info = handle->write_info;
                    handle->write_info = 0;
                }
            }
            else
            {
                clearevents |= FD_CONNECT|FD_WRITE;
            }
        }
        
        /* clear any events we want to ignore */
        if((handle->eventselect & clearevents))
        {
            handle->eventselect &= ~clearevents;
            WSAEventSelect(handle->socket, handle->event, handle->eventselect);
        }
    }
    win32_mutex_unlock(&handle->lock);

    /* do this outside the lock to avoid unnecessary contention */
    if(read_info)
    {
        globus_l_xio_win32_socket_complete(read_info, GLOBUS_TRUE);
    }
    
    if(write_info)
    {
        globus_l_xio_win32_socket_complete(write_info, GLOBUS_TRUE);
    }

    GlobusXIOSystemDebugExitFD(handle->socket);
    return GLOBUS_TRUE;

error_enum:
    GlobusXIOSystemDebugExitWithErrorFD(handle->socket);
    return GLOBUS_TRUE;
}

static
globus_result_t
globus_l_xio_win32_socket_register_read(
    globus_l_xio_win32_socket_t *       handle,
    globus_i_xio_system_op_info_t *     read_info)
{
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_win32_socket_register_read);
    
    GlobusXIOSystemDebugEnterFD(handle->socket);
    
    /* I have to do this outside the lock because of lock inversion issues */
    if(read_info->op && globus_xio_operation_enable_cancel(
        read_info->op, globus_l_xio_win32_socket_cancel_cb, read_info))
    {
        result = GlobusXIOErrorCanceled();
        goto error_cancel_enable;
    }

    win32_mutex_lock(&handle->lock);
    {
        if(read_info->state == GLOBUS_I_XIO_SYSTEM_OP_CANCELED)
        {
            result = globus_error_put(read_info->error);
            goto error_canceled;
        }

        if(handle->read_info)
        {
            result = GlobusXIOErrorAlreadyRegistered();
            goto error_already_registered;
        }
        
        read_info->state = GLOBUS_I_XIO_SYSTEM_OP_PENDING;
        /* if select() functionality requested, only handle after we receive
         * read event
         */
        if(read_info->waitforbytes > 0 || handle->ready_events & FD_READ)
        {
            globus_l_xio_win32_socket_handle_read(handle, read_info);
        }
        
        if(read_info->state != GLOBUS_I_XIO_SYSTEM_OP_COMPLETE)
        {
            /* make sure we're set up to be notified of events */
            long needevents;
            
            if(read_info->type == GLOBUS_I_XIO_SYSTEM_OP_ACCEPT)
            {
                needevents = FD_ACCEPT;
            }
            else
            {
                needevents = FD_READ|FD_CLOSE;
            }
            
            if((handle->eventselect & needevents) != needevents)
            {
                if(WSAEventSelect(
                    handle->socket, handle->event,
                    handle->eventselect|needevents) == SOCKET_ERROR)
                {
                    read_info->error = GlobusXIOErrorObjSystemError(
                        "WSAEventSelect", WSAGetLastError());
                }
                else
                {
                    handle->eventselect |= needevents;
                }
            }
            
            if(!read_info->error)
            {
                handle->read_info = read_info;
                read_info = 0;
            }
        }
    }
    win32_mutex_unlock(&handle->lock);
    
    /* do this outside the lock to avoid unnecessary contention */
    if(read_info)
    {
        globus_l_xio_win32_socket_complete(read_info, GLOBUS_FALSE);
    }
    
    GlobusXIOSystemDebugExitFD(handle->socket);
    return GLOBUS_SUCCESS;

error_already_registered:
error_canceled:
    read_info->state = GLOBUS_I_XIO_SYSTEM_OP_COMPLETE;
    win32_mutex_unlock(&handle->lock);
    if(read_info->op)
    {
        globus_xio_operation_disable_cancel(read_info->op);
    }
error_cancel_enable:
    GlobusXIOSystemDebugExitWithErrorFD(handle->socket);
    return result;
}

static
globus_result_t
globus_l_xio_win32_socket_register_write(
    globus_l_xio_win32_socket_t *       handle,
    globus_i_xio_system_op_info_t *     write_info)
{
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_win32_socket_register_write);
    
    GlobusXIOSystemDebugEnterFD(handle->socket);
    
    /* I have to do this outside the lock because of lock inversion issues */
    if(write_info->op && globus_xio_operation_enable_cancel(
        write_info->op, globus_l_xio_win32_socket_cancel_cb, write_info))
    {
        result = GlobusXIOErrorCanceled();
        goto error_cancel_enable;
    }

    win32_mutex_lock(&handle->lock);
    {
        if(write_info->state == GLOBUS_I_XIO_SYSTEM_OP_CANCELED)
        {
            result = globus_error_put(write_info->error);
            goto error_canceled;
        }

        if(handle->write_info)
        {
            result = GlobusXIOErrorAlreadyRegistered();
            goto error_already_registered;
        }
        
        write_info->state = GLOBUS_I_XIO_SYSTEM_OP_PENDING;
        /* if select() functionality requested, only handle after we receive
         * write event
         * 
         * also dont call for connect operations.  waitforbytes == 0 in this
         * case and handle->ready_events should not yet have a write event
         */
        if(write_info->waitforbytes > 0 || handle->ready_events & FD_WRITE)
        {
            globus_l_xio_win32_socket_handle_write(handle, write_info);
        }
        
        if(write_info->state != GLOBUS_I_XIO_SYSTEM_OP_COMPLETE)
        {
            /* make sure we're set up to be notified of events */
            long needevents;
            
            if(write_info->type == GLOBUS_I_XIO_SYSTEM_OP_CONNECT)
            {
                needevents = FD_CONNECT|FD_CLOSE;
            }
            else
            {
                needevents = FD_WRITE|FD_CLOSE;
            }
            
            if((handle->eventselect & needevents) != needevents)
            {
                if(WSAEventSelect(
                    handle->socket, handle->event,
                    handle->eventselect|needevents) == SOCKET_ERROR)
                {
                    write_info->error = GlobusXIOErrorObjSystemError(
                        "WSAEventSelect", WSAGetLastError());
                }
                else
                {
                    handle->eventselect |= needevents;
                }
            }
            
            if(!write_info->error)
            {
                handle->write_info = write_info;
                write_info = 0;
            }
        }
    }
    win32_mutex_unlock(&handle->lock);
    
    /* do this outside the lock to avoid unnecessary contention */
    if(write_info)
    {
        globus_l_xio_win32_socket_complete(write_info, GLOBUS_FALSE);
    }

    GlobusXIOSystemDebugExitFD(handle->socket);
    return GLOBUS_SUCCESS;

error_already_registered:
error_canceled:
    write_info->state = GLOBUS_I_XIO_SYSTEM_OP_COMPLETE;
    win32_mutex_unlock(&handle->lock);
    if(write_info->op)
    {
        globus_xio_operation_disable_cancel(write_info->op);
    }
error_cancel_enable:
    GlobusXIOSystemDebugExitWithErrorFD(handle->socket);
    return result;
}

globus_result_t
globus_xio_system_socket_init(
    globus_xio_system_socket_handle_t * uhandle,
    globus_xio_system_socket_t          socket,
    globus_xio_system_type_t            type)
{
    globus_result_t                     result;
    globus_l_xio_win32_socket_t *       handle;
    unsigned long                       flag;
    GlobusXIOName(globus_xio_system_socket_init);
    
    GlobusXIOSystemDebugEnterFD(socket);
    
    handle = (globus_l_xio_win32_socket_t *)
        globus_calloc(1, sizeof(globus_l_xio_win32_socket_t));
    if(!handle)
    {
        result = GlobusXIOErrorMemory("handle");
        goto error_alloc;
    }
    
    handle->socket = socket;
    win32_mutex_init(&handle->lock, 0);
    handle->ready_events = FD_READ; /* to avoid winsock fd_close bug */
    
    handle->event = WSACreateEvent();
    if(handle->event == 0)
    {
        result = GlobusXIOErrorSystemError(
            "WSACreateEvent", WSAGetLastError());
        goto error_create;
    }
    
    flag = 1;
    if(ioctlsocket(socket, FIONBIO, &flag) == SOCKET_ERROR)
    {
        result = GlobusXIOErrorSystemError(
            "ioctlsocket", WSAGetLastError());
        goto error_ioctl;
    }
    
    result = globus_i_xio_win32_event_register(
        &handle->event_entry,
        handle->event,
        globus_l_xio_win32_socket_event_cb,
        handle);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_i_xio_win32_event_register", result);
        goto error_register;
    }
    
    GlobusXIOSystemDebugPrintf(
        GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
        ("[%s] Registered event handle=%lu\n",
            _xio_name, (unsigned long) handle->event));
        
    *uhandle = handle;
    
    GlobusXIOSystemDebugExitFD(socket);
    return GLOBUS_SUCCESS;

error_register:
    flag = 0;
    ioctlsocket(socket, FIONBIO, &flag);
error_ioctl:
    WSACloseEvent(handle->event);
error_create:
    win32_mutex_destroy(&handle->lock);
    globus_free(handle);
error_alloc:
    GlobusXIOSystemDebugExitWithErrorFD(socket);
    return result;
}

void
globus_xio_system_socket_destroy(
    globus_xio_system_socket_handle_t   handle)
{
    unsigned long                       flag = 0;
    GlobusXIOName(globus_xio_system_socket_destroy);
    
    GlobusXIOSystemDebugEnterFD(handle->socket);
    
    globus_assert(!handle->read_info && !handle->write_info);
    
    /* no need to ensure entry is still registered, as I always return true
     * in the callback and this is only place i unregister
     */
    globus_i_xio_win32_event_lock(handle->event_entry);
    
    GlobusXIOSystemDebugPrintf(
        GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
        ("[%s] Unregistering event handle=%lu for socket %ld\n",
            _xio_name, (unsigned long) handle->event, (long) handle->socket));
            
    globus_i_xio_win32_event_unregister(handle->event_entry);
    globus_i_xio_win32_event_unlock(handle->event_entry);
    
    WSAEventSelect(handle->socket, 0, 0);
    ioctlsocket(handle->socket, FIONBIO, &flag);
    WSACloseEvent(handle->event);
    win32_mutex_destroy(&handle->lock);
    
    GlobusXIOSystemDebugExitFD(handle->socket);
    globus_free(handle);
}
    
globus_result_t
globus_xio_system_socket_register_connect(
    globus_xio_operation_t              op,
    globus_xio_system_socket_handle_t   handle,
    globus_sockaddr_t *                 addr,
    globus_xio_system_callback_t        callback,
    void *                              user_arg)
{
    globus_result_t                     result;
    int                                 error;
    globus_i_xio_system_op_info_t *     op_info;
    GlobusXIOName(globus_xio_system_socket_register_connect);
    
    GlobusXIOSystemDebugEnterFD(handle->socket);
    
    if(connect(
        handle->socket, (const struct sockaddr *) addr,
        GlobusLibcSockaddrLen(addr)) == SOCKET_ERROR &&
        (error = WSAGetLastError()) != WSAEWOULDBLOCK)
    {
        result = GlobusXIOErrorSystemError("connect", error);
        goto error_connect;
    }

    GlobusIXIOSystemAllocOperation(op_info);
    if(!op_info)
    {
        result = GlobusXIOErrorMemory("op_info");
        goto error_op_info;
    }

    op_info->type = GLOBUS_I_XIO_SYSTEM_OP_CONNECT;
    op_info->state = GLOBUS_I_XIO_SYSTEM_OP_NEW;
    op_info->op = op;
    op_info->handle = handle;
    op_info->user_arg = user_arg;
    op_info->sop.non_data.callback = callback;

    result = globus_l_xio_win32_socket_register_write(handle, op_info);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_win32_socket_register_write", result);
        goto error_register;
    }

    GlobusXIOSystemDebugExitFD(handle->socket);
    return GLOBUS_SUCCESS;

error_register:
    GlobusIXIOSystemFreeOperation(op_info);
error_op_info:
error_connect:
    GlobusXIOSystemDebugExitWithErrorFD(handle->socket);
    return result;
}

globus_result_t
globus_xio_system_socket_register_accept(
    globus_xio_operation_t              op,
    globus_xio_system_socket_handle_t   listener_handle,
    globus_xio_system_socket_t *        out_handle,
    globus_xio_system_callback_t        callback,
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_i_xio_system_op_info_t *     op_info;
    GlobusXIOName(globus_xio_system_socket_register_accept);
    
    GlobusXIOSystemDebugEnterFD(listener_handle->socket);
    
    GlobusIXIOSystemAllocOperation(op_info);
    if(!op_info)
    {
        result = GlobusXIOErrorMemory("op_info");
        goto error_op_info;
    }

    op_info->type = GLOBUS_I_XIO_SYSTEM_OP_ACCEPT;
    op_info->state = GLOBUS_I_XIO_SYSTEM_OP_NEW;
    op_info->op = op;
    op_info->handle = listener_handle;
    op_info->user_arg = user_arg;
    op_info->sop.non_data.callback = callback;
    op_info->sop.non_data.out_fd = out_handle;
    op_info->waitforbytes = 1;

    result = globus_l_xio_win32_socket_register_read(listener_handle, op_info);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_win32_socket_register_read", result);
        goto error_register;
    }

    GlobusXIOSystemDebugExitFD(listener_handle->socket);
    return GLOBUS_SUCCESS;

error_register:
    GlobusIXIOSystemFreeOperation(op_info);
error_op_info:
    GlobusXIOSystemDebugExitWithErrorFD(listener_handle->socket);
    return result;
}

/* calling this with null op could cause deadlock... 
 * reserved for internal use
 */
/* if waitforbytes == 0 and iov[0].iov_len == 0
 * behave like select()... ie notify when data ready
 */
globus_result_t
globus_l_xio_system_socket_register_read(
    globus_xio_operation_t              op,
    globus_xio_system_socket_handle_t   handle,
    const globus_xio_iovec_t *          u_iov,
    int                                 u_iovc,
    globus_size_t                       waitforbytes,
    globus_size_t                       nbytes,
    int                                 flags,
    globus_sockaddr_t *                 out_from,
    globus_xio_system_data_callback_t   callback,
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_i_xio_system_op_info_t *     op_info;
    struct iovec *                      iov;
    int                                 iovc;
    GlobusXIOName(globus_l_xio_system_socket_register_read);
    
    GlobusXIOSystemDebugEnterFD(handle->socket);
    GlobusXIOSystemDebugPrintf(
        GLOBUS_I_XIO_SYSTEM_DEBUG_DATA,
        ("[%s] Waiting for %ld bytes\n", _xio_name, (long) waitforbytes));
    
    GlobusIXIOSystemAllocOperation(op_info);
    if(!op_info)
    {
        result = GlobusXIOErrorMemory("op_info");
        goto error_op_info;
    }
    
    GlobusIXIOSystemAllocIovec(u_iovc, iov);
    if(!iov)
    {
        result = GlobusXIOErrorMemory("iov");
        goto error_iovec;
    }
    
    GlobusIXIOUtilTransferIovec(iov, u_iov, u_iovc);
    iovc = u_iovc;
    
    op_info->type = GLOBUS_I_XIO_SYSTEM_OP_READ;
    op_info->sop.data.start_iov = iov;
    op_info->sop.data.start_iovc = iovc;
    
    GlobusIXIOUtilAdjustIovec(iov, iovc, nbytes);
    op_info->sop.data.iov = iov;
    op_info->sop.data.iovc = iovc;
    op_info->sop.data.addr = out_from;
    op_info->sop.data.flags = flags;
    
    op_info->state = GLOBUS_I_XIO_SYSTEM_OP_NEW;
    op_info->op = op;
    op_info->handle = handle;
    op_info->user_arg = user_arg;
    op_info->sop.data.callback = callback;
    op_info->waitforbytes = waitforbytes;
    op_info->nbytes = nbytes;
    
    result = globus_l_xio_win32_socket_register_read(handle, op_info);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_win32_socket_register_read", result);
        goto error_register;
    }

    GlobusXIOSystemDebugExitFD(handle->socket);
    return GLOBUS_SUCCESS;

error_register:
    GlobusIXIOSystemFreeIovec(u_iovc, op_info->sop.data.start_iov);
error_iovec:
    GlobusIXIOSystemFreeOperation(op_info);
error_op_info:
    GlobusXIOSystemDebugExitWithErrorFD(handle->socket);
    return result;
}

globus_result_t
globus_xio_system_socket_register_read(
    globus_xio_operation_t              op,
    globus_xio_system_socket_handle_t   handle,
    const globus_xio_iovec_t *          u_iov,
    int                                 u_iovc,
    globus_size_t                       waitforbytes,
    int                                 flags,
    globus_sockaddr_t *                 out_from,
    globus_xio_system_data_callback_t   callback,
    void *                              user_arg)
{
    return globus_l_xio_system_socket_register_read(
        op,
        handle,
        u_iov,
        u_iovc,
        waitforbytes,
        0,
        flags,
        out_from,
        callback,
        user_arg);
}

/* calling this with null op could cause deadlock... 
 * reserved for internal use
 */
/* if waitforbytes == 0 and iov[0].iov_len == 0
 * behave like select()... ie notify when data ready
 */
globus_result_t
globus_l_xio_system_socket_register_write(
    globus_xio_operation_t              op,
    globus_xio_system_socket_handle_t   handle,
    const globus_xio_iovec_t *          u_iov,
    int                                 u_iovc,
    globus_size_t                       waitforbytes,
    globus_size_t                       nbytes,
    int                                 flags,
    globus_sockaddr_t *                 to,
    globus_xio_system_data_callback_t   callback,
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_i_xio_system_op_info_t *     op_info;
    struct iovec *                      iov;
    int                                 iovc;
    GlobusXIOName(globus_l_xio_system_socket_register_write);
    
    GlobusXIOSystemDebugEnterFD(handle->socket);
    GlobusXIOSystemDebugPrintf(
        GLOBUS_I_XIO_SYSTEM_DEBUG_DATA,
        ("[%s] Waiting for %ld bytes\n", _xio_name, (long) waitforbytes));
    
    GlobusIXIOSystemAllocOperation(op_info);
    if(!op_info)
    {
        result = GlobusXIOErrorMemory("op_info");
        goto error_op_info;
    }
    
    GlobusIXIOSystemAllocIovec(u_iovc, iov);
    if(!iov)
    {
        result = GlobusXIOErrorMemory("iov");
        goto error_iovec;
    }

    GlobusIXIOUtilTransferIovec(iov, u_iov, u_iovc);
    iovc = u_iovc;

    op_info->type = GLOBUS_I_XIO_SYSTEM_OP_WRITE;
    op_info->sop.data.start_iov = iov;
    op_info->sop.data.start_iovc = iovc;
    
    GlobusIXIOUtilAdjustIovec(iov, iovc, nbytes);
    op_info->sop.data.iov = iov;
    op_info->sop.data.iovc = iovc;
    op_info->sop.data.addr = to;
    op_info->sop.data.flags = flags;
    
    op_info->state = GLOBUS_I_XIO_SYSTEM_OP_NEW;
    op_info->op = op;
    op_info->handle = handle;
    op_info->user_arg = user_arg;
    op_info->sop.data.callback = callback;
    op_info->waitforbytes = waitforbytes;
    op_info->nbytes = nbytes;
    
    result = globus_l_xio_win32_socket_register_write(handle, op_info);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_win32_socket_register_write", result);
        goto error_register;
    }

    GlobusXIOSystemDebugExitFD(handle->socket);
    return GLOBUS_SUCCESS;

error_register:
    GlobusIXIOSystemFreeIovec(u_iovc, op_info->sop.data.start_iov);
error_iovec:
    GlobusIXIOSystemFreeOperation(op_info);
error_op_info:
    GlobusXIOSystemDebugExitWithErrorFD(handle->socket);
    return result;
}

globus_result_t
globus_xio_system_socket_register_write(
    globus_xio_operation_t              op,
    globus_xio_system_socket_handle_t   handle,
    const globus_xio_iovec_t *          u_iov,
    int                                 u_iovc,
    globus_size_t                       waitforbytes,
    int                                 flags,
    globus_sockaddr_t *                 to,
    globus_xio_system_data_callback_t   callback,
    void *                              user_arg)
{
    return globus_l_xio_system_socket_register_write(
        op,
        handle,
        u_iov,
        u_iovc,
        waitforbytes,
        0,
        flags,
        to,
        callback,
        user_arg);
}

typedef struct
{
    HANDLE                              event;
    globus_size_t                       nbytes;
    globus_object_t *                   error;
} globus_l_xio_win32_blocking_info_t;

static
globus_result_t
globus_l_xio_win32_blocking_init(
    globus_l_xio_win32_blocking_info_t ** u_blocking_info)
{
    globus_l_xio_win32_blocking_info_t * blocking_info;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_win32_blocking_init);
    
    GlobusXIOSystemDebugEnter();
    
    blocking_info = (globus_l_xio_win32_blocking_info_t *)
        globus_calloc(1, sizeof(globus_l_xio_win32_blocking_info_t));
    if(!blocking_info)
    {
        result = GlobusXIOErrorMemory("blocking_info");
        goto error_info;
    }
    
    blocking_info->event = CreateEvent(0, FALSE, FALSE, 0);
    if(blocking_info->event == 0)
    {
        result = GlobusXIOErrorSystemError(
            "CreateEvent", GetLastError());
        goto error_create;
    }
    
    *u_blocking_info = blocking_info;
    
    GlobusXIOSystemDebugExit();
    return GLOBUS_SUCCESS;

error_create:
    globus_free(blocking_info);
error_info:
    *u_blocking_info = 0;
    GlobusXIOSystemDebugExitWithError();
    return result;
}

static
void
globus_l_xio_win32_blocking_destroy(
    globus_l_xio_win32_blocking_info_t * blocking_info)
{
    GlobusXIOName(globus_l_xio_win32_blocking_destroy);
    
    GlobusXIOSystemDebugEnter();
    
    WSACloseEvent(blocking_info->event);
    globus_free(blocking_info);
    
    GlobusXIOSystemDebugExit();
}

static
void
globus_l_xio_win32_blocking_cb(
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_xio_win32_blocking_info_t * blocking_info;
    GlobusXIOName(globus_l_xio_win32_blocking_cb);
    
    GlobusXIOSystemDebugEnter();
    
    blocking_info = (globus_l_xio_win32_blocking_info_t *) user_arg;
    if(result != GLOBUS_SUCCESS)
    {
        blocking_info->error = globus_error_get(result);
    }
    blocking_info->nbytes = nbytes;
    SetEvent(blocking_info->event);
    
    GlobusXIOSystemDebugExit();
}

globus_result_t
globus_xio_system_socket_read(
    globus_xio_system_socket_handle_t   handle,
    const globus_xio_iovec_t *          iov,
    int                                 iovc,
    globus_size_t                       waitforbytes,
    int                                 flags,
    globus_sockaddr_t *                 from,
    globus_size_t *                     nbytes)
{
    globus_result_t                     result;
    globus_l_xio_win32_blocking_info_t * blocking_info;
    GlobusXIOName(globus_xio_system_socket_read);
    
    GlobusXIOSystemDebugEnterFD(handle->socket);
    GlobusXIOSystemDebugPrintf(
        GLOBUS_I_XIO_SYSTEM_DEBUG_DATA,
        ("[%s] Waiting for %ld bytes\n", _xio_name, (long) waitforbytes));
    
    win32_mutex_lock(&handle->lock);
    {
        result = globus_i_xio_system_socket_try_read(
            handle->socket,
            iov,
            iovc,
            flags,
            from,
            nbytes);
    }
    win32_mutex_unlock(&handle->lock);
    
    if(result == GLOBUS_SUCCESS && *nbytes < waitforbytes)
    {
        result = globus_l_xio_win32_blocking_init(&blocking_info);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_l_xio_win32_blocking_init", result);
            goto error_init;
        }
        
        result = globus_l_xio_system_socket_register_read(
            0,
            handle,
            iov,
            iovc,
            waitforbytes,
            *nbytes,
            flags,
            from,
            globus_l_xio_win32_blocking_cb,
            blocking_info);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_l_xio_system_socket_register_read", result);
            goto error_register;
        }
        
        while(WaitForSingleObject(
            blocking_info->event, INFINITE) != WAIT_OBJECT_0) {}
        
        if(blocking_info->error)
        {
            result = globus_error_put(blocking_info->error);
        }
        *nbytes = blocking_info->nbytes;
        
        globus_l_xio_win32_blocking_destroy(blocking_info);
    }

    GlobusXIOSystemDebugExitFD(handle->socket);
    return result;

error_register:
    globus_l_xio_win32_blocking_destroy(blocking_info);
error_init:
    GlobusXIOSystemDebugExitWithErrorFD(handle->socket);
    return result;
}

globus_result_t
globus_xio_system_socket_write(
    globus_xio_system_socket_handle_t   handle,
    const globus_xio_iovec_t *          iov,
    int                                 iovc,
    globus_size_t                       waitforbytes,
    int                                 flags,
    globus_sockaddr_t *                 to,
    globus_size_t *                     nbytes)
{
    globus_result_t                     result;
    globus_l_xio_win32_blocking_info_t * blocking_info;
    GlobusXIOName(globus_xio_system_socket_write);
    
    GlobusXIOSystemDebugEnterFD(handle->socket);
    GlobusXIOSystemDebugPrintf(
        GLOBUS_I_XIO_SYSTEM_DEBUG_DATA,
        ("[%s] Waiting for %ld bytes\n", _xio_name, (long) waitforbytes));
    
    win32_mutex_lock(&handle->lock);
    {
        result = globus_i_xio_system_socket_try_write(
            handle->socket,
            iov,
            iovc,
            flags,
            to,
            nbytes);
    }
    win32_mutex_unlock(&handle->lock);
    
    if(result == GLOBUS_SUCCESS && *nbytes < waitforbytes)
    {
        result = globus_l_xio_win32_blocking_init(&blocking_info);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_l_xio_win32_blocking_init", result);
            goto error_init;
        }
        
        result = globus_l_xio_system_socket_register_write(
            0,
            handle,
            iov,
            iovc,
            waitforbytes,
            *nbytes,
            flags,
            to,
            globus_l_xio_win32_blocking_cb,
            blocking_info);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_l_xio_system_socket_register_write", result);
            goto error_register;
        }
        
        while(WaitForSingleObject(
            blocking_info->event, INFINITE) != WAIT_OBJECT_0) {}
        
        if(blocking_info->error)
        {
            result = globus_error_put(blocking_info->error);
        }
        *nbytes = blocking_info->nbytes;
        
        globus_l_xio_win32_blocking_destroy(blocking_info);
    }

    GlobusXIOSystemDebugExitFD(handle->socket);
    return result;

error_register:
    globus_l_xio_win32_blocking_destroy(blocking_info);
error_init:
    GlobusXIOSystemDebugExitWithErrorFD(handle->socket);
    return result;
}

globus_result_t
globus_xio_system_socket_create(
    globus_xio_system_socket_t *        sock,
    int                                 domain,
    int                                 type,
    int                                 protocol)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_create);
    
    *sock = INVALID_SOCKET;
    GlobusXIOSystemDebugEnter();
    
    *sock = socket(domain, type, protocol);
    if(*sock == INVALID_SOCKET)
    {
        result = GlobusXIOErrorSystemError("socket", WSAGetLastError());
        goto error_socket;
    }

    /* all handles created by me are closed on exec */
    SetHandleInformation((HANDLE)*sock, HANDLE_FLAG_INHERIT, 0);

    GlobusXIOSystemDebugExitFD(*sock);
    return GLOBUS_SUCCESS;

error_socket:
    GlobusXIOSystemDebugExitWithErrorFD(*sock);
    return result;
}

globus_result_t
globus_xio_system_socket_setsockopt(
    globus_xio_system_socket_t          socket,
    int                                 level,
    int                                 optname,
    const void *                        optval,
    socklen_t                           optlen)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_setsockopt);
    
    GlobusXIOSystemDebugEnterFD(socket);
    
    if(setsockopt(socket, level, optname, optval, optlen) == SOCKET_ERROR)
    {
        result = GlobusXIOErrorSystemError("setsockopt", WSAGetLastError());
        goto error_setsockopt;
    }
    
    GlobusXIOSystemDebugExitFD(socket);
    return GLOBUS_SUCCESS;

error_setsockopt:
    GlobusXIOSystemDebugExitWithErrorFD(socket);
    return result;
}

globus_result_t
globus_xio_system_socket_getsockopt(
    globus_xio_system_socket_t          socket,
    int                                 level,
    int                                 optname,
    void *                              optval,
    socklen_t *                         optlen)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_getsockopt);
    
    GlobusXIOSystemDebugEnterFD(socket);
    
    if(getsockopt(socket, level, optname, optval, optlen) == SOCKET_ERROR)
    {
        result = GlobusXIOErrorSystemError("getsockopt", WSAGetLastError());
        goto error_getsockopt;
    }
    
    GlobusXIOSystemDebugExitFD(socket);
    return GLOBUS_SUCCESS;

error_getsockopt:
    GlobusXIOSystemDebugExitWithErrorFD(socket);
    return result;
}

globus_result_t
globus_xio_system_socket_getsockname(
    globus_xio_system_socket_t          socket,
    struct sockaddr *                   name,
    socklen_t *                         namelen)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_getsockname);
    
    GlobusXIOSystemDebugEnterFD(socket);
    
    if(getsockname(socket, name, namelen) == SOCKET_ERROR)
    {
        result = GlobusXIOErrorSystemError("getsockname", WSAGetLastError());
        goto error_getsockname;
    }
    
    GlobusXIOSystemDebugExitFD(socket);
    return GLOBUS_SUCCESS;

error_getsockname:
    GlobusXIOSystemDebugExitWithErrorFD(socket);
    return result;
}

globus_result_t
globus_xio_system_socket_getpeername(
    globus_xio_system_socket_t          socket,
    struct sockaddr *                   name,
    socklen_t *                         namelen)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_getpeername);
    
    GlobusXIOSystemDebugEnterFD(socket);
    
    if(getpeername(socket, name, namelen) == SOCKET_ERROR)
    {
        result = GlobusXIOErrorSystemError("getpeername", WSAGetLastError());
        goto error_getpeername;
    }
    
    GlobusXIOSystemDebugExitFD(socket);
    return GLOBUS_SUCCESS;

error_getpeername:
    GlobusXIOSystemDebugExitWithErrorFD(socket);
    return result;
}

globus_result_t
globus_xio_system_socket_bind(
    globus_xio_system_socket_t          socket,
    struct sockaddr *                   addr,
    socklen_t                           addrlen)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_bind);
    
    GlobusXIOSystemDebugEnterFD(socket);
    
    if(bind(socket, addr, addrlen) == SOCKET_ERROR)
    {
        result = GlobusXIOErrorSystemError("bind", WSAGetLastError());
        goto error_bind;
    }
    
    GlobusXIOSystemDebugExitFD(socket);
    return GLOBUS_SUCCESS;

error_bind:
    GlobusXIOSystemDebugExitWithErrorFD(socket);
    return result;
}

globus_result_t
globus_xio_system_socket_listen(
    globus_xio_system_socket_t          socket,
    int                                 backlog)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_listen);
    
    GlobusXIOSystemDebugEnterFD(socket);
    
    if(listen(socket, backlog) == SOCKET_ERROR)
    {
        result = GlobusXIOErrorSystemError("listen", WSAGetLastError());
        goto error_listen;
    }
    
    GlobusXIOSystemDebugExitFD(socket);
    return GLOBUS_SUCCESS;

error_listen:
    GlobusXIOSystemDebugExitWithErrorFD(socket);
    return result;
}

globus_result_t
globus_xio_system_socket_connect(
    globus_xio_system_socket_t          socket,
    const struct sockaddr *             addr,
    socklen_t                           addrlen)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_connect);
    
    GlobusXIOSystemDebugEnterFD(socket);
    
    if(connect(socket, addr, addrlen) == SOCKET_ERROR)
    {
        result = GlobusXIOErrorSystemError("connect", WSAGetLastError());
        goto error_connect;
    }
    
    GlobusXIOSystemDebugExitFD(socket);
    return GLOBUS_SUCCESS;

error_connect:
    GlobusXIOSystemDebugExitWithErrorFD(socket);
    return result;
}

globus_result_t
globus_xio_system_socket_close(
    globus_xio_system_socket_t          socket)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_close);
    
    GlobusXIOSystemDebugEnterFD(socket);
    
    if(closesocket(socket) == SOCKET_ERROR)
    {
        result = GlobusXIOErrorSystemError("closesocket", WSAGetLastError());
        goto error_close;
    }
    
    GlobusXIOSystemDebugExitFD(socket);
    return GLOBUS_SUCCESS;

error_close:
    GlobusXIOSystemDebugExitWithErrorFD(socket);
    return result;
}
#endif /*TARGET_ARCH_WIN32 */
