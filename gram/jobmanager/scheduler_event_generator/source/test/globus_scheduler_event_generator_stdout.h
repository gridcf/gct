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
#ifndef GLOBUS_SCHEDULER_EVENT_GENERATOR_STDOUT_H
#define GLOBUS_SCHEDULER_EVENT_GENERATOR_STDOUT_H

#include "globus_common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern globus_module_descriptor_t
globus_i_scheduler_event_generator_stdout_module;

#define GLOBUS_SCHEDULER_EVENT_GENERATOR_STDOUT_MODULE \
        (&globus_i_scheduler_event_generator_stdout_module)

extern 
globus_result_t
globus_scheduler_event_generator_stdout_handler(
    void *                              arg,
    const globus_scheduler_event_t *    event);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_SCHEDULER_EVENT_GENERATOR_H */
