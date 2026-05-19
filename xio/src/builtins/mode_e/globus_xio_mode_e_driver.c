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
#include "globus_xio_mode_e_driver.h"
#include "version.h"

GlobusDebugDefine(GLOBUS_XIO_MODE_E);
GlobusXIODeclareDriver(mode_e);

#define GlobusXIOModeEDebugPrintf(level, message)                            \
    GlobusDebugPrintf(GLOBUS_XIO_MODE_E, level, message)

#define GlobusXIOModeEDebugEnter()                                           \
    GlobusXIOModeEDebugPrintf(                                               \
        GLOBUS_L_XIO_MODE_E_DEBUG_TRACE,                                     \
        ("[%s] Entering\n", _xio_name))

#define GlobusXIOModeEDebugExit()                                            \
    GlobusXIOModeEDebugPrintf(                                               \
        GLOBUS_L_XIO_MODE_E_DEBUG_TRACE,                                     \
        ("[%s] Exiting\n", _xio_name))

#define GlobusXIOModeEDebugExitWithError()                                   \
    GlobusXIOModeEDebugPrintf(                                               \
        GLOBUS_L_XIO_MODE_E_DEBUG_TRACE,                                     \
        ("[%s] Exiting with error\n", _xio_name))

enum globus_l_xio_error_levels
{
    GLOBUS_L_XIO_MODE_E_DEBUG_TRACE                = 1,
    GLOBUS_L_XIO_MODE_E_DEBUG_INTERNAL_TRACE       = 2
};

#define GLOBUS_XIO_MODE_E_IO_Q_SIZE 8
#define GLOBUS_XIO_MODE_E_HEADER_COUNT 8
#define GLOBUS_XIO_MODE_E_MAX_OFFSET_SIZE 8
#define GLOBUS_XIO_MODE_E_OFFSET_HT_SIZE 8

#define GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_CLOSE 0x04
#define GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOD 0x08
#define GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOF 0x40


typedef enum globus_i_xio_mode_e_state_s
{

    GLOBUS_XIO_MODE_E_NONE,
    GLOBUS_XIO_MODE_E_OPEN,
    GLOBUS_XIO_MODE_E_OPENING,
    GLOBUS_XIO_MODE_E_SENDING_EOD,
    GLOBUS_XIO_MODE_E_EOF_RECEIVED,
    GLOBUS_XIO_MODE_E_EOF_DELIVERED,
    GLOBUS_XIO_MODE_E_CLOSING,
    GLOBUS_XIO_MODE_E_ERROR

} globus_i_xio_mode_e_state_t;

typedef globus_result_t
(*globus_xio_mode_e_handle_cntl_callback_t)(
    globus_xio_handle_t                 xio_handle);

typedef struct
{
    globus_xio_stack_t                  stack;
    int                                 max_connection_count;
    int                                 eod_count;
    globus_xio_attr_t                   xio_attr;
    globus_bool_t                       send_eod;
    globus_bool_t                       manual_eodc;
    globus_off_t                        offset;
    globus_bool_t                       offset_reads;
} globus_l_xio_mode_e_attr_t;

static globus_l_xio_mode_e_attr_t       globus_l_xio_mode_e_attr_default =
{
    GLOBUS_NULL,
    1,
    0,
    GLOBUS_NULL,
    GLOBUS_FALSE,
    GLOBUS_FALSE,
    -1,
    GLOBUS_FALSE
};

typedef struct
{
    globus_byte_t                       descriptor;
    globus_byte_t                       count[8];
    globus_byte_t                       offset[8];
} globus_l_xio_mode_e_header_t;

typedef struct
{
    globus_xio_server_t                 server;
    globus_xio_handle_t                 accepted_handle;
    globus_l_xio_mode_e_attr_t *        attr;
    globus_i_xio_mode_e_state_t         state;
    globus_memory_t                     requestor_memory;
    globus_memory_t                     header_memory;
    char *                              cs;
    globus_list_t *                     connection_list;
    globus_list_t *                     close_list;
    globus_list_t *                     eod_list;
    globus_fifo_t                       connection_q;
    globus_hashtable_t                  offset_ht;
    globus_fifo_t                       eod_q;
    int                                 connection_count;
    int                                 close_count;
    globus_off_t                        eod_count;
    globus_size_t                       eods_received;
    globus_size_t                       eods_sent;
    globus_bool_t                       eof_sent;
    globus_bool_t                       close_canceled;
    globus_fifo_t                       io_q;
    globus_mutex_t                      mutex;
    globus_off_t                        offset;
    globus_off_t                        eod_offset;
    globus_xio_operation_t              outstanding_op;
    int                                 ref_count;
    globus_xio_stack_t                  stack;
    globus_xio_driver_t                 driver;
    globus_object_t *                   error;
} globus_l_xio_mode_e_handle_t;

typedef struct
{
    globus_xio_operation_t              op;
    globus_xio_iovec_t *                iovec;
    int                                 iovec_count;
    globus_l_xio_mode_e_attr_t *        dd;
    globus_l_xio_mode_e_handle_t *      handle;
    globus_xio_handle_t                 xio_handle;
} globus_i_xio_mode_e_requestor_t;

typedef struct
{
    globus_xio_handle_t                 xio_handle;
    globus_l_xio_mode_e_handle_t *      mode_e_handle;
    globus_i_xio_mode_e_requestor_t *   requestor;
    int                                 iovec_index;
    globus_size_t                       iovec_index_len;
    globus_off_t                        outstanding_data_len;
    globus_off_t                        outstanding_data_offset;
    globus_bool_t                       eod;
    globus_bool_t                       close;
} globus_l_xio_mode_e_connection_handle_t;

static
int
globus_l_xio_mode_e_activate(void);

static
int
globus_l_xio_mode_e_deactivate(void);

static
globus_result_t
globus_l_xio_mode_e_handle_create(
    globus_l_xio_mode_e_handle_t **     out_handle,
    globus_l_xio_mode_e_attr_t *        attr);

static
globus_result_t
globus_l_xio_mode_e_handle_destroy(
    globus_l_xio_mode_e_handle_t *      handle);

static
void
globus_i_xio_mode_e_server_open_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg);

static
void
globus_l_xio_mode_e_server_open_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg);

static
globus_result_t
globus_i_xio_mode_e_register_read_header(
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle);

static
globus_result_t
globus_i_xio_mode_e_register_read(
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle);

static
globus_result_t
globus_i_xio_mode_e_register_write(
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle);

static
globus_result_t
globus_l_xio_mode_e_register_eod(
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle,
    globus_byte_t                       descriptor);

static
void
globus_l_xio_mode_e_close_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg);

static
globus_result_t
globus_l_xio_mode_e_attr_init(
    void **                             out_attr);

static
globus_result_t
globus_l_xio_mode_e_attr_copy(
    void **                             dst,
    void *                              src);

static
globus_result_t
globus_l_xio_mode_e_attr_destroy(
    void *                              driver_attr);


GlobusXIODefineModule(mode_e) =
{
    "globus_xio_mode_e",
    globus_l_xio_mode_e_activate,
    globus_l_xio_mode_e_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};


#define GlobusXIOModeEHeaderError(reason)                                   \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GlobusXIOMyModule(mode_e),                                      \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_MODE_E_HEADER_ERROR,                                 \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Header error: %s", (reason)))


static globus_xio_string_cntl_table_t mode_e_l_string_opts_table[] =
{
    {"streams", GLOBUS_XIO_MODE_E_SET_NUM_STREAMS, globus_xio_string_cntl_int},
    {NULL, 0, NULL}
};


static
int
globus_l_xio_mode_e_activate(void)
{
    int rc;
    GlobusXIOName(globus_l_xio_mode_e_activate);

    GlobusDebugInit(GLOBUS_XIO_MODE_E, TRACE);

    GlobusXIOModeEDebugEnter();
    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto error_xio_system_activate;
    }

    GlobusXIORegisterDriver(mode_e);
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_xio_system_activate:
    GlobusXIOModeEDebugExitWithError();
    GlobusDebugDestroy(GLOBUS_XIO_MODE_E);
    return rc;
}


static
int
globus_l_xio_mode_e_deactivate(void)
{
    int rc;
    GlobusXIOName(globus_l_xio_mode_e_deactivate);

    GlobusXIOModeEDebugEnter();
    GlobusXIOUnRegisterDriver(mode_e);
    rc = globus_module_deactivate(GLOBUS_XIO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto error_deactivate;
    }

    GlobusXIOModeEDebugExit();
    GlobusDebugDestroy(GLOBUS_XIO_MODE_E);
    return GLOBUS_SUCCESS;

error_deactivate:
    GlobusXIOModeEDebugExitWithError();
    GlobusDebugDestroy(GLOBUS_XIO_MODE_E);
    return rc;
}


int
globus_l_xio_mode_e_hashtable_offset_hash(
    void *                              offsetp,
    int                                 limit)
{
    unsigned long                       h = 0;
    unsigned long                       g;
    char *                              key;
    int                                 i;
    globus_size_t                       size;
    GlobusXIOName(globus_l_xio_mode_e_hashtable_offset_hash);

    GlobusXIOModeEDebugEnter();
    key = (char *) offsetp;
    size = sizeof(globus_off_t);
    for (i = 0; i < size; i++)
    {
        h = (h << 4) + *key++;
        if ((g = (h & 0xF0UL)))
        {
            h ^= g >> 24;
            h ^= g;
        }
    }
    GlobusXIOModeEDebugExit();
    return h % limit;
}


int
globus_l_xio_mode_e_hashtable_offset_keyeq(
    void *                              offsetp1,
    void *                              offsetp2)
{
    globus_size_t                       size;
    int                                 rc = 0;
    GlobusXIOName(globus_l_xio_mode_e_hashtable_offset_keyeq);

    GlobusXIOModeEDebugEnter();
    size = sizeof(globus_off_t);
    if(offsetp1 == offsetp2 ||
        (offsetp1 && offsetp2 && strncmp(offsetp1, offsetp2, size) == 0))
    {
        rc = 1;
    }
    GlobusXIOModeEDebugExit();
    return rc;
}


static
globus_result_t
globus_l_xio_mode_e_handle_destroy(
    globus_l_xio_mode_e_handle_t *      handle)
{
    globus_result_t                     result;
    globus_bool_t                       stack = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_mode_e_handle_destroy);

    GlobusXIOModeEDebugEnter();
    if (!handle->attr->stack)
    {
        stack = GLOBUS_TRUE;
    }
    result = globus_l_xio_mode_e_attr_destroy(handle->attr);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_mode_e_attr_destroy", result);
        goto error;
    }
    globus_object_free(handle->error);
    globus_fifo_destroy(&handle->connection_q);
    globus_fifo_destroy(&handle->eod_q);
    globus_fifo_destroy(&handle->io_q);
    globus_memory_destroy(&handle->requestor_memory);
    globus_memory_destroy(&handle->header_memory);
    globus_list_free(handle->connection_list);
    globus_list_free(handle->eod_list);
    globus_list_free(handle->close_list);
    globus_mutex_destroy(&handle->mutex);
    if (handle->server)
    {
        globus_xio_server_close(handle->server);
    }
    if (stack)
    {
        globus_xio_stack_destroy(handle->stack);
    }
    globus_free(handle);
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
void
globus_i_xio_mode_e_header_encode(
    globus_byte_t *                     buf,
    globus_off_t                        in_value)
{
    globus_off_t                        x;
    int                                 i;
    globus_size_t                       offset_size;
    GlobusXIOName(globus_i_xio_mode_e_header_encode);

    GlobusXIOModeEDebugEnter();
    offset_size = sizeof(globus_off_t);
    /*
     * buf[0] contains most significant byte and buf[7] contains the
     * least significant byte
     */
    globus_assert(GLOBUS_XIO_MODE_E_MAX_OFFSET_SIZE >= offset_size);
    for (i = GLOBUS_XIO_MODE_E_MAX_OFFSET_SIZE; i > offset_size; i--)
    {
        buf[GLOBUS_XIO_MODE_E_MAX_OFFSET_SIZE - i] = 0;
    }
    x = (globus_off_t)in_value;
    for (i = offset_size; i > 0; i--)
    {
        buf[GLOBUS_XIO_MODE_E_MAX_OFFSET_SIZE - i] = (x >> (i - 1) * 8) & 0xff;
    }
    GlobusXIOModeEDebugExit();
}


