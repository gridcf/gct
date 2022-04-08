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

/**
 * @file globus_ftp_control.h
 * @brief GridFTP Control Connection API
 */

#ifndef GLOBUS_INCLUDE_FTP_CONTROL_H
#define GLOBUS_INCLUDE_FTP_CONTROL_H 1

#include "globus_common.h"
#include "globus_error_string.h"
#include "globus_io.h"
#include "globus_gss_assist.h"
#include "globus_handle_table.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus GridFTP Control Connection API
 * @copydoc globus_ftp_control
 */
#endif

/**
 * @defgroup globus_ftp_control GridFTP Control API
 * @brief GridFTP Control API
 *
 * @details
 * The globus_ftp_control library provides low-level services
 * needed to implement FTP client and servers. The API provided is
 * protocol specific. See the GASS Transfer library for a
 * protocol-independent transfer interface.
 *
 * This data transfer portion of this API provides support for the
 * standard data methods described in the @ref rfc959 "FTP Specification"
 * as well as @ref page_extensions "extensions" for parallel, striped, and
 * partial data transfer.
 *
 * Any program that uses the GridFTP Control Library must include
 * the globus_ftp_control.h header.
 *
 * The API documentation is organized into several sections
 * - @ref globus_ftp_control_server
 * - @ref globus_ftp_control_client
 * - @ref globus_ftp_control_data
 * - @ref globus_ftp_control_constants
 */

/**
 * @defgroup globus_ftp_control_constants GridFTP Constants
 * @ingroup globus_ftp_control
 * @brief Constants
 */

/**
 * @brief GridFTP TYPE values
 * @ingroup globus_ftp_control_constants
 * @details
 * The enumeration values match the character value of the argument to TYPE.
 */
typedef enum globus_ftp_control_type_e
{
    /** Undefined TYPE */
    GLOBUS_FTP_CONTROL_TYPE_NONE,
    /** ASCII TYPE */
    GLOBUS_FTP_CONTROL_TYPE_ASCII = 'A',
    /** EBCDIC TYPE */
    GLOBUS_FTP_CONTROL_TYPE_EBCDIC = 'E',
    /** Image TYPE */
    GLOBUS_FTP_CONTROL_TYPE_IMAGE = 'I',
    /** Local TYPE */
    GLOBUS_FTP_CONTROL_TYPE_LOCAL = 'L'
} globus_ftp_control_type_t;

/**
 * @brief GridFTP MODE values
 * @ingroup globus_ftp_control_constants
 * @details
 * The enumeration values match the character value of the argument to MODE.
 */
typedef enum globus_ftp_control_mode_e
{
    /** Undefined MODE */
    GLOBUS_FTP_CONTROL_MODE_NONE, 
    /** Stream Mode */
    GLOBUS_FTP_CONTROL_MODE_STREAM = 'S',
    /** Block Mode */
    GLOBUS_FTP_CONTROL_MODE_BLOCK = 'B', 
    /** Extended Block Mode */
    GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK = 'E',
    /** Compressed Mode */
    GLOBUS_FTP_CONTROL_MODE_COMPRESSED = 'C'
} globus_ftp_control_mode_t;

/**
 * @brief GridFTP DCAU values
 * @ingroup globus_ftp_control_constants
 * @details
 * The enumeration values match the character value of the argument to DCAU.
 */
typedef enum globus_ftp_control_dcau_mode_e
{
    /** No Authorization */
    GLOBUS_FTP_CONTROL_DCAU_NONE = 'N',
    /** Self Authorization */
    GLOBUS_FTP_CONTROL_DCAU_SELF = 'A',
    /** Subject Authorization */
    GLOBUS_FTP_CONTROL_DCAU_SUBJECT = 'S',
    /** Default Authorization */
    GLOBUS_FTP_CONTROL_DCAU_DEFAULT
} globus_ftp_control_dcau_mode_t;

/**
 * @brief Control DCAU subject authentication type
 */
typedef struct globus_ftp_control_dcau_subject_s
{
    globus_ftp_control_dcau_mode_t	mode;
    char *				subject;
} globus_ftp_control_dcau_subject_t;

/**
 * @brief GridFTP Striping Types
 * @ingroup globus_ftp_control_constants
 * @details
 * The enumeration values match the types of striping handled by the GridFTP
 * server.
 */
typedef enum globus_ftp_control_striping_mode_e
{
    GLOBUS_FTP_CONTROL_STRIPING_NONE,
    GLOBUS_FTP_CONTROL_STRIPING_PARTITIONED,
    GLOBUS_FTP_CONTROL_STRIPING_BLOCKED_ROUND_ROBIN
} globus_ftp_control_striping_mode_t;

/**
 * @brief Control striping round robin attribute structure
 */
typedef struct globus_ftp_control_round_robin_s
{
    globus_ftp_control_striping_mode_t mode;
    globus_size_t                 block_size;
} globus_ftp_control_round_robin_t;

typedef struct globus_ftp_control_partitioned_s
{
    globus_ftp_control_striping_mode_t    mode;
    globus_size_t                         size;
} globus_ftp_control_partitioned_t;

/**
 * @brief Control DCAU union
 */
typedef union globus_ftp_control_dcau_u
{
    globus_ftp_control_dcau_mode_t	mode;
    globus_ftp_control_dcau_subject_t	subject;
} globus_ftp_control_dcau_t;

/**
 * @brief GridFTP Protection Types
 * @ingroup globus_ftp_control_constants
 * @details
 * The enumeration values match the types of protection handled by the GridFTP
 * server.
 */
typedef enum
{
    /** Clear protection */
    GLOBUS_FTP_CONTROL_PROTECTION_CLEAR = 'C',
    /** Safe (integrity) protection */
    GLOBUS_FTP_CONTROL_PROTECTION_SAFE = 'S',
    /** Confidential (encrypted) protection */
    GLOBUS_FTP_CONTROL_PROTECTION_CONFIDENTIAL = 'E',
    /** Private (integrity and encrypted) protection */
    GLOBUS_FTP_CONTROL_PROTECTION_PRIVATE = 'P'
} globus_ftp_control_protection_t;

/**
 * delayed passive flags
 */
typedef enum
{
    GLOBUS_FTP_CONTROL_DELAYED_SINGLE_PASSIVE = 'S',
    GLOBUS_FTP_CONTROL_DELAYED_STRIPED_PASSIVE = 'M',
    GLOBUS_FTP_CONTROL_NORMAL_PASSIVE = 'N'
} globus_ftp_control_delay_passive_t;

/**
 * @brief Control striping attribute union
 */
typedef union globus_ftp_control_layout_u
{
    globus_ftp_control_striping_mode_t    mode;
    globus_ftp_control_round_robin_t      round_robin;
    globus_ftp_control_partitioned_t      partitioned;
    /*
     * No data required for:
     *     GLOBUS_FTP_CONTROL_STRIPING_NONE
     *     GLOBUS_FTP_CONTROL_STRIPING_PARTITIONED
     */
} globus_ftp_control_layout_t;

/**
 *  control structure structure
 */
