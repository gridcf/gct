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
 * @file globus_module.c
 * @brief Module Activation and Deactivation
 */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

/******************************************************************************
			     Include header files
******************************************************************************/
#include "globus_i_common_config.h"
#include "globus_common_include.h"
#include "globus_common.h"
#include "globus_module.h"
#include "globus_list.h"
#include "globus_thread_common.h"
#include "globus_memory.h"
#include "globus_hashtable.h"
#include "globus_libc.h"
#include "globus_thread.h"

/******************************************************************************
			       Type definitions
******************************************************************************/

/*
 * global vars
 */

static int *                            globus_l_module_argc = NULL;
static char ***                         globus_l_module_argv = NULL;
static globus_thread_key_t              globus_l_activate_parent_key;
static globus_thread_key_t              globus_l_deactivate_parent_key;
/*
 * data structure needed to implement a recursive mutex
 */
typedef struct
{
    globus_mutex_t			mutex;
    globus_cond_t			cond;
    globus_thread_t			thread_id;
    int					level;
} globus_l_module_mutex_t;

/*
 * data structures for a hash table entry and the associated key
 */
typedef globus_module_activation_func_t globus_l_module_key_t;
typedef struct
{
    globus_module_descriptor_t *	descriptor;
    globus_list_t *			clients;
    int					reference_count;
    globus_module_deactivate_proxy_cb_t deactivate_cb;
    void *                              user_arg;
} globus_l_module_entry_t;

/******************************************************************************
		       Define module specific variables
******************************************************************************/

globus_bool_t
globus_i_module_initialized = GLOBUS_FALSE;

static globus_bool_t
globus_l_environ_initialized = GLOBUS_FALSE;
static globus_bool_t
globus_l_environ_mutex_initialized = GLOBUS_FALSE;

/* Recursive mutex to protect internal data structures */
static globus_l_module_mutex_t		globus_l_module_mutex;

/* Hash table and list to maintain a table of registered modules */
const int GLOBUS_L_MODULE_TABLE_SIZE = 13;
static globus_hashtable_t		globus_l_module_table;
static globus_list_t *			globus_l_module_list;

/* Hash table for globus_environ*/
const int GLOBUS_L_ENVIRON_TABLE_SIZE = 13;
static globus_mutex_t		globus_l_environ_hashtable_mutex;
static globus_hashtable_t		globus_l_environ_table;

globus_list_t *globus_l_module_atexit_funcs = GLOBUS_NULL;

/******************************************************************************
		      Module specific function prototypes
******************************************************************************/
static void
globus_l_module_initialize();

static globus_bool_t
globus_l_module_increment(
    globus_module_descriptor_t *	module_descriptor,
    globus_l_module_key_t		parent_key,
    globus_module_deactivate_proxy_cb_t deactivate_cb,
    void *                              user_arg);

static globus_l_module_entry_t *
globus_l_module_decrement(
    globus_module_descriptor_t *	module_descriptor,
    globus_l_module_key_t		parent_key);

static
int
globus_l_module_reference_count(
    globus_module_descriptor_t *	module_descriptor);
/******************************************************************************
		      Recursive mutex function prototypes
******************************************************************************/
static void
globus_l_module_mutex_init(
    globus_l_module_mutex_t *		mutex);

static void
globus_l_module_mutex_lock(
    globus_l_module_mutex_t *		mutex);

static void
globus_l_module_mutex_unlock(
    globus_l_module_mutex_t *		mutex);
    
/******************************************************************************
			   API function definitions
******************************************************************************/

#if USE_SYMBOL_LABELS
__asm__(".symver globus_module_activate_proxy_compat,"
        "globus_module_activate_proxy@GLOBUS_COMMON_11");
__asm__(".symver globus_module_activate_proxy_new,"
        "globus_module_activate_proxy@@GLOBUS_COMMON_14");
__asm__(".symver globus_module_activate_compat,"
        "globus_module_activate@GLOBUS_COMMON_11");
__asm__(".symver globus_module_activate_new,"
        "globus_module_activate@@GLOBUS_COMMON_14");
__asm__(".symver globus_module_activate_array_compat,"
        "globus_module_activate_array@GLOBUS_COMMON_11");
__asm__(".symver globus_module_activate_array_new,"
        "globus_module_activate_array@@GLOBUS_COMMON_14");

