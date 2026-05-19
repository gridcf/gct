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
 * @file globus_gsi_callback.c
 * @brief Globus GSI Callback
 * @author Sam Meder, Sam Lang
 */
#endif

#include "globus_common.h"
#include "globus_gsi_callback_constants.h"
#include "globus_i_gsi_callback.h"
#include "globus_gsi_system_config.h"
#include "openssl/err.h"
#include "openssl/asn1.h"
#include "openssl/ssl.h"
#include "openssl/crypto.h"
#include "openssl/rand.h"
#include "openssl/x509v3.h"
#include "version.h"

#ifndef BUILD_FOR_K5CERT_ONLY
#include "globus_oldgaa.h"
#include "globus_oldgaa_utils.h"
#endif

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define X509_STORE_CTX_get0_chain(ctx) (ctx)->chain
#define X509_STORE_CTX_get0_cert(ctx) (ctx)->cert
#define X509_get_pathlen(x509) (x509)->ex_pathlen
#define X509_get_extension_flags(x509) (x509)->ex_flags
#define X509_STORE_CTX_set_current_cert(ctx, x509) (ctx)->current_cert = (x509)
#define X509_set_proxy_flag(c) (c)->ex_flags |= EXFLAG_PROXY
typedef int (*X509_STORE_CTX_get_issuer_fn)(X509 **issuer, X509_STORE_CTX *ctx, X509 *x); /* get issuers cert from ctx */
#define X509_STORE_CTX_get_get_issuer(c) (c)->get_issuer;
#define X509_OBJECT_get0_X509_CRL(o) (o)->data.crl
#define X509_REVOKED_get0_serialNumber(r) (r)->serialNumber
#define X509_OBJECT_new() calloc(1, sizeof(X509_OBJECT))
#define X509_OBJECT_free(o) \
    do { \
        X509_OBJECT *otmp = (o); \
        X509_OBJECT_free_contents(otmp); \
        free(otmp); \
    } while (0)
#define X509_CRL_get0_nextUpdate(crl) X509_CRL_get_nextUpdate(crl)
#define X509_CRL_get0_lastUpdate(crl) X509_CRL_get_lastUpdate(crl)
#endif

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
static globus_mutex_t                   globus_l_gsi_callback_oldgaa_mutex;
static globus_mutex_t                   globus_l_gsi_callback_verify_mutex;

static int globus_l_gsi_callback_activate(void);
static int globus_l_gsi_callback_deactivate(void);

int                              globus_i_gsi_callback_debug_level   = 0;
FILE *                           globus_i_gsi_callback_debug_fstream = NULL;

/**
 * Module descriptor static initializer.
 */
globus_module_descriptor_t globus_i_gsi_callback_module =
{
    "globus_gsi_callback_module",
    globus_l_gsi_callback_activate,
    globus_l_gsi_callback_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

static int globus_i_gsi_callback_SSL_callback_data_index = -1;
static int globus_i_gsi_callback_X509_STORE_callback_data_index = -1;

static int
globus_l_gsi_callback_openssl_new(
    void *                              parent,
    void *                              ptr,
    CRYPTO_EX_DATA *                    ad,
    int                                 idx,
    long                                argl,
    void *                              argp)
{
    int                                 result = 1;
    static char *                       _function_name_ =
        "globus_gsi_callback_openssl_new";
    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    /* init app specific data (callback data)
     * since we can't allocate the ptr here
     * this function isn't particularly useful
     */

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

static int
globus_l_gsi_callback_openssl_free(
    void *                              parent,
    void *                              ptr,
    CRYPTO_EX_DATA *                    ad,
    int                                 idx,
    long                                argl,
    void *                              argp)
{
    int                                 result = 1;
    static char *                       _function_name_ =
        "globus_gsi_callback_openssl_free";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    /* free the callback data - currently not used*/

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

static int
globus_l_gsi_callback_openssl_dup(
    CRYPTO_EX_DATA *                    to,
    CRYPTO_EX_DATA *                    from,
    void *                              from_d,
    int                                 idx,
    long                                argl,
    void *                              argp)
{
    int                                 result = 1;
    static char *                       _function_name_ =
        "globus_gsi_callback_openssl_dup";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    /* copy the callback data - currenlty not used by OpenSSL */

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

/**
 * Module activation
 */
static
int
globus_l_gsi_callback_activate(void)
{
    int                                 result = (int) GLOBUS_SUCCESS;
    char *                              tmp_string;
    static char *                       _function_name_ =
        "globus_l_gsi_callback_activate";

    tmp_string = globus_module_getenv("GLOBUS_GSI_CALLBACK_DEBUG_LEVEL");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_gsi_callback_debug_level = atoi(tmp_string);

        if(globus_i_gsi_callback_debug_level < 0)
        {
            globus_i_gsi_callback_debug_level = 0;
        }
    }

    tmp_string = globus_module_getenv("GLOBUS_GSI_CALLBACK_DEBUG_FILE");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_gsi_callback_debug_fstream = fopen(tmp_string, "a");
        if(globus_i_gsi_callback_debug_fstream == NULL)
        {
            result = (int) GLOBUS_FAILURE;
            goto exit;
        }
    }
    else
    {
        globus_i_gsi_callback_debug_fstream = stderr;
    }

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

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

    result = globus_module_activate(GLOBUS_GSI_OPENSSL_ERROR_MODULE);

    if(result != GLOBUS_SUCCESS)
    {
        goto exit;
    }

    globus_mutex_init(&globus_l_gsi_callback_oldgaa_mutex, NULL);
    globus_mutex_init(&globus_l_gsi_callback_verify_mutex, NULL);

    OpenSSL_add_all_algorithms();

    if(globus_i_gsi_callback_X509_STORE_callback_data_index < 0)
    {
        globus_i_gsi_callback_X509_STORE_callback_data_index =
            X509_STORE_CTX_get_ex_new_index(
                0, NULL,
                (CRYPTO_EX_new *)  &globus_l_gsi_callback_openssl_new,
                (CRYPTO_EX_dup *)  &globus_l_gsi_callback_openssl_dup,
                (CRYPTO_EX_free *) &globus_l_gsi_callback_openssl_free);
        if(globus_i_gsi_callback_X509_STORE_callback_data_index < 0)
        {
            GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_CALLBACK_ERROR_WITH_CALLBACK_DATA_INDEX,
                (_CLS("Couldn't create external data index for SSL object")));
            goto exit;
        }
    }

    if(globus_i_gsi_callback_SSL_callback_data_index < 0)
    {
        globus_i_gsi_callback_SSL_callback_data_index = SSL_get_ex_new_index(
            0, NULL,
            (CRYPTO_EX_new *)  &globus_l_gsi_callback_openssl_new,
            (CRYPTO_EX_dup *)  &globus_l_gsi_callback_openssl_dup,
            (CRYPTO_EX_free *) &globus_l_gsi_callback_openssl_free);
        if(globus_i_gsi_callback_SSL_callback_data_index < 0)
        {
            GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_CALLBACK_ERROR_WITH_CALLBACK_DATA_INDEX,
                (_CLS("Couldn't create external data index for SSL object")));
            goto exit;
        }
    }

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;

 exit:

    return result;
}

