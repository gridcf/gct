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
 * @file globus_url.c
 * @brief Globus URL parsing utility functions
 *
 * Parses URLs of the form
 * @code
 *  <scheme>://[<user>[:<password>]@]<host>[:<port>]/<url-path>
 * @endcode
 *
 * @code
 * <scheme>://<scheme-specific-part>
 * @endcode
 */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

#include "globus_url.h"
#include "globus_libc.h"

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/*
 * Module specific prototypes
 */
static int globusl_url_get_substring(const char *src,
				    char **destp,
				    int nullp);

static int globusl_url_get_scheme(const char **stringp,
				 char **scheme,
				 globus_url_scheme_t *scheme_type);

static int globusl_url_get_user_password(const char **stringp,
					char **user,
					char **password);

static int globusl_url_get_host_port(const char **stringp,
				    char **host,
				    unsigned short *port);

static int globusl_url_get_path(const char **stringp,
			       char **url_path,
			       globus_url_scheme_t scheme_type);

static int globusl_url_get_path_rfc1738(const char **stringp,
			       char **url_path,
			       globus_url_scheme_t scheme_type);

static int globusl_url_get_path_loose(const char **stringp,
			       char **url_path,
			       globus_url_scheme_t scheme_type);

static int globusl_url_get_ldap_specific(const char **stringp,
					char **dn,
					char **attributes,
					char **scope,
					char **filter);
	
static int globusl_url_get_file_specific(const char **stringp,
			       char ** host,
			       char ** path);

static int globusl_url_get_file_specific_loose(const char **stringp,
			       char ** host,
			       char ** path);

static int globusl_url_issafe(char x);
static int globusl_url_isextra(char x);
static int globusl_url_isscheme_special(char x);
static int globusl_url_isglob(char x);
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

/**
 * Parse a string containing a URL into a globus_url_t
 * @ingroup globus_url
 *
 * @param url_string
 *        String to parse
 * @param url
 *        Pointer to globus_url_t to be filled with the fields of the url
 *
 * @retval GLOBUS_SUCCESS
 *         The string was successfully parsed.
 * @retval GLOBUS_URL_ERROR_NULL_STRING
 *         The url_string was GLOBUS_NULL.
 * @retval GLOBUS_URL_ERROR_NULL_URL
 *         The URL pointer was GLOBUS_NULL.
 * @retval GLOBUS_URL_ERROR_BAD_SCHEME 
 *         The URL scheme (protocol) contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_USER 
 *         The user part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_PASSWORD
 *         The password part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_HOST
 *         The host part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_PORT
 *         The port part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_PATH
 *         The path part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_DN -9
 *         The DN part of an LDAP URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_ATTRIBUTES -10
 *         The attributes part of an LDAP URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_SCOPE -11
 *         The scope part of an LDAP URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_FILTER -12
 *         The filter part of an LDAP URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_OUT_OF_MEMORY -13
 *         The library was unable to allocate memory to create the
 *         the globus_url_t contents.
 * @retval GLOBUS_URL_ERROR_INTERNAL_ERROR -14
 *         Some unexpected error occurred parsing the URL.
 */
int
globus_url_parse(const char *url_string,
		 globus_url_t *url)
{
    const char *substring;		/* where we are in the parse */
    int rc;			/* return code from helper functions */
    
    if(url == NULL)
    {
	return GLOBUS_URL_ERROR_NULL_URL;
    }

    url->scheme = NULL;
    url->host = NULL;
    url->port = 0;
    url->user = NULL;
    url->password = NULL;
    url->url_path = NULL;
    url->url_specific_part = NULL;
    url->dn = NULL;
    url->attributes = NULL;
    url->scope = NULL;
    url->filter = NULL;

    if(url_string == NULL)
    {
	return GLOBUS_URL_ERROR_NULL_STRING;
    }
    
    substring = url_string;

    rc = globusl_url_get_scheme(&substring, 
			       &(url->scheme),
			       &(url->scheme_type));
    if(rc != GLOBUS_SUCCESS)
    {
	goto parse_error;
    }

    if(strncmp(substring, "://", 3) != 0 &&
       url->scheme_type != GLOBUS_URL_SCHEME_FILE)
    {
	rc = GLOBUS_URL_ERROR_BAD_SCHEME;
	goto parse_error;
    }
    else if(url->scheme_type == GLOBUS_URL_SCHEME_FILE)
    {
	substring++;
    }
    else
    {
	substring+=3;
    }
    switch(url->scheme_type)
    {
    case GLOBUS_URL_SCHEME_FTP:
    case GLOBUS_URL_SCHEME_GSIFTP:
    case GLOBUS_URL_SCHEME_SSHFTP:
	/* optional part of an ftp scheme, password is
	   only set if user is set
	*/
	rc = globusl_url_get_user_password(&substring,
					  &(url->user),
					  &(url->password));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	/* fall through here */
    case GLOBUS_URL_SCHEME_HTTP:
    case GLOBUS_URL_SCHEME_HTTPS:
	/* port not http or ftp */
	rc=globusl_url_get_host_port(&substring,
				    &(url->host),
				    &(url->port));
	if(rc != GLOBUS_SUCCESS)
	{
	    rc = GLOBUS_URL_ERROR_BAD_PORT;
	    goto parse_error;
	}

	rc=globusl_url_get_path(&substring,
			       &(url->url_path),
			       url->scheme_type);
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	break;

    case GLOBUS_URL_SCHEME_X_NEXUS:
	/* port required for x-nexus URL */
	rc=globusl_url_get_host_port(&substring,
				 &(url->host),
				 &(url->port));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	if(url->port == 0)
	{
	    rc = GLOBUS_URL_ERROR_BAD_PORT;
	    goto parse_error;
	}
	break;
    case GLOBUS_URL_SCHEME_LDAP:
	rc = globusl_url_get_host_port(&substring,
				      &(url->host),
				      &(url->port));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	if(*substring != '/')
	{
	    rc = GLOBUS_URL_ERROR_BAD_DN;
	    goto parse_error;
	}
	else
	{
	    substring++;
	}
	rc = globusl_url_get_ldap_specific(&substring,
					  &(url->dn),
					  &(url->attributes),
					  &(url->scope),
					  &(url->filter));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	break;

    case GLOBUS_URL_SCHEME_FILE:
	rc = globusl_url_get_file_specific(&substring,
					   &(url->host),
					   &(url->url_path));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	break;

    case GLOBUS_URL_SCHEME_X_GASS_CACHE:
	rc = globusl_url_get_substring(substring,
				      &(url->url_specific_part),
				      strlen(substring));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}	
	break;
	
    default:
	rc = globusl_url_get_host_port(&substring,
				      &(url->host),
				      &(url->port));
	if(rc == GLOBUS_URL_ERROR_INTERNAL_ERROR)
	{
	    goto parse_error;
	}
	if(rc == GLOBUS_SUCCESS)
	{
	    rc = globusl_url_get_path(&substring,
				     &(url->url_path),
				     url->scheme_type);
	    if(rc == GLOBUS_URL_ERROR_INTERNAL_ERROR)
	    {
		goto parse_error;
	    }
	    if(rc == GLOBUS_SUCCESS)
	    {
		break;
	    }
	}
	rc = globusl_url_get_substring(substring,
				      &(url->url_specific_part),
				      strlen(substring));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
    }
    return rc;

parse_error:
    globus_url_destroy(url);

    return rc;
}
/* globus_url_parse() */


