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
 * @file globus_gass_server_ez.c
 * @brief Simple File Server Library Implementation using GASS Transfer API
 */

/******************************************************************************
                             Include header files
******************************************************************************/
#include "globus_common.h"
#include "globus_gass_server_ez.h"
#include "globus_gass_transfer.h"

#include <stdio.h>
#include <errno.h>
#include "version.h"

/******************************************************************************
                               Type definitions
******************************************************************************/
/* Data type associated with each server_ez listener */
typedef struct globus_l_gass_server_ez_s
{
    globus_gass_transfer_listener_t listener;
    globus_gass_server_ez_client_shutdown_t callback;
    unsigned long options;
    globus_gass_transfer_requestattr_t * reqattr;
    globus_bool_t free_reqattr;
    globus_mutex_t lock; /* only acquire this in a server_ez_enter block */
    globus_bool_t closing;
} globus_l_gass_server_ez_t;

/******************************************************************************
                          Module specific variables
******************************************************************************/

static globus_hashtable_t globus_l_gass_server_ez_listeners;

static globus_mutex_t globus_l_gass_server_ez_mutex;
static globus_cond_t globus_l_gass_server_ez_cond;
static globus_bool_t globus_l_gass_server_ez_activated = GLOBUS_FALSE;

static const size_t MAX_DEFAULT_SIZE = 32*1024;
/******************************************************************************
                          Module definition
******************************************************************************/
static int
globus_l_gass_server_ez_activate(void);

static int
globus_l_gass_server_ez_deactivate(void);

globus_module_descriptor_t globus_i_gass_server_ez_module =
{
    "globus_gass_server_ez",
    globus_l_gass_server_ez_activate,
    globus_l_gass_server_ez_deactivate,
    NULL,
    NULL,
    &local_version
};


/******************************************************************************
                          Module specific prototypes
******************************************************************************/
/* callbacks called by globus_gass_transfer library when a request arrives */
static
void
globus_l_gass_server_ez_put_callback(
    void                               *arg,
    globus_gass_transfer_request_t      request,
    globus_byte_t                      *bytes,
    globus_size_t                       len,
    globus_bool_t                       last_data);

static
void
globus_l_gass_server_ez_get_callback(
    void                               *arg,
    globus_gass_transfer_request_t      request,
    globus_byte_t                      *bytes,
    globus_size_t                       len,
    globus_bool_t                       last_data);

/* callbacks to handle completed send or receive of part of the request's data
 */

static
void
globus_l_gass_server_ez_listen_callback(
    void                               *user_arg,
    globus_gass_transfer_listener_t     listener);

static
void
globus_l_gass_server_ez_close_callback(
    void                               *user_arg,
    globus_gass_transfer_listener_t     listener);

static
void
globus_l_gass_server_ez_register_accept_callback(
    void                               *user_arg,
    globus_gass_transfer_request_t      request);

/* utility routines */
static
int
globus_l_gass_server_ez_tilde_expand(
    unsigned long                       options,
    char                               *inpath,
    char                              **outpath);

#define globus_l_gass_server_ez_enter() globus_mutex_lock(&globus_l_gass_server_ez_mutex)
#define globus_l_gass_server_ez_exit()	globus_mutex_unlock(&globus_l_gass_server_ez_mutex)



