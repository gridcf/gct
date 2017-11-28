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

#ifndef GLOBUS_GSI_SYSTEM_CONFIG_H
#define GLOBUS_GSI_SYSTEM_CONFIG_H

/**
 * @file globus_gsi_system_config.h
 * @brief Globus GSI System Config Library
 * @author Sam Lang, Sam Meder
 */

#include "globus_common.h"
#include "globus_gsi_system_config_constants.h"
#include "openssl/x509.h"


#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus GSI System Config API
 *
 * @copydoc globus_gsi_sysconfig
 */
#endif

/**
 * @defgroup globus_gsi_sysconfig Globus GSI System Config API
 *
 * This API provides helper functions for detecting installation and
 * environment specific settings applicable to GSI. It also servers as a
 * abstraction layer for OS specific programming details. The public interface
 * for this library consists of the GLOBUS_GSI_SYSCONFIG_* macros that point at
 * the resolve to platform specific functions. <b>You should never use the
 * platform specific functions directly.</b>.
 *
 * Any program that uses Globus GSI System Config functions must include
 * the globus_gsi_system_config.h header.
 *
 */

/**
 * @defgroup globus_gsi_system_config_defines Defines
 * @ingroup globus_gsi_sysconfig
 *
 * These preprocessor defines allow for a platform (ie Win32 vs UNIX)
 * independent API.
 */

/**
 * @defgroup globus_gsi_sysconfig_unix Functions for UNIX platforms
 * @ingroup globus_gsi_sysconfig
 *
 * These functions implement the UNIX version of the Globus GSI System
 * Configuration API. <b>They should never be called directly, please use the
 * provided platform independent defines.</b>
 */

/**
 * @defgroup globus_gsi_sysconfig_win32 Functions for Win32 platforms 
 * @ingroup globus_gsi_sysconfig
 *
 * These functions implement the Win32 version of the Globus GSI System
 * Configuration API. <b>They should never be called directly, please use the
 * provided platform independent defines.</b>
 */

/**
 * @defgroup globus_gsi_sysconfig_shared Functions for all platforms
 * @ingroup globus_gsi_sysconfig
 *
 * These functions are platform independent members of the Globus GSI System
 * Configuration API. 
 *
 */


#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @defgroup globus_i_gsi_system_config Internal Globus Credential
 * System Config API
 *
 */

/**
 * @defgroup globus_i_gsi_system_config_win32 Internal Globus Credential
 * System Config API for Win32 platforms
 */

/**
 * @defgroup globus_i_gsi_system_config_unix Internal Globus Credential
 * System Config API for Unix platforms
 */
#endif

/** 
 * @defgroup globus_gsi_sysconfig_activation Activation
 * @ingroup globus_gsi_sysconfig
 *
 * Globus GSI System Configuration API uses standard Globus module activation
 * and deactivation.  Before any Globus GSI System Configuration API functions
 * are called, the following function must be called:
 *
 * @code
 *      globus_module_activate(GLOBUS_GSI_SYSCONFIG_MODULE)
 * @endcode
 *
 *
 * This function returns GLOBUS_SUCCESS if the Globus GSI System Configuration
 * API was successfully initialized, and you are therefore allowed to
 * subsequently call Globus GSI System Configuration API functions.  Otherwise,
 * an error code is returned, and Globus GSI Credential functions should not be
 * subsequently called. This function may be called multiple times.
 *
 * To deactivate Globus GSI System Configuration API, the following function
 * must be called: 
 *
 * @code
 *    globus_module_deactivate(GLOBUS_GSI_SYSCONFIG_MODULE)
 * @endcode
 *
 * This function should be called once for each time Globus GSI System
 * Configuration API was activated. 
 */

/** Module descriptor
 * @ingroup globus_gsi_sysconfig_activation
 * @hideinitializer
 */
#define GLOBUS_GSI_SYSCONFIG_MODULE    (&globus_i_gsi_sysconfig_module)

extern 
globus_module_descriptor_t              globus_i_gsi_sysconfig_module;

#ifdef WIN32
#    define GLOBUS_GSI_SYSCONFIG_SET_KEY_PERMISSIONS \
            globus_gsi_sysconfig_set_key_permissions_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR \
            globus_gsi_sysconfig_get_home_dir_win32
