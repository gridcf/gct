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
/**
 * @file globus_ftp_control_data.c
 * @brief FTP Data Connection Configuration and Management
 */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

#include "globus_ftp_control.h"
#include "globus_i_ftp_control.h"
#include <string.h>
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif
#ifndef TARGET_ARCH_WIN32
#include <sys/uio.h>
#endif

/*
 *  logging messages
 */
#define GFTP_NL_EVENT_RECEIVED_DATA       "GFTPC_DATA_RECEIVED"
#define GFTP_NL_EVENT_SENT_DATA           "GFTPC_DATA_SENT"

#define GLOBUS_FTP_CONTROL_DATA_MAGIC               "FTPControlData-1.0"

#define GlobusFTPControlSetMagic(dc_handle)                           \
{                                                                     \
    strcpy(dc_handle->magic, GLOBUS_FTP_CONTROL_DATA_MAGIC);          \
}

#define GlobusFTPControlDataTestMagic(dc_handle)                      \
{                                                                     \
    globus_assert(dc_handle != GLOBUS_NULL &&                                \
       strcmp(dc_handle->magic, GLOBUS_FTP_CONTROL_DATA_MAGIC) == 0); \
}

#define DATA_CONN_MALLOC(data_conn, stripe, cb, ua)                   \
{                                                                     \
    data_conn = (globus_ftp_data_connection_t *)                      \
                     globus_malloc(                                   \
                          sizeof(globus_ftp_data_connection_t));      \
    data_conn->whos_my_daddy = stripe;                                \
    data_conn->offset = 0;                                            \
    data_conn->callback = cb;                                         \
    data_conn->user_arg = ua;                                         \
    data_conn->bytes_ready = 0;                                       \
    data_conn->eod = GLOBUS_FALSE;                                    \
    data_conn->close = GLOBUS_FALSE;                                  \
    data_conn->free_me = GLOBUS_FALSE;                                \
    data_conn->reusing = GLOBUS_FALSE;                                \
                                                                      \
}

#define CALLBACK_INFO_MALLOC(ci, dh, th, s, dc)                         \
{                                                                       \
    ci = (globus_l_ftp_data_callback_info_t *)                          \
              globus_malloc(sizeof(globus_l_ftp_data_callback_info_t)); \
    ci->stripe = s;                                                     \
    ci->dc_handle = dh;                                                 \
    ci->transfer_handle = th;                                           \
    ci->data_conn = dc;                                                 \
}

#define TABLE_ENTRY_MALLOC(t_e, buf, len, off, _eof, cb, cb_a, dc_h)    \
{                                                                       \
    t_e = (globus_l_ftp_handle_table_entry_t *)                         \
          globus_malloc(sizeof(globus_l_ftp_handle_table_entry_t));     \
    t_e->buffer = buf;                                                  \
    t_e->length = len;                                                  \
    t_e->offset = off;                                                  \
    t_e->error = GLOBUS_NULL;                                           \
    t_e->callback = cb;                                                 \
    t_e->callback_arg = cb_a;                                           \
    t_e->direction = dc_h->transfer_handle->direction;                  \
    t_e->dc_handle = dc_h;                                              \
    t_e->transfer_handle = dc_h->transfer_handle;                       \
    t_e->type = dc_h->type;                                             \
    t_e->error = GLOBUS_NULL;                                           \
    t_e->whos_my_daddy = GLOBUS_NULL;                                   \
    t_e->ascii_buffer = GLOBUS_NULL;                                    \
    t_e->eof = _eof;                                                    \
}

/********************************************************************
*        module data types
********************************************************************/
struct globus_ftp_data_stripe_s;
struct globus_l_ftp_handle_table_entry_s;
struct globus_i_ftp_dc_transfer_handle_s;

typedef struct globus_l_ftp_c_data_layout_s
{
    globus_ftp_control_layout_func_t            layout_func;
    globus_ftp_control_layout_verify_func_t     verify_func;
    char *                                      name;
} globus_l_ftp_c_data_layout_t;


/*
 *  individual data connection
 *  1 to many relationship with a stripe
 */
typedef struct globus_ftp_data_connection_s
{
    globus_io_handle_t                          io_handle;
    globus_off_t                                offset;
    struct globus_ftp_data_stripe_s *           whos_my_daddy;
    globus_ftp_control_data_connect_callback_t  callback;
    void *                                      user_arg;
    globus_size_t                               bytes_ready;

    globus_bool_t                               eod;
    globus_bool_t                               close;
    globus_bool_t                               reusing;

    /* need free_me for globus_io cancel issue */
    globus_bool_t                               free_me;
} globus_ftp_data_connection_t;

/*
 *  each strip can have multiple parallel connections to
 *  the same host
 */
typedef struct globus_ftp_data_stripe_s
{
    globus_fifo_t                               free_conn_q;
    globus_list_t *                             free_cache_list;
    globus_list_t *                             all_conn_list;

    globus_list_t *                             outstanding_conn_list;

    unsigned int                                stripe_ndx;
    int                                         outstanding_connections;
    globus_bool_t                               listening;
    globus_bool_t                               eof_sent;
    globus_fifo_t                               command_q;
    globus_io_handle_t                          listener_handle;
    globus_ftp_control_parallelism_t            parallel;
    globus_ftp_control_host_port_t              host_port;
    struct globus_i_ftp_dc_transfer_handle_s *  whos_my_daddy;

    int                                         connection_count;
    int                                         total_connection_count;

    globus_bool_t                               eof;
    globus_size_t                               eod_count;
    globus_size_t                               eods_received;
} globus_ftp_data_stripe_t;

/* transient */
typedef struct globus_i_ftp_dc_transfer_handle_s
{
    globus_ftp_data_stripe_t *                  stripes;
    int                                         stripe_count;
    globus_handle_table_t                       handle_table;

    globus_ftp_data_connection_state_t          direction;

    int                                         ref;
    globus_ftp_control_parallelism_t            parallel;
    globus_bool_t                               eof_registered;
    globus_handle_t                             eof_table_handle;
    struct globus_l_ftp_handle_table_entry_s *  eof_cb_ent;

    /* big buffer stuff */
    globus_byte_t *                             big_buffer;
    globus_size_t                               big_buffer_length;
    globus_byte_t                               big_buffer_byte[1];
    void *                                      big_buffer_cb_arg;
    globus_ftp_control_data_callback_t          big_buffer_cb;

    globus_bool_t                               x_state;

    struct globus_l_ftp_send_eof_entry_s *      send_eof_ent;

    globus_mutex_t *                            mutex;
    globus_i_ftp_dc_handle_t *                  whos_my_daddy;
    struct globus_ftp_control_handle_s *        control_handle;
    
    globus_mutex_t                              order_mutex;
    globus_bool_t                               order_data;
    int                                         order_ctr;
    globus_off_t                                order_next_offset;
    globus_off_t                                order_next_offset_passed;
    globus_priority_q_t                         ordered_buffer_q;
} globus_i_ftp_dc_transfer_handle_t;

typedef struct globus_l_ftp_send_eof_entry_s
{
    globus_ftp_data_connection_state_t          direction;
    globus_ftp_data_connection_t *              whos_my_daddy;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;

/* above must match first elements  of globus_l_ftp_handle_table_entry_t */
    globus_bool_t                               eof_message;

    int *                                       count;
    int                                         array_size;
    globus_ftp_control_callback_t               cb;
    void *                                      user_arg;

    globus_ftp_data_connection_t *              data_conn;

    globus_handle_t                             callback_table_handle;
} globus_l_ftp_send_eof_entry_t;

typedef struct globus_l_ftp_handle_table_entry_s
{
    globus_ftp_data_connection_state_t          direction;
    globus_ftp_data_connection_t *              whos_my_daddy;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;

    globus_byte_t *                             buffer;
    globus_byte_t *                             ascii_buffer;
    globus_size_t                               length;
    globus_off_t                                offset;
    globus_bool_t                               eof;
    globus_ftp_control_data_callback_t          callback;
    void *                                      callback_arg;

    globus_object_t *                           error;
    globus_handle_t                             callback_table_handle;

    globus_ftp_control_type_t                   type;

} globus_l_ftp_handle_table_entry_t;

/*
 *  extended block mode header
 */
typedef struct globus_l_ftp_eb_header_s
{
    globus_byte_t                               descriptor;
    globus_byte_t                               count[8];
    globus_byte_t                               offset[8];
} globus_l_ftp_eb_header_t;

/*
 *  this structure is passed around to the callbacks to
 *  allow references directly to the
 */
typedef struct globus_l_ftp_data_callback_info_s
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_ftp_data_connection_t *              data_conn;
    globus_ftp_data_stripe_t *                  stripe;
    globus_l_ftp_eb_header_t *                  eb_header;
} globus_l_ftp_data_callback_info_t;

typedef struct globus_l_ftp_dc_connect_cb_info_s
{
    int                                         stripe_ndx;
    globus_ftp_control_data_connect_callback_t  callback;
    void *                                      user_arg;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
} globus_l_ftp_dc_connect_cb_info_t;

/********************************************************************
 *    internal function signatures
 ********************************************************************/

globus_result_t
globus_l_ftp_control_data_send_eof(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_data_connection_t *              data_conn,
    globus_l_ftp_send_eof_entry_t *             eof_ent);

globus_result_t
globus_i_ftp_control_data_force_close(
    globus_i_ftp_dc_handle_t *                   dc_handle,
    globus_ftp_control_callback_t                close_callback_func,
    void *                                       close_arg,
    globus_object_t *                            err);

globus_result_t
globus_l_ftp_control_data_stream_connect_direction(
    globus_i_ftp_dc_handle_t *                  handle,
    globus_ftp_control_data_connect_callback_t  callback,
    void *                                      user_arg,
    globus_ftp_data_connection_state_t          direction);

globus_result_t
globus_l_ftp_data_stream_stripe_poll(
    globus_ftp_data_stripe_t *		         stripe);

globus_result_t
globus_l_ftp_data_stripe_poll(
    globus_i_ftp_dc_handle_t *                   dc_handle);

void
globus_l_ftp_control_stripes_create(
    globus_i_ftp_dc_handle_t *                   dc_handle,
    globus_ftp_control_host_port_t               addresses[],
    int                                          stripe_count);

void
globus_l_ftp_stream_listen_callback(
    void *                                      callback_arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result);

void
globus_l_ftp_stream_write_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbytes);

void
globus_l_ftp_stream_read_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbyte);

void
globus_l_ftp_stream_accept_connect_callback(
    void *                                      callback_arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result);

void
globus_l_ftp_eb_listen_callback(
    void *                                      callback_arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result);

void
globus_l_ftp_eb_accept_callback(
    void *                                      callback_arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result);

void
globus_l_ftp_eb_connect_callback(
    void *                                      callback_arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result);

void
globus_l_ftp_eb_write_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    struct iovec *                              iov,
    globus_size_t                               iovcnt,
    globus_size_t                               nbytes);

void
globus_l_ftp_eb_read_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbyte);

globus_result_t
globus_l_ftp_control_data_stream_read_write(
    globus_i_ftp_dc_handle_t *                  handle,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    globus_ftp_control_data_callback_t          callback,
    void *                                      callback_arg);

globus_result_t
globus_l_ftp_control_data_eb_write(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    globus_ftp_control_data_callback_t          callback,
    void *                                      callback_arg);

void
globus_l_ftp_eb_send_eof_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbytes);

void
globus_l_ftp_control_stripes_destroy(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_object_t *                           error);

void
globus_l_error_flush_command_q(
    globus_ftp_data_stripe_t *                  stripe,
    globus_object_t *                           error);

globus_byte_t *
globus_l_ftp_control_add_ascii(
    globus_byte_t *                             in_buf,
    int                                         length,
    globus_off_t *                              ascii_len);

int
globus_l_ftp_control_strip_ascii(
    globus_byte_t *                             in_buf,
    int                                         length);

void
globus_l_ftp_io_close_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result);

static
void
globus_l_ftp_control_command_kickout(
    void *                                      user_args);

static
void
globus_l_ftp_control_send_data_kickout(
    void *                                      user_args);

static
void
globus_l_ftp_control_reuse_connect_callback(
    void *                                      user_args);

static
void
globus_l_ftp_control_close_kickout(
    void *                                      user_args);

void
globus_l_ftp_control_deactivate_quit_callback(
    void *                                      user_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error);

globus_result_t
globus_l_ftp_data_eb_poll(
    globus_i_ftp_dc_handle_t *                   dc_handle);

globus_result_t
globus_l_ftp_control_data_extended_block_enqueue(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_l_ftp_handle_table_entry_t *         entry,
    int                                         chunk);

globus_result_t
globus_l_ftp_control_data_register_connect(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_data_stripe_t *                  stripe,
    globus_ftp_control_data_connect_callback_t  callback,
    void *                                      user_arg);

static
void
globus_l_ftp_control_data_encode(
    globus_byte_t *                             buf,
    globus_off_t                                x);

static
void
globus_l_ftp_control_data_decode(
    globus_byte_t *                             buf,
    globus_off_t *                              x);

void
globus_l_ftp_eb_read_header_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbyte);

globus_result_t
globus_l_ftp_control_data_eb_connect_write(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_control_data_connect_callback_t  callback,
    void *                                      user_arg);

globus_result_t
globus_l_ftp_control_data_eb_connect_read(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_control_data_connect_callback_t  callback,
    void *                                      user_arg);

globus_result_t
globus_l_ftp_control_data_adjust_connection(
    globus_ftp_data_stripe_t *                  stripe);

globus_result_t
globus_l_ftp_control_data_register_eod(
    globus_ftp_data_stripe_t *                   stripe,
    globus_ftp_data_connection_t *               data_conn);

globus_result_t
globus_l_ftp_control_data_register_eof(
    globus_ftp_data_stripe_t *                   stripe,
    globus_ftp_data_connection_t *               data_conn);

void
globus_l_ftp_eb_eof_eod_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbytes);

void
globus_l_ftp_close_msg_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbytes);

globus_result_t
globus_l_ftp_control_register_close_msg(
    globus_i_ftp_dc_handle_t *                   dc_handle,
    globus_ftp_data_connection_t *               data_conn);

globus_result_t
globus_i_ftp_control_data_write_stripe(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    int                                         stripe_ndx,
    globus_ftp_control_data_write_info_t *      data_info);

globus_result_t
globus_i_ftp_control_create_data_info(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_control_data_write_info_t *      data_info,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    globus_ftp_control_data_callback_t          callback,
    void *                                      callback_arg);

globus_result_t
globus_i_ftp_control_release_data_info(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_control_data_write_info_t *      data_info);

globus_bool_t
globus_l_ftp_control_dc_dec_ref(
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle);

const char *
globus_l_ftp_control_state_to_string(
    globus_ftp_data_connection_state_t          state);

/******************************************************************
*                module variables
******************************************************************/
static globus_mutex_t               globus_l_ftp_control_data_mutex;
static globus_list_t *              globus_l_ftp_control_data_dc_list
                                                               = GLOBUS_NULL;
static globus_bool_t                globus_l_ftp_control_data_active
                                                               = GLOBUS_FALSE;
static globus_cond_t                globus_l_ftp_control_data_cond;
static int                          globus_l_ftp_control_data_dc_count = 0;

static globus_hashtable_t           globus_l_ftp_control_data_layout_table;

#define GFTPC_HASH_TABLE_SIZE       64
/******************************************************************
*                   header definitions
******************************************************************/
#define GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_CLOSE          0x04
#define GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_EOD            0x08
#define GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_RESTART        0x10
#define GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_ERRORS         0x20
#define GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_EOF            0x40
#define GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_EOR            0x80

/*
 *  NETLOGGER functions
 */
globus_result_t
globus_i_ftp_control_data_set_netlogger(
    globus_ftp_control_handle_t *               handle,
    globus_netlogger_handle_t *                 nl_handle,
    globus_bool_t                               nl_ftp_control,
    globus_bool_t                               nl_globus_io)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_set_netlogger";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(nl_ftp_control)
        {
            globus_io_attr_netlogger_copy_handle(nl_handle,
                &dc_handle->nl_ftp_handle);
            dc_handle->nl_ftp_handle_set = GLOBUS_TRUE;
        }
        if(nl_globus_io)
        {
            globus_io_attr_netlogger_copy_handle(nl_handle,
                &dc_handle->nl_io_handle);
            globus_io_attr_netlogger_set_handle(
                &dc_handle->io_attr,
                &dc_handle->nl_io_handle);
            globus_netlogger_set_desc(
                &dc_handle->nl_io_handle,
                "FTP_DATA");
            dc_handle->nl_io_handle_set = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}

/*
 * if priority_1 comes after priority_2, return > 0
 * else if priority_1 comes before priority_2, return < 0
 * else return 0
 */
 
static
int
globus_l_ftp_control_q_compare(
    void *                              priority_1,
    void *                              priority_2)
{
    int                                 rc;
    globus_l_ftp_handle_table_entry_t * entry1;
    globus_l_ftp_handle_table_entry_t * entry2;
    
    entry1 = (globus_l_ftp_handle_table_entry_t *) priority_1;
    entry2 = (globus_l_ftp_handle_table_entry_t *) priority_2;
    
    if(entry1->offset > entry2->offset || entry1->eof)
    {
        rc = 1;
    }
    else if(entry1->offset < entry2->offset || entry2->eof)
    {
        rc = -1;
    }
    else
    {
        rc = 0;
    }
    return rc;
}

typedef struct globus_i_ftp_control_stack_entry_s
{
    globus_xio_driver_t                 driver;
    char *                              driver_name;
    char *                              opts;
} globus_i_ftp_control_stack_entry_t;

globus_result_t
globus_i_ftp_control_create_stack(
    globus_ftp_control_handle_t *               handle,
    globus_list_t *                             driver_list,
    globus_xio_stack_t *                        stack)
{
    globus_result_t                             result;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    globus_xio_attr_t                           xio_attr;
    globus_i_ftp_control_stack_entry_t *        driver_ent;
    globus_list_t *                             list;
    static char *                               myname =
                                        "globus_i_ftp_control_create_stack";

    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(driver_list == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "driver_list",
                  2,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    globus_xio_stack_init(stack, NULL);

    globus_i_ftp_control_data_get_attr(
        handle, &xio_attr);

    for(list = driver_list;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
        driver_ent = 
            (globus_i_ftp_control_stack_entry_t *) globus_list_first(list);

        result = GLOBUS_SUCCESS;

        result = globus_xio_stack_push_driver(
            *stack, driver_ent->driver);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }

        if(driver_ent->opts != NULL)
        {
            /* ignore error */
            globus_xio_attr_cntl(
                xio_attr,
                driver_ent->driver,
                GLOBUS_XIO_SET_STRING_OPTIONS,
                driver_ent->opts);
        }
    }
    
    return GLOBUS_SUCCESS;

error:
    globus_xio_stack_destroy(*stack);
    return result;
    
}

void
globus_i_ftp_control_unload_xio_drivers(
    globus_list_t *                        driver_list)
{
    globus_i_ftp_control_stack_entry_t *   driver_ent;

    while(!globus_list_empty(driver_list))
    {
        driver_ent = (globus_i_ftp_control_stack_entry_t *)
            globus_list_first(driver_list);
       
         
        if(driver_ent->driver_name)
        {
            if(strcmp(driver_ent->driver_name, "tcp") != 0 && 
                strcmp(driver_ent->driver_name, "gsi") != 0)
            {
    /*        globus_xio_driver_unload(driver_ent->driver); */
            }
            globus_free(driver_ent->driver_name);
        }

        if(driver_ent->opts)
        {
            globus_free(driver_ent->opts);
        }
        globus_free(driver_ent);
        
        globus_list_remove(&driver_list, driver_list);
    }
}
   
globus_result_t
globus_i_ftp_control_load_xio_drivers(
    char *                              driver_string,
    globus_list_t **                    driver_list)
{
    globus_result_t                     result;
    globus_bool_t                       done = GLOBUS_FALSE;
    char *                              opts;
    char *                              ptr;
    char *                              driver_str;
    char *                              driver_name;
    char *                              tmp_str;
    globus_xio_driver_t                 driver;
    globus_list_t *                     list = NULL;
    globus_i_ftp_control_stack_entry_t *stack_ent;

    if(driver_string != NULL)
    {
        driver_str = globus_libc_strdup(driver_string);
        tmp_str = driver_str;
        while(!done)
        {
            driver_name = tmp_str;
            ptr = strchr(driver_name, ',');
            if(ptr != NULL)
            {
                *ptr = '\0';
                tmp_str = ptr+1; 
            }
            else
            {
                done = GLOBUS_TRUE;
            }
            opts = strchr(driver_name, ':');
            if(opts != NULL)
            {
                *opts = '\0';
                opts++;
            }

            if(strcmp(driver_name, "tcp") == 0)
            {
                driver = globus_io_compat_get_tcp_driver();
            }
            else if(strcmp(driver_name, "gsi") == 0)
            {
                driver = globus_io_compat_get_gsi_driver();
            }
            else
            {
                result = globus_xio_driver_load(driver_name, &driver);
                if(result != GLOBUS_SUCCESS)
                {
                    goto error_load;
                }
            }
            stack_ent = (globus_i_ftp_control_stack_entry_t *)
                globus_calloc(1, sizeof(globus_i_ftp_control_stack_entry_t));
            stack_ent->opts = globus_libc_strdup(opts);
            stack_ent->driver = driver;
            stack_ent->driver_name = globus_libc_strdup(driver_name);

            globus_list_insert(&list, stack_ent);
        }
        
        globus_free(driver_str);
    }
    else
    {
        stack_ent = (globus_i_ftp_control_stack_entry_t *)
            globus_calloc(1, sizeof(globus_i_ftp_control_stack_entry_t));
        stack_ent->opts = NULL;
        stack_ent->driver = globus_io_compat_get_gsi_driver();
        stack_ent->driver_name = globus_libc_strdup("gsi");
        globus_list_insert(&list, stack_ent);

        stack_ent = (globus_i_ftp_control_stack_entry_t *)
            globus_calloc(1, sizeof(globus_i_ftp_control_stack_entry_t));
        stack_ent->opts = NULL;
        stack_ent->driver = globus_io_compat_get_tcp_driver();
        stack_ent->driver_name = globus_libc_strdup("tcp");
        globus_list_insert(&list, stack_ent);
    }

    /* reverse list */
    while(!globus_list_empty(list))
    {
        globus_list_insert(driver_list, globus_list_first(list));
        globus_list_remove(&list, list);
    }
    
    return GLOBUS_SUCCESS;

error_load:
    globus_free(driver_str);
    return result;
}


globus_result_t
globus_i_ftp_control_data_set_stack(
    globus_ftp_control_handle_t *               handle,
    globus_xio_stack_t                          stack)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname =
                                      "globus_i_ftp_control_data_set_stack";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(stack == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "stack",
                  2,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        globus_io_attr_set_stack(&dc_handle->io_attr, stack);
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_i_ftp_control_data_get_attr(
    globus_ftp_control_handle_t *               handle,
    globus_xio_attr_t *                         attr)
{
    globus_result_t                             result;
    globus_i_ftp_dc_handle_t *                  dc_handle;

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);

    result = globus_io_attr_get_xio_attr(&dc_handle->io_attr, attr);

    return result;
}


globus_bool_t
globus_list_remove_element(
    globus_list_t * volatile *                  headp,
    void *                                      datum)
{
    globus_list_t *                             list;

    for(list = *headp;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
         if(datum == globus_list_first(list))
         {
             globus_list_remove(headp, list);
             return GLOBUS_TRUE;
         }
    }

    return GLOBUS_FALSE;
}



/**
 * @brief Create an incoming FTP data connection
 * @ingroup globus_ftp_control_data
 * @details
 * This function will register a globus_io_{accept, connect}. Further
 * accepts/connects are done by registering a new accept/connect in
 * the current accept/connect callback. A call to either
 * globus_ftp_control_local_pasv() or
 * globus_ftp_control_local_port() needs to precede this calling
 * this function. This function may be followed by a
 * globus_ftp_data_read.
 *
 * @param handle
 *        A pointer to a FTP control handle which is configured to
 *        create an incoming data connection.
 *
 * @param callback
 *        This callback is called when the connection occurs.  This
 *        parameter may be NULL.
 *
 * @param user_arg
 *        The user argument passed to the connect callback.
 */
globus_result_t
globus_ftp_control_data_connect_read(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_data_connect_callback_t  callback,
    void *                                      user_arg)
{
    globus_result_t                             result;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               my_name=
                                        "globus_ftp_control_data_connect_read";

    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  my_name);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;

    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  my_name);
        return globus_error_put(err);
    }
    if(dc_handle->transfer_handle == GLOBUS_NULL)
    {
        err = globus_error_construct_string(
                           GLOBUS_FTP_CONTROL_MODULE,
                           GLOBUS_NULL,
                           _FCSL("[%s]:%s():transfer handle does not exist"),
                           GLOBUS_FTP_CONTROL_MODULE->module_name,
                           my_name);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(dc_handle->connect_error)
        {
            globus_object_free(dc_handle->connect_error);
            dc_handle->connect_error = GLOBUS_NULL;
        }
        
        if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_STREAM)
        {
            if(dc_handle->state != GLOBUS_FTP_DATA_STATE_PORT &&
               dc_handle->state != GLOBUS_FTP_DATA_STATE_PASV)
            {
                globus_mutex_unlock(&dc_handle->mutex);

                err = globus_error_construct_string(
                           GLOBUS_FTP_CONTROL_MODULE,
                           GLOBUS_NULL,
   _FCSL("[%s] Need to call local_pasv() or local_port() before calling connect_read/write()"),
                           GLOBUS_FTP_CONTROL_MODULE->module_name);
                return globus_error_put(err);
            }
            result =  globus_l_ftp_control_data_stream_connect_direction(
                          dc_handle,
                          callback,
                          user_arg,
                          GLOBUS_FTP_DATA_STATE_CONNECT_READ);
        }
        else if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK)
        {
            result = globus_l_ftp_control_data_eb_connect_read(
                          dc_handle,
                          callback,
                          user_arg);
        }
        else
        {
            err = globus_error_construct_string(
                           GLOBUS_FTP_CONTROL_MODULE,
                           GLOBUS_NULL,
                           _FCSL("[%s]:%s(): invalid mode, possible memory corruption"),
                           GLOBUS_FTP_CONTROL_MODULE->module_name,
                           my_name);
            result = globus_error_put(err);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return result;
}
/* globus_ftp_control_data_connect_read() */

/**
 * @brief Create an outgoing FTP data connection
 * @ingroup globus_ftp_control_data
 * @details
 *  This function sets the interface that will be used to send and
 *  receive information along the data channel.
 *
 * @param handle
 *        A pointer to a FTP control handle which is configured to
 *        create an outgoing data connection.
 * @param interface_addr
 *
 */
globus_result_t
globus_ftp_control_data_set_interface(
    globus_ftp_control_handle_t *               handle,
    const char *                                interface_addr)
{
    globus_result_t                             res;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               my_name=
                                      "globus_ftp_control_data_set_interface";

    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  my_name);
        return globus_error_put(err);
    }
    if(interface_addr == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "interface_addr",
                  2,
                  my_name);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  my_name);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        res = globus_io_attr_set_tcp_interface(
                  &dc_handle->io_attr,
                  interface_addr);
        if(res == GLOBUS_SUCCESS)
        {
            dc_handle->interface_addr = strdup(interface_addr);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return res;
}

/**
 * @brief Create an outgoing FTP data connection
 * @ingroup globus_ftp_control_data
 * @details
 * This function will register a globus_io_{accept, connect}. Further
 * accepts/connects are done by registering a new accept/connect in
 * the current accept/connect callback. A call to either
 * globus_ftp_control_local_pasv() or
 * globus_ftp_control_local_port() needs to precede this calling
 * this function. This function may be followed by a
 * globus_ftp_data_write.
 *
 * @param handle
 *        A pointer to a FTP control handle which is configured to
 *        create an outgoing data connection.
 *
 * @param callback
 *        This callback is called when the connection occurs.  This
 *        parameter may be NULL.
 *
 * @param user_arg
 *        The user argument passed to the connect callback.
 */
globus_result_t
globus_ftp_control_data_connect_write(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_data_connect_callback_t  callback,
    void *                                      user_arg)
{
    globus_result_t                             result;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               my_name=
                                      "globus_ftp_control_data_connect_write";

    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  my_name);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  my_name);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(dc_handle->connect_error)
        {
            globus_object_free(dc_handle->connect_error);
            dc_handle->connect_error = GLOBUS_NULL;
        }
        
        if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_STREAM)
        {
            if(dc_handle->state != GLOBUS_FTP_DATA_STATE_PORT &&
               dc_handle->state != GLOBUS_FTP_DATA_STATE_SPOR &&
               dc_handle->state != GLOBUS_FTP_DATA_STATE_PASV)
            {
                globus_mutex_unlock(&dc_handle->mutex);
                err =  globus_error_construct_string(
                           GLOBUS_FTP_CONTROL_MODULE,
                           GLOBUS_NULL,
   _FCSL("[%s]:%s() Need to call local_pasv() or local_port() before calling connect_read/write()"),
                           GLOBUS_FTP_CONTROL_MODULE->module_name,
                           my_name);
                return globus_error_put(err);
            }

            result = globus_l_ftp_control_data_stream_connect_direction(
                         dc_handle,
                         callback,
                         user_arg,
                         GLOBUS_FTP_DATA_STATE_CONNECT_WRITE);
        }
        else if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK)
        {
            result = globus_l_ftp_control_data_eb_connect_write(
                         dc_handle,
                         callback,
                         user_arg);
        }
        else
        {
            err = globus_error_construct_string(
                   GLOBUS_FTP_CONTROL_MODULE,
                   GLOBUS_NULL,
                   _FCSL("[%s]:%s(): Invalid mode, possible memory corruption"),
                   GLOBUS_FTP_CONTROL_MODULE->module_name,
                   my_name);
            result = globus_error_put(err);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return result;
}

/*
 *  globus_l_ftp_control_data_eb_connect_write()
 *  --------------------------------------------
 *
 *  valid state:
 *  SPOR:
 *     -- the handle is in spor mode, via a call to local_spor.
 *
 *  using a cached connection:
 *     -- in extended block mode we cache data connections
 *        until the user calls local_pasv() or local_spor().
 *        So the user is allowed to call connect_read/write
 *        multiple times provided they are going in the same
 *        direction.
 */
