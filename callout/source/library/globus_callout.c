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
 * @file globus_callout.c
 * @brief Globus Callout Infrastructure
 * @author Sam Meder
 */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

#include "globus_common.h"
#include "globus_callout_constants.h"
#include "globus_i_callout.h"

#include <ltdl.h>

#include "version.h"

#define GLOBUS_I_CALLOUT_HASH_SIZE 64
#define GLOBUS_I_CALLOUT_LINEBUF 4096

#if defined(_WIN32) || defined(__CYGWIN__)
#define MY_LIB_EXT ".dll"
#else
#define MY_LIB_EXT ".so"
#endif

static void
globus_l_callout_library_table_element_free(
    void *                              element);

static void
globus_l_callout_data_free(
    void *           data);


static int globus_l_callout_activate(void);
static int globus_l_callout_deactivate(void);

int                              globus_i_callout_debug_level   = 0;
FILE *                           globus_i_callout_debug_fstream = NULL;

/**
 * Module descriptor static initializer.
 * @ingroup globus_callout_activation
 */
globus_module_descriptor_t globus_i_callout_module =
{
    "globus_callout_module",
    globus_l_callout_activate,
    globus_l_callout_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

/**
 * Module activation
 * @ingroup globus_callout_activation
 */
static
int
globus_l_callout_activate(void)
{
    int                                 result = (int) GLOBUS_SUCCESS;
    char *                              tmp_string;
    static char *                       _function_name_ =
        "globus_l_callout_activate";

    tmp_string = globus_module_getenv("GLOBUS_CALLOUT_DEBUG_LEVEL");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_callout_debug_level = atoi(tmp_string);
        
        if(globus_i_callout_debug_level < 0)
        {
            globus_i_callout_debug_level = 0;
        }
    }

    tmp_string = globus_module_getenv("GLOBUS_CALLOUT_DEBUG_FILE");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_callout_debug_fstream = fopen(tmp_string, "a");
        if(globus_i_callout_debug_fstream == NULL)
        {
            result = (int) GLOBUS_FAILURE;
            goto exit;
        }
    }
    else
    {
        globus_i_callout_debug_fstream = stderr;
    }

    GLOBUS_I_CALLOUT_DEBUG_ENTER;

    result = globus_module_activate(GLOBUS_COMMON_MODULE);

    if(result != GLOBUS_SUCCESS)
    {
        goto exit;
    }

    if((result = lt_dlinit()) != 0)
    {
        goto exit;
    }

    GLOBUS_I_CALLOUT_DEBUG_EXIT;

 exit:

    return result;
}

/**
 * Module deactivation
 * @ingroup globus_callout_activation
 */
static
int
globus_l_callout_deactivate(void)
{
    int                                 result = (int) GLOBUS_SUCCESS;
    static char *                       _function_name_ =
        "globus_l_callout_deactivate";

    GLOBUS_I_CALLOUT_DEBUG_ENTER;

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    GLOBUS_I_CALLOUT_DEBUG_EXIT;

    if(globus_i_callout_debug_fstream != stderr)
    {
        fclose(globus_i_callout_debug_fstream);
    }

    result = lt_dlexit();

    return result;
}

/**
 * Initialize a Globus Callout Handle
 * @ingroup globus_callout_handle
 *
 * @param handle
 *        Pointer to the handle that is to be initialized
 * @return
 *    This function returns GLOBUS_SUCCESS or a globus_result_t referring
 *    to an error object of one of the  following types
 * @retval GLOBUS_CALLOUT_ERROR_WITH_HASHTABLE Hashtable initialization failed
 */
