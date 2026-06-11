/*
 * Copyright (c) 2004-2006 Roumen Petrov.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Modified from X.509 certificates support for OpenSSH by
 * Roumen Petrov (http://roumenpetrov.info/openssh/).
 */

#include "myproxy_common.h"
#include "myproxy_ocsp_aia.h"

#if defined(HAVE_OCSP)
#include <openssl/bio.h>
#include <openssl/conf.h>
#include <openssl/ocsp.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#if OPENSSL_VERSION_NUMBER < 0x40000000L
static AUTHORITY_INFO_ACCESS *
my_aia_get(X509_EXTENSION *ext)
#else
static AUTHORITY_INFO_ACCESS *
my_aia_get(const X509_EXTENSION *ext)
#endif
{
    const X509V3_EXT_METHOD *method = NULL;
    void *ext_str = NULL;
    const unsigned char *p;
    int len;

    if (ext == NULL) {
        verror_put_string("my_aia_get: ext is NULL");
        return(NULL);
    }

    method = X509V3_EXT_get(ext);
    if (method == NULL) {
        myproxy_debug("my_aia_get: cannot get method");
        return(NULL);
    }

    p = ASN1_STRING_get0_data(X509_EXTENSION_get_data(ext));
    len = ASN1_STRING_length(X509_EXTENSION_get_data(ext));
    if (method->it) {
        ext_str = ASN1_item_d2i(NULL, &p, len, ASN1_ITEM_ptr(method->it));
    } else {
        ext_str = method->d2i(NULL, &p, len);
    }
    if (ext_str == NULL) {
        myproxy_debug("my_aia_get: null ext_str!");
        return(NULL);
    }

    return((AUTHORITY_INFO_ACCESS*)ext_str);
}

#if OPENSSL_VERSION_NUMBER < 0x40000000L
static void
my_aia_free(X509_EXTENSION *ext, AUTHORITY_INFO_ACCESS* aia)
#else
static void
my_aia_free(const X509_EXTENSION *ext, AUTHORITY_INFO_ACCESS* aia)
#endif
{
    const X509V3_EXT_METHOD *method = NULL;

    if (ext == NULL) {
        verror_put_string("my_aia_free: ext is NULL");
        return;
    }

    method = X509V3_EXT_get(ext);
    if (method == NULL) return;

    if (method->it) {
        ASN1_item_free((void*)aia, ASN1_ITEM_ptr(method->it));
    } else {
        method->ext_free(aia);
    }
}

char *
myproxy_get_aia_ocsp_uri(X509 *cert)
{
    int loc = -1;
    char *uri = NULL;

    if (cert == NULL) return(0);

    for (loc = X509_get_ext_by_NID(cert, NID_info_access, loc);
         loc >= 0;
         loc = X509_get_ext_by_NID(cert, NID_info_access, loc)) {

#if OPENSSL_VERSION_NUMBER < 0x40000000L
        X509_EXTENSION  *xe;
#else
        const X509_EXTENSION  *xe;
#endif
        AUTHORITY_INFO_ACCESS   *aia;
        int k;

        xe = X509_get_ext(cert, loc);
        if (xe == NULL) {
            myproxy_debug("get_aia_ocsp_uri: cannot get x509 extension");
            continue;
        }

        aia = my_aia_get(xe);
        if (aia == NULL) continue;

        for (k = 0; k < sk_ACCESS_DESCRIPTION_num(aia); k++) {
            ACCESS_DESCRIPTION *ad = sk_ACCESS_DESCRIPTION_value(aia, k);
            GENERAL_NAME *gn;
            ASN1_IA5STRING *asn1_uri;

            if (OBJ_obj2nid(ad->method) != NID_ad_OCSP) continue;

            gn = ad->location;
            if (gn->type != GEN_URI) continue;

            asn1_uri = gn->d.uniformResourceIdentifier;
            uri = strdup((const char*)ASN1_STRING_get0_data(asn1_uri));
            break;
        }

        my_aia_free(xe, aia);

        if (uri) break;
    }

    return uri;
}
#endif
