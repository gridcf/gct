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

#ifndef GLOBUS_XIO_TCP_DRIVER_INCLUDE
#define GLOBUS_XIO_TCP_DRIVER_INCLUDE
/**
 * @file globus_xio_tcp_driver.h
 * @brief Header file for XIO TCP Driver
 */
#include "globus_xio_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_xio_tcp_driver Globus XIO TCP Driver
 * @ingroup globus_xio
 * @brief Globus XIO TCP Driver
 *
 * The IPV4/6 TCP socket driver.
 */
 
/**
 * @defgroup globus_xio_tcp_driver_instance Opening/Closing
 * @ingroup globus_xio_tcp_driver
 * @brief Opening/Closing
 * 
 * An XIO handle with the tcp driver can be created with either
 * @ref globus_xio_handle_create() or @ref globus_xio_server_register_accept().
 * 
 * If the handle is created with @ref globus_xio_server_register_accept(),
 * the @ref globus_xio_register_open() call does nothing more than initialize
 * the internal handle with the accepted socket.
 * 
 * If the handle is created with @ref globus_xio_handle_create(), and
 * there is no handle set on the attr passed to the 
 * @ref globus_xio_register_open() call, it performs the equivalent of an
 * asynchronous connect() call.  In this case, the contact string must contain
 * a host name and service/port.  Both the hostname and port number can be
 * numeric or symbolic (eg: some.webserver.com:80 or 214.123.12.1:http).
 * If the hostname is symbolic and it resolves to multiple ip addresses, each
 * one will be attempted in succession, until the connect is successful or
 * there are no more addresses.
 * 
 * When the XIO handle is closed, the tcp driver will destroy its internal
 * resources and close the socket (unless this socket was set on an attr).
 * Any write data pending in system buffers will be sent unless the linger
 * option has been set.  Any remaining data in recv buffers will be discarded
 * and (on some systems) a connection reset sent to the peer.
 */
 
/**
 * @defgroup globus_xio_tcp_driver_io Reading/Writing
 * @ingroup globus_xio_tcp_driver
 * @brief Reading/Writing
 * 
 * Both the @ref globus_xio_register_read() and
 * @ref globus_xio_register_write() calls follow similar semantics as described
 * below.
 * 
 * If the waitforbytes parameter is greater than zero, the io will happen
 * asynchronously and be completed when at least waitforbytes has been
 * read/written.
 * 
 * If the waitforbytes parameter is equal to zero, one of the following
 * alternative behaviors occur:
 * 
 * If the length of the buffer is > 0 the read or write happens synchronously.
 * If the user is using one of the blocking xio calls, no internal callback
 * will occur.
 * 
 * If the length of the buffer is also 0, the call behaves like an asynchronous
 * notification of data ready to be either read or written. ie, an
 * asynchronous select().
 * 
 * In any case, when an error or EOF occurs before the waitforbytes request
 * has been met, the outgoing nbytes is set to the amount of data actually
 * read/written before the error or EOF occurred.
 */
 
/**
 * @defgroup globus_xio_tcp_driver_server Server
 * @ingroup globus_xio_tcp_driver
 * @brief Server
 * 
 * @ref globus_xio_server_create() causes a tcp listener socket to be created
 * and listened upon.  @ref globus_xio_server_register_accept() performs an
 * asynchronous accept(). @ref globus_xio_server_register_close() cleans up
 * the internal resources associated with the tcp server and calls close() on
 * the listener socket (unless the socket was set on the server via the attr)
 * 
 * All accepted handles inherit all tcp specific attributes set in the attr to
 * @ref globus_xio_server_create(), but can be overridden with the attr to 
 * @ref globus_xio_register_open().
 */
 
