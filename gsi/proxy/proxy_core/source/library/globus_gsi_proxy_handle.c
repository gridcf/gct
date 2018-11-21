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
 * @file globus_gsi_proxy_handle.c
 * @brief GSI Proxy Handle
 * @author Sam Meder, Sam Lang
 */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

#define GLOBUS_GSI_PROXY_HANDLE_MALLOC_ERROR(_LENGTH_) \
    globus_error_put(globus_error_wrap_errno_error( \
        GLOBUS_GSI_PROXY_MODULE, \
        errno, \
        GLOBUS_GSI_PROXY_ERROR_ERRNO, \
        __FILE__, \
        __func__, \
        __LINE__, \
        "Could not allocate enough memory: %d bytes", \
        _LENGTH_))

#include "globus_i_gsi_proxy.h"

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define EVP_PKEY_id(k) (k)->type
#endif
#if OPENSSL_VERSION_NUMBER < 0x10002000L
static
STACK_OF(X509_EXTENSION) *
sk_X509_EXTENSION_deep_copy(
        STACK_OF(X509_EXTENSION) *e,
        X509_EXTENSION *(*copy_func)(const X509_EXTENSION *),
        void (*free_func)(X509_EXTENSION *))
{
    STACK_OF(X509_EXTENSION)           *newe = sk_X509_EXTENSION_new_null();
    int                                 count = sk_X509_EXTENSION_num(e);

    if (newe == NULL)
    {
        return NULL;
    }
    for (int i = 0; i < count; i++)
    {
        sk_X509_EXTENSION_push(newe, copy_func(sk_X509_EXTENSION_value(e, i)));
    }
    return newe;
}
#endif
    
/**
 * @brief Initialize a GSI Proxy handle
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Initialize a proxy handle which can be used in subsequent
 * operations. The handle may only be used in one sequence of
 * operations at a time.
 *
 * @param[out] handle
 *        A pointer to the handle to be initialized.  If the
 *        handle is originally NULL, space is allocated for it.
 *        Otherwise, the current values of the handle are overwritten.
 *        
 * @param[in] handle_attrs
 *        Initial attributes to be used to create this handle.
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 *
 * @see globus_gsi_proxy_handle_destroy()
 */
globus_result_t
globus_gsi_proxy_handle_init(
    globus_gsi_proxy_handle_t *         handle,
    globus_gsi_proxy_handle_attrs_t     handle_attrs)
{
    globus_gsi_proxy_handle_t           handle_i;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    /* setup the handle */
    if(handle == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }

    *handle = calloc(1, sizeof(globus_i_gsi_proxy_handle_t));

    if(*handle == NULL)
    {
        result = GLOBUS_GSI_PROXY_HANDLE_MALLOC_ERROR(
                sizeof(globus_i_gsi_proxy_handle_t));
        goto exit;
    }

    handle_i = *handle; 

    /* initialize the X509 request structure */
    if((handle_i->req = X509_REQ_new()) == NULL)
    {
        GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_X509_REQ,
            (_PCSL("Couldn't create new X509_REQ structure for handle")));
        goto free_handle;
    }

    /* create a new PCI extension */
    if((handle_i->proxy_cert_info = PROXY_CERT_INFO_EXTENSION_new()) == NULL)
    {
        GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO,
            (_PCSL("Error initializing new PROXY_CERT_INFO_EXTENSION struct")));
        goto free_handle;
    }

    ASN1_OBJECT_free(handle_i->proxy_cert_info->proxyPolicy->policyLanguage); 
    handle_i->proxy_cert_info->proxyPolicy->policyLanguage = NULL;

    handle_i->proxy_cert_info->proxyPolicy->policyLanguage = OBJ_dup(
            OBJ_nid2obj(NID_id_ppl_inheritAll));
    
    /* initialize the handle attributes */
    if(handle_attrs == NULL)
    {
        result = globus_gsi_proxy_handle_attrs_init(&handle_i->attrs);
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_PROXY_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS);
            goto free_handle;
        }
    }
    else
    {
        result = globus_gsi_proxy_handle_attrs_copy(handle_attrs, 
                                                    &handle_i->attrs);
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_PROXY_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS);
            goto free_handle;
        }
    }

    /* Default to RFC3820 impersonation proxy, which is also the default for
     * grid-proxy-init */
    handle_i->type = GLOBUS_GSI_CERT_UTILS_TYPE_RFC_IMPERSONATION_PROXY;


    handle_i->extensions = NULL;
    
    goto exit;

 free_handle:

    if(handle_i)
    {
        globus_gsi_proxy_handle_destroy(handle_i);
        *handle = NULL;
    }

 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_init() */

/**
 * @brief Destroy a GSI Proxy handle.
 * @details
 *     Free the memory used by a GSI proxy handle.
 *
 * @param[in] handle
 *        The handle to be destroyed.
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 *
 * @see globus_gsi_proxy_handle_init()
 */
globus_result_t
globus_gsi_proxy_handle_destroy(
    globus_gsi_proxy_handle_t           handle)
{
    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle != NULL)
    {
        /* free each of the pointers in the handle struct */
        X509_REQ_free(handle->req);
        EVP_PKEY_free(handle->proxy_key);
        globus_gsi_proxy_handle_attrs_destroy(handle->attrs);
        PROXY_CERT_INFO_EXTENSION_free(handle->proxy_cert_info);
        
        free(handle->common_name);
        if (handle->extensions != NULL)
        {
            sk_X509_EXTENSION_free(handle->extensions);
        }
        
        /* free the handle struct memory */
        free(handle);
        handle = NULL;
    }

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return GLOBUS_SUCCESS;
}
/* globus_gsi_proxy_handle_destroy */


