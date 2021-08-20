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
 * @file globus_gram_protocol_io.c
 * @brief I/O Functions
 */

#include "globus_i_gram_protocol.h"

#include <string.h>

static int
globus_l_gram_protocol_setup_accept_attr(
    globus_io_attr_t *                          attr,
    globus_i_gram_protocol_connection_t *       connection);

static int
globus_l_gram_protocol_setup_connect_attr(
    globus_io_attr_t *                     attr,
    char *                                 identity);

static
globus_bool_t
globus_l_gram_protocol_authorization_callback(
	void *				arg,
	globus_io_handle_t *		handle,
	globus_result_t			result,
	char *				identity,
	gss_ctx_id_t 			context_handle);

static
void
globus_l_gram_protocol_listen_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result);

static
void
globus_l_gram_protocol_accept_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result);

static
void
globus_l_gram_protocol_close_listener(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result);

static
void
globus_l_gram_protocol_connect_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result);

static
void
globus_l_gram_protocol_write_request_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes);

static
void
globus_l_gram_protocol_read_request_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes);

static
void
globus_l_gram_protocol_write_reply_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes);

static
void
globus_l_gram_protocol_read_reply_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes);

static
void
globus_l_gram_protocol_connection_close_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result);

static
int
globus_l_gram_protocol_parse_request_header(
    const globus_byte_t *		buf,
    globus_size_t *			payload_length,
    char **				uri);

static
int
globus_l_gram_protocol_parse_reply_header(
    const globus_byte_t *		buf,
    globus_size_t *			payload_length);

static
int
globus_l_gram_protocol_reply(
    globus_gram_protocol_handle_t	handle,
    int					code,
    globus_byte_t *			message,
    globus_size_t			message_size,
    gss_OID_set				restriction_oids,
    gss_buffer_set_t			restriction_bufers,
    OM_uint32				req_flags,
    OM_uint32				time_req,
    globus_gram_protocol_delegation_callback_t
    					callback,
    void *				arg);

static
int
globus_l_gram_protocol_post(
    const char *			url,
    globus_gram_protocol_handle_t *	handle,
    globus_io_attr_t *			attr,
    globus_byte_t *			message,
    globus_size_t			message_size,
    globus_bool_t			keep_open,
    gss_cred_id_t			cred_handle,
    gss_OID_set				restriction_oids,
    gss_buffer_set_t			restriction_buffers,
    OM_uint32				req_flags,
    OM_uint32				time_req,
    globus_gram_protocol_callback_t	callback,
    void *				callback_arg);

static
void
globus_l_gram_protocol_delegation_read_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes);

static
void
globus_l_gram_protocol_delegation_write_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes);

static
void
globus_l_gram_protocol_accept_delegation(
    globus_i_gram_protocol_connection_t *
    					connection,
    gss_buffer_t			input_token);

static
void
globus_l_gram_protocol_init_delegation(
    globus_i_gram_protocol_connection_t *
    					connection,
    gss_buffer_t			input_token);

static
void
globus_l_gram_protocol_free_old_credentials();

#endif

/**
 * @defgroup globus_gram_protocol_io Message I/O
 * @ingroup globus_gram_protocol_functions
 * @brief Message I/O
 *
 * @details
 * The functions in this section are related to sending and receiving
 * GRAM protocol messages. 
 */

/**
 * @brief Create a GRAM protocol service listener
 * @ingroup globus_gram_protocol_io
 *
 * @details
 * The globus_gram_protocol_allow_attach() function creates a 
 * GRAM protocol listener to which other processes can send GRAM protocol
 * messages. The listener will automatically accept new connections on it's
 * TCP/IP port and parse GRAM requests.  The requests will be passed to the
 * function pointed to by the @a callback parameter for the
 * application to unpack, handle, and send a reply by calling
 * globus_gram_protocol_reply().
 *
 * @param url
 *     An output parameter that will be initialized to point to a string
 *     that will hold the URL of the new listener. This URL may be published
 *     or otherwise passed to applications which need to contact this
 *     GRAM protocol server. The URL will be of the form
 *     @a https://host:port/. 
 * @param callback
 *     A pointer to a function to be called when a new request has been
 *     received by this listener. This function will be passed the
 *     request, which may be unpacked using one of the functions described 
 *     in the @link globus_gram_protocol_pack message packing @endlink
 *     section of the documentation.
 * @param callback_arg
 *     A pointer to arbitrary user data which will be passed to the callback
 *     function as its first parameter.
 *
 * @return
 *     Upon success, globus_gram_protocol_allow_attach() returns
 *     @a GLOBUS_SUCCESS and modifies the @a url parameter to point to a newly
 *     allocated string. The caller is then responsible for freeing this
 *     string. If an error occurs, an integer error code will be returned and
 *     the @a url parameter value will be uninitialized.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *     Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *     No resources
 *
 * @see globus_gram_protocol_callback_disallow()
 */
int
globus_gram_protocol_allow_attach(
    char **				url,
    globus_gram_protocol_callback_t	callback,
    void *				callback_arg)
{
    int					rc = GLOBUS_SUCCESS;
    char				hostnamebuf[256];
    unsigned short			port;
    globus_result_t			res;
    globus_io_handle_t *		handle;
    globus_i_gram_protocol_listener_t *	listener;
    globus_list_t *			node;

    *url = NULL;

    globus_mutex_lock(&globus_i_gram_protocol_mutex);
    if(globus_i_gram_protocol_shutdown_called)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST;

	goto error_exit;
    }

    handle = globus_libc_malloc(sizeof(globus_io_handle_t));
    if(handle == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto error_exit;
    }

    globus_libc_gethostname(hostnamebuf, 256);
    port = 0;

    res = globus_io_tcp_create_listener(&port,
                                        -1,
					&globus_i_gram_protocol_default_attr,
					handle);
    if(res != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;

	goto free_handle_exit;
    }

    listener = globus_libc_malloc(sizeof(globus_i_gram_protocol_listener_t));
    if(listener == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto close_handle_exit;
    }
    listener->port = port;
    listener->allow_attach = GLOBUS_TRUE;
    listener->handle = handle;
    listener->callback = callback;
    listener->callback_arg = callback_arg;
    listener->connection_count = 0;
    globus_cond_init(&listener->cond, NULL);

    globus_list_insert(&globus_i_gram_protocol_listeners, listener);

    res = globus_io_tcp_register_listen(handle,
                                        globus_l_gram_protocol_listen_callback,
					listener);
    if(res != GLOBUS_SUCCESS)
    {
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;

	goto remove_listener_exit;
    }
    listener->listen_registered = GLOBUS_TRUE;
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);
    (*url) = globus_libc_malloc(17 + strlen(hostnamebuf));

    if((*url) == NULL)
    {
        goto remove_listener_exit;
    }
    sprintf(*url, "https://%s:%hu/", hostnamebuf, port);

    return GLOBUS_SUCCESS;

  remove_listener_exit:
    node = globus_list_search(globus_i_gram_protocol_listeners,
                              listener);
    if(node)
    {
        globus_list_remove(&globus_i_gram_protocol_listeners,
	                   node);
    }

    globus_libc_free(listener);

  close_handle_exit:
    res = globus_io_register_close(handle,
                                   globus_l_gram_protocol_close_listener,
				   NULL);
    if(res != GLOBUS_SUCCESS)
    {
  free_handle_exit:
        globus_libc_free(handle);
    }
  error_exit:
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);

    return rc;
}
/* globus_gram_protocol_allow_attach() */

/**
 * @brief Stop a GASS protocol listener from handling new requests
 * @ingroup globus_gram_protocol_io
 *
 * @details
 * The globus_gram_protocol_callback_disallow() function stops the
 * listener named by the value of the @a url parameter from receiving any
 * new requests. It also frees memory used internally by the GRAM protocol
 * implementation to handle requests for this listener.
 *
 * The globus_gram_protocol_callback_disallow() function will wait until
 * all requests being processed by this listener have completed processing.
 * Once globus_gram_protocol_callback_disallow() returns, no further
 * request callbacks will occur for the listener.
 *
 * @param url
 *     A pointer to the URL string which names the listener to disable.
 *
 * @return
 *     Upon success, the globus_gram_protocol_callback_disallow() function
 *     returns @a GLOBUS_SUCCESS and frees internal state associated with the
 *     listener named by the @a url parameter. If an error occurs, its integer
 *     error code value will be returned and no listener will be affected.
 *     
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *     Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_CALLBACK_NOT_FOUND
 *     Callback not found
 *
 * @see globus_gram_protocol_allow_attach()
 */
int
globus_gram_protocol_callback_disallow(
    char *				url)
{
    int					rc;
    globus_list_t *			list;
    globus_i_gram_protocol_listener_t *	listener;
    globus_io_handle_t *		handle;
    globus_url_t			parsed_url;
    unsigned short			port;

    /* get port number from url---we'll use that as a key
     * to locate the listener
     */
    rc = globus_url_parse(url, &parsed_url);
    if(rc == GLOBUS_SUCCESS)
    {
        port = parsed_url.port;
	globus_url_destroy(&parsed_url);
    }
    else
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT;
    }

    globus_mutex_lock(&globus_i_gram_protocol_mutex);

    /* Locate listener with matching port number */
    handle = NULL;
    list = globus_i_gram_protocol_listeners;
    while(!handle && !globus_list_empty(list))
    {
        listener = globus_list_first(list);

	if(listener->port == port)
	{
	    handle = listener->handle;
	}
	else
	{
	    list = globus_list_rest(list);
	}
    }
    if(handle)
    {
        rc = globus_i_gram_protocol_callback_disallow(listener);
    }
    else
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_CALLBACK_NOT_FOUND;
    }

    globus_mutex_unlock(&globus_i_gram_protocol_mutex);

    return rc;
}
/* globus_gram_protocol_callback_disallow() */


