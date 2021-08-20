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

/**
 * @file globus_error_errno.h
 * @brief Globus Errno Error API
 */

#ifndef GLOBUS_ERROR_ERRNO_H
#define GLOBUS_ERROR_ERRNO_H

/**
 * @defgroup globus_errno_error_api Globus Errno Error API
 * @ingroup globus_error_api
 * @brief Globus Errno Error API
 *
 * These globus_error functions are motivated by the desire to provide
 * a easier way of generating new error types, while at the same time
 * preserving all features (e.g. memory management, chaining) of the
 * current error handling framework. The functions in this API are
 * auxiliary to the function in the Globus Generic Error API in the
 * sense that they provide a wrapper for representing system errors in
 * terms of a globus_error_t.
 *
 * Any program that uses Globus Errno Error functions must include
 * "globus_common.h". 
 */

#include "globus_common_include.h"
#include "globus_object.h"
#include "globus_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_errno_error_object Error Construction
 * @ingroup globus_errno_error_api
 * @brief Error Construction
 *
 * Create and initialize a Globus Errno Error object.
 *
 * This section defines operations to create and initialize Globus
 * Errno Error objects.
 */


/** Error type definition
 * @ingroup globus_errno_error_object
 * @hideinitializer
 */
#define GLOBUS_ERROR_TYPE_ERRNO (&GLOBUS_ERROR_TYPE_ERRNO_DEFINITION)

extern const globus_object_type_t GLOBUS_ERROR_TYPE_ERRNO_DEFINITION;

#ifndef DOXYGEN

globus_object_t *
globus_error_construct_errno_error(
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    const int                           system_errno);

globus_object_t *
globus_error_initialize_errno_error(
    globus_object_t *                   error,
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    const int                           system_errno);

#endif

/**
 * @defgroup globus_errno_error_accessor Error Data Accessors and Modifiers
 * @ingroup globus_errno_error_api
 * @brief Error Data Accessors and Modifiers
 *
 * Get and set data in a Globus Errno Error object.
 *
 * This section defines operations for accessing and modifying data in a Globus
 * Errno Error object.
 */

#ifndef DOXYGEN

int
globus_error_errno_get_errno(
    globus_object_t *                   error);

void
globus_error_errno_set_errno(
    globus_object_t *                   error,
    const int                           system_errno);

#endif

/**
 * @defgroup globus_errno_error_utility Error Handling Helpers
 * @ingroup globus_errno_error_api
 * @brief Error Handling Helpers
 *
 * Helper functions for dealing with Globus Errno Error objects.
 *
 * This section defines utility functions for dealing with Globus
 * Errno Error objects.
 */

#ifndef DOXYGEN

globus_bool_t
globus_error_errno_match(
    globus_object_t *                   error,
    globus_module_descriptor_t *        module,
    int                                 system_errno);

int
globus_error_errno_search(
    globus_object_t *                   error);

globus_object_t *
globus_error_wrap_errno_error(
    globus_module_descriptor_t *        base_source,
    int                                 system_errno,
    int                                 type,
    const char *                        source_file,
    const char *                        source_func,
    int                                 source_line,
    const char *                        short_desc_format,
    ...);

#endif

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_ERROR_ERRNO_H */
