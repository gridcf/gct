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
 * @file globus_gass_transfer_request.c
 * @brief GASS Transfer Request
 *
 * This module implements the request structure accessors for the
 * GASS transfer library 
 */
#endif

#include "globus_i_gass_transfer.h"

/**
 * Determine the type of a request.
 * @ingroup globus_gass_transfer_request
 *
 * This function is used by GASS server implementations to discover what
 * type of operation the client is requesting for an URL.
 *
 * @param request
 *        The request to query.
 *
 * @return The @link #globus_gass_transfer_request_type_t type@endlink
 * of the request.
 */
globus_gass_transfer_request_type_t
globus_gass_transfer_request_get_type(
    globus_gass_transfer_request_t		request)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_REQUEST_TYPE_INVALID;
    }
    else
    {
	return req->type;
    }
}
/* globus_gass_transfer_request_get_type() */

/**
 * Get the user pointer associated with a request
 * @ingroup globus_gass_transfer_request
 *
 * This function extracts the user pointer from a request handle. The
 * user-pointer may be used by the application which is generating or
 * servicing the request to store a pointer to any application-specific
 * piece of data.
 *
 * @param request
 *        The request to query.
 *
 * @return The user pointer's value.
 */
void *
globus_gass_transfer_request_get_user_pointer(
    globus_gass_transfer_request_t		request)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL||
       req->type == GLOBUS_GASS_TRANSFER_REQUEST_TYPE_INVALID)
    {
	return GLOBUS_NULL;
    }
    else
    {
	return req->user_pointer;
    }
}
/* globus_gass_transfer_request_get_user_pointer() */

/**
 * Set the user pointer associated with a request handle.
 * @ingroup globus_gass_transfer_request
 *
 * This function sets the user pointer from a request handle. The
 * user-pointer may be used by the application which is generating or
 * servicing the request to store a pointer to any application-specific
 * piece of data.
 *
 * @param request
 *        The request to modify.
 * @param user_pointer
 *        The new value of the user pointer for the request.
 *
 * @retval GLOBUS_SUCCES
 *         The user pointer's value was set.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE
 *         An invalid request handle was passed to this function
 */
int
globus_gass_transfer_request_set_user_pointer(
    globus_gass_transfer_request_t		request,
    void *					user_pointer)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE;
    }
    else
    {
	req->user_pointer = user_pointer;
	return GLOBUS_SUCCESS;
    }
}
/* globus_gass_transfer_request_set_user_pointer() */

/**
 * Check the status of a request.
 * @ingroup globus_gass_transfer_request
 *
 * This function queries a request to determine the status of the request.
 * This function should be called after EOF has been reached, or after
 * the initial get, put, or append has returned or had it's callback function
 * called to determine if it is possible to proceed, or whether the file
 * transfer was successfully processed.
 *
 * @param request
 *        The request handle to query.
 *
 * @return A #globus_gass_transfer_request_status_t indicating
 *         the current status of the request.
 */
globus_gass_transfer_request_status_t
globus_gass_transfer_request_get_status(
    globus_gass_transfer_request_t		request)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req != GLOBUS_NULL)
    {
	switch(req->status)
	{
	  case GLOBUS_GASS_TRANSFER_REQUEST_STARTING3:
	  case GLOBUS_GASS_TRANSFER_REQUEST_PENDING:
	  case GLOBUS_GASS_TRANSFER_REQUEST_ACTING:
	  case GLOBUS_GASS_TRANSFER_REQUEST_ACTING_TO_PENDING:
	  case GLOBUS_GASS_TRANSFER_REQUEST_ACCEPTING:
	    return GLOBUS_GASS_TRANSFER_REQUEST_PENDING;
	  case GLOBUS_GASS_TRANSFER_REQUEST_ACTING_TO_FAILING:
	  case GLOBUS_GASS_TRANSFER_REQUEST_FAILING:
	  case GLOBUS_GASS_TRANSFER_REQUEST_FAILED:
	  case GLOBUS_GASS_TRANSFER_REQUEST_SERVER_FAIL1:
	  case GLOBUS_GASS_TRANSFER_REQUEST_SERVER_FAIL2:
	  case GLOBUS_GASS_TRANSFER_REQUEST_SERVER_FAIL3:
	  case GLOBUS_GASS_TRANSFER_REQUEST_USER_FAIL:
	    return GLOBUS_GASS_TRANSFER_REQUEST_FAILED;
	  case GLOBUS_GASS_TRANSFER_REQUEST_FINISHING:
	  case GLOBUS_GASS_TRANSFER_REQUEST_DONE:
	    return GLOBUS_GASS_TRANSFER_REQUEST_DONE;
	  case GLOBUS_GASS_TRANSFER_REQUEST_STARTING:
	  case GLOBUS_GASS_TRANSFER_REQUEST_STARTING2:
	    return GLOBUS_GASS_TRANSFER_REQUEST_STARTING;
	  case GLOBUS_GASS_TRANSFER_REQUEST_REFERRED:
	  case GLOBUS_GASS_TRANSFER_REQUEST_ACTING_TO_REFERRING:
	  case GLOBUS_GASS_TRANSFER_REQUEST_REFERRING:
	    return GLOBUS_GASS_TRANSFER_REQUEST_REFERRED;
	  case GLOBUS_GASS_TRANSFER_REQUEST_DENIED:
	    return GLOBUS_GASS_TRANSFER_REQUEST_DENIED;
	  case GLOBUS_GASS_TRANSFER_REQUEST_INVALID:
	    return GLOBUS_GASS_TRANSFER_REQUEST_INVALID;
	}
    }
    return GLOBUS_GASS_TRANSFER_REQUEST_INVALID;
}
/* globus_gass_transfer_request_get_status() */

