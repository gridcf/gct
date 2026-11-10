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
 * @file export_sec_context.c
 * @author Sam Lang, Sam Meder
 */
#endif

#include "gssapi.h"
#include "globus_i_gss_assist.h"

#include <stdio.h>

/**
 * Export the security context from a file
 * @ingroup globus_gss_assist_context
 *
 * @param minor_status 
 *        GSSAPI return code.  This is a Globus Error code (or GLOBUS_SUCCESS)
 *        cast to a OM_uint32 pointer.  If an error has occurred, the resulting
 *        error (from calling globus_error_get on this variable) needs to
 *        be freed by the caller
 * @param context_handle
 *        The context to export
 * @param token_status
 *        Errors that occurred while reading from the file
 * @param fdp
 *        the file descriptor pointing to a file containing the security
 *        context
 * @param fperr
 *        FILE * to write error messages
 * 
 * @return
 *        the major status
 */
OM_uint32
globus_gss_assist_export_sec_context(
    OM_uint32 *                         minor_status,
    gss_ctx_id_t * 	                context_handle,
    int *			        token_status,
    int  				fdp,
    FILE *				fperr)
{
    globus_result_t                     local_result;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           local_minor_status = 0;
    gss_buffer_desc                     export_token = GSS_C_EMPTY_BUFFER;
    unsigned  char                      int_buf[4];
    int                                 fd = -1;
    char *                              context_fd_char; 
    static char *                       _function_name_ =
        "globus_gss_assist_export_sec_context";
    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_ENTER;

    *minor_status = 0;
    *token_status = 0;
    
    if (fdp < 0)
    {
        if ((context_fd_char = getenv("GRID_SECURITY_CONTEXT_FD"))
            == NULL)
        {
            *token_status = GLOBUS_GSS_ASSIST_TOKEN_NOT_FOUND;
            goto err;
        }
        if ((fd = atoi(context_fd_char)) <= 0)
        {
            *token_status = GLOBUS_GSS_ASSIST_TOKEN_NOT_FOUND;
            goto err;
        }
    }
    else
    {
        fd = fdp;
    }

    major_status = gss_export_sec_context(
        minor_status,
        context_handle,
        (gss_buffer_t) & export_token);
    
    int_buf[0] = (unsigned char)(((export_token.length)>>24)&0xff);
    int_buf[1] = (unsigned char)(((export_token.length)>>16)&0xff);
    int_buf[2] = (unsigned char)(((export_token.length)>> 8)&0xff);
    int_buf[3] = (unsigned char)(((export_token.length)    )&0xff);

    if(write(fd, int_buf, 4) != 4)
    {
        *token_status = GLOBUS_GSS_ASSIST_TOKEN_ERR_BAD_SIZE;
        GLOBUS_GSI_GSS_ASSIST_ERROR_RESULT(
            local_result,
            GLOBUS_GSI_GSS_ASSIST_ERROR_EXPORTING_CONTEXT,
            (_GASL("Error attempting to write 4 bytes to file descriptor")));
        *minor_status = (OM_uint32) local_result;
        major_status = GSS_S_FAILURE;
        goto err;
    }

    if(write(fd, export_token.value, export_token.length) 
       != export_token.length)
    {
        *token_status = GLOBUS_GSS_ASSIST_TOKEN_ERR_BAD_SIZE;
        GLOBUS_GSI_GSS_ASSIST_ERROR_RESULT(
            local_result,
            GLOBUS_GSI_GSS_ASSIST_ERROR_EXPORTING_CONTEXT,
            (_GASL("Error attempting to write %d bytes of export token "
             "to file descriptor."), export_token.length));
        *minor_status = (OM_uint32) local_result;
        major_status = GSS_S_FAILURE;
        goto err;
    }

    major_status = gss_release_buffer(&local_minor_status, & export_token);
    if(major_status != GSS_S_COMPLETE)
    {
        local_result = (globus_result_t) local_minor_status;
        GLOBUS_GSI_GSS_ASSIST_ERROR_CHAIN_RESULT(
            local_result,
            GLOBUS_GSI_GSS_ASSIST_ERROR_EXPORTING_CONTEXT);
        *minor_status = (OM_uint32) local_result;
        goto err;
    }

 err:
    if (fdp < 0 && fd >= 0)
    {
        (void) close(fd);
    }

    gss_release_buffer(&local_minor_status,
                       &export_token);

    if(major_status != GSS_S_COMPLETE)
    {
        globus_object_t *               error_obj;
        globus_object_t *               error_copy;

        error_obj = globus_error_get((globus_result_t) *minor_status);
        error_copy = globus_object_copy(error_obj);
        *minor_status = (OM_uint32) globus_error_put(error_obj);

        if(fperr)
        {
            globus_gss_assist_display_status(
                fperr,
                _GASL("gss_assist_export_sec_context failure:"),
                major_status,
                *minor_status,
                *token_status);

            fprintf(fperr, _GASL("token_status%d\n"), *token_status);
        }

        *minor_status = (OM_uint32) globus_error_put(error_copy);
    }

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_EXIT;
    return major_status;
}
/* globus_gss_assist_export_sec_context() */
