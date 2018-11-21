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

#ifndef GLOBUS_GASS_COPY_H
#define GLOBUS_GASS_COPY_H

#include <openssl/evp.h>

/**
 * @file globus_gass_copy.h
 * @brief GASS Copy Library
 */
 
#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
 /**
 * @mainpage GASS Copy API
 * @copydoc globus_gass_copy
 */
#endif

/**
 * @defgroup globus_gass_copy GASS Copy
 * @brief Protocol-Independent File Transfer
 *
 * @details
 * The Globus GASS Copy library is motivated by the desire to provide a
 * uniform interface to transfer files specified by different protocols.
 *
 * The goals in doing this are to:

 *   - Provide a robust way to describe and apply file transfer properties
 * for a variety of protocols. These include the standard HTTP, FTP and
 * GSIFTP options.  Some of the new file transfer capabilities in GSIFTP are
 * parallel, striping, authentication and TCP buffer sizing.
 *   - Provide a service to support nonblocking file transfer and handle
 * asynchronous file and network events.
 *   - Provide a simple and portable way to implement file transfers.
 *
 * Any program that uses Globus GASS Copy functions must include
 * "globus_gass_copy.h".
 *
 */

#include "globus_gass_transfer.h"
#include "globus_ftp_client.h"
#include "globus_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup globus_gass_copy
 *
 * Globus GASS Copy uses standard Globus module activation and deactivation.
 * Before any Globus GASS Copy functions are called, the following function
 * must be called:
 *
 * @code
        globus_module_activate(GLOBUS_GASS_COPY_MODULE)
   @endcode
 *
 * This function returns GLOBUS_SUCCESS if Globus GASS Copy was successfully
 * initialized, and you are therefore allowed to subsequently call
 * Globus GASS Copy functions.  Otherwise, an error code is returned, and
 * Globus GASS Copy functions should not be subsequently called. This
 * function may be called multiple times.
 *
 * To deactivate Globus GASS Copy, the following function must be called:
 *
 * @code
      globus_module_deactivate(GLOBUS_GASS_COPY_MODULE)
   @endcode
 *
 * This function should be called once for each time Globus GASS Copy was
 * activated.
 */

/**
 * @brief Module Descriptor
 * @ingroup globus_gass_copy
 */
#define GLOBUS_GASS_COPY_MODULE (&globus_i_gass_copy_module)

extern
globus_module_descriptor_t        globus_i_gass_copy_module;

#define _GASCSL(s) globus_common_i18n_get_string( \
		     GLOBUS_GASS_COPY_MODULE, \
		     s)

#define CKSM_SIZE (EVP_MAX_MD_SIZE * 2 + 1)

typedef struct globus_gass_copy_state_s globus_gass_copy_state_t;
typedef struct globus_gass_copy_handle_s globus_gass_copy_handle_t;
typedef struct globus_gass_copy_perf_info_s globus_gass_copy_perf_info_t;

/**
 * @brief Performance Callback
 * @ingroup globus_gass_copy
 * @details
 * This callback is registered with 'globus_gass_copy_register_performance_cb'
 * It will be called during a transfer to supply performance information on
 * current transfer.  Its frequency will be at most one per second, but
 * it is possible to receive no callbacks. This is possible in very short
 * transfers and in ftp transfers in which the server does not provide
 * performance information.
 *
 * @param handle
 *        the gass copy handle this transfer is occurring on
 *
 * @param user_arg
 *        a user pointer registered with
 *        'globus_gass_copy_register_performance_cb'
 *
 * @param total_bytes
 *        the total number of bytes transfer so far
 *
 * @param instantaneous_throughput
 *        instantaneous rate of transfer (since last callback or start)
 *        (bytes / sec)
 *
 * @param avg_throughput
 *        the avg thoughput calculated since the start of the transfer
 *        (bytes / sec)
 *
 * @return
 *        - n/a
 */
typedef void (*globus_gass_copy_performance_cb_t)(
    void *                                          user_arg,
    globus_gass_copy_handle_t *                     handle,
    globus_off_t                                    total_bytes,
    float                                           instantaneous_throughput,
    float                                           avg_throughput);

/**
 * @brief Copy Callback
 * @ingroup globus_gass_copy
 * @details
 * Signature of a callback from globus_gass_copy_register_*() functions.
 * (nonblocking transfer functions)
 */
typedef void (*globus_gass_copy_callback_t)(
    void * callback_arg,
    globus_gass_copy_handle_t * handle,
    globus_object_t * error);

/**
 * @brief Status States
 * @ingroup globus_gass_copy
 */
