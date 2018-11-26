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
 * @file array_delete.c
 * @brief globus_net_manager_attr_array_delete()
 */

#include "globus_net_manager_attr.h"
#include "globus_net_manager.h"


/**
 * @brief End of array value
 * @ingroup globus_net_manager_attr
 * @details
 * This value may be assigned to an element in an array of
 * Network Manager attributes to terminate the array.
 */
const
globus_net_manager_attr_t
globus_net_manager_null_attr = GLOBUS_NET_MANAGER_NULL_ATTR;

/**
 * @brief Destroy an array of Network Manager attributes
 * @ingroup globus_net_manager_attr
 * @details
 * This function deletes an array of Network Manager attributes and
 * all values contained within them. The array must be terminated by
 * the value #GLOBUS_NET_MANAGER_NULL_ATTR.
 *
 * @param[in] attrs
 *     A pointer to an array of attributes to be freed.
 */
void
globus_net_manager_attr_array_delete(
    globus_net_manager_attr_t          *attrs)
{
    if (attrs)
    {
        for (int i = 0; attrs[i].scope || attrs[i].name || attrs[i].value; i++)
        {
            globus_net_manager_attr_destroy(&attrs[i]);
        }
        free(attrs);
    }
    return;
}
/* globus_net_manager_attr_array_delete() */