/******************************************************************************
Function: globus_gass_server_ez_init()

Description: 

Parameters: 

Returns: 
******************************************************************************/
int
globus_gass_server_ez_init(
    globus_gass_transfer_listener_t    *listener,
    globus_gass_transfer_listenerattr_t
                                       *attr,
    char                               *scheme,
    globus_gass_transfer_requestattr_t
                                       *reqattr,
    unsigned long                       options,
    globus_gass_server_ez_client_shutdown_t
                                        callback)
{
    int                                 rc;
    globus_l_gass_server_ez_t          *server = NULL;
    const char                         *default_scheme = "https";
    globus_bool_t                       free_reqattr = GLOBUS_FALSE;

    if (scheme==NULL)
    {
        scheme = (char *) default_scheme;
    }

    if (reqattr==NULL)
    {
	reqattr = malloc(sizeof(globus_gass_transfer_requestattr_t));
        free_reqattr = GLOBUS_TRUE;

        globus_gass_transfer_requestattr_init(reqattr,
    					      scheme);
        globus_gass_transfer_secure_requestattr_set_authorization(
                reqattr,
                GLOBUS_GASS_TRANSFER_AUTHORIZE_SELF,
                scheme);
    }
    rc = globus_gass_transfer_create_listener(listener, attr, scheme);

    if (rc!=GLOBUS_SUCCESS)
    {
	goto error_exit;
    }

    server = malloc( sizeof (globus_l_gass_server_ez_t));
    if (server==NULL)
    {
        rc = GLOBUS_GASS_TRANSFER_ERROR_MALLOC_FAILED;
	goto error_exit;
    }

    server->options=options;
    server->listener=*listener;
    server->reqattr=reqattr;
    server->free_reqattr = free_reqattr;
    server->callback=callback;
    globus_mutex_init(&server->lock, NULL);
    server->closing = GLOBUS_FALSE;

    globus_l_gass_server_ez_enter();
    if (!globus_l_gass_server_ez_activated)
    {
        rc = GLOBUS_FAILURE;
        goto error_exit;
    }
    globus_hashtable_insert(&globus_l_gass_server_ez_listeners,
			    (void *) (intptr_t) *listener,
			    server);

    rc = globus_gass_transfer_register_listen(
            *listener,
            globus_l_gass_server_ez_listen_callback,
            reqattr);

    if (rc != GLOBUS_SUCCESS)
    {
        globus_hashtable_remove(
                &globus_l_gass_server_ez_listeners,
                (void*) (intptr_t) *listener);
error_exit:
        globus_mutex_destroy(&server->lock);
        free(server);
        if (free_reqattr)
        {
            globus_gass_transfer_requestattr_destroy(reqattr);
        }
    }
    globus_l_gass_server_ez_exit();

    return rc;
} /* globus_gass_server_ez_init() */

/******************************************************************************
Function: globus_gass_server_ez_shutdown()

Description: 

Parameters: 

Returns: 
******************************************************************************/
int
globus_gass_server_ez_shutdown(globus_gass_transfer_listener_t listener)
{
    int rc;
    globus_bool_t skip_close = GLOBUS_FALSE;
    globus_l_gass_server_ez_t *s;

    globus_l_gass_server_ez_enter();
    if (globus_l_gass_server_ez_activated)
    {
        s = globus_hashtable_lookup(
                &globus_l_gass_server_ez_listeners,
                (void*) (intptr_t) listener);
        if (s)
        {
            globus_mutex_lock(&s->lock);
            if (s->closing)
            {
                skip_close = GLOBUS_TRUE;
                rc = GLOBUS_SUCCESS;
            }
            else
            {
                s->closing = GLOBUS_TRUE;
            }
            globus_mutex_unlock(&s->lock);
        }
    }
    globus_l_gass_server_ez_exit();

    if (! skip_close)
    {
        rc = globus_gass_transfer_close_listener(
                listener,
                globus_l_gass_server_ez_close_callback,
                NULL); 
    }

    return rc;
} /* globus_gass_server_ez_shutdown() */

static
void
globus_l_gass_server_ez_destroy(void *arg)
{
    globus_l_gass_server_ez_t *server = arg;
    if (server)
    {
        globus_mutex_lock(&server->lock);
        if (server->free_reqattr)
        {
            globus_gass_transfer_requestattr_destroy(server->reqattr);
        }
        globus_mutex_unlock(&server->lock);
        globus_mutex_destroy(&server->lock);
        free(server);
    }
}