/**
 * @brief Get the certificate request from a GSI Proxy handle
 * @ingroup globus_gsi_proxy_handle
 * @details
 *     Copies the certificate request associated with the proxy handle to
 *     the req parameter.
 *
 * @param[in] handle
 *        The handle from which to get the certificate request
 * @param[out] req
 *        Parameter used to return the request. It is the users responsibility
 *        to free the returned request.
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 *
 * @see globus_gsi_proxy_handle_set_req()
 */
globus_result_t
globus_gsi_proxy_handle_get_req(
    globus_gsi_proxy_handle_t           handle,
    X509_REQ **                         req)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }

    if(!req)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_X509_REQ,
            (_PCSL("Invalid req pointer passed to function")));
        goto exit;
    }

    *req = X509_REQ_dup(handle->req);

    if(!(*req))
    {
        GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_X509_REQ,
            (_PCSL("X509_REQ could not be copied")));
        goto exit;
    }

 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_get_req */

/**
 * @brief Set Request
 * Set the certificate request in a GSI Proxy handle.
 *
 * @param[in] handle
 *        The handle for which to set the certificate request
 * @param[in] req
 *        Request to be copied to handle.
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 *
 * @see globus_gsi_proxy_handle_get_req()
 */
globus_result_t
globus_gsi_proxy_handle_set_req(
    globus_gsi_proxy_handle_t           handle,
    X509_REQ *                          req)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }
    
    if(handle->req)
    {
        X509_REQ_free(handle->req);
        handle->req = NULL;
    }

    if(req)
    {
        handle->req = X509_REQ_dup(req);
        if(!handle->req)
        {
            GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_X509_REQ,
                (_PCSL("Couldn't copy X509_REQ")));
            goto exit;
        }
    }
    
 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_set_req */


/**
 * @brief Get the private key from a GSI Proxy handle.
 * @ingroup globus_gsi_proxy_handle
 * @details
 *     Copies the private key associated with the proxy handle to the
 *     value pointed to by the proxy_key parameter.
 * @param[in] handle
 *        The handle from which to get the private key
 * @param[in] proxy_key
 *        Parameter used to return the key. It is the users responsibility to
 *        free the returned key by calling EVP_PKEY_free().
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 *
 * @see globus_gsi_proxy_handle_set_private_key()
 */
globus_result_t
globus_gsi_proxy_handle_get_private_key(
    globus_gsi_proxy_handle_t           handle,
    EVP_PKEY **                         proxy_key)
{
    int                                 length;
    unsigned char *                     der_encoded = NULL;
    unsigned char *                     tmp;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }
    
    if(!proxy_key)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_PRIVATE_KEY,
            (_PCSL("Invalid proxy_key (NULL) passed to function")));
        goto exit;
    }

    if(!handle->proxy_key)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_PRIVATE_KEY,
            (_PCSL("handle's proxy key hasn't been initialized")));
        goto exit;
    }

    *proxy_key = NULL;

    length = i2d_PrivateKey(handle->proxy_key, NULL);

    if(length < 0)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_PRIVATE_KEY,
            (_PCSL("Couldn't convert private key from internal"
             "to DER encoded form")));
        goto exit;
        
    }
    
    der_encoded = malloc(length);

    if(!der_encoded)
    {
        GLOBUS_GSI_PROXY_HANDLE_MALLOC_ERROR(length);
        goto exit;
    }

    tmp = der_encoded;

    length = i2d_PrivateKey(handle->proxy_key, &tmp);

    if(length < 0)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_PRIVATE_KEY,
            (_PCSL("Couldn't convert private key from internal"
             "to DER encoded form")));
        goto exit;
        
    }

    tmp = der_encoded;
    
    if(!d2i_PrivateKey(EVP_PKEY_id(handle->proxy_key), proxy_key, 
                       (const unsigned char **) &tmp, length))
    {
        GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_PRIVATE_KEY,
            (_PCSL("Error converting DER encoded private key to internal form")));
        goto exit;
    }
    
 exit:

    if(der_encoded)
    {
        free(der_encoded);
    }
    
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_get_private_key */

/**
 * @brief Set the private key in a GSI Proxy handle
 * @details
 *     Copies the private key pointed to by proxy_key to the 
 *     handle.
 * @param[in] handle
 *        The handle for which to set the private key
 * @param[in] proxy_key
 *        Parameter used to pass the key
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 *
 * @see globus_gsi_proxy_handle_get_private_key()
 */
globus_result_t
globus_gsi_proxy_handle_set_private_key(
    globus_gsi_proxy_handle_t           handle,
    const EVP_PKEY *                    proxy_key)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if (!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }

    if (handle->proxy_key != NULL)
    {
        EVP_PKEY_free(handle->proxy_key);
        handle->proxy_key = NULL;
    }
    
    if (proxy_key != NULL)
    {
        handle->proxy_key = ASN1_dup_of(
                EVP_PKEY, i2d_PrivateKey, d2i_AutoPrivateKey, proxy_key);

        
        if(handle->proxy_key == NULL)
        {
            GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_PRIVATE_KEY,
                (_PCSL("Error converting DER encoded private key to internal form")));
            goto exit;
        }
    }

exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_set_private_key */

