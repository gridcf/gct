/*
 * Copyright 1999-2014 University of Chicago
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
 * @file globus_gsi_cert_utils.c
 * @brief Globus GSI Cert Utils Library
 * @author Sam Lang
 * @author Sam Meder
 */
#endif

#include "globus_i_gsi_cert_utils.h"
#include "globus_openssl.h"
#include "openssl/asn1.h"
#include "openssl/x509.h"
#include "openssl/x509v3.h"
#include "openssl/pem.h"
#include "version.h"
#include "globus_i_gsi_cert_utils_config.h"
#include <ctype.h>

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

#define LIMITED_PROXY_OID               "1.3.6.1.4.1.3536.1.1.1.9"
#define PROXYCERTINFO_OLD_OID           "1.3.6.1.4.1.3536.1.222"

int                               globus_i_gsi_cert_utils_debug_level = 0;
FILE *                            globus_i_gsi_cert_utils_debug_fstream = NULL;

static int globus_l_gsi_cert_utils_activate(void);
static int globus_l_gsi_cert_utils_deactivate(void);

/**
 * Module descriptor static initializer.
 */
globus_module_descriptor_t globus_i_gsi_cert_utils_module =
{
    "globus_cert_utils",
    globus_l_gsi_cert_utils_activate,
    globus_l_gsi_cert_utils_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

/**
 * Module activation
 */
static
int
globus_l_gsi_cert_utils_activate(void)
{
    int                                 result = (int) GLOBUS_SUCCESS;
    char *                              tmp_string;
    static char *                       _function_name_ =
        "globus_l_gsi_cert_utils_activate";

    tmp_string = globus_module_getenv("GLOBUS_GSI_CERT_UTILS_DEBUG_LEVEL");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_gsi_cert_utils_debug_level = atoi(tmp_string);
        
        if(globus_i_gsi_cert_utils_debug_level < 0)
        {
            globus_i_gsi_cert_utils_debug_level = 0;
        }
    }

    tmp_string = globus_module_getenv("GLOBUS_GSI_CERT_UTILS_DEBUG_FILE");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_gsi_cert_utils_debug_fstream = fopen(tmp_string, "w");
        if(globus_i_gsi_cert_utils_debug_fstream == NULL)
        {
            result = (int) GLOBUS_FAILURE;
            goto exit;
        }
    }
    else
    {
        /* if the env. var. isn't set, use stderr */
        globus_i_gsi_cert_utils_debug_fstream = stderr;
    }

    GLOBUS_I_GSI_CERT_UTILS_DEBUG_ENTER;

    result = globus_module_activate(GLOBUS_OPENSSL_MODULE);
    if (result != GLOBUS_SUCCESS)
    {
        goto activate_openssl_fail;
    }
    
    result = globus_module_activate(GLOBUS_GSI_OPENSSL_ERROR_MODULE);
    if (result != GLOBUS_SUCCESS)
    {
        goto activate_openssl_error_fail;
    }

    GLOBUS_I_GSI_CERT_UTILS_DEBUG_EXIT;
    return GLOBUS_SUCCESS;

activate_openssl_error_fail:
    globus_module_deactivate(GLOBUS_OPENSSL_MODULE);
activate_openssl_fail:
    if (globus_i_gsi_cert_utils_debug_fstream != NULL &&
        globus_i_gsi_cert_utils_debug_fstream != stderr)
    {
        fclose(globus_i_gsi_cert_utils_debug_fstream);
        globus_i_gsi_cert_utils_debug_fstream = NULL;
    }
 exit:
    return result;
}
/* globus_l_gsi_cert_utils_activate() */

/**
 * Module deactivation
 *
 */
