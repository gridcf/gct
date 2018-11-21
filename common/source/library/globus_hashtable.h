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
 * @file globus_hashtable.h
 * @brief Hash Table
 */

#ifndef GLOBUS_HASHTABLE_H
#define GLOBUS_HASHTABLE_H

/**
 * @defgroup globus_hashtable Hash Table
 * @ingroup globus_common
 * @brief Hash Table
 *
 * The globus_hashtable data type provides an abstract hashtable mapping
 * representation and operations on such mappings. These queues can contain
 * arbitrary data in the form of a void pointer for each key and a void pointer
 * for each datum. It is the user's responsibility to provide and interpret
 * keys and data of the correct type.
 */

#include "globus_types.h"
#include "globus_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup globus_hashtable
 * An anonymous hash function providing an onto mapping of (key, limit) pairs
 * to integers, where the result integer is in the range [ 0, limit - 1 ] .
 * *
 * Note that as a proper function, such hash routines must always compute the
 * same result given the same key and limit value.
 * @param key
 *     Value to map
 * @param limit
 *     Map range limit
 * @return Integer hash value of key
 */
typedef int
(*globus_hashtable_hash_func_t)(
    void *                              key,
    int                                 limit);

/**
 * @ingroup globus_hashtable
 * An anonymous predicate that returns true when the keys are equal and false
 * otherwise.
 * Truth and falsity are represented by non-zero and zero (0) integers for use
 * directly in C language conditionals.
 */
typedef int
(*globus_hashtable_keyeq_func_t)(
    void *                              key1,
    void *                              key2);

/**
 * @ingroup globus_hashtable
 * datum copy func
 */
typedef void
(*globus_hashtable_copy_func_t)(
    void **                             dest_key,
    void **                             dest_datum,
    void *                              src_key,
    void *                              src_datum);


/**
 * @ingroup globus_hashtable
 * Destructor callback for use with globus_hashtable_destroy_all
 */
typedef void
(*globus_hashtable_destructor_func_t)(
    void *                              datum);

typedef struct globus_l_hashtable_s *     globus_hashtable_t;

int
globus_hashtable_init(
    globus_hashtable_t *                table,
    int                                 size,
    globus_hashtable_hash_func_t        hash_func,
    globus_hashtable_keyeq_func_t       keyeq_func);

int
globus_hashtable_copy(
    globus_hashtable_t *                dest_table,
    globus_hashtable_t *                src_table,
    globus_hashtable_copy_func_t        copy_func);
    
void *
globus_hashtable_lookup(
    globus_hashtable_t *                table,
    void *                              key);

int
globus_hashtable_insert(
    globus_hashtable_t *                table,
    void *                              key,
    void *                              datum);

void *
globus_hashtable_update(
    globus_hashtable_t *                table,
    void *                              key,
    void *                              datum);

void *
globus_hashtable_remove(
    globus_hashtable_t *                table,
    void *                              key);

int
globus_hashtable_to_list(
    globus_hashtable_t *                table,
    globus_list_t **                    list);

globus_bool_t
globus_hashtable_empty(
    globus_hashtable_t *                table);

int
globus_hashtable_size(
    globus_hashtable_t *                table);

/**
 * @defgroup globus_hashtable_iterators Iterators
 * @ingroup globus_hashtable
 * @brief Iterators
 *
 * @details
 * The iterator is initially NULL until one of 
 * globus_hashtable_first() or globus_hashtable_last() has been called.  All
 * other calls have no effect on iterator except for globus_hashtable_remove().
 * If the iterator points at the entry being removed, the iterator is moved
 * to the next entry.
 * 
 * Once an 'end' has been reached with globus_hashtable_next() or
 * globus_hashtable_prev(), the iterator must again be reset with 
 * globus_hashtable_first() or globus_hashtable_last() before being used.
 */
void *
globus_hashtable_first(
    globus_hashtable_t *                table);

void *
globus_hashtable_next(
    globus_hashtable_t *                table);

void *
globus_hashtable_last(
    globus_hashtable_t *                table);

void *
globus_hashtable_prev(
    globus_hashtable_t *                table);

int
globus_hashtable_destroy(
    globus_hashtable_t *                table);
    
void
globus_hashtable_destroy_all(
    globus_hashtable_t *                table,
    globus_hashtable_destructor_func_t  element_free);

int
globus_hashtable_string_hash(
    void *                              string,
    int                                 limit);

int
globus_hashtable_string_keyeq(
    void *                              string1,
    void *                              string2);

int
globus_hashtable_voidp_hash(
    void *                              voidp,
    int                                 limit);

int
globus_hashtable_voidp_keyeq(
    void *                              voidp1,
    void *                              voidp2);

int
globus_hashtable_int_hash(
    void *                              integer,
    int                                 limit);

int
globus_hashtable_int_keyeq(
    void *                              integer1,
    void *                              integer2);

int
globus_hashtable_ulong_hash(
    void *                              integer,
    int                                 limit);

int
globus_hashtable_ulong_keyeq(
    void *                              integer1,
    void *                              integer2);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_HASHTABLE_H */
