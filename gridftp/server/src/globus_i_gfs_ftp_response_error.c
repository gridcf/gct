/*
 * Copyright 1999-2016 University of Chicago
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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_i_gfs_ftp_response_error.c
 * @brief FTP Response Error Type Implementation
 */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

#include "globus_i_gridftp_server_config.h"
#include "globus_gridftp_server.h"
#include "globus_object.h"
#include "globus_module.h"
#include "globus_error_generic.h"

#include <string.h>

typedef struct globus_l_gfs_ftp_response_error_s
{
    int                                 response_code;
    char *                              response_error_code;
    char *                              message;
}
globus_l_gfs_ftp_response_error_t;

/**
 * Allocate and initialize an error of type GLOBUS_ERROR_TYPE_FTP_RESPONSE
 * @ingroup globus_gridftp_server_error_response
 *
 * @param base_source
 *        Pointer to the originating module.
 * @param base_cause
 *        The error object causing the error. If this is the original
 *        error, this parameter may be NULL.
 * @param response_code
 *        The FTP response code.
 * @param response_error_code
 *        If non-NULL, this is the GlobusError response code and the message
 *        will be formatted as a v=1 message and fmt is optional. If NULL,
 *        the fmt parameter is required to be non-NULL and the error will not
 *        be formatted as a v=1 error.
 * @param fmt
 *        Response string format 
 * @param ...
 *        Response string format arguments
 * @return
 *        The resulting error object. It is the user's responsibility
 *        to eventually free this object using globus_object_free(). A
 *        globus_result_t may be obtained by calling
 *        globus_error_put() on this object.        
 */
globus_object_t *
globus_gfs_ftp_response_error_construct(
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    int                                 response_code,
    const char                         *response_error_code,
    const char                         *fmt,
    ...)
{
    globus_object_t *                   error;
    globus_object_t *                   newerror;
    va_list                             ap;


    va_start(ap, fmt);

    newerror = globus_object_construct(
            GLOBUS_GFS_ERROR_FTP_RESPONSE_TYPE);
    error = globus_gfs_ftp_response_error_v_initialize(
        newerror,
        base_source,
        base_cause,
        response_code,
        response_error_code,
        fmt,
        ap);

    va_end(ap);

    if (error == NULL)
    {
        globus_object_free(newerror);
    }

    return error;
}
/* globus_gfs_ftp_response_error_construct() */

/**
 * Initialize a previously allocated error of type
 * GLOBUS_GRIDFTP_SERVER_ERROR_FTP_RESPONSE_TYPE
 * @ingroup globus_gridftp_server_error_response
 *
 * @param error
 *        The previously allocated error object.
 * @param base_source
 *        Pointer to the originating module.
 * @param base_cause
 *        The error object causing the error. If this is the original
 *        error this parameter may be NULL.
 * @param response_code
 *        The FTP response code.
 * @param response_error_code
 *        If non-NULL, this is the GlobusError response code and the message
 *        will be formatted as a v=1 message and fmt is optional. If NULL,
 *        the fmt parameter is required to be non-NULL and the error will not
 *        be formatted as a v=1 error.
 * @param fmt
 *        Response string format
 * @param ap
 *        Response string format arguments
 * @return
 *        The resulting error object. You may have to call
 *        globus_error_put() on this object before passing it on.
 */
globus_object_t *
globus_gfs_ftp_response_error_initialize(
    globus_object_t *                   error,
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    int                                 response_code,
    const char *                        response_error_code,
    const char *                        fmt,
    ...)
{
    va_list                             ap;

    va_start(ap, fmt);
    error = globus_gfs_ftp_response_error_v_initialize(
            error,
            base_source,
            base_cause,
            response_code,
            response_error_code,
            fmt,
            ap);
    va_end(ap);
    return error;
}
/* globus_ftp_response_error_initialize() */