static void
globus_l_gass_server_ez_close_callback(
				void * user_arg,
				globus_gass_transfer_listener_t listener)
{
    globus_l_gass_server_ez_t *server;
    globus_l_gass_server_ez_enter();
    server = globus_hashtable_remove(
                &globus_l_gass_server_ez_listeners,
                (void *) (intptr_t) listener);
    globus_l_gass_server_ez_destroy(server);
    globus_cond_signal(&globus_l_gass_server_ez_cond);
    globus_l_gass_server_ez_exit();
}

static void
globus_l_gass_server_ez_listen_callback(
				void * user_arg,
				globus_gass_transfer_listener_t listener)
{
    int rc;
    globus_gass_transfer_request_t request;


    rc=globus_gass_transfer_register_accept(&request,
				 (globus_gass_transfer_requestattr_t *)
				 user_arg,
				 listener,
				 globus_l_gass_server_ez_register_accept_callback,
				 (void *) (intptr_t) listener);

    if(rc != GLOBUS_SUCCESS)
    {
	/* to listen for additional requests*/
	globus_gass_transfer_register_listen(
	    listener,
	    globus_l_gass_server_ez_listen_callback,
	    user_arg);
    }
}


static void
globus_l_gass_server_ez_register_accept_callback(
					void * listener,
					globus_gass_transfer_request_t request 
					)
{
    int rc;
    char * path=NULL;
    FILE *fp;
    char * url;
    globus_url_t parsed_url;
    globus_l_gass_server_ez_t * s;
    struct stat	statstruct;
    globus_byte_t * buf;
    int amt;
    const char *flags = "";
    size_t length;
    uintptr_t buffer_size;

    globus_l_gass_server_ez_enter();
    /* lookup our options */
    s = globus_hashtable_lookup(&globus_l_gass_server_ez_listeners, listener);
    if (s == NULL)
    {
        globus_gass_transfer_deny(request, 400, "Bad Request");
        globus_gass_transfer_request_destroy(request);
        globus_l_gass_server_ez_exit();
        return;
    }
    globus_mutex_lock(&s->lock);
    globus_l_gass_server_ez_exit();

    /* Check for valid URL */
    url=globus_gass_transfer_request_get_url(request);
    rc = globus_url_parse(url, &parsed_url);
    if(rc != GLOBUS_SUCCESS ||
       parsed_url.url_path == NULL || strlen(parsed_url.url_path) == 0U)
    {
        globus_gass_transfer_deny(request, 404, "File Not Found");
        globus_gass_transfer_request_destroy(request);
        if (rc == GLOBUS_SUCCESS)
            globus_url_destroy(&parsed_url);
	goto reregister_nourl;
    }

    if(globus_gass_transfer_request_get_type(request) ==
       GLOBUS_GASS_TRANSFER_REQUEST_TYPE_APPEND)
    {
        flags = "ab";
    }
    else if(globus_gass_transfer_request_get_type(request) ==
            GLOBUS_GASS_TRANSFER_REQUEST_TYPE_PUT)
    {
        flags = "wb";
    }
    switch(globus_gass_transfer_request_get_type(request))
        {
          case GLOBUS_GASS_TRANSFER_REQUEST_TYPE_APPEND:
          case GLOBUS_GASS_TRANSFER_REQUEST_TYPE_PUT:

	    /* Check to see if this is a request we are allowed to handle */

            if(((s->options & GLOBUS_GASS_SERVER_EZ_WRITE_ENABLE) == 0UL) &&
              ((s->options & GLOBUS_GASS_SERVER_EZ_STDOUT_ENABLE) == 0UL) &&
              ((s->options & GLOBUS_GASS_SERVER_EZ_STDERR_ENABLE) == 0UL) &&
              ((s->options & GLOBUS_GASS_SERVER_EZ_CLIENT_SHUTDOWN_ENABLE) ==
									 0UL))
    	    {
		goto deny;
            }
	
	    /* Expand ~ and ~user prefix if enaabled in options */
    	    rc = globus_l_gass_server_ez_tilde_expand(s->options,
                                              parsed_url.url_path,
                                              &path);
    	    /* Check for "special" file names, and if we will handle them */
    	    if(strcmp(path, "/dev/stdout") == 0 &&
              (s->options & GLOBUS_GASS_SERVER_EZ_STDOUT_ENABLE))
            {
        	fp = stdout;
		goto authorize;
    	    }
    	    else if(strcmp(path, "/dev/stdout") == 0)
    	    {
		goto deny;
    	    }
    	    else if(strcmp(path, "/dev/stderr") == 0 &&
                   (s->options & GLOBUS_GASS_SERVER_EZ_STDERR_ENABLE))
    	    {
                fp = stderr;
		goto authorize;
    	    }
    	    else if(strcmp(path, "/dev/stderr") == 0)
    	    {
		goto deny;
    	    }
    	    else if(strcmp(path, "/dev/globus_gass_client_shutdown") == 0)
    	    {
        	if(s->options & GLOBUS_GASS_SERVER_EZ_CLIENT_SHUTDOWN_ENABLE &&
           	   s->callback != NULL)
        	{
            	    s->callback();
        	}

		goto deny;
    	    }
            fp = fopen(path, flags);

            if(fp == NULL)
            {
                goto deny;
            }
	
	    authorize:
	    if(s->options & GLOBUS_GASS_SERVER_EZ_LINE_BUFFER)
	    {
                setvbuf(fp, NULL, _IOLBF, 0);
            }

            rc = fstat(fileno(fp), &statstruct);
            #ifdef _WIN32
            {
                buffer_size = MAX_DEFAULT_SIZE;
            }
            #else
            {
                if (rc == 0)
                {
                    if (statstruct.st_blksize > MAX_DEFAULT_SIZE)
                    {
                        buffer_size = MAX_DEFAULT_SIZE;
                    }
                    else
                    {
                        buffer_size = statstruct.st_blksize;
                    }
                }
                else
                {
                    buffer_size = MAX_DEFAULT_SIZE;
                }
            }
            #endif
            buf = malloc(buffer_size);
            if (!buf)
            {
                fclose(fp);
                goto deny;
            }
            globus_gass_transfer_request_set_user_pointer(request,
                    (void *) buffer_size);

            globus_gass_transfer_authorize(request, 0);

            globus_gass_transfer_receive_bytes(request,
                                               buf,
                                               buffer_size,
                                               1,
                                               globus_l_gass_server_ez_put_callback,
                                               (void *) fp);
            break;

          case GLOBUS_GASS_TRANSFER_REQUEST_TYPE_GET:
            flags = "rb";

			/* Expand ~ and ~user prefix if enaabled in options */
            rc = globus_l_gass_server_ez_tilde_expand(s->options,
                                              parsed_url.url_path,
                                              &path);

   	    if((s->options & GLOBUS_GASS_SERVER_EZ_READ_ENABLE) == 0UL)
    	    {
		goto deny;
    	    }
	   
	    if(stat(path, &statstruct)==0)
	    {
                fp = fopen(path, flags);

                if (statstruct.st_size > SIZE_MAX)
                {
                    length = GLOBUS_GASS_TRANSFER_LENGTH_UNKNOWN;
                }
                else
                {
                    length = statstruct.st_size;
                }
	    }
	    else
	    {
		globus_gass_transfer_deny(request, 404, "File Not Found");
		globus_gass_transfer_request_destroy(request);
		goto reregister;
	    }

            #ifdef _WIN32
            {
                if (statstruct.st_size > MAX_DEFAULT_SIZE)
                {
                    buffer_size = MAX_DEFAULT_SIZE;
                }
                else
                {
                    buffer_size = statstruct.st_size;
                }
            }
            #else
            {
                if (statstruct.st_blksize > statstruct.st_size)
                {
                    if (statstruct.st_size > MAX_DEFAULT_SIZE)
                    {
                        buffer_size = MAX_DEFAULT_SIZE;
                    }
                    else
                    {
                        buffer_size = statstruct.st_size;
                    }
                }
                else
                {
                    if (statstruct.st_blksize > MAX_DEFAULT_SIZE)
                    {
                        buffer_size = MAX_DEFAULT_SIZE;
                    }
                    else
                    {
                        buffer_size = statstruct.st_blksize;
                    }
                }
            }
            #endif

            buf = malloc(buffer_size);
            if (!buf)
            {
                fclose(fp);
                goto deny;
            }
            amt = fread(buf, 1, buffer_size, fp);
            if(amt == 0 && ferror(fp))
            {
                free(buf);
                fclose(fp);
                goto deny;
            }
            globus_gass_transfer_request_set_user_pointer(request,
                    (void *) buffer_size);
            globus_gass_transfer_authorize(request, (size_t) length);

            globus_gass_transfer_send_bytes(request,
                                            buf,
                                            amt,
                                            GLOBUS_FALSE,
                                            globus_l_gass_server_ez_get_callback,
                                            (void *) fp);
	  break;
	default:
	deny:
	  globus_gass_transfer_deny(request, 400, "Bad Request");
	  globus_gass_transfer_request_destroy(request);

	}

  reregister:
    globus_url_destroy(&parsed_url);

  reregister_nourl:
    globus_gass_transfer_register_listen(
				(globus_gass_transfer_listener_t) (intptr_t)
                                    listener,
				globus_l_gass_server_ez_listen_callback,
				s->reqattr);

    if (path != NULL) free(path);
    globus_mutex_unlock(&s->lock);

} /*globus_l_gass_server_ez_register_accept_callback*/


