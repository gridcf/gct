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


#include "globus_i_xio_config.h"
#include "globus_common.h"
#ifndef TARGET_ARCH_WIN32
#include "globus_xio_system.h"
#include "globus_i_xio_system_common.h"
#include "globus_xio_driver.h"
#include <stdio.h>
#include <fcntl.h>

#ifdef HAVE_SYSCONF
#define GLOBUS_L_OPEN_MAX sysconf(_SC_OPEN_MAX)
#else
#define GLOBUS_L_OPEN_MAX 256
#endif

typedef struct globus_l_xio_system_s
{
    globus_xio_system_type_t            type;
    int                                 fd;
    globus_mutex_t                      lock; /* only used to protect below */
    globus_off_t                        file_position;
} globus_l_xio_system_t;

static
int
globus_l_xio_system_activate(void);

static
int
globus_l_xio_system_deactivate(void);

#include "version.h"

globus_module_descriptor_t              globus_i_xio_system_module =
{
    "globus_xio_system_select",
    globus_l_xio_system_activate,
    globus_l_xio_system_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

static globus_cond_t                    globus_l_xio_system_cond;
static globus_mutex_t                   globus_l_xio_system_fdset_mutex;
static globus_mutex_t                   globus_l_xio_system_cancel_mutex;
static globus_bool_t                    globus_l_xio_system_select_active;
static globus_bool_t                    globus_l_xio_system_wakeup_pending;
static globus_bool_t                    globus_l_xio_system_shutdown_called;
static int                              globus_l_xio_system_highest_fd;
static int                              globus_l_xio_system_max_fds;
static int                              globus_l_xio_system_fd_allocsize;
static fd_set *                         globus_l_xio_system_read_fds;
static fd_set *                         globus_l_xio_system_write_fds;
static fd_set *                         globus_l_xio_system_ready_reads;
static fd_set *                         globus_l_xio_system_ready_writes;
static globus_list_t *                  globus_l_xio_system_canceled_reads;
static globus_list_t *                  globus_l_xio_system_canceled_writes;
static globus_i_xio_system_op_info_t ** globus_l_xio_system_read_operations;
static globus_i_xio_system_op_info_t ** globus_l_xio_system_write_operations;
static int                              globus_l_xio_system_wakeup_pipe[2];
static globus_callback_handle_t         globus_l_xio_system_poll_handle;

/* In the pre-activation of the thread module, we
 * are setting up some code to block the SIGPIPE
 * signal. This is necessary because some of
 * the TCP protocols we are using do not have
 * a mode in which we can safely detect a remotely-
 * closing socket.
 */

static
void
globus_l_xio_system_poll(
    void *                              user_args);

static
void
globus_l_xio_system_kickout(
    void *                              user_arg);

static
void
globus_l_xio_system_select_wakeup(void);

static
void
globus_l_xio_system_unregister_read(
    int                                 fd);

static
void
globus_l_xio_system_unregister_write(
    int                                 fd);

static
int
globus_l_xio_system_add_nonblocking(
    globus_l_xio_system_t *             handle);

static
int
globus_l_xio_system_remove_nonblocking(
    globus_l_xio_system_t *             handle);

static
globus_result_t
globus_l_xio_system_try_read(
    globus_l_xio_system_t *             handle,
    globus_off_t                        offset,
    const globus_xio_iovec_t *          iov,
    int                                 iovc,
    int                                 flags,
    globus_sockaddr_t *                 from,
    globus_size_t *                     nbytes);

static
globus_result_t
globus_l_xio_system_try_write(
    globus_l_xio_system_t *             handle,
    globus_off_t                        offset,
    const globus_xio_iovec_t *          iov,
    int                                 iovc,
    int                                 flags,
    globus_sockaddr_t *                 to,
    globus_size_t *                     nbytes);

static
globus_result_t
globus_l_xio_system_close(
    int                                 fd);

static
void
globus_l_xio_system_wakeup_handler(
    void *                              user_arg)
{
    int                                 rc;
    char                                byte;
    GlobusXIOName(globus_l_xio_system_wakeup_handler);

    GlobusXIOSystemDebugEnter();

    if(!globus_l_xio_system_shutdown_called)
    {
        byte = 0;
        do
        {
            rc = write(
                globus_l_xio_system_wakeup_pipe[1], &byte, sizeof(byte));
        } while(rc < 0 && errno == EINTR);
    }

    GlobusXIOSystemDebugExit();
}

static
int
globus_l_xio_system_activate(void)
{
    int                                 i;
    char *                              block;
    globus_result_t                     result;
    globus_reltime_t                    period;
    GlobusXIOName(globus_l_xio_system_activate);

    if(globus_i_xio_system_common_activate() != GLOBUS_SUCCESS)
    {
        goto error_activate;
    }

    GlobusXIOSystemDebugEnter();

    globus_cond_init(&globus_l_xio_system_cond, GLOBUS_NULL);
    globus_mutex_init(&globus_l_xio_system_fdset_mutex, GLOBUS_NULL);
    globus_mutex_init(&globus_l_xio_system_cancel_mutex, GLOBUS_NULL);

    globus_l_xio_system_select_active = GLOBUS_FALSE;
    globus_l_xio_system_wakeup_pending = GLOBUS_FALSE;
    globus_l_xio_system_shutdown_called = GLOBUS_FALSE;

    /*
     * On some machines (SGI Irix at least), the fd_set structure isn't
     * necessarily large enough to hold the maximum number of open file
     * descriptors.  This ensures that it will be.
     */
    globus_l_xio_system_max_fds = GLOBUS_L_OPEN_MAX;
    globus_l_xio_system_fd_allocsize = sizeof(fd_set);
    if(globus_l_xio_system_fd_allocsize * 8 < globus_l_xio_system_max_fds)
    {
        /* Conservatively round up to 64 bits */
        globus_l_xio_system_fd_allocsize =
            ((globus_l_xio_system_max_fds + 63) & ~63) / 8;
    }

    i = globus_l_xio_system_fd_allocsize;
    block = (char *) globus_calloc(4, i);
    if(!block)
    {
        goto error_fdsets;
    }
    globus_l_xio_system_read_fds        = (fd_set *) block;
    globus_l_xio_system_write_fds       = (fd_set *) (block + i * 1);
    globus_l_xio_system_ready_reads     = (fd_set *) (block + i * 2);
    globus_l_xio_system_ready_writes    = (fd_set *) (block + i * 3);

    globus_l_xio_system_canceled_reads  = GLOBUS_NULL;
    globus_l_xio_system_canceled_writes = GLOBUS_NULL;

    globus_l_xio_system_read_operations = (globus_i_xio_system_op_info_t **)
        globus_calloc(
            globus_l_xio_system_max_fds * 2,
            sizeof(globus_i_xio_system_op_info_t *));
    if(!globus_l_xio_system_read_operations)
    {
        goto error_operations;
    }
    globus_l_xio_system_write_operations =
        globus_l_xio_system_read_operations + globus_l_xio_system_max_fds;

    /*
     * Create a pipe to myself, so that I can wake up the thread that is
     * blocked on a select().
     */
    if(pipe(globus_l_xio_system_wakeup_pipe) != 0)
    {
        goto error_pipe;
    }
    fcntl(globus_l_xio_system_wakeup_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(globus_l_xio_system_wakeup_pipe[1], F_SETFD, FD_CLOEXEC);

    globus_l_xio_system_highest_fd = globus_l_xio_system_wakeup_pipe[0];
    FD_SET(globus_l_xio_system_wakeup_pipe[0], globus_l_xio_system_read_fds);

    GlobusTimeReltimeSet(period, 0, 0);
    result = globus_callback_register_periodic(
        &globus_l_xio_system_poll_handle,
         GLOBUS_NULL,
         &period,
         globus_l_xio_system_poll,
         GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_callback_register_periodic", result);
        goto error_register;
    }

    globus_callback_add_wakeup_handler(
        globus_l_xio_system_wakeup_handler, GLOBUS_NULL);

    GlobusXIOSystemDebugExit();
    return GLOBUS_SUCCESS;

error_register:
    globus_l_xio_system_close(globus_l_xio_system_wakeup_pipe[0]);
    globus_l_xio_system_close(globus_l_xio_system_wakeup_pipe[1]);

error_pipe:
    globus_free(globus_l_xio_system_read_operations);

error_operations:
    globus_free(globus_l_xio_system_read_fds);

error_fdsets:
    globus_mutex_destroy(&globus_l_xio_system_cancel_mutex);
    globus_mutex_destroy(&globus_l_xio_system_fdset_mutex);
    globus_cond_destroy(&globus_l_xio_system_cond);

    GlobusXIOSystemDebugExitWithError();
    globus_i_xio_system_common_deactivate();
error_activate:
    return GLOBUS_FAILURE;
}

static
void
globus_l_xio_system_unregister_periodic_cb(
    void *                              user_args)
{
    globus_bool_t *                     signaled;
    GlobusXIOName(globus_l_xio_system_unregister_periodic_cb);

    GlobusXIOSystemDebugEnter();

    signaled = (globus_bool_t *) user_args;
    globus_mutex_lock(&globus_l_xio_system_fdset_mutex);
    {
        *signaled = GLOBUS_TRUE;
        globus_cond_signal(&globus_l_xio_system_cond);
    }
    globus_mutex_unlock(&globus_l_xio_system_fdset_mutex);

    GlobusXIOSystemDebugExit();
}

static
int
globus_l_xio_system_deactivate(void)
{
    GlobusXIOName(globus_l_xio_system_deactivate);

    GlobusXIOSystemDebugEnter();

    globus_mutex_lock(&globus_l_xio_system_fdset_mutex);
    {
        globus_bool_t                   signaled;

        globus_l_xio_system_shutdown_called = GLOBUS_TRUE;
        signaled = GLOBUS_FALSE;
        globus_callback_unregister(
            globus_l_xio_system_poll_handle,
            globus_l_xio_system_unregister_periodic_cb,
            &signaled,
            GLOBUS_NULL);
        globus_l_xio_system_wakeup_pending = GLOBUS_TRUE;
        globus_l_xio_system_select_wakeup();

        while(!signaled)
        {
            globus_cond_wait(
                &globus_l_xio_system_cond, &globus_l_xio_system_fdset_mutex);
        }
    }
    globus_mutex_unlock(&globus_l_xio_system_fdset_mutex);

    globus_l_xio_system_close(globus_l_xio_system_wakeup_pipe[0]);
    globus_l_xio_system_close(globus_l_xio_system_wakeup_pipe[1]);

    globus_list_free(globus_l_xio_system_canceled_reads);
    globus_list_free(globus_l_xio_system_canceled_writes);
    globus_free(globus_l_xio_system_read_operations);
    globus_free(globus_l_xio_system_read_fds);

    globus_mutex_destroy(&globus_l_xio_system_cancel_mutex);
    globus_mutex_destroy(&globus_l_xio_system_fdset_mutex);
    globus_cond_destroy(&globus_l_xio_system_cond);

    GlobusXIOSystemDebugExit();

    globus_i_xio_system_common_deactivate();

    return GLOBUS_SUCCESS;
}

static
globus_result_t
globus_l_xio_system_handle_init(
    globus_l_xio_system_t **            u_handle,
    int                                 fd,
    globus_xio_system_type_t            type)
{
    globus_l_xio_system_t *             handle;
    int                                 rc;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_system_handle_init);

    GlobusXIOSystemDebugEnterFD(fd);

    handle = (globus_l_xio_system_t *)
        globus_malloc(sizeof(globus_l_xio_system_t));
    if(!handle)
    {
        result = GlobusXIOErrorMemory("handle");
        goto error_alloc;
    }

    handle->type = type;
    handle->fd = fd;

    handle->file_position = globus_xio_system_file_get_position(fd);

    rc = globus_l_xio_system_add_nonblocking(handle);
    if(rc < 0)
    {
        result = GlobusXIOErrorSystemError("fcntl", errno);
        goto error_fcntl;
    }

    globus_mutex_init(&handle->lock, NULL);

    *u_handle = handle;
    GlobusXIOSystemDebugExitFD(fd);
    return GLOBUS_SUCCESS;

error_fcntl:
    globus_free(handle);
error_alloc:
    *u_handle = GLOBUS_NULL;
    GlobusXIOSystemDebugExitWithErrorFD(fd);
    return result;
}

globus_result_t
globus_xio_system_file_init(
    globus_xio_system_file_handle_t *   u_handle,
    globus_xio_system_file_t            fd)
{
    return globus_l_xio_system_handle_init(
        u_handle, fd, GLOBUS_XIO_SYSTEM_FILE);
}

globus_result_t
globus_xio_system_socket_init(
    globus_xio_system_socket_handle_t * u_handle,
    globus_xio_system_socket_t          fd,
    globus_xio_system_type_t            type)
{
    return globus_l_xio_system_handle_init(u_handle, fd, type);
}

static
void
globus_l_xio_system_handle_destroy(
    globus_l_xio_system_t *             handle)
{
    int                                 fd = handle->fd;

    GlobusXIOName(globus_l_xio_system_handle_destroy);

    GlobusXIOSystemDebugEnterFD(fd);

    globus_l_xio_system_remove_nonblocking(handle);
    globus_free(handle);

    GlobusXIOSystemDebugExitFD(fd);
}

void
globus_xio_system_file_destroy(
    globus_xio_system_file_handle_t     handle)
{
    globus_l_xio_system_handle_destroy(handle);
}

void
globus_xio_system_socket_destroy(
    globus_xio_system_socket_handle_t   handle)
{
    globus_l_xio_system_handle_destroy(handle);
}

static
void
globus_l_xio_system_cancel_cb(
    globus_xio_operation_t              op,
    void *                              user_arg,
    globus_xio_error_type_t             reason)
{
    globus_i_xio_system_op_info_t *     op_info;
    GlobusXIOName(globus_l_xio_system_cancel_cb);

    GlobusXIOSystemDebugEnter();

    op_info = (globus_i_xio_system_op_info_t *) user_arg;

    globus_mutex_lock(&globus_l_xio_system_cancel_mutex);
    {
        if(op_info->state != GLOBUS_I_XIO_SYSTEM_OP_COMPLETE &&
            op_info->state != GLOBUS_I_XIO_SYSTEM_OP_CANCELED)
        {
            op_info->error = reason == GLOBUS_XIO_ERROR_TIMEOUT
                ? GlobusXIOErrorObjTimeout()
                : GlobusXIOErrorObjCanceled();

            globus_mutex_lock(&globus_l_xio_system_fdset_mutex);
            {
                globus_bool_t           pend;

                if(op_info->state == GLOBUS_I_XIO_SYSTEM_OP_NEW)
                {
                    op_info->state = GLOBUS_I_XIO_SYSTEM_OP_CANCELED;

                    GlobusXIOSystemDebugPrintf(
                        GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                        (_XIOSL("[%s] fd=%d, Canceling NEW\n"),
                            _xio_name, op_info->handle->fd));
                }
                else
                {
                    if(globus_l_xio_system_select_active)
                    {
                        op_info->state = GLOBUS_I_XIO_SYSTEM_OP_CANCELED;

                        GlobusXIOSystemDebugPrintf(
                            GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                            (_XIOSL("[%s] fd=%d, Canceling Active\n"),
                                _xio_name, op_info->handle->fd));

                        /* pend the cancel for after select wakes up */
                        if(!globus_l_xio_system_wakeup_pending)
                        {
                            globus_l_xio_system_wakeup_pending = GLOBUS_TRUE;
                            globus_l_xio_system_select_wakeup();
                        }

                        pend = GLOBUS_TRUE;
                    }
                    else
                    {
                        globus_result_t result;

                        op_info->state = GLOBUS_I_XIO_SYSTEM_OP_COMPLETE;

                        GlobusXIOSystemDebugPrintf(
                            GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                            (_XIOSL("[%s] fd=%d, Canceling Pending\n"),
                                _xio_name, op_info->handle->fd));

                        /* unregister and kickout now */
                        result = globus_callback_register_oneshot(
                            GLOBUS_NULL,
                            GLOBUS_NULL,
                            globus_l_xio_system_kickout,
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

                        pend = GLOBUS_FALSE;
                    }

                    /* I can access op_info even though I oneshoted above
                     * because the CancelDisallow() call in the kickout will
                     * block until I leave this function
                     */
                    if(op_info->type == GLOBUS_I_XIO_SYSTEM_OP_READ             ||
                        op_info->type == GLOBUS_I_XIO_SYSTEM_OP_ACCEPT)
                    {
                        if(pend)
                        {
                            globus_list_insert(
                                &globus_l_xio_system_canceled_reads,
                                (void *) (intptr_t) op_info->handle->fd);
                        }
                        else
                        {
                            globus_l_xio_system_unregister_read(
                                op_info->handle->fd);
                        }
                    }
                    else
                    {
                        if(pend)
                        {
                            globus_list_insert(
                                &globus_l_xio_system_canceled_writes,
                                (void *) (intptr_t) op_info->handle->fd);
                        }
                        else
                        {
                            globus_l_xio_system_unregister_write(
                                op_info->handle->fd);
                        }
                    }
                }
            }
            globus_mutex_unlock(&globus_l_xio_system_fdset_mutex);
        }
    }
    globus_mutex_unlock(&globus_l_xio_system_cancel_mutex);

    GlobusXIOSystemDebugExit();
}

static
globus_result_t
globus_l_xio_system_register_read_fd(
    int                                 fd,
    globus_i_xio_system_op_info_t *     read_info)
{
    globus_result_t                     result;
    globus_bool_t                       do_wakeup = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_system_register_read_fd);

    GlobusXIOSystemDebugEnterFD(fd);

    /* I have to do this outside the lock because of lock inversion issues */
    if(globus_xio_operation_enable_cancel(
        read_info->op, globus_l_xio_system_cancel_cb, read_info))
    {
        result = GlobusXIOErrorCanceled();
        goto error_cancel_enable;
    }

    globus_mutex_lock(&globus_l_xio_system_fdset_mutex);
    {
        /* this really shouldnt be possible, but to be thorough ... */
        if(read_info->state == GLOBUS_I_XIO_SYSTEM_OP_CANCELED)
        {
            result = globus_error_put(read_info->error);
            goto error_canceled;
        }

        if(globus_l_xio_system_shutdown_called)
        {
            result = GlobusXIOErrorNotActivated();
            goto error_deactivated;
        }

        if(fd >= globus_l_xio_system_max_fds)
        {
            result = GlobusXIOErrorSystemResource(_XIOSL("too many fds"));
            goto error_too_many_fds;
        }

        if(FD_ISSET(fd, globus_l_xio_system_read_fds))
        {
            result = GlobusXIOErrorAlreadyRegistered();
            goto error_already_registered;
        }

        if(fd > globus_l_xio_system_highest_fd)
        {
            globus_l_xio_system_highest_fd = fd;
        }

        FD_SET(fd, globus_l_xio_system_read_fds);
        globus_l_xio_system_read_operations[fd] = read_info;

        if(globus_l_xio_system_select_active &&
            !globus_l_xio_system_wakeup_pending)
        {
            globus_l_xio_system_wakeup_pending = GLOBUS_TRUE;
            do_wakeup = GLOBUS_TRUE;
        }

        read_info->state = GLOBUS_I_XIO_SYSTEM_OP_PENDING;
    }
    globus_mutex_unlock(&globus_l_xio_system_fdset_mutex);

    if(do_wakeup)
    {
        /* I do this outside the lock because the select thread is likely
         * to wakeup immediately which would mean immediate contention for
         * that lock
         */
        globus_l_xio_system_select_wakeup();
    }

    GlobusXIOSystemDebugExitFD(fd);
    return GLOBUS_SUCCESS;

error_already_registered:
error_too_many_fds:
error_deactivated:
error_canceled:
    read_info->state = GLOBUS_I_XIO_SYSTEM_OP_COMPLETE;
    globus_mutex_unlock(&globus_l_xio_system_fdset_mutex);
    globus_xio_operation_disable_cancel(read_info->op);

error_cancel_enable:
    GlobusXIOSystemDebugExitWithErrorFD(fd);
    return result;
}

static
globus_result_t
globus_l_xio_system_register_write_fd(
    int                                 fd,
    globus_i_xio_system_op_info_t *     write_info)
{
    globus_result_t                     result;
    globus_bool_t                       do_wakeup = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_system_register_write_fd);

    GlobusXIOSystemDebugEnterFD(fd);

    /* I have to do this outside the lock because of lock inversion issues */
    if(globus_xio_operation_enable_cancel(
        write_info->op, globus_l_xio_system_cancel_cb, write_info))
    {
        result = GlobusXIOErrorCanceled();
        goto error_cancel_enable;
    }

    globus_mutex_lock(&globus_l_xio_system_fdset_mutex);
    {
        /* this really shouldnt be possible, but to be thorough ... */
        if(write_info->state == GLOBUS_I_XIO_SYSTEM_OP_CANCELED)
        {
            result = globus_error_put(write_info->error);
            goto error_canceled;
        }

        if(globus_l_xio_system_shutdown_called)
        {
            result = GlobusXIOErrorNotActivated();
            goto error_deactivated;
        }

        if(fd >= globus_l_xio_system_max_fds)
        {
            result = GlobusXIOErrorSystemResource(_XIOSL("too many fds"));
            goto error_too_many_fds;
        }

        if(FD_ISSET(fd, globus_l_xio_system_write_fds))
        {
            result = GlobusXIOErrorAlreadyRegistered();
            goto error_already_registered;
        }

        if(fd > globus_l_xio_system_highest_fd)
        {
            globus_l_xio_system_highest_fd = fd;
        }

        FD_SET(fd, globus_l_xio_system_write_fds);
        globus_l_xio_system_write_operations[fd] = write_info;

        if(globus_l_xio_system_select_active &&
            !globus_l_xio_system_wakeup_pending)
        {
            globus_l_xio_system_wakeup_pending = GLOBUS_TRUE;
            do_wakeup = GLOBUS_TRUE;
        }

        write_info->state = GLOBUS_I_XIO_SYSTEM_OP_PENDING;
    }
    globus_mutex_unlock(&globus_l_xio_system_fdset_mutex);

    if(do_wakeup)
    {
        /* I do this outside the lock because the select thread is likely
         * to wakeup immediately which would mean immediate contention for
         * that lock
         */
        globus_l_xio_system_select_wakeup();
    }

    GlobusXIOSystemDebugExitFD(fd);
    return GLOBUS_SUCCESS;

error_already_registered:
error_too_many_fds:
error_deactivated:
error_canceled:
    write_info->state = GLOBUS_I_XIO_SYSTEM_OP_COMPLETE;
    globus_mutex_unlock(&globus_l_xio_system_fdset_mutex);
    globus_xio_operation_disable_cancel(write_info->op);

error_cancel_enable:
    GlobusXIOSystemDebugExitWithErrorFD(fd);
    return result;
}

