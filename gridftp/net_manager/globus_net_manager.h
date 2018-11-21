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

#ifndef GLOBUS_NET_MANAGER_H
#define GLOBUS_NET_MANAGER_H 1

#include "globus_common.h"
#include "globus_net_manager_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file globus_net_manager.h
 * @brief Globus Net Manager Interface
 */

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Overview
 * The Globus Net Manager library is a plug-in point for network
 * management tasks, such as:
 * - selectively open ports in a firewall and allow these ports to be closed
 *   when transfers are complete
 * - configure a virtual circuit based on a site policy and route traffic
 *   over this circuit
 * - route network traffic related to a task over a particular network
 *
 * @section globus_net_manager_implementor Implementing a Network Manager
 * For users interested in implementing such functionality,
 * the @link globus_net_manager globus_net_manager library @endlink provides a
 * low-level set of interfaces to implement specific network managers.
 *
 * These interfaces provide methods for registering functions to be called
 * before and after interesting network operations. Those functions may
 * observe or modify the attributes of the network operations. This information
 * can be used to modify the port to listen on, or modify the
 * addresses used to use a particular network interface.
 *
 * In addition, the globus_net_manager library includes sample
 * implementations to provide a starting point for implementing
 * network managers.
 * <dl>
 * <dt>Logging Manager</dt>
 * <dd>Logs network operations as they occur. This implementation
 * shows the simplest network manager implementation in C</dd>
 * <!--<dt>Exec Manager</dt>
 * <dd>Launches a command when network operations occur. This implementation
 * enables network managers to be implemented in any external process.</dd>-->
 * <dt>@ref globus_net_manager_python<dt>
 * <dd>Loads a python module, and calls python functions when network
 * operations occur.</dd>
 * </dl>
 *
 * The Logging Manager sample is broken down in detail in the
 * @ref globus_net_manager_tutorial page.
 *
 * @section globus_net_manager_user Using Network Managers
 * For users interested in using the network manager in their own services,
 * they can use the @ref globus_net_manager_context APIs to configure and
 * invoke network manager plug-ins, or the
 * @link globus_xio_net_manager_driver Globus XIO Net Manager Driver @endlink
 * to plug the network manager interface directly into the Globus XIO stack.
 *
 * To configure a GridFTP server to use the network manager, use the
 * configuration option '-xnetmgr' ('xnetmgr' in the config file). The
 * parameters to this option are a list of managers and their configuration
 * attributes. See the @ref globus_net_manager_python documentation for an
 * example.
 */
#endif

struct globus_net_manager_s;

/**
 * @defgroup globus_net_manager Net Manager
 * @brief Net Manager API
 *
 * @details
 * This API defines the plug-in points for network manager functionality,
 * as well as functions for registering a particular network manager
 * implementation so that it can be invoked during network events.
 *
 * To implement a network manager, define a struct #globus_net_manager_s
 * with the functions you want called, and register it by calling
 * globus_net_manager_register(). More details are included in
 * @ref globus_net_manager_tutorial
 */

/**
 * @defgroup globus_net_manager_types Net Manager Data Types
 * @ingroup globus_net_manager
 * @brief Net Manager Data Types
 *
 * @details
 * Data types for the Net Manager interface.
 * @ref globus_net_manager_tutorial
 */

/**
 * @defgroup globus_net_manager_signatures Function Signatures
 * @ingroup globus_net_manager
 * @brief Net Manager Function Signatures
 *
 * Function signatures for the Net Manager interface.
 * @ref globus_net_manager_tutorial
 */
