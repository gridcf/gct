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

#ifndef GLOBUS_XIO_UDP_DRIVER_H
#define GLOBUS_XIO_UDP_DRIVER_H
/**
 * @file globus_xio_udp_driver.h
 * @brief XIO UDP Driver
 */

#include "globus_xio_system.h"

/**
 * @defgroup globus_xio_udp_driver Globus XIO UDP Driver
 * @ingroup globus_xio
 * @brief Globus XIO UDP Driver
 *
 * The IPV4/6 UDP socket driver.
 */
 
/**
 * @defgroup globus_xio_udp_driver_instance Opening/Closing
 * @ingroup globus_xio_udp_driver
 * @brief Opening/Closing
 * 
 * An XIO handle with the udp driver can be created with
 * @ref globus_xio_handle_create().
 * 
 * The handle can be created in two modes: open server or connected client.
 * If the contact string does not have a host and port, the udp socket will
 * accept messages from any sender.  If a host and port is specified, the udp
 * socket will be 'connected' immediately to that host:port.  This blocks
 * packets from any sender other than the contact string.  A handle that starts
 * out as an open server can later be 'connected' with
 * @ref GLOBUS_XIO_UDP_CONNECT (presumably after the first message is received
 * from a sender and his contact info is available).
 * 
 * When the XIO handle is closed, the udp driver will destroy its internal
 * resources and close the socket (unless this socket was set on the attr to
 * @ref globus_xio_register_open()).
 */
 
/**
 * @defgroup globus_xio_udp_driver_io Reading/Writing
 * @ingroup globus_xio_udp_driver
 * @brief Reading/Writing
 * 
 * @ref globus_xio_register_read() semantics:
 * 
 * If the waitforbytes parameter is greater than zero, the read will happen
 * asynchronously and be completed when at least waitforbytes has been
 * read/written.
 * 
 * If the waitforbytes parameter is equal to zero, one of the following
 * alternative behaviors occur:
 * 
 * If the length of the buffer is > 0 the read happens synchronously.  If
 * the user is using one of the blocking xio calls, no internal callback will
 * occur.
 * 
 * If the length of the buffer is also 0, the call behaves like an asynchronous
 * notification of data ready to be read. ie, an asynchronous select().
 * 
 * In any case, when an error occurs before the waitforbytes request
 * has been met, the outgoing nbytes is set to the amount of data actually
 * read before the error occurred.
 * 
 * If the handle is not connected, the user should pass in a data descriptor.
 * After the read, this data_descriptor will contain the contact string of the
 * sender.  The user can either get this contact string with
 * @ref GLOBUS_XIO_UDP_GET_CONTACT or pass the data descriptor directly to
 * @ref globus_xio_register_write() to send a message back to the sender.
 * 
 * Also, if the handle is not connected, the waitforbytes should probably be
 * 1 to guarantee that only one packet is received and the sender contact isn't
 * overwritten by multiple packets from different senders.
 * 
 * @ref globus_xio_register_write() semantics:
 * 
 * When performing a write, exactly one UDP packet is sent of the entire
 * buffer length.  The waitforbytes parameter is ignored.  If the entire buffer
 * can not be written, a @ref GLOBUS_XIO_UDP_ERROR_SHORT_WRITE error will be
 * returned with nbytes set to the number of bytes actually sent.
 * 
 * If the handle is not 'connected', a contact string must be set in the
 * data descriptor to  @ref globus_xio_register_write().  This can either be
 * done explicitly with @ref GLOBUS_XIO_UDP_SET_CONTACT or implicitly by
 * passing in a data descriptor received from @ref globus_xio_register_read().
 * 
 * The udp write semantics are always synchronous.  No blocking or internal
 * callback will occur when using @ref globus_xio_write().
 */
 
/**
 * @defgroup globus_xio_udp_driver_envs Env Variables
 * @ingroup globus_xio_udp_driver
 * @brief Env Variables
 * 
 * The udp driver uses the following environment variables
 * - GLOBUS_HOSTNAME Used when setting the hostname in the contact string
 * - GLOBUS_UDP_PORT_RANGE Used to restrict the port the udp socket binds to
 * - GLOBUS_XIO_SYSTEM_DEBUG Available if using a debug build.  See 
 *      globus_debug.h for format. The UDP driver uses globus_xio_system
 *      (along with the File and TCP drivers) which defines the following
 *      levels: TRACE for all function call tracing, DATA for data read and
 *      written counts, INFO for some special events, and RAW which dumps the
 *      raw buffers actually read or written.  This can contain binary data,
 *      so be careful when you enable it.
 */

/**
 * @defgroup globus_xio_udp_driver_cntls Attributes and Cntls
 * @ingroup globus_xio_udp_driver
 * @brief Attributes and Cntls
 * 
 * UDP driver specific attrs and cntls.
 * 
 * @see globus_xio_attr_cntl()
 * @see globus_xio_handle_cntl()
 * @see globus_xio_data_descriptor_cntl()
 */