static
globus_result_t
globus_i_xio_mode_e_header_decode(
    globus_byte_t *                     buf,
    globus_off_t *                      out_value)
{
    globus_off_t                        x = 0;
    int                                 i;
    globus_size_t                       offset_size;
    globus_result_t                     result;
    GlobusXIOName(globus_i_xio_mode_e_header_decode);

    GlobusXIOModeEDebugEnter();
    offset_size = sizeof(globus_off_t);
    for (i = GLOBUS_XIO_MODE_E_MAX_OFFSET_SIZE; i > offset_size; i--)
    {
        /*
         * if offset_size < MAX_OFFSET_SIZE; then the most significant
         * (MAX_OFFSET_SIZE - offset_size) number of bytes should be zero;
         * otherwise there is an overflow
         */
        if (buf[GLOBUS_XIO_MODE_E_MAX_OFFSET_SIZE - i] != 0)
        {
            result = GlobusXIOModeEHeaderError("offset overflow");
            goto overflow;
        }
    }
    if (offset_size > GLOBUS_XIO_MODE_E_MAX_OFFSET_SIZE)
    {
        offset_size = GLOBUS_XIO_MODE_E_MAX_OFFSET_SIZE;
    }
    for (i = 0; i < offset_size; i++)
    {
        x += ((globus_off_t) buf[i]) << (offset_size - i - 1) * 8;
    }
    *out_value = (globus_off_t)x;
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

overflow:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


/*
 *  allocate the memory for and initialize an internal handle
 */
static
globus_result_t
globus_l_xio_mode_e_handle_create(
    globus_l_xio_mode_e_handle_t **     out_handle,
    globus_l_xio_mode_e_attr_t *        attr)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_result_t                     result;
    int                                 node_size;
    int                                 node_count;
    GlobusXIOName(globus_l_xio_mode_e_handle_create);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *)
                globus_malloc(sizeof(globus_l_xio_mode_e_handle_t));
    if (handle == GLOBUS_NULL)
    {
        result = GlobusXIOErrorMemory("handle");
        goto error_handle;
    }
    memset(handle, 0, sizeof(globus_l_xio_mode_e_handle_t));
    if (!attr)
    {
        result = globus_l_xio_mode_e_attr_init((void**)(void*)&handle->attr);
        if (result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_l_xio_mode_e_attr_init", result);
            goto error_attr;
        }
    }
    else
    {
        result = globus_l_xio_mode_e_attr_copy(
                                (void**)(void*)&handle->attr, (void*)attr);
        if (result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_l_xio_mode_e_attr_copy", result);
            goto error_attr;
        }
    }
    if (!handle->attr->stack)
    {
        result = globus_xio_driver_load("tcp", &handle->driver);
        if (result != GLOBUS_SUCCESS)
        {
            goto error_driver_load;
        }
        result = globus_xio_stack_init(&handle->stack, NULL);
        if (result != GLOBUS_SUCCESS)
        {
            goto error_stack_init;
        }
        result = globus_xio_stack_push_driver(handle->stack, handle->driver);
        if (result != GLOBUS_SUCCESS)
        {
            goto error_push_driver;
        }
    }
    else
    {
        handle->stack = handle->attr->stack;
    }

    result = globus_fifo_init(&handle->connection_q);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_connection_q_init;
    }
    result = globus_fifo_init(&handle->eod_q);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_eod_q_init;
    }
    result = globus_fifo_init(&handle->io_q);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_io_q_init;
    }
    node_size = sizeof(globus_i_xio_mode_e_requestor_t);
    node_count = GLOBUS_XIO_MODE_E_IO_Q_SIZE;
    globus_memory_init(&handle->requestor_memory, node_size, node_count);
    node_size = sizeof(globus_l_xio_mode_e_header_t);
    node_count = GLOBUS_XIO_MODE_E_HEADER_COUNT;
    globus_memory_init(&handle->header_memory, node_size, node_count);
    globus_mutex_init(&handle->mutex, NULL);
    /*
     * As I did memset(handle, 0) in the beginning, here i initialize only the
     * fields that has to initialized with a non-zero value
     */
    handle->eod_count = -1;
    handle->ref_count = 1;
    *out_handle = handle;
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_io_q_init:
    globus_fifo_destroy(&handle->eod_q);
error_eod_q_init:
    globus_fifo_destroy(&handle->connection_q);
error_connection_q_init:
error_push_driver:
    if (!handle->attr->stack)
    {
        globus_xio_stack_destroy(handle->stack);
    }
error_stack_init:
    if (!handle->attr->stack)
    {
        globus_xio_driver_unload(handle->driver);
    }
error_driver_load:
    globus_l_xio_mode_e_attr_destroy(handle->attr);
error_attr:
    globus_free(handle);
error_handle:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_mode_e_server_init(
    void *                              driver_attr,
    const globus_xio_contact_t *        contact_info,
    globus_xio_operation_t              op)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_l_xio_mode_e_attr_t *        attr;
    globus_xio_contact_t                my_contact_info;
    char *                              cs;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_mode_e_server_init);

    GlobusXIOModeEDebugEnter();
    attr = (globus_l_xio_mode_e_attr_t *) driver_attr;
    result = globus_l_xio_mode_e_handle_create(&handle, attr);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_handle_create;
    }
    if (!attr)
    {
        attr = handle->attr;
    }
    if(attr->xio_attr == NULL)
    {
        result = globus_xio_attr_init(&attr->xio_attr);
        if (result != GLOBUS_SUCCESS)
        {
            goto error_attr_init;
        }
    }
    result = globus_xio_server_create(
            &handle->server, attr->xio_attr, handle->stack);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_server_create;
    }
    result = globus_xio_server_get_contact_string(handle->server, &cs);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_get_cs;
    }
    result = globus_xio_contact_parse(&my_contact_info, cs);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_parse_cs;
    }
    result = globus_xio_driver_pass_server_init(op, &my_contact_info, handle);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_pass_server_init;
    }
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_pass_server_init:
error_parse_cs:
error_get_cs:
    globus_xio_server_close(handle->server);
error_server_create:
    globus_xio_attr_destroy(attr->xio_attr);
error_attr_init:
    globus_l_xio_mode_e_handle_destroy(handle);
error_handle_create:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
void
globus_l_xio_mode_e_save_error(
    globus_l_xio_mode_e_handle_t *      handle,
    globus_result_t                     result)
{
    GlobusXIOName(globus_l_xio_mode_e_save_error);

    GlobusXIOModeEDebugEnter();
    handle->state = GLOBUS_XIO_MODE_E_ERROR;
    if (handle->error == GLOBUS_NULL)
    {
        handle->error = globus_object_copy(globus_error_peek(result));
    }
    GlobusXIOModeEDebugExit();
}


static
void
globus_l_xio_mode_e_server_accept_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_xio_operation_t              op;
    GlobusXIOName(globus_l_xio_mode_e_server_accept_cb);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *)user_arg;
    globus_xio_operation_disable_cancel(handle->outstanding_op);
    globus_mutex_lock(&handle->mutex);
    op = handle->outstanding_op;
    if (result == GLOBUS_SUCCESS)
    {
        ++handle->ref_count;
        handle->accepted_handle = xio_handle;
    }
    else
    {
        goto error_accept;
    }
    globus_mutex_unlock(&handle->mutex);
    globus_xio_driver_finished_accept(op, handle, result);
    GlobusXIOModeEDebugExit();
    return;

error_accept:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_driver_finished_accept(op, handle, result);
    GlobusXIOModeEDebugExitWithError();
    return;
}


/*
 * At any instance, there would be a globus_xio_server_register_accept
 * outstanding to accept any new incoming connection. For the very first
 * register accept (done in globus_l_xio_mode_e_server_accept),
 * globus_l_xio_mode_e_server_accept_cb is used. For the subsequent
 * register accpets, this function is used
 */
static
void
globus_i_xio_mode_e_server_accept_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_result_t                     res;
    GlobusXIOName(globus_i_xio_mode_e_server_accept_cb);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *)user_arg;
    globus_mutex_lock(&handle->mutex);
    if (result != GLOBUS_SUCCESS)
    {
        res = result;
        goto error_accept;
    }
    switch (handle->state)
    {
        case GLOBUS_XIO_MODE_E_OPEN:
        {
            res = globus_xio_register_open(
                        xio_handle,
                        NULL,
                        handle->attr->xio_attr,
                        globus_i_xio_mode_e_server_open_cb,
                        handle);
            if (res != GLOBUS_SUCCESS)
            {
                globus_xio_register_close(
                    xio_handle,
                    NULL,
                    NULL,
                    NULL);
                goto error_register_open;
            }
            res = globus_xio_server_register_accept(
                server,
                globus_i_xio_mode_e_server_accept_cb,
                handle);
            if (res != GLOBUS_SUCCESS)
            {
                goto error_register_accept;
            }
            break;
        }
        default:
            res = GlobusXIOErrorInvalidState(handle->state);
            goto error_invalid_state;
    }
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOModeEDebugExit();
    return;

error_register_accept:
error_register_open:
error_invalid_state:
error_accept:
    globus_l_xio_mode_e_save_error(handle, res);
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOModeEDebugExitWithError();
    return;
}


/* called locked */
static
globus_result_t
globus_i_xio_mode_e_cancel_operations(
    globus_l_xio_mode_e_handle_t *      handle)
{
    globus_xio_handle_t                 xio_handle;
    globus_result_t                     result;
    int                                 mask;
    GlobusXIOName(globus_i_xio_mode_e_cancel_operations);

    GlobusXIOModeEDebugEnter();
    /*
     * If user cancels close on the client side, both register_write (eods)
     * and register_close can be outstanding
     */
    mask = GLOBUS_XIO_CANCEL_WRITE;
    while (!globus_list_empty(handle->eod_list))
    {
        xio_handle = (globus_xio_handle_t)
                    globus_list_remove(&handle->eod_list, handle->eod_list);
        result = globus_xio_handle_cancel_operations(xio_handle, mask);
        if (result != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }
    mask = GLOBUS_XIO_CANCEL_CLOSE;
    while (!globus_list_empty(handle->close_list))
    {
        xio_handle = (globus_xio_handle_t)
                globus_list_remove(&handle->close_list, handle->close_list);
        result = globus_xio_handle_cancel_operations(xio_handle, mask);
        if (result != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
void
globus_l_xio_mode_e_cancel_cb(
    globus_xio_operation_t              op,
    void *                              user_arg,
    globus_xio_error_type_t             reason)
{
    globus_i_xio_mode_e_requestor_t *   requestor;
    globus_l_xio_mode_e_handle_t *      handle;
    int                                 mask;
    globus_bool_t                       finish = GLOBUS_FALSE;
    globus_result_t                     result;
    globus_bool_t                       send_eod = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_mode_e_cancel_cb);

    GlobusXIOModeEDebugEnter();
    requestor = (globus_i_xio_mode_e_requestor_t *) user_arg;
    handle = requestor->handle;
    globus_mutex_lock(&handle->mutex);
    switch (handle->state)
    {
        case GLOBUS_XIO_MODE_E_NONE:
            result = globus_xio_server_cancel_accept(handle->server);
            break;
        case GLOBUS_XIO_MODE_E_OPENING:
            result = globus_xio_handle_cancel_operations(
                        requestor->xio_handle, GLOBUS_XIO_CANCEL_OPEN);
            if (result != GLOBUS_SUCCESS)
            {
                goto error;
            }
            break;
        case GLOBUS_XIO_MODE_E_OPEN:
        case GLOBUS_XIO_MODE_E_SENDING_EOD:
            if (handle->server)
            {
                mask = GLOBUS_XIO_CANCEL_READ;
            }
            else
            {
                mask = GLOBUS_XIO_CANCEL_WRITE;
            }
            if (requestor->dd)
            {
                send_eod = requestor->dd->send_eod;
            }
            if (globus_fifo_empty(&handle->io_q) ||
                !globus_fifo_remove(&handle->io_q, requestor))
            {
                /*
                 * requestor->xio_handle would be NULL if cancel was called
                 * after enable_cancel is called in read/write and before the
                 * lock is required in read/write. In that case an xio
                 * operation would not have been initiated. So I don't do
                 * anything here.
                 */
                if (requestor->xio_handle)
                {
                    result = globus_xio_handle_cancel_operations(
                                                requestor->xio_handle, mask);
                    if (result != GLOBUS_SUCCESS)
                    {
                        goto error;
                    }
                }
            }
            else
            {
                globus_memory_push_node(
                        &handle->requestor_memory, (void*)requestor);
                finish = GLOBUS_TRUE;
            }
            if (send_eod)
            {
                result = globus_i_xio_mode_e_cancel_operations(handle);
                if (result != GLOBUS_SUCCESS)
                {
                    goto error;
                }
            }
            break;
        case GLOBUS_XIO_MODE_E_CLOSING:
            result = globus_i_xio_mode_e_cancel_operations(handle);
            if (result != GLOBUS_SUCCESS)
            {
                goto error;
            }
            break;
        case GLOBUS_XIO_MODE_E_EOF_RECEIVED:
        case GLOBUS_XIO_MODE_E_EOF_DELIVERED:
            break;
        default:
            result = GlobusXIOErrorInvalidState(handle->state);
            goto error;

    }
    globus_mutex_unlock(&handle->mutex);
    if (finish)
    {
        if (mask == GLOBUS_XIO_CANCEL_READ)
        {
            globus_xio_driver_finished_read(op, GlobusXIOErrorCanceled(), 0);
        }
        else
        {
            globus_xio_driver_finished_write(op, GlobusXIOErrorCanceled(), 0);
        }
    }
    GlobusXIOModeEDebugExit();
    return;

error:
    globus_l_xio_mode_e_save_error(handle, result);
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOModeEDebugExitWithError();
    return;
}


static
globus_result_t
globus_l_xio_mode_e_server_accept(
    void *                              driver_server,
    globus_xio_operation_t              op)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_i_xio_mode_e_requestor_t *   requestor;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_mode_e_server_accept);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t*)driver_server;
    handle->outstanding_op = op;
    requestor = (globus_i_xio_mode_e_requestor_t *)
                    globus_memory_pop_node(&handle->requestor_memory);
    requestor->handle = handle;
    requestor->op = op;
    if (globus_xio_operation_enable_cancel(
        op, globus_l_xio_mode_e_cancel_cb, requestor))
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
    result = globus_xio_server_register_accept(
                handle->server,
                globus_l_xio_mode_e_server_accept_cb,
                handle);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_register_accept;
    }
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_register_accept:
error_operation_canceled:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_operation_disable_cancel(op);
error_cancel_enable:
    /* globus memory has its own lock and i can use it like malloc and free */
    globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
    GlobusXIOModeEDebugExitWithError();
    return result;
}