#define globus_module_activate_proxy globus_module_activate_proxy_new
#define globus_module_activate globus_module_activate_new
#define globus_module_activate_array globus_module_activate_array_new
int WARNING_USING_MIXED_THREAD_MODELS = 0;
#endif

/*
 * globus_module_activate()
 */
#ifdef __GNUC__
__attribute__ ((externally_visible))
#endif
int
globus_module_activate_proxy(
    globus_module_descriptor_t *	module_descriptor,
    globus_module_deactivate_proxy_cb_t deactivate_cb,
    void *                              user_arg)
{
    globus_l_module_key_t               parent_key;
    int                                 ret_val;
    globus_l_module_key_t               parent_key_save;
    
    /*
     * If this is the first time this routine has been called, then we need to
     * initialize the internal data structures and activate the threads
     * packages if the system has been configured to use threads.
     */
    if (globus_i_module_initialized == GLOBUS_FALSE)
    {
	globus_i_module_initialized = GLOBUS_TRUE;
	globus_l_module_initialize();
    }
    
    parent_key = (globus_l_module_key_t)
        globus_thread_getspecific(globus_l_activate_parent_key);
    /*
     * Once the recursive mutex has been acquired, increment the reference
     * counter for this module, and call it's activation function if it is not
     * currently active.
     */
    globus_l_module_mutex_lock(&globus_l_module_mutex);
    {
	ret_val = GLOBUS_SUCCESS;

	if (module_descriptor->activation_func != GLOBUS_NULL)
	{
	    if (globus_l_module_increment(module_descriptor,
					  parent_key,
					  deactivate_cb,
					  user_arg) == GLOBUS_TRUE)
	    {
		parent_key_save = parent_key;
		globus_thread_setspecific(
		    globus_l_activate_parent_key,
		    module_descriptor->activation_func);
		
		ret_val = module_descriptor->activation_func();
                
                if(ret_val != GLOBUS_SUCCESS)
                {
                    globus_l_module_decrement(
                        module_descriptor, parent_key_save);
                }
                else
                {
		/*
		 * Set up the exit handler
		 */
                    if(module_descriptor->atexit_func != GLOBUS_NULL)
                    {
                        /* only call the atexit function once */
                        if(!globus_list_search(
                            globus_l_module_atexit_funcs,
                            (void *) module_descriptor->atexit_func))
                        {
                            globus_list_insert(
                                &globus_l_module_atexit_funcs,
                                (void *) module_descriptor->atexit_func);

                            atexit(module_descriptor->atexit_func);
                        }
                    }
                }
                
                globus_thread_setspecific(
		    globus_l_activate_parent_key, parent_key_save);
	    }
	}
    }
    globus_l_module_mutex_unlock(&globus_l_module_mutex);

    return ret_val;
}
/* globus_module_activate() */

/**
 * @brief Activate a module
 * @ingroup globus_module
 * @details
 * Add a reference to the module named by module_descriptor to the list of
 * activated modules. If this is the first reference to this module, its
 * activation function will be called.
 *
 * @param module_descriptor
 *     Module to activate
 */
#ifdef __GNUC__
__attribute__ ((externally_visible))
#endif
int
globus_module_activate(
    globus_module_descriptor_t *	module_descriptor)
{
    return globus_module_activate_proxy(module_descriptor, NULL, NULL);
}

/**
 * @brief Activate a group of modules
 * @ingroup globus_module
 * @details
 * Activate an NULL-terminated array of modules. If any fail to activate, all
 * are deactivated and the error from the failed activation is returned. If
 * nonzero is returned, and failed_module is non-NULL, it will be set to point
 * to the 1st module which failed to activate. 
 * @param module_array
 *     NULL-terminated array of module descriptors to activate.
 * @param failed_module
 *     Pointer to set to the first module whose activation function failed.
 */
#ifdef __GNUC__
__attribute__ ((externally_visible))
#endif
int
globus_module_activate_array(
    globus_module_descriptor_t *        module_array[],
    globus_module_descriptor_t **       failed_module)
{
    int                                 i;
    int                                 rc = GLOBUS_SUCCESS;

    if (failed_module)
    {
        *failed_module = NULL;
    }
    for (i = 0; module_array[i] != NULL; i++)
    {
        rc = globus_module_activate(module_array[i]);

        if (rc != GLOBUS_SUCCESS)
        {
            if (failed_module)
            {
                *failed_module = module_array[i];
            }
            goto deactivate_out;
        }
    }

deactivate_out:
    if (rc != GLOBUS_SUCCESS)
    {
        for (--i; i >=0; i--)
        {
            globus_module_deactivate(module_array[i]);
        }
    }
    return rc;
}
/* globus_module_activate_array() */

