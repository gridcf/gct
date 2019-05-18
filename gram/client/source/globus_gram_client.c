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
 * @file globus_gram_client.c
 * @brief Resource Managemant Client APIs
 * 
 * This file contains the GRAM Client API functions.
 * This API provides functions for submitting a job request to a GRAM resource,
 * checking status, cancelling a job and requesting notification of state
 * changes for a request.
 */

/*
 * Include header files
 */
#include "globus_config.h"
#include "globus_i_gram_client.h"
#include "globus_gram_protocol.h"
#include "globus_io.h"
#include "globus_rsl.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "version.h"

typedef enum globus_l_gram_client_callback_type_e
{
    GLOBUS_GRAM_CLIENT_JOB_REQUEST,
    GLOBUS_GRAM_CLIENT_PING,
    GLOBUS_GRAM_CLIENT_STATUS,
    GLOBUS_GRAM_CLIENT_SIGNAL,
    GLOBUS_GRAM_CLIENT_CANCEL,
    GLOBUS_GRAM_CLIENT_CALLBACK_REGISTER,
    GLOBUS_GRAM_CLIENT_CALLBACK_UNREGISTER,
    GLOBUS_GRAM_CLIENT_RENEW,
    GLOBUS_GRAM_CLIENT_JOBMANAGER_VERSION
}
globus_l_gram_client_callback_type_t;

typedef struct globus_l_gram_client_monitor_s
{
    globus_mutex_t                      mutex;
    globus_cond_t                       cond;
    globus_gram_protocol_handle_t       handle;
    globus_l_gram_client_callback_type_t
                                        type;
    volatile globus_bool_t              done;

    globus_gram_client_job_info_t *     info;

    globus_gram_client_nonblocking_func_t
                                        callback;
    globus_gram_client_info_callback_func_t
                                        info_callback;
    void *                              callback_arg;
} globus_l_gram_client_monitor_t;

typedef struct globus_l_gram_client_callback_info_s
{
    globus_gram_client_callback_func_t  callback;
    globus_gram_client_info_callback_func_t
                                        info_callback;
    void *                              callback_arg;
    char *                              callback_contact;
}
globus_l_gram_client_callback_info_t;

static
int
globus_l_gram_client_parse_gatekeeper_contact(
    const char *                        contact_string,
    const char *                        service_prefix,
    const char *                        username,
    char **                             gatekeeper_url,
    char **                             gatekeeper_dn);

static int 
globus_l_gram_client_setup_gatekeeper_attr(
    globus_io_attr_t *                     attrp,
    gss_cred_id_t                          credential,
    globus_io_secure_delegation_mode_t     delegation_mode,
    char *                                 gatekeeper_dn );

static int
globus_l_gram_client_setup_jobmanager_attr(
    globus_io_attr_t *                      attr,
    gss_cred_id_t                           credential);


static
int
globus_l_gram_client_job_request(
    const char *                        resource_manager_contact,
    const char *                        description,
    int                                 job_state_mask,
    globus_i_gram_client_attr_t *       iattr,
    const char *                        callback_contact,
    globus_l_gram_client_monitor_t *    monitor);

static
int 
globus_l_gram_client_ping(
    const char *                        resource_manager_contact,
    globus_i_gram_client_attr_t *       iattr,
    globus_l_gram_client_monitor_t *    monitor);

static
int 
globus_l_gram_client_get_jobmanager_version(
    const char *                        resource_manager_contact,
    globus_i_gram_client_attr_t *       iattr,
    globus_l_gram_client_monitor_t *    monitor);

static
int
globus_l_gram_client_job_refresh_credentials(
    char *                              job_contact,
    gss_cred_id_t                       creds,
    globus_i_gram_client_attr_t *       iattr,
    globus_l_gram_client_monitor_t *    monitor);

static
void
globus_l_gram_client_callback(
    void *                              arg,
    globus_gram_protocol_handle_t       handle,
    globus_byte_t *                     buf,
    globus_size_t                       nbytes,
    int                                 errorcode,
    char *                              uri);

static
void
globus_l_gram_client_monitor_callback(
    void *                              user_arg,
    globus_gram_protocol_handle_t       handle,
    globus_byte_t *                     message,
    globus_size_t                       msgsize,
    int                                 errorcode,
    char *                              uri);

static
void
globus_l_gram_client_register_callback(
    void *                              user_arg,
    globus_gram_protocol_handle_t       handle,
    globus_byte_t *                     message,
    globus_size_t                       msgsize,
    int                                 errorcode,
    char *                              uri);

static
int
globus_l_gram_client_monitor_init(
    globus_l_gram_client_monitor_t *    monitor,
    globus_gram_client_job_info_t *     info,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    globus_gram_client_info_callback_func_t
                                        info_callback,
    void *                              callback_arg);

static
int
globus_l_gram_client_monitor_destroy(
    globus_l_gram_client_monitor_t *    monitor);

int
globus_i_gram_client_deactivate(void);

int
globus_i_gram_client_activate(void);

static
int
globus_l_gram_info_get_int(
    globus_hashtable_t *                extensions,
    const char *                        key);

/******************************************************************************
                       Define module specific variables
******************************************************************************/

globus_module_descriptor_t globus_gram_client_module = 
{
    "globus_gram_client",
    globus_i_gram_client_activate,
    globus_i_gram_client_deactivate,
    NULL,
    NULL,
    &local_version
};

static FILE *                           globus_l_print_fp;
static int                              globus_l_is_initialized = 0;
static globus_hashtable_t               globus_l_gram_client_contacts;

static globus_mutex_t                   globus_l_mutex;
static globus_mutex_t                   globus_l_rsl_mutex;

#define GLOBUS_L_CHECK_IF_INITIALIZED assert(globus_l_is_initialized==1)

/*
 * globus_i_gram_client_activate()
 * Description: Initialize variables
 * Call authorization routine for password entry.
 */
int
globus_i_gram_client_activate(void)
{
    int rc;
    
    rc = globus_module_activate(GLOBUS_POLL_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        return(rc);
    }

    rc = globus_module_activate(GLOBUS_IO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        return(rc);
    }

    rc = globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        return(rc);
    }

    rc = globus_module_activate(GLOBUS_GRAM_PROTOCOL_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        return(rc);
    }

    
    if ( globus_l_is_initialized == 0 )
    {
        /* initialize mutex which makes the client thread-safe */
        int err;
        err = globus_mutex_init (&globus_l_mutex, NULL);
        assert (!err);

        err = globus_mutex_init (&globus_l_rsl_mutex, NULL);
        assert (!err);

        globus_l_is_initialized = 1;

    }
    
    globus_l_print_fp = NULL;
    globus_hashtable_init(&globus_l_gram_client_contacts,
                          16,
                          globus_hashtable_string_hash,
                          globus_hashtable_string_keyeq);


    return 0;
} /* globus_i_gram_client_activate() */


/*
 * globus_i_gram_client_deactivate()
 */
int
globus_i_gram_client_deactivate(void)
{
    int rc;

    if ( globus_l_is_initialized == 0 )
    {
        return(GLOBUS_FAILURE);
    }
    else
    {
        globus_l_is_initialized = 0;
    }

    /* 
     * this will free any allocated space, but not malloc any new
     */
    globus_gram_protocol_error_7_hack_replace_message((const char*) NULL);
    
    rc = globus_module_deactivate(GLOBUS_GRAM_PROTOCOL_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        return(rc);
    }

    rc = globus_module_deactivate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        return(rc);
    }

    rc = globus_module_deactivate(GLOBUS_IO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        return(rc);
    }

    rc = globus_module_deactivate(GLOBUS_POLL_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        return(rc);
    }
    globus_hashtable_destroy(&globus_l_gram_client_contacts);

    return (GLOBUS_SUCCESS);
} /* globus_i_gram_client_deactivate() */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */



/**
 * @brief Enable GRAM debugging
 * @ingroup globus_gram_client
 *
 * @details
 * The globus_gram_client_debug() function enables
 * the displaying of internal GRAM debug messages to standard output. Most
 * of the information printed by this debugging system is related to errors
 * that occur during GRAM Client API functions. The messages printed to
 * standard output are not structured in any way.
 *
 * @return void
 */
void
globus_gram_client_debug(void)
{
    globus_l_print_fp = stdout;
    globus_libc_fprintf(globus_l_print_fp,
                  "globus_gram_client: debug messages will be printed.\n");
} /* globus_gram_client_debug() */


#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
static
int
globus_l_gram_client_parse_gatekeeper_contact(
    const char *                        contact_string,
    const char *                        service_prefix,
    const char *                        username,
    char **                             gatekeeper_url,
    char **                             gatekeeper_dn)
{
    char *                              duplicate;
    char *                              host = NULL;
    char *                              port = NULL;
    char *                              dn = NULL;
    char *                              service;
    int                                 got_port = 0;
    int                                 got_service = 0;
    char *                              ptr;
    unsigned short                      iport;
    globus_url_t                        some_struct;
    int                                 rc = GLOBUS_SUCCESS;

    /*
     *  the gatekeeper contact format: [https://]<host>:<port>[/<service>]:<dn>
     */    

    service = "jobmanager";
    iport = 2119;

    if ((duplicate = strdup(contact_string)))
    {
        host = duplicate;

        if (strncmp(duplicate,"https://", 8) == 0)
            host += 8;

        dn = host;

        for (ptr = duplicate; *ptr != '\0'; ptr++)
        {
            if ( *ptr == ':' )
            {
                got_port = 1;
                *ptr++ = '\0';
                port = ptr;
                break;
            }
            if ( *ptr == '/' )
            {
                got_service = 1;
                *ptr++ = '\0';
                service = ptr;
                break;
            }
        }

        if (got_port || got_service) 
        {
            if ((dn = strchr(ptr, ':')))
            {
                *dn++ = '\0';
            }

            if (got_port)
            {
                if ((service = strchr(port,'/')) != NULL)
                {
                    if ((service - port) > 1)
                    {
                        iport = (unsigned short) atoi(port);
                    }
                    *service++ = '\0';
                }
                else
                {
                    service = "jobmanager";
                    if (strlen(port) > 0)
                       iport = (unsigned short) atoi(port);
                }
            }
        }
        else
        {
            dn = NULL;
        }
    } 
    else 
    {
        if(globus_l_print_fp)
        {
            globus_libc_fprintf(globus_l_print_fp,
                                "strdup failed for contact_string\n");
        }
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto error_exit;
    }
    
    if (! *host)
    {
        free(duplicate);
        if(globus_l_print_fp)
        {
            globus_libc_fprintf(globus_l_print_fp,
                                "empty host value in contact_string\n");
        }
       return(GLOBUS_GRAM_PROTOCOL_ERROR_BAD_GATEKEEPER_CONTACT);
    }

    (*gatekeeper_url) = malloc(11 /* https://:/\0 */ +
                                           strlen(host) +
                                           5 + /*unsigned short*/
                                           strlen(service) +
                                           ((service_prefix != NULL)
                                               ? strlen(service_prefix)
                                               : 0) +
                                           ((username != NULL)
                                               ? strlen(username) + 1 : 0));

    if ((*gatekeeper_url) == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto free_duplicate_exit;
    }
    globus_libc_sprintf((*gatekeeper_url),
                        "https://%s:%hu%s/%s%s%s",
                        host,
                        (unsigned short) iport,
                        ((service_prefix != NULL) ? service_prefix : ""),
                        service,
                        (username != NULL) ? "@" : "",
                        (username != NULL) ? username : "");

    if (globus_url_parse(*gatekeeper_url, &some_struct) != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_GATEKEEPER_CONTACT;

        goto free_gatekeeper_url_exit;
    }
    globus_url_destroy(&some_struct);

    if ((dn) && (*dn))
    {
        *gatekeeper_dn = strdup(dn);

        if((*gatekeeper_dn) == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            goto free_gatekeeper_url_exit;
        }
    }
    else
    {
        *gatekeeper_dn = NULL;
    }
    free(duplicate);

    return rc;

free_gatekeeper_url_exit:
    free(*gatekeeper_url);
free_duplicate_exit:
    free(duplicate);
error_exit:

    return rc;
}


/*
 * globus_l_gram_client_setup_gatekeeper_attr()
 */
static int 
globus_l_gram_client_setup_gatekeeper_attr(
    globus_io_attr_t *                     attrp,
    gss_cred_id_t                          credential,
    globus_io_secure_delegation_mode_t     delegation_mode,
    char *                                 gatekeeper_dn )
{
    globus_result_t                        res;
    globus_io_secure_authorization_data_t  auth_data;

    res = globus_io_secure_authorization_data_initialize(&auth_data);
    if (res != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }
    res = globus_io_tcpattr_init(attrp);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_1;
    }
    res = globus_io_attr_set_socket_keepalive(attrp, GLOBUS_TRUE);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_2;
    }
    
    res = globus_io_attr_set_secure_authentication_mode(
        attrp,
        GLOBUS_IO_SECURE_AUTHENTICATION_MODE_MUTUAL,
        (credential != GSS_C_NO_CREDENTIAL)
            ? credential 
            : globus_i_gram_protocol_credential);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_2;
    }
    res = globus_io_attr_set_tcp_allow_ipv6(
        attrp,
        GLOBUS_TRUE);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_2;
    }

    if(gatekeeper_dn)
    {
        res = globus_io_secure_authorization_data_set_identity(
            &auth_data,
            gatekeeper_dn);
        if (res != GLOBUS_SUCCESS)
        {
            goto destroy_2;
        }
    }
    res = globus_io_attr_set_secure_authorization_mode(
        attrp,
        gatekeeper_dn 
            ? GLOBUS_IO_SECURE_AUTHORIZATION_MODE_IDENTITY 
            : GLOBUS_IO_SECURE_AUTHORIZATION_MODE_HOST,
        &auth_data);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_2;
    }
    res = globus_io_attr_set_secure_delegation_mode(
        attrp,
        delegation_mode);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_2;
    }
    res = globus_io_attr_set_secure_channel_mode(
        attrp,
        GLOBUS_IO_SECURE_CHANNEL_MODE_GSI_WRAP);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_2;
    }

    globus_io_secure_authorization_data_destroy(&auth_data);

    return GLOBUS_SUCCESS;
    