/**
 * Parse a string containing a URL into a globus_url_t
 * @ingroup globus_url
 *
 * @param url_string
 *        String to parse
 * @param url
 *        Pointer to globus_url_t to be filled with the fields of the url
 *
 * @retval GLOBUS_SUCCESS
 *         The string was successfully parsed.
 * @retval GLOBUS_URL_ERROR_NULL_STRING
 *         The url_string was GLOBUS_NULL.
 * @retval GLOBUS_URL_ERROR_NULL_URL
 *         The URL pointer was GLOBUS_NULL.
 * @retval GLOBUS_URL_ERROR_BAD_SCHEME 
 *         The URL scheme (protocol) contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_USER 
 *         The user part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_PASSWORD
 *         The password part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_HOST
 *         The host part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_PORT
 *         The port part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_PATH
 *         The path part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_DN -9
 *         The DN part of an LDAP URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_ATTRIBUTES -10
 *         The attributes part of an LDAP URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_SCOPE -11
 *         The scope part of an LDAP URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_FILTER -12
 *         The filter part of an LDAP URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_OUT_OF_MEMORY -13
 *         The library was unable to allocate memory to create the
 *         the globus_url_t contents.
 * @retval GLOBUS_URL_ERROR_INTERNAL_ERROR -14
 *         Some unexpected error occurred parsing the URL.
 */
int
globus_url_parse_rfc1738(const char *url_string,
		 globus_url_t *url)
{
    const char *substring;		/* where we are in the parse */
    int rc;			/* return code from helper functions */
    
    if(url == NULL)
    {
	return GLOBUS_URL_ERROR_NULL_URL;
    }

    url->scheme = NULL;
    url->host = NULL;
    url->port = 0;
    url->user = NULL;
    url->password = NULL;
    url->url_path = NULL;
    url->url_specific_part = NULL;
    url->dn = NULL;
    url->attributes = NULL;
    url->scope = NULL;
    url->filter = NULL;

    if(url_string == NULL)
    {
	return GLOBUS_URL_ERROR_NULL_STRING;
    }
    
    substring = url_string;

    rc = globusl_url_get_scheme(&substring, 
			       &(url->scheme),
			       &(url->scheme_type));
    if(rc != GLOBUS_SUCCESS)
    {
	goto parse_error;
    }

    if(strncmp(substring, "://", 3) != 0 &&
       url->scheme_type != GLOBUS_URL_SCHEME_FILE)
    {
	rc = GLOBUS_URL_ERROR_BAD_SCHEME;
	goto parse_error;
    }
    else if(url->scheme_type == GLOBUS_URL_SCHEME_FILE)
    {
	substring++;
    }
    else
    {
	substring+=3;
    }
    switch(url->scheme_type)
    {
    case GLOBUS_URL_SCHEME_FTP:
    case GLOBUS_URL_SCHEME_GSIFTP:
    case GLOBUS_URL_SCHEME_SSHFTP:
	/* optional part of an ftp scheme, password is
	   only set if user is set
	*/
	rc = globusl_url_get_user_password(&substring,
					  &(url->user),
					  &(url->password));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	rc=globusl_url_get_host_port(&substring,
				    &(url->host),
				    &(url->port));
	if(rc != GLOBUS_SUCCESS)
	{
	    rc = GLOBUS_URL_ERROR_BAD_PORT;
	    goto parse_error;
	}
	rc=globusl_url_get_path_rfc1738(&substring,
			       &(url->url_path),
			       url->scheme_type);
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	break;
	/* fall through here */
    case GLOBUS_URL_SCHEME_HTTP:
    case GLOBUS_URL_SCHEME_HTTPS:
	/* port not http or ftp */
	rc=globusl_url_get_host_port(&substring,
				    &(url->host),
				    &(url->port));
	if(rc != GLOBUS_SUCCESS)
	{
	    rc = GLOBUS_URL_ERROR_BAD_PORT;
	    goto parse_error;
	}

	rc=globusl_url_get_path(&substring,
			       &(url->url_path),
			       url->scheme_type);
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	break;

    case GLOBUS_URL_SCHEME_X_NEXUS:
	/* port required for x-nexus URL */
	rc=globusl_url_get_host_port(&substring,
				 &(url->host),
				 &(url->port));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	if(url->port == 0)
	{
	    rc = GLOBUS_URL_ERROR_BAD_PORT;
	    goto parse_error;
	}
	break;
    case GLOBUS_URL_SCHEME_LDAP:
	rc = globusl_url_get_host_port(&substring,
				      &(url->host),
				      &(url->port));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	if(*substring != '/')
	{
	    rc = GLOBUS_URL_ERROR_BAD_DN;
	    goto parse_error;
	}
	else
	{
	    substring++;
	}
	rc = globusl_url_get_ldap_specific(&substring,
					  &(url->dn),
					  &(url->attributes),
					  &(url->scope),
					  &(url->filter));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	break;

    case GLOBUS_URL_SCHEME_FILE:
	rc = globusl_url_get_file_specific(&substring,
					   &(url->host),
					   &(url->url_path));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	break;

    case GLOBUS_URL_SCHEME_X_GASS_CACHE:
	rc = globusl_url_get_substring(substring,
				      &(url->url_specific_part),
				      strlen(substring));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}	
	break;
	
    default:
	rc = globusl_url_get_host_port(&substring,
				      &(url->host),
				      &(url->port));
	if(rc == GLOBUS_URL_ERROR_INTERNAL_ERROR)
	{
	    goto parse_error;
	}
	if(rc == GLOBUS_SUCCESS)
	{
	    rc = globusl_url_get_path(&substring,
				     &(url->url_path),
				     url->scheme_type);
	    if(rc == GLOBUS_URL_ERROR_INTERNAL_ERROR)
	    {
		goto parse_error;
	    }
	    if(rc == GLOBUS_SUCCESS)
	    {
		break;
	    }
	}
	rc = globusl_url_get_substring(substring,
				      &(url->url_specific_part),
				      strlen(substring));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
    }
    return rc;

parse_error:
    globus_url_destroy(url);

    return rc;
}
/* globus_url_parse_rfc1738() */


/**
 * Parse a string containing a URL into a globus_url_t
 * Looser restrictions on characters allowed in the path
 * part of the URL.
 * @ingroup globus_url
 *
 * @param url_string
 *        String to parse
 * @param url
 *        Pointer to globus_url_t to be filled with the fields of the url
 *
 * @retval GLOBUS_SUCCESS
 *         The string was successfully parsed.
 * @retval GLOBUS_URL_ERROR_NULL_STRING
 *         The url_string was GLOBUS_NULL.
 * @retval GLOBUS_URL_ERROR_NULL_URL
 *         The URL pointer was GLOBUS_NULL.
 * @retval GLOBUS_URL_ERROR_BAD_SCHEME 
 *         The URL scheme (protocol) contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_USER 
 *         The user part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_PASSWORD
 *         The password part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_HOST
 *         The host part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_PORT
 *         The port part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_PATH
 *         The path part of the URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_DN -9
 *         The DN part of an LDAP URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_ATTRIBUTES -10
 *         The attributes part of an LDAP URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_SCOPE -11
 *         The scope part of an LDAP URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_BAD_FILTER -12
 *         The filter part of an LDAP URL contained invalid characters.
 * @retval GLOBUS_URL_ERROR_OUT_OF_MEMORY -13
 *         The library was unable to allocate memory to create the
 *         the globus_url_t contents.
 * @retval GLOBUS_URL_ERROR_INTERNAL_ERROR -14
 *         Some unexpected error occurred parsing the URL.
 */
