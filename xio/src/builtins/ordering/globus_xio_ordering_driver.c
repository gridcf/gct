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

#include "globus_xio_driver.h"
#include "globus_xio_ordering_driver.h"
#include "version.h"

GlobusDebugDefine(GLOBUS_XIO_ORDERING);
GlobusXIODeclareDriver(ordering);

#define GlobusXIOOrderingDebugPrintf(level, message)                          \
    GlobusDebugPrintf(GLOBUS_XIO_ORDERING, level, message)

#define GlobusXIOOrderingDebugEnter()                                         \
    GlobusXIOOrderingDebugPrintf(                                             \
        GLOBUS_L_XIO_ORDERING_DEBUG_TRACE,                                    \
        ("[%s] Entering\n", _xio_name))

#define GlobusXIOOrderingDebugExit()                                          \
    GlobusXIOOrderingDebugPrintf(                                             \
        GLOBUS_L_XIO_ORDERING_DEBUG_TRACE,                                    \
        ("[%s] Exiting\n", _xio_name))

#define GlobusXIOOrderingDebugExitWithError()                                 \
    GlobusXIOOrderingDebugPrintf(                                             \
        GLOBUS_L_XIO_ORDERING_DEBUG_TRACE,                                    \
        ("[%s] Exiting with error\n", _xio_name))

enum globus_l_xio_error_levels
{
    GLOBUS_L_XIO_ORDERING_DEBUG_TRACE                = 1,
    GLOBUS_L_XIO_ORDERING_DEBUG_INTERNAL_TRACE       = 2
};


typedef enum globus_i_xio_ordering_state_s
{

    GLOBUS_XIO_ORDERING_NONE,
    GLOBUS_XIO_ORDERING_READY,
    GLOBUS_XIO_ORDERING_IO_PENDING,
    GLOBUS_XIO_ORDERING_EOF_RECEIVED,
    GLOBUS_XIO_ORDERING_EOF_DELIVERED,
    GLOBUS_XIO_ORDERING_CLOSE_PENDING,
    GLOBUS_XIO_ORDERING_CLOSING,
    GLOBUS_XIO_ORDERING_ERROR

} globus_i_xio_ordering_state_t;

typedef struct
{
    /* Specifies max number of reads that could be outstanding at any time */
    int					max_read_count;
    globus_bool_t			buffering;
    globus_size_t			buf_size;

    /* Specifies max number of buffers that could be outstanding at any time */
    int					max_buf_count;
} globus_l_xio_ordering_attr_t;

static globus_l_xio_ordering_attr_t     globus_l_xio_ordering_attr_default =
{
    1,
    GLOBUS_FALSE,
    100000,
    100
};

typedef struct globus_l_xio_ordering_user_req_s 
				globus_l_xio_ordering_user_req_t;

typedef struct
{
    globus_l_xio_ordering_attr_t *      attr;
    globus_i_xio_ordering_state_t       state;  
    globus_l_xio_ordering_user_req_t *	user_req;
    globus_priority_q_t                 buffer_q;
    globus_list_t *			driver_op_list;
    globus_mutex_t                      mutex;
    globus_off_t                        offset;
    globus_off_t                        expected_offset;
    int                                 outstanding_read_count;
    int					read_count;
    int					write_count;
    int					buffer_count;
    globus_xio_operation_t              close_op;
    globus_xio_driver_handle_t		driver_handle;
    globus_object_t *			error;
} globus_l_xio_ordering_handle_t;

struct globus_l_xio_ordering_user_req_s
{
    globus_xio_operation_t		op;
    globus_xio_iovec_t *                iovec;
    int                                 iovec_count;
    int                                 index;
    globus_off_t                        offset;
    globus_size_t                       length;
    globus_size_t                       wait_for;
    globus_size_t                       nbytes;
    globus_object_t *			error;
};

typedef struct
{
    globus_xio_operation_t		op;
    globus_l_xio_ordering_handle_t *	handle;
    globus_xio_iovec_t *                iovec;
    globus_off_t                        offset;
    globus_off_t                        data_offset;
    globus_size_t                       nbytes;
    globus_object_t *			error;
} globus_l_xio_ordering_buffer_t;

static
int
globus_l_xio_ordering_activate(void);

static
int
globus_l_xio_ordering_deactivate(void);

static
void
globus_l_xio_ordering_read_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg);

static
void
globus_l_xio_ordering_close_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg);

static
globus_result_t
globus_l_xio_ordering_attr_init(
    void **                             out_attr);

static
globus_result_t
globus_l_xio_ordering_attr_copy(
    void **                             dst,
    void *                              src);

static
globus_result_t
globus_l_xio_ordering_attr_destroy(
    void *                              driver_attr);

GlobusXIODefineModule(ordering) =
{
    "globus_xio_ordering",
    globus_l_xio_ordering_activate,
    globus_l_xio_ordering_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

#define GlobusXIOOrderingErrorRead(reason)                                  \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GlobusXIOMyModule(ordering),                                    \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_ORDERING_ERROR_READ,                                 \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Read error: %s", (reason)))

#define GlobusXIOOrderingErrorCancel(reason)                                \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GlobusXIOMyModule(ordering),                                    \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_ORDERING_ERROR_CANCEL,                               \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Read error: %s", (reason)))

#define GlobusXIOOrderingMin(min, size1, size2)				    \
    do									    \
    {									    \
	globus_size_t			_size1;				    \
	globus_size_t			_size2;				    \
	globus_size_t			_min;				    \
	_size1 = (size1);						    \
	_size2 = (size2);						    \
	if (_size1 > _size2)						    \
	{								    \
	    _min = _size2;						    \
	}								    \
	else								    \
	{								    \
	    _min = _size1;						    \
	}								    \
	min = _min;							    \
    } while(0)


static
int
globus_l_xio_ordering_activate(void)
{
    int rc;
    GlobusXIOName(globus_l_xio_ordering_activate);

    GlobusDebugInit(GLOBUS_XIO_ORDERING, TRACE);
    GlobusXIOOrderingDebugEnter();
    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto error_xio_system_activate;
    }
    GlobusXIORegisterDriver(ordering);
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;

error_xio_system_activate:
    GlobusXIOOrderingDebugExitWithError();
    GlobusDebugDestroy(GLOBUS_XIO_ORDERING);
    return rc;
}