/**
 * Extract referral information from a request handle.
 * @ingroup globus_gass_transfer_request
 *
 * This function queries the request handle to determine any referral
 * information that it contains. This function should only be called
 * on request handles in the GLOBUS_GASS_TRANSFER_REQUEST_REFERRED
 * state. If no referral information is stored in the request handle,
 * then the referral will be initialized to an empty referral.
 * The referral must be destroyed by calling
 * globus_gass_transfer_referral_destroy() by the caller.
 *
 * @param request
 *        The request handle to query.
 * @param referral
 *        A pointer to an uninitialized referral structure. It will be
 *        populated by calling this function.
 *
 * @retval GLOBUS_SUCCESS
 *         The referral was successfully extracted from the request
 *         handle.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         The referral pointer was GLOBUS_NULL;
 */
int
globus_gass_transfer_request_get_referral(
    globus_gass_transfer_request_t		request,
    globus_gass_transfer_referral_t *		referral)
{
    globus_gass_transfer_request_struct_t *	req;

    /* Sanity check of arguments */
    if(referral == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    /* Check for illegal handle */
    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE;
    }
    else if(req->referral_count == 0)
    {
	referral->url = GLOBUS_NULL;
	referral->count = 0;

	return GLOBUS_SUCCESS;
    }
    else
    {
	globus_size_t				i;

	referral->url =
	    globus_malloc(sizeof(char *) * req->referral_count);

	for(i = 0; i < req->referral_count; i++)
	{
	    referral->url[i] = globus_libc_strdup(req->referral_url[i]);
	}
	referral->count = req->referral_count;

	return GLOBUS_SUCCESS;
    }
}
/* globus_gass_transfer_request_get_referral() */

/**
 * Get the URL from a request handle.
 * @ingroup globus_gass_transfer_request
 *
 * This function queries the request handle to determine the URL associated
 * with the request. This function is intended to be useful to GASS server
 * implementors.
 *
 * @param request
 *        The request handle to query.
 *
 * @return A pointer to the URL, or GLOBUS_NULL if the request handle
 *         is invalid. The string which is returned must not be freed by
 *         the caller. It may not be accessed after the request has been
 *         destroyed.
 */
char *
globus_gass_transfer_request_get_url(
    globus_gass_transfer_request_t		request)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL)
    {
	return GLOBUS_NULL;
    }
    else
    {
	return req->url;
    }
}
/* globus_gass_transfer_request_get_url() */

/**
 * Get the length of a file to be transferred using GASS.
 * @ingroup globus_gass_transfer_request
 *
 * This function queries the request handle to determine the amount of
 * data that will be transferred to copy the URL. The length may be
 * @a GLOBUS_GASS_TRANSFER_LENGTH_UNKNOWN if the sender can not determine the length
 * before making or authorizing the request.
 *
 * @param request
 *        The request to query.
 *
 * @return The length of the file located at the request's URL, or
 *         @a GLOBUS_GASS_TRANSFER_LENGTH_UNKNOWN if that cannot be determined.
 */
globus_size_t
globus_gass_transfer_request_get_length(
    globus_gass_transfer_request_t		request)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_LENGTH_UNKNOWN;
    }
    else
    {
	return req->length;
    }
}

