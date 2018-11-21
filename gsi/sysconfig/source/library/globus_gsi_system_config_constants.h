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

#ifndef GLOBUS_GSI_SYSTEM_CONFIG_CONSTANTS_H
#define GLOBUS_GSI_SYSTEM_CONFIG_CONSTANTS_H

/**
 * @file globus_gsi_system_config_constants.h
 * @brief Globus GSI System Config Library
 * @author Sam Lang, Sam Meder
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_gsi_sysconfig_datatypes Data Types
 * @ingroup globus_gsi_sysconfig
 * @brief Data Types
 */

/**
 * GSI System Config Error codes
 * @ingroup globus_gsi_sysconfig_datatypes
 */
typedef enum
{
    /** Success - never used */
    GLOBUS_GSI_SYSCONFIG_ERROR_SUCCESS = 0,
    /** Unable to determine trusted certificates directory */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR = 1,
    /** Error while generating certificate filename */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING = 2,
    /** Error while generating private key filename */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING = 3,
    /** Unable to determine user's home directory */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_HOME_DIR = 4,
    /** System Error -- see underlying error for details */
    GLOBUS_GSI_SYSCONFIG_ERROR_ERRNO = 5,
    /** Unable to determine whether file exists */
    GLOBUS_GSI_SYSCONFIG_ERROR_CHECKING_FILE_EXISTS = 6,
    /** Unable to determine the location of the certificate file */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_FILENAME = 7,
    /** Unable to determine the location of the proxy file */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PROXY_FILENAME = 8,
    /** Unable to determine the location of the delegated proxy file */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_DELEG_FILENAME = 9,
    /** Unable to generate a list of CA certificate filenames */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CA_CERT_FILENAMES = 10,
    /** Error while discovering the current working directory */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CWD = 11,
    /** Failed to remove all proxy files */
    GLOBUS_GSI_SYSCONFIG_ERROR_REMOVING_OWNED_FILES = 12,
    /** Unable to determine the location of the grid map file */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GRIDMAP_FILENAME = 13,
    /** Failure while checking whether the current user is the super user */
    GLOBUS_GSI_SYSCONFIG_ERROR_CHECKING_SUPERUSER = 14,
    /** Error while trying to set file permissions */
    GLOBUS_GSI_SYSCONFIG_ERROR_SETTING_PERMS = 15,
    /** Unable to determine the location of a signing policy file */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_SIGNING_POLICY = 16,
    /** Could not find password entry for user */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PW_ENTRY = 17,
    /** Failed to locate the authorization callout configuration file */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_FILENAME = 18,
    /** File is not a regular file */
    GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_REGULAR = 19,
    /** File does not exist */
    GLOBUS_GSI_SYSCONFIG_ERROR_FILE_DOES_NOT_EXIST = 20,
    /** File has incorrect permissions for operation */
    GLOBUS_GSI_SYSCONFIG_ERROR_FILE_BAD_PERMISSIONS = 21,
    /** File is not owned by current user */
    GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_OWNED = 22,
    /** File is a directory */
    GLOBUS_GSI_SYSCONFIG_ERROR_FILE_IS_DIR = 23,
    /** File has zero length */
    GLOBUS_GSI_SYSCONFIG_ERROR_FILE_ZERO_LENGTH = 24,
    /** Invalid argument */
    GLOBUS_GSI_SYSCONFIG_INVALID_ARG = 25,
    /** File has more than one link */
    GLOBUS_GSI_SYSCONFIG_ERROR_FILE_HAS_LINKS = 26,
    /** File has changed in the meantime */
    GLOBUS_GSI_SYSCONFIG_ERROR_FILE_HAS_CHANGED = 27,
    /** Failed to locate the authorization callout library configuration file */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_LIB_FILENAME = 28,
    /** Failed to locate the GAA configuration file */
    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GAA_FILENAME = 29,
    /** File is not a directory */
    GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_DIR = 30,
    /** Last marker - never used */
    GLOBUS_GSI_SYSCONFIG_ERROR_LAST = 31
}
globus_gsi_sysconfig_error_t;

/**
 * Enumerator used to keep track of input/output types of filenames
 * @ingroup globus_gsi_sysconfig_datatypes
 *
 */
typedef enum
{
    /** The proxy filename is intended for reading (it should already exist) */
    GLOBUS_PROXY_FILE_INPUT,
    /** The proxy filename is intended for writing (it does not need to exist) */
    GLOBUS_PROXY_FILE_OUTPUT
}
globus_gsi_proxy_file_type_t;

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_GSI_SYSTEM_CONFIG_CONSTANTS_H */
