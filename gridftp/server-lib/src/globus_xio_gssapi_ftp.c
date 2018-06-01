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
#include "globus_xio_telnet.h"
#include "globus_xio_load.h"
#include "globus_common.h"
#include "globus_error_string.h"
#include "globus_xio_gssapi_ftp.h"
#include "globus_error_openssl.h"
#include "globus_gss_assist.h"
#include "gssapi.h"
#include <string.h>

#define GlobusXIOGssapiBadParameter()                                       \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_GSSAPI_FTP_BAD_PARAMETER,                            \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Bad Parameter"))
                                                                                
#define GlobusXIOGssapiFTPOutstandingOp()                                   \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_GSSAPI_FTP_OUTSTANDING_OP,                           \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Operation is outstanding"))
                                                                                
#define GlobusXIOGssapiFTPEncodingError()                                   \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_GSSAPI_FTP_ERROR_ENCODING,                           \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Error encoding."))
                                                                                
#define GlobusXIOGssapiFTPAllocError()                                      \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_GSSAPI_FTP_ERROR_ALLOC,                              \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Not enough memory"))

#define GlobusXIOGssapiFTPGSIAuthFailure(maj, min)                          \
    globus_error_put(                                                       \
        globus_error_wrap_gssapi_error(                                     \
            GLOBUS_XIO_MODULE,                                              \
            (maj),                                                          \
            (min),                                                          \
            GLOBUS_XIO_GSSAPI_FTP_ERROR_AUTH,                               \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Authentication Error"))

#define GlobusXIOGssapiFTPGSIFailure(maj, min, failure)                     \
    globus_error_put(                                                       \
        globus_error_wrap_gssapi_error(                                     \
            GLOBUS_XIO_MODULE,                                              \
            (maj),                                                          \
            (min),                                                          \
            GLOBUS_XIO_GSSAPI_FTP_ERROR_AUTH,                               \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            failure))
                                                                                            
#define GlobusXIOGssapiFTPAuthenticationFailure(str)                         \
     globus_error_put(                                                       \
         globus_error_construct_error(                                       \
             GLOBUS_XIO_MODULE,                                              \
             GLOBUS_NULL,                                                    \
             GLOBUS_XIO_GSSAPI_FTP_ERROR_AUTH,                               \
             __FILE__,                                                       \
             _xio_name,                                                      \
             __LINE__,                                                       \
             "Authentication Error: %s",                                     \
             str))
                                                                                
#define GlobusXIOGssapiFTPQuit()                                             \
     globus_error_put(                                                       \
         globus_error_construct_error(                                       \
             GLOBUS_XIO_MODULE,                                              \
             GLOBUS_NULL,                                                    \
             GLOBUS_XIO_GSSAPI_FTP_ERROR_QUIT,                               \
             __FILE__,                                                       \
             _xio_name,                                                      \
             __LINE__,                                                       \
             "Pre mature Quit, close connection"))

GlobusDebugDefine(GLOBUS_XIO_GSSAPI_FTP);

#define GlobusXIOGssapiftpDebugPrintf(level, message)                      \
    GlobusDebugPrintf(GLOBUS_XIO_GSSAPI_FTP, level, message)

#define GlobusXIOGssapiftpDebugEnter()                                     \
    GlobusXIOGssapiftpDebugPrintf(                                         \
        GLOBUS_L_XIO_GSSAPI_FTP_DEBUG_TRACE,                               \
        ("[%s] Entering\n", _xio_name))

#define GlobusXIOGssapiftpDebugExit()                                      \
    GlobusXIOGssapiftpDebugPrintf(                                         \
        GLOBUS_L_XIO_GSSAPI_FTP_DEBUG_TRACE,                               \
        ("[%s] Exiting\n", _xio_name))

#define GlobusXIOGssapiftpDebugExitWithError()                             \
    GlobusXIOGssapiftpDebugPrintf(                                         \
        GLOBUS_L_XIO_GSSAPI_FTP_DEBUG_TRACE,                               \
        ("[%s] Exiting with error\n", _xio_name))

#define GlobusXIOGssapiftpDebugPassRead()                             \
    GlobusXIOGssapiftpDebugPrintf(                                         \
        GLOBUS_L_XIO_GSSAPI_FTP_DEBUG_TRACE,                               \
        ("[%s] passing read\n", _xio_name))

#define GlobusXIOGssapiftpDebugPassWrite()                             \
    GlobusXIOGssapiftpDebugPrintf(                                         \
        GLOBUS_L_XIO_GSSAPI_FTP_DEBUG_TRACE,                               \
        ("[%s] passing write\n", _xio_name))

#define GlobusXIOGssapiftpDebugChangeState(_h, _new)                        \
do                                                                          \
{                                                                           \
    GlobusXIOGssapiftpDebugPrintf(                                          \
        GLOBUS_L_XIO_GSSAPI_FTP_DEBUG_TRACE,                                \
        ("[%s] Auth state change.\n  From %s\n  To %s\n", _xio_name,       \
        globus_l_xio_gssapi_ftp_state_names[_h->state],                     \
        globus_l_xio_gssapi_ftp_state_names[_new]));                         \
    _h->state = _new;                                                       \
} while(0)

enum globus_l_xio_error_levels
{
    GLOBUS_L_XIO_GSSAPI_FTP_DEBUG_TRACE       = 1,
    GLOBUS_L_XIO_GSSAPI_FTP_DEBUG_INFO        = 2
};

#define REPLY_530_BAD_MESSAGE "530 Please login with USER and PASS.\r\n"
#define REPLY_504_BAD_AUTH_TYPE "504 Unknown authentication type.\r\n"
#define REPLY_334_GOOD_AUTH_TYPE "334 Using authentication type; ADAT must follow.\r\n"
#define REPLY_530_EXPECTING_ADAT "530 Must perform GSSAPI authentication.\r\n"
#define REPLY_530_NO_CRED "530 Server does not have credentials for GSSAPI authentication.\r\n"
#define REPLY_530_BAD_ADAT "530 Authentication failed.\r\n"

#define REPLY_235_ADAT_DATA "235 ADAT="
#define REPLY_335_ADAT_DATA "335 ADAT="
#define REPLY_530_QUIT      "211 Goodbye.\r\n"

#define CLIENT_AUTH_GSSAPI_COMMAND "AUTH GSSAPI\r\n"

#define GSSAPI_FTP_DEFAULT_BUFSIZE 1024

typedef enum  globus_i_xio_gssapi_ftp_state_s
{
    /* starting state for both client and server */
    GSSAPI_FTP_STATE_NONE,
    /* server auhenticating states */
    GSSAPI_FTP_STATE_SERVER_READING_AUTH,
    GSSAPI_FTP_STATE_SERVER_GSSAPI_READ,
    GSSAPI_FTP_STATE_SERVER_READING_ADAT,
    GSSAPI_FTP_STATE_SERVER_ADAT_REPLY,
    GSSAPI_FTP_STATE_SERVER_QUITING,

    /* client authenticating states */
    GSSAPI_FTP_STATE_CLIENT_READING_220,
    GSSAPI_FTP_STATE_CLIENT_SENDING_AUTH,
    GSSAPI_FTP_STATE_CLIENT_ADAT_INIT,
    GSSAPI_FTP_STATE_CLIENT_SENDING_ADAT,

    /* open state is final state xio takes care of closing */
    GSSAPI_FTP_STATE_OPEN,
    GSSAPI_FTP_STATE_OPEN_CLEAR
} globus_i_xio_gssapi_ftp_state_t;


static char *                           globus_l_xio_gssapi_ftp_state_names[] =
{
    "GSSAPI_FTP_STATE_NONE",
    "GSSAPI_FTP_STATE_SERVER_READING_AUTH",
    "GSSAPI_FTP_STATE_SERVER_GSSAPI_READ",
    "GSSAPI_FTP_STATE_SERVER_READING_ADAT",
    "GSSAPI_FTP_STATE_SERVER_ADAT_REPLY",
    "GSSAPI_FTP_STATE_SERVER_QUITING",
    "GSSAPI_FTP_STATE_CLIENT_READING_220",
    "GSSAPI_FTP_STATE_CLIENT_SENDING_AUTH",
    "GSSAPI_FTP_STATE_CLIENT_ADAT_INIT",
    "GSSAPI_FTP_STATE_CLIENT_SENDING_ADAT",
    "GSSAPI_FTP_STATE_OPEN",
    "GSSAPI_FTP_STATE_OPEN_CLEAR"
};

static globus_xio_driver_t              globus_l_gssapi_telnet_driver = NULL;
static char                             globus_l_xio_gssapi_ftp_pad = '=';
static char *                           globus_l_xio_gssapi_ftp_radix_n =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                                                                                
/**************************************************************************
 *                    data type definitions 
 *                    ---------------------
 *************************************************************************/

/**
 *  opening
 *  -------
 *  When a user opens a handle the authentication process begins.  The 
 *  default case will do the entire authentication processes as described
 *  in the state machine.  However the user can override various bits
 *  of the authentication process by setting the start state on the handle
 *  attr.
 */
/*
 *  writting
 *  --------
 *  Once an open handle is esstablished the user may post writes.  If 
 *  a write is posted that does not contain a complete command (no \r\n)
 *  then it is copied to an internal buffer and the user is told that the
 *  write operation is complete.  As soon as a complete comman is received
 *  it is wrapped and encoded then passed down the stack.  Additional 
 *  data beyound a complete command is cached in the same way.
 */

/*
 *  reading
 *  -------
 *  When the user request a read into a buffer a request structure is created
 *  and added to a queue.  The queue is then polled for availuable data.
 *  There is alos a queue of complete commands recevied.  If this queue
 *  when the user requests data and a read is not outstanding, then another
 *  read is posted.  If it is not empty the command is removed and unwrapped.
 *  Unwrapped commands are put into yet another queue.  The first element
 *  in the unwrapped copied to the user buffer.  If the user buffer is not
 *  larege enough to compy the entire unwrapped command, then the remaining
 *  bits of it are left in the queue, otherwise it is removed.
 *
 *  note:  the reason for the queue which holds the commands before they
 *         are unrapped is fo the autenticaton of the open processes,  It
 *         may be possible to remove the need for this queue but at this point
 *         it is not causing any problems.
 */
typedef struct globus_l_xio_gssapi_ftp_handle_s
{
    /* gssapi security info */
    gss_ctx_id_t                        gssapi_context;
    gss_cred_id_t                       cred_handle;
    gss_cred_id_t                       delegated_cred_handle;
    char *                              auth_gssapi_subject;
    gss_name_t                          target_name;
    globus_bool_t                       encrypt;
    char *                              host;
    char *                              subject;

    char *                              banner;
    int                                 banner_length;

    globus_i_xio_gssapi_ftp_state_t     state;

    globus_bool_t                       client;
    globus_bool_t                       allow_clear;

    globus_bool_t                       read_posted;

    globus_mutex_t                      mutex;

    globus_xio_iovec_t                  auth_read_iov;
    globus_xio_iovec_t                  auth_write_iov;
    globus_xio_iovec_t *                read_iov;
    globus_xio_iovec_t *                write_iov;
    int                                 write_iov_count;
    globus_size_t                       orig_write_len;

    globus_byte_t *                     write_buffer;
    globus_bool_t                       write_posted;
    globus_xio_operation_t              op;
} globus_l_xio_gssapi_ftp_handle_t;

/*
 *  attribute structure.
 */
typedef struct globus_l_xio_gssapi_attr_s
{
    globus_bool_t                       encrypt;
    globus_bool_t                       force_server;
    globus_bool_t                       allow_clear;
    char *                              subject;
    globus_i_xio_gssapi_ftp_state_t     start_state;
} globus_l_xio_gssapi_attr_t;

/**************************************************************************
 *                    function prototypes
 *                    -------------------
 *************************************************************************/
static int
globus_l_xio_gssapi_ftp_activate();

static int
globus_l_xio_gssapi_ftp_deactivate();

static void
globus_l_xio_gssapi_ftp_preauth_client_read_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg);

static void
globus_l_xio_gssapi_ftp_auth_server_write_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg);

