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
 * @file globus_i_gsi_gss_utils.c
 * @author Sam Lang, Sam Meder
 */

#include "gssapi_openssl.h"
#include "globus_i_gsi_gss_utils.h"
#include "globus_gsi_credential.h"
#include "globus_gsi_callback.h"
#include "globus_gsi_callback_constants.h"
#include "globus_gsi_system_config.h"
#include "openssl/ssl3.h"

#include <string.h>
#include <stdlib.h>

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#include "globus_ssl_locl.h"

#define X509_STORE_set_check_issued(store, func) (store)->check_issued = (func)
#endif

#ifdef WIN32
#define strcasecmp stricmp
#define strncasecmp strnicmp
#endif

extern int                              globus_i_gsi_gssapi_debug_level;
extern FILE *                           globus_i_gsi_gssapi_debug_fstream;

static
int
globus_l_gsi_gss_servername_callback(
    SSL                                *SSL,
    int                                *ad,
    void                               *callback_arg);

static
int
globus_l_gsi_gss_alpn_select_callback(
    SSL                                *ssl,
    const unsigned char               **out,
    unsigned char                      *outlen,
    const unsigned char                *in,
    unsigned int                        inlen,
    void                               *arg);
/**
 * @defgroup globus_i_gsi_gss_utils Globus GSSAPI Internals
 *
 * The globus_i_gsi_gss_utils code is used by the other 
 * gss api code to perform internal functions such as
 * initializing objects and performing the SSL handshake
 */

/**
 * @brief Copy GSS API Name
 * @ingroup globus_i_gsi_gss_utils
 * @details
 * Copy a gss_name_t structure, including the group information
 * and the name's OID.
 *
 * @param minor_status
 *        This minor status contains the resulting error
 *        if an error occurred.  The error should be cast
 *        to a globus_result_t before accessing
 * @param output
 *        Target name.
 * @param input
 *        Source name.
 * @retval GSS_S_COMPLETE Success
 * @retval GSS_F_FAILURE Failed to copy
 */
OM_uint32 
globus_i_gsi_gss_copy_name_to_name(
    OM_uint32 *                         minor_status,
    gss_name_desc **                    output,
    const gss_name_desc *               input)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    gss_name_desc *                     output_name;
    X509_NAME *                         x509n = NULL;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;
    
    output_name = calloc(1, sizeof(gss_name_desc));

    if (output_name == NULL)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_BAD_NAME;
        goto exit;
    }

    output_name->name_oid = input->name_oid;
    
    if(input->x509n != NULL)
    {
        x509n = X509_NAME_dup(input->x509n);
        if (x509n == NULL)
        {
            GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME,
                (_GGSL("Couldn't copy X509_NAME struct")));
            major_status = GSS_S_BAD_NAME;
            goto exit;
        }
    }
    output_name->x509n = x509n;

    if (input->x509n_oneline != NULL)
    {
        output_name->x509n_oneline = globus_libc_strdup(input->x509n_oneline);

        if (output_name->x509n_oneline == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }

    if (input->subjectAltNames != NULL)
    {
        output_name->subjectAltNames = ASN1_item_dup(
                ASN1_ITEM_rptr(GENERAL_NAMES),
                input->subjectAltNames);
        if (output_name->subjectAltNames == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }
    
    if (input->user_name != NULL)
    {
        output_name->user_name = globus_libc_strdup(input->user_name);
        if (output_name->user_name == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }

    if (input->service_name != NULL)
    {
        output_name->service_name = globus_libc_strdup(input->service_name);
        if (output_name->service_name == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }

    if (input->host_name != NULL)
    {
        output_name->host_name = globus_libc_strdup(input->host_name);
        if (output_name->host_name == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }

    if (input->ip_address != NULL)
    {
        output_name->ip_address = globus_libc_strdup(input->ip_address);
        if (output_name->ip_address == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }

    if (input->ip_name != NULL)
    {
        output_name->ip_name = globus_libc_strdup(input->ip_name);
        if (output_name->ip_name == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }
    *output = output_name;

    *minor_status = (OM_uint32) GLOBUS_SUCCESS;
    major_status = GSS_S_COMPLETE;

 exit:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;    
    return major_status;
}

/**
 * @brief Initialize a Security Context
 * @ingroup globus_i_gsi_gss_utils
 * @details
 *
 * Called by the GSSAPI functions @ref gss_init_sec_context 
 * and @ref gss_accept_sec_context to 
 * setup the initial context. This includes establishing the
 * SSL session control blocks.
 *
 * @param minor_status
 *        A mechanism specific status code.  This is cast from a 
 *        globus_result_t which will either be GLOBUS_SUCCESS or
 *        a globus error object ID.  If not GLOBUS_SUCCESS, the
 *        globus_error_t object can be retrieved by doing:
 *        globus_error_get((globus_result_t)*minor_status)
 * 
 * @param context_handle_P
 *        
 * @param cred_handle
 * @param cred_usage
 * @param req_flags
 * @return
 */
OM_uint32
globus_i_gsi_gss_create_and_fill_context(
    OM_uint32 *                         minor_status,
    gss_ctx_id_desc **                  context_handle_P,
    gss_OID                             mech,
    const gss_name_t                    target_name,
    gss_cred_id_desc *                  cred_handle,
    const gss_cred_usage_t              cred_usage,
    OM_uint32                           req_flags)
{
    globus_result_t                     local_result;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    gss_ctx_id_desc*                    context = NULL;
    int                                 cb_index = -1;
    OM_uint32                           local_minor_status;
    char *                              certdir = NULL;
    globus_bool_t                       allocated_context = GLOBUS_FALSE;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    *minor_status = GLOBUS_SUCCESS;

    /* initialize the context handle */
    if(*context_handle_P == GSS_C_NO_CONTEXT)
    {
        context = (gss_ctx_id_desc*) 
            malloc(sizeof(gss_ctx_id_desc));
        if (context == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
        
        memset(context, 0, sizeof(gss_ctx_id_desc));
        *context_handle_P = context;
        context->ctx_flags = 0;
        allocated_context = GLOBUS_TRUE;
    }
    else
    {
        context = *context_handle_P;
    }
    
    context->req_flags = req_flags;
    context->gss_state = GSS_CON_ST_HANDSHAKE;
    context->delegation_state = GSS_DELEGATION_START;
    context->locally_initiated = (cred_usage == GSS_C_INITIATE);
    context->ctx_flags |= GSS_I_CTX_INITIALIZED;
    context->mech = mech;
#if OPENSSL_VERSION_NUMBER >= 0x10000100L
    context->mac_read_sequence = 0;
    context->mac_write_sequence = 0;
    context->mac_key = NULL;
    context->mac_iv_fixed = NULL;
#endif

    globus_mutex_init(&context->mutex, NULL);

    /* initialize the peer_cred_handle */
    context->peer_cred_handle = calloc(1, sizeof(gss_cred_id_desc));
    if(context->peer_cred_handle == NULL)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;
        goto free_context;
    }
    
    local_result = globus_gsi_cred_handle_init(
        &context->peer_cred_handle->cred_handle, NULL);
    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result, 
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto free_peer_cred;
    }

    /* initialize the proxy_handle */
    local_result = globus_gsi_proxy_handle_init(&context->proxy_handle, NULL);
    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_PROXY);
        major_status = GSS_S_FAILURE;
        goto free_peer_cred_handle;
    }

    /* initialize the callback data */
    if(context->callback_data == NULL)
    {
        local_result = globus_gsi_callback_data_init(&context->callback_data);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_CALLBACK_DATA);
            major_status = GSS_S_FAILURE;
            goto free_proxy_handle;
        }
    }
    
    /* if the extension_oids are set, then we set them in the callback data */
    if(context->extension_oids)
    {
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
    }
    
    /* get the local credential */
    if (cred_handle == GSS_C_NO_CREDENTIAL || (req_flags & GSS_C_ANON_FLAG))
    {
        if(req_flags & GSS_C_ANON_FLAG)
        {
            major_status = globus_i_gsi_gss_create_anonymous_cred(
                &local_minor_status, 
                (gss_cred_id_t *) &context->cred_handle, 
                cred_usage);
        }
        else
        {
            major_status = gss_acquire_cred(
                &local_minor_status, 
                GSS_C_NO_NAME, 
                GSS_C_INDEFINITE,
                GSS_C_NO_OID_SET, 
                cred_usage, 
                (gss_cred_id_t *) &context->cred_handle, 
                NULL, 
                NULL);

            /* OK if that failed, if we are using SNI */
            if (major_status != GSS_S_COMPLETE
                && cred_usage != GSS_C_INITIATE
                && context->sni_credentials == NULL
                && getenv("X509_VHOST_CRED_DIR") != NULL)
            {
                globus_gsi_cred_handle_t    cred_handle = NULL;

                local_result = globus_gsi_cred_handle_init(&cred_handle, NULL);
                if (local_result != GLOBUS_SUCCESS)
                {
                    local_minor_status = local_result;
                    major_status = GSS_S_FAILURE;
                }
                else
                {
                    major_status = globus_i_gsi_gss_create_cred(
                        &local_minor_status,
                        cred_usage,
                        &context->cred_handle,
                        &cred_handle,
                        GLOBUS_TRUE);
                    if (major_status != GSS_S_COMPLETE && cred_handle != NULL)
                    {
                        globus_gsi_cred_handle_destroy(cred_handle);
                    }
                }
            }
        }
        
        if (GSS_ERROR(major_status))
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
            goto free_callback_data;
        }

        context->cred_obtained = 1;
    }
    else
    {
        context->cred_obtained = 0;
        context->cred_handle = cred_handle;
    }
    /* Set up the SNI callback if this is the accept side, and the app
     * has provided an array of credentials to use
     */
    if (cred_usage != GSS_C_INITIATE)
    {
        if (context->sni_credentials == NULL
            && getenv("X509_VHOST_CRED_DIR") != NULL)
        {
            size_t                      sni_creds_len = 0;

            major_status = globus_i_gss_read_vhost_cred_dir(
                minor_status,
                NULL,
                &context->sni_credentials,
                &context->sni_credentials_count);
            if(major_status != GLOBUS_SUCCESS)
            {
                goto free_callback_data;
            }
        }
        if (context->sni_credentials_count > 0)
        {
            SSL_CTX_set_tlsext_servername_callback(
                context->cred_handle->ssl_context,
                globus_l_gsi_gss_servername_callback);

            SSL_CTX_set_tlsext_servername_arg(
                context->cred_handle->ssl_context,
                context);
        }
    }
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
    if (cred_usage != GSS_C_INITIATE
        && context->alpn != NULL)
    {
        SSL_CTX_set_alpn_select_cb(
            context->cred_handle->ssl_context,
            globus_l_gsi_gss_alpn_select_callback,
            context);
    }
#endif

    /* set the cert_dir in the callback data */
    local_result = 
        GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&certdir);
    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto free_callback_data;
    }

    if (certdir)
    {
        local_result = globus_gsi_callback_set_cert_dir(
            context->callback_data, 
            certdir);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
            major_status = GSS_S_FAILURE;
            goto free_cert_dir;
        }
    }
    else
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status, 
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL,
            (_GGSL("The cert_dir parameter in "
             "the credential handle needs to bet set")));
        major_status = GSS_S_FAILURE;
        goto free_cert_dir;
    }

    free(certdir);
    certdir = NULL;
    
    if(req_flags & GSS_C_GLOBUS_ALLOW_MISSING_SIGNING_POLICY)
    {
        local_result = globus_gsi_callback_set_allow_missing_signing_policy(
            context->callback_data,
            GLOBUS_TRUE);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_CALLBACK_DATA);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }    
    
    #if (OPENSSL_VERSION_NUMBER >= 0x009080dfL)
    {
        /*
         * OpenSSL 0.9.8m and 1.0.0 introduce changes to how
         * ssl3_output_cert_chain creates the certificate chain to send.
         *
         * The new code uses X509_verify_cert(), which fails if
         * a certificate does not have a X509_V_ERR_KEYUSAGE_NO_CERTSIGN but
         * signs a proxy. As a result, the entire certificate chain is not
         * sent during the handshake.
         *
         * This code causes the issuer checks to use
         * globus_gsi_callback_check_issued() to handle that error if
         * the certificate in question is a proxy.
         */
        X509_STORE * store = SSL_CTX_get_cert_store(
                context->cred_handle->ssl_context);
        X509_STORE_set_check_issued(store, globus_gsi_callback_check_issued);
    }
    #endif

    context->gss_ssl = SSL_new(context->cred_handle->ssl_context);

    if (context->gss_ssl == NULL)
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status, 
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CONTEXT,
            (_GGSL("Couldn't create SSL object for handshake")));
        major_status = GSS_S_FAILURE;
        goto free_cert_dir;
    }
    /* Set the SNI name if we're the client and have a target */
    if (cred_usage == GSS_C_INITIATE &&
        target_name != GSS_C_NO_NAME &&
        target_name->host_name != NULL)
    {
        SSL_set_tlsext_host_name(
                    context->gss_ssl,
                    target_name->host_name);
    }
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
    /* Set the ALPN if we're the client and have one set */
    if (cred_usage == GSS_C_INITIATE
        && context->alpn != NULL)
    {
        SSL_set_alpn_protos(
            context->gss_ssl, context->alpn, context->alpn_length);
    }
