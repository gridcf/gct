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

#if !defined(GLOBUS_XIO_DRIVER_H)
#define GLOBUS_XIO_DRIVER_H 1

#include "globus_common.h"
#include "globus_xio_load.h"
#include "globus_common.h"
#include "globus_xio_types.h"
#include "globus_xio.h"
#include "globus_xio_util.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*-**********************************************************************
 *                      attribute macros
 ***********************************************************************/
#define GlobusIXIOAttrGetDS(_out_ds, _in_attr, _in_driver)                  \
do                                                                          \
{                                                                           \
    int                                 _ctr;                               \
    globus_i_xio_attr_t *               _attr;                              \
    globus_xio_driver_t                 _driver;                            \
    globus_i_xio_attr_ent_t *           _entry;                             \
    void *                              _ds = NULL;                         \
                                                                            \
    _attr = (_in_attr);                                                     \
    _driver = (_in_driver);                                                 \
                                                                            \
    if(_in_attr == NULL)                                                    \
    {                                                                       \
        _out_ds = NULL;                                                     \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        _entry = _attr->entry;                                              \
        for(_ctr = 0; _ctr < _attr->ndx && _ds == NULL; _ctr++)             \
        {                                                                   \
            if(_entry[_ctr].driver == _driver)                              \
            {                                                               \
                _ds = _entry[_ctr].driver_data;                             \
            }                                                               \
    }                                                                       \
        _out_ds = _ds;                                                      \
    }                                                                       \
} while(0)
    
/*-*****************************************************************
 *                      driver interface
 ******************************************************************/
/**
 * @defgroup globus_xio_driver Globus XIO Driver
 * @ingroup globus_xio
 * @brief Globus XIO Driver
 *
 * Globus XIO introduces a notion of a driver stack to its API.
 * Within globus_xio every I/O operation must occur on a globus_xio 
 * handle.  Associated with each handle is a stack of drivers.
 * A driver is a modulular piece of code that implements the globus_xio
 * driver interface.  The purpose of a driver is manipulate data passed
 * in by the user in someway.  Each driver in a stack will serve its own 
 * unique purpose.
 * 
 * I/O operations pass from driver to driver, starting at the top of the 
 * stack and ending at the bottom.  When the bottom layer driver finishes 
 * with the operation it signals globus_xio that it has completed.  
 * Completion notification then flows up to the top of the driver stack.
 *
 *  @section driver_types Driver Types
 * 
 *  <dl>
 *     <dt>Transport driver:</dt>
 *     <dd>
 *     A transport driver is one that is responsible for
 *     data communication. For example, a TCP or UDP driver would
 *     transmit data via network sockets, or a file driver would write
 *     data to a file.
 *     
 *     There must be exactly one transport driver in a stack, at its bottom.
 *     A transport driver never passes an operation to another driver in
 *     the stack. Instead, this type of driver relies on globus_xio system
 *     functions to implement data operations.
 *     </dd>
 *
 *     <dt>Transform driver:</dt>
 *     <dd>
 *     A transform driver is any intermediate driver in the stack. A transform 
 *     driver relies on some other drivers on the driver stack to
 *     perform the data transport operation.  An example of a transform driver
 *     would one which implements a network protocol such as http.  This driver
 *     would frame and parse messages, but would rely on other drivers in
 *     the XIO stack.
 *
 *     This allows additional transforms to happen in the XIO stack, such as
 *     layering the http protocol on top of an SSL protocol driver before
 *     transmitting the data via TCP.
 *     </dd>
 *  </dl>
 *
 *  @section driver_api Driver API 
 *  The Globus XIO Driver API is a set of functions and interfaces
 *  to allow a developer to create an XIO driver.
 *  To create a driver the user must implement all of the interface
 *  functions in the driver specification.
 *  There is also a set of functions provided to assist the driver
 *  author in implementing a driver.
 *
 *  @subsection driver_api_quickstart Quick Start
 *  For basic driver needs, the developer will have to pay attention to a
 *  few structures and concepts.
 *
 *  <dl>
 *        <dt><code>globus_xio_operation_t</code></dt>
 *        <dd>
 *        This structure represents a request for an operation.  If
 *        the driver can service the operation it does so and the
 *        calls the appropriate <em>finished</em> function.  If the
 *        driver cannot completely service the operation it can <em>pass</em>
 *        it to the next driver in the stack. As soon as the
 *        operation structure is either finished or passed it is no 
 *        longer valid for use in any other function.
 *        </dd>
 *
 *        <dt><code>globus_xio_driver_handle_t</code></dt>
 *        <dd>
 *        A globus_xio_driver_handle_t represents an open handle to the driver
 *        stack for XIO. The driver obtains a driver_handle by calling 
 *        globus_xio_operation_get_driver_handle().
 *        The driver_handle allows the user to do some
 *        complex things that will be described later.
 *        </dd>
 *    </dl>
 * 
 *  @subsection globus_xio_driver_typical Typical Sequence:
 *  Here is a typical sequence of events for a globus_xio transform
 *  driver. All operations are initiated either by the application calling
 *  a function in the Globus XIO API, or from a driver above the transform
 *  driver in the stack passing an operation to this driver.
 *
 *  <dl>
 *           <dt>Open</dt>
 *           <dd>
 *           XIO calls the globus_xio_driver_transform_open_t function of the
 *           driver, passing it the operation and, in the case of a passive
 *           open, the link from a server accept. The driver typically
 *           allocates a private data structure containing the state it wishes
 *           to associate with this handle, and passes this along with a
 *           callback function pointer to globus_xio_driver_pass_open(). 
 *           This allows the other drivers in the XIO stack to continue to
 *           process the operation.
 *
 *           After the lower drivers in the stack have completed processing
 *           the open, they will call globus_xio_driver_finished_open(),
 *           which will in turn call the callback function which the
 *           driver passed as a parameter to globus_xio_driver_pass_open().
 *           The driver then does any post-open operation needed, and then
 *           finishes the operation by calling
 *           globus_xio_driver_finished_open(), passing the private state
 *           data as the <em>driver_handle</em> parameter.  This state
 *           data will be passed to future operation implementations done 
 *           with this handle.
 *
 *           <dt>Read/Write</dt>
 *           <dd>XIO calls the driver's
 *           globus_xio_driver_read_t or globus_xio_driver_write_t function
 *           with an operation as a parameter and the state date from
 *           the globus_xio_driver_finished_open() call. The driver then 
 *           transforms the data if necessary and then calls
 *           the globus_xio_driver_pass_read() or
 *           globus_xio_driver_pass_write()
 *           which passes the operation down the XIO driver stack. When
 *           the driver below it on the XIO stack calls
 *           globus_xio_driver_finished_read() or
 *           globus_xio_driver_finished_write(), the callback which was
 *           passed is invoked. In that callback, the driver should itself
 *           call globus_xio_driver_finished_read() or
 *           globus_xio_driver_finished_write() to indicate completion.
 *           </dd>
 *
 *           <dt>Close</dt>
 *           <dd>XIO calls the close interface function, passing an
 *            operation and the private driver state.  The driver will call 
 *            globus_xio_driver_pass_close() passing it the operation.
 *            When the close callback is received the driver calls
 *            globus_xio_driver_finished_close() passing it the
 *            operation. At this point, the driver typically frees its
 *            private data, as the handle is no longer valid.
 *            </dd>
 *    </dl>
 *
 *  @section globus_xio_driver_attributes Globus XIO Attributes
 *  Attributes provide a way to pass additional metadata to driver operations.
 *  Most Globus XIO API functions include an attribute parameter. Each driver
 *  implements its own set of attribute handling function. To implement
 *  attributes, a driver must provide functions of these types:
 *
 *  - globus_xio_driver_attr_init_t
 *  - globus_xio_driver_attr_copy_t
 *  - globus_xio_driver_attr_cntl_t
 *  - globus_xio_driver_attr_destroy_t
 *
 *  Pointers to these functions are associated with the driver by calling
 *  globus_xio_driver_set_attr
 *
 *   @section globus_xio_advanced_driver_programming Advanced Driver Programming
 *   The typical driver implementation is describe above.  However globus_xio
 *   allows driver authors to do more advanced things such as initiating
 *   operations on their own.
 *
 *   <dl>
 *   <dt>Read Ahead</dt>
 *   <dd>Once a handle is open, a driver can create operation structures
 *   from it.  A driver can then request I/O from the driver
 *   stack before it receives a call to its own I/O interface
 *   functions, implementing read-ahead functionality. This can be done
 *   as follows:
 *
 *   <ol>
 *   <li>Obtain a driver handle from the open operation by calling
 *   globus_xio_operation_get_driver_handle()</li>
 *   <li>Create an operation by calling globus_xio_driver_operation_create()
 *   and pass it the driver_handle from the previous step</li>
 *   <li>call globus_xio_driver_pass_read() using this operation.  When the
 *   read callback is received, the driver saves the returned data in
 *   its private data and then calls globus_xio_driver_finished_read() on the
 *   operation.</li>
 *   <li>When XIO calls the driver's read interface function,
 *   the driver can immediately call globus_xio_driver_finished_read()
 *   function after updating the iovec structure with the data it had
 *   previously read.</li>
 *   </ol>
 *   </dd>
 *   </dl>
 */