globus_result_t
globus_l_ftp_control_data_eb_connect_write(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_control_data_connect_callback_t  callback,
    void *                                      user_arg)
{
    globus_result_t                             result = GLOBUS_SUCCESS;
    globus_ftp_data_stripe_t *                  stripe;
    globus_ftp_data_connection_t *              data_conn;
    globus_bool_t                               reusing = GLOBUS_FALSE;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    int                                         ctr;
    globus_bool_t *                             connected;
    globus_reltime_t                            reltime;
    globus_l_ftp_dc_connect_cb_info_t *         connect_cb_info;
    static char *                               my_name =
       "globus_l_ftp_control_data_eb_connect_write";

    /*
     *  make sure that we still exist
     */
    if(dc_handle->transfer_handle == GLOBUS_NULL)
    {
        return globus_error_put(globus_error_construct_string(
                   GLOBUS_FTP_CONTROL_MODULE,
                   GLOBUS_NULL,
                   _FCSL("[%s]:%s(): Handle not in transfer state proper state.  Call local_port or local_spor before calling connect_write."),
                           GLOBUS_FTP_CONTROL_MODULE->module_name,
                           my_name));

    }
    transfer_handle = dc_handle->transfer_handle;
    if(transfer_handle->direction != GLOBUS_FTP_DATA_STATE_CONNECT_WRITE &&
       dc_handle->state == GLOBUS_FTP_DATA_STATE_EOF)
    {
        return globus_error_put(globus_error_construct_string(
                   GLOBUS_FTP_CONTROL_MODULE,
                   GLOBUS_NULL,
                   _FCSL("eb_connect_write(): Cannot reuse a read connection for writing.  Call local_port() or local_spor() to reset state.")));
    }
    else if(dc_handle->state != GLOBUS_FTP_DATA_STATE_SPOR &&
            dc_handle->state != GLOBUS_FTP_DATA_STATE_PORT &&
            !(dc_handle->state == GLOBUS_FTP_DATA_STATE_EOF &&
              transfer_handle->direction == GLOBUS_FTP_DATA_STATE_CONNECT_WRITE))
    {
        return globus_error_put(globus_error_construct_string(
                   GLOBUS_FTP_CONTROL_MODULE,
                   GLOBUS_NULL,
                   _FCSL("eb_connect_write(): Handle not in the proper state.  Call local_port or local_spor before calling connect_write.")));
    }

    connected = globus_malloc(
                    sizeof(globus_bool_t) * transfer_handle->stripe_count);
    memset(connected, '\0',
        sizeof(globus_bool_t) * transfer_handle->stripe_count);
    /*
     *  if we are using cached connections
     */

    if(transfer_handle->direction == GLOBUS_FTP_DATA_STATE_CONNECT_WRITE &&
       dc_handle->state == GLOBUS_FTP_DATA_STATE_EOF)
    {
        reusing = GLOBUS_TRUE;

        transfer_handle->eof_registered = GLOBUS_FALSE;
        transfer_handle->eof_cb_ent = GLOBUS_NULL;
        transfer_handle->big_buffer = GLOBUS_NULL;
        transfer_handle->big_buffer_cb = GLOBUS_NULL;
        transfer_handle->send_eof_ent = GLOBUS_NULL;

        for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
        {
            stripe = &transfer_handle->stripes[ctr];

            stripe->eods_received = 0;
            stripe->eof_sent = GLOBUS_FALSE;
            stripe->eof = GLOBUS_FALSE;
            stripe->eod_count = -1;
            stripe->total_connection_count = 0;

            while(!globus_list_empty(stripe->free_cache_list))
            {
                /* get first item and remove it from cache list */
                data_conn = (globus_ftp_data_connection_t *)
                                globus_list_first(stripe->free_cache_list);
                data_conn->eod = GLOBUS_FALSE;

                globus_list_remove(
                    &stripe->free_cache_list,
                    stripe->free_cache_list);

                if(stripe->connection_count <= stripe->parallel.base.size)
                {
                    globus_fifo_enqueue(
                        &stripe->free_conn_q,
                        data_conn);
                    stripe->connection_count++;
                    stripe->total_connection_count++;
                }
                else
                {
                    globus_list_remove_element(
                        &stripe->all_conn_list, data_conn);
                    data_conn->whos_my_daddy = NULL;
                    globus_l_ftp_control_register_close_msg(
                        dc_handle,
                        data_conn);
                }
                if(!connected[ctr] && callback != GLOBUS_NULL)
                {
                    connected[ctr] = GLOBUS_TRUE;
                    transfer_handle->ref++;

                    connect_cb_info = (globus_l_ftp_dc_connect_cb_info_t *)
                      globus_malloc(sizeof(globus_l_ftp_dc_connect_cb_info_t));
                    connect_cb_info->callback = callback;
                    connect_cb_info->stripe_ndx = stripe->stripe_ndx;
                    connect_cb_info->dc_handle = dc_handle;
                    connect_cb_info->user_arg = user_arg;
                    connect_cb_info->transfer_handle = transfer_handle;

                    /* register a on shot for connection */
                    GlobusTimeReltimeSet(reltime, 0, 0);
                    globus_callback_register_oneshot(
                        GLOBUS_NULL,
                        &reltime,
                        globus_l_ftp_control_reuse_connect_callback,
                        (void *) connect_cb_info);
                    /* register callback */
                }
            }
        }
    }

    /*
     *  if we are creating new data connections
     */
    if(dc_handle->state == GLOBUS_FTP_DATA_STATE_SPOR ||
       dc_handle->state == GLOBUS_FTP_DATA_STATE_PORT ||
       reusing)
    {
        for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
        {
            stripe = &transfer_handle->stripes[ctr];

            if(stripe->connection_count < stripe->parallel.base.size &&
               !connected[ctr])
            {
                result = globus_l_ftp_control_data_register_connect(
                             dc_handle,
                             stripe,
                             callback,
                             user_arg);
                if(result != GLOBUS_SUCCESS)
                {
                    goto exit;
                }
                if(callback != GLOBUS_NULL)
                {
                    transfer_handle->ref++;
                }
            }
        }
        dc_handle->state = GLOBUS_FTP_DATA_STATE_CONNECT_WRITE;
        transfer_handle->direction = GLOBUS_FTP_DATA_STATE_CONNECT_WRITE;
    }
    else
    {
        result =  globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("eb_connect_write(): Handle not in the proper state")));
    }

 exit:

    globus_free(connected);
    return result;
}

globus_result_t
globus_l_ftp_control_data_eb_connect_read(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_control_data_connect_callback_t  callback,
    void *                                      user_arg)
{
    globus_result_t                             result = GLOBUS_SUCCESS;
    int                                         ctr;
    globus_ftp_data_stripe_t *                  stripe;
    globus_ftp_data_connection_t *              data_conn;
    globus_object_t *                           err;
    globus_l_ftp_eb_header_t *                  eb_header;
    globus_result_t                             res;
    globus_bool_t                               reusing = GLOBUS_FALSE;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_reltime_t                            reltime;
    globus_l_ftp_dc_connect_cb_info_t *         connect_cb_info;
    static char *                               my_name =
        "globus_l_ftp_control_data_eb_connect_read";
    /*
     *  make sure that we still exist
     */
    if(dc_handle->transfer_handle == GLOBUS_NULL)
    {
        err = globus_error_construct_string(
                   GLOBUS_FTP_CONTROL_MODULE,
                   GLOBUS_NULL,
                   _FCSL("[%s]:%s Handle not in transfer state proper state.  Call local_port or local_spor before calling connect_write."),
                   GLOBUS_FTP_CONTROL_MODULE->module_name,
                   my_name);

        return globus_error_put(err);
    }
    transfer_handle = dc_handle->transfer_handle;
    if(dc_handle->state == GLOBUS_FTP_DATA_STATE_EOF &&
       transfer_handle->direction != GLOBUS_FTP_DATA_STATE_CONNECT_READ)
    {
        err = globus_error_construct_string(
                   GLOBUS_FTP_CONTROL_MODULE,
                   GLOBUS_NULL,
                   _FCSL("eb_connect_read(): Cannot reuse a write connection for reading.  Call local_pasv() or local_spas() to reset state."));

        return globus_error_put(err);
    }
    else if((dc_handle->state != GLOBUS_FTP_DATA_STATE_PASV) &&
            !(dc_handle->state == GLOBUS_FTP_DATA_STATE_EOF &&
             transfer_handle->direction == GLOBUS_FTP_DATA_STATE_CONNECT_READ))
    {
        err = globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("eb_connect_read(): Handle not in the proper state"));
        return globus_error_put(err);
    }

    /*
     *  cached data connections
     */
    if(dc_handle->state == GLOBUS_FTP_DATA_STATE_EOF &&
       transfer_handle->direction == GLOBUS_FTP_DATA_STATE_CONNECT_READ)
    {
        globus_bool_t                          register_onshot;

        reusing = GLOBUS_TRUE;

        transfer_handle->eof_registered = GLOBUS_FALSE;
        transfer_handle->eof_cb_ent = GLOBUS_NULL;
        transfer_handle->big_buffer = GLOBUS_NULL;
        transfer_handle->big_buffer_cb = GLOBUS_NULL;
        transfer_handle->send_eof_ent = GLOBUS_NULL;

        for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
        {
            stripe = &transfer_handle->stripes[ctr];

            stripe->eods_received = 0;
            stripe->eof_sent = GLOBUS_FALSE;
            stripe->eof = GLOBUS_FALSE;
            stripe->eod_count = -1;

            register_onshot = GLOBUS_TRUE;

            while(!globus_list_empty(stripe->free_cache_list))
            {
                data_conn = (globus_ftp_data_connection_t *)
                               globus_list_first(stripe->free_cache_list);
                globus_list_remove(
                    &stripe->free_cache_list,
                    stripe->free_cache_list);

                eb_header = (globus_l_ftp_eb_header_t *)
                              globus_malloc(sizeof(globus_l_ftp_eb_header_t));
                data_conn->bytes_ready = 0;
                data_conn->eod = GLOBUS_FALSE;
                data_conn->reusing = GLOBUS_TRUE;

                stripe->connection_count++;
                /* register a header read */
                res = globus_io_register_read(
                          &data_conn->io_handle,
                          (globus_byte_t *)eb_header,
                          sizeof(globus_l_ftp_eb_header_t),
                          sizeof(globus_l_ftp_eb_header_t),
                          globus_l_ftp_eb_read_header_callback,
                          (void *)data_conn);
                globus_assert(res == GLOBUS_SUCCESS);

                if(callback != GLOBUS_NULL && register_onshot)
                {
                    register_onshot = GLOBUS_FALSE;
                    transfer_handle->ref++;

                    connect_cb_info = (globus_l_ftp_dc_connect_cb_info_t *)
                      globus_malloc(sizeof(globus_l_ftp_dc_connect_cb_info_t));
                    connect_cb_info->callback = callback;
                    connect_cb_info->stripe_ndx = stripe->stripe_ndx;
                    connect_cb_info->dc_handle = dc_handle;
                    connect_cb_info->user_arg = user_arg;
                    connect_cb_info->transfer_handle = transfer_handle;

                    /* register a on shot for connection */
                    GlobusTimeReltimeSet(reltime, 0, 0);
                    globus_callback_register_oneshot(
                        GLOBUS_NULL,
                        &reltime,
                        globus_l_ftp_control_reuse_connect_callback,
                        (void *) connect_cb_info);
                }
            }
        }
        dc_handle->state = GLOBUS_FTP_DATA_STATE_CONNECT_READ;
    }
    /*
     *  if we are creating new data connections
     */
    else if(dc_handle->state == GLOBUS_FTP_DATA_STATE_PASV ||
       reusing)
    {
        for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
        {
            stripe = &transfer_handle->stripes[ctr];

            /* add a reference for the listener */
            transfer_handle->ref++;
            DATA_CONN_MALLOC(data_conn, stripe, callback, user_arg);

            if(callback != GLOBUS_NULL)
            {
                transfer_handle->ref++;
            }
            result = globus_io_tcp_register_listen(
                          &stripe->listener_handle,
                          globus_l_ftp_eb_listen_callback,
                          (void *)data_conn);
            if(result != GLOBUS_SUCCESS)
            {
                globus_free(data_conn);
                return result;
            }
        }
        dc_handle->state = GLOBUS_FTP_DATA_STATE_CONNECT_READ;
        transfer_handle->direction = GLOBUS_FTP_DATA_STATE_CONNECT_READ;
    }
    else
    {
        err = globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("[%s]:%s Handle not in the proper state"),
                      GLOBUS_FTP_CONTROL_MODULE->module_name,
                      my_name);
        return globus_error_put(err);
    }

    return result;
}

globus_result_t
globus_l_ftp_control_data_stream_connect_direction(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_control_data_connect_callback_t  callback,
    void *                                      user_arg,
    globus_ftp_data_connection_state_t          direction)
{
    globus_result_t                             result = GLOBUS_SUCCESS;
    int                                         ctr;
    globus_ftp_data_stripe_t *                  stripe;
    globus_ftp_data_connection_t *              data_conn;
    globus_l_ftp_data_callback_info_t *         callback_info;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_object_t *                           err;
    static char *                               my_name =
        "globus_l_ftp_control_data_stream_connect_direction";

    /*
     *  make sure that we still exist
     */
    if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING ||
       dc_handle->state == GLOBUS_FTP_DATA_STATE_NONE)
    {
        err = globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                _FCSL("stream_connect_direction(): Handle not in the proper state"));
        globus_error_put(err);
    }

    transfer_handle = dc_handle->transfer_handle;
    /*
     *  in stream mode there must be exactly 1 stripe
     */
    if(transfer_handle->stripe_count != 1)
    {
        err =  globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
               _FCSL("[%s]:%s() stripe count does not equal 1."),
                      GLOBUS_FTP_CONTROL_MODULE->module_name,
                      my_name);
        globus_error_put(err);
    }
    if(dc_handle->parallel.base.size != 1)
    {
        err = globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
          _FCSL("[%s]:%s(): requesting parallelism in stream mode is not valid."),
                      GLOBUS_FTP_CONTROL_MODULE->module_name,
                      my_name);
        globus_error_put(err);
    }

    if(dc_handle->state == GLOBUS_FTP_DATA_STATE_PORT)
    {
        for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
        {
            stripe = &transfer_handle->stripes[ctr];

            result = globus_l_ftp_control_data_register_connect(
                         dc_handle,
                         stripe,
                         callback,
                         user_arg);
            if(result != GLOBUS_SUCCESS)
            {
                return result;
            }
            /*
             *  if there is a callback inc the reference
             *  count once per stripe
             */
            if(callback != GLOBUS_NULL)
            {
                transfer_handle->ref++;
            }
        }
        transfer_handle->direction = direction;
        dc_handle->state = direction;
    }
    else if(dc_handle->state == GLOBUS_FTP_DATA_STATE_PASV)
    {
        for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
        {
            stripe = &transfer_handle->stripes[ctr];

            /*
             *  inc the reference count for the listener callback
             */
            transfer_handle->ref++;

            DATA_CONN_MALLOC(data_conn, stripe, callback, user_arg);

            CALLBACK_INFO_MALLOC(
                callback_info,
                dc_handle,
                transfer_handle,
                stripe,
                data_conn);
            result = globus_io_tcp_register_listen(
                          &stripe->listener_handle,
                          globus_l_ftp_stream_listen_callback,
                          (void *)callback_info);
            if(result != GLOBUS_SUCCESS)
            {
                globus_free(callback_info);
                return result;
            }
            /*
             *  if there is a callback inc the reference
             *  count once per stripe
             */
            if(callback != GLOBUS_NULL)
            {
                transfer_handle->ref++;
            }
        }
        dc_handle->state = direction;
        transfer_handle->direction = direction;
    }
    else
    {
        result = globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("stream_connect_direction(): must call local_pasv/port first.")));
    }

    return result;
}

/**
 * @brief Add data channels
 * @ingroup globus_ftp_control_data
 * @details
 * Opens additional data channels (connections) to the host identified
 * by the stripe parameter.
 *
 * @param handle
 *        A pointer to a FTP control handle. This handle is used to
 *        determine the host corresponding to the stripe number and to
 *        store information about any channels added by this function.
 * @param num_channels
 *        The number of additional channels to add.
 * @param stripe_ndx
 *        A integer identifying the stripe to add channels too. In the
 *        case of non-striped transfer this parameter will be ignored.
 */
globus_result_t
globus_ftp_control_data_add_channels(
    globus_ftp_control_handle_t *               handle,
    unsigned int                                num_channels,
    unsigned int                                stripe_ndx)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_result_t                             res = GLOBUS_SUCCESS;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_data_add_channels";

    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        res = globus_error_put(globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  _FCSL("[%s]:%s() : not yet implemented."),
                  GLOBUS_FTP_CONTROL_MODULE->module_name,
                  myname));
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return res;
}

/**
 * @brief Send EOF
 * @ingroup globus_ftp_control_data
 * @details
 * Sends an EOF message to each stripe along an open data connection.
 *
 * @param handle
 *        A pointer to a FTP control handle.  This handle contains 
 *        the state for a connection.
 * @param  count[]
 *        This array of integers should contain an integer that
 *        will be added to the current parallel data connection count on
 *        each stripe.  The order of the integers corresponds to each
 *        stripe in the same order as what was returned from local_port().
 *
 *        An EOF message must be sent to all receiving hosts in a transfer.
 *        The message contains the total number of data connections used
 *        by each stripe.  Many stripes may be sending to a single receiver
 *        but only one EOF message may be sent.  The count parameter allows
 *        the user to pass in the total number of data connections used by all
 *        other hosts.  The local values are added to the passed in values
 *        and then sent to the receiver.
 *
 * @param array_size
 *        The number of elements in count[].
 * @param eof_message
 * @param cb
 *        The function to be called when the eof message has been called.
 * @param user_arg
 *        A user pointer that is threaded through to the user callback.
 */
globus_result_t
globus_ftp_control_data_send_eof(
    globus_ftp_control_handle_t *                  handle,
    int                                            count[],
    int                                            array_size,
    globus_bool_t                                  eof_message,
    globus_ftp_control_callback_t                  cb,
    void *                                         user_arg)
{
    globus_l_ftp_send_eof_entry_t *                eof_ent;
    globus_l_ftp_send_eof_entry_t *                tmp_ent;
    globus_i_ftp_dc_handle_t *                     dc_handle;
    globus_ftp_data_stripe_t *                     stripe;
    globus_object_t *                              err;
    globus_i_ftp_dc_transfer_handle_t *            transfer_handle;
    int                                            ctr;
    static char *                                  myname=
                                        "globus_ftp_control_data_send_eof";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(count == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "count",
                  2,
                  myname);
        return globus_error_put(err);
    }
    if(cb == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "cb",
                  4,
                  myname);
        return globus_error_put(err);
    }

    transfer_handle = dc_handle->transfer_handle;

    globus_mutex_lock(&dc_handle->mutex);
    {
        err = GLOBUS_NULL;
        if(dc_handle->transfer_handle == GLOBUS_NULL)
        {
            err = dc_handle->connect_error
                ? globus_object_copy(dc_handle->connect_error)
                : globus_error_construct_string(
                          GLOBUS_FTP_CONTROL_MODULE,
                          GLOBUS_NULL,
                    _FCSL("[%s]:%s() : Handle not in the proper state"),
                    GLOBUS_FTP_CONTROL_MODULE->module_name,
                    myname);
        }
        else if(dc_handle->mode != GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK)
        {
            err = globus_error_construct_string(
                       GLOBUS_FTP_CONTROL_MODULE,
                       GLOBUS_NULL,
   _FCSL("globus_ftp_control_data_send_eof() can only be called when in extended block mode"));
        }
        else if(dc_handle->state != GLOBUS_FTP_DATA_STATE_SEND_EOF &&
           dc_handle->state != GLOBUS_FTP_DATA_STATE_CONNECT_WRITE)
        {
            err = dc_handle->connect_error
                ? globus_object_copy(dc_handle->connect_error)
                : globus_error_construct_string(
                       GLOBUS_FTP_CONTROL_MODULE,
                       GLOBUS_NULL,
   _FCSL("globus_ftp_control_data_send_eof() handle not in proper state %s"), 
    globus_l_ftp_control_state_to_string(dc_handle->state));
        }
        else if(!transfer_handle->eof_registered)
        {
            err = globus_error_construct_string(
                       GLOBUS_FTP_CONTROL_MODULE,
                       GLOBUS_NULL,
   _FCSL("globus_ftp_control_data_send_eof() can only be sent after eof has been registered"));
        }
        else if(dc_handle->send_eof)
        {
            
            err =  globus_error_construct_string(
                       GLOBUS_FTP_CONTROL_MODULE,
                       GLOBUS_NULL,
   _FCSL("globus_ftp_control_data_send_eof() : The current handle is set to automatically send eof.  Pass GLOBUS_FALSE to globus_ftp_control_local_send_eof()."));
        }
        
        if(err)
        {
            globus_mutex_unlock(&dc_handle->mutex);
            return globus_error_put(err);
        }


        /*
         *  if we are not sending EOF we still need to send an EOD message
         */
        if(!eof_message)
        {
            for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
            {
                stripe = &transfer_handle->stripes[ctr];
                count[ctr] = stripe->total_connection_count;
            }
        }

        eof_ent = (globus_l_ftp_send_eof_entry_t *)
                      globus_malloc(sizeof(globus_l_ftp_send_eof_entry_t));
        eof_ent->count = (int *)globus_malloc(sizeof(int) * array_size);
        memcpy(eof_ent->count, count, sizeof(int) * array_size);
        eof_ent->array_size = array_size;
        eof_ent->cb = cb;
        eof_ent->user_arg = user_arg;
        eof_ent->direction = GLOBUS_FTP_DATA_STATE_SEND_EOF;
        eof_ent->dc_handle = &handle->dc_handle;
        eof_ent->transfer_handle = transfer_handle;

        /*
         *  1 count for each stripe
         */
        eof_ent->callback_table_handle = globus_handle_table_insert(
            &transfer_handle->handle_table,
            (void *)eof_ent,
            transfer_handle->stripe_count);

        for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
        {
            stripe = &transfer_handle->stripes[ctr];

            if(stripe->eof_sent)
            {
                globus_mutex_unlock(&dc_handle->mutex);
                err =  globus_error_construct_string(
                           GLOBUS_FTP_CONTROL_MODULE,
                           GLOBUS_NULL,
   _FCSL("globus_ftp_control_data_send_eof() : eof has already been sent on a stripe."));
                globus_mutex_unlock(&dc_handle->mutex);

                return globus_error_put(err);
            }

            tmp_ent = (globus_l_ftp_send_eof_entry_t *)
                          globus_malloc(sizeof(globus_l_ftp_send_eof_entry_t));
            tmp_ent->count = (int *)globus_malloc(sizeof(int) * array_size);
            memcpy(tmp_ent->count, count, sizeof(int) * array_size);
            tmp_ent->array_size = array_size;
            tmp_ent->cb = cb;
            tmp_ent->user_arg = user_arg;
            tmp_ent->direction = GLOBUS_FTP_DATA_STATE_SEND_EOF;
            tmp_ent->dc_handle = &handle->dc_handle;
            tmp_ent->callback_table_handle = eof_ent->callback_table_handle;
            tmp_ent->eof_message = eof_message;
            tmp_ent->transfer_handle = transfer_handle;

            globus_fifo_enqueue(&stripe->command_q,
                                (void *)tmp_ent);
        }
        globus_l_ftp_data_stripe_poll(dc_handle);
    }
    globus_mutex_unlock(&dc_handle->mutex);


    return GLOBUS_SUCCESS;
}

globus_result_t
globus_l_ftp_control_data_send_eof(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_data_connection_t *              data_conn,
    globus_l_ftp_send_eof_entry_t *             eof_ent)
{
    globus_l_ftp_eb_header_t *                  eb_header;
    globus_ftp_data_stripe_t *                  stripe;
    globus_result_t                             res;
    int                                         ctr;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;

    transfer_handle = dc_handle->transfer_handle;
    stripe = data_conn->whos_my_daddy;
    /*
     *  add our data connection count
     */
    ctr = 0;

    globus_assert(stripe->eof);

    if(stripe->eof_sent)
    {
        return globus_error_put(GLOBUS_ERROR_NO_INFO);
    }

    stripe->eof_sent = GLOBUS_TRUE;

    eb_header = (globus_l_ftp_eb_header_t *)
        globus_malloc(sizeof(globus_l_ftp_eb_header_t));
    memset(eb_header, '\0', sizeof(globus_l_ftp_eb_header_t));
    eb_header->descriptor =
        GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_EOD;

    if(eof_ent->eof_message)
    {
        eb_header->descriptor |= GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_EOF;
        globus_l_ftp_control_data_encode(
            eb_header->offset,
            stripe->total_connection_count + eof_ent->count[ctr]);
    }

    stripe->connection_count--;
    transfer_handle->ref++;
    res = globus_io_register_write(
              &data_conn->io_handle,
              (globus_byte_t *)eb_header,
              sizeof(globus_l_ftp_eb_header_t),
              globus_l_ftp_eb_send_eof_callback,
              (void *)eof_ent);

    return res;
}

/**
 * @brief Remove Data Channels
 * @ingroup globus_ftp_control_data
 * @details
 * Removes data channels (connections) to the host identified by the
 * stripe parameter.
 *
 * @param handle
 *        A pointer to a FTP control handle. This handle is used to
 *        determine the host corresponding to the stripe number and to
 *        update information about any channels removed by this function.
 * @param num_channels
 *        The number of channels to remove.
 * @param stripe_ndx
 *        A integer identifying the stripe to remove channels from. In the
 *        case of non-striped transfer this parameter will be ignored.
 */
globus_result_t
globus_ftp_control_data_remove_channels(
    globus_ftp_control_handle_t *               handle,
    unsigned int                                num_channels,
    unsigned int                                stripe_ndx)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_ftp_data_stripe_t *                  stripe;
    globus_result_t                             res = GLOBUS_SUCCESS;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;

    globus_object_t *                           err;
    static char *                               myname=
                                  "globus_ftp_control_data_remove_channels";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    transfer_handle = dc_handle->transfer_handle;

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(stripe_ndx >= transfer_handle->stripe_count)
        {
            res = globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      "Invalid Stripe index."));
        }
        else
        {
            stripe = &transfer_handle->stripes[stripe_ndx];
            if(stripe->parallel.base.mode ==
                               GLOBUS_FTP_CONTROL_PARALLELISM_FIXED)
            {
                if(stripe->parallel.base.size < 2)
                {
                    res = globus_error_put(globus_error_construct_string(
                              GLOBUS_FTP_CONTROL_MODULE,
                              GLOBUS_NULL,
                              _FCSL("It is invalid to set the number of data channels to zero.")));
                }
                else
                {
                    stripe->parallel.base.size--;
                }
            }
            else
            {
                res = globus_error_put(globus_error_construct_string(
                          GLOBUS_FTP_CONTROL_MODULE,
                          GLOBUS_NULL,
                          _FCSL("Cannot remove a channel on current parallel mode.")));
            }
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return res;
}

/**
 * @brief Number of data channels
 * @ingroup globus_ftp_control_data
 * @details
 * Returns the number of currently open channels for the host
 * identified by the stripe parameter.  This number may be less then
 * the level of parallelism specified in local_parallelism, due to
 * the possibility that some channels have not yet connected.
 *
 * @param handle
 *        A pointer to a FTP control handle. This handle is used to
 *        determine the host corresponding to "stripe" and number of
 *        channels corresponding to that host.
 * @param num_channels
 * @param stripe_ndx
 *        A integer identifying the stripe for which to return the
 *        number of channels. In the case of non-striped transfer this
 *        parameter should be zero.
 */
globus_result_t
globus_ftp_control_data_query_channels(
    globus_ftp_control_handle_t *		handle,
    unsigned int *				num_channels,
    unsigned int                                stripe_ndx)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_ftp_data_stripe_t *                  stripe;
    globus_result_t                             res = GLOBUS_SUCCESS;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;

    globus_object_t *                           err;
    static char *                               myname=
                                  "globus_ftp_control_data_query_channels";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(num_channels == GLOBUS_NULL)
    {
        err =  globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                _FCSL("number of channels must not ne a null pointer"));
        return globus_error_put(err);
    }

    transfer_handle = dc_handle->transfer_handle;

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(stripe_ndx >= transfer_handle->stripe_count)
        {
            res = globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      "Invalid Stripe index."));
        }
        else
        {
            stripe = &transfer_handle->stripes[stripe_ndx];
            *num_channels = stripe->connection_count;
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return res;
}

/**
 * @brief Get Total Data Channels For a Stripe
 * @ingroup globus_ftp_control_data
 * @details
 * Returns the total number of data channels used so far in the
 * current transfer on the given stripe.
 *
 * @param handle
 *        A pointer to a FTP control handle. This handle is used to
 *        determine the host corresponding to "stripe" and number of
 *        channels corresponding to that host.
 * @param num_channels
 *
 * @param stripe_ndx
 *        A integer identifying the stripe for which to return the
 *        number of channels. In the case of non-striped transfer this
 *        parameter should be zero.
 */
globus_result_t
globus_ftp_control_data_get_total_data_channels(
    globus_ftp_control_handle_t *		handle,
    unsigned int *				num_channels,
    unsigned int                                stripe_ndx)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_ftp_data_stripe_t *                  stripe;
    globus_result_t                             res = GLOBUS_SUCCESS;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_object_t *                           err;
    static char *                               myname=
                          "globus_ftp_control_data_get_total_data_channels";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(num_channels == GLOBUS_NULL)
    {
        err =  globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                _FCSL("number of channels must not ne a null pointer"));
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        transfer_handle = dc_handle->transfer_handle;

        if(transfer_handle == GLOBUS_NULL)
        {
            *num_channels = 0;
            res = globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("handle not in proper state.")));
        }
        else if(stripe_ndx >= transfer_handle->stripe_count)
        {
            *num_channels = 0;
            res = globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("Invalid Stripe index.")));
        }
        else
        {
            stripe = &transfer_handle->stripes[stripe_ndx];
            *num_channels = stripe->total_connection_count;
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return res;
}


globus_result_t
globus_ftp_control_data_get_remote_hosts(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_host_port_t *            address,
    int *                                       addr_count)
{
    globus_object_t *                           err;
    globus_result_t                             res = GLOBUS_SUCCESS;
    globus_list_t *                             list;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_ftp_data_stripe_t *                  stripe;
    globus_ftp_data_connection_t *              data_conn;
    int                                         ctr;
    int                                         ndx;
    int                                         count;
    static char *                               myname=
                          "globus_ftp_control_data_get_remote_hosts";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(address == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "address",
                  2,
                  myname);
        return globus_error_put(err);
    }
    if(addr_count == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "addr_count",
                  3,
                  myname);
        return globus_error_put(err);
    }
    if(*addr_count < 1)
    {
        res = globus_error_put(globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  _FCSL("*addr_count is less than 1.")));
        return res;
    }


    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        transfer_handle = dc_handle->transfer_handle;

        if(transfer_handle == GLOBUS_NULL)
        {
            res = globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("handle not in proper state.")));
            globus_mutex_unlock(&dc_handle->mutex);
            return res;
        }

        /* count the total # of connections */
        count = 0;
        for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
        {
            count += globus_list_size(transfer_handle->stripes[ctr].all_conn_list);
        }

        ndx = 0;
        for(ctr = 0; ctr < transfer_handle->stripe_count &&
                     ndx < *addr_count; ctr++)
        {
            stripe = &transfer_handle->stripes[ctr];
            for(list = stripe->all_conn_list;
                !globus_list_empty(list) && ndx < *addr_count;
                list = globus_list_rest(list))
            {
                data_conn = (globus_ftp_data_connection_t *)
                                 globus_list_first(list);
                res = globus_io_tcp_get_remote_address_ex(
                          &data_conn->io_handle,
                          address[ndx].host,
                          &address[ndx].hostlen,
                          &address[ndx].port);
                if(res != GLOBUS_SUCCESS)
                {
                    globus_mutex_unlock(&dc_handle->mutex);
                    return res;
                }
                ndx++;
            }
        }
        *addr_count = ndx;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return res;
}

/**
 * @brief Enable or disable automatic EOF handling
 * @ingroup globus_ftp_control_data
 * @details
 * Determines if the library will automatically send an EOF message in
 * extended block mode, or if the user will have to explicitly do it
 * by calling globus_ftp_control_data_send_eof().
 *
 * @param handle
 *        The ftp handle you wish to sent the send_eof attribute on.
 *
 * @param send_eof
 *        A boolean representing whether or not to automatically send an
 *        EOF message.
 */
globus_result_t
globus_ftp_control_local_send_eof(
    globus_ftp_control_handle_t *		handle,
    globus_bool_t                               send_eof)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_local_send_eof";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        dc_handle->send_eof = send_eof;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}
/* globus_ftp_control_local_send_eof() */

globus_result_t
globus_ftp_control_get_parallelism(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_parallelism_t *	        parallelism)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_get_parallelism";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(parallelism == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "parallelism",
                  2,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        globus_i_ftp_parallelism_copy(
            parallelism,
            &dc_handle->parallel);
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}

/**
 * @brief Set control handle parallelism
 * @ingroup globus_ftp_control_data
 * @details
 * Set the parallelism information in a FTP control handle
 *
 * @param handle
 *        A pointer to the FTP control handle for which the
 *        parallelism information is to be updated
 * @param parallelism
 *        A structure containing parallelism information
 *
 */
globus_result_t
globus_ftp_control_local_parallelism(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_parallelism_t *	        parallelism)
{
    int                                         ctr;
    globus_ftp_data_stripe_t *                  stripe;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    static char *                               myname=
                                      "globus_ftp_control_local_parallelism";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(parallelism == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "parallelism",
                  2,
                  myname);
        return globus_error_put(err);
    }

    transfer_handle = dc_handle->transfer_handle;

    globus_mutex_lock(&dc_handle->mutex);
    {
        globus_i_ftp_parallelism_copy(
            &dc_handle->parallel,
            parallelism);

        for(ctr = 0; transfer_handle != GLOBUS_NULL &&
            ctr < transfer_handle->stripe_count; ctr++)
        {
            stripe = &transfer_handle->stripes[ctr];

            globus_i_ftp_parallelism_copy(
                &stripe->parallel,
                &dc_handle->parallel);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}
/* globus_ftp_control_local_parallelism() */

/**
 * @brief Create a passive socket
 * @ingroup globus_ftp_control_data
 * @details
 * Create a local listening socket, bind it and return the address the
 * socket is listening to. If there is a existing data connection it
 * is closed.
 *
 * @param handle
 *        A pointer to a FTP control handle. Information about the
 *        listening socket is stored in the handle.
 * @param address
 *        The host IP address and port is returned through this
 *        parameter.
 */
globus_result_t
globus_ftp_control_local_pasv(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_host_port_t *            address)
{
    globus_result_t                             result = GLOBUS_SUCCESS;
    globus_result_t                             res = GLOBUS_SUCCESS;
    globus_ftp_data_stripe_t *                  stripe;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_local_pasv";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    if(address == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "address",
                  2,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        /*
         *  this function cannot be called during transfer
         */
        if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING ||
           dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_READ ||
           dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_WRITE)
        {
            globus_mutex_unlock(&dc_handle->mutex);
            return globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("globus_ftp_control_local_pasv(): Handle not in the proper state %s."), 
                      globus_l_ftp_control_state_to_string(dc_handle->state)));
        }

        /*  close all open data connections */
        globus_l_ftp_control_stripes_destroy(
            dc_handle,
            GLOBUS_NULL);

        globus_l_ftp_control_stripes_create(
            dc_handle,
            address,
            1);
        stripe = &dc_handle->transfer_handle->stripes[0];
        
        result = globus_io_tcp_create_listener(
            &address->port,
            -1,
            &dc_handle->io_attr,
            &stripe->listener_handle);
        
        if(result == GLOBUS_SUCCESS)
        {
            dc_handle->transfer_handle->ref++;
            stripe->listening = GLOBUS_TRUE;
            address->hostlen = 4;
            if(address->host[0] == 0 &&
               address->host[1] == 0 &&
               address->host[2] == 0 &&
               address->host[3] == 0 &&
               handle->cc_handle.cc_state == GLOBUS_FTP_CONTROL_CONNECTED)
            {
                unsigned short          p;
                char *                  dataip;
                
                if((dataip = getenv("GLOBUS_FTP_CLIENT_DATA_IP")) != NULL)
                {
                    result = globus_libc_contact_string_to_ints(
                        dataip, address->host, &address->hostlen, NULL);
                    if(result != GLOBUS_SUCCESS)
                    {
                        return result;
                    }
                }
                else
                {                    
                    res = globus_io_tcp_get_local_address_ex(
                              &handle->cc_handle.io_handle,
                              address->host,
                              &address->hostlen,
                              &p);
                    if(res != GLOBUS_SUCCESS)
                    {
                        char *              cs;
                        globus_sockaddr_t   addr;
                        
                        GlobusLibcSockaddrSetFamily(addr, AF_INET);
                        GlobusLibcSockaddrSetPort(addr, address->port);
                        result = globus_libc_addr_to_contact_string(
                            &addr,
                            GLOBUS_LIBC_ADDR_LOCAL | 
                            GLOBUS_LIBC_ADDR_NUMERIC |
                            GLOBUS_LIBC_ADDR_IPV4,
                            &cs);
                        if(result != GLOBUS_SUCCESS)
                        {
                            return result;
                        }
                        result = globus_libc_contact_string_to_ints(
                            cs, address->host, &address->hostlen, NULL);
                        globus_free(cs);
                        if(result != GLOBUS_SUCCESS)
                        {
                            return result;
                        }
                    }
                }
           }
    
           dc_handle->state = GLOBUS_FTP_DATA_STATE_PASV;
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return result;
}
/* globus_ftp_control_local_pasv() */