static
int
globus_l_gsi_cert_utils_deactivate(void)
{
    int                                 res1 = GLOBUS_SUCCESS,
                                        res2 = GLOBUS_SUCCESS;
    static char *                       _function_name_ =
        "globus_l_gsi_cert_utils_deactivate";

    GLOBUS_I_GSI_CERT_UTILS_DEBUG_ENTER;

    res1 = globus_module_deactivate(GLOBUS_GSI_OPENSSL_ERROR_MODULE);

    res2 = globus_module_deactivate(GLOBUS_OPENSSL_MODULE);

    GLOBUS_I_GSI_CERT_UTILS_DEBUG_EXIT;

    /*
     * This must have been set to non-NULL fopen result or stderr in activate
     */
    globus_assert(globus_i_gsi_cert_utils_debug_fstream != NULL);
    if (globus_i_gsi_cert_utils_debug_fstream != stderr)
    {
        fclose(globus_i_gsi_cert_utils_debug_fstream);
    }

    if (res1 != GLOBUS_SUCCESS)
    {
        return res1;
    }
    else
    {
        return res2;
    }
}
/* globus_l_gsi_cert_utils_deactivate() */

#endif

/**
 * @brief Convert ASN1_TIME to time_t
 * @ingroup globus_gsi_cert_utils
 * @details
 * Convert a ASN1_TIME structure to a time_t
 *
 * @param ctm
 *        The ASN1_TIME to convert
 * @param newtime
 *        The converted time
 *
 * @return
 *        GLOBUS_SUCCESS or GLOBUS_FAILURE on error
 */
globus_result_t
globus_gsi_cert_utils_make_time(
    const ASN1_TIME *                   ctm,
    time_t *                            newtime)
{
    int                                 pday, psec;
    globus_result_t                     result;
    static char *                       _function_name_ =
        "globus_gsi_cert_utils_make_time";

    GLOBUS_I_GSI_CERT_UTILS_DEBUG_ENTER;

    if (ASN1_TIME_diff(&pday, &psec, NULL, ctm))
    {
        *newtime = time(NULL)+pday*86400L+psec;
        result = GLOBUS_SUCCESS;
    }
    else
    {
        *newtime = 0;
        result = GLOBUS_FAILURE;
    }

    GLOBUS_I_GSI_CERT_UTILS_DEBUG_EXIT;

    return result;
}

/**
 * @brief Get the X509 certificate type
 * @ingroup globus_gsi_cert_utils
 * @details
 * Determine the type of the given X509 certificate For the list of possible
 * values returned, see globus_gsi_cert_utils_cert_type_t.
 *
 * @param cert
 *        The X509 certificate 
 * @param type
 *        The returned X509 certificate type
 *
 * @return
 *        GLOBUS_SUCCESS or an error captured in a globus_result_t
 */
