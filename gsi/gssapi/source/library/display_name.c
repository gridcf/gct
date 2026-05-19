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
 * @file display_name.c
 * @author Sam Lang, Sam Meder
 */
#endif

#include "gssapi.h"
#include "globus_i_gsi_gss_utils.h"
#include "gssapi_openssl.h"
#include <string.h>

#define GSS_I_ANON_NAME "<anonymous>"

/**
 * @brief Display Name
 * @ingroup globus_gsi_gssapi
 * @details
 * Produces a single line version of the internal X.509 name
 *
 * @param minor_status
 * @param input_name_P
 * @param output_name
 * @param output_name_type
 *
 * @return
 */
OM_uint32
GSS_CALLCONV
gss_display_name(
    OM_uint32 *                         minor_status,
    const gss_name_t                    input_name_P,
    gss_buffer_t                        output_name,
    gss_OID *                           output_name_type)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;

    const gss_name_desc*                input_name =
                                        (gss_name_desc*) input_name_P;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    *minor_status = (OM_uint32) GLOBUS_SUCCESS;

    if (!(input_name) || !(output_name))
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            ("Bad argument"));
        goto exit;
    }

    output_name->value = NULL;
    output_name->length = 0;

    if (output_name_type != NULL)
    {
        *output_name_type = GSS_C_NO_OID;
    }
    if(g_OID_equal(input_name->name_oid, GSS_C_NT_ANONYMOUS))
    {
        output_name->value = globus_libc_strdup(GSS_I_ANON_NAME);
        if (output_name->value == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
        output_name->length = strlen(GSS_I_ANON_NAME);
    }
    else if (g_OID_equal(input_name->name_oid, GSS_C_NO_OID) ||
             g_OID_equal(input_name->name_oid, GSS_C_NT_USER_NAME))
    {
        output_name->value = globus_libc_strdup(input_name->user_name);
        if (output_name->value == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
        output_name->length = strlen(output_name->value);
    }
    else if (g_OID_equal(input_name->name_oid, GSS_C_NT_HOSTBASED_SERVICE))
    {
        if (input_name->service_name)
        {
            output_name->value = globus_common_create_string(
                    "/CN=%s/%s",
                    input_name->service_name,
                    input_name->host_name);
        }
        else
        {
            output_name->value = globus_libc_strdup(input_name->host_name);
        }
        output_name->length = strlen(output_name->value);
    }
    else if (g_OID_equal(input_name->name_oid, GLOBUS_GSS_C_NT_HOST_IP))
    {
        output_name->value = globus_common_create_string(
                "%s%s%s",
                input_name->host_name ? input_name->host_name : "",
                input_name->ip_address ? "/" : "",
                input_name->ip_address ? input_name->ip_address : "");
        if (output_name->value == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
        output_name->length = strlen(output_name->value);
    }
    else if (g_OID_equal(input_name->name_oid, GLOBUS_GSS_C_NT_X509))
    {
        /* For X.509 names, we only put SubjectName in the displayed name */
        if (input_name->x509n != NULL)
        {
            output_name->value = X509_NAME_oneline(input_name->x509n, NULL, 0);
            if (output_name->value == NULL)
            {
                GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                major_status = GSS_S_FAILURE;
                goto exit;
            }
            output_name->length = strlen(output_name->value);
        }
        else if (input_name->subjectAltNames)
        {
            int                         name_length;
            GENERAL_NAME *              name;
            unsigned char *             dns;
            int                         i;

            name_length = sk_GENERAL_NAME_num(input_name->subjectAltNames);
            for (i = 0; i < name_length; i++)
            {
                name = sk_GENERAL_NAME_value(input_name->subjectAltNames, i);

                if (name->type == GEN_DNS)
                {
                    dns = ASN1_STRING_data(name->d.dNSName);
                    output_name->value =
                            globus_common_create_string("/CN=%s", (char *) dns);
                    if (output_name->value == NULL)
                    {
                        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                        major_status = GSS_S_FAILURE;
                        goto exit;
                    }
                    output_name->length = strlen(output_name->value);
                    break;
                }
            }
        }
        if (output_name->value == NULL)
        {
            major_status = GSS_S_BAD_NAME;
            GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME,
                ("X.509 Name contains no SubjectName and no dNSName."));
            goto exit;
        }
    }
    else
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME,
            ("Bad Name"));
        goto exit;
    }

    if(output_name_type)
    {
        *output_name_type = input_name->name_oid;
    }

 exit:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}
/* gss_display_name */