static
int
globus_l_xio_ordering_deactivate(void)
{   
    int rc;
    GlobusXIOName(globus_l_xio_ordering_deactivate);
    
    GlobusXIOOrderingDebugEnter();
    GlobusXIOUnRegisterDriver(ordering);
    rc = globus_module_deactivate(GLOBUS_XIO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {   
        goto error_deactivate;
    }
    GlobusXIOOrderingDebugExit();
    GlobusDebugDestroy(GLOBUS_XIO_ORDERING);
    return GLOBUS_SUCCESS;

error_deactivate:
    GlobusXIOOrderingDebugExitWithError();
    GlobusDebugDestroy(GLOBUS_XIO_ORDERING);
    return rc;
}


static
int
globus_l_xio_ordering_offset_cmp(
    void *                              priority1,
    void *                              priority2)
{
    globus_off_t *                      offset1;
    globus_off_t *                      offset2;
    int                                 rc = 0;
    GlobusXIOName(globus_l_xio_ordering_offset_cmp);
 
    GlobusXIOOrderingDebugEnter();
    offset1 = (globus_off_t *)priority1;
    offset2 = (globus_off_t *)priority2;
    if (*offset1 > *offset2)
    {
        rc = 1;
    }
    else if (*offset1 < *offset2)
    {
        rc = -1;
    }
    GlobusXIOOrderingDebugExit();
    return rc;
}
   

static
globus_result_t
globus_l_xio_ordering_handle_destroy(
    globus_l_xio_ordering_handle_t *      handle)
{
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_ordering_handle_destroy);

    GlobusXIOOrderingDebugEnter();
    result = globus_l_xio_ordering_attr_destroy(handle->attr);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_ordering_attr_destroy", result);
        goto error;
    }
    globus_object_free(handle->error);
    globus_priority_q_destroy(&handle->buffer_q);
    if (handle->driver_op_list)
    {
        globus_list_free(handle->driver_op_list);
    }
    globus_mutex_destroy(&handle->mutex);
    globus_free(handle);
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOOrderingDebugExitWithError();
    return result;
}

static
void
globus_i_xio_ordering_save_error(
    globus_l_xio_ordering_handle_t *    handle,
    globus_result_t                     result)
{
    GlobusXIOName(globus_i_xio_ordering_save_error);

    GlobusXIOOrderingDebugEnter();
    handle->state = GLOBUS_XIO_ORDERING_ERROR;
    if (handle->error == GLOBUS_NULL)
    {
        handle->error = globus_object_copy(globus_error_peek(result));
    }
    GlobusXIOOrderingDebugExit();
}


/* allocate the memory for and initialize an internal handle */
static 
globus_result_t 
globus_l_xio_ordering_handle_create(
    globus_l_xio_ordering_handle_t **   out_handle,
    globus_l_xio_ordering_attr_t *      attr)
{
    globus_l_xio_ordering_handle_t *    handle;
    globus_result_t                     result;
    globus_size_t			handle_size;
    GlobusXIOName(globus_l_xio_ordering_handle_create);

    GlobusXIOOrderingDebugEnter();
    handle_size = sizeof(globus_l_xio_ordering_handle_t);
    handle = (globus_l_xio_ordering_handle_t *)globus_malloc(handle_size);
    if (handle == GLOBUS_NULL)
    {
        result = GlobusXIOErrorMemory("handle");
        goto error_handle;
    }
    memset(handle, 0, handle_size);
    handle->user_req = (globus_l_xio_ordering_user_req_t *)
		    globus_malloc(sizeof(globus_l_xio_ordering_user_req_t));
    if (handle->user_req == GLOBUS_NULL)
    {
        result = GlobusXIOErrorMemory("user_req");
        goto error_user_req;
    }
    if (!attr)
    {
        result = globus_l_xio_ordering_attr_init((void**)(void*)&handle->attr);
    }
    else
    {
        result = globus_l_xio_ordering_attr_copy(
                                (void**)(void*)&handle->attr, (void*)attr);
    }
    if (result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_ordering_attr_copy", result);
        goto error_attr;
    }
    result = globus_priority_q_init(
                &handle->buffer_q, globus_l_xio_ordering_offset_cmp);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_buffer_q_init;
    }
    memset(handle->user_req, 0, sizeof(globus_l_xio_ordering_user_req_t));
    globus_mutex_init(&handle->mutex, NULL);     
    GlobusXIOOrderingDebugExit();
    *out_handle = handle;
    return GLOBUS_SUCCESS;

error_buffer_q_init:
    globus_l_xio_ordering_attr_destroy(handle->attr);
error_attr:
    globus_free(handle->user_req);
error_user_req:
    globus_free(handle);
error_handle:
    GlobusXIOOrderingDebugExitWithError();
    return result;
}


/* called locked */
static
globus_result_t
globus_i_xio_ordering_register_read(
    globus_l_xio_ordering_handle_t *	handle,
    globus_l_xio_ordering_buffer_t *	buffer)
{
    globus_list_t *			op_sub_list;
    globus_result_t			result;
    GlobusXIOName(globus_i_xio_ordering_regsiter_read);

    GlobusXIOOrderingDebugEnter();
    if (!buffer)
    {
	/* I'm allocated to create upto attr->max_buf_count no. of buffers */
	if (handle->buffer_count >= handle->attr->max_buf_count)
	{
	    result = GlobusXIOErrorMemory("too many buffers");
	    goto error_buffer;
	}
	buffer = (globus_l_xio_ordering_buffer_t *)
		globus_malloc(sizeof(globus_l_xio_ordering_buffer_t));
	if (!buffer)
	{
	    result = GlobusXIOErrorMemory("buffer");
	    goto error_buffer;
	}
	memset(buffer, 0, sizeof(globus_l_xio_ordering_buffer_t));
	buffer->iovec = (globus_xio_iovec_t *) globus_malloc(
					    sizeof(globus_xio_iovec_t));
	if (!buffer->iovec)
	{
	    result = GlobusXIOErrorMemory("buffer->iovec");
	    goto error_iovec;
	}
	buffer->iovec[0].iov_base = globus_malloc(
			    sizeof(globus_byte_t) * handle->attr->buf_size); 
	if (!buffer->iovec[0].iov_base)
	{
	    result = GlobusXIOErrorMemory("buffer->iovec[0]->iov_base");
	    goto error_iov_base;
	}
	buffer->iovec[0].iov_len = handle->attr->buf_size;
	result = globus_xio_driver_operation_create(
				&buffer->op, handle->driver_handle);
	if (result != GLOBUS_SUCCESS)
	{
	    goto error_operation_create;
	}
	buffer->handle = handle;
	++handle->buffer_count;
	globus_list_insert(&handle->driver_op_list, buffer->op);
    }
    result = globus_xio_driver_pass_read(
			buffer->op,
			(globus_xio_iovec_t *)buffer->iovec,
			1,
			1,
			globus_l_xio_ordering_read_cb,
			buffer);
    if (result != GLOBUS_SUCCESS)
    {
	goto error;
    }
    else
    {
	++handle->outstanding_read_count;
    }
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;

/* 
 * If there is an error in pass_read, i destroy buffer irrespective of 
 * whether I created the buffer now or not
 */
error:
    --handle->buffer_count;
    op_sub_list = globus_list_search(handle->driver_op_list, buffer->op);
    globus_list_remove(&handle->driver_op_list, op_sub_list);
    globus_xio_driver_operation_destroy(buffer->op);
error_operation_create:
    globus_free(buffer->iovec[0].iov_base);
error_iov_base:
    globus_free(buffer->iovec);
error_iovec:
    globus_free(buffer);
error_buffer:
    GlobusXIOOrderingDebugExitWithError();
    return result;
}
   

