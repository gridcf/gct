/*
 * Copyright 1999-2016 University of Chicago
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

#include "gssapi_test_utils.h"
#include <stdbool.h>

#define NUM_TEST_CREDS 3

gss_cred_id_t                           creds[NUM_TEST_CREDS];
gss_name_t                              names[NUM_TEST_CREDS];

static gss_OID_desc GSS_SNI_CREDENTIALS_OID =
   {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x03\x04"};
static gss_OID_desc * GSS_SNI_CREDENTIALS =
   &GSS_SNI_CREDENTIALS_OID;

static gss_OID_desc gss_ext_server_name_oid_desc =
     {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x01\x09"}; 
static gss_OID_desc * gss_ext_server_name_oid =
                &gss_ext_server_name_oid_desc;
struct test_case
{
    bool                              (*func)(void);
    const char *                        name;
};

/**
 * @brief Test case for non-SNI aware client
 * @details
 *     In this test case, establish a security context, with server ready for
 *     SNI, but the client not providing one. The SNI callback should respond
 *     with the default credential. The client inquires the peer name and
 *     verifies it matches the default credential.
 */
bool
init_no_client_sni(void)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_ctx_id_t                        init_context = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    gss_name_t                          peer_name = GSS_C_NO_NAME;
    gss_buffer_desc                     init_generated_token = {0};
    gss_buffer_desc                     accept_generated_token = {0};
    bool                                result = true;
    int                                 name_equal = false;
    OM_uint32                           ignore_minor_status = 0;

    major_status = gss_set_sec_context_option(
            &minor_status,
            &accept_context,
            GSS_SNI_CREDENTIALS,
            &(gss_buffer_desc)
            {
                .value = creds,
                .length = sizeof(creds),
            });
    if (major_status != GSS_S_COMPLETE)
    {
        result = false;

        goto fail;
    }
    do
    {
        major_status = gss_init_sec_context(
                &minor_status,
                GSS_C_NO_CREDENTIAL,
                &init_context,
                GSS_C_NO_NAME,
                GSS_C_NO_OID,
                0,
                0,
                GSS_C_NO_CHANNEL_BINDINGS,
                &accept_generated_token,
                NULL,
                &init_generated_token,
                NULL,
                NULL);

        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);

        if (GSS_ERROR(major_status))
        {
            result = false;
            break;
        }

        if (init_generated_token.length > 0)
        {
            major_status = gss_accept_sec_context(
                    &minor_status,
                    &accept_context,
                    GSS_C_NO_CREDENTIAL,
                    &init_generated_token,
                    GSS_C_NO_CHANNEL_BINDINGS,
                    NULL,
                    NULL,
                    &accept_generated_token,
                    NULL,
                    NULL,
                    NULL);
            gss_release_buffer(
                    &ignore_minor_status,
                    &init_generated_token);

            if (GSS_ERROR(major_status))
            {
                result = false;
            }
        }
    }
    while (major_status == GSS_S_CONTINUE_NEEDED);

    if (major_status == GSS_S_COMPLETE)
    {
        major_status = gss_inquire_context(
                &minor_status,
                init_context,
                NULL,
                &peer_name,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL);
        if (major_status != GSS_S_COMPLETE)
        {
            result = false;

            goto fail;
        }

        major_status = gss_compare_name(
                &minor_status,
                names[0],
                peer_name,
                &name_equal);

        if (major_status != GSS_S_COMPLETE)
        {
            result = false;
        }
    }

fail:
    if (major_status != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(
                stderr,
                major_status,
                minor_status);
    }

    if (init_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
                &ignore_minor_status,
                &init_context,
                NULL);
    }
    if (accept_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
                &ignore_minor_status,
                &accept_context,
                NULL);
    }
    if (peer_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &ignore_minor_status,
                &peer_name);
    }
    if (init_generated_token.length != 0)
    {
        gss_release_buffer(
                &ignore_minor_status,
                &init_generated_token);
    }
    if (accept_generated_token.length != 0)
    {
        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);
    }

    return result;
}
/* init_no_client_sni() */

/**
 * @brief Test case for SNI aware client
 * @details
 *     In this test case, establish a security context, with server not ready
 *     for SNI, and the client sends the known name. The client inquires the
 *     peer name and verifies it matches the desired credential.
 */
