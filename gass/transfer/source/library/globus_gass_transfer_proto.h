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
 * @file globus_gass_transfer_proto.h
 * @brief GASS Transfer Protocol Interface
 *
 * This header defines the GASS protocol module library interface
 */

/**
 * @brief Protocol Implementation API
 *
 * @defgroup globus_gass_transfer_protocol Protocol Modules
 * @ingroup globus_gass_transfer
 * @brief Protocol Modules
 *
 * The GASS Protocol Module API is designed to make it possible to
 * extend the GASS client and server APIs to support additional protocols
 * without making any changes to the core of the GASS implementation. GASS
 * protocol modules are intended to to handle protocol-specific connection
 * and data handling. The GASS Transfer library includes protocol modules
 * which implement the HTTP, HTTPS, FTP, and GSI-FTP protocols. 
 *
 * Every protocol module implementation must include the following
 * header file:
 * @code
   #include "globus_gass_transfer_proto.h"
   @endcode
 *
 * To implement a protocol module, one must create a 
 * #globus_gass_transfer_proto_descriptor_t structure which indicates what
 * the protocol module is able to do. This structure contains the URL scheme
 * which the protocol module supports, and function pointers which indicate
 * what type of operations (client or server) that the module implements.
 * To implement a client-side protocol module, the new_requestattr and
 * new_request fields must be set to the protocol module's implementations
 * of those functions. To implement a server-side protocol module, the
 * new_listenerattr and new_listener functions must be set to the protocol
 * module's implementations of those functions. 
 *
 * A protocol module implementor registers a protocol module with the GASS
 * Transfer library by calling the function
 * globus_gass_transfer_proto_register_protocol(), and unregisters the module
 * by calling globus_gass_transfer_proto_unregister_protocol(). This functions
 * must be called after the #GLOBUS_GASS_TRANSFER_MODULE has already been
 * activated. Once registered, applications may use URLs of the scheme type
 * provided by the protocol module for the standard @link
 * globus_gass_transfer_client client@endlink or @link
 * globus_gass_transfer_server server@endlink operations.
 */

#ifndef GLOBUS_GASS_PROTO_H
#define GLOBUS_GASS_PROTO_H

#include "globus_common.h"
#include "globus_gass_transfer.h"

