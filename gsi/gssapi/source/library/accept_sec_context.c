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
 * @file accept_sec_context.c
 * @brief Globus GSI GSS-API gss_accept_sec_context
 * @author Sam Meder, Sam Lang
 */
#endif

#include "gssapi_openssl.h"
#include "globus_i_gsi_gss_utils.h"
#include <string.h>

/**
 * @brief GSS Accept Security Context
 * @ingroup globus_gsi_gssapi
 * @details
 * @param minor_status
 * @param context_handle_P
 * @param acceptor_cred_handle
 * @param input_token
 * @param input_chan_bindings
 * @param src_name_P
 * @param mech_type
 * @param output_token
 * @param ret_flags
 *        Also used as req_flags for other functions
 * @param time_rec
 * @param delegated_cred_handle_P
 *
 * @return
 */
OM_uint32
GSS_CALLCONV gss_accept_sec_context(
    OM_uint32 *                         minor_status,
    gss_ctx_id_t *                      context_handle_P,
    const gss_cred_id_t                 acceptor_cred_handle,
    const gss_buffer_t                  input_token,
    const gss_channel_bindings_t        input_chan_bindings,
    gss_name_t *                        src_name_P,
    gss_OID *                           mech_type,
    gss_buffer_t                        output_token,
    OM_uint32 *                         ret_flags,
    OM_uint32 *                         time_rec,
    gss_cred_id_t *                     delegated_cred_handle_P) 
{
    gss_ctx_id_desc *                   context = NULL;
    globus_gsi_cred_handle_t            delegated_cred = NULL;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           local_minor_status;
    OM_uint32                           local_major_status;
    globus_result_t                     local_result;
    OM_uint32                           nreq_flags = 0;
    gss_OID                             actual_mech = GSS_C_NO_OID;
    char                                dbuf[1];
    STACK_OF(X509) *                    cert_chain = NULL;
    globus_gsi_cert_utils_cert_type_t   cert_type;
    int                                 readlen;
    X509 *                              peer_cert = NULL;
    const EVP_MD *                      peer_digest;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    *minor_status = (OM_uint32) GLOBUS_SUCCESS;
    output_token->length = 0;

    if(!context_handle_P)
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Parameter context_handle_P passed to function: %s is NULL"),
             __func__));
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    context = *context_handle_P;
    
    /* module activation if not already done by calling
     * globus_module_activate
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
    
    if (context == (gss_ctx_id_t) GSS_C_NO_CONTEXT ||
        !(context->ctx_flags & GSS_I_CTX_INITIALIZED))
    {
        /* accept does not have req_flags, so we will use ret_flags */
        if (ret_flags)
        {
            nreq_flags = *ret_flags;
        }

        /* Use mech from cred if available */
        if (acceptor_cred_handle != GSS_C_NO_CREDENTIAL)
        {
            actual_mech = acceptor_cred_handle->mech;
        }

        GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
            2, (globus_i_gsi_gssapi_debug_fstream, 
                "accept_sec_context:OID from cred:%s\n",
                (actual_mech == GSS_C_NO_OID)? "GSS_C_NO_OID":
                ((g_OID_equal(actual_mech, (gss_OID) gss_mech_globus_gssapi_openssl))?
                  "OLD MECH OID":
                 ((g_OID_equal(actual_mech, (gss_OID) gss_mech_globus_gssapi_openssl_micv2))?
                  "MICV2 MECH OID": "UNKNOWN MECH OID"))));

        major_status = globus_i_gsi_gss_create_and_fill_context(
            &local_minor_status,
            &context,
            actual_mech,
            GSS_C_NO_NAME,
            acceptor_cred_handle,
            GSS_C_ACCEPT,
            nreq_flags);

        if (GSS_ERROR(major_status))
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status,
                local_minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CONTEXT);
            goto exit;
        }

        *context_handle_P = context;

        if (mech_type != NULL)
        {
            *mech_type = actual_mech;
        }

        if (ret_flags != NULL)
        {
            /* accept does not have req_flags, we need one */
            *ret_flags = 0 ;
        }
        
        if (delegated_cred_handle_P != NULL)
        {
            *delegated_cred_handle_P = GSS_C_NO_CREDENTIAL;
        }
    } /* end of first time */

    /* put token data onto the SSL bio so it can be read */
    major_status = globus_i_gsi_gss_put_token(&local_minor_status,
                                              context, NULL, input_token);
    if (GSS_ERROR(major_status))
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_TOKEN_FAIL);
        goto exit;
    }

    switch (context->gss_state)
    {
        case(GSS_CON_ST_HANDSHAKE):
            
            major_status = globus_i_gsi_gss_handshake(&local_minor_status, 
                                                      context);
            
            if (major_status == GSS_S_CONTINUE_NEEDED)
            {
                break;
            }   
            
            if (GSS_ERROR(major_status))
            {
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_HANDSHAKE);
                context->gss_state = GSS_CON_ST_DONE;
                break; 
            }

            major_status = globus_i_gsi_gss_retrieve_peer(&local_minor_status,
                                                          context,
                                                          GSS_C_ACCEPT);
            if (GSS_ERROR(major_status))
            {
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME);
                context->gss_state = GSS_CON_ST_DONE;
                break;
            }

            if(g_OID_equal(context->peer_cred_handle->globusid->name_oid,
                           GSS_C_NT_ANONYMOUS))
            {
                context->ret_flags |= GSS_C_ANON_FLAG;
            }
            
            if (src_name_P != NULL)
            {
                major_status = globus_i_gsi_gss_copy_name_to_name(
                    &local_minor_status,
                    (gss_name_desc **)src_name_P, 
                    context->peer_cred_handle->globusid);

                if(GSS_ERROR(major_status))
                {
                    GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                        minor_status, local_minor_status,
                        GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME);
                    context->gss_state = GSS_CON_ST_DONE;
                    break;
                }

                if(context->ret_flags & GSS_C_ANON_FLAG)
                {
                    ((gss_name_desc *)(*src_name_P))->name_oid
                        = GSS_C_NT_ANONYMOUS;
                }
            }
                        
            local_result = globus_gsi_callback_get_cert_type(
                context->callback_data,
                &cert_type);

            if(local_result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_result,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_CALLBACK_DATA);
                context->gss_state = GSS_CON_ST_DONE;
                major_status = GSS_S_FAILURE;
                break;
            }

            if(GLOBUS_GSI_CERT_UTILS_IS_LIMITED_PROXY(cert_type))
            {
                context->ret_flags |= GSS_C_GLOBUS_RECEIVED_LIMITED_PROXY_FLAG;
                /*
                 * Are we willing to accept authentication 
                 * from a limited proxy? 
                 * Globus gatekeepers will say no
                 */
                if (context->req_flags & 
                    GSS_C_GLOBUS_DONT_ACCEPT_LIMITED_PROXY_FLAG)
                {
                    major_status = GSS_S_UNAUTHORIZED;
                    GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                        minor_status,
                        GLOBUS_GSI_GSSAPI_ERROR_PROXY_VIOLATION,
                        (_GGSL("Function set to not accept limited proxies")));
                    context->gss_state = GSS_CON_ST_DONE;
                    break;
                }
                                
            }

            context->ret_flags |= GSS_C_MUTUAL_FLAG;
            context->ret_flags |= GSS_C_PROT_READY_FLAG;
            context->ret_flags |= GSS_C_INTEG_FLAG  
                               |  GSS_C_REPLAY_FLAG
                               |  GSS_C_SEQUENCE_FLAG;

