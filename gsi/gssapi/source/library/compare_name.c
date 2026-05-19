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
 * @file compare_name.c
 * @brief Globus GSI GSSAPI gss_compare_name
 * @author Sam Meder, Sam Lang
 */
#endif

#include "gssapi_openssl.h"
#include "globus_i_gsi_gss_utils.h"
#include "openssl/x509v3.h"

#include <ctype.h>
#include <string.h>

#ifdef WIN32
#define strcasecmp stricmp
#define strncasecmp strnicmp
#else
#include <strings.h>
#endif

/* Comparison types */
typedef enum
{
    GSS_I_COMPARE_NT_ANONYMOUS,
    GSS_I_COMPARE_NT_X509,
    GSS_I_COMPARE_NT_NO_OID,
    GSS_I_COMPARE_NT_HOSTBASED_SERVICE,
    GSS_I_COMPARE_NT_HOST_IP
}
gss_l_compare_type_t;

/* Wildcard types */
typedef enum
{
    GSS_I_WILDCARD_NONE,
    GSS_I_WILDCARD_GT2,
    GSS_I_WILDCARD_RFC2818
}
gss_l_wildcard_type_t;

/* Compatibility modes */
gss_i_name_compatibility_mode_t         gss_i_name_compatibility_mode =
        GSS_I_COMPATIBILITY_HYBRID;

static
char * gss_l_name_types[] =
{
    "GSS_C_NT_ANONYMOUS",
    "GLOBUS_GSS_C_NT_X509",
    "GSS_C_NO_OID",
    "GSS_C_NT_HOSTBASED_SERVICE",
    "GLOBUS_GSS_C_NT_HOST_IP"
};

static
OM_uint32
gss_l_compare_x509_to_x509(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal);

static
OM_uint32
gss_l_compare_x509_to_default(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal);

static
OM_uint32
gss_l_compare_x509_to_hostbased_service(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal);

static
OM_uint32
gss_l_compare_x509_to_host_ip(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal);

static
OM_uint32
gss_l_compare_default_to_default(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal);

static
OM_uint32
gss_l_compare_default_to_hostbased_service(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal);

static
OM_uint32
gss_l_compare_default_to_host_ip(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal);

static
OM_uint32
gss_l_compare_hostbased_service_to_hostbased_service(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal);

static
OM_uint32
gss_l_compare_hostbased_service_to_host_ip(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal);

static
OM_uint32
gss_l_compare_host_ip_to_host_ip(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal);

static
OM_uint32
gss_l_compare_hostnames_with_wildcards(
    OM_uint32 *                         minor_status,
    const char *                        host1,
    gss_l_wildcard_type_t               wildcards1,
    const char *                        host2,
    gss_l_wildcard_type_t               wildcards2,
    int *                               name_equal);

static
char *
gss_l_strsep(char **stringp, const char * delim);

static
int
gss_l_get_oid_type(
    gss_OID                             oid);

/**
 * @brief Compare Name
 * @ingroup globus_gsi_gssapi
 * @details
 * Compare two names. GSSAPI names in this implementation
 * are pointers to X.509 names.
 *
 * @param minor_status
 *        currently is always set to GLOBUS_SUCCESS
 * @param name1_P
 * @param name2_P
 * @param name_equal
 *
 * @return
 *        currently always returns GSS_S_COMPLETE
 */
