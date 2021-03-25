/*
 * Copyright 1999-2016 University of Chicago
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

#include "gssapi_openssl.h"
#include "globus_i_gsi_gss_utils.h"

/**
 * @brief Find the hash and cipher functions used by a context
 */
OM_uint32
globus_i_gss_get_hash(
    OM_uint32                          *minor_status,
    const gss_ctx_id_t                  context_handle,
    const EVP_MD **                     hash,
    const EVP_CIPHER **                 cipher)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    gss_ctx_id_desc *                   context = context_handle;
    int                                 hash_nid = NID_undef;
    int                                 cipher_nid = NID_undef;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    assert (minor_status != NULL);
    assert (hash != NULL);
    assert (cipher != NULL);

    *minor_status = GLOBUS_SUCCESS;
    *hash = NULL;
    *cipher = NULL;

    #if OPENSSL_VERSION_NUMBER < 0x10000000L
    hash_nid = EVP_MD_type(context->gss_ssl->read_hash);
    #elif OPENSSL_VERSION_NUMBER < 0x10100000L
    if (context->gss_ssl->read_hash->digest != NULL)
    {
        hash_nid = EVP_MD_CTX_type(context->gss_ssl->read_hash);
    }
    if (context->gss_ssl->enc_read_ctx != NULL)
    {
        cipher_nid = EVP_CIPHER_CTX_nid(context->gss_ssl->enc_read_ctx);
    }
        #ifdef NID_rc4_hmac_md5
        /* Some versions of OpenSSL use special ciphers which
         * combine HMAC with the encryption operation:
         * for these ssl->write_hash is NULL.
         * If the cipher context is one of these set the
         * hash manually.
         */
        if (*hash == NULL)
        {
            EVP_CIPHER_CTX *cctx = context->gss_ssl->enc_read_ctx;
            switch(EVP_CIPHER_CTX_nid(cctx))
            {
                case NID_rc4_hmac_md5:
                    *hash = EVP_md5();
                    break;
                case NID_aes_128_cbc_hmac_sha1:
                case NID_aes_256_cbc_hmac_sha1:
                    *hash = EVP_sha1();
                    break;
            }
        }
        #endif
    #else
    {
        const SSL_CIPHER *              ssl_cipher = NULL;

        ssl_cipher = SSL_get_current_cipher(context->gss_ssl);
        hash_nid = SSL_CIPHER_get_digest_nid(ssl_cipher);
        if (hash_nid == NID_undef && SSL_CIPHER_is_aead(ssl_cipher))
        {
            cipher_nid = SSL_CIPHER_get_cipher_nid(ssl_cipher);
        }
    }
    #endif

    if (hash_nid != NID_undef)
    {
        *hash = EVP_get_digestbynid(hash_nid);
    }

    if (*hash == NULL && cipher_nid != NID_undef)
    {
        *cipher = EVP_get_cipherbynid(cipher_nid);
    }

    if (*hash == NULL && *cipher == NULL)
    {
        /* Shouldn't happen: some error occurred */
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;
    }

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;

    return major_status;
}
/* globus_i_gss_get_hash() */