static
void
globus_l_xio_ordering_open_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_ordering_handle_t *    handle;
    globus_result_t			res;
    GlobusXIOName(globus_l_xio_ordering_open_cb);

    GlobusXIOOrderingDebugEnter();
    handle = (globus_l_xio_ordering_handle_t *)user_arg;
    if (result == GLOBUS_SUCCESS)
    {
        globus_mutex_lock(&handle->mutex);
        handle->state = GLOBUS_XIO_ORDERING_READY;
        globus_mutex_unlock(&handle->mutex);
    }
    else
    {
	goto error;
    }
    globus_xio_driver_finished_open(handle, op, GLOBUS_SUCCESS);
    GlobusXIOOrderingDebugExit();
    return;    

error:
    res = globus_l_xio_ordering_handle_destroy(handle);
    globus_assert(res == GLOBUS_SUCCESS);
    handle = GLOBUS_NULL;
    globus_xio_driver_finished_open(handle, op, result);
    GlobusXIOOrderingDebugExitWithError();
    return;    
}


static
globus_result_t
globus_l_xio_ordering_open(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_l_xio_ordering_handle_t *    handle;
    globus_l_xio_ordering_attr_t *      attr;
    globus_result_t                     result = GLOBUS_SUCCESS;
    GlobusXIOName(globus_l_xio_ordering_open);

    GlobusXIOOrderingDebugEnter();
    handle = (globus_l_xio_ordering_handle_t *) driver_link;
    attr = (globus_l_xio_ordering_attr_t *) driver_attr;
    globus_assert(handle == NULL);
    result = globus_l_xio_ordering_handle_create(&handle, attr);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_handle_create;
    }
    handle->driver_handle = globus_xio_operation_get_driver_handle(op);
    globus_xio_driver_pass_open(
                op, contact_info, globus_l_xio_ordering_open_cb, handle);
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;

error_handle_create:
    GlobusXIOOrderingDebugExitWithError();
    return result;
}


static
void
globus_l_xio_ordering_cancel_cb(
    globus_xio_operation_t              op,
    void *                              user_arg,
    globus_xio_error_type_t             reason)
{
    globus_l_xio_ordering_handle_t *	handle;
    globus_size_t			nbytes;
    globus_bool_t			finish_read = GLOBUS_FALSE;
    globus_bool_t			finish_close = GLOBUS_FALSE;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_ordering_cancel_cb);

    GlobusXIOOrderingDebugEnter();
    handle = (globus_l_xio_ordering_handle_t *) user_arg;
    /* 
     * I did some changes to globus_xio_handle.c and globus_xio_server.c to
     * allow finish to be called from cancel_cb. I put 'my_op->in_register =
     * GLOBUS_TRUE' before every occurence of 'op->cancel_cb()' and 
     * 'my_op->in_register = GLOBUS_FALSE after 'op->cancel_cb()' where my_op
     * is the 'op->op_entry[op->ndx-1]' 
     */
    globus_mutex_lock(&handle->mutex);
    switch (handle->state)
    {
	case GLOBUS_XIO_ORDERING_IO_PENDING:
	    if (handle->read_count == 1)
	    {
		nbytes = handle->user_req->nbytes;
		finish_read = GLOBUS_TRUE;
		--handle->read_count;
		if (handle->write_count == 0)
		{
		    handle->state = GLOBUS_XIO_ORDERING_READY;
		}
	    }
	    break;
	/* 
	 * If the handle's state is READY or CLOSING. it implies that 
	 * finished_read/finished_close is going to be called soon. so i can
	 * ignore the cancel.
	 */
	case GLOBUS_XIO_ORDERING_READY:
	case GLOBUS_XIO_ORDERING_CLOSING:
	    break;
	case GLOBUS_XIO_ORDERING_CLOSE_PENDING:
	    finish_close = GLOBUS_TRUE;
	    handle->state = GLOBUS_XIO_ORDERING_READY;
	    break;
	case GLOBUS_XIO_ORDERING_ERROR:
            goto error;
	default:    
            result = GlobusXIOOrderingErrorCancel(
			    "Invalid state in cancel_cb"); 
            globus_i_xio_ordering_save_error(handle, result);
	    goto error;
    }
    globus_mutex_unlock(&handle->mutex);
    if (finish_read)
    {
	globus_xio_driver_finished_read(op, GlobusXIOErrorCanceled(), nbytes);
    }
    else if (finish_close)
    {
	globus_xio_driver_finished_close(op, GlobusXIOErrorCanceled());
    }
    GlobusXIOOrderingDebugExit();
    return;

error:
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOOrderingDebugExitWithError();
    return;
}


/* called locked */
static
void
globus_l_xio_ordering_buffer_destroy(
    globus_l_xio_ordering_handle_t *    handle,
    globus_l_xio_ordering_buffer_t *	buffer)
{
    globus_list_t *			op_sub_list = GLOBUS_NULL;
    GlobusXIOName(globus_l_xio_ordering_buffer_destroy);

    GlobusXIOOrderingDebugEnter();
    /*
     * I do this checking here becoz if user calls close and there are some
     * reads pending with driver created op, those ops will be available in
     * handle->driver_op_list. I remove those ops from the driver_op_list
     * in the close interface and call cancel on those ops. Once they are
     * canceled, i'll get read_cb and I call buffer_destroy (this function) 
     * in read_cb. I do remove only if i'm sure the element is present.
     */  
    if (handle->driver_op_list)
    {
	op_sub_list = globus_list_search(handle->driver_op_list, buffer->op);
    }
    if (op_sub_list)
    {
        globus_list_remove(&handle->driver_op_list, op_sub_list);
    }
    globus_xio_driver_operation_destroy(buffer->op);
    globus_free(buffer->iovec[0].iov_base);
    globus_free(buffer->iovec);
    globus_free(buffer);
    GlobusXIOOrderingDebugExit();
}