destroy_2:
    globus_io_tcpattr_destroy(attrp);
destroy_1:
    globus_io_secure_authorization_data_destroy(&auth_data);
error_exit:
    if(globus_l_print_fp)
    {
        globus_libc_fprintf(globus_l_print_fp, 
            "setting up IO attributes failed\n");
    }
    globus_object_free(globus_error_get(res));
    
    return GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
} /* globus_l_gram_client_setup_gatekeeper_attr() */

static int
globus_l_gram_client_setup_jobmanager_attr(
    globus_io_attr_t *                      attr,
    gss_cred_id_t                           credential)
{
    globus_result_t                        res;
    globus_io_secure_authorization_data_t  auth_data;

    res = globus_io_secure_authorization_data_initialize(&auth_data);
    if (res != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }
    res = globus_io_tcpattr_init(attr);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_1;
    }
    res = globus_io_attr_set_socket_keepalive(attr, GLOBUS_TRUE);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_2;
    }
    res = globus_io_attr_set_secure_authentication_mode(
        attr,
        GLOBUS_IO_SECURE_AUTHENTICATION_MODE_MUTUAL,
        (credential != GSS_C_NO_CREDENTIAL)
            ? credential
            : globus_i_gram_protocol_credential);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_2;
    }
    res = globus_io_attr_set_tcp_allow_ipv6(
        attr,
        GLOBUS_TRUE);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_2;
    }
    res = globus_io_attr_set_secure_authorization_mode(
        attr,
        GLOBUS_IO_SECURE_AUTHORIZATION_MODE_SELF,
        &auth_data);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_2;
    }
    res = globus_io_attr_set_secure_channel_mode(
        attr,
        GLOBUS_IO_SECURE_CHANNEL_MODE_SSL_WRAP);
    if (res != GLOBUS_SUCCESS)
    {
        goto destroy_2;
    }

    globus_io_secure_authorization_data_destroy(&auth_data);

    return GLOBUS_SUCCESS;
    
destroy_2:
    globus_io_tcpattr_destroy(attr);
destroy_1:
    globus_io_secure_authorization_data_destroy(&auth_data);
error_exit:
    globus_object_free(globus_error_get(res));
    
    return GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
} /* globus_l_gram_client_setup_jobmanager_attr() */
#endif

/**
 * @brief Return GRAM protocol version
 * @ingroup globus_gram_client
 *
 * @details
 * The globus_gram_client_version() function returns the version of the
 * GRAM protocol understood by this implementation.
 *
 * @return The integer protocol revision.
 */
int 
globus_gram_client_version(void)
{
    return(GLOBUS_GRAM_PROTOCOL_VERSION);

} /* globus_gram_client_version() */


/**
 * @brief Set the default GRAM credential
 * @ingroup globus_gram_client
 *
 * @details
 * The globus_gram_client_set_credentials() function causes subsequent
 * GRAM operations to use the GSSAPI credential @a new_credentials.
 * These operations include job requests, job signals, callback registration,
 * and job state callbacks. After this function returns, the caller must not
 * use the credential, as it may be freed by GRAM when it is no longer needed.
 *
 * @param new_credentials
 *     New GSSAPI credential to use.
 *
 * @return
 *     Upon success, globus_gram_client_set_credentials() returns
 *     GLOBUS_SUCCESS. There are no error values returned by this fucntion.
 * 
 * @retval GLOBUS_SUCCESS
 *     Success
 */
int
globus_gram_client_set_credentials(gss_cred_id_t new_credentials)
{
    return globus_gram_protocol_set_credentials(new_credentials);
}

/**
 * @brief Send a ping request to a GRAM service
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_register_ping() function sends a
 * specially-formatted GRAM protocol message which checks to
 * see if a Gatekeeper is running on a given PORT, and whether that
 * Gatekeeper is configured to support the desired job manager service.
 * This is used for diagnostic purposes.
 *
 * If globus_gram_client_register_ping() determines that this request could
 * not be processed before contacting the gatekeeper (for example, a malformed
 * @a resource_manager_contact),  it will return an error, and the 
 * @a regiser_callback function will not be called. Otherwise, the success or
 * failure an be determined by the @a operation_failure_code parameter to
 * the function pointed to by the @a register_callback parameter.
 *
 * @param resource_manager_contact
 *     A NULL-terminated character string containing a
 *     @link globus_gram_resource_manager_contact GRAM contact@endlink that
 *     this function will contact.
 * @param attr
 *     A set of client attributes to use to contact the gatekeeper. If no
 *     custom attributes are needed, the caller should pass the value 
 *     @a GLOBUS_GRAM_CLIENT_NO_ATTR.
 * @param register_callback
 *     A pointer to a function to call when the ping request has completed or
 *     failed.
 * @param register_callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a register_callback as its @a user_callback_arg
 *     parameter.
 *
 * @return
 *     Upon success, globus_gram_client_register_ping() returns
 *     GLOBUS_SUCCESS and the @a register_callback function will be called once
 *     the ping operation completes. If an error occurs, this function returns
 *     an integer error code and the function pointed to by the
 *     @a register_callback parameter will not be called.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int 
globus_gram_client_register_ping(
    const char *                        resource_manager_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg)
{
    globus_i_gram_client_attr_t *       iattr = NULL;
    globus_l_gram_client_monitor_t *    monitor;
    int                                 rc;

    monitor = malloc(sizeof(globus_l_gram_client_monitor_t));

    if(!monitor)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }
    iattr = (globus_i_gram_client_attr_t *) attr;

    globus_l_gram_client_monitor_init(
            monitor,
            NULL,
            register_callback,
            NULL,
            register_callback_arg);

    rc = globus_l_gram_client_ping(
            resource_manager_contact,
            iattr,
            monitor);

    if (rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(monitor);
        free(monitor);

    }
    return rc;
}
/* globus_gram_client_register_ping() */

/**
 * @brief Send a ping request to a GRAM service
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_ping() function sends a
 * specially-formatted GRAM protocol message which checks to
 * see if a Gatekeeper is running on a given PORT, and whether that
 * Gatekeeper is configured to support the desired job manager service.
 * This is used for diagnostic purposes.
 *
 * @param resource_manager_contact
 *     A NULL-terminated character string containing a
 *     @link globus_gram_resource_manager_contact GRAM contact@endlink that
 *     this function will contact.
 *
 * @return
 *     Upon success, globus_gram_client_ping() contacts the gatekeeper
 *     service and returns @a GLOBUS_SUCCESS.  If an error occurs, this
 *     function returns an integer error code.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int 
globus_gram_client_ping(
    const char *                        resource_manager_contact)
{
    int                                 rc;
    globus_l_gram_client_monitor_t      monitor;

    globus_l_gram_client_monitor_init(&monitor, NULL, NULL, NULL, NULL);

    rc = globus_l_gram_client_ping(
            resource_manager_contact,
            NULL,
            &monitor);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(&monitor);

        return rc;
    }

    globus_mutex_lock(&monitor.mutex);
    while (!monitor.done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }
    rc = monitor.info->protocol_error_code;

    globus_mutex_unlock(&monitor.mutex);

    globus_l_gram_client_monitor_destroy(&monitor);

    return rc;
}
/* globus_gram_client_ping() */

/**
 * @brief Get version information from a job manager
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_get_jobmanager_version() function sends a message
 * to a GRAM service which returns information about the job manager version
 * in the value pointed to by the @a extensions parameter. Note that job
 * managers prior to GT5 do not support the version request and so will return
 * a GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED error. This function blocks
 * while processing this request.
 *
 * @param resource_manager_contact
 *     A NULL-terminated character string containing a
 *     @link globus_gram_resource_manager_contact GRAM contact@endlink.
 * @param extensions
 *     A pointer to a hash table which will be initialized to contain the
 *     version information returned by the service. The extensions defined by
 *     GRAM5 are @a toolkit-version and @a version.
 *
 * @return
 *     Upon success, globus_gram_client_get_jobmanager_version() function
 *     returns GLOBUS_SUCCESS and modifies the @a extensions parameter as
 *     described above. If an error occurs, the integer error code will be
 *     returned and the value pointed to by the @a extensions parameter is
 *     undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_BAD_GATEKEEPER_CONTACT
 *     Bad gatekeeper contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *     NULL parameter
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int 
globus_gram_client_get_jobmanager_version(
    const char *                        resource_manager_contact,
    globus_hashtable_t *                extensions)
{
    int                                 rc;
    globus_l_gram_client_monitor_t      monitor;

    if (resource_manager_contact == NULL || extensions == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto null_param;
    }

    globus_l_gram_client_monitor_init(&monitor, NULL, NULL, NULL, NULL);

    rc = globus_l_gram_client_get_jobmanager_version(
            resource_manager_contact,
            NULL,
            &monitor);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(&monitor);

        return rc;
    }

    globus_mutex_lock(&monitor.mutex);
    while (!monitor.done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }
    rc = monitor.info->protocol_error_code;

    if (rc == GLOBUS_SUCCESS)
    {
        *extensions = monitor.info->extensions;
        monitor.info->extensions = NULL;
    }

    globus_mutex_unlock(&monitor.mutex);

    globus_l_gram_client_monitor_destroy(&monitor);

null_param:
    return rc;
}
/* globus_gram_client_get_jobmanager_version() */

/**
 * @brief Get version information from a job manager without blocking
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_register_get_jobmanager_version() function sends a
 * message to a GRAM service which returns information about the job manager
 * version to the function pointed to by the @a info_callback function. Note
 * that job managers prior to GT5 do not support the version request and so
 * will return a GLOBUS_GRAM_PROTOCOL_ERROR_HTTP_UNPACK_FAILED error. This
 * function blocks while processing this request.
 *
 * @param resource_manager_contact
 *     A NULL-terminated character string containing a
 *     @link globus_gram_resource_manager_contact GRAM contact@endlink.
 * @param attr
 *     A set of client attributes to use to contact the gatekeeper. If no
 *     custom attributes are needed, the caller should pass the value 
 *     @a GLOBUS_GRAM_CLIENT_NO_ATTR.
 * @param info_callback
 *     A pointer to a function to call when the version request has
 *     completed or failed.
 * @param callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a info_callback as its @a user_callback_arg
 *     parameter.
 *
 * @return
 *     Upon success, globus_gram_client_register_get_jobmanager_version()
 *     function returns GLOBUS_SUCCESS and begins processing the version
 *     request to contact @a resource_manager_contact; when complete, the
 *     @a info_callback function will be called.
 *     If an error occurs, the integer error code will be
 *     returned and the value pointed to by the @a extensions parameter is
 *     undefined.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_BAD_GATEKEEPER_CONTACT
 *     Bad gatekeeper contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *     NULL parameter
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol failed
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int 
globus_gram_client_register_get_jobmanager_version(
    const char *                        resource_manager_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_info_callback_func_t
                                        info_callback,
    void *                              callback_arg)
{
    int                                 rc;
    globus_l_gram_client_monitor_t *    monitor;

    if (resource_manager_contact == NULL || info_callback == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto null_param;
    }
    monitor = malloc(sizeof(globus_l_gram_client_monitor_t));

    if(!monitor)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto monitor_malloc_failed;
    }
    globus_l_gram_client_monitor_init(monitor, NULL, NULL,
            info_callback, callback_arg);

    rc = globus_l_gram_client_get_jobmanager_version(
            resource_manager_contact,
            attr,
            monitor);

    if (rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(monitor);
        free(monitor);
    }

monitor_malloc_failed:
null_param:
    return rc;
}
/* globus_gram_client_register_get_jobmanager_version() */

/**
 * @brief Send a job request to a GRAM service
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_register_job_request() function sends a
 * GRAM protocol message to a service to request that it start a job on behalf
 * of the client. 
 *
 * If globus_gram_client_register_job_request() determines that this request
 * could not be processed before contacting the gatekeeper (for example, a
 * malformed @a resource_manager_contact),  it will return an error, and the 
 * @a regiser_callback function will not be called. Otherwise, the success or
 * failure an be determined by the @a operation_failure_code parameter to
 * the function pointed to by the @a register_callback parameter.
 *
 * @param resource_manager_contact
 *     A NULL-terminated character string containing a
 *     @link globus_gram_resource_manager_contact GRAM contact@endlink that
 *     this function will contact.
 * @param description
 *     A pointer to a string containing the job request information formatted
 *     in RSL syntax.
 * @param job_state_mask
 *     A bitwise-or of the GLOBUS_GRAM_PROTOCOL_JOB_STATE_* states that
 *     the job manager will send job state notification messages for to the
 *     contact named by @a callback_contact.
 * @param callback_contact
 *     A GRAM listener contact that the job manager will send job state
 *     notification messages to.
 * @param attr
 *     A set of client attributes to use to contact the gatekeeper. If no
 *     custom attributes are needed, the caller should pass the value 
 *     @a GLOBUS_GRAM_CLIENT_NO_ATTR.
 * @param register_callback
 *     A pointer to a function to call when the job_request request has
 *     completed or failed.
 * @param register_callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a register_callback as its @a user_callback_arg
 *     parameter.
 *
 * @return
 *     Upon success, globus_gram_client_register_job_request() returns
 *     GLOBUS_SUCCESS and the @a register_callback function will be called once
 *     the job request operation completes. If an error occurs, this function
 *     returns an integer error code and the function pointed to by the
 *     @a register_callback parameter will not be called.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int
globus_gram_client_register_job_request(
    const char *                        resource_manager_contact,
    const char *                        description,
    int                                 job_state_mask,
    const char *                        callback_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg)
{
    globus_i_gram_client_attr_t *       iattr = NULL;
    globus_l_gram_client_monitor_t *    monitor;
    int                                 rc;

    monitor = malloc(sizeof(globus_l_gram_client_monitor_t));
    if(!monitor)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }

    iattr = (globus_i_gram_client_attr_t *) attr;

    globus_l_gram_client_monitor_init(
            monitor,
            NULL,
            register_callback,
            NULL,
            register_callback_arg);

    rc = globus_l_gram_client_job_request(resource_manager_contact,
                                          description,
                                          job_state_mask,
                                          iattr,
                                          callback_contact,
                                          monitor);
    if(rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(monitor);
        free(monitor);
    }
    return rc;
}
/* globus_gram_client_register_job_request() */

