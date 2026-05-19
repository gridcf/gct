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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

#include "globus_io.h"
#ifndef TARGET_ARCH_WIN32
#include <arpa/inet.h>
#endif
#include <gssapi.h>
#include <globus_error_gssapi.h>

#ifdef __GNUC__
#define GlobusIOName(func) static const char * _io_name __attribute__((__unused__)) = #func
#else
#define GlobusIOName(func) static const char * _io_name = #func
#endif

#define GlobusLIOCheckHandle(handle, _type)                                 \
    do                                                                      \
    {                                                                       \
        if(!(handle) || !*(handle))                                         \
        {                                                                   \
            return globus_error_put(                                        \
                globus_io_error_construct_null_parameter(                   \
                    GLOBUS_IO_MODULE,                                       \
                    GLOBUS_NULL,                                            \
                    #handle,                                                \
                    1,                                                      \
                    (char *) _io_name));                                    \
        }                                                                   \
                                                                            \
        if((_type) && !((*(handle))->type & (_type)))                       \
        {                                                                   \
            return globus_error_put(                                        \
                globus_io_error_construct_bad_pointer(                      \
                    GLOBUS_IO_MODULE,                                       \
                    GLOBUS_NULL,                                            \
                    #handle,                                                \
                    1,                                                      \
                    (char *) _io_name));                                    \
        }                                                                   \
    } while(0)

#define GlobusLIOCheckAttr(attr, types)                                     \
    do                                                                      \
    {                                                                       \
        globus_result_t                 _result;                            \
                                                                            \
        _result = globus_l_io_attr_check(                                   \
            (attr),                                                         \
            (types),                                                        \
            _io_name);                                                      \
        if(_result != GLOBUS_SUCCESS)                                       \
        {                                                                   \
            return _result;                                                 \
        }                                                                   \
    } while(0)

#define GlobusLIOCheckNullParam(arg)                                        \
    if(!(arg))                                                              \
        return globus_error_put(                                            \
            globus_io_error_construct_null_parameter(                       \
                GLOBUS_IO_MODULE,                                           \
                GLOBUS_NULL,                                                \
                #arg,                                                       \
                1,                                                          \
                (char *) _io_name))

#define GlobusLIOMalloc(pointer, type)                                      \
    ((pointer = (type *) globus_calloc(1, sizeof(type)))                    \
        ? (GLOBUS_SUCCESS)                                                  \
        : (globus_error_put(                                                \
            globus_io_error_construct_system_failure(                       \
                GLOBUS_IO_MODULE,                                           \
                GLOBUS_NULL,                                                \
                GLOBUS_NULL,                                                \
                errno))))

#define GlobusLIOMallocSize(__pointer, __size)                              \
    ((__pointer = globus_calloc(1, __size))                                 \
        ? (GLOBUS_SUCCESS)                                                  \
        : (globus_error_put(                                                \
            globus_io_error_construct_system_failure(                       \
                GLOBUS_IO_MODULE,                                           \
                GLOBUS_NULL,                                                \
                GLOBUS_NULL,                                                \
                errno))))


#define GlobusLIOErrorWrapGSSFailed(failed_func, major_status, minor_status) \
    globus_error_put(                                                        \
        globus_error_wrap_gssapi_error(                                      \
            GLOBUS_IO_MODULE,                                                \
            major_status,                                                    \
            minor_status,                                                    \
            2,                                                               \
            __FILE__,                                                        \
            _io_name,                                                        \
            __LINE__,                                                        \
            "%s failed.",                                                    \
            (failed_func)))


typedef enum
{
    GLOBUS_I_IO_FILE_ATTR = 1,
    GLOBUS_I_IO_TCP_ATTR  = 2
} globus_l_io_attr_type_t;

typedef enum
{
    GLOBUS_I_IO_FILE_HANDLE = 1,
    GLOBUS_I_IO_TCP_HANDLE  = 2
} globus_l_io_handle_type_t;

typedef struct globus_l_io_delegation_cb_arg_s
{
    void *                              user_arg;
    globus_io_handle_t *                handle;
    globus_io_delegation_callback_t     callback;
} globus_l_io_delegation_cb_arg_t;

typedef struct globus_l_io_secure_authorization_data_s
{
    gss_name_t                                  identity;
    globus_io_secure_authorization_callback_t   callback;
    void *                                      callback_arg;
} globus_l_io_secure_authorization_data_t;

typedef struct globus_l_io_attr_s
{
    globus_l_io_attr_type_t                     type;
    globus_xio_attr_t                           attr;
    int                                         file_flags;
    globus_bool_t                               allow_ipv6;
    globus_io_secure_authentication_mode_t      authentication_mode;
    globus_io_secure_authorization_mode_t       authorization_mode;
    globus_io_secure_channel_mode_t             channel_mode;
    globus_l_io_secure_authorization_data_t     authz_data;
    globus_callback_space_t                     space;
    globus_xio_stack_t                          stack;
} globus_l_io_attr_t;

typedef struct globus_l_io_handle_s
{
    globus_l_io_handle_type_t                   type;
    int                                         refs;
    globus_io_handle_t *                        io_handle;
    globus_xio_handle_t                         xio_handle;
    globus_callback_space_t                     space;

    globus_list_t *                             pending_ops;
    globus_mutex_t                              pending_lock;
    void *                                      user_pointer;
    globus_io_attr_t                            attr;

    /* used only for listener */
    globus_xio_server_t                         xio_server;
    globus_xio_handle_t                         accepted_handle;
} globus_l_io_handle_t;

typedef struct
{
    globus_bool_t                       done;
    globus_object_t *                   error;
    globus_mutex_t                      lock;
    globus_cond_t                       cond;
} globus_l_io_monitor_t;

typedef struct
{
    globus_io_handle_t *                handle;
    int                                 refs;
    globus_bool_t                       perform_callbacks;
    globus_io_callback_t                callback;
    void *                              user_arg;
    globus_bool_t                       blocking;
} globus_l_io_cancel_info_t;

typedef struct
{
    globus_l_io_handle_t *              handle;
    union
    {
        globus_io_callback_t            non_io;
        globus_io_read_callback_t       read_write;
        globus_io_writev_callback_t     writev;
    } cb;
    void *                              user_arg;
    globus_bool_t                       blocking;
    globus_l_io_cancel_info_t *         cancel_info;
    globus_object_t *                   error;
    globus_byte_t *                     buffer;
    globus_xio_iovec_t *                iov;
    int                                 iovc;
    globus_size_t                       nbytes;
} globus_l_io_bounce_t;

static globus_xio_driver_t              globus_l_io_file_driver;
static globus_xio_driver_t              globus_l_io_tcp_driver;
static globus_xio_driver_t              globus_l_io_gsi_driver;
static globus_xio_stack_t               globus_l_io_file_stack;
static globus_xio_stack_t               globus_l_io_tcp_stack;
static globus_xio_stack_t               globus_l_io_gsi_stack;
static globus_reltime_t                 globus_l_io_open_timeout =
{
    90,  /* 1.5 minutes */
    0,
};

static
int
globus_l_io_activate(void);

static
int
globus_l_io_deactivate(void);

#include "version.h"

globus_module_descriptor_t              globus_l_io_module =
{
    "globus_io",
    globus_l_io_activate,
    globus_l_io_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

static
int
globus_l_io_activate(void)
{
    globus_result_t                     result;

    if(globus_module_activate(GLOBUS_XIO_MODULE) != GLOBUS_SUCCESS)
    {
        goto error_activate;
    }

    if(globus_xio_driver_load(
        "file", &globus_l_io_file_driver) != GLOBUS_SUCCESS)
    {
        goto error_load_file;
    }

    if(globus_xio_driver_load(
        "tcp", &globus_l_io_tcp_driver) != GLOBUS_SUCCESS)
    {
        goto error_load_tcp;
    }

    if(globus_xio_driver_load(
        "gsi", &globus_l_io_gsi_driver) != GLOBUS_SUCCESS)
    {
        goto error_load_gsi;
    }

    result = globus_xio_stack_init(&globus_l_io_file_stack, GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_file_stack;
    }

    result = globus_xio_stack_push_driver(
        globus_l_io_file_stack, globus_l_io_file_driver);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_file_push;
    }

    result = globus_xio_stack_init(&globus_l_io_tcp_stack, GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_tcp_stack;
    }

    result = globus_xio_stack_push_driver(
        globus_l_io_tcp_stack, globus_l_io_tcp_driver);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_tcp_push;
    }

    result = globus_xio_stack_init(&globus_l_io_gsi_stack, GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_gsi_stack;
    }

    result = globus_xio_stack_push_driver(
        globus_l_io_gsi_stack, globus_l_io_tcp_driver);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_gsi_push;
    }

    result = globus_xio_stack_push_driver(
        globus_l_io_gsi_stack, globus_l_io_gsi_driver);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_gsi_push;
    }

    return GLOBUS_SUCCESS;

error_gsi_push:
    globus_xio_stack_destroy(globus_l_io_gsi_stack);

error_gsi_stack:
error_tcp_push:
    globus_xio_stack_destroy(globus_l_io_tcp_stack);

error_tcp_stack:
error_file_push:
    globus_xio_stack_destroy(globus_l_io_file_stack);

error_file_stack:
    globus_xio_driver_unload(globus_l_io_gsi_driver);

error_load_gsi:
    globus_xio_driver_unload(globus_l_io_tcp_driver);

 error_load_tcp:
    globus_xio_driver_unload(globus_l_io_file_driver);

error_load_file:
    globus_module_deactivate(GLOBUS_XIO_MODULE);

error_activate:
    return GLOBUS_FAILURE;
}

static
int
globus_l_io_deactivate(void)
{
    globus_xio_stack_destroy(globus_l_io_gsi_stack);
    globus_xio_stack_destroy(globus_l_io_tcp_stack);
    globus_xio_stack_destroy(globus_l_io_file_stack);

    globus_xio_driver_unload(globus_l_io_gsi_driver);
    globus_xio_driver_unload(globus_l_io_tcp_driver);
    globus_xio_driver_unload(globus_l_io_file_driver);

    return globus_module_deactivate(GLOBUS_XIO_MODULE);
}

static
globus_result_t
globus_l_io_handle_init(
    globus_l_io_handle_t **             _ihandle,
    globus_io_handle_t *                io_handle,
    globus_l_io_handle_type_t           type,
    globus_callback_space_t             space)
{
    globus_result_t                     result;
    globus_l_io_handle_t *              ihandle;
    GlobusIOName(globus_l_io_handle_init);

    result = GlobusLIOMalloc(ihandle, globus_l_io_handle_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_alloc;
    }

    ihandle->type               = type;
    ihandle->refs               = 1;
    ihandle->io_handle          = io_handle;
    ihandle->xio_handle         = GLOBUS_NULL;
    ihandle->xio_server         = GLOBUS_NULL;
    ihandle->space              = space;
    ihandle->accepted_handle    = GLOBUS_NULL;
    ihandle->attr               = GLOBUS_NULL;
    ihandle->pending_ops        = GLOBUS_NULL;
    ihandle->user_pointer       = GLOBUS_NULL;
    globus_mutex_init(&ihandle->pending_lock, GLOBUS_NULL);
    globus_callback_space_reference(ihandle->space);

    *_ihandle = ihandle;

    return GLOBUS_SUCCESS;

error_alloc:
    return result;
}

static
void
globus_l_io_handle_destroy(
    globus_l_io_handle_t *              ihandle)
{
    globus_bool_t                       destroy;
    GlobusIOName(globus_l_io_handle_destroy);

    globus_mutex_lock(&ihandle->pending_lock);
    {
        if(--ihandle->refs == 0)
        {
            destroy = GLOBUS_TRUE;
        }
        else
        {
            destroy = GLOBUS_FALSE;
        }
    }
    globus_mutex_unlock(&ihandle->pending_lock);

    if(destroy)
    {
        if(ihandle->attr)
        {
            if(ihandle->attr->type == GLOBUS_I_IO_TCP_ATTR)
            {
                globus_io_tcpattr_destroy(&ihandle->attr);
            }
            else if(ihandle->attr->type == GLOBUS_I_IO_FILE_ATTR)
            {
                globus_io_fileattr_destroy(&ihandle->attr);
            }
        }
        if(ihandle->accepted_handle)
        {
            globus_xio_close(ihandle->accepted_handle, GLOBUS_NULL);
        }
        globus_callback_space_destroy(ihandle->space);
        globus_mutex_destroy(&ihandle->pending_lock);
        globus_free(ihandle);
    }
}

static
globus_result_t
globus_l_io_attr_check(
    globus_io_attr_t *                  attr,
    int                                 types,
    const char *                        func_name)
{
    globus_l_io_attr_t *                iattr;

    if(!attr)
    {
        return globus_error_put(
            globus_io_error_construct_null_parameter(
                GLOBUS_IO_MODULE,
                GLOBUS_NULL,
                "attr",
                1,
                (char *) func_name));
    }

    iattr = (globus_l_io_attr_t *) *attr;

    if(!iattr || !(iattr->type & types))
    {
        return globus_error_put(
            globus_io_error_construct_bad_pointer(
                GLOBUS_IO_MODULE,
                GLOBUS_NULL,
                "attr",
                1,
                (char *) func_name));
    }

    return GLOBUS_SUCCESS;
}

static
globus_result_t
globus_l_io_iattr_copy(
    globus_io_attr_t *                  dest,
    globus_io_attr_t *                  source)
{
    globus_l_io_attr_t *                source_iattr;
    globus_l_io_attr_t *                dest_iattr;
    globus_io_secure_authorization_data_t data;
    globus_result_t                     result;
    GlobusIOName(globus_l_io_iattr_copy);

    source_iattr = (globus_l_io_attr_t *) *source;

    result = GlobusLIOMalloc(dest_iattr, globus_l_io_attr_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_alloc;
    }

    memset(dest_iattr, 0, sizeof(globus_l_io_attr_t));

    dest_iattr->type = source_iattr->type;
    if(dest_iattr->type == GLOBUS_I_IO_TCP_ATTR)
    {
        dest_iattr->allow_ipv6 = source_iattr->allow_ipv6;
        dest_iattr->authentication_mode = source_iattr->authentication_mode;
        dest_iattr->channel_mode = source_iattr->channel_mode;

        result = globus_io_attr_get_secure_authorization_mode(
            &source_iattr,
            &dest_iattr->authorization_mode,
            &data);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_auth_copy;
        }

        dest_iattr->authz_data.identity = data->identity;
        dest_iattr->authz_data.callback = data->callback;
        dest_iattr->authz_data.callback_arg = data->callback_arg;
        globus_free(data);
    }
    else
    {
        globus_assert(dest_iattr->type == GLOBUS_I_IO_FILE_ATTR);
        dest_iattr->file_flags = source_iattr->file_flags;
    }

    result = globus_xio_attr_copy(&dest_iattr->attr, source_iattr->attr);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_xio_copy;
    }

    if(source_iattr->stack != NULL)
    {
        result = globus_xio_stack_copy(&dest_iattr->stack, source_iattr->stack);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_xio_stack_copy;
        }
    }
    dest_iattr->space = source_iattr->space;
    globus_callback_space_reference(dest_iattr->space);

    *dest = dest_iattr;
    return GLOBUS_SUCCESS;

error_xio_stack_copy:
    globus_xio_attr_destroy(dest_iattr->attr);
error_xio_copy:
    if(dest_iattr->authz_data.identity != GSS_C_NO_NAME)
    {
        OM_uint32                       minor_status;

        gss_release_name(&minor_status, &dest_iattr->authz_data.identity);
    }

error_auth_copy:
    globus_free(dest_iattr);

error_alloc:
    *dest = GLOBUS_NULL;
    return result;
}

/* called locked */
static
void
globus_l_io_cancel_insert(
    globus_l_io_bounce_t *              bounce_info)
{
    bounce_info->handle->refs++;
    globus_list_insert(&bounce_info->handle->pending_ops, bounce_info);
}

