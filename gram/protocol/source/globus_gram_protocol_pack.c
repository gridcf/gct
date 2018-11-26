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
 * @file globus_gram_protocol_pack.c
 * @brief Message Packing
 */
#include "globus_i_gram_protocol.h"
#include <string.h>

static
globus_size_t
globus_l_gram_protocol_quote_string(
    const char *                        in,
    globus_byte_t *                     bufp);

static
int
globus_l_gram_protocol_unquote_string(
    const globus_byte_t *               inbuf,
    globus_size_t                       insize,
    char *                              out);

static
void
globus_l_gram_protocol_extension_destroy(
    void *                              datum);

static
int
globus_l_gram_protocol_get_int_attribute(
    globus_hashtable_t *                extensions,
    const char *                        attribute_name,
    int *                               value);

static
int
globus_l_gram_protocol_get_string_attribute(
    globus_hashtable_t *                extensions,
    const char *                        attribute_name,
    char **                             value);
#endif

/**
 * @defgroup globus_gram_protocol_pack Message Packing
 * @ingroup globus_gram_protocol_functions
 * @brief Message Packing
 */

/**
 * @defgroup globus_gram_protocol_unpack Message Unpacking
 * @ingroup globus_gram_protocol_functions
 * @brief Message Unpacking
 */

/**
 * @brief Pack a GRAM Job Request
 * @ingroup globus_gram_protocol_pack 
 *
 * @details
 * The globus_gram_protocol_pack_job_request() function combines its
 * parameters into a GRAM job request message body. The caller may frame
 * and send the resulting message by calling globus_gram_protocol_post()
 * or just frame it by calling globus_gram_protocol_frame_request() and send
 * it by some other mechanism. The globus_gram_protocol_pack_job_request()
 * function returns the packed message by modifying the @a query parameter to
 * point to a new string containing the message. The caller is responsible for
 * freeing that string.
 *
 * @param job_state_mask
 *     The bitwise-or of the GRAM job states which the client would like
 *     to register for job state change callbacks.
 * @param callback_url
 *     A callback contact string which will be contacted when a
 *     job state change which matches the @a job_state_mask occurs.
 *     This may be NULL, if the client does not wish to register
 *     a callback contact with this job request. Typically, this value
 *     is returned in the @a url parameter to
 *     globus_gram_protocol_allow_attach().
 * @param rsl
 *     An RSL string which contains the job request. This will
 *     be processed on the server side.
 * @param query
 *     An output parameter which will be set to a new
 *     string containing the packed job request message. The caller
 *     must free this memory by calling free()
 * @param querysize
 *     An output parameter which will be populated with the length
 *     of the job request message returned in @a query.
 *
 * @return
 *     Upon success, globus_gram_protocol_pack_job_request() returns
 *     GLOBUS_SUCCESS and modifies the  @a query and @a querysize parameters to
 *     point to the values described above.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *     Null parameter
 */
int
globus_gram_protocol_pack_job_request(
    int                                 job_state_mask,
    const char *                        callback_url,
    const char *                        rsl,
    globus_byte_t **                    query,
    globus_size_t *                     querysize)
{
    int                                 len;
    int                                 rc = GLOBUS_SUCCESS;

    if (query == NULL || rsl == NULL || querysize == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;
        goto null_param;
    }
    *query = malloc(
            strlen(GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE) +
            strlen(GLOBUS_GRAM_HTTP_PACK_JOB_STATE_MASK_LINE) +
            strlen(GLOBUS_GRAM_HTTP_PACK_CALLBACK_URL_LINE) +
            ((callback_url) ? strlen(callback_url) : 2)
            + 2*strlen(rsl) + 16);

    len = sprintf((char *) *query,
                              GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE
                              GLOBUS_GRAM_HTTP_PACK_JOB_STATE_MASK_LINE
                              GLOBUS_GRAM_HTTP_PACK_CALLBACK_URL_LINE
                              "rsl: ",
                              GLOBUS_GRAM_PROTOCOL_VERSION,
                              job_state_mask,
                              (callback_url) ? callback_url : "\"\"" );

    len += globus_l_gram_protocol_quote_string(rsl, (*query)+len );

    sprintf((char *)(*query)+len, "%s", CRLF);
    *querysize = (globus_size_t)(len+3);

null_param:
    return rc;
}
/* globus_gram_protocol_pack_job_request() */

/**
 * @brief Unpack a GRAM Job Request
 * @ingroup globus_gram_protocol_unpack
 *
 * @details
 * The globus_gram_protocol_unpack_job_request() function parses the
 * job request message packed in the @a query message and returns copies of
 * the standard message attributes in the @a job_state_mask, @a callback_url,
 * and @a description parameters.
 *
 * @param query
 *     The unframed job request message to parse.
 * @param querysize
 *     The length of the job request message string.
 * @param job_state_mask
 *     A pointer to an integer to be set to the job state
 *     mask from the job request.
 * @param callback_url
 *     A pointer to be set with a copy of the URL of the callback
 *     contact to be registered for this job request. The caller must
 *     free this memory by calling free().
 * @param description
 *     A pointer to be set to a copy of the job description RSL
 *     string for this job request. The caller must
 *     free this memory by calling free().
 *
 * @return
 *     Upon success, globus_gram_protocol_unpack_job_request() will
 *     return @a GLOBUS_SUCCESS and modify the @a job_state_mask,
 *     @a callback_url, and @a description parameters to values extracted
 *     from the message in @a query. If an error occurs, an integer error code
 *     will be returned and the values of @a job_state_mask, @a callback_url,
 *     and @a description will be undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *     Null parameter
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED
 *     Unpack failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH
 *     Version mismatch
 */
int
globus_gram_protocol_unpack_job_request(
    const globus_byte_t *               query,
    globus_size_t                       querysize,
    int  *                              job_state_mask,
    char **                             callback_url,
    char **                             description)
{
    int                                 protocol_version;
    int                                 rc;
    globus_hashtable_t                  attributes;

    if (query == NULL || job_state_mask == NULL || callback_url == NULL ||
        description == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto bad_param;
    }

    *job_state_mask = 0;
    *callback_url = NULL;
    *description = NULL;

    rc = globus_gram_protocol_unpack_message(
            (const char *) query,
            querysize,
            &attributes);
    if (rc != GLOBUS_SUCCESS)
    {
        goto parse_error;
    }

    rc = globus_l_gram_protocol_get_int_attribute(
            &attributes,
            GLOBUS_GRAM_ATTR_PROTOCOL_VERSION,
            &protocol_version);

    if (rc != GLOBUS_SUCCESS)
    {
        goto version_error;
    }

    if (protocol_version != GLOBUS_GRAM_PROTOCOL_VERSION)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH;

        goto version_mismatch;
    }

    rc = globus_l_gram_protocol_get_int_attribute(
            &attributes,
            GLOBUS_GRAM_ATTR_JOB_STATE_MASK,
            job_state_mask);
    if (rc != GLOBUS_SUCCESS)
    {
        goto job_state_mask_failed;
    }

    rc = globus_l_gram_protocol_get_string_attribute(
            &attributes,
            GLOBUS_GRAM_ATTR_CALLBACK_URL,
            callback_url);
    if (rc != GLOBUS_SUCCESS)
    {
        goto callback_url_failed;
    }

    if (*callback_url[0] == '\0')
    {
        free(*callback_url);
        *callback_url = NULL;
    }

    rc = globus_l_gram_protocol_get_string_attribute(
            &attributes,
            GLOBUS_GRAM_ATTR_RSL,
            description);

    if (rc != GLOBUS_SUCCESS)
    {
        if (*callback_url)
        {
            free(*callback_url);
            *callback_url = NULL;
        }
    }
callback_url_failed:
job_state_mask_failed:
version_mismatch:
version_error:
    globus_gram_protocol_hash_destroy(&attributes);
parse_error:
bad_param:
    return rc;
}
/* globus_gram_protocol_unpack_job_request() */