/*******************************************************************
 *                        callbacks
 ******************************************************************/
/**
 * @brief Open and Close Callback Signature
 * @ingroup globus_xio_driver
 * @details
 *  This is the function signature of callbacks for the 
 *  globus_xio_pass_open() and globus_xio_pass_close() functions.
 *
 * @param op
 *     The operation structure associated with the open or the
 *     close requested operation.  The driver should call the 
 *     appropriate finished operation to clean up this structure.
 * @param result
 *     The result of the requested data operation
 * @param user_arg
 *     The user pointer that is threaded through to the callback.
 */
typedef void
(*globus_xio_driver_callback_t)(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg);


/**
 *  Data Callback interface
 *  @ingroup globus_xio_driver
 *
 *  This is the function signature of read and write operation 
 *  callbacks.  
 *
 * @param op
 *         The operation structure associated with the read or write
 *         operation request.  The driver should call the appropriate
 *         finished operation when it receives this operation.
 *
 * @param result
 *         The result of the requested data operation
 *  
 * @param nbytes
 *         the number of bytes read or written
 *
 * @param user_arg
 *         The user pointer that is threaded through to the callback.
 */
typedef void
(*globus_xio_driver_data_callback_t)(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg);


typedef globus_result_t
(*globus_xio_driver_push_driver_t)(
    globus_xio_driver_t                 driver,
    globus_xio_stack_t                  stack);

globus_result_t
globus_xio_driver_attr_cntl(
    globus_xio_operation_t              op,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    ...);

globus_result_t
globus_xio_driver_data_descriptor_cntl(
    globus_xio_operation_t              op,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    ...);

/**
 *  @ingroup globus_xio_driver
 *  Touch driver specific information in a handle object.
 *
 *  pass the driver to control a specific driver
 *  pass NULL for driver for XIO specific cntls
 *  pass GLOBUS_XIO_QUERY for driver to try each driver (below current)
 *    in order
 */
globus_result_t
globus_xio_driver_handle_cntl(
    globus_xio_driver_handle_t          handle,
    globus_xio_driver_t                 driver,
    int                                 cmd,
    ...);