typedef enum globus_ftp_control_structure_e
{
    GLOBUS_FTP_CONTROL_STRUCTURE_NONE,
    GLOBUS_FTP_CONTROL_STRUCTURE_FILE,
    GLOBUS_FTP_CONTROL_STRUCTURE_PAGE,
    GLOBUS_FTP_CONTROL_STRUCTURE_RECORD
} globus_ftp_control_structure_t;

/**
 *  control parallelism Types 
 */
typedef enum globus_ftp_control_parallelism_mode_e
{
    GLOBUS_FTP_CONTROL_PARALLELISM_NONE,
    GLOBUS_FTP_CONTROL_PARALLELISM_FIXED
} globus_ftp_control_parallelism_mode_t;

/*  
 *  The base class for all parallel types.  Subtypes
 *  must first define all types in this structure.
 */
typedef struct globus_i_ftp_parallelism_base_s
{
    globus_ftp_control_parallelism_mode_t       mode;
    globus_size_t                               size;
} globus_i_ftp_parallelism_base_t;

typedef struct globus_ftp_parallelism_fixed_s
{
    globus_ftp_control_parallelism_mode_t       mode;
    globus_size_t                               size;
} globus_ftp_parallelism_fixed_t;

/**
 * @brief Control parallelism attribute structure  
 */
typedef union globus_ftp_control_parallelism_u
{
    globus_ftp_control_parallelism_mode_t    mode;
    globus_i_ftp_parallelism_base_t          base;
    globus_ftp_parallelism_fixed_t           fixed;
} globus_ftp_control_parallelism_t;

typedef struct globus_ftp_control_host_port_s
{
    int                                         host[16];
    unsigned short                              port;
    
    /*
     * if ipv6 is not enabled, the following param will be assumed to be 4
     * when passed as an in-parameter. otherwise it must indicate the correct
     * len.
     * 
     * for out-parameters, the following will _always_ be 4 unless ipv6 is
     * allowed. then it will be either 4 or 16
     */
    int                                         hostlen;
} globus_ftp_control_host_port_t;

/**
 * @addtogroup globus_ftp_control
 *
 * The Globus FTP Control library uses the standard module activation and
 * deactivation API to initialize it's state. Before any GridFTP
 * functions are called, the module must be activated
 *
 * @code
      globus_module_activate(GLOBUS_FTP_CONTROL_MODULE);
   @endcode
 *
 * This function returns GLOBUS_SUCCESS if the GridFTP library was
 * successfully initialized. This may be called multiple times.
 *
 * To deactivate the GridFTP library, the following must be called
 *
   @code
      globus_module_deactivate(GLOBUS_FTP_CONTROL_MODULE);
   @endcode
 */

/**
 * @brief Module descriptor
 * @ingroup globus_ftp_control_constants
 */
#define GLOBUS_FTP_CONTROL_MODULE (&globus_i_ftp_control_module)

extern globus_module_descriptor_t globus_i_ftp_control_module; 

#define _FCSL(s) globus_common_i18n_get_string(GLOBUS_FTP_CONTROL_MODULE,s)
/*
 * Module Specific Data Types
 */
typedef enum globus_ftp_control_response_class_e
{
    GLOBUS_FTP_UNKNOWN_REPLY,
    GLOBUS_FTP_POSITIVE_PRELIMINARY_REPLY,
    GLOBUS_FTP_POSITIVE_COMPLETION_REPLY,
    GLOBUS_FTP_POSITIVE_INTERMEDIATE_REPLY,
    GLOBUS_FTP_TRANSIENT_NEGATIVE_COMPLETION_REPLY,
    GLOBUS_FTP_PERMANENT_NEGATIVE_COMPLETION_REPLY
} globus_ftp_control_response_class_t;


typedef struct globus_ftp_control_response_s
{ 
    int                                     code;
    globus_ftp_control_response_class_t     response_class;
    globus_byte_t *                         response_buffer;
    globus_size_t                           response_length;
    globus_size_t                           response_buffer_size;
} globus_ftp_control_response_t;

/** TCP Buffer Setting Modes */
typedef enum globus_ftp_control_tcpbuffer_mode_e
{
    /** Don't change the TCP buffer/window size from the system default */
    GLOBUS_FTP_CONTROL_TCPBUFFER_DEFAULT,
    
    /** Set the TCP buffer/window size to a fixed value */
    GLOBUS_FTP_CONTROL_TCPBUFFER_FIXED,

    /** Automatically set the TCP buffer/window size */
    GLOBUS_FTP_CONTROL_TCPBUFFER_AUTOMATIC
} globus_ftp_control_tcpbuffer_mode_t;

/**
 * @brief Don't change the TCP buffer/window size from the system default
  */
typedef struct
{
    globus_ftp_control_tcpbuffer_mode_t	mode;
}
globus_ftp_control_tcpbuffer_default_t;

/**
 * @brief Set the TCP buffer/window size to a fixed value
 */
typedef struct
{
    globus_ftp_control_tcpbuffer_mode_t	mode;
    int				        size;
}
globus_ftp_control_tcpbuffer_fixed_t;

/**
 * @brief Automatically set the TCP buffer/window size
 */
typedef struct globus_ftp_control_tcpbuffer_automatic_s
{
    globus_ftp_control_tcpbuffer_mode_t	mode;
    unsigned int				initial_size;
    unsigned int				minimum_size;
    unsigned int				maximum_size;
} globus_ftp_control_tcpbuffer_automatic_t;

/**
 * @brief Control tcpbuffer attribute structure
 */
typedef union globus_ftp_control_tcpbuffer_t
{
    globus_ftp_control_tcpbuffer_mode_t	        mode;
    globus_ftp_control_tcpbuffer_default_t	default_tcpbuffer; 
    globus_ftp_control_tcpbuffer_fixed_t	fixed;
    globus_ftp_control_tcpbuffer_automatic_t	automatic;
} globus_ftp_control_tcpbuffer_t;

/*
 *  each strip can have multiple parallel connections to
 *  the same host
 */

typedef enum globus_ftp_data_connection_state_e
{
    GLOBUS_FTP_DATA_STATE_NONE, /* dc_handle has no references */
    GLOBUS_FTP_DATA_STATE_PASV, /* in local pasv mode */
    GLOBUS_FTP_DATA_STATE_PORT, /* in local port mode */
    GLOBUS_FTP_DATA_STATE_SPOR, /* in local spor mode */
    GLOBUS_FTP_DATA_STATE_CONNECT_READ, /* connected for reading */
    GLOBUS_FTP_DATA_STATE_CONNECT_WRITE, /* connected for writing */
    GLOBUS_FTP_DATA_STATE_CLOSING, /* closing all connections */
    GLOBUS_FTP_DATA_STATE_EOF, /* user has received eof */
    GLOBUS_FTP_DATA_STATE_SEND_EOF /* not used for state at all */
} globus_ftp_data_connection_state_t;

#define GLOBUS_FTP_CONTROL_READ_BUFFER_SIZE 1000
#define GLOBUS_FTP_CONTROL_HOSTENT_BUFFER_SIZE 8192

typedef enum
{
    GLOBUS_FTP_CONTROL_UNCONNECTED,
    GLOBUS_FTP_CONTROL_CONNECTING,
    GLOBUS_FTP_CONTROL_CONNECTED,
    GLOBUS_FTP_CONTROL_CLOSING
}
globus_ftp_cc_state_t;

