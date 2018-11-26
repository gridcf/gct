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
 * @file globus_priority_q.h
 * @brief Priority Queue
 */

#ifndef GLOBUS_PRIORITY_Q_H
#define GLOBUS_PRIORITY_Q_H

#include "globus_types.h"
#include "globus_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_priority_q Priority Queue
 * @ingroup globus_common
 * @brief Priority Queue
 *
 * @details
 * This module defines a priority queue for globus.
 * It is implemented using a binary heap (minheap) and does NOT have
 * a fifo fallback for like priorities.  If you need fifo fallback,
 * you should use a compound priority with the primary priority being
 * the 'real' priority and the secondary being a serial number.
 *
 * To use this priority queue type, define a comparison function of type
 * globus_priority_q_cmp_func_t and pass that to globus_priority_q_init().
 * 
 * To add and remove items in priority order, use
 * globus_priority_q_enqueue() and globus_priority_q_dequeue() respectively.
 *
 * To remove a datum ignoring its priority, use globus_priority_q_remove().
 *
 * To inspect the first element and its priority, use globus_priority_q_first()
 * and globus_priority_q_first_priority() respectively.
 *
 * To determine whether a queue is empty or the number of data in it, use
 * globus_priority_q_empty() and globus_priority_q_size().
 *
 * To modify the priority of a datum already in the queue, use
 * globus_priority_q_modify().
 *
 * When finished with the queue, use globus_priority_q_destroy() to free data
 * associated with the priority queue.
 */
 
/**
 * @brief Priority Comparison Predicate
 * @ingroup globus_priority_q
 * @details
 * This type is used to implement comparison of two priorities for inserting
 * items into the priority queue. A function of this type is passed to 
 * globus_priority_q_init() to determine how priorities are computed in a newly
 * created priority queue.
 *
 * @param priority_1
 *     First priority to compare
 * @param priority_2
 *     Second priority to compare
 *
 * @retval > 0
 *     The priority of priority_1 is less than that of priority_2.
 * @retval < 0
 *     The priority of priority_1 is greater than that of priority_2.
 * @retval = 0
 *     The priorities of priority_1 and priority_2 are the same.
 */
typedef int (*globus_priority_q_cmp_func_t)(
    void *                                  priority_1,
    void *                                  priority_2);

/**
 * @brief Priority Queue Structure
 * @ingroup globus_priority_q
 * @details
 * A pointer to a structure of this type is passed to all functions in the
 * @link globus_priority_q Priority Queue @endlink module. It is not intended
 * to be inspected or modified outside of this API.
 */
typedef struct globus_priority_q_s
{
    struct globus_l_priority_q_entry_s **   heap;
    int                                     next_slot;
    size_t                                  max_len;
    globus_memory_t                         memory;
    globus_priority_q_cmp_func_t            cmp_func;
} globus_priority_q_t;

int
globus_priority_q_init(
    globus_priority_q_t *               priority_q,
    globus_priority_q_cmp_func_t        cmp_func);

int
globus_priority_q_destroy(
    globus_priority_q_t *               priority_q);

globus_bool_t
globus_priority_q_empty(
    globus_priority_q_t *               priority_q);

int
globus_priority_q_size(
    globus_priority_q_t *               priority_q);

int
globus_priority_q_enqueue(
    globus_priority_q_t *               priority_q,
    void *                              datum,
    void *                              priority);

void *
globus_priority_q_remove(
    globus_priority_q_t *               priority_q,
    void *                              datum);

/*
 * it is acceptable to modify the priority already stored within the queue
 * before making this call.  The old priority is not looked at
 */
void *
globus_priority_q_modify(
    globus_priority_q_t *               priority_q,
    void *                              datum,
    void *                              new_priority);

void *
globus_priority_q_dequeue(
    globus_priority_q_t *               priority_q);

void *
globus_priority_q_first(
    globus_priority_q_t *               priority_q);

void *
globus_priority_q_first_priority(
    globus_priority_q_t *               priority_q);


#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_PRIORITY_Q_H */