globus_result_t
globus_callout_handle_init(
    globus_callout_handle_t *           handle)
{
    int                                 rc;
    globus_result_t                     result = GLOBUS_SUCCESS;

    static char *                       _function_name_ =
        "globus_callout_handle_init";

    GLOBUS_I_CALLOUT_DEBUG_ENTER;

    *handle = malloc(sizeof(globus_i_callout_handle_t));

    if(*handle == NULL)
    {
        GLOBUS_CALLOUT_MALLOC_ERROR(result);
        goto exit;
    }
    
    if((rc = globus_hashtable_init(&((*handle)->symbol_htable),
                                   GLOBUS_I_CALLOUT_HASH_SIZE,
                                   globus_hashtable_string_hash,
                                   globus_hashtable_string_keyeq)) < 0)
    {
        free(*handle);
        GLOBUS_CALLOUT_ERROR_RESULT(
            result,
            GLOBUS_CALLOUT_ERROR_WITH_HASHTABLE,
            ("globus_hashtable_init retuned %d", rc));
        goto exit;
    }    

    if((rc = globus_hashtable_init(&((*handle)->library_htable),
                                   GLOBUS_I_CALLOUT_HASH_SIZE,
                                   globus_hashtable_string_hash,
                                   globus_hashtable_string_keyeq)) < 0)
    {
        globus_hashtable_destroy(&((*handle)->symbol_htable));
        free(*handle);
        GLOBUS_CALLOUT_ERROR_RESULT(
            result,
            GLOBUS_CALLOUT_ERROR_WITH_HASHTABLE,
            ("globus_hashtable_init retuned %d", rc));
        goto exit;
    }    

    GLOBUS_I_CALLOUT_DEBUG_EXIT;

 exit:
    
    return result;
}/*globus_callout_handle_init*/

/**
 * Destroy a Globus Callout Handle
 * @ingroup globus_callout_handle
 *
 * @param handle
 *        The handle that is to be destroyed
 * @return
 *        GLOBUS_SUCCESS
 */
globus_result_t
globus_callout_handle_destroy(
    globus_callout_handle_t             handle)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    static char *                       _function_name_ =
        "globus_callout_handle_destroy";
    GLOBUS_I_CALLOUT_DEBUG_ENTER;
    
    /* free hashes */

    globus_hashtable_destroy_all(
        &(handle->library_htable),
        globus_l_callout_library_table_element_free);

    globus_hashtable_destroy_all(
        &(handle->symbol_htable),
        globus_l_callout_data_free);

    free(handle);
    
    GLOBUS_I_CALLOUT_DEBUG_EXIT;

    return result;
}/*globus_callout_handle_destroy*/

/**
 * Read callout configuration from file.
 * @ingroup globus_callout_config
 *
 * This function read a configuration file with the following format:
 *    - Anything after a <i>#</i> is assumed to be a comment
 *    - Blanks lines are ignored
 *    - Lines specifying callouts have the format
 @verbatim
        abstract type           library         symbol
 @endverbatim
 *    - where <i>abstract type</i> denotes the type of callout,
 *      e.g. globus_gram_jobmanager_authz, <i>library</i> denotes the library
 *      the callout can be found in and <i>symbol</i> denotes the function name
 *      of the callout. The library argument can be specified in two forms,
 *      libfoo or libfoo_<i>flavor</i>. When using the former version the
 *      current flavor will automatically be added to the library name if
 *      needed. 
 *
 * @param handle
 *        The handle that is to be configured
 * @param filename
 *        The file to read configuration from
 * @return
 *    This function returns GLOBUS_SUCCESS or a globus_result_t referring
 *    to an error object of one of the  following types
 * @retval GLOBUS_CALLOUT_ERROR_OPENING_CONF_FILE Error opening filename
 * @retval GLOBUS_CALLOUT_ERROR_PARSING_CONF_FILE Error parsing file
 * @retval GLOBUS_CALLOUT_ERROR_OUT_OF_MEMORY Out of memory
 */
