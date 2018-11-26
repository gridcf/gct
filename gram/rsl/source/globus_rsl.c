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

#include "globus_common.h"
#include <stdio.h>
#include "globus_rsl.h"
#include <strings.h>
#include "version.h"

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
static int
globus_i_rsl_value_unparse_to_fifo (globus_rsl_value_t * ast,
                                    globus_fifo_t      * bufferp);

static int
globus_i_rsl_unparse_to_fifo (globus_rsl_t  * ast,
                            globus_fifo_t * bufferp);

static int
globus_i_rsl_unparse_operator_to_fifo (int             operator,
                                       globus_fifo_t * bufferp);

static int
globus_i_rsl_unparse_string_literal_to_fifo (const char    * string,
                                             globus_fifo_t * bufferp);

globus_mutex_t globus_i_rsl_mutex;

/***************************************************************************
 *
 *
 *                        Module activation structure
 *
 *
 ****************************************************************************
 */
static int
globus_l_rsl_activate(void);

static int
globus_l_rsl_deactivate(void);

globus_module_descriptor_t              globus_i_rsl_module =
{
    "globus_rsl",
    globus_l_rsl_activate,
    globus_l_rsl_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};


/***************************************************************************
 *
 *                 globus_rsl module activation functions
 *
 ****************************************************************************
 */

static int
globus_l_rsl_activate(void)
{
    if( globus_module_activate(GLOBUS_COMMON_MODULE) != GLOBUS_SUCCESS )
    {
	return GLOBUS_FAILURE;
    }
    globus_mutex_init(&globus_i_rsl_mutex, NULL);

    return GLOBUS_SUCCESS;
}

static int
globus_l_rsl_deactivate(void)
{
    int  rc = GLOBUS_SUCCESS;

    globus_mutex_destroy(&globus_i_rsl_mutex);
    if( globus_module_deactivate(GLOBUS_COMMON_MODULE) != GLOBUS_SUCCESS )
    {
	rc = GLOBUS_FAILURE;
    }

    return rc;
}
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Globus RSL API
 * @copydoc globus_rsl
 */
#endif

/**
 * @defgroup globus_rsl Globus RSL
 * @brief Resource Specification Language
 *
 * @details
 * The Globus RSL library is provides the following functionality:
 * - @ref globus_rsl_predicates
 * - @ref globus_rsl_constructors
 * - @ref globus_rsl_memory
 * - @ref globus_rsl_accessor
 * - @ref globus_rsl_param
 * - @ref globus_rsl_print
 * - @ref globus_rsl_parse
 * - @ref globus_rsl_list
 */

/**
 * @defgroup globus_rsl_predicates RSL Predicates
 * @ingroup globus_rsl
 * @brief RSL Predicates
 *
 * The functions in this group return boolean values indicating whether
 * an RSL syntax tree is of a particular type.
 */


/**
 * @brief RSL relation test
 * @ingroup globus_rsl_predicates
 * @details
 * The globus_rsl_is_relation() function tests whether the 
 * the RSL pointed to by the @a ast parameter is a relation. The
 * RSL syntax supports the following relation operations:
 * <dl>
 *     <dt>=</dt>
 *     <dd>Equal</dd>
 *     <dt>!=</dt>
 *     <dd>Not Equal</dd>
 *     <dt>&gt;</dt>
 *     <dd>Greater Than</dd>
 *     <dt>&gt;=</dt>
 *     <dd>Greater Than or Equal</dd>
 *     <dt>&lt;</dt>
 *     <dd>Less Than</dd>
 *     <dt>&lt;=</dt>
 *     <dd>Less Than or Equal</dd>
 *     <dt>&lt;=</dt>
 *     <dd>Less Than or Equal</dd>
 * </dl>
 *
 * Some examples of RSL relations are 
 * @code
   "queue" = "debug"
   "queue" != "slow"
   "min_memory" > "1000"
   "max_wall_time" >= "60"
   "count < "10"
   "host_count" <= "5"
   @endcode
 *
 * @note GRAM only supports equality relations.
 *
 * @param ast
 *     Pointer to an RSL parse tree structure.
 *
 * @return
 *     The globus_rsl_is_relation() function returns GLOBUS_TRUE if
 *     the RSL parse tree pointed to by @a ast is a relation; otherwise,
 *     it returns GLOBUS_FALSE.
 */
int 
globus_rsl_is_relation (globus_rsl_t *ast)
{
    if (ast==NULL) return(0);

    if (ast->type == GLOBUS_RSL_RELATION)
       return(1);
    else
       return(0);
}

/**
 * @brief RSL boolean test
 * @ingroup globus_rsl_predicates
 *
 * @details
 * The globus_rsl_is_boolean() function tests whether the 
 * the RSL pointed to by the @a ast parameter is a boolean composition
 * of other RSL parse trees. The syntactically understood boolean
 * compositions are "&" (conjunction), "|" (disjunction), and "+"
 * (multi-request). Some bexamples of RSL booleans are
 *
 * @code
   & ( "queue" = "debug") ( "max_time" = "10000")
   | ("count" = "1")("count" = "10")
   + ( &("executable" = "1.exe") ) ( & ("executable" = "2.exe" )
   @endcode
 *
 * @param ast
 *     Pointer to an RSL parse tree structure.
 *
 * @return
 *     The globus_rsl_is_boolean() function returns GLOBUS_TRUE if
 *     the RSL parse tree pointed to by @a ast is a boolean composition;
 *     otherwise, it returns GLOBUS_FALSE.
 */
int
globus_rsl_is_boolean (globus_rsl_t *ast)
{
    if (ast==NULL) return(0);

    if (ast->type == GLOBUS_RSL_BOOLEAN)
       return(1);
    else
       return(0);
}

/**
 * @brief RSL equality operation test
 * @ingroup globus_rsl_predicates
 *
 * @details
 * The globus_rsl_is_relation_eq() function tests whether the 
 * the RSL pointed to by the @a ast parameter is an equality relation.
 * An example of an equality relation is
   @code
   "queue" = "debug"
   @endcode
 *
 * @param ast
 *     Pointer to an RSL parse tree structure.
 *
 * @return
 *     The globus_rsl_is_relation_eq() function returns GLOBUS_TRUE if
 *     the RSL parse tree pointed to by @a ast is an equality relation; 
 *     otherwise, it returns GLOBUS_FALSE.
 */
int 
globus_rsl_is_relation_eq (globus_rsl_t *ast)
{
    if (! globus_rsl_is_relation(ast)) return(0);

    if (ast->req.relation.my_operator == GLOBUS_RSL_EQ)
       return(1);
    else
       return(0);
}

/**
 * @brief RSL less than operation test
 * @ingroup globus_rsl_predicates
 * @details
 * The globus_rsl_is_relation_lessthan() function tests whether the 
 * the RSL pointed to by the @a ast parameter is a less-than relation.
 * An example of a less-than relation is
   @code
   "count" = "10"
   @endcode
 *
 * @param ast
 *     Pointer to an RSL parse tree structure.
 *
 * @return
 *     The globus_rsl_is_relation_lessthan() function returns GLOBUS_TRUE if
 *     the RSL parse tree pointed to by @a ast is a less-than relation; 
 *     otherwise, it returns GLOBUS_FALSE.
 */
int 
globus_rsl_is_relation_lessthan (globus_rsl_t *ast)
{
    if (! globus_rsl_is_relation(ast)) return(0);

    if (ast->req.relation.my_operator == GLOBUS_RSL_LT)
       return(1);
    else
       return(0);
}

/**
 * @brief RSL attribute name test
 * @ingroup globus_rsl_predicates
 * @details
 * The globus_rsl_is_relation_attribute_equal() function tests whether
 * the the RSL pointed to by the @a ast parameter is a relation with
 * the attribute name which matches the string pointed to by the
 * @a attribute parameter. This attribute name comparision is
 * case-insensitive.
 *
 * @param ast
 *     Pointer to an RSL parse tree structure.
 * @param attribute
 *     Name of the attribute to test
 *
 * @return
 *     The globus_rsl_is_relation_attribute_equal() function returns
 *     GLOBUS_TRUE if the RSL parse tree pointed to by @a ast is a relation
 *     and its attribute name matches the @a attribute parameter;
 *     otherwise, it returns GLOBUS_FALSE.
 */
int 
globus_rsl_is_relation_attribute_equal (globus_rsl_t *ast, char * attribute)
{
    if (! globus_rsl_is_relation(ast)) return(0);

    if (strcasecmp(globus_rsl_relation_get_attribute(ast), attribute) == 0)
       return(1);
    else
       return(0);
}

/**
 * @brief RSL boolean and test
 * @ingroup globus_rsl_predicates
 *
 * @details
 * The globus_rsl_is_boolean_and() function tests whether the 
 * the RSL pointed to by the @a ast parameter is a boolean "and"
 * composition of RSL trees.
 *
 * An example of a boolean and relation is
   @code
   & ( "queue" = "debug" ) ( "executable" = "a.out" )
   @endcode
 *
 * @param ast
 *     Pointer to an RSL parse tree structure.
 *
 * @return
 *     The globus_rsl_is_boolean_and() function returns GLOBUS_TRUE if
 *     the RSL parse tree pointed to by @a ast is a boolean and of RSL
 *     parse trees; otherwise, it returns GLOBUS_FALSE.
 */
int
globus_rsl_is_boolean_and (globus_rsl_t *ast)
{
    if (! globus_rsl_is_boolean(ast)) return(0);

    if (ast->req.boolean.my_operator == GLOBUS_RSL_AND)
       return(1);
    else
       return(0);
}

/**
 * @brief RSL boolean or test
 * @ingroup globus_rsl_predicates
 * @details
 * The globus_rsl_is_boolean_or() function tests whether the 
 * the RSL pointed to by the @a ast parameter is a boolean "or" composition
 * of RSL trees.
 *
 * An example of a boolean or relation is
   @code
   | ( "count" = "2" ) ( "count" = "4" )
   @endcode
 *
 * @param ast
 *     Pointer to an RSL parse tree structure.
 *
 * @return
 *     The globus_rsl_is_boolean_or() function returns GLOBUS_TRUE if
 *     the RSL parse tree pointed to by @a ast is a boolean and of RSL
 *     parse trees; otherwise, it returns GLOBUS_FALSE.
 */
int
globus_rsl_is_boolean_or (globus_rsl_t *ast)
{
    if (! globus_rsl_is_boolean(ast)) return(0);

    if (ast->req.boolean.my_operator == GLOBUS_RSL_OR)
       return(1);
    else
       return(0);
}

/**
 * @brief RSL boolean multi test
 * @ingroup globus_rsl_predicates
 * @details
 * The globus_rsl_is_boolean_multi() function tests whether the 
 * the RSL pointed to by the @a ast parameter is a boolean "multi-request"
 * composition of RSL trees.
 *
 * An example of a boolean multie-request relation is
   @code
   + ( &( "executable" = "exe.1") ( "count" = "2" ) )
     ( &( "executable" =" exe.2") ( "count" = "2" ) )
   @endcode
 *
 * @param ast
 *     Pointer to an RSL parse tree structure.
 *
 * @return
 *     The globus_rsl_is_boolean_multi() function returns GLOBUS_TRUE if
 *     the RSL parse tree pointed to by @a ast is a boolean multi-request of
 *     RSL parse trees; otherwise, it returns GLOBUS_FALSE.
 */
int
globus_rsl_is_boolean_multi (globus_rsl_t *ast)
{
    if (! globus_rsl_is_boolean(ast)) return(0);

    if (ast->req.boolean.my_operator == GLOBUS_RSL_MULTIREQ)
       return(1);
    else
       return(0);
}