typedef enum
{
    GLOBUS_GASS_COPY_STATUS_NONE,
    GLOBUS_GASS_COPY_STATUS_PENDING,
    GLOBUS_GASS_COPY_STATUS_INITIAL,
    GLOBUS_GASS_COPY_STATUS_SOURCE_READY,
    GLOBUS_GASS_COPY_STATUS_TRANSFER_IN_PROGRESS,
    GLOBUS_GASS_COPY_STATUS_READ_COMPLETE,
    GLOBUS_GASS_COPY_STATUS_WRITE_COMPLETE,
    GLOBUS_GASS_COPY_STATUS_DONE,
    GLOBUS_GASS_COPY_STATUS_FAILURE,
    GLOBUS_GASS_COPY_STATUS_CANCEL,
    GLOBUS_GASS_COPY_STATUS_DONE_SUCCESS,
    GLOBUS_GASS_COPY_STATUS_DONE_FAILURE,
    GLOBUS_GASS_COPY_STATUS_DONE_CANCELLED
} globus_gass_copy_status_t;

/**
 * @brief URL Modes
 * @ingroup globus_gass_copy
 */
typedef enum
{
    GLOBUS_GASS_COPY_URL_MODE_UNSUPPORTED,
    GLOBUS_GASS_COPY_URL_MODE_FTP,
    GLOBUS_GASS_COPY_URL_MODE_GASS,
    GLOBUS_GASS_COPY_URL_MODE_IO
} globus_gass_copy_url_mode_t;

/**
 * @brief Copy Handle
 * @ingroup globus_gass_copy
 */
struct globus_gass_copy_handle_s
{
  /**
   * the status of the current transfer
   */
  globus_gass_copy_status_t             status;

  /**
   * pointer to the state structure which contains internal info related to a
   * transfer
   */
  globus_gass_copy_state_t             *state;

  /**
   * pointer to user data
   */
  void  		               *user_pointer;

  /**
   * Pointer to perf_info structure used to provide the user transfer
   * performance information
   */
  globus_gass_copy_perf_info_t         *performance;

  /**
   * Indicates if the 3rd party transfer is done externally
   * to this library.
   */
  globus_bool_t                         external_third_party;

  /**
   * Pointer to user callback function
   */
  globus_gass_copy_callback_t           user_callback;

  /**
   * pointer to user argument to user callback function
   */
  void                                 *callback_arg;

  /**
   * pointer to user cancel callback function
   */
  globus_gass_copy_callback_t           user_cancel_callback;

  /**
   * pointer to user argument to user cancel callback function
   */
  void                                 *cancel_callback_arg;

  /**
   * Error object to pass to the callback function
   */
  globus_object_t                      *err;

  /**
   * Size of the buffers to be used in the transfers
   */
  int                                   buffer_length;

  /**
   * Indicates whether third_party transfers should be used (for ftp to
   * ftp transfers).  If set to FALSE, the default,
   * globus_ftp_client_third_party_transfer() will be used.  if set to
   * TRUE, gass_copy will manage the transfer
   */
  globus_bool_t                         no_third_party_transfers;

  globus_ftp_client_handle_t	        ftp_handle;
  /**
   * handle only used when no_third_party_transfers is true (for 3pt)
   */
  globus_ftp_client_handle_t	        ftp_handle_2;
  
  /**
   * offsets for partial file transfers
   */
  /*@{*/
  globus_off_t                        partial_offset;
  globus_off_t                        partial_end_offset;
  globus_off_t                        partial_bytes_remaining; 
  /*@}*/
  /**
   * Indicates whether to send ALLO for ftp destinations
   */
  globus_bool_t                       send_allo;

  /**
   * Run a stat check on all urls passed to globus_gass_copy_glob_expand_url
   */
  globus_bool_t                       always_stat_on_expand;
};

/**
 * @brief Attributes
 * @ingroup globus_gass_copy
 * @details
 * Contains any/all attributes that are
 * required to perform the supported transfer methods (ftp, gass, io).
 */
typedef struct globus_gass_copy_attr_s
{
  globus_ftp_client_operationattr_t * ftp_attr;
  globus_io_attr_t * io;
  globus_gass_transfer_requestattr_t * gass_requestattr;
} globus_gass_copy_attr_t;

/**
 * @brief Handle Attributes
 * @ingroup globus_gass_copy
 * @details
 * Contains any/all attributes that are required to create lower-level handles
 * (ftp, gass, io).
 */
typedef struct globus_gass_copy_handleattr_s
{
  globus_ftp_client_handleattr_t *	ftp_attr;
} globus_gass_copy_handleattr_t;