static
globus_bool_t
globus_l_io_cancel_precallback(
    globus_l_io_bounce_t *              bounce_info)
{
    globus_l_io_handle_t *              ihandle;
    globus_bool_t                       perform_callback;

    ihandle = bounce_info->handle;

    globus_mutex_lock(&ihandle->pending_lock);
    {
        perform_callback = GLOBUS_TRUE;
        if(bounce_info->cancel_info)
        {
            if(!bounce_info->blocking &&
                !bounce_info->cancel_info->perform_callbacks)
            {
                perform_callback = GLOBUS_FALSE;
            }
        }
        else
        {
            globus_list_remove(&ihandle->pending_ops,
                globus_list_search(ihandle->pending_ops, bounce_info));
        }
    }
    globus_mutex_unlock(&ihandle->pending_lock);

    return perform_callback;
}

static
void
globus_l_io_cancel_kickout(
    void *                              user_arg)
{
    globus_l_io_cancel_info_t *         cancel_info;

    cancel_info = (globus_l_io_cancel_info_t *) user_arg;

    if(cancel_info->callback)
    {
        cancel_info->callback(
            cancel_info->user_arg,
            cancel_info->handle,
            GLOBUS_SUCCESS);
    }

    globus_free(cancel_info);
}

static
void
globus_l_io_cancel_complete(
    globus_l_io_bounce_t *              bounce_info)
{
    globus_l_io_handle_t *              ihandle;
    globus_l_io_cancel_info_t *         cancel_info;
    globus_bool_t                       call_cancel;
    GlobusIOName(globus_l_io_cancel_complete);

    ihandle = bounce_info->handle;
    cancel_info = bounce_info->cancel_info;

    globus_mutex_lock(&ihandle->pending_lock);
    {
        if(cancel_info && --cancel_info->refs == 0)
        {
            call_cancel = GLOBUS_TRUE;
        }
        else
        {
            call_cancel = GLOBUS_FALSE;
        }
    }
    globus_mutex_unlock(&ihandle->pending_lock);

    if(call_cancel)
    {
        globus_callback_space_t         space;

        if(ihandle->space != GLOBUS_CALLBACK_GLOBAL_SPACE &&
            globus_callback_space_get(&space) == GLOBUS_SUCCESS && (
            (cancel_info->blocking && space != GLOBUS_CALLBACK_GLOBAL_SPACE) ||
            (!cancel_info->blocking && space != ihandle->space)))
        {
            globus_result_t             result;

            result = globus_callback_space_register_oneshot(
                GLOBUS_NULL,
                GLOBUS_NULL,
                globus_l_io_cancel_kickout,
                cancel_info,
                cancel_info->blocking
                    ? GLOBUS_CALLBACK_GLOBAL_SPACE : ihandle->space);
            if(result != GLOBUS_SUCCESS)
            {
                globus_panic(
                    GLOBUS_IO_MODULE,
                    result,
                    _IOSL("[%s:%d] Couldn't register callback"),
                    _io_name,
                    __LINE__);
            }
        }
        else
        {
            if(cancel_info->callback)
            {
                cancel_info->callback(
                    cancel_info->user_arg,
                    cancel_info->handle,
                    GLOBUS_SUCCESS);
            }

            globus_free(cancel_info);
        }
    }

    /* removes reference added in cancel_insert and destroys if necessary */
    globus_l_io_handle_destroy(ihandle);
}

static
globus_bool_t
globus_l_io_should_bounce(
    globus_l_io_bounce_t *              bounce_info)
{
    globus_callback_space_t             space;

    if(bounce_info->handle->space != GLOBUS_CALLBACK_GLOBAL_SPACE &&
        globus_callback_space_get(&space) == GLOBUS_SUCCESS &&
        ((bounce_info->blocking && space != GLOBUS_CALLBACK_GLOBAL_SPACE) ||
        (!bounce_info->blocking && space != bounce_info->handle->space)))
    {
        return GLOBUS_TRUE;
    }

    return GLOBUS_FALSE;
}

static
void
globus_l_io_bounce_io_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

static
void
globus_l_io_bounce_io_kickout(
    void *                              user_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    GlobusIOName(globus_l_io_bounce_io_kickout);

    bounce_info = (globus_l_io_bounce_t *) user_arg;
    globus_l_io_bounce_io_cb(
        GLOBUS_NULL,
        bounce_info->error
            ? globus_error_put(bounce_info->error) : GLOBUS_SUCCESS,
        bounce_info->buffer,
        0,
        bounce_info->nbytes,
        GLOBUS_NULL,
        bounce_info);
}

static
void
globus_l_io_bounce_io_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    globus_l_io_handle_t *              ihandle;
    GlobusIOName(globus_l_io_bounce_io_cb);

    bounce_info = (globus_l_io_bounce_t *) user_arg;
    ihandle = bounce_info->handle;

    if(globus_l_io_should_bounce(bounce_info))
    {
        bounce_info->error = result == GLOBUS_SUCCESS
            ? GLOBUS_NULL : globus_error_get(result);
        bounce_info->buffer = buffer;
        bounce_info->nbytes = nbytes;
        result = globus_callback_space_register_oneshot(
            GLOBUS_NULL,
            GLOBUS_NULL,
            globus_l_io_bounce_io_kickout,
            bounce_info,
            bounce_info->handle->space);
        if(result != GLOBUS_SUCCESS)
        {
            globus_panic(
                GLOBUS_IO_MODULE,
                result,
                _IOSL("[%s:%d] Couldn't register callback"),
                _io_name,
                __LINE__);
        }
        return;
    }

    if(result != GLOBUS_SUCCESS)
    {
        if(globus_xio_error_is_eof(result))
        {
            result = globus_error_put(
                globus_io_error_construct_eof(
                    GLOBUS_IO_MODULE,
                    globus_error_get(result),
                    ihandle->io_handle));
        }
        else if(globus_xio_error_is_canceled(result))
        {
            result = globus_error_put(
                globus_io_error_construct_io_cancelled(
                    GLOBUS_IO_MODULE,
                    globus_error_get(result),
                    ihandle->io_handle));
        }
    }

    if(globus_l_io_cancel_precallback(bounce_info))
    {
        bounce_info->cb.read_write(
            bounce_info->user_arg,
            ihandle->io_handle,
            result,
            buffer,
            nbytes);
    }

    globus_l_io_cancel_complete(bounce_info);
    globus_free(bounce_info);
}

static
void
globus_l_io_bounce_iovec_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

static
void
globus_l_io_bounce_iovec_kickout(
    void *                              user_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    GlobusIOName(globus_l_io_bounce_iovec_kickout);

    bounce_info = (globus_l_io_bounce_t *) user_arg;
    globus_l_io_bounce_iovec_cb(
        GLOBUS_NULL,
        bounce_info->error
            ? globus_error_put(bounce_info->error) : GLOBUS_SUCCESS,
        bounce_info->iov,
        bounce_info->iovc,
        bounce_info->nbytes,
        GLOBUS_NULL,
        bounce_info);
}

static
void
globus_l_io_bounce_iovec_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    globus_l_io_handle_t *              ihandle;
    GlobusIOName(globus_l_io_bounce_iovec_cb);

    bounce_info = (globus_l_io_bounce_t *) user_arg;
    ihandle = bounce_info->handle;

    if(globus_l_io_should_bounce(bounce_info))
    {
        bounce_info->error = result == GLOBUS_SUCCESS
            ? GLOBUS_NULL : globus_error_get(result);
        bounce_info->iov = iovec;
        bounce_info->iovc = count;
        bounce_info->nbytes = nbytes;
        result = globus_callback_space_register_oneshot(
            GLOBUS_NULL,
            GLOBUS_NULL,
            globus_l_io_bounce_iovec_kickout,
            bounce_info,
            bounce_info->handle->space);
        if(result != GLOBUS_SUCCESS)
        {
            globus_panic(
                GLOBUS_IO_MODULE,
                result,
                _IOSL("[%s:%d] Couldn't register callback"),
                _io_name,
                __LINE__);
        }
        return;
    }

    if(result != GLOBUS_SUCCESS)
    {
        if(globus_xio_error_is_eof(result))
        {
            result = globus_error_put(
                globus_io_error_construct_eof(
                    GLOBUS_IO_MODULE,
                    globus_error_get(result),
                    ihandle->io_handle));
        }
        else if(globus_xio_error_is_canceled(result))
        {
            result = globus_error_put(
                globus_io_error_construct_io_cancelled(
                    GLOBUS_IO_MODULE,
                    globus_error_get(result),
                    ihandle->io_handle));
        }
    }

    if(globus_l_io_cancel_precallback(bounce_info))
    {
        bounce_info->cb.writev(
            bounce_info->user_arg,
            ihandle->io_handle,
            result,
            iovec,
            count,
            nbytes);
    }

    globus_l_io_cancel_complete(bounce_info);
    globus_free(bounce_info);
}

static
void
globus_l_io_bounce_authz_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg);

static
void
globus_l_io_bounce_authz_kickout(
    void *                              user_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    GlobusIOName(globus_l_io_bounce_authz_kickout);

    bounce_info = (globus_l_io_bounce_t *) user_arg;

    globus_l_io_bounce_authz_cb(
        GLOBUS_NULL,
        bounce_info->error
            ? globus_error_put(bounce_info->error) : GLOBUS_SUCCESS,
        user_arg);
}

static
void
globus_l_io_bounce_authz_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    globus_l_io_handle_t *              ihandle;
    gss_name_t                          peer_identity;
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    gss_buffer_desc                     peer_name_buffer;
    gss_ctx_id_t                        context;
    globus_bool_t                       perform_callback;
    GlobusIOName(globus_l_io_bounce_authz_cb);

    bounce_info = (globus_l_io_bounce_t *) user_arg;

    if(globus_l_io_should_bounce(bounce_info) ||
       (handle != NULL && bounce_info->handle->attr->authz_data.callback))
    {
        bounce_info->error = result == GLOBUS_SUCCESS
            ? GLOBUS_NULL : globus_error_get(result);
        result = globus_callback_space_register_oneshot(
            GLOBUS_NULL,
            GLOBUS_NULL,
            globus_l_io_bounce_authz_kickout,
            bounce_info,
            bounce_info->blocking
                ? GLOBUS_CALLBACK_GLOBAL_SPACE : bounce_info->handle->space);
        if(result != GLOBUS_SUCCESS)
        {
            globus_panic(
                GLOBUS_IO_MODULE,
                result,
                _IOSL("[%s:%d] Couldn't register callback"),
                _io_name,
                __LINE__);
        }
        return;
    }

    ihandle = bounce_info->handle;
    perform_callback = globus_l_io_cancel_precallback(bounce_info);

    if(result != GLOBUS_SUCCESS)
    {
        if(globus_xio_error_is_eof(result))
        {
            result = globus_error_put(
                globus_io_error_construct_eof(
                    GLOBUS_IO_MODULE,
                    globus_error_get(result),
                    ihandle->io_handle));
        }
        else if(globus_xio_error_is_canceled(result) && !globus_xio_error_match(result, GLOBUS_XIO_ERROR_TIMEOUT))
        {
            result = globus_error_put(
                globus_io_error_construct_io_cancelled(
                    GLOBUS_IO_MODULE,
                    globus_error_get(result),
                    ihandle->io_handle));
        }
        else
        {
            globus_object_t * error = globus_error_get(result);
            if(globus_error_gssapi_match(error,
                                         GLOBUS_GSI_GSSAPI_MODULE,
                                         GSS_S_UNAUTHORIZED) ||
               globus_xio_driver_error_match(
                   globus_l_io_gsi_driver,
                   error,
                   GLOBUS_XIO_GSI_AUTHORIZATION_FAILED))
            {
                result = globus_error_put(
                    globus_io_error_construct_authorization_failed(
                        GLOBUS_IO_MODULE,
                        error,
                        ihandle->io_handle,
                        0,
                        0,
                        0));
            }
            else if (globus_xio_driver_error_match(
                        globus_l_io_gsi_driver,
                        error,
                        GLOBUS_XIO_GSI_ERROR_WRAP_GSSAPI))
            {
                /* Some other GSSAPI error, assume authentication failure here
                 */
                result = globus_error_put(
                    globus_io_error_construct_authentication_failed(
                        GLOBUS_IO_MODULE,
                        error,
                        ihandle->io_handle,
                        0,
                        0,
                        0));
            }
            else
            {
                result = globus_error_put(error);
            }
        }
        goto done;
    }

    if(ihandle->attr->authorization_mode ==
       GLOBUS_IO_SECURE_AUTHORIZATION_MODE_CALLBACK)
    {
        result = globus_xio_handle_cntl(
            ihandle->xio_handle,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_GET_PEER_NAME,
            &peer_identity);

        if(result != GLOBUS_SUCCESS)
        {
            goto done;
        }

        result = globus_xio_handle_cntl(
            ihandle->xio_handle,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_GET_CONTEXT,
            &context);

        if(result != GLOBUS_SUCCESS)
        {
            goto done;
        }

        major_status = gss_display_name(&minor_status,
                                        peer_identity,
                                        &peer_name_buffer,
                                        GLOBUS_NULL);

        if(GSS_ERROR(major_status))
        {
            result = GlobusLIOErrorWrapGSSFailed("gss_display_name",
                                                 major_status,
                                                 minor_status);
            goto done;
        }

        if(!perform_callback || !ihandle->attr->authz_data.callback(
           ihandle->attr->authz_data.callback_arg,
           ihandle->io_handle,
           GLOBUS_SUCCESS,
           peer_name_buffer.value,
           context))
        {
            result = globus_error_put(
                globus_io_error_construct_authorization_failed(
                    GLOBUS_IO_MODULE,
                    GLOBUS_NULL,
                    ihandle->io_handle,
                    0,
                    0,
                    0));
        }

        free(peer_name_buffer.value);
    }

done:

    if(result != GLOBUS_SUCCESS)
    {
        globus_xio_close(ihandle->xio_handle, GLOBUS_NULL);
        ihandle->xio_handle = GLOBUS_NULL;
        *ihandle->io_handle = GLOBUS_NULL;
    }

    if(perform_callback)
    {
        bounce_info->cb.non_io(
            bounce_info->user_arg,
            ihandle->io_handle,
            result);
    }

    globus_l_io_cancel_complete(bounce_info);
    globus_free(bounce_info);

    if(result != GLOBUS_SUCCESS)
    {
        globus_l_io_handle_destroy(ihandle);
    }
}

static
void
globus_l_io_bounce_listen_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg);

static
void
globus_l_io_bounce_listen_kickout(
    void *                              user_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    GlobusIOName(globus_l_io_bounce_authz_kickout);

    bounce_info = (globus_l_io_bounce_t *) user_arg;

    globus_l_io_bounce_listen_cb(
        GLOBUS_NULL,
        bounce_info->handle->accepted_handle,
        bounce_info->error
            ? globus_error_put(bounce_info->error) : GLOBUS_SUCCESS,
        user_arg);
}

static
void
globus_l_io_bounce_listen_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    globus_l_io_handle_t *              ihandle;
    GlobusIOName(globus_l_io_bounce_listen_cb);

    bounce_info = (globus_l_io_bounce_t *) user_arg;
    ihandle = bounce_info->handle;
    ihandle->accepted_handle = handle;

    if(globus_l_io_should_bounce(bounce_info))
    {
        bounce_info->error = result == GLOBUS_SUCCESS
            ? GLOBUS_NULL : globus_error_get(result);
        result = globus_callback_space_register_oneshot(
            GLOBUS_NULL,
            GLOBUS_NULL,
            globus_l_io_bounce_listen_kickout,
            bounce_info,
            bounce_info->blocking
                ? GLOBUS_CALLBACK_GLOBAL_SPACE : bounce_info->handle->space);
        if(result != GLOBUS_SUCCESS)
        {
            globus_panic(
                GLOBUS_IO_MODULE,
                result,
                _IOSL("[%s:%d] Couldn't register callback"),
                _io_name,
                __LINE__);
        }
        return;
    }

    if(globus_l_io_cancel_precallback(bounce_info))
    {
        if(result != GLOBUS_SUCCESS && globus_xio_error_is_canceled(result))
        {
            result = globus_error_put(
                globus_io_error_construct_io_cancelled(
                    GLOBUS_IO_MODULE,
                    globus_error_get(result),
                    ihandle->io_handle));
        }

        bounce_info->cb.non_io(
            bounce_info->user_arg,
            ihandle->io_handle,
            result);
    }

    globus_l_io_cancel_complete(bounce_info);
    globus_free(bounce_info);
}

