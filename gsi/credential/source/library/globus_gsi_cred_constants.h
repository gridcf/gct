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
 * @file globus_gsi_cred_constants.h
 * @brief Globus GSI Credential Library
 * @author Sam Lang, Sam Meder
 */

#ifndef GLOBUS_GSI_CREDENTIAL_CONSTANTS_H
#define GLOBUS_GSI_CREDENTIAL_CONSTANTS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_gsi_credential_constants Credential Constants
 * @brief Credential Constants
 */
/**
 * Credential Error codes
 * @ingroup globus_gsi_credential_constants
 */
typedef enum
{
    /** Success - never used */
    GLOBUS_GSI_CRED_ERROR_SUCCESS = 0,
    /** Failed to read proxy credential*/
    GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED = 1,
    /** Failed to read host credential*/
    GLOBUS_GSI_CRED_ERROR_READING_HOST_CRED = 2,
    /** Failed to read service credential*/
    GLOBUS_GSI_CRED_ERROR_READING_SERVICE_CRED = 3,
    /** Failed to read user credential*/
    GLOBUS_GSI_CRED_ERROR_READING_CRED = 4,
    /** Failed to write credential */
    GLOBUS_GSI_CRED_ERROR_WRITING_CRED = 5,
    /** Failed to write proxy credential */
    GLOBUS_GSI_CRED_ERROR_WRITING_PROXY_CRED = 6,
    /** Error checking for proxy credential */
    GLOBUS_GSI_CRED_ERROR_CHECKING_PROXY = 7,
    /** Failed to verify credential */
    GLOBUS_GSI_CRED_ERROR_VERIFYING_CRED = 8,
    /** Invalid credential */
    GLOBUS_GSI_CRED_ERROR_WITH_CRED = 9,
    /** Invalid certificate */
    GLOBUS_GSI_CRED_ERROR_WITH_CRED_CERT = 10,
    /** Invalid private key */
    GLOBUS_GSI_CRED_ERROR_WITH_CRED_PRIVATE_KEY = 11,
    /** Invalid certificate chain*/
    GLOBUS_GSI_CRED_ERROR_WITH_CRED_CERT_CHAIN = 12,
    /** System error */
    GLOBUS_GSI_CRED_ERROR_ERRNO = 13,
    /** A Globus GSI System Configuration call failed */
    GLOBUS_GSI_CRED_ERROR_SYSTEM_CONFIG = 14,
    /** Invalid credential handle attributes */
    GLOBUS_GSI_CRED_ERROR_WITH_CRED_HANDLE_ATTRS = 15,
    /** Faulty SSL context */
    GLOBUS_GSI_CRED_ERROR_WITH_SSL_CTX = 16,
    /** Faulty callback data */
    GLOBUS_GSI_CRED_ERROR_WITH_CALLBACK_DATA = 17,
    /** Failed to aggregate errors */
    GLOBUS_GSI_CRED_ERROR_CREATING_ERROR_OBJ = 18,
    /** Error reading private key - the key is password protected */
    GLOBUS_GSI_CRED_ERROR_KEY_IS_PASS_PROTECTED = 19,
    /** Couldn't find credential to read */
    GLOBUS_GSI_CRED_ERROR_NO_CRED_FOUND = 20,
    /** Credential subjects do not compare */
    GLOBUS_GSI_CRED_ERROR_SUBJECT_CMP = 21,
    /** Unable to obtain service name from CN entry */
    GLOBUS_GSI_CRED_ERROR_GETTING_SERVICE_NAME = 22,
    /** Invalid function parameter */
    GLOBUS_GSI_CRED_ERROR_BAD_PARAMETER = 23,
    /** Failed to process certificate subject */
    GLOBUS_GSI_CRED_ERROR_WITH_CRED_CERT_NAME = 24,
    /** End marker - never used */
    GLOBUS_GSI_CRED_ERROR_LAST = 25
} globus_gsi_cred_error_t;

/**
 * Credential Type
 * @ingroup globus_gsi_credential_constants
 *
 * An enum representing a GSI Credential Type which holds info about 
 * the type of a particular credential.  The three types of credential
 * can be: GLOBUS_PROXY, GLOBUS_USER, or GLOBUS_HOST.
 * 
 * @see globus_gsi_cred_handle
 */
typedef enum 
{
    GLOBUS_PROXY,
    GLOBUS_USER,
    GLOBUS_HOST,
    GLOBUS_SERVICE,
    GLOBUS_SO_END
} globus_gsi_cred_type_t;

#define GLOBUS_NULL_GROUP               "GLOBUS_NULL_GROUP"
#define GLOBUS_NULL_POLICY              "GLOBUS_NULL_POLICY"

#ifdef __cplusplus
}
#endif

#endif