bool
init_no_server_sni(void)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_ctx_id_t                        init_context = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    gss_name_t                          target_name = GSS_C_NO_NAME;
    gss_name_t                          peer_name = GSS_C_NO_NAME;
    gss_buffer_desc                     init_generated_token = {0};
    gss_buffer_desc                     accept_generated_token = {0};
    bool                                result = true;
    int                                 name_equal = false;
    OM_uint32                           ignore_minor_status = 0;

    major_status = gss_import_name(
            &minor_status,
            &(gss_buffer_desc)
            {
                .value = "/CN=test",
                .length = strlen("/CN=test"),
            },
            GSS_C_NO_OID,
            &target_name);

    if (major_status != GSS_S_COMPLETE)
    {
        result = false;
        goto fail;
    }
    do
    {
        major_status = gss_init_sec_context(
                &minor_status,
                GSS_C_NO_CREDENTIAL,
                &init_context,
                target_name,
                GSS_C_NO_OID,
                0,
                0,
                GSS_C_NO_CHANNEL_BINDINGS,
                &accept_generated_token,
                NULL,
                &init_generated_token,
                NULL,
                NULL);

        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);

        if (GSS_ERROR(major_status))
        {
            result = false;
            break;
        }

        if (init_generated_token.length > 0)
        {
            major_status = gss_accept_sec_context(
                    &minor_status,
                    &accept_context,
                    GSS_C_NO_CREDENTIAL,
                    &init_generated_token,
                    GSS_C_NO_CHANNEL_BINDINGS,
                    NULL,
                    NULL,
                    &accept_generated_token,
                    NULL,
                    NULL,
                    NULL);
            gss_release_buffer(
                    &ignore_minor_status,
                    &init_generated_token);

            if (GSS_ERROR(major_status))
            {
                result = false;
            }
        }
    }
    while (major_status == GSS_S_CONTINUE_NEEDED);

    if (major_status == GSS_S_COMPLETE)
    {
        major_status = gss_inquire_context(
                &minor_status,
                init_context,
                NULL,
                &peer_name,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL);
        if (major_status != GSS_S_COMPLETE)
        {
            result = false;

            goto fail;
        }

        major_status = gss_compare_name(
                &minor_status,
                target_name,
                peer_name,
                &name_equal);

        if (major_status != GSS_S_COMPLETE)
        {
            result = false;
        }
    }

fail:
    if (major_status != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(
                stderr,
                major_status,
                minor_status);
    }

    if (init_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
                &ignore_minor_status,
                &init_context,
                NULL);
    }
    if (accept_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
                &ignore_minor_status,
                &accept_context,
                NULL);
    }
    if (peer_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &ignore_minor_status,
                &peer_name);
    }
    if (init_generated_token.length != 0)
    {
        gss_release_buffer(
                &ignore_minor_status,
                &init_generated_token);
    }
    if (accept_generated_token.length != 0)
    {
        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);
    }
    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &minor_status,
                &target_name);
    }
    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &minor_status,
                &target_name);
    }

    return result;
}
/* init_no_server_sni() */


/**
 * @brief Test case for SNI aware client
 * @details
 *     In this test case, establish a security context, with server ready for
 *     SNI, and the client sends a known name. The SNI callback should respond
 *     with the matching credential. The client inquires the peer name and
 *     verifies it matches the desired credential.
 */
