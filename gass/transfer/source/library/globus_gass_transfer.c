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
 * @file globus_gass_transfer.c
 * @brief GASS transfer module descriptor
 *
 * This module implements the GASS transfer module descriptor
 */
#endif

#include "globus_i_gass_transfer.h"
#include "version.h"

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
globus_hashtable_t globus_i_gass_transfer_protocols;
globus_handle_table_t globus_i_gass_transfer_request_handles;
globus_handle_table_t globus_i_gass_transfer_listener_handles;
globus_list_t * globus_i_gass_transfer_requests = GLOBUS_NULL;
globus_list_t * globus_i_gass_transfer_listeners = GLOBUS_NULL;
globus_mutex_t globus_i_gass_transfer_mutex;
globus_cond_t globus_i_gass_transfer_shutdown_cond;
globus_bool_t globus_i_gass_transfer_deactivating = GLOBUS_FALSE;

static
int
globus_l_gass_transfer_activate(void);

static
int
globus_l_gass_transfer_deactivate(void);

globus_module_descriptor_t globus_i_gass_transfer_module =
{
    "globus_gass_transfer",
    globus_l_gass_transfer_activate,
    globus_l_gass_transfer_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

static
void
globus_l_gass_transfer_listener_close_callback(
    void *                                      user_arg,
    globus_gass_transfer_listener_t             request);

static
int
globus_l_gass_transfer_activate(void)
{
    globus_module_activate(GLOBUS_COMMON_MODULE);

    globus_hashtable_init(&globus_i_gass_transfer_protocols,
                          16,
                          globus_hashtable_string_hash,
                          globus_hashtable_string_keyeq);

    globus_handle_table_init(
        &globus_i_gass_transfer_request_handles,
        GLOBUS_NULL);
    globus_handle_table_init(
        &globus_i_gass_transfer_listener_handles,
        GLOBUS_NULL);

    globus_module_activate(GLOBUS_I_GASS_TRANSFER_HTTP_MODULE);

#if 0
    /* we don't want to build this in
     */
    globus_module_activate(GLOBUS_I_GASS_TRANSFER_FTP_MODULE);
#endif

    globus_gass_transfer_proto_register_protocol(
        &globus_i_gass_transfer_http_descriptor);
    globus_gass_transfer_proto_register_protocol(
        &globus_i_gass_transfer_https_descriptor);

#if 0
    /* we don't want to build this in
     */
    globus_gass_transfer_proto_register_protocol(
        &globus_i_gass_transfer_ftp_descriptor);
    globus_gass_transfer_proto_register_protocol(
        &globus_i_gass_transfer_gsiftp_descriptor);
#endif

    globus_mutex_init(&globus_i_gass_transfer_mutex,
                      GLOBUS_NULL);
    globus_cond_init(&globus_i_gass_transfer_shutdown_cond,
                     GLOBUS_NULL);

    return GLOBUS_SUCCESS;
}
/* globus_l_gass_transfer_activate() */

static
int
globus_l_gass_transfer_deactivate(void)
{
    globus_list_t *                             rest;

    globus_i_gass_transfer_lock();
    globus_i_gass_transfer_deactivating = GLOBUS_TRUE;

#if DEBUG_GASS_TRANSFER
    printf(_GTSL("Entering globus_l_gass_transfer_deactivate()\n"));
#endif

    rest = globus_i_gass_transfer_requests;

    while(!globus_list_empty(rest))
    {
        globus_gass_transfer_request_t          tmp;
        globus_gass_transfer_request_struct_t * req;
        int                                     rc;

        tmp = (globus_gass_transfer_request_t) (intptr_t)
            globus_list_first(rest);

        rest = globus_list_rest(rest);

        req = globus_handle_table_lookup(
            &globus_i_gass_transfer_request_handles,
            tmp);

#if DEBUG_GASS_TRANSFER
        printf(_GTSL("failing: %s\n"), req->url);
#endif
        rc = globus_i_gass_transfer_fail(
            tmp,
            req,
            globus_i_gass_transfer_deactivate_callback,
            GLOBUS_NULL);

        if (rc == GLOBUS_GASS_TRANSFER_ERROR_DONE)
        {
            rc = globus_i_gass_transfer_request_destroy(tmp);
            globus_assert(rc == 0);
        }
    }

    rest = globus_i_gass_transfer_listeners;

    while(!globus_list_empty(rest))
    {
        globus_gass_transfer_listener_t         tmp;
        globus_gass_transfer_listener_struct_t *l;
        int                                     rc;

        tmp = (globus_gass_transfer_listener_t) (intptr_t)
            globus_list_first(rest);

        rest = globus_list_rest(rest);

        l = globus_handle_table_lookup(
            &globus_i_gass_transfer_listener_handles,
            tmp);

        rc = globus_i_gass_transfer_close_listener(
            tmp,
            l,
            globus_l_gass_transfer_listener_close_callback,
            GLOBUS_NULL);
    }

    while((!globus_list_empty(globus_i_gass_transfer_requests)) ||
          (!globus_list_empty(globus_i_gass_transfer_listeners)))
    {
#if DEBUG_GASS_TRANSFER
        printf(_GTSL("waiting for requests\n"));
#endif
        globus_cond_wait(&globus_i_gass_transfer_shutdown_cond,
                         &globus_i_gass_transfer_mutex);
    }

#if 0
    /* we don't want to build this in
     */
    globus_gass_transfer_proto_unregister_protocol(
        &globus_i_gass_transfer_ftp_descriptor);
    globus_gass_transfer_proto_unregister_protocol(
        &globus_i_gass_transfer_gsiftp_descriptor);
#endif

    globus_gass_transfer_proto_unregister_protocol(
        &globus_i_gass_transfer_http_descriptor);
    globus_gass_transfer_proto_unregister_protocol(
        &globus_i_gass_transfer_https_descriptor);


#if 0
    /* we don't want to build this in
     */
    globus_module_deactivate(GLOBUS_I_GASS_TRANSFER_FTP_MODULE);
#endif

    globus_i_gass_transfer_unlock();

    globus_module_deactivate(GLOBUS_I_GASS_TRANSFER_HTTP_MODULE);

    globus_handle_table_destroy(&globus_i_gass_transfer_listener_handles);
    globus_handle_table_destroy(&globus_i_gass_transfer_request_handles);


    globus_hashtable_destroy(&globus_i_gass_transfer_protocols);

#if !defined(BUILD_LITE)
    globus_mutex_destroy(&globus_i_gass_transfer_mutex);
#endif

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

#if DEBUG_GASS_TRANSFER
    printf(_GTSL("Exiting globus_l_gass_transfer_deactivate()\n"));
#endif

    return GLOBUS_SUCCESS;
}
/* globus_l_gass_transfer_deactivate() */

void
globus_i_gass_transfer_deactivate_callback(
    void *                                      user_arg,
    globus_gass_transfer_request_t              request)
{
    globus_i_gass_transfer_request_destroy(request);
}

static
void
globus_l_gass_transfer_listener_close_callback(
    void *                                      user_arg,
    globus_gass_transfer_listener_t             request)
{
    return;
}
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
