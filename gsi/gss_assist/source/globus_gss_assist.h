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

#ifndef _GLOBUS_GSS_ASSIST_H
#define _GLOBUS_GSS_ASSIST_H

/**
 * @file globus_gss_assist.h
 * @brief GSS Assist Header
 */
 
#include "gssapi.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus GSS Assist
 * @copydoc globus_gss_assist
 */
#endif

/**
 * @defgroup globus_gss_assist Globus GSS Assist
 * @brief Convenience Functions for GSSAPI
 *
 * @details
 * The GSS Assist code provides convenience functions
 * for using the Globus GSS-API.
 *
 * This API includes
 * - @ref globus_gss_assist_activation
 * - @ref globus_gss_assist_credential
 * - @ref globus_gss_assist_context
 * - @ref globus_gss_assist_gridmap
 * - @ref globus_gss_assist_tokens
 * - @ref globus_gss_assist_display
 * - @ref globus_gss_assist_constants
 */

/**
 * @defgroup globus_gss_assist_activation Activation
 * @ingroup globus_gss_assist
 * @brief Module Activation
 *
 * @details
 * Globus GSI GSS Assist uses standard Globus module activation and
 * deactivation.  Before any Globus GSS Assist functions are called,
 * the following function must be called:
 *
 * @code
       globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE);
   @endcode
 *
 * This function returns GLOBUS_SUCCESS if Globus GSI GSS Assist was
 * successfully initialized, and you are therefore allowed to
 * call GSS Assist functions.  Otherwise, an error code is returned,
 * and GSS Assist functions should not be subsequently called.  
 * This function may be called multiple times.
 *
 * To deactivate Globus GSS Assist, the following function must be called:
 * 
 * @code
     globus_module_deactivate(GLOBUS_GSI_GSS_ASSIST_MODULE)
  @endcode
 *
 * This function should be called once for each time Globus GSI GSS Assist
 * was activated.
 */

/**
 * Module descriptor
 * @ingroup globus_gss_assist_activation
 * @hideinitializer
 */
#define GLOBUS_GSI_GSS_ASSIST_MODULE  (&globus_i_gsi_gss_assist_module)

extern
globus_module_descriptor_t              globus_i_gsi_gss_assist_module;

#define _GASL(s) globus_common_i18n_get_string( \
		    GLOBUS_GSI_GSS_ASSIST_MODULE, \
		    s)
#include "globus_gss_assist_constants.h"


#define GLOBUS_GSS_ASSIST_TOKEN_ERR_MALLOC		1
#define GLOBUS_GSS_ASSIST_TOKEN_ERR_BAD_SIZE	        2
#define GLOBUS_GSS_ASSIST_TOKEN_EOF			3
#define GLOBUS_GSS_ASSIST_TOKEN_NOT_FOUND		4

/* for kerberos v5.1.0.5 compatibility we need this */
#ifndef GSS_C_NO_NAME
#define GSS_C_NO_NAME ((gss_name_t *) 0)
#define GSS_ASSIST_KRB5_HACK
#endif

/* for the globus_gss_assist_ex flags: */

#define GLOBUS_GSS_ASSIST_EX_SEND_WITHOUT_LENGTH  1

typedef struct globus_gss_assist_ex_st
{
	void * arg;
	int    flags;
} globus_gss_assist_ex;

/**
 * @defgroup globus_gss_assist_tokens Token Transport
 * @ingroup globus_gss_assist
 * @brief Send and Receive Security Tokens
 *
 * @details
 * The functions in this section are used to send and receive
 * GSSAPI tokens using various methods.
 * These are used by the @ref globus_gss_assist_context functions.
 */
extern int
globus_gss_assist_token_get_fd(
    void *                              arg, 
    void **                             bufp, 
    size_t *                            sizep);

extern int
globus_gss_assist_token_send_fd(
    void *                              arg, 
    void *                              buf, 
    size_t                              size);

extern int
globus_gss_assist_token_send_fd_ex(
    void *                              arg, 
    void *                              buf, 
    size_t                              size);

extern int
globus_gss_assist_token_send_fd_without_length(
    void *                              arg, 
    void *                              buf, 
    size_t                              size);

/**
 * @defgroup globus_gss_assist_credential Credential Management
 * @ingroup globus_gss_assist
 * @brief Acquire Credential
 *
 * @details
 * The functions in this section are used to acquire security
 * credentials.
 */
extern OM_uint32
globus_gss_assist_acquire_cred(
    OM_uint32 *,             /*  minor_status */
    gss_cred_usage_t,        /* cred_usage */
    gss_cred_id_t *          /* output_cred_handle */);