static void
globus_l_xio_gssapi_ftp_user_server_write_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg);

static globus_result_t
globus_l_xio_gssapi_ftp_decode_adat(
    globus_l_xio_gssapi_ftp_handle_t *  handle,
    const char *                        wrapped_command,
    char **                             out_reply,
    globus_bool_t *                     out_complete);

globus_result_t
globus_l_xio_gssapi_ftp_client_incoming(
    globus_l_xio_gssapi_ftp_handle_t *  handle,
    globus_xio_operation_t              op,
    char **                             cmd_a);

/**************************************************************************
 *                    global data
 *                    -----------
 *************************************************************************/
#include "version.h"

GlobusXIODefineModule(gssapi_ftp) =
{
    "globus_xio_gssapi_ftp",
    globus_l_xio_gssapi_ftp_activate,
    globus_l_xio_gssapi_ftp_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

/************************************************************************
 *                  utility functions
 *                  ------------------
 *
 *  A variety of operations use these function.
 ***********************************************************************/
static globus_result_t
globus_l_xio_gssapi_ftp_push_driver(
    globus_xio_driver_t                 driver,
    globus_xio_stack_t                  stack)
{
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_gssapi_ftp_push_driver);

    GlobusXIOGssapiftpDebugEnter();

    res = globus_xio_stack_push_driver(stack, globus_l_gssapi_telnet_driver);
    if(res != GLOBUS_SUCCESS)
    {
        return res;
    }
    res = globus_xio_stack_push_driver(stack, driver);
    {
        return res;
    }

    GlobusXIOGssapiftpDebugExit();

    return GLOBUS_SUCCESS;
}

/*
 *  allocate the memory for and initialize an internal handle
 */
static globus_l_xio_gssapi_ftp_handle_t *
globus_l_xio_gssapi_ftp_handle_create()
{
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    GlobusXIOName(globus_l_xio_gssapi_ftp_handle_create);

    GlobusXIOGssapiftpDebugEnter();

    /*
     *  create a new handle and initialize it
     */
    handle = (globus_l_xio_gssapi_ftp_handle_t *) 
                globus_libc_calloc(sizeof(globus_l_xio_gssapi_ftp_handle_t), 1);
    if(handle == NULL)
    {
        goto err;
    }
    handle->auth_read_iov.iov_base = (void *) 0x10;
    handle->auth_read_iov.iov_len = 1;
    handle->gssapi_context = GSS_C_NO_CONTEXT;
    handle->cred_handle = GSS_C_NO_CREDENTIAL;
    handle->delegated_cred_handle = GSS_C_NO_CREDENTIAL;
    handle->encrypt = GLOBUS_FALSE;
    handle->host = NULL;
    handle->subject = NULL;
    handle->target_name = GSS_C_NO_NAME;
    globus_mutex_init(&handle->mutex, NULL);

    /* read data members */
    handle->read_posted = GLOBUS_FALSE;
    handle->write_posted = GLOBUS_FALSE;

    GlobusXIOGssapiftpDebugExit();
    return handle;

err:

    GlobusXIOGssapiftpDebugExitWithError();
    return NULL;
}

/*
 *  clean up a handle and all memory associated with it
 */
static void
globus_l_xio_gssapi_ftp_handle_destroy(
    globus_l_xio_gssapi_ftp_handle_t *  handle)
{
    OM_uint32                           min_stat;
    GlobusXIOName(globus_l_xio_gssapi_ftp_handle_destroy);

    GlobusXIOGssapiftpDebugEnter();

    if(handle->subject)
    {
        globus_free(handle->subject);
    }
    if(handle->host)
    {
        globus_free(handle->host);
    }

    if(handle->target_name != GSS_C_NO_NAME)
    {
        gss_release_name(&min_stat, &handle->target_name);
    }
    if(handle->cred_handle != GSS_C_NO_CREDENTIAL)
    {
        gss_release_cred(&min_stat, &handle->cred_handle);
    }
    if(handle->delegated_cred_handle != GSS_C_NO_CREDENTIAL)
    {
        gss_release_cred(&min_stat, &handle->delegated_cred_handle);
    }
    if(handle->gssapi_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
            &min_stat,
            &handle->gssapi_context,
            GLOBUS_NULL);
    }
    if(handle->auth_gssapi_subject != NULL)
    {
        globus_free(handle->auth_gssapi_subject);
    }

    globus_free(handle);
    GlobusXIOGssapiftpDebugExit();
}

/*
 *  decode a base64 encoded string.  The caller provides all the needed
 *  memory.
 *
 *  TODO: move this to globus common
 */
static globus_result_t
globus_l_xio_gssapi_ftp_radix_decode(
    const unsigned char  *              inbuf,
    globus_byte_t *                     outbuf,
    globus_size_t *                     out_len)
{
    int                                 i;
    int                                 j;
    int                                 D;
    char *                              p;
    GlobusXIOName(globus_l_xio_gssapi_ftp_radix_decode);

    GlobusXIOGssapiftpDebugEnter();

    for (i=0,j=0; inbuf[i] && inbuf[i] != globus_l_xio_gssapi_ftp_pad; i++)
    {
        if ((p = strchr(globus_l_xio_gssapi_ftp_radix_n, inbuf[i])) == NULL)
        {
	    goto err;
        }
        D = p - globus_l_xio_gssapi_ftp_radix_n;
        switch (i&3)
        {
            case 0:
                outbuf[j] = D<<2;
                break;
            case 1:
                outbuf[j++] |= D>>4;
                outbuf[j] = (D&15)<<4;
                break;
            case 2:
                outbuf[j++] |= D>>2;
                outbuf[j] = (D&3)<<6;
                break;
            case 3:
                outbuf[j++] |= D;
                break;
            default:
                break;
        }
    }

    switch (i&3)
    {
        case 1:
	    goto err;
 
       case 2:
            if (D&15)
            {
	        goto err;
            }
            if (strcmp((char *)&inbuf[i], "=="))
            {
	        goto err;
            }
            break;

        case 3:
            if (D&3)
            {
	        goto err;
            }
            if (strcmp((char *)&inbuf[i], "="))
            {
	        goto err;
            }
            break;

        default:
            break;
    }
    *out_len = j;

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;

err:

    GlobusXIOGssapiftpDebugExitWithError();
    return GlobusXIOGssapiFTPEncodingError();

}

/*
 *  base64 encode a string, string may not be null terminated
 *
 *  TODO: move this to globus common
 */
static globus_result_t
globus_l_xio_gssapi_ftp_radix_encode(
    const unsigned char *               inbuf,
    globus_size_t                       in_len,
    globus_byte_t *                     outbuf,
    globus_size_t *                     out_len)
{
    int                                 i;
    int                                 j;
    unsigned char                       c;
    GlobusXIOName(globus_l_xio_gssapi_ftp_radix_encode);

    GlobusXIOGssapiftpDebugEnter();

    for (i=0,j=0; i < in_len; i++)
    {
        switch (i%3)
        {
            case 0:
                outbuf[j++] = globus_l_xio_gssapi_ftp_radix_n[inbuf[i]>>2];
                c = (inbuf[i]&3)<<4;
                break;
            case 1:
                outbuf[j++] = globus_l_xio_gssapi_ftp_radix_n[c|inbuf[i]>>4];
                c = (inbuf[i]&15)<<2;
                break;
            case 2:
                outbuf[j++] = globus_l_xio_gssapi_ftp_radix_n[c|inbuf[i]>>6];
                outbuf[j++] = globus_l_xio_gssapi_ftp_radix_n[inbuf[i]&63];
                c = 0;
                break;
            default:
                globus_assert(0);
                break;
        }
    }
    if (i%3)
    {
        outbuf[j++] = globus_l_xio_gssapi_ftp_radix_n[c];
    }
    switch (i%3)
    {
        case 1:
            outbuf[j++] = globus_l_xio_gssapi_ftp_pad;
        case 2:
            outbuf[j++] = globus_l_xio_gssapi_ftp_pad;
    }

    outbuf[j] = '\0';
    *out_len = j;

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;
}

/*
 *  tokenze a bufer based on the globus ftp protocol.  White space will
 *  seperate tokens and DRLF will be removed from them.
 */
static globus_byte_t *
globus_l_xio_gssapi_ftp_token(
    globus_byte_t *                     in_str,
    globus_size_t                       length,
    globus_size_t *                     out_start_off,
    globus_size_t *                     out_length)
{
    globus_byte_t *                     start_ptr;
    globus_byte_t *                     tmp_ptr;
    globus_byte_t *                     end_ptr;
    GlobusXIOName(globus_l_xio_gssapi_ftp_token);

    GlobusXIOGssapiftpDebugEnter();

    end_ptr = &in_str[length];
    tmp_ptr = in_str;
    while(tmp_ptr != end_ptr && isspace(*tmp_ptr))
    {
        tmp_ptr++;
    }
    if(tmp_ptr == end_ptr)
    {
        GlobusXIOGssapiftpDebugExit();
        return NULL;
    }
    start_ptr = tmp_ptr;
    *out_start_off = (tmp_ptr - in_str);

    while(tmp_ptr != end_ptr && !isspace(*tmp_ptr))
    {
        tmp_ptr++;
    }
    *out_length = tmp_ptr - start_ptr;

    GlobusXIOGssapiftpDebugExit();
    return start_ptr;
}

/*
 *  decode a command
 */
