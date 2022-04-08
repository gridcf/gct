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


/*
 *  This multiplexes buffer across many gridftp client library handles,
 *  and passes that buffer down the stack.  The main (and likely only)
 *  use is for multicast broad casts in the gridftp server.
 *
 *  If an error occurs on any one gridftp handle it is not imediately
 *  reported to the user.  It is reecorded and no futher buffers will be 
 *  passed down that connection.  Upon closing the handle all errors 
 *  are reported.
 *
 *  However, if a pass error occurs it is reported immediately and all
 *  active ftp connections are terminated. 
 *
 *  The user is not required to actually pass data down the stack.  In
 *  some cases they will want buffers to hop around servers in a network
 *  overlay, but not actually write them to disk.  In this case, errors
 *  are reported at close OR when there are no connects left to forward
 *  the buffer to.
 *
 */
#include "globus_xio_driver.h"
#include "globus_xio_gridftp_multicast_driver.h"
#include "globus_ftp_client.h"  
#include "version.h"

GlobusDebugDefine(GLOBUS_XIO_GRIDFTP_MULTICAST);
GlobusXIODeclareDriver(gridftp_multicast);

#define GlobusXIOGridftpMulticastDebugPrintf(level, message)                  \
    GlobusDebugPrintf(GLOBUS_XIO_GRIDFTP_MULTICAST, level, message)

#define GlobusXIOGridftpMulticastDebugEnter()                                 \
    GlobusXIOGridftpMulticastDebugPrintf(                                     \
        GLOBUS_L_XIO_GRIDFTP_MULTICAST_DEBUG_TRACE,                                         \
        ("[%s] Entering\n", _xio_name))

#define GlobusXIOGridftpMulticastDebugExit()                                  \
    GlobusXIOGridftpMulticastDebugPrintf(                                     \
        GLOBUS_L_XIO_GRIDFTP_MULTICAST_DEBUG_TRACE,                                         \
        ("[%s] Exiting\n", _xio_name))

#define GlobusXIOGridftpMulticastDebugExitWithError()                         \
    GlobusXIOGridftpMulticastDebugPrintf(                                     \
        GLOBUS_L_XIO_GRIDFTP_MULTICAST_DEBUG_TRACE,                                         \
        ("[%s] Exiting with error\n", _xio_name))


#define GlobusXIOGMCNoOpError(_r)                                           \
    globus_error_put(GlobusXIOGMCNoOpErrorObj(_r))

#define GlobusXIOGMCNoOpErrorObj(_reason)                                   \
    globus_error_construct_error(                                           \
        GlobusXIOMyModule(gridftp_multicast),                               \
        NULL,                                                               \
        GLOBUS_XIO_GRIDFTP_MULTICAST_ERROR_NOOP,                            \
        __FILE__,                                                           \
        _xio_name,                                                          \
        __LINE__,                                                           \
        _XIOSL(_reason))                                

#define GMC_ERROR_TOKEN "GMC_ERROR=\n"
#define GMC_URL_ENC_CHAR "#;:=+ ,"

enum xio_l_error_levels
{
    GLOBUS_L_XIO_GRIDFTP_MULTICAST_DEBUG_TRACE          = 1,
    GLOBUS_L_XIO_GRIDFTP_MULTICAST_DEBUG_INTERNAL_TRACE
};

typedef enum xio_l_gmc_state_e
{
    XIO_GMC_STATE_OPENING = 1,
    XIO_GMC_STATE_OPEN,
    XIO_GMC_STATE_OPENING_ERROR,
    XIO_GMC_STATE_CLOSING
} xio_l_gmc_state_t;

typedef struct xio_l_gridftp_multicast_attr_s
{
    globus_fifo_t                       url_q;
    int                                 P;
    globus_size_t                       tcp_bs;
    int                                 cast_count;
    globus_bool_t                       pass_write;
    char *                              subject;
} xio_l_gridftp_multicast_attr_t;

typedef struct
{
    globus_ftp_client_handleattr_t      handle_attr;
    globus_ftp_client_operationattr_t   op_attr;
    char *                              stack_str;
    globus_ftp_client_handle_t          client_h;
    globus_bool_t                       closed;
    globus_bool_t                       closing;
    char *                              url;
    char *                              str_opts;
    int                                 ndx;
    globus_fifo_t                       url_q;
    globus_result_t                     result;
    struct xio_l_gridftp_multicast_handle_s *   whos_my_daddy;
    globus_byte_t                       mt_buf[1];
} xio_l_gmc_ftp_handle_t;

typedef struct xio_l_gridftp_multicast_handle_s
{
    globus_mutex_t                      mutex;
    xio_l_gmc_ftp_handle_t *            ftp_handles;
    globus_xio_operation_t              open_op;
    globus_xio_operation_t              write_op;
    globus_xio_operation_t              close_op;
    int                                 op_count;
    int                                 write_op_count;
    int                                 ftps;
    int                                 ftps_total;
    globus_bool_t                       pass_write;
    xio_l_gmc_state_t                   state;
    globus_off_t                        offset;
    char *                              local_url;
    int                                 P;
    int                                 tcp_bs;
    globus_size_t                       nbytes;
    globus_result_t                     result;
} xio_l_gridftp_multicast_handle_t;

static
int
xio_l_gridftp_multicast_activate(void);

static
int
xio_l_gridftp_multicast_deactivate(void);

GlobusXIODefineModule(gridftp_multicast) =
{
    "globus_xio_gridftp_multicast",
    xio_l_gridftp_multicast_activate,
    xio_l_gridftp_multicast_deactivate,
    NULL,
    NULL,
    &local_version
};

static xio_l_gridftp_multicast_attr_t   xio_l_gmc_default_attr;

static
globus_result_t
xio_l_gmc_setup_forwarder(
    xio_l_gmc_ftp_handle_t *            ftp_handle,
    gss_cred_id_t                       cred,
    char *                              sbj,
    char *                              username,
    char *                              pw,
    xio_l_gridftp_multicast_attr_t *    attr,
    globus_fifo_t *                     url_q,
    int                                 max_str_len,
    int                                 each_cast_count);

static
void
xio_l_gmc_destroy_forwarder(
    xio_l_gmc_ftp_handle_t *            ftp_handle);

