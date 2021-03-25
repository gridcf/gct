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
 * @file get_mic.c
 * @author Sam Lang, Sam Meder
 */

/* borrowed from OpenSSL's s3_enc.c
 */
static unsigned char ssl3_pad_1[48]={
	0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,
	0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,
	0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,
	0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,
	0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,
	0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36 };

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */


#include "gssapi.h"
#include "globus_i_gsi_gss_utils.h"
#include "gssapi_openssl.h"

/**
 * @brief Get MIC
 * @ingroup globus_gsi_gssapi
 * Calculates a cryptographic MIC (message integrity check)
 * over an application message, and returns that MIC in the token.
 * The token and message can then be passed to the peer application
 * which calls @ref gss_verify_mic to verify the MIC.
 *
 * @param minor_status
 * @param context_handle
 * @param qop_req
 * @param message_buffer
 * @param message_token
 *
 * @return
 */
OM_uint32 
GSS_CALLCONV gss_get_mic(
    OM_uint32 *                         minor_status,
    const gss_ctx_id_t                  context_handle,
    gss_qop_t                           qop_req,
    const gss_buffer_t                  message_buffer,
    gss_buffer_t                        message_token)
{
    /* 
     * We can't use the SSL mac methods directly,
     * partly because they only allow a length of
     * 64K, and we want to use larger blocks. 
     * We will add the seq number and 32 bit length 
     * to the mic, and send them as well. 
     * this will allow us to check for out of
     * seq records. 
     * 
     * These have 8 byte sequence number, 4 byte length, md. 
     */
 
    gss_ctx_id_desc *                   context = context_handle; 
    unsigned char *                     mac_sec;
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    unsigned char *                     seq;
    int                                 index;
#endif
    unsigned char *                     token_value;
    EVP_MD_CTX *                        md_ctx = NULL;
    const EVP_MD *                      hash = NULL;
    const EVP_CIPHER *                  evp_cipher = NULL;
    unsigned int                        md_size;
    int                                 npad;
    unsigned char *                     message_digest;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    globus_result_t                     local_result;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;
    
    *minor_status = (OM_uint32) GLOBUS_SUCCESS;

    if (context_handle == GSS_C_NO_CONTEXT)
    {
        major_status = GSS_S_NO_CONTEXT;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Invalid context_handle parameter passed to function: %s"),
             __func__));
        goto exit;
    }

    /* lock the context mutex */    
    globus_mutex_lock(&context->mutex);
    
    if(context->ctx_flags & GSS_I_PROTECTION_FAIL_ON_CONTEXT_EXPIRATION)
    {
        time_t                          lifetime;
        local_result = globus_gsi_cred_get_lifetime(
            context->cred_handle->cred_handle,
            &lifetime);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
            major_status = GSS_S_FAILURE;
            goto unlock_mutex;
        }

        if(lifetime <= 0)
        {
            major_status = GSS_S_CONTEXT_EXPIRED;
            GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL,
                (_GGSL("The credential has expired")));
            goto unlock_mutex;
        }
    }

    major_status = globus_i_gss_get_hash(
            minor_status,
            context_handle,
            &hash,
            &evp_cipher);
    
    if (major_status!= GSS_S_COMPLETE)
    {
        goto unlock_mutex;
    }
    
    if (hash != NULL)
    {
        #if OPENSSL_VERSION_NUMBER < 0x10100000L
        if (g_OID_equal(context->mech, gss_mech_globus_gssapi_openssl))
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "get_mic: OLD MIC\n"));
            mac_sec = context->gss_ssl->s3->write_mac_secret;
            seq = context->gss_ssl->s3->write_sequence;
        }
        else
        #endif
        {
            #if OPENSSL_VERSION_NUMBER >= 0x10000100L
                mac_sec = context->mac_key;
                GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                    2, (globus_i_gsi_gssapi_debug_fstream,
                        "get_mic: MICv2\n"));
            #else
                GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                major_status = GSS_S_FAILURE;
                goto unlock_mutex;
            #endif

        }
        md_size = EVP_MD_size(hash);
        message_token->value = (char *) malloc(GSS_SSL_MESSAGE_DIGEST_PADDING 
                                               + md_size);

        if (message_token->value == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto unlock_mutex;
        }

        message_token->length = GSS_SSL_MESSAGE_DIGEST_PADDING + md_size;
        token_value = message_token->value;
        
        #if OPENSSL_VERSION_NUMBER < 0x10100000L
        if (g_OID_equal(context->mech, gss_mech_globus_gssapi_openssl))
        {
            for (index = 0; index < GSS_SSL3_WRITE_SEQUENCE_SIZE; ++index)
            {
                *(token_value++) = seq[index];
            }

            for (index = (GSS_SSL3_WRITE_SEQUENCE_SIZE - 1); index >= 0; --index)
            {
                if (++seq[index]) break;
            }
        }
        else
        #endif
        {
            #if OPENSSL_VERSION_NUMBER >= 0x10000100L
                U642N(context_handle->mac_write_sequence, token_value);
                token_value += sizeof(context_handle->mac_write_sequence);
                context_handle->mac_write_sequence++;
            #else
            abort();
            #endif
        }

        L2N(message_buffer->length, token_value);
        token_value += 4;
        message_digest = token_value;

        npad = (48 / md_size) * md_size;
        
        md_ctx = EVP_MD_CTX_create();
        EVP_DigestInit(md_ctx, (EVP_MD *) hash);
        EVP_DigestUpdate(md_ctx, mac_sec, md_size);
        EVP_DigestUpdate(md_ctx, ssl3_pad_1, npad);
        EVP_DigestUpdate(md_ctx, message_token->value,
                         GSS_SSL_MESSAGE_DIGEST_PADDING);
        EVP_DigestUpdate(md_ctx, message_buffer->value,
                         message_buffer->length);
        EVP_DigestFinal(md_ctx, message_digest, NULL);
        EVP_MD_CTX_destroy(md_ctx);
        
    }
