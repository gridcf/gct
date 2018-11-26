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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file buffer_set.c
 * @author Sam Lang, Sam Meder
 */
#endif

#include "gssapi_openssl.h"
#include "globus_i_gsi_gss_utils.h"

#include <string.h>

/* Only build if we have the extended GSSAPI */
#ifdef _HAVE_GSI_EXTENDED_GSSAPI
/**
 * @defgroup globus_gsi_gssapi_buffer_set Buffer Set Utilities
 * @ingroup globus_gsi_gssapi_extensions
 * @brief Buffer Set Utilities
 *
 * @details
 * Functions in this group provide utilities for creating, modifying,
 * and freeing a buffer set.
 */

/**
 * @brief Create a empty buffer set.
 * @ingroup globus_gsi_gssapi_buffer_set
 * @details
 * This function allocates and initializes a empty buffer set. The
 * memory allocated in this function should be freed by a call to
 * gss_release_buffer_set.
 *
 * @param minor_status
 *        The minor status returned by this function. This parameter
 *        will be 0 upon success.
 * @param buffer_set
 *        Pointer to a buffer set structure.
 * 
 * @retval GSS_S_COMPLETE Success
 * @retval GSS_S_FAILURE Failure
 *
 * @see gss_add_buffer_set_member
 * @see gss_release_buffer_set
 */
OM_uint32 
GSS_CALLCONV gss_create_empty_buffer_set(
    OM_uint32 *                         minor_status,
    gss_buffer_set_t *                  buffer_set)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    /* Sanity check */
    if ((buffer_set == NULL) || (minor_status == NULL))
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status, 
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("NULL parameters passed to function: %s"),
             __func__));
        goto exit;
    }

    *minor_status = GLOBUS_SUCCESS;

    *buffer_set = (gss_buffer_set_desc *) malloc(
        sizeof(gss_buffer_set_desc));

    if (!*buffer_set)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    (*buffer_set)->count = 0;
    (*buffer_set)->elements = NULL;

 exit:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return GSS_S_COMPLETE;
} 
/* gss_create_empty_buffer_set */

/**
 * @brief Add a buffer to a buffer set
 * @ingroup globus_gsi_gssapi_buffer_set
 * @details
 * This function allocates a new gss_buffer_t, initializes it with the
 * values in the member_buffer parameter.
 *
 *
 * @param minor_status
 *        The minor status returned by this function. This parameter
 *        will be 0 upon success.
 * @param member_buffer
 *        Buffer to insert into the buffer set.
 * @param buffer_set
 *        Pointer to a initialized buffer set structure.
 * 
 * @retval GSS_S_COMPLETE Success
 * @retval GSS_S_FAILURE Failure
 *
 * @see gss_create_empty_buffer_set
 * @see gss_release_buffer_set
 */
OM_uint32
GSS_CALLCONV gss_add_buffer_set_member(
    OM_uint32 *                         minor_status,
    const gss_buffer_t                  member_buffer,
    gss_buffer_set_t *                  buffer_set)
{
    int                                 new_count;
    gss_buffer_t                        new_elements;
    gss_buffer_set_t                    set;
    OM_uint32                           major_status = GSS_S_COMPLETE;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;
        
    /* Sanity check */
    if ((minor_status == NULL) || (member_buffer == NULL) ||
        (buffer_set == NULL) || (*buffer_set == GSS_C_NO_BUFFER_SET))
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Invalid buffer_set passed to function")));
        goto exit;
    }
        
    set = *buffer_set;
        
    new_count = set->count + 1;
    new_elements = malloc(sizeof(gss_buffer_desc) * new_count);
        
    if (new_elements == NULL)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;
        goto exit;
    }
        
    if (set->count > 0)
    {
        /* Copy existing buffers */
        memcpy(new_elements, set->elements,
               sizeof(gss_buffer_desc) * set->count);
    }
        
    /* And append new buffer */
    new_elements[set->count].value = malloc(member_buffer->length);

    if(new_elements[set->count].value == NULL)
    {
        free(new_elements);
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    memcpy(new_elements[set->count].value,
           member_buffer->value,
           member_buffer->length);

    new_elements[set->count].length = member_buffer->length;

    set->count = new_count;

    free(set->elements);
    set->elements = new_elements;

 exit:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

/**
 * @brief Free a Buffer Set
 * @ingroup globus_gsi_gssapi_buffer_set
 * @details
 * This function will free all memory associated with a buffer
 * set. Note that it will also free all memory associated with the
 * buffers int the buffer set.
 *
 * @param minor_status
 *        The minor status returned by this function. This parameter
 *        will be 0 upon success.
 * @param buffer_set
 *        Pointer to a buffer set structure. This pointer will point
 *        at a NULL value upon return.
 * 
 * @retval GSS_S_COMPLETE Success
 * @retval GSS_S_FAILURE Failure
 *
 * @see gss_create_empty_buffer_set
 * @see gss_add_buffer_set_member
 */
OM_uint32 
GSS_CALLCONV gss_release_buffer_set(
    OM_uint32 *                         minor_status,
    gss_buffer_set_t *                  buffer_set)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           local_minor_status;
    int                                 index;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;
    
    *minor_status = GLOBUS_SUCCESS;
        
    if (buffer_set == NULL || *buffer_set == GSS_C_NO_BUFFER_SET)
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status, 
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("NULL parameters passed to function: %s"),
             __func__));
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    for(index = 0; index < (*buffer_set)->count; index++)
    {
        major_status = gss_release_buffer(&local_minor_status,
                                          &(*buffer_set)->elements[index]);
        if(GSS_ERROR(major_status))
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_BUFFER);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }

    free((*buffer_set)->elements);

    free(*buffer_set);

    *buffer_set = NULL;
    
 exit:
    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;

} 
/* gss_release_buffer_set */

#endif /* _HAVE_GSI_EXTENDED_GSSAPI */
