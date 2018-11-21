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

#ifndef GSSAPI_H_
#define GSSAPI_H_

/*
 * Also define _GSSAPI_H_ as that is what the Kerberos 5 code defines and
 * what header files on some systems look for.
 */
#define _GSSAPI_H_

/* 
 * Define windows specific needed parameters.
 */

#ifndef GSS_CALLCONV
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__)
#define GSS_CALLCONV __stdcall
#define GSS_CALLCONV_C __cdecl
#else
#define GSS_CALLCONV
#define GSS_CALLCONV_C
#endif
#endif /* GSS_CALLCONV */

#ifdef GSS_USE_FUNCTION_POINTERS
#define GSS_FUNC(f) (*f##_type)
#define GSS_MAKE_TYPEDEF typedef
#else
#define GSS_FUNC(f) f
#define GSS_MAKE_TYPEDEF
#endif

#include "globus_common.h"


/*
 * Include stddef.h to get size_t defined.
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus GSSAPI
 * @copydoc globus_gsi_gssapi
 */
#endif

/**
 * @defgroup globus_gsi_gssapi Globus GSSAPI
 * @brief GSI Implementation Details
 *
 * @details
 * The Globus GSI GSSAPI is an implementation of <a href="http://www.ietf.org/rfc/rfc2744.txt">GSS API C Bindings</a> using OpenSSL. This API documentation
 * is intended to explain implementation-specific behavior of this GSSAPI
 * implementation, as well as GSSAPI extensions.
 *
 * The API documentation is divided into sections covering:
 * - @ref globus_gsi_gssapi
 * - @ref globus_gsi_gssapi_activation
 * - @ref globus_gsi_gssapi_constants
 * - @ref globus_gsi_gss_requested_context_flags
 * - @ref globus_gsi_gss_returned_context_flags
 * - @ref globus_gsi_gssapi_extensions
 * - @ref globus_gsi_gssapi_extensions_delegation
 */

/**
 * @defgroup globus_gsi_gssapi_activation Activation
 * @ingroup globus_gsi_gssapi
 * @brief Module Activation
 *
 * @details
 * Globus GSI GSSAPI uses standard Globus module activation and
 * deactivation. Before any Globus GSI GSSAPI functions are called,
 * the following function should be called:
 *
 * @code
        globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE)
   @endcode
 *
 *
 * This function returns GLOBUS_SUCCESS if Globus GSI GSSAPI was
 * successfully initialized, and you are therefore allowed to
 * subsequently call Globus GSI GSSAPI functions.  Otherwise, an error
 * code is returned, and Globus GSI GSSAPI functions should not
 * subsequently be called. This function may be called multiple times.
 *
 * To deactivate Globus GSI GSSAPI, the following function should be called:
 *
   @code
      globus_module_deactivate(GLOBUS_GSI_GSSAPI_MODULE)
   @endcode
 *
 * This function should be called once for each time Globus GSI GSSAPI
 * was activated.
 *
 * Note that it is not mandatory to call the above functions.
 */

/** Module descriptor
 * @ingroup globus_gsi_gssapi_activation
 * @hideinitializer
 */
#define GLOBUS_GSI_GSSAPI_MODULE (&globus_i_gsi_gssapi_module)

extern
globus_module_descriptor_t		globus_i_gsi_gssapi_module;

#define _GGSL(s) globus_common_i18n_get_string(GLOBUS_GSI_GSSAPI_MODULE,\
		                s)

/*
 * If the platform supports the xom.h header file, it should be
 * included here.
 */
/* #include <xom.h> */


/*
 * Now define the three implementation-dependent types.
 */


typedef struct gss_name_desc_struct * gss_name_t;
typedef struct gss_ctx_id_desc_struct * gss_ctx_id_t;
typedef struct gss_cred_id_desc_struct * gss_cred_id_t;

/*
 * The following type must be defined as the smallest natural
 * unsigned integer supported by the platform that has at least
 * 32 bits of precision.
 */

#ifdef OM_STRING
/*
 * We have included the xom.h header file.  Verify that OM_uint32
 * is defined correctly.
 */

#if sizeof(globus_uint) != sizeof(OM_uint32)
#error Incompatible definition of OM_uint32 from xom.h
#endif

typedef OM_object_identifier gss_OID_desc, *gss_OID;

#else /* !OM_STRING */

/*
 * We can't use X/Open definitions, so roll our own.
 */