/**
 * Initialize a previously allocated error of type
 * GLOBUS_GRIDFTP_SERVER_ERROR_FTP_RESPONSE_TYPE
 * @ingroup globus_gridftp_server_error_response
 *
 * @param error
 *        The previously allocated error object.
 * @param base_source
 *        Pointer to the originating module.
 * @param base_cause
 *        The error object causing the error. If this is the original
 *        error this parameter may be NULL.
 * @param response_code
 *        The FTP response code.
 * @param response_error_code
 *        If non-NULL, this is the GlobusError response code and the message
 *        will be formatted as a v=1 message and fmt is optional. If NULL,
 *        the fmt parameter is required to be non-NULL and the error will not
 *        be formatted as a v=1 error.
 * @param fmt
 *        Response string format
 * @param ap
 *        Response string format arguments
 * @return
 *        The resulting error object. You may have to call
 *        globus_error_put() on this object before passing it on.
 */
globus_object_t *
globus_gfs_ftp_response_error_v_initialize(
    globus_object_t *                   error,
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    int                                 response_code,
    const char *                        response_error_code,
    const char *                        fmt,
    va_list                             ap)
{
    globus_l_gfs_ftp_response_error_t * instance_data = NULL;
    globus_object_t *                   this_error = NULL;
    char *                              v1_fmt = NULL;

    if ((this_error = globus_object_upcast(
                error, GLOBUS_GFS_ERROR_FTP_RESPONSE_TYPE)) == NULL)
    {
        return NULL;
    }
    instance_data = malloc(sizeof(globus_l_gfs_ftp_response_error_t));

    if (instance_data == NULL)
    {
        return NULL;
    }

    if (response_error_code != NULL)
    {
        v1_fmt = globus_common_create_string(
                "GlobusError: v=1 c=%s%s%s",
                response_error_code,
                (fmt != NULL) ? "\n" : "",
                (fmt != NULL) ? fmt : "");
    }

    *instance_data = (globus_l_gfs_ftp_response_error_t)
    {
        .response_code = response_code,
        .message = globus_common_v_create_string(
                (v1_fmt != NULL) ? v1_fmt : fmt, ap),
    };

    if (instance_data->message == NULL)
    {
        free(instance_data);
        return NULL;
    }

    if (response_error_code != NULL)
    {
        instance_data->response_error_code = strdup(response_error_code);
        if (instance_data->response_error_code == NULL)
        {
            free(instance_data->message);
            free(instance_data);
            return NULL;
        }
    }

    globus_object_set_local_instance_data(this_error, instance_data);

    return globus_error_initialize_base(error,
                                        base_source,
                                        base_cause);
}
/* globus_ftp_response_error_v_initialize() */

/**
 * Retrieve the response code from a globus_ftp_response_error object
 * @ingroup globus_gridftp_server_error_response
 *
 * @param error
 *        The error from which to retrieve the response code
 * @return
 *        The response code stored in the object
 */
int
globus_gfs_error_get_ftp_response_code(
    globus_object_t *                   error)
{
    globus_l_gfs_ftp_response_error_t * instance_data = NULL;

    error = globus_object_upcast(
            error,
            GLOBUS_GFS_ERROR_FTP_RESPONSE_TYPE);

    if (error != NULL)
    {
        instance_data = globus_object_get_local_instance_data(error);
        return instance_data->response_code;
    }
    else
    {
        return 0;
    }
}
/* globus_gfs_error_get_ftp_response_code() */

/**
 * Retrieve the gridftp response_error_code from a globus_ftp_response_error
 * object
 * @ingroup globus_gridftp_server_error_response
 *
 * @param error
 *        The error from which to retrieve the response code
 * @return
 *        The response error code stored in the object
 */
const char *
globus_gfs_error_get_ftp_response_error_code(
    globus_object_t *                   error)
{
    globus_l_gfs_ftp_response_error_t * instance_data = NULL;

    error = globus_object_upcast(
            error,
            GLOBUS_GFS_ERROR_FTP_RESPONSE_TYPE);

    if (error != NULL)
    {
        instance_data = globus_object_get_local_instance_data(error);
        return instance_data->response_error_code;
    }
    else
    {
        return NULL;
    }
}
/* globus_gfs_error_get_ftp_response_error_code() */

