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

#ifndef GLOBUS_XIO_FILE_DRIVER_H
#define GLOBUS_XIO_FILE_DRIVER_H
/**
 * @file globus_xio_file_driver.h
 * @brief XIO File Driver
 */
#include "globus_xio_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_xio_file_driver Globus XIO File Driver
 * @ingroup globus_xio
 * @brief Globus XIO File Driver
 *
 * File I/O driver
 */

/**
 * @defgroup globus_xio_file_driver_instance Opening/Closing
 * @ingroup globus_xio_file_driver
 * @brief Opening/Closing
 * 
 * An XIO handle with the file driver can be created with
 * @ref globus_xio_handle_create()
 * 
 * If there is no handle set on the attr passed to the 
 * @ref globus_xio_open() call, it performs the equivalent of an
 * open() call.  In this case, the contact string must contain
 * either a pathname or one of stdin://, stdout://, or stderr://.  If a
 * pathname is used, that path is opened.  If one of the schemes are used
 * the corresponding stdio handle is used (retrieved with fileno()).
 * 
 * In either of the above cases, it is most efficient to call the blocking
 * version of globus_xio_open().  It is also safe to call within a locked
 * critical section.
 * 
 * When the XIO handle is closed, the file driver will destroy its internal
 * resources and close the fd (unless this fd was set on an attr or converted
 * from one of the stdio handles).
 */
 
/**
 * @defgroup globus_xio_file_driver_io Reading/Writing
 * @ingroup globus_xio_file_driver
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
 * 
 * You may either use GLOBUS_XIO_FILE_SEEK or GLOBUS_XIO_SEEK to position the
 * file pointer before each read or write or you can specify the
 * desired offset on a data descriptor with the xio cmd,
 * GLOBUS_XIO_DD_SET_OFFSET.  Simultaneous reading and writing is only
 * predictable if the data descriptor method is used.
 */
 
/**
 * @defgroup globus_xio_file_driver_envs Environment Variables
 * @ingroup globus_xio_file_driver
 * @brief Environment Variables
 * 
 * The file driver uses the following environment variables
 * - GLOBUS_XIO_FILE_DEBUG Available if using a debug build. See globus_debug.h
 *      for format.  The File driver defines the levels TRACE for all function
 *      call tracing and INFO for write buffer sizes
 * - GLOBUS_XIO_SYSTEM_DEBUG Available if using a debug build.
 *      See globus_debug.h for format. The File driver uses globus_xio_system
 *      (along with the TCP and UDP drivers) which defines the following
 *      levels: TRACE for all function call tracing, DATA for data read and
 *      written counts, INFO for some special events, and RAW which dumps the
 *      raw buffers actually read or written. This can contain binary data, 
 *      so be careful when you enable it.
 */

/**
 * @defgroup globus_xio_file_driver_cntls Attributes and Cntls
 * @ingroup globus_xio_file_driver
 * @brief Attributes and Cntls
 * 
 * File driver specific attrs and cntls.
 * 
 * @see globus_xio_attr_cntl()
 * @see globus_xio_handle_cntl()
 */
/**
 * @defgroup globus_xio_file_driver_types Types
 * @ingroup globus_xio_file_driver
 * @brief Types
 */
/**
 * @defgroup globus_xio_file_driver_errors Error Types
 * @ingroup globus_xio_file_driver
 * @brief Error Types
 * 
 * The File driver is very close to the system code, so most errors
 * reported by it are converted from the system errno. A few of the exceptions
 * are GLOBUS_XIO_ERROR_EOF, GLOBUS_XIO_ERROR_COMMAND,
 * GLOBUS_XIO_ERROR_CONTACT_STRING, and GLOBUS_XIO_ERROR_CANCELED
 * 
 * @see globus_error_errno_match()
 */

/**
 * Invalid handle type
 * @ingroup globus_xio_file_driver_types
 * @hideinitializer
 * @see GLOBUS_XIO_FILE_SET_HANDLE
 */
#define GLOBUS_XIO_FILE_INVALID_HANDLE GLOBUS_XIO_SYSTEM_INVALID_FILE

/** doxygen varargs filter stuff
 * GlobusVarArgDefine(
 *      attr, globus_result_t, globus_xio_attr_cntl, attr, driver)
 * GlobusVarArgDefine(
 *      handle, globus_result_t, globus_xio_handle_cntl, handle, driver)
 */

/**
 * File driver specific cntls
 * @ingroup globus_xio_file_driver_cntls
 */