/**
 * @brief Pack a GRAM reply message
 * @ingroup globus_gram_protocol_pack
 *
 * @details
 * The globus_gram_protocol_pack_job_request_reply() function combines its
 * parameters into a GRAM reply message body. The caller may frame
 * and send the resulting message by calling globus_gram_protocol_reply()
 * or just frame it by calling globus_gram_protocol_frame_reply() and send
 * it by some other mechanism. The 
 * globus_gram_protocol_pack_job_request_reply()
 * function returns the packed message by modifying the @a reply parameter to
 * point to a new string containing the message. The caller is responsible for
 * freeing that string.
 *
 * @param status
 *     The job's failure code if the job failed, or 0, if the job
 *     request was processed successfully.
 * @param job_contact
 *     A string containing the job contact string. This may be
 *     NULL, if the job request was not successful.
 * @param reply
 *     A pointer which will be set to the packed reply string
 *     The caller must free this string by calling free().
 * @param replysize
 *     A pointer which will be set to the length of the reply string.
 *
 * @return
 *     Upon success, globus_gram_protocol_pack_job_request_reply() returns
 *     @a GLOBUS_SUCCESS and modifies the  @a reply and @a replysize parameters
 *     to point to the values described above. If an error occurs, an integer
 *     error code is returned and the values pointed to by @a reply and
 *     @a replysize are undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_MALLOC_FAILED
 *     Out of memory
 */
int
globus_gram_protocol_pack_job_request_reply(
    int                                 status,
    const char *                        job_contact,    /* may be null */
    globus_byte_t **                    reply,
    globus_size_t *                     replysize )
{
    *reply = globus_libc_malloc(
                       strlen(GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE) +
                       strlen(GLOBUS_GRAM_HTTP_PACK_STATUS_LINE) +
                       strlen(GLOBUS_GRAM_HTTP_PACK_JOB_MANAGER_URL_LINE) +
                       ((job_contact) ? strlen(job_contact) + 3 : 3));
    if(*reply == GLOBUS_NULL)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }

    if (job_contact)
        globus_libc_sprintf( (char *) *reply,
                             GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE
                             GLOBUS_GRAM_HTTP_PACK_STATUS_LINE
                             GLOBUS_GRAM_HTTP_PACK_JOB_MANAGER_URL_LINE,
                             GLOBUS_GRAM_PROTOCOL_VERSION,
                             status,
                             job_contact );
    else
        globus_libc_sprintf( (char *) *reply,
                             GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE
                             GLOBUS_GRAM_HTTP_PACK_STATUS_LINE,
                             GLOBUS_GRAM_PROTOCOL_VERSION,
                             status );

    *replysize = (globus_size_t)(strlen((char *) *reply) + 1);
    return GLOBUS_SUCCESS;
}
/* globus_gram_protocol_pack_job_request_reply() */

/**
 * @brief Unpack a GRAM reply message
 * @ingroup globus_gram_protocol_unpack
 *
 * @details
 * The globus_gram_protocol_unpack_job_request_reply() function parses the
 * reply message packed in the @a reply message and returns copies of
 * the standard message attributes in the @a status and @a job_contact
 * parameters. 
 *
 * @param reply
 *     The unframed job reply message to parse.
 * @param replysize
 *     The length of the reply string.
 * @param status
 *     A pointer to an integer to be set to the failure code
 *     associated with the job request. This may be GLOBUS_SUCCESS,
 *     if the job request was successful.
 * @param job_contact
 *     A pointer to a string to be set to the job contact string.  This may set
 *     to NULL if the job request failed. If
 *     globus_gram_protocol_unpack_job_request_reply() returns GLOBUS_SUCCESS,
 *     then the caller must free this string using free().
 *
 * @return
 *     Upon success, globus_gram_protocol_unpack_job_request_reply() returns
 *     @a GLOBUS_SUCCESS and modifies the  @a status and @a job_contact
 *     parameters to point to the values described above. If an error occurs,
 *     an integer error code is returned and the values pointed to by @a status
 *     and @a job_contact are undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAN_PROTOCOL_ERROR_NULL_PARAMETER
 *     Null parameter
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED
 *     Unpack failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH
 *     Version mismatch
 */
int
globus_gram_protocol_unpack_job_request_reply(
    const globus_byte_t *               reply,
    globus_size_t                       replysize,
    int *                               status,
    char **                             job_contact )
{
    int                                 rc = GLOBUS_SUCCESS;
    int                                 protocol_version;
    globus_hashtable_t                  message_attributes;

    if (reply == NULL || status == NULL || job_contact == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto bad_param;
    }
    *status = 0;
    *job_contact = NULL;

    rc = globus_gram_protocol_unpack_message(
            (const char *) reply,
            replysize,
            &message_attributes);
    if (rc != GLOBUS_SUCCESS)
    {
        goto parse_error;
    }

    rc = globus_l_gram_protocol_get_int_attribute(
            &message_attributes,
            GLOBUS_GRAM_ATTR_PROTOCOL_VERSION,
            &protocol_version);
    if (rc != GLOBUS_SUCCESS)
    {
        goto version_error;
    }
    if (protocol_version != GLOBUS_GRAM_PROTOCOL_VERSION)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH;

        goto version_mismatch;
    }

    rc = globus_l_gram_protocol_get_int_attribute(
            &message_attributes,
            GLOBUS_GRAM_ATTR_STATUS,
            status);
    if (rc != GLOBUS_SUCCESS)
    {
        goto status_failed;
    }

    /* Only if the job is successfully created */
    rc = globus_l_gram_protocol_get_string_attribute(
            &message_attributes,
            GLOBUS_GRAM_ATTR_JOB_MANAGER_URL,
            job_contact);
    if (rc != GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED)
    {
        rc = GLOBUS_SUCCESS;
    }

status_failed:
version_mismatch:
version_error:
    globus_gram_protocol_hash_destroy(&message_attributes);
parse_error:
bad_param:
    return rc;
}
/* globus_gram_protocol_unpack_job_request_reply() */

/**
 * @brief Pack a GRAM reply message with extension attributes
 * @ingroup globus_gram_protocol_pack
 *
 * @details
 * The globus_gram_protocol_pack_job_request_reply_with_extensions()
 * function combines its parameters into a GRAM reply message body. The
 * caller may frame and send the resulting message by calling
 * globus_gram_protocol_reply() or just frame it by calling
 * globus_gram_protocol_frame_reply() and send it by some other mechanism.
 * The globus_gram_protocol_pack_job_request_reply_with_extensions()
 * function returns the packed message by modifying the @a reply parameter to
 * point to a new string containing the message. The caller is responsible for
 * freeing that string.
 *
 * @param status
 *     The job's failure code if the job failed, or 0, if the job
 *     request was processed successfully.
 * @param job_contact
 *     A string containing the job contact string. This may be
 *     NULL, if the job request was not successful.
 * @param extensions
 *     A pointer to a hash table keyed on a string attribute name with the
 *     hash values being pointers to @a globus_gram_protocol_extension_t
 *     structures. These will be encoded in the reply message after the
 *     standard attributes.
 * @param reply
 *     A pointer which will be set to the packed reply string
 *     The caller must free this string by calling free().
 * @param replysize
 *     A pointer which will be set to the length of the reply string.
 *
 * @return
 *     Upon success,
 *     globus_gram_protocol_pack_job_request_reply_with_extensions() returns
 *     @a GLOBUS_SUCCESS and modifies the  @a reply and @a replysize parameters
 *     to point to the values described above. If an error occurs, an integer
 *     error code is returned and the values pointed to by @a reply and
 *     @a replysize are undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_MALLOC_FAILED
 *     Out of memory
 */
