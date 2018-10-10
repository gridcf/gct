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
 * @file set_sec_context_option.c
 * @author Sam Lang, Sam Meder
 */
#endif

#include "gssapi_openssl.h"
#include "globus_i_gsi_gss_utils.h"
#include <string.h>
#include "openssl/evp.h"

/* Only build if we have the extended GSSAPI */
#ifdef  _HAVE_GSI_EXTENDED_GSSAPI

static const gss_OID_desc GSS_DISALLOW_ENCRYPTION_OID =
   {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x03\x01"}; 
const gss_OID_desc * const GSS_DISALLOW_ENCRYPTION =
   &GSS_DISALLOW_ENCRYPTION_OID;

static const gss_OID_desc GSS_PROTECTION_FAIL_ON_CONTEXT_EXPIRATION_OID =
   {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x03\x02"}; 
const gss_OID_desc * const GSS_PROTECTION_FAIL_ON_CONTEXT_EXPIRATION =
   &GSS_PROTECTION_FAIL_ON_CONTEXT_EXPIRATION_OID;

static const gss_OID_desc GSS_APPLICATION_WILL_HANDLE_EXTENSIONS_OID =
   {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x03\x03"}; 
const gss_OID_desc * const GSS_APPLICATION_WILL_HANDLE_EXTENSIONS =
   &GSS_APPLICATION_WILL_HANDLE_EXTENSIONS_OID;

static const gss_OID_desc GSS_SNI_CREDENTIALS_OID =
   {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x03\x04"};
const gss_OID_desc * const GSS_SNI_CREDENTIALS =
   &GSS_SNI_CREDENTIALS_OID;

static const gss_OID_desc GSS_ALPN_OID =
   {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x03\x05"};
const gss_OID_desc * const GSS_ALPN =
   &GSS_ALPN_OID;

/**
 * @brief Set Security Context Option
 * @ingroup globus_gsi_gssapi_extensions
 * @details
 * GSSAPI routine to initiate the sending of a security context
 * See: <draft-ietf-cat-gssv2-cbind-04.txt>
 *
 * @param minor_status
 * @param context_handle
 * @param option
 * @param value
 *
 * @return
 */
OM_uint32
GSS_CALLCONV gss_set_sec_context_option(
    OM_uint32 *                         minor_status,
    gss_ctx_id_t *                      context_handle,
    const gss_OID                       option,
    const gss_buffer_t                  value)
{
    gss_ctx_id_desc *                   context = NULL;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           local_minor_status;
    globus_result_t                     local_result = GLOBUS_SUCCESS;
    int                                 index;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;
    
    if(minor_status == NULL)
    {
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    *minor_status = (OM_uint32) GLOBUS_SUCCESS;
    
    if(context_handle == NULL)
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Invalid context_handle passed to function - cannot be NULL")));
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    context = *context_handle;

    if(option == GSS_C_NO_OID)
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Invalid option passed to function with value: GSS_C_NO_OID")));
        major_status = GSS_S_FAILURE;
        goto exit;
    }
    
    if (*context_handle == GSS_C_NO_CONTEXT)
    {
        /* for now just malloc and zero the context */
        context = (gss_ctx_id_desc *) malloc(sizeof(gss_ctx_id_desc));
        if (context == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }

        *context_handle = context;
        memset(context, 0, sizeof(gss_ctx_id_desc));
        context->ctx_flags = 0;

        major_status = gss_create_empty_oid_set(
            &local_minor_status,
            (gss_OID_set *) &context->extension_oids);

        /* initialize the callback_data in the context.  This needs
         * to be done so the verify_callback func can be set later.
         */
        local_result = globus_gsi_callback_data_init(
            &context->callback_data);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CONTEXT,
                (_GGSL("The callback data in the context "
                 "could not be initialized.")));
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }
    else if(context->ctx_flags & GSS_I_CTX_INITIALIZED)
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CONTEXT,
            (_GGSL("The context has already been initialized!  %s should be "
             "called on a context before initialization"), __func__));
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    if(g_OID_equal(option, GSS_DISALLOW_ENCRYPTION))
    {
        context->ctx_flags |= GSS_I_DISALLOW_ENCRYPTION;
    }
    else if(g_OID_equal(option, GSS_PROTECTION_FAIL_ON_CONTEXT_EXPIRATION))
    {
        context->ctx_flags |= GSS_I_PROTECTION_FAIL_ON_CONTEXT_EXPIRATION;
    }
    else if(g_OID_equal(option, GSS_APPLICATION_WILL_HANDLE_EXTENSIONS))
    {
        if(value == GSS_C_NO_BUFFER)
        {
            GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
                (_GGSL("Invalid buffer passed to function")));
            major_status = GSS_S_FAILURE;
            goto exit;
        }

        for(index = 0; 
            index < ((gss_OID_set_desc *) value->value)->count; 
            index++)
        {
            major_status = gss_add_oid_set_member(
                &local_minor_status,
                (gss_OID) 
                &((gss_OID_set_desc *) value->value)->elements[index],
                (gss_OID_set *) &context->extension_oids);

            if(GSS_ERROR(major_status))
            {
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_OID);
                goto exit;
            }
        }

        local_result = globus_gsi_callback_set_extension_cb(
            context->callback_data,
            globus_i_gsi_gss_verify_extensions_callback);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_CALLBACK_DATA);
            major_status = GSS_S_FAILURE;
            goto exit;
        }

        /* if the extension_oids are set, 
         * then we set them in the callback data */
        local_result = globus_gsi_callback_set_extension_oids(
            context->callback_data,
            (void *) context->extension_oids);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_CALLBACK_DATA);
            major_status = GSS_S_FAILURE;
            goto exit;
        }

        context->ctx_flags |= GSS_I_APPLICATION_WILL_HANDLE_EXTENSIONS;
    }
    else if(g_OID_equal(option, GSS_SNI_CREDENTIALS))
    {
        if (value == GSS_C_NO_BUFFER)
        {
            GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
                (_GGSL("Invalid buffer passed to function")));
            major_status = GSS_S_FAILURE;
            goto exit;
        }
        context->sni_credentials = malloc(value->length);
        memcpy(context->sni_credentials, value->value, value->length);
        context->sni_credentials_count = value->length / sizeof(gss_cred_id_t);
    }
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
    else if(g_OID_equal(option, GSS_ALPN))
    {
        if (value == GSS_C_NO_BUFFER)
        {
            GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
                (_GGSL("Invalid buffer passed to function")));
            major_status = GSS_S_FAILURE;
            goto exit;
        }
        context->alpn = malloc(value->length);
        memcpy(context->alpn, value->value, value->length);
        context->alpn_length = value->length;
    }
#endif
    else
    {
        /* unknown option */
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_UNKNOWN_OPTION,
            (NULL));
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    *context_handle = context;

 exit:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

#endif /* _HAVE_GSI_EXTENDED_GSSAPI */