struct globus_ftp_control_handle_s;
struct globus_i_ftp_dc_transfer_handle_t;

union globus_ftp_control_command_u;

typedef enum globus_ftp_control_command_code_e
{
    GLOBUS_FTP_CONTROL_COMMAND_OPTS,
    GLOBUS_FTP_CONTROL_COMMAND_AUTH,
    GLOBUS_FTP_CONTROL_COMMAND_ADAT,
    GLOBUS_FTP_CONTROL_COMMAND_SPAS,
    GLOBUS_FTP_CONTROL_COMMAND_SPOR,
    GLOBUS_FTP_CONTROL_COMMAND_PORT,
    GLOBUS_FTP_CONTROL_COMMAND_PASV,
    GLOBUS_FTP_CONTROL_COMMAND_SITE,
    GLOBUS_FTP_CONTROL_COMMAND_TYPE,
    GLOBUS_FTP_CONTROL_COMMAND_DELE,
    GLOBUS_FTP_CONTROL_COMMAND_FEAT,
    GLOBUS_FTP_CONTROL_COMMAND_ERET,
    GLOBUS_FTP_CONTROL_COMMAND_ESTO,
    GLOBUS_FTP_CONTROL_COMMAND_RMD,
    GLOBUS_FTP_CONTROL_COMMAND_MKD,
    GLOBUS_FTP_CONTROL_COMMAND_PWD,
    GLOBUS_FTP_CONTROL_COMMAND_CWD,
    GLOBUS_FTP_CONTROL_COMMAND_CDUP,
    GLOBUS_FTP_CONTROL_COMMAND_NLST,
    GLOBUS_FTP_CONTROL_COMMAND_HELP,
    GLOBUS_FTP_CONTROL_COMMAND_STAT,
    GLOBUS_FTP_CONTROL_COMMAND_NOOP,
    GLOBUS_FTP_CONTROL_COMMAND_SYST,
    GLOBUS_FTP_CONTROL_COMMAND_STOU,
    GLOBUS_FTP_CONTROL_COMMAND_QUIT,
    GLOBUS_FTP_CONTROL_COMMAND_REIN,
    GLOBUS_FTP_CONTROL_COMMAND_ABOR,
    GLOBUS_FTP_CONTROL_COMMAND_ALLO,
    GLOBUS_FTP_CONTROL_COMMAND_MODE,
    GLOBUS_FTP_CONTROL_COMMAND_STRU,
    GLOBUS_FTP_CONTROL_COMMAND_ACCT,
    GLOBUS_FTP_CONTROL_COMMAND_PASS,
    GLOBUS_FTP_CONTROL_COMMAND_USER,
    GLOBUS_FTP_CONTROL_COMMAND_SMNT,
    GLOBUS_FTP_CONTROL_COMMAND_LIST,
    GLOBUS_FTP_CONTROL_COMMAND_RETR,
    GLOBUS_FTP_CONTROL_COMMAND_REST,
    GLOBUS_FTP_CONTROL_COMMAND_SBUF,
    GLOBUS_FTP_CONTROL_COMMAND_SIZE,
    GLOBUS_FTP_CONTROL_COMMAND_STOR,
    GLOBUS_FTP_CONTROL_COMMAND_APPE,
    GLOBUS_FTP_CONTROL_COMMAND_RNFR,
    GLOBUS_FTP_CONTROL_COMMAND_RNTO,
    GLOBUS_FTP_CONTROL_COMMAND_LANG,
    GLOBUS_FTP_CONTROL_COMMAND_UNKNOWN
} globus_ftp_control_command_code_t;

/**
 *  @brief Authentication Values.
 *
 *  This structure is populated and passed back to the user via
 *  the globus_ftp_control_auth_callback_t().  It contains the
 *  information needed to decide if a client may use the server.
 */

typedef struct globus_ftp_control_auth_info_s
{
    globus_bool_t                               authenticated;
    globus_ftp_control_command_code_t           prev_cmd;
    char *					auth_gssapi_subject;
    gss_ctx_id_t  				auth_gssapi_context;
    gss_cred_id_t                               credential_handle;
    globus_bool_t                               locally_acquired_credential;
    gss_name_t                                  target_name;
    OM_uint32                                   req_flags;
    char *                                      user;
    char *                                      password;
    char *	                                    account;
    gss_cred_id_t                               delegated_credential_handle;
    globus_bool_t                               encrypt;
}
globus_ftp_control_auth_info_t;



/**
 * Asynchronous operation completion callback.
 * 
 * This callback is called whenever a reply to command is received on
 * the FTP control channel. It allows the user to handle the received
 * reply or alternatively handle any errors that occurred during the
 * interaction with the FTP server. This function will be called
 * multiple times in the case when intermediate responses (1yz) are
 * received.
 *
 * @param callback_arg
 *        User supplied argument to the callback function
 * @param handle
 *        A pointer to the GridFTP control handle. Used to identify
 *        which control connection the operation was applied to.
 * @param error
 *        Pointer to a globus error object containing information
 *        about any errors that occurred processing the operation
 * @param ftp_response
 *        Pointer to a response structure containing the FTP response to
 *        the command.
 */

typedef void (*globus_ftp_control_response_callback_t)(
    void *                                      callback_arg,
    struct globus_ftp_control_handle_s *        handle,
    globus_object_t *				error,
    globus_ftp_control_response_t *		ftp_response);

/**
 * Asynchronous control callback.
 * 
 * This callback is used as a generic control operation callback.
 *
 * @param callback_arg
 *        User supplied argument to the callback function
 * @param handle
 *        A pointer to the GridFTP control handle. Used to identify
 *        which control connection the operation was applied to.
 * @param error
 *        Pointer to a globus error object containing information
 *        about any errors that occurred processing the operation
 */
typedef void (*globus_ftp_control_callback_t)(
    void *                                      callback_arg,
    struct globus_ftp_control_handle_s *        handle,
    globus_object_t *				error);


typedef void
(*globus_ftp_control_data_connect_callback_t)(
    void *                                      callback_arg,
    struct globus_ftp_control_handle_s *        handle,
    unsigned int                                stripe_ndx,
    globus_bool_t                               reused,
    globus_object_t *				error);
    
/**
 *  Server command callback.
 *
 *  When a command from a client is received on the control channel
 *  a user callback with this signature is called.
 *
 *  @param callback_arg
 *         The user argument passed to the callback function.
 *  @param handle
 *         The control handle that the command was issued on.
 *  @param error
 *         Indicates if a command was successfully read or
 *         or if a failure occurred. This object will be freed once
 *         this callback returns. If the user wishes to have a copy
 *         of the error that persists past the life of this callback,
 *         they must make a copy using globus_object_copy(), and free
 *         it with globus_object_free().
 *  @param command
 *         The command structure indicates what type of command the
 *         client issued.  Based on the 'type' further information
 *         can be extracted. This command structure will be freed once
 *         this callback returns. If the user wishes to have a copy
 *         of the error that persists past the life of this callback,
 *         they must make a copy using
 *         globus_ftp_control_command_copy(), and free
 *         it with globus_ftp_control_command_free().
 */
typedef void (*globus_ftp_control_command_callback_t)(
    void *                                   callback_arg,
    struct globus_ftp_control_handle_s *     handle,
    globus_object_t *                        error,
    union globus_ftp_control_command_u *     command);