/**
 * @brief RSL literal string  test
 * @ingroup globus_rsl_predicates
 * @details
 * The globus_rsl_value_is_literal() function tests whether the 
 * the RSL value pointed to by the @a ast parameter is a literal string
 * value.
 *
 * An example of a literal string is
   @code
   "count"
   @endcode
 *
 * @param ast
 *     Pointer to an RSL value structure.
 *
 * @return
 *     The globus_rsl_value_is_literal() function returns GLOBUS_TRUE if
 *     the RSL value pointed to by @a ast is a literal string value;
 *     otherwise, it returns GLOBUS_FALSE.
 */
int
globus_rsl_value_is_literal (globus_rsl_value_t *ast)
{
    if (ast==NULL) return(0);

    if (ast->type == GLOBUS_RSL_VALUE_LITERAL)
       return(1);
    else
       return(0);
}

/**
 * @brief RSL value sequence test
 * @ingroup globus_rsl_predicates
 * @details
 * The globus_rsl_value_is_sequence() function tests whether the 
 * the RSL value pointed to by the @a ast parameter is a sequence of
 * RSL values. An example of a sequence of values is
   @code
   "1" "2" "3"
   @endcode
 *
 * @param ast
 *     Pointer to an RSL value structure.
 *
 * @return
 *     The globus_rsl_value_is_sequence() function returns GLOBUS_TRUE if
 *     the RSL value pointed to by @a ast is a value sequnce;
 *     otherwise, it returns GLOBUS_FALSE.
 */
int
globus_rsl_value_is_sequence (globus_rsl_value_t *ast)
{
    if (ast==NULL) return(0);

    if (ast->type == GLOBUS_RSL_VALUE_SEQUENCE)
       return(1);
    else
       return(0);
}

/**
 * @brief RSL value variable test
 * @ingroup globus_rsl_predicates
 * @details
 * The globus_rsl_value_is_variable() function tests whether the 
 * the RSL value pointed to by the @a ast parameter is a variable reference.
 * RSL values. An example of a variable reference is
   @code
   $( "GLOBUSRUN_GASS_URL" )
   @endcode
 *
 * @param ast
 *     Pointer to an RSL value structure.
 *
 * @return
 *     The globus_rsl_value_is_sequence() function returns GLOBUS_TRUE if
 *     the RSL value pointed to by @a ast is a value sequnce;
 *     otherwise, it returns GLOBUS_FALSE.
 */
int
globus_rsl_value_is_variable (globus_rsl_value_t *ast)
{
    if (ast==NULL) return(0);

    if (ast->type == GLOBUS_RSL_VALUE_VARIABLE)
       return(1);
    else
       return(0);
}

/**
 * @brief RSL value concatenation test
 * @ingroup globus_rsl_predicates
 * @details
 * The globus_rsl_value_is_concatenation() function tests whether the 
 * the RSL value pointed to by the @a ast parameter is a concatenation of
 * RSL values. An example of an RSL value concatenation is
   @code
   $( "GLOBUSRUN_GASS_URL" ) # "/input"
   @endcode
 *
 * @param ast
 *     Pointer to an RSL value structure.
 *
 * @return
 *     The globus_rsl_value_is_concatenation() function returns GLOBUS_TRUE
 *     if the RSL value pointed to by @a ast is a value concatenation;
 *     otherwise, it returns GLOBUS_FALSE.
 */
int
globus_rsl_value_is_concatenation (globus_rsl_value_t *ast)
{
    if (ast==NULL) return(0);

    if (ast->type == GLOBUS_RSL_VALUE_CONCATENATION)
       return(1);
    else
       return(0);
}

/**
 * @defgroup globus_rsl_constructors RSL Constructors
 * @ingroup globus_rsl
 * @brief RSL Constructors
 */

/**
 * @brief RSL boolean constructor
 * @ingroup globus_rsl_constructors
 *
 * @details
 * The globus_rsl_make_boolean() function creates a boolean composition
 * of the RSL nodes in the list pointed to by @a children. The new
 * RSL node which is returned contains a reference to the list, not a copy.
 *
 * @param operator
 *     The boolean RSL operator to use to join the RSL parse tree list pointed
 *     to by the @a children parameter. This value must be one of
 *     GLOBUS_RSL_AND, GLOBUS_RSL_OR, GLOBUS_RSL_MULTIREQ in order to create
 *     a valid RSL tree.
 * @param children
 *     Pointer to a list of RSL syntax trees to combine with the boolean
 *     operation described by the @a operator parameter.
 *
 * @return
 *     The globus_rsl_make_boolean() function returns a new
 *     RSL parse tree node that contains a shallow reference to the 
 *     list of values pointed to by the @a children parameter joined by
 *     the operator value in the @a operator parameter. If an error occurs,
 *     globus_rsl_make_boolean() returns NULL.
 */
globus_rsl_t *
globus_rsl_make_boolean (int operator,
                       globus_list_t *children)
{
    globus_rsl_t * tmp_rsl;

    tmp_rsl = (globus_rsl_t *) globus_malloc (sizeof(globus_rsl_t));
    if (tmp_rsl != NULL)
    {
        tmp_rsl->type = GLOBUS_RSL_BOOLEAN;
        tmp_rsl->req.boolean.my_operator = operator;
        tmp_rsl->req.boolean.operand_list = children;
    }

    return(tmp_rsl);
}

/**
 * @brief RSL relation constructor
 * @ingroup globus_rsl_constructors
 * @details
 * The globus_rsl_make_relation() function creates a relation between
 * the attribute named by the @a attributename parameter and the values
 * pointed to by the @a value_sequence list. The new RSL relation
 * node which is returned contains a reference to the @a attributename 
 * and @a value_sequence parameters, not a copy.
 *
 * @param operator
 *     The RSL operator to use to relate the RSL attribute name pointed to by
 *     the @a attributename parameter and the values pointed to by the
 *     @a value_sequence parameter. This value must be one of GLOBUS_RSL_EQ,
 *     GLOBUS_RSL_NEQ, GLOBUS_RSL_GT, GLOBUS_RSL_GTEQ, GLOBUS_RSL_LT, or
 *     GLOBUS_RSL_LTEQ in order to create a valid RSL node.
 * @param attributename
 *     Pointer to a string naming the attribute of the new RSL relation.
 * @param value_sequence
 *     Pointer to a sequence of RSL values to use in the new RSL relation.
 *
 * @return
 *     The globus_rsl_make_relation() function returns a new
 *     RSL parse tree node that contains a shallow reference to the 
 *     attribute name pointed to by the @a attributename parameter and the
 *     RSL value sequence pointed to by the @a value_sequence parameter.
 *     If an error occurs, globus_rsl_make_relation() returns NULL.
 */
globus_rsl_t *
globus_rsl_make_relation (int operator,
                        char *attributename,
                        globus_rsl_value_t *value_sequence)
{
    globus_rsl_t * tmp_rsl;

    tmp_rsl = (globus_rsl_t *) globus_malloc (sizeof(globus_rsl_t));
    if (tmp_rsl != NULL)
    {
        tmp_rsl->type = GLOBUS_RSL_RELATION;
        tmp_rsl->req.relation.my_operator = operator;
        tmp_rsl->req.relation.value_sequence = value_sequence;
        tmp_rsl->req.relation.attribute_name = attributename;
    }

    return(tmp_rsl);
}

/**
 * @brief RSL literal constructor
 * @ingroup globus_rsl_constructors
 *
 * @details
 * The globus_rsl_value_make_literal() function creates a string literal
 * RSL value node containing the value pointed to by the @a string
 * parameter.  The new RSL value
 * node which is returned contains a reference to the @a string
 * parameter, not a copy.
 *
 * @param string
 *     The literal string to be used in the new value. 
 *
 * @return
 *     The globus_rsl_value_make_literal() function returns a new
 *     RSL value node that contains a shallow reference to the 
 *     string pointed to by the @a string parameter.
 *     If an error occurs, globus_rsl_value_make_literal() returns NULL.
 */
globus_rsl_value_t *
globus_rsl_value_make_literal (char *string)
{
    globus_rsl_value_t * tmp_rsl_value;

    tmp_rsl_value = (globus_rsl_value_t *) 
                     globus_malloc (sizeof(globus_rsl_value_t));
    if (tmp_rsl_value != NULL)
    {
        tmp_rsl_value->type = GLOBUS_RSL_VALUE_LITERAL;
        tmp_rsl_value->value.literal.string = string;
    }

    return(tmp_rsl_value);
}

/**
 * @brief RSL value sequence constructor
 * @ingroup globus_rsl_constructors
 *
 * @details
 * The globus_rsl_value_make_sequence() function creates a value
 * sequence RSL node referring to the values pointed to by the
 * @a value_list parameter.  The new node returned by this function
 * contains a reference to the @a value_list parameter, not a copy.
 *
 * @param value_list
 *     A pointer to a list of globus_rsl_value_t pointers.
 *
 * @return
 *     The globus_rsl_value_make_sequence() function returns a new
 *     RSL value node that contains a shallow reference to the 
 *     list pointed to by the @a value_list parameter.
 *     If an error occurs, globus_rsl_value_make_sequence() returns NULL.
 */
globus_rsl_value_t *
globus_rsl_value_make_sequence (globus_list_t * value_list)
{
    globus_rsl_value_t * tmp_rsl_value;

    tmp_rsl_value = (globus_rsl_value_t *) 
                     globus_malloc (sizeof(globus_rsl_value_t));
    if (tmp_rsl_value != NULL)
    {
        tmp_rsl_value->type = GLOBUS_RSL_VALUE_SEQUENCE;
        tmp_rsl_value->value.sequence.value_list = value_list;
    }

    return(tmp_rsl_value);
}

/**
 * @brief RSL variable reference constructor
 * @ingroup globus_rsl_constructors
 * @details
 * The globus_rsl_value_make_variable() function creates a variable
 * reference RSL node referring to the variable name contained in the
 * value pointed to by @a sequence parameter.  The new node returned by
 * this function contains a reference to the @a sequence parameter, not a
 * copy.
 *
 * @param sequence
 *     A pointer to a RSL value sequnce.
 *
 * @return
 *     The globus_rsl_value_make_variable() function returns a new
 *     RSL value node that contains a shallow reference to the 
 *     value sequence pointed to by the @a sequence parameter.
 *     If an error occurs, globus_rsl_value_make_variable() returns NULL.
 */
globus_rsl_value_t *
globus_rsl_value_make_variable (globus_rsl_value_t * sequence)
{
    globus_rsl_value_t * tmp_rsl_value;

    tmp_rsl_value = (globus_rsl_value_t *) 
                     globus_malloc (sizeof(globus_rsl_value_t));
    if (tmp_rsl_value != NULL)
    {
        tmp_rsl_value->type = GLOBUS_RSL_VALUE_VARIABLE;
        tmp_rsl_value->value.variable.sequence = sequence;
    }

    return(tmp_rsl_value);
}

/**
 * @brief RSL concatenation  constructor
 * @ingroup globus_rsl_constructors
 * @details
 * The globus_rsl_value_make_concatenation() function creates a
 * concatenation of the values pointed to by the @a left_value and 
 * @a right_value parameters. The new node returned by
 * this function contains a reference to these parameters' values, not a
 * copy.
 *
 * @param left_value
 *     A pointer to a RSL value to act as the left side of the concatenation.
 *     This must be a string literal or variable reference.
 * @param right_value
 *     A pointer to a RSL value to act as the right side of the concatenation.
 *     This must be a string literal or variable reference.
 *
 * @return
 *     The globus_rsl_value_make_concatenation() function returns a new
 *     RSL value node that contains a shallow reference to the 
 *     values pointed to by the @a left_value and @a right_value parameters.
 *     If an error occurs, globus_rsl_value_make_concatenation() returns
 *     NULL.
 */
