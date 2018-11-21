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
 * @file globus_list.h
 * @brief Linked List
 */

/**
 * @defgroup globus_list Linked List
 * @ingroup globus_common
 * @brief Linked List
 */
#ifndef GLOBUS_LIST_H
#define GLOBUS_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief List data type
 * @ingroup globus_list
 * @param
 * A structure representing a node containing a single datum and a reference to
 * additional elements in the list.
 *
 * The special value NULL is used to represent a list with zero elements, also
 * called an empty list.
 */
typedef struct globus_list 
{
    void * volatile                        datum;
    struct globus_list * volatile          next;
    int                                    malloced;
} globus_list_t;

/**
 * @brief List search predicate
 * @ingroup globus_list
 * @details
 * An anonymous predicate that evaluates a datum as true or false in the
 * context of user-provided arg. These predicates are used for example in
 * general searches with globus_list_search_pred(), and the arg field is used
 * to implement in C something approximating closures in a functional language
 * by encapsulating instance-specific search criteria.
 *
 * These predicates return non-zero for truth and zero for falsity so they can
 * be used in C conditional expressions.
 * @param datum
 *     Datum of the list item to compute the predicate against.
 * @param arg
 *     Parameter supplied to globus_list_search_pred()
 */
typedef int (*globus_list_pred_t) (void *datum, void *arg);

/**
 * @brief Relation predicate
 * @ingroup globus_list
 * @details
 * An anonymous predicate that defines a partial ordering of data. Such
 * ordering predicates evaluate true if low_datum is <em>less than</em>
 * (or <em>comes before</em>) high_datum in the ordering, and false otherwise.
 * These predicates are used for example in general sorts with
 * globus_list_sort(), and the relation_arg field is used to implement in C
 * something approximating closures in a functional language by encapsulating
 * instance-specific ordering criteria.
 *
 * These predicates return non-zero for truth and zero for falsity so they can
 * be used in C conditional expressions.
 * @param low_datum
 *     Datum to compare
 * @param high_datum
 *     Datum to compare
 * @param relation_arg
 *     Parameter supplied to globus_list_sort()
 */
typedef int (*globus_list_relation_t) (void *low_datum, void *high_datum,
				       void *relation_arg);
int
globus_i_list_pre_activate(void);

extern int
globus_list_int_less (void * low_datum, void * high_datum,
		      void * ignored);

extern int 
globus_list_empty (globus_list_t * head);

globus_list_t *
globus_list_concat(
    globus_list_t *                     front_list,
    globus_list_t *                     back_list);

extern void *
globus_list_first (globus_list_t * head);

extern globus_list_t *
globus_list_rest (globus_list_t * head);

extern globus_list_t **
globus_list_rest_ref (globus_list_t * head);

extern int 
globus_list_size (globus_list_t * head);

extern void *
globus_list_replace_first (globus_list_t * head, void *datum);

extern globus_list_t *
globus_list_search (globus_list_t * head, void * datum);

extern globus_list_t *
globus_list_search_pred (globus_list_t * head, 
			 globus_list_pred_t predicate,
			 void * pred_args);

extern globus_list_t *
globus_list_min (globus_list_t * head,
		 globus_list_relation_t relation,
		 void * relation_args);

extern globus_list_t *
globus_list_sort_destructive (globus_list_t * head,
			      globus_list_relation_t relation,
			      void *relation_args);

extern void
globus_list_halves_destructive (globus_list_t * head,
				globus_list_t * volatile * left_halfp,
				globus_list_t * volatile * right_halfp);

extern globus_list_t *
globus_list_sort_merge_destructive (globus_list_t *left,
				    globus_list_t *right,
				    globus_list_relation_t relation,
				    void * relation_args);

extern globus_list_t *
globus_list_sort (globus_list_t *head,
		  globus_list_relation_t relation,
		  void *relation_args);

extern int 
globus_list_insert (globus_list_t * volatile *headp, void *datum);

extern globus_list_t *
globus_list_cons (void * datum, globus_list_t *list);

extern globus_list_t *
globus_list_copy (globus_list_t *head);

extern void *
globus_list_remove (globus_list_t * volatile *headp, globus_list_t *entry);

extern void
globus_list_free (globus_list_t *head);

void globus_list_destroy_all(
    globus_list_t *                     head,
    void                                (*data_free)(void *));

extern globus_list_t *
globus_list_from_string(
    const char *                        in_string,
    int                                 delim,
    const char *                        ignored);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_LIST_H */
