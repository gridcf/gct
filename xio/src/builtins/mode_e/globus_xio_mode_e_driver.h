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

#ifndef GLOBUS_XIO_MODE_E_DRIVER_H
#define GLOBUS_XIO_MODE_E_DRIVER_H

/**
 * @file globus_xio_mode_e_driver.h
 * @brief XIO MODE_E Driver
 */

#include "globus_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_xio_mode_e_driver Globus XIO MODE_E Driver
 * @ingroup globus_xio
 * @brief Globus XIO MODE_E Driver
 */

/**
 * @defgroup globus_xio_mode_e_driver_instance Opening/Closing
 * @ingroup globus_xio_mode_e_driver
 * @brief Opening/Closing
 *
 * An XIO handle with the mode_e driver can be created with either
 * @ref globus_xio_handle_create() or @ref globus_xio_server_register_accept().
 *
 * If the handle is created with @ref globus_xio_handle_create(), the contact
 * string passed to ref globus_xio_register_open() call must contain
 * a host name and service/port. The number of streams required can be 
 * specified on the attr using @ref GLOBUS_XIO_MODE_E_SET_NUM_STREAMS (default
 * is one stream). The stack of drivers to be used on the streams can be
 * specified on the attr using @ref GLOBUS_XIO_MODE_E_SET_STACK (default is
 * a stack containing TCP driver). 
 *
 * When the XIO handle is closed, the mode_e driver will destroy its internal
 * resources and close the stream(s).
 */

/**
 * @defgroup globus_xio_mode_e_driver_io Reading/Writing
 * @ingroup globus_xio_mode_e_driver
 * @brief Reading/Writing
 *
 * Mode E is unidirectional. Clients can only write and the server can only 
 * read.
 * The @ref globus_xio_register_read() enforce that the waitforbytes parameter
 * should be one. When multiple transport streams are used between the client 
 * and the server, data might not be delivered in order.
 * @ref globus_xio_data_descriptor_cntl() can be used to get the offset of the
 * data.
 *
 * @ref globus_xio_register_write() does not enforce any restriction on the
 * waitforbytes parameter.
 *
 * In any case, when an error or EOF occurs before the waitforbytes request
 * has been met, the outgoing nbytes is set to the amount of data actually
 * read/written before the error or EOF occurred.
 */

/**
 * @defgroup globus_xio_mode_e_driver_server Server
 * @ingroup globus_xio_mode_e_driver
 * @brief Server
 *
 * @ref globus_xio_server_create() causes a mode_e listener to be created
 * and listened upon.  @ref globus_xio_server_register_accept() performs an
 * asynchronous accept(). @ref globus_xio_server_register_close() cleans up
 * the internal resources associated with the mode_e server.
 *
 * All accepted handles inherit all mode_e specific attributes set in the attr 
 * to @ref globus_xio_server_create()
 */

/**
 * @defgroup globus_xio_mode_e_driver_envs Env Variables
 * @ingroup globus_xio_mode_e_driver
 * @brief Env Variables
 *
 * The mode_e driver uses the following environment variable
 * - GLOBUS_XIO_MODE_E_DEBUG Available if using a debug build.  See 
 * globus_debug.h for format.  
 */

/**
 * @defgroup globus_xio_mode_e_driver_cntls Attributes and Cntls
 * @ingroup globus_xio_mode_e_driver
 * @brief Attributes and Cntls
 *
 * Mode_e driver specific attrs and cntls.
 *
 * @see globus_xio_attr_cntl()
 * @see globus_xio_handle_cntl()
 * @see globus_xio_server_cntl()
 * @see globus_xio_data_descriptor_cntl()
 */
/**
 * @defgroup globus_xio_mode_e_driver_types Types
 * @ingroup globus_xio_mode_e_driver
 * @brief Types
 */
/**
 * @defgroup globus_xio_mode_e_driver_errors Error Types
 * @ingroup globus_xio_mode_e_driver
 * @brief Error Types
 *
 * The errors reported by MODE_E driver include
 * GLOBUS_XIO_ERROR_COMMAND, GLOBUS_XIO_ERROR_MEMORY, GLOBUS_XIO_ERROR_STATE, 
 * GLOBUS_XIO_ERROR_PARAMETER, GLOBUS_XIO_ERROR_EOF, 
 * GLOBUS_XIO_ERROR_CANCELED, @ref GLOBUS_XIO_MODE_E_HEADER_ERROR
 * 
 * @see globus_xio_driver_error_match()
 * @see globus_error_errno_match()
 */

/**
 * MODE_E driver specific error types
 * @ingroup globus_xio_mode_e_driver_errors
 */
typedef enum
{   
    /**
     * Indicates that the mode_e header is erroneous
     */
    GLOBUS_XIO_MODE_E_HEADER_ERROR
    
} globus_xio_mode_e_error_type_t;


/** doxygen varargs filter stuff
 * GlobusVarArgDefine(
 *      attr, globus_result_t, globus_xio_attr_cntl, attr, driver)
 * GlobusVarArgDefine(
 *      handle, globus_result_t, globus_xio_handle_cntl, handle, driver)
 * GlobusVarArgDefine(
 *      dd, globus_result_t, globus_xio_data_descriptor_cntl, dd, driver)
 */

/**
 * MODE_E driver specific cntls
 * @ingroup globus_xio_mode_e_driver_cntls
 */