/**
 * @brief Send a job request to a GRAM service
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_job_request() function sends a
 * GRAM protocol message to a service to request that it start a job on behalf
 * of the client.  Unlike, globus_gram_client_register_job_request(),
 * globus_gram_client_job_request() blocks until the job
 * request has been processed by the service. 
 *
 * @param resource_manager_contact
 *     A NULL-terminated character string containing a
 *     @link globus_gram_resource_manager_contact GRAM contact@endlink that
 *     this function will contact.
 * @param description
 *     A pointer to a string containing the job request information formatted
 *     in RSL syntax.
 * @param job_state_mask
 *     A bitwise-or of the GLOBUS_GRAM_PROTOCOL_JOB_STATE_* states that
 *     the job manager will send job state notification messages for to the
 *     contact named by @a callback_contact.
 * @param callback_contact
 *     A GRAM listener contact that the job manager will send job state
 *     notification messages to.
 * @param job_contact
 *     An output parameter pointing to a string that will be set to the
 *     job contact for this job. This value will only be set if the job
 *     request is successful or the two-phase commit protocol is being used
 *     and the return code is @a GLOBUS_GRAM_PROTOCOL_ERROR_WAITING_FOR_COMMIT.
 *
 * @return
 *     Upon success, globus_gram_client_job_request() returns
 *     GLOBUS_SUCCESS and modifies the value pointed to by @a job_contact as
 *     described above.  If an error occurs, this function
 *     returns an integer error code and the value pointed to by
 *     @a job_contact. In addition to the error codes described below, any
 *     #globus_gram_protocol_error_t value may be returned as a cause for
 *     the job to fail.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int 
globus_gram_client_job_request(
    const char *                        resource_manager_contact,
    const char *                        description,
    int                                 job_state_mask,
    const char *                        callback_contact,
    char **                             job_contact)
{
    int                                 rc;
    globus_l_gram_client_monitor_t      monitor;

    if(job_contact)
    {
        *job_contact = NULL;
    }

    globus_l_gram_client_monitor_init(
            &monitor,
            NULL,
            NULL,
            NULL,
            NULL);

    rc = globus_l_gram_client_job_request(resource_manager_contact,
                                          description,
                                          job_state_mask,
                                          NULL,
                                          callback_contact,
                                          &monitor);
    if(rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(&monitor);

        return rc;
    }

    globus_mutex_lock(&monitor.mutex);
    while (!monitor.done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }
    rc = monitor.info->protocol_error_code;
    if(job_contact && monitor.info->job_contact)
    {
        *job_contact = strdup(monitor.info->job_contact);
    }
    globus_mutex_unlock(&monitor.mutex);

    globus_l_gram_client_monitor_destroy(&monitor);

    return rc;
}
/* globus_gram_client_job_request() */

/**
 * @brief Send a job request to a GRAM service with extensions-aware callback
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_register_job_request_with_info() function sends a
 * GRAM protocol message to a service to request that it start a job on behalf
 * of the client. 
 *
 * If globus_gram_client_register_job_request_with_info() determines that
 * this request could not be processed before contacting the gatekeeper (for
 * example, a malformed @a resource_manager_contact),  it will return an error,
 * and the @a callback function will not be called. Otherwise, the
 * success or failure an be determined by the @a operation_failure_code
 * parameter to the function pointed to by the @a callback parameter. The
 * difference between this function and
 * @a #globus_gram_client_register_job_request() is the function signature of
 * the callback function.
 *
 * @param resource_manager_contact
 *     A NULL-terminated character string containing a
 *     @link globus_gram_resource_manager_contact GRAM contact@endlink that
 *     this function will contact.
 * @param description
 *     A pointer to a string containing the job request information formatted
 *     in RSL syntax.
 * @param job_state_mask
 *     A bitwise-or of the GLOBUS_GRAM_PROTOCOL_JOB_STATE_* states that
 *     the job manager will send job state notification messages for to the
 *     contact named by @a callback_contact.
 * @param callback_contact
 *     A GRAM listener contact that the job manager will send job state
 *     notification messages to.
 * @param attr
 *     A set of client attributes to use to contact the gatekeeper. If no
 *     custom attributes are needed, the caller should pass the value 
 *     @a GLOBUS_GRAM_CLIENT_NO_ATTR.
 * @param callback
 *     A pointer to a function to call when the job_request request has
 *     completed or failed.
 * @param callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a callback as its @a user_callback_arg
 *     parameter.
 *
 * @return
 *     Upon success, globus_gram_client_register_job_request_with_info()
 *     returns GLOBUS_SUCCESS and the @a callback function will be
 *     called once the job request operation completes. If an error occurs,
 *     this function returns an integer error code and the function pointed to
 *     by the @a callback parameter will not be called.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int
globus_gram_client_register_job_request_with_info(
    const char *                        resource_manager_contact,
    const char *                        description,
    int                                 job_state_mask,
    const char *                        callback_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_info_callback_func_t
                                        callback,
    void *                              callback_arg)
{
    globus_i_gram_client_attr_t *       iattr = NULL;
    globus_l_gram_client_monitor_t *    monitor;
    int                                 rc;

    monitor = malloc(sizeof(globus_l_gram_client_monitor_t));
    if(!monitor)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }

    iattr = (globus_i_gram_client_attr_t *) attr;

    globus_l_gram_client_monitor_init(
            monitor,
            NULL,
            NULL,
            callback,
            callback_arg);

    rc = globus_l_gram_client_job_request(resource_manager_contact,
                                          description,
                                          job_state_mask,
                                          iattr,
                                          callback_contact,
                                          monitor);
    if(rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(monitor);
        free(monitor);
    }
    return rc;
}

/**
 * @brief Send a job request to a GRAM service and parse extensions in the response
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_job_request_with_info() function sends a
 * GRAM protocol message to a service to request that it start a job on behalf
 * of the client.  Unlike,
 * globus_gram_client_register_job_request_with_info(),
 * globus_gram_client_job_request_with_info() blocks until the job
 * request has been processed by the service. 
 *
 * @param resource_manager_contact
 *     A NULL-terminated character string containing a
 *     @link globus_gram_resource_manager_contact GRAM contact@endlink that
 *     this function will contact.
 * @param description
 *     A pointer to a string containing the job request information formatted
 *     in RSL syntax.
 * @param job_state_mask
 *     A bitwise-or of the GLOBUS_GRAM_PROTOCOL_JOB_STATE_* states that
 *     the job manager will send job state notification messages for to the
 *     contact named by @a callback_contact.
 * @param callback_contact
 *     A GRAM listener contact that the job manager will send job state
 *     notification messages to.
 * @param job_contact
 *     An output parameter pointing to a string that will be set to the
 *     job contact for this job. This value will only be set if the job
 *     request is successful or the two-phase commit protocol is being used
 *     and the return code is @a GLOBUS_GRAM_PROTOCOL_ERROR_WAITING_FOR_COMMIT.
 * @param info
 *     An output parameter pointing to a structure to hold the extensions in
 *     the GRAM response. The caller is responsible for destroying this by
 *     calling the globus_gram_client_job_info_destroy() function.
 *
 * @return
 *     Upon success, globus_gram_client_job_request_with_info() returns
 *     GLOBUS_SUCCESS and modifies the values pointed to by @a job_contact and
 *     @a info as described above.  If an error occurs, this function
 *     returns an integer error code and the value pointed to by
 *     @a job_contact. In addition to the error codes described below, any
 *     #globus_gram_protocol_error_t value may be returned as a cause for
 *     the job to fail.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int 
globus_gram_client_job_request_with_info(
    const char *                        resource_manager_contact,
    const char *                        description,
    int                                 job_state_mask,
    const char *                        callback_contact,
    char **                             job_contact,
    globus_gram_client_job_info_t *     info)
{
    int                                 rc;
    globus_l_gram_client_monitor_t      monitor;

    if(job_contact)
    {
        *job_contact = NULL;
    }

    globus_l_gram_client_monitor_init(
            &monitor,
            info,
            NULL,
            NULL,
            NULL);

    rc = globus_l_gram_client_job_request(resource_manager_contact,
                                          description,
                                          job_state_mask,
                                          NULL,
                                          callback_contact,
                                          &monitor);
    if(rc != GLOBUS_SUCCESS)
    {
	monitor.info = NULL;
        globus_l_gram_client_monitor_destroy(&monitor);

        return rc;
    }

    globus_mutex_lock(&monitor.mutex);
    while (!monitor.done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }
    rc = monitor.info->protocol_error_code;
    if(job_contact && monitor.info->job_contact)
    {
        *job_contact = strdup(monitor.info->job_contact);
    }
    globus_mutex_unlock(&monitor.mutex);

    monitor.info = NULL;
    globus_l_gram_client_monitor_destroy(&monitor);

    return rc;
}
/* globus_gram_client_job_request_with_info() */

/**
 * @brief Get a description of a a GRAM error code
 * @ingroup globus_gram_client
 *
 * @details
 * The globus_gram_client_error_string() function takes a GRAM error code
 * value and returns the associated error code string. The string is statically
 * allocated by the Globus GRAM Client library and should not be modified or
 * freed. The string is intended to complete a sentence of the form
 * "[operation] failed because ..."
 *
 * @param error_code
 *     The error code to translate into a string.
 *
 * @return
 *     The globus_gram_client_error_string() function returns a static 
 *     string containing an explanation of the error.
 *
 */
const char *
globus_gram_client_error_string(int error_code)
{
    return globus_gram_protocol_error_string(error_code);
}
/* globus_gram_protocol_error_string() */

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/*
 * packing/sending to jobmanager URL/waiting/unpacking 
 */
int
globus_l_gram_client_to_jobmanager(
    const char *                        job_contact,
    const char *                        request,
    globus_i_gram_client_attr_t *       iattr,
    globus_l_gram_client_callback_type_t
                                        request_type,
    globus_l_gram_client_monitor_t *    monitor)
{
    int                                 rc;
    globus_byte_t *                     query = NULL; 
    globus_size_t                       querysize;
    globus_io_attr_t                    attr;
    globus_bool_t                       use_attr = GLOBUS_FALSE;

    if (iattr != NULL && iattr->credential != GSS_C_NO_CREDENTIAL)
    {
        rc = globus_l_gram_client_setup_jobmanager_attr( 
                     &attr,
                     iattr->credential);
        if (rc != GLOBUS_SUCCESS)
        {
            goto error_exit;
        }
        use_attr = GLOBUS_TRUE;
    }
    rc = globus_gram_protocol_pack_status_request(
              request,
              &query,
              &querysize);

    if (rc!=GLOBUS_SUCCESS)
    {
        goto free_attr_exit;
    }
    
    globus_mutex_lock(&monitor->mutex);
    monitor->type = request_type;

    rc = globus_gram_protocol_post(
                 job_contact,
                 &monitor->handle,
                 use_attr ? &attr : NULL,
                 query,
                 querysize,
                 (monitor->callback != NULL || monitor->info_callback != NULL) 
                    ? globus_l_gram_client_register_callback
                    : globus_l_gram_client_monitor_callback,
                 monitor);

    globus_mutex_unlock(&monitor->mutex);

    if(rc != GLOBUS_SUCCESS)
    {
        if(rc == GLOBUS_GRAM_PROTOCOL_ERROR_CONNECTION_FAILED)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_CONTACTING_JOB_MANAGER;
            monitor->info->protocol_error_code =
                GLOBUS_GRAM_PROTOCOL_ERROR_CONTACTING_JOB_MANAGER;
        }
        else
        {
            monitor->info->protocol_error_code = rc;
        }
    }

    if(query)
    {
        free(query);
    }
free_attr_exit:
    if(use_attr)
    {
        globus_io_tcpattr_destroy (&attr);
    }
error_exit:
    return rc;
}
/* globus_l_gram_client_to_jobmanager() */
#endif

/**
 * @brief Cancel a GRAM job
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_job_cancel() function cancels a
 * GRAM job. Depending on the job's current state, this cancellation may be
 * immediate or require a delay for interacting with the LRM servicing the
 * job. Notification when the job has been successfully canceled will be
 * sent to all client contacts registered for notifications after the 
 * cancellation has been completely processed. Unlike,
 * globus_gram_client_register_job_cancel(),
 * globus_gram_client_job_cancel() blocks until the job
 * cancel request has been processed by the service. 

 *
 * @param  job_contact
 *     A NULL-terminated character string containing a
 *     GRAM job contact that this function will contact to cancel the job.
 *
 * @return
 *     Upon succes, globus_gram_client_job_cancel() returns
 *     @a GLOBUS_SUCCESS if the cancellation was successful posted to the 
 *     service. If an error occurs, globus_gram_client_job_cancel()
 *     returns one of the #globus_gram_protocol_error_t values
 *     values indicating why the client could not cancel the job.
 *
 * @see globus_gram_client_register_job_cancel()
 */
