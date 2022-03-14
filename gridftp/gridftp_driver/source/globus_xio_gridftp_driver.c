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
#include "globus_xio_gridftp_driver.h"
#include "globus_ftp_client.h"  
#include "version.h"

GlobusDebugDefine(GLOBUS_XIO_GRIDFTP);
GlobusXIODeclareDriver(gridftp);

#define GlobusXIOGridftpDebugPrintf(level, message)                           \
    GlobusDebugPrintf(GLOBUS_XIO_GRIDFTP, level, message)

#define GlobusXIOGridftpDebugEnter()                                          \
    GlobusXIOGridftpDebugPrintf(                                              \
        GLOBUS_L_XIO_GRIDFTP_DEBUG_TRACE,                                     \
        ("[%s] Entering\n", _xio_name))

#define GlobusXIOGridftpDebugExit()                                           \
    GlobusXIOGridftpDebugPrintf(                                              \
        GLOBUS_L_XIO_GRIDFTP_DEBUG_TRACE,                                     \
        ("[%s] Exiting\n", _xio_name))

#define GlobusXIOGridftpDebugExitWithError()                                  \
    GlobusXIOGridftpDebugPrintf(                                              \
        GLOBUS_L_XIO_GRIDFTP_DEBUG_TRACE,                                     \
        ("[%s] Exiting with error\n", _xio_name))

enum globus_l_xio_error_levels
{
    GLOBUS_L_XIO_GRIDFTP_DEBUG_TRACE                = 1,
    GLOBUS_L_XIO_GRIDFTP_DEBUG_INTERNAL_TRACE       = 2
};

#define GLOBUS_XIO_GRIDFTP_REQUESTOR_COUNT 8

typedef enum globus_i_xio_gridftp_state_s
{

    GLOBUS_XIO_GRIDFTP_NONE,
    GLOBUS_XIO_GRIDFTP_OPEN,
    GLOBUS_XIO_GRIDFTP_OPENING,
    GLOBUS_XIO_GRIDFTP_IO_PENDING,
    GLOBUS_XIO_GRIDFTP_IO_DONE,
    GLOBUS_XIO_GRIDFTP_ABORT_PENDING,
    GLOBUS_XIO_GRIDFTP_ABORT_PENDING_IO_PENDING,
    GLOBUS_XIO_GRIDFTP_ABORT_PENDING_CLOSING

} globus_i_xio_gridftp_state_t;

typedef struct
{
    globus_ftp_client_handle_t *        ftp_handle;
    globus_ftp_client_operationattr_t   ftp_operation_attr;
    globus_bool_t                       partial_xfer;
    globus_bool_t			append;
    char *				eret_alg_str;
    char *				esto_alg_str;

} globus_l_xio_gridftp_attr_t;

static globus_l_xio_gridftp_attr_t      globus_l_xio_gridftp_attr_default =
{
    NULL,
    NULL,
    GLOBUS_FALSE,
    GLOBUS_FALSE,
    NULL,
    NULL
};

typedef struct
{
    globus_ftp_client_handle_t *        ftp_handle;
    globus_l_xio_gridftp_attr_t *       attr;
    globus_i_xio_gridftp_state_t        state;  
    globus_memory_t                     requestor_memory;
    globus_fifo_t                       pending_ops_q;
    char *                              url;    

    /* TRUE - read, FALSE - write */
    globus_bool_t                       outstanding_ops_direction; 
    globus_bool_t                       pending_ops_direction; 
    struct globus_i_xio_gridftp_requestor_s *  
                                        partial_requestor;        

    /* this is necessary because xfer_cb might be called before io_cb */
    globus_bool_t                       xfer_done;
    int                                 outstanding_io_count;
    /* No pending_io_count (I use globus_fifo_empty(pending_ops_q) instead) */ 

    globus_off_t                        offset;
    globus_off_t                        end_offset;
    globus_off_t                        size;
    globus_mutex_t                      mutex;
        
} globus_l_xio_gridftp_handle_t;

typedef struct globus_i_xio_gridftp_requestor_s
{
    globus_xio_operation_t              op;
    globus_xio_iovec_t *                iovec;
    int                                 iovec_count;
    globus_l_xio_gridftp_handle_t *     handle;
    globus_off_t                        offset;
    globus_size_t                       length;
    globus_object_t *                   saved_error;
    int                                 finished_count;

} globus_i_xio_gridftp_requestor_t;

typedef struct
{
    globus_xio_operation_t              op;
    globus_result_t                     result;

} globus_i_xio_gridftp_error_info_t;

static
int
globus_l_xio_gridftp_activate(void);

static
int
globus_l_xio_gridftp_deactivate(void);

static
globus_result_t
globus_i_xio_gridftp_register_get(
    globus_i_xio_gridftp_requestor_t *  requestor);

static
globus_result_t
globus_i_xio_gridftp_register_put(
    globus_i_xio_gridftp_requestor_t *  requestor);

static
globus_result_t
globus_i_xio_gridftp_register_read(
    globus_i_xio_gridftp_requestor_t *  requestor);

static
globus_result_t
globus_i_xio_gridftp_register_write(
    globus_i_xio_gridftp_requestor_t *  requestor);

static
void
globus_i_xio_gridftp_abort_io(
    globus_l_xio_gridftp_handle_t *     handle);

static      
globus_result_t
globus_i_xio_gridftp_set_authorization(
    globus_ftp_client_operationattr_t * attr,
    va_list                             ap);

static
globus_result_t
globus_l_xio_gridftp_attr_init(
    void **                             out_attr);

static
globus_result_t
globus_l_xio_gridftp_attr_copy(
    void **                             dst,
    void *                              src);

static
globus_result_t
globus_l_xio_gridftp_attr_destroy(
    void *                              driver_attr);

GlobusXIODefineModule(gridftp) =
{
    "globus_xio_gridftp",
    globus_l_xio_gridftp_activate,
    globus_l_xio_gridftp_deactivate,
    NULL,
    NULL,
    &local_version
};

#define GlobusXIOGridftpErrorAttr(reason)                                   \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GlobusXIOMyModule(gridftp),                                     \
            NULL,                                                    \
            GLOBUS_XIO_GRIDFTP_ERROR_ATTR,                                  \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Attr error: %s", (reason)))

#define GlobusXIOGridftpErrorSeek(reason)                                   \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GlobusXIOMyModule(gridftp),                                     \
            NULL,                                                    \
            GLOBUS_XIO_GRIDFTP_ERROR_SEEK,                                  \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Seek error: %s", (reason)))

#define GlobusXIOGridftpErrorOutstandingRead()                              \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GlobusXIOMyModule(gridftp),                                     \
            NULL,                                                    \
            GLOBUS_XIO_GRIDFTP_ERROR_OUTSTANDING_READ,                      \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Read is outstanding"))

#define GlobusXIOGridftpErrorOutstandingWrite()                             \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GlobusXIOMyModule(gridftp),                                     \
            NULL,                                                    \
            GLOBUS_XIO_GRIDFTP_ERROR_OUTSTANDING_WRITE,                     \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Write is outstanding"))

#define GlobusXIOGridftpErrorPendingRead()                                  \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GlobusXIOMyModule(gridftp),                                     \
            NULL,                                                    \
            GLOBUS_XIO_GRIDFTP_ERROR_PENDING_READ,                          \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Read pending"))

#define GlobusXIOGridftpErrorPendingWrite()                                 \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GlobusXIOMyModule(gridftp),                                     \
            NULL,                                                    \
            GLOBUS_XIO_GRIDFTP_ERROR_PENDING_WRITE,                         \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Write pending"))

#define GlobusXIOGridftpErrorOutstandingPartialXfer()                       \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GlobusXIOMyModule(gridftp),                                     \
            NULL,                                                    \
            GLOBUS_XIO_GRIDFTP_ERROR_OUTSTANDING_PARTIAL_XFER,              \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "A Partial Xfer is outstanding"))


static
int
globus_l_xio_gridftp_activate(void)
{
    int rc;
    GlobusXIOName(globus_l_xio_gridftp_activate);

    GlobusDebugInit(GLOBUS_XIO_GRIDFTP, TRACE);
    GlobusXIOGridftpDebugEnter();
    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto error_xio_system_activate;
    }
    rc = globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto error_ftp_client_activate;
    }
    GlobusXIORegisterDriver(gridftp);
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error_ftp_client_activate:
    globus_module_deactivate(GLOBUS_XIO_MODULE);
error_xio_system_activate:
    GlobusXIOGridftpDebugExitWithError();
    GlobusDebugDestroy(GLOBUS_XIO_GRIDFTP);
    return rc;
}