static globus_result_t
globus_l_xio_gssapi_ftp_decode_adat(
    globus_l_xio_gssapi_ftp_handle_t *  handle,
    const char *                        wrapped_command,
    char **                             out_reply,
    globus_bool_t *                     out_complete)
{
    char *                              reply;
    globus_result_t                     res;
    OM_uint32                           ret_flags = GSS_C_CONF_FLAG;
    OM_uint32                           min_stat;
    OM_uint32                           maj_stat;
    globus_size_t                       length;
    globus_byte_t *                     decoded_cmd;
    gss_buffer_desc                     recv_tok = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc                     send_tok = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc                     subject_buf = GSS_C_EMPTY_BUFFER;
    gss_OID                             mech_type;
    GlobusXIOName(globus_l_xio_gssapi_ftp_decode_adat);

    GlobusXIOGssapiftpDebugEnter();

    length = globus_libc_strlen(wrapped_command);
    if(length <= 0)
    {
        res = GlobusXIOGssapiFTPAuthenticationFailure(
            "attempting to wrap a 0 length command.");
        goto err;
    }

    decoded_cmd = malloc((length+3) * 6/8);
    if(decoded_cmd == NULL)
    {
        res = GlobusXIOGssapiFTPAllocError();
        goto err;
    }
    res = globus_l_xio_gssapi_ftp_radix_decode(
            (globus_byte_t *) wrapped_command,
            decoded_cmd,
            &length);
    if(res != GLOBUS_SUCCESS)
    {
        globus_free(decoded_cmd);
        goto err;
    }

    recv_tok.value = decoded_cmd;
    recv_tok.length = length;
    maj_stat = gss_accept_sec_context(
        &min_stat,
        &handle->gssapi_context,
        handle->cred_handle,
        &recv_tok,
        GSS_C_NO_CHANNEL_BINDINGS,
        &handle->target_name,
        &mech_type,
        &send_tok,
        &ret_flags,
        GLOBUS_NULL,
        &handle->delegated_cred_handle);
    globus_free(decoded_cmd);

    switch(maj_stat)
    {
        /* if we have finished the security exchange */
        case GSS_S_COMPLETE:
            /* get the subject and copy into handle */
            
            maj_stat = gss_display_name(
                &min_stat,
                handle->target_name,
                &subject_buf,
                &mech_type);
            if(maj_stat != GSS_S_COMPLETE)
            {
                gss_release_buffer(&min_stat, &send_tok);
                res = GlobusXIOGssapiFTPAllocError();
                goto err;
            }
            handle->auth_gssapi_subject =
                globus_libc_strndup(subject_buf.value, subject_buf.length);
            globus_free(subject_buf.value);

            if(handle->auth_gssapi_subject == NULL)
            {
                gss_release_buffer(&min_stat, &send_tok);
                res = GlobusXIOGssapiFTPAllocError();
                goto err;
            }

            /* may have to still send some adat stuff back, check out len */
            if(send_tok.length == 0)
            {
                reply = globus_libc_strdup(
                            "235 GSSAPI Authentication successful.\r\n");
                if(reply == NULL)
                {
                    gss_release_buffer(&min_stat, &send_tok);
                    res = GlobusXIOGssapiFTPAllocError();
                    goto err;
                }
            }
            else
            {
                reply = (char *) globus_libc_malloc(
                                send_tok.length * 8 / 6 + 16);
                if(reply == NULL)
                {
                    gss_release_buffer(&min_stat, &send_tok);
                    res = GlobusXIOGssapiFTPAllocError();
                    goto err;
                }
                strcpy(reply, REPLY_235_ADAT_DATA);
                length = send_tok.length;
                res = globus_l_xio_gssapi_ftp_radix_encode(
                        send_tok.value,
                        send_tok.length,
                        (globus_byte_t *) &reply[strlen(REPLY_235_ADAT_DATA)],
                        &length);
                if(res != GLOBUS_SUCCESS)
                {
                    gss_release_buffer(&min_stat, &send_tok);
                    goto err;
                }
                memcpy(&reply[strlen(REPLY_235_ADAT_DATA)+length], "\r\n\0", 3);
            }
            *out_complete = GLOBUS_TRUE;
            gss_release_buffer(&min_stat, &send_tok);
            break;

        /* if we have more ADATS to send around */
        case GSS_S_CONTINUE_NEEDED:
            reply = (char *) globus_libc_malloc(
                            send_tok.length * 8 / 6 + 16);
            if(reply == NULL)
            {
                globus_free(reply);
                gss_release_buffer(&min_stat, &send_tok);
                res = GlobusXIOGssapiFTPAllocError();
                goto err;
            }
            strcpy(reply, REPLY_335_ADAT_DATA);
            length = send_tok.length;
            res = globus_l_xio_gssapi_ftp_radix_encode(
                    send_tok.value,
                    send_tok.length,
                    (globus_byte_t *) &reply[strlen(REPLY_335_ADAT_DATA)],
                    &length);
            if(res != GLOBUS_SUCCESS)
            {
                globus_free(reply);
                gss_release_buffer(&min_stat, &send_tok);
                goto err;
            }
            memcpy(&reply[strlen(REPLY_335_ADAT_DATA)+length], "\r\n\0", 3);

            *out_complete = GLOBUS_FALSE;
            gss_release_buffer(&min_stat, &send_tok);
            break;

        default:
            res = GlobusXIOGssapiFTPGSIAuthFailure(maj_stat, min_stat);
            goto err;
            break;
    }

    *out_reply = reply;

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusXIOGssapiftpDebugExitWithError();
    return res;
}

static void
globus_l_xio_gssapi_ftp_free_cmd_a(
    char **                             cmd_a)
{
    int                                 ndx;

    for(ndx = 0; cmd_a[ndx] != NULL; ndx++)
    {
        globus_free(cmd_a[ndx]);
    }
    globus_free(cmd_a);
}

/*
 *  tokenize a command into a null teminated array of strings.  If the
 *  command being tokenized is a reply from the server this code will
 *  remove all continuation headers (631-) and the first element in the
 *  finally tokenized reply array will be the reply number.
 */
static globus_result_t
globus_l_xio_gssapi_ftp_parse_command(
    globus_byte_t *                     command,
    globus_size_t                       length,
    globus_bool_t                       client,
    char ***                            out_cmd_a)
{
    char *                              tmp_ptr;
    char **                             cmd_a = NULL;
    int                                 cmd_len = 16;
    int                                 ctr;
    globus_size_t                       start_ndx;
    globus_result_t                     res;
    globus_size_t                       len;
    globus_size_t                       sub_len;
    globus_bool_t                       multi = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_gssapi_ftp_parse_command);

    GlobusXIOGssapiftpDebugEnter();

    *out_cmd_a = NULL;

    cmd_a = (char **) globus_malloc(sizeof(char *) * cmd_len);
    if(cmd_a == NULL)
    {
        res = GlobusXIOGssapiFTPAllocError();
        goto err;
    }

    len = length;
    ctr = 0;
    tmp_ptr = (char *) globus_l_xio_gssapi_ftp_token(
        command, len, &start_ndx, &sub_len);
    while(tmp_ptr != NULL)
    {
        len -= start_ndx; /* start ndx is removed from the front */
        /* special handling for multiplart commands */
        if(client)
        {
            /* if this will be a multipart command */
            if(ctr == 0 && tmp_ptr[3] == '-')
            {
                cmd_a[ctr] = globus_libc_strndup(tmp_ptr, 3);
                multi = GLOBUS_TRUE;
                len -= 4;
                tmp_ptr += 4;
                sub_len -= 4;
                ctr++;
            }
            /* if a continuation line move past the start */
            else if(multi && sub_len >= 3 &&
                    strncmp(cmd_a[0], tmp_ptr, 3) == 0)
            {
                if(tmp_ptr[3] == ' ')
                {
                    len -= 4;
                    tmp_ptr = (char *) globus_l_xio_gssapi_ftp_token(
                        (globus_byte_t *) &tmp_ptr[4], len, &start_ndx, &sub_len);
                }
                else if(tmp_ptr[3] == '-')
                {
                    len -= 4;
                    tmp_ptr += 4;
                    sub_len -= 4;
                }
                /* feat is returning continuation commands that look strabge
                   this allows it to work, but TODO: verifiy server is 
                   correct */
                else
                {
                    len -= 4;
                    tmp_ptr += 4;
                    sub_len -= 4;
                }
            }
        }
        cmd_a[ctr] = globus_libc_strndup(
            tmp_ptr, sub_len);
        len -= sub_len;
        tmp_ptr += sub_len;
        tmp_ptr = (char *) globus_l_xio_gssapi_ftp_token(
            (globus_byte_t *) tmp_ptr, len, &start_ndx, &sub_len);
        ctr++;
        if(ctr == cmd_len)
        {
            cmd_len *= 2;
            cmd_a = (char **) globus_libc_realloc(cmd_a,
                                sizeof(char *) * cmd_len);
        }
    }
    if(ctr == 0)
    {
        globus_free(cmd_a);
        cmd_a = NULL;
    }
    else
    {
        cmd_a[ctr] = NULL;
    }
    *out_cmd_a = cmd_a;

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;

err:
    if(cmd_a != NULL)
    {
        globus_free(cmd_a);
    }
    GlobusXIOGssapiftpDebugExitWithError();
    return res;
}

/*
 *  take a wrapped buffer and decode and unwrap it.  The caller is 
 *  responsible for freeing the out buffer if the function returns 
 *  successfully.
 */
static globus_result_t
globus_l_xio_gssapi_ftp_unwrap(
    globus_l_xio_gssapi_ftp_handle_t *  handle,
    const char  *                       in_buf,
    globus_size_t                       in_length,
    char **                             out_buffer)
{
    globus_result_t                     res;
    gss_buffer_desc                     wrapped_token;
    gss_buffer_desc                     unwrapped_token;
    OM_uint32                           maj_stat;
    OM_uint32                           min_stat;
    globus_byte_t *                     buf;
    globus_size_t                       len;
    GlobusXIOName(globus_l_xio_gssapi_ftp_unwrap);

    GlobusXIOGssapiftpDebugEnter();

    /* allocate out buffer same size as in, assuming unwrap will be samller */
    buf = globus_malloc(in_length+3); /* + 2 is likely not needed since
                                         buffer will be big enough anyway
                                         but there maybe some crazy special
                                         case */

    if(buf == NULL)
    {
        res = GlobusXIOGssapiFTPAllocError();
        goto err;
    }
    len = in_length;

    res = globus_l_xio_gssapi_ftp_radix_decode(
            (const globus_byte_t *) in_buf, buf, &len);
    if(res != GLOBUS_SUCCESS)
    {
        res = GlobusXIOGssapiFTPAllocError();
        globus_free(buf);
        goto err;
    }

    wrapped_token.value = buf;
    wrapped_token.length = len;

    maj_stat = gss_unwrap(
                    &min_stat,
                    handle->gssapi_context,
                    &wrapped_token,
                    &unwrapped_token,
                    NULL,
                    NULL);
    if(maj_stat != GSS_S_COMPLETE)
    {
        res = GlobusXIOGssapiFTPGSIAuthFailure(maj_stat, min_stat);
        globus_free(buf);
        goto err;
    }
    
    globus_free(buf);
    
    /* copy the unwrapped token in */
    len = unwrapped_token.length;
    buf = globus_malloc(len+3);
    memcpy(buf, unwrapped_token.value, unwrapped_token.length);

    /* get rid of terminating NULL */
    if(buf[len - 1] == '\0')
    {
        len--;
    }
    if(buf[len - 1] != '\n' && buf[len - 2] != '\r')
    {
        buf[len] = '\r';
        len++;
        buf[len] = '\n';
        len++;
    }
    buf[len] = '\0';
    *out_buffer = (char *) buf;

    gss_release_buffer(&min_stat, &unwrapped_token);

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusXIOGssapiftpDebugExitWithError();
    return res;
}

