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
 * @file globus_gsi_proxy_handle_attrs.c
 * @brief GSI Proxy Handle Attributes
 */
#endif

#include "globus_i_gsi_proxy.h"
#include <errno.h>

#define DEFAULT_KEY_BITS                2048
#define DEFAULT_PUB_EXPONENT            RSA_F4  /* 65537 */
#define DEFAULT_SIGNING_ALGORITHM       NULL
#define DEFAULT_CLOCK_SKEW              (5*60)    /* actually in seconds */

#define GLOBUS_GSI_PROXY_HANDLE_ATTRS_MALLOC_ERROR \
    globus_error_put(globus_error_wrap_errno_error( \
        GLOBUS_GSI_PROXY_MODULE, \
        errno, \
        GLOBUS_GSI_PROXY_ERROR_ERRNO, \
        __FILE__, \
        __func__, \
        __LINE__, \
        "Could not allocate enough memory: %d bytes", \
        len))

/**
 * @brief Initialize Attributes
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Initialize GSI Proxy Handle Attributes.
 *
 * Initialize proxy handle attributes, which
 * can (and should) be associated with a proxy handle.
 * For most purposes, these attributes should primarily
 * be used by the proxy handle. 
 *
 * Currently, no attribute values are initialized.
 *
 * @param handle_attrs
 *        The handle attributes structure to be initialized
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case, 
 *        a globus error object ID is returned
 *
 * @see globus_gsi_proxy_handle_attrs_destroy()
 */
globus_result_t
globus_gsi_proxy_handle_attrs_init(
    globus_gsi_proxy_handle_attrs_t *   handle_attrs)
{
    globus_result_t                     result;
    globus_gsi_proxy_handle_attrs_t     attrs;
    int                                 len;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle_attrs == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"), 
             __func__));
        goto exit;
    }

    len = sizeof(globus_i_gsi_proxy_handle_attrs_t);
    if((*handle_attrs = (globus_gsi_proxy_handle_attrs_t)
       malloc(len)) == NULL)
    {
        result = GLOBUS_GSI_PROXY_HANDLE_ATTRS_MALLOC_ERROR;
        goto exit;
    }

    attrs = *handle_attrs;

    attrs->key_bits = DEFAULT_KEY_BITS;
    attrs->init_prime = DEFAULT_PUB_EXPONENT;
    attrs->signing_algorithm = DEFAULT_SIGNING_ALGORITHM;
    attrs->clock_skew = DEFAULT_CLOCK_SKEW;
    attrs->key_gen_callback = NULL;
    
    result = GLOBUS_SUCCESS;
   
 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_init() */

/**
 * @brief Destroy attributes
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Destroy the GSI Proxy handle attributes
 *
 * @param handle_attrs
 *        The handle attributes to be destroyed.
 * @return 
 *        GLOBUS_SUCCESS
 *
 * @see globus_gsi_proxy_handle_attrs_init()
 */
globus_result_t
globus_gsi_proxy_handle_attrs_destroy(
    globus_gsi_proxy_handle_attrs_t     handle_attrs)
{
    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    free(handle_attrs);

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return GLOBUS_SUCCESS;
}
/* globus_gsi_proxy_handle_destroy() */

/**
 * @brief Set Key Bits
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Set the length of the public key pair
 * used by the proxy certificate
 *
 * @param handle_attrs 
 *        the attributes to set
 * @param bits
 *        the length to set it to (usually 1024)
 *
 * @return 
 *        GLOBUS_SUCCESS
 */
globus_result_t
globus_gsi_proxy_handle_attrs_set_keybits(
    globus_gsi_proxy_handle_attrs_t     handle_attrs,
    int                                 bits)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle_attrs == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"), 
             __func__));
        goto exit;
    }

    handle_attrs->key_bits = bits;
    
exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/**
 * @brief Get Key Bits
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Gets the length of the public key pair used by
 * the proxy certificate
 *
 * @param handle_attrs
 *        the attributes to get the key length from
 * @param bits
 *        the length of the key pair in bits
 * @return
 *        GLOBUS_SUCCESS
 */
globus_result_t
globus_gsi_proxy_handle_attrs_get_keybits(
    globus_gsi_proxy_handle_attrs_t     handle_attrs,
    int *                               bits)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if (handle_attrs == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"), 
             __func__));
        goto exit;
    }
    if (bits == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_INVALID_PARAMETER,
            (_PCSL("NULL bits passed to function: %s"), 
             __func__));
        goto exit;
    }
    *bits = handle_attrs->key_bits;

exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/**
 * @brief Set Initial Prime Number
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Set the initial prime number used for
 * generating public key pairs in the RSA
 * algorithm
 *
 * @param handle_attrs
 *        The attributes to set
 * @param prime
 *        The prime number to set it to
 *        This value needs to be a prime number
 * @return 
 *        GLOBUS_SUCCESS
 */
globus_result_t
globus_gsi_proxy_handle_attrs_set_init_prime(
    globus_gsi_proxy_handle_attrs_t     handle_attrs,
    int                                 prime)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if (handle_attrs == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"), 
             __func__));
        goto exit;
    }
    handle_attrs->init_prime = prime;

exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/**
 * @brief Get Initial Prime Number
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Get the initial prime number used for
 * generating the public key pair in the
 * RSA algorithm
 *
 * @param handle_attrs
 *        The attributes to get the initial
 *        prime number from
 * @param prime
 *        The initial prime number taken from the
 *        attributes
 * @return
 *        GLOBUS_SUCCESS
 */
globus_result_t
globus_gsi_proxy_handle_attrs_get_init_prime(
    globus_gsi_proxy_handle_attrs_t     handle_attrs,
    int *                               prime)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if (handle_attrs == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"), 
             __func__));
        goto exit;
    }
    if (prime == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_INVALID_PARAMETER,
            (_PCSL("NULL prime passed to function: %s"), 
             __func__));
        goto exit;
    }
    *prime = handle_attrs->init_prime;
    
exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;

    return result;
}

/**
 * @brief Set Signing Algorithm
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Sets the Signing Algorithm to be used to sign
 * the certificate request.  In most cases, the
 * signing party will ignore this value, and sign
 * with an algorithm of its choice.
 *
 * @param handle_attrs
 *        The proxy handle to set the signing algorithm of
 * @param algorithm
 *        The signing algorithm to set 
 * @return
 *        Returns 
 *        GLOBUS_SUCCESS if the handle is valid, otherwise
 *        an error object is returned.
 */
globus_result_t
globus_gsi_proxy_handle_attrs_set_signing_algorithm(
    globus_gsi_proxy_handle_attrs_t     handle_attrs,
    const EVP_MD *                      algorithm)
{
    globus_result_t                     result;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle_attrs == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"), 
             __func__));
        goto exit;
    }

    handle_attrs->signing_algorithm = algorithm;

    result = GLOBUS_SUCCESS;

 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/**
 * @brief Get Signing Algorithm
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Gets the Signing Algorithm to used to sign
 * the certificate request.  In most cases, the
 * signing party will ignore this value, and sign
 * with an algorithm of its choice.
 *
 * @param handle_attrs
 *        The proxy handle_attrs to get the signing algorithm of
 * @param algorithm
 *        Parameter used to return the signing algorithm used
 * @return
 *        Returns 
 *        GLOBUS_SUCCESS if the handle is valid, otherwise
 *        an error object is returned.
 */
globus_result_t
globus_gsi_proxy_handle_attrs_get_signing_algorithm(
    globus_gsi_proxy_handle_attrs_t     handle_attrs,
    const EVP_MD **                     algorithm)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if (handle_attrs == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"),
             __func__));
        goto exit;
    }
    if (algorithm == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_INVALID_PARAMETER,
            (_PCSL("NULL algorithm passed to function: %s"),
             __func__));
        goto exit;
    }

    *algorithm = handle_attrs->signing_algorithm;

 exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/**
 * @brief Set Clock Skew Allowable
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Sets the clock skew in minutes of the proxy cert request
 * so that time differences between hosts won't
 * cause problems.  This value defaults to 5 minutes.
 *
 * @param handle_attrs
 *        the handle_attrs containing the clock skew to be set
 * @param skew
 *        the amount to skew by (in seconds)
 * @return 
 *        GLOBUS_SUCCESS if the handle_attrs is valid - otherwise an
 *        error is returned.
 */