static
int
globus_l_xio_gridftp_deactivate(void)
{   
    int rc;
    GlobusXIOName(globus_l_xio_gridftp_deactivate);
    
    GlobusXIOGridftpDebugEnter();
    GlobusXIOUnRegisterDriver(gridftp);
    rc = globus_module_deactivate(GLOBUS_FTP_CLIENT_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {   
        goto error_deactivate;
    }
    rc = globus_module_deactivate(GLOBUS_XIO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {   
        goto error_deactivate;
    }
    GlobusXIOGridftpDebugExit();
    GlobusDebugDestroy(GLOBUS_XIO_GRIDFTP);
    return GLOBUS_SUCCESS;

error_deactivate:
    GlobusXIOGridftpDebugExitWithError();
    GlobusDebugDestroy(GLOBUS_XIO_GRIDFTP);
    return rc;
}


static
void
globus_i_xio_gridftp_contact_info_setup(
    globus_xio_contact_t *              dst_contact_info,
    const globus_xio_contact_t *        src_contact_info) 
{
    GlobusXIOName(globus_i_xio_gridftp_contact_info_setup);     

    GlobusXIOGridftpDebugEnter();
    /* 
     * I use globus_xio_contact_info_to_url to construct the URL to
     * pass to the client library. Since the client lib does not take the 
     * subject string in the URL, it is set to NULL
     */
    memset(dst_contact_info, 0, sizeof(globus_xio_contact_t)); 
    dst_contact_info->resource = src_contact_info->resource;
    dst_contact_info->host = src_contact_info->host;
    dst_contact_info->port = src_contact_info->port;
    dst_contact_info->scheme = src_contact_info->scheme;
    dst_contact_info->user = src_contact_info->user;
    dst_contact_info->pass = src_contact_info->pass;
    GlobusXIOGridftpDebugExit();
}


static
globus_result_t
globus_l_xio_gridftp_handle_destroy(
    globus_l_xio_gridftp_handle_t *     handle)
{
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_gridftp_handle_destroy);

    GlobusXIOGridftpDebugEnter();
    result = globus_ftp_client_handle_flush_url_state(
                                handle->ftp_handle, handle->url);
    if (result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_ftp_client_handle_flush_url_state", result);
        goto error;
    }
    if (!handle->attr->ftp_handle)
    {
        result = globus_ftp_client_handle_destroy(handle->ftp_handle);
        if (result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_ftp_client_handle_destroy", result);
            goto error;
        }
        globus_free(handle->ftp_handle);
    }
    result = globus_l_xio_gridftp_attr_destroy(handle->attr);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_gridftp_attr_destroy", result);
        goto error;
    }
    globus_free(handle->url);
    globus_fifo_destroy(&handle->pending_ops_q);
    globus_memory_destroy(&handle->requestor_memory);
    globus_mutex_destroy(&handle->mutex);
    globus_free(handle);
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


/* allocate the memory for and initialize an internal handle */
static 
globus_result_t
globus_l_xio_gridftp_handle_create(
    globus_l_xio_gridftp_handle_t **    out_handle,
    globus_l_xio_gridftp_attr_t *       attr,
    const globus_xio_contact_t *        contact_info)
{
    globus_l_xio_gridftp_handle_t *     handle;
    globus_result_t                     result;
    int                                 node_size;
    int                                 node_count;
    globus_xio_contact_t                contact_info_local;
    GlobusXIOName(globus_l_xio_gridftp_handle_create);

    GlobusXIOGridftpDebugEnter();
    handle = (globus_l_xio_gridftp_handle_t *)
                globus_malloc(sizeof(globus_l_xio_gridftp_handle_t));
    if (handle == NULL)
    {
        result = GlobusXIOErrorMemory("handle");
        goto error_handle;
    }
    if (attr)
    {   
        result = globus_l_xio_gridftp_attr_copy(
                        (void**)&handle->attr, (void*)attr);
    }
    else
    {
        result = globus_l_xio_gridftp_attr_init((void**)&handle->attr);
    }
    if (result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
                        "globus_l_xio_gridftp_attr_copy", result);
        goto error_attr;
    }
    if (handle->attr->ftp_handle == NULL)
    {
        handle->ftp_handle = (globus_ftp_client_handle_t *)
                            globus_malloc(sizeof(globus_ftp_client_handle_t));
        result = globus_ftp_client_handle_init(handle->ftp_handle, NULL);
        if (result != GLOBUS_SUCCESS)
        {
            goto error_ftp_handle_init;
        }
    }
    else
    {
        handle->ftp_handle = handle->attr->ftp_handle;
    }
    globus_i_xio_gridftp_contact_info_setup(&contact_info_local, contact_info);
    result = globus_xio_contact_info_to_url(&contact_info_local, &handle->url);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_url;
    }   
    result = globus_ftp_client_handle_cache_url_state(
                                        handle->ftp_handle, handle->url);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_enable_caching;
    }
    result = globus_fifo_init(&handle->pending_ops_q);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_fifo_init;
    }           
    node_size = sizeof(globus_i_xio_gridftp_requestor_t);       
    node_count = GLOBUS_XIO_GRIDFTP_REQUESTOR_COUNT;      
    globus_memory_init(&handle->requestor_memory, node_size, node_count);
    globus_mutex_init(&handle->mutex, NULL);    
    handle->state = GLOBUS_XIO_GRIDFTP_NONE;
    handle->outstanding_io_count = 0;   
    handle->offset = 0; 
    handle->end_offset = -1; 
    *out_handle = handle;
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error_fifo_init:
    globus_ftp_client_handle_flush_url_state(handle->ftp_handle, handle->url);
error_enable_caching:
    globus_free(handle->url);   
error_url:
    if (!handle->attr->ftp_handle)
    {   
        globus_ftp_client_handle_destroy(handle->ftp_handle);
    }
error_ftp_handle_init:
    globus_l_xio_gridftp_attr_destroy(handle->attr);
error_attr:
    globus_free(handle);
error_handle:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


static
void
globus_l_xio_gridftp_cancel_cb(
    globus_xio_operation_t              op,
    void *                              user_arg,
    globus_xio_error_type_t             reason)
{
    globus_i_xio_gridftp_requestor_t *  requestor;
    globus_l_xio_gridftp_handle_t *     handle;
    globus_xio_operation_t              requestor_op = NULL;
    globus_bool_t                       reading;
    GlobusXIOName(globus_l_xio_gridftp_cancel_cb);

    GlobusXIOGridftpDebugEnter();       
    requestor = (globus_i_xio_gridftp_requestor_t *) user_arg;  
    handle = requestor->handle;
    /* no need to finish read or write here, it is done in read/write cb */
    globus_mutex_lock(&handle->mutex);
    switch (handle->state)
    {
        case GLOBUS_XIO_GRIDFTP_NONE:
            break;
        case GLOBUS_XIO_GRIDFTP_OPENING:
            globus_ftp_client_abort(handle->ftp_handle);
            break;
        case GLOBUS_XIO_GRIDFTP_IO_PENDING:
            handle->state = GLOBUS_XIO_GRIDFTP_ABORT_PENDING;
            globus_i_xio_gridftp_abort_io(handle);
            break;
        case GLOBUS_XIO_GRIDFTP_IO_DONE:
            break;
        case GLOBUS_XIO_GRIDFTP_ABORT_PENDING:
            break;
        case GLOBUS_XIO_GRIDFTP_ABORT_PENDING_IO_PENDING:
            requestor = globus_fifo_remove(&handle->pending_ops_q, requestor);
            if (requestor != NULL)
            {
                requestor_op = requestor->op;
                reading = handle->pending_ops_direction;
                globus_memory_push_node(
                    &handle->requestor_memory, (void*)requestor);
            }
            if (globus_fifo_empty(&handle->pending_ops_q))
            {
                handle->state = GLOBUS_XIO_GRIDFTP_ABORT_PENDING;
            }   
            break;
        default:
            /* if it gets here, something is wrong */
            globus_assert(0 && "Unexpected state in cancel callback");  
    }  
    globus_mutex_unlock(&handle->mutex);
    if (requestor_op)
    {
        if (reading)
        {
            globus_xio_driver_finished_read(requestor_op,  
                GlobusXIOErrorCanceled(), 0);
        }
        else
        {
            globus_xio_driver_finished_write(requestor_op,  
                GlobusXIOErrorCanceled(), 0);
        }
    }
    GlobusXIOGridftpDebugExit();        
}


static
void
globus_l_xio_gridftp_open_cb(
    void *                              user_arg,
    globus_ftp_client_handle_t *        ftp_handle,
    globus_object_t *                   error)
{
    globus_i_xio_gridftp_requestor_t *  requestor;
    globus_l_xio_gridftp_handle_t *     handle;
    globus_xio_operation_t              requestor_op;
    GlobusXIOName(globus_l_xio_gridftp_open_cb);

    GlobusXIOGridftpDebugEnter();
    requestor = (globus_i_xio_gridftp_requestor_t *) user_arg;
    globus_xio_operation_disable_cancel(requestor->op);
    handle = requestor->handle; 
    requestor_op = requestor->op;
    globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
    /* 
     * error code 550 (file not found) is considered success. If the handle is
     * opened for writing, the file may not be available now and will be 
     * created only the transfer is initiated
     */
    if (error != GLOBUS_SUCCESS &&
        globus_error_ftp_error_get_code(error) != 550)
    {
        globus_result_t result;
        result = GlobusXIOErrorWrapFailed("globus_ftp_client_size", 
                                globus_error_put(globus_object_copy(error)));
        globus_l_xio_gridftp_handle_destroy(handle);
        globus_xio_driver_finished_open(NULL, requestor_op, result);
    }
    else
    {
        globus_mutex_lock(&handle->mutex);
	if (handle->attr->append && error == GLOBUS_SUCCESS) 
	{
	    handle->offset = handle->size;
	}
        handle->state = GLOBUS_XIO_GRIDFTP_OPEN;
        globus_mutex_unlock(&handle->mutex);
        globus_xio_driver_finished_open(
                handle, requestor_op, GLOBUS_SUCCESS);
    }           
    GlobusXIOGridftpDebugExit();
}