globus_rsl_value_t *
globus_rsl_value_make_concatenation (globus_rsl_value_t *left_value,
                                   globus_rsl_value_t *right_value)
{
    globus_rsl_value_t * tmp_rsl_value;

    tmp_rsl_value = (globus_rsl_value_t *) globus_malloc 
                    (sizeof(globus_rsl_value_t));
    if (tmp_rsl_value != NULL)
    {
        tmp_rsl_value->type = GLOBUS_RSL_VALUE_CONCATENATION;
        tmp_rsl_value->value.concatenation.left_value = left_value;
        tmp_rsl_value->value.concatenation.right_value = right_value;
    }

    return(tmp_rsl_value);
}

/**
 * @defgroup globus_rsl_memory RSL Memory Management
 * @ingroup globus_rsl
 * @brief RSL Memory Management
 */

/**
 * @brief Create a deep copy of an RSL syntax tree
 * @ingroup globus_rsl_memory
 *
 * @details
 * The globus_rsl_copy_recursive() function performs a deep copy
 * of the RSL syntax tree pointed to by the @a ast_node parameter. All
 * RSL nodes, value nodes, variable names, attributes, and literals
 * will be copied to the return value.
 *
 * @param ast_node
 *     An RSL syntax tree to copy.
 *
 * @return 
 *     The globus_rsl_copy_recursive() function returns a copy of its
 *     input parameter that that can be used after
 *     the @a ast_node and its values have been freed.
 *     If an error occurs, globus_rsl_copy_recursive() returns
 *     NULL.
 */
globus_rsl_t *
globus_rsl_copy_recursive(globus_rsl_t * ast_node)
{
    globus_rsl_t *       tmp_rsl_ptr;
    globus_rsl_t *       new_rsl_ptr;
    globus_list_t *      tmp_rsl_list;
    globus_list_t *      new_rsl_list;
    globus_list_t *      new_rsl_list_reverse;
    globus_rsl_value_t * tmp_rsl_value_ptr;
    globus_rsl_value_t * new_rsl_value_ptr;
    globus_list_t *      tmp_value_list;
    globus_list_t *      new_value_list;
    globus_list_t *      new_value_list_reverse;
    char *               tmp_string;

    if (ast_node==NULL) return(NULL);

    switch (ast_node->type)
    {
        case GLOBUS_RSL_BOOLEAN:
            
            tmp_rsl_list = globus_rsl_boolean_get_operand_list(ast_node);

            new_rsl_list = NULL;

            while (! globus_list_empty(tmp_rsl_list))
            {
                tmp_rsl_ptr = (globus_rsl_t *) globus_list_first
                     (tmp_rsl_list);

                new_rsl_ptr = globus_rsl_copy_recursive(tmp_rsl_ptr);

                globus_list_insert(&new_rsl_list, (void *) new_rsl_ptr);

                tmp_rsl_list = globus_list_rest(tmp_rsl_list);
            }

            new_rsl_list_reverse = globus_list_copy_reverse(new_rsl_list);
            globus_list_free(new_rsl_list);

            return(globus_rsl_make_boolean
                       (globus_rsl_boolean_get_operator(ast_node),
                       new_rsl_list_reverse));

        case GLOBUS_RSL_RELATION:

            tmp_value_list = globus_rsl_value_sequence_get_value_list(
                             globus_rsl_relation_get_value_sequence(ast_node));

            new_value_list = NULL;

            while (! globus_list_empty(tmp_value_list))
            {
                tmp_rsl_value_ptr = (globus_rsl_value_t *) globus_list_first
                     (tmp_value_list);

                new_rsl_value_ptr = globus_rsl_value_copy_recursive
                                        (tmp_rsl_value_ptr);
                globus_list_insert(&new_value_list, (void *) new_rsl_value_ptr);

                tmp_value_list = globus_list_rest(tmp_value_list);
            }

        new_value_list_reverse = globus_list_copy_reverse(new_value_list);
        globus_list_free(new_value_list);

            tmp_string = (char *) globus_malloc
                 (strlen(globus_rsl_relation_get_attribute(ast_node)) + 1);
            strcpy(tmp_string, 
                 globus_rsl_relation_get_attribute(ast_node));

            return(globus_rsl_make_relation
                       (globus_rsl_relation_get_operator(ast_node),
                       tmp_string,
                       globus_rsl_value_make_sequence(new_value_list_reverse)));

        default:
            return(NULL);
    }
}

/**
 * @brief Create a deep copy of an RSL value
 * @ingroup globus_rsl_memory
 *
 * @details
 * The globus_rsl_value_copy_recursive() function performs a deep copy
 * of the RSL value pointed to by the @a globus_rsl_value_ptr parameter.
 * All variable names, attributes, literals, and value lists will be copied
 * to the return value.
 *
 * @param globus_rsl_value_ptr
 *     A pointer to an RSL value to copy.
 *
 * @return 
 *     The globus_rsl_value_copy_recursive() function returns a copy of its
 *     input parameter that that can be used after
 *     the @a globus_rsl_value_ptr and its values have been freed.
 *     If an error occurs, globus_rsl_value_copy_recursive() returns
 *     NULL.
 */
globus_rsl_value_t *
globus_rsl_value_copy_recursive(globus_rsl_value_t * globus_rsl_value_ptr)
{
    globus_rsl_value_t * tmp_rsl_value_ptr;
    globus_rsl_value_t * new_rsl_value_ptr;
    globus_rsl_value_t * new_rsl_value_seq;
    globus_rsl_value_t * new_rsl_value_left;
    globus_rsl_value_t * new_rsl_value_right;
    globus_list_t *      tmp_value_list;
    globus_list_t *      new_value_list;
    globus_list_t *      new_value_list_reverse;
    char *               tmp_string;
    char *               literal_ptr;

    if (globus_rsl_value_ptr==NULL) return(NULL);

    switch (globus_rsl_value_ptr->type)
    {
        case GLOBUS_RSL_VALUE_LITERAL:

            literal_ptr = 
                globus_rsl_value_literal_get_string(globus_rsl_value_ptr);

            if (literal_ptr == NULL)
            {
                return globus_rsl_value_make_literal(NULL);
            }
            else
            {
                tmp_string = (char *) globus_malloc 
                       (strlen(literal_ptr) + 1);
                strcpy(tmp_string, literal_ptr);

                return(globus_rsl_value_make_literal(tmp_string));
            }

            break;

        case GLOBUS_RSL_VALUE_SEQUENCE:

            tmp_value_list =
                globus_rsl_value_sequence_get_value_list(globus_rsl_value_ptr);
 
            new_value_list = NULL;

            while (! globus_list_empty(tmp_value_list))
            {
                tmp_rsl_value_ptr = (globus_rsl_value_t *) globus_list_first
                     (tmp_value_list);

                new_rsl_value_ptr = globus_rsl_value_copy_recursive
                                        (tmp_rsl_value_ptr);

                globus_list_insert(&new_value_list, (void *) new_rsl_value_ptr);

                tmp_value_list = globus_list_rest(tmp_value_list);
            }

        new_value_list_reverse = globus_list_copy_reverse(new_value_list);
        globus_list_free(new_value_list);

        return(globus_rsl_value_make_sequence(new_value_list_reverse));

        case GLOBUS_RSL_VALUE_VARIABLE:

            new_rsl_value_seq = globus_rsl_value_copy_recursive(
                (globus_rsl_value_variable_get_sequence
                     (globus_rsl_value_ptr)));

            return(globus_rsl_value_make_variable(new_rsl_value_seq));

        case GLOBUS_RSL_VALUE_CONCATENATION:
            new_rsl_value_left = globus_rsl_value_copy_recursive
                   (globus_rsl_value_concatenation_get_left
                        (globus_rsl_value_ptr));

            new_rsl_value_right = globus_rsl_value_copy_recursive
                   (globus_rsl_value_concatenation_get_right
                        (globus_rsl_value_ptr));

            return(globus_rsl_value_make_concatenation
                   (new_rsl_value_left,
                    new_rsl_value_right));

        default:
            return(NULL);
    }
    return(NULL);
}


/**
 * @defgroup globus_rsl_accessor RSL Accessor Functions
 * @ingroup globus_rsl
 * @brief RSL Accessor Functions
 */



/**
 * @brief Get the RSL operator used in a boolean RSL composition
 * @ingroup globus_rsl_accessor
 * @details
 * The globus_rsl_boolean_get_operator() function returns
 * the operator that is used by the boolean RSL composition.
 *
 * @param ast_node
 *     The RSL syntax tree to inspect.
 *
 * @return
 *     Upon success, globus_rsl_boolean_get_operator() returns one
 *     of GLOBUS_RSL_AND, GLOBUS_RSL_OR, GLOBUS_RSL_MULTIREQ. If an error
 *     occurs, globus_rsl_boolean_get_operator() returns -1.
 */
int
globus_rsl_boolean_get_operator (globus_rsl_t *ast_node)
{
    if (ast_node==NULL) return(-1);
    if (! globus_rsl_is_boolean(ast_node)) return(-1);

    return(ast_node->req.boolean.my_operator);
}

/**
 * @brief Get the RSL operand list from a boolean RSL composition
 * @ingroup globus_rsl_accessor
 * @details
 * The globus_rsl_boolean_get_operand_list() function returns
 * the list of RSL syntax tree nodes that is joined by a boolean
 * composition.
 *
 * @param ast_node
 *     The RSL syntax tree to inspect.
 *
 * @return
 *     Upon success, globus_rsl_boolean_get_operand_list() returns a
 *     pointer to a list of RSL syntax tree nodes that are the operand of
 *     a boolean composition operation.  If an error
 *     occurs, globus_rsl_boolean_get_operand_list() returns NULL.
 */
globus_list_t *
globus_rsl_boolean_get_operand_list (globus_rsl_t *ast_node)
{
    if (ast_node==NULL) return(NULL);
    if (! globus_rsl_is_boolean(ast_node)) return(NULL);

    return(ast_node->req.boolean.operand_list);
}

/**
 * @brief Get a reference to the RSL operand list from a boolean RSL composition
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_boolean_get_operand_list_ref() function returns
 * a pointer to the list of RSL syntax tree nodes that is joined by a
 * boolean composition. If this list is modified, then the value of boolean
 * syntax tree is modified.
 *     
 * @param boolean_node
 *     The RSL syntax tree to inspect.
 *
 * @return
 *     Upon success, globus_rsl_boolean_get_operand_list_ref() returns a
 *     pointer to the list pointer in the RSL syntax tree data structure. This
 *     list can be modified to change the oprands of the boolean operation.
 *     If an error occurs, globus_rsl_boolean_get_operand_list_ref() returns
 *     NULL.
 */
globus_list_t **
globus_rsl_boolean_get_operand_list_ref (globus_rsl_t *boolean_node)
{
  if (boolean_node==NULL) return NULL;
  if (! globus_rsl_is_boolean(boolean_node)) return NULL;

  return &(boolean_node->req.boolean.operand_list);
}


/*                   relations                   */

/**
 * @brief Get an RSL relation attribute name
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_relation_get_attribute() function returns
 * a pointer to the name of the attribute in an RSL relation. This
 * return value is a shallow reference to the attribute name.
 *     
 * @param ast_node
 *     The RSL relation node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_relation_get_attribute() returns a
 *     pointer to the name of the attribute of the relation. 
 *     If an error occurs, globus_rsl_relation_get_attribute() returns
 *     NULL.
 */