bool
init_sni1(void)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_ctx_id_t                        init_context = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    gss_name_t                          target_name = GSS_C_NO_NAME;
    gss_name_t                          peer_name = GSS_C_NO_NAME;
    gss_buffer_desc                     init_generated_token = {0};
    gss_buffer_desc                     accept_generated_token = {0};
    bool                                result = true;
    int                                 name_equal = false;
    OM_uint32                           ignore_minor_status = 0;

    major_status = gss_set_sec_context_option(
            &minor_status,
            &accept_context,
            GSS_SNI_CREDENTIALS,
            &(gss_buffer_desc)
            {
                .value = creds,
                .length = sizeof(creds),
            });
    if (major_status != GSS_S_COMPLETE)
    {
        result = false;

        goto fail;
    }

    major_status = gss_import_name(
            &minor_status,
            &(gss_buffer_desc)
            {
                .value = "dns1.example.globus.org",
                .length = strlen("dns1.example.globus.org"),
            },
            GLOBUS_GSS_C_NT_HOST_IP,
            &target_name);

    if (major_status != GSS_S_COMPLETE)
    {
        result = false;
        goto fail;
    }
    do
    {
        major_status = gss_init_sec_context(
                &minor_status,
                GSS_C_NO_CREDENTIAL,
                &init_context,
                target_name,
                GSS_C_NO_OID,
                0,
                0,
                GSS_C_NO_CHANNEL_BINDINGS,
                &accept_generated_token,
                NULL,
                &init_generated_token,
                NULL,
                NULL);

        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);

        if (GSS_ERROR(major_status))
        {
            result = false;
            break;
        }

        if (init_generated_token.length > 0)
        {
            major_status = gss_accept_sec_context(
                    &minor_status,
                    &accept_context,
                    GSS_C_NO_CREDENTIAL,
                    &init_generated_token,
                    GSS_C_NO_CHANNEL_BINDINGS,
                    NULL,
                    NULL,
                    &accept_generated_token,
                    NULL,
                    NULL,
                    NULL);
            gss_release_buffer(
                    &ignore_minor_status,
                    &init_generated_token);

            if (GSS_ERROR(major_status))
            {
                result = false;
            }
        }
    }
    while (major_status == GSS_S_CONTINUE_NEEDED);

    if (major_status == GSS_S_COMPLETE)
    {
        major_status = gss_inquire_context(
                &minor_status,
                init_context,
                NULL,
                &peer_name,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL);
        if (major_status != GSS_S_COMPLETE)
        {
            result = false;

            goto fail;
        }

        major_status = gss_compare_name(
                &minor_status,
                target_name,
                peer_name,
                &name_equal);

        if (major_status != GSS_S_COMPLETE)
        {
            result = false;
        }
    }

fail:
    if (major_status != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(
                stderr,
                major_status,
                minor_status);
    }

    if (init_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
                &ignore_minor_status,
                &init_context,
                NULL);
    }
    if (accept_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
                &ignore_minor_status,
                &accept_context,
                NULL);
    }
    if (peer_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &ignore_minor_status,
                &peer_name);
    }
    if (init_generated_token.length != 0)
    {
        gss_release_buffer(
                &ignore_minor_status,
                &init_generated_token);
    }
    if (accept_generated_token.length != 0)
    {
        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);
    }
    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &minor_status,
                &target_name);
    }
    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &minor_status,
                &target_name);
    }

    return result;
}
/* init_sni1() */

/**
 * @brief Test case for SNI aware client
 * @details
 *     In this test case, establish a security context, with server ready for
 *     SNI, and the client sends a name that only matches the wildcard name.
 *     The SNI callback should respond with the wildcard credential. The client
 *     inquires the peer name and verifies it matches the desired credential.
 */