int
globus_gram_client_job_cancel(
    const char *                        job_contact)
{
    int                                 rc;
    globus_l_gram_client_monitor_t      monitor;

    GLOBUS_L_CHECK_IF_INITIALIZED;

    globus_l_gram_client_monitor_init(
            &monitor,
            NULL,
            NULL,
            NULL,
            NULL);

    rc = globus_l_gram_client_to_jobmanager( job_contact,
                                             "cancel",
                                             NULL,
                                             GLOBUS_GRAM_CLIENT_CANCEL,
                                             &monitor);

    if(rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(&monitor);

        return rc;
    }

    globus_mutex_lock(&monitor.mutex);
    while (!monitor.done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }
    rc = monitor.info->protocol_error_code;

    globus_mutex_unlock(&monitor.mutex);

    globus_l_gram_client_monitor_destroy(&monitor);

    return rc;
}
/* globus_gram_client_job_cancel() */

/**
 * @brief Cancel a GRAM job
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_register_job_cancel() function sends a message
 * to a GRAM service to cancel a GRAM job. Depending on the job's current
 * state, this cancellation may be immediate or require a delay for
 * interacting with the LRM servicing the job. In either case, this function
 * returns as soon as it is able to start sending the message. Notification
 * when the job has been successfully canceled will be sent to all client
 * contacts registered for notifications after the cancellation has been
 * completely processed.
 *
 * @param  job_contact
 *     A NULL-terminated character string containing a
 *     GRAM job contact that this function will contact to cancel the job.
 * @param attr
 *     A set of client attributes to use to contact the job. If no
 *     custom attributes are needed, the caller should pass the value 
 *     @a GLOBUS_GRAM_CLIENT_NO_ATTR.
 * @param register_callback
 *     A pointer to a function to call when the job_request request has
 *     completed or failed.
 * @param register_callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a register_callback as its @a user_callback_arg
 *     parameter.
 *
 * @return
 *     Upon succes, globus_gram_client_register_job_cancel() returns
 *     @a GLOBUS_SUCCESS if the cancellation was successful posted to the 
 *     service. If an error occurs, globus_gram_client_register_job_cancel()
 *     returns one an integer error code indicating why it could not cancel the
 *     job.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 *
 * @see globus_gram_client_job_cancel()
 */
int
globus_gram_client_register_job_cancel(
    const char *                        job_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg)
{
    int                                 rc;
    globus_l_gram_client_monitor_t *    monitor;

    GLOBUS_L_CHECK_IF_INITIALIZED;

    monitor = malloc(sizeof(globus_l_gram_client_monitor_t));

    if(!monitor)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }

    globus_l_gram_client_monitor_init(
            monitor,
            NULL,
            register_callback,
            NULL,
            register_callback_arg);

    rc = globus_l_gram_client_to_jobmanager( job_contact,
                                             "cancel",
                                             (globus_i_gram_client_attr_t*)attr,
                                             GLOBUS_GRAM_CLIENT_CANCEL,
                                             monitor);

    if(rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(monitor);
        free(monitor);
    }
    return rc;
}
/* globus_gram_client_register_job_cancel() */

/**
 * @brief Send a signal a GRAM job
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_job_signal() function sends a signal message to
 * a job managed by the GRAM service.
 * Signals consist of a signal number and an optional string
 * argument. The meanings of the signals supported by the GRAM job manager
 * are defined in the GRAM Protocol documentation.
 * Unlike globus_gram_client_register_job_signal(), this function blocks
 * until the signal has been delivered and acknowledged by the GRAM service.
 *
 * @param job_contact
 *     The job contact string of the job manager to contact. This is the
 *     same value returned from globus_gram_client_job_request().
 * @param signal
 *     The signal code to send to the job manager.
 * @param signal_arg
 *     Parameters for the signal, as described in the documentation
 *     for the #globus_gram_protocol_job_signal_t enumeration.
 * @param job_status
 *     An output parameter pointing to an integer to set to the status
 *     of the job after the signal has been processed.
 * @param failure_code
 *     An output parameter pointing to an integer to set to the reason
 *     why the job has failed if the value pointed to by @a job_status is
 *     set to @a GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED by this function.
 *
 * @return
 *     Upon success, globus_gram_client_job_signal() returns GLOBUS_SUCCESS
 *     after sending the signal and receiving a response and modifies the
 *     @a job_status and @a failure_code parameters as described above. If an
 *     error occurs, this function returns an integer error code indicating
 *     why the client could not signal the job.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 *
 * @see globus_gram_client_register_job_signal()
 */
int 
globus_gram_client_job_signal(
    const char  *                       job_contact,
    globus_gram_protocol_job_signal_t   signal,
    const char *                        signal_arg,
    int *                               job_status,
    int *                               failure_code)
{
    int       rc;
    char  *   request;
    globus_l_gram_client_monitor_t      monitor;

    GLOBUS_L_CHECK_IF_INITIALIZED;

    globus_l_gram_client_monitor_init(
            &monitor,
            NULL,
            NULL,
            NULL,
            NULL);

    if (signal_arg != NULL)
    {
        /* 'signal' = 6, allow 10-digit integer, 2 spaces and null  */
        request = (char *) malloc( strlen(signal_arg)
                                               + 6 + 10 + 2 + 1 );
        if (request == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto error_exit;
        }

        globus_libc_sprintf(request,
                            "signal %d %s",
                            signal,
                            signal_arg);
    }
    else
    {
        /* 'signal' = 6, allow 10-digit integer, 1 space and null  */
        request = (char *) malloc( 6 + 10 + 1 + 1 );
        if (request == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto error_exit;
        }

        globus_libc_sprintf(request,
                            "signal %d",
                            signal);
    }

    rc = globus_l_gram_client_to_jobmanager( job_contact,
                                             request,
                                             NULL,
                                             GLOBUS_GRAM_CLIENT_SIGNAL,
                                             &monitor);
    if(rc != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }

    globus_mutex_lock(&monitor.mutex);
    while (!monitor.done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }
    rc = monitor.info->protocol_error_code;

    globus_mutex_unlock(&monitor.mutex);

error_exit:
    if(job_status)
    {
        *job_status = monitor.info->job_state;
    }
    if(failure_code)
    {
        if (monitor.info->protocol_error_code)
        {
            *failure_code = monitor.info->protocol_error_code;
        }
        else
        {
            *failure_code = globus_l_gram_info_get_int(
                    &monitor.info->extensions,
                    "job-failure-code");
        }
    }
    globus_l_gram_client_monitor_destroy(&monitor);
    if (request != NULL)
    {
        free(request);
    }

    return rc;
}
/* globus_gram_client_job_signal() */

/**
 * @brief Send a signal a GRAM job
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_register_job_signal() function sends a signal
 * message to a job managed by the GRAM service.
 * Signals consist of a signal number and an optional string
 * argument. The meanings of the signals supported by the GRAM job manager
 * are defined in the GRAM Protocol documentation. This function returns as
 * soon as it has determined that its parameters are valid and it has
 * begun to send the message to the GRAM service.
 *
 * @param job_contact
 *     The job contact string of the job manager to contact. This is the
 *     same value returned from globus_gram_client_job_request().
 * @param signal
 *     The signal code to send to the job manager.
 * @param signal_arg
 *     Parameters for the signal, as described in the documentation
 *     for the #globus_gram_protocol_job_signal_t enumeration.
 * @param attr
 *     A set of client attributes to use to contact the job. If no
 *     custom attributes are needed, the caller should pass the value 
 *     @a GLOBUS_GRAM_CLIENT_NO_ATTR.
 * @param register_callback
 *     A pointer to a function to call when the signal request has
 *     completed or failed.
 * @param register_callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a register_callback as its @a user_callback_arg
 *     parameter.
 *
 * @return
 *     Upon success, globus_gram_client_job_register_signal() returns
 *     GLOBUS_SUCCESS after beginnning to send the signal to the GRAM job and
 *     registers the @a register_callback function to be called once that
 *     has completed. If an error occurs, this function returns an integer
 *     error code indicating why the client could not signal the job.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 *
 * @see globus_gram_client_register_job_signal()
 */
int 
globus_gram_client_register_job_signal(
    const char  *                       job_contact,
    globus_gram_protocol_job_signal_t   signal,
    const char *                        signal_arg,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg)
{
    int                                 rc;
    char *                              request;
    globus_l_gram_client_monitor_t *    monitor;

    GLOBUS_L_CHECK_IF_INITIALIZED;

    monitor = malloc(sizeof(globus_l_gram_client_monitor_t));
    if(!monitor)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }

    globus_l_gram_client_monitor_init(
            monitor,
            NULL,
            register_callback,
            NULL,
            register_callback_arg);

    if (signal_arg != NULL)
    {
        /* 'signal' = 6, allow 10-digit integer, 2 spaces and null  */
        request = (char *) malloc( strlen(signal_arg)
                                               + 6 + 10 + 2 + 1 );
        if (request == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto error_exit;
        }

        globus_libc_sprintf(request,
                            "signal %d %s",
                            signal,
                            signal_arg);
    }
    else
    {
        /* 'signal' = 6, allow 10-digit integer, 1 space and null  */
        request = (char *) malloc( 6 + 10 + 1 + 1 );
        if (request == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto error_exit;
        }

        globus_libc_sprintf(request,
                            "signal %d",
                            signal);
    }

    rc = globus_l_gram_client_to_jobmanager( job_contact,
                                             request,
                                             attr,
                                             GLOBUS_GRAM_CLIENT_SIGNAL,
                                             monitor);
    free(request);
error_exit:
    if(rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(monitor);
        free(monitor);
    }

    return rc;
}
/* globus_gram_client_register_job_signal() */

/**
 * @brief Send a status query to a GRAM job
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_status() function queries the current status of
 * the job associated with the job contact,
 * returning its current status, as well as the job's failure reason if it has
 * failed in this function's return parameters. This function blocks until
 * the service has responded to the status query.
 *
 * @param job_contact
 *     The job contact string of the job to query. This is the same
 *     value returned from globus_gram_client_job_request().
 * @param job_status
 *     An output parameter that points to an integer to be set to the current
 *     status of the job named by the @a job_contact parameter.
 * @param failure_code
 *     An output parameter that points to an integer to be set to the reason
 *     why the job failed if its current status is
 *     @a GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED. If the job has not failed,
 *     the value will be set to 0.
 *
 * @return
 *     Upon success, the globus_gram_client_job_status() function returns
 *     @a GLOBUS_SUCCESS, sends a job state query to the job named by
 *     @a job_contact and parses the service response, modifying the values
 *     pointed to by @a job_status and @a failure_code as described above. If
 *     an error occurs, globus_gram_client_job_status() returns an integer
 *     error code.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int
globus_gram_client_job_status(
    const char *                        job_contact,
    int *                               job_status,
    int *                               failure_code)
{
    int                                 rc;
    globus_l_gram_client_monitor_t      monitor;

    GLOBUS_L_CHECK_IF_INITIALIZED;

    globus_l_gram_client_monitor_init(
            &monitor,
            NULL,
            NULL,
            NULL,
            NULL);

    rc = globus_l_gram_client_to_jobmanager( job_contact,
                                             "status",
                                             NULL,
                                             GLOBUS_GRAM_CLIENT_STATUS,
                                             &monitor);

    if(rc != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }

    globus_mutex_lock(&monitor.mutex);
    while (!monitor.done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }
    rc = monitor.info->protocol_error_code;

    globus_mutex_unlock(&monitor.mutex);

error_exit:
    if(job_status)
    {
        *job_status = monitor.info->job_state;
    }
    if(failure_code)
    {
        if (monitor.info->protocol_error_code)
        {
            *failure_code = monitor.info->protocol_error_code;
        }
        else
        {
            *failure_code = globus_l_gram_info_get_int(
                    &monitor.info->extensions,
                    "job-failure-code");
        }
    }
    globus_l_gram_client_monitor_destroy(&monitor);

    return rc;
}
/* globus_gram_client_job_status() */

/**
 * @brief Send a status query to a GRAM job
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_register_job_status() function initiates a
 * query of the current status of the job associated with the @a job contact
 * parameter. The job's status and failure code are passed to the function
 * pointed to by the @a register_callback parameter. This function returns
 * as soon as it has begun requesting the job status.
 *
 * @param job_contact
 *     The job contact string of the job to query. This is the same
 *     value returned from globus_gram_client_job_request().
 * @param attr
 *     A set of client attributes to use to contact the job. If no
 *     custom attributes are needed, the caller should pass the value 
 *     @a GLOBUS_GRAM_CLIENT_NO_ATTR.
 * @param register_callback
 *     A pointer to a function to call when the status request has
 *     completed or failed.
 * @param register_callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a register_callback as its @a user_callback_arg
 *     parameter.
 *
 * @return
 *     Upon success, the globus_gram_client_register_job_status() function
 *     returns @a GLOBUS_SUCCESS and begins to send a job state query to the
 *     job named by @a job_contact and registers the function pointed to by
 *     the @a register_callback parameter to be called once the status query
 *     terminates or fails.  If an error occurs,
 *     globus_gram_client_register_job_status() returns an integer
 *     error code.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *    Null parameter
 */