char *
globus_rsl_relation_get_attribute (globus_rsl_t *ast_node)
{
    if (ast_node==NULL) return NULL;
    if (! globus_rsl_is_relation(ast_node)) return NULL;

    return(ast_node->req.relation.attribute_name);
}

/**
 * @brief Get an RSL relation operator
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_relation_get_operator() function returns
 * the operation type represented by the RSL relation node pointed to by
 * the @a ast_node parameter.
 * 
 * @param ast_node
 *     The RSL relation node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_relation_get_operator() returns one
 *     of GLOBUS_RSL_EQ, GLOBUS_RSL_NEQ, GLOBUS_RSL_GT, GLOBUS_RSL_GTEQ, 
 *     GLOBUS_RSL_LT, or GLOBUS_RSL_LTEQ.
 *     If an error occurs, globus_rsl_relation_get_operator() returns
 *     -1.
 */
int
globus_rsl_relation_get_operator (globus_rsl_t *ast_node)
{
    if (ast_node==NULL) return(-1);
    if (! globus_rsl_is_relation(ast_node)) return(-1);

    return(ast_node->req.relation.my_operator);
}

/**
 * @brief Get the value of an RSL relation
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_relation_get_value_sequence() function returns
 * the value of an RSL relation node pointed to by
 * the @a ast_node parameter. 
 * 
 * @param ast_node
 *     The RSL relation node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_relation_get_value_sequence() returns
 *     the value sequence pointer in the RSL relation pointed to by the
 *     @a ast_node parameter.
 *     If an error occurs, globus_rsl_relation_get_value_sequence() returns
 *     NULL.
 */
globus_rsl_value_t *
globus_rsl_relation_get_value_sequence (globus_rsl_t *ast_node)
{
    if (ast_node==NULL) return(NULL);
    if (! globus_rsl_is_relation(ast_node)) return(NULL);

    return(ast_node->req.relation.value_sequence);
}


/**
 * @brief Get the single value of an RSL relation
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_relation_get_single_value() function returns
 * the value of an RSL relation node pointed to by
 * the @a ast_node parameter if the value is a sequence of one value. 
 * 
 * @param ast_node
 *     The RSL relation node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_relation_get_single_value() returns
 *     the value pointer at the head of the RSL relation pointed to by the
 *     @a ast_node parameter. If the value sequence has more than one value
 *     or the @a ast_node points to an RSL syntax tree that is not a relation,
 *     globus_rsl_relation_get_value_sequence() returns
 *     NULL.
 */
globus_rsl_value_t *
globus_rsl_relation_get_single_value (globus_rsl_t *ast_node)
{
    if ( ast_node == NULL ) return NULL;
    if ( ! globus_rsl_is_relation (ast_node) ) return NULL;

    if ( globus_list_size (
                 globus_rsl_value_sequence_get_value_list (
                           globus_rsl_relation_get_value_sequence (ast_node)))
       == 1)
    {
        return ((globus_rsl_value_t *)
            globus_list_first (
               globus_rsl_value_sequence_get_value_list (
                           globus_rsl_relation_get_value_sequence (ast_node))));
    }
    else
    {
        return NULL;
    }
}

/**
 * @brief Get the string value of an RSL literal
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_value_literal_get_string() function returns
 * the string value of an RSL literal node pointed to by
 * the @a literal_node parameter.
 * 
 * @param literal_node
 *     The RSL literal node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_value_literal_get_string() returns
 *     a pointer to the string value of the literal pointed to by the 
 *     @a literal_node parameter.  If the value is not a literal,
 *     globus_rsl_value_literal_get_string() returns
 *     NULL.
 */
char *
globus_rsl_value_literal_get_string (globus_rsl_value_t *literal_node)
{
    if ( literal_node == NULL ) return NULL;
    if ( ! globus_rsl_value_is_literal (literal_node) ) return NULL;

    return(literal_node->value.literal.string);
}

/**
 * @brief Get the value list from an RSL value sequence
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_value_sequence_get_value_list() function returns
 * the list of globus_rsl_value_t pointer values associated with
 * the RSL value sequence pointed to by the @a sequence_node parameter.
 * 
 * @param sequence_node
 *     The RSL sequence node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_value_sequence_get_value_list() returns
 *     a pointer to the list of values pointed to by the 
 *     @a sequence_node parameter.  If the value is not a sequence,
 *     globus_rsl_value_literal_get_string() returns
 *     NULL.
 */
globus_list_t *
globus_rsl_value_sequence_get_value_list (globus_rsl_value_t *sequence_node)
{
    if ( sequence_node == NULL ) return NULL;
    if ( ! globus_rsl_value_is_sequence (sequence_node) ) return NULL;

    return(sequence_node->value.sequence.value_list);
}

/**
 * @brief Get the value sequence from an RSL variable reference
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_value_variable_get_sequence() function returns
 * the sequence value associated with the RSL variable reference pointed to
 * by the @a variable_node parameter.
 * 
 * @param variable_node
 *     The RSL variable node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_value_variable_get_sequence() returns
 *     a pointer to the rsl value sequence pointed to by the 
 *     @a variable_node parameter.  If the value is not a variable reference,
 *     globus_rsl_value_variable_get_sequence() returns
 *     NULL.
 */
globus_rsl_value_t *
globus_rsl_value_variable_get_sequence (globus_rsl_value_t * variable_node)
{
    if ( variable_node == NULL ) return(NULL);
    if ( ! globus_rsl_value_is_variable (variable_node) ) return(NULL);

    return(variable_node->value.variable.sequence);
}

/**
 * @brief Get the name of an RSL variable reference
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_value_variable_get_name() function returns
 * a pointer to the name of the RSL variable name pointed to by the
 * @a variable_node parameter.
 * 
 * @param variable_node
 *     The RSL variable node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_value_variable_get_name() returns
 *     a pointer to the string containing the name of the variable referenced
 *     by the @a variable_node parameter.  If the node is not a variable
 *     reference, globus_rsl_value_variable_get_sequence() returns
 *     NULL.
 */
char *
globus_rsl_value_variable_get_name (globus_rsl_value_t *variable_node)
{
    globus_rsl_value_t * x;
    char * y;

    if ( variable_node == NULL ) return(NULL);
    if ( ! globus_rsl_value_is_variable (variable_node) ) return(NULL);

    x = (globus_rsl_value_t *) globus_list_first (
            globus_rsl_value_sequence_get_value_list (
               globus_rsl_value_variable_get_sequence(variable_node)));

    y = globus_rsl_value_literal_get_string(x);

    return (y);
}


/**
 * @brief Get the default value of an RSL variable reference
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_value_variable_get_default() function returns
 * a pointer to the default value of the RSL variable pointed to by the 
 * @a variable_node parameter to use if the
 * variable's name is not bound in the current evaluation context.
 * 
 * @param variable_node
 *     The RSL variable node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_value_variable_get_default() returns
 *     a pointer to the string containing the default value of the variable
 *     referenced by the @a variable_node parameter.  If the node is not a
 *     variable reference or no default value exists in the RSL node,
 *     globus_rsl_value_variable_get_default() returns
 *     NULL.
 */
char *
globus_rsl_value_variable_get_default (globus_rsl_value_t *variable_node)
{
    if (variable_node==NULL) return NULL;
    if ( ! globus_rsl_value_is_variable (variable_node) ) return(NULL);
    
    if ( globus_rsl_value_variable_get_size(variable_node) > 1 )
    {
        return  (
          globus_rsl_value_literal_get_string( (globus_rsl_value_t *)
            globus_list_first (globus_list_rest (
              globus_rsl_value_sequence_get_value_list (
                globus_rsl_value_variable_get_sequence(variable_node))))));
    }
    else
    {
        return NULL;
    }
}

/**
 * @brief Get the size of the value list within an RSL variable reference node
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_value_variable_get_size() function returns
 * the number of nodes in the RSL variable reference node pointed to by the
 * @a variable_node parameter.
 * 
 * @param variable_node
 *     The RSL variable node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_value_variable_get_size() returns
 *     the list of values within a RSL variable reference, or -1 if the
 *     node pointed to by @a variable_node is not a variable reference. If the
 *     return value is 1, then the variable has no default value included
 *     in the reference.
 */
int
globus_rsl_value_variable_get_size (globus_rsl_value_t *variable_node)
{
    if ( variable_node == NULL ) return(-1);
    if ( ! globus_rsl_value_is_variable (variable_node) ) return(-1);

    return( globus_list_size (
            globus_rsl_value_sequence_get_value_list (
               globus_rsl_value_variable_get_sequence(variable_node))));
}

/**
 * @brief Get the left side of a concatenation value 
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_value_concatenation_get_left() function returns
 * the left side of an RSL value concatenation pointed to by the
 * @a concatenation_node parameter.
 * 
 * @param concatenation_node
 *     The RSL concatenation node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_value_concatenation_get_left() returns
 *     a pointer to the left value of the concatenation values pointed to by
 *     the @a concatenation_node parameter.  If an error occurs, 
 *     globus_rsl_value_concatenation_get_left() returns NULL.
 */
globus_rsl_value_t *
globus_rsl_value_concatenation_get_left (globus_rsl_value_t *concatenation_node)
{
    if (concatenation_node==NULL) return NULL;
    if ( ! globus_rsl_value_is_concatenation (concatenation_node) ) return(NULL);

    return(concatenation_node->value.concatenation.left_value);
}

/**
 * @brief Get the right side of a concatenation value 
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_value_concatenation_get_right() function returns
 * the right side of an RSL value concatenation pointed to by the
 * @a concatenation_node parameter.
 * 
 * @param concatenation_node
 *     The RSL concatenation node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_value_concatenation_get_right() returns
 *     a pointer to the right value of the concatenation values pointed to by
 *     the @a concatenation_node parameter.  If an error occurs, 
 *     globus_rsl_value_concatenation_get_right() returns NULL.
 */
globus_rsl_value_t *
globus_rsl_value_concatenation_get_right (globus_rsl_value_t *concatenation_node)
{
    if (concatenation_node==NULL) return NULL;
    if (! globus_rsl_value_is_concatenation (concatenation_node)) return(NULL);

    return(concatenation_node->value.concatenation.right_value);
}

/**
 * @brief Get a reference to the list of values in a sequence
 * @ingroup globus_rsl_accessor
 *
 * @details
 * The globus_rsl_value_sequence_get_list_ref() function returns
 * a reference to the list of values in a value sequence. Any changes to
 * the elements of this list will affect the @a sequence_node
 * parameter.
 * 
 * @param sequence_node
 *     The RSL sequence node to inspect.
 *
 * @return
 *     Upon success, globus_rsl_value_sequence_get_list_ref() returns
 *     a pointer to the list of the globus_rsl_value_t pointer values contained
 *     in the @a sequence_node parameter.  If an error occurs, 
 *     globus_rsl_value_sequence_get_list_ref() returns NULL.
 */
globus_list_t **
globus_rsl_value_sequence_get_list_ref (globus_rsl_value_t *sequence_node)
{
  if (sequence_node==NULL) return NULL;
  if (! globus_rsl_value_is_sequence (sequence_node)) return NULL;

  return &(sequence_node->value.sequence.value_list);
}


