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
 * @file globus_gram_client.h
 * @brief GRAM Client API
 *
 * @details
 * This header file contains the exported client interface of 
 * the Resource Allocation Management System.
 */

#ifndef GLOBUS_GRAM_CLIENT_H
#define GLOBUS_GRAM_CLIENT_H

/* Include header files */
#include "globus_common.h"
#include "globus_io.h"
#include "globus_gram_protocol_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage GRAM Client API
 * @copydoc globus_gram_client
 */
#endif

/**
 * @defgroup globus_gram_client GRAM Client API
 * @brief GRAM Client API
 *
 * The resource management API provides function for requesting
 * that a job be started or terminated, as well as for requesting
 * information about the status of a job.
 */

/******************************************************************************
                               Type definitions
******************************************************************************/


/**
 * @defgroup globus_gram_client_callback Job state callbacks
 * @ingroup globus_gram_client
 * @brief Job state callbacks
 */

/**
 * @brief Signature for GRAM state notification callback functions
 * @ingroup globus_gram_client_callback
 *
 * @details
 * The globus_gram_client_callback_func_t type describes the function
 * signature for job state callbacks.  A pointer to a function
 * of this type is passed to the globus_gram_client_callback_allow()
 * function to create a callback contact. The contact string can be passed to
 * globus_gram_client_job_request() or
 * globus_gram_client_job_callback_register() to let the job management
 * service know to where to send information on GRAM job state changes.
 *
 * @param user_callback_arg
 *     A pointer to application-specific data.
 * @param job_contact
 *     A string containing the job contact. This string indicates which job
 *     this callback is referring to. It should in most cases match the return
 *     value @a job_contact from a call to globus_gram_client_job_request()
 *     or in the @a job_contact parameter to the
 *     globus_gram_client_nonblocking_func_t used with
 *     globus_gram_client_register_job_request(). However, in some cases,
 *     the port number in the job contact URL may change if the job manager
 *     is restarted.
 * @param state
 *     The new state (one of the #globus_gram_protocol_job_state_t values)
 *     of the job.
 * @param errorcode
 *     The error code if the @a state parameter is equal to
 *     GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED.
 */
typedef void (* globus_gram_client_callback_func_t)(
    void *                              user_callback_arg,
    char *                              job_contact,
    int                                 state,
    int                                 errorcode);

/**
 * @brief Extensible job information structure
 * @ingroup globus_gram_client_callback
 *
 * @details
 * The #globus_gram_client_job_info_t data type is used to pass protocol
 * extensions along with the standard job status
 * information included in the GRAM2 protocol. This structure contains the
 * information returned in job state callbacks plus a hash table of extension
 * entries that contain #globus_gram_protocol_extension_t name-value pairs.
 */
typedef struct globus_gram_client_job_info_s
{
    /**
     * Table of extension values
     */
    globus_hashtable_t                  extensions;
    /**
     * GRAM Job Contact String
     */
    char *                              job_contact;
    /**
     * GRAM Job State
     */
    int                                 job_state;
    /**
     * GRAM Error Code
     */
    int                                 protocol_error_code;
}
globus_gram_client_job_info_t;

/**
 * @brief Signature for GRAM state notification callback functions with
 * extension support
 * @ingroup globus_gram_client_callback
 *
 * @details
 * The #globus_gram_client_info_callback_func_t type describes the function
 * signature for job state callbacks that carry any GRAM protocol extensions
 * beyond the set used in GRAM2.  A pointer to a function
 * of this type is passed to the globus_gram_client_info_callback_allow()
 * function to create a callback contact that can handle extensions. The
 * contact string can be passed to
 * globus_gram_client_job_request() or
 * globus_gram_client_job_callback_register() to let the job management
 * service know to where to send information on GRAM job state changes.
 *
 * @param  user_callback_arg
 *     Application-specific callback information.
 * @param job_contact
 *     Job this information is related to
 * @param job_info
 *     Job state and extensions
 *
 * @see globus_gram_client_info_callback_allow()
 */