#           if LINK_WITH_INTERNAL_OPENSSL_API
            context->ret_flags |=  GSS_C_TRANS_FLAG;
#           endif
            /* 
             * IF we are talking to a real SSL client,
             * we don't want to do delegation, so we are done
             */
            if (context->req_flags & GSS_C_GLOBUS_SSL_COMPATIBLE)
            {
                context->gss_state = GSS_CON_ST_DONE;
                break;
            }
        
            /* 
             * To keep the gss exchange going, if we received
             * the last token but don't have a token to send
             * we need to send a null So peek at what we might send
             */
            if (BIO_pending(context->gss_wbio) == 0)
            {
                BIO_write(context->gss_sslbio, "\0", 1);
            }
            context->gss_state = GSS_CON_ST_FLAGS;
            break;

        case(GSS_CON_ST_FLAGS):
        
            readlen = BIO_read(context->gss_sslbio, dbuf, 1);
            if (readlen != 1)
            {
                if (BIO_should_retry(context->gss_sslbio))
                {
                    /*
                     * Handle receipt of empty fragments sent as a
                     * countermeasure against an SSL 3.0/TLS 1.0
                     * protocol vulnerability affecting CBC ciphers.
                     * http://www.openssl.org/~bodo/tls-cbc.txt
                     */
                    context->gss_state = GSS_CON_ST_FLAGS;
                    break;
                }
                GLOBUS_GSI_GSSAPI_ERROR_RESULT(minor_status,
                     GLOBUS_GSI_GSSAPI_ERROR_WITH_DELEGATION,
                     (_GGSL("Delegation protocol violation: read failed")));
                context->gss_state = GSS_CON_ST_DONE;
                major_status = GSS_S_FAILURE;
                break;
            }
            
            /* 
             * proxy_handle gets initialized in
             * globus_i_gsi_gss_create_and_fill_context (called in the beginning
             * of this routine. As the key_bits value of peer_credentials is 
             * not available before the globus_i_gsi_gss_create_and_fill_context
             * call is made, it is destroyed and initialized here again with
             * appropriate attribute (with the key_bits set to peer's value).
             * This fixes bug 3794 (delegated credential generates 512 bit keys 
             * irrespective of the key strength of the peer credential), 
             * Couldn't remove the proxy_handle initialization in
             * globus_i_gsi_gss_create_and_fill_context because the removal 
             * caused an error 'NULL proxy handle passed to function: 
             * globus_gsi_proxy_inquire_req' in the delegation tests in 
             * gssapi/test. Also, the key_bits attr on the proxy_handle can not 
             * be set by globus_gsi_proxy_handle_attrs_set_keybits(context->
             * proxy_handle->attrs, key_bits) as context->proxy_handle->attrs is
             * private and can not be accessed directly. context->proxy_handle 
             * is used here and in 'case(GSS_CON_ST_CERT):'. The control will go
             * to 'case(GSS_CON_ST_CERT):' only if the proxy is created
             * successfully here. So it makes sense to initialize the proxy 
             * here. 
             */
             
            if (*dbuf == 'D')
            {
                globus_gsi_proxy_handle_attrs_t     proxy_handle_attrs;
                int                                 key_bits;
                
                local_result = globus_gsi_cred_get_key_bits(
                        context->peer_cred_handle->cred_handle, &key_bits);
                if(local_result != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                        minor_status, local_result,
                        GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
                    context->gss_state = GSS_CON_ST_DONE;
                    major_status = GSS_S_FAILURE;
                    break;
                }
                local_result = globus_gsi_proxy_handle_attrs_init(
                                                        &proxy_handle_attrs);
                if(local_result != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                        minor_status, local_result,
                        GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_PROXY);
                    context->gss_state = GSS_CON_ST_DONE;
                    major_status = GSS_S_FAILURE;
                    break;
                }            
                local_result = globus_gsi_proxy_handle_attrs_set_keybits(
                                                proxy_handle_attrs, key_bits);
                if(local_result != GLOBUS_SUCCESS)
                {
                    globus_gsi_proxy_handle_attrs_destroy(proxy_handle_attrs);
                    GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                        minor_status, local_result,
                        GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_PROXY);
                    context->gss_state = GSS_CON_ST_DONE;
                    major_status = GSS_S_FAILURE;
                    break;
                }

                local_result = globus_gsi_cred_get_cert(
                            context->peer_cred_handle->cred_handle,
                            &peer_cert);
                if (local_result != GLOBUS_SUCCESS)
                {
                    globus_gsi_proxy_handle_attrs_destroy(proxy_handle_attrs);
                    GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                        minor_status, local_result,
                        GLOBUS_GSI_GSSAPI_ERROR_WITH_DELEGATION);
                    major_status = GSS_S_FAILURE;
                    break;
                }
