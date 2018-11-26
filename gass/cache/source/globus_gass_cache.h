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

/**
 * @file globus_gass_cache.h
 * @brief GASS Cache API
 */

#ifndef GLOBUS_GASS_CACHE_H
#define GLOBUS_GASS_CACHE_H

#include "globus_common.h"

#include <sys/param.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus GASS Cache
 * @copydoc globus_gass_cache
 */
#endif
/**
 * @defgroup globus_gass_cache Globus GASS Cache
 * @brief Globus GASS Cache
 */
/*
 * Codes returned by globus_gass_cache module
 */
#define GLOBUS_GASS_CACHE_ADD_NEW                       1
#define GLOBUS_GASS_CACHE_URL_NOT_FOUND                 2
#define GLOBUS_GASS_CACHE_ADD_EXISTS                    3

#define GLOBUS_GASS_CACHE_ERROR_NO_HOME                -1
#define GLOBUS_GASS_CACHE_ERROR_CAN_NOT_CREATE         -2
#define GLOBUS_GASS_CACHE_ERROR_NAME_TOO_LONG          -3
#define GLOBUS_GASS_CACHE_ERROR_LOCK_ERROR             -4
/* not used in a first impl.: */
#define GLOBUS_GASS_CACHE_ERROR_LOCK_TIME_OUT          -5 
#define GLOBUS_GASS_CACHE_ERROR_OPEN_STATE             -6
#define GLOBUS_GASS_CACHE_ERROR_STATE_F_CORRUPT        -7
#define GLOBUS_GASS_CACHE_ERROR_NO_MEMORY              -8
#define GLOBUS_GASS_CACHE_ERROR_CAN_NOT_CREATE_DATA_F  -9
/* only for "done" or delete  :*/
#define GLOBUS_GASS_CACHE_ERROR_URL_NOT_FOUND          -10 
#define GLOBUS_GASS_CACHE_ERROR_CAN_NOT_DEL_LOCK       -11
#define GLOBUS_GASS_CACHE_ERROR_WRONG_TAG              -12
#define GLOBUS_GASS_CACHE_ERROR_ALREADY_DONE           -13
#define GLOBUS_GASS_CACHE_ERROR_CAN_NOT_WRITE          -14
#define GLOBUS_GASS_CACHE_ERROR_CAN_NOT_READ           -15
#define GLOBUS_GASS_CACHE_ERROR_CAN_NOT_DELETE_DATA_F  -16
#define GLOBUS_GASS_CACHE_ERROR_CACHE_NOT_OPENED       -17
#define GLOBUS_GASS_CACHE_ERROR_CACHE_ALREADY_OPENED   -18
#define GLOBUS_GASS_CACHE_ERROR_INVALID_PARRAMETER     -19
#define GLOBUS_GASS_CACHE_ERROR_INVALID_VERSION        -20
#define GLOBUS_GASS_CACHE_ERROR_NO_SPACE	       -21
#define GLOBUS_GASS_CACHE_ERROR_QUOTA_EXCEEDED         -22

#define GLOBUS_GASS_CACHE_TIMESTAMP_UNKNOWN 0UL

/**
 * @brief GASS Cache Handle
 * @ingroup globus_gass_cache
 * @details
 * Data structure used to store information concerning an open cache
 * directory. This structure MUST NOT be modified directly, but passed to
 * the globus_gass_cache functions
 */
typedef struct globus_i_gass_cache_t * globus_gass_cache_t;


extern
int 
globus_gass_cache_open(const char*          cache_directory_path,
		       globus_gass_cache_t*  cache_handle);

extern int
globus_gass_cache_close(globus_gass_cache_t *  cache_handle);

extern
int
globus_gass_cache_add(globus_gass_cache_t    cache_handle,
	       const char	*url,
	       const char	*tag,
	       globus_bool_t	create,
	       unsigned long	*timestamp,
	       char		**local_filename);

extern
int
globus_gass_cache_add_done(
    globus_gass_cache_t  cache_handle,
    const char		*url,
    const char		*tag,
    unsigned long	timestamp);

extern
int
globus_gass_cache_query(
    globus_gass_cache_t		 cache_handle,
    const char			*url,
    const char			*tag,
    globus_bool_t		wait_for_lock,
    unsigned long		*timestamp,
    char			**local_filename,
    globus_bool_t		*is_locked );


extern
int
globus_gass_cache_delete_start(
    globus_gass_cache_t	 cache_handle,
    const char		*url,
    const char		*tag,
    unsigned long	*timestamp);

extern
int
globus_gass_cache_delete(
    globus_gass_cache_t  cache_handle,
    const char		*url,
    const char		*tag,
    unsigned long	timestamp,
    globus_bool_t	is_locked);

extern
int
globus_gass_cache_cleanup_tag(
    globus_gass_cache_t	 cache_handle,
    const char		*url,
    const char		*tag);

extern
int
globus_gass_cache_cleanup_tag_all(
    globus_gass_cache_t  cache_handle,
    char                *tag );

extern
int
globus_gass_cache_mangle_url( const globus_gass_cache_t	cache_handle,
			      const char		*url,
			      char			**mangled_url,
			      int			*length );

extern
int
globus_gass_cache_mangle_tag( const globus_gass_cache_t	cache_handle,
			      const char		*tag,
			      char			**mangled_tag,
			      int			*length );

extern
int
globus_gass_cache_get_dirs( const globus_gass_cache_t	 cache_handle,
			    const char			*url,
			    const char			*tag,
			    char			**global_root,
			    char			**local_root,
			    char			**tmp_root,
			    char			**log_root,
			    char			**global_dir,
			    char			**local_dir );

extern
int
globus_gass_cache_get_cache_dir( const globus_gass_cache_t	 cache_handle,
				 char			**cache_dir );

extern
int
globus_gass_cache_get_cache_type_string( const globus_gass_cache_t	 cache_handle,
					 char			**cache_type );

extern
const char *
globus_gass_cache_error_string(
    int error_code);

/**
 * @brief module_descriptor
 * @ingroup globus_gass_cache
 */
#define GLOBUS_GASS_CACHE_MODULE (&globus_i_gass_cache_module)

extern globus_module_descriptor_t globus_i_gass_cache_module;

#ifdef __cplusplus
}
#endif

#endif   /* GLOBUS_GASS_CACHE_H */
