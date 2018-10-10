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
 * @file delete_sec_context.c
 * @author Sam Lang, Sam Meder
 */
#endif

#include "gssapi_openssl.h"
#include "globus_i_gsi_gss_utils.h"

/**
 * @brief Delete Security Context
 * @ingroup globus_gsi_gssapi
 * @details
 * Delete the GSS Security Context
 *
 * @param minor_status
 *        The minor status result - this is a globus_result_t
 *        cast to a OM_uint32.
 * @param context_handle_P
 *        The context handle to be deleted
 * @param output_token
 *        A token created upon destroying the context. If non-empty, this
 *        should be sent to the peer of the context to indicate that the
 *        context is closed.
 * @return This function always returns GSS_S_COMPLETE
 */
OM_uint32 
GSS_CALLCONV gss_delete_sec_context(
    OM_uint32 *                         minor_status,
    gss_ctx_id_t *                      context_handle_P, 
    gss_buffer_t                        output_token)
{
    gss_ctx_id_desc **                  context_handle = 
        (gss_ctx_id_desc**) context_handle_P;
    OM_uint32                           local_minor_status;
    OM_uint32                           local_major_status;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    globus_result_t                     local_result;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    *minor_status = (OM_uint32) GLOBUS_SUCCESS;

    if (output_token != GSS_C_NO_BUFFER)
    {
        output_token->length = 0;
        output_token->value = NULL;
    }

    if (*context_handle == NULL ||
        *context_handle == GSS_C_NO_CONTEXT)
    {
        goto exit;
    }

    /* lock the context mutex */
    
    globus_mutex_lock(&(*context_handle)->mutex);

    if ((*context_handle)->gss_state == GSS_CON_ST_DONE
        && (*context_handle)->gss_ssl 
        && output_token != GSS_C_NO_BUFFER)
    {
        SSL_shutdown((*context_handle)->gss_ssl);
        
        local_major_status = globus_i_gsi_gss_get_token(
            &local_minor_status,
            *context_handle,
            NULL,
            output_token);

        if(GSS_ERROR(local_major_status))
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CONTEXT);
            goto exit;
        }

        GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
            2, (globus_i_gsi_gssapi_debug_fstream,
                "delete_sec_context: output_token->length=%zd\n",
                output_token->length));
    }

    /* ignore errors to allow for incomplete context handles */

    local_result = globus_gsi_callback_data_destroy(
        (*context_handle)->callback_data);
    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_CALLBACK_DATA);
        major_status = GSS_S_FAILURE;
        goto exit;
    }
    (*context_handle)->callback_data = NULL;
    
    local_major_status = gss_release_cred(
        &local_minor_status,
        (gss_cred_id_t *)&(*context_handle)->peer_cred_handle);
    if(GSS_ERROR(local_major_status))
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto exit;
    } 

    if((*context_handle)->cred_obtained)
    {
        local_major_status = gss_release_cred(
            &local_minor_status,
            (gss_cred_id_t *) &(*context_handle)->cred_handle);
        if(GSS_ERROR(local_major_status))
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }

    local_result = globus_gsi_proxy_handle_destroy(
        (*context_handle)->proxy_handle);
    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_PROXY);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    if ((*context_handle)->gss_sslbio)
    {
        BIO_free_all((*context_handle)->gss_sslbio);
        (*context_handle)->gss_sslbio = NULL;
    }

    if ((*context_handle)->gss_ssl)
    {
        SSL_free((*context_handle)->gss_ssl);
        (*context_handle)->gss_ssl = NULL;
    } 

    major_status = gss_release_oid_set(
        minor_status,
        &(*context_handle)->extension_oids);
    if(GSS_ERROR(major_status))
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
            ("Can't delete oid set."));
        goto exit;
    }
    free((*context_handle)->sni_servername);
    if ((*context_handle)->sni_credentials_count > 0
        && (*context_handle)->sni_credentials_obtained)
    {
        for (size_t i = 0; i < (*context_handle)->sni_credentials_count; i++)
        {
            local_major_status = gss_release_cred(
                &local_minor_status,
                &(*context_handle)->sni_credentials[i]);
            if(GSS_ERROR(local_major_status))
            {
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
                major_status = GSS_S_FAILURE;
                goto exit;
            }
            (*context_handle)->sni_credentials[i] = GSS_C_NO_CREDENTIAL;
        }
    }
    free((*context_handle)->sni_credentials);
    free((*context_handle)->alpn);
#if OPENSSL_VERSION_NUMBER >= 0x10000100L
    free((*context_handle)->mac_key);
    free((*context_handle)->mac_iv_fixed);
#endif

    globus_mutex_unlock(&(*context_handle)->mutex);

    globus_mutex_destroy(&(*context_handle)->mutex);
    
    globus_libc_free(*context_handle);
    *context_handle = GSS_C_NO_CONTEXT;

    GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "delete_sec_context: done\n");

 exit:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return GSS_S_COMPLETE;
} 
/* gss_delete_sec_context */