/*
 *  wrap a command with gssapi encoding then base 64 encode it.  If the
 *  function returns successfully the caller is responsible for freeing
 *  the out_buffer.  The out buffer is formed into a ftp buffer based on
 *  values in the handle.  ex: if client starts with MIC or ENC. if server
 *  starts with 631 or 632
 */
static globus_result_t
globus_l_xio_gssapi_ftp_wrap(
    globus_l_xio_gssapi_ftp_handle_t *  handle,
    globus_byte_t  *                    in_buf, 
    globus_size_t                       length,
    void **                             out_buffer,
    globus_size_t *                     out_len,
    globus_bool_t                       client)
{
    char *                              encoded_buf;
    int                                 conf_state;
    gss_buffer_desc                     gss_in_buf;
    gss_buffer_desc                     gss_out_buf;
    OM_uint32                           maj_stat;
    OM_uint32                           min_stat;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_gssapi_ftp_wrap);

    GlobusXIOGssapiftpDebugEnter();

    gss_in_buf.value = in_buf;
    gss_in_buf.length = length;
                                                                                
    maj_stat = gss_wrap(&min_stat,
                        handle->gssapi_context,
                        0,
                        GSS_C_QOP_DEFAULT,
                        &gss_in_buf,
                        &conf_state,
                        &gss_out_buf);
    if(maj_stat  != GSS_S_COMPLETE)
    {
        res = GlobusXIOGssapiFTPGSIAuthFailure(maj_stat, min_stat);
        goto err;
    }

    encoded_buf = (char *) 
        globus_libc_malloc((gss_out_buf.length + 3) * 8 / 6 + 9);
    if(encoded_buf == NULL)
    {
        gss_release_buffer(&min_stat, &gss_out_buf);
        res = GlobusXIOGssapiFTPAllocError();
        goto err;
    }

    if(client)
    {
        if(conf_state == 0)
        {
            memcpy(encoded_buf, "MIC ", 4);
        }
        else
        {
            memcpy(encoded_buf, "ENC ", 4);
        }
    }
    else
    {
        if(conf_state == 0)
        {
            memcpy(encoded_buf, "631 ", 4);
        }
        else
        {
            memcpy(encoded_buf, "632 ", 4);
        }
    }

    globus_l_xio_gssapi_ftp_radix_encode(
        gss_out_buf.value,
        gss_out_buf.length,
        (globus_byte_t *) &encoded_buf[4],
        &gss_out_buf.length);

    encoded_buf[gss_out_buf.length+4]='\r';
    encoded_buf[gss_out_buf.length+5]='\n';
    encoded_buf[gss_out_buf.length+6]='\0';
    *out_buffer = (void *)encoded_buf;
    *out_len = gss_out_buf.length+6;

    gss_release_buffer(&min_stat, &gss_out_buf);

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusXIOGssapiftpDebugExitWithError();
    return res;
}

/*  preline only affects multiline strings.
    a null preline will prepend a "code-" to the front of each line by default, 
    otherwise the preline is prepended.
*/
static 
char *
globus_l_xio_gssapi_ftp_string_to_959(
    int                                 code,
    const char *                        in_str,
    const char *                        preline)
{
    globus_bool_t                       done = GLOBUS_FALSE;
    char *                              msg;
    char *                              tmp_ptr;
    char *                              start_ptr;
    char *                              start_ptr_copy;
    char *                              end_ptr;
    char *                              prepad = NULL;
    int                                 ctr = 0;

    if(in_str == NULL)
    {
        msg = globus_common_create_string("%d .\r\n", code);
    }
    else
    {
        start_ptr_copy = strdup(in_str);
        start_ptr = start_ptr_copy;
        msg = globus_common_create_string("%d-", code);
        if(preline == NULL)
        {
            prepad = globus_libc_strdup(msg);
        }
        else
        {
            prepad = (char *) preline;
        }
        while(!done)
        {
            end_ptr = strchr(start_ptr, '\n');
            if(end_ptr != NULL)
            {
                *end_ptr = '\0';
                end_ptr++;
                if(*end_ptr == '\0')
                {
                    end_ptr = NULL;
                    done = GLOBUS_TRUE;
                }
            }
            else
            {
                done = GLOBUS_TRUE;
            }

            tmp_ptr = msg;
            msg = globus_common_create_string(
                "%s%s%s\r\n", 
                tmp_ptr, 
                (ctr > 0) ? prepad : "",
                start_ptr);
            globus_free(tmp_ptr);

            start_ptr = end_ptr;
            ctr++;
        }
        globus_free(start_ptr_copy);
        if(preline == NULL)
        {
            globus_free(prepad);
        }
        if(ctr == 1)
        {
            msg[3] = ' ';
        }
        else
        {
            tmp_ptr = msg;
            msg = globus_common_create_string("%s%d End.\r\n", tmp_ptr, code);
            globus_free(tmp_ptr);
        }
    }

    return msg;
}
/************************************************************************
 *                  server open
 *                  -----------
 *
 *  This section contains the functions used by a server in openning 
 *  a handle.
 ***********************************************************************/
/*
 *  when a full command comes in for the server this is called.
 */
static void
globus_l_xio_gssapi_ftp_server_read_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    char *                              out_buf;
    char *                              msg;
    globus_result_t                     res;
    globus_bool_t                       complete;
    globus_bool_t                       reply = GLOBUS_TRUE;
    char **                             cmd_a = NULL;
    globus_byte_t *                     in_buffer;
    globus_size_t                       in_buffer_len;
    globus_ssize_t                      finish_len = -1;
    OM_uint32                           maj_stat;
    OM_uint32                           min_stat;
    GlobusXIOName(globus_l_xio_gssapi_ftp_server_read_cb);

    GlobusXIOGssapiftpDebugEnter();

    handle = (globus_l_xio_gssapi_ftp_handle_t *) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        globus_assert(!handle->client);

        if(result != GLOBUS_SUCCESS)
        {
            res = result;
            goto err;
        }

        handle->read_posted = GLOBUS_FALSE;

        in_buffer = handle->auth_read_iov.iov_base;
        in_buffer_len = handle->auth_read_iov.iov_len;
        res = globus_l_xio_gssapi_ftp_parse_command(
                in_buffer,
                in_buffer_len,
                GLOBUS_FALSE,
                &cmd_a);
        if(res != GLOBUS_SUCCESS || cmd_a == NULL)
        {
            res = GlobusXIOGssapiFTPAllocError();
            goto err;
        }
        switch(handle->state)
        {
            /* verifiy that we can handle this auth type */
            case GSSAPI_FTP_STATE_SERVER_READING_AUTH:
                /* if command is not expected, stay in this state. */
                if(strcasecmp(cmd_a[0], "QUIT") == 0)
                {
                    GlobusXIOGssapiftpDebugChangeState(handle,
                        GSSAPI_FTP_STATE_SERVER_QUITING);
                    msg = globus_libc_strdup(REPLY_530_QUIT);
                }
                else if(strcasecmp(cmd_a[0], "AUTH") != 0)
                {
                    if(handle->allow_clear)
                    {
                        if(strcasecmp(cmd_a[0], "USER") == 0)
                        {
                            GlobusXIOGssapiftpDebugChangeState(handle,
                                GSSAPI_FTP_STATE_OPEN_CLEAR);
                        }
                        reply = GLOBUS_FALSE;

                        handle->read_iov[0].iov_base = in_buffer;
                        handle->read_iov[0].iov_len = in_buffer_len;
                        finish_len = in_buffer_len;
                        in_buffer = NULL;
                    }
                    else
                    {
                        msg = globus_libc_strdup(REPLY_530_EXPECTING_ADAT);
                    }
                }
                else if(cmd_a[1] == NULL || 
                        strcasecmp(cmd_a[1], "GSSAPI") != 0)
                {
                    msg = globus_libc_strdup(REPLY_504_BAD_AUTH_TYPE);
                }
                else
                {
                    /* get the credential now.  if we can't get it fail */
                        maj_stat = globus_gss_assist_acquire_cred(
                                        &min_stat,
                                        GSS_C_ACCEPT,
                                        &handle->cred_handle);
                    if (maj_stat != GSS_S_COMPLETE
                        && getenv("X509_VHOST_CRED_DIR") == NULL)
                    {
                        char *          tmp_msg;
                        /* XXX need to propagate this error to server */
                        res = GlobusXIOGssapiFTPGSIFailure(
                            maj_stat, min_stat,
                            "Server side credential failure");
                        tmp_msg = globus_error_print_friendly(
                            globus_error_peek(res));
                        msg = globus_l_xio_gssapi_ftp_string_to_959(
                            530, tmp_msg, NULL);
                        globus_free(tmp_msg);
                       /* msg = globus_libc_strdup(REPLY_530_NO_CRED); */
                    }
                    else
                    {
                        GlobusXIOGssapiftpDebugChangeState(handle,
                            GSSAPI_FTP_STATE_SERVER_GSSAPI_READ);
                        msg = globus_libc_strdup(REPLY_334_GOOD_AUTH_TYPE);
                    }
                }
                break;

            /* on errors we stay in this state */
            case GSSAPI_FTP_STATE_SERVER_READING_ADAT:
                if(globus_libc_strcmp(cmd_a[0], "ADAT") != 0)
                {
                    if(strcasecmp(cmd_a[0], "QUIT") == 0)
                    {
                        GlobusXIOGssapiftpDebugChangeState(handle,
                            GSSAPI_FTP_STATE_SERVER_QUITING);
                        msg = globus_libc_strdup(REPLY_530_QUIT);
                    }
                    else
                    {
                        msg = globus_libc_strdup(REPLY_530_EXPECTING_ADAT);
                    }
                }
                else if(cmd_a[1] == NULL)
                {
                    msg = globus_libc_strdup(REPLY_530_EXPECTING_ADAT);
                }
                /* do all the work to figure out adat reply */
                else
                {
                    res = globus_l_xio_gssapi_ftp_decode_adat(
                        handle,
                        cmd_a[1],
                        &msg,
                        &complete);
                    if(res != GLOBUS_SUCCESS)
                    {
                        /* XXX send reply but restsart to READING_AUTH */
                        char *          tmp_msg;
                        tmp_msg = globus_error_print_friendly(
                            globus_error_peek(res));
                        msg = globus_l_xio_gssapi_ftp_string_to_959(
                            530, tmp_msg, NULL);
                        globus_free(tmp_msg);
                        /* msg = strdup(REPLY_530_BAD_ADAT); */
                    }
                    else
                    {
                        /* if compete change to the next state */
                        if(complete)
                        {
                            GlobusXIOGssapiftpDebugChangeState(handle,
                                GSSAPI_FTP_STATE_SERVER_ADAT_REPLY);
                        }
                    }
                }
                break;

            case GSSAPI_FTP_STATE_OPEN:
                reply = GLOBUS_FALSE;
                res = globus_l_xio_gssapi_ftp_unwrap(
                    handle,
                    cmd_a[1],
                    strlen(cmd_a[1]),
                    &out_buf);
                if(res != GLOBUS_SUCCESS)
                {
                    goto err;
                }
                handle->read_iov[0].iov_base = out_buf;
                handle->read_iov[0].iov_len = strlen(out_buf);

                finish_len = handle->read_iov[0].iov_len;
                break;

            case GSSAPI_FTP_STATE_OPEN_CLEAR:
                handle->read_iov[0].iov_base = in_buffer;
                handle->read_iov[0].iov_len = in_buffer_len;
                finish_len = in_buffer_len;
                in_buffer = NULL;
                reply = GLOBUS_FALSE;
                break;

            default:
                globus_assert(0 && "Handle should be in reading state");
                break;
        }
        if(reply)
        {
            /* send the entire reply */
            handle->auth_write_iov.iov_base = msg;
            handle->auth_write_iov.iov_len = globus_libc_strlen(msg);
            GlobusXIOGssapiftpDebugPassWrite();
            res = globus_xio_driver_pass_write(
                op, 
                &handle->auth_write_iov,
                1, 
                handle->auth_write_iov.iov_len,
                globus_l_xio_gssapi_ftp_auth_server_write_cb,
                handle);
            if(res != GLOBUS_SUCCESS)
            {
                goto err;
            }
        }

        globus_l_xio_gssapi_ftp_free_cmd_a(cmd_a);
    }
    globus_mutex_unlock(&handle->mutex);

    if(finish_len >= 0)
    {
        globus_xio_driver_finished_read(op, GLOBUS_SUCCESS, finish_len);
    }

    if(in_buffer != NULL)
    {
        globus_free(in_buffer);
    }

    GlobusXIOGssapiftpDebugExit();
    return;

  err:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_driver_finished_read(op, res, 0);
    GlobusXIOGssapiftpDebugExitWithError();
}