int
globus_gram_client_register_job_status(
    const char *			job_contact,
    globus_gram_client_attr_t		attr,
    globus_gram_client_nonblocking_func_t
    					register_callback,
    void *				register_callback_arg)
{
    int					rc;
    globus_l_gram_client_monitor_t * 	monitor;

    if (job_contact == NULL || register_callback == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto null_param;
    }
    GLOBUS_L_CHECK_IF_INITIALIZED;

    monitor = globus_libc_malloc(sizeof(globus_l_gram_client_monitor_t));
    if(!monitor)
    {
	return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }
    globus_l_gram_client_monitor_init(
            monitor,
            NULL,
            register_callback,
            NULL,
            register_callback_arg);

    rc = globus_l_gram_client_to_jobmanager( job_contact,
					     "status",
                                             attr,
					     GLOBUS_GRAM_CLIENT_STATUS,
					     monitor);

    if(rc != GLOBUS_SUCCESS)
    {
	globus_l_gram_client_monitor_destroy(monitor);
	globus_libc_free(monitor);
    }
null_param:
    return rc;
}
/* globus_gram_client_register_job_status() */

/**
 * @brief Send a status query to a GRAM job
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_register_job_status_with_info() function initiates
 * a query of the current status of the job associated with the @a job contact
 * parameter. Job information is returned via the @a job_info parameter passed
 * to the function pointed to by the @a info_callback parameter.
 * This function returns as soon as it has begun requesting the job status.
 *
 * @param job_contact
 *     The job contact string of the job to query. This is the same
 *     value returned from globus_gram_client_job_request().
 * @param attr
 *     A set of client attributes to use to contact the job. If no
 *     custom attributes are needed, the caller should pass the value 
 *     @a GLOBUS_GRAM_CLIENT_NO_ATTR.
 * @param info_callback
 *     A pointer to a function to call when the status request has
 *     completed or failed.
 * @param callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a info_callback as its @a user_callback_arg
 *     parameter.
 *
 * @return
 *     Upon success, the globus_gram_client_register_job_status_with_info()
 *     function returns @a GLOBUS_SUCCESS and begins to send a job state query
 *     to the job named by @a job_contact and registers the function pointed to
 *     by the @a info_callback parameter to be called once the status query
 *     terminates or fails.  If an error occurs,
 *     globus_gram_client_register_job_status_with_info() returns an integer
 *     error code.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *    Null parameter
 */
int
globus_gram_client_register_job_status_with_info(
    const char *                        job_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_info_callback_func_t
                                        info_callback,
    void *                              callback_arg)
{
    int                                 rc;
    globus_l_gram_client_monitor_t *    monitor;

    GLOBUS_L_CHECK_IF_INITIALIZED;

    if (job_contact == NULL || info_callback == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;

        goto null_param;
    }

    monitor = malloc(sizeof(globus_l_gram_client_monitor_t));
    if(!monitor)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }
    globus_l_gram_client_monitor_init(
            monitor,
            NULL,
            NULL,
            info_callback,
            callback_arg);

    rc = globus_l_gram_client_to_jobmanager( job_contact,
                                             "status",
                                             attr,
                                             GLOBUS_GRAM_CLIENT_STATUS,
                                             monitor);

    if(rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(monitor);
        free(monitor);
    }
null_param:
    return rc;
}
/* globus_gram_client_register_job_status_with_info() */

/**
 * @brief Send a status query to a GRAM job
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_status_with_info() function queries the current
 * status of the job associated with the job contact,
 * returning its current status, as well as the job's failure reason if it has
 * failed in this function's return parameters. This function blocks until
 * the service has responded to the status query.
 *
 * @param job_contact
 *     The job contact string of the job to query. This is the same
 *     value returned from globus_gram_client_job_request().
 * @param info
 *     An output parameter that points to a globus_gram_client_job_info_t 
 *     structure which will be populated with the state information associated
 *     with the job named by the @a job_contact parameter. The caller is
 *     responsible for calling globus_gram_client_job_info_destroy() to free
 *     the state pointed to by this parameter if this function returns 
 *     @a GLOBUS_SUCCESS.
 *
 * @return
 *     Upon success, the globus_gram_client_job_status_with_info() function
 *     returns @a GLOBUS_SUCCESS, sends a job state query to the job named by
 *     @a job_contact and parses the service response, modifying the structure
 *     pointed to by @a info as described above. If
 *     an error occurs, globus_gram_client_job_status_with_info() returns an
 *     integer error code.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int
globus_gram_client_job_status_with_info(
    const char *                        job_contact,
    globus_gram_client_job_info_t *     info)
{
    int                                 rc;
    globus_l_gram_client_monitor_t      monitor;

    GLOBUS_L_CHECK_IF_INITIALIZED;

    globus_l_gram_client_monitor_init(
            &monitor,
            info,
            NULL,
            NULL,
            NULL);

    rc = globus_l_gram_client_to_jobmanager( job_contact,
                                             "status",
                                             NULL,
                                             GLOBUS_GRAM_CLIENT_STATUS,
                                             &monitor);

    if(rc != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }

    globus_mutex_lock(&monitor.mutex);
    while (!monitor.done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }
    rc = monitor.info->protocol_error_code;

    globus_mutex_unlock(&monitor.mutex);

error_exit:
    monitor.info = NULL;
    globus_l_gram_client_monitor_destroy(&monitor);

    return rc;
}
/* globus_gram_client_job_status_with_info() */

/**
 * @brief Register a new callback contact to be notified for job state changes
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_job_callback_register() function contacts a
 * GRAM service managing a job and instructs it to send subsequent job state
 * callbacks to the client listening for callbacks at the contact url named
 * by the @a callback_contact parameter. This function blocks until the 
 * registration operation either completes or exits.
 *
 * @param job_contact
 *     The job contact string of the job to contact. This is the same
 *     value returned from globus_gram_client_job_request().
 * @param job_state_mask
 *     A bitwise-or of the GLOBUS_GRAM_PROTOCOL_JOB_STATE_* states that
 *     the job manager will send job state notification messages for to the
 *     contact named by @a callback_contact.
 * @param callback_contact
 *     A URL string containing a GRAM client callback. This string is
 *     normally be generated by a process calling
 *     globus_gram_client_callback_allow().
 * @param job_status
 *     An output parameter pointing to an integer to set to the status
 *     of the job after the registration message has been processed.
 * @param failure_code
 *     An output parameter that points to an integer to be set to the reason
 *     why the job failed if its current status is
 *     @a GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED. If the job has not failed,
 *     the value will be set to 0.
 *
 * @return
 *     Upon success, the globus_gram_client_job_callback_register() function
 *     returns @a GLOBUS_SUCCESS, sends a registration request the job named by
 *     @a job_contact and parses the service response, modifying the values
 *     pointed to by the @a job_status and @a failure_code parameters as
 *     described above. If an error occurs,
 *     globus_gram_client_job_callback_register() returns an
 *     integer error code indicating why it can't register the callback
 *     contact. The return code may be any value defined by the
 *     @a globus_gram_protocol_error_t enumeration in addition to those
 *     listed below.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int 
globus_gram_client_job_callback_register(
    const char *                        job_contact,
    int                                 job_state_mask,
    const char *                        callback_contact,
    int *                               job_status,
    int *                               failure_code)
{
    int                                 rc;
    char  *                             request;
    globus_l_gram_client_monitor_t      monitor;

    GLOBUS_L_CHECK_IF_INITIALIZED;
    globus_l_gram_client_monitor_init(&monitor, NULL, NULL, NULL, NULL);

    /* 'register' = 8, allow 10-digit integer, 2 spaces and null  */
    request = (char *) malloc( 
                          strlen(callback_contact)
                          + 8 + 10 + 2 + 1 );

    if (request == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto error_exit;
    }

    globus_libc_sprintf(request,
                        "register %d %s",
                        job_state_mask,
                        callback_contact);

    rc = globus_l_gram_client_to_jobmanager(
            job_contact,
            request,
            NULL,
            GLOBUS_GRAM_CLIENT_CALLBACK_REGISTER,
            &monitor);

    if(rc != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }

    globus_mutex_lock(&monitor.mutex);
    while (!monitor.done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }
    rc = monitor.info->protocol_error_code;

    globus_mutex_unlock(&monitor.mutex);

error_exit:
    if(job_status)
    {
        *job_status = monitor.info->job_state;
    }
    if(failure_code)
    {
        if (monitor.info->protocol_error_code)
        {
            *failure_code = monitor.info->protocol_error_code;
        }
        else
        {
            *failure_code = globus_l_gram_info_get_int(
                    &monitor.info->extensions,
                    "job-failure-code");
        }
    }
    globus_l_gram_client_monitor_destroy(&monitor);
    if (request != NULL)
    {
        free(request);
    }

    return rc;
}
/* globus_gram_client_job_callback_register() */

/**
 * @brief Unregister a callback contact to stop job state change notifications
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_job_callback_unregister() function contacts a
 * GRAM service managing a job and instructs it to stop sending job state
 * callbacks to the client listening for callbacks at the contact url named
 * by the @a callback_contact parameter. This function blocks until the 
 * unregistration operation either completes or exits. It is possible that
 * callbacks related to the job arrive at the contact after this function 
 * returns depending on network delays.
 * 
 * @param job_contact
 *     The job contact string of the job to contact. This is the same
 *     value returned from globus_gram_client_job_request().
 * @param callback_contact
 *     A URL string containing a GRAM client callback. This string is
 *     normally be generated by a process calling
 *     globus_gram_client_callback_allow().
 * @param job_status
 *     An output parameter pointing to an integer to set to the status
 *     of the job after the registration message has been processed.
 * @param failure_code
 *     An output parameter that points to an integer to be set to the reason
 *     why the job failed if its current status is
 *     @a GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED. If the job has not failed,
 *     the value will be set to 0.
 *
 * @return
 *     Upon success, the globus_gram_client_job_callback_unregister()
 *     function returns @a GLOBUS_SUCCESS, sends an unregister request the job
 *     named by @a job_contact and parses the service response, modifying the
 *     values pointed to by the @a job_status and @a failure_code parameters as
 *     described above. If an error occurs,
 *     globus_gram_client_job_callback_unregister() returns an
 *     integer error code indicating why it can't unregister the callback
 *     contact. The return code may be any value defined by the
 *     @a globus_gram_protocol_error_t enumeration in addition to those
 *     listed below.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int 
globus_gram_client_job_callback_unregister(
    const char *                        job_contact,
    const char *                        callback_contact,
    int *                               job_status,
    int *                               failure_code)
{
    int                                 rc;
    char *                              request;
    globus_l_gram_client_monitor_t      monitor;

    GLOBUS_L_CHECK_IF_INITIALIZED;

    globus_l_gram_client_monitor_init(&monitor, NULL, NULL, NULL, NULL);

    /* 'unregister' = 10, a space and null  */
    request = (char *) malloc( 
                          strlen(callback_contact)
                          + 10 + 1 + 1 );

    if (request == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto error_exit;
    }

    globus_libc_sprintf(request,
                        "unregister %s",
                        callback_contact);

    rc = globus_l_gram_client_to_jobmanager(
            job_contact,
            request,
            NULL,
            GLOBUS_GRAM_CLIENT_CALLBACK_UNREGISTER,
            &monitor);

    if(rc != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }

    globus_mutex_lock(&monitor.mutex);
    while (!monitor.done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }
    rc = monitor.info->protocol_error_code;

    globus_mutex_unlock(&monitor.mutex);

error_exit:
    if(job_status)
    {
        *job_status = monitor.info->job_state;
    }
    if(failure_code)
    {
        if (monitor.info->protocol_error_code)
        {
            *failure_code = monitor.info->protocol_error_code;
        }
        else
        {
            *failure_code = globus_l_gram_info_get_int(
                    &monitor.info->extensions,
                    "job-failure-code");
        }
    }
    globus_l_gram_client_monitor_destroy(&monitor);

    if (request != NULL)
    {
        free(request);
    }

    return rc;
}
/* globus_gram_client_job_callback_unregister() */

/**
 * @brief Delegate a new credential to a job
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_job_refresh_credentials() function sends a
 * "renew" signal to a GRAM service and then initiates the delegation of
 * a new credential to the job manager and job. This prevents errors that
 * can occur when a credential expires. This function blocks until the
 * delegation has completed or failed.
 *
 * @param job_contact
 *     The job contact string of the job to contact. This is the same
 *     value returned from globus_gram_client_job_request().
 * @param creds
 *     A GSSAPI credential handle which will be used to authenticate with the
 *     job manager and sign the delegated credential. This parameter's value
 *     may be set to @a GSS_C_NO_CREDENTIAL to indicate the desire to use this
 *     process's default credential.
 *
 * @return 
 *     Upon success, the globus_gram_client_job_refresh_credentials()
 *     function returns @a GLOBUS_SUCCESS, sends an proxy renew request the job
 *     named by @a job_contact, parses the service response and performs a
 *     GSSAPI delegation to send a new credential to the job service.
 *     If an error occurs,
 *     globus_gram_client_job_refresh_credentials() returns an
 *     integer error code indicating why it can't refresh the job service's
 *     credential. The return code may be any value defined by the
 *     @a globus_gram_protocol_error_t enumeration in addition to those
 *     listed below.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int
globus_gram_client_job_refresh_credentials(
    char *                              job_contact,
    gss_cred_id_t                       creds)
{
    globus_l_gram_client_monitor_t      monitor;
    int                                 rc;

    globus_l_gram_client_monitor_init(&monitor, NULL, NULL, NULL, NULL);

    rc = globus_l_gram_client_job_refresh_credentials(
            job_contact,
            creds,
            NULL,
            &monitor);

    if (rc != GLOBUS_SUCCESS)
    {
        goto end;
    }

    globus_mutex_lock(&monitor.mutex);
    while (!monitor.done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
    }
    rc = monitor.info->protocol_error_code;
    globus_mutex_unlock(&monitor.mutex);

end:
    globus_l_gram_client_monitor_destroy(&monitor);

    return rc;
}
/* globus_gram_client_job_refresh_credentials() */

