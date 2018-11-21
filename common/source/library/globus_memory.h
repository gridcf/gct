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
 * @file globus_memory.h
 * @brief Memory Pool
 */

#if !defined(GLOBUS_MEMORY_H)
#define GLOBUS_MEMORY_H 

/******************************************************************************
			     Include header files
******************************************************************************/
#include "globus_types.h"
#include "globus_module.h"
#include "globus_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_memory Memory Pool
 * @ingroup globus_common
 * @brief Memory Pool
 *
 * @details
 * The globus_memory abstraction implements a memory management pool for
 * groups of same-sized data items.
 */
/******************************************************************************
			       Type definitions
******************************************************************************/

struct globus_memory_s;
typedef struct globus_memory_s *                globus_memory_t;

globus_bool_t
globus_i_memory_pre_activate();

globus_bool_t
globus_memory_init(
    globus_memory_t *             mem_info,
    int                           node_size,
    int                           node_count);

void *
globus_memory_pop_node(
    globus_memory_t *                           mem_info);

globus_bool_t
globus_memory_push_node(
    globus_memory_t *          mem_info,
    void *                      buf);

globus_bool_t
globus_memory_destroy(
    globus_memory_t *                           mem_info);


#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_MEMORY_H */