/**
 * @brief Get Proxy Type
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Determine the type of proxy that will be generated when using this handle. 
 *
 * @param[in] handle
 *        The handle from which to get the type
 * @param[out] type
 *        Parameter used to return the type.
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 *
 * @see globus_gsi_proxy_handle_set_type()
 */
globus_result_t
globus_gsi_proxy_handle_get_type(
    globus_gsi_proxy_handle_t           handle,
    globus_gsi_cert_utils_cert_type_t * type)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }
    if (type == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid type (NULL) passed to function")));
        goto exit;
    }

    *type = handle->type;

 exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_get_type */

/**
 * @brief Get Proxy Type
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Set the type of proxy that will be generated when using this handle. Note
 * that this will have no effect when generating a proxy from a proxy. In that
 * case the generated proxy will inherit the type of the parent.
 *
 * @param handle
 *        The handle for which to set the type
 * @param type
 *        Parameter used to pass the type.
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 *
 * @see globus_gsi_proxy_handle_set_type()
 */
globus_result_t
globus_gsi_proxy_handle_set_type(
    globus_gsi_proxy_handle_t           handle,
    globus_gsi_cert_utils_cert_type_t   type)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }

    handle->type = type;

    switch(type)
    {
      case GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_IMPERSONATION_PROXY:
      case GLOBUS_GSI_CERT_UTILS_TYPE_RFC_IMPERSONATION_PROXY:
        result = globus_gsi_proxy_handle_set_policy(
            handle, NULL, 0, OBJ_txt2nid(IMPERSONATION_PROXY_OID));
        break;

      case GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_INDEPENDENT_PROXY:
      case GLOBUS_GSI_CERT_UTILS_TYPE_RFC_INDEPENDENT_PROXY:
        result = globus_gsi_proxy_handle_set_policy(
            handle, NULL, 0, OBJ_txt2nid(INDEPENDENT_PROXY_OID));
        break;

      case GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_LIMITED_PROXY:
      case GLOBUS_GSI_CERT_UTILS_TYPE_RFC_LIMITED_PROXY:
        result = globus_gsi_proxy_handle_set_policy(
            handle, NULL, 0, OBJ_txt2nid(LIMITED_PROXY_OID));
        break;
      default:
        break;
    }

 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_set_type */

/**
 * @brief Set Policy
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Set the policy to be used in the GSI Proxy handle.
 *
 * This function sets the policy to be used in the proxy cert
 * info extension.
 *
 * @param handle
 *        The handle to be modified.
 * @param policy_data
 *        The policy data.
 * @param policy_length
 *        The length of the policy data
 * @param policy_language_NID
 *        The NID of the policy language.
 *
 * @return
 *        GLOBUS_SUCCESS if the handle and its associated fields are valid
 *        otherwise an error is returned
 *
 * @see globus_gsi_proxy_handle_get_policy()
 */
globus_result_t
globus_gsi_proxy_handle_set_policy(
    globus_gsi_proxy_handle_t           handle,
    const unsigned char *               policy_data,
    int                                 policy_length,
    int                                 policy_language_NID)
{
    PROXY_POLICY *                      policy = NULL;
    ASN1_OBJECT *                       policy_object = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;
    
    if(handle == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }
    
    if (handle->proxy_cert_info->proxyPolicy == NULL)
    {
        handle->proxy_cert_info->proxyPolicy = PROXY_POLICY_new();
    }
    policy = handle->proxy_cert_info->proxyPolicy;

    policy_object = OBJ_nid2obj(policy_language_NID);
    if (policy_object == NULL)
    {
        GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_PROXYPOLICY,
            (_PCSL("Invalid numeric ID: %d"), policy_language_NID));
        goto exit;
    }

    ASN1_OBJECT_free(policy->policyLanguage);
    policy->policyLanguage = OBJ_dup(policy_object);
    if (policy->policyLanguage  == NULL)
    {
        GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_PROXYPOLICY,
            (_PCSL("PROXYPOLICY of proxy handle could not be initialized")));
        goto exit;
    }

    if(policy->policy)
    {
        ASN1_OCTET_STRING_free(policy->policy);
        policy->policy = NULL;
    }
    if(policy_data != NULL)
    {
        policy->policy = ASN1_OCTET_STRING_new();
        ASN1_OCTET_STRING_set(policy->policy, policy_data, policy_length);

    }
    result = GLOBUS_SUCCESS;

 exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_set_policy */

/**
 * @brief Get Policy
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Get the policy from the GSI Proxy handle.
 *
 * This function gets the policy that is being used in the 
 * proxy cert info extension.
 *
 * @param handle
 *        The handle to be interrogated.
 * @param policy_data
 *        The policy data.
 * @param policy_length
 *        The length of the returned policy
 * @param policy_NID
 *        The NID of the policy language.
 *
 * @return
 *        GLOBUS_SUCCESS if the handle is valid, otherwise an error
 *        is returned
 *
 * @see globus_gsi_proxy_handle_set_policy()
 */
globus_result_t
globus_gsi_proxy_handle_get_policy(
    globus_gsi_proxy_handle_t           handle,
    unsigned char **                    policy_data,
    int *                               policy_length,
    int *                               policy_NID)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }

    if (policy_data == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL policy_data passed to function: %s"), __func__));
        goto exit;
    }
    if (policy_length == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL policy_length passed to function: %s"), __func__));
        goto exit;
    }
    if (policy_NID == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL policy_NID passed to function: %s"), __func__));
        goto exit;
    }

    if (handle->proxy_cert_info->proxyPolicy->policy != NULL)
    {
        *policy_length = ASN1_STRING_length(
                handle->proxy_cert_info->proxyPolicy->policy);
        *policy_data = malloc(*policy_length + 1);
        
        memcpy(*policy_data,
                ASN1_STRING_data(handle->proxy_cert_info->proxyPolicy->policy),
                *policy_length);
        (*policy_data)[*policy_length] = 0;
    }
    else
    {
        *policy_length = 0;
        *policy_data = NULL;
    }

    *policy_NID = OBJ_obj2nid(
            handle->proxy_cert_info->proxyPolicy->policyLanguage);
    
 exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_get_policy */