static
globus_bool_t
xio_l_gmc_anyone_alive(
    xio_l_gridftp_multicast_handle_t *  handle)
{
    int                                 i;
    int                                 open_count = 0;

    for(i = 0; i < handle->ftps_total; i++)
    {
        if(handle->ftp_handles[i].result == GLOBUS_SUCCESS)
        {
            open_count++;
        }
    }
    if(handle->result == GLOBUS_SUCCESS)
    {
        open_count++;
    }

    return open_count;
}

static
void
xio_l_gmc_handle_destroy(
    xio_l_gridftp_multicast_handle_t *  handle)
{
    int                                 i;
    xio_l_gmc_ftp_handle_t *            ftp_handle;
    globus_object_t *                   err_obj;
    char *                              str;

    for(i = 0; i < handle->ftps_total; i++)
    {
        ftp_handle = &handle->ftp_handles[i];

        if(ftp_handle->result != GLOBUS_SUCCESS)
        {
            err_obj = globus_error_get(ftp_handle->result);
            globus_object_free(err_obj);
        }
        while(!globus_fifo_empty(&ftp_handle->url_q))
        {
            str = (char *) globus_fifo_dequeue(&ftp_handle->url_q);
            free(str);
        }
        globus_fifo_destroy(&ftp_handle->url_q);

        globus_free(ftp_handle->url);

        if(ftp_handle->stack_str != NULL)
        {
            globus_free(ftp_handle->stack_str);
        }

        globus_ftp_client_handleattr_destroy(&ftp_handle->handle_attr);
        globus_ftp_client_operationattr_destroy(&ftp_handle->op_attr);
        /* free up the client lib stuff 
            globus_ftp_client_handle_t          client_h;
        */
    }

    if(handle->result != GLOBUS_SUCCESS)
    {
        err_obj = globus_error_get(handle->result);
        globus_object_free(err_obj);
    }
    if(handle->local_url != NULL)
    {
        globus_free(handle->local_url);
    }

    globus_mutex_destroy(&handle->mutex);
    globus_free(handle->ftp_handles);
    globus_free(handle);
}

static
globus_list_t *
xio_l_gmc_make_ftp_error_list(
    xio_l_gmc_ftp_handle_t *            ftp_handle)
{
    char *                              start_str;
    char *                              end_str;
    char *                              error_url;
    globus_list_t *                     url_error_list = NULL;
    char *                              err_str;
    char *                              tmp_str;
    globus_url_t                        url_info;
    int                                 rc;
    int                                 i;
    globus_object_t *                   err_obj;

    if(ftp_handle->result == GLOBUS_SUCCESS)
    {
        return NULL;
    }

    err_obj = globus_error_peek(ftp_handle->result);
    if(err_obj == NULL)
    {
        goto error;
    }
    err_str = globus_error_print_friendly(err_obj);
    if(err_str == NULL)
    {
        goto error;
    }
    tmp_str = strstr(err_str, GMC_ERROR_TOKEN);
    if(tmp_str == NULL)
    {
        goto error;
    }
    /* just parse out the ones that failed if anything in parsing fails,
        then we fail everything */
    tmp_str += sizeof(GMC_ERROR_TOKEN);
    start_str = tmp_str;
    while(*start_str != '\0')
    {
        end_str = strstr(start_str, "\n");
        if(end_str == NULL)
        {
            end_str = start_str + strlen(start_str);
        }
        else
        {
            *end_str = '\0';
            end_str += 1;
        }

        rc = globus_url_parse(start_str, &url_info);
        if(rc != GLOBUS_URL_SUCCESS)
        {
            goto error;
        }

        /* we only allow the 2 types of urls */
        if(url_info.scheme_type != GLOBUS_URL_SCHEME_FTP &&
            url_info.scheme_type != GLOBUS_URL_SCHEME_GSIFTP)
        {
            goto error;
        }

        error_url = strdup(start_str);
        globus_list_insert(&url_error_list, error_url);

        start_str = end_str;
    }

    return url_error_list;
error:

    while(!globus_list_empty(url_error_list))
    {
        error_url = (char*)globus_list_remove(&url_error_list,url_error_list);
        free(error_url);
    }

    /* if this token is not found we must assume that this link,
        and all of its children failed */
    for(i = 0; i < globus_fifo_size(&ftp_handle->url_q); i++)
    {
        error_url = (char *) globus_fifo_dequeue(&ftp_handle->url_q);
        globus_fifo_enqueue(&ftp_handle->url_q, error_url);

        globus_list_insert(&url_error_list, strdup(error_url));
    }
    /* gotta include its own url */
    error_url = strdup(ftp_handle->url);
    globus_list_insert(&url_error_list, error_url);

    return url_error_list;
}

static
globus_result_t
xio_l_gmc_get_error(
    xio_l_gridftp_multicast_handle_t *  handle)
{
    char *                              url_str;
    char *                              tmp_err_str;
    char *                              n_ch;
    char *                              err_str;
    int                                 i;
    globus_list_t *                     list;
    globus_list_t *                     tmp_list;
    globus_list_t *                     error_list = NULL;
    globus_object_t *                   err_obj;

    for(i = 0; i < handle->ftps_total; i++)
    {
        list = xio_l_gmc_make_ftp_error_list(&handle->ftp_handles[i]);
        tmp_list = globus_list_concat(error_list, list);
        globus_list_free(list);
        globus_list_free(error_list);

        error_list = tmp_list;
    }

    if(handle->result != GLOBUS_SUCCESS)
    {
        /* it doesnt matter why the error happened, we stick it in
            the list no matter what */

        globus_list_insert(&error_list, handle->local_url);
    }

    /* call this when there are no errors in the close case */
    if(globus_list_empty(error_list))
    {
        return GLOBUS_SUCCESS;
    }

    err_str = globus_libc_strdup("");
    n_ch = "";
    while(!globus_list_empty(error_list))
    {
        url_str = globus_list_remove(&error_list, error_list);
        tmp_err_str = globus_common_create_string(
            "%s%s%s", err_str, n_ch, url_str);

        globus_free(err_str);
        globus_free(url_str);
        err_str = tmp_err_str;
        n_ch = "\n";
    }

    err_obj = globus_error_construct_string(
        NULL,
        NULL,
        "%s%s",
        GMC_ERROR_TOKEN,
        err_str);

    return globus_error_put(err_obj);
}