/**
 * @brief Net Manager Pre-Listen Function Signature
 * @ingroup globus_net_manager_signatures
 * @details
 * A function of this signature, if included in a network manager
 * implementation, is called before the transport-specific listening port has
 * been created.
 *
 * The network manager is passed the network transport-specific options
 * for the listener. It may modify these before the
 * listener is created by the transport.
 *
 * The globus_net_manager library aborts the listen operation
 * if this function returns a value other than GLOBUS_SUCCESS. In this
 * case, the globus_net_manager will not call any other
 * globus_net_manager_pre_listen functions configured for this listener.
 *
 * @param[in] manager
 *      Pointer to the network manager struct that is being invoked.
 * @param[in] manager_attr_array
 *      An array of attributes associated with the
 *      manager. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[in] task_id
 *      An application-specific task ID associated with this network operation.
 * @param[in] transport
 *      The name of the transport associated with this listener.
 * @param[in] attr_array
 *      An array of transport attributes associated with the
 *      listener. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[out] attr_array_out
 *      A pointer to an array of transport options to apply to the
 *      listener prior to returning it to the service. This may be NULL to
 *      indicate no change in the options. If non-NULL, this array must be
 *      terminated with an attribute having a NULL scope.
 *      The array and the members of the
 *      globus_net_manager_attr_t struct will be freed by the
 *      globus_net_manager library by calling
 *      free().
 */
typedef globus_result_t (*globus_net_manager_pre_listen)(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const globus_net_manager_attr_t    *attr_array,
    globus_net_manager_attr_t         **attr_array_out);

/**
 * @brief Net Manager Post-Listen Function Signature
 * @ingroup globus_net_manager_signatures
 * @details
 * A function of this signature, if included in a network manager
 * implementation, is called after the transport-specific listening port has
 * been created.
 *
 * The network manager is passed the network transport-specific options and
 * contact string for the listener. It may modify either of these before the
 * transport listener is made available to the service which requested
 * the listening port.
 *
 * The globus_net_manager library aborts the listen operation
 * if this function returns a value other than GLOBUS_SUCCESS. In this
 * case, the globus_net_manager will not call any other
 * globus_net_manager_post_listen functions configured for this listener.
 *
 * @param[in] manager
 *      Pointer to the network manager struct that is being invoked.
 * @param[in] manager_attr_array
 *      An array of attributes associated with the
 *      manager. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[in] task_id
 *      An application-specific task ID associated with this network operation.
 * @param[in] transport
 *      The name of the transport associated with this listener.
 * @param[in] local_contact
 *      The transport-specific contact string for the listener [in].
 * @param[in] attr_array
 *      An array of transport attributes associated with the
 *      listener. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[out] local_contact_out
 *      A pointer to the local contact which the network manager wants to
 *      return to the service. This may be NULL to indicate no change in
 *      the contact. This value will be freed() by the globus_net_manager
 *      library.
 * @param[out] attr_array_out
 *      A pointer to an array of transport options to apply to the
 *      listener prior to returning it to the service. This may be NULL to
 *      indicate no change in the options. If non-NULL, this array must be
 *      terminated with an attribute having a NULL scope.
 *      The array and the members of the
 *      globus_net_manager_attr_t struct will be freed by the
 *      globus_net_manager library by calling
 *      free().
 */
typedef globus_result_t (*globus_net_manager_post_listen)(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const globus_net_manager_attr_t    *attr_array,
    char                              **local_contact_out,
    globus_net_manager_attr_t         **attr_array_out);

/**
 * @brief Net Manager End-Listen Function Signature
 * @ingroup globus_net_manager_signatures
 * @details
 * A function of this signature, if included in a network manager
 * implementation, is called when the transport-specific listening port will
 * be closed.
 *
 * The network manager is passed the network transport-specific options and
 * contact string for the listener.
 *
 * The globus_net_manager library aborts the closing of the listening port
 * if this function returns a value other than GLOBUS_SUCCESS. In this
 * case, the globus_net_manager will not call any other
 * globus_net_manager_end_listen functions configured for this listener.
 *
 * @param[in] manager
 *      Pointer to the network manager struct that is being invoked.
 * @param[in] manager_attr_array
 *      An array of attributes associated with the
 *      manager. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[in] task_id
 *      An application-specific task ID associated with this network operation.
 * @param[in] transport
 *      The name of the transport associated with this listener.
 * @param[in] local_contact
 *      The transport-specific contact string for the listener [in].
 * @param[in] attr_array
 *      An array of transport attributes associated with the
 *      listener. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 */