/**
 * @defgroup globus_xio_udp_driver_types Types
 * @ingroup globus_xio_udp_driver
 * @brief Types
 */
/**
 * @defgroup globus_xio_udp_driver_errors Error Types
 * @ingroup globus_xio_udp_driver
 * @brief Error Types
 * 
 * The UDP driver is very close to the system code, so most errors
 * reported by it are converted from the system errno. A few of the exceptions
 * are GLOBUS_XIO_ERROR_COMMAND, GLOBUS_XIO_ERROR_CONTACT_STRING,
 * GLOBUS_XIO_ERROR_CANCELED, @ref GLOBUS_XIO_UDP_ERROR_NO_ADDRS,
 * and @ref GLOBUS_XIO_UDP_ERROR_SHORT_WRITE
 * 
 * @see globus_xio_driver_error_match()
 * @see globus_error_errno_match()
 */

/**
 * Invalid handle type
 * @ingroup globus_xio_udp_driver_types
 * @hideinitializer
 * @see GLOBUS_XIO_UDP_SET_HANDLE
 */
#define GLOBUS_XIO_UDP_INVALID_HANDLE GLOBUS_XIO_SYSTEM_INVALID_SOCKET

/**
 * UDP driver specific error types
 * @ingroup globus_xio_udp_driver_errors
 */
typedef enum
{
    /** 
     * Indicates that no IPv4/6 compatible sockets could be resolved 
     * for the specified hostname
     */
    GLOBUS_XIO_UDP_ERROR_NO_ADDRS,
    /**
     * Indicates that a write of the full buffer failed.  Possibly need to 
     * increase the send buffer size.
     */
    GLOBUS_XIO_UDP_ERROR_SHORT_WRITE
} globus_xio_udp_error_type_t;

/** doxygen varargs filter stuff
 * GlobusVarArgDefine(
 *      attr, globus_result_t, globus_xio_attr_cntl, attr, driver)
 * GlobusVarArgDefine(
 *      handle, globus_result_t, globus_xio_handle_cntl, handle, driver)
 * GlobusVarArgDefine(
 *      dd, globus_result_t, globus_xio_data_descriptor_cntl, dd, driver)
 */

/**
 * UDP driver specific cntls
 * @ingroup globus_xio_udp_driver_cntls
 */