/******************************************************************************
Function: globus_l_gass_server_ez_get_callback()

Description: 

Parameters: 

Returns: 
******************************************************************************/
static void 
globus_l_gass_server_ez_get_callback(
    void *arg,
    globus_gass_transfer_request_t request,
    globus_byte_t *     bytes,
    globus_size_t       len,
    globus_bool_t       last_data)
{
    FILE *fp;
    globus_size_t amt;

    fp = arg;
    if(!last_data)
    {
	amt = fread(bytes, 1, len, fp);
        if(amt == 0)
        {
            globus_gass_transfer_send_bytes(request,
                                            bytes,
                                            0,
                                            GLOBUS_TRUE,
                                            globus_l_gass_server_ez_get_callback,
                                            arg);
        }
        else
        {
            globus_gass_transfer_send_bytes(request,
                                            bytes,
                                            amt,
                                            GLOBUS_FALSE,
                                            globus_l_gass_server_ez_get_callback,
                                            arg);
        }
        return;
    }

    if (fp != stdout || fp != stderr)
    {
        fclose(fp);
    }
    free(bytes);
    globus_gass_transfer_request_destroy(request);

} /* globus_l_gass_server_ez_get_callback() */

/******************************************************************************
Function: globus_l_gass_server_ez_put_callback()

Description: 

Parameters: 

Returns: 
******************************************************************************/
static void
globus_l_gass_server_ez_put_callback(
				    void *arg,
				    globus_gass_transfer_request_t request,
				    globus_byte_t *     bytes,
				    globus_size_t       len,
				    globus_bool_t       last_data)
{
    FILE *fp;
    uintptr_t blocksize = (uintptr_t)
        globus_gass_transfer_request_get_user_pointer(request);

    fp = arg;

    fwrite(bytes, len, 1, fp);
    if(!last_data)
    {
        globus_gass_transfer_receive_bytes(request,
                                           bytes,
                                           (size_t) blocksize,
                                           1,
                                           globus_l_gass_server_ez_put_callback,
                                           arg);
    return ;
    }

    if ((fp!=stdout) && (fp!=stderr))
    {
        fclose(fp);
    }
    free(bytes);
    globus_gass_transfer_request_destroy(request);
    return ;

} /* globus_l_gass_server_ez_put_callback() */

