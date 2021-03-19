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
 * @file accept.c
 * @brief GSSAPI Accept Function Implementations
 * @author Sam Lang, Sam Meder
 */
#endif

#include "globus_i_gss_assist.h"
#include "gssapi.h"
#include <string.h>

/**
 * @brief Accept a Security Context
 * @ingroup globus_gss_assist_context
 * @details
 * This routine accepts a GSSAPI security context and
 * is called by the gram_gatekeeper. It isolates
 * the GSSAPI from the rest of the gram code.
 *
 * Initialize a GSSAPI security connection. Used by the server.
 * The context_handle is returned, and there is one for each
 * connection.  This routine will take cake of the looping
 * and token processing, using the supplied get_token and
 * send_token routines.
 *
 * @param minor_status
 *        GSSAPI return code
 * @param context_handle
 *        pointer to returned context.
 * @param cred_handle
 *        the cred handle obtained by acquire_cred.
 * @param src_name_char
 *        Pointer to char string representation of the
 *        client which contacted the server. Maybe NULL if not wanted.
 *        Should be freed when done.
 * @param ret_flags
 *        Pointer to which services are available after
 *        the connection is established. Maybe NULL if not wanted.
 *        We will also use this to pass in flags to the globus
 *        version of GSSAPI
 * @param user_to_user_flag
 *        Pointer to flag to be set if
 *        the src_name is the same as our name.
 *        (Following are particular to this assist routine)
 * @param token_status
 *        assist routine get/send token status
 * @param delegated_cred_handle
 *        pointer to be set to the credential delegated by the client if
 *        delegation occurs during the security handshake
 * @param gss_assist_get_token
 *        a get token routine
 * @param gss_assist_get_context
 *        first arg for the get token routine
 * @param gss_assist_send_token
 *        a send token routine
 * @param gss_assist_send_context
 *        first arg for the send token routine
 * @return
 *        GSS_S_COMPLETE on success
 *        Other GSSAPI errors on failure.
 */
OM_uint32
globus_gss_assist_accept_sec_context(
    OM_uint32 *                         minor_status,
    gss_ctx_id_t *                      context_handle,
    const gss_cred_id_t                 cred_handle,
    char **                             src_name_char,
    OM_uint32 *                         ret_flags,
    int *                               user_to_user_flag,
    int *                               token_status,
    gss_cred_id_t *                     delegated_cred_handle,
    int                                 (*gss_assist_get_token)(void *, void **, size_t *),
    void *                              gss_assist_get_context,
    int                                 (*gss_assist_send_token)(void *, void *, size_t),
    void *                              gss_assist_send_context)
{

    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status1 = 0;
    OM_uint32                           minor_status2 = 0;

    gss_buffer_desc                     input_token_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        input_token = &input_token_desc;
    gss_buffer_desc                     output_token_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        output_token = &output_token_desc;

    gss_channel_bindings_t              input_chan_bindings =
        GSS_C_NO_CHANNEL_BINDINGS;
    gss_name_t                          client_name = GSS_C_NO_NAME;
    gss_name_t                          my_name = GSS_C_NO_NAME;
    gss_OID                             mech_type = GSS_C_NO_OID;
    OM_uint32                           time_req;

    char *                              cp;
    gss_buffer_desc                     tmp_buffer_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        tmp_buffer = &tmp_buffer_desc;
    static char *                       _function_name_ =
        "globus_gss_assist_accept_sec_context";
    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_ENTER;

/*
 * should not set context_handle to NULL since it may have been
 * allocated by a call to set_sec_context_option
 */
/*    *context_handle = GSS_C_NO_CONTEXT; */
    *token_status = 0;

    if (src_name_char)
    {
        *src_name_char = NULL;
    }

    if (user_to_user_flag)
    {
        *user_to_user_flag = 0;
    }

    do
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

        GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
            3, (globus_i_gsi_gss_assist_debug_fstream,
                _GASL("gss_assist_accept_sec_context(1):inlen:%u\n"),
                input_token->length));

        major_status = gss_accept_sec_context(
            &minor_status1,
            context_handle,
            cred_handle,
            input_token,
            input_chan_bindings,
            &client_name,
            &mech_type,
            output_token,
            ret_flags,
            &time_req,
            delegated_cred_handle);

        GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
            3, (globus_i_gsi_gss_assist_debug_fstream,
                _GASL("gss_assist_accept_sec_context(2)"
                "maj:%8.8x:min:%8.8x:ret:%8.8x "
                "outlen:%u:context:%p\n"),
                (unsigned int) major_status,
                (unsigned int) minor_status1,
                (unsigned int) ((ret_flags) ? *ret_flags : -1),
                output_token->length,
                *context_handle));

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
                gss_delete_sec_context(&minor_status2,
                                       context_handle,
                                       GSS_C_NO_BUFFER);
            break;
        }

        if (input_token->length >0)
        {
            free(input_token->value); /* alloc done by g_get_token */
            input_token->length = 0;
        }
    }
    while (major_status & GSS_S_CONTINUE_NEEDED);

    if (input_token->length >0)
    {
        free(input_token->value); /* alloc done by g_get_token */
        input_token->length = 0;
    }

    if (major_status == GSS_S_COMPLETE)
    {
        /* caller wants the name of the client */

        if (src_name_char)
        {
            major_status = gss_display_name(&minor_status2,
                                            client_name,
                                            tmp_buffer,
                                            NULL);
            if (major_status == GSS_S_COMPLETE)
            {
                cp = (char *)malloc(tmp_buffer->length+1);
                if (cp)
                {
                    memcpy(cp, tmp_buffer->value, tmp_buffer->length);
                    cp[tmp_buffer->length] = '\0';
                    *src_name_char = cp;
                }
                else
                {
                    major_status = GSS_S_FAILURE;
                }
            }
            gss_release_buffer(&minor_status2, tmp_buffer);
        }

/* caller wants to know if the client and server are the same */

        if (user_to_user_flag)
        {
            if ((major_status = gss_inquire_cred(&minor_status1,
                                                 cred_handle,
                                                 &my_name,
                                                 NULL,
                                                 NULL,
                                                 NULL)) == GSS_S_COMPLETE)
            {
                major_status = gss_compare_name(&minor_status1,
                                                client_name,
                                                my_name,
                                                user_to_user_flag);
            }
        }
    }

    gss_release_name(&minor_status2, &client_name);
    gss_release_name(&minor_status2, &my_name);

    *minor_status = minor_status1;

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_EXIT;
    return major_status;
}
/* globus_gss_assist_accept_sec_context() */

