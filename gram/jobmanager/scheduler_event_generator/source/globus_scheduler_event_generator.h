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

#ifndef GLOBUS_SCHEDULER_EVENT_GENERATOR_H
#define GLOBUS_SCHEDULER_EVENT_GENERATOR_H

#include "globus_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLOBUS_GLOBAL_DOCUMENT_SET
/**
 * @mainpage Scheduler Event Generator
 * @copydoc globus_scheduler_event_generator
 */
#endif

/**
 * @defgroup globus_scheduler_event_generator Scheduler Event Generator
 * @brief Scheduler Event Generator
 *
 * @details
 * The Globus Scheduler Event Generator is a service for processing job state
 * change information from a variety of local resource managers so that
 * they can be used by the GRAM5 service.
 *
 * The API for implementing additional resource manager interfaces is described
 * in the @ref globus_scheduler_event_generator_api "API documentation page"
 */

/** @ingroup globus_scheduler_event_generator_api
 * Error types used by the SEG.
 */
typedef enum
{
    /** NULL Parameter */
    GLOBUS_SEG_ERROR_TYPE_NULL = 1024,
    /** Already called a one-time function */
    GLOBUS_SEG_ERROR_TYPE_ALREADY_SET,
    /** Shared module missing descriptor */
    GLOBUS_SEG_ERROR_TYPE_INVALID_MODULE,
    /** Invalid printf format for SEG protocol message */
    GLOBUS_SEG_ERROR_TYPE_INVALID_FORMAT,
    /** Out of memory */
    GLOBUS_SEG_ERROR_TYPE_OUT_OF_MEMORY,
    /** Unable to load scheduler module */
    GLOBUS_SEG_ERROR_TYPE_LOADING_MODULE
}
globus_scheduler_event_generator_error_t;

#if __STDC_VERSION__ == 199901L
#    define MYNAME __func__
#elif defined(__GNUC__)
#    define MYNAME __FUNCTION__
#else
#    define MYNAME ""
#endif

typedef void (*globus_scheduler_event_generator_fault_t)(
    void *                              user_arg,
    globus_result_t                     fault);

#define GLOBUS_SEG_ERROR_NULL_OBJECT() \
    globus_error_construct_error(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE, \
            NULL, \
            GLOBUS_SEG_ERROR_TYPE_NULL, \
            __FILE__, \
            MYNAME, \
            __LINE__, \
            "Null parameter")

#define GLOBUS_SEG_ERROR_ALREADY_SET_OBJECT() \
    globus_error_construct_error(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE, \
            NULL, \
            GLOBUS_SEG_ERROR_TYPE_ALREADY_SET, \
            __FILE__, \
            MYNAME, \
            __LINE__, \
            "Value already set")
    
#define GLOBUS_SEG_ERROR_INVALID_MODULE_OBJECT(module, errmsg) \
    globus_error_construct_error(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE, \
            NULL, \
            GLOBUS_SEG_ERROR_TYPE_INVALID_MODULE, \
            __FILE__, \
            MYNAME, \
            __LINE__, \
            "Invalid module %s: %s", \
            module, \
            errmsg)

#define GLOBUS_SEG_ERROR_INVALID_FORMAT_OBJECT(fmt) \
    globus_error_construct_error(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE, \
            NULL, \
            GLOBUS_SEG_ERROR_TYPE_INVALID_FORMAT, \
            __FILE__, \
            MYNAME, \
            __LINE__, \
            "Invalid format %s", \
            fmt)

#define GLOBUS_SEG_ERROR_OUT_OF_MEMORY_OBJECT() \
    globus_error_construct_error(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE, \
            NULL, \
            GLOBUS_SEG_ERROR_TYPE_OUT_OF_MEMORY, \
            __FILE__, \
            MYNAME, \
            __LINE__, \
            "Out of memory")

#define GLOBUS_SEG_ERROR_LOADING_MODULE_OBJECT(module, dlerr_msg) \
    globus_error_construct_error(GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE, \
            NULL, \
            GLOBUS_SEG_ERROR_TYPE_LOADING_MODULE, \
            __FILE__, \
            MYNAME, \
            __LINE__, \
            "Unable to dlopen module \"%s\": %s", \
            module, \
            dlerr_msg)

#define GLOBUS_SEG_ERROR_NULL \
    globus_error_put(GLOBUS_SEG_ERROR_NULL_OBJECT())

#define GLOBUS_SEG_ERROR_ALREADY_SET \
    globus_error_put(GLOBUS_SEG_ERROR_ALREADY_SET_OBJECT())

#define GLOBUS_SEG_ERROR_INVALID_MODULE(module, errmsg) \
    globus_error_put(GLOBUS_SEG_ERROR_INVALID_MODULE_OBJECT(module, errmsg))

#define GLOBUS_SEG_ERROR_INVALID_FORMAT(fmt) \
    globus_error_put(GLOBUS_SEG_ERROR_INVALID_FORMAT_OBJECT(fmt))

#define GLOBUS_SEG_ERROR_OUT_OF_MEMORY \
    globus_error_put(GLOBUS_SEG_ERROR_OUT_OF_MEMORY_OBJECT())

#define GLOBUS_SEG_ERROR_LOADING_MODULE(module, dlerr_msg) \
    globus_error_put(GLOBUS_SEG_ERROR_LOADING_MODULE_OBJECT(module, \
            dlerr_msg))

extern globus_module_descriptor_t globus_i_scheduler_event_generator_module;
#define GLOBUS_SCHEDULER_EVENT_GENERATOR_MODULE \
    (&globus_i_scheduler_event_generator_module)

/**
 * @defgroup globus_scheduler_event_generator_api Scheduler Implementation API
 * @ingroup globus_scheduler_event_generator
 * @brief Scheduler Implementation API
 * 
 * Scheduler-specific SEG module implementations use this API to issue events
 * to the Job State Monitor. Events occur whenever a job is placed in the 
 * scheduler's queue (PENDING), begins execution (ACTIVE), terminates
 * successfully (DONE), or ends abnormally (FAILED).
 *
 * A SEG module should register an event with the Globus event driver 
 * (most likely using either the Globus Callback or Globus XIO interfaces)
 * in its activation function and then return. All events should be triggered
 * from callbacks. When the SEG detects that it should terminate, it will
 * deactivate the SEG module it started. The SEG module should wait for any
 * outstanding callbacks to subside and before returning from its deactivation
 * function to ensure that all events will be properly dispatched. After
 * deactivation is complete, the SEG will unload the shared module and
 * terminate.
 */
globus_result_t
globus_scheduler_event(
    const char * format,
    ...);

globus_result_t
globus_scheduler_event_pending(
    time_t                              timestamp,
    const char *                        jobid);

globus_result_t
globus_scheduler_event_active(
    time_t                              timestamp,
    const char *                        jobid);

globus_result_t
globus_scheduler_event_failed(
    time_t                              timestamp,
    const char *                        jobid,
    int                                 failure_code);

globus_result_t
globus_scheduler_event_done(
    time_t                              timestamp,
    const char *                        jobid,
    int                                 exit_code);

globus_result_t
globus_scheduler_event_generator_get_timestamp(
    time_t *                            timestamp);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_SCHEDULER_EVENT_GENERATOR_H */
