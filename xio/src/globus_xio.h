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

#if !defined(GLOBUS_XIO_H)
#define GLOBUS_XIO_H

#include "globus_common.h"
#include "globus_xio_types.h"
#include "globus_xio_util.h"
#include "globus_xio_load.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus XIO
 * @copydoc globus_xio
 */
#endif

/**
 * @defgroup globus_xio Globus XIO
 * @brief eXtensible Input Output library
 *
 * - @ref GLOBUS_XIO_API
 * - @ref GLOBUS_XIO_API_ASSIST
 * - @ref globus_xio_driver
 */

/**
 *  @defgroup GLOBUS_XIO_API Globus XIO API
 *  @ingroup globus_xio
 *  @brief Globus XIO API
 */

/**
 *  @defgroup GLOBUS_XIO_API_ASSIST XIO Examples
 *  @ingroup globus_xio
 *  @brief XIO Examples
 *
 *  Help understanding the globus_xio api.
 */
/**
 * \addtogroup GLOBUS_XIO_API_ASSIST 
 * 
 *  \section globus_xio_stack_construction Stack Construction
 *  The driver stack that is used for a given XIO handle is constructed
 *  using a globus_xio_stack_t.  Each driver is loaded by name 
 *  and pushed onto a stack.
 * 
 *  Stack setup example:
 *  \code
   
    // First load the drivers
    globus_xio_driver_load("tcp", &tcp_driver);
    globus_xio_driver_load("gsi", &gsi_driver);
   
    //build the stack
    globus_xio_stack_init(&stack);
    globus_xio_stack_push_driver(stack, tcp_driver, NULL);
    globus_xio_stack_push_driver(stack, gsi_driver, NULL);
    \endcode
 */
/**
 *  \addtogroup GLOBUS_XIO_API_ASSIST
 *
 *  \section globus_xio_servers Servers
 *  A server data structure provides functionality for passive opens.
 *  A server is initialized and bound to a protocol stack and set
 *  of attributes with the function globus_xio_server_create().  Once
 *  a server is created many "connections" can be accepted.  Each connection
 *  will result in an intialized handle which can later be opened.
 *
 *  \code
  
    globus_xio_server_t             server;
    globus_xio_attr_t               attr;
  
    globus_xio_attr_init(&attr);
    globus_xio_server_create(&server_handle, attr, stack); 
    globus_xio_server_accept(&handle, server);
  
    \endcode
 */

/**
 *  \addtogroup GLOBUS_XIO_API_ASSIST
 *
 *  \section globus_xio_handle_construction Handle Construction
 *  There are two ways to create a handle.  The first is for use as a 
 *  client (one that is doing an active open).  The function:
 *  globus_xio_handle_create() is used to create such a handle and bind
 *  that handle to a protocol stack.
 *
 *  \code
    globus_xio_handle_create(&handle, stack);
    \endcode
 *
 *  The second means of creating a handle is for use as a server (one
 *  that is doing a passive open).  This is created by accepting a
 *  connection on a server_handle with the function globus_xio_server_accept()
 *  or globus_xio_server_register_accept().
 *
 *  Mutable attrs can be altered
 *  via a call to globus_xio_handle_cntl() described later.
 *
 *  \code
    globus_xio_server_accept(&xio_handle, server_handle);
    \endcode
 * 
 *  once a handle is initialized the user can call globus_xio_open()
 *  to begin the open process.
 */

