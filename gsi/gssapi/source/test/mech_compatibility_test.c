/*
 * Copyright 1999-2015 University of Chicago
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
#include <openssl/opensslv.h>
#include <stdbool.h>

static gss_OID_desc gss_mech_oid_globus_gssapi_openssl = 
	{9, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01"};
static gss_OID_desc gss_mech_oid_globus_gssapi_openssl_micv2 = 
	{10, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x01"};

gss_ctx_id_t                            init_ctx = GSS_C_NO_CONTEXT;
gss_ctx_id_t                            accept_ctx = GSS_C_NO_CONTEXT;
static gss_buffer_desc                  hello_buffer =
{
    .value = "hello",
    .length = 5
};

bool
mic_test_itoa(void)
{
    OM_uint32                           get_mic_major_status = GSS_S_COMPLETE;
    OM_uint32                           get_mic_minor_status = 0;
    OM_uint32                           verify_mic_major_status = GSS_S_COMPLETE;
    OM_uint32                           verify_mic_minor_status = 0;
    OM_uint32                           release_minor_status = 0;
    gss_buffer_desc                     mic_hello_buffer = { .value = NULL };
    bool                                ok = true;

    get_mic_major_status = gss_get_mic(
            &get_mic_minor_status,
            init_ctx,
            GSS_C_QOP_DEFAULT,
            &hello_buffer,
            &mic_hello_buffer);
    if (get_mic_major_status != GSS_S_COMPLETE)
    {
        ok = false;

        globus_gsi_gssapi_test_print_error(
                stderr,
                get_mic_major_status,
                get_mic_minor_status);
        
        goto end_test;
    }
    
    verify_mic_major_status = gss_verify_mic(
            &verify_mic_minor_status,
            accept_ctx,
            &hello_buffer,
            &mic_hello_buffer,
            NULL);
    if (verify_mic_major_status != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(
                stderr,
                verify_mic_major_status,
                verify_mic_minor_status);
        ok = false;
    }
    gss_release_buffer(&release_minor_status, &mic_hello_buffer);
end_test:
    return ok;
}

bool
mic_test_atoi(void)
{
    OM_uint32                           get_mic_major_status = GSS_S_COMPLETE;
    OM_uint32                           get_mic_minor_status = 0;
    OM_uint32                           verify_mic_major_status = GSS_S_COMPLETE;
    OM_uint32                           verify_mic_minor_status = 0;
    OM_uint32                           release_minor_status = 0;
    gss_buffer_desc                     mic_hello_buffer = { .value = NULL };
    bool                                ok = true;

    get_mic_major_status = gss_get_mic(
            &get_mic_minor_status,
            accept_ctx,
            GSS_C_QOP_DEFAULT,
            &hello_buffer,
            &mic_hello_buffer);
    if (get_mic_major_status != GSS_S_COMPLETE)
    {
        ok = false;

        globus_gsi_gssapi_test_print_error(
                stderr,
                get_mic_major_status,
                get_mic_minor_status);
        
        goto end_test;
    }
    
    verify_mic_major_status = gss_verify_mic(
            &verify_mic_minor_status,
            init_ctx,
            &hello_buffer,
            &mic_hello_buffer,
            NULL);
    if (verify_mic_major_status != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(
                stderr,
                verify_mic_major_status,
                verify_mic_minor_status);
        ok = false;
    }
    gss_release_buffer(&release_minor_status, &mic_hello_buffer);
end_test:
    return ok;
}

struct test_case
{
    bool                              (*func)(void);
    const char *                        name;
};

#define TEST_CASE_INITIALIZER(x) {x,#x}
int
main(int argc, char *argv[])
{
    OM_uint32                           context_major_status;
    OM_uint32                           context_minor_status;
    OM_uint32                           release_minor_status;
    int                                 failed = 0;
    struct test_case                    test_cases[] =
    {
        TEST_CASE_INITIALIZER(mic_test_itoa),
        TEST_CASE_INITIALIZER(mic_test_atoi),
    };
    size_t                              num_test_cases;
    int                                 ch = 0;
    gss_OID                             mech = &gss_mech_oid_globus_gssapi_openssl;

    while ((ch = getopt(argc, argv, "n")) != -1) {
             switch (ch) {
             case 'n':
                     mech = &gss_mech_oid_globus_gssapi_openssl_micv2;
                     break;
             case '?':
             default:
                     exit(1);
             }
     }
    argc -= optind;
    argv += optind;

#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    {
        char *                          backward_compatible_mic = NULL;
        char *                          accept_backward_compatible_mic = NULL;

        backward_compatible_mic = getenv(
                "GLOBUS_GSSAPI_BACKWARD_COMPATIBLE_MIC");

        accept_backward_compatible_mic = getenv(
                "GLOBUS_GSSAPI_ACCEPT_BACKWARD_COMPATIBLE_MIC");

        if (backward_compatible_mic != NULL
            && (strcmp(backward_compatible_mic, "true") == 0))
        {
            failed = 77;
            goto skip;
        }
        else if (accept_backward_compatible_mic != NULL
            && (strcmp(accept_backward_compatible_mic, "true") == 0))
        {
            failed = 77;
            goto skip;
        }
    }
#endif

    num_test_cases = sizeof(test_cases)/sizeof(test_cases[0]);

    failed = test_establish_contexts_with_mechs(
        &init_ctx,
        &accept_ctx,
        mech,
        0,
        &context_major_status,
        &context_minor_status);

    if (failed != 0)
    {
        printf("Bail out! couldn't establish security context\n");
        globus_gsi_gssapi_test_print_error(
                stderr, context_major_status, context_minor_status);
        failed = 99;
        goto establish_failed;
    }
    
    for (size_t i = 0; i < num_test_cases; i++)
    {
        int ok = test_cases[i].func();

        printf("test case %s returned %s\n",
            test_cases[i].name, ok ? "ok" : "not ok");

        if (!ok)
        {
            failed++;
        }
    }

establish_failed:
    if (init_ctx != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(&release_minor_status, &init_ctx, NULL);
    }
    if (accept_ctx != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(&release_minor_status, &accept_ctx, NULL);
    }
skip:
    exit(failed);
}
