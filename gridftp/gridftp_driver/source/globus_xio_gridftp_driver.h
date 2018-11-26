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

#ifndef GLOBUS_XIO_GRIDFTP_DRIVER_H
#define GLOBUS_XIO_GRIDFTP_DRIVER_H

/**
 * @file globus_xio_gridftp_driver.h
 * @brief XIO GRIDFTP Client Driver
 */

#include "globus_xio_system.h"
#include "globus_common.h"

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus XIO GridFTP Driver
 * @copydoc globus_xio_gridftp_driver 
 */
#endif

/**
 * @defgroup globus_xio_gridftp_driver Globus XIO GRIDFTP Client Driver
 * @brief Globus XIO GRIDFTP Client Driver
 */

/**
 * @addtogroup globus_xio_gridftp_driver
 * @section globus_xio_gridftp_driver_handles Handles
 *
 * An XIO handle with the gridftp client driver can be created with 
 * globus_xio_handle_create() 
 *
 * The GridFTP client driver makes use of globus ftp client library. 
 * globus_xio_register_open() call creates a new ftp client handle (unless 
 * one is set on the attr passed), establishes connection with the 
 * GridFTP server. The contact string must contain the scheme, host name,
 * and the resource, optionally it might contain port and subject also.
 *
 * When the XIO handle is closed, the GridFTP driver will destroy its internal
 * resources and the ftp client handle (unless this handle was set on an attr).
 */

/**
 * @addtogroup globus_xio_gridftp_driver
 * @section globus_xio_gridftp_driver_io Reading/Writing
 *
 * The globus_xio_register_read() enforce that the waitforbytes parameter
 * should be one. When multiple TCP streams are used between the client and the
 * server, data might not be delivered in order. 
 * globus_xio_data_descriptor_cntl() can be used to get the offset of the
 * data. 
 *
 * globus_xio_register_write() does not enforce any restriction on the 
 * waitforbytes parameter. 
 *
 * In any case, when an error or EOF occurs before the waitforbytes request
 * has been met, the outgoing nbytes is set to the amount of data actually
 * read/written before the error or EOF occurred.
 */

/**
 * @addtogroup globus_xio_gridftp_driver
 * 
 * @section globus_xio_gridftp_driver_env Environment Variables
 *
 * - GLOBUS_XIO_GRIDFTP_DEBUG Available if using a debug build. See 
 * globus_debug.h for format. 
 */

/**
 * @addtogroup globus_xio_gridftp_driver
 * @section globus_xio_gridftp_driver_cntls Attributes and Cntls
 *
 * GridFTP driver specific attrs and cntls.
 *
 * @see globus_xio_attr_cntl ()
 *
 * @see globus_xio_handle_cntl ()
 *
 * @see globus_xio_data_descriptor_cntl ()
 */
#define globus_xio_gridftp_attr_cntl globus_xio_attr_cntl
#define globus_xio_gridftp_handle_cntl globus_xio_handle_cntl

/**
 * @addtogroup globus_xio_gridftp_driver
 * @section globus_xio_gridftp_driver_types Types
 *
 * The errors reported by the GRIDFTP driver include GLOBUS_XIO_ERROR_EOF, 
 * GLOBUS_XIO_ERROR_CANCELED, @ref GLOBUS_XIO_GRIDFTP_ERROR_OUTSTANDING_READ, 
 * @ref GLOBUS_XIO_GRIDFTP_ERROR_SEEK, 
 * @ref GLOBUS_XIO_GRIDFTP_ERROR_OUTSTANDING_WRITE,
 * @ref GLOBUS_XIO_GRIDFTP_ERROR_PENDING_READ, 
 * @ref GLOBUS_XIO_GRIDFTP_ERROR_PENDING_WRITE,
 * @ref GLOBUS_XIO_GRIDFTP_ERROR_OUTSTANDING_PARTIAL_XFER
 *
 * @see globus_xio_driver_error_match()
 * @see globus_error_errno_match()
 */

/**
 * GridFTP driver specific error types
 * @ingroup globus_xio_gridftp_driver
 */
