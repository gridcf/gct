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

#include "globus_i_xio_http.h"

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @defgroup globus_i_xio_http_server Internal Server Implementation
 * @brief Internal Server Implementation
 */
#endif

static
void
globus_l_xio_http_accept_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg);

static
globus_result_t
globus_l_xio_http_server_parse_request(
    globus_i_xio_http_handle_t *        http_handle,
    globus_bool_t *                     done);

static
void
globus_l_xio_http_server_write_response_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg);

/**
 * Accept an HTTP request
 * @ingroup globus_i_xio_http_server
 *
 * Passes the request to the transport driver. In the callback, 
 * the request target information will be generated.
 *
 * @param driver_server
 *     Void * pointing to a server structure. Not used yet.
 * @param accept_op
 *     Operation associated with the accept.
 *
 * @return
 * This function passes the accept to the underlying transport driver,
 * so will return whatever value that driver returns.
 */
globus_result_t
globus_i_xio_http_accept(
    void *                              driver_server,
    globus_xio_operation_t              accept_op)
{
    return globus_xio_driver_pass_accept(accept_op,
            globus_l_xio_http_accept_callback, NULL);
}
/* globus_i_xio_http_accept() */

/**
 * Accept callback
 * @ingroup globus_i_xio_http_server
 *
 * Callback function called when the transport completes accepting
 * a connection for the request. Generates a new target to associate with
 * the result of this accept. This target will be passed to
 * globus_xio_open() or globus_xio_register_open() to begin processing the
 * request.
 *
 * @param op
 *     XIO Data structure passed through to globus_xio_driver_finished_accept().
 * @param result
 *     Result from the transport's attempt to accept a new connection.
 * @param user_arg
 *     Not used.
 *
 * @return void
 *
 * @todo When implemented in the XIO driver framework, parse the request
 * header before returning from this, so the target is populated with
 * meaningful information for the user. This will help enable persistent
 * connections.
 */
static
void
globus_l_xio_http_accept_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_i_xio_http_target_t *        target_info = NULL;
    GlobusXIOName(globus_l_xio_http_accept_callback);
    
    if (result == GLOBUS_SUCCESS)
    {
        target_info = globus_i_xio_http_target_new();

        if (target_info == NULL)
        {
            result = GlobusXIOErrorMemory("target");
        }
    }

    globus_xio_driver_finished_accept(op, target_info, result);
}
/* globus_l_xio_http_accept_callback() */

/**
 * Server-side connection open callback
 * @ingroup globus_i_xio_http_server
 *
 * Called as a result of open at the transport level. If this was successful,
 * we will finish the open operation. If an error happens, this function will
 * close the * handle internally and call globus_xio_driver_finished_open() to
 * propagate the error.
 *
 * @param op
 *     Operation associated with the open.
 * @param result
 *     Result from the transport's attempt to open the new connection.
 * @param user_arg
 *     Void * pointing to a #globus_i_xio_http_handle_t associated with
 *     this open.
 *
 * @return void
 */
void
globus_i_xio_http_server_open_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_i_xio_http_handle_t *        http_handle = user_arg;
    GlobusXIOName(globus_i_xio_http_server_open_callback);

    if (result != GLOBUS_SUCCESS)
    {
        globus_i_xio_http_handle_destroy(http_handle);
        globus_libc_free(http_handle);
        http_handle = NULL;
    }

    globus_xio_driver_finished_open(
            http_handle,
            op,
            result);
    return;
}
/* globus_i_xio_http_server_open_callback() */

/**
 * Write the response to an HTTP request
 * @ingroup globus_i_xio_http_server
 *
 * Generates an HTTP response line from a handle, and passes it to the
 * transport. The globus_l_xio_http_server_write_response_callback() will
 * be called once the transport has sent the response.
 *
 * This call may be triggered by either the first write on a server handle,
 * or by calling the #GLOBUS_XIO_HTTP_HANDLE_SET_END_OF_ENTITY handle
 * control function.
 *
 * Called with my mutex lock.
 *
 * @param http_handle
 *     Handle associated with this HTTP stream.
 * @param iovec
 *     Array of globus_xio_iovec_t structs associated with the user's write.
 * @param iovec_count
 *     Length of the @a iovec array. If this is zero, we assume that the
 *     response is being generated by the
 *     #GLOBUS_XIO_HTTP_HANDLE_SET_END_OF_ENTITY control.
 * @param op
 *     Operation associated with the write. If this is NULL (in the case
 *     of the GLOBUS_XIO_HTTP_HANDLE_SET_END_OF_ENTITY control), one
 *     will be created in this function.
 *
 * This function returns GLOBUS_SUCCESS, GLOBUS_XIO_ERROR_MEMORY, or an
 * error result from globus_xio_driver_operation_create(), or
 * globus_xio_driver_pass_write().
 *
 * @retval GLOBUS_SUCCESS
 *     Response was passed to the transport for writing. If this was generated
 *     by a user writing data, then the write will occur after the 
 *     globus_l_xio_http_server_write_response_callback() has been called.
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Unable to compose the response due to memory constraints.
 */