#if USE_SYMBOL_LABELS
#ifdef __GNUC__
__attribute__ ((externally_visible))
#endif
int
globus_module_activate_proxy_compat(
    globus_module_descriptor_t *	module_descriptor,
    globus_module_deactivate_proxy_cb_t deactivate_cb,
    void *                              user_arg)
{
    int rc;
    rc = globus_thread_set_model("pthread");

    if (rc != GLOBUS_SUCCESS)
    {
        WARNING_USING_MIXED_THREAD_MODELS = 1;
    }
    return globus_module_activate_proxy(
        module_descriptor, deactivate_cb, user_arg);
}
/* globus_module_activate_proxy_compat() */

#ifdef __GNUC__
__attribute__ ((externally_visible))
#endif
int
globus_module_activate_compat(
    globus_module_descriptor_t *	module_descriptor)
{
    int rc;
    rc = globus_thread_set_model("pthread");

    if (rc != GLOBUS_SUCCESS)
    {
        WARNING_USING_MIXED_THREAD_MODELS = 1;
    }
    return globus_module_activate(module_descriptor);
}
/* globus_module_activate_compat() */

#ifdef __GNUC__
__attribute__ ((externally_visible))
#endif
int
globus_module_activate_array_compat(
    globus_module_descriptor_t *        module_array[],
    globus_module_descriptor_t **       failed_module)
{
    int rc;
    rc = globus_thread_set_model("pthread");

    if (rc != GLOBUS_SUCCESS)
    {
        WARNING_USING_MIXED_THREAD_MODELS = 1;

        abort();
    }

    return globus_module_activate_array(module_array, failed_module);
}
/* globus_module_activate_array_compat() */

#endif

/**
 * @brief Deactivate a module
 * @ingroup globus_module
 */
int
globus_module_deactivate(
    globus_module_descriptor_t *	module_descriptor)
{
    globus_l_module_key_t               parent_key;
    int					ret_val;
    globus_l_module_key_t		parent_key_save;


    /*
     * If module activation hasn't been initialized then return an error
     */
    if (!globus_i_module_initialized)
    {
	return GLOBUS_FAILURE;
    }
    
    parent_key = (globus_l_module_key_t)
        globus_thread_getspecific(globus_l_deactivate_parent_key);
    /*
     * Once the recursive mutex has been acquired, decrement the reference
     * counter for this module, and call it's deactivation function if it is
     * no longer being used.
     */
    ret_val = GLOBUS_SUCCESS;
    if (module_descriptor->activation_func != GLOBUS_NULL)
    {
        globus_l_module_entry_t *       entry;
        
        globus_l_module_mutex_lock(&globus_l_module_mutex);
        
        entry = globus_l_module_decrement(module_descriptor, parent_key);
        if (entry && entry->reference_count == 0)
        {
            globus_l_module_mutex_unlock(&globus_l_module_mutex);
            
            parent_key_save = parent_key;
            globus_thread_setspecific(
                globus_l_deactivate_parent_key,
                module_descriptor->activation_func);
            
            if(entry->deactivate_cb)
            {
                ret_val = entry->deactivate_cb(
                    module_descriptor, entry->user_arg);
            }
            else if(module_descriptor->deactivation_func != NULL)
            {
                ret_val = module_descriptor->deactivation_func();
            }
            
            globus_thread_setspecific(
                globus_l_deactivate_parent_key, parent_key_save);
        }
        else
        {
            if(globus_l_module_reference_count(module_descriptor) == 0)
            {
                ret_val = GLOBUS_FAILURE;
            }
            globus_l_module_mutex_unlock(&globus_l_module_mutex);
        }
    }

    return ret_val;
}
/* globus_module_deactivate() */

/*
 * globus_module_deactivate_all()
 */