/*
 *  while in the open authentication process, this callback is used for
 *  all of the writes.  
 */ 
static void
globus_l_xio_gssapi_ftp_auth_server_write_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    /* this is used to alter where we read, while doing auth we use
       an internal iov. once auth is complete we switch to the user
       iov and pass a final read */
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusXIOName(globus_l_xio_gssapi_ftp_auth_server_write_cb);

    GlobusXIOGssapiftpDebugEnter();

    handle = (globus_l_xio_gssapi_ftp_handle_t *) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        /* if there was an error, finish the open with an error */
        if(result != GLOBUS_SUCCESS)
        {
            res = result;
            goto err;
        }

        handle->write_posted = GLOBUS_FALSE;
        globus_free(handle->auth_write_iov.iov_base);
        switch(handle->state)
        {
            /* this case occurs when a bad command wasread when an auth
                was expected.  Remain in this state and pass another read */
            case GSSAPI_FTP_STATE_SERVER_READING_AUTH:
                break;

            /* occurs after AUTH GSSAPI successfuly read, move to the
                ADAT state */
            case GSSAPI_FTP_STATE_SERVER_GSSAPI_READ:
                GlobusXIOGssapiftpDebugChangeState(handle,
                    GSSAPI_FTP_STATE_SERVER_READING_ADAT);
                break;

            /* occurs when unexpected command happens when adat is expected,
                remain in this state, and post another read */
            case GSSAPI_FTP_STATE_SERVER_READING_ADAT:
                break;

           case GSSAPI_FTP_STATE_SERVER_ADAT_REPLY:
                GlobusXIOGssapiftpDebugChangeState(handle, 
                    GSSAPI_FTP_STATE_OPEN);
                break;

            case GSSAPI_FTP_STATE_SERVER_QUITING:
                res = GlobusXIOGssapiFTPQuit();
                goto err;
                break;

            default:
                break;
        }

        GlobusXIOGssapiftpDebugPassRead();
        res = globus_xio_driver_pass_read(
            op,
            &handle->auth_read_iov,
            1,
            1,
            globus_l_xio_gssapi_ftp_server_read_cb,
            handle);
        /* start processing the next command */
        if(res != GLOBUS_SUCCESS)
        {
            goto err;
        }
    }
    globus_mutex_unlock(&handle->mutex);

    GlobusXIOGssapiftpDebugExit();
    return;

  err:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_driver_finished_read(op, res, nbytes);
    GlobusXIOGssapiftpDebugExitWithError();
    return;
}
/*
 *  while in the open authentication process, this callback is used for
 *  all of the writes.  
 */ 
static void
globus_l_xio_gssapi_ftp_client_preauth_client_write_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusXIOName(globus_l_xio_gssapi_ftp_client_preauth_client_write_cb);

    GlobusXIOGssapiftpDebugEnter();

    handle = (globus_l_xio_gssapi_ftp_handle_t *) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        globus_free(handle->auth_write_iov.iov_base);
        /* if there was an error, finish the open with an error */
        if(result != GLOBUS_SUCCESS)
        {
            res = result;
            goto err;
        }

        GlobusXIOGssapiftpDebugPassRead();
        /* bogus read iov */
        res = globus_xio_driver_pass_read(
            op,
            &handle->auth_read_iov,
            1,
            1,
            globus_l_xio_gssapi_ftp_preauth_client_read_cb,
            handle);
        if(res != GLOBUS_SUCCESS)
        {
            goto err;
        }
    }
    globus_mutex_unlock(&handle->mutex);

    GlobusXIOGssapiftpDebugExit();
    return;

  err:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_driver_finished_open(handle, op, res);
    GlobusXIOGssapiftpDebugExitWithError();

    return;
}


/*
 *   accepting
 *
 *   Meary pass the accept, set handle state to server.  The open will
 *   take care of the protocol exchange.
 */
static void
globus_l_xio_gssapi_ftp_accept_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    GlobusXIOName(globus_l_xio_gssapi_ftp_accept_cb);

    GlobusXIOGssapiftpDebugEnter();

    if(result != GLOBUS_SUCCESS)
    {
        goto err;
    }

    globus_xio_driver_finished_accept(op, (void *) 0x01, GLOBUS_SUCCESS);

    GlobusXIOGssapiftpDebugExit();
    return;

  err:

    globus_xio_driver_finished_accept(op, NULL, result);
    GlobusXIOGssapiftpDebugExitWithError();
    return;
}

/*
 *  callback for the pass open.  If the state is not completely open 
 *  post a read to move to the next point in the authentication 
 *  process.  In the normal case this will be AUTH, however the user
 *  may circumvent these steps.
 */
static void
globus_l_xio_gssapi_ftp_client_open_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_bool_t                       finish = GLOBUS_FALSE;
    globus_result_t                     res;
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    GlobusXIOName(globus_l_xio_gssapi_ftp_client_open_cb);

    GlobusXIOGssapiftpDebugEnter();

    handle = (globus_l_xio_gssapi_ftp_handle_t *) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        globus_assert(handle->client);
        if(result != GLOBUS_SUCCESS)
        {
            res = result;
            goto err;
        }

        if(handle->state != GSSAPI_FTP_STATE_OPEN)
        {
            GlobusXIOGssapiftpDebugPassRead();
            res = globus_xio_driver_pass_read(
                op,
                &handle->auth_read_iov,
                1,
                1,
                globus_l_xio_gssapi_ftp_preauth_client_read_cb,
                handle);
            if(res != GLOBUS_SUCCESS)
            {
                goto err;
            }
        }
        else
        {
            finish = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&handle->mutex);

    if(finish)
    {
        globus_xio_driver_finished_open(handle, op, GLOBUS_SUCCESS);
    }

    GlobusXIOGssapiftpDebugExit();
    return;

  err:
    globus_mutex_unlock(&handle->mutex);
    globus_xio_driver_finished_open(handle, op, res);
}

static void
globus_l_xio_gssapi_ftp_server_open_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    GlobusXIOName(globus_l_xio_gssapi_ftp_server_open_cb);

    GlobusXIOGssapiftpDebugEnter();

    handle = (globus_l_xio_gssapi_ftp_handle_t *) user_arg;

    globus_xio_driver_finished_open(handle, op, result);

    GlobusXIOGssapiftpDebugExit();
}

/************************************************************************
 *                  client open functions
 *                  ---------------------
 *  
 *   This section has functions that open a handle for a client
 ***********************************************************************/

static globus_result_t
globus_l_xio_gssapi_ftp_client_adat(
    globus_l_xio_gssapi_ftp_handle_t *  handle,
    const char *                        buffer,
    char **                             out_buffer,
    globus_bool_t *                     complete)
{
    globus_result_t                     result;
    char *                              contact_string = NULL;
    gss_buffer_desc                     send_tok;
    gss_buffer_desc                     recv_tok;
    gss_buffer_desc *                   token_ptr;
    OM_uint32                           min_stat;
    OM_uint32                           maj_stat;
    OM_uint32                           req_flags = 0;
    globus_result_t                     res;
    globus_byte_t *                     radix_buf;
    globus_size_t                       length;
    gss_OID                             name_type;
    globus_xio_driver_handle_t          xio_driver_handle;
    globus_xio_contact_t                contact_info;
    GlobusXIOName(globus_l_xio_gssapi_ftp_client_adat);

    GlobusXIOGssapiftpDebugEnter();

    switch(handle->state)
    {
        case GSSAPI_FTP_STATE_CLIENT_ADAT_INIT:

            if(handle->subject == NULL)
            {
                xio_driver_handle = 
                    globus_xio_operation_get_driver_handle(handle->op);

                result = globus_xio_driver_handle_cntl(
                    xio_driver_handle,
                    GLOBUS_XIO_QUERY,
                    GLOBUS_XIO_GET_REMOTE_CONTACT,
                    &contact_string);
                if(result != GLOBUS_SUCCESS)
                {
                    contact_string = globus_common_create_string(
                        "host@%s", handle->host);
                }
                else
                {
                    result = globus_xio_contact_parse(
                        &contact_info, contact_string);
                    globus_free(contact_string);
                    if(result != GLOBUS_SUCCESS)
                    {
                        contact_string = globus_common_create_string(
                            "host@%s", handle->host);
                    }
                    else
                    {
                        contact_string = globus_common_create_string(
                            "host@%s", contact_info.host);
                        globus_xio_contact_destroy(&contact_info);
                    }
                }

                send_tok.value = contact_string;
                send_tok.length = strlen(contact_string) + 1;
                name_type = GSS_C_NT_HOSTBASED_SERVICE;
            }
            else
            {
                send_tok.value = handle->subject;
                send_tok.length = strlen(handle->subject) + 1;
                name_type = GSS_C_NT_USER_NAME;
            }
            maj_stat = gss_import_name(
                            &min_stat,
                            &send_tok,
                            name_type,
                            &handle->target_name);
            if(contact_string != NULL)
            {
                globus_free(contact_string);
            }
            if(maj_stat != GSS_S_COMPLETE)
            {
                res = GlobusXIOGssapiFTPGSIAuthFailure(maj_stat, min_stat);
                goto err;
            }

            token_ptr = GSS_C_NO_BUFFER;

            break;

        case GSSAPI_FTP_STATE_CLIENT_SENDING_ADAT:
            /* base64 decode the reply */
            length = globus_libc_strlen(buffer);
                                                                                
            radix_buf = globus_libc_malloc((length + 1) * 6 / 8 + 1);
                                                                                
            if(radix_buf == GLOBUS_NULL)
            {
                res = GlobusXIOGssapiFTPAllocError();
                goto err;
            }
                                                                                
            res = globus_l_xio_gssapi_ftp_radix_decode(
                    (const globus_byte_t *) buffer,
                    radix_buf,
                    &length);
            if(res != GLOBUS_SUCCESS)
            {
                globus_libc_free(radix_buf);
                goto err;
            }

            recv_tok.value = radix_buf;
            recv_tok.length = length;
            token_ptr = &recv_tok;
            break;

        default:
            globus_assert(0);
            break;
    }

    req_flags |= GSS_C_MUTUAL_FLAG;
    req_flags |= GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG|GSS_C_DELEG_FLAG;
    if(handle->encrypt)
    {
        req_flags |= GSS_C_CONF_FLAG;
    }

    maj_stat = gss_init_sec_context(
        &min_stat,
        handle->cred_handle,
        &handle->gssapi_context,
        handle->target_name,
        GSS_C_NULL_OID,
        req_flags,
        0,
        NULL,
        token_ptr,
        NULL,
        &send_tok,
        NULL,
        NULL);
    *complete = GLOBUS_FALSE;
    *out_buffer = NULL;
    switch(maj_stat)
    {
        case GSS_S_COMPLETE:
            *complete = GLOBUS_TRUE;

        case GSS_S_CONTINUE_NEEDED:
            if(send_tok.length != 0)
            {
                radix_buf = globus_libc_malloc(send_tok.length * 8 / 6 + 11);
                if(radix_buf == NULL)
                {
                    res = GlobusXIOGssapiFTPAllocError();
                    goto err;
                }

                memcpy(radix_buf, "ADAT ", 5);
                length = send_tok.length;
                res = globus_l_xio_gssapi_ftp_radix_encode(
                        send_tok.value, 
                        send_tok.length, 
                        &radix_buf[5],
                        &length);
                if(res != GLOBUS_SUCCESS)
                {
                    globus_free(radix_buf);
                    goto err;
                }
                radix_buf[length+5] = '\r';
                radix_buf[length+6] = '\n';
                radix_buf[length+7] = '\0';

                *out_buffer = (char *) radix_buf;
            }

            break; 

        default:
            res = GlobusXIOGssapiFTPGSIAuthFailure(maj_stat, min_stat);
            goto err;
    }
    gss_release_buffer(&min_stat, &send_tok);
    gss_release_buffer(&min_stat, token_ptr);

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusXIOGssapiftpDebugExitWithError();
    return res;
}