#ifdef EVP_CIPH_GCM_MODE
    else if (evp_cipher != NULL
        && EVP_CIPHER_mode(evp_cipher) == EVP_CIPH_GCM_MODE)
    {
        unsigned char                  *tag = NULL;
        size_t                          iv_len=EVP_CIPHER_iv_length(evp_cipher);
        unsigned char                   iv[iv_len];

        /*
         * This implementation uses sequence numbers from the
         * context instead of trying to locate the SSL record sequence numbers
         * in the SSL structure.
         */

        /*
         * Message Token:
         * 8 byte sequence (big endian)
         * 4 byte input length (big endian)
         * 16 byte mac
         */
        message_token->value = token_value = malloc(8 + 4 + 16);

        /* copy sequence number to token */
        U642N(context->mac_write_sequence, token_value);
        token_value += 8;
        
        /* Increment it */
        context->mac_write_sequence++;

        /* Add input buffer length to token*/
        L2N(message_buffer->length, token_value);
        token_value += 4;

        tag = token_value;
        token_value += 16;

        /* IV is 8 byte sequence followed the first n bytes from mac_iv_fixed */
        assert(iv_len > 8);

        memcpy(iv, message_token->value, 8);
        memcpy(iv+8, context->mac_iv_fixed, iv_len - 8);

        major_status = globus_i_gssapi_gsi_gmac(
                minor_status,
                evp_cipher,
                iv,
                context->mac_key,
                message_buffer,
                tag);
        message_token->length = 4 + 8 + 16;
    }
#endif
    else
    {
        /* Shouldn't happen: some error occurred or some other type of cipher*/
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;

        goto unlock_mutex;
    }

    /* DEBUG BLOCK */
    if (globus_i_gsi_gssapi_debug_level >= 2)
    {
        unsigned int                    index;
        unsigned char *                 p;
        
        GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
            2, (globus_i_gsi_gssapi_debug_fstream,
                "get_mic: len=%zd mic:", message_token->length));
        p = message_token->value;
        for (index = 0;  index < message_token->length; index++)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%2.2X", *p++));
        }
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2 ,"\n");
    }
    
 unlock_mutex:
    globus_mutex_unlock(&context->mutex);
    
 exit:
    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

/**
 * @brief Sign
 * @ingroup globus_gsi_gssapi
 * @details
 * Deprecated.  Does the same thing as gss_get_mic for V1 compatibility.
 *
 * @param minor_status
 * @param context_handle
 * @param qop_req
 * @param message_buffer
 * @param message_token
 *
 * @return
 */
OM_uint32 
GSS_CALLCONV gss_sign(
    OM_uint32 *                         minor_status,
    gss_ctx_id_t                        context_handle,
    int                                 qop_req,
    gss_buffer_t                        message_buffer,
    gss_buffer_t                        message_token)
    
{
    OM_uint32                           major_status;
    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    major_status = gss_get_mic(minor_status, 
                               context_handle,
                               qop_req,
                               message_buffer,
                               message_token);

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}