#    define GLOBUS_GSI_SYSCONFIG_CHECK_KEYFILE \
            globus_gsi_sysconfig_check_keyfile_win32
#    define GLOBUS_GSI_SYSCONFIG_CHECK_KEYFILE_UID \
            globus_gsi_sysconfig_check_keyfile_uid_win32
#    define GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE \
            globus_gsi_sysconfig_check_certfile_win32
#    define GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE_UID \
            globus_gsi_sysconfig_check_certfile_uid_win32
#    define GLOBUS_GSI_SYSCONFIG_FILE_EXISTS \
            globus_gsi_sysconfig_file_exists_win32
#    define GLOBUS_GSI_SYSCONFIG_DIR_EXISTS \
            globus_gsi_sysconfig_dir_exists_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR \
            globus_gsi_sysconfig_get_cert_dir_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_USER_CERT_FILENAME \
            globus_gsi_sysconfig_get_user_cert_filename_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_HOST_CERT_FILENAME \
            globus_gsi_sysconfig_get_host_cert_filename_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_SERVICE_CERT_FILENAME \
            globus_gsi_sysconfig_get_service_cert_filename_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_PROXY_FILENAME \
            globus_gsi_sysconfig_get_proxy_filename_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_SIGNING_POLICY_FILENAME \
            globus_gsi_sysconfig_get_signing_policy_filename_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_CA_CERT_FILES \
            globus_gsi_sysconfig_get_ca_cert_files_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_CURRENT_WORKING_DIR \
            globus_gsi_sysconfig_get_current_working_dir_win32
#    define GLOBUS_GSI_SYSCONFIG_MAKE_ABSOLUTE_PATH_FOR_FILENAME \
            globus_gsi_sysconfig_make_absolute_path_for_filename_win32
#    define GLOBUS_GSI_SYSCONFIG_SPLIT_DIR_AND_FILENAME \
            globus_gsi_sysconfig_split_dir_and_filename_win32
#    define GLOBUS_GSI_SYSCONFIG_REMOVE_ALL_OWNED_FILES \
            globus_gsi_sysconfig_remove_all_owned_files_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_GRIDMAP_FILENAME \
            globus_gsi_sysconfig_get_gridmap_filename_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_AUTHZ_CONF_FILENAME \
            globus_gsi_sysconfig_get_authz_conf_filename_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_GAA_CONF_FILENAME \
            globus_gsi_sysconfig_get_gaa_conf_filename_win32
#    define GLOBUS_GSI_SYSCONFIG_IS_SUPERUSER \
            globus_gsi_sysconfig_is_superuser_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_USER_ID_STRING \
            globus_gsi_sysconfig_get_user_id_string_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_PROC_ID_STRING \
            globus_gsi_sysconfig_get_proc_id_string_win32
#    define GLOBUS_GSI_SYSCONFIG_GET_USERNAME \
            globus_gsi_sysconfig_get_username_win32
#else
/**
 * Set the correct file permissions on a private key.
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_set_key_permissions_unix() and
 * globus_gsi_sysconfig_set_key_permissions_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_SET_KEY_PERMISSIONS \
            globus_gsi_sysconfig_set_key_permissions_unix
/**
 * Get the current users home directory
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_home_dir_unix() and
 * globus_gsi_sysconfig_get_home_dir_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR \
            globus_gsi_sysconfig_get_home_dir_unix
/**
 * Check for the correct file permissions on a private key.
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_check_keyfile_unix() and
 * globus_gsi_sysconfig_check_keyfile_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_CHECK_KEYFILE \
            globus_gsi_sysconfig_check_keyfile_unix
/**
 * Check for the correct file permissions on a private key owned by a
 * particular user id.
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_check_keyfile_unix() and
 * globus_gsi_sysconfig_check_keyfile_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_CHECK_KEYFILE_UID \
            globus_gsi_sysconfig_check_keyfile_uid_unix
/**
 * Check for the correct file permissions on a certificate.
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_check_certfile_unix() and
 * globus_gsi_sysconfig_check_certfile_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE \
            globus_gsi_sysconfig_check_certfile_unix
/**
 * Check for the correct file permissions on a certificate owned by a
 * particular user id.
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_check_certfile_uid_junix() and
 * globus_gsi_sysconfig_check_certfile_uid_jwin32()
 */