typedef enum
{
    /** GlobusVarArgEnum(attr)
     * Set the file create mode.
     * @ingroup globus_xio_file_driver_cntls
     * Use this to set the permissions a non-existent file is created with,
     * The default mode is 0644.
     * 
     * @param mode
     *      A bitwise OR of all the modes desired
     * 
     * @see globus_xio_file_mode_t
     *
     * string opt: mode=<em>int</em>
     */
    /* int                              mode */
    GLOBUS_XIO_FILE_SET_MODE,
    
    /** GlobusVarArgEnum(attr)
     * Get the file create mode.
     * @ingroup globus_xio_file_driver_cntls
     * 
     * @param mode_out
     *      The current mode will be stored here.
     */
    /* int *                            mode_out */
    GLOBUS_XIO_FILE_GET_MODE,
    
    /** GlobusVarArgEnum(attr)
     * Set the file open flags.
     * @ingroup globus_xio_file_driver_cntls
     * The default flags specify to create the file if it doesn't exist,
     * open it for reading and writing, and interpret it as a binary file.
     * 
     * @param flags
     *      A bitwise OR of all the flags desired
     * 
     * @see globus_xio_file_flag_t
     *
     * string opt: flags=<em>int</em>
     */
    /* int                              flags */
    GLOBUS_XIO_FILE_SET_FLAGS,
    
    /** GlobusVarArgEnum(attr)
     * Get the file open flags.
     * @ingroup globus_xio_file_driver_cntls
     * 
     * @param flags_out
     *      The current flags will be stored here.
     */
    /* int *                            flags_out */
    GLOBUS_XIO_FILE_GET_FLAGS,
    
    /** GlobusVarArgEnum(attr)
     * Set the file truncate offset.
     * @ingroup globus_xio_file_driver_cntls
     * Use this in conjunction with the @ref GLOBUS_XIO_FILE_TRUNC flag
     * to truncate a file to a non-zero offset. If the file was larger
     * than offset bytes, the extra data is lost.  If the file was shorter or
     * non-existent, it is extended and the extended part reads as zeros.
     * (default is 0)
     * 
     * @param offset
     *      The desired size of the file.
     */
    /* globus_off_t                     offset */
    GLOBUS_XIO_FILE_SET_TRUNC_OFFSET,
    
    /** GlobusVarArgEnum(attr)
     * Get the file truncate offset.
     * @ingroup globus_xio_file_driver_cntls
     * 
     * @param offset_out
     *      The offset will be stored here.
     */
    /* globus_off_t *                   offset_out */
    GLOBUS_XIO_FILE_GET_TRUNC_OFFSET,
    
    /** GlobusVarArgEnum(attr)
     * Set the file handle to use.
     * @ingroup globus_xio_file_driver_cntls
     * Do not open a new file, use this pre-opened handle instead.
     * 
     * @param handle
     *      Use this handle (fd or HANDLE) for the file.
     *      Note:  close() will not be called on this handle.
     */
    /* globus_xio_system_file_t         handle */
    GLOBUS_XIO_FILE_SET_HANDLE,
    
    /** GlobusVarArgEnum(attr, handle)
     * Get the file handle in use or in attr.
     * @ingroup globus_xio_file_driver_cntls
     * 
     * @param handle_out
     *      The file handle (fd or HANDLE) will be stored here. If none is set,
     *      GLOBUS_XIO_TCP_INVALID_HANDLE will be set.
     */
    /* globus_xio_system_file_t *       handle_out */
    GLOBUS_XIO_FILE_GET_HANDLE,
    
    /** GlobusVarArgEnum(attr, handle)
     * Enable true blocking io when making globus_xio_read/write() calls.
     * Note: use with caution.  You can deadlock an entire app with this.
     * @ingroup globus_xio_file_driver_cntls
     * 
     * @param use_blocking_io
     *      If GLOBUS_TRUE, true blocking io will be enabled.
     *      GLOBUS_FALSE will disable it (default);
     */
    /* globus_bool_t                    use_blocking_io */
    GLOBUS_XIO_FILE_SET_BLOCKING_IO,
    
    /** GlobusVarArgEnum(attr, handle)
     * Get the blocking io status in use or in attr.
     * @ingroup globus_xio_file_driver_cntls
     * 
     * @param use_blocking_io_out
     *      The flag will be set here.  GLOBUS_TRUE for enabled.
     *
     * string opt: blocking=<em>bool</em>
     */
    /* globus_bool_t *                  use_blocking_io_out */
    GLOBUS_XIO_FILE_GET_BLOCKING_IO,
    
    /** GlobusVarArgEnum(handle)
     * Reposition read/write file offset.
     * @ingroup globus_xio_file_driver_cntls
     * 
     * @param in_out_offset
     *      Specify the desired offset (according to whence).  On success,
     *      the actual file offset will be stored here.
     * 
     * @param whence
     *      Specify how offset should be interpreted.
     * 
     * @see globus_xio_file_whence_t
     * @see GLOBUS_XIO_SEEK
     */
    /* globus_off_t *                   in_out_offset,
     * globus_xio_file_whence_t         whence */
    GLOBUS_XIO_FILE_SEEK
} globus_xio_file_attr_cmd_t;

