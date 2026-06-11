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

#include "globus_xio_driver.h"
#include "globus_xio_pipe_driver.h"
#include "version.h"
#include <stdio.h>

GlobusDebugDefine(GLOBUS_XIO_PIPE);

#define GlobusXIOPipeDebugPrintf(level, message)                            \
    GlobusDebugPrintf(GLOBUS_XIO_PIPE, level, message)

#define GlobusXIOPipeDebugEnter()                                           \
    GlobusXIOPipeDebugPrintf(                                               \
        GLOBUS_L_XIO_PIPE_DEBUG_TRACE,                                      \
        (_XIOSL("[%s] Entering\n"), _xio_name))

#define GlobusXIOPipeDebugExit()                                            \
    GlobusXIOPipeDebugPrintf(                                               \
        GLOBUS_L_XIO_PIPE_DEBUG_TRACE,                                      \
        (_XIOSL("[%s] Exiting\n"), _xio_name))

#define GlobusXIOPipeDebugExitWithError()                                   \
    GlobusXIOPipeDebugPrintf(                                               \
        GLOBUS_L_XIO_PIPE_DEBUG_TRACE,                                      \
        (_XIOSL("[%s] Exiting with error\n"), _xio_name))

enum globus_l_xio_error_levels
{
    GLOBUS_L_XIO_PIPE_DEBUG_TRACE       = 1,
    GLOBUS_L_XIO_PIPE_DEBUG_INFO        = 2
};

static
int
globus_l_xio_pipe_activate(void);

static
int
globus_l_xio_pipe_deactivate(void);

GlobusXIODefineModule(pipe) =
{
    "globus_xio_pipe",
    globus_l_xio_pipe_activate,
    globus_l_xio_pipe_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

/*
 *  attribute structure
 */
typedef struct xio_l_pipe_attr_s
{
    globus_bool_t                       use_blocking_io;
    globus_xio_system_file_t            infd;
    globus_xio_system_file_t            outfd;
} xio_l_pipe_attr_t;

/* default attr */
#ifdef _WIN32
static xio_l_pipe_attr_t                xio_l_pipe_attr_default =
{
    GLOBUS_FALSE,
    NULL, /* Set in globus_l_xio_pipe_activate() */
    NULL  /* Set in globus_l_xio_pipe_activate() */
};
#else
static const xio_l_pipe_attr_t          xio_l_pipe_attr_default =
{
    GLOBUS_FALSE,
    STDIN_FILENO,
    STDOUT_FILENO
};
#endif

/*
 *  handle structure
 */
typedef struct xio_l_pipe_handle_s
{
    globus_xio_system_file_handle_t     in_system;
    globus_xio_system_file_handle_t     out_system;
    globus_xio_system_file_t            infd;
    globus_xio_system_file_t            outfd;
    globus_bool_t                       use_blocking_io;
    globus_mutex_t                      lock; /* only used to protect below */
} xio_l_pipe_handle_t;

static
globus_result_t
globus_l_xio_pipe_attr_init(
    void **                             out_attr)
{
    xio_l_pipe_attr_t *                 attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_pipe_attr_init);

    GlobusXIOPipeDebugEnter();
    /*
     *  create a file attr structure and intialize its values
     */
    attr = (xio_l_pipe_attr_t *) globus_malloc(sizeof(xio_l_pipe_attr_t));
    if(!attr)
    {
        result = GlobusXIOErrorMemory("attr");
        goto error_attr;
    }

    memcpy(attr, &xio_l_pipe_attr_default, sizeof(xio_l_pipe_attr_t));
    *out_attr = attr;

    GlobusXIOPipeDebugExit();
    return GLOBUS_SUCCESS;

error_attr:
    GlobusXIOPipeDebugExitWithError();
    return result;
}

/*
 *  copy an attribute structure
 */
