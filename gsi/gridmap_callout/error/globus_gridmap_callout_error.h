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
 * @file globus_gridmap_callout_error.h
 * @author Sam Meder
 */
#endif

#ifndef GLOBUS_GRIDMAP_CALLOUT_ERROR_H
#define GLOBUS_GRIDMAP_CALLOUT_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "globus_common.h"
#include "globus_error_gssapi.h"

/**
 * @defgroup globus_gridmap_callout_error_activation Activation
 * @brief Activation
 *
 * The Globus Gridmap Callout Error API uses standard Globus module
 * activation and deactivation.  Before any Globus Gridmap Callout
 * Error API functions are called, the following function must be called:
 *
 * @code
 *      globus_module_activate(GLOBUS_GRIDMAP_CALLOUT_ERROR_MODULE)
 * @endcode
 *
 *
 * This function returns GLOBUS_SUCCESS if the Globus Gridmap Callout
 * Error API was successfully initialized, and you are therefore allowed to
 * subsequently call Globus Gridmap Callout Error API functions.
 * Otherwise, an error code is returned, and Globus GSI Credential functions
 * should not be subsequently called. This function may be called multiple
 * times. 
 *
 * To deactivate Globus Gridmap Callout Error API, the following
 * function must be called: 
 *
 * @code
 *    globus_module_deactivate(GLOBUS_GRIDMAP_CALLOUT_ERROR_MODULE)
 * @endcode
 *
 * This function should be called once for each time Globus Gridmap
 * Callout Error API was activated. 
 *
 */

/**
 * Module descriptor
 * @ingroup globus_gridmap_callout_error_activation
 * @hideinitializer
 */
#define GLOBUS_GRIDMAP_CALLOUT_ERROR_MODULE    (&globus_i_gridmap_callout_error_module)

extern 
globus_module_descriptor_t              globus_i_gridmap_callout_error_module;

/**
 * @defgroup globus_gridmap_callout_error_datatypes Datatypes
 * @brief Datatypes
 */

/**
 * Gridmap Callout Error codes
 * @ingroup globus_gridmap_callout_error_datatypes
 */
typedef enum
{
    /** Gridmap lookup failed */
    GLOBUS_GRIDMAP_CALLOUT_LOOKUP_FAILED = 0,
    /** GSSAPI error */
    GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR = 1,
    /** Buffer too small */
    GLOBUS_GRIDMAP_CALLOUT_BUFFER_TOO_SMALL = 2,
    /** Last marker - never used */
    GLOBUS_GRIDMAP_CALLOUT_ERROR_LAST = 3
}
globus_gridmap_callout_error_t;

extern char * globus_i_gridmap_callout_error_strings[];

#define GLOBUS_GRIDMAP_CALLOUT_ERROR(__RESULT, __TYPE, __ERRSTR)         \
{                                                                        \
    char *                          _tmp_str_ =                          \
        globus_common_create_string __ERRSTR;                            \
    (__RESULT) = globus_error_put(                                       \
        globus_error_construct_error(                                    \
            GLOBUS_GRIDMAP_CALLOUT_ERROR_MODULE,                         \
            (__RESULT) ? globus_error_get(__RESULT) : NULL,              \
            __TYPE,                                                      \
            __FILE__, \
            "Globus Gridmap Callout",                \
            __LINE__, \
            "%s%s%s",                                         \
            globus_i_gridmap_callout_error_strings[__TYPE],              \
            _tmp_str_ ? ": " : "",                                       \
            _tmp_str_ ? _tmp_str_ : ""));                                \
    if(_tmp_str_) free(_tmp_str_);                                       \
}

#define GLOBUS_GRIDMAP_CALLOUT_GSS_ERROR(__RESULT, __MAJOR_STATUS, __MINOR_STATUS) \
    __RESULT = globus_error_put(                                                   \
        globus_error_wrap_gssapi_error(                                            \
            GLOBUS_GRIDMAP_CALLOUT_ERROR_MODULE,                                   \
            __MAJOR_STATUS,                                                        \
            __MINOR_STATUS,                                                        \
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,                                   \
            __FILE__,                                                              \
            "Globus Gridmap Callout",                                              \
            __LINE__,                                                              \
            "%s",                                                                  \
            globus_i_gridmap_callout_error_strings[GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR]))

#ifdef __cplusplus
}
#endif

#endif