#endif

    /* No longer setting SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS since it seemed
     * like a stop-gap measure to interoperate with broken SSL */

    local_result = globus_gsi_callback_get_SSL_callback_data_index(&cb_index);
    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL);
        major_status = GSS_S_FAILURE;
        goto free_cert_dir;
    }
    
    if(!SSL_set_ex_data(context->gss_ssl, 
                        cb_index,
                        (char *) &context->callback_data))
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
            (_GGSL("Couldn't set the callback data as the external data "
             "of the SSL object")));
        major_status = GSS_S_FAILURE;
        goto free_cert_dir;
    }

    /*
     * If initiate and caller did not set the GSS_C_CONF_FLAG
     * then add the NULL ciphers to beginning.
     */
    if (!(context->req_flags & GSS_C_CONF_FLAG))
    {
        if(!SSL_set_cipher_list(context->gss_ssl,
                                "eNULL:ALL:!COMPLEMENTOFDEFAULT"))
        {
            GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
                (_GGSL("Couldn't set the cipher cert order in the SSL object")));
            major_status = GSS_S_FAILURE;
            goto free_cert_dir;   
        }
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
        /* Security level 0 is needed to use eNULL ciphers */
        SSL_set_security_level(context->gss_ssl, 0);
#endif
    }
    else if (globus_i_gsi_gssapi_cipher_list != NULL)
    {
        if(!SSL_set_cipher_list(context->gss_ssl, globus_i_gsi_gssapi_cipher_list))
        {
            GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
                (_GGSL("Couldn't set the cipher list in the SSL object")));
            major_status = GSS_S_FAILURE;
            goto free_cert_dir;   
        }
        
    }
    
    if (cred_usage == GSS_C_ACCEPT && globus_i_gsi_gssapi_server_cipher_order)
    {
        SSL_set_options(context->gss_ssl, SSL_OP_CIPHER_SERVER_PREFERENCE);
    }
    GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
        3, (globus_i_gsi_gssapi_debug_fstream,
            "SSL is at %p\n", context->gss_ssl));
    GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
        3, (globus_i_gsi_gssapi_debug_fstream,
            "SSL_set_app_data to callback data %p\n", context->callback_data));
    
    if ((context->gss_rbio = BIO_new(BIO_s_mem())) == NULL)
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
            (_GGSL("Can't initialize read BIO for SSL handle")));
        major_status = GSS_S_FAILURE;
        goto free_cert_dir;
    }

    if ((context->gss_wbio = BIO_new(BIO_s_mem())) == NULL)
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
            (_GGSL("Can't initialize write BIO for SSL handle")));
        major_status = GSS_S_FAILURE;
        goto free_rbio;
    }

    if ((context->gss_sslbio = BIO_new(BIO_f_ssl())) == NULL)
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
            (_GGSL("Can't create SSL bio")));
        major_status = GSS_S_FAILURE;
        goto free_wbio;
    }
    if ( cred_usage == GSS_C_INITIATE)
    {
        SSL_set_connect_state(context->gss_ssl);
    }
    else
    {
        SSL_set_accept_state(context->gss_ssl);
    }
    
    SSL_set_bio(context->gss_ssl,
                context->gss_rbio,
                context->gss_wbio);
    
    BIO_set_ssl(context->gss_sslbio, 
                context->gss_ssl, 
                BIO_NOCLOSE);
    
    {
        char buff[256];
        int i;
        STACK_OF(SSL_CIPHER) *sk;
        
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(
            2, "Ciphers available:\n");
        sk=SSL_get_ciphers(context->gss_ssl);
        for (i=0; i<sk_SSL_CIPHER_num(sk); i++)
        {
            SSL_CIPHER_description(sk_SSL_CIPHER_value(sk,i),
                                   buff,256);
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                3, (globus_i_gsi_gssapi_debug_fstream, "%s", buff));
        }
    }

    if(!context->extension_oids)
    {
        major_status = gss_create_empty_oid_set(
            &local_minor_status,
            (gss_OID_set *) &context->extension_oids);
        
        if(GSS_ERROR(major_status))
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_OID);
            goto exit;
        }
    }
    
    goto exit;

 free_wbio:

    if(context->gss_wbio)
    {
        BIO_free(context->gss_wbio);
        context->gss_wbio = NULL;
    }

 free_rbio:

    if(context->gss_rbio)
    {
        BIO_free(context->gss_rbio);
        context->gss_rbio = NULL;
    }

 free_cert_dir:
    
    free(certdir);
    certdir = NULL;

    if(context->cred_handle && context->cred_obtained)
    {
        gss_release_cred(&local_minor_status, 
                         (gss_cred_id_t *) &context->cred_handle);
        context->cred_handle = NULL;
    }

 free_callback_data:
    if(context->callback_data)
    {
        globus_gsi_callback_data_destroy(context->callback_data);
        context->callback_data = NULL;
    }

 free_proxy_handle:
    
    if(context->proxy_handle != NULL)
    {
        globus_gsi_proxy_handle_destroy(context->proxy_handle);
        context->proxy_handle = NULL;
    }

 free_peer_cred_handle:

    if(context->peer_cred_handle->cred_handle)
    {
        globus_gsi_cred_handle_destroy(context->peer_cred_handle->cred_handle);
        context->peer_cred_handle->cred_handle = NULL;
    }

 free_peer_cred:

    free(context->peer_cred_handle);
    context->peer_cred_handle = NULL;

 free_context:

    if(allocated_context)
    {
        free(context);
        *context_handle_P = NULL;
    }

 exit:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

/**
 * @brief Put Token
 * @ingroup globus_i_gsi_gss_utils
 * @details
 * Called by init_sec_context and accept_sec_context.
 * An input token is placed in the SSL read BIO
 * 
 * @param minor_status
 * @param context_handle
 * @param bio
 * @param input_token
 *
 * @return
 */