/**
 * @brief Add X.509 Extensions
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Add an X.509 extension to the GSI Proxy handle to be added to certificate
 *
 * This function adds a X.509 extension to the proxy certificate.
 *
 * @param handle
 *        The handle for the proxy to which the extension should be added.
 * @param ext
 *        The extension to be added.
 *
 * @return
 *        GLOBUS_SUCCESS if the addition was successful, otherwise an
 *        error is returned.
 *
 * @see globus_gsi_proxy_handle_get_extensions()
 * @see globus_gsi_proxy_handle_set_extensions()
 */
globus_result_t
globus_gsi_proxy_handle_add_extension(
    globus_gsi_proxy_handle_t           handle,
    X509_EXTENSION *                    ext)
{
    globus_result_t                     result;


    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }

    if (ext == NULL)
    {
        /* Nothing to do */
        result = GLOBUS_SUCCESS;
        goto exit;
    }

    if (handle->extensions == NULL)
    {
        handle->extensions = sk_X509_EXTENSION_new_null();

        if (handle->extensions == NULL)
        {
            GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
                (_PCSL("Failed to allocation new X.509 Extension stack: %s"), __func__));
            goto exit;
        }
    }
    
    sk_X509_EXTENSION_push(handle->extensions,
                           X509_EXTENSION_dup(ext));
    result = GLOBUS_SUCCESS;

  exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/* globus_gsi_proxy_handle_add_extension */

/**
 * @brief Set X.509 Extensions
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Set the X.509 extensions from a GSI Proxy handle
 *
 * This function sets the X.509 extensions for a proxy certificate.
 *
 * @param handle
 *        The handle for the proxy from which the extension should be set.
 * @param exts
 *        The extensions to be set. Can be NULL to clear extensions.
 *
 * @return
 *        GLOBUS_SUCCESS if the addition was successful, otherwise an
 *        error is returned.
 *
 * @see globus_gsi_proxy_handle_add_extension()
 * @see globus_gsi_proxy_handle_get_extensions()
 */
globus_result_t
globus_gsi_proxy_handle_set_extensions(
    globus_gsi_proxy_handle_t           handle,
    STACK_OF(X509_EXTENSION)*           exts)
{
    globus_result_t                     result;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }

    if (handle->extensions != NULL)
    {
        sk_X509_EXTENSION_free(handle->extensions);
    }
    
    if (exts == NULL)
    {
        handle->extensions = NULL;
    }
    else
    {
        handle->extensions = sk_X509_EXTENSION_deep_copy(
                exts,
                (X509_EXTENSION *(*)(const X509_EXTENSION *)) X509_EXTENSION_dup,
                X509_EXTENSION_free);
        
        if (handle->extensions == NULL)
        {
            GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
                (_PCSL("Failed to duplicate X.509 Extension stack: %s"), __func__));
            goto exit;
        }
    }

    result = GLOBUS_SUCCESS;

  exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/* globus_gsi_proxy_handle_set_extensions */

/**
 * @brief Get X.509 Extensions
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Get the X.509 extensions from a GSI Proxy handle
 *
 * This function returns the X.509 extensions from the proxy certificate.
 *
 * @param handle
 *        The handle for the proxy from which the extensions should be
 *        retrieved.
 * @param exts
 *        The variable to hold the extensions. The caller is responsible
 *        for freeing the extensions with sk_X509_EXTENSION_free()
 *        when they are done with them.
 *
 * @return
 *        GLOBUS_SUCCESS if the retrieval was successful, otherwise an
 *        error is returned.
 *
 * @see globus_gsi_proxy_handle_add_extension()
 * @see globus_gsi_proxy_handle_set_extensions()
 */
globus_result_t
globus_gsi_proxy_handle_get_extensions(
    globus_gsi_proxy_handle_t           handle,
    STACK_OF(X509_EXTENSION)**          exts)
{
    globus_result_t                     result;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }
    if (exts == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL exts passed to function: %s"), __func__));
        goto exit;
    }
    
    if (handle->extensions == NULL)
    {
        *exts = sk_X509_EXTENSION_new_null();
    }
    else
    {
        *exts = sk_X509_EXTENSION_dup(handle->extensions);
    }
    
    if (*exts == NULL)
    {
        GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Failed to duplicate X.509 Extension stack: %s"), __func__));
        goto exit;
    }

    result = GLOBUS_SUCCESS;

  exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/* globus_gsi_proxy_handle_get_extensions */

/**
 * @brief Set Path Length
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Set the path length to be used in the GSI Proxy handle.
 *
 * This function sets the path length to be used in the proxy
 * cert info extension.
 *
 * @param handle
 *        The handle to be modified.
 * @param pathlen
 *        The maximum allowable path length
 * @return
 *        GLOBUS_SUCCESS if the handle is valid, otherwise an
 *        error is returned
 *
 * @see globus_gsi_proxy_handle_get_pathlen()
 */