typedef globus_uint_t OM_uint32;
typedef struct gss_OID_desc_struct
{
    OM_uint32 length;
    void *elements;
} gss_OID_desc, *gss_OID;

#endif /* !OM_STRING */

typedef struct gss_OID_set_desc_struct
{
    size_t     count;
    gss_OID    elements;
} gss_OID_set_desc, *gss_OID_set;


/*
 * For now, define a QOP-type as an OM_uint32
 */
typedef OM_uint32 gss_qop_t;

typedef int gss_cred_usage_t;


typedef struct gss_buffer_desc_struct
{
    size_t length;
    void *value;
} gss_buffer_desc, *gss_buffer_t;

typedef struct gss_channel_bindings_struct
{
    OM_uint32 initiator_addrtype;
    gss_buffer_desc initiator_address;
    OM_uint32 acceptor_addrtype;
    gss_buffer_desc acceptor_address;
    gss_buffer_desc application_data;
} *gss_channel_bindings_t;

/*
 * Flag bits for context-level services.
 */
#define GSS_C_DELEG_FLAG 1
#define GSS_C_MUTUAL_FLAG 2
#define GSS_C_REPLAY_FLAG 4
#define GSS_C_SEQUENCE_FLAG 8
#define GSS_C_CONF_FLAG 16
#define GSS_C_INTEG_FLAG 32
#define GSS_C_ANON_FLAG 64
#define GSS_C_PROT_READY_FLAG 128
#define GSS_C_TRANS_FLAG 256

/*
 * For the Globus gssapi_openssl we add three
 * flags. These may have to be changed in the
 * future, if they are defined in the RFCs.
 * GSS_C_GLOBUS_SSL_COMPATIBLE
 * input - The other is not using GSSAPI, but
 * 		 real SSL, so no delegation, etc.
 * GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG
 * input -  delegates a limited proxy
 * output - limited proxy received via delegation
 * GSS_C_GLOBUS_LIMITED_PROXY_FLAG
 * input - Dont accept limited proxy for auth
 * output- Limited proxy received.
 *
 * (Deprecated) GSS_C_GLOBUS_LIMITED_PROXY_MANY_FLAG
 * input - Accept proxies signed by limited proxies
 *
 * Since gss_accept_sec_context does not
 * provide a req_flag as input, we will
 * pass in  our GSS_C_GLOBUS_LIMITED_PROXY_FLAG
 * and GSS_C_GLOBUS_LIMITED_PROXY_MANY_FLAG
 * using the ret_flag
 */

/**
 * @defgroup globus_gsi_gss_requested_context_flags Request Flags
 * @ingroup globus_gsi_gssapi
 * @brief Request Flags
 *
 * @details
 * These macros set the REQUESTED type of context - these should
 * be set (or not) in the context's req_flags (or
 * in the context's ret_flags if accept_sec_context is
 * being called)
 */

/**
 * @brief No limited proxy flag
 * @ingroup globus_gsi_gss_requested_context_flags
 * @hideinitializer
 * @details
 * Set if you don't want a context to accept a limited proxy.
 * If this flag is set, and a limited proxy is received,
 * the call will not be successful
 * and the context will not be set up
 */
#define GSS_C_GLOBUS_DONT_ACCEPT_LIMITED_PROXY_FLAG 8192

/**
 * @brief Delegate Limited Proxy Flag
 * @ingroup globus_gsi_gss_requested_context_flags
 * @hideinitializer
 * @details
 * Set if you want the delegated proxy to be a limited proxy
 */
#define GSS_C_GLOBUS_DELEGATE_LIMITED_PROXY_FLAG    4096

/**
 * @brief Accept Proxy Signed By Limited Proxy Flag
 * @ingroup globus_gsi_gss_requested_context_flags
 * @hideinitializer
 * @details
 * Set if you want to accept proxies signed by limited proxies
 * @deprecated We now accept proxies signed by limited proxies if
 * they are limited or independent.
 */
#define GSS_C_GLOBUS_ACCEPT_PROXY_SIGNED_BY_LIMITED_PROXY_FLAG 32768

/**
 * @brief Allow Missing Signing Policy Flag
 * @ingroup globus_gsi_gss_requested_context_flags
 * @hideinitializer
 * @details
 * Set if you want to allow CA certs without a signing policy to verify.
 */
#define GSS_C_GLOBUS_ALLOW_MISSING_SIGNING_POLICY 65536