/**
 * @brief Accept a Security Context Without Blocking
 * @ingroup globus_gss_assist_context
 * @details
 * This is an asynchronous version of the
 * globus_gss_assist_accept_sec_context() function. Instead of looping
 * itself it passes in and out the read and written buffers and
 * the calling application is responsible for doing the I/O directly.
 *
 * @param minor_status
 *        GSSAPI return code
 * @param context_handle
 *        pointer to returned context.
 * @param cred_handle
 *        the cred handle obtained by acquire_cred.
 * @param src_name_char
 *        Pointer to char string representation of the
 *        client which contacted the server. Maybe NULL if not wanted.
 *        Should be freed when done.
 * @param ret_flags
 *        Pointer to which services are available after
 *        the connection is established. Maybe NULL if not wanted.
 *        We will also use this to pass in flags to the Globus
 *        version of GSSAPI
 * @param user_to_user_flag
 *        Pointer to flag to be set if
 *        the src_name is the same as our name.
 * @param input_buffer
 *        pointer to a buffer received from peer.
 * @param input_buffer_len
 *        length of the buffer input_buffer.
 * @param output_bufferp
 *        pointer to a pointer which will be filled in
 *        with a pointer to an allocated block of memory. If
 *        non-NULL the contents of this block should be written
 *        to the peer where they will be fed into the
 *        globus_gss_assist_init_sec_context_async() function.
 * @param output_buffer_lenp
 *        pointer to an integer which will be filled
 *        in with the length of the allocated output buffer
 *        pointed to by *output_bufferp.
 * @param delegated_cred_handle
 *        pointer to be set to the credential delegated by the client if
 *        delegation occurs during the security handshake
 *
 * @return
 *        GSS_S_COMPLETE on successful completion when this function does not
 *        need to be called again.
 *
 *        GSS_S_CONTINUE_NEEDED when *output_bufferp should be sent to the
 *        peer and a new input_buffer read and this function called again.
 *
 *        Other GSSAPI errors on failure.
 */