static
int
xio_l_gridftp_multicast_activate()
{
    int rc;
    GlobusXIOName(xio_l_gridftp_activate);

    GlobusDebugInit(GLOBUS_XIO_GRIDFTP_MULTICAST, TRACE);
    GlobusXIOGridftpMulticastDebugEnter();
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
    GlobusXIORegisterDriver(gridftp_multicast);
    GlobusXIOGridftpMulticastDebugExit();
    return GLOBUS_SUCCESS;

error_ftp_client_activate:
    globus_module_deactivate(GLOBUS_XIO_MODULE);
error_xio_system_activate:
    GlobusXIOGridftpMulticastDebugExitWithError();
    GlobusDebugDestroy(GLOBUS_XIO_GRIDFTP_MULTICAST);
    return rc;
}


static
int
xio_l_gridftp_multicast_deactivate()
{   
    int rc;
    GlobusXIOName(xio_l_gridftp_multicast_deactivate);
    
    GlobusXIOGridftpMulticastDebugEnter();
    GlobusXIOUnRegisterDriver(gridftp_multicast);
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
    GlobusXIOGridftpMulticastDebugExit();
    GlobusDebugDestroy(GLOBUS_XIO_GRIDFTP_MULTICAST);
    return GLOBUS_SUCCESS;

error_deactivate:
    GlobusXIOGridftpMulticastDebugExitWithError();
    GlobusDebugDestroy(GLOBUS_XIO_GRIDFTP_MULTICAST);
    return rc;
}


static
void
xio_l_gmc_put_done(
    void *                              user_arg,
    globus_ftp_client_handle_t *        in_handle,
    globus_object_t *                   err)
{
    int                                 i;
    globus_bool_t                       finish_open = GLOBUS_FALSE;
    globus_bool_t                       finish_close = GLOBUS_FALSE;
    xio_l_gridftp_multicast_handle_t *  handle;
    xio_l_gmc_ftp_handle_t *            ftp_handle;
    globus_result_t                     result;
    GlobusXIOName(xio_l_gmc_put_done);

    ftp_handle = (xio_l_gmc_ftp_handle_t *) user_arg;
    handle = ftp_handle->whos_my_daddy;

    globus_mutex_lock(&handle->mutex);
    {
        /* This is open count */
        handle->op_count--;

        ftp_handle->closed = GLOBUS_TRUE;
        handle->ftps--;
        globus_ftp_client_handle_destroy(&ftp_handle->client_h);
        if(err != NULL)
        {
            result = globus_error_put(globus_object_copy(err));
            ftp_handle->result = result;
            goto error;
        }

        switch(handle->state)
        {
            case XIO_GMC_STATE_OPEN:
                /* this is a premature end?  let the others finish
                    properly if they can */
                /* does this ever happen? */
                globus_assert(0 && "how did this happen");
                break;

            case XIO_GMC_STATE_OPENING_ERROR:
                if(handle->op_count == 0)
                {
                    finish_open = GLOBUS_TRUE;
                }
                break;

            case XIO_GMC_STATE_OPENING:
                if(handle->op_count == 0)
                {
                    finish_open = GLOBUS_TRUE;
                    handle->state = XIO_GMC_STATE_OPEN;
                }
                break;

            case XIO_GMC_STATE_CLOSING:
                if(handle->op_count == 0)
                {
                    finish_close = GLOBUS_TRUE;
                }
                break;
        }

    }
    globus_mutex_unlock(&handle->mutex);

    if(finish_open)
    {
        result = xio_l_gmc_get_error(handle);
        globus_xio_driver_finished_open(handle, handle->open_op, result);

        if(result != GLOBUS_SUCCESS)
        {
            xio_l_gmc_handle_destroy(handle);
        }
    }
    if(finish_close)
    {
        result = xio_l_gmc_get_error(handle);
        globus_xio_driver_finished_close(handle->close_op, result);

        xio_l_gmc_handle_destroy(handle);
    }

    return;

error:

    switch(handle->state)
    {
        case XIO_GMC_STATE_OPEN:
            /* let the others finish if they can */
            break;

        case XIO_GMC_STATE_OPENING:
            handle->state = XIO_GMC_STATE_OPENING_ERROR;
            /* kill every thing that is still open */
            for(i = 0; i < handle->ftps; i++)
            {
                xio_l_gmc_destroy_forwarder(&handle->ftp_handles[i]);
            }
        case XIO_GMC_STATE_OPENING_ERROR:
            /* set the error */

            if(handle->op_count == 0)
            {
                finish_open = GLOBUS_TRUE;
            }
            break;

        case XIO_GMC_STATE_CLOSING:
            if(handle->op_count == 0)
            {
                finish_close = GLOBUS_TRUE;
            }
            break;
    }
    globus_mutex_unlock(&handle->mutex);

    if(finish_open)
    {
        result = xio_l_gmc_get_error(handle);
        globus_xio_driver_finished_open(handle, handle->open_op, result);

        xio_l_gmc_handle_destroy(handle);
    }
    if(finish_close)
    {
        result = xio_l_gmc_get_error(handle);
        globus_xio_driver_finished_close(handle->close_op, result);

        xio_l_gmc_handle_destroy(handle);
    }
}

static
void
xio_l_gridftp_multicast_open_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_bool_t                       finish_open = GLOBUS_FALSE;
    int                                 i;
    xio_l_gridftp_multicast_handle_t *  handle;
    GlobusXIOName(xio_l_gridftp_multicast_open_cb);

    handle = (xio_l_gridftp_multicast_handle_t *) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }

        switch(handle->state)
        {
            case XIO_GMC_STATE_OPENING:
                finish_open = GLOBUS_TRUE;
                handle->state = XIO_GMC_STATE_OPEN;
                result = GLOBUS_SUCCESS;
                break;

            case XIO_GMC_STATE_OPENING_ERROR:
                handle->op_count--;
                if(handle->op_count == 0)
                {
                    finish_open = GLOBUS_TRUE;
                }
                result = xio_l_gmc_get_error(handle);
                break;

            case XIO_GMC_STATE_OPEN:
            case XIO_GMC_STATE_CLOSING:
                globus_assert(0 && "bad state");
        }
    }
    globus_mutex_unlock(&handle->mutex);

    if(finish_open)
    {
        globus_xio_driver_finished_open(handle, handle->open_op, result);

        if(result != GLOBUS_SUCCESS)
        {
            xio_l_gmc_handle_destroy(handle);
        }
    }

    return;