int
globus_module_deactivate_all(void)
{
    /*
     * If module activation hasn't been initialized then return an error
     */
    if (!globus_i_module_initialized)
    {
	return GLOBUS_FAILURE;
    }
    
    globus_l_module_mutex_lock(&globus_l_module_mutex);
    {
	globus_bool_t			 deactivated_one;

	deactivated_one = GLOBUS_TRUE;

	while(deactivated_one)
	{
	    globus_list_t *		module_list;

	    module_list = globus_l_module_list;
	    deactivated_one = GLOBUS_FALSE;

	    while(!globus_list_empty(module_list))
	    {
		globus_l_module_entry_t *module_entry;

		module_entry = globus_list_first(module_list);
		module_list = globus_list_rest(module_list);
	    
		if(globus_list_empty(module_entry->clients) &&
		   module_entry->reference_count > 0)
		{
		    globus_l_module_mutex_unlock(&globus_l_module_mutex);
		    globus_module_deactivate(module_entry->descriptor);
		    globus_l_module_mutex_lock(&globus_l_module_mutex);
		    deactivated_one = GLOBUS_TRUE;
		}
	    }
	}
    }
    globus_l_module_mutex_unlock(&globus_l_module_mutex);

    return GLOBUS_SUCCESS;
}
/* globus_module_deactivate_all() */

/*
 * globus_module_get_module_pointer()
 *
 */

void *
globus_module_get_module_pointer(
    globus_module_descriptor_t *	structptr)
{
    void * retptr;
    void * (*module_func)();

    module_func=structptr->get_pointer_func;

    if (module_func!=NULL)
    {	
        retptr=(*module_func)();
    }
    else
    {
        retptr=GLOBUS_NULL;
    }

    return(retptr);
} 
/*globus_module_get_module_pointer();*/


/*
 * globus_module_setenv();
 */ 

void
globus_module_setenv(
    const char *                        name,
    const char *                        value)
{
    int				rc;

    /*
     *  First, check to see if the environment mutex has been initialized
     */

    if(globus_l_environ_mutex_initialized == GLOBUS_FALSE)
    {
	if(globus_i_module_initialized == GLOBUS_TRUE)
	{
	    rc = globus_mutex_init(&globus_l_environ_hashtable_mutex,
                           (globus_mutexattr_t *) GLOBUS_NULL);
            globus_assert (rc == 0);
	    globus_l_environ_mutex_initialized = GLOBUS_TRUE;
	}
    }
   
    /*
     *  then, check to see if the environment hash table has been initialized
     */
 

    if(globus_l_environ_initialized == GLOBUS_FALSE)
    {
	if(globus_i_module_initialized==GLOBUS_TRUE)
	{
	    globus_mutex_lock(&globus_l_environ_hashtable_mutex);
	}

        globus_hashtable_init(&globus_l_environ_table,
                          GLOBUS_L_ENVIRON_TABLE_SIZE,
                          globus_hashtable_string_hash,
                          globus_hashtable_string_keyeq);

	globus_l_environ_initialized = GLOBUS_TRUE;

	if(globus_i_module_initialized == GLOBUS_TRUE)
	{
	    globus_mutex_unlock(&globus_l_environ_hashtable_mutex);
	}
    }

    /*
     *  Then actually put the name and value into the hash table
     */

    if(globus_i_module_initialized == GLOBUS_TRUE)
    {
	globus_mutex_lock(&globus_l_environ_hashtable_mutex);
    }

    globus_hashtable_remove(
	&globus_l_environ_table,
	(void *) name);
    globus_hashtable_insert(
         &globus_l_environ_table,
         (void *) name,
         (void *) value);

    if(globus_i_module_initialized == GLOBUS_TRUE)
    {
	globus_mutex_unlock(&globus_l_environ_hashtable_mutex);
    }

}
/*globus_module_setenv();*/

/*
 * globus_module_getenv();
 */

char * 
globus_module_getenv(
    const char *                        name)
{
    char * 			entry;

    if(globus_l_environ_initialized == GLOBUS_TRUE)
    {
	if((globus_i_module_initialized == GLOBUS_TRUE)
	    &&(globus_l_environ_mutex_initialized == GLOBUS_TRUE))
	{
	    globus_mutex_lock(&globus_l_environ_hashtable_mutex);
	}

        entry =
           globus_hashtable_lookup(
               &globus_l_environ_table,
               (void *) name); 


	if((globus_i_module_initialized == GLOBUS_TRUE)
	    &&(globus_l_environ_mutex_initialized == GLOBUS_TRUE))
	{
	    globus_mutex_unlock(&globus_l_environ_hashtable_mutex);
	}
    }
    else
    {
        entry=GLOBUS_NULL;
    }

    /*
     *  If we have found an entry, return it
     */

    if (entry!=GLOBUS_NULL)
    {
	return(entry);
    }

    /*
     *  otherwise check system environment
     */

    entry=getenv(name);

    if (entry!=NULL)
    {
	return(entry);
    }

    return(GLOBUS_NULL);
}
/*globus_module_getenv();*/