globus_result_t
globus_gsi_proxy_handle_set_pathlen(
    globus_gsi_proxy_handle_t           handle,
    long                                pathlen)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }

    if (handle->proxy_cert_info->pcPathLengthConstraint == NULL)
    {
        handle->proxy_cert_info->pcPathLengthConstraint =
            ASN1_INTEGER_new();
    }

    if (handle->proxy_cert_info->pcPathLengthConstraint == NULL)
    {
        GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_PATHLENGTH,
            (_PCSL("Error setting the path length of the PROXYCERTINFO extension "
             "in the proxy handle")));
        goto exit;
    }
    ASN1_INTEGER_set(handle->proxy_cert_info->pcPathLengthConstraint, pathlen);


exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_set_pathlen */

/**
 * @brief Get Path Length
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Get the path length from the GSI Proxy handle.
 *
 * This function gets the path length that is being used in the 
 * proxy cert info extension.
 *
 * @param handle
 *        The handle to be interrogated.
 * @param pathlen
 *        The maximum allowable path length
 * @return
 *        GLOBUS_SUCCESS if the handle is valid, otherwise an
 *        error is returned
 *
 * @see globus_gsi_proxy_handle_set_pathlen()
 */
globus_result_t
globus_gsi_proxy_handle_get_pathlen(
    globus_gsi_proxy_handle_t           handle,
    int *                               pathlen)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }
    if (pathlen == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL pathlen passed to function: %s"), __func__));
        goto exit;
    }
    if (handle->proxy_cert_info->pcPathLengthConstraint == NULL)
    {
        *pathlen = -1;
    }
    else
    {
        *pathlen = ASN1_INTEGER_get(
                handle->proxy_cert_info->pcPathLengthConstraint);
    }
    
exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_get_pathlen */

/**
 * @brief Get Time Valid
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Get the validity time of the proxy
 *
 * @param handle
 *        The proxy handle to get the expiration date of
 * @param time_valid
 *        expiration date of the proxy handle
 * 
 * @result
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 */
globus_result_t
globus_gsi_proxy_handle_get_time_valid(
    globus_gsi_proxy_handle_t           handle,
    int *                               time_valid)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }
    if(time_valid == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL time_valid passed to function: %s"), __func__));
        goto exit;
    }

    *time_valid = handle->time_valid;

 exit:        
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_get_time_valid */

/**
 * @brief Set Time Valid
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Set the validity time of the proxy
 *
 * @param handle
 *        The proxy handle to set the expiration date for
 * @param time_valid
 *        desired expiration date of the proxy
 * 
 * @result
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 *        GLOBUS_SUCCESS
 */
globus_result_t
globus_gsi_proxy_handle_set_time_valid(
    globus_gsi_proxy_handle_t           handle,
    int                                 time_valid)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    
    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }

    handle->time_valid = time_valid;

 exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_set_time_valid */

/**
 * @brief Clear Cert Info
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Clear the proxy cert info extension stored in the GSI Proxy handle.
 *
 * This function clears proxy cert info extension related setting in
 * the GSI Proxy handle.
 *
 * @param handle
 *        The handle for which to clear the proxy cert info extension.
 * @return
 *        GLOBUS_SUCCESS if the handle is valid, otherwise an
 *        error is returned
 */
globus_result_t
globus_gsi_proxy_handle_clear_cert_info(
    globus_gsi_proxy_handle_t           handle)
{
    globus_result_t                     result;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("NULL handle passed to function: %s"), __func__));
        goto exit;
    }

    PROXY_CERT_INFO_EXTENSION_free(handle->proxy_cert_info);
    handle->proxy_cert_info = PROXY_CERT_INFO_EXTENSION_new();
    if(handle->proxy_cert_info == NULL)
    {
        GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO,
            (_PCSL("PROXYCERTINFO could not be initialized")));
        goto exit;
    }
    ASN1_OBJECT_free(handle->proxy_cert_info->proxyPolicy->policyLanguage); 
    handle->proxy_cert_info->proxyPolicy->policyLanguage = NULL;

    handle->proxy_cert_info->proxyPolicy->policyLanguage = OBJ_dup(
            OBJ_nid2obj(NID_id_ppl_inheritAll));

    result = GLOBUS_SUCCESS;

 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_clear_cert_info */

#ifdef USE_SYMBOL_VERSIONS
__asm__(".symver globus_gsi_proxy_handle_get_proxy_cert_info_proxy_ssl,"
        "globus_gsi_proxy_handle_get_proxy_cert_info@GLOBUS_GSI_PROXY_CORE_7");
__asm__(".symver globus_gsi_proxy_handle_set_proxy_cert_info_proxy_ssl,"
        "globus_gsi_proxy_handle_set_proxy_cert_info@GLOBUS_GSI_PROXY_CORE_7");

__asm__(".symver globus_gsi_proxy_handle_set_proxy_cert_info_openssl,"
        "globus_gsi_proxy_handle_set_proxy_cert_info@@GLOBUS_GSI_PROXY_CORE_8");
__asm__(".symver globus_gsi_proxy_handle_get_proxy_cert_info_openssl,"
        "globus_gsi_proxy_handle_get_proxy_cert_info@@GLOBUS_GSI_PROXY_CORE_8");
#else

#ifdef globus_gsi_proxy_handle_set_proxy_cert_info
#undef globus_gsi_proxy_handle_set_proxy_cert_info
#endif

#ifdef globus_gsi_proxy_handle_get_proxy_cert_info
#undef globus_gsi_proxy_handle_get_proxy_cert_info
#endif