globus_result_t
globus_i_xio_http_server_write_response(
    globus_i_xio_http_handle_t *        http_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_result_t                     result;
    globus_fifo_t                       iovecs;
    const char *                        str;
    char                                code_str[5];
    globus_xio_iovec_t *                iov;
    int                                 rc;
    int                                 i;
    int                                 send_size;
    char *                              size_buffer = NULL;
    globus_bool_t                       free_op = GLOBUS_FALSE;
    globus_xio_http_header_t *          current_header;
    GlobusXIOName(globus_i_xio_server_write_response);

    globus_assert(http_handle->send_state == GLOBUS_XIO_HTTP_STATUS_LINE);
    rc = globus_fifo_init(&iovecs);

    if (rc != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorMemory("iovecs");

        goto error_exit;
    }

    /* Compose HTTP Response:
     * HTTP-Version SP Status-Code SP Reason-Phrase CRLF
     */
    if (http_handle->response_info.http_version == GLOBUS_XIO_HTTP_VERSION_1_0)
    {
        str = "HTTP/1.0 ";
    }
    else
    {
        http_handle->response_info.http_version = GLOBUS_XIO_HTTP_VERSION_1_1;
        str = "HTTP/1.1 ";
    }
    GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs, str, 9, free_iovecs_error);

    sprintf(code_str, "%d ", http_handle->response_info.status_code);
    GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs, code_str, 4, free_iovecs_error);

    if (http_handle->response_info.reason_phrase != NULL)
    {
        str = http_handle->response_info.reason_phrase;
    }
    else
    {
        str = globus_i_xio_http_lookup_reason(
                http_handle->response_info.status_code);
    }

    GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs, str, strlen(str), free_iovecs_error);
    GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs, "\r\n", 2, free_iovecs_error);

    current_header = globus_hashtable_first(
            &http_handle->response_info.headers.headers);

    while (current_header)
    {
        GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs,
                current_header->name,
                strlen(current_header->name),
                free_iovecs_error);

        GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs,
                ": ",
                2,
                free_iovecs_error);

        GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs,
                current_header->value,
                strlen(current_header->value),
                free_iovecs_error);

        GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs,
                "\r\n",
                2,
                free_iovecs_error);
        current_header = globus_hashtable_next(
                &http_handle->response_info.headers.headers);
    }

    /*
     * Special headers we generate.
     */
    if (GLOBUS_I_XIO_HTTP_HEADER_IS_CONNECTION_CLOSE(
                &http_handle->response_info.headers) ||
            (http_handle->request_info.http_version ==
                GLOBUS_XIO_HTTP_VERSION_1_0) ||
            (http_handle->response_info.headers.transfer_encoding
                == GLOBUS_XIO_HTTP_TRANSFER_ENCODING_IDENTITY &&
             GLOBUS_I_XIO_HTTP_HEADER_IS_CONTENT_LENGTH_SET(
                &http_handle->response_info.headers)))
    {
        http_handle->response_info.headers.flags |= 
                GLOBUS_I_XIO_HTTP_HEADER_CONNECTION_CLOSE;

        GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs,
                "Connection: close\r\n",
                19,
                free_iovecs_error);
    }
    if (iovec_count > 0)
    {
        /*
         * We are sending a body, so we'll set the appropriate entity-related
         * headers
         */
        if (http_handle->request_info.http_version
                == GLOBUS_XIO_HTTP_VERSION_1_0 ||
            (http_handle->response_info.headers.transfer_encoding
                == GLOBUS_XIO_HTTP_TRANSFER_ENCODING_IDENTITY &&
             GLOBUS_I_XIO_HTTP_HEADER_IS_CONTENT_LENGTH_SET(
                     &http_handle->response_info.headers)))
        {
            http_handle->response_info.headers.transfer_encoding
                = GLOBUS_XIO_HTTP_TRANSFER_ENCODING_IDENTITY;
            /* Transfer-Encoding mustn't be sent to a HTTP/1.0 client */
            if (http_handle->request_info.http_version
                != GLOBUS_XIO_HTTP_VERSION_1_0)
            {
                GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs,
                        "Transfer-Encoding: identity\r\n",
                        29,
                        free_iovecs_error);
            }
            /*
             * When we know the content-length beforehand we can set it here,
             * otherwise, we will use the connection: close header
             */
            if (GLOBUS_I_XIO_HTTP_HEADER_IS_CONTENT_LENGTH_SET(
                    &http_handle->response_info.headers))
            {
                GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs,
                        "Content-Length: ",
                        16,
                        free_iovecs_error);

                size_buffer = globus_common_create_string(
                        "%"GLOBUS_OFF_T_FORMAT"\r\n",
                         http_handle->response_info.headers.content_length);
                if (size_buffer == NULL)
                {
                    result = GlobusXIOErrorMemory("iovec.iov_base");

                    goto free_iovecs_error;
                }
                GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs,
                        size_buffer,
                        strlen(size_buffer),
                        free_iovecs_error);

                free(size_buffer);

                size_buffer = NULL;
            }
        }
        else
        {
            http_handle->response_info.headers.transfer_encoding
                = GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED;
            GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs,
                    "Transfer-Encoding: chunked\r\n",
                    28,
                    free_iovecs_error);
        }
    }
    GLOBUS_XIO_HTTP_COPY_BLOB(&iovecs, "\r\n", 2, free_iovecs_error);

    http_handle->header_iovcnt = globus_fifo_size(&iovecs);
    http_handle->header_iovec = globus_libc_malloc(
            http_handle->header_iovcnt * sizeof(globus_xio_iovec_t));
    if (http_handle->header_iovec == NULL)
    {
        goto free_iovecs_error;
    }

    /* Convert fifo to iovec array, counting up size for wait_for_nbytes
     * parameter to globus_xio_driver_pass_write.
     */
    for (i = 0, send_size = 0; i < http_handle->header_iovcnt; i++)
    {
        iov = globus_fifo_dequeue(&iovecs);

        globus_assert(iov != NULL);

        http_handle->header_iovec[i].iov_base = iov->iov_base;
        http_handle->header_iovec[i].iov_len = iov->iov_len;

        send_size += iov->iov_len;

        globus_libc_free(iov);
    }

    if (op == NULL)
    {
        result = globus_xio_driver_operation_create(
                &op,
                http_handle->handle);

        free_op = GLOBUS_TRUE;
        if (result != GLOBUS_SUCCESS)
        {
            goto free_headers_exit;
        }
    }

    /* Stash user buffer info until we've sent response headers */
    http_handle->write_operation.operation = op;
    http_handle->write_operation.iov = (globus_xio_iovec_t *) iovec;
    http_handle->write_operation.iovcnt = iovec_count;
    http_handle->write_operation.wait_for = 0;

    result = globus_xio_driver_pass_write(
            http_handle->write_operation.operation,
            http_handle->header_iovec,
            http_handle->header_iovcnt,
            send_size,
            globus_l_xio_http_server_write_response_callback,
            http_handle);
    if (result != GLOBUS_SUCCESS)
    {
        goto free_operation_exit;
    }
    globus_fifo_destroy(&iovecs);

    if (iovec_count == 0)
    {
        http_handle->send_state = GLOBUS_XIO_HTTP_EOF;
    }
    else if (http_handle->response_info.headers.transfer_encoding ==
            GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED)
    {
        http_handle->send_state = GLOBUS_XIO_HTTP_CHUNK_BODY;
    }
    else
    {
        http_handle->send_state = GLOBUS_XIO_HTTP_IDENTITY_BODY;
    }

    return GLOBUS_SUCCESS;