/**
 *  @ingroup globus_xio_driver
 */
typedef globus_result_t
(*globus_xio_driver_get_driver_t)(
    globus_xio_driver_t *               out_driver);
/**
 *  @ingroup globus_xio_driver
 *  Create a driver specific attribute.
 *
 *  The driver should implement this function to create a driver 
 *  specific attribute and return it via the out_attr parameter. 
 * 
 */
typedef globus_result_t
(*globus_xio_driver_attr_init_t)(
    void **                             out_driver_attr);

/**
 *  @ingroup globus_xio_driver
 *  Copy a driver attr.
 *
 *  When this function is called the driver will create a copy of the attr 
 *  in parameter src and place it in the parameter dst.
 */
typedef globus_result_t
(*globus_xio_driver_attr_copy_t)(
    void **                             dst,
    void *                              src);

/**
 *  @ingroup globus_xio_driver
 *  Destroy the driver attr.
 *
 *  Clean up all resources associate with the attr. 
 *
 */
typedef globus_result_t
(*globus_xio_driver_attr_destroy_t)(
    void *                              driver_attr);

/**
 *  @ingroup globus_xio_driver
 *  get or set information in an attr.
 *
 *  The cmd parameter determines what functionality the user is requesting.
 *  The driver is responsible for providing documentation to the user on
 *  all the possible values that cmd can be.
 *
 *  @param driver_attr
 *         The driver specific attr, created by 
 *         globus_xio_driver_attr_init_t.
 *
 *  @param cmd
 *         An integer representing what functionality the user is requesting.
 *
 *  @param ap
 *         variable arguments.  These are determined by the driver and the 
 *         value of cmd.
 */
typedef globus_result_t
(*globus_xio_driver_attr_cntl_t)(
    void *                              driver_attr,
    int                                 cmd,
    va_list                             ap);

/**
 *  @ingroup globus_xio_driver
 *  Initialize a server object
 *
 *  The driver developer should implement this function if their driver
 *  handles server operations (passive opens).  In the TCP driver this 
 *  function creates a TCP socket and calls listen() on it. Unlike all
 *  other XIO driver implementation functions, the globus_xio_server_create()
 *  function begins at the bottom (transport driver) of the stack, and
 *  the globus_xio_driver_pass_server_init() acts like a finished() operation.
 *
 *  @param driver_attr
 *         A driver-specific attribute. This may be NULL.
 *  @param contact_info
 *         Contact information from the stack below this driver.
 *         This is NULL for the transport driver.
 *  @param op
 *         An op which must be passed to globus_xio_driver_pass_server_init()
 *         to pass the server contact information up the XIO driver stack.
 * 
 *  @return
 *         Returning GLOBUS_SUCCESS for this means that 
 *         globus_xio_driver_pass_server_init returned success and
 *         the driver's server-specific data is initialized.
 */
typedef globus_result_t
(*globus_xio_driver_server_init_t)(
    void *                              driver_attr,
    const globus_xio_contact_t *        contact_info,
    globus_xio_operation_t              op);

/**
 * signify that the server has been created with this call.  Must be called
 * within the call to the driver's globus_xio_driver_server_init_t
 * interface.  This call is different than all other pass calls, as it operates
 * from the bottom of the stack to the top.  If it returns an error, the user
 * should destroy their driver_server.
 * 
 *  @param op
 *         The operation passed to the globus_xio_driver_server_init_t
 *         function.
 * 
 *  @param contact_info
 *         The contact info for this driver and the stack below.  If the driver
 *         has nothing to add, it should just pass the one it received on the
 *         interface.
 * 
 *         The memory for this contact_info is only needed for the life of the
 *         call, so it is acceptable for it to be declared on the stack and
 *         it is acceptable to 'steal' pointers from the received contact_info.
 * 
 *  @param driver_server
 *         The driver's server-specific state.  Future calls to the
 *         server_accept, server_cntl, and server_destroy will be passed this
 *         value.
 */
globus_result_t
globus_xio_driver_pass_server_init(
    globus_xio_operation_t              op,
    const globus_xio_contact_t *        contact_info,
    void *                              driver_server);

/**
 *  @ingroup globus_xio_driver
 *  destroy a server.
 *
 *  When this function is called the driver should free up all resources
 *  associated with a server.
 *
 *  @param driver_server
 *         The reference to the internal server that is being declared
 *         invalid with this function call.
 */
typedef globus_result_t
(*globus_xio_driver_server_destroy_t)(
    void *                              driver_server);

/**
 *  @ingroup globus_xio_driver
 *  Accept a server connection
 *
 *  The driver developer should implement this function if their driver 
 *  handles server operations.  Once the accept operation completes, the
 *  connection is established.  The user still has an opportunity to
 *  open the link or destroy it.  They can query the link for 
 *  additional information on which to base the decision to open.
 *
 *  @param driver_server
 *         The server object from which the link connection will be 
 *         accepted.
 *
 *  @param op
 *         The requested operation.  When the driver is finished accepting
 *         the server connection it uses this structure to signal globus_xio 
 *         that it has completed the operation.
 */

typedef globus_result_t
(*globus_xio_driver_server_accept_t)(
    void *                              driver_server,
    globus_xio_operation_t              op);

globus_result_t
globus_xio_driver_pass_accept(
    globus_xio_operation_t              op,
    globus_xio_driver_callback_t        in_cb,
    void *                              in_user_arg);

/**
 *  @ingroup globus_xio_driver
 */
typedef void
(*globus_xio_driver_cancel_callback_t)(
    globus_xio_operation_t              op,
    void *                              user_arg,
    globus_xio_error_type_t             reason);