globus_result_t
globus_l_xio_mode_e_server_destroy(
    void *                              driver_server)
{
    globus_l_xio_mode_e_handle_t *      handle;
    GlobusXIOName(globus_l_xio_mode_e_server_destroy);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *)driver_server;
    globus_mutex_lock(&handle->mutex);
    --handle->ref_count;
    if (handle->ref_count == 0)
    {
        globus_mutex_unlock(&handle->mutex);
        globus_l_xio_mode_e_handle_destroy(handle);
    }
    else
    {
        globus_mutex_unlock(&handle->mutex);
    }
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;
}


globus_result_t
globus_l_xio_mode_e_server_cntl(
    void *                              driver_server,
    int                                 cmd,
    va_list                             ap)
{
    GlobusXIOName(globus_l_xio_mode_e_server_cntl);

    GlobusXIOModeEDebugEnter();
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;
}


globus_result_t
globus_l_xio_mode_e_link_cntl(
    void *                              driver_link,
    int                                 cmd,
    va_list                             ap)
{
    GlobusXIOName(globus_l_xio_mode_e_link_cntl);

    GlobusXIOModeEDebugEnter();
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;
}


globus_result_t
globus_l_xio_mode_e_link_destroy(
    void *                              driver_link)
{
    globus_l_xio_mode_e_handle_t *      handle;
    GlobusXIOName(globus_l_xio_mode_e_link_destroy);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *) driver_link;
    globus_mutex_lock(&handle->mutex);
    --handle->ref_count;
    if (handle->ref_count == 0)
    {
        globus_mutex_unlock(&handle->mutex);
        globus_l_xio_mode_e_handle_destroy(handle);
    }
    else
    {
        globus_mutex_unlock(&handle->mutex);
    }
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;
}


static
void
globus_l_xio_mode_e_server_connection_handle_init(
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle)
{
    GlobusXIOName(globus_l_xio_mode_e_server_connection_handle_init);
    GlobusXIOModeEDebugEnter();
    connection_handle->requestor = GLOBUS_NULL;
    connection_handle->eod = GLOBUS_FALSE;
    connection_handle->close = GLOBUS_FALSE;
    connection_handle->outstanding_data_len = 0;
    connection_handle->iovec_index = -1;
    GlobusXIOModeEDebugExit();
}


/* called locked */
static
globus_result_t
globus_l_xio_mode_e_process_header(
    globus_l_xio_mode_e_header_t *      header,
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle)
{
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_mode_e_process_header);

    GlobusXIOModeEDebugEnter();
    if (header->descriptor & GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOD)
    {
        connection_handle->eod = GLOBUS_TRUE;
    }
    if (header->descriptor & GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_CLOSE)
    {
        /* sending CLOSE before EOD is a protocol violation */
        globus_assert(connection_handle->eod);
        connection_handle->close = GLOBUS_TRUE;
    }
    if (header->descriptor & GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOF)
    {
        connection_handle->outstanding_data_len = 0;
        connection_handle->outstanding_data_offset = 0;
        result = globus_i_xio_mode_e_header_decode(
            header->offset, &connection_handle->mode_e_handle->eod_count);
        if (result != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }
    else
    {
        result = globus_i_xio_mode_e_header_decode(
            header->count, &connection_handle->outstanding_data_len);
        if (result != GLOBUS_SUCCESS)
        {
            goto error;
        }
        result = globus_i_xio_mode_e_header_decode(
            header->offset, &connection_handle->outstanding_data_offset);
        if (result != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


/* called locked */
static
globus_i_xio_mode_e_requestor_t *
globus_l_xio_mode_e_process_outstanding_data(
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_i_xio_mode_e_requestor_t *   requestor = GLOBUS_NULL;
    GlobusXIOName(globus_l_xio_mode_e_process_outstanding_data);

    GlobusXIOModeEDebugEnter();
    handle = connection_handle->mode_e_handle;
    if (!globus_fifo_empty(&handle->io_q))
    {
        requestor = (globus_i_xio_mode_e_requestor_t *)
                        globus_fifo_dequeue(&handle->io_q);
        if (handle->attr->offset_reads)
        {
            /* wait_for of this requestor should be zero */
            requestor->dd->offset =
                connection_handle->outstanding_data_offset;
            globus_hashtable_insert(
                        &handle->offset_ht,
                        (void *) &connection_handle->outstanding_data_offset,
                        (void *) connection_handle);
        }
        else
        {
            connection_handle->requestor = requestor;
            requestor = GLOBUS_NULL;
            globus_i_xio_mode_e_register_read(connection_handle);
        }
    }
    else
    {
        globus_fifo_enqueue(&handle->connection_q, connection_handle);
    }
    GlobusXIOModeEDebugExit();
    return requestor;
}


static
globus_bool_t
globus_l_xio_mode_e_process_eod(
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle,
    globus_fifo_t *                     requestor_q)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_i_xio_mode_e_requestor_t *   requestor;
    globus_bool_t                       eof = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_mode_e_process_eod);

    GlobusXIOModeEDebugEnter();
    handle = connection_handle->mode_e_handle;
    ++handle->eods_received;
    if (connection_handle->close)
    {
        globus_xio_register_close(
                connection_handle->xio_handle,
                NULL,
                globus_l_xio_mode_e_close_cb,
                handle);
        globus_list_remove(&handle->connection_list,
            globus_list_search(handle->connection_list, connection_handle));
        globus_free(connection_handle);
    }
    else
    {
        globus_fifo_enqueue(&handle->eod_q, connection_handle);
    }
    if (handle->eod_count == handle->eods_received)
    {
        eof = GLOBUS_TRUE;
        while (!globus_fifo_empty(&handle->io_q))
        {
            requestor = (globus_i_xio_mode_e_requestor_t*)
                            globus_fifo_dequeue(&handle->io_q);
            globus_fifo_enqueue(requestor_q, requestor);
        }
    }
    GlobusXIOModeEDebugExit();
    return eof;
}


static
void
globus_l_xio_mode_e_read_header_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_l_xio_mode_e_header_t *      header;
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle;
    globus_i_xio_mode_e_requestor_t *   requestor = GLOBUS_NULL;
    globus_xio_operation_t              op;
    globus_fifo_t                       requestor_q;
    globus_bool_t                       eof;
    globus_bool_t                       finish = GLOBUS_FALSE;
    globus_bool_t                       finish_close = GLOBUS_FALSE;
    globus_result_t                     res;
    globus_off_t                        offset;
    GlobusXIOName(globus_l_xio_mode_e_read_header_cb);

    GlobusXIOModeEDebugEnter();
    globus_fifo_init(&requestor_q);
    connection_handle = (globus_l_xio_mode_e_connection_handle_t *) user_arg;
    handle = connection_handle->mode_e_handle;
    offset = connection_handle->outstanding_data_offset;
    globus_mutex_lock(&handle->mutex);
    if (result == GLOBUS_SUCCESS)
    {
        header = (globus_l_xio_mode_e_header_t *) buffer;
        result = globus_l_xio_mode_e_process_header(header, connection_handle);
        if (result != GLOBUS_SUCCESS)
        {
            goto error;
        }
        globus_memory_push_node(&handle->header_memory, (void*)buffer);
        if (connection_handle->outstanding_data_len > 0)
        {
            requestor = globus_l_xio_mode_e_process_outstanding_data(
                                                        connection_handle);
        }
        /*
         * If EOF is set in header, data len will be zero. If EOD alone is
         * set, there can be data present. If EOD is set on header and also
         * there is data present, i process EOD in read_cb. If I process it
         * here and if it so happens that eods_received == eod_count,
         * process_eod will return true (eof) and the state would be set to
         * GLOBUS_XIO_MODE_E_EOF_RECEIVED or DELIVERED and there would be no
         * way for the user to read the data that had EOD set on the header.
         */
        else if (connection_handle->eod)
        {
            eof = globus_l_xio_mode_e_process_eod(
                                        connection_handle, &requestor_q);
            if (eof)
            {
                if (globus_fifo_empty(&requestor_q))
                {
                    if (handle->state == GLOBUS_XIO_MODE_E_OPEN)
                    {
                        handle->state = GLOBUS_XIO_MODE_E_EOF_RECEIVED;
                    }
                }
                else
                {
                    requestor = (globus_i_xio_mode_e_requestor_t *)
                                        globus_fifo_peek(&requestor_q);
                    globus_xio_driver_set_eof_received(requestor->op);
                    /*
                     * If this part of the code is executed, finished_read
                     * happen only in if (finish) {} block below. The
                     * finished_read in if (requestor) {} block below should
                     * happen only if process_outstanding_data called above
                     * returns a non NULL requestor value. Thats why requestor
                     * is set to NULL here
                     */
                    requestor = GLOBUS_NULL;
                    finish = GLOBUS_TRUE;
                    if (handle->state == GLOBUS_XIO_MODE_E_OPEN)
                    {
                        handle->state = GLOBUS_XIO_MODE_E_EOF_DELIVERED;
                    }
                }
            }
        }
        else
        {
            result = globus_i_xio_mode_e_register_read_header(
                                                        connection_handle);
            if (result != GLOBUS_SUCCESS)
            {
                goto error;
            }
        }
    }
    else if(globus_error_match(
                globus_error_peek(result),
                GLOBUS_XIO_MODULE,
                GLOBUS_XIO_ERROR_CANCELED))
    {
        /*
         * if there are outstanding header reads and user calls close, the
         * header reads are canceled in close
         */
        if (!handle->close_canceled)
        {
            globus_xio_register_close(
                connection_handle->xio_handle,
                NULL,
                globus_l_xio_mode_e_close_cb,
                connection_handle->mode_e_handle);
            globus_list_insert(
                &handle->close_list, connection_handle->xio_handle);
        }
        else
        {
            ++handle->close_count;
            if (handle->close_count == handle->connection_count)
            {
                finish_close = GLOBUS_TRUE;
                op = handle->outstanding_op;
            }
        }
        globus_free(connection_handle);
    }
    else
    {
        while (!globus_fifo_empty(&handle->io_q))
        {
            requestor = (globus_i_xio_mode_e_requestor_t*)
                            globus_fifo_dequeue(&handle->io_q);
            globus_fifo_enqueue(&requestor_q, requestor);
        }
        goto error;
    }
    globus_mutex_unlock(&handle->mutex);
    if (requestor)
    {
        globus_xio_operation_disable_cancel(requestor->op);
        op = requestor->op;
        globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
        globus_xio_driver_finished_read(op, GLOBUS_SUCCESS, 0);
    }
    if (finish)
    {
        while (!globus_fifo_empty(&requestor_q))
        {
            requestor = (globus_i_xio_mode_e_requestor_t*) globus_fifo_dequeue(
                                                                &requestor_q);
            globus_xio_operation_disable_cancel(requestor->op);
            op = requestor->op;
            /*
             * Earlier I had this push_node inside process_eod but then i moved
             * here to avoid a race. process_eod is called with lock held and
             * when lock is released above, sometimes cancel_cb gets called and
             * it tries to access the requestor which is no more. To get rid
             * of that, I moved the push_node to here after disable_cancel.
             * Since requestor is removed from the handle->io_q and put in
             * requestor_q by process_eod function, cancel_cb will not do
             * finished_read on requestor->op. Similar thing is done in read_cb
             * too
             */
            globus_memory_push_node(&handle->requestor_memory, requestor);
            globus_xio_driver_data_descriptor_cntl(
                        op,
                        NULL,
                        GLOBUS_XIO_DD_SET_OFFSET,
                        offset);
            res = GlobusXIOErrorEOF();
            globus_xio_driver_finished_read(op, res, 0);
        }
    }
    globus_fifo_destroy(&requestor_q);
    if (finish_close)
    {
        globus_xio_operation_disable_cancel(op);
        globus_xio_driver_finished_close(op, result);
    }
    GlobusXIOModeEDebugExit();
    return;

error:
    globus_l_xio_mode_e_save_error(handle, result);
    globus_mutex_unlock(&handle->mutex);
    while (!globus_fifo_empty(&requestor_q))
    {
        requestor = (globus_i_xio_mode_e_requestor_t*)
                                globus_fifo_dequeue(&requestor_q);
        globus_xio_operation_disable_cancel(requestor->op);
        op = requestor->op;
        globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
        globus_xio_driver_finished_read(op, result, 0);
    }
    GlobusXIOModeEDebugExitWithError();
    return;
}


