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
 * @file globus_gass_transfer.h
 * @brief GASS transfer
 */

#ifndef GLOBUS_GASS_TRANSFER_H
#define GLOBUS_GASS_TRANSFER_H

#include "globus_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus GASS Transfer
 * @copydoc globus_gass_transfer
 */
#endif

/**
 * @defgroup globus_gass_transfer GASS Transfer API
 * @brief GASS Transfer API
 *
 * The GASS Transfer API is the core part of the GASS
 * (Global Access to Secondary Storage) component of the Grid Community Toolkit.
 * The purpose of GASS is to provide a simple way to enable grid applications
 * to securely stage and access data to and from remote file servers using a
 * simple protocol-independent API.
 *
 * The GASS Transfer API provides a way to implement both
 * @link globus_gass_transfer_client client@endlink and
 * @link globus_gass_transfer_server server@endlink
 * components. These share common data block and request management
 * functionality. Client-specific functions are provided to implement file
 * "get", "put", and "append" operations. Server-specific functions are
 * provided to implement servers which service such requests. Client and
 * server functionality can be included in a single application, so one
 * could implement proxies or cross-protocol bridges.
 *
 * The GASS Transfer API is easily extensible to support different remote
 * data access protocols. The standard Globus distribution includes client-side
 * support for the http, and https protocols, as well as
 * server-side support for the http and https protocols. 
 * An application which requires additional
 * protocol support may add this through the @link
 * globus_gass_transfer_protocol protocol module interface@endlink.
 *
 * The GASS Transfer API is defined in the header file
 * "globus_gass_transfer.h"
 *
 * The #GLOBUS_GASS_TRANSFER_MODULE must be activated before calling any
 * functions in this API.
 */

/**
 * @defgroup globus_gass_transfer_activation Activation
 * @ingroup globus_gass_transfer
 *
 * The Globus GASS Transfer library uses the standard module activation
 * and deactivation API to initialize its state. Before any GASS
 * functions are called, the module must be activated
 *
 * @code
 *    globus_module_activate(GLOBUS_GASS_TRANSFER_MODULE);
 * @endcode
 *
 * This function returns GLOBUS_SUCCESS if the GASS library was
 * successfully initialized. This may be called multiple times.
 *
 * To deactivate the GASS transfer library, the following must be called
 *
 * @code
 *    globus_module_deactivate(GLOBUS_GASS_TRANSFER_MODULE);
 * @endcode
 */
extern globus_module_descriptor_t		globus_i_gass_transfer_module;

/** Module descriptor
 *  @ingroup globus_gass_transfer_activation
 *  @hideinitializer
 */
#define GLOBUS_GASS_TRANSFER_MODULE		(&globus_i_gass_transfer_module)

#define _GTSL(s) globus_common_i18n_get_string( \
		    GLOBUS_GASS_TRANSFER_MODULE, \
		    s)
/**
 * @struct globus_gass_transfer_request_t
 * @ingroup globus_gass_transfer_request
 * @brief Request handle.
 *
 * A request handle is associated with each file transfer operation. The
 * same structure is used for both client- and server- side requests. For
 * client operations, the initial call to globus_gass_transfer_get(),
 * globus_gass_transfer_register_get(), globus_gass_transfer_get(),
 * globus_gass_transfer_register_put(), globus_gass_transfer_append(),
 * globus_gass_transfer_register_append() initializes the request.
 * For server operations, the request is initialized by calling
 * globus_gass_transfer_accept().
 *
 * The functions in the @link globus_gass_transfer_request request
 * section@endlink of this manual describe the functions available for
 * accessing information from a request handle.
 *
 * Each request handle should be destroyed by calling
 * globus_gass_transfer_request_destroy() once the user has completed
 * processing the request.
 */
typedef globus_handle_t globus_gass_transfer_request_t;
typedef globus_handle_t globus_gass_transfer_listener_t;

/**
 * @ingroup globus_gass_transfer_requestattr
 */
typedef globus_object_t * globus_gass_transfer_requestattr_t;
/**
 * @ingroup globus_gass_transfer_listenerattr
 */
typedef globus_object_t * globus_gass_transfer_listenerattr_t;

/* Module Specific Types */
/**
 * Type of operation associated with a request handle.
 * @ingroup globus_gass_transfer_request
 */