typedef enum
{
    /** GlobusVarArgEnum(attr)
     * Set the stack (of xio drivers) to be used for the connection(s).
     * @ingroup globus_xio_mode_e_driver_cntls
     * Do not create a new ftp client handle, use this handle instead.
     * 
     * @param stack
     *      Specifies the stack to use for the connection(s).
     *      Note: this stack will not be destroyed.
     */
    /* globus_xio_stack_t 			stack */
    GLOBUS_XIO_MODE_E_SET_STACK,

    /** GlobusVarArgEnum(attr)
     * Get the stack on the attr.
     * @ingroup globus_xio_mode_e_driver_cntls
     * 
     * @param stack_out
     *      The stack will be stored here. If none is set, GLOBUS_NULL will be 
     *      set.
     */
    /* globus_xio_stack_t *			stack_out */
    GLOBUS_XIO_MODE_E_GET_STACK,

    /** GlobusVarArgEnum(attr)
     * Set the number of streams to be used between the client and the
     * server.
     * @ingroup globus_xio_mode_e_driver_cntls
     *
     * @param num_streams
     *      Specifies the number of streams to use.
     */
    /* int					num_streams */
    GLOBUS_XIO_MODE_E_SET_NUM_STREAMS,

    /** GlobusVarArgEnum(attr)
     * Get the number of streams on the attr.
     * @ingroup globus_xio_mode_e_driver_cntls
     *
     * @param num_streams_out
     *      The stream count will be stored here.
     */
    /* int *					num_streams_out */
    GLOBUS_XIO_MODE_E_GET_NUM_STREAMS,

    /** GlobusVarArgEnum(attr)
     * Set flag to indicate whether the data read from user would always be 
     * preceded by an offset read or not. The user can do a read with 
     * wait_for_bytes set to zero, to find the offset of the data that he is 
     * going to get in his next read operation
     * @ingroup globus_xio_mode_e_driver_cntls
     *
     * @param offset_reads
     *      GLOBUS_TRUE to enable offset reads, GLOBUS_FALSE to disable 
     * offset reads (default).
     */
    /* globus_bool_t				offset_reads */
    GLOBUS_XIO_MODE_E_SET_OFFSET_READS,

    /** GlobusVarArgEnum(attr)
     * Get OFFSET_READS flag on the attr.
     * @ingroup globus_xio_mode_e_driver_cntls
     *
     * @param offset_reads_out
     *      The OFFSET_READS flag will be stored here.
     */
    /* globus_bool_t *				offset_reads_out */
    GLOBUS_XIO_MODE_E_GET_OFFSET_READS,

    /** GlobusVarArgEnum(attr)
     * Set flag to indicate whether EODC will be set manually by the user on 
     * a data_desc or the driver has to calculate the EODC
     * @ingroup globus_xio_mode_e_driver_cntls
     *
     * @param manual_eodc
     *      GLOBUS_TRUE to set EODC manually, GLOBUS_FALSE to not set EODC
     * manually (default).
     */
    /* globus_bool_t				manual_eodc */
    GLOBUS_XIO_MODE_E_SET_MANUAL_EODC,

    /** GlobusVarArgEnum(attr)
     * Get MANUAL_EODC flag on the attr.
     * @ingroup globus_xio_mode_e_driver_cntls
     *
     * @param manual_eodc_out
     *      The MANUAL_EODC flag will be stored here.
     */
    /* globus_bool_t *				manual_eodc_out */
    GLOBUS_XIO_MODE_E_GET_MANUAL_EODC,

    /** GlobusVarArgEnum(dd)
     * Set SEND_EOD flag
     * @ingroup globus_xio_mode_e_driver_cntls
     * Used only for data descriptors to write calls.
     *
     * @param send_eod
     *	    GLOBUS_TRUE to send EOD, GLOBUS_FALSE to not send EOD (default).
     */
    /* globus_bool_t				send_eod */
    GLOBUS_XIO_MODE_E_SEND_EOD,

    /** GlobusVarArgEnum(handle)
     * Set EOD count
     * @ingroup globus_xio_mode_e_driver_cntls
     * Used only if MANUAL_EODC flag is set to GLOBUS_TRUE.
     *
     * @param eod_count
     *      specifies the eod count 
     */
    /* int					eod_count */
    GLOBUS_XIO_MODE_E_SET_EODC,

    /** GlobusVarArgEnum(dd)
     * Get offset of the next available data 
     * @ingroup globus_xio_mode_e_driver_cntls
     * Used only if OFFSET_READS is enabled.
     *
     * @param offset_out
     *      offset will be stored here
     */
    /* globus_off_t *				offset_out */
    GLOBUS_XIO_MODE_E_DD_GET_OFFSET,

    /** GlobusVarArgEnum(attr)
     * Set the attr to be used with the stack set from 
     * GLOBUS_XIO_MODE_E_SET_STACK.
     * @ingroup globus_xio_mode_e_driver_cntls
     * Do not create a new ftp client handle, use this handle instead.
     *
     * @param stack
     *      Specifies the stack to use for the connection(s).
     *      Note: this stack will not be destroyed.
     */
    /* globus_xio_stack_t           stack */

    GLOBUS_XIO_MODE_E_SET_STACK_ATTR,

    /** GlobusVarArgEnum(attr)
     * Get the attr that will be used with the stack.  This is intended for
     * use with GLOBUS_XIO_MODE_E_SET_STACK.
     * @ingroup globus_xio_mode_e_driver_cntls
     *
     * @param stack_out
     *      The stack will be stored here. If none is set, GLOBUS_NULL will be
     *      set.
     */
    /* globus_xio_attr_t *         stack_out */

    GLOBUS_XIO_MODE_E_GET_STACK_ATTR

} globus_xio_mode_e_cmd_t;	

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_XIO_MODE_E_DRIVER_H */