OM_uint32
globus_i_gsi_gss_put_token(
    OM_uint32 *                         minor_status,
    const gss_ctx_id_desc *             context_handle,
    BIO *                               bio,
    const gss_buffer_t                  input_token)
{
    BIO *                               read_bio;
    OM_uint32                           major_status = GSS_S_COMPLETE;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    if(input_token == GSS_C_NO_BUFFER)
    {
        major_status = GSS_S_DEFECTIVE_TOKEN;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_TOKEN_FAIL,
            (_GGSL("The input token is NULL (GSS_C_NO_BUFFER)\n")));
        goto exit;        
    }

    if(bio)
    {
        read_bio = bio;
    }
    else
    {
        read_bio = context_handle->gss_rbio;
    }

    /* add any input data onto the input for the SSL BIO */

    if (input_token->length > 0)
    {
        BIO_write(read_bio,
                  input_token->value,
                  input_token->length);

        if(GLOBUS_I_GSI_GSSAPI_DEBUG(3))
        {
            BIO *                       debug_bio;
            fprintf(globus_i_gsi_gssapi_debug_fstream,
                    "input token: length = %lu\n"
                    "              value  = \n",
                    (unsigned long) input_token->length);
        
            debug_bio = BIO_new_fp(globus_i_gsi_gssapi_debug_fstream, 
                                   BIO_NOCLOSE);
            BIO_dump(debug_bio, 
                     input_token->value,
                     input_token->length);
            BIO_free(debug_bio);
        }
    }
    else 
    {
        GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
            3, (globus_i_gsi_gssapi_debug_fstream,
                "input_token: length = %lu\n",
                (unsigned long) input_token->length));

        major_status = GSS_S_DEFECTIVE_TOKEN;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_TOKEN_FAIL,
            (_GGSL("The input token has an invalid length of: %lu\n"), 
             (unsigned long) input_token->length));
        goto exit;
    }

 exit:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

/**
 * @brief Get Token
 * @ingroup globus_i_gsi_gss_utils
 * @details
 * Get the token from the context handle
 *
 * @param minor_status
 * @param context_handle
 * @param bio
 * @param output_token
 * 
 * @return
 */
OM_uint32
globus_i_gsi_gss_get_token(
    OM_uint32 *                         minor_status,
    const gss_ctx_id_desc*              context_handle,
    BIO *                               bio,
    const gss_buffer_t                  output_token)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    BIO *                               write_bio;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    *minor_status = (OM_uint32) GLOBUS_SUCCESS;

    if(bio)
    {
        write_bio = bio;
    }
    else
    {
        write_bio = context_handle->gss_wbio;
    }
    
    /* make out token */
    output_token->length = BIO_pending(write_bio);
    if (output_token->length > 0)
    {
        int                             len = 0;
        int                             rc;

        output_token->value = (char *) malloc(output_token->length);
        if (output_token->value == NULL)
        {
            output_token->length = 0;
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }

        while(len < output_token->length)
        { 
            
            rc = BIO_read(write_bio,
                          ((char *) output_token->value) + len,
                          output_token->length - len);
            if(rc > 0)
            {
                len += rc;
            }
            else
            {
                GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                    minor_status, 
                    GLOBUS_GSI_GSSAPI_ERROR_TOKEN_FAIL,
                    (_GGSL("Error reading token from BIO: %d\n"), rc));
                major_status = GSS_S_FAILURE;
                goto exit;
            }
        }            

        if(GLOBUS_I_GSI_GSSAPI_DEBUG(3))
        {
            BIO *                       debug_bio;
            fprintf(globus_i_gsi_gssapi_debug_fstream,
                    "output token: length = %lu\n"
                    "              value  = \n",
                    (unsigned long) output_token->length);
        
            debug_bio = BIO_new_fp(globus_i_gsi_gssapi_debug_fstream, 
                                   BIO_NOCLOSE);
            BIO_dump(debug_bio, 
                     output_token->value,
                     output_token->length);
            BIO_free(debug_bio);
        }
    }
    else
    {
        output_token->value = NULL;
    }

exit:
    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

/**
 * @brief Handshake
 * @ingroup globus_i_gsi_gss_utils
 * @details
 * Do the SSL handshake
 *
 * Called by init_sec_context and accept_sec_context.
 * the BIO_handshake is done again which causes the SSL 
 * session to start or continue its handshake process,
 * and when it waits return. 
 *
 * @param minor_status
 * @param context_handle
 *
 * @return 
 */
OM_uint32    
globus_i_gsi_gss_handshake(
    OM_uint32 *                         minor_status,
    gss_ctx_id_desc *                   context_handle)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_result_t                     local_result = GLOBUS_SUCCESS;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    int rc;
    
    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    ERR_clear_error();
    /*
     * do the BIO_do_handshake which may produce output,
     * and endup waiting for input.
     * when completed without error, connection established
     */
    rc = BIO_do_handshake(context_handle->gss_sslbio);
    if (rc <= 0) {
        if (!BIO_should_retry(context_handle->gss_sslbio) || 
            !BIO_should_read(context_handle->gss_sslbio)) {
            
            /* problem! */
            
#if OPENSSL_VERSION_NUMBER < 0x10100000L
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "disp=%d,level=%d,desc=%d,left=%d\n",
                    context_handle->gss_ssl->s3->alert_dispatch,
                    context_handle->gss_ssl->s3->send_alert[0],
                    context_handle->gss_ssl->s3->send_alert[1],
                    context_handle->gss_ssl->s3->wbuf.left));
#endif

            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "SSL_get_error = %d\n",
                    SSL_get_error(context_handle->gss_ssl, rc)));

            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream, 
                    "shutdown=%d\n",
                    SSL_get_shutdown(context_handle->gss_ssl)));

            /* checks for ssl alert 42 */
            if (ERR_peek_error() == 
                ERR_PACK(ERR_LIB_SSL,SSL_F_SSL3_READ_BYTES,
                         SSL_R_SSLV3_ALERT_BAD_CERTIFICATE))
            {
                GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_REMOTE_CERT_VERIFY_FAILED,
                    (_GGSL("Couldn't verify the remote certificate")));
            }
            else
            {
                GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_HANDSHAKE,
                    (_GGSL("Couldn't do ssl handshake")));
            }

            major_status = GSS_S_DEFECTIVE_CREDENTIAL;
        }
    }

    local_result = globus_gsi_callback_get_error(context_handle->callback_data,
                                                 &result);

    if(local_result != GLOBUS_SUCCESS)
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_CALLBACK_DATA);
        goto exit;
    }
    
    if(result != GLOBUS_SUCCESS && GSS_ERROR(major_status))
    {
        result = globus_i_gsi_gssapi_error_join_chains_result(
            (globus_result_t) *minor_status,
            result);
        
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, result,
            GLOBUS_GSI_GSSAPI_ERROR_REMOTE_CERT_VERIFY_FAILED);
        goto exit;
    }
    else if(GSS_ERROR(major_status))
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, *minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_REMOTE_CERT_VERIFY_FAILED);
        goto exit;
    }
    else if(result != GLOBUS_SUCCESS)
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, result,
            GLOBUS_GSI_GSSAPI_ERROR_REMOTE_CERT_VERIFY_FAILED);
        goto exit;
    }

    if (!GSS_ERROR(major_status))
    {
        if (rc > 0)
        {
            const EVP_MD *          hash = NULL;
            const EVP_CIPHER *      evp_cipher = NULL;
            const SSL_CIPHER *      current_cipher = NULL;

            #if OPENSSL_VERSION_NUMBER >= 0x10000100L
            size_t                      keying_material_len = 0;
            #endif

            major_status = globus_i_gss_get_hash(
                    minor_status,
                    context_handle,
                    &hash,
                    &evp_cipher);

            if (GSS_ERROR(major_status))
            {
                goto exit;
            }

            current_cipher = SSL_get_current_cipher(context_handle->gss_ssl);

            #if OPENSSL_VERSION_NUMBER >= 0x10000100L
            if (evp_cipher != NULL && EVP_CIPHER_key_length(evp_cipher) > 0)
            {
                keying_material_len = EVP_CIPHER_key_length(evp_cipher);
                context_handle->mac_key = malloc(keying_material_len);

                if (context_handle->mac_key == NULL)
                {
                    GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                    major_status = GSS_S_FAILURE;
                    goto exit;
                }

                SSL_export_keying_material(
                        context_handle->gss_ssl,
                        context_handle->mac_key,
                        keying_material_len,
                        "EXPERIMENTAL-GSI-MAC-KEY",
                        strlen("EXPERIMENTAL-GSI-MAC-KEY"),
                        NULL,
                        0,
                        0);

                keying_material_len = EVP_CIPHER_iv_length(evp_cipher);
                context_handle->mac_iv_fixed = malloc(keying_material_len);
                if (context_handle->mac_iv_fixed == NULL)
                {
                    GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                    major_status = GSS_S_FAILURE;
                    goto exit;
                }

                SSL_export_keying_material(
                        context_handle->gss_ssl,
                        context_handle->mac_iv_fixed,
                        keying_material_len,
                        "EXPERIMENTAL-GSI-MAC-IV-FIXED",
                        strlen("EXPERIMENTAL-GSI-MAC-IV-FIXED"),
                        NULL,
                        0,
                        0);
            }
            else
            {
                if (hash != NULL)
                {
                    keying_material_len = EVP_MD_size(hash);
                    context_handle->mac_key = malloc(keying_material_len);
                }
                if (context_handle->mac_key == NULL)
                {
                    GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                    major_status = GSS_S_FAILURE;
                    goto exit;
                }

                SSL_export_keying_material(
                        context_handle->gss_ssl,
                        context_handle->mac_key,
                        keying_material_len,
                        "EXPERIMENTAL-GSI-MAC-KEY",
                        strlen("EXPERIMENTAL-GSI-MAC-KEY"),
                        NULL,
                        0,
                        0);
            }
            #endif

            /*
             * Set  GSS_C_CONF_FLAG if cipher uses encryption
             * which is at least 56 bit. SSL defines a number
             * of different levels, we need to map to a single GSS
             * flag. See the s3_lib.c for list of ciphers. 
             * This could be changed to SSL_MEDIUM or SSL_HIGH 
             * if a site wants higher protection. 
             */

            
#if OPENSSL_VERSION_NUMBER < 0x10100000L
            if ((current_cipher->algo_strength & SSL_STRONG_MASK) >= SSL_LOW) 
            {
                context_handle->ret_flags |= GSS_C_CONF_FLAG;
            }
#else
            /*
             *  We don't have access to the algorithm strength for version
             *  >= 1.1.0, so we set the flag if the cipher is not a NULL
             *  encryption cipher.
             */

            if (evp_cipher != NULL)
            {
                context_handle->ret_flags |= GSS_C_CONF_FLAG;
            }
#endif
            /* DEBUG BLOCK */
            if (globus_i_gsi_gssapi_debug_level >= 2)
            {
                char                    cipher_description[256];
                GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(
                    2, "SSL handshake finished\n");
                GLOBUS_I_GSI_GSSAPI_DEBUG_FNPRINTF(
                    2, (20, "Using %s.\n",
                        SSL_get_version(context_handle->gss_ssl)));
                GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                    2, (globus_i_gsi_gssapi_debug_fstream,
                        "cred_usage=%d\n",
                        context_handle->cred_handle->cred_usage));
                GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(
                    2, "Cipher being used:\n");
                SSL_CIPHER_description(
                    current_cipher,
                    cipher_description, 256);
                GLOBUS_I_GSI_GSSAPI_DEBUG_FNPRINTF(
                    2, (256, "%s", cipher_description));
            }

        }
        else 
        {
            major_status = GSS_S_CONTINUE_NEEDED;
        }
    }

 exit:
    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

