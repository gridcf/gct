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
 * @file proxycertinfo.c
 * @brief Proxy Certificate Info
 */
#endif

#include <stdio.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/x509v3.h>

#define GLOBUS_DEPRECATED(x) x

#include "proxycertinfo.h"

typedef PROXY_CERT_INFO_EXTENSION PROXYCERTINFO_OLD;

ASN1_SEQUENCE(PROXYCERTINFO) =
{
    ASN1_OPT(PROXYCERTINFO, path_length, ASN1_INTEGER),
    ASN1_SIMPLE(PROXYCERTINFO, policy, PROXYPOLICY)
    
} ASN1_SEQUENCE_END(PROXYCERTINFO)

ASN1_SEQUENCE(PROXYCERTINFO_OLD) =
{
    ASN1_SIMPLE(PROXYCERTINFO_OLD, proxyPolicy, PROXY_POLICY),
    ASN1_EXP_OPT(PROXYCERTINFO_OLD, pcPathLengthConstraint, ASN1_INTEGER, 1),
} ASN1_SEQUENCE_END(PROXYCERTINFO_OLD)

IMPLEMENT_ASN1_FUNCTIONS(PROXYCERTINFO)
IMPLEMENT_ASN1_DUP_FUNCTION(PROXYCERTINFO)

IMPLEMENT_ASN1_FUNCTIONS(PROXYCERTINFO_OLD)
IMPLEMENT_ASN1_DUP_FUNCTION(PROXYCERTINFO_OLD)

#if OPENSSL_VERSION_NUMBER < 0x10000000L
/**
 * Define the functions required for 
 * manipulating a PROXYCERTINFO and its ASN.1 form. 
 * @ingroup proxycertinfo
 * 
 * Creates an ASN1_METHOD structure, which contains
 * pointers to routines that convert any PROXYCERTINFO
 * structure to its associated ASN.1 DER encoded form
 * and vice-versa.
*
 * @return the ASN1_METHOD object
 */
ASN1_METHOD * PROXYCERTINFO_asn1_meth()
{
    static ASN1_METHOD proxycertinfo_asn1_meth =
    {
        (i2d_of_void *) i2d_PROXYCERTINFO, 
        (d2i_of_void *) d2i_PROXYCERTINFO, 
        (void * (*)(void)) PROXYCERTINFO_new, 
        (void (*)(void *)) PROXYCERTINFO_free 
    };
    return (&proxycertinfo_asn1_meth);
}
/* PROXYCERTINFO_asn1_meth() */
#endif

/**
 * @ingroup proxycertinfo
 * 
 * Compares two PROXYCERTINFO structures
 *
 * @param a pointer to the first PROXYCERTINFO structure
 * @param b pointer to the second PROXYCERTINFO structure
 *
 * @return an integer - the result of the comparison.  
 * The comparison compares each of the fields, so if any of those
 * fields are not equal then a nonzero value is returned. Equality
 * is indicated by returning a 0.
 */
int PROXYCERTINFO_cmp(
    const PROXYCERTINFO *               a,
    const PROXYCERTINFO *               b)
{
    if(ASN1_INTEGER_cmp(a->path_length, b->path_length) ||
       PROXYPOLICY_cmp(a->policy, b->policy))
    {
        return 1;
    }
    return 0;
}
/* PROXYCERTINFO_cmp() */


/**
 * @ingroup proxycertinfo
 *
 * Print the PROXYCERTINFO structure to stdout
 *
 * @param bp the BIO to print to
 * @param cert_info the PROXYCERTINFO to print
 *
 * @return 1 on success, 0 on error
 */
int PROXYCERTINFO_print(
    BIO *                               bp,
    PROXYCERTINFO *                     cert_info) 
{
    STACK_OF(CONF_VALUE) *              values = NULL;

    values = i2v_PROXYCERTINFO(PROXYCERTINFO_x509v3_ext_meth(),
                               cert_info,
                               NULL);

    X509V3_EXT_val_prn(bp, values, 0, 1);
    
    sk_CONF_VALUE_pop_free(values, X509V3_conf_free);
    return 1;
}
/* PROXYCERTINFO_print() */


/**
 * @ingroup proxycertinfo
 *
 * Print the PROXYCERTINFO structure to the
 * specified file stream
 *
 * @param fp the file stream (FILE *) to print to
 * @param cert_info the PROXYCERTINFO structure to print
 *
 * @return the number of characters printed
 */
int PROXYCERTINFO_print_fp(
    FILE *                              fp,
    PROXYCERTINFO *                     cert_info)
{
    int                                 ret;
    BIO *                               bp;

    bp = BIO_new(BIO_s_file());
    
    BIO_set_fp(bp, fp, BIO_NOCLOSE);
    ret =  PROXYCERTINFO_print(bp, cert_info);
    BIO_free(bp);

    return (ret);
}   
/* PROXYCERTINFO_print_fp() */

/**
 * @ingroup proxycertinfo
 *
 * Sets the policy on the PROXYCERTINFO
 * Since this is an optional field in the
 * ASN1 encoding, this variable can be set
 * to NULL through this function - which
 * means that when the PROXYCERTINFO is encoded
 * the policy won't be included.
 *
 * @param cert_info the PROXYCERTINFO object
 * to set the policy of
 * @param policy the PROXYPOLICY
 * to set it to
 *
 * @return 1 if success, 0 if error
 */
int PROXYCERTINFO_set_policy(
    PROXYCERTINFO *                     cert_info,
    PROXYPOLICY *                       policy)
{
    PROXYPOLICY_free(cert_info->policy);
    if(policy != NULL)
    {
        cert_info->policy = PROXYPOLICY_dup(policy);
    }
    else
    {
        cert_info->policy = NULL;
    }
    return 1;
}
/* PROXYCERTINFO_set_policy() */