globus_result_t
globus_callout_read_config(
    globus_callout_handle_t             handle,
    char *                              filename)
{
    FILE *                              conf_file;
    char                                buffer[GLOBUS_I_CALLOUT_LINEBUF];
    char                                type[128];
    char                                library[256];
    char                                symbol[128];
    char *                              env_argstr;
    char **                             env_args;
    int                                 numpairs = 0;
    char *                              flavor_start;
    char *                              pound;
    int                                 index;
    int                                 rc;
    globus_result_t                     result;
    globus_i_callout_data_t *           datum = NULL;
    globus_i_callout_data_t *           existing_datum;
    

    static char *                       _function_name_ =
        "globus_callout_read_config";

    GLOBUS_I_CALLOUT_DEBUG_ENTER;
    
    conf_file = fopen(filename, "r");

    if(conf_file == NULL)
    {
        GLOBUS_CALLOUT_ERRNO_ERROR_RESULT(
            result,
            GLOBUS_CALLOUT_ERROR_OPENING_CONF_FILE,
            ("filename %s", filename));
        goto error_exit;
    }
    
    while(fgets(buffer,GLOBUS_I_CALLOUT_LINEBUF,conf_file))
    {
        if(!strchr(buffer, '\n'))
        {
            GLOBUS_CALLOUT_ERROR_RESULT(
                result,
                GLOBUS_CALLOUT_ERROR_PARSING_CONF_FILE,
                ("malformed line, line too long or missing newline"));
            goto error_exit;
        }

        /* strip any comments */
        pound = strchr(buffer, '#');

        if(pound != NULL)
        { 
            *pound = '\0';
        }

        /* strip white space from start */
        
        index = 0;

        while(buffer[index] == '\t' || buffer[index] == ' ')
        {
            index++;
        }

        /* if blank line continue */
        
        if(buffer[index] == '\0' || buffer[index] == '\n')
        { 
            continue;
        }
        
        if(sscanf(&buffer[index],"%127s%255s%127s",type,library,symbol) < 3)
        {
            GLOBUS_CALLOUT_ERROR_RESULT(
                result,
                GLOBUS_CALLOUT_ERROR_PARSING_CONF_FILE,
                ("malformed line: %s", &buffer[index]));
            goto error_exit;
        }
        
        /* check for ENV vars to set */
        env_argstr = strstr(buffer, "ENV:");
        if(env_argstr && strchr(env_argstr, '='))
        {
            int                         i;
            char *                      ptr;
            char *                      start;
            
            numpairs = 0;
            ptr = strchr(env_argstr, '=');
            while(ptr)
            {
                numpairs++;
                ptr++;
                if(*ptr == '"')
                {
                    ptr = strchr(ptr + 1, '"');
                    if(!ptr)
                    {
                        GLOBUS_CALLOUT_ERROR_RESULT(
                            result,
                            GLOBUS_CALLOUT_ERROR_PARSING_CONF_FILE,
                            ("malformed line, unmatched quote: %s", buffer));
                        goto error_exit;
                    }
                }
                ptr = strchr(ptr + 1, '=');
            }
            
            if(numpairs > 0)
            {
                env_args = globus_calloc(numpairs*2+1, sizeof(char *));
                
                start = env_argstr + 4;
                
                i = 0;
                while(start)
                {                    
                    /* skip initial space */
                    while(isspace(*start))
                    {
                        start++;
                    }
                    
                    /* find var name */
                    ptr = strchr(start, '=');
                    *ptr = '\0';
                    
                    if(strcspn(start, " \"=") != strlen(start))
                    {
                        GLOBUS_CALLOUT_ERROR_RESULT(
                            result,
                            GLOBUS_CALLOUT_ERROR_PARSING_CONF_FILE,
                            ("malformed line, invalid character in ENV var: %s", start));
                        goto error_exit;
                    }

                    env_args[i] = globus_libc_strdup(start);
                    
                    /* find value in quotes or before a space or end of line */
                    start = ++ptr;
                    
                    if(*start == '"')
                    {
                        start++;
                        ptr = strchr(start, '"');
                        *ptr = '\0';
                    }
                    else
                    {
                        ptr = strchr(start, ' ');
                        if(!ptr)
                        {
                            ptr = strchr(start, '\n');
                        }
                        *ptr = '\0';                        
                    }
                    env_args[i+1] = globus_libc_strdup(start);

                    ptr++;
                    while(*ptr && isspace(*ptr))
                    {
                        ptr++;
                    }
                    if(*ptr && strchr(ptr, '='))
                    {
                        start = ptr;
                    }
                    else
                    {
                        start = NULL;
                    }
                    
                    i += 2; 
                }
                env_args[i] = NULL;
            }
        }
        else
        {
            env_args = NULL;
        }
        
        /* push values into hash */
        datum = malloc(sizeof(globus_i_callout_data_t));

        if(datum == NULL)
        {
            GLOBUS_CALLOUT_MALLOC_ERROR(result);
            goto error_exit;
        }

        memset(datum,'\0',sizeof(globus_i_callout_data_t));

        /* check if library is flavored already */

        if((flavor_start = strrchr(library,'_')) &&
           (strstr(flavor_start, "32") || strstr(flavor_start, "64")))
        {
            datum->file = strdup(library);
            
            if(datum->file == NULL)
            {
                GLOBUS_CALLOUT_MALLOC_ERROR(result);
                goto error_exit;
            }
        }
        else
        { 
            datum->file = malloc(strlen(library) + 2 + strlen(flavor));
            if(datum->file == NULL)
            {
                GLOBUS_CALLOUT_MALLOC_ERROR(result);
                goto error_exit;
            }
            datum->file[0] = '\0';
            strcat(datum->file, library);
            strcat(datum->file, "_");
            strcat(datum->file, flavor);
        }
        
        datum->symbol = strdup(symbol);

        if(datum->symbol == NULL)
        {
            GLOBUS_CALLOUT_MALLOC_ERROR(result);
            goto error_exit;
        }
        
        if(*type == '|')
        {
            datum->mandatory = GLOBUS_FALSE;
            datum->type = strdup(type + 1);
        }
        else
        {
            datum->mandatory = GLOBUS_TRUE;
            datum->type = strdup(type);
        }

        if(datum->type == NULL)
        {
            GLOBUS_CALLOUT_MALLOC_ERROR(result);
            goto error_exit;
        }

        datum->env_args = env_args;
        datum->num_env_args = numpairs;

        if((rc = globus_hashtable_insert(&handle->symbol_htable,
                                         datum->type,
                                         datum)) == -1)
        {
            existing_datum = globus_hashtable_lookup(&handle->symbol_htable,
                                                     datum->type);
            while(existing_datum->next)
            {
                existing_datum = existing_datum->next;
            }
            existing_datum->next = datum;
        }
    }

    fclose(conf_file);
    
    GLOBUS_I_CALLOUT_DEBUG_EXIT;

    return GLOBUS_SUCCESS;

 error_exit:

    if(datum != NULL)
    {
        globus_l_callout_data_free(datum);
    }

    if(conf_file != NULL)
    {
        fclose(conf_file);
    }

    return result;
}/*globus_callout_read_config*/