/**
 * @brief Retrieve Peer
 * @ingroup globus_i_gsi_gss_utils
 * @details
 * Called after the handshake has completed successfully,
 * and gets the subject name, so it can be returned to the
 * call of the GSSAPI init_sec_context or accept_sec_context. 
 * 
 * @param minor_status
 * @param context_handle
 * @param cred_usage
 *
 * @return
 */
OM_uint32
globus_i_gsi_gss_retrieve_peer(
    OM_uint32 *                         minor_status,
    gss_ctx_id_desc *                   context_handle,
    const gss_cred_usage_t              cred_usage) 
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    globus_result_t                     local_result = GLOBUS_SUCCESS;
    gss_buffer_desc                     peer_buffer;
    X509 *                              peer_cert = NULL;
    X509 *                              identity_cert = NULL;
    STACK_OF(X509) *                    peer_cert_chain = NULL;
    
    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;
    
    *minor_status = GLOBUS_SUCCESS;

    if (SSL_get_session(context_handle->gss_ssl) != NULL)
    {
        peer_cert = SSL_get_peer_certificate(
                context_handle->gss_ssl);
    }

    if(peer_cert == NULL)
    {
        peer_buffer.value = NULL;
        peer_buffer.length = 0;

        major_status = gss_import_name(
                minor_status,
                &peer_buffer,
                GSS_C_NT_ANONYMOUS,
                &context_handle->peer_cred_handle->globusid);
    }
    else
    {
        local_result = globus_gsi_cred_set_cert(
            context_handle->peer_cred_handle->cred_handle, 
            peer_cert);

        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_GETTING_PEER_CRED);
            major_status = GSS_S_FAILURE;
            goto exit;
        }

        local_result = globus_gsi_callback_get_cert_chain(
            context_handle->callback_data,
            &peer_cert_chain);
        
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_CALLBACK_DATA);
            major_status = GSS_S_FAILURE;
            peer_cert_chain = NULL;
            goto exit;
        }
        
        local_result = globus_gsi_cert_utils_get_identity_cert(
            peer_cert_chain,
            &identity_cert);
        if(local_result != GLOBUS_SUCCESS || identity_cert == NULL)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_GETTING_PEER_CRED);
            major_status = GSS_S_FAILURE;
            goto exit;
        }

        peer_buffer.value = identity_cert;
        peer_buffer.length = sizeof(X509*);

        major_status = gss_import_name(
                minor_status,
                &peer_buffer,
                GLOBUS_GSS_C_NT_X509,
                &context_handle->peer_cred_handle->globusid);

        X509_free(sk_X509_shift(peer_cert_chain));
        
        local_result = globus_gsi_cred_set_cert_chain(
            context_handle->peer_cred_handle->cred_handle, 
            peer_cert_chain);

        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_GETTING_PEER_CRED);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
            
        /* debug statement */
        { 
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream, 
                    "X509 subject after proxy : %s\n", 
                    context_handle->peer_cred_handle->globusid->x509n_oneline));
        }
    }

 exit:

    if(peer_cert_chain)
    { 
        sk_X509_pop_free(peer_cert_chain, X509_free);
    }
    if (peer_cert != NULL)
    {
        X509_free(peer_cert);
    }

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

/**
 * @brief Create Anonymous Cred
 * @ingroup globus_i_gsi_gss_utils
 *
 * @param minor_status
 * @param output_cred_handle
 * @param cred_usage
 *
 * @return
 */
OM_uint32
globus_i_gsi_gss_create_anonymous_cred(
    OM_uint32 *                         minor_status,
    gss_cred_id_t *                     output_cred_handle,
    const gss_cred_usage_t              cred_usage)
{
    gss_cred_id_desc *                  newcred = NULL;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           local_minor_status;
    gss_buffer_desc                     name_buffer;
    globus_result_t                     local_result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    *minor_status = (OM_uint32) GLOBUS_SUCCESS;

    *output_cred_handle = GSS_C_NO_CREDENTIAL;
    
    newcred = (gss_cred_id_desc*) calloc(1, sizeof(gss_cred_id_desc));
    
    if (newcred == NULL)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    newcred->cred_usage = cred_usage;

    local_result = globus_gsi_cred_handle_init(&newcred->cred_handle, NULL);
    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto free_cred;
    }

    name_buffer.value = NULL;
    name_buffer.length = 0;
    major_status = gss_import_name(
            &local_minor_status,
            &name_buffer,
            GSS_C_NT_ANONYMOUS,
            &newcred->globusid);
    if (major_status != GSS_S_COMPLETE)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto free_cred;
    }

    major_status = globus_i_gsi_gssapi_init_ssl_context(
        &local_minor_status,
        (gss_cred_id_t) newcred,
        GLOBUS_I_GSI_GSS_ANON_CONTEXT,
        GLOBUS_FALSE);
    if(GSS_ERROR(major_status))
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto free_cred;
    }
   
    *output_cred_handle = newcred;
    
    major_status = GSS_S_COMPLETE;
    goto exit;
    
 free_cred:

    if(newcred)
    {
        major_status =
            gss_release_cred(&local_minor_status, (gss_cred_id_t *) &newcred);
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CREDENTIAL);
    }

 exit:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