/* called locked */
static
void
globus_l_xio_system_unregister_read(
    int                                 fd)
{
    GlobusXIOName(globus_l_xio_system_unregister_read);

    GlobusXIOSystemDebugEnterFD(fd);

    globus_assert(FD_ISSET(fd, globus_l_xio_system_read_fds));
    FD_CLR(fd, globus_l_xio_system_read_fds);
    globus_l_xio_system_read_operations[fd] = GLOBUS_NULL;

    GlobusXIOSystemDebugExitFD(fd);
}

/* called locked */
static
void
globus_l_xio_system_unregister_write(
    int                                 fd)
{
    GlobusXIOName(globus_l_xio_system_unregister_write);

    GlobusXIOSystemDebugEnterFD(fd);

    globus_assert(FD_ISSET(fd, globus_l_xio_system_write_fds));
    FD_CLR(fd, globus_l_xio_system_write_fds);
    globus_l_xio_system_write_operations[fd] = GLOBUS_NULL;

    GlobusXIOSystemDebugExitFD(fd);
}

static
int
globus_l_xio_system_add_nonblocking(
    globus_l_xio_system_t *             handle)
{
    int flags;
    int rc;

    flags = fcntl(handle->fd, F_GETFL);
    if(flags < 0)
    {
        rc = flags;
    }
    else
    {
        flags |= O_NONBLOCK;
        rc = fcntl(handle->fd, F_SETFL, flags);
    }
    GlobusXIOSystemUpdateErrno();

    return rc;
}