int
globus_gram_protocol_pack_job_request_reply_with_extensions(
    int					status,
    const char *			job_contact,    /* may be null */
    globus_hashtable_t *                extensions,
    globus_byte_t **			reply,
    globus_size_t *			replysize)
{
    globus_gram_protocol_extension_t * entry;
    size_t                              len = 0;
    int                                 chrs;
    int                                 rc = GLOBUS_SUCCESS;

    if (reply != NULL)
    {
        *reply = NULL;
    }

    if (replysize != NULL)
    {
        *replysize = 0;
    }
    if (extensions == NULL || reply == NULL || replysize == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto bad_param;
    }

    for (entry = globus_hashtable_first(extensions);
         entry != NULL;
         entry = globus_hashtable_next(extensions))
    {
        if (entry->attribute == NULL || entry->value == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_PACK_FAILED;

            goto bad_attr;
        }
        len += strlen(entry->attribute) + (2*strlen(entry->value)) + 4;
    }
    len += strlen(GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE) +
           strlen(GLOBUS_GRAM_HTTP_PACK_STATUS_LINE) +
           (job_contact
            ? strlen(GLOBUS_GRAM_HTTP_PACK_JOB_MANAGER_URL_LINE)
            : 0) +
           (job_contact
            ? strlen(job_contact) : 0) +
           4;

    *reply = malloc(len);
    if(*reply == GLOBUS_NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto reply_malloc_failed;
    }

    if (job_contact)
    {
        chrs = sprintf((char *)*reply,
                GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE
                GLOBUS_GRAM_HTTP_PACK_STATUS_LINE
                GLOBUS_GRAM_HTTP_PACK_JOB_MANAGER_URL_LINE,
                GLOBUS_GRAM_PROTOCOL_VERSION,
                status,
                job_contact);
    }
    else
    {
        chrs = sprintf((char *)*reply,
                GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE
                GLOBUS_GRAM_HTTP_PACK_STATUS_LINE,
                GLOBUS_GRAM_PROTOCOL_VERSION,
                status);
    }

    for (entry = globus_hashtable_first(extensions);
         entry != NULL;
         entry = globus_hashtable_next(extensions))
    {
        chrs += sprintf(((char *) *reply) + chrs,
                "%s: ",
                entry->attribute);

        chrs += globus_l_gram_protocol_quote_string(
                entry->value,
                *reply + chrs);

        chrs += sprintf(((char *) *reply) + chrs, "\r\n");
    }

    *replysize = (globus_size_t)(strlen((char *)*reply) + 1);

reply_malloc_failed:
bad_attr:
bad_param:
    return rc;
}
/* globus_gram_protocol_pack_job_request_reply_with_extensions() */

/**
 * @brief Unpack a GRAM reply message, parsing all extensions
 * @ingroup globus_gram_protocol_unpack
 *
 * @details
 * The globus_gram_protocol_unpack_job_request_reply_with_extensions()
 * function parses the reply message packed in the @a reply message parameter
 * and returns copies of the standard message attributes in the @a status and
 * @a job_contact parameters, and all other extension attributes in the
 * hashtable pointed to by @a extensions. Each entry in the hashtable will
 * be keyed by the attribute name and the value will be a pointer to a
 * @a globus_gram_protocol_extension_t structure.
 *
 * @param status
 *     A pointer to an integer to be set to the failure code
 *     associated with the job request. This may be GLOBUS_SUCCESS,
 *     if the job request was successful.
 * @param job_contact
 *     A pointer to a string to be set to the job contact string.  This may set
 *     to NULL if the job request failed. If
 *     globus_gram_protocol_unpack_job_request_reply_with_extensions() returns
 *     GLOBUS_SUCCESS, then the caller must free this string using free().
 * @param extensions
 *     A pointer to be set to a hash table containing the
 *     names and values of all protocol extensions present in the response
 *     message. If
 *     globus_gram_protocol_unpack_job_request_reply_with_extensions()
 *     returns GLOBUS_SUCCESS, the caller must free this hash table and its
 *     values by calling globus_gram_protocol_hash_destroy().
 * @param reply
 *     The unframed job reply message to parse.
 * @param replysize
 *     The length of the reply string.
 *
 * @return
 *     Upon success,
 *     globus_gram_protocol_unpack_job_request_reply_with_extensions()
 *     returns @a GLOBUS_SUCCESS and modifies the  @a status, @a job_contact,
 *     and @a extensions to point to the values described above. If an error
 *     occurs, an integer error code is returned and the values pointed to by
 *     @a status, @a job_contact, and @a extensions are undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAN_PROTOCOL_ERROR_NULL_PARAMETER
 *     Null parameter
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED
 *     Unpack failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH
 *     Version mismatch
 */
int
globus_gram_protocol_unpack_job_request_reply_with_extensions(
    const globus_byte_t *		reply,
    globus_size_t			replysize,
    int *				status,
    char **				job_contact,
    globus_hashtable_t *                extensions)
{
    globus_gram_protocol_extension_t * entry = NULL;
    int                                 rc;

    if (reply == NULL || status == NULL || job_contact == NULL || 
            extensions == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto bad_param;
    }
    *status = 0;
    *job_contact = NULL;
    *extensions = NULL;

    rc = globus_gram_protocol_unpack_message(
            (char *) reply,
            replysize,
            extensions);

    if (rc != GLOBUS_SUCCESS)
    {
        goto parse_error;
    }

    /* Check that required attributes are present */
    entry = globus_hashtable_lookup(
            extensions,
            "protocol-version");
    if (entry == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

        goto verify_error;
    }
    if (strtol(entry->value, NULL, 10) != GLOBUS_GRAM_PROTOCOL_VERSION)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH;

        goto verify_error;
    }

    entry = globus_hashtable_lookup(
            extensions,
            "status");
    if (entry == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
        
        goto verify_error;
    }
    *status = atoi(entry->value);

    entry = globus_hashtable_lookup(
            extensions,
            "job-manager-url");
    /* This may not be present if the job failed before replying, such as
     * unparsable or invalid RSL
     */
    if (entry != NULL)
    {
        *job_contact = strdup(entry->value);
        if (*job_contact == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto copy_contact_failed;
        }
    }

    rc = GLOBUS_SUCCESS;

    if (rc != GLOBUS_SUCCESS)
    {
copy_contact_failed:
verify_error:
        globus_gram_protocol_hash_destroy(extensions);
    }
parse_error:
bad_param:

    return rc;
}

/**
 * @brief Pack a GRAM query message
 * @ingroup globus_gram_protocol_pack 
 *
 * @details
 * The globus_gram_protocol_pack_status_request()
 * function combines its parameters into a GRAM status query message body. The
 * caller may frame and send the resulting message by calling
 * globus_gram_protocol_post() or just frame it by calling
 * globus_gram_protocol_frame_request() and send it by some other mechanism.
 * The globus_gram_protocol_pack_status_request()
 * function returns the packed message by modifying the @a query parameter to
 * point to a new string containing the message. The caller is responsible for
 * freeing that string.
 *
 * @param status_request
 *     A string containing the type of query message to send, including any
 *     query parameters. The valid strings supported by GRAM in GT5 are:
 *     - status
 *     - register
 *     - unregister
 *     - signal
 *     - renew
 *     - cancel
 * @param query
 *     An output parameter which will be set to a new
 *     string containing the packed job query message.
 * @param querysize
 *     An output parameter which will be set to the length
 *     of the job query message returned in @a query.
 *
 * @return
 *     Upon success,
 *     globus_gram_protocol_pack_status_request() returns
 *     @a GLOBUS_SUCCESS and modifies the  @a query and @a querysize parameters
 *     to point to the values described above. If an error occurs, an integer
 *     error code is returned and the values pointed to by @a query and
 *     @a querysize are undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_MALLOC_FAILED
 *     Out of memory
 */
int
globus_gram_protocol_pack_status_request(
    const char *                        status_request,
    globus_byte_t **                    query,
    globus_size_t *                     querysize)
{
    globus_size_t                       len;

    *query = globus_libc_malloc(
                       strlen(GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE) +
                       strlen(GLOBUS_GRAM_HTTP_PACK_CLIENT_REQUEST_LINE) +
                       2*strlen(status_request));
    if(*query == GLOBUS_NULL)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }

    len = globus_libc_sprintf( (char *) *query,
                               GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE,
                               GLOBUS_GRAM_PROTOCOL_VERSION );

    len += globus_l_gram_protocol_quote_string( status_request,
                                            (*query) + len );

    globus_libc_sprintf( (char *)(*query)+len, CRLF);

    *querysize = (globus_size_t)(strlen((char*)*query) + 1);

    return GLOBUS_SUCCESS;
}
/* globus_gram_protocol_pack_status_request() */

