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
 * @file globus_error_generic.h
 * @brief Globus Error API
 */

#ifndef GLOBUS_ERROR_GENERIC_H
#define GLOBUS_ERROR_GENERIC_H

#include "globus_object.h"
#include "globus_module.h"

#include <stdarg.h>

/**
 * @defgroup globus_error_api Globus Error API
 * @ingroup globus_common
 * @brief Globus Error API
 *
 * Intended use:
 *
 * If a function needs to return an error it should do the following:
 *
 * - External errors, such as error returns from system calls and
 *   GSSAPI errors, should be wrapped using the appropriate error
 *   type.
 * - The wrapped external error should then be passed as the cause of
 *   a globus error.
 * - External error types are expected to provide a utility function
 *   to combine the above two steps.
 * - The globus error should then be returned from the function.
 *
 * Notes on how to generate globus errors:
 *
 * - Module specific error types should be greater or equal to 1024
 *   (to leave some space for global error types).
 * - You may wish to generate a mapping from error types to format
 *   strings for use in short descriptions.
 * - You may also wish to generate a common prefix for all of the
 *   above fromat strings. The suggested prefix is "Function: %s Line:
 *   %s ".
 */

/**
 * @anchor globus_generic_error_api
 * @defgroup globus_generic_error_api Globus Generic Error API
 * @ingroup globus_error_api
 * @brief Globus Generic Error API
 *
 * These globus_error functions are motivated by the desire to provide
 * a easier way of generating new error types, while at the same time
 * preserving all features (e.g. memory management, chaining) of the
 * current error handling framework. It does this by defining a
 * generic error type for globus which in turn contains a integer in
 * it's instance data which is used for carrying the actual error type
 * information.
 *
 * Any program that uses Globus Generic Error functions must include
 * "globus_common.h". 
 */


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_generic_error_object Error Construction
 * @ingroup globus_generic_error_api
 * @brief Error Construction
 *
 * Create and initialize a Globus Generic Error object.
 *
 * This section defines operations to create and initialize Globus
 * Generic Error objects.
 */


/** Error type definition
 * @ingroup globus_generic_error_object
 * @hideinitializer
 */
#define GLOBUS_ERROR_TYPE_GLOBUS (&GLOBUS_ERROR_TYPE_GLOBUS_DEFINITION)
extern const globus_object_type_t GLOBUS_ERROR_TYPE_GLOBUS_DEFINITION;

#ifndef DOXYGEN

globus_object_t *
globus_error_construct_error(
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    int                                 type,
    const char *                        source_file,
    const char *                        source_func,
    int                                 source_line,
    const char *                        short_desc_format,
    ...);

globus_object_t *
globus_error_v_construct_error(
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    int                                 type,
    const char *                        source_file,
    const char *                        source_func,
    int                                 source_line,
    const char *                        short_desc_format,
    va_list                             ap);

globus_object_t *
globus_error_initialize_error(
    globus_object_t *                   error,
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    int                                 type,
    const char *                        source_file,
    const char *                        source_func,
    int                                 source_line,
    const char *                        short_desc_format,
    va_list                             ap);

#endif

/**
 * @defgroup globus_generic_error_accessor Error Data Accessors and Modifiers
 * @ingroup globus_generic_error_api
 * @brief Error Data Accessors and Modifiers
 *
 * Get and set data in a Globus Generic Error object.
 *
 * This section defines operations for accessing and modifying data in a Globus
 * Generic Error object.
 */

globus_module_descriptor_t *
globus_error_get_source(
    globus_object_t *                   error);

void
globus_error_set_source(
    globus_object_t *                   error,
    globus_module_descriptor_t *        source_module);

globus_object_t *
globus_error_get_cause (
    globus_object_t *                   error);

void
globus_error_set_cause (
    globus_object_t *                   error,
    globus_object_t *                   causal_error);

int
globus_error_get_type(
    globus_object_t *                   error);

void
globus_error_set_type(
    globus_object_t *                   error,
    const int                           type);

char *
globus_error_get_short_desc(
    globus_object_t *                   error);

void
globus_error_set_short_desc(
    globus_object_t *                   error,
    const char *                        short_desc_format,
    ...);

char *
globus_error_get_long_desc(
    globus_object_t *                   error);

void
globus_error_set_long_desc(
    globus_object_t *                   error,
    const char *                        long_desc_format,
    ...);

/**
 * @defgroup globus_generic_error_utility Error Handling Helpers
 * @ingroup globus_generic_error_api
 * @brief Error Handling Helpers
 *
 * Helper functions for dealing with Globus Generic Error objects.
 *
 * This section defines utility functions for dealing with Globus
 * Generic Error objects.
 */

globus_bool_t
globus_error_match(
    globus_object_t *                   error,
    globus_module_descriptor_t *        module,
    int                                 type);

char *
globus_error_print_chain(
    globus_object_t *                   error);

char *
globus_error_print_friendly(
    globus_object_t *                   error);

/**
 * If registered with a module's descriptor, this handler will be called on
 * behalf of globus_error_print_friendly()
 * 
 * @param error
 *      The error chain that originated from this module.  The top error object
 *      in the chain will be one created by this module and have a type of
 *      'type';  The remaining objects are the same as the cause chain used at
 *      creation time.  The user can use globus_error_get_type(error) to get
 *      the error code (for GLOBUS_ERROR_TYPE_GLOBUS objects)
 * 
 * @param type
 *      The error object type for the top object in the error chain
 *      (e.g. GLOBUS_ERROR_TYPE_GLOBUS, GLOBUS_ERROR_TYPE_ERRNO)
 * 
 * @return
 *      The function should return a newly allocated string with a friendly
 *      error message explaining the error in more detail.  This string should
 *      be considered the only message a user will see.  If the module
 *      has nothing nice to say, it should return NULL so the next module in
 *      the error chain can be tried.
 * 
 *      If you think a friendly error from causes beneath you should be 
 *      included, you may use 
 *      globus_error_print_friendly(globus_error_get_cause(error)) 
 *      within this handler to append to your message.
 */
typedef char * (*globus_error_print_friendly_t)(
    globus_object_t *                   error,
    const globus_object_type_t *        type);


#define GLOBUS_ERROR_TYPE_MULTIPLE (&GLOBUS_ERROR_TYPE_MULTIPLE_DEFINITION)
extern const globus_object_type_t GLOBUS_ERROR_TYPE_MULTIPLE_DEFINITION;

globus_object_t *
globus_error_construct_multiple(
    globus_module_descriptor_t *        base_source,
    int                                 type,
    const char *                        fmt,
    ...);

void
globus_error_mutliple_add_chain(
    globus_object_t *                   multiple_error,
    globus_object_t *                   chain,
    const char *                        fmt,
    ...);

globus_object_t *
globus_error_multiple_remove_chain(
    globus_object_t *                   multiple_error);
    
#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_ERROR_GENERIC_H */
