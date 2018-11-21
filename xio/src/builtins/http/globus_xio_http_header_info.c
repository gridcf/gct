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
 * @defgroup globus_i_xio_http_header_info Internal Header Information Implementation
 * @brief Internal Header Information Implementation
 */
#endif

/**
 * Initialize an HTTP header info structure
 * @ingroup globus_i_xio_http_header_info
 *
 * All fields of the @a header_info structure will be initialized to their
 * default values. If this function returns a failre, the header_info
 * should be considered uninitialized.
 *
 * @param header_info
 *     Header information structure to initialize.
 *
 * @retval GLOBUS_SUCCESS
 *     Header initialized successfully.
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Initialization failed due to memory constraints.
 */
globus_result_t
globus_i_xio_http_header_info_init(
    globus_i_xio_http_header_info_t *   header_info)
{
    int                                 rc;
    globus_result_t                     result = GLOBUS_SUCCESS;
    GlobusXIOName(globus_i_xio_http_header_info_init);

    memset(header_info, '\0', sizeof(globus_i_xio_http_header_info_t));

    rc = globus_hashtable_init(
            &header_info->headers,
            16,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);

    if (rc != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorMemory("hashtable");
    }
    return result;
}
/* globus_i_xio_http_header_info_init() */

/**
 * Destroy a header info structure
 * @ingroup globus_i_xio_http_header_info
 *
 * All fields of this header information structure will be freed.
 *
 * @param header_info
 *     Header information structure to destroy.
 *
 * @return void
 */
void
globus_i_xio_http_header_info_destroy(
    globus_i_xio_http_header_info_t *   header_info)
{
    globus_hashtable_destroy_all(
            &header_info->headers,
            globus_i_xio_http_header_destroy);
}
/* globus_i_xio_http_header_info_destroy() */

/**
 * Copy the contents of a header information structure.
 * @ingroup globus_i_xio_http_header_info
 *
 * All values associated with the @a src header information structure will
 * be copied to the @a dest one. If this function returns a failure, then the
 * @a dest structure should be considered uninitialized.
 *
 * @param dest
 *     Header information structure to initialize. This should not be
 *     initialized before this function is called, or memory may be leaked.
 * @param src
 *     Header information structure containing valid values.
 *
 * @retval GLOBUS_SUCCESS
 *     Structure successfully copied.
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Copy failed due to memory constraints.
 */
globus_result_t
globus_i_xio_http_header_info_copy(
    globus_i_xio_http_header_info_t *   dest,
    const globus_i_xio_http_header_info_t *
                                        src)
{
    int                                 rc;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GlobusXIOName(globus_i_xio_http_header_info_init);
    rc = globus_hashtable_copy(
            &dest->headers,
            (globus_hashtable_t *) &src->headers,
            globus_i_xio_http_header_copy);
    if (rc != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorMemory("hashtable");

        goto error_exit;
    }

    dest->content_length = src->content_length;
    dest->transfer_encoding = src->transfer_encoding;
    dest->flags = src->flags;

    return result;

error_exit:
    return result;
}
/* globus_i_xio_http_header_info_copy() */

/**
 * Set the value of a header in a hashtable
 * @ingroup globus_i_xio_http_header
 *
 * Adds a new header to a header info structure, or updates the value of an
 * existing header. Copies of the name and value will be stored in a
 * #globus_xio_http_header_t in a hashtable in the header info structure.
 * 
 * @param headers
 *     Pointer to the header info structure.
 * @param header_name
 *     Name of the header.
 * @param header_value
 *     Value of the header.
 * @param store_all
 *     Set to GLOBUS_TRUE to add all entries to the hashtable, otherwise
 *     Content-Length, Transfer-Encoding, and Connection are discarded.
 *     You probably only want to do this when acting as a client and parsing
 *     the server response.
 *
 * @retval GLOBUS_SUCCESS
 *     Header successfully added to the hashtable.
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Unable to add header due to memory constraints.
 */
globus_result_t
globus_i_xio_http_header_info_set_header(
    globus_i_xio_http_header_info_t *   headers,
    const char *                        header_name,
    const char *                        header_value,
    globus_bool_t                       store_all)
{
    char *                              save_header;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_xio_http_header_t *          header;
    int                                 rc;
    globus_off_t                        length;
    globus_bool_t                       store;
    GlobusXIOName(globus_l_xio_http_header_set);

    store = store_all;
    /* Special cases for entity-body handling headers */
    if (strcasecmp(header_name, "Content-Length") == 0)
    {
        rc = globus_libc_scan_off_t(header_value, &length, NULL);
        if (rc < 1)
        {
            result = GlobusXIOHttpErrorInvalidHeader(header_name, header_value);

            goto error_exit;
        }
        headers->content_length = length;
        headers->flags |= GLOBUS_I_XIO_HTTP_HEADER_CONTENT_LENGTH_SET;
    }
    else if (strcasecmp(header_name, "Transfer-Encoding") == 0)
    {
        if (strcasecmp(header_value, "identity") == 0)
        {
            headers->transfer_encoding =
                GLOBUS_XIO_HTTP_TRANSFER_ENCODING_IDENTITY;
        }
        else if (strcasecmp(header_value, "chunked") == 0)
        {
            headers->transfer_encoding =
                GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED;
        }
        else
        {
            result = GlobusXIOHttpErrorInvalidHeader(header_name, header_value);

            goto error_exit;
        }
    }
    else if (strcasecmp(header_name, "Connection") == 0)
    {
        if (strcasecmp(header_value, "close") == 0)
        {
            headers->flags |= GLOBUS_I_XIO_HTTP_HEADER_CONNECTION_CLOSE;
        }
        else if (strcasecmp(header_value, "keep-alive") == 0)
        {
            headers->flags &= ~GLOBUS_I_XIO_HTTP_HEADER_CONNECTION_CLOSE;
        }
        else
        {
            result = GlobusXIOHttpErrorInvalidHeader(header_name, header_value);

            goto error_exit;
        }
    }
    else
    {
        store = GLOBUS_TRUE;
    }
    
    if(store)
    {
        /*
         * Either modify the header's value in the hashtable, if it's a
         * duplicate, or create a new entry in the hashtable
         */
        header = globus_hashtable_lookup(
                &headers->headers,
                (void *) header_name);

        if (header != NULL)
        {
            /* Replace current header's value */
            save_header = header->value;

            header->value = globus_libc_strdup(header_value);

            if (header->value == NULL)
            {
                header->value = save_header;

                result = GlobusXIOErrorMemory("header");

                goto error_exit;
            }
            globus_libc_free(save_header);
        }
        else
        {
            header = globus_libc_malloc(sizeof(globus_xio_http_header_t));

            if (header == NULL)
            {
                result = GlobusXIOErrorMemory("header");

                goto error_exit;
            }
            header->name = globus_libc_strdup(header_name);

            if (header->name == NULL)
            {
                result = GlobusXIOErrorMemory("header");
                goto free_header_exit;
            }

            header->value = globus_libc_strdup(header_value);

            if (header->value == NULL)
            {
                result = GlobusXIOErrorMemory("header");
                goto free_header_name_exit;
            }

            rc = globus_hashtable_insert(
                    &headers->headers,
                    header->name,
                    header);

            if (rc != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorMemory("header");

                goto free_header_value_exit;
            }
        }
    }
    return result;

free_header_value_exit:
    globus_libc_free(header->value);
free_header_name_exit:
    globus_libc_free(header->name);
free_header_exit:
    globus_libc_free(header);
error_exit:
    return result;
}
/* globus_l_xio_http_header_info_set_header() */
