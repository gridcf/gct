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

#ifndef GSSAPI_OPENSSL_H
#define GSSAPI_OPENSSL_H

/**
 * @file gssapi_openssl.h
 * @brief GSS API OpenSSL
 * @author Sam Lang, Sam Meder
 */

#include "globus_config.h"
#include "globus_common.h"

#include "gssapi.h"
#include "globus_gsi_gss_constants.h"

#include "globus_gsi_callback.h"
#include "globus_gsi_proxy.h"
#include "globus_gsi_credential.h"

#include <stdio.h>
#include "openssl/ssl.h"
#include "openssl/err.h"
#include "openssl/bio.h"
#include "openssl/pem.h"
#include "openssl/x509.h"
#include "openssl/x509v3.h"
#include "openssl/stack.h"

#define GLOBUS_I_GSI_GSSAPI_IMPL_VERSION            1

#define GSS_I_CTX_INITIALIZED                       1
#define GSS_I_DISALLOW_ENCRYPTION                   2
#define GSS_I_PROTECTION_FAIL_ON_CONTEXT_EXPIRATION 4
#define GSS_I_APPLICATION_WILL_HANDLE_EXTENSIONS    8

#define GSS_C_QOP_GLOBUS_GSSAPI_OPENSSL_BIG 1

/*
 * we need to distinguish between a token
 * created by us using get_mic vs one using
 * the SSL application data
 * We use this in wrap and unwrap
 * Future versions of SSL may use this
 *
 * Our wrapped buffer (integrity only) has
 *
 *  byte  type[1]          = SSL3_RT_GSSAPI_OPENSSL
 *  byte  version_major[1] = 0x03
 *  byte  version_minor[1] = 0
 *  byte  mic_length[2]    = 2 byte length of following mic 
 * 
 *  byte  mic_seq[8]           = 8 byte sequence number
 *  byte  mic_data_length[4]   = 4 byte length of data 
 *  byte  hash[*]          = the hash of variable length
 *
 *  byte  data[*]          = the data being wrapped. 
 */

#define SSL3_RT_GSSAPI_OPENSSL                   26

/* These conversions macros are taken from SSL */

#define L2N(LONG_VAL, CHAR_ARRAY) \
   {  \
       unsigned char *                  _char_array_ = CHAR_ARRAY; \
       *(_char_array_++) = (unsigned char) (((LONG_VAL) >> 24) & 0xff); \
       *(_char_array_++) = (unsigned char) (((LONG_VAL) >> 16) & 0xff); \
       *(_char_array_++) = (unsigned char) (((LONG_VAL) >> 8)  & 0xff); \
       *(_char_array_++) = (unsigned char) (((LONG_VAL))       & 0xff); \
   }

#define N2L(CHAR_ARRAY, LONG_VAL) \
   { \
       const unsigned char *                _char_array_ = CHAR_ARRAY; \
       (LONG_VAL)  = ((*(_char_array_++)) << 24) & 0xff000000; \
       (LONG_VAL) |= ((*(_char_array_++)) << 16) & 0xff0000; \
       (LONG_VAL) |= ((*(_char_array_++)) << 8) & 0xff00; \
       (LONG_VAL) |= ((*(_char_array_++)) & 0xff); \
   }

#define N2S(CHAR_ARRAY, SHORT) \
   { \
       char *                           _char_array_ = CHAR_ARRAY; \
       (SHORT)  = ((unsigned int) (*(_char_array_++))) << 8; \
       (SHORT) |= ((unsigned int) (*(_char_array_++))); \
   }

#define S2N(SHORT, CHAR_ARRAY) \
   { \
       char *                           _char_array_ = CHAR_ARRAY; \
       *(_char_array_++) = (unsigned char) (((SHORT) >> 8) & 0xff); \
       *(_char_array_++) = (unsigned char) ((SHORT) & 0xff); \
   } 

#define U642N(U64VAL, CHAR_ARRAY) \
    { \
        unsigned char *             _char_array_ = CHAR_ARRAY; \
        *(_char_array_++) = (unsigned char) (((U64VAL) >> 56) & 0xff); \
        *(_char_array_++) = (unsigned char) (((U64VAL) >> 48) & 0xff); \
        *(_char_array_++) = (unsigned char) (((U64VAL) >> 40) & 0xff); \
        *(_char_array_++) = (unsigned char) (((U64VAL) >> 32) & 0xff); \
        *(_char_array_++) = (unsigned char) (((U64VAL) >> 24) & 0xff); \
        *(_char_array_++) = (unsigned char) (((U64VAL) >> 16) & 0xff); \
        *(_char_array_++) = (unsigned char) (((U64VAL) >>  8) & 0xff); \
        *(_char_array_++) = (unsigned char) (((U64VAL)      ) & 0xff); \
    }