#ifdef __cplusplus
"C" {
#endif

/* Module-specific types */
/**
 * Protocol module request handling structure.
 * @ingroup globus_gass_transfer_protocol
 * @see #globus_gass_transfer_request_proto_s */
typedef struct globus_gass_transfer_request_proto_s
globus_gass_transfer_request_proto_t;

/**
 * Protocol module listener handling structure.
 * @ingroup globus_gass_transfer_protocol
 * @see #globus_gass_transfer_listener_proto_s */
typedef struct globus_gass_transfer_listener_proto_s
globus_gass_transfer_listener_proto_t;

/**
 * Protocol module function type to handle sending data.
 * @ingroup globus_gass_transfer_protocol
 *
 * A function pointer of this type is associated with the
 * #globus_gass_transfer_request_proto_t associated with a request
 * handle. It is called when client or server has registered a bytes
 * array for sending to the client or server which is handling the request.
 * The GASS Transfer Library will only pass one @a bytes array to the 
 * protocol module for processing per request at any given time.
 *
 * Once the protocol module has processed the array, it must call
 * globus_gass_transfer_proto_send_complete() to let the GASS Transfer
 * library continue to process the request.
 *
 * @param proto
 *        The protocol module's request handler.
 * @param request
 *        The request handle with which this block of bytes is associated.
 * @param bytes
 *        The user-supplied byte array containing the data associated with the
 *        request.
 * @param bytes_length
 *        The length of the @a bytes array.
 * @param last_data
 *        A flag to indicate whether this is the final block of data
 *        for the request. If this is true, then the @a callback
 *        function will be delayed until the server acknowledges that
 *        the file has been completely received.
 * @see globus_gass_transfer_send_bytes()
 */
typedef void
(* globus_gass_transfer_proto_send_t)(
    globus_gass_transfer_request_proto_t *	proto,
    globus_gass_transfer_request_t		request,
    globus_byte_t *				bytes,
    globus_size_t				send_length,
    globus_bool_t				last_data);

/**
 * Protocol module function type to handle receiving data.
 * @ingroup globus_gass_transfer_protocol
 *
 * A function pointer of this type is associated with the
 * #globus_gass_transfer_request_proto_t associated with a request
 * handle. It is called when client or server has registered a bytes
 * array for receiving from the client or server which is handling the request.
 * The GASS Transfer Library will only pass one @a bytes array to the
 * protocol module for processing per request at any given time.
 *
 * Once the protocol module has processed the array, it must call
 * globus_gass_transfer_proto_receive_complete() to let the GASS Transfer
 * library continue to process the request.
 *
 * @param proto
 *        The protocol module's request handler.
 * @param request
 *        The request handle with which this block of bytes is associated.
 * @param bytes
 *        The user-supplied byte array containing the data associated with the
 *        request.
 * @param bytes_length
 *        The length of the @a bytes array.
 * @param wait_for_length
 *        The minimum amount of data to receive before calling
 *        globus_gass_transfer_proto_receive_complete() for
 *        the request. The GASS Transfer protocol module may call
 *        that function with a smaller value for the amount received
 *        if EOF has been reached.
 * @see globus_gass_transfer_receive_bytes()
 */
typedef void
(* globus_gass_transfer_proto_receive_t)(
    globus_gass_transfer_request_proto_t *	proto,
    globus_gass_transfer_request_t		request,
    globus_byte_t *				bytes,
    globus_size_t				bytes_length,
    globus_size_t				wait_for_length);

/**
 * Protocol module implementation function type.
 * @ingroup globus_gass_transfer_protocol
 *
 * Function pointers of this type are associated with the
 * #globus_gass_transfer_request_proto_t associated with a particular
 * request handle. They are called when certain functions which modify
 * the status of a request have been called by a client or server. 
 *
 * A function of this type is used for the fail, deny, refer,
 * authorize, and destroy fields of the #globus_gass_transfer_request_proto_t. 
 * A protocol module can query the request handle to determine the
 * status and, if applicable, denial reasons if necessary.
 *
 * @param proto
 *        The protocol module's request handler.
 * @param request
 *        The request handle.
 */
typedef void
(* globus_gass_transfer_proto_func_t)(
    globus_gass_transfer_request_proto_t *	proto,
    globus_gass_transfer_request_t		request);

/**
 * Protocol module implementation function type for new client requests.
 * @ingroup globus_gass_transfer_protocol
 *
 * A function pointer of this type is associated with the
 * #globus_gass_transfer_proto_descriptor_t for a particular protocol
 * module's implementation. It is called when the client has begun a
 * file transfer request by calling one of the functions in the
 * "@ref globus_gass_transfer_client" section of this manual.
 *
 * When this function is called for a protocol module, the module should query
 * the request handle to determine the URL which is being requested by
 * the client, and the operation being done on that URL. The protocol
 * module should initiate the request, and once it has determined that
 * it has been authorized, denied, or referred, one of 
 * globus_gass_transfer_proto_request_ready(),
 * globus_gass_transfer_proto_request_denied(), or
 * globus_gass_transfer_proto_request_referred() must be called.
 *
 * @param request
 *        The request handle containing the information about the
 *        request.
 * @param attr
 *        A protocol-specific attribute set, created by calling the
 *        protocol module's
 *        @link globus_gass_transfer_proto_descriptor_t::new_requestattr
 *        new_requestattr function pointer@endlink.
 */
typedef void
(* globus_gass_transfer_proto_new_request_t)(
    globus_gass_transfer_request_t		request,
    globus_gass_transfer_requestattr_t *	attr);

/**
 * Protocol module implementation function type for new server listeners.
 * @ingroup globus_gass_transfer_protocol
 *
 * A function pointer of this type is associated with the
 * #globus_gass_transfer_proto_descriptor_t for a particular protocol
 * module's implementation. It is called when the server has called
 * globus_gass_transfer_create_listener().
 *
 * @param listener
 *        The listener handle to associate with the new @a proto
 *        created by the protocol module.
 * @param attr
 *        A protocol-specific attribute set, created by calling the
 *        protocol module's
 *        @link globus_gass_transfer_proto_descriptor_t::new_listenerattr
 *        new_listenerattr function pointer@endlink.
 * @param scheme
 *        The URL scheme that the server has requested for the new listener.
 *        This will be one the scheme associated with a particular
 *        protocol module.
 * @param base_url
 *        A pointer to be set the value of the base url of this listener.
 *        For most protocols, this will contain the scheme, hostname, and
 *        port number of the listener. This string must be allocated using
 *        one of the memory allocators defined in the globus_common library.
 *        It will be freed by the GASS Transfer library when the listener
 *        is closed.
 * @param proto
 *        A pointer to be set to a new globus_gass_transfer_listener_proto_t
 *        which will be associated with this listener. This must be
 *        allocated by the protocol module using one of the memory allocators
 *        defined in the globus_common library. It will be freed by the
 *        GASS Transfer library when the listener is closed.
 *
 * @return A GASS error value, or GLOBUS_SUCCESS.
 */
typedef int
(* globus_gass_transfer_proto_create_listener_t)(
    globus_gass_transfer_listener_t		listener,
    globus_gass_transfer_listenerattr_t *	attr,
    char *					scheme,
    char **					base_url,
    globus_gass_transfer_listener_proto_t **	proto);

/**
 * Protocol module implementation function type for server operations.
 * @ingroup globus_gass_transfer_protocol
 *
 * Function pointers of this type are associated with the
 * #globus_gass_transfer_listener_proto_t associated with a particular
 * listener handle. They are called when a server implementation
 * wants to close the listener, listen for new connections, or
 * destroy the listener.
 *
 * @param proto
 *        The protocol-specific implementation of the
 *        #globus_gass_transfer_listener_proto_t for a particular listener.
 * @param listener
 *        The listener handle associated with the @a proto.
 * @see #globus_gass_transfer_proto_create_listener_t
 */
typedef void
(* globus_gass_transfer_proto_listener_t)(
    globus_gass_transfer_listener_proto_t *	proto,
    globus_gass_transfer_listener_t		listener);

/**
 * Protocol module implementation function type for attribute creation.
 * @ingroup globus_gass_transfer_protocol
 *
 * A function pointer of this type is associated with the 
 * #globus_gass_transfer_proto_descriptor_t defining a protocol module.
 * It is called when a client requests a new request attribute set
 * be created for a URL scheme handled by a protocol module.
 * The function implementation must create a new request attribute
 * usable by the protocol.
 *
 * The returned attribute must be a 
 * globus object which inherits from one of the base attributes defined
 * in the GASS Transfer API. A client or server operation will use a
 * request attribute generated by this function when creating a new
 * #globus_gass_transfer_request_proto_t to handle a request.
 *
 * @param url_scheme
 *        The URL scheme that the request attribute should be compatible
 *        with.
 *
 * @return A globus_object_t-based request attribute.
 *
 * @see #globus_gass_transfer_proto_new_request_t,
 *      #globus_gass_transfer_proto_accept_t
 */
typedef globus_object_t *
(* globus_gass_transfer_proto_new_attr_t)(
    char *					url_scheme);

/**
 * Protocol module implementation function type for server request
 * parsing.
 * @ingroup globus_gass_transfer_protocol
 *
 * Function pointers of this type are associated with the
 * #globus_gass_transfer_listener_proto_t associated with a particular
 * listener handle. They are called when a server implementation
 * wants to accept a new connection from the listener. A new request
 * is generated based on the protocol-specific request done on the
 * new connection.
 *
 * The new request will be created with the attributes
 * specified in the @a attr parameter. Once the protocol module
 * has parsed the request, it must call
 * globus_gass_transfer_proto_new_listener_request() to let the server
 * implementation decide how to process this request.
 *
 * The protocol module should update the @a request to indicate the
 * type of operation being requested, the size of the file (if applicable),
 * and the identity of the client (if applicable).
 *
 * @param proto
 *        The protocol specific listener data structure associated with
 *        the listener handle.
 * @param listener
 *        The listener handle which the user requested the listen on.
 * @param request
 *        The new request handle.
 * @param attr
 *        The request attribute set to be used when processing this
 *        request.
 */
typedef void
(* globus_gass_transfer_proto_accept_t)(
    globus_gass_transfer_listener_proto_t *	proto,
    globus_gass_transfer_listener_t		listener,
    globus_gass_transfer_request_t		request,
    globus_gass_transfer_requestattr_t *	attr);

/**
 * @struct globus_gass_transfer_request_proto_s
 * @brief Protocol module request handling structure.
 *
 * @ingroup globus_gass_transfer_protocol
 * This structure is created by a GASS transfer protocol module to
 * handle a particular request. It is created in response to a
 * @link globus_gass_transfer_listener_proto_s::accept
 * listener's accept method@endlink or a @link
 * globus_gass_transfer_proto_descriptor_t::new_request
 * protocol module's new_request method@endlink.
 *
 * Memory management of this structure is the responsibility of the protocol
 * module. The destroy method will be called when the GASS Transfer library
 * is finished dealing with it.
 *
 * A protocol module may create a extension
 * to this structure to contain protocol-specific information, as long
 * as the first fields of the structure match this type.
 *
 * @see globus_gass_transfer_proto_request_ready()
 */
struct globus_gass_transfer_request_proto_s
{
    /**
     * Send bytes.
     * @see globus_gass_transfer_proto_send_t
     */
    globus_gass_transfer_proto_send_t		send_buffer;
    /**
     * Receive bytes.
     * @see globus_gass_transfer_proto_recv_t
     */
    globus_gass_transfer_proto_receive_t	recv_buffer;


    /**
     * Fail a request.
     *
     * This function is called when the application calls
     * globus_gass_transfer_fail() on a request.
     */
    globus_gass_transfer_proto_func_t		fail;

    /**
     * Deny a request.
     */
    globus_gass_transfer_proto_func_t		deny;
    /**
     * Refer a request.
     */
    globus_gass_transfer_proto_func_t		refer;
    /**
     * Authorize a request.
     */
    globus_gass_transfer_proto_func_t		authorize;

    /**
     * Destroy a request.
     */
    globus_gass_transfer_proto_func_t		destroy;
};

/**
 * @struct globus_gass_transfer_listener_proto_s
 * @brief Protocol module listener handling structure.
 * @ingroup globus_gass_transfer_protocol
 */
struct globus_gass_transfer_listener_proto_s
{
    /** Close listener. */
    globus_gass_transfer_proto_listener_t  	close_listener;
    /** Listen. */
    globus_gass_transfer_proto_listener_t	listen;
    /** Accept */
    globus_gass_transfer_proto_accept_t	 	accept;
    /** Destroy */
    globus_gass_transfer_proto_listener_t	destroy;
};

/**
 * @struct globus_gass_transfer_proto_descriptor_t
 * @brief Protocol module descriptor structure.
 *
 * @ingroup globus_gass_transfer_protocol
 * @see globus_gass_transfer_proto_register_protocol(),
 * globus_gass_transfer_proto_unregister_protocol()
 */
typedef struct
{
    /**
     * URL Scheme.
     *
     * The URL scheme which this protocol module supports. The
     * scheme is the first part of a URL, which names the protocol
     * which is used to access the resource named by the URL, for example
     * "http" or "ftp".
     *
     * The GASS Transfer library allows only one protocol module to be
     * registered to handle a particular @a url_scheme. However, a protocol
     * module may implement only the client or only the server part of the
     * protocol. If a protocol has several variations with different scheme
     * names (for example http and https), each scheme must be registered
     * with GASS in order to be used.
     */
    char *					 url_scheme;

    /**
     * New request attributes.
     *
     * The function pointed to by this pointer is used by GASS to
     * forward requests to create a request attribute for this
     * protocol's @a url_scheme to the protocol module. The function
     * returns a request attribute which inherits from
     * one of the GASS Transfer request attributes.
     *
     * @see globus_gass_transfer_proto_new_attr_t
     */
    globus_gass_transfer_proto_new_attr_t	 new_requestattr;

    /**
     * New request.
     *
     * The function pointed to by this pointer is used by GASS to
     * initiate a new file transfer request by a protocol module.
     * The request handle has been initialized with the parameters
     * passed to one of the functions in the
     * @ref globus_gass_transfer_client section of the GASS Transfer API.
     *
     * The protocol module should begin processing this request by
     * sending appropriate messages to the file server. Once the request
     * is authorized, denied, or referred, the protocol module calls
     * globus_gass_transfer_proto_request_ready(),
     * globus_gass_transfer_proto_request_denied(), or
     * globus_gass_transfer_proto_request_referred().
     *
     * @see globus_gass_transfer_proto_new_request_t
     */
    globus_gass_transfer_proto_new_request_t	 new_request;

    /**
     * New listener attributes.
     *
     * The function pointed to by this pointer is used by GASS to
     * forward requests to create a listener attribute for this
     * protocol's @a url_scheme to the protocol module. The function
     * returns a listener attribute which inherits from
     * one of the GASS Transfer request attributes.
     *
     * @see globus_gass_transfer_proto_new_attr_t
     */
    globus_gass_transfer_proto_new_attr_t	 new_listenerattr;

    /**
     * New listener.
     *
     * The function pointed to by this pointer is used by GASS to
     * create a new listener handle.
     * The listener handle has been initialized with the parameters
     * passed to one of the functions in the
     * @ref globus_gass_transfer_client section of the GASS Transfer API.
     *
     * The protocol module should begin processing this request by
     * sending appropriate messages to the file server. Once the request
     * is authorized, denied, or referred, the protocol module calls
     * globus_gass_transfer_proto_request_ready(),
     * globus_gass_transfer_proto_request_denied(), or
     * globus_gass_transfer_proto_request_referred().
     *
     * @see globus_gass_transfer_proto_new_request_t
     */
     globus_gass_transfer_proto_create_listener_t new_listener;
} globus_gass_transfer_proto_descriptor_t;

#ifndef DOXYGEN
int
globus_gass_transfer_proto_register_protocol(
    globus_gass_transfer_proto_descriptor_t *	proto_desc);

int
globus_gass_transfer_proto_unregister_protocol(
    globus_gass_transfer_proto_descriptor_t *	proto_desc);

void
globus_gass_transfer_proto_request_ready(
    globus_gass_transfer_request_t		request,
    globus_gass_transfer_request_proto_t *	proto);

void
globus_gass_transfer_proto_request_denied(
    globus_gass_transfer_request_t		request,
    int						reason,
    char *					message);

void
globus_gass_transfer_proto_request_referred(
    globus_gass_transfer_request_t		request,
    char **					url,
    globus_size_t				num_urls);

void
globus_gass_transfer_proto_new_listener_request(
    globus_gass_transfer_listener_t		listener,
    globus_gass_transfer_request_t		request,
    globus_gass_transfer_request_proto_t *	proto);

void
globus_gass_transfer_proto_send_complete(
    globus_gass_transfer_request_t		request,
    globus_byte_t *				bytes,
    globus_size_t				nbytes,
    globus_bool_t				failed,
    globus_bool_t				last_data);

void
globus_gass_transfer_proto_receive_complete(
    globus_gass_transfer_request_t		request,
    globus_byte_t *				bytes,
    globus_size_t				nbytes,
    globus_bool_t				failed,
    globus_bool_t				last_data);

void
globus_gass_transfer_proto_listener_ready(
    globus_gass_transfer_listener_t		listener);


/* Protocol Implementation Helper Functions */
/* implemented in globus_gass_transfer_text.c */
void
globus_gass_transfer_crlf_to_lf(
    globus_byte_t *				src,
    globus_size_t 				src_len,
    globus_byte_t **				dst,
    globus_size_t * 				dst_len);

void
globus_gass_transfer_lf_to_crlf(
    globus_byte_t *				src,
    globus_size_t 				src_len,
    globus_byte_t **				dst,
    globus_size_t * 				dst_len);

#endif

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_GASS_PROTO_H */
