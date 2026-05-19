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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_error_generic.c
 * @brief Globus Error API
 */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

#include "globus_object.h"
#include "globus_module.h"
#include "globus_error_generic.h"
#include "globus_i_error_generic.h"
#include "globus_error.h"
#include "globus_libc.h"
#include "globus_list.h"
#include <string.h>
#include "globus_common.h"

const char *                            _globus_func_name = "";

typedef struct
{
    int                                 type;
    globus_list_t *                     chains;
    char *                              desc;
} globus_l_error_multiple_t;

typedef struct
{
    char *                              desc;
    globus_object_t *                   chain;
} globus_l_error_multiple_chain_t;

/**
 * @name Construct Error
 */
/*@{*/
/**
 * Allocate and initialize an error of type GLOBUS_ERROR_TYPE_GLOBUS
 * @ingroup globus_generic_error_object
 *
 * @param base_source
 *        Pointer to the originating module.
 * @param base_cause
 *        The error object causing the error. If this is the original
 *        error this parameter may be NULL.
 * @param type
 *        The error type. We may reserve part of this namespace for
 *        common errors. Errors not in this space are assumed to be
 *        local to the originating module.
 * @param source_file
 *        Name of file.  Use __FILE__
 * @param source_func
 *        Name of function.  Use _globus_func_name and declare your func with
 *        GlobusFuncName(<name>)
 * @param source_line
 *        Line number.  Use __LINE__
 * @param short_desc_format
 *        Short format string giving a succinct description
 *        of the error. To be passed on to the user.
 * @param ...
 *        Arguments for the format string.
 * @return
 *        The resulting error object. It is the user's responsibility
 *        to eventually free this object using globus_object_free(). A
 *        globus_result_t may be obtained by calling
 *        globus_error_put() on this object.
 */
globus_object_t *
globus_error_construct_error(
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    int                                 type,
    const char *                        source_file,
    const char *                        source_func,
    int                                 source_line,
    const char *                        short_desc_format,
    ...)
{
    globus_object_t *                   error;
    globus_object_t *                   newerror;
    va_list                             ap;

    va_start(ap, short_desc_format);

    newerror = globus_object_construct(GLOBUS_ERROR_TYPE_GLOBUS);
    error = globus_error_initialize_error(
        newerror,
        base_source,
        base_cause,
        type,
        source_file,
        source_func,
        source_line,
        short_desc_format,
        ap);

    va_end(ap);
    if (error == NULL)
    {
        globus_object_free(newerror);
    }

    return error;
}/* globus_error_construct_error() */

/**
 * Allocate and initialize an error of type GLOBUS_ERROR_TYPE_GLOBUS
 * @ingroup globus_generic_error_object
 *
 * @param base_source
 *        Pointer to the originating module.
 * @param base_cause
 *        The error object causing the error. If this is the original
 *        error this parameter may be NULL.
 * @param type
 *        The error type. We may reserve part of this namespace for
 *        common errors. Errors not in this space are assumed to be
 *        local to the originating module.
 * @param source_file
 *        Name of file.  Use __FILE__
 * @param source_func
 *        Name of function.  Use _globus_func_name and declare your func with
 *        GlobusFuncName(<name>)
 * @param source_line
 *        Line number.  Use __LINE__
 * @param short_desc_format
 *        Short format string giving a succinct description
 *        of the error. To be passed on to the user.
 * @param ap
 *        Arguments for the format string.
 * @return
 *        The resulting error object. It is the user's responsibility
 *        to eventually free this object using globus_object_free(). A
 *        globus_result_t may be obtained by calling
 *        globus_error_put() on this object.
 */
globus_object_t *
globus_error_v_construct_error(
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    const int                           type,
    const char *                        source_file,
    const char *                        source_func,
    int                                 source_line,
    const char *                        short_desc_format,
    va_list                             ap)
{
    globus_object_t *                   error;
    globus_object_t *                   newerror;

    newerror = globus_object_construct(GLOBUS_ERROR_TYPE_GLOBUS);
    error = globus_error_initialize_error(
        newerror,
        base_source,
        base_cause,
        type,
        source_file,
        source_func,
        source_line,
        short_desc_format,
        ap);

    if (error == NULL)
    {
        globus_object_free(newerror);
    }

    return error;
}/* globus_error_v_construct_error() */
/*@}*/