static
void
globus_l_xio_mode_e_open_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle;
    globus_l_xio_mode_e_handle_t *      handle;
    globus_xio_operation_t              op;
    globus_result_t                     res = result;
    globus_size_t                       connection_handle_size;
    GlobusXIOName(globus_l_xio_mode_e_open_cb);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *)user_arg;
    globus_xio_operation_disable_cancel(handle->outstanding_op);
    globus_mutex_lock(&handle->mutex);
    op = handle->outstanding_op;
    if (res != GLOBUS_SUCCESS)
    {
        goto error;
    }
    connection_handle_size = sizeof(globus_l_xio_mode_e_connection_handle_t);
    connection_handle = (globus_l_xio_mode_e_connection_handle_t *)
                                    globus_malloc(connection_handle_size);
    if (!connection_handle)
    {
        res = GlobusXIOErrorMemory("connection_handle");
        goto error_connection_handle;
    }
    memset(connection_handle, 0, connection_handle_size);
    handle->state = GLOBUS_XIO_MODE_E_OPEN;
    connection_handle->xio_handle = xio_handle;
    connection_handle->mode_e_handle = handle;
    connection_handle->eod = GLOBUS_FALSE;
    globus_fifo_enqueue(&handle->connection_q, connection_handle);
    globus_mutex_unlock(&handle->mutex);
    globus_xio_driver_finished_open(handle, op, res);
    GlobusXIOModeEDebugExit();
    return;

error_connection_handle:
    globus_xio_register_close(xio_handle, NULL, NULL, NULL);
    /* attr_init is done before register_open in open_new_stream */
error:
    globus_xio_attr_destroy(handle->attr->xio_attr);
    if (--handle->ref_count == 0)
    {
        globus_mutex_unlock(&handle->mutex);
        globus_l_xio_mode_e_handle_destroy(handle);
    }
    else
    {
        globus_l_xio_mode_e_save_error(handle, res);
        globus_mutex_unlock(&handle->mutex);
    }
    globus_xio_driver_finished_open(handle, op, res);
    GlobusXIOModeEDebugExitWithError();
    return;
}


static
void
globus_i_xio_mode_e_open_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle;
    globus_l_xio_mode_e_handle_t *      handle;
    globus_xio_operation_t              write_op;
    globus_result_t                     res;
    globus_bool_t                       finish_write = GLOBUS_FALSE;
    GlobusXIOName(globus_i_xio_mode_e_open_cb);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *)user_arg;
    globus_mutex_lock(&handle->mutex);
    if (result == GLOBUS_SUCCESS)
    {
        globus_size_t connection_handle_size;
        connection_handle_size =
                        sizeof(globus_l_xio_mode_e_connection_handle_t);
        connection_handle = (globus_l_xio_mode_e_connection_handle_t *)
                                        globus_malloc(connection_handle_size);
        if (!connection_handle)
        {
            res = GlobusXIOErrorMemory("connection_handle");
            goto error_connection_handle;
        }
        memset(connection_handle, 0, connection_handle_size);
        connection_handle->xio_handle = xio_handle;
        connection_handle->mode_e_handle = handle;
        connection_handle->eod = GLOBUS_FALSE;
        if (!globus_fifo_empty(&handle->io_q))
        {
            globus_i_xio_mode_e_requestor_t * requestor;
            requestor = (globus_i_xio_mode_e_requestor_t *)
                            globus_fifo_dequeue(&handle->io_q);
            connection_handle->requestor = requestor;
            res = globus_i_xio_mode_e_register_write(connection_handle);
            if (res != GLOBUS_SUCCESS)
            {
                write_op = requestor->op;
                finish_write = GLOBUS_TRUE;
                goto error_register_write;
            }
        }
        else
        {
            globus_fifo_enqueue(&handle->connection_q, connection_handle);
        }
    }
    else
    {
        res = result;
        goto error_open;
    }
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOModeEDebugExit();
    return;

error_register_write:
error_connection_handle:
    globus_xio_register_close(xio_handle, NULL, NULL, NULL);
    /* attr_init is done before register_open in open_new_stream */
    globus_xio_attr_destroy(handle->attr->xio_attr);
error_open:
    globus_l_xio_mode_e_save_error(handle, res);
    globus_mutex_unlock(&handle->mutex);
    if (finish_write)
    {
        globus_xio_driver_data_descriptor_cntl(
                write_op,
                NULL,
                GLOBUS_XIO_DD_SET_OFFSET,
                connection_handle->outstanding_data_offset);
        globus_xio_driver_finished_write(write_op, res, 0);
    }
    GlobusXIOModeEDebugExitWithError();
    return;
}


static
void
globus_i_xio_mode_e_server_open_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_result_t                     res;
    GlobusXIOName(globus_i_xio_mode_e_server_open_cb);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *) user_arg;
    globus_mutex_lock(&handle->mutex);
    if (result == GLOBUS_SUCCESS)
    {
        globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle;

        connection_handle = (globus_l_xio_mode_e_connection_handle_t *)
                                globus_malloc(sizeof(
                                    globus_l_xio_mode_e_connection_handle_t));
        connection_handle->mode_e_handle = handle;
        connection_handle->xio_handle = xio_handle;
        globus_list_insert(&handle->connection_list, connection_handle);
        res = globus_i_xio_mode_e_register_read_header(connection_handle);
        if (res != GLOBUS_SUCCESS)
        {
            goto error;
        }
        ++handle->connection_count;
    }
    else
    {
        res = result;
        goto error;
    }
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOModeEDebugExit();
    return;

error:
    globus_l_xio_mode_e_save_error(handle, res);
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOModeEDebugExitWithError();
    return;
}


static
void
globus_l_xio_mode_e_server_open_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_xio_operation_t              op;
    GlobusXIOName(globus_l_xio_mode_e_server_open_cb);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *) user_arg;
    globus_xio_operation_disable_cancel(handle->outstanding_op);
    globus_mutex_lock(&handle->mutex);
    op = handle->outstanding_op;
    if (result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    handle->state = GLOBUS_XIO_MODE_E_OPEN;
    globus_mutex_unlock(&handle->mutex);
    globus_i_xio_mode_e_server_open_cb(xio_handle, result, user_arg);
    globus_xio_driver_finished_open(handle, op, result);
    GlobusXIOModeEDebugExit();
    return;

error:
    if (--handle->ref_count == 0)
    {
        globus_mutex_unlock(&handle->mutex);
        globus_l_xio_mode_e_handle_destroy(handle);
    }
    else
    {
        globus_l_xio_mode_e_save_error(handle, result);
        globus_mutex_unlock(&handle->mutex);
    }
    globus_xio_driver_finished_open(NULL, op, result);
    GlobusXIOModeEDebugExitWithError();
    return;
}


/* called locked (if handle->state == GLOBUS_XIO_MODE_E_OPEN) */
static
globus_result_t
globus_l_xio_mode_e_open_new_stream(
    globus_l_xio_mode_e_handle_t *      handle,
    globus_xio_callback_t               open_cb)
{
    globus_xio_handle_t                 xio_handle;
    globus_l_xio_mode_e_attr_t *        attr;
    globus_i_xio_mode_e_requestor_t *   requestor;
    globus_xio_operation_t              op;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_mode_e_open_new_stream);

    GlobusXIOModeEDebugEnter();
    attr = handle->attr;
    if(attr->xio_attr == NULL)
    {
        result = globus_xio_attr_init(&attr->xio_attr);
        if (result != GLOBUS_SUCCESS)
        {
            goto error_attr_init;
        }
    }
    result = globus_xio_handle_create(&xio_handle, handle->stack);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_handle_create;
    }
    if (handle->state == GLOBUS_XIO_MODE_E_OPENING)
    {
        requestor = (globus_i_xio_mode_e_requestor_t *)
                        globus_memory_pop_node(&handle->requestor_memory);
        requestor->xio_handle = xio_handle;
        requestor->handle = handle;
        op = handle->outstanding_op;
        if (globus_xio_operation_enable_cancel(
            op, globus_l_xio_mode_e_cancel_cb, requestor))
        {
            result = GlobusXIOErrorCanceled();
            goto error_cancel_enable;
        }
        globus_mutex_lock(&handle->mutex);
        if (globus_xio_operation_is_canceled(handle->outstanding_op))
        {
            result = GlobusXIOErrorCanceled();
            goto error_operation_canceled;
        }
    }
    result = globus_xio_register_open(
                xio_handle,
                handle->cs,
                handle->attr->xio_attr,
                open_cb,
                handle);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_open;
    }
    ++handle->connection_count;
    if (handle->state == GLOBUS_XIO_MODE_E_OPENING)
    {
        globus_mutex_unlock(&handle->mutex);
    }
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_open:
error_operation_canceled:
    if (handle->state == GLOBUS_XIO_MODE_E_OPENING)
    {
        globus_mutex_unlock(&handle->mutex);
        globus_xio_operation_disable_cancel(op);
    }
error_cancel_enable:
    if (handle->state == GLOBUS_XIO_MODE_E_OPENING)
    {
        globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
    }
error_handle_create:
    globus_xio_register_close(xio_handle, NULL, NULL, NULL);
    globus_xio_attr_destroy(handle->attr->xio_attr);