OM_uint32
GSS_CALLCONV gss_compare_name(
    OM_uint32 *                         minor_status,
    const gss_name_t                    name1_P,
    const gss_name_t                    name2_P,
    int *                               name_equal)
{
    OM_uint32                           major_status;
    int                                 type1 = -1;
    int                                 type2 = -1;
    gss_name_t                          name1 = name1_P, name2 = name2_P;
    /* Order must match gss_l_compare_type_t */
    /* Activate module the first time this is called if it hasn't happened yet
     */
    globus_thread_once(
        &once_control,
        globus_l_gsi_gssapi_activate_once);

    globus_mutex_lock(&globus_i_gssapi_activate_mutex);
    if (!globus_i_gssapi_active)
    {
        globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE);
    }
    globus_mutex_unlock(&globus_i_gssapi_activate_mutex);


    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    major_status = GSS_S_COMPLETE;
    if (name_equal != NULL)
    {
        *name_equal = GSS_NAMES_NOT_EQUAL;
    }

    if (minor_status == NULL ||
        name1 == NULL || name2 == NULL || name_equal == NULL)
    {
        major_status = GSS_S_FAILURE;

        if (minor_status != NULL)
        {
            GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
                    (_GGSL("Invalid parameter")));
        }

        goto exit;
    }

    if (name1 == GSS_C_NO_NAME && name2 == GSS_C_NO_NAME)
    {
        *name_equal = GSS_NAMES_EQUAL;
        goto exit;
    }
    else if (name1 == GSS_C_NO_NAME || name2 == GSS_C_NO_NAME)
    {
        *name_equal = GSS_NAMES_NOT_EQUAL;
        goto exit;
    }

    /* Convert name types from gss_OID to integer for easier comparisons below
     */

    type1 = gss_l_get_oid_type(name1->name_oid);
    type2 = gss_l_get_oid_type(name2->name_oid);

    if (type1 == -1 || type2 == -1)
    {
        /* Unknown or unsupported name type */
        major_status = GSS_S_BAD_NAMETYPE;

        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME,
                (_GGSL("Invalid or unsupported name type")));
        goto exit;
    }

    GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, _GGSL("Comparing names:\n"));
    GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(2, (globus_i_gsi_gssapi_debug_fstream,
                                      _GGSL("Name 1 is of type %s:\n"),
                                      gss_l_name_types[type1]));
    GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(2, (globus_i_gsi_gssapi_debug_fstream,
                                      _GGSL("Name 2 is of type %s:\n"),
                                      gss_l_name_types[type2]));

    /* Normalize order of name1 and name2 so we can have fewer comparisons
     * below
     */
    if (type1 > type2)
    {
        int tmptype = type1;
        gss_name_desc * tmpname = name1;

        type1 = type2;
        name1 = name2;

        type2 = tmptype;
        name2 = tmpname;
    }

    /* Choose comparison function based on name types for name1_P and name2_P */
    if (type1 == GSS_I_COMPARE_NT_ANONYMOUS ||
        type2 == GSS_I_COMPARE_NT_ANONYMOUS)
    {
        if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_RFC2818
            && type1 == type2)
        {
            *name_equal = GSS_NAMES_EQUAL;
        }
        else
        {
            /* RFC 2743: If either name presented to GSS_Compare_name() denotes
             * an anonymous principal, GSS_Compare_name() shall indicate FALSE
             */
            *name_equal = GSS_NAMES_NOT_EQUAL;
        }
        goto exit;
    }
    else if (type1 == GSS_I_COMPARE_NT_X509 &&
             type2 == GSS_I_COMPARE_NT_X509)
    {
        major_status = gss_l_compare_x509_to_x509(
                minor_status, name1, name2, name_equal);
    }
    else if (type1 == GSS_I_COMPARE_NT_X509 &&
             type2 == GSS_I_COMPARE_NT_NO_OID)
    {
        major_status = gss_l_compare_x509_to_default(
                minor_status, name1, name2, name_equal);
    }
    else if (type1 == GSS_I_COMPARE_NT_X509 &&
             type2 == GSS_I_COMPARE_NT_HOSTBASED_SERVICE)
    {
        major_status = gss_l_compare_x509_to_hostbased_service(
                minor_status, name1, name2, name_equal);
    }
    else if (type1 == GSS_I_COMPARE_NT_X509 &&
             type2 == GSS_I_COMPARE_NT_HOST_IP)
    {
        major_status = gss_l_compare_x509_to_host_ip(
                minor_status, name1, name2, name_equal);
    }
    else if (type1 == GSS_I_COMPARE_NT_NO_OID &&
             type2 == GSS_I_COMPARE_NT_NO_OID)
    {
        major_status = gss_l_compare_default_to_default(
                minor_status, name1, name2, name_equal);
    }
    else if (type1 == GSS_I_COMPARE_NT_NO_OID &&
             type2 == GSS_I_COMPARE_NT_HOSTBASED_SERVICE)
    {
        major_status = gss_l_compare_default_to_hostbased_service(
                minor_status, name1, name2, name_equal);
    }
    else if (type1 == GSS_I_COMPARE_NT_NO_OID &&
             type2 == GSS_I_COMPARE_NT_HOST_IP)
    {
        major_status = gss_l_compare_default_to_host_ip(
                minor_status, name1, name2, name_equal);
    }
    else if (type1 == GSS_I_COMPARE_NT_HOSTBASED_SERVICE &&
             type2 == GSS_I_COMPARE_NT_HOSTBASED_SERVICE)
    {
        major_status = gss_l_compare_hostbased_service_to_hostbased_service(
                minor_status, name1, name2, name_equal);
    }
    else if (type1 == GSS_I_COMPARE_NT_HOSTBASED_SERVICE &&
             type2 == GSS_I_COMPARE_NT_HOST_IP)
    {
        major_status = gss_l_compare_hostbased_service_to_host_ip(
                minor_status, name1, name2, name_equal);
    }
    else if (type1 == GSS_I_COMPARE_NT_HOST_IP &&
             type2 == GSS_I_COMPARE_NT_HOST_IP)
    {
        major_status = gss_l_compare_host_ip_to_host_ip(
                minor_status, name1, name2, name_equal);
    }
    else
    {
        globus_assert_string(0, "Unsupported gss_name_t comparison\n");
    }

 exit:
    GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
        2, (globus_i_gsi_gssapi_debug_fstream, "Compared %d \n", *name_equal));

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;

}
/* gss_compare_name */