static
void
globus_l_io_blocking_cb(
    void *                              user_arg,
    globus_io_handle_t *                handle,
    globus_result_t                     result)
{
    globus_l_io_monitor_t *             monitor;
    GlobusIOName(globus_l_io_blocking_cb);

    monitor = (globus_l_io_monitor_t *) user_arg;

    globus_mutex_lock(&monitor->lock);
    {
        monitor->done = GLOBUS_TRUE;
        monitor->error = globus_error_get(result);
        globus_cond_signal(&monitor->cond);
    }
    globus_mutex_unlock(&monitor->lock);
}

globus_result_t
globus_io_attr_set_callback_space(
    globus_io_attr_t *                  attr,
    globus_callback_space_t             space)
{
    GlobusIOName(globus_io_attr_set_callback_space);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_FILE_ATTR | GLOBUS_I_IO_TCP_ATTR);

    globus_callback_space_destroy((*attr)->space);
    globus_callback_space_reference(space);
    (*attr)->space = space;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_attr_get_callback_space(
    globus_io_attr_t *                  attr,
    globus_callback_space_t *           space)
{
    GlobusIOName(globus_io_attr_get_callback_space);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_FILE_ATTR | GLOBUS_I_IO_TCP_ATTR);
    GlobusLIOCheckNullParam(space);

    *space = (*attr)->space;
    return GLOBUS_SUCCESS;
}

/* file attrs */

globus_result_t
globus_io_fileattr_init(
    globus_io_attr_t *                  attr)
{
    globus_l_io_attr_t *                iattr;
    globus_result_t                     result;
    GlobusIOName(globus_io_fileattr_init);

    GlobusLIOCheckNullParam(attr);

    result = GlobusLIOMalloc(iattr, globus_l_io_attr_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_alloc;
    }

    iattr->type = GLOBUS_I_IO_FILE_ATTR;
    iattr->space = GLOBUS_CALLBACK_GLOBAL_SPACE;

    result = globus_xio_attr_init(&iattr->attr);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr;
    }

    iattr->file_flags = GLOBUS_IO_FILE_TYPE_BINARY;
    *attr = iattr;

    return GLOBUS_SUCCESS;

error_attr:
    globus_free(iattr);

error_alloc:
    *attr = GLOBUS_NULL;
    return result;
}

globus_result_t
globus_io_fileattr_destroy(
    globus_io_attr_t *                  attr)
{
    globus_l_io_attr_t *                iattr;
    GlobusIOName(globus_io_fileattr_destroy);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_FILE_ATTR);

    iattr = (globus_l_io_attr_t *) *attr;

    globus_callback_space_destroy(iattr->space);
    globus_xio_attr_destroy(iattr->attr);
    if(iattr->stack)
    {
        globus_xio_stack_destroy(iattr->stack);
    }
    globus_free(iattr);
    *attr = GLOBUS_NULL;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_attr_set_file_type(
    globus_io_attr_t *                  attr,
    globus_io_file_type_t               file_type)
{
    GlobusIOName(globus_io_attr_set_file_type);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_FILE_ATTR);

    (*attr)->file_flags = file_type;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_attr_get_file_type(
    globus_io_attr_t *                  attr,
    globus_io_file_type_t *             file_type)
{
    GlobusIOName(globus_io_attr_get_file_type);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_FILE_ATTR);

    *file_type = (*attr)->file_flags;

    return GLOBUS_SUCCESS;
}

/* tcp attrs */

globus_result_t
globus_io_tcpattr_init(
    globus_io_attr_t *                  attr)
{
    globus_l_io_attr_t *                iattr;
    globus_result_t                     result;
    GlobusIOName(globus_io_tcpattr_init);

    GlobusLIOCheckNullParam(attr);

    result = GlobusLIOMalloc(iattr, globus_l_io_attr_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_alloc;
    }

    iattr->type = GLOBUS_I_IO_TCP_ATTR;
    iattr->space = GLOBUS_CALLBACK_GLOBAL_SPACE;
    iattr->file_flags = 0;
    iattr->allow_ipv6 = getenv("GLOBUS_IO_IPV6") ? GLOBUS_TRUE : GLOBUS_FALSE;
    iattr->authentication_mode = GLOBUS_IO_SECURE_AUTHENTICATION_MODE_NONE;
    iattr->authorization_mode = GLOBUS_IO_SECURE_AUTHORIZATION_MODE_NONE;
    iattr->channel_mode = GLOBUS_IO_SECURE_CHANNEL_MODE_CLEAR;
    memset(&iattr->authz_data, 0, sizeof(globus_l_io_secure_authorization_data_t));
    result = globus_xio_attr_init(&iattr->attr);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr;
    }

    result = globus_xio_attr_cntl(
            iattr->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_PROTECTION_LEVEL,
            GLOBUS_XIO_GSI_PROTECTION_LEVEL_NONE);

    if(result != GLOBUS_SUCCESS)
    {
        goto error_xio_attr;
    }

    *attr = iattr;

    return GLOBUS_SUCCESS;

 error_xio_attr:
    globus_xio_attr_destroy(iattr->attr);

 error_attr:
    globus_free(iattr);

 error_alloc:
    *attr = GLOBUS_NULL;
    return result;
}

globus_result_t
globus_io_tcpattr_destroy(
    globus_io_attr_t *                  attr)
{
    globus_l_io_attr_t *                iattr;
    GlobusIOName(globus_io_tcpattr_destroy);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    iattr = (globus_l_io_attr_t *) *attr;

    if(iattr->authz_data.identity != GSS_C_NO_NAME)
    {
        OM_uint32                       minor_status;

        gss_release_name(&minor_status, &iattr->authz_data.identity);
    }

    globus_callback_space_destroy(iattr->space);
    globus_xio_attr_destroy(iattr->attr);
    if(iattr->stack)
    {
        globus_xio_stack_destroy(iattr->stack);
    }
    globus_free(iattr);
    *attr = GLOBUS_NULL;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_attr_set_tcp_restrict_port(
    globus_io_attr_t *                  attr,
    globus_bool_t                       restrict_port)
{
    GlobusIOName(globus_io_attr_set_tcp_restrict_port);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_SET_RESTRICT_PORT,
        restrict_port);
}

globus_result_t
globus_io_attr_get_tcp_restrict_port(
    globus_io_attr_t *                  attr,
    globus_bool_t *                     restrict_port)
{
    GlobusIOName(globus_io_attr_get_tcp_restrict_port);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_GET_RESTRICT_PORT,
        restrict_port);
}

globus_result_t
globus_io_attr_set_tcp_nodelay(
    globus_io_attr_t *                  attr,
    globus_bool_t                       nodelay)
{
    GlobusIOName(globus_io_attr_set_tcp_nodelay);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_SET_NODELAY,
        nodelay);
}

globus_result_t
globus_io_attr_get_tcp_nodelay(
    globus_io_attr_t *                  attr,
    globus_bool_t *                     nodelay)
{
    GlobusIOName(globus_io_attr_get_tcp_nodelay);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_GET_NODELAY,
        nodelay);
}

globus_result_t
globus_io_attr_set_tcp_interface(
    globus_io_attr_t *                  attr,
    const char *                        interface_addr)
{
    GlobusIOName(globus_io_attr_set_tcp_interface);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_SET_INTERFACE,
        interface_addr);
}

globus_result_t
globus_io_attr_get_tcp_interface(
    globus_io_attr_t *                  attr,
    char **                             interface_addr)
{
    GlobusIOName(globus_io_attr_get_tcp_interface);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_GET_INTERFACE,
        interface_addr);
}

globus_result_t
globus_io_attr_set_tcp_allow_ipv6(
    globus_io_attr_t *                  attr,
    globus_bool_t                       allow)
{
    globus_l_io_attr_t *                iattr;
    GlobusIOName(globus_io_attr_set_tcp_allow_ipv6);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    iattr = (globus_l_io_attr_t *) *attr;
    iattr->allow_ipv6 = allow;

    return GLOBUS_SUCCESS;
}

/* socket attrs */
globus_result_t
globus_io_attr_set_socket_reuseaddr(
    globus_io_attr_t *                  attr,
    globus_bool_t                       reuseaddr)
{
    GlobusIOName(globus_io_attr_set_socket_reuseaddr);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_SET_REUSEADDR,
        reuseaddr);
}

globus_result_t
globus_io_attr_get_socket_reuseaddr(
    globus_io_attr_t *                  attr,
    globus_bool_t *                     reuseaddr)
{
    GlobusIOName(globus_io_attr_get_socket_reuseaddr);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_GET_REUSEADDR,
        reuseaddr);
}

globus_result_t
globus_io_attr_set_socket_keepalive(
    globus_io_attr_t *                  attr,
    globus_bool_t                       keepalive)
{
    GlobusIOName(globus_io_attr_set_socket_keepalive);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_SET_KEEPALIVE,
        keepalive);
}

globus_result_t
globus_io_attr_get_socket_keepalive(
    globus_io_attr_t *                  attr,
    globus_bool_t *                     keepalive)
{
    GlobusIOName(globus_io_attr_get_socket_keepalive);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_GET_KEEPALIVE,
        keepalive);
}

globus_result_t
globus_io_attr_set_socket_linger(
    globus_io_attr_t *                  attr,
    globus_bool_t                       linger,
    int                                 linger_time)
{
    GlobusIOName(globus_io_attr_set_socket_linger);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_SET_LINGER,
        linger,
        linger_time);
}

globus_result_t
globus_io_attr_get_socket_linger(
    globus_io_attr_t *                  attr,
    globus_bool_t *                     linger,
    int *                               linger_time)
{
    GlobusIOName(globus_io_attr_get_socket_linger);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_GET_LINGER,
        linger,
        linger_time);
}

globus_result_t
globus_io_attr_set_socket_oobinline(
    globus_io_attr_t *                  attr,
    globus_bool_t                       oobinline)
{
    GlobusIOName(globus_io_attr_set_socket_oobinline);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_SET_OOBINLINE,
        oobinline);
}

globus_result_t
globus_io_attr_get_socket_oobinline(
    globus_io_attr_t *                  attr,
    globus_bool_t *                     oobinline)
{
    GlobusIOName(globus_io_attr_get_socket_oobinline);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_GET_OOBINLINE,
        oobinline);
}

globus_result_t
globus_io_attr_set_socket_sndbuf(
    globus_io_attr_t *                  attr,
    int                                 sndbuf)
{
    GlobusIOName(globus_io_attr_set_socket_sndbuf);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_SET_SNDBUF,
        sndbuf);
}

globus_result_t
globus_io_attr_get_socket_sndbuf(
    globus_io_attr_t *                  attr,
    int *                               sndbuf)
{
    GlobusIOName(globus_io_attr_get_socket_sndbuf);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_GET_SNDBUF,
        sndbuf);
}

globus_result_t
globus_io_attr_set_socket_rcvbuf(
    globus_io_attr_t *                  attr,
    int                                 rcvbuf)
{
    GlobusIOName(globus_io_attr_set_socket_rcvbuf);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_SET_RCVBUF,
        rcvbuf);
}

globus_result_t
globus_io_attr_get_socket_rcvbuf(
    globus_io_attr_t *                  attr,
    int *                               rcvbuf)
{
    GlobusIOName(globus_io_attr_get_socket_rcvbuf);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
        (*attr)->attr,
        globus_l_io_tcp_driver,
        GLOBUS_XIO_TCP_GET_RCVBUF,
        rcvbuf);
}

globus_result_t
globus_io_handle_get_socket_buf(
    globus_io_handle_t *                handle,
    int *                               rcvbuf,
    int *                               sndbuf)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusIOName(globus_io_handle_get_socket_buf);

    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);

    if(rcvbuf != NULL)
    {
        res = globus_xio_handle_cntl(
            (*handle)->xio_handle,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_GET_RCVBUF,
            rcvbuf);
        if(res != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }
    if(sndbuf != NULL)
    {
        res = globus_xio_handle_cntl(
            (*handle)->xio_handle,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_GET_SNDBUF,
            sndbuf);
        if(res != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }
    return GLOBUS_SUCCESS;

error:
    return res;
}


/* file operations */
static
globus_result_t
globus_l_io_file_open(
    globus_io_handle_t *                handle,
    globus_io_attr_t *                  attr,
    const char *                        path,
    int                                 flags,
    int                                 mode,
    int                                 fd,
    globus_xio_system_file_t            winhandle)
{
    globus_result_t                     result;
    globus_io_attr_t                    myattr;
    globus_l_io_attr_t *                iattr;
    globus_l_io_handle_t *              ihandle;
    GlobusIOName(globus_l_io_file_open);

    GlobusLIOCheckNullParam(handle);
    *handle = GLOBUS_NULL;

    if(attr)
    {
        GlobusLIOCheckAttr(attr, GLOBUS_I_IO_FILE_ATTR);
        result = globus_l_io_iattr_copy(&myattr, attr);
    }
    else
    {
        result = globus_io_fileattr_init(&myattr);
    }
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr;
    }
    iattr = (globus_l_io_attr_t *) myattr;

    if(path)
    {
        result = globus_xio_attr_cntl(
            iattr->attr,
            globus_l_io_file_driver,
            GLOBUS_XIO_FILE_SET_MODE,
            mode);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_cntl;
        }

        result = globus_xio_attr_cntl(
            iattr->attr,
            globus_l_io_file_driver,
            GLOBUS_XIO_FILE_SET_FLAGS,
            flags | iattr->file_flags);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_cntl;
        }
    }
    else
    {
        globus_xio_system_file_t        file;

#ifndef WIN32
        file = fd;
#else
        if (fd)
        {
            file = (globus_xio_system_file_t) _get_osfhandle(fd);
        }
        else
        {
            file = (globus_xio_system_file_t) winhandle;
        }
#endif
         result = globus_xio_attr_cntl(
            iattr->attr,
            globus_l_io_file_driver,
            GLOBUS_XIO_FILE_SET_HANDLE,
            file);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_cntl;
        }
    }

    result = globus_l_io_handle_init(
        &ihandle, handle, GLOBUS_I_IO_FILE_HANDLE, iattr->space);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_alloc;
    }

    result = globus_xio_handle_create(
        &ihandle->xio_handle, globus_l_io_file_stack);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_handle;
    }

    result = globus_xio_open(
        ihandle->xio_handle,
        path,
        iattr->attr);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_open;
    }

    *handle = ihandle;
    globus_io_fileattr_destroy(&myattr);

    return GLOBUS_SUCCESS;

error_open:
    globus_xio_close(ihandle->xio_handle, GLOBUS_NULL);
error_handle:
    globus_l_io_handle_destroy(ihandle);
error_alloc:
error_cntl:
    globus_io_fileattr_destroy(&myattr);
error_attr:
    return result;
}

globus_result_t
globus_io_file_open(
    const char *                        path,
    int                                 flags,
    int                                 mode,
    globus_io_attr_t *                  attr,
    globus_io_handle_t *                handle)
{
    GlobusIOName(globus_io_file_open);

    GlobusLIOCheckNullParam(path);

    return globus_l_io_file_open(handle, attr, path, flags, mode, -1, 0);
}

globus_result_t
globus_io_file_seek(
    globus_io_handle_t *                handle,
    globus_off_t                        offset,
    globus_io_whence_t                  whence)
{
    GlobusIOName(globus_io_file_seek);

    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_FILE_HANDLE);

    return globus_xio_handle_cntl(
        (*handle)->xio_handle,
        globus_l_io_file_driver,
        GLOBUS_XIO_FILE_SEEK,
        &offset,
        whence);
}

globus_result_t
globus_io_file_posix_convert(
    int                                 fd,
    globus_io_attr_t *                  attr,
    globus_io_handle_t *                handle)
{
    GlobusIOName(globus_io_file_posix_convert);

    return globus_l_io_file_open(
        handle,
        attr,
        GLOBUS_NULL,
        0,
        0,
        fd,
        0);
}