globus_result_t
globus_gsi_cert_utils_get_cert_type(
    X509 *                              cert,
    globus_gsi_cert_utils_cert_type_t * type)
{
    X509_NAME *                         subject = NULL;
    X509_NAME *                         name = NULL;
    X509_NAME_ENTRY *                   ne = NULL;
    X509_NAME_ENTRY *                   new_ne = NULL;
    X509_EXTENSION *                    pci_ext = NULL;
    ASN1_STRING *                       data = NULL;
    PROXY_CERT_INFO_EXTENSION *         pci = NULL;
    PROXY_POLICY *                      policy = NULL;
    ASN1_OBJECT *                       policy_lang = NULL;
    int                                 policy_nid;
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 index = -1;
    int                                 critical;
    BASIC_CONSTRAINTS *                 x509v3_bc = NULL;
    static char *                       _function_name_ =
        "globus_gsi_cert_utils_get_cert_type";
    
    GLOBUS_I_GSI_CERT_UTILS_DEBUG_ENTER;

    /* assume it is a EEC if nothing else matches */
    
    *type = GLOBUS_GSI_CERT_UTILS_TYPE_EEC;
    
    if((x509v3_bc = X509_get_ext_d2i(cert,
                                     NID_basic_constraints,
                                     &critical,
                                     &index)) && x509v3_bc->ca)
    {
        *type = GLOBUS_GSI_CERT_UTILS_TYPE_CA;
        goto exit;
    }
    
    subject = X509_get_subject_name(cert);
    
    if((ne = X509_NAME_get_entry(subject, X509_NAME_entry_count(subject)-1))
       == NULL)
    {
        GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CERT_UTILS_ERROR_GETTING_NAME_ENTRY_OF_SUBJECT,
            (_CUSL("Can't get X509 name entry from subject")));
        goto exit;
    }

    if (!OBJ_cmp(X509_NAME_ENTRY_get_object(ne), OBJ_nid2obj(NID_commonName)))
    {
        /* the name entry is of the type: common name */
        data = X509_NAME_ENTRY_get_data(ne);
        if(data->length == 5 && !memcmp(data->data,"proxy",5))
        {
            *type = GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2_PROXY;
        }
        else if(data->length == 13 && !memcmp(data->data,"limited proxy",13))
        {
            *type = GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2_LIMITED_PROXY;
        }
        else if((index = X509_get_ext_by_NID(cert,
                                             NID_proxyCertInfo,
                                             -1)) != -1  &&
                (pci_ext = X509_get_ext(cert,index)) &&
                X509_EXTENSION_get_critical(pci_ext))
        {
            if((pci = X509V3_EXT_d2i(pci_ext)) == NULL)
            {
                GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_NON_COMPLIANT_PROXY,
                    (_CUSL("Can't convert DER encoded PROXYCERTINFO "
                     "extension to internal form")));
                goto exit;
            }
            
            if((policy = pci->proxyPolicy) == NULL)
            {
                GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_NON_COMPLIANT_PROXY,
                    (_CUSL("Can't get policy from PROXYCERTINFO extension")));
                goto exit;
            }

            if((policy_lang = policy->policyLanguage) == NULL)
            {
                GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_NON_COMPLIANT_PROXY,
                    (_CUSL("Can't get policy language from"
                     " PROXYCERTINFO extension")));
                goto exit;
            }

            policy_nid = OBJ_obj2nid(policy_lang);
            
            if (policy_nid == NID_id_ppl_inheritAll)
            {
                *type = GLOBUS_GSI_CERT_UTILS_TYPE_RFC_IMPERSONATION_PROXY;
            }
            else if (policy_nid == NID_Independent)
            {
                *type = GLOBUS_GSI_CERT_UTILS_TYPE_RFC_INDEPENDENT_PROXY;
            }
            else if(policy_nid == OBJ_txt2nid(LIMITED_PROXY_OID))
            {
                *type = GLOBUS_GSI_CERT_UTILS_TYPE_RFC_LIMITED_PROXY;
            }
            else
            {
                *type = GLOBUS_GSI_CERT_UTILS_TYPE_RFC_RESTRICTED_PROXY;
            }
            
            if(X509_get_ext_by_NID(cert,
                                   NID_proxyCertInfo,
                                   index) != -1)
            { 
                GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_NON_COMPLIANT_PROXY,
                    (_CUSL("Found more than one PCI extension")));
                goto exit;
            }
        }
        else if((index = X509_get_ext_by_NID(cert,
                                             OBJ_txt2nid(PROXYCERTINFO_OLD_OID),
                                             -1)) != -1 &&
                (pci_ext = X509_get_ext(cert,index)) &&
                X509_EXTENSION_get_critical(pci_ext))
        {
            if((pci = X509V3_EXT_d2i(pci_ext)) == NULL)
            {
                GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_NON_COMPLIANT_PROXY,
                    (_CUSL("Can't convert DER encoded PROXYCERTINFO "
                     "extension to internal form")));
                goto exit;
            }
            
            if((policy = pci->proxyPolicy) == NULL)
            {
                GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_NON_COMPLIANT_PROXY,
                    (_CUSL("Can't get policy from PROXYCERTINFO extension")));
                goto exit;
            }

            if((policy_lang = policy->policyLanguage) == NULL)
            {
                GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_NON_COMPLIANT_PROXY,
                    (_CUSL("Can't get policy language from"
                     " PROXYCERTINFO extension")));
                goto exit;
            }

            policy_nid = OBJ_obj2nid(policy_lang);
            
            if (policy_nid == NID_id_ppl_inheritAll)
            {
                *type = GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_IMPERSONATION_PROXY;
            }
            else if (policy_nid == NID_Independent)
            {
                *type = GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_INDEPENDENT_PROXY;
            }
            else if(policy_nid == OBJ_txt2nid(LIMITED_PROXY_OID))
            {
                *type = GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_LIMITED_PROXY;
            }
            else
            {
                *type = GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_RESTRICTED_PROXY;
            }
            
            if(X509_get_ext_by_NID(cert,
                                   OBJ_txt2nid(PROXYCERTINFO_OLD_OID),
                                   index) != -1)
            { 
                GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_NON_COMPLIANT_PROXY,
                    (_CUSL("Found more than one PCI extension")));
                goto exit;
            }
        }

        if(GLOBUS_GSI_CERT_UTILS_IS_PROXY(*type))
        {
            /* its some kind of proxy - now we check if the subject
             * matches the signer, by adding the proxy name entry CN
             * to the signer's subject
             */

            GLOBUS_I_GSI_CERT_UTILS_DEBUG_FPRINTF(
                2, (globus_i_gsi_cert_utils_debug_fstream, 
                    "Subject is %s\n", data->data));

            if((name = X509_NAME_dup(
                       X509_get_issuer_name(cert))) == NULL)
            {
                GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_COPYING_SUBJECT,
                    (_CUSL("Error copying X509_NAME struct")));
                goto exit;
            }
            
            if((new_ne = X509_NAME_ENTRY_create_by_NID(NULL, NID_commonName,
                                                       data->type,
                                                       data->data, -1)) == NULL)
            {
                GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_GETTING_CN_ENTRY,
                    (_CUSL("Error creating X509 name entry of: %s"), data->data));
                goto exit;
            }
            
            if(!X509_NAME_add_entry(name, new_ne, X509_NAME_entry_count(name),0))
            {
                X509_NAME_ENTRY_free(new_ne);
                new_ne = NULL;
                GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_ADDING_CN_TO_SUBJECT,
                    (_CUSL("Error adding name entry with value: %s, to subject"),
                     data->data));
                goto exit;
            }
 
            if(new_ne)
            {
                X509_NAME_ENTRY_free(new_ne);
                new_ne = NULL;
            }
           
            if (X509_NAME_cmp(name,subject))
            {
                /*
                 * Reject this certificate, only the user
                 * may sign the proxy
                 */
                GLOBUS_GSI_CERT_UTILS_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_NON_COMPLIANT_PROXY,
                    (_CUSL("Issuer name + proxy CN entry does not equal subject name")));
                goto exit;
            }

            if(name)
            {
                X509_NAME_free(name);
                name = NULL;
            }
        }
    }

    result = GLOBUS_SUCCESS;

 exit:

    if(x509v3_bc)
    {
        BASIC_CONSTRAINTS_free(x509v3_bc);
    }

    if(new_ne)
    {
        X509_NAME_ENTRY_free(new_ne);
    }

    if(name)
    {
        X509_NAME_free(name);
    }

    if(pci)
    {
        PROXY_CERT_INFO_EXTENSION_free(pci);
    }

    GLOBUS_I_GSI_CERT_UTILS_DEBUG_EXIT;
    return result;
}