/**
 * @brief Unpack a GRAM query message
 * @ingroup globus_gram_protocol_unpack
 *
 * @details
 * The globus_gram_protocol_unpack_status_request()
 * function parses the message packed in the @a query parameter
 * and returns a copy of the message in the @a status_request parameter.
 *
 * @param query
 *     The unframed query message to parse.
 * @param querysize
 *     The length of the query string.
 * @param status_request
 *     A pointer to a string to be set to the query 
 *     value.  The caller must free this string using free().
 *
 * @return
 *     Upon success,
 *     globus_gram_protocol_unpack_status_request() returns
 *     @a GLOBUS_SUCCESS and modifies the  @a status_request parameter
 *     to point to the value described above. If an error occurs, an integer
 *     error code is returned and the value pointed to by @a status_request
 *     is undefined.

 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *     Null parameter
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED
 *     Unpack failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH
 *     Version mismatch
 */
int
globus_gram_protocol_unpack_status_request(
    const globus_byte_t *               query,
    globus_size_t                       querysize,
    char **                             status_request)
{
    int                                 rc;
    int                                 protocol_version;
    char *                              p;
    globus_size_t                       msgsize;

    if (query == NULL || status_request == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto null_param;
    }
    p = strstr((char *) query, CRLF);
    if (!p)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
        goto no_crlf;
    }

    p+=2;
    msgsize = querysize - (globus_size_t)(p-(char *)query);
    *status_request = malloc(msgsize);
    if (*status_request == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto malloc_failed;
    }

    rc = sscanf((char *) query,
                 GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE,
                 &protocol_version);
    if (rc != 1)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
        goto scan_failed;
    }
    else if (protocol_version != GLOBUS_GRAM_PROTOCOL_VERSION)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH;
        goto version_mismatch;
    }

    rc = globus_l_gram_protocol_unquote_string(
                  (globus_byte_t*) p,
                  msgsize,
                  *status_request);

version_mismatch:
scan_failed:
    if (rc != GLOBUS_SUCCESS)
    {
        free(*status_request);
        *status_request = GLOBUS_NULL;
    }
malloc_failed:
no_crlf:
null_param:

    return rc;
}
/* globus_gram_protocol_unpack_status_request() */

/**
 * @brief Pack a GRAM query reply message 
 * @ingroup globus_gram_protocol_pack
 *
 * @details
 * The globus_gram_protocol_pack_status_reply()
 * function combines its parameters into a GRAM status reply message body. The
 * caller may frame and send the resulting message by calling
 * globus_gram_protocol_reply() or just frame it by calling
 * globus_gram_protocol_frame_reply() and send it by some other mechanism.
 * The globus_gram_protocol_pack_status_reply()
 * function returns the packed message by modifying the @a reply parameter to
 * point to a new string containing the message. The caller is responsible for
 * freeing that string.
 *
 * @param job_status
 *     The job's current
 *     @ref globus_gram_protocol_job_state_t "job state".
 * @param failure_code
 *     The error code generated by the query. This may be GLOBUS_SUCCESS
 *     if the query succeeded.
 * @param job_failure_code
 *     The error code associated with the job if it has failed. This may
 *     be GLOBUS_SUCCESS if the job has not failed.
 * @param reply
 *     An output parameter which will be set to a new
 *     string containing the packed reply message.
 * @param replysize
 *     An output parameter which will be set to the length
 *     of the reply message returned in @a reply.
 *
 * @return
 *     Upon success,
 *     globus_gram_protocol_pack_status_reply() returns
 *     @a GLOBUS_SUCCESS and modifies the  @a reply and @a replysize parameters
 *     to point to the values described above. If an error occurs, an integer
 *     error code is returned and the values pointed to by @a reply and
 *     @a replysize are undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_MALLOC_FAILED
 *     Out of memory
 */
int
globus_gram_protocol_pack_status_reply(
    int                                 job_status,
    int                                 failure_code,
    int                                 job_failure_code,
    globus_byte_t **                    reply,
    globus_size_t *                     replysize)
{
    *reply = globus_libc_malloc(
                        strlen(GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE) +
                        strlen(GLOBUS_GRAM_HTTP_PACK_STATUS_LINE) +
                        strlen(GLOBUS_GRAM_HTTP_PACK_FAILURE_CODE_LINE) +
                        strlen(GLOBUS_GRAM_HTTP_PACK_JOB_FAILURE_CODE_LINE)
                        + 4 );
    if(*reply == GLOBUS_NULL)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }

    globus_libc_sprintf((char *)*reply,
                        GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE
                        GLOBUS_GRAM_HTTP_PACK_STATUS_LINE
                        GLOBUS_GRAM_HTTP_PACK_FAILURE_CODE_LINE
                        GLOBUS_GRAM_HTTP_PACK_JOB_FAILURE_CODE_LINE,
                        GLOBUS_GRAM_PROTOCOL_VERSION,
                        job_status,
                        failure_code,
                        job_failure_code);

    *replysize = (globus_size_t)(strlen((char *)*reply) + 1);

    return GLOBUS_SUCCESS;
}
/* globus_gram_protocol_pack_status_reply() */

/**
 * @brief Unpack a GRAM query reply
 * @ingroup globus_gram_protocol_unpack
 * @details
 * The globus_gram_protocol_unpack_status_reply()
 * function parses the message packed in the @a reply parameter
 * and sets the current job state, protocol failure code, and job failure code
 * values in its output parameters.
 *
 * @param reply
 *     The unframed reply message to parse.
 * @param replysize
 *     The length of the reply message.
 * @param job_status
 *     A pointer to an integer to be set to the job's current @ref
 *     globus_gram_protocol_job_state_t "job state".
 * @param failure_code
 *     A pointer to an integer to be set to the failure code
 *     associated with the query request. This may be GLOBUS_SUCCESS,
 *     if the request was successful.
 * @param job_failure_code
 *     A pointer to an integer to be set to the failure code
 *     for the job, if the @a job_status is
 *     GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED.
 *
 * @return
 *     Upon success, globus_gram_protocol_unpack_status_reply() returns
 *     @a GLOBUS_SUCCESS and modifies the  @a job_status, @a failure_code,
 *     and @a job_failure_code parameters to point to the value described
 *     above. If an error occurs, an integer error code is returned and the
 *     values pointed to by @a job_status, @a failure_code, and
 *     @a job_failure_code are undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED
 *     Unpack failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH
 *     Version mismatch
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *     Null parameter
 */
int
globus_gram_protocol_unpack_status_reply(
    const globus_byte_t *               reply,
    globus_size_t                       replysize,
    int *                               job_status,
    int *                               failure_code,
    int *                               job_failure_code)
{
    int                                 protocol_version;
    int                                 rc;

    if (job_status == NULL || failure_code == NULL || job_failure_code == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto null_param;
    }
    rc = sscanf( (char *) reply,
                 GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE
                 GLOBUS_GRAM_HTTP_PACK_STATUS_LINE
                 GLOBUS_GRAM_HTTP_PACK_FAILURE_CODE_LINE
                 GLOBUS_GRAM_HTTP_PACK_JOB_FAILURE_CODE_LINE,
                 &protocol_version,
                 job_status,
                 failure_code,
                 job_failure_code );
    if (rc == 3)
    {
        *job_failure_code = 0;
    }
    if (rc != 3 && rc != 4)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
    }
    else if (protocol_version != GLOBUS_GRAM_PROTOCOL_VERSION)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH;
    }
    else
    {
        rc = GLOBUS_SUCCESS;
    }

null_param:
    return rc;
}
/* globus_gram_protocol_unpack_status_reply() */

/**
 * @brief Pack a GRAM query reply message with extensions
 * @ingroup globus_gram_protocol_pack
 *
 * @details
 * The globus_gram_protocol_pack_status_reply_with_extensions()
 * function combines its parameters into a GRAM status reply message body. The
 * caller may frame and send the resulting message by calling
 * globus_gram_protocol_reply() or just frame it by calling
 * globus_gram_protocol_frame_reply() and send it by some other mechanism.
 * The globus_gram_protocol_pack_status_reply_with_extensions()
 * function returns the packed message by modifying the @a reply parameter to
 * point to a new string containing the message. The caller is responsible for
 * freeing that string.
 *
 * @param job_status
 *     The job's current
 *     @ref globus_gram_protocol_job_state_t "job state".
 * @param failure_code
 *     The error code generated by the query. This may be GLOBUS_SUCCESS
 *     if the query succeeded.
 * @param job_failure_code
 *     The error code associated with the job if it has failed. This may
 *     be GLOBUS_SUCCESS if the job has not failed.
 * @param extensions
 *     A pointer to a hash table containing the 
 *     names and values of the protocol extensions to add to this message.
 * @param reply
 *     An output parameter which will be set to a new
 *     string containing the packed reply message.
 * @param replysize
 *     An output parameter which will be set to the length
 *     of the reply message returned in @a reply.
 *
 * @return
 *     Upon success,
 *     globus_gram_protocol_pack_status_reply_with_extensions() returns
 *     @a GLOBUS_SUCCESS and modifies the  @a reply and @a replysize parameters
 *     to point to the values described above. If an error occurs, an integer
 *     error code is returned and the values pointed to by @a reply and
 *     @a replysize are undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_MALLOC_FAILED
 *     Out of memory
 */
