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
 * @file globus_openssl.h
 * @brief Globus OpenSSL Module
 */

#ifndef GLOBUS_INCLUDE_GLOBUS_OPENSSL_H
#define GLOBUS_INCLUDE_GLOBUS_OPENSSL_H

#include "globus_common.h"

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus OpenSSL Module
 * @copydoc globus_openssl_module
 */
#endif
/**
 * @defgroup globus_openssl_module Globus OpenSSL Module
 * @brief Globus OpenSSL Module
 *
 * The globus_openssl library is motivated by the desire to
 * make OpenSSL thread safe. This is done by allocating a mutex pool
 * and setting relevant callback functions in the module activation
 * functions. 
 *
 * Any program that uses OpenSSL functions with Globus must include
 * the globus_openssl.h header.
 *
 * @copydoc globus_openssl_activation
 */


#ifdef __cplusplus
extern "C" {
#endif


/**
 * @defgroup globus_openssl_activation Activation
 * @ingroup globus_openssl_module
 * @brief Activation
 *
 * Globus OpenSSL uses standard Globus module activation and
 * deactivation. Before any OpenSSL functions are called, the
 * following function must be called:
 *
   @code
        globus_module_activate(GLOBUS_OPENSSL_MODULE)
   @endcode
 *
 *
 * This function returns GLOBUS_SUCCESS if OpenSSL was
 * successfully initialized, and you are therefore allowed to
 * subsequently call OpenSSL functions.  Otherwise, an error
 * code is returned, and OpenSSL functions should not
 * subsequently be called. This function may be called multiple times.
 *
 * To deactivate the OpenSSL module , the following function must be
 * called: 
 *
   @code
      globus_module_deactivate(GLOBUS_OPENSSL_MODULE)
   @endcode
 *
 * This function should be called once for each time OpenSSL
 * was activated. 
 *
 */

/** Module descriptor
 * @ingroup globus_openssl_activation
 * @hideinitializer
 */
#define GLOBUS_OPENSSL_MODULE (&globus_i_openssl_module)

extern
globus_module_descriptor_t		globus_i_openssl_module;

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_INCLUDE_GLOBUS_OPENSSL_H */
