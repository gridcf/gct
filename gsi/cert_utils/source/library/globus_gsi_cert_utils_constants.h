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
 * @file globus_gsi_cert_utils_constants.h
 * @brief Globus GSI Cert Utils
 * @author Sam Meder, Sam Lang
 */
#endif

#ifndef GLOBUS_GSI_CERT_UTILS_CONSTANTS_H
#define GLOBUS_GSI_CERT_UTILS_CONSTANTS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_gsi_cert_utils_constants Cert Utils Constants
 * @ingroup globus_gsi_cert_utils
 * @brief Cert Utils Constants
 */
/**
 * Cert Utils Error Codes
 * @ingroup globus_gsi_cert_utils_constants
 */
typedef enum
{
    /** Success - never used */
    GLOBUS_GSI_CERT_UTILS_ERROR_SUCCESS = 0,
    /** Failed to retrieve a subcomponent of the subject */
    GLOBUS_GSI_CERT_UTILS_ERROR_GETTING_NAME_ENTRY_OF_SUBJECT = 1,
    /** A error occured while trying to copy a X.509 subject */
    GLOBUS_GSI_CERT_UTILS_ERROR_COPYING_SUBJECT = 2,
    /** Failed to retrieve a CN subcomponent of the subject */
    GLOBUS_GSI_CERT_UTILS_ERROR_GETTING_CN_ENTRY = 3,
    /** Failed to add a CN component to a X.509 subject name */
    GLOBUS_GSI_CERT_UTILS_ERROR_ADDING_CN_TO_SUBJECT = 4,
    /** Out of memory */
    GLOBUS_GSI_CERT_UTILS_ERROR_OUT_OF_MEMORY = 5,
    /** Something unexpected happen while converting a string subject to a
     * X509_NAME structure */ 
    GLOBUS_GSI_CERT_UTILS_ERROR_UNEXPECTED_FORMAT = 6,
    /** Proxy does not comply with the expected format */
    GLOBUS_GSI_CERT_UTILS_ERROR_NON_COMPLIANT_PROXY = 7,
    /** Couldn't dtermine the certificate type */
    GLOBUS_GSI_CERT_UTILS_ERROR_DETERMINING_CERT_TYPE = 8,
    /** Last marker - never used */
    GLOBUS_GSI_CERT_UTILS_ERROR_LAST = 9
} globus_gsi_cert_utils_error_t;


/**
 * Certificate Types.
 * @ingroup globus_gsi_cert_utils_constants
 *
 * These certificate types are used to describe some properties of a
 * certificate and to specify what type of proxy should be generated in
 * the proxy core code. There are two non-proxy types of certificates
 * understood by Globus: EEC (End-Entity Certificate) and CA (Certificate
 * Authority Certificates), three proxy formats (GSI 2 "legacy" proxies, 
 * GSI 3 "Draft" proxies, and RFC 3820-compliant proxies), and four types
 * of proxy (limited, impersonation "full", restricted, and independent). The
 * latter two types are not expressible in the GSI 2 format.
 * 
 * In addition to enumerations for the concrete renderings of certificate
 * format and type combined, there are default, formats-without-types and
 * types-without-formats so that application logic which uses the proxy
 * library can request default proxy formats which are compatible with the
 * issuing certificate.
 */