/**
 * @brief Set the left-hand value of a concatenation
 * @ingroup globus_rsl_param
 *
 * @details
 * The globus_rsl_value_concatenation_set_left() sets the left hand side
 * of a concatenation pointed to by @a concatenation_node to the value
 * pointed to by  @a new_left_node. If there was any
 * previous value to the left hand side of the concatenation, it is
 * discarded but not freed.
 *
 * @param concatenation_node
 *     A pointer to the RSL value concatenation node to modify.
 * @param new_left_node
 *     A pointer to the new left hand side of the concatenation.
 *
 * @return
 *     Upon success, globus_rsl_value_concatenation_set_left() returns
 *     @a GLOBUS_SUCCESS and modifies the value pointed to by the
 *     @a concatenation_node parameter to use the value pointed to by the
 *     @a new_left_node parameter as its left hand side value. If an error
 *     occurs, globus_rsl_value_concatenation_set_left() returns -1.
 */
int
globus_rsl_value_concatenation_set_left (globus_rsl_value_t *concatenation_node,
                                       globus_rsl_value_t *new_left_node)
{
    if (concatenation_node==NULL) return(-1);
    if (new_left_node==NULL) return(-1);
    if (! globus_rsl_value_is_concatenation (concatenation_node)) return(-1);

    concatenation_node->value.concatenation.left_value = new_left_node;

    return(0);
}

/**
 * @brief Set the right-hand value of a concatenation
 * @ingroup globus_rsl_param
 *
 * @details
 * The globus_rsl_value_concatenation_set_right() sets the right-hand
 * side of a concatenation pointed to by @a concatenation_node to the value
 * pointed to by  @a new_right_node. If there was any
 * previous value to the right-hand side of the concatenation, it is
 * discarded but not freed.
 *
 * @param concatenation_node
 *     A pointer to the RSL value concatenation node to modify.
 * @param new_right_node
 *     A pointer to the new right hand side of the concatenation.
 *
 * @return
 *     Upon success, globus_rsl_value_concatenation_set_right() returns
 *     @a GLOBUS_SUCCESS and modifies the value pointed to by the
 *     @a concatenation_node parameter to use the value pointed to by the
 *     @a new_right_node parameter as its right hand side value. If an error
 *     occurs, globus_rsl_value_concatenation_set_right() returns -1.
 */
int
globus_rsl_value_concatenation_set_right (globus_rsl_value_t *concatenation_node,
                                        globus_rsl_value_t *new_right_node)
{
    if (concatenation_node==NULL) return(-1);
    if (new_right_node==NULL) return(-1);
    if (! globus_rsl_value_is_concatenation (concatenation_node)) return(-1);

    concatenation_node->value.concatenation.right_value = new_right_node;

    return(0);
}

/**
 * @defgroup globus_rsl_list List Functions
 * @ingroup globus_rsl
 * @brief List Functions
 */

/**
 * @brief Create a reverse-order copy of a list
 * @ingroup globus_rsl_list
 * @details
 * The globus_list_copy_reverse() function creates and returns a copy of
 * its input parameter, with the order of the list elements reversed. This
 * copy is a shallow copy of list nodes, so both the list pointed to by 
 * @a orig and the returned list point to the same list element data.
 *
 * @param orig
 *     A pointer to the list to copy.
 *
 * @return
 *     Upon success, globus_list_copy_reverse() returns a new list
 *     containing the same elements as the list pointed to by @a orig in 
 *     reverse order. If an error occurs, globus_list_copy_reverse() returns
 *     NULL.
 */
globus_list_t *
globus_list_copy_reverse (globus_list_t * orig)
{
  globus_list_t *new_list = NULL;

  while ( orig != NULL ) {
    globus_list_insert ( &new_list,
                         globus_list_first (orig) );
    orig = globus_list_rest (orig);
  }

  return new_list;
}



/**
 * @brief Free an RSL value node
 * @ingroup globus_rsl_memory
 * 
 * @details
 * The globus_rsl_value_free() function frees the RSL value pointed to
 * by the @a val parameter. This only frees the RSL value node itself, and
 * not any sequence or string values associated with that node.
 *
 * @param val
 *     The RSL value node to free.
 *
 * @return
 *     The globus_rsl_value_free() function always returns GLOBUS_SUCCESS.
 */
int
globus_rsl_value_free (globus_rsl_value_t *val)
{
    globus_free(val);
    return(0);
}

/**
 * @brief Free an RSL syntax tree node
 * @ingroup globus_rsl_memory
 * 
 * @details
 * The globus_rsl_free() function frees the RSL syntax tree node pointed
 * to by the @a ast_node parameter. This only frees the RSL syntax tree
 * node itself, and not any boolean operands, relation names, or values
 * associated with the node.
 *
 * @param ast_node
 *     The RSL syntax tree node to free.
 *
 * @return
 *     The globus_rsl_value_free() function always returns GLOBUS_SUCCESS.
 */
int
globus_rsl_free (globus_rsl_t *ast_node)
{
    globus_free(ast_node);
    return(0);
}

/**
 * @brief Free an RSL value and all its child nodes
 * @ingroup globus_rsl_memory
 *
 * @details
 * The globus_rsl_free_recursive() function frees the RSL value
 * node pointed to by the @a globus_rsl_value_ptr, including all 
 * literal strings, variable names, and value sequences.
 * Any pointers to these are no longer valid after
 * globus_rsl_value_free_recursive() returns.
 * 
 * @param globus_rsl_value_ptr
 *     An RSL value node to free.
 *
 * @return
 *     The globus_rsl_value_free_recursive() function always returns
 *     GLOBUS_SUCCESS.
 */
int
globus_rsl_value_free_recursive (globus_rsl_value_t * globus_rsl_value_ptr)
{
    globus_rsl_value_t * tmp_rsl_value_ptr;
    globus_list_t * tmp_rsl_list;

    if (globus_rsl_value_ptr==NULL) return(0);

    switch (globus_rsl_value_ptr->type)
    {
        case GLOBUS_RSL_VALUE_LITERAL:

            globus_free(globus_rsl_value_literal_get_string
                           (globus_rsl_value_ptr));
            break;

        case GLOBUS_RSL_VALUE_SEQUENCE:

            tmp_rsl_list = globus_rsl_value_sequence_get_value_list
                                 (globus_rsl_value_ptr);

            while (! globus_list_empty(tmp_rsl_list))
            {
                tmp_rsl_value_ptr = (globus_rsl_value_t *)
		    globus_list_remove(&tmp_rsl_list, tmp_rsl_list);
                globus_rsl_value_free_recursive(tmp_rsl_value_ptr);
            }

            break;

        case GLOBUS_RSL_VALUE_VARIABLE:

            globus_rsl_value_free_recursive
                 (globus_rsl_value_variable_get_sequence(globus_rsl_value_ptr));
            break;

        case GLOBUS_RSL_VALUE_CONCATENATION:

            globus_rsl_value_free_recursive
               (globus_rsl_value_concatenation_get_left(globus_rsl_value_ptr));
            globus_rsl_value_free_recursive
               (globus_rsl_value_concatenation_get_right(globus_rsl_value_ptr));
            break;

        default:

            break;
    }

    globus_free(globus_rsl_value_ptr);
    return(0);
}

/**
 * @brief Free an RSL syntax tree and all its child nodes
 * @ingroup globus_rsl_memory
 *
 * @details
 * The globus_rsl_free_recursive() function frees the RSL syntax tree
 * pointed to by the @a ast_node parameter, including all 
 * boolean operands, attribute names, and values.
 * Any pointers to these are no longer valid after
 * globus_rsl_free_recursive() returns.
 * 
 * @param ast_node
 *     An RSL parse tree to free.
 *
 * @return
 *     The globus_rsl_value_free_recursive() function always returns
 *     GLOBUS_SUCCESS.
 */
int
globus_rsl_free_recursive (globus_rsl_t *ast_node)
{
    globus_list_t * tmp_rsl_list;
    globus_rsl_t * tmp_rsl_ptr;

    switch (ast_node->type)
    {
        case GLOBUS_RSL_BOOLEAN:

            tmp_rsl_list = globus_rsl_boolean_get_operand_list(ast_node);

            while (! globus_list_empty(tmp_rsl_list))
            {
                tmp_rsl_ptr = (globus_rsl_t *) globus_list_first
                     (tmp_rsl_list);

                globus_rsl_free_recursive(tmp_rsl_ptr);

                tmp_rsl_list = globus_list_rest(tmp_rsl_list);
            }

            globus_list_free
                 (globus_rsl_boolean_get_operand_list(ast_node));

            break;

        case GLOBUS_RSL_RELATION:

            globus_rsl_value_free_recursive
                (globus_rsl_relation_get_value_sequence(ast_node));

            globus_free(globus_rsl_relation_get_attribute(ast_node));

            break;

        default:

            return(1);
    }

    globus_free(ast_node);
    return(0);
}

/**
 * @brief Replace the first value in a value list with a literal
 * @ingroup globus_rsl_memory
 *
 * @details
 * The globus_rsl_value_list_literal_replace() function replaces
 * the first value in the list pointed to by the @a value_list parameter
 * with a new value node that is a literal string node pointing to the
 * value of the @a string_value parameter, freeing the old value.
 * 
 * @param value_list
 *     The RSL value list to modify by replacing its first element.
 * @param string_value
 *     The new string value to use as a literal first element of the
 *     list pointed to by the @a value_list parameter.
 *
 * @return
 *     Upon success, globus_rsl_value_list_literal_replace() returns
 *     @a GLOBUS_SUCCESS, frees the current first value of @a value_list and
 *     replaces it with a new literal string node pointing to the value of the
 *     @a string_value parameter. If an error occurs,
 *     globus_rsl_value_list_literal_replace() returns 1.
 */
int
globus_rsl_value_list_literal_replace(globus_list_t * value_list,
                                     char * string_value)
{
    globus_rsl_value_t * rsl_value_ptr;

    rsl_value_ptr = globus_rsl_value_make_literal (string_value);
    if (rsl_value_ptr == NULL)
       return(1);

    globus_rsl_value_free(
        (globus_rsl_value_t *) globus_list_replace_first(value_list,
                                                (void *) rsl_value_ptr));
    return(GLOBUS_SUCCESS);
}

/**
 * @brief Evaluate RSL substitions in an RSL value node
 * @ingroup globus_rsl_memory
 *
 * @details
 * The globus_rsl_value_eval() function modifies the value pointed to
 * by its @a ast_node parameter by replacing all RSL substitution
 * variable reference nodes with the literal values those variables 
 * evaluate to based on the current scope of the symbol table pointed to
 * by the @a symbol_table parameter. It also combines string
 * concatenations into literal string values. Any nodes which are replaced
 * by this function are freed using globus_rsl_value_free_recursive().
 * 
 * @param ast_node
 *     A pointer to the RSL value node to evaluate.
 * @param symbol_table
 *     A symbol table containing current definitions of the RSL substitutions
 *     which can occur in this evaluation scope.
 * @param string_value
 *     An output parameter which is set to point to the value of the string
 *     returned by evaluating the value node pointed to by @a ast_node
 *     if it evaluates to a literal value.
 *     list pointed to by the @a value_list parameter.
 * @param rsl_substitution_flag
 *     A flag indicating whether the node pointed to by the @a ast_node
 *     parameter defines RSL substition variables.
 *
 * @return
 *     Upon success, globus_rsl_value_eval() returns
 *     @a GLOBUS_SUCCESS, and replaces any RSL substitution values in the 
 *     node pointed to by the @a ast_node parameter. If the node evaluates
 *     to a single literal, the @a string_value parameter is modified to
 *     point to the value of that literal. If an error occurs,
 *     globus_rsl_value_eval() returns a non-zero value.
 */
