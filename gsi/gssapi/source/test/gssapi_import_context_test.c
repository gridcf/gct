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

#include "gssapi.h"
#include "gssapi_test_utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{
    OM_uint32                           init_maj_stat;
    OM_uint32                           accept_maj_stat;
    OM_uint32                           min_stat;
    OM_uint32                           init_ret_flags;
    OM_uint32                           accept_ret_flags;
    OM_uint32                           req_flags;
    gss_buffer_desc                     send_tok;
    gss_buffer_desc                     recv_tok;
    gss_buffer_desc *                   token_ptr;
    gss_buffer_desc                     init_buffer;
    gss_buffer_desc                     accept_buffer;
    gss_OID                             mech_type;
    gss_name_t                          source_name;
    gss_ctx_id_t                        init_context;
    gss_ctx_id_t                        accept_context;
    int                                 rc = 0;

    printf("1..1\n");

    /* Initialize variables */
    
    token_ptr = GSS_C_NO_BUFFER;
    init_context = GSS_C_NO_CONTEXT;
    accept_context = GSS_C_NO_CONTEXT;
    accept_maj_stat = GSS_S_CONTINUE_NEEDED;
    init_ret_flags = 0;
    accept_ret_flags = 0;
    req_flags = GSS_C_CONF_FLAG;

    /* set up the first security context */
    init_maj_stat = gss_init_sec_context(&min_stat,
                                         GSS_C_NO_CREDENTIAL,
                                         &init_context,
                                         GSS_C_NO_NAME,
                                         GSS_C_NULL_OID,
                                         req_flags,
                                         0,
                                         GSS_C_NO_CHANNEL_BINDINGS,
                                         token_ptr,
                                         NULL,
                                         &send_tok,
                                         &init_ret_flags,
                                         NULL);


    if(init_maj_stat != GSS_S_CONTINUE_NEEDED)
    {
        globus_gsi_gssapi_test_print_error(stderr, init_maj_stat, min_stat);
        rc = 1;
        goto fail;
    }

    while(1)
    {
        
        accept_maj_stat=gss_accept_sec_context(&min_stat,
                                               &accept_context,
                                               GSS_C_NO_CREDENTIAL,
                                               &send_tok, 
                                               GSS_C_NO_CHANNEL_BINDINGS,
                                               &source_name,
                                               &mech_type,
                                               &recv_tok,
                                               &accept_ret_flags,
                                               /* ignore time_rec */
                                               0, 
                                               NULL);

        if(accept_maj_stat != GSS_S_COMPLETE &&
           accept_maj_stat != GSS_S_CONTINUE_NEEDED)
        {
            globus_gsi_gssapi_test_print_error(stderr, accept_maj_stat, min_stat);
            rc = 1;
            goto fail;
        }
        else if(accept_maj_stat == GSS_S_COMPLETE)
        {
            break;
        }

        init_maj_stat = gss_init_sec_context(&min_stat,
                                             GSS_C_NO_CREDENTIAL,
                                             &init_context,
                                             GSS_C_NO_NAME,
                                             GSS_C_NULL_OID,
                                             req_flags,
                                             0,
                                             GSS_C_NO_CHANNEL_BINDINGS,
                                             &recv_tok,
                                             NULL,
                                             &send_tok,
                                             &init_ret_flags,
                                             NULL);
        
        
        if(init_maj_stat != GSS_S_COMPLETE &&
           init_maj_stat != GSS_S_CONTINUE_NEEDED)
        {
            globus_gsi_gssapi_test_print_error(stderr, init_maj_stat, min_stat);
            rc = 1;
            goto fail;
        }
    }
    if (!(init_ret_flags&GSS_C_TRANS_FLAG))
    {
        printf("ok 1 - gssapi_import_context_test # skip GSS_C_TRANS_FLAG not set\n");
        rc = 77;
        goto skip;
    }
    
    /* Wrap a token with the initiator context */
    send_tok.value = "hello";
    send_tok.length = 6;
    init_maj_stat = gss_wrap(&min_stat, init_context, 0, 0, &send_tok, NULL, 
                             &recv_tok);
    if (init_maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, init_maj_stat, min_stat);
        rc = 1;
        goto fail;
    }
    /* unwrap the token with the acceptor context */
    accept_maj_stat = gss_unwrap(&min_stat, accept_context, &recv_tok, &send_tok, 0, NULL);
    if (accept_maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, accept_maj_stat, min_stat);
        rc = 1;
        goto fail;
    }
    if (strcmp("hello", send_tok.value) != 0)
    {
        printf("Hello garbled\n");
        rc = 1;
        goto fail;
    }
    /* Export initiator context */
    init_maj_stat = gss_export_sec_context(
            &min_stat, &init_context,
            &init_buffer);
    if (init_maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, init_maj_stat, min_stat);
        rc = 1;
        goto fail;
    }

    /* Export acceptor context */
    accept_maj_stat = gss_export_sec_context(
            &min_stat, &accept_context,
            &accept_buffer);
    if (accept_maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, accept_maj_stat, min_stat);
        rc = 1;
        goto fail;
    }

    /* Import the init context */
    init_maj_stat = gss_import_sec_context(&min_stat,
            &init_buffer, &init_context);
    if (init_maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, init_maj_stat, min_stat);
        rc = 1;
        goto fail;
    }

    /* Import the accept context */
    accept_maj_stat = gss_import_sec_context(&min_stat,
            &accept_buffer, &accept_context);
    if (accept_maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, accept_maj_stat, min_stat);
        rc = 1;
        goto fail;
    }
    /* Wrap a token with the initiator context */
    send_tok.value = "hello";
    send_tok.length = 6;
    init_maj_stat = gss_wrap(&min_stat, init_context, 0, 0, &send_tok, NULL, 
                             &recv_tok);
    if (init_maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, init_maj_stat, min_stat);
        rc = 1;
        goto fail;
    }
    /* unwrap the token with the acceptor context */
    accept_maj_stat = gss_unwrap(&min_stat, accept_context, &recv_tok, &send_tok, 0, NULL);
    if (accept_maj_stat != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, accept_maj_stat, min_stat);
        rc = 1;
        goto fail;
    }
    if (strcmp("hello", send_tok.value) != 0)
    {
        rc = 1;
        fprintf(stderr, "# Hello garbled in 2nd message\n");
        goto fail;
    }

    printf("ok 1 - gssapi_import_context_test\n");
    return 0;
fail:
    printf("not ok 1 - gssapi_import_context_test\n");
skip:
    return rc;
}