static
globus_result_t
globus_l_xio_pipe_attr_copy(
    void **                             dst,
    void *                              src)
{
    xio_l_pipe_attr_t *                 attr;
    xio_l_pipe_attr_t *                 src_attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_pipe_attr_copy);

    GlobusXIOPipeDebugEnter();

    src_attr = (xio_l_pipe_attr_t *) src;
    attr = (xio_l_pipe_attr_t *) globus_malloc(sizeof(xio_l_pipe_attr_t));
    if(!attr)
    {
        result = GlobusXIOErrorMemory("attr");
        goto error_attr;
    }

    memcpy(attr, src_attr, sizeof(xio_l_pipe_attr_t));
    *dst = attr;

    GlobusXIOPipeDebugExit();
    return GLOBUS_SUCCESS;

error_attr:
    GlobusXIOPipeDebugExitWithError();
    return result;
}

static
globus_result_t
globus_l_xio_pipe_attr_cntl(
    void *                              driver_attr,
    int                                 cmd,
    va_list                             ap)
{
    xio_l_pipe_attr_t *                 attr;
    GlobusXIOName(globus_l_xio_pipe_attr_cntl);

    GlobusXIOPipeDebugEnter();

    attr = (xio_l_pipe_attr_t *) driver_attr;

    switch(cmd)
    {
        case GLOBUS_XIO_PIPE_SET_BLOCKING_IO:
            attr->use_blocking_io = va_arg(ap, globus_bool_t);
            break;
        case GLOBUS_XIO_PIPE_SET_IN_HANDLE:
            attr->infd = va_arg(ap, globus_xio_system_file_t);
            break;
        case GLOBUS_XIO_PIPE_SET_OUT_HANDLE:
            attr->outfd = va_arg(ap, globus_xio_system_file_t);
            break;

        default:
            break;
    }

    GlobusXIOPipeDebugExit();
    return GLOBUS_SUCCESS;
}

static
globus_result_t
globus_l_xio_pipe_attr_destroy(
    void *                              driver_attr)
{
    GlobusXIOName(globus_l_xio_pipe_attr_destroy);

    GlobusXIOPipeDebugEnter();

    globus_free(driver_attr);

    GlobusXIOPipeDebugExit();
    return GLOBUS_SUCCESS;
}




static
globus_result_t
globus_l_xio_pipe_handle_init(
    xio_l_pipe_handle_t **              handle,
    xio_l_pipe_attr_t *                 attr)
{
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_pipe_handle_init);

    GlobusXIOPipeDebugEnter();

    *handle = (xio_l_pipe_handle_t *)
        globus_calloc(1, sizeof(xio_l_pipe_handle_t));
    if(!*handle)
    {
        result = GlobusXIOErrorMemory("handle");
        goto error_handle;
    }

    globus_mutex_init(&(*handle)->lock, NULL);
    (*handle)->use_blocking_io = attr->use_blocking_io;
    (*handle)->infd = attr->infd;
    (*handle)->outfd = attr->outfd;

    GlobusXIOPipeDebugExit();
    return GLOBUS_SUCCESS;

error_handle:
    GlobusXIOPipeDebugExitWithError();
    return result;
}

static
void
globus_l_xio_pipe_handle_destroy(
    xio_l_pipe_handle_t *               handle)
{
    GlobusXIOName(globus_l_xio_pipe_handle_destroy);

    GlobusXIOPipeDebugEnter();

    globus_mutex_destroy(&handle->lock);
    globus_free(handle);

    GlobusXIOPipeDebugExit();
}


/*
 *  open a file
 */
static
globus_result_t
globus_l_xio_pipe_open(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    xio_l_pipe_handle_t *               handle;
    xio_l_pipe_attr_t *                 attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_pipe_open);

    GlobusXIOPipeDebugEnter();

    attr = (xio_l_pipe_attr_t *)
        (driver_attr ? driver_attr : &xio_l_pipe_attr_default);
    result = globus_l_xio_pipe_handle_init(&handle, attr);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_pipe_handle_init", result);
        goto error_handle;
    }

    result = globus_xio_system_file_init(&handle->out_system, handle->outfd);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_xio_system_file_init", result);
        goto error_init;
    }
    result = globus_xio_system_file_init(&handle->in_system, handle->infd);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_xio_system_file_init", result);
        goto error_init;
    }

    globus_xio_driver_finished_open(handle, op, GLOBUS_SUCCESS);

    GlobusXIOPipeDebugExit();
    return GLOBUS_SUCCESS;