static
int
globus_l_xio_system_remove_nonblocking(
    globus_l_xio_system_t *             handle)
{
    int                             flags;
    int                             rc;

    flags = fcntl(handle->fd, F_GETFL);
    if(flags < 0)
    {
        rc = flags;
    }
    else
    {
        flags &= ~O_NONBLOCK;
        rc = fcntl(handle->fd, F_SETFL, flags);
    }

    GlobusXIOSystemUpdateErrno();
    return rc;
}

static
void
globus_l_xio_system_kickout(
    void *                              user_arg)
{
    globus_i_xio_system_op_info_t *     op_info;
    int                                 fd;
    GlobusXIOName(globus_l_xio_system_kickout);

    op_info = (globus_i_xio_system_op_info_t *) user_arg;

    fd = op_info->handle->fd;
    GlobusXIOSystemDebugEnterFD(fd);

    globus_xio_operation_disable_cancel(op_info->op);

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

    GlobusXIOSystemDebugExitFD(fd);
    GlobusIXIOSystemFreeOperation(op_info);
}

static
void
globus_l_xio_system_select_wakeup(void)
{
    globus_ssize_t                      rc;
    char                                byte;
    GlobusXIOName(globus_l_xio_system_select_wakeup);

    GlobusXIOSystemDebugEnter();

    byte = 0;

    do
    {
        rc = write(globus_l_xio_system_wakeup_pipe[1], &byte, sizeof(byte));
    } while(rc < 0 && errno == EINTR);

    if(rc <= 0)
    {
        globus_panic(
            GLOBUS_XIO_SYSTEM_MODULE,
            GlobusXIOErrorSystemError("write", errno),
            _XIOSL("[%s:%d] Couldn't wakeup select"),
            _xio_name,
            __LINE__);
    }

    GlobusXIOSystemDebugExit();
}

