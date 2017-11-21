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
 * @file globus_gsi_credential.h
 * @brief Globus GSI Credential Library
 * @author Sam Lang, Sam Meder
*/

#ifndef GLOBUS_INCLUDE_GLOBUS_GSI_CREDENTIAL_H
#define GLOBUS_INCLUDE_GLOBUS_GSI_CREDENTIAL_H

#include "globus_common.h"
#include "globus_error_openssl.h"
#include "globus_gsi_cred_constants.h"
#include "globus_gsi_callback.h"
#include "globus_gsi_cert_utils.h"
#include "globus_gsi_cred_constants.h"

#include "openssl/x509.h"
#include "openssl/bio.h"
#include "openssl/ssl.h"
#include "globus_error_generic.h"
#include "globus_error_openssl.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus GSI Credential
 * @copydoc globus_gsi_credential
 */
#endif

/**
 * @defgroup globus_gsi_credential Globus GSI Credential
 *
 * The Globus GSI Credential library. This library contains functions that
 * provide support for handling X.509 based PKI credentials
 *
 * - @ref globus_gsi_credential_activation
 * - @ref globus_gsi_cred_handle
 * - @ref globus_gsi_cred_handle_attrs
 * - @ref globus_gsi_cred_operations
 * - @ref globus_gsi_credential_constants
 */

/** 
 * @defgroup globus_gsi_credential_activation Activation
 * @ingroup globus_gsi_credential
 *
 * Globus GSI Credential uses standard Globus module activation and
 * deactivation.  Before any Globus GSI Credential functions are called, the
 * following function must be called:
 *
 * @code
 *      globus_module_activate(GLOBUS_GSI_CREDENTIAL_MODULE)
 * @endcode
 *
 *
 * This function returns GLOBUS_SUCCESS if Globus GSI Credential was
 * successfully initialized, and you are therefore allowed to
 * subsequently call Globus GSI Credential functions.  Otherwise, an error
 * code is returned, and Globus GSI Credential functions should not be
 * subsequently called. This function may be called multiple times.
 *
 * To deactivate Globus GSI Credential, the following function must be called:
 *
 * @code
 *    globus_module_deactivate(GLOBUS_GSI_CREDENTIAL_MODULE)
 * @endcode
 *
 * This function should be called once for each time Globus GSI Credential
 * was activated. 
 *
 */

/** Module descriptor
 * @ingroup globus_gsi_credential_activation
 * @hideinitializer
 */
#define GLOBUS_GSI_CREDENTIAL_MODULE    (&globus_i_gsi_credential_module)

extern 
globus_module_descriptor_t              globus_i_gsi_credential_module;

#define _GCRSL(s) globus_common_i18n_get_string( \
		    GLOBUS_GSI_CREDENTIAL_MODULE, \
		    s) 
/**
 * @defgroup globus_gsi_cred_handle Credential Handle Management
 * @ingroup globus_gsi_credential
 *
 * Create/Destroy/Modify a GSI Credential Handle.
 *
 * Within the Globus GSI Credential Library, all credential operations 
 * require a handle parameter.  Currently only one operation may be
 * in progress at once per credential handle.
 *
 * This section defines operations to create, modify and destroy GSI
 * Credential handles.
 */

/**
 * @defgroup globus_gsi_cred_handle_attrs Credential Handle Attributes
 * @ingroup globus_gsi_credential
 *
 * Create/Destroy/Modify GSI Credential Handle Attributes.
 *
 * Within the Globus GSI Credential Library, all credential handles 
 * contain a attribute structure, which in turn contains handle instance
 * independent attributes.
 *
 * This section defines operations to create, modify and destroy GSI
 * Credential handle attributes.
 */


/**
 * @defgroup globus_gsi_cred_operations Credential Operations
 * @ingroup globus_gsi_credential
 *
 * Read/Write a GSI Credential Handle.
 *
 * This section defines operations to read and write GSI
 * Credential handles.
 */