static void
globus_l_xio_gssapi_ftp_preauth_client_read_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_bool_t                       done = GLOBUS_FALSE;
    globus_bool_t                       complete;
    globus_result_t                     res = GLOBUS_SUCCESS;
    char *                              send_buffer;
    char *                              tmp_buf;
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    char **                             cmd_a = NULL;
    globus_bool_t                       finish = GLOBUS_FALSE;
    globus_byte_t *                     in_buffer = NULL;
    GlobusXIOName(globus_l_xio_gssapi_ftp_preauth_client_read_cb);

    GlobusXIOGssapiftpDebugEnter();

    handle = (globus_l_xio_gssapi_ftp_handle_t *) user_arg;

    if(result != GLOBUS_SUCCESS)
    {
        goto error_param;
    }

    in_buffer = handle->auth_read_iov.iov_base;
    globus_mutex_lock(&handle->mutex);
    {
        globus_assert(handle->client);

        res = globus_l_xio_gssapi_ftp_parse_command(
                handle->auth_read_iov.iov_base,
                handle->auth_read_iov.iov_len,
                GLOBUS_TRUE,
                &cmd_a);
        if(res != GLOBUS_SUCCESS || cmd_a == NULL)
        {
            res = GlobusXIOGssapiFTPAllocError();
            goto error_parse;
        }

        switch(handle->state)
        {
            case GSSAPI_FTP_STATE_CLIENT_READING_220:
                /* if we did not get a 220 from the server finsh the open
                    with an error */
                if(strcmp(cmd_a[0], "220") != 0)
                {
                    res = GlobusXIOGssapiFTPAuthenticationFailure(
                        "Expected 220");
                    goto err;
                }
                else
                {
                    GlobusXIOGssapiftpDebugChangeState(handle,
                        GSSAPI_FTP_STATE_CLIENT_SENDING_AUTH);
                    send_buffer = 
                        globus_libc_strdup(CLIENT_AUTH_GSSAPI_COMMAND);
                    handle->banner = handle->auth_read_iov.iov_base;
                    handle->banner_length = handle->auth_read_iov.iov_len;
                    in_buffer = NULL;
                }
                break;

            case GSSAPI_FTP_STATE_CLIENT_SENDING_AUTH:
                if(strcmp(cmd_a[0], "334") != 0)
                {
                    res = GlobusXIOGssapiFTPAuthenticationFailure(
                        "Expected 334");
                    goto err;
                }
                else
                {
                    GlobusXIOGssapiftpDebugChangeState(handle,
                        GSSAPI_FTP_STATE_CLIENT_ADAT_INIT);

                    handle->op = op;
                    res = globus_l_xio_gssapi_ftp_client_adat(
                        handle,
                        NULL,
                        &send_buffer,
                        &complete);
                    if(res != GLOBUS_SUCCESS)
                    {
                        goto err;
                    }
                    if(send_buffer == NULL)
                    {
                        res = GlobusXIOGssapiFTPAuthenticationFailure(
                            "Client should have adat buffer to send");
                        goto err;
                    }
                    globus_assert(complete == GLOBUS_FALSE);
                }
                break;

            /* change state and fall through */
            case GSSAPI_FTP_STATE_CLIENT_ADAT_INIT:
                GlobusXIOGssapiftpDebugChangeState(handle,
                    GSSAPI_FTP_STATE_CLIENT_SENDING_ADAT);

            case GSSAPI_FTP_STATE_CLIENT_SENDING_ADAT:
                /* if completed successfully */
                if(*cmd_a[0] == '2')
                {
                    if(strncmp(cmd_a[1], "ADAT=", 5) == 0)
                    {
                        tmp_buf = cmd_a[1] + sizeof("ADAT=") - 1;
                        res = globus_l_xio_gssapi_ftp_client_adat(
                            handle,
                            tmp_buf,
                            &send_buffer,
                            &complete);
                        if(res != GLOBUS_SUCCESS)
                        {
                            goto err;
                        }
                        if(!complete || send_buffer != NULL)
                        {
                            res = GlobusXIOGssapiFTPAuthenticationFailure(
                                "Client should have adat buffer to send");
                            goto err;
                        }
                    }
                    GlobusXIOGssapiftpDebugChangeState(handle,
                        GSSAPI_FTP_STATE_OPEN);
                    done = GLOBUS_TRUE;
                    finish = GLOBUS_TRUE;
                }
                /* if we still need to send more adats, but all is well */
                else if(*cmd_a[0] == '3')
                {
                    tmp_buf = cmd_a[1] + sizeof("ADAT=") - 1;
                    res = globus_l_xio_gssapi_ftp_client_adat(
                        handle,
                        tmp_buf,
                        &send_buffer,
                        &complete);
                    if(res != GLOBUS_SUCCESS)
                    {
                        goto err;
                    }
                    if(send_buffer == NULL)
                    {
                        res = GlobusXIOGssapiFTPAuthenticationFailure(
                            handle->read_iov[0].iov_base);
                        goto err;
                    }
                }
                /* if an error occurred */
                else
                {
                    ((char *)handle->auth_read_iov.iov_base)
                        [handle->auth_read_iov.iov_len-1] = '\0';
                    res = GlobusXIOGssapiFTPAuthenticationFailure(
                        handle->auth_read_iov.iov_base);
                    goto err;
                }
                break;

            default:
                globus_assert(0 && "Client read in a bad state");
                break;
        }

        if(!done)
        {
            handle->auth_write_iov.iov_base = send_buffer;
            handle->auth_write_iov.iov_len = globus_libc_strlen(send_buffer);
            GlobusXIOGssapiftpDebugPassWrite();
            res = globus_xio_driver_pass_write(
                op,
                &handle->auth_write_iov,
                1,
                handle->auth_write_iov.iov_len,
                globus_l_xio_gssapi_ftp_client_preauth_client_write_cb,
                handle);
            if(res != GLOBUS_SUCCESS)
            {
                goto err;
            }
        }
        globus_l_xio_gssapi_ftp_free_cmd_a(cmd_a);
    }
    globus_mutex_unlock(&handle->mutex);
    if(in_buffer != NULL)
    {
        globus_free(in_buffer);
    }

    if(finish)
    {
        globus_xio_driver_finished_open(handle, op, res);
    }

    GlobusXIOGssapiftpDebugExit();
    return;

  err:
    if(cmd_a != NULL)
    {
        globus_l_xio_gssapi_ftp_free_cmd_a(cmd_a);
    }
error_parse:
    globus_mutex_unlock(&handle->mutex);
error_param:
    if(in_buffer != NULL)
    {
        globus_free(in_buffer);
    }
    globus_xio_driver_finished_open(handle, op, res);
    GlobusXIOGssapiftpDebugExitWithError();
}

static globus_result_t
globus_l_xio_gssapi_ftp_accept(
    void *                              driver_server,
    globus_xio_operation_t              accept_op)
{
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_gssapi_ftp_accept);

    GlobusXIOGssapiftpDebugEnter();

    res = globus_xio_driver_pass_accept(accept_op, 
        globus_l_xio_gssapi_ftp_accept_cb, NULL);

    GlobusXIOGssapiftpDebugExit();
    return res;
}

/************************************************************************
 *                  attr handling
 *                  ---------------
 ***********************************************************************/
static globus_result_t
globus_l_xio_gssapi_ftp_attr_init(
    void **                             out_attr)
{
    globus_l_xio_gssapi_attr_t *        attr;
    GlobusXIOName(globus_l_xio_gssapi_ftp_attr_init);

    GlobusXIOGssapiftpDebugEnter();

    attr = (globus_l_xio_gssapi_attr_t *) 
        globus_calloc(1, sizeof(globus_l_xio_gssapi_attr_t));
    if(attr == NULL)
    {
	    goto err;
    }
    attr->subject = NULL;
    attr->start_state = GSSAPI_FTP_STATE_NONE;

    *out_attr = attr;

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;

err:

    GlobusXIOGssapiftpDebugExitWithError();
    return GlobusXIOGssapiFTPAllocError();
}

static globus_result_t
globus_l_xio_gssapi_ftp_attr_cntl(
    void *                              driver_attr,
    int                                 cmd,
    va_list                             ap)
{
    globus_l_xio_gssapi_attr_t *        attr;
    char *                              subject;
    GlobusXIOName(globus_l_xio_gssapi_ftp_attr_cntl);

    GlobusXIOGssapiftpDebugEnter();

    attr = (globus_l_xio_gssapi_attr_t *) driver_attr;

    switch(cmd)
    {
        case GLOBUS_XIO_GSSAPI_ATTR_TYPE_SUBJECT:
            subject = va_arg(ap, char *);
            if(subject != NULL)
            {
                if(attr->subject != NULL)
                {
                    globus_free(attr->subject);
                }
                attr->subject = globus_libc_strdup(subject);
            }
            break;

        case GLOBUS_XIO_GSSAPI_ATTR_TYPE_START_STATE:
            attr->start_state = va_arg(ap, int);
            break;

        case GLOBUS_XIO_GSSAPI_ATTR_TYPE_ENCRYPT:
            attr->encrypt = va_arg(ap, int);
            break;

        case GLOBUS_XIO_GSSAPI_ATTR_TYPE_FORCE_SERVER:
            attr->force_server = va_arg(ap, globus_bool_t);
            break;

        case GLOBUS_XIO_GSSAPI_ATTR_TYPE_ALLOW_CLEAR:
            attr->allow_clear = va_arg(ap, globus_bool_t);
            break;

        default:
            break;
    }

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;
}

