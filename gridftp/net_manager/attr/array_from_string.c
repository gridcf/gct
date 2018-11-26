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
 * @file array_from_string.c
 * @brief globus_net_manager_attr_array_from_string()
 */

#include "globus_net_manager_attr.h"
#include "globus_net_manager.h"

/**
 * @brief Parse an array of Network Manager attributes from a string
 * @ingroup globus_net_manager_attr
 * @details
 * This function parses a string containing a list
 * of attributes and creates a new array of Network Manager attribute
 * values from it. The array is terminated by globus_net_manager_null_attr.
 *
 * Attribute strings are formed by the regular expression
@verbatim
    NAME=VALUE(;NAME=VALUE)*
@endverbatim
 * The NAME and VALUE strings may contain any character except <code>;</code>
 * <code>=</code>, and the carriage return and newline characters.
 *
 * The caller must free the array stored in *attr by calling
 * globus_net_manager_attr_array_destroy().
 *
 * @param[out] attr
 *     A pointer to an array of attributes to be allocated and initialized.
 * @param[in] scope
 *     The string to be added as the scope value of the attributes.
 * @param[in] attr_string
 *     The string to be parsed.
 *
 * @return
 *     On success, this function returns GLOBUS_SUCCESS. Otherwise, an
 *     error result is returned to the caller.
 */
globus_result_t
globus_net_manager_attr_array_from_string(
    globus_net_manager_attr_t         **attr,
    const char                         *scope,
    const char                         *attr_string)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_list_t                      *attr_string_list = NULL;
    int                                 attr_string_list_size = 0;
    globus_net_manager_attr_t          *attr_array = NULL;
    int                                 i = 0;
    
    if (attr == NULL)
    {
        result = GlobusNetManagerErrorParameter("NULL attr.");
        goto bad_attr_param;
    }
    if (scope == NULL || attr_string == NULL)
    {
        result = GlobusNetManagerErrorParameter("NULL scope or attr_string.");
        goto bad_param;
    }
    if (strchr(attr_string, '\r') || strchr(attr_string, '\n'))
    {
        result = GlobusNetManagerErrorParameter(
            "CR and LF are not allowed in attr_string.");
        goto illegal_string;
    }
    attr_string_list = globus_list_from_string(attr_string, ';', NULL);
    if (attr_string_list == NULL)
    {
        if (strlen(attr_string) > 0)
        {
            result = GlobusNetManagerErrorParameter("Invalid attr_string.");
        }
        goto no_attrs;
    }
    attr_string_list_size = globus_list_size(attr_string_list);
    attr_array = calloc(
            attr_string_list_size + 1,
            sizeof(globus_net_manager_attr_t));
    if (attr_array == NULL)
    {
        result = GlobusNetManagerErrorMemory("attr_array");
        goto attr_array_malloc_fail;
    }

    while (!globus_list_empty(attr_string_list))
    {
        char                           *attr_name = NULL;
        char                           *attr_value = NULL;

        attr_name = globus_list_remove(
                &attr_string_list, attr_string_list);
        if (*attr_name  == '\0')
        {
            attr_string_list_size--;
            attr_array[attr_string_list_size - 1] =
                    globus_net_manager_null_attr;
            free(attr_name);
            continue;
        }
        attr_value = strchr(attr_name, '=');
        if (!attr_value)
        {
            free(attr_name);
            result = GlobusNetManagerErrorParameter("Invalid attr_string.");
            goto bad_attr;
        }
        *attr_value++ = '\0';
        if (*attr_value == '\0')
        {
            free(attr_name);
            result = GlobusNetManagerErrorParameter("Empty value in attr_string.");
            goto bad_value;
        }
        attr_array[attr_string_list_size - i - 1].scope = strdup(scope);
        if (attr_array[attr_string_list_size - i - 1].scope == NULL)
        {
            free(attr_name);
            result = GlobusNetManagerErrorMemory("scope");
            goto strdup_scope_fail;
        }
        attr_array[attr_string_list_size - i - 1].name = attr_name;
        attr_array[attr_string_list_size - i - 1].value = strdup(attr_value);
        if (attr_array[attr_string_list_size - i - 1].value == NULL)
        {
            result = GlobusNetManagerErrorMemory("attr_value");
            free(attr_array[attr_string_list_size - i - 1].scope);
            free(attr_array[attr_string_list_size - i - 1].name);
            free(attr_array[attr_string_list_size - i - 1].value);
            goto strdup_value_fail;
        }
        i++;
    }

strdup_value_fail:
strdup_scope_fail:
bad_value:
bad_attr:
    attr_array[i] = globus_net_manager_null_attr;
    if (result)
    {
        if (i > 0)
        {
            for (int j = i - 1; j >= 0; j--)
            {
                free(attr_array[j].scope);
                free(attr_array[j].name);
                free(attr_array[j].value);
            }
        }
        free(attr_array);
        attr_array = NULL;
    }
attr_array_malloc_fail:
    globus_list_destroy_all(attr_string_list, free);
no_attrs:
illegal_string:
bad_param:
    *attr = attr_array;
bad_attr_param:
    return result;
}
/* globus_net_manager_attr_array_from_string() */