/**
 * @brief Force SSLv3 Flag
 * @ingroup globus_gsi_gss_requested_context_flags
 * @hideinitializer
 * @details
 * Set if you want to force SSLv3 instead of negotiating TLSv1 or SSLv3
 */
#define GSS_C_GLOBUS_FORCE_SSL3 131072  /* NO LONGER SUPPORTED */

/**
 * @defgroup globus_gsi_gss_returned_context_flags Return Flags
 * @ingroup globus_gsi_gssapi
 * @brief Return Flags
 *
 * @details
 * These macros set the RETURNED context type - these will be
 * be set (or not) in the context's ret_flags
 */

/**
 * @brief Received Limited Proxy Flag
 * @ingroup globus_gsi_gss_returned_context_flags
 * @hideinitializer
 * @details
 * If the proxy received is a limited proxy, this flag will be
 * set in the returned context flags (ret_flags)
 */
#define GSS_C_GLOBUS_RECEIVED_LIMITED_PROXY_FLAG    8192

/**
 * @brief Received Limited Proxy During Delegation Flag
 * @ingroup globus_gsi_gss_returned_context_flags
 * @hideinitializer
 * @details
 * If the proxy received is a limited proxy received during
 * delegation, this flag is set in the returned flags
 */
#define GSS_C_GLOBUS_RECEIVED_LIMITED_PROXY_DURING_DELEGATION_FLAG 4096

#define GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG  4096
#define GSS_C_GLOBUS_LIMITED_PROXY_FLAG        8192
#define GSS_C_GLOBUS_SSL_COMPATIBLE           16384
/**
 * @brief Limited Proxy Many Flag
 * @ingroup globus_gsi_gss_returned_context_flags
 * @hideinitializer
 * @details
 * @deprecated We now accept proxies signed by limited proxies if
 * they are limited or independent.
 */
#define GSS_C_GLOBUS_LIMITED_PROXY_MANY_FLAG  32768

/*
 * Credential usage options
 */
#define GSS_C_BOTH 0
#define GSS_C_INITIATE 1
#define GSS_C_ACCEPT 2

/*
 * Status code types for gss_display_status
 */
#define GSS_C_GSS_CODE 1
#define GSS_C_MECH_CODE 2

/*
 * The constant definitions for channel-bindings address families
 */
#define GSS_C_AF_UNSPEC     0
#define GSS_C_AF_LOCAL      1
#define GSS_C_AF_INET       2
#define GSS_C_AF_IMPLINK    3
#define GSS_C_AF_PUP        4
#define GSS_C_AF_CHAOS      5
#define GSS_C_AF_NS         6
#define GSS_C_AF_NBS        7
#define GSS_C_AF_ECMA       8
#define GSS_C_AF_DATAKIT    9
#define GSS_C_AF_CCITT      10
#define GSS_C_AF_SNA        11
#define GSS_C_AF_DECnet     12
#define GSS_C_AF_DLI        13
#define GSS_C_AF_LAT        14
#define GSS_C_AF_HYLINK     15
#define GSS_C_AF_APPLETALK  16
#define GSS_C_AF_BSC        17
#define GSS_C_AF_DSS        18
#define GSS_C_AF_OSI        19
#define GSS_C_AF_X25        21

#define GSS_C_AF_NULLADDR   255

/*
 * Various Null values
 */
#define GSS_C_NO_NAME ((gss_name_t) 0)
#define GSS_C_NO_BUFFER ((gss_buffer_t) 0)
#define GSS_C_NO_BUFFER_SET ((gss_buffer_set_t) 0)
#define GSS_C_NO_OID ((gss_OID) 0)
#define GSS_C_NO_OID_SET ((gss_OID_set) 0)
#define GSS_C_NO_CONTEXT ((gss_ctx_id_t) 0)
#define GSS_C_NO_CREDENTIAL ((gss_cred_id_t) 0)
#define GSS_C_NO_CHANNEL_BINDINGS ((gss_channel_bindings_t) 0)
#define GSS_C_EMPTY_BUFFER {0, NULL}
#define GSS_C_EMPTY_BUFFER_SET {0, (gss_buffer_t) 0}

/*
 * Some alternate names for a couple of the above
 * values.  These are defined for V1 compatibility.
 */
#define GSS_C_NULL_OID GSS_C_NO_OID
#define GSS_C_NULL_OID_SET GSS_C_NO_OID_SET