/**
 * Set the type of a request.
 * @ingroup globus_gass_transfer_request
 *
 * This function modifies a request handle by setting the type of
 * operation that it is being used for. This function may only be
 * called once per handle, and only from a GASS protocol module
 * implementation.
 *
 * @param request
 *        The request handle to modify.
 * @param type
 *        The type of operation that this request handle will be used for.
 *
 * @retval GLOBUS_SUCCESS
 *         The request handle's type has been set.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE
 *         The request handle was invalid or it's type was already set.
 *         The request handle was not modified.
 *
 * @note Only GASS Protocol modules may call this function.
 */
int
globus_gass_transfer_request_set_type(
    globus_gass_transfer_request_t		request,
    globus_gass_transfer_request_type_t		type)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL||
       req->type != GLOBUS_GASS_TRANSFER_REQUEST_TYPE_INVALID)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE;
    }
    else
    {
	req->type = type;
	return GLOBUS_SUCCESS;
    }
}
/* globus_gass_transfer_request_set_type() */

/**
 * Set the URL to which a request handle refers.
 * @ingroup globus_gass_transfer_request
 *
 * This function modifies the given request handle so that it's URL field
 * is set to string pointed to by @a url.
 *
 * No copy is made of the string, so the caller must not free it. It must
 * be allocated by calling one of the memory allocators in globus_libc, as
 * it will be freed when the request handle is destroyed.
 *
 * This function must only be called by protocol modules when constructing
 * a request handle when accepting a new request. This function can only
 * be called once per request handle.
 *
 * @param request
 *        A handle to the request to modify.
 * @param url
 *        A string containing the URL that this request will be associated
 *        with.
 *
 * @retval GLOBUS_SUCCESS
 *         The URL was set for the request handle.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE
 *         The request handle was invalid, or the URL had already been set.
 */
int
globus_gass_transfer_request_set_url(
    globus_gass_transfer_request_t		request,
    char *					url)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL ||
       req->url != GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE;
    }
    else
    {
	req->url = url;
	return GLOBUS_SUCCESS;
    }
}

/**
 * Set the length of a transfer associated request handle.
 * @ingroup globus_gass_transfer_request
 *
 * This function modifies the given request handle so that it's length field
 * is set to give length parameter.
 *
 * This function must only be called by protocol modules when constructing
 * a request handle when receiving the response to a get request. This
 * function can only be called once per request handle.
 *
 * @param request
 *        A handle to the request to modify.
 * @param length
 *        The length of the file request.
 *
 * @retval GLOBUS_SUCCESS
 *         The URL was set for the request handle.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE
 *         The request handle was invalid, or the URL had already been set.
 */
void
globus_gass_transfer_request_set_length(
    globus_gass_transfer_request_t		request,
    globus_size_t				length)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL)
    {
	return ;
    }
    else
    {
	req->length = length;
    }
}
/* globus_gass_transfer_request_set_length() */

/**
 * Get an integer code describing why the request was denied.
 * @ingroup globus_gass_transfer_request
 *
 * This function queries a request which was denied by a server to
 * determine why it was denied. The denial reason will be expressed
 * in a protocol-specific response code. Knowledge of the protocol
 * is needed to understand this response.
 *
 * @param request
 *        A handle to the request to query.
 *
 * @return A protocol-specific integer indicating why the request
 *         was denied. If the request handle is invalid or the
 *         request was not denied, then this function returns 0.
 * @see globus_gass_transfer_request_get_denial_message()
 */
int
globus_gass_transfer_request_get_denial_reason(
    globus_gass_transfer_request_t 		request)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL)
    {
	return 0;
    }
    else
    {
	return req->denial_reason;
    }
}

/**
 * Get an string describing why a request was denied.
 * @ingroup globus_gass_transfer_request
 *
 * This function queries a request which was denied by a server to
 * determine why it was denied. The denial reason will be expressed
 * as a response string. The string must be freed by the caller.
 *
 * @param request
 *        A handle to the request to query.
 *
 * @return A string indicating why the request
 *         was denied. If the request handle is invalid or the
 *         request was not denied, then this function returns GLOBUS_NULL.
 * @see globus_gass_transfer_request_get_denial_reason()
 */
char *
globus_gass_transfer_request_get_denial_message(
    globus_gass_transfer_request_t		request)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL)
    {
	return GLOBUS_NULL;
    }
    else
    {
	return globus_libc_strdup(req->denial_message);
    }
}