#if _WIN32
globus_result_t
globus_io_file_windows_convert(
    globus_xio_system_file_t            fd,
    globus_io_attr_t *                  attr,
    globus_io_handle_t *                handle)
{
    GlobusIOName(globus_io_file_posix_convert);

    return globus_l_io_file_open(
        handle,
        attr,
        GLOBUS_NULL,
        0,
        0,
        0,
        fd);
}
#endif

/* tcp operations */

globus_result_t
globus_io_tcp_get_attr(
    globus_io_handle_t *                handle,
    globus_io_attr_t *                  attr)
{
    GlobusIOName(globus_io_tcp_get_attr);

    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);
    GlobusLIOCheckNullParam(attr);

    if((*handle)->attr)
    {
        return globus_l_io_iattr_copy(attr, &(*handle)->attr);
    }
    else
    {
        return globus_io_tcpattr_init(attr);
    }
}

static
globus_bool_t
globus_l_io_open_timeout_cb(
    globus_xio_handle_t                 handle,
    globus_xio_operation_type_t         type,
    void *                              user_arg)
{
    return GLOBUS_TRUE;
}

static
globus_result_t
globus_l_io_tcp_register_connect(
    const char *                        host,
    unsigned short                      port,
    int                                 socket,
    globus_io_attr_t *                  attr,
    globus_io_callback_t                callback,
    void *                              callback_arg,
    globus_io_handle_t *                handle,
    globus_bool_t                       blocking)
{
    globus_result_t                     result;
    globus_l_io_handle_t *              ihandle;
    char                                buf[256];
    globus_l_io_bounce_t *              bounce_info;
    globus_xio_stack_t                  stack;
    char *                              cs = GLOBUS_NULL;
    GlobusIOName(globus_l_io_tcp_register_connect);

    GlobusLIOCheckNullParam(handle);
    GlobusLIOCheckNullParam(callback);

    if(attr)
    {
        GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);
    }

    result = GlobusLIOMalloc(bounce_info, globus_l_io_bounce_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_bounce;
    }

    result = globus_l_io_handle_init(
        &ihandle, handle, GLOBUS_I_IO_TCP_HANDLE,
        attr ? (*attr)->space : GLOBUS_CALLBACK_GLOBAL_SPACE);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_handle;
    }

    stack = globus_l_io_tcp_stack;
    if(attr)
    {
        result = globus_l_io_iattr_copy(&ihandle->attr, attr);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_attr;
        }
        if((*attr)->stack != NULL)
        {
            stack = (*attr)->stack;
        }
        else
        {
            if(ihandle->attr->authentication_mode !=
               GLOBUS_IO_SECURE_AUTHENTICATION_MODE_NONE)
            {
                stack = globus_l_io_gsi_stack;
            }
        }
    }
    else
    {
        result = globus_io_tcpattr_init(&ihandle->attr);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_attr;
        }
    }

    if(!ihandle->attr->allow_ipv6)
    {
        result = globus_xio_attr_cntl(
            ihandle->attr->attr,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_SET_NO_IPV6,
            GLOBUS_TRUE);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_attr;
        }
    }

    result = globus_xio_attr_cntl(
        ihandle->attr->attr,
        GLOBUS_NULL,
        GLOBUS_XIO_ATTR_SET_TIMEOUT_OPEN,
        globus_l_io_open_timeout_cb,
        &globus_l_io_open_timeout,
        GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr;
    }

    if(host)
    {
        if(strchr(host, ':'))
        {
            snprintf(buf, sizeof(buf), "[%s]:%hu", host, port);
        }
        else
        {
            snprintf(buf, sizeof(buf), "%s:%hu", host, port);
        }
        cs = buf;
    }
    else
    {
        result = globus_xio_attr_cntl(
            ihandle->attr->attr,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_SET_HANDLE,
            socket);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_attr;
        }
    }

    result = globus_xio_handle_create(&ihandle->xio_handle, stack);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_create;
    }

    bounce_info->handle = ihandle;
    bounce_info->cb.non_io = callback;
    bounce_info->user_arg = callback_arg;
    bounce_info->blocking = blocking;
    bounce_info->cancel_info = GLOBUS_NULL;
    *handle = ihandle;

    globus_mutex_lock(&ihandle->pending_lock);
    {
        result = globus_xio_register_open(
            ihandle->xio_handle,
            cs,
            ihandle->attr->attr,
            globus_l_io_bounce_authz_cb,
            bounce_info);
        if(result != GLOBUS_SUCCESS)
        {
            globus_mutex_unlock(&ihandle->pending_lock);
            goto error_open;
        }

        globus_l_io_cancel_insert(bounce_info);
    }
    globus_mutex_unlock(&ihandle->pending_lock);

    return GLOBUS_SUCCESS;

error_open:
    globus_xio_close(ihandle->xio_handle, GLOBUS_NULL);
error_create:
error_attr:
    globus_l_io_handle_destroy(ihandle);
error_handle:
    globus_free(bounce_info);
error_bounce:
    *handle = GLOBUS_NULL;
    return result;
}

globus_result_t
globus_io_tcp_register_connect(
    const char *                        host,
    unsigned short                      port,
    globus_io_attr_t *                  attr,
    globus_io_callback_t                callback,
    void *                              callback_arg,
    globus_io_handle_t *                handle)
{
    GlobusIOName(globus_io_tcp_register_connect);

    GlobusLIOCheckNullParam(host);

    return globus_l_io_tcp_register_connect(
        host,
        port,
        -1,
        attr,
        callback,
        callback_arg,
        handle,
        GLOBUS_FALSE);
}

static
globus_result_t
globus_l_io_tcp_connect(
    const char *                        host,
    unsigned short                      port,
    int                                 socket,
    globus_io_attr_t *                  attr,
    globus_io_handle_t *                handle)
{
    globus_result_t                     result;
    globus_l_io_monitor_t               monitor;
    GlobusIOName(globus_l_io_tcp_connect);

    monitor.done = GLOBUS_FALSE;
    globus_mutex_init(&monitor.lock, GLOBUS_NULL);
    globus_cond_init(&monitor.cond, GLOBUS_NULL);

    result = globus_l_io_tcp_register_connect(
        host,
        port,
        socket,
        attr,
        globus_l_io_blocking_cb,
        &monitor,
        handle,
        GLOBUS_TRUE);
    if(result != GLOBUS_SUCCESS)
    {
        monitor.done = GLOBUS_TRUE;
        monitor.error = globus_error_get(result);
    }

    globus_mutex_lock(&monitor.lock);
    {
        while(!monitor.done)
        {
            globus_cond_wait(&monitor.cond, &monitor.lock);
        }
    }
    globus_mutex_unlock(&monitor.lock);

    globus_mutex_destroy(&monitor.lock);
    globus_cond_destroy(&monitor.cond);

    if(monitor.error != GLOBUS_NULL)
    {
        result = globus_error_put(monitor.error);
        goto error_register;
    }

    return GLOBUS_SUCCESS;

error_register:
    *handle = GLOBUS_NULL;
    return result;
}

globus_result_t
globus_io_tcp_connect(
    const char *                        host,
    unsigned short                      port,
    globus_io_attr_t *                  attr,
    globus_io_handle_t *                handle)
{
    GlobusIOName(globus_io_tcp_connect);

    GlobusLIOCheckNullParam(host);

    return globus_l_io_tcp_connect(host, port, -1, attr, handle);
}

static
globus_result_t
globus_l_io_tcp_create_listener(
    unsigned short *                    port,
    int                                 backlog,
    int                                 socket,
    globus_io_attr_t *                  attr,
    globus_io_handle_t *                handle)
{
    globus_result_t                     result;
    globus_io_attr_t                    myattr;
    globus_l_io_attr_t *                iattr;
    globus_l_io_handle_t *              ihandle;
    globus_xio_stack_t                  stack;
    GlobusIOName(globus_l_io_tcp_create_listener);

    GlobusLIOCheckNullParam(handle);

    if(attr)
    {
        GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);
        result = globus_l_io_iattr_copy(&myattr, attr);
    }
    else
    {
        result = globus_io_tcpattr_init(&myattr);
    }

    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr;
    }

    iattr = (globus_l_io_attr_t *) myattr;
    if(port)
    {
        result = globus_xio_attr_cntl(
            iattr->attr,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_SET_PORT,
            *port);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_cntl;
        }

        result = globus_xio_attr_cntl(
            iattr->attr,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_SET_BACKLOG,
            backlog);
    }
    else
    {
        result = globus_xio_attr_cntl(
            iattr->attr,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_SET_HANDLE,
            socket);
    }
    if(result != GLOBUS_SUCCESS)
    {
        goto error_cntl;
    }

    if(!iattr->allow_ipv6)
    {
        result = globus_xio_attr_cntl(
            iattr->attr,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_SET_NO_IPV6,
            GLOBUS_TRUE);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_cntl;
        }
    }

    result = globus_l_io_handle_init(
        &ihandle, handle, GLOBUS_I_IO_TCP_HANDLE, iattr->space);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_alloc;
    }

    if(iattr->stack != NULL)
    {
        stack = iattr->stack;
    }
    else if(iattr->authentication_mode ==
        GLOBUS_IO_SECURE_AUTHENTICATION_MODE_NONE)
    {
        stack = globus_l_io_tcp_stack;
    }
    else
    {
        stack = globus_l_io_gsi_stack;
    }

    result = globus_xio_server_create(
        &ihandle->xio_server, iattr->attr, stack);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_server;
    }

    if(port && !*port)
    {
        char *                          contact_string;
        char *                          s;

        result = globus_xio_server_get_contact_string(
            ihandle->xio_server, &contact_string);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_server_cntl;
        }

        s = strrchr(contact_string, ':');
        globus_assert(s);
        *port = atoi(s + 1);

        globus_free(contact_string);
    }

    ihandle->attr = iattr;
    *handle = ihandle;

    return GLOBUS_SUCCESS;

error_server_cntl:
    globus_xio_server_close(ihandle->xio_server);

error_server:
    globus_l_io_handle_destroy(ihandle);

error_alloc:
error_cntl:
    globus_io_tcpattr_destroy(&myattr);

error_attr:
    *handle = GLOBUS_NULL;
    return result;
}

globus_result_t
globus_io_tcp_create_listener(
    unsigned short *                    port,
    int                                 backlog,
    globus_io_attr_t *                  attr,
    globus_io_handle_t *                handle)
{
    GlobusIOName(globus_io_tcp_create_listener);

    GlobusLIOCheckNullParam(port);

    return globus_l_io_tcp_create_listener(
        port, backlog, -1, attr, handle);
}

static
globus_result_t
globus_l_io_tcp_register_listen(
    globus_io_handle_t *                handle,
    globus_io_callback_t                callback,
    void *                              callback_arg,
    globus_bool_t                       blocking)
{
    globus_result_t                     result;
    globus_l_io_handle_t *              ihandle;
    globus_l_io_bounce_t *              bounce_info;
    GlobusIOName(globus_l_io_tcp_register_listen);

    GlobusLIOCheckNullParam(callback);
    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);

    ihandle = *handle;
    if(ihandle->accepted_handle)
    {
        /* user called listen before accepting previous connection */
        result = globus_error_put(
            globus_io_error_construct_registration_error(
                GLOBUS_IO_MODULE,
                GLOBUS_NULL,
                ihandle->io_handle));

        goto error_registered;
    }

    result = GlobusLIOMalloc(bounce_info, globus_l_io_bounce_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_bounce;
    }

    bounce_info->handle = ihandle;
    bounce_info->cb.non_io = callback;
    bounce_info->user_arg = callback_arg;
    bounce_info->blocking = blocking;
    bounce_info->cancel_info = GLOBUS_NULL;

    globus_mutex_lock(&ihandle->pending_lock);
    {
        result = globus_xio_server_register_accept(
            ihandle->xio_server,
            globus_l_io_bounce_listen_cb,
            bounce_info);
        if(result != GLOBUS_SUCCESS)
        {
            globus_mutex_unlock(&ihandle->pending_lock);
            goto error_register;
        }

        globus_l_io_cancel_insert(bounce_info);
    }
    globus_mutex_unlock(&ihandle->pending_lock);

    return GLOBUS_SUCCESS;

error_register:
    globus_free(bounce_info);

error_bounce:
error_registered:
    return result;
}

globus_result_t
globus_io_tcp_register_listen(
    globus_io_handle_t *                handle,
    globus_io_callback_t                callback,
    void *                              callback_arg)
{
    return globus_l_io_tcp_register_listen(
        handle, callback, callback_arg, GLOBUS_FALSE);
}

globus_result_t
globus_io_tcp_listen(
    globus_io_handle_t *                handle)
{
    globus_result_t                     result;
    globus_l_io_monitor_t               monitor;
    GlobusIOName(globus_io_tcp_listen);

    monitor.done = GLOBUS_FALSE;
    globus_mutex_init(&monitor.lock, GLOBUS_NULL);
    globus_cond_init(&monitor.cond, GLOBUS_NULL);

    result = globus_l_io_tcp_register_listen(
        handle, globus_l_io_blocking_cb, &monitor, GLOBUS_TRUE);
    if(result != GLOBUS_SUCCESS)
    {
        monitor.done = GLOBUS_TRUE;
        monitor.error = globus_error_get(result);
    }

    globus_mutex_lock(&monitor.lock);
    {
        while(!monitor.done)
        {
            globus_cond_wait(&monitor.cond, &monitor.lock);
        }
    }
    globus_mutex_unlock(&monitor.lock);

    globus_mutex_destroy(&monitor.lock);
    globus_cond_destroy(&monitor.cond);

    if(monitor.error != GLOBUS_NULL)
    {
        result = globus_error_put(monitor.error);
        goto error_register;
    }

    return GLOBUS_SUCCESS;

error_register:
    return result;
}

/*
 * attr passed on create_listener will always be applied.
 * this attr will also be applied, except that it can't change whether or not
 * gsi is used and other things that need to be set before a socket is accepted
 */
static
globus_result_t
globus_l_io_tcp_register_accept(
    globus_io_handle_t *                listener_handle,
    globus_io_attr_t *                  attr,
    globus_io_handle_t *                new_handle,
    globus_io_callback_t                callback,
    void *                              callback_arg,
    globus_bool_t                       blocking)
{
    globus_result_t                     result;
    globus_l_io_handle_t *              ihandle;
    globus_l_io_handle_t *              ilistener_handle;
    globus_l_io_bounce_t *              bounce_info;
    char *                              contact_string = GLOBUS_NULL;
    GlobusIOName(globus_io_tcp_register_accept);

    GlobusLIOCheckNullParam(new_handle);
    GlobusLIOCheckNullParam(callback);
    GlobusLIOCheckHandle(listener_handle, GLOBUS_I_IO_TCP_HANDLE);

    ilistener_handle = *listener_handle;
    if(!ilistener_handle->accepted_handle)
    {
        result = globus_error_put(
            globus_io_error_construct_not_initialized(
                GLOBUS_IO_MODULE,
                GLOBUS_NULL,
                "listener_handle",
                1,
                (char *) _io_name));

        goto error_handle;
    }

    if(attr)
    {
        GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);
    }

    result = globus_l_io_handle_init(
        &ihandle, new_handle, GLOBUS_I_IO_TCP_HANDLE,
        attr ? (*attr)->space : ilistener_handle->space);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_handle;
    }

    result = GlobusLIOMalloc(bounce_info, globus_l_io_bounce_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_bounce;
    }

    bounce_info->handle = ihandle;
    bounce_info->cb.non_io = callback;
    bounce_info->user_arg = callback_arg;
    bounce_info->blocking = blocking;
    bounce_info->cancel_info = GLOBUS_NULL;
    *new_handle = ihandle;

    if(attr)
    {
        result = globus_l_io_iattr_copy(&ihandle->attr, attr);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_gsi;
        }

        if((ilistener_handle->attr->authentication_mode ==
            GLOBUS_IO_SECURE_AUTHENTICATION_MODE_NONE ||
            ihandle->attr->authentication_mode ==
                GLOBUS_IO_SECURE_AUTHENTICATION_MODE_NONE) &&
            ilistener_handle->attr->authentication_mode !=
                ihandle->attr->authentication_mode)
        {
            result = globus_error_put(
                globus_error_construct_error(
                    GLOBUS_XIO_MODULE,
                    GLOBUS_NULL,
                    GLOBUS_XIO_ERROR_PARAMETER,
                    __FILE__,
                    _io_name,
                    __LINE__,
                    _IOSL("Globus IO-XIO requires that the attrs"
                        " passed to globus_io_tcp_create_listener and"
                        " globus_io_tcp_register_accept either both require"
                        " authentication or both not require it")));
            goto error_gsi;
        }
    }
    else
    {
        result = globus_l_io_iattr_copy(
            &ihandle->attr, &ilistener_handle->attr);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_gsi;
        }
    }

    ihandle->xio_handle = ilistener_handle->accepted_handle;
    ilistener_handle->accepted_handle = GLOBUS_NULL;

    result = globus_xio_attr_cntl(
        ihandle->attr->attr,
        GLOBUS_NULL,
        GLOBUS_XIO_ATTR_SET_TIMEOUT_OPEN,
        globus_l_io_open_timeout_cb,
        &globus_l_io_open_timeout,
        GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_gsi;
    }

    globus_mutex_lock(&ihandle->pending_lock);
    {
        result = globus_xio_register_open(
            ihandle->xio_handle,
            contact_string,
            ihandle->attr->attr,
            globus_l_io_bounce_authz_cb,
            bounce_info);

        if(contact_string != GLOBUS_NULL)
        {
            globus_free(contact_string);
        }

        if(result != GLOBUS_SUCCESS)
        {
            globus_mutex_unlock(&ihandle->pending_lock);
            goto error_open;
        }

        globus_l_io_cancel_insert(bounce_info);
    }
    globus_mutex_unlock(&ihandle->pending_lock);

    return GLOBUS_SUCCESS;