typedef void (* globus_gram_client_info_callback_func_t)(
    void *                              user_callback_arg,
    const char *                        job_contact,
    globus_gram_client_job_info_t *     job_info);

/**
 * @brief GRAM client operation attribute
 * @ingroup globus_gram_client_attr
 *
 * @details
 * The #globus_gram_client_attr_t type is an opaque type describing
 * GRAM attributes. It can be accessed or modified by functions in the 
 * @ref globus_gram_client_attr documentation.
 */
typedef void * globus_gram_client_attr_t;


/**
 * @brief Default GRAM client operation attribute
 * @ingroup globus_gram_client_attr
 * @hideinitializer
 * @details
 * The GLOBUS_GRAM_CLIENT_NO_ATTR macro defines a constant for use
 * when a user of the GRAM client API does not want to specify any
 * non-default GRAM attributes.
 */
#define GLOBUS_GRAM_CLIENT_NO_ATTR (globus_gram_client_attr_t) NULL

/**
 * @brief Signature for callbacks signalling completion of non-blocking GRAM requests
 * @ingroup globus_gram_client_callback
 *
 * @details
 * The #globus_gram_client_info_callback_func_t type describes the function
 * signature for callbacks which indicate that a GRAM operation has completed.
 * A pointer to a function of this type is passed to the
 * following functions:
 * - globus_gram_client_register_job_request()
 * - globus_gram_client_register_job_cancel()
 * - globus_gram_client_register_job_status()
 * - globus_gram_client_register_job_refresh_credentials()
 * - globus_gram_client_register_job_signal()
 * - globus_gram_client_register_job_callback_registration()
 * - globus_gram_client_register_job_callback_unregistration()
 * - globus_gram_client_register_ping()
 * 
 * @param user_callback_arg
 *     Application-specific callback information.
 * @param operation_failure_code
 *     The result of the nonblocking operation , indicating whether the
 *     operation was processed by the job manager successfully or not.
 * @param job_contact
 *     A string containing the job contact associated with this non-blocking
 *     operation.
 * @param job_state
 *     The state (one of the #globus_gram_protocol_job_state_t
 *     values) of the job related to this non-blocking operation.
 * @param job_failure_code
 *     The error code of the job request if the job_state parameter
 *     is GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED. Otherwise, its value is
 *     undefined.
 */
typedef void (* globus_gram_client_nonblocking_func_t)(
    void *                              user_callback_arg,
    globus_gram_protocol_error_t        operation_failure_code,
    const char *                        job_contact,
    globus_gram_protocol_job_state_t    job_state,
    globus_gram_protocol_error_t        job_failure_code);


/**
 * @defgroup globus_gram_client_job_functions GRAM Job Functions
 * @ingroup globus_gram_client
 * @brief GRAM Job Functions
 */

/******************************************************************************
                              Function prototypes
******************************************************************************/
int 
globus_gram_client_callback_allow(
    globus_gram_client_callback_func_t  callback_func,
    void *                              user_callback_arg,
    char **                             callback_contact);


int
globus_gram_client_info_callback_allow(
    globus_gram_client_info_callback_func_t
                                        callback_func,
    void *                              user_callback_arg,
    char **                             callback_contact);

int
globus_gram_client_register_job_request(
    const char *                        resource_manager_contact,
    const char *                        description,
    int                                 job_state_mask,
    const char *                        callback_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg);

int 
globus_gram_client_job_request(
    const char *                        resource_manager_contact,
    const char *                        description,
    int                                 job_state_mask,
    const char *                        callback_contact,
    char **                             job_contact);

int
globus_gram_client_register_job_request_with_info(
    const char *                        resource_manager_contact,
    const char *                        description,
    int                                 job_state_mask,
    const char *                        callback_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_info_callback_func_t
                                        callback,
    void *                              callback_arg);

int 
globus_gram_client_job_request_with_info(
    const char *                        resource_manager_contact,
    const char *                        description,
    int                                 job_state_mask,
    const char *                        callback_contact,
    char **                             job_contact,
    globus_gram_client_job_info_t *     info);