int
globus_rsl_value_eval(globus_rsl_value_t * ast_node,
                      globus_symboltable_t * symbol_table, 
                      char ** string_value,
                      int rsl_substitution_flag)
{
    char * symbol_name;
    char * symbol_value;
    char * tmp_string_value;
    globus_list_t * tmp_rsl_value_list;
    globus_rsl_value_t * tmp_rsl_value_ptr;

    if ( globus_rsl_value_is_literal (ast_node) )
    {
        tmp_string_value = globus_rsl_value_literal_get_string(ast_node);
        *string_value = tmp_string_value ? strdup(tmp_string_value) : NULL;
        return GLOBUS_SUCCESS;
    }
    else if ( globus_rsl_value_is_sequence (ast_node) )
    {
        tmp_rsl_value_list = 
             globus_rsl_value_sequence_get_value_list(ast_node);

        if (rsl_substitution_flag)
        {
            /* in this case the each sequence should only have 2 elements.
             * call globus_rsl_value_eval on each element and replace each
             * with a literal node.
             */

            if (globus_list_size(tmp_rsl_value_list) != 2)
            {
               return(1);
            }

            /*
             * take the first value in the list as the symbol name and 
             * the second as the symbol value.
             */
            if (globus_rsl_value_eval(
                (globus_rsl_value_t *) globus_list_first(tmp_rsl_value_list),
                     symbol_table,
                     &symbol_name,
                     rsl_substitution_flag) != 0)
            {
                 return(1);
            }

            /* globus_list_replace_first returns the replaced 
             * rsl_value_ptr, so in this case we want to free it up.
             */
            globus_rsl_value_free_recursive(
                (globus_rsl_value_t *) globus_list_replace_first
                     (tmp_rsl_value_list,
                     (void *) globus_rsl_value_make_literal(symbol_name)));

            tmp_rsl_value_list = globus_list_rest(tmp_rsl_value_list);

            if (globus_rsl_value_eval(
                (globus_rsl_value_t *) globus_list_first(tmp_rsl_value_list),
                     symbol_table,
                     &symbol_value,
                     rsl_substitution_flag) != 0)
            {
                 return(1);
            }

            /* globus_list_replace_first returns the replaced 
             * rsl_value_ptr, so in this case we want to free it up.
             */
            globus_rsl_value_free_recursive(
                (globus_rsl_value_t *) globus_list_replace_first
                     (tmp_rsl_value_list,
                     (void *) globus_rsl_value_make_literal(symbol_value)));

            if (!symbol_name || !symbol_value)
            {
                return(1);
            }
            else
            {
                /* 
                printf("inserting symbol = %s, value = %s\n",
                        symbol_name, symbol_value);
                */

                globus_symboltable_insert(symbol_table,
                                  (void *) symbol_name,
                                  (void *) symbol_value);
            }
        }
        else
        {
            while (! globus_list_empty(tmp_rsl_value_list))
            {
                tmp_rsl_value_ptr = (globus_rsl_value_t *) globus_list_first
                     (tmp_rsl_value_list);

                if (globus_rsl_value_eval(tmp_rsl_value_ptr,
                                symbol_table,
                                &tmp_string_value,
                                rsl_substitution_flag) == GLOBUS_SUCCESS)
                {
                    if (!globus_rsl_value_is_sequence(tmp_rsl_value_ptr))
                    {
                        /* globus_list_replace_first returns the replaced 
                         * rsl_value_ptr, so in this case we want to free it up.
                         */
                        globus_rsl_value_free_recursive(
                            (globus_rsl_value_t *) globus_list_replace_first
                               (tmp_rsl_value_list,
                               (void *) globus_rsl_value_make_literal
                                    (tmp_string_value)));
                    }
                    
                }
                else
                {
                   /* error evaluating rsl value */
                   return(1);
                }

                tmp_rsl_value_list = globus_list_rest(tmp_rsl_value_list);
            }

        }

        *string_value = NULL;
        return GLOBUS_SUCCESS;
    
    }
    else if ( globus_rsl_value_is_variable (ast_node) )
    {

        if (globus_rsl_value_variable_get_size(ast_node) != 1) 
        {
            return(1);
        }

        symbol_name = globus_rsl_value_variable_get_name(ast_node);

        if (symbol_name == NULL)
        {
            /* unable to get var name from variable ast node */
            return(1);
        }

        if ((tmp_string_value = globus_symboltable_lookup(symbol_table,
                 (void *) symbol_name)
            )
            == NULL)
        {
            /* lookup failed for %s, symbol_name */
            return (1); /*  not-bound-error */
        }
        else
        {
            *string_value = strdup(tmp_string_value);
            return *string_value ? (GLOBUS_SUCCESS) : 1;
        }
    }
    else if ( globus_rsl_value_is_concatenation (ast_node) )
    {
         char * left = NULL;
         char * right = NULL;

         if ( (globus_rsl_value_eval ( 
                   globus_rsl_value_concatenation_get_left (ast_node),
                   symbol_table,
                   &left,
                   rsl_substitution_flag) 
              == GLOBUS_SUCCESS)  &&
              (globus_rsl_value_eval ( 
                   globus_rsl_value_concatenation_get_right (ast_node),
                   symbol_table,
                   &right,
                   rsl_substitution_flag) 
              == GLOBUS_SUCCESS) )
         {
             if ( (left == NULL) || (right == NULL) )
             {
                 if (left)
                 {
                     free(left);
                 }
                 if (right)
                 {
                     free(right);
                 }
                 return(1);
             }

             *string_value = (char *) globus_malloc 
                                                (strlen(left) +
                                                 strlen(right) + 1);
             strcpy(*string_value, left);
             strcat(*string_value, right);

             globus_rsl_value_free_recursive(
                globus_rsl_value_concatenation_get_left(ast_node));
             globus_rsl_value_free_recursive(
                globus_rsl_value_concatenation_get_right(ast_node));
             ast_node->value.concatenation.left_value = NULL;
             ast_node->value.concatenation.right_value = NULL;
             free(left);
             free(right);

             return GLOBUS_SUCCESS;
         }
         else 
         {
             if (left)
             {
                 free(left);
             }
             if (right)
             {
                 free(right);
             }
             return(1); /* concatenate-error; */
         }
    }
    else return(1); /* spec-too-complex-error; */
}

/**
 * @brief Evaluate an RSL syntax tree
 * @ingroup globus_rsl_memory
 *
 * @details
 * The globus_rsl_eval() function modifies the RSL parse tree pointed
 * to by its @a ast_node parameter by replacing all RSL substitution
 * variable reference nodes with the literal values those variables 
 * evaluate to based on the current scope of the symbol table pointed to
 * by the @a symbol_table parameter. It also combines string
 * concatenations into literal string values. Any nodes which are replaced
 * by this function are freed using globus_rsl_value_free_recursive().
 * 
 * @param ast_node
 *     A pointer to the RSL syntax tree to evaluate.
 * @param symbol_table
 *     A symbol table containing current definitions of the RSL substitutions
 *     which can occur in this evaluation scope.
 *
 * @return
 *     Upon success, globus_rsl_eval() returns
 *     @a GLOBUS_SUCCESS, and replaces all RSL substitution values and
 *     concatenations in @a ast_node or its child nodes with the evaluated
 *     forms described above.  If an error occurs,
 *     globus_rsl_eval() returns a non-zero value.
 */
int
globus_rsl_eval (globus_rsl_t *ast_node, 
                 globus_symboltable_t * symbol_table)
{
    globus_rsl_t * tmp_rsl_ptr;
    globus_list_t * tmp_rsl_list;
    globus_list_t * tmp_value_list;
    globus_rsl_value_t * tmp_rsl_value_ptr;
    char * string_value;
    int rsl_substitution_flag = 0;

    if (globus_rsl_is_boolean(ast_node))
    {
        globus_symboltable_create_scope(symbol_table);

        tmp_rsl_list = globus_rsl_boolean_get_operand_list(ast_node);

        while (! globus_list_empty(tmp_rsl_list))
        {
            tmp_rsl_ptr = (globus_rsl_t *) globus_list_first
                 (tmp_rsl_list);

            if (globus_rsl_eval(tmp_rsl_ptr, symbol_table) != GLOBUS_SUCCESS)
            {
                return(1);
            }

            tmp_rsl_list = globus_list_rest(tmp_rsl_list);
        }

        globus_symboltable_remove_scope(symbol_table);

    }
    else if (globus_rsl_is_relation(ast_node))
    {
        tmp_value_list = globus_rsl_value_sequence_get_value_list(
                         globus_rsl_relation_get_value_sequence(ast_node));

        rsl_substitution_flag = globus_rsl_is_relation_attribute_equal(
                                   ast_node,
                                   "rsl_substitution");
        rsl_substitution_flag |= globus_rsl_is_relation_attribute_equal(
                                   ast_node,
                                   "rslsubstitution");

        while (! globus_list_empty(tmp_value_list))
        {
            tmp_rsl_value_ptr = (globus_rsl_value_t *) globus_list_first
                 (tmp_value_list);

            if (rsl_substitution_flag && 
                !globus_rsl_value_is_sequence(tmp_rsl_value_ptr) )
            {
               return(1);
            }

            if (globus_rsl_value_eval(tmp_rsl_value_ptr,
                            symbol_table,
                            &string_value,
                            rsl_substitution_flag) == GLOBUS_SUCCESS)
            {
                if (string_value != NULL)
                {
                    /* globus_list_replace_first returns the replaced 
                     * rsl_value_ptr, so in this case we want to free it up.
                     */
                    globus_rsl_value_free_recursive(
                        (globus_rsl_value_t *) globus_list_replace_first
                             (tmp_value_list,
                             (void *) globus_rsl_value_make_literal
                                  (string_value)));
                }
            }
            else
            {
               return(1);
            }

            tmp_value_list = globus_list_rest(tmp_value_list);
        }

    }
    else
    {
        return(1);
    }
    return(0);
}

/**
 * @defgroup globus_rsl_param RSL Value Accessors
 * @ingroup globus_rsl
 * @brief RSL Value Accessors
 */

/**
 * @brief Get the values of an RSL value list
 * @ingroup globus_rsl_param
 *
 * @details
 * The globus_rsl_value_list_param_get() function copies pointers to
 * literal string values or string pairs associated with the list of
 * globus_rsl_value_t pointers pointed to by the @a ast_node_list parameter
 * to the output array pointed to by the @a value parameter. It modifies
 * the value pointed to by the @a value_ctr parameter to be the number of
 * strings copied into the array.
 *
 * @param ast_node_list
 *     A pointer to a list of globus_rsl_value_t pointers whose values will
 *     be copied to the @a value parameter array.
 * @param required_type
 *     A flag indicating whether the list is expected to contain literal
 *     strings or string pairs. This value may be one of
 *     @a GLOBUS_RSL_VALUE_LITERAL or @a GLOBUS_RSL_VALUE_SEQUENCE.
 * @param value
 *     An output parameter pointing to an array of strings. This array must
 *     be at least as large as the number of elements in the list pointed to
 *     by @a ast_node_list.
 * @param value_ctr
 *     An output parameter pointing to an integer that will be incremented
 *     for each string copied into the @a value array.
 *
 * @return
 *     Upon success, the globus_rsl_value_list_param_get() function returns
 *     GLOBUS_SUCCESS and modifies the values pointed to by the @a value and
 *     @a value_ctr prameters as described above. If an error occurs,
 *     globus_rsl_value_list_param_get() returns a non-zero value.
 */
