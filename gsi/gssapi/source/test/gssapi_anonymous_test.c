/*
 * Copyright 1999-2008 University of Chicago
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

#include "gssapi.h"
#include "globus_error_openssl.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "openssl/crypto.h"
#include "gssapi_test_utils.h"

void globus_print_error(globus_result_t              error_result);

int main()
{
    OM_uint32                           init_maj_stat;
    OM_uint32                           accept_maj_stat;
    OM_uint32                           maj_stat;
    OM_uint32                           min_stat;
    OM_uint32                           ret_flags;
    OM_uint32                           req_flags;
    gss_buffer_desc                     send_tok;
    gss_buffer_desc                     recv_tok;
    gss_buffer_desc *                   token_ptr;
    gss_OID                             mech_type;
    gss_name_t                          target_name;
    gss_name_t                          source_name;
    gss_ctx_id_t                        init_context;
    gss_ctx_id_t                        accept_context;
    gss_cred_id_t                       cred_handle;
    int                                 rc = EXIT_SUCCESS;

    printf("1..1\n");

    /* Initialize variables */
    
    token_ptr = GSS_C_NO_BUFFER;
    init_context = GSS_C_NO_CONTEXT;
    accept_context = GSS_C_NO_CONTEXT;
    accept_maj_stat = GSS_S_CONTINUE_NEEDED;
    req_flags = GSS_C_ANON_FLAG|GSS_C_CONF_FLAG;
    ret_flags = 0;
    send_tok.value = NULL;
    recv_tok.value = NULL;

    globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE);
    
    /* acquire the credential */

    CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ENABLE);
    CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ON);

    maj_stat = gss_acquire_cred(&min_stat,
                                NULL,
                                GSS_C_INDEFINITE,
                                GSS_C_NO_OID_SET,
                                GSS_C_BOTH,
                                &cred_handle,
                                NULL,
                                NULL);

    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, maj_stat, min_stat);
        globus_print_error((globus_result_t) min_stat);
        rc = EXIT_FAILURE;
        goto fail;
    }

    /* get the subject name */
    
    maj_stat = gss_inquire_cred(&min_stat, 
                                cred_handle,
                                &target_name,
                                NULL,
                                NULL,
                                NULL);
    
    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, maj_stat, min_stat);
        globus_print_error((globus_result_t) min_stat);
        rc = EXIT_FAILURE;
        goto fail;
    }

    /* set up the first security context */
    
    init_maj_stat = gss_init_sec_context(&min_stat,
                                         GSS_C_NO_CREDENTIAL,
                                         &init_context,
                                         target_name,
                                         GSS_C_NULL_OID,
                                         req_flags,
                                         0,
                                         GSS_C_NO_CHANNEL_BINDINGS,
                                         token_ptr,
                                         NULL,
                                         &send_tok,
                                         NULL,
                                         NULL);

    if(init_maj_stat != GSS_S_CONTINUE_NEEDED)
    {
        globus_gsi_gssapi_test_print_error(stderr, init_maj_stat, min_stat);
        globus_print_error((globus_result_t) min_stat);
        rc = EXIT_FAILURE;
        goto fail;
    }

    while(1)
    {
        if(recv_tok.value)
        {
            free(recv_tok.value);
            recv_tok.value = NULL;
        }

        accept_maj_stat=gss_accept_sec_context(&min_stat,
                                               &accept_context,
                                               GSS_C_NO_CREDENTIAL,
                                               &send_tok, 
                                               GSS_C_NO_CHANNEL_BINDINGS,
                                               &source_name,
                                               &mech_type,
                                               &recv_tok,
                                               &ret_flags,
                                               NULL, 
                                               NULL);

        if(accept_maj_stat != GSS_S_COMPLETE &&
           accept_maj_stat != GSS_S_CONTINUE_NEEDED)
        {
            globus_gsi_gssapi_test_print_error(stderr, accept_maj_stat, min_stat);
            globus_print_error((globus_result_t) min_stat);
            rc = EXIT_FAILURE;
            goto fail;
        }
        else if(accept_maj_stat == GSS_S_COMPLETE)
        {
            break;
        }

        if(send_tok.value)
        {
            free(send_tok.value);
            send_tok.value = NULL;
        }

        init_maj_stat = gss_init_sec_context(&min_stat,
                                             GSS_C_NO_CREDENTIAL,
                                             &init_context,
                                             target_name,
                                             GSS_C_NULL_OID,
                                             req_flags,
                                             0,
                                             GSS_C_NO_CHANNEL_BINDINGS,
                                             &recv_tok,
                                             NULL,
                                             &send_tok,
                                             NULL,
                                             NULL);
        
        
        if(init_maj_stat != GSS_S_COMPLETE &&
           init_maj_stat != GSS_S_CONTINUE_NEEDED)
        {
            globus_gsi_gssapi_test_print_error(stderr, init_maj_stat, min_stat);
            globus_print_error((globus_result_t) min_stat);
            rc = EXIT_FAILURE;
            goto fail;
        }
    }

    maj_stat = gss_delete_sec_context(&min_stat,
                                      &init_context,
                                      GSS_C_NO_BUFFER);
    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, maj_stat, min_stat);
        globus_print_error((globus_result_t) min_stat);
        rc = EXIT_FAILURE;
        goto fail;
    }

    maj_stat = gss_delete_sec_context(&min_stat,
                                      &accept_context,
                                      GSS_C_NO_BUFFER);
    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, maj_stat, min_stat);
        globus_print_error((globus_result_t) min_stat);
        rc = EXIT_FAILURE;
        goto fail;
    }

    maj_stat = gss_release_name(&min_stat,
                                &target_name);
    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, maj_stat, min_stat);
        globus_print_error((globus_result_t) min_stat);
        rc = EXIT_FAILURE;
        goto fail;
    }

    maj_stat = gss_release_cred(&min_stat,
                                &cred_handle);
    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, maj_stat, min_stat);
        globus_print_error((globus_result_t) min_stat);
        rc = EXIT_FAILURE;
        goto fail;
    }

fail:
    printf("%s gssapi_anonymous_test\n", rc == EXIT_SUCCESS ? "ok" : "no ok");
    globus_module_deactivate(GLOBUS_GSI_GSSAPI_MODULE);

    exit(rc);
}

void globus_print_error(
    globus_result_t                     error_result)
{
    globus_object_t *                   error_obj = NULL;
    char *                              error_string = NULL;
    
    error_obj = globus_error_get(error_result);
    error_string = globus_error_print_chain(error_obj);
    globus_libc_fprintf(stderr, "%s\n", error_string);
    globus_libc_free(error_string);
    globus_object_free(error_obj);
}

