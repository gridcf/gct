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
 * @file init.c
 * @author Sam Lang, Sam Meder
 */
#endif

#include "globus_i_gss_assist.h"
#include "gssapi.h"

extern gss_OID gss_nt_service_name;

/**
 * @ingroup globus_gss_assist_context
 * Initialize a GSSAPI security connection. Used by the client.
 * The context_handle is returned, and there is one for each
 * connection.  This routine will take cake of the looping
 * and token processing, using the supplied get_token and
 * send_token routines.
 *
 * @param minor_status
 *        GSSAPI return code.  The new minor_status is a globus_result_t
 *        cast to an OM_uint32.  If the call was successful, the minor
 *        status is equivalent to GLOBUS_SUCCESS.  Otherwise, it is a
 *        globus error object ID that can be passed to globus_error_get
 *        to get the error object.  The error object needs to be freed
 *        with globus_object_free.
 * @param cred_handle
 *        the cred handle obtained by acquire_cred.
 * @param context_handle
 *        pointer to returned context.
 * @param target_name_char
 *        char string representation of the
 *        server to be contacted.
 * @param req_flags
 *        request flags, such as GSS_C_DELEG_FLAG for delegation
 *        and the GSS_C_MUTUAL_FLAG for mutual authentication.
 * @param ret_flags
 *        Pointer to which services are available after
 *        the connection is established. Maybe NULL if not wanted.
 *
 * The following are particular to this assist routine:
 *
 * @param token_status
 *        the assist routine's get/send token status
 * @param gss_assist_get_token
 *        function pointer for getting the token
 * @param gss_assist_get_context
 *        first argument passed to the
 *        gss_assist_get_token function
 * @param gss_assist_send_token
 *        function pointer for setting the token
 * @param gss_assist_send_context
 *        first argument passed to the
 *        gss_assist_set_token function pointer
 *
 * @return
 *        The major status
 */
OM_uint32
globus_gss_assist_init_sec_context(
    OM_uint32 *                         minor_status,
    const gss_cred_id_t                 cred_handle,
    gss_ctx_id_t *                      context_handle,
    char *                              target_name_char,
    OM_uint32                           req_flags,
    OM_uint32 *                         ret_flags,
    int *                               token_status,
    int                                 (*gss_assist_get_token)(void *, void **, size_t *),
    void *                              gss_assist_get_context,
    int                                 (*gss_assist_send_token)(void *, void *, size_t),
    void *                              gss_assist_send_context)
{
    int                                 context_established = 0;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status1 = 0;
    OM_uint32                           minor_status2 = 0;
    gss_buffer_desc                     input_token_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        input_token = &input_token_desc;
    gss_buffer_desc                     output_token_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        output_token = &output_token_desc;
    gss_name_t                          target_name = GSS_C_NO_NAME;
    gss_OID                             target_name_type = GSS_C_NO_OID;
    gss_OID                             mech_type = GSS_C_NO_OID;
    OM_uint32                           time_req = 0;
    OM_uint32                           time_rec = 0;
    gss_channel_bindings_t              input_chan_bindings =
        GSS_C_NO_CHANNEL_BINDINGS;
    gss_OID *                           actual_mech_type = NULL;
    gss_buffer_desc                     tmp_buffer_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        tmp_buffer = &tmp_buffer_desc;
    globus_result_t                     result = GLOBUS_SUCCESS;
    static char *                       _function_name_ =
        "globus_gss_assist_init_sec_context";
    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_ENTER;

    /*
     * should not set context_handle to NULL since it may have been
     * allocated by a call to set_sec_context_option
     */

    /*    *context_handle = GSS_C_NO_CONTEXT; */
    if(ret_flags)
    {
        *ret_flags = 0;
    }

    /* supply the service name to the gss-api
     * If NULL, then we want user_to_user
     * so get it from the cred
     */

    if (target_name_char)
    {
        if(!strncmp("GSI-NO-TARGET", target_name_char, 13))
        {
            target_name = GSS_C_NO_NAME;
        }
        else
        {
            tmp_buffer->value = target_name_char;
            tmp_buffer->length = strlen(target_name_char);

            /*
             * A gss_nt_service_name is of the form service@FQDN
             * At least the Globus GSSAPI, and the Kerberos GSSAPI
             * use the same form. We will check for
             * two special forms here: host@FQDN and ftp@FQDN
             * This could be another parameter to the gss_assist
             * instead.
             */

            if (strchr(target_name_char,'@') &&
                !strstr(target_name_char,"CN="))
            {
                target_name_type = gss_nt_service_name;
            }

            major_status = gss_import_name(&minor_status1,
                                           tmp_buffer,
                                           target_name_type,
                                           &target_name);
        }
    }
    else
    {
        major_status = gss_inquire_cred(&minor_status1,
                                        cred_handle,
                                        &target_name,
                                        NULL,
                                        NULL,
                                        NULL);
    }

    if (major_status == GSS_S_COMPLETE)
    {
        while (!context_established)
        {
            GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
                4, (globus_i_gsi_gss_assist_debug_fstream,
                    _GASL("req_flags: %8.8x  input_token length: %u\n"),
                    (unsigned int) req_flags,
                    input_token->length));

            major_status = gss_init_sec_context(&minor_status1,
                                                cred_handle,
                                                context_handle,
                                                target_name,
                                                mech_type,
                                                req_flags,
                                                time_req,
                                                input_chan_bindings,
                                                input_token,
                                                actual_mech_type,
                                                output_token,
                                                ret_flags,
                                                &time_rec);

            GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
                4, (globus_i_gsi_gss_assist_debug_fstream,
                    _GASL("major:%8.8x  minor:%8.8x  ret_flags: %8.8x\n "
                    "output_token length: %u  context_handle: %p\n"),
                    (unsigned int) major_status,
                    (unsigned int) minor_status1,
                    (unsigned int) ((ret_flags) ? *ret_flags : -1),
                    output_token->length,
                    *context_handle));

            if (input_token->length > 0)
            {
                free(input_token->value);
                input_token->length = 0;
            }

            if (output_token->length != 0)
            {
                if ((*token_status = gss_assist_send_token(
                    gss_assist_send_context,
                    output_token->value,
                    output_token->length)) != 0)
                {
                    major_status =
                        GSS_S_DEFECTIVE_TOKEN | GSS_S_CALL_INACCESSIBLE_WRITE;
                }

                gss_release_buffer(&minor_status2,
                                   output_token);
            }

            if (GSS_ERROR(major_status))
            {
                if (*context_handle != GSS_C_NO_CONTEXT)
                {
                    gss_delete_sec_context(&minor_status2,
                                           context_handle,
                                           GSS_C_NO_BUFFER);
                }
                break;
            }

            if (major_status & GSS_S_CONTINUE_NEEDED)
            {
                if ((*token_status = gss_assist_get_token(
                    gss_assist_get_context,
                    &input_token->value,
                    &input_token->length)) != 0)
                {
                    major_status =
                        GSS_S_DEFECTIVE_TOKEN | GSS_S_CALL_INACCESSIBLE_READ;
                    break;
                }

            }
            else
            {
                context_established = 1;
            }
        } /* end of GSS loop */
    }

    if (input_token->length > 0)
    {
        free(input_token->value); /* alloc done by g_get_token */
        input_token->value = NULL;
        input_token->length = 0;
    }

    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(&minor_status2,&target_name);
    }

    result = (globus_result_t) minor_status1;
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSS_ASSIST_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_GSS_ASSIST_ERROR_WITH_INIT);
    }
    *minor_status = (OM_uint32) result;

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_EXIT;
    return major_status;
}
/* globus_gss_assist_init_sec_context() */