static
OM_uint32
gss_l_compare_x509_to_x509(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal)
{
    int                                 i1, i2;
    int                                 name_count1, name_count2;
    GENERAL_NAME                        *gn1, *gn2;
    char                                *ns1, *ns2;
    globus_bool_t                       dns_alt_name_found1 = GLOBUS_FALSE;
    globus_bool_t                       dns_alt_name_found2 = GLOBUS_FALSE;
    OM_uint32                           major_status = GSS_S_COMPLETE;

    name_count1 = name1->subjectAltNames
        ? sk_GENERAL_NAME_num(name1->subjectAltNames)
        : 0;
    name_count2 = name1->subjectAltNames
        ? sk_GENERAL_NAME_num(name2->subjectAltNames)
        : 0;

    if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_GT2)
    {
        /* First compare subjectAltName/dNSName with wildcards, and
         * subjectAltName/dNSName with SubjectName
         */
        for (i1 = 0; i1 < name_count1; i1++)
        {
            gn1 = sk_GENERAL_NAME_value(name1->subjectAltNames, i1);
            if (gn1->type == GEN_DNS)
            {
                dns_alt_name_found1 = GLOBUS_TRUE;

                ns1 = (char *) ASN1_STRING_get0_data(gn1->d.dNSName);

                for (i2 = 0; i2 < name_count2; i2++)
                {
                    gn2 = sk_GENERAL_NAME_value(name2->subjectAltNames, i2);

                    if (gn2->type == GEN_DNS)
                    {
                        ns2 = (char *) ASN1_STRING_get0_data(gn2->d.dNSName);
                        dns_alt_name_found2 = GLOBUS_TRUE;

                        major_status = gss_l_compare_hostnames_with_wildcards(
                                minor_status,
                                ns1, GSS_I_WILDCARD_RFC2818,
                                ns2, GSS_I_WILDCARD_RFC2818,
                                name_equal);
                        if (*name_equal == GSS_NAMES_EQUAL ||
                            GSS_ERROR(major_status))
                        {
                            return major_status;
                        }
                    }
                }

                if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_RFC2818 ||
                    !dns_alt_name_found2)
                {
                    if (name2->x509n_oneline != NULL)
                    {
                        ns2 = name2->x509n_oneline;
                        major_status = gss_l_compare_hostnames_with_wildcards(
                                minor_status,
                                ns1, GSS_I_WILDCARD_RFC2818,
                                ns2, GSS_I_WILDCARD_NONE,
                                name_equal);
                        if (*name_equal == GSS_NAMES_EQUAL || GSS_ERROR(major_status))
                        {
                            return major_status;
                        }
                    }
                }
            }
        }

        if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_RFC2818 ||
            !dns_alt_name_found1)
        {
            if (name1->host_name != NULL)
            {
                ns1 = name1->x509n_oneline;

                for (i2 = 0; i2 < name_count2; i2++)
                {
                    gn2 = sk_GENERAL_NAME_value(name2->subjectAltNames, i2);

                    if (gn2->type == GEN_DNS)
                    {
                        ns2 = (char *) ASN1_STRING_get0_data(gn2->d.dNSName);

                        major_status = gss_l_compare_hostnames_with_wildcards(
                                minor_status,
                                ns1, GSS_I_WILDCARD_NONE,
                                ns2, GSS_I_WILDCARD_RFC2818,
                                name_equal);
                        if (*name_equal == GSS_NAMES_EQUAL || GSS_ERROR(major_status))
                        {
                            return major_status;
                        }
                    }
                }
            }
        }

        /* Compare subjectAltNames/iPAddress */
        for (i1 = 0; i1 < name_count1; i1++)
        {
            gn1 = sk_GENERAL_NAME_value(name1->subjectAltNames, i1);
            if (gn1->type == GEN_IPADD)
            {
                for (i2 = 0; i2 < name_count2; i2++)
                {
                    gn2 = sk_GENERAL_NAME_value(name2->subjectAltNames, i2);

                    if (gn2->type == GEN_IPADD)
                    {
                        if (ASN1_OCTET_STRING_cmp(
                                gn1->d.iPAddress, gn2->d.iPAddress) == 0)
                        {
                            *name_equal = GSS_NAMES_EQUAL;
                            return GSS_S_COMPLETE;
                        }
                    }
                }
            }
        }
    }

    if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_RFC2818 ||
        ((!dns_alt_name_found1) || (!dns_alt_name_found2)))
    {
        /* Compare SubjectName */
        if (name1->x509n_oneline != NULL && name2->x509n_oneline != NULL)
        {
            if (strcmp(name1->x509n_oneline, name2->x509n_oneline) == 0)
            {
                *name_equal = GSS_NAMES_EQUAL;
                return GSS_S_COMPLETE;
            }
        }
    }

    *name_equal = GSS_NAMES_NOT_EQUAL;
    return major_status;
}
/* gss_l_compare_x509_to_x509() */