typedef enum
{
    /** Handle no longer valid */
    GLOBUS_GASS_TRANSFER_REQUEST_TYPE_INVALID,
    /** A get request */
    GLOBUS_GASS_TRANSFER_REQUEST_TYPE_GET,
    /** A put request */
    GLOBUS_GASS_TRANSFER_REQUEST_TYPE_PUT,
    /** An append request */
    GLOBUS_GASS_TRANSFER_REQUEST_TYPE_APPEND
} globus_gass_transfer_request_type_t;

typedef enum
{
    GLOBUS_GASS_TRANSFER_FILE_MODE_BINARY,
    GLOBUS_GASS_TRANSFER_FILE_MODE_TEXT
} globus_gass_transfer_file_mode_t;


typedef enum
{
    GLOBUS_GASS_TRANSFER_AUTHORIZE_SELF,
    GLOBUS_GASS_TRANSFER_AUTHORIZE_HOST,
    GLOBUS_GASS_TRANSFER_AUTHORIZE_SUBJECT,
    GLOBUS_GASS_TRANSFER_AUTHORIZE_CALLBACK
} globus_gass_transfer_authorization_t;


/* globus_gass_transfer_request_status will return only the
 * "visible" subset:
 *     GLOBUS_GASS_TRANSFER_REQUEST_INVALID
 *     GLOBUS_GASS_TRANSFER_REQUEST_STARTING
 *     GLOBUS_GASS_TRANSFER_REQUEST_PENDING (PENDING, ACTING,
 *						ACTING_TO_PENDING)
 *     GLOBUS_GASS_TRANSFER_REQUEST_FAILED  (FAILED, USER_FAIL, ACTING_TO_FAIL)
 *     GLOBUS_GASS_TRANSFER_REQUEST_REFERRED (REFERRING, ACTING_TO_REFERRING,
 *						ACTING_TO_REFERRING2)
 *     GLOBUS_GASS_TRANSFER_REQUEST_DENIED
 *     GLOBUS_GASS_TRANSFER_REQUEST_DONE
 */
/**
 * Request Status
 * @ingroup globus_gass_transfer_request
 */
typedef enum
{
    GLOBUS_GASS_TRANSFER_REQUEST_INVALID,  /**< Handle is no longer valid */
    GLOBUS_GASS_TRANSFER_REQUEST_STARTING, /**< Initial connection and
                                                authorization is not yet
						completed */
    GLOBUS_GASS_TRANSFER_REQUEST_PENDING,  /**< Request is authorized. */
    GLOBUS_GASS_TRANSFER_REQUEST_FAILED,   /**< Request failed due to protocol
                                                error or client or server
						aborting the request */
    GLOBUS_GASS_TRANSFER_REQUEST_REFERRED, /**<  Request can not be processed
                                                 by this server, referred to
						 another URL or URLs */
    GLOBUS_GASS_TRANSFER_REQUEST_DENIED,   /**< The server denied this
                                                request */
    GLOBUS_GASS_TRANSFER_REQUEST_DONE,     /**< All callbacks have completed */
#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
    GLOBUS_GASS_TRANSFER_REQUEST_ACCEPTING,/**< for listener-created requests,
					       the state before starting,
					       after the user has called
					       register_accept, but before the
					       callback is done */
    GLOBUS_GASS_TRANSFER_REQUEST_ACTING,   /* op passed to request */
    GLOBUS_GASS_TRANSFER_REQUEST_ACTING_TO_PENDING, /* calling back to user */
    GLOBUS_GASS_TRANSFER_REQUEST_ACTING_TO_FAILING, /* op passed to request, and
						    user called fail */
    GLOBUS_GASS_TRANSFER_REQUEST_FAILING,   /* user called fail, or op
					       completed, user callback
					       started */
    GLOBUS_GASS_TRANSFER_REQUEST_USER_FAIL, /* user called fail before
					       ready/referred/denied */
    GLOBUS_GASS_TRANSFER_REQUEST_REFERRING,	    /* proto called referred,
					       callback pending */
    GLOBUS_GASS_TRANSFER_REQUEST_ACTING_TO_REFERRING, /* proto called referred,
						op in progress */
    GLOBUS_GASS_TRANSFER_REQUEST_FINISHING, /* op completed successfully,
					       with last data, user callback
					       started*/
    GLOBUS_GASS_TRANSFER_REQUEST_SERVER_FAIL1, /* user called fail before
					          new_listener_request */
    GLOBUS_GASS_TRANSFER_REQUEST_SERVER_FAIL2, /* user called fail before
					         authorize/refer/deny */
    GLOBUS_GASS_TRANSFER_REQUEST_SERVER_FAIL3, /* user called fail before
					         request_ready */
    GLOBUS_GASS_TRANSFER_REQUEST_STARTING2, /* server not authorized/denied/failed */
    GLOBUS_GASS_TRANSFER_REQUEST_STARTING3  /* server not authorized/denied/failed */
#endif
} globus_gass_transfer_request_status_t;