int
globus_url_parse_loose(const char *url_string,
		 globus_url_t *url)
{
    const char *substring;		/* where we are in the parse */
    int rc;			/* return code from helper functions */
    
    if(url == NULL)
    {
	return GLOBUS_URL_ERROR_NULL_URL;
    }

    url->scheme = NULL;
    url->host = NULL;
    url->port = 0;
    url->user = NULL;
    url->password = NULL;
    url->url_path = NULL;
    url->url_specific_part = NULL;
    url->dn = NULL;
    url->attributes = NULL;
    url->scope = NULL;
    url->filter = NULL;

    if(url_string == NULL)
    {
	return GLOBUS_URL_ERROR_NULL_STRING;
    }
    
    substring = url_string;

    rc = globusl_url_get_scheme(&substring, 
			       &(url->scheme),
			       &(url->scheme_type));
    if(rc != GLOBUS_SUCCESS)
    {
	goto parse_error;
    }

    if(strncmp(substring, "://", 3) != 0 &&
       url->scheme_type != GLOBUS_URL_SCHEME_FILE)
    {
	rc = GLOBUS_URL_ERROR_BAD_SCHEME;
	goto parse_error;
    }
    else if(url->scheme_type == GLOBUS_URL_SCHEME_FILE)
    {
	substring++;
    }
    else
    {
	substring+=3;
    }
    switch(url->scheme_type)
    {
    case GLOBUS_URL_SCHEME_FTP:
    case GLOBUS_URL_SCHEME_GSIFTP:
    case GLOBUS_URL_SCHEME_SSHFTP:
	/* optional part of an ftp scheme, password is
	   only set if user is set
	*/
	rc = globusl_url_get_user_password(&substring,
					  &(url->user),
					  &(url->password));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	/* fall through here */
    case GLOBUS_URL_SCHEME_HTTP:
    case GLOBUS_URL_SCHEME_HTTPS:
	/* port not http or ftp */
	rc=globusl_url_get_host_port(&substring,
				    &(url->host),
				    &(url->port));
	if(rc != GLOBUS_SUCCESS)
	{
	    rc = GLOBUS_URL_ERROR_BAD_PORT;
	    goto parse_error;
	}

	rc=globusl_url_get_path_loose(&substring,
			       &(url->url_path),
			       url->scheme_type);
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	break;

    case GLOBUS_URL_SCHEME_X_NEXUS:
	/* port required for x-nexus URL */
	rc=globusl_url_get_host_port(&substring,
				 &(url->host),
				 &(url->port));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	if(url->port == 0)
	{
	    rc = GLOBUS_URL_ERROR_BAD_PORT;
	    goto parse_error;
	}
	break;
    case GLOBUS_URL_SCHEME_LDAP:
	rc = globusl_url_get_host_port(&substring,
				      &(url->host),
				      &(url->port));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	if(*substring != '/')
	{
	    rc = GLOBUS_URL_ERROR_BAD_DN;
	    goto parse_error;
	}
	else
	{
	    substring++;
	}
	rc = globusl_url_get_ldap_specific(&substring,
					  &(url->dn),
					  &(url->attributes),
					  &(url->scope),
					  &(url->filter));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	break;

    case GLOBUS_URL_SCHEME_FILE:
	rc = globusl_url_get_file_specific_loose(&substring,
					   &(url->host),
					   &(url->url_path));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
	break;

    case GLOBUS_URL_SCHEME_X_GASS_CACHE:
	rc = globusl_url_get_substring(substring,
				      &(url->url_specific_part),
				      strlen(substring));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}	
	break;
	
    default:
	rc = globusl_url_get_host_port(&substring,
				      &(url->host),
				      &(url->port));
	if(rc == GLOBUS_URL_ERROR_INTERNAL_ERROR)
	{
	    goto parse_error;
	}
	if(rc == GLOBUS_SUCCESS)
	{
	    rc = globusl_url_get_path(&substring,
				     &(url->url_path),
				     url->scheme_type);
	    if(rc == GLOBUS_URL_ERROR_INTERNAL_ERROR)
	    {
		goto parse_error;
	    }
	    if(rc == GLOBUS_SUCCESS)
	    {
		break;
	    }
	}
	rc = globusl_url_get_substring(substring,
				      &(url->url_specific_part),
				      strlen(substring));
	if(rc != GLOBUS_SUCCESS)
	{
	    goto parse_error;
	}
    }
    return rc;

parse_error:
    globus_url_destroy(url);

    return rc;
}
/* globus_url_parse_loose() */

/**
 * Destroy a globus_url_t structure.
 * @ingroup globus_url
 *
 * This function frees all memory associated with a
 * globus_url_t structure.
 *
 * @param url
 *        The url structure to destroy
 *
 * @retval GLOBUS_SUCCESS
 *         The URL was successfully destroyed.
 */
int
globus_url_destroy(globus_url_t *url)
{
    if(url == NULL)
    {
	return GLOBUS_URL_ERROR_NULL_URL;
    }

    if(url->scheme != NULL)
    {
	free(url->scheme);
	url->scheme=NULL;
    }
    if(url->user != NULL)
    {
	free(url->user);
	url->user=NULL;
    }
    if(url->password != NULL)
    {
	free(url->password);
	url->password = NULL;
    }
    if(url->host != NULL)
    {
	free(url->host);
	url->host = NULL;
    }
    if(url->url_path != NULL)
    {
	free(url->url_path);
	url->url_path = NULL;
    }
    if(url->dn != NULL)
    {
	free(url->dn);
	url->dn = NULL;
    }
    if(url->attributes != NULL)
    {
	free(url->attributes);
	url->attributes = NULL;
    }
    if(url->scope != NULL)
    {
	free(url->scope);
	url->scope = NULL;
    }
    if(url->filter != NULL)
    {
	free(url->filter);
	url->filter = NULL;
    }
    if(url->url_specific_part != NULL)
    {
	free(url->url_specific_part);
	url->url_specific_part = NULL;
    }
    return GLOBUS_SUCCESS;
}

/******************************************************************************
Function: globusl_url_get_substring()

Description: Copy a substring of the src string

Parameters:

Returns: GLOBUS_SUCCESS on success, GLOBUS_URL_ERROR on ERROR
******************************************************************************/
static int
globusl_url_get_substring (const char *src, char **destp, int nulpos)
{
    int i;
    int j;
    
    *(destp) = globus_malloc (sizeof(char)*(nulpos+1));
    if(*destp == NULL)
    {
	return GLOBUS_URL_ERROR_NULL_STRING;
    }
    
    for(i = 0, j = 0; i < nulpos; i++, j++) {
	if(src[i] == '%' && i + 2 < nulpos)
	{
	    if(isxdigit(src[i+1]) &&
	       isxdigit(src[i+2]))
	    {
		char hexstring[3];

		hexstring[0] = src[i+1];
		hexstring[1] = src[i+2];
		hexstring[2] = '\0';

		(*(destp))[j] = (char) (int) strtol(hexstring, NULL, 16);
		i += 2;
		continue;
	    }
	}
	(*(destp))[j] = (src)[i];
    }

    (*(destp))[j] = '\0';

    return GLOBUS_SUCCESS;
}

