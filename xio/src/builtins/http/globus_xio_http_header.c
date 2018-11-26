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
 * @defgroup globus_i_xio_http_header Internal Header Implementation
 * @brief Internal Header Implementation
 */
#endif

/**
 * Destroy an HTTP Header
 * @ingroup globus_i_xio_http_header
 *
 * Frees all storage associated with an HTTP header. This function should
 * be passed to globus_hashtable_destroy_all() to free the key and value
 * of the headers in a hashtable.
 *
 * @param header
 *     A <code>void *</code> pointing to the header to be freed.
 *
 * @return void
 */
extern
void
globus_i_xio_http_header_destroy(
    void *                              header)
{
    globus_xio_http_header_t *          http_header = header;

    if (http_header->name != NULL)
    {
        globus_libc_free(http_header->name);
    }
    if (http_header->value != NULL)
    {
        globus_libc_free(http_header->value);
    }
    globus_libc_free(http_header);
}
/* globus_i_xio_http_header_destroy() */

/**
 * Copy an HTTP header
 * @ingroup globus_i_xio_http_header
 *
 * Makes a new copy of an http header. This function is used in conjunction
 * with globus_hashtable_copy() to generate a duplicate of a HTTP header
 * hashtable.
 *
 * @param dest_key
 *     Pointer to a <code>void *</code> which will be set to the HTTP header
 *     name.
 * @param dest_datum
 *     Pointer to a <code>void *</code> which will be set to point to
 *     the HTTP header structure.
 * @param src_key
 *     A <code>void *</code> pointing to the HTTP header name in the original
 *     hashtable. This is ignored, as the name is stored in the HTTP header
 *     structure in @a src_datum.
 * @param src_datum
 *     A <code>void *</code> pointing to the HTTP header structure in the
 *     original hashtable to copy.
 *
 * @return void
 */
extern
void
globus_i_xio_http_header_copy(
    void **                             dest_key,
    void **                             dest_datum,
    void *                              src_key,
    void *                              src_datum)
{
    globus_xio_http_header_t *          dest_header;
    globus_xio_http_header_t *          src_header = src_datum;

    dest_header = globus_libc_malloc(sizeof(globus_xio_http_header_t));
    globus_assert(dest_header != NULL);

    dest_header->name = globus_libc_strdup(src_header->name);
    globus_assert(dest_header->name);

    dest_header->value = globus_libc_strdup(src_header->value);
    globus_assert(dest_header->value);

    *dest_key = dest_header->name;
    *dest_datum = dest_header;
}
/* globus_i_xio_http_header_copy() */

/**
 * Parse HTTP headers
 * @ingroup globus_i_xio_http_header
 *
 * Parse the header block of an HTTP request or response. If the entire header
 * section is parsed, then the boolean pointed to by @a done will be modified
 * to be GLOBUS_TRUE.
 *
 * @param http_handle
 *     HTTP handle containing the buffer to be parsed.
 * @param done
 *     Pointer to flag indicating whether the header parsing completed.
 *
 * @retval GLOBUS_SUCCESS
 *     No parsing errors occurred. More data may be required to finish parsing
 *     the headers, depending on the final value of the boolean pointed to
 *     by @a done.
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Parsing failed because of insufficient memory.
 * @retval <driver>::GLOBUS_XIO_HTTP_ERROR_PARSE
 *     Error parsing a header.
 */