static globus_result_t
globus_l_xio_gssapi_ftp_attr_copy(
    void **                             dst,
    void *                              src)
{
    globus_result_t                     res;
    globus_l_xio_gssapi_attr_t *        src_attr;
    globus_l_xio_gssapi_attr_t *        dst_attr;
    GlobusXIOName(globus_l_xio_gssapi_ftp_attr_copy);

    GlobusXIOGssapiftpDebugEnter();

    src_attr = (globus_l_xio_gssapi_attr_t *) src;
    res = globus_l_xio_gssapi_ftp_attr_init((void **) &dst_attr);
    if(res != GLOBUS_SUCCESS)
    {
	goto err;
    }
    memcpy(dst_attr, src_attr, sizeof(globus_l_xio_gssapi_attr_t));
    if(src_attr->subject != NULL)
    {
        dst_attr->subject = strdup(src_attr->subject);
    }
    *dst = dst_attr;

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;

err:

    GlobusXIOGssapiftpDebugExitWithError();
    return res;
}

static globus_result_t
globus_l_xio_gssapi_ftp_attr_destroy(
    void *                              driver_attr)
{
    globus_l_xio_gssapi_attr_t *        attr;
    GlobusXIOName(globus_l_xio_gssapi_ftp_attr_destroy);

    GlobusXIOGssapiftpDebugEnter();

    attr = (globus_l_xio_gssapi_attr_t *) driver_attr;
    if(attr->subject != NULL)
    {
        globus_free(attr->subject);
    }
    globus_free(attr);

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;
}

/************************************************************************
 *                    io handlers
 *                    -----------
 ***********************************************************************/

static globus_result_t
globus_l_xio_gssapi_ftp_open(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    globus_l_xio_gssapi_attr_t *        attr;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_gssapi_ftp_open);

    GlobusXIOGssapiftpDebugEnter();

    attr = (globus_l_xio_gssapi_attr_t *) driver_attr;

    globus_xio_driver_attr_cntl(
        op, globus_l_gssapi_telnet_driver, 
        GLOBUS_XIO_TELNET_BUFFER, GLOBUS_TRUE);

    /*
     *  create a new handle and initialize it 
     */
    handle = globus_l_xio_gssapi_ftp_handle_create();
    if(handle == NULL)
    {
        res = GlobusXIOGssapiFTPAllocError();
        goto err;
    }

    if(attr != NULL && attr->force_server)
    {
        handle->client = GLOBUS_FALSE;
        globus_xio_driver_attr_cntl(
            op, globus_l_gssapi_telnet_driver, 
            GLOBUS_XIO_TELNET_FORCE_SERVER, GLOBUS_TRUE);
    }
    else
    {
        handle->client = driver_link ? GLOBUS_FALSE : GLOBUS_TRUE;
    }

    if(attr != NULL)
    {
        if(attr->subject != NULL)
        {
            handle->subject = strdup(attr->subject);
        }
        handle->encrypt = attr->encrypt;
        handle->allow_clear = attr->allow_clear;
    }

    /* do client protocol */
    if(handle->client)
    {
        handle->host = globus_libc_strdup(contact_info->host);
        GlobusXIOGssapiftpDebugChangeState(handle,
            GSSAPI_FTP_STATE_CLIENT_READING_220);
        handle->cred_handle = GSS_C_NO_CREDENTIAL;
        res = globus_xio_driver_pass_open(
            op, contact_info, globus_l_xio_gssapi_ftp_client_open_cb, handle);
    }
    /* do server protocol */
    else
    {
        GlobusXIOGssapiftpDebugChangeState(handle,
            GSSAPI_FTP_STATE_SERVER_READING_AUTH);
        res = globus_xio_driver_pass_open(
            op, contact_info, globus_l_xio_gssapi_ftp_server_open_cb, handle);
    }
    if(res != GLOBUS_SUCCESS)
    {
        globus_l_xio_gssapi_ftp_handle_destroy(handle);
        goto err;
    }

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusXIOGssapiftpDebugExitWithError();
    return res;
}

static globus_result_t
globus_l_xio_gssapi_ftp_close(
    void *                              driver_specific_handle,
    void *                              attr,
    globus_xio_operation_t              op)
{
    globus_result_t                     res;
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    GlobusXIOName(globus_l_xio_gssapi_ftp_close);

    GlobusXIOGssapiftpDebugEnter();

    handle = (globus_l_xio_gssapi_ftp_handle_t *) driver_specific_handle;

    res = globus_xio_driver_pass_close(op, NULL, NULL);
    globus_l_xio_gssapi_ftp_handle_destroy(handle);

    GlobusXIOGssapiftpDebugExit();
    return res;
}

/************************************************************************
 *                  write functions
 *                  ---------------
 *  
 *  This section has function that handle writes
 ***********************************************************************/

static void
globus_l_xio_gssapi_ftp_write_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    int                                 i;
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    GlobusXIOName(globus_l_xio_gssapi_ftp_write_cb);

    GlobusXIOGssapiftpDebugEnter();

    /* change state back and free stuff */

    handle = (globus_l_xio_gssapi_ftp_handle_t *) user_arg;

    for(i = 0; i < handle->write_iov_count; i++)
    {
        globus_free(handle->write_iov[i].iov_base);
    }
    globus_free(handle->write_iov);
    globus_mutex_lock(&handle->mutex);
    {
        /*globus_free(handle->auth_write_iov.iov_base); */
        handle->write_posted = GLOBUS_FALSE;
    }
    globus_mutex_unlock(&handle->mutex);
    globus_xio_driver_finished_write(op, result, handle->orig_write_len);

    GlobusXIOGssapiftpDebugExit();
}

static void
globus_l_xio_gssapi_ftp_user_server_write_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    GlobusXIOName(globus_l_xio_gssapi_ftp_user_server_write_cb);

    handle = (globus_l_xio_gssapi_ftp_handle_t *) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        globus_free(handle->write_iov);
        globus_free(handle->auth_write_iov.iov_base);
        handle->write_posted = GLOBUS_FALSE;
    }
    globus_mutex_unlock(&handle->mutex);

    globus_xio_driver_finished_write(op, result, handle->orig_write_len);
}

static globus_result_t
globus_l_xio_gssapi_ftp_write(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_result_t                     res;
    globus_size_t                       length;
    globus_size_t                       len;
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    globus_xio_driver_data_callback_t   cb;
    globus_byte_t *                     out_buf;
    globus_byte_t *                     next_ptr;
    globus_byte_t *                     tmp_ptr;
    int                                 tmp_i;
    int                                 tmp_i2;
    globus_xio_iovec_t *                l_iov;
    int                                 l_iov_ndx = 1;
    int                                 m_iovc = 16;
    GlobusXIOName(globus_l_xio_gssapi_ftp_write);


    GlobusXIOGssapiftpDebugEnter();

    handle = (globus_l_xio_gssapi_ftp_handle_t *) driver_specific_handle;

    globus_mutex_lock(&handle->mutex);
    {
        if(handle->write_posted)
        {
            globus_mutex_unlock(&handle->mutex);
            res = GlobusXIOGssapiFTPOutstandingOp();
            goto err;
        }

        /* serialize into the write buffer */
        /* TODO: make this not needed */
        GlobusXIOUtilIovTotalLength(length, iovec, iovec_count);
        handle->write_buffer = globus_malloc(length + 1);
        GlobusXIOUtilIovSerialize(handle->write_buffer, iovec, iovec_count);
        handle->write_buffer[length] = '\0';
        handle->orig_write_len = length;

        /* for now insist that they use it correctly */
        if(handle->write_buffer[length-1] != '\n' ||
            handle->write_buffer[length-2] != '\r')
        {
            globus_mutex_unlock(&handle->mutex);
            res = GlobusXIOGssapiFTPOutstandingOp();
            goto err;
        }

        l_iov = (globus_xio_iovec_t *)
            calloc(sizeof(globus_xio_iovec_t), m_iovc);
        /* deconstipation */
        if(handle->client)
        {
            res = globus_l_xio_gssapi_ftp_wrap(
                    handle, handle->write_buffer, length, 
                    &l_iov[0].iov_base,
                    &l_iov[0].iov_len,
                    handle->client);
            globus_free(handle->write_buffer);
            length = l_iov[0].iov_len;
            if(res != GLOBUS_SUCCESS)
            {
                globus_mutex_unlock(&handle->mutex);
                goto error_free_vec;
            }

            cb = globus_l_xio_gssapi_ftp_write_cb;
        }
        else
        {
            /* if the server is not yet open but can write unwrapped stuff
            really just a special case for 220 message */
            if(handle->state != GSSAPI_FTP_STATE_OPEN)
            {
                handle->auth_write_iov.iov_len = length;
                handle->auth_write_iov.iov_base = handle->write_buffer;
                l_iov[0].iov_base = handle->auth_write_iov.iov_base;
                l_iov[0].iov_len = handle->auth_write_iov.iov_len;
                cb = globus_l_xio_gssapi_ftp_user_server_write_cb;
            }
            /* check multiline replies */
            else
            {
                globus_bool_t           first = GLOBUS_TRUE;
                globus_size_t           total_len = 0;

                l_iov_ndx = 0;
                out_buf = NULL;
                tmp_i = 3;
                tmp_ptr = handle->write_buffer;
                while(tmp_ptr - handle->write_buffer < length)
                {
                    if(!first)
                    {
                        out_buf[3] = '-';
                        tmp_i += tmp_i2;
                    }
                    next_ptr = (globus_byte_t *)
                        strstr((const char *) tmp_ptr, "\r\n");
                    len = next_ptr - tmp_ptr + 2;

                    res = globus_l_xio_gssapi_ftp_wrap(
                        handle, tmp_ptr, len,
                        &l_iov[l_iov_ndx].iov_base,
                        &l_iov[l_iov_ndx].iov_len,
                        handle->client);
                    tmp_i2 = l_iov[l_iov_ndx].iov_len;
                    total_len += tmp_i2;
                    out_buf = l_iov[l_iov_ndx].iov_base;

                    l_iov_ndx++;
                    if(l_iov_ndx >= m_iovc)
                    {
                        m_iovc *= 2;
                        l_iov = (globus_xio_iovec_t *) globus_realloc(
                            l_iov, sizeof(globus_xio_iovec_t)* m_iovc);
                    }
                    tmp_ptr = next_ptr + 2;
                    first = GLOBUS_FALSE;
                }
                length = total_len;
                globus_free(handle->write_buffer);
                cb = globus_l_xio_gssapi_ftp_write_cb;
            }
        }

        handle->write_iov = l_iov;
        handle->write_iov_count = l_iov_ndx;
        res = globus_xio_driver_pass_write(
            op, 
            l_iov,
            l_iov_ndx,
            length,
            cb,
            handle);
        if(res != GLOBUS_SUCCESS)
        {
            globus_mutex_unlock(&handle->mutex);
            goto error_free_vec;
        }
        handle->write_posted = GLOBUS_TRUE;
    }
    globus_mutex_unlock(&handle->mutex);

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;

error_free_vec:
    globus_free(l_iov);

  err:
    GlobusXIOGssapiftpDebugExitWithError();

    return res;
}