/**
 * File driver open flags
 * @ingroup globus_xio_file_driver_types
 * @hideinitializer
 * OR together all the flags you want
 * @see GLOBUS_XIO_FILE_SET_FLAGS
 */
typedef enum
{
    /** Create a new file if it doesn't exist (default) */
    GLOBUS_XIO_FILE_CREAT               = O_CREAT,
    /** Fail if file already exists */
    GLOBUS_XIO_FILE_EXCL                = O_EXCL,
    /** Open for read only */
    GLOBUS_XIO_FILE_RDONLY              = O_RDONLY,
    /** Open for write only */
    GLOBUS_XIO_FILE_WRONLY              = O_WRONLY,
    /** Open for reading and writing (default) */
    GLOBUS_XIO_FILE_RDWR                = O_RDWR,
    /** Truncate file @see GLOBUS_XIO_FILE_SET_TRUNC_OFFSET */
    GLOBUS_XIO_FILE_TRUNC               = O_TRUNC,
    /** Open file for appending */
    GLOBUS_XIO_FILE_APPEND              = O_APPEND,
#ifdef TARGET_ARCH_CYGWIN
    GLOBUS_XIO_FILE_BINARY              = O_BINARY,
    GLOBUS_XIO_FILE_TEXT                = O_TEXT
#else
    /** File is binary (default) */
    GLOBUS_XIO_FILE_BINARY              = 0,
    /** File is text */
    GLOBUS_XIO_FILE_TEXT                = 0
#endif
} globus_xio_file_flag_t;

/**
 * File driver create mode
 * @ingroup globus_xio_file_driver_types
 * @hideinitializer
 * OR these modes together to get the mode you want.
 * @see GLOBUS_XIO_FILE_SET_MODE
 * 
 * NOTE: for Win32, you only have a choice between read-only and read-write.
 * If the chosen mode does not specify writing, the file will be read only
 */
typedef enum
{
    /** User read, write, and execute */
    GLOBUS_XIO_FILE_IRWXU               = S_IRWXU,
    /** User read */
    GLOBUS_XIO_FILE_IRUSR               = S_IRUSR,
    /** User write */
    GLOBUS_XIO_FILE_IWUSR               = S_IWUSR,
    /** User execute */
    GLOBUS_XIO_FILE_IXUSR               = S_IXUSR,
    /** Others read, write, and execute */
    GLOBUS_XIO_FILE_IRWXO               = S_IRWXO,
    /** Others read */
    GLOBUS_XIO_FILE_IROTH               = S_IROTH,
    /** Others write */
    GLOBUS_XIO_FILE_IWOTH               = S_IWOTH,
    /** Others execute */
    GLOBUS_XIO_FILE_IXOTH               = S_IXOTH,
    /** Group read, write, and execute */
    GLOBUS_XIO_FILE_IRWXG               = S_IRWXG,
    /** Group read */
    GLOBUS_XIO_FILE_IRGRP               = S_IRGRP,
    /** Group write */
    GLOBUS_XIO_FILE_IWGRP               = S_IWGRP,
    /** Group execute */
    GLOBUS_XIO_FILE_IXGRP               = S_IXGRP
} globus_xio_file_mode_t;

/**
 * File driver seek options
 * @ingroup globus_xio_file_driver_types
 * @hideinitializer
 * @see GLOBUS_XIO_FILE_SEEK
 */
typedef enum
{
    /** set the file pointer at the specified offset */
    GLOBUS_XIO_FILE_SEEK_SET            = SEEK_SET,
    /** set the file pointer at current position + offset */
    GLOBUS_XIO_FILE_SEEK_CUR            = SEEK_CUR,
    /** set the file pointer at size of file + offset */ 
    GLOBUS_XIO_FILE_SEEK_END            = SEEK_END
} globus_xio_file_whence_t;

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_XIO_FILE_DRIVER_H */