/******************************************************************************
Function: globus_l_gass_server_ez_tilde_expand()

Description: 

Parameters: 

Returns: 
******************************************************************************/
static int
globus_l_gass_server_ez_tilde_expand(unsigned long options,
			     char *inpath,
			     char **outpath)
{
    /*
     * If this is a relative path, the strip off the leading /./
     */
    if(strlen(inpath) >= 2)
    {
	if (inpath[0] == '/' && inpath[1] == '.' && inpath[2] == '/')
	{
	    inpath += 3;
	    goto notilde;
	}
    }

    /* here call the new function globus_tilde_expand()*/
    if (globus_tilde_expand(options, GLOBUS_TRUE, inpath, outpath)
            == GLOBUS_SUCCESS)
    {
        return GLOBUS_SUCCESS;
    }

notilde:
    *outpath = malloc(strlen(inpath)+1);
    if (*outpath == NULL)
    {
        return GLOBUS_FAILURE;
    }
    strcpy(*outpath, inpath);
    return GLOBUS_SUCCESS;
} /* globus_l_gass_server_ez_tilde_expand() */

/******************************************************************************
Function: globus_l_gass_server_ez_activate()

Description: 

Parameters: 

Returns: 
******************************************************************************/
static int
globus_l_gass_server_ez_activate(void)
{
    int rc;
   
    rc = globus_module_activate(GLOBUS_COMMON_MODULE); 
    if(rc != GLOBUS_SUCCESS)
    {
        return rc;
    }
	
    rc = globus_module_activate(GLOBUS_GASS_TRANSFER_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }

    globus_mutex_init(&globus_l_gass_server_ez_mutex, NULL);
    globus_cond_init(&globus_l_gass_server_ez_cond, NULL);
    globus_l_gass_server_ez_activated = GLOBUS_TRUE;

    globus_hashtable_init(&globus_l_gass_server_ez_listeners,
                          16,
                          globus_hashtable_int_hash,
                          globus_hashtable_int_keyeq);

    return GLOBUS_SUCCESS;
} /* globus_l_gass_server_ez_activate() */