static
void
globus_l_xio_system_handle_wakeup(void)
{
    char                                buf[64];
    globus_ssize_t                      done;
    GlobusXIOName(globus_l_xio_system_handle_wakeup);

    GlobusXIOSystemDebugEnter();

    do
    {
        done = read(globus_l_xio_system_wakeup_pipe[0], buf, sizeof(buf));
    } while(done < 0 && errno == EINTR);

    GlobusXIOSystemDebugExit();
}

static
globus_bool_t
globus_l_xio_system_handle_read(
    int                                 fd)
{
    globus_bool_t                       handled_it;
    globus_i_xio_system_op_info_t *     read_info;
    globus_size_t                       nbytes;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_system_handle_read);

    GlobusXIOSystemDebugEnterFD(fd);

    handled_it = GLOBUS_FALSE;
    read_info = globus_l_xio_system_read_operations[fd];
    result = GLOBUS_SUCCESS;

    globus_xio_operation_refresh_timeout(read_info->op);

    if(read_info->state == GLOBUS_I_XIO_SYSTEM_OP_CANCELED)
    {
        /* error already set on info */
        goto error_canceled;
    }

    switch(read_info->type)
    {
      case GLOBUS_I_XIO_SYSTEM_OP_ACCEPT:
        {
            int                         new_fd;

            do
            {
                new_fd = accept(fd, GLOBUS_NULL, GLOBUS_NULL);
                GlobusXIOSystemUpdateErrno();
            } while(new_fd < 0 && errno == EINTR);

            if(new_fd < 0)
            {
                if(errno != ECONNABORTED &&
                    errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    result = GlobusXIOErrorSystemError("accept", errno);
                }
            }
            else
            {
                int                     rc;
                globus_l_xio_system_t   tmp_handle;

                *read_info->sop.non_data.out_fd = new_fd;
                tmp_handle.fd = new_fd;
                tmp_handle.type = GLOBUS_XIO_SYSTEM_TCP;

                rc = globus_l_xio_system_remove_nonblocking(&tmp_handle);

                read_info->nbytes++;
                GlobusXIOSystemDebugPrintf(
                    GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                    (_XIOSL("[%s] Accepted new connection, fd=%d\n"),
                         _xio_name, new_fd));
            }
        }
        break;

      case GLOBUS_I_XIO_SYSTEM_OP_READ:
        result = globus_l_xio_system_try_read(
            read_info->handle,
            read_info->offset,
            read_info->sop.data.iov,
            read_info->sop.data.iovc,
            read_info->sop.data.flags,
            read_info->sop.data.addr,
            &nbytes);
        if(result == GLOBUS_SUCCESS)
        {
            read_info->nbytes += nbytes;
            read_info->offset += nbytes;
            GlobusIXIOUtilAdjustIovec(
                read_info->sop.data.iov, read_info->sop.data.iovc, nbytes);
        }
        break;

      default:
        globus_assert(0 && "Unexpected type for read operation");
        return GLOBUS_FALSE;
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
error_canceled:
        handled_it = GLOBUS_TRUE;
        read_info->state = GLOBUS_I_XIO_SYSTEM_OP_COMPLETE;

        globus_mutex_lock(&globus_l_xio_system_fdset_mutex);
        {
            globus_l_xio_system_unregister_read(fd);
        }
        globus_mutex_unlock(&globus_l_xio_system_fdset_mutex);

        result = globus_callback_register_oneshot(
            GLOBUS_NULL, GLOBUS_NULL, globus_l_xio_system_kickout, read_info);
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

    GlobusXIOSystemDebugExitFD(fd);
    return handled_it;
}

static
globus_bool_t
globus_l_xio_system_handle_write(
    int                                 fd)
{
    globus_bool_t                       handled_it;
    globus_i_xio_system_op_info_t *     write_info;
    globus_size_t                       nbytes;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_system_handle_write);

    GlobusXIOSystemDebugEnterFD(fd);

    handled_it = GLOBUS_FALSE;
    result = GLOBUS_SUCCESS;
    write_info = globus_l_xio_system_write_operations[fd];

    globus_xio_operation_refresh_timeout(write_info->op);

    if(write_info->state == GLOBUS_I_XIO_SYSTEM_OP_CANCELED)
    {
        /* error already set on info */
        goto error_canceled;
    }

    switch(write_info->type)
    {
      case GLOBUS_I_XIO_SYSTEM_OP_CONNECT:
        {
            int                         err;
            globus_socklen_t            errlen;

            errlen = sizeof(err);
            if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0)
            {
                GlobusXIOSystemUpdateErrno();
                err = errno;
            }

            if(err)
            {
                result = GlobusXIOErrorSystemError("connect", err);
            }
        }
        break;

      case GLOBUS_I_XIO_SYSTEM_OP_WRITE:
        result = globus_l_xio_system_try_write(
            write_info->handle,
            write_info->offset,
            write_info->sop.data.iov,
            write_info->sop.data.iovc,
            write_info->sop.data.flags,
            write_info->sop.data.addr,
            &nbytes);
        if(result == GLOBUS_SUCCESS)
        {
            write_info->nbytes += nbytes;
            write_info->offset += nbytes;
            GlobusIXIOUtilAdjustIovec(
                write_info->sop.data.iov, write_info->sop.data.iovc, nbytes);
        }
        break;

      default:
        globus_assert(0 && "Unexpected type for write operation");
        return GLOBUS_FALSE;
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
error_canceled:
        handled_it = GLOBUS_TRUE;
        write_info->state = GLOBUS_I_XIO_SYSTEM_OP_COMPLETE;

        globus_mutex_lock(&globus_l_xio_system_fdset_mutex);
        {
            globus_l_xio_system_unregister_write(fd);
        }
        globus_mutex_unlock(&globus_l_xio_system_fdset_mutex);

        result = globus_callback_register_oneshot(
            GLOBUS_NULL, GLOBUS_NULL, globus_l_xio_system_kickout, write_info);
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

    GlobusXIOSystemDebugExitFD(fd);
    return handled_it;
}

/*
 * one of these fds is bad, lock down the fdset and check them all
 * --- assumed to be called with cancel lock held after a select
 */
