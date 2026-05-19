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
 * @file import_name.c
 * @author Sam Lang, Sam Meder
 */
#endif

#include "gssapi_openssl.h"
#include "globus_i_gsi_gss_utils.h"
#include "globus_gsi_gss_constants.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static
OM_uint32
gss_l_resolve_ip(
    OM_uint32 *                         minor_status,
    gss_name_desc *                     name);


/**
 * Import a name into a gss_name_t
 * @ingroup globus_gsi_gssapi
 *
 * Creates a new gss_name_t which contains a mechanism-specific representation
 * of the input name. GSSAPI OpenSSL implements the following name types, based
 * on the input_name_type OID:
 *
 * - GSS_C_NT_ANONYMOUS (input_name_buffer is ignored)
 * - GSS_C_NT_HOSTBASED_SERVICE (input_name_buffer contains a string
 * "service@FQN" which will match /CN=service/FQDN)
 * - GSS_C_NT_EXPORT_NAME (input_name_buffer contains a string with the
 * X509_oneline representation of a name)
 * like "/X=Y/Z=A...")
 * - GSS_C_NO_OID or GSS_C_NT_USER_NAME (input_name_buffer contains an X.500
 *   name formatted
 * like "/X=Y/Z=A...")
 * - GLOBUS_GSS_C_NT_HOST_IP (input_name_buffer contains a string
 * "FQDN/ip-address" which will match names with the FQDN or the IP address)
 * - GLOBUS_SSS_C_NT_X509 (input buffer is an X509 struct from OpenSSL)
 *
 * @param minor_status
 *     Minor status
 * @param input_name_buffer
 *     Input name buffer which is interpreted based on the @a input_name_type
 * @param input_name_type
 *     OID of the name
 * @param output_name_P
 *     New gss_name_t value containing the name
 *
 * @retval GSS_S_COMPLETE
 *     indicates that a valid name representation is
 *     output in output_name and described by the type value in
 *     output_name_type.
 * @retval GSS_S_BAD_NAMETYPE
 *     indicates that the input_name_type is unsupported
 *     by the applicable underlying GSS-API mechanism(s), so the import
 *     operation could not be completed.
 * @retval GSS_S_BAD_NAME
 *     indicates that the provided input_name_string is ill-formed in terms of
 *     the input_name_type, so the import operation could not be completed.
 * @retval GSS_S_BAD_MECH
 *     indicates that the input presented for import was an exported name
 *     object and that its enclosed mechanism type was not recognized or was
 *     unsupported by the GSS-API implementation.
 * @retval GSS_S_FAILURE
 *     indicates that the requested operation could not be performed for
 *     reasons unspecified at the GSS-API level.
 */