/**
 * @brief Post a GRAM protocol request to a GRAM server
 * @ingroup globus_gram_protocol_io
 *
 * @details
 * The globus_gram_protocol_post() function initiates a GRAM protocol
 * message exchange with a GRAM protocol listener. It returns after framing
 * the message and initiating the connection. When the message exchange is
 * complete, the function pointed to by @a callback is invoked either in
 * another thread or when a non-threaded application calls the 
 * globus_poll() or globus_cond_wait() functions.
 *
 * @param url
 *     A pointer to a string containing the URL of the server to post the
 *     request to. This URL must be an HTTPS URL naming a GRAM service
 *     resource.
 * @param handle
 *     A pointer to a @a globus_gram_protocol_handle_t which
 *     will be initialized with a unique handle identifier. This
 *     identifier will be passed to the @a callback function to 
 *     allow the caller to differentiate replies to multiple GRAM Protocol
 *     requests. This pointer may be NULL if the caller will not have
 *     multiple simultaneous requests.
 * @param attr
 *     A pointer to a Globus I/O attribute set, which will be used as
 *     parameters when connecting to the GRAM server. The value of
 *     @a attr may be NULL, in which case, the default GRAM Protocol
 *     attributes will be used (authentication to self, SSL-compatible
 *     transport, with message integrity).
 * @param message
 *     A pointer to a message string to be sent to the GRAM server. This is
 *     normally created by calling one of the GRAM Protocol 
 *     @link globus_gram_protocol_pack pack @endlink functions. This
 *     message need not be NULL terminated as the length is passed in
 *     the @a message_size parameter. 
 * @param message_size
 *     The length of the @a message string. Typically generated as one of
 *     the output parameters to one of the GRAM Protocol 
 *     @link globus_gram_protocol_pack pack @endlink functions.
 * @param callback
 *     A pointer to a function to call when the response to this
 *     message is received or the message exchange fails. This may be NULL,
 *     in which case no callback will be received, and the caller will be
 *     unable to verify whether the message was successfully received.
 * @param callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a callback as its first parameter. This may
 *     be NULL if the application has a NULL @a callback or does not require
 *     the pointer to establish its context in the callback.
 *
 * @return
 *    Upon success, globus_gram_protocol_post() returns GLOBUS_SUCCESS,
 *    initiates the message exchange, registers the function pointed to by
 *    @a callback to be called when the exchange completes or fails, and
 *    modifies the @a handle parameter if it is non-NULL. If an
 *    error occurs, its error code will be returned, the @a handle parameter
 *    will be uninitialized and the function pointed to be @a callback will
 *    not be called.
 *
 * @retval GLOBUS_SUCCESS
 *    Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *    Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 *
 * @note
 * There is no way to time out or cancel a service request that is
 * begun with globus_gram_protocol_post().
 *
 * @see globus_gram_protocol_reply()
 */
int
globus_gram_protocol_post(
    const char *                        url,
    globus_gram_protocol_handle_t *     handle,
    globus_io_attr_t *                  attr,
    globus_byte_t *                     message,
    globus_size_t                       message_size,
    globus_gram_protocol_callback_t     callback,
    void *                              callback_arg)
{
    return globus_l_gram_protocol_post(
	url,
	handle,
	attr,
	message,
	message_size,
	GLOBUS_FALSE,
	GSS_C_NO_CREDENTIAL,
	GSS_C_NO_OID_SET,
	GSS_C_NO_BUFFER_SET,
	0,
	0,
	callback,
	callback_arg);
}
/* globus_gram_protocol_post() */


/**
 * @brief Post a GRAM protocol delegation request to a GRAM server
 * @ingroup globus_gram_protocol_io
 *
 * @details
 * The globus_gram_protocol_post_delegation() function initiates a GRAM
 * protocol delegation exchange with a GRAM protocol listener. The delegation
 * protocol is a custom mix of HTTP and SSL records.
 *
 * The globus_gram_protocol_post_delegation() function returns after framing
 * the message and initiating the connection to be used for delegation. When
 * the message exchange is complete, the function pointed to by @a callback is
 * invoked either in another thread or when a non-threaded application calls
 * the globus_poll() or globus_cond_wait() functions.
 *
 * @param url
 *     A pointer to a string containing the URL of the server to post the
 *     request to. This URL must be an HTTPS URL naming a GRAM service
 *     resource.
 * @param handle
 *     A pointer to a @a globus_gram_protocol_handle_t which
 *     will be initialized with a unique handle identifier. This
 *     identifier will be passed to the @a callback function to 
 *     allow the caller to differentiate replies to multiple GRAM Protocol
 *     requests. This pointer may be NULL if the caller will not have
 *     multiple simultaneous requests.
 * @param attr
 *     A pointer to a Globus I/O attribute set, which will be used as
 *     parameters when connecting to the GRAM server. The value of
 *     @a attr may be NULL, in which case, the default GRAM Protocol
 *     attributes will be used (authentication to self, SSL-compatible
 *     transport, with message integrity).
 * @param message
 *     A pointer to a message string to be sent to the GRAM server. This is
 *     normally created by calling one of the GRAM Protocol 
 *     @link globus_gram_protocol_pack pack @endlink functions. This
 *     message need not be NULL terminated as the length is passed in
 *     the @a message_size parameter. 
 * @param message_size
 *     The length of the @a message string. Typically generated as one of
 *     the output parameters to one of the GRAM Protocol 
 *     @link globus_gram_protocol_pack pack @endlink functions.
 * @param cred_handle
 *     Handle to an existing GSSAPI security credential. If this parameter
 *     is set to @a GSS_C_NO_CREDENTIAL, then the current account's default
 *     credential will be used. A proxy credential sharing the identity of this
 *     credential will be delegated to the GRAM protocol server.
 * @param restriction_oids
 *     A set of OID values indicating the data in the @a restriction_buffers
 *     parameter. This parameter may have the value GSS_C_NO_OID_SET if there
 *     are no restriction buffers.
 * @param restriction_buffers
 *     A set of binary data buffers which will be included in the delegated
 *     credential. The type of data in these buffers is determined by the
 *     OID values in @a restriction_oids. This parameter may have the value
 *     GSS_C_EMPTY_BUFFER_SET if there are no extra restrictions to be 
 *     added to the credential.
 * @param req_flags
 *     A bitwise-or of GSSAPI flag values to use when delegating the
 *     credential using gss_init_delegation().
 * @param time_req
 *     An integer value indicating the length of time (in seconds) that the
 *     delegated credential should be valid for. This is an advisory parameter:
 *     no error will be returned if a credential with the requested lifetime
 *     can not be created.
 * @param callback
 *     A pointer to a function to call when the response to this
 *     message is received or the message exchange fails. This may be NULL,
 *     in which case no callback will be received, and the caller will be
 *     unable to verify whether the message was successfully received.
 * @param callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a callback as its first parameter. This may
 *     be NULL if the application has a NULL @a callback or does not require
 *     the pointer to establish its context in the callback.
 *
 * @return
 *    Upon success, globus_gram_protocol_post_delegation() returns
 *    GLOBUS_SUCCESS, initiates the message exchange, registers the function
 *    pointed to by @a callback to be called when the exchange completes or
 *    fails, and modifies the @a handle parameter if it is non-NULL. If an
 *    error occurs, its error code will be returned, the @a handle parameter
 *    will be uninitialized and the function pointed to be @a callback will
 *    not be called. In the case of a protocol or delegation failure, the
 *    callback function will be called with the @a errorcode parameter
 *    set to the error.
 *
 * @retval GLOBUS_SUCCESS
 *    Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *    Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 *
 * @note
 * There is no way to time out or cancel a service request that is
 * begun with globus_gram_protocol_post_delegation().
 *
 * @see globus_gram_protocol_reply()
 */
int
globus_gram_protocol_post_delegation(
    const char *			url,
    globus_gram_protocol_handle_t *	handle,
    globus_io_attr_t *			attr,
    globus_byte_t *			message,
    globus_size_t			message_size,
    gss_cred_id_t			cred_handle,
    gss_OID_set				restriction_oids,
    gss_buffer_set_t			restriction_buffers,
    OM_uint32				req_flags,
    OM_uint32				time_req,
    globus_gram_protocol_callback_t	callback,
    void *				callback_arg)
{
    return globus_l_gram_protocol_post(
	url,
	handle,
	attr,
	message,
	message_size,
	GLOBUS_TRUE,
	cred_handle,
	restriction_oids,
	restriction_buffers,
	req_flags,
	time_req,
	callback,
	callback_arg);
}
/* globus_gram_protocol_post_delegation() */

/**
 * @brief Reply to a GRAM protocol message
 * @ingroup globus_gram_protocol_io
 *
 * @details
 * The @a #globus_gram_protocol_reply() function sends a response message
 * to a client which initiated a GRAM message exchange. The
 * @a #globus_gram_protocol_reply() function composes the message with an 
 * HTTP message frame and then sends it to the client which initiated the
 * exchange.
 * 
 * @param handle
 *    A GRAM protocol handle which is used by this function to determine
 *    the network connection to use for this reply. This must be the same
 *    value as was passed as a parameter to the callback function registered
 *    with the @a #globus_gram_protocol_allow_attach() function.
 * @param code
 *    The HTTP response code. The code should be one from the set described
 *    in RFC 2616.
 * @param message
 *     A pointer to a message string to be sent to the GRAM client. This is
 *     normally created by calling one of the GRAM Protocol 
 *     @link globus_gram_protocol_pack pack @endlink functions. This
 *     message need not be NULL terminated as the length is passed in
 *     the @a message_size parameter. 
 * @param message_size
 *     The length of the @a message string. Typically generated as one of
 *     the output parameters to one of the GRAM Protocol 
 *     @link globus_gram_protocol_pack pack @endlink functions.
 *
 * @return
 *     Upon success, @a #globus_gram_protocol_reply() returns GLOBUS_SUCCESS,
 *     frames the @a message with an HTTP header and initiates sending the
 *     message to the client. The caller must not try to use the value of the
 *     @a handle parameter after this function returns. If an error occurs,
 *     its integer error code will be returned.
 *    
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *     Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *     No Resources
 *
 * @see #globus_gram_protocol_allow_attach()
 */