/**
 * @brief Get the certificate name
 * @ingroup globus_gsi_cert_utils
 * @details
 * Get the X509_NAME from a subject string.
 * OpenSSL doesn't provide this function, probably because
 * it shouldn't be used.  If you are getting an X509_NAME from
 * just a string, its impossible to verify its integrity.
 *
 * @param subject_string
 *        The subject in the format: "/O=Grid/OU=..."
 * @param length
 *        The length of the subject string
 * @param x509_name
 *        The resulting X509_NAME object
 *
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 */
globus_result_t
globus_gsi_cert_utils_get_x509_name(
    const char *                        subject_string,
    int                                 length,
    X509_NAME *                         x509_name)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char *                              local_copy = NULL;
    char *                              name_entry_str = NULL;
    char *                              name_value_str = NULL;
    char *                              index = NULL;
    char *                              index2 = NULL;
    char *                              uc_index = NULL;
    X509_NAME_ENTRY *                   x509_name_entry = NULL;
    int                                 nid;
    int                                 res;
    static char *                       _function_name_ =
        "globus_gsi_cert_utils_get_x509_name";

    GLOBUS_I_GSI_CERT_UTILS_DEBUG_ENTER;

    local_copy = malloc(length + 1);
    if(local_copy == NULL)
    {
        GLOBUS_GSI_CERT_UTILS_MALLOC_ERROR(result);
        goto exit;
    }

    memcpy(local_copy, subject_string, length);
    local_copy[length] = '\0';

    index = local_copy;
    if (*index == '/')
    {
        /* skip first / */
        name_entry_str = index + 1;                 
        while ((index != NULL) && (*index != '\0'))
        {
            /* point at name = */
            index = strchr(name_entry_str,'=');
            if (index == NULL)
            {
                GLOBUS_GSI_CERT_UTILS_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_UNEXPECTED_FORMAT,
                    (_CUSL("The subject_string cannot be convert to an "
                     "X509_NAME, unexpected format")));
                goto exit;
            }
            /* terminate name string */
            *index = '\0';           

            name_value_str = index + 1;

            /* find next =, then last / */
            index = strchr(name_value_str, '=');   
            if (index != NULL)
            {
                /* for now set = to \0 */
                *index = '\0';	
                    
                /* find last / in  value */
                index2 = strrchr(name_value_str, '/');   

                /* reset = */
                *index = '=';	

                if (index2 != NULL)
                {
                    /* terminate value string */
                    *index2 = '\0'; 
                }
            }

            nid = OBJ_txt2nid(name_entry_str);
            
            if (nid == NID_undef)
            {
                /* 
                 * not found, lets try upper case instead
                 */
                uc_index = name_entry_str;
                while (*uc_index != '\0')
                {
                    *uc_index = toupper(*uc_index);
                    uc_index++;
                }

                nid = OBJ_txt2nid(name_entry_str);
                if (nid == NID_undef)
                {
                    GLOBUS_GSI_CERT_UTILS_ERROR_RESULT(
                        result,
                        GLOBUS_GSI_CERT_UTILS_ERROR_UNEXPECTED_FORMAT,
                        (_CUSL("The name entry: %s is not "
                         "recognized as a valid OID"), name_entry_str));
                    goto exit;
                }
            }

            x509_name_entry = X509_NAME_ENTRY_create_by_NID(
                &x509_name_entry,
                nid,
                V_ASN1_APP_CHOOSE, 
                (unsigned char *) name_value_str,
                -1);

            if (x509_name_entry == NULL)
            {
                GLOBUS_GSI_CERT_UTILS_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_UNEXPECTED_FORMAT,
                    (_CUSL("Error with name entry: %s, with a value of: %s"),
                     name_entry_str, name_value_str));
                goto exit;
            }
            
            res = X509_NAME_add_entry(x509_name, x509_name_entry, 
                                      X509_NAME_entry_count(x509_name), 0);
            if (!res)
            {
                GLOBUS_GSI_CERT_UTILS_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CERT_UTILS_ERROR_UNEXPECTED_FORMAT,
                    (_CUSL("Couldn't add name entry to  X509_NAME object")));
                goto exit;
            }
            
            X509_NAME_ENTRY_free(x509_name_entry);
            x509_name_entry = NULL;

            if (index2 == NULL)
            {
                break;
            }
            name_entry_str = index2 + 1;
        }
    }
    else
    {
        GLOBUS_GSI_CERT_UTILS_ERROR_RESULT(
            result,
            GLOBUS_GSI_CERT_UTILS_ERROR_UNEXPECTED_FORMAT,
            (_CUSL("The X509 name doesn't start with a /")));
        goto exit;
    }
    /* ToDo: Fix memory leak from X509_NAME_oneline call below */
    GLOBUS_I_GSI_CERT_UTILS_DEBUG_PRINT(2, "ORIGINAL SUBJECT STRING: ");
    GLOBUS_I_GSI_CERT_UTILS_DEBUG_FNPRINTF(2, (length, subject_string));
    GLOBUS_I_GSI_CERT_UTILS_DEBUG_FPRINTF(
        2, (globus_i_gsi_cert_utils_debug_fstream,
            "\nGENERATED X509_NAME STRING: %s\n",
            X509_NAME_oneline(x509_name, NULL, 0)));

 exit:

    if(x509_name_entry != NULL)
    {
        X509_NAME_ENTRY_free(x509_name_entry);
    }

    if(local_copy != NULL)
    {
        globus_libc_free(local_copy);
    }

    GLOBUS_I_GSI_CERT_UTILS_DEBUG_EXIT;
    return result;
}