OM_uint32
globus_i_gsi_gss_cred_read_bio(
    OM_uint32 *                         minor_status,
    const gss_cred_usage_t              cred_usage,
    gss_cred_id_t *                     cred_id_handle,
    BIO *                               bp)
{
    globus_gsi_cred_handle_t            local_cred_handle;
    OM_uint32                           local_minor_status;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    globus_result_t                     local_result;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    *minor_status = GLOBUS_SUCCESS;

    local_result = globus_gsi_cred_handle_init(&local_cred_handle, NULL);

    if(local_result != GLOBUS_SUCCESS)
    {
        local_cred_handle = NULL;
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    local_result = globus_gsi_cred_read_proxy_bio(local_cred_handle, bp);

    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    major_status = globus_i_gsi_gss_create_cred(&local_minor_status,
                                                cred_usage,
                                                cred_id_handle, 
                                                &local_cred_handle,
                                                GLOBUS_FALSE);
    
    if(GSS_ERROR(major_status))
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

 exit:

    if(local_cred_handle != NULL)
    {
        globus_gsi_cred_handle_destroy(local_cred_handle);
    }
    
    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

OM_uint32
globus_i_gsi_gss_cred_read(
    OM_uint32 *                         minor_status,
    const gss_cred_usage_t              cred_usage,
    gss_cred_id_t *                     cred_handle,
    const X509_NAME *                   desired_subject) 
{
    globus_result_t                     local_result = GLOBUS_SUCCESS;
    globus_gsi_cred_handle_t            local_cred_handle;
    OM_uint32                           local_minor_status;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    int                                 rc = 0;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    local_result = globus_gsi_cred_handle_init(&local_cred_handle, NULL);
    if(local_result != GLOBUS_SUCCESS)
    {
        local_cred_handle = NULL;
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto exit;
    }


    local_result = globus_gsi_cred_read(local_cred_handle, 
                                        (X509_NAME *) desired_subject);
    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    major_status = globus_i_gsi_gss_create_cred(&local_minor_status,
                                                cred_usage,
                                                cred_handle, 
                                                &local_cred_handle,
                                                GLOBUS_FALSE);
    if(GSS_ERROR(major_status))
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto exit;
    }
    
 exit:

    if(local_cred_handle != NULL)
    {
        globus_gsi_cred_handle_destroy(local_cred_handle);
    }

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

/**
 * @brief Create Cred
 * @ingroup globus_i_gsi_gss_utils
 * @details
 * Called by acquire_cred and accept_sec_context for a delegate.
 * Setup the credential including the SSL_CTX
 *
 * @param minor_status
 * @param cred_usage
 * @param output_cred_handle_P
 * @param cred_handle
 *
 * @return
 */
OM_uint32 
globus_i_gsi_gss_create_cred(
    OM_uint32 *                         minor_status,
    const gss_cred_usage_t              cred_usage,
    gss_cred_id_t *                     output_cred_handle_P,
    globus_gsi_cred_handle_t *          cred_handle,
    globus_bool_t                       sni_context)
{
    gss_cred_id_desc **                 output_cred_handle = 
        (gss_cred_id_desc **) output_cred_handle_P;
    OM_uint32                           major_status = GSS_S_NO_CRED;
    OM_uint32                           local_minor_status;
    globus_result_t                     local_result;
    gss_cred_id_desc *                  newcred = NULL;
    globus_gsi_cert_utils_cert_type_t   cert_type;
    gss_buffer_desc                     name_buffer;
    X509 *                              identity_cert;
    STACK_OF(X509) *                    cert_chain = NULL;
    globus_bool_t                       free_identity_cert = GLOBUS_FALSE;
    
    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;
    
    *output_cred_handle = NULL;

    newcred = (gss_cred_id_desc*) calloc(1, sizeof(gss_cred_id_desc));

    if (newcred == NULL)
    {
        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    newcred->cred_usage = cred_usage;

    if(!cred_handle || !*cred_handle)
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL,
            (_GGSL("NULL credential handle passed to function: %s"), 
             __func__));
        goto error_exit;
    }

    newcred->cred_handle = *cred_handle;
    *cred_handle = NULL;

    major_status = globus_i_gsi_gssapi_init_ssl_context(
        &local_minor_status,
        (gss_cred_id_t) newcred,
        GLOBUS_I_GSI_GSS_DEFAULT_CONTEXT,
        sni_context);
    if(GSS_ERROR(major_status))
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto error_exit;
    }

    if (sni_context)
    {
        goto skip_for_sni;
    }
    local_result = globus_gsi_cred_get_cert_type(
        newcred->cred_handle, 
        &cert_type);
    
    if (local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto error_exit;
    }

    if(GLOBUS_GSI_CERT_UTILS_IS_PROXY(cert_type) && 
        !(cert_type & GLOBUS_GSI_CERT_UTILS_TYPE_INDEPENDENT_PROXY))
    {
        local_result = globus_gsi_cred_get_cert_chain(
            newcred->cred_handle, 
            &cert_chain);
        if (local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
            major_status = GSS_S_FAILURE;
            goto error_exit;
        }
        local_result = globus_gsi_cert_utils_get_identity_cert(
            cert_chain,
            &identity_cert);
        if (local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
            major_status = GSS_S_FAILURE;
            sk_X509_pop_free(cert_chain, X509_free);
            cert_chain = NULL;
            goto error_exit;
        }
    }
    else
    {
        local_result = globus_gsi_cred_get_cert(
            newcred->cred_handle, 
            &identity_cert);
        if (local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
            major_status = GSS_S_FAILURE;
            goto error_exit;
        }
        free_identity_cert = GLOBUS_TRUE;
    }
    name_buffer.value = identity_cert;
    name_buffer.length = sizeof(X509*);

    major_status = gss_import_name(
            &local_minor_status,
            &name_buffer,
            GLOBUS_GSS_C_NT_X509,
            &newcred->globusid);
    if (major_status != GSS_S_COMPLETE)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto error_exit;
    }
            
    
skip_for_sni:
    *output_cred_handle = newcred;

 error_exit:
    if (major_status != GSS_S_COMPLETE)
    {
        if(newcred)
        {
            gss_release_cred(&local_minor_status, (gss_cred_id_t *) &newcred);
        }
    }

 exit:
    if (free_identity_cert)
    {
        X509_free(identity_cert);
    }
    if (cert_chain != NULL)
    {
        sk_X509_pop_free(cert_chain, X509_free);
    }
    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;
    return major_status;
}


#if LINK_WITH_INTERNAL_OPENSSL_API
OM_uint32
globus_i_gsi_gss_SSL_write_bio(
    OM_uint32 *                         minor_status,
    gss_ctx_id_desc *                   context,
    BIO *                               bp)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    SSL *                               ssl_handle;
    unsigned char                       intbuffer[4];

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    *minor_status = (OM_uint32) GLOBUS_SUCCESS;

    ssl_handle = context->gss_ssl;

    /* DEBUG BLOCK */
    if (globus_i_gsi_gssapi_debug_level >= 2)
    {
        int index;
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "client_random=");
        for(index = 0; index < SSL3_RANDOM_SIZE; ++index)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->s3->client_random[index]));
        }
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\nserver_random =");
        for(index = 0; index < SSL3_RANDOM_SIZE; ++index)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->s3->server_random[index]));
        }
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\n");
    }
    
    BIO_write(bp, (char *) ssl_handle->s3->client_random, SSL3_RANDOM_SIZE);
    BIO_write(bp, (char *) ssl_handle->s3->server_random, SSL3_RANDOM_SIZE);
    
    ssl_handle->method->ssl3_enc->setup_key_block(ssl_handle);
    
    /* DEBUG BLOCK */
    if (globus_i_gsi_gssapi_debug_level >= 2)
    {
        int index;
        GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
            2, (globus_i_gsi_gssapi_debug_fstream,
                "tmp.key_block_length=%d\ntmp.key_block=",
                ssl_handle->s3->tmp.key_block_length));
        for (index = 0; index < ssl_handle->s3->tmp.key_block_length; ++index)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X",ssl_handle->s3->tmp.key_block[index]));
        }
        
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\nwrite_sequence=");
        for (index = 0; index < 8; ++index)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->s3->write_sequence[index]));
        }
        
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\nread_sequence =");
        for (index = 0; index < 8; ++index)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->s3->read_sequence[index]));
        }
        
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\nwrite_iv=");
        for (index = 0; index < EVP_MAX_IV_LENGTH; ++index)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->enc_write_ctx->iv[index]));
        }
        
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\nread_iv=");
        for (index = 0; index < EVP_MAX_IV_LENGTH; index++)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->enc_read_ctx->iv[index]));
        }
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\n");
    }
    
    L2N(ssl_handle->s3->tmp.key_block_length, intbuffer);

    BIO_write(bp, (char *) intbuffer, 4);

    GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
        3, (globus_i_gsi_gssapi_debug_fstream,
            "exporting security context: BIO pending=%d\n",
            BIO_pending(bp)));

    BIO_write(bp, (char *) ssl_handle->s3->tmp.key_block,
              ssl_handle->s3->tmp.key_block_length);
    BIO_write(bp, (char *) ssl_handle->s3->write_sequence, 8);
    BIO_write(bp, (char *) ssl_handle->s3->read_sequence, 8);
    BIO_write(bp, (char *) ssl_handle->enc_write_ctx->iv, EVP_MAX_IV_LENGTH);
    BIO_write(bp, (char *) ssl_handle->enc_read_ctx->iv, EVP_MAX_IV_LENGTH);
    
    ssl3_cleanup_key_block(ssl_handle);

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