/**
 * Module deactivation
 */
static
int
globus_l_gsi_callback_deactivate(void)
{
    int                                 result = (int) GLOBUS_SUCCESS;
    static char *                       _function_name_ =
        "globus_l_gsi_callback_deactivate";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    EVP_cleanup();

    globus_mutex_destroy(&globus_l_gsi_callback_oldgaa_mutex);
    globus_mutex_destroy(&globus_l_gsi_callback_verify_mutex);
    globus_module_deactivate(GLOBUS_GSI_OPENSSL_ERROR_MODULE);
    globus_module_deactivate(GLOBUS_GSI_SYSCONFIG_MODULE);
    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;

    if(globus_i_gsi_callback_debug_fstream != stderr)
    {
        fclose(globus_i_gsi_callback_debug_fstream);
    }

    return result;
}

#endif

/**
 * @brief Get callback data index from X509_STORE
 * @ingroup globus_gsi_callback_functions
 * @details
 * Retrieve or create the index for our callback data structure in the
 * X509_STORE.
 *
 * @param index
 *        Will contain the index upon return
 *
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case,
 *        a globus error object ID is returned
 */
globus_result_t
globus_gsi_callback_get_X509_STORE_callback_data_index(
    int *                               index)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    static char *                       _function_name_ =
        "globus_gsi_callback_get_X509_STORE_callback_data_index";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    *index = globus_i_gsi_callback_X509_STORE_callback_data_index;

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

/**
 * @brief Get callback data index from SSL structure
 * @ingroup globus_gsi_callback_functions
 * @details
 * Retrieve or create the index for our callback data structure in the
 * SSL structure.
 *
 * @param index
 *        Will contain the index upon return
 *
 * @return
 *        GLOBUS_SUCCESS unless an error occurred, in which case,
 *        a globus error object ID is returned
 */
globus_result_t
globus_gsi_callback_get_SSL_callback_data_index(
    int *                               index)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    static char *                       _function_name_ =
        "globus_gsi_callback_get_SSL_callback_data_index";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;


    *index = globus_i_gsi_callback_SSL_callback_data_index;

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

/**
 * @brief Certificate verify wrapper
 * @ingroup globus_gsi_callback_functions
 * @details
 * This function wraps the OpenSSL certificate verification callback for the
 * purpose of a replacing the standard issuer check with one that deals with
 * proxy certificates. Should be used with SSL_CTX_set_cert_verify_callback()
 *
 * @param context
 *        The X509_STORE_CTX for which to register the callback.
 * @param arg
 *        Arguments to the callback. Currently ignored.
 * @return
 *        1 on success
 *        0 on failure
 */
int
globus_gsi_callback_X509_verify_cert(
    X509_STORE_CTX *                    context,
    void *                              arg)
{
    int                                 result;
    static char *                       _function_name_ =
        "globus_gsi_callback_X509_verify_cert";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    /*
     * OpenSSL-0.9.6 has a  check_issued routine which
     * we want to override so we  can replace some of the checks.
     */
    context->check_issued = globus_gsi_callback_check_issued;
#else
    X509_STORE_set_check_issued(X509_STORE_CTX_get0_store(context), globus_gsi_callback_check_issued);
#endif
    /*
     * If this is not set, OpenSSL-0.9.8 assumes the proxy cert
     * as an EEC and the next level cert in the chain as a CA cert
     * and throws an invalid CA error. If we set this, the callback
     * (globus_gsi_callback_handshake_callback) gets called with
     * preverify_ok = 0 with an error "unhandled critical extension"
     * and "path length exceeded".
     * globus_i_gsi_callback_cred_verify() called by
     * globus_gsi_callback_handshake_callback() checks for these
     * errors and returns success. globus_i_gsi_callback_cred_verify()
     * will check the critical extension later.
     */
    #if defined(X509_V_FLAG_ALLOW_PROXY_CERTS)
    X509_STORE_CTX_set_flags(
                   context, X509_V_FLAG_ALLOW_PROXY_CERTS);
    #endif
    globus_mutex_lock(&globus_l_gsi_callback_verify_mutex);
    result = X509_verify_cert(context);
    globus_mutex_unlock(&globus_l_gsi_callback_verify_mutex);

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

/**
 * @brief Independent path validation callback.
 * @ingroup globus_gsi_callback_functions
 * @details
 * This function provides a path validation callback for validation outside of
 * a SSL session. It should be used in X509_STORE_set_verify_cb_func().
 *
 * @param preverify_ok
 *        Communicates the result of default validation steps performed by
 *        OpenSSL
 * @param x509_context
 *        The validation state object
 * @return
 *        1 on success
 *        0 on failure
 */
int globus_gsi_callback_create_proxy_callback(
    int                                 preverify_ok,
    X509_STORE_CTX *                    x509_context)
{
    int                                 cb_index;
    int                                 verify_result;
    globus_result_t                     result;
    globus_gsi_callback_data_t          callback_data;
    static char *                       _function_name_ =
        "globus_i_gsi_callback_create_proxy_callback";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    result = globus_gsi_callback_get_X509_STORE_callback_data_index(&cb_index);
    if(result != GLOBUS_SUCCESS)
    {
        verify_result = 0;
        goto exit;
    }

    callback_data = (globus_gsi_callback_data_t)
       X509_STORE_CTX_get_ex_data(
           x509_context,
           cb_index);

    if(!callback_data)
    {
        verify_result = 0;
        goto exit;
    }

    result = globus_i_gsi_callback_cred_verify(preverify_ok,
                                               callback_data,
                                               x509_context);

    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED);
        callback_data->error = result;
        verify_result = 0;
        goto set_callback_data_error;
    }

    result = GLOBUS_SUCCESS;
    verify_result = 1;

 set_callback_data_error:

    callback_data->error = result;

 exit:

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return verify_result;
}

/**
 * @brief SSL path validation callback.
 * @ingroup globus_gsi_callback_functions
 * @details
 * This function provides a path validation callback for the validation part of
 * establishing a SSL session. It handles proxy certificates, X509 Extensions
 * and CRL checking. It should be used in SSL_CTX_set_verify().
 *
 * @param preverify_ok
 *        Communicates the result of default validation steps performed by
 *        OpenSSL
 * @param x509_context
 *        The validation state object.
 * @return
 *        1 on success
 *        0 on failure
 */
