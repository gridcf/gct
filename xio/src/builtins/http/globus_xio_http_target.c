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
 * @defgroup globus_i_xio_http_target Internal Target Implementation
 * @brief Internal Target Implementation
 */
#endif

/**
 * Allocate a new target
 * @ingroup globus_i_xio_http_target
 */
globus_i_xio_http_target_t *
globus_i_xio_http_target_new(void)
{
    return globus_libc_calloc(1, sizeof(globus_i_xio_http_target_t));
}
/* globus_l_xio_http_target_new() */

globus_result_t
globus_i_xio_http_target_init(
    globus_i_xio_http_target_t **       out_target,
    const globus_xio_contact_t *        contact_info)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_i_xio_http_target_t *        target;
    GlobusXIOName(globus_l_xio_http_target_init);

    target = globus_i_xio_http_target_new();

    if (target == NULL)
    {
        res = GlobusXIOErrorMemory("target");

        goto error_exit;
    }
    target->is_client = GLOBUS_TRUE;

    if (contact_info->host == NULL)
    {
        res = GlobusXIOErrorParameter("contact_info");

        goto free_target_exit;
    }

    target->host = globus_libc_strdup(contact_info->host);

    if (target->host == NULL)
    {
        res = GlobusXIOErrorMemory("host");

        goto free_target_exit;
    }

    if (contact_info->resource == NULL)
    {
        target->uri = globus_libc_strdup("/");
    }
    else
    {
        if (contact_info->resource[0] != '/')
        {
            size_t                      urilen = strlen(contact_info->resource);

            target->uri = malloc(urilen + 2);
            target->uri[0] = '/';
            memcpy(target->uri+1, contact_info->resource, urilen);
            target->uri[urilen + 1] = '\0';
        }
        else
        {
            target->uri = globus_libc_strdup(contact_info->resource);
        }
    }

    if (target->uri == NULL)
    {
        goto free_target_exit;
    }

    if(contact_info->port == 0)
    {
        if(strcmp(contact_info->scheme, "http") == 0)
        {
            target->port = 80;
        }
        else if(strcmp(contact_info->scheme, "https") == 0)
        {
            target->port = 443;
        }
        else
        {
            res = GlobusXIOErrorParameter("port");
    
            goto free_target_exit;
        }
    }
    else
    {
        target->port = (unsigned short) atoi(contact_info->port);
    }

    *out_target = target;

    return res;

free_target_exit:
    globus_i_xio_http_target_destroy(target);
error_exit:
    return res;
}
/* globus_i_xio_http_target_init() */

/**
 * Copy the contents of an HTTP target
 * @ingroup globus_i_xio_http_target
 *
 * All values associated with the @a src target will be copied into the
 * corresponding fields of the @a dest target. If this function returns
 * a failure, then the @a dest target should be considered uninitialized.
 *
 * @param dest
 *     Target to be initialized with the values form src. This should not
 *     be initialized before this is called, or memory may be leaked.
 * @param src
 *     Target containing defined values.
 *
 * @retval GLOBUS_SUCCESS
 *     Copy successful.
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Copy failed due to memory constraints.
 */
globus_result_t
globus_i_xio_http_target_copy(
    globus_i_xio_http_target_t *        dest,
    const globus_i_xio_http_target_t *  src)
{
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusXIOName(globus_i_xio_http_target_copy);

    dest->is_client = src->is_client;

    if (src->host != NULL)
    {
        dest->host = globus_libc_strdup(src->host);

        if (dest->host == NULL)
        {
            res = GlobusXIOErrorMemory("host");

            goto error_exit;
        }
    }

    if (src->uri != NULL)
    {
        dest->uri = globus_libc_strdup(src->uri);

        if (dest->uri == NULL)
        {
            res = GlobusXIOErrorMemory("uri");

            goto free_host_exit;
        }
    }

    dest->port = src->port;

    return res;

free_host_exit:
    globus_libc_free(dest->host);
    dest->host = NULL;
error_exit:
    return res;
}
/* globus_i_xio_http_target_copy() */

/**
 * Destroy an HTTP target
 * @ingroup globus_i_xio_http_target
 *
 * Frees all storage associated with an HTTP target. No further opens may
 * be alled with this target. This is called by the XIO driver via
 * globus_xio_target_destroy().
 *
 * @param driver_target
 *     Void pointer to a #globus_i_xio_http_target_t structure to be
 *     destroyed.
 *
 * @return This function always returns GLOBUS_SUCCESS.
 */
globus_result_t
globus_i_xio_http_target_destroy(
    void *                              driver_target)
{

    globus_i_xio_http_target_destroy_internal(driver_target);
    globus_libc_free(driver_target);

    return GLOBUS_SUCCESS;
}
/* globus_i_xio_http_target_destroy() */

extern
void
globus_i_xio_http_target_destroy_internal(
    globus_i_xio_http_target_t *        target)
{
    globus_i_xio_http_target_t *        http_target = target;

    if (http_target->host != NULL)
    {
        globus_libc_free(http_target->host);
    }

    if (http_target->uri != NULL)
    {
        globus_libc_free(http_target->uri);
    }
}
/* globus_i_xio_http_target_destroy_internal() */