OM_uint32
globus_i_gsi_gss_SSL_read_bio(
    OM_uint32 *                         minor_status,
    gss_ctx_id_desc *                   context,
    BIO *                               bp)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    SSL *                               ssl_handle;
    unsigned char                       int_buffer[4];
    int                                 length;
    int                                 len = 0;
    int                                 rc;
    int                                 ssl_result;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    *minor_status = (OM_uint32) GLOBUS_SUCCESS;

    ssl_handle = context->gss_ssl;

    if (BIO_pending(bp) < (2 * SSL3_RANDOM_SIZE))
    {
        major_status = GSS_S_NO_CONTEXT;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_IMPEXP_BIO_SSL,
            (_GGSL("Couldn't read from bio for importing SSL handle")));
        goto exit;
    }

    BIO_read(bp, (char*) ssl_handle->s3->client_random, SSL3_RANDOM_SIZE);
    BIO_read(bp, (char*) ssl_handle->s3->server_random, SSL3_RANDOM_SIZE);

    /* DEBUG BLOCK */
    if (globus_i_gsi_gssapi_debug_level >= 2)
    {
        int index;
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "client_random=");
        for (index =0 ; index < SSL3_RANDOM_SIZE; index++)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->s3->client_random[index]));
        }

        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\nserver_random ="); 
        for (index = 0; index < SSL3_RANDOM_SIZE; index++)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->s3->server_random[index]));
        }
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\n");
    }

    ssl_handle->shutdown = 0;

    ssl_handle->s3->tmp.new_cipher = ssl_handle->session->cipher;
        
    /* read the tmp.key_block */
    if (BIO_pending(bp) < 4)
    {
        major_status = GSS_S_NO_CONTEXT;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_IMPEXP_BAD_LEN,
            (_GGSL("Invalid data on BIO, should be 4 bytes available")));
        goto exit;
    }

    /* get length */
    BIO_read(bp, (char *) int_buffer, 4); 
    N2L(int_buffer, length);

    if (length > 0)
    {
        if (BIO_pending(bp) < length)
        {
            major_status = GSS_S_NO_CONTEXT;
            GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_IMPEXP_BAD_LEN,
                (_GGSL("Invalid BIO - not enough data to read an int")));
            goto exit;
        }

        ssl_handle->s3->tmp.key_block = (unsigned char *) OPENSSL_malloc(length);
        if (ssl_handle->s3->tmp.key_block == NULL)
        {
            GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }
    else
    {
        ssl_handle->s3->tmp.key_block = NULL;
    }
    ssl_handle->s3->tmp.key_block_length = length;

    GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
        3, (globus_i_gsi_gssapi_debug_fstream,
            "reading in context: BIO pending = %d\n",
            BIO_pending(bp)));

    while(len < length)
    {
        rc = BIO_read(bp,  
                      (char *) ssl_handle->s3->tmp.key_block + len,
                      ssl_handle->s3->tmp.key_block_length - len);
        if(rc > 0)
        {
            len += rc;
        }
        else
        {
            GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_READ_BIO,
                (_GGSL("Couldn't read expected bytes of: %d from BIO"),
                 length));;
        }
    }

    /* DEBUG BLOCK */
    if (globus_i_gsi_gssapi_debug_level >= 2)
    {
        int index;
        GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
            2, (globus_i_gsi_gssapi_debug_fstream,
                "tmp.key_block_length=%d\ntmp.key_block=",
                ssl_handle->s3->tmp.key_block_length));
        for(index = 0; index < ssl_handle->s3->tmp.key_block_length; index++)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->s3->tmp.key_block[index]));
        }
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\n");
    }

    ssl_result = ssl_cipher_get_evp(
        ssl_handle->session,
        &ssl_handle->s3->tmp.new_sym_enc,
        &ssl_handle->s3->tmp.new_hash,
#if (OPENSSL_VERSION_NUMBER >= 0x10000000L)
        &ssl_handle->s3->tmp.new_mac_pkey_type,
        &ssl_handle->s3->tmp.new_mac_secret_size,
#endif
        (SSL_COMP **) &ssl_handle->s3->tmp.new_compression);
    if (!ssl_result)
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_IMPEXP_BIO_SSL,
            (_GGSL("Couldn't set the compression type in the SSL handle")));
        major_status = GSS_S_FAILURE;
        goto free_key_block;
    }
    
    ssl_result = ssl_handle->method->ssl3_enc->change_cipher_state(
        ssl_handle,
        (!ssl_handle->server)?SSL3_CHANGE_CIPHER_CLIENT_WRITE:SSL3_CHANGE_CIPHER_SERVER_WRITE);
    if (!ssl_result)
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_IMPEXP_BIO_SSL,
            (_GGSL("Attempt to change cipher state of the SSL handle failed")));
        major_status = GSS_S_FAILURE;
        goto free_key_block;
    }

    ssl_result = ssl_cipher_get_evp(
        ssl_handle->session,
        &ssl_handle->s3->tmp.new_sym_enc,
        &ssl_handle->s3->tmp.new_hash,
#if (OPENSSL_VERSION_NUMBER >= 0x10000000L)
        &ssl_handle->s3->tmp.new_mac_pkey_type,
        &ssl_handle->s3->tmp.new_mac_secret_size,
#endif
        (SSL_COMP **) &ssl_handle->s3->tmp.new_compression);
    if (!ssl_result)
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_IMPEXP_BIO_SSL,
            (_GGSL("Couldn't set the compression type in the SSL handle")));
        major_status = GSS_S_FAILURE;
        goto free_key_block;
    }

    ssl_result = ssl_handle->method->ssl3_enc->change_cipher_state(
        ssl_handle,
        (!ssl_handle->server)?SSL3_CHANGE_CIPHER_CLIENT_READ:SSL3_CHANGE_CIPHER_SERVER_READ); 
    
    if (!ssl_result)
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_IMPEXP_BIO_SSL,
            (_GGSL("Attempt to change cipher state of the SSL handle failed")));
        major_status = GSS_S_FAILURE;
        goto free_key_block;
    }

    ssl_handle->hit = 1;

    ssl_handle->state = SSL_ST_OK;

    /* will free the one we read */
    ssl3_cleanup_key_block(ssl_handle); 
    
    length = BIO_pending(bp);
    if (length != 8 + 8 + EVP_MAX_IV_LENGTH + EVP_MAX_IV_LENGTH)
    {
        major_status = GSS_S_NO_CONTEXT;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_IMPEXP_BIO_SSL,
            (_GGSL("Error reading SSL data from BIO")));
        goto free_key_block;
    }
    
    BIO_read(bp, (char*) ssl_handle->s3->write_sequence, 8);
    BIO_read(bp, (char*) ssl_handle->s3->read_sequence,  8);
    BIO_read(bp, (char*) ssl_handle->enc_write_ctx->iv,  EVP_MAX_IV_LENGTH);
    BIO_read(bp, (char*) ssl_handle->enc_read_ctx->iv,   EVP_MAX_IV_LENGTH);
    
    /* DEBUG BLOCK */
    if (globus_i_gsi_gssapi_debug_level >= 2)
    {
        int index;
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "write_sequence=");
        for (index = 0; index < 8; index++)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->s3->write_sequence[index]));
        }

        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\nread_sequence=");
        for (index = 0; index < 8; index++)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->s3->read_sequence[index]));
        }
        
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\nwrite_iv=");
        for (index = 0; index < EVP_MAX_IV_LENGTH; index++)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->enc_write_ctx->iv[index]));
        }
        
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\nread_iv=");
        for (index = 0; index < EVP_MAX_IV_LENGTH; index++)
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                2, (globus_i_gsi_gssapi_debug_fstream,
                    "%02X", ssl_handle->enc_read_ctx->iv[index]));
        }
        GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(2, "\n");
    }

    goto exit;

 free_key_block:

    if(ssl_handle->s3->tmp.key_block)
    {
        OPENSSL_free(ssl_handle->s3->tmp.key_block);
    }

 exit:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}
#endif /* LINK_WITH_INTERNAL_OPENSSL_API */

/**
 * @brief Init SSL Context
 * @ingroup globus_i_gsi_gssapi
 * @details
 * Initialize the SSL Context for use in the SSL authentication mechanism.
 * The ssl context held by the cred handle is used to generate SSL objects
 * for the SSL handshake.  Initializing the SSL context consists of
 * setting the method to be used (TLS), setting the callback to perform
 * certificate verification, and finding the appropriate issuing CA's of
 * the certs used for authentication.
 *
 * @param minor_status
 *        The mech specific status code.  This is GLOBUS_SUCCESS if the
 *        call was successful, otherwise it is set to a globus error
 *        object identifier
 * @param cred_handle
 *        The credential handle containing the SSL_CTX to be initialized
 * @param anon_ctx
 *        Specify the SSL context as anonymous (1) or not (0).  An anonymous
 *        SSL_CTX does not have the cert or key set. 
 *
 * @return
 *        GSS_S_COMPLETE if initiating the SSL_CTX was successful
 *        GSS_S_FAILURE if an error occurred
 */
OM_uint32
globus_i_gsi_gssapi_init_ssl_context(
    OM_uint32 *                         minor_status,
    gss_cred_id_t                       credential,
    globus_i_gsi_gss_context_type_t     anon_ctx,
    globus_bool_t                       sni_context)
{
    X509 *                              client_cert = NULL;
    EVP_PKEY *                          client_key = NULL;
    STACK_OF(X509) *                    client_cert_chain = NULL;
#if (OPENSSL_VERSION_NUMBER >= 0x00908000L) && !defined(OPENSSL_NO_COMP)
    STACK_OF(SSL_COMP) *                comp_methods;
#endif
    globus_result_t                     local_result;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    gss_cred_id_desc *                  cred_handle;
    char *                              ca_cert_dir = NULL;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    cred_handle = (gss_cred_id_desc *) credential;
    
    if(cred_handle == NULL)
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CREDENTIAL,
            (_GGSL("Null credential handle passed to function: %s"),
             __func__));
        goto exit;
    }

#if (OPENSSL_VERSION_NUMBER >= 0x00908000L) && !defined(OPENSSL_NO_COMP)
    /*
     * post-0.9.8 versions of OpenSSL include data compression. unfortunately,
     * there isn't a way to export a session's compression info, so
     * re-importing a context fails
     */
    comp_methods = SSL_COMP_get_compression_methods();
    if (comp_methods != 0)
    {
        sk_SSL_COMP_zero(comp_methods);
    }
