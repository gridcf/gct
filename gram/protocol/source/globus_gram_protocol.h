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

#if !defined(GLOBUS_GRAM_PROTOCOL_H)
#define GLOBUS_GRAM_PROTOCOL_H

/**
 * @file globus_gram_protocol.h
 * @brief GRAM Protocol API
 */

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus GRAM Protocol
 * @copydoc globus_gram_protocol
 */
#endif

/**
 * @defgroup globus_gram_protocol GRAM Protocol
 * @brief GRAM Protocol
 *
 * @details
 * The Globus GRAM Protocol Library implements the GRAM protocol. It is used
 * by the GRAM Client and GRAM Job Manager. It provides the constants used
 * by in the sending and receiving of GRAM messages. It also provides
 * functions to encode GRAM requests and replies, and to send and receive
 * the GRAM queries.
 *
 * - @ref globus_gram_protocol_functions "GRAM Protocol Functions"
 * - @ref globus_gram_protocol_job_state_t "Job States"
 * - @ref globus_gram_protocol_job_signal_t "Signals"
 * - @ref globus_gram_protocol_error_t "GRAM Errors"
 * - @ref globus_gram_protocol "GRAM Protocol Message Format"
 */

#include "globus_io.h"
#include "globus_gram_protocol_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { GLOBUS_GRAM_PROTOCOL_VERSION = 2 };
enum { GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE = 64000 };
enum { GLOBUS_GRAM_PROTOCOL_PARAM_SIZE = 1024 };
/**
 * @typedef globus_gram_protocol_handle_t
 * @brief Unique GRAM protocol identifier
 * @ingroup globus_gram_protocol_io
 *
 * @details
 * The @a #globus_gram_protocol_handle_t data type is used by functions in
 * the GRAM protocol API as a unique discriminant between instances
 * of a callback invocation.
 *
 * There are no public functions that operate on these handles. They are
 * used as identifiers for callback functions.
 */
typedef unsigned long globus_gram_protocol_handle_t;

/**
 * @typedef globus_gram_protocol_extension_t
 * @brief GRAM protocol extension attribute-value pair
 * @ingroup globus_gram_protocol_io
 *
 * @details
 * The @a globus_gram_protocol_extension_t data type contains an attribute
 * value pair that represents an extension to the GRAM2 protocol.
 */
typedef struct globus_gram_protocol_hash_entry_s
{
    /** Name of the extension attribute */
    char *                              attribute;
    /** Value of the extension attribute */
    char *                              value;
}
globus_gram_protocol_extension_t;

typedef void (*globus_gram_protocol_callback_t)(
    void  *				arg,
    globus_gram_protocol_handle_t	handle,
    globus_byte_t *			message,
    globus_size_t			msgsize,
    int					errorcode,
    char *				uri);

typedef void (*globus_gram_protocol_delegation_callback_t)(
    void  *				arg,
    globus_gram_protocol_handle_t	handle,
    gss_cred_id_t			credential,
    int					errorcode);
#define GLOBUS_GRAM_PROTOCOL_MODULE	(&globus_i_gram_protocol_module)

extern globus_module_descriptor_t	globus_i_gram_protocol_module;

extern gss_cred_id_t			globus_i_gram_protocol_credential;

/*
 * creates a default set of TCP attributes (authentication with self, SSL
 * wrappers around messages)
 */
int
globus_gram_protocol_setup_attr(
    globus_io_attr_t *			attr);


/*
 * authorizes the remote party if the remote party is the same as the
 * local party
 */

globus_bool_t
globus_gram_protocol_authorize_self(
    gss_ctx_id_t                        context);


/*
 * replaces all credentials used in this module with the given ones
 */
int
globus_gram_protocol_set_credentials(gss_cred_id_t new_credentials);


/* 
 * sets up and registers a listener. returns port and host. user_ptr
 * must contain the read callback to be used. 
 */
int
globus_gram_protocol_allow_attach(
    char **				url,
    globus_gram_protocol_callback_t	callback,
    void *				callback_arg);

/*
 * kills the listener at the specified URL.
 */
int
globus_gram_protocol_callback_disallow(
    char *				url);


/* Frame and send a GRAM protocol message. */
int
globus_gram_protocol_post(
    const char *			url,
    globus_gram_protocol_handle_t *	handle,
    globus_io_attr_t *			attr,
    globus_byte_t *			message,
    globus_size_t			message_size,
    globus_gram_protocol_callback_t	callback,
    void *				callback_arg);

/* Frame and send a GRAM protocol message, following up an ok reply with
 * a GSSAPI delegation handshake.
 */
int
globus_gram_protocol_post_delegation(
    const char *                        url,
    globus_gram_protocol_handle_t *     handle,
    globus_io_attr_t *                  attr,
    globus_byte_t *                     message,
    globus_size_t                       message_size,
    gss_cred_id_t			cred_handle,
    gss_OID_set				restriction_oids,
    gss_buffer_set_t			restriction_buffers,
    OM_uint32				req_flags,
    OM_uint32				time_req,
    globus_gram_protocol_callback_t     callback,
    void *                              callback_arg);

