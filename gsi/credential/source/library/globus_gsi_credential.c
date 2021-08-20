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

#include "globus_i_gsi_credential.h"
#include "globus_gsi_system_config.h"
#include "globus_gsi_cert_utils.h"
#include "version.h"
#include "openssl/pem.h"
#include "openssl/x509.h"
#include "openssl/pkcs12.h"
#include "openssl/err.h"

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

#define d2i_arg_2_cast (const unsigned char **)
#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define PKCS12_bag_type(b) M_PKCS12_bag_type(b)
#define PKCS12_cert_bag_type(b) M_PKCS12_cert_bag_type(b)
#define PKCS12_SAFEBAG_get0_p8inf(bag) (bag)->value.keybag;
#endif

static int globus_l_gsi_credential_activate(void);
static int globus_l_gsi_credential_deactivate(void);

static
globus_result_t
globus_l_credential_sort_cert_list(
    STACK_OF(X509) *                    certs);

int                                     globus_i_gsi_cred_debug_level = 0;
FILE *                                  globus_i_gsi_cred_debug_fstream = NULL;

/**
 * Module descriptor static initializer.
 */
globus_module_descriptor_t globus_i_gsi_credential_module =
{
    "globus_credential",
    globus_l_gsi_credential_activate,
    globus_l_gsi_credential_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

/**
 * Module activation
 */
static
int
globus_l_gsi_credential_activate(void)
{
    int                                 result = (int) GLOBUS_SUCCESS;
    char *                              tmp_string;

    tmp_string = globus_module_getenv("GLOBUS_GSI_CRED_DEBUG_LEVEL");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_gsi_cred_debug_level = atoi(tmp_string);
        
        if(globus_i_gsi_cred_debug_level < 0)
        {
            globus_i_gsi_cred_debug_level = 0;
        }
    }

    tmp_string = globus_module_getenv("GLOBUS_GSI_CRED_DEBUG_FILE");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_gsi_cred_debug_fstream = fopen(tmp_string, "a");
        if(globus_i_gsi_cred_debug_fstream == NULL)
        {
            result = (int) GLOBUS_FAILURE;
            goto exit;
        }
    }
    else
    {
        /* if the env. var. isn't set, use stderr */
        globus_i_gsi_cred_debug_fstream = stderr;
    }

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    result = globus_module_activate(GLOBUS_COMMON_MODULE);

    if(result != GLOBUS_SUCCESS)
    {
        goto exit;
    }
    
    result = globus_module_activate(GLOBUS_GSI_SYSCONFIG_MODULE);

    if(result != GLOBUS_SUCCESS)
    {
        goto exit;
    }

    result = globus_module_activate(GLOBUS_GSI_CALLBACK_MODULE);

    if(result != GLOBUS_SUCCESS)
    {
        goto exit;
    }
    
    OpenSSL_add_all_algorithms();

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    
 exit:

    return result;
}

/**
 * Module deactivation
 *
 */
static
int
globus_l_gsi_credential_deactivate(void)
{
    int                                 result = (int) GLOBUS_SUCCESS;

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    EVP_cleanup();

    globus_module_deactivate(GLOBUS_GSI_CALLBACK_MODULE);

    globus_module_deactivate(GLOBUS_GSI_SYSCONFIG_MODULE);

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;

    if(globus_i_gsi_cred_debug_fstream != stderr)
    {
        fclose(globus_i_gsi_cred_debug_fstream);
    }
    return result;
}
/* globus_l_gsi_credential_deactivate() */

static globus_result_t
globus_l_gsi_cred_get_service(
    X509_NAME *                         subject,
    char **                             service);

static globus_result_t
globus_l_gsi_cred_subject_cmp(
    X509_NAME *                   actual_subject,
    X509_NAME *                   desired_subject);

#endif

/**
 * @brief Read a credential
 * @ingroup globus_gsi_cred_operations
 * @details
 * Read a credential from a filesystem location. The credential
 * to read will be determined by the search order specified in the handle
 * attributes.  
 * @param handle
 *        The credential handle to set.  This credential handle
 *        should already be initialized using globus_gsi_cred_handle_init.
 * @param desired_subject
 *        The subject to check for when reading in a credential.  The
 *        desired_subject should be either a exact match of the read cert's
 *        subject or should just contain the /CN entry. If null, the
 *        credential read in is the first match based on the system
 *        configuration (paths and environment variables)
 * @return
 *        GLOBUS_SUCCESS if no errors occurred, otherwise, an error object
 *        identifier is returned.
 *
 * @see globus_gsi_cred_read_proxy()
 * @see globus_gsi_cred_read_cert_and_key()
 *
 * @note  This function always searches for the desired credential.
 *        If you don't want to perform a search, then don't use this
 *        function.  The search goes in the order of the handle
 *        attributes' search order.
 *
 */
