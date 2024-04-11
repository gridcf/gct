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
 * @file globus_l_gass_transfer_http.h
 * @brief http protocol module
 *
 * This header defines the prototypes for the local functions in the
 * http protocol module source file
 */
#ifndef GLOBUS_L_GASS_TRANSFER_HTTP_H
#define GLOBUS_L_GASS_TRANSFER_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
			  Module Specific Constants
******************************************************************************/
enum 
{
    GLOBUS_L_DEFAULT_HTTP_PORT			= 80,
    GLOBUS_L_DEFAULT_HTTPS_PORT			= 443,
    GLOBUS_L_GASS_RESPONSE_LEN			= 256
};

typedef enum
{
    GLOBUS_L_LINE_MODE_UNKNOWN,
    GLOBUS_L_LINE_MODE_CR,
    GLOBUS_L_LINE_MODE_LF,
    GLOBUS_L_LINE_MODE_CRLF
} globus_gass_transfer_http_line_mode_t;

/*
 * Local text format to convert requests with the text_mode attribute
 * set. Always LF, which is used by the C language. Conversion from LF
 *  ode to the local storage is up to the user (or Globus I/O library).
 */
const globus_gass_transfer_http_line_mode_t
globus_l_gass_transfer_http_line_mode =
#ifndef TARGET_ARCH_WIN32
    GLOBUS_L_LINE_MODE_LF;
#else
    GLOBUS_L_LINE_MODE_CRLF;
#endif

/* For 8-bit Text:
#define GLOBUS_L_TEXT_BYTE(text) (text)
*/
#define GLOBUS_L_TEXT_BYTE(text) (text & 0x7f)

static globus_mutex_t globus_l_gass_transfer_http_mutex;
static globus_cond_t globus_l_gass_transfer_http_cond;

#define globus_l_gass_transfer_http_lock() \
	globus_mutex_lock(&globus_l_gass_transfer_http_mutex)/*,*/ \
	/*printf("locked mutex at %s: %d\n", __FILE__, __LINE__)*/
#define globus_l_gass_transfer_http_unlock() \
	/*printf("unlocking mutex at %s: %d\n", __FILE__, __LINE__), */\
	globus_mutex_unlock(&globus_l_gass_transfer_http_mutex)
#define globus_l_gass_transfer_http_wait() \
	globus_cond_wait(&globus_l_gass_transfer_http_cond, \
			 &globus_l_gass_transfer_http_mutex)
#define globus_l_gass_transfer_http_signal() \
	globus_cond_signal(&globus_l_gass_transfer_http_cond)

static char * globus_l_gass_transfer_http_subject_name;

/* Some handy, common values in the HTTP protocol */
#define CR				'\015'
#define LF				'\012'
#define CRLF				"\015\012"
#define CR_STRING			"\015"
#define LF_STRING			"\012"

/* Commands the client issues to the server */
#define GLOBUS_GASS_HTTP_VERSION	"Globus-GASS-HTTP/1.1.0"

#define GLOBUS_L_APPEND_URI		"/globus-bins/GASSappend?"

#define GLOBUS_L_GET_COMMAND		"GET %s HTTP/1.1" CRLF \
                                        "Host: %s" CRLF \
					"Connection: close" CRLF \
					"User-Agent: " GLOBUS_GASS_HTTP_VERSION CRLF

#define GLOBUS_L_PUT_COMMAND		"PUT %s HTTP/1.1" CRLF \
					"Host: %s" CRLF \
					"Connection: close" CRLF \
					"User-Agent: " GLOBUS_GASS_HTTP_VERSION CRLF

#define GLOBUS_L_APPEND_COMMAND		"POST " GLOBUS_L_APPEND_URI "%s " \
					     "HTTP/1.1" CRLF \
					"Host: %s" CRLF \
					"Connection: close" CRLF \
					"User-Agent: " GLOBUS_GASS_HTTP_VERSION CRLF

#define GLOBUS_L_REFER_RESPONSE		"HTTP/1.1 302 Moved Temporarily" CRLF \
					"Connection: close" CRLF \
					"Server: " GLOBUS_GASS_HTTP_VERSION CRLF

#define GLOBUS_L_CONTINUE_RESPONSE	"HTTP/1.1 100 Continue" CRLF

#define GLOBUS_L_GENERIC_RESPONSE	"HTTP/1.%d %d %s" CRLF \
					"Connection: close" CRLF \
					"Server: " GLOBUS_GASS_HTTP_VERSION CRLF