/* Frame and send a GRAM protocol reply. */
int
globus_gram_protocol_reply(
    globus_gram_protocol_handle_t 	handle,
    int					code,
    globus_byte_t *			message,
    globus_size_t			message_size);

/* Frame and send a GRAM protocol reply indicating that we will 
 * accept a delegated credential now. Call back when the delegation
 * is completed.
 *
 * After delegation is complete, the user must call
 * globus_gram_protocol_reply to indicate the status after the delegation.
 */
int
globus_gram_protocol_accept_delegation(
    globus_gram_protocol_handle_t       handle,
    gss_OID_set				restriction_oids,
    gss_buffer_set_t			restriction_bufers,
    OM_uint32				req_flags,
    OM_uint32				time_req,
    globus_gram_protocol_delegation_callback_t
    					callback,
    void *				arg);
/* Frame a GRAM protocol message */
int
globus_gram_protocol_frame_request(
    const char *			url,
    const globus_byte_t *		msg,
    globus_size_t			msgsize,
    globus_byte_t **			framedmsg,
    globus_size_t *			framedsize);

/* Frame a GRAM protocol reply */
int
globus_gram_protocol_frame_reply(
    int					code,
    const globus_byte_t *		msg,
    globus_size_t			msgsize,
    globus_byte_t **			framedmsg,
    globus_size_t *			framedsize);

/************************ "HTTP" pack/unpack functions *********************/

int
globus_gram_protocol_pack_job_request(
    int					job_state_mask,
    const char *			callback_url,
    const char *			rsl,
    globus_byte_t **			query,
    globus_size_t *			querysize);


int
globus_gram_protocol_unpack_job_request(
    const globus_byte_t *		query,
    globus_size_t			querysize,
    int  *				job_state_mask,
    char **				callback_url,
    char **				description);


int
globus_gram_protocol_pack_job_request_reply(
    int					status,
    const char *			job_contact,    /* may be null */
    globus_byte_t **			reply,
    globus_size_t *			replysize);


int
globus_gram_protocol_unpack_job_request_reply(
    const globus_byte_t *		reply,
    globus_size_t			replysize,
    int *				status,
    char **				job_contact);

int
globus_gram_protocol_pack_job_request_reply_with_extensions(
    int					status,
    const char *			job_contact,    /* may be null */
    globus_hashtable_t *                extensions,
    globus_byte_t **			reply,
    globus_size_t *			replysize);


int
globus_gram_protocol_unpack_job_request_reply_with_extensions(
    const globus_byte_t *		reply,
    globus_size_t			replysize,
    int *				status,
    char **				job_contact,
    globus_hashtable_t *                extensions);

int
globus_gram_protocol_pack_status_request(
    const char *			status_request,
    globus_byte_t **			query,
    globus_size_t *			querysize);


int
globus_gram_protocol_unpack_status_request(
    const globus_byte_t *		query,
    globus_size_t			querysize,
    char **				status_requst);


int
globus_gram_protocol_pack_status_reply(
    int					job_status,
    int					failure_code,
    int					job_failure_code,
    globus_byte_t **			reply,
    globus_size_t *			replysize);

int
globus_gram_protocol_pack_status_reply_with_extensions(
    int					job_status,
    int					failure_code,
    int					job_failure_code,
    globus_hashtable_t *                extensions,
    globus_byte_t **			reply,
    globus_size_t *			replysize);

int
globus_gram_protocol_unpack_status_reply(
    const globus_byte_t *		reply,
    globus_size_t			replysize,
    int *				job_status,
    int *				failure_code,
    int *				job_failure_code);

int
globus_gram_protocol_unpack_status_reply_with_extensions(
    const globus_byte_t *               reply,
    globus_size_t                       replysize,
    globus_hashtable_t *                extensions);

int
globus_gram_protocol_pack_status_update_message(   
    char *				job_contact,
    int					status,            
    int					failure_code,
    globus_byte_t **			reply,
    globus_size_t *			replysize);


int
globus_gram_protocol_unpack_status_update_message(
    const globus_byte_t *		reply,
    globus_size_t			replysize,
    char **				job_contact,
    int *				status,
    int *				failure_code);

int
globus_gram_protocol_pack_status_update_message_with_extensions(
    char *                              job_contact,
    int                                 status,
    int                                 failure_code,
    globus_hashtable_t *                extensions,
    globus_byte_t **                    reply,
    globus_size_t *                     replysize);

int
globus_gram_protocol_unpack_status_update_message_with_extensions(
    const globus_byte_t *		reply,
    globus_size_t			replysize,
    globus_hashtable_t *                message_hash);

int
globus_gram_protocol_unpack_message(
    const char *                        message,
    size_t                              message_length,
    globus_hashtable_t *                message_attributes);

int
globus_gram_protocol_pack_version_request(
    char **                             request,
    size_t *                            requestsize);

void
globus_gram_protocol_hash_destroy(
    globus_hashtable_t *                message_hash);

globus_gram_protocol_extension_t *
globus_gram_protocol_create_extension(
    const char *                        attribute,
    const char *                        format,
    ...);