/*
 * Define the default Quality of Protection for per-message
 * services.  Note that an implementation that offers multiple
 * levels of QOP may define GSS_C_QOP_DEFAULT to be either zero
 * (as done here) to mean "default protection", or to a specific
 * explicit QOP value.  However, a value of 0 should always be
 * interpreted by a GSSAPI implementation as a request for the
 * default protection level.
 */
#define GSS_C_QOP_DEFAULT 0

/*
 * Expiration time of 2^32-1 seconds means infinite lifetime for a
 * credential or security context
 */
#define GSS_C_INDEFINITE 0xfffffffful

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *              "\x01\x02\x01\x01"},
 * corresponding to an object-identifier value of
 * {iso(1) member-body(2) United States(840) mit(113554)
 *  infosys(1) gssapi(2) generic(1) user_name(1)}.  The constant
 * GSS_C_NT_USER_NAME should be initialized to point
 * to that gss_OID_desc.
 */
extern gss_OID GSS_C_NT_USER_NAME;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *              "\x01\x02\x01\x02"},
 * corresponding to an object-identifier value of
 * {iso(1) member-body(2) United States(840) mit(113554)
 *  infosys(1) gssapi(2) generic(1) machine_uid_name(2)}.
 * The constant GSS_C_NT_MACHINE_UID_NAME should be
 * initialized to point to that gss_OID_desc.
 */
extern gss_OID GSS_C_NT_MACHINE_UID_NAME;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *              "\x01\x02\x01\x03"},
 * corresponding to an object-identifier value of
 * {iso(1) member-body(2) United States(840) mit(113554)
 *  infosys(1) gssapi(2) generic(1) string_uid_name(3)}.
 * The constant GSS_C_NT_STRING_UID_NAME should be
 * initialized to point to that gss_OID_desc.
 */
extern gss_OID GSS_C_NT_STRING_UID_NAME;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {6, (void *)"\x2b\x06\x01\x05\x06\x02"},
 * corresponding to an object-identifier value of
 * {iso(1) org(3) dod(6) internet(1) security(5)
 * nametypes(6) gss-host-based-services(2)).  The constant
 * GSS_C_NT_HOSTBASED_SERVICE_X should be initialized to point
 * to that gss_OID_desc.  This is a deprecated OID value, and
 * implementations wishing to support hostbased-service names
 * should instead use the GSS_C_NT_HOSTBASED_SERVICE OID,
 * defined below, to identify such names;
 * GSS_C_NT_HOSTBASED_SERVICE_X should be accepted a synonym
 * for GSS_C_NT_HOSTBASED_SERVICE when presented as an input
 * parameter, but should not be emitted by GSSAPI
 * implementations
 */
extern gss_OID GSS_C_NT_HOSTBASED_SERVICE_X;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *              "\x01\x02\x01\x04"}, corresponding to an
 * object-identifier value of {iso(1) member-body(2)
 * Unites States(840) mit(113554) infosys(1) gssapi(2)
 * generic(1) service_name(4)}.  The constant
 * GSS_C_NT_HOSTBASED_SERVICE should be initialized
 * to point to that gss_OID_desc.
 */
extern gss_OID GSS_C_NT_HOSTBASED_SERVICE;


/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {6, (void *)"\x2b\x06\01\x05\x06\x03"},
 * corresponding to an object identifier value of
 * {1(iso), 3(org), 6(dod), 1(internet), 5(security),
 * 6(nametypes), 3(gss-anonymous-name)}.  The constant
 * and GSS_C_NT_ANONYMOUS should be initialized to point
 * to that gss_OID_desc.
 */
extern gss_OID GSS_C_NT_ANONYMOUS;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {6, (void *)"\x2b\x06\x01\x05\x06\x04"},
 * corresponding to an object-identifier value of
 * {1(iso), 3(org), 6(dod), 1(internet), 5(security),
 * 6(nametypes), 4(gss-api-exported-name)}.  The constant
 * GSS_C_NT_EXPORT_NAME should be initialized to point
 * to that gss_OID_desc.
 */
extern gss_OID GSS_C_NT_EXPORT_NAME;

/* Major status codes */

#define GSS_S_COMPLETE 0

/*
 * Some "helper" definitions to make the status code macros obvious.
 */
#define GSS_C_CALLING_ERROR_OFFSET 24
#define GSS_C_ROUTINE_ERROR_OFFSET 16
#define GSS_C_SUPPLEMENTARY_OFFSET 0
#define GSS_C_CALLING_ERROR_MASK 0377ul
#define GSS_C_ROUTINE_ERROR_MASK 0377ul
#define GSS_C_SUPPLEMENTARY_MASK 0177777ul

