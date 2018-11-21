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

#ifndef GLOBUS_URL_H
#define GLOBUS_URL_H

/**
 * @file globus_url.h
 * @brief URL Parsing
 */

/**
 * @defgroup globus_url URL String Parser
 * @ingroup globus_common
 * @brief URL String Parser
 *
 * The Globus URL functions provide a simple mechanism for parsing
 * a URL string into a data structure, and for determining the scheme
 * of an URL string.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Supported URL Schemes */
/**
 * @brief URL Schemes.
 * @ingroup globus_url
 *
 * The Globus URL library supports a set of URL schemes (protocols). This
 * enumeration can be used to quickly dispatch a parsed URL based on a
 * constant value.
 *
 * @see globus_url_t::scheme_type
 */
typedef enum
{
    /** File Transfer Protocol */
    GLOBUS_URL_SCHEME_FTP=0,
    /** GSI-enhanced File Transfer Protocol */
    GLOBUS_URL_SCHEME_GSIFTP,
    /** HyperText Transfer Protocol */
    GLOBUS_URL_SCHEME_HTTP,
    /** Secure HyperText Transfer Protocol */
    GLOBUS_URL_SCHEME_HTTPS,
    /** Lightweight Directory Access Protocol*/
    GLOBUS_URL_SCHEME_LDAP,
    /** File Location */
    GLOBUS_URL_SCHEME_FILE,
    /** Nexus endpoint */
    GLOBUS_URL_SCHEME_X_NEXUS,
    /** GASS Cache Entry */
    GLOBUS_URL_SCHEME_X_GASS_CACHE,
    /** Any other URL of the form <b>scheme</b>://<b>something</b> */
    GLOBUS_URL_SCHEME_UNKNOWN,
    GLOBUS_URL_SCHEME_SSHFTP,
    /** Total number of URL schemes supported */
    GLOBUS_URL_NUM_SCHEMES
} globus_url_scheme_t;

/*
  Other schemes defined in RFCs but _not_ supported here are 
 'news', 'nntp', 'telnet', 'gopher', 'wais', 'mailto', and 'prospero'
*/

/**
 * @brief Parsed URLs.
 * @ingroup globus_url
 *
 * This structure contains the fields which were parsed from an string
 * representation of an URL. There are no methods to access fields of this
 * structure. 
 */
typedef struct
{
    /** A string containing the URL's scheme (http, ftp, etc) */
    char *scheme;

    /** An enumerated scheme type. This is derived from the scheme string */
    globus_url_scheme_t scheme_type;


    /*
     * Other fields as seen in these known url schemes:
     *
     * ftp://[user[:password]@]host[:port]/[url_path]
     * gsiftp://[user[:password]@]host[:port]/[url_path]
     * http://host[:port]/url_path
     * x-nexus://host:port
     * x-gass-cache://url_path
     * ldap://host[:port]/dn?attributes?scope?filter
     * otherurl://host[:port]/url_path or
     * otherurl://url_specific_part
     */

    char *user;		/**< The username portion of the URL. [ftp, gsiftp] */
    char *password;	/**< The user's password from the URL. [ftp, gsiftp] */
    char *host;		/**< The host name or IP address of the URL.
			     [ftp, gsiftp, http, https, ldap, x-nexus */
    unsigned short port;/**< The TCP port number of the service providing the
			     URL [ftp, gsiftp, http, https, ldap, x-nexus] */
    char *url_path;	/**< The path name of the resource on the service
			     providing the URL. [ftp, gsiftp, http, https]  */
    char *dn;		/**< The distinguished name for the base of an LDAP
			     search. [ldap] */
    char *attributes;	/**< The list of attributes which should be returned
			     from an LDAP search. [ldap] */
    char *scope;	/**< The scope of an LDAP search. [ldap] */
    char *filter;	/**< The filter to be applied to an LDAP search
			     [ldap] */
    
    char *url_specific_part;
			/**< An unparsed string containing the remaining text
			     after the optional host and port of an unknown
			     URL, or the contents of a x-gass-cache URL
			     [x-gass-cache, unknown] */
} globus_url_t;

/* Fill in the data structure pointed to by url */
int globus_url_parse(const char *url_string, globus_url_t *url);

/* Fill in the data structure pointed to by url */
int globus_url_parse_rfc1738(const char *url_string, globus_url_t *url);

/* Fill in the data structure pointed to by url */
int globus_url_parse_loose(const char *url_string, globus_url_t *url);

/* Destroy the fields of the data structure pointed to by url */
int globus_url_destroy(globus_url_t *url);

/* Create a copy of a globus_url_t structure */
int globus_url_copy(globus_url_t * dest, const globus_url_t * src);

/* Find out the URL scheme type */
int globus_url_get_scheme(const char *url_string,
			  globus_url_scheme_t *scheme_type);


void
globus_url_string_hex_decode(
    char *                              s);

char *
globus_url_string_hex_encode(
    const char *                        s,
    const char *                        enc);

/* Return conditions */
#define GLOBUS_URL_SUCCESS 0
#define GLOBUS_URL_ERROR_NULL_STRING -1
#define GLOBUS_URL_ERROR_NULL_URL -2
#define GLOBUS_URL_ERROR_BAD_SCHEME -3
#define GLOBUS_URL_ERROR_BAD_USER -4
#define GLOBUS_URL_ERROR_BAD_PASSWORD -5
#define GLOBUS_URL_ERROR_BAD_HOST -6
#define GLOBUS_URL_ERROR_BAD_PORT -7
#define GLOBUS_URL_ERROR_BAD_PATH -8

/* for ldap URLs */
#define GLOBUS_URL_ERROR_BAD_DN -9
#define GLOBUS_URL_ERROR_BAD_ATTRIBUTES -10
#define GLOBUS_URL_ERROR_BAD_SCOPE -11
#define GLOBUS_URL_ERROR_BAD_FILTER -12

/* when malloc fails */
#define GLOBUS_URL_ERROR_OUT_OF_MEMORY -13

/* for nexus errors/former assertion failures */
#define GLOBUS_URL_ERROR_INTERNAL_ERROR -14

#define GLOBUS_URL_TRUE 1
#define GLOBUS_URL_FALSE 0


#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_URL_H */
