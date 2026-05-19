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
 * @file inquire_cred_by_oid.c
 * @author Sam Lang, Sam Meder
 */
#endif

#include "gssapi_openssl.h"
#include "globus_i_gsi_gss_utils.h"
#include <string.h>

/* Only build if we have the extended GSSAPI */
#ifdef _HAVE_GSI_EXTENDED_GSSAPI

/**
 * @brief Inquire Cred By OID
 * @ingroup globus_gsi_gssapi_extensions
 * @details
 * NOTE: Checks both the cert in the credential and
 * the certs in the cert chain for a valid extension
 * that matches the desired OID.  The first one found
 * is used, starting with the endpoint cert, and then
 * searching the cert chain.
 *
 *
 * @param minor_status
 * @param cred_handle
 * @param desired_object
 * @param data_set
 *
 * @return
 */
OM_uint32
GSS_CALLCONV gss_inquire_cred_by_oid(
    OM_uint32 *                         minor_status,
    const gss_cred_id_t                 cred_handle,
    const gss_OID                       desired_object,
    gss_buffer_set_t *                  data_set)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           local_minor_status;
    X509_EXTENSION *                    extension;
    X509 *                              cert = NULL;
    STACK_OF(X509) *                    cert_chain = NULL;
    ASN1_OBJECT *                       desired_asn1_obj;
    ASN1_OCTET_STRING *                 asn1_oct_string;
    gss_buffer_desc                     data_set_buffer;
    int                                 chain_index;
    int                                 found_index;
    globus_result_t                     local_result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    if(minor_status == NULL)
    {
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    *minor_status = (OM_uint32) GLOBUS_SUCCESS;

    /* parameter checking goes here */

    if(cred_handle == GSS_C_NO_CREDENTIAL)
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Invalid credential handle passed to function")));
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    if(desired_object == GSS_C_NO_OID)
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Invalid desired object passed to function")));
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    if(data_set == NULL)
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Invalid data_set passed to function")));
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    local_result = globus_gsi_cred_get_cert_chain(
        ((gss_cred_id_desc *)cred_handle)->cred_handle,
        &cert_chain);
    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_CALLBACK_DATA);
        major_status = GSS_S_FAILURE;
        cert_chain = NULL;
        goto exit;
    }

    major_status = gss_create_empty_buffer_set(
        &local_minor_status,
        data_set);

    if(GSS_ERROR(major_status))
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_BUFFER);
        goto exit;
    }

    local_result = globus_gsi_cred_get_cert(
        ((gss_cred_id_desc *)cred_handle)->cred_handle,
        &cert);
    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    /* create the ASN1_OBJECT from the gss_OID structure */
    desired_asn1_obj = ASN1_OBJECT_create(
            NID_undef,
            desired_object->elements,
            desired_object->length, NULL, NULL);
    if(!desired_asn1_obj)
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
            (_GGSL("Couldn't create ASN1_OBJECT for the desired extension")));
        major_status = GSS_S_FAILURE;
        goto exit;
    }


    /* check the extensions in the cert first */

    chain_index = 0;
    found_index = -1;

    do
    {
        data_set_buffer.value = NULL;
        data_set_buffer.length = 0;

        found_index = X509_get_ext_by_OBJ(cert, desired_asn1_obj, found_index);
        if(found_index >= 0)
        {
            /* no extension with correct OID found */
            extension = X509_get_ext(cert, found_index);
            if(!extension)
            {
                GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
                    (_GGSL("Couldn't get extension at index %d "
                     "from cert in credential."),
                     found_index));
                major_status = GSS_S_FAILURE;
                goto exit;
            }

            asn1_oct_string = X509_EXTENSION_get_data(extension);
            if(!asn1_oct_string)
            {
                GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
                    (_GGSL("Couldn't get cert extension in the form of an "
                     "ASN1 octet string.")));
                major_status = GSS_S_FAILURE;
                goto exit;
            }

            data_set_buffer.value = asn1_oct_string->data;
            data_set_buffer.length = asn1_oct_string->length;

            major_status = gss_add_buffer_set_member(
                &local_minor_status,
                &data_set_buffer,
                data_set);
            if(GSS_ERROR(major_status))
            {
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_BUFFER);
                goto exit;
            }
        }

    } while(chain_index < sk_X509_num(cert_chain) &&
            (cert = sk_X509_value(cert_chain, chain_index++)));

 exit:

    if(cert_chain != NULL)
    {
        sk_X509_pop_free(cert_chain, X509_free);
    }

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

#endif /* _HAVE_GSI_EXTENDED_GSSAPI */
