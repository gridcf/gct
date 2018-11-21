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
 * @file globus_gram_protocol_frame.c
 * @brief Message Framing
 */

#include "globus_i_gram_protocol.h"
#include <string.h>

static
char *
globus_l_gram_protocol_lookup_reason(
    int					code);

#endif

/**
 * @defgroup globus_gram_protocol_framing Message Framing
 * @ingroup globus_gram_protocol_functions
 * @brief Message Framing
 *
 * @details
 * The functions in this section frame a GRAM request, query, or reply 
 * message with HTTP headers compatible with the GRAM2 protocol parsers in 
 * GT2 GT3, and GT4. 
 *
 * These functions should be used when an application wants to control
 * the way that the GRAM Protocol messages are sent, while still using
 * the standard message formatting and framing routines. An alternative
 * set of functions in the @ref globus_gram_protocol_io section of the manual
 * combine message framing with callback-driven I/O.
 */

/**
 * @brief Create a HTTP-framed copy of a GRAM request
 * @ingroup globus_gram_protocol_framing
 *
 * @details
 * The globus_gram_protocol_frame_request() function adds HTTP 1.1
 * framing around the input message. The framed message includes HTTP headers 
 * relating the the destination URL and the length of the message content.
 * The framed message is returned by modifying @a framedmsg to point to a
 * newly allocated string. The integer pointed to by the @a framedsize
 * parameter is set to the length of this message.
 *
 * @param url
 *        The URL of the GRAM resource to contact. This is parsed and used
 *        to generate the HTTP POST operation destination and the Host
 *        HTTP header.
 * @param msg
 *        A string containing the message content to be framed.
 * @param msgsize
 *        The length of the string pointed to by @a msg
 * @param framedmsg
 *        An output parameter which will be set to a copy of the @a msg string
 *        with an HTTP frame around it.
 * @param framedsize
 *        An output parameter which will be set to the length of the 
 *        framed message.
 *
 * @return
 *     Upon success, globus_gram_protocol_frame_request() will return
 *     GLOBUS_SUCCESS and the @a framedmsg and @a framedsize parameters will be
 *     modified to point to the new framed message string and its length
 *     respectively. When this occurs, the caller is responsible for freeing
 *     the string pointed to by @a framedmsg. If an error occurs, its value
 *     will returned and the @a framedmsg and @a framedsize parameters will 
 *     be uninitialized.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *     Invalid job contact
 */
int
globus_gram_protocol_frame_request(
    const char *			url,
    const globus_byte_t *		msg,
    globus_size_t			msgsize,
    globus_byte_t **			framedmsg,
    globus_size_t *			framedsize)
{
    char *				buf;
    globus_size_t			digits = 0;
    globus_size_t			tmp;
    globus_size_t			framedlen;
    globus_url_t			parsed;
    int					rc;

    rc = globus_url_parse(url, &parsed);

    if(rc != GLOBUS_SUCCESS)
    {
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT;

        goto out;
    }

    if (parsed.url_path == NULL)
    {
	rc = GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT;

        goto destroy_out;
    }

    /*
     * HTTP request message framing:
     *    POST <uri> HTTP/1.1<CR><LF>
     *    Host: <hostname><CR><LF>
     *    Content-Type: application/x-globus-gram<CR><LF>
     *    Content-Length: <msgsize><CR><LF>
     *    <CR><LF>
     *    <msg>
     */
    tmp = msgsize;

    do
    {
	tmp /= 10;
	digits++;
    }
    while(tmp > 0);

    framedlen  = strlen(GLOBUS_GRAM_HTTP_REQUEST_LINE);
    framedlen += strlen((char *) parsed.url_path);
    framedlen += strlen(GLOBUS_GRAM_HTTP_HOST_LINE);
    framedlen += strlen((char *) parsed.host);
    framedlen += strlen(GLOBUS_GRAM_HTTP_CONTENT_TYPE_LINE);
    framedlen += strlen(GLOBUS_GRAM_HTTP_CONTENT_LENGTH_LINE);
    framedlen += digits;
    framedlen += 2;
    framedlen += msgsize;

    buf = (char *) globus_libc_malloc(framedlen + 1 /*null terminator*/);

    tmp  = 0;
    tmp += globus_libc_sprintf(buf + tmp,
			      GLOBUS_GRAM_HTTP_REQUEST_LINE,
			      parsed.url_path);
    tmp += globus_libc_sprintf(buf + tmp,
			      GLOBUS_GRAM_HTTP_HOST_LINE,
			      parsed.host);
    tmp += globus_libc_sprintf(buf + tmp,
			       GLOBUS_GRAM_HTTP_CONTENT_TYPE_LINE);
    tmp += globus_libc_sprintf(buf + tmp,
			       GLOBUS_GRAM_HTTP_CONTENT_LENGTH_LINE,
			       (long) msgsize);
    tmp += globus_libc_sprintf(buf + tmp,
			       CRLF);

    if (msgsize > 0)    /* allow for empty message body (msg==NULL) */
    {
	memcpy(buf + tmp,
	       msg,
	       msgsize);
    }

    *framedmsg = (globus_byte_t *) buf;
    *framedsize = tmp + msgsize;

destroy_out:
    globus_url_destroy(&parsed);
out:
    return rc;
}