#define globus_gsi_proxy_handle_set_proxy_cert_info_openssl \
        globus_gsi_proxy_handle_set_proxy_cert_info

#define globus_gsi_proxy_handle_get_proxy_cert_info_openssl \
        globus_gsi_proxy_handle_get_proxy_cert_info
#endif

/**
 * @brief Get Cert Info
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Get the proxy cert info extension stored in the GSI Proxy handle.
 *
 * This function retrieves the proxy cert info extension from the GSI Proxy
 * handle. 
 *
 * @param handle
 *        The handle from which to get the proxy cert info extension.
 * @param pci
 *        Contains the proxy cert info extension upon successful return. If the
 *        handle does not contain a pci extension, this parameter will be NULL
 *        upon return.
 * @return
 *        GLOBUS_SUCCESS upon success
 *        GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE if handle is invalid
 *        GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO if the pci
 *        pointer is invalid or if the get failed.
 */
globus_result_t
globus_gsi_proxy_handle_get_proxy_cert_info_openssl(
    globus_gsi_proxy_handle_t           handle,
    PROXY_CERT_INFO_EXTENSION **        pci)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;
    
    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }

    if(!pci)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO,
            (_PCSL("Invalid PROXYCERTINFO passed to function")));
        goto exit;
    }

    if(handle->proxy_cert_info)
    { 
        *pci = ASN1_dup_of(PROXY_CERT_INFO_EXTENSION,
                i2d_PROXY_CERT_INFO_EXTENSION,
                d2i_PROXY_CERT_INFO_EXTENSION,
                handle->proxy_cert_info);
        if(!*pci)
        {
            GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO,
                (_PCSL("Couldn't copy PROXYCERTINFO structure")));
            goto exit;
        }
    }
    else
    {
        *pci = NULL;
    }

 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_get_proxy_cert_info_openssl() */

globus_result_t
globus_gsi_proxy_handle_get_proxy_cert_info_proxy_ssl(
    globus_gsi_proxy_handle_t           handle,
    PROXYCERTINFO **                    pci)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    PROXYCERTINFO                      *pci_new = NULL;
    PROXY_CERT_INFO_EXTENSION          *pci_copy = NULL;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if (!pci)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO,
            (_PCSL("Invalid PROXYCERTINFO passed to function")));
        goto exit;
    }
    *pci = NULL;
    
    if (!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }

    if(handle->proxy_cert_info)
    {
        pci_copy = ASN1_dup_of(
                PROXY_CERT_INFO_EXTENSION,
                i2d_PROXY_CERT_INFO_EXTENSION,
                d2i_PROXY_CERT_INFO_EXTENSION,
                handle->proxy_cert_info);
        if (!pci_copy)
        {
            GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO,
                (_PCSL("Couldn't copy PROXYCERTINFO structure")));
            goto exit;
        }
        pci_new = PROXYCERTINFO_new();
        if (pci_new == NULL)
        {
            GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO,
                (_PCSL("Couldn't copy PROXYCERTINFO structure")));
            goto exit;
        }
        pci_new->path_length = pci_copy->pcPathLengthConstraint;
        pci_copy->pcPathLengthConstraint = NULL;

        if (pci_new->policy == NULL)
        {
            pci_new->policy = PROXYPOLICY_new();
        }
        if (pci_new->policy == NULL)
        {
            GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO,
                (_PCSL("Couldn't copy PROXYCERTINFO structure")));

            goto free_pci_new_exit;
        }
        pci_new->policy->policy_language =
                pci_copy->proxyPolicy->policyLanguage;
        pci_copy->proxyPolicy->policyLanguage = NULL;

        pci_new->policy->policy = pci_copy->proxyPolicy->policy;
        pci_copy->proxyPolicy->policy = NULL;
    }

    if (result != GLOBUS_SUCCESS)
    {
free_pci_new_exit:
        PROXYCERTINFO_free(pci_new);
        pci_new = NULL;
    }
    PROXY_CERT_INFO_EXTENSION_free(pci_copy);
    *pci = pci_new;

 exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_get_proxy_cert_info_proxy_ssl() */

/**
 * @brief Set Cert Info
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Set the proxy cert info extension stored in the GSI Proxy handle.
 *
 * This function sets the proxy cert info extension in the GSI Proxy handle.
 *
 * @param handle
 *        The handle for which to set the proxy cert info extension.
 * @param pci
 *        The proxy cert info extension to set.
 * @retval GLOBUS_SUCCESS Success
 * @retval GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE Handle is invalid
 * @retval GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO pci pointer is invalid or if the set failed.
 */
globus_result_t
globus_gsi_proxy_handle_set_proxy_cert_info_openssl(
    globus_gsi_proxy_handle_t           handle,
    PROXY_CERT_INFO_EXTENSION *         pci)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }

    if(handle->proxy_cert_info)
    {
        PROXY_CERT_INFO_EXTENSION_free(handle->proxy_cert_info);
        handle->proxy_cert_info = NULL;
    }
    
    if(pci)
    {
        handle->proxy_cert_info = ASN1_dup_of(PROXY_CERT_INFO_EXTENSION,
                i2d_PROXY_CERT_INFO_EXTENSION,
                d2i_PROXY_CERT_INFO_EXTENSION,
                pci);
        if (handle->proxy_cert_info == NULL)
        {
            GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO,
                (_PCSL("Couldn't copy PROXYCERTINFO")));
            goto exit;
        }
    }
    else
    {
        if((handle->proxy_cert_info = PROXY_CERT_INFO_EXTENSION_new()) == NULL)
        {
            GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO,
                (_PCSL("Error initializing new PROXY_CERT_INFO_EXTENSION struct")));
            goto exit;
        }

        ASN1_OBJECT_free(handle->proxy_cert_info->proxyPolicy->policyLanguage); 
        handle->proxy_cert_info->proxyPolicy->policyLanguage = NULL;

        handle->proxy_cert_info->proxyPolicy->policyLanguage = OBJ_dup(
                OBJ_nid2obj(NID_id_ppl_inheritAll));
    }

 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_set_proxy_cert_info_openssl */