/*
 * The macros that test status codes for error conditions.
 * Note that the GSS_ERROR() macro has changed slightly from
 * the V1 GSSAPI so that it now evaluates its argument
 * only once.
 */
#define GSS_CALLING_ERROR(x) \
(x & (GSS_C_CALLING_ERROR_MASK << GSS_C_CALLING_ERROR_OFFSET))
#define GSS_ROUTINE_ERROR(x) \
     (x & (GSS_C_ROUTINE_ERROR_MASK << GSS_C_ROUTINE_ERROR_OFFSET))
#define GSS_SUPPLEMENTARY_INFO(x) \
     (x & (GSS_C_SUPPLEMENTARY_MASK << GSS_C_SUPPLEMENTARY_OFFSET))
#define GSS_ERROR(x) \
     (x & ((GSS_C_CALLING_ERROR_MASK << GSS_C_CALLING_ERROR_OFFSET) | \
           (GSS_C_ROUTINE_ERROR_MASK << GSS_C_ROUTINE_ERROR_OFFSET)))

/*
 * Now the actual status code definitions
 */

/*
 * Calling errors:
 */
#define GSS_S_CALL_INACCESSIBLE_READ \
     (1ul << GSS_C_CALLING_ERROR_OFFSET)
#define GSS_S_CALL_INACCESSIBLE_WRITE \
     (2ul << GSS_C_CALLING_ERROR_OFFSET)
#define GSS_S_CALL_BAD_STRUCTURE \
     (3ul << GSS_C_CALLING_ERROR_OFFSET)

/*
 * Routine errors:
 */
#define GSS_S_BAD_MECH (1ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_NAME (2ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_NAMETYPE (3ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_BINDINGS (4ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_STATUS (5ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_SIG (6ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_MIC GSS_S_BAD_SIG
#define GSS_S_NO_CRED (7ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_NO_CONTEXT (8ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_DEFECTIVE_TOKEN (9ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_DEFECTIVE_CREDENTIAL (10ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_CREDENTIALS_EXPIRED (11ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_CONTEXT_EXPIRED (12ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_FAILURE (13ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_QOP (14ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_UNAUTHORIZED (15ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_UNAVAILABLE (16ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_DUPLICATE_ELEMENT (17ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_NAME_NOT_MN (18ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_EXT_COMPAT (19ul << GSS_C_ROUTINE_ERROR_OFFSET)


/*
 * Supplementary info bits:
 */
#define GSS_S_CONTINUE_NEEDED (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 0))
#define GSS_S_DUPLICATE_TOKEN (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 1))
#define GSS_S_OLD_TOKEN (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 2))
#define GSS_S_UNSEQ_TOKEN (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 3))
#define GSS_S_GAP_TOKEN (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 4))

/*
 * Finally, function prototypes for the GSS-API routines.
 */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_acquire_cred)
    (OM_uint32 *,             /*  minor_status */
     const gss_name_t,        /* desired_name */
     OM_uint32,               /* time_req */
     const gss_OID_set,       /* desired_mechs */
     gss_cred_usage_t,        /* cred_usage */
     gss_cred_id_t *,         /* output_cred_handle */
     gss_OID_set *,           /* actual_mechs */
     OM_uint32 *);            /* time_rec */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_release_cred)
    (OM_uint32 *,             /* minor_status */
     gss_cred_id_t *);        /* cred_handle */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_init_sec_context)
    (OM_uint32 *,             /* minor_status */
     const gss_cred_id_t,     /* initiator_cred_handle */
     gss_ctx_id_t *,          /* context_handle */
     const gss_name_t,        /* target_name */
     const gss_OID,           /* mech_type */
     OM_uint32,               /* req_flags */
     OM_uint32,               /* time_req */
     const gss_channel_bindings_t, /* input_chan_bindings */
     const gss_buffer_t,      /* input_token */
     gss_OID *,               /* actual_mech_type */
     gss_buffer_t,            /* output_token */
     OM_uint32 *,             /* ret_flags */
     OM_uint32 *);            /* time_rec */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_accept_sec_context)
    (OM_uint32 *,             /* minor_status */
     gss_ctx_id_t *,          /* context_handle */
     const gss_cred_id_t,     /* acceptor_cred_handle */
     const gss_buffer_t,      /* input_token_buffer */
     const gss_channel_bindings_t, /* input_chan_bindings */
     gss_name_t *,            /* src_name */
     gss_OID *,               /* mech_type */
     gss_buffer_t,            /* output_token */
     OM_uint32 *,             /* ret_flags */
     OM_uint32 *,             /* time_rec */
     gss_cred_id_t *);        /* delegated_cred_handle */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_process_context_token)
    (OM_uint32 *,             /* minor_status */
     const gss_ctx_id_t,      /* context_handle */
     const gss_buffer_t);     /* token_buffer */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_delete_sec_context)
    (OM_uint32 *,             /* minor_status */
     gss_ctx_id_t *,          /* context_handle */
     gss_buffer_t);           /* output_token */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_context_time)
    (OM_uint32 *,             /* minor_status */
     const gss_ctx_id_t,      /* context_handle */
     OM_uint32 *);            /* time_rec */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_get_mic)
    (OM_uint32 *,             /* minor_status */
     const gss_ctx_id_t,      /* context_handle */
     gss_qop_t,               /* qop_req */
     const gss_buffer_t,      /* message_buffer */
     gss_buffer_t);           /* message_token */


GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_verify_mic)
    (OM_uint32 *,             /* minor_status */
     const gss_ctx_id_t,      /* context_handle */
     const gss_buffer_t,      /* message_buffer */
     const gss_buffer_t,      /* token_buffer */
     gss_qop_t *);            /* qop_state */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_wrap)
    (OM_uint32 *,             /* minor_status */
     const gss_ctx_id_t,      /* context_handle */
     int,                     /* conf_req_flag */
     gss_qop_t,               /* qop_req */
     const gss_buffer_t,      /* input_message_buffer */
     int *,                   /* conf_state */
     gss_buffer_t);           /* output_message_buffer */


GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_unwrap)
    (OM_uint32 *,             /* minor_status */
     const gss_ctx_id_t,      /* context_handle */
     const gss_buffer_t,      /* input_message_buffer */
     gss_buffer_t,            /* output_message_buffer */
     int *,                   /* conf_state */
     gss_qop_t *);            /* qop_state */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_display_status)
    (OM_uint32 *,             /* minor_status */
     OM_uint32,               /* status_value */
     int,                     /* status_type */
     const gss_OID,           /* mech_type */
     OM_uint32 *,             /* message_context */
     gss_buffer_t);           /* status_string */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_indicate_mechs)
    (OM_uint32 *,             /* minor_status */
     gss_OID_set *);          /* mech_set */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_compare_name)
    (OM_uint32 *,             /* minor_status */
     const gss_name_t,        /* name1 */
     const gss_name_t,        /* name2 */
     int *);                  /* name_equal */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_display_name)
    (OM_uint32 *,             /* minor_status */
     const gss_name_t,        /* input_name */
     gss_buffer_t,            /* output_name_buffer */
     gss_OID *);              /* output_name_type */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_import_name)
    (OM_uint32 *,             /* minor_status */
     const gss_buffer_t,      /* input_name_buffer */
     const gss_OID,           /* input_name_type */
     gss_name_t *);           /* output_name */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_export_name)
    (OM_uint32  *,            /* minor_status */
     const gss_name_t,        /* input_name */
     gss_buffer_t);           /* exported_name */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_release_name)
    (OM_uint32 *,             /* minor_status */
     gss_name_t *);           /* input_name */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_release_buffer)
    (OM_uint32 *,             /* minor_status */
     gss_buffer_t);           /* buffer */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_release_oid_set)
    (OM_uint32 *,             /* minor_status */
     gss_OID_set *);          /* set */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_inquire_cred)
    (OM_uint32 *,             /* minor_status */
     const gss_cred_id_t,     /* cred_handle */
     gss_name_t *,            /* name */
     OM_uint32 *,             /* lifetime */
     gss_cred_usage_t *,      /* cred_usage */
     gss_OID_set *);          /* mechanisms */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_inquire_context)
    (OM_uint32 *,             /* minor_status */
     const gss_ctx_id_t,      /* context_handle */
     gss_name_t *,            /* src_name */
     gss_name_t *,            /* targ_name */
     OM_uint32 *,             /* lifetime_rec */
     gss_OID *,               /* mech_type */
     OM_uint32 *,             /* ctx_flags */
     int *,                   /* locally_initiated */
     int *);                  /* open */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_wrap_size_limit)
    (OM_uint32 *,             /* minor_status */
     const gss_ctx_id_t,      /* context_handle */
     int,                     /* conf_req_flag */
     gss_qop_t,               /* qop_req */
     OM_uint32,               /* req_output_size */
     OM_uint32 *);            /* max_input_size */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_add_cred)
    (OM_uint32 *,             /* minor_status */
     const gss_cred_id_t,     /* input_cred_handle */
     const gss_name_t,        /* desired_name */
     const gss_OID,           /* desired_mech */
     gss_cred_usage_t,        /* cred_usage */
     OM_uint32,               /* initiator_time_req */
     OM_uint32,               /* acceptor_time_req */
     gss_cred_id_t *,         /* output_cred_handle */
     gss_OID_set *,           /* actual_mechs */
     OM_uint32 *,             /* initiator_time_rec */
     OM_uint32 *);            /* acceptor_time_rec */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_inquire_cred_by_mech)
    (OM_uint32 *,             /* minor_status */
     const gss_cred_id_t,     /* cred_handle */
     const gss_OID,           /* mech_type */
     gss_name_t *,            /* name */
     OM_uint32 *,             /* initiator_lifetime */
     OM_uint32 *,             /* acceptor_lifetime */
     gss_cred_usage_t *);     /* cred_usage */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_export_sec_context)
    (OM_uint32 *,             /* minor_status */
     gss_ctx_id_t *,          /* context_handle */
     gss_buffer_t);           /* interprocess_token */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_import_sec_context)
    (OM_uint32 *,             /* minor_status */
     const gss_buffer_t,      /* interprocess_token */
     gss_ctx_id_t *);         /* context_handle */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_create_empty_oid_set)
    (OM_uint32 *,             /* minor_status */
     gss_OID_set *);          /* oid_set */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_add_oid_set_member)
    (OM_uint32 *,             /* minor_status */
     const gss_OID,           /* member_oid */
     gss_OID_set *);          /* oid_set */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_test_oid_set_member)
    (OM_uint32 *,             /* minor_status */
     const gss_OID,           /* member */
     const gss_OID_set,       /* set */
     int *);                  /* present */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_inquire_names_for_mech)
    (OM_uint32 *,             /* minor_status */
     const gss_OID,           /* mechanism */
     gss_OID_set *            /* name_types */
        );

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_inquire_mechs_for_name)
    (OM_uint32 *,             /* minor_status */
     const gss_name_t,        /* input_name */
     gss_OID_set *);          /* mech_types */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_canonicalize_name)
    (OM_uint32 *,             /* minor_status */
     const gss_name_t,        /* input_name */
     const gss_OID,           /* mech_type */
     gss_name_t *);           /* output_name */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_duplicate_name)
    (OM_uint32 *,             /* minor_status */
     const gss_name_t,        /* src_name */
     gss_name_t *);           /* dest_name */