/**
 * get version associated with module
 *
 * This function copies the version structure associated with the module
 * into 'version'.
 *
 * @param module_descriptor
 *        pointer to a module descriptor (module does NOT need to be activated)
 *
 * @param version
 *        pointer to storage for a globus_version_t.  The version will be
 *        copied into this
 *
 * @return
 *        - GLOBUS_SUCCESS
 *        - GLOBUS_FAILURE if there is no version associated with this module
 *          (module->version == null)
 */

int
globus_module_get_version(
    globus_module_descriptor_t *	module_descriptor,
    globus_version_t *                  version)
{
    globus_version_t *                  module_version;
    
    module_version = module_descriptor->version;
    
    if(!module_version)
    {
        return GLOBUS_FAILURE;
    }
    
    version->major      = module_version->major;       
    version->minor      = module_version->minor;       
    version->timestamp  = module_version->timestamp;   
    version->branch_id  = module_version->branch_id;   

    return GLOBUS_SUCCESS;
}


/**
 * print module's version
 *
 * This function prints a modules version info using the standard form 
 * provided by globus_version_print
 *
 * @param module_descriptor
 *        pointer to a module descriptor (module does NOT need to be activated)
 *
 * @param stream
 *        stream to print on (stdout, stderr, etc)
 *
 * @param verbose
 *        If GLOBUS_TRUE, then all available version info is printed 
 *        (ex: globus_module: 1.1 (1013708618-5))
 *        else, only the major.minor is printed (ex: globus_module: 1.1)
 *
 * @return
 *        - void
 */

void
globus_module_print_version(
    globus_module_descriptor_t *	module_descriptor,
    FILE *                              stream,
    globus_bool_t                       verbose)
{
    globus_version_print(
        module_descriptor->module_name,
        module_descriptor->version,
        stream,
        verbose);
}

/**
 * print all activated modules' versions
 *
 * This function prints all activated modules' version info using the standard 
 * form provided by globus_version_print
 *
 * @param stream
 *        stream to print on (stdout, stderr, etc)
 *
 * @param verbose
 *        If GLOBUS_TRUE, then all available version info is printed 
 *        (ex: globus_module: 1.1 (1013708618-5))
 *        else, only the major.minor is printed (ex: globus_module: 1.1)
 *
 * @return
 *        - void
 */

void
globus_module_print_activated_versions(
    FILE *                              stream,
    globus_bool_t                       verbose)
{
    /*
     * If module activation hasn't been initialized then there are no
     * activated modules
     */
    if(!globus_i_module_initialized)
    {
        return;
    }
    
    globus_l_module_mutex_lock(&globus_l_module_mutex);
    {
        globus_list_t *		        module_list;
        
        module_list = globus_l_module_list;
        while(!globus_list_empty(module_list))
        {
            globus_l_module_entry_t *       module_entry;
    
            module_entry = globus_list_first(module_list);
            module_list = globus_list_rest(module_list);
            
            if(module_entry->reference_count > 0)
            {
                globus_version_print(
                    module_entry->descriptor->module_name,
                    module_entry->descriptor->version,
                    stream,
                    verbose);
            }
        }
    }
    globus_l_module_mutex_unlock(&globus_l_module_mutex);

    return;
}


/**
 * print version structure
 *
 * This function provides a stand way of printing version information
 * The version is printed to stream with the following form:
 * name: major.minor                        if verbose = false
 * name: major.minor.timestamp-branch_id    if verbose = true
 *
 * In either case, if name is NULL, then only the numerical version will be 
 * printed.
 *
 * @param name
 *        A string to prefix the version.  May be NULL
 *
 * @param version
 *        The version structure containing the info to print.
 *        (May be NULL, although pointless to do so)
 *
 * @param stream
 *        stream to print on (stdout, stderr, etc)
 *
 * @param verbose
 *        If GLOBUS_TRUE, then all available version info is printed 
 *        (ex: globus_module: 1.1 (1013708618-5))
 *        else, only the major.minor is printed (ex: globus_module: 1.1)
 *
 * @return
 *        - void
 */

