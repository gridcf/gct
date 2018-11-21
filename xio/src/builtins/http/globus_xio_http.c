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

#include "version.h"

#include <string.h>

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_xio_http.c
 * @brief Globus XIO HTTP Driver
 */

/**
 * @defgroup globus_i_xio_http_util Internal HTTP Utility Functions
 * @brief Internal HTTP Utility Functions
 */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

static
int
globus_l_xio_http_activate(void);

static
int
globus_l_xio_http_deactivate(void);

GlobusXIODefineModule(http) =
{
    "globus_xio_http",                  /*module name*/
    globus_l_xio_http_activate,         /*activate*/
    globus_l_xio_http_deactivate,       /*deactivate*/
    GLOBUS_NULL,                        /*at exit*/
    GLOBUS_NULL,                        /*get pointer*/
    &local_version                      /*version*/
};

static
globus_result_t
globus_l_xio_http_init(
    globus_xio_driver_t *               out_driver)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     res;

    res = globus_xio_driver_init(&driver, "http", NULL);
    if(res != GLOBUS_SUCCESS)
    {
        return res;
    }

    globus_xio_driver_set_transform(
        driver,
        globus_i_xio_http_open,
        globus_i_xio_http_close,
        globus_i_xio_http_read,
        globus_i_xio_http_write,
        globus_i_xio_http_handle_cntl,
	NULL);

    globus_xio_driver_set_server(
        driver,
        NULL,
        globus_i_xio_http_accept,
        NULL,
        NULL,
        NULL,
        globus_i_xio_http_target_destroy);

    globus_xio_driver_set_attr(
            driver,
            globus_i_xio_http_attr_init,
            globus_i_xio_http_attr_copy,
            globus_i_xio_http_attr_cntl,
            globus_i_xio_http_attr_destroy);

    *out_driver = driver;

    return GLOBUS_SUCCESS;
}
/* globus_l_xio_http_init() */

static
void
globus_l_xio_http_destroy(
    globus_xio_driver_t                 driver)
{
    globus_xio_driver_destroy(driver);
}
/* globus_l_xio_http_destroy() */

/**
 * Copy a block of data into an iovec fifo.
 *
 * @param iovecs
 *     A fifo of iovec pointers. Each iovec will be GLOBUS_XIO_HTTP_CHUNK_SIZE
 *     bytes long. If the last one in the fifo is full, a new one
 *     will be allocated from a memory pool and used. iovecs in this
 *     fifo must be freed by calling globus_libc_free().
 * @param data
 *     Pointer to a data block.
 * @param datalen
 *     Length of the data block.
 */
globus_result_t
globus_i_xio_http_copy_blob(
    globus_fifo_t *                     iovecs,
    const char *                        data,
    size_t                              datalen)
{
    globus_xio_iovec_t *                iov = NULL;
    size_t                              to_copy;
    GlobusXIOName(globus_l_http_copy_blob);

    if (globus_fifo_size(iovecs) > 0)
    {
        iov = globus_fifo_tail_peek(iovecs);
    }

    while (datalen > 0)
    {
        if (iov == NULL || iov->iov_len == GLOBUS_XIO_HTTP_CHUNK_SIZE)
        {
            iov = globus_libc_malloc(sizeof(globus_xio_iovec_t));

            if (iov == NULL)
            {
                return GlobusXIOErrorMemory("iovec");
            }

            iov->iov_base = globus_libc_malloc(GLOBUS_XIO_HTTP_CHUNK_SIZE);
            if (iov->iov_base == NULL)
            {
                globus_free(iov);
                return GlobusXIOErrorMemory("iovec.iov_base");
            }
            iov->iov_len = 0;

            globus_fifo_enqueue(iovecs, iov);
        }

        to_copy = GLOBUS_XIO_HTTP_CHUNK_SIZE - iov->iov_len;

        if (datalen < to_copy)
        {
            to_copy = datalen;
        }

        memcpy((char *) iov->iov_base + iov->iov_len, data, to_copy);

        iov->iov_len += to_copy;
        datalen -= to_copy;
        data += to_copy;
    }
    return GLOBUS_SUCCESS;
}
/* globus_l_xio_http_copy_blob() */