int
globus_gram_client_register_job_cancel(
    const char *                        job_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg);

int 
globus_gram_client_job_cancel(
    const char *                        job_contact);

int
globus_gram_client_register_job_status(
    const char *                        job_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg);

int
globus_gram_client_register_job_status_with_info(
    const char *                        job_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_info_callback_func_t
                                        info_callback,
    void *                              callback_arg);


int
globus_gram_client_job_refresh_credentials(
    char *                              job_contact,
    gss_cred_id_t                       creds);

int
globus_gram_client_register_job_refresh_credentials(
    char *                              job_contact,
    gss_cred_id_t                       creds,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg);

int
globus_gram_client_job_status(
    const char *                        job_contact,
    int *                               job_status,
    int *                               failure_code);

int
globus_gram_client_job_status_with_info(
    const char *                        job_contact,
    globus_gram_client_job_info_t *     job_info);

int
globus_gram_client_register_job_signal(
    const char *                        job_contact,
    globus_gram_protocol_job_signal_t   signal,
    const char *                        signal_arg,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg);

int
globus_gram_client_job_signal(
    const char *                        job_contact,
    globus_gram_protocol_job_signal_t   signal,
    const char *                        signal_arg,
    int *                               job_status,
    int *                               failure_code);


int
globus_gram_client_register_job_callback_registration(
    const char *                        job_contact,
    int                                 job_state_mask,
    const char *                        callback_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg);

int
globus_gram_client_job_callback_register(
    const char *                        job_contact,
    int                                 job_state_mask,
    const char *                        callback_contact,
    int *                               job_status,
    int *                               failure_code);

int
globus_gram_client_register_job_callback_unregistration(
    const char *                        job_contact,
    const char *                        callback_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg);

int
globus_gram_client_job_callback_unregister(
    const char *                        job_contact,
    const char *                        callback_contact,
    int *                               job_status,
    int *                               failure_code);

int 
globus_gram_client_callback_disallow(
    char *                              callback_contact);

int 
globus_gram_client_job_contact_free(
    char *                              job_contact);

const char *
globus_gram_client_error_string(
    int                                 error_code);

int
globus_gram_client_version(void);

int
globus_gram_client_set_credentials(gss_cred_id_t new_credentials);

int 
globus_gram_client_ping(
    const char *                        resource_manager_contact);

int 
globus_gram_client_register_ping(
    const char *                        resource_manager_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg);

int 
globus_gram_client_get_jobmanager_version(
    const char *                        resource_manager_contact,
    globus_hashtable_t *                extensions);

int 
globus_gram_client_register_get_jobmanager_version(
    const char *                        resource_manager_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_info_callback_func_t
                                        info_callback,
    void *                              callback_arg);

void
globus_gram_client_debug(void);

/**
 * @defgroup globus_gram_client_attr GRAM Client Attribute Functions
 * @ingroup globus_gram_client
 * @brief GRAM Client Attribute Functions
 */
int
globus_gram_client_attr_init(
    globus_gram_client_attr_t *         attr);
int
globus_gram_client_attr_destroy(
    globus_gram_client_attr_t *         attr);

int
globus_gram_client_attr_set_credential(
    globus_gram_client_attr_t           attr,
    gss_cred_id_t                       credential);

int
globus_gram_client_attr_get_credential(
    globus_gram_client_attr_t           attr,
    gss_cred_id_t *                     credential);

int
globus_gram_client_attr_set_delegation_mode(
    globus_gram_client_attr_t           attr,
    globus_io_secure_delegation_mode_t  mode);

int
globus_gram_client_attr_get_delegation_mode(
    globus_gram_client_attr_t           attr,
    globus_io_secure_delegation_mode_t *mode);

void
globus_gram_client_job_info_destroy(
    globus_gram_client_job_info_t *     info);

#define GLOBUS_GRAM_CLIENT_MODULE (&globus_gram_client_module)

extern globus_module_descriptor_t       globus_gram_client_module;

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_GRAM_CLIENT_H */