#endif
   /* openssl 1.1.0 adds a new method of setting this, deprecates old */ 
    #if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    cred_handle->ssl_context = SSL_CTX_new(TLS_method());
    #else
    cred_handle->ssl_context = SSL_CTX_new(SSLv23_method());
    #endif
    if(cred_handle->ssl_context == NULL)
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
            (_GGSL("Can't initialize the SSL_CTX")));
        goto exit;
    }

    /* "On July 1, 2015, we will update our security packages to disable SSLv3
     * and require TLS for all secure communication." */
    GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(
        2, "Disabling SSLv2 and SSLv3.\n");
    /* Minimum version allowed is TLS 1.0 */
    if (globus_i_gsi_gssapi_min_tls_protocol == 0)
        globus_i_gsi_gssapi_min_tls_protocol = TLS1_VERSION;
    if (globus_i_gsi_gssapi_max_tls_protocol == 0)
        globus_i_gsi_gssapi_max_tls_protocol = TLS_MAX_VERSION;
    GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
        3, (globus_i_gsi_gssapi_debug_fstream,
        "MIN_TLS_PROTOCOL: %x\n", globus_i_gsi_gssapi_min_tls_protocol));
    GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
        3, (globus_i_gsi_gssapi_debug_fstream,
        "MAX_TLS_PROTOCOL: %x\n", globus_i_gsi_gssapi_max_tls_protocol));

    /* openssl 1.1.0 adds a new method of setting this, deprecates old */
    #if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    {
        SSL_CTX_set_min_proto_version(cred_handle->ssl_context,
                               globus_i_gsi_gssapi_min_tls_protocol);
        SSL_CTX_set_max_proto_version(cred_handle->ssl_context,
                               globus_i_gsi_gssapi_max_tls_protocol);
    }
    #else
    {
        /* Minimum version allowed is TLS 1.0 */
        SSL_CTX_set_options(cred_handle->ssl_context,SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);
        if (TLS1_VERSION < globus_i_gsi_gssapi_min_tls_protocol ||
            TLS1_VERSION > globus_i_gsi_gssapi_max_tls_protocol)
            SSL_CTX_set_options(cred_handle->ssl_context,SSL_OP_NO_TLSv1);
        if (TLS1_1_VERSION < globus_i_gsi_gssapi_min_tls_protocol ||
            TLS1_1_VERSION > globus_i_gsi_gssapi_max_tls_protocol)
            SSL_CTX_set_options(cred_handle->ssl_context,SSL_OP_NO_TLSv1_1);
        if (TLS1_2_VERSION < globus_i_gsi_gssapi_min_tls_protocol ||
            TLS1_2_VERSION > globus_i_gsi_gssapi_max_tls_protocol)
            SSL_CTX_set_options(cred_handle->ssl_context,SSL_OP_NO_TLSv1_2);
    }
    #endif
            
    SSL_CTX_set_cert_verify_callback(cred_handle->ssl_context,
                                     globus_gsi_callback_X509_verify_cert,
                                     NULL);

    SSL_CTX_sess_set_cache_size(cred_handle->ssl_context, 5);

    local_result = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&ca_cert_dir);
    
    if(local_result != GLOBUS_SUCCESS)
    {
        ca_cert_dir = NULL;
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        goto exit;
    }

    if(!SSL_CTX_load_verify_locations(cred_handle->ssl_context,
                                      NULL,
                                      ca_cert_dir))
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
            ("\n       x509_cert_dir=", (ca_cert_dir) ? ca_cert_dir : _GGSL("NONE")));
        goto exit;
    }

    /* Set the verify callback to test our proxy 
     * policies. 
     */
    SSL_CTX_set_verify(cred_handle->ssl_context, SSL_VERIFY_PEER,
                       globus_gsi_callback_handshake_callback);

    SSL_CTX_set_verify_depth(cred_handle->ssl_context,
                             GLOBUS_GSI_CALLBACK_VERIFY_DEPTH);

    /*
     * for now we will accept any purpose, as Globus does
     * not have any restrictions such as this is an SSL client
     * or SSL server. Globus certificates are not required
     * to have these fields set today.
     */
    SSL_CTX_set_purpose(cred_handle->ssl_context, X509_PURPOSE_ANY);

    /*
     * post OpenSSL-0.9.8, existence of this call creates problem when
     * the server (for eg. GridFTP server) is run as a user and thus the
     * cert presented is proxy cert. As the OpenSSL code does not 
     * recognize Globus legacy proxies, we need to explicitly set the
     * proxy flag in the cert and we do it only when our callback is
     * called by OpenSSL with the critical extension error, so this call
     * is removed for post OpenSSL-0.9.8.
     */
     
    #if (OPENSSL_VERSION_NUMBER < 0x0090707fL)
    X509_STORE_set_flags(SSL_CTX_get_cert_store(cred_handle->ssl_context),
                         X509_V_FLAG_IGNORE_CRITICAL);
    #endif
    
    if(anon_ctx != GLOBUS_I_GSI_GSS_ANON_CONTEXT)
    {
        if (!sni_context)
        {
            local_result = globus_gsi_cred_get_cert(cred_handle->cred_handle,
                                                    &client_cert);
            if(local_result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_result,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
                major_status = GSS_S_FAILURE;
                goto exit;
            }

            if(!client_cert)
            {
                GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL,
                    (_GGSL("The GSI credential's certificate has not been set.")));
                major_status = GSS_S_FAILURE;
                goto exit;
            }

            if(!SSL_CTX_use_certificate(cred_handle->ssl_context, 
                                        client_cert))
            {
                GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
                    (_GGSL("Couldn't set the certificate to "
                     "be used for the SSL context")));
                major_status = GSS_S_FAILURE;
                goto exit;
            }

            local_result = globus_gsi_cred_get_key(cred_handle->cred_handle,
                                                   &client_key);
            if(local_result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_result,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
                major_status = GSS_S_FAILURE;
                goto exit;
            }

            if(!client_key)
            {
                GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL,
                    (_GGSL("The GSI credential's private key has not been set.")));
                major_status = GSS_S_FAILURE;
                goto exit;
            }

            if(!SSL_CTX_use_PrivateKey(cred_handle->ssl_context, client_key))
            {
                GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
                    (_GGSL("Couldn't set the private key to "
                     "be used for the SSL context")));
                major_status = GSS_S_FAILURE;
                goto exit;
            }
                
            local_result = globus_gsi_cred_get_cert_chain(cred_handle->cred_handle,
                                                          &client_cert_chain);
            if(local_result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_result,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
                major_status = GSS_S_FAILURE;
                goto exit;
            }

            if(client_cert_chain)
            {
                int                         index;
                X509 *                      tmp_cert = NULL;
                for(index = 0; index < sk_X509_num(client_cert_chain); ++index)
                {
                    tmp_cert = X509_dup(sk_X509_value(client_cert_chain, index));
                    if(!X509_STORE_add_cert(
                           SSL_CTX_get_cert_store(cred_handle->ssl_context),
                           tmp_cert))
                    {
                        /* need to free to reduce ref count */
                        X509_free(tmp_cert);
                        if ((ERR_GET_REASON(ERR_peek_error()) ==
                             X509_R_CERT_ALREADY_IN_HASH_TABLE))
                        {
                            ERR_clear_error();
                            break;
                        }
                        else
                        {
                            GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                                minor_status,
                                GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
                                (_GGSL("Couldn't add certificate to the SSL context's "
                                 "certificate store.")));
                            major_status = GSS_S_FAILURE;
                            goto exit;
                        }
                    }
                    /* need to free to reduce ref count */
                    X509_free(tmp_cert);
                }
            }
        }
    }
    else
    {
        char *                          extra_pem;

        extra_pem = getenv("GLOBUS_GFS_EXTRA_CA_CERTS");
        if(extra_pem)
        {
            X509 *                      extra_x509 = NULL;
            BIO *                       B_mem;

            B_mem = BIO_new(BIO_s_mem());
            BIO_puts(B_mem, extra_pem);
            
            while((extra_x509 = PEM_read_bio_X509(B_mem, NULL, 0, NULL)) != NULL)
            {
                if(!X509_STORE_add_cert(
                    SSL_CTX_get_cert_store(cred_handle->ssl_context),
                    extra_x509))
                {
                    if ((ERR_GET_REASON(ERR_peek_error()) ==
                         X509_R_CERT_ALREADY_IN_HASH_TABLE))
                    {
                        ERR_clear_error();
                    }
                    else
                    {
                        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                            minor_status,
                            GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
                            (_GGSL("Couldn't add certificate to the SSL context's "
                             "certificate store.")));
                        major_status = GSS_S_FAILURE;
                        X509_free(extra_x509);
                        BIO_free(B_mem);
                        goto exit;
                    }
                }
                /* need to free to reduce ref count */
                X509_free(extra_x509);
            }
            BIO_free(B_mem);
        }
    }


 exit:

    if(client_cert)
    {
        X509_free(client_cert);
    }
    
    if(client_key)
    {
        EVP_PKEY_free(client_key);
    }

    if(client_cert_chain)
    {
        sk_X509_pop_free(client_cert_chain, X509_free);
    }

    if(ca_cert_dir)
    {
        free(ca_cert_dir);
    }

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

OM_uint32
globus_i_gsi_gss_get_context_goodtill(
    OM_uint32 *                         minor_status,
    gss_ctx_id_t                        context,
    time_t *                            goodtill)
{
    time_t                              peer_cred_goodtill;
    time_t                              local_cred_goodtill;
    globus_result_t                     local_result;
    OM_uint32                           major_status = GSS_S_COMPLETE;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    *goodtill = 0;
    if (((gss_ctx_id_desc *)context)->cred_handle)
    {
        local_result = globus_gsi_cred_get_goodtill(
            ((gss_ctx_id_desc *)context)->cred_handle->cred_handle,
            &local_cred_goodtill);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
        if (local_cred_goodtill > *goodtill)
        {
            *goodtill = local_cred_goodtill;
        }
    }

    if (((gss_ctx_id_desc *)context)->peer_cred_handle)
    {
        local_result = globus_gsi_cred_get_goodtill(
            context->peer_cred_handle->cred_handle,
            &peer_cred_goodtill);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
        if (peer_cred_goodtill > *goodtill)
        {
            *goodtill = peer_cred_goodtill;
        }
    }

 exit:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}

/**
 * @brief Verify Extensions Callback
 * @ingroup globus_i_gsi_gss_utils
 * @details
 * Verify Extensions Callback 
 *
 * @param callback_data
 * @param extension
 *
 * @return
 */
int globus_i_gsi_gss_verify_extensions_callback(
    globus_gsi_callback_data_t          callback_data,
    X509_EXTENSION *                    extension)
{
    gss_OID_set                         extension_oids;
    ASN1_OBJECT *                       extension_obj;
    int                                 index;
    int                                 return_val = 0;
    globus_result_t                     local_result;
    gss_OID_desc                        oid;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;
    
    local_result = globus_gsi_callback_get_extension_oids(
        callback_data,
        (void **) (void *) &extension_oids);

    if(local_result != GLOBUS_SUCCESS)
    {
        return_val = 0;
        goto exit;
    }

    if(extension_oids == GSS_C_NO_OID_SET)
    {
        return_val = 0;
        goto exit;
    }
    
    extension_obj = X509_EXTENSION_get_object(extension);

    for(index = 0; index < extension_oids->count; index++)
    {
        ASN1_OBJECT                    *oid_object;
        const unsigned char            *elements;

        oid = extension_oids->elements[index];
        elements = oid.elements;
        oid_object = d2i_ASN1_OBJECT(NULL, &elements, oid.length);

        if(OBJ_obj2nid(extension_obj) == OBJ_obj2nid(oid_object))
        {
            return_val = 1;
            ASN1_OBJECT_free(oid_object);
            goto exit;
        }
        ASN1_OBJECT_free(oid_object);
    }

    return_val = 0;

 exit:

    GLOBUS_I_GSI_GSSAPI_INTERNAL_DEBUG_EXIT;
    return return_val;
}