#if OPENSSL_VERSION_NUMBER < 0x10100000L
                peer_digest = EVP_get_digestbynid(
                        OBJ_obj2nid(peer_cert->sig_alg->algorithm));
#else
                {
                    X509_ALGOR *algor = X509_get0_tbs_sigalg(peer_cert);
                    peer_digest = EVP_get_digestbynid(
                            OBJ_obj2nid(algor->algorithm));

                }
#endif

                local_result = globus_gsi_proxy_handle_attrs_set_signing_algorithm(
                        proxy_handle_attrs, (EVP_MD *) peer_digest);
                if (local_result != GLOBUS_SUCCESS)
                {
                    globus_gsi_proxy_handle_attrs_destroy(proxy_handle_attrs);
                    GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                        minor_status, local_result,
                        GLOBUS_GSI_GSSAPI_ERROR_WITH_DELEGATION);
                    major_status = GSS_S_FAILURE;
                    break;
                }

                if(context->proxy_handle)
                {
                    globus_gsi_proxy_handle_destroy(context->proxy_handle);
                }
                local_result = globus_gsi_proxy_handle_init(
                                    &context->proxy_handle, proxy_handle_attrs);
                if(local_result != GLOBUS_SUCCESS)
                {
                    globus_gsi_proxy_handle_attrs_destroy(proxy_handle_attrs);
                    GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                        minor_status, local_result,
                        GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_PROXY);
                    major_status = GSS_S_FAILURE;
                    break;
                }
                local_result = globus_gsi_proxy_handle_attrs_destroy(
                                                        proxy_handle_attrs);
                if(local_result != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                        minor_status, local_result,
                        GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_PROXY);
                    context->gss_state = GSS_CON_ST_DONE;
                    major_status = GSS_S_FAILURE;
                    break;
                } 
                local_result = globus_gsi_proxy_create_req(
                                context->proxy_handle, context->gss_sslbio);
                if(local_result != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                        minor_status, local_result,
                        GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_PROXY);
                    context->gss_state = GSS_CON_ST_DONE;
                    major_status = GSS_S_FAILURE;
                    break;
                }                    
                context->gss_state = GSS_CON_ST_CERT;
            }
            else if(*dbuf != '0')
            {
                GLOBUS_GSI_GSSAPI_ERROR_RESULT(minor_status,
                                               GLOBUS_GSI_GSSAPI_ERROR_WITH_DELEGATION,
                                               (_GGSL("Delegation protocol violation")));
                context->gss_state = GSS_CON_ST_DONE;
                major_status = GSS_S_FAILURE;
            }
            else
            {
                context->gss_state = GSS_CON_ST_DONE;
            }


            break;

        case(GSS_CON_ST_CERT):

            local_result = globus_gsi_proxy_assemble_cred(
                context->proxy_handle,
                &delegated_cred,
                context->gss_sslbio);
            if(local_result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_result,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_PROXY);
                context->gss_state = GSS_CON_ST_DONE;
                major_status = GSS_S_FAILURE;
                break;
            }

            local_result = globus_gsi_callback_get_cert_chain(
                context->callback_data,
                &cert_chain);
            if(local_result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_result,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_CALLBACK_DATA);
                context->gss_state = GSS_CON_ST_DONE;
                major_status = GSS_S_FAILURE;
                break;
            }

            local_result = globus_gsi_cred_set_cert_chain(
                delegated_cred,
                cert_chain);

            sk_X509_pop_free(cert_chain, X509_free);
            
            if(local_result != GLOBUS_SUCCESS)
            {
                globus_gsi_cred_handle_destroy(delegated_cred);
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_result,
                    GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
                context->gss_state = GSS_CON_ST_DONE;
                major_status = GSS_S_FAILURE;
                break;
            }

            if(delegated_cred_handle_P)
            {
                major_status = globus_i_gsi_gss_create_cred(
                    &local_minor_status,
                    GSS_C_BOTH,
                    delegated_cred_handle_P,
                    &delegated_cred,
                    GLOBUS_FALSE);
                if(GSS_ERROR(major_status))
                {
                    globus_gsi_cred_handle_destroy(delegated_cred);
                    GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                        minor_status, local_minor_status,
                        GLOBUS_GSI_GSSAPI_ERROR_WITH_DELEGATION);
                    context->gss_state = GSS_CON_ST_DONE;
                    major_status = GSS_S_FAILURE;
                    break;
                }            
            }
            else
            {
                local_result = globus_gsi_cred_handle_destroy(delegated_cred);
                if(local_result != GLOBUS_SUCCESS)
                {
                    GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                        minor_status, local_result,
                        GLOBUS_GSI_GSSAPI_ERROR_WITH_DELEGATION);
                    context->gss_state = GSS_CON_ST_DONE;
                    major_status = GSS_S_FAILURE;
                    break;
                }
            }

            context->ret_flags |= GSS_C_DELEG_FLAG;
            
            context->gss_state = GSS_CON_ST_DONE;

        default:
            break;

    } /* end of switch for gss_con_st */

    /* We want to go ahead and get an ouput token even if we previously
     * encountered an error since the output token may contain information
     * about the error (i.e. an SSL alert message) we want to send to the other
     * side.
     */
    local_major_status = globus_i_gsi_gss_get_token(
        &local_minor_status, 
        context, NULL, output_token);

    if(GSS_ERROR(local_major_status))
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_TOKEN_FAIL);
        major_status = local_major_status;
        goto exit;
    }

    if(GSS_ERROR(major_status))
    {
        goto exit;
    }

    if(context->gss_state != GSS_CON_ST_DONE)
    {
        major_status |= GSS_S_CONTINUE_NEEDED;
    }
    else if(time_rec != NULL)
    {
        time_t                          lifetime;
        time_t                          current_time;
        
        major_status = globus_i_gsi_gss_get_context_goodtill(
            &local_minor_status,
            context,
            &lifetime);
        if(GSS_ERROR(major_status))
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CONTEXT);
            goto exit;
        }

        current_time = time(NULL);

        if(current_time > lifetime)
        {
            *time_rec = 0;
        }
        else
        {
            *time_rec = (OM_uint32) (lifetime - current_time);
        }
    }

    if (ret_flags != NULL)
    {
        *ret_flags = context->ret_flags;
    }

    GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
        2, (globus_i_gsi_gssapi_debug_fstream, 
            "accept_sec_context:major_status:%08x"
            ":gss_state:%d:ret_flags=%08x\n",
            (unsigned int) major_status, 
            context->gss_state, 
            (unsigned int) context->ret_flags));

 exit:
    if (peer_cert != NULL)
    {
        X509_free(peer_cert);
    }

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}