error_open:
    globus_xio_close(ihandle->xio_handle, GLOBUS_NULL);
error_gsi:
    globus_free(bounce_info);
error_bounce:
    globus_l_io_handle_destroy(ihandle);
error_handle:
    *new_handle = GLOBUS_NULL;
    return result;
}

globus_result_t
globus_io_tcp_register_accept(
    globus_io_handle_t *                listener_handle,
    globus_io_attr_t *                  attr,
    globus_io_handle_t *                new_handle,
    globus_io_callback_t                callback,
    void *                              callback_arg)
{
    return globus_l_io_tcp_register_accept(
        listener_handle,
        attr,
        new_handle,
        callback,
        callback_arg,
        GLOBUS_FALSE);
}

globus_result_t
globus_io_tcp_accept(
    globus_io_handle_t *                listener_handle,
    globus_io_attr_t *                  attr,
    globus_io_handle_t *                handle)
{
    globus_result_t                     result;
    globus_l_io_monitor_t               monitor;
    GlobusIOName(globus_io_tcp_accept);

    monitor.done = GLOBUS_FALSE;
    globus_mutex_init(&monitor.lock, GLOBUS_NULL);
    globus_cond_init(&monitor.cond, GLOBUS_NULL);

    result = globus_l_io_tcp_register_accept(
        listener_handle,
        attr,
        handle,
        globus_l_io_blocking_cb,
        &monitor,
        GLOBUS_TRUE);
    if(result != GLOBUS_SUCCESS)
    {
        monitor.done = GLOBUS_TRUE;
        monitor.error = globus_error_get(result);
    }

    globus_mutex_lock(&monitor.lock);
    {
        while(!monitor.done)
        {
            globus_cond_wait(&monitor.cond, &monitor.lock);
        }
    }
    globus_mutex_unlock(&monitor.lock);

    globus_mutex_destroy(&monitor.lock);
    globus_cond_destroy(&monitor.cond);

    if(monitor.error != GLOBUS_NULL)
    {
        result = globus_error_put(monitor.error);
        goto error_register;
    }

    return GLOBUS_SUCCESS;

error_register:
    return result;
}

/* host must have room for 16 ints,
 * count will be passed back: 4 for ipv4, 16 for ipv6
 */
globus_result_t
globus_io_tcp_get_local_address_ex(
    globus_io_handle_t *                handle,
    int *                               host,
    int *                               count,
    unsigned short *                    port)
{
    globus_result_t                     result;
    char *                              cs;
    GlobusIOName(globus_io_tcp_get_local_address_ex);

    GlobusLIOCheckNullParam(host);
    GlobusLIOCheckNullParam(count);
    GlobusLIOCheckNullParam(port);
    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);

    if((*handle)->xio_handle)
    {
        result = globus_xio_handle_cntl(
            (*handle)->xio_handle,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_GET_LOCAL_NUMERIC_CONTACT,
            &cs);
    }
    else
    {
        result = globus_xio_server_cntl(
            (*handle)->xio_server,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_GET_LOCAL_NUMERIC_CONTACT,
            &cs);
    }
    if(result != GLOBUS_SUCCESS)
    {
        goto error_cntl;
    }

    result = globus_libc_contact_string_to_ints(cs, host, count, port);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_parse;
    }

    globus_free(cs);

    return GLOBUS_SUCCESS;

error_parse:
    globus_free(cs);

error_cntl:
    return result;
}

globus_result_t
globus_io_tcp_get_remote_address_ex(
    globus_io_handle_t *                handle,
    int *                               host,
    int *                               count,
    unsigned short *                    port)
{
    globus_result_t                     result;
    char *                              cs;
    GlobusIOName(globus_io_tcp_get_remote_address_ex);

    GlobusLIOCheckNullParam(host);
    GlobusLIOCheckNullParam(count);
    GlobusLIOCheckNullParam(port);
    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);

    if((*handle)->xio_handle)
    {
        result = globus_xio_handle_cntl(
            (*handle)->xio_handle,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_GET_REMOTE_NUMERIC_CONTACT,
            &cs);
    }
    else
    {
        result = globus_xio_server_cntl(
            (*handle)->xio_server,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_GET_REMOTE_NUMERIC_CONTACT,
            &cs);
    }
    if(result != GLOBUS_SUCCESS)
    {
        goto error_cntl;
    }

    result = globus_libc_contact_string_to_ints(cs, host, count, port);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_parse;
    }

    globus_free(cs);

    return GLOBUS_SUCCESS;

error_parse:
    globus_free(cs);

error_cntl:
    return result;
}

globus_result_t
globus_io_tcp_get_local_address(
    globus_io_handle_t *                handle,
    int *                               host,
    unsigned short *                    port)
{
    int                                 myhost[16];
    int                                 count;
    globus_result_t                     result;
    GlobusIOName(globus_io_tcp_get_local_address);

    GlobusLIOCheckNullParam(host);
    GlobusLIOCheckNullParam(port);
    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);

    result = globus_io_tcp_get_local_address_ex(handle, myhost, &count, port);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_cntl;
    }

    if(count != 4)
    {
        /* interface doesnt support ipv6 addresses */
        result = globus_error_put(
            globus_io_error_construct_internal_error(
            GLOBUS_IO_MODULE,
            GLOBUS_NULL,
            (char *) _io_name));

        goto error_ipv6;
    }

    while(count--)
    {
        host[count] = myhost[count];
    }

    return GLOBUS_SUCCESS;

error_ipv6:
error_cntl:
    return result;
}

globus_result_t
globus_io_tcp_get_remote_address(
    globus_io_handle_t *                handle,
    int *                               host,
    unsigned short *                    port)
{
    int                                 myhost[16];
    int                                 count;
    globus_result_t                     result;
    GlobusIOName(globus_io_tcp_get_remote_address);

    GlobusLIOCheckNullParam(host);
    GlobusLIOCheckNullParam(port);
    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);

    result = globus_io_tcp_get_remote_address_ex(handle, myhost, &count, port);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_cntl;
    }

    if(count != 4)
    {
        /* interface doesnt support ipv6 addresses */
        result = globus_error_put(
            globus_io_error_construct_internal_error(
            GLOBUS_IO_MODULE,
            GLOBUS_NULL,
            (char *) _io_name));

        goto error_ipv6;
    }

    while(count--)
    {
        host[count] = myhost[count];
    }

    return GLOBUS_SUCCESS;

error_ipv6:
error_cntl:
    return result;
}

/* read operations */

globus_result_t
globus_io_register_read(
    globus_io_handle_t *                handle,
    globus_byte_t *                     buf,
    globus_size_t                       max_nbytes,
    globus_size_t                       wait_for_nbytes,
    globus_io_read_callback_t           callback,
    void *                              callback_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    globus_l_io_handle_t *              ihandle;
    globus_result_t                     result;
    GlobusIOName(globus_io_register_read);

    GlobusLIOCheckNullParam(callback);
    GlobusLIOCheckHandle(handle, 0);

    ihandle = *handle;
    result = GlobusLIOMalloc(bounce_info, globus_l_io_bounce_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_alloc;
    }

    bounce_info->handle = ihandle;
    bounce_info->cb.read_write = callback;
    bounce_info->user_arg = callback_arg;
    bounce_info->blocking = GLOBUS_FALSE;
    bounce_info->cancel_info = GLOBUS_NULL;

    globus_mutex_lock(&ihandle->pending_lock);
    {
        result = globus_xio_register_read(
            ihandle->xio_handle,
            buf,
            max_nbytes,
            wait_for_nbytes,
            GLOBUS_NULL,
            globus_l_io_bounce_io_cb,
            bounce_info);
        if(result != GLOBUS_SUCCESS)
        {
            globus_mutex_unlock(&ihandle->pending_lock);
            goto error_register;
        }

        globus_l_io_cancel_insert(bounce_info);
    }
    globus_mutex_unlock(&ihandle->pending_lock);

    return GLOBUS_SUCCESS;

error_register:
    globus_free(bounce_info);

error_alloc:
    return result;
}

globus_result_t
globus_io_try_read(
    globus_io_handle_t *                handle,
    globus_byte_t *                     buf,
    globus_size_t                       max_nbytes,
    globus_size_t *                     nbytes_read)
{
    globus_result_t                     result;
    GlobusIOName(globus_io_try_read);

    GlobusLIOCheckNullParam(nbytes_read);
    *nbytes_read = 0;
    GlobusLIOCheckHandle(handle, 0);

    result = globus_xio_read(
        (*handle)->xio_handle,
        buf,
        max_nbytes,
        0,
        nbytes_read,
        GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_read;
    }

    return GLOBUS_SUCCESS;

error_read:
    if(globus_xio_error_is_eof(result))
    {
        result = globus_error_put(
            globus_io_error_construct_eof(
                GLOBUS_IO_MODULE,
                globus_error_get(result),
                (*handle)->io_handle));
    }
    else if(globus_xio_error_is_canceled(result))
    {
        result = globus_error_put(
            globus_io_error_construct_io_cancelled(
                GLOBUS_IO_MODULE,
                globus_error_get(result),
                (*handle)->io_handle));
    }

    return result;
}

globus_result_t
globus_io_read(
    globus_io_handle_t *                handle,
    globus_byte_t *                     buf,
    globus_size_t                       max_nbytes,
    globus_size_t                       wait_for_nbytes,
    globus_size_t *                     nbytes_read)
{
    globus_result_t                     result;
    GlobusIOName(globus_io_read);

    GlobusLIOCheckNullParam(nbytes_read);
    *nbytes_read = 0;
    GlobusLIOCheckHandle(handle, 0);

    result = globus_xio_read(
        (*handle)->xio_handle,
        buf,
        max_nbytes,
        wait_for_nbytes,
        nbytes_read,
        GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_read;
    }

    return GLOBUS_SUCCESS;

error_read:
    if(globus_xio_error_is_eof(result))
    {
        result = globus_error_put(
            globus_io_error_construct_eof(
                GLOBUS_IO_MODULE,
                globus_error_get(result),
                (*handle)->io_handle));
    }
    else if(globus_xio_error_is_canceled(result))
    {
        result = globus_error_put(
            globus_io_error_construct_io_cancelled(
                GLOBUS_IO_MODULE,
                globus_error_get(result),
                (*handle)->io_handle));
    }

    return result;
}

/* write operations */

globus_result_t
globus_io_register_write(
    globus_io_handle_t *                handle,
    globus_byte_t *                     buf,
    globus_size_t                       nbytes,
    globus_io_write_callback_t          write_callback,
    void *                              callback_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    globus_l_io_handle_t *              ihandle;
    globus_result_t                     result;
    GlobusIOName(globus_io_register_write);

    GlobusLIOCheckNullParam(write_callback);
    GlobusLIOCheckHandle(handle, 0);

    ihandle = *handle;
    result = GlobusLIOMalloc(bounce_info, globus_l_io_bounce_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_alloc;
    }

    bounce_info->handle = ihandle;
    bounce_info->cb.read_write = write_callback;
    bounce_info->user_arg = callback_arg;
    bounce_info->blocking = GLOBUS_FALSE;
    bounce_info->cancel_info = GLOBUS_NULL;

    globus_mutex_lock(&ihandle->pending_lock);
    {
        result = globus_xio_register_write(
            ihandle->xio_handle,
            buf,
            nbytes,
            nbytes,
            GLOBUS_NULL,
            globus_l_io_bounce_io_cb,
            bounce_info);
        if(result != GLOBUS_SUCCESS)
        {
            globus_mutex_unlock(&ihandle->pending_lock);
            goto error_register;
        }

        globus_l_io_cancel_insert(bounce_info);
    }
    globus_mutex_unlock(&ihandle->pending_lock);

    return GLOBUS_SUCCESS;

error_register:
    globus_free(bounce_info);

error_alloc:
    return result;
}

globus_result_t
globus_io_register_send(
    globus_io_handle_t *                handle,
    globus_byte_t *                     buf,
    globus_size_t                       nbytes,
    int                                 flags,
    globus_io_write_callback_t          write_callback,
    void *                              callback_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    globus_l_io_handle_t *              ihandle;
    globus_result_t                     result;
    globus_xio_data_descriptor_t        dd;
    GlobusIOName(globus_io_register_send);

    GlobusLIOCheckNullParam(write_callback);
    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);

    ihandle = *handle;
    result = GlobusLIOMalloc(bounce_info, globus_l_io_bounce_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_alloc;
    }

    if(flags)
    {
        result = globus_xio_data_descriptor_init(&dd, ihandle->xio_handle);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_dd;
        }

        result = globus_xio_data_descriptor_cntl(
            dd,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_SET_SEND_FLAGS,
            flags);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_dd_cntl;
        }
    }
    else
    {
        dd = GLOBUS_NULL;
    }

    bounce_info->handle = ihandle;
    bounce_info->cb.read_write = write_callback;
    bounce_info->user_arg = callback_arg;
    bounce_info->blocking = GLOBUS_FALSE;
    bounce_info->cancel_info = GLOBUS_NULL;

    globus_mutex_lock(&ihandle->pending_lock);
    {
        result = globus_xio_register_write(
            ihandle->xio_handle,
            buf,
            nbytes,
            nbytes,
            dd,
            globus_l_io_bounce_io_cb,
            bounce_info);
        dd = GLOBUS_NULL; /* XXX is xio freeing this for us ?? */
        if(result != GLOBUS_SUCCESS)
        {
            globus_mutex_unlock(&ihandle->pending_lock);
            goto error_register;
        }

        globus_l_io_cancel_insert(bounce_info);
    }
    globus_mutex_unlock(&ihandle->pending_lock);

    return GLOBUS_SUCCESS;

error_register:
error_dd_cntl:
    if(dd)
    {
        globus_xio_data_descriptor_destroy(dd);
    }

error_dd:
    globus_free(bounce_info);

error_alloc:
    return result;
}