/**
 * GSI Credential Handle.
 * @ingroup globus_gsi_cred_handle
 *
 * A GSI Credential handle keeps track of state relating to a credential.
 * Handles can have immutable @ref globus_gsi_cred_handle_attrs_t "attributes"
 * associated with them.  All credential @link globus_gsi_cred_operations
 * operations @endlink take a credential handle pointer as a parameter.
 * 
 * @see globus_gsi_cred_handle_init(),
 * globus_gsi_cred_handle_destroy(), globus_gsi_cred_handle_attrs_t
 */
typedef struct globus_l_gsi_cred_handle_s * 
                                        globus_gsi_cred_handle_t;

/**
 * Credential Handle Attributes.
 * @ingroup globus_gsi_cred_handle_attrs
 *
 * Credential handle attributes provide a set of immutable parameters
 * for a credential handle
 *
 * @see globus_gsi_cred_handle_init
 */

typedef struct globus_l_gsi_cred_handle_attrs_s *
                                        globus_gsi_cred_handle_attrs_t;


globus_result_t globus_gsi_cred_handle_init(
    globus_gsi_cred_handle_t *          handle,
    globus_gsi_cred_handle_attrs_t      handle_attrs);

globus_result_t globus_gsi_cred_handle_destroy(
    globus_gsi_cred_handle_t            handle);

globus_result_t globus_gsi_cred_handle_copy(
    globus_gsi_cred_handle_t            source,
    globus_gsi_cred_handle_t *          dest);

globus_result_t globus_gsi_cred_handle_attrs_init(
    globus_gsi_cred_handle_attrs_t *    handle_attrs);

globus_result_t globus_gsi_cred_handle_attrs_destroy(
    globus_gsi_cred_handle_attrs_t      handle_attrs);

globus_result_t globus_gsi_cred_handle_attrs_copy(
    globus_gsi_cred_handle_attrs_t      source,
    globus_gsi_cred_handle_attrs_t *    dest);

globus_result_t globus_gsi_cred_read(
    globus_gsi_cred_handle_t            handle,
    X509_NAME *                         desired_subject);

globus_result_t globus_gsi_cred_read_proxy(
    globus_gsi_cred_handle_t            handle,
    const char *                        proxy_filename);

globus_result_t globus_gsi_cred_read_proxy_bio(
    globus_gsi_cred_handle_t            handle,
    BIO *                               bio);

globus_result_t globus_gsi_cred_read_key(
    globus_gsi_cred_handle_t            handle,
    const char *                        key_filename,
    int                                 (*pw_cb)());

globus_result_t globus_gsi_cred_read_cert(
    globus_gsi_cred_handle_t            handle,
    const char *                        cert_filename);

globus_result_t globus_gsi_cred_read_cert_bio(
    globus_gsi_cred_handle_t            handle,
    BIO *                               bio);

globus_result_t globus_gsi_cred_read_cert_buffer(
    const char *                        pem_buf,
    globus_gsi_cred_handle_t *          out_handle,
    X509 **                             out_cert,
    STACK_OF(X509) **                   out_cert_chain,
    char **                             out_subject);

globus_result_t globus_gsi_cred_read_pkcs12(
    globus_gsi_cred_handle_t            handle,
    const char *                        pkcs12_filename);

globus_result_t globus_gsi_cred_write(
    globus_gsi_cred_handle_t            handle,
    BIO *                               bio);

globus_result_t globus_gsi_cred_write_proxy(
    globus_gsi_cred_handle_t            handle,
    const char *                        proxy_filename);

globus_result_t
globus_gsi_cred_verify_cert_chain(
    globus_gsi_cred_handle_t            cred_handle,
    globus_gsi_callback_data_t          callback_data);

globus_result_t
globus_gsi_cred_verify_cert_chain_when(
    globus_gsi_cred_handle_t            cred_handle,
    globus_gsi_callback_data_t          callback_data,
    time_t                              check_time);

globus_result_t globus_gsi_cred_verify(
    globus_gsi_cred_handle_t            handle);

globus_result_t globus_gsi_cred_get_X509_subject_name(
    globus_gsi_cred_handle_t            handle,
    X509_NAME **                        subject_name);