/**
 * @ingroup globus_gss_assist_context
 * This is a asynchronous version of the
 * globus_gss_assist_init_sec_context() function. Instead of looping
 * itself it passes in and out the read and written buffers and
 * the calling application is responsible for doing the I/O directly.
 *
 * @param minor_status
 *        GSSAPI return code.  The new minor status is a globus_result_t
 *        cast to a OM_uint32.  If an error occurred (GSS_ERROR(major_status))
 *        the minor_status is a globus error object id.  The error object
 *        can be obtained via globus_error_get and should be destroyed
 *        with globus_object_free when no longer needed.  If no error
 *        occurred, the minor status is equal to GLOBUS_SUCCESS.
 * @param cred_handle
 *        the cred handle obtained by acquire_cred.
 * @param context_handle
 *        pointer to returned context.
 * @param target_name_char
 *        char string representation of the
 *        server to be contacted.
 * @param req_flags
 *        request flags, such as GSS_C_DELEG_FLAG for delegation
 *        and the GSS_C_MUTUAL_FLAG for mutual authentication.
 * @param ret_flags
 *        Pointer to which services are available after
 *        the connection is established. Maybe NULL if not wanted.
 * @param input_buffer
 *        pointer to a buffer received from peer. Should
 *        be NULL on first call.
 * @param input_buffer_len
 *        length of the buffer input_buffer. Should
 *        be zero on first call.
 * @param output_bufferp
 *        pointer to a pointer which will be filled in
 *        with a pointer to a allocated block of memory. If
 *        non-NULL the contents of this block should be written
 *        to the peer where they will be fed into the
 *        gss_assist_init_sec_context_async() function.
 * @param output_buffer_lenp
 *        pointer to an integer which will be filled
 *        in with the length of the allocated output buffer
 *        pointed to by *output_bufferp.
 * @return
 *        GSS_S_COMPLETE on successful completion when this function does not
 *        need to be called again.
 *
 *        GSS_S_CONTINUE_NEEDED when *output_bufferp should be sent to the
 *        peer and a new input_buffer read and this function called again.
 *
 *        Other gss errors on failure.
 */