error_init:
    globus_l_xio_pipe_handle_destroy(handle);
error_handle:
    GlobusXIOPipeDebugExitWithError();
    return result;
}

/*
 *  close a file
 */
static
globus_result_t
globus_l_xio_pipe_close(
    void *                              driver_specific_handle,
    void *                              attr,
    globus_xio_operation_t              op)
{
    xio_l_pipe_handle_t *               handle;
    GlobusXIOName(globus_l_xio_pipe_close);

    GlobusXIOPipeDebugEnter();

    handle = (xio_l_pipe_handle_t *) driver_specific_handle;

    globus_xio_system_file_destroy(handle->in_system);
    globus_xio_system_file_destroy(handle->out_system);

    globus_xio_system_file_close(handle->infd);
    globus_xio_system_file_close(handle->outfd);

    globus_xio_driver_finished_close(op, GLOBUS_SUCCESS);
    globus_l_xio_pipe_handle_destroy(handle);

    GlobusXIOPipeDebugExit();
    return GLOBUS_SUCCESS;
}

static
void
globus_l_xio_pipe_system_read_cb(
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_xio_operation_t              op;
    GlobusXIOName(globus_l_xio_pipe_system_read_cb);

    GlobusXIOPipeDebugEnter();

    op = (globus_xio_operation_t) user_arg;

    globus_xio_driver_finished_read(op, result, nbytes);

    GlobusXIOPipeDebugExit();
}

/*
 *  read from a file
 */
static
globus_result_t
globus_l_xio_pipe_read(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    xio_l_pipe_handle_t *               handle;
    globus_size_t                       nbytes;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_pipe_read);

    GlobusXIOPipeDebugEnter();

    handle = (xio_l_pipe_handle_t *) driver_specific_handle;

    /* if buflen and waitfor are both 0, we behave like register select */
    if((globus_xio_operation_get_wait_for(op) == 0 &&
        (iovec_count > 1 || iovec[0].iov_len > 0)) ||
        (handle->use_blocking_io &&
        globus_xio_driver_operation_is_blocking(op)))
    {
        result = globus_xio_system_file_read(
            handle->in_system,
            0,
            iovec,
            iovec_count,
            globus_xio_operation_get_wait_for(op),
            &nbytes);

        globus_xio_driver_finished_read(op, result, nbytes);
        result = GLOBUS_SUCCESS;
    }
    else
    {
        result = globus_xio_system_file_register_read(
            op,
            handle->in_system,
            0,
            iovec,
            iovec_count,
            globus_xio_operation_get_wait_for(op),
            globus_l_xio_pipe_system_read_cb,
            op);
    }

    GlobusXIOPipeDebugExit();
    return result;
}

static
void
globus_l_xio_pipe_system_write_cb(
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_xio_operation_t              op;
    GlobusXIOName(globus_l_xio_pipe_system_write_cb);

    GlobusXIOPipeDebugEnter();

    op = (globus_xio_operation_t) user_arg;

    globus_xio_driver_finished_write(op, result, nbytes);

    GlobusXIOPipeDebugExit();
}

/*
 *  write to a file
 */
