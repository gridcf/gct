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
 * @file unwrap.c
 * @brief Unwrap Tokens
 * @author Sam Lang, Sam Meder
 */
#endif

#include "globus_i_gss_assist.h"
#include "gssapi.h"

/**
 * @brief Get Unwrap
 * @ingroup globus_gss_assist_context
 * @details
 * Gets a token using the specific tokenizing functions,
 * and performs the GSS unwrap of that token
 *
 * @see gss_unwrap
 *
 * @param minor_status
 *        GSSAPI return code, @see gss_unwrap
 * @param context_handle
 *        the context 
 * @param data
 *        pointer to be set to the unwrapped application data. This must be
 *        freed by the caller.
 * @param length
 *        pointer to be set to the length of the @a data byte array.
 * @param token_status
 *        assist routine get/send token status 
 * @param gss_assist_get_token
 *        a detokenizing routine 
 * @param gss_assist_get_context
 *        first arg for above routine
 * @param fperr
 *        error stream to print to
 * 
 * @return
 *        GSS_S_COMPLETE on success
 *        Other gss errors on failure.  
 */
OM_uint32
globus_gss_assist_get_unwrap(
    OM_uint32 *                         minor_status,
    const gss_ctx_id_t                  context_handle,
    char **			        data,
    size_t *			        length,
    int *			        token_status,
    int (*gss_assist_get_token)(void *, void **, size_t *),
    void *                              gss_assist_get_context,
    FILE *                              fperr)
{

  OM_uint32                             major_status = GSS_S_COMPLETE;
  OM_uint32                             minor_status1 = 0;
  gss_buffer_desc                       input_token_desc  = GSS_C_EMPTY_BUFFER;
  gss_buffer_t                          input_token       = &input_token_desc;
  gss_buffer_desc                       output_token_desc = GSS_C_EMPTY_BUFFER;
  gss_buffer_t                          output_token      = &output_token_desc;

  static char *                         _function_name_ =
      "globus_gss_assist_get_unwrap";
  GLOBUS_I_GSI_GSS_ASSIST_DEBUG_ENTER;

  *token_status = (*gss_assist_get_token)(gss_assist_get_context,
                                          &input_token->value,
                                          &input_token->length);

  if (*token_status == 0) {
      
      major_status = gss_unwrap(minor_status,
                                context_handle,
                                input_token,
                                output_token,
                                NULL,
                                NULL);

      GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
          3, (globus_i_gsi_gss_assist_debug_fstream,
              _GASL("unwrap: maj: %8.8x min: %8.8x inlen: %u outlen: %u\n"),
              (unsigned int) major_status, 
              (unsigned int) *minor_status, 
              input_token->length,
              output_token->length));
      
      gss_release_buffer(&minor_status1,
                         input_token);
      
      *data = output_token->value;
      *length = output_token->length;
  }
  
  if (fperr && (major_status != GSS_S_COMPLETE || *token_status != 0)) {
      globus_gss_assist_display_status(stderr,
                                       _GASL("gss_assist_get_unwrap failure:"),
                                       major_status,
                                       *minor_status,
                                       *token_status);
  }

  *data = output_token->value;
  *length = output_token->length;
  
  if (*token_status) {
      major_status = GSS_S_FAILURE;
  }

  GLOBUS_I_GSI_GSS_ASSIST_DEBUG_EXIT;
  return major_status;
}
/* globus_gss_assist_get_unwrap() */
