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
 * @file globus_error_gssapi.c
 * @brief Globus Error GSS API
 */
#endif

#include "globus_i_error_gssapi.h"
#include <string.h>

/**
 * @brief Construct Error
 * @ingroup globus_gssapi_error_object
 * @details
 * Allocate and initialize an error of type GLOBUS_ERROR_TYPE_GSSAPI
 *
 * @param base_source
 *        Pointer to the originating module.
 * @param base_cause
 *        The error object causing the error. If this is the original
 *        error, this parameter may be NULL.
 * @param major_status
 *        The GSSAPI major status
 * @param minor_status
 *        The GSSAPI minor status
 * @return
 *        The resulting error object. It is the user's responsibility
 *        to eventually free this object using globus_object_free(). A
 *        globus_result_t may be obtained by calling
 *        globus_error_put() on this object.        
 */
globus_object_t *
globus_error_construct_gssapi_error(
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    const OM_uint32                     major_status,
    const OM_uint32                     minor_status)
{
    globus_object_t *                   error;
    globus_object_t *                   newerror;

    newerror = globus_object_construct(GLOBUS_ERROR_TYPE_GSSAPI);
    error = globus_error_initialize_gssapi_error(
        newerror,
        base_source,
        base_cause,
        major_status,
        minor_status);

    if (error == NULL)
    {
        globus_object_free(newerror);
    }

    return error;
}/* globus_error_construct_gssapi_error() */

/**
 * @brief Initialize Error
 * @ingroup globus_gssapi_error_object 
 * @details
 * Initialize a previously allocated error of type
 * GLOBUS_ERROR_TYPE_GSSAPI
 *
 * @param error
 *        The previously allocated error object.
 * @param base_source
 *        Pointer to the originating module.
 * @param base_cause
 *        The error object causing the error. If this is the original
 *        error this parameter may be NULL.
 * @param major_status
 *        The GSSAPI major status
 * @param minor_status
 *        The GSSAPI minor status
 * @return
 *        The resulting error object. You may have to call
 *        globus_error_put() on this object before passing it on.
 */
globus_object_t *
globus_error_initialize_gssapi_error(
    globus_object_t *                   error,
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    const OM_uint32                     major_status,
    const OM_uint32                     minor_status)
{
    globus_l_gssapi_error_data_t *      instance_data;
    globus_object_t *                   minor_obj;
    gss_OID_set                         actual_mechs;
    OM_uint32                           local_minor_status;
    extern gss_OID                      gss_mech_globus_gssapi_openssl;
    
    instance_data = (globus_l_gssapi_error_data_t *)
        malloc(sizeof(globus_l_gssapi_error_data_t));

    instance_data->major_status = major_status;
    instance_data->minor_status = minor_status;
    instance_data->is_globus_gsi = GLOBUS_FALSE;
    
    if(gss_indicate_mechs(
        &local_minor_status, &actual_mechs) == GSS_S_COMPLETE)
    {
        int                             boolean;
        
        if(gss_test_oid_set_member(
            &local_minor_status,
            gss_mech_globus_gssapi_openssl,
            actual_mechs,
            &boolean) == GSS_S_COMPLETE && boolean)
        {
            instance_data->is_globus_gsi = GLOBUS_TRUE;
        }
        
        gss_release_oid_set(&local_minor_status, &actual_mechs);
    }
    
    if(instance_data->is_globus_gsi)
    {
        minor_obj = globus_error_get((globus_result_t) minor_status);
        if(!base_cause)
        {
            base_cause = minor_obj;
        }
        else if(minor_obj)
        {
            base_cause = globus_error_initialize_base(
                minor_obj, globus_error_get_source(base_cause), base_cause);
        }
    }
        
    globus_object_set_local_instance_data(error, instance_data);
    
    return globus_error_initialize_base(error, base_source, base_cause);
}/* globus_error_initialize_gssapi_error() */

/**
 * @brief Get Major Status
 * @ingroup globus_gssapi_error_accessor  
 * @details
 * Retrieve the major status from a GSSAPI error object.
 *
 * @param error
 *        The error from which to retrieve the major status
 * @return
 *        The major status stored in the object
 */
OM_uint32
globus_error_gssapi_get_major_status(
    globus_object_t *                   error)
{
    return ((globus_l_gssapi_error_data_t *)
            globus_object_get_local_instance_data(error))->major_status;
}/* globus_error_gssapi_get_major_status */

/**
 * @brief Set Major Status
 * @ingroup globus_gssapi_error_accessor  
 * @details
 * Set the major status in a GSSAPI error object.
 *
 * @param error
 *        The error object for which to set the major status
 * @param major_status
 *        The major status
 */
void
globus_error_gssapi_set_major_status(
    globus_object_t *                   error,
    const OM_uint32                     major_status)
{
    ((globus_l_gssapi_error_data_t *)
     globus_object_get_local_instance_data(error))->major_status = major_status;
}/* globus_error_gssapi_set_major_status */

