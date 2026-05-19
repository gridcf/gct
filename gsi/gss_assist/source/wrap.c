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
 * @file wrap.c
 * @brief Wrap GSSAPI buffers
 * @author Sam Lang, Sam Meder
 */
#endif

#include "globus_i_gss_assist.h"
#include "gssapi.h"

#include <stdio.h>

/**
 * @brief Wrap
 * @ingroup globus_gsi_gss_assist
 *
 * @param minor_status
 *        GSSAPI return code.  If the call was successful, the minor
 *        status is equal to GLOBUS_SUCCESS.  Otherwise, it is an
 *        error object ID for which
 *        globus_error_get() and globus_object_free()
 *        can be used to get and destroy it.
 * @param context_handle
 *        the context.
 * @param data
 *        pointer to application data to wrap and send
 * @param length
 *        length of the @a data array
 * @param token_status
 *        assist routine get/send token status
 * @param gss_assist_send_token
 *        a send_token routine
 * @param gss_assist_send_context
 *        first arg for the send_token
 * @param fperr
 *        file handle to write error message to.
 *
 * @return
 *        GSS_S_COMPLETE on success
 *        Other GSSAPI errors on failure.
 *
 * @see gss_wrap()
 */
OM_uint32
globus_gss_assist_wrap_send(
    OM_uint32 *                         minor_status,
    const gss_ctx_id_t                  context_handle,
    char *                              data,
    size_t                              length,
    int *                               token_status,
    int (*gss_assist_send_token)(void *, void *, size_t),
    void *                              gss_assist_send_context,
    FILE *                              fperr)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           local_minor_status;
    globus_result_t                     local_result = GLOBUS_SUCCESS;
    gss_buffer_desc                     input_token_desc  = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        input_token       = &input_token_desc;
    gss_buffer_desc                     output_token_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        output_token      = &output_token_desc;
    static char *                       _function_name_ =
        "globus_gss_assist_wrap_send";
    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_ENTER;

    *token_status = 0;
    input_token->value = data;
    input_token->length = length;

    major_status = gss_wrap(&local_minor_status,
                            context_handle,
                            0,
                            GSS_C_QOP_DEFAULT,
                            input_token,
                            NULL,
                            output_token);

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
        3, (globus_i_gsi_gss_assist_debug_fstream,
            _GASL("Wrap_send:maj:%8.8x min:%8.8x inlen:%u outlen:%u\n"),
            (unsigned int) major_status,
            (unsigned int) *minor_status,
            input_token->length = length,
            output_token->length));

    if (major_status != GSS_S_COMPLETE)
    {
        globus_object_t *               error_obj;
        globus_object_t *               error_copy;

        error_obj = globus_error_get((globus_result_t) local_minor_status);
        error_copy = globus_object_copy(error_obj);

        local_minor_status = (OM_uint32) globus_error_put(error_obj);
        if(fperr)
        {
            globus_gss_assist_display_status(
                stderr,
                _GASL("gss_assist_wrap_send failure:"),
                major_status,
                local_minor_status,
                *token_status);
        }

        local_result = globus_error_put(error_copy);
        GLOBUS_GSI_GSS_ASSIST_ERROR_CHAIN_RESULT(
            local_result,
            GLOBUS_GSI_GSS_ASSIST_ERROR_WITH_WRAP);
        *minor_status = (OM_uint32) local_result;
        goto release_output_token;
    }

    *token_status = (*gss_assist_send_token)(gss_assist_send_context,
                                             output_token->value,
                                             output_token->length);
    if(*token_status != 0)
    {
        GLOBUS_GSI_GSS_ASSIST_ERROR_RESULT(
            local_result,
            GLOBUS_GSI_GSS_ASSIST_ERROR_WITH_WRAP,
            (_GASL("Error sending output token. token status: %d\n"),
             *token_status));
        *minor_status = (OM_uint32) local_result;
        major_status = GSS_S_FAILURE;
        goto release_output_token;
    }

    major_status = gss_release_buffer(& local_minor_status,
                                      output_token);
    if(GSS_ERROR(major_status))
    {
        GLOBUS_GSI_GSS_ASSIST_ERROR_CHAIN_RESULT(
            local_result,
            GLOBUS_GSI_GSS_ASSIST_ERROR_WITH_WRAP);
        *minor_status = (OM_uint32) local_result;
    }

    goto exit;

 release_output_token:

    gss_release_buffer(&local_minor_status,
                       output_token);

 exit:

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_EXIT;
    return major_status;
}
/* globus_gss_assist_wrap_send() */