/* called locked */
static
globus_bool_t
globus_l_xio_ordering_copy(
    globus_l_xio_ordering_handle_t *    handle)
{
    globus_l_xio_ordering_buffer_t *	buffer;
    globus_l_xio_ordering_user_req_t *	user_req;
    globus_xio_iovec_t *                iovec;
    globus_size_t                       len;
    globus_size_t                       total_len;
    globus_size_t                       nbytes_left;
    globus_bool_t                       finish = GLOBUS_FALSE;
    globus_bool_t                       done;
    GlobusXIOName(globus_l_xio_ordering_copy);

    GlobusXIOOrderingDebugEnter();
    user_req = handle->user_req;
    iovec = user_req->iovec;
    do
    {
	total_len = 0;
        buffer = (globus_l_xio_ordering_buffer_t *) 
                    globus_priority_q_dequeue(&handle->buffer_q);
	if (buffer->error)
	{
	    if (!user_req->error)
	    {
		user_req->error = buffer->error;
	    }
	    else
	    {
		globus_error_put(buffer->error);
	    }
	}
	nbytes_left = buffer->nbytes - buffer->offset;
        while (nbytes_left > 0 && user_req->nbytes < user_req->length)
        {
            GlobusXIOOrderingMin(
			len,
			iovec[user_req->index].iov_len - user_req->offset, 
			buffer->nbytes - buffer->offset);
            memcpy(
               (globus_byte_t *) 
                   iovec[user_req->index].iov_base + user_req->offset, 
               (globus_byte_t *) buffer->iovec[0].iov_base + buffer->offset, 
               len);
            if (user_req->offset + len == iovec[user_req->index].iov_len)
            {
                ++user_req->index;
                user_req->offset = 0;
            }
            else
            {
                user_req->offset += len;
            }
            user_req->nbytes += len;
            nbytes_left -= len;
            buffer->offset += len;
	    total_len += len;
        }
        handle->expected_offset += total_len;
	done = GLOBUS_TRUE;
        if (nbytes_left == 0)
        {
	    globus_xio_driver_pass_read(
                                buffer->op,
                                (globus_xio_iovec_t *)buffer->iovec,
                                1,
                                1,
                                globus_l_xio_ordering_read_cb,
                                buffer);
	    ++handle->outstanding_read_count;
	    /* 
	     * This if block is not put outside of the above if block coz
	     * if nbytes_left != 0, then user_req->nbytes will surely be 
	     * >= user_req->length (otherwise while loop would not have ended)
	     * and the condition would fail. To avoids unnecessary checking of
	     * that condition, this is placed here.
	     */
	    if (user_req->nbytes < user_req->length && 
		!globus_priority_q_empty(&handle->buffer_q))
	    {
		globus_off_t * offset;
		offset = (globus_off_t *) 
			globus_priority_q_first_priority(&handle->buffer_q);
		if (*offset == handle->expected_offset)
		{
		    done = GLOBUS_FALSE;
		}
	    }
	}
	else
	{
            buffer->data_offset += total_len;
            globus_priority_q_enqueue(
                &handle->buffer_q, buffer, &buffer->data_offset);
	}
    } while (!done);
    if (user_req->nbytes >= user_req->wait_for)
    {
        finish = GLOBUS_TRUE;
    }
    GlobusXIOOrderingDebugExit();
    return finish;
}