/**
 * @name Initialize Error
 */
/*@{*/
/**
 * Initialize a previously allocated error of type
 * GLOBUS_ERROR_TYPE_GLOBUS
 * @ingroup globus_generic_error_object
 *
 * @param error
 *        The previously allocated error object.
 * @param base_source
 *        Pointer to the originating module.
 * @param base_cause
 *        The error object causing the error. If this is the original
 *        error this parameter may be NULL.
 * @param type
 *        The error type. We may reserve part of this namespace for
 *        common errors. Errors not in this space are assumed to be
 *        local to the originating module.
 * @param source_file
 *        Name of file.  Use __FILE__
 * @param source_func
 *        Name of function.  Use _globus_func_name and declare your func with
 *        GlobusFuncName(<name>)
 * @param source_line
 *        Line number.  Use __LINE__
 * @param short_desc_format
 *        Short format string giving a succinct description
 *        of the error. To be passed on to the user.
 * @param ap
 *        Arguments for the format string.
 * @return
 *        The resulting error object. You may have to call
 *        globus_error_put() on this object before passing it on.
 */
globus_object_t *
globus_error_initialize_error(
    globus_object_t *                   error,
    globus_module_descriptor_t *        base_source,
    globus_object_t *                   base_cause,
    int                                 type,
    const char *                        source_file,
    const char *                        source_func,
    int                                 source_line,
    const char *                        short_desc_format,
    va_list                             ap)
{
    globus_l_error_data_t *             instance_data;
    int                                 size;
    va_list                             ap_copy;

    if ((error = globus_object_upcast(error, GLOBUS_ERROR_TYPE_GLOBUS)) == NULL)
    {
        return NULL;
    }

    instance_data = (globus_l_error_data_t *)
        malloc(sizeof(globus_l_error_data_t));

    if(instance_data == NULL)
    {
        return NULL;
    }
    memset((void *) instance_data,0,sizeof(globus_l_error_data_t));

    instance_data->type = type;
    instance_data->file = source_file;
    instance_data->func = source_func;
    instance_data->line = source_line;

    if(short_desc_format != NULL)
    {
        globus_libc_va_copy(ap_copy,ap);
        size = globus_libc_vprintf_length(short_desc_format,ap_copy);
        va_end(ap_copy);

        size++;

        if ((instance_data->short_desc = malloc (size)) == NULL)
        {
            return NULL;
        }

        globus_libc_vsnprintf(instance_data->short_desc,
                              size,
                              short_desc_format,
                              ap);
    }

    globus_object_set_local_instance_data(error, instance_data);

    return globus_error_initialize_base(error,
                                        base_source,
                                        base_cause);
}/* globus_error_initialize_error() */
/*@}*/

/**
 * @name Get Source
 */
/*@{*/
/**
 * Retrieve the originating module descriptor from a error object.
 * @ingroup globus_generic_error_accessor
 *
 * @param error
 *        The error from which to retrieve the module descriptor
 * @return
 *        The originating module descriptor.
 */
globus_module_descriptor_t *
globus_error_get_source(
    globus_object_t *                   error)
{
    return globus_error_base_get_source(error);
}/* globus_error_get_source */
/*@}*/

/**
 * @name Set Source
 */
/*@{*/
/**
 * Set the originating module descriptor in a error object.
 * @ingroup globus_generic_error_accessor
 *
 * @param error
 *        The error object for which to set the causative error
 * @param source_module
 *        The originating module descriptor
 * @return
 *        void
 */
void
globus_error_set_source(
    globus_object_t *                   error,
    globus_module_descriptor_t *        source_module)
{
    globus_error_base_set_source(error,source_module);
}/* globus_error_set_source */
/*@}*/

