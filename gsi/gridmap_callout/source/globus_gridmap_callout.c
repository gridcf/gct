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
 * @file globus_gridmap_callout.c
 * @brief Globus Gridmap Callout
 * @author Sam Meder
 */

#include "globus_common.h"
#include "gssapi.h"
#include "globus_gss_assist.h"
#include "globus_gsi_credential.h"
#include "globus_gridmap_callout_error.h"
#include "version.h"
#include <stdlib.h>

#endif

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus Gridmap Callout
 * @copydoc globus_gridmap_callout
 */
#endif

/**
 * @defgroup globus_gridmap_callout Globus Gridmap Callout
 * @brief Globus Gridmap Callout
 */

/**
 * @brief Globus Gridmap Callout Function
 * @ingroup globus_gridmap_callout
 * @details
 * This function provides a gridmap lookup in callout form.
 *
 * @param ap
 *        This function, like all functions using the Globus Callout API, is 
 *        passed parameter though the variable argument list facility. The
 *        actual arguments that are passed are:
 *
 *        - The GSS Security context established during service
 *          invocation. This parameter is of type gss_ctx_id_t.
 *        - The name of the service being invoced. This parameter should be
 *          passed as a NUL terminated string. If no service string is
 *          available a value of NULL should be passed in its stead. This
 *          parameter is of type char *
 *        - A NUL terminated string indicating the desired local identity. If
 *          no identity is desired NULL may be passed. In this case the first
 *          local identity that is found will be returned. This parameter is of
 *          type char *.
 *        - A pointer to a buffer. This buffer will contain the mapped (local)
 *          identity (NUL terminated string) upon successful return. This
 *          parameter is of type char *.
 *        - The length of the above mentioned buffer. This parameter is of type
 *          unsigned int.
 *
 * @return
 *        GLOBUS_SUCCESS upon success
 *        A globus result_t structure upon failure (needs to be defined better)
 */
globus_result_t
globus_gridmap_callout(
    va_list                             ap)
{
    gss_ctx_id_t                        context;
    char *                              service;
    char *                              desired_identity;
    char *                              identity_buffer;
    char *                              local_identity;
    unsigned int                        buffer_length;
    globus_result_t                     result = GLOBUS_SUCCESS;
    gss_name_t                          peer;
    gss_buffer_desc                     peer_name_buffer;
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    int                                 rc;
    int                                 initiator;
    FILE *                              debug_file;
    char *                              shared_user_buf;

    
    context = va_arg(ap, gss_ctx_id_t);
    service = va_arg(ap, char *);
    desired_identity = va_arg(ap, char *);
    identity_buffer = va_arg(ap, char *);
    buffer_length = va_arg(ap, unsigned int);

    rc = globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE);

    /* check rc */
    
    rc = globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE);

    /* check rc */

    rc = globus_module_activate(GLOBUS_GRIDMAP_CALLOUT_ERROR_MODULE);

    /* check rc */

    
    if(strcmp(service, "sharing") == 0)
    {
        char *  subject;
        shared_user_buf = va_arg(ap, char *);

        result = globus_gsi_cred_read_cert_buffer(
            shared_user_buf, NULL, NULL, NULL, &subject);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
        peer_name_buffer.value = subject;
        peer_name_buffer.length = strlen(subject);
    }
    else
    {
        major_status = gss_inquire_context(&minor_status,
                                           context,
                                           GLOBUS_NULL,
                                           GLOBUS_NULL,
                                           GLOBUS_NULL,
                                           GLOBUS_NULL,
                                           GLOBUS_NULL,
                                           &initiator,
                                           GLOBUS_NULL);
    
        if(GSS_ERROR(major_status))
        {
            GLOBUS_GRIDMAP_CALLOUT_GSS_ERROR(result, major_status, minor_status);
            goto error;
        }
    
        major_status = gss_inquire_context(&minor_status,
                                           context,
                                           initiator ? GLOBUS_NULL : &peer,
                                           initiator ? &peer : GLOBUS_NULL,
                                           GLOBUS_NULL,
                                           GLOBUS_NULL,
                                           GLOBUS_NULL,
                                           GLOBUS_NULL,
                                           GLOBUS_NULL);
    
        if(GSS_ERROR(major_status))
        {
            GLOBUS_GRIDMAP_CALLOUT_GSS_ERROR(result, major_status, minor_status);
            goto error;
        }
        
        major_status = gss_display_name(&minor_status,
                                        peer,
                                        &peer_name_buffer,
                                        GLOBUS_NULL);
        
        if(GSS_ERROR(major_status))
        {
            GLOBUS_GRIDMAP_CALLOUT_GSS_ERROR(result, major_status, minor_status);
            gss_release_name(&minor_status, &peer);
            goto error;
        }
    
        gss_release_name(&minor_status, &peer);
    }
    debug_file = fopen("gridmap_debug.txt","w");

    fprintf(debug_file,
            "Authorizing for service %s\n",
            service == NULL ? "NULL" : service);
    
    if(desired_identity == NULL)
    {
        rc = globus_gss_assist_gridmap(
            peer_name_buffer.value, 
            &local_identity);
        if(rc != 0)
        {
            GLOBUS_GRIDMAP_CALLOUT_ERROR(
                result,
                GLOBUS_GRIDMAP_CALLOUT_LOOKUP_FAILED,
                ("Could not map %s\n", peer_name_buffer.value));
            gss_release_buffer(&minor_status, &peer_name_buffer);
            goto error;
        }

        if(strlen(local_identity) + 1 > buffer_length)
        {
            GLOBUS_GRIDMAP_CALLOUT_ERROR(
                result,
                GLOBUS_GRIDMAP_CALLOUT_BUFFER_TOO_SMALL,
                ("Local identity length: %d Buffer length: %d\n",
                 strlen(local_identity), buffer_length));
        }
        else
        {
            strcpy(identity_buffer, local_identity);
            fprintf(debug_file, "Mapped %s to %s",
                    peer_name_buffer.value, identity_buffer);
            fclose(debug_file);
        }
        free(local_identity);           
    }
    else
    {
        rc = globus_gss_assist_userok(peer_name_buffer.value,
                                      desired_identity);
        if(rc != 0)
        {
            GLOBUS_GRIDMAP_CALLOUT_ERROR(
                result,
                GLOBUS_GRIDMAP_CALLOUT_LOOKUP_FAILED,
                ("Could not map %s to %s\n",
                 peer_name_buffer.value,
                 desired_identity));
            fprintf(debug_file, "Failed to map %s to %s",
                    peer_name_buffer.value, desired_identity);
        }
        else
        { 
            fprintf(debug_file, "Mapped %s to %s",
                    peer_name_buffer.value, desired_identity);
        }
        fclose(debug_file);
    }

    gss_release_buffer(&minor_status, &peer_name_buffer);

 error:
    
    globus_module_deactivate(GLOBUS_GSI_GSSAPI_MODULE);
    globus_module_deactivate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    
    return result;
}
/* globus_gridmap_callout() */
