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
 * @file init_sec_context.c
 * @author Sam Lang, Sam Meder
 */
#endif

#include "gssapi_openssl.h"
#include "globus_i_gsi_gss_utils.h"
#include <string.h>
#include "openssl/evp.h"

#include "globus_gsi_cert_utils.h"

/**
 * @brief Init Sec Context
 * @ingroup globus_gsi_gssapi
 */
OM_uint32
GSS_CALLCONV gss_init_sec_context(
    OM_uint32 *                         minor_status,
    const gss_cred_id_t                 initiator_cred_handle,
    gss_ctx_id_t *                      context_handle_P,
    const gss_name_t                    target_name,
    const gss_OID                       mech_type,
    OM_uint32                           req_flags,
    OM_uint32                           time_req,
    const gss_channel_bindings_t        input_chan_bindings,
    const gss_buffer_t                  input_token,
    gss_OID *                           actual_mech_type,
    gss_buffer_t                        output_token,
    OM_uint32 *                         ret_flags,
    OM_uint32 *                         time_rec)
{
    gss_ctx_id_desc *                   context = NULL;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           local_minor_status;
    OM_uint32                           local_major_status;
    globus_result_t                     local_result;
    int                                 rc;
    unsigned char *                     tbuf = NULL;
    size_t                              tlen = 0;
    char                                cbuf[1];
    gss_OID                             actual_mech = GSS_C_NO_OID;
    globus_gsi_cert_utils_cert_type_t   cert_type;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    *minor_status = (OM_uint32) GLOBUS_SUCCESS;
    output_token->length = 0;

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

    if(req_flags & GSS_C_ANON_FLAG &&
       req_flags & GSS_C_DELEG_FLAG)
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Can't initialize a context to be both anonymous and "
             "provide delegation")));
        goto error_exit;
    }

    if(req_flags & GSS_C_GLOBUS_SSL_COMPATIBLE &&
       req_flags & GSS_C_DELEG_FLAG)
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Can't initialize a context to both use SSL compatible "
             "context establishment and provide delegation")));
        goto error_exit;
    }

    if(req_flags & GSS_C_DELEG_FLAG &&
       target_name == GSS_C_NO_NAME)
    {
        major_status = GSS_S_FAILURE;
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Need a target name for authorization prior "
             "to doing delegation")));
        goto error_exit;
    }


    if ((context == (gss_ctx_id_t) GSS_C_NO_CONTEXT) ||
        !(context->ctx_flags & GSS_I_CTX_INITIALIZED))
    {
        GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
            2, (globus_i_gsi_gssapi_debug_fstream,
                "Creating context w/ %s.\n",
                (initiator_cred_handle == GSS_C_NO_CREDENTIAL) ?
                "GSS_C_NO_CREDENTIAL" :
                "Credentials provided"));

        if (mech_type == GSS_C_NO_OID)
        {
            if (globus_i_backward_compatible_mic)
            {
                actual_mech = (gss_OID) gss_mech_globus_gssapi_openssl;
                GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                    2, (globus_i_gsi_gssapi_debug_fstream,
                        "init_sec_context: no mech_type requested; using OLD MECH OID\n"));
            }
            else
            {
                actual_mech = (gss_OID) gss_mech_globus_gssapi_openssl_micv2;
                GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                    2, (globus_i_gsi_gssapi_debug_fstream,
                        "init_sec_context: no mech_type requested; using MICV2 MECH OID\n"));
            }
        }
#       if OPENSSL_VERSION_NUMBER < 0x10100000L
        else if (g_OID_equal(mech_type, (gss_OID) gss_mech_globus_gssapi_openssl))
        {
            actual_mech = (gss_OID) gss_mech_globus_gssapi_openssl;
                GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                    2, (globus_i_gsi_gssapi_debug_fstream,
                        "init_sec_context: OLD MECH OID requested\n"));
        }