/**
 * @name Get Cause
 */
/*@{*/
/**
 * Retrieve the underlying error from a error object.
 * @ingroup globus_generic_error_accessor
 *
 * @param error
 *        The error from which to retrieve the causative error.
 * @return
 *        The underlying error object if it exists, NULL if it
 *        doesn't.
 */
globus_object_t *
globus_error_get_cause (
    globus_object_t *                   error)
{
    return globus_error_base_get_cause(error);
}/* globus_error_get_cause */
/*@}*/


/**
 * @name Set Cause
 */
/*@{*/
/**
 * Set the causative error in a error object.
 * @ingroup globus_generic_error_accessor
 *
 * @param error
 *        The error object for which to set the causative error.
 * @param causal_error
 *        The causative error.
 * @return
 *        void
 */
void
globus_error_set_cause (
    globus_object_t *                   error,
    globus_object_t *                   causal_error)
{
    globus_error_base_set_cause(error,causal_error);
}/* globus_error_set_cause */
/*@}*/


/**
 * @name Get Type
 */
/*@{*/
/**
 * Retrieve the error type from a generic globus error object.
 * @ingroup globus_generic_error_accessor
 *
 * @param error
 *        The error from which to retrieve the error type
 * @return
 *        The error type of the object
 */
int
globus_error_get_type(
    globus_object_t *                   error)
{
    globus_object_t *                   tmp;

    if ((tmp = globus_object_upcast(error, GLOBUS_ERROR_TYPE_GLOBUS)) != NULL)
    {
        return ((globus_l_error_data_t *)
                globus_object_get_local_instance_data(tmp))->type;
    }
    else if ((tmp = globus_object_upcast(error, GLOBUS_ERROR_TYPE_MULTIPLE)) != NULL)
    {
        return ((globus_l_error_multiple_t *)
                globus_object_get_local_instance_data(tmp))->type;
    }
    else
    {
        return 0;
    }
}/* globus_error_get_type */
/*@}*/

/**
 * @name Set Type
 */
/*@{*/
/**
 * Set the error type in a generic globus error object.
 * @ingroup globus_generic_error_accessor
 *
 * @param error
 *        The error object for which to set the error type
 * @param type
 *        The error type
 * @return
 *        void
 */
void
globus_error_set_type(
    globus_object_t *                   error,
    const int                           type)
{
    globus_object_t *                   tmp;

    if ((tmp = globus_object_upcast(error, GLOBUS_ERROR_TYPE_GLOBUS)) != NULL)
    {
        ((globus_l_error_data_t *)
                globus_object_get_local_instance_data(tmp))->type = type;
    }
}/* globus_error_set_type */
/*@}*/


/**
 * @name Get Short Description
 */
/*@{*/
/**
 * Retrieve the short error description from a generic globus error
 * object.
 * @ingroup globus_generic_error_accessor
 *
 * @param error
 *        The error from which to retrieve the description
 * @return
 *        The short error description of the object
 */
char *
globus_error_get_short_desc(
    globus_object_t *                   error)
{
    if ((error = globus_object_upcast(error, GLOBUS_ERROR_TYPE_GLOBUS)) != NULL)
    {
        return globus_libc_strdup(
            ((globus_l_error_data_t *)
             globus_object_get_local_instance_data(error))->short_desc);
    }
    else
    {
        return NULL;
    }
}/* globus_error_get_short_desc */
/*@}*/

/**
 * @name Set Short Description
 */
/*@{*/
/**
 * Set the short error description in a generic globus error object.
 * @ingroup globus_generic_error_accessor
 *
 * @param error
 *        The error object for which to set the description
 * @param short_desc_format
 *        Short format string giving a succinct description
 *        of the error. To be passed on to the user.
 * @param ...
 *        Arguments for the format string.
 * @return
 *        void
 */