int
globus_rsl_value_list_param_get(globus_list_t * ast_node_list,
                                int required_type,
                                char *** value,
                                int * value_ctr)
{
    globus_rsl_value_t * tmp_rsl_value_ptr;
    globus_list_t * tmp_value_list;
    char * tmp_value;

    while (! globus_list_empty(ast_node_list))
    {
        tmp_rsl_value_ptr = (globus_rsl_value_t *) globus_list_first
             (ast_node_list);

        if (globus_rsl_value_is_literal(tmp_rsl_value_ptr) &&
            required_type == GLOBUS_RSL_VALUE_LITERAL)
        {
            tmp_value = globus_rsl_value_literal_get_string(tmp_rsl_value_ptr);
            (*value)[*value_ctr] = tmp_value;
            *value_ctr = *value_ctr + 1;
        }
        else if (globus_rsl_value_is_sequence(tmp_rsl_value_ptr) &&
                 required_type == GLOBUS_RSL_VALUE_SEQUENCE)
        {
            tmp_value_list = globus_rsl_value_sequence_get_value_list
                                (tmp_rsl_value_ptr);

            if (globus_list_size(tmp_value_list) != 2)
            {
                return(1);
            }

            if (globus_rsl_value_list_param_get(tmp_value_list,
                                                GLOBUS_RSL_VALUE_LITERAL,
                                                value,
                                                value_ctr) != 0)
            {
                return(1);
            }
        }
        else
        {
            /* if we are after a parameter the best thing to do with an
             * ast node other than a sequence or a literal is to ignore it.
             */
            return(1);
        }

        ast_node_list = globus_list_rest(ast_node_list);
    }
    return(0);
}

/**
 * @brief Get the list of values for an RSL attribute
 * @ingroup globus_rsl_param
 *
 * @details
 * The globus_rsl_param_get_values() function searches the RSL parse
 * tree pointed to by the @a ast_node parameter and returns the value list
 * that is bound to the attribute named by the @a param parameter. 
 * 
 *
 * @param ast_node
 *     A pointer to an RSL syntax tree that will be searched. This may be
 *     a relation or boolean RSL string.
 * @param param
 *     The name of the attribute to search for in the parse tree pointed to
 *     by the @a ast_node parameter.
 *
 * @return
 *     Upon success, the globus_rsl_param_get_values() function returns
 *     a pointer to the list of values associated with the attribute named
 *     by @a param in the RSL parse tree pointed to by @a ast_node. 
 *     If an error occurs, globus_rsl_param_get_values() returns NULL.
 */
globus_list_t *
globus_rsl_param_get_values(
    globus_rsl_t *			ast_node,
    char *				param)
{
    globus_rsl_t * tmp_rsl_ptr;
    globus_list_t * tmp_rsl_list;
    globus_list_t * values;

    if (globus_rsl_is_boolean(ast_node))
    {
        tmp_rsl_list = globus_rsl_boolean_get_operand_list(ast_node);

        while (! globus_list_empty(tmp_rsl_list))
        {
            tmp_rsl_ptr = (globus_rsl_t *) globus_list_first
                 (tmp_rsl_list);

	    values = globus_rsl_param_get_values(tmp_rsl_ptr, param);

	    if(values)
	    {
		return values;
	    }

            tmp_rsl_list = globus_list_rest(tmp_rsl_list);
        }
    }
    else if (globus_rsl_is_relation(ast_node))
    {
        if (!globus_rsl_is_relation_attribute_equal(ast_node, param))
        {
            return(0);
        }

        return globus_rsl_value_sequence_get_value_list(
                         globus_rsl_relation_get_value_sequence(ast_node));

    }
    else
    {
        return GLOBUS_NULL;
    }

    return GLOBUS_NULL;
}
/* globus_rsl_param_get_values() */

/**
 * @brief Get the value strings for an RSL attribute
 * @ingroup globus_rsl_param
 *
 * @details
 * The globus_rsl_param_get() function searches the RSL parse
 * tree pointed to by the @a ast_node parameter and returns an array of
 * pointers to the strings bound to the attribute named by the @a param
 * parameter.
 *
 * @param ast_node
 *     A pointer to an RSL syntax tree that will be searched. This may be
 *     a relation or boolean RSL string.
 * @param param_type
 *     A flag indicating what type of values are expected for the RSL
 *     attribute named by the @a param parameter. This flag value may be
 *     @a GLOBUS_RSL_PARAM_SINGLE_LITERAL, @a GLOBUS_RSL_PARAM_MULTI_LITERAL,
 *     or @a GLOBUS_RSL_PARAM_SEQUENCE.
 * @param param
 *     A string pointing to the name of of the RSL attribute to search for.
 * @param values
 *     An output parameter pointing to an array of strings that will be
 *     allocated and contain pointers to the RSL value strings if they
 *     match the format specified by the @a param_type flag. The caller is
 *     responsible for freeing this array, but not the strings in the array.
 *
 * @return
 *     Upon success, the globus_rsl_param_get() function returns
 *     @a GLOBUS_SUCCESS and modifies the @a values parameter as described
 *     above. If an error occurs, globus_rsl_param_get() returns a non-zero
 *     value.
 */
int
globus_rsl_param_get(globus_rsl_t * ast_node,
                     int param_type,
                     char * param,
                     char *** values)
{
    globus_rsl_t * tmp_rsl_ptr;
    globus_list_t * tmp_rsl_list;
    globus_list_t * tmp_value_list;
    int value_ctr = 0;
    int required_type;

    if (globus_rsl_is_boolean(ast_node))
    {
        tmp_rsl_list = globus_rsl_boolean_get_operand_list(ast_node);

        *values = (char **)globus_malloc(sizeof(char *));
        (*values)[0] = NULL;
 
        while (! globus_list_empty(tmp_rsl_list))
        {
            tmp_rsl_ptr = (globus_rsl_t *) globus_list_first
                 (tmp_rsl_list);

            if (globus_rsl_param_get(tmp_rsl_ptr,
                                     param_type,
                                     param,
                                     values) != 0)
            {
                return(1);
            }

            if ((*values)[0])
            {
                return(0);
            }

            tmp_rsl_list = globus_list_rest(tmp_rsl_list);
        }
    }
    else if (globus_rsl_is_relation(ast_node))
    {
        if (!globus_rsl_is_relation_attribute_equal(ast_node, param))
        {
            return(0);
        }

        tmp_value_list = globus_rsl_value_sequence_get_value_list(
                         globus_rsl_relation_get_value_sequence(ast_node));

        switch (param_type)
        {
            case GLOBUS_RSL_PARAM_SINGLE_LITERAL:
                /* only 1 literal value.
                 * this is the catch all.
                 */
                if (globus_list_size(tmp_value_list) != 1)
                    return(1);
                required_type = GLOBUS_RSL_VALUE_LITERAL;
                break;
            case GLOBUS_RSL_PARAM_MULTI_LITERAL:
                /* only literal value(s)
                 * for example a list of arguments
                 */
                required_type = GLOBUS_RSL_VALUE_LITERAL;
                break;
            case GLOBUS_RSL_PARAM_SEQUENCE:
                /* only sequence(s).  The list of each sequence must be
                 * made up of 2 literals only!
                 * for example var/value pairs for environment and 
                 * rsl_substitution. 
                 */
                required_type = GLOBUS_RSL_VALUE_SEQUENCE;
                break;
            default:
                return(1);
        }
        
        *values = (char **)globus_malloc(sizeof(char *) *
                                         (globus_list_size (tmp_value_list) *
                                         2 + 1));

        if (globus_rsl_value_list_param_get(tmp_value_list,
                                            required_type,
                                            values,
                                            &value_ctr) != 0)
        {
            return(1);
        }

        (*values)[value_ctr] = NULL;

        return(0);
    }
    else
    {
        return(1);
    }

    return(0);
}

int lvl = 0;

/* for printing RSL tree */
#define INDENT(LVL) { \
    int i; \
    for (i = 0; i < (LVL); i ++) printf("  "); \
    }

/**
 * @defgroup globus_rsl_print RSL Display
 * @ingroup globus_rsl
 * @brief RSL Display
 */

/**
 * @brief Print the value of a globus_rsl_value_t to standard output
 * @ingroup globus_rsl_print
 *
 * @details
 * The globus_rsl_value_print_recursive() function prints a string
 * representation of the RSL value node pointed to by the
 * @a globus_rsl_value_ptr parameter to standard output. This function
 * is not reentrant.
 * 
 * @param globus_rsl_value_ptr
 *     A pointer to the RSL value to display.
 * 
 * @return
 *     The globus_rsl_value_print_recursive() function always returns
 *     @a GLOBUS_SUCCESS.
 */
int
globus_rsl_value_print_recursive (globus_rsl_value_t * globus_rsl_value_ptr)
{
    globus_rsl_value_t * tmp_rsl_value_ptr;
    globus_list_t * tmp_rsl_list;

    if (globus_rsl_value_ptr==NULL) return(0);

    lvl++;

    switch (globus_rsl_value_ptr->type)
    {
        case GLOBUS_RSL_VALUE_LITERAL:

            if (globus_rsl_value_ptr->value.literal.string == NULL)
            {
                INDENT(lvl) printf("LITERAL string = >NULL<\n");
            }
            else
            {
                INDENT(lvl) printf("LITERAL string = %s\n", 
                    globus_rsl_value_ptr->value.literal.string);
            }
            break;

        case GLOBUS_RSL_VALUE_SEQUENCE:

            INDENT(lvl) printf("SEQUENCE\n");

            tmp_rsl_list = globus_rsl_value_ptr->value.sequence.value_list;

            while (! globus_list_empty(tmp_rsl_list))
            {
                tmp_rsl_value_ptr = (globus_rsl_value_t *) globus_list_first
                     (tmp_rsl_list);
                globus_rsl_value_print_recursive(tmp_rsl_value_ptr);

                tmp_rsl_list = globus_list_rest(tmp_rsl_list);
            }

            break;

        case GLOBUS_RSL_VALUE_VARIABLE:

            INDENT(lvl) printf("VARIABLE\n");
            globus_rsl_value_print_recursive
                  (globus_rsl_value_ptr->value.variable.sequence);
            break;

        case GLOBUS_RSL_VALUE_CONCATENATION:

            INDENT(lvl) printf("CONCATENATION (left)\n");

            globus_rsl_value_print_recursive
                  (globus_rsl_value_ptr->value.concatenation.left_value);
            INDENT(lvl) printf("CONCATENATION (right)\n");
            globus_rsl_value_print_recursive
                  (globus_rsl_value_ptr->value.concatenation.right_value);
            break;

        default:

            break;
    }

    lvl--;

    return(0);
}

/**
 * @brief Get the string representation of an RSL operator
 * @ingroup globus_rsl_print
 *
 * @details
 * The globus_rsl_get_operator() function returns a pointer to a
 * static string that represents the RSL operator passed in via the
 * @a my_op parameter. If the operator is not value, then 
 * globus_rsl_get_operator() returns a pointer to the string "??"
 * 
 * @param my_op
 *     The RSL operator to return.
 * 
 * @return
 *     The globus_rsl_get_operator() function returns a pointer to the
 *     string representation of the @a my_op parameter, or "??" if that value
 *     is not a value RSL operator.
 */
char * globus_rsl_get_operator(int my_op)
{
    switch (my_op)
    {
         case GLOBUS_RSL_EQ:        return ("=");
         case GLOBUS_RSL_NEQ:       return ("!=");
         case GLOBUS_RSL_GT:        return (">");
         case GLOBUS_RSL_GTEQ:      return (">=");
         case GLOBUS_RSL_LT:        return ("<");
         case GLOBUS_RSL_LTEQ:      return ("<=");
         case GLOBUS_RSL_AND:       return ("&");
         case GLOBUS_RSL_OR:        return ("|");
         case GLOBUS_RSL_MULTIREQ:  return ("+");
         default:                   return ("??");
    }
}