bool
init_sni_wildcard(void)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_ctx_id_t                        init_context = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    gss_name_t                          target_name = GSS_C_NO_NAME;
    gss_name_t                          peer_name = GSS_C_NO_NAME;
    gss_buffer_desc                     init_generated_token = {0};
    gss_buffer_desc                     accept_generated_token = {0};
    bool                                result = true;
    int                                 name_equal = false;
    OM_uint32                           ignore_minor_status = 0;

    major_status = gss_set_sec_context_option(
            &minor_status,
            &accept_context,
            GSS_SNI_CREDENTIALS,
            &(gss_buffer_desc)
            {
                .value = creds,
                .length = sizeof(creds),
            });
    if (major_status != GSS_S_COMPLETE)
    {
        result = false;

        goto fail;
    }

    major_status = gss_import_name(
            &minor_status,
            &(gss_buffer_desc)
            {
                .value = "wildcard.example.globus.org",
                .length = strlen("wildcard.example.globus.org"),
            },
            GLOBUS_GSS_C_NT_HOST_IP,
            &target_name);

    if (major_status != GSS_S_COMPLETE)
    {
        result = false;
        goto fail;
    }
    do
    {
        major_status = gss_init_sec_context(
                &minor_status,
                GSS_C_NO_CREDENTIAL,
                &init_context,
                target_name,
                GSS_C_NO_OID,
                0,
                0,
                GSS_C_NO_CHANNEL_BINDINGS,
                &accept_generated_token,
                NULL,
                &init_generated_token,
                NULL,
                NULL);

        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);

        if (GSS_ERROR(major_status))
        {
            result = false;
            break;
        }

        if (init_generated_token.length > 0)
        {
            major_status = gss_accept_sec_context(
                    &minor_status,
                    &accept_context,
                    GSS_C_NO_CREDENTIAL,
                    &init_generated_token,
                    GSS_C_NO_CHANNEL_BINDINGS,
                    NULL,
                    NULL,
                    &accept_generated_token,
                    NULL,
                    NULL,
                    NULL);
            gss_release_buffer(
                    &ignore_minor_status,
                    &init_generated_token);

            if (GSS_ERROR(major_status))
            {
                result = false;
            }
        }
    }
    while (major_status == GSS_S_CONTINUE_NEEDED);

    if (major_status == GSS_S_COMPLETE)
    {
        major_status = gss_inquire_context(
                &minor_status,
                init_context,
                NULL,
                &peer_name,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL);
        if (major_status != GSS_S_COMPLETE)
        {
            result = false;

            goto fail;
        }

        major_status = gss_compare_name(
                &minor_status,
                target_name,
                peer_name,
                &name_equal);

        if (major_status != GSS_S_COMPLETE)
        {
            result = false;
        }
    }

fail:
    if (major_status != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(
                stderr,
                major_status,
                minor_status);
    }

    if (init_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
                &ignore_minor_status,
                &init_context,
                NULL);
    }
    if (accept_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
                &ignore_minor_status,
                &accept_context,
                NULL);
    }
    if (peer_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &ignore_minor_status,
                &peer_name);
    }
    if (init_generated_token.length != 0)
    {
        gss_release_buffer(
                &ignore_minor_status,
                &init_generated_token);
    }
    if (accept_generated_token.length != 0)
    {
        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);
    }
    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &minor_status,
                &target_name);
    }
    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &minor_status,
                &target_name);
    }

    return result;
}
/* init_sni_wildcard() */

/**
 * @brief Test case for SNI aware client
 * @details
 *     In this test case, establish a security context, with server ready for
 *     SNI, and the client sends a name that only matches the wildcard name.
 *     The SNI callback should respond with the wildcard credential. The server
 *     inquires context to discover which name the client provided.
 */
bool
init_sni_inquire_servername(void)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_ctx_id_t                        init_context = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    gss_name_t                          target_name = GSS_C_NO_NAME;
    gss_name_t                          peer_name = GSS_C_NO_NAME;
    gss_buffer_desc                     init_generated_token = {0};
    gss_buffer_desc                     accept_generated_token = {0};
    bool                                result = true;
    OM_uint32                           ignore_minor_status = 0;
    char                                wildcard_name[] = "wildcard.example.globus.org";
    gss_buffer_set_t                    data_set = NULL;


    major_status = gss_set_sec_context_option(
            &minor_status,
            &accept_context,
            GSS_SNI_CREDENTIALS,
            &(gss_buffer_desc)
            {
                .value = creds,
                .length = sizeof(creds),
            });
    if (major_status != GSS_S_COMPLETE)
    {
        result = false;

        goto fail;
    }

    major_status = gss_import_name(
            &minor_status,
            &(gss_buffer_desc)
            {
                .value = wildcard_name,
                .length = strlen(wildcard_name),
            },
            GLOBUS_GSS_C_NT_HOST_IP,
            &target_name);

    if (major_status != GSS_S_COMPLETE)
    {
        result = false;
        goto fail;
    }
    do
    {
        major_status = gss_init_sec_context(
                &minor_status,
                GSS_C_NO_CREDENTIAL,
                &init_context,
                target_name,
                GSS_C_NO_OID,
                0,
                0,
                GSS_C_NO_CHANNEL_BINDINGS,
                &accept_generated_token,
                NULL,
                &init_generated_token,
                NULL,
                NULL);

        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);

        if (GSS_ERROR(major_status))
        {
            result = false;
            break;
        }

        if (init_generated_token.length > 0)
        {
            major_status = gss_accept_sec_context(
                    &minor_status,
                    &accept_context,
                    GSS_C_NO_CREDENTIAL,
                    &init_generated_token,
                    GSS_C_NO_CHANNEL_BINDINGS,
                    NULL,
                    NULL,
                    &accept_generated_token,
                    NULL,
                    NULL,
                    NULL);
            gss_release_buffer(
                    &ignore_minor_status,
                    &init_generated_token);

            if (GSS_ERROR(major_status))
            {
                result = false;
            }
        }
    }
    while (major_status == GSS_S_CONTINUE_NEEDED);

    if (major_status == GSS_S_COMPLETE)
    {
        major_status = gss_inquire_sec_context_by_oid(
                &minor_status,
                accept_context,
                &gss_ext_server_name_oid_desc,
                &data_set);
        if (major_status != GSS_S_COMPLETE)
        {
            result = false;

            goto fail;
        }

        if (data_set->count != 1)
        {
            result = false;
            goto fail;
        }

        if (strncmp(data_set->elements[0].value,
                    wildcard_name,
                    data_set->elements[0].length) != 0)
        {
            result = false;
            goto fail;
        }

    }