int
globus_gram_protocol_reply(
    globus_gram_protocol_handle_t       handle,
    int					code,
    globus_byte_t *			message,
    globus_size_t			message_size)
{
    return globus_l_gram_protocol_reply(handle,
	  				code,
					message,
					message_size,
					GSS_C_NO_OID_SET,
					GSS_C_NO_BUFFER_SET,
					0,
					0,
					NULL,
					NULL);
}
/* globus_gram_protocol_reply() */

/**
 * @brief Perform the server-side of the GSSAPI delegation handshake to receive a new delegated credential
 * @ingroup globus_gram_protocol_io
 *
 * @details
 * The globus_gram_protocol_accept_delegation() function performs the
 * service side accepting of a GRAM protocol delegation exchange with a GRAM
 * protocol client. This is performed after the delegation HTTP message has
 * been unpacked by the application. 
 *
 * The globus_gram_protocol_accept_delegation() function returns after
 * processing the GSSAPI handshake, passing the delegated credential or
 * error information to the function pointed to by the @a callback parameter.
 *
 * @param handle
 *     A GRAM protocol handle on which the server received a protocol refresh
 *     message.
 * @param restriction_oids
 *     A set of OID values indicating the data in the @a restriction_buffers
 *     parameter. This parameter may have the value GSS_C_NO_OID_SET if there
 *     are no restriction buffers.
 * @param restriction_buffers
 *     A set of binary data buffers which will be included in the delegated
 *     credential. The type of data in these buffers is determined by the
 *     OID values in @a restriction_oids. This parameter may have the value
 *     GSS_C_EMPTY_BUFFER_SET if there are no extra restrictions to be 
 *     added to the credential.
 * @param req_flags
 *     A bitwise-or of GSSAPI flag values to use when delegating the
 *     credential using gss_init_delegation().
 * @param time_req
 *     An integer value indicating the length of time (in seconds) that the
 *     delegated credential should be valid for. This is an advisory parameter:
 *     no error will be returned if a credential with the requested lifetime
 *     can not be created.
 * @param callback
 *     A pointer to a function to call when the delegation handshake has
 *     completed or failed. This function will be passed the value of
 *     @a arg as well as the handle and delegated credential or error
 *     that occurred processing the delegation messages.
 * @param arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a callback as its first parameter. This may
 *     be NULL if the application has a NULL @a callback or does not require
 *     the pointer to establish its context in the callback.
 *
 * @return
 *     Upon success, globus_gram_protocol_accept_delegation() returns 
 *     GLOBUS_SUCCESS and registers the function pointed to by @a callback 
 *     to be called after the delegation completes or fails. If an error
 *     occurs, globus_gram_protocol_accept_delegation() returns an integer
 *     error code and the @a callback function is not registered.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_MALLOC_FAILED
 *     Malloc failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *     Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *     No resources
 */
int
globus_gram_protocol_accept_delegation(
    globus_gram_protocol_handle_t       handle,
    gss_OID_set				restriction_oids,
    gss_buffer_set_t			restriction_buffers,
    OM_uint32				req_flags,
    OM_uint32				time_req,
    globus_gram_protocol_delegation_callback_t
    					callback,
    void *				arg)
{
    int					rc;
    globus_byte_t * 			reply;
    globus_size_t			replysize;

    rc = globus_gram_protocol_pack_status_reply(
	    0,
	    0,
	    0,
	    &reply,
	    &replysize);

    rc = globus_l_gram_protocol_reply(handle,
	  				200,
					reply,
					replysize,
					restriction_oids,
					restriction_buffers,
					req_flags,
					time_req,
					callback,
					arg);
    globus_libc_free(reply);
    return rc;
}
/* globus_gram_protocol_accept_delegation() */

/**
 * @brief Get a reference to the GSSAPI security context associated with a GRAM protocol handle
 * @ingroup globus_gram_protocol_io
 *
 * @details
 * The @a #globus_gram_protocol_get_sec_context() function retrieves a
 * reference to the GSSAPI security context associated with a particular GRAM
 * protocol handle. This context may be inspected by the caller but must not be
 * destroyed by the caller.  The @a #globus_gram_protocol_get_sec_context()
 * function must only be called after the GRAM protocol library has called
 * the @a callback function associated with a GRAM protocol message exchange.
 *
 * @param handle
 *     The GRAM protocol handle associated with a GRAM protocol message
 *     exchange.
 * @param context
 *     The GSSAPI security context associated with the protocol handle.
 *
 * @return
 *     Upon success, @a #globus_gram_protocol_get_sec_context() returns
 *     GLOBUS_SUCCESS and modifies the @a context parameter to point to the
 *     security context associated with the @a handle parameter. If an error
 *     occurs, an integer error code is returned and the value of the
 *     @a context parameter is undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *     Invalid request
 */
int
globus_gram_protocol_get_sec_context(
    globus_gram_protocol_handle_t       handle,
    gss_ctx_id_t *                      context)
{
    globus_i_gram_protocol_connection_t *
    					connection;
    globus_list_t *			list;
    int					rc;

    list = globus_i_gram_protocol_connections;
    while(list != NULL)
    {
        connection = globus_list_first(list);
	if(connection->handle == handle)
	{
	    break;
	}
	list = globus_list_rest(list);
    }

    if(list == NULL)
    {
	/* No match */
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST;
	goto error_exit;
    }

    *context = connection->context;

    if(*context == GSS_C_NO_CONTEXT)
    {
	/* No context */
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST;
	goto error_exit;        
    }

    return GLOBUS_SUCCESS;
    
 error_exit:
    return rc;
}
/* globus_gram_protocol_get_sec_context() */
  
#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * Listen callback.
 *
 * This function is called when Globus I/O decides that a connection is
 * ready to be accepted on a listening TCP socket. If the gram protocol module
 * has not been deactivated, then we create a new connection handle to
 * register the accept on. Otherwise, we register the close of this listener.  
 * 
 * @param callback_arg
 *        The globus_i_gram_protocol_listener_t associated with this
 *        Globus I/O handle.
 * @param handle
 *        The Globus I/O handle which is ready for accepting a new connection.
 * @param result
 *        The result of the listen attempt. This is usally GLOBUS_SUCCESS,
 *        unless the listener handle is being closed, or something is
 *        very wrong.
 */
static
void
globus_l_gram_protocol_listen_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result)
{
    globus_i_gram_protocol_listener_t *	listener;
    globus_i_gram_protocol_connection_t *
    					connection;
    globus_list_t *			node;
    globus_io_attr_t                    accept_attrs;
    
    listener = callback_arg;
    listener->listen_registered = GLOBUS_FALSE;

    globus_mutex_lock(&globus_i_gram_protocol_mutex);
    if(globus_i_gram_protocol_shutdown_called || !listener->allow_attach)
    {
        goto error_exit;
    }
    if(result != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }
    connection = globus_libc_calloc(
		    1,
		    sizeof(globus_i_gram_protocol_connection_t));

    if(connection == NULL)
    {
        goto error_exit;
    }
    connection->read_type = GLOBUS_GRAM_PROTOCOL_REQUEST;
    connection->callback = listener->callback;
    connection->callback_arg = listener->callback_arg;
    connection->io_handle = globus_libc_malloc(sizeof(globus_io_handle_t));
    if(connection->io_handle == NULL)
    {
        goto free_connection_exit;
    }
    connection->listener = listener;
    connection->handle = ++globus_i_gram_protocol_handle;
    connection->accepting = GLOBUS_TRUE;
    globus_list_insert(&globus_i_gram_protocol_connections, connection);
    listener->connection_count++;

    result = globus_io_tcp_get_attr(listener->handle,
                                    &accept_attrs);

    if(result != GLOBUS_SUCCESS)
    {
        goto free_io_handle_exit;
    }    

    if(globus_l_gram_protocol_setup_accept_attr(&accept_attrs, connection))
    {
        goto free_attrs_exit;
    }
    
    result = globus_io_tcp_register_accept(
                    listener->handle,
		    &accept_attrs,
		    connection->io_handle,
		    globus_l_gram_protocol_accept_callback,
		    connection);
    if(result != GLOBUS_SUCCESS)
    {
        goto free_attrs_exit;
    }
    globus_io_tcpattr_destroy(&accept_attrs);

    if ((!globus_i_gram_protocol_shutdown_called) &&
        listener->allow_attach &&
        listener->listen_registered == GLOBUS_FALSE &&
        listener->connection_count < globus_i_gram_protocol_max_concurrency)
    {
        /* If this fails, not much we can do. Disallow attach will
         * be called eventually to clean this listener up.
         */
        result = globus_io_tcp_register_listen(
                listener->handle,
                globus_l_gram_protocol_listen_callback,
                listener);
        if (result == GLOBUS_SUCCESS)
        {
            listener->listen_registered = GLOBUS_TRUE;
        }
    }
    else
    {
        listener->listen_registered = GLOBUS_FALSE;
    }

    globus_mutex_unlock(&globus_i_gram_protocol_mutex);
    return;

  free_attrs_exit:
    globus_io_tcpattr_destroy(&accept_attrs);
    
  free_io_handle_exit:
    listener->connection_count--;
    node = globus_list_search(globus_i_gram_protocol_connections, connection);
    if(node)
    {
	globus_list_remove(&globus_i_gram_protocol_connections, node);
    }
    globus_libc_free(connection->io_handle);

  free_connection_exit:
    globus_libc_free(connection);

  error_exit:
    if((!globus_i_gram_protocol_shutdown_called) &&
            listener->allow_attach &&
            (!listener->listen_registered) &&
            listener->connection_count < globus_i_gram_protocol_max_concurrency)
    {
        result = globus_io_tcp_register_listen(
                listener->handle,
                globus_l_gram_protocol_listen_callback,
                listener);
        if (result == GLOBUS_SUCCESS)
        {
            listener->listen_registered = GLOBUS_TRUE;
        }
        else
        {
            listener->listen_registered = GLOBUS_FALSE;
        }
    }
    else
    {
        listener->listen_registered = GLOBUS_FALSE;
    }
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);
}
/* globus_l_gram_protocol_listen_callback() */