error:
    handle->result = result;

    switch(handle->state)
    {
        case XIO_GMC_STATE_OPENING:
            handle->state = XIO_GMC_STATE_OPENING_ERROR;
            handle->op_count--;
            if(handle->op_count == 0)
            {
                finish_open = GLOBUS_TRUE;
            }
            else
            {
                /* shut down everything that is open */
                for(i = 0; i < handle->ftps; i++)
                {
                    xio_l_gmc_destroy_forwarder(&handle->ftp_handles[i]);
                }
            }

            break;

        case XIO_GMC_STATE_OPENING_ERROR:
            handle->op_count--;
            if(handle->op_count == 0)
            {
                finish_open = GLOBUS_TRUE;
            }
            break;

        case XIO_GMC_STATE_OPEN:
        case XIO_GMC_STATE_CLOSING:
            globus_assert(0 && "bad state");
    }
    globus_mutex_unlock(&handle->mutex);

    if(finish_open)
    {
        globus_xio_driver_finished_open(handle, handle->open_op, result);

        globus_assert(result != GLOBUS_SUCCESS);
        xio_l_gmc_handle_destroy(handle);
    }
}

globus_result_t
xio_l_gridftp_multicast_open(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    int                                 error_cast_count = 0;
    int                                 each_cast_count;
    int                                 total_url_count;
    int                                 cast_count;
    char *                              str;
    int                                 i;
    xio_l_gridftp_multicast_attr_t *    attr;
    xio_l_gridftp_multicast_handle_t *  handle;
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 str_max_len = 0;
    globus_fifo_t                       url_q;
    gss_cred_id_t                       cred = NULL;
    char *                              sbj;
    char *                              username;
    char *                              pw;
    char *                              str_ptr;
    globus_bool_t                       finish_open = GLOBUS_FALSE;
    GlobusXIOName(xio_l_gridftp_multicast_open);

    attr = (xio_l_gridftp_multicast_attr_t *) driver_attr;

    if(attr == NULL)
    {
        /*set attr to some default */
        attr = &xio_l_gmc_default_attr;
    }

    handle = (xio_l_gridftp_multicast_handle_t *) globus_calloc(
        1, sizeof(xio_l_gridftp_multicast_handle_t));
    globus_mutex_init(&handle->mutex, NULL);
    handle->local_url = strdup(contact_info->unparsed);
    handle->P = attr->P;    
    handle->tcp_bs = attr->tcp_bs;
    handle->pass_write = attr->pass_write;

    result = globus_xio_operation_attr_cntl(
        op,
        GLOBUS_XIO_ATTR_GET_CREDENTIAL,
        &cred,
        &sbj,
        &username,
        &pw);
    if(result == GLOBUS_SUCCESS && cred != NULL)
    {
        /* we can just ignore this */
    }

    /* move the list in the attr to a fifo */
    globus_fifo_init(&url_q);
    for(i = 0; i < globus_fifo_size(&attr->url_q); i++)
    {
        str = (char *) globus_fifo_dequeue(&attr->url_q);
        globus_fifo_enqueue(&attr->url_q, str);
        globus_fifo_enqueue(&url_q, globus_libc_strdup(str));

        /* get the largest possible string length for later mem management */
        str_max_len += strlen(str);
    }

    /* turn down the cast count if we do not have enough urls left */
    cast_count = attr->cast_count;
    if(globus_fifo_size(&url_q) < cast_count)
    {
        cast_count = globus_fifo_size(&url_q);
    }

    /* here down is where callbacks start to be registered so we lock */
    globus_mutex_lock(&handle->mutex);
    {
        handle->open_op = op;
        /* if we have anyone to forward onto */
        total_url_count = globus_fifo_size(&url_q);
        if(cast_count != 0)
        {
            /* allocate a forwarder handle for everyone that needs one */
            handle->ftp_handles = (xio_l_gmc_ftp_handle_t *)
                globus_calloc(cast_count, sizeof(xio_l_gmc_ftp_handle_t));
            for(i = 0; i < cast_count; i++)
            {
                handle->ftp_handles[i].whos_my_daddy = handle;
                handle->ftp_handles[i].url = (char *)
                    globus_fifo_dequeue(&url_q);

                str_ptr = strchr(handle->ftp_handles[i].url, '?');
                if(str_ptr != NULL)
                {
                    *str_ptr = '\0';

                    str_ptr++;
                    handle->ftp_handles[i].str_opts = strdup(str_ptr);
                }

                handle->ftp_handles[i].ndx = i;
                globus_fifo_init(&handle->ftp_handles[i].url_q);
            }
            each_cast_count = total_url_count / cast_count;

            /* if not an even divisor give each 1 extra for an even
                distribution of the remainder */
            if(total_url_count % cast_count != 0)
            {
                each_cast_count++;
            }

            /* attach the next url strings to all the receivers */
            for(i = 0; i < cast_count; i++)
            {
                result = xio_l_gmc_setup_forwarder(
                    &handle->ftp_handles[i],
                    cred,
                    sbj,
                    username,
                    pw,
                    attr,
                    &url_q,
                    str_max_len,
                    each_cast_count);
                if(result != GLOBUS_SUCCESS)
                {
                    goto error_forward_setup;
                }

                /* keep a count of successful ones for clean up */
                error_cast_count++;
                handle->op_count++;
                handle->ftps++;
                handle->ftps_total++;
            }
        }

        if(handle->pass_write)
        {
            /*alter resource and pass down */
            result = globus_xio_driver_pass_open(
                op, contact_info, xio_l_gridftp_multicast_open_cb, handle);
            if(result != GLOBUS_SUCCESS)
            {
                goto error_disk_open;
            }
            handle->op_count++;
        }

        if(handle->op_count == 0)
        {
            /* This is the only case when we should return an error */
            result = GlobusXIOGMCNoOpError("Nothing to open");
            goto error_no_operations;
        }
        handle->state = XIO_GMC_STATE_OPENING;
        if(!handle->pass_write)
        {
            handle->state = XIO_GMC_STATE_OPEN;
            finish_open = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&handle->mutex);

    if(finish_open)
    {
        globus_xio_driver_finished_open(handle, op, GLOBUS_SUCCESS);
    }

    return GLOBUS_SUCCESS;

error_forward_setup:
error_disk_open:

    for(i = 0; i < error_cast_count; i++)
    {
        xio_l_gmc_destroy_forwarder(&handle->ftp_handles[i]);
        handle->result = result;
        result = GLOBUS_SUCCESS; /* gotta wait for callbacks */
    }
    handle->state = XIO_GMC_STATE_OPENING_ERROR;

    globus_mutex_unlock(&handle->mutex);

error_no_operations:
    xio_l_gmc_handle_destroy(handle);

    return result;
}

static
void
xio_l_gmc_destroy_forwarder(
    xio_l_gmc_ftp_handle_t *            ftp_handle)
{
    globus_result_t                     result;

    if(ftp_handle->closing)
    {
        return;
    }

    ftp_handle->closing = GLOBUS_TRUE;
    result = globus_ftp_client_abort(&ftp_handle->client_h);
    
    /* not the ost interesting error, but we will hang onto it if there
        wasnt a better one around */
    if(result != GLOBUS_SUCCESS && ftp_handle->result == GLOBUS_SUCCESS)
    {
        ftp_handle->result = result;
    }
}

static
globus_result_t
xio_l_gmc_setup_forwarder(
    xio_l_gmc_ftp_handle_t *            ftp_handle,
    gss_cred_id_t                       cred,
    char *                              sbj,
    char *                              username,
    char *                              pw,
    xio_l_gridftp_multicast_attr_t *    attr,
    globus_fifo_t *                     url_q,
    int                                 max_str_len,
    int                                 each_cast_count)
{
    int                                 cast_count;
    char *                              driver_del;
    char *                              stack_str;
    int                                 stack_str_ndx;
    int                                 i;
    char *                              str;
    char *                              tmp_ss;
    globus_result_t                     result;
    char                                delim = '#';
    xio_l_gridftp_multicast_handle_t *  handle;
    globus_ftp_control_parallelism_t    para;
    globus_ftp_control_tcpbuffer_t      tcp_buffer;

    handle = ftp_handle->whos_my_daddy;

    cast_count = 0;
    stack_str = malloc(max_str_len*3); /* 3x for url encoding */
    stack_str_ndx = 0;
    for(i = 0; i < each_cast_count; i++)
    {
        if(!globus_fifo_empty(url_q))
        {
            str = (char *) globus_fifo_dequeue(url_q);
            globus_fifo_enqueue(&ftp_handle->url_q, str);

            str = globus_url_string_hex_encode(str, GMC_URL_ENC_CHAR);

            stack_str[stack_str_ndx] = delim;
            stack_str_ndx++;
            strcpy(&stack_str[stack_str_ndx], str);
            stack_str_ndx += strlen(str);

            free(str);
            cast_count++;
        }
        stack_str[stack_str_ndx] = '\0';
    }
    /* if we have anyone to send to */
    globus_ftp_client_handleattr_init(&ftp_handle->handle_attr);
    globus_ftp_client_operationattr_init(&ftp_handle->op_attr);

    if(cred != NULL || username != NULL)
    {
        result = globus_ftp_client_operationattr_set_authorization(
            &ftp_handle->op_attr,
            cred,
            username,
            pw,
            NULL,
            sbj);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_attr;
        }
    }
    result = globus_ftp_client_operationattr_set_mode(
        &ftp_handle->op_attr,
        GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr;
    }

    para.fixed.mode = GLOBUS_FTP_CONTROL_PARALLELISM_FIXED;
    para.fixed.size = handle->P;
    result = globus_ftp_client_operationattr_set_parallelism(
        &ftp_handle->op_attr,
        &para);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr;
    }

    tcp_buffer.mode = GLOBUS_FTP_CONTROL_TCPBUFFER_FIXED;
    tcp_buffer.fixed.size = attr->tcp_bs;
    result = globus_ftp_client_operationattr_set_tcp_buffer(
        &ftp_handle->op_attr,
        &tcp_buffer);

    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr;
    }

    driver_del = "";
    if(*stack_str != '\0')
    {
        tmp_ss = stack_str;
        stack_str = globus_common_create_string("urls=%s;", tmp_ss);
        globus_free(tmp_ss);
        driver_del = ":";
    }

    /* add on the opts if there are any */
    if(ftp_handle->str_opts != NULL)
    {
        tmp_ss = stack_str;
        stack_str = globus_common_create_string(
            "%s%s", tmp_ss, ftp_handle->str_opts);
        globus_free(tmp_ss);
        driver_del = ":";
    }

    tmp_ss = stack_str;
    stack_str = globus_url_string_hex_encode(tmp_ss, GMC_URL_ENC_CHAR);
    globus_free(tmp_ss);

    /* XXX what if they put other things on the stack too ? */
    ftp_handle->stack_str = globus_common_create_string(
            "file,gridftp_multicast%s%s", driver_del, stack_str);

    result = globus_ftp_client_operationattr_set_disk_stack(
        &ftp_handle->op_attr,
        ftp_handle->stack_str);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr;
    }

    result = globus_ftp_client_handle_init(
        &ftp_handle->client_h, &ftp_handle->handle_attr);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr;
    }
    result = globus_ftp_client_put(
        &ftp_handle->client_h,
        ftp_handle->url,
        &ftp_handle->op_attr,
        NULL,
        xio_l_gmc_put_done,
        ftp_handle);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_handle;
    }
    globus_free(stack_str);

    return GLOBUS_SUCCESS;