globus_result_t
globus_gsi_cred_read(
    globus_gsi_cred_handle_t            handle,
    X509_NAME *                         desired_subject)
{
    time_t                              lifetime = 0;
    int                                 index = 0;
    int                                 result_index = 0;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_result_t                     results[4];
    X509_NAME *                         found_subject = NULL;
    char *                              cert = NULL;
    char *                              key = NULL;
    char *                              proxy = NULL;
    char *                              service_name = NULL;
    
    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    for(result_index = 0; result_index < 4; ++result_index)
    {
        results[result_index] = GLOBUS_SUCCESS;
    }
    result_index = 0;

    if(handle == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            results[result_index],
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("Null handle passed to function: %s"), __func__));
        goto exit;
    }

    /* search for the credential of choice */

    do
    {
        switch(handle->attrs->search_order[index])
        {
        case GLOBUS_PROXY:

            results[result_index] = GLOBUS_GSI_SYSCONFIG_GET_PROXY_FILENAME(
                &proxy, 
                GLOBUS_PROXY_FILE_INPUT);
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                proxy = NULL;
                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED);
                break;
            }
                
            results[result_index] = globus_gsi_cred_read_proxy(handle, proxy);
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED);
                goto exit;
            }
            
            if(desired_subject != NULL)
            {
                results[result_index] = globus_gsi_cred_get_X509_subject_name(
                    handle, 
                    &found_subject);
                if(results[result_index] != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED);
                    goto exit;
                }

                results[result_index] = globus_l_gsi_cred_subject_cmp(found_subject,
                                                                      desired_subject);

                X509_NAME_free(found_subject);
                found_subject = NULL;                
                
                if(results[result_index] != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED);
                    goto exit;
                }
            }

            results[result_index] = globus_gsi_cred_get_lifetime(
                handle,
                &lifetime);
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_WITH_CRED);
                goto exit;
            }
            
            if(lifetime <= 0)
            {
                char *                          subject = NULL;
                
                subject = X509_NAME_oneline(
                    X509_get_subject_name(handle->cert),
                    NULL, 0);
                
                GLOBUS_GSI_CRED_ERROR_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_WITH_CRED,
                    (_GCRSL("The proxy credential: %s\n      with subject: %s\n"
                     "      expired %d minutes ago.\n"),
                     proxy,
                     subject,
                     (-lifetime)/60));
                
                OPENSSL_free(subject);
                goto exit;
            }
            GLOBUS_I_GSI_CRED_DEBUG_FPRINTF(1, (globus_i_gsi_cred_debug_fstream,
				"Using Proxy file (%s)\n", proxy));

            goto exit;

        case GLOBUS_USER:
            
            results[result_index] = 
                GLOBUS_GSI_SYSCONFIG_GET_USER_CERT_FILENAME(&cert, &key);
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                cert = NULL;
                key = NULL;
                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_READING_CRED);
                break;
            }                    

            results[result_index] = globus_gsi_cred_read_cert(handle, cert);
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_READING_CRED);
                goto exit;
            }

            results[result_index] = globus_gsi_cred_read_key(
                handle, 
                key, 
                globus_i_gsi_cred_password_callback_no_prompt);
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                globus_object_t *       error_obj;
                error_obj = globus_error_peek(results[result_index]);
                if(globus_error_get_type(error_obj) == 
                   GLOBUS_GSI_CRED_ERROR_KEY_IS_PASS_PROTECTED)
                {
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_READING_CRED);
                    break;
                }

                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_READING_CRED);
                goto exit;
            }

            results[result_index] = globus_i_gsi_cred_goodtill(
                handle,
                &(handle->goodtill));
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_READING_CRED);
                goto exit;
            }

            if(desired_subject != NULL)
            {
                results[result_index] = globus_gsi_cred_get_X509_subject_name(
                    handle, 
                    &found_subject);
                if(results[result_index] != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_READING_CRED);
                    goto exit;
                }
                
                results[result_index] = globus_l_gsi_cred_subject_cmp(
                    found_subject,
                    desired_subject);

                X509_NAME_free(found_subject);
                found_subject = NULL;
                
                if(results[result_index] != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_READING_CRED);
                    goto exit;
                }
            }

            results[result_index] = globus_gsi_cred_get_lifetime(
                handle,
                &lifetime);
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_WITH_CRED);
                goto exit;
            }
            
            if(lifetime <= 0)
            {
                char *                          subject = NULL;
                
                subject = X509_NAME_oneline(
                    X509_get_subject_name(handle->cert),
                    NULL, 0);
                
                GLOBUS_GSI_CRED_ERROR_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_WITH_CRED,
                    (_GCRSL("The user credential: %s\n      with subject: %s\n"
                     "     has expired %d minutes ago.\n"),
                     cert,
                     subject,
                     (-lifetime)/60));
                
                OPENSSL_free(subject);
                goto exit;
            }

            GLOBUS_I_GSI_CRED_DEBUG_FPRINTF(1, (globus_i_gsi_cred_debug_fstream,
			"Using User cert file (%s), key file (%s)\n", cert, key));
            goto exit;

        case GLOBUS_HOST:
            
            results[result_index] = 
                GLOBUS_GSI_SYSCONFIG_GET_HOST_CERT_FILENAME(&cert, &key);
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                cert = NULL;
                key = NULL;
                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_READING_HOST_CRED);
                break;
            }                    

            results[result_index] = globus_gsi_cred_read_cert(handle, cert);
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_READING_HOST_CRED);
                goto exit;
            }

            results[result_index] = globus_gsi_cred_read_key(
                handle, 
                key, 
                globus_i_gsi_cred_password_callback_no_prompt);
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                globus_object_t *       error_obj;
                error_obj = globus_error_peek(results[result_index]);
                if(globus_error_get_type(error_obj) == 
                   GLOBUS_GSI_CRED_ERROR_KEY_IS_PASS_PROTECTED)
                {
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_READING_HOST_CRED);
                    break;
                }

                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_READING_HOST_CRED);
                goto exit;
            }

            results[result_index] = globus_i_gsi_cred_goodtill(
                handle,
                &(handle->goodtill));
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_READING_HOST_CRED);
                goto exit;
            }

            if(desired_subject != NULL)
            {
                results[result_index] = globus_gsi_cred_get_X509_subject_name(
                    handle, 
                    &found_subject);
                
                if(results[result_index] != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_READING_HOST_CRED);
                    goto exit;
                }
                
                results[result_index] = globus_l_gsi_cred_subject_cmp(found_subject,
                                                                      desired_subject);

                X509_NAME_free(found_subject);
                found_subject = NULL;                
                
                if(results[result_index] != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_READING_HOST_CRED);
                    goto exit;
                }
            }
            
            results[result_index] = globus_gsi_cred_get_lifetime(
                handle,
                &lifetime);
            if(results[result_index] != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_WITH_CRED);
                goto exit;
            }
            
            if(lifetime <= 0)
            {
                char *                          subject = NULL;
                
                subject = X509_NAME_oneline(
                    X509_get_subject_name(handle->cert),
                    NULL, 0);
                
                GLOBUS_GSI_CRED_ERROR_RESULT(
                    results[result_index],
                    GLOBUS_GSI_CRED_ERROR_WITH_CRED,
                    (_GCRSL("The host credential: %s\n     with subject: %s\n     "
                     "has expired %d minutes ago.\n"),
                     cert,
                     subject,
                     (-lifetime)/60));
                
                OPENSSL_free(subject);
                goto exit;
            }

            GLOBUS_I_GSI_CRED_DEBUG_FPRINTF(1, (globus_i_gsi_cred_debug_fstream,
			"Using Host cert file (%s), key file (%s)\n", cert, key));
            goto exit;
            
        case GLOBUS_SERVICE:

            if(desired_subject != NULL)
            { 
                results[result_index] =
                    globus_l_gsi_cred_get_service(desired_subject,
                                                  &service_name);
          
                if(results[result_index] != GLOBUS_SUCCESS)
                {
                    service_name = NULL;
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_READING_SERVICE_CRED);
                    break;
                }                    
                
                results[result_index] = 
                    GLOBUS_GSI_SYSCONFIG_GET_SERVICE_CERT_FILENAME(
                        service_name, &cert, &key);
                if(results[result_index] != GLOBUS_SUCCESS)
                {
                    cert = NULL;
                    key = NULL;
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_READING_SERVICE_CRED);
                    break;
                }                    

                results[result_index] = 
                    globus_gsi_cred_read_cert(handle, cert);
                if(results[result_index] != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_READING_SERVICE_CRED);
                    goto exit;
                }
                    
                results[result_index] = globus_gsi_cred_read_key(
                    handle, 
                    key, 
                    globus_i_gsi_cred_password_callback_no_prompt);
                if(results[result_index] != GLOBUS_SUCCESS)
                {
                    globus_object_t *   error_obj;
                    error_obj = globus_error_peek(results[result_index]);
                    if(globus_error_get_type(error_obj) == 
                       GLOBUS_GSI_CRED_ERROR_KEY_IS_PASS_PROTECTED)
                    {
                        GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                            results[result_index],
                            GLOBUS_GSI_CRED_ERROR_READING_SERVICE_CRED);
                        break;
                    }

                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_READING_SERVICE_CRED);
                    goto exit;
                }
                    
                results[result_index] = globus_i_gsi_cred_goodtill(
                    handle,
                    &(handle->goodtill));
                if(results[result_index] != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_WITH_CRED);
                    goto exit;
                }

                if(desired_subject != NULL)
                {
                    results[result_index] = globus_gsi_cred_get_X509_subject_name(
                        handle, 
                        &found_subject);
                    if(results[result_index] != GLOBUS_SUCCESS)
                    {
                        GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                            results[result_index],
                            GLOBUS_GSI_CRED_ERROR_READING_SERVICE_CRED);
                        goto exit;
                    }
                    
                    results[result_index] = globus_l_gsi_cred_subject_cmp(found_subject,
                                                                          desired_subject);

                    X509_NAME_free(found_subject);
                    found_subject = NULL;                
                
                    if(results[result_index] != GLOBUS_SUCCESS)
                    {
                        GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                            results[result_index],
                            GLOBUS_GSI_CRED_ERROR_READING_SERVICE_CRED);
                        break;
                    }
                }
            
                results[result_index] = globus_gsi_cred_get_lifetime(
                    handle,
                    &lifetime);
                if(results[result_index] != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_WITH_CRED);
                    goto exit;
                }
                
                if(lifetime <= 0)
                {
                    char *                          subject = NULL;
                    
                    subject = X509_NAME_oneline(
                        X509_get_subject_name(handle->cert),
                        NULL, 0);
                    
                    GLOBUS_GSI_CRED_ERROR_RESULT(
                        results[result_index],
                        GLOBUS_GSI_CRED_ERROR_WITH_CRED,
                        (_GCRSL("The service credential: %s\n     with subject:\n%s\n"
                         "     has expired %d minutes ago.\n"),
                         cert,
                         subject,
                         (-lifetime)/60));
                    
                    OPENSSL_free(subject);
                    goto exit;
                }

                GLOBUS_I_GSI_CRED_DEBUG_FPRINTF(1, (globus_i_gsi_cred_debug_fstream,
			"Using Service cert file (%s), key file (%s)\n", cert, key));
                goto exit;
            }
            else
            {
                result_index--;
                break;
            }
            
        case GLOBUS_SO_END:
            {
                globus_object_t *       multiple_obj;
                
                multiple_obj = globus_error_construct_multiple(
                    GLOBUS_GSI_CREDENTIAL_MODULE,
                    GLOBUS_GSI_CRED_ERROR_NO_CRED_FOUND,
                    _GCRSL(globus_l_gsi_cred_error_strings[
                        GLOBUS_GSI_CRED_ERROR_NO_CRED_FOUND]));
                
                while(result_index--)
                {
                    globus_error_mutliple_add_chain(
                        multiple_obj,
                        globus_error_get(results[result_index]),
                        _GCRSL("Attempt %d"),
                        result_index + 1);
                }
                
                result_index = 0;
                results[result_index] = globus_error_put(multiple_obj);
            }
            
            GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
                results[result_index],
                GLOBUS_GSI_CRED_ERROR_NO_CRED_FOUND);
            goto exit;
        }

        if(proxy)
        {
            free(proxy);
            proxy = NULL;
        }
        
        if(cert)
        {
            free(cert);
            cert = NULL;
        }
        
        if(key)
        {
            free(key);
            key = NULL;
        }
            
        if(service_name)
        {
            free(service_name);
            service_name = NULL;
        }
            
        result_index++;
    } while(++index);
    
 exit:

    result = results[result_index];
    for(index = 0; index < result_index; ++index)
    {
        globus_object_t *               result_obj;
        if(results[index] != GLOBUS_SUCCESS)
        {
            result_obj = globus_error_get(results[index]);
            globus_object_free(result_obj);
        }
    }

    if(proxy)
    {
        free(proxy);
    }
    
    if(cert)
    {
        free(cert);
    }

    if(key)
    {
        free(key);
    }
    
    if(service_name)
    {
        free(service_name);
    }

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    return result;
}
/* globus_gsi_cred_read() */