typedef enum
{
    GLOBUS_GASS_TRANSFER_LISTENER_INVALID,     /* handle no longer valid */
    GLOBUS_GASS_TRANSFER_LISTENER_STARTING,    /* new listener called */
    GLOBUS_GASS_TRANSFER_LISTENER_LISTENING,   /* register listen/
						  proto->register listen */
    GLOBUS_GASS_TRANSFER_LISTENER_READY,       /* proto calls listener_ready */
    GLOBUS_GASS_TRANSFER_LISTENER_ACCEPTING,   /* register_accept() */
    GLOBUS_GASS_TRANSFER_LISTENER_CLOSING1,    /* close_listener before
						  listener_ready called */
    GLOBUS_GASS_TRANSFER_LISTENER_CLOSING2,    /* close listener before
						  new_request */
    GLOBUS_GASS_TRANSFER_LISTENER_CLOSED       /* listener is closed */
} globus_gass_transfer_listener_status_t;

/**
 * GASS error codes
 * @ingroup globus_gass_constants
 */
enum
{
    /** Invalid port in URL */
    GLOBUS_GASS_TRANSFER_ERROR_BAD_PORT = 2,
    /** Something bad occurred while processing the request */
    GLOBUS_GASS_TRANSFER_ERROR_INTERNAL_ERROR,
    /** Unparsable URL */
    GLOBUS_GASS_TRANSFER_ERROR_BAD_URL,
    /** Invalid file open mode in the GASS File library */
    GLOBUS_GASS_TRANSFER_ERROR_NOT_SUPPORTED,
    /** Operation not supported by GASS for this type of URL */
    GLOBUS_GASS_TRANSFER_ERROR_NOT_IMPLEMENTED,
    /** Out of memory */
    GLOBUS_GASS_TRANSFER_ERROR_MALLOC_FAILED,
    /** Uninitialized or invalid handle */
    GLOBUS_GASS_TRANSFER_ERROR_NOT_INITIALIZED,
    /** NULL pointer passed as parameter */
    GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER,
    /** GASS Server not yet registered */
    GLOBUS_GASS_TRANSFER_ERROR_NOT_REGISTERED,
    /** URL not in cache */
    GLOBUS_GASS_TRANSFER_ERROR_NOT_FOUND,
    /** Invalid use of a GASS handle */
    GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE,
    /** Bytes array exceeds GASS request size */
    GLOBUS_GASS_TRANSFER_ERROR_TOO_LARGE,
    /** GASS Transfer request did not complete successfully */
    GLOBUS_GASS_TRANSFER_ERROR_REQUEST_FAILED,
    /** GASS handle already closed before this operation began*/
    GLOBUS_GASS_TRANSFER_ERROR_DONE,
    /** GASS handle already registered for processing */
    GLOBUS_GASS_TRANSFER_ERROR_ALREADY_REGISTERED,
    /** Could not open local file */
    GLOBUS_GASS_TRANSFER_ERROR_OPEN_FAILED,
    /** A protocol error or client-initiated failure has occurred */
    GLOBUS_GASS_TRANSFER_ERROR_TRANSFER_FAILED
};

/**
 * @ingroup globus_gass_constants
 *
 * Default buffer length for the globus_gass_transfer_assist library.
 *
 * @hideinitializer
 */
enum
{
    GLOBUS_GASS_TRANSFER_DEFAULT_BUFFER_LENGTH = 1024
};

/**
 * @ingroup globus_gass_constants
 *
 * Value for files we don't know the length of.
 *
 * @hideinitializer
 */
#define GLOBUS_GASS_TRANSFER_LENGTH_UNKNOWN 0UL

/**
 * @ingroup globus_gass_constants
 *
 * Value for timestamps we don't know the value of.
 *
 * @hideinitializer
 */
#define GLOBUS_GASS_TRANSFER_TIMESTAMP_UNKNOWN 0UL