static
void
globus_l_xio_ordering_read_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_result_t                     res;
    globus_l_xio_ordering_handle_t *    handle;
    globus_l_xio_ordering_buffer_t *    buffer;
    globus_off_t                        offset;
    globus_off_t                        expected_offset;
    globus_xio_operation_t		requestor_op;
    globus_result_t                     requestor_result;
    globus_size_t			requestor_nbytes;
    globus_bool_t                       finish = GLOBUS_FALSE;
    globus_bool_t                       finish_close = GLOBUS_FALSE;
    globus_bool_t			eof = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_ordering_read_cb);

    GlobusXIOOrderingDebugEnter();
    buffer = (globus_l_xio_ordering_buffer_t *) user_arg;
    handle = buffer->handle;
    globus_mutex_lock(&handle->mutex); 
    --handle->outstanding_read_count;
    if (globus_error_match(globus_error_peek(result),
				GLOBUS_XIO_MODULE,
				GLOBUS_XIO_ERROR_EOF))
    {
        eof = GLOBUS_TRUE;
    }
    switch (handle->state)
    {
	case GLOBUS_XIO_ORDERING_READY:
        case GLOBUS_XIO_ORDERING_IO_PENDING:
	    if ((result == GLOBUS_SUCCESS) || (eof && (nbytes > 0)))
	    {
		/* 
		 * This driver can be used in 2 modes; ordering (care about
		 * offsets) and buffering (do not care about offsets). In
		 * buffering mode, buffer->data_offset (which is used as the
		 * priority in the priority q) is set to handle->offset and 
		 * handle->offset is incremented by nbytes. the new 
		 * handle->offset becomes the priority for the next chunk of 
		 * data. The increment value does not really matter. I could
		 * have just used a value of 1 (the reason for choosing nbytes
		 * is purely arbitrary). In buffering mode, the priority
		 * is just the order of arrival
		 */
		if (handle->attr->buffering)
		{
		    /* 
		     * this assignment below is to make sure that copy is
		     * called whenever there is a user_op pending. see the
		     * condition used to check before copy is called below
		     */
		    offset = handle->expected_offset;
		    buffer->data_offset = handle->offset;
		    handle->offset += nbytes;
		}
		else
		{
		    res = globus_xio_driver_data_descriptor_cntl(
				op, NULL, GLOBUS_XIO_DD_GET_OFFSET, &offset);
		    if (res != GLOBUS_SUCCESS)
		    {
			globus_i_xio_ordering_save_error(handle, res);
			goto error;
		    }
		    buffer->data_offset = offset;
		}
		/* 
		 * buffer->offset is used to offset into the buffer. It 
		 * specifies the offset from the start of the buffer upto 
		 * which the data has been copied to the user buffer
		 */
		buffer->offset = 0;
		buffer->nbytes = nbytes;
		buffer->error = globus_error_get(result);
		globus_priority_q_enqueue(
			    &handle->buffer_q, buffer, &buffer->data_offset);
		expected_offset = handle->expected_offset;
		/* 
		 * I register a new read if outstanding_read_count < 
		 * max_read_count or offset != handle->expected_offset. If
		 * the condition on the 'if loop' succeeds, then copy() might
		 * modify handle->expected_offset. thats why i store it above
		 */
		if (handle->read_count == 1 && offset == expected_offset)
		{
		    finish = globus_l_xio_ordering_copy(handle);
		    if (eof || finish)
		    {
			--handle->read_count;
			if (eof)
			{
			    handle->state = GLOBUS_XIO_ORDERING_EOF_RECEIVED;
			}
			else if (handle->write_count == 0)
			{
			    handle->state = GLOBUS_XIO_ORDERING_READY;
			}
			requestor_op = handle->user_req->op;
			requestor_result = globus_error_put(
						handle->user_req->error);
			requestor_nbytes = handle->user_req->nbytes;
		    }
		}
		if (handle->outstanding_read_count < 
					    handle->attr->max_read_count)
		{
		    res = globus_i_xio_ordering_register_read(
							handle, GLOBUS_NULL);
		    if (res != GLOBUS_SUCCESS)
		    {
                        globus_i_xio_ordering_save_error(handle, res);
			/* 
			 * I do not have a goto error; here coz under error 
			 * label, i finish read with error but there may not 
			 * any error in the read that finished
			 */
		    }
		}
	    }
	    else if (eof)
	    {
	        handle->state = GLOBUS_XIO_ORDERING_EOF_RECEIVED;
		if (handle->read_count > 0 && 
				globus_priority_q_empty(&handle->buffer_q))
		{
		    requestor_op = handle->user_req->op;
		    requestor_result = result;
		    requestor_nbytes = handle->user_req->nbytes;
		    finish = GLOBUS_TRUE;
		}
	    }
            else if(globus_error_match(
                globus_error_peek(result),
                GLOBUS_XIO_MODULE,
                GLOBUS_XIO_ERROR_CANCELED))
	    {
		/* 
		 * While the handle is in READY state, SET_OFFSET handle cntl
		 * alone can cancel the driver_ops. In that case, all the 
		 * buffered data is discarded and I have to initiate new reads 
		 * and I make use of the existing buffers
		 */
		res = globus_i_xio_ordering_register_read(handle, buffer);
		if (res != GLOBUS_SUCCESS)
		{
                    globus_i_xio_ordering_save_error(handle, res);
		    /* 
		     * Here I do not have a goto error; coz this error does
		     * not correspond to the finished read
		     */
		}
	    }
	    else
	    {
		globus_i_xio_ordering_save_error(handle, result);
		goto error;
	    }
	    globus_mutex_unlock(&handle->mutex);
	    break;
	case GLOBUS_XIO_ORDERING_CLOSE_PENDING:
	    globus_l_xio_ordering_buffer_destroy(handle, buffer);
	    if (handle->outstanding_read_count == 0)
	    {
		handle->state = GLOBUS_XIO_ORDERING_CLOSING;
	        globus_mutex_unlock(&handle->mutex);
		globus_xio_operation_disable_cancel(handle->close_op);
		/* 
		 * If cancel has been called in between unlock and disable 
		 * pass_close will fail. 
		 */
		res = globus_xio_driver_pass_close(
			handle->close_op,
			globus_l_xio_ordering_close_cb,
			handle);
		if (res != GLOBUS_SUCCESS)
		{
		    finish_close = GLOBUS_TRUE;
		}
	    }
	    else
	    {
	        globus_mutex_unlock(&handle->mutex);
	    }
	    break;
	case GLOBUS_XIO_ORDERING_EOF_RECEIVED:
	case GLOBUS_XIO_ORDERING_EOF_DELIVERED:
	    globus_mutex_unlock(&handle->mutex);
	    break;
	case GLOBUS_XIO_ORDERING_ERROR:
	    goto error;
	default:
	    res = GlobusXIOOrderingErrorRead("Invalid state in read cb");
	    globus_i_xio_ordering_save_error(handle, res);
	    goto error;
    }
    if (finish)
    {
	/*
	 * If finish is TRUE, I change state to READY (see above) so that I 
	 * dont do anything in the cancel_cb (in case if the op is cancelled 
	 * before I call disable_cancel)
	 */
	globus_xio_operation_disable_cancel(requestor_op);
	globus_xio_driver_finished_read(
			    requestor_op, requestor_result, requestor_nbytes);
    }
    if (finish_close)
    {
	/* no contention for handle->close_op so I use it without lock */
	globus_xio_driver_finished_close(handle->close_op, res);
    }
    GlobusXIOOrderingDebugExit();
    return;

error:
    globus_l_xio_ordering_buffer_destroy(handle, buffer);
    if (handle->read_count == 1)
    {
	--handle->read_count;
	finish = GLOBUS_TRUE;
        res = globus_error_put(globus_object_copy(handle->error));
    }
    globus_mutex_unlock(&handle->mutex);
    /*
     * I access the handle->user_req etc after unlock coz there is no 
     * contention for them after i set state to ERROR 
     */
    if (finish)
    {
	globus_xio_operation_disable_cancel(handle->user_req->op);
	globus_xio_driver_finished_read(
	    handle->user_req->op, res, handle->user_req->nbytes);
    }
    GlobusXIOOrderingDebugExitWithError();
    return;
}

static
void
globus_i_xio_ordering_user_req_init(
    globus_l_xio_ordering_user_req_t *  user_req,
    globus_xio_operation_t              op,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count)
{
    GlobusXIOName(globus_l_xio_ordering_read);

    GlobusXIOOrderingDebugEnter();
    user_req->op = op;
    user_req->iovec = (globus_xio_iovec_t *)iovec;
    user_req->iovec_count = iovec_count;
    GlobusXIOUtilIovTotalLength(user_req->length, iovec, iovec_count);
    user_req->index = 0;
    user_req->offset = 0;
    user_req->nbytes = 0;
    user_req->wait_for = globus_xio_operation_get_wait_for(op);
    GlobusXIOOrderingDebugExit();
}

