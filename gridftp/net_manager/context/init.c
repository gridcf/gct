/*
 * Copyright 1999-2014 University of Chicago
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
 * @file
 * @brief globus_net_manager_context_init()
 */

#include "globus_net_manager_context.h"


static 
globus_result_t
globus_l_net_manager_context_load_entry(
    const char *                            name,
    globus_i_net_manager_context_entry_t ** entry)
{
    globus_extension_handle_t               ext_handle;
    globus_net_manager_t *                  loaded_manager;
    char *                                  dll_name = NULL;
    globus_i_net_manager_context_entry_t *  ent = NULL;
    int                                     rc;
    globus_result_t                         result = GLOBUS_SUCCESS;

    /* is module already in registry? */
    loaded_manager = (globus_net_manager_t *) globus_extension_lookup(
        &ext_handle, GLOBUS_NET_MANAGER_REGISTRY, (void *) name);
    if(loaded_manager == NULL)
    {
        /* load and activate the dll */
        dll_name = globus_common_create_string(
            "globus_net_manager_%s", name);
        if (dll_name == NULL)
        {
            result = GlobusNetManagerErrorMemory("dll_name");
            goto dll_name_alloc;
        }

        rc = globus_extension_activate(dll_name);        
        if(rc != GLOBUS_SUCCESS)
        {
            result = GlobusNetManagerErrorManager(
                rc, name, "attempting to activate module.");
            goto error_activate;
        }
    
        /* now module should be in registry */
        loaded_manager = globus_extension_lookup(
            &ext_handle, GLOBUS_NET_MANAGER_REGISTRY, (void *) name);
        if(loaded_manager == NULL)
        {
            result = GlobusNetManagerErrorManager(
                rc, name, "attempting to load activated module.");
            goto error_lookup;
        }
    }
    ent = malloc(sizeof(globus_i_net_manager_context_entry_t));
    if (ent == NULL)
    {
        result = GlobusNetManagerErrorMemory("ent");
        goto error_ent;
    }
    *ent = (globus_i_net_manager_context_entry_t) {
        .manager = loaded_manager,
        .ext_handle = ext_handle,
        .name = strdup(name),
        .dll_name = dll_name
    };

    if (ent->name == NULL)
    {
        result = GlobusNetManagerErrorMemory("name");
    
        free(ent);
        ent = NULL;
error_ent:
error_lookup:
        if (dll_name)
        {
            globus_extension_deactivate(dll_name);
error_activate:
            free(dll_name);
        }
dll_name_alloc:
        if (result == GLOBUS_SUCCESS)
        {
            result = GLOBUS_FAILURE;
        }
    }
    *entry = ent;
    return result;
}
/* globus_l_net_manager_context_load_entry() */


/**
 * @brief Initialize Context
 * @ingroup globus_net_manager_context
 * @details
 * This functions initializes *context* with the attribute list *attrs*.
 * 
 * @param [out] context
 *     A pointer to the context to initialize.
 * @param [in] attrs
 *     An array of attributes to initialize the context with.
 * @return
 *     On error, the 'context' is set to NULL and this function returns
 *     an error object. Otherwise this function returns 'GLOBUS_SUCCESS'
 */
globus_result_t
globus_net_manager_context_init(
    globus_net_manager_context_t *      context,
    const globus_net_manager_attr_t *   attrs)
{
    globus_i_net_manager_context_t *    ctx;
    globus_result_t                     result;
    int                                 i;
    int                                 j;
    int                                 rc;
    int                                 max_attr_count;
    int                                 attrnum;
    char *                              current_scope = NULL;
    globus_i_net_manager_context_entry_t *  ent = NULL;

    if(context == NULL || attrs == NULL || attrs[0].scope == NULL)
    {
        result = GlobusNetManagerErrorParameter("No parameter may be NULL.");
        goto error_no_attr;
    }
    
    ctx = malloc(sizeof(globus_i_net_manager_context_t));
    if(ctx == NULL)
    {
        result = GlobusNetManagerErrorMemory("context");
        goto error_ctx_mem;
    }
    ctx->managers = NULL;

    for(max_attr_count = 0; 
        attrs[max_attr_count].scope != NULL;
        max_attr_count++);
    
    for(i = 0; attrs[i].scope != NULL; i++)
    {
        if(strcmp(attrs[i].scope, "global") != 0)
        {
            /* Ignore global scope attributes here. They get added
             * to each new manager's attribute list when we encounter them.
             */
            if (current_scope == NULL ||
                     strcmp(attrs[i].scope, current_scope) != 0)
            {
                /* start of a new manager entry, either explicitly or by
                 * changing scope
                 */
                if(strcmp(attrs[i].scope, "net_manager") == 0 && 
                    strcmp(attrs[i].name, "manager") == 0)
                {
                    ent = NULL;
                    attrnum = 0;
                    current_scope = attrs[i].value;
                }
                else
                {
                    ent = NULL;
                    attrnum = 0;
                    current_scope = attrs[i].scope;
                }

                result = globus_l_net_manager_context_load_entry(
                    current_scope, &ent);
                if(result)
                {
                    goto error_load;
                }

                ent->attrs = calloc(
                    max_attr_count, sizeof(globus_net_manager_attr_t));
                for(j = 0; attrs[j].scope != NULL; j++)
                {
                    if(strcmp(attrs[j].scope, "global") == 0)
                    {
                        result = globus_net_manager_attr_init(
                                &ent->attrs[attrnum++],
                                attrs[j].scope,
                                attrs[j].name,
                                attrs[j].value);
                        if(result)
                        {
                            goto error_global_attr;
                        }
                    }
                }
                ent->attrs[attrnum] = globus_net_manager_null_attr;
                
                rc = globus_list_insert(&ctx->managers, ent);
                if (rc != GLOBUS_SUCCESS)
                {
                    result = GlobusNetManagerErrorMemory("managers");
                    goto error_list_insert;
                }
            }
            /* attrs for the current manager entry */
            if(current_scope && strcmp(attrs[i].scope, current_scope) == 0)
            {
                result = globus_net_manager_attr_init(
                        &ent->attrs[attrnum++],
                        attrs[i].scope,
                        attrs[i].name,
                        attrs[i].value);
                if(result)
                {
                    goto error_attr;
                }
                ent->attrs[attrnum] = globus_net_manager_null_attr;
            }
        }
    }
    
    *context = ctx;
    return GLOBUS_SUCCESS;

error_global_attr:
error_list_insert:
    globus_extension_release(ent->ext_handle);
    /* if dll_name is set, driver was activated by me */
    if(ent->dll_name)
    {
        globus_extension_deactivate(ent->dll_name);
        free(ent->dll_name);
    }
    globus_net_manager_attr_array_delete(ent->attrs);
    free(ent->name);
    free(ent);

error_attr:
error_load:
    globus_net_manager_context_destroy(ctx);
error_ctx_mem:
error_no_attr:
    if (context)
    {
        *context = NULL;
    }

    return result;
}
/* globus_net_manager_context_init() */