/**
 *  Server authentication complete callback.
 *
 *  A function with this signature is registered by calling
 *  globus_ftp_control_accept().  It is called when the authentication
 *  protocol has completed. Based on the auth_result, the server
 *  implementor should determine authorization and then send the appropriate
 *  response using globus_ftp_control_send_response(), indicating
 *  to the client whether authorization was successful or not.
 *
 *  @param handle
 *         This structure is populated when the callback is called and
 *         represents a control connection to the client.
 *  @param error
 *         Indicates if a command was successfully read or
 *         or if a failure occurred. This object will be freed once
 *         this callback returns. If the user wishes to have a copy
 *         of the error that persists past the life of this callback,
 *         they must make a copy using globus_object_copy(), and free
 *         it with globus_object_free().
 *  @param auth_result
 *         A globus_ftp_control_auth_result_t containing the
 *         values the client sent for gss authentication, user name,
 *         password and account.  If any of the values were not sent by
 *         the client they will be NULL.  Based on that information
 *         the user can decide if the client will be authorized for use
 *         of the server.     
 *  @param callback_arg
 *         The user argument passed to the callback.
 */
typedef void
(*globus_ftp_control_auth_callback_t)(
    void *                                   callback_arg,
    struct globus_ftp_control_handle_s *     handle,
    globus_object_t *                        error,
    globus_ftp_control_auth_info_t *         auth_result);

/**
 * Authentication requirements.
 *
 * The value of this should be a bitwise or of
 * - GLOBUS_FTP_CONTROL_AUTH_NONE
 * - GLOBUS_FTP_CONTROL_AUTH_GSSAPI
 * - GLOBUS_FTP_CONTROL_AUTH_USER
 * - GLOBUS_FTP_CONTROL_AUTH_PASS
 * - GLOBUS_FTP_CONTROL_AUTH_ACCT
 */
typedef unsigned long globus_ftp_control_auth_requirements_t;

#define GLOBUS_FTP_CONTROL_AUTH_REQ_NONE             1
#define GLOBUS_FTP_CONTROL_AUTH_REQ_GSSAPI	     2
#define GLOBUS_FTP_CONTROL_AUTH_REQ_USER	     4
#define GLOBUS_FTP_CONTROL_AUTH_REQ_PASS	     8
#define GLOBUS_FTP_CONTROL_AUTH_REQ_ACCT	     16
#define GLOBUS_FTP_CONTROL_AUTH_REQ_TLS 	     32

typedef struct globus_ftp_control_rw_queue_element_s
{
    globus_ftp_control_response_callback_t	callback;
    globus_ftp_control_callback_t	        send_response_cb;
    void *					arg;
    globus_byte_t *                             write_buf;
    int                                         write_flags;
    globus_io_write_callback_t                  write_callback;
    globus_io_read_callback_t                   read_callback;
    globus_bool_t                               expect_response;
} 
globus_ftp_control_rw_queue_element_t;

typedef struct globus_ftp_cc_handle_s
{
    globus_io_attr_t                                 io_attr;
    globus_netlogger_handle_t                        nl_handle;
    globus_bool_t                                    nl_handle_set;

    globus_fifo_t                                    readers;
    globus_fifo_t                                    writers;
    globus_ftp_control_command_callback_t	     command_cb; 
    void *					     command_cb_arg;

    /* callback and arg for accept */
    globus_ftp_control_callback_t                    accept_cb;
    void *                                           accept_cb_arg;

    globus_ftp_control_auth_callback_t               auth_cb;
    void *                                           auth_cb_arg;
    globus_ftp_control_auth_requirements_t           auth_requirements; 
    globus_ftp_control_response_t                    response;
    globus_byte_t *                                  read_buffer;
    globus_size_t                                    read_buffer_size;
    globus_size_t                                    bytes_read;
    globus_ftp_control_auth_info_t                   auth_info;
    globus_bool_t                                    use_auth;
    globus_io_handle_t                               io_handle;
    globus_ftp_cc_state_t                            cc_state;
    char                                             serverhost[MAXHOSTNAMELEN];
    struct hostent                                   server;
    char                                             server_buffer[
	GLOBUS_FTP_CONTROL_HOSTENT_BUFFER_SIZE];
    globus_list_t *                                  list_elem;
    
    globus_mutex_t                                   mutex;
    globus_bool_t                                    do_close;
    int                                              cb_count;
    globus_ftp_control_response_callback_t           close_cb;
    void *                                           close_cb_arg;
    globus_object_t *                                close_result;
    globus_ftp_control_response_t                    quit_response;
    globus_bool_t                                    signal_deactivate;
}
globus_ftp_cc_handle_t;

struct globus_ftp_control_data_write_info_s;

typedef globus_result_t (*globus_ftp_control_layout_func_t)(
    struct globus_ftp_control_handle_s *        handle,
    struct globus_ftp_control_data_write_info_s *      data_info,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    int                                         stripe_count,
    char *                                      enqueue_str,
    void *                                      user_arg);

typedef globus_result_t (*globus_ftp_control_layout_verify_func_t)(
    char *                                     enqueue_str);

typedef struct globus_i_ftp_dc_handle_s
{
    char                                        magic[32];

    globus_ftp_control_dcau_t                   dcau;
    unsigned long                               pbsz;
    globus_ftp_control_protection_t             protection;

    globus_ftp_data_connection_state_t          state;

    globus_ftp_control_mode_t                   mode;
    globus_ftp_control_type_t                   type;
    globus_ftp_control_structure_t              structure;
    int                                         tcp_buffer_size;
    int                                         form_code;
    globus_ftp_control_parallelism_t            parallel;

    globus_io_attr_t                            io_attr;
    char *                                      interface_addr;

    struct globus_i_ftp_dc_transfer_handle_s *  transfer_handle;
    globus_list_t *                             transfer_list;
    globus_bool_t                               send_eof;

    globus_ftp_control_layout_func_t            layout_func;
    globus_ftp_control_layout_t                 layout;
    char *                                      layout_str;
    void *                                      layout_user_arg;

    globus_bool_t                               initialized;
    globus_mutex_t                              mutex;

    globus_ftp_control_callback_t               close_callback;
    void *                                      close_callback_arg;

    globus_netlogger_handle_t                   nl_io_handle;
    globus_bool_t                               nl_io_handle_set;

    globus_netlogger_handle_t                   nl_ftp_handle;
    globus_bool_t                               nl_ftp_handle_set;

    globus_object_t *                           connect_error;
    struct globus_ftp_control_handle_s *        whos_my_daddy;
} globus_i_ftp_dc_handle_t;

typedef struct globus_ftp_control_handle_s
{
    struct globus_i_ftp_dc_handle_s          dc_handle;
    struct globus_ftp_cc_handle_s            cc_handle;
} globus_ftp_control_handle_t;