globus_result_t
globus_gsi_proxy_handle_set_proxy_cert_info_proxy_ssl(
    globus_gsi_proxy_handle_t           handle,
    PROXYCERTINFO *                     pci)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }

    if(handle->proxy_cert_info)
    {
        PROXY_CERT_INFO_EXTENSION_free(handle->proxy_cert_info);
        handle->proxy_cert_info = NULL;
    }
    
    if(pci)
    {
        PROXY_POLICY pp = 
        {
            .policyLanguage = pci->policy ? pci->policy->policy_language : NULL,
            .policy = pci->policy ? pci->policy->policy : NULL,
        };
        PROXY_CERT_INFO_EXTENSION real_pci =
        {
            .pcPathLengthConstraint = pci->path_length,
            .proxyPolicy = pci->policy ? &pp : NULL
        };
        handle->proxy_cert_info = ASN1_dup_of(PROXY_CERT_INFO_EXTENSION,
                i2d_PROXY_CERT_INFO_EXTENSION,
                d2i_PROXY_CERT_INFO_EXTENSION,
                &real_pci);
        if (handle->proxy_cert_info == NULL)
        {
            GLOBUS_GSI_PROXY_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_PROXY_ERROR_WITH_PROXYCERTINFO,
                (_PCSL("Couldn't copy PROXYCERTINFO")));
            goto exit;
        }
    }

 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_set_proxy_cert_info_proxy_ssl */

/**
 * @brief Get Signing Algorithm
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Get the signing algorithm used to sign the proxy cert request
 *
 * @param handle
 *        The proxy handle containing the type of signing algorithm used
 * @param signing_algorithm
 *        signing algorithm of the proxy handle
 * 
 * @retval
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 */
globus_result_t
globus_gsi_proxy_handle_get_signing_algorithm(
    globus_gsi_proxy_handle_t           handle,
    const EVP_MD **                     signing_algorithm)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    
    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }
    if(!signing_algorithm)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_INVALID_PARAMETER,
            (_PCSL("Invalid signing_algorithm (NULL) passed to function")));
        goto exit;
    }
    result = globus_gsi_proxy_handle_attrs_get_signing_algorithm(
        handle->attrs,
        signing_algorithm);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_PROXY_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS);
    }
        
exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/**
 * @brief Get Key Bits
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Get the key bits used for the pub/private key pair of the proxy
 *
 * @param handle
 *        The proxy handle to get the key bits of
 * @param key_bits
 *        key bits of the proxy handle
 * 
 * @result
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 *        GLOBUS_SUCCESS
 */
globus_result_t
globus_gsi_proxy_handle_get_keybits(
    globus_gsi_proxy_handle_t           handle,
    int *                               key_bits)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }
    if(!key_bits)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_INVALID_PARAMETER,
            (_PCSL("Invalid key_bits (NULL) passed to function")));
        goto exit;
    }
    result = globus_gsi_proxy_handle_attrs_get_keybits(handle->attrs,
                                                       key_bits);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_PROXY_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS);
    }
        
exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/**
 * @brief Get Init Prime
 * @ingroup globus_gsi_proxy_handle 
 * @details
 * Get the init prime of the proxy handle
 *
 * @param handle
 *        The handle to get the init prime used in generating the key pair
 * @param init_prime
 *        The resulting init prime
 *
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case
 *        an error object identifier (in the form of a globus_result_t)
 *        is returned
 */
globus_result_t
globus_gsi_proxy_handle_get_init_prime(
    globus_gsi_proxy_handle_t           handle,
    int *                               init_prime)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }
    if(!init_prime)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_INVALID_PARAMETER,
            (_PCSL("Invalid init_prime (NULL) passed to function")));
        goto exit;
    }
    result = globus_gsi_proxy_handle_attrs_get_init_prime(handle->attrs,
                                                          init_prime);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_PROXY_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS);
    }

exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}


/**
 * @brief Get Clock Skew
 * @ingroup globus_gsi_proxy_handle  
 * @details
 * Get the clock skew of the proxy handle
 *
 * @param handle
 *        The handle to get the clock skew of
 * @param skew
 *        The resulting clock skew
 *
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case
 *        an error object identifier (in the form of a globus_result_t)
 *        is returned
 */
globus_result_t
globus_gsi_proxy_handle_get_clock_skew_allowable(
    globus_gsi_proxy_handle_t           handle,
    int *                               skew)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;
    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }
    if(!skew)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_INVALID_PARAMETER,
            (_PCSL("Invalid skew (NULL) passed to function")));
        goto exit;
    }

    result = globus_gsi_proxy_handle_attrs_get_clock_skew_allowable(
        handle->attrs,
        skew);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_PROXY_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS);
    }

exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/**
 * @brief Get Callback for Creating Keys
 * @ingroup globus_gsi_proxy_handle 
 * @details
 * Get the callback for creating the public/private key pair
 *
 * @param handle
 *        The proxy handle to get the callback from
 * @param callback
 *        Parameter used for returning the callback
 *
 * @result
 *        GLOBUS_SUCCESS or an error object identifier
 */