/**
 * @addtogroup GLOBUS_XIO_API_ASSIST
 *
 * \section globus_xio_timeouts Timeouts
 * A user can set a timeout value for any I/O operation.  Each I/O 
 * operation (open close read write) can have its own timeout value.
 * If no timeout is set the operation will be allowed to infinitely
 * block.
 *
 * When time expires the outstanding operation is canceled.  If the
 * timeout callback for the given operation is not NULL it is called first to
 * notify the user that the operation timed out and give the user a chance to
 * ignore that timeout.  If canceled, the user will get the callback they 
 * registered for the operation as well, but it will come with an error
 * indicating that it has been canceled.
 *
 * It is possible that part of an I/O operation will complete before
 * the timeout expires.  In this case the operation can still be 
 * canceled.  The user will receive there IO callback with and 
 * error set and the length value appropriately set to indicate how
 * much of the operation completed.
 *
 * Timeouts are registered by using the following handle_cntl values:
 *
 * - GLOBUS_XIO_ATTR_SET_TIMEOUT_ALL
 * - GLOBUS_XIO_ATTR_SET_TIMEOUT_OPEN
 * - GLOBUS_XIO_ATTR_SET_TIMEOUT_CLOSE
 * - GLOBUS_XIO_ATTR_SET_TIMEOUT_READ
 * - GLOBUS_XIO_ATTR_SET_TIMEOUT_WRITE
 * - GLOBUS_XIO_ATTR_SET_TIMEOUT_ACCEPT
 *
 * Each of these cntls expect three parameters:
 *
 * - globus_xio_timeout_callback_t      callback_function
 * - globus_reltime_t *                 delay_time
 * - void *                             callback_arg
 */

/**
 * @addtogroup GLOBUS_XIO_API_ASSIST
 *
 *  \section data_descriptor Data Descriptor
 *  The data descriptor ADT gives the user a means of attaching/extracting
 *  meta data to a read or write operation.\n
 *  Things like offset, out of band message, and other driver specific
 *  meta data are contained in the data descriptor.\n
 *  Data descriptors are passed to globus_xio in globus_xio_read() and 
 *  globus_xio_write().  Within the globus_xio framework
 *  it is acceptable to pass NULL instead of a valid data_descriptor,
 *
 *  Example:
 *  \code
    globus_xio_data_descriptor_init(&desc);
    globus_xio_data_descriptor_cntl(desc, 
        tcp_driver,
        GLOBUS_XIO_TCP_SET_SEND_FLAGS,
        GLOBUS_XIO_TCP_SEND_OOB);
    \endcode
 */

/*************************************************************************
 *    define types
 ************************************************************************/

/**
 * \addtogroup GLOBUS_XIO_API_ASSIST
 *
 *  \section user_attributes User Attributes
 *  Globus XIO uses a single attribute object for all of its functions.
 *  Attributes give the user an extensible mechanism to alter default
 *  values which control parameters in an operation.
 *
 *  In most of the Globus XIO user API functions a user passes an 
 *  attribute as a parameter.  In many cases the user may ignore the
 *  attribute parameter and just pass in NULL.  However at times the user
 *  will wish to tweak the operation.  The attribute structure is used for
 *  this tweaking.
 *
 *  There are only three attribute functions. @ref globus_xio_attr_init 
 *  @ref globus_xio_attr_cntl and @ref globus_xio_attr_destroy.  The
 *  init and destroy functions are very simple and require little explanation.
 *  Before an attribute can be used it must be initialized, and to clean up all
 *  memory associated with it the user must call destroy on it.
 *
 *  The function @ref globus_xio_attr_cntl manipulates values in the
 *  attribute.  For more info on it see @ref globus_xio_attr_cntl.
 */

/**
 *  \ingroup GLOBUS_XIO_API
 *  Initialize a Globus XIO attribute.
 *
 *  @param attr
 *         upon return from this function this out parameter will be 
 *         initialized.  Once the user is finished with the attribute
 *         they should make sure they destroy it in order to free 
 *         resources associated with it.
 */
globus_result_t
globus_xio_attr_init(
    globus_xio_attr_t *                 attr);

/**
 *  @ingroup GLOBUS_XIO_API
 *  Manipulate the values associated in the attr.
 *
 *  This function provides a means to access the attr structure.  What
 *  exactly this function does is determined by the value in the parameter
 *  cmd and the value of the parameter driver.  When the driver parameter
 *  is NULL it indicates that this function applies to general Globus XIO
 *  values.  If it is not NULL it indicates that the function will effect 
 *  driver specific values.  Each driver is responsible for defining its own
 *  enumeration of values for cmd and the var args associated with that 
 *  command.  
 *
 *  @param attr
 *          the attribute structure to be manipulated.
 *
 *  @param driver
 *          This parameter indicates which driver the user would like
 *          to perform the requested operation.  If this parameter is 
 *          NULL this request will be scoped to general attribute functions.
 *
 *  @param cmd
 *         an enum that determines what specific operation the user is 
 *         requesting.  Each driver will determine the value for this 
 *         enumeration.
 */