OM_uint32
GSS_CALLCONV gss_import_name(
    OM_uint32 *                         minor_status,
    const gss_buffer_t                  input_name_buffer,
    const gss_OID                       input_name_type,
    gss_name_t *                        output_name_P)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    globus_result_t                     local_result = GLOBUS_SUCCESS;
    gss_name_desc *                     output_name = NULL;
    int                                 length, i;
    char *                              name_buffer = NULL;
    char *                              index;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    if (minor_status == NULL || input_name_buffer == NULL ||
        output_name_P == NULL)
    {
        major_status = GSS_S_FAILURE;

        if (minor_status != NULL)
        {
            GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
                    (_GGSL("Invalid parameter")));
        }

        goto out;
    }

    *minor_status = (OM_uint32) GLOBUS_SUCCESS;

    output_name = calloc(1, sizeof(gss_name_desc));

    if (output_name == NULL)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;
        goto out;
    }

    output_name->name_oid = input_name_type;
    output_name->x509n = X509_NAME_new();
    if (output_name->x509n == NULL)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;
        goto release_name_out;
    }

    if(g_OID_equal(input_name_type, GSS_C_NT_ANONYMOUS))
    {
        ;
    }
    else if (g_OID_equal(GSS_C_NT_HOSTBASED_SERVICE, input_name_type))
    {
        name_buffer = malloc(input_name_buffer->length+1);
        if (name_buffer == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto release_name_out;
        }

        memcpy(name_buffer,
               input_name_buffer->value,
               input_name_buffer->length);
        name_buffer[input_name_buffer->length] = '\0';

        index = strchr(name_buffer, '@');
        if (index)
        {
            *index = '\0';
            output_name->service_name = name_buffer;
            name_buffer = NULL;
            output_name->host_name = globus_libc_strdup(index+1);
            if (output_name->host_name == NULL)
            {
                GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                major_status = GSS_S_FAILURE;

                goto release_name_out;
            }
        }
        else
        {
            output_name->host_name = name_buffer;
            name_buffer = NULL;
            output_name->service_name = globus_libc_strdup("host");
            if (output_name->service_name == NULL)
            {
                GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                major_status = GSS_S_FAILURE;

                goto release_name_out;
            }
        }
        name_buffer = globus_common_create_string(
                "/CN=%s%s%s",
                output_name->service_name ? output_name->service_name : "",
                output_name->service_name ? "/" : "",
                output_name->host_name);
        if (name_buffer == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;

            goto release_name_out;
        }

        local_result = globus_gsi_cert_utils_get_x509_name(
                name_buffer,
                strlen(name_buffer),
                output_name->x509n);

        free(name_buffer);
        name_buffer = NULL;

        if (local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME);
            major_status = GSS_S_FAILURE;

            goto release_name_out;
        }
        output_name->x509n_oneline =
                X509_NAME_oneline(output_name->x509n, NULL, 0);
        if (output_name->x509n_oneline == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;

            goto release_name_out;
        }
    }
    else if (g_OID_equal(GSS_C_NT_EXPORT_NAME, input_name_type))
    {
        /* Note: Does not support all name types */
        output_name->name_oid = GSS_C_NO_OID;
        name_buffer = input_name_buffer->value;

        i = 0;
        if (name_buffer[i++] != 0x04 || name_buffer[i++] != 0x01 ||
            name_buffer[i++] !=
                ((gss_mech_globus_gssapi_openssl->length+2) >> 8) ||
            name_buffer[i++] !=
                ((gss_mech_globus_gssapi_openssl->length+2) & 0xff) ||
            name_buffer[i++] != 0x06 ||
            name_buffer[i++] !=
                (gss_mech_globus_gssapi_openssl->length & 0xff) ||
            (memcmp(&(name_buffer[i]), gss_mech_globus_gssapi_openssl->elements,
                    gss_mech_globus_gssapi_openssl->length) != 0))
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME);
            major_status = GSS_S_BAD_NAME;
            goto release_name_out;
        }

        i += gss_mech_globus_gssapi_openssl->length;
        length = name_buffer[i++] << 24;
        length += name_buffer[i++] << 16;
        length += name_buffer[i++] << 8;
        length += name_buffer[i++] & 0xff;

        local_result = globus_gsi_cert_utils_get_x509_name(
            &(name_buffer[i]),
            length,
            output_name->x509n);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME);
            major_status = GSS_S_BAD_NAME;
            goto release_name_out;
        }
        output_name->x509n_oneline =
                X509_NAME_oneline(output_name->x509n, NULL, 0);

        major_status = globus_i_gsi_gssapi_get_hostname(
                minor_status,
                output_name);
        if (GSS_ERROR(major_status))
        {
            goto release_name_out;
        }
    }
    else if (g_OID_equal(GSS_C_NO_OID, input_name_type) ||
             g_OID_equal(GSS_C_NT_USER_NAME, input_name_type))
    {
        output_name->user_name = malloc(input_name_buffer->length + 1);
        if (output_name->user_name == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;

            goto release_name_out;
        }
        memcpy(output_name->user_name,
               input_name_buffer->value,
               input_name_buffer->length);
        output_name->user_name[input_name_buffer->length] = '\0';

        local_result = globus_gsi_cert_utils_get_x509_name(
                output_name->user_name,
                strlen(output_name->user_name),
                output_name->x509n);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME);
            major_status = GSS_S_BAD_NAME;

            goto release_name_out;
        }
        output_name->x509n_oneline =
                X509_NAME_oneline(output_name->x509n, NULL, 0);
        if (output_name->x509n_oneline == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;

            goto release_name_out;
        }
        major_status = globus_i_gsi_gssapi_get_hostname(
                minor_status, output_name);
        if (GSS_ERROR(major_status))
        {
            goto release_name_out;
        }
    }
    else if (g_OID_equal(GLOBUS_GSS_C_NT_HOST_IP, input_name_type))
    {
        name_buffer = malloc(input_name_buffer->length+1);
        if (name_buffer == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;

            goto release_name_out;
        }
        memcpy(name_buffer,
               input_name_buffer->value, input_name_buffer->length);
        name_buffer[input_name_buffer->length] = '\0';

        index = strchr(name_buffer, '/');
        if (index)
        {
            *index = '\0';
            if (index == name_buffer)
            {
                output_name->host_name = NULL;
            }
            else
            {
                output_name->host_name = name_buffer;
                name_buffer = NULL;
            }
            if (strlen(index+1) > 0)
            {
                output_name->ip_address = globus_libc_strdup(index+1);

                if (output_name->ip_address == NULL)
                {
                    GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                    major_status = GSS_S_FAILURE;

                    goto release_name_out;
                }
            }
            else
            {
                output_name->ip_address = NULL;
            }
        }
        else
        {
            output_name->host_name = name_buffer;
            name_buffer = NULL;
            output_name->ip_address = NULL;
        }
        name_buffer = globus_common_create_string(
                "/CN=%s",
                output_name->host_name);
        if (name_buffer == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;

            goto release_name_out;
        }

        local_result = globus_gsi_cert_utils_get_x509_name(
                name_buffer,
                strlen(name_buffer),
                output_name->x509n);
        free(name_buffer);
        name_buffer = NULL;
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME);
            major_status = GSS_S_BAD_NAME;

            goto release_name_out;
        }
        output_name->x509n_oneline =
                X509_NAME_oneline(output_name->x509n, NULL, 0);
        if (output_name->x509n_oneline == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;

            goto release_name_out;
        }
        major_status = gss_l_resolve_ip(minor_status, output_name);
    }
    else if (g_OID_equal(GLOBUS_GSS_C_NT_X509, input_name_type))
    {
        X509_NAME                      *n;
        X509                           *x509_input;
        GENERAL_NAMES                  *subject_alt_name;
        int                             idx;

        if (input_name_buffer->length != sizeof(X509*))
        {
            /* Invalid input */
            GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
                    (_GGSL("Invalid parameter")));
            major_status = GSS_S_FAILURE;

            goto release_name_out;
        }

        x509_input = input_name_buffer->value;
        /* Extract SubjectName if present */
        if ((n = X509_get_subject_name(x509_input)) != NULL)
        {
            X509_NAME_free(output_name->x509n);

            output_name->x509n = X509_NAME_dup(n);
            if (output_name->x509n == NULL)
            {
                GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                major_status = GSS_S_FAILURE;

                goto release_name_out;
            }
            output_name->x509n_oneline =
                    X509_NAME_oneline(output_name->x509n, NULL, 0);
            if (output_name->x509n_oneline == NULL)
            {
                GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                major_status = GSS_S_FAILURE;

                goto release_name_out;
            }
            major_status = globus_i_gsi_gssapi_get_hostname(
                    minor_status, output_name);
            if (GSS_ERROR(major_status))
            {
                goto release_name_out;
            }
        }

        /* Extract subjectAltName if present */
        idx = -1;
        for (idx = X509_get_ext_by_NID(x509_input, NID_subject_alt_name, idx);
             idx != -1;
             idx = X509_get_ext_by_NID(x509_input, NID_subject_alt_name, idx))
        {
            X509_EXTENSION * ext_value;

            ext_value = X509_get_ext(x509_input, idx);
            if (!ext_value)
            {
                continue;
            }
            subject_alt_name = X509V3_EXT_d2i(ext_value);
            if (!subject_alt_name)
            {
                continue;
            }

            output_name->subjectAltNames =
                    sk_GENERAL_NAME_dup(subject_alt_name);
            sk_GENERAL_NAME_free(subject_alt_name);
            if (output_name->subjectAltNames == NULL)
            {
                GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                major_status = GSS_S_FAILURE;

                goto release_name_out;
            }
        }
    }
    else
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME,
                (_GGSL("Bad name type")));

        major_status = GSS_S_BAD_NAMETYPE;

        goto release_name_out;
    }

    if (major_status != GSS_S_COMPLETE)
    {
        OM_uint32 dummy;
release_name_out:
        gss_release_name(&dummy, &output_name);
    }

    *output_name_P = output_name;

 out:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}