int globus_gsi_callback_handshake_callback(
    int                                 preverify_ok,
    X509_STORE_CTX *                    x509_context)
{
    int                                 verify_result;
    int                                 callback_data_index;
    globus_result_t                     result;
    globus_gsi_callback_data_t          callback_data;
    SSL *                               ssl = NULL;
    static char *                       _function_name_ =
        "globus_gsi_callback_handshake_callback";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    /* the first index should contain the SSL structure */
    ssl = (SSL *)
        X509_STORE_CTX_get_ex_data(x509_context,
                                   SSL_get_ex_data_X509_STORE_CTX_idx());
    if(!ssl)
    {
        verify_result = 0;
        goto exit;
    }

    result = globus_gsi_callback_get_SSL_callback_data_index(
        &callback_data_index);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED);
        verify_result = 0;
        goto exit;
    }

    callback_data = *(globus_gsi_callback_data_t *)
        SSL_get_ex_data(ssl, callback_data_index);
    if(!callback_data)
    {
        verify_result = 0;
        goto exit;
    }

    result = globus_i_gsi_callback_cred_verify(preverify_ok,
                                               callback_data,
                                               x509_context);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED);
        verify_result = 0;
        goto set_callback_data_error;
    }

    result = GLOBUS_SUCCESS;
    verify_result = 1;

 set_callback_data_error:

    callback_data->error = result;

 exit:
    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return verify_result;
}

/**
 * @brief OpenSSL X509_check_issued() wrapper
 * @ingroup globus_gsi_callback_functions
 * @details
 * This function wraps the OpenSSL X509_check_issued() call and catches the
 * error caused by the fact that a proxy certificate issuer may not have to
 * have the correct KeyUsage fields set.
 *
 * @param context
 *        The validation state object.
 * @param cert
 *        The certificate to check
 * @param issuer
 *        The issuer certificate to check
 * @return
 *        1 on success
 *        0 on failure
 */
#if OPENSSL_VERSION_NUMBER < 0x40000000L
int globus_gsi_callback_check_issued(
    X509_STORE_CTX *                    context,
    X509 *                              cert,
    X509 *                              issuer)
#else
int globus_gsi_callback_check_issued(
    X509_STORE_CTX *                    context,
    const X509 *                        cert,
    const X509 *                        issuer)
#endif
{
    globus_result_t                     result;
    int                                 return_value;
    int                                 return_code = 1;
    globus_gsi_cert_utils_cert_type_t   cert_type;
    static char *                       _function_name_ =
        "globus_gsi_callback_check_issued";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    return_value = X509_check_issued(issuer, cert);
    if(return_value != X509_V_OK)
    {
        return_code = 0;
        switch(return_value)
        {

        case X509_V_ERR_KEYUSAGE_NO_CERTSIGN:
             /* If this is a proxy certificate then the issuer
              * does not need to have the key_usage set.
              * So check if its a proxy, and ignore
              * the error if so.
              */
            result = globus_gsi_cert_utils_get_cert_type(cert, &cert_type);
            if(result != GLOBUS_SUCCESS)
            {
                return_code = 0;
                break;
            }

            if(GLOBUS_GSI_CERT_UTILS_IS_PROXY(cert_type))
            {
                /* its a proxy! */
                return_code = 1;
            }
            break;

        default:
            break;
        }
    }

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return return_code;
}

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