void
globus_error_set_short_desc(
    globus_object_t *                   error,
    const char *                        short_desc_format,
    ...)
{
    char **                             instance_short_desc;
    va_list                             ap;
    int                                 size;

    if ((error = globus_object_upcast(error, GLOBUS_ERROR_TYPE_GLOBUS)) == NULL)
    {
        return;
    }
    instance_short_desc =
        &((globus_l_error_data_t *)
          globus_object_get_local_instance_data(error))->short_desc;

    if(*instance_short_desc != NULL)
    {
        globus_libc_free(*instance_short_desc);
    }

    *instance_short_desc = NULL;

    va_start(ap, short_desc_format);

    size = globus_libc_vprintf_length(short_desc_format,ap);

    va_end(ap);

    size++;

    if ((*instance_short_desc = malloc (size)) == NULL)
    {
        return;
    }

    va_start(ap, short_desc_format);

    globus_libc_vsnprintf(*instance_short_desc,
                          size,
                          short_desc_format,
                          ap);

    va_end(ap);

    return;
}/* globus_error_set_short_desc */
/*@}*/


/**
 * @name Get Long Description
 */
/*@{*/
/**
 * Retrieve the long error description from a generic globus error
 * object.
 * @ingroup globus_generic_error_accessor
 *
 * @param error
 *        The error from which to retrieve the description
 * @return
 *        The long error description of the object
 */
char *
globus_error_get_long_desc(
    globus_object_t *                   error)
{
    if ((error = globus_object_upcast(error, GLOBUS_ERROR_TYPE_GLOBUS)) == NULL)
    {
        return NULL;
    }
    return globus_libc_strdup(
        ((globus_l_error_data_t *)
         globus_object_get_local_instance_data(error))->long_desc);
}/* globus_error_get_long_desc */
/*@}*/

/**
 * @name Set Long Description
 */
/*@{*/
/**
 * Set the long error description in a generic globus error object.
 * @ingroup globus_generic_error_accessor
 *
 * @param error
 *        The error object for which to set the description
 * @param long_desc_format
 *        Longer format string giving a more detailed explanation of
 *        the error.
 * @return
 *        void
 */
void
globus_error_set_long_desc(
    globus_object_t *                   error,
    const char *                        long_desc_format,
    ...)
{
    char **                             instance_long_desc;
    va_list                             ap;
    int                                 size;

    if ((error = globus_object_upcast(error, GLOBUS_ERROR_TYPE_GLOBUS)) == NULL)
    {
        return;
    }
    instance_long_desc =
        &((globus_l_error_data_t *)
          globus_object_get_local_instance_data(error))->long_desc;

    if(*instance_long_desc != NULL)
    {
        globus_libc_free(*instance_long_desc);
    }

    *instance_long_desc = NULL;

    va_start(ap, long_desc_format);

    size = globus_libc_vprintf_length(long_desc_format,ap);

    va_end(ap);

    size++;

    if ((*instance_long_desc = malloc (size)) == NULL)
    {
        return;
    }

    va_start(ap, long_desc_format);

    globus_libc_vsnprintf(*instance_long_desc,
                          size,
                          long_desc_format,
                          ap);
    va_end(ap);

    return;
}/* globus_error_set_long_desc */
/*@}*/


/**
 * @name Error Match
 */
/*@{*/
/**
 * Check whether the error originated from a specific module and is of
 * a specific type.
 * @ingroup globus_generic_error_utility
 *
 * This function checks whether the error or any of it's causative
 * errors originated from a specific module and is of a specific
 * type. If the module descriptor is left unspecified this function
 * will check for any error of the specified type and vice versa.
 *
 * @param error
 *        The error object for which to perform the check
 * @param module
 *        The module descriptor to check for
 * @param type
 *        The type to check for
 * @return
 *        GLOBUS_TRUE - the error matched the module and type
 *        GLOBUS_FALSE - the error failed to match the module and type
 */