/**
 * @brief Create multiple passive sockets
 * @ingroup globus_ftp_control_data
 * @details
 * Create num_addresses local listening sockets, bind them and return
 * the addresses the sockets are listening to. If there is a existing
 * data connection it is closed.
 *
 * @param handle
 *        A pointer to a FTP control handle. Information about the
 *        listening sockets is stored in the handle.
 * @param addresses
 *        The host IP addresses and ports are returned through this
 *        parameter.
 * @param num_addresses
 *        The number of listening sockets to create
 */
globus_result_t
globus_ftp_control_local_spas(
    globus_ftp_control_handle_t *            handle,
    globus_ftp_control_host_port_t           addresses[],
    unsigned int                             num_addresses)
{
    return globus_error_put(
              globus_error_construct_string(
              GLOBUS_FTP_CONTROL_MODULE,
              GLOBUS_NULL,
              _FCSL("globus_ftp_control_local_spas(): this function is not implemented")));
}
/* globus_ftp_control_local_spas() */

/**
 * @brief Set remote data address
 * @ingroup globus_ftp_control_data
 * @details
 * Insert the host/port information returned by a PASV on the remote
 * host into the local FTP control handle. (close any outstanding data
 * con)
 *
 * @param handle
 *        A pointer to the FTP control handle into which to insert the
 *        host/port information
 * @param address
 *        The host IP address and port
 */
globus_result_t
globus_ftp_control_local_port(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_host_port_t *            address)
{
    globus_result_t                             result = GLOBUS_SUCCESS;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_local_port";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(address == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "address",
                  2,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        /*
         *  this function cannot be called during transfer
         */
        if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING ||
           dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_READ ||
           dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_WRITE)
        {
            globus_mutex_unlock(&dc_handle->mutex);
            return globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("globus_ftp_control_local_port(): Handle not in the proper state %s."), 
                      globus_l_ftp_control_state_to_string(dc_handle->state)));
        }

        /*  close all open data connections */
        globus_l_ftp_control_stripes_destroy(
            dc_handle,
            GLOBUS_NULL);

        globus_l_ftp_control_stripes_create(
            dc_handle,
            address,
            1);

        dc_handle->state = GLOBUS_FTP_DATA_STATE_PORT;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return result;
}
/* globus_ftp_control_local_port() */

globus_result_t
globus_ftp_control_get_spor(
    globus_ftp_control_handle_t *	             handle,
    globus_ftp_control_host_port_t                   addresses[],
    unsigned int *                                   num_addresses)
{
    globus_i_ftp_dc_handle_t *                       dc_handle;
    int                                              ctr;
    globus_object_t *                                err;
    static char *                                    myname=
                                      "globus_ftp_control_get_spor";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(addresses == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "addresses",
                  2,
                  myname);
        return globus_error_put(err);
    }

    if(*num_addresses < 1)
    {
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  _FCSL("globus_ftp_control_local_pasv(): address count is less than 1."));
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(dc_handle->transfer_handle == GLOBUS_NULL)
        {
            *num_addresses = 0;
        }
        else
        {
            for(ctr = 0;
                ctr < *num_addresses &&
                  ctr < dc_handle->transfer_handle->stripe_count;
                ctr++)
            {
                globus_ftp_control_host_port_copy(
                    &addresses[ctr],
                    &dc_handle->transfer_handle->stripes[ctr].host_port);
            }
            *num_addresses = ctr;
        }
    }
    globus_mutex_lock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}

/**
 * @brief Set remote data addresses
 * @ingroup globus_ftp_control_data
 * @details
 * Insert the host/port addresses returned by a SPAS on the remote
 * host into the local FTP control handle. If there are any
 * outstanding data connections at this point, they are closed.
 *
 * @param handle
 *        A pointer to the FTP control handle into which to insert the
 *        host/port addresses
 * @param addresses
 *        The host IP addresses and port numbers
 * @param num_addresses
 *        The number of addresses
 */
globus_result_t
globus_ftp_control_local_spor(
        globus_ftp_control_handle_t *	             handle,
	globus_ftp_control_host_port_t               addresses[],
	unsigned int                                 num_addresses)
{
    globus_i_ftp_dc_handle_t *                       dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_local_spor";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(addresses == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "addresses",
                  2,
                  myname);
        return globus_error_put(err);
    }

    if(num_addresses < 1)
    {
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  _FCSL("globus_ftp_control_local_pasv(): address count is less than 1."));
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        /*  close all open data connections */
        globus_l_ftp_control_stripes_destroy(
            dc_handle,
            GLOBUS_NULL);

        globus_l_ftp_control_stripes_create(
            dc_handle,
            addresses,
            num_addresses);

        dc_handle->state = GLOBUS_FTP_DATA_STATE_SPOR;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}

/**
 * @brief Set control handle TYPE value
 * @ingroup globus_ftp_control_data
 * @details
 * Update the FTP control handle with the given type information.
 *
 * @param handle
 *        A pointer to the FTP control handle to be updated
 * @param type
 *        The type of the data connection. Possible values are: ASCII,
 *        EBCDIC, IMAGE and LOCAL. Currently only ASCII and IMAGE
 *        types are supported.
 * @param form_code
 *        The logical byte size parameter for the LOCAL type.
 */
globus_result_t
globus_ftp_control_local_type(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_type_t                   type,
    int                                         form_code)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_local_spor";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    if(type != GLOBUS_FTP_CONTROL_TYPE_ASCII &&
       type != GLOBUS_FTP_CONTROL_TYPE_IMAGE)
    {
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
              _FCSL("globus_ftp_control_local_type(): Type must be ascii or image."));
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);

    globus_mutex_lock(&dc_handle->mutex);
    {
        dc_handle->type = type;
        dc_handle->form_code = form_code;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}
/* globus_ftp_control_local_type() */

globus_result_t
globus_ftp_control_get_type(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_type_t *                 type)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_get_type";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    globus_mutex_lock(&dc_handle->mutex);
    {
        *type = dc_handle->type;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;

}

globus_result_t
globus_ftp_control_get_mode(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_mode_t *                 mode)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_get_mode";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    globus_mutex_lock(&dc_handle->mutex);
    {
        *mode = dc_handle->mode;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}

/**
 * @brief Set data handle mode
 * @ingroup globus_ftp_control_data
 * @details
 * Update the FTP control handle with the given mode information.
 *
 * @param handle
 *        A pointer to the FTP control handle to be updated
 * @param mode
 *        Specifies the mode of the data connection. Possible modes
 *        are STREAM, BLOCK, EXTENDED BLOCK and COMPRESSED. Out of
 *        these only STREAM and EXTENDED BLOCK are supported in this
 *        implementation. Also, EXTENDED BLOCK is only supported in
 *        combination with the IMAGE type.
 *
 */
globus_result_t
globus_ftp_control_local_mode(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_mode_t                   mode)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_local_mode";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    /* only allowing STREAM and EBLOCK mode for now */
    if(mode != GLOBUS_FTP_CONTROL_MODE_STREAM &&
       mode != GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK)
    {
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
              _FCSL("globus_ftp_control_local_mode(): mode must be stream or extended block."));
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    globus_mutex_lock(&dc_handle->mutex);
    {
        dc_handle->mode = mode;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}

/**
 * @brief Set data handle to return read mode E data in order
 * @ingroup globus_ftp_control_data
 * @details
 * Update the FTP control handle forced data order flag.  Must be called
 * before globus_ftp_control_data_connect_read().
 *
 * @param handle
 *        A pointer to the FTP control handle to be updated
 * @param order_data
 *        GLOBUS_TRUE if data must be ordered, false if not.
 * @param starting_offset
 *        The starting offset to expect. May not be 0 for restarted transfers.
 *
 * Note that this may result in slower transfers, or, if 
 * the data arrives far out of order, failed transfers.
 */
globus_result_t
globus_ftp_control_set_force_order(
    globus_ftp_control_handle_t *               handle,
    globus_bool_t                               order_data,
    globus_off_t                                starting_offset)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_set_force_order";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(!dc_handle->transfer_handle)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "transfer_handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    globus_mutex_lock(&dc_handle->mutex);
    {
        dc_handle->transfer_handle->order_data = order_data;
        dc_handle->transfer_handle->order_next_offset = starting_offset; 
        dc_handle->transfer_handle->order_next_offset_passed = starting_offset; 
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;

}


/**
 * @brief Set data handle TCP buffer size
 * @ingroup globus_ftp_control_data
 * @details
 * Update the FTP control handle with the given socket buffer
 * information.
 *
 * @param handle
 *        A pointer to the FTP control handle to be updated
 * @param tcp_buffer
 *        A pointer to the socket buffer.
 *
 */
globus_result_t
globus_ftp_control_local_tcp_buffer(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_tcpbuffer_t *            tcp_buffer)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
	"globus_ftp_control_local_tcp_buffer";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    if(tcp_buffer->mode != GLOBUS_FTP_CONTROL_TCPBUFFER_FIXED)
    {
	err = globus_error_construct_string(
	    GLOBUS_FTP_CONTROL_MODULE,
	    GLOBUS_NULL,
	    _FCSL("globus_ftp_control_local_tcp_buffer(): buffer setting mode not supported"));
	return globus_error_put(err);
    }


    if( 0 > tcp_buffer->fixed.size )
    {
	err = globus_error_construct_string(
	    GLOBUS_FTP_CONTROL_MODULE,
	    GLOBUS_NULL,
	    _FCSL("globus_ftp_control_local_tcp_buffer(): buffer size must be greater than 0"));
	return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    globus_mutex_lock(&dc_handle->mutex);
    {
        dc_handle->tcp_buffer_size = tcp_buffer->fixed.size;
	if(dc_handle->tcp_buffer_size > 0)
	{
	    globus_io_attr_set_socket_sndbuf(
		&dc_handle->io_attr,
		dc_handle->tcp_buffer_size);
	    globus_io_attr_set_socket_rcvbuf(
		&dc_handle->io_attr,
		dc_handle->tcp_buffer_size);
	}
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_ftp_control_data_get_socket_buf(
    globus_ftp_control_handle_t *       handle,
    int *                               rcvbuf,
    int *                               sndbuf)
{
    globus_result_t                     res;
    globus_object_t *                   err;
    globus_ftp_data_connection_t *      data_conn;
    globus_ftp_data_stripe_t *          stripes;
    globus_i_ftp_dc_handle_t *          dc_handle;
    static char *                       my_name =
	    "globus_ftp_control_data_get_socket_buf";

    if(handle == NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  my_name);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;

    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  my_name);
        return globus_error_put(err);
    }
    if(dc_handle->transfer_handle == NULL)
    {
        err = globus_error_construct_string(
                           GLOBUS_FTP_CONTROL_MODULE,
                           GLOBUS_NULL,
                           _FCSL("[%s]:%s():transfer handle does not exist"),
                           GLOBUS_FTP_CONTROL_MODULE->module_name,
                           my_name);
        return globus_error_put(err);
    }
    stripes = dc_handle->transfer_handle->stripes;
    if(stripes == NULL)
    {
        err = globus_error_construct_string(
                           GLOBUS_FTP_CONTROL_MODULE,
                           GLOBUS_NULL,
                           _FCSL("[%s]:%s():transfer handle has no stripes."),
                           GLOBUS_FTP_CONTROL_MODULE->module_name,
                           my_name);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(globus_list_empty(stripes[0].all_conn_list))
        {
            globus_mutex_unlock(&dc_handle->mutex);
            err = globus_error_construct_string(
                           GLOBUS_FTP_CONTROL_MODULE,
                           GLOBUS_NULL,
                           _FCSL("[%s]:%s():no data connection."),
                           GLOBUS_FTP_CONTROL_MODULE->module_name,
                           my_name);
            return globus_error_put(err);
        }
        data_conn = (globus_ftp_data_connection_t *) globus_list_first(
            stripes[0].all_conn_list);
        if(data_conn == NULL)
        {
            globus_mutex_unlock(&dc_handle->mutex);
            err = globus_error_construct_string(
                           GLOBUS_FTP_CONTROL_MODULE,
                           GLOBUS_NULL,
                           _FCSL("[%s]:%s():no data connection."),
                           GLOBUS_FTP_CONTROL_MODULE->module_name,
                           my_name);
            return globus_error_put(err);
        }
        res = globus_io_handle_get_socket_buf(  
            &data_conn->io_handle,
            rcvbuf,
            sndbuf);
        if(res != GLOBUS_SUCCESS)
        {
            globus_mutex_unlock(&dc_handle->mutex);
            return res;
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}



globus_result_t
globus_ftp_control_data_get_retransmit_count(
    globus_ftp_control_handle_t *               handle,
    char **                                     retransmit_count)
{
    globus_object_t *                           err;
    globus_result_t                             res = GLOBUS_SUCCESS;
    globus_list_t *                             list;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_ftp_data_stripe_t *                  stripe;
    globus_ftp_data_connection_t *              data_conn;
    globus_xio_driver_t                         tcp_driver;
    int                                         ctr;
    int                                         count;
    char *                                      count_str = NULL;
    static char *                               myname=
                          "globus_ftp_control_data_get_retransmit_count";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(retransmit_count == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "retransmit_count",
                  3,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        transfer_handle = dc_handle->transfer_handle;

        if(transfer_handle == GLOBUS_NULL)
        {
            res = globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("handle not in proper state.")));
            globus_mutex_unlock(&dc_handle->mutex);
            return res;
        }

        tcp_driver = globus_io_compat_get_tcp_driver();
        
        for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
        {
            stripe = &transfer_handle->stripes[ctr];
            for(list = stripe->all_conn_list;
                !globus_list_empty(list);
                list = globus_list_rest(list))
            {
                globus_xio_handle_t         xio_handle;
                globus_xio_system_socket_t  socket;
                char *                      tmp_str;

                data_conn = (globus_ftp_data_connection_t *)
                                 globus_list_first(list);

                res = globus_io_handle_get_xio_handle(
                    &data_conn->io_handle, &xio_handle);
                if(res != GLOBUS_SUCCESS)
                {
                    globus_mutex_unlock(&dc_handle->mutex);
                    return res;
                }

                res = globus_xio_handle_cntl(
                    xio_handle,
                    tcp_driver,
                    GLOBUS_XIO_TCP_GET_HANDLE,
                    &socket);
                if(res != GLOBUS_SUCCESS)
                {
                    globus_mutex_unlock(&dc_handle->mutex);
                    return res;
                }

                count = -1;
#if defined(TARGET_ARCH_LINUX) && defined(TCP_MD5SIG)
                {
                    struct tcp_info             tcpinfo;
                    socklen_t                   len;
    
                    len = sizeof(tcpinfo);
                    res = globus_xio_system_socket_getsockopt(
                        socket, IPPROTO_TCP, TCP_INFO, (void *) &tcpinfo, &len);
                    if(res != GLOBUS_SUCCESS)
                    {
                        globus_mutex_unlock(&dc_handle->mutex);
                        return res;
                    }
    
                    count = tcpinfo.tcpi_total_retrans;
                }
#endif

                if(count_str)
                {
                    tmp_str = globus_common_create_string("%s,%d", count_str, count);
                    globus_free(count_str);
                    count_str = tmp_str;
                }
                else
                {
                    count_str = globus_common_create_string("%d", count);
                }
            }
        }
        *retransmit_count = count_str;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return res;
}


/**
 * @brief Set data channel DCAU
 * @ingroup globus_ftp_control_data
 * @details
 * Update the FTP control handle with the given data channel
 * authentication information.
 *
 * If authentication is set to GLOBUS_FTP_CONTROL_DCAU_NONE,
 * then protection will also be disabled for this control handle.
 *
 * @param handle
 *        A pointer to the FTP control handle to be updated
 * @param dcau
 *        A parameter specifying the data channel authentication
 *        mode. Possible values are No Authentication, Self
 *        Authentication and Subject-name authentication.
 * @param delegated_credential_handle */
globus_result_t
globus_ftp_control_local_dcau(
    globus_ftp_control_handle_t *		handle,
    const globus_ftp_control_dcau_t *           dcau,
    gss_cred_id_t                               delegated_credential_handle)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    static char *                               myname=
	"globus_ftp_control_local_dcau";
    globus_object_t *                           err;

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    else if(dcau->mode != GLOBUS_FTP_CONTROL_DCAU_NONE &&
            dcau->mode != GLOBUS_FTP_CONTROL_DCAU_SELF &&
            dcau->mode != GLOBUS_FTP_CONTROL_DCAU_SUBJECT)
    {
        err = globus_error_construct_string(
		    GLOBUS_FTP_CONTROL_MODULE,
		    GLOBUS_NULL,
		    _FCSL("globus_ftp_control_local_dcau: invalid dcau mode"));
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
	if(dc_handle->dcau.mode == GLOBUS_FTP_CONTROL_DCAU_SUBJECT &&
	   dc_handle->dcau.subject.subject != GLOBUS_NULL)
	{
	    globus_libc_free(dc_handle->dcau.subject.subject);
	    dc_handle->dcau.subject.subject = GLOBUS_NULL;
	}
	dc_handle->dcau.mode = dcau->mode;
	if(dcau->mode == GLOBUS_FTP_CONTROL_DCAU_SUBJECT)
	{
	    dc_handle->dcau.subject.subject =
		globus_libc_strdup(dcau->subject.subject);
	}

	if(dc_handle->dcau.mode != GLOBUS_FTP_CONTROL_DCAU_NONE)
	{
	    globus_io_secure_authorization_data_t	auth_data;
	    globus_io_secure_authorization_data_initialize(&auth_data);

	    globus_io_attr_set_secure_authentication_mode(
		&dc_handle->io_attr,
		GLOBUS_IO_SECURE_AUTHENTICATION_MODE_MUTUAL,
		delegated_credential_handle);

            globus_io_attr_set_secure_proxy_mode(
                &dc_handle->io_attr,
		GLOBUS_IO_SECURE_PROXY_MODE_MANY);

	    switch(dc_handle->dcau.mode)
	    {
	      case GLOBUS_FTP_CONTROL_DCAU_SELF:
		globus_io_attr_set_secure_authorization_mode(
			&dc_handle->io_attr,
			GLOBUS_IO_SECURE_AUTHORIZATION_MODE_SELF,
			&auth_data);
		break;

	      case GLOBUS_FTP_CONTROL_DCAU_SUBJECT:
		globus_io_secure_authorization_data_set_identity(
			&auth_data,
			dc_handle->dcau.subject.subject);
		globus_io_attr_set_secure_authorization_mode(
			&dc_handle->io_attr,
			GLOBUS_IO_SECURE_AUTHORIZATION_MODE_IDENTITY,
			&auth_data);
		break;
	      default:
		break;
	    }

	    globus_io_secure_authorization_data_destroy(&auth_data);
	}
	else
	{
	    dc_handle->protection = GLOBUS_FTP_CONTROL_PROTECTION_CLEAR;

	    globus_io_attr_set_secure_channel_mode(
		    &dc_handle->io_attr,
		    GLOBUS_IO_SECURE_CHANNEL_MODE_CLEAR);

	    globus_io_attr_set_secure_authorization_mode(
		    &dc_handle->io_attr,
		    GLOBUS_IO_SECURE_AUTHORIZATION_MODE_NONE,
		    GLOBUS_NULL);

	    globus_io_attr_set_secure_authentication_mode(
		    &dc_handle->io_attr,
		    GLOBUS_IO_SECURE_AUTHENTICATION_MODE_NONE,
		    GLOBUS_NULL);
	}
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_ftp_control_get_dcau(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_dcau_t *           	dcau)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err = GLOBUS_SUCCESS;
    static char *                               myname=
                                      "globus_ftp_control_get_dcau";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
	dcau->mode = dc_handle->dcau.mode;
	if(dc_handle->dcau.mode == GLOBUS_FTP_CONTROL_DCAU_SUBJECT)
	{
	    dcau->subject.subject =
		globus_libc_strdup(dc_handle->dcau.subject.subject);
	    if(!dcau->subject.subject)
	    {
		err = globus_error_construct_string(
		    GLOBUS_FTP_CONTROL_MODULE,
		    GLOBUS_NULL,
		    _FCSL("globus_ftp_control_get_dcau: malloc failed"));
	    }
	}
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return err ? globus_error_put(err): GLOBUS_SUCCESS;
}
/* globus_ftp_control_get_dcau() */

globus_result_t
globus_ftp_control_local_prot(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_protection_t		protection)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    static char *                               myname=
	"globus_ftp_control_local_prot";
    globus_object_t *                           err;

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(dc_handle->dcau.mode == GLOBUS_FTP_CONTROL_DCAU_NONE)
    {
	err = globus_error_construct_string(
		GLOBUS_FTP_CONTROL_MODULE,
		GLOBUS_NULL,
		_FCSL("Cannot set protection without using dcau"));
	return globus_error_put(err);
    }
    if(dc_handle->protection == GLOBUS_FTP_CONTROL_PROTECTION_CONFIDENTIAL)
    {
	err = globus_error_construct_string(
		GLOBUS_FTP_CONTROL_MODULE,
		GLOBUS_NULL,
		_FCSL("\"Confidential\" protection level not supported with GSSAPI"));
	return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
	dc_handle->protection = protection;

	switch(dc_handle->protection)
	{
	  case GLOBUS_FTP_CONTROL_PROTECTION_CLEAR:
	    globus_io_attr_set_secure_channel_mode(
		    &dc_handle->io_attr,
		    GLOBUS_IO_SECURE_CHANNEL_MODE_CLEAR);
	    globus_io_attr_set_secure_protection_mode(
		    &dc_handle->io_attr,
		    GLOBUS_IO_SECURE_PROTECTION_MODE_NONE);
	    break;

	  case GLOBUS_FTP_CONTROL_PROTECTION_SAFE:
	    globus_io_attr_set_secure_channel_mode(
		    &dc_handle->io_attr,
		    GLOBUS_IO_SECURE_CHANNEL_MODE_SSL_WRAP);
	    globus_io_attr_set_secure_protection_mode(
		    &dc_handle->io_attr,
		    GLOBUS_IO_SECURE_PROTECTION_MODE_SAFE);
	    break;

	  case GLOBUS_FTP_CONTROL_PROTECTION_PRIVATE:
	    globus_io_attr_set_secure_channel_mode(
		    &dc_handle->io_attr,
		    GLOBUS_IO_SECURE_CHANNEL_MODE_SSL_WRAP);
	    globus_io_attr_set_secure_protection_mode(
		    &dc_handle->io_attr,
		    GLOBUS_IO_SECURE_PROTECTION_MODE_PRIVATE);
	    break;

	  default:
	    globus_assert(
		    dc_handle->protection
		        == GLOBUS_FTP_CONTROL_PROTECTION_CLEAR ||
		    dc_handle->protection
		        == GLOBUS_FTP_CONTROL_PROTECTION_SAFE ||
		    dc_handle->protection
		        == GLOBUS_FTP_CONTROL_PROTECTION_PRIVATE);
	    break;
	}
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}
/* globus_ftp_control_local_prot() */

globus_result_t
globus_ftp_control_get_prot(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_protection_t *		protection)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err = GLOBUS_SUCCESS;
    static char *                               myname=
                                      "globus_ftp_control_get_prot";
    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
	*protection = dc_handle->protection;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}
/* globus_ftp_control_get_prot() */

/**
 * @brief Set data channel protection buffer size
 * @ingroup globus_ftp_control_data
 * @details
 * Update the FTP control handle with the given protection buffer size
 * information.
 *
 * This function sets protection buffer size to be used by this handle. This
 * value is used to determine how much data will be sent in each packet
 * during a protected data transfer.
 *
 * @param handle
 *        A pointer to the FTP control handle to be updated
 * @param bufsize
 *        A parameter specifying the protection buffer size value.
 */
globus_result_t
globus_ftp_control_local_pbsz(
    globus_ftp_control_handle_t *		handle,
    unsigned long 				bufsize)
{
    globus_object_t *				error = GLOBUS_NULL;

    if(handle == GLOBUS_NULL)
    {
	error = globus_error_construct_string(
		GLOBUS_FTP_CONTROL_MODULE,
		GLOBUS_NULL,
		_FCSL("globus_ftp_control_local_pbsz: Handle argument is NULL"));
	goto error_exit;
    }
    /* pbsz must be <= 32 bits */
    if(bufsize != (bufsize & 0xffffffff))
    {
	error = globus_error_construct_string(
		GLOBUS_FTP_CONTROL_MODULE,
		GLOBUS_NULL,
		_FCSL("globus_ftp_control_local_pbsz: Invalid buffer size"));
	goto error_exit;
    }

    globus_mutex_lock(&(handle->dc_handle.mutex));
    {
	if(handle->dc_handle.pbsz != 0UL &&
	   handle->dc_handle.pbsz < bufsize)
	{
	    error = globus_error_construct_string(
		GLOBUS_FTP_CONTROL_MODULE,
		GLOBUS_NULL,
		_FCSL("globus_ftp_control_local_pbsz: Invalid buffer size"));

	    goto unlock_exit;
	}
	handle->dc_handle.pbsz = bufsize;
    }
unlock_exit:
    globus_mutex_unlock(&handle->dc_handle.mutex);
error_exit:
    return (error ? globus_error_put(error) : GLOBUS_SUCCESS);
}
/* globus_ftp_control_local_pbsz() */

/**
 * @brief Get data channel protection buffer size
 * @ingroup globus_ftp_control_data
 * @details
 * Query the FTP control handle for the protection buffer size
 * information.
 *
 * This function queries the handle to determine the protection buffer size
 * which is used by this handle. This value is used to determine how much data
 * will be sent in each packet during a protected data transfer.
 *
 * @param handle
 *        A pointer to the FTP control handle to be updated
 * @param bufsize
 *        A pointer to a parameter to store the value of the protection buffer
 *        size.
 */
globus_result_t
globus_ftp_control_get_pbsz(
    globus_ftp_control_handle_t *		handle,
    unsigned long *				bufsize)
{
    if(handle == GLOBUS_NULL)
    {
	return globus_error_put(
		globus_error_construct_string(
		    GLOBUS_FTP_CONTROL_MODULE,
		    GLOBUS_NULL,
		    _FCSL("globus_ftp_control_local_pbsz: Handle argument is NULL")));
    }
    if(bufsize == GLOBUS_NULL)
    {
	return globus_error_put(
		globus_error_construct_string(
		    GLOBUS_FTP_CONTROL_MODULE,
		    GLOBUS_NULL,
		    _FCSL("globus_ftp_control_local_pbsz: bufsize argument is NULL")));
    }
    globus_mutex_lock(&(handle->dc_handle.mutex));
    {
	*bufsize = handle->dc_handle.pbsz;
    }
    globus_mutex_unlock(&(handle->dc_handle.mutex));

    return GLOBUS_SUCCESS;
}
/* globus_ftp_control_get_pbsz() */

/**
 * @brief Set data channel structure
 * @ingroup globus_ftp_control_data
 * @details
 * Updates the handle with information on the structure of the data
 * being sent on the data channel.
 *
 * This function updates the handle with the provided structure
 * information. At this point the only structure type that is
 * supported is the file type.
 *
 * @param handle
 *        A pointer to a FTP control handle. The handle contains
 *        information about the current state of the control and data
 *        connections.
 * @param structure
 *        This parameter is used to pass the structure
 *        information. Possible values are file, record and page. Only
 *        the file type is supported
 *
 */
globus_result_t
globus_ftp_control_local_stru(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_structure_t		structure)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_local_stru";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    /* only file type allowed for now */
    if(structure != GLOBUS_FTP_CONTROL_STRUCTURE_FILE)
    {
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
              _FCSL("globus_ftp_control_local_structure(): Only file structure is supported."));
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    globus_mutex_lock(&dc_handle->mutex);
    {
        dc_handle->structure = structure;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}

/**
 * @brief Write data to data connections
 * @ingroup globus_ftp_control_data
 * @details
 * Writes data from the supplied buffer to data connection(s)
 *
 * This function writes contained in the buffer to the data
 * channel(s).
 *
 * @param handle
 *        A pointer to a FTP control handle. The handle contains
 *        information about the current state of the control and data
 *        connections.
 * @param buffer
 *        A user supplied buffer from which data will written to the
 *        data connection(s)
 * @param length
 *        The length of the data contained in the buffer.
 * @param offset
 *        The offset in the file at which the data in the buffer starts
 * @param eof
 *        Indicates that the buffer is that last part of a file. In
 *        the striped case this will cause a EOF block to be send to
 *        every data node involved in the transfer.
 * @param callback
 *        The function to be called once the data has been sent
 * @param callback_arg
 *        User supplied argument to the callback function
 *
 */
globus_result_t
globus_ftp_control_data_write(
    globus_ftp_control_handle_t *		handle,
    globus_byte_t *				buffer,
    globus_size_t				length,
    globus_off_t				offset,
    globus_bool_t				eof,
    globus_ftp_control_data_callback_t	        callback,
    void *					callback_arg)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_result_t                             result = GLOBUS_SUCCESS;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_data_write";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(buffer == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "buffer",
                  2,
                  myname);
        return globus_error_put(err);
    }
    if(callback == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "callback",
                  6,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        err = GLOBUS_NULL;
        if(dc_handle->transfer_handle == GLOBUS_NULL)
        {
            err = dc_handle->connect_error
                ? globus_object_copy(dc_handle->connect_error)
                : globus_error_construct_string(
                          GLOBUS_FTP_CONTROL_MODULE,
                          GLOBUS_NULL,
                    _FCSL("Handle not in the proper state"));
        }
        else if(dc_handle->state != GLOBUS_FTP_DATA_STATE_CONNECT_WRITE)
        {
            err = dc_handle->connect_error
                ? globus_object_copy(dc_handle->connect_error)
                : globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
         _FCSL("globus_ftp_control_data_write(): Handle not in proper state. %s"), 
         globus_l_ftp_control_state_to_string(dc_handle->state));
        }
        else if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_STREAM)
        {
            result = globus_l_ftp_control_data_stream_read_write(
                         dc_handle,
                         buffer,
                         length,
                         offset,
                         eof,
                         callback,
                         callback_arg);
        }
        else if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK)
        {
            result = globus_l_ftp_control_data_eb_write(
                         dc_handle,
                         buffer,
                         length,
                         offset,
                         eof,
                         callback,
                         callback_arg);
        }
        else
        {
            err = dc_handle->connect_error
                ? globus_object_copy(dc_handle->connect_error)
                : globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
         _FCSL("globus_ftp_control_data_write(): Handle not in proper state."));
        }
        
        if(err)
        {
            globus_mutex_unlock(&dc_handle->mutex);
            return globus_error_put(err);
        }
        globus_l_ftp_data_stripe_poll(dc_handle);
    }
    globus_mutex_unlock(&dc_handle->mutex);


    return result;
}

globus_result_t
globus_ftp_control_get_stripe_count(
    globus_ftp_control_handle_t *		handle,
    int *                                       stripe_count)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    static char *                               myname=
                                      "globus_ftp_control_get_stripe_count";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(dc_handle->transfer_handle == GLOBUS_NULL)
    {
        err = globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                _FCSL("Handle not in the proper state"));
        return globus_error_put(err);
    }

    transfer_handle = dc_handle->transfer_handle;

    globus_mutex_lock(&dc_handle->mutex);
    {
       *stripe_count = transfer_handle->stripe_count;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}

/**
 * @brief Read data from data connections
 * @ingroup globus_ftp_control_data
 * @details
 * Reads data from data connection(s) and put them in the supplied
 * buffer.
 *
 * This function takes the given buffer and will try to read data from
 * the data connection(s).
 *
 * @param handle
 *        A pointer to a FTP control handle. The handle contains
 *        information about the current state of the control and data
 *        connections.
 * @param buffer
 *        A user supplied buffer into which data from the data
 *        connection(s) will be written
 * @param max_length
 *        The maximum length of the data that can be written to the buffer
 * @param callback
 *        The function to be called once the data has been read
 * @param callback_arg
 *        User supplied argument to the callback function
 *
 */