typedef enum
{
    /** GlobusVarArgEnum(attr)
     * Set the udp socket to use.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param handle
     *      Use this handle (fd or SOCKET).
     *      Note:  close() will not be called on this handle.
     */
    /* globus_xio_system_socket_t       handle */
    GLOBUS_XIO_UDP_SET_HANDLE,
    
    /** GlobusVarArgEnum(attr)
     * Set the udp service name to listen on.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param service_name
     *      The service name to use when setting up the listener.  If the 
     *      service name cannot be resolved, the port (if one is set) will
     *      be used instead.
     */
    /* const char *                     service_name */
    GLOBUS_XIO_UDP_SET_SERVICE,
    
    /** GlobusVarArgEnum(attr)
     * Get the service name to listen on.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param service_name_out
     *      A pointer to the service name will be stored here  If none is set,
     *      NULL will be passed back.  Otherwise, the name will be
     *      duplicated with strdup() and the user should call free() on it.
     */
    /* char **                          service_name_out */
    GLOBUS_XIO_UDP_GET_SERVICE,
    
    /** GlobusVarArgEnum(attr)
     * Set the port number to listen on.
     * @ingroup globus_xio_udp_driver_cntls
     * The default is 0 (system assigned)
     * 
     * @param listener_port
     *      The port number to use when setting up the listener.  If the 
     *      service name is also set, this will only be used if that can't be
     *      resolved.
     */
    /* int                              listener_port */
    GLOBUS_XIO_UDP_SET_PORT,
    
    /** GlobusVarArgEnum(attr)
     *  the port number to listen on.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param listener_port_out
     *      The port will be stored here.
     */
    /* int *                            listener_port_out */
    GLOBUS_XIO_UDP_GET_PORT,
    
    /** GlobusVarArgEnum(attr)
     * Set the port range to confine the listener to.
     * @ingroup globus_xio_udp_driver_cntls
     * Used only where no specific service or port has been set.  It overrides
     * the range set in the GLOBUS_UDP_PORT_RANGE env variable.  If
     * 'restrict port' is true, the listening port will be constrained to the
     * range specified.
     * 
     * @param listener_min_port
     *      The lower bound on the listener port. (default 0 -- no bound)
     * 
     * @param listener_max_port
     *      The upper bound on the listener port. (default 0 -- no bound)
     * 
     * @see GLOBUS_XIO_UDP_SET_RESTRICT_PORT
     */
    /* int                              listener_min_port,
     * int                              listener_max_port */
    GLOBUS_XIO_UDP_SET_LISTEN_RANGE,
    
    /** GlobusVarArgEnum(attr)
     * Get the udp port range on an attr.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param listener_min_port_out
     *      The lower bound will be stored here.
     * 
     * @param listener_max_port_out
     *      The upper bound will be stored here.
     */
    /* int *                            listener_min_port_out,
     * int *                            listener_max_port_out */
    GLOBUS_XIO_UDP_GET_LISTEN_RANGE,
    
    /** GlobusVarArgEnum(attr)
     * Set the interface to bind the socket to.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param interface
     *      The interface to use.  Can be a hostname or numeric IP
     */
    /* const char *                     interface */
    GLOBUS_XIO_UDP_SET_INTERFACE,
    
    /** GlobusVarArgEnum(attr)
     * Get the interface on the attr.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param interface_out
     *      A pointer to the interface will be stored here  If one is set,
     *      NULL will be passed back.  Otherwise, the interface will be
     *      duplicated with strdup() and the user should call free() on it.
     */
    /* char **                          interface_out */
    GLOBUS_XIO_UDP_GET_INTERFACE,
    
    /** GlobusVarArgEnum(attr)
     * Enable or disable the listener range constraints.
     * @ingroup globus_xio_udp_driver_cntls
     * This enables or ignores the port range found in the attr or in then env.
     * By default, those ranges are enabled.
     * 
     * @param restrict_port
     *      GLOBUS_TRUE to enable (default), GLOBUS_FALSE to disable.
     * 
     * @see GLOBUS_XIO_UDP_SET_LISTEN_RANGE
     */
    /* globus_bool_t                    restrict_port */
    GLOBUS_XIO_UDP_SET_RESTRICT_PORT,
    
    /** GlobusVarArgEnum(attr)
     * Get the restrict port flag.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param restrict_port_out
     *      The restrict port flag will be stored here.
     */
    /* globus_bool_t *                  restrict_port_out */
    GLOBUS_XIO_UDP_GET_RESTRICT_PORT,
    
    /** GlobusVarArgEnum(attr)
     * Reuse addr when binding.
     * @ingroup globus_xio_udp_driver_cntls
     * Used to determine whether or not to allow reuse of addresses when
     * binding a socket to a port number.
     * 
     * @param resuseaddr
     *      GLOBUS_TRUE to allow, GLOBUS_FALSE to disallow (default)
     */
    /* globus_bool_t                    resuseaddr */
    GLOBUS_XIO_UDP_SET_REUSEADDR,
    
    /** GlobusVarArgEnum(attr)
     * Get the reuseaddr flag on an attr.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param resuseaddr_out
     *      The reuseaddr flag will be stored here.
     */
    /* globus_bool_t *                  resuseaddr_out */
    GLOBUS_XIO_UDP_GET_REUSEADDR,
    
    /** GlobusVarArgEnum(attr)
     * Restrict to IPV4 only.
     * @ingroup globus_xio_udp_driver_cntls
     * Disallow IPV6 sockets from being used
     * (default is to use either ipv4 or ipv6)
     * 
     * @param no_ipv6
     *      GLOBUS_TRUE to disallow ipv6, GLOBUS_FALSE to allow (default)
     */
    /* globus_bool_t                    no_ipv6 */
    GLOBUS_XIO_UDP_SET_NO_IPV6,
    
    /** GlobusVarArgEnum(attr)
     * Get the no ipv6 flag on an attr.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param no_ipv6_out
     *      The no ipv6 flag will be stored here.
     */
    /* globus_bool_t *                  no_ipv6_out */
    GLOBUS_XIO_UDP_GET_NO_IPV6,
    
    /** GlobusVarArgEnum(attr, handle)
     * Get the socket handle on an attr or handle.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param handle_out
     *      The udp socket will be stored here. If none is set,
     *      GLOBUS_XIO_UDP_INVALID_HANDLE will be set.
     */
    /* globus_xio_system_socket_t *     handle_out */
    GLOBUS_XIO_UDP_GET_HANDLE,
    
    /** GlobusVarArgEnum(attr, handle)
     * Set the socket send buffer size.
     * @ingroup globus_xio_udp_driver_cntls
     * Used to set the size of the send buffer used on the socket.
     * 
     * @param sndbuf
     *      The send buffer size in bytes to use. (default is system specific)
     */
    /* int                              sndbuf */
    GLOBUS_XIO_UDP_SET_SNDBUF,
    
    /** GlobusVarArgEnum(attr, handle)
     * Get the send buffer size on the attr or handle.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param sndbuf_out
     *      The send buffer size will be stored here.
     */
    /* int *                            sndbuf_out */
    GLOBUS_XIO_UDP_GET_SNDBUF,
    
    /** GlobusVarArgEnum(attr, handle)
     * Set the socket receive buffer size.
     * @ingroup globus_xio_udp_driver_cntls
     * Used to set the size of the receive buffer used on the socket.
     * 
     * @param rcvbuf
     *      The receive buffer size in bytes. (default is system specific)
     */
    /* int                              rcvbuf */
    GLOBUS_XIO_UDP_SET_RCVBUF,
    
    /** GlobusVarArgEnum(attr, handle)
     * Get the receive buffer size on the attr or handle.
     * @ingroup globus_xio_udp_driver_cntls
     * 
     * @param rcvbuf_out
     *      The receive buffer size will be stored here.
     */
    /* int *                            rcvbuf_out */
    GLOBUS_XIO_UDP_GET_RCVBUF,
    
    /** GlobusVarArgEnum(handle, dd)
     * Get the contact string associated with a handle or data descriptor.
     * @ingroup globus_xio_udp_driver_cntls
     * Use with globus_xio_handle_cntl() to get a contact string for the udp
     * listener.  Use with globus_xio_data_descriptor_cntl() to get the
     * sender's contact string from a data descriptor passed to
     * @ref globus_xio_register_read().
     * 
     * @param contact_string_out
     *      A pointer to a contact string will be stored here.  The user 
     *      should free() it when done with it.  It will be in the 
     *      format: \<hostname\>:\<port\>
     * 
     * @see GLOBUS_XIO_GET_LOCAL_CONTACT
     */
    /* char **                          contact_string_out */
    GLOBUS_XIO_UDP_GET_CONTACT,
    
    /** GlobusVarArgEnum(handle, dd)
     * Get the contact string associated with a handle or data descriptor.
     * @ingroup globus_xio_udp_driver_cntls
     * Use with globus_xio_handle_cntl() to get a contact string for the udp
     * listener.  Use with globus_xio_data_descriptor_cntl() to get the
     * sender's contact string from a data descriptor passed to
     * @ref globus_xio_register_read().
     * 
     * @param contact_string_out
     *      A pointer to a contact string will be stored here.  The user 
     *      should free() it when done with it.  It will be in the 
     *      format: \<ip\>:\<port\>
     * 
     * @see GLOBUS_XIO_GET_LOCAL_NUMERIC_CONTACT
     */
    /* char **                          contact_string_out */
    GLOBUS_XIO_UDP_GET_NUMERIC_CONTACT,
    
    /** GlobusVarArgEnum(dd)
     * Set the destination contact.
     * @ingroup globus_xio_udp_driver_cntls
     * Use on a data descriptor passed to @ref globus_xio_register_write() to
     * specify the recipient of the data.  This is necessary with unconnected
     * handles or to send to recipients other than the connected one.
     * 
     * @param contact_string
     *      A pointer to a contact string of the format
     *      \<hostname/ip\>:\<port/service\>
     * 
     * @see GLOBUS_XIO_UDP_CONNECT
     */
    /* char *                           contact_string */
    GLOBUS_XIO_UDP_SET_CONTACT,
    
    /** GlobusVarArgEnum(handle)
     * Set the default destination contact.
     * @ingroup globus_xio_udp_driver_cntls
     * Connecting a handle to a specific contact blocks packets from any other
     * contact.  It also sets the default destination of all outgoing packets
     * so, using @ref GLOBUS_XIO_UDP_SET_CONTACT is unnecessary.
     * 
     * @param contact_string
     *      A pointer to a contact string of the format
     *      \<hostname/ip\>:\<port/service\>
     */
    /* char *                           contact_string */
    GLOBUS_XIO_UDP_CONNECT,
    
   /** GlobusVarArgEnum(attr)
     * Join a multicast group.
     * @ingroup globus_xio_udp_driver_cntls
     * Specify a multicast group to join.  All packets received will be
     * to the specified multicast address.  Do not use
     * @ref GLOBUS_XIO_UDP_CONNECT, @ref GLOBUS_XIO_UDP_SET_PORT, or
     * pass a contact string on the open. Consider using 
     * @ref GLOBUS_XIO_UDP_SET_REUSEADDR to allow other apps to join this
     * group.  Use @ref GLOBUS_XIO_UDP_SET_INTERFACE to specify the
     * interface to use.  Will not affect handles set with
     * @ref GLOBUS_XIO_UDP_SET_HANDLE.  @ref GLOBUS_XIO_UDP_SET_RESTRICT_PORT
     * is ignored.
     * 
     * @param contact_string
     *      A pointer to a contact string of the multicast group to join with
     *      the format: \<hostname/ip\>:\<port/service\> 
     */
    /* char *                           contact_string */
    GLOBUS_XIO_UDP_SET_MULTICAST

} globus_xio_udp_cmd_t;

#endif