error_attr_init:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_mode_e_server_open(
    globus_l_xio_mode_e_handle_t *      handle,
    globus_xio_operation_t              op)
{
    globus_i_xio_mode_e_requestor_t *   requestor;
    globus_result_t                     result;
    globus_bool_t                       destroy = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_mode_e_server_open);

    GlobusXIOModeEDebugEnter();
    globus_mutex_lock(&handle->mutex);
    handle->outstanding_op = op;
    if (handle->attr->offset_reads)
    {
        result = globus_hashtable_init(
                    &handle->offset_ht,
                    GLOBUS_XIO_MODE_E_OFFSET_HT_SIZE,
                    globus_l_xio_mode_e_hashtable_offset_hash,
                    globus_l_xio_mode_e_hashtable_offset_keyeq);
        if (result != GLOBUS_SUCCESS)
        {
            goto error_hashtable_init;
        }
    }
    requestor = (globus_i_xio_mode_e_requestor_t *)
                    globus_memory_pop_node(&handle->requestor_memory);
    requestor->xio_handle = handle->accepted_handle;
    requestor->handle = handle;
    globus_mutex_unlock(&handle->mutex);
    if (globus_xio_operation_enable_cancel(
        op, globus_l_xio_mode_e_cancel_cb, requestor))
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
    result = globus_xio_register_open(
                handle->accepted_handle,
                NULL,
                handle->attr->xio_attr,
                globus_l_xio_mode_e_server_open_cb,
                handle);
    if (result != GLOBUS_SUCCESS)
    {
        if (handle->ref_count == 0)
        {
            destroy = GLOBUS_TRUE;
        }
        goto error_register_open;
    }
    ++handle->ref_count;
    result = globus_xio_server_register_accept(
            handle->server,
            globus_i_xio_mode_e_server_accept_cb,
            handle);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_register_accept;
    }
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_register_accept:
error_register_open:
error_operation_canceled:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_operation_disable_cancel(op);
    if (destroy)
    {
        globus_l_xio_mode_e_handle_destroy(handle);
    }
error_cancel_enable:
    globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
error_hashtable_init:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_mode_e_open(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_l_xio_mode_e_attr_t *        attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_mode_e_open);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *) driver_link;
    attr = (globus_l_xio_mode_e_attr_t *) driver_attr;
    if (!handle) /* Client */
    {
        result = globus_l_xio_mode_e_handle_create(&handle, attr);
        if (result != GLOBUS_SUCCESS)
        {
            goto error_handle_create;
        }
        handle->state = GLOBUS_XIO_MODE_E_OPENING;
        result = globus_xio_contact_info_to_string(
                                        contact_info, &handle->cs);
        if (result != GLOBUS_SUCCESS)
        {
            goto error_contact_info_to_string;
        }
        handle->outstanding_op = op;
        result = globus_l_xio_mode_e_open_new_stream(
                                handle, globus_l_xio_mode_e_open_cb);
        if (result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                            "globus_l_xio_mode_e_open_new_stream", result);
            goto error_open_new_stream;
        }
    }
    else                /* Server */
    {
        handle->state = GLOBUS_XIO_MODE_E_OPENING;
        result = globus_l_xio_mode_e_server_open(handle, op);
        if (result != GLOBUS_SUCCESS)
        {
            globus_l_xio_mode_e_save_error(handle, result);
            goto error_server_open;
        }
    }
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_open_new_stream:
error_contact_info_to_string:
    globus_l_xio_mode_e_handle_destroy(handle);
error_handle_create:
error_server_open:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
globus_result_t
globus_i_xio_mode_e_register_read_header(
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle)
{
    globus_l_xio_mode_e_header_t *      header;
    globus_size_t                       header_size;
    globus_result_t                     result;
    GlobusXIOName(globus_i_xio_mode_e_register_read_header);

    GlobusXIOModeEDebugEnter();
    globus_l_xio_mode_e_server_connection_handle_init(connection_handle);
    header = (globus_l_xio_mode_e_header_t *)
                    globus_memory_pop_node(
                        &connection_handle->mode_e_handle->header_memory);
    header_size = sizeof(globus_l_xio_mode_e_header_t);
    result = globus_xio_register_read(
        connection_handle->xio_handle,
        (globus_byte_t*)header,
        header_size,
        header_size,
        NULL,               /* data_desc */
        globus_l_xio_mode_e_read_header_cb,
        connection_handle);
    GlobusXIOModeEDebugExit();
    return result;
}


static
void
globus_l_xio_mode_e_read_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 iovec_count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_result_t                     res = result;
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle;
    globus_l_xio_mode_e_handle_t *      handle;
    globus_bool_t                       eof;
    globus_xio_operation_t              op;
    globus_off_t                        offset;
    globus_fifo_t                       requestor_q;
    globus_i_xio_mode_e_requestor_t *   requestor = GLOBUS_NULL;
    GlobusXIOName(globus_l_xio_mode_e_read_cb);

    GlobusXIOModeEDebugEnter();
    connection_handle = (globus_l_xio_mode_e_connection_handle_t *) user_arg;
    op = connection_handle->requestor->op;
    globus_xio_operation_disable_cancel(op);
    handle = connection_handle->mode_e_handle;
    globus_fifo_init(&requestor_q);
    offset = connection_handle->outstanding_data_offset;
    if (connection_handle->iovec_index != -1)
    {
        iovec[connection_handle->iovec_index].iov_len =
                                        connection_handle->iovec_index_len;
        connection_handle->iovec_index = -1;
    }
    globus_mutex_lock(&handle->mutex);
    globus_memory_push_node(
        &handle->requestor_memory, (void*)connection_handle->requestor);
    if (result == GLOBUS_SUCCESS)
    {
        connection_handle->outstanding_data_len -= nbytes;
        if (connection_handle->outstanding_data_len > 0)
        {
            connection_handle->outstanding_data_offset += nbytes;
            requestor = globus_l_xio_mode_e_process_outstanding_data(
                                                        connection_handle);
        }
        else if (connection_handle->eod)
        {
            eof = globus_l_xio_mode_e_process_eod(
                                            connection_handle, &requestor_q);
            if (eof)
            {
                globus_xio_driver_set_eof_received(op);
                res = GlobusXIOErrorEOF();
                if (handle->state == GLOBUS_XIO_MODE_E_OPEN)
                {
                    handle->state = GLOBUS_XIO_MODE_E_EOF_DELIVERED;
                }
            }
        }
        else
        {
            result = globus_i_xio_mode_e_register_read_header(
                                                        connection_handle);
            if (result != GLOBUS_SUCCESS)
            {
                goto error;
            }
        }
    }
    else
    {
        while (!globus_fifo_empty(&handle->io_q))
        {
            requestor = (globus_i_xio_mode_e_requestor_t*)
                            globus_fifo_dequeue(&handle->io_q);
            globus_fifo_enqueue(&requestor_q, requestor);
        }
        goto error;
    }
    globus_mutex_unlock(&handle->mutex);
    globus_xio_driver_data_descriptor_cntl(
                        op,
                        NULL,
                        GLOBUS_XIO_DD_SET_OFFSET,
                        offset);
    globus_xio_driver_finished_read(op, res, nbytes);
    if (requestor)
    {
        globus_xio_operation_disable_cancel(requestor->op);
        op = requestor->op;
        globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
        globus_xio_driver_finished_read(op, GLOBUS_SUCCESS, 0);
    }
    while (!globus_fifo_empty(&requestor_q))
    {
        requestor = (globus_i_xio_mode_e_requestor_t*)
                                globus_fifo_dequeue(&requestor_q);
        globus_xio_operation_disable_cancel(requestor->op);
        op = requestor->op;
        globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
        globus_xio_driver_finished_read(op, res, 0);
    }
    globus_fifo_destroy(&requestor_q);
    GlobusXIOModeEDebugExit();
    return;

error:
    globus_l_xio_mode_e_save_error(handle, result);
    globus_mutex_unlock(&handle->mutex);
    while (!globus_fifo_empty(&requestor_q))
    {
        requestor = (globus_i_xio_mode_e_requestor_t*)
                                globus_fifo_dequeue(&requestor_q);
        globus_xio_operation_disable_cancel(requestor->op);
        op = requestor->op;
        globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
        globus_xio_driver_finished_read(op, result, 0);
    }
    GlobusXIOModeEDebugExitWithError();
    return;
}


/* called locked */
static
globus_result_t
globus_i_xio_mode_e_register_read(
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle)
{
    globus_size_t                       iovec_len;
    globus_result_t                     result;
    globus_xio_iovec_t *                iovec;
    int                                 iovec_count;
    GlobusXIOName(globus_i_xio_mode_e_register_read);

    GlobusXIOModeEDebugEnter();
    iovec = connection_handle->requestor->iovec;
    iovec_count = connection_handle->requestor->iovec_count;
    GlobusXIOUtilIovTotalLength(iovec_len, iovec, iovec_count);
    /*
     * Upto iovec_len amount of data would be read from the channel. If
     * iovec_len > outstanding_data_len, this read might possibly get the next
     * header that has arrived on this channel. So I modify iovec (and later
     * restore in the read_cb) such that the amount of data read would be
     * min(outstanding_data_len, iovec_len)
     */
    if (connection_handle->outstanding_data_len < iovec_len)
    {
        globus_size_t                   size = 0;
        int                             i;
        iovec_len = connection_handle->outstanding_data_len;
        for (i = 0; i < iovec_count; i++)
        {
            size += iovec[i].iov_len;
            if (size > iovec_len)
            {
                connection_handle->iovec_index = i;
                iovec_count = i + 1;
                connection_handle->iovec_index_len = iovec[i].iov_len;
                iovec[i].iov_len -= (size - iovec_len);
                break;
            }
        }
    }
    result = globus_xio_register_readv(
                connection_handle->xio_handle,
                (globus_xio_iovec_t*)iovec,
                iovec_count,
                iovec_len,
                NULL,
                globus_l_xio_mode_e_read_cb,
                connection_handle);
    GlobusXIOModeEDebugExit();
    return result;
}


/* called locked */
static
void
globus_l_xio_mode_e_reset_connections(
    globus_l_xio_mode_e_handle_t *      handle)
{
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_mode_e_reset_connections);
    GlobusXIOModeEDebugEnter();
    handle->eod_count = -1;
    handle->eods_received = 0;
    while (!globus_fifo_empty(&handle->eod_q))
    {
        connection_handle = (globus_l_xio_mode_e_connection_handle_t *)
                        globus_fifo_dequeue(&handle->eod_q);
        result = globus_i_xio_mode_e_register_read_header(connection_handle);
        if (result != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }
    handle->state = GLOBUS_XIO_MODE_E_OPEN;
    GlobusXIOModeEDebugExit();
    return;

error:
    globus_l_xio_mode_e_save_error(handle, result);
    GlobusXIOModeEDebugExitWithError();
    return;
}


static
globus_result_t
globus_l_xio_mode_e_read(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t*           iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_i_xio_mode_e_requestor_t *   requestor;
    globus_l_xio_mode_e_attr_t *        dd = GLOBUS_NULL;
    globus_result_t                     result;
    globus_size_t                       wait_for;
    globus_bool_t                       finish = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_mode_e_read);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *) driver_specific_handle;
    wait_for = globus_xio_operation_get_wait_for(op);
    if (wait_for > 1)
    {
        result = GlobusXIOErrorParameter("Waitforbytes");
        goto error_wait_for;
    }
    else if (wait_for == 0 && !handle->attr->offset_reads)
    {
        result = GlobusXIOErrorParameter(
                "Waitforbytes cant be zero. Offset reads not set on attr");
        goto error_wait_for;
    }
    if (handle->attr->offset_reads)
    {
        dd = (globus_l_xio_mode_e_attr_t *)
                globus_xio_operation_get_data_descriptor(op, GLOBUS_FALSE);
        if (!dd)
        {
            result = GlobusXIOErrorParameter("data_descriptor");
            goto error_dd;
        }
    }
    requestor = (globus_i_xio_mode_e_requestor_t *)
                globus_memory_pop_node(&handle->requestor_memory);
    requestor->op = op;
    requestor->iovec = (globus_xio_iovec_t*)iovec;
    requestor->iovec_count = iovec_count;
    requestor->dd = dd;
    requestor->handle = handle;
    requestor->xio_handle = GLOBUS_NULL;
    if (globus_xio_operation_enable_cancel(
        op, globus_l_xio_mode_e_cancel_cb, requestor))
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
    switch (handle->state)
    {
        case GLOBUS_XIO_MODE_E_EOF_RECEIVED:
            globus_xio_driver_set_eof_received(op);
            handle->state = GLOBUS_XIO_MODE_E_EOF_DELIVERED;
            globus_memory_push_node(
                        &handle->requestor_memory, (void*)requestor);
            result = GlobusXIOErrorEOF();
            finish = GLOBUS_TRUE;
            break;
        case GLOBUS_XIO_MODE_E_EOF_DELIVERED:
            if (globus_xio_driver_eof_received(op) ||
                                        handle->connection_count == 0)
            {
                globus_memory_push_node(
                        &handle->requestor_memory, (void*)requestor);
                result = GlobusXIOErrorEOF();
                finish = GLOBUS_TRUE;
                break;
            }
            else
            {
                globus_l_xio_mode_e_reset_connections(handle);
                /*
                 * connection_q will be empty at this point. I let this
                 * fall through to enqueue the request in the io_q
                 */
            }
            /* fall through */
        case GLOBUS_XIO_MODE_E_OPEN:
            if (globus_fifo_empty(&handle->connection_q))
            {
                globus_fifo_enqueue(&handle->io_q, requestor);
            }
            else
            {
                globus_l_xio_mode_e_connection_handle_t *
                                                connection_handle;
                if (wait_for == 0)
                {
                    globus_memory_push_node(
                                &handle->requestor_memory, (void*)requestor);
                    connection_handle =
                                (globus_l_xio_mode_e_connection_handle_t*)
                                    globus_fifo_dequeue(&handle->connection_q);
                    globus_hashtable_insert(
                        &handle->offset_ht,
                        (void *)&connection_handle->outstanding_data_offset,
                        (void *)connection_handle);
                    dd->offset = connection_handle->outstanding_data_offset;
                    result = GLOBUS_SUCCESS;
                    finish = GLOBUS_TRUE;
                }
                else
                {
                    if (handle->attr->offset_reads)
                    {
                        connection_handle =
                                (globus_l_xio_mode_e_connection_handle_t*)
                                        globus_hashtable_remove(
                                            &handle->offset_ht, &dd->offset);
                        if (!connection_handle)
                        {
                            result = GlobusXIOErrorParameter("Invalid offset");
                            goto error_offset;
                        }
                    }
                    else
                    {
                        connection_handle =
                            (globus_l_xio_mode_e_connection_handle_t*)
                                globus_fifo_dequeue(&handle->connection_q);
                    }
                    connection_handle->requestor = requestor;
                    requestor->xio_handle = connection_handle->xio_handle;
                    globus_i_xio_mode_e_register_read(connection_handle);
                }
            }
            break;
        case GLOBUS_XIO_MODE_E_ERROR:
            result = globus_error_put(globus_object_copy(handle->error));
            goto error_invalid_state;
        default:
            result = GlobusXIOErrorInvalidState(handle->state);
            goto error_invalid_state;
    }
    globus_mutex_unlock(&handle->mutex);
    if (finish)
    {
        globus_xio_operation_disable_cancel(op);
        globus_xio_driver_finished_read(op, result, 0);
    }
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_invalid_state:
error_offset:
error_operation_canceled:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_operation_disable_cancel(op);
error_cancel_enable:
    globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