/******************************************************************************
Function: globusl_url_get_scheme()

Description:
  Copy the scheme (protocol type) of the url in the second argument
  

Parameters: (stringp in) (scheme out)

Returns: GLOBUS_SUCCESS on success, GLOBUS_URL_ERROR_INTERNAL_ERROR on ERROR
******************************************************************************/
static int
globusl_url_get_scheme(const char **stringp, 
		      char **scheme,
		      globus_url_scheme_t *scheme_type)
{
  int pos = 0;

  /* scan stringp for a URL scheme */
  if(stringp == NULL)
  {
      return GLOBUS_URL_ERROR_BAD_SCHEME;
  }
  if(*stringp == NULL)
  {
      return GLOBUS_URL_ERROR_BAD_SCHEME;
  }
  if(scheme == NULL || scheme_type == NULL)
  {
      return GLOBUS_URL_ERROR_INTERNAL_ERROR;
  }

  while(islower((*stringp)[pos]) ||
	isdigit((*stringp)[pos]) ||
	(*stringp)[pos] == '+' ||
	(*stringp)[pos] == '-' ||
	(*stringp)[pos] == '.')
  {
      pos++;
  }

  if((*stringp)[pos] != ':')
  {
      return GLOBUS_URL_ERROR_BAD_SCHEME;
  }

  if(pos == 0)
  {
      return GLOBUS_URL_ERROR_BAD_SCHEME;
  }

  if(globusl_url_get_substring(*stringp, scheme, pos) != GLOBUS_SUCCESS)
  {
      return GLOBUS_URL_ERROR_INTERNAL_ERROR;
  }

  /* move past : */
  (*stringp) += pos++;

  /* parse the URL scheme found */
  if(strcmp(*scheme, "ftp") == 0)
  {
      *scheme_type=GLOBUS_URL_SCHEME_FTP;
  }
  else if(strcmp(*scheme, "gsiftp") == 0)
  {
      *scheme_type=GLOBUS_URL_SCHEME_GSIFTP;
  }
  else if(strcmp(*scheme, "http") == 0)
  {
      *scheme_type=GLOBUS_URL_SCHEME_HTTP;
  }
  else if(strcmp(*scheme, "https") == 0)
  {
      *scheme_type=GLOBUS_URL_SCHEME_HTTPS;
  }
  else if(strcmp(*scheme, "ldap") == 0)
  {
      *scheme_type=GLOBUS_URL_SCHEME_LDAP;
  }
  else if(strcmp(*scheme, "file") == 0)
  {
      *scheme_type=GLOBUS_URL_SCHEME_FILE;
  }
  else if(strcmp(*scheme, "x-nexus") == 0)
  {
      *scheme_type=GLOBUS_URL_SCHEME_X_NEXUS;
  }
  else if(strcmp(*scheme, "x-gass-cache") == 0)
  {
      *scheme_type=GLOBUS_URL_SCHEME_X_GASS_CACHE;
  }
  else if(strcmp(*scheme, "sshftp") == 0)
  {
      *scheme_type=GLOBUS_URL_SCHEME_SSHFTP;
  }
  else
  {
      *scheme_type=GLOBUS_URL_SCHEME_UNKNOWN;
  }
  return GLOBUS_SUCCESS;
}

/******************************************************************************
Function: globusl_url_get_user_password()

Description:
  Copy the optional user and optional password into the out variables

  Can return SUCCESS even if neither user or password is found, because
  these are optional parts of the url.

Parameters: (stringp in) (user out) (password out)

Returns: GLOBUS_SUCCESS on success, GLOBUS_URL_ERROR_INTERNAL_ERROR on ERROR
******************************************************************************/
static int
globusl_url_get_user_password(const char **stringp, 
			     char **user,
			     char **password)
{
    int pos = 0;
    int startpos = 0;
    int lastpos=0;
    int rc;

    /* scan stringp for [user[:password]@] */
    if(stringp == NULL)
    {
       return GLOBUS_URL_ERROR_INTERNAL_ERROR;
    }
    if(*stringp == NULL)
    {
       return GLOBUS_URL_ERROR_INTERNAL_ERROR;
    }
    if(user == NULL || password == NULL)
    {
       return GLOBUS_URL_ERROR_INTERNAL_ERROR;
    }
    /* pre-scan for '@' so we don't get confused by ':' */
    do
    {
	lastpos=pos;
	while(isalnum((*stringp)[pos]) ||
	      globusl_url_issafe((*stringp)[pos]) ||
	      globusl_url_isextra((*stringp)[pos]) ||
	      (*stringp)[pos] == ';' ||
	      (*stringp)[pos] == '?' ||
	      (*stringp)[pos] == '&' ||
	      (*stringp)[pos] == '=' ||
	      (*stringp)[pos] == ':')
	{
	    pos++;
	}
	if((*stringp)[pos] == '\0')
	{
	    /* didn't find anything, but it's optional, anyway */
	    return GLOBUS_SUCCESS;
	}
	if((*stringp)[pos] == '%')
	{
	    pos++;
	    if(isxdigit((*stringp)[pos]))
	    {
		pos++;
		if(isxdigit((*stringp)[pos]))
		{
		    pos++;
		}
		else
		{
		    return GLOBUS_URL_ERROR_BAD_USER;
		}
	    }
	    else
	    {
		return GLOBUS_URL_ERROR_BAD_USER;
	    }
	}
    }
    while((*stringp)[pos] != '@' &&
          pos != lastpos);

    if((*stringp)[pos] != '@')
    {
	return GLOBUS_SUCCESS;
    }

    pos = 0;

    do
    {
	lastpos=pos;
	while(isalnum((*stringp)[pos]) ||
	      globusl_url_issafe((*stringp)[pos]) ||
	      globusl_url_isextra((*stringp)[pos]) ||
	      (*stringp)[pos] == ';' ||
	      (*stringp)[pos] == '?' ||
	      (*stringp)[pos] == '&' ||
	      (*stringp)[pos] == '=')
	{
	    pos++;
	}
	if((*stringp)[pos] == '\0')
	{
	    /* didn't find anything, but it's optional, anyway */
	    return GLOBUS_SUCCESS;
	}
	if((*stringp)[pos] == '%')
	{
	    pos++;
	    if(isxdigit((*stringp)[pos]))
	    {
		pos++;
		if(isxdigit((*stringp)[pos]))
		{
		    pos++;
		}
		else
		{
		    return GLOBUS_URL_ERROR_BAD_USER;
		}
	    }
	    else
	    {
		return GLOBUS_URL_ERROR_BAD_USER;
	    }
	}
    } while((*stringp)[pos] != '@' &&
	    (*stringp)[pos] != ':' &&
	    pos != lastpos);
    
    if(pos == startpos)
    {
	return GLOBUS_URL_ERROR_BAD_USER;
    }

    if((*stringp)[pos] == '@')
    {
	rc = globusl_url_get_substring(*stringp, user, pos);
	(*stringp) += pos+1;
	return rc;
    }
    else if((*stringp)[pos]==':')
    {
	rc = globusl_url_get_substring(*stringp, user, pos);
	
	if(rc != GLOBUS_SUCCESS)
	{
	    return rc;
	}
	(*stringp) += pos+1;	/* skip ':' */
	pos=0;
	startpos = pos;

	do
	{
	    lastpos=pos;
	    if(isalnum((*stringp)[pos]) ||
	       globusl_url_issafe((*stringp)[pos]) ||
	       globusl_url_isextra((*stringp)[pos]) ||
	       (*stringp)[pos] == ';' ||
	       (*stringp)[pos] == '?' ||
	       (*stringp)[pos] == '&' ||
	       (*stringp)[pos] == '=')
	    {
		pos++;
	    }

	    if((*stringp)[pos] == '\0')
	    {
		return GLOBUS_URL_ERROR_BAD_PASSWORD;
	    }

	    else if((*stringp)[pos] == '%')
	    {
		pos++;
		if(isxdigit((*stringp)[pos]))
		{
		    pos++;
		    if(isxdigit((*stringp)[pos]))
		    {
			pos++;
		    }
		    else
		    {
			return GLOBUS_URL_ERROR_BAD_PASSWORD;
		    }
		}
		else
		{
		    return GLOBUS_URL_ERROR_BAD_PASSWORD;
		}
	    }
	} while((*stringp)[pos] != '@' && pos != lastpos);
	if(pos == startpos)
	{
	    return GLOBUS_URL_ERROR_BAD_PASSWORD;
	}
	if((*stringp)[pos] == '@')
	{
	    rc = globusl_url_get_substring(*stringp,
					  password,
					  pos);
	    (*stringp) += pos+1;
	    return rc;
	}
    }
    else
    {
	return GLOBUS_SUCCESS;
    }
    return GLOBUS_SUCCESS;
}


