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
 * @brief globus_net_manager_attr_destroy()
 */

#include "globus_net_manager_attr.h"
#include "globus_net_manager.h"

/**
 * @brief Destroy the contents of an attribute
 * @ingroup globus_net_manager_attr
 * @details
 * This function frees the values contained in <b>attr</b> and reinitializes
 * them to NULL. It <em>does not</em> free attr itself.
 *
 * @param[in] attr
 *     Pointer to the attribute to destroy.
 */
void
globus_net_manager_attr_destroy(
    globus_net_manager_attr_t          *attr)
{
    if (attr)
    {
        free(attr->scope);
        free(attr->name);
        free(attr->value);
        *attr = globus_net_manager_null_attr;
    }
}
/* globus_net_manager_attr_destroy() */