/**
 * Close callback used when globus_io_tcp_register_listen() fails.
 *
 * Frees the handle.
 *
 * @param callback_arg
 *        This parameter should always be NULL. It is ignored.
 * @param handle
 *        A pointer to the TCP listener Globus I/O handle which is being
 *        closed. This handle will be freed during this function.
 * @param result
 *        The result of the attempt to close the listener. This is ignored.
 */
static
void
globus_l_gram_protocol_close_listener(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result)
{
    globus_libc_free(handle);

    return;
}
/* globus_l_gram_protocol_close_listener() */

/**
 * Handle a new GRAM protocol connection.
 *
 * Allocates a buffer to read in a new GRAM protocol request on the
 * new connection and register a read on that new connection. If an error
 * occurs or the module is being deactivated, then the
 * connection will be closed and no user callbacks will occur.
 *
 * @param callback_arg
 *        A pointer to the
 *        @link globus_i_gram_protocol_connection_t connection @endlink
 *        structure for this request. This should never be NULL.
 * @param handle
 *        The Globus I/O handle associated with the connection.
 * @param result
 *        The result of the creation of the new connection.
 */
static
void
globus_l_gram_protocol_accept_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result)
{
    globus_i_gram_protocol_connection_t *
    					connection;
    int					rc;

    connection = callback_arg;

    globus_mutex_lock(&globus_i_gram_protocol_mutex);
    connection->accepting = GLOBUS_FALSE;

    if(globus_i_gram_protocol_shutdown_called)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST;
        goto error_exit;
    }
    if(result)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;
	goto error_exit;
    }
    connection->buf =
        globus_libc_malloc(GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE);
    connection->bufsize = GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE;

    if(connection->buf == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto error_exit;
    }
    connection->replybufsize = GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE;

    result = globus_io_register_read(
                 connection->io_handle,
                 connection->buf,
		 connection->bufsize,
		 1,
		 globus_l_gram_protocol_read_request_callback,
		 connection);
    if(result)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;
        goto error_exit;
    }
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);

    return;

  error_exit:
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);

    /* We don't have to call the user's callback on this one, since
     * an accept without a request isn't useful to the caller.
     */

    result = globus_io_register_close(
	    handle,
	    globus_l_gram_protocol_connection_close_callback,
	    callback_arg);

    if(result != GLOBUS_SUCCESS)
    {
        /* If we can't close the handle, we'd still like to clean up
	 * our memory.
	 */
	globus_l_gram_protocol_connection_close_callback(
	    callback_arg,
	    handle,
	    result);
    }
}
/* globus_l_gram_protocol_accept_callback() */

/**
 * Server read callback.
 *
 * Reads a GRAM Protocol request and unframes the message from the HTTP
 * headers. This callback may be called multiple times during a connection if
 * the whole message is not available at one time.
 *
 * @param callback_arg
 *        A pointer to the
 *        @link globus_i_gram_protocol_connection_t connection @endlink
 *        structure for this request. This should never be NULL.
 * @param handle
 *        The Globus I/O handle associated with the connection.
 * @param result
 *        The result of the creation of the new connection.
 * @param buf
 *        A pointer to the location in the connection's buffer that this
 *        part read has been begun with.
 * @param nbytes
 *        The amount of data read during this read.
 */
static
void
globus_l_gram_protocol_read_request_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes)
{
    globus_object_t *			err;
    globus_i_gram_protocol_connection_t *
    					connection;
    char *				p;
    int					rc;
    globus_size_t			header_length;

    connection = callback_arg;

    if(result != GLOBUS_SUCCESS)
    {
        err = globus_error_get(result);

	if(!globus_io_eof(err) || !connection->got_header)
	{
	    globus_object_free(err);

	    goto error_exit;
	}
    }
    if(!connection->got_header)
    {
        if(connection->n_read == 0 && ((*buf == '0') || (*buf == 'D')))
	{
	    /* Delegation packet? */

	    goto reregister_read;
	}

	connection->n_read += nbytes;
	connection->buf[connection->n_read] = '\0';

	p = strstr((const char *)connection->buf, CRLF CRLF);
	header_length = (const char *)p - (const char *)connection->buf;

	if(p)
	{
	    connection->got_header = GLOBUS_TRUE;

	    rc = globus_l_gram_protocol_parse_request_header(
	             connection->buf,
		     &connection->payload_length,
		     &connection->uri);
	    if(rc != GLOBUS_SUCCESS)
	    {
	        goto error_exit;
	    }
	    /* p + 4 is the beginning of the payload (after CRLF CRLF) */
	    memmove(connection->buf,
		    p + 4,
		    connection->n_read - header_length - 4);
	    connection->n_read = connection->n_read - header_length - 4;
	    connection->buf[connection->n_read] = '\0';
	    nbytes = 0;
	}
	else
	{
	    goto reregister_read;
	}
    }
    if(connection->got_header)
    {
        if(connection->n_read < connection->payload_length)
	{
	    goto reregister_read;
	}
	/* Call user callback... users should not free the
	 * buffers, unlike the original code.
	 */
	if(connection->callback)
	{
	    connection->callback(connection->callback_arg,
				 connection->handle,
				 connection->buf,
				 connection->payload_length,
				 GLOBUS_SUCCESS,
				 connection->uri);
	}
    }
    return;

  reregister_read:
    result = globus_io_register_read(
                 connection->io_handle,
		 connection->buf + connection->n_read,
		 connection->bufsize - connection->n_read,
		 1,
		 globus_l_gram_protocol_read_request_callback,
		 connection);
    if(result == GLOBUS_SUCCESS)
    {
	return;
    }

  error_exit:
    result = globus_io_register_close(
                 connection->io_handle,
		 globus_l_gram_protocol_connection_close_callback,
		 connection);

    if(result != GLOBUS_SUCCESS)
    {
        /* If we can't close the handle, we'd still like to clean up
	 * our memory.
	 */
	globus_l_gram_protocol_connection_close_callback(
	    callback_arg,
	    handle,
	    result);
    }
}
/* globus_l_gram_protocol_read_request_callback() */

/**
 * Begin sending a GRAM Protocol message.
 *
 * After the connection has been established, register the
 * write of the framed GRAM Protocol request on the newly established
 * TCP/IP connection.  If an error occurs,
 * then the connection will be closed and a user callbacks will be called
 * with an error code.
 *
 * @param callback_arg
 *        A pointer to the
 *        @link globus_i_gram_protocol_connection_t connection @endlink
 *        structure for this request. This should never be NULL.
 * @param handle
 *        The Globus I/O handle associated with the connection.
 * @param result
 *        The result of the establishment of this new connection.
 */
static
void
globus_l_gram_protocol_connect_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result)
{
    globus_object_t *			err;
    int					rc = 0;
    char *				errstring;
    globus_i_gram_protocol_connection_t *
    					connection;
    connection = callback_arg;
    globus_mutex_lock(&globus_i_gram_protocol_mutex);
    connection->accepting = GLOBUS_FALSE;
    if(result != GLOBUS_SUCCESS)
    {
	err = globus_error_get(result);

	if(globus_object_type_match(
	       globus_object_get_type(err),
	       GLOBUS_IO_ERROR_TYPE_SECURITY_FAILED))
	{
	    errstring = globus_error_print_friendly(err);
	    rc = GLOBUS_GRAM_PROTOCOL_ERROR_AUTHORIZATION;
	    globus_gram_protocol_error_7_hack_replace_message(errstring);
	    globus_free(errstring);
	}
	else
	{
	    rc = GLOBUS_GRAM_PROTOCOL_ERROR_CONNECTION_FAILED;
	}
	result = globus_error_put(err);
        goto error_exit;
    }

    /* Write the framed GRAM request */
    result = globus_io_register_write(
                 handle,
		 connection->buf,
		 connection->bufsize,
		 globus_l_gram_protocol_write_request_callback,
		 connection);

    if(result != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);

    return;

  error_exit:
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);

    /*
     * Since the user requested the POST with a callback, they
     * should get their callback
     */
    if(connection->callback)
    {
	connection->callback(connection->callback_arg,
			     connection->handle,
			     NULL,
			     0,
			     rc,
			     NULL);
    }
    
    result = globus_io_register_close(
	handle,
	globus_l_gram_protocol_connection_close_callback,
	callback_arg);
    
    if(result != GLOBUS_SUCCESS)
    {
        /* If we can't close the handle, we'd still like to clean up
	 * our memory.
	 */
	globus_l_gram_protocol_connection_close_callback(
	    callback_arg,
	    handle,
	    result);
    }
    return;
}
/* globus_l_gram_protocol_connect_callback() */

/**
 * Complete sending a GRAM Protocol request.
 *
 * After Globus I/O has completed writing the GRAM request, register
 * a read of the reply to this request. If an error occurs,
 * then the connection will be closed and a user callbacks will be called
 * with an error code.
 *
 * @param callback_arg
 *        A pointer to the
 *        @link globus_i_gram_protocol_connection_t connection @endlink
 *        structure for this request. This should never be NULL.
 * @param handle
 *        The Globus I/O handle associated with the connection.
 * @param result
 *        The result of writing the message.
 * @param buf
 *        The message buffer which was sent to the GRAM Protocol server.
 * @param nbytes
 *        The number of bytes of @a buf written.
 */