/* initialization and destruction of GASS Copy handle */
globus_result_t
globus_gass_copy_handle_init(
    globus_gass_copy_handle_t * handle,
    globus_gass_copy_handleattr_t * handle_attr);

globus_result_t
globus_gass_copy_handle_destroy(
    globus_gass_copy_handle_t * handle);

globus_result_t
globus_gass_copy_handleattr_init(
    globus_gass_copy_handleattr_t * handle_attr);

globus_result_t
globus_gass_copy_handleattr_destroy(
    globus_gass_copy_handleattr_t * handle_attr);

globus_result_t
globus_gass_copy_handleattr_set_ftp_attr(
    globus_gass_copy_handleattr_t * handle_attr,
    globus_ftp_client_handleattr_t * ftp_attr);

/* set the size of the buffer to be used for the transfers */
globus_result_t
globus_gass_copy_set_buffer_length(
    globus_gass_copy_handle_t * handle,
    int length);

/* get the size of the buffer being used for the transfers */
globus_result_t
globus_gass_copy_get_buffer_length(
    globus_gass_copy_handle_t * handle,
    int * length);

/* sets whether third_party transfers should be used for ftp to
 * ftp transfers */
globus_result_t
globus_gass_copy_set_no_third_party_transfers(
    globus_gass_copy_handle_t * handle,
    globus_bool_t no_third_party_transfers);

/* get the size of the buffer being used for the transfers */
globus_result_t
globus_gass_copy_get_no_third_party_transfers(
    globus_gass_copy_handle_t * handle,
    globus_bool_t * no_third_party_transfers);
    
/* get offsets for partial file transfer */
globus_result_t
globus_gass_copy_get_partial_offsets(
    globus_gass_copy_handle_t * handle,
    globus_off_t * offset,
    globus_off_t * end_offset);
    
/* set offsets for partial file transfer */
globus_result_t
globus_gass_copy_set_partial_offsets(
    globus_gass_copy_handle_t * handle,
    globus_off_t offset,
    globus_off_t end_offset);

/* send ALLO to ftp destinations */
globus_result_t
globus_gass_copy_set_allocate(
    globus_gass_copy_handle_t *         handle,
    globus_bool_t                       send_allo);


/* run a stat check on all urls passed to globus_gass_copy_glob_expand_url 
    FALSE by default 
 */

globus_result_t
globus_gass_copy_set_stat_on_expand(
    globus_gass_copy_handle_t *         handle,
    globus_bool_t                       always_stat);

globus_result_t 
globus_gass_copy_set_checksum_algo(
    globus_gass_copy_handle_t *         handle, 
    char                      *         algo,
    globus_gass_copy_handle_t *         cksm_handle);

globus_result_t 
globus_gass_copy_set_checksum(
    globus_gass_copy_handle_t *         handle, 
    char                      *         cksm);

/* find out what transfer mode will be used for a given url, so that the proper attributes may be passed to one of the copy function */
globus_result_t
globus_gass_copy_get_url_mode(
    char * url,
    globus_gass_copy_url_mode_t * mode);

/* get current ftp client handle -- use with care if modifying the handle */
globus_result_t
globus_gass_copy_get_ftp_handle(
    globus_gass_copy_handle_t *         handle,
    globus_ftp_client_handle_t *        ftp_handle);

/* initialize the attr structure */
globus_result_t
globus_gass_copy_attr_init(
    globus_gass_copy_attr_t * attr);

/* functions for setting attributes for specific protocols */
globus_result_t
globus_gass_copy_attr_set_ftp(
    globus_gass_copy_attr_t * attr,
    globus_ftp_client_operationattr_t * ftp_attr);

globus_result_t
globus_gass_copy_attr_set_io(
    globus_gass_copy_attr_t * attr,
    globus_io_attr_t * io_attr);

globus_result_t
globus_gass_copy_attr_set_gass(
    globus_gass_copy_attr_t * attr,
    globus_gass_transfer_requestattr_t * gass_attr);

/*
 * copy functions (blocking)
 */
globus_result_t
globus_gass_copy_url_to_url(
    globus_gass_copy_handle_t * handle,
    char * source_url,
    globus_gass_copy_attr_t * source_attr,
    char * dest_url,
    globus_gass_copy_attr_t * dest_attr);

globus_result_t
globus_gass_copy_url_to_handle(
    globus_gass_copy_handle_t * handle,
    char * source_url,
    globus_gass_copy_attr_t * source_attr,
    globus_io_handle_t * dest_handle);