typedef enum globus_gsi_cert_utils_cert_type_e
{
    /** Default proxy type */
    GLOBUS_GSI_CERT_UTILS_TYPE_DEFAULT  = 0,
    /** A end entity certificate */
    GLOBUS_GSI_CERT_UTILS_TYPE_EEC      = (1 << 0),
    /** A CA certificate */
    GLOBUS_GSI_CERT_UTILS_TYPE_CA       = (1 << 1),
    /** Legacy Proxy Format */
    GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2    = (1 << 2),
    /** X.509 Proxy Certificate Profile (draft) Proxy Format */
    GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3    = (1 << 3),
    /** X.509 Proxy Certificate Profile Compliant Proxy Format */
    GLOBUS_GSI_CERT_UTILS_TYPE_RFC      = (1 << 4),
    /** Proxy certificate formats mask */
    GLOBUS_GSI_CERT_UTILS_TYPE_FORMAT_MASK =
            (GLOBUS_GSI_CERT_UTILS_TYPE_EEC |
             GLOBUS_GSI_CERT_UTILS_TYPE_CA |
             GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2 |
             GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3 |
             GLOBUS_GSI_CERT_UTILS_TYPE_RFC),
    /** Impersonation proxy type */
    GLOBUS_GSI_CERT_UTILS_TYPE_IMPERSONATION_PROXY
                                        = (1 << 5),
    /** Limited proxy type */
    GLOBUS_GSI_CERT_UTILS_TYPE_LIMITED_PROXY
                                        = (1 << 6),
    /** Restricted proxy type */
    GLOBUS_GSI_CERT_UTILS_TYPE_RESTRICTED_PROXY
                                        = (1 << 7),
    /** Independent proxy type */
    GLOBUS_GSI_CERT_UTILS_TYPE_INDEPENDENT_PROXY
                                        = (1 << 8),
    /** Proxy types mask */
    GLOBUS_GSI_CERT_UTILS_TYPE_PROXY_MASK =
            (GLOBUS_GSI_CERT_UTILS_TYPE_IMPERSONATION_PROXY |
             GLOBUS_GSI_CERT_UTILS_TYPE_LIMITED_PROXY |
             GLOBUS_GSI_CERT_UTILS_TYPE_RESTRICTED_PROXY |
             GLOBUS_GSI_CERT_UTILS_TYPE_INDEPENDENT_PROXY),
    /**
     * A X.509 Proxy Certificate Profile (pre-RFC) compliant
     *  impersonation proxy
     */
    GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_IMPERSONATION_PROXY =
            (GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3 |
             GLOBUS_GSI_CERT_UTILS_TYPE_IMPERSONATION_PROXY),
    /**
     * A X.509 Proxy Certificate Profile (pre-RFC) compliant
     *  independent proxy
     */
    GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_INDEPENDENT_PROXY =
            (GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3 |
             GLOBUS_GSI_CERT_UTILS_TYPE_INDEPENDENT_PROXY),
    /** A X.509 Proxy Certificate Profile (pre-RFC) compliant
     *  limited proxy
     */
    GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_LIMITED_PROXY =
            (GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3 |
             GLOBUS_GSI_CERT_UTILS_TYPE_LIMITED_PROXY),
    /** A X.509 Proxy Certificate Profile (pre-RFC) compliant
     *  restricted proxy
     */
    GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_RESTRICTED_PROXY =
            (GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3 |
             GLOBUS_GSI_CERT_UTILS_TYPE_RESTRICTED_PROXY),
    /** A legacy Globus impersonation proxy */
    GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2_PROXY =
            (GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2 |
             GLOBUS_GSI_CERT_UTILS_TYPE_IMPERSONATION_PROXY),
    /** A legacy Globus limited impersonation proxy */
    GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2_LIMITED_PROXY =
            (GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2 |
             GLOBUS_GSI_CERT_UTILS_TYPE_LIMITED_PROXY),
    /** A X.509 Proxy Certificate Profile RFC compliant impersonation proxy */
    GLOBUS_GSI_CERT_UTILS_TYPE_RFC_IMPERSONATION_PROXY =
            (GLOBUS_GSI_CERT_UTILS_TYPE_RFC |
             GLOBUS_GSI_CERT_UTILS_TYPE_IMPERSONATION_PROXY),
    /** A X.509 Proxy Certificate Profile RFC compliant independent proxy */
    GLOBUS_GSI_CERT_UTILS_TYPE_RFC_INDEPENDENT_PROXY =
            (GLOBUS_GSI_CERT_UTILS_TYPE_RFC | 
             GLOBUS_GSI_CERT_UTILS_TYPE_INDEPENDENT_PROXY),
    /** A X.509 Proxy Certificate Profile RFC compliant limited proxy */
    GLOBUS_GSI_CERT_UTILS_TYPE_RFC_LIMITED_PROXY =
            (GLOBUS_GSI_CERT_UTILS_TYPE_RFC | 
             GLOBUS_GSI_CERT_UTILS_TYPE_LIMITED_PROXY),
    /** A X.509 Proxy Certificate Profile RFC compliant restricted proxy */
    GLOBUS_GSI_CERT_UTILS_TYPE_RFC_RESTRICTED_PROXY =
            (GLOBUS_GSI_CERT_UTILS_TYPE_RFC | 
             GLOBUS_GSI_CERT_UTILS_TYPE_RESTRICTED_PROXY)
} globus_gsi_cert_utils_cert_type_t;

#ifdef __cplusplus
}
#endif

#endif
