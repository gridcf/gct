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

#ifndef GLOBUS_INCLUDE_GSSAPI_ERROR_H
#define GLOBUS_INCLUDE_GSSAPI_ERROR_H

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus GSSAPI Error API
 * @copydoc globus_gssapi_error_api
 */
#endif

/**
 * @defgroup globus_gssapi_error_api Globus GSSAPI Error API
 * @brief Globus GSSAPI Error API
 *
 * These globus_error functions are motivated by the desire to provide
 * a easier way of generating new error types, while at the same time
 * preserving all features (e.g. memory management, chaining) of the
 * current error handling framework. The functions in this API are
 * auxiliary to the function in the Globus Generic Error API in the
 * sense that they provide a wrapper for representing GSSAPI errors in
 * terms of a globus_error_t.
 *
 * Any program that uses Globus GSSAPI Error functions must include
 * the globus_error_gssapi.h header.
 */

#include "globus_common.h"
#include "gssapi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_gssapi_error_object Error Construction
 * @ingroup globus_gssapi_error_api
 * @brief Error Construction
 *
 * Create and initialize a Globus GSSAPI Error object.
 *
 * This section defines operations to create and initialize Globus
 * GSSAPI Error objects.
 */


/** Error type definition
 * @ingroup globus_gssapi_error_object
 * @hideinitializer
 */
#define GLOBUS_ERROR_TYPE_GSSAPI (&GLOBUS_ERROR_TYPE_GSSAPI_DEFINITION)

extern const globus_object_type_t GLOBUS_ERROR_TYPE_GSSAPI_DEFINITION;

globus_object_t *
globus_error_construct_gssapi_error(
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    const OM_uint32                     major_status,
    const OM_uint32                     minor_status);

globus_object_t *
globus_error_initialize_gssapi_error(
    globus_object_t *                   error,
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    const OM_uint32                     major_status,
    const OM_uint32                     minor_status);

/**
 * @defgroup globus_gssapi_error_accessor Error Data Accessors and Modifiers
 * @ingroup globus_gssapi_error_api
 * @brief Error Data Accessors and Modifiers
 *
 * Get and set data in a Globus GSSAPI Error object.
 *
 * This section defines operations for accessing and modifying data in a Globus
 * GSSAPI Error object.
 */

OM_uint32
globus_error_gssapi_get_major_status(
    globus_object_t *                   error);

void
globus_error_gssapi_set_major_status(
    globus_object_t *                   error,
    const OM_uint32                     major_status);

OM_uint32
globus_error_gssapi_get_minor_status(
    globus_object_t *                   error);

/**
 * @defgroup globus_gssapi_error_utility Error Handling Helpers
 * @ingroup globus_gssapi_error_api
 * @brief Error Handling Helpers
 *
 * Helper functions for dealing with Globus GSSAPI Error objects.
 *
 * This section defines utility functions for dealing with Globus
 * GSSAPI Error objects.
 */

globus_bool_t
globus_error_gssapi_match(
    globus_object_t *                   error,
    globus_module_descriptor_t *        module,
    const OM_uint32                     major_status);

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
    ...);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_INCLUDE_GSSAPI_ERROR_H */
