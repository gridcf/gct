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

#include "gssapi.h"
#include "globus_gsi_authz.h"
#include <sys/types.h>
#include "globus_preload.h"

static void
authtest_l_handle_init_callback(void *				cb_arg,
				globus_gsi_authz_handle_t 	handle,
				globus_result_t		result);

static void
authtest_l_authorize_callback(void *				cb_arg,
			      globus_gsi_authz_handle_t 	handle,
			      globus_result_t			result);

static void
authtest_l_authz_handle_destroy_callback(void *				cb_arg,
				   globus_gsi_authz_handle_t 	handle,
				   globus_result_t		result);

static void
authtest_l_authz_get_authz_id_callback(void *				cb_arg,
				       globus_gsi_authz_handle_t 	handle,
				       globus_result_t		result);

int result_count=1;
#define check_result(pred, str) \
    do { \
        int __ok = (pred); \
        if (!__ok) { \
            fail_count++; \
        } \
        printf("%s %d - %s\n", \
                (__ok) ? "ok" : "not ok", result_count++, str); \
        ok = -1; \
    } while(0)

int
main(int argc, char **argv)
{
    gss_cred_id_t                       credential;
    gss_buffer_desc                     init_token = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc                     accept_token = GSS_C_EMPTY_BUFFER;
    OM_uint32                           maj_stat, min_stat;
    gss_ctx_id_t                        init_ctx = GSS_C_NO_CONTEXT;
    gss_ctx_id_t                        accept_ctx = GSS_C_NO_CONTEXT;
    globus_result_t                     result;
    globus_gsi_authz_handle_t           authz_handle;
    char *                              identity;
    int                                 ok = -1;
    int                                 fail_count = 0;
    OM_uint32                           d_maj, d_min, message_context;

    LTDL_SET_PRELOADED_SYMBOLS();
    printf("1..11\n");

    /* module activation */
    globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE);
    globus_module_activate(GLOBUS_COMMON_MODULE);
    globus_module_activate(GLOBUS_GSI_AUTHZ_MODULE);
    
    /* acquire credentials */
    maj_stat = gss_acquire_cred(
        &min_stat,
        GSS_C_NO_NAME,
        GSS_C_INDEFINITE,
        GSS_C_NO_OID_SET,
        GSS_C_BOTH,
        &credential,
        NULL,
        NULL);
    if (maj_stat != GSS_S_COMPLETE)
    {
        fprintf(stderr,"Unable to acquire credential\n");
        exit(EXIT_FAILURE);
    }

    do
    {
        maj_stat = gss_init_sec_context(
            &min_stat,
            credential,
            &init_ctx,
            GSS_C_NO_NAME,
            GSS_C_NO_OID,
            0,
            0,
            GSS_C_NO_CHANNEL_BINDINGS,
            &accept_token,
            NULL,
            &init_token,
            NULL,
            NULL);
        if (GSS_ERROR(maj_stat))
        {
            gss_buffer_desc status_string;

            fprintf(stderr, "# Unable to establish security context: %s\n",
                globus_error_print_friendly(globus_error_peek(min_stat)));
            do
            {
                d_maj = gss_display_status(&d_min,
                        maj_stat,
                        GSS_C_GSS_CODE,
                        GSS_C_NO_OID,
                        &message_context,
                        &status_string);
                fprintf(stderr, "# %s", (char *) status_string.value);
                gss_release_buffer(&d_min, &status_string);
            }
            while (d_maj & GSS_S_CONTINUE_NEEDED);
            exit(EXIT_FAILURE);
        }
        gss_release_buffer(&min_stat, &accept_token);
        accept_token.value = NULL;
        accept_token.length = 0;

        if (init_token.length != 0)
        {
            maj_stat = gss_accept_sec_context(
                &min_stat,
                &accept_ctx,
                credential,
                &init_token,
                GSS_C_NO_CHANNEL_BINDINGS,
                NULL,
                NULL,
                &accept_token,
                NULL,
                NULL,
                NULL);
        }
    }
    while ((maj_stat & GSS_S_CONTINUE_NEEDED) && accept_token.length != 0);

    if (GSS_ERROR(maj_stat))
    {
        fprintf(stderr, "Unable to establish security context\n");
        exit(EXIT_FAILURE);
    }


    if ((result = globus_module_activate(GLOBUS_GSI_AUTHZ_MODULE))
            != GLOBUS_SUCCESS)
    {
        char * msg = globus_error_print_friendly(globus_error_peek(result));
	fprintf(stderr, "SERVER: activation of authz module failed: %s\n",
            msg);
        free(msg);
	exit(EXIT_FAILURE);
    }
  
    ok = -1;
    result = globus_gsi_authz_handle_init(&authz_handle,
					  "goodservice",
					  GSS_C_NO_CONTEXT,
					  authtest_l_handle_init_callback,
					  &ok);
    check_result(ok == GLOBUS_TRUE, "globus_gsi_authz_handle_init");
    result = globus_gsi_authz_get_authorization_identity (
        authz_handle,
        &identity,
        authtest_l_authz_get_authz_id_callback,
        &ok);
    check_result(ok == GLOBUS_FALSE && identity == NULL,
        "globus_gsi_authz_get_authorization_identity no context");
    result = globus_gsi_authz_handle_destroy(
            authz_handle,
            authtest_l_authz_handle_destroy_callback,
            &ok);
    check_result(ok == GLOBUS_TRUE, "globus_gsi_authz_handle_destroy");

    result = globus_gsi_authz_handle_init(&authz_handle,
					  "goodservice",
					  accept_ctx,
					  authtest_l_handle_init_callback,
					  &ok);
    check_result(ok == GLOBUS_TRUE, "globus_gsi_authz_handle_init");

    result = globus_gsi_authz_get_authorization_identity (
        authz_handle,
        &identity,
        authtest_l_authz_get_authz_id_callback,
        &ok);
    check_result(ok == GLOBUS_TRUE && strcmp(identity, "identity") == 0,
        "globus_gsi_authz_get_authorization_identity");
    free(identity);

    result = globus_gsi_authorize(authz_handle,
                                  "inaction",
                                  "good",
                                  authtest_l_authorize_callback, 
                                  &ok);
    check_result(ok == GLOBUS_FALSE, "globus_gsi_authorize bad action good object");

    result = globus_gsi_authorize(authz_handle,
                                  "action",
                                  "good",
                                  authtest_l_authorize_callback, 
                                  &ok);
    check_result(ok == GLOBUS_TRUE, "globus_gsi_authorize good action good object");

    result = globus_gsi_authorize(authz_handle,
                                  "inaction",
                                  "bad",
                                  authtest_l_authorize_callback, 
                                  &ok);
    check_result(ok == GLOBUS_FALSE,
        "globus_gsi_authorize bad action bad object");

    result = globus_gsi_authorize(authz_handle,
                                  "action",
                                  "bad",
                                  authtest_l_authorize_callback, 
                                  &ok);
    check_result(ok == GLOBUS_FALSE,
        "globus_gsi_authorize good action bad object");

    result = globus_gsi_authz_handle_destroy(
            authz_handle,
            authtest_l_authz_handle_destroy_callback,
            &ok);
    check_result(ok == GLOBUS_TRUE, "globus_gsi_authz_handle_destroy");

    result = globus_module_deactivate(GLOBUS_GSI_AUTHZ_MODULE);
    check_result(result == GLOBUS_SUCCESS, "globus_module_deactivate");

    /* release credentials */
    gss_release_cred(&min_stat, &credential);
    
    globus_module_deactivate(GLOBUS_COMMON_MODULE);
    globus_module_deactivate(GLOBUS_GSI_GSSAPI_MODULE);

    return fail_count;
}