/**
 * @brief Read proxy credential
 * @ingroup globus_gsi_cred_operations
 * @details
 * Read a proxy from a PEM file.
 *
 * @param[inout] handle
 *        The credential handle to set based on the proxy
 *        credential read from the file
 * @param[in] proxy_filename
 *        The file containing the proxy credential
 *
 * @return
 *        GLOBUS_SUCCESS or an error object identifier
 */
globus_result_t
globus_gsi_cred_read_proxy(
    globus_gsi_cred_handle_t            handle,
    const char *                        proxy_filename)
{
    BIO *                               proxy_bio = NULL;
    globus_result_t                     result;
    
    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
            (_GCRSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }

    /* create the bio to read the proxy in from */

    if((proxy_bio = BIO_new_file(proxy_filename, "r")) == NULL)
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
            (_GCRSL("Can't open proxy file: %s for reading"), proxy_filename));
        goto exit;
    }

    result = globus_gsi_cred_read_proxy_bio(handle, proxy_bio);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED);
        goto exit;
    }

 exit:

    if(proxy_bio)
    {
        BIO_free(proxy_bio);
    }

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    return result;
}
/* globus_gsi_cred_read_proxy() */

/**
 * @brief Read proxy credential from a BIO
 * @ingroup globus_gsi_cred_operations
 * @details
 * Read a Proxy Credential from a BIO stream and set the 
 * credential handle to represent the read credential.
 * The values read from the stream, in order, will be
 * the signed certificate, the private key, 
 * and the certificate chain.
 *
 * @param handle
 *        The credential handle to set.  The credential
 *        should handle be initialized (i.e. not NULL).
 * @param bio
 *        The stream to read the credential from
 *
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which
 *        case an error object is returned
 */
globus_result_t
globus_gsi_cred_read_proxy_bio(
    globus_gsi_cred_handle_t            handle,
    BIO *                               bio)
{
    globus_result_t                     result;
    STACK_OF(X509) *                    certs = NULL;
    X509 *                              tmp_cert = NULL;
    char *                              name = NULL;
    char *                              header = NULL;
    unsigned char *                     data = NULL;
    unsigned char *                     save_data = NULL;
    long                                len;
    EVP_CIPHER_INFO                     cipher;

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
            (_GCRSL("Null handle passed to function: %s"), __func__));
        goto exit;
    }

    if(bio == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
            (_GCRSL("Null bio variable passed to function: %s"), __func__));
        goto exit;
    }

    /* Clear aspects of the handle related to the proxy info */
    if(handle->cert != NULL)
    {
        X509_free(handle->cert);
        handle->cert = NULL;
    }
    if(handle->key != NULL)
    {
        EVP_PKEY_free(handle->key);
        handle->key = NULL;
    }
    if(handle->cert_chain != NULL)
    {
        sk_X509_pop_free(handle->cert_chain, X509_free);
        handle->cert_chain = NULL;
    }

    certs = handle->cert_chain = sk_X509_new_null();

    /* Read all the cert and key PEM data from the proxy BIO */
    while ((!BIO_eof(bio)) && PEM_read_bio(bio, &name, &header, &data, &len))
    {
        save_data = data;

        if (strcmp(name, PEM_STRING_X509) == 0 ||
            strcmp(name, PEM_STRING_X509_OLD) == 0)
        {
            tmp_cert = NULL;
            tmp_cert = d2i_X509(&tmp_cert, d2i_arg_2_cast &data, len);
            if (tmp_cert == NULL)
            {
                GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
                    (_GCRSL("Couldn't read certificate from bio")));

                goto exit;
            }
            sk_X509_push(certs, tmp_cert);
        }
        else if (strcmp(name, PEM_STRING_RSA) == 0 ||
                 strcmp(name, PEM_STRING_DSA) == 0)
        {
            if (!PEM_get_EVP_CIPHER_INFO(header, &cipher))
            {
                GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
                    (_GCRSL("Couldn't read certificate from bio")));

                goto exit;
            }
            if (!PEM_do_header(&cipher, data, &len, (int (*) ()) globus_i_gsi_cred_password_callback_no_prompt, NULL))
            {
                GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
                    (_GCRSL("Couldn't read certificate from bio")));

                goto exit;
            }

            handle->key = d2i_AutoPrivateKey(&handle->key, d2i_arg_2_cast &data, len);
            if (handle->key == NULL)
            {
                GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
                    (_GCRSL("Couldn't read certificate from bio")));

                goto exit;
            }
        }
        else if (strcmp(name, PEM_STRING_PKCS8INF) == 0)
        {
            PKCS8_PRIV_KEY_INFO *p8inf = NULL;

            p8inf = d2i_PKCS8_PRIV_KEY_INFO(&p8inf, d2i_arg_2_cast &data, len);
            if (p8inf == NULL)
            {
                GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
                    (_GCRSL("Couldn't read pkcs8 key info from bio")));

                goto exit;
            }
            handle->key = EVP_PKCS82PKEY(p8inf);

            PKCS8_PRIV_KEY_INFO_free(p8inf);
            if (handle->key == NULL)
            {
                GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
                    (_GCRSL("Couldn't parse pkcs8 key")));

                goto exit;
            }
        }
        else
        {
            GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
                (_GCRSL("Unhandled PEM sequence: %s"), name));
            goto exit;
        }
        if (save_data)
        {
            OPENSSL_free(save_data);
            save_data = NULL;
        }
        if (header)
        {
            OPENSSL_free(header);
            header = NULL;
        }
        if (name)
        {
            OPENSSL_free(name);
            name = NULL;
        }
    }
    save_data = NULL;

    if (handle->key == NULL || sk_X509_num(certs) == 0)
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
            (_GCRSL("Couldn't read PEM from bio")));
        goto exit;
    }

    /* sort the certs in the X509 stack so that the nth cert is
     * signed by (n+1)th in the stack
     */
    if ((result = globus_l_credential_sort_cert_list(certs)) != GLOBUS_SUCCESS)
    {
        goto exit;
    }

    /* The head of the stack is now the outermost proxy */
    handle->cert = sk_X509_shift(certs);

    /* The rest is the signature chain for it */
    handle->cert_chain = certs;

    result = globus_i_gsi_cred_goodtill(handle, &(handle->goodtill));
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WITH_CRED);
        goto exit;
    }

    result = GLOBUS_SUCCESS;

 exit:
    ERR_clear_error();
    if (save_data)
    {
        OPENSSL_free(save_data);
    }
    if (header)
    {
        OPENSSL_free(header);
        header = NULL;
    }
    if (name)
    {
        OPENSSL_free(name);
        name = NULL;
    }

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    return result;
}
/* globus_gsi_cred_read_proxy_bio() */