/******************************************************************************
Function: globusl_url_get_host_port()

Description: look for a host:port in the specified string.
             NOTE: Does not ensure that the hostname is a valid RFC 1738

Parameters: 

Returns: GLOBUS_TRUE if in the character class, GLOBUS_FALSE otherwise
******************************************************************************/
static int
globusl_url_get_host_port(const char **stringp,
			 char **host,
			 unsigned short *port)
{
    int pos = 0;
    int startpos = 0;
    int rc;

    if(stringp == NULL || host == NULL || port == NULL)
    {
        return GLOBUS_URL_ERROR_INTERNAL_ERROR;
    }
    if(*stringp == NULL)
    {
        return GLOBUS_URL_ERROR_INTERNAL_ERROR;
    }

    *port = 0;
    
    if((*stringp)[pos] == '[')
    {
        (*stringp)++;
        while(isxdigit((*stringp)[pos]) ||
          (*stringp)[pos] == ':' ||
          (*stringp)[pos] == '.')
        {
            pos++;
        } 
    }
    else
    {
        while(isalnum((*stringp)[pos]) ||
          (*stringp)[pos] == '-' ||
          (*stringp)[pos] == '.')
        {
            pos++;
        } 
    }
    
    if(pos == startpos)
    {
	return GLOBUS_URL_ERROR_BAD_HOST;
    }
    
    if((*stringp)[pos] == ':' ||
       (*stringp)[pos] == '/' ||
       ((*stringp)[pos] == ']' && 
        ((*stringp)[pos + 1] == ':' || (*stringp)[pos + 1] == '/')))
    {
	char *tmp;
	rc = globusl_url_get_substring(*stringp, host, pos);
	if((*stringp)[pos] == ']') pos++;
	(*stringp) += pos;
	if(rc != GLOBUS_SUCCESS)
	{
	    return rc;
	}
	
	pos = 0;

	if((*stringp)[pos] == ':')
	{
	    (*stringp)++;
	    while(isdigit((*stringp)[pos]))
	    {
		pos++;
	    }
	    if(pos == 0)
	    {
		return GLOBUS_URL_ERROR_BAD_PORT;
	    }
	    
	    rc = globusl_url_get_substring(*stringp, &tmp, pos);
	    (*stringp) += pos++;
	    if(rc != GLOBUS_SUCCESS)
	    {
		return rc;
	    }
	    else
	    {
		*port = (unsigned short) atoi(tmp);
		free(tmp);
		return GLOBUS_SUCCESS;
	    }
	}
	else
	{
	    return GLOBUS_SUCCESS;
	}
    }
    else if((*stringp)[pos] == '\0' ||
        ((*stringp)[pos] == ']' && (*stringp)[pos + 1] == '\0'))
    {
	rc = globusl_url_get_substring(*stringp, host, pos);
	if((*stringp)[pos] == ']') pos++;
	(*stringp) += pos;
	return rc;
    }
    else
    {
	return GLOBUS_URL_ERROR_BAD_HOST;
    }
}

/******************************************************************************
Function: globusl_url_get_ldap_specific()

Description: Look for a properly formatted ldap scheme-specific information:
             namely <dn>?<attributes>?<scope>?<filter>
	     with all of the bits in <>'s legal URL characters

Parameters: 

Returns: GLOBUS_SUCCESS if the parsing turns out OK, GLOBUS_URL_ERROR_*
         otherwise
******************************************************************************/
static int
globusl_url_get_ldap_specific(const char **stringp,
					char **dn,
					char **attributes,
					char **scope,
					char **filter)
{
    int pos = 0;
    int lastpos;
    char ***which[4];
    int errs[4];
    int i;
    int rc;
    
    which[0] = &dn;
    which[1] = &attributes;
    which[2] = &scope;
    which[3] = &filter;
    errs[0] = GLOBUS_URL_ERROR_BAD_DN;
    errs[1] = GLOBUS_URL_ERROR_BAD_ATTRIBUTES;
    errs[2] = GLOBUS_URL_ERROR_BAD_SCOPE;
    errs[3] = GLOBUS_URL_ERROR_BAD_FILTER;

    for(i = 0; i < 4; i++)
    {
	pos = 0;
	do
	{
	    lastpos = pos;
	    if(isalnum((*stringp)[pos]) ||
	       globusl_url_isextra((*stringp)[pos]) ||
	       globusl_url_issafe((*stringp)[pos]) ||
	       (*stringp)[pos] == '=')
	    {
		pos++;
	    }
	    
	    if((*stringp)[pos] == '%')
	    {
		pos++;
		if(isxdigit((*stringp)[pos]))
		{
		    pos++;
		    if(isxdigit((*stringp)[pos]))
		    {
			pos++;
		    }
		    else
		    {
			return errs[i];
		    }
		}
		else
		{
		    return errs[i];
		}
	    }
	} while(pos != lastpos && (*stringp)[pos] != '?');

	if(pos == 0)
	{
	    return errs[i];
	}
	else
	{
	    if(((*stringp)[pos] == '?'  && i != 3) ||
	       ((*stringp)[pos] == '\0' && i == 3))
	    {
		rc = globusl_url_get_substring(*stringp, *(which[i]), pos);
		if(rc != GLOBUS_SUCCESS)
		{
		    return rc;
		}
		(*stringp) += pos+1;
	    }
	    else
	    {
		return errs[i];
	    }
	}	
    }
    return GLOBUS_SUCCESS;
}
	