typedef void
(* globus_gass_transfer_callback_t)(
    void *					arg,
    globus_gass_transfer_request_t 		request);
/* Client Interface */
/**
 * @brief GASS Transfer Client Operations
 *
 * @defgroup globus_gass_transfer_client Client-Initiated Operations
 * @ingroup globus_gass_transfer
 *
 * One mode of using the GASS Transfer API is to initiate
 * file transfers. The operations supported by the GASS Transfer API
 * are file get, put, and append. These operations are provided for
 * HTTP, and HTTPS file servers. The @link
 * globus_gass_transfer_protocol protocol module interface@endlink
 * allows support for additional protocols to be added
 * easily.
 *
 * The GASS transfer library provides both blocking and non-blocking
 * versions of all its client functions. When a blocking function completes,
 * or the non-blocking function's callback is called, the user should
 * check the request's status to discover whether the transfer was
 * completed successfully, denied, or referred.
 */
int
globus_gass_transfer_register_get(
    globus_gass_transfer_request_t *		request,
    globus_gass_transfer_requestattr_t *	attr,
    char *					url,
    globus_gass_transfer_callback_t		callback,
    void *					user_arg);

int
globus_gass_transfer_get(
    globus_gass_transfer_request_t *		request,
    globus_gass_transfer_requestattr_t *	attr,
    char *					url);

int
globus_gass_transfer_register_put(
    globus_gass_transfer_request_t *		request,
    globus_gass_transfer_requestattr_t *	attr,
    char *					url,
    globus_size_t				length,
    globus_gass_transfer_callback_t		callback,
    void *					user_arg);

int
globus_gass_transfer_put(
    globus_gass_transfer_request_t *		request,
    globus_gass_transfer_requestattr_t *	attr,
    char *					url,
    globus_size_t				length);

int
globus_gass_transfer_register_append(
    globus_gass_transfer_request_t *		request,
    globus_gass_transfer_requestattr_t *	attr,
    char *					url,
    globus_size_t				length,
    globus_gass_transfer_callback_t		callback,
    void *					user_arg);

int
globus_gass_transfer_append(
    globus_gass_transfer_request_t *		request,
    globus_gass_transfer_requestattr_t *	attr,
    char *					url,
    globus_size_t				length);

/**
 * @brief GASS Server Implementation
 *
 * @defgroup globus_gass_transfer_server Implementing Servers
 * @ingroup globus_gass_transfer
 *
 * Another mode of using the GASS Transfer API is to implement
 * data servers. The primary difference between the client and
 * server parts of the GASS Transfer API are how requests are generated.
 * 
 * To implement a server, the user would call
 * globus_gass_transfer_create_listener() to create a new server port on
 * which a specific protocol will be used to request file transfer operations.
 * The user may obtain the URL that the listener is bound to by calling
 * globus_gass_transfer_listener_get_base_url().
 *
 * Once the listener is created, the user can call
 * globus_gass_transfer_register_listen() to wait for clients to connect to
 * it. Once the server has detected an attempt to connect by a client, the
 * use can call globus_gass_transfer_register_accept() to accept the connection
 * from the client and parse the request.
 *
 * In the callback associated with globus_gass_transfer_register_accept(),
 * the server can decide how to process the request. The user may choose to
 * authorize the request by calling globus_gass_transfer_authorize(),
 * refer it to another URL or URLs by calling globus_gass_transfer_refer()
 * or deny the client access to the URL by calling globus_gass_transfer_deny().
 */

/**
 * Listener close callback
 * @ingroup globus_gass_transfer_server
 * @param callback_arg
 * @param listener
 */
typedef void
(* globus_gass_transfer_close_callback_t)(
    void *					callback_arg,
    globus_gass_transfer_listener_t		listener);

/**
 * Listen callback.
 * @ingroup globus_gass_transfer_server
 * @param callback_arg
 * @param listener
 */
typedef void
(* globus_gass_transfer_listen_callback_t)(
    void *					callback_arg,
    globus_gass_transfer_listener_t 		listener);

int
globus_gass_transfer_create_listener(
    globus_gass_transfer_listener_t *		listener,
    globus_gass_transfer_listenerattr_t *	attr,
    char *					scheme);

int
globus_gass_transfer_close_listener(
    globus_gass_transfer_listener_t		listener,
    globus_gass_transfer_close_callback_t 	callback,
    void *					user_arg);

