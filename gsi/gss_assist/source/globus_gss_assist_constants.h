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

#ifndef GLOBUS_GSI_GSS_ASSIST_CONSTANTS_H
#define GLOBUS_GSI_GSS_ASSIST_CONSTANTS_H

/**
 * @file globus_gss_assist_constants.h
 * @brief Globus GSI GSS Assist Library
 * @author Sam Lang, Sam Meder
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_gss_assist_constants GSI GSS Assist Constants
 * @ingroup globus_gss_assist
 * @brief Constant Definitions
 */

/** GSI GSS Assist Error codes
 * @ingroup globus_gss_assist_constants
 */
typedef enum
{
    /** Success */
    GLOBUS_GSI_GSS_ASSIST_ERROR_SUCCESS = 0,
    /** No user entry in gridmap file */
    GLOBUS_GSI_GSS_ASSIST_ERROR_WITH_ARGUMENTS = 1,
    /** Error user ID doesn't match */
    GLOBUS_GSI_GSS_ASSIST_ERROR_USER_ID_DOESNT_MATCH = 2,
    /** Error with arguments passed to function */
    GLOBUS_GSI_GSS_ASSIST_ERROR_IN_GRIDMAP_NO_USER_ENTRY = 3,
    /** Error querying gridmap file */
    GLOBUS_GSI_GSS_ASSIST_ERROR_WITH_GRIDMAP = 4,
    /** Invalid gridmap file format */
    GLOBUS_GSI_GSS_ASSIST_ERROR_INVALID_GRIDMAP_FORMAT = 5,
    /** System Error */
    GLOBUS_GSI_GSS_ASSIST_ERROR_ERRNO = 6,
    /** Error during context initialization */
    GLOBUS_GSI_GSS_ASSIST_ERROR_WITH_INIT = 7,
    /** Error during message wrap */
    GLOBUS_GSI_GSS_ASSIST_ERROR_WITH_WRAP = 8,
    /** Error with token */
    GLOBUS_GSI_GSS_ASSIST_ERROR_WITH_TOKEN = 9,
    /** Error exporting context */
    GLOBUS_GSI_GSS_ASSIST_ERROR_EXPORTING_CONTEXT = 10,
    /** Error importing context */
    GLOBUS_GSI_GSS_ASSIST_ERROR_IMPORTING_CONTEXT = 11,
    /** Error initializing callout handle */
    GLOBUS_GSI_GSS_ASSIST_ERROR_INITIALIZING_CALLOUT_HANDLE = 12,
    /** Error reading callout configuration */
    GLOBUS_GSI_GSS_ASSIST_ERROR_WITH_CALLOUT_CONFIG = 13,
    /** Error invoking callout */
    GLOBUS_GSI_GSS_ASSIST_CALLOUT_ERROR = 14,
    /** A GSSAPI returned an error */
    GLOBUS_GSI_GSS_ASSIST_GSSAPI_ERROR = 15,
    /** Gridmap lookup failure */
    GLOBUS_GSI_GSS_ASSIST_GRIDMAP_LOOKUP_FAILED = 16,
    /** Caller provided insufficient buffer space for local identity */
    GLOBUS_GSI_GSS_ASSIST_BUFFER_TOO_SMALL = 17,
    /** Failed to obtain canonical host name */
    GLOBUS_GSI_GSS_ASSIST_ERROR_CANONICALIZING_HOSTNAME = 18,
    GLOBUS_GSI_GSS_ASSIST_ERROR_LAST = 19
} globus_gsi_gss_assist_error_t;

#ifdef __cplusplus
}
#endif

#endif