/**
 *  @ingroup globus_xio_driver
 *  Driver API finished accept
 *
 *  This function should be called to signal globus_xio that it has 
 *  completed the accept operation requested of it.  It will free up 
 *  resources associated with the accept_op and potentially cause xio
 *  to pop the signal up the driver stack.
 *
 *  @param op
 *          The requested accept operation that has completed.
 *
 *  @param driver_link
 *          This is the initialized driver link that is that will be passed to
 *          the open interface when this handle is opened.
 * 
 *  @param result
 *          Return status of the completed operation
 */
void
globus_xio_driver_finished_accept(
    globus_xio_operation_t              op,
    void *                              driver_link,
    globus_result_t                     result);
    
/**
 *  @ingroup globus_xio_driver
 *  Query a server for information.
 *
 *  This function allows a user to request information from a driver
 *  specific server handle.
 *
 *  @param driver_server
 *         the server handle.
 *
 *  @param cmd
 *         An integer telling the driver what operation to preform on this
 *         server handle.
 *
 *  @param ap
 *         variable args.
 */
typedef globus_result_t
(*globus_xio_driver_server_cntl_t)(
    void *                              driver_server,
    int                                 cmd,
    va_list                             ap);


/**
 *  @ingroup globus_xio_driver
 */
typedef globus_result_t
(*globus_xio_driver_link_cntl_t)(
    void *                              driver_link,
    int                                 cmd,
    va_list                             ap);

/**
 *  @ingroup globus_xio_driver
 *  destroy a link
 *
 *  The driver should clean up all resources associated with the link
 *  when this function is called.
 *
 *  @param driver_link
 *         The link to be destroyed.
 */
typedef globus_result_t
(*globus_xio_driver_link_destroy_t)(
    void *                              driver_link);


/**********************************************************************
 *                          Open
 *********************************************************************/

/**
 * @brief Open a handle
 * @ingroup globus_xio_driver
 * @details
 * This is called when a user opens a handle.
 *
 *  @param contact_info
 *         Contains information about the requested resource.  Its members
 *         may all be null (especially when link is not null).  XIO will
 *         destroy this contact info upon return from the interface function
 *    
 *  @param driver_link
 *         Comes from server accept.  Used to link an accepted connection to
 *         an xio handle.  XIO will destroy this object upon the return of
 *         this interface call.
 *
 *  @param driver_attr
 *         A attribute describing how to open.  This points to a piece of 
 *         memory created by the globus_xio_driver_attr_init_t
 *         interface function.
 *
 *  @param op
 *         The open operation.  When the driver is finished opening
 *         the handle, it passes this to globus_xio_driver_finished_open()
 *         to return status up the driver stack to the application.
 *         
 */
typedef globus_result_t
(*globus_xio_driver_transform_open_t)(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op);

/**
 * @ingroup globus_xio_driver
 * @copydoc globus_xio_driver_transform_open_t
 */
typedef globus_result_t
(*globus_xio_driver_transport_open_t)(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op);

/**
 *  Driver API Open
 *  @ingroup globus_xio_driver
 *
 *  This function will pass an open request down the driver stack.
 *  Upon completion of the open operation globus_xio will call the @a cb 
 *  function,  at which point the handle structure will be initialized
 *  and available for use.
 *
 *  As soon as the function returns the handle is valid for creating 
 *  other operations.
 *
 *  @param op
 *         The operation from which the handle will be established.  This
 *         parameter is used to determine what drivers are in the stack and
 *         other such information.
 * 
 *  @param contact_info
 *         The contact info describing the resource the driver below should
 *         open.  This will normally be the same contact info that was
 *         passed in on the open interface.
 * 
 *  @param cb
 *         The function to be called when the open operation is complete.
 *
 *  @param user_arg
 *         a user pointer that will be threaded through to the callback.
 */
globus_result_t
globus_xio_driver_pass_open(
    globus_xio_operation_t              op,
    const globus_xio_contact_t *        contact_info,
    globus_xio_driver_callback_t        cb,
    void *                              user_arg);

/**
 *  Driver API finished open
 *  @ingroup globus_xio_driver
 *
 *  This function should be called to signal globus_xio that it has 
 *  completed the open operation requested of it.  It will free up 
 *  resources associated with the op and potentially cause xio
 *  to pop the signal up the driver stack.
 *
 *  @param driver_handle
 *          The driver specific handle pointer that will be passed to 
 *          future interface function calls.
 *
 *  @param op
 *          The requested open operation that has completed.
 *
 *  @param result
 *          Return status of the completed operation
 */
void
globus_xio_driver_finished_open(
    void *                              driver_handle,
    globus_xio_operation_t              op,
    globus_result_t                     result);

/**********************************************************************
 *                      Context functions
 *********************************************************************/
/**
 *  Driver API Create Operation
 *  @ingroup globus_xio_driver
 *
 *  This function will create an operation from an initialized handle
 *  This operation can then be used for io operations related to the
 *  handle that created them.
 *
 *   @param operation
 *          The operation to be created.  When this function returns
 *          this structure will be populated and available for use
 *          for the driver.
 *
 *   @param handle
 *          The initialized handle representing the user handle from
 *          which the operation will be created.
 *   
 */
globus_result_t
globus_xio_driver_operation_create(
    globus_xio_operation_t *            operation,
    globus_xio_driver_handle_t          handle);

/**
 *  @ingroup globus_xio_driver
 */
void
globus_xio_driver_operation_destroy(
    globus_xio_operation_t              operation);

/**
 *  @ingroup globus_xio_driver
 */
globus_result_t
globus_xio_driver_operation_cancel(
    globus_xio_driver_handle_t          handle,
    globus_xio_operation_t              operation);