globus_bool_t
globus_error_match(
    globus_object_t *                   error,
    globus_module_descriptor_t *        module,
    int                                 type)
{
    globus_module_descriptor_t *        source_module;
    int                                 error_type;
    globus_object_t *                   tmp;

    if(error == NULL)
    {
        return GLOBUS_FALSE;
    }

    if ((tmp = globus_object_upcast(error, GLOBUS_ERROR_TYPE_GLOBUS)) != NULL)
    {
        error = tmp;
    }
    else if ((tmp = globus_object_upcast(error, GLOBUS_ERROR_TYPE_MULTIPLE)) != NULL)
    {
        error = tmp;
    }
    else
    {
        /* not our type, skip it */
        return globus_error_match(
            globus_error_get_cause(error),
            module,
            type);
    }
    source_module = globus_error_get_source(error);
    error_type = globus_error_get_type(error);

    if(source_module == module && error_type == type)
    {
        return GLOBUS_TRUE;
    }
    else
    {
        return globus_error_match(
            globus_error_get_cause(error),
            module,
            type);
    }
}/* globus_error_match */
/*@}*/


/**
 * @name Print Error Chain
 */
/*@{*/
/**
 * Return a string containing all printable errors found in a error
 * object and it's causative error chain.
 * @ingroup globus_generic_error_utility
 *
 * If the GLOBUS_ERROR_VERBOSE env is set, file, line and function info will
 * also be printed (where available).  Otherwise, only the module name will
 * be printed.
 *
 * @param error
 *        The error to print
 * @return
 *        A string containing all printable errors. This string needs
 *        to be freed by the user of this function.
 */
char *
globus_error_print_chain(
    globus_object_t *                   error)
{
    char *                              error_string;
    char *                              tmp;
    int                                 length = 0;
    globus_object_t *                   current_error;

    current_error = error;
    error_string = (char *) globus_libc_malloc(1);

    do
    {
        tmp = globus_object_printable_to_string(current_error);
        if(tmp != NULL)
        {
            int                         l;

            l = strlen(tmp);
            if(l)
            {
                char *                  ns;

                ns = (char *) globus_libc_realloc(
                    error_string, length + l + 2);
                if(ns)
                {
                    error_string = ns;
                    memcpy(error_string + length, tmp, l);
                    length += l;
                    error_string[length++] = '\n';
                }
            }
            globus_libc_free(tmp);
        }
    }
    while((current_error = globus_error_get_cause(current_error)));

    error_string[length] = '\0';

    if(length == 0)
    {
        globus_libc_free(error_string);
        error_string = NULL;
    }

    return error_string;
}/* globus_error_print_chain */
/*@}*/

static
char *
globus_l_error_multiple_print(
    globus_object_t *                   error,
    globus_bool_t                       friendly)
{
    globus_l_error_multiple_t *         data;
    int                                 count = 0;
    char **                             layout = NULL;
    char **                             free_ptrs = NULL;
    char *                              error_string;
    int                                 i = 0;

    if ((error = globus_object_upcast(error, GLOBUS_ERROR_TYPE_MULTIPLE)) == NULL)
    {
        return NULL;
    }
    data = (globus_l_error_multiple_t *)
        globus_object_get_local_instance_data(error);
    if(data && data->chains)
    {
        count = globus_list_size(data->chains);
        layout = (char **) globus_malloc(sizeof(char *) * (3 + (4 * count)));
        free_ptrs = (char **) globus_malloc(sizeof(char *) * count);
        if(layout && free_ptrs)
        {
            globus_list_t *             tmp;
            int                         j = 0;

            if(data->desc && *data->desc)
            {
                layout[i++] = data->desc;
                layout[i++] = "\n";
            }

            for(tmp = data->chains;
                !globus_list_empty(tmp);
                tmp = globus_list_rest(tmp))
            {
                globus_l_error_multiple_chain_t * instance;

                instance = (globus_l_error_multiple_chain_t *)
                    globus_list_first(tmp);

                if(instance->desc && *instance->desc)
                {
                    layout[i++] = instance->desc;
                    layout[i++] = "\n";
                }

                if(friendly)
                {
                    free_ptrs[j++] = layout[i++] =
                        globus_error_print_friendly(instance->chain);
                }
                else
                {
                    free_ptrs[j++] = layout[i++] =
                        globus_error_print_chain(instance->chain);
                }
            }
        }
    }

    error_string = layout ? globus_libc_join((const char **) layout, i) : NULL;
    if(layout)
    {
        globus_free(layout);
    }
    if(free_ptrs)
    {
        for(i = 0; i < count; i++)
        {
            if(free_ptrs[i])
            {
                globus_free(free_ptrs[i]);
            }
        }
        globus_free(free_ptrs);
    }

    return error_string;
}