globus_result_t
globus_i_gsi_callback_cred_verify(
    int                                 preverify_ok,
    globus_gsi_callback_data_t          callback_data,
    X509_STORE_CTX *                    x509_context)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_cert_utils_cert_type_t   cert_type;
    X509 *                              tmp_cert = NULL;
    X509 *                              prev_cert = NULL;
    static char *                       _function_name_ =
        "globus_i_gsi_callback_cred_verify";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    /* Now check for some error conditions which
     * can be disregarded.
     */
    if (!preverify_ok)
    {
        switch (X509_STORE_CTX_get_error(x509_context))
        {
        case X509_V_ERR_PATH_LENGTH_EXCEEDED:

        /*
         * OpenSSL-0.9.8 has this error (0.9.7d did not have this)
         * So we will ignore the errors now and do our checks later
         * on (as explained below).
         */
        case X509_V_ERR_PROXY_PATH_LENGTH_EXCEEDED:

            GLOBUS_I_GSI_CALLBACK_DEBUG_PRINT(
                2, "X509_V_ERR_PATH_LENGTH_EXCEEDED\n");
            /*
             * Since OpenSSL does not know about proxies,
             * it will count them against the path length
             * So we will ignore the errors and do our
             * own checks later on, when we check the last
             * certificate in the chain we will check the chain.
             */
            result = GLOBUS_SUCCESS;
            break;
        /*
         * In the later version (097g+) OpenSSL does know about
         * proxies, but not non-rfc compliant proxies, it will
         * count them as unhandled critical extensions.
         * So we will ignore the errors and do our
         * own checks later on, when we check the last
         * certificate in the chain we will check the chain.
         * As OpenSSL does not recognize legacy proxies
         */
        case X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION:
            GLOBUS_I_GSI_CALLBACK_DEBUG_PRINT(
                2, "X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION\n");
            /*
             * Setting this for 098 or later versions avoid the invalid
             * CA error but would result in proxy path len exceeded which
             * is handled above.
             */
            X509_set_proxy_flag(X509_STORE_CTX_get_current_cert(x509_context));
            result = GLOBUS_SUCCESS;
            break;
        case X509_V_ERR_INVALID_PURPOSE:
            /*
             * Invalid purpose if we init sec context with a server that does
             * not have the SSL Server Netscape extension (occurs with 0.9.7
             * servers)
             */
            result = GLOBUS_SUCCESS;
            break;

        case X509_V_ERR_INVALID_CA:
            /*
             * If the previous cert in the chain is a proxy cert then
             * we get this error just because openssl does not recognize
             * our proxy and treats it as an EEC. And thus, it would
             * treat higher level proxies (if any) or EEC as CA cert
             * (which are not actually CA certs) and would throw this
             * error. As long as the previous cert in the chain is a
             * proxy cert, we ignore this error.
             */
            prev_cert = sk_X509_value(
                    X509_STORE_CTX_get0_chain(x509_context), X509_STORE_CTX_get_error_depth(x509_context)-1);
            result = globus_gsi_cert_utils_get_cert_type(prev_cert, &cert_type);
            if(result != GLOBUS_SUCCESS)
            {
                result = (globus_result_t)GLOBUS_FAILURE;
            }
            else
            {
                if(GLOBUS_GSI_CERT_UTILS_IS_PROXY(cert_type))
                {
                    result = GLOBUS_SUCCESS;
                }
                else
                {
                    result = (globus_result_t)GLOBUS_FAILURE;
                }
            }
            break;
        default:
            result = (globus_result_t)GLOBUS_FAILURE;
            break;
        }

        if (result != GLOBUS_SUCCESS)
        {
            char *                      subject_name =
              X509_NAME_oneline(X509_get_subject_name(X509_STORE_CTX_get_current_cert(x509_context)), 0, 0);
            unsigned long               issuer_hash =
                    X509_issuer_name_hash(X509_STORE_CTX_get_current_cert(x509_context));
            char *                      cert_dir;

            if (X509_STORE_CTX_get_error(x509_context) == X509_V_ERR_CERT_NOT_YET_VALID)
            {
                GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_CERT_NOT_YET_VALID,
                    (_CLS("Cert with subject: %s is not yet valid"
                     "- check clock skew between hosts."), subject_name));
            }
            else if (X509_STORE_CTX_get_error(x509_context) ==
                     X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY)
            {
                cert_dir = NULL;
                GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(
                    &cert_dir);
                GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_CANT_GET_LOCAL_CA_CERT,
                    (_CLS("Cannot find trusted CA certificate "
                     "with hash %lx%s%s"),
                     issuer_hash, cert_dir ? " in " : "",
                     cert_dir ? cert_dir : ""));
                if (cert_dir)
                {
                    free(cert_dir);
                }
            }
            else if (X509_STORE_CTX_get_error(x509_context) == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN)
            {
                cert_dir = NULL;
                GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(
                    &cert_dir);
                GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_CANT_GET_LOCAL_CA_CERT,
                    (_CLS("Untrusted self-signed certificate in chain "
                     "with hash %lx"),
                     issuer_hash));
                if (cert_dir)
                {
                    free(cert_dir);
                }
            }
            else if (X509_STORE_CTX_get_error(x509_context) == X509_V_ERR_CERT_HAS_EXPIRED)
            {
                GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_CERT_HAS_EXPIRED,
                    (_CLS("Credential with subject: %s has expired."), subject_name));
            }
            else
            {
                GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED,
                    (X509_verify_cert_error_string(X509_STORE_CTX_get_error(x509_context))));
            }

            OPENSSL_free(subject_name);

            goto exit;
        }

        goto exit;
    }

    result = globus_i_gsi_callback_check_proxy(x509_context,
                                               callback_data);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED);
        goto exit;
    }

    if(callback_data->cert_type == GLOBUS_GSI_CERT_UTILS_TYPE_EEC ||
       callback_data->cert_type == GLOBUS_GSI_CERT_UTILS_TYPE_CA)
    {
        /* only want to check that the cert isn't revoked if its not
         * a proxy, since proxies don't ever get revoked
         */
#ifdef X509_V_ERR_CERT_REVOKED
        result = globus_i_gsi_callback_check_revoked(x509_context,
                                                     callback_data);
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED);
            goto exit;
        }
#endif

        /* only want to check singing_policy file if its not a proxy */
        result = globus_i_gsi_callback_check_signing_policy(
            x509_context,
            callback_data);
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED);
            goto exit;
        }
    }

    tmp_cert = X509_dup(X509_STORE_CTX_get_current_cert(x509_context));

    /* add the current cert to the callback data's cert chain */
    sk_X509_insert(callback_data->cert_chain,
                   tmp_cert, 0);

    callback_data->cert_depth++;

    result =
        globus_i_gsi_callback_check_critical_extensions(x509_context,
                                                        callback_data);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED);
        goto exit;
    }

    result = globus_i_gsi_callback_check_path_length(x509_context,
                                                     callback_data);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED);
        goto exit;
    }

 exit:

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

globus_result_t
globus_i_gsi_callback_check_proxy(
    X509_STORE_CTX *                    x509_context,
    globus_gsi_callback_data_t          callback_data)
{
    globus_gsi_cert_utils_cert_type_t   cert_type;
    globus_result_t                     result = GLOBUS_SUCCESS;
    static char *                       _function_name_ =
        "globus_i_gsi_callback_check_proxy";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    /* All of the OpenSSL tests have passed and we now get to
     * look at the certificate to verify the proxy rules,
     * and ca-signing-policy rules. We will also do a CRL check
     */
    result = globus_gsi_cert_utils_get_cert_type(X509_STORE_CTX_get_current_cert(x509_context),
                                                    &cert_type);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED);
        goto exit;
    }

    if(GLOBUS_GSI_CERT_UTILS_IS_PROXY(cert_type))
    {
        /* it is a proxy */

        /* a legacy globus proxy may only be followed by another legacy globus
         * proxy or a limited legacy globus_proxy.
         * a limited legacy globus proxy may only be followed by another
         * limited legacy globus proxy
         * a draft compliant proxy may only be followed by another draft
         * compliant proxy
         * a draft compliant limited proxy may only be followed by another draft
         * compliant limited proxy or a draft compliant independent proxy
         */

        if((GLOBUS_GSI_CERT_UTILS_IS_GSI_2_PROXY(callback_data->cert_type) &&
            !GLOBUS_GSI_CERT_UTILS_IS_GSI_2_PROXY(cert_type)) ||
           (GLOBUS_GSI_CERT_UTILS_IS_GSI_3_PROXY(callback_data->cert_type) &&
            !GLOBUS_GSI_CERT_UTILS_IS_GSI_3_PROXY(cert_type)) ||
           (GLOBUS_GSI_CERT_UTILS_IS_RFC_PROXY(callback_data->cert_type) &&
            !GLOBUS_GSI_CERT_UTILS_IS_RFC_PROXY(cert_type)))
        {
            GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_CALLBACK_ERROR_MIXING_DIFFERENT_PROXY_TYPES);
            goto exit;
        }

        if(GLOBUS_GSI_CERT_UTILS_IS_LIMITED_PROXY(callback_data->cert_type) &&
           !(GLOBUS_GSI_CERT_UTILS_IS_LIMITED_PROXY(cert_type) ||
             GLOBUS_GSI_CERT_UTILS_IS_INDEPENDENT_PROXY(cert_type)))
        {
            GLOBUS_GSI_CALLBACK_ERROR_RESULT(
                result,
                GLOBUS_GSI_CALLBACK_ERROR_LIMITED_PROXY,
                (_CLS("Can't sign a non-limited, non-independent proxy "
                      "with a limited proxy")));
            X509_STORE_CTX_set_error(x509_context, X509_V_ERR_CERT_SIGNATURE_FAILURE);
            goto exit;
        }

        GLOBUS_I_GSI_CALLBACK_DEBUG_PRINT(2, "Passed proxy test\n");

        callback_data->proxy_depth++;

        if(callback_data->max_proxy_depth != -1 &&
           callback_data->max_proxy_depth < callback_data->proxy_depth)
        {
            GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_CALLBACK_ERROR_PROXY_PATH_LENGTH_EXCEEDED);
            goto exit;
        }
    }

    callback_data->cert_type = cert_type;

 exit:

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