extern
OM_uint32
globus_gss_assist_read_vhost_cred_dir(
    OM_uint32                          *minor_status,
    const char                         *dir,
    gss_cred_id_t                     **output_credentials_array,
    size_t                             *output_credentials_array_size);

extern OM_uint32
globus_gss_assist_acquire_cred_dir(
    OM_uint32 *,             /*  minor_status */
    gss_cred_usage_t,        /* cred_usage */
    gss_cred_id_t **         /* output_cred_handle */,
    size_t);
/*
 * globus_gss_assist_acquire_cred_ext, assist with the gss_acquire_cred
 */
extern OM_uint32
globus_gss_assist_acquire_cred_ext(
    OM_uint32 *,             /* minor_status */
    char *,                  /* desired_name_char */
    OM_uint32,               /* time_req */
    const gss_OID_set,       /* desired_mechs */
    gss_cred_usage_t,        /* cred_usage */
    gss_cred_id_t *,         /* output_cred_handle */
    gss_OID_set *,           /* actual_mechs */
    OM_uint32 *              /* time_rec */);

/**
 * @defgroup globus_gss_assist_context Security Context Management
 * @ingroup globus_gss_assist
 * @brief Security Context Creation and Use
 *
 * @details
 * The functions in this section are used to create security contexts
 * and send and receive messages sent over them. They use the functions
 * provided by @ref globus_gss_assist_tokens or user-supplied functions
 * to communicate security tokens over the context, looping over continue
 * results from the GSSAPI as needed.
 */
extern OM_uint32
globus_gss_assist_accept_sec_context(
    OM_uint32 *,                        /* minor_status */
    gss_ctx_id_t *,                     /* context_handle */
    const gss_cred_id_t,                /* acceptor_cred_handle */
    char **,                            /* src_name as char ** */
    OM_uint32 *,                        /* ret_flags */
    int *,				/* user_to_user_flag */
    int *,                              /* token_status */
    gss_cred_id_t *,                    /* delegated_cred_handle */
    int (* get_token)(void *, void **, size_t *),
    void *                              get_context,
    int (* send_token)(void *, void *, size_t),
    void *                              send_context);

/*
 * globus_gss_assist_accept_sec_context_async - async version of
 * globus_gss_assist_accept_sec_context().
 */
extern OM_uint32
globus_gss_assist_accept_sec_context_async(
    OM_uint32 *			        minor_status,
    gss_ctx_id_t *			context_handle,
    const gss_cred_id_t		        cred_handle,
    char **			        src_name_char,
    OM_uint32 *			        ret_flags,
    int *				user_to_user_flag,
    void *				input_buffer,
    size_t				input_buffer_len,
    void **			        output_bufferp,
    size_t *			        output_buffer_lenp,
    gss_cred_id_t *                     delegated_cred_handle);

/*
 * globus_gss_assist_init_sec_context - takes care of looping
 * over multiple tokens using the get and send tokens
 * routines
 */
extern OM_uint32
globus_gss_assist_init_sec_context(
    OM_uint32 *,                        /* minor_status */
    const gss_cred_id_t,                /* initiator_cred_handle */
    gss_ctx_id_t *,                     /* context_handle */
    char *,                             /* target_name as char * */
    OM_uint32,                          /* req_flags */
    OM_uint32 *,                        /* ret_flags */
    int *,                              /* token_status */
    int (* get_token)(void *, void **, size_t *),
    void *                              get_arg,
    int (* send_token)(void *, void *, size_t),
    void *                              send_arg);

/*
 * globus_gss_assist_init_sec_context_async - async version of
 * globus_gss_assist_init_sec_context().
 */
extern OM_uint32
globus_gss_assist_init_sec_context_async(
    OM_uint32 *			        minor_status,
    const gss_cred_id_t		        cred_handle,
    gss_ctx_id_t *			context_handle,
    char *				target_name_char,
    OM_uint32 			        req_flags,
    OM_uint32 *			        ret_flags,
    void *				input_buffer,
    size_t				input_buffer_len,
    void **			        output_bufferp,
    size_t *			        output_buffer_lenp);

/**
 * @defgroup globus_gss_assist_display GSSAPI Result Status Strings
 * @ingroup globus_gss_assist
 * @brief Display Error Status from a GSSAPI Result
 *
 * @details
 * The functions in this section convert a GSSAPI result code into
 * a message.
 */