/**
 * @brief Create a HTTP-framed copy of a GRAM reply
 * @ingroup globus_gram_protocol_framing
 *
 * @details
 * The globus_gram_protocol_frame_reply() function adds HTTP 1.1
 * framing around the input message. The framed message includes HTTP headers 
 * relating the the status of the operation being replied to and the length of
 * the message content.  The framed message is returned by modifying
 * @a framedmsg to point to a newly allocated string. The integer pointed to by
 * the @a framedsize
 * parameter is set to the length of this message.
 *
 * @param code
 *        The HTTP response code to send along with this reply.
 * @param msg
 *        A string containing the reply message content to be framed.
 * @param msgsize
 *        The length of the string pointed to by @a msg.
 * @param framedmsg
 *        An output parameter which will be set to a copy of the @a msg
 *        string with an HTTP reply frame around it.
 * @param framedsize
 *        An output parameter which will be set to the length of the 
 *        framed reply string pointed to by @a framedmsg.
 *
 * @return
 *     Upon success, globus_gram_protocol_frame_reply() will return
 *     GLOBUS_SUCCESS and the @a framedmsg and @a framedsize parameters will be
 *     modified to point to the new framed message string and its length
 *     respectively. When this occurs, the caller is responsible for freeing
 *     the string pointed to by @a framedmsg. If an error occurs, its value
 *     will returned and the @a framedmsg and @a framedsize parameters will 
 *     be uninitialized.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 */