/**
 * Register callout configuration
 * @ingroup globus_callout_config
 *
 * This function registers a callout type in the given handle.
 *
 * @param handle
 *        The handle that is to be configured
 * @param type
 *        The abstract type of the callout
 * @param library
 *        The location of the library containing the callout
 * @param symbol
 *        The symbol (ie function name) for the callout
 * @return
 *    This function returns GLOBUS_SUCCESS or a globus_result_t referring
 *    to an error object of one of the following types
 * @retval GLOBUS_CALLOUT_ERROR_OUT_OF_MEMORY Out of memory
 */
globus_result_t
globus_callout_register(
    globus_callout_handle_t             handle,
    char *                              type,
    char *                              library,
    char *                              symbol)
{
    int                                 rc;
    globus_result_t                     result;
    globus_i_callout_data_t *           datum = NULL;
    globus_i_callout_data_t *           existing_datum;
    char *                              flavor_start;
    
    static char *                       _function_name_ =
        "globus_callout_register";

    GLOBUS_I_CALLOUT_DEBUG_ENTER;
    
    
    /* push values into hash */

    datum = malloc(sizeof(globus_i_callout_data_t));
    
    if(datum == NULL)
    {
        GLOBUS_CALLOUT_MALLOC_ERROR(result);
        goto error_exit;
    }
    
    memset(datum,'\0',sizeof(globus_i_callout_data_t));
    datum->mandatory = GLOBUS_TRUE;

    if((flavor_start = strrchr(library,'_')) &&
       (strstr(flavor_start, "32") || strstr(flavor_start, "64")))
    {
        datum->file = strdup(library);
        
        if(datum->file == NULL)
        {
            GLOBUS_CALLOUT_MALLOC_ERROR(result);
            goto error_exit;
        }
    }
    else
    { 
        datum->file = malloc(strlen(library) + 2 + strlen(flavor));
        if(datum->file == NULL)
        {
            GLOBUS_CALLOUT_MALLOC_ERROR(result);
            goto error_exit;
        }
        datum->file[0] = '\0';
        strcat(datum->file, library);
        strcat(datum->file, "_");            
        strcat(datum->file, flavor);
    }
    
    datum->symbol = strdup(symbol);
    
    if(datum->symbol == NULL)
    {
        GLOBUS_CALLOUT_MALLOC_ERROR(result);
        goto error_exit;
    }
    
    datum->type = strdup(type);
    
    if(datum->type == NULL)
    {
        GLOBUS_CALLOUT_MALLOC_ERROR(result);
        goto error_exit;
    }
    
    if((rc = globus_hashtable_insert(&handle->symbol_htable,
                                     datum->type,
                                     datum)) == -1)
    {
        existing_datum = globus_hashtable_lookup(&handle->symbol_htable,
                                                 datum->type);
        while(existing_datum->next)
        {
            existing_datum = existing_datum->next;
        }
        existing_datum->next = datum;
    }
    
    GLOBUS_I_CALLOUT_DEBUG_EXIT;

    return GLOBUS_SUCCESS;

 error_exit:

    GLOBUS_I_CALLOUT_DEBUG_EXIT;
    
    if(datum != NULL)
    {
        globus_l_callout_data_free(datum);
    }
 
    return result;
}/*globus_callout_register*/

