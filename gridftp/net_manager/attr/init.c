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
 * @file init.c
 * @brief globus_net_manager_attr_init()
 */

#include "globus_net_manager_attr.h"
#include "globus_net_manager.h"

/**
 * @brief Initialize the contents of an attribute
 * @ingroup globus_net_manager_attr
 * @details
 * This function initializes <b>attr</b> and with copies of the values
 * passed as the <b>scope</b>, <b>name</b>, and <b>value</b> parameters.
 *
 * @param[out] attr
 *     Pointer to the attribute to initialize.
 * @param[in] scope
 *     Attribute scope
 * @param[in] name
 *     Attribute name
 * @param[in] value
 *     Attribute value
 *
 * @return
 *     On error, 'attr' is set to NULL and this function returns
 *     an error object. Otherwise this function returns 'GLOBUS_SUCCESS'
 */
globus_result_t
globus_net_manager_attr_init(
    globus_net_manager_attr_t          *attr,
    const char                         *scope,
    const char                         *name,
    const char                         *value)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_net_manager_attr_t           new_attr = GLOBUS_NET_MANAGER_NULL_ATTR;

    if ((!attr) || (!scope) || (!name) || (!value))
    {
        result = GlobusNetManagerErrorParameter("No parameter may be NULL.");
        goto null_attr;
    }
    new_attr.scope = strdup(scope);
    if (!new_attr.scope)
    {
        result = GlobusNetManagerErrorMemory("scope");
        goto scope_strdup_fail;
    }
    new_attr.name = strdup(name);
    if (!new_attr.name)
    {
        result = GlobusNetManagerErrorMemory("name");
        goto name_strdup_fail;
    }
    new_attr.value = strdup(value);
    if (!new_attr.value)
    {
        result = GlobusNetManagerErrorMemory("value");
        goto value_strdup_fail;
    }

    *attr = new_attr;

    if (result)
    {
value_strdup_fail:
        free(new_attr.name);
name_strdup_fail:
        free(new_attr.scope);
    }
scope_strdup_fail:
null_attr:
    return result;
}
/* globus_net_manager_attr_init() */