free_operation_exit:
    if (free_op)
    {
        globus_xio_driver_operation_destroy(
                http_handle->write_operation.operation);
    }
free_headers_exit:
    http_handle->write_operation.operation = NULL;
    http_handle->write_operation.driver_handle = NULL;
    http_handle->write_operation.iov = NULL;
    http_handle->write_operation.iovcnt = 0;
    http_handle->write_operation.wait_for = 0;

    for (i = 0; i < http_handle->header_iovcnt; i++)
    {
        globus_libc_free(http_handle->header_iovec[i].iov_base);
    }
    globus_libc_free(http_handle->header_iovec);

    http_handle->header_iovec = NULL;
    http_handle->header_iovcnt = 0;

free_iovecs_error:
    while (!globus_fifo_empty(&iovecs))
    {
        iov = globus_fifo_dequeue(&iovecs);

        globus_libc_free(iov->iov_base);
        globus_libc_free(iov);
    }
    globus_fifo_destroy(&iovecs);
    if (size_buffer != NULL)
    {
        free(size_buffer);
    }

error_exit:
    return result;
}
/* globus_i_xio_http_server_write_response() */

/**
 * Callback after writing response
 * @ingroup globus_i_xio_http_server
 *
 * Frees the iovec array associated with the response and then if
 * writing user data was used to trigger the response, write it to the
 * transport.  If an error occurs while writing, the operation will be
 * finished. If the response was triggered by the
 * GLOBUS_XIO_HTTP_HANDLE_SET_END_OF_ENTITY control, then the operation
 * is simply destroyed.
 *
 * @return void
 */
