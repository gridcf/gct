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
 * @defgroup globus_i_xio_http_attr Internal Attribute Implementation
 * @brief Internal Attribute Implementation
 */
#endif

/**
 * Allocate and initialize an HTTP attribute
 * @ingroup globus_i_xio_http_attr
 *
 * Creates a new attribute with default values. This is called by the XIO
 * driver via globus_xio_attr_init().
 *
 * @param out_attr
 *     Pointer value will be set to point to a 
 *     newly allocated and initilized #globus_i_xio_http_attr_t
 *     structure.
 *
 * @retval GLOBUS_SUCCESS
 *     Attribute successfully initialized.
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Initialization failed due to memory constraints.
 *
 * @see globus_i_xio_http_attr_destroy()
 */
globus_result_t
globus_i_xio_http_attr_init(
    void **                             out_attr)
{
    globus_result_t                     res;
    globus_i_xio_http_attr_t *          attr;
    GlobusXIOName(globus_i_xio_http_attr_init);

    attr = globus_libc_malloc(sizeof(globus_i_xio_http_attr_t));
    if (attr == NULL)
    {
        res = GlobusXIOErrorMemory("attr");

        goto error_exit;
    }

    res = globus_i_xio_http_request_init(&attr->request);

    if (res != GLOBUS_SUCCESS)
    {
        goto free_attr_exit;
    }
    res = globus_i_xio_http_response_init(&attr->response);

    if (res != GLOBUS_SUCCESS)
    {
        goto free_request_exit;
    }
    attr->delay_write_header = GLOBUS_FALSE;

    *out_attr = attr;
    return GLOBUS_SUCCESS;

free_request_exit:
    globus_i_xio_http_request_destroy(&attr->request);
free_attr_exit:
    globus_libc_free(attr);
error_exit:
    return res;
}
/* globus_i_xio_http_attr_init() */

/**
 * Modify the state of an HTTP attribute
 * @ingroup globus_i_xio_http_attr
 *
 * Modify the state of an attribute. This is called by the XIO driver via
 * globus_xio_attr_cntl().
 *
 * @param driver_attr
 *     Void pointer to a #globus_i_xio_http_attr_t structure containing
 *     the attribute's values.
 * @param cmd
 *     Integer value indicating which attribute will be changed. Valid
 *     commands values are in the set defined by  #globus_xio_http_attr_cmd_t
 * @param ap
 *     Variable-length argument list containing any cmd-specific parameters.
 *
 * @retval GLOBUS_SUCCESS
 *     Attribute successfully modified.
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Attribute control failed due to memory constraints.
 * @retval GLOBUS_XIO_ERROR_PARAMETER
 *     Invalid @a cmd parameter or invalid value for cmd-specific parameters
 *     in @a ap.
 */
globus_result_t
globus_i_xio_http_attr_cntl(
    void *                              driver_attr,
    int                                 cmd,
    va_list                             ap)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_i_xio_http_attr_t *          attr = driver_attr;
    char *                              in_string;
    char *                              save_string;
    globus_xio_http_version_t           in_http_version;
    char *                              in_header_name;
    char *                              in_header_value;
    char **                             out_method;
    char **                             out_uri;
    globus_xio_http_version_t *         out_http_version;
    globus_hashtable_t *                out_headers;
    int *                               out_status_code;
    char **                             out_reason_phrase;

    GlobusXIOName(globus_i_xio_http_attr_cntl);

    switch (cmd)
    {
        case GLOBUS_XIO_HTTP_ATTR_SET_REQUEST_METHOD:
            save_string = attr->request.method;

            in_string = va_arg(ap, char *);

            if (in_string == NULL)
            {
                res = GlobusXIOErrorParameter("method");
                break;
            }

            attr->request.method = globus_libc_strdup(in_string);
            if (attr->request.method == NULL)
            {
                attr->request.method = save_string;
                res = GlobusXIOErrorMemory("method");
                break;
            }

            if (save_string != NULL)
            {
                globus_libc_free(save_string);
            }
            break;

        case GLOBUS_XIO_HTTP_ATTR_SET_REQUEST_HTTP_VERSION:
            in_http_version = va_arg(ap, globus_xio_http_version_t);

            if (in_http_version != GLOBUS_XIO_HTTP_VERSION_1_0 &&
                    in_http_version != GLOBUS_XIO_HTTP_VERSION_1_1)
            {
                res = GlobusXIOErrorParameter("version");
                break;
            }
            attr->request.http_version = in_http_version;
            break;

        case GLOBUS_XIO_HTTP_ATTR_SET_REQUEST_HEADER:
            in_header_name = va_arg(ap, char *);
            if (in_header_name == NULL)
            {
                res = GlobusXIOErrorParameter("name");
                break;
            }

            in_header_value = va_arg(ap, char *);
            if (in_header_value == NULL)
            {
                res = GlobusXIOErrorParameter("value");
                break;
            }

            res = globus_i_xio_http_header_info_set_header(
                    &attr->request.headers,
                    in_header_name,
                    in_header_value,
                    GLOBUS_FALSE);
            break;

        case GLOBUS_XIO_HTTP_ATTR_DELAY_WRITE_HEADER:
            attr->delay_write_header = 1;
            break;

        case GLOBUS_XIO_HTTP_GET_REQUEST:
            out_method = va_arg(ap, char **);
            out_uri = va_arg(ap, char **);
            out_http_version = va_arg(ap, globus_xio_http_version_t *);
            out_headers = va_arg(ap, globus_hashtable_t *);

            if (out_method != NULL)
            {
                *out_method = attr->request.method;
            }
            if (out_uri != NULL)
            {
                *out_uri = attr->request.uri;
            }
            if (out_http_version != NULL)
            {
                *out_http_version = attr->request.http_version;
            }
            if (out_headers != NULL)
            {
                *out_headers = attr->request.headers.headers;
            }
            break;
        case GLOBUS_XIO_HTTP_GET_RESPONSE:
            out_status_code = va_arg(ap, int *);
            out_reason_phrase = va_arg(ap, char **);
            out_http_version = va_arg(ap, globus_xio_http_version_t *);
            out_headers = va_arg(ap, globus_hashtable_t *);

            if (out_status_code != NULL)
            {
                *out_status_code = attr->response.status_code;
            }
            if (out_reason_phrase != NULL)
            {
                *out_reason_phrase = attr->response.reason_phrase;
            }
            if (out_http_version != NULL)
            {
                *out_http_version = attr->response.http_version;
            }
            if (out_headers != NULL)
            {
                *out_headers = attr->response.headers.headers;
            }
            break;
        default:
            res = GlobusXIOErrorParameter("cmd");
    }

    return res;
}
/* globus_i_xio_http_attr_cntl() */