/**
 * Get the subject string associated with a request.
 * @ingroup globus_gass_transfer_request
 *
 * This function queries a request handle to determine the subject
 * identity of the client who initiated the request. 
 * The string must not be freed by the caller.
 *
 * @param request
 *        A handle to the request to query.
 *
 * @return A string containing the request initiator's subject identity.
 *         If the request handle is invalid or a credential was not used
 *         to initiate the request, this value will be GLOBUS_NULL.
 */
char *
globus_gass_transfer_request_get_subject(
    globus_gass_transfer_request_t		request)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL)
    {
	return GLOBUS_NULL;
    }
    else
    {
	return req->subject;
    }
}
/* globus_gass_transfer_request_get_subject() */

/**
 * @ingroup globus_gass_transfer_request
 */
int
globus_gass_transfer_request_set_subject(
    globus_gass_transfer_request_t		request,
    char *					subject)
{
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE;
    }
    else if(req->subject != GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE;
    }
    else
    {
	req->subject = subject;
	return GLOBUS_SUCCESS;
    }
}
/* globus_gass_transfer_request_set_subject() */



/**
 * Destroy a request handle.
 * @ingroup globus_gass_transfer_request
 *
 * This function destroys the caller's reference to a request handle.
 * It must be called for all request handles which are created by calling
 * functions in the "@ref globus_gass_transfer_client" or
 * "@ref globus_gass_transfer_server" sections of this manual.
 * After calling the function, the caller must not attempt to use the
 * request handle for any purpose.
 *
 * @param request
 *        The request to destroy.
 * @retval GLOBUS_SUCCESS
 *        The request handle reference was successfully destroyed.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE
 *        Either an invalid request handle or one which is actively being
 *        used was passed to this function as the @a request parameter.
 */
int
globus_gass_transfer_request_destroy(
    globus_gass_transfer_request_t		request)
{
    globus_gass_transfer_request_struct_t *	req;
    int						rc;

    globus_i_gass_transfer_lock();
    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL)
    {
	rc = GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE;
	goto finish;
    }
    if(req->status != GLOBUS_GASS_TRANSFER_REQUEST_FAILED &&
       req->status != GLOBUS_GASS_TRANSFER_REQUEST_DONE &&
       req->status != GLOBUS_GASS_TRANSFER_REQUEST_FINISHING &&
       req->status != GLOBUS_GASS_TRANSFER_REQUEST_FAILING &&
       req->status != GLOBUS_GASS_TRANSFER_REQUEST_REFERRED &&
       req->status != GLOBUS_GASS_TRANSFER_REQUEST_REFERRING &&
       req->status != GLOBUS_GASS_TRANSFER_REQUEST_ACTING_TO_FAILING &&
       req->status != GLOBUS_GASS_TRANSFER_REQUEST_DENIED)
    {
	rc = GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE;
	goto finish;
    }

    rc =  globus_i_gass_transfer_request_destroy(request);

 finish:
    globus_i_gass_transfer_unlock();
    return rc;
}
/* globus_gass_transfer_request_destroy() */

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * Initialize a gass_transfer request handle.
 *
 * This function creates a #globus_gass_transfer_request_struct_t and
 * associates it with a #gass_transfer_request_t handle. The structure
 * is initialized with the information passed as the arguments to the
 * function.
 *
 * @note This function must be called with the request handle mutex lock.
 *
 * @param request
 *        The request handle to initialize. If this function is successful, 
 *        the value pointed to by this will be initialized to the new
 *        handle id; otherwise, the it will be set to 
 *        GLOBUS_NULL_HANDLE.
 * @param attr
 *        The request attributes to use to create the handle. If non-NULL,
 *        they are copied into the request structure.
 * @param url
 *        An URL string containing the location of the file to access. A
 *        copy of this is stored in the request handle.
 * @param type
 *        The type of file transfer that this request will be used for.
 * @param callback
 *        The callback function to be called once the request is in the
 *        ready state.
 * @param user_arg
 *        User-supplied argument to the callback function.
 *
 * @retval void
 */