globus_result_t
globus_xio_attr_cntl(
    globus_xio_attr_t                   attr,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    ...);

/**
 *  @ingroup GLOBUS_XIO_API
 *
 *  Copy an attribute structure.
 */
globus_result_t
globus_xio_attr_copy(
    globus_xio_attr_t *                 dst,
    globus_xio_attr_t                   src);

/**
 *  @ingroup GLOBUS_XIO_API
 *  Clean up resources associated with an attribute.
 *
 *  @param attr
 *         Upon completion of this function all resources associated
 *         with this structure will returned to the system and the attr
 *         will no longer be valid.
 */
globus_result_t
globus_xio_attr_destroy(
    globus_xio_attr_t                   attr);

/*************************************************************************
 *                      Stack functions
 *                      ---------------
 ************************************************************************/

/**
 *  @ingroup GLOBUS_XIO_API
 *  Initialize a stack object 
 */
globus_result_t
globus_xio_stack_init(
    globus_xio_stack_t *                stack,
    globus_xio_attr_t                  stack_attr);

/**
 *  @ingroup GLOBUS_XIO_API
 *  Push a driver onto a stack.
 *
 *  No attrs are associated with a driver. The stack represents the
 *  ordered lists of transform drivers and 1 transport driver.  The
 *  transport driver must be pushed on first.
 */
globus_result_t
globus_xio_stack_push_driver(
    globus_xio_stack_t                  stack,
    globus_xio_driver_t                 driver);

/**
 *  @ingroup GLOBUS_XIO_API
 *  Copy a stack object
 */
globus_result_t
globus_xio_stack_copy(
    globus_xio_stack_t *                dst,
    globus_xio_stack_t                  src);

/**
 *  @ingroup GLOBUS_XIO_API
 *  Destroy a stack object.
 */
globus_result_t
globus_xio_stack_destroy(
    globus_xio_stack_t                  stack);

/*-------------------------------------------------------------------------
 *  server 
 *-----------------------------------------------------------------------*/
/**
 *  @ingroup GLOBUS_XIO_API
 *  Callback signature for accept.
 *
 *  When a registered accept operation completes the users function of
 *  this signature is called.
 *
 *  @param server
 *         The server object on which the accept was registered.
 *
 *  @param handle
 *         The newly created handle that was created by the accept 
 *         operation.
 *
 *  @param result
 *         A result code indicating the success of the accept operation.
 *         GLOBUS_SUCCESS indicates a successful accept.
 *
 *  @param user_arg
 *         A user argument that is threaded from the registration to the
 *         callback.
 */
typedef void
(*globus_xio_accept_callback_t)(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg);

/**
 *  @ingroup GLOBUS_XIO_API
 *  Server callback signature.
 *
 *  This is the generic server callback signature.  It is currently only 
 *  used for the register close operation.
 */
typedef void
(*globus_xio_server_callback_t)(
    globus_xio_server_t                 server,
    void *                              user_arg);

/**
 *  @ingroup GLOBUS_XIO_API
 *  Create a server object.
 *
 *  This function allows the user to create a server object which can then
 *  be used to accept connections.
 *
 *  @param server
 *         An out parameter.  Once the function successfully returns this
 *         will point to a valid server object.
 *
 *  @param server_attr
 *         an attribute structure used to alter the default server 
 *         initialization.  This will mostly be used in a driver specific manner.
 *         can be NULL.
 *
 *  @param stack
 */
globus_result_t
globus_xio_server_create(
    globus_xio_server_t *               server,
    globus_xio_attr_t                   server_attr,
    globus_xio_stack_t                  stack);

