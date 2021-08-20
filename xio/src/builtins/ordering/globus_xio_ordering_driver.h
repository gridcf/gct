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

#ifndef GLOBUS_XIO_ORDERING_DRIVER_H
#define GLOBUS_XIO_ORDERING_DRIVER_H

/**
 * @file globus_xio_ordering_driver.h
 * @brief XIO ORDERING Driver
 */

#include "globus_common.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @defgroup globus_xio_ordering_driver Globus XIO ORDERING Driver
 * @ingroup globus_xio
 * @brief Globus XIO ORDERING Driver
 */

/**
 * @defgroup globus_xio_ordering_driver_instance Opening/Closing
 * @ingroup globus_xio_ordering_driver
 * @brief Opening/Closing
 *
 * Ordering driver is a transform driver and thus has to be used on top of a 
 * transport driver. An XIO handle with the ordering driver can be created with 
 * either @ref globus_xio_handle_create() or 
 * @ref globus_xio_server_register_accept().
 *
 * When the XIO handle is closed, the ordering driver will destroy its internal
 * resources.
 */

/**
 * @defgroup globus_xio_ordering_driver_io Reading/Writing
 * @ingroup globus_xio_ordering_driver
 * @brief Reading/Writing
 *
 * Ordering driver does not allow multiple @ref globus_xio_register_read() 
 * to be outstanding. This limitation is there to enforce that the users get 
 * the read callback in order. There is a known issue in enforcing the order
 * in which read callbacks are delivered with multiple outstanding reads. This
 * limitation does not restrict the use of parallel reads feature provided by
 * the underlying transport driver. @ref GLOBUS_XIO_ORDERING_SET_MAX_READ_COUNT 
 * on the attr can be used to specify the number of parallel reads. Ordering
 * will have a maximum of this many number of reads outstanding to the driver
 * below it on the stack. It buffers the data read and delivers it to the user
 * in order.
 *
 * @ref globus_xio_register_write() does not enforce any restriction.
 *
 */

/**
 * @defgroup globus_xio_ordering_driver_envs Env Variables
 * @ingroup globus_xio_ordering_driver
 * @brief Env Variables
 *
 * The ordering driver uses the following environment variable
 * - GLOBUS_XIO_ORDERING_DEBUG Available if using a debug build.  See
 * globus_debug.h for format.
 */

/**
 * @defgroup globus_xio_ordering_driver_cntls Attributes and Cntls
 * @ingroup globus_xio_ordering_driver
 * @brief Attributes and Cntls
 *
 * Ordering driver specific attrs and cntls.
 *
 * @see globus_xio_attr_cntl()
 * @see globus_xio_handle_cntl()
 */
/**
 * @defgroup globus_xio_ordering_driver_types Types
 * @ingroup globus_xio_ordering_driver
 * @brief Types
 */
/**
 * @defgroup globus_xio_ordering_driver_errors Error Types
 * @ingroup globus_xio_ordering_driver
 * @brief Error Types
 *
 * The errors reported by ORDERING driver include
 * GLOBUS_XIO_ERROR_COMMAND, GLOBUS_XIO_ERROR_MEMORY, GLOBUS_XIO_ERROR_STATE,
 * GLOBUS_XIO_ERROR_CANCELED
 *
 * @see globus_xio_driver_error_match()
 * @see globus_error_errno_match()
 */

/** doxygen varargs filter stuff
 * GlobusVarArgDefine(
 *      attr, globus_result_t, globus_xio_attr_cntl, attr, driver)
 * GlobusVarArgDefine(
 *      handle, globus_result_t, globus_xio_handle_cntl, handle, driver)
 */

/**
 * ORDERING driver specific error types
 * @ingroup globus_xio_ordering_driver_errors
 */
typedef enum
{
    /**
     * Indicates that an error occurred in reading data
     */
    GLOBUS_XIO_ORDERING_ERROR_READ,
    /**
     * Indicates an error occurred in canceling an operation
     */
    GLOBUS_XIO_ORDERING_ERROR_CANCEL

} globus_xio_ordering_error_type_t;