/**
 * Asynchronous data transmission operation callback.
 *
 * This callback is called in functions that send or receive data on
 * the data channel(s).
 *
 * In the case of a write, this function is invoked when the entire
 * data buffer is sent. Depending on the data transfer properties set
 * by the globus_ftp_control_local_*() functions, the data may
 * actually be split into multiple buffers and sent to multiple data
 * nodes.
 *
 * In the case of a read, this function will return a single extent of 
 * the data. The order of the data returned is not defined in an
 * extended block mode data transfer, unless the ordered_data flag is set.
 * It is up to the user of the API to re-construct the file order.
 *
 * @param callback_arg
 *        User supplied argument to the callback function
 * @param handle
 *        A pointer to the GridFTP control handle. Used to identify
 *        which control connection the operation was applied to.
 * @param error
 *        Pointer to a globus error object containing information
 *        about any errors that occurred processing the operation
 * @param buffer
 *        The user buffer passed as a parameter to
 *	  globus_ftp_control_data_read() or
 *	  globus_ftp_control_data_write().
 * @param length
 *        The amount of data in the buffer. In the case of an incoming
 *        data channel, this may be less than the 
 *        buffer size.
 * @param offset
 *        The file offset of the data which is contained in the buffer.
 * @param eof
 *        This is set to GLOBUS_TRUE then all of the data associated
 *	  with the transfer has arrived on the data connections
 *	  associated with this handle. If multiple data callbacks are
 *	  registered with this handle, there is no guaranteed order
 *	  of the EOF callback with respect to other data
 *	  callbacks. If multiple callbacks are registered when EOF is
 *	  reached on the data connections, at least one callback
 *	  function will be called with eof set to GLOBUS_TRUE.
 */
typedef void (*globus_ftp_control_data_callback_t)(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *				error,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t				eof);

typedef struct globus_ftp_control_data_write_info_s
{
    globus_ftp_control_data_callback_t          cb;
    void *                                      cb_arg;
    globus_handle_t                             callback_table_handle;
} globus_ftp_control_data_write_info_t;

globus_result_t
globus_ftp_control_layout_register_func(
    char *                                      name,
    globus_ftp_control_layout_func_t            enqueue_func,
    globus_ftp_control_layout_verify_func_t     verify_func);

globus_result_t
globus_X_ftp_control_local_layout(
    globus_ftp_control_handle_t *               handle,
    char *                                      enqueue_str,
    void *                                      user_arg);

globus_result_t
globus_ftp_control_local_layout(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_layout_t *               layout,
    globus_size_t                               data_size);

/*
 *  NET LOGGER STUFF
 */
globus_result_t
globus_ftp_control_set_netlogger(
    globus_ftp_control_handle_t *               handle,
    globus_netlogger_handle_t *                 nl_handle,
    globus_bool_t                               nl_ftp_control,
    globus_bool_t                               nl_globus_io);

globus_result_t
globus_ftp_control_data_set_interface(
    globus_ftp_control_handle_t *               handle,
    const char *                                interface_addr);

globus_result_t
globus_i_ftp_control_data_set_stack(
    globus_ftp_control_handle_t *               handle,
    globus_xio_stack_t                          stack);

globus_result_t
globus_i_ftp_control_data_get_attr(
    globus_ftp_control_handle_t *               handle,
    globus_xio_attr_t *                         attr);

globus_result_t
globus_i_ftp_control_client_get_attr(
    globus_ftp_control_handle_t *               handle,
    globus_xio_attr_t *                         attr);

globus_result_t
globus_i_ftp_control_client_set_stack(
    globus_ftp_control_handle_t *               handle,
    globus_xio_stack_t                          stack);
    
globus_result_t
globus_i_ftp_control_load_xio_drivers(
    char *                                      driver_string,
    globus_list_t **                            driver_list);

void
globus_i_ftp_control_unload_xio_drivers(
    globus_list_t *                             driver_list);

globus_result_t
globus_i_ftp_control_create_stack(
    globus_ftp_control_handle_t *               handle,
    globus_list_t *                             driver_list,
    globus_xio_stack_t  *                       stack);



/*****************************************************************
 *  standard layout functions 
 ****************************************************************/
/*
 * blocked functions
 */
globus_result_t
globus_ftp_control_layout_blocked_verify(
    char *                                     layout_str);

void *
globus_ftp_control_layout_blocked_user_arg_create();

void
globus_ftp_control_layout_blocked_user_arg_destroy(
    void *                                      user_arg);

globus_result_t
globus_ftp_control_layout_blocked(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_data_write_info_t *      data_info,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                in_offset,
    globus_bool_t                               eof,
    int                                         stripe_count,
    char *                                      enqueue_str,
    void *                                      user_arg);

/*
 * partitioned functions
 */
globus_result_t
globus_ftp_control_layout_partitioned_verify(
    char *                                     layout_str);

void *
globus_ftp_control_layout_partitioned_user_arg_create(
    globus_size_t                              file_size);

void
globus_ftp_control_layout_partitioned_user_arg_destroy(
    void *                                      user_arg);

globus_result_t
globus_ftp_control_layout_partitioned(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_data_write_info_t *      data_info,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                in_offset,
    globus_bool_t                               eof,
    int                                         stripe_count,
    char *                                      enqueue_str,
    void *                                      user_arg);

/*
 *  data registration functions
 */
globus_result_t
globus_ftp_control_create_data_info(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_data_write_info_t *      data_info,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    globus_ftp_control_data_callback_t          callback,
    void *                                      callback_arg);

globus_result_t
globus_ftp_control_release_data_info(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_data_write_info_t *      data_info);

globus_result_t
globus_ftp_control_data_write_stripe(
    globus_ftp_control_handle_t *               handle,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    int                                         stripe_ndx,
    globus_ftp_control_data_callback_t          callback,
    void *                                      callback_arg);

globus_result_t
globus_X_ftp_control_data_write_stripe(
    globus_ftp_control_handle_t *               handle,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof,
    int                                         stripe_ndx,
    globus_ftp_control_data_write_info_t *      data_info);


/* 
 *  Server API callbacks
 *  ----------------------------------------------------------------
 */

/**
 * @defgroup globus_ftp_control_server GridFTP Server Control
 * @ingroup globus_ftp_control
 * @brief Manage GridFTP Server Control Connections
 */
struct globus_ftp_control_server_s;

/**
 * @brief Server callback
 * @ingroup globus_ftp_control_server
 *
 * A function with this signature can be used as general callbacks for 
 * the GridFTP server API.
 *
 * @param server_handle
 *        The server handle associated with callback.
 * @param error
 *        Indicates if the operation completed successfully or
 *        if a failure occurred.
 * @param callback_arg
 *        The user argument passed to the callback function.
 */
typedef void (*globus_ftp_control_server_callback_t)(
    void *                                      callback_arg,
    struct globus_ftp_control_server_s *        server_handle,
    globus_object_t *                           error);


typedef enum globus_ftp_control_server_state_n
{
    GLOBUS_FTP_CONTROL_SERVER_LISTENING,
    GLOBUS_FTP_CONTROL_SERVER_DEAF
}
globus_ftp_control_server_state_t;

typedef struct globus_ftp_control_server_s
{
    globus_io_handle_t                          io_handle;
    globus_ftp_control_server_state_t           state;
    globus_ftp_control_server_callback_t        callback;
    void *                                      callback_arg;
    globus_list_t *                             list_elem;
    globus_mutex_t                              mutex;
} globus_ftp_control_server_t;



typedef struct globus_ftp_control_command_str_s
{ 
    globus_ftp_control_command_code_t         code;
    char *                                    raw_command;
    char *                                    string_arg;
} globus_ftp_control_command_str_t;

