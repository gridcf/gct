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

#ifndef GLOBUS_GSI_CALLBACK_CONSTANTS_H
#define GLOBUS_GSI_CALLBACK_CONSTANTS_H

/**
 * @file globus_gsi_callback_constants.h
 * @brief Globus GSI Callback
 * @author Sam Meder, Sam Lang
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_gsi_callback_constants GSI Callback Constants
 * @brief GSI Callback Constants
 */
/**
 * GSI Callback Error codes
 * @ingroup globus_gsi_callback_constants
 */
typedef enum
{
    /** Success - never used */
    GLOBUS_GSI_CALLBACK_ERROR_SUCCESS = 0,
    /** Error verifying credential */
    GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED = 1,
    /** The certificate is not yet valid */
    GLOBUS_GSI_CALLBACK_ERROR_CERT_NOT_YET_VALID = 2,
    /** Unable to discover a local trusted CA for a given certificate */
    GLOBUS_GSI_CALLBACK_ERROR_CANT_GET_LOCAL_CA_CERT = 3,
    /** The certificate has expired */
    GLOBUS_GSI_CALLBACK_ERROR_CERT_HAS_EXPIRED = 4,
    /** The proxy format is invalid */
    GLOBUS_GSI_CALLBACK_ERROR_INVALID_PROXY = 5,
    /** Limited proxies are not accepted */
    GLOBUS_GSI_CALLBACK_ERROR_LIMITED_PROXY = 6,
    /** Invalid CRL */
    GLOBUS_GSI_CALLBACK_ERROR_INVALID_CRL = 7,
    /** The certificate has been revoked */
    GLOBUS_GSI_CALLBACK_ERROR_REVOKED_CERT = 8,
    /** The cert chain contains both legacy on RFC compliant proxies */
    GLOBUS_GSI_CALLBACK_ERROR_MIXING_DIFFERENT_PROXY_TYPES = 9,
    /** Could not verify certificate chain against the signing policy for the issuing CA */
    GLOBUS_GSI_CALLBACK_ERROR_WITH_SIGNING_POLICY = 10,
    /** OldGAA error */
    GLOBUS_GSI_CALLBACK_ERROR_OLD_GAA = 11,
    /** Error with the callback data structure */
    GLOBUS_GSI_CALLBACK_ERROR_CALLBACK_DATA = 12,
    /** System error */
    GLOBUS_GSI_CALLBACK_ERROR_ERRNO = 13,
    /** Error setting or getting the cert chain from callback data */
    GLOBUS_GSI_CALLBACK_ERROR_CERT_CHAIN = 14,
    /** Error getting callback data index */
    GLOBUS_GSI_CALLBACK_ERROR_WITH_CALLBACK_DATA_INDEX = 15,
    /** Exceeded the path length specified in the proxy cert info extension */
    GLOBUS_GSI_CALLBACK_ERROR_PROXY_PATH_LENGTH_EXCEEDED = 16,
    /** Last marker - never used */
    GLOBUS_GSI_CALLBACK_ERROR_LAST = 18

} globus_gsi_callback_error_t;

/**
 * Constant passed to SSL_CTX_set_verify_depth() and
 * SSL_set_verify_depth() to increase allowed certificate depth to 100
 * rather than the OpenSSL default of 9. 100 is an arbitrarily chosen
 * large value.
 */
#define GLOBUS_GSI_CALLBACK_VERIFY_DEPTH 100

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_GSI_CALLBACK_CONSTANTS_H */