globus_result_t
globus_io_register_writev(
    globus_io_handle_t *                handle,
    struct iovec *                      iov,
    globus_size_t                       iovcnt,
    globus_io_writev_callback_t         writev_callback,
    void *                              callback_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    globus_l_io_handle_t *              ihandle;
    globus_result_t                     result;
    int                                 i;
    globus_size_t                       nbytes;
    GlobusIOName(globus_io_register_writev);

    GlobusLIOCheckNullParam(writev_callback);
    GlobusLIOCheckNullParam(iov);
    GlobusLIOCheckHandle(handle, 0);

    ihandle = *handle;
    result = GlobusLIOMalloc(bounce_info, globus_l_io_bounce_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_alloc;
    }

    bounce_info->handle = ihandle;
    bounce_info->cb.writev = writev_callback;
    bounce_info->user_arg = callback_arg;
    bounce_info->blocking = GLOBUS_FALSE;
    bounce_info->cancel_info = GLOBUS_NULL;

    nbytes = 0;
    for(i = 0; i < iovcnt; i++)
    {
        nbytes += iov[i].iov_len;
    }

    globus_mutex_lock(&ihandle->pending_lock);
    {
        result = globus_xio_register_writev(
            ihandle->xio_handle,
            iov,
            iovcnt,
            nbytes,
            GLOBUS_NULL,
            globus_l_io_bounce_iovec_cb,
            bounce_info);
        if(result != GLOBUS_SUCCESS)
        {
            globus_mutex_unlock(&ihandle->pending_lock);
            goto error_register;
        }

        globus_l_io_cancel_insert(bounce_info);
    }
    globus_mutex_unlock(&ihandle->pending_lock);

    return GLOBUS_SUCCESS;

error_register:
    globus_free(bounce_info);

error_alloc:
    return result;
}

globus_result_t
globus_io_try_write(
    globus_io_handle_t *                handle,
    globus_byte_t *                     buf,
    globus_size_t                       max_nbytes,
    globus_size_t *                     nbytes_written)
{
    globus_result_t                     result;
    GlobusIOName(globus_io_try_write);

    GlobusLIOCheckNullParam(nbytes_written);
    *nbytes_written = 0;
    GlobusLIOCheckHandle(handle, 0);

    result = globus_xio_write(
        (*handle)->xio_handle,
        buf,
        max_nbytes,
        0,
        nbytes_written,
        GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_write;
    }

    return GLOBUS_SUCCESS;

error_write:
    if(globus_xio_error_is_canceled(result))
    {
        result = globus_error_put(
            globus_io_error_construct_io_cancelled(
                GLOBUS_IO_MODULE,
                globus_error_get(result),
                (*handle)->io_handle));
    }

    return result;
}

globus_result_t
globus_io_try_send(
    globus_io_handle_t *                handle,
    globus_byte_t *                     buf,
    globus_size_t                       nbytes,
    int                                 flags,
    globus_size_t *                     nbytes_sent)
{
    globus_result_t                     result;
    globus_xio_data_descriptor_t        dd;
    GlobusIOName(globus_io_try_send);

    GlobusLIOCheckNullParam(nbytes_sent);
    *nbytes_sent = 0;
    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);

    if(flags)
    {
        result = globus_xio_data_descriptor_init(&dd, (*handle)->xio_handle);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_dd;
        }

        result = globus_xio_data_descriptor_cntl(
            dd,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_SET_SEND_FLAGS,
            flags);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_dd_cntl;
        }
    }
    else
    {
        dd = GLOBUS_NULL;
    }

    result = globus_xio_write(
        (*handle)->xio_handle,
        buf,
        nbytes,
        0,
        nbytes_sent,
        dd);
    dd = GLOBUS_NULL; /* XXX is xio freeing this for us ?? */
    if(result != GLOBUS_SUCCESS)
    {
        goto error_write;
    }
    return GLOBUS_SUCCESS;

error_write:
    if(globus_xio_error_is_canceled(result))
    {
        result = globus_error_put(
            globus_io_error_construct_io_cancelled(
                GLOBUS_IO_MODULE,
                globus_error_get(result),
                (*handle)->io_handle));
    }

error_dd_cntl:
    if(dd)
    {
        globus_xio_data_descriptor_destroy(dd);
    }

error_dd:
    return result;
}

globus_result_t
globus_io_write(
    globus_io_handle_t *                handle,
    globus_byte_t *                     buf,
    globus_size_t                       nbytes,
    globus_size_t *                     nbytes_written)
{
    globus_result_t                     result;
    GlobusIOName(globus_io_write);

    GlobusLIOCheckNullParam(nbytes_written);
    *nbytes_written = 0;
    GlobusLIOCheckHandle(handle, 0);

    result = globus_xio_write(
        (*handle)->xio_handle,
        buf,
        nbytes,
        nbytes,
        nbytes_written,
        GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_write;
    }

    return GLOBUS_SUCCESS;

error_write:
    if(globus_xio_error_is_canceled(result))
    {
        result = globus_error_put(
            globus_io_error_construct_io_cancelled(
                GLOBUS_IO_MODULE,
                globus_error_get(result),
                (*handle)->io_handle));
    }

    return result;
}

globus_result_t
globus_io_send(
    globus_io_handle_t *                handle,
    globus_byte_t *                     buf,
    globus_size_t                       nbytes,
    int                                 flags,
    globus_size_t *                     nbytes_sent)
{
    globus_result_t                     result;
    globus_xio_data_descriptor_t        dd;
    GlobusIOName(globus_io_send);

    GlobusLIOCheckNullParam(nbytes_sent);
    *nbytes_sent = 0;
    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);

    if(flags)
    {
        result = globus_xio_data_descriptor_init(&dd, (*handle)->xio_handle);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_dd;
        }

        result = globus_xio_data_descriptor_cntl(
            dd,
            globus_l_io_tcp_driver,
            GLOBUS_XIO_TCP_SET_SEND_FLAGS,
            flags);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_dd_cntl;
        }
    }
    else
    {
        dd = GLOBUS_NULL;
    }

    result = globus_xio_write(
        (*handle)->xio_handle,
        buf,
        nbytes,
        nbytes,
        nbytes_sent,
        dd);
    dd = GLOBUS_NULL; /* XXX is xio freeing this for us ?? */
    if(result != GLOBUS_SUCCESS)
    {
        goto error_write;
    }

    return GLOBUS_SUCCESS;

error_write:
    if(globus_xio_error_is_canceled(result))
    {
        result = globus_error_put(
            globus_io_error_construct_io_cancelled(
                GLOBUS_IO_MODULE,
                globus_error_get(result),
                (*handle)->io_handle));
    }

error_dd_cntl:
    if(dd)
    {
        globus_xio_data_descriptor_destroy(dd);
    }

error_dd:
    return result;
}

globus_result_t
globus_io_writev(
    globus_io_handle_t *                handle,
    struct iovec *                      iov,
    globus_size_t                       iovcnt,
    globus_size_t *                     bytes_written)
{
    globus_result_t                     result;
    int                                 i;
    globus_size_t                       nbytes;
    GlobusIOName(globus_io_writev);

    GlobusLIOCheckNullParam(bytes_written);
    *bytes_written = 0;
    GlobusLIOCheckNullParam(iov);
    GlobusLIOCheckHandle(handle, 0);

    nbytes = 0;
    for(i = 0; i < iovcnt; i++)
    {
        nbytes += iov[i].iov_len;
    }

    result = globus_xio_writev(
        (*handle)->xio_handle,
        iov,
        iovcnt,
        nbytes,
        bytes_written,
        GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_write;
    }

    return GLOBUS_SUCCESS;

error_write:
    if(globus_xio_error_is_canceled(result))
    {
        result = globus_error_put(
            globus_io_error_construct_io_cancelled(
                GLOBUS_IO_MODULE,
                globus_error_get(result),
                (*handle)->io_handle));
    }

    return result;
}

/* miscelaneous */

static
void
globus_l_io_bounce_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg);

static
void
globus_l_io_bounce_close_kickout(
    void *                              user_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    GlobusIOName(globus_l_io_bounce_close_kickout);

    bounce_info = (globus_l_io_bounce_t *) user_arg;

    globus_l_io_bounce_close_cb(
        GLOBUS_NULL,
        bounce_info->error
            ? globus_error_put(bounce_info->error) : GLOBUS_SUCCESS,
        user_arg);
}

static
void
globus_l_io_bounce_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    GlobusIOName(globus_l_io_bounce_close_cb);

    bounce_info = (globus_l_io_bounce_t *) user_arg;

    if(globus_l_io_should_bounce(bounce_info))
    {
        bounce_info->error = result == GLOBUS_SUCCESS
            ? GLOBUS_NULL : globus_error_get(result);
        result = globus_callback_space_register_oneshot(
            GLOBUS_NULL,
            GLOBUS_NULL,
            globus_l_io_bounce_close_kickout,
            bounce_info,
            bounce_info->blocking
                ? GLOBUS_CALLBACK_GLOBAL_SPACE : bounce_info->handle->space);
        if(result != GLOBUS_SUCCESS)
        {
            globus_panic(
                GLOBUS_IO_MODULE,
                result,
                _IOSL("[%s:%d] Couldn't register callback"),
                _io_name,
                __LINE__);
        }
        return;
    }

    bounce_info->cb.non_io(
        bounce_info->user_arg,
        bounce_info->handle->io_handle,
        result);

    globus_mutex_lock(&bounce_info->handle->pending_lock);
    {
        bounce_info->handle->refs--;
    }
    globus_mutex_unlock(&bounce_info->handle->pending_lock);

    globus_l_io_handle_destroy(bounce_info->handle);
    globus_free(bounce_info);
}

static
void
globus_l_io_server_close_cb(
    globus_xio_server_t                 xio_server,
    void *                              user_arg);

static
void
globus_l_io_server_close_kickout(
    void *                              user_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    GlobusIOName(globus_l_io_server_close_kickout);

    bounce_info = (globus_l_io_bounce_t *) user_arg;

    globus_l_io_server_close_cb(GLOBUS_NULL, user_arg);
}

static
void
globus_l_io_server_close_cb(
    globus_xio_server_t                 xio_server,
    void *                              user_arg)
{
    globus_l_io_bounce_t *              bounce_info;
    GlobusIOName(globus_l_io_server_close_cb);

    bounce_info = (globus_l_io_bounce_t *) user_arg;

    if(globus_l_io_should_bounce(bounce_info))
    {
        globus_result_t                 result;

        result = globus_callback_space_register_oneshot(
            GLOBUS_NULL,
            GLOBUS_NULL,
            globus_l_io_server_close_kickout,
            bounce_info,
            bounce_info->blocking
                ? GLOBUS_CALLBACK_GLOBAL_SPACE : bounce_info->handle->space);
        if(result != GLOBUS_SUCCESS)
        {
            globus_panic(
                GLOBUS_IO_MODULE,
                result,
                _IOSL("[%s:%d] Couldn't register callback"),
                _io_name,
                __LINE__);
        }
        return;
    }

    bounce_info->cb.non_io(
        bounce_info->user_arg,
        bounce_info->handle->io_handle,
        GLOBUS_SUCCESS);

    globus_mutex_lock(&bounce_info->handle->pending_lock);
    {
        bounce_info->handle->refs--;
    }
    globus_mutex_unlock(&bounce_info->handle->pending_lock);

    globus_l_io_handle_destroy(bounce_info->handle);
    globus_free(bounce_info);
}

globus_result_t
globus_l_io_register_close(
    globus_io_handle_t *                handle,
    globus_io_callback_t                callback,
    void *                              callback_arg,
    globus_bool_t                       blocking)
{
    globus_l_io_bounce_t *              bounce_info;
    globus_l_io_handle_t *              ihandle;
    globus_result_t                     result;
    GlobusIOName(globus_io_register_close);

    GlobusLIOCheckNullParam(callback);
    GlobusLIOCheckHandle(handle, 0);
    ihandle = *handle;

    result = GlobusLIOMalloc(bounce_info, globus_l_io_bounce_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_alloc;
    }

    bounce_info->handle = ihandle;
    bounce_info->cb.non_io = callback;
    bounce_info->user_arg = callback_arg;
    bounce_info->blocking = blocking;

    globus_mutex_lock(&ihandle->pending_lock);
    {
        if(ihandle->xio_handle)
        {
            globus_xio_handle_t             xio_handle;

            xio_handle = ihandle->xio_handle;
            ihandle->xio_handle = GLOBUS_NULL;

            result = globus_xio_register_close(
                xio_handle,
                GLOBUS_NULL,
                globus_l_io_bounce_close_cb,
                bounce_info);
        }
        else if(ihandle->xio_server)
        {
            globus_xio_server_t             xio_server;

            xio_server = ihandle->xio_server;
            ihandle->xio_server = GLOBUS_NULL;

            result = globus_xio_server_register_close(
                xio_server,
                globus_l_io_server_close_cb,
                bounce_info);
        }
        else
        {
            result = globus_error_put(
                globus_io_error_construct_not_initialized(
                    GLOBUS_IO_MODULE,
                    GLOBUS_NULL,
                    "handle",
                    1,
                    (char *) _io_name));
        }

        if(result == GLOBUS_SUCCESS)
        {
            ihandle->refs++;
        }
    }
    globus_mutex_unlock(&ihandle->pending_lock);

    if(result != GLOBUS_SUCCESS)
    {
        goto error_register;
    }

    return GLOBUS_SUCCESS;

error_register:
    globus_free(bounce_info);

error_alloc:
    *handle = GLOBUS_NULL;
    return result;
}

globus_result_t
globus_io_register_close(
    globus_io_handle_t *                handle,
    globus_io_callback_t                callback,
    void *                              callback_arg)
{
    return globus_l_io_register_close(
        handle, callback, callback_arg, GLOBUS_FALSE);
}

globus_result_t
globus_io_close(
    globus_io_handle_t *                handle)
{
    globus_result_t                     result;
    globus_l_io_monitor_t               monitor;
    GlobusIOName(globus_io_close);

    monitor.done = GLOBUS_FALSE;
    globus_mutex_init(&monitor.lock, GLOBUS_NULL);
    globus_cond_init(&monitor.cond, GLOBUS_NULL);

    result = globus_l_io_register_close(
        handle, globus_l_io_blocking_cb, &monitor, GLOBUS_TRUE);
    if(result != GLOBUS_SUCCESS)
    {
        monitor.done = GLOBUS_TRUE;
        monitor.error = globus_error_get(result);
    }

    globus_mutex_lock(&monitor.lock);
    {
        while(!monitor.done)
        {
            globus_cond_wait(&monitor.cond, &monitor.lock);
        }
    }
    globus_mutex_unlock(&monitor.lock);

    globus_mutex_destroy(&monitor.lock);
    globus_cond_destroy(&monitor.cond);

    if(monitor.error != GLOBUS_NULL)
    {
        result = globus_error_put(monitor.error);
        goto error_register;
    }

    return GLOBUS_SUCCESS;

error_register:
    return result;
}

globus_bool_t
globus_io_eof(
    globus_object_t *                   eof)
{
    GlobusIOName(globus_io_eof);

    if(eof && globus_object_get_type(eof) == GLOBUS_IO_ERROR_TYPE_EOF)
    {
        return GLOBUS_TRUE;
    }
    else
    {
        return GLOBUS_FALSE;
    }
}

globus_result_t
globus_io_tcp_posix_convert(
    int                                 socket,
    globus_io_attr_t *                  attr,
    globus_io_handle_t *                handle)
{
    GlobusIOName(globus_io_tcp_posix_convert);

    return globus_l_io_tcp_connect(GLOBUS_NULL, 0, socket, attr, handle);
}

globus_result_t
globus_io_tcp_posix_convert_listener(
    int                                 socket,
    globus_io_attr_t *                  attr,
    globus_io_handle_t *                handle)
{
    GlobusIOName(globus_io_tcp_posix_convert_listener);

    return globus_l_io_tcp_create_listener(
        GLOBUS_NULL, 0, socket, attr, handle);
}

