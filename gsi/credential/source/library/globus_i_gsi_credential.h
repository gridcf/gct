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
 * @file globus_i_gsi_credential.h
 * @brief Globus GSI Credential Library Internals
 * @author Sam Lang, Sam Meder
 */

#ifndef GLOBUS_I_GSI_CREDENTIAL_H
#define GLOBUS_I_GSI_CREDENTIAL_H

#include "globus_gsi_credential.h"
#include "openssl/x509v3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DEBUG MACROS */
#ifdef BUILD_DEBUG

extern int                              globus_i_gsi_cred_debug_level;
extern FILE *                           globus_i_gsi_cred_debug_fstream;

#define GLOBUS_I_GSI_CRED_DEBUG(_LEVEL_) \
    (globus_i_gsi_cred_debug_level >= (_LEVEL_))

#define GLOBUS_I_GSI_CRED_DEBUG_FPRINTF(_LEVEL_, _MESSAGE_) \
    { \
        if (GLOBUS_I_GSI_CRED_DEBUG(_LEVEL_)) \
        { \
           globus_libc_fprintf _MESSAGE_; \
        } \
    }


#define GLOBUS_I_GSI_CRED_DEBUG_FNPRINTF(_LEVEL_, _MESSAGE_) \
    { \
        if (GLOBUS_I_GSI_CRED_DEBUG(_LEVEL_)) \
        { \
           char *                          _tmp_str_ = \
               globus_common_create_nstring _MESSAGE_; \
           globus_libc_fprintf(globus_i_gsi_cred_debug_fstream, \
                               _tmp_str_); \
           globus_libc_free(_tmp_str_); \
        } \
    }

#define GLOBUS_I_GSI_CRED_DEBUG_PRINT(_LEVEL_, _MESSAGE_) \
    { \
        if (GLOBUS_I_GSI_CRED_DEBUG(_LEVEL_)) \
        { \
           globus_libc_fprintf(globus_i_gsi_cred_debug_fstream, _MESSAGE_); \
        } \
    }

#define GLOBUS_I_GSI_CRED_DEBUG_PRINT_OBJECT(_LEVEL_, _OBJ_NAME_, _OBJ_) \
    { \
        if (GLOBUS_I_GSI_CRED_DEBUG(_LEVEL_)) \
        { \
           _OBJ_NAME_##_print_fp(globus_i_gsi_cred_debug_fstream, _OBJ_); \
        } \
    }

#else

#define GLOBUS_I_GSI_CRED_DEBUG_FPRINTF(_LEVEL_, _MESSAGE_) {}
#define GLOBUS_I_GSI_CRED_DEBUG_FNPRINTF(_LEVEL_, _MESSAGE_) {}
#define GLOBUS_I_GSI_CRED_DEBUG_PRINT(_LEVEL_, _MESSAGE_) {}
#define GLOBUS_I_GSI_CRED_DEBUG_PRINT_OBJECT(_LEVEL_, _OBJ_NAME_, _OBJ_) {}

#endif
         
#define GLOBUS_I_GSI_CRED_DEBUG_ENTER \
            GLOBUS_I_GSI_CRED_DEBUG_FPRINTF( \
                2, (globus_i_gsi_cred_debug_fstream, \
                    "%s entering\n", __func__))

#define GLOBUS_I_GSI_CRED_DEBUG_EXIT \
            GLOBUS_I_GSI_CRED_DEBUG_FPRINTF( \
                2, (globus_i_gsi_cred_debug_fstream, \
                    "%s exiting\n", __func__))

/* ERROR MACROS */

#define GLOBUS_GSI_CRED_OPENSSL_ERROR_RESULT(_RESULT_, _ERRORTYPE_, _ERRSTR_) \
    { \
        char *                          _tmp_str_ = \
            globus_common_create_string _ERRSTR_; \
        _RESULT_ = globus_i_gsi_cred_openssl_error_result(_ERRORTYPE_, \
                                                          __FILE__, \
                                                          __func__, \
                                                          __LINE__, \
                                                          _tmp_str_, \
                                                          NULL); \
        globus_libc_free(_tmp_str_); \
    }

#define GLOBUS_GSI_CRED_ERROR_RESULT(_RESULT_, _ERRORTYPE_, _ERRSTR_) \
    { \
        char *                          _tmp_str_ = \
            globus_common_create_string _ERRSTR_; \
        _RESULT_ = globus_i_gsi_cred_error_result(_ERRORTYPE_, \
                                                  __FILE__, \
                                                  __func__, \
                                                  __LINE__, \
                                                  _tmp_str_, \
                                                  NULL); \
        globus_libc_free(_tmp_str_); \
    }