globus_result_t
globus_gass_copy_handle_to_url(
    globus_gass_copy_handle_t * handle,
    globus_io_handle_t * source_handle,
    char * dest_url,
    globus_gass_copy_attr_t * dest_attr);

/*
 * copy functions (asyncronous)
 */
globus_result_t
globus_gass_copy_register_url_to_url(
    globus_gass_copy_handle_t * handle,
    char * source_url,
    globus_gass_copy_attr_t * dest_attr,
    char * dest_url,
    globus_gass_copy_attr_t * source_attr,
    globus_gass_copy_callback_t callback_func,
    void * callback_arg);

globus_result_t
globus_gass_copy_register_url_to_handle(
    globus_gass_copy_handle_t * handle,
    char * source_url,
    globus_gass_copy_attr_t * source_attr,
    globus_io_handle_t * dest_handle,
    globus_gass_copy_callback_t callback_func,
    void * callback_arg);

globus_result_t
globus_gass_copy_register_handle_to_url(
    globus_gass_copy_handle_t * handle,
    globus_io_handle_t * source_handle,
    char * dest_url,
    globus_gass_copy_attr_t * dest_attr,
    globus_gass_copy_callback_t callback_func,
    void * callback_arg);

/*
 * get the status code of the current transfer
 */
globus_result_t
globus_gass_copy_get_status(
    globus_gass_copy_handle_t * handle,
    globus_gass_copy_status_t *status);

/*
 * get the status string of the current transfer
 */

const char *
globus_gass_copy_get_status_string(
    globus_gass_copy_handle_t * handle);

/*
 * cancel the current transfer
 */
globus_result_t
globus_gass_copy_cancel(
     globus_gass_copy_handle_t * handle,
     globus_gass_copy_callback_t cancel_callback,
     void * cancel_callback_arg);

/*
 * cache handles functions
 *
 * Use this when transferring mulitple files from or to the same host
 */
globus_result_t
globus_gass_copy_cache_url_state(
    globus_gass_copy_handle_t * handle,
    char * url);

globus_result_t
globus_gass_copy_flush_url_state(
    globus_gass_copy_handle_t * handle,
    char * url);

/*
 *  get/set user pointers from/to GASS Copy handles
 */
globus_result_t
globus_gass_copy_set_user_pointer(
    globus_gass_copy_handle_t * handle,
    void * user_data);

globus_result_t
globus_gass_copy_get_user_pointer(
    globus_gass_copy_handle_t * handle,
    void ** user_data);

globus_result_t
globus_gass_copy_register_performance_cb(
    globus_gass_copy_handle_t *         handle,
    globus_gass_copy_performance_cb_t   callback,
    void *                              user_arg);

/*
 * Set Attribute functions
 */

#ifdef USE_FTP
/* TCP buffer/window size */
globus_result_t
globus_gass_copy_attr_set_tcpbuffer(
    globus_gass_copy_attr_t * attr,
    globus_ftp_control_tcpbuffer_t * tcpbuffer_info);

/* parallel transfer options */
globus_result_t
globus_gass_copy_attr_set_parallelism(
    globus_gass_copy_attr_t * attr,
    globus_ftp_control_parallelism_t * parallelism_info);

/* striping options */
globus_result_t
globus_gass_copy_attr_set_striping(
    globus_gass_copy_attr_t * attr,
    globus_ftp_control_striping_t * striping_info);

/* authorization options */
globus_result_t
globus_gass_copy_attr_set_authorization(
    globus_gass_copy_attr_t * attr,
    globus_io_authorization_t * authorization_info);

/* secure channel options */
globus_result_t
globus_gass_copy_attr_set_secure_channel(
    globus_gass_copy_attr_t * attr,
    globus_io_secure_channel_t * secure_channel_info);
#endif

/**
 * globbed entry types
 * @ingroup globus_gass_copy
 */ 
typedef enum {
    GLOBUS_GASS_COPY_GLOB_ENTRY_UNKNOWN,
    GLOBUS_GASS_COPY_GLOB_ENTRY_FILE,
    GLOBUS_GASS_COPY_GLOB_ENTRY_DIR,
    GLOBUS_GASS_COPY_GLOB_ENTRY_OTHER
} globus_gass_copy_glob_entry_t;

/**
 * @brief Glob expanded entry information
 * @ingroup globus_gass_copy
 */