/**
 * @brief Delegate a new credential to a job
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_register_job_refresh_credentials() function sends
 * a "renew" signal to a GRAM service and then initiates the delegation of
 * a new credential to the job manager and job. This prevents errors that
 * can occur when a credential expires.  This function returns as
 * soon as it has determined that its parameters are valid and it has
 * begun to send the message to the GRAM service.
 *
 * @param job_contact
 *     The job contact string of the job to contact. This is the same
 *     value returned from globus_gram_client_job_request().
 * @param creds
 *     A GSSAPI credential handle which will be used to authenticate with the
 *     job manager and sign the delegated credential. This parameter's value
 *     may be set to @a GSS_C_NO_CREDENTIAL to indicate the desire to use this
 *     process's default credential.
 * @param attr
 *     A set of client attributes to use to contact the job. If no
 *     custom attributes are needed, the caller should pass the value 
 *     @a GLOBUS_GRAM_CLIENT_NO_ATTR.
 * @param register_callback
 *     A pointer to a function to call when the status request has
 *     completed or failed.
 * @param register_callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a register_callback as its @a user_callback_arg
 *     parameter.
 *
 * @return 
 *     Upon success, the globus_gram_client_job_refresh_credentials()
 *     function returns @a GLOBUS_SUCCESS and begins sending the "renew"
 *     request to the GRAM service. If an error occurs,
 *     globus_gram_client_job_refresh_credentials() returns an
 *     integer error code indicating why it can't refresh the job service's
 *     credential. The return code may be any value defined by the
 *     @a globus_gram_protocol_error_t enumeration in addition to those
 *     listed below.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 */
int
globus_gram_client_register_job_refresh_credentials(
    char *                              job_contact,
    gss_cred_id_t                       creds,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg)
{
    globus_l_gram_client_monitor_t *    monitor;
    int                                 rc;

    monitor = malloc(sizeof(globus_l_gram_client_monitor_t));

    if(!monitor)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }
    globus_l_gram_client_monitor_init(
            monitor,
            NULL,
            register_callback,
            NULL,
            register_callback_arg);

    rc = globus_l_gram_client_job_refresh_credentials(
            job_contact,
            creds,
            attr,
            monitor);

    if (rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(monitor);
        free(monitor);
    }

    return rc;
}
/* globus_gram_client_register_job_refresh_credentials() */

/**
 * @brief Register a new callback contact to be notified for job state changes
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_register_job_callback_registration() function
 * initiates the protocol to contact a GRAM service and request that it
 * send subsequent job state callbacks to the client listening for callbacks at
 * the contact url named by the @a callback_contact parameter. This function
 * returns as soon as it has validated its parameters and begun sending the
 * GRAM message. When the registration is complete, the function pointed to
 * by @a register_callback is called.
 *
 * @param job_contact
 *     The job contact string of the job to contact. This is the same
 *     value returned from globus_gram_client_job_request().
 * @param job_state_mask
 *     A bitwise-or of the GLOBUS_GRAM_PROTOCOL_JOB_STATE_* states that
 *     the job manager will send job state notification messages for to the
 *     contact named by @a callback_contact.
 * @param callback_contact
 *     A URL string containing a GRAM client callback. This string is
 *     normally be generated by a process calling
 *     globus_gram_client_callback_allow().
 * @param attr
 *     A set of client attributes to use to contact the job. If no
 *     custom attributes are needed, the caller should pass the value 
 *     @a GLOBUS_GRAM_CLIENT_NO_ATTR.
 * @param register_callback
 *     A pointer to a function to call when the registration request has
 *     completed or failed.
 * @param register_callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a register_callback as its @a user_callback_arg
 *     parameter.
 *
 * @return
 *     Upon success, the
 *     globus_gram_client_register_job_callback_registration() function
 *     returns @a GLOBUS_SUCCESS, begins to send a registration request to
 *     the job named by @a job_contact, and schedules the @a register_callback
 *     to be called once the registration completes or fails.
 *     If an error occurs, this function returns an
 *     integer error code indicating why it can't process the request.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 *
 * @see globus_gram_client_job_callback_register()
 */
int 
globus_gram_client_register_job_callback_registration(
    const char *                        job_contact,
    int                                 job_state_mask,
    const char *                        callback_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg)
{
    int                                 rc;
    char  *                             request;
    globus_l_gram_client_monitor_t *    monitor;

    GLOBUS_L_CHECK_IF_INITIALIZED;

    monitor = malloc(sizeof(globus_l_gram_client_monitor_t));
    if(!monitor)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }
    globus_l_gram_client_monitor_init(
            monitor,
            NULL,
            register_callback,
            NULL,
            register_callback_arg);

    /* 'register' = 8, allow 10-digit integer, 2 spaces and null  */
    request = (char *) malloc( 
                          strlen(callback_contact)
                          + 8 + 10 + 2 + 1 );

    if (request == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto error_exit;
    }

    globus_libc_sprintf(request,
                        "register %d %s",
                        job_state_mask,
                        callback_contact);

    rc = globus_l_gram_client_to_jobmanager(
            job_contact,
            request,
            NULL,
            GLOBUS_GRAM_CLIENT_CALLBACK_REGISTER,
            monitor);

error_exit:
    if(rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(monitor);
        free(monitor);
    }

    if(request != NULL)
    {
        free(request);
    }

    return rc;
}
/* globus_gram_client_register_job_callback_registration() */

/**
 * @brief Unregister a callback contact to stop job state change notifications
 * @ingroup globus_gram_client_job_functions
 *
 * @details
 * The globus_gram_client_register_job_callback_unregistration() function
 * initiates the protocol to contact a GRAM service and request that it
 * stop sending job state callbacks to the client listening at
 * the contact url named by the @a callback_contact parameter. This function
 * returns as soon as it has validated its parameters and begun sending the
 * GRAM message. When the unregistration is complete, the function pointed to
 * by @a register_callback is called.
 * 
 * @param job_contact
 *     The job contact string of the job to contact. This is the same
 *     value returned from globus_gram_client_job_request().
 * @param callback_contact
 *     A URL string containing a GRAM client callback. This string is
 *     normally be generated by a process calling
 *     globus_gram_client_callback_allow().
 * @param attr
 *     A set of client attributes to use to contact the job. If no
 *     custom attributes are needed, the caller should pass the value 
 *     @a GLOBUS_GRAM_CLIENT_NO_ATTR.
 * @param register_callback
 *     A pointer to a function to call when the registration request has
 *     completed or failed.
 * @param register_callback_arg
 *     A pointer to application-specific data which will be passed to the
 *     function pointed to by @a register_callback as its @a user_callback_arg
 *     parameter.
 *
 * @return
 *     Upon success, the
 *     globus_gram_client_register_job_callback_unregistration()
 *     function returns @a GLOBUS_SUCCESS, begins sending an unregister request
 *     to the job named by @a job_contact and schedules the function pointed to
 *     by the @a register_callback parameter to be called.
 *     If an error occurs,
 *     globus_gram_client_register_job_callback_unregistration() returns an
 *     integer error code indicating why it can't process the unregister
 *     request.
 *
 * @retval GLOBUS_GRAM_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED
 *     Protocol error
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_JOB_CONTACT
 *    Invalid job contact
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *    Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *    No resources
 *
 * @see globus_gram_client_job_callback_unregister()
 */
int 
globus_gram_client_register_job_callback_unregistration(
    const char *                        job_contact,
    const char *                        callback_contact,
    globus_gram_client_attr_t           attr,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    void *                              register_callback_arg)
{
    int                                 rc;
    char *                              request;
    globus_l_gram_client_monitor_t *    monitor;

    GLOBUS_L_CHECK_IF_INITIALIZED;

    monitor = malloc(sizeof(globus_l_gram_client_monitor_t));
    if(!monitor)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
    }
    globus_l_gram_client_monitor_init(
            monitor,
            NULL,
            register_callback,
            NULL,
            register_callback_arg);

    /* 'unregister' = 10, a space and null  */
    request = (char *) malloc( 
                          strlen(callback_contact)
                          + 10 + 1 + 1 );
    if (request == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto error_exit;
    }

    globus_libc_sprintf(request,
                        "unregister %s",
                        callback_contact);

    rc = globus_l_gram_client_to_jobmanager(
            job_contact,
            request,
            attr,
            GLOBUS_GRAM_CLIENT_CALLBACK_UNREGISTER,
            monitor);

error_exit:
    if(rc != GLOBUS_SUCCESS)
    {
        globus_l_gram_client_monitor_destroy(monitor);
        free(monitor);
    }
    if(request != NULL)
    {
        free(request);
    }

    return rc;
}
/* globus_gram_client_register_job_callback_unregistration() */

/**
 * @brief Begin listening for job state change callbacks
 * @ingroup globus_gram_client_callback
 *
 * @details
 * The globus_gram_client_callback_allow() function initializes a GRAM protocol
 * service in the current process which will process job state updates from
 * GRAM Job Managers. The URL to contact this service is returned and
 * may be used with the globus_gram_client_job_request() or
 * globus_gram_client_callback_register() family of functions.
 *
 * @param callback_func
 *     A pointer to a function to call when a new job state update is
 *     received.
 * @param user_callback_arg
 *     A pointer to application-specific data which is passed to the
 *     function pointed to by @a callback_func as its @a user_callback_arg
 *     parameter.
 * @param callback_contact
 *     An output parameter that points to a string that will be allocated 
 *     and set to the URL that the GRAM callback listener is waiting on.
 *
 * @return 
 *     Upon success, globus_gram_client_callback_allow() returns 
 *     @a GLOBUS_SUCCESS opens a TCP port to accept job state updates and
 *     modifies the value pointed to by the @a callback_contact parameter 
 *     as described above. If an error occurs,
 *     globus_gram_client_callback_allow() returns an integer error code.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *     Null parameter
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *     Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *     No resources
 */
int 
globus_gram_client_callback_allow(
    globus_gram_client_callback_func_t callback_func,
    void * user_callback_arg,
    char ** callback_contact)
{
    int                                 rc;
    globus_l_gram_client_callback_info_t *
                                        callback_info;

    GLOBUS_L_CHECK_IF_INITIALIZED;

    if (callback_contact == NULL)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;
    }

    *callback_contact = NULL;

    callback_info = malloc(
                        sizeof(globus_l_gram_client_callback_info_t));

    if (callback_info == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto error_exit;
    }

    callback_info->callback = callback_func;
    callback_info->info_callback = NULL;
    callback_info->callback_arg = user_callback_arg;

    rc = globus_gram_protocol_allow_attach(
            &callback_info->callback_contact,
            globus_l_gram_client_callback,
            callback_info);

    if (rc != GLOBUS_SUCCESS)
    {
        goto free_callback_info_exit;
    }

    globus_mutex_lock(&globus_l_mutex);
    rc = globus_hashtable_insert(&globus_l_gram_client_contacts,
                            callback_info->callback_contact,
                            callback_info);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto disallow_exit;
    }
    globus_mutex_unlock(&globus_l_mutex);

    *callback_contact = strdup(callback_info->callback_contact);

    if (*callback_contact == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto remove_from_hashtable_exit;
    }
    return rc;

remove_from_hashtable_exit:
    globus_mutex_lock(&globus_l_mutex);
    globus_hashtable_remove(&globus_l_gram_client_contacts,
            (void *) callback_info->callback_contact);
    globus_mutex_unlock(&globus_l_mutex);

disallow_exit:
    globus_gram_protocol_callback_disallow(callback_info->callback_contact);
    free(callback_info->callback_contact);
free_callback_info_exit:
    free(callback_info);

error_exit:
    return rc;
} /* globus_gram_client_callback_allow() */

/**
 * @brief Begin listening for job state change callbacks
 * @ingroup globus_gram_client_callback
 *
 * @details
 * The globus_gram_client_info_callback_allow() function initializes a GRAM
 * protocol service in the current process which will process job state updates
 * from GRAM Job Managers. The URL to contact this service is returned and
 * may be used with the globus_gram_client_job_request_with_info() or
 * globus_gram_client_register_job_status_with_info() family of functions.
 *
 * @param callback_func
 *     A pointer to a function to call when a new job state update is
 *     received. The function signature of this parameter supports
 *     GRAM protocol extensions.
 * @param user_callback_arg
 *     A pointer to application-specific data which is passed to the
 *     function pointed to by @a callback_func as its @a user_callback_arg
 *     parameter.
 * @param callback_contact
 *     An output parameter that points to a string that will be allocated 
 *     and set to the URL that the GRAM callback listener is waiting on.
 *
 * @return 
 *     Upon success, globus_gram_client_callback_allow() returns 
 *     @a GLOBUS_SUCCESS opens a TCP port to accept job state updates and
 *     modifies the value pointed to by the @a callback_contact parameter 
 *     as described above. If an error occurs,
 *     globus_gram_client_callback_allow() returns an integer error code.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER
 *     Null parameter
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_INVALID_REQUEST
 *     Invalid request
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Out of memory
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES
 *     No resources
 */