globus_result_t
globus_ftp_control_data_read(
    globus_ftp_control_handle_t *		handle,
    globus_byte_t *				buffer,
    globus_size_t				max_length,
    globus_ftp_control_data_callback_t	        callback,
    void *					callback_arg)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_result_t                             result = GLOBUS_SUCCESS;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_data_read";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(buffer == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "buffer",
                  2,
                  myname);
        return globus_error_put(err);
    }
    if(callback == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "callback",
                  4,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        err = GLOBUS_NULL;
        
        if(dc_handle->transfer_handle == GLOBUS_NULL)
        {
            err = dc_handle->connect_error
                ? globus_object_copy(dc_handle->connect_error)
                : globus_error_construct_string(
                          GLOBUS_FTP_CONTROL_MODULE,
                          GLOBUS_NULL,
                    _FCSL("Handle not in the proper state:transfer handle == NULL"));
        }
        else if(dc_handle->state != GLOBUS_FTP_DATA_STATE_CONNECT_READ && 
            (dc_handle->state != GLOBUS_FTP_DATA_STATE_EOF ||
                   dc_handle->transfer_handle->direction != GLOBUS_FTP_DATA_STATE_CONNECT_READ))
        {
            err = dc_handle->connect_error
                ? globus_object_copy(dc_handle->connect_error)
                : globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
         _FCSL("globus_ftp_control_data_read(): Handle not in proper state %s."),
          globus_l_ftp_control_state_to_string(dc_handle->state));
        }
        else if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_STREAM)
        {
            result = globus_l_ftp_control_data_stream_read_write(
                         dc_handle,
                         buffer,
                         max_length,
                         0,
                         GLOBUS_FALSE,
                         callback,
                         callback_arg);
        }
        else if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK)
        {
            /* queue in the same manner as stream mode, single queue */
            result = globus_l_ftp_control_data_stream_read_write(
                         dc_handle,
                         buffer,
                         max_length,
                         0,
                         GLOBUS_FALSE,
                         callback,
                         callback_arg);
        }
        else
        {
            err = dc_handle->connect_error
                ? globus_object_copy(dc_handle->connect_error)
                : globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
         _FCSL("globus_ftp_control_data_read(): Handle not using correct mode.  Possible memory corruption."));
        }
        
        if(err)
        {
            globus_mutex_unlock(&dc_handle->mutex);
            return globus_error_put(err);
        }
        globus_l_ftp_data_stripe_poll(dc_handle);
    }
    globus_mutex_unlock(&dc_handle->mutex);


    return result;
}

/*
 *  register a big buffer read
 */
globus_result_t
globus_ftp_control_data_read_all(
    globus_ftp_control_handle_t *		handle,
    globus_byte_t *				buffer,
    globus_size_t				length,
    globus_ftp_control_data_callback_t          callback,
    void *					callback_arg)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_ftp_data_stripe_t *                  stripe;
    globus_ftp_data_connection_t *              data_conn;
    globus_result_t                             res;
    globus_object_t *                           err;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    int                                         ctr;
    static char *                               myname=
                                      "globus_ftp_control_data_read";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(buffer == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "buffer",
                  2,
                  myname);
        return globus_error_put(err);
    }
    if(callback == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "callback",
                  4,
                  myname);
        return globus_error_put(err);
    }

    transfer_handle = dc_handle->transfer_handle;

    globus_mutex_lock(&dc_handle->mutex);
    {
        err = GLOBUS_NULL;
        
        if(dc_handle->transfer_handle == GLOBUS_NULL)
        {
            err = dc_handle->connect_error
                ? globus_object_copy(dc_handle->connect_error)
                : globus_error_construct_string(
                          GLOBUS_FTP_CONTROL_MODULE,
                          GLOBUS_NULL,
                    _FCSL("Handle not in the proper state"));
        }
        else if(dc_handle->state != GLOBUS_FTP_DATA_STATE_CONNECT_READ)
        {
            err = dc_handle->connect_error
                ? globus_object_copy(dc_handle->connect_error)
                : globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
         _FCSL("globus_ftp_control_data_read_all(): Handle not in proper state %s."),
          globus_l_ftp_control_state_to_string(dc_handle->state));
        }
        else if(transfer_handle->big_buffer != GLOBUS_NULL)
        {
            err = globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
         _FCSL("globus_ftp_control_data_read_all(): Only one read_all can be registered at a time.  You must wait for a callback with eof set to GLOBUS_TRUE before calling read all again."));
        }
        
        if(err)
        {
            globus_mutex_unlock(&dc_handle->mutex);
            return globus_error_put(err);
        }
        
        transfer_handle->big_buffer = buffer;
        transfer_handle->big_buffer_length = length;
        transfer_handle->big_buffer_cb = callback;
        transfer_handle->big_buffer_cb_arg = callback_arg;

        if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_STREAM)
        {
            res = globus_l_ftp_control_data_stream_read_write(
                      dc_handle,
                      buffer,
                      length,
                      0,
                      GLOBUS_FALSE,
                      callback,
                      callback_arg);
        }
        else if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK)
        {
            res = GLOBUS_SUCCESS;

            for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
            {
                stripe = &transfer_handle->stripes[ctr];

                while(!globus_fifo_empty(&stripe->free_conn_q))
                {
                    globus_off_t end_offset;
                    globus_off_t end_buffer;

                    data_conn = (globus_ftp_data_connection_t *)
                        globus_fifo_dequeue(&stripe->free_conn_q);

                    end_offset = ((globus_off_t) data_conn->bytes_ready) +
                                        data_conn->offset;
                    end_buffer = ((globus_off_t)
                                        transfer_handle->big_buffer_length);

                    /*
                     *  if the sender sent more bytes than the users
                     *  buffer can handle
                     */
                    if(end_offset > end_buffer)
                    {
                        err =  globus_error_construct_string(
                                     GLOBUS_FTP_CONTROL_MODULE,
                                     GLOBUS_NULL,
                                     _FCSL("too much data has been sent."));
                        globus_l_ftp_control_stripes_destroy(dc_handle, err);

                        return globus_error_put(err);
                    }
                    else
                    {
                        globus_l_ftp_handle_table_entry_t *    t_e;

                        transfer_handle->ref++;
                        TABLE_ENTRY_MALLOC(
                            t_e,
                            &transfer_handle->big_buffer[data_conn->offset],
                            data_conn->bytes_ready,
                            data_conn->offset,
                            GLOBUS_FALSE,
                            transfer_handle->big_buffer_cb,
                            transfer_handle->big_buffer_cb_arg,
                            dc_handle);
                        t_e->whos_my_daddy = data_conn;

                        /*
                         *  register a read into the users buffer at the
                         *  correct offset.
                         */
                        res = globus_io_register_read(
                                  &data_conn->io_handle,
                                  &transfer_handle->big_buffer[data_conn->offset],
                                  data_conn->bytes_ready,
                                  data_conn->bytes_ready,
                                  globus_l_ftp_eb_read_callback,
                                  (void *)t_e);
                        globus_assert(res == GLOBUS_SUCCESS);
                    }
                }
            }
        }
        else
        {
            err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                    _FCSL("[%s]:%s() : Invalid mode.  Shouldn't happen."),
                    GLOBUS_FTP_CONTROL_MODULE->module_name,
                    myname);
            res = globus_error_put(err);

        }
        globus_l_ftp_data_stripe_poll(dc_handle);
    }
    globus_mutex_unlock(&dc_handle->mutex);


    return res;
}

globus_result_t
globus_i_ftp_control_data_activate(void)
{
    globus_mutex_init(&globus_l_ftp_control_data_mutex, GLOBUS_NULL);
    globus_cond_init(&globus_l_ftp_control_data_cond, GLOBUS_NULL);

    globus_l_ftp_control_data_dc_list = GLOBUS_NULL;

    /* keep this last */
    globus_l_ftp_control_data_active = GLOBUS_TRUE;

    globus_hashtable_init(
        &globus_l_ftp_control_data_layout_table,
        GFTPC_HASH_TABLE_SIZE,
        globus_hashtable_string_hash,
        globus_hashtable_string_keyeq);

    globus_ftp_control_layout_register_func(
        "Blocked",
        globus_ftp_control_layout_blocked,
        globus_ftp_control_layout_blocked_verify);
    globus_ftp_control_layout_register_func(
        "Partitioned",
        globus_ftp_control_layout_partitioned,
        globus_ftp_control_layout_partitioned_verify);

    return GLOBUS_SUCCESS;
}

/*===================================================*/

void
globus_l_ftp_control_deactivate_quit_callback(
    void *                                      user_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error)
{
    globus_mutex_lock(&globus_l_ftp_control_data_mutex);
    {
        globus_l_ftp_control_data_dc_count--;
        globus_cond_signal(&globus_l_ftp_control_data_cond);
    }
    globus_mutex_unlock(&globus_l_ftp_control_data_mutex);
}

static
void
globus_l_ftp_control_data_layout_clean(
    void *                              arg)
{
    globus_l_ftp_c_data_layout_t *      layout_info;

    layout_info = (globus_l_ftp_c_data_layout_t *) arg;

    globus_free(layout_info->name);
    globus_free(layout_info);
}

globus_result_t
globus_i_ftp_control_data_deactivate(void)
{
    globus_hashtable_destroy_all(
        &globus_l_ftp_control_data_layout_table,
        globus_l_ftp_control_data_layout_clean);
    globus_cond_destroy(&globus_l_ftp_control_data_cond);
    globus_mutex_destroy(&globus_l_ftp_control_data_mutex);

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_l_ftp_control_data_stream_read_write(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    globus_ftp_control_data_callback_t          callback,
    void *                                      callback_arg)
{
    globus_l_ftp_handle_table_entry_t *         table_entry;
    globus_ftp_data_stripe_t *                  stripe;
    globus_object_t *                           err;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    static char *                               my_name = 
        "globus_l_ftp_control_data_stream_read_write";

    if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING)
    {
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
         _FCSL("[%s]:%s() : In closing state."),
          GLOBUS_FTP_CONTROL_MODULE->module_name,
          my_name);
        return globus_error_put(err);
    }
    transfer_handle = dc_handle->transfer_handle;

    /*
     *  allocate and populate the entry structure
     */
    TABLE_ENTRY_MALLOC(
        table_entry,
        buffer,
        length,
        offset,
        eof,
        callback,
        callback_arg,
        dc_handle);

    stripe = &transfer_handle->stripes[0];
    globus_fifo_enqueue(&stripe->command_q,
                        (void *)table_entry);

    /* every callback gets a reference count */
    transfer_handle->ref++;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_l_ftp_control_data_eb_write(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    globus_ftp_control_data_callback_t          callback,
    void *                                      callback_arg)
{
    globus_result_t                             res;
    globus_l_ftp_handle_table_entry_t *         tmp_ent;
    globus_ftp_data_stripe_t *                  stripe;
    int                                         ctr;
    globus_object_t *                           err;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_ftp_control_data_write_info_t        data_info;
    globus_ftp_control_layout_func_t            layout_func;
    static char *                               my_name =
      "globus_l_ftp_control_data_eb_write";

    transfer_handle = dc_handle->transfer_handle;
    layout_func = dc_handle->layout_func;
    /*
     * if eof has been registered reject all future writes
     */
    if(transfer_handle->eof_registered)
    {
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
    _FCSL("[%s]:%s() : eof has already been registered"),
                  GLOBUS_FTP_CONTROL_MODULE->module_name,
                  my_name);
        return globus_error_put(err);
    }

    /*
     *  only allow zero length messages if eof is true
     */
    if(length <= 0 && !eof)
    {
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
         _FCSL("[%s]:%s() : cannot register a zero length message unless you are signifying eof."),
                  GLOBUS_FTP_CONTROL_MODULE->module_name,
                  my_name);
        return globus_error_put(err);
    }

    globus_i_ftp_control_create_data_info(
        dc_handle,
        &data_info,
        buffer,
        length,
        offset,
        eof,
        callback,
        callback_arg);

    if(length > 0)
    {
        if(layout_func)
        {
            transfer_handle->x_state = GLOBUS_TRUE;

            layout_func(
                dc_handle->whos_my_daddy,
                &data_info,
                buffer,
                length,
                offset,
                eof,
                transfer_handle->stripe_count,
                dc_handle->layout_str,
                dc_handle->layout_user_arg);

            transfer_handle->x_state = GLOBUS_FALSE;
        }
        else
        {
            res = globus_i_ftp_control_data_write_stripe(
                dc_handle,
                buffer,
                length,
                offset,
                eof,
                0,
                &data_info);
            if(res != GLOBUS_SUCCESS)
            {
                /* need to free what was created in:
                    globus_i_ftp_control_create_data_info*/
                return res;
            }
        }
    }

    /*
     *  if eof has been registered we must add a final message to
     *  all stripe queues that eof has been hit.  For efficiency if the
     *  queue is not empty turn eof on in it last message.
     *
     *  each callback has a reference for each stripe it was registered
     *  on.  Add another reference for every stripe since they all
     *  will have a eof message put on them.
     */
    if(eof)
    {
        transfer_handle->eof_registered = GLOBUS_TRUE;
        transfer_handle->eof_table_handle = data_info.callback_table_handle;
        transfer_handle->eof_cb_ent =
            globus_handle_table_lookup(&transfer_handle->handle_table,
                                       data_info.callback_table_handle);

        for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
        {
            stripe = &transfer_handle->stripes[ctr];

            /*
             * only register a new EOF message if one has not already
             * been registered
             */
            tmp_ent = GLOBUS_NULL;
            if(!globus_fifo_empty(&stripe->command_q))
            {
                tmp_ent = (globus_l_ftp_handle_table_entry_t *)
                                  globus_fifo_tail_peek(&stripe->command_q);
            }

            /*
             *  if no eof message has been registered on this stripe yet.
             */
            if(tmp_ent == GLOBUS_NULL || !tmp_ent->eof)
            {
                TABLE_ENTRY_MALLOC(
                    tmp_ent,
                    buffer,
                    0,
                    0,
                    GLOBUS_TRUE,
                    GLOBUS_NULL,
                    GLOBUS_NULL,
                    dc_handle);
                tmp_ent->callback_table_handle = 
                    data_info.callback_table_handle;
                    
                globus_fifo_enqueue(&stripe->command_q,
                                    (void *)tmp_ent);

            }
            /*
             * inc reference on eof callback, x for the number of stripes
             * it was broken across and 1 for eof
             */
            globus_handle_table_increment_reference(
                &transfer_handle->handle_table,
                transfer_handle->eof_table_handle);
        }
    }

    globus_i_ftp_control_release_data_info(
        dc_handle,
        &data_info);

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_l_ftp_control_data_extended_block_enqueue(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_l_ftp_handle_table_entry_t *         entry,
    int                                         chunk)
{
    int                                         stripe_ndx;
    globus_off_t                                offset;
    globus_ftp_data_stripe_t *                  stripe;
    globus_l_ftp_handle_table_entry_t *         tmp_ent;
    globus_size_t                               size;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;

    transfer_handle = dc_handle->transfer_handle;

    /* determine what stripe to put things on */
    for(offset = entry->offset;
        offset < entry->offset + entry->length;
        offset += size)
    {
        if(chunk > 0)
        {
            stripe_ndx = (offset / chunk) %
                         transfer_handle->stripe_count;

            stripe = &transfer_handle->stripes[stripe_ndx];
            size = chunk - (offset % chunk);
            if(size > entry->length - (offset - entry->offset))
            {
                size = entry->length - (offset - entry->offset);
            }
        }
        /* if we only have 1 stripe put it all on it */
        else
        {
            stripe_ndx = 0;
            size = entry->length;
            stripe = &transfer_handle->stripes[stripe_ndx];
        }

        TABLE_ENTRY_MALLOC(
            tmp_ent,
            &entry->buffer[(globus_size_t)(offset-entry->offset)],
            size,
            offset,
            entry->eof,
            entry->callback,
            entry->callback_arg,
            entry->dc_handle);
        tmp_ent->callback_table_handle = entry->callback_table_handle;

        globus_handle_table_increment_reference(
            &tmp_ent->dc_handle->transfer_handle->handle_table,
            tmp_ent->callback_table_handle);
        globus_fifo_enqueue(&stripe->command_q, (void *) tmp_ent);
    }

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_ftp_control_layout_register_func(
    char *                                      name,
    globus_ftp_control_layout_func_t            layout_func,
    globus_ftp_control_layout_verify_func_t     verify_func)
{
    globus_l_ftp_c_data_layout_t *              layout_info;
    globus_object_t *                           err;
    static char *                               myname=
                                  "globus_ftp_control_local_register_func";

    if(name == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "name",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(layout_func == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "layout_func",
                  2,
                  myname);
        return globus_error_put(err);
    }
    if(verify_func == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "verify_func",
                  3,
                  myname);
        return globus_error_put(err);
    }

    layout_info = (globus_l_ftp_c_data_layout_t *)
                       globus_malloc(sizeof(globus_l_ftp_c_data_layout_t));
    layout_info->layout_func = layout_func;
    layout_info->verify_func = verify_func;
    layout_info->name = strdup(name);

    globus_mutex_lock(&globus_l_ftp_control_data_mutex);
    {
        globus_hashtable_insert(
            &globus_l_ftp_control_data_layout_table,
            name,
            layout_info);
    }
    globus_mutex_unlock(&globus_l_ftp_control_data_mutex);

    return GLOBUS_SUCCESS;
}

/**
 * @brief Set data layout
 * @ingroup globus_ftp_control_data
 * @details
 * Update the handle with the layout and the size of the data sent
 * over the data channel.
 *
 * This function is deprecated.  The interface will be the changed to
 * that of globus_X_ftp_control_local_layout()
 *
 * @param handle
 *        A pointer to the FTP control handle into which to insert the
 *        layout information.
 * @param layout
 *        A variable containing the layout information
 * @param data_size
 *        The size of the data that is going to be sent. This may be
 *        needed to interpret the layout information.
 */
globus_result_t
globus_ftp_control_local_layout(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_layout_t *               layout,
    globus_size_t                               data_size)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    char                                        strmsg[512];
    void *                                      user_arg;
    globus_object_t *                           err;
    globus_result_t                             res;
    static char *                               myname=
                                      "globus_ftp_control_local_layout";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(layout == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "layout",
                  2,
                  myname);
        return globus_error_put(err);
    }

    if(layout->mode == GLOBUS_FTP_CONTROL_STRIPING_BLOCKED_ROUND_ROBIN)
    {
        if(layout->round_robin.block_size <= 0)
        {
            err = globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("[%s]:%s() : round robin block size must be greater than 0."),
                      GLOBUS_FTP_CONTROL_MODULE->module_name,
                      myname);

            return globus_error_put(err);
        }

        user_arg = GLOBUS_NULL;
        sprintf(strmsg, "StripedLayout=Blocked;BlockSize=%ld;",
                (long) layout->round_robin.block_size);
    }
    else if(layout->mode == GLOBUS_FTP_CONTROL_STRIPING_PARTITIONED)
    {
        if(layout->partitioned.size <= 0)
        {
            err = globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("[%s]:%s() : partition block size must be greater than 0."),
                      GLOBUS_FTP_CONTROL_MODULE->module_name,
                      myname);

            return globus_error_put(err);
        }
        sprintf(strmsg, "StripedLayout=Partitioned;");

        user_arg = globus_ftp_control_layout_partitioned_user_arg_create(
                       layout->partitioned.size);
    }
    else if(layout->mode == GLOBUS_FTP_CONTROL_STRIPING_NONE)
    {
        globus_mutex_lock(&dc_handle->mutex);
        {
            dc_handle->layout_func = GLOBUS_NULL;
            dc_handle->layout_user_arg = GLOBUS_NULL;

            if(dc_handle->layout_str != GLOBUS_NULL)
            {
                free(dc_handle->layout_str);
            }
            dc_handle->layout_str = GLOBUS_NULL;
        }
        globus_mutex_unlock(&dc_handle->mutex);

        return GLOBUS_SUCCESS;
    }
    else
    {
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  _FCSL("[%s]:%s() : unknown type."),
                  GLOBUS_FTP_CONTROL_MODULE->module_name,
                  myname);

        return globus_error_put(err);
    }

    res = globus_X_ftp_control_local_layout(
              handle,
              strmsg,
              user_arg);

    return res;
}

globus_result_t
globus_X_ftp_control_local_layout(
    globus_ftp_control_handle_t *               handle,
    char *                                      layout_str,
    void *                                      user_arg)
{
    globus_l_ftp_c_data_layout_t *              layout_info;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_object_t *                           err;
    char *                                      name;
    char *                                      tmp_ptr;
    globus_result_t                             res;
    static char *                               myname=
                                      "globus_ftp_control_local_layout";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(layout_str == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "layout_str",
                  2,
                  myname);
        return globus_error_put(err);
    }

    name = (char *)globus_malloc(strlen(layout_str));

    if(sscanf(layout_str, "StripedLayout=%s;", name) < 1)
    {
        globus_free(name);

        return globus_error_put(globus_error_construct_string(
                   GLOBUS_FTP_CONTROL_MODULE,
                   GLOBUS_NULL,
                   _FCSL("[%s]:%s() : Enqueue string has invalid format.  Must be of the form: StripedLayout=<name>;[parameters]"),
                   GLOBUS_FTP_CONTROL_MODULE->module_name,
                   myname));

    }
    tmp_ptr = strstr(name, ";");
    if(tmp_ptr == GLOBUS_NULL)
    {
        globus_free(name);

        return globus_error_put(globus_error_construct_string(
                   GLOBUS_FTP_CONTROL_MODULE,
                   GLOBUS_NULL,
                   _FCSL("[%s]:%s() : Enqueue string has invalid format.  Must be of the form: StripedLayout=<name>;[parameters]"),
                   GLOBUS_FTP_CONTROL_MODULE->module_name,
                   myname));
    }
    *tmp_ptr = '\0';

    globus_mutex_lock(&globus_l_ftp_control_data_mutex);
    {
        layout_info = globus_hashtable_lookup(
            &globus_l_ftp_control_data_layout_table,
            name);
    }
    globus_mutex_unlock(&globus_l_ftp_control_data_mutex);
    
    globus_free(name);
    
    if(layout_info == GLOBUS_NULL)
    {
        return globus_error_put(globus_error_construct_string(
                   GLOBUS_FTP_CONTROL_MODULE,
                   GLOBUS_NULL,
                   _FCSL("[%s]:%s() : layout name has not be registered"),
                    GLOBUS_FTP_CONTROL_MODULE->module_name,
                   myname));
    }

    res = layout_info->verify_func(layout_str);
    if(res != GLOBUS_SUCCESS)
    {
        return res;
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        dc_handle->layout_func = layout_info->layout_func;
        dc_handle->layout_user_arg = user_arg;

        if(dc_handle->layout_str != GLOBUS_NULL)
        {
            free(dc_handle->layout_str);
        }
        dc_handle->layout_str = strdup(layout_str);
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return GLOBUS_SUCCESS;
}

/**
 * @brief Create a globus_ftp_control_data_write_info_t structure
 * @ingroup globus_ftp_control_data
 * @details
 * This function populates a globus_ftp_control_data_callback_t
 * structure with valid information.  This structure provides the user
 * a way to register several data writes with a single callback.  This
 * is quite useful to the writer of enqueue functions.  It allows a
 * single call to globus_ftp_control_data_write() to be broken up
 * into many writes, potentially on different stripes, and for a single
 * callback to be called when all are finished.
 *
 * @param handle
 *        A pointer to a FTP control handle. The handle contains
 *        information about the current state of the control and data
 *        connections.
 * @param data_info
 *        The globus_ftp_control_data_write_info_t structure to be released.
 * @param buffer
 *        The pointer to the user buffer that will be passed to the
 *        callback argument when there are zero references to data_info.
 *        This is intended to be the start of all the data the user intends
 *        to write using globus_ftp_control_data_write_stripe(), but it
 *        does not have to be.
 * @param length
 *        The length of the memory segment pointed to by the argument buffer.
 * @param offset
 *        The file offset of the data segment specified.
 * @param eof
 *        This should be set to true if the user plans on registering eof
 *        on the data_info structure.
 * @param callback
 *        The user function to be called when all references to data_info
 *        are released.  This occurs after all data registered for write
 *        from globus_ftp_control_data_write_stripe have occurred and the
 *        user calls globus_ftp_control_release_data_info().  The callback
 *        is passed all of the arguments passed to this function with the
 *        exception of data_info.
 * @param callback_arg
 *        User supplied argument to the callback function
 */
globus_result_t
globus_ftp_control_create_data_info(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_data_write_info_t *      data_info,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    globus_ftp_control_data_callback_t          callback,
    void *                                      callback_arg)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_result_t                             res;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_create_data_info";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(data_info == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "data_info",
                  2,
                  myname);
        return globus_error_put(err);
    }
    if(callback == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "callback",
                  3,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(dc_handle->transfer_handle == GLOBUS_NULL)
        {
            res = globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("[%s]:%s() : Handle not in the proper state"),
                       GLOBUS_FTP_CONTROL_MODULE->module_name,
                       myname));

        }
        else
        {
            res = globus_i_ftp_control_create_data_info(
                      dc_handle,
                      data_info,
                      buffer,
                      length,
                      offset,
                      eof,
                      callback,
                      callback_arg);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return res;
}

/**
 * @brief Release a data_info structure.
 * @ingroup globus_ftp_control_data
 * @details
 * This function releases all memory and references created when a call
 * to globus_ftp_control_create_data_info() was made.  For every call to
 * globus_ftp_control_create_data_info() a call to this function must be
 * made.
 *
 * @param handle
 *        A pointer to a FTP control handle. The handle contains
 *        information about the current state of the control and data
 *        connections.
 * @param data_info
 *        The globus_ftp_control_data_write_info_t structure to be released.
 */
globus_result_t
globus_ftp_control_release_data_info(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_data_write_info_t *      data_info)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_result_t                             res;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_release_data_info";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(data_info == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "data_info",
                  2,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(dc_handle->transfer_handle == GLOBUS_NULL)
        {
            res = globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                      _FCSL("[%s]:%s() : Handle not in the proper state"),
                      GLOBUS_FTP_CONTROL_MODULE->module_name,
                      myname));

        }
        else
        {
            res = globus_i_ftp_control_release_data_info(
                      dc_handle,
                      data_info);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return res;
}

/**
 * @brief Write FTP data to a stripe
 * @ingroup globus_ftp_control_data
 * @details
 * This function allows the user to write to a specified stripe.  The stripe
 * index relates to the order passsed into local_spor().  This function
 * differs from globus_ftp_control_data_write() in that no enqueue function
 * is needed since the user specifies the stripe on which data is written.
 * In order to use this function the user must have a valid pointer to a
 * globus_ftp_control_data_write_info_t structure.  The data_info structure
 * can be obtained by a call to globus_ftp_control_create_data_info().
 * Many calls to this function can be made, but only a single user callback
 * occurs per creation of a globus_ftp_control_data_write_info_t structure.
 *
 * @param handle
 *        A pointer to a FTP control handle. The handle contains
 *        information about the current state of the control and data
 *        connections.
 * @param buffer
 *        a pointer to the data the user wishes to send along the FTP
 *        data channels.
 * @param length
 *        the length of the data pointer to by the parameter buffer.
 * @param offset
 *        the offset into the file of the data.
 * @param eof
 *        A boolean stating that this will be the last chuck of data
 *        registered on the given stripe.  In order to properly send an eof
 *        message the user must register an eof on every stripe.
 * @param stripe_ndx
 *        The index of the stripe on which the data will be sent.  The index
 *        of each stripe is determined by the call to local_spas or local_spor.
 * @param callback
 *        The function to be called once the data has been sent
 * @param callback_arg
 *        User supplied argument to the callback function
 */
globus_result_t
globus_ftp_control_data_write_stripe(
    globus_ftp_control_handle_t *		handle,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    int                                         stripe_ndx,
    globus_ftp_control_data_callback_t          callback,
    void *                                      callback_arg)
{
    globus_ftp_control_data_write_info_t        data_info;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_result_t                             res;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_ftp_control_data_write_stripe";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(buffer == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "buffer",
                  2,
                  myname);
        return globus_error_put(err);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(dc_handle->transfer_handle == GLOBUS_NULL)
        {
            globus_mutex_unlock(&dc_handle->mutex);
            err = dc_handle->connect_error
                ? globus_object_copy(dc_handle->connect_error)
                : globus_error_construct_string(
                          GLOBUS_FTP_CONTROL_MODULE,
                          GLOBUS_NULL,
                    _FCSL("Handle not in the proper state"));
            return globus_error_put(err);
        }
        
        res = globus_i_ftp_control_create_data_info(
                      dc_handle,
                      &data_info,
                      buffer,
                      length,
                      offset,
                      eof,
                      callback,
                      callback_arg);
        if(res != GLOBUS_SUCCESS)
        {
            globus_mutex_unlock(&dc_handle->mutex);
            goto exit;
        }

        res = globus_i_ftp_control_data_write_stripe(
                  dc_handle,
                  buffer,
                  length,
                  offset,
                  eof,
                  stripe_ndx,
                  &data_info);

        res = globus_i_ftp_control_release_data_info(
                  dc_handle,
                  &data_info);
exit:
        globus_l_ftp_data_stripe_poll(dc_handle);
    }
    globus_mutex_unlock(&dc_handle->mutex);


    return res;
}

/**
 * @brief Write data to a stripe from an enqueue callback
 * @ingroup globus_ftp_control_data
 * @details
 * This function allows the user to register the write of ftp data on
 * a specific stripe.  This function can only be called from an enqueue
 * function callback.  This function should be used only by the implementor
 * of an enqueue function.  It should be viewed as unstable and used used
 * only by advanced users.  This is the only function in the library that
 * the enqueue function implementor is allowed from the enqueue callback.
 *
 * @param handle
 *        A pointer to a FTP control handle. The handle contains
 *        information about the current state of the control and data
 *        connections.
 * @param buffer
 *        a pointer to the data the user wishes to send along the FTP
 *        data channels.
 * @param length
 *        the length of the data pointer to by the parameter buffer.
 * @param offset
 *        the offset into the file of the data.
 * @param eof
 *        a boolean stating that this is the last buffer to be registered.
 *        When using the _X_ version of this function the user does not
 *        need to register an eof on each stripe, the control library will take
 *        care of that internally.
 * @param stripe_ndx
 *        The index of the stripe on which the data will be sent.  The index
 *        of each stripe is determined by the call to local_spas or local_spor.
 * @param data_info
 *        An opaque structure that is passed into the enqueue function and
 *        contains reference count and state information.  The same data_info
 *        pointer that is passed into the enqueue function must be used for this
 *        parameter.
 *
 */
globus_result_t
globus_X_ftp_control_data_write_stripe(
    globus_ftp_control_handle_t *		handle,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    int                                         stripe_ndx,
    globus_ftp_control_data_write_info_t *      data_info)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_result_t                             res;
    globus_object_t *                           err;
    static char *                               myname=
                                      "globus_X_ftp_control_data_write_stripe";

    /*
     *  error checking
     */
    if(handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(buffer == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "buffer",
                  2,
                  myname);
        return globus_error_put(err);
    }

    /* check to see if in special state */
    if(dc_handle->transfer_handle  == GLOBUS_NULL ||
       !dc_handle->transfer_handle->x_state)
    {
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  _FCSL("[%s]:%s() : not in X state"),
                  GLOBUS_FTP_CONTROL_MODULE->module_name,
                  myname);

        return globus_error_put(err);
    }

    res = globus_i_ftp_control_data_write_stripe(
              dc_handle,
              buffer,
              length,
              offset,
              eof,
              stripe_ndx,
              data_info);

    return res;
}

/*
 *  internal stripe write functions
 */
globus_result_t
globus_i_ftp_control_data_write_stripe(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_byte_t *				buffer,
    globus_size_t				length,
    globus_off_t				offset,
    globus_bool_t				eof,
    int                                         stripe_ndx,
    globus_ftp_control_data_write_info_t *      data_info)
{
    globus_l_ftp_handle_table_entry_t *         tmp_ent;
    globus_ftp_data_stripe_t *                  stripe;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_object_t *                           err;
    static char *                               myname =
        "globus_i_ftp_control_data_write_stripe";

    transfer_handle = dc_handle->transfer_handle;

    if(transfer_handle == GLOBUS_NULL)
    {
        return globus_error_put(globus_error_construct_string(
                   GLOBUS_FTP_CONTROL_MODULE,
                   GLOBUS_NULL,
                   _FCSL("[%s]:%s() : Handle not in the proper state"),
                   GLOBUS_FTP_CONTROL_MODULE->module_name,
                   myname));
    }

    if(transfer_handle->eof_registered)
    {
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  _FCSL("[%s]:%s() : eof has already been registered."),
                  GLOBUS_FTP_CONTROL_MODULE->module_name,
                  myname);
        return globus_error_put(err);
    }

    /*
     *  only allow zero length messages if eof is true
     */
    if(length <= 0 && !eof)
    {
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
         _FCSL("[%s]:%s() : cannot register a zero length message unless you are signifying eof."),
                  GLOBUS_FTP_CONTROL_MODULE->module_name,
                  myname);
        return globus_error_put(err);
    }

    stripe = &transfer_handle->stripes[stripe_ndx];

    TABLE_ENTRY_MALLOC(
        tmp_ent,
        buffer,
        length,
        offset,
        eof,
        data_info->cb,
        data_info->cb_arg,
        dc_handle);

    tmp_ent->callback_table_handle = data_info->callback_table_handle;

    globus_handle_table_increment_reference(
        &tmp_ent->dc_handle->transfer_handle->handle_table,
        tmp_ent->callback_table_handle);
    globus_fifo_enqueue(&stripe->command_q, (void *) tmp_ent);

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_i_ftp_control_create_data_info(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_control_data_write_info_t *      data_info,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    globus_ftp_control_data_callback_t          callback,
    void *                                      callback_arg)
{
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_l_ftp_handle_table_entry_t *         table_entry;

    transfer_handle = dc_handle->transfer_handle;

    transfer_handle->ref++;
    TABLE_ENTRY_MALLOC(
        table_entry,
        buffer,
        length,
        offset,
        eof,
        callback,
        callback_arg,
        dc_handle);

    /*
     *  insert main structure into the callback table
     */
    table_entry->callback_table_handle = globus_handle_table_insert(
        &transfer_handle->handle_table,
        (void *)table_entry,
        1);

    data_info->callback_table_handle = table_entry->callback_table_handle;
    data_info->cb = callback;
    data_info->cb_arg = callback_arg;

    return GLOBUS_SUCCESS;
}