/**
 * @ingroup proxycertinfo
 * 
 * Gets the policy on the PROXYCERTINFO
 * 
 * @param cert_info the PROXYCERTINFO to get the policy of
 *
 * @return the PROXYPOLICY of the PROXYCERTINFO
 */
PROXYPOLICY * PROXYCERTINFO_get_policy(
    PROXYCERTINFO *                     cert_info)
{
    if(cert_info)
    {
        return cert_info->policy;
    }
    return NULL;
}
/* PROXYCERTINFO_get_policy() */


/**
 * @ingroup proxycertinfo
 *
 * Sets the path length of the PROXYCERTINFO. The path length specifies 
 * the maximum depth of the path of the Proxy Certificates that
 * can be signed by an End Entity Certificate (EEC) or Proxy Certificate.
 *
 * Since this is an optional field in its ASN.1 coded representation,
 * it can be set to NULL through this function - which means
 * that it won't be included in the encoding.
 *
 * @param cert_info the PROXYCERTINFO to set the path length of
 * @param path_length the path length to set it to
 *        if -1 is passed in, the path length gets unset,
 *        which configures the PROXYCERTINFO
 *        to not include the path length in the DER encoding
 *
 * @return 1 on success, 0 on error
 */
int PROXYCERTINFO_set_path_length(
    PROXYCERTINFO *                     cert_info,
    long                                path_length)
{
    if(cert_info != NULL) 
    {
        if(path_length != -1)
        {
            if(cert_info->path_length == NULL)
            {
                cert_info->path_length = ASN1_INTEGER_new();
            }
            return ASN1_INTEGER_set(cert_info->path_length, path_length);
        }
        else
        {
            if(cert_info->path_length != NULL)
            {
                ASN1_INTEGER_free(cert_info->path_length);
                cert_info->path_length = NULL;
            }
            return 1;
        }
    }
    return 0;
}
/* PROXYCERTINFO_set_path_length() */


/**
 * @ingroup proxycertinfo
 * 
 * Gets the path length of the PROXYCERTINFO.
 *
 * @see PROXYCERTINFO_set_path_length
 *
 * @param cert_info the PROXYCERTINFO to get the path length from
 * 
 * @return the path length of the PROXYCERTINFO, or -1 if not set
 */
long PROXYCERTINFO_get_path_length(
    PROXYCERTINFO *                     cert_info)
{
    if(cert_info && cert_info->path_length)
    {
        return ASN1_INTEGER_get(cert_info->path_length);
    }
    else
    {
        return -1;
    }
}
/* PROXYCERTINFO_get_path_length() */

    
X509V3_EXT_METHOD * PROXYCERTINFO_x509v3_ext_meth()
{
    static X509V3_EXT_METHOD proxycertinfo_x509v3_ext_meth =
    {
        -1,
        X509V3_EXT_MULTILINE,
        NULL,
        (X509V3_EXT_NEW) PROXYCERTINFO_new,
        (X509V3_EXT_FREE) PROXYCERTINFO_free,
        (X509V3_EXT_D2I) d2i_PROXYCERTINFO,
        (X509V3_EXT_I2D) i2d_PROXYCERTINFO,
        NULL, NULL,
        (X509V3_EXT_I2V) i2v_PROXYCERTINFO,
        NULL,
        NULL, NULL,
        NULL
    };
    return (&proxycertinfo_x509v3_ext_meth);
}

X509V3_EXT_METHOD * PROXYCERTINFO_OLD_x509v3_ext_meth()
{
    static X509V3_EXT_METHOD proxycertinfo_x509v3_ext_meth =
    {
        -1,
        X509V3_EXT_MULTILINE,
        NULL,
        (X509V3_EXT_NEW) PROXYCERTINFO_new,
        (X509V3_EXT_FREE) PROXYCERTINFO_free,
        (X509V3_EXT_D2I) d2i_PROXYCERTINFO_OLD,
        (X509V3_EXT_I2D) i2d_PROXYCERTINFO_OLD,
        NULL, NULL,
        (X509V3_EXT_I2V) i2v_PROXYCERTINFO,
        NULL,
        NULL, NULL,
        NULL
    };
    return (&proxycertinfo_x509v3_ext_meth);    
}

STACK_OF(CONF_VALUE) * i2v_PROXYCERTINFO(
    struct v3_ext_method *              method,
    PROXYCERTINFO *                     ext,
    STACK_OF(CONF_VALUE) *              extlist)
{
    int                                 len = 128;
    char                                tmp_string[128];
    
    if(!ext)
    {
        extlist = NULL;
        return extlist;
    }

    if(extlist == NULL)
    {
        extlist = sk_CONF_VALUE_new_null();
        if(extlist == NULL)
        { 
            return NULL;
        }
    }
    
    if(PROXYCERTINFO_get_path_length(ext) > -1)
    {
        memset(tmp_string, 0, len);
        BIO_snprintf(tmp_string, len, " %lu (0x%lx)",
                     PROXYCERTINFO_get_path_length(ext),
                     PROXYCERTINFO_get_path_length(ext));
        X509V3_add_value("Path Length", tmp_string, &extlist);
    }

    if(PROXYCERTINFO_get_policy(ext))
    {
        i2v_PROXYPOLICY(PROXYPOLICY_x509v3_ext_meth(),
                             PROXYCERTINFO_get_policy(ext),
                             extlist);
    }


    return extlist;
}