/******************************************************************************
Function: globus_l_gass_server_ez_deactivate()

Description: 

Parameters: 

Returns: 
******************************************************************************/
static int
globus_l_gass_server_ez_deactivate(void)
{
    int rc;
    globus_l_gass_server_ez_t *s;
    
    globus_mutex_lock(&globus_l_gass_server_ez_mutex);
    globus_l_gass_server_ez_activated = GLOBUS_FALSE;
    for (s = globus_hashtable_first(&globus_l_gass_server_ez_listeners);
         s != NULL;
         s = globus_hashtable_next(&globus_l_gass_server_ez_listeners))
    {
        globus_bool_t skip_closing = GLOBUS_TRUE;

        globus_mutex_lock(&s->lock);
        if (!s->closing)
        {
            s->closing = GLOBUS_TRUE;
            skip_closing = GLOBUS_FALSE;
        }
        globus_mutex_unlock(&s->lock);

        if (!skip_closing)
        {
            globus_gass_transfer_close_listener(
                    s->listener,
                    globus_l_gass_server_ez_close_callback,
                    NULL);
        }
    }
    while (globus_hashtable_size(&globus_l_gass_server_ez_listeners) > 0)
    {
        globus_cond_wait(
                &globus_l_gass_server_ez_cond,
                &globus_l_gass_server_ez_mutex);
    }
    globus_mutex_unlock(&globus_l_gass_server_ez_mutex);
    
    rc = globus_module_deactivate(GLOBUS_GASS_TRANSFER_MODULE);

    globus_mutex_lock(&globus_l_gass_server_ez_mutex);
    globus_hashtable_destroy_all(
            &globus_l_gass_server_ez_listeners,
            globus_l_gass_server_ez_destroy);
    globus_mutex_unlock(&globus_l_gass_server_ez_mutex);
    globus_mutex_destroy(&globus_l_gass_server_ez_mutex);

    rc|=globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return rc;
} /* globus_l_gass_server_ez_deactivate() */

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