static
globus_result_t
globus_l_xio_gridftp_open(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_l_xio_gridftp_handle_t *     handle = NULL;
    globus_i_xio_gridftp_requestor_t *  requestor;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_gridftp_open);

    GlobusXIOGridftpDebugEnter();
    globus_assert(driver_link == NULL);
    if (!contact_info->resource || !contact_info->host || 
        !contact_info->scheme)
    {
        result = GlobusXIOErrorParameter("contact_string");
        goto error_contact_info;
    }
    result = globus_l_xio_gridftp_handle_create(
                &handle, 
                (globus_l_xio_gridftp_attr_t *) driver_attr, 
                contact_info);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_handle;
    }
    if (contact_info->subject || contact_info->user || contact_info->pass)
    {
        /* 
         * if user and/or pass present in contact_info, it would have been
         * put into the URL (client lib allows user and pass be to present
         * in the URL but not subject)
         */
        result = globus_ftp_client_operationattr_set_authorization(
            &handle->attr->ftp_operation_attr,
            GSS_C_NO_CREDENTIAL,
            contact_info->user,
            contact_info->pass,
            NULL,                /* acct */
            contact_info->subject);
        if (result != GLOBUS_SUCCESS)
        {
            goto error_auth;
        }
    }
    requestor = (globus_i_xio_gridftp_requestor_t *)
                        globus_memory_pop_node(&handle->requestor_memory);
    requestor->op = op;
    requestor->handle = handle;
    if (globus_xio_operation_enable_cancel(
        op, globus_l_xio_gridftp_cancel_cb, requestor))
    {
        result = GlobusXIOErrorCanceled();
        goto error_cancel_enable;
    }
    globus_mutex_lock(&handle->mutex);  
    if (globus_xio_operation_is_canceled(op))
    {
        result = GlobusXIOErrorCanceled();
        goto error_operation_canceled;
    }   
    result = globus_ftp_client_size(
        handle->ftp_handle, 
        handle->url, 
        &handle->attr->ftp_operation_attr, 
        &handle->size,
        globus_l_xio_gridftp_open_cb, 
        requestor);
    if (result != GLOBUS_SUCCESS)       
    {
        goto error_size;
    }
    handle->state = GLOBUS_XIO_GRIDFTP_OPENING;
    globus_mutex_unlock(&handle->mutex);        
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error_size:
error_operation_canceled:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_operation_disable_cancel(op);
error_cancel_enable:
    /* 
     * XIO calls the cancel_cb with cancel lock held and disable_cancel waits 
     * for that. So cancel_cb can not be active after I call disable_cancel
     * and thus I can safely push the requestor memory and call handle_destroy
     */ 
    globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
error_auth:
    res = globus_l_xio_gridftp_handle_destroy(handle);  
    globus_assert (res == GLOBUS_SUCCESS);      
error_handle:
error_contact_info:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