/* this function can go away with OpenSSL 0.9.7 and
 * its support for CRL checking - SLANG
 */
globus_result_t
globus_i_gsi_callback_check_revoked(
    X509_STORE_CTX *                    x509_context,
    globus_gsi_callback_data_t          callback_data)
{
    X509_REVOKED *                      revoked = NULL;
    X509_CRL *                          crl = NULL;
    X509_OBJECT *                       x509_object = NULL;
    STACK_OF(X509_REVOKED) *            revoked_stack = NULL;
    int                                 i, n;
    long                                err = 0;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_bool_t                       crl_was_expired = GLOBUS_FALSE;
    globus_bool_t                       recheck_crl_done = GLOBUS_FALSE;
    static char *                       _function_name_ =
        "globus_i_gsi_callback_check_revoked";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;


    do
    {
        x509_object = X509_OBJECT_new();
        /*
         * SSLeay 0.9.0 handles CRLs but does not check them.
         * We will check the crl for this cert, if there
         * is a CRL in the store.
         * If we find the crl is not valid, we will fail,
         * as once the sysadmin indicates that CRLs are to
         * be checked, he best keep it upto date.
         *
         * When future versions of SSLeay support this better,
         * we can remove these tests.
         *
         * we come through this code for each certificate,
         * starting with the CA's We will check for a CRL
         * each time, but only check the signature if the
         * subject name matches, and check for revoked
         * if the issuer name matches.
         * this allows the CA to revoke its own cert as well.
         */
        if (X509_STORE_get_by_subject(
                x509_context,
                X509_LU_CRL,
                X509_get_issuer_name(X509_STORE_CTX_get_current_cert(x509_context)),
                x509_object))
        {
            X509 *                          issuer;
            const ASN1_TIME *               last_update;
            const ASN1_TIME *               next_update;
            time_t                          last_time;
            int                             has_next_time;
            time_t                          next_time;
            EVP_PKEY *                      issuer_key;
            X509_STORE_CTX_get_issuer_fn    get_issuer;

            crl = X509_OBJECT_get0_X509_CRL(x509_object);
            next_update = X509_CRL_get0_nextUpdate(crl);
            last_update = X509_CRL_get0_lastUpdate(crl);
            has_next_time = (next_update != NULL);

            globus_gsi_cert_utils_make_time(last_update, &last_time);
            if (has_next_time)
            {
                globus_gsi_cert_utils_make_time(next_update, &next_time);
            }

            GLOBUS_I_GSI_CALLBACK_DEBUG_PRINT(2, "CRL last Update: ");
            GLOBUS_I_GSI_CALLBACK_DEBUG_FPRINTF(
                2, (globus_i_gsi_callback_debug_fstream,
                    "%s", asctime(gmtime(&last_time))));
            GLOBUS_I_GSI_CALLBACK_DEBUG_PRINT(2, "\nCRL next Update: ");
            GLOBUS_I_GSI_CALLBACK_DEBUG_FPRINTF(
                2, (globus_i_gsi_callback_debug_fstream,
                    "%s", has_next_time ? asctime(gmtime(&next_time)) : "<not set>" ));
            GLOBUS_I_GSI_CALLBACK_DEBUG_PRINT(2, "\n");

            get_issuer = X509_STORE_CTX_get_get_issuer(x509_context);

            /* verify the signature on this CRL */
            if(get_issuer(&issuer,
                                        x509_context,
                                        X509_STORE_CTX_get_current_cert(x509_context)) <= 0)
            {
                char *                      subject_string;

                subject_string = X509_NAME_oneline(
                    X509_get_issuer_name(X509_STORE_CTX_get_current_cert(x509_context)),
                    NULL, 0);

                GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_INVALID_CRL,
                    (_CLS("Couldn't get the issuer certificate of the CRL with "
                     "subject: %s"), subject_string));
                OPENSSL_free(subject_string);
                X509_STORE_CTX_set_error(x509_context, X509_V_ERR_CRL_SIGNATURE_FAILURE);
                goto free_X509_object;
            }

            issuer_key = X509_get_pubkey(issuer);

            if(issuer_key == NULL)
            {
                GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_INVALID_CRL,
                    (_CLS("Couldn't verify that the available CRL is valid")));
                X509_STORE_CTX_set_error(x509_context, X509_V_ERR_CRL_SIGNATURE_FAILURE);
                X509_free(issuer);
                goto free_X509_object;
            }

            X509_free(issuer);

            if (X509_CRL_verify(crl, issuer_key) <= 0)
            {
                GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_INVALID_CRL,
                    (_CLS("Couldn't verify that the available CRL is valid")));
                X509_STORE_CTX_set_error(x509_context, X509_V_ERR_CRL_SIGNATURE_FAILURE);
                EVP_PKEY_free(issuer_key);
                goto free_X509_object;
            }

            EVP_PKEY_free(issuer_key);

            /* Check date */

            i = X509_cmp_current_time(last_update);
            if (i == 0)
            {
                GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_INVALID_CRL,
                    (_CLS("In the available CRL, the thisUpdate field is not valid")));
                X509_STORE_CTX_set_error(x509_context, X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD);
                goto free_X509_object;
            }

            if (i > 0)
            {
                GLOBUS_GSI_CALLBACK_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_INVALID_CRL,
                    (_CLS("The available CRL is not yet valid")));
                X509_STORE_CTX_set_error(x509_context, X509_V_ERR_CRL_NOT_YET_VALID);
                goto free_X509_object;
            }

            i = (has_next_time) ? X509_cmp_current_time(next_update) : 1;
            if (i == 0)
            {
                GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_INVALID_CRL,
                    (_CLS("In the available CRL, the nextUpdate field is not valid")));
                X509_STORE_CTX_set_error(x509_context, X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD);
                goto free_X509_object;
            }

            /* If we get an expired CRL, we'll delete it from the store
             * associated with this ssl context and then try this operation one
             * more time to see if a new one is in place.
             */
            if (i < 0 && !crl_was_expired)
            {
                int idx;

#               if OPENSSL_VERSION_NUMBER < 0x10100000L
                {
                    CRYPTO_w_lock(CRYPTO_LOCK_X509_STORE);
                    idx=sk_X509_OBJECT_find(x509_context->ctx->objs, x509_object);
                    if (idx >= 0) X509_OBJECT_free(sk_X509_OBJECT_delete(x509_context->ctx->objs, idx));
                    X509_OBJECT_free(x509_object);
                    x509_object = NULL;
                    CRYPTO_w_unlock(CRYPTO_LOCK_X509_STORE);
                }
#               else
                {
                    STACK_OF(X509_OBJECT) *objects;
                    X509_STORE_lock(X509_STORE_CTX_get0_store(x509_context));
                    objects = X509_STORE_get0_objects(X509_STORE_CTX_get0_store(x509_context));
                    idx=sk_X509_OBJECT_find(objects, x509_object);
                    if (idx >= 0) X509_OBJECT_free(sk_X509_OBJECT_delete(objects, idx));
                    X509_OBJECT_free(x509_object);
                    x509_object = NULL;
                    X509_STORE_unlock(X509_STORE_CTX_get0_store(x509_context));
                }
#               endif

                /* OpenSSL 1.0.0 will try to reload the CRL if one with next
                 * index extension .r1 is present, but not reload if an old CRL
                 * is replaced. We explicitly try to reload it here to get
                 * around this; otherwise, there's no way to reliably replace
                 * a CRL so that new and old processes can find it.
                 */
                if (OPENSSL_VERSION_NUMBER >= 0x10000000L)
                {
                    char * cert_dir;
                    unsigned long hash;
                    char * crl_path;
                    X509_CRL * new_crl = NULL;
                    FILE * crl_fp;

                    result = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&cert_dir);
                    if (result != GLOBUS_SUCCESS)
                    {
                        return result;
                    }
                    hash = X509_issuer_name_hash(X509_STORE_CTX_get_current_cert(x509_context));

                    crl_path = globus_common_create_string(
                            "%s/%lx.r0", cert_dir, hash);

                    free(cert_dir);
                    cert_dir = NULL;

                    if (crl_path == NULL)
                    {
                        GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                            result,
                            GLOBUS_GSI_CALLBACK_ERROR_INVALID_CRL,
                            (_CLS("Unable to find valid CRL")));
                        goto free_X509_object;
                    }

                    errno = 0;
                    crl_fp = fopen(crl_path, "r");
                    free(crl_path);
                    crl_path = NULL;
                    if (crl_fp == NULL && errno == ENOENT)
                    {
                        /* CRL was removed */
                        result = GLOBUS_SUCCESS;
                        break;
                    }
                    else if (crl_fp == NULL)
                    {
                        /* Unable to open CRL */
                        GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                            result,
                            GLOBUS_GSI_CALLBACK_ERROR_INVALID_CRL,
                            (_CLS("Unable to find valid CRL")));
                        goto free_X509_object;
                    }
                    new_crl = PEM_read_X509_CRL(crl_fp, &new_crl, NULL, NULL);
                    fclose(crl_fp);