globus_result_t
globus_i_xio_http_header_parse(
    globus_i_xio_http_handle_t *        http_handle,
    globus_bool_t *                     done)
{
    globus_result_t                     result;
    char *                              eol;
    char *                              current_offset;
    int                                 parsed;
    globus_i_xio_http_header_info_t *   headers;
    char *                              header_name;
    char *                              header_value;
    int                                 rc;

    GlobusXIOName(globus_i_xio_http_header_parse);

    if (http_handle->target_info.is_client)
    {
        headers = &http_handle->response_info.headers;
    }
    else
    {
        headers = &http_handle->request_info.headers;
    }

    current_offset = ((char *) (http_handle->read_buffer.iov_base))
            + http_handle->read_buffer_offset;

    /*
     * While we find a non-empty line, we are in the header block.
     */
    while ((eol = globus_i_xio_http_find_eol(
                current_offset,
                http_handle->read_buffer_valid)) != current_offset)
    {
        if (eol == NULL)
        {
            /* Full headers not present */
            *done = GLOBUS_FALSE;

            return GLOBUS_SUCCESS;
        }

        if ((eol - current_offset + 2) < http_handle->read_buffer_valid)
        {
            /* Peek ahead for LWS for multi-line headers */
            if (*(eol+2) == ' ' || *(eol+2) == '\t')
            {
                /*
                 * first char after eol is space or tab---convert CRLF
                 * into space and try again.
                 */
                *(eol) = ' ';
                *(eol+1) = ' ';
                continue;
            }
        }
        *eol = '\0';

        /* Find end of header name */
        rc = sscanf(current_offset, "%*[^: \t\r\n]%n", &parsed);
        if (rc < 0)
        {
            result = GlobusXIOHttpErrorParse("field-name", current_offset);

            goto error_exit;
        }
        header_name = current_offset;

        if (*(current_offset+parsed) != ':')
        {
            result = GlobusXIOHttpErrorParse("field-name", current_offset);

            goto error_exit;
        }
        /* replace : with '\0' */
        *(current_offset+parsed) = '\0';
        parsed++;

        current_offset += parsed;

        /* Find end of post-colon whitespace */
        rc = sscanf(current_offset, " %n", &parsed);
        if (rc < 0)
        {
            result = GlobusXIOHttpErrorParse("header-value", current_offset);

            goto error_exit;
        }

        /* Skip leading whitespace */
        current_offset += parsed;
        header_value = current_offset;

        /* skip past \r\n */
        current_offset = eol + 2;

        parsed = current_offset - ((char *) http_handle->read_buffer.iov_base
                + http_handle->read_buffer_offset);
        http_handle->read_buffer_valid -= parsed;
        http_handle->read_buffer_offset += parsed;

        /* Actually deal with the header */
        result = globus_i_xio_http_header_info_set_header(
                headers,
                header_name,
                header_value,
                http_handle->target_info.is_client);

        if (result != GLOBUS_SUCCESS)
        {
            goto error_exit;
        }

    }

    if (eol != NULL)
    {
        /* We found an empty line----end of headers found. */
        *done = GLOBUS_TRUE;
        current_offset = eol+2;

        parsed = current_offset - ((char *) http_handle->read_buffer.iov_base
                + http_handle->read_buffer_offset);
        http_handle->read_buffer_valid -= parsed;
        http_handle->read_buffer_offset += parsed;

        /* Decide how we will handle the next data coming on the stream.
         *
         * If we are chunked, a client of an HTTP/1.0 server, or have a content
         * length header, then we should expect an entity body. Otherwise,
         * the headers are the end of the message.
         */
        if (headers->transfer_encoding
                == GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED)
        {
            http_handle->parse_state = GLOBUS_XIO_HTTP_CHUNK_LINE;
        }
        else if ((http_handle->target_info.is_client && 
                http_handle->response_info.http_version
                    == GLOBUS_XIO_HTTP_VERSION_1_0) ||
                GLOBUS_I_XIO_HTTP_HEADER_IS_CONTENT_LENGTH_SET(headers))
        {
            http_handle->parse_state = GLOBUS_XIO_HTTP_IDENTITY_BODY;
        }
        else
        {
            http_handle->parse_state = GLOBUS_XIO_HTTP_EOF;
        }
    }
    else
    {
        /* We ran out of lines before finishing parsing */
        *done = GLOBUS_FALSE;
    }

    return GLOBUS_SUCCESS;

error_exit:
    parsed = current_offset - ((char *) http_handle->read_buffer.iov_base
            + http_handle->read_buffer_offset);

    /* Chop off what we managed to parse from the buffer */
    http_handle->read_buffer_valid -= parsed;
    http_handle->read_buffer_valid += parsed;

    return result;
}
/* globus_i_xio_http_header_parse() */
