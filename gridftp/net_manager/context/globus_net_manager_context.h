/*
 * Copyright 1999-2014 University of Chicago
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

#ifndef GLOBUS_NET_MANAGER_CONTEXT_H
#define GLOBUS_NET_MANAGER_CONTEXT_H 1

/**
 * @file globus_net_manager_context.h
 * @brief Globus Network Manager Context
 */

#include "globus_common.h"
#include "globus_net_manager.h"
#include "globus_net_manager_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_net_manager_context Context
 * @ingroup globus_net_manager
 * @brief Net Manager Context Management Functions
 *
 * @details
 * The Net Manager Context manages a collection of network
 * manager plug-ins which will be called when network events occur. 
 *
 * Initialize the context by calling globus_net_manager_context_init().
 * This processes an array of attributes. The "scope" member of
 * of attributes indicate the name of a network manager to associate
 * with the context. The (name, value) tuples of the attributes
 * are added to the attributes passed to all of that particular
 * network manager's implementation functions.
 *
 * Once the network manager context is created, pass it to the 
 * per-operation invocation functions to trigger all of the manager
 * callouts associated with the context.
 *
 * When the network manager context is no longer needed, destroy
 * it by calling globus_net_manager_context_destroy().
 */


/**
 * @brief Net Manager Context
 * @ingroup globus_net_manager_context
 * @details
 * A structure of this type is used to thread attributes through the set
 * of configured net manager modules and invoke their functions in
 * configuration order.
 */
typedef struct globus_i_net_manager_context_s *globus_net_manager_context_t;

globus_result_t
globus_net_manager_context_init(
    globus_net_manager_context_t       *context,
    const globus_net_manager_attr_t    *attrs);

void
globus_net_manager_context_destroy(
    globus_net_manager_context_t        context);

globus_result_t
globus_net_manager_context_pre_listen(
    globus_net_manager_context_t        context,
    const char                         *task_id,
    const char                         *transport,
    const globus_net_manager_attr_t    *attr_array,
    globus_net_manager_attr_t         **attr_array_out);

globus_result_t
globus_net_manager_context_post_listen(
    globus_net_manager_context_t        context,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const globus_net_manager_attr_t    *attr_array,
    char                              **local_contact_out,
    globus_net_manager_attr_t         **attr_array_out);

globus_result_t
globus_net_manager_context_end_listen(
    globus_net_manager_context_t        context,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const globus_net_manager_attr_t    *attr_array);

globus_result_t
globus_net_manager_context_pre_accept(
    globus_net_manager_context_t        context,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const globus_net_manager_attr_t    *attr_array,
    globus_net_manager_attr_t         **attr_array_out);

globus_result_t
globus_net_manager_context_post_accept(
    globus_net_manager_context_t        context,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array,
    globus_net_manager_attr_t         **attr_array_out);

globus_result_t
globus_net_manager_context_pre_connect(
    globus_net_manager_context_t        context,
    const char                         *task_id,
    const char                         *transport,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array,
    char                              **remote_contact_out,
    globus_net_manager_attr_t         **attr_array_out);

globus_result_t
globus_net_manager_context_post_connect(
    globus_net_manager_context_t        context,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array,
    globus_net_manager_attr_t         **attr_array_out);

globus_result_t
globus_net_manager_context_pre_close(
    globus_net_manager_context_t        context,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array);

globus_result_t
globus_net_manager_context_post_close(
    globus_net_manager_context_t        context,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array);


typedef struct globus_i_net_manager_context_s
{
    globus_list_t *                     managers;
} globus_i_net_manager_context_t;

typedef struct globus_i_net_manager_context_entry_s
{
    char *                              name;
    char *                              dll_name;
    globus_extension_handle_t           ext_handle;
    globus_net_manager_t *              manager;
    globus_net_manager_attr_t *         attrs;
} globus_i_net_manager_context_entry_t;

#ifdef __cplusplus
}
#endif

#endif /* #define GLOBUS_NET_MANAGER_CONTEXT_H */