/*
 * The following routines are obsolete variants of gss_get_mic,
 * gss_verify_mic, gss_wrap and gss_unwrap.  They should be
 * provided by GSSAPI V2 implementations for backwards
 * compatibility with V1 applications.  Distinct entrypoints
 * (as opposed to #defines) should be provided, both to allow
 * GSSAPI V1 applications to link against GSSAPI V2 implementations,
 * and to retain the slight parameter type differences between the
 * obsolete versions of these routines and their current forms.
 */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_sign)
    (OM_uint32 *,        /* minor_status */
     gss_ctx_id_t,       /* context_handle */
     int,                /* qop_req */
     gss_buffer_t,       /* message_buffer */
     gss_buffer_t);      /* message_token */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_verify)
    (OM_uint32 *,        /* minor_status */
     gss_ctx_id_t,       /* context_handle */
     gss_buffer_t,       /* message_buffer */
     gss_buffer_t,       /* token_buffer */
     int *);             /* qop_state */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_seal)
    (OM_uint32 *,        /* minor_status */
     gss_ctx_id_t,       /* context_handle */
     int,                /* conf_req_flag */
     int,                /* qop_req */
     gss_buffer_t,       /* input_message_buffer */
     int *,              /* conf_state */
     gss_buffer_t);      /* output_message_buffer */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_unseal)
    (OM_uint32 *,        /* minor_status */
     gss_ctx_id_t,       /* context_handle */
     gss_buffer_t,       /* input_message_buffer */
     gss_buffer_t,       /* output_message_buffer */
     int *,              /* conf_state */
     int *);             /* qop_state */

#ifndef USE_ONLY_STANDARD_GSSAPI
#define _HAVE_GSI_EXTENDED_GSSAPI