static
void
globus_l_ftp_control_release_data_kickout(
    void *                                      user_args)
{
    globus_l_ftp_handle_table_entry_t *          cb_ent;

    cb_ent = (globus_l_ftp_handle_table_entry_t *)user_args;

    cb_ent->callback(
        cb_ent->callback_arg,
        cb_ent->dc_handle->whos_my_daddy,
        GLOBUS_NULL,
        cb_ent->buffer,
        cb_ent->length,
        cb_ent->offset,
        cb_ent->eof);
    globus_free(cb_ent);
}

globus_result_t
globus_i_ftp_control_release_data_info(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_control_data_write_info_t *      data_info)
{
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_l_ftp_handle_table_entry_t *         cb_ent;
    globus_reltime_t                            reltime;

    transfer_handle = dc_handle->transfer_handle;

    cb_ent = globus_handle_table_lookup(
                 &transfer_handle->handle_table,
                 data_info->callback_table_handle);

    /*
     *  if this was the last reference held we must register
     *  a oneshot for the users callback
     */
    if(!globus_handle_table_decrement_reference(
           &transfer_handle->handle_table,
           data_info->callback_table_handle))
    {
        GlobusTimeReltimeSet(reltime, 0, 0);
        globus_callback_register_oneshot(
            GLOBUS_NULL,
            &reltime,
            globus_l_ftp_control_release_data_kickout,
            (void *) cb_ent);
    }

    return GLOBUS_SUCCESS;
}

/**********************************************************************
*    poll functions
**********************************************************************/

/*
 *  poll all stripes
 */
globus_result_t
globus_l_ftp_data_stripe_poll(
    globus_i_ftp_dc_handle_t *                   dc_handle)
{
    globus_ftp_data_stripe_t *                   stripe;
    globus_result_t                              result;
    globus_i_ftp_dc_transfer_handle_t *          transfer_handle;

        transfer_handle = dc_handle->transfer_handle;
        if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING)
        {
            /*
             *  I can do this since this function is only called
             *  internally
             */
            result = globus_error_put(GLOBUS_ERROR_NO_INFO);
        }
        else
        {
            if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_STREAM)
            {
                if(transfer_handle != GLOBUS_NULL)
                {
                    stripe = &transfer_handle->stripes[0];
                    globus_l_ftp_data_stream_stripe_poll(stripe);
                }
            }
            else if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK)
            {
                globus_l_ftp_data_eb_poll(dc_handle);
            }
           result = GLOBUS_SUCCESS;
        }

    return result;
}

/*
 *  poll a specific stripe
 *  ----------------------
 *  dequeue the next item in the queue an register it for read or write
 *  this should be called locked
 */
globus_result_t
globus_l_ftp_data_stream_stripe_poll(
    globus_ftp_data_stripe_t *		          stripe)
{
    globus_l_ftp_handle_table_entry_t *           entry;
    globus_ftp_data_connection_t *                data_conn;
    globus_result_t                               result;

    globus_assert(stripe->whos_my_daddy->whos_my_daddy->mode == GLOBUS_FTP_CONTROL_MODE_STREAM);
    /*
     *  check to see that there is a connection
     */
    while(!globus_fifo_empty(&stripe->free_conn_q) &&
       !globus_fifo_empty(&stripe->command_q))
    {
        entry = (globus_l_ftp_handle_table_entry_t *)
            globus_fifo_peek(&stripe->command_q);

        data_conn = (globus_ftp_data_connection_t *)
            globus_fifo_peek(&stripe->free_conn_q);

        if(data_conn != GLOBUS_NULL)
        {
            entry->whos_my_daddy = data_conn;
            if(entry->direction == GLOBUS_FTP_DATA_STATE_CONNECT_WRITE)
            {
                globus_byte_t *                   tmp_buf = entry->buffer;
                globus_off_t                      tmp_len;

                tmp_len = entry->length;
                if(stripe->whos_my_daddy->whos_my_daddy->type ==
                               GLOBUS_FTP_CONTROL_TYPE_ASCII)
                {
                    entry->ascii_buffer = globus_l_ftp_control_add_ascii(
                                              entry->buffer,
                                              entry->length,
                                              &tmp_len);
                    
                    if(entry->ascii_buffer)
                    {
                        tmp_buf = entry->ascii_buffer;
                    }
                }

                /* remove from queue */
                globus_fifo_dequeue(&stripe->command_q);

                globus_fifo_dequeue(&stripe->free_conn_q);

                result = globus_io_register_write(
                             &data_conn->io_handle,
                             tmp_buf,
                             tmp_len,
                             globus_l_ftp_stream_write_callback,
                             (void *)entry);
                globus_assert(result == GLOBUS_SUCCESS);
            }
            else if(entry->direction == GLOBUS_FTP_DATA_STATE_CONNECT_READ)
            {
                /* remove from queue */
                globus_fifo_dequeue(&stripe->command_q);
                globus_fifo_dequeue(&stripe->free_conn_q);

                result = globus_io_register_read(
                             &data_conn->io_handle,
                             entry->buffer,
                             entry->length,
                             entry->length,
                             globus_l_ftp_stream_read_callback,
                             (void *)entry);
                globus_assert(result == GLOBUS_SUCCESS);
            }
        }
    }

    return GLOBUS_SUCCESS;
}

int
globus_i_ftp_queue_size(
    globus_ftp_control_handle_t *                handle,
    int                                          stripe_ndx)
{
    globus_i_ftp_dc_handle_t *                   dc_handle;
    globus_ftp_data_stripe_t *                   stripe;
    globus_i_ftp_dc_transfer_handle_t *          transfer_handle;

    dc_handle = &handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    transfer_handle = dc_handle->transfer_handle;

    stripe = &transfer_handle->stripes[stripe_ndx];;

    return globus_fifo_size(&stripe->command_q);
}


globus_result_t
globus_l_ftp_data_eb_poll(
    globus_i_ftp_dc_handle_t *                   dc_handle)
{
    globus_ftp_data_stripe_t *                   stripe;
    globus_l_ftp_handle_table_entry_t *          entry;
    globus_ftp_data_connection_t *               data_conn;
    globus_ftp_data_connection_t *               data_conn2;
    globus_l_ftp_eb_header_t *                   eb_header;
    struct iovec *                               io_vec;
    globus_result_t                              res;
    globus_bool_t                                done = GLOBUS_FALSE;
    globus_reltime_t                             reltime;
    globus_off_t                                 tmp_len;
    globus_byte_t *                              tmp_buf;
    int                                          ctr;
    globus_fifo_t                                remaining_command_q = NULL;
    globus_fifo_t                                remaining_conn_q = NULL;
    globus_bool_t                                do_read;
    globus_bool_t                                checked_order = GLOBUS_FALSE;
    globus_i_ftp_dc_transfer_handle_t *          transfer_handle;

    transfer_handle = dc_handle->transfer_handle;

    if(transfer_handle == GLOBUS_NULL)
    {
        return GLOBUS_SUCCESS;
    }

    for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
    {
        stripe = &transfer_handle->stripes[ctr];

        /* if entry is a write */
        if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_WRITE ||
           dc_handle->state == GLOBUS_FTP_DATA_STATE_SEND_EOF)
        {
            if(!globus_fifo_empty(&stripe->command_q))
            {
                globus_l_ftp_control_data_adjust_connection(stripe);
            }
            while(!globus_fifo_empty(&stripe->free_conn_q) &&
                  !globus_fifo_empty(&stripe->command_q))
            {
                entry = (globus_l_ftp_handle_table_entry_t *)
                    globus_fifo_dequeue(&stripe->command_q);

                data_conn = (globus_ftp_data_connection_t *)
                    globus_fifo_dequeue(&stripe->free_conn_q);
                entry->whos_my_daddy = data_conn;

                if(entry->direction == GLOBUS_FTP_DATA_STATE_CONNECT_WRITE)
                {
                    /*
                     *  if an eof message
                     */
                    if(entry->eof)
                    {
                        /*
                         *  increment the callback reference to all
                         *  current data connections
                         *    stripe->connection_count
                         */
                        globus_handle_table_increment_reference_by(
                            &transfer_handle->handle_table,
                            transfer_handle->eof_table_handle,
                            stripe->connection_count +
                            stripe->outstanding_connections - 1);

                        /*
                         *  if there is no payload use this data connection
                         *  to send and EOF message if we are sending the
                         *  EOF message
                         */
                        if(entry->length == 0)
                        {
                            /* send eof message */
                            if(dc_handle->send_eof)
                            {
                                res = globus_l_ftp_control_data_register_eof(
                                          stripe,
                                          data_conn);
                                globus_assert(res == GLOBUS_SUCCESS);

                                globus_free(entry);
                            }
                            /* send eod message */
                            else
                            {
                                transfer_handle->ref++;
                                /* kick out a callback */
                                GlobusTimeReltimeSet(reltime, 0, 0);
                                globus_callback_register_oneshot(
                                    GLOBUS_NULL,
                                    &reltime,
                                    globus_l_ftp_control_send_data_kickout,
                                    (void *) entry);
                            }
                        }
                        /*
                         *  if there is payload
                         */
                        else
                        {
                            eb_header = (globus_l_ftp_eb_header_t *)
                                globus_malloc(sizeof(globus_l_ftp_eb_header_t));
                            eb_header->descriptor = 0;

                            tmp_buf = entry->buffer;
                            tmp_len = entry->length;
                            if(stripe->whos_my_daddy->whos_my_daddy->type ==
                                   GLOBUS_FTP_CONTROL_TYPE_ASCII)
                            {
                                entry->ascii_buffer =
                                    globus_l_ftp_control_add_ascii(
                                        entry->buffer,
                                        entry->length,
                                        &tmp_len);
                                tmp_buf = entry->ascii_buffer;
                            }

                            globus_l_ftp_control_data_encode(
                                 eb_header->count,
                                 tmp_len);
                            globus_l_ftp_control_data_encode(
                                 eb_header->offset,
                                 entry->offset);

                            io_vec = (struct iovec *)globus_malloc(
                                         sizeof(struct iovec) * 2);

                            /* populate the header */
                            io_vec[0].iov_base = eb_header;
                            io_vec[0].iov_len =
                                 sizeof(globus_l_ftp_eb_header_t);
                            io_vec[1].iov_base = tmp_buf;
                            io_vec[1].iov_len = tmp_len;

                            res = globus_io_register_writev(
                                         &data_conn->io_handle,
                                         io_vec,
                                         2,
                                         globus_l_ftp_eb_write_callback,
                                        (void *)entry);
                            globus_assert(res == GLOBUS_SUCCESS);
                        }

                        /* this will stop adjusting connections */
                        stripe->eof = GLOBUS_TRUE;

                        /*
                         *  register an empty EOD on all data connection
                         *  except for the one that was just removed.  We
                         *  will need that connection open to send
                         *  final EOF message.
                         */
                        while(!globus_fifo_empty(&stripe->free_conn_q))
                        {
                            data_conn2 = (globus_ftp_data_connection_t *)
                                globus_fifo_dequeue(&stripe->free_conn_q);

                            res = globus_l_ftp_control_data_register_eod(
                                      stripe,
                                      data_conn2);
                            globus_assert(res == GLOBUS_SUCCESS);
                        }
                    }
                    /* not an eof message */
                    else
                    {
                        eb_header = (globus_l_ftp_eb_header_t *)
                            globus_malloc(sizeof(globus_l_ftp_eb_header_t));
                        eb_header->descriptor = 0;

                        tmp_buf = entry->buffer;
                        tmp_len = entry->length;
                        if(stripe->whos_my_daddy->whos_my_daddy->type ==
                               GLOBUS_FTP_CONTROL_TYPE_ASCII)
                        {
                            entry->ascii_buffer =
                                globus_l_ftp_control_add_ascii(
                                    entry->buffer,
                                    entry->length,
                                    &tmp_len);
                            tmp_buf = entry->ascii_buffer;
                        }

                        globus_l_ftp_control_data_encode(
                             eb_header->count,
                             tmp_len);
                        globus_l_ftp_control_data_encode(
                             eb_header->offset,
                             entry->offset);

                        io_vec = (struct iovec *)globus_malloc(
                                     sizeof(struct iovec) * 2);

                        /* populate the header */
                        io_vec[0].iov_base = eb_header;
                        io_vec[0].iov_len = sizeof(globus_l_ftp_eb_header_t);
                        io_vec[1].iov_base = tmp_buf;
                        io_vec[1].iov_len = tmp_len;

                        res = globus_io_register_writev(
                                  &data_conn->io_handle,
                                  io_vec,
                                  2,
                                  globus_l_ftp_eb_write_callback,
                                  (void *)entry);
                        globus_assert(res == GLOBUS_SUCCESS);
                    }
                }
                else if(entry->direction == GLOBUS_FTP_DATA_STATE_SEND_EOF)
                {
                    globus_l_ftp_send_eof_entry_t *           tmp_ent;

                    tmp_ent = (globus_l_ftp_send_eof_entry_t *)entry;

                    res = globus_l_ftp_control_data_send_eof(
                              dc_handle,
                              data_conn,
                              (globus_l_ftp_send_eof_entry_t *)tmp_ent);
                    globus_assert(res == GLOBUS_SUCCESS);
                }
            }/* end while */
        }
        else if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_READ
		|| (dc_handle->state == GLOBUS_FTP_DATA_STATE_EOF &&
		    transfer_handle->direction == GLOBUS_FTP_DATA_STATE_CONNECT_READ))
        {
            /*
             *  if we are reading a big buffer
             */
            if(transfer_handle->big_buffer != GLOBUS_NULL)
            {
                if(stripe->eod_count == stripe->eods_received)
                {
                    transfer_handle->ref++;
                    /* delay setting eof till command kickout */
                    /* dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF; */
                    TABLE_ENTRY_MALLOC(
                        entry,
                        transfer_handle->big_buffer,
                        0,
                        transfer_handle->big_buffer_length,
                        GLOBUS_TRUE,
                        transfer_handle->big_buffer_cb,
                        transfer_handle->big_buffer_cb_arg,
                        dc_handle);

                    transfer_handle->big_buffer = GLOBUS_NULL;
                    GlobusTimeReltimeSet(reltime, 0, 0);
                    globus_callback_register_oneshot(
                        GLOBUS_NULL,
                        &reltime,
                        globus_l_ftp_control_command_kickout,
                        (void *) entry);
                }
            }

            /*
             *  in big buffer mode this should never be entered
             */
            if(transfer_handle->order_data)
            {
                globus_fifo_init(&remaining_command_q);
                globus_fifo_init(&remaining_conn_q);
            }
            while(!globus_fifo_empty(&stripe->command_q) && !done)
            {
                globus_assert(transfer_handle->big_buffer == GLOBUS_NULL);

                /*
                 *  if we are at EOF we can not register a read,
                 *  simply kick out the callback with eof set to true.
                 */
                if(stripe->eod_count == stripe->eods_received)
                {
                    /* delay setting eof till command kickout */
                    /* dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF; */
                    entry = (globus_l_ftp_handle_table_entry_t *)
                        globus_fifo_dequeue(&stripe->command_q);
                    /*
                     *  once EOF is set to true there should be no more
                     *  data connections
                     */
                    globus_assert(globus_fifo_empty(&stripe->free_conn_q));

                    GlobusTimeReltimeSet(reltime, 0, 0);
                    globus_callback_register_oneshot(
                        GLOBUS_NULL,
                        &reltime,
                        globus_l_ftp_control_command_kickout,
                        (void *) entry);
                }
                /*
                 *  if we are not at eof and there is a free data connection
                 */
                else if(!globus_fifo_empty(&stripe->free_conn_q))
                {
                    entry = (globus_l_ftp_handle_table_entry_t *)
                                globus_fifo_dequeue(&stripe->command_q);

                    data_conn = (globus_ftp_data_connection_t *)
                        globus_fifo_dequeue(&stripe->free_conn_q);

                    if(transfer_handle->order_data)
                    {                        
                        /* forcing ordered data */
                        checked_order = GLOBUS_TRUE;
                        
                        /* find offset we are looking for */
                        while(data_conn->offset != 
                            transfer_handle->order_next_offset && 
                            !globus_fifo_empty(&stripe->free_conn_q))
                        {
                            globus_fifo_enqueue(&remaining_conn_q, data_conn);
                            data_conn = (globus_ftp_data_connection_t *)
                                globus_fifo_dequeue(&stripe->free_conn_q);
                        }

                        globus_i_ftp_control_debug_printf(9,
                            (stderr, "w: %"GLOBUS_OFF_T_FORMAT", h: %"GLOBUS_OFF_T_FORMAT", %p\n", 
                                transfer_handle->order_next_offset, 
                                data_conn->offset,
                                (void *) data_conn));
                                
                        /* if this stream has the offset we want, 
                         * register the read, otherwise queue up the read */
                        if(data_conn->offset == transfer_handle->order_next_offset) 
                        {
                            do_read = GLOBUS_TRUE;
                            transfer_handle->order_ctr = 0;
                        }
                        else
                        {
                            do_read = GLOBUS_FALSE;
                            done = GLOBUS_TRUE;
                            globus_fifo_enqueue(&remaining_conn_q, data_conn);
                            
                            /* offset not available, restore queues */
                            globus_fifo_enqueue(&remaining_command_q, entry);
                            while(!globus_fifo_empty(&stripe->command_q))
                            {
                                globus_fifo_enqueue(&remaining_command_q,
                                    globus_fifo_dequeue(&stripe->command_q));
                            }
                            while(!globus_fifo_empty(&remaining_command_q))
                            {
                                globus_fifo_enqueue(&stripe->command_q,
                                    globus_fifo_dequeue(&remaining_command_q));
                            }
                        }

                        while(!globus_fifo_empty(&remaining_conn_q))
                        {
                            globus_fifo_enqueue(&stripe->free_conn_q,
                                globus_fifo_dequeue(&remaining_conn_q));
                        }

                    }
                    else
                    {
                        do_read = GLOBUS_TRUE;
                    }

                    if(do_read)
                    {
                        /*
                         *  set the entry's offset to the offset on the
                         *  data_conn.
                         *  If user is requesting more bytes than are available
                         *  on this connection set the length to bytes_ready
                         */
                        entry->whos_my_daddy = data_conn;
                        entry->offset = data_conn->offset;
                        if(entry->length > data_conn->bytes_ready)
                        {
                            entry->length = data_conn->bytes_ready;
                        }
                        /*
                         *  register a read
                         */
                        res = globus_io_register_read(
                                  &data_conn->io_handle,
                                  entry->buffer,
                                  entry->length,
                                  entry->length,
                                  globus_l_ftp_eb_read_callback,
                                  (void *)entry);
                        globus_assert(res == GLOBUS_SUCCESS);
                        
                        transfer_handle->order_next_offset += entry->length;
                    }
                }
                /*
                 *  if we have not hit EOF and there are no available data
                 *  connections
                 */
                else
                {
                    done = GLOBUS_TRUE;
                }
            }/* end while */
            if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_READ && 
                transfer_handle->order_data)
            {
                int                     pending_connections;
                
                pending_connections = 
                    stripe->total_connection_count
                    + stripe->outstanding_connections
                    - globus_fifo_size(&stripe->free_conn_q);
                globus_i_ftp_control_debug_printf(9,
                    (stderr, "all:%d, new:%d, idle:%d, pending:%d\n",
                        stripe->total_connection_count,
                        stripe->outstanding_connections,
                        globus_fifo_size(&stripe->free_conn_q),
                        pending_connections));
                if(pending_connections == 0 && checked_order)
                {   
                    /* give time for pending incoming connections */
                    if(transfer_handle->order_ctr > stripe->total_connection_count * 2 + 10)
                    {
                        globus_object_t *       error;
                        error = globus_error_construct_string(
                            GLOBUS_FTP_CONTROL_MODULE,
                            GLOBUS_NULL,
                            _FCSL("Ordered data flag was set, but data is too far out of order."));
                        globus_l_ftp_control_stripes_destroy(dc_handle, error);
                    }
                    transfer_handle->order_ctr++;
                }
            }
            if(transfer_handle->order_data)
            {
                globus_fifo_destroy(&remaining_command_q);
                globus_fifo_destroy(&remaining_conn_q);
            }
        }
    }

    return GLOBUS_SUCCESS;
}

/*
 *  globus_l_ftp_control_data_register_eof()
 *  ----------------------------------------
 *  build and resister an eof message.  Decrement the number of
 *  connections on the current stripe.
 */
globus_result_t
globus_l_ftp_control_data_register_eof(
    globus_ftp_data_stripe_t *                   stripe,
    globus_ftp_data_connection_t *               data_conn)
{
    globus_l_ftp_eb_header_t *                   eb_header;
    globus_result_t                              res;
    globus_l_ftp_data_callback_info_t *          cb_info;

    globus_assert(stripe->eof_sent == GLOBUS_FALSE);
    if(stripe->eof_sent)
    {
        /* I can do this because I only use this internally */
        return globus_error_put(GLOBUS_ERROR_NO_INFO);
    }

    stripe->eof_sent = GLOBUS_TRUE;

    eb_header = (globus_l_ftp_eb_header_t *)
        globus_malloc(sizeof(globus_l_ftp_eb_header_t));
    memset(eb_header, '\0', sizeof(globus_l_ftp_eb_header_t));
    eb_header->descriptor =
        GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_EOF |
        GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_EOD;
    globus_l_ftp_control_data_encode(
        eb_header->offset,
        stripe->total_connection_count);

    CALLBACK_INFO_MALLOC(
        cb_info,
        stripe->whos_my_daddy->whos_my_daddy,
        stripe->whos_my_daddy,
        stripe,
        data_conn);

    stripe->connection_count--;
    stripe->whos_my_daddy->ref++;
    res = globus_io_register_write(
              &data_conn->io_handle,
              (globus_byte_t *)eb_header,
              sizeof(globus_l_ftp_eb_header_t),
              globus_l_ftp_eb_eof_eod_callback,
              (void *)cb_info);

    return res;
}

/*
 *  globus_l_ftp_control_data_register_eod()
 *  ----------------------------------------
 *  creates and registers a empty EOD message on the
 *  given connection.  Decrements the connection count.
 */
globus_result_t
globus_l_ftp_control_data_register_eod(
    globus_ftp_data_stripe_t *                   stripe,
    globus_ftp_data_connection_t *               data_conn)
{
    globus_l_ftp_eb_header_t *                   eb_header;
    globus_result_t                              res;
    globus_l_ftp_data_callback_info_t *          cb_info;

    eb_header = (globus_l_ftp_eb_header_t *)
        globus_malloc(sizeof(globus_l_ftp_eb_header_t));
    memset(eb_header, '\0', sizeof(globus_l_ftp_eb_header_t));
    eb_header->descriptor =
         GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_EOD;

    stripe->whos_my_daddy->ref++;
    stripe->connection_count--;

    CALLBACK_INFO_MALLOC(
        cb_info,
        stripe->whos_my_daddy->whos_my_daddy,
        stripe->whos_my_daddy,
        stripe,
        data_conn);
    res = globus_io_register_write(
              &data_conn->io_handle,
              (globus_byte_t *)eb_header,
              sizeof(globus_l_ftp_eb_header_t),
              globus_l_ftp_eb_eof_eod_callback,
              (void *)cb_info);
    globus_assert(res == GLOBUS_SUCCESS);

    return res;
}

/*
 *  this function is kicked out when EOF is not automatically sent.
 *  When not automatically sent we need to leave a final data connection
 *  open for sending the EOF message.  This function will call the users
 *  callback if all reference to the EOF function are gone.
 *
 *  This function works like a dummy _eod callback.
 */
static
void
globus_l_ftp_control_send_data_kickout(
    void *                                      user_args)
{
    globus_i_ftp_dc_handle_t *                   dc_handle;
    globus_ftp_control_data_callback_t           eof_callback = GLOBUS_NULL;
    globus_l_ftp_handle_table_entry_t *          eof_cb_ent;
    globus_i_ftp_dc_transfer_handle_t *          transfer_handle;
    globus_l_ftp_handle_table_entry_t *          entry;
    globus_ftp_data_connection_t *               data_conn;
    globus_ftp_data_stripe_t *                   stripe;
    globus_bool_t                               poll;
    globus_l_ftp_send_eof_entry_t *              send_eof_ent = GLOBUS_NULL;

    entry = (globus_l_ftp_handle_table_entry_t *)user_args;
    data_conn = entry->whos_my_daddy;
    stripe = data_conn->whos_my_daddy;

    dc_handle = entry->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    transfer_handle = entry->transfer_handle;

    globus_mutex_lock(&dc_handle->mutex);
    {
        eof_cb_ent = globus_handle_table_lookup(
                         &transfer_handle->handle_table,
                         transfer_handle->eof_table_handle);

        if(eof_cb_ent && !globus_handle_table_decrement_reference(
          &transfer_handle->handle_table,
           transfer_handle->eof_table_handle))
        {
            eof_callback = eof_cb_ent->callback;
            send_eof_ent = transfer_handle->send_eof_ent;
            transfer_handle->eof_cb_ent = GLOBUS_NULL;

            /*
             *  if the send_eof callback has already happened
             *  we call it, otherwise we set the state to SEND_EOF
             */
            if(send_eof_ent == GLOBUS_NULL)
            {
                dc_handle->state = GLOBUS_FTP_DATA_STATE_SEND_EOF;
            }
            else
            {
                dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF;
            }
        }

        globus_fifo_enqueue(&stripe->free_conn_q, data_conn);
    }
    globus_mutex_unlock(&dc_handle->mutex);

    if(eof_callback != GLOBUS_NULL)
    {
        eof_callback(
            eof_cb_ent->callback_arg,
            dc_handle->whos_my_daddy,
            GLOBUS_NULL,
            eof_cb_ent->buffer,
            eof_cb_ent->length,
            eof_cb_ent->offset,
            GLOBUS_TRUE);

        globus_free(eof_cb_ent);
    }

    /*
     *  call the send EOF callback.  This is done to insure that it
     *  is called after the EOF callback
     */
    if(send_eof_ent != GLOBUS_NULL)
    {
        send_eof_ent->cb(
            send_eof_ent->user_arg,
            dc_handle->whos_my_daddy,
            GLOBUS_NULL);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        /*
         *  decrement the reference the callbacks had
         */
        if(eof_callback != GLOBUS_NULL)
        {
            globus_l_ftp_control_dc_dec_ref(transfer_handle);
        }
        if(send_eof_ent != GLOBUS_NULL)
        {
            globus_l_ftp_control_dc_dec_ref(transfer_handle);
        }
        /*
         *  decrement the reference this callback has
         */
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    globus_free(entry);
}


/*
 *  this should be called with the control_handle mutex locked
 */
void
globus_l_ftp_control_stripes_create(
    globus_i_ftp_dc_handle_t *                   dc_handle,
    globus_ftp_control_host_port_t               addresses[],
    int                                          stripe_count)
{
    int                                          ctr;
    globus_ftp_data_stripe_t *                   stripe;
    globus_i_ftp_dc_transfer_handle_t *          transfer_handle;

    dc_handle->transfer_handle = (globus_i_ftp_dc_transfer_handle_t *)
                  globus_malloc(sizeof(globus_i_ftp_dc_transfer_handle_t));

    transfer_handle = dc_handle->transfer_handle;

    transfer_handle->mutex = &dc_handle->mutex;

    transfer_handle->eof_registered = GLOBUS_FALSE;
    transfer_handle->eof_cb_ent = GLOBUS_NULL;
    transfer_handle->big_buffer = GLOBUS_NULL;
    transfer_handle->big_buffer_cb = GLOBUS_NULL;
    transfer_handle->big_buffer_cb_arg = GLOBUS_NULL;

    transfer_handle->send_eof_ent = GLOBUS_NULL;

    transfer_handle->x_state = GLOBUS_FALSE;

    globus_mutex_init(&transfer_handle->order_mutex, GLOBUS_NULL);
    globus_priority_q_init(
        &transfer_handle->ordered_buffer_q, globus_l_ftp_control_q_compare);
    transfer_handle->order_data = GLOBUS_FALSE;
    transfer_handle->order_ctr = 0;
    transfer_handle->order_next_offset = 0;
    transfer_handle->order_next_offset_passed = 0;

    transfer_handle->direction = GLOBUS_FTP_DATA_STATE_NONE;
    transfer_handle->whos_my_daddy = dc_handle;
    transfer_handle->control_handle = dc_handle->whos_my_daddy;
    transfer_handle->ref = 1;

    globus_list_insert(&dc_handle->transfer_list, transfer_handle);

    globus_handle_table_init(
        &transfer_handle->handle_table,
        GLOBUS_NULL);

    /*
     *  Add new structure to list for destruction in case
     *  deactivate is called without properly destroying it.
     */
    globus_list_insert(
        &globus_l_ftp_control_data_dc_list,
        (void *)transfer_handle);

    transfer_handle->stripes =
       (globus_ftp_data_stripe_t *)
              globus_malloc(sizeof(globus_ftp_data_stripe_t) * stripe_count);
    transfer_handle->stripe_count = stripe_count;

    for(ctr = 0; ctr < stripe_count; ctr++)
    {
        stripe = &transfer_handle->stripes[ctr];

        globus_i_ftp_parallelism_copy(&stripe->parallel,
            &dc_handle->parallel);
        stripe->stripe_ndx = ctr;
        stripe->outstanding_connections = 0;

        globus_fifo_init(&stripe->free_conn_q);
        stripe->all_conn_list = GLOBUS_NULL;
        stripe->outstanding_conn_list = GLOBUS_NULL;
        stripe->free_cache_list = GLOBUS_NULL;

        stripe->listening = GLOBUS_FALSE;
        globus_fifo_init(&stripe->command_q);

        stripe->total_connection_count = 0;
        stripe->eods_received = 0;
        stripe->eof_sent = GLOBUS_FALSE;
        stripe->eof = GLOBUS_FALSE;
        stripe->eod_count = -1;

        stripe->whos_my_daddy = transfer_handle;
        stripe->connection_count = 0;
        globus_ftp_control_host_port_copy(&stripe->host_port, &addresses[ctr]);

        transfer_handle->ref++;
    }
}

globus_result_t
globus_l_ftp_control_register_close_msg(
    globus_i_ftp_dc_handle_t *                   dc_handle,
    globus_ftp_data_connection_t *               data_conn)
{
    globus_l_ftp_eb_header_t *                   eb_header;
    globus_result_t                              res;
    globus_l_ftp_data_callback_info_t *          cb_info;

    eb_header = (globus_l_ftp_eb_header_t *)
                     globus_malloc(sizeof(globus_l_ftp_eb_header_t));

    memset(eb_header, '\0', sizeof(globus_l_ftp_eb_header_t));
    eb_header->descriptor |= GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_CLOSE;
    if(data_conn->eod)
    {
        eb_header->descriptor |= GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_EOD;
    }

    CALLBACK_INFO_MALLOC(
        cb_info,
        dc_handle,
        dc_handle->transfer_handle,
        data_conn->whos_my_daddy,
        data_conn);

    res = globus_io_register_write(
              &data_conn->io_handle,
              (globus_byte_t *)eb_header,
              sizeof(globus_l_ftp_eb_header_t),
              globus_l_ftp_close_msg_callback,
              (void *)cb_info);

    return res;
}

static
void
globus_l_ftp_control_io_close_kickout(
    void *                                      user_args)
{
    globus_l_ftp_io_close_callback(user_args, GLOBUS_NULL, GLOBUS_SUCCESS);
}

/*
 *  globus_l_ftp_control_stripes_destroy()
 *  --------------------------------------
 *  destroy all structures and connections associated with a stripe
 *  close all data connections on a given stripe
 *  this should be called locked
 */
void
globus_l_ftp_control_stripes_destroy(
    globus_i_ftp_dc_handle_t *                   dc_handle,
    globus_object_t *                            error)
{
    globus_ftp_data_connection_t *               data_conn;
    int                                          ctr;
    globus_ftp_data_stripe_t *                   stripe;
    globus_l_ftp_data_callback_info_t *          callback_info;
    globus_result_t                              res;
    globus_i_ftp_dc_transfer_handle_t *          transfer_handle;
    globus_list_t *                              list;

    if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING ||
       dc_handle->transfer_handle == GLOBUS_NULL)
    {
        /* I can do this because I only use it internally */
        return;
    }

    transfer_handle = dc_handle->transfer_handle;
    dc_handle->state = GLOBUS_FTP_DATA_STATE_CLOSING;
    /*
     *  orphan transfer handle
     */
    dc_handle->transfer_handle = GLOBUS_NULL;

    /*
     *  clean up all the stripes
     */
    for(ctr = 0; transfer_handle != GLOBUS_NULL &&
        ctr < transfer_handle->stripe_count; ctr++)
    {
        stripe = &transfer_handle->stripes[ctr];

        /*
         *  if there are outstanding commands to process call all the
         *  callbacks with errors
         */
        globus_l_error_flush_command_q(stripe, error);

        /*
         *  register close on all open data connections
         */
        while(!globus_list_empty(stripe->all_conn_list))
        {
            data_conn = (globus_ftp_data_connection_t *)
                globus_list_first(stripe->all_conn_list);
            data_conn->free_me = GLOBUS_TRUE;

            CALLBACK_INFO_MALLOC(
                callback_info,
                dc_handle,
                transfer_handle,
                stripe,
                data_conn);
            /*
              *  this will force out all remaining callbacks
              */
            res = globus_io_register_close(
                          &data_conn->io_handle,
                          globus_l_ftp_io_close_callback,
                          (void *)callback_info);
             
            if(res != GLOBUS_SUCCESS)
            {
                res = globus_callback_register_oneshot(
                     GLOBUS_NULL,
                     GLOBUS_NULL,
                     globus_l_ftp_control_io_close_kickout,
                     callback_info);
                globus_assert(res == GLOBUS_SUCCESS);
            }
            globus_list_remove(
                 &stripe->all_conn_list,
                 stripe->all_conn_list);
        }

        for(list = stripe->outstanding_conn_list;
           !globus_list_empty(list);
           list = globus_list_rest(list))
        {
            data_conn = (globus_ftp_data_connection_t *)
                globus_list_first(list);
            data_conn->free_me = GLOBUS_FALSE;

            CALLBACK_INFO_MALLOC(
                callback_info,
                dc_handle,
                transfer_handle,
                stripe,
                data_conn);

            res = globus_io_register_close(
                      &data_conn->io_handle,
                      globus_l_ftp_io_close_callback,
                      (void *)callback_info);
            if(res != GLOBUS_SUCCESS)
            {
                res = globus_callback_register_oneshot(
                         GLOBUS_NULL,
                         GLOBUS_NULL,
                         globus_l_ftp_control_io_close_kickout,
                         callback_info);
                globus_assert(res == GLOBUS_SUCCESS);
            }
        }

        globus_list_free(stripe->free_cache_list);

        if(stripe->listening)
        {
            stripe->listening = GLOBUS_FALSE;
            CALLBACK_INFO_MALLOC(
                callback_info,
                dc_handle,
                transfer_handle,
                stripe,
                GLOBUS_NULL);
            res = globus_io_register_close(
                &stripe->listener_handle,
                globus_l_ftp_io_close_callback,
                (void *)callback_info);
            if(res != GLOBUS_SUCCESS)
            {
                res = globus_callback_register_oneshot(
                         GLOBUS_NULL,
                         GLOBUS_NULL,
                         globus_l_ftp_control_io_close_kickout,
                         callback_info);
                globus_assert(res == GLOBUS_SUCCESS);
            }
        }
        /* remove the reference the stripe had to it */
        globus_l_ftp_control_dc_dec_ref(transfer_handle);
    }

    /*
     *  remove reference the transfer handle has to itself
     */
    globus_l_ftp_control_dc_dec_ref(transfer_handle);

    return;
}

