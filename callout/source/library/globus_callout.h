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

#ifndef _GLOBUS_CALLOUT_H_
#define _GLOBUS_CALLOUT_H_

/**
 * @file globus_callout.h
 * @brief Globus Callout Infrastructure
 * @author Sam Meder
 */


#include "globus_common.h"
#include "globus_callout_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus Callout API
 * @copydoc globus_callout
 */
#endif
/**
 * @defgroup globus_callout Globus Callout API
 * @brief Globus Callout API
 *
 * This API is intended to ease integration of configurable callouts into the
 * Grid Community Toolkit and to provide a platform independent way of dealing
 * with runtime loadable functions. It (hopefully) achieves this goal by
 * providing the following functionality:
 *
 * - It provides a function for reading callout configuration files. Files are
 *   assumed to have the following format:
 *    - Anything after a <tt>#</tt> is assumed to be a comment
 *    - Blanks lines are ignored
 *    - Lines specifying callouts have the format
 @verbatim
        abstract type           library         symbol
 @endverbatim
 *    - where <i>abstract type</i> denotes the type of callout,
 *      e.g. globus_gram_jobmanager_authz, <i>library</i> denotes the library
 *      the callout can be found in and <i>symbol</i> denotes the function name
 *      of the callout.
 * - It provides a API function for registering callouts
 * - All callouts are assumed to have the function signature
 *   globus_result_t callout_func(va_list ap)
 * - It provides a function for calling a callout given an abstract type. If
 *   multiple callouts are defined for the same abstract type then all callouts
 *   for the abstract type will be called. Implementers should not rely on any
 *   correlation between the order of configuration and the order of invocation
 *   of callouts of the same abstract type.
 *
 * Any program that uses Globus Callout functions must include
 * the globus_callout.h header
 *
 * Function Categories
 * - @ref globus_callout_activation
 * - @ref globus_callout_handle
 * - @ref globus_callout_config 
 * - @ref globus_callout_call 
 * - @ref globus_callout_constants
 */

/**
 * @defgroup globus_callout_activation Activation
 * @ingroup globus_callout
 * @brief Callback API Activation
 *
 * @details
 * Globus Callout API uses standard Globus module activation and
 * deactivation.  Before any Globus Callout API functions are called, the
 * following function must be called:
 *
 * @code
 *      globus_module_activate(GLOBUS_CALLOUT_MODULE)
 * @endcode
 *
 *
 * This function returns GLOBUS_SUCCESS if Globus Callout API was
 * successfully initialized, and you are therefore allowed to
 * subsequently call Globus Callout API functions.  Otherwise, an error
 * code is returned, and Globus GSI Credential functions should not be
 * subsequently called. This function may be called multiple times.
 *
 * To deactivate Globus Callout API, the following function must be called:
 *
 * @code
 *    globus_module_deactivate(GLOBUS_CALLOUT_MODULE)
 * @endcode
 *
 * This function should be called once for each time Globus Callout API
 * was activated. 
 *
 */

/**
 * Module descriptor
 * @ingroup globus_callout_activation
 * @hideinitializer
 */
#define GLOBUS_CALLOUT_MODULE    (&globus_i_callout_module)

extern 
globus_module_descriptor_t              globus_i_callout_module;


/**
 * Callout handle type definition
 * @ingroup globus_callout_handle
 */
typedef struct globus_i_callout_handle_s * globus_callout_handle_t;


/**
 * Callout function type definition
 * @ingroup globus_callout_call
 */
typedef globus_result_t (*globus_callout_function_t)(
    va_list                             ap);


/**
 * @defgroup globus_callout_handle Callout Handle Operations
 * @ingroup globus_callout
 * @brief Manage a Globus Callout Handle structure
 *
 * @details
 * This section defines operations for initializing and destroying Globus
 * Callout Handle structure.
 */
globus_result_t
globus_callout_handle_init(
    globus_callout_handle_t *           handle);

globus_result_t
globus_callout_handle_destroy(
    globus_callout_handle_t             handle);

/**
 * @defgroup globus_callout_config Callout Configuration
 * @ingroup globus_callout
 * @brief Register callouts
 *
 * @details
 * This section defines operations for registering callouts. Callouts may be
 * registered either through a configuration file or through calls to
 * globus_callout_register. 
 */
globus_result_t
globus_callout_read_config(
    globus_callout_handle_t             handle,
    char *                              filename);

globus_result_t
globus_callout_register(
    globus_callout_handle_t             handle,
    char *                              type,
    char *                              library,
    char *                              symbol);

/**
 * @defgroup globus_callout_call Callout Invocation
 * @ingroup globus_callout
 * @brief Invoke callouts
 *
 * @details
 * This section defines a operation for invoking callouts by their abstract
 * type. 
 */
globus_result_t
globus_callout_call_type(
    globus_callout_handle_t             handle,
    char *                              type,
    ...);


#ifdef __cplusplus
}
#endif

#endif