int
globus_gass_transfer_register_listen(
    globus_gass_transfer_listener_t		listener,
    globus_gass_transfer_listen_callback_t	callback,
    void *					user_arg);

int
globus_gass_transfer_register_accept(
    globus_gass_transfer_request_t *		request,
    globus_gass_transfer_requestattr_t *	attr,
    globus_gass_transfer_listener_t		listener,
    globus_gass_transfer_callback_t		callback,
    void *					user_arg);

int
globus_gass_transfer_refer(
    globus_gass_transfer_request_t		request,
    char **					urls,
    globus_size_t				num_urls);

int
globus_gass_transfer_authorize(
    globus_gass_transfer_request_t		request,
    globus_size_t				total_length);

int
globus_gass_transfer_deny(
    globus_gass_transfer_request_t		request,
    int						reason,
    char *					message);

char *
globus_gass_transfer_listener_get_base_url(
    globus_gass_transfer_listener_t		listener);

void *
globus_gass_transfer_listener_get_user_pointer(
    globus_gass_transfer_listener_t		listener);

int
globus_gass_transfer_listener_set_user_pointer(
    globus_gass_transfer_listener_t		listener,
    void *					user_pointer);

/**
 * @defgroup globus_gass_transfer_data Sending and Receiving Data
 * @ingroup globus_gass_tansfer
 */

/**
 * Byte send or receive callback function.
 * @ingroup globus_gass_transfer_data
 *
 * @param arg
 *        The user_arg passed to the function which registered this callback.
 *        The user may use this value for any purpose.
 * @param request
 *        The request handle associated with this byte array.
 * @param bytes
 *        The byte array which was sent or received.
 * @param length
 *        The length of data which was sent or received.
 * @param last_data
 *        Boolean flag whether this is the final byte array for this request.
 *
 * @see globus_gass_transfer_send_bytes(),
 *      globus_gass_transfer_receive_bytes()
 */
typedef void
(* globus_gass_transfer_bytes_callback_t)(
    void *					arg,
    globus_gass_transfer_request_t		request,
    globus_byte_t *				bytes,
    globus_size_t				length,
    globus_bool_t				last_data);

int
globus_gass_transfer_send_bytes(
    globus_gass_transfer_request_t		request,
    globus_byte_t *				bytes,
    globus_size_t				send_length,
    globus_bool_t				last_data,
    globus_gass_transfer_bytes_callback_t	callback,
    void *					user_arg);

int
globus_gass_transfer_receive_bytes(
    globus_gass_transfer_request_t		request,
    globus_byte_t *				bytes,
    globus_size_t				max_length,
    globus_size_t				wait_for_length,
    globus_gass_transfer_bytes_callback_t	callback,
    void *					user_arg);

int
globus_gass_transfer_fail(
    globus_gass_transfer_request_t		request,
    globus_gass_transfer_callback_t		callback,
    void *					user_arg);
/**
 * @brief GASS Referrals
 *
 * @defgroup globus_gass_transfer_referral Referrals
 * @ingroup globus_gass_transfer
 *
 * The GASS Transfer API supports referring URL requests to alternate
 * URLs via referrals. Referrals are essentially pointers to another
 * URL or URLs which contain the same file as the original location which
 * a client has requested of a server.  Referrals may span multiple protocol
 * schemes, though not all protocols may be able to generate referrals.
 * For example, an HTTP server may refer a client to another HTTP server,
 * an HTTPS server.
 *
 * Upon receiving a referred response from a server, a client should query
 * the request handle to determine from where the file can be retrieved.
 */
#ifndef DOXYGEN
typedef struct
{
    char **					url;
    globus_size_t				count;
} globus_gass_transfer_referral_t;


globus_size_t
globus_gass_transfer_referral_get_count(
    globus_gass_transfer_referral_t *		referral);

char *
globus_gass_transfer_referral_get_url(
    globus_gass_transfer_referral_t *		referral,
    globus_size_t				index);

int
globus_gass_transfer_referral_destroy(
    globus_gass_transfer_referral_t *		referral);

#endif