static
void
globus_l_gram_protocol_write_request_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes)
{
    globus_i_gram_protocol_connection_t *
    					connection;
    int					rc;

    connection = callback_arg;
    globus_mutex_lock(&globus_i_gram_protocol_mutex);
    if(result != GLOBUS_SUCCESS)
    {
        globus_object_t *               err;
        char *                          errstring;

	err = globus_error_get(result);

        errstring = globus_error_print_friendly(err);
        globus_gram_protocol_error_10_hack_replace_message(errstring);
        globus_free(errstring);
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
        globus_object_free(err);

        goto error_exit;
    }
    /* Allocate reply buffer */
    connection->replybuf = globus_libc_malloc(
                               GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE);
    connection->replybufsize = GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE;

    if(connection->replybuf == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto error_exit;
    }
    /* Read reply from server */
    result = globus_io_register_read(connection->io_handle,
				     connection->replybuf,
				     connection->replybufsize,
				     1,
				     globus_l_gram_protocol_read_reply_callback,
				     connection);

    if(result)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;
        goto error_exit;
    }
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);

    return;

  error_exit:
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);

    if(connection->callback)
    {
	/*
	 * Since the user requested the POST with a callback, they
	 * should get their callback
	 */
	connection->callback(connection->callback_arg,
			     connection->handle,
			     NULL,
			     0,
			     rc,
			     NULL);
    }
    result = globus_io_register_close(
	    handle,
	    globus_l_gram_protocol_connection_close_callback,
	    callback_arg);

    if(result != GLOBUS_SUCCESS)
    {
        /* If we can't close the handle, we'd still like to clean up
	 * our memory.
	 */
	globus_l_gram_protocol_connection_close_callback(
	    callback_arg,
	    handle,
	    result);
    }
    return;
}
/* globus_l_gram_protocol_write_request_callback() */

/**
 * Complete replying to a GRAM Protocol request.
 *
 * After Globus I/O has completed writing a normal GRAM reply, register
 * a close of the connection handle. In the case of a proxy refresh reply,
 * we will start accepting the delegated credential.
 *
 * If an error occurs, then the connection will be closed. There is no user
 * callback associated with a normal reply.
 *
 * @param callback_arg
 *        A pointer to the
 *        @link globus_i_gram_protocol_connection_t connection @endlink
 *        structure for this reply. This should never be NULL.
 * @param handle
 *        The Globus I/O handle associated with the connection.
 * @param result
 *        The result of writing the reply.
 * @param buf
 *        The reply buffer which was sent to the GRAM Protocol server.
 * @param nbytes
 *        The number of bytes of @a buf written.
 * 
 */
static
void
globus_l_gram_protocol_write_reply_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes)
{
    globus_i_gram_protocol_connection_t *
       					connection;
    connection = callback_arg;

    if(connection->keep_open)
    {
	if(result == GLOBUS_SUCCESS)
	{
	    if(connection->replybuf == NULL)
	    {
		connection->replybuf = globus_libc_malloc(4096);
		connection->replybufsize = 4096;
	    }
	    result = globus_io_register_read(
		    handle,
		    connection->replybuf,
		    4,
		    4,
		    globus_l_gram_protocol_delegation_read_callback,
		    connection);

	    if(result == GLOBUS_SUCCESS)
	    {
		return;
	    }
	}
	if(result != GLOBUS_SUCCESS)
	{
	    /* Error occurred. Call callback with error. */
	    connection->delegation_callback(
		    connection->delegation_arg,
		    connection->handle,
		    GSS_C_NO_CREDENTIAL,
		    GLOBUS_GRAM_PROTOCOL_ERROR_DELEGATION_FAILED);
	}
    }
     
    result = globus_io_register_close(
	    handle,
	    globus_l_gram_protocol_connection_close_callback,
	    callback_arg);

    if(result != GLOBUS_SUCCESS)
    {
        /* If we can't close the handle, we'd still like to clean up
	 * our memory.
	 */
	globus_l_gram_protocol_connection_close_callback(
	    callback_arg,
	    handle,
	    result);
    }
}
/* globus_l_gram_protocol_write_reply_callback() */
    
/**
 * Unpack a reply and call user's callback.
 *
 * Called when a reply is received on a GRAM Protocol TCP/IP connection.
 * If the entire reply is present, it is unframed and the user's POST
 * callback is called. If it is not present, then another read is registered
 * on the connection.
 * 
 * @param callback_arg
 *        A pointer to the
 *        @link globus_i_gram_protocol_connection_t connection @endlink
 *        structure for this reply. This should never be NULL.
 * @param handle
 *        The Globus I/O handle associated with the connection.
 * @param result
 *        The result of reading the reply.
 * @param buf
 *        The portion of the reply buffer which was read into.
 * @param nbytes
 *        The number of bytes of @a buf read.
 */
static
void
globus_l_gram_protocol_read_reply_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes)
{
    globus_object_t *			err;
    globus_i_gram_protocol_connection_t *
    					connection;
    char *				p;
    globus_size_t			header_length;

    connection = callback_arg;

    if(result != GLOBUS_SUCCESS)
    {
        err = globus_error_get(result);

	if(!globus_io_eof(err) || !connection->got_header)
	{
            char *                      errstring;

            errstring = globus_error_print_friendly(err);
            globus_gram_protocol_error_10_hack_replace_message(errstring);
            globus_free(errstring);
	    globus_object_free(err);

	    connection->rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;

	    goto callback_exit;
	}
    }
    if(!connection->got_header)
    {
        if(connection->n_read == 0 && ((*buf == '0') || (*buf == 'D')))
	{
	    /* Delegation packet?!? */
            globus_gram_protocol_error_10_hack_replace_message(
                "server sent unexpected delegation protocol message");
	    connection->rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;

	    goto callback_exit;
	}

	connection->n_read += nbytes;
	connection->replybuf[connection->n_read] = '\0';

	p = strstr((const char *)connection->replybuf, CRLF CRLF);
	header_length = (const char *)p - (const char *)connection->replybuf;

	if(p)
	{
	    connection->got_header = GLOBUS_TRUE;

	    connection->rc = globus_l_gram_protocol_parse_reply_header(
	             connection->replybuf,
		     &connection->payload_length);

	    if(connection->rc != GLOBUS_SUCCESS)
	    {
	        goto callback_exit;
	    }
	    /* p + 4 is the beginning of the payload (after CRLF CRLF) */
	    memmove(connection->replybuf,
		    p + 4,
		    connection->n_read - header_length - 4);
	    connection->n_read = connection->n_read - header_length - 4;
	    connection->replybuf[connection->n_read] = '\0';
	    nbytes = 0;
	}
    }
    if(connection->got_header)
    {
        if(connection->n_read >= connection->payload_length)
	{
	    /* Got the payload, call back to user now */
	    goto callback_exit;
	}
    }
    /* Missing part of the header or payload, register another read */
    globus_assert((!connection->got_header) ||
	          (connection->n_read < connection->payload_length));

    result = globus_io_register_read(
                 connection->io_handle,
		 connection->replybuf + connection->n_read,
		 connection->replybufsize - connection->n_read,
		 1,
		 globus_l_gram_protocol_read_reply_callback,
		 connection);

    if(result == GLOBUS_SUCCESS)
    {
	return;
    }

    /* If we couldn't register the read, the we'll fall through,
     * callback to user, and close
     */

  callback_exit:
    /* Call user callback... users should not free the
     * buffers, unlike the original code. It's ok if there is no callback,
     * just means the caller of globus_gram_protocol_post() doesn't
     * care about the response----the job manager is like that.
     */
    if(connection->callback && (connection->rc || !connection->keep_open))
    {
	connection->callback(connection->callback_arg,
			     connection->handle,
			     connection->replybuf,
			     connection->payload_length,
			     connection->rc,
			     NULL);
    }
    if ((!connection->rc) && connection->keep_open)
    {
	/* In the post  delegation case, we get a reply containing no body.
	 * We then start doing delegation.
	 */
	connection->got_header = 0;
	connection->n_read = 0;

	globus_l_gram_protocol_init_delegation(connection, GSS_C_NO_BUFFER);
	
	return;
    }
    /* For reply handling, we just need to close up the connection
     * after we've dispatched the callback.
     */
    result = globus_io_register_close(
                 connection->io_handle,
		 globus_l_gram_protocol_connection_close_callback,
		 connection);

    if(result != GLOBUS_SUCCESS)
    {
        /* If we can't close the handle, we'd still like to clean up
	 * our memory.
	 */
	globus_l_gram_protocol_connection_close_callback(
	    callback_arg,
	    handle,
	    result);
    }
    return;
}
/* globus_l_gram_protocol_read_reply_callback() */

/**
 * Free memory associated with a now-closed connection.
 *
 * @param callback_arg
 *        A pointer to the
 *        @link globus_i_gram_protocol_connection_t connection @endlink
 *        structure associated with this handle.
 * @param handle
 *        The Globus I/O handle which is now closed.
 * @param result
 *        The result of closing the handle.
 */