/**
 *  Is Operation blocking.
 *  @ingroup globus_xio_driver
 *
 *  If the operation is blocking the driver developer may be able to make
 *  certain optimizations.  The function returns true if the given operation
 *  was created via a user call to a blocking function.
 */
globus_bool_t
globus_xio_driver_operation_is_blocking(
    globus_xio_operation_t              operation);

/**
 *  @ingroup globus_xio_driver
 * 
 *  this call *must* return an GLOBUS_XIO_ERROR_COMMAND error for unsupported
 *  command numbers.   (use GlobusXIOErrorInvalidCommand(cmd))
 * 
 *  Drivers that have reason to support the commands listed at
 *  @ref globus_xio_handle_cmd_t should accept the xio generic cmd numbers
 *  and their driver specific command number.  Do NOT implement those handle
 *  cntls unless you really are the definitive source.
 */
typedef globus_result_t
(*globus_xio_driver_handle_cntl_t)(
    void *                              handle,
    int                                 cmd,
    va_list                             ap);


globus_result_t
globus_xio_driver_merge_handle(
    globus_xio_operation_t              op,
    globus_xio_driver_handle_t          handle);

/**********************************************************************
 *                          Close
 *********************************************************************/
/**
 * @brief Close a handle
 * @ingroup globus_xio_driver
 * @details
 * This closes a handle. Driver implementations should pass the close to the
 * other drivers in the stack by calling globus_xio_pass_close().
 *
 * In the close callback, the driver should clean up the data
 * associated with driver_handle.
 * @param driver_specific_handle
 *     The driver handle to be closed.
 * @param driver_attr
 *     A driver specific attr which may be used to alter how a close
 *     is performed (e.g. caching drivers)
 * @param op
 *     The open operation.  When the driver is finished opening
 *     the handle, it passes this to globus_xio_driver_finished_close()
 *     to return status up the driver stack to the application.
 */
typedef globus_result_t
(*globus_xio_driver_close_t)(
    void *                              driver_specific_handle,
    void *                              driver_attr,
    globus_xio_operation_t              op);

/**
 * @brief Pass the close operation down the driver stack
 * @ingroup globus_xio_driver
 * @details
 * This function will pass a close request down the driver stack.  Upon
 * completion of the close operation globus_xio will call the function
 * pointed to by the cb parameter.
 *
 * @param op
 *     The close operation.
 * @param cb
 *     A pointer to the function to be called once all drivers lower
 *     in the stack have closed.
 * @param callback_arg
 *     A pointer that will be passed to the callback.
 */
globus_result_t
globus_xio_driver_pass_close(
    globus_xio_operation_t              op,
    globus_xio_driver_callback_t        cb,
    void *                              callback_arg);

/**
 *  Driver API finished_close
 *  @ingroup globus_xio_driver
 *
 *  The driver calls this function after completing a close operation
 *  on a driver_handle.  Once this function returns the driver_handle is 
 *  no longer 
 *  valid.
 *
 *  @param op
 *         The close operation that has completed.
 *
 *  @param result
 *          Return status of the completed operation
 */
void
globus_xio_driver_finished_close(
    globus_xio_operation_t              op,
    globus_result_t                     result);

/**********************************************************************
 *                          Read
 *********************************************************************/
/**
 *  Read data from an open handle.
 *  @ingroup globus_xio_driver
 *
 *  This function is called when the user requests to read data from 
 *  a handle.  The driver author shall implement all code needed to for
 *  there driver to complete a read operations.
 *
 *  @param driver_specific_handle
 *          The driver handle from which data should be read.
 *
 *  @param iovec
 *         An io vector pointing to the buffers to be read into.
 *
 *  @param iovec_count
 *         The number if entries in the io vector.
 *
 *  @param op
 *         The requested operation.  When the driver is finished fulfilling
 *         the requested read operation it must use this structure to 
 *         signal globus_xio that the operation is completed.  This is done
 *         by calling globus_xio_driver_finished_operation().
 */
typedef globus_result_t
(*globus_xio_driver_read_t)(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op);

/**
 *  Driver read
 *  @ingroup globus_xio_driver
 *
 *  This function passes a read operation down the driver stack.  After
 *  this function is called the op structure is no longer valid.  However
 *  when the driver stack finishes servicing the read request it will 
 *  pass a new operation structure in the function pointed to by cb.
 *  Finish read can be called on the new operation received.
 *
 *  @param op
 *         The operation structure representing this requested io
 *         operation.
 *
 *  @param iovec
 *         A pointer to the array of iovecs.
 *
 *  @param iovec_count
 *         The number of iovecs in the array.
 *
 *  @param wait_for
 *         The minimum number of bytes to read before returning. if a driver
 *         has no specific requirement, he should use the user's request
 *         available via GlobusXIOOperationMinimumRead(op)
 *  @param cb
 *         The function to be called when the operation request is 
 *         completed.
 *
 *  @param user_arg
 *         A user pointer that will be threaded through to the callback.
 */
globus_result_t
globus_xio_driver_pass_read(
    globus_xio_operation_t              op,
    globus_xio_iovec_t *                iovec,
    int                                 iovec_count,
    globus_size_t                       wait_for,
    globus_xio_driver_data_callback_t   cb,
    void *                              user_arg);

/**
 *  Finished Read
 *  @ingroup globus_xio_driver
 *
 *  This function is called to signal globus_xio that the requested
 *  read operation has been completed.
 *
 *  @param op
 *         The operation structure representing the requested read
 *         operation.
 *
 *  @param result
 *          Return status of the completed operation
 * 
 *  @param nread
 *          The number of bytes read
 */
void
globus_xio_driver_finished_read(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nread);