typedef enum
{
    /**
     * Indicates that an error occurred in the attribute control
     */
    GLOBUS_XIO_GRIDFTP_ERROR_ATTR,
    /**
     * Indicates that a seek has been called while there is an outstanding I/O
     */
    GLOBUS_XIO_GRIDFTP_ERROR_SEEK,
    /**
     * Indicates that a write has been called while there is an outstanding 
     * read
     */
    GLOBUS_XIO_GRIDFTP_ERROR_OUTSTANDING_READ,
    /**
     * Indicates that a read has been called while there is an outstanding 
     * write
     */
    GLOBUS_XIO_GRIDFTP_ERROR_OUTSTANDING_WRITE,
    /**
     * Indicates that a write has been called while there is a read pending 
     */
    GLOBUS_XIO_GRIDFTP_ERROR_PENDING_READ,
    /**
     * Indicates that a read has been called while there is a write pending 
     */
    GLOBUS_XIO_GRIDFTP_ERROR_PENDING_WRITE,
    /**
     * Indicates that a second partial xfer has been initiated while the first
     * one is still outstanding
     */
    GLOBUS_XIO_GRIDFTP_ERROR_OUTSTANDING_PARTIAL_XFER

} globus_xio_gridftp_error_type_t;

#define globus_xio_gridftp_attr_cntl globus_xio_attr_cntl
#define globus_xio_gridftp_handle_cntl globus_xio_handle_cntl

/** doxygen varargs filter stuff
 * GlobusVarArgDefine(
 *      attr, globus_result_t, globus_xio_gridftp_attr_cntl, attr, driver)
 * GlobusVarArgDefine(
 *      handle, globus_result_t, globus_xio_gridftp_handle_cntl, handle, driver)
 */

/**
 * GRIDFTP driver specific cntls
 * @ingroup globus_xio_gridftp_driver
 */