/**
 * ORDERING driver specific cntls
 * @ingroup globus_xio_ordering_driver_cntls
 */
typedef enum
{
    /** GlobusVarArgEnum(handle)
     * Set offset for the next IO operation. This is not allowed when there is
     * an outstanding IO operation. This operation clears all the buffered data.
     * @ingroup globus_xio_ordering_driver_cntls
     *
     * @param offset
     *      Specifies the offset to use in the next IO operation.
     */
    /* globus_off_t			offset */
    GLOBUS_XIO_ORDERING_SET_OFFSET,

    /** GlobusVarArgEnum(attr)
     * Set the maximum number of reads that ordering driver can have outstanding
     * on driver(s) below.
     * @ingroup globus_xio_ordering_driver_cntls
     *
     * @param max_read_count
     *      Specifies the maximum number of parallel reads (default is 1).
     */
    /* int				max_read_count */
    GLOBUS_XIO_ORDERING_SET_MAX_READ_COUNT,

    /** GlobusVarArgEnum(attr)
     * Get the maximum number of parallel reads set on the attr.
     * @ingroup globus_xio_ordering_driver_cntls
     *
     * @param max_read_count_out
     *      The maximum number of parallel reads allowed will be stored here.
     */
    /* int *				max_read_count_out */
    GLOBUS_XIO_ORDERING_GET_MAX_READ_COUNT,

    /** GlobusVarArgEnum(attr)
     * This driver can be used in 2 modes; ordering (care about offsets of the 
     * data read - underlying transport driver may deliver data out of order -
     * this driver will rearrange data based on the offset and deliver inorder
     * to user) and buffering (do not care about offsets - just buffer the data
     * read abd deliver it when requested). This attribute control can be used
     * to enable buffering.
     * @ingroup globus_xio_ordering_driver_cntls
     *
     * @param buffering
     *	    GLOBUS_TRUE to enable buffering, GLOBUS_FALSE (default) to disable 
     * buffering.
     */
    /* globus_bool_t			buffering */
    GLOBUS_XIO_ORDERING_SET_BUFFERING,

    /** GlobusVarArgEnum(attr)
     * Get the buffering flag on the attr.
     * @ingroup globus_xio_ordering_driver_cntls
     *
     * @param buffering_out
     *      Buffering flag will be stored in here.
     */
    /* globus_bool_t *			buffering_out */
    GLOBUS_XIO_ORDERING_GET_BUFFERING,

    /** GlobusVarArgEnum(attr)
     * Set the size of the buffer that ordering driver creates to use for 
     * reading data from the driver below it.
     * @ingroup globus_xio_ordering_driver_cntls
     *
     * @param buf_size
     *      Specifies the buffer size for internal reads (default is 100 KB).
     */
    /* int				buf_size */
    GLOBUS_XIO_ORDERING_SET_BUF_SIZE,

    /** GlobusVarArgEnum(attr)
     * Get the size of the buffer used for the internal reads.
     * @ingroup globus_xio_ordering_driver_cntls
     *
     * @param buf_size_out
     *      The buffer size will be stored in here.
     */ 
    /* int *				buf_size_out */
    GLOBUS_XIO_ORDERING_GET_BUF_SIZE,

    /** GlobusVarArgEnum(attr)
     * Set the maximum number of buffers that this driver can create for
     * reading data from the driver below it.
     * @ingroup globus_xio_ordering_driver_cntls
     *
     * @param max_buf_count
     *      Specifies the max buffer count for internal reads (default is 100).
     */
    /* int				max_buf_count */
    GLOBUS_XIO_ORDERING_SET_MAX_BUF_COUNT,

    /** GlobusVarArgEnum(attr)
     * Get the maximum buffer count set on the attr.
     * @ingroup globus_xio_ordering_driver_cntls
     *
     * @param max_buf_count_out
     *      The maximun buffer count will be stored in here.
     */
    /* int *				max_buf_count_out */
    GLOBUS_XIO_ORDERING_GET_MAX_BUF_COUNT

} globus_xio_ordering_cmd_t;	

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_XIO_ORDERING_DRIVER_H */