globus_result_t globus_gsi_cred_get_subject_name(
    globus_gsi_cred_handle_t            handle,
    char **                             subject_name);

globus_result_t globus_gsi_cred_get_policies(
    globus_gsi_cred_handle_t            handle,
    STACK_OF(OPENSSL_STRING) **         policies);

globus_result_t globus_gsi_cred_get_policy_languages(
    globus_gsi_cred_handle_t            handle,
    STACK_OF(ASN1_OBJECT) **            languages);

globus_result_t globus_gsi_cred_get_path_lengths(
    globus_gsi_cred_handle_t            handle,
    STACK_OF(ASN1_INTEGER) *            integer);

globus_result_t globus_gsi_cred_get_X509_issuer_name(
    globus_gsi_cred_handle_t            handle,
    X509_NAME **                        issuer_name);

globus_result_t globus_gsi_cred_get_issuer_name(
    globus_gsi_cred_handle_t            handle,
    char **                             issuer_name);

globus_result_t globus_gsi_cred_get_X509_identity_name(
    globus_gsi_cred_handle_t            handle,
    X509_NAME **                        identity_name);

globus_result_t globus_gsi_cred_get_identity_name(
    globus_gsi_cred_handle_t            handle,
    char **                             identity_name);

globus_result_t globus_gsi_cred_set_cert(
    globus_gsi_cred_handle_t            handle,
    X509 *                              cert);

globus_result_t globus_gsi_cred_set_key(
    globus_gsi_cred_handle_t            handle,
    EVP_PKEY *                          key);

globus_result_t globus_gsi_cred_set_cert_chain(
    globus_gsi_cred_handle_t            handle,
    STACK_OF(X509) *                    cert_chain);

globus_result_t globus_gsi_cred_get_cert(
    globus_gsi_cred_handle_t            handle,
    X509 **                             cert);

globus_result_t globus_gsi_cred_get_key(
    globus_gsi_cred_handle_t            handle,
    EVP_PKEY **                         key);

globus_result_t globus_gsi_cred_get_cert_chain(
    globus_gsi_cred_handle_t            handle,
    STACK_OF(X509) **                   cert_chain);

globus_result_t globus_gsi_cred_get_handle_attrs(
    globus_gsi_cred_handle_t            handle,
    globus_gsi_cred_handle_attrs_t *    handle_attrs);

globus_result_t globus_gsi_cred_get_lifetime(
    globus_gsi_cred_handle_t            handle,
    time_t *                            lifetime);

globus_result_t globus_gsi_cred_get_goodtill(
    globus_gsi_cred_handle_t            handle,
    time_t *                            goodtill);
 
globus_result_t globus_gsi_cred_get_cert_type(
    globus_gsi_cred_handle_t            handle,
    globus_gsi_cert_utils_cert_type_t * type);

globus_result_t globus_gsi_cred_get_key_bits(
    globus_gsi_cred_handle_t            handle,
    int *                               key_bits);

GLOBUS_DEPRECATED(
globus_result_t globus_gsi_cred_handle_attrs_set_ca_cert_dir(
    globus_gsi_cred_handle_attrs_t      handle_attrs,
    const char *                        ca_cert_dir));

GLOBUS_DEPRECATED(
globus_result_t globus_gsi_cred_handle_attrs_get_ca_cert_dir(
    globus_gsi_cred_handle_attrs_t      handle_attrs,
    char **                             ca_cert_dir));

globus_result_t globus_gsi_cred_handle_attrs_set_search_order(
    globus_gsi_cred_handle_attrs_t      handle_attrs,
    globus_gsi_cred_type_t              search_order[]); /*{PROXY,USER,HOST}*/


globus_result_t globus_gsi_cred_handle_attrs_get_search_order(
    globus_gsi_cred_handle_attrs_t      handle_attrs,
    globus_gsi_cred_type_t *            search_order[]);/*{PROXY,USER,HOST}*/

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_INCLUDE_GLOBUS_GSI_CREDENTIAL_H */