static
char *
globus_l_add_error_string(
    char **                             layout,
    int *                               idx,
    globus_object_t *                   error)
{
    char *                              msg;

    if(globus_object_get_type(error) == GLOBUS_ERROR_TYPE_MULTIPLE)
    {
        msg = globus_l_error_multiple_print(error, GLOBUS_TRUE);
        if(msg && *msg)
        {
            layout[(*idx)++] = msg;
        }
    }
    else
    {
         msg = globus_object_printable_to_string(error);
         if(msg && *msg)
         {
             layout[(*idx)++] = msg;
             layout[(*idx)++] = "\n";
         }
    }

    return msg;
}

/**
 * @name Print User Friendly Error Message
 */
/*@{*/
/**
 * Return a string containing error messages from the top 1 and bottom 3
 * objects, and, if found, show a friendly error message.  The error chain will
 * be searched from top to bottom until a friendly handler is found and a
 * friendly message is created.
 * @ingroup globus_generic_error_utility
 *
 * If the GLOBUS_ERROR_VERBOSE env is set, then the result from
 * globus_error_print_chain() will be used.
 *
 * @param error
 *        The error to print
 * @return
 *        A string containing a friendly error message. This string needs
 *        to be freed by the user of this function.
 */
char *
globus_error_print_friendly(
    globus_object_t *                   error)
{
    char *                              error_string;
    globus_object_t *                   current_error;
    globus_module_descriptor_t *        module;
    char *                              layout[16];
    int                                 i = 0;
    char *                              friendly = NULL;
    char *                              top = NULL;
    char *                              bottom1 = NULL;
    char *                              bottom2 = NULL;
    char *                              bottom3 = NULL;
    char *                              verbose = NULL;
    globus_bool_t                       verbose_allowed = GLOBUS_TRUE;

    if(!error)
    {
        return NULL;
    }

    if(globus_i_error_verbose)
    {
        int *                           in_progress;

        in_progress = (int *)
            globus_thread_getspecific(globus_i_error_verbose_key);
        if(in_progress)
        {
            verbose_allowed = GLOBUS_FALSE;
        }
        else
        {
            globus_thread_setspecific(
                globus_i_error_verbose_key, (int *) 0x01);
        }
    }

    if(globus_i_error_verbose && verbose_allowed)
    {
        verbose = globus_error_print_chain(error);
        if(verbose)
        {
            layout[i++] = verbose;
        }

        globus_thread_setspecific(globus_i_error_verbose_key, NULL);
    }
    else
    {
        globus_object_t *               source_error1 = NULL;
        globus_object_t *               source_error2 = NULL;
        globus_object_t *               source_error3;

        /* here we only take the top error and the bottom 3 */
        current_error = error;
        do
        {
            source_error3 = source_error2;
            source_error2 = source_error1;
            source_error1 = current_error;
            module = globus_error_get_source(current_error);
            if(friendly == NULL && module && module->friendly_error_func)
            {
                friendly = module->friendly_error_func(
                    current_error,
                    globus_object_get_type(current_error));
            }
        } while((current_error = globus_error_get_cause(current_error)));

        top = globus_l_add_error_string(layout, &i, error);

        if(error != source_error1)
        {
            if(error != source_error2)
            {
                if(error != source_error3)
                {
                    bottom3 = globus_l_add_error_string(
                        layout, &i, source_error3);
                }

                bottom2 = globus_l_add_error_string(layout, &i, source_error2);
            }

            bottom1 = globus_l_add_error_string(layout, &i, source_error1);
        }

        if(friendly && *friendly)
        {
            layout[i++] = friendly;
            layout[i++] = "\n";
        }
    }

    error_string = globus_libc_join((const char **) layout, i);

    if(top)
    {
        globus_free(top);
    }
    if(bottom3)
    {
        globus_free(bottom3);
    }
    if(bottom2)
    {
        globus_free(bottom2);
    }
    if(bottom1)
    {
        globus_free(bottom1);
    }
    if(friendly)
    {
        globus_free(friendly);
    }
    if(verbose)
    {
        globus_free(verbose);
    }

    return error_string;
}/* globus_error_print_friendly */
/*@}*/