typedef struct 
{
    /** The file type of the entry
     */
    globus_gass_copy_glob_entry_t       type;
    
    /** A string that uniquely identifies the data that the entry 
     *  refers to.  A file and a symlink to that file will have the 
     *  same unique_id.  
     *  It is NULL for when not available.
     */
    char *                              unique_id;

    /** This points to the full path of the target of a symlink.  
     *  It is NULL for non-symlinks or when not available.
     */
    char *                              symlink_target;
    
    /** An integer specifying the mode of the file.  
     *  It is set to -1 when not available.
     */
    int                                 mode;
     
    /** An integer specifying the modification time of the file.
     *  It is set to -1 when not available.
     */
    int                                 mdtm;
     
    /** A globus_off_t specifying the size of the file.  
     *  It is set to -1 when not available.
     */
    globus_off_t                        size;
} globus_gass_copy_glob_stat_t;

/**
 * @brief Gass copy glob entry callback
 * @ingroup globus_gass_copy
 * @details
 * This callback is passed as a parameter to globus_gass_copy_glob_expand_url().
 * It is called once for each entry that the original expands to.
 * 
 * @param url
 *        The full url to the expanded entry.  A directory entry will end
 *        in a forward slash '/'.
 * 
 * @param stat
 *        A pointer to a globus_gass_copy_glob_stat_t containing information
 *        about the entry. 
 *
 * @param user_arg
 *        The user_arg passed to globus_gass_copy_glob_expand()
 * 
 * @see globus_gass_copy_glob_stat_t,
 *      globus_gass_copy_glob_expand_url
 */
typedef void (*globus_gass_copy_glob_entry_cb_t)(
    const char *                         url,
    const globus_gass_copy_glob_stat_t * info_stat,
    void *                               user_arg);
    
/**
 * @brief Expand globbed url
 * @ingroup globus_gass_copy
 * @details
 * This function expands wildcards in a globbed url, and calls
 * entry_cb() on each one.
 * 
 * @param handle
 *        A gass copy handle to use for the operation.
 * 
 * @param url
 *	  The URL to expand. The URL may be an ftp, gsiftp or file URL.
 *        Wildcard characters supported are '?' '*' '[ ]' in the filename
 *        portion of the url.
 * 
 * @param attr
 *	  Gass copy attributes for this operation.
 * 
 * @param entry_cb
 *        Function to call with information about each entry
 * 
 * @param user_arg
 *        An argument to pass to entry_cb()
 *
 * @return
 *        This function returns an error when any of these conditions are
 *        true:
 *        - handle is GLOBUS_NULL
 *        - url is GLOBUS_NULL
 *        - url cannot be parsed
 *        - url is not a ftp, gsiftp or file url
 */     
globus_result_t 
globus_gass_copy_glob_expand_url( 
     globus_gass_copy_handle_t *        handle, 
     const char *                       url, 
     globus_gass_copy_attr_t *          attr,
     globus_gass_copy_glob_entry_cb_t   entry_cb,
     void *                             user_arg);
     
/**
 * @brief Make directory
 * @ingroup globus_gass_copy
 * @details
 * This function creates a directory given a ftp or file url.
 * 
 * @param handle
 *        A gass copy handle to use for the mkdir operation.
 * @param url
 *	  The URL for the directory to create. The URL may be an ftp,
 *        gsiftp or file URL.
 * @param attr
 *	  Gass copy attributes for this operation.
 *
 * @return
 *        This function returns an error when any of these conditions are
 *        true:
 *        - handle is GLOBUS_NULL
 *        - url is GLOBUS_NULL
 *        - url cannot be parsed
 *        - url is not a ftp, gsiftp or file url
 *        - the directory could not be created
 */     
globus_result_t
globus_gass_copy_mkdir(
    globus_gass_copy_handle_t *         handle,
    char *                              url,
    globus_gass_copy_attr_t *           attr);

globus_result_t
globus_gass_copy_cksm(
    globus_gass_copy_handle_t *         handle,
    char *                              url,
    globus_gass_copy_attr_t *           attr,
    globus_off_t                        offset,
    globus_off_t                        length,
    const char *                        algorithm,
    char *				cksm);

globus_result_t
globus_gass_copy_cksm_async(
    globus_gass_copy_handle_t *         handle,
    char *                              url,
    globus_gass_copy_attr_t *           attr,
    globus_off_t                        offset,
    globus_off_t                        length,
    const char *                        algorithm,
    char *                              cksm,
    globus_gass_copy_callback_t         callback,
    void *                              callback_arg);

globus_result_t
globus_gass_copy_stat(
    globus_gass_copy_handle_t *         handle,
    char *                              url,
    globus_gass_copy_attr_t *           attr,
    globus_gass_copy_glob_stat_t *      stat_info);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_GASS_COPY_H */