fail:
    if (major_status != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(
                stderr,
                major_status,
                minor_status);
    }

    if (init_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
                &ignore_minor_status,
                &init_context,
                NULL);
    }
    if (accept_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
                &ignore_minor_status,
                &accept_context,
                NULL);
    }
    if (peer_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &ignore_minor_status,
                &peer_name);
    }
    if (init_generated_token.length != 0)
    {
        gss_release_buffer(
                &ignore_minor_status,
                &init_generated_token);
    }
    if (accept_generated_token.length != 0)
    {
        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);
    }
    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &minor_status,
                &target_name);
    }
    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &minor_status,
                &target_name);
    }

    if (data_set != NULL)
    {
        gss_release_buffer_set(
                &minor_status,
                &data_set);
    }

    return result;
}
/* init_sni_inquire_servername */


/**
 * @brief Test case for SNI aware client
 * @details
 *     In this test case, establish a security context, with server ready for
 *     SNI, and the client sends a name that doesn't match any known
 *     credentials and so the SNI callback fails, causing the context to
 *     fail establishment.
 */
bool
init_sni_fail(void)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_ctx_id_t                        init_context = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    gss_name_t                          target_name = GSS_C_NO_NAME;
    gss_name_t                          peer_name = GSS_C_NO_NAME;
    gss_buffer_desc                     init_generated_token = {0};
    gss_buffer_desc                     accept_generated_token = {0};
    bool                                result = false;
    OM_uint32                           ignore_minor_status = 0;

    major_status = gss_set_sec_context_option(
            &minor_status,
            &accept_context,
            GSS_SNI_CREDENTIALS,
            &(gss_buffer_desc)
            {
                .value = creds,
                .length = sizeof(creds),
            });
    if (major_status != GSS_S_COMPLETE)
    {
        result = false;

        goto fail;
    }

    major_status = gss_import_name(
            &minor_status,
            &(gss_buffer_desc)
            {
                .value = "not-at-example.globus.org",
                .length = strlen("not-at-example.globus.org"),
            },
            GLOBUS_GSS_C_NT_HOST_IP,
            &target_name);

    if (major_status != GSS_S_COMPLETE)
    {
        result = false;
        goto fail;
    }
    do
    {
        major_status = gss_init_sec_context(
                &minor_status,
                GSS_C_NO_CREDENTIAL,
                &init_context,
                target_name,
                GSS_C_NO_OID,
                0,
                0,
                GSS_C_NO_CHANNEL_BINDINGS,
                &accept_generated_token,
                NULL,
                &init_generated_token,
                NULL,
                NULL);

        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);

        if (GSS_ERROR(major_status))
        {
            result = true;
            break;
        }

        if (init_generated_token.length > 0)
        {
            major_status = gss_accept_sec_context(
                    &minor_status,
                    &accept_context,
                    GSS_C_NO_CREDENTIAL,
                    &init_generated_token,
                    GSS_C_NO_CHANNEL_BINDINGS,
                    NULL,
                    NULL,
                    &accept_generated_token,
                    NULL,
                    NULL,
                    NULL);
            gss_release_buffer(
                    &ignore_minor_status,
                    &init_generated_token);

            if (GSS_ERROR(major_status))
            {
                result = true;
                break;
            }
        }
    }
    while (major_status == GSS_S_CONTINUE_NEEDED);

