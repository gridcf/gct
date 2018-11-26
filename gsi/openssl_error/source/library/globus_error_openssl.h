/*
 * Copyright 1999-2013 University of Chicago
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

#ifndef GLOBUS_INCLUDE_OPENSSL_ERROR_H
#define GLOBUS_INCLUDE_OPENSSL_ERROR_H

/**
 * @file globus_error_openssl.h
 * @brief Globus Generic Error
 */
#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus OpenSSL Error API
 * @copydoc globus_openssl_error_api
 */
#endif
/**
 * @defgroup globus_openssl_error_api Globus OpenSSL Error API
 * @brief Globus OpenSSL Error API
 *
 * The globus_gsi_openssl_error functions provide a wrapper to error
 * types defined by OpenSSL. The types and prototypes for the
 * globus_gsi_openssl_error library are defined in the 
 * globus_error_openssl.h header. Applications which use these functions
 * must link with the libglobus_openssl_error library. The library is
 * distributed with a pkg-config module called globus-gsi-openssl-error
 * to automatically find header and library paths.
 *
 * @section globus_gsi_openssl_error_activation_section Library Initialization
 * @copydoc globus_gsi_openssl_error_activation
 * @section globus_openssl_error_object_section Error Object Data Definition
 * @copydoc globus_openssl_error_object
 * @section globus_openssl_error_utility_section Error Object Manipulators
 * @copydoc globus_openssl_error_utility
 */


#include "globus_common.h"
#include "globus_error_generic.h"
#include "openssl/err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 @defgroup globus_gsi_openssl_error_activation Activation
 @ingroup globus_openssl_error_api
 @brief Activation
 
 Globus GSI OpenSSL Error uses standard Globus module activation and
 deactivation. Before any Globus GSI OpenSSL Error functions are called, the
 following function must be called:
 
 @code
      globus_module_activate(GLOBUS_GSI_OPENSSL_ERROR_MODULE)
 @endcode
 
 
 This function returns GLOBUS_SUCCESS if Globus GSI OpenSSL Error was
 successfully initialized, and you are therefore allowed to
 subsequently call Globus GSI OpenSSL Error functions.  Otherwise, an error
 code is returned, and Globus GSI OpenSSL Error functions should not be
 subsequently called. This function may be called multiple times.
 
 To deactivate Globus GSI OpenSSL Error, the 
 following function must be called:
 
 @code
    globus_module_deactivate(GLOBUS_GSI_OPENSSL_ERROR_MODULE)
 @endcode
 
 This function should be called once for each time Globus GSI OpenSSL Error
 was activated. 
 */

/** Module descriptor
 * @ingroup globus_gsi_openssl_error_activation
 * @hideinitializer
 */
#define GLOBUS_GSI_OPENSSL_ERROR_MODULE (&globus_i_gsi_openssl_error_module)

extern
globus_module_descriptor_t		globus_i_gsi_openssl_error_module;


/** Error type definition
 * @ingroup globus_openssl_error_object
 * @hideinitializer
 */
#define GLOBUS_ERROR_TYPE_OPENSSL \
                                (&GLOBUS_ERROR_TYPE_OPENSSL_DEFINITION)

extern const globus_object_type_t GLOBUS_ERROR_TYPE_OPENSSL_DEFINITION;

#define _GOESL(s) globus_common_i18n_get_string(\
		GLOBUS_GSI_OPENSSL_ERROR_MODULE, \
		s)
/**
 * @defgroup globus_openssl_error_object Globus OpenSSL Error Object
 * @ingroup globus_openssl_error_api
 * @brief Globus OpenSSL Error Object
 *
 * The globus_openssl_error_handle_t is an opaque structure pointer which
 * contains information about the OpenSSL call which generated an error. The
 * functions in the @link globus_openssl_error_object Globus OpenSSL Error Object @endlink section of
 * the API are used to create, manipulate, and destroy Globus
 * OpenSSL Error object data. 
 */

typedef struct globus_l_openssl_error_handle_s *
                                        globus_openssl_error_handle_t;

unsigned long
globus_openssl_error_handle_get_error_code(
    globus_openssl_error_handle_t       error_code);

const char *
globus_openssl_error_handle_get_filename(
    globus_openssl_error_handle_t       handle);

int
globus_openssl_error_handle_get_linenumber(
    globus_openssl_error_handle_t       handle);

const char *
globus_openssl_error_handle_get_library(
    globus_openssl_error_handle_t       handle);

const char *
globus_openssl_error_handle_get_function(
    globus_openssl_error_handle_t       handle);

const char *
globus_openssl_error_handle_get_reason(
    globus_openssl_error_handle_t       handle);

const char *
globus_openssl_error_handle_get_data(
    globus_openssl_error_handle_t       handle);

int
globus_openssl_error_handle_get_data_flags(
    globus_openssl_error_handle_t       handle);


globus_object_t *
globus_error_construct_openssl_error(
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause);

globus_object_t *
globus_error_initialize_openssl_error(
    globus_object_t *                   error,
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    globus_openssl_error_handle_t       openssl_error_handle);

/**
 * @defgroup globus_openssl_error_utility Error Object Helper Functions
 * @ingroup globus_openssl_error_api
 * @brief Error Object Helper Functions
 *
 * The functions in the @link globus_openssl_error_utility Error Object Helper Functions @endlink section
 * deal with Globus error objects constructed with OpenSSL error data.
 * These functions manipulate the generic globus_object_t representation
 * of an OpenSSL error.
 */

globus_object_t *
globus_error_wrap_openssl_error(
    globus_module_descriptor_t *        base_source,
    int                                 error_type,
    const char *                        source_file,
    const char *                        source_func,
    int                                 source_line,
    const char *                        format,
    ...);

globus_bool_t
globus_error_match_openssl_error(
    globus_object_t *                   error,
    unsigned long                       library,
    unsigned long                       function,
    unsigned long                       reason);

const char *
globus_error_openssl_error_get_filename(
    globus_object_t *                   error);

int
globus_error_openssl_error_get_linenumber(
    globus_object_t *                   error);

const char *
globus_error_openssl_error_get_library(
    globus_object_t *                   error);

const char *
globus_error_openssl_error_get_function(
    globus_object_t *                   error);

const char *
globus_error_openssl_error_get_reason(
    globus_object_t *                   error);

const char *
globus_error_openssl_error_get_data(
    globus_object_t *                   error);

int
globus_error_openssl_error_get_data_flags(
    globus_object_t *                   error);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_INCLUDE_OPENSSL_ERROR_H */
