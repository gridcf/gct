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

#ifndef GLOBUS_XIO_UTIL_INCLUDE
#define GLOBUS_XIO_UTIL_INCLUDE

#include "globus_xio.h"

#ifdef __cplusplus
extern "C" {
#endif

globus_bool_t
globus_xio_get_env_pair(
    const char *                        env_name,
    int *                               min,
    int *                               max);

globus_bool_t
globus_xio_error_is_eof(
    globus_result_t                     res);

globus_bool_t
globus_xio_error_is_canceled(
    globus_result_t                     res);

globus_bool_t
globus_xio_driver_error_match(
    globus_xio_driver_t                 driver,
    globus_object_t *                   error,
    int                                 type);

globus_bool_t
globus_xio_driver_error_match_with_cb(
    globus_xio_driver_t                 driver,
    globus_object_t *                   error,
    globus_extension_error_match_cb_t   callback,
    void *                              type);

globus_bool_t
globus_xio_error_match(
    globus_result_t                     result,
    int                                 type);
    
void
globus_xio_contact_destroy(
    globus_xio_contact_t *              contact_info);

globus_result_t
globus_xio_contact_parse(
    globus_xio_contact_t *              contact_info,
    const char *                        contact_string);

globus_result_t
globus_xio_contact_info_to_string(
    const globus_xio_contact_t *        contact_info,
    char **                             contact_string);

globus_result_t
globus_xio_contact_info_to_url(
    const globus_xio_contact_t *        contact_info,
    char **                             contact_string);

globus_result_t
globus_xio_contact_info_to_encoded_string(
    const globus_xio_contact_t *        contact_info,
    const globus_xio_contact_t *        encode_chars,
    char **                             contact_string);

globus_result_t
globus_xio_contact_copy(
    globus_xio_contact_t *              dst,
    const globus_xio_contact_t *        src);

/*
 * Utility macros
 */

/* all macros in this file require each function to 'declare' their name with
 * this
 */
#ifdef __GNUC__
#define GlobusXIOName(func) static const char * _xio_name __attribute__((__unused__)) = #func
#else
#define GlobusXIOName(func) static const char * _xio_name = #func
#endif

#define GlobusXIOErrorCanceled()                                            \
    globus_error_put(GlobusXIOErrorObjCanceled())

#define GlobusXIOErrorObjCanceled()                                         \
    globus_error_construct_error(                                           \
        GLOBUS_XIO_MODULE,                                                  \
        GLOBUS_NULL,                                                        \
        GLOBUS_XIO_ERROR_CANCELED,                                          \
        __FILE__,                                                           \
        _xio_name,                                                          \
        __LINE__,							    \
        _XIOSL("Operation was canceled"))                                          
#define GlobusXIOErrorTimeout()                                             \
    globus_error_put(GlobusXIOErrorObjTimeout())                               

#define GlobusXIOErrorObjTimeout()                                          \
    globus_error_construct_error(                                           \
        GLOBUS_XIO_MODULE,                                                  \
        GlobusXIOErrorObjTimeoutOnly(),                                     \
        GLOBUS_XIO_ERROR_CANCELED,                                          \
        __FILE__,                                                           \
        _xio_name,                                                          \
        __LINE__,                                                           \
        _XIOSL("Operation was canceled"))

#define GlobusXIOErrorObjTimeoutOnly()                                      \
    globus_error_construct_error(                                           \
        GLOBUS_XIO_MODULE,                                                  \
        GLOBUS_NULL,                                                        \
        GLOBUS_XIO_ERROR_TIMEOUT,                                           \
        __FILE__,                                                           \
        _xio_name,                                                          \
        __LINE__,                                                           \
        _XIOSL("Operation timed out"))

#define GlobusXIOErrorObjEOF()                                              \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_ERROR_EOF,                                           \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("An end of file occurred"))                                
                                                                            
#define GlobusXIOErrorEOF()                                                 \
    globus_error_put(                                                       \
        GlobusXIOErrorObjEOF())                                             \
                                                                            
#define GlobusXIOErrorInvalidCommand(cmd_number)                            \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_ERROR_COMMAND,                                       \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("An invalid command (%d) was issued"),                   \
            (cmd_number)))                             
                                                                            
#define GlobusXIOErrorContactString(reason)                                 \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_ERROR_CONTACT_STRING,                                \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("Contact string invalid. %s"),                           \
            (reason)))                                 
                                                                            
#define GlobusXIOErrorObjParameter(param_name)                              \
    globus_error_construct_error(                                           \
        GLOBUS_XIO_MODULE,                                                  \
        GLOBUS_NULL,                                                        \
        GLOBUS_XIO_ERROR_PARAMETER,                                         \
        __FILE__,                                                           \
        _xio_name,                                                          \
        __LINE__,                                                           \
        _XIOSL("Bad parameter, %s"),                                        \
        (param_name))