/**
 *  @ingroup GLOBUS_XIO_API
 *  get contact string
 *
 *  This function allows the user to get the contact string for a server.
 *  this string could be used as the contact string for the client side.
 *
 *  @param server
 *         An initialized server handle created with globus_xio_server_create()
 *
 *  @param contact_string
 *         an out variable.  Will point to a newly allocated string on success.
 *         must be freed by the caller.
 */
globus_result_t
globus_xio_server_get_contact_string(
    globus_xio_server_t                 server,
    char **                             contact_string);

/**
 *  @ingroup GLOBUS_XIO_API
 *  post a close on a server object
 *
 *  This function registers a close operation on a server.  When the user
 *  function pointed to by parameter cb is called the server object is closed.
 */
globus_result_t
globus_xio_server_register_close(
    globus_xio_server_t                 server,
    globus_xio_server_callback_t        cb,
    void *                              user_arg);

/**
 *  @ingroup GLOBUS_XIO_API
 *  A blocking server close
 */
globus_result_t
globus_xio_server_close(
    globus_xio_server_t                 server);

/**
 *  @ingroup GLOBUS_XIO_API
 *  Touch driver specific information in a server object.
 *
 *  This function allows the user to communicate directly with a driver
 *  in association with a server object.  The driver defines what operations
 *  can be preformed.
 */
globus_result_t
globus_xio_server_cntl(
    globus_xio_server_t                 server,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    ...);

/**
 *  @ingroup GLOBUS_XIO_API
 *  Accept a connection
 *
 *  This function will accept a connection on the given server object
 *  and the parameter out_handle will be valid if the function returns
 *  successfully.
 */
globus_result_t
globus_xio_server_accept(
    globus_xio_handle_t *               out_handle,
    globus_xio_server_t                 server);

/**
 *  @ingroup GLOBUS_XIO_API
 *  Asynchronous accept.
 *
 *  This function posts an nonblocking accept.  Once the operation has
 *  completed the user function pointed to by the parameter cb is called.
 */
globus_result_t
globus_xio_server_register_accept(
    globus_xio_server_t                 server,
    globus_xio_accept_callback_t        cb,
    void *                              user_arg);

/**
 *  @ingroup GLOBUS_XIO_API
 *  Initialize a handle for client opens
 *
 *  This function will initialize a handle for active opens (client side 
 *  connections).
 * 
 */
globus_result_t
globus_xio_handle_create(
    globus_xio_handle_t *               handle,
    globus_xio_stack_t                  stack);

/******************************************************************
 *                      handle construction
 *****************************************************************/

/******************************************************************
 *                      setting timeout values
 *****************************************************************/

/**
 *  @ingroup GLOBUS_XIO_API
 *  The timeout callback function signature.
 *
 *  @param handle
 *         The handle the handle on which the timeout operation was 
 *         requested.
 *
 *  @param type
 *         The type of operation that timed out:
 *         GLOBUS_XIO_OPERATION_OPEN
 *         GLOBUS_XIO_OPERATION_CLOSE
 *         GLOBUS_XIO_OPERATION_READ
 *         GLOBUS_XIO_OPERATION_WRITE
 *
 *  @param arg
 *         A user arg threaded through to the callback.
 */

typedef globus_bool_t
(*globus_xio_timeout_callback_t)(
    globus_xio_handle_t                 handle,
    globus_xio_operation_type_t         type,
    void *                              user_arg);

typedef globus_bool_t
(*globus_xio_timeout_server_callback_t)(
    globus_xio_server_t                 server,
    globus_xio_operation_type_t         type);


/******************************************************************
 *                      data descriptor
 *****************************************************************/

/**
 *  Initialize a data descriptor
 *  @ingroup GLOBUS_XIO_API
 *
 *  @param data_desc
 *         An out parameter.  The data descriptor to be initialized.
 *
 *  @param handle
 *         The handle this data descriptor will be used with.  This
 *         parameter is require in order to optimize the code 
 *         handling the data descriptors use.
 */
globus_result_t
globus_xio_data_descriptor_init(
    globus_xio_data_descriptor_t *      data_desc,
    globus_xio_handle_t                 handle);

