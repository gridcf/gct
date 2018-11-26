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
 * @file globus_gram_jobmanager_callout_error.h
 * @brief Globus GRAM Job Manager Callout Error
 * @author Sam Meder
 */

#ifndef GLOBUS_GRAM_JOBMANAGER_CALLOUT_ERROR_H
#define GLOBUS_GRAM_JOBMANAGER_CALLOUT_ERROR_H


#include "globus_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_gram_job_manager_callout_error Globus GRAM Job Manager Callout Error
 * @brief Globus GRAM Job Manager Callout Error
 *
 * The globus_gram_jobmanager_callout_error API defines error object types to
 * return error conditions from GRAM authorization callouts.
 */
/**
 * @defgroup globus_gram_jobmanager_callout_error_activation Activation
 * @ingroup globus_gram_job_manager_callout_error 
 * @brief Activation
 *
 * Globus GRAM Jobmanager Callout Error API uses standard Globus module
 * activation and deactivation.  Before any Globus GRAM Jobmanager Callout
 * Error API functions are called, the following function must be called:
 *
 * @code
 *      globus_module_activate(GLOBUS_GRAM_JOBMANAGER_CALLOUT_ERROR_MODULE)
 * @endcode
 *
 *
 * This function returns GLOBUS_SUCCESS if the Globus GRAM Jobmanager Callout
 * Error API was successfully initialized, and you are therefore allowed to
 * subsequently call Globus GRAM Jobmanager Callout Error API functions.
 * Otherwise, an error code is returned, and Globus GSI Credential functions
 * should not be subsequently called. This function may be called multiple
 * times. 
 *
 * To deactivate Globus GRAM Jobmanager Callout Error API, the following
 * function must be called: 
 *
 * @code
 *    globus_module_deactivate(GLOBUS_GRAM_JOBMANAGER_CALLOUT_ERROR_MODULE)
 * @endcode
 *
 * This function should be called once for each time Globus GRAM Jobmanager
 * Callout Error API was activated. 
 *
 */

/** Module descriptor
 * @ingroup globus_gram_jobmanager_callout_error_activation
 * @hideinitializer
 */
#define GLOBUS_GRAM_JOBMANAGER_CALLOUT_ERROR_MODULE    (&globus_i_gram_jobmanager_callout_error_module)

extern 
globus_module_descriptor_t              globus_i_gram_jobmanager_callout_error_module;

/**
 * @defgroup globus_gram_jobmanager_callout_error_datatypes Datatypes
 * @ingroup globus_gram_job_manager_callout_error 
 * @brief Datatypes
 */

/**
 * GRAM Jobmanager Callout Error codes
 * @ingroup globus_gram_jobmanager_callout_error_datatypes
 */
typedef enum
{
    /** Credentials not accepted */
    GLOBUS_GRAM_JOBMANAGER_CALLOUT_BAD_CREDS = 0,
    /** Authorization system misconfigured */
    GLOBUS_GRAM_JOBMANAGER_CALLOUT_AUTHZ_SYSTEM_ERROR = 1,
    /** Authorization denied */
    GLOBUS_GRAM_JOBMANAGER_CALLOUT_AUTHZ_DENIED = 2,
    /** Authorization denied - invalid job id */
    GLOBUS_GRAM_JOBMANAGER_CALLOUT_AUTHZ_DENIED_INVALID_JOB = 3,
    /** Authorization denied - executable not allowed */
    GLOBUS_GRAM_JOBMANAGER_CALLOUT_AUTHZ_DENIED_BAD_EXECUTABLE = 4,
    /** Last marker - never used */
    GLOBUS_GRAM_JOBMANAGER_CALLOUT_ERROR_LAST = 5
}
globus_gram_jobmanager_callout_error_t;

extern char * globus_i_gram_jobmanager_callout_error_strings[];

#define GLOBUS_GRAM_JOBMANAGER_CALLOUT_ERROR(__RESULT, __TYPE, __ERRSTR) \
{                                                                        \
    char *                          _tmp_str_ =                          \
        globus_common_create_string __ERRSTR;                            \
    (__RESULT) = globus_error_put(                                       \
        globus_error_construct_error(                                    \
            GLOBUS_GRAM_JOBMANAGER_CALLOUT_ERROR_MODULE,                 \
            (__RESULT) ? globus_error_get(__RESULT) : NULL,              \
            __TYPE,                                                      \
            __FILE__,                                                    \
            "GRAM Authorization Callout",                                \
            __LINE__,                                                    \
            "%s%s%s",                                                    \
            globus_i_gram_jobmanager_callout_error_strings[__TYPE],      \
            _tmp_str_ ? ": " : "",                                       \
            _tmp_str_ ? _tmp_str_ : ""));                                \
    if(_tmp_str_) free(_tmp_str_);                                       \
}


#ifdef __cplusplus
}
#endif

#endif