static
globus_result_t
globus_l_io_register_cancel(
    globus_io_handle_t *                handle,
    globus_bool_t                       perform_callbacks,
    globus_io_callback_t                cancel_callback,
    void *                              cancel_arg,
    globus_bool_t                       blocking)
{
    globus_result_t                     result;
    globus_l_io_handle_t *              ihandle;
    globus_l_io_cancel_info_t *         cancel_info;
    globus_l_io_bounce_t *              bounce_info;
    GlobusIOName(globus_io_register_cancel);

    GlobusLIOCheckHandle(handle, 0);
    ihandle = *handle;

    result = GlobusLIOMalloc(cancel_info, globus_l_io_cancel_info_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_cancel_info;
    }

    cancel_info->handle = handle;
    cancel_info->refs = 0;
    cancel_info->blocking = blocking;
    cancel_info->perform_callbacks = perform_callbacks;
    cancel_info->callback = cancel_callback;
    cancel_info->user_arg = cancel_arg;

    globus_mutex_lock(&ihandle->pending_lock);
    {
        if(ihandle->xio_handle)
        {
            result = globus_xio_handle_cancel_operations(
                ihandle->xio_handle,
                GLOBUS_XIO_CANCEL_OPEN |
                    GLOBUS_XIO_CANCEL_READ | GLOBUS_XIO_CANCEL_WRITE);
        }
        else if(ihandle->xio_server)
        {
            result = globus_xio_server_cancel_accept(ihandle->xio_server);
        }
        else
        {
            result = globus_error_put(
                globus_io_error_construct_bad_pointer(
                    GLOBUS_IO_MODULE,
                    GLOBUS_NULL,
                    "handle",
                    1,
                    (char *) _io_name));
        }

        if(result == GLOBUS_SUCCESS)
        {
            globus_list_t *             pending;

            pending = ihandle->pending_ops;
            ihandle->pending_ops = GLOBUS_NULL;

            while(!globus_list_empty(pending))
            {
                bounce_info = globus_list_remove(&pending, pending);
                bounce_info->cancel_info = cancel_info;
                cancel_info->refs++;
            }
        }

        if(cancel_info->refs == 0)
        {
            result = globus_callback_space_register_oneshot(
                GLOBUS_NULL,
                GLOBUS_NULL,
                globus_l_io_cancel_kickout,
                cancel_info,
                cancel_info->blocking
                    ? GLOBUS_CALLBACK_GLOBAL_SPACE : ihandle->space);
            if(result != GLOBUS_SUCCESS)
            {
                goto error_oneshot;
            }
        }
    }
    globus_mutex_unlock(&ihandle->pending_lock);

    return GLOBUS_SUCCESS;

error_oneshot:
    globus_mutex_unlock(&ihandle->pending_lock);
    globus_free(cancel_info);

error_cancel_info:
    return result;
}

globus_result_t
globus_io_register_cancel(
    globus_io_handle_t *                handle,
    globus_bool_t                       perform_callbacks,
    globus_io_callback_t                cancel_callback,
    void *                              cancel_arg)
{
    return globus_l_io_register_cancel(
        handle, perform_callbacks, cancel_callback, cancel_arg, GLOBUS_FALSE);
}

globus_result_t
globus_io_cancel(
    globus_io_handle_t *                handle,
    globus_bool_t                       perform_callbacks)
{
    globus_result_t                     result;
    globus_l_io_monitor_t               monitor;
    GlobusIOName(globus_io_cancel);

    monitor.done = GLOBUS_FALSE;
    globus_mutex_init(&monitor.lock, GLOBUS_NULL);
    globus_cond_init(&monitor.cond, GLOBUS_NULL);

    result = globus_l_io_register_cancel(
        handle,
        perform_callbacks,
        globus_l_io_blocking_cb,
        &monitor,
        GLOBUS_TRUE);
    if(result != GLOBUS_SUCCESS)
    {
        monitor.done = GLOBUS_TRUE;
        monitor.error = globus_error_get(result);
    }

    globus_mutex_lock(&monitor.lock);
    {
        while(!monitor.done)
        {
            globus_cond_wait(&monitor.cond, &monitor.lock);
        }
    }
    globus_mutex_unlock(&monitor.lock);

    globus_mutex_destroy(&monitor.lock);
    globus_cond_destroy(&monitor.cond);

    if(monitor.error != GLOBUS_NULL)
    {
        result = globus_error_put(monitor.error);
        goto error_register;
    }

    return GLOBUS_SUCCESS;

error_register:
    return result;
}

static
void
globus_l_io_bounce_select_cb(
    void *                              arg,
    globus_io_handle_t *                handle,
    globus_result_t                     result,
    globus_byte_t *                     buf,
    globus_size_t                       nbytes)
{
    globus_io_callback_t                cb;

    /* callback is buried in buffer... bad me */
    cb = (globus_io_callback_t) buf;

    cb(arg, handle, result);
}

globus_result_t
globus_io_register_select(
    globus_io_handle_t *                handle,
    globus_io_callback_t                read_callback_func,
    void *                              read_callback_arg,
    globus_io_callback_t                write_callback_func,
    void *                              write_callback_arg,
    globus_io_callback_t                except_callback_func,
    void *                              except_callback_arg)
{
    globus_l_io_handle_t *              ihandle;
    globus_result_t                     result;
    GlobusIOName(globus_io_register_select);

    GlobusLIOCheckHandle(handle, 0);
    ihandle = *handle;

    if(except_callback_func)
    {
        result = globus_error_put(
            globus_error_construct_error(
                GLOBUS_XIO_MODULE,
                GLOBUS_NULL,
                GLOBUS_XIO_ERROR_PARAMETER,
                __FILE__,
                _io_name,
                __LINE__,
                _IOSL("Globus IO-XIO does not support use of the except callback")));
        goto error_notsupported;
    }

    if(read_callback_func)
    {
        result = globus_io_register_read(
            handle,
            (globus_byte_t *) read_callback_func,
            0,
            0,
            globus_l_io_bounce_select_cb,
            read_callback_arg);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_register;
        }
    }

    if(write_callback_func)
    {
        result = globus_io_register_write(
            handle,
            (globus_byte_t *) write_callback_func,
            0,
            globus_l_io_bounce_select_cb,
            write_callback_arg);
        if(result != GLOBUS_SUCCESS)
        {
            /* XXX if the read callback was registered, I probably need
             * to panic, because I am too lazy to stop the read from happening
             */
            goto error_register;
        }
    }

    return GLOBUS_SUCCESS;

error_register:
error_notsupported:
    return result;
}

globus_io_handle_type_t
globus_io_get_handle_type(
    globus_io_handle_t *                handle)
{
    globus_l_io_handle_t *              ihandle;
    GlobusIOName(globus_io_get_handle_type);

    GlobusLIOCheckHandle(handle, 0);
    ihandle = *handle;

    if(ihandle->type == GLOBUS_I_IO_FILE_HANDLE)
        return GLOBUS_IO_HANDLE_TYPE_FILE;
    else
        return GLOBUS_IO_HANDLE_TYPE_TCP_CONNECTED;
}

globus_result_t
globus_io_handle_get_user_pointer(
    globus_io_handle_t *                handle,
    void **                             user_pointer)
{
    GlobusIOName(globus_io_handle_get_user_pointer);

    GlobusLIOCheckHandle(handle, 0);
    GlobusLIOCheckNullParam(user_pointer);

    *user_pointer = (*handle)->user_pointer;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_handle_set_user_pointer(
    globus_io_handle_t *                handle,
    void *                              user_pointer)
{
    GlobusIOName(globus_io_handle_set_user_pointer);

    GlobusLIOCheckHandle(handle, 0);

    (*handle)->user_pointer = user_pointer;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_handle_get_xio_handle(
    globus_io_handle_t *                handle,
    globus_xio_handle_t *               xio_handle)
{
    GlobusIOName(globus_io_handle_get_xio_handle);

    GlobusLIOCheckHandle(handle, 0);
    GlobusLIOCheckNullParam(xio_handle);

    *xio_handle = (*handle)->xio_handle;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_tcp_get_security_context(
    globus_io_handle_t *                handle,
    gss_ctx_id_t *                      context)
{
    GlobusIOName(globus_io_tcp_get_security_context);

    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);
    GlobusLIOCheckNullParam(context);

    return globus_xio_handle_cntl(
        (*handle)->xio_handle,
        globus_l_io_gsi_driver,
        GLOBUS_XIO_GSI_GET_CONTEXT,
        context);
}

globus_result_t
globus_io_tcp_get_delegated_credential(
    globus_io_handle_t *                handle,
    gss_cred_id_t *                     cred)
{
    GlobusIOName(globus_io_tcp_get_delegated_credential);

    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);
    GlobusLIOCheckNullParam(cred);

    return globus_xio_handle_cntl(
        (*handle)->xio_handle,
        globus_l_io_gsi_driver,
        GLOBUS_XIO_GSI_GET_DELEGATED_CRED,
        cred);
}

/* new api just for gram_protocol_io */
globus_result_t
globus_io_tcp_set_credential(
    globus_io_handle_t *                handle,
    gss_cred_id_t                       credential)
{
    GlobusIOName(globus_io_tcp_set_credential);

    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);

    if ((*handle)->xio_handle)
    {
        return globus_xio_handle_cntl(
            (*handle)->xio_handle,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_CREDENTIAL,
            credential);
    }
    else
    {
        return globus_xio_attr_cntl(
            (*handle)->attr->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_CREDENTIAL,
            credential);
    }
}

/* new api just for gram_protocol_io */
globus_result_t
globus_io_tcp_get_credential(
    globus_io_handle_t *                handle,
    gss_cred_id_t *                     credential)
{
    GlobusIOName(globus_io_tcp_get_credential);

    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);
    GlobusLIOCheckNullParam(credential);

    return globus_xio_handle_cntl(
        (*handle)->xio_handle,
        globus_l_io_gsi_driver,
        GLOBUS_XIO_GSI_GET_CREDENTIAL,
        credential);
}

static
void
globus_l_io_init_delegation_cb(
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_io_delegation_cb_arg_t *   wrapper;
    GlobusIOName(globus_l_io_init_delegation_cb);

    wrapper = (globus_l_io_delegation_cb_arg_t *) user_arg;

    wrapper->callback(wrapper->user_arg,
                      wrapper->handle,
                      result,
                      GSS_C_NO_CREDENTIAL,
                      0);

    free(wrapper);

    return;
}

static
void
globus_l_io_accept_delegation_cb(
    globus_result_t                     result,
    gss_cred_id_t                       delegated_cred,
    OM_uint32                           time_rec,
    void *                              user_arg)
{
    globus_l_io_delegation_cb_arg_t *   wrapper;
    GlobusIOName(globus_l_io_accept_delegation_cb);

    wrapper = (globus_l_io_delegation_cb_arg_t *) user_arg;

    wrapper->callback(wrapper->user_arg,
                      wrapper->handle,
                      result,
                      delegated_cred,
                      time_rec);

    free(wrapper);

    return;
}

globus_result_t
globus_io_register_init_delegation(
    globus_io_handle_t *                handle,
    const gss_cred_id_t                 cred_handle,
    const gss_OID_set                   restriction_oids,
    const gss_buffer_set_t              restriction_buffers,
    OM_uint32                           time_req,
    globus_io_delegation_callback_t     callback,
    void *                              callback_arg)
{
    globus_result_t                     result;
    globus_l_io_handle_t *              ihandle;
    globus_l_io_delegation_cb_arg_t *   wrapper;
    GlobusIOName(globus_io_register_init_delegation);

    GlobusLIOCheckNullParam(callback);
    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);
    ihandle = *handle;

    result = GlobusLIOMalloc(wrapper, globus_l_io_delegation_cb_arg_t);
    if(result != GLOBUS_SUCCESS)
    {
        return result;
    }

    wrapper->handle = handle;
    wrapper->user_arg = callback_arg;
    wrapper->callback = callback;

    result = globus_xio_handle_cntl(
        ihandle->xio_handle,
        globus_l_io_gsi_driver,
        GLOBUS_XIO_GSI_REGISTER_INIT_DELEGATION,
        cred_handle,
        restriction_oids,
        restriction_buffers,
        time_req,
        globus_l_io_init_delegation_cb,
        wrapper);
    if(result != GLOBUS_SUCCESS)
    {
        free(wrapper);
    }

    return result;
}

globus_result_t
globus_io_init_delegation(
    globus_io_handle_t *                handle,
    const gss_cred_id_t                 cred_handle,
    const gss_OID_set                   restriction_oids,
    const gss_buffer_set_t              restriction_buffers,
    OM_uint32                           time_req)
{
    globus_result_t                     result;
    globus_l_io_handle_t *              ihandle;
    GlobusIOName(globus_io_init_delegation);

    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);
    ihandle = *handle;

    result = globus_xio_handle_cntl(
        ihandle->xio_handle,
        globus_l_io_gsi_driver,
        GLOBUS_XIO_GSI_INIT_DELEGATION,
        cred_handle,
        restriction_oids,
        restriction_buffers,
        time_req);

    return result;
}

globus_result_t
globus_io_register_accept_delegation(
    globus_io_handle_t *                handle,
    const gss_OID_set                   restriction_oids,
    const gss_buffer_set_t              restriction_buffers,
    OM_uint32                           time_req,
    globus_io_delegation_callback_t     callback,
    void *                              callback_arg)
{
    globus_result_t                     result;
    globus_l_io_handle_t *              ihandle;
    globus_l_io_delegation_cb_arg_t *   wrapper;
    GlobusIOName(globus_io_register_accept_delegation);

    GlobusLIOCheckNullParam(callback);
    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);
    ihandle = *handle;

    result = GlobusLIOMalloc(wrapper, globus_l_io_delegation_cb_arg_t);
    if(result != GLOBUS_SUCCESS)
    {
        return result;
    }

    wrapper->handle = handle;
    wrapper->user_arg = callback_arg;
    wrapper->callback = callback;

    result = globus_xio_handle_cntl(
        ihandle->xio_handle,
        globus_l_io_gsi_driver,
        GLOBUS_XIO_GSI_REGISTER_ACCEPT_DELEGATION,
        restriction_oids,
        restriction_buffers,
        time_req,
        globus_l_io_accept_delegation_cb,
        wrapper);
    if(result != GLOBUS_SUCCESS)
    {
        free(wrapper);
    }

    return result;
}

globus_result_t
globus_io_accept_delegation(
    globus_io_handle_t *                handle,
    gss_cred_id_t *                     delegated_cred,
    const gss_OID_set                   restriction_oids,
    const gss_buffer_set_t              restriction_buffers,
    OM_uint32                           time_req,
    OM_uint32 *                         time_rec)
{
    globus_result_t                     result;
    globus_l_io_handle_t *              ihandle;
    GlobusIOName(globus_io_accept_delegation);

    GlobusLIOCheckHandle(handle, GLOBUS_I_IO_TCP_HANDLE);
    ihandle = *handle;

    result = globus_xio_handle_cntl(
        ihandle->xio_handle,
        globus_l_io_gsi_driver,
        GLOBUS_XIO_GSI_ACCEPT_DELEGATION,
        delegated_cred,
        restriction_oids,
        restriction_buffers,
        time_req,
        time_rec);

    return result;

}

globus_result_t
globus_io_attr_set_secure_authentication_mode(
    globus_io_attr_t *                  attr,
    globus_io_secure_authentication_mode_t
                                        mode,
    gss_cred_id_t                       credential)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    GlobusIOName(globus_io_attr_set_secure_authentication_mode);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);
    (*attr)->authentication_mode = mode;

    switch(mode)
    {
      case GLOBUS_IO_SECURE_AUTHENTICATION_MODE_GSSAPI:
      case GLOBUS_IO_SECURE_AUTHENTICATION_MODE_MUTUAL:
        if(credential != GSS_C_NO_CREDENTIAL)
        {
            result = globus_xio_attr_cntl(
                (*attr)->attr,
                globus_l_io_gsi_driver,
                GLOBUS_XIO_GSI_SET_CREDENTIAL,
                credential);
        }
        break;
      case GLOBUS_IO_SECURE_AUTHENTICATION_MODE_ANONYMOUS:
        result = globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_ANON);
        break;
      case GLOBUS_IO_SECURE_AUTHENTICATION_MODE_NONE:
        break;
      default:
        globus_assert(0 && "Unexpected state");
        break;
    }

    return result;
}

