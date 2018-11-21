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
 * @file destroy.c
 * @brief globus_net_manager_context_destroy()
 */

#include "globus_net_manager_context.h"

/**
 * @brief Destroy Context
 * @ingroup globus_net_manager_context
 * @details
 * This functions destroys *context*.
 * 
 * @param [in] context
 *     Pointer to the context to destroy.
 */
void
globus_net_manager_context_destroy(
    globus_net_manager_context_t        context)
{
    globus_i_net_manager_context_t *    ctx = context;
    globus_list_t *                     list;
    globus_i_net_manager_context_entry_t * ent;
    if(ctx)
    {
        list = ctx->managers;
        while (!globus_list_empty(list))
        {            
            ent = globus_list_remove(&list, list);
            
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
        }
        free(ctx);
    }
}