typedef enum
{
    /*
     * handle cntls
     */         

    /** GlobusVarArgEnum(handle)
     * Reposition the offset of the file being read/written.
     * @ingroup gridftp_driver_cntls
     *
     * @param seek_offset
     *      Specifies the desired offset.
     */
    /* globus_off_t			seek_offset */
    GLOBUS_XIO_GRIDFTP_SEEK,

    /*
     * attr cntls
     */
    /** GlobusVarArgEnum(attr)
     * Set the ftp client handle to use.
     * @ingroup gridftp_driver_cntls
     * Do not create a new ftp client handle, use this handle instead.
     *
     * @param ftp_handle
     *      Specifies the pointer to globus ftp client handle.
     *      Note: this handle will not be destroyed.
     */
    /* globus_ftp_client_handle_t *	ftp_handle */
    GLOBUS_XIO_GRIDFTP_SET_HANDLE,

    /** GlobusVarArgEnum(attr)
     * Get the ftp client handle on the attr.
     * @ingroup gridftp_driver_cntls
     *
     * @param ftp_handle_out
     *      The ftp client handle pointer will be stored here. If none is set,
     *      NULL will be set.
     */
    /* globus_ftp_client_handle_t **	ftp_handle_out */
    GLOBUS_XIO_GRIDFTP_GET_HANDLE,

    /** GlobusVarArgEnum(attr)
     * Enable or disable opening the file in append mode.
     * @ingroup gridftp_driver_cntls
     *
     * @param append
     *      GLOBUS_TRUE to enable, GLOBUS_FALSE to disable (default).
     */
    /* globus_bool_t			append */
    GLOBUS_XIO_GRIDFTP_SET_APPEND,

    /** GlobusVarArgEnum(attr)
     * Get the append flag on the attr.
     * @ingroup gridftp_driver_cntls
     *
     * @param append_out
     *	    The append flag will be stored here.
     */
    /* globus_bool_t *			append_out */
    GLOBUS_XIO_GRIDFTP_GET_APPEND,

    /** GlobusVarArgEnum(attr)
     * Set the ERET algorithm string. This string contains information needed
     * to invoke a server-specific data reduction algorithm on the file being 
     * retrieved.
     * @ingroup gridftp_driver_cntls
     *
     * @param eret_alg_str
     *      Specifies the ERET algorithm string.
     */
    /* const char *			eret_alg_str */
    GLOBUS_XIO_GRIDFTP_SET_ERET,

    /** GlobusVarArgEnum(attr)
     * Get the ERET algorithm string. 
     * @ingroup gridftp_driver_cntls
     *
     * @param eret_alg_str_out
     *      The ERET algorithm string will be stored here. It is the 
     * responsibility of the user to free the memory allocated for this string.
     */
    /* char **				eret_alg_str_out */
    GLOBUS_XIO_GRIDFTP_GET_ERET,

    /** GlobusVarArgEnum(attr)
     * Set the ESTO algorithm string. This string contains information needed
     * to invoke a server-specific data reduction algorithm on the file being 
     * stored.
     * @ingroup gridftp_driver_cntls
     *
     * @param esto_alg_str
     *      Specifies the ESTO algorithm string.
     */
    /* const char *			esto_alg_str */
    GLOBUS_XIO_GRIDFTP_SET_ESTO,

    /** GlobusVarArgEnum(attr)
     * Get the ESTO algorithm string. 
     * @ingroup gridftp_driver_cntls
     *
     * @param esto_alg_str_out
     *      The ESTO algorithm string will be stored here. It is the 
     * responsibility of the user to free the memory allocated for this string.
     */
    /* char **				esto_alg_str_out */
    GLOBUS_XIO_GRIDFTP_GET_ESTO,

    /** GlobusVarArgEnum(attr)
     * Enable or disable partial transfer (associate a transfer with each 
     * read/write) on the GridFTP handle.
     * @ingroup gridftp_driver_cntls
     *
     * @param partial_xfer
     *      GLOBUS_TRUE to enable, GLOBUS_FALSE to disable (default).
     */
    /* globus_bool_t			partial_xfer */
    GLOBUS_XIO_GRIDFTP_SET_PARTIAL_TRANSFER,

    /** GlobusVarArgEnum(attr)
     * Get the partial transfer flag on the attr. 
     * @ingroup gridftp_driver_cntls
     *
     * @param partial_xfer_out
     *      The partial xfer flag will be stored here.
     */
    /* globus_bool_t *			partial_xfer_out */
    GLOBUS_XIO_GRIDFTP_GET_PARTIAL_TRANSFER,

    /** GlobusVarArgEnum(attr)
     * Set the number of TCP streams to be used between the client and the 
     * server.
     * @ingroup gridftp_driver_cntls
     *
     * @param num_streams
     *      Specifies the number of streams to use.
     */
    /* int				num_streams */
    GLOBUS_XIO_GRIDFTP_SET_NUM_STREAMS,

    /** GlobusVarArgEnum(attr)
     * Get the number of TCP streams on the attr.
     * @ingroup gridftp_driver_cntls
     *
     * @param num_streams_out
     *      The stream count will be stored here.
     */
    /* int *				num_streams_out */
    GLOBUS_XIO_GRIDFTP_GET_NUM_STREAMS,

    /** GlobusVarArgEnum(attr)
     * Set the TCP socket send/recv buffer size.
     * @ingroup gridftp_driver_cntls
     *
     * @param buf_size
     *      The send/recv buffer size in bytes to use. (default is system 
     *      specific)
     */
    /* int				buf_size */
    GLOBUS_XIO_GRIDFTP_SET_TCP_BUFFER,

    /** GlobusVarArgEnum(attr)
     * Get the TCP socket send/recv buffer size on the attr.
     * @ingroup gridftp_driver_cntls
     *
     * @param buf_size_out
     *      The send/recv buffer size will be stored here.
     */
    /* int *				buf_size_out */
    GLOBUS_XIO_GRIDFTP_GET_TCP_BUFFER,

    /** GlobusVarArgEnum(attr)
     * Set the transmission mode used for data transfer
     * @ingroup gridftp_driver_cntls
     *
     * @param mode
     *      Specifies the data transmission mode. (default is stream mode)
     * 
     * @see globus_l_xio_gridftp_mode_t
     */
    /* int				mode */
    GLOBUS_XIO_GRIDFTP_SET_MODE,

    /** GlobusVarArgEnum(attr)
     * Get the data transmission mode on the attr.
     * @ingroup gridftp_driver_cntls
     *
     * @param mode_out
     *      The data transmission mode will be stored here.
     *
     * @see globus_l_xio_gridftp_mode_t
     */  
    /* int *				mode_out */
    GLOBUS_XIO_GRIDFTP_GET_MODE,

    /** GlobusVarArgEnum(attr)
     * Set the authentication information used to authenticate with the GridFTP
     * server
     * @ingroup gridftp_driver_cntls
     *
     * @param credential
     *      The credential to use for authenticating with a GridFTP server.
     *      This may be GSS_C_NO_CREDENTIAL to use the default credential.
     * @param user
     *      The user name to send to the GridFTP server. When doing a GSI 
     *      transfer, this may be set to NULL, and the default gridmap entry 
     *      for the user's GSI identity will be used
     * @param password
     *      The password to send to the GridFTP server. When doing a GSI 
     *      transfer, this may be set to NULL. 
     * @param account
     *      The account to use for the data transfer.
     * @param subject
     *      The subject name of the GridFTP server. This is only used when 
     *      doing a GSI transfer, and then only when the security subject 
     *      name does not match the hostname of the server (ie, when the server
     *      is being run by a user). 
     */
    /* gss_cred_id_t			credential,
     * const char *			user,
     * const char *			password,
     * const char *			account,
     * const char *			subject */
    GLOBUS_XIO_GRIDFTP_SET_AUTH,

    /** GlobusVarArgEnum(attr)
     * Get the authentication information on the attr.
     * @ingroup gridftp_driver_cntls
     *
     * @param credential_out
     *      The credential will be stored here.
     * @param user_out
     *      The user name will be stored here.
     * @param password_out
     *      The password will be stored here.
     * @param account_out
     *      The account information will be stored here.
     * @param subject_out
     *      The subject name will be stored here.
     */
    /* gss_cred_id_t *			credential_out,
     * const char **                    user_out,
     * const char **                    password_out,
     * const char **                    account_out,
     * const char **                    subject_out */
    GLOBUS_XIO_GRIDFTP_GET_AUTH,

    /** GlobusVarArgEnum(attr)
     * Set the mode of authentication to be performed on GridFTP data channels.
     * @ingroup gridftp_driver_cntls
     *
     * @param dcau_mode
     *      Specifies the authentication mode.
     *
     * @see globus_l_xio_gridftp_dcau_mode_t
     */
    /* int				dcau_mode */
    GLOBUS_XIO_GRIDFTP_SET_DCAU,

    /** GlobusVarArgEnum(attr)
     * Get the data channel authentication mode on the attr.
     * @ingroup gridftp_driver_cntls
     *
     * @param dcau_mode_out
     *      The data channel authentication mode will be stored here.
     *
     * @see globus_l_xio_gridftp_dcau_mode_t
     */
    /* int *				dcau_mode_out */
    GLOBUS_XIO_GRIDFTP_GET_DCAU,

    /** GlobusVarArgEnum(attr)
     * Set protection level on the data channel.
     * @ingroup gridftp_driver_cntls
     *
     * @param protection
     *      Specifies the protection level.
     *
     * @see globus_l_xio_gridftp_protection_t
     */
    /* int				protection */
    GLOBUS_XIO_GRIDFTP_SET_DATA_PROTECTION,

    /** GlobusVarArgEnum(attr)
     * Get the data channel protection level on the attr.
     * @ingroup gridftp_driver_cntls
     *
     * @param protection_out
     *      The data channel protection level will be stored here.
     *
     * @see globus_l_xio_gridftp_dcau_mode_t
     */
    /* int *				protection_out */
    GLOBUS_XIO_GRIDFTP_GET_DATA_PROTECTION,

    /** GlobusVarArgEnum(attr)
     * Set protection level on the control channel.
     * @ingroup gridftp_driver_cntls
     *
     * @param protection
     *      Specifies the protection level.
     *
     * @see globus_l_xio_gridftp_protection_t
     */
    /* int				protection */
    GLOBUS_XIO_GRIDFTP_SET_CONTROL_PROTECTION,

    /** GlobusVarArgEnum(attr)
     * Get the control channel protection level on the attr.
     * @ingroup gridftp_driver_cntls
     *
     * @param protection_out
     *      The control channel protection level will be stored here.
     *
     * @see globus_l_xio_gridftp_protection_t
     */
    /* int *				protection_out */
    GLOBUS_XIO_GRIDFTP_GET_CONTROL_PROTECTION

} globus_xio_gridftp_cmd_t;     