static
void
globus_l_xio_http_server_write_response_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_i_xio_http_handle_t *        http_handle = user_arg;
    int                                 i;

    globus_mutex_lock(&http_handle->mutex);

    for (i = 0; i < http_handle->header_iovcnt; i++)
    {
        globus_libc_free(http_handle->header_iovec[i].iov_base);
    }
    globus_libc_free(http_handle->header_iovec);

    http_handle->header_iovec = NULL;
    http_handle->header_iovcnt = 0;

    if (http_handle->write_operation.iovcnt > 0)
    {
        /* User data to be sent */
        if (http_handle->response_info.headers.transfer_encoding
                == GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED)
        {
            result = globus_i_xio_http_write_chunk(
                    http_handle,
                    http_handle->write_operation.iov,
                    http_handle->write_operation.iovcnt,
                    op);
        }
        else
        {
            result = globus_xio_driver_pass_write(
                    op,
                    http_handle->write_operation.iov,
                    http_handle->write_operation.iovcnt,
                    globus_xio_operation_get_wait_for(op),
                    globus_i_xio_http_write_callback,
                    http_handle);
        }

        if (result != GLOBUS_SUCCESS)
        {
            globus_xio_driver_finished_write(op, result, 0);
            http_handle->write_operation.operation = NULL;
            http_handle->write_operation.driver_handle = NULL;
        }
    }
    else
    {
        /* destroy synthesized operation */
        globus_xio_driver_operation_destroy(
                http_handle->write_operation.operation);
        http_handle->write_operation.operation = NULL;
        http_handle->write_operation.driver_handle = NULL;
    }

    if (http_handle->close_operation != NULL)
    {
        result = globus_xio_driver_pass_close(
                http_handle->close_operation,
                globus_i_xio_http_close_callback,
                http_handle);

        globus_mutex_unlock(&http_handle->mutex);
        if (result != GLOBUS_SUCCESS)
        {
            globus_i_xio_http_close_callback(
                http_handle->close_operation,
                result,
                http_handle);
        }
    }
    else
    {
        http_handle->parse_state = GLOBUS_XIO_HTTP_PRE_REQUEST_LINE;
        globus_mutex_unlock(&http_handle->mutex);
    }
    return;

}
/* globus_l_xio_http_server_write_response_callback() */

/**
 * Parse an HTTP request
 * @ingroup globus_i_xio_http_server
 *
 * Parses the HTTP request line and then uses globus_i_xio_http_header_parse()
 * to parse the header bock .If the entire request header section is reqad, the
 * boolean pointed to by @a done will be modified to be GLOBUS_TRUE
 *
 * Called with mutex locked.
 *
 * @param http_handle
 * @param done
 *
 * @return
 *     This function returns GLOBUS_SUCCESS, GLOBUS_XIO_HTTP_ERROR_PARSE,  or
 *     GLOBUS_XIO_ERROR_MEMORY. Other errors may be generated from
 *     globus_i_xio_http_header_parse()
 *
 * @retval GLOBUS_SUCCESS
 *     No parsing errors occurred while parsing the status line or headers.
 *     Parsing may still be incomplete, depending on the final value of @a
 *     done.
 * @retval <driver>::GLOBUS_XIO_HTTP_ERROR_PARSE
 *     Parse error reading the HTTP request line
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Parsing failed because of memory constraints.
 */
