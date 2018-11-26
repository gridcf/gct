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

#ifndef GLOBUS_GSI_AUTHZ_H
#define GLOBUS_GSI_AUTHZ_H 1

/**
 * @file globus_gsi_authz.h
 * @brief GSI Authorization API
 */

#include "globus_common.h"
#include "gssapi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GLOBUS_GSI_AUTHZ_MODULE         (&globus_i_gsi_authz_module)

extern
globus_module_descriptor_t    globus_i_gsi_authz_module;

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus GSI Authorization API
 * @copydoc globus_gsi_authz
 */
#endif 
/**
 * @defgroup globus_gsi_authz GSI Authorization API
 * @brief Authorization Callouts
 *
 * @details
 * The GSI Authorization API provides an interface for making
 * generic authorization decisions using dynamically-loaded authorization
 * modules. The API documentation is divided into the following sections:
 *
 * - @ref globus_gsi_authz
 * - @ref globus_gsi_authz_constants
 */
typedef struct globus_i_gsi_authz_handle_s *
    globus_gsi_authz_handle_t;

typedef void (* globus_gsi_authz_cb_t)(
    void *                              callback_arg,
    globus_gsi_authz_handle_t           handle,
    globus_result_t                     result); 

globus_result_t
globus_gsi_authz_handle_init(
    globus_gsi_authz_handle_t *         handle,
    const char *                        service_name,
    const gss_ctx_id_t                  context,
    globus_gsi_authz_cb_t               callback,
    void *                              callback_arg);

globus_result_t
globus_gsi_authorize(
    globus_gsi_authz_handle_t           handle,
    const void *                        action,
    const void *                        object,
    globus_gsi_authz_cb_t               callback,
    void *                              callback_arg);

globus_result_t
globus_gsi_cancel_authz(
    globus_gsi_authz_handle_t           handle);

globus_result_t
globus_gsi_authz_handle_destroy(
    globus_gsi_authz_handle_t           handle,
    globus_gsi_authz_cb_t               callback,
    void *                              callback_arg);

globus_result_t
globus_gsi_authz_get_authorization_identity(
    globus_gsi_authz_handle_t           handle,
    char **				identity_ptr,
    globus_gsi_authz_cb_t               callback,
    void *                              callback_arg);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_GSI_AUTHZ_H */
