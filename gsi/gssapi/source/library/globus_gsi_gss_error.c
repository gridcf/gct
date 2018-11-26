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
 * @file globus_gsi_gss_error.c
 * @author Sam Lang, Sam Meder
 */
#endif

#include "globus_i_gsi_gss_utils.h"
#include "globus_gsi_gss_constants.h"
#include "globus_error_openssl.h"

char *
globus_l_gsi_gssapi_error_strings[GLOBUS_GSI_GSSAPI_ERROR_LAST] =
{
/* 0 */   "SSL handshake problems",
/* 1 */   "globusid not found",
/* 2 */   "getting cert subject name",
/* 3 */   "Mutual authentication failed",
/* 4 */   "internal problem with SSL BIO",
/* 5 */   "Peer is using (limited) proxy",
/* 6 */   "Failed to receive proxy request",
/* 7 */   "Bad argument",
/* 8 */   "Internal SSL problem",
/* 9 */   "Cipher not available",
/* 10 */  "Token is wrong length",
/* 11 */  "Error with gss credential handle",
/* 12 */  "Unable to marshal credential for export",
/* 13 */  "Unable to read credential for import",
/* 14 */  "Input Error",
/* 15 */  "Output Error",
/* 16 */  "Error with gss context",
/* 17 */  "Not in expected Format",
/* 18 */  "Error with GSI proxy",
/* 19 */  "Error with GSS credential",
/* 20 */  "Cannot verify message date",
/* 21 */  "Requested mechanism not supported",
/* 22 */  "Unable to add extension",
/* 23 */  "Unable to verify remote side's credentials",
/* 24 */  "Out of memory",
/* 25 */  "Bad GSS name",
/* 26 */  "Cert chain not in signing order",
/* 27 */  "Error with GSI credential",
/* 28 */  "Error with openssl",
/* 29 */  "Error with GSS token",
/* 30 */  "Error during delegation",
/* 31 */  "Error with OID",
/* 32 */  "Credential has expired",
/* 33 */  "Error with MIC (Message Integrity Check)",
/* 34 */  "Error could not encrypt message",
/* 35 */  "Error with buffer",
/* 36 */  "Error getting peer credential",
/* 37 */  "Error unknown option",
/* 38 */  "Error creating error object",
/* 39 */  "Host lookup failed",
/* 40 */  "Function not supported on the current platform",
/* 41 */  "Authorization denied"
};

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

/**
 * @defgroup globus_i_gsi_gssapi_error Internal GSS-API Error Functions
 * @brief Internal GSS-API Error Functions
 */
globus_result_t
globus_i_gsi_gssapi_openssl_error_result(
    int                                 error_type,
    const char *                        filename,
    const char *                        function_name,
    int                                 line_number,
    const char *                        short_desc,
    const char *                        long_desc)
{
    globus_object_t *                   error_object;
    globus_result_t                     result;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    error_object =
        globus_error_wrap_openssl_error(
            GLOBUS_GSI_GSSAPI_MODULE,
            error_type,
            filename,
            function_name,
            line_number,
            "%s%s%s",
            _GGSL(globus_l_gsi_gssapi_error_strings[error_type]),
            short_desc ? ": " : "",
            short_desc ? short_desc : "");
    
    if(long_desc)
    {
        globus_error_set_long_desc(error_object, long_desc);
    }

    result = globus_error_put(error_object);

    GLOBUS_I_GSI_GSSAPI_INTERNAL_DEBUG_EXIT;
    return result;
}

globus_result_t
globus_i_gsi_gssapi_error_result(
    const OM_uint32                     minor_status,
    const char *                        filename,
    const char *                        function_name,
    int                                 line_number,
    const char *                        short_desc,
    const char *                        long_desc)
{
    globus_object_t *                   error_object;
    globus_result_t                     result;


    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    error_object =
        globus_error_construct_error(
            GLOBUS_GSI_GSSAPI_MODULE,
            NULL,
            minor_status,
            filename,
            function_name,
            line_number, 
            "%s%s%s",
            globus_l_gsi_gssapi_error_strings[minor_status],
            short_desc ? ": " : "",
            short_desc ? short_desc : "");

    if(long_desc)
    {
        globus_error_set_long_desc(error_object, long_desc);
    }

    result = globus_error_put(error_object);

    GLOBUS_I_GSI_GSSAPI_INTERNAL_DEBUG_EXIT;
    return result;
}
    
globus_result_t
globus_i_gsi_gssapi_error_chain_result(
    globus_result_t                     chain_result,
    int                                 error_type,
    const char *                        filename,
    const char *                        function_name,
    int                                 line_number,
    const char *                        short_desc,
    const char *                        long_desc)
{
    globus_result_t                     result;
    globus_object_t *                   error_object;
    
    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    error_object = 
        globus_error_construct_error(
            GLOBUS_GSI_GSSAPI_MODULE,
            globus_error_get(chain_result),
            error_type,
            filename,
            function_name,
            line_number, 
            "%s%s%s",
            (error_type < 0 || error_type >= GLOBUS_GSI_GSSAPI_ERROR_LAST)
                ? _GGSL("Unknown error")
                : _GGSL(globus_l_gsi_gssapi_error_strings[error_type]),
            short_desc ? ": " : "",
            short_desc ? short_desc : "");
        
    if(long_desc)
    {    
        globus_error_set_long_desc(error_object, long_desc);
    }

    result = globus_error_put(error_object);

    GLOBUS_I_GSI_GSSAPI_INTERNAL_DEBUG_EXIT;
    return result;
}

globus_result_t
globus_i_gsi_gssapi_error_join_chains_result(
    globus_result_t                     outer_error,
    globus_result_t                     inner_error)
{
    globus_result_t                     result;
    globus_object_t *                   result_error_obj = NULL;
    globus_object_t *                   outer_error_obj = NULL;
    globus_object_t *                   inner_error_obj = NULL;
    globus_object_t *                   temp_error_obj = NULL;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    outer_error_obj = globus_error_get(outer_error);
    inner_error_obj = globus_error_get(inner_error);
    if(outer_error_obj && inner_error_obj)
    {
        temp_error_obj = outer_error_obj;
        while(globus_error_get_cause(temp_error_obj))
        {
            temp_error_obj = globus_error_get_cause(temp_error_obj);
        }

        /*
         * Ignore failure of assigning inner_error as the root cause of
         * outer_error.  This can only fail if the outer_error is not a dynamic
         * error object
         */
        (void) globus_error_initialize_base(
                temp_error_obj,
                globus_error_get_source(temp_error_obj),
                inner_error_obj);
        result_error_obj = outer_error_obj;
    }
    else if(inner_error_obj)
    {
        result_error_obj = inner_error_obj;
    }
    else
    {
        result_error_obj = 
            globus_error_construct_error(
                GLOBUS_GSI_GSSAPI_MODULE,
                NULL,
                GLOBUS_GSI_GSSAPI_ERROR_CREATING_ERROR_OBJ,
                __FILE__,
                __func__,
                __LINE__, 
                "Couldn't join inner and outer error chains");
    }

    result = globus_error_put(result_error_obj);

    GLOBUS_I_GSI_GSSAPI_INTERNAL_DEBUG_EXIT;
    return result;
}

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