/**
 * EOF state manipulation
 * @ingroup globus_xio_driver
 * 
 * This function is used by drivers that allow multiple outstanding reads at
 * a time.  It can only be called on behalf of a read operation (while in the
 * read interface call or the pass_read callback).
 * 
 * Typical use for this would be to hold a driver specific lock and call this
 * when an internal EOF has been received.  The read operation this is called
 * on behalf of must be finished with an EOF error or the results are
 * undefined.
 * 
 * In general, you should not have an EOF flag in your driver.  Use this call
 * and globus_xio_driver_eof_received() instead.  This is necessary to support
 * XIO's automatic EOF resetting.  If your driver absolutely can not be read
 * after an EOF has been set, then you will need your own EOF flag.
 * 
 * This call will typically only be used just before a finished_read() call.
 * 
 * @param op
 *      The operation structure representing the requested read
 *      operation.
 */
void
globus_xio_driver_set_eof_received(
    globus_xio_operation_t              op);

/**
 * EOF state checking
 * @ingroup globus_xio_driver
 * 
 * This function is used by drivers that allow multiple outstanding reads at
 * a time.  It can only be called on behalf of a read operation (while in the
 * read interface call or the pass_read callback).
 * 
 * Typical use for this would be to hold a driver specific lock (the same one
 * used when calling globus_xio_driver_set_eof_received()) and call this to
 * see if an EOF has been received. If so, the operation should immediately be
 * finished with an EOF error (do not _return_ an EOF error).
 * 
 * This call will typically only be used in the read interface call.
 * 
 * @param op
 *      The operation structure representing the requested read
 *      operation.
 * 
 * @return
 *      GLOBUS_TRUE if EOF received, GLOBUS_FALSE otherwise.
 */
globus_bool_t
globus_xio_driver_eof_received(
    globus_xio_operation_t              op);

/**********************************************************************
 *                          Write
 *********************************************************************/
/**
 *  Write data from an open handle.
 *  @ingroup globus_xio_driver
 *
 *  This function is called when the user requests to write data to
 *  a handle.  The driver author shall implement all code needed to for
 *  there driver to complete write operations.
 *
 *  @param driver_specific_handle
 *          The driver handle to which data should be written.
 *
 *  @param iovec
 *         An io vector pointing to the buffers to be written.
 *
 *  @param iovec_count
 *         The number if entries in the io vector.
 *
 *  @param op
 *         The requested operation.  When the driver is finished fulfilling
 *         the requested read operation it must use this structure to 
 *         signal globus_xio that the operation is completed.  This is done
 *         by calling globus_xio_driver_finished_operation().
 */
typedef globus_result_t
(*globus_xio_driver_write_t)(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op);

/**
 *  Driver write
 *  @ingroup globus_xio_driver
 *
 *  This function passes a write operation down the driver stack.  After
 *  this function is called the op structure is no longer valid.  However
 *  when the driver stack finishes servicing the write request it will 
 *  pass a new operation structure in the function pointed to by cb.
 *  Finished write can be called on the new operation received.
 *
 *  @param op
 *         The operation structure representing this requested io
 *         operation.
 *
 *  @param iovec
 *         A pointer to the array of iovecs.
 *
 *  @param iovec_count
 *         The number of iovecs in the array.
 *
 *  @param wait_for
 *         The minimum number of bytes to write before returning. If a driver
 *         has no specific requirement, he should use the user's request
 *         available via GlobusXIOOperationMinimumWrite(op)
 * 
 *  @param cb
 *         The function to be called when the operation request is 
 *         completed.
 *
 *  @param user_arg
 *         A user pointer that will be threaded through to the callback.
 */
globus_result_t
globus_xio_driver_pass_write(
    globus_xio_operation_t              op,
    globus_xio_iovec_t *                iovec,
    int                                 iovec_count,
    globus_size_t                       wait_for,
    globus_xio_driver_data_callback_t   cb,
    void *                              user_arg);

/**
 *  Finished Write
 *  @ingroup globus_xio_driver
 *
 *  This function is called to signal globus_xio that the requested
 *  write operation has been completed.
 *
 *  @param op
 *         The operation structure representing the requested write
 *         operation.
 *
 *  @param result
 *          Return status of the completed operation
 * 
 *  @param nwritten
 *          The number of bytes written
 */
void
globus_xio_driver_finished_write(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nwritten);

/**
 *  Finishes an operation and merge two op structures.
 *  (XXX not implemented yet)
 *  @ingroup globus_xio_driver
 *
 *  This function will join to operations together and signal globus_xio
 *  that it has completed.  This is an advanced function.  Most drivers will
 *  not require its use.  This function takes an operation that was created
 *  by this driver and passed on to drivers lower on the stack and an
 *  operation that came in on the interface function (that has seen the top
 *  half of the stack) and joins them together.  The purpose of this function
 *  is to join data descriptors that were pre-staged and cached with those
 *  that have later come in at the users request.  See the read ahead doc
 *  for more information.
 *
 *  @param top_op
 *         The operation that has seen the top part of the driver stack.
 *
 *  @param bottom_op
 *         The operation that has seen the bottom part of the driver stack.
 *  
 *  (result is always success in this case. if there is an error, use the
 * other finish() call)
 */
globus_result_t
globus_xio_driver_merge_operation(
     globus_xio_operation_t             top_op,
     globus_xio_operation_t             bottom_op);


/**********************************************************************
 *                     data descriptors
 *********************************************************************/