/************************************************************************
 *                  read functions
 *                  --------------
 *  
 *  This section has function that handle writes
 ***********************************************************************/

static void
globus_l_xio_gssapi_ftp_client_read_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    char **                             cmd_a;
    int                                 ctr;
    int                                 ndx;
    int                                 tmp_i;
    globus_byte_t *                     out_buffer = NULL;
    globus_byte_t *                     in_buffer = NULL;
    globus_size_t                       out_length;
    char *                              send_buffer;
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_gssapi_ftp_client_read_cb);

    GlobusXIOGssapiftpDebugEnter();

    handle = (globus_l_xio_gssapi_ftp_handle_t *) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        globus_assert(handle->state == GSSAPI_FTP_STATE_OPEN);

        handle->read_posted = GLOBUS_FALSE;
        if(result != GLOBUS_SUCCESS)
        {
            res = result;
            goto err;
        }

        in_buffer = handle->read_iov[0].iov_base;
        send_buffer = (char *) handle->read_iov[0].iov_base;
        if(send_buffer[0] == '6')
        {
           res = globus_l_xio_gssapi_ftp_parse_command(
                handle->read_iov[0].iov_base,
                nbytes,
                GLOBUS_TRUE,
                &cmd_a);
            if(res != GLOBUS_SUCCESS || cmd_a == NULL)
            {
                res = GlobusXIOGssapiFTPAllocError();
                goto err;
            }
            ndx = 0;
            out_length = 0;
            for(ctr = 1; cmd_a[ctr] != NULL; ctr++)
            {
                res = globus_l_xio_gssapi_ftp_unwrap(
                        handle,
                        cmd_a[ctr],
                        strlen(cmd_a[ctr]),
                        &send_buffer);
                if(res != GLOBUS_SUCCESS)
                {
                    goto err_unwrap;
                }
                tmp_i = strlen(send_buffer);
                out_length += tmp_i;
                out_buffer = globus_libc_realloc(out_buffer, out_length + 1);
                memcpy(&out_buffer[ndx], send_buffer, tmp_i);
                ndx += tmp_i;
                globus_free(send_buffer);
            }
            handle->read_iov[0].iov_base = out_buffer;
            handle->read_iov[0].iov_len = out_length;
            globus_l_xio_gssapi_ftp_free_cmd_a(cmd_a);
        }
        /* XXX: should this be an error */
        else
        {
            out_length = nbytes;
        }
    }
    globus_mutex_unlock(&handle->mutex);
    globus_free(in_buffer);

    globus_xio_driver_finished_read(op, GLOBUS_SUCCESS, out_length);

    return;

err_unwrap:
    globus_l_xio_gssapi_ftp_free_cmd_a(cmd_a);

 err:
    if(in_buffer != NULL)
    {
        globus_free(in_buffer);
    }
    globus_mutex_unlock(&handle->mutex);
    globus_xio_driver_finished_read(op, res, 0);

    return;
}

static globus_result_t
globus_l_xio_gssapi_ftp_read(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_size_t                       finished_len = -1;
    globus_bool_t                       finished = GLOBUS_FALSE;
    globus_l_xio_gssapi_ftp_handle_t *  handle;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_gssapi_ftp_read);

    GlobusXIOGssapiftpDebugEnter();

    handle = (globus_l_xio_gssapi_ftp_handle_t *) driver_specific_handle;

    globus_mutex_lock(&handle->mutex);
    {
        if(handle->read_posted)
        {
            res = GlobusXIOGssapiFTPOutstandingOp();
            goto err;
        }

        /* should serialize */
        /* completely de const'ipating here */
        handle->read_iov = (globus_xio_iovec_t *) iovec; 
        if(handle->client)
        {
            if(handle->banner != NULL)
            {
                handle->read_iov->iov_base = handle->banner;
                handle->read_iov->iov_len = handle->banner_length;
                finished_len = handle->banner_length;
                finished = GLOBUS_TRUE;
                handle->banner = NULL;
            }
            else
            {
                GlobusXIOGssapiftpDebugPassRead();
                res = globus_xio_driver_pass_read(
                    op,
                    handle->read_iov,
                    1,
                    1,
                    globus_l_xio_gssapi_ftp_client_read_cb,
                    handle);
                if(res != GLOBUS_SUCCESS)
                {
                    goto err;
                }
                handle->read_posted = GLOBUS_TRUE;
            }
        }
        else
        {
            GlobusXIOGssapiftpDebugPassRead();
            res = globus_xio_driver_pass_read(
                op,
                &handle->auth_read_iov,
                1,
                1,
                globus_l_xio_gssapi_ftp_server_read_cb, 
                handle);
            if(res != GLOBUS_SUCCESS)
            {
                goto err;
            }
            handle->read_posted = GLOBUS_TRUE;
        }
    }	
    globus_mutex_unlock(&handle->mutex);

    if(finished)
    {
        globus_xio_driver_finished_read(op, GLOBUS_SUCCESS, finished_len);
    }
    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;

err:

    globus_mutex_unlock(&handle->mutex);
    GlobusXIOGssapiftpDebugExitWithError();
    return res;
}

static globus_result_t
globus_l_xio_gssapi_ftp_handle_cntl(
    void *                              handle,
    int                                 cmd,
    va_list                             ap)
{
    gss_ctx_id_t *                      out_context;
    char **                             out_subject;
    int *                               out_type;
    gss_cred_id_t *                     out_cred;
    gss_cred_id_t *                     out_del_cred;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_l_xio_gssapi_ftp_handle_t *  ds_handle;
    GlobusXIOName(globus_l_xio_gssapi_ftp_handle_cntl);

    GlobusXIOGssapiftpDebugEnter();

    ds_handle = (globus_l_xio_gssapi_ftp_handle_t *) handle;

    globus_mutex_lock(&ds_handle->mutex);
    {
        switch(cmd)
        {
            case GLOBUS_XIO_DRIVER_GSSAPI_FTP_GET_AUTH:
                out_type = va_arg(ap, int *);
                out_context = va_arg(ap, gss_ctx_id_t *);
                out_cred = va_arg(ap, gss_cred_id_t *);
                out_del_cred = va_arg(ap, gss_cred_id_t *);
                out_subject = va_arg(ap, char **);

                switch(ds_handle->state)
                {
                    case GSSAPI_FTP_STATE_OPEN:
                        *out_type = GLOBUS_XIO_GSSAPI_FTP_SECURE;
                        break;
                    case GSSAPI_FTP_STATE_OPEN_CLEAR:
                        *out_type = GLOBUS_XIO_GSSAPI_FTP_CLEAR;
                        break;
                    default:
                        *out_type = GLOBUS_XIO_GSSAPI_FTP_NONE;
                        break;
                }

                *out_context = ds_handle->gssapi_context;
                *out_cred = ds_handle->cred_handle;
                *out_del_cred = ds_handle->delegated_cred_handle;
                *out_subject = ds_handle->auth_gssapi_subject;

                ds_handle->delegated_cred_handle = GSS_C_NO_CREDENTIAL;
                break;

            default:
                res = GlobusXIOGssapiBadParameter();
                goto error;
                break;
        }
    }
    globus_mutex_unlock(&ds_handle->mutex);

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;

error:
    globus_mutex_unlock(&ds_handle->mutex);
    return res;
}

/************************************************************************
 *                  load and activate
 *                  -----------------
 *  
 *  This section has function that handle writes
 ***********************************************************************/
static globus_result_t
globus_l_xio_gssapi_ftp_init(
    globus_xio_driver_t *               out_driver)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     res;
    GlobusXIOName(globus_l_xio_gssapi_ftp_init);

    GlobusXIOGssapiftpDebugEnter();

    res = globus_xio_driver_init(&driver, "gssapi_ftp", NULL);
    if(res != GLOBUS_SUCCESS)
    {
        return res;
    }

    globus_xio_driver_set_transform(
        driver,
        globus_l_xio_gssapi_ftp_open,
        globus_l_xio_gssapi_ftp_close,
        globus_l_xio_gssapi_ftp_read,
        globus_l_xio_gssapi_ftp_write,
        globus_l_xio_gssapi_ftp_handle_cntl,
        globus_l_xio_gssapi_ftp_push_driver);

    globus_xio_driver_set_attr(
        driver,
        globus_l_xio_gssapi_ftp_attr_init,
        globus_l_xio_gssapi_ftp_attr_copy,
        globus_l_xio_gssapi_ftp_attr_cntl,
        globus_l_xio_gssapi_ftp_attr_destroy);

    globus_xio_driver_set_server(
        driver,
        NULL,
        globus_l_xio_gssapi_ftp_accept,
        NULL,
        NULL,
        NULL,
        NULL);

    *out_driver = driver;

    GlobusXIOGssapiftpDebugExit();
    return GLOBUS_SUCCESS;
}



static void
globus_l_xio_gssapi_ftp_destroy(
    globus_xio_driver_t                 driver)
{
    GlobusXIOName(globus_l_xio_gssapi_ftp_destroy);

    GlobusXIOGssapiftpDebugEnter();

    globus_xio_driver_destroy(driver);

    GlobusXIOGssapiftpDebugExit();
}

GlobusXIODefineDriver(
    gssapi_ftp,
    globus_l_xio_gssapi_ftp_init,
    globus_l_xio_gssapi_ftp_destroy);

static int
globus_l_xio_gssapi_ftp_activate(void)
{
    globus_result_t                     res;
    int                                 rc;
    GlobusXIOName(globus_l_xio_gssapi_ftp_activate);
    GlobusDebugInit(GLOBUS_XIO_GSSAPI_FTP, TRACE);

    GlobusXIOGssapiftpDebugEnter();

    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    rc = globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    globus_module_activate(GLOBUS_GSI_OPENSSL_ERROR_MODULE);
    
    res = globus_xio_driver_load("telnet", &globus_l_gssapi_telnet_driver);
    if(res != GLOBUS_SUCCESS)
    {
        return GLOBUS_FAILURE;
    }
    
    GlobusXIORegisterDriver(gssapi_ftp);
    GlobusXIOGssapiftpDebugExit();
    return rc;
}

static int
globus_l_xio_gssapi_ftp_deactivate(void)
{
    GlobusXIOName(globus_l_xio_gssapi_ftp_deactivate);

    GlobusXIOGssapiftpDebugEnter();
    GlobusXIOUnRegisterDriver(gssapi_ftp);
    globus_module_deactivate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    globus_module_deactivate(GLOBUS_GSI_OPENSSL_ERROR_MODULE);
    globus_xio_driver_unload(globus_l_gssapi_telnet_driver);

    GlobusXIOGssapiftpDebugExit();
    return globus_module_deactivate(GLOBUS_XIO_MODULE);
}