/* Request Accessors */
/**
 * @brief Request Handles
 *
 * @defgroup globus_gass_transfer_request Request Handles
 * @ingroup globus_gass_transfer
 *
 * Request handles are used by the GASS Transfer API to associate
 * operations with a single file transfer request. Specifically,
 * they are used to register multiple byte range buffers with
 * a file transfer request, and to query the state of a transfer
 * in-progress.
 *
 * To implement a server, the request handle is populated by
 * the protocol module implementation. The server may use the functions
 * in this section to determine information about what the client
 * is requesting.
 *
 * To implement a client, the request handle should be queried
 * after the blocking call or initial callback has been invoked to determine
 * if the request has been authorized or referred, and after EOF, to
 * determine whether the request has completed successfully.
 *
 * A request handle contains a pointer which may be used by the
 * handler of the request to store a pointer to arbitrary
 * application-specific data.
 */
#ifndef DOXYGEN
globus_gass_transfer_request_type_t
globus_gass_transfer_request_get_type(
    globus_gass_transfer_request_t		request);

globus_gass_transfer_request_status_t
globus_gass_transfer_request_get_status(
    globus_gass_transfer_request_t		request);

char *
globus_gass_transfer_request_get_subject(
    globus_gass_transfer_request_t		request);

int
globus_gass_transfer_request_set_subject(
    globus_gass_transfer_request_t		request,
    char *					subject);

int
globus_gass_transfer_request_get_referral(
    globus_gass_transfer_request_t		request,
    globus_gass_transfer_referral_t *		referral);

char *
globus_gass_transfer_request_get_url(
    globus_gass_transfer_request_t		request);

int
globus_gass_transfer_request_set_url(
    globus_gass_transfer_request_t		request,
    char *					url);

globus_size_t
globus_gass_transfer_request_get_length(
    globus_gass_transfer_request_t		request);

int
globus_gass_transfer_request_get_denial_reason(
    globus_gass_transfer_request_t		request);

char *
globus_gass_transfer_request_get_denial_message(
    globus_gass_transfer_request_t		request);

void *
globus_gass_transfer_request_get_user_pointer(
    globus_gass_transfer_request_t		request);

int
globus_gass_transfer_request_set_user_pointer(
    globus_gass_transfer_request_t		request,
    void *					user_pointer);

int
globus_gass_transfer_request_destroy(
    globus_gass_transfer_request_t		request);

int
globus_gass_transfer_request_set_type(
    globus_gass_transfer_request_t		request,
    globus_gass_transfer_request_type_t		type);
#endif


/**
 * @defgroup globus_gass_transfer_requestattr Request Attributes
 * @ingroup globus_gass_transfer
 *
 * The GASS Transfer library uses Globus objects to provide an
 * extensible way of creating protocol-specific attributes.
 */
#ifndef DOXYGEN
int
globus_gass_transfer_requestattr_init(
    globus_gass_transfer_requestattr_t *	attr,
    char *				        url_scheme);

int
globus_gass_transfer_requestattr_destroy(
    globus_gass_transfer_requestattr_t *	attr);

int
globus_gass_transfer_listenerattr_init(
    globus_gass_transfer_listenerattr_t *	attr,
    char *				        url_scheme);

int
globus_gass_transfer_listenerattr_destroy(
    globus_gass_transfer_listenerattr_t *	attr);

/* Base Attribute Accessors */
int
globus_gass_transfer_requestattr_set_proxy_url(
    globus_gass_transfer_requestattr_t *	attr,
    char *				        proxy_url);

int
globus_gass_transfer_requestattr_get_proxy_url(
    globus_gass_transfer_requestattr_t *	attr,
    char **					proxy_url);

int
globus_gass_transfer_requestattr_set_block_size(
    globus_gass_transfer_requestattr_t *	attr,
    globus_size_t				block_size);

int
globus_gass_transfer_requestattr_get_block_size(
    globus_gass_transfer_requestattr_t *	attr,
    globus_size_t *				block_size);

int
globus_gass_transfer_requestattr_set_file_mode(
    globus_gass_transfer_requestattr_t *	attr,
    globus_gass_transfer_file_mode_t		file_mode);

int
globus_gass_transfer_requestattr_get_file_mode(
    globus_gass_transfer_requestattr_t *	attr,
    globus_gass_transfer_file_mode_t *		file_mode);

int
globus_gass_transfer_requestattr_set_connection_reuse(
    globus_gass_transfer_requestattr_t *	attr,
    globus_bool_t				connection_reuse);

int
globus_gass_transfer_requestattr_get_connection_reuse(
    globus_gass_transfer_requestattr_t *	attr,
    globus_bool_t *				connection_reuse);

/* Socket Attribute Accessors */
int
globus_gass_transfer_requestattr_set_socket_sndbuf(
    globus_gass_transfer_requestattr_t *	attr,
    int						sndbuf);