/******************************************************************************
Function: globusl_url_get_file_specific()

Description: Look for properly formatted file scheme-specific information:
		/some/path
		//hostname/some/path
		(Note: only the second form is valid in the RFC definition
		of file URLS; however, the first and is used in common
		practice

Parameters: 

Returns: GLOBUS_SUCCESS if the parsing turns out OK, GLOBUS_URL_ERROR_*
         otherwise
******************************************************************************/
static int
globusl_url_get_file_specific(const char **stringp,
			      char ** host,
			      char ** path)
{
    int    rc;
    size_t pos = 0;

    while((*stringp)[pos] == '/')
    {
	pos++;
    }

    if(pos == 0)
    {
	return GLOBUS_URL_ERROR_BAD_PATH;
    }
    if(pos == 2)
    {
	(*stringp) += pos;

	pos = 0;
	/* Parse host name */
    #ifdef WIN32
	while(isalnum((*stringp)[pos]) ||
	      (*stringp)[pos] == '\\' ||
	      (*stringp)[pos] == ':' ||
	      (*stringp)[pos] == '-' ||
	      (*stringp)[pos] == '.')
    #else
	while(isalnum((*stringp)[pos]) ||
	      (*stringp)[pos] == '-' ||
	      (*stringp)[pos] == '.')
    #endif
	{
	    pos++;
	} 

    #ifdef WIN32
	if(((*stringp)[pos] == '\\' || (*stringp)[pos] == '/') && pos != 0)
    #else
	if((*stringp)[pos] == '/' && pos != 0)
    #endif
	{
	    rc = globusl_url_get_substring(*stringp, host, pos);
	    (*stringp) += pos;
	    if(rc != GLOBUS_SUCCESS)
	    {
		return rc;
	    }
	}
	pos = 0;
    }
    /* We've consumed the host name, now consume any leading /'s,
     * except for the last one */
    while((*stringp)[pos] == '/')
    {
	pos++;
    }

    if(pos > 1)
    {
	(*stringp) += (pos - 1);
    }
    pos = 0;

    #ifdef WIN32
    /* This is something of a hack. Rather than rewire lower level routines it
       does a simple check for windows file syntax here and returns success */
       
    /* verify "c:\" type syntax */
    if(strlen((*stringp)+pos) > 3 &&
        (*stringp)[pos] == '/' &&
        isalnum((*stringp)[pos+1]) && 
        (*stringp)[pos+2] == ':'  &&
        ((*stringp)[pos+3] == '\\' || (*stringp)[pos+3] == '/'))
    {
        char *temp_path;
        size_t i;
        temp_path = malloc(strlen(*stringp));
        strcpy(temp_path,(*stringp)+1);
        for(i = 0; temp_path[i]; i++)
        {
            if(temp_path[i] == '/') temp_path[i] = '\\';
        }
        *path = temp_path;
        return GLOBUS_SUCCESS;
    }
    /* verify "\" type syntax */
    else if((*stringp)[pos] == '\\' || (*stringp)[pos] == '/')
    {
        char *temp_path;
        size_t i;
        temp_path = malloc(strlen(*stringp) + 1);
        strcpy(temp_path,*stringp);
        for(i = 0;i < strlen(temp_path);i++)
        {
            if(temp_path[i] == '/') temp_path[i] = '\\';
        }
        *path = temp_path;
        return GLOBUS_SUCCESS;
    }
    else
    {
        rc = GLOBUS_URL_ERROR_BAD_PATH;
        return rc;
    }
    #endif

    if((*stringp)[pos] != '/')
    {
        rc = GLOBUS_URL_ERROR_BAD_PATH;
    }
    else
    {
        rc = globusl_url_get_path(stringp,
			          path,
				  GLOBUS_URL_SCHEME_FILE);
    }
    return rc;
}

/******************************************************************************
Function: globusl_url_get_file_specific_loose()

Description: Look for properly formatted file scheme-specific information:
		/some/path
		//hostname/some/path
		(Note: only the second form is valid in the RFC definition
		of file URLS; however, the first is used in common
		practice)
		loose restrictions on characters allowed for globbing purposes

Parameters: 

Returns: GLOBUS_SUCCESS if the parsing turns out OK, GLOBUS_URL_ERROR_*
         otherwise
******************************************************************************/
static int
globusl_url_get_file_specific_loose(const char **stringp,
			      char ** host,
			      char ** path)
{
    int    rc;
    size_t pos = 0;

    while((*stringp)[pos] == '/')
    {
	pos++;
    }

    if(pos == 0)
    {
	return GLOBUS_URL_ERROR_BAD_PATH;
    }
    if(pos == 2)
    {
	(*stringp) += pos;

	pos = 0;
	/* Parse host name */
    #ifdef WIN32
	while(isalnum((*stringp)[pos]) ||
	      (*stringp)[pos] == '\\' ||
	      (*stringp)[pos] == ':' ||
	      (*stringp)[pos] == '-' ||
	      (*stringp)[pos] == '.')
    #else
	while(isalnum((*stringp)[pos]) ||
	      (*stringp)[pos] == '-' ||
	      (*stringp)[pos] == '.')
    #endif

    #ifdef WIN32
	if(((*stringp)[pos] == '\\' || (*stringp)[pos] == '/') && pos != 0)
    #else
	if((*stringp)[pos] == '/' && pos != 0)
    #endif
	{
	    rc = globusl_url_get_substring(*stringp, host, pos);
	    (*stringp) += pos;
	    if(rc != GLOBUS_SUCCESS)
	    {
		return rc;
	    }
	}
	pos = 0;
    }
    /* We've consumed the host name, now consume any leading /'s,
     * except for the last one */
    while((*stringp)[pos] == '/')
    {
	pos++;
    }

    if(pos > 1)
    {
	(*stringp) += (pos - 1);
    }
    pos = 0;

    if((*stringp)[pos] != '/')
    {
        rc = GLOBUS_URL_ERROR_BAD_PATH;
    }
    else
    {
        rc = globusl_url_get_path_loose(stringp,
			          path,
				  GLOBUS_URL_SCHEME_FILE);
#if _WIN32
        if (rc == GLOBUS_SUCCESS)
        {
            size_t pathlen = strlen(*path);
            size_t i;
            /* If it's a file URL on Windows with a driver specifier,
             * remove the leading /.
             * Replace all "/" values with "\"
             */
            if ((pathlen > 3) &&
                isalpha((*path)[1]) &&
                (*path)[2] == ':')
            {
                size_t i;
                memmove(*path, &(*path)[1], pathlen);
                pathlen--;
            }

            for (i = 0; i < pathlen; i++)
            {
                if ((*path)[i] == '/')
                {
                    (*path)[i] = '\\';
                }
                else
                {
                    (*path)[i] = (*path)[i];
                }
            }
        }
#endif
    }
    return rc;
}

/******************************************************************************
Function: globusl_url_get_path()

Description: look for a path in the specified string.

Parameters: 

Returns: GLOBUS_TRUE if in the character class, GLOBUS_FALSE otherwise
******************************************************************************/
static int
globusl_url_get_path(const char **stringp,
		    char **url_path,
		    globus_url_scheme_t scheme_type)
{
    int rc;
    size_t pos = 0;
    size_t lastpos;
    
    do
    {
	lastpos = pos;
	if(isalnum((*stringp)[pos]) ||
	   globusl_url_issafe((*stringp)[pos]) ||
	   globusl_url_isextra((*stringp)[pos]) ||
	   globusl_url_isscheme_special((*stringp)[pos]) ||
	   (*stringp)[pos] == '~' || /* incorrect, but de facto */
	   (*stringp)[pos] == '/'||
	   (*stringp)[pos] == ' ') /* to be nice */
	{
	    pos++;
	}

	if((*stringp)[pos] == '%')
	{
	    pos++;
	    if(isxdigit((*stringp)[pos]))
	    {
		pos++;
		if(isxdigit((*stringp)[pos]))
		{
		    pos++;
		}
		else
		{
		    return GLOBUS_URL_ERROR_BAD_PATH;
		}
	    }
	    else
	    {
		return GLOBUS_URL_ERROR_BAD_PATH;
	    }
	}
    } while((*stringp)[pos] != '\0' &&
	    lastpos != pos);

    if(pos == 0)
    {
	return GLOBUS_SUCCESS;
    }
    if(pos != strlen(*stringp))
    {
	return GLOBUS_URL_ERROR_BAD_PATH;
    }

    /* reduce /~ to ~ if FTP */

    if((scheme_type == GLOBUS_URL_SCHEME_FTP ||
	scheme_type == GLOBUS_URL_SCHEME_GSIFTP ||
        scheme_type == GLOBUS_URL_SCHEME_SSHFTP) &&
	pos > 1 && **stringp == '/' && *(*stringp + 1) == '~')
    {
	*stringp = *stringp + 1;
    }

    rc = globusl_url_get_substring(*stringp, url_path, pos);

    return rc;
}