void
globus_version_print(
    const char *                        name,
    const globus_version_t *            version,
    FILE *                              stream,
    globus_bool_t                       verbose)
{
    if(name)
    {
        globus_libc_fprintf(stream, "%s: ", name);
    }
    
    if(version)
    {
        if(verbose)
        {
            globus_libc_fprintf(
                stream, 
                "%d.%d (%lu-%d)\n", 
                version->major,
                version->minor,
                version->timestamp,
                version->branch_id);
        }
        else
        {
            globus_libc_fprintf(
                stream, 
                "%d.%d\n", 
                version->major,
                version->minor);
        }
    }
    else
    {
        globus_libc_fprintf(stream, "%s", _GCSL("<no version>\n"));
    }
}


/******************************************************************************
		     Module specific function definitions
******************************************************************************/

/*
 * globus_l_module_initialize()
 */
static void
globus_l_module_initialize()
{
    /*
     * Initialize the threads package (can't use the standard interface since
     * it depends on threads)
     */
    globus_i_thread_pre_activate();
    globus_i_memory_pre_activate();
    /*
     * Initialize the registered module table and list
     */
    globus_hashtable_init(&globus_l_module_table,
			  GLOBUS_L_MODULE_TABLE_SIZE,
			  globus_hashtable_voidp_hash,
			  globus_hashtable_voidp_keyeq);

    globus_l_module_list = GLOBUS_NULL;
    
    /*
     * Initialize the recursive mutex
     */
    globus_l_module_mutex_init(&globus_l_module_mutex);
    
    globus_thread_key_create(&globus_l_activate_parent_key, NULL);
    globus_thread_key_create(&globus_l_deactivate_parent_key, NULL);
    
    /*
     * Now finish initializing the threads package
     */
    globus_module_activate(GLOBUS_THREAD_MODULE);
}
/* globus_l_module_initialize() */


/*
 * globus_l_module_increment()
 */
static globus_bool_t
globus_l_module_increment(
    globus_module_descriptor_t *	module_descriptor,
    globus_l_module_key_t		parent_key,
    globus_module_deactivate_proxy_cb_t deactivate_cb,
    void *                              user_arg)
{
    globus_l_module_entry_t *		entry;
    
    entry =
	globus_hashtable_lookup(
	    &globus_l_module_table,
	    (void *) module_descriptor->activation_func);

    if (entry != GLOBUS_NULL)
    {
	/*
	 * The module has already been registered.  Increment its reference
	 * counter and add any new clients to the dependency list
	 */
	entry->reference_count++;
	if (parent_key != GLOBUS_NULL
	    && globus_list_search(entry->clients,
				  (void *) parent_key) == GLOBUS_NULL)
	{
	    globus_list_insert(&entry->clients, (void *) parent_key);
	}

	if(entry->reference_count == 1)
	{
	    entry->deactivate_cb = deactivate_cb;
	    entry->user_arg = user_arg;
	    return GLOBUS_TRUE;
	}
	else
	{
    	    return GLOBUS_FALSE;
	}
    }
    else
    {
	/*
	 * This is the first time this module has been registered.  Create a
	 * new entry in the modules table.
	 */
	entry = (globus_l_module_entry_t *)
	    globus_malloc(sizeof(globus_l_module_entry_t));
	globus_assert(entry != GLOBUS_NULL);

	entry->descriptor = module_descriptor;
	entry->reference_count = 1;
	entry->clients = GLOBUS_NULL;
	entry->deactivate_cb = deactivate_cb;
	entry->user_arg = user_arg;
	if (parent_key != GLOBUS_NULL)
	{
	    globus_list_insert(&entry->clients, (void *) parent_key);
	}
	
	globus_hashtable_insert(
	    &globus_l_module_table,
	    (void *) module_descriptor->activation_func,
	    entry);

	globus_list_insert(&globus_l_module_list, entry);
	
	return GLOBUS_TRUE;
    }
}
/* globus_l_module_increment() */

static
int
globus_l_module_reference_count(
    globus_module_descriptor_t *	module_descriptor)
{
    globus_l_module_entry_t *		entry;
    
    entry =
	globus_hashtable_lookup(
	    &globus_l_module_table,
	    (void *) module_descriptor->activation_func);
    if (entry == GLOBUS_NULL || entry->reference_count <= 0)
    {
	return 0;
    }
    else
    {
        return entry->reference_count;
    }
}

/*
 * globus_l_module_decrement()
 */