/***************************************************************************/
/**
 * Multiple error type stuff
 */

/**
 * Construct a container object for multiple error chains.  Useful when
 * an application tries many things (and each fails) before finally
 * giving up;
 *
 * Use globus_error_mutliple_add_chain() to add error objects/chains to this
 * object.
 *
 * @param base_source
 *        Pointer to the originating module.
 * @param type
 *        The error type. We may reserve part of this namespace for
 *        common errors. Errors not in this space are assumed to be
 *        local to the originating module.
 *        globus_error_match() will match against this type, but not of
 *        the contained chains.
 * @param fmt
 *        a printf style format string describing the multiple errors
 * @return
 *        The resulting error object. It is the user's responsibility
 *        to eventually free this object using globus_object_free(). A
 *        globus_result_t may be obtained by calling
 *        globus_error_put() on this object.
 */
globus_object_t *
globus_error_construct_multiple(
    globus_module_descriptor_t *        base_source,
    int                                 type,
    const char *                        fmt,
    ...)
{
    globus_object_t *                   newerror;
    globus_object_t *                   error;
    globus_l_error_multiple_t *         data;

    newerror = globus_object_construct(GLOBUS_ERROR_TYPE_MULTIPLE);
    if(!newerror)
    {
        goto error_object;
    }

    data = (globus_l_error_multiple_t *)
        globus_malloc(sizeof(globus_l_error_multiple_t));
    if(!data)
    {
        goto error_data;
    }

    data->type = type;
    data->chains = NULL;
    data->desc = NULL;

    if(fmt)
    {
        int                             size;
        va_list                         ap;
        va_list                         ap_copy;

        va_start(ap, fmt);

        globus_libc_va_copy(ap_copy, ap);
        size = globus_libc_vprintf_length(fmt, ap_copy);
        va_end(ap_copy);

        data->desc = (char *) globus_malloc(size + 1);
        if(data->desc)
        {
            globus_libc_vsnprintf(data->desc, size + 1, fmt, ap);
        }

        va_end(ap);
    }

    globus_object_set_local_instance_data(newerror, data);
    error = globus_error_initialize_base(newerror, base_source, NULL);
    if(!error)
    {
        goto error_construct;
    }

    return error;

error_construct:
error_data:
    globus_object_free(newerror);

error_object:
    return NULL;
}

/**
 * Add an error chain to a multiple error object.
 *
 * @param multiple_error
 *        The error to add the chain to.  Must have been created with
 *        globus_error_construct_multiple()
 * @param chain
 *        The chain to add to this error.  This error object assumes control
 *        over 'chain''s memory after this call.
 * @param fmt
 *        a printf style format string describing this chain
 * @return
 *        void
 */
void
globus_error_mutliple_add_chain(
    globus_object_t *                   multiple_error,
    globus_object_t *                   chain,
    const char *                        fmt,
    ...)
{
    globus_l_error_multiple_t *         data;
    globus_l_error_multiple_chain_t *   instance;

    data = (globus_l_error_multiple_t *)
        globus_object_get_local_instance_data(multiple_error);
    if(data && chain)
    {
        instance = (globus_l_error_multiple_chain_t *)
            globus_malloc(sizeof(globus_l_error_multiple_chain_t));
        if(instance)
        {
            instance->chain = chain;
            instance->desc = NULL;

            if(fmt)
            {
                int                     size;
                va_list                 ap;
                va_list                 ap_copy;

                va_start(ap, fmt);

                globus_libc_va_copy(ap_copy, ap);
                size = globus_libc_vprintf_length(fmt, ap_copy);
                va_end(ap_copy);

                instance->desc = (char *) globus_malloc(size + 1);
                if(instance->desc)
                {
                    globus_libc_vsnprintf(instance->desc, size + 1, fmt, ap);
                }

                va_end(ap);
            }

            globus_list_insert(&data->chains, instance);
        }
    }
}