/**
 * Call a callout of specified abstract type
 * @ingroup globus_callout_call
 *
 * This function looks up the callouts corresponding to the given type and
 * invokes them with the passed arguments. If a invoked callout returns an
 * error it will be chained to a error of the type
 * GLOBUS_CALLOUT_ERROR_CALLOUT_ERROR and no more callouts will be called.
 *
 * @param handle
 *        A configured callout handle
 * @param type
 *        The abstract type of the callout that is to be invoked
 * @return
 *    This function returns GLOBUS_SUCCESS or a globus_result_t referring
 *    to an error object of one of the following types
 * @retval GLOBUS_CALLOUT_ERROR_TYPE_NOT_REGISTERED Callout type not registered
 * @retval GLOBUS_CALLOUT_ERROR_CALLOUT_ERROR Callout function error
 * @retval GLOBUS_CALLOUT_ERROR_WITH_DL Error with dlopen or dlsym
 * @retval GLOBUS_CALLOUT_ERROR_WITH_HASHTABLE Error caching dlopen handle
 * @retval GLOBUS_CALLOUT_ERROR_OUT_OF_MEMORY Out of memory
 */
globus_result_t
globus_callout_call_type(
    globus_callout_handle_t             handle,
    char *                              type,
    ...)
{
    globus_i_callout_data_t *           current_datum;
    lt_ptr                              function;
    lt_dlhandle *                       dlhandle;
    globus_result_t                     result = GLOBUS_SUCCESS;
    va_list                             ap;
    int                                 rc;
    const char *                        dlerror;
    char *                              flavor_start;
    char *                              file;
    char                                library[1024];
    char **                             save_env;
    int                                 i;
    globus_i_callout_data_t *           tmp_datum;
    int                                 mandatory_callouts_remaining = 0;
    static char *                       _function_name_ =
        "globus_callout_handle_call_type";
    GLOBUS_I_CALLOUT_DEBUG_ENTER;

    current_datum = globus_hashtable_lookup(&handle->symbol_htable,
                                            type);
    if(current_datum == NULL)
    {
        GLOBUS_CALLOUT_ERROR_RESULT(
            result,
            GLOBUS_CALLOUT_ERROR_TYPE_NOT_REGISTERED,
            ("unknown type: %s\n", type));
        goto exit;
    }
    
    tmp_datum = current_datum;
    while(tmp_datum)
    {
        if(tmp_datum->mandatory)
        {
            mandatory_callouts_remaining++;
        }
        tmp_datum = tmp_datum->next;
    }
    
    do
    {
        dlhandle = globus_hashtable_lookup(&handle->library_htable,
                                           current_datum->file);

        if(dlhandle == NULL)
        {
            dlhandle = malloc(sizeof(lt_dlhandle));
            
            if(dlhandle == NULL)
            {
                GLOBUS_CALLOUT_MALLOC_ERROR(result);
            }
            
            *dlhandle = NULL;
            rc = globus_hashtable_insert(&handle->library_htable,
                                         current_datum->file,
                                         dlhandle);
            if(rc < 0)
            {
                free(dlhandle);
                GLOBUS_CALLOUT_ERROR_RESULT(
                    result,
                    GLOBUS_CALLOUT_ERROR_WITH_HASHTABLE,
                    ("globus_hashtable_insert retuned %d", rc));
                goto exit;
            }            
        }
    
        if(*dlhandle == NULL)
        {
            /* first time a symbol is referenced in this library ->
             * need to open it
             */
            
            *dlhandle = lt_dlopenext(current_datum->file);
            if(*dlhandle == NULL)
            {
                /* older libtools don't search the extensions correctly */
                snprintf(library, 1024, "%s" MY_LIB_EXT, current_datum->file);
                library[1023] = 0;
                *dlhandle = lt_dlopenext(library);
            }
            
            if(*dlhandle == NULL)
            {
                /* try again with flavor string removed */
                flavor_start = strrchr(current_datum->file, '_');
                if (flavor_start) {
                    file = strdup(current_datum->file);
                    if(file == NULL)
                        {
                            GLOBUS_CALLOUT_MALLOC_ERROR(result);
                            goto exit;
                        }
                    file[flavor_start - current_datum->file] = '\0';
                    *dlhandle = lt_dlopenext(file);
                    if(*dlhandle == NULL)
                    {
                        /* older libtools don't search the extensions correctly */
                        snprintf(library, 1024, "%s" MY_LIB_EXT, file);
                        library[1023] = 0;
                        *dlhandle = lt_dlopenext(library);
                    }
                    free(file);
                }
            }
            if(*dlhandle == NULL)
            {
                GLOBUS_CALLOUT_ERROR_RESULT(
                    result,
                    GLOBUS_CALLOUT_ERROR_WITH_DL,
                    ("couldn't dlopen %s: %s\n",
                     library,
                     (dlerror = lt_dlerror()) ? dlerror : 
                        "unknown error, possibly file not found."));
                goto exit;
            }
        }

        function = lt_dlsym(*dlhandle, current_datum->symbol);

        if(function == NULL)
        {
            GLOBUS_CALLOUT_ERROR_RESULT(
                result,
                GLOBUS_CALLOUT_ERROR_WITH_DL,
                ("symbol %s could not be found in %s: %s\n",
                 current_datum->symbol,
                 current_datum->file,
                 (dlerror = lt_dlerror()) ? dlerror : "(null)"));
            goto exit;
        }

        if(current_datum->env_args)
        {            
            save_env = globus_calloc(
                current_datum->num_env_args*2+1, sizeof(char *));

            i = 0;
            while(current_datum->env_args[i] != NULL && 
                current_datum->env_args[i+1] != NULL)
            {
                save_env[i] = current_datum->env_args[i];
                save_env[i+1] = 
                    globus_libc_strdup(getenv(current_datum->env_args[i]));
                globus_libc_setenv(current_datum->env_args[i], current_datum->env_args[i+1], 1);
                i += 2;
            }
            save_env[i] = NULL;
        }

        va_start(ap,type);
    
        result = ((globus_callout_function_t) function)(ap);
        
        va_end(ap);

        if(current_datum->env_args)
        {
            i = 0;            
            while(save_env[i] != NULL)
            {
                if(save_env[i+1] == NULL)
                {
                    globus_libc_unsetenv(save_env[i]);
                }
                else
                {
                    globus_libc_setenv(save_env[i], save_env[i+1], 1);
                    globus_free(save_env[i+1]);
                }
                                
                i += 2;
            }
            globus_free(save_env);
        }

        if(result == GLOBUS_SUCCESS)
        {
            if(current_datum->mandatory)
            {
                mandatory_callouts_remaining--;
            }
            
            if(!mandatory_callouts_remaining)
            {
                goto exit;
            }
        }
        
        if(result != GLOBUS_SUCCESS)
        {
            if(current_datum->mandatory)
            {
                GLOBUS_CALLOUT_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_CALLOUT_ERROR_CALLOUT_ERROR);
                goto exit;
            }
            else if(current_datum->next == NULL)
            {
                /* chain error with stored error */
                GLOBUS_CALLOUT_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_CALLOUT_ERROR_CALLOUT_ERROR);
                goto exit;
            }
            else
            {
                /* store error */
                result = GLOBUS_SUCCESS;
            }
        }

        current_datum = current_datum->next;
    }
    while(current_datum);
    
 exit:
    GLOBUS_I_CALLOUT_DEBUG_EXIT;
    return result;
}/*globus_callout_call_type*/

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