static
globus_result_t
globus_l_xio_ordering_read(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_xio_ordering_handle_t *    handle;
    globus_bool_t                       finish = GLOBUS_FALSE;
    globus_result_t                     result;
    globus_result_t                     res;
    globus_size_t			nbytes = 0;
    globus_off_t *                      offset;
    GlobusXIOName(globus_l_xio_ordering_read);

    GlobusXIOOrderingDebugEnter();
    handle = (globus_l_xio_ordering_handle_t *) driver_specific_handle;
    globus_mutex_lock(&handle->mutex);
    switch (handle->state)
    {
	case GLOBUS_XIO_ORDERING_EOF_RECEIVED:
            if (!globus_priority_q_empty(&handle->buffer_q))
            {
		globus_i_xio_ordering_user_req_init(
			    handle->user_req, op, iovec, iovec_count);
    		offset = (globus_off_t *) 
                	globus_priority_q_first_priority(&handle->buffer_q);
		if (*offset == handle->expected_offset)
		{
		    finish = globus_l_xio_ordering_copy(handle);
		}
		nbytes = handle->user_req->nbytes;
		if (finish)
		{
		    res = globus_error_put(handle->user_req->error);
		}
		else
		{   
		    result = GlobusXIOOrderingErrorRead(
		      "EOF already received but requested data not in buffer"); 
		    goto error;
		}
		break;
            }
	    else 
	    {
		handle->state = GLOBUS_XIO_ORDERING_EOF_DELIVERED;
		/* fall through */
	    }
	    /* 
	     * The framework should allow any further reads after an eof is
	     * delivered but just in case
	     */
	case GLOBUS_XIO_ORDERING_EOF_DELIVERED: 
	    res = GlobusXIOErrorEOF();
	    finish = GLOBUS_TRUE;
	    break;
	case GLOBUS_XIO_ORDERING_IO_PENDING:
	    /* 
	     * IO_PENDING implies that either a read or write is pending. 
	     * Cant have multiple simultaneous reads but a simultaneous read
	     * and write is allowed.
	     */ 
	    if (handle->read_count == 1)
	    {
		result = GlobusXIOOrderingErrorRead(
		  "Ordering driver does not allow multiple simultaneous reads");
		goto error;
	    }
	    /* fall through */
        case GLOBUS_XIO_ORDERING_READY:
	    globus_i_xio_ordering_user_req_init(
			    handle->user_req, op, iovec, iovec_count);
            if (!globus_priority_q_empty(&handle->buffer_q))
            {
    		offset = (globus_off_t *) 
                	globus_priority_q_first_priority(&handle->buffer_q);
		if (*offset == handle->expected_offset)
		{
		    finish = globus_l_xio_ordering_copy(handle);
		}
            }
	    else if (!handle->driver_op_list) /* first read after open */
	    {
		int i;
		for (i = 0; i < handle->attr->max_read_count; i++)
		{
		    result = globus_i_xio_ordering_register_read(
							handle, GLOBUS_NULL);
		    if (result != GLOBUS_SUCCESS)
		    {
			goto error;
		    }
		}
		
	    }
	    if (finish)
	    {
		nbytes = handle->user_req->nbytes;
		res = globus_error_put(handle->user_req->error);
	    }
	    else
	    {
		handle->state = GLOBUS_XIO_ORDERING_IO_PENDING;
	        ++handle->read_count;
	    }
            break;
	case GLOBUS_XIO_ORDERING_ERROR:
	    result = globus_error_put(globus_object_copy(handle->error));
	    goto error;
        default:
	    result = GlobusXIOErrorInvalidState(handle->state);
            goto error;
    }
    globus_mutex_unlock(&handle->mutex);
    if (finish)
    {
	globus_xio_driver_finished_read(op, res, nbytes);
    }
    else if (globus_xio_operation_enable_cancel(
        op, globus_l_xio_ordering_cancel_cb, handle))
    {
	globus_mutex_lock(&handle->mutex);
	--handle->read_count;
	if (handle->write_count == 0)
	{
	    handle->state = GLOBUS_XIO_ORDERING_READY;
	}
        result = GlobusXIOErrorCanceled();
	/* unlock happens at error label */
	goto error;
    }
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;

error:
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOOrderingDebugExitWithError();
    return result;
}


static
void
globus_l_xio_ordering_write_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_xio_ordering_handle_t *	handle;
    GlobusXIOName(globus_l_xio_ordering_write_cb);

    GlobusXIOOrderingDebugEnter();
    handle = (globus_l_xio_ordering_handle_t *) user_arg;
    globus_mutex_lock(&handle->mutex);
    --handle->write_count;
    if (handle->write_count == 0 && handle->read_count == 0)
    {
	/*
	 * The state could also be EOF_RECEIVED/EOF_DELIVERED. In that
	 * case, the state should not be changed to READY because once
	 * EOF is received, no further reads are allowed. The state machine
	 * can be better but for some reason the initial version of this driver
	 * did not handle EOF and the EOF states were added later and thus had
	 * to do it this way.
	 */
	if (handle->state == GLOBUS_XIO_ORDERING_IO_PENDING)
	{
            handle->state = GLOBUS_XIO_ORDERING_READY;
	}
    }
    globus_mutex_unlock(&handle->mutex);
    globus_xio_driver_finished_write(op, result, nbytes);
    GlobusXIOOrderingDebugExit();
    return;
}


static
globus_result_t
globus_l_xio_ordering_write(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_xio_ordering_handle_t *    handle;
    globus_size_t                       length;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_ordering_write);

    GlobusXIOOrderingDebugEnter();
    handle = (globus_l_xio_ordering_handle_t *) driver_specific_handle;

    /* 
     * I dont have to do anything special in cancel_cb if the user cancels a
     * write operation. So I dont do any enable cancel here. As I just pass
     * the user op to the driver below, I assume xio_cancel will invoke 
     * cancel on driver below and I'll eventually get the write_cb
     */
    globus_mutex_lock(&handle->mutex);
    switch (handle->state)
    {
	/* 
	 * I allow multiple outstanding writes from the user. Otherwise user
	 * of this ordering driver will not be able to take advantage of the
	 * mode_e or gridftp driver below (basically use parallel writes on
	 * multiple sockets. Also the order in which the write_cb goes to the 
	 * user does not matter (unlike the read_cb's). I use read_count and
	 * write_count to track the number of pending reads and writes. 
	 * read_count can be either 0 or 1 as i dont allow multiple pending 
	 * reads.
	 */
        case GLOBUS_XIO_ORDERING_READY:
	    handle->state = GLOBUS_XIO_ORDERING_IO_PENDING;
	    /* fall through */
	
	    /*
	     * The state machine
	     * can be better but for some reason the initial version of this 
	     * driver did not handle EOF and the EOF states were added later 
	     * and thus had to do it this way.
	     */
	case GLOBUS_XIO_ORDERING_EOF_RECEIVED:
	    /* fall through */
	case GLOBUS_XIO_ORDERING_EOF_DELIVERED:
	    /* fall through */
        case GLOBUS_XIO_ORDERING_IO_PENDING:
	    /* ??? this may be right. not sure if this will work for indicating
	       the offset to the driver below ??? */
	    ++handle->write_count;
            result = globus_xio_driver_data_descriptor_cntl(
                        op,
                        NULL,
                        GLOBUS_XIO_DD_SET_OFFSET,
                        handle->offset);
            if (result != GLOBUS_SUCCESS)
            {
                goto error;
            }
            GlobusXIOUtilIovTotalLength(length, iovec, iovec_count);
            result = globus_xio_driver_pass_write(
                op,
                (globus_xio_iovec_t *)iovec,
                iovec_count,
                length,
                globus_l_xio_ordering_write_cb,
                handle);
            if (result != GLOBUS_SUCCESS)
            {
                goto error;
            }
            handle->offset += length;
            break;
	case GLOBUS_XIO_ORDERING_ERROR:
	    result = globus_error_put(globus_object_copy(handle->error));
	    goto error;
        default:
	    result = GlobusXIOErrorInvalidState(handle->state);
            goto error;
    }
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;