error_dd:
error_wait_for:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
void
globus_l_xio_mode_e_write_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_result_t                     requestor_result;
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle;
    globus_l_xio_mode_e_handle_t *      handle;
    globus_xio_operation_t              op;
    globus_xio_operation_t              requestor_op;
    globus_off_t                        offset;
    globus_off_t                        requestor_offset;
    globus_bool_t                       finish = GLOBUS_TRUE;
    globus_bool_t                       finish_next = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_mode_e_write_cb);

    GlobusXIOModeEDebugEnter();
    connection_handle = (globus_l_xio_mode_e_connection_handle_t *) user_arg;
    handle = connection_handle->mode_e_handle;
    op = connection_handle->requestor->op;
    /* push_node has its own lock. so need for lock before this call */
    globus_memory_push_node(
        &handle->requestor_memory, (void*)connection_handle->requestor);
    offset = connection_handle->outstanding_data_offset;
    globus_mutex_lock(&handle->mutex);
    if (result != GLOBUS_SUCCESS)
    {
        requestor_result = result;
        goto error;
    }
    if (!globus_fifo_empty(&handle->io_q))
    {
        globus_i_xio_mode_e_requestor_t * requestor;
        requestor = (globus_i_xio_mode_e_requestor_t *)
                        globus_fifo_dequeue(&handle->io_q);
        connection_handle->requestor = requestor;
        requestor_result = globus_i_xio_mode_e_register_write(
                                                        connection_handle);
        if (requestor_result != GLOBUS_SUCCESS)
        {
            requestor_op = requestor->op;
            requestor_offset = connection_handle->outstanding_data_offset;
            finish_next = GLOBUS_TRUE;
            globus_memory_push_node(
                        &handle->requestor_memory, (void*)requestor);
            goto error_register_write;
        }
    }
    else
    {
        if (handle->state == GLOBUS_XIO_MODE_E_SENDING_EOD)
        {
            globus_byte_t               descriptor;
            /*
             * I'll get this cb with eod_sent == TRUE for one connection alone
             * and I send EOF (if need be) on this connection alone. If I don't
             * need to send EOF and eods_sent == connection_count, I need to
             * finish the write (that had SEND_EOD set on dd)
             */
            if (!connection_handle->eod)
            {
                descriptor = GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOD;
                requestor_result = globus_l_xio_mode_e_register_eod(
                                              connection_handle, descriptor);
                if (requestor_result != GLOBUS_SUCCESS)
                {
                    goto error_register_eod;
                }
            }
            else
            {
                connection_handle->eod = GLOBUS_FALSE;
                if (handle->eod_count > -1)
                {
                    descriptor = GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOF;
                    requestor_result = globus_l_xio_mode_e_register_eod(
                                              connection_handle, descriptor);
                    if (requestor_result != GLOBUS_SUCCESS)
                    {
                        goto error_register_eod;
                    }
                }
                else
                {
                    globus_fifo_enqueue(
                                &handle->connection_q, connection_handle);
                    if (handle->eods_sent < handle->connection_count)
                    {
                        finish = GLOBUS_FALSE;
                    }
                }
            }
        }
        else
        {
            globus_fifo_enqueue(&handle->connection_q, connection_handle);
        }
    }
    globus_mutex_unlock(&handle->mutex);
    if (finish)
    {
        globus_xio_driver_data_descriptor_cntl(
                            op,
                            NULL,
                            GLOBUS_XIO_DD_SET_OFFSET,
                            offset);
        globus_xio_driver_finished_write(op, result, nbytes);
    }
    GlobusXIOModeEDebugExit();
    return;

error_register_eod:
error_register_write:
error:
    globus_fifo_enqueue(&handle->connection_q, connection_handle);
    globus_l_xio_mode_e_save_error(handle, result);
    globus_mutex_unlock(&handle->mutex);
    if (finish_next)
    {
        globus_xio_driver_data_descriptor_cntl(
                    requestor_op,
                    NULL,
                    GLOBUS_XIO_DD_SET_OFFSET,
                    requestor_offset);
        globus_xio_driver_finished_write(requestor_op, requestor_result, 0);
    }
    globus_xio_driver_data_descriptor_cntl(
                        op,
                        NULL,
                        GLOBUS_XIO_DD_SET_OFFSET,
                        offset);
    globus_xio_driver_finished_write(op, result, nbytes);
    GlobusXIOModeEDebugExitWithError();
    return;
}


static
void
globus_l_xio_mode_e_write_header_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle;
    globus_l_xio_mode_e_handle_t *      handle;
    globus_l_xio_mode_e_header_t *      header;
    const globus_xio_iovec_t *          iovec;
    int                                 iovec_count;
    globus_size_t                       iovec_len;
    globus_xio_operation_t              op;
    globus_off_t                        offset;
    globus_bool_t                       finish = GLOBUS_FALSE;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_mode_e_write_header_cb);

    GlobusXIOModeEDebugEnter();
    connection_handle = (globus_l_xio_mode_e_connection_handle_t *) user_arg;
    handle = connection_handle->mode_e_handle;
    header = (globus_l_xio_mode_e_header_t *) buffer;
    globus_xio_operation_disable_cancel(connection_handle->requestor->op);
    globus_mutex_lock(&handle->mutex);
    if (result == GLOBUS_SUCCESS)
    {
        /*
         * if handle->eod_count != -1, then register_eod will be called (either
         * in here or in write_cb) to send EOF and eods_sent will be
         * incremented in eod_cb
         */
        if (header->descriptor & GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOD &&
            handle->eod_count == -1)
        {
            ++handle->eods_sent;
        }
        globus_memory_push_node(&handle->header_memory, (void*)header);
        if (connection_handle->requestor->iovec)
        {
            iovec = connection_handle->requestor->iovec;
            iovec_count = connection_handle->requestor->iovec_count;
            GlobusXIOUtilIovTotalLength(iovec_len, iovec, iovec_count);
            res = globus_xio_register_writev(
                        connection_handle->xio_handle,
                        (globus_xio_iovec_t*)iovec,
                        iovec_count,
                        iovec_len,
                        GLOBUS_NULL,
                        globus_l_xio_mode_e_write_cb,
                        connection_handle);
            if (res != GLOBUS_SUCCESS)
            {
                goto error;
            }
        }
        else if (handle->eod_count > -1)
        {
            globus_byte_t               descriptor;
            /*
             * handle->iovec can be NULL only when the user sets SEND_EOD
             * on the dd. So I just wrote EOD on this channel and now i check
             * to see if i need to send EOF
             */
            descriptor = GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOF;
            res = globus_l_xio_mode_e_register_eod(
                                connection_handle, descriptor);
            if (res != GLOBUS_SUCCESS)
            {
                goto error;
            }
        }
        else
        {
            globus_fifo_enqueue(&handle->connection_q, connection_handle);
            if (handle->eods_sent == handle->connection_count)
            {
                /*
                 * user is allowed to do a write with buffer = NULL and
                 * SEND_EOD set on dd
                 */
                handle->state = GLOBUS_XIO_MODE_E_OPEN;
                handle->eod_count = -1;
                handle->attr->eod_count = -1;
                handle->offset = 0;
                finish = GLOBUS_TRUE;
                /*
                 * handle->eod_offset also has same value. if i have to finish
                 * in eod_cb, connection_handle->outstanding_data_offset cant
                 * be used coz i don't know which connection_handle is
                 * associated with the user_write (that had SEND_EOD set).
                 */
                offset = connection_handle->outstanding_data_offset;
                /* the op is stored in handle->outstanding_op too */
                op = connection_handle->requestor->op;
                globus_memory_push_node(
                    &handle->requestor_memory,
                    (void*)connection_handle->requestor);
            }
        }
    }
    else
    {
        globus_memory_push_node(&handle->header_memory, (void*)header);
        res = result;
        goto error;
    }
    globus_mutex_unlock(&handle->mutex);
    if (finish)
    {
        globus_xio_driver_data_descriptor_cntl(
                    op,
                    NULL,
                    GLOBUS_XIO_DD_SET_OFFSET,
                    offset);
        globus_xio_driver_finished_write(op, result, 0);
    }
    GlobusXIOModeEDebugExit();
    return;

error:
    globus_l_xio_mode_e_save_error(handle, res);
    op = connection_handle->requestor->op;
    globus_memory_push_node(
        &handle->requestor_memory, (void*)connection_handle->requestor);
    globus_fifo_enqueue(&handle->connection_q, connection_handle);
    globus_mutex_unlock(&handle->mutex);
    globus_xio_driver_finished_write(op, res, 0);
    GlobusXIOModeEDebugExitWithError();
    return;
}