#define GLOBUS_GSI_CRED_ERROR_CHAIN_RESULT(_TOP_RESULT_, _ERRORTYPE_) \
    _TOP_RESULT_ = globus_i_gsi_cred_error_chain_result(_TOP_RESULT_, \
                                                        _ERRORTYPE_, \
                                                        __FILE__, \
                                                        __func__, \
                                                        __LINE__, \
                                                        NULL, \
                                                        NULL)

#define GLOBUS_GSI_CRED_OPENSSL_LONG_ERROR_RESULT(_RESULT_, \
                                                  _ERRORTYPE_, \
                                                  _ERRSTR_, \
                                                  _LONG_DESC_) \
    { \
        char *                          _tmp_str_ = \
            globus_common_create_string _ERRSTR_; \
        _RESULT_ = globus_i_gsi_cred_openssl_error_result(_ERRORTYPE_, \
                                                          __FILE__, \
                                                          __func__, \
                                                          __LINE__, \
                                                          _tmp_str_, \
                                                          _LONG_DESC_); \
        globus_libc_free(_tmp_str_); \
    }

#define GLOBUS_GSI_CRED_LONG_ERROR_RESULT(_RESULT_, \
                                          _ERRORTYPE_, \
                                          _ERRSTR_) \
    { \
        char *                          _tmp_str_ = \
            globus_common_create_string _ERRSTR_; \
        _RESULT_ = globus_i_gsi_cred_error_result(_ERRORTYPE_, \
                                                  __FILE__, \
                                                  __func__, \
                                                  __LINE__, \
                                                  _tmp_str_, \
                                                  _LONG_DESC_); \
        globus_libc_free(_tmp_str_); \
    }

#define GLOBUS_GSI_CRED_LONG_ERROR_CHAIN_RESULT(_TOP_RESULT_, \
                                                _ERRORTYPE_, \
                                                _LONG_DESC_) \
    _TOP_RESULT_ = globus_i_gsi_cred_error_chain_result(_TOP_RESULT_, \
                                                        _ERRORTYPE_, \
                                                        __FILE__, \
                                                        __func__, \
                                                        __LINE__, \
                                                        NULL, \
                                                        _LONG_DESC_)

extern char *                    globus_l_gsi_cred_error_strings[];

/**
 * @brief GSI Credential handle attributes implementation
 * @ingroup globus_gsi_credential_handle
 * @internal
 *
 * This structure contains immutable attributes
 * of a credential handle
 */
typedef struct globus_l_gsi_cred_handle_attrs_s
{
    /* the order to search in for a certificate */
    globus_gsi_cred_type_t *            search_order; /*{PROXY,USER,HOST}*/
} globus_i_gsi_cred_handle_attrs_t;

/**
 * @brief GSI Credential handle implementation
 * @ingroup globus_gsi_credential_handle
 * @internal
 *
 * Contains all the state associated with a credential handle, including
 * 
 * @see globus_credential_handle_init(), globus_credential_handle_destroy()
 */
typedef struct globus_l_gsi_cred_handle_s
{
    /** The credential's signed certificate */ 
    X509 *                              cert;
    /** The private key of the credential */
    EVP_PKEY *                          key;
    /** The chain of signing certificates */
    STACK_OF(X509) *                    cert_chain;
    /** The immutable attributes of the credential handle */
    globus_gsi_cred_handle_attrs_t      attrs;
    /** The amount of time the credential is valid for */
    time_t                              goodtill;
} globus_i_gsi_cred_handle_t;


globus_result_t
globus_i_gsi_cred_goodtill(
    globus_gsi_cred_handle_t            cred_handle,
    time_t *                            goodtill);

globus_result_t globus_i_gsi_cred_get_proxycertinfo(
    X509 *                              cert,
    PROXY_CERT_INFO_EXTENSION **        proxycertinfo);

int
globus_i_gsi_cred_password_callback_no_prompt(
    char *                              buffer,
    int                                 size,
    int                                 w,
    void *                              u);

globus_result_t
globus_i_gsi_cred_openssl_error_result(
    int                                 error_type,
    const char *                        filename,
    const char *                        function_name,
    int                                 line_number,
    const char *                        short_desc,
    const char *                        long_desc);

globus_result_t
globus_i_gsi_cred_error_result(
    int                                 error_type,
    const char *                        filename,
    const char *                        function_name,
    int                                 line_number,
    const char *                        short_desc,
    const char *                        long_desc);

globus_result_t
globus_i_gsi_cred_error_chain_result(
    globus_result_t                     chain_result,
    int                                 error_type,
    const char *                        filename,
    const char *                        function_name,
    int                                 line_number,
    const char *                        short_desc,
    const char *                        long_desc);

globus_result_t
globus_i_gsi_cred_error_join_chains_result(
    globus_result_t                     outter_error,
    globus_result_t                     inner_error);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_I_GSI_CREDENTIAL_H */

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