static
void
globus_l_gram_protocol_connection_close_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result)
{
    globus_i_gram_protocol_connection_t *
					connection;
    globus_list_t *			node;

    connection = callback_arg;

    globus_mutex_lock(&globus_i_gram_protocol_mutex);
    node = globus_list_search(globus_i_gram_protocol_connections, connection);

    if(node)
    {
	globus_list_remove(&globus_i_gram_protocol_connections, node);

	if(connection->listener)
	{
	    /* Connection was created by an accept() */
	    connection->listener->connection_count--;

	    if(connection->listener->connection_count == 0)
	    {
		globus_cond_signal(&connection->listener->cond);
	    }
            if((!globus_i_gram_protocol_shutdown_called) &&
                    connection->listener->allow_attach &&
                    (!connection->listener->listen_registered) &&
                    connection->listener->connection_count < globus_i_gram_protocol_max_concurrency)
            {
                result = globus_io_tcp_register_listen(
                        connection->listener->handle,
                        globus_l_gram_protocol_listen_callback,
                        connection->listener);
                if (result == GLOBUS_SUCCESS)
                {
                    connection->listener->listen_registered = GLOBUS_TRUE;
                }
            }
	}
	else
	{
	    /* Connection was created by POSTing */
	    globus_i_gram_protocol_num_connects--;
	    
	    if(globus_i_gram_protocol_num_connects == 0)
	    {
		globus_cond_signal(&globus_i_gram_protocol_cond);
	    }
	}
	if(connection->buf)
	{
	    globus_libc_free(connection->buf);
	}
	if(connection->replybuf)
	{
	    globus_libc_free(connection->replybuf);
	}
	if(connection->io_handle)
	{
	    globus_libc_free(connection->io_handle);
	}
	if(connection->uri)
	{
	    globus_libc_free(connection->uri);
	}
	globus_libc_free(connection);
	globus_l_gram_protocol_free_old_credentials();
    }
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);
}
/* globus_l_gram_protocol_connection_close_callback() */
					
/**
 * Internal function to close a listener.
 *
 * Used by both the deactivation function and the
 * globus_gram_protocol_callback_disallow() to close and free a 
 * listener handle. Waits for all connections created by this listener
 * to be completed...
 */
int
globus_i_gram_protocol_callback_disallow(
    globus_i_gram_protocol_listener_t *	listener)
{
    globus_list_t *			node;
    globus_io_handle_t *		handle;

    handle = listener->handle;

    if(listener->allow_attach == GLOBUS_FALSE)
    {
        return GLOBUS_SUCCESS; /* sort of */
    }
    listener->allow_attach = GLOBUS_FALSE;

    while(listener->connection_count != 0)
    {
        globus_cond_wait(&listener->cond, &globus_i_gram_protocol_mutex);
    }

    /* Need to unlock here to allow callbacks triggered by the blocking close
     * to be handled.
     */
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);
    globus_io_close(handle); /* What if this fails? */
    globus_mutex_lock(&globus_i_gram_protocol_mutex);

    node = globus_list_search(globus_i_gram_protocol_listeners, listener);
    if(node)
    {
        globus_list_remove(&globus_i_gram_protocol_listeners, node);
	globus_cond_destroy(&listener->cond);
	globus_libc_free(handle);
	globus_libc_free(listener);
    }
    globus_cond_signal(&globus_i_gram_protocol_cond);

    return GLOBUS_SUCCESS;
}
/* globus_i_gram_protocol_callback_disallow() */

/********************** replace credentials ******************************/

void
globus_l_gram_protocol_free_old_credentials()
{
    globus_list_t *cred_list;
    globus_list_t *conn_list;
    gss_cred_id_t cred;
    globus_i_gram_protocol_connection_t *conn;

    cred_list = globus_i_gram_protocol_old_creds;
    while(!globus_list_empty(cred_list))
    {
	globus_list_t *dead_cred = cred_list;
	cred = (gss_cred_id_t) globus_list_first(cred_list);

	conn_list = globus_i_gram_protocol_connections;
	while(!globus_list_empty(conn_list))
	{
	    conn = (globus_i_gram_protocol_connection_t *) globus_list_first(conn_list);
            if (conn->accepting)
            {
                return;
            }
            
	    if (conn->io_handle != NULL)
	    {
	        gss_cred_id_t           cur_cred;
	        
	        globus_io_tcp_get_credential(conn->io_handle, &cur_cred);
	        if (cur_cred == cred)
                {
                    dead_cred = NULL;
                    break;
                }
	    }

	    conn_list = globus_list_rest(conn_list);
	}

	cred_list = globus_list_rest(cred_list);

	if (dead_cred != NULL)
	{
	    globus_list_remove(&globus_i_gram_protocol_old_creds,
			       dead_cred);

	    if (cred != GSS_C_NO_CREDENTIAL)
	    {
		OM_uint32 minor_status;
		gss_release_cred(&minor_status, &cred);
	    }
	}

    }

}

/**
 * @brief Set GRAM default credential
 * @ingroup globus_gram_protocol_io
 * @details
 * The globus_gram_protocol_set_credentials() function causes subsequent
 * GRAM operations to use the GSSAPI credential @a new_credentials. After this
 * function returns, the caller must not use the credential, as it may be freed
 * by GRAM when it is no longer needed.
 *
 * As a side effect, globus_gram_protocol_set_credentials() may free any 
 * credential previously used by GRAM.
 *
 * @param new_credentials
 *     New GSSAPI credential to use.
 *
 * @return
 *     Upon success, globus_gram_protocol_set_credentials() returns
 *     GLOBUS_SUCCESS. There are currently no error cases handled by this
 *     function.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 */
int
globus_gram_protocol_set_credentials(gss_cred_id_t new_credentials)
{
    globus_list_t *tmp_list;
    globus_i_gram_protocol_listener_t *listener;
    gss_cred_id_t old_cred;

    globus_mutex_lock( &globus_i_gram_protocol_mutex );

    old_cred = globus_i_gram_protocol_credential;

    globus_i_gram_protocol_credential = new_credentials;

    globus_io_attr_set_secure_authentication_mode(
			&globus_i_gram_protocol_default_attr,
			GLOBUS_IO_SECURE_AUTHENTICATION_MODE_GSSAPI,
			globus_i_gram_protocol_credential);

    tmp_list = globus_i_gram_protocol_listeners;
    while(!globus_list_empty(tmp_list))
    {
	listener = (globus_i_gram_protocol_listener_t *) globus_list_first(tmp_list);
        globus_io_tcp_set_credential(listener->handle, new_credentials);

	tmp_list = globus_list_rest(tmp_list);
    }

    globus_list_insert(&globus_i_gram_protocol_old_creds, old_cred);
    globus_l_gram_protocol_free_old_credentials();

    globus_mutex_unlock( &globus_i_gram_protocol_mutex );

    return GLOBUS_SUCCESS;
}
/* globus_gram_protocol_set_credentials() */


/* Parsing Functions */

/**
 * Parse a GRAM Protocol request header.
 *
 * Parses the headers
 * @param buf
 * @param payload_length
 */
static
int
globus_l_gram_protocol_parse_request_header(
    const globus_byte_t *		buf,
    globus_size_t *			payload_length,
    char **				uri)
{
    int					rc;
    long				tmp;
    char *				tmp_uri;
    char *				host;

    tmp_uri = (char *) globus_libc_malloc(strlen((char *) buf));
    host = (char *) globus_libc_malloc(strlen((char *) buf));

    globus_libc_lock();
    rc = sscanf((const char *) buf,
                GLOBUS_GRAM_HTTP_REQUEST_LINE
		GLOBUS_GRAM_HTTP_HOST_LINE
		GLOBUS_GRAM_HTTP_CONTENT_TYPE_LINE
		GLOBUS_GRAM_HTTP_CONTENT_LENGTH_LINE
		CRLF,
		tmp_uri,
		host,
		&tmp);

    globus_libc_unlock();
    if(rc != 3)
    {
        globus_gram_protocol_error_10_hack_replace_message("GRAM is unable to parse HTTp message" );
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;

	*payload_length = 0;
    }
    else
    {
	*payload_length = tmp;
	*uri = strdup(tmp_uri);
	rc = GLOBUS_SUCCESS;
    }
    globus_free(tmp_uri);
    globus_free(host);

    return rc;
}
/* globus_l_gram_protocol_parse_request_header() */

/**
 * Parse a GRAM protocol reply header.
 */
static
int
globus_l_gram_protocol_parse_reply_header(
    const globus_byte_t *		buf,
    globus_size_t *			payload_length)
{
    int					rc;
    int					code;
    int					offset;
    char *				reason;
    long				tmp;

    reason = (char *) globus_malloc(strlen((char *)buf));

    *payload_length = 0;

    globus_libc_lock();
    rc = sscanf( (char *) buf,
		 GLOBUS_GRAM_HTTP_PARSE_REPLY_LINE "%n",
		 &code,
		 reason,
		 &offset);
    globus_libc_unlock();

    if(rc < 2)
    {
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNFRAME_FAILED;
    }
    else if(code == 200)
    {
	globus_libc_lock();
	rc = sscanf( (char *)buf + offset,
		     GLOBUS_GRAM_HTTP_CONTENT_TYPE_LINE
		     GLOBUS_GRAM_HTTP_CONTENT_LENGTH_LINE,
		     &tmp);
	globus_libc_unlock();
	if(rc != 1)
	{
	    rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNFRAME_FAILED;
	    *payload_length = 0;
	}
	else
	{
	    *payload_length = tmp;
	    rc = GLOBUS_SUCCESS;
	}
    }
    else if(code==400)  /* JM failed to frame reply */
    {
        globus_gram_protocol_error_10_hack_replace_message("job manager failed to frame reply");
 
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
    }
    else if(code==403)
    {
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_AUTHORIZATION;
    }
    else if(code==404)
    {
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_SERVICE_NOT_FOUND;
    }
    else if(code==500)
    {
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_GATEKEEPER_MISCONFIGURED;
    }
    else
    {
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNFRAME_FAILED;
    }

    globus_free(reason);

    return rc; 
}
/* globus_l_gram_protocol_parse_reply_header() */