fail:
    if (major_status != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(
                stderr,
                major_status,
                minor_status);
    }

    if (init_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
                &ignore_minor_status,
                &init_context,
                NULL);
    }
    if (accept_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
                &ignore_minor_status,
                &accept_context,
                NULL);
    }
    if (peer_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &ignore_minor_status,
                &peer_name);
    }
    if (init_generated_token.length != 0)
    {
        gss_release_buffer(
                &ignore_minor_status,
                &init_generated_token);
    }
    if (accept_generated_token.length != 0)
    {
        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);
    }
    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(
                &minor_status,
                &target_name);
    }

    return result;
}
/* init_sni_fail() */

static
bool
init_environment(void)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_ctx_id_t                        init_context = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    gss_name_t                          target_name = GSS_C_NO_NAME;
    gss_name_t                          peer_name = GSS_C_NO_NAME;
    gss_buffer_desc                     init_generated_token = {0};
    gss_buffer_desc                     accept_generated_token = {0};
    bool                                result = true;
    int                                 name_equal = false;
    OM_uint32                           ignore_minor_status = 0;
    char                               *old_vhost_env = NULL;

    old_vhost_env = getenv("X509_VHOST_CRED_DIR");
    globus_libc_setenv("X509_VHOST_CRED_DIR", "vhostdir", 1);

    major_status = gss_import_name(
        &minor_status,
        &(gss_buffer_desc)
        {
            .value = "wildcard.example.globus.org",
            .length = strlen("wildcard.example.globus.org"),
        },
        GLOBUS_GSS_C_NT_HOST_IP,
        &target_name);

    if (major_status != GSS_S_COMPLETE)
    {
        result = false;
        goto fail;
    }
    do
    {
        major_status = gss_init_sec_context(
            &minor_status,
            GSS_C_NO_CREDENTIAL,
            &init_context,
            target_name,
            GSS_C_NO_OID,
            0,
            0,
            GSS_C_NO_CHANNEL_BINDINGS,
            &accept_generated_token,
            NULL,
            &init_generated_token,
            NULL,
            NULL);

        gss_release_buffer(
                &ignore_minor_status,
                &accept_generated_token);

        if (GSS_ERROR(major_status))
        {
            result = false;
            break;
        }

        if (init_generated_token.length > 0)
        {
            major_status = gss_accept_sec_context(
                &minor_status,
                &accept_context,
                GSS_C_NO_CREDENTIAL,
                &init_generated_token,
                GSS_C_NO_CHANNEL_BINDINGS,
                NULL,
                NULL,
                &accept_generated_token,
                NULL,
                NULL,
                NULL);
            gss_release_buffer(
                    &ignore_minor_status,
                    &init_generated_token);

            if (GSS_ERROR(major_status))
            {
                result = false;
            }
        }
    }
    while (major_status == GSS_S_CONTINUE_NEEDED);

    if (major_status == GSS_S_COMPLETE)
    {
        major_status = gss_inquire_context(
            &minor_status,
            init_context,
            NULL,
            &peer_name,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL);
        if (major_status != GSS_S_COMPLETE)
        {
            result = false;

            goto fail;
        }

        major_status = gss_compare_name(
            &minor_status,
            target_name,
            peer_name,
            &name_equal);

        if (major_status != GSS_S_COMPLETE)
        {
            result = false;
        }
    }

