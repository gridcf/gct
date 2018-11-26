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
 * @file array_copy.c
 * @brief globus_net_manager_attr_array_copy()
 */

#include "globus_net_manager_attr.h"
#include "globus_net_manager.h"

/**
 * @brief Copy an array of Network Manager attributes
 * @ingroup globus_net_manager_attr
 * @details
 * This function performs a deep copy of array of net_manager attributes.
 * The new array will be stored in the pointer passed to by the dest_array
 * parameter. It will contain all entries in the array passed as the 
 * src_array, ending with the value #GLOBUS_NET_MANAGER_NULL_ATTR.
 *
 * The caller must free the array stored in *dest_array by calling
 * globus_net_manager_attr_array_destroy().
 *
 *
 * @param[out] dest_array
 *     A pointer to an array of attributes to be allocated and initialized.
 * @param[in] src_array
 *     An array of attributes to copy.
 *
 * @return
 * On error, the dest_array is set to NULL and this function returns an error
 * object. Otherwise, this function returns GLOBUS_SUCCESS.
 */
globus_result_t
globus_net_manager_attr_array_copy(
    globus_net_manager_attr_t         **dest_array,
    const globus_net_manager_attr_t    *src_array)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_net_manager_attr_t          *new_array = NULL;
    int                                 i = 0;

    if (dest_array == NULL)
    {
        result = GlobusNetManagerErrorParameter("NULL dest_array.");
        goto null_dest;
    }
    if (src_array == NULL)
    {
        result = GlobusNetManagerErrorParameter("NULL src_array.");
        goto null_src;
    }

    /* Count elements of src_array into i */
    for (i = 0; src_array[i].scope != NULL; i++)
    {
    }
    new_array = calloc(i+1, sizeof(globus_net_manager_attr_t));
    if (new_array == NULL)
    {
        result = GlobusNetManagerErrorMemory("new_array");
        goto calloc_new_array_fail;
    }
    for (i = 0; src_array[i].scope != NULL; i++)
    {
        new_array[i].scope = strdup(src_array[i].scope);
        if (new_array[i].scope == NULL)
        {
            result = GlobusNetManagerErrorMemory("scope");
            goto strdup_scope_fail;
        }
        new_array[i].name = strdup(src_array[i].name);
        if (new_array[i].name == NULL)
        {
            result = GlobusNetManagerErrorMemory("name");
            goto strdup_name_fail;
        }
        new_array[i].value = strdup(src_array[i].value);
        if (new_array[i].value == NULL)
        {
            result = GlobusNetManagerErrorMemory("value");
            goto strdup_value_fail;
        }
    }
    new_array[i] = globus_net_manager_null_attr;

    if (result != GLOBUS_SUCCESS)
    {
        do
        {
            free(new_array[i].value);
strdup_value_fail:
            free(new_array[i].name);
strdup_name_fail:
            free(new_array[i].scope);
strdup_scope_fail:
            i--;
        } while (i >= 0);
        free(new_array);
        new_array = NULL;
    }
calloc_new_array_fail:
null_src:
    *dest_array = new_array;
null_dest:
    return result;
}
/* globus_net_manager_attr_array_copy() */