#       endif
#       if OPENSSL_VERSION_NUMBER >= 0x10000100L
        else if (g_OID_equal(mech_type, (gss_OID) gss_mech_globus_gssapi_openssl_micv2))
        {
            actual_mech = (const gss_OID) gss_mech_globus_gssapi_openssl_micv2;
                GLOBUS_I_GSI_GSSAPI_DEBUG_FPRINTF(
                    2, (globus_i_gsi_gssapi_debug_fstream,
                        "init_sec_context: MICV2 MECH OID requested\n"));
        }
#       endif
        else
        {
            GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
                (_GGSL("Invalid desired_mech passed to function")));
            major_status = GSS_S_FAILURE;
            goto exit;
        }

        major_status =
            globus_i_gsi_gss_create_and_fill_context(&local_minor_status,
                                                     &context,
                                                     actual_mech,
                                                     target_name,
                                                     initiator_cred_handle,
                                                     GSS_C_INITIATE,
                                                     req_flags);
        if (GSS_ERROR(major_status))
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CONTEXT);
            goto error_exit;
        }

        *context_handle_P = context;

        if (actual_mech_type != NULL)
        {
            *actual_mech_type = (gss_OID) actual_mech;
        }

        if (ret_flags != NULL)
        {
            *ret_flags = 0 ;
        }
    }
    else if (input_token->length > 0)
    {
        major_status = globus_i_gsi_gss_put_token(&local_minor_status,
                                                  context,
                                                  NULL,
                                                  input_token);
        if (GSS_ERROR(major_status))
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_TOKEN_FAIL);
            goto error_exit;
        }
    }

    switch (context->gss_state)
    {
    case(GSS_CON_ST_HANDSHAKE):

        /* do the handshake work */

        major_status = globus_i_gsi_gss_handshake(&local_minor_status,
                                                  context);

        if (major_status == GSS_S_CONTINUE_NEEDED)
        {
            break;
        }

        if(GSS_ERROR(major_status))
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_HANDSHAKE);
            context->gss_state = GSS_CON_ST_DONE;
            break;
        }

        /* make sure we are talking to the correct server */
        major_status = globus_i_gsi_gss_retrieve_peer(&local_minor_status,
                                                      context,
                                                      GSS_C_INITIATE);
        if (GSS_ERROR(major_status))
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSS_CONTEXT);
            context->gss_state = GSS_CON_ST_DONE;
            break;
        }

        local_result = globus_gsi_callback_get_cert_type(
            context->callback_data,
            &cert_type);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_CALLBACK_DATA);
            major_status = GSS_S_FAILURE;
            goto error_exit;
        }

        /*
         * Need to check if the server is using a limited proxy.
         * And if that is acceptable here.
         * Caller tells us if it is not acceptable to
         * use a limited proxy.
         */
        if ((context->req_flags &
             GSS_C_GLOBUS_DONT_ACCEPT_LIMITED_PROXY_FLAG)
            && GLOBUS_GSI_CERT_UTILS_IS_LIMITED_PROXY(cert_type))
        {
            major_status = GSS_S_UNAUTHORIZED;
            GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_PROXY_VIOLATION,
                (_GGSL("Function set to not accept limited proxies")));
            context->gss_state = GSS_CON_ST_DONE;
            break;
        }

        /* this is the mutual authentication test */
        if (target_name != NULL)
        {
            major_status =
                gss_compare_name(&local_minor_status,
                                 context->peer_cred_handle->globusid,
                                 target_name,
                                 &rc);
            if (GSS_ERROR(major_status))
            {
                GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                    minor_status, local_minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_BAD_NAME);
                context->gss_state = GSS_CON_ST_DONE;
                break;
            }
            else if(rc == GSS_NAMES_NOT_EQUAL)
            {
                char *                  expected_name;
                char *                  actual_name;

                if(g_OID_equal(((gss_name_desc*)  target_name)->name_oid,
                               GSS_C_NT_HOSTBASED_SERVICE))
                {
                    GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                        minor_status,
                        GLOBUS_GSI_GSSAPI_ERROR_AUTHZ_DENIED,
                        (_GGSL("The expected name for the remote host (%s%s%s) does not match the authenticated "
                               "name of the remote host (%s%s%s). This happens when the name in the host certificate does not match the information obtained from DNS and is often a DNS configuration problem."),
                         target_name->service_name
                            ? target_name->service_name : "",
                         target_name->service_name
                            ? "@" : "",
                         target_name->host_name
                            ? target_name->host_name : "unknown",
                         context->peer_cred_handle->globusid->service_name
                            ? context->peer_cred_handle->globusid->service_name
                            : "",
                         context->peer_cred_handle->globusid->service_name
                            ? "@" : "",
                         context->peer_cred_handle->globusid->host_name
                            ?  context->peer_cred_handle->globusid->host_name
                            : "unknown"));
                }
                else
                {
                    expected_name =
                        ((gss_name_desc*)  target_name)->x509n_oneline;
                    actual_name =
                        ((gss_name_desc*)
                         context->peer_cred_handle->globusid)->x509n_oneline;

                    GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                        minor_status,
                        GLOBUS_GSI_GSSAPI_ERROR_AUTHZ_DENIED,
                        (_GGSL("The name of the remote entity (%s), and the expected "
                               "name for the remote entity (%s) do not match"),
                         actual_name, expected_name));
                }

                major_status = GSS_S_UNAUTHORIZED;
                context->gss_state = GSS_CON_ST_DONE;
                break;
            }
        }

        context->ret_flags |= GSS_C_MUTUAL_FLAG;
        context->ret_flags |= GSS_C_PROT_READY_FLAG;
        context->ret_flags |= GSS_C_INTEG_FLAG
            | GSS_C_REPLAY_FLAG
            | GSS_C_SEQUENCE_FLAG
            | GSS_C_ANON_FLAG
            | GSS_C_DELEG_FLAG;
        if (GLOBUS_GSI_CERT_UTILS_IS_LIMITED_PROXY(cert_type))
        {
            context->ret_flags |= GSS_C_GLOBUS_RECEIVED_LIMITED_PROXY_FLAG;
        }