static
int
globus_l_gram_protocol_reply(
    globus_gram_protocol_handle_t 	handle,
    int					code,
    globus_byte_t *			message,
    globus_size_t			message_size,
    gss_OID_set				restriction_oids,
    gss_buffer_set_t			restriction_buffers,
    OM_uint32				req_flags,
    OM_uint32				time_req,
    globus_gram_protocol_delegation_callback_t
    					callback,
    void *				arg)
{
    globus_i_gram_protocol_connection_t *
    					connection;
    globus_list_t *			list;
    int					rc;
    globus_result_t			result;

    /* lookup up connection using handle as key */
    globus_mutex_lock(&globus_i_gram_protocol_mutex);
    list = globus_i_gram_protocol_connections;
    while(list != NULL)
    {
        connection = globus_list_first(list);
	if(connection->handle == handle)
	{
	    break;
	}
	list = globus_list_rest(list);
    }

    if(list == NULL)
    {
	/* No match */
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST;

	goto error_exit;
    }
    if(connection->read_type != GLOBUS_GRAM_PROTOCOL_REQUEST ||
       connection->replybuf != NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST;

	goto error_exit;
    }

    /* frame reply */
    rc = globus_gram_protocol_frame_reply(code,
                                          message,
					  message_size,
					  &connection->replybuf,
					  &connection->replybufsize);
    if(rc != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }

    if(callback)
    {
	connection->keep_open = GLOBUS_TRUE;
    }
    connection->delegation_callback = callback;
    connection->delegation_arg = arg;
    connection->delegation_restriction_oids = restriction_oids;
    connection->delegation_restriction_buffers = restriction_buffers;
    connection->delegation_req_flags = req_flags;
    connection->delegation_time_req = time_req;
    connection->delegation_major_status = GSS_S_CONTINUE_NEEDED;
    connection->delegation_minor_status = 0;

    result = globus_io_register_write(
	         connection->io_handle,
		 connection->replybuf,
		 connection->replybufsize,
		 globus_l_gram_protocol_write_reply_callback,
		 connection);

    if(result != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;

	goto free_reply_exit;
    }

    globus_mutex_unlock(&globus_i_gram_protocol_mutex);
    return GLOBUS_SUCCESS;

  free_reply_exit:
    globus_libc_free(connection->replybuf);
    connection->replybuf = NULL;
    connection->replybufsize = 0;
  error_exit:
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);

    return rc;
}
/* globus_l_gram_protocol_reply() */

static
int
globus_l_gram_protocol_post(
    const char *			url,
    globus_gram_protocol_handle_t *	handle,
    globus_io_attr_t *			attr,
    globus_byte_t *			message,
    globus_size_t			message_size,
    globus_bool_t			keep_open,
    gss_cred_id_t			cred_handle,
    gss_OID_set				restriction_oids,
    gss_buffer_set_t			restriction_buffers,
    OM_uint32				req_flags,
    OM_uint32				time_req,
    globus_gram_protocol_callback_t	callback,
    void *				callback_arg)
{
    int					rc;
    globus_i_gram_protocol_connection_t *
    					connection;
    globus_byte_t *			framed;
    globus_size_t			framedsize;
    globus_result_t			res;
    globus_url_t			parsed_url;
    globus_io_attr_t 			local_attr;
    globus_list_t *			node;
    char *                              local_url = NULL;
    char *                              subject = NULL;
    
    rc = globus_url_parse(url, &parsed_url);

    if(rc != GLOBUS_SUCCESS)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT;
    }


    if(parsed_url.url_path &&
       (subject = strrchr(parsed_url.url_path,':')))
    {
        local_url = strdup(url);

        if(!local_url)
        {
            goto error_exit;
        }

        subject = strrchr(local_url,':');

        *subject = '\0';
        subject++;
    }

    rc = globus_gram_protocol_frame_request(local_url ? local_url : url,
					    message,
					    message_size,
					    &framed,
					    &framedsize);
    if(rc != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }
					    
    connection = globus_libc_calloc(
                     1,
		     sizeof(globus_i_gram_protocol_connection_t));
    
    if(connection == NULL)
    {
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
	goto free_framed_exit;
    }
    connection->callback = callback;
    connection->callback_arg = callback_arg;
    connection->buf = framed;
    connection->bufsize = framedsize;
    connection->accepting = GLOBUS_TRUE;
    if(keep_open)
    {
	connection->keep_open = keep_open;
    }
    connection->delegation_major_status = GSS_S_CONTINUE_NEEDED;
    connection->delegation_minor_status = 0;
    connection->delegation_cred = cred_handle;
    connection->delegation_restriction_oids = restriction_oids;
    connection->delegation_restriction_buffers = restriction_buffers;
    connection->delegation_req_flags = req_flags;
    connection->delegation_time_req = time_req;
    connection->read_type = GLOBUS_GRAM_PROTOCOL_REPLY;

    globus_mutex_lock(&globus_i_gram_protocol_mutex);
    if(globus_i_gram_protocol_shutdown_called)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST;
	
	goto free_connection_exit;
    }
    connection->handle = ++globus_i_gram_protocol_handle;
    if(handle)
    {
	*handle = connection->handle;
    }
    connection->io_handle = globus_libc_malloc(sizeof(globus_io_handle_t));
    if(connection->io_handle == NULL)
    {
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
	goto free_connection_exit;
    }
    globus_i_gram_protocol_num_connects++;
    globus_list_insert(&globus_i_gram_protocol_connections,
		       connection);

    if(!attr && subject)
    {   
	globus_l_gram_protocol_setup_connect_attr(&local_attr, subject);

        res = globus_io_tcp_register_connect(
            parsed_url.host,
            parsed_url.port,
            &local_attr,
            globus_l_gram_protocol_connect_callback,
            connection,
            connection->io_handle);

        globus_io_tcpattr_destroy(&local_attr);
    }
    else
    {
        res = globus_io_tcp_register_connect(
            parsed_url.host,
            parsed_url.port,
            attr ? attr : &globus_i_gram_protocol_default_attr,
            globus_l_gram_protocol_connect_callback,
            connection,
            connection->io_handle);
    }

    if(res != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_CONNECTION_FAILED;

	goto remove_connection_exit;
    }
    
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);

    globus_url_destroy(&parsed_url);
    
    return GLOBUS_SUCCESS;
    
 remove_connection_exit:
    globus_i_gram_protocol_num_connects--;
    node = globus_list_search(globus_i_gram_protocol_connections, connection);
    if(node)
    {
	globus_list_remove(&globus_i_gram_protocol_connections, node);
    }
    globus_libc_free(connection->io_handle);
 free_connection_exit:
    globus_mutex_unlock(&globus_i_gram_protocol_mutex);
    globus_libc_free(connection);
 free_framed_exit:
    globus_libc_free(framed);
 error_exit:
    if (handle)
    {
        *handle = 0;
    }

    if(local_url)
    {
        free(local_url);
    }

    globus_url_destroy(&parsed_url);

    return rc;
}
/* globus_l_gram_protocol_post() */

static
void
globus_l_gram_protocol_delegation_read_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes)
{
    gss_buffer_desc			input_token;
    globus_i_gram_protocol_connection_t *
					connection;

    connection = callback_arg;
    
    if(connection->token_length == 0 &&
       nbytes >= 4)
    {
	connection->token_length  = buf[0] << 24;
	connection->token_length |= buf[1] << 16;
	connection->token_length |= buf[2] <<  8;
	connection->token_length |= buf[3]      ;

	if(connection->replybufsize < connection->token_length)
	{
	    globus_libc_free(connection->replybuf);
	    connection->replybuf =
		globus_libc_malloc(connection->token_length);
	    connection->replybufsize = connection->token_length;
	}

	result = globus_io_register_read(
		connection->io_handle,
		connection->replybuf,
		connection->token_length,
		connection->token_length,
		globus_l_gram_protocol_delegation_read_callback,
		connection);

	if(result == GLOBUS_SUCCESS)
	{
	    return;
	}
	nbytes = 0;
    }

    input_token.value = buf;
    input_token.length = nbytes;
    connection->token_length = 0;

    connection = callback_arg;

    if(result != GLOBUS_SUCCESS)
    {
	connection->delegation_major_status =
	    GSS_S_DEFECTIVE_TOKEN | GSS_S_CALL_INACCESSIBLE_READ;
    }

    if(connection->read_type == GLOBUS_GRAM_PROTOCOL_REQUEST)
    {
	globus_l_gram_protocol_accept_delegation(
		connection,
		&input_token);
    }
    else
    {
	globus_assert(connection->read_type == GLOBUS_GRAM_PROTOCOL_REPLY);
	globus_l_gram_protocol_init_delegation(
		connection,
		&input_token);
    }
}
/* globus_l_gram_protocol_delegation_read_callback() */

static
void
globus_l_gram_protocol_delegation_write_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes)
{
    gss_buffer_desc			input_token;
    globus_i_gram_protocol_connection_t *
					connection;

    input_token.value = NULL;
    input_token.length = 0;


    globus_libc_free(buf);

    connection = callback_arg;

    if(result != GLOBUS_SUCCESS)
    {
	connection->delegation_major_status =
	    GSS_S_DEFECTIVE_TOKEN | GSS_S_CALL_INACCESSIBLE_READ;
    }
    if(connection->read_type == GLOBUS_GRAM_PROTOCOL_REQUEST)
    {
	globus_l_gram_protocol_accept_delegation(
		connection,
		&input_token);
    }
    else
    {
	globus_assert(connection->read_type == GLOBUS_GRAM_PROTOCOL_REPLY);
	globus_l_gram_protocol_init_delegation(
		connection,
		&input_token);
    }
}
/* globus_l_gram_protocol_delegation_write_callback() */