error_handle:
    ftp_handle->result = result;
    globus_ftp_client_handle_destroy(&ftp_handle->client_h);
error_attr:
    globus_ftp_client_handleattr_destroy(&ftp_handle->handle_attr);
    globus_ftp_client_operationattr_destroy(&ftp_handle->op_attr);
    globus_free(stack_str);

    /* is this safe ? */
    return result;
}

static
void
xio_l_gmc_ftp_write_cb(
    void *                              user_arg,
    globus_ftp_client_handle_t *        in_handle,
    globus_object_t *                   err,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_off_t                        offset,
    globus_bool_t                       eof)
{
    globus_result_t                     result;
    globus_bool_t                       finish_write = GLOBUS_FALSE;
    globus_bool_t                       alive;
    xio_l_gridftp_multicast_handle_t *  handle;
    xio_l_gmc_ftp_handle_t *            ftp_handle;
    GlobusXIOName(xio_l_gmc_ftp_write_cb);

    ftp_handle = (xio_l_gmc_ftp_handle_t *) user_arg;
    handle = ftp_handle->whos_my_daddy;

    globus_mutex_lock(&handle->mutex);
    {
        handle->write_op_count--;
        if(err != NULL)
        {
            ftp_handle->result = globus_error_put(globus_object_copy(err));
        }
        if(handle->write_op_count == 0 && handle->write_op != NULL)
        {
            finish_write = GLOBUS_TRUE;
            alive = xio_l_gmc_anyone_alive(handle);
            if(!alive)
            {
                result = xio_l_gmc_get_error(handle);
            }
            else
            {
                result = GLOBUS_SUCCESS;
            }
        }
    }
    globus_mutex_unlock(&handle->mutex);

    if(finish_write)
    {
        globus_xio_driver_finished_write(
            handle->write_op, result, handle->nbytes);
    }
}