#       if LINK_WITH_INTERNAL_OPENSSL_API
        context->ret_flags |= GSS_C_TRANS_FLAG;
#       endif

        /*
         * TLS v1.3 sends 2 session tokens after the handshake is completed.
         * We need to recieve them before continuing with the GSI proxy.
         */

#       if defined(TLS1_3_VERSION)
        if (SSL_version(context->gss_ssl) >= TLS1_3_VERSION)
        {
            context->gss_state = GSS_CON_ST_TOKEN1;
            break;
        }
#       endif

        /*
         * IF we are talking to a real SSL server,
         * we don't want to do delegation, so we are done
         */

        if (context->req_flags & GSS_C_GLOBUS_SSL_COMPATIBLE)
        {
            context->gss_state = GSS_CON_ST_DONE;
            break;
        }

        /*
         * If we have completed the handshake, but don't
         * have any more data to send, we can send the flag
         * now. i.e. fall through without break,
         * Otherwise, we will wait for the null byte
         * to get back in sync which we will ignore.
         * We can't check output_token->length, since we haven't
         * extracted the output payload yet, so look directly
         * in context->gss_wbio.
         */

        if (BIO_pending(context->gss_wbio) != 0)
        {
            context->gss_state=GSS_CON_ST_FLAGS;
            break;
        }

        goto flags;

    case(GSS_CON_ST_TOKEN1):

        if (input_token->length > 0)
        {
            tbuf = input_token->value;
            tlen = ((unsigned int)tbuf[3] << 8) + (unsigned int)tbuf[4];

            if (input_token->length == tlen + 5)
            {
                context->gss_state=GSS_CON_ST_TOKEN2;
                break;
            }

            if (input_token->length < tlen + 10)
            {
                major_status = GSS_S_UNAUTHORIZED;
                GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_TOKEN_FAIL,
                    (_GGSL("Failed identifying TLS session token 1")));
                context->gss_state = GSS_CON_ST_DONE;
                break;
            }

            tbuf = &tbuf[tlen + 5];
        }

    case(GSS_CON_ST_TOKEN2):

        if (input_token->length > 0)
        {
            if (tbuf == NULL)
            {
                tbuf = input_token->value;
            }
            tlen = ((unsigned int)tbuf[3] << 8) + (unsigned int)tbuf[4];

            if (input_token->value + input_token->length != tbuf + tlen + 5)
            {
                major_status = GSS_S_UNAUTHORIZED;
                GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_TOKEN_FAIL,
                    (_GGSL("Failed identifying TLS session token 2")));
                context->gss_state = GSS_CON_ST_DONE;
                break;
            }
        }

        /*
         * IF we are talking to a real SSL server,
         * we don't want to do delegation, so we are done
         */

        if (context->req_flags & GSS_C_GLOBUS_SSL_COMPATIBLE)
        {
            context->gss_state = GSS_CON_ST_DONE;
            break;
        }

    case(GSS_CON_ST_FLAGS):

        if (tbuf == NULL && input_token->length > 0)
        {
            BIO_read(context->gss_sslbio, cbuf, 1);
        }

    flags:
        /* send D if we want delegation, 0 otherwise */

        if (context->req_flags & GSS_C_DELEG_FLAG)
        {
            BIO_write(context->gss_sslbio, "D", 1);
            context->gss_state = GSS_CON_ST_REQ;
        }
        else
        {
            BIO_write(context->gss_sslbio, "0", 1);
            context->gss_state = GSS_CON_ST_DONE;
        }
        break;

    case(GSS_CON_ST_REQ):

        local_result = globus_gsi_proxy_inquire_req(
            context->proxy_handle,
            context->gss_sslbio);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_PROXY_NOT_RECEIVED);
            major_status = GSS_S_FAILURE;
            context->gss_state = GSS_CON_ST_DONE;
            goto error_exit;
        }

        local_result = globus_gsi_cred_get_cert_type(
            context->cred_handle->cred_handle,
            &cert_type);

        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
            major_status = GSS_S_FAILURE;
            context->gss_state = GSS_CON_ST_DONE;
            goto error_exit;
        }

        local_result =
            globus_gsi_proxy_handle_set_type(
                context->proxy_handle,
                (context->req_flags & GSS_C_GLOBUS_DELEGATE_LIMITED_PROXY_FLAG)
                ? GLOBUS_GSI_CERT_UTILS_TYPE_LIMITED_PROXY
                : GLOBUS_GSI_CERT_UTILS_TYPE_IMPERSONATION_PROXY);

        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_PROXY);
            major_status = GSS_S_FAILURE;
            context->gss_state = GSS_CON_ST_DONE;
            goto exit;
        }


        local_result = globus_gsi_proxy_sign_req(
            context->proxy_handle,
            context->cred_handle->cred_handle,
            context->gss_sslbio);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_PROXY);
            major_status = GSS_S_FAILURE;
            context->gss_state = GSS_CON_ST_DONE;
            goto error_exit;
        }

        context->gss_state = GSS_CON_ST_DONE;
        break;

    case(GSS_CON_ST_CERT): ;
    case(GSS_CON_ST_DONE): ;
    } /* end of switch for gss_con_st */

    local_major_status = globus_i_gsi_gss_get_token(&local_minor_status,
                                                    context,
                                                    NULL,
                                                    output_token);

    if(GSS_ERROR(local_major_status))
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_TOKEN_FAIL);
        major_status = GSS_S_FAILURE;
        context->gss_state = GSS_CON_ST_DONE;
        goto error_exit;
    }

    /* some error occurred during switch */
    if(GSS_ERROR(major_status))
    {
        goto error_exit;
    }

    if (context->gss_state != GSS_CON_ST_DONE)
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
            "init_sec_context:major_status:%08x"
            ":gss_state:%d req_flags=%08x:ret_flags=%08x\n",
            (unsigned int) major_status,
            context->gss_state,
            (unsigned int) req_flags,
            (unsigned int) context->ret_flags));

    goto exit;

 error_exit:

    gss_delete_sec_context(&local_minor_status,
                           (gss_ctx_id_t *) &context,
                           output_token->length == 0 ? output_token : GSS_C_NO_BUFFER);
    *context_handle_P = (gss_ctx_id_t) context;

 exit:

    GLOBUS_I_GSI_GSSAPI_DEBUG_EXIT;
    return major_status;
}
