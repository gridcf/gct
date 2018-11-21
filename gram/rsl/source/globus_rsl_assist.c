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


/**
 * @defgroup globus_rsl_assist RSL Helper Functions
 * @ingroup globus_rsl
 * @brief RSL Helper Functions
 *
 * @details
 *   The rsl_assist library provide a set of functions to canonicalize
 *   RSL parse trees or strings.
 */

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_rsl_assist.c
 * @brief RSL Help Functions
 *
 * @ref globus_rsl_assist
 */
#endif /*GLOBUS_DONT_DOCUMENT_INTERNAL */

#include "globus_common.h"
#include "globus_rsl_assist.h"

#include <string.h>

/**
 * @brief Canonicalize all attribute names in an RSL parse tree
 * @ingroup globus_rsl_assist
 *
 * @details
 * The globus_rsl_assist_attributes_canonicalize() function performs an
 * in-place canonicalization of the RSL parse tree pointed to by its
 * @a rsl parameter. All relation attribute names will be changed so that
 * they lower-case, with all internal underscore characters removed.
 *
 * @param rsl
 *     Pointer to the RSL parse tree to canonicalize.
 *
 * @return
 *     If globus_rsl_assist_attributes_canonicalize() is successful, it will
 *     ensure that all attribute names in the given RSL will be in canonical
 *     form and return GLOBUS_SUCCESS. If an error occurs, it will return
 *     GLOBUS_FAILURE.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_FAILURE
 *     Failure
 */
int
globus_rsl_assist_attributes_canonicalize(globus_rsl_t * rsl)
{
 
    globus_list_t *             lists=GLOBUS_NULL;
    globus_rsl_t *              an_rsl;

    /*
     * if the request is a multirequest, run this function repeatedly
     * over the list of requests
     */
    if (globus_rsl_is_boolean_multi(rsl))
    {
        lists = (globus_list_t *) globus_rsl_boolean_get_operand_list(rsl);
        while (!globus_list_empty(lists))
        {
            an_rsl=globus_list_first(lists);
            if (globus_rsl_assist_attributes_canonicalize(an_rsl) 
                != GLOBUS_SUCCESS)
            {
                return GLOBUS_FAILURE;
            }
            lists=globus_list_rest(lists);
        }
        return GLOBUS_SUCCESS;
    }
    else if (globus_rsl_is_boolean(rsl))
    {
        lists = globus_rsl_boolean_get_operand_list(rsl);

        /* look at each operand of the boolean, and figure out if
         * it is a nested boolean, or a relation (x=y)
         */
        while(!globus_list_empty(lists))
        {
            an_rsl = globus_list_first(lists);

            /* if boolean, recursively process the request */
            if (globus_rsl_is_boolean(an_rsl))
            {
                if (globus_rsl_assist_attributes_canonicalize(an_rsl)
                       != GLOBUS_SUCCESS)
                {
                    return GLOBUS_FAILURE;
                }
            }
            else if (globus_rsl_is_relation(an_rsl))
            {
                globus_rsl_assist_string_canonicalize(
                       globus_rsl_relation_get_attribute(an_rsl));
            }
            lists = globus_list_rest(lists);
        }
        return GLOBUS_SUCCESS;
    }
    else if (globus_rsl_is_relation(rsl))
    {
        globus_rsl_assist_string_canonicalize(
               globus_rsl_relation_get_attribute(rsl));
    }
    else
    {
        return GLOBUS_FAILURE;
    }

    return GLOBUS_SUCCESS;

} /* globus_rsl_assist_attributes_canonicalize() */


/**
 * @brief Canonicalize an attribute name
 * @ingroup globus_rsl_assist
 *
 * @details
 * The globus_rsl_assist_string_canonicalize() function modifies the
 * NULL-terminated string pointed to by its @a ptr parameter so that it
 * is in canonical form. The canonical form is all lower-case with all
 * underscore characters removed.
 *
 * @param ptr
 *     Pointer to the RSL string to modify in place.
 *
 * @return void
 */
void
globus_rsl_assist_string_canonicalize(char * ptr)
{
    char * tmp_p;

    if (ptr == GLOBUS_NULL)
        return;

    for (tmp_p=ptr; *ptr != '\0'; ptr++)
    {
        if ( *ptr == '_' )
        {
            continue;
        }

        *tmp_p = tolower(*ptr);
        tmp_p++;
    }
    *tmp_p = '\0';

    return;

} /* globus_rsl_assist_string_canonicalize() */
