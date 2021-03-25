/*
 * Copyright 1999-2017 University of Chicago
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

gss_cred_id_t                           creds;
gss_name_t                              names;

#define GSS_SNI_CREDENTIALS \
    &(gss_OID_desc) {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x03\x04"}

#define GSS_ALPN \
    &(gss_OID_desc) {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x03\x05"}

struct test_case
{
    bool                              (*func)(void);
    const char *                        name;
};

/**
 * @brief Test case for non-ALPN aware client
 * @details
 *     In this test case, establish a security context, with server ready for
 *     ALPN, but the client not providing one. The server inquires the
 *     context and sees no ALPN.
 */
bool
init_no_client_alpn(void)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_ctx_id_t                        init_context = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    gss_buffer_desc                     init_generated_token = {0};
    gss_buffer_desc                     accept_generated_token = {0};
    bool                                result = true;
    OM_uint32                           ignore_minor_status = 0;
    unsigned char                       server_alpn[] =
    {
        3, 'g', 's', 'i',
    };
    const char                         *why = "";

    major_status = gss_set_sec_context_option(
            &minor_status,
            &accept_context,
            GSS_ALPN,
            &(gss_buffer_desc)
            {
                .value = server_alpn,
                .length = sizeof(server_alpn),
            });
    if (major_status != GSS_S_COMPLETE)
    {
        why = "gss_set_sec_context_option";
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
            why = "gss_init_sec_context";
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
                why = "accept_sec_context";
                result = false;
            }
        }
    }
    while (major_status == GSS_S_CONTINUE_NEEDED);

    if (major_status == GSS_S_COMPLETE)
    {
        gss_buffer_set_desc            *data = NULL;

        major_status = gss_inquire_sec_context_by_oid(
            &minor_status,
            accept_context,
            (gss_OID_desc *) gss_ext_alpn_oid,
            &data);

        if (major_status != GSS_S_COMPLETE)
        {
            why = "inquire_context_by_oid";
            result = false;

            goto fail;
        }

        if (data->count != 0)
        {
            why = "inquire_result";
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
/* init_no_client_alpn() */

/**
 * @brief Test case for SNI aware client without server support
 * @details
 *     In this test case, establish a security context, with server not ready
 *     for alpn, and the client sends the known name. The client inquires the
 *     peer name and verifies it matches the desired credential.
 */
bool
init_no_server_alpn(void)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_ctx_id_t                        init_context = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    gss_name_t                          target_name = GSS_C_NO_NAME;
    gss_buffer_desc                     init_generated_token = {0};
    gss_buffer_desc                     accept_generated_token = {0};
    bool                                result = true;
    OM_uint32                           ignore_minor_status = 0;
    const char                         *why = "";
    unsigned char                       client_alpn[] =
    {
        3, 'g', 's', 'i',
    };

    major_status = gss_set_sec_context_option(
        &minor_status,
        &init_context,
        GSS_ALPN,
        &(gss_buffer_desc)
        {
            .value = client_alpn,
            .length = sizeof(client_alpn),
        });
    if (major_status != GSS_S_COMPLETE)
    {
        why = "gss_set_sec_context_option";
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
            why = "gss_init_sec_context";
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
                why = "accept_sec_context";
                result = false;
            }
        }
    }
    while (major_status == GSS_S_CONTINUE_NEEDED);

    if (major_status == GSS_S_COMPLETE)
    {
        gss_buffer_set_desc            *data = NULL;

        major_status = gss_inquire_sec_context_by_oid(
            &minor_status,
            init_context,
            (gss_OID_desc *) gss_ext_alpn_oid,
            &data);
        if (major_status != GSS_S_COMPLETE)
        {
            why = "inquire_context_by_oid";
            result = false;

            goto fail;
        }

        if (data->count != 0)
        {
            why = "inquire_result";
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
/* init_no_server_alpn() */


/**
 * @brief Test case for SNI aware client
 * @details
 *     In this test case, establish a security context, with server ready for
 *     ALPN, and the client sends a known protocol. The ALPN callback should
 *     respond with the matching protocol. The client and server inquire the
 *     context and verify the protocol matches the desired alpn.
 */
bool
init_alpn1(void)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_ctx_id_t                        init_context = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    gss_name_t                          target_name = GSS_C_NO_NAME;
    gss_buffer_desc                     init_generated_token = {0};
    gss_buffer_desc                     accept_generated_token = {0};
    bool                                result = true;
    OM_uint32                           ignore_minor_status = 0;
    const char                         *why = "";
    unsigned char                       server_alpn[] =
    {
        3, 'g', 's', 'i',
    };
    unsigned char                       client_alpn[] =
    {
        3, 'g', 's', 'i',
    };

    major_status = gss_set_sec_context_option(
        &minor_status,
        &accept_context,
        GSS_ALPN,
        &(gss_buffer_desc)
        {
            .value = server_alpn,
            .length = sizeof(server_alpn),
        });
    if (major_status != GSS_S_COMPLETE)
    {
        why = "gss_set_sec_context_option";
        result = false;

        goto fail;
    }
    major_status = gss_set_sec_context_option(
        &minor_status,
        &init_context,
        GSS_ALPN,
        &(gss_buffer_desc)
        {
            .value = client_alpn,
            .length = sizeof(client_alpn),
        });
    if (major_status != GSS_S_COMPLETE)
    {
        why = "gss_set_sec_context_option";
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
            why = "gss_init_sec_context";
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
                why = "accept_sec_context";
                result = false;
            }
        }
    }
    while (major_status == GSS_S_CONTINUE_NEEDED);

    if (major_status == GSS_S_COMPLETE)
    {
        gss_buffer_set_desc            *data = NULL;
        major_status = gss_inquire_sec_context_by_oid(
            &minor_status,
            init_context,
            (gss_OID_desc *) gss_ext_alpn_oid,
            &data);
        if (major_status != GSS_S_COMPLETE)
        {
            why = "inquire_context_by_oid";
            result = false;

            goto fail;
        }

        if (data->count != 1
            || data->elements[0].length != server_alpn[0]
            || memcmp(
                data->elements[0].value,
                &server_alpn[1],
                server_alpn[0]) != 0)
        {
            why = "inquire_result";
            result = false;
            goto fail;
        }
        gss_release_buffer_set(&minor_status, &data);

        major_status = gss_inquire_sec_context_by_oid(
            &minor_status,
            accept_context,
            (gss_OID_desc *) gss_ext_alpn_oid,
            &data);
        if (major_status != GSS_S_COMPLETE)
        {
            why = "inquire_context_by_oid";
            result = false;

            goto fail;
        }

        if (data->count != 1
            || data->elements[0].length != client_alpn[0]
            || memcmp(
                data->elements[0].value,
                &client_alpn[1],
                client_alpn[0]) != 0)
        {
            why = "inquire_result";
            result = false;
            goto fail;
        }
        gss_release_buffer_set(&minor_status, &data);
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
/* init_alpn1() */

/**
 * @brief Test case for ALPN aware client and server
 * @details
 *     In this test case, establish a security context, with server ready for
 *     ALPN, and the client sends the second alpn in the server's protocol list.
 */
bool
init_alpn_secondary_match(void)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_ctx_id_t                        init_context = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    gss_name_t                          target_name = GSS_C_NO_NAME;
    gss_buffer_desc                     init_generated_token = {0};
    gss_buffer_desc                     accept_generated_token = {0};
    bool                                result = true;
    OM_uint32                           ignore_minor_status = 0;
    const char                         *why = "";
    unsigned char                       server_alpn[] =
    {
        3, 'g', 's', 's',
        3, 'g', 's', 'i',
    };
    unsigned char                       client_alpn[] =
    {
        3, 'g', 's', 'i',
    };

    major_status = gss_set_sec_context_option(
        &minor_status,
        &init_context,
        GSS_ALPN,
        &(gss_buffer_desc)
        {
            .value = client_alpn,
            .length = sizeof(client_alpn),
        });
    if (major_status != GSS_S_COMPLETE)
    {
        why = "gss_set_sec_context_option";
        result = false;

        goto fail;
    }

    major_status = gss_set_sec_context_option(
        &minor_status,
        &accept_context,
        GSS_ALPN,
        &(gss_buffer_desc)
        {
            .value = server_alpn,
            .length = sizeof(server_alpn),
        });
    if (major_status != GSS_S_COMPLETE)
    {
        why = "gss_set_sec_context_option";
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
            why = "gss_init_sec_context";
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
                why = "accept_sec_context";
                result = false;
            }
        }
    }
    while (major_status == GSS_S_CONTINUE_NEEDED);

    if (major_status == GSS_S_COMPLETE)
    {
        gss_buffer_set_desc            *data = NULL;

        major_status = gss_inquire_sec_context_by_oid(
            &minor_status,
            init_context,
            (gss_OID_desc *) gss_ext_alpn_oid,
            &data);
        if (major_status != GSS_S_COMPLETE)
        {
            why = "inquire_context_by_oid";
            result = false;

            goto fail;
        }

        if (data->count != 1
            || data->elements[0].length != server_alpn[0]
            || memcmp(
                data->elements[0].value,
                &server_alpn[5],
                server_alpn[4]) != 0)
        {
            why = "inquire_result";
            result = false;
            goto fail;
        }
        gss_release_buffer_set(&minor_status, &data);

        major_status = gss_inquire_sec_context_by_oid(
            &minor_status,
            accept_context,
            (gss_OID_desc *) gss_ext_alpn_oid,
            &data);
        if (major_status != GSS_S_COMPLETE)
        {
            why = "inquire_context_by_oid";
            result = false;

            goto fail;
        }

        if (data->count != 1
            || data->elements[0].length != client_alpn[0]
            || memcmp(
                data->elements[0].value,
                &client_alpn[1],
                client_alpn[0]) != 0)
        {
            why = "inquire_result";
            result = false;
            goto fail;
        }
        gss_release_buffer_set(&minor_status, &data);
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
/* init_alpn_secondary_match() */

/**
 * @brief Test case for ALPN mismatch
 * @details
 *     In this test case, establish a security context, with server ready for
 *     ALPN, and the client sends a protocol that doesn't match any known
 *     to the server. The client can detect that alpn was not used.
 */
bool
init_alpn_nomatch(void)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_ctx_id_t                        init_context = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    gss_name_t                          target_name = GSS_C_NO_NAME;
    gss_buffer_desc                     init_generated_token = {0};
    gss_buffer_desc                     accept_generated_token = {0};
    bool                                result = false;
    OM_uint32                           ignore_minor_status = 0;
    const char                         *why = "";

    unsigned char                       server_alpn[] =
    {
        3, 'g', 's', 's',
    };
    unsigned char                       client_alpn[] =
    {
        3, 'g', 's', 'i',
    };

    major_status = gss_set_sec_context_option(
        &minor_status,
        &init_context,
        GSS_ALPN,
        &(gss_buffer_desc)
        {
            .value = client_alpn,
            .length = sizeof(client_alpn),
        });
    if (major_status != GSS_S_COMPLETE)
    {
        why = "gss_set_sec_context_option";
        result = false;

        goto fail;
    }

    major_status = gss_set_sec_context_option(
        &minor_status,
        &accept_context,
        GSS_ALPN,
        &(gss_buffer_desc)
        {
            .value = server_alpn,
            .length = sizeof(server_alpn),
        });
    if (major_status != GSS_S_COMPLETE)
    {
        why = "gss_set_sec_context_option";
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
            why = "gss_init_sec_context";
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
                why = "accept_sec_context";
                result = false;
                break;
            }
        }
    }
    while (major_status == GSS_S_CONTINUE_NEEDED);

    if (major_status == GSS_S_COMPLETE)
    {
        gss_buffer_set_desc            *data = NULL;

        major_status = gss_inquire_sec_context_by_oid(
            &minor_status,
            accept_context,
            (gss_OID_desc *) gss_ext_alpn_oid,
            &data);

        if (major_status != GSS_S_COMPLETE)
        {
            why = "inquire_context_by_oid";
            result = false;

            goto fail;
        }

        if (data->count != 0)
        {
            why = "inquire_result";
            result = false;
            goto fail;
        }
        result = true;
    }