/**
 * @brief Read a private key
 * @ingroup globus_gsi_cred_operations
 * @details
 * Read a key from a PEM file.
 *
 * @param handle
 *        the handle to set based on the key that is read
 * @param key_filename
 *        the filename of the key to read
 * @param pw_cb
 *        the callback for obtaining a password for decrypting the key. 
 *
 * @return
 *        GLOBUS_SUCCESS or an error object identifier
 */
globus_result_t
globus_gsi_cred_read_key(
    globus_gsi_cred_handle_t            handle,
    const char *                        key_filename,
    int                                 (*pw_cb)())
{
    BIO *                               key_bio = NULL;
    globus_result_t                     result;

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("NULL handle passed to function: %s"), __func__));
       goto exit;
    }

    if(!(key_bio = BIO_new_file(key_filename, "r")))
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("Can't open bio stream for "
             "key file: %s for reading"), key_filename));
        goto exit;
    }
    
    /* read in the key */

    if(handle->key != NULL)
    {
        EVP_PKEY_free(handle->key);
        handle->key = NULL;
    }

    if(!PEM_read_bio_PrivateKey(key_bio, & handle->key, pw_cb, NULL))
    {
        if(ERR_GET_REASON(ERR_peek_error()) == PEM_R_BAD_PASSWORD_READ)
        {
            GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_CRED_ERROR_KEY_IS_PASS_PROTECTED,
                (_GCRSL("GSI does not currently support password protected "
                 "private keys.")));
            goto exit;
        }
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("Can't read credential's private key from PEM")));
        goto exit;
    }

    result = GLOBUS_SUCCESS;

 exit:

    if(key_bio)
    {
        BIO_free(key_bio);
    }

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    return result;
}
/* globus_gsi_cred_read_key() */

/**
 * @brief Read a certificate chain from a file
 * @ingroup globus_gsi_cred_operations
 * @details
 * Read a cert from a file.  Cert should be in PEM format.  Will also
 * read additional certificates as chain if present.
 *
 * @param[out] handle
 *        the handle to set based on the certificate that is read
 * @param[in] cert_filename
 *        the filename of the certificate to read
 *
 * @return
 *        GLOBUS_SUCCESS or an error object identifier
 */
globus_result_t
globus_gsi_cred_read_cert(
    globus_gsi_cred_handle_t            handle,
    const char *                        cert_filename)
{
    BIO *                               cert_bio = NULL;
    globus_result_t                     result;

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("NULL handle passed to function: %s"), __func__));
       goto exit;
    }

    if(!(cert_bio = BIO_new_file(cert_filename, "r")))
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("Can't open cert file: %s for reading"), cert_filename));
        goto exit;
    }

    result = globus_gsi_cred_read_cert_bio(handle, cert_bio);

 exit:

    if(cert_bio)
    {
        BIO_free(cert_bio);
    }

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    return result;
}
/* globus_gsi_cred_read_cert() */

/**
 * @brief Read a certificate chain from a BIO
 * @ingroup globus_gsi_cred_operations
 * @details
 * Read a cert from a BIO.  Cert should be in PEM format.  Will also
 * read additional certificates as chain if present.
 *
 * @param handle
 *        the handle to set based on the certificate that is read
 * @param bio
 *        the bio to read the certificate from
 *
 * @return
 *        GLOBUS_SUCCESS or an error object identifier
 */
globus_result_t
globus_gsi_cred_read_cert_bio(
    globus_gsi_cred_handle_t            handle,
    BIO *                               bio)
{
    globus_result_t                     result;
    int                                 i = 0;

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("NULL handle passed to function: %s"), __func__));
       goto exit;
    }

    if(bio == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED,
            (_GCRSL("Null bio variable passed to function: %s"), __func__));
        goto exit;
    }

    /* read in the cert */
    
    if(handle->cert != NULL)
    {
        X509_free(handle->cert);
        handle->cert = NULL;
    }

    if(!PEM_read_bio_X509(bio, &handle->cert, NULL, NULL))
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("Can't read credential cert from bio stream")));
        goto exit;
    }

    if(handle->cert_chain != NULL)
    {
        sk_X509_pop_free(handle->cert_chain, X509_free);
    }
    
    if((handle->cert_chain = sk_X509_new_null()) == NULL)
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("Can't initialize cert chain\n")));
        goto exit;
    }
    
    while(!BIO_eof(bio))
    {
        X509 *                          tmp_cert = NULL;
        if(!PEM_read_bio_X509(bio, &tmp_cert, NULL, NULL))
        {
            ERR_clear_error();
            break;
        }

        if(!sk_X509_insert(handle->cert_chain, tmp_cert, i))
        {
            X509_free(tmp_cert);
            /* ToDo: Fix memory leak from X509_NAME_online call below */
            GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_CRED_ERROR_READING_CRED,
                (_GCRSL("Error adding cert: %s\n to issuer cert chain\n"),
                 X509_NAME_oneline(X509_get_subject_name(tmp_cert), 0, 0)));
            goto exit;
        }
        ++i;
    }
    
    result = globus_i_gsi_cred_goodtill(handle, &(handle->goodtill));

    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WITH_CRED);
        goto exit;
    }
    
    result = GLOBUS_SUCCESS;

 exit:

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    return result;
}
/* globus_gsi_cred_read_cert_bio() */