static
globus_result_t
globus_l_xio_http_server_parse_request(
    globus_i_xio_http_handle_t *        http_handle,
    globus_bool_t *                     done)
{
    globus_result_t                     result;
    char *                              eol;
    char *                              current_offset;
    int                                 parsed;
    int                                 rc;
    int                                 http_major;
    int                                 http_minor;
    GlobusXIOName(globus_l_xio_http_server_parse_request);

    if (http_handle->parse_state == GLOBUS_XIO_HTTP_REQUEST_LINE)
    {
        /*
         * Make sure any old request info has been freed so we don't leak here
         * when reusing a handle (or have old headers around)
         */
        globus_i_xio_http_request_destroy(&http_handle->request_info);
        result = globus_i_xio_http_request_init(&http_handle->request_info);

        if (result != GLOBUS_SUCCESS)
        {
            goto error_exit_init;
        }

        /* Parse the request line:
         *
         * Method SP Request-URI SP HTTP-Version CRLF
         */
        current_offset = ((char *) (http_handle->read_buffer.iov_base))
                + http_handle->read_buffer_offset;

        eol = globus_i_xio_http_find_eol(
                current_offset,
                http_handle->read_buffer_valid);
        if (eol == NULL)
        {
            *done = GLOBUS_FALSE;

            return GLOBUS_SUCCESS;
        }
        *eol = '\0';

        rc = sscanf(current_offset, "%*s %n", &parsed);

        if (rc < 0)
        {
            result = GlobusXIOHttpErrorParse("Method", current_offset);

            goto error_exit;
        }

        http_handle->request_info.method = globus_libc_malloc(parsed+1);
        if (http_handle->request_info.method == NULL)
        {
            result = GlobusXIOErrorMemory("method");

            goto error_exit;
        }

        rc = sscanf(current_offset, "%s ", http_handle->request_info.method);
        globus_assert(rc == 1);

        current_offset += parsed;
        
        rc = sscanf(current_offset, "%*s %n", &parsed);
        if (rc < 0)
        {
            result = GlobusXIOHttpErrorParse("Request-URI", current_offset);

            goto error_exit;
        }

        http_handle->request_info.uri = globus_libc_malloc(parsed+1);
        if (http_handle->request_info.uri == NULL)
        {
            result = GlobusXIOErrorMemory("uri");

            goto error_exit;
        }
        rc = sscanf(current_offset, "%s ", http_handle->request_info.uri);
        globus_assert(rc == 1);

        current_offset += parsed;

        rc = sscanf(current_offset, "HTTP/%d.%d", &http_major, &http_minor);

        if (rc < 2)
        {
            result = GlobusXIOHttpErrorParse("Http-Version", current_offset);

            goto error_exit;
        }

        http_handle->request_info.http_version =
            globus_i_xio_http_guess_version(http_major, http_minor);

        /* Set current offset to end of CRLF at the end of this line */
        current_offset = eol+2;

        parsed = current_offset - ((char *) http_handle->read_buffer.iov_base
                + http_handle->read_buffer_offset);
        http_handle->read_buffer_valid -= parsed;
        http_handle->read_buffer_offset += parsed;
        http_handle->parse_state = GLOBUS_XIO_HTTP_HEADERS;
    }
    return globus_i_xio_http_header_parse(http_handle, done);

error_exit:
    parsed = current_offset - ((char *) http_handle->read_buffer.iov_base
                + http_handle->read_buffer_offset);

    /* Chop of what we managed to parse from the buffer */
    http_handle->read_buffer_valid -= parsed;
    http_handle->read_buffer_offset += parsed;

error_exit_init:
    return result;
}
/* globus_l_xio_http_server_parse_request() */

