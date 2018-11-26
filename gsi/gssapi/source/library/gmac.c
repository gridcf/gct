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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

/**
 * @file gmac.c
 * @brief GCM Message Authentication Code
 */

#include "gssapi.h"
#include "globus_i_gsi_gss_utils.h"
#include "gssapi_openssl.h"

/**
 * @brief Compute GCM MAC
 * @details
 *     This function computes the GCM MAC of a message by performing a
 *     GCM encryption with no ciphertext and the input message as the
 *     "additional authenticated data" (AAD). The resulting MAC is returned
 *     in the array pointed to by *tag*.
 *
 *     See NIST Special Publication 800-38D.
 */
OM_uint32
globus_i_gssapi_gsi_gmac(
    /**
     * [out] Pointer to set to the mechanism specific status code. The
     * value pointed to by this is always modified by this function
     */
    OM_uint32 *                         minor_status,
    /** [in] Cipher to use. This must be a GCM mode cipher */
    const EVP_CIPHER *                  evp_cipher,
    /** [in] Initialization vector */
    const unsigned char *               iv,
    /** [in] GCM Key */
    const unsigned char *               key,
    /** [in] Message to compute the MAC of */
    const gss_buffer_desc              *message_buffer,
    /** [out] Pointer to an array to hold the MAC */
    unsigned char                       tag[static 16])
{
#ifndef EVP_CIPH_GCM_MODE
    *minor_status = GLOBUS_FAILURE;
    return GSS_S_FAILURE;
#else
    OM_uint32                           major_status = GSS_S_COMPLETE;
    EVP_CIPHER_CTX                     *ctx = NULL;
    unsigned char                       ciphertext[1] = {0};
    int                                 len = 0;


    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    assert(EVP_CIPHER_mode(evp_cipher) == EVP_CIPH_GCM_MODE);

    if ((ctx = EVP_CIPHER_CTX_new()) == NULL)
    {
        /* Shouldn't happen: some error occurred */
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;

        goto ctx_new_error;
    }

    /* Initialise key and IV */
    if (EVP_EncryptInit_ex(ctx, evp_cipher, NULL, key, iv) != 1)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;

        goto init_error;
    }

    /* Pass the message to sign as additional authenticated data (AAD). */
    if (EVP_EncryptUpdate(
                ctx,
                NULL,
                &len,
                message_buffer->value,
                message_buffer->length) != 1)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;

        goto update_error;
    }

    /* Finalize the "encryption" */
    if (EVP_EncryptFinal_ex(ctx, ciphertext, &len) != 1)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;

        goto finalize_error;
    }
    assert(len == 0);

    /* Get the "tag" which is the MAC of the (empty) plaintext + aad */
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;

        goto tag_error;
    }
tag_error:
finalize_error:
update_error:
init_error:
    EVP_CIPHER_CTX_free(ctx);
ctx_new_error:
    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;

    /**
     * @return
     *     On success, this function returns GSS_S_COMPLETE, and updates
     *     the *tag* array. On failure, it returns GSS_S_FAILURE.
     */
    return major_status;
#endif
}
/* globus_i_gssapi_gsi_gmac() */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