error:
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOOrderingDebugExitWithError();
    return result;
}


static
void
globus_l_xio_ordering_close_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_ordering_handle_t *    handle;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_ordering_close_cb);

    GlobusXIOOrderingDebugEnter();
    handle = (globus_l_xio_ordering_handle_t *)user_arg;
    /* 
     * The following lock and unlock is avoid the following race: 
     * ordering_close obtains lock and calls pass_close, before the
     * pass_close returns and lock is released this close_cb gets called
     * and handle_destroyed
     */
    globus_mutex_lock(&handle->mutex);
    globus_mutex_unlock(&handle->mutex);
    res = globus_l_xio_ordering_handle_destroy(handle);
    globus_assert(res == GLOBUS_SUCCESS);
    globus_xio_driver_finished_close(op, result);
    GlobusXIOOrderingDebugExit();
}


static
globus_result_t
globus_l_xio_ordering_close(
    void *                              driver_specific_handle,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_l_xio_ordering_handle_t *    handle;
    globus_xio_operation_t		driver_op = GLOBUS_NULL;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_ordering_close);

    GlobusXIOOrderingDebugEnter();
    handle = (globus_l_xio_ordering_handle_t *) driver_specific_handle;
    /* 
     * This close interface will be invoked only when no other user op is 
     * pending
     */
    GlobusXIOOrderingDebugPrintf(GLOBUS_L_XIO_ORDERING_DEBUG_TRACE, 
		    ("before got lock\n"));
    globus_mutex_lock(&handle->mutex);
    GlobusXIOOrderingDebugPrintf(GLOBUS_L_XIO_ORDERING_DEBUG_TRACE, 
		    ("got lock\n"));
    if (!globus_priority_q_empty(&handle->buffer_q))
    {
        globus_l_xio_ordering_buffer_t* buffer;
        GlobusXIOOrderingDebugPrintf(GLOBUS_L_XIO_ORDERING_DEBUG_TRACE, 
			("priority queue not empty\n"));
	do
	{
	    buffer = globus_priority_q_dequeue(&handle->buffer_q);
	    globus_l_xio_ordering_buffer_destroy(handle, buffer);
	} while (!globus_priority_q_empty(&handle->buffer_q));
    }
    GlobusXIOOrderingDebugPrintf(GLOBUS_L_XIO_ORDERING_DEBUG_TRACE,
		    ("outside if\n"));
    /*
     * outstanding ops won't be present in buffer_q but will be in
     * driver_op_list
     */
    if (!globus_list_empty(handle->driver_op_list) && 
		    (handle->outstanding_read_count > 0))
    {
        GlobusXIOOrderingDebugPrintf(GLOBUS_L_XIO_ORDERING_DEBUG_TRACE, 
		    ("driver op not empty and outstanding read count > 0\n"));
	/*
	 * Cancel will be called on all ops irrespective of whether it is
	 * outstanding or in possession of this driver. This is because there
	 * is only one driver_op_list (if there is another list that has all
	 * the outstanding ops, it can be optimized). The check for outstanding
	 * reads in the if loop above ensures that atleast there is one
	 * outstanding read. So that the cancel below will get the control to
	 * read_cb
	 */
	
	handle->state = GLOBUS_XIO_ORDERING_CLOSE_PENDING;
	do
	{
	    driver_op = (globus_xio_operation_t) globus_list_remove(
			    &handle->driver_op_list, handle->driver_op_list);
	    result = globus_xio_driver_operation_cancel(
					handle->driver_handle, driver_op);
	    if (result != GLOBUS_SUCCESS)
	    {
		goto error;
	    }
	} while (!globus_list_empty(handle->driver_op_list));
        handle->close_op = op;
    }
    else
    {
        GlobusXIOOrderingDebugPrintf(GLOBUS_L_XIO_ORDERING_DEBUG_TRACE, 
		    ("driver op empty or outstanding read count > 0"));
	handle->state = GLOBUS_XIO_ORDERING_CLOSING;
        result = globus_xio_driver_pass_close(
                op,
                globus_l_xio_ordering_close_cb,
                handle);
        if (result != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }
    globus_mutex_unlock(&handle->mutex);
    /* 
     * user is allowed to cancel a close only if there are any driver_ops 
     * outstanding. driver_op != NULL implies there are outstanding op(s) 
     */
    if (driver_op) 
    {
	if (globus_xio_operation_enable_cancel(
	    	op, globus_l_xio_ordering_cancel_cb, handle))
	{
	    globus_mutex_lock(&handle->mutex);
	    handle->state = GLOBUS_XIO_ORDERING_READY;
	    result = GlobusXIOErrorCanceled();
	    /* unlock happens at error label */
	    goto error;
	}
    }
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;      

error:
    globus_i_xio_ordering_save_error(handle, result);
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOOrderingDebugExitWithError();
    return result;      
}


static
globus_result_t
globus_l_xio_ordering_cntl(
    void  *                             driver_specific_handle,
    int                                 cmd,
    va_list                             ap)
{
    globus_l_xio_ordering_handle_t *    handle;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_ordering_cntl);

    GlobusXIOOrderingDebugEnter();
    handle = (globus_l_xio_ordering_handle_t *) driver_specific_handle;
    globus_mutex_lock(&handle->mutex);
    switch(cmd)
    {
        case GLOBUS_XIO_SEEK:
        case GLOBUS_XIO_ORDERING_SET_OFFSET:
        {
	    if (handle->state == GLOBUS_XIO_ORDERING_READY)
	    {
		globus_off_t 		offset;
		globus_xio_operation_t	op;
		offset = va_arg(ap, globus_off_t);
		handle->offset = offset;
		if (!globus_list_empty(handle->driver_op_list))
		{
		    /* 
		     * it is harmless to cancel the idle op (cancel doesnt
		     * do anything if the op is considered in your 
		     * possession), so i cancel all the driver ops here
		     */
		    do
		    {
			op = (globus_xio_operation_t) globus_list_remove(
			    &handle->driver_op_list, handle->driver_op_list);
			result = globus_xio_driver_operation_cancel(
						handle->driver_handle, op);
			if (result != GLOBUS_SUCCESS)
			{
                            globus_i_xio_ordering_save_error(handle, result);
			    goto error;
			}
		    } while (!globus_list_empty(handle->driver_op_list));
		}
		if (!globus_priority_q_empty(&handle->buffer_q))
		{
		    globus_l_xio_ordering_buffer_t * buffer;
		    /* 
		     * discard all the buffered data and invoke new reads on
		     * buffered ops
		     */
		    do
		    {
			buffer = globus_priority_q_dequeue(&handle->buffer_q);
			result = globus_i_xio_ordering_register_read(
							    handle, buffer);
			if (result != GLOBUS_SUCCESS)
			{
			    globus_i_xio_ordering_save_error(handle, result);
			    goto error;
			}
		    } while (!globus_priority_q_empty(&handle->buffer_q));
		}
	    }
	    else if (handle->state == GLOBUS_XIO_ORDERING_EOF_RECEIVED ||
		    handle->state == GLOBUS_XIO_ORDERING_EOF_DELIVERED)
	    {
		/*
		 * In this case, there can not be any outstanding reads, so
		 * no need to worry about this like in the above if 
		 */
		globus_off_t 		offset;
		offset = va_arg(ap, globus_off_t);
		handle->offset = offset;
	    }
	    else
	    {
	        result = GlobusXIOErrorInvalidState(handle->state);
		goto error;
	    }
            break;
        }
        default:
            result = GlobusXIOErrorInvalidCommand(cmd);
            goto error;     
    }   
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;