#define GlobusXIOErrorParameter(param_name)                                 \
    globus_error_put(                                                       \
        GlobusXIOErrorObjParameter(param_name))
                                                                            
#define GlobusXIOErrorObjMemory(mem_name)                                   \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_ERROR_MEMORY,                                        \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("Memory allocation failed on %s"),                       \
            (mem_name))                               
                                                                            
#define GlobusXIOErrorMemory(mem_name_obj)                                  \
    globus_error_put(                                                       \
        GlobusXIOErrorObjMemory(mem_name_obj))
                                                                            
#define GlobusXIOErrorObjSystemError(system_func, _errno)                   \
        globus_error_wrap_errno_error(                                      \
            GLOBUS_XIO_MODULE,                                              \
            (_errno),                                                       \
            GLOBUS_XIO_ERROR_SYSTEM_ERROR,                                  \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("System error in %s"),                                   \
            (system_func))                           

#define GlobusXIOErrorSystemError(system_func, _errno)                      \
    globus_error_put(                                                       \
        GlobusXIOErrorObjSystemError(system_func, _errno))

#define GlobusXIOErrorSystemResource(reason)                                \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_ERROR_SYSTEM_RESOURCE,                               \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("System resource error, %s"),                            \
            (reason)))                                 
                                                                            
#define GlobusXIOErrorInvalidStack(reason)                                  \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_ERROR_STACK,                                         \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("Invalid stack, %s"),                                    \
            (reason)))                                 
                                                                            
#define GlobusXIOErrorInvalidDriver(reason)                                 \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_ERROR_DRIVER,                                        \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("Invalid Driver, %s"),                                   \
            (reason)))                                 
                                                                            
#define GlobusXIOErrorPass()                                                \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_ERROR_PASS,                                          \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("Operation passed too far")))                                   
                                                                            
#define GlobusXIOErrorAlreadyRegistered()                                   \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_ERROR_ALREADY_REGISTERED,                            \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("Operation already registered")))                                    
                                                                            
#define GlobusXIOErrorInvalidState(state)                                   \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_ERROR_STATE,                                         \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("Unexpected state, %d"),                                 \
            (state)))                                  
                                                                            
#define GlobusXIOErrorWrapFailed(failed_func, result)                       \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            globus_error_get((result)),                                     \
            GLOBUS_XIO_ERROR_WRAPPED,                                       \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("%s failed."),                                           \
            (failed_func)))

#define GlobusXIOErrorWrapFailedWithMessage(result, format, arg)            \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            globus_error_get((result)),                                     \
            GLOBUS_XIO_ERROR_WRAPPED,                                       \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            (format),                                                       \
            (arg)))

#define GlobusXIOErrorWrapFailedWithMessage2(result, format, arg1, arg2)    \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            globus_error_get((result)),                                     \
            GLOBUS_XIO_ERROR_WRAPPED,                                       \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            (format),                                                       \
            (arg1), (arg2)))

#define GlobusXIOErrorNotRegistered()                                       \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            NULL,                                                           \
            GLOBUS_XIO_ERROR_NOT_REGISTERED,                                \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("Not registered.")))                            

#define GlobusXIOErrorNotActivated()                                        \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            NULL,                                                           \
            GLOBUS_XIO_ERROR_NOT_ACTIVATED,                                 \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("Module not activated.")))
                                                                            
#define GlobusXIOErrorUnloaded()                                            \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            NULL,                                                           \
            GLOBUS_XIO_ERROR_UNLOADED,                                      \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("Driver in handle has been unloaded.")))

#define GlobusXIOErrorParse(arg)                                            \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GLOBUS_XIO_MODULE,                                              \
            NULL,                                                           \
            GLOBUS_XIO_ERROR_PARSE,                                         \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            _XIOSL("An error occurred parsing the string: %s"), arg))


#define GlobusIXIOUtilCopyNIovec(iov, iovc, siov, siovc, bytes)             \
    do                                                                      \
    {                                                                       \
        globus_size_t                   _i;                                 \
        globus_size_t                   _tb = 0;                            \
        globus_size_t                   _bytes;                             \
        const struct iovec *            _siov;                              \
        struct iovec *                  _iov;                               \
        int                             _iovc;                              \
        int                             _siovc;                             \
                                                                            \
        _siov = (siov);                                                     \
        _siovc = (siovc);                                                   \
        _iov = (iov);                                                       \
        _iovc = (iovc);                                                     \
        _bytes = (bytes);                                                   \
                                                                            \
        for(_i = 0; _i < _siovc && _tb < _bytes; _i++)                      \
        {                                                                   \
            if(_tb + _siov[_i].iov_len > _bytes)                            \
            {                                                               \
                _iov[_i].iov_len = _bytes - _tb;                            \
            }                                                               \
            else                                                            \
            {                                                               \
                _iov[_i].iov_len = _siov[_i].iov_len;                       \
            }                                                               \
            _tb += _siov[_i].iov_len;                                       \
            _iov[_i].iov_base = _siov[_i].iov_base;                         \
        }                                                                   \
        _iovc = _i;                                                         \
    } while(0)


                                                                            