/**
 * @brief Get Minor Status
 * @ingroup globus_gssapi_error_accessor  
 * @details
 * Retrieve the minor status from a gssapi error object.
 *
 * @param error
 *        The error from which to retrieve the minor status
 * @return
 *        The minor status stored in the object
 */
OM_uint32
globus_error_gssapi_get_minor_status(
    globus_object_t *                   error)
{
    globus_l_gssapi_error_data_t *      data;
    
    data = (globus_l_gssapi_error_data_t *)
        globus_object_get_local_instance_data(error);
    if(data)
    {
        if(data->is_globus_gsi)
        {
            return (OM_uint32) globus_error_put(
                globus_object_copy(globus_error_get_cause(error)));
        }
        else
        {
            return data->minor_status;
        }
    }
    
    return 0;
}
/* globus_error_gssapi_get_minor_status() */

/**
 * @brief Error Match
 * @ingroup globus_gssapi_error_utility  
 * @details
 * Check whether the error originated from a specific module and
 * match a specific major status.
 *
 * This function checks whether the error or any of it's causative
 * errors originated from a specific module and contains a specific
 * major status. If the module descriptor is left unspecified this
 * function  will check for any error of the specified major_status
 * and vice versa. 
 *
 * @param error
 *        The error object for which to perform the check
 * @param module
 *        The module descriptor to check for
 * @param major_status
 *        The major status to check for
 * @retval GLOBUS_TRUE The error matched the module and major status
 * @retval GLOBUS_FALSE The error failed to match the module and major status
 */
globus_bool_t
globus_error_gssapi_match(
    globus_object_t *                   error,
    globus_module_descriptor_t *        module,
    const OM_uint32                     major_status)
{
    globus_module_descriptor_t *        source_module;
    int                                 current_major_status;
    
    if(error == NULL)
    {
        return GLOBUS_FALSE;
    }

    if(globus_object_get_type(error) != GLOBUS_ERROR_TYPE_GSSAPI)
    {
        /* not our type, skip it */
        return globus_error_gssapi_match(
            globus_error_get_cause(error),
            module,
            major_status);
    }

    source_module = globus_error_get_source(error);
    current_major_status = globus_error_gssapi_get_major_status(error);
    
    if(source_module == module && current_major_status == major_status)
    {
        return GLOBUS_TRUE;
    }
    else
    {
        return globus_error_gssapi_match(
            globus_error_get_cause(error),
            module,
            major_status);
    }
}
/* globus_error_gssapi_match */

/**
 * @brief Wrap GSSAPI Error
 * @ingroup globus_gssapi_error_utility  
 * @details
 * Allocate and initialize an error of type GLOBUS_ERROR_TYPE_GLOBUS
 * which contains a causal error of type GLOBUS_ERROR_TYPE_GSSAPI.
 *
 * @param base_source
 *        Pointer to the originating module.
 * @param major_status
 *        The major status to use when generating the causal error.
 * @param minor_status
 *        The minor status to use when generating the causal error.
 * @param type
 *        The error type. We may reserve part of this namespace for
 *        common errors. Errors not in this space are assumed to be
 *        local to the originating module.
 * @param source_file
 *        Name of file.  Use __FILE__
 * @param source_func
 *        Name of function.  Use _globus_func_name and declare your func with
 *        GlobusFuncName(<name>)
 * @param source_line
 *        Line number.  Use __LINE__
 * @param short_desc_format
 *        Short format string giving a succinct description
 *        of the error. To be passed on to the user.
 * @param ...
 *        Arguments for the format string.
 * @return
 *        The resulting error object. It is the user's responsibility
 *        to eventually free this object using globus_object_free(). A
 *        globus_result_t may be obtained by calling
 *        globus_error_put() on this object.        
 */
globus_object_t *
globus_error_wrap_gssapi_error(
    globus_module_descriptor_t *        base_source,
    OM_uint32                           major_status,
    OM_uint32                           minor_status,
    int                                 type,
    const char *                        source_file,
    const char *                        source_func,
    int                                 source_line,
    const char *                        short_desc_format,
    ...)
{
    globus_object_t *                   causal_error;
    globus_object_t *                   error;
    va_list                             ap;
    
    causal_error = globus_error_construct_gssapi_error(
        GLOBUS_GSI_GSSAPI_MODULE,
        NULL,
        major_status,
        minor_status);

    if(!causal_error)
    {
        return GLOBUS_NULL;
    }

    va_start(ap, short_desc_format);
    
    error = globus_error_v_construct_error(
        base_source,
        causal_error,
        type,
        source_file,
        source_func,
        source_line,
        short_desc_format,
        ap);

    va_end(ap);

    if(error == GLOBUS_NULL)
    {
        globus_object_free(causal_error);
    }
    
    return error;
    
}
/* globus_error_wrap_gssapi_error */