globus_result_t
globus_gsi_proxy_handle_attrs_set_clock_skew_allowable(
    globus_gsi_proxy_handle_attrs_t     handle_attrs,
    int                                 skew)
{
    globus_result_t                     result;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(handle_attrs == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"),
             __func__));
        goto exit;
    }
    handle_attrs->clock_skew = skew;
    result = GLOBUS_SUCCESS;

 exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/**
 * @brief Get Clock Skew Allowable
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Get the allowable clock skew for the proxy certificate
 *
 * @param handle_attrs
 *        The handle_attrs to get the clock skew from
 * @param skew
 *        The allowable clock skew (in seconds)
 *        to get from the proxy certificate
 *        request.  This value gets set by the function, so it needs
 *        to be a pointer.
 * @return
 *        GLOBUS_SUCCESS if the handle_attrs is valid, otherwise an error
 *        is returned
 */
globus_result_t
globus_gsi_proxy_handle_attrs_get_clock_skew_allowable(
    globus_gsi_proxy_handle_attrs_t     handle_attrs,
    int *                               skew)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if (handle_attrs == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"),
             __func__));
        goto exit;
    }
    if (skew == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_INVALID_PARAMETER,
            (_PCSL("NULL skew passed to function: %s"),
             __func__));
        goto exit;
    }
    *skew = handle_attrs->clock_skew;

exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/**
 * @brief Get Key Gen Callback 
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Get the public/private key generation callback that provides status
 * during the generation of the keys
 * 
 * @param handle_attrs
 *        The handle_attrs to get the callback from
 * @param callback
 *        The callback from the handle attributes
 *
 * @return
 *        GLOBUS_SUCCESS if the handle_attrs is valid, otherwise an error
 *        is returned
 */
globus_result_t
globus_gsi_proxy_handle_attrs_get_key_gen_callback(
    globus_gsi_proxy_handle_attrs_t     handle_attrs,
    void                                (**callback)(int, int, void *))
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if (handle_attrs == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"),
             __func__));
        goto exit;
    }
    if (callback == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_INVALID_PARAMETER,
            (_PCSL("NULL callback passed to function: %s"),
             __func__));
        goto exit;
    }
    *callback = handle_attrs->key_gen_callback;

exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/**
 * @brief Set Key Gen Callback 
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Set the public/private key generation callback that provides status
 * during the generation of the keys
 *
 * @param handle_attrs
 *        The handle_attrs to get the callback from
 * @param callback
 *        The callback from the handle attributes
 *
 * @return
 *        GLOBUS_SUCCESS if the handle_attrs is valid, otherwise an error
 *        is returned
 */
globus_result_t
globus_gsi_proxy_handle_attrs_set_key_gen_callback(
    globus_gsi_proxy_handle_attrs_t     handle_attrs,
    void                                (*callback)(int, int, void *))
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if (handle_attrs == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"),
             __func__));
        goto exit;
    }
    handle_attrs->key_gen_callback = callback;

exit:
    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}

/**
 * @brief Copy Attributes
 * @ingroup globus_gsi_proxy_handle_attrs
 * @details
 * Make a copy of GSI Proxy handle attributes
 *
 * @param a 
 *        The handle attributes to copy
 * @param b 
 *        The copy
 * @return
 *        GLOBUS_SUCCESS
 */
globus_result_t
globus_gsi_proxy_handle_attrs_copy(
    globus_gsi_proxy_handle_attrs_t     a,
    globus_gsi_proxy_handle_attrs_t *   b)
{
    globus_result_t                     result;
    
    GLOBUS_I_GSI_PROXY_DEBUG_ENTER;

    if(a == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"),
             __func__));
        goto error_exit;
    }
    if(b == NULL)
    {
        GLOBUS_GSI_PROXY_ERROR_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS,
            (_PCSL("NULL handle attributes passed to function: %s"),
             __func__));
        goto error_exit;
    }

    result = globus_gsi_proxy_handle_attrs_init(b);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_PROXY_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_PROXY_ERROR_WITH_HANDLE_ATTRS);
        goto destroy_b_exit;
    }

    (*b)->key_bits = a->key_bits;
    (*b)->init_prime = a->init_prime;
    (*b)->signing_algorithm = a->signing_algorithm;
    (*b)->clock_skew = a->clock_skew;
    (*b)->key_gen_callback = a->key_gen_callback;

    result = GLOBUS_SUCCESS;
    goto exit;


destroy_b_exit:
    if(*b)
    {
        globus_gsi_proxy_handle_attrs_destroy(*b);
    }

error_exit:
    if (b)
    {
        *b = NULL;
    }

exit:

    GLOBUS_I_GSI_PROXY_DEBUG_EXIT;
    return result;
}
/* globus_gsi_proxy_handle_attrs_copy() */