/**
 * Remove an error chain from a multiple error object.
 *
 * @param multiple_error
 *        The error from which to remove a chain.  Must have been created with
 *        globus_error_construct_multiple()
 * @return
 *        The removed error chain, or NULL if none found.
 */
globus_object_t *
globus_error_multiple_remove_chain(
    globus_object_t *                   multiple_error)
{
    globus_l_error_multiple_t *         data;
    globus_object_t *                   chain = NULL;

    data = (globus_l_error_multiple_t *)
        globus_object_get_local_instance_data(multiple_error);
    if(data && data->chains)
    {
        globus_l_error_multiple_chain_t * instance;

        instance = (globus_l_error_multiple_chain_t *)
            globus_list_remove(&data->chains, data->chains);
        chain = instance->chain;
        if(instance->desc)
        {
            globus_free(instance->desc);
        }
        globus_free(instance);
    }

    return chain;
}

static
void
globus_l_error_multiple_copy(
    void *                              src,
    void **                             dst)
{
    globus_l_error_multiple_t *         copy;
    globus_l_error_multiple_t *         source;

    source = (globus_l_error_multiple_t *) src;
    copy = (globus_l_error_multiple_t *)
        globus_malloc(sizeof(globus_l_error_multiple_t));
    if(copy)
    {
        globus_list_t *                 tmp;

        copy->type = source->type;
        copy->chains = globus_list_copy(source->chains);
        copy->desc = source->desc ? globus_libc_strdup(source->desc) : NULL;

        for(tmp = copy->chains;
            !globus_list_empty(tmp);
            tmp = globus_list_rest(tmp))
        {
            globus_l_error_multiple_chain_t * instance;
            globus_l_error_multiple_chain_t * new_instance;

            instance = (globus_l_error_multiple_chain_t *)
                globus_list_first(tmp);

            new_instance = (globus_l_error_multiple_chain_t *)
                globus_malloc(sizeof(globus_l_error_multiple_chain_t));
            if(new_instance)
            {
                new_instance->chain = globus_object_copy(instance->chain);
                new_instance->desc = instance->desc
                    ? globus_libc_strdup(instance->desc) : NULL;
            }
            globus_list_replace_first(tmp, new_instance);
        }
    }

    *dst = copy;
}

static
void
globus_l_error_multiple_destroy_all(
    void *                              data)
{
    globus_l_error_multiple_chain_t * instance;

    if(!data)
    {
        return;
    }

    instance = (globus_l_error_multiple_chain_t *) data;

    globus_object_free(instance->chain);
    if(instance->desc)
    {
        globus_free(instance->desc);
    }
    globus_free(instance);
}

static
void
globus_l_error_multiple_free(
    void *                              data)
{
    globus_l_error_multiple_t *         d;

    d = (globus_l_error_multiple_t *) data;
    if(d->chains)
    {
        globus_list_destroy_all(
            d->chains, globus_l_error_multiple_destroy_all);
    }
    if(d->desc)
    {
        globus_free(d->desc);
    }
    globus_free(d);
}

static
char *
globus_l_error_multiple_printable(
    globus_object_t *                   error)
{
    return globus_l_error_multiple_print(error, GLOBUS_FALSE);
}

const globus_object_type_t GLOBUS_ERROR_TYPE_MULTIPLE_DEFINITION =
globus_error_type_static_initializer (
    GLOBUS_ERROR_TYPE_BASE,
    globus_l_error_multiple_copy,
    globus_l_error_multiple_free,
    globus_l_error_multiple_printable);