/** Copy an HTTP attribute
 * @ingroup globus_i_xio_http_attr
 *
 * Copies all values associated with the @a src http attribute to
 * a newly allocated attribute in @a dst. If this function returns a
 * failure, then the @a dst should be considered uninitiailized.  This is
 * called by the XIO driver via globus_xio_attr_copy().
 *
 * @param dst
 *     Void ** which will be set to point to a newly allocated attribute
 *     with equivalent values to those in @a src.
 * @param src
 *     Void * pointing to a #globus_i_xio_http_attr_t which contains the
 *     attributes we want to copy.
 *
 * @return
 *     This function returns GLOBUS_SUCCESS or GLOBUS_XIO_ERROR_MEMORY itself.
 *     Other errors generated by globus_i_xio_http_request_copy() may be
 *     returned as well.
 *
 * @retval GLOBUS_SUCCESS
 *     Attribute successfully copied.
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Attribute copy failed due to memory constraints.
 */
globus_result_t
globus_i_xio_http_attr_copy(
    void **                             dst,
    void *                              src)
{
    globus_result_t                     result;
    globus_i_xio_http_attr_t *          http_dst;
    globus_i_xio_http_attr_t *          http_src = src;
    GlobusXIOName(globus_i_xio_http_attr_copy);

    /*
     * Don't use globus_i_xio_http_request_init() here or the call to
     * globus_i_xio_http_request_copy() below will leak.
     */
    http_dst = globus_libc_malloc(sizeof(globus_i_xio_http_attr_t));
    if (http_dst == NULL)
    {
        result = GlobusXIOErrorMemory(dst);
        goto error_exit;
    }

    /* Copy request attrs */
    result = globus_i_xio_http_request_copy(
            &http_dst->request,
            &http_src->request);
    if (result != GLOBUS_SUCCESS)
    {
        goto free_http_dst_exit;
    }

    /* Copy response attrs */
    result = globus_i_xio_http_response_copy(
            &http_dst->response,
            &http_src->response);
    if (result != GLOBUS_SUCCESS)
    {
        goto free_http_dst_request_exit;
    }
    http_dst->delay_write_header = http_src->delay_write_header;

    *dst = http_dst;

    return GLOBUS_SUCCESS;
free_http_dst_request_exit:
    globus_i_xio_http_request_destroy(&http_dst->request);
free_http_dst_exit:
    globus_libc_free(http_dst);
error_exit:
    return result;
}
/* globus_i_xio_http_attr_copy() */

/**
 * Destroy an HTTP attribute
 * @ingroup globus_i_xio_http_attr
 *
 * Frees all storage associated with an HTTP attribute. No further
 * handle controls may be called on this attribute. This is called by the XIO
 * driver via globus_xio_attr_destroy().
 *
 * @param driver_attr
 *     Void pointer to a #globus_i_xio_http_attr_t structure to be destroyed.
 *
 * @return This function always returns GLOBUS_SUCCESS.
 *
 * @see globus_i_xio_http_attr_init()
 */
globus_result_t
globus_i_xio_http_attr_destroy(
    void *                              driver_attr)
{
    globus_i_xio_http_attr_t *          attr = driver_attr;
    GlobusXIOName(globus_i_xio_http_attr_destroy);

    globus_i_xio_http_request_destroy(&attr->request);
    globus_i_xio_http_response_destroy(&attr->response);
    globus_libc_free(attr);

    return GLOBUS_SUCCESS;
}
/* globus_i_xio_http_attr_destroy() */