static
OM_uint32
gss_l_compare_x509_to_default(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal)
{
    char                                *ns1, *ns2;

    ns1 = name1->x509n_oneline;
    ns2 = name2->x509n_oneline;

    if (strcmp(ns1, ns2) == 0)
    {
        *name_equal = GSS_NAMES_EQUAL;
    }
    else
    {
        *name_equal = GSS_NAMES_NOT_EQUAL;
    }
    return GSS_S_COMPLETE;
}
/* gss_l_compare_x509_to_default() */

static
OM_uint32
gss_l_compare_x509_to_hostbased_service(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    int                                 i1;
    int                                 name_count1;
    globus_bool_t                       dns_alt_name_found = GLOBUS_FALSE;
    GENERAL_NAME                        *gn1;
    char                                *ns1, *ns2;

    *name_equal = GSS_NAMES_NOT_EQUAL;

    name_count1 = name1->subjectAltNames
        ? sk_GENERAL_NAME_num(name1->subjectAltNames)
        : 0;

    ns2 = name2->host_name;


    /* From RFC 2818, section 3.1:
     *
     * If a subjectAltName extension of type dNSName is present, that MUST
     * be used as the identity. Otherwise, the (most specific) Common Name
     * field in the Subject field of the certificate MUST be used. Although
     * the use of the Common Name is existing practice, it is deprecated and
     * Certification Authorities are encouraged to use the dNSName instead.
     */
    if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_GT2)
    {
        /* Ignore subjectAltName in strict GT2-compatibility mode */
        for (i1 = 0; i1 < name_count1; i1++)
        {
            gn1 = sk_GENERAL_NAME_value(name1->subjectAltNames, i1);
            if (gn1->type == GEN_DNS)
            {
                ns1 = (char *) ASN1_STRING_get0_data(gn1->d.dNSName);

                /* In strict RFC mode, we're going to require the service
                 * portion of the hostbased service name to be "host" (the
                 * default).
                 */
                if (gss_i_name_compatibility_mode == GSS_I_COMPATIBILITY_STRICT_RFC2818)
                {
                    if (strcasecmp(name2->service_name, "host") != 0)
                    {
                        *name_equal = GSS_NAMES_NOT_EQUAL;

                        return major_status;
                    }
                }
                major_status = gss_l_compare_hostnames_with_wildcards(
                        minor_status,
                        ns1, GSS_I_WILDCARD_RFC2818,
                        ns2, GSS_I_WILDCARD_GT2,
                        name_equal);
                if (*name_equal == GSS_NAMES_EQUAL || GSS_ERROR(major_status))
                {
                    return major_status;
                }
                dns_alt_name_found = GLOBUS_TRUE;
            }
        }
    }

    if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_RFC2818 ||
        dns_alt_name_found == GLOBUS_FALSE)
    {
        /* In strict RFC mode, we're going to require the service
         * portion of the hostbased service name to be "host" (the
         * default).
         */
        if (gss_i_name_compatibility_mode == GSS_I_COMPATIBILITY_STRICT_RFC2818)
        {
            if (strcasecmp(name1->service_name, name2->service_name) != 0)
            {
                *name_equal = GSS_NAMES_NOT_EQUAL;

                return major_status;
            }
        }
        if (name1->host_name != NULL && name2->host_name)
        {
            ns1 = name1->host_name;
            major_status = gss_l_compare_hostnames_with_wildcards(
                    minor_status,
                    ns1, GSS_I_WILDCARD_GT2,
                    ns2, GSS_I_WILDCARD_GT2,
                    name_equal);
            if (*name_equal == GSS_NAMES_EQUAL || GSS_ERROR(major_status))
            {
                return major_status;
            }
        }
    }

    return major_status;
}
/* gss_l_compare_x509_to_hostbased_service() */

