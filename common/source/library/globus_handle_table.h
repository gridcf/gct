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
 * @file globus_handle_table.h
 * @brief Handle Table for Reference Counting Data
 */

/**
 * @defgroup globus_handle_table Handle Table for Reference Counting Data
 * @ingroup globus_common
 * @brief Handle Table for Reference Counting Data
 *
 * @details
 * The globus_handle_table_t abstraction provides a reference-counting
 * handle table to automatically free data when there are no more
 * references to it. Each datum in the globus_handle_table_t container
 * has a count associated with it which may be incremented and decremented
 * in single steps or by an increment. While a handle has any references to
 * it, the globus_handle_table_lookup() will return the datum associated
 * with the handle, otherwise it will return NULL. The value of a
 * globus_handle_t is not reused until INT_MAX data have been inserted into
 * the handle table.
 */
#ifndef GLOBUS_HANDLE_TABLE_H
#define GLOBUS_HANDLE_TABLE_H

#include "globus_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle table abstract type
 * @ingroup globus_handle_table
 */
typedef struct globus_l_handle_table_s * globus_handle_table_t;

/**
 * @brief Handle abstract type
 * @ingroup globus_handle_table
 */
typedef int globus_handle_t;

/**
 * @brief Handle datum destructor
 * @ingroup globus_handle_table
 * @param datum
 *     Datum to destroy
 */
typedef 
void 
(*globus_handle_destructor_t)(
    void *                              datum);

/**
 * Invalid handle value
 * @ingroup globus_handle_table
 * @hideinitializer
 */
#define GLOBUS_NULL_HANDLE 0
#define GLOBUS_HANDLE_TABLE_NO_HANDLE 0

int
globus_handle_table_init(
    globus_handle_table_t *             handle_table,
    globus_handle_destructor_t          destructor);

int
globus_handle_table_destroy(
    globus_handle_table_t *             handle_table);

globus_handle_t
globus_handle_table_insert(
    globus_handle_table_t *             handle_table,
    void *                              datum,
    int                                 initial_refs);

globus_bool_t
globus_handle_table_increment_reference(
    globus_handle_table_t *             handle_table,
    globus_handle_t                     handle);

globus_bool_t
globus_handle_table_increment_reference_by(
    globus_handle_table_t *             handle_table,
    globus_handle_t                     handle,
    unsigned int                        inc);

globus_bool_t
globus_handle_table_decrement_reference(
    globus_handle_table_t *             handle_table,
    globus_handle_t                     handle);

void *
globus_handle_table_lookup(
    globus_handle_table_t *             handle_table,
    globus_handle_t                     handle);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_HANDLE_TABLE_H */