/**
 *  @defgroup globus_xio_data_descriptors Data descriptors
 *  @ingroup globus_xio
 *  @brief Data descriptors
 *
 *  globus_xio uses data descriptors to associate meta data with the 
 *  data being written or the data read.
 *
 *  Data descriptors flow into the drivers read and write interface
 *  functions by way of the operation structure.  If the driver is
 *  interested in viewing the data descriptor it can request it from
 *  the operation structure via a call to 
 *  globus_xio_driver_operation_get_data_descriptor() and it can view
 *  any driver specific data descriptor via a call to
 *  globus_xio_driver_data_descriptor_get_specific().  The driver
 *  can modify values in the data descriptor by setting values before
 *  passing the request down the stack.  Several functions are available
 *  to modify the data descriptors.  There is no need to "set()" the 
 *  data descriptors back into the operation.  The functions for manipulating
 *  the values in a DD affect the values xio has directly.
 *
 *  Data descriptors flow back to the driver in the callbacks for
 *  the data operations.  When calling finished operation on a data 
 *  operation the driver must pass in a data descriptor.  It should get 
 *  this data descriptor from the io operation callback.
 *
 *  Life Cycle:
 *
 *  Passing in a data descriptor:
 *    A data descriptor is first created by the globus_xio user.  The user 
 *    can add driver specific data descriptors to it.  Once the user has
 *    created and set the attributes on its data descriptor to their liking
 *    they pass it into a globus_xio data operation (either read or write).
 *    When the data descriptor is passed on globus_xio will make an internal
 *    copy of it.  It does this by first coping the user the level
 *    data descriptor and then walking through the list of driver specific
 *    data descriptor contained in to and requesting the driver make 
 *    a copy of the driver specific data descriptor.  If ever a driver
 *    specific data descriptor is NULL globus_xio need not call into its
 *    drivers dd_copy function.  If ever the user level data descriptor is
 *    NULL globus_xio need not deal with the data descriptor functionality at 
 *    all.  
 *
 *  A data descriptor coming back up the stack
 *    Once an io operation reaches the transport driver (the bottom of the
 *    stack) it takes on a slightly different role.  On the way in it
 *    is describing what is requested to be done with the data, on the way
 *    out it is describing what has actually been done.  Once the transport
 *    driver performs the operation it should adjust the data descriptor
 *    to reflect what has actually happened (few drivers will need to worry
 *    about this).  Each driver on the way up can adjust the data 
 *    descriptor and its driver specific data descriptor.  When XIO reaches
 *    the top of the stack it calls a user callback.  When that callback 
 *    returns all memory associated with the data descriptor is cleaned up.
 *    The interface function globus_xio_driver_data_descriptor_free() is
 *    used for this.
 */

globus_result_t
globus_xio_driver_init(
    globus_xio_driver_t *               driver,
    const char *                        driver_name,
    void *                              user_data);

/**
 *  @ingroup globus_xio_driver
 */
globus_result_t
globus_xio_driver_get_user_data(
    globus_xio_driver_t                 in_driver,
    void **                             out_user_data);

globus_result_t
globus_xio_operation_attr_cntl(
    globus_xio_operation_t              op,
    globus_xio_attr_cmd_t               cmd,
    ...);

/**
 *  @ingroup globus_xio_driver
 */
globus_result_t
globus_xio_driver_destroy(
    globus_xio_driver_t                 driver);

/**
 *  @ingroup globus_xio_driver
 */
globus_result_t
globus_xio_driver_set_transport(
    globus_xio_driver_t                 driver,
    globus_xio_driver_transport_open_t  transport_open_func,
    globus_xio_driver_close_t           close_func,
    globus_xio_driver_read_t            read_func,
    globus_xio_driver_write_t           write_func,
    globus_xio_driver_handle_cntl_t     handle_cntl_func);

/**
 *  @ingroup globus_xio_driver
 */
globus_result_t
globus_xio_driver_set_transform(
    globus_xio_driver_t                 driver,
    globus_xio_driver_transform_open_t  transform_open_func,
    globus_xio_driver_close_t           close_func,
    globus_xio_driver_read_t            read_func,
    globus_xio_driver_write_t           write_func,
    globus_xio_driver_handle_cntl_t     handle_cntl_func,
    globus_xio_driver_push_driver_t     push_driver_func);

/**
 *  @ingroup globus_xio_driver
 */
globus_result_t
globus_xio_driver_set_server(
    globus_xio_driver_t                 driver,
    globus_xio_driver_server_init_t     server_init_func,
    globus_xio_driver_server_accept_t   server_accept_func,
    globus_xio_driver_server_destroy_t  server_destroy_func,
    globus_xio_driver_server_cntl_t     server_cntl_func,
    globus_xio_driver_link_cntl_t       link_cntl_func,
    globus_xio_driver_link_destroy_t    link_destroy_func);

globus_result_t
globus_xio_driver_set_server_pre_init(
    globus_xio_driver_t                 driver,
    globus_xio_driver_server_init_t     server_pre_init_func);
/**
 *  @ingroup globus_xio_driver
 */
globus_result_t
globus_xio_driver_set_attr(
    globus_xio_driver_t                 driver,
    globus_xio_driver_attr_init_t       attr_init_func,
    globus_xio_driver_attr_copy_t       attr_copy_func,
    globus_xio_driver_attr_cntl_t       attr_cntl_func,
    globus_xio_driver_attr_destroy_t    attr_destroy_func);

/*
 *  operation accessors
 */
void
globus_xio_operation_block_timeout(
    globus_xio_operation_t              op);
    
void
globus_xio_operation_unblock_timeout(
    globus_xio_operation_t              op);

void
globus_xio_operation_refresh_timeout(
    globus_xio_operation_t              op);

/** returns true if operation already canceled */
globus_bool_t
globus_xio_operation_enable_cancel(
    globus_xio_operation_t              op,
    globus_xio_driver_cancel_callback_t cb,
    void *                              user_arg);

void
globus_xio_operation_disable_cancel(
    globus_xio_operation_t              op);

globus_bool_t
globus_xio_operation_is_canceled(
    globus_xio_operation_t              op);