/* called locked */
static
globus_result_t
globus_i_xio_mode_e_register_write(
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_off_t                        size;
    globus_off_t                        offset;
    globus_l_xio_mode_e_header_t *      header;
    globus_size_t                       header_size;
    globus_result_t                     result;
    GlobusXIOName(globus_i_xio_mode_e_register_write);

    GlobusXIOModeEDebugEnter();
    handle = connection_handle->mode_e_handle;
    header = (globus_l_xio_mode_e_header_t *) globus_memory_pop_node(
                                                    &handle->header_memory);
    header_size = sizeof(globus_l_xio_mode_e_header_t);
    memset(header, 0, header_size);
    GlobusXIOUtilIovTotalLength(
                size,
                connection_handle->requestor->iovec,
                connection_handle->requestor->iovec_count);
    globus_i_xio_mode_e_header_encode(header->count, size);
    result = globus_xio_driver_data_descriptor_cntl(
                connection_handle->requestor->op,
                NULL,
                GLOBUS_XIO_DD_GET_OFFSET,
                &offset);
    if (result != GLOBUS_SUCCESS || offset == -1)
    {
        offset = handle->offset;
    }
    if (handle->state == GLOBUS_XIO_MODE_E_SENDING_EOD &&
        globus_fifo_empty(&handle->io_q))
    {
        header->descriptor = GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOD;
        handle->eod_offset = offset;
    }
    globus_i_xio_mode_e_header_encode(header->offset, offset);
    connection_handle->outstanding_data_offset = offset;
    offset += size;
    if (offset > handle->offset)
    {
        handle->offset = offset;
    }
    result = globus_xio_register_write(
                connection_handle->xio_handle,
                (globus_byte_t*)header,
                header_size,
                header_size,
                GLOBUS_NULL,
                globus_l_xio_mode_e_write_header_cb,
                connection_handle);
    if (result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    if (header->descriptor & GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOD)
    {
        connection_handle->eod = GLOBUS_TRUE;
    }
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_mode_e_write(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_i_xio_mode_e_requestor_t *   requestor;
    globus_l_xio_mode_e_attr_t *        dd;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_mode_e_write);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *) driver_specific_handle;
    /*
     * Mode E is unidirectional. Server can only read and client can only write
     */
    globus_assert(handle->server == GLOBUS_NULL);
    dd = (globus_l_xio_mode_e_attr_t *)
            globus_xio_operation_get_data_descriptor(op, GLOBUS_FALSE);
    requestor = (globus_i_xio_mode_e_requestor_t *)
                globus_memory_pop_node(&handle->requestor_memory);
    requestor->op = op;
    requestor->iovec = (globus_xio_iovec_t*)iovec;
    requestor->iovec_count = iovec_count;
    requestor->dd = dd;
    requestor->handle = handle;
    requestor->xio_handle = GLOBUS_NULL;
    if (globus_xio_operation_enable_cancel(
        op, globus_l_xio_mode_e_cancel_cb, requestor))
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
    switch (handle->state)
    {
        case GLOBUS_XIO_MODE_E_OPEN:
            /*
             * I reset eods_sent here (and not right after i send all the eods)
             * coz i use 'eods_sent == connection_count' in close to check if
             * eods have been sent or not
             */
            if (handle->eods_sent == handle->connection_count)
            {
                handle->eods_sent = 0;
            }
            if (handle->eof_sent)
            {
                handle->eof_sent = GLOBUS_FALSE;
            }
            if (dd && dd->send_eod)
            {
                handle->state = GLOBUS_XIO_MODE_E_SENDING_EOD;
                handle->outstanding_op = op;
                if (!handle->attr->manual_eodc)
                {
                    handle->eod_count = handle->connection_count;
                }
                else if (dd->eod_count > -1)
                {
                    handle->eod_count = dd->eod_count;
                }
            }
            if (!globus_fifo_empty(&handle->connection_q))
            {
                globus_l_xio_mode_e_connection_handle_t *
                                            connection_handle;
                connection_handle = (globus_l_xio_mode_e_connection_handle_t *)
                                    globus_fifo_dequeue(&handle->connection_q);
                connection_handle->requestor = requestor;
                requestor->xio_handle = connection_handle->xio_handle;
                result = globus_i_xio_mode_e_register_write(connection_handle);
                if (result != GLOBUS_SUCCESS)
                {
                    goto error_register_write;
                }
                if (handle->state == GLOBUS_XIO_MODE_E_SENDING_EOD)
                {
                    globus_byte_t               descriptor;
                    descriptor = GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOD;
                    while (!globus_fifo_empty(&handle->connection_q))
                    {
                        connection_handle =
                                    (globus_l_xio_mode_e_connection_handle_t *)
                                    globus_fifo_dequeue(&handle->connection_q);
                        globus_l_xio_mode_e_register_eod(
                                          connection_handle, descriptor);
                    }
                }
            }
            else
            {
                if (handle->connection_count <
                    handle->attr->max_connection_count)
                {
                    result = globus_l_xio_mode_e_open_new_stream(
                                handle, globus_i_xio_mode_e_open_cb);
                    if (result != GLOBUS_SUCCESS)
                    {
                        result = GlobusXIOErrorWrapFailed(
                            "globus_l_xio_mode_e_open_new_stream", result);
                        goto error_open_new_stream;
                    }
                }
                globus_fifo_enqueue(&handle->io_q, requestor);
            }
            break;
        case GLOBUS_XIO_MODE_E_ERROR:
            result = globus_error_put(globus_object_copy(handle->error));
            goto error_invalid_state;
        default:
            result = GlobusXIOErrorInvalidState(handle->state);
            goto error_invalid_state;
    }
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_invalid_state:
error_open_new_stream:
error_register_write:
error_operation_canceled:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_operation_disable_cancel(op);
error_cancel_enable:
    globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
void
globus_l_xio_mode_e_close_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_bool_t                       finish = GLOBUS_FALSE;
    globus_bool_t                       destroy = GLOBUS_FALSE;
    globus_xio_operation_t              op;
    GlobusXIOName(globus_l_xio_mode_e_close_cb);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *)user_arg;
    globus_mutex_lock(&handle->mutex);
    if (result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    if(globus_error_match(
                globus_error_peek(result),
                GLOBUS_XIO_MODULE,
                GLOBUS_XIO_ERROR_CANCELED))
    {
        ++handle->close_count;
        if (handle->close_count == handle->connection_count)
        {
            finish = GLOBUS_TRUE;
            handle->state = GLOBUS_XIO_MODE_E_NONE;
            op = handle->outstanding_op;
        }
    }
    else
    {
        if (!globus_list_empty(handle->close_list))
        {
            globus_list_remove(
                &handle->close_list,
                globus_list_search(handle->close_list, xio_handle));
        }
    }
    if (--handle->connection_count == 0)
    {
        switch (handle->state)
        {
            case GLOBUS_XIO_MODE_E_CLOSING:
                finish = GLOBUS_TRUE;
                handle->state = GLOBUS_XIO_MODE_E_NONE;
                op = handle->outstanding_op;
                if (--handle->ref_count == 0)
                {
                    destroy = GLOBUS_TRUE;
                }
                break;
            case GLOBUS_XIO_MODE_E_EOF_RECEIVED:
            case GLOBUS_XIO_MODE_E_EOF_DELIVERED:
                break;
            default:
                globus_assert(0 && "Unexpected state in mode_e_close_cb");
        }
    }
    globus_mutex_unlock(&handle->mutex);
    if (finish)
    {
        globus_xio_operation_disable_cancel(op);
        if (destroy)
        {
            globus_l_xio_mode_e_handle_destroy(handle);
        }
        globus_xio_driver_finished_close(op, result);
    }
    GlobusXIOModeEDebugExit();
    return;

error:
    globus_l_xio_mode_e_save_error(handle, result);
    globus_mutex_unlock(&handle->mutex);
    GlobusXIOModeEDebugExitWithError();
    return;
}


static
void
globus_l_xio_mode_e_write_eod_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle;
    globus_l_xio_mode_e_handle_t *      handle;
    globus_l_xio_mode_e_header_t *      header;
    globus_xio_operation_t              op;
    globus_off_t                        offset;
    globus_bool_t                       finish = GLOBUS_FALSE;
    globus_bool_t                       finish_close = GLOBUS_FALSE;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_mode_e_write_eod_cb);

    GlobusXIOModeEDebugEnter();
    connection_handle = (globus_l_xio_mode_e_connection_handle_t *) user_arg;
    header = (globus_l_xio_mode_e_header_t *) buffer;
    handle = connection_handle->mode_e_handle;
    globus_mutex_lock(&handle->mutex);
    if (result != GLOBUS_SUCCESS)
    {
        res = result;
        goto error;
    }
    ++handle->eods_sent;
    /*
     * If CLOSE is set on header, then it implies that user has called
     * close and I allow users to cancel close until all the channels are
     * closed
     */
    if (header->descriptor & GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_CLOSE)
    {
        /* I don't register_close, if user has canceled the close already */
        if (!globus_error_match(
                globus_error_peek(result),
                GLOBUS_XIO_MODULE,
                GLOBUS_XIO_ERROR_CANCELED))
        {
            res = globus_xio_register_close(
                    connection_handle->xio_handle,
                    handle->attr->xio_attr,
                    globus_l_xio_mode_e_close_cb,
                    handle);
            if (res != GLOBUS_SUCCESS)
            {
                goto error_close;
            }
            globus_list_insert(
                        &handle->close_list, connection_handle->xio_handle);
            /*
             * If it was canceled, then the xio_handle would have been removed
             * from the eod_list before the canceling the write eod. So I do
             * not have to this removal in the else below.
             */
            globus_list_remove(
                &handle->eod_list,
                globus_list_search(
                        handle->eod_list, connection_handle->xio_handle));
        }
        else
        {
            ++handle->close_count;
            if (handle->close_count == handle->connection_count)
            {
                finish_close = GLOBUS_TRUE;
                op = handle->outstanding_op;
            }
        }
        globus_free(connection_handle);
    }
    else
    {
        /*
         * getting here implies that register_eod (associated with this cb) is
         * called in response to the dd 'SEND_EOD' on write
         */
        if (header->descriptor & GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOF)
        {
            handle->eof_sent = GLOBUS_TRUE;
        }
        globus_fifo_enqueue(&handle->connection_q, connection_handle);
        if (handle->eods_sent == handle->connection_count)
        {
            handle->state = GLOBUS_XIO_MODE_E_OPEN;
            handle->eod_count = -1;
            handle->attr->eod_count = -1;
            handle->offset = 0;
            op = handle->outstanding_op;
            offset = handle->eod_offset;
            finish = GLOBUS_TRUE;
        }
        if (!globus_error_match(
                globus_error_peek(result),
                GLOBUS_XIO_MODULE,
                GLOBUS_XIO_ERROR_CANCELED))
        {
            globus_list_remove(
                &handle->eod_list,
                globus_list_search(
                        handle->eod_list, connection_handle->xio_handle));
        }

    }
    globus_memory_push_node(&handle->header_memory, (void*)header);
    globus_mutex_unlock(&handle->mutex);
    if (finish)
    {
        globus_xio_operation_disable_cancel(op);
        globus_xio_driver_data_descriptor_cntl(
                    op,
                    NULL,
                    GLOBUS_XIO_DD_SET_OFFSET,
                    offset);
        globus_xio_driver_finished_write(op, result, 0);
    }
    if (finish_close)
    {
        globus_xio_driver_finished_close(op, result);
    }
    GlobusXIOModeEDebugExit();
    return;

error_close:
error:
    globus_l_xio_mode_e_save_error(handle, res);
    globus_mutex_unlock(&handle->mutex);
    globus_memory_push_node(&handle->header_memory, (void*)header);
    GlobusXIOModeEDebugExitWithError();
    return;
}