int 
globus_gram_client_info_callback_allow(
    globus_gram_client_info_callback_func_t
                                        callback_func,
    void *                              user_callback_arg,
    char **                             callback_contact)
{
    int                                 rc;
    globus_l_gram_client_callback_info_t *
                                        callback_info;

    GLOBUS_L_CHECK_IF_INITIALIZED;

    if (callback_contact == NULL)
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_NULL_PARAMETER;
    }

    *callback_contact = NULL;

    callback_info = malloc(sizeof(globus_l_gram_client_callback_info_t));

    if (callback_info == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto error_exit;
    }

    callback_info->callback = NULL;
    callback_info->info_callback = callback_func;
    callback_info->callback_arg = user_callback_arg;

    rc = globus_gram_protocol_allow_attach(
            &callback_info->callback_contact,
            globus_l_gram_client_callback,
            callback_info);

    if (rc != GLOBUS_SUCCESS)
    {
        goto free_callback_info_exit;
    }

    globus_mutex_lock(&globus_l_mutex);
    rc = globus_hashtable_insert(&globus_l_gram_client_contacts,
                            callback_info->callback_contact,
                            callback_info);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto disallow_exit;
    }
    globus_mutex_unlock(&globus_l_mutex);

    *callback_contact = strdup(callback_info->callback_contact);

    if (*callback_contact == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto remove_from_hashtable_exit;
    }
    return rc;

remove_from_hashtable_exit:
    globus_mutex_lock(&globus_l_mutex);
    globus_hashtable_remove(&globus_l_gram_client_contacts,
            (void *) callback_info->callback_contact);
    globus_mutex_unlock(&globus_l_mutex);

disallow_exit:
    globus_gram_protocol_callback_disallow(callback_info->callback_contact);
    free(callback_info->callback_contact);
free_callback_info_exit:
    free(callback_info);

error_exit:
    return rc;
} /* globus_gram_client_callback_allow() */


/**
 * @brief Stop listening for job state change callbacks
 * @ingroup globus_gram_client_callback
 *
 * @details
 * The globus_gram_client_callback_disallow() function stops the GRAM protocol
 * handler associated with a callback contact from receiving further messages.
 * After this function returns, no further callbacks for this contact will
 * be called. Furthermore, the network port associated with the protocol
 * handler will be released.
 *
 * This function can only be used to disable a callback contact created in
 * the current process.
 *
 * @param callback_contact
 *     A callback contact string that refers to a protocol handler in
 *     the current process.
 *
 * @return 
 *     Upon success, globus_gram_client_callback_disallow() returns 
 *     @a GLOBUS_SUCCESS, closes the network port associated with the
 *     @a callback_contact parameter and stops further callbacks from 
 *     occurring.  If an error occurs,
 *     globus_gram_client_callback_disallow() returns an integer error code.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_CALLBACK_NOT_FOUND
 *     Callback not found
 */
int 
globus_gram_client_callback_disallow(
    char *                              callback_contact)
{
    int                                 rc;
    globus_l_gram_client_callback_info_t *
                                        callback_info;

    globus_mutex_lock(&globus_l_mutex);

    callback_info = globus_hashtable_remove(
            &globus_l_gram_client_contacts,
            (void *) callback_contact);

    globus_mutex_unlock(&globus_l_mutex);

    if(callback_info != NULL)
    {
        rc = globus_gram_protocol_callback_disallow(callback_contact);

        free(callback_info->callback_contact);
        free(callback_info);
    }
    else
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_CALLBACK_NOT_FOUND;
    }

    return rc;
} /* globus_gram_client_callback_allow() */


/**
 * @brief Free a job contact string
 * @ingroup globus_gram_client
 *
 * @details
 * The globus_gram_client_job_contact_free() function frees a job
 * contact string that was allocated by a call to one of the functions
 * in the globus_gram_client_job_request() family. The free() function
 * can be used in place of this function. After this function returns, the
 * string pointed to by the @a job_contact parameter has an undefined value.
 *
 * @param job_contact
 *     Pointer to a job contact string returned by a GRAM client API function.
 *
 * @return
 *     This function always returns GLOBUS_SUCCESS.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 */
int 
globus_gram_client_job_contact_free(
    char *                              job_contact)
{
    if(globus_l_print_fp)
    {
        globus_libc_fprintf(globus_l_print_fp,
                      "in globus_gram_client_job_contact_free()\n");
    }

    globus_free(job_contact);

    return (0);
} /* globus_gram_client_job_contact_free() */

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
static
int
globus_l_gram_client_job_request(
    const char *                        resource_manager_contact,
    const char *                        description,
    int                                 job_state_mask,
    globus_i_gram_client_attr_t *       iattr,
    const char *                        callback_contact,
    globus_l_gram_client_monitor_t *    monitor)
{
    int                                 rc;
    globus_byte_t *                     query = NULL;
    globus_size_t                       querysize; 
    globus_io_attr_t                    attr;
    char *                              url;
    char *                              dn;
    globus_rsl_t *                      rsl;
    char *                              username = NULL;

    /* The lexer used by the RSL Parser is not reentrant. The lex source
     * is somewhat lost from the rest of CVS, so we can't fix that directly.
     */
    globus_mutex_lock(&globus_l_rsl_mutex);
    rsl = globus_rsl_parse((char *) description);
    globus_mutex_unlock(&globus_l_rsl_mutex);

    if (rsl != NULL)
    {
        char **                         username_value = NULL;
        rc = globus_rsl_param_get(
                rsl,
                GLOBUS_RSL_PARAM_SINGLE_LITERAL,
                GLOBUS_GRAM_PROTOCOL_USER_NAME,
                &username_value);

        if (rc == 0 && username_value != NULL && username_value[0] != NULL)
        {
            username = strdup(username_value[0]);
        }

        if (username_value != NULL)
        {
            free(username_value);
        }

        globus_rsl_free_recursive(rsl);
        rsl = NULL;
    }

    if ((rc = globus_l_gram_client_parse_gatekeeper_contact(
                     resource_manager_contact,
                     NULL,
                     username,
                     &url,
                     &dn )) != GLOBUS_SUCCESS)
    {
        goto globus_gram_client_job_request_parse_failed;
    }

    if ((rc = globus_l_gram_client_setup_gatekeeper_attr( 
                     &attr,
                     (iattr != NULL)
                         ? iattr->credential : GSS_C_NO_CREDENTIAL,
                     (iattr != NULL)
                         ? iattr->delegation_mode
                         : GLOBUS_IO_SECURE_DELEGATION_MODE_LIMITED_PROXY,
                     dn )) != GLOBUS_SUCCESS)
    {
        goto globus_gram_client_job_request_attr_failed;
    }

    if ((rc = globus_gram_protocol_pack_job_request(
                     job_state_mask,
                     callback_contact,
                     description,
                     &query,
                     &querysize)) != GLOBUS_SUCCESS )
    {
        goto globus_gram_client_job_request_pack_failed;
    }

    globus_mutex_lock(&monitor->mutex);
    monitor->type = GLOBUS_GRAM_CLIENT_JOB_REQUEST;
    rc = globus_gram_protocol_post(
                 url,
                 &monitor->handle,
                 &attr,
                 query,
                 querysize,
                 (monitor->callback != NULL || monitor->info_callback != NULL) 
                     ?  globus_l_gram_client_register_callback
                     : globus_l_gram_client_monitor_callback,
                 monitor);
    globus_mutex_unlock(&monitor->mutex);

    if (query)
        free(query);

globus_gram_client_job_request_pack_failed:
    globus_io_tcpattr_destroy (&attr);

globus_gram_client_job_request_attr_failed:
    free(url);
    if (dn)
        free(dn);

globus_gram_client_job_request_parse_failed:
    if (username)
    {
        free(username);
    }
    return rc;
}
/* globus_l_gram_client_job_request() */

static
int 
globus_l_gram_client_ping(
    const char *                        resource_manager_contact,
    globus_i_gram_client_attr_t *       iattr,
    globus_l_gram_client_monitor_t *    monitor)
{
    int                                 rc;
    char *                              url;
    char *                              dn;
    globus_io_attr_t                    attr;

    rc = globus_l_gram_client_parse_gatekeeper_contact(
        resource_manager_contact,
        "ping",
        NULL,
        &url,
        &dn );

    if (rc != GLOBUS_SUCCESS)
    {
        goto globus_gram_client_ping_parse_failed;
    }

    rc = globus_l_gram_client_setup_gatekeeper_attr( 
        &attr,
        (iattr != NULL) ? iattr->credential : GSS_C_NO_CREDENTIAL,
        GLOBUS_IO_SECURE_DELEGATION_MODE_NONE,
        dn );

    if (rc != GLOBUS_SUCCESS)
    {
        goto globus_gram_client_ping_attr_failed;
    }

    globus_mutex_lock(&monitor->mutex);
    monitor->type = GLOBUS_GRAM_CLIENT_PING;

    rc = globus_gram_protocol_post(
                 url,
                 &monitor->handle,
                 &attr,
                 NULL,
                 0,
                 (monitor->callback != NULL)
                     ? globus_l_gram_client_register_callback
                     : globus_l_gram_client_monitor_callback,
                 monitor);
    globus_mutex_unlock(&monitor->mutex);

    globus_io_tcpattr_destroy (&attr);

globus_gram_client_ping_attr_failed:
    free(url);
    if (dn)
        free(dn);

globus_gram_client_ping_parse_failed:
    return rc;
}
/* globus_l_gram_client_ping() */

static
int 
globus_l_gram_client_get_jobmanager_version(
    const char *                        resource_manager_contact,
    globus_i_gram_client_attr_t *       iattr,
    globus_l_gram_client_monitor_t *    monitor)
{
    int                                 rc;
    char *                              url;
    char *                              dn;
    char *                              query;
    size_t                              query_size;
    globus_io_attr_t                    attr;

    rc = globus_l_gram_client_parse_gatekeeper_contact(
        resource_manager_contact,
        NULL,
        NULL,
        &url,
        &dn );

    if (rc != GLOBUS_SUCCESS)
    {
        goto parse_failed;
    }

    rc = globus_l_gram_client_setup_gatekeeper_attr( 
        &attr,
        (iattr != NULL) ? iattr->credential : GSS_C_NO_CREDENTIAL,
        (iattr != NULL)
            ? iattr->delegation_mode
            : GLOBUS_IO_SECURE_DELEGATION_MODE_LIMITED_PROXY,
        dn);

    if (rc != GLOBUS_SUCCESS)
    {
        goto attr_failed;
    }

    rc = globus_gram_protocol_pack_version_request(
         &query,
         &query_size);
    if (rc != GLOBUS_SUCCESS)
    {
        goto pack_failed;
    }

    globus_mutex_lock(&monitor->mutex);
    monitor->type = GLOBUS_GRAM_CLIENT_JOBMANAGER_VERSION;

    rc = globus_gram_protocol_post(
                 url,
                 &monitor->handle,
                 &attr,
                 (globus_byte_t *) query,
                 query_size,
                 (monitor->callback != NULL || monitor->info_callback != NULL)
                     ? globus_l_gram_client_register_callback
                     : globus_l_gram_client_monitor_callback,
                 monitor);

    if (rc == GLOBUS_GRAM_PROTOCOL_ERROR_CONNECTION_FAILED)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_CONTACTING_JOB_MANAGER;
    }
    globus_mutex_unlock(&monitor->mutex);

    free(query);
pack_failed:
    globus_io_tcpattr_destroy (&attr);
attr_failed:
    free(url);
    if (dn)
        free(dn);
parse_failed:
    return rc;
}
/* globus_l_gram_client_get_jobmanager_version() */


static
int
globus_l_gram_client_job_refresh_credentials(
    char *                              job_contact,
    gss_cred_id_t                       creds,
    globus_i_gram_client_attr_t *       attr,
    globus_l_gram_client_monitor_t *    monitor)
{
    int                                 rc;
    globus_byte_t *                     query = NULL;
    globus_size_t                       querysize;
    OM_uint32                           reqflags = 0;

    globus_mutex_lock(&monitor->mutex);

    if (attr)
    {
        switch (attr->delegation_mode)
        {
            case GLOBUS_IO_SECURE_DELEGATION_MODE_NONE:
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_DELEGATION_FAILED;
                goto end;
            case GLOBUS_IO_SECURE_DELEGATION_MODE_LIMITED_PROXY:
                reqflags = GSS_C_DELEG_FLAG |
                           GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG; 
                break;
            case GLOBUS_IO_SECURE_DELEGATION_MODE_FULL_PROXY:
                reqflags = GSS_C_DELEG_FLAG;
                break;
        }
    }
    else
    {
        reqflags = GSS_C_DELEG_FLAG | GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG; 
    }
    reqflags |= GSS_C_GLOBUS_SSL_COMPATIBLE;

    monitor->type = GLOBUS_GRAM_CLIENT_RENEW;

    rc = globus_gram_protocol_pack_status_request(
         "renew",
         &query,
         &querysize);

    if (rc!=GLOBUS_SUCCESS)
      goto end;

    /* Send the request to the job manager, if successful, delegate a
     * credential to the job manager. call back when all is done.
     */
    rc = globus_gram_protocol_post_delegation(
         job_contact,
         &monitor->handle,
         NULL,
         query,
         querysize,
         creds,
         GSS_C_NO_OID_SET,
         GSS_C_NO_BUFFER_SET,
         reqflags,
         0,
         (monitor->callback != NULL)
             ?  globus_l_gram_client_register_callback
             : globus_l_gram_client_monitor_callback,
         monitor);

    if(query)
    {
        free(query);
    }

end:
    globus_mutex_unlock(&monitor->mutex);

    return rc;
}
/* globus_l_gram_client_job_refresh_credentials() */