int
globus_gram_protocol_get_sec_context(
    globus_gram_protocol_handle_t       handle,
    gss_ctx_id_t *                      context);

const char *
globus_gram_protocol_error_string(int error_code);

/* To be used only by the GRAM client API */
void
globus_gram_protocol_error_7_hack_replace_message(const char * message);
void
globus_gram_protocol_error_10_hack_replace_message(const char * message);

/*
 * RSL Parameters
 */
#define GLOBUS_GRAM_PROTOCOL_EXECUTABLE_PARAM               "executable"
#define GLOBUS_GRAM_PROTOCOL_ARGUMENTS_PARAM                "arguments"
#define GLOBUS_GRAM_PROTOCOL_ENVIRONMENT_PARAM              "environment"
#define GLOBUS_GRAM_PROTOCOL_DIR_PARAM                      "directory"
#define GLOBUS_GRAM_PROTOCOL_COUNT_PARAM                    "count"
#define GLOBUS_GRAM_PROTOCOL_STDIN_PARAM                    "stdin"
#define GLOBUS_GRAM_PROTOCOL_STDOUT_PARAM                   "stdout"
#define GLOBUS_GRAM_PROTOCOL_STDERR_PARAM                   "stderr"
#define GLOBUS_GRAM_PROTOCOL_MAX_WALL_TIME_PARAM            "maxwalltime"
#define GLOBUS_GRAM_PROTOCOL_MAX_CPU_TIME_PARAM             "maxcputime"
#define GLOBUS_GRAM_PROTOCOL_MAX_TIME_PARAM                 "maxtime"
#define GLOBUS_GRAM_PROTOCOL_PARADYN_PARAM                  "paradyn"
#define GLOBUS_GRAM_PROTOCOL_JOB_TYPE_PARAM                 "jobtype"
#define GLOBUS_GRAM_PROTOCOL_MYJOB_PARAM                    "grammyjob"
#define GLOBUS_GRAM_PROTOCOL_QUEUE_PARAM                    "queue"
#define GLOBUS_GRAM_PROTOCOL_PROJECT_PARAM                  "project"
#define GLOBUS_GRAM_PROTOCOL_HOST_COUNT_PARAM               "hostcount"
#define GLOBUS_GRAM_PROTOCOL_DRY_RUN_PARAM                  "dryrun"
#define GLOBUS_GRAM_PROTOCOL_MIN_MEMORY_PARAM               "minmemory"
#define GLOBUS_GRAM_PROTOCOL_MAX_MEMORY_PARAM               "maxmemory"
#define GLOBUS_GRAM_PROTOCOL_START_TIME_PARAM               "starttime"
#define GLOBUS_GRAM_PROTOCOL_RESERVATION_HANDLE_PARAM       "reservationhandle"
#define GLOBUS_GRAM_PROTOCOL_STDOUT_POSITION_PARAM          "stdoutposition"
#define GLOBUS_GRAM_PROTOCOL_STDERR_POSITION_PARAM          "stderrposition"
#define GLOBUS_GRAM_PROTOCOL_SAVE_STATE_PARAM               "savestate"
#define GLOBUS_GRAM_PROTOCOL_RESTART_PARAM                  "restart"
#define GLOBUS_GRAM_PROTOCOL_TWO_PHASE_COMMIT_PARAM         "twophase"
#define GLOBUS_GRAM_PROTOCOL_REMOTE_IO_URL_PARAM            "remoteiourl"
#define GLOBUS_GRAM_PROTOCOL_FILE_STAGE_IN_PARAM            "filestagein"
#define GLOBUS_GRAM_PROTOCOL_FILE_STAGE_IN_SHARED_PARAM     "filestageinshared"
#define GLOBUS_GRAM_PROTOCOL_FILE_STAGE_OUT_PARAM           "filestageout"
#define GLOBUS_GRAM_PROTOCOL_FILE_CLEANUP_PARAM             "filecleanup"
#define GLOBUS_GRAM_PROTOCOL_SCRATCHDIR_PARAM               "scratchdir"
#define GLOBUS_GRAM_PROTOCOL_GASS_CACHE_PARAM               "gasscache"
#define GLOBUS_GRAM_PROTOCOL_PROXY_TIMEOUT_PARAM            "proxytimeout"
#define GLOBUS_GRAM_PROTOCOL_USER_NAME                      "username"

#define GLOBUS_GRAM_PROTOCOL_DEFAULT_STDIN                  "/dev/null"
#define GLOBUS_GRAM_PROTOCOL_DEFAULT_STDOUT                 "/dev/null"
#define GLOBUS_GRAM_PROTOCOL_DEFAULT_STDERR                 "/dev/null"
#define GLOBUS_GRAM_PROTOCOL_DEFAULT_MYJOB                  "collective"
#define GLOBUS_GRAM_PROTOCOL_DEFAULT_JOBTYPE                "multiple"
#define GLOBUS_GRAM_PROTOCOL_DEFAULT_DRYRUN                 "no"
#define GLOBUS_GRAM_PROTOCOL_DEFAULT_START_TIME             "none"

#ifdef __cplusplus
}
#endif

#endif