/**
 * @brief Read certificate chain from a buffer
 * @ingroup globus_gsi_cred_operations
 * @details
 * Read a cert from a buffer.  Cert should be in PEM format.  Will also
 * read additional certificates as chain if present.  Any parameter besides
 * pem_buf may be NULL.
 *
 * @param pem_buf
 *        The buffer containing the PEM formatted cert and chain.
 * @param out_handle
 *        The handle to initialize and set cert on.
 * @param out_cert
 *        The X509 certificate. This should be freed with X509_free().
 * @param out_cert_chain
 *        The X509 certificate chain. This should be freed with sk_X509_free().
 * @param out_subject
 *        The identity name of the cert. This should be freed with OPENSSL_free().
 * @return
 *        GLOBUS_SUCCESS or an error object identifier
 */
globus_result_t
globus_gsi_cred_read_cert_buffer(
    const char *                        pem_buf,
    globus_gsi_cred_handle_t *          out_handle,
    X509 **                             out_cert,
    STACK_OF(X509) **                   out_cert_chain,
    char **                             out_subject)
{
    BIO *                               bp = NULL;
    X509 *                              cert = NULL;
    STACK_OF(X509) *                    cert_chain = NULL;
    char *                              subject = NULL;
    globus_gsi_cred_handle_t            handle = NULL;
    globus_result_t                     result;

    if(!pem_buf)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("NULL buffer: %s"), __func__));
        goto error;
    }

    bp = BIO_new(BIO_s_mem());

    BIO_write(bp, pem_buf, strlen(pem_buf));
    
    result = globus_gsi_cred_handle_init(&handle, NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    
    result = globus_gsi_cred_read_cert_bio(handle, bp);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    
    if(out_cert)
    {
        result = globus_gsi_cred_get_cert(handle, &cert);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
        *out_cert = cert;
    }
    
    if(out_cert_chain)
    {
        result = globus_gsi_cred_get_cert_chain(handle, &cert_chain);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
        *out_cert_chain = cert_chain;
    }
    
    if(out_subject)
    {
        result = globus_gsi_cred_get_identity_name(handle, &subject);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
        *out_subject = subject;    
    }
    
    if(out_handle)
    {
        *out_handle = handle;
    }
    else
    {
        globus_gsi_cred_handle_destroy(handle);
    }
        
    BIO_free(bp);

    return GLOBUS_SUCCESS;
    
error:
    if(bp)
    {
        BIO_free(bp);
    }
    if(cert)
    {
        *out_cert = NULL;
        X509_free(cert);
    }
    if(cert_chain)
    {
        *out_cert_chain = NULL;
        sk_X509_free(cert_chain);
    }
    if(subject)
    {
        *out_subject = NULL;
        OPENSSL_free(subject);
    }
    if(handle)
    {
        globus_gsi_cred_handle_destroy(handle);
    }
    
    return result;
}
/* globus_gsi_cred_read_cert_buffer() */


/**
 * @brief Read certificate and key from a PKCS12 file
 * @ingroup globus_gsi_cred_operations
 * @details
 * Read a cert and key from a file. The file should be in PKCS12 format.
 *
 * @param handle
 *        the handle to populate with the read credential
 * @param pkcs12_filename
 *        the filename containing the credential to read
 *
 * @return
 *        GLOBUS_SUCCESS or an error object identifier
 */
globus_result_t
globus_gsi_cred_read_pkcs12(
    globus_gsi_cred_handle_t            handle,
    const char *                        pkcs12_filename)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char                                password[100];
    STACK_OF(X509) *                    pkcs12_certs = NULL;
    PKCS12 *                            pkcs12 = NULL;
    PKCS12_SAFEBAG *                    bag = NULL;
    STACK_OF(PKCS12_SAFEBAG) *          pkcs12_safebags = NULL;
    PKCS7 *                             pkcs7 = NULL;
    STACK_OF(PKCS7) *                   auth_safes = NULL;
    PKCS8_PRIV_KEY_INFO *               pkcs8 = NULL;
    BIO *                               pkcs12_bio = NULL;
    int                                 i, j, bag_NID;

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("NULL handle passed to function: %s"), __func__));
       goto exit;
    }
    
    pkcs12_bio = BIO_new_file(pkcs12_filename, "r");
    if(!pkcs12_bio)
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("Couldn't create BIO for file: %s"), pkcs12_filename));
        goto exit;
    }

    d2i_PKCS12_bio(pkcs12_bio, &pkcs12);
    if(!pkcs12)
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("Couldn't read in PKCS12 credential from BIO")));
        goto exit;
    }

    EVP_read_pw_string(password, 100, NULL, 0);

    if(!PKCS12_verify_mac(pkcs12, password, -1))
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("Couldn't verify the PKCS12 MAC using the specified password")));
        goto exit;
    }

    auth_safes = PKCS12_unpack_authsafes(pkcs12);
    
    if(!auth_safes)
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("Couldn't dump cert and key from PKCS12 credential")));
        goto exit;
    }

    pkcs12_certs = sk_X509_new_null();
    
    for (i = 0; i < sk_PKCS7_num(auth_safes); i++)
    {
        pkcs7 = sk_PKCS7_value(auth_safes, i);
        
        bag_NID = OBJ_obj2nid(pkcs7->type);
        
        if(bag_NID == NID_pkcs7_data)
        {
            pkcs12_safebags = PKCS12_unpack_p7data(pkcs7);
        }
        else if(bag_NID == NID_pkcs7_encrypted)
        {
            pkcs12_safebags = PKCS12_unpack_p7encdata (pkcs7, password, -1);
        }
        else
        {
            GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_CRED_ERROR_READING_CRED,
                (_GCRSL("Couldn't get NID from PKCS7 that matched "
                 "{NID_pkcs7_data, NID_pkcs7_encrypted}")));
            goto exit;
        }

        if(!pkcs12_safebags)
        {
            GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_CRED_ERROR_READING_CRED,
                (_GCRSL("Couldn't unpack the PKCS12 safebags from "
                 "the PKCS7 credential")));
            goto exit;
        }

        for (j = 0; j < sk_PKCS12_SAFEBAG_num(pkcs12_safebags); j++)
        {
            bag = sk_PKCS12_SAFEBAG_value(pkcs12_safebags, j);
            
            if(PKCS12_bag_type(bag) == NID_certBag &&
               PKCS12_cert_bag_type(bag) == NID_x509Certificate)
            {
                sk_X509_push(pkcs12_certs, 
                             PKCS12_certbag2x509(bag));
            }
            else if(PKCS12_bag_type(bag) == NID_keyBag &&
                    handle->key == NULL)
            {
                pkcs8 = PKCS12_SAFEBAG_get0_p8inf(bag);
                handle->key = EVP_PKCS82PKEY(pkcs8);
                if (!handle->key)
                {
                    GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                        result,
                        GLOBUS_GSI_CRED_ERROR_READING_CRED,
                        (_GCRSL("Couldn't get the private key from the"
                         "PKCS12 credential")));
                    goto exit;
                }
            }
            else if(PKCS12_bag_type(bag) == 
                    NID_pkcs8ShroudedKeyBag &&
                    handle->key == NULL)
            {
                pkcs8 = PKCS12_decrypt_skey(bag,
                                              password,
                                              strlen(password));
                if(!pkcs8)
                {
                    GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                        result,
                        GLOBUS_GSI_CRED_ERROR_READING_CRED,
                        (_GCRSL("Couldn't get PKCS8 key from PKCS12 credential")));
                    goto exit;
                }
            
                handle->key = EVP_PKCS82PKEY(pkcs8);
                if (!handle->key)
                {
                    GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                        result,
                        GLOBUS_GSI_CRED_ERROR_READING_CRED,
                        (_GCRSL("Couldn't get private key from PKCS12 credential")));
                    goto exit;
                }
                
                PKCS8_PRIV_KEY_INFO_free(pkcs8);
            }
        }
    }

    if(!handle->key)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("Couldn't read private key from PKCS12 credential "
             "for unknown reason")));
        goto exit;
    }

    j = sk_X509_num(pkcs12_certs); 
    for(i = 0 ; i < j; i++)
    {
        handle->cert = sk_X509_pop(pkcs12_certs);

        if(X509_check_private_key(handle->cert, handle->key)) 
        {
            sk_X509_pop_free(pkcs12_certs, X509_free);
            pkcs12_certs = NULL;
            break;
        }
        else
        {
            X509_free(handle->cert);
            handle->cert = NULL;
        }
    }

    if(!handle->cert)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_READING_CRED,
            (_GCRSL("Couldn't read X509 certificate from PKCS12 credential")));
        goto exit;
    }

    result = globus_i_gsi_cred_goodtill(handle, &(handle->goodtill));
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WITH_CRED);
        goto exit;
    }

 exit:

    if(pkcs12_bio)
    {
        BIO_free(pkcs12_bio);
    }

    if(pkcs12)
    {
        PKCS12_free(pkcs12);
    }

    if(pkcs12_certs)
    {
        sk_X509_pop_free(pkcs12_certs, X509_free);
    }

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    return result;
}
/* globus_gsi_cred_read_pkcs12() */