typedef globus_result_t (*globus_net_manager_end_listen)(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const globus_net_manager_attr_t    *attr_array);

/**
 * @brief Net Manager Pre-Accept Function Signature
 * @ingroup globus_net_manager_signatures
 * @details
 * A function of this signature, if included in a network manager
 * implementation, is called before accepting a connection on a
 * transport-specific listening port.
 *
 * The network manager is passed the network transport-specific options and
 * contact string for the listener. It may modify the options before the
 * accept operation is complete.
 *
 * The globus_net_manager library aborts the accept operation
 * if this function returns a value other than GLOBUS_SUCCESS. In this
 * case, the globus_net_manager will not call any other
 * globus_net_manager_pre_accept functions configured for this listener.
 *
 * @param[in] manager
 *      Pointer to the network manager struct that is being invoked.
 * @param[in] manager_attr_array
 *      An array of attributes associated with the
 *      manager. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[in] task_id
 *      An application-specific task ID associated with this network operation.
 * @param[in] transport
 *      The name of the transport associated with this listener.
 * @param[in] local_contact
 *      The transport-specific contact string for the listener.
 * @param[in] attr_array
 *      A NULL-terminated array of transport attributes associated with the
 *      listener.
 * @param[out] attr_array_out
 *      A pointer to an array of transport options to apply to the
 *      listener prior to returning it to the service. This may be NULL to
 *      indicate no change in the options. If non-NULL, this array must be
 *      terminated with an attribute having a NULL scope.
 *      The array and the members of the
 *      globus_net_manager_attr_t struct will be freed by the
 *      globus_net_manager library by calling
 *      free().
 */
typedef globus_result_t (*globus_net_manager_pre_accept)(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const globus_net_manager_attr_t    *attr_array,
    globus_net_manager_attr_t         **attr_array_out);

/**
 * @brief Net Manager Post-Accept Function Signature
 * @ingroup globus_net_manager_signatures
 * @details
 * A function of this signature, if included in a network manager
 * implementation, is called after accepting a connection on a
 * transport-specific listening port.
 *
 * The network manager is passed the network transport-specific options and
 * contact string for both ends of the connection. It may modify the attributes
 * of the local side of the connection.
 *
 * The globus_net_manager library aborts the accept operation
 * if this function returns a value other than GLOBUS_SUCCESS. In this
 * case, the globus_net_manager will not call any other
 * globus_net_manager_post_accept functions configured for this connection.
 *
 * @param[in] manager
 *      Pointer to the network manager struct that is being invoked.
 * @param[in] manager_attr_array
 *      An array of attributes associated with the
 *      manager. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[in] task_id
 *      An application-specific task ID associated with this network operation.
 * @param[in] transport
 *      The name of the transport associated with this listener.
 * @param[in] local_contact
 *      The transport-specific contact string for the local side of the
 *      connection.
 * @param[in] remote_contact
 *      The transport-specific contact string for the remote side of the
 *      connection.
 * @param[in] attr_array
 *      An array of transport attributes associated with the
 *      listener. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[out] attr_array_out
 *      A pointer to an array of transport options to apply to the
 *      listener prior to returning it to the service. This may be NULL to
 *      indicate no change in the options. If non-NULL, this array must be
 *      terminated with an attribute having a NULL scope.
 *      The array and the members of the
 *      globus_net_manager_attr_t struct will be freed by the
 *      globus_net_manager library by calling
 *      free().
 */
typedef globus_result_t (*globus_net_manager_post_accept)(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array,
    globus_net_manager_attr_t         **attr_array_out);