static
void
globus_l_xio_system_bad_apple(void)
{
    globus_i_xio_system_op_info_t *     op_info;
    int                                 fd;
    int                                 rc;
    struct stat                         stat_buf;
    GlobusXIOName(globus_l_xio_system_bad_apple);

    GlobusXIOSystemDebugEnter();

    globus_mutex_lock(&globus_l_xio_system_fdset_mutex);
    {
        for(fd = 0; fd <= globus_l_xio_system_highest_fd; fd++)
        {
            if(FD_ISSET(fd, globus_l_xio_system_read_fds))
            {
                rc = fstat(fd, &stat_buf);
                GlobusXIOSystemUpdateErrno();
                if(rc < 0 && errno == EBADF)
                {
                    GlobusXIOSystemDebugPrintf(
                        GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                        (_XIOSL("[%s] fd=%d, Canceling read bad apple\n"),
                        _xio_name, fd));

                    op_info = globus_l_xio_system_read_operations[fd];
                    if(op_info->state == GLOBUS_I_XIO_SYSTEM_OP_PENDING)
                    {
                        op_info->state = GLOBUS_I_XIO_SYSTEM_OP_CANCELED;
                        op_info->error = GlobusXIOErrorObjParameter("handle");
                        globus_list_insert(
                            &globus_l_xio_system_canceled_reads,
                            (void *) (intptr_t) fd);
                    }
                }
            }

            if(FD_ISSET(fd, globus_l_xio_system_write_fds))
            {
                rc = fstat(fd, &stat_buf);
                GlobusXIOSystemUpdateErrno();
                if(rc < 0 && errno == EBADF)
                {
                    GlobusXIOSystemDebugPrintf(
                        GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                        (_XIOSL("[%s] fd=%d, Canceling write bad apple\n"),
                        _xio_name, fd));

                    op_info = globus_l_xio_system_write_operations[fd];
                    if(op_info->state == GLOBUS_I_XIO_SYSTEM_OP_PENDING)
                    {
                        op_info->state = GLOBUS_I_XIO_SYSTEM_OP_CANCELED;
                        op_info->error = GlobusXIOErrorObjParameter("handle");
                        globus_list_insert(
                            &globus_l_xio_system_canceled_writes,
                            (void *) (intptr_t) fd);
                    }
                }
            }
        }
    }
    globus_mutex_unlock(&globus_l_xio_system_fdset_mutex);

    GlobusXIOSystemDebugExit();
}

static
void
globus_l_xio_system_poll(
    void *                              user_args)
{
    globus_bool_t                       time_left_is_zero;
    globus_bool_t                       handled_something;
    GlobusXIOName(globus_l_xio_system_poll);

    GlobusXIOSystemDebugEnter();

    handled_something = GLOBUS_FALSE;

    do
    {
        globus_reltime_t                time_left;
        globus_bool_t                   time_left_is_infinity;
        int                             num;
        int                             nready;
        int                             fd;
        int                             save_errno;

        time_left_is_zero = GLOBUS_FALSE;
        time_left_is_infinity = GLOBUS_FALSE;

        globus_callback_get_timeout(&time_left);

        if(globus_reltime_cmp(&time_left, &globus_i_reltime_zero) == 0)
        {
            time_left_is_zero = GLOBUS_TRUE;
        }
        else if(globus_time_reltime_is_infinity(&time_left))
        {
            time_left_is_infinity = GLOBUS_TRUE;
        }

        globus_mutex_lock(&globus_l_xio_system_fdset_mutex);
        {
            memcpy(
                globus_l_xio_system_ready_reads,
                globus_l_xio_system_read_fds,
                globus_l_xio_system_fd_allocsize);
            memcpy(
                globus_l_xio_system_ready_writes,
                globus_l_xio_system_write_fds,
                globus_l_xio_system_fd_allocsize);

            num = globus_l_xio_system_highest_fd + 1;
            globus_l_xio_system_select_active = GLOBUS_TRUE;
        }
        globus_mutex_unlock(&globus_l_xio_system_fdset_mutex);

        GlobusXIOSystemDebugPrintf(
            GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
            (_XIOSL("[%s] Before select\n"), _xio_name));

        nready = select(
            num,
            globus_l_xio_system_ready_reads,
            globus_l_xio_system_ready_writes,
            GLOBUS_NULL,
            (time_left_is_infinity ? GLOBUS_NULL : &time_left));

        GlobusXIOSystemUpdateErrno();
        save_errno = errno;

        GlobusXIOSystemDebugPrintf(
            GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
            (_XIOSL("[%s] After select\n"), _xio_name));

        globus_mutex_lock(&globus_l_xio_system_cancel_mutex);
        {
            globus_l_xio_system_select_active = GLOBUS_FALSE;

            if(nready > 0)
            {
                fd = globus_l_xio_system_wakeup_pipe[0];
                if(FD_ISSET(fd, globus_l_xio_system_ready_reads))
                {
                    globus_l_xio_system_handle_wakeup();
                    globus_l_xio_system_wakeup_pending = GLOBUS_FALSE;
                    FD_CLR(fd, globus_l_xio_system_ready_reads);
                    nready--;
                }
            }
            else
                if(nready == 0)
            {
                time_left_is_zero = GLOBUS_TRUE;
            }
            else
            {
                if(save_errno == EBADF)
                {
                    globus_l_xio_system_bad_apple();
                }

                /*
                 * can't really do anything about other errors
                 * so, set ready fds to known state in case there are things
                 * to be canceled
                 */
                nready = 0;
                memset(
                    globus_l_xio_system_ready_reads,
                    0,
                    globus_l_xio_system_fd_allocsize);
                memset(
                    globus_l_xio_system_ready_writes,
                    0,
                    globus_l_xio_system_fd_allocsize);
            }

            while(!globus_list_empty(globus_l_xio_system_canceled_reads))
            {
                fd = (int) (intptr_t) globus_list_remove(
                    &globus_l_xio_system_canceled_reads,
                    globus_l_xio_system_canceled_reads);

                GlobusXIOSystemDebugPrintf(
                    GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                    (_XIOSL("[%s] fd=%d, Setting canceled read\n"), _xio_name, fd));

                if(!FD_ISSET(fd, globus_l_xio_system_ready_reads))
                {
                    FD_SET(fd, globus_l_xio_system_ready_reads);
                    nready++;
                }
            }

            while(!globus_list_empty(globus_l_xio_system_canceled_writes))
            {
                fd = (int) (intptr_t) globus_list_remove(
                    &globus_l_xio_system_canceled_writes,
                    globus_l_xio_system_canceled_writes);

                GlobusXIOSystemDebugPrintf(
                    GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                    (_XIOSL("[%s] fd=%d, Setting canceled read\n"), _xio_name, fd));

                if(!FD_ISSET(fd, globus_l_xio_system_ready_writes))
                {
                    FD_SET(fd, globus_l_xio_system_ready_writes);
                    nready++;
                }
            }

            for(fd = 0; nready; fd++)
            {
                if(FD_ISSET(fd, globus_l_xio_system_ready_reads))
                {
                    nready--;

                    if(globus_l_xio_system_handle_read(fd))
                    {
                        handled_something = GLOBUS_TRUE;
                    }
                }

                if(FD_ISSET(fd, globus_l_xio_system_ready_writes))
                {
                    nready--;

                    if(globus_l_xio_system_handle_write(fd))
                    {
                        handled_something = GLOBUS_TRUE;
                    }
                }
            }
        }
        globus_mutex_unlock(&globus_l_xio_system_cancel_mutex);

    } while(!handled_something &&
        !time_left_is_zero &&
        !globus_l_xio_system_shutdown_called);

    GlobusXIOSystemDebugExit();
}