OM_uint32
globus_i_gsi_gssapi_get_hostname(
    OM_uint32 *                         minor_status,
    gss_name_desc *                     name)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    int                                 common_name_NID;
    int                                 index;
    unsigned int                        length;
    unsigned char *                     data;
    unsigned char *                     p;
    X509_NAME_ENTRY *                   name_entry = NULL;

    name->service_name = name->host_name = NULL;
    *minor_status = GLOBUS_SUCCESS;

    common_name_NID = OBJ_txt2nid("CN");
    for (index = 0; index < X509_NAME_entry_count(name->x509n); index++)
    {
        name_entry = X509_NAME_get_entry(name->x509n, index);
        if (OBJ_obj2nid(X509_NAME_ENTRY_get_object(name_entry)) == common_name_NID)
        {
            ASN1_STRING *s = X509_NAME_ENTRY_get_data(name_entry);
            length = ASN1_STRING_length(s);
            data = ASN1_STRING_data(s);

            p = memchr(data, '/', length);

            if (p)
            {
                name->service_name = malloc(p-data+1);
                if (name->service_name == NULL)
                {
                    GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                    major_status = GSS_S_FAILURE;

                    goto out;
                }
                strncpy(name->service_name, (char *) data, p-data);
                name->service_name[p-data] = 0;

                name->host_name = malloc(length - (p-data));
                if (name->host_name == NULL)
                {
                    GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                    major_status = GSS_S_FAILURE;

                    goto free_service_name_out;
                }
                strncpy(name->host_name, (char *) p+1, length - (p+1-data));
                name->host_name[length - (p+1-data)] = 0;
            }
            else
            {
                if (gss_i_name_compatibility_mode ==
                    GSS_I_COMPATIBILITY_STRICT_RFC2818)
                {
                    name->service_name = globus_libc_strdup("host");
                    if (name->service_name == NULL)
                    {
                        GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                        major_status = GSS_S_FAILURE;

                        goto out;
                    }
                }

                name->host_name = malloc(length + 1);
                if (name->host_name == NULL)
                {
                    GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                    major_status = GSS_S_FAILURE;

                    goto free_service_name_out;
                }

                strncpy(name->host_name, (char *) data, length);
                name->host_name[length] = 0;
            }
            break;
        }
    }

    if (name->host_name == NULL)
    {
free_service_name_out:
        if (name->service_name)
        {
            free(name->service_name);
            name->service_name = NULL;
        }
    }
    
out:
    return major_status;
}

static
int
globus_l_gsi_gss_servername_callback(
    SSL                                *s,
    int                                *ad,
    void                               *callback_arg)
{
    gss_ctx_id_t                        context = callback_arg;
    gss_cred_id_t                       credential = NULL;
    const char                         *servername = NULL;
    gss_name_t                          imported_name = GSS_C_NO_NAME;
    globus_result_t                     local_result = GLOBUS_SUCCESS;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           local_minor_status = GLOBUS_SUCCESS;
    X509                               *client_cert = NULL;
    EVP_PKEY                           *client_key = NULL;
    STACK_OF(X509)                     *client_cert_chain = NULL;
    globus_gsi_cert_utils_cert_type_t   cert_type;
    
    servername = SSL_get_servername(s, TLSEXT_NAMETYPE_host_name);
    if(GLOBUS_I_GSI_GSSAPI_DEBUG(3))
    {
        BIO *                       debug_bio;
        fprintf(globus_i_gsi_gssapi_debug_fstream,
                "SNI callback: servername = %s\n",
                (servername != NULL) ? servername : "NULL");
    }

    if (servername == NULL)
    {
        X509 *                          default_cert = NULL;

        if (globus_gsi_cred_get_cert(
                context->cred_handle->cred_handle,
                &default_cert) == GLOBUS_SUCCESS)
        {
            /* No SNI, but we have a default credential */
            X509_free(default_cert);
            return SSL_TLSEXT_ERR_OK;
        }
        else
        {
            /* No SNI, pick the first one we loaded */
            credential = context->sni_credentials[0];
            goto use_any;
        }
    }
    context->sni_servername = strdup(servername);

    major_status = gss_import_name(
            &local_minor_status,
            &(gss_buffer_desc)
            {
                .value = (void *) servername,
                .length = strlen(servername),
            },
            GLOBUS_GSS_C_NT_HOST_IP,
            &imported_name);

    if (major_status != GSS_S_COMPLETE)
    {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    for (size_t i = 0 ; i < context->sni_credentials_count; i++)
    {
        int                             name_equal = 0;

        major_status = gss_compare_name(
                &local_minor_status,
                imported_name,
                context->sni_credentials[i]->globusid,
                &name_equal);

        if (major_status == GSS_S_COMPLETE && name_equal)
        {
            credential = context->sni_credentials[i];
            break;
        }
    }
    gss_release_name(
            &local_minor_status,
            &imported_name);
    imported_name = GSS_C_NO_NAME;

    if (major_status != GSS_S_COMPLETE)
    {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    if (credential == NULL)
    {
        return SSL_TLSEXT_ERR_NOACK;
    }

use_any:
    /* Replace the credential used by the context with the newly acquired
     * one
     */
    if (context->cred_obtained)
    {
        major_status = gss_release_cred(
            &local_minor_status,
            &context->cred_handle);

        if (GSS_ERROR(major_status))
        {
            goto exit;
        }
    }

    context->cred_handle = credential;
    context->cred_obtained = GLOBUS_FALSE;
    credential = NULL;

    local_result = globus_gsi_cred_get_cert(
            context->cred_handle->cred_handle, &client_cert);
    if (local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            &local_minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    if (client_cert == NULL)
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            &local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL,
            (_GGSL("The GSI credential's certificate has not been set.")));
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    if (!SSL_CTX_use_certificate(
                context->cred_handle->ssl_context, 
                client_cert))
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            &local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
            (_GGSL("Couldn't set the certificate to "
             "be used for the SSL context")));
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    local_result = globus_gsi_cred_get_key(context->cred_handle->cred_handle,
                                           &client_key);
    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            &local_minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    if (client_key == NULL)
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            &local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL,
            (_GGSL("The GSI credential's private key has not been set.")));
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    if(!SSL_CTX_use_PrivateKey(context->cred_handle->ssl_context, client_key))
    {
        GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
            &local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
            (_GGSL("Couldn't set the private key to "
             "be used for the SSL context")));
        major_status = GSS_S_FAILURE;
        goto exit;
    }
        
    local_result = globus_gsi_cred_get_cert_chain(
            context->cred_handle->cred_handle,
            &client_cert_chain);
    if(local_result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            &local_minor_status, local_result,
            GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    if(client_cert_chain)
    {
        int                         index;
        X509 *                      tmp_cert = NULL;
        for(index = 0; index < sk_X509_num(client_cert_chain); ++index)
        {
            tmp_cert = X509_dup(sk_X509_value(client_cert_chain, index));
            if(!X509_STORE_add_cert(
                   SSL_CTX_get_cert_store(context->cred_handle->ssl_context),
                   tmp_cert))
            {
                /* need to free to reduce ref count */
                X509_free(tmp_cert);
                if ((ERR_GET_REASON(ERR_peek_error()) ==
                     X509_R_CERT_ALREADY_IN_HASH_TABLE))
                {
                    ERR_clear_error();
                    break;
                }
                else
                {
                    GLOBUS_GSI_GSSAPI_OPENSSL_ERROR_RESULT(
                        &local_minor_status,
                        GLOBUS_GSI_GSSAPI_ERROR_WITH_OPENSSL,
                        (_GGSL("Couldn't add certificate to the SSL context's "
                         "certificate store.")));
                    major_status = GSS_S_FAILURE;
                    goto exit;
                }
            }
            /* need to free to reduce ref count */
            X509_free(tmp_cert);
        }
    }
    SSL_set_SSL_CTX(context->gss_ssl, context->cred_handle->ssl_context);
exit:
    X509_free(client_cert);
    EVP_PKEY_free(client_key);
    sk_X509_pop_free(client_cert_chain, X509_free);

    if (major_status != GSS_S_COMPLETE)
    {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    else
    {
        return SSL_TLSEXT_ERR_OK;
    }
}

#if OPENSSL_VERSION_NUMBER >= 0x10002000L
int
globus_l_gsi_gss_alpn_select_callback(
    SSL                                *ssl,
    const unsigned char               **out,
    unsigned char                      *outlen,
    const unsigned char                *in,
    unsigned int                        inlen,
    void                               *arg)
{
    int                                 rc = 0;
    gss_ctx_id_desc*                    context = arg;
    unsigned char                      *tmpout = NULL;

    rc = SSL_select_next_proto(
        &tmpout,
        outlen,
        context->alpn,
        context->alpn_length,
        in,
        inlen);

    if (rc == OPENSSL_NPN_NEGOTIATED)
    {
        *out = tmpout;
        return SSL_TLSEXT_ERR_OK;
    }
    else
    {
        return SSL_TLSEXT_ERR_NOACK;
    }
}
#endif

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
