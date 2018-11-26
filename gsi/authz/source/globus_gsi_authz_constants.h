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

#ifndef GLOBUS_GSI_AUTHZ_CONSTANTS_H
#define GLOBUS_GSI_AUTHZ_CONSTANTS_H

/**
 * @file globus_gsi_authz_constants.h
 * @brief Globus GSI Authorization Library Constants
 */

/**
 * @defgroup globus_gsi_authz_constants GSI Credential Constants
 * @ingroup globus_gsi_authz
 * @brief GSI Credential Constants
 */
/**
 * GSI Authz Error codes
 * @ingroup globus_gsi_authz_constants
 */
typedef enum
{

    /** Success */
    GLOBUS_GSI_AUTHZ_ERROR_SUCCESS = 0,
    /** Error with system call */
    GLOBUS_GSI_AUTHZ_ERROR_ERRNO = 1,
    /** Invalid parameter */
    GLOBUS_GSI_AUTHZ_ERROR_BAD_PARAMETER = 2,
    /** Callout returned an error */
    GLOBUS_GSI_AUTHZ_ERROR_CALLOUT = 3,
    GLOBUS_GSI_AUTHZ_ERROR_LAST = 4
} globus_gsi_authz_error_t;

#endif