#define GLOBUS_L_OK			"Ok"

#define GLOBUS_L_DENIAL_RESPONSE	"HTTP/1.1 %d %s" CRLF \
					"Connection: close" CRLF \
					"Server: " GLOBUS_GASS_HTTP_VERSION CRLF

#define GLOBUS_L_DEFAULT_DENIAL_MESSAGE	"Internal Server Error"

#define GLOBUS_L_CONTENT_LENGTH_HEADER	"Content-Length: %zu" CRLF
#define GLOBUS_L_CHUNKED_HEADER		"Transfer-Encoding: chunked" CRLF
#define GLOBUS_L_BINARY_HEADER		"Content-Type: " \
					    "application/octet-stream" CRLF
#define GLOBUS_L_TEXT_HEADER		"Content-Type: text/plain" CRLF
#define GLOBUS_L_HTML_HEADER		"Content-Type: text/html" CRLF
#define GLOBUS_L_HTML_REFERRAL_BODY_HEAD	\
					"<html><head><title>Document Moved</title></head><body>"
#define GLOBUS_L_HTML_REFERRAL_BODY_TAIL	\
					"</body></html>"
#define GLOBUS_L_HTML_DENIAL_BODY	"<html><head><title>%d %s</title></head><body>" CRLF \
					"<h1>%d %s</h1></body></html>" CRLF
#define GLOBUS_L_HTML_HREF		"<a href=\"%s\">%s</a><br>"
#define GLOBUS_L_LOCATION_HEADER	"Location: %s" CRLF
					
#define GLOBUS_L_DEFAULT_FAILURE_CODE	400
#define GLOBUS_L_DEFAULT_FAILURE_REASON	"Bad Request"

/* The "client failure" (4xx) response codes defined in RFC 2068 end at 415 */
#define GLOBUS_L_PROTOCOL_FAILURE_CODE	416
#define GLOBUS_L_PROTOCOL_FAILURE_REASON "Protocol Error"

#define GLOBUS_L_MALLOC_FAILURE_CODE	417
#define GLOBUS_L_MALLOC_FAILURE_REASON  "Malloc Error"

/******************************************************************************
			  Module specific Types
******************************************************************************/
typedef enum
{
    /* Client-only states */
    GLOBUS_GASS_TRANSFER_HTTP_STATE_CONNECTING,
    GLOBUS_GASS_TRANSFER_HTTP_STATE_REQUESTING,
    GLOBUS_GASS_TRANSFER_HTTP_STATE_CLOSING,
    GLOBUS_GASS_TRANSFER_HTTP_STATE_REFERRED,
    GLOBUS_GASS_TRANSFER_HTTP_STATE_DENIED,
    GLOBUS_GASS_TRANSFER_HTTP_STATE_RESPONDING,
    /* Common states */
    GLOBUS_GASS_TRANSFER_HTTP_STATE_IDLE,
    GLOBUS_GASS_TRANSFER_HTTP_STATE_DONE,
    GLOBUS_GASS_TRANSFER_HTTP_STATE_PENDING
} globus_gass_transfer_http_state_t;

/* These mirror the GASS listener states exactly */
typedef enum
{
    GLOBUS_GASS_TRANSFER_HTTP_LISTENER_STARTING,
    GLOBUS_GASS_TRANSFER_HTTP_LISTENER_LISTENING,
    GLOBUS_GASS_TRANSFER_HTTP_LISTENER_READY,
    GLOBUS_GASS_TRANSFER_HTTP_LISTENER_ACCEPTING,
    GLOBUS_GASS_TRANSFER_HTTP_LISTENER_CLOSING1,
    GLOBUS_GASS_TRANSFER_HTTP_LISTENER_CLOSING2,
    GLOBUS_GASS_TRANSFER_HTTP_LISTENER_CLOSED
} globus_gass_transfer_listener_state_t;
typedef enum
{
    GLOBUS_GASS_TRANSFER_HTTP_RECV_STATE_CHUNK_SIZE,
    GLOBUS_GASS_TRANSFER_HTTP_RECV_STATE_CHUNK_EXT,
    GLOBUS_GASS_TRANSFER_HTTP_RECV_STATE_CHUNK_END_HEADER_CR,
    GLOBUS_GASS_TRANSFER_HTTP_RECV_STATE_CHUNK_END_HEADER_LF,
    GLOBUS_GASS_TRANSFER_HTTP_RECV_STATE_CHUNK_BODY,
    GLOBUS_GASS_TRANSFER_HTTP_RECV_STATE_CHUNK_END_BODY_CR,
    GLOBUS_GASS_TRANSFER_HTTP_RECV_STATE_CHUNK_END_BODY_LF,
    GLOBUS_GASS_TRANSFER_HTTP_RECV_STATE_CHUNK_FOOTER,
    GLOBUS_GASS_TRANSFER_HTTP_RECV_STATE_UNTIL_LENGTH,
    GLOBUS_GASS_TRANSFER_HTTP_RECV_STATE_UNTIL_EOF,
    GLOBUS_GASS_TRANSFER_HTTP_RECV_STATE_EOF,
    GLOBUS_GASS_TRANSFER_HTTP_RECV_STATE_ERROR
} globus_l_gass_transfer_http_recv_state_t;