globus_result_t
globus_l_ftp_control_data_adjust_connection(
    globus_ftp_data_stripe_t *                  stripe)
{
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_ftp_data_connection_t *              data_conn;
    globus_result_t                             res = GLOBUS_SUCCESS;
    int                                         ctr;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;

    transfer_handle =  stripe->whos_my_daddy;
    dc_handle = transfer_handle->whos_my_daddy;
    GlobusFTPControlDataTestMagic(dc_handle);

    /*
     *  only the writer connects
     *  do not adjust parallel level if stripe set to EOF
     */
    if(dc_handle->state != GLOBUS_FTP_DATA_STATE_CONNECT_WRITE ||
       stripe->eof)
    {
        return GLOBUS_SUCCESS;
    }

    /*
     *  if not enough connections register a new connection
     */
    if(stripe->parallel.base.size > stripe->connection_count
            + stripe->outstanding_connections)
    {
        for(ctr = stripe->connection_count + stripe->outstanding_connections;
            ctr < stripe->parallel.base.size;
            ctr++)
        {
            res = globus_l_ftp_control_data_register_connect(
                      dc_handle,
                      stripe,
                      GLOBUS_NULL,
                      GLOBUS_NULL);
            if(res != GLOBUS_SUCCESS)
            {
                return res;
            }
        }
    }
    else if(stripe->parallel.base.size < stripe->connection_count
            + stripe->outstanding_connections)
    {
        if(!globus_fifo_empty(&stripe->free_conn_q))
        {
            data_conn = (globus_ftp_data_connection_t *)
                 globus_fifo_dequeue(&stripe->free_conn_q);
            globus_list_remove(&stripe->all_conn_list, 
                globus_list_search(stripe->all_conn_list, data_conn));

            data_conn->eod = GLOBUS_TRUE;
            stripe->connection_count--;
            data_conn->whos_my_daddy = NULL;
            res = globus_l_ftp_control_register_close_msg(
                      dc_handle,
                      data_conn);
        }
    }
    /*
     * TODO:  remove a connection if the current count is too high.
     */

    return res;
}

