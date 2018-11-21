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
 * @defgroup globus_i_xio_http_response Internal Response Implementation
 * @brief Internal Response Implementation
 */
#endif

/**
 * Initialize an HTTP response
 * @ingroup globus_i_xio_http_response
 *
 * All fields of this response will be set to their default values. If
 * this function returns a failure, the response should be considered
 * uninitialized.
 *
 * @param response
 *     Response structure to initialize.
 *
 * @return
 *     This function returns GLOBUS_SUCCESS, or a result from
 *     globus_i_xio_http_header_info_init().
 * @retval GLOBUS_SUCCESS
 *     Response initialized
 */
globus_result_t
globus_i_xio_http_response_init(
    globus_i_xio_http_response_t *      response)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusXIOName(globus_i_xio_http_response_init);

    memset(response, '\0', sizeof(globus_i_xio_http_response_t));

    res = globus_i_xio_http_header_info_init(&response->headers);

    response->status_code = 200;

    return res;
}
/* globus_i_xio_http_response_init() */

/**
 * Destroy an HTTP response
 * @ingroup globus_i_xio_http_response
 *
 * All fields of this response will be freed.
 *
 * @param response
 *     Response structure to destroy.
 *
 * @return void
 */
void
globus_i_xio_http_response_destroy(
    globus_i_xio_http_response_t *      response)
{
    response->status_code = 0;
    if (response->reason_phrase != NULL)
    {
        globus_libc_free(response->reason_phrase);
        response->reason_phrase = NULL;
    }

    response->http_version = GLOBUS_XIO_HTTP_VERSION_UNSET;

    globus_i_xio_http_header_info_destroy(&response->headers);
}
/* globus_i_xio_http_response_destroy() */

/**
 * Copy the contents of an HTTP response
 * @ingroup globus_i_xio_http_response
 *
 * All values associated with the @a src response will be copied
 * to the corresponding fields of the @a dest response. If this function
 * returns a failure, then the @a dest should be considered uninitialized.
 *
 * @param dest
 *     Response to be initialized with values from src. This should
 *     not be initialized before this is called, or memory may be
 *     leaked.
 * @param src
 *     Response containing known values.
 *
 * @retval GLOBUS_SUCCESS
 *     Response successfully copied.
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Response copy failed due to memory constraints.
 */
globus_result_t
globus_i_xio_http_response_copy(
    globus_i_xio_http_response_t *      dest,
    const globus_i_xio_http_response_t *src)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusXIOName(globus_i_xio_http_response_copy);

    dest->status_code = src->status_code;

    if (src->reason_phrase == NULL)
    {
        dest->reason_phrase = NULL;
    }
    else
    {
        dest->reason_phrase = globus_libc_strdup(src->reason_phrase);
        if (dest->reason_phrase == NULL)
        {
            res = GlobusXIOErrorMemory("reason_phrase");

            goto error_exit;
        }
    }

    dest->http_version = src->http_version;

    res = globus_i_xio_http_header_info_copy(
            &dest->headers,
            &src->headers);

    if (res != GLOBUS_SUCCESS)
    {
        goto free_reason_phrase_exit;
    }

    return res;

free_reason_phrase_exit:
    if (dest->reason_phrase != NULL)
    {
        globus_libc_free(dest->reason_phrase);
        dest->reason_phrase = NULL;
    }
error_exit:
    return res;
}
/* globus_i_xio_http_response_copy() */