#if OPENSSL_VERSION_NUMBER < 0x10100000L
                    X509_STORE_add_crl(x509_context->ctx, new_crl);
#else
                    X509_STORE_add_crl(X509_STORE_CTX_get0_store(x509_context), new_crl);
#endif
                    X509_CRL_free(new_crl);
                }

                crl_was_expired = GLOBUS_TRUE;
                continue;
            }
            else if (i < 0)
            {
                GLOBUS_GSI_CALLBACK_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_INVALID_CRL,
                    (_CLS("The available CRL has expired")));
                X509_STORE_CTX_set_error(x509_context, X509_V_ERR_CRL_HAS_EXPIRED);
                goto free_X509_object;
            }

            /* If we get this far, then we're not going to recheck an expired
             * CRL and just fall out of the do/while
             */
            recheck_crl_done = GLOBUS_TRUE;

            X509_OBJECT_free(x509_object);
            x509_object = NULL;

            /* check if this cert is revoked */

            revoked_stack = X509_CRL_get_REVOKED(crl);
            n = sk_X509_REVOKED_num(revoked_stack);
            for (i = 0; i < n; i++)
            {
                const ASN1_INTEGER *revoked_serial_number = NULL;

                revoked = sk_X509_REVOKED_value(revoked_stack, i);
                revoked_serial_number = X509_REVOKED_get0_serialNumber(revoked);

                if(!ASN1_INTEGER_cmp(
                    revoked_serial_number,
                    X509_get_serialNumber(X509_STORE_CTX_get_current_cert(x509_context))))
                {
                    char *                      subject_string;
                    long                        serial;

                    serial = ASN1_INTEGER_get(revoked_serial_number);

                    subject_string = X509_NAME_oneline(X509_get_subject_name(
                        X509_STORE_CTX_get_current_cert(x509_context)), NULL, 0);

                    GLOBUS_GSI_CALLBACK_ERROR_RESULT(
                        result,
                        GLOBUS_GSI_CALLBACK_ERROR_REVOKED_CERT,
                        (_CLS("Serial number = %ld (0x%lX) "
                         "Subject=%s"),
                         serial, serial, subject_string));

                    X509_STORE_CTX_set_error(x509_context, X509_V_ERR_CERT_REVOKED);

                    GLOBUS_I_GSI_CALLBACK_DEBUG_FPRINTF(
                        2, (globus_i_gsi_callback_debug_fstream,
                            "revoked %lX\n",
                            ASN1_INTEGER_get(revoked_serial_number)));

                    OPENSSL_free(subject_string);
                }
            }
        }
        else
        {
            /* Error reading CRL or CRL not available */
            err = ERR_get_error();

            if (err != X509_V_OK)
            {
                GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_INVALID_CRL,
                    (_CLS("Couldn't verify that the available CRL is valid")));
            }
            break;
        }
    }
    while (crl_was_expired && !recheck_crl_done);

 free_X509_object:

    if (x509_object != NULL)
    {
        X509_OBJECT_free(x509_object);
    }

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

globus_result_t
globus_i_gsi_callback_check_signing_policy(
    X509_STORE_CTX *                    x509_context,
    globus_gsi_callback_data_t          callback_data)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    static char *                       _function_name_ =
        "globus_i_gsi_callback_check_signing_policy";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    /* Do not need to check self signed certs against ca_policy_file */

    if (X509_NAME_cmp(X509_get_subject_name(X509_STORE_CTX_get_current_cert(x509_context)),
                      X509_get_issuer_name(X509_STORE_CTX_get_current_cert(x509_context))) ||
        callback_data->check_self_signed_policy)
    {
        result = globus_i_gsi_callback_check_gaa_auth(
            x509_context, callback_data);

        if(result != GLOBUS_SUCCESS)
        {
            if(callback_data->allow_missing_signing_policy)
            {
                result = GLOBUS_SUCCESS;
            }
            else
            {
                GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_WITH_SIGNING_POLICY);
                goto exit;
            }
        }
    }

 exit:

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