OM_uint32
globus_gss_assist_accept_sec_context_async(
    OM_uint32 *                         minor_status,
    gss_ctx_id_t *                      context_handle,
    const gss_cred_id_t                 cred_handle,
    char **                             src_name_char,
    OM_uint32 *                         ret_flags,
    int *                               user_to_user_flag,
    void *                              input_buffer,
    size_t                              input_buffer_len,
    void **                             output_bufferp,
    size_t *                            output_buffer_lenp,
    gss_cred_id_t *                     delegated_cred_handle)
{

    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status1 = 0;
    OM_uint32                           minor_status2 = 0;
    gss_buffer_desc                     input_token_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        input_token = &input_token_desc;
    gss_buffer_desc                     output_token_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        output_token = &output_token_desc;
    gss_channel_bindings_t              input_chan_bindings
        = GSS_C_NO_CHANNEL_BINDINGS;
    gss_name_t                          client_name = GSS_C_NO_NAME;
    gss_name_t                          my_name = GSS_C_NO_NAME;
    gss_OID                             mech_type = GSS_C_NO_OID;
    OM_uint32                           time_req;
    char *                              cp;
    gss_buffer_desc                     tmp_buffer_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t                        tmp_buffer = &tmp_buffer_desc;
    static char *                       _function_name_ =
        "globus_gss_assist_accept_sec_context_async";
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
       access fields in the handle outside the GSSAPI. - Sam
    */

    if(*context_handle == GSS_C_NO_CONTEXT)
    {
        if (src_name_char)
        {
            *src_name_char = NULL;
        }

        if (user_to_user_flag)
        {
            *user_to_user_flag = -1;
        }
    }

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
        3, (globus_i_gsi_gss_assist_debug_fstream,
            _GASL("gss_assist_accept_sec_context_async(1):inlen:%u\n"),
            input_token->length));

    major_status = gss_accept_sec_context(&minor_status1,
                                          context_handle,
                                          cred_handle,
                                          input_token,
                                          input_chan_bindings,
                                          &client_name,
                                          &mech_type,
                                          output_token,
                                          ret_flags,
                                          &time_req,
                                          delegated_cred_handle);

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
        3, (globus_i_gsi_gss_assist_debug_fstream,
            _GASL("gss_assist_accept_sec_context_async(2)"
            "maj:%8.8x:min:%8.8x:ret:%8.8x outlen:%u:context:%p\n"),
            (unsigned int) major_status,
            (unsigned int) minor_status1,
            (unsigned int) ((ret_flags) ? *ret_flags : -1),
            (unsigned int) output_token->length,
            (char *) *context_handle));

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

    /*
     * Do we have the client's name?
     */
    if (!GSS_ERROR(major_status) && client_name)
    {
        OM_uint32 major_status2;

        /* Do this user want the name and we have not set it yet */
        if (src_name_char &&
            (*src_name_char == NULL))
        {
            major_status2 = gss_display_name(&minor_status2,
                                             client_name,
                                             tmp_buffer,
                                             NULL);

            if (major_status2 == GSS_S_COMPLETE)
            {

                cp = (char *)malloc(tmp_buffer->length+1);
                if (cp) {
                    memcpy(cp, tmp_buffer->value, tmp_buffer->length);
                    cp[tmp_buffer->length] = '\0';
                    *src_name_char = cp;
                } else {
                    major_status = GSS_S_FAILURE;
                }
            }
            else
            {
                /* Cause failure */
                major_status = major_status2;
            }
            gss_release_buffer(&minor_status2, tmp_buffer);
        }

        /*
         * Does the user want to know if this is user to user and
         * we have not set it yet?
         */
        if (!GSS_ERROR(major_status) &&
            user_to_user_flag &&
            (*user_to_user_flag == -1))
        {
            if ((major_status2 = gss_inquire_cred(&minor_status1,
                                                  cred_handle,
                                                  &my_name,
                                                  NULL,
                                                  NULL,
                                                  NULL)) == GSS_S_COMPLETE)
            {
                major_status2 = gss_compare_name(&minor_status1,
                                                 client_name,
                                                 my_name,
                                                 user_to_user_flag);
                {
                    OM_uint32 major_status3;
                    OM_uint32 minor_status3;

                    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
                        3, (globus_i_gsi_gss_assist_debug_fstream,
                            _GASL("gss_assist_accept_sec_context_async(3):"
                                 "u2uflag:%d\n"),
                         *user_to_user_flag));

                    major_status3 = gss_display_name(&minor_status3,
                                                     client_name,
                                                     tmp_buffer,
                                                     NULL);

                    if (GSS_ERROR(major_status3))
                    {
                        GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
                            3,
                            (globus_i_gsi_gss_assist_debug_fstream,
                             _GASL("   NO client_name: status:%8.8x %8.8x\n"),
                             (unsigned int) major_status3,
                             (unsigned int) minor_status3));
                    }
                    else
                    {
                        GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
                            3, (globus_i_gsi_gss_assist_debug_fstream,
                                _GASL("     client_name=%*s\n"),
                                tmp_buffer->length,
                                (char *) tmp_buffer->value));

                        gss_release_buffer(&minor_status2, tmp_buffer);
                    }

                    major_status3 = gss_display_name(&minor_status3,
                                                     my_name,
                                                     tmp_buffer,
                                                     NULL);

                    if (GSS_ERROR(major_status3))
                    {
                        GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
                            3, (globus_i_gsi_gss_assist_debug_fstream,
                                _GASL("   NO my_name: status:%8.8x %8.8x\n"),
                                (unsigned int) major_status3,
                                (unsigned int) minor_status3));
                    }
                    else
                    {
                        GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FNPRINTF(
                            3, (tmp_buffer->length + 14,
                                _GASL("     my_name=%*s\n"),
                                (char *) tmp_buffer->value));

                        gss_release_buffer(&minor_status2, tmp_buffer);
                    }
                }
            }

            if (GSS_ERROR(major_status2))
            {
                /* Cause failure */
                major_status = major_status2;
            }

        }
    }

    gss_release_name(&minor_status2, &client_name);
    gss_release_name(&minor_status2, &my_name);

    *minor_status = minor_status1;

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_EXIT;
    return major_status;
}
/* globus_gss_assist_accept_sec_context_async() */