void
globus_i_gass_transfer_request_init(
    globus_gass_transfer_request_t *            request,
    globus_gass_transfer_requestattr_t *        attr,
    char *                                      url,
    globus_gass_transfer_request_type_t         type,
    globus_gass_transfer_callback_t             callback,
    void *                                      user_arg)
{
    globus_gass_transfer_request_struct_t *	req;

    req = globus_malloc(sizeof(globus_gass_transfer_request_struct_t));
    if(req == GLOBUS_NULL)
    {
	goto error_exit;
    }

    if(url)
    {
	req->url = globus_libc_strdup(url);
        if(req->url == GLOBUS_NULL)
        {
	    goto free_req;
        }
    }
    else
    {
	req->url = GLOBUS_NULL;
    }
    req->type			= type;
    req->status			= GLOBUS_GASS_TRANSFER_REQUEST_STARTING;
    req->referral_url		= GLOBUS_NULL;
    req->referral_count		= 0;
    req->callback		= callback;
    req->callback_arg		= user_arg;
    req->proto			= GLOBUS_NULL;
    req->subject		= GLOBUS_NULL;
    req->denial_reason		= 0;
    req->denial_message		= GLOBUS_NULL;
    req->handled_length		= 0;
    req->posted_length		= 0;
    req->fail_callback		= GLOBUS_NULL;
    req->client_side		= GLOBUS_FALSE;
    req->user_pointer		= GLOBUS_NULL;

    globus_fifo_init(&req->pending_data);
    if(attr)
    {
	if(*attr)
	{
	    req->attr = globus_object_copy(*attr);
	    if(req->attr == GLOBUS_NULL)
	    {
	        goto free_fifo;
	    }
	}
	else
	{
	    req->attr = GLOBUS_NULL;
	}
    }
    else
    {
	req->attr = GLOBUS_NULL;
    }

    *request = globus_handle_table_insert(&globus_i_gass_transfer_request_handles,
					  (void *) req,
					  2);
    globus_list_insert(&globus_i_gass_transfer_requests,
		       (void *) (intptr_t) (*request));
    
    return;

  free_fifo:
    globus_fifo_destroy(&req->pending_data);
    globus_free(req->url);
  free_req:
    globus_free(req);
  error_exit:
    *request = GLOBUS_NULL_HANDLE;
    return;
}
/* globus_i_gass_transfer_request_init() */

/**
 * Dereference a request handle.
 *
 * This function decreases the reference count on an GASS Transfer
 * request handle. If the reference count becomes zero, then the
 * #globus_gass_transfer_request_struct_t associated with the handle
 * is destroyed.
 *
 * @note This function must be called with the request handle mutex locked.
 *
 * @param request
 *        The request to destroy.
 *
 * @retval GLOBUS_SUCCESS
 *         The request handle's reference count was decremented. The request
 *         structure is freed if this was the final reference to the handle.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE
 *         The request handle was not valid.
 * @see globus_gass_transfer_request_destroy()
 */
int
globus_i_gass_transfer_request_destroy(
    globus_gass_transfer_request_t		request)
{
    globus_bool_t				referenced;
    globus_gass_transfer_request_struct_t *	req;

    req =
	globus_handle_table_lookup(&globus_i_gass_transfer_request_handles,
				   request);
    if(req == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_INVALID_USE;
    }

    referenced =
	globus_handle_table_decrement_reference(&globus_i_gass_transfer_request_handles,
						request);
    if(!referenced)
    {
	int					i;
	globus_list_t *				tmp;

	tmp = globus_list_search(globus_i_gass_transfer_requests,
				 (void *) (intptr_t) request);
	
#if DEBUG_GASS_TRANSFER
	printf(_GTSL("removing from list\n"));
#endif
        if (tmp)
        {
            globus_list_remove(&globus_i_gass_transfer_requests,
                               tmp);

            globus_cond_signal(&globus_i_gass_transfer_shutdown_cond);
            
            if(req->attr)
            {
                globus_object_free(req->attr);
            }
            globus_fifo_destroy(&req->pending_data);
            if (req->url)
            {
                globus_free(req->url);
            }

            /* free referral */
            for(i = 0; i < req->referral_count; i++)
            {
                globus_free(req->referral_url[i]);
            }
            if(req->referral_url)
            {
                globus_free(req->referral_url);
            }
            req->referral_url = GLOBUS_NULL;
            req->referral_count = 0;

            /* free deny message */
            if(req->denial_message)
            {
                globus_free(req->denial_message);
            }

            /* free subject name */
            if(req->subject)
            {
                globus_free(req->subject);
            }

            globus_free(req);
            request = GLOBUS_NULL_HANDLE;
        }

	return GLOBUS_SUCCESS;
    }
    else
    {
	return GLOBUS_SUCCESS;
    }
}
/* globus_i_gass_transfer_request_destroy() */
#endif