/**
 * @brief Write Credential
 * @ingroup globus_gsi_cred_operations
 * @details
 * Write out a credential to a BIO.  The credential parameters written,
 * in order, are the signed certificate, the RSA private key,
 * and the certificate chain (a set of X509 certificates).
 * the credential is written out in PEM format. 
 *
 * @param handle
 *        The credential to write out
 * @param bio
 *        The BIO stream to write out to
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which
 *        case an error object ID is returned.
 */
globus_result_t
globus_gsi_cred_write(
    globus_gsi_cred_handle_t            handle,
    BIO *                               bio)
{
    int                                 i;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WRITING_CRED,
            (_GCRSL("NULL handle passed to function: %s"), __func__));
        goto error_exit;
    }
    
    if(bio == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WRITING_CRED,
            (_GCRSL("NULL bio variable passed to function: %s"), __func__));
        goto error_exit;
    }
    
    if(!PEM_write_bio_X509(bio, handle->cert))
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WRITING_CRED,
            (_GCRSL("Can't write PEM formatted X509 cert to BIO stream")));
        goto error_exit;
    }
    
    if(!PEM_write_bio_PrivateKey(bio, handle->key,
                           NULL, NULL, 0, NULL, NULL))
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WRITING_CRED,
            (_GCRSL("Can't write PEM formatted private key to BIO stream")));
        goto error_exit;
    }
    
    for(i = 0; i < sk_X509_num(handle->cert_chain); ++i)
    {
        if(!PEM_write_bio_X509(bio, sk_X509_value(handle->cert_chain, i)))
        {
            GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_CRED_ERROR_WRITING_CRED,
                (_GCRSL("Can't write PEM formatted X509 cert"
                 " in cert chain to BIO stream")));
            goto error_exit;
        }
    }
    
 error_exit:

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    return result;
}    
/* globus_gsi_cred_write() */
    
/**
 * @brief Write a proxy credential
 * @ingroup globus_gsi_cred_operations
 * @details
 * Write out a credential to a file.  The credential parameters written,
 * in order, are the signed certificate, the RSA private key,
 * and the certificate chain (a set of X509 certificates).
 * the credential is written out in PEM format. 
 *
 * @param handle
 *        The credential to write out
 * @param proxy_filename
 *        The file to write out to
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which
 *        case an error object ID is returned.
 */
globus_result_t
globus_gsi_cred_write_proxy(
    globus_gsi_cred_handle_t            handle,
    const char *                        proxy_filename)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    BIO *                               proxy_bio = NULL;
    mode_t                              oldmask;
    FILE *                              temp_proxy_fp = NULL;
    int                                 temp_proxy_fd = -1;
    
    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    /*
     * For systems that does not support a third (mode) argument in open()
     */
    oldmask = globus_libc_umask(0077);

    if(handle == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WRITING_PROXY_CRED,
            (_GCRSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }

    /* 
     * We always unlink the file first; it is the only way to be
     * certain that the file we open has never in its entire lifetime
     * had the world-readable bit set.  
     */
#if _WIN32
    /* Win32 API won't allow removing a read-only file */
    chmod(proxy_filename, S_IRWXU);
#endif
    temp_proxy_fd = remove(proxy_filename);

    /* 
     * Now, we must open w/ O_EXCL to make certain that WE are 
     * creating the file, so we know that the file was BORN w/ mode 0600.
     * As a bonus, O_EXCL flag will cause a failure in the presence
     * of a symlink, so we are safe from zaping a file due to the
     * presence of a symlink.
     */
    if ((temp_proxy_fd = globus_libc_open(
              proxy_filename, O_WRONLY|O_EXCL|O_CREAT, S_IRUSR|S_IWUSR)) < 0)
    {
        GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WRITING_PROXY_CRED);
        goto exit;
    }

    /* Finally, we have a safe fd.  Make it a stream like ssl wants. */
    temp_proxy_fp = fdopen(temp_proxy_fd,"w");

    /* Hand the stream over to ssl */
    if( !(temp_proxy_fp) || 
        !(proxy_bio = BIO_new_fp(temp_proxy_fp, BIO_CLOSE)))
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WRITING_PROXY_CRED,
            (_GCRSL("Can't open bio stream for writing to file: %s"), proxy_filename));
        if ( temp_proxy_fp ) 
        {
            fclose(temp_proxy_fp);
        } 
        else if (temp_proxy_fd >= 0 ) 
        {
            /* close underlying fd if we do not have a stream */
            close(temp_proxy_fd);
        }

        goto exit;
    }

    /* 
     * Note: at this point, calling BIO_free(proxy_bio) will
     * fclose the temp_proxy_fp, which in turn should close temp_proxy_fd.
     */

    result = globus_gsi_cred_write(handle, proxy_bio);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WRITING_PROXY_CRED);
        goto close_proxy_bio;
    }

    if(proxy_bio)
    {
        BIO_free(proxy_bio);
        proxy_bio = NULL;
    }

    goto exit;

 close_proxy_bio:

    if(proxy_bio != NULL)
    {
        BIO_free(proxy_bio);
    }

 exit:
    globus_libc_umask(oldmask);
    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    return result;
}    
/* globus_gsi_cred_write_proxy() */

/**
 * @brief Get the X.509 certificate type
 * @ingroup globus_gsi_cred_operations
 * @details
 * Determine the type of the given X509 certificate For the list of possible
 * values returned, see globus_gsi_cert_utils_cert_type_t.
 *
 * @param handle
 *        The credential handle containing the certificate
 * @param type
 *        The returned X509 certificate type
 *
 * @return
 *        GLOBUS_SUCCESS or an error captured in a globus_result_t
 */