/**  
 * GRIDFTP driver specific types
 * @ingroup globus_xio_gridftp_driver
 * @hideinitializer
 */  

typedef enum globus_l_xio_gridftp_mode_e
{
    GLOBUS_XIO_GRIDFTP_MODE_NONE,
    GLOBUS_XIO_GRIDFTP_MODE_STREAM = 'S',
    GLOBUS_XIO_GRIDFTP_MODE_BLOCK = 'B',
    GLOBUS_XIO_GRIDFTP_MODE_EXTENDED_BLOCK = 'E',
    GLOBUS_XIO_GRIDFTP_MODE_COMPRESSED = 'C'
} globus_l_xio_gridftp_mode_t;

typedef enum globus_l_xio_gridftp_dcau_mode_e
{
    GLOBUS_XIO_GRIDFTP_DCAU_NONE = 'N',
    GLOBUS_XIO_GRIDFTP_DCAU_SELF = 'A',
    GLOBUS_XIO_GRIDFTP_DCAU_SUBJECT = 'S',
    GLOBUS_XIO_GRIDFTP_DCAU_DEFAULT
} globus_l_xio_gridftp_dcau_mode_t;


typedef enum globus_l_xio_gridftp_protection_e
{
    GLOBUS_XIO_GRIDFTP_PROTECTION_CLEAR = 'C',
    GLOBUS_XIO_GRIDFTP_PROTECTION_SAFE = 'S',
    GLOBUS_XIO_GRIDFTP_PROTECTION_CONFIDENTIAL = 'E',
    GLOBUS_XIO_GRIDFTP_PROTECTION_PRIVATE = 'P'
} globus_l_xio_gridftp_protection_t;


#endif /* GLOBUS_XIO_GRIDFTP_DRIVER_H */