static
void
xio_l_gmc_disk_write_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    int                                 i;
    globus_bool_t                       finish_write = GLOBUS_FALSE;
    globus_bool_t                       alive;
    xio_l_gridftp_multicast_handle_t *  handle;
    GlobusXIOName(xio_l_gmc_disk_write_cb);

    handle = (xio_l_gridftp_multicast_handle_t *) user_arg;

    globus_mutex_lock(&handle->mutex);
    {
        handle->write_op_count--;
        if(result != GLOBUS_SUCCESS)
        {
            handle->result = result;
            /* disk error means clean all the ftp conenctsions */
            for(i = 0; i < handle->ftps; i++)
            {
                xio_l_gmc_destroy_forwarder(&handle->ftp_handles[i]);
            }
        }
        if(handle->write_op_count == 0)
        {
            finish_write = GLOBUS_TRUE;
            alive = xio_l_gmc_anyone_alive(handle);
            if(!alive)
            {
                result = xio_l_gmc_get_error(handle);
            }
            else
            {
                result = GLOBUS_SUCCESS;
            }
        }
    }
    globus_mutex_unlock(&handle->mutex);

    if(finish_write)
    {
        globus_xio_driver_finished_write(
            handle->write_op, result, handle->nbytes);
    }
}

static
globus_result_t
xio_l_gridftp_multicast_read(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t*           iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_size_t                       wait_for;
    globus_result_t                     result;

    wait_for = globus_xio_operation_get_wait_for(op);

    result = globus_xio_driver_pass_read(
        op,
        (globus_xio_iovec_t *)iovec,
        iovec_count,
        wait_for,
        NULL,
        NULL);

    return result;
}

static
globus_result_t
xio_l_gridftp_multicast_write(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_size_t                       wait_for = 0;
    int                                 i;
    int                                 j;
    xio_l_gridftp_multicast_handle_t *  handle;
    xio_l_gmc_ftp_handle_t *            ftp_handle;
    globus_result_t                     result;
    globus_off_t                        offset;
    globus_bool_t                       finish_write = GLOBUS_FALSE;
    GlobusXIOName(xio_l_gridftp_multicast_write);
    
    handle = (xio_l_gridftp_multicast_handle_t *) driver_specific_handle;

    globus_mutex_lock(&handle->mutex);
    {
        handle->write_op = op;
        switch(handle->state)
        {
            case XIO_GMC_STATE_OPEN:
                /* this is the standard case */
                for(i = 0; i < handle->ftps_total; i++)
                {
                    ftp_handle = &handle->ftp_handles[i];
                    offset = handle->offset;
                    for(j = 0; 
                        j < iovec_count && 
                            ftp_handle->result == GLOBUS_SUCCESS;
                        j++)
                    {
                        if(iovec[j].iov_len > 0)
                        {
                            result = globus_ftp_client_register_write(
                                &ftp_handle->client_h,
                                (globus_byte_t *) iovec[j].iov_base,
                                iovec[j].iov_len,
                                offset,
                                GLOBUS_FALSE,
                                xio_l_gmc_ftp_write_cb,
                                ftp_handle);
                            if(result != GLOBUS_SUCCESS)
                            {
                                ftp_handle->result = result;
                            }
                            else
                            {
                                handle->write_op_count++;
                            }
                            offset += iovec[j].iov_len;
                        }
                        else
                        {
                            /* XXX 0 len writes */
                        }
                    }
                }

                /* count seperatly in case we are not forwarding to
                    anyone one */
                wait_for = 0;
                for(i = 0; i < iovec_count; i++)
                {
                    wait_for += iovec[i].iov_len;
                }
                handle->offset += wait_for;
                handle->nbytes = wait_for;
                if(handle->pass_write)
                {
                    result = globus_xio_driver_pass_write(
                        op,
                        (globus_xio_iovec_t *)iovec,
                        iovec_count,
                        wait_for,
                        xio_l_gmc_disk_write_cb,
                        handle);
                    if(result != GLOBUS_SUCCESS)
                    {
                        goto error_pass;
                    }
                    handle->write_op_count++;
                }
                break;

            case XIO_GMC_STATE_OPENING_ERROR:
            case XIO_GMC_STATE_OPENING:
            case XIO_GMC_STATE_CLOSING:
                /* none of these should be possible */
                globus_assert(0 && "bad state");
                break;
        }

        if(handle->write_op_count == 0)
        {
            if(wait_for == 0)
            {
                /* just finish the write */
                finish_write = GLOBUS_TRUE;
            }
            else
            {
                result = xio_l_gmc_get_error(handle);
                if(result == GLOBUS_SUCCESS)
                {
                    /* could be no errors, but nothing to write to
                        because there was never anything opened. */
                    result = GlobusXIOGMCNoOpError("Nothing to open");
                }
                goto error_pass;
            }
        }
    }
    globus_mutex_unlock(&handle->mutex);

    if(finish_write)
    {
        /* should only happen witha wait for of 0 */
        globus_assert(wait_for == 0);
        globus_xio_driver_finished_write(
            handle->write_op, GLOBUS_SUCCESS, 0);
    }

    return GLOBUS_SUCCESS;

error_pass:
    /* take down all ftp handles when there is a disk error */
    for(i = 0; i < handle->ftps; i++)
    {
        xio_l_gmc_destroy_forwarder(&handle->ftp_handles[i]);
    }

    handle->write_op = NULL;

    globus_mutex_unlock(&handle->mutex);

    return result;
}

