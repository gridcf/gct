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
 * @defgroup globus_i_xio_http_request Internal Request Implementation
 * @brief Internal Request Implementation
 */
#endif

/**
 * Initialize an HTTP request
 * @ingroup globus_i_xio_http_request
 *
 * All fields of this request will be set to their default values. If
 * this function returns a failure, the request should be considered
 * uninitialized.
 *
 * @param request
 *     Request structure to initialize.
 *
 * @retval GLOBUS_SUCCESS
 *     Request initialized
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Request initialization failed due to memory constraints.
 */
globus_result_t
globus_i_xio_http_request_init(
    globus_i_xio_http_request_t *       request)
{
    memset(request, '\0', sizeof(globus_i_xio_http_request_t));

    return globus_i_xio_http_header_info_init(&request->headers);
}
/* globus_i_xio_http_request_init() */

/**
 * Destroy an HTTP request
 * @ingroup globus_i_xio_http_request
 *
 * All fields of this request will be freed.
 *
 * @param request
 *     Request structure to destroy.
 *
 * @return void
 */
void
globus_i_xio_http_request_destroy(
    globus_i_xio_http_request_t *       request)
{
    globus_i_xio_http_header_info_destroy(&request->headers);

    if (request->uri != NULL)
    {
        globus_libc_free(request->uri);
        request->uri = NULL;
    }
    if (request->method != NULL)
    {
        globus_libc_free(request->method);
        request->method = NULL;
    }
    request->http_version = GLOBUS_XIO_HTTP_VERSION_UNSET;

}
/* globus_i_xio_http_request_destroy() */

/**
 * Copy the contents of an HTTP request
 * @ingroup globus_i_xio_http_request
 *
 * All values associated with the @a src request will be copied
 * to the corresponding fields of the @a dest request. If this function
 * returns a failure, then the @a dest should be considered uninitialized.
 *
 * @param dest
 *     Request to be initialized with values from src. This should
 *     not be initialized before this is called, or memory may be
 *     leaked.
 * @param src
 *     Request containing known values.
 *
 * @retval GLOBUS_SUCCESS
 *     Request successfully copied.
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Request copy failed due to memory constraints.
 */
globus_result_t
globus_i_xio_http_request_copy(
    globus_i_xio_http_request_t *       dest,
    const globus_i_xio_http_request_t * src)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusXIOName(globus_i_xio_http_request_copy);

    if (src->uri == NULL)
    {
        dest->uri = NULL;
    }
    else
    {
        dest->uri = globus_libc_strdup(src->uri);
        if (dest->uri == NULL)
        {
            res = GlobusXIOErrorMemory("uri");

            goto error_exit;
        }
    }

    if (src->method == NULL)
    {
        dest->method = NULL;
    }
    else
    {
        dest->method = globus_libc_strdup(src->method);
        if (dest->method == NULL)
        {
            res = GlobusXIOErrorMemory("method");

            goto free_uri_exit;
        }
    }

    dest->http_version = src->http_version;

    res = globus_i_xio_http_header_info_copy(
            &dest->headers,
            &src->headers);

    if (res != GLOBUS_SUCCESS)
    {
        goto free_method_exit;
    }

    return res;

free_method_exit:
    if (dest->method)
    {
        globus_libc_free(dest->method);
        dest->method = NULL;
    }
free_uri_exit:
    if (dest->uri)
    {
        globus_libc_free(dest->uri);
        dest->uri = NULL;
    }
error_exit:
    return res;
}
/* globus_i_xio_http_request_copy() */