/* gss_import_name */


static
OM_uint32
gss_l_resolve_ip(
    OM_uint32 *                         minor_status,
    gss_name_desc *                     name)
{
#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif
    char                                realhostname[NI_MAXHOST + 1];
    OM_uint32                           major_status;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_addrinfo_t                   hints;
    globus_addrinfo_t *                 addrinfo = NULL;

    major_status = GSS_S_COMPLETE;

    memset(&hints, 0, sizeof(globus_addrinfo_t));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    /*
     * Hostname is an ip address: do a non-canonname getaddrinfo to get
     * the sockaddr, then getnameinfo to get the canonical hostname from that
     * address
     */
    hints.ai_flags = GLOBUS_AI_NUMERICHOST;
    result = globus_libc_getaddrinfo(name->ip_address, NULL, &hints, &addrinfo);

    if (result == GLOBUS_SUCCESS)
    {
        if (addrinfo == NULL || addrinfo->ai_addr == NULL)
        {
            goto error_exit;
        }

        /*
         * For connections to localhost, check for certificate
         * matching our real hostname, not "localhost"
         */
        if (globus_libc_addr_is_loopback(
            (const globus_sockaddr_t *) addrinfo->ai_addr) == GLOBUS_TRUE)
        {
            globus_libc_gethostname(
                realhostname, sizeof(realhostname) - 1);
        }
        else
        {
            /* use GLOBUS_NI_NAMEREQD to fail if address can't be looked up?
             * if not, realhostname will just be the same ip address
             * we pass in */
            result = globus_libc_getnameinfo(
                (const globus_sockaddr_t *) addrinfo->ai_addr,
                realhostname,
                sizeof(realhostname) - 1,
                NULL,
                0,
                0);
            if(result != GLOBUS_SUCCESS)
            {
                goto error_exit;
            }
        }
    }
    else
    {
        goto error_exit;
    }

    name->ip_name = globus_libc_strdup(realhostname);
    if (name->ip_name == NULL)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;
    }

 error_exit:
    if (addrinfo != NULL)
    {
        globus_libc_freeaddrinfo(addrinfo);
    }
    return major_status;
}
/* gss_l_resolve_ip() */