/**
 * @brief Net Manager Pre-Connect Function Signature
 * @ingroup globus_net_manager_signatures
 * @details
 * A function of this signature, if included in a network manager
 * implementation, is called after initiating a connection to a
 * transport-specific listener.
 *
 * The network manager is passed the network transport-specific options and
 * contact string for both ends of the connection. It may modify the attributes
 * of the local side of the connection.
 *
 * The globus_net_manager library aborts the connect operation
 * if this function returns a value other than GLOBUS_SUCCESS. In this
 * case, the globus_net_manager will not call any other
 * globus_net_manager_pre_connect functions configured for this connection.
 *
 * @param[in] manager
 *      Pointer to the network manager struct that is being invoked.
 * @param[in] manager_attr_array
 *      An array of attributes associated with the
 *      manager. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[in] task_id
 *      An application-specific task ID associated with this network operation.
 * @param[in] transport
 *      The name of the transport associated with this listener.
 * @param[in] remote_contact
 *      The transport-specific contact string for the remote side of the
 *      connection.
 * @param[in] attr_array
 *      An array of transport attributes associated with the
 *      listener. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[out] remote_contact_out
 *      A pointer to the remote contact which the network manager wants to
 *      connect to. This may be NULL to indicate no change in
 *      the contact. This value will be freed() by the globus_net_manager
 *      library.
 * @param[out] attr_array_out
 *      A pointer to an array of transport options to apply to the
 *      listener prior to returning it to the service. This may be NULL to
 *      indicate no change in the options. If non-NULL, this array must be
 *      terminated with an attribute having a NULL scope.
 *      The array and the members of the
 *      globus_net_manager_attr_t struct will be freed by the
 *      globus_net_manager library by calling
 *      free().
 */
typedef globus_result_t (*globus_net_manager_pre_connect)(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array,
    char                              **remote_contact_out,
    globus_net_manager_attr_t         **attr_array_out);

/**
 * @brief Net Manager Post-Connect Function Signature
 * @ingroup globus_net_manager_signatures
 * @details
 * A function of this signature, if included in a network manager
 * implementation, is called after establishing a connection to a
 * transport-specific listener.
 *
 * The network manager is passed the network transport-specific options and
 * contact string for both ends of the connection. It may modify the attributes
 * of the local side of the connection.
 *
 * The globus_net_manager library aborts the connect operation
 * if this function returns a value other than GLOBUS_SUCCESS. In this
 * case, the globus_net_manager will not call any other
 * globus_net_manager_post_connect functions configured for this connection.
 *
 * @param[in] manager
 *      Pointer to the network manager struct that is being invoked.
 * @param[in] manager_attr_array
 *      An array of attributes associated with the
 *      manager. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[in] task_id
 *      An application-specific task ID associated with this network operation.
 * @param[in] transport
 *      The name of the transport associated with this listener.
 * @param[in] remote_contact
 *      The transport-specific contact string for the remote side of the
 *      connection.
 * @param[in] attr_array
 *      An array of transport attributes associated with the
 *      listener. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[out] attr_array_out
 *      A pointer to an array of transport options to apply to the
 *      listener prior to returning it to the service. This may be NULL to
 *      indicate no change in the options. If non-NULL, this array must be
 *      terminated with an attribute having a NULL scope.
 *      The array and the members of the
 *      globus_net_manager_attr_t struct will be freed by the
 *      globus_net_manager library by calling
 *      free().
 */
typedef globus_result_t (*globus_net_manager_post_connect)(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array,
    globus_net_manager_attr_t         **attr_array_out);

/**
 * @brief Net Manager Pre-Close Function Signature
 * @ingroup globus_net_manager_signatures
 * @details
 * A function of this signature, if included in a network manager
 * implementation, is called prior to closing a connection.
 *
 * The network manager is passed the network transport-specific options and
 * contact string for both ends of the connection.
 *
 * The globus_net_manager library aborts the close operation
 * if this function returns a value other than GLOBUS_SUCCESS. In this
 * case, the globus_net_manager will not call any other
 * globus_net_manager_pre_close functions configured for this connection.
 *
 * @param[in] manager
 *      Pointer to the network manager struct that is being invoked.
 * @param[in] manager_attr_array
 *      An array of attributes associated with the
 *      manager. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[in] task_id
 *      An application-specific task ID associated with this network operation.
 * @param[in] transport
 *      The name of the transport associated with this listener.
 * @param[in] local_contact
 *      The transport-specific contact string for the local side of the
 *      connection.
 * @param[in] remote_contact
 *      The transport-specific contact string for the remote side of the
 *      connection.
 * @param[in] attr_array
 *      An array of transport attributes associated with the
 *      listener. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 */