/**
 * @brief Get the base certificate name from a certificate chain
 * @ingroup globus_gsi_cert_utils
 * @details
 * Get the base name of a proxy certificate.  Given an X509 name, strip
 * off the proxy related /CN components to get the base name of the
 * certificate's subject
 *
 * @param subject
 *        Pointer to an X509_NAME object which gets stripped
 * @param cert_chain
 *        The certificate chain used to detect the number of CNs to strip. This
 *        is done by figuring out the number of proxies in the chain.
 * @return
 *        GLOBUS_SUCCESS
 */
globus_result_t
globus_gsi_cert_utils_get_base_name(
    X509_NAME *                         subject,
    STACK_OF(X509) *                    cert_chain)
{
    X509_NAME_ENTRY *                   ne;
    int                                 i;
    int                                 depth = 0;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_cert_utils_cert_type_t   cert_type;
    static char *                       _function_name_ =
        "globus_gsi_cert_utils_get_base_name";
    GLOBUS_I_GSI_CERT_UTILS_DEBUG_ENTER;

    for(i = 0;i < sk_X509_num(cert_chain);i++)
    {
        result = globus_gsi_cert_utils_get_cert_type(
            sk_X509_value(cert_chain, i),
            &cert_type);

        if (result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_CERT_UTILS_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_CERT_UTILS_ERROR_DETERMINING_CERT_TYPE);
            goto exit;
        }

        if(GLOBUS_GSI_CERT_UTILS_IS_PROXY(cert_type) &&
           (GLOBUS_GSI_CERT_UTILS_IS_IMPERSONATION_PROXY(cert_type) ||
           GLOBUS_GSI_CERT_UTILS_IS_LIMITED_PROXY(cert_type)) )
        {
            depth++;
        }
        else
        {
            break;
        }
    }
    
    /* 
     * drop all the proxy related /CN=* entries 
     */
    for(i=0;i<depth;i++)
    {
        ne = X509_NAME_delete_entry(subject,
                                    X509_NAME_entry_count(subject)-1);
        if(ne)
        {
            X509_NAME_ENTRY_free(ne);
        }
    }

 exit:
    GLOBUS_I_GSI_CERT_UTILS_DEBUG_EXIT;
    return GLOBUS_SUCCESS;
}