/******************************************************************************
Function: globusl_url_get_path_rfc1738()

Description: look for a path in the specified string.

Parameters: 

Returns: GLOBUS_TRUE if in the character class, GLOBUS_FALSE otherwise
******************************************************************************/
static int
globusl_url_get_path_rfc1738(const char **stringp,
		    char **url_path,
		    globus_url_scheme_t scheme_type)
{
    int rc;
    size_t pos = 0;
    size_t tmppos = 0;
    size_t lastpos;
    char * tmpbuf;

    tmpbuf=globus_malloc(strlen((*stringp)));
    if(tmpbuf == NULL)
    {
	return GLOBUS_URL_ERROR_NULL_STRING;
    }
    
    do
    {
	lastpos = pos;

	while((*stringp)[pos] == '/')
	{
	    /*if (pos>strcspn(*stringp, "/")) */
	    if (pos>0)
	    {
	        if ((*stringp)[pos]!=(*stringp)[pos-1])   /*no strings of / */
		{
		    tmpbuf[tmppos]=(*stringp)[pos];
		    pos++;
		    tmppos++;
		}
		else
		{
		    pos++;
		}
	    }
	    else
	    {
		pos++;
	    }
	}

	if(isalnum((*stringp)[pos]) ||
	   globusl_url_issafe((*stringp)[pos]) ||
	   globusl_url_isextra((*stringp)[pos]) ||
	   globusl_url_isscheme_special((*stringp)[pos]) ||
	   (*stringp)[pos] == '~' || /* incorrect, but de facto */
	   (*stringp)[pos] == ' ') /* to be nice */
	{
	    tmpbuf[tmppos]=(*stringp)[pos];
	    tmppos++;
	    pos++;
	}

	if((*stringp)[pos] == '%')
	{
	    tmpbuf[tmppos]=(*stringp)[pos];
	    tmppos++;
	    pos++;
	    if(isxdigit((*stringp)[pos]))
	    {
		tmpbuf[tmppos]=(*stringp)[pos];
		tmppos++;
		pos++;
		if(isxdigit((*stringp)[pos]))
		{
		    tmpbuf[tmppos]=(*stringp)[pos];
		    tmppos++;
		    pos++;
		}
		else
		{
		    return GLOBUS_URL_ERROR_BAD_PATH;
		}
	    }
	    else
	    {
		return GLOBUS_URL_ERROR_BAD_PATH;
	    }
	}
    } while((*stringp)[pos] != '\0' &&
	    lastpos != pos);
    
    tmpbuf[tmppos] = '\0';

    if(pos == 0)
    {
	return GLOBUS_SUCCESS;
    }
    if(pos != strlen(*stringp))
    {
	return GLOBUS_URL_ERROR_BAD_PATH;
    }

    rc = globusl_url_get_substring(tmpbuf, url_path, tmppos);

    free(tmpbuf);

    return rc;
}

/******************************************************************************
Function: globusl_url_get_path_loose()

Description: look for a path in the specified string, loose restrictions on its
             contents.

Parameters: 

Returns: GLOBUS_TRUE if in the character class, GLOBUS_FALSE otherwise
******************************************************************************/
static int
globusl_url_get_path_loose(const char **stringp,
		    char **url_path,
		    globus_url_scheme_t scheme_type)
{
    int rc;
    size_t pos = 0;
    size_t lastpos;
    
    do
    {
	lastpos = pos;
	if(isalnum((*stringp)[pos]) ||
	   globusl_url_issafe((*stringp)[pos]) ||
	   globusl_url_isextra((*stringp)[pos]) ||
	   globusl_url_isscheme_special((*stringp)[pos]) ||
	   globusl_url_isglob((*stringp)[pos]) ||
	   (*stringp)[pos] == '~' || /* incorrect, but de facto */
	   (*stringp)[pos] == ' ') /* to be nice */
	{
	    pos++;
	}

	if((*stringp)[pos] == '%')
	{
	    pos++;
	    if(isxdigit((*stringp)[pos]))
	    {
		pos++;
		if(isxdigit((*stringp)[pos]))
		{
		    pos++;
		}
		else
		{
		    return GLOBUS_URL_ERROR_BAD_PATH;
		}
	    }
	    else
	    {
		return GLOBUS_URL_ERROR_BAD_PATH;
	    }
	}
    } while((*stringp)[pos] != '\0' &&
	    lastpos != pos);

    if(pos == 0)
    {
	return GLOBUS_SUCCESS;
    }
    if(pos != strlen(*stringp))
    {
	return GLOBUS_URL_ERROR_BAD_PATH;
    }

    /* reduce /~ to ~ if FTP */

    if((scheme_type == GLOBUS_URL_SCHEME_FTP ||
	scheme_type == GLOBUS_URL_SCHEME_GSIFTP ||
        scheme_type == GLOBUS_URL_SCHEME_SSHFTP) &&
	pos > 1 && **stringp == '/' && *(*stringp + 1) == '~')
    {
	*stringp = *stringp + 1;
    }

    rc = globusl_url_get_substring(*stringp, url_path, pos);

    return rc;
}

/******************************************************************************
Function: globusl_url_issafe()

Description: predicate returns true if the specified character is in the
             'safe' character class from RFC 1738

Parameters: 

Returns: GLOBUS_TRUE if in the character class, GLOBUS_FALSE otherwise
******************************************************************************/
static int
globusl_url_issafe(char x)
{
    if(x == '$' ||
       x == '-' ||
       x == '_' ||
       x == '.' ||
       x == '+')
    {
	return GLOBUS_TRUE;
    }
    else
    {
	return GLOBUS_FALSE;
    }
}

/******************************************************************************
Function: globusl_url_issafe()

Description: predicate returns true if the specified character is in the
             'safe' character class from RFC 1738

Parameters: 

Returns: GLOBUS_TRUE if in the character class, GLOBUS_FALSE otherwise
******************************************************************************/
static int
globusl_url_isglob(char x)
{
    if(x == '*' ||
       x == '?' ||
       x == '[' ||
       x == ']' ||
       x == '{' ||
       x == '}' ||
       x == '!' ||
       x == '?' ||
       x == '=' ||
       x == ',' ||
       x == ':' ||
       x == '-')
    {
	return GLOBUS_TRUE;
    }
    else
    {
	return GLOBUS_FALSE;
    }
}

/******************************************************************************
Function: globusl_url_isextra()

Description: predicate returns true if the specified character is in the
             'extra' character class from RFC 1738

Parameters: 

Returns: GLOBUS_TRUE if in the character class, GLOBUS_FALSE otherwise
******************************************************************************/
static int
globusl_url_isextra(char x)
{

    if(x == '!' ||
       x == '*' ||
       x == '\'' ||
       x == '(' ||
       x == ')' ||
       x == ',')
    {
	return GLOBUS_TRUE;
    }
    else
    {
	return GLOBUS_FALSE;
    }
}