/**
 * Determine if the gridftp response_error_code matches
 * @ingroup globus_gridftp_server_error_response
 *
 * @param error
 *        The error from which to retrieve the response code
 * @param response_error_code
 *        The response error code string to match.
 * @return
 *        The response error code stored in the object
 */
globus_bool_t
globus_gfs_error_match_response_error_code(
    globus_object_t *                   error,
    const char *                        response_error_code)
{
    globus_l_gfs_ftp_response_error_t * instance_data = NULL;

    error = globus_object_upcast(
            error,
            GLOBUS_GFS_ERROR_FTP_RESPONSE_TYPE);

    if (error != NULL && response_error_code != NULL)
    {
        instance_data = globus_object_get_local_instance_data(error);
        if (instance_data->response_error_code != NULL)
        {
            if (strcmp(
                        instance_data->response_error_code,
                        response_error_code) == 0)
            {
                return GLOBUS_TRUE;
            }
        }

    }
    return GLOBUS_FALSE;
}
/* globus_gfs_error_get_ftp_response_error_code() */

/**
 * Copy the instance data of a Globus FTP Response error object.
 * @ingroup globus_gridftp_server_error_response
 * 
 * @param src
 *        The source instance data
 * @param dst
 *        The destination instance data
 * @return
 *        void
 */
static
void
globus_l_gridftp_server_error_copy(
    void *                              src,
    void **                             dst)
{
    globus_l_gfs_ftp_response_error_t * src_instance_data = NULL;
    globus_l_gfs_ftp_response_error_t * dst_instance_data = NULL;

    if(src == NULL || dst == NULL) return;
    src_instance_data = src;
    dst_instance_data = malloc(sizeof(globus_l_gfs_ftp_response_error_t));
    if (dst_instance_data == NULL)
    {
        goto dst_fail;
    }
    dst_instance_data->response_code = src_instance_data->response_code;
    if (src_instance_data->message != NULL)
    {
        dst_instance_data->message = strdup(src_instance_data->message);
        if (dst_instance_data->message == NULL)
        {
            goto dst_msg_fail;
        }
    }
    else
    {
        dst_instance_data->message = NULL;
    }
    if (src_instance_data->response_error_code != NULL)
    {
        dst_instance_data->response_error_code =
                strdup(src_instance_data->response_error_code);
        if (dst_instance_data->response_error_code == NULL)
        {
            free(dst_instance_data->message);
dst_msg_fail:
            free(dst_instance_data);
            dst_instance_data = NULL;
        }
    }
    else
    {
        dst_instance_data->response_error_code = NULL;
    }
dst_fail:
    *dst = dst_instance_data;
    return;
}
/* globus_l_gridftp_server_error_copy() */

/**
 * Free the instance data of a Globus Errno Error object.
 * @ingroup globus_gridftp_server_error_response
 * 
 * @param data
 *        The instance data
 * @return
 *        void
 */
static
void
globus_l_gridftp_server_error_free(
    void *                              data)
{
    globus_l_gfs_ftp_response_error_t * instance_data = data;

    if (instance_data != NULL)
    {
        free(instance_data->message);
        free(instance_data->response_error_code);
        free(instance_data);
    }
}
/* globus_l_gridftp_server_error_free() */

/**
 * Return a copy of the short description from the instance data
 * @ingroup globus_gridftp_server_error_response
 * 
 * @param error
 *        The error object to retrieve the data from.
 * @return
 *        String containing the short description if it exists, NULL
 *        otherwise.
 */
static
char *
globus_l_gridftp_server_error_printable(
    globus_object_t *                   error)
{
    globus_object_t *                   ftp_response_error = NULL;
    char *                              msg = NULL;
    globus_l_gfs_ftp_response_error_t * instance_data = NULL;

    ftp_response_error = globus_object_upcast(
            error,
            GLOBUS_GFS_ERROR_FTP_RESPONSE_TYPE);

    assert (ftp_response_error != NULL);

    instance_data = globus_object_get_local_instance_data(error);
    msg = strdup(instance_data->message);

    return msg;
}/* globus_l_error_errno_printable */