fail:
    if (major_status != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(
                stderr,
                major_status,
                minor_status);
    }

    if (init_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
            &ignore_minor_status,
            &init_context,
            NULL);
    }
    if (accept_context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(
            &ignore_minor_status,
            &accept_context,
            NULL);
    }
    if (peer_name != GSS_C_NO_NAME)
    {
        gss_release_name(
            &ignore_minor_status,
            &peer_name);
    }
    if (init_generated_token.length != 0)
    {
        gss_release_buffer(
            &ignore_minor_status,
            &init_generated_token);
    }
    if (accept_generated_token.length != 0)
    {
        gss_release_buffer(
            &ignore_minor_status,
            &accept_generated_token);
    }
    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(
            &minor_status,
            &target_name);
    }
    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(
            &minor_status,
            &target_name);
    }

    if (old_vhost_env == NULL)
    {
        globus_libc_unsetenv("X509_VHOST_CRED_DIR");
    }
    else
    {
        globus_libc_setenv("X509_VHOST_CRED_DIR", old_vhost_env, 1);
    }
    return result;
}

#define TEST_CASE_INITIALIZER(x) {x,#x}
int
main(int argc, char *argv[])
{
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    int                                 failed = 0;
    struct test_case                    test_cases[] =
    {
        TEST_CASE_INITIALIZER(init_no_client_sni),
        TEST_CASE_INITIALIZER(init_no_server_sni),
        TEST_CASE_INITIALIZER(init_sni1),
        TEST_CASE_INITIALIZER(init_sni_wildcard),
        TEST_CASE_INITIALIZER(init_sni_inquire_servername), 
        TEST_CASE_INITIALIZER(init_sni_fail),
        TEST_CASE_INITIALIZER(init_environment),
    };
    char                               *default_cert = getenv("X509_USER_CERT");
    char                               *default_key = getenv("X509_USER_KEY");

    for (size_t i = 0; i < NUM_TEST_CREDS; i++)
    {
        char certname[strlen("X509_USER_CERT") + 2];
        char keyname[strlen("X509_USER_KEY") + 2];
        char *certfile = NULL;
        char *keyfile = NULL;

        if (i > 0)
        {
            snprintf(certname, sizeof(certname), "X509_USER_CERT%zu", i);
            snprintf(keyname, sizeof(keyname), "X509_USER_KEY%zu", i);
            certfile = getenv(certname);
            keyfile = getenv(keyname);
            globus_libc_setenv("X509_USER_CERT", certfile, 1);
            globus_libc_setenv("X509_USER_KEY", keyfile, 1);
        }
        else
        {
            certfile = getenv("X509_USER_CERT");
            keyfile = getenv("X509_USER_KEY");
        }

        major_status = gss_acquire_cred(
                &minor_status,
                GSS_C_NO_NAME,
                0,
                GSS_C_NO_OID_SET,
                GSS_C_BOTH,
                &creds[i],
                NULL,
                NULL);
        if (major_status != GSS_S_COMPLETE)
        {
            fprintf(stderr, "FATAL: Unable to load credential %zu (%s,%s)\n", i, certfile, keyfile);
            globus_gsi_gssapi_test_print_error(
                    stderr,
                    major_status,
                    minor_status);
            exit(99);
        }

        major_status = gss_inquire_cred(
                &minor_status,
                creds[i],
                &names[i],
                NULL,
                NULL,
                NULL);
        if (major_status != GSS_S_COMPLETE)
        {
            fprintf(stderr,
                    "FATAL: Unable to determine name for credential %zu\n", i);
            exit(99);
        }
    }
    if (default_cert != NULL)
    {
        globus_libc_setenv("X509_USER_CERT", default_cert, 1);
    }
    else
    {
        globus_libc_unsetenv("X509_USER_CERT");
    }
    if (default_key != NULL)
    {
        globus_libc_setenv("X509_USER_KEY", default_key, 1);
    }
    else
    {
        globus_libc_unsetenv("X509_USER_KEY");
    }
    
    size_t num_test_cases = sizeof(test_cases)/sizeof(test_cases[0]);

    printf("1..%zu\n", num_test_cases);
    for (size_t i = 0; i < num_test_cases; i++)
    {
        bool                            ok = test_cases[i].func();

        if (!ok)
        {
            printf("not ");
            failed++;
        }
        printf("ok %zu - %s\n",
                i+1,
                test_cases[i].name);
    }
    for (size_t i = 0; i < NUM_TEST_CREDS; i++)
    {
        gss_release_cred(
                &minor_status,
                &creds[i]);
    }

    exit(failed);
}