int
globus_gram_protocol_pack_status_reply_with_extensions(
    int                                 job_status,
    int                                 failure_code,
    int                                 job_failure_code,
    globus_hashtable_t *                extensions,
    globus_byte_t **                    reply,
    globus_size_t *                     replysize)
{
    globus_gram_protocol_extension_t * entry;
    size_t                              len = 0;
    int                                 chrs;
    int                                 rc = GLOBUS_SUCCESS;

    if (reply != NULL)
    {
        *reply = NULL;
    }

    if (replysize != NULL)
    {
        *replysize = 0;
    }
    if (extensions == NULL || reply == NULL || replysize == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto bad_param;
    }

    for (entry = globus_hashtable_first(extensions);
         entry != NULL;
         entry = globus_hashtable_next(extensions))
    {
        if (entry->attribute == NULL || entry->value == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_PACK_FAILED;

            goto bad_attr;
        }
        len += strlen(entry->attribute) + (2*strlen(entry->value)) + 4;
    }
    len += strlen(GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE) +
           strlen(GLOBUS_GRAM_HTTP_PACK_STATUS_LINE) +
           strlen(GLOBUS_GRAM_HTTP_PACK_FAILURE_CODE_LINE) +
           strlen(GLOBUS_GRAM_HTTP_PACK_JOB_FAILURE_CODE_LINE)
           + 4;

    *reply = malloc(len);
    if(*reply == GLOBUS_NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto reply_malloc_failed;
    }

    chrs = sprintf((char *)*reply,
            GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE
            GLOBUS_GRAM_HTTP_PACK_STATUS_LINE
            GLOBUS_GRAM_HTTP_PACK_FAILURE_CODE_LINE
            GLOBUS_GRAM_HTTP_PACK_JOB_FAILURE_CODE_LINE,
            GLOBUS_GRAM_PROTOCOL_VERSION,
            job_status,
            failure_code,
            job_failure_code);

    for (entry = globus_hashtable_first(extensions);
         entry != NULL;
         entry = globus_hashtable_next(extensions))
    {
        chrs += sprintf(((char *) *reply) + chrs,
                "%s: ",
                entry->attribute);

        chrs += globus_l_gram_protocol_quote_string(
                entry->value,
                *reply + chrs);

        chrs += sprintf(((char *) *reply) + chrs, "\r\n");
    }

    *replysize = (globus_size_t)(strlen((char *)*reply) + 1);

reply_malloc_failed:
bad_attr:
bad_param:
    return rc;
}
/* globus_gram_protocol_pack_status_reply_with_extensions() */

/**
 * @brief Unpack a GRAM query reply with extensions
 * @ingroup globus_gram_protocol_unpack
 * @details
 * The globus_gram_protocol_unpack_status_reply_with_extensions()
 * function parses the message packed in the @a reply parameter,
 * storing all attributes and values in a hash table. The @a extensions
 * parameter is modified to point to that hash table. The caller of
 * globus_gram_protocol_unpack_status_reply_with_extensions() must
 * free that hash table by calling globus_gram_protocol_hash_destroy().
 *
 * @param reply
 *     The unframed reply message to parse.
 * @param replysize
 *     The length of the reply message.
 * @param extensions
 *     A pointer to be set to a hash table containing the
 *     names and values of all protocol attributes present in the reply
 *     message. If
 *     globus_gram_protocol_unpack_status_reply_with_extensions()
 *     returns GLOBUS_SUCCESS, the caller must free this hash table and its
 *     values by calling globus_gram_protocol_hash_destroy().
 *
 * @return
 *     Upon success,
 *     globus_gram_protocol_unpack_status_reply_with_extensions() returns
 *     @a GLOBUS_SUCCESS and modifies the  @a extensions
 *     parameter to point to the value described
 *     above. If an error occurs, an integer error code is returned and the
 *     value pointed to by @a extensions is undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED
 *     Unpack failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH
 *     Version mismatch
 */
int
globus_gram_protocol_unpack_status_reply_with_extensions(
    const globus_byte_t *               reply,
    globus_size_t                       replysize,
    globus_hashtable_t *                extensions)
{
    int                                 rc;
    globus_gram_protocol_extension_t * entry = NULL;

    if (reply == NULL || extensions == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto bad_param;
    }

    rc = globus_gram_protocol_unpack_message(
            (const char *) reply,
            replysize,
            extensions);

    if (rc != GLOBUS_SUCCESS)
    {
        goto parse_error;
    }
    
    /* Check that required attributes are present */
    entry = globus_hashtable_lookup(
            extensions,
            "protocol-version");
    if (entry == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

        goto verify_error;
    }
    if (strtol(entry->value, NULL, 10) != GLOBUS_GRAM_PROTOCOL_VERSION)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH;

        goto verify_error;
    }

    entry = globus_hashtable_lookup(
            extensions,
            "status");
    if (entry == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
        
        goto verify_error;
    }

    entry = globus_hashtable_lookup(
            extensions,
            "failure-code");
    if (entry == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

        goto verify_error;
    }

    entry = globus_hashtable_lookup(
            extensions,
            "job-failure-code");
    if (entry == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

        goto verify_error;
    }

    rc = GLOBUS_SUCCESS;

    if (rc != GLOBUS_SUCCESS)
    {
verify_error:
        entry = NULL;
        globus_gram_protocol_hash_destroy(extensions);
    }
parse_error:
bad_param:

    return rc;
}
/* globus_gram_protocol_unpack_status_reply_with_extensions() */

/**
 * @brief Pack a GRAM status update message
 * @ingroup globus_gram_protocol_pack
 *
 * @details
 * The globus_gram_protocol_pack_status_update_message()
 * function combines its parameters into a GRAM status update message body. The
 * caller may frame and send the resulting message by calling
 * globus_gram_protocol_post() or just frame it by calling
 * globus_gram_protocol_frame_request() and send it by some other mechanism.
 * The globus_gram_protocol_pack_status_update_message()
 * function returns the packed message by modifying the @a reply parameter to
 * point to a new string containing the message. The caller is responsible for
 * freeing that string.
 *
 * @param job_contact
 *     The job contact string associated with the job.
 * @param status
 *     The job's current @ref globus_gram_protocol_job_state_t "job state".
 * @param failure_code
 *     The error associated with this job request if the @a status
 *     value is GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED.
 * @param reply
 *     An output parameter which will be set to a new
 *     string containing the packed status message. The caller
 *     must free this memory by calling free()
 * @param replysize
 *     An output parameter which will be set to the length
 *     of the status message returned in @a reply.
 *
 * @return
 *     Upon success,
 *     globus_gram_protocol_pack_status_update_message() returns
 *     @a GLOBUS_SUCCESS and modifies the  @a reply and @a replysize
 *     parameters as described above.
 *     If an error occurs, an integer error code is returned and the
 *     values pointed to by @a reply and @a replysize are undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 */