globus_result_t
globus_gsi_proxy_handle_get_key_gen_callback(
    globus_gsi_proxy_handle_t           handle,
    void                                (**callback)(int, int, void *))
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }
    if(!callback)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_INVALID_PARAMETER,
            (_PCSL("Invalid callback (NULL) passed to function")));
        goto exit;
    }
    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    result = globus_gsi_proxy_handle_attrs_get_key_gen_callback(
        handle->attrs,
        callback);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_PROXY_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS);
    }

exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
} 


/**
 * @brief Get/Set Proxy Common Name
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Get the proxy common name stored in the GSI Proxy handle.
 *
 * This function retrieves the proxy common name from the GSI Proxy
 * handle. The common name only impacts draft compliant proxies.
 *
 * @param handle
 *        The handle from which to get the proxy common name.
 * @param common_name
 *        Contains the proxy common name upon successful return. If the
 *        handle does not contain a common name, this parameter will be NULL
 *        upon return.
 * @return
 *        GLOBUS_SUCCESS upon success
 *        GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE if handle is invalid
 */
globus_result_t
globus_gsi_proxy_handle_get_common_name(
    globus_gsi_proxy_handle_t           handle,
    char **                             common_name)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;
    
    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }

    if(!common_name)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_INVALID_PARAMETER,
            (_PCSL("Invalid common name passed to function")));
        goto exit;
    }

    if(handle->common_name)
    { 
        *common_name = strdup(handle->common_name);
        if(!*common_name)
        {
            result = GLOBUS_GSI_PROXY_HANDLE_MALLOC_ERROR(
                strlen(handle->common_name));
            goto exit;
        }
    }
    else
    {
        *common_name = NULL;
    }

 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_get_common_name */

/**
 * Set the proxy common name stored in the GSI Proxy handle.
 *
 * This function sets the proxy common name in the GSI Proxy handle. Note
 * that the common name is only used for draft compliant proxies.
 *
 * @param handle
 *        The handle for which to set the proxy common name.
 * @param common_name
 *        The proxy common name to set.
 * @return
 *        GLOBUS_SUCCESS upon success
 *        GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE if handle is invalid
 */
globus_result_t
globus_gsi_proxy_handle_set_common_name(
    globus_gsi_proxy_handle_t           handle,
    const char *                        common_name)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }

    if(handle->common_name)
    {
        free(handle->common_name);
        handle->common_name = NULL;
    }
    
    if(common_name)
    {
        handle->common_name = strdup(common_name);
        if(!handle->common_name)
        {
            result = GLOBUS_GSI_PROXY_HANDLE_MALLOC_ERROR(
                strlen(common_name));
            goto exit;
        }
    } 

 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_set_common_name */


/**
 * @brief Set/Check Proxy Is Limited
 * @ingroup globus_gsi_proxy_handle
 * @details
 * Set the limited proxy flag on the proxy handle
 *
 * @param handle
 *        the proxy handle
 * @param is_limited
 *        boolean value to set on the proxy handle
 *
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 */
globus_result_t
globus_gsi_proxy_handle_set_is_limited(
    globus_gsi_proxy_handle_t           handle,
    globus_bool_t                       is_limited)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }

    if(is_limited == GLOBUS_TRUE)
    {
        if(GLOBUS_GSI_CERT_UTILS_IS_RFC_PROXY(handle->type))
        {
            result = globus_gsi_proxy_handle_set_type(
                handle,
                GLOBUS_GSI_CERT_UTILS_TYPE_RFC_LIMITED_PROXY);
        }
        else if(GLOBUS_GSI_CERT_UTILS_IS_GSI_3_PROXY(handle->type))
        {
            result = globus_gsi_proxy_handle_set_type(
                handle,
                GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_LIMITED_PROXY);
        }
        else
        {
            result = globus_gsi_proxy_handle_set_type(
                handle,
                GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2_LIMITED_PROXY);
        }
    }
    else
    {
        if(GLOBUS_GSI_CERT_UTILS_IS_RFC_PROXY(handle->type))
        {
            result = globus_gsi_proxy_handle_set_type(
                handle,
                GLOBUS_GSI_CERT_UTILS_TYPE_RFC_IMPERSONATION_PROXY);
        }
        else if(GLOBUS_GSI_CERT_UTILS_IS_GSI_3_PROXY(handle->type))
        {
            result = globus_gsi_proxy_handle_set_type(
                handle,
                GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_IMPERSONATION_PROXY);
        }
        else
        {
            result = globus_gsi_proxy_handle_set_type(
                handle,
                GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2_PROXY);
        }        
    }

 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}


/**
 * Check to see  the proxy is a limited proxy 
 *
 * @param handle
 *        the proxy handle to check
 * @param is_limited
 *        boolean value to set depending on the type of proxy 
 *
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 */
globus_result_t
globus_gsi_proxy_is_limited(
    globus_gsi_proxy_handle_t           handle,
    globus_bool_t *                     is_limited)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(!handle)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE,
            (_PCSL("Invalid handle (NULL) passed to function")));
        goto exit;
    }
    if (!is_limited)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_INVALID_PARAMETER,
            (_PCSL("Invalid is_limited passed to function")));
        goto exit;
    }

    *is_limited = GLOBUS_GSI_CERT_UTILS_IS_LIMITED_PROXY(handle->type);

exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