/* called locked */
static
globus_result_t
globus_l_xio_mode_e_register_eod(
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle,
    globus_byte_t                       descriptor)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_l_xio_mode_e_header_t *      header;
    globus_size_t                       header_size;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_mode_e_register_eod);

    GlobusXIOModeEDebugEnter();
    handle = connection_handle->mode_e_handle;
    header = (globus_l_xio_mode_e_header_t *)
                    globus_memory_pop_node(&handle->header_memory);
    header_size = sizeof(globus_l_xio_mode_e_header_t);
    memset(header, 0, header_size);
    header->descriptor = descriptor;
    if (header->descriptor & GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOF)
    {
        globus_i_xio_mode_e_header_encode(
                        header->offset, handle->eod_count);
    }
    result = globus_xio_register_write(
        connection_handle->xio_handle,
        (globus_byte_t*)header,
        header_size,
        header_size,
        GLOBUS_NULL,
        globus_l_xio_mode_e_write_eod_cb,
        connection_handle);
    if (result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    /* this is eod_list is for canceling the eod writes */
    globus_list_insert(&handle->eod_list, connection_handle->xio_handle);
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error:
    globus_memory_push_node(&handle->header_memory, (void*)header);
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
void
globus_l_xio_mode_e_close_connections(
    globus_l_xio_mode_e_handle_t *      handle)
{
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle;
    globus_l_xio_mode_e_connection_handle_t *
                                        idle_connection_handle;
    GlobusXIOName(globus_l_xio_mode_e_close_connections);

    GlobusXIOModeEDebugEnter();
    /*
     * handle->connection_list contains all the connections whereas the q's
     * (connection_q, eod_q contain only those connections that are free (idle)
     */
    while (!globus_list_empty(handle->connection_list))
    {
        connection_handle = globus_list_remove(
                            &handle->connection_list, handle->connection_list);
        idle_connection_handle = (globus_l_xio_mode_e_connection_handle_t *)
               globus_fifo_remove(&handle->connection_q, connection_handle);
        if (!idle_connection_handle)
        {
            idle_connection_handle =
                    (globus_l_xio_mode_e_connection_handle_t *)
                       globus_fifo_remove(&handle->eod_q, connection_handle);

        }
        if (idle_connection_handle)
        {
            /* connection_handle and idle_connection_handle are same here */
            globus_xio_register_close(
                connection_handle->xio_handle,
                NULL,
                globus_l_xio_mode_e_close_cb,
                connection_handle->mode_e_handle);
            globus_list_insert(
                        &handle->close_list, connection_handle->xio_handle);
            globus_free(connection_handle);
        }
        else
        {
            globus_xio_handle_cancel_operations(
                    connection_handle->xio_handle, GLOBUS_XIO_CANCEL_READ);
        }
    }
    GlobusXIOModeEDebugExit();
}


static
globus_result_t
globus_l_xio_mode_e_close(
    void *                              driver_specific_handle,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_l_xio_mode_e_connection_handle_t *
                                        connection_handle;
    globus_i_xio_mode_e_requestor_t *   requestor;
    globus_l_xio_mode_e_attr_t *        attr;
    globus_result_t                     result;
    globus_bool_t                       finish = GLOBUS_FALSE;
    globus_bool_t                       destroy = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_mode_e_close);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *) driver_specific_handle;
    attr = (globus_l_xio_mode_e_attr_t *)
                driver_attr ? driver_attr : handle->attr;
    requestor = (globus_i_xio_mode_e_requestor_t *)
                    globus_memory_pop_node(&handle->requestor_memory);
    requestor->handle = handle;
    requestor->op = op;
    if (globus_xio_operation_enable_cancel(
        op, globus_l_xio_mode_e_cancel_cb, requestor))
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
    handle->outstanding_op = op;
    if (!handle->server)
    {
        globus_byte_t                   descriptor;
        descriptor = GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_CLOSE;
        handle->state = GLOBUS_XIO_MODE_E_CLOSING;

        /* this implies eods are not sent yet */
        if (handle->eods_sent != handle->connection_count)
        {
            descriptor |= GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOD;
        }
        if (!handle->attr->manual_eodc)
        {
            handle->eod_count = handle->connection_count;
        }
        else if (attr->eod_count > -1)
        {
            handle->eod_count = attr->eod_count;
        }
        if (!handle->eof_sent && handle->eod_count > -1)
        {
            connection_handle = (globus_l_xio_mode_e_connection_handle_t *)
                                globus_fifo_dequeue(&handle->connection_q);
            descriptor |= GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOF;
            result = globus_l_xio_mode_e_register_eod(
                                connection_handle, descriptor);
            if (result != GLOBUS_SUCCESS)
            {
                goto error_register_eod;
            }
            descriptor &= ~GLOBUS_XIO_MODE_E_DATA_DESCRIPTOR_EOF;
        }
        while (!globus_fifo_empty(&handle->connection_q))
        {
            connection_handle = (globus_l_xio_mode_e_connection_handle_t *)
                                    globus_fifo_dequeue(&handle->connection_q);
            result = globus_l_xio_mode_e_register_eod(
                                connection_handle, descriptor);
            if (result != GLOBUS_SUCCESS)
            {
                goto error_register_eod;
            }
        }
    }
    else
    {
        if (handle->connection_count == 0)
        {
            finish = GLOBUS_TRUE;
            if (--handle->ref_count == 0)
            {
                destroy = GLOBUS_TRUE;
            }
        }
        else
        {
            globus_l_xio_mode_e_close_connections(handle);
            handle->state = GLOBUS_XIO_MODE_E_CLOSING;
        }
    }
    globus_mutex_unlock(&handle->mutex);
    if (finish)
    {
        if (destroy)
        {
            globus_l_xio_mode_e_handle_destroy(handle);
        }
        globus_xio_driver_finished_close(op, GLOBUS_SUCCESS);
    }
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_register_eod:
error_operation_canceled:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_operation_disable_cancel(op);
error_cancel_enable:
    globus_memory_push_node(&handle->requestor_memory, (void*)requestor);
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_mode_e_cntl(
    void  *                             driver_specific_handle,
    int                                 cmd,
    va_list                             ap)
{
    globus_l_xio_mode_e_handle_t *      handle;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_mode_e_cntl);

    GlobusXIOModeEDebugEnter();
    handle = (globus_l_xio_mode_e_handle_t *) driver_specific_handle;
    switch(cmd)
    {
        case GLOBUS_XIO_MODE_E_SET_EODC:
            if (handle->attr->manual_eodc)
            {
                if (handle->state == GLOBUS_XIO_MODE_E_OPEN)
                {
                    handle->eod_count = va_arg(ap, int);
                }
                else
                {
                    result = GlobusXIOErrorInvalidState(handle->state);
                    goto error;
                }
            }
            else
            {
                result = GlobusXIOErrorInvalidCommand(cmd);
            }
            break;
        case GLOBUS_XIO_MODE_E_ERROR:
            result = globus_error_put(globus_object_copy(handle->error));
            goto error;
        default:
            result = GlobusXIOErrorInvalidCommand(cmd);
            goto error;
    }
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_mode_e_attr_init(
    void **                             out_attr)
{
    globus_l_xio_mode_e_attr_t *        attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_mode_e_attr_init);

    GlobusXIOModeEDebugEnter();
    /*
     *  create a mode_e attr structure and intialize its values
     */
    attr = (globus_l_xio_mode_e_attr_t *)
                globus_malloc(sizeof(globus_l_xio_mode_e_attr_t));
    if(!attr)
    {
        result = GlobusXIOErrorMemory("attr");
        goto error_attr;
    }
    memcpy(attr, &globus_l_xio_mode_e_attr_default,
                        sizeof(globus_l_xio_mode_e_attr_t));
    *out_attr = attr;
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_attr:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_mode_e_attr_cntl(
    void *                              driver_attr,
    int                                 cmd,
    va_list                             ap)
{
    globus_result_t                     result;
    globus_l_xio_mode_e_attr_t *        attr;
    GlobusXIOName(globus_l_xio_mode_e_attr_cntl);

    GlobusXIOModeEDebugEnter();
    attr = (globus_l_xio_mode_e_attr_t *) driver_attr;
    switch(cmd)
    {
        case GLOBUS_XIO_MODE_E_SET_STACK:
        {
            /* globus_xio_stack_t is a pointer to a struct */
            globus_xio_stack_t stack;
            stack = va_arg(ap, globus_xio_stack_t);
            globus_xio_stack_copy(&attr->stack, stack);
            break;
        }
        case GLOBUS_XIO_MODE_E_GET_STACK:
        {
            globus_xio_stack_t * stack_out = va_arg(ap, globus_xio_stack_t *);
            *stack_out = attr->stack;
            break;
        }
        case GLOBUS_XIO_MODE_E_SET_NUM_STREAMS:
            attr->max_connection_count = va_arg(ap, int);
            break;
        case GLOBUS_XIO_MODE_E_GET_NUM_STREAMS:
        {
            int * max_connection_count_out = va_arg(ap, int*);
            *max_connection_count_out = attr->max_connection_count;
            break;
        }
        case GLOBUS_XIO_MODE_E_SET_OFFSET_READS:
            attr->offset_reads = va_arg(ap, globus_bool_t);
            break;
        case GLOBUS_XIO_MODE_E_GET_OFFSET_READS:
        {
            globus_bool_t * offset_reads_out = va_arg(ap, globus_bool_t*);
            *offset_reads_out = attr->offset_reads;
            break;
        }
        case GLOBUS_XIO_MODE_E_SET_MANUAL_EODC:
            attr->manual_eodc = va_arg(ap, globus_bool_t);
            break;
        case GLOBUS_XIO_MODE_E_GET_MANUAL_EODC:
        {
            globus_bool_t * manual_eodc_out = va_arg(ap, globus_bool_t*);
            *manual_eodc_out = attr->manual_eodc;
            break;
        }
        case GLOBUS_XIO_MODE_E_SEND_EOD:
            attr->send_eod = va_arg(ap, globus_bool_t);
            break;
        case GLOBUS_XIO_MODE_E_SET_EODC:
            attr->eod_count = va_arg(ap, int);
            break;
        case GLOBUS_XIO_MODE_E_DD_GET_OFFSET:
        {
            globus_off_t * offset_out = va_arg(ap, globus_off_t *);
            *offset_out = attr->offset;
            break;
        }

        case GLOBUS_XIO_MODE_E_SET_STACK_ATTR:
        {
            globus_xio_attr_t in_attr;
            in_attr = va_arg(ap, globus_xio_attr_t);
            if(attr->xio_attr != NULL)
            {
                globus_xio_attr_destroy(attr->xio_attr);
            }
            globus_xio_attr_copy(&attr->xio_attr, in_attr);
            break;
        }
        case GLOBUS_XIO_MODE_E_GET_STACK_ATTR:
        {
            globus_xio_attr_t *         out_attr;

            out_attr = va_arg(ap, globus_xio_attr_t *);
            if(out_attr != NULL) /* jsut in case user is dumb */
            {
                *out_attr = attr->xio_attr;
            }
            break;
        }
        default:
           result = GlobusXIOErrorInvalidCommand(cmd);
           goto error;
    }
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_mode_e_attr_copy(
    void **                             dst,
    void *                              src)
{
    globus_l_xio_mode_e_attr_t *        src_attr;
    globus_l_xio_mode_e_attr_t *        dst_attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_mode_e_attr_copy);

    GlobusXIOModeEDebugEnter();
    dst_attr = (globus_l_xio_mode_e_attr_t *)
                globus_malloc(sizeof(globus_l_xio_mode_e_attr_t));
    if(!dst_attr)
    {
        result = GlobusXIOErrorMemory("attr");
        goto error_dst_attr;
    }
    src_attr = (globus_l_xio_mode_e_attr_t *) src;
    memcpy(dst_attr, src_attr, sizeof(globus_l_xio_mode_e_attr_t));
    if(src_attr->stack != NULL)
    {
        globus_xio_stack_copy(&dst_attr->stack, src_attr->stack);
    }
    if(src_attr->xio_attr != NULL)
    {
        globus_xio_attr_copy(&dst_attr->xio_attr, src_attr->xio_attr);
    }
    /*
     * if there is any ptr in the attr structure do attr->xptr =
     * globus_libc_strdup(attr->xptr) and do if (!attr->xptr) { result =
     * GlobusXIOErrorMemory("xptr"); goto error_xptr; }
     */
    *dst = dst_attr;
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_dst_attr:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
globus_result_t
globus_l_xio_mode_e_attr_destroy(
    void *                              driver_attr)
{
    globus_l_xio_mode_e_attr_t *        attr;
    GlobusXIOName(globus_l_xio_mode_e_attr_destroy);

    GlobusXIOModeEDebugEnter();
    attr = (globus_l_xio_mode_e_attr_t *) driver_attr;
    globus_free(attr);
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;
}


static
globus_result_t
globus_l_xio_mode_e_init(
    globus_xio_driver_t *               out_driver)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_mode_e_init);

    GlobusXIOModeEDebugEnter();
    result = globus_xio_driver_init(&driver, "mode_e", GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_driver_init", result);
        goto error_init;
    }
    globus_xio_driver_set_transport(
        driver,
        globus_l_xio_mode_e_open,
        globus_l_xio_mode_e_close,
        globus_l_xio_mode_e_read,
        globus_l_xio_mode_e_write,
        globus_l_xio_mode_e_cntl);
    globus_xio_driver_set_server(
        driver,
        globus_l_xio_mode_e_server_init,
        globus_l_xio_mode_e_server_accept,
        globus_l_xio_mode_e_server_destroy,
        globus_l_xio_mode_e_server_cntl,
        globus_l_xio_mode_e_link_cntl,
        globus_l_xio_mode_e_link_destroy);
    globus_xio_driver_set_attr(
        driver,
        globus_l_xio_mode_e_attr_init,
        globus_l_xio_mode_e_attr_copy,
        globus_l_xio_mode_e_attr_cntl,
        globus_l_xio_mode_e_attr_destroy);

    globus_xio_driver_string_cntl_set_table(
        driver,
        mode_e_l_string_opts_table);

    *out_driver = driver;
    GlobusXIOModeEDebugExit();
    return GLOBUS_SUCCESS;

error_init:
    GlobusXIOModeEDebugExitWithError();
    return result;
}


static
void
globus_l_xio_mode_e_destroy(
    globus_xio_driver_t                 driver)
{
    globus_xio_driver_destroy(driver);
}


GlobusXIODefineDriver(
    mode_e,
    globus_l_xio_mode_e_init,
    globus_l_xio_mode_e_destroy);