int
globus_gass_transfer_requestattr_get_socket_sndbuf(
    globus_gass_transfer_requestattr_t *	attr,
    int *					sndbuf);

int
globus_gass_transfer_requestattr_set_socket_rcvbuf(
    globus_gass_transfer_requestattr_t *	attr,
    int						rcvbuf);

int
globus_gass_transfer_requestattr_get_socket_rcvbuf(
    globus_gass_transfer_requestattr_t *	attr,
    int *					rcvbuf);

int
globus_gass_transfer_requestattr_set_socket_nodelay(
    globus_gass_transfer_requestattr_t *	attr,
    globus_bool_t				nodelay);

int
globus_gass_transfer_requestattr_get_socket_nodelay(
    globus_gass_transfer_requestattr_t *	attr,
    globus_bool_t *				nodelay);

/* Security attribute accessors */
int
globus_gass_transfer_secure_requestattr_set_authorization(
    globus_gass_transfer_requestattr_t *	attr,
    globus_gass_transfer_authorization_t	mode,
    char *					subject);

int
globus_gass_transfer_secure_requestattr_get_authorization(
    globus_gass_transfer_requestattr_t *	attr,
    globus_gass_transfer_authorization_t *	mode,
    char **					subject);
#endif

/**
 * @defgroup globus_gass_transfer_listenerattr Listener attributes
 * @ingroup globus_gass_transfer
 */
#ifndef DOXYGEN
int
globus_gass_transfer_listenerattr_set_backlog(
    globus_gass_transfer_listenerattr_t *	attr,
    int						backlog);

int
globus_gass_transfer_listenerattr_get_backlog(
    globus_gass_transfer_listenerattr_t *	attr,
    int	*					backlog);

int
globus_gass_transfer_listenerattr_set_port(
    globus_gass_transfer_listenerattr_t *	attr,
    unsigned short				port);

int
globus_gass_transfer_listenerattr_get_port(
    globus_gass_transfer_listenerattr_t *	attr,
    unsigned short *				port);
#endif

/**
 * @defgroup globus_gass_transfer_requestattr_implementation Implementing Request Attributes
 * @ingroup globus_gass_transfer
 */
extern const globus_object_type_t
GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR_DEFINITION;

#define GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR \
	(&GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR_DEFINITION)

globus_object_t *
globus_gass_transfer_requestattr_initialize(
    globus_object_t *				obj,
    char *					proxy_url,
    globus_size_t				block_size,
    globus_gass_transfer_file_mode_t		file_mode,
    globus_bool_t				connection_reuse);

extern const globus_object_type_t
GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR_DEFINITION;

#define GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR \
	(&GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR_DEFINITION)

globus_object_t *
globus_gass_transfer_socket_requestattr_initialize(
    globus_object_t *				obj,
    char *					proxy_url,
    globus_size_t				block_size,
    globus_gass_transfer_file_mode_t		file_mode,
    globus_bool_t				connection_reuse,
    int						sndbuf,
    int						rcvbuf,
    globus_bool_t				nodelay);

extern const globus_object_type_t
GLOBUS_GASS_OBJECT_TYPE_SECURE_REQUESTATTR_DEFINITION;

#define GLOBUS_GASS_OBJECT_TYPE_SECURE_REQUESTATTR \
	(&GLOBUS_GASS_OBJECT_TYPE_SECURE_REQUESTATTR_DEFINITION)

globus_object_t *
globus_gass_transfer_secure_requestattr_initialize(
    globus_object_t *				obj,
    char *					proxy_url,
    globus_size_t				block_size,
    globus_gass_transfer_file_mode_t		file_mode,
    globus_bool_t				connection_reuse,
    int						sndbuf,
    int						rcvbuf,
    globus_bool_t				nodelay,
    globus_gass_transfer_authorization_t	authorization,
    char *					subject);

/* Listener Attribute Object Types */
extern const globus_object_type_t
GLOBUS_GASS_OBJECT_TYPE_LISTENERATTR_DEFINITION;

#define GLOBUS_GASS_OBJECT_TYPE_LISTENERATTR \
	(&GLOBUS_GASS_OBJECT_TYPE_LISTENERATTR_DEFINITION)

globus_object_t *
globus_gass_transfer_listenerattr_initialize(
    globus_object_t *				obj,
    int						backlog,
    unsigned short				port);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_GASS_TRANSFER_H */