static
globus_result_t
globus_l_xio_pipe_write(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    xio_l_pipe_handle_t *               handle;
    globus_size_t                       nbytes;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_pipe_write);

    GlobusXIOPipeDebugEnter();

    handle = (xio_l_pipe_handle_t *) driver_specific_handle;

    /* if buflen and waitfor are both 0, we behave like register select */
    if((globus_xio_operation_get_wait_for(op) == 0 &&
        (iovec_count > 1 || iovec[0].iov_len > 0)) ||
        (handle->use_blocking_io &&
        globus_xio_driver_operation_is_blocking(op)))
    {
        result = globus_xio_system_file_write(
            handle->out_system,
            0,
            iovec,
            iovec_count,
            globus_xio_operation_get_wait_for(op),
            &nbytes);

        globus_xio_driver_finished_write(op, result, nbytes);
        result = GLOBUS_SUCCESS;
    }
    else
    {
        result = globus_xio_system_file_register_write(
            op,
            handle->out_system,
            0,
            iovec,
            iovec_count,
            globus_xio_operation_get_wait_for(op),
            globus_l_xio_pipe_system_write_cb,
            op);
    }

    GlobusXIOPipeDebugExit();
    return result;
}

static globus_xio_string_cntl_table_t pipe_l_string_opts_table[] =

{
    {"blocking", GLOBUS_XIO_PIPE_SET_BLOCKING_IO,
        globus_xio_string_cntl_bool},
    {0}
};


static
globus_result_t
globus_l_xio_pipe_init(
    globus_xio_driver_t *               out_driver)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_pipe_init);

    GlobusXIOPipeDebugEnter();

    /* I dont support any driver options, so I'll ignore the ap */

    result = globus_xio_driver_init(&driver, "file", GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_pipe_init", result);
        goto error_init;
    }

    globus_xio_driver_set_transport(
        driver,
        globus_l_xio_pipe_open,
        globus_l_xio_pipe_close,
        globus_l_xio_pipe_read,
        globus_l_xio_pipe_write,
        NULL);

    globus_xio_driver_set_attr(
        driver,
        globus_l_xio_pipe_attr_init,
        globus_l_xio_pipe_attr_copy,
        globus_l_xio_pipe_attr_cntl,
        globus_l_xio_pipe_attr_destroy);

    globus_xio_driver_string_cntl_set_table(
        driver,
        pipe_l_string_opts_table);

    *out_driver = driver;

    GlobusXIOPipeDebugExit();
    return GLOBUS_SUCCESS;

error_init:
    GlobusXIOPipeDebugExitWithError();
    return result;
}

static
void
globus_l_xio_pipe_destroy(
    globus_xio_driver_t                 driver)
{
    GlobusXIOName(globus_l_xio_pipe_destroy);

    GlobusXIOPipeDebugEnter();

    globus_xio_driver_destroy(driver);

    GlobusXIOPipeDebugExit();
}

GlobusXIODefineDriver(
    pipe,
    globus_l_xio_pipe_init,
    globus_l_xio_pipe_destroy);

static
int
globus_l_xio_pipe_activate(void)
{
    int                                 rc;

    GlobusXIOName(globus_l_xio_pipe_activate);

    GlobusDebugInit(GLOBUS_XIO_PIPE, TRACE INFO);

    GlobusXIOPipeDebugEnter();

    rc = globus_module_activate(GLOBUS_XIO_SYSTEM_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
        goto error_activate;
    }
#   ifdef _WIN32
    xio_l_pipe_attr_default.infd = GetStdHandle(STD_INPUT_HANDLE);
    xio_l_pipe_attr_default.outfd = GetStdHandle(STD_OUTPUT_HANDLE);
#   endif

    GlobusXIORegisterDriver(pipe);

    GlobusXIOPipeDebugExit();
    return GLOBUS_SUCCESS;

error_activate:
    GlobusXIOPipeDebugExitWithError();
    GlobusDebugDestroy(GLOBUS_XIO_PIPE);
    return rc;
}

static
int
globus_l_xio_pipe_deactivate(void)
{
    GlobusXIOName(globus_l_xio_pipe_deactivate);

    GlobusXIOPipeDebugEnter();

    GlobusXIOUnRegisterDriver(pipe);
    globus_module_deactivate(GLOBUS_XIO_SYSTEM_MODULE);

    GlobusXIOPipeDebugExit();
    GlobusDebugDestroy(GLOBUS_XIO_PIPE);

    return GLOBUS_SUCCESS;
}