globus_size_t
globus_xio_operation_get_wait_for(
    globus_xio_operation_t              op);

void *
globus_xio_operation_get_driver_specific(
    globus_xio_operation_t              op);

globus_xio_driver_t
globus_xio_operation_get_user_driver(
    globus_xio_operation_t              op);

globus_xio_driver_t
globus_xio_operation_get_transport_user_driver(
    globus_xio_operation_t              op);

/* this returns the handle to the drivers below you */
globus_xio_driver_handle_t
globus_xio_operation_get_driver_handle(
    globus_xio_operation_t              op);

/* this returns the handle to your driver.
 * (only useful for canceling operations in your possession)
 * or getting user handle associated with the driver
 */
globus_xio_driver_handle_t
globus_xio_operation_get_driver_self_handle(
    globus_xio_operation_t              op);

void *
globus_xio_operation_get_data_descriptor(
    globus_xio_operation_t              op,
    globus_bool_t                       force_create);

globus_result_t
globus_xio_operation_copy_stack(
    globus_xio_operation_t              op,
    globus_xio_stack_t *                stack);

/* STRING PARSING STUFF */
/**
 *  @ingroup globus_xio_driver
 */
typedef globus_result_t
(*globus_xio_string_cntl_parse_func_t)(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func);

/**
 *  @ingroup globus_xio_driver
 */
typedef struct globus_xio_string_cntl_table_s
{
    const char *                        key;
    int                                 cmd;
    globus_xio_string_cntl_parse_func_t parse_func;
} globus_xio_string_cntl_table_t;

/**
 *
 *  The set of interface functions that the driver author must implement
 *  to create a driver and the functions to assist in the creation.
 *
 *  Driver attribute functions
 *
 *  If the driver wishes to provide driver specific attributes to the
 *  user it must implement the following functions:
 *
 *  globus_xio_driver_attr_init_t
 *  globus_xio_driver_attr_copy_t
 *  globus_xio_driver_attr_cntl_t
 *  globus_xio_driver_attr_destroy_t
 */

/**
 *  @defgroup string_globus_xio_driver_programming Driver Programming: String options
 *  @ingroup globus_xio_driver
 *  @brief Driver Programming: String options
 *
 *  A driver can choose to expose parameters as in a string form.  Providing
 *  this feature makes dynamically setting driver specific options much easier.
 *  a user can then load the driver by name and set specific options by name
 *  all at runtime with no object module references.  For example, a TCP driver
 *  can be loaded with the string: tcp, and the options can be set with:
 *
 *  port=50668;keepalive=yes;nodelay=N
 *
 *  this would set the port to 50668, keepalive to true and nodelay to false.
 *  The particular string definition is defined by the tcp driver by properly 
 *  creating a globus_i_xio_attr_parse_table_t array.  Each element of the
 *  array is 1 options.  There are 3 members of each array entry: key, cmd, and
 *  parse function.  The key is a string that defines what option is to be set.
 *  In the above example string "port" would be 1 key.  cmd tells the driver what
 *  cntl is associated with the key.  In other words, once the string is parsed out
 *  what driver specific control must be called to set the requested option.  For
 *  more information on controls see @ref globus_xio_attr_cntl.  The final value
 *  in the array entry is the parsing function.  The parsing function takes the 
 *  value of the key=value portion of the string and parses it into data types.
 *  once parsed globus_xio_attr_cntl is called and thus the option is set.  There are 
 *  many available parsing functions but the developer is free to right their own
 *  if the provided ones are not sufficient.  Sample parsing functions follow:
 *
 *  - @ref globus_xio_string_cntl_bool
 *  - @ref globus_xio_string_cntl_float
 *  - @ref globus_xio_string_cntl_int
 *  - @ref globus_xio_string_cntl_string
 *  - @ref globus_xio_string_cntl_int_int
 */
/**
 * Set the string table for attr cntl
 * @ingroup string_globus_xio_driver_programming
 */
globus_result_t
globus_xio_driver_string_cntl_set_table(
    globus_xio_driver_t                 driver,
    globus_xio_string_cntl_table_t *   table);

/**
 * Set the string table for handle cntl
 * @ingroup string_globus_xio_driver_programming
 */
globus_result_t
globus_xio_driver_handle_string_cntl_set_table(
    globus_xio_driver_t                driver,
    globus_xio_string_cntl_table_t *   table);

/**
 *  @ingroup string_globus_xio_driver_programming
 *
 *  New type functions call this one
 */
globus_result_t
globus_xio_string_cntl_bouncer(
    globus_xio_driver_attr_cntl_t       cntl_func,
    void *                              attr,
    int                                 cmd,
    ...);

/* list all of the built in parsing functions */
/**
 *  @ingroup string_globus_xio_driver_programming
 *
 *  String option parsing function.
 */
globus_result_t
globus_xio_string_cntl_bool(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func);

/**
 *  @ingroup string_globus_xio_driver_programming
 *
 *  String option parsing function.
 */
globus_result_t
globus_xio_string_cntl_float(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func);

/**
 *  @ingroup string_globus_xio_driver_programming
 *
 *  String option parsing function.
 */
globus_result_t
globus_xio_string_cntl_int(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func);

/**
 *  @ingroup string_globus_xio_driver_programming
 *
 *  String option parsing function.
 */
globus_result_t
globus_xio_string_cntl_string(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func);

/**
 *  @ingroup string_globus_xio_driver_programming
 *
 *  String option parsing function.
 */
globus_result_t
globus_xio_string_cntl_int_int(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func);

globus_result_t
globus_xio_string_cntl_formated_off(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func);

globus_result_t
globus_xio_string_cntl_formated_int(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func);

globus_result_t
globus_xio_string_cntl_string_list(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func);


#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_XIO_DRIVER_H */