globus_result_t
globus_gsi_cred_get_cert_type(
    globus_gsi_cred_handle_t            handle,
    globus_gsi_cert_utils_cert_type_t * type)
{
    globus_result_t                     result;

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;
    
    result = globus_gsi_cert_utils_get_cert_type(handle->cert, type);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WITH_CRED_CERT);
    }

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    return result;
}
/* globus_gsi_cred_get_cert_type() */

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

/**
 * @brief Get PROXYCERTINFO Struct
 * @ingroup globus_i_gsi_cred
 * @details
 * Get the PROXYCERTINFO struct from the X509 struct.
 * The PROXYCERTINFO struct that gets set must be freed
 * with a call to PROXYCERTINFO_free.
 *
 * @param cert
 *        The X509 struct containing the PROXY_CERT_INFO_EXTENSION struct
 *        in its extensions
 * @param proxycertinfo
 *        The resulting PROXY_CERT_INFO_EXTENSION struct.  This variable
 *        should be freed with a call to PROXY_CERT_INFO_EXTENSION_free when
 *        no longer in use.  It will have a value of NULL if no
 *        proxycertinfo extension exists in the X509 certificate
 * @return
 *        GLOBUS_SUCCESS (even if no proxycertinfo extension was found)
 *        or an globus error object id if an error occurred
 */
globus_result_t
globus_i_gsi_cred_get_proxycertinfo(
    X509 *                              cert,
    PROXY_CERT_INFO_EXTENSION **        proxycertinfo)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 pci_old_NID;
    X509_EXTENSION *                    pci_extension = NULL;
    int                                 extension_loc;

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    *proxycertinfo = NULL;

    pci_old_NID = OBJ_txt2nid("1.3.6.1.4.1.3536.1.222");
    if(pci_old_NID == NID_undef)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WITH_CRED,
            (_GCRSL("Couldn't get numeric ID for PROXYCERTINFO extension")));
        goto exit;
    }

    if(cert == NULL)
    {
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WITH_CRED,
            (_GCRSL("NULL X509 cert parameter passed to function: %s"), 
             __func__));
        goto exit;
    }

    if ((extension_loc = X509_get_ext_by_NID(cert, NID_proxyCertInfo, -1)) == -1
        && (extension_loc = X509_get_ext_by_NID(cert, pci_old_NID, -1)) == -1)
    {
        /* no proxycertinfo extension found in cert */
        result = GLOBUS_SUCCESS;
        goto exit;
    }

    if((pci_extension = X509_get_ext(cert, 
                                     extension_loc)) == NULL)
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WITH_CRED,
            (_GCRSL("Can't find PROXYCERTINFO extension in X509 cert at "
             "expected location: %d in extension stack"), extension_loc));
        goto exit;
    }

    if((*proxycertinfo = X509V3_EXT_d2i(pci_extension)) == NULL)
    {
        GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_WITH_CRED,
            (_GCRSL("Can't convert DER encoded PROXYCERTINFO "
             "extension to internal form")));
        goto exit;
    }
    
 exit:
    
    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    return result;
}
/* globus_i_gsi_cred_get_proxycertinfo() */

int
globus_i_gsi_cred_password_callback_no_prompt(
    char *                              buffer,
    int                                 size,
    int                                 w)
{

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    /* current gsi implementation does not allow for a password
     * encrypted certificate to be used for authentication
     */

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    return -1;
}

static globus_result_t
globus_l_gsi_cred_subject_cmp(
    X509_NAME *                         actual_subject,
    X509_NAME *                         desired_subject)
{
    int                                 cn_index;
    char *                              desired_cn = NULL;
    char *                              actual_cn = NULL;
    char *                              desired_service;
    char *                              actual_service;
    char *                              desired_host;
    char *                              actual_host;
    char *                              desired_str = NULL;
    char *                              actual_str = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 length;

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    
    /* if desired subject is NULL return success */
    
    if(!desired_subject)
    {
        goto exit;
    }

    /* check for single /CN entry */
    
    if(X509_NAME_entry_count(desired_subject) == 1)
    {
        /* make sure we actually got a common name */

        cn_index = X509_NAME_get_index_by_NID(desired_subject, NID_commonName, -1);

        if(cn_index < 0)
        {
            desired_str = X509_NAME_oneline(desired_subject, NULL, 0);
            GLOBUS_GSI_CRED_ERROR_RESULT(
                result,
                GLOBUS_GSI_CRED_ERROR_SUBJECT_CMP,
                (_GCRSL("No Common Name found in desired subject %s.\n"), desired_str));
            goto exit;
        }

        /* find /CN entry in actual subject */

        cn_index = X509_NAME_get_index_by_NID(actual_subject, NID_commonName, -1);

        /* error if no common name was found */
        
        if(cn_index < 0)
        {
            actual_str = X509_NAME_oneline(actual_subject, NULL, 0);
            GLOBUS_GSI_CRED_ERROR_RESULT(
                result,
                GLOBUS_GSI_CRED_ERROR_SUBJECT_CMP,
                (_GCRSL("No Common Name found in subject %s.\n"), actual_str));
            goto exit;
        }

        /* check that actual subject only has one CN entry */
        
        if(X509_NAME_get_index_by_NID(actual_subject, NID_commonName, cn_index) != -1)
        {
            actual_str = X509_NAME_oneline(actual_subject, NULL, 0);
            GLOBUS_GSI_CRED_ERROR_RESULT(
                result,
                GLOBUS_GSI_CRED_ERROR_SUBJECT_CMP,
                (_GCRSL("More than one Common Name found in subject %s.\n"), actual_str));
            goto exit;
        }

        /* get CN text for desired subject */

        length = X509_NAME_get_text_by_NID(desired_subject, NID_commonName,
                                           NULL, 1024) + 1;
        
        desired_cn = malloc(length);

        X509_NAME_get_text_by_NID(desired_subject, NID_commonName,
                                  desired_cn, length);

        /* get CN text for actual subject */

        length = X509_NAME_get_text_by_NID(actual_subject, NID_commonName,
                                           NULL, 1024) + 1;
        
        actual_cn = malloc(length);

        X509_NAME_get_text_by_NID(actual_subject, NID_commonName,
                                  actual_cn, length);

        /* straight comparison */

        if(!strcmp(desired_cn,actual_cn))
        {
            goto exit;
        }

        actual_host = strchr(actual_cn,'/');

        if(actual_host == NULL)
        {
            actual_host = actual_cn;
            actual_service = NULL;
        }
        else
        {
            *actual_host = '\0';
            actual_service = actual_cn;
            actual_host++;
        }
        
        desired_host = strchr(desired_cn,'/');

        if(desired_host == NULL)
        {
            desired_host = desired_cn;
            desired_service = NULL;
        }
        else
        {
            *desired_host = '\0';
            desired_service = desired_cn;
            desired_host++;
        }
        
        if(desired_service == NULL &&
           actual_service == NULL)
        {
            actual_str = X509_NAME_oneline(actual_subject, NULL, 0);
            desired_str = X509_NAME_oneline(desired_subject, NULL, 0);

            GLOBUS_GSI_CRED_ERROR_RESULT(
                result,
                GLOBUS_GSI_CRED_ERROR_SUBJECT_CMP,
                (_GCRSL("Desired subject and actual subject of certificate"
                 " do not match.\n"
                 "     Desired subject: %s\n"
                 "     Actual subject: %s\n"),
                 desired_str,
                 actual_str));

            goto exit;
        }
        else if(desired_service == NULL)
        {
            if(strcmp("host",actual_service))
            {
                actual_str = X509_NAME_oneline(actual_subject, NULL, 0);
                desired_str = X509_NAME_oneline(desired_subject, NULL, 0);
                
                GLOBUS_GSI_CRED_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CRED_ERROR_SUBJECT_CMP,
                    (_GCRSL("Desired subject and actual subject of certificate"
                     " do not match.\n"
                     "     Desired subject: %s\n"
                     "     Actual subject: %s\n"),
                     desired_str,
                     actual_str));
            }
            
            goto exit;
        }
        else if(actual_service == NULL)
        {
            if(strcmp("host",desired_service))
            {
                actual_str = X509_NAME_oneline(actual_subject, NULL, 0);
                desired_str = X509_NAME_oneline(desired_subject, NULL, 0);
                
                GLOBUS_GSI_CRED_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CRED_ERROR_SUBJECT_CMP,
                    (_GCRSL("Desired subject and actual subject of certificate"
                     " do not match.\n"
                     "     Desired subject: %s\n"
                     "     Actual subject: %s\n"),
                     desired_str,
                     actual_str));
            }
            
            goto exit;            
        }
        else
        {
            if(strcmp(desired_service,actual_service))
            {
                actual_str = X509_NAME_oneline(actual_subject, NULL, 0);
                desired_str = X509_NAME_oneline(desired_subject, NULL, 0);
                
                GLOBUS_GSI_CRED_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CRED_ERROR_SUBJECT_CMP,
                    (_GCRSL("Desired subject and actual subject of certificate"
                     " do not match.\n"
                     "     Desired subject: %s\n"
                     "     Actual subject: %s\n"),
                     desired_str,
                     actual_str));
            }
            
            goto exit;

        }
    }
    else
    {
        /* full subject name, don't care about equivalence classes */

        if(X509_NAME_cmp(desired_subject, actual_subject))
        {
            actual_str = X509_NAME_oneline(actual_subject, NULL, 0);
            desired_str = X509_NAME_oneline(desired_subject, NULL, 0);
            
            GLOBUS_GSI_CRED_ERROR_RESULT(
                result,
                GLOBUS_GSI_CRED_ERROR_SUBJECT_CMP,
                (_GCRSL("Desired subject and actual subject of certificate"
                 " do not match.\n"
                 "     Desired subject: %s\n"
                 "     Actual subject: %s\n"),
                 desired_str,
                 actual_str));
        }
        goto exit;
    }
    
 exit:

    if(actual_cn)
    {
        free(actual_cn);
    }

    if(desired_cn)
    {
        free(desired_cn);
    }
    
    if(actual_str)
    {
        OPENSSL_free(actual_str);
    }

    if(desired_str)
    {
        OPENSSL_free(desired_str);
    }

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    
    return result;
}