/**
 * @brief Get the end-entity certificate from a certificate chain
 * @ingroup globus_gsi_cert_utils
 * @details
 * Get the end-entity certificate associated with a certificate chain
 *
 * @param cert_chain
 *    Certificate chain to inspect.
 * @param eec
 *    Pointer to be set to the EEC value from within the cert chain. Must
 *    freed by the caller.
 */
globus_result_t
globus_gsi_cert_utils_get_eec(
    STACK_OF(X509) *                    cert_chain,
    X509 **                             eec)
{
    int                                 i;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_cert_utils_cert_type_t   cert_type;
    static char *                       _function_name_ =
        "globus_gsi_cert_utils_get_eec";
    GLOBUS_I_GSI_CERT_UTILS_DEBUG_ENTER;

    *eec = NULL;
    for(i = 0;i < sk_X509_num(cert_chain);i++)
    {
        result = globus_gsi_cert_utils_get_cert_type(
            sk_X509_value(cert_chain, i),
            &cert_type);

        if (result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_CERT_UTILS_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_CERT_UTILS_ERROR_DETERMINING_CERT_TYPE);
            goto exit;
        }

        if(cert_type == GLOBUS_GSI_CERT_UTILS_TYPE_EEC)
        {
            *eec = sk_X509_value(cert_chain, i);
            break;
        }
    }
    

 exit:
    GLOBUS_I_GSI_CERT_UTILS_DEBUG_EXIT;
    return GLOBUS_SUCCESS;
}
/* globus_gsi_cert_utils_get_eec() */

