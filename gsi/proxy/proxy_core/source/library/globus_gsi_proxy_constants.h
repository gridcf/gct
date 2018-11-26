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

#ifndef GLOBUS_GSI_PROXY_CONSTANTS_H
#define GLOBUS_GSI_PROXY_CONSTANTS_H

/**
 * @file globus_gsi_proxy_constants.h
 * @brief GSI Proxy Constants
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_gsi_proxy_constants Proxy Constants
 * @ingroup globus_gsi_proxy
 * @brief Proxy Constants
 */

/**
 * Proxy Error codes
 * @ingroup globus_gsi_proxy_constants
 */
typedef enum
{
    /** Success - never used */
    GLOBUS_GSI_PROXY_ERROR_SUCCESS = 0,
    /** Invalid proxy handle state */
    GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE = 1,
    /** Invalid proxy handle attributes state */
    GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS = 2,
    /** Error with ASN.1 proxycertinfo structure */
    GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO = 3,
    /** Error with ASN.1 proxypolicy structure */
    GLOBUS_GSI_PROXY_ERROR_WITH_PROXYPOLICY = 4,
    /** Error with proxy path length */
    GLOBUS_GSI_PROXY_ERROR_WITH_PATHLENGTH = 5,
    /** Error with the X.509 request structure */
    GLOBUS_GSI_PROXY_ERROR_WITH_X509_REQ = 6,
    /** Error with X.509 structure */
    GLOBUS_GSI_PROXY_ERROR_WITH_X509 = 7,
    /** Error with X.509 extensions */
    GLOBUS_GSI_PROXY_ERROR_WITH_X509_EXTENSIONS = 8,
    /** Error with private key */
    GLOBUS_GSI_PROXY_ERROR_WITH_PRIVATE_KEY = 9,
    /** Error with OpenSSL's BIO handle */
    GLOBUS_GSI_PROXY_ERROR_WITH_BIO = 10,
    /** Error with credential */
    GLOBUS_GSI_PROXY_ERROR_WITH_CREDENTIAL = 11,
    /** Error with credential handle */
    GLOBUS_GSI_PROXY_ERROR_WITH_CRED_HANDLE = 12,
    /** Error with credential handle attributes */
    GLOBUS_GSI_PROXY_ERROR_WITH_CRED_HANDLE_ATTRS = 13,
    /** System error */
    GLOBUS_GSI_PROXY_ERROR_ERRNO = 14,
    /** Unable to set proxy type */
    GLOBUS_GSI_PROXY_ERROR_SETTING_HANDLE_TYPE = 15,
    /** Invalid function parameter */
    GLOBUS_GSI_PROXY_INVALID_PARAMETER = 16,
    /** A error occurred while signing the proxy certificate */
    GLOBUS_GSI_PROXY_ERROR_SIGNING = 17,
    /** Last marker - never used */
    GLOBUS_GSI_PROXY_ERROR_LAST = 18
} globus_gsi_proxy_error_t;

#ifdef __cplusplus
}
#endif

#endif