static
void
globus_l_ftp_control_command_flush_callback(
    void *                                      user_args)
{
    globus_l_ftp_handle_table_entry_t *          entry;
    globus_l_ftp_handle_table_entry_t *          cb_ent;
    globus_i_ftp_dc_handle_t *                   dc_handle;
    globus_i_ftp_dc_transfer_handle_t *          transfer_handle;
    globus_ftp_control_data_callback_t           callback = GLOBUS_NULL;

    entry = (globus_l_ftp_handle_table_entry_t *)user_args;
    dc_handle = entry->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    transfer_handle = entry->transfer_handle;

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(entry->direction == GLOBUS_FTP_DATA_STATE_CONNECT_READ ||
            dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_STREAM)
        {
            cb_ent = entry;
            callback = cb_ent->callback;
        }
        else
        {
            cb_ent = globus_handle_table_lookup(
                     &transfer_handle->handle_table,
                     entry->callback_table_handle);
            globus_assert(cb_ent != GLOBUS_NULL);

            if(!globus_handle_table_decrement_reference(
                   &transfer_handle->handle_table,
                   entry->callback_table_handle))
            {
                callback = cb_ent->callback;
                if(cb_ent->eof)
                {
                    dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF;
                }
            }
            if(entry->ascii_buffer != GLOBUS_NULL)
            {
                globus_free(entry->ascii_buffer);
            }
        }

    }
    globus_mutex_unlock(&dc_handle->mutex);

    /*
     *  call any callbacks that are ready
     */
    if(callback != GLOBUS_NULL)
    {
        callback(
            cb_ent->callback_arg,
            dc_handle->whos_my_daddy,
            entry->error,
            cb_ent->buffer,
            0,
            0,
            GLOBUS_TRUE);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(callback != GLOBUS_NULL)
        {
            globus_l_ftp_control_dc_dec_ref(transfer_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);
    
    if(entry->error)
    {
        globus_object_free(entry->error);
    }
    globus_free(entry);
}

/*
 *  on error flush out all user callbacks with errors
 *  this function is called locked
 */
void
globus_l_error_flush_command_q(
    globus_ftp_data_stripe_t *               stripe,
    globus_object_t *                        error)
{
    globus_l_ftp_handle_table_entry_t *      entry;
    globus_reltime_t                         reltime;

    while(!globus_fifo_empty(&stripe->command_q))
    {
        entry = (globus_l_ftp_handle_table_entry_t *)
            globus_fifo_dequeue(&stripe->command_q);

        if(error != GLOBUS_NULL)
        {
            entry->error = globus_object_copy(error);
        }
        else
        {
            entry->error = GLOBUS_NULL;
        }

        GlobusTimeReltimeSet(reltime, 0, 0);
        globus_callback_register_oneshot(
            GLOBUS_NULL,
            &reltime,
            globus_l_ftp_control_command_flush_callback,
            (void *) entry);
    }
}

static
void
globus_l_ftp_control_command_kickout(
    void *                                      user_args)
{
    globus_l_ftp_handle_table_entry_t *          entry;
    globus_i_ftp_dc_handle_t *                   dc_handle;
    globus_i_ftp_dc_transfer_handle_t *          transfer_handle;
    globus_bool_t                               poll;

    entry = (globus_l_ftp_handle_table_entry_t *)user_args;
    dc_handle = entry->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    transfer_handle = entry->transfer_handle;
    
    if(entry->callback)
    {
        entry->callback(
            entry->callback_arg,
            dc_handle->whos_my_daddy,
            entry->error,
            entry->buffer,
            0,
            0,
            GLOBUS_TRUE);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF;
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        /* purge any callbacks registered before EOF was sent */
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    if(entry->error != GLOBUS_NULL)
    {
        globus_object_free(entry->error);
    }

    globus_free(entry);
    
}

static
void
globus_l_ftp_control_reuse_connect_callback(
    void *                                      user_args)
{
    globus_l_ftp_dc_connect_cb_info_t *          connect_cb_info;
    globus_i_ftp_dc_handle_t *                   dc_handle;

    connect_cb_info = (globus_l_ftp_dc_connect_cb_info_t *)user_args;

    dc_handle = connect_cb_info->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);

    connect_cb_info->callback(
        connect_cb_info->user_arg,
        dc_handle->whos_my_daddy,
        connect_cb_info->stripe_ndx,
        GLOBUS_TRUE,
        GLOBUS_NULL);

    globus_mutex_lock(&dc_handle->mutex);
    {
        globus_l_ftp_control_dc_dec_ref(connect_cb_info->transfer_handle);
    }
    globus_mutex_unlock(&dc_handle->mutex);

    globus_free(connect_cb_info);
}

globus_result_t
globus_ftp_control_data_add_closed_cb(
    globus_ftp_control_handle_t *                control_handle,
    globus_ftp_control_callback_t                close_callback_func,
    void *                                       close_arg)
{
    globus_result_t                              result = GLOBUS_SUCCESS;
    globus_i_ftp_dc_handle_t *                   dc_handle;

    dc_handle = &control_handle->dc_handle;
    globus_mutex_lock(&dc_handle->mutex);
    {
        dc_handle->close_callback = close_callback_func;
        dc_handle->close_callback_arg = close_arg;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return result;
}


/*
 *  globus_i_ftp_control_data_cc_init()
 *  -----------------------------------
 *  called when the user calls globus_ftp_control_handle_init()
 *  Initialized the dc_handle structure.
 */
globus_result_t
globus_i_ftp_control_data_cc_init(
    globus_ftp_control_handle_t *                control_handle)
{
    globus_i_ftp_dc_handle_t *                   dc_handle;
    globus_result_t                              res;
    globus_object_t *                            err;

    globus_mutex_lock(&globus_l_ftp_control_data_mutex);
    {
        if(globus_l_ftp_control_data_active)
        {
            dc_handle = &control_handle->dc_handle;

            GlobusFTPControlSetMagic(dc_handle);

            dc_handle->initialized = GLOBUS_TRUE;
            dc_handle->state = GLOBUS_FTP_DATA_STATE_NONE;
            dc_handle->dcau.mode = GLOBUS_FTP_CONTROL_DCAU_NONE;
            dc_handle->pbsz = 0UL;
            dc_handle->protection = GLOBUS_FTP_CONTROL_PROTECTION_CLEAR;
            dc_handle->mode = GLOBUS_FTP_CONTROL_MODE_STREAM;
            dc_handle->type = GLOBUS_FTP_CONTROL_TYPE_ASCII;
            dc_handle->structure = GLOBUS_FTP_CONTROL_STRUCTURE_FILE;
            dc_handle->tcp_buffer_size = 0;
            dc_handle->form_code = 0;
            dc_handle->send_eof = GLOBUS_TRUE;
            dc_handle->transfer_handle = GLOBUS_NULL;
            dc_handle->whos_my_daddy = control_handle;
            dc_handle->transfer_list = GLOBUS_NULL;
            dc_handle->close_callback = GLOBUS_NULL;
            dc_handle->close_callback_arg = GLOBUS_NULL;

            dc_handle->nl_io_handle_set = GLOBUS_FALSE;
            dc_handle->nl_ftp_handle_set = GLOBUS_FALSE;

            dc_handle->interface_addr = NULL;
            dc_handle->connect_error = GLOBUS_NULL;
            globus_io_tcpattr_init(&dc_handle->io_attr);
            globus_io_attr_set_tcp_nodelay(&dc_handle->io_attr,
					   GLOBUS_TRUE);

            dc_handle->layout_func = GLOBUS_NULL;
            dc_handle->layout_user_arg = GLOBUS_NULL;
            dc_handle->layout_str = GLOBUS_NULL;

            dc_handle->parallel.base.mode =
               GLOBUS_FTP_CONTROL_PARALLELISM_FIXED;
            dc_handle->parallel.base.size = 1;

            globus_mutex_init(&dc_handle->mutex, GLOBUS_NULL);

            res = GLOBUS_SUCCESS;
        }
        else
        {
            err = globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                  _FCSL("globus_i_ftp_control_data_cc_init(): code not activated."));
            res = globus_error_put(err);
        }
    }
    globus_mutex_unlock(&globus_l_ftp_control_data_mutex);

    return res;
}

/*
 * this should be called locked
 *  this function handles all clean up except for freeing the memory
 *  and destroying the mutex;
 */
globus_bool_t
globus_l_ftp_control_dc_dec_ref(
    globus_i_ftp_dc_transfer_handle_t *          transfer_handle)
{
    globus_bool_t                                rc = GLOBUS_FALSE;
    globus_reltime_t                             reltime;
    globus_i_ftp_dc_handle_t *                   dc_handle;
    int                                          ctr;
    globus_ftp_data_stripe_t *                   stripe;

    globus_assert(transfer_handle->ref > 0);
/*
    if(transfer_handle->ref == 0)
    {
        return GLOBUS_FALSE;
    }
*/
    transfer_handle->ref--;
    dc_handle = transfer_handle->whos_my_daddy;
    globus_assert(transfer_handle->ref >= 0);
    if(transfer_handle->ref == 0)
    {
        rc = GLOBUS_TRUE;
        globus_list_remove_element(
            &dc_handle->transfer_list, transfer_handle);
        /*
         *  if the transfer handle close_callback is not null
         *  it means that stripes destroy was called from force_close
         *  and the user should still have a reference to there handle
         *  in memory.  Therefore the transfer_handle->control_handle
         *  is still valid.
         */
        if(dc_handle->close_callback != GLOBUS_NULL &&
           globus_list_empty(dc_handle->transfer_list))
        {
	    globus_result_t             res;
            GlobusTimeReltimeSet(reltime, 0, 0);
            res = globus_callback_register_oneshot(
                         GLOBUS_NULL,
                         &reltime,
                         globus_l_ftp_control_close_kickout,
                         (void *)dc_handle);
            globus_assert(res == GLOBUS_SUCCESS);
        }
        else if(globus_list_empty(dc_handle->transfer_list))
        {
            dc_handle->state = GLOBUS_FTP_DATA_STATE_NONE;
        }

        /*
         *  destroy the transfer handle
         */
        for(ctr = 0; ctr < transfer_handle->stripe_count; ctr++)
        {
            stripe = &transfer_handle->stripes[ctr];
            globus_fifo_destroy(&stripe->free_conn_q);
            globus_ftp_control_host_port_destroy(&stripe->host_port);
            globus_fifo_destroy(&stripe->command_q);
        }
        globus_mutex_destroy(&transfer_handle->order_mutex);
        globus_priority_q_destroy(&transfer_handle->ordered_buffer_q);
        globus_handle_table_destroy(&transfer_handle->handle_table);
        globus_free(transfer_handle->stripes);
        
        globus_assert(dc_handle->transfer_handle != transfer_handle && 
                "Destroying a transfer_handle we still have a pointer to");
        
        globus_free(transfer_handle);
        globus_cond_signal(&globus_l_ftp_control_data_cond);
    }

    return rc;
}

static
void
globus_l_ftp_control_close_kickout(
    void *                                      user_args)
{
    globus_ftp_control_handle_t *                control_handle;
    globus_ftp_control_callback_t                cb;
    void *                                       cb_arg;
    globus_i_ftp_dc_handle_t *                   dc_handle;

    dc_handle = (globus_i_ftp_dc_handle_t *)user_args;
    GlobusFTPControlDataTestMagic(dc_handle);
    /*
     *  transfer_handle has been orphaned at this point.
     *  However, since this function is a result of a call to force_close
     *  its reference to control_handle should still be valid.
     */
    control_handle = dc_handle->whos_my_daddy;

    globus_mutex_lock(&dc_handle->mutex);
    {
        dc_handle->state = GLOBUS_FTP_DATA_STATE_NONE;

        cb = dc_handle->close_callback;
        cb_arg = dc_handle->close_callback_arg;

        dc_handle->close_callback = GLOBUS_NULL;
        dc_handle->close_callback_arg = GLOBUS_NULL;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    if(cb != GLOBUS_NULL)
    {
        cb(cb_arg, control_handle, GLOBUS_NULL);
    }
}

/*
 *  globus_i_ftp_control_data_cc_destroy()
 *  --------------------------------------
 *  if the handle is in the proper state destroy it.  Otherwise return
 *  failure.
 */
globus_result_t
globus_i_ftp_control_data_cc_destroy(
    globus_ftp_control_handle_t *                control_handle)
{
    globus_i_ftp_dc_handle_t *                   dc_handle;
    globus_result_t                              res = GLOBUS_SUCCESS;
    globus_object_t *                            err;

    dc_handle = &control_handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    globus_mutex_lock(&dc_handle->mutex);
    {
        if(dc_handle->state == GLOBUS_FTP_DATA_STATE_NONE)
        {
            dc_handle->initialized = GLOBUS_FALSE;
            res = GLOBUS_SUCCESS;
	        globus_io_tcpattr_destroy(&dc_handle->io_attr);
            if(dc_handle->nl_io_handle_set)
            {
                globus_netlogger_handle_destroy(&dc_handle->nl_io_handle);
            }
            if(dc_handle->nl_ftp_handle_set)
            {
                globus_netlogger_handle_destroy(&dc_handle->nl_ftp_handle);
            }
            if(dc_handle->interface_addr)
            {
                free(dc_handle->interface_addr);
            }
            if(dc_handle->dcau.mode == GLOBUS_FTP_CONTROL_DCAU_SUBJECT &&
                dc_handle->dcau.subject.subject)
            {
                globus_libc_free(dc_handle->dcau.subject.subject);
            }
            
            if(dc_handle->connect_error)
            {
                globus_object_free(dc_handle->connect_error);
            }
        }
        else
        {
            err = globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
              _FCSL("globus_i_ftp_control_data_cc_destroy(): handle has outstanding references."));
            res = globus_error_put(err);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return res;
}

/**
 * @brief Close data connections
 * @ingroup globus_ftp_control_data
 * @details
 * Forces an immediate close of all data connections.
 *
 *  @param control_handle
 *         The globus_ftp_control_handle that is have its data
 *         connections closed.
 *  @param close_callback_func
 *         A user function that will be called when the data connections
 *         are closed.
 *  @param close_arg
 *         The user argument that will be threaded through to
 *         close_callback_func.
 */
globus_result_t
globus_ftp_control_data_force_close(
    globus_ftp_control_handle_t *                control_handle,
    globus_ftp_control_callback_t                close_callback_func,
    void *                                       close_arg)
{
    globus_result_t                              res;
    globus_i_ftp_dc_handle_t *                   dc_handle;
    globus_object_t *                            err;
    static char *                                myname=
                          "globus_ftp_control_data_force_close";

    /*
     *  error checking
     */
    if(control_handle == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    dc_handle = &control_handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    if(!dc_handle->initialized)
    {
        err = globus_io_error_construct_not_initialized(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }
    if(close_callback_func == GLOBUS_NULL)
    {
        err = globus_io_error_construct_null_parameter(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
                  "handle",
                  1,
                  myname);
        return globus_error_put(err);
    }

    if(control_handle->dc_handle.transfer_handle == GLOBUS_NULL)
    {
        return globus_error_put(globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                _FCSL("Handle not in the proper state")));
    }

    dc_handle = &control_handle->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    globus_mutex_lock(&dc_handle->mutex);
    {
        /* already closed, or closing */
        if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING || dc_handle->state == GLOBUS_FTP_DATA_STATE_NONE)
        {
            err = globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
                  _FCSL("globus_ftp_control_data_force_close(): handle not connected."));
            res = globus_error_put(err);
        }
        else
        {
            err = globus_error_construct_string(
                      GLOBUS_FTP_CONTROL_MODULE,
                      GLOBUS_NULL,
          _FCSL("Data connection has been closed due to a call to globus_ftp_control_data_force_close(), or by deactivating the module."));

            res = globus_i_ftp_control_data_force_close(
                      dc_handle,
                      close_callback_func,
                      close_arg,
                      err);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    return res;
}

globus_result_t
globus_i_ftp_control_data_force_close(
    globus_i_ftp_dc_handle_t *                   dc_handle,
    globus_ftp_control_callback_t                close_callback_func,
    void *                                       close_arg,
    globus_object_t *                            err)
{
    globus_result_t                              result;

    result = GLOBUS_SUCCESS;

    if(close_callback_func != GLOBUS_NULL)
    {
        dc_handle->close_callback = close_callback_func;
        dc_handle->close_callback_arg = close_arg;
    }

    globus_l_ftp_control_stripes_destroy(dc_handle, err);

    globus_object_free(err);

    return result;
}

int
globus_l_ftp_control_strip_ascii(
    globus_byte_t *                                 buf,
    int                                             length)
{
    int                                             ctr;
    int                                             count = 0;

    if(length < 1)
    {
        return length;
    }

    for(ctr = 0; ctr < length - 1; ctr++)
    {
        if(buf[ctr] == '\r' &&
           buf[ctr + 1] == '\n')
        {
            memmove(&buf[ctr], &buf[ctr+1], length - (ctr + 1));
            count++;
        }
    }

    return length - count;
}
globus_byte_t *
globus_l_ftp_control_add_ascii(
    globus_byte_t *                                 in_buf,
    int                                             length,
    globus_off_t *                                  ascii_len)
{
    globus_byte_t *                                 out_buf;
    int                                             ctr;
    int                                             out_ndx = 0;

    if(length < 1)
    {
        *ascii_len = 0;
        return GLOBUS_NULL;
    }

    /* allocating twice the memory may be a bad idea */
    out_buf = (globus_byte_t *)globus_malloc(length*2);

    for(ctr = 0; ctr < length; ctr++)
    {
        if(in_buf[ctr] == '\n')
        {
            out_buf[out_ndx] = '\r';
            out_ndx++;
        }
        out_buf[out_ndx] = in_buf[ctr];
        out_ndx++;
    }

    *ascii_len = out_ndx;

    return out_buf;
}

globus_result_t
globus_l_ftp_control_data_register_connect(
    globus_i_ftp_dc_handle_t *                  dc_handle,
    globus_ftp_data_stripe_t *                  stripe,
    globus_ftp_control_data_connect_callback_t  callback,
    void *                                      user_arg)
{
    globus_ftp_data_connection_t *              data_conn;
    char                                        remote_host[256];
    unsigned int                                remote_port;
    globus_result_t                             result;
    globus_l_ftp_data_callback_info_t *         callback_info;
    globus_object_t *                           err;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;

    transfer_handle = stripe->whos_my_daddy;
    stripe->outstanding_connections++;
    stripe->total_connection_count++;

    DATA_CONN_MALLOC(data_conn, stripe, callback, user_arg);
    transfer_handle->ref++;
    globus_list_insert(&stripe->outstanding_conn_list, (void*)data_conn);

    CALLBACK_INFO_MALLOC(
        callback_info,
        dc_handle,
        transfer_handle,
        stripe,
        data_conn);
    transfer_handle->ref++;

    globus_ftp_control_host_port_get_host(
        &stripe->host_port,
        remote_host);
    remote_port = globus_ftp_control_host_port_get_port(
                     &stripe->host_port);

    /* register the next connection */
    if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_STREAM)
    {
        result = globus_io_tcp_register_connect(
                     remote_host,
                     remote_port,
                     &dc_handle->io_attr,
                     globus_l_ftp_stream_accept_connect_callback,
                     (void *)callback_info,
                      &data_conn->io_handle);
    }
    else if(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK)
    {
        result = globus_io_tcp_register_connect(
                     remote_host,
                     remote_port,
                     &dc_handle->io_attr,
                     globus_l_ftp_eb_connect_callback,
                     (void *)callback_info,
                      &data_conn->io_handle);
    }
    else
    {
        globus_free(callback_info);
        err = globus_error_construct_string(
                  GLOBUS_FTP_CONTROL_MODULE,
                  GLOBUS_NULL,
              _FCSL("globus_l_ftp_control_data_register_connect(): invalid transfer mode."));
        result = globus_error_put(err);
    }

    return result;
}

/**********************************************************************
*  callbacks
**********************************************************************/
/*
 * data connection cleanup
 */
void
globus_l_ftp_io_close_callback(
    void *                                       arg,
    globus_io_handle_t *                         handle,
    globus_result_t                              result)
{
    globus_ftp_data_connection_t *               data_conn;
    globus_ftp_data_stripe_t *                   stripe;
    globus_i_ftp_dc_transfer_handle_t *          transfer_handle;
    globus_i_ftp_dc_handle_t *                   dc_handle;
    globus_l_ftp_data_callback_info_t *          callback_info;
    globus_ftp_control_data_callback_t          eof_callback = GLOBUS_NULL;
    globus_l_ftp_handle_table_entry_t *         eof_cb_ent;
    globus_bool_t                               poll;

    callback_info = (globus_l_ftp_data_callback_info_t *)arg;

    data_conn = callback_info->data_conn;
    stripe = callback_info->stripe;
    transfer_handle = callback_info->transfer_handle;
    dc_handle = callback_info->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);

    globus_mutex_lock(&dc_handle->mutex);
    {
        if(stripe && stripe->eof)
        {
            eof_cb_ent = transfer_handle->eof_cb_ent;
            /* eof ent may not exist */
            if(eof_cb_ent != GLOBUS_NULL &&
               !globus_handle_table_decrement_reference(
               &transfer_handle->handle_table,
               transfer_handle->eof_table_handle))
            {
                eof_callback = eof_cb_ent->callback;
                transfer_handle->eof_cb_ent = GLOBUS_NULL;
            }
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    if(eof_callback != GLOBUS_NULL)
    {
        eof_callback(
            eof_cb_ent->callback_arg,
            dc_handle->whos_my_daddy,
            eof_cb_ent->error,
            eof_cb_ent->buffer,
            eof_cb_ent->length,
            eof_cb_ent->offset,
            GLOBUS_TRUE);
        if(eof_cb_ent->error)
        {
            globus_object_free(eof_cb_ent->error);
        }
        globus_free(eof_cb_ent);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        /*
         *  decrement the reference the callbacks had
         */
        if(eof_callback != GLOBUS_NULL)
        {
            poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    globus_free(callback_info);

    if(data_conn != GLOBUS_NULL)
    {
        /*
         *  we only wait if callback comes from a cancel, not a close
         */
        if(data_conn->free_me)
        {
            globus_free(data_conn);
        }
        else
        {
            data_conn->free_me = GLOBUS_TRUE;
        }
    }

    /*
    This is not needed and introduces a race
    if(poll)
    {
        globus_l_ftp_data_stripe_poll(dc_handle);
    }
    */
}


/******************************************************************
 *  stream mode globus_io callbacks
 *****************************************************************/

void
globus_l_ftp_stream_write_eof_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result)
{
    globus_l_ftp_handle_table_entry_t *         entry;
    globus_object_t *                           error = GLOBUS_NULL;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_ftp_control_handle_t    *            control_handle;
    globus_ftp_data_connection_t *              data_conn;
    globus_byte_t *                             buffer = GLOBUS_NULL;
    void *					big_buffer_cb_arg;
    globus_ftp_control_data_callback_t 		big_buffer_cb = GLOBUS_NULL;
    globus_byte_t *				big_buffer = GLOBUS_NULL;
    globus_bool_t                               poll;

    entry = (globus_l_ftp_handle_table_entry_t *) arg;

    data_conn = entry->whos_my_daddy;
    transfer_handle = data_conn->whos_my_daddy->whos_my_daddy;
    dc_handle = entry->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    control_handle = dc_handle->whos_my_daddy;
    buffer = entry->buffer;

    globus_mutex_lock(&dc_handle->mutex);
    {
        dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF;

        big_buffer_cb_arg = transfer_handle->big_buffer_cb_arg;
        big_buffer_cb = transfer_handle->big_buffer_cb;
        big_buffer = transfer_handle->big_buffer;

        transfer_handle->big_buffer = GLOBUS_NULL;

        if(transfer_handle->big_buffer != GLOBUS_NULL)
        {
            buffer = transfer_handle->big_buffer;
            transfer_handle->big_buffer = GLOBUS_NULL;
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    if(result != GLOBUS_SUCCESS)
    {
        error = globus_error_get(result);
    }

    if(big_buffer_cb != GLOBUS_NULL)
    {
        big_buffer_cb(
            big_buffer_cb_arg,
            control_handle,
            error,
            big_buffer,
            entry->length,
            entry->offset,
            GLOBUS_TRUE);
    }
    else
    {
        entry->callback(
            entry->callback_arg,
            control_handle,
            error,
            buffer,
            entry->length,
            entry->offset,
            GLOBUS_TRUE);
    }
    globus_free(entry);

    globus_mutex_lock(&dc_handle->mutex);
    {
        globus_l_ftp_control_stripes_destroy(dc_handle, GLOBUS_NULL);
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    globus_free(data_conn);

    if(error)
    {
        globus_object_free(error);
    }
}

/*
 *  listen callback
 */
void
globus_l_ftp_stream_listen_callback(
    void *                                      callback_arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result)
{
    globus_ftp_data_stripe_t *                  stripe;
    globus_ftp_data_connection_t *              data_conn;
    globus_object_t *                           error = GLOBUS_NULL;
    globus_l_ftp_data_callback_info_t *         callback_info;
    globus_l_ftp_data_callback_info_t *         cb_info;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_result_t                             res;
    globus_ftp_control_data_connect_callback_t  callback = GLOBUS_NULL;
    void *                                      user_arg;
    unsigned int                                stripe_ndx;
    globus_ftp_control_handle_t    *            control_handle;
    const globus_object_type_t *                type;
    globus_bool_t                               poll;

    callback_info = (globus_l_ftp_data_callback_info_t *)callback_arg;

    data_conn = callback_info->data_conn;
    stripe = callback_info->stripe;
    transfer_handle = callback_info->transfer_handle;
    dc_handle = callback_info->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    control_handle = dc_handle->whos_my_daddy;

    globus_mutex_lock(&dc_handle->mutex);
    {
        globus_assert(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_STREAM);

        /*
         *  result will be not be SUCCESS if the listen failed or
         *  if it was prematurely closed.  ie, the user called force
         *  close before a callback happened
         */
        if(result != GLOBUS_SUCCESS)
        {
            error = globus_error_get(result);
            type = globus_object_get_type(error);

            /*
             *  if not do to canceling the listen, then close
             *  the listener.
             */
            if(!globus_object_type_match(
                   type,
                   GLOBUS_IO_ERROR_TYPE_IO_CANCELLED))
            {
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }

            callback = data_conn->callback;
            user_arg = data_conn->user_arg;
            stripe_ndx = stripe->stripe_ndx;
            globus_free(callback_info);
        }
        /*
         *  if all is well register an accept
         */
        else if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_READ ||
            dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_WRITE)
        {
            data_conn = callback_info->data_conn;
            transfer_handle->ref++;
            globus_list_insert(
                &stripe->outstanding_conn_list,
                (void *)data_conn);
            /*
             *  inc reference count for accept
             *  and for the connection
             */
            transfer_handle->ref++;
            stripe->outstanding_connections++;

            res = globus_io_tcp_register_accept(
                      handle,
                      &dc_handle->io_attr,
                      &data_conn->io_handle,
                      globus_l_ftp_stream_accept_connect_callback,
                      (void *) callback_info);

            if(res != GLOBUS_SUCCESS)
            {
                globus_free(callback_info);
                error = globus_error_get(res);
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }
            else
            {
                stripe->listening = GLOBUS_FALSE;
                CALLBACK_INFO_MALLOC(
                    cb_info,
                    dc_handle,
                    transfer_handle,
                    stripe,
                    GLOBUS_NULL);
                res = globus_io_register_close(
                          handle,
                          globus_l_ftp_io_close_callback,
                          (void *)cb_info);
                if(res != GLOBUS_SUCCESS)
                {
                    res = globus_callback_register_oneshot(
                             GLOBUS_NULL,
                             GLOBUS_NULL,
                             globus_l_ftp_control_io_close_kickout,
                             cb_info);
                    globus_assert(res == GLOBUS_SUCCESS);
                }
            }
        }
        /* this will happen if we got a force_close after the connect_read but
         * before this callback.  just continue with an error */
        else if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING)
        {
            error =  globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("connection closed before accept"));
            callback = data_conn->callback;
            user_arg = data_conn->user_arg;
            stripe_ndx = stripe->stripe_ndx;
            globus_free(callback_info);
        }            

        /*
         *  remove reference for listener callback
         */
        if(error && !dc_handle->connect_error)
        {
            dc_handle->connect_error = globus_object_copy(error);
        }
        
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    /* if there was an error call the connect callback */
    if(callback != GLOBUS_NULL)
    {
        callback(user_arg, control_handle, stripe_ndx, GLOBUS_FALSE, error);
        
        /*
         *  if the user wanted a callback we must dec the reference
         *  it had after we call it.
         */
        globus_mutex_lock(&dc_handle->mutex);
        {
            poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
            if(poll)
            {
                globus_l_ftp_data_stripe_poll(dc_handle);
            }
        }
        globus_mutex_unlock(&dc_handle->mutex);
    }

    if(error)
    {
        globus_free(data_conn);
        globus_object_free(error);
    }
}


/*
 *  globus_l_ftp_stream_accept_connect_callback()
 *  ---------------------------------------------
 *  accept the connection.
 *
 *  references:
 *    entry
 *    -----
 *     1 for the accept callback
 *     1 for the user callback (if there is one)
 *
 *   exit
 *   ----
 *     decrement callback reference (if it exists)
 *     use accept reference as data connection reference
 *          (it will de decremented on close)
 */
void
globus_l_ftp_stream_accept_connect_callback(
    void *                                      callback_arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result)
{
    globus_ftp_data_connection_t *              data_conn;
    globus_ftp_data_stripe_t *                  stripe;
    globus_ftp_control_handle_t    *            control_handle;
    globus_object_t *                           error = GLOBUS_NULL;
    globus_l_ftp_data_callback_info_t *         callback_info;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_ftp_control_data_connect_callback_t  callback = GLOBUS_NULL;
    void *                                      user_arg;
    unsigned int                                stripe_ndx;
    const globus_object_type_t *                type;
    globus_bool_t                               poll;

    callback_info = (globus_l_ftp_data_callback_info_t *)callback_arg;

    dc_handle = callback_info->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);

    globus_mutex_lock(&dc_handle->mutex);
    {
        /* should always be in stream mode here */
        globus_assert(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_STREAM);


        data_conn = callback_info->data_conn;
        stripe = callback_info->stripe;
        transfer_handle = stripe->whos_my_daddy;

        control_handle = dc_handle->whos_my_daddy;
        callback = data_conn->callback;
        user_arg = data_conn->user_arg;
        stripe_ndx = stripe->stripe_ndx;

        /*
         * remove from the outstanding connection list
         */
        stripe->outstanding_connections--;
        globus_list_remove(
            &stripe->outstanding_conn_list,
            globus_list_search(stripe->outstanding_conn_list,(void*)data_conn));

        /*
         *  if an error occurred the accept was either canceled or
         *  failed.
         */
        if(result != GLOBUS_SUCCESS)
        {
            error = globus_error_get(result);
            type = globus_object_get_type(error);

            /*
             *  if not do to canceling the accept, then close
             *  the connection.
             */
            if(!globus_object_type_match(
                   type,
                   GLOBUS_IO_ERROR_TYPE_IO_CANCELLED))
            {
		globus_list_remove_element(
		    &stripe->all_conn_list,
		    data_conn);

                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }
        }
        else if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING)
        {
            error =  globus_error_construct_string(
                         GLOBUS_FTP_CONTROL_MODULE,
                         GLOBUS_NULL,
   _FCSL("connection closed before a data connection request was made"));

            /*
             * since globus io makes no guarantee on order of
             * cancel callbacks we free this structure here.
             */
            if(data_conn->free_me)
            {
                globus_free(data_conn);
            }
            else
            {
                data_conn->free_me = GLOBUS_TRUE;
            }
        }
        /*
         *  if all is well add to the free_conn_q
         */
        else
        {
            globus_assert(dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_READ ||
                   dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_WRITE);

            globus_list_insert(&stripe->all_conn_list, (void*)data_conn);
            stripe->total_connection_count++;
            globus_fifo_enqueue(&stripe->free_conn_q, data_conn);
            stripe->connection_count++;
        }
        
        if(error && !dc_handle->connect_error)
        {
            dc_handle->connect_error = globus_object_copy(error);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    /* call the connect callback */
    if(callback != GLOBUS_NULL)
    {
        callback(user_arg, control_handle, stripe_ndx, GLOBUS_FALSE, error);
    }
    
    if(error)
    {
        globus_object_free(error);
    }
    
    globus_mutex_lock(&dc_handle->mutex);
    {
        if(callback != GLOBUS_NULL)
        {
            poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        }
        /*
         * since connect came back dec ref
         */
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        /*
         *  poll the command_q on all stripes
         */
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    globus_free(callback_info);
}


/*
 *  globus_l_ftp_stream_write_callback()
 *  ------------------------------------
 *
 *  reference:
 *    entry
 *    -----
 *    1 for the user callback
 *    1 for the data connection
 *
 *    exit
 *    ----
 *    decrement the user callback reference
 */
void
globus_l_ftp_stream_write_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbytes)
{
    globus_l_ftp_handle_table_entry_t *         entry;
    globus_object_t *                           error = GLOBUS_NULL;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_ftp_control_handle_t    *            control_handle;
    globus_off_t                                offset;
    globus_ftp_data_connection_t *              data_conn;
    globus_ftp_data_stripe_t *                  stripe;
    globus_bool_t                               eof = GLOBUS_FALSE;
    const globus_object_type_t *                type;
    globus_bool_t                               fire_callback = GLOBUS_TRUE;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_size_t                               nl_nbytes;
    globus_bool_t                               poll;

    nl_nbytes = nbytes;

    entry = (globus_l_ftp_handle_table_entry_t *) arg;

    globus_assert(entry != GLOBUS_NULL);

    dc_handle = entry->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    control_handle = dc_handle->whos_my_daddy;

    globus_mutex_lock(&dc_handle->mutex);
    {
        /* should always be in stream mode here */
        globus_assert(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_STREAM);

        data_conn = entry->whos_my_daddy;
        stripe = data_conn->whos_my_daddy;
        transfer_handle = stripe->whos_my_daddy;

        offset = data_conn->offset;
        data_conn->offset += entry->length;

        /* if an error occurred get it and set EOF to true */
        if(result != GLOBUS_SUCCESS)
        {
            error = globus_error_get(result);
            type = globus_object_get_type(error);
            /*
             *  if not do to canceling the accept, then close
             *  the connection.
             */
            if(!globus_object_type_match(
                   type,
                   GLOBUS_IO_ERROR_TYPE_IO_CANCELLED))
            {
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }
            eof = GLOBUS_TRUE;
        }
        else if(entry->eof)
        {
            entry->offset = offset;

            globus_list_remove_element(
                &stripe->all_conn_list,
                data_conn);
            result = globus_io_register_close(
                         &data_conn->io_handle,
                         globus_l_ftp_stream_write_eof_callback,
                         (void *)entry);

            fire_callback = GLOBUS_FALSE;

            eof = GLOBUS_TRUE;
        }
        else
        {
            globus_fifo_enqueue(&stripe->free_conn_q, data_conn);
        }
        if(dc_handle->nl_ftp_handle_set)
        {
            /* faking memory allocation */
            char  tag_str[128];
            sprintf(tag_str, "MODE=S TYPE=%c NBYTES=%ld",
                    dc_handle->type, (long) nl_nbytes);
            globus_netlogger_write(
                &dc_handle->nl_ftp_handle,
                GFTP_NL_EVENT_SENT_DATA,
                "GFTPC",
                "Important",
                tag_str);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    if(entry->ascii_buffer != GLOBUS_NULL)
    {
        globus_free(entry->ascii_buffer);
    }

    if(fire_callback)
    {
        entry->callback(
            entry->callback_arg,
            control_handle,
            error,
            entry->buffer,
            entry->length,
            offset,
            eof);
        globus_free(entry);

    }
    globus_mutex_lock(&dc_handle->mutex);
    {
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        /*
         *  poll the command_q on all stripes
         */
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    if(error != GLOBUS_NULL)
    {
        globus_object_free(error);
    }
}

/*
 *  globus_l_ftp_stream_read_callback()
 *  -----------------------------------
 *
 *  reference:
 *    entry
 *    -----
 *    1 for the user callback
 *    1 for the data connection
 *
 *    exit
 *    ----
 *    decrement the user callback reference
 */
void
globus_l_ftp_stream_read_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbyte)
{
    globus_l_ftp_handle_table_entry_t *         entry;
    globus_object_t *                           error = GLOBUS_NULL;
    globus_bool_t                               eof = GLOBUS_FALSE;
    globus_ftp_data_connection_t *              data_conn;
    globus_off_t                                offset;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_ftp_control_handle_t    *            control_handle;
    globus_ftp_data_stripe_t *                  stripe;
    globus_byte_t *                             buffer = GLOBUS_NULL;
    const globus_object_type_t *                type;
    globus_result_t                             res;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_bool_t                               fire_callback = GLOBUS_TRUE;
    globus_bool_t                               poll;
    globus_size_t                               nl_nbytes;

    nl_nbytes = nbyte;
    entry = (globus_l_ftp_handle_table_entry_t *) arg;

    dc_handle = entry->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);

    if(entry->type == GLOBUS_FTP_CONTROL_TYPE_ASCII)
    {
        nbyte = globus_l_ftp_control_strip_ascii(buf, nbyte);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        globus_assert(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_STREAM);

        data_conn = entry->whos_my_daddy;
        stripe = data_conn->whos_my_daddy;
        transfer_handle = stripe->whos_my_daddy;
        control_handle = dc_handle->whos_my_daddy;

        buffer = entry->buffer;

        /*
         *  result will not be SUCCESS when the callback is canceled
         *  or an error occurs.
         */
        if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING)
        {
        }
        else if(result != GLOBUS_SUCCESS)
        {
            error = globus_error_get(result);
            type = globus_object_get_type(error);

            /* if it is EOF do not pass the user back an error */
            if(globus_io_eof(error))
            {
                globus_object_free(error);
                result = GLOBUS_SUCCESS;
                error = GLOBUS_NULL;
                eof = GLOBUS_TRUE;

                if(transfer_handle->big_buffer != GLOBUS_NULL)
                {
                    buffer = transfer_handle->big_buffer;

                    if(nbyte + data_conn->offset <
                           transfer_handle->big_buffer_length)
                    {
                        error = globus_error_construct_string(
                                  GLOBUS_FTP_CONTROL_MODULE,
                                  GLOBUS_NULL,
                    _FCSL("Buffer given to read_all was not completely filled."));
                    }
                }

                fire_callback = GLOBUS_FALSE;

                globus_list_remove_element(
                    &stripe->all_conn_list,
                    data_conn);
                result = globus_io_register_close(
                         &data_conn->io_handle,
                         globus_l_ftp_stream_write_eof_callback,
                         (void *)entry);
                globus_assert(result == GLOBUS_SUCCESS);
                entry->length = nbyte;
                entry->offset = data_conn->offset;
            }
            else if(!globus_object_type_match(
                     type,
                     GLOBUS_IO_ERROR_TYPE_IO_CANCELLED))
            {
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }

            eof = GLOBUS_TRUE;
        }
        else
        {
            if(transfer_handle->big_buffer != GLOBUS_NULL)
            {
                buffer = transfer_handle->big_buffer;
                if(data_conn->offset + nbyte > transfer_handle->big_buffer_length)
                {
                    error = globus_error_construct_string(
                              GLOBUS_FTP_CONTROL_MODULE,
                              GLOBUS_NULL,
         _FCSL("Buffer given to read_all is not large enough to hold data sent."));

                    eof = GLOBUS_TRUE;
                    nbyte = 0;
                    globus_l_ftp_control_stripes_destroy(dc_handle, error);
                }
                else
                {
                    globus_fifo_enqueue(&stripe->free_conn_q, data_conn);
                    /*
                     * register a read of 1 byte over to prompt EOF
                     */
                    res = globus_l_ftp_control_data_stream_read_write(
                              dc_handle,
                              transfer_handle->big_buffer_byte,
                              1,
                              data_conn->offset + nbyte,
                              GLOBUS_FALSE,
                              transfer_handle->big_buffer_cb,
                              transfer_handle->big_buffer_cb_arg);
                    globus_assert(res == GLOBUS_SUCCESS);
                }
            }
            else
            {
                globus_fifo_enqueue(&stripe->free_conn_q, data_conn);
            }
            if(dc_handle->nl_ftp_handle_set)
            {
                char tag_str[128];
                sprintf(tag_str, "MODE=S TYPE=%c NBYTES=%ld",
                        dc_handle->type, (long) nl_nbytes);
                globus_netlogger_write(
                    &dc_handle->nl_ftp_handle,
                    GFTP_NL_EVENT_RECEIVED_DATA,
                    "GFTPC",
                    "Important",
                    tag_str);
            }
        }

        offset = data_conn->offset;
        data_conn->offset += nbyte;
    }
    globus_mutex_unlock(&dc_handle->mutex);

    if(entry->callback && fire_callback)
    {
        entry->callback(
            entry->callback_arg,
            control_handle,
            error,
            buffer,
            nbyte,
            offset,
            eof);
        globus_free(entry);
    }


    globus_mutex_lock(&dc_handle->mutex);
    {
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        /*
         *  poll the command_q on all stripes
         */
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    if(error != GLOBUS_NULL)
    {
        globus_object_free(error);
    }
}

/******************************************************************
 *  extended block mode globus_io callbacks
 *****************************************************************/
/*
 *  listen callback
 *
 *  called when a connection request is made, or when the listener
 *  is closed
 *
 *  reregister the listener unless CLOSING or an error occurred
 *
 *  QUESTION:
 *  do we want the connect callback called every time there is a
 *  connection made or only the first time.  If it is every time
 *  we need to not set the callback arg of the reregistered data
 *  conn to GLOBUS_NULL and find a way to deal with the CLOSING state
 */
void
globus_l_ftp_eb_listen_callback(
    void *                                      callback_arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result)
{
    globus_ftp_data_stripe_t *                  stripe;
    globus_ftp_data_connection_t *              data_conn;
    globus_ftp_data_connection_t *              data_conn2;
    globus_object_t *                           error = GLOBUS_NULL;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_result_t                             res;
    globus_ftp_control_data_connect_callback_t  callback = GLOBUS_NULL;
    void *                                      user_arg;
    unsigned int                                stripe_ndx;
    globus_ftp_control_handle_t    *            control_handle;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_bool_t                               poll;

    data_conn = (globus_ftp_data_connection_t *)callback_arg;
    stripe = data_conn->whos_my_daddy;
    transfer_handle = stripe->whos_my_daddy;
    dc_handle = transfer_handle->whos_my_daddy;
    GlobusFTPControlDataTestMagic(dc_handle);
    control_handle = transfer_handle->control_handle;

    globus_mutex_lock(&dc_handle->mutex);
    {
        /*
         *  if in closing state the result should say the callback
         *  was canceled via a call to close so we do not need to close it
         */

        if(result != GLOBUS_SUCCESS)
        {
            const globus_object_type_t *               type;

            type = globus_object_get_type(globus_error_peek(result));

            if(globus_object_type_match(
                   type,
                   GLOBUS_IO_ERROR_TYPE_IO_CANCELLED))
            {
                error =  globus_error_construct_string(
                             GLOBUS_FTP_CONTROL_MODULE,
                             GLOBUS_NULL,
   _FCSL("connection closed before a data connection request was made"));
            }
            else
            {
                error = globus_error_get(result);
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }
            callback = data_conn->callback;
            user_arg = data_conn->user_arg;
            stripe_ndx = stripe->stripe_ndx;

            /*
             *  TODO: ?: if a big buffer is registered kick out its callback
             */
        }
        /*
         *  if all is well, do not call the callback (it will be called
         *  once the connection is accepted), register an accept, and
         *  re-register the listener.
         */
        else if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_READ)
        {
            /*
             *  inc reference count for accept
             */
            transfer_handle->ref++;

            /*
             *  inc count for data_conn
             */
            stripe->total_connection_count++;
            transfer_handle->ref++;
            stripe->outstanding_connections++;
            globus_list_insert(
                &stripe->outstanding_conn_list,
                (void *)data_conn);

            res = globus_io_tcp_register_accept(
                      handle,
                      &dc_handle->io_attr,
                      &data_conn->io_handle,
                      globus_l_ftp_eb_accept_callback,
                      (void *)data_conn);

            if(res != GLOBUS_SUCCESS)
            {
                error = globus_error_get(res);
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }
            else
            {
               /*
                *  re-register the listen
                *  see QUESTION above about connection callbacks
                */
                transfer_handle->ref++;
                DATA_CONN_MALLOC(data_conn2, stripe, GLOBUS_NULL, GLOBUS_NULL);
                res = globus_io_tcp_register_listen(
                          &stripe->listener_handle,
                          globus_l_ftp_eb_listen_callback,
                          (void *)data_conn2);
                if(res != GLOBUS_SUCCESS)
                {
                    error = globus_error_get(res);
                    globus_l_ftp_control_stripes_destroy(dc_handle, error);
                }
            }
        }
        /* this will happen if we got a force_close after the connect_read but
         * before this callback.  Just continue with an error */
        else if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING)
        {
            error =  globus_error_construct_string(
                GLOBUS_FTP_CONTROL_MODULE,
                GLOBUS_NULL,
                _FCSL("connection closed before accept"));
            callback = data_conn->callback;
            user_arg = data_conn->user_arg;
            stripe_ndx = stripe->stripe_ndx;
        }            
        /*
         *  remove reference for listener callback
         */
        
        if(error && !dc_handle->connect_error)
        {
            dc_handle->connect_error = globus_object_copy(error);
        }
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    /*
     *  if there was an error call the connect callback and decrement
     *  the reference count that the callback had.
     */
    if(callback != GLOBUS_NULL)
    {
        callback(user_arg, control_handle, stripe_ndx, GLOBUS_FALSE, error);

        /*
         *  if the user wanted a callback we must dec the reference
         *  it had after we call it.
         */
        globus_mutex_lock(&dc_handle->mutex);
        {
            poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
            if(poll)
            {
                globus_l_ftp_data_stripe_poll(dc_handle);
            }
        }
        globus_mutex_unlock(&dc_handle->mutex);
    }

    if(error !=GLOBUS_NULL)
    {
        globus_free(data_conn);
        globus_object_free(error);
    }
}

/*
 *  globus_l_ftp_eb_accept_callback()
 *  ---------------------------------
 */
void
globus_l_ftp_eb_accept_callback(
    void *                                      callback_arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result)
{
    globus_ftp_data_connection_t *              data_conn;
    globus_ftp_data_stripe_t *                  stripe;
    globus_ftp_control_handle_t    *            control_handle;
    globus_object_t *                           error = GLOBUS_NULL;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_ftp_control_data_connect_callback_t  callback = GLOBUS_NULL;
    void *                                      user_arg;
    unsigned int                                stripe_ndx;
    globus_result_t                             res;
    globus_l_ftp_eb_header_t *                  eb_header;
    const globus_object_type_t *                type;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_bool_t                               poll;

    data_conn = (globus_ftp_data_connection_t *) callback_arg;
    stripe = data_conn->whos_my_daddy;
    transfer_handle = stripe->whos_my_daddy;
    dc_handle = transfer_handle->whos_my_daddy;
    GlobusFTPControlDataTestMagic(dc_handle);
    control_handle = dc_handle->whos_my_daddy;

    globus_mutex_lock(&dc_handle->mutex);
    {
        globus_assert(
            dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK);

        callback = data_conn->callback;
        user_arg = data_conn->user_arg;
        stripe_ndx = stripe->stripe_ndx;

        stripe->outstanding_connections--;
        globus_list_remove(
            &stripe->outstanding_conn_list,
            globus_list_search(stripe->outstanding_conn_list, data_conn));
        /*
         *  The result may be != SUCCESS for 1 of 2 reasons.
         *     connection was closed resulting in a cancel callback
         *        call the user callback with an error
         *     accept failed
         *        tear down the stripe and call the user callback
         *        with an error.
         */
        if(result != GLOBUS_SUCCESS)
        {
            error = globus_error_get(result);
            type = globus_object_get_type(error);

            if(!globus_object_type_match(
                type,
                GLOBUS_IO_ERROR_TYPE_IO_CANCELLED))
            {
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }

            /*
             *  TODO: ?: if a big buffer is registered kickout its callback
             */
        }
        /*
         *  This *MUST* only happen if the callback occurs via a
         *  call to globus_io_register_cancel()
         */
        else if(
            dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING)
        {
            error =  globus_error_construct_string(
                         GLOBUS_FTP_CONTROL_MODULE,
                         GLOBUS_NULL,
   _FCSL("connection closed before a data connection request was made"));

            /*
             * since globus io makes no guarantee on order of
             * cancel callbacks we free this structure here.
             */
            if(data_conn->free_me)
            {
                globus_free(data_conn);
            }
            else
            {
                data_conn->free_me = GLOBUS_TRUE;
            }
        }
        /*
         *  if all is well register a read of the first header
         */
        else
        {
            /*
             *  ok to increment connection_count because register_eod will
             *  decrement it
             */
            stripe->connection_count++;
            globus_list_insert(&stripe->all_conn_list, data_conn);

            globus_assert(
                dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_READ ||
                dc_handle->state == GLOBUS_FTP_DATA_STATE_EOF);

            eb_header = (globus_l_ftp_eb_header_t *)
                          globus_malloc(sizeof(globus_l_ftp_eb_header_t));

            /* count active connections and total connections */
            data_conn->bytes_ready = 0;

            res = globus_io_register_read(
                      &data_conn->io_handle,
                      (globus_byte_t *)eb_header,
                      sizeof(globus_l_ftp_eb_header_t),
                      sizeof(globus_l_ftp_eb_header_t),
                      globus_l_ftp_eb_read_header_callback,
                      (void *)data_conn);
            if(res != GLOBUS_SUCCESS)
            {
                error = globus_error_get(res);
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }
        }
        /*
         *  since the accept came back, dec ref
         */
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);


    /*
     *  notify the user that a connection has been established,
     *  then decrement the reference that the callback had.
     */
    if(callback != GLOBUS_NULL)
    {
        callback(user_arg, control_handle, stripe_ndx, GLOBUS_FALSE, error);

        /*
         *  lock and decrement the reference the callback had
         */
        globus_mutex_lock(&dc_handle->mutex);
        {
            poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
            if(poll)
            {
                globus_l_ftp_data_stripe_poll(dc_handle);
            }
        }
        globus_mutex_unlock(&dc_handle->mutex);
    }
    
    if(error)
    {
        globus_object_free(error);
    }
}

/*
 *  globus_l_ftp_eb_read_header_callback()
 *  --------------------------------------
 */
void
globus_l_ftp_eb_read_header_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbyte)
{
    globus_ftp_data_connection_t *              data_conn;
    globus_ftp_data_stripe_t *                  stripe;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_l_ftp_eb_header_t *                  eb_header;
    globus_l_ftp_eb_header_t *                  eb_header2;
    globus_l_ftp_data_callback_info_t *         cb_info;
    globus_object_t *                           error = GLOBUS_NULL;
    globus_result_t                             res;
    const globus_object_type_t *                type;
    globus_off_t                                offset;
    globus_off_t                                tmp;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_bool_t                               eod = GLOBUS_FALSE;

    data_conn = (globus_ftp_data_connection_t *)arg;
    stripe = data_conn->whos_my_daddy;
    transfer_handle = stripe->whos_my_daddy;
    dc_handle = transfer_handle->whos_my_daddy;
    GlobusFTPControlDataTestMagic(dc_handle);
    eb_header = (globus_l_ftp_eb_header_t *)buf;

    globus_mutex_lock(&dc_handle->mutex);
    {
        /*
         *  If the result is not success we either canceled it
         *  or the connection broke.  If the connection broke tear
         *  down all connections.
         */
        if(result != GLOBUS_SUCCESS)
        {
            error = globus_error_get(result);
            type = globus_object_get_type(error);

            if(!globus_object_type_match(
                type,
                GLOBUS_IO_ERROR_TYPE_IO_CANCELLED))
            {
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }
        }
        else
        {
            if(eb_header->descriptor & GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_EOD)
    	    {
                /* set connection state eod and local state eod */
                data_conn->eod = GLOBUS_TRUE;
                eod = GLOBUS_TRUE;
            }
            if(eb_header->descriptor & GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_CLOSE)
            {
                data_conn->close = GLOBUS_TRUE;
            }

            /*
             *  if EOF get the data connection close count
             */
            if(eb_header->descriptor & GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_EOF)
            {
                data_conn->offset = 0;
                data_conn->bytes_ready = 0;
                globus_l_ftp_control_data_decode(
                    eb_header->offset,
                    &tmp);
                stripe->eod_count = tmp;
            }
            else
            {
                globus_l_ftp_control_data_decode(
                    eb_header->count,
                    &tmp);
                data_conn->bytes_ready = tmp;
                globus_l_ftp_control_data_decode(
                    eb_header->offset,
                    &data_conn->offset);
            }

            /*
             *  message without payload
             */
            if(data_conn->bytes_ready == 0)
            {
                /*
                 *  if end of data and close, close the connection.
                 */
                if(data_conn->close)
                {
                    /*
                     *  next assertion happens if the writer breaks
                     *  the protocol and sends a CLOSE prior to
                     *  an EOD.  
                     *
                     *  TODO: stop transfer and return error to user 
                     */
                    globus_assert(data_conn->reusing || data_conn->eod);

                    /* 
                     * if local eod state is true it means we have not
                     * already preformed the following needed steps so
                     * do them now.
                     */
                    if(eod)
                    {
                        stripe->eods_received++;
                        stripe->connection_count--;
                    }
                    /* 
                     * if not local eod but conn state eod, it means that
                     * that we have added the connection to the free_cache
                     * list and must remove it
                     */
                    else if(data_conn->eod)
                    {
                        globus_list_remove(
                            &stripe->free_cache_list,
                            globus_list_search(
                                    stripe->free_cache_list, data_conn));
                    }

                    /* 
                     *  remove from all list before closing
                     */
                    globus_list_remove_element(
                        &stripe->all_conn_list,
                        (void *)data_conn);

                    CALLBACK_INFO_MALLOC(
                        cb_info,
                        dc_handle,
                        transfer_handle,
                        stripe,
                        data_conn);
                    res = globus_io_register_close(
                              &data_conn->io_handle,
                              globus_l_ftp_io_close_callback,
                              (void *)cb_info);
                    if(res != GLOBUS_SUCCESS)
                    {
                        res = globus_callback_register_oneshot(
                                 GLOBUS_NULL,
                                 GLOBUS_NULL,
                                 globus_l_ftp_control_io_close_kickout,
                                 cb_info);
                        globus_assert(res == GLOBUS_SUCCESS);
                    }
                }
                /*
                 *  if we got EOD without a close message
                 *  cache the current connection
                 */
                else if(data_conn->eod)
                {
                    stripe->eods_received++;
                    stripe->connection_count--;

                    globus_list_insert(
                        &stripe->free_cache_list,
                        (void*) data_conn);
                }
                /*
                 *  we end up in this part of the branch if we got an EOF
                 *  without an EOD or if the sender sent a header with
                 *  length equal to zero (which they shouldn't do).
                 */
                else
                {
                    eb_header2 = (globus_l_ftp_eb_header_t *)globus_malloc(
                                     sizeof(globus_l_ftp_eb_header_t));

                    res = globus_io_register_read(
                              &data_conn->io_handle,
                              (globus_byte_t*)eb_header2,
                              sizeof(globus_l_ftp_eb_header_t),
                              sizeof(globus_l_ftp_eb_header_t),
                              globus_l_ftp_eb_read_header_callback,
                              (void *)data_conn);
                    globus_assert(res == GLOBUS_SUCCESS);
                }
            }
            else
            {
                /*
                 *  if not a big buffer read place connection in
                 *  free connection list
                 */
                if(transfer_handle->big_buffer == GLOBUS_NULL)
                {
                    globus_fifo_enqueue(
                        &stripe->free_conn_q,
                        (void *)data_conn);
                }
                /*
                 *  if it is a big buffer read, read directly into the
                 *  buffer.
                 */
                else
                {
                    globus_off_t end_offset;
            	    globus_off_t end_buffer;

            	    end_offset = ((globus_off_t) data_conn->bytes_ready) +
             	        data_conn->offset;
                    end_buffer = ((globus_off_t)
                                        transfer_handle->big_buffer_length);

                    /*
                     *  if the sender sent more bytes than the users
                     *  buffer can handle
                     */
                    if(end_offset > end_buffer)
                    {
                        error =  globus_error_construct_string(
                                     GLOBUS_FTP_CONTROL_MODULE,
                                     GLOBUS_NULL,
				     _FCSL("too much data has been sent."));
                        globus_l_ftp_control_stripes_destroy(dc_handle, error);
                    }
                    else
                    {
                        globus_l_ftp_handle_table_entry_t *    t_e;

                        offset = data_conn->offset;
                        transfer_handle->ref++;
                        TABLE_ENTRY_MALLOC(
                            t_e,
                            &transfer_handle->big_buffer[data_conn->offset],
                            data_conn->bytes_ready,
                            data_conn->offset,
                            GLOBUS_FALSE,
                            transfer_handle->big_buffer_cb,
                            transfer_handle->big_buffer_cb_arg,
                            dc_handle);
                        t_e->whos_my_daddy = data_conn;

                        /*
                         *  register a read into the users buffer at the
                         *  correct offset.
                         */
                        res = globus_io_register_read(
                                  &data_conn->io_handle,
                                  &transfer_handle->big_buffer[offset],
                                  data_conn->bytes_ready,
                                  data_conn->bytes_ready,
                                  globus_l_ftp_eb_read_callback,
                                  (void *)t_e);
                        globus_assert(res == GLOBUS_SUCCESS);
                    }
                }
            }
            data_conn->reusing = GLOBUS_FALSE;
        }
        globus_l_ftp_data_stripe_poll(dc_handle);
    }
    globus_mutex_unlock(&dc_handle->mutex);

    globus_free(eb_header);

    
    if(error != GLOBUS_NULL)
    {
        globus_object_free(error);
    }
}

void
globus_l_ftp_data_order_read_cb(
    void *                              ent)
{
    globus_l_ftp_handle_table_entry_t *         entry;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_bool_t                               done = GLOBUS_FALSE;
    int                                         rc;
        
    entry = (globus_l_ftp_handle_table_entry_t *) ent;
    transfer_handle = entry->transfer_handle;
    dc_handle = entry->dc_handle;
    globus_mutex_lock(&transfer_handle->order_mutex);
    {
        rc = globus_priority_q_enqueue(
            &transfer_handle->ordered_buffer_q,
            (void *)entry,
            (void *)entry);
        globus_assert(rc == GLOBUS_SUCCESS);
    
        while(!done && 
            !globus_priority_q_empty(&transfer_handle->ordered_buffer_q))
        {
            entry = (globus_l_ftp_handle_table_entry_t *) 
                globus_priority_q_first(
                    &transfer_handle->ordered_buffer_q);
            globus_i_ftp_control_debug_printf(9,
                (stderr, "bw: %"GLOBUS_OFF_T_FORMAT", bh: %"GLOBUS_OFF_T_FORMAT"\n", 
                    transfer_handle->order_next_offset_passed,
                    entry->offset));
            
            if(transfer_handle->order_next_offset_passed == entry->offset)
            {
                entry = (globus_l_ftp_handle_table_entry_t *)
                    globus_priority_q_dequeue(
                        &transfer_handle->ordered_buffer_q);
                assert(entry != NULL);
                transfer_handle->order_next_offset_passed += entry->length;
                if(entry->callback)
                {
                    entry->callback(
                        entry->callback_arg,
                        entry->dc_handle->whos_my_daddy,
                        entry->error,
                        entry->buffer,
                        entry->length,
                        entry->offset,
                        entry->eof);
                }
                globus_free(entry);
            }
            else
            {
                done = GLOBUS_TRUE;
            }
        }
    }
    globus_mutex_unlock(&transfer_handle->order_mutex);

    globus_mutex_lock(&dc_handle->mutex);
    {
        globus_l_ftp_control_dc_dec_ref(transfer_handle);
    }
    globus_mutex_unlock(&dc_handle->mutex);
}



void
globus_l_ftp_eb_read_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbyte)
{
    globus_l_ftp_handle_table_entry_t *         entry;
    globus_ftp_data_connection_t *              data_conn;
    globus_ftp_data_stripe_t *                  stripe;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_l_ftp_eb_header_t *                  eb_header;
    globus_l_ftp_data_callback_info_t *         cb_info;
    globus_ftp_control_handle_t    *            control_handle;
    globus_object_t *                           error = GLOBUS_NULL;
    globus_off_t                                offset = 0;
    globus_bool_t                               eof = GLOBUS_FALSE;
    globus_result_t                             res;
    globus_byte_t *                             buffer = GLOBUS_NULL;
    const globus_object_type_t *                type;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_size_t                               nl_bytes;
    globus_bool_t                               poll;
    globus_bool_t                               free_entry = GLOBUS_TRUE;

    nl_bytes = nbyte;
    entry = (globus_l_ftp_handle_table_entry_t *)arg;

    dc_handle = entry->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    data_conn = entry->whos_my_daddy;
    stripe = data_conn->whos_my_daddy;
    transfer_handle = stripe->whos_my_daddy;
    control_handle = dc_handle->whos_my_daddy;
        globus_i_ftp_control_debug_printf(9,
            (stderr, "readcb: %"GLOBUS_OFF_T_FORMAT", %p\n", 
                data_conn->offset,
                (void *) data_conn));

    globus_mutex_lock(&dc_handle->mutex);
    {
        buffer = entry->buffer;
        if(transfer_handle->big_buffer != GLOBUS_NULL)
        {
            buffer = transfer_handle->big_buffer;
        }

        if(result != GLOBUS_SUCCESS)
        {
            error = globus_error_get(result);
            eof = GLOBUS_TRUE;
            type = globus_object_get_type(error);

            if(!globus_object_type_match(
                type,
                GLOBUS_IO_ERROR_TYPE_IO_CANCELLED))
            {
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }
        }
        else if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING)
        {
            eof = GLOBUS_TRUE;
        }
        else
        {
            /*
             *  add the number of bytes read to data_conn->offset and
             *  subtract from bytes_ready
             */
            offset = data_conn->offset;
            data_conn->offset += nbyte;
            data_conn->bytes_ready -= nbyte;

            if(entry->type == GLOBUS_FTP_CONTROL_TYPE_ASCII)
            {
                nbyte = globus_l_ftp_control_strip_ascii(
                            entry->buffer, nbyte);
            }

            if(data_conn->bytes_ready == 0)
            {
                /*
                 *  if eod and close, close the connection
                 */
                if(data_conn->eod)
                {
                    stripe->eods_received++;
                    if(stripe->eod_count == stripe->eods_received)
                    {
                        eof = GLOBUS_TRUE;
                        transfer_handle->big_buffer = GLOBUS_NULL;
                    }
                    stripe->connection_count--;

                    /*
                     *  if we are closing the connection
                     */
                    if(data_conn->close)
                    {
                        globus_list_remove_element(
                            &stripe->all_conn_list,
                            (void *)data_conn);

                        CALLBACK_INFO_MALLOC(
                            cb_info,
                            dc_handle,
                            transfer_handle,
                            stripe,
                            data_conn);
                        res = globus_io_register_close(
                                  &data_conn->io_handle,
                                  globus_l_ftp_io_close_callback,
                                  (void *)cb_info);
                        if(res != GLOBUS_SUCCESS)
                        {
                            res = globus_callback_register_oneshot(
                                     GLOBUS_NULL,
                                     GLOBUS_NULL,
                                     globus_l_ftp_control_io_close_kickout,
                                     cb_info);
                            globus_assert(res == GLOBUS_SUCCESS);
                        }
                    }
                    /*
                     *  if we are caching the data connection
                     */
                    else
                    {
                        globus_list_insert(
                            &stripe->free_cache_list,
                            (void*) data_conn);
                    }
                }
                /* register the next header read */
                else
                {
                    eb_header = (globus_l_ftp_eb_header_t *)
                                 globus_malloc(
                                     sizeof(globus_l_ftp_eb_header_t));

                    res = globus_io_register_read(
                                 &data_conn->io_handle,
                                 (globus_byte_t *)eb_header,
                                 sizeof(globus_l_ftp_eb_header_t),
                                 sizeof(globus_l_ftp_eb_header_t),
                                 globus_l_ftp_eb_read_header_callback,
                                 (void *)data_conn);
                    if(res != GLOBUS_SUCCESS)
                    {
                        error = globus_error_get(result);
                        eof = GLOBUS_TRUE;
                    }
                }
            }
            else
            {
                globus_fifo_enqueue(&stripe->free_conn_q, (void *)data_conn);
            }
            if(dc_handle->nl_ftp_handle_set)
            {
                char tag_str[128];
                sprintf(tag_str, "MODE=E TYPE=%c NBYTES=%ld",
                        dc_handle->type, (long) nl_bytes);
                globus_netlogger_write(
                    &dc_handle->nl_ftp_handle,
                    GFTP_NL_EVENT_RECEIVED_DATA,
                    "GFTPC",
                    "Important",
                    tag_str);
            }
        }
        if(entry->ascii_buffer != GLOBUS_NULL)
        {
            globus_free(entry->ascii_buffer);
        }
    
        if(transfer_handle->order_data)
        {
            free_entry = GLOBUS_FALSE;
            entry->offset = offset;
            entry->length = nbyte;
            entry->buffer = buffer;
            entry->error = error;
            entry->eof = eof;
            transfer_handle->ref++;
            res = globus_callback_register_oneshot(
                GLOBUS_NULL,
                GLOBUS_NULL,
                globus_l_ftp_data_order_read_cb,
                entry);
            globus_assert(res == GLOBUS_SUCCESS);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);
    
    if(free_entry)
    {
        if(entry->callback != GLOBUS_NULL)
        {
            entry->callback(
                entry->callback_arg,
                control_handle,
                error,
                buffer,
                nbyte,
                offset,
                eof);
        }
        globus_free(entry);
    }
    globus_mutex_lock(&dc_handle->mutex);
    {
        if(eof && !error)
        {
            dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF;
        }
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        /*
         *  poll the command_q on all stripes
         */
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);
    
    if(error != GLOBUS_NULL)
    {
        globus_object_free(error);
    }
}

/*
 *  globus_l_ftp_eb_connect_callback()
 *  ----------------------------------
 */
void
globus_l_ftp_eb_connect_callback(
    void *                                      callback_arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result)
{
    globus_ftp_data_connection_t *              data_conn;
    globus_ftp_data_stripe_t *                  stripe;
    globus_ftp_control_handle_t    *            control_handle;
    globus_object_t *                           error = GLOBUS_NULL;
    globus_l_ftp_data_callback_info_t *         callback_info;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_result_t                             res;
    globus_ftp_control_data_connect_callback_t  callback = GLOBUS_NULL;
    void *                                      user_arg;
    unsigned int                                stripe_ndx;
    const globus_object_type_t *                type;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_ftp_control_data_callback_t          eof_callback = GLOBUS_NULL;
    globus_l_ftp_handle_table_entry_t *         eof_cb_ent;
    globus_bool_t                               poll = GLOBUS_TRUE;

    callback_info = (globus_l_ftp_data_callback_info_t *)callback_arg;

    dc_handle = callback_info->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    data_conn = callback_info->data_conn;
    stripe = callback_info->stripe;
    transfer_handle = stripe->whos_my_daddy;

    globus_mutex_lock(&dc_handle->mutex);
    {
        globus_assert(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK);
        callback = data_conn->callback;
        control_handle = dc_handle->whos_my_daddy;
        user_arg = data_conn->user_arg;
        stripe_ndx = stripe->stripe_ndx;

        stripe->outstanding_connections--;
        globus_list_remove(
            &stripe->outstanding_conn_list,
            globus_list_search(stripe->outstanding_conn_list, data_conn));
        /*
         *  if closing at this point we call the user callback with
         *  an error indicating that the connection ack was never read
         */
        if(result != GLOBUS_SUCCESS)
        {
            /*
             *  if we are closing do to an error tear down all of the stripes
             *  and pass the user the error pointed to by result
             */
            error = globus_error_get(result);
            type = globus_object_get_type(error);

            if(!globus_object_type_match(
                type,
                GLOBUS_IO_ERROR_TYPE_IO_CANCELLED))
            {
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }
            /*
             *  if the user requested the close the stripes had
             *  already been registered for tear down.
             */
            else
            {
                if(error)
                {
                    globus_object_free(error);
                }
                error = globus_error_construct_string(
                       GLOBUS_FTP_CONTROL_MODULE,
                       GLOBUS_NULL,
                       _FCSL("closed before connection could be established"));
            }

            /*
             *  if eof has already be registered we must kick out the
             *  eof write callback with an error
             */
            if(stripe->eof)
            {
                /*
                 *  get the eof entry structure and decrement its reference.
                 *  If it is the final reference call the uses callback.
                 */
                eof_cb_ent = globus_handle_table_lookup(
                                 &transfer_handle->handle_table,
                                 transfer_handle->eof_table_handle);
                if(eof_cb_ent != GLOBUS_NULL)
                {
                    if(!globus_handle_table_decrement_reference(
                       &transfer_handle->handle_table,
                       transfer_handle->eof_table_handle))
                    {
                        eof_callback = eof_cb_ent->callback;
                        transfer_handle->eof_cb_ent = GLOBUS_NULL;
                    }
                }
            }
        }
        else if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING)
        {
            error =  globus_error_construct_string(
                         GLOBUS_FTP_CONTROL_MODULE,
                         GLOBUS_NULL,
   _FCSL("connection closed before a data connection request was made"));

            /*
             * since globus io makes no guarantee on order of
             * cancel callbacks we free this structure here.
             */
            if(data_conn->free_me)
            {
                globus_free(data_conn);
            }
            else
            {
                data_conn->free_me = GLOBUS_TRUE;
            }
        }
        /*
         *  if we connected successfully
         */
        else
        {
            globus_assert(
                dc_handle->state == GLOBUS_FTP_DATA_STATE_CONNECT_WRITE ||
                dc_handle->state == GLOBUS_FTP_DATA_STATE_SEND_EOF);

            stripe->connection_count++;
            globus_list_insert(&stripe->all_conn_list, data_conn);
            /*
             *  it is possible that we had an outstanding connection
             *  when we sent the EOF message, therefore if the stripe
             *  has been set to eof send an eod message
             */
            if(stripe->eof)
            {
                res = globus_l_ftp_control_data_register_eod(
                          stripe,
                          data_conn);
                globus_assert(res == GLOBUS_SUCCESS);
            }
            else
            {
                globus_fifo_enqueue(&stripe->free_conn_q, data_conn);
            }
        }
        
        if(error && !dc_handle->connect_error)
        {
            dc_handle->connect_error = globus_object_copy(error);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    if(callback != GLOBUS_NULL)
    {
        callback(user_arg, control_handle, stripe_ndx, GLOBUS_FALSE, error);
    }

    /* this should only happen when there is an error */
    if(eof_callback != GLOBUS_NULL && eof_cb_ent != GLOBUS_NULL)
    {
        eof_callback(
            eof_cb_ent->callback_arg,
            control_handle,
            error,
            eof_cb_ent->buffer,
            eof_cb_ent->length,
            eof_cb_ent->offset,
            GLOBUS_TRUE);
        globus_free(eof_cb_ent);
    }
    
    if(error)
    {
        globus_object_free(error);
    }
    
    globus_mutex_lock(&dc_handle->mutex);
    {
        /*
         * since connect came back dec ref
         */
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        globus_assert(poll || (callback == NULL && eof_callback == NULL));

        /*
         *  decrement the reference the callbacks had
         */
        if(eof_callback != GLOBUS_NULL)
        {
            poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        }

        if(callback != GLOBUS_NULL)
        {
            poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        }
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    globus_free(callback_info);
}

/*
 *
 */
void
globus_l_ftp_eb_write_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    struct iovec *                              iov,
    globus_size_t                               iovcnt,
    globus_size_t                               nbytes)
{
    globus_l_ftp_handle_table_entry_t *         entry;
    globus_l_ftp_handle_table_entry_t *         eof_cb_ent;
    globus_l_ftp_handle_table_entry_t *         cb_ent;
    globus_ftp_data_connection_t *              data_conn;
    globus_ftp_data_stripe_t *                  stripe;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_ftp_control_data_callback_t          callback = GLOBUS_NULL;
    globus_ftp_control_data_callback_t          eof_callback = GLOBUS_NULL;
    globus_ftp_control_handle_t    *            control_handle;
    globus_object_t *                           error = GLOBUS_NULL;
    globus_l_ftp_eb_header_t *                  eb_header;
    globus_result_t                             res;
    const globus_object_type_t *                type;
    globus_bool_t                               eof = GLOBUS_FALSE;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_l_ftp_send_eof_entry_t *             send_eof_ent = GLOBUS_NULL;
    globus_size_t                               nl_bytes;
    globus_bool_t                               poll = GLOBUS_TRUE;

    nl_bytes = nbytes;

    entry = (globus_l_ftp_handle_table_entry_t *)arg;
    eb_header = (globus_l_ftp_eb_header_t *)iov[0].iov_base;

    dc_handle = entry->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    data_conn = entry->whos_my_daddy;
    stripe = data_conn->whos_my_daddy;
    transfer_handle = stripe->whos_my_daddy;
    control_handle = dc_handle->whos_my_daddy;

    globus_mutex_lock(&dc_handle->mutex);
    {
        globus_assert(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK);
        globus_assert(eb_header->descriptor == 0);

        /*
         *  if there was error or we have been prematurely closed
         *  just rip the sucker down.
         */
        if(result != GLOBUS_SUCCESS)
        {
            error = globus_error_get(result);
            type = globus_object_get_type(error);

            if(!globus_object_type_match(
                type,
                GLOBUS_IO_ERROR_TYPE_IO_CANCELLED))
            {
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }
            else
            {
                error = globus_error_construct_string(
                       GLOBUS_FTP_CONTROL_MODULE,
                       error,
                       _FCSL("connection prematurely closed"));
            }
            eof = GLOBUS_TRUE;
        }
        else
        {
            /*
             *  if the stripe is trying to
             *  close so we need to register an EOD or an EOF
             */
            if(stripe->eof)
            {
                /*
                 *  If we are sending eof and we have not yet
                 *  done so then use this open data connection.
                 *  Create EOF with EOD on and send it
                 */
                if(dc_handle->send_eof)
                {
                    if(!stripe->eof_sent)
                    {
                        res = globus_l_ftp_control_data_register_eof(
                                  stripe,
                                  data_conn);
                        globus_assert(res == GLOBUS_SUCCESS);
                    }
                    else
                    {
                        res = globus_l_ftp_control_data_register_eod(
                                  stripe,
                                  data_conn);
                        globus_assert(res == GLOBUS_SUCCESS);
                    }
                }
                /*
                 * if we are not sending eof automatically then
                 * send eod on all but the last connection.
                 * the last connection is needed for sending
                 * the EOF message across.
                 */
                else
                {
                    if(stripe->connection_count > 1 || stripe->eof_sent)
                    {
                        res = globus_l_ftp_control_data_register_eod(
                                  stripe,
                                  data_conn);
                        globus_assert(res == GLOBUS_SUCCESS);
                    }
                    else
                    {
                        eof_cb_ent = globus_handle_table_lookup(
                                 &transfer_handle->handle_table,
                                 transfer_handle->eof_table_handle);
                        globus_assert(eof_cb_ent != GLOBUS_NULL);

                        if(!globus_handle_table_decrement_reference(
                               &transfer_handle->handle_table,
                               transfer_handle->eof_table_handle))
                        {
                            eof_callback = eof_cb_ent->callback;
                            transfer_handle->eof_cb_ent = GLOBUS_NULL;

                            /*
                             *  if we are not automatically sending eof
                             *  this bit of code ensures that the send
                             *  eof callback will happen after the
                             *  data write eof callback
                             */
                            if(transfer_handle->send_eof_ent != GLOBUS_NULL)
                            {
                                dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF;
                                send_eof_ent = transfer_handle->send_eof_ent;
                            }
                            else
                            {
                                dc_handle->state =
                                               GLOBUS_FTP_DATA_STATE_SEND_EOF;
                            }
                        }

                        globus_fifo_enqueue(&stripe->free_conn_q, data_conn);
                    }
                }
            }
            else
            {
                globus_fifo_enqueue(&stripe->free_conn_q, data_conn);
            }
            eof = entry->eof;
        }

        cb_ent = globus_handle_table_lookup(
                     &transfer_handle->handle_table,
                     entry->callback_table_handle);
        globus_assert(cb_ent != GLOBUS_NULL);

        if(!globus_handle_table_decrement_reference(
               &transfer_handle->handle_table,
               entry->callback_table_handle))
        {
            callback = cb_ent->callback;
            if(eof)
            {
                /*
                 *  if we are not automatically sending eof
                 *  this bit of code ensures that the send
                 *  eof callback will happen after the
                 *  data write eof callback
                 */
                if(dc_handle->send_eof)
                {
                    dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF;
                }
                else
                {
                    if(transfer_handle->send_eof_ent != GLOBUS_NULL)
                    {
                        dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF;
                        send_eof_ent = transfer_handle->send_eof_ent;
                    }
                    else
                    {
                        dc_handle->state = GLOBUS_FTP_DATA_STATE_SEND_EOF;
                    }
                }
            }
        }

        /*
         * we can free the entry structure
         */
        if(entry->ascii_buffer != GLOBUS_NULL)
        {
            globus_free(entry->ascii_buffer);
        }
        if(dc_handle->nl_ftp_handle_set)
        {
            /* faking memory allocation */
            char tag_str[128];
            sprintf(tag_str, "MODE=E TYPE=%c NBYTES=%ld",
                    dc_handle->type, (long) nl_bytes);
            globus_netlogger_write(
                &dc_handle->nl_ftp_handle,
                GFTP_NL_EVENT_SENT_DATA,
                "GFTPC",
                "Important",
                tag_str);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    /*
     *  call any callbacks that are ready
     */
    if(callback != GLOBUS_NULL)
    {
        callback(
            cb_ent->callback_arg,
            control_handle,
            error,
            cb_ent->buffer,
            cb_ent->length,
            cb_ent->offset,
            eof);
    }
    /*
     *  if the eof callback is the same as the callback we
     *  want to avoid calling it twice
     */
    if(eof_callback != GLOBUS_NULL && !cb_ent->eof)
    {
        eof_callback(
            eof_cb_ent->callback_arg,
            control_handle,
            error,
            eof_cb_ent->buffer,
            eof_cb_ent->length,
            eof_cb_ent->offset,
            GLOBUS_TRUE);
        globus_free(eof_cb_ent);
        transfer_handle->eof_cb_ent = GLOBUS_NULL;
    }
    /*
     * for non-automatic EOF sends
     */
    if(send_eof_ent != GLOBUS_NULL)
    {
        send_eof_ent->cb(
            send_eof_ent->user_arg,
            dc_handle->whos_my_daddy,
            GLOBUS_NULL);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        /*
         *  decrement the reference the callbacks had
         */
        if(callback != GLOBUS_NULL)
        {
            poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);

            if(entry->eof)
            {
                transfer_handle->eof_cb_ent = GLOBUS_NULL;
            }
            globus_free(cb_ent);
        }
        if(eof_callback != GLOBUS_NULL && !entry->eof)
        {
            poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        }
        if(send_eof_ent != GLOBUS_NULL)
        {
            poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        }
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    globus_free(entry);
    globus_free(iov);
    globus_free(eb_header);
    if(error)
    {
        globus_object_free(error);
    }
}


void
globus_l_ftp_close_msg_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbytes)
{
    globus_ftp_data_connection_t *              data_conn;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_result_t                             res;
    globus_l_ftp_data_callback_info_t *         cb_info;

    cb_info = (globus_l_ftp_data_callback_info_t *)arg;
    data_conn = cb_info->data_conn;
    dc_handle = cb_info->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);

    globus_mutex_lock(&dc_handle->mutex);
    {
        res = globus_io_register_close(
                      &data_conn->io_handle,
                      globus_l_ftp_io_close_callback,
                      (void *)cb_info);
        if(res != GLOBUS_SUCCESS)
        {
            res = globus_callback_register_oneshot(
                     GLOBUS_NULL,
                     GLOBUS_NULL,
                     globus_l_ftp_control_io_close_kickout,
                     cb_info);
            globus_assert(res == GLOBUS_SUCCESS);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    globus_free(buf);
}

void
globus_l_ftp_eb_send_eof_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbytes)
{
    globus_ftp_data_connection_t *              data_conn;
    globus_ftp_data_stripe_t *                  stripe;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_l_ftp_send_eof_entry_t *             eof_ent;
    globus_l_ftp_send_eof_entry_t *             tmp_ent;
    globus_object_t *                           error = GLOBUS_NULL;
    globus_bool_t                               fire_cb = GLOBUS_FALSE;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    const globus_object_type_t *                type;
    globus_bool_t                               poll;
    globus_ftp_data_connection_state_t          initial_state;

    eof_ent = (globus_l_ftp_send_eof_entry_t *)arg;
    data_conn = eof_ent->whos_my_daddy;
    stripe = data_conn->whos_my_daddy;
    transfer_handle = stripe->whos_my_daddy;
    dc_handle = transfer_handle->whos_my_daddy;
    GlobusFTPControlDataTestMagic(dc_handle);

    globus_mutex_lock(&dc_handle->mutex);
    {
        /* in case of error we ended up never calling the user cb, since 
         * we destroy right after this -- save original error now so 
         * we can check later if we should fire_cb */
        initial_state = dc_handle->state;
        
        globus_assert(eof_ent->dc_handle->transfer_handle != NULL);
        if(result != GLOBUS_SUCCESS)
        {
            error = globus_error_get(result);
            type = globus_object_get_type(error);

            if(!globus_object_type_match(
                type,
                GLOBUS_IO_ERROR_TYPE_IO_CANCELLED))
            {
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }
            else
            {
                error = globus_error_construct_string(
                       GLOBUS_FTP_CONTROL_MODULE,
                       GLOBUS_NULL,
                       _FCSL("connection prematurely closed"));
            }
        }
        else
        {
            globus_list_insert(
                &stripe->free_cache_list,
                (void*)data_conn);
        }
        tmp_ent = globus_handle_table_lookup(
                      &transfer_handle->handle_table,
                      eof_ent->callback_table_handle);
        globus_assert(tmp_ent != NULL);

        if(!globus_handle_table_decrement_reference(
               &transfer_handle->handle_table,
               tmp_ent->callback_table_handle))
        {
            /*
             *  if data_write(eof = 1) callback has happened we
             *  can kick out this callback other wise we cache the
             *  send_eof_ent and call its callback after the data_write
             *  eof has occurred.
             */
            if(dc_handle->state == GLOBUS_FTP_DATA_STATE_SEND_EOF)
            {
                fire_cb = GLOBUS_TRUE;
                dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF;
                globus_free(tmp_ent->count);
                globus_free(tmp_ent);
            }
            else if(initial_state == GLOBUS_FTP_DATA_STATE_SEND_EOF && error)
            {
                fire_cb = GLOBUS_TRUE;
                globus_free(tmp_ent->count);
                globus_free(tmp_ent);
            }                
            else
            {
                transfer_handle->send_eof_ent = tmp_ent;
            }
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    if(fire_cb)
    {
        eof_ent->cb(
            eof_ent->user_arg,
            eof_ent->dc_handle->whos_my_daddy,
            error);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);

        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    globus_free(eof_ent->count);
    globus_free(eof_ent);
    globus_free(buf);
    if(error)
    {
        globus_object_free(error);
    }
}

/*
 *  this message is called when there is a zero length
 *  payload sent with an EOD or an EOF is sent.
 */
void
globus_l_ftp_eb_eof_eod_callback(
    void *                                      arg,
    globus_io_handle_t *                        handle,
    globus_result_t                             result,
    globus_byte_t *                             buf,
    globus_size_t                               nbytes)
{
    globus_ftp_data_connection_t *              data_conn;
    globus_ftp_data_stripe_t *                  stripe;
    globus_i_ftp_dc_handle_t *                  dc_handle;
    globus_l_ftp_eb_header_t *                  eb_header;
    globus_ftp_control_handle_t    *            control_handle;
    globus_ftp_control_data_callback_t          eof_callback = GLOBUS_NULL;
    globus_l_ftp_handle_table_entry_t *         eof_cb_ent;
    globus_l_ftp_data_callback_info_t *         callback_info;
    globus_object_t *                           error = GLOBUS_NULL;
    const globus_object_type_t *                type;
    globus_i_ftp_dc_transfer_handle_t *         transfer_handle;
    globus_l_ftp_send_eof_entry_t *             send_eof_ent = GLOBUS_NULL;
    globus_bool_t                               poll;

    callback_info = ( globus_l_ftp_data_callback_info_t *)arg;

    stripe = callback_info->stripe;
    dc_handle = callback_info->dc_handle;
    GlobusFTPControlDataTestMagic(dc_handle);
    data_conn = callback_info->data_conn;
    control_handle = dc_handle->whos_my_daddy;
    transfer_handle = stripe->whos_my_daddy;

    eb_header = (globus_l_ftp_eb_header_t *)buf;

    globus_mutex_lock(&dc_handle->mutex);
    {
        globus_assert(dc_handle->mode == GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK);
        /* if stripes destroy was called */
        if(dc_handle->state == GLOBUS_FTP_DATA_STATE_CLOSING)
        {
        }
        /* if an error occurred */
        else if(result != GLOBUS_SUCCESS)
        {
            error = globus_error_get(result);
            type = globus_object_get_type(error);

            if(!globus_object_type_match(
                type,
                GLOBUS_IO_ERROR_TYPE_IO_CANCELLED))
            {
                globus_l_ftp_control_stripes_destroy(dc_handle, error);
            }
            else
            {
                error = globus_error_construct_string(
                       GLOBUS_FTP_CONTROL_MODULE,
                       GLOBUS_NULL,
                       _FCSL("connection prematurely closed"));
            }
        }
        else
        {
            /* this will be either EOD, or EOD and EOF */

            /*
             * if a close message was sent
              */
            if(eb_header->descriptor & GLOBUS_FTP_CONTROL_DATA_DESCRIPTOR_CLOSE)            {
                globus_list_remove_element(
                    &stripe->all_conn_list,
                    (void *)data_conn);
                data_conn->eod = GLOBUS_FALSE;
                globus_l_ftp_control_register_close_msg(dc_handle, data_conn);
            }
            /*
             * otherwise cache the connection
             */
            else
            {
                globus_list_insert(
                    &stripe->free_cache_list,
                    (void*)data_conn);
            }
        }

        /*
         *  get the eof entry structure and decrement its reference.
         *  If it is the final reference call the uses callback.
         */
        eof_cb_ent = globus_handle_table_lookup(
                         &transfer_handle->handle_table,
                         transfer_handle->eof_table_handle);

        if(eof_cb_ent && !globus_handle_table_decrement_reference(
               &transfer_handle->handle_table,
               transfer_handle->eof_table_handle))
        {
            eof_callback = eof_cb_ent->callback;
            transfer_handle->eof_cb_ent = GLOBUS_NULL;

            if(dc_handle->send_eof)
            {
                dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF;
            }
            else
            {
                if(transfer_handle->send_eof_ent == GLOBUS_NULL)
                {
                    dc_handle->state = GLOBUS_FTP_DATA_STATE_SEND_EOF;
                }
                else
                {
                    send_eof_ent = transfer_handle->send_eof_ent;
                    dc_handle->state = GLOBUS_FTP_DATA_STATE_EOF;
                }
            }
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    if(eof_callback != GLOBUS_NULL)
    {
        eof_callback(
            eof_cb_ent->callback_arg,
            control_handle,
            error,
            eof_cb_ent->buffer,
            eof_cb_ent->length,
            eof_cb_ent->offset,
            GLOBUS_TRUE);
        globus_free(eof_cb_ent);
    }

    if(send_eof_ent != GLOBUS_NULL)
    {
        send_eof_ent->cb(
            send_eof_ent->user_arg,
            dc_handle->whos_my_daddy,
            GLOBUS_NULL);
    }

    globus_mutex_lock(&dc_handle->mutex);
    {
        /*
         *  decrement the reference the callbacks had
         */
        if(eof_callback != GLOBUS_NULL)
        {
            globus_l_ftp_control_dc_dec_ref(transfer_handle);
        }
        if(send_eof_ent != GLOBUS_NULL)
        {
            globus_l_ftp_control_dc_dec_ref(transfer_handle);
        }
        /*
         *  decrement the reference this callback has
         */
        poll = !globus_l_ftp_control_dc_dec_ref(transfer_handle);
        if(poll)
        {
            globus_l_ftp_data_stripe_poll(dc_handle);
        }
    }
    globus_mutex_unlock(&dc_handle->mutex);

    globus_free(callback_info);
    globus_free(eb_header);
    if(error)
    {
        globus_object_free(error);
    }
}


/*********************************************************************
*  other functions
*********************************************************************/
static
void
globus_l_ftp_control_data_encode(
    globus_byte_t *                   buf,
    globus_off_t                      in_size)
{
    globus_off_t                      x;
    int				      ind;

    x = (globus_off_t) in_size;

    ind = 0;

    if(sizeof(globus_off_t) < 8)
    {
	buf[ind++] = 0;
    }
    else
    {
	buf[ind++] = (x >> 56) & 0xff;
    }

    if(sizeof(globus_off_t) < 7)
    {
	buf[ind++] = 0;
    }
    else
    {
	buf[ind++] = (x >> 48) & 0xff;
    }

    if(sizeof(globus_off_t) < 6)
    {
	buf[ind++] = 0;
    }
    else
    {
	buf[ind++] = (x >> 40) & 0xff;
    }

    if(sizeof(globus_off_t) < 5)
    {
	buf[ind++] = 0;
    }
    else
    {
	buf[ind++] = (x >> 32) & 0xff;
    }

    if(sizeof(globus_off_t) < 4)
    {
	buf[ind++] = 0;
    }
    else
    {
	buf[ind++] = (x >> 24) & 0xff;
    }

    if(sizeof(globus_off_t) < 3)
    {
	buf[ind++] = 0;
    }
    else
    {
	buf[ind++] = (x >> 16) & 0xff;
    }

    if(sizeof(globus_off_t) < 2)
    {
	buf[ind++] = 0;
    }
    else
    {
	buf[ind++] = (x >> 8)  & 0xff;
    }

    if(sizeof(globus_off_t) < 1)
    {
	buf[ind++] = 0;
    }
    else
    {
	buf[ind++] = (x)       & 0xff;
    }
}
/* globus_l_ftp_control_data_encode() */

static
void
globus_l_ftp_control_data_decode(
    globus_byte_t *                   buf,
    globus_off_t  *                   out_size)
{
    globus_off_t		      x;
    globus_bool_t		      overflow;
    x = 0;

    overflow = GLOBUS_FALSE;

    if(sizeof(globus_off_t) >= 8)
    {
	x += ((globus_off_t) buf[0]) << 56;
    }
    else
    {
	if(buf[0] != 0)
	{
	    overflow = GLOBUS_TRUE;
	}
    }

    if(sizeof(globus_off_t) >= 7)
    {
	x += ((globus_off_t) buf[1]) << 48;
    }
    else
    {
	if(buf[1] != 0)
	{
	    overflow = GLOBUS_TRUE;
	}
    }

    if(sizeof(globus_off_t) >= 6)
    {
	x += ((globus_off_t) buf[2]) << 40;
    }
    else
    {
	if(buf[2] != 0)
	{
	    overflow = GLOBUS_TRUE;
	}
    }

    if(sizeof(globus_off_t) >= 5)
    {
	x += ((globus_off_t) buf[3]) << 32;
    }
    else
    {
	if(buf[3] != 0)
	{
	    overflow = GLOBUS_TRUE;
	}
    }

    if(sizeof(globus_off_t) >= 4)
    {
	x += ((globus_off_t) buf[4]) << 24;
    }
    else
    {
	if(buf[4] != 0)
	{
	    overflow = GLOBUS_TRUE;
	}
    }

    if(sizeof(globus_off_t) >= 3)
    {
	x += ((globus_off_t) buf[5]) << 16;
    }
    else
    {
	if(buf[5] != 0)
	{
	    overflow = GLOBUS_TRUE;
	}
    }

    if(sizeof(globus_off_t) >= 2)
    {
	x += ((globus_off_t) buf[6]) << 8;
    }
    else
    {
	if(buf[6] != 0)
	{
	    overflow = GLOBUS_TRUE;
	}
    }

    if(sizeof(globus_off_t) >= 1)
    {
	x += ((globus_off_t) buf[7]);
    }
    else
    {
	if(buf[7] != 0)
	{
	    overflow = GLOBUS_TRUE;
	}
    }

    /* should do something with overflow? */

    *out_size = (globus_off_t)x;
}
/* globus_l_ftp_control_data_decode() */

const char *
globus_l_ftp_control_state_to_string(
    globus_ftp_data_connection_state_t          state)
{
    static const char * none            = "NONE";
    static const char * pasv            = "PASV";
    static const char * port            = "PORT";
    static const char * spor            = "SPOR";
    static const char * connect_read    = "CONNECT_READ";
    static const char * connect_write   = "CONNECT_WRITE";
    static const char * closing         = "CLOSING";
    static const char * eof             = "EOF";
    static const char * send_eof        = "SEND_EOF";
    static const char * unknown         = "UNKNOWN";
    
    switch(state)
    {
      case GLOBUS_FTP_DATA_STATE_NONE:
        return none;
        break;
      case GLOBUS_FTP_DATA_STATE_PASV:
        return pasv;
        break;
      case GLOBUS_FTP_DATA_STATE_PORT:
        return port;
        break;
      case GLOBUS_FTP_DATA_STATE_SPOR:
        return spor;
        break;
      case GLOBUS_FTP_DATA_STATE_CONNECT_READ:
        return connect_read;
        break;
      case GLOBUS_FTP_DATA_STATE_CONNECT_WRITE:
        return connect_write;
        break;
      case GLOBUS_FTP_DATA_STATE_CLOSING:
        return closing;
        break;
      case GLOBUS_FTP_DATA_STATE_EOF:
        return eof;
        break;
      case GLOBUS_FTP_DATA_STATE_SEND_EOF:
        return send_eof;
        break;
      default:
        return unknown;
        break;
    }
}