fail:
    if (major_status != GSS_S_COMPLETE
        && strcmp(why, "accept_sec_context") == 0)
    {
        result = true;
    }
    else if (major_status != GSS_S_COMPLETE)
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
/* init_alpn_nomatch() */

#define TEST_CASE_INITIALIZER(x) {x,#x}
int
main(int argc, char *argv[])
{
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    int                                 failed = 0;
    struct test_case                    test_cases[] =
    {
        TEST_CASE_INITIALIZER(init_no_client_alpn),
        TEST_CASE_INITIALIZER(init_no_server_alpn),
        TEST_CASE_INITIALIZER(init_alpn1),
        TEST_CASE_INITIALIZER(init_alpn_secondary_match),
        TEST_CASE_INITIALIZER(init_alpn_nomatch),
    };
    char                               *default_cert = getenv("X509_USER_CERT");
    char                               *default_key = getenv("X509_USER_KEY");
    char                               *certfile = NULL;
    char                               *keyfile = NULL;
    gss_ctx_id_t                        check_context = GSS_C_NO_CONTEXT;

    certfile = getenv ("X509_USER_CERT");
    keyfile = getenv ("X509_USER_KEY");

    major_status = gss_acquire_cred(
            &minor_status,
            GSS_C_NO_NAME,
            0,
            GSS_C_NO_OID_SET,
            GSS_C_BOTH,
            &creds,
            NULL,
            NULL);
    if (major_status != GSS_S_COMPLETE)
    {
        fprintf(stderr, "FATAL: Unable to load credential (%s,%s)\n", certfile, keyfile);
        globus_gsi_gssapi_test_print_error(
                stderr,
                major_status,
                minor_status);
        exit(99);
    }

    major_status = gss_inquire_cred(
        &minor_status,
        creds,
        &names,
        NULL,
        NULL,
        NULL);
    if (major_status != GSS_S_COMPLETE)
    {
        fprintf(stderr, "FATAL: Unable to determine name for credential\n");
        exit(99);
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
    major_status = gss_set_sec_context_option(
        &minor_status,
        &check_context,
        GSS_ALPN,
        &(gss_buffer_desc)
        {
            .value = (char[]){1, 'a'},
            .length = 2,
        });

    if (major_status != GSS_S_COMPLETE)
    {
        fprintf(stderr, "ALPN not supported\n");
        exit(77);
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
    gss_release_cred(&minor_status, &creds);

    exit(failed);
}