/*
 * complex commands
 */

typedef struct globus_ftp_control_command_stru_s
{
    globus_ftp_control_command_code_t         code;
    char *                                    raw_command;
    globus_ftp_control_structure_t            structure; 
} globus_ftp_control_command_stru_t;

typedef struct globus_ftp_control_command_port_s
{
    globus_ftp_control_command_code_t         code;
    char *                                    raw_command;
    globus_ftp_control_host_port_t            host_port;
} globus_ftp_control_command_port_t;

typedef struct globus_ftp_control_command_spor_s
{
    globus_ftp_control_command_code_t         code;
    char *                                    raw_command;
    int                                       num_args;
    globus_ftp_control_host_port_t *          host_port;
} globus_ftp_control_command_spor_t;

typedef struct globus_ftp_control_command_mode_s
{
    globus_ftp_control_command_code_t         code;
    char *                                    raw_command;
    globus_ftp_control_mode_t                 mode; 
} globus_ftp_control_command_mode_t;

typedef struct  globus_ftp_control_command_allo_s
{
    globus_ftp_control_command_code_t         code;
    char *                                    raw_command;
    int                                       size;
    int                                       record_size;
} globus_ftp_control_command_allo_t;

typedef struct globus_ftp_control_command_sbuf_s
{
    globus_ftp_control_command_code_t         code;
    char *                                    raw_command;
    int                                       buffer_size;
} globus_ftp_control_command_sbuf_t;
/*

Can't parse marker unless I know state

typedef struct globus_ftp_control_command_rest_s
{
} globus_ftp_control_command_rest_t;

*/

typedef enum globus_ftp_control_type_option_e
{
    GLOBUS_FTP_CONTROL_TYPE_NO_OPTION,
    GLOBUS_FTP_CONTROL_TYPE_OPTION_N,
    GLOBUS_FTP_CONTROL_TYPE_OPTION_T,
    GLOBUS_FTP_CONTROL_TYPE_OPTION_C
} globus_ftp_control_type_option_t;

typedef struct globus_ftp_control_command_type_s
{
    globus_ftp_control_command_code_t         code;
    char *                                    raw_command;
    globus_ftp_control_type_t                 type;
    globus_ftp_control_type_option_t          option;
    unsigned int                              bytesize; 
} globus_ftp_control_command_type_t;

typedef enum globus_ftp_control_auth_type_e
{
    GLOBUS_FTP_CONTROL_AUTH_GSSAPI,
    GLOBUS_FTP_CONTROL_AUTH_UNKNOWN
} globus_ftp_control_auth_type_t;

typedef struct globus_ftp_control_command_auth_s
{
    globus_ftp_control_command_code_t         code;
    char *                                    raw_command;
    globus_ftp_control_auth_type_t            type;
} globus_ftp_control_command_auth_t;

typedef struct globus_ftp_control_command_opts_s
{
    globus_ftp_control_command_code_t         code;
    char *                                    raw_command;
    char *                                    cmd_name;
    char *                                    cmd_opts;
} globus_ftp_control_command_opts_t;

/*
 * single string commands
 */

typedef globus_ftp_control_command_str_t
globus_ftp_control_command_site_t;

typedef globus_ftp_control_command_str_t
globus_ftp_control_command_acct_t;

typedef globus_ftp_control_command_str_t  
globus_ftp_control_command_cwd_t;

typedef globus_ftp_control_command_str_t  
globus_ftp_control_command_cdup_t;

typedef globus_ftp_control_command_str_t
globus_ftp_control_command_pass_t;

typedef globus_ftp_control_command_str_t  
globus_ftp_control_command_user_t;

typedef globus_ftp_control_command_str_t
globus_ftp_control_command_smnt_t;

typedef globus_ftp_control_command_str_t
globus_ftp_control_command_list_t;

typedef globus_ftp_control_command_str_t 
globus_ftp_control_command_retr_t;

typedef globus_ftp_control_command_str_t    
globus_ftp_control_command_size_t;

typedef globus_ftp_control_command_str_t    
globus_ftp_control_command_stor_t;

typedef globus_ftp_control_command_str_t 
globus_ftp_control_command_appe_t;

typedef globus_ftp_control_command_str_t 
globus_ftp_control_command_rnfr_t;

typedef globus_ftp_control_command_str_t  
globus_ftp_control_command_rnto_t;

typedef globus_ftp_control_command_str_t
globus_ftp_control_command_feat_t;

typedef globus_ftp_control_command_str_t   
globus_ftp_control_command_dele_t;

typedef globus_ftp_control_command_str_t 
globus_ftp_control_command_rmd_t;

typedef globus_ftp_control_command_str_t  
globus_ftp_control_command_mkd_t;

typedef globus_ftp_control_command_str_t
globus_ftp_control_command_nlst_t;

typedef globus_ftp_control_command_str_t      
globus_ftp_control_command_help_t;

typedef globus_ftp_control_command_str_t
globus_ftp_control_command_stou_t;

typedef globus_ftp_control_command_str_t      
globus_ftp_control_command_rest_t;

typedef globus_ftp_control_command_str_t      
globus_ftp_control_command_eret_t;

typedef globus_ftp_control_command_str_t      
globus_ftp_control_command_esto_t;

/*
 * no string commands
 */
typedef globus_ftp_control_command_str_t 
globus_ftp_control_command_pasv_t;

typedef globus_ftp_control_command_str_t 
globus_ftp_control_command_spas_t;

typedef globus_ftp_control_command_str_t
globus_ftp_control_command_stat_t;

typedef globus_ftp_control_command_str_t      
globus_ftp_control_command_noop_t;

typedef globus_ftp_control_command_str_t
globus_ftp_control_command_syst_t;

typedef globus_ftp_control_command_str_t      
globus_ftp_control_command_quit_t;

typedef globus_ftp_control_command_str_t      
globus_ftp_control_command_rein_t;

typedef globus_ftp_control_command_str_t      
globus_ftp_control_command_abor_t;

typedef globus_ftp_control_command_str_t      
globus_ftp_control_command_pwd_t;

typedef globus_ftp_control_command_str_t      
globus_ftp_control_command_adat_t;