#    define GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE_UID \
            globus_gsi_sysconfig_check_certfile_uid_unix
/**
 * Check whether a given file exists
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_file_exists_unix() and
 * globus_gsi_sysconfig_file_exists_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_FILE_EXISTS \
            globus_gsi_sysconfig_file_exists_unix
/**
 * Check whether a given directory exists
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_dir_exists_unix() and
 * globus_gsi_sysconfig_dir_exists_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_DIR_EXISTS \
            globus_gsi_sysconfig_dir_exists_unix
/**
 * Determine the location of the trusted certificates directory
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_cert_dir_unix() and
 * globus_gsi_sysconfig_get_cert_dir_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR \
            globus_gsi_sysconfig_get_cert_dir_unix
/**
 * Determine the location of the users certificate and private key
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_user_cert_filename_unix() and
 * globus_gsi_sysconfig_get_user_cert_filename_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_USER_CERT_FILENAME \
            globus_gsi_sysconfig_get_user_cert_filename_unix
/**
 * Determine the location of the host certificate and private key
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_host_cert_filename_unix() and
 * globus_gsi_sysconfig_get_host_cert_filename_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_HOST_CERT_FILENAME \
            globus_gsi_sysconfig_get_host_cert_filename_unix
/**
 * Determine the location of a service certificate and private key
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_service_cert_filename_unix() and
 * globus_gsi_sysconfig_get_service_cert_filename_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_SERVICE_CERT_FILENAME \
            globus_gsi_sysconfig_get_service_cert_filename_unix
/**
 * Determine the location of a proxy certificate and private key
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_proxy_filename_unix() and
 * globus_gsi_sysconfig_get_proxy_filename_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_PROXY_FILENAME \
            globus_gsi_sysconfig_get_proxy_filename_unix
/**
 * Determine the name of the signing policy file for a given CA
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_signing_policy_filename_unix() and
 * globus_gsi_sysconfig_get_signing_policy_filename_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_SIGNING_POLICY_FILENAME \
            globus_gsi_sysconfig_get_signing_policy_filename_unix
/**
 * Get a list of of trusted CA certificate filenames in a trusted CA
 * certificate directory. 
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_ca_cert_files_unix() and
 * globus_gsi_sysconfig_get_ca_cert_files_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_CA_CERT_FILES \
            globus_gsi_sysconfig_get_ca_cert_files_unix
/**
 * Get the current working directory
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_current_working_dir_unix() and
 * globus_gsi_sysconfig_get_current_working_dir_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_CURRENT_WORKING_DIR \
            globus_gsi_sysconfig_get_current_working_dir_unix
/**
 * Prepend the current working directory to the give filename
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_make_absolute_path_for_filename_unix() and
 * globus_gsi_sysconfig_make_absolute_path_for_filename_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_MAKE_ABSOLUTE_PATH_FOR_FILENAME \
            globus_gsi_sysconfig_make_absolute_path_for_filename_unix
/**
 * Split directory component of path from filename.
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_split_dir_and_filename_unix() and
 * globus_gsi_sysconfig_split_dir_and_filename_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_SPLIT_DIR_AND_FILENAME \
            globus_gsi_sysconfig_split_dir_and_filename_unix
/**
 * Remove all proxies owned by current uid 
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_remove_all_owned_files_unix() and
 * globus_gsi_sysconfig_remove_all_owned_files_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_REMOVE_ALL_OWNED_FILES \
            globus_gsi_sysconfig_remove_all_owned_files_unix
/**
 * Determine the location of the grid map file. 
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_gridmap_filename_unix() and
 * globus_gsi_sysconfig_get_gridmap_filename_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_GRIDMAP_FILENAME \
            globus_gsi_sysconfig_get_gridmap_filename_unix
/**
 * Determine the location of the authorization callout config file. 
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_authz_conf_filename_unix()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_AUTHZ_CONF_FILENAME \
            globus_gsi_sysconfig_get_authz_conf_filename_unix

/**
 * Determine the location of the GAA callout config file. 
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_gaa_conf_filename_unix()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_GAA_CONF_FILENAME \
            globus_gsi_sysconfig_get_gaa_conf_filename_unix
/**
 * Determine whether the current user is the super user
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_is_superuser_unix() and
 * globus_gsi_sysconfig_is_superuser_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_IS_SUPERUSER \
            globus_gsi_sysconfig_is_superuser_unix
/**
 * Get the current UID in string form
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_user_id_string_unix() and
 * globus_gsi_sysconfig_get_user_id_string_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_USER_ID_STRING \
            globus_gsi_sysconfig_get_user_id_string_unix
/**
 * Get the current PID in string form
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_proc_id_string_unix() and
 * globus_gsi_sysconfig_get_proc_id_string_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_PROC_ID_STRING \
            globus_gsi_sysconfig_get_proc_id_string_unix
/**
 * Get the current user name 
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_username_unix() and
 * globus_gsi_sysconfig_get_username_win32()
 */