/* called locked */
static
globus_result_t
globus_l_xio_gridftp_process_pending_ops(
    globus_l_xio_gridftp_handle_t *     handle,
    globus_list_t **                    error_list)
{
    globus_i_xio_gridftp_requestor_t *  requestor;
    globus_result_t                     result;
    globus_i_xio_gridftp_error_info_t * error_info = NULL;
    globus_bool_t                       reading;
    GlobusXIOName(globus_l_xio_gridftp_process_pending_ops);

    GlobusXIOGridftpDebugEnter();
    handle->state = GLOBUS_XIO_GRIDFTP_OPEN;
    globus_assert(!globus_fifo_empty(&handle->pending_ops_q));
    requestor = (globus_i_xio_gridftp_requestor_t*)
                      globus_fifo_peek(&handle->pending_ops_q);
    reading = handle->pending_ops_direction;
    if (reading)
    {
        result = globus_i_xio_gridftp_register_get(requestor);
    }
    else
    {
        result = globus_i_xio_gridftp_register_put(requestor);
    }
    if (result != GLOBUS_SUCCESS)
    {
        do
        {
            requestor = (globus_i_xio_gridftp_requestor_t*)
                          globus_fifo_dequeue(&handle->pending_ops_q);
            error_info = (globus_i_xio_gridftp_error_info_t *)
                    globus_malloc(sizeof(globus_i_xio_gridftp_error_info_t));
            error_info->op = requestor->op;
            /* 
             * As I don't use this across cbs, I just store result rather than 
             * error object
             */
            error_info->result = result;
            globus_list_insert(error_list, error_info);
            globus_memory_push_node(
                            &handle->requestor_memory, (void*)requestor);
        } while (!globus_fifo_empty(&handle->pending_ops_q));
        goto error;
    }
    do
    {
        requestor = (globus_i_xio_gridftp_requestor_t*)
                          globus_fifo_dequeue(&handle->pending_ops_q);
        if (reading)
        {
            result = globus_i_xio_gridftp_register_read(requestor);
        }
        else
        {
            result = globus_i_xio_gridftp_register_write(requestor);
        }
        if (result != GLOBUS_SUCCESS)
        {
            error_info = (globus_i_xio_gridftp_error_info_t *)
                globus_malloc(sizeof(globus_i_xio_gridftp_error_info_t));
            error_info->op = requestor->op;
            /* 
             * As I don't use this across cbs, I just store result rather than 
             * error object
             */
            error_info->result = result;
            globus_list_insert(error_list, error_info);
            globus_memory_push_node(
                            &handle->requestor_memory, (void*)requestor);
        }    
        else
        {
            ++handle->outstanding_io_count;
        }
    } while (!globus_fifo_empty(&handle->pending_ops_q));
    if (handle->outstanding_io_count > 0)
    {   
        handle->state = GLOBUS_XIO_GRIDFTP_IO_PENDING;
    }
    if (!globus_list_empty(*error_list))
    {
        /* 
         * The result that I return from this function is not really used in
         * reporting. I use the error information stored in the error_list
         * for reporting it to the user.
         */
        result = error_info ? error_info->result : GLOBUS_FAILURE;
        goto error;
    }
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


/* called locked */
static
globus_result_t
globus_l_xio_gridftp_change_state(
    globus_l_xio_gridftp_handle_t *     handle,
    globus_bool_t *                     close,
    globus_list_t **                    error_list)
{
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_gridftp_change_state);

    GlobusXIOGridftpDebugEnter();
    *close = GLOBUS_FALSE;
    switch (handle->state)
    {
        case GLOBUS_XIO_GRIDFTP_IO_PENDING:             
            if (handle->outstanding_io_count == 0)
            {
                handle->state = GLOBUS_XIO_GRIDFTP_IO_DONE;
            }
            /* fall through */
        case GLOBUS_XIO_GRIDFTP_IO_DONE:
            if (handle->xfer_done == GLOBUS_TRUE)
            {
                handle->state = GLOBUS_XIO_GRIDFTP_OPEN;
            }       
            break;
        case GLOBUS_XIO_GRIDFTP_ABORT_PENDING:
            if ((handle->outstanding_io_count == 0) && 
                (handle->xfer_done == GLOBUS_TRUE))
            {
                handle->state = GLOBUS_XIO_GRIDFTP_OPEN;
            }
            break;
        case GLOBUS_XIO_GRIDFTP_ABORT_PENDING_IO_PENDING:
        {
            if ((handle->outstanding_io_count == 0) && 
                (handle->xfer_done == GLOBUS_TRUE))
            {
                result = globus_l_xio_gridftp_process_pending_ops(
                                                        handle, error_list);
                if (result != GLOBUS_SUCCESS)
                {
                    goto error;
                }
            }           
            break;
        }
        case GLOBUS_XIO_GRIDFTP_ABORT_PENDING_CLOSING:
            if ((handle->outstanding_io_count == 0) && 
                (handle->xfer_done == GLOBUS_TRUE))
            {
                handle->state = GLOBUS_XIO_GRIDFTP_NONE;
                *close = GLOBUS_TRUE;
            }   
            break;
        default:
            /* if it gets here, something is wrong */
            globus_assert(0 && "Unexpected state in gridftp_change_state");
    }   

    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


static
void
globus_i_xio_gridftp_finish_failed_ops(
    globus_list_t **                    error_list,
    globus_bool_t                       reading)
{
    globus_i_xio_gridftp_error_info_t * error_info;
    GlobusXIOName(globus_i_xio_gridftp_finish_failed_ops);
    GlobusXIOGridftpDebugEnter();
    do 
    {   
        error_info = (globus_i_xio_gridftp_error_info_t *)
                                globus_list_remove(error_list, *error_list);
        if (reading)
        {
            globus_xio_driver_finished_read(
                                error_info->op, error_info->result, 0);
        }
        else
        {    
            globus_xio_driver_finished_write(
                                error_info->op, error_info->result, 0);
        }
        globus_free(error_info);
    } while (!globus_list_empty(*error_list));
    GlobusXIOGridftpDebugExit();
}


static
void
globus_l_xio_gridftp_xfer_cb(
    void *                              user_arg,
    globus_ftp_client_handle_t *        ftp_handle,
    globus_object_t *                   error)
{
    globus_l_xio_gridftp_handle_t *     handle;
    globus_i_xio_gridftp_requestor_t *  requestor;
    globus_bool_t                       close = GLOBUS_FALSE;  
    globus_list_t *                     error_list = NULL;
    globus_xio_operation_t              requestor_op = NULL;
    globus_bool_t                       reading;
    globus_size_t                       len = 0;    
    globus_off_t                        offset;
    globus_result_t                     result;        
    GlobusXIOName(globus_l_xio_gridftp_xfer_cb);

    GlobusXIOGridftpDebugEnter();
    /*
     * Here I need to finish read/write if partial_xfer is enabled or if 
     * ftp_client_register_read/write of pending read/write failed while in 
     * ABORT_PENDING_IO_PENDING state. An error returned by 
     * globus_l_xio_gridftp_change_state() indicates pending read/write failed
     */
    handle = (globus_l_xio_gridftp_handle_t *) user_arg;
    globus_mutex_lock(&handle->mutex);
    handle->xfer_done = GLOBUS_TRUE;
    result = globus_l_xio_gridftp_change_state(handle, &close, &error_list);
    if (result != GLOBUS_SUCCESS)
    {
        reading = handle->pending_ops_direction;
        globus_mutex_unlock(&handle->mutex);
        goto error;
    }
    if (close == GLOBUS_TRUE)
    {
        globus_xio_operation_t close_op;
        /*
         * If close was called in 'ABORT_PENDING' state, a close requestor 
         * would have been enqueued in the pending_ops_q
         */
        requestor = (globus_i_xio_gridftp_requestor_t*)
                        globus_fifo_dequeue(&handle->pending_ops_q);
        close_op = requestor->op;
        globus_memory_push_node(
                    &handle->requestor_memory, (void*)requestor);
        globus_mutex_unlock(&handle->mutex);
        result = globus_l_xio_gridftp_handle_destroy(handle);
        globus_assert(result == GLOBUS_SUCCESS);
        globus_xio_driver_finished_close(close_op, result);
    }           
    else if (handle->attr->partial_xfer && 
             handle->state == GLOBUS_XIO_GRIDFTP_OPEN)
    {
        requestor = handle->partial_requestor;
        requestor_op = requestor->op;
        reading = handle->outstanding_ops_direction;
        offset = requestor->offset;
        len = requestor->length;
        if (error == GLOBUS_SUCCESS)      
        {
            result = globus_xio_driver_data_descriptor_cntl(
                        requestor_op,
                        NULL,
                        GLOBUS_XIO_DD_SET_OFFSET,
                        offset);
        }
        else
        {
            result = GlobusXIOErrorWrapFailed("globus_ftp_client_io", 
                            globus_error_put(globus_object_copy(error)));
        }
        if (result == GLOBUS_SUCCESS && 
                            requestor->saved_error != NULL)
        {
            result = globus_error_put(requestor->saved_error);   
        }
        globus_memory_push_node(
                            &handle->requestor_memory, (void*)requestor);
        globus_mutex_unlock(&handle->mutex);
    }
    if (requestor_op)
    {
        if (reading)
        { 
            globus_xio_driver_finished_read(requestor_op, result, len);
        }
        else
        {
            globus_xio_driver_finished_write(requestor_op, result, len);
        }
    }
    GlobusXIOGridftpDebugExit();
    return;

error:
    globus_i_xio_gridftp_finish_failed_ops(&error_list, reading);
    GlobusXIOGridftpDebugExitWithError();
    return;
}


static
void
globus_l_xio_gridftp_write_cb(
    void *                              user_arg,
    globus_ftp_client_handle_t *        ftp_handle,
    globus_object_t *                   error,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_off_t                        offset,
    globus_bool_t                       eof)
{
    globus_i_xio_gridftp_requestor_t *  requestor;
    globus_l_xio_gridftp_handle_t *     handle;
    globus_xio_operation_t              requestor_op = NULL;
    globus_list_t *                     error_list = NULL;
    globus_off_t                        requestor_offset = 0;
    globus_size_t                       requestor_length = 0;
    globus_result_t                     requestor_result = GLOBUS_SUCCESS;
    globus_result_t                     result;
    globus_bool_t                       finish = GLOBUS_TRUE;
    globus_bool_t                       finish_pending_op = GLOBUS_FALSE;
    globus_bool_t                       close = GLOBUS_FALSE;
    globus_bool_t                       reading;
    GlobusXIOName(globus_l_xio_gridftp_write_cb);

    GlobusXIOGridftpDebugEnter();
    requestor = (globus_i_xio_gridftp_requestor_t *) user_arg;
    handle = requestor->handle;
    globus_mutex_lock(&handle->mutex);
    if (error != GLOBUS_SUCCESS && requestor->saved_error == NULL)
    {
        requestor->saved_error = globus_object_copy(error);
    }
    if (--requestor->finished_count == 0)
    {
        requestor_op = requestor->op;
        globus_mutex_unlock(&handle->mutex); 
        /* 
         * Unlock the mutex here because I can't call disable_cancel with lock
         * held (lock inversion issues)
         */
        globus_xio_operation_disable_cancel(requestor_op);
        globus_mutex_lock(&handle->mutex);
        handle->outstanding_io_count--;
        result = globus_l_xio_gridftp_change_state(
                                        handle, &close, &error_list);
        /* XIO wouldn't call close while there is an outstanding operation */
        globus_assert(close == GLOBUS_FALSE);
        if (result != GLOBUS_SUCCESS)
        {
            finish_pending_op = GLOBUS_TRUE;
            reading = handle->pending_ops_direction;
        }
        /* 
         * The offset returned in the cb is not used because we might register 
         * multiple ftp_client_writes for a single user write
         */
        requestor_offset = requestor->offset;
        requestor_length = requestor->length;
        if (requestor->saved_error != NULL)
        {
            requestor_result = globus_error_put(requestor->saved_error);
        }
        if (handle->attr->partial_xfer)
        {
            if (handle->state == GLOBUS_XIO_GRIDFTP_OPEN)
            {
                globus_memory_push_node(
                                &handle->requestor_memory, (void*)requestor);
            }
            else
            {
                /* 
                 * put is not done yet. Partial transfers (where each 
                 * read/write is associated with a get/put) are not yet 
                 * finished until get/put is also done 
                 */
                finish = GLOBUS_FALSE;
            }
        }
        else
        {
            globus_memory_push_node(
                                &handle->requestor_memory, (void*)requestor);
        }
    }
    globus_mutex_unlock(&handle->mutex);
    if (finish)
    {
        if (requestor_result == GLOBUS_SUCCESS)
        {
            requestor_result = globus_xio_driver_data_descriptor_cntl(
                                    requestor_op,
                                    NULL,
                                    GLOBUS_XIO_DD_SET_OFFSET,
                                    requestor_offset);
        }
        globus_xio_driver_finished_write(
                        requestor_op, requestor_result, requestor_length);
    }
    if (finish_pending_op)
    {
        globus_i_xio_gridftp_finish_failed_ops(&error_list, reading);
    }
    GlobusXIOGridftpDebugExit();
}


static
void
globus_l_xio_gridftp_read_cb(
    void *                              user_arg,
    globus_ftp_client_handle_t *        ftp_handle, 
    globus_object_t *                   error, 
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_off_t                        offset,
    globus_bool_t                       eof)
{
    globus_result_t                     requestor_result = GLOBUS_SUCCESS;
    globus_result_t                     result;
    globus_i_xio_gridftp_requestor_t *  requestor;
    globus_l_xio_gridftp_handle_t *     handle;
    globus_bool_t                       close = GLOBUS_FALSE;
    globus_bool_t                       finish = GLOBUS_TRUE;
    globus_bool_t                       finish_pending_op = GLOBUS_FALSE;
    globus_bool_t                       reading;
    globus_xio_operation_t              requestor_op;
    globus_list_t *                     error_list = NULL;
    GlobusXIOName(globus_l_xio_gridftp_read_cb);

    GlobusXIOGridftpDebugEnter();
    requestor = (globus_i_xio_gridftp_requestor_t *) user_arg;
    handle = requestor->handle; 
    requestor_op = requestor->op;
    globus_xio_operation_disable_cancel(requestor_op);
    globus_mutex_lock(&handle->mutex);
    handle->outstanding_io_count--;
    result = globus_l_xio_gridftp_change_state(handle, &close, &error_list);
    /* XIO wouldn't call close while there is an outstanding operation */
    globus_assert(close == GLOBUS_FALSE);
    if (result != GLOBUS_SUCCESS)
    {
        finish_pending_op = GLOBUS_TRUE;
        reading = handle->pending_ops_direction;
    }
    if (error == GLOBUS_SUCCESS)
    {
        if (offset + length > handle->offset)
        {
            handle->offset = offset + length;
        }
        /* 
         * For the partial reads per buffer, eof returned in this cb 
         * will always be TRUE. I do xio_driver_set_eof_received only when 
         * (eof == TRUE && length < partial_xfer_len. I assume offset 
         * returned in this cb will be same as the one I set in the get/put
         * (thats why I use handle->end_offset - offset to compute the
         * partial_xfer_len)
         */
        if (handle->attr->partial_xfer && eof && 
                    length == handle->end_offset - offset)
        {
            eof = GLOBUS_FALSE;
        }
    }
    else
    {
        requestor_result = GlobusXIOErrorWrapFailed("globus_ftp_client_io",  
                            globus_error_put(globus_object_copy(error)));
    }
    if (handle->attr->partial_xfer)
    {   
        if (handle->state == GLOBUS_XIO_GRIDFTP_OPEN)
        {
            globus_memory_push_node(
                        &handle->requestor_memory, (void*)requestor);
        }
        else
        {
            finish = GLOBUS_FALSE;
            if (eof && requestor_result == GLOBUS_SUCCESS)
            {
                globus_xio_driver_set_eof_received(requestor_op);
                requestor_result = GlobusXIOErrorEOF();
            }
            /* handle->partial_requestor points to this requestor */
            requestor->saved_error = globus_error_get(requestor_result);
            /* read will be finished in xfer_cb, so we need to store offset */
            requestor->offset = offset;
            requestor->length = length;
        }
    }
    else
    {
        globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
    }
    globus_mutex_unlock(&handle->mutex);
    if (finish)
    {
        if (error == GLOBUS_SUCCESS)
        {
            requestor_result = globus_xio_driver_data_descriptor_cntl(
                        requestor_op, NULL, GLOBUS_XIO_DD_SET_OFFSET, offset);
            if (eof && requestor_result == GLOBUS_SUCCESS)
            {
                globus_xio_driver_set_eof_received(requestor_op);
                requestor_result = GlobusXIOErrorEOF();
            }
        }
        globus_xio_driver_finished_read(
                                requestor_op, requestor_result, length);
    }   
    if (finish_pending_op)
    {
        globus_i_xio_gridftp_finish_failed_ops(&error_list, reading);
    }
    GlobusXIOGridftpDebugExit();
    return;
}


static
void
globus_l_xio_gridftp_write_eof_cb(
    void *                              user_arg,
    globus_ftp_client_handle_t *        ftp_handle,
    globus_object_t *                   error,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_off_t                        offset,
    globus_bool_t                       eof)
{
    GlobusXIOName(globus_l_xio_gridftp_write_eof_cb);
    GlobusXIOGridftpDebugEnter();       
    GlobusXIOGridftpDebugExit();        
}


/* called locked */
static
globus_result_t
globus_i_xio_gridftp_register_get(
    globus_i_xio_gridftp_requestor_t *  requestor)
{
    globus_l_xio_gridftp_handle_t *     handle;
    globus_result_t                     result;
    GlobusXIOName(globus_i_xio_gridftp_register_get);

    GlobusXIOGridftpDebugEnter();
    handle = requestor->handle;
    handle->outstanding_ops_direction = GLOBUS_TRUE;  
    if (handle->attr->partial_xfer)
    {
        handle->partial_requestor = requestor;
        /* 
         * for reads, user cant specify an offset via dd, so there is no need
         * for requestor->offset here
         */
        handle->end_offset = handle->offset + requestor->iovec[0].iov_len;
    }
    /* if partial xfer is not enabled, handle->end_offset will be -1(eof) */
    handle->xfer_done = GLOBUS_FALSE;
    /* 
     * handle is passed as user_arg to get/put and not requestor because
     * requestor is associated with individual reads/writes; whereas a get/put
     * might span multiple reads/writes
     */
    if (handle->offset > 0 || handle->attr->partial_xfer)
    {
        result = globus_ftp_client_partial_get(
            handle->ftp_handle,
            handle->url,
            &handle->attr->ftp_operation_attr,
            NULL,        /* restart marker */
            handle->offset,
            handle->end_offset,
            globus_l_xio_gridftp_xfer_cb,
            handle);
    }
    else
    {
	if (handle->attr->eret_alg_str)
	{
	    result = globus_ftp_client_extended_get(
		handle->ftp_handle,
		handle->url,
		&handle->attr->ftp_operation_attr,
		NULL,        /* restart_marker */
		(const char *) handle->attr->eret_alg_str,
		globus_l_xio_gridftp_xfer_cb,
		handle);
	}
	else
	{
	    result = globus_ftp_client_get(
		handle->ftp_handle,
		handle->url,
		&handle->attr->ftp_operation_attr,
		NULL,        /* restart_marker */
		globus_l_xio_gridftp_xfer_cb,
		handle);
	}
    }   
    if (result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


/* called locked */
static
globus_result_t
globus_i_xio_gridftp_register_read(
    globus_i_xio_gridftp_requestor_t *  requestor)
{
    globus_l_xio_gridftp_handle_t *     handle;
    globus_result_t                     result;
    GlobusXIOName(globus_i_xio_gridftp_register_read);

    GlobusXIOGridftpDebugEnter();
    handle = requestor->handle;
    /* simultaneous read and write not allowed */       
    if (handle->outstanding_ops_direction == GLOBUS_FALSE)
    {
        result = GlobusXIOGridftpErrorOutstandingWrite();
        goto error;
    }           
    result = globus_ftp_client_register_read(
        handle->ftp_handle,
        requestor->iovec[0].iov_base,
        requestor->iovec[0].iov_len,
        globus_l_xio_gridftp_read_cb,
        requestor);
    if (result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_gridftp_read(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t*           iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_xio_gridftp_handle_t *     handle;
    globus_i_xio_gridftp_requestor_t *  requestor;
    globus_result_t                     result;
    globus_size_t                       wait_for;
    GlobusXIOName(globus_l_xio_gridftp_read);

    GlobusXIOGridftpDebugEnter();
    wait_for = globus_xio_operation_get_wait_for(op);
    if (wait_for != 1)
    {
        result = GlobusXIOErrorParameter("Waitforbytes");
        goto error_wait_for;
    }
    handle = (globus_l_xio_gridftp_handle_t *) driver_specific_handle;
    requestor = (globus_i_xio_gridftp_requestor_t *)
                globus_memory_pop_node(&handle->requestor_memory);
    requestor->op = op;
    requestor->handle = handle;
    requestor->iovec = (globus_xio_iovec_t*)iovec;
    if (globus_xio_operation_enable_cancel(
        op, globus_l_xio_gridftp_cancel_cb, requestor))
    {
        result = GlobusXIOErrorCanceled();
        goto error_cancel_enable;
    }
    /* 
     * I can't call enable_cancel with lock being held because of lock
     * inversion issues 
     */
    globus_mutex_lock(&handle->mutex);
    if (globus_xio_operation_is_canceled(op))
    {
        result = GlobusXIOErrorCanceled();
        goto error_operation_canceled;
    }   
    if (handle->attr->partial_xfer && handle->state != GLOBUS_XIO_GRIDFTP_OPEN)
    {
        result = GlobusXIOGridftpErrorOutstandingPartialXfer();
        goto error_outstanding_partial_xfer;
    }
    if (globus_xio_driver_eof_received(op))
    {
        result = GlobusXIOErrorEOF();
	globus_mutex_unlock(&handle->mutex);
	globus_xio_operation_disable_cancel(op);
	globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
	globus_xio_driver_finished_read(op, result, 0);  
    }
    else
    {
	switch (handle->state)
	{
	    case GLOBUS_XIO_GRIDFTP_OPEN:           
		result = globus_i_xio_gridftp_register_get(requestor);
		if (result != GLOBUS_SUCCESS)
		{   
		    goto error_get;
		}   
		/* fall through */  
	    case GLOBUS_XIO_GRIDFTP_IO_DONE:
		/* fall through */
	    case GLOBUS_XIO_GRIDFTP_IO_PENDING:
		result = globus_i_xio_gridftp_register_read(requestor);
		if (result != GLOBUS_SUCCESS)
		{
		    goto error_register_read;
		}
		++handle->outstanding_io_count;                     
		handle->state = GLOBUS_XIO_GRIDFTP_IO_PENDING;
		break;
	    case GLOBUS_XIO_GRIDFTP_ABORT_PENDING:
		handle->pending_ops_direction = GLOBUS_TRUE;        
		handle->state = GLOBUS_XIO_GRIDFTP_ABORT_PENDING_IO_PENDING;
		/* fall through */  
	    case GLOBUS_XIO_GRIDFTP_ABORT_PENDING_IO_PENDING:
	    {
		/* simultaneous read and write not allowed */       
		if (handle->pending_ops_direction == GLOBUS_FALSE)
		{
		    result = GlobusXIOGridftpErrorPendingWrite();
		    goto error_pending_write;
		}           
		globus_fifo_enqueue(&handle->pending_ops_q, requestor);
		break;
	    }
	    default:
		/* if it gets here, something is wrong */
		globus_assert(0 && "Unexpected state in read");
	}
	globus_mutex_unlock(&handle->mutex);
    }
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error_pending_write:
error_register_read:
error_get:
error_outstanding_partial_xfer:
error_operation_canceled:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_operation_disable_cancel(op);
error_cancel_enable:
    globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
error_wait_for:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


/* called locked */
static
globus_result_t
globus_i_xio_gridftp_register_put(
    globus_i_xio_gridftp_requestor_t *  requestor)
{
    globus_l_xio_gridftp_handle_t *     handle;
    globus_result_t                     result;
    GlobusXIOName(globus_i_xio_gridftp_register_put);

    GlobusXIOGridftpDebugEnter();
    handle = requestor->handle;
    handle->outstanding_ops_direction = GLOBUS_FALSE;
    if (handle->attr->partial_xfer)
    {
        handle->partial_requestor = requestor;
        handle->end_offset = requestor->offset + requestor->length;
    }
    /* if partial xfer is not enabled, handle->end_offset will be -1(eof) */
    handle->xfer_done = GLOBUS_FALSE;
    /* 
     * handle is passed as user_arg to get/put and not requestor because
     * requestor is associated with individual reads/writes; whereas a get/put
     * might span multiple reads/writes
     */
    if (requestor->offset > 0 || handle->attr->partial_xfer)
    {
        result = globus_ftp_client_partial_put(
            handle->ftp_handle,
            handle->url,
            &handle->attr->ftp_operation_attr,
            NULL,
            requestor->offset,
            handle->end_offset,
            globus_l_xio_gridftp_xfer_cb,
            handle);
    }
    else
    {
	if (handle->attr->esto_alg_str)
	{
	    result = globus_ftp_client_extended_put(
		handle->ftp_handle,
		handle->url,
		&handle->attr->ftp_operation_attr,
		NULL,
		(const char *) handle->attr->esto_alg_str,
		globus_l_xio_gridftp_xfer_cb,
		handle);
	}
	else
	{
	    result = globus_ftp_client_put(
		handle->ftp_handle,
		handle->url,
		&handle->attr->ftp_operation_attr,
		NULL,
		globus_l_xio_gridftp_xfer_cb,
		handle);
	}
    }
    if (result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


/* called locked */
static
globus_result_t
globus_i_xio_gridftp_register_write(
    globus_i_xio_gridftp_requestor_t *  requestor)
{
    globus_l_xio_gridftp_handle_t *     handle;
    globus_xio_iovec_t *                iovec;
    globus_result_t                     result;
    globus_bool_t                       eof = GLOBUS_FALSE;
    globus_off_t                        offset;
    int                                 i;
    GlobusXIOName(globus_i_xio_gridftp_register_write);

    GlobusXIOGridftpDebugEnter();
    handle = requestor->handle;
    /* simultaneous read and write not allowed */       
    if (handle->outstanding_ops_direction == GLOBUS_TRUE)
    {
        result = GlobusXIOGridftpErrorOutstandingRead();
        goto error;
    }
    /* This offset is either handle->offset or specified by user via dd */
    offset = requestor->offset;
    iovec = requestor->iovec;
    if (handle->attr->partial_xfer)
    {
        eof = GLOBUS_TRUE;
    }
    for (i = 0; i < requestor->iovec_count; i++)
    {    
        result = globus_ftp_client_register_write(
            handle->ftp_handle,     
            iovec[i].iov_base,
            iovec[i].iov_len,
            offset,
            eof, 
            globus_l_xio_gridftp_write_cb,
            requestor);
        if (result != GLOBUS_SUCCESS)
        {
            if (requestor->finished_count == 0)
            {
                goto error;
            }
            else if (requestor->saved_error == NULL)
            {
                requestor->saved_error = globus_error_get(result);
            }
        }
        ++requestor->finished_count;
        offset = offset + iovec[i].iov_len;
    }
    /* 
     * I do not modify handle->offset if globus_ftp_client_register_write
     * fails because the failure indicates that something is wrong anyways
     */
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_gridftp_write(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_xio_gridftp_handle_t *     handle;
    globus_i_xio_gridftp_requestor_t *  requestor;
    globus_result_t                     result;
    globus_off_t                        offset;
    int					i;
    GlobusXIOName(globus_l_xio_gridftp_write);

    GlobusXIOGridftpDebugEnter();
    handle = (globus_l_xio_gridftp_handle_t *) driver_specific_handle;
    requestor = (globus_i_xio_gridftp_requestor_t *)
                globus_memory_pop_node(&handle->requestor_memory);
    requestor->op = op;
    requestor->handle = handle;
    requestor->iovec = (globus_xio_iovec_t*)iovec;
    if (globus_xio_operation_enable_cancel(
        op, globus_l_xio_gridftp_cancel_cb, requestor))
    {
        result = GlobusXIOErrorCanceled();
        goto error_cancel_enable;
    }
    /* 
     * I can't call enable_cancel with lock being held because of lock
     * inversion issues 
     */
    globus_mutex_lock(&handle->mutex);
    if (globus_xio_operation_is_canceled(op))
    {
        result = GlobusXIOErrorCanceled();
        goto error_operation_canceled;
    }   
    if (handle->attr->partial_xfer && 
        handle->state != GLOBUS_XIO_GRIDFTP_OPEN)
    {
        result = GlobusXIOGridftpErrorOutstandingPartialXfer();
        goto error_outstanding_partial_xfer;
    }
    result = globus_xio_driver_data_descriptor_cntl(
                op,
                NULL,
                GLOBUS_XIO_DD_GET_OFFSET,
                &offset);
    /* 
     * If offset is not specified, dd_cntl will return offset = -1.
     * In that case offset is set to handle->offset (whose initial value
     * is zero). Basically, the file will be overwritten from the start
     * if offset is not specified.
     */ 
    if (result != GLOBUS_SUCCESS || offset == -1)
    {
        offset = handle->offset;
    }
    requestor->offset = offset;
    GlobusXIOUtilIovTotalLength(requestor->length, iovec, iovec_count);
    requestor->finished_count = 0;
    requestor->iovec_count = iovec_count;
    requestor->saved_error = NULL;
    for (i = 0; i < iovec_count; i++)
    {
	offset += iovec[i].iov_len;
    }
    /*
     * The updated offset might be less than handle->offset if user pass an
     * offset (via dd) that is less than handle->offset - length of buffer.
     * I update the handle->offset here so that it gets updated for the 
     * requests that we put in the pending q as well.
     */
    if (offset > handle->offset)
    {
        handle->offset = offset;
    }
    switch (handle->state)
    {   
        case GLOBUS_XIO_GRIDFTP_OPEN:
            result = globus_i_xio_gridftp_register_put(requestor);
            if (result != GLOBUS_SUCCESS)
            {   
                goto error_put;
            }
            /* fall through */  
        case GLOBUS_XIO_GRIDFTP_IO_DONE:
            /* fall through */
        case GLOBUS_XIO_GRIDFTP_IO_PENDING:
        {
            result = globus_i_xio_gridftp_register_write(requestor);
            if (result != GLOBUS_SUCCESS)
            {
                goto error_register_write;
            }
            ++handle->outstanding_io_count;                     
            handle->state = GLOBUS_XIO_GRIDFTP_IO_PENDING;
            break;
        }
        case GLOBUS_XIO_GRIDFTP_ABORT_PENDING:
            handle->pending_ops_direction = GLOBUS_FALSE;
            handle->state = GLOBUS_XIO_GRIDFTP_ABORT_PENDING_IO_PENDING;
            /* fall through */  
        case GLOBUS_XIO_GRIDFTP_ABORT_PENDING_IO_PENDING:
        {
            /* simultaneous read and write not allowed */       
            if (handle->pending_ops_direction == GLOBUS_TRUE)
            {
                result = GlobusXIOGridftpErrorPendingRead();
                goto error_pending_read;
            }
            globus_fifo_enqueue(&handle->pending_ops_q, requestor);
            break;
        }
        default:
            /* if it gets here, something is wrong */
            globus_assert(0 && "Unexpected state in write");
    }
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error_pending_read:
error_register_write:
error_put:
error_outstanding_partial_xfer:
error_operation_canceled:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_operation_disable_cancel(op);
error_cancel_enable:
    globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


static
void
globus_i_xio_gridftp_abort_io(
    globus_l_xio_gridftp_handle_t *     handle)
{
    globus_byte_t                       buffer;
    GlobusXIOName(globus_l_xio_gridftp_close);

    GlobusXIOGridftpDebugEnter();
    if (handle->outstanding_ops_direction == GLOBUS_TRUE)
    {
        globus_ftp_client_abort(handle->ftp_handle);
    }
    else
    {
        globus_ftp_client_register_write(
            handle->ftp_handle,
            &buffer,
            0,
            handle->offset,
            GLOBUS_TRUE, /* eof */
            globus_l_xio_gridftp_write_eof_cb,
            NULL);
    }
    GlobusXIOGridftpDebugExit();
}


static
globus_result_t
globus_l_xio_gridftp_close(
    void *                              driver_specific_handle,
    void *                              attr,
    globus_xio_operation_t              op)
{
    globus_l_xio_gridftp_handle_t *     handle;
    globus_result_t                     result;
    globus_bool_t                       finish = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_gridftp_close);

    GlobusXIOGridftpDebugEnter();
    handle = (globus_l_xio_gridftp_handle_t *) driver_specific_handle;
    /* 
     * canceling close is not supported. All that is done here abort any 
     * outstanding IO and there is no way to cancel an abort called on 
     * ftp_clinet_lib
     */
    globus_mutex_lock(&handle->mutex);
    switch (handle->state)
    {
        case GLOBUS_XIO_GRIDFTP_OPEN:
            handle->state = GLOBUS_XIO_GRIDFTP_NONE;
            finish = GLOBUS_TRUE;
            break;
        case GLOBUS_XIO_GRIDFTP_IO_DONE:
            globus_i_xio_gridftp_abort_io(handle);
            /* fall through */  
        case GLOBUS_XIO_GRIDFTP_ABORT_PENDING:
        {
            globus_i_xio_gridftp_requestor_t * requestor;
            requestor = (globus_i_xio_gridftp_requestor_t *)
                        globus_memory_pop_node(&handle->requestor_memory);
            requestor->op = op;
            /* 
             * Here, as a special case, close requestor is put in pending_ops_q
             * (pending_ops_q usually contain only read/write requestors). 
             * After transfer is aborted, close will be finished (in xfer_cb)
             */
            globus_fifo_enqueue(&handle->pending_ops_q, requestor);
            handle->state = GLOBUS_XIO_GRIDFTP_ABORT_PENDING_CLOSING;   
            break;
        }
        default:
            /* 
             * I should get close only when I'm in one of the above states
             * otherwise something is wrong (xio wouldn't give me a close
             * when there is an user operation pending)
             */
            globus_assert(0 && "Unexpected state in close");
    }
    globus_mutex_unlock(&handle->mutex);
    if (finish == GLOBUS_TRUE)
    {   
        result = globus_l_xio_gridftp_handle_destroy(handle);
        globus_assert(result == GLOBUS_SUCCESS);
        globus_xio_driver_finished_close(op, GLOBUS_SUCCESS);
    }
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;      
}


static
globus_result_t
globus_l_xio_gridftp_cntl(
    void  *                             driver_specific_handle,
    int                                 cmd,
    va_list                             ap)
{
    globus_l_xio_gridftp_handle_t *     handle;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_gridftp_cntl);

    GlobusXIOGridftpDebugEnter();
    handle = (globus_l_xio_gridftp_handle_t *) driver_specific_handle;
    globus_mutex_lock(&handle->mutex);      
    switch(cmd)
    {
        case GLOBUS_XIO_GRIDFTP_SEEK:
        {
            /* seek is always from the start of the file */
            globus_off_t seek_offset;   
	    if (handle->attr->append)
	    {
		result = GlobusXIOGridftpErrorSeek(
					"file opened in append mode"); 
		goto error; 
	    }
            seek_offset = va_arg(ap, globus_off_t);
            if (handle->offset != seek_offset)
            {
                switch (handle->state)
                {
                    case GLOBUS_XIO_GRIDFTP_IO_DONE:
                        if (handle->attr->partial_xfer)
                        {
                            result = GlobusXIOGridftpErrorSeek(
						"operation is outstanding"); 
                            goto error; 
                        }
                        globus_i_xio_gridftp_abort_io(handle);
                        handle->state = GLOBUS_XIO_GRIDFTP_ABORT_PENDING;
                        /* fall through */      
                    case GLOBUS_XIO_GRIDFTP_OPEN:
                        /* fall through */      
                    case GLOBUS_XIO_GRIDFTP_ABORT_PENDING:
                        /* fall through */      
                    case GLOBUS_XIO_GRIDFTP_ABORT_PENDING_IO_PENDING:
                        handle->offset = seek_offset;
                        break;
                    default:
                        /* seek not allowed in state's other than above */
                        result = GlobusXIOGridftpErrorSeek(
				"operation is outstanding / invalid state"); 
                        goto error; 
                }
            }   
            break;
        }
        default:
            result = GlobusXIOErrorInvalidCommand(cmd);
            goto error;     
    }   
    globus_mutex_unlock(&handle->mutex);    
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error:
    globus_mutex_unlock(&handle->mutex);    
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_gridftp_attr_init(
    void **                             out_attr)
{
    globus_l_xio_gridftp_attr_t *       attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_gridftp_attr_init);

    GlobusXIOGridftpDebugEnter();
    /*
     *  create a gridftp attr structure and initialize its values
     */
    attr = (globus_l_xio_gridftp_attr_t *) 
                globus_malloc(sizeof(globus_l_xio_gridftp_attr_t));
    if(!attr)
    {
        result = GlobusXIOErrorMemory("attr");
        goto error;
    }
    memcpy(attr, &globus_l_xio_gridftp_attr_default, 
                sizeof(globus_l_xio_gridftp_attr_t));
    result = globus_ftp_client_operationattr_init(
        &attr->ftp_operation_attr);
    if (result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_ftp_client_operationattr_init", result);
        goto error;
    }
    *out_attr = attr;
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


static
globus_result_t
globus_i_xio_gridftp_set_authorization(
    globus_ftp_client_operationattr_t * attr,
    va_list                             ap)
{       
    globus_result_t                     result; 
    gss_cred_id_t			credential;
    char *                              user;
    char *                              password;
    char *                              account;
    char *                              subject;
    GlobusXIOName(globus_i_xio_gridftp_set_authorization);

    GlobusXIOGridftpDebugEnter();
    credential = va_arg(ap, gss_cred_id_t);
    user = va_arg(ap, char *);
    password = va_arg(ap, char *);
    account = va_arg(ap, char *);
    subject = va_arg(ap, char *);
    result = globus_ftp_client_operationattr_set_authorization(
        attr,
	credential,
        user,
        password,
        account, 
        subject);
    GlobusXIOGridftpDebugExit();
    return result;
}


static
globus_result_t
globus_i_xio_gridftp_set_parallelism(
    globus_ftp_client_operationattr_t * attr,
    int                                 num_streams)
{
    globus_result_t                     result;
    globus_ftp_control_parallelism_t    parallelism;
    GlobusXIOName(globus_i_xio_gridftp_set_parallelism);

    GlobusXIOGridftpDebugEnter();       
    /*
     * typedef enum globus_ftp_control_parallelism_mode_e
     * {
     *     GLOBUS_FTP_CONTROL_PARALLELISM_NONE,
     *     GLOBUS_FTP_CONTROL_PARALLELISM_FIXED
     * } globus_ftp_control_parallelism_mode_t;
     *
     * typedef struct globus_i_ftp_parallelism_base_s
     * {
     *     globus_ftp_control_parallelism_mode_t       mode;
     *     globus_size_t                               size;
     * } globus_i_ftp_parallelism_base_t;
     *
     * typedef struct globus_ftp_parallelism_fixed_s
     * {
     *     globus_ftp_control_parallelism_mode_t       mode;
     *     globus_size_t                               size;
     * } globus_ftp_parallelism_fixed_t;
     *
     * typedef union globus_ftp_control_parallelism_u
     * {
     *     globus_ftp_control_parallelism_mode_t    mode;
     *     globus_i_ftp_parallelism_base_t          base;
     *     globus_ftp_parallelism_fixed_t           fixed;
     * } globus_ftp_control_parallelism_t; 
     */
    result = globus_ftp_client_operationattr_set_mode(
        attr,
        GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK);
    if (result != GLOBUS_SUCCESS)
    {
        goto error;
    }           
    parallelism.mode = GLOBUS_FTP_CONTROL_PARALLELISM_FIXED;
    parallelism.fixed.size = num_streams;
    result = globus_ftp_client_operationattr_set_parallelism(
        attr,
        &parallelism);
    if (result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error:  
    GlobusXIOGridftpDebugExitWithError();
    return result;
}       


static
globus_result_t
globus_l_xio_gridftp_attr_cntl(
    void *                              driver_attr,
    int                                 cmd,
    va_list                             ap)
{
    globus_result_t                     result;
    globus_l_xio_gridftp_attr_t *       attr;
    GlobusXIOName(globus_l_xio_gridftp_attr_cntl);

    GlobusXIOGridftpDebugEnter();
    attr = (globus_l_xio_gridftp_attr_t *) driver_attr;
    switch(cmd)
    {
        case GLOBUS_XIO_GRIDFTP_SET_HANDLE:
            attr->ftp_handle = va_arg(ap, globus_ftp_client_handle_t*);
            break;
        case GLOBUS_XIO_GRIDFTP_GET_HANDLE:
        {
            globus_ftp_client_handle_t ** ftp_handle_out;
            ftp_handle_out = va_arg(ap, globus_ftp_client_handle_t**);
            *ftp_handle_out = attr->ftp_handle;
            break;
        }
	case GLOBUS_XIO_GRIDFTP_SET_APPEND:
	    attr->append = va_arg(ap, globus_bool_t);
	    break;
	case GLOBUS_XIO_GRIDFTP_GET_APPEND:
	{
	    globus_bool_t * append_out = va_arg(ap, globus_bool_t*);
	    *append_out = attr->append;
	    break;
	}
	case GLOBUS_XIO_GRIDFTP_SET_ERET:
	    if (attr->partial_xfer)
	    {
		result = GlobusXIOGridftpErrorAttr(
			    "Partial transfers can not be used with ERET/ESTO");
		goto error;
	    }
            free(attr->eret_alg_str);
	    attr->eret_alg_str = strdup(va_arg(ap, char*));
	    break;
	case GLOBUS_XIO_GRIDFTP_GET_ERET:
	{
	    char ** eret_alg_str_out = va_arg(ap, char **);
	    *eret_alg_str_out = strdup(attr->eret_alg_str);
	    break;
	}    
	case GLOBUS_XIO_GRIDFTP_SET_ESTO:
	    if (attr->partial_xfer)
	    {
		result = GlobusXIOGridftpErrorAttr(
			    "Partial transfers can not be used with ERET/ESTO");
		goto error;
	    }
            free(attr->esto_alg_str);
	    attr->esto_alg_str = strdup(va_arg(ap, char*));
	    break;
	case GLOBUS_XIO_GRIDFTP_GET_ESTO:
	{
	    char ** esto_alg_str_out = va_arg(ap, char **);
	    *esto_alg_str_out = strdup(attr->esto_alg_str);
	    break;
	}
        /* Each read/write maps to a single partial xfer */ 
        case GLOBUS_XIO_GRIDFTP_SET_PARTIAL_TRANSFER:
        {
            globus_bool_t partial_xfer = va_arg(ap, globus_bool_t);
	    if (attr->eret_alg_str || attr->esto_alg_str)
	    {
		result = GlobusXIOGridftpErrorAttr(
			    "Partial transfers can not be used with ERET/ESTO");
		goto error;
	    }
            attr->partial_xfer = partial_xfer;
            result = globus_ftp_client_operationattr_set_read_all(
                        &attr->ftp_operation_attr, 
                        partial_xfer, 
                        NULL,
                        NULL);
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_set_read_all", result);
                goto error;     
            }
            break;
        }
        case GLOBUS_XIO_GRIDFTP_GET_PARTIAL_TRANSFER:
        {
            globus_bool_t * partial_xfer_out;
            partial_xfer_out = va_arg(ap, globus_bool_t*);  
            *partial_xfer_out = attr->partial_xfer;
            break;
        }    
        case GLOBUS_XIO_GRIDFTP_SET_NUM_STREAMS:
        {
            int num_streams;
            num_streams = va_arg(ap, int);
            result = globus_i_xio_gridftp_set_parallelism(
                &attr->ftp_operation_attr, num_streams);
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_i_xio_gridftp_set_parallelism", result);
                goto error;     
            }
            break;
        }
        case GLOBUS_XIO_GRIDFTP_GET_NUM_STREAMS:
        {
            globus_ftp_control_parallelism_t parallelism;
            int * num_streams_out = va_arg(ap, int*);
            result = globus_ftp_client_operationattr_get_parallelism(
                &attr->ftp_operation_attr,
                &parallelism);
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_get_parallelism", result);
                goto error;     
            }
            *num_streams_out = parallelism.fixed.size;
            break;
        }
        case GLOBUS_XIO_GRIDFTP_SET_TCP_BUFFER:
        {
            /*
             * typedef union globus_ftp_control_tcpbuffer_t
             * {
             *    globus_ftp_control_tcpbuffer_mode_t         mode;
             *    globus_ftp_control_tcpbuffer_default_t      default_tcpbuf;
             *    globus_ftp_control_tcpbuffer_fixed_t        fixed;
             *    globus_ftp_control_tcpbuffer_automatic_t    automatic;
             * } globus_ftp_control_tcpbuffer_t;
             */
            globus_ftp_control_tcpbuffer_t tcp_buffer;
            tcp_buffer.mode = GLOBUS_FTP_CONTROL_TCPBUFFER_FIXED;
            tcp_buffer.fixed.size = va_arg(ap, int);
            result = globus_ftp_client_operationattr_set_tcp_buffer(
                &attr->ftp_operation_attr,
                &tcp_buffer);
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_set_tcp_buffer", result);
                goto error;     
            }
            break;
        }
        case GLOBUS_XIO_GRIDFTP_GET_TCP_BUFFER:
        {
            globus_ftp_control_tcpbuffer_t tcp_buffer;
            int * buf_size_out = va_arg(ap, int*);
            result = globus_ftp_client_operationattr_get_tcp_buffer(
                &attr->ftp_operation_attr,
                &tcp_buffer);
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_get_tcp_buffer", result);
                goto error;     
            }
            *buf_size_out = tcp_buffer.fixed.size;
            break;
        }
        /* 
         * I force all the xfers to be in type I (binary). So I don't support
         * GLOBUS_XIO_GRIDFTP_SET/GET_TYPE 
         */
        case GLOBUS_XIO_GRIDFTP_SET_MODE:
            result = globus_ftp_client_operationattr_set_mode(
                &attr->ftp_operation_attr,
                va_arg(ap, int));
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_set_mode", result);
                goto error;     
            }
            break;
        case GLOBUS_XIO_GRIDFTP_GET_MODE:
        {
            globus_ftp_control_mode_t ftp_mode;
            int * mode_out;
            mode_out = va_arg(ap, int*);
            result = globus_ftp_client_operationattr_get_mode(
                &attr->ftp_operation_attr,
                &ftp_mode);
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_get_mode", result);
                goto error;     
            }
            *mode_out = ftp_mode;
            break;
        }
        case GLOBUS_XIO_GRIDFTP_SET_AUTH:
            result = globus_i_xio_gridftp_set_authorization(
                &attr->ftp_operation_attr, 
                ap);
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_i_xio_gridftp_set_authorization", result);
                goto error;     
            }
            break;
        case GLOBUS_XIO_GRIDFTP_GET_AUTH:
        {
            gss_cred_id_t * credential = va_arg(ap, gss_cred_id_t*);
            char ** user_out = va_arg(ap, char**);
            char ** password_out = va_arg(ap, char**);
            char ** account_out = va_arg(ap, char**);
            char ** subject_out = va_arg(ap, char**);
	    
            result = globus_ftp_client_operationattr_get_authorization(
                &attr->ftp_operation_attr,
                credential,
                user_out,
                password_out,
                account_out,
                subject_out);
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_get_authorization", 
                    result);
                goto error;     
            }
            break;
        }
        case GLOBUS_XIO_GRIDFTP_SET_DCAU:
        {
            globus_ftp_control_dcau_t dcau;
            dcau.mode = va_arg(ap, int);        
            dcau.subject.subject = va_arg(ap, char*);
            result = globus_ftp_client_operationattr_set_dcau(
                &attr->ftp_operation_attr,
                &dcau);
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_set_dcau", result);
                goto error;     
            }
            break;
        }
        case GLOBUS_XIO_GRIDFTP_GET_DCAU:
        {
            globus_ftp_control_dcau_t dcau;
            int * dcau_mode_out = va_arg(ap, int*);
            char ** dcau_subject_out = va_arg(ap, char**);
            result = globus_ftp_client_operationattr_get_dcau(
                &attr->ftp_operation_attr,
                &dcau);
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_get_dcau", result);
                goto error;     
            }
            *dcau_mode_out = dcau.mode;
            *dcau_subject_out = dcau.subject.subject;
            break;
        }
        case GLOBUS_XIO_GRIDFTP_SET_DATA_PROTECTION:
            result = globus_ftp_client_operationattr_set_data_protection(
                &attr->ftp_operation_attr,
                va_arg(ap, int));
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_set_data_protection", 
                    result);
                goto error;     
            }
            break;
        case GLOBUS_XIO_GRIDFTP_GET_DATA_PROTECTION:
        {
            globus_ftp_control_protection_t ftp_protection;
            int * protection_out = va_arg(ap, int*);
            result = globus_ftp_client_operationattr_get_data_protection(
                &attr->ftp_operation_attr,
                &ftp_protection);
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_get_data_protection", 
                    result);
                goto error;     
            }
            *protection_out = ftp_protection;
            break;
        }
        case GLOBUS_XIO_GRIDFTP_SET_CONTROL_PROTECTION:
            result = globus_ftp_client_operationattr_set_control_protection(
                &attr->ftp_operation_attr,
                va_arg(ap, int));
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_set_control_protection", 
                    result);
                goto error;     
            }
            break;
        case GLOBUS_XIO_GRIDFTP_GET_CONTROL_PROTECTION:
        {
            globus_ftp_control_protection_t ftp_protection;
            int * protection_out = va_arg(ap, int*);
            result = globus_ftp_client_operationattr_get_control_protection(
                &attr->ftp_operation_attr,
                &ftp_protection);
            if (result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_ftp_client_operationattr_get_control_protection", 
                    result);
                goto error;     
            }
            *protection_out = ftp_protection;
            break;
        }
        default:
           result = GlobusXIOErrorInvalidCommand(cmd);
           goto error;
    }   
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error:  
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_gridftp_attr_copy(
    void **                             dst,
    void *                              src)
{
    globus_l_xio_gridftp_attr_t *       src_attr;
    globus_l_xio_gridftp_attr_t *       dst_attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_gridftp_attr_copy);

    GlobusXIOGridftpDebugEnter();
    dst_attr = (globus_l_xio_gridftp_attr_t *) 
                globus_malloc(sizeof(globus_l_xio_gridftp_attr_t));
    if(!dst_attr)
    {
        result = GlobusXIOErrorMemory("attr");
        goto error_dst_attr;
    }
    src_attr = (globus_l_xio_gridftp_attr_t *) src;
    memcpy(dst_attr, src_attr, sizeof(globus_l_xio_gridftp_attr_t)); 

    result = globus_ftp_client_operationattr_copy(
        &dst_attr->ftp_operation_attr, &src_attr->ftp_operation_attr);
    if (result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_ftp_client_operationattr_copy", result);
        goto error_operationattr_copy;
    }
    *dst = dst_attr;
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error_operationattr_copy:
    globus_free(dst_attr);