/**
 * @defgroup globus_xio_tcp_driver_envs Env Variables
 * @ingroup globus_xio_tcp_driver
 * @brief Env Variables
 * 
 * The tcp driver uses the following environment variables
 * - GLOBUS_HOSTNAME Used when setting the hostname in the contact string
 * - GLOBUS_TCP_PORT_RANGE Used to restrict anonymous listener ports
 *      ex: GLOBUS_TCP_PORT_RANGE=4000,4100
 * - GLOBUS_TCP_PORT_RANGE_STATE_FILE Used in conjunction with
 *      GLOBUS_TCP_PORT_RANGE to maintain last used port among many
 *      applications making use of the same port range.  That last port + 1
 *      will be used as a starting point within the specified tcp port range
 *      instead of always starting at the beginning.  This is really only
 *      necessary when a machine is behind a stateful firewall which is holding
 *      a port in a different state than the application's machine.
 *      See bugzilla.globus.org, bug 1851 for more info.
 *      ex: GLOBUS_TCP_PORT_RANGE_STATE_FILE=/tmp/port_state
 *      (file will be created if it does not exist)
 * - GLOBUS_TCP_SOURCE_RANGE Used to restrict local ports used in a connection
 * - GLOBUS_XIO_TCP_DEBUG Available if using a debug build.  See globus_debug.h
 *      for format.  The TCP driver defines the levels TRACE for all function
 *      call tracing and INFO for write buffer sizes
 * - GLOBUS_XIO_SYSTEM_DEBUG Available if using a debug build.  See 
 *      globus_debug.h for format. The TCP driver uses globus_xio_system
 *      (along with the File and UDP drivers) which defines the following
 *      levels: TRACE for all function call tracing, DATA for data read and
 *      written counts, INFO for some special events, and RAW which dumps the
 *      raw buffers actually read or written.  This can contain binary data,
 *      so be careful when you enable it.
 */

/**
 * @defgroup globus_xio_tcp_driver_cntls Attributes and Cntls
 * @ingroup globus_xio_tcp_driver
 * @brief Attributes and Cntls
 * 
 * Tcp driver specific attrs and cntls.
 * 
 * @see globus_xio_attr_cntl()
 * @see globus_xio_handle_cntl()
 * @see globus_xio_server_cntl()
 * @see globus_xio_data_descriptor_cntl()
 */
/**
 * @defgroup globus_xio_tcp_driver_types Types
 * @ingroup globus_xio_tcp_driver
 * @brief Types
 */
/**
 * @defgroup globus_xio_tcp_driver_errors Error Types
 * @ingroup globus_xio_tcp_driver
 * @brief Error Types
 * 
 * The TCP driver is very close to the system code, so most errors
 * reported by it are converted from the system errno. A few of the exceptions
 * are GLOBUS_XIO_ERROR_EOF, GLOBUS_XIO_ERROR_COMMAND,
 * GLOBUS_XIO_ERROR_CONTACT_STRING, GLOBUS_XIO_ERROR_CANCELED, and
 * @ref GLOBUS_XIO_TCP_ERROR_NO_ADDRS
 * 
 * @see globus_xio_driver_error_match()
 * @see globus_error_errno_match()
 */

/**
 * Invalid handle type
 * @ingroup globus_xio_tcp_driver_types
 * @hideinitializer
 * @see GLOBUS_XIO_TCP_SET_HANDLE
 */
#define GLOBUS_XIO_TCP_INVALID_HANDLE GLOBUS_XIO_SYSTEM_INVALID_SOCKET

/**
 * TCP driver specific error types
 * @ingroup globus_xio_tcp_driver_errors
 */
typedef enum
{
    /** 
     * Indicates that no IPv4/6 compatible sockets could be resolved 
     * for the specified hostname
     */
    GLOBUS_XIO_TCP_ERROR_NO_ADDRS
} globus_xio_tcp_error_type_t;

/** doxygen varargs filter stuff
 * GlobusVarArgDefine(
 *      attr, globus_result_t, globus_xio_attr_cntl, attr, driver)
 * GlobusVarArgDefine(
 *      handle, globus_result_t, globus_xio_handle_cntl, handle, driver)
 * GlobusVarArgDefine(
 *      server, globus_result_t, globus_xio_server_cntl, server, driver)
 * GlobusVarArgDefine(
 *      dd, globus_result_t, globus_xio_data_descriptor_cntl, dd, driver)
 */

/**
 * TCP driver specific cntls
 * @ingroup globus_xio_tcp_driver_cntls
 */