static
void
xio_l_gmc_eof_cb(
    void *                              user_arg,
    globus_ftp_client_handle_t *        in_handle,
    globus_object_t *                   err,
    globus_byte_t *                     buffer,
    globus_size_t                       length,
    globus_off_t                        offset,
    globus_bool_t                       eof)
{
    xio_l_gmc_ftp_handle_t *            ftp_handle;
    xio_l_gridftp_multicast_handle_t *  handle;
    GlobusXIOName(xio_l_gmc_eof_cb);

    ftp_handle = (xio_l_gmc_ftp_handle_t *) user_arg;
    handle = ftp_handle->whos_my_daddy;

    globus_mutex_lock(&handle->mutex);
    {
        if(err != NULL)
        {
            ftp_handle->result = globus_error_put(globus_object_copy(err));
        }
    }
    globus_mutex_unlock(&handle->mutex);
}

static
void
xio_l_gmc_close_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_bool_t                       finish_close = GLOBUS_FALSE;
    xio_l_gridftp_multicast_handle_t *  handle;
    GlobusXIOName(xio_l_gmc_close_cb);
    
    handle = (xio_l_gridftp_multicast_handle_t *) user_arg;
    globus_mutex_lock(&handle->mutex);
    {
        handle->op_count--;

        if(result != GLOBUS_SUCCESS)
        {
            handle->result = result;
        }
        if(handle->op_count == 0)
        {
            finish_close = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&handle->mutex);

    if(finish_close)
    {
        result = xio_l_gmc_get_error(handle);
        globus_xio_driver_finished_close(handle->close_op, result);
        xio_l_gmc_handle_destroy(handle);
    }
}


static
globus_result_t
xio_l_gridftp_multicast_close(
    void *                              driver_specific_handle,
    void *                              attr,
    globus_xio_operation_t              op)
{
    globus_result_t                     result;
    int                                 i;
    xio_l_gmc_ftp_handle_t *            ftp_handle;
    xio_l_gridftp_multicast_handle_t *  handle;
    GlobusXIOName(xio_l_gridftp_multicast_close);
    
    handle = (xio_l_gridftp_multicast_handle_t *) driver_specific_handle;

    globus_mutex_lock(&handle->mutex);
    {
        handle->close_op = op;
        switch(handle->state)
        {
            case XIO_GMC_STATE_OPEN:
                /* this is the standard case, write an eof, then wait for
                    closes */
                handle->state = XIO_GMC_STATE_CLOSING;
                handle->close_op = op;

                handle->op_count = handle->ftps;
                for(i = 0; i < handle->ftps; i++)
                {
                    ftp_handle = &handle->ftp_handles[i];

                    if(ftp_handle->result == GLOBUS_SUCCESS)
                    {
                        result = globus_ftp_client_register_write(
                            &ftp_handle->client_h,
                            ftp_handle->mt_buf,
                            0,
                            handle->offset,
                            GLOBUS_TRUE,
                            xio_l_gmc_eof_cb,
                            ftp_handle);
                        if(result != GLOBUS_SUCCESS)
                        {
                            /*  record error, but then treat as normal.
                            no matter what we need to get all the done
                            callbacks. so nothing to sweat */
                            ftp_handle->result = result;

                            /* might need to abort the transfer.  for
                                now assume the registration error means
                                more errors hav ecome or a re coming.
                                if hangs, revisit XXX */
                        }
                        else
                        {
                        }
                    }
                }

                if(handle->pass_write)
                {
                    result = globus_xio_driver_pass_close(
                        op, xio_l_gmc_close_cb, handle);
                    if(result != GLOBUS_SUCCESS)
                    {
                        if(handle->result == GLOBUS_SUCCESS)
                        {
                            handle->result = result;
                        }
                        goto error_pass;
                    }
                    handle->op_count++;
                }
                break;
    
            case XIO_GMC_STATE_OPENING_ERROR:
            case XIO_GMC_STATE_OPENING:
            case XIO_GMC_STATE_CLOSING:
                /* none of these should be possible */
                globus_assert(0 && "bad state");
                break;
        }

        if(handle->op_count == 0)
        {
            goto error_pass;
        }
    }
    globus_mutex_unlock(&handle->mutex);

    return GLOBUS_SUCCESS;

error_pass:
    globus_mutex_unlock(&handle->mutex);

    result = xio_l_gmc_get_error(handle);
    xio_l_gmc_handle_destroy(handle);

    return result;


}

static
globus_result_t
xio_l_gridftp_multicast_cntl(
    void  *                             driver_specific_handle,
    int                                 cmd,
    va_list                             ap)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_off_t                        in_offset;
    xio_l_gridftp_multicast_handle_t *  handle;
    GlobusXIOName(xio_l_gridftp_multicast_cntl);

    handle = (xio_l_gridftp_multicast_handle_t *) driver_specific_handle;

    globus_mutex_lock(&handle->mutex);
    {
        switch(cmd)
        {
            case GLOBUS_XIO_SEEK:
                in_offset = va_arg(ap, globus_off_t);
                handle->offset = in_offset;

                /* let it keep going */
                if(handle->pass_write)
                {
                    result = GlobusXIOErrorInvalidCommand(cmd);
                }
                else
                {
                    result = GLOBUS_SUCCESS;
                }
                break;

            default:
                result = GlobusXIOErrorInvalidCommand(cmd);
                break;
        }
    }
    globus_mutex_unlock(&handle->mutex);

    return result;
}

static
globus_result_t
xio_l_gridftp_multicast_attr_init(
    void **                             out_attr)
{
    xio_l_gridftp_multicast_attr_t *    attr;

    attr = (xio_l_gridftp_multicast_attr_t *) 
        globus_calloc(1, sizeof(xio_l_gridftp_multicast_attr_t));

    globus_fifo_init(&attr->url_q);

    attr->P = xio_l_gmc_default_attr.P;
    attr->tcp_bs = xio_l_gmc_default_attr.tcp_bs;
    attr->cast_count = xio_l_gmc_default_attr.cast_count;
    attr->pass_write = xio_l_gmc_default_attr.pass_write;
    
    *out_attr = attr;

    return GLOBUS_SUCCESS;
}