typedef globus_result_t (*globus_net_manager_pre_close)(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array);

/**
 * @brief Net Manager Post-Close Function Signature
 * @ingroup globus_net_manager_signatures
 * @details
 * A function of this signature, if included in a network manager
 * implementation, is called after closing a connection.
 *
 * The network manager is passed the network transport-specific options and
 * contact string for both ends of the connection.
 *
 * The globus_net_manager library aborts the close operation
 * if this function returns a value other than GLOBUS_SUCCESS. In this
 * case, the globus_net_manager will not call any other
 * globus_net_manager_pre_close functions configured for this connection.
 *
 * @param[in] manager
 *      Pointer to the network manager struct that is being invoked.
 * @param[in] manager_attr_array
 *      An array of attributes associated with the
 *      manager. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 * @param[in] task_id
 *      An application-specific task ID associated with this network operation.
 * @param[in] transport
 *      The name of the transport associated with this listener.
 * @param[in] local_contact
 *      The transport-specific contact string for the local side of the
 *      connection.
 * @param[in] remote_contact
 *      The transport-specific contact string for the remote side of the
 *      connection.
 * @param[in] attr_array
 *      An array of transport attributes associated with the
 *      listener. The end of the array is indicated by
 *      an attribute containing a NULL scope.
 */
typedef globus_result_t (*globus_net_manager_post_close)(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array);

/**
 * @brief Net Manager Definition
 * @ingroup globus_net_manager_types
 * @details
 */
typedef
struct globus_net_manager_s
{
    /** Name of the network manager */
    const char                         *name;
    /** Pre-listen function implementation */
    globus_net_manager_pre_listen       pre_listen;
    /** Post-listen function implementation */
    globus_net_manager_post_listen      post_listen;
    /** End-listen function implementation */
    globus_net_manager_end_listen       end_listen;
    /** Pre-accept function implementation */
    globus_net_manager_pre_accept       pre_accept;
    /** Post-accept function implementation */
    globus_net_manager_post_accept      post_accept;
    /** Pre-connect function implementation */
    globus_net_manager_pre_connect      pre_connect;
    /** Post-connect function implementation */
    globus_net_manager_post_connect     post_connect;
    /** Pre-close function implementation */
    globus_net_manager_pre_close        pre_close;
    /** Post-close function implementation */
    globus_net_manager_post_close       post_close;
}
globus_net_manager_t;

extern globus_extension_registry_t      globus_i_net_manager_registry;
#define GLOBUS_NET_MANAGER_REGISTRY     &globus_i_net_manager_registry

globus_result_t
globus_net_manager_register(
    globus_net_manager_t               *manager,
    globus_module_descriptor_t         *module);

globus_result_t
globus_net_manager_unregister(
    globus_net_manager_t               *manager);

extern globus_module_descriptor_t       globus_i_net_manager_module;
/**
 * @brief Module descriptor
 * @ingroup globus_net_manager
 * @hideinitializer
 * @details
 * The Globus Net Manager uses Grid Community Toolkit module activation and
 * deactivation. Before any Net Manager functions can be called,
 * the module must be activated, like this:
 * @code
       globus_module_activate(GLOBUS_NET_MANAGER_MODULE);
   @endcode
 *
 * This function returns GLOBUS_SUCCESS if the library
 * is successfully initialized.  Otherwise, an error code is returned,
 * and Net Manager functions should not be subsequently called.
 *
 * Activations are reference counted, so it is safe to activate the
 * module multiple times or in different modules.
 *
 * To deactivate Net Manager, call
 *
 * @code
     globus_module_deactivate(GLOBUS_NET_MANAGER_MODULE);
  @endcode
 *
 * This function should be done once for each time the Net Manager
 * was activated.
 *
 */
#define GLOBUS_NET_MANAGER_MODULE     (&globus_i_net_manager_module)