void
globus_i_xio_http_server_read_request_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_i_xio_http_handle_t *        http_handle = user_arg;
    globus_bool_t                       done;
    globus_result_t                     eof_result = GLOBUS_SUCCESS;
    globus_i_xio_http_attr_t *          descriptor;
    globus_bool_t                       registered_again = GLOBUS_FALSE;
    GlobusXIOName(globus_i_xio_http_server_read_request_callback);

    globus_mutex_lock(&http_handle->mutex);

    if (result != GLOBUS_SUCCESS)
    {
        if (globus_xio_error_is_eof(result))
        {
            eof_result = result;
        }
        else
        {
            goto error_exit;
        }
    }

    /* Haven't parsed request and headers yet */
    http_handle->read_buffer_valid += nbytes;

    result = globus_l_xio_http_server_parse_request(http_handle, &done);
    if (result == GLOBUS_SUCCESS && !done)
    {
        goto reregister_read;
    }
    else if (result != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }

    /* Determine whether we should expect an entity along with the
     * request
     */
    if ((http_handle->request_info.http_version == GLOBUS_XIO_HTTP_VERSION_1_1)
            && (http_handle->request_info.headers.transfer_encoding
            == GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED))
    {
        http_handle->parse_state = GLOBUS_XIO_HTTP_CHUNK_LINE;
    }
    else if (GLOBUS_I_XIO_HTTP_HEADER_IS_CONTENT_LENGTH_SET(
                &http_handle->request_info.headers))
    {
        http_handle->parse_state = GLOBUS_XIO_HTTP_IDENTITY_BODY;
    }

    if (GLOBUS_I_XIO_HTTP_HEADER_IS_CONNECTION_CLOSE(
                &http_handle->request_info.headers))
    {
        http_handle->response_info.headers.flags |=
                GLOBUS_I_XIO_HTTP_HEADER_CONNECTION_CLOSE;
    }

    http_handle->send_state = GLOBUS_XIO_HTTP_STATUS_LINE;

    descriptor = globus_xio_operation_get_data_descriptor(op, GLOBUS_TRUE);
    if (descriptor == NULL)
    {
        result = GlobusXIOErrorMemory("descriptor");
        
        goto error_exit;
    }
    globus_i_xio_http_request_destroy(&descriptor->request);
    result = globus_i_xio_http_request_copy(
            &descriptor->request,
            &http_handle->request_info);
    if (result != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }

    result = globus_i_xio_http_parse_residue(http_handle, &registered_again);

    if ((http_handle->read_operation.wait_for <= 0 && !registered_again) ||
        result != GLOBUS_SUCCESS)
    {
        if (http_handle->response_info.headers.transfer_encoding !=
                GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED &&
            GLOBUS_I_XIO_HTTP_HEADER_IS_CONTENT_LENGTH_SET(
                    &http_handle->response_info.headers) &&
            http_handle->response_info.headers.content_length == 0)
        {
            /* Synthesize EOF if we've read all of the entity content */
            result = GlobusXIOErrorEOF();
        }
        /*
         * Either we've read enough, hit end of chunk, no entity was present,
         * or pass to transport failed. Call finished_read
         */
        nbytes = http_handle->read_operation.nbytes;
        globus_libc_free(http_handle->read_operation.iov);
        http_handle->read_operation.iov = NULL;
        http_handle->read_operation.iovcnt = 0;
        http_handle->read_operation.operation = NULL;
        http_handle->read_operation.driver_handle = NULL;
        http_handle->read_operation.nbytes = 0;

        globus_mutex_unlock(&http_handle->mutex);
        
        globus_xio_driver_finished_read(op, result, nbytes);

        return;
    }
    else if (registered_again)
    {
        globus_mutex_unlock(&http_handle->mutex);
        return;
    }

    /* FALLSTHROUGH */
reregister_read:
    globus_assert(op == http_handle->read_operation.operation);
    if (eof_result != GLOBUS_SUCCESS)
    {
        /* Header block wasn't complete before eof */
        result = eof_result;
        goto error_exit;
    }
    result = globus_i_xio_http_clean_read_buffer(http_handle);

    if (result != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }

    result = globus_xio_driver_pass_read(
            op,
            &http_handle->read_iovec,
            1,
            1,
            globus_i_xio_http_server_read_request_callback,
            http_handle);

    if (result != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }

    globus_mutex_unlock(&http_handle->mutex);
    return;

error_exit:
    globus_libc_free(http_handle->read_operation.iov);
    http_handle->read_operation.iov = NULL;
    http_handle->read_operation.iovcnt = 0;
    http_handle->read_operation.operation = NULL;
    http_handle->read_operation.driver_handle = NULL;
    http_handle->read_operation.nbytes = 0;
    globus_mutex_unlock(&http_handle->mutex);

    globus_xio_driver_finished_read(op, result, 0);
}
/* globus_i_xio_http_server_read_request_callback() */