globus_result_t
globus_i_gsi_callback_check_gaa_auth(
    X509_STORE_CTX *                    x509_context,
    globus_gsi_callback_data_t          callback_data)
{
    char *                              error_string = NULL;
    char *                              issuer_name = NULL;
    char *                              subject_name = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;
    char *                              ca_policy_file_path = NULL;

    oldgaa_rights_ptr                   rights = NULL;
    oldgaa_policy_ptr                   policy_handle = NULL;
    oldgaa_answer_ptr                   detailed_answer = NULL;
    oldgaa_sec_context_ptr              oldgaa_sc = NULL;
    oldgaa_options_ptr                  options = NULL;
    oldgaa_error_code                   policy_result;
    oldgaa_data_ptr                     policy_db = OLDGAA_NO_DATA;
    uint32                              minor_status;

    static char *                       _function_name_ =
        "globus_i_gsi_callback_check_gaa_auth";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    subject_name = X509_NAME_oneline(
        X509_get_subject_name(X509_STORE_CTX_get_current_cert(x509_context)),
        NULL,
        0);
    issuer_name = X509_NAME_oneline(
        X509_get_issuer_name(X509_STORE_CTX_get_current_cert(x509_context)),
        NULL,
        0);

    result =
        GLOBUS_GSI_SYSCONFIG_GET_SIGNING_POLICY_FILENAME(
            X509_get_issuer_name(X509_STORE_CTX_get_current_cert(x509_context)),
            callback_data->cert_dir,
            &ca_policy_file_path);

    if(result != GLOBUS_SUCCESS)
    {
        ca_policy_file_path = NULL;
        GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_WITH_SIGNING_POLICY);
        goto exit;
    }

    if(ca_policy_file_path == NULL)
    {
        /* signing policy file doesn't exist or can't be read */

        GLOBUS_GSI_CALLBACK_ERROR_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_WITH_SIGNING_POLICY,
            (_CLS("The signing policy file doesn't exist or can't be read")));
        X509_STORE_CTX_set_error(x509_context, X509_V_ERR_APPLICATION_VERIFICATION);
        goto exit;
    }

    GLOBUS_I_GSI_CALLBACK_DEBUG_FPRINTF(
        2, (globus_i_gsi_callback_debug_fstream,
            "ca_policy_file_path is %s\n", ca_policy_file_path));

    globus_mutex_lock(&globus_l_gsi_callback_oldgaa_mutex);

    if(oldgaa_globus_initialize(&oldgaa_sc,
                                &rights,
                                &options,
                                &policy_db,
                                issuer_name,
                                subject_name,
                                ca_policy_file_path)
       != OLDGAA_SUCCESS)
    {
        GLOBUS_GSI_CALLBACK_ERROR_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_OLD_GAA,
            (_CLS("Couldn't initialize OLD GAA: "
             "Minor status=%d"), policy_db->error_code));
        X509_STORE_CTX_set_error(x509_context, X509_V_ERR_APPLICATION_VERIFICATION);
        globus_mutex_unlock(&globus_l_gsi_callback_oldgaa_mutex);
        goto exit;
    }

    if(oldgaa_get_object_policy_info(
        &minor_status,
        OLDGAA_NO_DATA,
        policy_db,
        oldgaa_globus_policy_retrieve,
        &policy_handle) != OLDGAA_SUCCESS)
    {
        GLOBUS_GSI_CALLBACK_ERROR_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_OLD_GAA,
            (_CLS("Could not get policy info: "
             "Minor status=%d"), minor_status));
        oldgaa_globus_cleanup(&oldgaa_sc,
                              &rights,
                              options,
                              &detailed_answer,
                              policy_db,
                              NULL);
        X509_STORE_CTX_set_error(x509_context,  X509_V_ERR_APPLICATION_VERIFICATION);
        globus_mutex_unlock(&globus_l_gsi_callback_oldgaa_mutex);
        goto exit;
    }

    policy_result = oldgaa_check_authorization(
        &minor_status,
        oldgaa_sc,
        policy_handle,
        rights,
        options,
        &detailed_answer);

    if (!detailed_answer)
    {
        GLOBUS_GSI_CALLBACK_ERROR_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_OLD_GAA,
            (_CLS("No policy definitions for CA \"%s\" "
             "in signing policy file %s"),
             issuer_name == NULL ? "NULL" : issuer_name,
             ca_policy_file_path == NULL ? "NULL" : ca_policy_file_path));
        X509_STORE_CTX_set_error(x509_context, X509_V_ERR_INVALID_PURPOSE);

        oldgaa_globus_cleanup(&oldgaa_sc,
                              &rights,
                              options,
                              &detailed_answer,
                              policy_db,
                              NULL);
        globus_mutex_unlock(&globus_l_gsi_callback_oldgaa_mutex);
        goto exit;
    }

    if(GLOBUS_I_GSI_CALLBACK_DEBUG(2))
    {

        fprintf(globus_i_gsi_callback_debug_fstream,
                "oldgaa result: %d(0 yes, 1 no, -1 maybe)\n", policy_result);

        if(detailed_answer)
        {
            fprintf(globus_i_gsi_callback_debug_fstream,
                    "\nprint detailed answer:\n\n");

#ifndef WIN32
            if(detailed_answer->rights)
            {
                oldgaa_globus_print_rights(detailed_answer->rights);
            }
#endif
        }
    }

    if (policy_handle)
    {
        oldgaa_release_principals(&minor_status, &policy_handle);
    }

    oldgaa_globus_cleanup(&oldgaa_sc,
                          &rights,
                          options,
                          &detailed_answer,
                          policy_db,
                          NULL);

    globus_mutex_unlock(&globus_l_gsi_callback_oldgaa_mutex);

    if (policy_result != 0)
    {
        GLOBUS_GSI_CALLBACK_ERROR_RESULT(
            result,
            GLOBUS_GSI_CALLBACK_ERROR_OLD_GAA,
            (_CLS("The subject of the certificate \"%s\" "
             "does not match the signing policies defined in %s"),
             subject_name == NULL ? "NULL" : subject_name,
             ca_policy_file_path == NULL ? "NULL" : ca_policy_file_path));
        X509_STORE_CTX_set_error(x509_context, X509_V_ERR_INVALID_PURPOSE);
    }

 exit:

    if(ca_policy_file_path)
    {
        globus_libc_free(ca_policy_file_path);
    }

    if(error_string)
    {
        globus_libc_free(error_string);
    }

    if(issuer_name)
    {
        OPENSSL_free(issuer_name);
    }

    if(subject_name)
    {
        OPENSSL_free(subject_name);
    }

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