static
void
globus_l_gram_protocol_accept_delegation(
    globus_i_gram_protocol_connection_t *
    					connection,
    gss_buffer_t			input_token)
{
    globus_result_t			result;
    gss_buffer_desc			output_token;
    unsigned char *			output_buffer;

    output_token.value = NULL;
    output_token.length = 0;

    if(input_token->length != 0)
    {
        gss_ctx_id_t                    context;
	    
        globus_io_tcp_get_security_context(connection->io_handle, &context);
	        
	connection->delegation_major_status = gss_accept_delegation(
		&connection->delegation_minor_status,
		context,
		connection->delegation_restriction_oids,
		connection->delegation_restriction_buffers,
		input_token,
		connection->delegation_req_flags,
		connection->delegation_time_req,
		NULL,
		&connection->delegation_cred,
		NULL,
		&output_token);


	if(output_token.length > 0)
	{
	    output_buffer = globus_libc_malloc(output_token.length+4);
	    output_buffer[0] = (output_token.length >> 24) & 0xff;
	    output_buffer[1] = (output_token.length >> 16) & 0xff;
	    output_buffer[2] = (output_token.length >>  8) & 0xff;
	    output_buffer[3] = (output_token.length      ) & 0xff;
	    memcpy(output_buffer+4, output_token.value, output_token.length);

	    result = globus_io_register_write(
		    connection->io_handle,
		    output_buffer,
		    output_token.length+4,
		    globus_l_gram_protocol_delegation_write_callback,
		    connection);
	    globus_libc_free(output_token.value);

	    if(result == GLOBUS_SUCCESS)
	    {
		return;
	    }
	    else
	    {
		connection->delegation_major_status =
		    GSS_S_DEFECTIVE_TOKEN | GSS_S_CALL_INACCESSIBLE_READ;
	    }
	}
    }
    if(connection->delegation_major_status & GSS_S_CONTINUE_NEEDED)
    {
	result = globus_io_register_read(
		connection->io_handle,
		connection->replybuf,
		4,
		4,
		globus_l_gram_protocol_delegation_read_callback,
		connection);
	if(result == GLOBUS_SUCCESS)
	{
	    return;
	}
	else
	{
	    connection->delegation_major_status =
		GSS_S_DEFECTIVE_TOKEN | GSS_S_CALL_INACCESSIBLE_READ;
	}
    }
    if(GSS_ERROR(connection->delegation_major_status))
    {
	/* TODO: error 7 hack */
    }

    /* Finished with delegation... callback */
    if(connection->replybuf)
    {
	globus_libc_free(connection->replybuf);
	connection->replybuf=NULL;
	connection->replybufsize=0;
    }
    connection->keep_open = GLOBUS_FALSE;
    connection->delegation_callback(
	    connection->delegation_arg,
	    connection->handle,
	    connection->delegation_cred,
	    GSS_ERROR(connection->delegation_major_status) 
		? GLOBUS_GRAM_PROTOCOL_ERROR_AUTHORIZATION
		: GLOBUS_SUCCESS);
}
/* globus_l_gram_protocol_accept_delegation() */

static
void
globus_l_gram_protocol_init_delegation(
    globus_i_gram_protocol_connection_t *
    					connection,
    gss_buffer_t			input_token)
{
    globus_result_t			result;
    gss_buffer_desc			output_token;
    unsigned char *			output_buffer;

    output_token.value = NULL;
    output_token.length = 0;

    if(connection->delegation_major_status & GSS_S_CONTINUE_NEEDED)
    {
	if((input_token != GSS_C_NO_BUFFER  && input_token->length != 0) ||
	   input_token == GSS_C_NO_BUFFER)
	{
	    gss_ctx_id_t                context;
	    
	    globus_io_tcp_get_security_context(
	        connection->io_handle, &context);
	        
	    connection->delegation_major_status = gss_init_delegation(
		    &connection->delegation_minor_status,
		    context,
		    connection->delegation_cred,
		    GSS_C_NO_OID,
		    connection->delegation_restriction_oids,
		    connection->delegation_restriction_buffers,
		    input_token,
		    connection->delegation_req_flags,
		    connection->delegation_time_req,
		    &output_token);
	}


	if(output_token.length > 0)
	{
	    output_buffer = globus_libc_malloc(output_token.length+4);
	    output_buffer[0] = (output_token.length >> 24) & 0xff;
	    output_buffer[1] = (output_token.length >> 16) & 0xff;
	    output_buffer[2] = (output_token.length >>  8) & 0xff;
	    output_buffer[3] = (output_token.length      ) & 0xff;
	    memcpy(output_buffer+4, output_token.value, output_token.length);
		
	    result = globus_io_register_write(
		    connection->io_handle,
		    output_buffer,
		    output_token.length+4,
		    globus_l_gram_protocol_delegation_write_callback,
		    connection);
	    globus_libc_free(output_token.value);

	    if(result == GLOBUS_SUCCESS)
	    {
		return;
	    }
	    else
	    {
		connection->delegation_major_status =
		    GSS_S_DEFECTIVE_TOKEN | GSS_S_CALL_INACCESSIBLE_READ;
	    }
	}

	result = globus_io_register_read(
		connection->io_handle,
		connection->replybuf,
		4,
		4,
		globus_l_gram_protocol_delegation_read_callback,
		connection);
	if(result == GLOBUS_SUCCESS)
	{
	    return;
	}
	else
	{
	    connection->delegation_major_status =
		GSS_S_DEFECTIVE_TOKEN | GSS_S_CALL_INACCESSIBLE_READ;
	}
    }
    if(GSS_ERROR(connection->delegation_major_status))
    {
	/* TODO: error 7 hack */
    }

    /* Finished with delegation... register read of delegation status */
    connection->keep_open = GLOBUS_FALSE;

    if(connection->replybufsize < GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE)
    {
	globus_libc_free(connection->replybuf);
	connection->replybuf =
	    globus_libc_malloc(GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE);
	connection->replybufsize = GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE;
    }
    result = globus_io_register_read(
	    connection->io_handle,
	    connection->replybuf,
	    connection->replybufsize,
	    1,
	    globus_l_gram_protocol_read_reply_callback,
	    connection);

    if(result == GLOBUS_SUCCESS)
    {
	return;
    }
    else
    {
	connection->rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;
    }
    if(connection->callback && connection->rc)
    {
	connection->callback(connection->callback_arg,
			     connection->handle,
			     connection->replybuf,
			     connection->payload_length,
			     connection->rc,
			     NULL);
    }
    /* For reply handling, we just need to close up the connection
     * after we've dispatched the callback.
     */
    result = globus_io_register_close(
                 connection->io_handle,
		 globus_l_gram_protocol_connection_close_callback,
		 connection);

    if(result != GLOBUS_SUCCESS)
    {
        /* If we can't close the handle, we'd still like to clean up
	 * our memory.
	 */
	globus_l_gram_protocol_connection_close_callback(
	    connection,
	    connection->io_handle,
	    result);
    }
}
/* globus_l_gram_protocol_init_delegation() */


static
globus_bool_t
globus_l_gram_protocol_authorization_callback(
	void *				arg,
	globus_io_handle_t *		handle,
	globus_result_t			result,
	char *				identity,
	gss_ctx_id_t 			context_handle)
{
    globus_i_gram_protocol_connection_t *
    					connection;
    
    connection = (globus_i_gram_protocol_connection_t *) arg;

    connection->context = context_handle;
    
    return GLOBUS_TRUE;
}

static int
globus_l_gram_protocol_setup_accept_attr(
    globus_io_attr_t *                          attr,
    globus_i_gram_protocol_connection_t *       connection)
{
    globus_result_t                     res;
    globus_io_secure_authorization_data_t  auth_data;
    globus_object_t *                   err;

    res = globus_io_secure_authorization_data_initialize(&auth_data);

    if (res != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }
    res = globus_io_secure_authorization_data_set_callback(
                &auth_data,
                globus_l_gram_protocol_authorization_callback,
                (void *) connection);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_auth_data;
    }

    res = globus_io_attr_set_secure_authorization_mode(
                attr,
                GLOBUS_IO_SECURE_AUTHORIZATION_MODE_CALLBACK,
                &auth_data);

    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_auth_data;
    }

    globus_io_secure_authorization_data_destroy(&auth_data);

    return GLOBUS_SUCCESS;

destroy_auth_data:
    globus_io_secure_authorization_data_destroy(&auth_data);
error_exit:
    err = globus_error_get(res);
    globus_object_free(err);

    return GLOBUS_GRAM_PROTOCOL_ERROR_CONNECTION_FAILED;
}

static int
globus_l_gram_protocol_setup_connect_attr(
    globus_io_attr_t *                     attr,
    char *                                 identity)
{
    globus_result_t                        res;
    int                                    rc = GLOBUS_SUCCESS;
    globus_io_secure_authorization_data_t  auth_data = NULL;

    /* acquire mutex */
    if ((res = globus_io_tcpattr_init(attr)) != GLOBUS_SUCCESS)
    {
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_CONNECTION_FAILED;
        goto out;
    }

    if ( (res = globus_io_secure_authorization_data_initialize(
	                &auth_data))
	 || (res = globus_io_secure_authorization_data_set_identity(
	                &auth_data,
                        identity))
	 || (res = globus_io_attr_set_secure_authentication_mode(
	                attr,
			GLOBUS_IO_SECURE_AUTHENTICATION_MODE_MUTUAL,
			globus_i_gram_protocol_credential))
	 || (res = globus_io_attr_set_secure_authorization_mode(
	                attr,
			GLOBUS_IO_SECURE_AUTHORIZATION_MODE_IDENTITY,
			&auth_data))
	 || (res = globus_io_attr_set_secure_channel_mode(
	                attr,
			GLOBUS_IO_SECURE_CHANNEL_MODE_SSL_WRAP))
         || (res = globus_io_attr_set_tcp_allow_ipv6(
                        attr,
                        GLOBUS_TRUE)) )
    {
        globus_io_tcpattr_destroy(attr);

	rc = GLOBUS_GRAM_PROTOCOL_ERROR_CONNECTION_FAILED;
    }

    if (auth_data)
    {
        globus_io_secure_authorization_data_destroy(&auth_data);
    }

out:
    return rc;
}


#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