globus_object_t *
globus_i_gfs_error_system(
    /**
     * [in] FTP Response code. If this is 0, a reasonable value (based on
     * system_errno) is used.
     */
    int                                 ftp_code,
    /**
     * [in] Error value. This is the errno set upon failing the system call.
     */
    int                                 system_errno,
    /**
     * [in] Unstructured error context added to the resulting error object.
     * This is a printf-style format string, with conversion values passed
     * as the variable arguments that follow. If this is NULL, then the 
     * strerror() value associated with the errno is used.
     */
    const char *                        fmt,
    ...)
{
    char                                msg[256];
    char                               *m = NULL;
    char                               *n = NULL;
    const char                         *error_code = "INTERNAL_ERROR";
    globus_object_t                    *err = NULL;
    va_list                             ap;

    msg[0] = 0;

    if (fmt != NULL)
    {
        va_start(ap, fmt);
        m = globus_common_v_create_string(fmt, ap);
        va_end(ap);
    }
#ifdef _WIN32
    strerror_s(msg, sizeof(msg), system_errno);
    n = msg;
#elif defined(HAVE_DECL_STRERROR_R)
#ifdef STRERROR_R_CHAR_P
    n = strerror_r(system_errno, msg, sizeof(msg));
#else
    strerror_r(system_errno, msg, sizeof(msg));
    n = msg;
#endif
#else
    n = strerror(system_errno);
#endif

    if (ftp_code == 0)
    {
        switch (system_errno)
        {
#ifdef ETXTBSY
            case ETXTBSY:
                ftp_code = 450;
                break;
#endif
#ifdef ECONNREFUSED
            case ECONNREFUSED:
                ftp_code = 425;
                error_code = "DATA_CHANNEL_COMMUNICATION_FAILURE";
                break;
#endif
#if defined(ECONNRESET)
            case ECONNRESET:
                ftp_code = 426;
                error_code = "DATA_CHANNEL_COMMUNICATION_FAILURE";
                break;
#endif
#if defined(ECONNABORTED)
            case ECONNABORTED:
                ftp_code = 426;
                error_code = "DATA_CHANNEL_COMMUNICATION_FAILURE";
                break;
#endif
            case ENOENT:
                ftp_code = 550;
                error_code = "PATH_NOT_FOUND";
                break;
            case EACCES:
                ftp_code = 550;
                error_code = "PERMISSION_DENIED";
                break;
            case EPERM:
                ftp_code = 550;
                break;
            case ENOTDIR:
                ftp_code = 550;
                error_code = "NOT_A_DIRECTORY";
                break;
            case EISDIR:
                ftp_code = 550;
                error_code = "IS_A_DIRECTORY";
                break;
            case EROFS:
                ftp_code = 550;
                break;
            case ESPIPE:
                ftp_code = 550;
                break;
            case EFBIG:
                ftp_code = 552;
                break;
            case ENOSPC:
                ftp_code = 552;
                error_code = "NO_SPACE_LEFT";
                break;
#if defined(EDQUOT)
            case EDQUOT:
                ftp_code = 552;
                error_code = "QUOTA_EXCEEDED";
                break;
#endif
            case EEXIST:
                ftp_code = 553;
                error_code = "PATH_EXISTS";
                break;
            default:
                ftp_code = 451;
        }
    }

    err = globus_gfs_ftp_response_error_construct(
        NULL,
        NULL,
        ftp_code,
        error_code,
        "GridFTP-Errno: %d%s%s%s%s",
        system_errno,
        (m != NULL) ? "\nGridFTP-Reason: " : "",
        (m != NULL) ? m : "",
        (n != NULL) ? "\nGridFTP-Error-String: ": "",
        (n != NULL) ? n : "");

    if (fmt != NULL)
    {
        free(m);
    }
    return err;
}

/**
 * Error type static initializer.
 */
const globus_object_type_t GLOBUS_GFS_ERROR_FTP_RESPONSE_TYPE_DEFINITION
= globus_error_type_static_initializer (
    GLOBUS_ERROR_TYPE_BASE,
    globus_l_gridftp_server_error_copy,
    globus_l_gridftp_server_error_free,
    globus_l_gridftp_server_error_printable);