#define N2U64(CHAR_ARRAY, U64VAL) \
    { \
        const unsigned char *       _char_array_ = CHAR_ARRAY; \
        uint64_t                    _u64val_ = 0; \
        _u64val_ = (((uint64_t)(*(_char_array_++))) << 56) & 0xff00000000000000; \
        _u64val_ = (((uint64_t)(*(_char_array_++))) << 48) & 0xff000000000000; \
        _u64val_ = (((uint64_t)(*(_char_array_++))) << 40) & 0xff0000000000; \
        _u64val_ = (((uint64_t)(*(_char_array_++))) << 32) & 0xff00000000; \
        _u64val_ = (((uint64_t)(*(_char_array_++))) << 24) & 0xff000000; \
        _u64val_ = (((uint64_t)(*(_char_array_++))) << 16) & 0xff0000; \
        _u64val_ = (((uint64_t)(*(_char_array_++))) <<  8) & 0xff00; \
        _u64val_ = (((uint64_t)(*(_char_array_++)))      ) & 0xff; \
        (U64VAL) = _u64val_; \
    }
/* Compare OIDs */

#define g_OID_equal(o1, o2) \
        (((o1) == (o2)) || \
         ((o1) && (o2) && \
         ((o1)->length == (o2)->length) && \
         (memcmp((o1)->elements,(o2)->elements,(int) (o1)->length) == 0)))

typedef struct gss_name_desc_struct {
    /* gss_buffer_desc  name_buffer ; */
    gss_OID                             name_oid;

    X509_NAME *                         x509n;
    char *                              x509n_oneline;
    GENERAL_NAMES *                     subjectAltNames;
    char *                              user_name;
    char *                              service_name;
    char *                              host_name;
    char *                              ip_address;
    char *                              ip_name;
} gss_name_desc;


typedef struct gss_cred_id_desc_struct {
    globus_gsi_cred_handle_t            cred_handle;
    gss_name_desc *                     globusid;
    gss_cred_usage_t                    cred_usage;
    SSL_CTX *                           ssl_context;
    gss_OID                             mech;
} gss_cred_id_desc;

typedef struct gss_ctx_id_desc_struct{
    globus_mutex_t                      mutex;
    globus_gsi_callback_data_t          callback_data;
    gss_cred_id_desc *                  peer_cred_handle;
    gss_cred_id_desc *                  cred_handle;
    gss_cred_id_desc *                  deleg_cred_handle;
    globus_gsi_proxy_handle_t           proxy_handle;
    OM_uint32                           ret_flags;
    OM_uint32                           req_flags;
    OM_uint32                           ctx_flags;
    int                                 cred_obtained;
    gss_OID                             mech;
#if OPENSSL_VERSION_NUMBER >= 0x10000100L
    /** For GCM ciphers, sequence number of next read MAC token */
    uint64_t                            mac_read_sequence;
    /** For GCM ciphers, sequence number of next write MAC token */
    uint64_t                            mac_write_sequence;
    /** For GCM ciphers, key for MAC token generation/validation */
    unsigned char *                     mac_key;
    /**
     * For GCM ciphers, fixed part of the IV for MAC token
     * generation/validation
     */
    unsigned char *                     mac_iv_fixed;
#endif
    SSL *                               gss_ssl; 
    BIO *                               gss_rbio;
    BIO *                               gss_wbio;
    BIO *                               gss_sslbio;
    gss_con_st_t                        gss_state;
    int                                 locally_initiated;
    gss_delegation_state_t              delegation_state;
    gss_OID_set                         extension_oids;
    gss_cred_id_t                      *sni_credentials;
    size_t                              sni_credentials_count;
    char                               *sni_servername;
    unsigned char                      *alpn;
    size_t                              alpn_length;
} gss_ctx_id_desc;

extern
const gss_OID_desc * const              gss_mech_globus_gssapi_openssl;

extern
const gss_OID_desc * const              gss_mech_globus_gssapi_openssl_micv2;

extern
const gss_OID_desc * const              gss_proxycertinfo_extension;

extern
gss_OID_desc *                          gss_nt_host_ip;

extern
gss_OID_desc *                          gss_nt_x509;

extern
const gss_OID_desc * const gss_ext_server_name_oid;

extern
const gss_OID_desc * const gss_ext_alpn_oid;

extern
const gss_OID_desc * const gss_ext_tls_version_oid;

extern
const gss_OID_desc * const gss_ext_tls_cipher_oid;

extern
globus_bool_t                           globus_i_backward_compatible_mic;
extern
globus_bool_t                           globus_i_accept_backward_compatible_mic;

#define GLOBUS_GSS_C_NT_HOST_IP         gss_nt_host_ip
#define GLOBUS_GSS_C_NT_X509            gss_nt_x509

extern
globus_thread_once_t                    once_control;

void
globus_l_gsi_gssapi_activate_once(void);

OM_uint32
globus_i_gss_get_hash(
    OM_uint32                          *minor_status,
    const gss_ctx_id_t                  context_handle,
    const EVP_MD **                     hash,
    const EVP_CIPHER **                 cipher);


OM_uint32
globus_i_gssapi_gsi_gmac(
    OM_uint32 *                         minor_status,
    const EVP_CIPHER *                  evp_cipher,
    const unsigned char *               iv,
    const unsigned char *               key,
    const gss_buffer_desc              *message_buffer,
    unsigned char                       tag[static 16]);

#endif /* GSSAPI_OPENSSL_H */