static globus_xio_string_cntl_table_t  
    xio_l_gridftp_multicast_string_opts_table[] =
{
    {"P", GLOBUS_XIO_GRIDFTP_MULTICAST_ATTR_PARALLEL,
        globus_xio_string_cntl_int},
    {"cc", GLOBUS_XIO_GRIDFTP_MULTICAST_ATTR_CAST_COUNT,
        globus_xio_string_cntl_int},
    {"tcpbs", GLOBUS_XIO_GRIDFTP_MULTICAST_ATTR_TCPBS,
        globus_xio_string_cntl_formated_int},
    {"urls", GLOBUS_XIO_GRIDFTP_MULTICAST_ATTR_URLS,
        globus_xio_string_cntl_string_list},
    {"local_write", GLOBUS_XIO_GRIDFTP_MULTICAST_ATTR_LOCAL_WRITE,
        globus_xio_string_cntl_bool},
    {"subject", GLOBUS_XIO_GRIDFTP_MULTICAST_ATTR_SUBJECT,
        globus_xio_string_cntl_string},
    {NULL, 0, NULL}
};

static
globus_result_t
xio_l_gridftp_multicast_attr_cntl(
    void *                              driver_attr,
    int                                 cmd,
    va_list                             ap)
{
    char **                             argv;
    int                                 i;
    xio_l_gridftp_multicast_attr_t *    attr;
    char *                              sbj;
    char *                              url;

    attr = (xio_l_gridftp_multicast_attr_t *) driver_attr;

    switch(cmd)
    {
        case GLOBUS_XIO_GRIDFTP_MULTICAST_ATTR_PARALLEL:
            attr->P = va_arg(ap, int);
            break;

        case GLOBUS_XIO_GRIDFTP_MULTICAST_ATTR_CAST_COUNT:
            attr->cast_count = va_arg(ap, int);
            break;

        case GLOBUS_XIO_GRIDFTP_MULTICAST_ATTR_TCPBS:
            attr->tcp_bs = (globus_size_t) va_arg(ap, int);
            break;

        case GLOBUS_XIO_GRIDFTP_MULTICAST_ATTR_URLS:
            argv = (char **) va_arg(ap, char **);
            for(i = 0; argv[i] != NULL; i++)
            {
                url = strdup(argv[i]);
                globus_url_string_hex_decode(url);
                globus_fifo_enqueue(&attr->url_q, url);
            }
            break;

        case GLOBUS_XIO_GRIDFTP_MULTICAST_ATTR_LOCAL_WRITE:
            attr->pass_write = (globus_bool_t) va_arg(ap, globus_bool_t);
            break;

        case GLOBUS_XIO_GRIDFTP_MULTICAST_ATTR_SUBJECT:
            sbj = va_arg(ap, char *);
            if(sbj != NULL)
            {
                attr->subject = strdup(sbj);
            }
            break;
    }

    return GLOBUS_SUCCESS;
}


static
globus_result_t
xio_l_gridftp_multicast_attr_copy(
    void **                             dst,
    void *                              src)
{
    int                                 i;
    char *                              str;
    xio_l_gridftp_multicast_attr_t *    dst_attr; 
    xio_l_gridftp_multicast_attr_t *    src_attr;

    src_attr = (xio_l_gridftp_multicast_attr_t *) src;
    xio_l_gridftp_multicast_attr_init((void **)&dst_attr);

    dst_attr->P = src_attr->P;
    dst_attr->tcp_bs = src_attr->tcp_bs;
    dst_attr->cast_count = src_attr->cast_count;
    dst_attr->pass_write = src_attr->pass_write;

    for(i = 0; i < globus_fifo_size(&src_attr->url_q); i++)
    {
        str = (char *) globus_fifo_dequeue(&src_attr->url_q);
        globus_fifo_enqueue(&src_attr->url_q, str);
        globus_fifo_enqueue(&dst_attr->url_q, globus_libc_strdup(str));
    }

    *dst = dst_attr;

    return GLOBUS_SUCCESS;
}


static
globus_result_t
xio_l_gridftp_multicast_attr_destroy(
    void *                              driver_attr)
{
    char *                              str;
    xio_l_gridftp_multicast_attr_t *    attr;

    attr = (xio_l_gridftp_multicast_attr_t *) driver_attr;

    while(!globus_fifo_empty(&attr->url_q))
    {
        str = (char *) globus_fifo_dequeue(&attr->url_q);
        globus_free(str);
    }
    globus_fifo_destroy(&attr->url_q);
    globus_free(attr);

    return GLOBUS_SUCCESS;
}


static
globus_result_t
xio_l_gridftp_multicast_init(
    globus_xio_driver_t *               out_driver)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     result;
    GlobusXIOName(xio_l_gridftp_multicast_init);

    GlobusXIOGridftpMulticastDebugEnter();
    result = globus_xio_driver_init(&driver, "gridftp_multicast", NULL);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "xio_l_driver_init", result);
        goto error_init;
    }
    globus_xio_driver_set_transform(
        driver,
        xio_l_gridftp_multicast_open,
        xio_l_gridftp_multicast_close,
        xio_l_gridftp_multicast_read,
        xio_l_gridftp_multicast_write,
        xio_l_gridftp_multicast_cntl,
        NULL);
    globus_xio_driver_set_attr(
        driver,
        xio_l_gridftp_multicast_attr_init,
        xio_l_gridftp_multicast_attr_copy,
        xio_l_gridftp_multicast_attr_cntl,
        xio_l_gridftp_multicast_attr_destroy);

    globus_xio_driver_string_cntl_set_table(
        driver,
        xio_l_gridftp_multicast_string_opts_table);

    *out_driver = driver;

    globus_fifo_init(&xio_l_gmc_default_attr.url_q);
    xio_l_gmc_default_attr.P = 1;
    xio_l_gmc_default_attr.tcp_bs = 131072;
    xio_l_gmc_default_attr.cast_count = 2;
    xio_l_gmc_default_attr.pass_write = GLOBUS_TRUE;

    GlobusXIOGridftpMulticastDebugExit();
    return GLOBUS_SUCCESS;

error_init:
    GlobusXIOGridftpMulticastDebugExitWithError();
    return result;
}


static
void
xio_l_gridftp_multicast_destroy(
    globus_xio_driver_t                 driver)
{
    globus_xio_driver_destroy(driver);
}


GlobusXIODefineDriver(
    gridftp_multicast,
    xio_l_gridftp_multicast_init,
    xio_l_gridftp_multicast_destroy);