typedef struct 
{
    /* Standard "proto" elements */
    globus_gass_transfer_proto_listener_t  	close_listener;
    globus_gass_transfer_proto_listener_t	listen;
    globus_gass_transfer_proto_accept_t	 	accept;
    globus_gass_transfer_proto_listener_t	destroy;

    /* Begin internal http-specific proto state */
    globus_gass_transfer_listener_t		listener;
    globus_io_handle_t				handle;
    globus_url_scheme_t				url_scheme;

    globus_gass_transfer_listener_state_t	state;
    globus_bool_t				destroy_called;

    struct globus_gass_transfer_http_request_proto_s *	request;
} globus_gass_transfer_http_listener_proto_t;

typedef struct globus_gass_transfer_http_request_proto_s 
{
    /* Standard "proto" elements */
    globus_gass_transfer_proto_send_t		send_buffer;
    globus_gass_transfer_proto_receive_t	recv_buffer;

    globus_gass_transfer_proto_func_t		fail;

    globus_gass_transfer_proto_func_t		deny;
    globus_gass_transfer_proto_func_t		refer;
    globus_gass_transfer_proto_func_t		authorize;

    globus_gass_transfer_proto_func_t		destroy;

    /* Begin internal http-specific proto state */
    globus_io_handle_t				handle;
    /* last data for sending, and EOF on receiving */

    volatile globus_bool_t			oneshot_registered;
    volatile globus_bool_t			oneshot_active;

    volatile globus_gass_transfer_http_state_t	state;

    globus_gass_transfer_request_t		request;

    globus_bool_t				failure_occurred;
    globus_bool_t				destroy_called;

    /* Flags from the request attributes */
    globus_url_t				proxy_url;
    globus_bool_t				text_mode;
    globus_size_t				block_size;

    /* Type and name of the requested resource */
    globus_url_t				url;
    char *					url_string;
    globus_gass_transfer_request_type_t		type;

    /*
     * For the "send" case, a 4 item iovec array, containing
     * the chunk header, body, and trailing CRLF, and a 0-length
     * chunk + crlf
     */
    struct iovec				iov[4];
    /* Length and transfer encoding */
    globus_size_t				length;
    globus_size_t				handled;
    globus_bool_t				chunked;
    /* sending-side: are we handling the last data block? */
    globus_bool_t				last_data;

    globus_bool_t				client_side;
    /* Amount of data from the current chunk still needs to
       be given to the user */
    globus_size_t				chunk_left;
    globus_l_gass_transfer_http_recv_state_t	recv_state;
    globus_bool_t				eof_read;

    /* Used to parse/store responses from the HTTP server */
    globus_byte_t * 				response_buffer;	
    globus_size_t				response_buflen;
    globus_size_t				response_offset;
    globus_size_t				parsed_offset;
    int						code;
    char *					reason;
    globus_bool_t				parse_error;

    /* Major/minor version of the HTTP protocol we are using */
    int						major;
    int						minor;
    /* HTTP headers we've received */
    globus_list_t *				headers;

    /* Line mode of this particular file we are reading */
    globus_gass_transfer_http_line_mode_t	line_mode;
    /*
     * The buffer which was handed to the protocol module
     * from GASS
     */
    globus_byte_t *				user_buffer;
    globus_size_t				user_buflen;
    globus_size_t				user_offset;
    globus_size_t				user_waitlen;

    globus_gass_transfer_authorization_t	authorization_mode;
    char *					authorized_subject;
    char *					connected_subject;

    /* For handling requests from client */
    char *					uri;
    char *					method;
    globus_bool_t				proxy_connect;
    globus_bool_t				got_response;
    globus_bool_t				waiting_for_response;
} globus_gass_transfer_http_request_proto_t;

/******************************************************************************
			  Module specific Prototypes
******************************************************************************/
#if !defined(GLOBUS_GASS_TRANSFER_HTTP_PARSER_TEST)
static
void
globus_l_gass_transfer_http_send(
    globus_gass_transfer_request_proto_t *	proto,
    globus_gass_transfer_request_t		request,
    globus_byte_t *				buffer,
    globus_size_t				buffer_length,
    globus_bool_t				last_data);

static
void
globus_l_gass_transfer_http_receive(
    globus_gass_transfer_request_proto_t *	proto,
    globus_gass_transfer_request_t		request,
    globus_byte_t *				buffer,
    globus_size_t				buffer_length,
    globus_size_t				wait_for_length);

static
void
globus_l_gass_transfer_http_writev_callback(
    void *					callback_arg,
    globus_io_handle_t *			handle,
    globus_result_t 				result,
    struct iovec *				iov,
    globus_size_t				iovcnt,
    globus_size_t				nbytes);

static
void
globus_l_gass_transfer_http_write_callback(
    void *					callback_arg,
    globus_io_handle_t *			handle,
    globus_result_t				result,
    globus_byte_t *				buf,
    globus_size_t				nbytes);

static
void
globus_l_gass_transfer_http_write_response(
    void *				arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    globus_byte_t *			buf,
    globus_size_t			nbytes);

static
void
globus_l_gass_transfer_http_proto_destroy(
    globus_gass_transfer_http_request_proto_t *		proto);

static
void
globus_l_gass_transfer_http_read_callback(
    void *					callback_arg,
    globus_io_handle_t *			handle,
    globus_result_t				result,
    globus_byte_t *				buf,
    globus_size_t				nbytes);

static
void
globus_l_gass_transfer_http_read_buffered_callback(
    void *					callback_arg,
    globus_io_handle_t *			handle,
    globus_result_t				result,
    globus_byte_t *				buf,
    globus_size_t				nbytes);

static
void
globus_l_gass_transfer_http_callback_read_buffered_callback(
    void *					arg);

static
void
globus_l_gass_transfer_http_callback_ready_callback(
    void *					arg);
void
globus_l_gass_transfer_http_copy_text_buffer(
    globus_byte_t *				output,
    globus_byte_t *				input,
    globus_gass_transfer_http_line_mode_t *	line_mode,
    globus_size_t				input_max_to_copy,
    globus_size_t				output_max_to_copy,
    globus_size_t *				input_copied,
    globus_size_t *				output_copied);

static
void
globus_l_gass_transfer_http_fail(
    globus_gass_transfer_request_proto_t *	proto,
    globus_gass_transfer_request_t		request);

static
void
globus_l_gass_transfer_http_close_callback(
    void *					callback_arg,
    globus_io_handle_t *			handle,
    globus_result_t				result);

static
void
globus_l_gass_transfer_http_accept_callback(
    void *					callback_arg,
    globus_io_handle_t *			handle,
    globus_result_t				result);


static
void
globus_l_gass_transfer_http_destroy(
    globus_gass_transfer_request_proto_t *	proto,
    globus_gass_transfer_request_t		request);

static
void
globus_l_gass_transfer_http_new_request(
    globus_gass_transfer_request_t		request,
    globus_gass_transfer_requestattr_t *	attr);

static
globus_object_t *
globus_l_gass_transfer_http_new_requestattr(
    char *                              	url_scheme);

static
globus_object_t *
globus_l_gass_transfer_http_new_listenerattr(
    char *					url_scheme);

static
void
globus_l_gass_transfer_http_close_listener(
    globus_gass_transfer_listener_proto_t *	proto,
    globus_gass_transfer_listener_t		listener);

static
void
globus_l_gass_transfer_http_listen(
    globus_gass_transfer_listener_proto_t *	proto,
    globus_gass_transfer_listener_t		listener);

static
void
globus_l_gass_transfer_http_accept(
    globus_gass_transfer_listener_proto_t *	proto,
    globus_gass_transfer_listener_t		listener,
    globus_gass_transfer_request_t		request,
    globus_gass_transfer_requestattr_t *	attr);