/**
 * @brief Get the identity certificate from a certificate chain
 * @ingroup globus_gsi_cert_utils
 * @details
 * Get the identity-providing certificate associated with a certificate chain.
 * This may be an independent proxy or a end-entity certificate.
 *
 * @param cert_chain
 *    Certificate chain to inspect.
 * @param identity_cert
 *    Pointer to be set to the certificate value from within the cert chain.
 *    Must freed by the caller.
 */
globus_result_t
globus_gsi_cert_utils_get_identity_cert(
    STACK_OF(X509) *                    cert_chain,
    X509 **                             identity_cert)
{
    int                                 i;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_cert_utils_cert_type_t   cert_type;
    static char *                       _function_name_ =
        "globus_gsi_cert_utils_get_identity_cert";
    GLOBUS_I_GSI_CERT_UTILS_DEBUG_ENTER;

    *identity_cert = NULL;
    for(i = 0;i < sk_X509_num(cert_chain);i++)
    {
        result = globus_gsi_cert_utils_get_cert_type(
            sk_X509_value(cert_chain, i),
            &cert_type);

        if (result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_CERT_UTILS_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_CERT_UTILS_ERROR_DETERMINING_CERT_TYPE);
            goto exit;
        }

        if(cert_type == GLOBUS_GSI_CERT_UTILS_TYPE_EEC ||
           (cert_type & GLOBUS_GSI_CERT_UTILS_TYPE_INDEPENDENT_PROXY))
        {
            *identity_cert = sk_X509_value(cert_chain, i);
            break;
        }
    }
    

 exit:
    GLOBUS_I_GSI_CERT_UTILS_DEBUG_EXIT;
    return GLOBUS_SUCCESS;
}
/* globus_gsi_cert_utils_get_identity_cert() */

static char *
globus_l_gsi_cert_utils_normalize_dn(
    const char *                        dn)
{
    char *                              result;
    int                                 i = 0;
    int                                 j = 0;
    size_t                              length;
    char *                              tmp;

    length = strlen(dn) + 1;

    result = malloc(length);

    if(result == NULL)
    {
        return NULL;
    }

    while(i < strlen(dn))
    {
        result[j] = dn[i];
        i++;
        j++;

        if(dn[i - 1] == '/')
        {
            if(strncasecmp(&dn[i], "UID=", 4) == 0)
            {
                length += 3;
                tmp = realloc(result, length);
                if(tmp == NULL)
                {
                    free(result);
                    return NULL;
                }
                result = tmp;
                memcpy(&result[j], "USERID=", 7);
                j += 7;
                i += 4;
            }
            else if(strncasecmp(&dn[i], "E=", 2) == 0)
            {
                length += 11;
                tmp = realloc(result, length);
                if(tmp == NULL)
                {
                    free(result);
                    return NULL;
                }
                result = tmp;
                memcpy(&result[j], "emailAddress=", 13);
                j += 13;
                i += 2;
            }
            else if(strncasecmp(&dn[i], "Email=", 6) == 0)
            {
                length += 7;
                tmp = realloc(result, length);
                if(tmp == NULL)
                {
                    free(result);
                    return NULL;
                }
                result = tmp;
                memcpy(&result[j], "emailAddress=", 13);
                j += 13;
                i += 6;
            }
        }
    }
    result[j] = '\0';
    return result;
}

int
globus_i_gsi_cert_utils_dn_cmp(
    const char *                        dn1,
    const char *                        dn2)
{
    if(strcasecmp(dn1, dn2) == 0)
    {
        return 0;
    }
    else
    {
        char * tmp_dn1;
        char * tmp_dn2;
        int result;

        tmp_dn1 = globus_l_gsi_cert_utils_normalize_dn(dn1);

        if(tmp_dn1 == NULL)
        {
            return -1;
        }
        
        tmp_dn2 = globus_l_gsi_cert_utils_normalize_dn(dn2);

        if(tmp_dn2 == NULL)
        {
            free(tmp_dn1);
            return -1;
        }
        
        result = strcasecmp(tmp_dn1, tmp_dn2);

        free(tmp_dn1);
        free(tmp_dn2);
        
        return result;
    }
}