globus_result_t
globus_i_gsi_callback_check_critical_extensions(
    X509_STORE_CTX *                    x509_context,
    globus_gsi_callback_data_t          callback_data)
{
    const ASN1_OBJECT *                 extension_object = NULL;
#if OPENSSL_VERSION_NUMBER < 0x40000000L
    X509_EXTENSION *                    extension = NULL;
#else
    const X509_EXTENSION *              extension = NULL;
#endif
    PROXY_CERT_INFO_EXTENSION *         proxycertinfo = NULL;
    PROXY_POLICY *                      policy = NULL;
    int                                 nid;
    int                                 pci_NID;
    int                                 pci_old_NID;
    int                                 critical_position = -1;
    long                                path_length;
    globus_result_t                     result = GLOBUS_SUCCESS;
    static char *                       _function_name_ =
        "globus_i_gsi_callback_check_critical_extensions";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    pci_NID = NID_proxyCertInfo;
    pci_old_NID = OBJ_txt2nid("1.3.6.1.4.1.3536.1.222");

    while((critical_position =
          X509_get_ext_by_critical(X509_STORE_CTX_get_current_cert(x509_context),
                                   1, critical_position)) >= 0)
    {
        extension = X509_get_ext(X509_STORE_CTX_get_current_cert(x509_context), critical_position);
        if(!extension)
        {
            GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED,
                (_CLS("Couldn't get critical extension of "
                 "certificate being verified")));
            X509_STORE_CTX_set_error(x509_context, X509_V_ERR_CERT_REJECTED);
            goto exit;
        }

        extension_object = X509_EXTENSION_get_object(extension);
        if(!extension_object)
        {
            GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                result,
                GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED,
                (_CLS("Couldn't get object form of X509 extension for "
                 "the certificate being verified.")));
            X509_STORE_CTX_set_error(x509_context, X509_V_ERR_CERT_REJECTED);
            goto exit;
        }

        nid = OBJ_obj2nid(extension_object);

        if(nid == pci_NID || nid == pci_old_NID)
        {
            /* check for path length constraint */

            if((proxycertinfo = X509V3_EXT_d2i(extension)) == NULL)
            {
                GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED,
                    (_CLS("Can't convert DER encoded PROXYCERTINFO "
                     "extension to internal form")));
                X509_STORE_CTX_set_error(x509_context, X509_V_ERR_CERT_REJECTED);
                goto exit;
            }
            if (proxycertinfo->pcPathLengthConstraint != NULL)
            {
                path_length = ASN1_INTEGER_get(proxycertinfo->pcPathLengthConstraint);
                if(path_length > -1)
                {
                    if(callback_data->max_proxy_depth == -1 ||
                       callback_data->max_proxy_depth >
                       callback_data->proxy_depth + path_length)
                    {
                        callback_data->max_proxy_depth =
                            callback_data->proxy_depth + path_length;
                    }
                }
            }

            policy = proxycertinfo->proxyPolicy;
        }

        if((nid != NID_basic_constraints &&
            nid != NID_key_usage &&
            nid != NID_ext_key_usage &&
            nid != NID_netscape_cert_type &&
            nid != NID_subject_key_identifier &&
            nid != NID_authority_key_identifier &&
            nid != pci_NID &&
            nid != pci_old_NID) || (policy && policy->policy))
        {
            if(callback_data->extension_cb)
            {
                if(!callback_data->extension_cb(callback_data, extension))
                {
                    GLOBUS_GSI_CALLBACK_ERROR_RESULT(
                        result,
                        GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED,
                        (_CLS("Certificate has unknown critical extension "
                         "with numeric ID: %d, "
                         "rejected during validation"), nid));
                    X509_STORE_CTX_set_error(x509_context, X509_V_ERR_CERT_REJECTED);
                    goto exit;
                }
            }
            else
            {
                GLOBUS_GSI_CALLBACK_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED,
                    (_CLS("Certificate has unknown critical extension, "
                     "with numeric ID: %d, "
                     "rejected during validation"), nid));
                X509_STORE_CTX_set_error(x509_context, X509_V_ERR_CERT_REJECTED);
                goto exit;
            }
        }
    }

 exit:

    if(proxycertinfo != NULL)
    {
        PROXY_CERT_INFO_EXTENSION_free(proxycertinfo);
    }

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

globus_result_t
globus_i_gsi_callback_check_path_length(
    X509_STORE_CTX *                    x509_context,
    globus_gsi_callback_data_t          callback_data)
{
    X509 *                              cert = NULL;
    globus_result_t                     result  = GLOBUS_SUCCESS;
    int                                 i;

    static char *                       _function_name_ =
        "globus_i_gsi_callback_check_path_length";

    GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER;

    /*
     * We ignored any path length restriction errors because
     * OpenSSL was counting proxies against the limit.
     * If we are on the last cert in the chain, we
     * know how many are proxies, so we can do the
     * path length check now.
     * See x509_vfy.c check_chain_purpose
     * all we do is substract off the proxy_depth
     */

    if(X509_STORE_CTX_get_current_cert(x509_context) == X509_STORE_CTX_get0_cert(x509_context))
    {
        for (i = 0; i < sk_X509_num(X509_STORE_CTX_get0_chain(x509_context)); i++)
        {
            cert = sk_X509_value(X509_STORE_CTX_get0_chain(x509_context), i);

            GLOBUS_I_GSI_CALLBACK_DEBUG_FPRINTF(
                3, (globus_i_gsi_callback_debug_fstream,
                    "pathlen=:i=%d x=%p pl=%ld\n",
                    i, cert, X509_get_pathlen(cert)));

            if (((i - callback_data->proxy_depth) > 1) &&
                (X509_get_pathlen(cert) != -1) &&
                ((i - callback_data->proxy_depth) > (X509_get_pathlen(cert) + 1)) &&
                (X509_get_extension_flags(cert) & EXFLAG_BCONS))
            {
                X509_STORE_CTX_set_current_cert(x509_context, cert); /* point at failing cert */
                GLOBUS_GSI_CALLBACK_ERROR_RESULT(
                    result,
                    GLOBUS_GSI_CALLBACK_ERROR_VERIFY_CRED,
                    (_CLS("Path length of proxy cert has exceeded the limit")));
            }
        }
    }

    GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT;
    return result;
}

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