OM_uint32
globus_gss_assist_init_sec_context_async(
    OM_uint32 *                         minor_status,
    const gss_cred_id_t                 cred_handle,
    gss_ctx_id_t *                      context_handle,
    char *                              target_name_char,
    OM_uint32                           req_flags,
    OM_uint32 *                         ret_flags,
    void *                              input_buffer,
    size_t                              input_buffer_len,
    void **                             output_bufferp,
    size_t *                            output_buffer_lenp)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status1 = 0;
    OM_uint32                           minor_status2 = 0;
    gss_buffer_desc                     input_token_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        input_token = &input_token_desc;
    gss_buffer_desc                     output_token_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        output_token = &output_token_desc;
    gss_name_t                          target_name = GSS_C_NO_NAME;
    gss_OID                             target_name_type = GSS_C_NO_OID;
    gss_OID                             mech_type = GSS_C_NO_OID;
    OM_uint32                           time_req = 0;
    OM_uint32                           time_rec = 0;
    gss_channel_bindings_t              input_chan_bindings =
        GSS_C_NO_CHANNEL_BINDINGS;
    gss_OID *                           actual_mech_type = NULL;
    gss_buffer_desc                     tmp_buffer_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        tmp_buffer = &tmp_buffer_desc;
    globus_result_t                     result = GLOBUS_SUCCESS;
    static char *                       _function_name_ =
        "globus_gss_assist_init_sec_context_async";
    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_ENTER;

    /* Set up our input token from passed buffer */
    if ((input_buffer != NULL) && (input_buffer_len != 0))
    {
        input_token_desc.length = input_buffer_len;
        input_token_desc.value = input_buffer;
    }

    /* Do initialization first time through the loop */

    /* This will not work if the context handle has been initialized
       before the first call. Don't know how to fix it since I can't
       access fields in the handle outside the GSS API. - Sam
    */
    if (*context_handle == GSS_C_NO_CONTEXT)
    {
        if(ret_flags)
        {
            *ret_flags = 0;
        }
    }

    /* supply the service name to the gss-api
     * If NULL, then we want user_to_user
     * so get it from the cred
     */

    if (target_name_char)
    {
        if(!strncmp("GSI-NO-TARGET",target_name_char,13))
        {
            target_name = GSS_C_NO_NAME;
        }
        else
        {
            tmp_buffer->value = target_name_char;
            tmp_buffer->length = strlen(target_name_char);

            /*
             * A gss_nt_service_name is of the form service@FQDN
             * At least the Globus gssapi, and the Kerberos gssapi
             * use the same form. We will check for
             * two special forms here: host@FQDN and ftp@FQDN
             * This could be another parameter to the gss_assist
             * instead.
             */

            if (strchr(target_name_char, '@') &&
                !strstr(target_name_char, "CN="))
            {
                target_name_type = gss_nt_service_name;
            }

            major_status = gss_import_name(&minor_status1,
                                           tmp_buffer,
                                           target_name_type,
                                           &target_name);
        }
    }
    else
    {

        major_status = gss_inquire_cred(&minor_status1,
                                        cred_handle,
                                        &target_name,
                                        NULL,
                                        NULL,
                                        NULL);
    }

    if (major_status == GSS_S_COMPLETE)
    {
        GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
            4, (globus_i_gsi_gss_assist_debug_fstream,
                _GASL("req_flags: %8.8x  input_token length: %u\n"),
                (unsigned int) req_flags,
                input_token->length));

        major_status = gss_init_sec_context(&minor_status1,
                                            cred_handle,
                                            context_handle,
                                            target_name,
                                            mech_type,
                                            req_flags,
                                            time_req,
                                            input_chan_bindings,
                                            input_token,
                                            actual_mech_type,
                                            output_token,
                                            ret_flags,
                                            &time_rec);
        GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
            4, (globus_i_gsi_gss_assist_debug_fstream,
                _GASL("major: %8.8x minor: %8.8x ret_flags: %8.8x\n"
                "output_token length: %u context_handle: %p\n"),
                (unsigned int) major_status,
                (unsigned int) minor_status1,
                (unsigned int) ((ret_flags) ? *ret_flags : -1),
                output_token->length,
                *context_handle));

        if (output_token->length != 0)
        {
            *output_bufferp = output_token->value;
            *output_buffer_lenp = output_token->length;
            /* These will now be freed by the caller */
        }
        else
        {
            *output_bufferp = NULL;
            *output_buffer_lenp = 0;
        }

        if (GSS_ERROR(major_status))
        {
            if (*context_handle != GSS_C_NO_CONTEXT)
                gss_delete_sec_context(&minor_status2,
                                       context_handle,
                                       GSS_C_NO_BUFFER);
        }

    }

    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(&minor_status2,&target_name);
    }


    result = (globus_result_t) minor_status1;
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_GSS_ASSIST_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_GSS_ASSIST_ERROR_WITH_INIT);
    }
    *minor_status = (OM_uint32) result;

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_EXIT;
    return major_status;
}
/* globus_gss_assist_init_sec_context_async() */