typedef union globus_ftp_control_command_u
{
    globus_ftp_control_command_code_t         code;
    globus_ftp_control_command_site_t         site;
    globus_ftp_control_command_sbuf_t         sbuf;
    globus_ftp_control_command_type_t         type;
    globus_ftp_control_command_rest_t         rest;
    globus_ftp_control_command_allo_t         allo;
    globus_ftp_control_command_eret_t         eret;
    globus_ftp_control_command_esto_t         esto;
    globus_ftp_control_command_mode_t         mode;
    globus_ftp_control_command_port_t         port;
    globus_ftp_control_command_spor_t         spor;
    globus_ftp_control_command_stru_t         stru;
    globus_ftp_control_command_auth_t         auth;

    globus_ftp_control_command_adat_t         adat;
    globus_ftp_control_command_acct_t         acct;
    globus_ftp_control_command_cwd_t          cwd;
    globus_ftp_control_command_cdup_t         cdup;
    globus_ftp_control_command_pass_t         pass;
    globus_ftp_control_command_user_t         user;
    globus_ftp_control_command_smnt_t         smnt;
    globus_ftp_control_command_opts_t         opts;
    globus_ftp_control_command_list_t         list;
    globus_ftp_control_command_retr_t         retr;
    globus_ftp_control_command_size_t         size;
    globus_ftp_control_command_stor_t         stor;
    globus_ftp_control_command_appe_t         appe;
    globus_ftp_control_command_rnfr_t         rnfr;
    globus_ftp_control_command_rnto_t         rnto;
    globus_ftp_control_command_dele_t         dele;
    globus_ftp_control_command_feat_t         feat;
    globus_ftp_control_command_rmd_t          rmd;
    globus_ftp_control_command_mkd_t          mkd;
    globus_ftp_control_command_nlst_t         nlst;
    globus_ftp_control_command_help_t         help;

    globus_ftp_control_command_pasv_t         pasv;
    globus_ftp_control_command_spas_t         spas;
    globus_ftp_control_command_stat_t         stat;
    globus_ftp_control_command_noop_t         noop;
    globus_ftp_control_command_syst_t         syst;
    globus_ftp_control_command_stou_t         stou;
    globus_ftp_control_command_quit_t         quit;
    globus_ftp_control_command_rein_t         rein;
    globus_ftp_control_command_abor_t         abor; 
    globus_ftp_control_command_pwd_t          pwd;
 
    globus_ftp_control_command_str_t          base;
} globus_ftp_control_command_t;


typedef struct globus_ftp_data_server_s
{
    int bogus;
} globus_ftp_data_server_t;

/**
 * @defgroup globus_ftp_control_client GridFTP Control Client
 * @ingroup globus_ftp_control
 * @brief Control Client
 */
globus_result_t 
globus_ftp_control_auth_info_init(
    globus_ftp_control_auth_info_t *       auth_info,
    gss_cred_id_t			   credential_handle,
    globus_bool_t			   encrypt,
    char *                                 user,
    char *                                 password,
    char *                                 account,
    char *                                 subject);

int
globus_ftp_control_auth_info_compare(
    globus_ftp_control_auth_info_t *       auth_info_1,
    globus_ftp_control_auth_info_t *       auth_info_2);

globus_result_t 
globus_ftp_control_handle_init(
    globus_ftp_control_handle_t *           handle);

globus_result_t 
globus_ftp_control_handle_destroy(
    globus_ftp_control_handle_t *          handle);

globus_result_t 
globus_ftp_control_server_handle_init(
    globus_ftp_control_server_t *           handle);

globus_result_t 
globus_ftp_control_server_handle_destroy(
    globus_ftp_control_server_t *          handle);

globus_result_t 
globus_ftp_control_response_destroy(
    globus_ftp_control_response_t *        response);

globus_result_t 
globus_ftp_control_response_copy(
    globus_ftp_control_response_t *       src,
    globus_ftp_control_response_t *       dest);


globus_result_t
globus_ftp_control_connect(
    globus_ftp_control_handle_t *		handle,
    char *				        host,
    unsigned short				port,
    globus_ftp_control_response_callback_t	callback,
    void *					callback_arg);


globus_result_t
globus_ftp_control_use_tls(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_auth_info_t *            auth_info);

globus_result_t
globus_ftp_control_authenticate(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_auth_info_t *            auth_info,
    globus_bool_t                               use_auth,
    globus_ftp_control_response_callback_t      callback,
    void *                                      callback_arg);

globus_result_t
globus_ftp_control_authenticate_ex(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_auth_info_t *            auth_info,
    globus_bool_t				use_auth,
    globus_ftp_control_response_callback_t	callback,
    void *					callback_arg);

globus_result_t
globus_ftp_control_abort(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_response_callback_t	callback,
    void *					callback_arg);

globus_result_t
globus_ftp_control_quit(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_response_callback_t	callback,
    void *					callback_arg);

globus_result_t
globus_ftp_control_force_close(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_response_callback_t	callback,
    void *					callback_arg);


globus_result_t
globus_ftp_control_send_command(
    globus_ftp_control_handle_t *		handle,
    const char *				cmdspec,
    globus_ftp_control_response_callback_t	callback,
    void *					callback_arg,
    ...);

globus_result_t
globus_ftp_control_local_pbsz(
    globus_ftp_control_handle_t *		handle,
    unsigned long 				bufsize);

globus_result_t
globus_ftp_control_get_pbsz(
    globus_ftp_control_handle_t *		handle,
    unsigned long *				bufsize);

/* globus_ftp_control_server.c */
globus_result_t
globus_ftp_control_server_listen(
    globus_ftp_control_server_t *		handle,
    unsigned short *				port,
    globus_ftp_control_server_callback_t	callback,
    void *					callback_arg);

globus_result_t
globus_ftp_control_server_listen_ex(
    globus_ftp_control_server_t *		handle,
    globus_io_attr_t *				attr,
    unsigned short *				port,
    globus_ftp_control_server_callback_t	callback,
    void *					callback_arg);

globus_result_t
globus_ftp_control_server_stop(
    globus_ftp_control_server_t *		listener,
    globus_ftp_control_server_callback_t	callback,
    void *					callback_arg);

globus_result_t
globus_ftp_control_server_accept(
    globus_ftp_control_server_t *               listener,
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_callback_t               callback,
    void *                                      callback_arg);

globus_result_t
globus_ftp_control_server_authenticate(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_auth_requirements_t      auth_requirements,
    globus_ftp_control_auth_callback_t          callback,
    void *                                      callback_arg);

globus_result_t
globus_ftp_control_read_commands(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_command_callback_t	callback,
    void *					callback_arg);

globus_result_t
globus_ftp_control_send_response(
    globus_ftp_control_handle_t *		handle,
    const char *				respspec,
    globus_ftp_control_callback_t               callback,
    void *                                      callback_arg,
    ...);

globus_result_t
globus_ftp_control_close(
    globus_ftp_control_server_t *		listener,
    globus_ftp_control_server_callback_t	callback,
    void *					callback_arg);

globus_result_t
globus_ftp_data_close(
    globus_ftp_data_server_t *		        listener,
    globus_ftp_control_server_callback_t	callback,
    void *					callback_arg);

int
 globus_i_ftp_queue_size(
     globus_ftp_control_handle_t *           handle,
     int                                          stripe_ndx);

/* command object functions */
globus_result_t
globus_ftp_control_command_copy(
    globus_ftp_control_command_t *           dest,
    globus_ftp_control_command_t *           src);

globus_result_t
globus_ftp_control_command_init(
    globus_ftp_control_command_t *              command,
    char *                                      raw_command,
    globus_ftp_control_auth_info_t *            auth_info);

globus_result_t
globus_ftp_control_command_destroy(
    globus_ftp_control_command_t *           command);

/**
 * @defgroup globus_ftp_control_data GridFTP Data Connections
 * @ingroup globus_ftp_control
 * @brief Data Connections
 */
globus_result_t
globus_i_ftp_control_data_cc_init(
    globus_ftp_control_handle_t *                control_handle);

globus_result_t
globus_i_ftp_control_data_cc_destroy(
    globus_ftp_control_handle_t *                control_handle);