globus_result_t
globus_io_attr_get_secure_authentication_mode(
    globus_io_attr_t *                  attr,
    globus_io_secure_authentication_mode_t *
                                        mode,
    gss_cred_id_t *                     credential)
{
    GlobusIOName(globus_io_attr_get_secure_authentication_mode);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);
    GlobusLIOCheckNullParam(mode);

    *mode = (*attr)->authentication_mode;
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_attr_set_secure_authorization_mode(
    globus_io_attr_t *                  attr,
    globus_io_secure_authorization_mode_t
                                        mode,
    globus_io_secure_authorization_data_t *
                                        data)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    GlobusIOName(globus_io_attr_set_secure_authorization_mode);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);
    (*attr)->authorization_mode = mode;

    switch(mode)
    {
      case GLOBUS_IO_SECURE_AUTHORIZATION_MODE_NONE:
      case GLOBUS_IO_SECURE_AUTHORIZATION_MODE_SELF:
      case GLOBUS_IO_SECURE_AUTHORIZATION_MODE_HOST:
        if((*attr)->authz_data.identity != GSS_C_NO_NAME)
        {
            gss_release_name(&minor_status,
                             &(*attr)->authz_data.identity);
            (*attr)->authz_data.identity = GSS_C_NO_NAME;
        }
        result = globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_AUTHORIZATION_MODE,
            mode);
        break;
      case GLOBUS_IO_SECURE_AUTHORIZATION_MODE_IDENTITY:
        GlobusLIOCheckNullParam(data);
        if((*attr)->authz_data.identity != GSS_C_NO_NAME)
        {
            gss_release_name(&minor_status,
                             &(*attr)->authz_data.identity);
        }

        major_status = gss_duplicate_name(&minor_status,
                                          (*data)->identity,
                                          &(*attr)->authz_data.identity);

        if(GSS_ERROR(major_status))
        {
            result = GlobusLIOErrorWrapGSSFailed("gss_duplicate_name",
                                                 major_status,
                                                 minor_status);
            goto error;
        }

        result = globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_AUTHORIZATION_MODE,
            mode);

        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }

        result = globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_TARGET_NAME,
            (*attr)->authz_data.identity);
        break;
      case GLOBUS_IO_SECURE_AUTHORIZATION_MODE_CALLBACK:
        GlobusLIOCheckNullParam(data);
        (*attr)->authz_data.callback = (*data)->callback;
        (*attr)->authz_data.callback_arg = (*data)->callback_arg;
        result = globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_AUTHORIZATION_MODE,
            GLOBUS_IO_SECURE_AUTHORIZATION_MODE_NONE);
        break;
    }

 error:
    return result;
}

globus_result_t
globus_io_attr_get_secure_authorization_mode(
    globus_io_attr_t *                  attr,
    globus_io_secure_authorization_mode_t *
                                        mode,
    globus_io_secure_authorization_data_t *
                                        data)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    GlobusIOName(globus_io_attr_get_secure_authorization_mode);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);
    GlobusLIOCheckNullParam(data);
    GlobusLIOCheckNullParam(mode);

    result = GlobusLIOMalloc(*data, globus_l_io_secure_authorization_data_t);
    if(result != GLOBUS_SUCCESS)
    {
        goto done;
    }

    memset(*data, 0, sizeof(globus_l_io_secure_authorization_data_t));
    *mode = (*attr)->authorization_mode;

    switch((*attr)->authorization_mode)
    {
      case GLOBUS_IO_SECURE_AUTHORIZATION_MODE_NONE:
        break;
      case GLOBUS_IO_SECURE_AUTHORIZATION_MODE_SELF:
        break;
      case GLOBUS_IO_SECURE_AUTHORIZATION_MODE_HOST:
        break;
      case GLOBUS_IO_SECURE_AUTHORIZATION_MODE_IDENTITY:
        major_status = gss_duplicate_name(&minor_status,
                                          (*attr)->authz_data.identity,
                                          &(*data)->identity);
        if(GSS_ERROR(major_status))
        {
            result = GlobusLIOErrorWrapGSSFailed("gss_duplicate_name",
                                                 major_status,
                                                 minor_status);
        }
        break;
      case GLOBUS_IO_SECURE_AUTHORIZATION_MODE_CALLBACK:
        (*data)->callback = (*attr)->authz_data.callback;
        (*data)->callback_arg = (*attr)->authz_data.callback_arg;
        break;
    }

 done:

    return result;
}

globus_result_t
globus_io_attr_set_secure_extension_oids(
    globus_io_attr_t *                  attr,
    gss_OID_set                         extension_oids)
{
    GlobusIOName(globus_io_attr_set_secure_extension_oids);
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_attr_get_secure_extension_oids(
    globus_io_attr_t *                  attr,
    gss_OID_set *                       extension_oids)
{
    GlobusIOName(globus_io_attr_get_secure_extension_oids);
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_secure_authorization_data_initialize(
    globus_io_secure_authorization_data_t * data)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    GlobusIOName(globus_io_secure_authorization_data_initialize);

    GlobusLIOCheckNullParam(data);

    result = GlobusLIOMalloc(*data, globus_l_io_secure_authorization_data_t);
    if(result == GLOBUS_SUCCESS)
    {
        memset(*data, 0, sizeof(globus_l_io_secure_authorization_data_t));
    }

    return result;
}

globus_result_t
globus_io_secure_authorization_data_destroy(
    globus_io_secure_authorization_data_t *
                                        data)
{
    GlobusIOName(globus_io_secure_authorization_data_destroy);

    GlobusLIOCheckNullParam(data);

    if((*data)->identity != GSS_C_NO_NAME)
    {
        OM_uint32                       minor_status;

        gss_release_name(&minor_status,
                         &(*data)->identity);
    }

    globus_free(*data);
    *data = GLOBUS_NULL;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_secure_authorization_data_set_identity(
    globus_io_secure_authorization_data_t *
                                        data,
    char *                              identity)
{
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    gss_buffer_desc                     name_buffer;
    gss_OID                             name_type = GSS_C_NO_OID;
    globus_result_t                     result = GLOBUS_SUCCESS;
    GlobusIOName(globus_io_secure_authorization_data_set_identity);

    GlobusLIOCheckNullParam(data);
    GlobusLIOCheckNullParam(identity);

    if(!strncmp("GSI-NO-TARGET", identity, 13))
    {
        (*data)->identity = GSS_C_NO_NAME;
    }
    else
    {
        name_buffer.value = identity;
        name_buffer.length = strlen(identity);

        /*
         * A  GSS_C_NT_HOSTBASED_SERVICE is of the form service@FQDN
         * At least the Globus gssapi, and the Kerberos gssapi
         * use the same form. We will check for
         * two special forms here: host@FQDN and ftp@FQDN
         */

        if (strchr(identity,'@') &&
            !strstr(identity,"CN="))
        {
            name_type = GSS_C_NT_HOSTBASED_SERVICE;
        }
        else if(!strncmp("<anonymous>",identity, 11))
        {
            name_type = GSS_C_NT_ANONYMOUS;
        }

        major_status = gss_import_name(&minor_status,
                                       &name_buffer,
                                       name_type,
                                       &(*data)->identity);
        if(GSS_ERROR(major_status))
        {
            result = GlobusLIOErrorWrapGSSFailed("gss_import_name",
                                                 major_status,
                                                 minor_status);
        }
    }

    return result;
}

globus_result_t
globus_io_secure_authorization_data_get_identity(
    globus_io_secure_authorization_data_t *
                                        data,
    char **                             identity)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    GlobusIOName(globus_io_secure_authorization_data_get_identity);

    GlobusLIOCheckNullParam(data);
    GlobusLIOCheckNullParam(identity);

    if((*data)->identity != GSS_C_NO_NAME)
    {
        OM_uint32                       major_status;
        OM_uint32                       minor_status;
        gss_buffer_desc                 name_buffer;

        major_status = gss_display_name(&minor_status,
                                        (*data)->identity,
                                        &name_buffer,
                                        GLOBUS_NULL);
        if(GSS_ERROR(major_status))
        {
            result = GlobusLIOErrorWrapGSSFailed("gss_export_name",
                                                 major_status,
                                                 minor_status);
            goto done;
        }

        /* could probably just realloc the buffer contents */

        result = GlobusLIOMallocSize(*identity, name_buffer.length + 1);

        if(result != GLOBUS_SUCCESS)
        {
            gss_release_buffer(&minor_status,
                               &name_buffer);
            goto done;
        }

        memcpy(*identity, name_buffer.value, name_buffer.length);

        (*identity)[name_buffer.length] = '\0';

        gss_release_buffer(&minor_status,
                           &name_buffer);
    }
    else
    {
        *identity = GLOBUS_NULL;
    }

 done:

    return result;
}

globus_result_t
globus_io_secure_authorization_data_set_callback(
    globus_io_secure_authorization_data_t *
                                        data,
    globus_io_secure_authorization_callback_t
                                        callback,
    void *                              callback_arg)
{
    GlobusIOName(globus_io_secure_authorization_data_set_callback);

    GlobusLIOCheckNullParam(data);

    (*data)->callback = callback;
    (*data)->callback_arg = callback_arg;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_secure_authorization_data_get_callback(
    globus_io_secure_authorization_data_t *
                                        data,
    globus_io_secure_authorization_callback_t *
                                        callback,
    void **                             callback_arg)
{
    GlobusIOName(globus_io_secure_authorization_data_get_callback);

    GlobusLIOCheckNullParam(data);
    GlobusLIOCheckNullParam(callback);
    GlobusLIOCheckNullParam(callback_arg);

    *callback = (*data)->callback;
    *callback_arg = (*data)->callback_arg;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_attr_set_secure_channel_mode(
    globus_io_attr_t *                  attr,
    globus_io_secure_channel_mode_t     mode)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_xio_gsi_protection_level_t   protection_level;

    GlobusIOName(globus_io_attr_set_secure_channel_mode);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    (*attr)->channel_mode = mode;
    switch(mode)
    {
      case GLOBUS_IO_SECURE_CHANNEL_MODE_CLEAR:
        result = globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_SSL_COMPATIBLE,
            GLOBUS_FALSE);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }

        result = globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_PROTECTION_LEVEL,
            GLOBUS_XIO_GSI_PROTECTION_LEVEL_NONE);
        break;

      case GLOBUS_IO_SECURE_CHANNEL_MODE_GSI_WRAP_SSL3:
        result = globus_error_put(
            globus_io_error_construct_bad_parameter(
                GLOBUS_IO_MODULE,
                NULL,
                "mode",
                1,
                (char *) _io_name));
        goto error;

      case GLOBUS_IO_SECURE_CHANNEL_MODE_GSI_WRAP:
        result = globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_WRAP_MODE,
            GLOBUS_TRUE);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }

        result = globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_GET_PROTECTION_LEVEL,
            &protection_level);

        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }

        if(protection_level == GLOBUS_XIO_GSI_PROTECTION_LEVEL_NONE)
        {
            result = globus_xio_attr_cntl(
                (*attr)->attr,
                globus_l_io_gsi_driver,
                GLOBUS_XIO_GSI_SET_PROTECTION_LEVEL,
                GLOBUS_XIO_GSI_PROTECTION_LEVEL_INTEGRITY);
        }
        break;

      case GLOBUS_IO_SECURE_CHANNEL_MODE_SSL_WRAP:
        result = globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_SSL_COMPATIBLE,
            GLOBUS_TRUE);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }

        result = globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_GET_PROTECTION_LEVEL,
            &protection_level);

        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }

        if(protection_level == GLOBUS_XIO_GSI_PROTECTION_LEVEL_NONE)
        {
            result = globus_xio_attr_cntl(
                (*attr)->attr,
                globus_l_io_gsi_driver,
                GLOBUS_XIO_GSI_SET_PROTECTION_LEVEL,
                GLOBUS_XIO_GSI_PROTECTION_LEVEL_INTEGRITY);
        }
        break;
    }

 error:
    return result;
}

globus_result_t
globus_io_attr_get_secure_channel_mode(
    globus_io_attr_t *                  attr,
    globus_io_secure_channel_mode_t *   mode)
{
    GlobusIOName(globus_io_attr_get_secure_channel_mode);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);
    GlobusLIOCheckNullParam(mode);

    *mode = (*attr)->channel_mode;
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_attr_set_secure_protection_mode(
    globus_io_attr_t *                  attr,
    globus_io_secure_protection_mode_t  mode)
{
    GlobusIOName(globus_io_attr_set_secure_protection_mode);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_PROTECTION_LEVEL,
            mode);
}

globus_result_t
globus_io_attr_get_secure_protection_mode(
    globus_io_attr_t *                  attr,
    globus_io_secure_protection_mode_t *mode)
{
    GlobusIOName(globus_io_attr_get_secure_protection_mode);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);
    GlobusLIOCheckNullParam(mode);

    return globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_GET_PROTECTION_LEVEL,
            mode);
}

globus_result_t
globus_io_attr_set_secure_delegation_mode(
    globus_io_attr_t *                  attr,
    globus_io_secure_delegation_mode_t  mode)
{
    GlobusIOName(globus_io_attr_set_secure_delegation_mode);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_DELEGATION_MODE,
            mode);
}


globus_result_t
globus_io_attr_get_secure_delegation_mode(
    globus_io_attr_t *                  attr,
    globus_io_secure_delegation_mode_t *
                                        mode)
{
    GlobusIOName(globus_io_attr_set_secure_delegation_mode);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);
    GlobusLIOCheckNullParam(mode);

    return globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_GET_DELEGATION_MODE,
            mode);
}

globus_result_t
globus_io_attr_set_secure_proxy_mode(
    globus_io_attr_t *                  attr,
    globus_io_secure_proxy_mode_t       mode)
{
    GlobusIOName(globus_io_attr_set_secure_proxy_mode);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);

    return globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_SET_PROXY_MODE,
            mode);
}

globus_result_t
globus_io_attr_get_secure_proxy_mode(
    globus_io_attr_t *                  attr,
    globus_io_secure_proxy_mode_t *     mode)
{
    GlobusIOName(globus_io_attr_get_secure_proxy_mode);

    GlobusLIOCheckAttr(attr, GLOBUS_I_IO_TCP_ATTR);
    GlobusLIOCheckNullParam(mode);

    return globus_xio_attr_cntl(
            (*attr)->attr,
            globus_l_io_gsi_driver,
            GLOBUS_XIO_GSI_GET_PROXY_MODE,
            mode);
}

/* netlogger functions */

globus_result_t
globus_io_attr_netlogger_set_handle(
    globus_io_attr_t *                  attr,
    globus_netlogger_handle_t *         nl_handle)
{
    GlobusIOName(globus_io_attr_netlogger_set_handle);
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_attr_netlogger_copy_handle(
    globus_netlogger_handle_t *              src,
    globus_netlogger_handle_t *              dst)
{
    GlobusIOName(globus_io_attr_netlogger_copy_handle);
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_netlogger_write(
    globus_netlogger_handle_t *       nl_handle,
    const char *                      event,
    const char *                      id,
    const char *                      level,
    const char *                      tag)
{
    GlobusIOName(globus_netlogger_write);
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_netlogger_handle_init(
    globus_netlogger_handle_t *       gnl_handle,
    const char *                      hostname,
    const char *                      progname,
    const char *                      pid)
{
    GlobusIOName(globus_netlogger_handle_init);
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_netlogger_handle_destroy(
    globus_netlogger_handle_t *       nl_handle)
{
    GlobusIOName(globus_netlogger_handle_destroy);
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_netlogger_get_nlhandle(
    globus_netlogger_handle_t *       nl_handle,
    void **                           handle)
{
    GlobusIOName(globus_netlogger_get_nlhandle);
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_netlogger_set_desc(
    globus_netlogger_handle_t *       nl_handle,
    char *                            desc)
{
    GlobusIOName(globus_netlogger_set_desc);
    return GLOBUS_SUCCESS;
}

globus_xio_driver_t
globus_io_compat_get_tcp_driver()
{
    return globus_l_io_tcp_driver;
}

globus_xio_driver_t
globus_io_compat_get_gsi_driver()
{
    return globus_l_io_gsi_driver;
}

globus_xio_driver_t
globus_io_compat_get_file_driver()
{
    return globus_l_io_file_driver;
}

globus_result_t
globus_io_attr_get_xio_attr(
    globus_io_attr_t *                  attr,
    globus_xio_attr_t *                 xio_attr)
{
    GlobusIOName(globus_io_attr_get_xio_attr);

    GlobusLIOCheckNullParam(attr);
    *xio_attr = (*attr)->attr;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_io_attr_set_stack(
    globus_io_attr_t *                  attr,
    globus_xio_stack_t                  stack)
{
    globus_result_t                     result;
    GlobusIOName(globus_io_attr_set_stack);

    GlobusLIOCheckNullParam(attr);

    result = globus_xio_stack_copy(&(*attr)->stack, stack);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    return GLOBUS_SUCCESS;
error:
    return result;
}

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