#    define GLOBUS_GSI_SYSCONFIG_GET_USERNAME \
            globus_gsi_sysconfig_get_username_unix
#endif

/**
 * Generate a unique proxy file name
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_unique_proxy_filename() 
 */
#define     GLOBUS_GSI_SYSCONFIG_GET_UNIQUE_PROXY_FILENAME \
            globus_gsi_sysconfig_get_unique_proxy_filename

/**
 * Determine the location of the vhost credentials directory
 * @ingroup globus_gsi_system_config_defines
 * @hideinitializer
 * See globus_gsi_sysconfig_get_vhost_cred_dir()
 */
#define GLOBUS_GSI_SYSCONFIG_GET_VHOST_CRED_DIR \
        globus_gsi_sysconfig_get_vhost_cred_dir

globus_result_t
GLOBUS_GSI_SYSCONFIG_SET_KEY_PERMISSIONS(
    char *                              filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(
    char **                             home_dir);

globus_result_t
GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
    const char *                        filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_DIR_EXISTS(
    const char *                        filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_CHECK_KEYFILE(
    const char *                        filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_CHECK_KEYFILE_UID(
    const char *                        filename,
    uid_t                               uid);

globus_result_t
GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE(
    const char *                        filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE_UID(
    const char *                        filename,
    uid_t                               uid);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(
    char **                             cert_dir);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_USER_CERT_FILENAME(
    char **                             user_cert_filename,
    char **                             user_key_filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_HOST_CERT_FILENAME(
    char **                             host_cert_filename,
    char **                             host_key_filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_SERVICE_CERT_FILENAME(
    char *                              service_name,
    char **                             service_cert_filename,
    char **                             service_key_filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_PROXY_FILENAME(
    char **                             proxy_filename,
    globus_gsi_proxy_file_type_t        proxy_file_type);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_SIGNING_POLICY_FILENAME(
    X509_NAME *                         ca_name,
    char *                              cert_dir,
    char **                             signing_policy_filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_CA_CERT_FILES(
    char *                              ca_cert_dir,
    globus_fifo_t *                     ca_cert_list);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_CURRENT_WORKING_DIR(
    char **                             working_dir);

globus_result_t
GLOBUS_GSI_SYSCONFIG_MAKE_ABSOLUTE_PATH_FOR_FILENAME(
    char *                              filename,
    char **                             absolute_path);

globus_result_t
GLOBUS_GSI_SYSCONFIG_SPLIT_DIR_AND_FILENAME(
    char *                              full_filename,
    char **                             dir_string,
    char **                             filename_string);

globus_result_t
GLOBUS_GSI_SYSCONFIG_REMOVE_ALL_OWNED_FILES(
    char *                              default_filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_IS_SUPERUSER(
    int *                               is_superuser);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_USER_ID_STRING(
    char **                             user_id_string);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_USERNAME(
    char **                             username);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_PROC_ID_STRING(
    char **                             proc_id_string);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_GRIDMAP_FILENAME(
    char **                             filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_AUTHZ_CONF_FILENAME(
    char **                             filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_UNIQUE_PROXY_FILENAME(
    char **                             unique_filename);

globus_result_t
GLOBUS_GSI_SYSCONFIG_GET_VHOST_CRED_DIR(
    char                              **dir_name_out);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_GSI_SYSTEM_CONFIG_H */