typedef enum
{
    GLOBUS_NET_MANAGER_ERROR_PARAMETER,
    GLOBUS_NET_MANAGER_ERROR_MEMORY,
    GLOBUS_NET_MANAGER_ERROR_INIT,
    GLOBUS_NET_MANAGER_ERROR_MANAGER,
    GLOBUS_NET_MANAGER_ERROR_WRAPPED
} globus_net_manager_error_type_t;


/* all macros in this file require each function to 'declare' their name with
 * this
 */
#define GlobusNetManagerName(func)
/*
#ifdef __GNUC__
#define GlobusNetManagerName(func) static const char * _netmgr_name __attribute__((__unused__)) = #func
#else
#define GlobusNetManagerName(func) static const char * _netmgr_name = #func
#endif
*/



#define GlobusNetManagerErrorParameter(param_name)                   \
    globus_error_put(                                                \
        globus_error_construct_error(                                \
            GLOBUS_NET_MANAGER_MODULE,                               \
            GLOBUS_NULL,                                             \
            GLOBUS_NET_MANAGER_ERROR_PARAMETER,                      \
            __FILE__,                                                \
            __func__,                                                \
            __LINE__,                                                \
           "Bad parameter, %s",                                      \
            (param_name)))

#define GlobusNetManagerErrorMemory(mem_name)                        \
    globus_error_put(                                                \
        globus_error_construct_error(                                \
            GLOBUS_NET_MANAGER_MODULE,                               \
            GLOBUS_NULL,                                             \
            GLOBUS_NET_MANAGER_ERROR_MEMORY,                         \
            __FILE__,                                                \
            __func__,                                                \
            __LINE__,                                                \
           "Memory allocation failed on %s",                         \
            (mem_name)))

#define GlobusNetManagerErrorInit(manager, explain)                  \
    globus_error_put(                                                \
        globus_error_construct_error(                                \
            GLOBUS_NET_MANAGER_MODULE,                               \
            NULL,                                                    \
            GLOBUS_NET_MANAGER_ERROR_INIT,                           \
            __FILE__,                                                \
            __func__,                                                \
            __LINE__,                                                \
           "An error occurred accessing a module. %s: %s", manager, explain))

#define GlobusNetManagerErrorManager(result, manager, explain)       \
    globus_error_put(                                                \
        globus_error_construct_error(                                \
            GLOBUS_NET_MANAGER_MODULE,                               \
            globus_error_get((result)),                              \
            GLOBUS_NET_MANAGER_ERROR_MANAGER,                        \
            __FILE__,                                                \
            __func__,                                                \
            __LINE__,                                                \
           "A network manager (%s) errored in %s.", manager, explain))

#define GlobusNetManagerErrorWrapFailed(failed_func, result)         \
    globus_error_put(                                                \
        globus_error_construct_error(                                \
            GLOBUS_NET_MANAGER_MODULE,                               \
            globus_error_get((result)),                              \
            GLOBUS_NET_MANAGER_ERROR_WRAPPED,                        \
            __FILE__,                                                \
            __func__,                                                \
            __LINE__,                                                \
           "%s failed.",                                             \
            (failed_func)))

#define GlobusNetManagerErrorWrapFailedWithMessage(result, format, arg) \
    globus_error_put(                                                \
        globus_error_construct_error(                                \
            GLOBUS_NET_MANAGER_MODULE,                               \
            globus_error_get((result)),                              \
            GLOBUS_NET_MANAGER_ERROR_WRAPPED,                        \
            __FILE__,                                                \
            __func__,                                                \
            __LINE__,                                                \
            (format),                                                \
            (arg)))

#define GlobusNetManagerErrorWrapFailedWithMessage2(result, format, arg1, arg2) \
    globus_error_put(                                                \
        globus_error_construct_error(                                \
            GLOBUS_NET_MANAGER_MODULE,                               \
            globus_error_get((result)),                              \
            GLOBUS_NET_MANAGER_ERROR_WRAPPED,                        \
            __FILE__,                                                \
            __func__,                                                \
            __LINE__,                                                \
            (format),                                                \
            (arg1), (arg2)))


#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_NET_MANAGER_H */