globus_result_t
globus_xio_system_socket_register_connect(
    globus_xio_operation_t              op,
    globus_xio_system_socket_handle_t   handle,
    globus_sockaddr_t *                 addr,
    globus_xio_system_callback_t        callback,
    void *                              user_arg)
{
    globus_bool_t                       done;
    globus_result_t                     result;
    globus_i_xio_system_op_info_t *     op_info;
    int                                 fd = handle->fd;
    GlobusXIOName(globus_xio_system_socket_register_connect);

    GlobusXIOSystemDebugEnterFD(fd);

    done = GLOBUS_FALSE;
    while(!done && connect(
        fd, (const struct sockaddr *) addr, GlobusLibcSockaddrLen(addr)) < 0)
    {
        GlobusXIOSystemUpdateErrno();
        switch(errno)
        {
          case EINPROGRESS:
            done = GLOBUS_TRUE;
            break;

          case EINTR:
            /* retry */
            break;

          case ETIMEDOUT: /* this was in globus io.. not sure why */
            globus_thread_yield();
            break;

          default:
            result = GlobusXIOErrorSystemError("connect", errno);
            goto error_connect;
        }
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

    result = globus_l_xio_system_register_write_fd(fd, op_info);

    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            _XIOSL("globus_l_xio_system_register_write_fd"), result);
        goto error_register;

    }

    /* handle could be destroyed by time we get here - no touch! */
    GlobusXIOSystemDebugExitFD(fd);
    return GLOBUS_SUCCESS;

error_register:
    GlobusIXIOSystemFreeOperation(op_info);

error_op_info:
error_connect:
    GlobusXIOSystemDebugExitWithErrorFD(fd);
    return result;
}

globus_result_t
globus_xio_system_socket_register_accept(
    globus_xio_operation_t              op,
    globus_xio_system_socket_handle_t   listener_handle,
    globus_xio_system_socket_t *        out_fd,
    globus_xio_system_callback_t        callback,
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_i_xio_system_op_info_t *     op_info;
    int                                 fd = listener_handle->fd;
    GlobusXIOName(globus_xio_system_socket_register_accept);

    GlobusXIOSystemDebugEnterFD(fd);

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
    op_info->sop.non_data.out_fd = out_fd;
    op_info->waitforbytes = 1;

    result = globus_l_xio_system_register_read_fd(fd, op_info);

    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            _XIOSL("globus_l_xio_system_register_read_fd"), result);
        goto error_register;
    }

    /* handle could be destroyed by time we get here - no touch! */
    GlobusXIOSystemDebugExitFD(fd);
    return GLOBUS_SUCCESS;

error_register:
    GlobusIXIOSystemFreeOperation(op_info);

error_op_info:
    GlobusXIOSystemDebugExitWithErrorFD(fd);
    return result;
}

static
globus_result_t
globus_l_xio_system_register_read(
    globus_xio_operation_t              op,
    globus_l_xio_system_t *             handle,
    globus_off_t                        offset,
    const globus_xio_iovec_t *          u_iov,
    int                                 u_iovc,
    globus_size_t                       waitforbytes,
    int                                 flags,
    globus_sockaddr_t *                 from,
    globus_xio_system_data_callback_t   callback,
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_i_xio_system_op_info_t *     op_info;
    struct iovec *                      iov;
    int                                 fd = handle->fd;
    GlobusXIOName(globus_l_xio_system_register_read);

    GlobusXIOSystemDebugEnterFD(fd);
    GlobusXIOSystemDebugPrintf(
        GLOBUS_I_XIO_SYSTEM_DEBUG_DATA,
        (_XIOSL("[%s] Waiting for %u bytes\n"), _xio_name, (unsigned) waitforbytes));

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

    op_info->type = GLOBUS_I_XIO_SYSTEM_OP_READ;
    op_info->sop.data.start_iov = iov;
    op_info->sop.data.start_iovc = u_iovc;
    op_info->sop.data.iov = iov;
    op_info->sop.data.iovc = u_iovc;
    op_info->sop.data.addr = from;
    op_info->sop.data.flags = flags;

    op_info->state = GLOBUS_I_XIO_SYSTEM_OP_NEW;
    op_info->op = op;
    op_info->handle = handle;
    op_info->user_arg = user_arg;
    op_info->sop.data.callback = callback;
    op_info->waitforbytes = waitforbytes;
    op_info->offset = offset;

    result = globus_l_xio_system_register_read_fd(fd, op_info);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_system_register_read_fd", result);
        goto error_register;
    }

    /* handle could be destroyed by time we get here - no touch! */
    GlobusXIOSystemDebugExitFD(fd);
    return GLOBUS_SUCCESS;

error_register:
    GlobusIXIOSystemFreeIovec(u_iovc, iov);

error_iovec:
    GlobusIXIOSystemFreeOperation(op_info);

error_op_info:
    GlobusXIOSystemDebugExitWithErrorFD(fd);
    return result;
}

globus_result_t
globus_xio_system_file_register_read(
    globus_xio_operation_t              op,
    globus_xio_system_file_handle_t     handle,
    globus_off_t                        offset,
    const globus_xio_iovec_t *          u_iov,
    int                                 u_iovc,
    globus_size_t                       waitforbytes,
    globus_xio_system_data_callback_t   callback,
    void *                              user_arg)
{
    return globus_l_xio_system_register_read(
        op,
        handle,
        offset,
        u_iov,
        u_iovc,
        waitforbytes,
        0,
        GLOBUS_NULL,
        callback,
        user_arg);
}

globus_result_t
globus_xio_system_socket_register_read(
    globus_xio_operation_t              op,
    globus_xio_system_socket_handle_t   handle,
    const globus_xio_iovec_t *          u_iov,
    int                                 u_iovc,
    globus_size_t                       waitforbytes,
    int                                 flags,
    globus_sockaddr_t *                 from,
    globus_xio_system_data_callback_t   callback,
    void *                              user_arg)
{
    return globus_l_xio_system_register_read(
        op,
        handle,
        -1,
        u_iov,
        u_iovc,
        waitforbytes,
        flags,
        from,
        callback,
        user_arg);
}

static
globus_result_t
globus_l_xio_system_register_write(
    globus_xio_operation_t              op,
    globus_l_xio_system_t *             handle,
    globus_off_t                        offset,
    const globus_xio_iovec_t *          u_iov,
    int                                 u_iovc,
    globus_size_t                       waitforbytes,
    int                                 flags,
    globus_sockaddr_t *                 to,
    globus_xio_system_data_callback_t   callback,
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_i_xio_system_op_info_t *     op_info;
    struct iovec *                      iov;
    int                                 fd = handle->fd;
    GlobusXIOName(globus_l_xio_system_register_write);

    GlobusXIOSystemDebugEnterFD(fd);
    GlobusXIOSystemDebugPrintf(
        GLOBUS_I_XIO_SYSTEM_DEBUG_DATA,
        (_XIOSL("[%s] Waiting for %u bytes\n"), _xio_name, (unsigned) waitforbytes));

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

    op_info->type = GLOBUS_I_XIO_SYSTEM_OP_WRITE;
    op_info->sop.data.start_iov = iov;
    op_info->sop.data.start_iovc = u_iovc;
    op_info->sop.data.iov = iov;
    op_info->sop.data.iovc = u_iovc;
    op_info->sop.data.addr = to;
    op_info->sop.data.flags = flags;

    op_info->state = GLOBUS_I_XIO_SYSTEM_OP_NEW;
    op_info->op = op;
    op_info->handle = handle;
    op_info->user_arg = user_arg;
    op_info->sop.data.callback = callback;
    op_info->waitforbytes = waitforbytes;
    op_info->offset = offset;

    result = globus_l_xio_system_register_write_fd(fd, op_info);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_system_register_write_fd", result);
        goto error_register;
    }

    /* handle could be destroyed by time we get here - no touch! */
    GlobusXIOSystemDebugExitFD(fd);
    return GLOBUS_SUCCESS;

error_register:
    GlobusIXIOSystemFreeIovec(u_iovc, iov);

error_iovec:
    GlobusIXIOSystemFreeOperation(op_info);

error_op_info:
    GlobusXIOSystemDebugExitWithErrorFD(fd);
    return result;
}

globus_result_t
globus_xio_system_file_register_write(
    globus_xio_operation_t              op,
    globus_xio_system_file_handle_t     handle,
    globus_off_t                        offset,
    const globus_xio_iovec_t *          u_iov,
    int                                 u_iovc,
    globus_size_t                       waitforbytes,
    globus_xio_system_data_callback_t   callback,
    void *                              user_arg)
{
    return globus_l_xio_system_register_write(
        op,
        handle,
        offset,
        u_iov,
        u_iovc,
        waitforbytes,
        0,
        GLOBUS_NULL,
        callback,
        user_arg);
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
    return globus_l_xio_system_register_write(
        op,
        handle,
        -1,
        u_iov,
        u_iovc,
        waitforbytes,
        flags,
        to,
        callback,
        user_arg);
}