/**
 * Determine whether an HTTP method should contain an entity.
 * @ingroup globus_i_xio_http_util
 *
 * Based on the information in RFC 2616, determine whether the method
 * should contain an HTTP entity body along with it. This information is
 * used to determine whether the client should be required to explicitly
 * call the GLOBUS_XIO_HTTP_HANDLE_SET_END_OF_ENTITY handle control.
 *
 * @param method
 *     HTTP Method name
 *
 * @retval GLOBUS_TRUE
 *     The client's HTTP method requires an entity-body by default.
 * @retval GLOBUS_FALSE
 *     The client's HTTP method does not require an entity-body by default.
 */
globus_bool_t
globus_i_xio_http_method_requires_entity(
    const char *                        method)
{
    char *                              methods_with_entity[] =
    {
        "OPTIONS",
        "POST",
        "PUT"
    };
    int                                 i;

    for (i = 0; i < GLOBUS_XIO_ARRAY_LENGTH(methods_with_entity); i++)
    {
        if (strcmp(method, methods_with_entity[i]) == 0)
        {
            return GLOBUS_TRUE;
        }
    }
    return GLOBUS_FALSE;
}
/* globus_i_xio_http_method_requires_entity() */

/**
 * Choose HTTP version based on major and minor version numbers
 * @ingroup globus_i_xio_http_util
 *
 * Determine from an HTTP major and minor version which
 * #globus_xio_http_version_t best describes the HTTP version.
 *
 * @param http_major
 *     Major version to use for the basis of our guess.
 * @param http_minor
 *     Minor version to use for the basis of our guess.
 *
 * @retval GLOBUS_XIO_HTTP_VERSION_1_0
 *     The version number is definitely less than 1.1
 * @retval GLOBUS_XIO_HTTP_VERSION_1_1
 *     The version number is definitely greater than 1.0
 */
globus_xio_http_version_t
globus_i_xio_http_guess_version(
    int                                 http_minor,
    int                                 http_major)
{
    if (http_major > 1)
    {
        /* Unknown major version, assume compatible with 1.1 */
        return GLOBUS_XIO_HTTP_VERSION_1_1;
    }
    else if (http_major < 1)
    {
        /* Unknown major version, assume compatible with 1.0 */
        return GLOBUS_XIO_HTTP_VERSION_1_0;
    }
    else if (http_minor == 0)
    {
        /* definitely 1.0 */
        return GLOBUS_XIO_HTTP_VERSION_1_0;
    }
    else
    {
        /* definitely 1.1 */
        return GLOBUS_XIO_HTTP_VERSION_1_1;
    }
}
/* globus_i_xio_http_guess_version() */

/**
 * Locate the next CRLF sequence in a byte array
 * @ingroup globus_i_xio_http_util
 *
 * Finds the next CRLF sequence in a byte array, returning a pointer to
 * the beginning of the sequence.
 *
 * @param blob
 *     Character array to search through.
 * @param blob_length
 *     Length of valid data in the @a blob array.
 *
 * @return
 *     This function returns either a pointer to the beginning of the CRLF
 *     sequence or NULL, if no such sequence was found.
 */
char *
globus_i_xio_http_find_eol(
    const char *                        blob,
    globus_size_t                       blob_length)
{
    char *                              result;
    globus_size_t                       skip = 0;

    while (((skip + 1) < blob_length) && 
        (result = memchr(blob + skip, '\r', blob_length-skip)) != NULL)
    {
        if (result+1 == (blob + skip + blob_length))
        {
            return NULL;
        }
        else if (*(result+1) == '\n')
        {
            return result;
        }
        else 
        {
            skip += (result - blob + 1);
        }
    }
    return NULL;
}
/* globus_i_xio_http_find_eol() */

/**
 * Prepare the read buffer of a http handle for a new read
 * @ingroup globus_i_xio_http_util
 *
 * Resets the read_buffer-related fields of the handle so that another
 * read can be passed with it. The read_buffer may be extended if it is
 * currently full.
 *
 * @param http_handle
 *     Handle to modify.
 *
 * @retval GLOBUS_SUCCESS
 *    Some space was freed up in the read buffer
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *    Unable to resize the read buffer to allow more data in.
 */