int
globus_gram_protocol_pack_status_update_message(
    char *                              job_contact,
    int                                 status,
    int                                 failure_code,
    globus_byte_t **                    reply,
    globus_size_t *                     replysize)
{
    *reply = globus_libc_malloc(
        strlen(GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE) +
        strlen(GLOBUS_GRAM_HTTP_PACK_JOB_MANAGER_URL_LINE) +
        strlen(GLOBUS_GRAM_HTTP_PACK_STATUS_LINE) +
        strlen(GLOBUS_GRAM_HTTP_PACK_FAILURE_CODE_LINE) +
        strlen(job_contact) + 5 );
    if(*reply == GLOBUS_NULL)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }

    globus_libc_sprintf( (char *) *reply,
                         GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE
                         GLOBUS_GRAM_HTTP_PACK_JOB_MANAGER_URL_LINE
                         GLOBUS_GRAM_HTTP_PACK_STATUS_LINE
                         GLOBUS_GRAM_HTTP_PACK_FAILURE_CODE_LINE,
                         GLOBUS_GRAM_PROTOCOL_VERSION,
                         job_contact,
                         status,
                         failure_code );

    *replysize = (globus_size_t)(strlen((char *)*reply) + 1);

    return GLOBUS_SUCCESS;
}
/* globus_gram_protocol_pack_status_update_message() */

/**
 * @brief Pack a GRAM status update message with extensions
 * @ingroup globus_gram_protocol_pack
 *
 * @details
 * The globus_gram_protocol_pack_status_update_message_with_extensions()
 * function combines its parameters into a GRAM status update message body. The
 * caller may frame and send the resulting message by calling
 * globus_gram_protocol_post() or just frame it by calling
 * globus_gram_protocol_frame_request() and send it by some other mechanism.
 * The globus_gram_protocol_pack_status_update_message_with_extensions()
 * function returns the packed message by modifying the @a reply parameter to
 * point to a new string containing the message. The caller is responsible for
 * freeing that string. 
 *
 * @param job_contact
 *     The job contact string associated with the job.
 * @param status
 *     The job's current @ref globus_gram_protocol_job_state_t "job state".
 * @param failure_code
 *     The error associated with this job request if the @a status
 *     value is GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED.
 * @param extensions
 *     A pointer to a hash table keyed by extension attribute names with the
 *     values being pointers to globus_gram_protocol_extension_t structures.
 * @param reply
 *     An output parameter which will be set to a new
 *     string containing the packed status message. The caller
 *     must free this memory by calling free()
 * @param replysize
 *     An output parameter which will be set to the length
 *     of the status message returned in @a reply.
 *
 * @return
 *     Upon success,
 *     globus_gram_protocol_pack_status_update_message_with_extensions()
 *     returns @a GLOBUS_SUCCESS and modifies the  @a reply and @a replysize
 *     parameters as described above.
 *     If an error occurs, an integer error code is returned and the
 *     values pointed to by @a reply and @a replysize are undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 */
int
globus_gram_protocol_pack_status_update_message_with_extensions(
    char *                              job_contact,
    int                                 status,
    int                                 failure_code,
    globus_hashtable_t *                extensions,
    globus_byte_t **                    reply,
    globus_size_t *                     replysize)
{
    globus_gram_protocol_extension_t * entry;
    size_t                              len = 0;
    size_t                              chrs = 0;
    char *                              tmp;
    int                                 rc = GLOBUS_SUCCESS;

    if (job_contact == NULL || extensions == NULL ||
        reply == NULL || replysize == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto null_param;
    }

    for (entry = globus_hashtable_first(extensions);
         entry != NULL;
         entry = globus_hashtable_next(extensions))
    {
        if (entry->attribute == NULL || entry->value == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_PACK_FAILED;

            goto bad_attr;
        }
        len += strlen(entry->attribute) + (2*strlen(entry->value)) + 4;
    }

    tmp = globus_common_create_string(
                 GLOBUS_GRAM_HTTP_PACK_PROTOCOL_VERSION_LINE
                 GLOBUS_GRAM_HTTP_PACK_JOB_MANAGER_URL_LINE
                 GLOBUS_GRAM_HTTP_PACK_STATUS_LINE
                 GLOBUS_GRAM_HTTP_PACK_FAILURE_CODE_LINE,
                 GLOBUS_GRAM_PROTOCOL_VERSION,
                 job_contact,
                 status,
                 failure_code);
    if (tmp == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto tmp_malloc_failed;
    }

    chrs = strlen(tmp);
    len += chrs + 1;
    *reply = (globus_byte_t *) tmp;
    tmp = realloc(tmp, len);
    if (tmp == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        free(*reply);
        *reply = NULL;

        goto reply_realloc_failed;
    }
    *reply = (globus_byte_t *) tmp;

    for (entry = globus_hashtable_first(extensions);
         entry != NULL;
         entry = globus_hashtable_next(extensions))
    {
        chrs += sprintf(((char *) *reply) + chrs,
                "%s: ",
                entry->attribute);

        chrs += globus_l_gram_protocol_quote_string(
                entry->value,
                *reply + chrs);

        chrs += sprintf(((char *) *reply) + chrs, "\r\n");
    }

    *replysize = (globus_size_t)(strlen(tmp) + 1);

reply_realloc_failed:
tmp_malloc_failed:
bad_attr:
null_param:

    return rc;
}

/**
 * @brief Unpack a GRAM status update message
 * @ingroup globus_gram_protocol_unpack
 *
 * @details
 * The globus_gram_protocol_unpack_status_update_message()
 * function parses the message packed in the @a reply parameter,
 * storing the standard message attribute values in its return parameters
 * @a job_contact, @a status, and @a failure_code. The caller is responsible
 * for freeing the @a job_contact value.
 *
 * @param reply
 *     The unframed reply message to parse.
 * @param replysize
 *     The length of the reply message.
 * @param job_contact
 *     An output parameter to be set to the job contact string.
 *     If globus_gram_protocol_unpack_status_update_message() returns
 *     GLOBUS_SUCCESS, then the caller must free this string using free().
 * @param status
 *     An output parameter to be set to the integer value of the job's current
 *     @ref globus_gram_protocol_job_state_t "job state".
 * @param failure_code
 *     An output parameter to be set to the integer failure code for
 *     the job if the @a job_status is GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED.
 *
 * @return
 *     Upon success,
 *     globus_gram_protocol_unpack_status_update_message()
 *     returns @a GLOBUS_SUCCESS and modifies the @a job_contact, @a status,
 *     and @a failure_code parameters as described above.
 *     If an error occurs, an integer error code is returned and the
 *     values pointed to by the @a job_contact, @a status, and @a failure_code
 *     parameters are undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Sucess
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *     Null parameter
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED
 *     Unpack failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH
 *     Version mismatch
 */
int
globus_gram_protocol_unpack_status_update_message(
    const globus_byte_t *               reply,
    globus_size_t                       replysize,
    char **                             job_contact,
    int *                               status,
    int *                               failure_code)
{
    int                                 rc = GLOBUS_SUCCESS;
    globus_hashtable_t                  extensions;

    if (reply == NULL || job_contact == NULL || status == NULL ||
        failure_code == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto bad_param;
    }
    *job_contact = NULL;
    *status = 0;
    *failure_code = 0;

    rc = globus_gram_protocol_unpack_status_update_message_with_extensions(
            reply,
            replysize,
            &extensions);

    if (rc != GLOBUS_SUCCESS)
    {
        goto parse_error;
    }

    rc = globus_l_gram_protocol_get_string_attribute(
            &extensions,
            GLOBUS_GRAM_ATTR_JOB_MANAGER_URL,
            job_contact);
    if (rc != GLOBUS_SUCCESS)
    {
        goto job_manager_url_error;
    }

    rc = globus_l_gram_protocol_get_int_attribute(
            &extensions,
            GLOBUS_GRAM_ATTR_STATUS,
            status);
    if (rc != GLOBUS_SUCCESS)
    {
        goto status_error;
    }

    rc = globus_l_gram_protocol_get_int_attribute(
            &extensions,
            GLOBUS_GRAM_ATTR_FAILURE_CODE,
            failure_code);
    if (rc != GLOBUS_SUCCESS)
    {
status_error:
        free(*job_contact);
        *job_contact = NULL;
    }
job_manager_url_error:
    globus_gram_protocol_hash_destroy(&extensions);
parse_error:
bad_param:
    return rc;
}
/* globus_gram_protocol_unpack_status_update_message() */