static
OM_uint32
gss_l_compare_x509_to_host_ip(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal)
{
    int                                 i1;
    int                                 name_count1;
    GENERAL_NAME                        *gn1;
    char                                *ns1, *ns2;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    globus_bool_t                       dns_alt_name_found = GLOBUS_FALSE;

    *name_equal = GSS_NAMES_NOT_EQUAL;

    name_count1 = name1->subjectAltNames
        ? sk_GENERAL_NAME_num(name1->subjectAltNames)
        : 0;

    ns2 = name2->host_name;

    if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_GT2)
    {
        /* Compare hostname against subjectAltName/dNSName */
        for (i1 = 0; i1 < name_count1; i1++)
        {
            gn1 = sk_GENERAL_NAME_value(name1->subjectAltNames, i1);
            if (gn1->type == GEN_DNS)
            {
                ns1 = (char *) ASN1_STRING_get0_data(gn1->d.dNSName);

                major_status = gss_l_compare_hostnames_with_wildcards(
                        minor_status,
                        ns1, GSS_I_WILDCARD_RFC2818,
                        ns2, GSS_I_WILDCARD_GT2,
                        name_equal);
                if (*name_equal == GSS_NAMES_EQUAL || GSS_ERROR(major_status))
                {
                    return major_status;
                }
                dns_alt_name_found = GLOBUS_TRUE;
            }
        }

        /* Compare ip address against subjectAltName/iPAddress */
        for (i1 = 0; i1 < name_count1; i1++)
        {
            gn1 = sk_GENERAL_NAME_value(name1->subjectAltNames, i1);
            if (gn1->type == GEN_IPADD)
            {
                int ip_as_ints[16], j;
                int len = ASN1_STRING_length(gn1->d.iPAddress);
                ns1 = (char *) ASN1_STRING_get0_data(gn1->d.iPAddress);

                for (j = 0; j < len; j++)
                {
                    ip_as_ints[j] = ((unsigned char *) ns1)[j];
                }
                ns1 = globus_libc_ints_to_contact_string(
                    ip_as_ints,
                    len,
                    0);

                if (ns1 == NULL)
                {
                    GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                    major_status = GSS_S_FAILURE;

                    return major_status;
                }

                if (name2->ip_address != NULL &&
                    strcmp(ns1, name2->ip_address) == 0)
                {
                    *name_equal = GSS_NAMES_EQUAL;
                    free(ns1);
                    return GSS_S_COMPLETE;
                }
                free(ns1);
            }
        }
    }

    /* Compare hostname to host name from CN of SubjectName */
    if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_RFC2818 ||
        !dns_alt_name_found)
    {
        if (name1->host_name != NULL)
        {
            ns1 = name1->host_name;

            major_status = gss_l_compare_hostnames_with_wildcards(
                    minor_status,
                    ns1, GSS_I_WILDCARD_NONE,
                    ns2, GSS_I_WILDCARD_GT2,
                    name_equal);
            if (*name_equal == GSS_NAMES_EQUAL || GSS_ERROR(major_status))
            {
                return major_status;
            }

            if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_RFC2818
                && name2->ip_name != NULL)
            {
                ns2 = name2->ip_name;

                major_status = gss_l_compare_hostnames_with_wildcards(
                        minor_status,
                        ns1, GSS_I_WILDCARD_NONE,
                        ns2, GSS_I_WILDCARD_GT2,
                        name_equal);
            }
        }
    }

    return major_status;
}
/* gss_l_compare_x509_to_host_ip() */

static
OM_uint32
gss_l_compare_default_to_default(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal)
{
    if (strcmp(name1->x509n_oneline, name2->x509n_oneline) == 0)
    {
        *name_equal = GSS_NAMES_EQUAL;
    }
    else
    {
        *name_equal = GSS_NAMES_NOT_EQUAL;
    }
    return GSS_S_COMPLETE;
}
/* gss_l_compare_default_to_default() */