static void
globus_l_callout_data_free(
    void *           datum)
{
    static char *                       _function_name_ =
        "globus_l_callout_data_free";
    globus_i_callout_data_t *           data = datum;
    GLOBUS_I_CALLOUT_DEBUG_ENTER;

    if(data != NULL)
    {
        if(data->type != NULL)
        {
            free(data->type);
        }
        
        if(data->file != NULL)
        {
            free(data->file);
        }
        
        if(data->symbol != NULL)
        {
            free(data->symbol);
        }
        
        if(data->env_args != NULL)
        {
            int i = 0;
            while(data->env_args[i] != NULL)
            {
                free(data->env_args[i]);
                i++;
            }
            free(data->env_args);
        }
        
        free(data);
    }
    
    GLOBUS_I_CALLOUT_DEBUG_EXIT;
}



static void
globus_l_callout_library_table_element_free(
    void *                              element)
{
    lt_dlhandle *                       dlhandle;
    globus_result_t                     result;
    static char *                       _function_name_ =
        "globus_l_callout_library_table_element_free";
    GLOBUS_I_CALLOUT_DEBUG_ENTER;

    dlhandle = (lt_dlhandle *) element;

    if(dlhandle != NULL)
    { 
        if(*dlhandle != NULL)
        {
            if(lt_dlclose(*dlhandle) < 0)
            {
                GLOBUS_CALLOUT_ERROR_RESULT(
                    result,
                    GLOBUS_CALLOUT_ERROR_WITH_DL,
                    ("failed to close library"));
            }
        }

        free(dlhandle);
    }
    
    GLOBUS_I_CALLOUT_DEBUG_EXIT;
    return;
}

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