/**
 * @brief Unpack a GRAM status update message with extensions
 * @ingroup globus_gram_protocol_unpack
 *
 * @details
 * The globus_gram_protocol_unpack_status_update_message_with_extensions()
 * function parses the message packed in the @a reply parameter,
 * storing the message attribute values in its return parameter
 * @a extensions. The caller is responsible for freeing the @a extensions
 * hash table by calling globus_gram_protocol_hash_destroy().
 *
 * @param reply
 *     The unframed reply message to parse.
 * @param replysize
 *     The length of the reply message.
 * @param extensions
 *     An output parameter which will be initialized to a hashtable
 *     containing the message attributes. The caller must destroy this
 *     hashtable calling globus_gram_protocol_hash_destroy().
 *
 * @return
 *     Upon success,
 *     globus_gram_protocol_unpack_status_update_message_with_extensions()
 *     returns @a GLOBUS_SUCCESS and modifies the @a extensions
 *     parameter as described above.
 *     If an error occurs, an integer error code is returned and the
 *     value pointed to by the @a extensions parameters is undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Sucess
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED
 *     Unpack failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH
 *     Version mismatch
 */
int
globus_gram_protocol_unpack_status_update_message_with_extensions(
    const globus_byte_t *               reply,
    globus_size_t                       replysize,
    globus_hashtable_t *                extensions)
{
    int                                 protocol_version;
    int                                 rc = GLOBUS_SUCCESS;
    globus_gram_protocol_extension_t *  entry;
    int                                 failure_code;
    char *                              failure_type = NULL;
    char *                              failure_message = NULL;
    char *                              failure_source = NULL;
    char *                              failure_destination = NULL;
    char *                              extended_error = NULL;

    if (reply == NULL || extensions == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto bad_param;
    }

    rc = globus_gram_protocol_unpack_message(
            (const char *) reply,
            replysize,
            extensions);
    if (rc != GLOBUS_SUCCESS)
    {
        goto parse_error;
    }

    rc = globus_l_gram_protocol_get_int_attribute(
            extensions,
            GLOBUS_GRAM_ATTR_PROTOCOL_VERSION,
            &protocol_version);
    if (rc != GLOBUS_SUCCESS)
    {
        goto version_missing_error;
    }

    if (protocol_version != GLOBUS_GRAM_PROTOCOL_VERSION)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_VERSION_MISMATCH;

        goto version_error;
    }

    entry = globus_hashtable_lookup(
            extensions,
            GLOBUS_GRAM_ATTR_JOB_MANAGER_URL);
    if (entry == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

        goto job_manager_url_error;
    }

    entry = globus_hashtable_lookup(
            extensions,
            GLOBUS_GRAM_ATTR_STATUS);
    if (entry == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

        goto status_error;
    }

    rc = globus_l_gram_protocol_get_int_attribute(
            extensions,
            GLOBUS_GRAM_ATTR_FAILURE_CODE,
            &failure_code);
    if (rc != GLOBUS_SUCCESS)
    {
        goto failure_code_error;
    }

    rc = globus_l_gram_protocol_get_string_attribute(
            extensions,
            "gt3-failure-type",
            &failure_type);
    if (rc != GLOBUS_SUCCESS)
    {
        if (rc != GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED)
        {
            goto failure_type_error;
        }
        rc = GLOBUS_SUCCESS;
    }

    rc = globus_l_gram_protocol_get_string_attribute(
            extensions,
            "gt3-failure-message",
            &failure_message);
    if (rc != GLOBUS_SUCCESS)
    {
        if (rc != GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED)
        {
            goto failure_message_error;
        }
        rc = GLOBUS_SUCCESS;
    }

    rc = globus_l_gram_protocol_get_string_attribute(
            extensions,
            "gt3-failure-source",
            &failure_source);
    if (rc != GLOBUS_SUCCESS)
    {
        if (rc != GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED)
        {
            goto failure_source_error;
        }
        rc = GLOBUS_SUCCESS;
    }

    rc = globus_l_gram_protocol_get_string_attribute(
            extensions,
            "gt3-failure-destination",
            &failure_destination);

    if (rc != GLOBUS_SUCCESS)
    {
        if (rc != GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED)
        {
            goto failure_destination_error;
        }
        rc = GLOBUS_SUCCESS;
    }

    if (failure_code == GLOBUS_GRAM_PROTOCOL_ERROR_STAGE_IN_FAILED ||
        failure_code == GLOBUS_GRAM_PROTOCOL_ERROR_STAGING_EXECUTABLE ||
        failure_code == GLOBUS_GRAM_PROTOCOL_ERROR_STAGING_STDIN)
    {
        if (failure_type && 
                (strcmp(failure_type, "executable") == 0 ||
                 strcmp(failure_type, "stdin") == 0))
        {
            extended_error = globus_common_create_string(
                    "the job manager could not stage in %s "
                    "from %s",
                    failure_type,
                    failure_source ? failure_source : "UNKNOWN");
        }
        else
        {
            extended_error = globus_common_create_string(
                    "the job manager could not stage in a file "
                    "from %s to %s%s%s",
                    failure_source ? failure_source : "UNKNOWN",
                    failure_destination ? failure_destination : "UNKNOWN",
                    (failure_message && *failure_message) ? ": " : "",
                    (failure_message && *failure_message)
                        ? failure_message : "");
        }
        if (extended_error)
        {
            globus_i_gram_protocol_error_hack_replace_message(
                    failure_code,
                    extended_error);
            free(extended_error);
        }
    }

    if (failure_destination)
    {
        free(failure_destination);
    }
failure_destination_error:
    if (failure_source)
    {
        free(failure_source);
    }
failure_source_error:
    if (failure_message)
    {
        free(failure_message);
    }
failure_message_error:
    if (failure_type)
    {
        free(failure_type);
    }
failure_type_error:
failure_code_error:
status_error:
job_manager_url_error:
version_error:
version_missing_error:
    if (rc != GLOBUS_SUCCESS)
    {
        globus_gram_protocol_hash_destroy(extensions);
        *extensions = NULL;
    }
parse_error:
bad_param:
    return rc;
}
/* globus_gram_protocol_unpack_status_update_message_with_extensions() */

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
static
void
globus_l_gram_protocol_extension_destroy(
    void *                              datum)
{
    globus_gram_protocol_extension_t * entry = datum;

    if (entry)
    {
        if (entry->attribute)
        {
            free(entry->attribute);
        }
        if (entry->value)
        {
            free(entry->value);
        }
        free(entry);
    }
}
#endif

/**
 * Destroy message attribute hash
 * @ingroup globus_gram_protocol_unpack
 * 
 * @param message_hash
 *     Hashtable of globus_gram_protocol_extension_t * values to destroy
 */
void
globus_gram_protocol_hash_destroy(
    globus_hashtable_t *                message_hash)
{
    if (message_hash != NULL)
    {
        globus_hashtable_destroy_all(
                message_hash,
                globus_l_gram_protocol_extension_destroy);
        *message_hash = NULL;
    }
}
/* globus_gram_protocol_hash_destroy() */


/**
 * Create a GRAM5 protocol extension entry
 * @ingroup globus_gram_protocol_extensions
 *
 * Allocates a new GRAM5 protocol extension entry containing an attribute-value
 * pair. The @a attribute parameter is copied into the extension, and the
 * @a format parameter is a printf-style format string used to construct the
 * value of the extension.
 * 
 * The caller is responsible for freeing the extension when done with it. The
 * quoting rules described in @ref globus_gram_protocol must be implemented
 * by the caller in the format string.
 * 
 * @param attribute
 *     Name of the extension attribute
 * @param format
 *     Printf-style format string used along with the varargs to construct
 *     the extension's value string.
 *
 * @retval
 *     A new GRAM5 extension structure, or NULL if a malloc error occurred.
 */
globus_gram_protocol_extension_t *
globus_gram_protocol_create_extension(
    const char *                        attribute,
    const char *                        format,
    ...)
{
    globus_gram_protocol_extension_t *  extension;
    va_list                             ap;
    size_t                              vlen;

    if (attribute == NULL || format == NULL)
    {
        extension = NULL;

        goto bad_param;
    }
    extension = malloc(sizeof(globus_gram_protocol_extension_t));
    if (extension == NULL)
    {
        goto malloc_extension_failed;
    }
    extension->attribute = strdup(attribute);
    if (extension->attribute == NULL)
    {
        goto malloc_attribute_failed;
    }

    va_start(ap, format);
    vlen = vsnprintf(NULL, 0, format, ap);
    va_end(ap);

    extension->value = malloc(vlen + 1);
    if (extension->value == NULL)
    {
        goto malloc_value_failed;
    }

    va_start(ap, format);
    vsnprintf(extension->value, vlen + 1, format, ap);
    va_end(ap);

    return extension;

malloc_value_failed:
    free(extension->attribute);
malloc_attribute_failed:
    free(extension);
    extension = NULL;
malloc_extension_failed:
bad_param:
    return extension;
}
/* globus_gram_protocol_create_extension() */