static
OM_uint32
gss_l_compare_default_to_hostbased_service(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal)
{
    char                                *ns1, *ns2;
    OM_uint32                           major_status = GSS_S_COMPLETE;

    *minor_status = 0;

    ns1 = name1->host_name;
    ns2 = name2->host_name;

    if (gss_i_name_compatibility_mode == GSS_I_COMPATIBILITY_STRICT_RFC2818)
    {
        /* In strict RFC mode, we're going to require the service
         * portion of the hostbased service name to match that of the
         * subject name (defaulting to host if the subject doesn't contain
         * a service).
         */
        if (name1->service_name && name2->service_name)
        {
            if (strcasecmp(name1->service_name, name2->service_name) != 0)
            {
                *name_equal = GSS_NAMES_NOT_EQUAL;

                return major_status;
            }
        }
        else
        {
            if (name2->service_name &&
                strcasecmp(name2->service_name, "host") != 0)
            {
                *name_equal = GSS_NAMES_NOT_EQUAL;

                return major_status;
            }
        }
    }

    major_status = gss_l_compare_hostnames_with_wildcards(
            minor_status,
            ns1, GSS_I_WILDCARD_GT2,
            ns2, GSS_I_WILDCARD_GT2,
            name_equal);

    return major_status;
}
/* gss_l_compare_default_to_hostbased_service() */

static
OM_uint32
gss_l_compare_default_to_host_ip(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal)
{
    char                                *ns1, *ns2;
    OM_uint32                           major_status = GSS_S_COMPLETE;

    *minor_status = 0;

    ns1 = name1->host_name;
    ns2 = name2->host_name;

    major_status = gss_l_compare_hostnames_with_wildcards(
            minor_status,
            ns1, GSS_I_WILDCARD_NONE,
            ns2, GSS_I_WILDCARD_NONE,
            name_equal);
    if (*name_equal == GSS_NAMES_EQUAL || GSS_ERROR(major_status))
    {
        return major_status;
    }

    if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_RFC2818 &&
        name2->ip_name != NULL)
    {
        ns2 = name2->ip_name;
        major_status = gss_l_compare_hostnames_with_wildcards(
                minor_status,
                ns1, GSS_I_WILDCARD_NONE,
                ns2, GSS_I_WILDCARD_NONE,
                name_equal);
    }
    return major_status;
}
/* gss_l_compare_default_to_host_ip() */

static
OM_uint32
gss_l_compare_hostbased_service_to_hostbased_service(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal)
{
    OM_uint32                           major_status;
    char                                *ns1, *ns2;

    major_status = GSS_S_COMPLETE;

    ns1 = name1->host_name;
    ns2 = name2->host_name;

    /* In strict mode, we compare service names, for backward compatibility,
     * we ignore them.
     */
    if (gss_i_name_compatibility_mode == GSS_I_COMPATIBILITY_STRICT_RFC2818 &&
        (strcasecmp(name1->service_name, name2->service_name) != 0))
    {
            *name_equal = GSS_NAMES_NOT_EQUAL;
            return major_status;
    }

    major_status = gss_l_compare_hostnames_with_wildcards(
            minor_status,
            ns1, GSS_I_WILDCARD_GT2,
            ns2, GSS_I_WILDCARD_GT2,
            name_equal);

    return major_status;
}
/* gss_l_compare_hostbased_service_to_hostbased_service() */

static
OM_uint32
gss_l_compare_hostbased_service_to_host_ip(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal)
{
    OM_uint32                           major_status;
    char                                *ns1, *ns2;

    major_status = GSS_S_COMPLETE;

    ns1 = name1->host_name;
    ns2 = name2->host_name;

    if (gss_i_name_compatibility_mode == GSS_I_COMPATIBILITY_STRICT_RFC2818 &&
        strcasecmp(name1->service_name, "host") != 0)
    {
        *name_equal = GSS_NAMES_NOT_EQUAL;

        return major_status;
    }

    major_status = gss_l_compare_hostnames_with_wildcards(
            minor_status,
            ns1, GSS_I_WILDCARD_GT2,
            ns2, GSS_I_WILDCARD_NONE,
            name_equal);

    if (*name_equal == GSS_NAMES_EQUAL || GSS_ERROR(major_status))
    {
        return major_status;
    }

    if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_RFC2818 &&
        name2->ip_name != NULL)
    {
        /* Ignore reverse ip lookup in strict rfc2818 mode */
        ns2 = name2->ip_name;
        major_status = gss_l_compare_hostnames_with_wildcards(
                minor_status,
                ns1, GSS_I_WILDCARD_GT2,
                ns2, GSS_I_WILDCARD_NONE,
                name_equal);
    }

    return major_status;
}
/* gss_l_compare_hostbased_service_to_hostbased_service() */