static
globus_result_t
globus_l_xio_system_try_read(
    globus_l_xio_system_t *             handle,
    globus_off_t                        offset,
    const globus_xio_iovec_t *          iov,
    int                                 iovc,
    int                                 flags,
    globus_sockaddr_t *                 from,
    globus_size_t *                     nbytes)
{
    if(handle->type == GLOBUS_XIO_SYSTEM_FILE)
    {
        globus_result_t                 result;

        globus_mutex_lock(&handle->lock);
        {
            if(handle->file_position != offset &&
                (iovc > 1 || iov->iov_len > 0)) /* else select() mode */
            {
                GlobusXIOSystemDebugPrintf(
                    GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                    ("[globus_l_xio_system_try_read] fd=%d, "
                        "Changing file position to %" GLOBUS_OFF_T_FORMAT "\n",
                            handle->fd, offset));

                /* assume success as possible failures are ignorable */
                lseek(handle->fd, offset, SEEK_SET);
                handle->file_position = offset;
            }

            result = globus_i_xio_system_file_try_read(
                handle->fd, iov, iovc, nbytes);

            handle->file_position += *nbytes;
        }
        globus_mutex_unlock(&handle->lock);

        return result;
    }
    else
    {
        return globus_i_xio_system_socket_try_read(
            handle->fd, iov, iovc, flags, from, nbytes);
    }
}

static
globus_result_t
globus_l_xio_system_read(
    globus_l_xio_system_t *             handle,
    globus_off_t                        offset,
    const globus_xio_iovec_t *          u_iov,
    int                                 u_iovc,
    globus_size_t                       waitforbytes,
    int                                 flags,
    globus_sockaddr_t *                 from,
    globus_size_t *                     u_nbytes)
{
    globus_result_t                     result;
    int                                 rc;
    GlobusXIOName(globus_l_xio_system_read);

    GlobusXIOSystemDebugEnter();

    result = globus_l_xio_system_try_read(
        handle, offset, u_iov, u_iovc, flags, from, u_nbytes);

    if(result == GLOBUS_SUCCESS && *u_nbytes < waitforbytes)
    {
        struct iovec *                  iov;
        int                             iovc;
        globus_size_t                   nbytes = *u_nbytes;
        globus_size_t                   total = nbytes;

        /*
         * XXX this is not thread safe... both reads and writes are mucking
         * with blocking status.
         * worst case, we read 0 bytes in the loop below, return, and xio
         * calls us again to finish up.
         */
        rc = globus_l_xio_system_remove_nonblocking(handle);
        GlobusIXIOSystemAllocIovec(u_iovc, iov);
        if(!iov)
        {
            result = GlobusXIOErrorMemory("iov");
            goto error_iovec;
        }

        GlobusIXIOUtilTransferIovec(iov, u_iov, u_iovc);
        u_iov = iov;
        iovc = u_iovc;

        do
        {
            if(total > 0)
            {
                /* only capture source first time around */
                from = GLOBUS_NULL;
            }

            offset += nbytes;
            GlobusIXIOUtilAdjustIovec(iov, iovc, nbytes);
            result = globus_l_xio_system_try_read(
                handle, offset, iov, iovc, flags, from, &nbytes);
            total += nbytes;
        } while(result == GLOBUS_SUCCESS && nbytes && total < waitforbytes);

        *u_nbytes = total;

        GlobusIXIOSystemFreeIovec(u_iovc, (globus_xio_iovec_t *) u_iov);
        rc = globus_l_xio_system_add_nonblocking(handle);
    }

    GlobusXIOSystemDebugExit();
    return result;

error_iovec:
    rc = globus_l_xio_system_add_nonblocking(handle);
    GlobusXIOSystemDebugExitWithError();
    return result;
}

globus_result_t
globus_xio_system_file_read(
    globus_xio_system_file_handle_t     handle,
    globus_off_t                        offset,
    const globus_xio_iovec_t *          iov,
    int                                 iovc,
    globus_size_t                       waitforbytes,
    globus_size_t *                     nbytes)
{
    return globus_l_xio_system_read(
        handle, offset, iov, iovc, waitforbytes, 0, GLOBUS_NULL, nbytes);
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
    return globus_l_xio_system_read(
        handle, -1, iov, iovc, waitforbytes, flags, from, nbytes);
}

static
globus_result_t
globus_l_xio_system_try_write(
    globus_l_xio_system_t *             handle,
    globus_off_t                        offset,
    const globus_xio_iovec_t *          iov,
    int                                 iovc,
    int                                 flags,
    globus_sockaddr_t *                 to,
    globus_size_t *                     nbytes)
{
    if(handle->type == GLOBUS_XIO_SYSTEM_FILE)
    {
        globus_result_t                 result;

        globus_mutex_lock(&handle->lock);
        {
            if(handle->file_position != offset &&
                (iovc > 1 || iov->iov_len > 0)) /* else select() mode */
            {
                GlobusXIOSystemDebugPrintf(
                    GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
                    ("[globus_l_xio_system_try_write] fd=%d, "
                        "Changing file position to %" GLOBUS_OFF_T_FORMAT "\n",
                            handle->fd, offset));

                /* assume success as possible failures are ignorable */
                lseek(handle->fd, offset, SEEK_SET);
                handle->file_position = offset;
            }

            result = globus_i_xio_system_file_try_write(
                handle->fd, iov, iovc, nbytes);

            handle->file_position += *nbytes;
        }
        globus_mutex_unlock(&handle->lock);

        return result;
    }
    else
    {
        return globus_i_xio_system_socket_try_write(
            handle->fd, iov, iovc, flags, to, nbytes);
    }
}

static
globus_result_t
globus_l_xio_system_write(
    globus_l_xio_system_t *             handle,
    globus_off_t                        offset,
    const globus_xio_iovec_t *          u_iov,
    int                                 u_iovc,
    globus_size_t                       waitforbytes,
    int                                 flags,
    globus_sockaddr_t *                 to,
    globus_size_t *                     u_nbytes)
{
    globus_result_t                     result;
    int                                 rc;
    GlobusXIOName(globus_l_xio_system_write);

    GlobusXIOSystemDebugEnter();

    result = globus_l_xio_system_try_write(
        handle, offset, u_iov, u_iovc, flags, to, u_nbytes);

    if(result == GLOBUS_SUCCESS && *u_nbytes < waitforbytes)
    {
        struct iovec *                  iov;
        int                             iovc;
        globus_size_t                   nbytes = *u_nbytes;
        globus_size_t                   total = nbytes;

        /*
         * XXX this is not thread safe... both reads and writes are mucking
         * with blocking status
         */
        rc = globus_l_xio_system_remove_nonblocking(handle);
        GlobusIXIOSystemAllocIovec(u_iovc, iov);
        if(!iov)
        {
            result = GlobusXIOErrorMemory("iov");
            goto error_iovec;
        }

        GlobusIXIOUtilTransferIovec(iov, u_iov, u_iovc);
        u_iov = iov;
        iovc = u_iovc;

        do
        {
            offset += nbytes;
            GlobusIXIOUtilAdjustIovec(iov, iovc, nbytes);
            result = globus_l_xio_system_try_write(
                handle, offset, iov, iovc, flags, to, &nbytes);
            total += nbytes;
        } while(result == GLOBUS_SUCCESS && nbytes && total < waitforbytes);

        *u_nbytes = total;

        GlobusIXIOSystemFreeIovec(u_iovc, (globus_xio_iovec_t *) u_iov);
        rc = globus_l_xio_system_add_nonblocking(handle);
    }

    GlobusXIOSystemDebugExit();
    return result;

error_iovec:
    rc = globus_l_xio_system_add_nonblocking(handle);
    GlobusXIOSystemDebugExitWithError();
    return result;
}