static
globus_bool_t
globus_l_gass_transfer_http_authorization_callback(
    void *				arg,
    globus_io_handle_t *		handle,
    globus_result_t			result,
    char *				identity,
    gss_ctx_id_t  			context_handle);

static
void
globus_l_gass_transfer_http_listener_destroy(
    globus_gass_transfer_listener_proto_t *	proto,
    globus_gass_transfer_listener_t		listener);

static
int
globus_l_gass_transfer_http_new_listener(
    globus_gass_transfer_listener_t		listener,
    globus_gass_transfer_listenerattr_t *	attr,
    char *					scheme,
    char **					base_url,
    globus_gass_transfer_listener_proto_t **	proto);

static
void
globus_l_gass_transfer_http_connect_callback(
    void *					arg, 
    globus_io_handle_t *			handle, 
    globus_result_t				result);

static
void
globus_l_gass_transfer_http_command_callback(
    void *					arg, 
    globus_io_handle_t *			handle, 
    globus_result_t				result,
    globus_byte_t *				buf,
    globus_size_t				nbytes);

static
void
globus_l_gass_transfer_http_response_callback(
    void *					arg,
    globus_io_handle_t *			handle,
    globus_result_t				result,
    globus_byte_t *				buf,
    globus_size_t				nbytes);

static
void
globus_l_gass_transfer_http_listener_proto_destroy(
    globus_gass_transfer_http_listener_proto_t *
						proto);
static
void
globus_l_gass_transfer_http_callback_listen_callback(
    void *					arg);

static
void
globus_l_gass_transfer_http_listen_callback(
    void *				callback_arg,
    globus_io_handle_t *		handle,
    globus_result_t			result);
#endif

static
globus_bool_t
globus_l_gass_transfer_http_find_crlf(
    globus_byte_t *				bytes,
    globus_size_t				len,
    globus_size_t *				crlf_offset);

static
globus_bool_t
globus_l_gass_transfer_http_parse_headers(
    globus_gass_transfer_http_request_proto_t *		proto);

static
globus_bool_t
globus_l_gass_transfer_http_parse_one_header(
    globus_gass_transfer_http_request_proto_t *		proto,
    globus_bool_t *				last_header);

static
globus_bool_t
globus_l_gass_transfer_http_parse_status_line(
    globus_gass_transfer_http_request_proto_t *		proto);

static
globus_bool_t
islws(
    char 					byte);

static
globus_bool_t
ischar(
    char 					byte);

static
globus_bool_t
istspecial(
    char 					byte);

static
void
globus_l_gass_transfer_http_callback_send_callback(
    void *					arg);

static
void
globus_l_gass_transfer_http_request_callback(
    void *					arg,
    globus_io_handle_t *			handle,
    globus_result_t				result,
    globus_byte_t *				buf,
    globus_size_t				nbytes);

static
globus_result_t
globus_l_gass_transfer_http_register_read(
    globus_gass_transfer_http_request_proto_t *		proto);

static
char *
globus_l_gass_transfer_http_construct_request(
    globus_gass_transfer_http_request_proto_t *		proto);

static
globus_bool_t
globus_l_gass_transfer_http_handle_chunk(
    globus_gass_transfer_http_request_proto_t *		proto);

static
globus_bool_t
globus_l_gass_transfer_http_parse_response(
    globus_gass_transfer_http_request_proto_t *		proto);

static
globus_bool_t
globus_l_gass_transfer_http_parse_request(
    globus_gass_transfer_http_request_proto_t *		proto);

static
globus_bool_t
globus_l_gass_transfer_http_parse_request_line(
    globus_gass_transfer_http_request_proto_t *		proto);

static
void
globus_l_gass_transfer_http_extract_referral(
    globus_gass_transfer_http_request_proto_t *		proto,
    char ***						referral,
    globus_size_t *					referral_count);

static
void
globus_l_gass_transfer_http_callback_denied(
    void *					arg);

static
void
globus_l_gass_transfer_http_close(
    globus_gass_transfer_http_request_proto_t *		proto);

static
globus_result_t
globus_l_gass_transfer_http_register_close(
    globus_gass_transfer_http_request_proto_t *		proto);

static
void
globus_l_gass_transfer_http_listener_close(
    globus_gass_transfer_http_listener_proto_t * proto);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_L_GASS_TRANSFER_HTTP_H */

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