globus_result_t
globus_i_xio_http_clean_read_buffer(
    globus_i_xio_http_handle_t *        http_handle)
{
    globus_result_t                     result;
    GlobusXIOName(globus_i_xio_http_clean_read_buffer);

    if (http_handle->read_buffer_valid == 0)
    {
        /* Nothing in buffer, can reuse in its entirety */
        http_handle->read_buffer_offset = 0;
    }
    else if (http_handle->read_buffer_valid < http_handle->read_buffer.iov_len)
    {
        /* Something in buffer, but there's some slack left, shift to beginning
         * and reuse part of the buffer
         */
        memmove(http_handle->read_buffer.iov_base,
                (char *) http_handle->read_buffer.iov_base
                    + http_handle->read_buffer_offset,
                http_handle->read_buffer_valid);
        http_handle->read_buffer_offset = 0;
    }
    else
    {
        /* Buffer is totally full, extend it */
        void * tmp_buf = http_handle->read_buffer.iov_base;

        http_handle->read_buffer.iov_base = globus_libc_realloc(
                http_handle->read_buffer.iov_base,
                http_handle->read_buffer.iov_len + GLOBUS_XIO_HTTP_CHUNK_SIZE);

        if (http_handle->read_buffer.iov_base == NULL)
        {
            http_handle->read_buffer.iov_base = tmp_buf;

            result = GlobusXIOErrorMemory("read_buffer");

            goto error_exit;
        }
        http_handle->read_buffer.iov_len += GLOBUS_XIO_HTTP_CHUNK_SIZE;
    }

    /*
     * Set read iovec to point to part of the buffer without any
     * data in it.
     */
    http_handle->read_iovec.iov_base = (char *)
        http_handle->read_buffer.iov_base
            + http_handle->read_buffer_offset
            + http_handle->read_buffer_valid;

    http_handle->read_iovec.iov_len = 
        http_handle->read_buffer.iov_len
        - http_handle->read_buffer_offset
        - http_handle->read_buffer_valid;

    return GLOBUS_SUCCESS;
error_exit:
    return result;
}
/* globus_i_xio_http_cleanup_read_buffer() */

GlobusXIODefineDriver(
    http,
    globus_l_xio_http_init,
    globus_l_xio_http_destroy);

static
int
globus_l_xio_http_activate(void)
{
    int                                 rc;

    rc = globus_module_activate(GLOBUS_XIO_MODULE);

    globus_mutex_init(&globus_i_xio_http_cached_handle_mutex, NULL);
    globus_mutex_init(&globus_i_xio_http_cancel_mutex, NULL);
    globus_i_xio_http_cached_handles = NULL;
    globus_i_xio_http_cancellable_handles = NULL;
    if(rc == GLOBUS_SUCCESS)
    {
        GlobusXIORegisterDriver(http);
    }
    return rc;
}

static
int
globus_l_xio_http_deactivate(void)
{
    globus_i_xio_http_handle_t *        http_handle;
    globus_result_t                     result;
    
    GlobusXIOUnRegisterDriver(http);
    
    globus_mutex_lock(&globus_i_xio_http_cached_handle_mutex);
    while (!globus_list_empty(globus_i_xio_http_cached_handles))
    {
        http_handle = globus_list_remove(
                &globus_i_xio_http_cached_handles,
                globus_i_xio_http_cached_handles);

        result = globus_xio_driver_operation_create(
                &http_handle->close_operation,
                http_handle->handle);

        globus_assert(result == GLOBUS_SUCCESS);
        http_handle->user_close = GLOBUS_FALSE;

        result = globus_i_xio_http_close_internal(http_handle);

        globus_assert(result == GLOBUS_SUCCESS);
    }
    globus_mutex_unlock(&globus_i_xio_http_cached_handle_mutex);
    globus_mutex_destroy(&globus_i_xio_http_cached_handle_mutex);
    globus_mutex_destroy(&globus_i_xio_http_cancel_mutex);

    return globus_module_deactivate(GLOBUS_XIO_MODULE);
}