static globus_l_module_entry_t *
globus_l_module_decrement(
    globus_module_descriptor_t *	module_descriptor,
    globus_l_module_key_t		parent_key)
{
    globus_l_module_entry_t *		entry;
    
    entry =
	globus_hashtable_lookup(
	    &globus_l_module_table,
	    (void *) module_descriptor->activation_func);
    if (entry == GLOBUS_NULL || entry->reference_count <= 0)
    {
	return NULL;
    }

    entry->reference_count--;
    
    if (parent_key != GLOBUS_NULL)
    {
	globus_list_t *			client_entry;

	
	client_entry = globus_list_search(entry->clients,
					  (void *) parent_key);
	if(client_entry != GLOBUS_NULL)
        {
	    globus_list_remove(&entry->clients, client_entry);
	}
	/* else module was activated outside this parent */
    }

    return entry;
}
/* globus_l_module_decrement() */


void
globus_i_module_dump(
    FILE *				out_f)
{
    globus_list_t *			module_list;

    globus_libc_fprintf(out_f, "==========\nModule List\n----------\n");
    
    module_list = globus_l_module_list;
    while(!globus_list_empty(module_list))
    {
	globus_list_t *			client_list;
	globus_l_module_entry_t *	module_entry;

	module_entry = globus_list_first(module_list);
	module_list = globus_list_rest(module_list);

	globus_libc_fprintf(out_f, "%s; cnt=%d",
		module_entry->descriptor->module_name,
		module_entry->reference_count);

	client_list = module_entry->clients;

	if (!globus_list_empty(client_list))
	{
	    void *			client_entry;
	    globus_l_module_entry_t *	client_module_entry;
	    
	    client_entry = globus_list_first(client_list);
	    client_list = globus_list_rest(client_list);
	    client_module_entry =
		globus_hashtable_lookup(&globus_l_module_table, client_entry);
	    globus_libc_fprintf(out_f, "; clients=%s",
		    client_module_entry->descriptor->module_name);
	    
	    while(!globus_list_empty(client_list))
	    {
		client_entry = globus_list_first(client_list);
		client_list = globus_list_rest(client_list);
		client_module_entry =
		    globus_hashtable_lookup(&globus_l_module_table,
					    client_entry);
		globus_libc_fprintf(out_f, ",%s",
			client_module_entry->descriptor->module_name);
	    }
	}

	globus_libc_fprintf(out_f, "\n");
    }

    globus_libc_fprintf(out_f, "==========\n");
}


/******************************************************************************
		     Recursive mutex function definitions
******************************************************************************/

/*
 * globus_l_module_mutex_init()
 */
static void
globus_l_module_mutex_init(
	globus_l_module_mutex_t *		mutex)
{
    globus_mutex_init(&mutex->mutex, (globus_mutexattr_t *) GLOBUS_NULL);
    globus_cond_init(&mutex->cond, (globus_condattr_t *) GLOBUS_NULL);

    mutex->level = 0;
}
/* globus_l_module_mutex_init() */

/*
 * globus_l_module_mutex_lock()
 */
static void
globus_l_module_mutex_lock(
	globus_l_module_mutex_t *		mutex)
{
    globus_mutex_lock(&mutex->mutex);
    {
	globus_assert(mutex->level >= 0);
	while (mutex->level > 0
		   && !globus_thread_equal( mutex->thread_id, globus_thread_self()) )
	{
	    globus_cond_wait(&mutex->cond, &mutex->mutex);
	}

	mutex->level++;
	mutex->thread_id = globus_thread_self();
	
    }
    globus_mutex_unlock(&mutex->mutex);
}
/* globus_l_module_mutex_lock() */

/*
 * globus_l_module_mutex_unlock()
 */
static void
globus_l_module_mutex_unlock(
	globus_l_module_mutex_t *		mutex)
{
    globus_mutex_lock(&mutex->mutex);
    {
	globus_assert(mutex->level > 0);
	globus_assert( globus_thread_equal( mutex->thread_id, globus_thread_self() ) );

	mutex->level--;
	if (mutex->level == 0)
	{
	    globus_cond_signal(&mutex->cond);
	}
    }
    globus_mutex_unlock(&mutex->mutex);
}
/* globus_l_module_mutex_unlock() */

void
globus_module_set_args(
    int *                               argc,
    char ***                            argv)
{
    globus_l_module_argc = argc;
    globus_l_module_argv = argv;
}

void
globus_module_get_args(
    int **                               argc,
    char ****                            argv)
{
    *argc = globus_l_module_argc;
    *argv = globus_l_module_argv;
}
