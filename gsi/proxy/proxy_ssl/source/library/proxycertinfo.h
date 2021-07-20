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

#ifndef HEADER_PROXYCERTINFO_H
#define HEADER_PROXYCERTINFO_H

/**
 * @file proxycertinfo.h
 * @brief Proxy Certificate Info
 * @author Sam Meder
 * @author Sam Lang
 */
#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus GSI Proxy SSL API
 * @copydoc globus_gsi_proxy_ssl_api
 */
#endif

/**
 * @defgroup globus_gsi_proxy_ssl_api Globus GSI Proxy SSL API
 * @brief Globus GSI Proxy SSL API
 *
 * The globus_gsi_proxy_ssl library provides the ability
 * to create a PROXYCERTINFO extension for inclusion in
 * an X.509 certificate.  The current specification for the
 * extension is described in
 * <a href="http://www.ietf.org/rfc/rfc3820.txt">RFC 3820</a>.
 * 
 * The library conforms to the ASN.1 implementation in
 * the OpenSSL library, and provides
 * an interface to convert from a DER encoded PROXYCERTINFO
 * to its internal structure and vice-versa.
 *
 * @section proxycertinfo_section ProxyCertInfo
 * @copydoc proxycertinfo
 * For more information, see the documentation in 
 * @link proxycertinfo ProxyCertInfo @endlink
 * @section proxypolicy_section ProxyPolicy
 * @copydoc proxypolicy
 * For more information, see the documentation in 
 * @link proxypolicy ProxyPolicy @endlink
 */

#include "proxypolicy.h"
#include <openssl/asn1.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup proxycertinfo ProxyCertInfo
 * @ingroup globus_gsi_proxy_ssl_api
 * @brief ProxyCertInfo
 * 
 * The proxycertinfo.h file defines a method of
 * maintaining information about proxy certificates.
 */

#define PROXYCERTINFO_OLD_OID           "1.3.6.1.4.1.3536.1.222"
#define PROXYCERTINFO_OID               "1.3.6.1.5.5.7.1.14"
#define PROXYCERTINFO_SN                "PROXYCERTINFO"
#define PROXYCERTINFO_LN                "Proxy Certificate Info Extension"
#define PROXYCERTINFO_OLD_SN            "OLD_PROXYCERTINFO"
#define PROXYCERTINFO_OLD_LN                "Proxy Certificate Info Extension (old OID)"

/*
 * Used for error checking
 */
#define ASN1_F_PROXYCERTINFO_NEW                         430
#define ASN1_F_D2I_PROXYCERTINFO                         431

#ifndef GLOBUS_DEPRECATED
#define GLOBUS_DEPRECATED_IN_PROXYCERTINFO_H
#if __GNUC__
#   define GLOBUS_DEPRECATED(func) func __attribute__((deprecated))
#elif defined(_MSC_VER)
#   define GLOBUS_DEPRECATED(func)  __declspec(deprecated) func
#else
#   define GLOBUS_DEPRECATED(func) func
#endif
#endif

/*
 * The PROXYCERTINFO functions are deprecated, as OpenSSL has provided
 * its own data structure since 0.9.8.
 */
/* data structures */

/**
 * @ingroup proxycertinfo
 *
 * This typedef maintains information about a proxy
 * certificate.
 *
 * @note NOTE: The API provides functions to manipulate
 * the fields of a PROXYCERTINFO.  Accessing the fields
 * directly is not a good idea.
 *
 * 
 * @param path_length an optional field in the ANS.1 DER encoding, 
 * it specifies the maximum depth of the path of Proxy Certificates 
 * that can be signed by this End Entity Certificate or Proxy Certificate.
 * @param policy a non-optional field in the ANS.1 DER encoding,
 * specifies policies on the use of this certificate.
 */
struct PROXYCERTINFO_st
{
    ASN1_INTEGER *                      path_length;       /* [ OPTIONAL ] */
    PROXYPOLICY *                       policy;
};

typedef struct PROXYCERTINFO_st PROXYCERTINFO;

#ifdef DECLARE_STACK_OF
DECLARE_STACK_OF(PROXYCERTINFO)
#endif

DECLARE_ASN1_FUNCTIONS(PROXYCERTINFO)

/* macros */

#define d2i_PROXYCERTINFO_bio(bp, pci) \
    (PROXYCERTINFO *) ASN1_d2i_bio((char *(*)()) PROXYCERTINFO_new, \
    (char *(*)()) d2i_PROXYCERTINFO, \
    (bp), (unsigned char **) pci)

#define i2d_PROXYCERTINFO_bio(bp, pci) \
                ASN1_i2d_bio(i2d_PROXYCERTINFO, bp, \
		(unsigned char *)pci)

/* functions */
    
#if OPENSSL_VERSION_NUMBER < 0x10000000L
ASN1_METHOD * PROXYCERTINFO_asn1_meth();
#endif

GLOBUS_DEPRECATED(
PROXYCERTINFO * PROXYCERTINFO_dup(
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    const
#endif
    PROXYCERTINFO *                     cert_info));

GLOBUS_DEPRECATED(int PROXYCERTINFO_cmp(
    const PROXYCERTINFO *               a,
    const PROXYCERTINFO *               b));

GLOBUS_DEPRECATED(int PROXYCERTINFO_print(
    BIO *                               bp,
    PROXYCERTINFO *                     cert_info));

GLOBUS_DEPRECATED(int PROXYCERTINFO_print_fp(
    FILE *                              fp,
    PROXYCERTINFO *                     cert_info));

GLOBUS_DEPRECATED(int PROXYCERTINFO_set_policy(
    PROXYCERTINFO *                     cert_info,
    PROXYPOLICY *                       policy));

GLOBUS_DEPRECATED(PROXYPOLICY * PROXYCERTINFO_get_policy(
    PROXYCERTINFO *                     cert_info));

GLOBUS_DEPRECATED(int PROXYCERTINFO_set_path_length(
    PROXYCERTINFO *                     cert_info,
    long                                path_length));

GLOBUS_DEPRECATED(long PROXYCERTINFO_get_path_length(
    PROXYCERTINFO *                     cert_info));

GLOBUS_DEPRECATED(X509V3_EXT_METHOD * PROXYCERTINFO_x509v3_ext_meth());

X509V3_EXT_METHOD * PROXYCERTINFO_OLD_x509v3_ext_meth();

GLOBUS_DEPRECATED(STACK_OF(CONF_VALUE) * i2v_PROXYCERTINFO(
    struct v3_ext_method *              method,
    PROXYCERTINFO *                     ext,
    STACK_OF(CONF_VALUE) *              extlist));

#ifdef __cplusplus
}
#endif

#ifdef GLOBUS_DEPRECATED_IN_PROXYCERTINFO_H
#   ifdef GLOBUS_DEPRECATED
#       undef GLOBUS_DEPRECATED
#   endif
#   undef GLOBUS_DEPRECATED_IN_PROXYCERTINFO_H
#endif

#endif /* HEADER_PROXYCERTINFO_H */