error:
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOOrderingDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_ordering_attr_init(
    void **                             out_attr)
{
    globus_l_xio_ordering_attr_t *      attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_ordering_attr_init);

    GlobusXIOOrderingDebugEnter();
    /*
     *  create a ordering attr structure and intialize its values
     */
    attr = (globus_l_xio_ordering_attr_t *) 
                globus_malloc(sizeof(globus_l_xio_ordering_attr_t));
    if(!attr)
    {
        result = GlobusXIOErrorMemory("attr");
        goto error_attr;
    }
    memcpy(attr, &globus_l_xio_ordering_attr_default, 
                        sizeof(globus_l_xio_ordering_attr_t));
    *out_attr = attr;
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;

error_attr:
    GlobusXIOOrderingDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_ordering_attr_cntl(
    void *                              driver_attr,
    int                                 cmd,
    va_list                             ap)
{
    globus_result_t                     result;
    globus_l_xio_ordering_attr_t *      attr;
    GlobusXIOName(globus_l_xio_ordering_attr_cntl);

    GlobusXIOOrderingDebugEnter();
    attr = (globus_l_xio_ordering_attr_t *) driver_attr;
    switch(cmd)
    {
        case GLOBUS_XIO_ORDERING_SET_MAX_READ_COUNT:
	    attr->max_read_count = va_arg(ap, int);
            break;
        case GLOBUS_XIO_ORDERING_GET_MAX_READ_COUNT:
	{
	    int * max_read_count_out = va_arg(ap, int *);
	    *max_read_count_out = attr->max_read_count;
            break;
	}
        case GLOBUS_XIO_ORDERING_SET_BUFFERING:
	    attr->buffering = va_arg(ap, globus_bool_t);
	    break;
        case GLOBUS_XIO_ORDERING_GET_BUFFERING:
	{
	    globus_bool_t * buffering_out = va_arg(ap, globus_bool_t *);
	    *buffering_out = attr->buffering;
	    break;
	}
        case GLOBUS_XIO_ORDERING_SET_BUF_SIZE:
	    attr->buf_size = va_arg(ap, globus_size_t);
	    break;
        case GLOBUS_XIO_ORDERING_GET_BUF_SIZE:
	{
	    globus_size_t * buf_size_out = va_arg(ap, globus_size_t *);
	    *buf_size_out = attr->buf_size;
	    break;
	}
        case GLOBUS_XIO_ORDERING_SET_MAX_BUF_COUNT:
	    attr->max_buf_count = va_arg(ap, int);
	    break;
        case GLOBUS_XIO_ORDERING_GET_MAX_BUF_COUNT:
	{
	    int * out_max_buf_count;
	    out_max_buf_count = va_arg(ap, int *);
	    *out_max_buf_count = attr->max_buf_count;
	    break;
	}
        default:
           result = GlobusXIOErrorInvalidCommand(cmd);
           goto error;
    }   
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;

error:  
    GlobusXIOOrderingDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_ordering_attr_copy(
    void **                             dst,
    void *                              src)
{
    globus_l_xio_ordering_attr_t *      src_attr;
    globus_l_xio_ordering_attr_t *      dst_attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_ordering_attr_copy);

    GlobusXIOOrderingDebugEnter();
    dst_attr = (globus_l_xio_ordering_attr_t *) 
                globus_malloc(sizeof(globus_l_xio_ordering_attr_t));
    if(!dst_attr)
    {
        result = GlobusXIOErrorMemory("attr");
        goto error_dst_attr;
    }
    src_attr = (globus_l_xio_ordering_attr_t *) src;
    memcpy(dst_attr, src_attr, sizeof(globus_l_xio_ordering_attr_t)); 
    *dst = dst_attr;
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;

error_dst_attr:
    GlobusXIOOrderingDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_ordering_attr_destroy(
    void *                              driver_attr)
{
    globus_l_xio_ordering_attr_t *      attr;
    GlobusXIOName(globus_l_xio_ordering_attr_destroy);

    GlobusXIOOrderingDebugEnter();
    attr = (globus_l_xio_ordering_attr_t *) driver_attr;
    globus_free(attr);
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;
}


static
globus_result_t
globus_l_xio_ordering_init(
    globus_xio_driver_t *               out_driver)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_ordering_init);

    GlobusXIOOrderingDebugEnter();
    result = globus_xio_driver_init(&driver, "ordering", GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_driver_init", result);
        goto error_init;
    }
    globus_xio_driver_set_transform(
        driver,
        globus_l_xio_ordering_open,
        globus_l_xio_ordering_close,
        globus_l_xio_ordering_read,
        globus_l_xio_ordering_write,
        globus_l_xio_ordering_cntl,
        GLOBUS_NULL);
    globus_xio_driver_set_attr(
        driver,
        globus_l_xio_ordering_attr_init,
        globus_l_xio_ordering_attr_copy,
        globus_l_xio_ordering_attr_cntl,
        globus_l_xio_ordering_attr_destroy);
    *out_driver = driver;
    GlobusXIOOrderingDebugExit();
    return GLOBUS_SUCCESS;

error_init:
    GlobusXIOOrderingDebugExitWithError();
    return result;
}


static
void
globus_l_xio_ordering_destroy(
    globus_xio_driver_t                 driver)
{
    globus_xio_driver_destroy(driver);
}


GlobusXIODefineDriver(
    ordering,
    globus_l_xio_ordering_init,
    globus_l_xio_ordering_destroy);