error_dst_attr:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_gridftp_attr_destroy(
    void *                              driver_attr)
{
    globus_l_xio_gridftp_attr_t *       attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_gridftp_attr_destroy);

    GlobusXIOGridftpDebugEnter();
    attr = (globus_l_xio_gridftp_attr_t *) driver_attr;
    if (attr->ftp_operation_attr)
    {
        result = globus_ftp_client_operationattr_destroy(
                    &attr->ftp_operation_attr);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_ftp_client_operationattr_destroy", result);
            goto error;
        }
    }
    globus_free(attr);
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_gridftp_init(
    globus_xio_driver_t *               out_driver)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_gridftp_init);

    GlobusXIOGridftpDebugEnter();
    result = globus_xio_driver_init(&driver, "gridftp", NULL);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_driver_init", result);
        goto error_init;
    }
    globus_xio_driver_set_transport(
        driver,
        globus_l_xio_gridftp_open,
        globus_l_xio_gridftp_close,
        globus_l_xio_gridftp_read,
        globus_l_xio_gridftp_write,
        globus_l_xio_gridftp_cntl);
    globus_xio_driver_set_attr(
        driver,
        globus_l_xio_gridftp_attr_init,
        globus_l_xio_gridftp_attr_copy,
        globus_l_xio_gridftp_attr_cntl,
        globus_l_xio_gridftp_attr_destroy);
    *out_driver = driver;
    GlobusXIOGridftpDebugExit();
    return GLOBUS_SUCCESS;

error_init:
    GlobusXIOGridftpDebugExitWithError();
    return result;
}


static
void
globus_l_xio_gridftp_destroy(
    globus_xio_driver_t                 driver)
{
    globus_xio_driver_destroy(driver);
}


GlobusXIODefineDriver(
    gridftp,
    globus_l_xio_gridftp_init,
    globus_l_xio_gridftp_destroy);