static
OM_uint32
gss_l_compare_host_ip_to_host_ip(
    OM_uint32 *                         minor_status,
    const gss_name_desc*                name1,
    const gss_name_desc*                name2,
    int *                               name_equal)
{
    OM_uint32                           major_status;
    char                                *ns1, *ns2;

    *name_equal = GSS_NAMES_NOT_EQUAL;

    ns1 = name1->host_name;
    ns2 = name2->host_name;
    major_status = gss_l_compare_hostnames_with_wildcards(
            minor_status,
            ns1, GSS_I_WILDCARD_GT2,
            ns2, GSS_I_WILDCARD_GT2,
            name_equal);
    if (*name_equal == GSS_NAMES_EQUAL || GSS_ERROR(major_status))
    {
        goto out;
    }
    else if (name1->ip_address != NULL &&
             name2->ip_address != NULL &&
             strcmp(name1->ip_address, name2->ip_address) == 0)
    {
        *name_equal = GSS_NAMES_EQUAL;
    }
    else if (gss_i_name_compatibility_mode != GSS_I_COMPATIBILITY_STRICT_RFC2818)
    {
        ns1 = name1->ip_name;
        major_status = gss_l_compare_hostnames_with_wildcards(
                minor_status,
                ns1, GSS_I_WILDCARD_GT2,
                ns2, GSS_I_WILDCARD_GT2,
                name_equal);
        if (*name_equal == GSS_NAMES_EQUAL || GSS_ERROR(major_status))
        {
            goto out;
        }
        ns1 = name1->host_name;
        ns2 = name2->ip_name;
        major_status = gss_l_compare_hostnames_with_wildcards(
                minor_status,
                ns1, GSS_I_WILDCARD_GT2,
                ns2, GSS_I_WILDCARD_GT2,
                name_equal);
        if (*name_equal == GSS_NAMES_EQUAL || GSS_ERROR(major_status))
        {
            goto out;
        }
    }

out:
    return major_status;
}
/* gss_l_compare_host_ip_to_host_ip() */