static
void
globus_l_gram_client_callback(
    void *                              arg,
    globus_gram_protocol_handle_t       handle,
    globus_byte_t *                     buf,
    globus_size_t                       nbytes,
    int                                 errorcode,
    char *                              uri)
{
    globus_l_gram_client_callback_info_t *
                                        info;
    char *                              url = NULL;
    int                                 job_status;
    int                                 failure_code;
    int                                 rc;
    gss_ctx_id_t                        context;
    globus_gram_client_job_info_t       job_info;
    globus_gram_protocol_extension_t *  entry;

    info = arg;
    
    rc = errorcode;

    if (rc != GLOBUS_SUCCESS || nbytes <= 0)
    {
        job_status   = GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED;
        failure_code = rc;

        goto error_out;
    }
    else
    {
        if(globus_gram_protocol_get_sec_context(handle,
                                                &context))
        {
            job_status   = GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED;
            failure_code = GLOBUS_GRAM_PROTOCOL_ERROR_AUTHORIZATION;
        }
        else if(context != GSS_C_NO_CONTEXT &&
                globus_gram_protocol_authorize_self(context)
                == GLOBUS_FALSE)
        {
            job_status   = GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED;
            failure_code = GLOBUS_GRAM_PROTOCOL_ERROR_AUTHORIZATION;
        }
        else if (info->callback != NULL)
        { 
            /* GRAM2-style callback function */
            rc = globus_gram_protocol_unpack_status_update_message(
                buf,
                nbytes,
                &url,
                &job_status,
                &failure_code);
        }
        else if (info->info_callback != NULL)
        {
            memset(&job_info, 0, sizeof(globus_gram_client_job_info_t));

            /* GRAM5-style callback function which adds additional info */
            rc = globus_gram_protocol_unpack_status_update_message_with_extensions(
                    buf,
                    nbytes,
                    &job_info.extensions);

            if (rc != GLOBUS_SUCCESS)
            {
                goto error_out;
            }

            entry = globus_hashtable_lookup(
                    &job_info.extensions,
                    "job-manager-url");
            if (entry != NULL)
            {
                job_info.job_contact = entry->value;
            }

            entry = globus_hashtable_lookup(
                    &job_info.extensions,
                    "status");
            if (entry != NULL)
            {
                job_info.job_state = strtol(entry->value, NULL, 0);
            }

            entry = globus_hashtable_lookup(
                    &job_info.extensions,
                    "failure-code");
            if (entry != NULL)
            {
                job_info.protocol_error_code = strtol(entry->value, NULL, 0);
            }
        }
    }

error_out:
    rc = globus_gram_protocol_reply(handle,
                                    200,
                                    NULL,
                                    0);
    
    if (info->callback)
    {
        info->callback(info->callback_arg,
                       url,
                       job_status,
                       failure_code);
    }
    else if (info->info_callback)
    {
        info->info_callback(info->callback_arg,
                            job_info.job_contact,
                            &job_info);
        globus_gram_protocol_hash_destroy(&job_info.extensions);
    }

    free(url);
}

static
void
globus_l_gram_client_monitor_callback(
    void *                              user_arg,
    globus_gram_protocol_handle_t       handle,
    globus_byte_t *                     message,
    globus_size_t                       msgsize,
    int                                 errorcode,
    char *                              uri)
{
    globus_l_gram_client_monitor_t *    monitor;
    int                                 rc;
    globus_gram_protocol_extension_t *  extension;

    monitor = user_arg;

    globus_mutex_lock(&monitor->mutex);

    monitor->info->protocol_error_code = errorcode;
    monitor->done = GLOBUS_TRUE;

    /* 
     * Connection failed error means "couldn't connect to gatekeeper". For
     * non-job request messages, we were talking to the job manager, so we'll
     * map to another error.
     */
    if(monitor->info->protocol_error_code ==
                GLOBUS_GRAM_PROTOCOL_ERROR_CONNECTION_FAILED &&
       monitor->type != GLOBUS_GRAM_CLIENT_JOB_REQUEST)
    {
        monitor->info->protocol_error_code =
                GLOBUS_GRAM_PROTOCOL_ERROR_CONTACTING_JOB_MANAGER;
    }

    if(!errorcode)
    {
        switch(monitor->type)
        {
          case GLOBUS_GRAM_CLIENT_JOB_REQUEST:
            rc = globus_gram_protocol_unpack_job_request_reply(
                    message,
                    msgsize,
                    &monitor->info->job_state,
                    &monitor->info->job_contact);
            if(rc != GLOBUS_SUCCESS)
            {
                monitor->info->protocol_error_code = rc;
            }
            else
            {
                /* XXX: Why is this? */
                monitor->info->protocol_error_code = monitor->info->job_state;
            }
            break;

          case GLOBUS_GRAM_CLIENT_PING:
          case GLOBUS_GRAM_CLIENT_RENEW:
            break;
          case GLOBUS_GRAM_CLIENT_STATUS:
          case GLOBUS_GRAM_CLIENT_SIGNAL:
          case GLOBUS_GRAM_CLIENT_CANCEL:
          case GLOBUS_GRAM_CLIENT_CALLBACK_REGISTER:
          case GLOBUS_GRAM_CLIENT_CALLBACK_UNREGISTER:
            rc = globus_gram_protocol_unpack_status_reply_with_extensions(
                    message,
                    msgsize,
                    &monitor->info->extensions);
            if(rc != GLOBUS_SUCCESS)
            {
                monitor->info->protocol_error_code = rc;
                break;
            }

            monitor->info->job_state = globus_l_gram_info_get_int(
                    &monitor->info->extensions,
                    "status");

            monitor->info->protocol_error_code = globus_l_gram_info_get_int(
                    &monitor->info->extensions,
                    "failure-code");
            break;
        case GLOBUS_GRAM_CLIENT_JOBMANAGER_VERSION:
            rc = globus_gram_protocol_unpack_message(
                    (const char *) message,
                    msgsize,
                    &monitor->info->extensions);
            if(rc != GLOBUS_SUCCESS)
            {
                monitor->info->protocol_error_code = rc;
                break;
            }
            extension = globus_hashtable_lookup(
                    &monitor->info->extensions,
                    "failure-code");
            if (extension != NULL)
            {
                monitor->info->protocol_error_code = atoi(extension->value);
                break;
            }

            if (globus_hashtable_lookup(
                    &monitor->info->extensions,
                    "toolkit-version") == NULL &&
                globus_hashtable_lookup(
                    &monitor->info->extensions,
                    "version") == NULL)
            {
                extension = globus_hashtable_lookup(
                        &monitor->info->extensions,
                        "status");

                if (extension != NULL)
                {
                    monitor->info->protocol_error_code = atoi(extension->value);
                }
            }
        }
    }
    globus_cond_signal(&monitor->cond);
    globus_mutex_unlock(&monitor->mutex);
}
/* globus_l_gram_client_monitor_callback() */

static
void
globus_l_gram_client_register_callback(
    void *                              user_arg,
    globus_gram_protocol_handle_t       handle,
    globus_byte_t *                     message,
    globus_size_t                       msgsize,
    int                                 errorcode,
    char *                              uri)
{
    globus_l_gram_client_monitor_t *    monitor;
    globus_gram_protocol_extension_t *  extension;
    int                                 rc;

    monitor = user_arg;

    globus_mutex_lock(&monitor->mutex);

    monitor->info->job_state = 0;
    monitor->info->protocol_error_code = errorcode;
    monitor->done = GLOBUS_TRUE;

    /* 
     * Connection failed error means "couldn't connect to gatekeeper". For
     * non-job request messages, we were talking to the job manager, so we'll
     * map to another error.
     */
    if(monitor->info->protocol_error_code ==
                GLOBUS_GRAM_PROTOCOL_ERROR_CONNECTION_FAILED &&
       monitor->type != GLOBUS_GRAM_CLIENT_JOB_REQUEST)
    {
        monitor->info->protocol_error_code =
                GLOBUS_GRAM_PROTOCOL_ERROR_CONTACTING_JOB_MANAGER;
    }
    if(!errorcode)
    {
        switch(monitor->type)
        {
          case GLOBUS_GRAM_CLIENT_JOB_REQUEST:
            rc = globus_gram_protocol_unpack_job_request_reply_with_extensions(
                    message,
                    msgsize,
                    &monitor->info->protocol_error_code,
                    &monitor->info->job_contact,
                    &monitor->info->extensions);
            if(rc != GLOBUS_SUCCESS)
            {
                monitor->info->protocol_error_code = rc;
            }

            break;

          case GLOBUS_GRAM_CLIENT_PING:
          case GLOBUS_GRAM_CLIENT_RENEW:
            break;
          case GLOBUS_GRAM_CLIENT_STATUS:
          case GLOBUS_GRAM_CLIENT_SIGNAL:
          case GLOBUS_GRAM_CLIENT_CANCEL:
          case GLOBUS_GRAM_CLIENT_CALLBACK_REGISTER:
          case GLOBUS_GRAM_CLIENT_CALLBACK_UNREGISTER:
            /* GRAM5-style callback function which adds additional info */
            rc = globus_gram_protocol_unpack_status_reply_with_extensions(
                    message,
                    msgsize,
                    &monitor->info->extensions);
            if (rc != GLOBUS_SUCCESS)
            {
                monitor->info->protocol_error_code = rc;
            }

            monitor->info->job_state = globus_l_gram_info_get_int(
                    &monitor->info->extensions,
                    "status");


            monitor->info->protocol_error_code = globus_l_gram_info_get_int(
                    &monitor->info->extensions,
                    "failure-code");
            break;
        case GLOBUS_GRAM_CLIENT_JOBMANAGER_VERSION:
            rc = globus_gram_protocol_unpack_message(
                    (const char *) message,
                    msgsize,
                    &monitor->info->extensions);
            if (rc != GLOBUS_SUCCESS)
            {
                monitor->info->protocol_error_code = rc;
                break;
            }

            extension = globus_hashtable_lookup(
                    &monitor->info->extensions,
                    "failure-code");
            if (extension != NULL)
            {
                monitor->info->protocol_error_code = atoi(extension->value);
                break;
            }

            if (globus_hashtable_lookup(
                    &monitor->info->extensions,
                    "toolkit-version") == NULL &&
                globus_hashtable_lookup(
                    &monitor->info->extensions,
                    "version") == NULL)
            {
                extension = globus_hashtable_lookup(
                        &monitor->info->extensions,
                        "status");

                if (extension != NULL)
                {
                    monitor->info->protocol_error_code = atoi(extension->value);
                }
            }
        }
    }

    globus_mutex_unlock(&monitor->mutex);

    if (monitor->info_callback)
    {
        monitor->info_callback(
                monitor->callback_arg,
                monitor->info->job_contact,
                monitor->info);
    }
    else
    {
        monitor->callback(
                monitor->callback_arg,
                monitor->info->protocol_error_code,
                monitor->info->job_contact,
                monitor->info->job_state,
                globus_l_gram_info_get_int(
                        &monitor->info->extensions,
                        "job-failure-code"));
    }

    globus_l_gram_client_monitor_destroy(monitor);
    free(monitor);
}
/* globus_l_gram_client_register_callback() */

static
int
globus_l_gram_client_monitor_init(
    globus_l_gram_client_monitor_t *    monitor,
    globus_gram_client_job_info_t *     info,
    globus_gram_client_nonblocking_func_t
                                        register_callback,
    globus_gram_client_info_callback_func_t
                                        info_callback,
    void *                              register_callback_arg)
{
    memset(monitor, '\0', sizeof(globus_l_gram_client_monitor_t));

    globus_mutex_init(&monitor->mutex, NULL);
    globus_cond_init(&monitor->cond, NULL);
    monitor->done = GLOBUS_FALSE;
    monitor->callback = register_callback;
    monitor->info_callback = info_callback;
    monitor->callback_arg = register_callback_arg;

    if (info)
    {
        memset(info, 0, sizeof(globus_gram_client_job_info_t));
        monitor->info = info;
    }
    else
    {
        monitor->info = calloc(1, sizeof(globus_gram_client_job_info_t));
    }

    return GLOBUS_SUCCESS;
}
/* globus_l_gram_client_monitor_init() */

static
int
globus_l_gram_client_monitor_destroy(
    globus_l_gram_client_monitor_t *    monitor)
{

    globus_gram_client_job_info_destroy(monitor->info);
    free(monitor->info);

    globus_mutex_destroy(&monitor->mutex);
    globus_cond_destroy(&monitor->cond);

    return GLOBUS_SUCCESS;
}
/* globus_l_gram_client_monitor_destroy() */

static
int
globus_l_gram_info_get_int(
    globus_hashtable_t *                extensions,
    const char *                        key)
{
    globus_gram_protocol_extension_t *  entry = NULL;

    if (extensions && *extensions)
    {
        entry = globus_hashtable_lookup(extensions, (void *) key);
    }

    if (entry)
    {
        return (int) strtol(entry->value, NULL, 10);
    }
    else
    {
        return 0;
    }
}
/* globus_l_gram_info_get_int() */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

/**
 * @brief Free memory associated with a globus_gram_client_job_info_t structure
 * @ingroup globus_gram_client
 *
 * @details
 * The globus_gram_client_job_info_destroy() function frees data pointed
 * to by the @a extensions and @a job_contact fields of the
 * @a globus_gram_client_job_info_t structure pointed to by the 
 * @a info parameter. 
 *
 * @param info
 *     A structure containing data to free.
 */
void
globus_gram_client_job_info_destroy(
    globus_gram_client_job_info_t *     info)
{
    if (!info)
    {
        return;
    }
    if (info->extensions)
    {
        globus_gram_protocol_hash_destroy(&info->extensions);
    }
    if (info->job_contact)
    {
        free(info->job_contact);
    }
}
/* globus_gram_client_job_info_destroy() */