/**
 * @brief Print the value of an RSL syntax tree to standard output
 * @ingroup globus_rsl_print
 *
 * @details
 * The globus_rsl_print_recursive() function prints a string
 * representation of the RSL syntax tree pointed to by the
 * @a ast_node parameter to standard output. This function
 * is not reentrant.
 * 
 * @param ast_node
 *     A pointer to the RSL syntax tree to display.
 * 
 * @return
 *     The globus_rsl_print_recursive() function always returns
 *     @a GLOBUS_SUCCESS.
 */
int
globus_rsl_print_recursive (globus_rsl_t *ast_node)
{
    globus_list_t * tmp_rsl_list;
    globus_rsl_t * tmp_rsl_ptr;

    if (globus_rsl_is_boolean(ast_node))
    {
        printf("\nBOOLEAN\n");
        printf("  operator = %s\n", 
                globus_rsl_get_operator(ast_node->req.boolean.my_operator));

        tmp_rsl_list = ast_node->req.boolean.operand_list;

        while (! globus_list_empty(tmp_rsl_list))
        {
            tmp_rsl_ptr = (globus_rsl_t *) globus_list_first
                 (tmp_rsl_list);
            globus_rsl_print_recursive(tmp_rsl_ptr);

            tmp_rsl_list = globus_list_rest(tmp_rsl_list);
        }
    }
    else
    {
        printf("\n  RELATION\n");
        printf("    attibute name = %s\n", 
            ast_node->req.relation.attribute_name);
        printf("    operator = %s\n", 
                globus_rsl_get_operator(ast_node->req.boolean.my_operator));

        lvl++;
        globus_rsl_value_print_recursive
                (ast_node->req.relation.value_sequence);
        lvl--;
    }
    return(0);
}

/**
 * @brief Convert an RSL parse tree to a string
 * @ingroup globus_rsl_print
 *
 * @details
 * The globus_rsl_unparse() function returns a new string which
 * can be parsed into the RSL syntax tree passed as the @a rsl_spec
 * parameter. The caller is responsible for freeing this string.
 * 
 * @param rsl_spec
 *     A pointer to the RSL syntax tree to unparse.
 * 
 * @return
 *     Upon success, the globus_rsl_unparse() function returns a new
 *     string which represents the RSL parse tree passed as the @a rsl_spec
 *     parameter. If an error occurs, globus_rsl_unparse() returns NULL.
 */
char *
globus_rsl_unparse (globus_rsl_t *rsl_spec)
{
  int             err;
  globus_fifo_t   buffer;
  int             size;
  char          * char_buffer;

  globus_fifo_init (&buffer);
  err = globus_i_rsl_unparse_to_fifo (rsl_spec, &buffer);

  if (err) {
    char_buffer = NULL;
    goto unparse_exit;
  }

  size = globus_fifo_size (&buffer);
  char_buffer = globus_malloc (sizeof(char) * (size + 1));

  if ( char_buffer != NULL ) {
    int i;

    for (i=0; (i<size) && (!globus_fifo_empty (&buffer)); i++) {
      char_buffer[i] = (char) (long) globus_fifo_dequeue (&buffer);
    }
    char_buffer[size] = '\0';
  }

unparse_exit:
  globus_fifo_destroy (&buffer);
  return char_buffer;
}

/**
 * @brief Convert an RSL value pointer to a string
 * @ingroup globus_rsl_print
 *
 * @details
 * The globus_rsl_value_unparse() function returns a new string which
 * can be parsed into the value of an RSL relation that has the same
 * syntactic meaning as the @a rsl_value parameter.
 * The caller is responsible for freeing this string.
 * 
 * @param rsl_value
 *     A pointer to the RSL value node to unparse.
 * 
 * @return
 *     Upon success, the globus_rsl_value_unparse() function returns a new
 *     string which represents the RSL value ndoe passed as the @a rsl_value
 *     parameter. If an error occurs, globus_rsl_value_unparse() returns
 *     NULL.
 */
char *
globus_rsl_value_unparse (globus_rsl_value_t * rsl_value)
{
  int             err;
  globus_fifo_t   buffer;
  int             size;
  char          * char_buffer;

  globus_fifo_init (&buffer);

  err = globus_i_rsl_value_unparse_to_fifo (rsl_value, &buffer);

  if (err) {
    char_buffer = NULL;
    goto unparse_exit;
  }

  size = globus_fifo_size (&buffer);
  char_buffer = globus_malloc (sizeof(char) * (size + 1));

  if ( char_buffer != NULL ) {
    int i;

    for (i=0; (i<size) && (!globus_fifo_empty (&buffer)); i++) {
      char_buffer[i] = (char) (long) globus_fifo_dequeue (&buffer);
    }
    char_buffer[size] = '\0';
  }

unparse_exit:
  globus_fifo_destroy (&buffer);
  return char_buffer;
}

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
static int
globus_i_rsl_unparse_string_literal_to_fifo (const char    * string,
                                             globus_fifo_t * bufferp)
{
  int    i;

  if ( string == NULL ) return 1;

  globus_fifo_enqueue (bufferp, (void *) '"');

  for (i=0; string[i] != '\0'; i++) {
    if ( string[i] == '"' ) {
      /* escape '"' characters */
      globus_fifo_enqueue (bufferp, (void *) '"');
      globus_fifo_enqueue (bufferp, (void *) '"');
    }
    else {
      globus_fifo_enqueue (bufferp, (void *) (intptr_t) string[i]);
    }
  }

  globus_fifo_enqueue (bufferp, (void *) '"');

  return 0;
}

static int
globus_i_rsl_unparse_operator_to_fifo (int             operator,
                                       globus_fifo_t * bufferp)
{
  switch ( operator ) {
  case GLOBUS_RSL_EQ:
    globus_fifo_enqueue (bufferp, (void *) '=');
    break;

  case GLOBUS_RSL_NEQ:
    globus_fifo_enqueue (bufferp, (void *) '!');
    globus_fifo_enqueue (bufferp, (void *) '=');
    break;

  case GLOBUS_RSL_GT:
    globus_fifo_enqueue (bufferp, (void *) '>');
    break;

  case GLOBUS_RSL_GTEQ:
    globus_fifo_enqueue (bufferp, (void *) '>');
    globus_fifo_enqueue (bufferp, (void *) '=');
    break;

  case GLOBUS_RSL_LT:
    globus_fifo_enqueue (bufferp, (void *) '<');
    break;

  case GLOBUS_RSL_LTEQ:
    globus_fifo_enqueue (bufferp, (void *) '<');
    globus_fifo_enqueue (bufferp, (void *) '=');
    break;

  case GLOBUS_RSL_MULTIREQ:
    globus_fifo_enqueue (bufferp, (void *) '+');
    break;
    
  case GLOBUS_RSL_AND:
    globus_fifo_enqueue (bufferp, (void *) '&');
    break;

  case GLOBUS_RSL_OR:
    globus_fifo_enqueue (bufferp, (void *) '|');
    break;

  default:
    return 1;
  }

  return 0;
}

static int
globus_i_rsl_unparse_to_fifo (globus_rsl_t  * ast,
                            globus_fifo_t * bufferp)
{
  int err;

  if ( ast == NULL ) return 1;
  else if ( globus_rsl_is_relation (ast) ) {
    globus_list_t * values;

    err = globus_i_rsl_unparse_string_literal_to_fifo (
                               globus_rsl_relation_get_attribute (ast),
                               bufferp);
    if ( err ) return 1;

    globus_fifo_enqueue (bufferp, (void *) ' ');

    globus_i_rsl_unparse_operator_to_fifo (
                           globus_rsl_relation_get_operator (ast),
                           bufferp);

    globus_fifo_enqueue (bufferp, (void *) ' ');

    values = globus_rsl_value_sequence_get_value_list (
                               globus_rsl_relation_get_value_sequence (ast));
    while (! globus_list_empty (values)) {
      err = globus_i_rsl_value_unparse_to_fifo (((globus_rsl_value_t *)
                                                 globus_list_first (values)),
                                                bufferp);
      if (err) return 1;

      globus_fifo_enqueue (bufferp, (void *) ' ');

      values = globus_list_rest (values);
    }

    return 0;
  }
  else if ( globus_rsl_is_boolean (ast) ) {
    globus_list_t * operands;

    globus_i_rsl_unparse_operator_to_fifo (
                           globus_rsl_boolean_get_operator (ast),
                           bufferp);

    operands = globus_rsl_boolean_get_operand_list (ast);
    while (! globus_list_empty (operands)) {
      globus_fifo_enqueue (bufferp, (void *) '(');

      err = globus_i_rsl_unparse_to_fifo (((globus_rsl_t *)
                                           globus_list_first (operands)),
                                          bufferp);
      if (err) return 1;

      globus_fifo_enqueue (bufferp, (void *) ')');

      operands = globus_list_rest (operands);
    }

    return 0;
  }
  else return 1;
}

static int
globus_i_rsl_value_unparse_to_fifo (globus_rsl_value_t * ast,
                                    globus_fifo_t      * bufferp)
{
  int err;

  if ( ast == NULL ) return 1;
  else if ( globus_rsl_value_is_literal (ast) ) {
    globus_i_rsl_unparse_string_literal_to_fifo (
                                 globus_rsl_value_literal_get_string (ast),
                                 bufferp);
    return 0;
  }
  else if ( globus_rsl_value_is_sequence (ast) ) {
    globus_list_t * values;

    globus_fifo_enqueue (bufferp, (void *) '(');

    values = globus_rsl_value_sequence_get_value_list (ast);
    while (! globus_list_empty (values)) {
      err = globus_i_rsl_value_unparse_to_fifo (((globus_rsl_value_t *)
                                                 globus_list_first (values)),
                                                bufferp);
      if (err) return 1;

      globus_fifo_enqueue (bufferp, (void *) ' ');

      values = globus_list_rest (values);
    }

    globus_fifo_enqueue (bufferp, (void *) ')');

    return 0;
  }
  else if ( globus_rsl_value_is_variable (ast) ) {
    globus_fifo_enqueue (bufferp, (void *) '$');
    globus_fifo_enqueue (bufferp, (void *) '(');

    err = globus_i_rsl_unparse_string_literal_to_fifo (
                               globus_rsl_value_variable_get_name (ast),
                               bufferp);
    if (err) return 1;

    if ( globus_rsl_value_variable_get_default (ast) != NULL ) {
      globus_fifo_enqueue (bufferp, (void *) ' ');

      err = globus_i_rsl_unparse_string_literal_to_fifo (
                                 globus_rsl_value_variable_get_default (ast),
                                 bufferp);
      if (err) return 1;
    }

    globus_fifo_enqueue (bufferp, (void *) ')');
    return 0;
  }
  else if ( globus_rsl_value_is_concatenation (ast) ) {
    err = globus_i_rsl_value_unparse_to_fifo (
                              globus_rsl_value_concatenation_get_left (ast),
                              bufferp);
    if (err) return 1;

    globus_fifo_enqueue (bufferp, (void *) ' ');
    globus_fifo_enqueue (bufferp, (void *) '#');
    globus_fifo_enqueue (bufferp, (void *) ' ');

    err = globus_i_rsl_value_unparse_to_fifo (
                              globus_rsl_value_concatenation_get_right (ast),
                              bufferp);
    if (err) return 1;

    return 0;
  }
  else return 1;
}
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