static
OM_uint32
gss_l_compare_hostnames_with_wildcards(
    OM_uint32 *                         minor_status,
    const char *                        host1,
    gss_l_wildcard_type_t               wildcards1,
    const char *                        host2,
    gss_l_wildcard_type_t               wildcards2,
    int *                               name_equal)
{
    char                                *host_cpy1 = NULL, *host_cpy2 = NULL;
    char                                *tok1, *tok2;
    char                                *run1, *run2;
    int                                 i;
    globus_bool_t                       first_token = GLOBUS_TRUE;
    OM_uint32                           major_status = GSS_S_COMPLETE;

    major_status = GSS_S_COMPLETE;

    if (host1 == NULL || host2 == NULL)
    {
        goto nomatch;
    }

    /* Normalize to lowercase */
    host_cpy1 = malloc(strlen(host1)+1);
    if (host_cpy1 == NULL)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;
        goto out;
    }
    for (i = 0; host1[i] != '\0'; i++)
    {
        host_cpy1[i] = tolower(host1[i]);
    }
    host_cpy1[i] = 0;

    host_cpy2 = malloc(strlen(host2)+1);
    if (host_cpy2 == NULL)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;

        goto free_name1_out;
    }
    for (i = 0; host2[i] != '\0'; i++)
    {
        host_cpy2[i] = tolower(host2[i]);
    }
    host_cpy2[i] = 0;

    /* current token separator (modified by gss_l_strsep below) */
    run1 = host_cpy1;
    run2 = host_cpy2;

    /* RFC 2818 is a little unclear about wildcard matching. What this
     * implements is a single wildcard per name component (separated by .) at
     * the end of the token. (so f*.com is ok but *f.com is not)
     *
     * GT2 has a different concept of wildcards --- if the 1st name component
     * contains a -, any data after it (and the -) is ignored in the
     * comparison (so foo-1.com matches foo.com but foo-1.com does not match
     * foo-2.com).
     */

    switch (gss_i_name_compatibility_mode)
    {
        case GSS_I_COMPATIBILITY_HYBRID:
            break;
        case GSS_I_COMPATIBILITY_STRICT_GT2:
            if (wildcards1 == GSS_I_WILDCARD_RFC2818)
            {
                wildcards1 = GSS_I_WILDCARD_NONE;
            }
            if (wildcards2 == GSS_I_WILDCARD_RFC2818)
            {
                wildcards2 = GSS_I_WILDCARD_NONE;
            }
            break;
        case GSS_I_COMPATIBILITY_STRICT_RFC2818:
            if (wildcards1 == GSS_I_WILDCARD_GT2)
            {
                wildcards1 = GSS_I_WILDCARD_NONE;
            }
            if (wildcards2 == GSS_I_WILDCARD_GT2)
            {
                wildcards2 = GSS_I_WILDCARD_NONE;
            }
            break;
    }

    for (tok1 = gss_l_strsep(&run1, "."), tok2 = gss_l_strsep(&run2, ".");
         tok1 != NULL && tok2 != NULL;
         tok1 = gss_l_strsep(&run1, "."), tok2 = gss_l_strsep(&run2, "."))
    {
        /* Match non-wildcard bits */
        while (*tok1 && *tok2 && *tok1 == *tok2)
        {
            if (wildcards1 == GSS_I_WILDCARD_RFC2818 && *tok1 == '*')
            {
                break;
            }
            if (wildcards2 == GSS_I_WILDCARD_RFC2818 && *tok2 == '*')
            {
                break;
            }

            tok1++;
            tok2++;
        }

        /* If anything remains, check for wildcards */
        if (wildcards1 == GSS_I_WILDCARD_RFC2818 && *tok1 == '*')
        {
            tok1++;

            if (*tok1 != '\0')
            {
                goto nomatch;
            }
        }
        else if (wildcards2 == GSS_I_WILDCARD_RFC2818 && *tok2 == '*')
        {
            tok2++;

            if (*tok2 != 0)
            {
                goto nomatch;
            }
        }
        else if (wildcards1 == GSS_I_WILDCARD_GT2 && *tok1 == '-'
            && first_token)
        {
            if (*tok2 != '\0')
            {
                goto nomatch;
            }

        }
        else if (wildcards2 == GSS_I_WILDCARD_GT2 && *tok2 == '-'
            && first_token)
        {
            if (*tok1 != '\0')
            {
                goto nomatch;
            }
        }
        else if (*tok1 || *tok2)
        {
            goto nomatch;
        }
        first_token = GLOBUS_FALSE;
    }

    if (tok1 != NULL || tok2 != NULL)
    {
nomatch:
        *name_equal = GSS_NAMES_NOT_EQUAL;
    }
    else
    {
        *name_equal = GSS_NAMES_EQUAL;
    }

    free(host_cpy2);
free_name1_out:
    free(host_cpy1);
out:
    return major_status;
}
/* gss_l_compare_hostnames_with_wildcards() */

static
char *
gss_l_strsep(char **stringp, const char * delim)
{
    char * input = *stringp;
    size_t span;

    if (input == NULL)
    {
        return NULL;
    }
    span = strcspn(input, delim);
    if (input[span] == '\0')
    {
        *stringp = NULL;
    }
    else
    {
        input[span] = 0;
        *stringp = input + span + 1;
    }

    return input;
}

/* Convert oid struct to integer
 *
 * @param oid
 *     OID to convert to a gss_l_compare_type_t (or -1 if the
 *     name type is not supported.
 */
static
int
gss_l_get_oid_type(
    gss_OID                             oid)
{
    if (g_OID_equal(oid, GSS_C_NT_ANONYMOUS))
    {
        return GSS_I_COMPARE_NT_ANONYMOUS;
    }
    else if (g_OID_equal(oid, GLOBUS_GSS_C_NT_X509))
    {
        return GSS_I_COMPARE_NT_X509;
    }
    else if (g_OID_equal(oid, GSS_C_NO_OID) ||
             g_OID_equal(oid, GSS_C_NT_USER_NAME))
    {
        return GSS_I_COMPARE_NT_NO_OID;
    }
    else if (g_OID_equal(oid, GSS_C_NT_HOSTBASED_SERVICE))
    {
        return GSS_I_COMPARE_NT_HOSTBASED_SERVICE;
    }
    else if (g_OID_equal(oid, GLOBUS_GSS_C_NT_HOST_IP))
    {
        return GSS_I_COMPARE_NT_HOST_IP;
    }
    else
    {
        return -1;
    }
}