static void
authtest_l_handle_init_callback(void *				cb_arg,
				globus_gsi_authz_handle_t 	handle,
				globus_result_t		result)
{
    int *okp = cb_arg;
    if (result == GLOBUS_SUCCESS)
    {
        *okp = GLOBUS_TRUE;
    }
    else
    {
        *okp = GLOBUS_FALSE;
    }
}

static void
authtest_l_authorize_callback(void *				cb_arg,
			      globus_gsi_authz_handle_t 	handle,
			      globus_result_t			result)
{
    int *okp = cb_arg;
    if (result == GLOBUS_SUCCESS)
    {
        *okp = GLOBUS_TRUE;
    }
    else
    {
        *okp = GLOBUS_FALSE;
    }
}

static void
authtest_l_authz_handle_destroy_callback(void *				cb_arg,
					 globus_gsi_authz_handle_t 	handle,
					 globus_result_t		result)
{
    int *okp = cb_arg;
    if (result == GLOBUS_SUCCESS)
    {
        *okp = GLOBUS_TRUE;
    }
    else
    {
        *okp = GLOBUS_FALSE;
    }
}

static void
authtest_l_authz_get_authz_id_callback(void *				cb_arg,
					 globus_gsi_authz_handle_t 	handle,
					 globus_result_t		result)
{
    int *okp = cb_arg;

    if (result == GLOBUS_SUCCESS)
    {
        *okp = GLOBUS_TRUE;
    }
    else
    {
        *okp = GLOBUS_FALSE;
    }
}