/******************************************************************************
Function: globusl_url_isscheme_special()

Description: predicate returns true if the specified character is in the
             'scheme special' character class from RFC 1738

Parameters: 

Returns: GLOBUS_TRUE if in the character class, GLOBUS_FALSE otherwise
******************************************************************************/
static int
globusl_url_isscheme_special(char x)
{
 
    if(x == ';' ||
       x == '/' ||
       x == '?' ||
       x == ':' ||
       x == '@' ||
       x == '=' ||
       x == '&')
    {
        return GLOBUS_TRUE;
    }
    else
    {
        return GLOBUS_FALSE;
    }
}

/**
 * Get the scheme of an URL.
 * @ingroup globus_url
 *
 * This function determines the scheme type of the url string, and populates
 * the variable pointed to by second parameter with that value.
 * This performs a less expensive parsing than globus_url_parse() and is
 * suitable for applications which need only to choose a handler based on
 * the URL scheme.
 *
 * @param url_string
 *        The string containing the URL.
 * @param scheme_type
 *        A pointer to a globus_url_scheme_t which will be set to
 *        the scheme.
 *
 * @retval GLOBUS_SUCCESS
 *         The URL scheme was recognized, and scheme_type has been updated.
 * @retval GLOBUS_URL_ERROR_BAD_SCHEME
 *         The URL scheme was not recognized.
 */
int
globus_url_get_scheme(const char *url_string, globus_url_scheme_t *scheme_type)
{

    if(strncmp(url_string, "ftp:", 4) == 0)
    {
	*scheme_type = GLOBUS_URL_SCHEME_FTP;
    }
    else if(strncmp(url_string, "gsiftp:", 7) == 0)
    {
	*scheme_type = GLOBUS_URL_SCHEME_GSIFTP;
    }
    else if(strncmp(url_string, "sshftp:", 7) == 0)
    {
	*scheme_type = GLOBUS_URL_SCHEME_SSHFTP;
    }
    else if(strncmp(url_string, "http:", 5) == 0)
    {
	*scheme_type = GLOBUS_URL_SCHEME_HTTP;
    }
    else if(strncmp(url_string, "https:", 5) == 0)
    {
	*scheme_type = GLOBUS_URL_SCHEME_HTTPS;
    }
    else if(strncmp(url_string, "ldap:", 5) == 0)
    {
	*scheme_type = GLOBUS_URL_SCHEME_LDAP;
    }
    else if(strncmp(url_string, "file:", 5) == 0)
    {
	*scheme_type = GLOBUS_URL_SCHEME_FILE;
    }
    else if(strncmp(url_string, "x-nexus:", 8) == 0)
    {
	*scheme_type = GLOBUS_URL_SCHEME_X_NEXUS;
    }
    else if(strncmp(url_string, "x-gass-cache:", 13) == 0)
    {
	*scheme_type = GLOBUS_URL_SCHEME_X_GASS_CACHE;
    }
    else
    {
	*scheme_type = GLOBUS_URL_SCHEME_UNKNOWN;
        return GLOBUS_URL_ERROR_BAD_SCHEME;
    }

    return GLOBUS_SUCCESS;
}
/* globus_url_get_scheme() */

#define COPY_FIELD(x,prev) \
    if(src->x && !(dst->x = globus_libc_strdup(src->x))) { goto free_##prev; }


#define FREE_EXIT(x) \
    free_##x: if(dst->x) { globus_libc_free(dst->x); }


/**
 * Create a copy of an URL structure.
 * @ingroup globus_url
 *
 * This function copies the contents of a url structure into another.
 *
 * @param dst
 *        The URL structure to be populated with a copy of the contents
 *        of src.
 * @param src
 *        The original URL.
 *
 * @retval GLOBUS_SUCCESS
 *         The URL was successfully copied.
 * @retval GLOBUS_URL_ERROR_NULL_URL
 *         One of the URLs was GLOBUS_NULL.
 * @retval GLOBUS_URL_ERROR_OUT_OF_MEMORY;
 *         The library was unable to allocate memory to create the
 *         the globus_url_t contents.
 */
int
globus_url_copy(
    globus_url_t *				dst,
    const globus_url_t *			src)
{
    if(src == NULL)
    {
	return GLOBUS_URL_ERROR_NULL_URL;
    }
    if(dst == NULL)
    {
	return GLOBUS_URL_ERROR_NULL_URL;
    }
    memset(dst, '\0', sizeof(globus_url_t));
    
    dst->scheme_type = src->scheme_type;
    dst->port = src->port;

    if(src->scheme)
    {
	if(!(dst->scheme = globus_libc_strdup(src->scheme)))
	{
	    goto error_exit;
	}
    }

    COPY_FIELD(user, scheme)
    COPY_FIELD(password, user)
    COPY_FIELD(host, password)
    COPY_FIELD(url_path, host)
    COPY_FIELD(dn, url_path)
    COPY_FIELD(attributes, dn)
    COPY_FIELD(scope, attributes)
    COPY_FIELD(filter, scope)
    COPY_FIELD(url_specific_part, filter)
    
    return GLOBUS_SUCCESS;

    FREE_EXIT(filter)
    FREE_EXIT(scope)
    FREE_EXIT(attributes)
    FREE_EXIT(dn)
    FREE_EXIT(url_path)
    FREE_EXIT(host)
    FREE_EXIT(password)
    FREE_EXIT(user)
    FREE_EXIT(scheme)
    
 error_exit:
    memset(dst, '\0', sizeof(globus_url_t));
    return GLOBUS_URL_ERROR_OUT_OF_MEMORY;
}
/* globus_url_copy() */


char *
globus_url_string_hex_encode(
    const char *                        s,
    const char *                        enc)
{
    char *                              e;
    char *                              encoded;
    static char                         hexchars[] = "0123456789ABCDEF";

    if(!s)
    {
        return NULL;
    }

    if(!enc)
    {
        return globus_libc_strdup(s);
    }


    encoded = (char *) globus_malloc((3 * strlen(s) + 1) * sizeof(char));
    if(encoded)
    {
        e = encoded;
        while(*s)
        {
            unsigned char               n = (unsigned char) *s;

            if(n <= 0x1F || n >= 0x7F || *s == '%' || (*enc && strchr(enc, n)))
            {
                *(e++) = '%';
                *(e++) = hexchars[n >> 4];
                *(e++) = hexchars[n & 15];
                s++;
            }
            else
            {
                *(e++) = *(s++);
            }
        }

        *e = 0;
    }

    return encoded;
}

void
globus_url_string_hex_decode(
    char *                              s)
{
    char *                              d;
    char                                t;

    if(!s || !(s = strchr(s, '%')))
    {
        return;
    } 
    d = s;

    while(*s)
    {
        t = *s;
        if(t == '%')
        {
            if(*(s + 1) == '%')
            {
                s++;
            }
            else if(isxdigit(*(s + 1)) && isxdigit(*(s + 2)))
            {
                char                    hexstring[3];
                
                hexstring[0] = *(++s);
                hexstring[1] = *(++s);
                hexstring[2] = 0;

                t = (char) strtol(hexstring, NULL, 16);
            }
        }

        *d = t;
        s++;
        d++;
    }

    *d = 0;
}