globus_result_t
globus_xio_system_file_write(
    globus_xio_system_file_handle_t     handle,
    globus_off_t                        offset,
    const globus_xio_iovec_t *          iov,
    int                                 iovc,
    globus_size_t                       waitforbytes,
    globus_size_t *                     nbytes)
{
    return globus_l_xio_system_write(
        handle, offset, iov, iovc, waitforbytes, 0, GLOBUS_NULL, nbytes);
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
    return globus_l_xio_system_write(
        handle, -1, iov, iovc, waitforbytes, flags, to, nbytes);
}

static
globus_result_t
globus_l_xio_system_close(
    int                                 fd)
{
    globus_result_t                     result;
    int                                 rc;
    GlobusXIOName(globus_l_xio_system_close);

    GlobusXIOSystemDebugEnterFD(fd);

    do
    {
        rc = close(fd);
        GlobusXIOSystemUpdateErrno();
    } while(rc < 0 && errno == EINTR);

    if(rc < 0)
    {
        result = GlobusXIOErrorSystemError("close", errno);
        goto error_close;
    }

    GlobusXIOSystemDebugExitFD(fd);
    return GLOBUS_SUCCESS;

error_close:
    GlobusXIOSystemDebugExitWithErrorFD(fd);
    return result;
}

globus_off_t
globus_xio_system_file_get_position(
    globus_xio_system_file_t            fd)
{
    globus_off_t                        offset;
    GlobusXIOName(globus_xio_system_file_get_position);

    GlobusXIOSystemDebugEnterFD(fd);

    /* ignore errors, may be a pipe or other unseekable */
    offset = lseek(fd, 0, SEEK_CUR);
    if(offset == -1)
    {
        offset = 0;
    }

    GlobusXIOSystemDebugExitFD(fd);

    return offset;
}

globus_off_t
globus_xio_system_file_get_size(
    globus_xio_system_file_t            fd)
{
    globus_off_t                        size = -1;
    struct stat                         buf;
    GlobusXIOName(globus_xio_system_file_get_size);

    GlobusXIOSystemDebugEnterFD(fd);

    if(fstat(fd, &buf) == 0)
    {
        size = buf.st_size;
    }

    GlobusXIOSystemDebugExitFD(fd);

    return size;
}

globus_xio_system_file_t
globus_xio_system_convert_stdio(
    const char *                        stdio)
{
    if(strcmp(stdio, "stdin") == 0)
    {
        return fileno(stdin);
    }
    else if(strcmp(stdio, "stdout") == 0)
    {
        return fileno(stdout);
    }
    else if(strcmp(stdio, "stderr") == 0)
    {
        return fileno(stderr);
    }

    return GLOBUS_XIO_SYSTEM_INVALID_FILE;
}

globus_result_t
globus_xio_system_file_truncate(
    globus_xio_system_file_t            fd,
    globus_off_t                        size)
{
    globus_result_t                     result;
    int                                 rc;
    GlobusXIOName(globus_xio_system_file_truncate);

    GlobusXIOSystemDebugEnterFD(fd);

#ifdef TARGET_ARCH_ARM
    setErrno(EINVAL);

    result = GlobusXIOErrorSystemError("ftruncate", errno);
#else
    rc = ftruncate(fd, size);
    if(rc < 0)
    {
        GlobusXIOSystemUpdateErrno();
        result = GlobusXIOErrorSystemError("ftruncate", errno);
        goto error_truncate;
    }

    GlobusXIOSystemDebugExitFD(fd);
    return GLOBUS_SUCCESS;

error_truncate:
#endif
    GlobusXIOSystemDebugExitWithErrorFD(fd);
    return result;
}

globus_result_t
globus_xio_system_file_open(
    globus_xio_system_file_t *          fd,
    const char *                        filename,
    int                                 flags,
    unsigned long                       mode)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_file_open);

    *fd = -1;
    GlobusXIOSystemDebugEnterFD(*fd);

    do
    {
        *fd = open(filename, flags, mode);
        GlobusXIOSystemUpdateErrno();
    } while(*fd < 0 && errno == EINTR);

    if(*fd < 0)
    {
        result = GlobusXIOErrorSystemError("open", errno);
        goto error_open;
    }

    /* all handles created by me are closed on exec */
    fcntl(*fd, F_SETFD, FD_CLOEXEC);

    GlobusXIOSystemDebugPrintf(
        GLOBUS_I_XIO_SYSTEM_DEBUG_INFO,
        ("[%s] Opened file, %s fd=%d\n", _xio_name, filename, *fd));

    GlobusXIOSystemDebugExitFD(*fd);
    return GLOBUS_SUCCESS;

error_open:
    GlobusXIOSystemDebugExitWithErrorFD(*fd);
    return result;
}

globus_result_t
globus_xio_system_file_close(
    globus_xio_system_file_t            fd)
{
    return globus_l_xio_system_close(fd);
}

globus_result_t
globus_xio_system_socket_create(
    globus_xio_system_socket_t *        fd,
    int                                 domain,
    int                                 type,
    int                                 protocol)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_create);

    *fd = -1;
    GlobusXIOSystemDebugEnterFD(*fd);

    *fd = socket(domain, type, protocol);
    GlobusXIOSystemUpdateErrno();
    if(*fd == -1)
    {
        result = GlobusXIOErrorSystemError("socket", errno);
        goto error_socket;
    }

    /* all handles created by me are closed on exec */
    fcntl(*fd, F_SETFD, FD_CLOEXEC);

    GlobusXIOSystemDebugExitFD(*fd);
    return GLOBUS_SUCCESS;

error_socket:
    GlobusXIOSystemDebugExitWithErrorFD(*fd);
    return result;
}

globus_result_t
globus_xio_system_socket_setsockopt(
    globus_xio_system_socket_t          socket,
    int                                 level,
    int                                 optname,
    const void *                        optval,
    globus_socklen_t                    optlen)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_setsockopt);

    GlobusXIOSystemDebugEnterFD(socket);

    if(setsockopt(socket, level, optname, (void *) optval, optlen) < 0)
    {
        GlobusXIOSystemUpdateErrno();
        result = GlobusXIOErrorSystemError("setsockopt", errno);
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
    globus_socklen_t *                  optlen)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_getsockopt);

    GlobusXIOSystemDebugEnterFD(socket);

    if(getsockopt(socket, level, optname, optval, optlen) < 0)
    {
        GlobusXIOSystemUpdateErrno();
        result = GlobusXIOErrorSystemError("getsockopt", errno);
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
    globus_socklen_t *                  namelen)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_getsockname);

    GlobusXIOSystemDebugEnterFD(socket);

    if(getsockname(socket, name, namelen) < 0)
    {
        GlobusXIOSystemUpdateErrno();
        result = GlobusXIOErrorSystemError("getsockname", errno);
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
    globus_socklen_t *                  namelen)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_getpeername);

    GlobusXIOSystemDebugEnterFD(socket);

    if(getpeername(socket, name, namelen) < 0)
    {
        GlobusXIOSystemUpdateErrno();
        result = GlobusXIOErrorSystemError("getpeername", errno);
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
    globus_socklen_t                    addrlen)
{
    globus_result_t                     result;
    GlobusXIOName(globus_xio_system_socket_bind);

    GlobusXIOSystemDebugEnterFD(socket);

    if(bind(socket, addr, addrlen) < 0)
    {
        GlobusXIOSystemUpdateErrno();
        result = GlobusXIOErrorSystemError("bind", errno);
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

    if(listen(socket, backlog) < 0)
    {
        GlobusXIOSystemUpdateErrno();
        result = GlobusXIOErrorSystemError("listen", errno);
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
    globus_socklen_t                    addrlen)
{
    globus_result_t                     result;
    int                                 rc;
    GlobusXIOName(globus_xio_system_socket_connect);

    GlobusXIOSystemDebugEnterFD(socket);

    do
    {
        rc = connect(socket, addr, addrlen);
        GlobusXIOSystemUpdateErrno();
    } while(rc < 0 && errno == EINTR);

    if(rc < 0)
    {
        result = GlobusXIOErrorSystemError("connect", errno);
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
    return globus_l_xio_system_close(socket);
}
#endif /*TARGET_ARCH_WIN32*/