static globus_result_t
globus_l_gsi_cred_get_service(
    X509_NAME *                         subject,
    char **                             service)
{
    int                                 cn_index;
    int                                 length;
    char *                              cn = NULL;
    char *                              host;
    char *                              subject_str = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_CRED_DEBUG_ENTER;

    *service = NULL;
    
    /* if desired subject is NULL return success */
    
    if(!subject)
    {
        goto exit;
    }

    /* find /CN entry in subject */

    cn_index = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);

    /* error if no common name was found */
        
    if(cn_index < 0)
    {
        subject_str = X509_NAME_oneline(subject, NULL, 0);
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_GETTING_SERVICE_NAME,
            (_GCRSL("No Common Name found in subject %s.\n"), subject_str));
        goto exit;
    }

    /* check that subject only has one CN entry */
        
    if(X509_NAME_get_index_by_NID(subject, NID_commonName, cn_index) != -1)
    {
        subject_str = X509_NAME_oneline(subject, NULL, 0);
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_GETTING_SERVICE_NAME,
            (_GCRSL("More than one Common Name found in subject %s.\n"), subject_str));
        goto exit;
    }

    /* get CN text for subject */

    length = X509_NAME_get_text_by_NID(subject, NID_commonName,
                                       NULL, 1024) + 1;
    
    cn = malloc(length);
    
    X509_NAME_get_text_by_NID(subject, NID_commonName,
                              cn, length);
    
    host = strchr(cn,'/');

    if(host == NULL)
    {
        subject_str = X509_NAME_oneline(subject, NULL, 0);
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_GETTING_SERVICE_NAME,
            (_GCRSL("No service name found in subject %s.\n"), subject_str));
        goto exit;
    }

    *host = '\0';

    if(strcmp("host",cn))
    {
        *service = strdup(cn);
        if (*service == NULL)
        {
            GLOBUS_GSI_CRED_ERROR_RESULT(
                result,
                GLOBUS_GSI_CRED_ERROR_GETTING_SERVICE_NAME,
                (_GCRSL("Error copying service name.\n")));
        }
    }
    else
    {
        subject_str = X509_NAME_oneline(subject, NULL, 0);
        GLOBUS_GSI_CRED_ERROR_RESULT(
            result,
            GLOBUS_GSI_CRED_ERROR_GETTING_SERVICE_NAME,
            (_GCRSL("No service name found in subject %s.\n"), subject_str));        
    }
    
    goto exit;

 exit:

    if(cn)
    {
        free(cn);
    }

    if(subject_str)
    {
        OPENSSL_free(subject_str);
    }

    GLOBUS_I_GSI_CRED_DEBUG_EXIT;
    
    return result;
}

static
globus_result_t
globus_l_credential_sort_cert_list(
    STACK_OF(X509) *                    certs)
{
    X509 *                              tmp_cert = NULL;
    X509 *                              tmp_signer = NULL;
    X509_NAME *                         candidate_issuer;
    X509_NAME *                         signer_subject;
    STACK_OF(X509) *                    ordered_certs;
    int                                 i, j, issuer_idx;

    ordered_certs = sk_X509_new_null();

    /* Iterate through the certificate stack, checking to see if we've already
     * seen its signer. If so, we put the new cert into the ordered_stack before
     * the signer. Otherwise, we stick it on the end of the stack
     */
    for (i = 0; i < sk_X509_num(certs); i++)
    {
        tmp_cert = sk_X509_value(certs, i);
        candidate_issuer = X509_get_issuer_name(tmp_cert);

        for (j = 0, issuer_idx = -1; j < sk_X509_num(ordered_certs); j++)
        {
            tmp_signer = sk_X509_value(ordered_certs, j);
            signer_subject = X509_get_subject_name(tmp_signer);

            if (X509_NAME_cmp(candidate_issuer, signer_subject) == 0)
            {
                issuer_idx = j;
                break;
            }
        }

        if (issuer_idx == -1)
        {
            sk_X509_push(ordered_certs, tmp_cert);
        }
        else
        {
            sk_X509_insert(ordered_certs, tmp_cert, issuer_idx);
        }
    }
    sk_X509_zero(certs);

    for (i = 0; i < sk_X509_num(ordered_certs); i++)
    {
        tmp_cert = sk_X509_value(ordered_certs, i);
        sk_X509_push(certs, tmp_cert);
    }
    sk_X509_free(ordered_certs);

    return GLOBUS_SUCCESS;
}
#endif