int
globus_gram_protocol_frame_reply(
    int					code,
    const globus_byte_t *		msg,
    globus_size_t			msgsize,
    globus_byte_t **			framedmsg,
    globus_size_t *			framedsize)
{
    char *				buf;
    char *				reason;
    globus_size_t			digits = 0;
    globus_size_t			tmp;
    globus_size_t			framedlen;

    /*
     * HTTP reply message framing:
     *    HTTP/1.1 <3 digit code> Reason String<CR><LF>
     *    Connection: close<CR><LF>
     *    <CR><LF>
     *
     * or
     *    HTTP/1.1 <3 digit code> Reason String<CR><LF>
     *    Content-Type: application/x-globus-gram<CR><LF>
     *    Content-Length: <msgsize><CR><LF>
     *    <CR><LF>
     *    msg
     */

    reason = globus_l_gram_protocol_lookup_reason(code);

    if(msgsize == 0)
    {
	framedlen = 0;
	framedlen += strlen(GLOBUS_GRAM_HTTP_REPLY_LINE);
	framedlen += strlen(reason);
	framedlen += strlen(GLOBUS_GRAM_HTTP_CONNECTION_LINE);

	buf = (char *) globus_malloc(framedlen + 1 /* null terminator */);

	tmp = 0;
	tmp += globus_libc_sprintf(buf + tmp,
				   GLOBUS_GRAM_HTTP_REPLY_LINE,
				   code,
				   reason);
	tmp += globus_libc_sprintf(buf + tmp,
				   GLOBUS_GRAM_HTTP_CONNECTION_LINE);
	tmp += globus_libc_sprintf(buf + tmp,
				   CRLF);
    }
    else
    {
	tmp = msgsize;

	do
	{
	    tmp /= 10;
	    digits++;
	}
	while(tmp > 0);

	framedlen = 0;
	framedlen += strlen(GLOBUS_GRAM_HTTP_REPLY_LINE);
	framedlen += strlen(reason);
	framedlen += strlen(GLOBUS_GRAM_HTTP_CONTENT_TYPE_LINE);
	framedlen += strlen(GLOBUS_GRAM_HTTP_CONTENT_LENGTH_LINE);
	framedlen += digits;
	framedlen += 2;
	framedlen += msgsize;

	buf = (char *) globus_malloc(framedlen);
	tmp = 0;
	tmp += globus_libc_sprintf(buf + tmp,
				   GLOBUS_GRAM_HTTP_REPLY_LINE,
				   code,
				   reason);
	tmp += globus_libc_sprintf(buf + tmp,
		       GLOBUS_GRAM_HTTP_CONTENT_TYPE_LINE);
	tmp += globus_libc_sprintf(buf + tmp,
		       GLOBUS_GRAM_HTTP_CONTENT_LENGTH_LINE,
		       (long)msgsize);
	tmp += globus_libc_sprintf(buf + tmp,
		       CRLF);

	if (msgsize > 0)   /* this allows msg = NULL */
	{
	    memcpy(buf + tmp,
		   msg,
		   msgsize);
	}
    }


    *framedmsg = (globus_byte_t *) buf;
    *framedsize = tmp + msgsize;

    return GLOBUS_SUCCESS;
}
/* globus_gram_protocol_frame_reply() */

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
static
char *
globus_l_gram_protocol_lookup_reason(
    int					code)
{
    char * reason = GLOBUS_NULL;

    /* These are culled from RFC 2616 */
    switch(code)
    {
    case 100: reason="Continue"; break;
    case 101: reason="Switching Protocols"; break;
    case 200: reason="OK"; break;
    case 201: reason="Created"; break;
    case 202: reason="Accepted"; break;
    case 203: reason="Non-Authoritative Information"; break;
    case 204: reason="No Content"; break;
    case 205: reason="Reset Content"; break;
    case 206: reason="Partial Content"; break;
    case 300: reason="Multiple Choices"; break;
    case 301: reason="Moved Permanently"; break;
    case 302: reason="Found"; break;
    case 303: reason="See Other"; break;
    case 304: reason="Not Modified"; break;
    case 305: reason="Use Proxy"; break;
    case 307: reason="Temporary Redirect"; break;
    case 400: reason="Bad Request"; break;
    case 401: reason="Unauthorized"; break;
    case 402: reason="Payment Required"; break;
    case 403: reason="Forbidden"; break;
    case 404: reason="Not Found"; break;
    case 405: reason="Method Not Allowed"; break;
    case 406: reason="Not Acceptable"; break;
    case 407: reason="Proxy Authentication Required"; break;
    case 408: reason="Request Time-out"; break;
    case 409: reason="Conflict"; break;
    case 410: reason="Gone"; break;
    case 411: reason="Length Required"; break;
    case 412: reason="Precondition Failed"; break;
    case 413: reason="Request Entity Too Large"; break;
    case 414: reason="Request-URI Too Large"; break;
    case 415: reason="Unsupported Media Type"; break;
    case 416: reason="Requested range not satisfiable"; break;
    case 417: reason="Expectation Failed"; break;
    case 500: reason="Internal Server Error"; break;
    case 501: reason="Not Implemented"; break;
    case 502: reason="Bad Gateway"; break;
    case 503: reason="Service Unavailable"; break;
    case 504: reason="Gateway Time-out"; break;
    case 505: reason="HTTP Version not supported"; break;
    default:
	if(code < 100 ||
	   code >= 600)
	{
	    reason="Internal Server Error";
	}
	else if(code < 200)
	{
	    reason="Continue";
	}
	else if(code < 300)
	{
	    reason="OK";
	}
	else if(code < 400)
	{
	    reason="Multiple Choices";
	}
	else if(code < 500)
	{
	    reason="Bad Request";
	}
	else if(code < 600)
	{
	    reason="Internal Server Error";
	}
    }
    return reason;
}
/* globus_l_gram_protocol_lookup_reason() */
#endif