typedef enum
{
    /** GlobusVarArgEnum(attr)
     * Set the tcp service name to bind to.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used only on attrs for @ref globus_xio_server_create().
     * 
     * @param service_name
     *      The service name to use when setting up the listener.  If the 
     *      service name cannot be resolved, the port (if one is set) will
     *      be used instead.
     *
     * string opt: port=<em>string</em>
     */
    /* const char *                     service_name */
    GLOBUS_XIO_TCP_SET_SERVICE,
    
    /** GlobusVarArgEnum(attr)
     * Get the tcp service name to bind to.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param service_name_out
     *      A pointer to the service name will be stored here  If none is set,
     *      NULL will be passed back.  Otherwise, the name will be
     *      duplicated with strdup() and the user should call free() on it.
     */
    /* char **                          service_name_out */
    GLOBUS_XIO_TCP_GET_SERVICE,
    
    /** GlobusVarArgEnum(attr)
     * Set the tcp port number to bind to.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used only on attrs for @ref globus_xio_server_create(). The default
     * port number is 0 (system assigned)
     * 
     * @param listener_port
     *      The port number to use when setting up the listener.  If the 
     *      service name is also set, this will only be used if that can't be
     *      resolved.
     *
     * string opt: port=<em>int</em>
     */
    /* int                              listener_port */
    GLOBUS_XIO_TCP_SET_PORT,
    
    /** GlobusVarArgEnum(attr)
     * Get the tcp port number to bind to.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param listener_port_out
     *      The port will be stored here.
     */
    /* int *                            listener_port_out */
    GLOBUS_XIO_TCP_GET_PORT,
    
    /** GlobusVarArgEnum(attr)
     * Set the listener backlog on a server.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used only on attrs for @ref globus_xio_server_create(). The default
     * backlog is -1 (system maximum)
     * 
     * @param listener_backlog
     *      This indicates the maximum length of the system's queue of 
     *      pending connections. Any connection attempts when the queue is
     *      full will fail. If backlog is equal to -1, then the system-specific
     *      maximum queue length will be used.
     */
    /* int                              listener_backlog */
    GLOBUS_XIO_TCP_SET_BACKLOG,
    
    /** GlobusVarArgEnum(attr)
     * Get the listener backlog on an attr.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param listener_backlog_out
     *      The backlog will be stored here.
     */
    /* int *                            listener_backlog_out */
    GLOBUS_XIO_TCP_GET_BACKLOG,
    
    /** GlobusVarArgEnum(attr)
     * Set the tcp port range to confine the server to.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used only on attrs for @ref globus_xio_server_create() where no 
     * specific service or port has been set.  It overrides the range set in
     * the GLOBUS_TCP_PORT_RANGE env variable.  If 'restrict port' is true,
     * the server's listening port will be constrained to the range specified.
     * 
     * @param listener_min_port
     *      The lower bound on the listener port. (default 0 -- no bound)
     * 
     * @param listener_max_port
     *      The upper bound on the listener port. (default 0 -- no bound)
     * 
     * @see GLOBUS_XIO_TCP_SET_RESTRICT_PORT
     *
     * string opt: listen_range=<em>int</em>,<em>int</em>
     */
    /* int                              listener_min_port,
     * int                              listener_max_port */
    GLOBUS_XIO_TCP_SET_LISTEN_RANGE,
    
    /** GlobusVarArgEnum(attr)
     * Get the tcp port range on an attr.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param listener_min_port_out
     *      The lower bound will be stored here.
     * 
     * @param listener_max_port_out
     *      The upper bound will be stored here.
     */
    /* int *                            listener_min_port_out,
     * int *                            listener_max_port_out */
    GLOBUS_XIO_TCP_GET_LISTEN_RANGE,
    
    /** GlobusVarArgEnum(attr, handle, server)
     * Get the tcp socket handle on an attr, handle, or server.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param handle_out
     *      The tcp socket will be stored here. If none is set,
     *      GLOBUS_XIO_TCP_INVALID_HANDLE will be set.
     */
    /* globus_xio_system_socket_t *     handle_out */
    GLOBUS_XIO_TCP_GET_HANDLE,
    
    /** GlobusVarArgEnum(attr)
     * Set the tcp socket to use for a handle or server.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used only on attrs for @ref globus_xio_server_create() or
     * @ref globus_xio_register_open().
     * 
     * @param handle
     *      Use this handle (fd or SOCKET) for the listener or connection.
     *      Note:  close() will not be called on this handle.
     */
    /* globus_xio_system_socket_t        handle */
    GLOBUS_XIO_TCP_SET_HANDLE,
    
    /** GlobusVarArgEnum(attr)
     * Set the interface to bind a listener or connection to.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used only on attrs for @ref globus_xio_server_create() or
     * @ref globus_xio_register_open.
     * 
     * @param interface
     *      The interface to use.  Can be a hostname or numeric IP
     *
     * string opt: iface=<em>string</em>
     */
    /* const char *                     interface */
    GLOBUS_XIO_TCP_SET_INTERFACE,
    
    /** GlobusVarArgEnum(attr)
     * Get the interface on the attr.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param interface_out
     *      A pointer to the interface will be stored here  If one is set,
     *      NULL will be passed back.  Otherwise, the interface will be
     *      duplicated with strdup() and the user should call free() on it.
     */
    /* char **                          interface_out */
    GLOBUS_XIO_TCP_GET_INTERFACE,
    
    /** GlobusVarArgEnum(attr)
     * Enable or disable the listener or connector range constraints.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used only on attrs for @ref globus_xio_server_create() or
     * @ref globus_xio_register_open().  This enables or ignores the port range
     * found in the attr or in then env.  By default, those ranges are enabled.
     * 
     * @param restrict_port
     *      GLOBUS_TRUE to enable (default), GLOBUS_FALSE to disable.
     * 
     * @see GLOBUS_XIO_TCP_SET_LISTEN_RANGE
     * @see GLOBUS_XIO_TCP_SET_CONNECT_RANGE
     */
    /* globus_bool_t                    restrict_port */
    GLOBUS_XIO_TCP_SET_RESTRICT_PORT,
    
    /** GlobusVarArgEnum(attr)
     * Get the restrict port flag.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param restrict_port_out
     *      The restrict port flag will be stored here.
     */
    /* globus_bool_t *                  restrict_port_out */
    GLOBUS_XIO_TCP_GET_RESTRICT_PORT,
    
    /** GlobusVarArgEnum(attr)
     * Reuse addr when binding.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used only on attrs for @ref globus_xio_server_create() or
     * @ref globus_xio_register_open() to determine whether or not to allow
     * reuse of addresses when binding a socket to a port number.
     * 
     * @param resuseaddr
     *      GLOBUS_TRUE to allow, GLOBUS_FALSE to disallow (default)
     *
     * string opt: reuse=<em>bool</em>
     */
    /* globus_bool_t                    resuseaddr */
    GLOBUS_XIO_TCP_SET_REUSEADDR,
    
    /** GlobusVarArgEnum(attr)
     * Get the reuseaddr flag on an attr.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param resuseaddr_out
     *      The reuseaddr flag will be stored here.
     */
    /* globus_bool_t *                  resuseaddr_out */
    GLOBUS_XIO_TCP_GET_REUSEADDR,
    
    /** GlobusVarArgEnum(attr)
     * Restrict to IPV4 only.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used only on attrs for @ref globus_xio_server_create() or
     * @ref globus_xio_register_open().  Disallow IPV6 sockets from being used
     * (default is to use either ipv4 or ipv6)
     * 
     * @param no_ipv6
     *      GLOBUS_TRUE to disallow ipv6, GLOBUS_FALSE to allow (default)
     *
     * string opt: noipv6=<em>bool</em>
     */
    /* globus_bool_t                    no_ipv6 */
    GLOBUS_XIO_TCP_SET_NO_IPV6,
    
    /** GlobusVarArgEnum(attr)
     * Get the no ipv6 flag on an attr.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param no_ipv6_out
     *      The no ipv6 flag will be stored here.
     */
    /* globus_bool_t *                  no_ipv6_out */
    GLOBUS_XIO_TCP_GET_NO_IPV6,
    
    /** GlobusVarArgEnum(attr)
     * Set the tcp port range to confine the server to.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used only on attrs for @ref globus_xio_register_open(). It overrides
     * the range set in the GLOBUS_TCP_SOURCE_RANGE env variable. If 
     * 'restrict port' is true, the connecting socket's local port will be
     * constrained to the range specified.
     * 
     * @param connector_min_port
     *      The lower bound on the listener port. (default 0 -- no bound)
     * 
     * @param connector_max_port
     *      The upper bound on the listener port. (default 0 -- no bound)
     * 
     * @see GLOBUS_XIO_TCP_SET_RESTRICT_PORT
     */
    /* int                              connector_min_port,
     * int                              connector_max_port */
    GLOBUS_XIO_TCP_SET_CONNECT_RANGE,
    
    /** GlobusVarArgEnum(attr)
     * Get the tcp source port range on an attr.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param connector_min_port_out
     *      The lower bound will be stored here.
     * 
     * @param connector_max_port_out
     *      The upper bound will be stored here.
     */
    /* int *                            connector_min_port_out,
     * int *                            connector_max_port_out */
    GLOBUS_XIO_TCP_GET_CONNECT_RANGE,
    
    /** GlobusVarArgEnum(attr, handle)
     * Enable tcp keepalive.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used on attrs for @ref globus_xio_server_create(), 
     * @ref globus_xio_register_open() and with @ref globus_xio_handle_cntl()
     * to determine whether or not to periodically send "keepalive" messages
     * on a connected socket handle. This may enable earlier detection of
     * broken connections.
     * 
     * @param keepalive
     *      GLOBUS_TRUE to enable, GLOBUS_FALSE to disable (default)
     *
     * string opt: keepalive=<em>bool</em>
     */
    /* globus_bool_t                    keepalive */
    GLOBUS_XIO_TCP_SET_KEEPALIVE,
    
    /** GlobusVarArgEnum(attr, handle)
     * Get the tcp keepalive flag.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param keepalive_out
     *      The tcp keepalive flag will be stored here.
     */
    /* globus_bool_t *                  keepalive_out */
    GLOBUS_XIO_TCP_GET_KEEPALIVE,
    
    /** GlobusVarArgEnum(attr, handle)
     * Set tcp linger.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used on attrs for @ref globus_xio_server_create(), 
     * @ref globus_xio_register_open() and with @ref globus_xio_handle_cntl()
     * to determine what to do when data is in the socket's buffer when the
     * socket is closed. If linger is set to true, then the close operation
     * will block until the socket buffers are empty, or the linger_time has 
     * expired.  If this is enabled, any data remaining after the linger time
     * has expired, will be discarded.  If this is disabled, close finishes
     * immediately, but the OS will still attempt to transmit the remaining
     * data.
     * 
     * @param linger
     *      GLOBUS_TRUE to enable, GLOBUS_FALSE to disable (default)
     * 
     * @param linger_time
     *      The time (in seconds) to block at close time if linger is true
     *      and data is queued in the socket buffer.
     */
    /* globus_bool_t                    linger,
     * int                              linger_time */
    GLOBUS_XIO_TCP_SET_LINGER,
    
    /** GlobusVarArgEnum(attr, handle)
     * Get the tcp linger flag and time.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param linger_out
     *      The linger flag will be stored here.
     * 
     * @param linger_time_out
     *      The linger time will be set here.
     */
    /* globus_bool_t *                  linger_out,
     * int *                            linger_time_out */
    GLOBUS_XIO_TCP_GET_LINGER,
    
    /** GlobusVarArgEnum(attr, handle)
     * Receive out of band data (tcp urgent data) in normal stream.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used on attrs for @ref globus_xio_server_create(), 
     * @ref globus_xio_register_open() and with @ref globus_xio_handle_cntl()
     * to choose whether out-of-band data is received in the normal data
     * queue. (Currently, there is no other way to receive OOB data)
     * 
     * @param oobinline 
     *      GLOBUS_TRUE to enable, GLOBUS_FALSE to disable (default)
     */
    /* globus_bool_t                    oobinline */
    GLOBUS_XIO_TCP_SET_OOBINLINE,
    
    /** GlobusVarArgEnum(attr, handle)
     * Get the oobinline flag.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param oobinline_out
     *      The oobinline flag will be stored here.
     */
    /* globus_bool_t *                  oobinline_out */
    GLOBUS_XIO_TCP_GET_OOBINLINE,
    
    /** GlobusVarArgEnum(attr, handle)
     * Set the tcp socket send buffer size.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used on attrs for @ref globus_xio_server_create(), 
     * @ref globus_xio_register_open() and with @ref globus_xio_handle_cntl()
     * to set the size of the send buffer used on the socket.
     * 
     * @param sndbuf
     *      The send buffer size in bytes to use. (default is system specific)
     *
     * string opt: sndbuf=<em>formatted int</em>
     */
    /* int                              sndbuf */
    GLOBUS_XIO_TCP_SET_SNDBUF,
    
    /** GlobusVarArgEnum(attr, handle)
     * Get the tcp send buffer size on the attr or handle.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param sndbuf_out
     *      The send buffer size will be stored here.
     */
    /* int *                            sndbuf_out */
    GLOBUS_XIO_TCP_GET_SNDBUF,
    
    /** GlobusVarArgEnum(attr, handle)
     * Set the tcp socket receive buffer size.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used on attrs for @ref globus_xio_server_create(), 
     * @ref globus_xio_register_open() and with @ref globus_xio_handle_cntl()
     * to set the size of the receive buffer used on the socket. The receive
     * buffer size is often used by the operating system to choose the
     * appropriate TCP window size.
     * 
     * @param rcvbuf
     *      The receive buffer size in bytes. (default is system specific)
     *
     * string opt: rcvbuf=<em>formatted int</em>
     */
    /* int                              rcvbuf */
    GLOBUS_XIO_TCP_SET_RCVBUF,
    
    /** GlobusVarArgEnum(attr, handle)
     * Get the tcp receive buffer size on the attr or handle.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param rcvbuf_out
     *      The receive buffer size will be stored here.
     */
    /* int *                            rcvbuf_out */
    GLOBUS_XIO_TCP_GET_RCVBUF,
    
    /** GlobusVarArgEnum(attr, handle)
     * Disable Nagle's algorithm.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used on attrs for @ref globus_xio_server_create(), 
     * @ref globus_xio_register_open() and with @ref globus_xio_handle_cntl()
     * to determine whether or not to disable Nagle's algorithm. If set to 
     * GLOBUS_TRUE, the socket will send packets as soon as possible with
     * no unnecessary delays introduced.
     * 
     * @param nodelay
     *      GLOBUS_TRUE to disable nagle, GLOBUS_FALSE to enable (default)
     *
     * string opt: nodelay=<em>bool</em>
     */
    /* globus_bool_t                    nodelay */
    GLOBUS_XIO_TCP_SET_NODELAY,
    
    /** GlobusVarArgEnum(attr, handle)
     * Get the tcp nodelay flag.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param nodelay_out
     *      The no delay flag will be stored here.
     */
    /* globus_bool_t *                  nodelay_out */
    GLOBUS_XIO_TCP_GET_NODELAY,
    
    /**GlobusVarArgEnum(dd)
     * Set tcp send flags.
     * @ingroup globus_xio_tcp_driver_cntls
     * Used only for data descriptors to write calls.
     * 
     * @param send_flags
     *      The flags to use when sending data.
     * 
     * @see globus_xio_tcp_send_flags_t
     */
    /* int                              send_flags */
    GLOBUS_XIO_TCP_SET_SEND_FLAGS,
    
    /**GlobusVarArgEnum(dd)
     * Get tcp send flags.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param send_flags_out
     *      The flags to use will be stored here.
     */
    /* int *                            send_flags_out */
    GLOBUS_XIO_TCP_GET_SEND_FLAGS,
    
    /**GlobusVarArgEnum(handle, server)
     * Get local socket info.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param contact_string_out
     *      A pointer to a contact string for the local end of a connected
     *      socket or listener will be stored here.  The user should free() it
     *      when done with it.  It will be in the format: \<hostname\>:\<port\>
     * 
     * @see globus_xio_server_get_contact_string()
     * @see GLOBUS_XIO_GET_LOCAL_CONTACT
     */
    /* char **                          contact_string_out */
    GLOBUS_XIO_TCP_GET_LOCAL_CONTACT,
    
    /**GlobusVarArgEnum(handle, server)
     * Get local socket info.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param contact_string_out
     *      A pointer to a contact string for the local end of a connected
     *      socket or listener will be stored here.  The user should free() it
     *      when done with it.   It will be in the format: \<ip\>:\<port\>
     * 
     * @see GLOBUS_XIO_GET_LOCAL_NUMERIC_CONTACT
     */
    /* char **                          contact_string_out */
    GLOBUS_XIO_TCP_GET_LOCAL_NUMERIC_CONTACT,
    
    /**GlobusVarArgEnum(handle)
     * Get remote socket info.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param contact_string_out
     *      A pointer to a contact string for the remote end of a connected
     *      socket will be stored here.  The user should free() it
     *      when done with it. It will be in the format: \<hostname\>:\<port\>
     * 
     * @see GLOBUS_XIO_GET_REMOTE_CONTACT
     */
    /* char **                          contact_string_out */
    GLOBUS_XIO_TCP_GET_REMOTE_CONTACT,
    
    /**GlobusVarArgEnum(handle)
     * Get remote socket info.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param contact_string_out
     *      A pointer to a contact string for the remote end of a connected
     *      socket will be stored here.  The user should free() it
     *      when done with it.   It will be in the format: \<ip\>:\<port\>
     * 
     * @see GLOBUS_XIO_GET_REMOTE_NUMERIC_CONTACT
     */
    /* char **                          contact_string_out */
    GLOBUS_XIO_TCP_GET_REMOTE_NUMERIC_CONTACT,
    
    /**GlobusVarArgEnum(attr)
     * Change the default attr values.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param affect_global
     *      If GLOBUS_TRUE, any future cntls on this attr will access
     *      the global default attr (which all new attrs are initialized from)
     *      The default is GLOBUS_FALSE.  Note:  this should only be used at
     *      the application level and there should only be one.  There is no
     *      mutex protecting the global attr.  This feature should not be
     *      abused.  There are some attrs that make no sense to change
     *      globally.  Attrs that do include the tcp port range stuff, socket
     *      buffer sizes, etc.
     */
    /* globus_bool_t                    affect_global */
    GLOBUS_XIO_TCP_AFFECT_ATTR_DEFAULTS,
    
    /** GlobusVarArgEnum(attr, handle)
     * Enable true blocking io when making globus_xio_read/write() calls.
     * Note: use with caution.  you can deadlock an entire app with this.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param use_blocking_io
     *      If GLOBUS_TRUE, true blocking io will be enabled.
     *      GLOBUS_FALSE will disable it (default);
     */
    /* globus_bool_t                    use_blocking_io */
    GLOBUS_XIO_TCP_SET_BLOCKING_IO,
    
    /** GlobusVarArgEnum(attr, handle)
     * Get the blocking io status in use or in attr.
     * @ingroup globus_xio_tcp_driver_cntls
     * 
     * @param use_blocking_io_out
     *      The flag will be set here.  GLOBUS_TRUE for enabled.
     */
    /* globus_bool_t *                  use_blocking_io_out */
    GLOBUS_XIO_TCP_GET_BLOCKING_IO
    
} globus_xio_tcp_cmd_t;


/**
 * TCP driver specific types
 * @ingroup globus_xio_tcp_driver_types
 * @hideinitializer
 */
typedef enum
{
    /**
     * Use this with @ref GLOBUS_XIO_TCP_SET_SEND_FLAGS to send a TCP message
     * out of band (Urgent data flag set)
     */
    GLOBUS_XIO_TCP_SEND_OOB = MSG_OOB
} globus_xio_tcp_send_flags_t;

#ifdef __cplusplus
}
#endif

#endif