int
globus_gram_protocol_unpack_message(
    const char *                        message,
    size_t                              message_length,
    globus_hashtable_t *                message_attributes)
{
    globus_gram_protocol_extension_t *  extension;
    const char                          *attr_start, *value_start;
    size_t                              attr_len, value_len;
    const char *                        p;
    char *                              q;
    const char *                        message_end;
    int                                 rc = GLOBUS_SUCCESS;
    int                                 i;

    if (message == NULL || message_attributes == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto bad_param;
    }
    message_end = message + message_length;

    rc = globus_hashtable_init(
            message_attributes,
            17,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);
    if (rc != GLOBUS_SUCCESS)
    {
        goto hashtable_init_failed;
    }
    p = message;

    while (p < message_end && *p != 0)
    {
        attr_start = p;
        /* Pull out attribute start and length */
        while (p < message_end && *p != ':' && *p != '\0')
        {
            p++;
        }

        if (p < message_end && *p != ':')
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

            goto parse_error;
        }
        if (p >= message_end)
        {
            break;
        }

        attr_len = p - attr_start;
        p++;

        if ((p < message_end && *p != ' ') ||
            p >= message_end)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

            goto parse_error;
        }
        p++;

        if (p < message_end && *p == '"')
        {
            globus_bool_t               escaped = GLOBUS_FALSE;

            p++;

            value_start = p;
            while (*p != 0)
            {
                if (escaped)
                {
                    escaped = GLOBUS_FALSE;
                }
                else if (*p == '"')
                {
                    break;
                }
                else if (*p == '\\')
                {
                    escaped = GLOBUS_TRUE;
                }
                p++;
            }
            value_len = p - value_start;
            p++;
        }
        else
        {
            value_start = p;
            while (p < message_end && *p != '\r' && *p != 0)
            {
                p++;
            }
            value_len = p - value_start;
        }
        if ( p < message_end && *(p++) != '\r')
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

            goto parse_error;
        }
        if (p < message_end && *(p++) != '\n')
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

            goto parse_error;
        }
        extension = malloc(sizeof(globus_gram_protocol_extension_t));

        extension->attribute = malloc(attr_len+1);
        sprintf(extension->attribute, "%.*s", (int) attr_len, attr_start);
        q = extension->value = malloc(value_len+1);

        for (i = 0; i < value_len; i++)
        {
            if (value_start[i] != '\\')
            {
                *(q++) = value_start[i];
            }
            else
            {
                *(q++) = value_start[++i];
            }
        }
        *q = '\0';

        globus_hashtable_insert(
                message_attributes,
                extension->attribute,
                extension);
    }
parse_error:
    if (rc != GLOBUS_SUCCESS)
    {
        globus_gram_protocol_hash_destroy(message_attributes);
    }
hashtable_init_failed:
bad_param:
    return rc;
}
/* globus_gram_protocol_unpack_message() */

/**
 * @brief Pack a GRAM version request message
 * @ingroup globus_gram_protocol_pack
 * 
 * @details
 * The globus_gram_protocol_pack_job_request() function creates a copy
 * of the GRAM version request.  The caller may frame
 * and send the resulting message by calling globus_gram_protocol_post()
 * or just frame it by calling globus_gram_protocol_frame_request() and send
 * it by some other mechanism. The globus_gram_protocol_pack_version_request()
 * function returns the packed message by modifying the @a request parameter to
 * point to a new string containing the message. The caller is responsible for
 * freeing that string.
 *
 * @param request
 *     An output parameter which will be set to a new
 *     string containing the packed version request message. The caller
 *     must free this memory by calling free().
 * @param requestsize
 *     An output parameter which will be populated with the length
 *     of the version request message returned in @a query.
 *
 * @return
 *     Upon success, globus_gram_protocol_pack_job_request() returns
 *     GLOBUS_SUCCESS and modifies the  @a request and @a requestsize
 *     parameters to point to the values described above. If an error occurs,
 *     globus_gram_protocol_pack_version_request() returns an integer
 *     error code and the values pointed to by @a request and @a requestsize
 *     are undefined.
 * 
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *     Null parameter
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 */
int
globus_gram_protocol_pack_version_request(
    char **                             request,
    size_t *                            requestsize)
{
    int                                 rc = GLOBUS_SUCCESS;

    if (request == NULL || requestsize == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto bad_param;
    }
    *request = globus_common_create_string("command: version\r\n");
    if (*request == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto malloc_failed;
    }
    *requestsize = strlen(*request) + 1;

malloc_failed:
bad_param:
    return rc;
}
/* globus_gram_protocol_pack_version_request() */


#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/* assumes bufp has sufficient memory */
static
globus_size_t
globus_l_gram_protocol_quote_string(
    const char *                        in,
    globus_byte_t *                     bufp)
{
    char *                              out = (char *) bufp;

    *out++='"';                 /* Start the quoted string */
    while (*in)
    {
        if (*in == '"' || *in == '\\')   /* need escaping */
            *out++ = '\\';
        *out++ = *in++;
    }
    *out++ = '"';               /* End the quoted string. */
    *out   = '\0';

    return (globus_size_t)(out - (char *)bufp);
}
/* globus_l_gram_protocol_quote_string() */


/*
 *
 * TODO: Add unquoting for the % HEX HEX mechanism.
 * assumes enough mem alloc'd
 */
static
int
globus_l_gram_protocol_unquote_string(
    const globus_byte_t *               inbuf,
    globus_size_t                       insize,
    char *                              out)
{
    globus_bool_t  in_quote = GLOBUS_FALSE;
    globus_bool_t  done     = GLOBUS_FALSE;
    char *         in       = (char *) inbuf;

    if (*in == '"')
    {
        in_quote = GLOBUS_TRUE;
        ++in;
    }
    while (!done && ((globus_size_t)(in - (char *)inbuf) < insize))
    {
        if (!*in)
        {
            done = GLOBUS_TRUE;
            continue;
        }
        if (in_quote)
        {
            if (*in == '"')  /* done */
            {
                ++in;
                in_quote = GLOBUS_FALSE;
                done = GLOBUS_TRUE;
                continue;
            }
            else if (*in == '\\')   /* escaped characeter, do next instead */
                *out++ = *(++in);
            else
                *out++ = *in;
        }
        else   /* no quote */
        {
            if (*in == '\r')    /* end of the line. */
            {
                if (*(++in) != '\n')
                {
                    /* Malformed line */
                    return GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;
                }
            }
            /* TODO: Recognize % HEX HEX here. */
            *out++ = *in;
        }
        ++in;
    }   /* while */

    if (in_quote)
        return GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

    *out  = '\0';
    return GLOBUS_SUCCESS;
}
/* globus_l_gram_protocol_unquote_string() */

static
int
globus_l_gram_protocol_get_int_attribute(
    globus_hashtable_t *                extensions,
    const char *                        attribute_name,
    int *                               value)
{
    globus_gram_protocol_extension_t *  extension;
    int                                 rc = GLOBUS_SUCCESS;

    extension = globus_hashtable_lookup(extensions, (void *) attribute_name);
    if (extension == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

        goto unpack_failed;
    }

    *value = atoi(extension->value);

unpack_failed:
    return rc;
}
/* globus_l_gram_protocol_get_int_attribute() */

static
int
globus_l_gram_protocol_get_string_attribute(
    globus_hashtable_t *                extensions,
    const char *                        attribute_name,
    char **                             value)
{
    globus_gram_protocol_extension_t *  extension;
    int                                 rc = GLOBUS_SUCCESS;

    extension = globus_hashtable_lookup(extensions, (void *) attribute_name);
    if (extension == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED;

        goto unpack_failed;
    }

    *value = strdup(extension->value);

    if (*value == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }

unpack_failed:
    return rc;
}
/* globus_l_gram_protocol_get_string_attribute() */

#endif