/**
 *  @ingroup GLOBUS_XIO_API
 *  clean up a data descriptor.
 */
globus_result_t
globus_xio_data_descriptor_destroy(
    globus_xio_data_descriptor_t        data_desc);

/**
 *  Touch driver specific data in data descriptors
 *  @ingroup GLOBUS_XIO_API
 * 
 *  This function allows the user to communicate directly with a driver
 *  in association with a data descriptors.  The driver defines what operations
 *  can be preformed.
 */
globus_result_t
globus_xio_data_descriptor_cntl(
    globus_xio_data_descriptor_t        data_desc,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    ...);

/*********************************************************************
 *                         callbacks
 ********************************************************************/
/**
 *  globus_xio_callback_t
 *  @ingroup GLOBUS_XIO_API
 *
 *   This callback is used for the open and close asynchronous
 *   operations.
 */
typedef void (*globus_xio_callback_t)(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg);

/**
 *  globus_xio_data_callback_t 
 *  @ingroup GLOBUS_XIO_API
 *
 *  This callback is used for asynchronous operations that send or receive
 *  data.
 *
 *  On EOF, result_t will be of type GLOBUS_XIO_ERROR_EOF
 */
typedef void (*globus_xio_data_callback_t)(
    globus_xio_handle_t                 handle, 
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes, 
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

/**
 *  globus_xio_iovec_callback_t 
 *  @ingroup GLOBUS_XIO_API
 *
 *  This callback is used for asynchronous operations that send or receive
 *  data with an iovec structure.
 *
 *  On EOF, result_t will be of type GLOBUS_XIO_ERROR_EOF
 */
typedef void (*globus_xio_iovec_callback_t)(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

/**
 *  @ingroup GLOBUS_XIO_API
 *  Touch driver specific information in a handle object.
 *
 *  This function allows the user to communicate directly with a driver
 *  in association with a handle object.  The driver defines what operations
 *  can be preformed.
 * 
 *  pass the driver to control a specific driver
 *  pass NULL for driver for XIO specific cntls
 *  pass GLOBUS_XIO_QUERY for driver to try each driver in order until success
 */
globus_result_t
globus_xio_handle_cntl(
    globus_xio_handle_t                 handle,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    ...);

/**
 * Open a handle
 *  @ingroup GLOBUS_XIO_API
 *
 * Creates an open handle based on the state contained in the given
 * stack.
 * 
 * No operation can be preformed on a handle until it is initialized 
 * and then opened.  If 
 * an already open handle used the information contained in that handle
 * will be destroyed.
 * 
 * @param handle
 *     The handle created with @ref globus_xio_handle_create() or 
 *     @ref globus_xio_server_register_accept() that is to be opened.
 * 
 * @param attr
 *         how to open attribute.  can be NULL
 *
 * @param cb
 *         The function to be called when the open operation completes.
 *
 * @param user_arg
 *         A user pointer that will be threaded through to the callback.
 * 
 * @param contact_string
 *     An url describing the resource.  NULL is allowed.
 *     Drivers interpret the various parts
 *     of this url as described in their documentation.  An alternative
 *     form is also supported:  if contact_string does not specify a scheme
 *     (e.g. http://) and it contains a ':', it will be parsed as a host:port
 *     pair.  if it does not contain a ':', it will be parsed as the path
 * 
 *  the following are examples of valid formats:
 *  <pre>
 *    \<path to file\>
 *    host-name ":" \<service or port\>
 *    "file:" \<path to file\>
 *    \<scheme\> "://" [ "/" [ \<path to resource\> ]  ]
 *    \<scheme\> "://" location [ "/" [ \<path to resource\> ] ]
 *      location:
 *          [ auth-part ] host-part
 *      auth-part:
 *          \<user\> [ ":" \<password\> ] "@" 
 *      host-part:
 *          [ "<" \<subject\> ">" ] host-name [ ":" \<port or service\> ]
 *      host-name:
 *          \<hostname\> | \<dotted quad\> | "[" \<ipv6 address\> "]"
 *    </pre>
 *
 *    Except for use as the above delimiters, the following special characters
 *    MUST be encoded with the \%HH format where H == hex char.
 *    
 *    <pre>
 *    "/" and "@" in location except subject
 *    "<" and ">" in location
 *    ":" everywhere except ipv6 address and subject
 *    "%" everywhere (can be encoded with \%HH or \%%)
 *    </pre>
 */
globus_result_t
globus_xio_register_open(
    globus_xio_handle_t                 handle,
    const char *                        contact_string,
    globus_xio_attr_t                   attr,
    globus_xio_callback_t               cb,
    void *                              user_arg);

/**
 *  Blocking open
 *  @ingroup GLOBUS_XIO_API
 */
globus_result_t
globus_xio_open(
    globus_xio_handle_t                 handle,
    const char *                        contact_string,
    globus_xio_attr_t                   attr);

/**
 *  Read data from a handle
 *  @ingroup GLOBUS_XIO_API
 */
globus_result_t
globus_xio_register_read(
    globus_xio_handle_t                 handle,
    globus_byte_t *                     buffer,
    globus_size_t                       buffer_length,
    globus_size_t                       waitforbytes,
    globus_xio_data_descriptor_t        data_desc,
    globus_xio_data_callback_t          cb,
    void *                              user_arg);

/**
 *  Read data from a handle
 *  @ingroup GLOBUS_XIO_API
 */
globus_result_t
globus_xio_read(
    globus_xio_handle_t                 handle,
    globus_byte_t *                     buffer,
    globus_size_t                       buffer_length,
    globus_size_t                       waitforbytes,
    globus_size_t *                     nbytes,
    globus_xio_data_descriptor_t        data_desc);

/**
 * Read data from a handle into a globus_xio_iovec_t (struct iovec)
 *  @ingroup GLOBUS_XIO_API
 */
globus_result_t
globus_xio_register_readv(
    globus_xio_handle_t                 handle,
    globus_xio_iovec_t *                iovec,
    int                                 iovec_count,
    globus_size_t                       waitforbytes,
    globus_xio_data_descriptor_t        data_desc,
    globus_xio_iovec_callback_t         cb,
    void *                              user_arg);

/**
 * Read data from a handle into a globus_xio_iovec_t (struct iovec)
 *  @ingroup GLOBUS_XIO_API
 */
globus_result_t
globus_xio_readv( 
    globus_xio_handle_t                 handle,
    globus_xio_iovec_t *                iovec,
    int                                 iovec_count,
    globus_size_t                       waitforbytes,
    globus_size_t *                     nbytes,
    globus_xio_data_descriptor_t        data_desc);

/**
 * Write data to a handle
 *  @ingroup GLOBUS_XIO_API
 */
globus_result_t
globus_xio_register_write(
    globus_xio_handle_t                 handle,
    globus_byte_t *                     buffer,
    globus_size_t                       buffer_length,
    globus_size_t                       waitforbytes,
    globus_xio_data_descriptor_t        data_desc,
    globus_xio_data_callback_t          cb,
    void *                              user_arg);

/**
 * Write data to a handle
 *  @ingroup GLOBUS_XIO_API
 */
globus_result_t
globus_xio_write(
    globus_xio_handle_t                 handle,
    globus_byte_t *                     buffer,
    globus_size_t                       buffer_length,
    globus_size_t                       waitforbytes,
    globus_size_t *                     nbytes,
    globus_xio_data_descriptor_t        data_desc);

/**
 * Write data to a handle from a globus_xio_iovec_t (struct iovec)
 *  @ingroup GLOBUS_XIO_API
 */
globus_result_t
globus_xio_register_writev(
    globus_xio_handle_t                 handle,
    globus_xio_iovec_t *                iovec,
    int                                 iovec_count,
    globus_size_t                       waitforbytes,
    globus_xio_data_descriptor_t        data_desc,
    globus_xio_iovec_callback_t         cb,
    void *                              user_arg);

/**
 * Write data to a handle from a globus_xio_iovec_t (struct iovec)
 *  @ingroup GLOBUS_XIO_API
 */
globus_result_t
globus_xio_writev(
    globus_xio_handle_t                 handle,
    globus_xio_iovec_t *                iovec,
    int                                 iovec_count,
    globus_size_t                       waitforbytes,
    globus_size_t *                     nbytes,
    globus_xio_data_descriptor_t        data_desc);


/**
 *  Cancel outstanding operations
 */
globus_result_t
globus_xio_handle_cancel_operations(
    globus_xio_handle_t                 handle,
    int                                 mask);

globus_result_t
globus_xio_server_cancel_accept(
    globus_xio_server_t                 server);

/**
 *  Close a handle
 *  @ingroup GLOBUS_XIO_API
 * 
 *  This functions servers as a destroy for the handle.  As soon as the
 *  operations completes (the callback is called).  The handle is
 *  destroyed.
 *
 *  @param handle
 *         the handle to be closed.
 *
 *  @param attr
 *         how to close attribute
 *
 *  @param cb
 *         The function to be called when the close operation completes.
 *
 *  @param user_arg
 *         A user pointer that will be threaded through to the callback.
 */
globus_result_t
globus_xio_register_close(
    globus_xio_handle_t                 handle,
    globus_xio_attr_t                   attr,
    globus_xio_callback_t               cb,
    void *                              user_arg);

/**
 *  Blocking close
 *  @ingroup GLOBUS_XIO_API
 */
globus_result_t
globus_xio_close(
    globus_xio_handle_t                 handle,
    globus_xio_attr_t                   attr);

/*
 *                         signal stuff
 *  TODO:
 *  Signals are not implemented in the first release.
 *  =================================================
 *
 *  Signals in Globus XIO give the user a means of requesting 
 *  notification when given things in the system change.  
 *
 *  For example:
 *  It may be useful for the user of globus_xio to know when/if the 
 *  optimal post buffer size changes.  The optimal post buffer size is
 *  a value that tells the user the best possible length of a buffer to 
 *  post for an I/O operation.  This value is mutable and can be changed
 *  by events internal to globus_xio of which the user is unaware.
 *  The signal API allows the user to register for notification of when
 *  this value changes.
 *
 *  Globus XIO enumerates the signal types for which the user can register.  
 *  One of these types is GLOBUS_XIO_DRIVER_SPECIFIC.  This type allows
 *  the user to catch driver specific signals.  A driver specific signal
 *  example could be when the TCP window size changes.  Only a TCP driver
 *  can be aware of this information so only it can send the signal.  
 *  Further a user only knows to listen for that signal if it knows that
 *  tcp is in its driver stack.
 *
 *  Once a signal is delivered to the globus_xio user the handle can be 
 *  queried for specific information regarding that signal.
 *  Event Types.
 *  @ingroup GLOBUS_XIO_API
 *  
 *  An enumeration of the signal types of which globus_xio is aware.  
enum globus_xio_signal_type_t
{
    GLOBUS_XIO_SIGNAL_BUFFER_POST_SIZE,
    GLOBUS_XIO_SIGNAL_OPTIMAL_BUFFER_SIZE,
    GLOBUS_XIO_SIGNAL_DRIVER_SPECIFIC,
};
 *
 *  Signal Callback
 *  @ingroup GLOBUS_XIO_API
 *
 *  The callback signature for signal events.
 *
 *  @param handle
 *         The handle associated with the event.
 * 
 *  @param signal_type
 *         The type of signal that occurred.
 *
 *  @param driver
 *         The driver that caused this event.  If it is not a driver
 *         specific signal than this will be NULL.
typedef void
(*globus_xio_signal_callback_t)(
    globus_xio_handle_t                     handle,
    globus_xio_signal_type_t                signal_type,
    globus_xio_driver_t                     driver);
 *
 *  Register a signal listener.
 *  @ingroup GLOBUS_XIO_API
 *
 *  Request notification when event change in the system relating
 *  to a given handle.
 *
 *  @param handle
 *         The handle on which the user would like to receive 
 *         notifications.
 *
 *  @param signal_mask
 *         A mask of the signals to be observed.
 *
 *  @param driver
 *         The driver to which the signal mask applies.  If this is for a
 *         non driver specific event this will be null.  This function
 *         must be called once for every driver of interest.
 *
 *  @param callback
 *         The function to be called when the given events occur.
 *
 *  @param user_arg
 *         A user pointed threaded through to the callback.
globus_result_t
globus_xio_handle_register_signal_handler(
    globus_xio_handle_t                     handle,
    int                                     signal_mask,
    globus_xio_driver_t                     driver,
    globus_xio_signal_callback_t            callback,
    void *                                  user_arg);

 *  Register a signal listener.
 *  @ingroup GLOBUS_XIO_API
 *
 *  Request notification when event change in the system relating
 *  to a given factory.
 *
 *  @param factory
 *         The factory on which the user would like to receive 
 *         notifications of events.
 *
 *  @param signal_mask
 *         A mask of the signals to be observed.
 *
 *  @param driver
 *         The driver to which the signal mask applies.  If this is for a
 *         non driver specific event this will be null.  This function
 *         must be called once for every driver of interest.
 *
 *  @param callback
 *         The function to be called when the given events occur.
 *
 *  @param user_arg
 *         A user pointed threaded through to the callback.
globus_result_t
globus_xio_server_register_signal_handler(
    globus_xio_server_t                     factory,
    int                                     signal_mask,
    globus_xio_driver_t                     driver,
    globus_xio_callback_t                   callback,
    void *                                  user_arg);
 */


globus_xio_driver_list_ent_t *
globus_xio_driver_list_find_driver(
    globus_list_t *                     driver_list,
    const char *                        driver_name);

void
globus_xio_driver_list_destroy(
    globus_list_t *                     driver_list,
    globus_bool_t                       unload);

globus_result_t
globus_xio_driver_list_to_stack_attr(
    globus_list_t *                     driver_list,
    globus_xio_stack_t                  stack,
    globus_xio_attr_t                   attr);

globus_result_t
globus_xio_driver_list_from_string(
    char *                              driver_string,
    globus_list_t **                    driver_list,
    globus_hashtable_t *                safe_table);

globus_result_t
globus_xio_driver_safe_table_from_string(
    char *                              driver_string,
    globus_hashtable_t *                safe_table);

globus_result_t
globus_xio_driver_list_create_ent(
    const char *                        driver_desc,
    globus_xio_driver_t                 driver_in,
    globus_bool_t                       load,
    globus_xio_driver_list_ent_t **     ent_out);

extern globus_module_descriptor_t       globus_i_xio_module;
#define GLOBUS_XIO_MODULE &globus_i_xio_module

#define _XIOSL(s) globus_common_i18n_get_string( \
		     GLOBUS_XIO_MODULE, \
		     s)

/**
 *  @ingroup GLOBUS_XIO_API
 *  Initializes a handle based on the scheme given.
 *
 *  @param out_h
 *         An uninitialized handle that will be initialized in the function
 *         to correspond to the scheme given. This handle should be used
 *         for any I/O operations.
 *
 *  @param scheme
 *         A string containing the protocol which the handle should be
 *         initialized to. The string can either be a protocol by itself,
 *         for example, "http", or a complete scheme such as 
 *         "http://www.example.com".
 *
 *  @param attr
 *         Attribute to be used for setting parameter string. It is
 *         initialized by the function. Can be NULL if attributes
 *         are not being used.
 *
 *  @param param_string
 *         A string containing attributes to be set for the drivers
 *         associated with the scheme. This should be in the form
 *         "protocol1:option1=value1;option2=value2,protocol2:option1=value1;
 *         option2=value2" Can be NULL if attributes are not being used.
 */

globus_result_t
globus_xio_handle_create_from_url(
    globus_xio_handle_t *           out_h,
    const char *                    scheme,
    globus_xio_attr_t               attr,
    char *                          param_string);

#ifdef __cplusplus
}
#endif

#endif