#define GlobusIXIOUtilTransferIovec(iov, siov, iovc)                        \
    do                                                                      \
    {                                                                       \
        int                             _i;                                 \
        const struct iovec *            _siov;                              \
        struct iovec *                  _iov;                               \
        int                             _iovc;                              \
                                                                            \
        _siov = (siov);                                                     \
        _iov = (iov);                                                       \
        _iovc = (iovc);                                                     \
                                                                            \
        for(_i = 0; _i < _iovc; _i++)                                       \
        {                                                                   \
            _iov[_i].iov_base = _siov[_i].iov_base;                         \
            _iov[_i].iov_len = _siov[_i].iov_len;                           \
        }                                                                   \
    } while(0)

#define GlobusIXIOUtilAdjustIovec(iov, iovc, nbytes)                        \
    do                                                                      \
    {                                                                       \
        globus_size_t                   _n = (nbytes);                      \
                                                                            \
        if(_n > 0)                                                          \
        {                                                                   \
            struct iovec *              _iov = (iov);                       \
            int                         _iovc = (iovc);                     \
            int                         _i;                                 \
                                                                            \
            /* skip all completely filled iovecs */                         \
            for(_i = 0;                                                     \
                _i < _iovc &&  _n >= _iov[_i].iov_len;                      \
                _n -= _iov[_i].iov_len, _i++);                              \
                                                                            \
            if(_i < _iovc)                                                  \
            {                                                               \
                _iov[_i].iov_base = (char *) _iov[_i].iov_base + _n;        \
                _iov[_i].iov_len -= _n;                                     \
                (iov) += _i;                                                \
            }                                                               \
                                                                            \
            (iovc) -= _i;                                                   \
        }                                                                   \
    } while(0)

#define GlobusIXIOUtilTransferAdjustedIovec(                                \
    new_iov, new_iovc, iov, iovc, nbytes)                                   \
    do                                                                      \
    {                                                                       \
        globus_size_t                   _n;                                 \
        const struct iovec *            _iov;                               \
        int                             _iovc;                              \
        struct iovec *                  _new_iov;                           \
        int                             _i;                                 \
        int                             _j;                                 \
                                                                            \
        _iov = (iov);                                                       \
        _iovc = (iovc);                                                     \
        _new_iov = (new_iov);                                               \
                                                                            \
        /* skip all completely filled iovecs */                             \
        for(_i = 0, _n = (nbytes);                                          \
            _i < _iovc &&  _n >= _iov[_i].iov_len;                          \
            _n -= _iov[_i].iov_len, _i++);                                  \
                                                                            \
        (new_iovc) = _iovc - _i;                                            \
        if(_i < _iovc)                                                      \
        {                                                                   \
            _new_iov[0].iov_base = (char *) _iov[_i].iov_base + _n;         \
            _new_iov[0].iov_len = _iov[_i].iov_len - _n;                    \
                                                                            \
            /* copy remaining */                                            \
            for(_j = 1, _i++; _i < _iovc; _j++, _i++)                       \
            {                                                               \
                _new_iov[_j].iov_base = _iov[_i].iov_base;                  \
                _new_iov[_j].iov_len = _iov[_i].iov_len;                    \
            }                                                               \
        }                                                                   \
    } while(0)

#define GlobusXIOUtilIovTotalLength(                                        \
    out_len, iov, iovc)                                                     \
    do                                                                      \
    {                                                                       \
        int                             _i;                                 \
        const struct iovec *            _iov;                               \
        int                             _iovc;                              \
        globus_size_t                   _out_len;                           \
        _iov = (iov);							    \
        _iovc = (iovc);							    \
        _out_len = 0;                                                       \
        for(_i = 0; _i < _iovc; _i++)                                       \
        {                                                                   \
            _out_len += _iov[_i].iov_len;                                   \
        }                                                                   \
        out_len = _out_len;						    \
    } while(0)

#define GlobusXIOUtilIovSerialize(                                          \
    out_buf, iov, iovc)                                                     \
    do                                                                      \
    {                                                                       \
        int                             _i;                                 \
        int                             _ndx = 0;                           \
        for(_i = 0; _i < iovc; _i++)                                        \
        {                                                                   \
            memcpy(&(out_buf)[_ndx], (iov)[_i].iov_base, (iov)[_i].iov_len);\
            _ndx += (iov)[_i].iov_len;                                      \
        }                                                                   \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif
