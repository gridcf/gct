/*
 * Copyright 1999-2009 University of Chicago
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
 * @file globus_gram_job_manager_rsl.c
 * @brief Job Manager RSL Manipulations
 */
#include "globus_gram_job_manager.h"
#include "globus_rsl_assist.h"

#include <string.h>

/**
 * @defgroup globus_gram_job_manager_rsl Job Manager RSL Utilities
 * @ingroup globus_gram_job_manager
 * @brief Job Manager RSL Utilities
 */
#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/* Module Specific Prototypes */
static
int
globus_l_gram_job_manager_rsl_match(
    void *                              datum,
    void *                              arg);

#endif

/**
 * @brief Merge two sets of RSL relations
 * @ingroup globus_gram_job_manager_rsl
 * @details
 * Create a new RSL consisting of the merging of the base_rsl and override_rsl.
 * The result after completion is one RSL containing all of the relations
 * from the base_rsl and the override_rsl with any conflicting definitions
 * resolved by the override_rsl winning. The base_rsl and override_rsl
 * are unmodified in this process.
 */
globus_rsl_t *
globus_gram_job_manager_rsl_merge(
    globus_rsl_t *                      base_rsl,
    globus_rsl_t *                      override_rsl)
{
    globus_rsl_t *                      tmp;
    globus_list_t **                    base_relations;
    globus_list_t *                     override_relations;
    globus_rsl_t *                      result;
    char *                              attribute;
    globus_list_t *                     node;

    globus_assert(globus_rsl_is_boolean_and(base_rsl));
    globus_assert(globus_rsl_is_boolean_and(override_rsl));

    result = globus_rsl_copy_recursive(base_rsl);

    base_relations = globus_rsl_boolean_get_operand_list_ref(result);
    override_relations = globus_rsl_boolean_get_operand_list(override_rsl);

    while(!globus_list_empty(override_relations))
    {
        tmp = globus_list_first(override_relations);
        override_relations = globus_list_rest(override_relations);
        attribute = globus_rsl_relation_get_attribute(tmp);

        node = globus_list_search_pred(*base_relations,
                                       globus_l_gram_job_manager_rsl_match,
                                       attribute);
        if(node)
        {
            globus_rsl_free_recursive(globus_list_remove(base_relations, node));
        }
        globus_list_insert(base_relations, globus_rsl_copy_recursive(tmp));
    }
    return result;
}
/* globus_l_gram_job_manager_rsl_merge() */

/**
 * @brief Attribute exists in an RSL 
 * @ingroup globus_gram_job_manager_rsl
 * @details
 * Check to see if an RSL attribute exists in the given RSL.
 * @param rsl
 *     RSL parse tree to search
 * @param attribute
 *     Attribute name to search for.
 *
 * @retval GLOBUS_TRUE
 *     Attribute exists in the RSL.
 * @retval GLOBUS_FALSE
 *     Attribute does not exist in the RSL.
 */
globus_bool_t
globus_gram_job_manager_rsl_attribute_exists(
    globus_rsl_t *                      rsl,
    const char *                        attribute)
{
    globus_list_t *                     attributes;

    attributes = globus_rsl_boolean_get_operand_list(rsl);

    return globus_list_search_pred(
            attributes,
            globus_l_gram_job_manager_rsl_match,
            (void *) attribute) ? GLOBUS_TRUE : GLOBUS_FALSE;
}

globus_bool_t
globus_gram_job_manager_rsl_need_stage_in(
    globus_gram_jobmanager_request_t *  request)
{
    globus_list_t *                     attributes;
    globus_list_t *                     node;
    char *                              value;
    globus_url_t                        url;
    int                                 i;
    char *                              can_stage[] =
                                        { GLOBUS_GRAM_PROTOCOL_STDIN_PARAM,
                                          GLOBUS_GRAM_PROTOCOL_EXECUTABLE_PARAM,
                                          NULL
                                        };

    attributes = globus_rsl_boolean_get_operand_list(request->rsl);

    if(globus_list_search_pred(attributes,
                               globus_l_gram_job_manager_rsl_match,
                               GLOBUS_GRAM_PROTOCOL_FILE_STAGE_IN_PARAM))
    {
        return GLOBUS_TRUE;
    }
    else if(globus_list_search_pred(
                attributes,
                globus_l_gram_job_manager_rsl_match,
                GLOBUS_GRAM_PROTOCOL_FILE_STAGE_IN_SHARED_PARAM))
    {
        return GLOBUS_TRUE;
    }
    else
    {
        for(i = 0; can_stage[i] != NULL; i++)
        {
            node = globus_rsl_param_get_values(
                request->rsl,
                can_stage[i]);

            if(node)
            {
                value =
                    globus_rsl_value_literal_get_string(
                            globus_list_first(node));

                if(globus_url_parse(value, &url) == 0)
                {
                    if(url.scheme_type != GLOBUS_URL_SCHEME_FILE)
                    {
                        globus_url_destroy(&url);
                        return GLOBUS_TRUE;
                    }
                    else
                    {
                        globus_url_destroy(&url);
                    }
                }
            }
        }
    }
    return GLOBUS_FALSE;
}
/* globus_gram_job_manager_rsl_need_stage_in() */

globus_bool_t
globus_gram_job_manager_rsl_need_stage_out(
    globus_gram_jobmanager_request_t *  request)
{
    globus_list_t *                     attributes;

    attributes = globus_rsl_boolean_get_operand_list(request->rsl);

    if(globus_list_search_pred(attributes,
                               globus_l_gram_job_manager_rsl_match,
                               GLOBUS_GRAM_PROTOCOL_FILE_STAGE_OUT_PARAM))
    {
        return GLOBUS_TRUE;
    }
    else if (globus_list_search_pred(
                attributes,
                globus_l_gram_job_manager_rsl_match,
                "filestreamout"))
    {
        return GLOBUS_TRUE;
    }
    else
    {
        return GLOBUS_FALSE;
    }
}
/* globus_gram_job_manager_rsl_need_stage_out() */

globus_bool_t
globus_gram_job_manager_rsl_need_file_cleanup(
    globus_gram_jobmanager_request_t *  request)
{
    globus_list_t *                     attributes;

    attributes = globus_rsl_boolean_get_operand_list(request->rsl);

    if(globus_list_search_pred(attributes,
                               globus_l_gram_job_manager_rsl_match,
                               GLOBUS_GRAM_PROTOCOL_FILE_CLEANUP_PARAM))
    {
        return GLOBUS_TRUE;
    }
    else
    {
        return GLOBUS_FALSE;
    }
}
/* globus_gram_job_manager_rsl_need_file_cleanup() */

globus_bool_t
globus_gram_job_manager_rsl_need_scratchdir(
    globus_gram_jobmanager_request_t *  request)
{
    globus_list_t *                     attributes;

    attributes = globus_rsl_boolean_get_operand_list(request->rsl);

    if(globus_list_search_pred(attributes,
                               globus_l_gram_job_manager_rsl_match,
                               GLOBUS_GRAM_PROTOCOL_SCRATCHDIR_PARAM))
    {
        return GLOBUS_TRUE;
    }
    else
    {
        return GLOBUS_FALSE;
    }
}
/* globus_gram_job_manager_rsl_need_scratchdir() */

globus_bool_t
globus_gram_job_manager_rsl_need_restart(
    globus_gram_jobmanager_request_t *  request)
{
    return globus_gram_job_manager_rsl_attribute_exists(
            request->rsl, 
            GLOBUS_GRAM_PROTOCOL_RESTART_PARAM);
}
/* globus_gram_job_manager_rsl_need_restart() */

globus_rsl_t *
globus_gram_job_manager_rsl_extract_relation(
    globus_rsl_t *                      rsl,
    const char *                        attribute)
{
    globus_list_t **                    operand_ref;
    globus_list_t *                     node;

    if(! globus_rsl_is_boolean_and(rsl))
    {
        return GLOBUS_NULL;
    }
    operand_ref = globus_rsl_boolean_get_operand_list_ref(rsl);
    node = globus_list_search_pred(*operand_ref,
                                   globus_l_gram_job_manager_rsl_match,
                                   (void *) attribute);
    if(node)
    {
        globus_rsl_t *                  relation;

        relation = globus_list_remove(operand_ref, node);

        return relation;
    }
    return GLOBUS_NULL;
}
/* globus_gram_job_manager_rsl_extract_relation() */

int
globus_gram_job_manager_rsl_add_relation(
    globus_rsl_t *                      rsl,
    globus_rsl_t *                      relation)
{
    globus_list_t **                    operand_ref;
    int                                 rc = GLOBUS_SUCCESS;

    if(! globus_rsl_is_boolean_and(rsl))
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
        goto nonboolean;
    }
    operand_ref = globus_rsl_boolean_get_operand_list_ref(rsl);
    if (operand_ref == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
        goto no_operands;
    }
    rc = globus_list_insert(operand_ref, relation);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto insert_failed;
    }

insert_failed:
no_operands:
nonboolean:
    return rc;
}
/* globus_gram_job_manager_rsl_add_relation() */

/**
 * @brief Add an output attribute to an RSL
 * @ingroup globus_gram_job_manager_rsl
 * @details
 * This function modifies the @a rsl parameter, adding a new relation of the
 * form:
 * @a attribute = ( @a value )
 * This function assumes that the specified attribute is not present in the
 * RSL when called. Unlike the globus_rsl library, this function copies the
 * @a attribute and @a value strings as needed to keep it so the RSL can
 * be freed by calling globus_rsl_free_recursive().
 * 
 * @param request
 *     Job request
 * @param rsl
 *     RSL to modify
 * @param attribute
 *     Attribute (either stdout or stderr)
 * @param value
 *     Local output path
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed
 */
int
globus_gram_rsl_add_output(
    globus_gram_jobmanager_request_t *  request,
    globus_rsl_t *                      rsl,
    const char *                        attribute,
    const char *                        value)
{
    globus_rsl_t *                      relation;
    char *                              attr_copy;
    char *                              value_copy;
    globus_list_t *                     value_list = NULL;
    globus_rsl_value_t *                value_literal;
    globus_rsl_value_t *                value_sequence;
    int                                 rc = GLOBUS_SUCCESS;

    attr_copy = strdup(attribute);
    if (attr_copy == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto attr_copy_failed;
    }

    value_copy = strdup(value);
    if (value_copy == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto value_copy_failed;
    }

    value_literal = globus_rsl_value_make_literal(
            value_copy);
    if (value_literal == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto make_literal_failed;
    }

    rc = globus_list_insert(&value_list, value_literal);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto list_insert_failed;
    }

    value_sequence = globus_rsl_value_make_sequence(value_list);
    if (value_sequence == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto make_sequence_failed;
    }


    relation = globus_rsl_make_relation(
            GLOBUS_RSL_EQ,
            attr_copy,
            value_sequence);

    if (relation == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto make_relation_failed;
    }

    rc = globus_gram_job_manager_rsl_add_relation(
            request->rsl,
            relation);

    if (rc != GLOBUS_SUCCESS)
    {
        goto add_relation_failed;
    }

    if (rc != GLOBUS_SUCCESS)
    {
add_relation_failed:
make_relation_failed:
        globus_rsl_value_free(value_sequence);
make_sequence_failed:
        globus_list_free(value_list);
list_insert_failed:
        globus_rsl_value_free(value_literal);
make_literal_failed:
        free(value_copy);
value_copy_failed:
        free(attr_copy);
attr_copy_failed:
        ;
    }
    return rc;
}
/* globus_gram_rsl_add_output() */

/**
 * @brief Add a stage out value to the RSL
 * @ingroup globus_gram_job_manager_rsl
 * @details
 * Creates a new entry in the RSL's filestreamout value list for the
 * given (@a source, @a destination) pair. If the RSL does not contain
 * filestageout, it is added; otherwise, the new pair is prepended to the
 * existing list.
 *
 * @param request
 *     Job request 
 * @param rsl
 *     RSL to modify
 * @param source
 *     Source URL
 * @param destination
 *     Destination URL
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED
 *     Malloc failed
 */
int
globus_gram_rsl_add_stream_out(
    globus_gram_jobmanager_request_t *  request,
    globus_rsl_t *                      rsl,
    const char *                        source,
    const char *                        destination)
{
    globus_list_t **                    operand_ref;
    globus_list_t *                     node;
    char *                              attr_copy;
    char *                              source_copy;
    char *                              dest_copy;
    int                                 rc = GLOBUS_SUCCESS;
    globus_rsl_t *                      relation;
    globus_rsl_value_t *                value_sequence;
    globus_rsl_value_t *                source_literal;
    globus_rsl_value_t *                dest_literal;
    globus_list_t *                     file_stage_out_pair;
    globus_list_t **                    file_stage_out_pair_end;
    globus_rsl_value_t *                new_stage_sequence;


    operand_ref = globus_rsl_boolean_get_operand_list_ref(rsl);
    node = globus_list_search_pred(
            *operand_ref,
            globus_l_gram_job_manager_rsl_match,
            (void *) "filestreamout");
    if (!node)
    {
        /* No file_stage_out in RSL, add a new empty one */
        attr_copy = strdup("filestreamout");
        if (attr_copy == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto attr_copy_failed;
        }

        value_sequence = globus_rsl_value_make_sequence(NULL);
        if (value_sequence == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto make_value_sequence_failed;
        }

        relation = globus_rsl_make_relation(
                GLOBUS_RSL_EQ,
                attr_copy,
                value_sequence);
        if (relation == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto make_value_relation_failed;
        }

        rc = globus_list_insert(operand_ref, relation);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto filestageout_insert_failed;
        }

        if (rc != GLOBUS_SUCCESS)
        {
filestageout_insert_failed:
            globus_rsl_free(relation);
make_value_relation_failed:
            globus_rsl_value_free(value_sequence);
make_value_sequence_failed:
            free(attr_copy);
attr_copy_failed:
            goto bad_relation;
        }
    }
    else
    {
        /* Adding new value to existing filestageout */
        relation = globus_list_first(node);
    }

    /*
     * Now we're going to create a value sequence to append to the relation's
     * value sequence
     */
    source_copy = strdup(source);
    if (source_copy == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto source_copy_failed;
    }
    dest_copy = strdup(destination);
    if (dest_copy == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto dest_copy_failed;
    }
    source_literal = globus_rsl_value_make_literal(source_copy);
    if (source_literal == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto source_literal_failed;
    }
    dest_literal = globus_rsl_value_make_literal(dest_copy);
    if (dest_literal == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto dest_literal_failed;
    }

    file_stage_out_pair = NULL;
    file_stage_out_pair_end = &file_stage_out_pair;

    rc = globus_list_insert(
            file_stage_out_pair_end,
            source_literal);

    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto add_source_literal_failed;
    }

    file_stage_out_pair_end = globus_list_rest_ref(*file_stage_out_pair_end);

    rc = globus_list_insert(
            file_stage_out_pair_end,
            dest_literal);
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto add_dest_literal_failed;
    }

    new_stage_sequence = globus_rsl_value_make_sequence(file_stage_out_pair);
    if (new_stage_sequence == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto make_new_stage_sequence_failed;

    }

    rc = globus_list_insert(
            globus_rsl_value_sequence_get_list_ref(
                     globus_rsl_relation_get_value_sequence(relation)),
                     new_stage_sequence);
    if (rc != GLOBUS_SUCCESS)
    {
        goto insert_new_stage_sequence_failed;
    }

    if (rc != GLOBUS_SUCCESS)
    {
insert_new_stage_sequence_failed:
        globus_rsl_value_free(new_stage_sequence);
make_new_stage_sequence_failed:
add_dest_literal_failed:
        globus_list_free(file_stage_out_pair);
add_source_literal_failed:
        globus_rsl_value_free(dest_literal);
dest_literal_failed:
        globus_rsl_value_free(source_literal);
source_literal_failed:
        free(dest_copy);
dest_copy_failed:
        free(source_copy);
    }
source_copy_failed:
bad_relation:
    return rc;
}
/* globus_gram_rsl_add_stream_out() */

/**
 * @brief Add an environment variable to the job RSL
 * @ingroup globus_gram_job_manager_rsl
 * @details
 * This function adds a single environment variable to the job RSL. If
 * there is no environment relation in the RSL, then one is added. Both
 * the variable name and value are copied into the RSL, so the original
 * values passed in may be static strings or pointers to data which is
 * freed or overwritten once this function returns.
 *
 * @param ast_node
 *        A pointer to the RSL tree to update. This should point to the
 *        root of the rsl tree (the boolean &) on the invocation of the
 *        function, but will point to various relations in the RSL as
 *        it calls itself recursively.
 * @param var
 *        A pointer to a string containing the variable to be added to
 *        the RSL. No checking is done to see if this environment variable
 *        is already defined in the RSL. This will be duplicated and inserted
 *        into the RSL.
 * @param value
 *        The value of the environment variable named @a var. This will
 *        be duplicated and inserted into the RSL.
 * 
 * @retval 0
 *         The environment variable was added to the RSL.
 * @retval 1
 *         The @a ast_node points to a relation other than an environment
 *         relation
 * @retval 2
 *         The @a ast_node points to some unexpected part of the RSL.
 *
 * @todo Remove old values of @a var if it is already in the RSL's
 *       environment attribute's value.
 */
int
globus_gram_job_manager_rsl_env_add(
    globus_rsl_t *                      ast_node,
    const char *                        var,
    const char *                        value)
{
    globus_rsl_t *                      tmp_rsl_ptr;
    globus_list_t *                     tmp_rsl_list;
    globus_list_t *                     new_list;
    char *                              tmp_rsl_str;
    int                                 rc;

    if (globus_rsl_is_boolean(ast_node))
    {
        tmp_rsl_list = globus_rsl_boolean_get_operand_list(ast_node);

        while (! globus_list_empty(tmp_rsl_list))
        {
            tmp_rsl_ptr = (globus_rsl_t *) globus_list_first
                 (tmp_rsl_list);

            rc = globus_gram_job_manager_rsl_env_add(
                    tmp_rsl_ptr,
                    var,
                    value);
            if(rc == 0)
            {
                return rc;
            }

            tmp_rsl_list = globus_list_rest(tmp_rsl_list);

        }
        /* Didn't find environment in the RSL: add it! */
        tmp_rsl_str = malloc(
                    strlen("environment = (%s %s)") +
                    strlen(var) +
                    strlen(value));

        sprintf(tmp_rsl_str, "environment = (%s %s)", var, value);
        tmp_rsl_ptr = globus_rsl_parse(tmp_rsl_str);

        free(tmp_rsl_str);

        globus_list_insert(
                globus_rsl_boolean_get_operand_list_ref(ast_node),
                tmp_rsl_ptr);

        return 0;
    }
    else if (globus_rsl_is_relation(ast_node))
    {
        if (!globus_rsl_is_relation_attribute_equal(ast_node, "environment"))
        {
            return(1);
        }

        new_list = NULL;

        globus_list_insert(&new_list, (void *)
            globus_rsl_value_make_literal(strdup(value)));

        globus_list_insert(&new_list, (void *)
            globus_rsl_value_make_literal(strdup(var)));

        globus_list_insert(
            globus_rsl_value_sequence_get_list_ref(
                 globus_rsl_relation_get_value_sequence(ast_node)),
                 (void *) globus_rsl_value_make_sequence(new_list));

        return(0);
    }
    else
    {
        return(2);
    }
}
/* globus_gram_job_manager_rsl_env_add() */

/**
 * @brief Remove an RSL attribute
 * @ingroup globus_gram_job_manager_rsl
 * @details
 * Remove an RSL attribute from and RSL tree.
 * @param rsl
 *        The RSL parse tree to modify
 * @param attribute
 *        The name of the attribute to remove from the RSL. The
 *        attribute and it's values will be freed.
 *
 * @retval GLOBUS_SUCCESS
 *         Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL
 *         Bad RSL
 */
int
globus_gram_job_manager_rsl_remove_attribute(
    globus_rsl_t *                      rsl,
    char *                              attribute)
{
    globus_list_t **                    operand_ref;
    globus_list_t *                     node;

    if(! globus_rsl_is_boolean_and(rsl))
    {
        return GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
    }
    operand_ref = globus_rsl_boolean_get_operand_list_ref(rsl);
    node = globus_list_search_pred(*operand_ref,
                                   globus_l_gram_job_manager_rsl_match,
                                   attribute);
    if(node)
    {
        globus_rsl_free_recursive(globus_list_remove(operand_ref, node));
    }
    return GLOBUS_SUCCESS;
}
/* globus_gram_job_manager_rsl_remove_attribute() */

/**
 * Evaluate RSL substitutions for a single RSL attribute
 *
 * Evaluates the value of the named RSL attribute. If it is present, and
 * the evaluation succeeds, then the @a value parameter is modified to
 * contain a copy of the newly-evaluated value of the attribute. 
 *
 * @param request
 *        The request containing the RSL tree to evaluate.
 * @param attribute
 *        The name of the attribute to evaluate.
 * @param value
 *        A pointer to a char * which will be filled with a copy
 *        of the evaluated value of the RSL attribute. If the attribute
 *        is not found, or an error occurs, this will be set to NULL.
 *
 * @retval GLOBUS_SUCCESS
 *         The RSL attribute is present and has evaluated successfully.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL
 *         The RSL is not a boolean tree containing attributes.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_EVALUATION_FAILED
 *         The RSL attribute is present but some RSL substitution
 *         could not be evaluated for that attribute.
 */
int
globus_gram_job_manager_rsl_eval_one_attribute(
    globus_gram_jobmanager_request_t *  request,
    char *                              attribute,
    char **                             value)
{
    globus_list_t *                     attributes;
    globus_list_t *                     node;
    globus_rsl_t *                      attribute_rsl = GLOBUS_NULL;
    char *                              single_value;
    int                                 rc = GLOBUS_SUCCESS;

    *value = GLOBUS_NULL;

    attributes = globus_rsl_boolean_get_operand_list(request->rsl);
    if (attributes == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        goto bad_operand_list;
    }

    node = globus_list_search_pred(
            attributes,
            globus_l_gram_job_manager_rsl_match,
            attribute);

    if (!node)
    {
        goto no_match;
    }

    attribute_rsl = globus_list_first(node);

    if (!attribute_rsl)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        goto no_attribute;
    }

    rc = globus_rsl_eval(attribute_rsl, &request->symbol_table);

    if(rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_RSL_EVALUATION_FAILED;
        goto rsl_eval_failed;
    }

    single_value = globus_rsl_value_literal_get_string(
            globus_rsl_relation_get_single_value(
                    attribute_rsl));
    if (single_value)
    {
        *value = strdup(single_value);

        if (*value == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            goto value_strdup_failed;
        }
    }
    else
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
    }

value_strdup_failed:
rsl_eval_failed:
no_attribute:
no_match:
bad_operand_list:
    return rc;
}
/* globus_gram_job_manager_eval_one_attribute() */

int
globus_gram_job_manager_rsl_parse_value(
    char *                              value_string,
    globus_rsl_value_t **               rsl_value)
{
    char *                              rsl_spec = NULL;
    char *                              format = "x = %s\n";
    globus_rsl_t *                      rsl;
    globus_rsl_value_t *                values;
    int                                 rc = GLOBUS_SUCCESS;

    rsl_spec = malloc(strlen(format) + strlen(value_string) + 1);

    if (rsl_spec == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto out;
    }
    sprintf(rsl_spec, format, value_string);
    rsl = globus_rsl_parse(rsl_spec);
    if (rsl == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        goto free_rsl_spec_out;
    }

    values = globus_list_first(
            globus_rsl_value_sequence_get_value_list(
                globus_rsl_relation_get_value_sequence(rsl)));
    if (values == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;

        goto free_rsl_out;
    }
    *rsl_value = globus_rsl_value_copy_recursive(values);
    if (*rsl_value == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto free_rsl_out;
    }

free_rsl_out:
    globus_rsl_free_recursive(rsl);

free_rsl_spec_out:
    free(rsl_spec);

out:
    return rc;
}
/* globus_gram_job_manager_rsl_parse_value() */

int
globus_gram_job_manager_rsl_evaluate_value(
    globus_symboltable_t *              symbol_table,
    globus_rsl_value_t *                value,
    char **                             value_string)
{
    globus_rsl_value_t *                copy;
    int                                 rc = GLOBUS_SUCCESS;

    *value_string = NULL;

    copy = globus_rsl_value_copy_recursive(value);
    if (copy == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto out;
    }

    if(globus_rsl_value_is_literal(copy))
    {
        *value_string =
            strdup(globus_rsl_value_literal_get_string(copy));

        if (*value_string == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto free_copy_out;
        }
    }
    else
    {
        rc = globus_rsl_value_eval(
                copy,
                symbol_table,
                value_string,
                0);
    }

free_copy_out:
    globus_rsl_value_free_recursive(copy);

out:

    return rc;
}
/* globus_gram_job_manager_rsl_evaluate_value() */

int
globus_gram_job_manager_rsl_eval_string(
    globus_symboltable_t *              symbol_table,
    const char *                        string,
    char **                             value_string)
{
    globus_rsl_value_t *                value;
    int                                 rc;

    *value_string = NULL;

    rc = globus_gram_job_manager_rsl_parse_value(
            (char *) string,
            &value);

    if(rc != GLOBUS_SUCCESS)
    {
        goto parse_failed;
    }

    rc = globus_gram_job_manager_rsl_evaluate_value(
            symbol_table,
            value,
            value_string);

    if(rc != GLOBUS_SUCCESS || (*value_string) == NULL)
    {
        goto eval_failed;
    }

eval_failed:
    globus_rsl_value_free_recursive(value);
parse_failed:

    return rc;
}
/* globus_gram_job_manager_rsl_eval_string() */

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
static
int
globus_l_gram_job_manager_rsl_match(
    void *                              datum,
    void *                              arg)
{
    globus_rsl_t *                      relation = datum;
    char *                              attribute = arg;
    char *                              test;

    if(!globus_rsl_is_relation(relation))
    {
        return GLOBUS_FALSE;
    }

    test = globus_rsl_relation_get_attribute(relation);

    return (strcmp(test, attribute)==0);
}
/* globus_l_gram_job_manager_rsl_match() */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

