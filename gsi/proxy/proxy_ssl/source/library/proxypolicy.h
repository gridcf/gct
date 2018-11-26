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


#ifndef HEADER_PROXYPOLICY_H
#define HEADER_PROXYPOLICY_H

/**
 * @file proxypolicy.h
 * @brief Proxy Policy
 * @author Sam Meder
 * @author Sam Lang
 */
/**
 * @defgroup proxypolicy ProxyPolicy
 * @ingroup globus_gsi_proxy_ssl_api
 * @brief ProxyPolicy
 *
 * The proxypolicy set of data structures
 * and functions provides an interface to generating
 * a PROXYPOLICY structure which is maintained as
 * a field in the PROXYCERTINFO structure,
 * and ultimately gets written to a DER encoded string.
 *
 * Further Information about proxy policies
 * is available in the <a href="http://www.ietf.org/rfc/rfc3820.txt">X.509 Proxy Certificate Profile</a> document.
 */

#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANY_LANGUAGE_OID         "1.3.6.1.5.5.7.21.0"
#define ANY_LANGUAGE_SN          "ANY_LANGUAGE"
#define ANY_LANGUAGE_LN          "Any Language"

#define IMPERSONATION_PROXY_OID         "1.3.6.1.5.5.7.21.1"
#define IMPERSONATION_PROXY_SN          "IMPERSONATION_PROXY"
#define IMPERSONATION_PROXY_LN          "GSI impersonation proxy"

#define INDEPENDENT_PROXY_OID           "1.3.6.1.5.5.7.21.2"
#define INDEPENDENT_PROXY_SN            "INDEPENDENT_PROXY"
#define INDEPENDENT_PROXY_LN            "GSI independent proxy"

#define LIMITED_PROXY_OID               "1.3.6.1.4.1.3536.1.1.1.9"
#define LIMITED_PROXY_SN                "LIMITED_PROXY"
#define LIMITED_PROXY_LN                "GSI limited proxy"

/* Used for error handling */
#define ASN1_F_PROXYPOLICY_NEW          450
#define ASN1_F_D2I_PROXYPOLICY          451

/* data structures */

/**
 * @ingroup proxypolicy
 *
 * @note NOTE: The API provides functions to manipulate
 * the fields of a PROXYPOLICY.  Accessing the fields
 * directly will not work.
 *
 * This typedef maintains information about the policies
 * that have been placed on a proxy certificate
 *
 * @param policy_language defines which policy language
 * is to be used to define the policies
 * @param policy the policy that determines the policies
 * on a certificate
 */
struct PROXYPOLICY_st
{
    ASN1_OBJECT *                       policy_language;
    ASN1_OCTET_STRING *                 policy;
};

typedef struct PROXYPOLICY_st PROXYPOLICY;

#ifdef DECLARE_STACK_OF
DECLARE_STACK_OF(PROXYPOLICY)
#endif
DECLARE_ASN1_FUNCTIONS(PROXYPOLICY)

/* functions */

#if OPENSSL_VERSION_NUMBER < 0x10000000L
ASN1_METHOD * PROXYPOLICY_asn1_meth();
#endif

PROXYPOLICY * PROXYPOLICY_dup(
    PROXYPOLICY *                       policy);

int PROXYPOLICY_cmp(
    const PROXYPOLICY *                 a,
    const PROXYPOLICY *                 b);

int PROXYPOLICY_print(
    BIO *                               bp,
    PROXYPOLICY *                       policy);

int PROXYPOLICY_print_fp(
    FILE *                              fp,
    PROXYPOLICY *                       policy);

int PROXYPOLICY_set_policy_language(
    PROXYPOLICY *                       policy,
    ASN1_OBJECT *                       policy_language);

ASN1_OBJECT * PROXYPOLICY_get_policy_language(
    PROXYPOLICY *                       policy);

int PROXYPOLICY_set_policy(
    PROXYPOLICY *                       proxypolicy,
    unsigned char *                     policy,
    int                                 length);

unsigned char * PROXYPOLICY_get_policy(
    PROXYPOLICY *                       policy,
    int *                               length);

X509V3_EXT_METHOD * PROXYPOLICY_x509v3_ext_meth();

STACK_OF(CONF_VALUE) * i2v_PROXYPOLICY(
    struct v3_ext_method *              method,
    PROXYPOLICY *                       ext,
    STACK_OF(CONF_VALUE) *              extlist);

#ifdef __cplusplus
}
#endif

#endif /* HEADER_PROXYPOLICY_H */