/**
 * @defgroup globus_gsi_gssapi_extensions GSSAPI Extensions
 * @ingroup globus_gsi_gssapi
 * @brief Extensions
 *
 * @details
 * Experimental GSSAPI routines are defined here.
 * These may change, and we will be looking at adding
 * these to Kerberos as mods, and submitting them to the IETF.
 *
 * These extensions are more fully documented in <a href="http://www.ggf.org/documents/GFD.24.pdf">GSS-API Extensions</a>
 */

/**
 * @defgroup globus_gsi_gssapi_extensions_delegation Delegation
 * @ingroup globus_gsi_gssapi_extensions
 * @brief Delegation Functions
 *
 * @details
 * Functions in this section allow delegation to occur outside of
 * the context initiation handshake.
 */
typedef struct gss_buffer_set_desc_struct
{
    size_t                              count;
    gss_buffer_t                        elements;
} gss_buffer_set_desc, *gss_buffer_set_t;

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_create_empty_buffer_set)
    (OM_uint32 *,          /* minor_status */
     gss_buffer_set_t *);  /* buffer set to be created */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_add_buffer_set_member)
    (OM_uint32 *,          /* minor_status */
     const gss_buffer_t,   /* member_buffer */
     gss_buffer_set_t *);  /* buffer set to be freed */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_release_buffer_set)
    (OM_uint32 *,          /* minor_status */
     gss_buffer_set_t *);  /* buffer set to be freed */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_import_cred)
    (OM_uint32 *,        /* minor_status */
     gss_cred_id_t *,    /* cred to be exported */
     const gss_OID,      /* desired mech*/
     OM_uint32,          /* option req */
     const gss_buffer_t, /* import buffer */
     OM_uint32,          /* time req */
     OM_uint32 *);       /* time rec */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_export_cred)
    (OM_uint32 *,        /* minor_status */
     const gss_cred_id_t,/* cred_handle */
     const gss_OID,      /* desired mech */
     OM_uint32,          /* option req */
     gss_buffer_t);      /* output buffer */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_init_delegation)
    (OM_uint32 *,              /* minor_status */
     const gss_ctx_id_t,       /* context_handle */
     const gss_cred_id_t,      /* cred_handle */
     const gss_OID,            /* desired_mech */
     const gss_OID_set,        /* extension_oids */
     const gss_buffer_set_t,   /* extension_buffers */
     const gss_buffer_t,       /* input_token */
     OM_uint32,                /* req_flags */
     OM_uint32,                /* time_req */
     gss_buffer_t);            /* output_token */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_accept_delegation)
    (OM_uint32 *,            /* minor_status */
     const gss_ctx_id_t,     /* context_handle */
     const gss_OID_set,      /* extension_oids */
     const gss_buffer_set_t, /* extension_buffers */
     const gss_buffer_t,     /* input_token */
     OM_uint32,              /* req_flags */
     OM_uint32,              /* time_req */
     OM_uint32 *,            /* time_rec */
     gss_cred_id_t *,        /* delegated_cred_handle */
     gss_OID *,              /* mech_type */
     gss_buffer_t);          /* output_token */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_inquire_sec_context_by_oid)
    (OM_uint32 *,            /* minor_status */
     const gss_ctx_id_t,     /* context_handle */
     const gss_OID,          /* desired_object */
     gss_buffer_set_t *);    /* data_set */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_inquire_cred_by_oid)
    (OM_uint32 *,            /* minor_status */
     const gss_cred_id_t,    /* context_handle */
     const gss_OID,          /* desired_object */
     gss_buffer_set_t *);    /* data_set */

GSS_MAKE_TYPEDEF
OM_uint32
GSS_CALLCONV GSS_FUNC(gss_set_sec_context_option)
    (OM_uint32 *,                       /* minor_status */
     gss_ctx_id_t *,                    /* context_handle */
     const gss_OID,                     /* option */
     const gss_buffer_t);               /* value */

extern const gss_OID_desc * const GSS_DISALLOW_ENCRYPTION;
extern const gss_OID_desc * const GSS_PROTECTION_FAIL_ON_CONTEXT_EXPIRATION;
extern const gss_OID_desc * const GSS_APPLICATION_WILL_HANDLE_EXTENSIONS;

#endif /* GSI_EXTENDED_GSSAPI */

#ifdef __cplusplus
}
#endif

#endif /* GSSAPI_H_ */