/**
 * Search the RSL tree for an attribute and return its single value
 *
 * @param rsl
 *     RSL tree to search
 * @param attribute
 *     Attribute name to search for
 * @param value_ptr
 *     Pointer to set to the value of this attribute. Must not be freed
 *     by the caller. Will be set to NULL if the attribute is not present or
 *     does not have a literal string value.
 *
 * @retval GLOBUS_SUCCESS
 *     Success.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_UNDEFINED_ATTRIBUTE
 *     Attribute not found.
 */
int
globus_gram_job_manager_rsl_attribute_get_string_value(
    globus_rsl_t *                      rsl,
    const char *                        attribute,
    const char **                       value_ptr)
{
    globus_list_t *                     attributes;
    globus_list_t *                     node;
    int                                 rc = GLOBUS_SUCCESS;

    attributes = globus_rsl_boolean_get_operand_list(rsl);

    node = globus_list_search_pred(
            attributes,
            globus_l_gram_job_manager_rsl_match,
            (void *) attribute);
    if (node == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_UNDEFINED_ATTRIBUTE;

        goto search_failed;
    }
    *value_ptr = globus_rsl_value_literal_get_string(
            globus_rsl_relation_get_single_value(
                    globus_list_first(node)));

search_failed:
    if (rc != GLOBUS_SUCCESS)
    {
        *value_ptr = NULL;
    }
    return rc;
}
/* globus_gram_job_manager_rsl_attribute_get_string_value() */

/**
 * Search the RSL tree for an attribute and return its boolean value
 *
 * @param rsl
 *     RSL tree to search
 * @param attribute
 *     Attribute name to search for
 * @param value_ptr
 *     Pointer to set to the value of this attribute. 
 *
 * @retval GLOBUS_SUCCESS
 *     Success.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_UNDEFINED_ATTRIBUTE;
 *     Attribute not found.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL
 *     Value is non-literal or has a non-boolean value.
 */
int
globus_gram_job_manager_rsl_attribute_get_boolean_value(
    globus_rsl_t *                      rsl,
    const char *                        attribute,
    globus_bool_t *                     value_ptr)
{
    const char *                        s;
    int                                 rc = GLOBUS_SUCCESS;

    rc = globus_gram_job_manager_rsl_attribute_get_string_value(
            rsl,
            attribute,
            &s);
    if (rc != GLOBUS_SUCCESS)
    {
        goto get_literal_failed;
    }
    if (s && strcmp(s, "yes") == 0)
    {
        *value_ptr = GLOBUS_TRUE;
    }
    else if (s && strcmp(s, "no") == 0)
    {
        *value_ptr = GLOBUS_FALSE;
    }
    else
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
get_literal_failed:
        *value_ptr = GLOBUS_FALSE;
    }

    return rc;
}
/* globus_gram_job_manager_rsl_attribute_get_boolean_value() */

/**
 * Search the RSL tree for an attribute and return its integer value
 *
 * @param rsl
 *     RSL tree to search
 * @param attribute
 *     Attribute name to search for
 * @param value_ptr
 *     Pointer to set to the value of this attribute. 
 *
 * @retval GLOBUS_SUCCESS
 *     Success.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_UNDEFINED_ATTRIBUTE;
 *     Attribute not found.
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL
 *     Value is non-literal or has a non-int value.
 */
int
globus_gram_job_manager_rsl_attribute_get_int_value(
    globus_rsl_t *                      rsl,
    const char *                        attribute,
    int *                               value_ptr)
{
    const char *                        s;
    char *                              end;
    int                                 rc = GLOBUS_SUCCESS;

    rc = globus_gram_job_manager_rsl_attribute_get_string_value(
            rsl,
            attribute,
            &s);

    if (rc != GLOBUS_SUCCESS)
    {
        goto get_literal_failed;
    }
    else if (s == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
        goto get_literal_failed;
    }

    errno = 0;
    *value_ptr = strtol(s, &end, 10);
    if (errno != 0 || strlen(end) != 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
get_literal_failed:
        *value_ptr = 0;
    }

    return rc;
}
/* globus_gram_job_manager_rsl_attribute_get_int_value() */