globus_result_t
globus_ftp_control_data_force_close(
    globus_ftp_control_handle_t *                control_handle,
    globus_ftp_control_callback_t                destroy_callback,
    void *                                       destroy_callback_arg);

globus_result_t
globus_ftp_control_local_send_eof(
    globus_ftp_control_handle_t *               handle,
    globus_bool_t                               send_eof);

globus_result_t
globus_ftp_control_data_send_eof(
    globus_ftp_control_handle_t *                  handle,
    int                                            count[],
    int                                            array_size,
    globus_bool_t                                  eof_message,
    globus_ftp_control_callback_t                  cb,
    void *                                         user_arg);

globus_result_t
globus_ftp_control_get_stripe_count(
    globus_ftp_control_handle_t *               handle,
    int *                                       stripe_count);

globus_result_t
globus_ftp_control_data_connect_read(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_data_connect_callback_t  callback,
    void *                                      user_arg);

globus_result_t
globus_ftp_control_data_connect_write(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_data_connect_callback_t  callback,
    void *                                      user_arg);

globus_result_t
globus_ftp_control_data_add_channels(
    globus_ftp_control_handle_t *               handle,
    unsigned int                                num_channels,
    unsigned int                                stripe);

globus_result_t
globus_ftp_control_data_remove_channels(
    globus_ftp_control_handle_t *            handle,
    unsigned int                                num_channels,
    unsigned int                                stripe);

globus_result_t
globus_ftp_control_data_query_channels(
    globus_ftp_control_handle_t *		handle,
    unsigned int *				num_channels,
    unsigned int                                stripe);

globus_result_t
globus_ftp_control_data_get_total_data_channels(
    globus_ftp_control_handle_t *               handle,
    unsigned int *                              num_channels,
    unsigned int                                stripe_ndx);

globus_result_t
globus_ftp_control_data_get_remote_hosts(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_host_port_t *            address,
    int *                                       addr_count);

globus_result_t
globus_ftp_control_get_parallelism(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_parallelism_t *          parallelism);

globus_result_t
globus_ftp_control_local_parallelism(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_parallelism_t *          parallelism);

globus_result_t
globus_ftp_control_local_pasv(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_host_port_t *            address);

globus_result_t
globus_ftp_control_local_spas(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_host_port_t              addresses[],
    unsigned int                                num_addresses);

globus_result_t
globus_ftp_control_local_port(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_host_port_t *            address);

globus_result_t
globus_ftp_control_local_spor(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_host_port_t              addresses[],
    unsigned int                                num_addresses);

globus_result_t
globus_ftp_control_get_spor(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_host_port_t              addresses[],
    unsigned int *                              num_addresses);

globus_result_t
globus_ftp_control_local_type(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_type_t			type,
    int						form_code);

globus_result_t
globus_ftp_control_local_tcp_buffer(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_tcpbuffer_t *            tcp_buffer);

globus_result_t
globus_ftp_control_local_mode(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_mode_t			mode);

globus_result_t
globus_ftp_control_get_mode(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_mode_t *                 mode);

globus_result_t
globus_ftp_control_get_type(
    globus_ftp_control_handle_t *               handle,
    globus_ftp_control_type_t *                 type);

globus_result_t
globus_ftp_control_local_dcau(
    globus_ftp_control_handle_t *               handle,
    const globus_ftp_control_dcau_t *           dcau,
    gss_cred_id_t                               delegated_credential_handle);

globus_result_t
globus_ftp_control_get_dcau(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_dcau_t *			dcau);

globus_result_t
globus_ftp_control_local_prot(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_protection_t		protection);

globus_result_t
globus_ftp_control_get_prot(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_protection_t *		protection);

globus_result_t
globus_ftp_control_local_stru(
    globus_ftp_control_handle_t *		handle,
    globus_ftp_control_structure_t		structure);

globus_result_t
globus_ftp_control_data_write(
    globus_ftp_control_handle_t *		handle,
    globus_byte_t *				buffer,
    globus_size_t				length,
    globus_off_t				offset,
    globus_bool_t				eof,
    globus_ftp_control_data_callback_t        	callback,
    void *					callback_arg);

globus_result_t
globus_ftp_control_data_read(
    globus_ftp_control_handle_t *		handle,
    globus_byte_t *				buffer,
    globus_size_t				max_length,
    globus_ftp_control_data_callback_t     	callback,
    void *					callback_arg);

globus_result_t
globus_ftp_control_data_read_all(
    globus_ftp_control_handle_t *               handle,
    globus_byte_t *                             buffer,
    globus_size_t                               length,
    globus_ftp_control_data_callback_t          callback,
    void *                                      callback_arg);

globus_result_t
globus_ftp_control_set_force_order(
    globus_ftp_control_handle_t *               handle,
    globus_bool_t                               order_data,
    globus_off_t                                starting_offset);

/* this has only been tested enough for the client library and GridFTP server
 * it is very likely to not work for general usage
 */
globus_result_t
globus_ftp_control_ipv6_allow(
    globus_ftp_control_handle_t *               handle,
    globus_bool_t                               allow);

/*
 *  internal function definitions
 */
globus_result_t
globus_i_ftp_parallelism_copy(
    globus_ftp_control_parallelism_t *             dest_parallelism,
    globus_ftp_control_parallelism_t *             src_parallelism);

int
globus_i_ftp_parallelism_get_size(
    globus_ftp_control_parallelism_t *             parallelism);

int
globus_i_ftp_parallelism_get_min_size(
    globus_ftp_control_parallelism_t *             parallelism);

int
globus_i_ftp_parallelism_get_max_size(
    globus_ftp_control_parallelism_t *             parallelism);

void
globus_ftp_control_host_port_init(
    globus_ftp_control_host_port_t *              host_port,
    char *                                        host,
    unsigned short                                port);

void
globus_ftp_control_host_port_destroy(
    globus_ftp_control_host_port_t *              host_port);

void
globus_ftp_control_host_port_get_host(
    globus_ftp_control_host_port_t *              host_port,
    char *                                        host);

unsigned short
globus_ftp_control_host_port_get_port(
    globus_ftp_control_host_port_t *              host_port);

void
globus_ftp_control_host_port_copy(
    globus_ftp_control_host_port_t *              dest,
    globus_ftp_control_host_port_t *              src);

#define globus_i_ftp_control_client_get_connection_info \
     globus_ftp_control_client_get_connection_info
globus_result_t
globus_ftp_control_client_get_connection_info(
    globus_ftp_control_handle_t *         handle,
    int                                   localhost[4],
    unsigned short *                      localport,
    int                                   remotehost[4],
    unsigned short *                      remoteport);

globus_result_t
globus_ftp_control_client_get_connection_info_ex(
    globus_ftp_control_handle_t *         handle,
    globus_ftp_control_host_port_t *      local_info,
    globus_ftp_control_host_port_t *      remote_info);

globus_result_t
globus_ftp_control_data_get_socket_buf(
    globus_ftp_control_handle_t *       handle,
    int *                               rcvbuf,
    int *                               sndbuf);

globus_result_t
globus_ftp_control_data_get_retransmit_count(
    globus_ftp_control_handle_t *       handle,
    char **                             retransmit_count);

#ifdef __cplusplus
}
#endif


#endif  /* GLOBUS_INCLUDE_FTP_CONTROL_H */