extern OM_uint32
globus_gss_assist_display_status(
    FILE *,                             /* where to print */
    char *,				/* comment */
    OM_uint32,                          /* major_status */
    OM_uint32,                          /* minor_status */
    int                                 /* token_status */);

extern OM_uint32
globus_gss_assist_display_status_str(
    char **,                            /* string returned with newlines */
    char *,				/* comment */
    OM_uint32,                          /* major_status */
    OM_uint32,                          /* minor_status */
    int                                 /* token_status */);

/*
 * globus_gss_assist_wrap_send - used to wrap a 
 * simple message and send it
 */
extern OM_uint32
globus_gss_assist_wrap_send(
    OM_uint32 *                         minor_status,
    const gss_ctx_id_t                  context_handle,
    char *                              data,
    size_t                              length,
    int *                               token_status,
    int (*gss_assist_send_token)(void *, void *, size_t),
    void *                              gss_assist_send_context,
    FILE *                              fperr);

/*
 * globus_gss_assist_get_unwrap - used to get and unwrap a message
 */
extern OM_uint32
globus_gss_assist_get_unwrap(
    OM_uint32 *                         minor_status,
    const gss_ctx_id_t                  context_handle,
    char **                             data,
    size_t *                            length,
    int *                               token_status,
    int (*gss_assist_get_token)(void *, void **, size_t *),
    void *                              gss_assist_get_context,
    FILE *                              fperr);

/*
 * globus_gss_assist_will_handle_restrictions - used to tell OpenSSL
 * that the application will deal with the restrictions extension
 */
OM_uint32
globus_gss_assist_will_handle_restrictions(
    OM_uint32 *                         minor_status,
    gss_ctx_id_t *                      context_handle);


/*
 * globus_gss_assist_import_sec_context - read a security context
 */
extern OM_uint32
globus_gss_assist_export_sec_context(
    OM_uint32 *                         minor_status,
    gss_ctx_id_t *                      context_handle,
    int *                               token_status,
    int                                 fdp,
    FILE *                              fperr);

/*
 * globus_gss_assist_import_sec_context - read a security context
 */
extern OM_uint32
globus_gss_assist_import_sec_context(
    OM_uint32 *                         minor_status,
    gss_ctx_id_t *                      context_handle,
    int *                               token_status,
    int                                 fdp,
    FILE *                              fperr);


globus_result_t
globus_gss_assist_authorization_host_name(
    char *                              hostname,
    gss_name_t *                        authorization_hostname);

/**
 * @defgroup globus_gss_assist_gridmap Gridmap Authorization
 * @ingroup globus_gss_assist
 * @brief Gridmap Authorization and Local User Mapping
 *
 * @details
 * Functions in this group are used to authorize a GSSAPI credential to
 * perform some action on the local machine. In addition to checking whether
 * a credential is authorized, it can also be mapped to a local user name.
 */  
extern int
globus_gss_assist_gridmap(
    char *                              globusidp, 
    char **                             useridp);

extern int
globus_gss_assist_userok(
    char *                              globusid,
    char *                              userid);

extern int
globus_gss_assist_map_local_user(
    char *                              local_user,
    char **                             globusidp);

globus_result_t
globus_gss_assist_lookup_all_globusid(
    char *                                      username,
    char **                                     dns[],
    int *                                       dn_count);

globus_result_t
globus_gss_assist_map_and_authorize(
    gss_ctx_id_t                        context,
    char *                              service,
    char *                              desired_identity,
    char *                              identity_buffer,
    unsigned int                        identity_buffer_length);

globus_result_t
globus_gss_assist_map_and_authorize_sharing(
    char *                              shared_user_certificate,
    gss_ctx_id_t                        context,
    char *                              desired_identity,
    char *                              identity_buffer,
    unsigned int                        identity_buffer_length);


/**
 * @brief Free array of distinguished names
 * @ingroup globus_gss_assist_gridmap
 * @hideinitializer
 *
 * @details
 * Free the contents of a name array created during a successful call to
 * globus_gss_assist_lookup_all_globusid()
 *
 * @param dn_a
 *     Array of names to free.
 *
 * @retval void
 */
#define GlobusGssAssistFreeDNArray(dn_a)                    \
{                                                           \
    int __ctr = 0;                                          \
    while(dn_a[__ctr] != NULL)                              \
    {                                                       \
        free(dn_a[__ctr]);                                  \
        __ctr++;                                            \
    }                                                       \
    free(dn_a);                                             \
}


#ifdef __cplusplus
}
#endif

#endif /* _GLOBUS_GSS_ASSIST_H */
