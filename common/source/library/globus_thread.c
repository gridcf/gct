/*
 * Copyright 1999-2010 University of Chicago
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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_thread.c
 * @brief Globus Threading Abstraction
 *
 * Stubs for the Globus threads package, to be used when Globus has been
 * configured not to use threads.
 */
#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

#include "globus_i_common_config.h"
#include "globus_thread.h"
#include "globus_common_include.h"
#include <string.h>

extern globus_result_t globus_eval_path(const char *, char **);

#include "ltdl.h"
extern globus_module_descriptor_t globus_i_thread_none_module;

const globus_thread_once_t GLOBUS_THREAD_ONCE_INIT_VALUE = GLOBUS_THREAD_ONCE_INIT;

static
int
globus_l_thread_activate(void);

static
int
globus_l_thread_deactivate(void);

extern globus_mutex_t                   globus_libc_mutex;
static globus_thread_impl_t *           globus_l_thread_impl;
static globus_thread_impl_t *           globus_l_activated_thread_impl;
static globus_module_descriptor_t *     globus_l_thread_impl_module;

static char                             globus_l_thread_model[16] = "";

globus_module_descriptor_t              globus_i_thread_module =
{
    "globus_thread",
    globus_l_thread_activate,
    globus_l_thread_deactivate
};

/**
 * @defgroup globus_thread Threading
 * @ingroup globus_common
 * @brief Portable Thread Abstraction
 *
 * The Globus runtime includes support for portably creating threads on POSIX
 * and Windows systems. It also provides a callback-driven system for
 * applications that may use threads but don't require them. The Globus
 * Thread API is modeled closely after the POSIX threads API.
 *
 * Applications can choose whether to run as threaded or non-threaded at
 * runtime by either setting the GLOBUS_THREAD_MODEL environment variable
 * or calling the globus_thread_set_model() function prior to activating
 * any Globus modules.
 *
 * The Globus thread system provides primitives for mutual exclusion
 * (globus_mutex_t, globus_rmutex_t, globus_rw_mutex_t), event
 * synchronization (globus_cond_t), one-time execution (globus_once_t), and
 * threading (globus_thread_t). 
 *
 * In non-threaded operation, globus_cond_wait() and its variants will poll
 * the callback queue and I/O system to allow event-driven programs to 
 * run in the absence of threads. The globus_thread_create() function will
 * fail in that model. Other primitive operations will return success but
 * not provide any thread exclusion as there is only one thread.
 */

/**
 * @brief Select threading model for an application
 * @ingroup globus_thread
 * @details
 * The globus_thread_set_model() function selects which runtime model
 * the current application will use. By default, the Globus runtime
 * uses a non-threaded model. Additional models may be available based
 * on system support: pthread, or windows. This function must be called
 * prior to activating any globus module, as it changes how certain
 * functions (like globus_mutex_lock() and globus_cond_wait()) behave.
 * This function overrides the value set by the
 * GLOBUS_THREAD_MODEL environment variable.
 *
 * The globus_thread_set_model() function will fail if a Globus module
 * has been activated already.
 *
 * @param model
 * The name of the thread model to use. Depending on operating system
 * capabilities, this may be "none", "pthread", "windows", or some other
 * custom thread implementation. The corresponding libtool module
 * "libglobus_thread_pthread.la" or "libglobus_thread_windows.la" must
 * be installed on the system for it to be used. 
 * 
 * @return
 * On success, globus_thread_set_model() sets the name of the thread
 * model to use and returns GLOBUS_SUCCESS. If an error occurs, then
 * globus_thread_set_model() returns GLOBUS_FAILURE.
 */
extern
int
globus_thread_set_model(
    const char *                        model)
{
    if (model == NULL)
    {
        return GLOBUS_FAILURE;
    }

    if (globus_l_thread_impl != NULL &&
        strcmp(model, globus_l_thread_model) != 0)
    {
        return GLOBUS_FAILURE;
    }

    strncpy(globus_l_thread_model, model, sizeof(globus_l_thread_model));
    globus_l_thread_model[sizeof(globus_l_thread_model)-1] = 0;

    return GLOBUS_SUCCESS;
}
/* globus_thread_set_model() */

static
int
globus_l_thread_activate(void)
{
    return globus_module_activate(globus_l_thread_impl_module);
}

static
int
globus_l_thread_deactivate(void)
{
    return globus_module_deactivate(globus_l_thread_impl_module);
}

extern
int
globus_i_thread_pre_activate(void)
{
    char *                              impl_name;
    char *                              libdir;
    const char                          format[] = "libglobus_thread_%s";
    lt_dlhandle                         impl_lib;
    globus_thread_impl_t *              impl;
    globus_result_t                     result;

    result = globus_eval_path("${libdir}", &libdir);
    if (result != GLOBUS_SUCCESS || libdir == NULL)
    {
        return GLOBUS_FAILURE;
    }

    lt_dlinit();
    lt_dladdsearchdir(libdir);

    if (globus_l_thread_model[0] == 0)
    {
        char *                          model;

        model = getenv("GLOBUS_THREAD_MODEL");

        if (model)
        {
            strncpy(globus_l_thread_model, model, sizeof(globus_l_thread_model));
            globus_l_thread_model[sizeof(globus_l_thread_model)-1] = 0;
        }
    }
    if (globus_l_thread_model[0] == 0)
    {
        strncpy(globus_l_thread_model, "none", sizeof(globus_l_thread_model));
    }
    if (strcmp(globus_l_thread_model, "none") != 0)
    {
        impl_name = malloc(sizeof(format) + strlen(globus_l_thread_model) + 1);
        sprintf(impl_name, format, globus_l_thread_model);

        impl_lib = lt_dlopenext(impl_name);
        if (impl_lib == NULL)
        {
            printf("dlopen %s: %s\n", impl_name, lt_dlerror());
            exit(1);
        }
        globus_assert(impl_lib != NULL);

        globus_l_thread_impl_module = lt_dlsym(impl_lib, "globus_extension_module");
        if (globus_l_thread_impl_module == NULL)
        {
            char extension_name_format[] = "globus_thread_%s_module";
            char * extension_name = malloc(sizeof(extension_name_format) + strlen(globus_l_thread_model) + 1);
            sprintf(extension_name, extension_name_format, globus_l_thread_model);
            globus_l_thread_impl_module = lt_dlsym(impl_lib, extension_name);
            if (globus_l_thread_impl_module == NULL)
            {
                printf("dlsym %s: %s\n", extension_name, lt_dlerror());
                free(extension_name);
                exit(1);
            }
            free(extension_name);
        }
        globus_assert(globus_l_thread_impl_module != NULL);
    }
    else
    {
        globus_l_thread_impl_module = &globus_i_thread_none_module;
    }

    impl = globus_l_thread_impl_module->get_pointer_func();
    globus_assert(impl != NULL);

    globus_l_thread_impl = impl;

    globus_l_activated_thread_impl = globus_l_thread_impl;

    globus_l_thread_impl->thread_pre_activate();

    globus_mutex_init(&globus_libc_mutex, NULL);

    return 0;
}

/**
 * @defgroup globus_mutex Mutual Exclusion
 * @ingroup globus_thread
 * @brief Mutual Exclusion
 *
 * @details
 * The Globus runtime includes three portable, related mutual exclusion
 * primitives that can be used in applications and libraries. These are
 * - globus_mutex_t: a non-recursive, non-shared lock
 * - globus_rmutex_t: a recursive non-shared lock
 * - globus_rw_mutex_t: a reader-writer lock
 */

/**
 * @brief Initialize a mutex
 * @ingroup globus_mutex
 * @details
 * The globus_mutex_init() function creates a mutex
 * variable that can be used for synchronization.  Currently, the 
 * @a attr parameter is ignored.
 *
 * @param mutex
 *     Pointer to the mutex to initialize.
 * @param attr
 *     Ignored.
 * 
 * @return
 *     On success, globus_mutex_init() initializes the mutex and
 *     returns GLOBUS_SUCCESS. Otherwise, a non-0 value is returned.
 */
extern
int
globus_mutex_init(
    globus_mutex_t *                    mutex,
    globus_mutexattr_t *                attr)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }
    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->mutex_init)
    {
        rc = globus_l_thread_impl->mutex_init(mutex, attr);
    }

    return rc;
}
/* globus_mutex_init() */

/**
 * @brief Destroy a mutex
 * @ingroup globus_mutex
 * @details
 * The globus_mutex_destroy() function destroys the mutex pointed to by its
 * @a mutex parameter. After a mutex is destroyed it may no longer be used
 * unless it is again initialized by globus_mutex_init(). Behavior is
 * undefined if globus_mutex_destroy() is called with a pointer to a locked
 * mutex.
 *
 * @param mutex
 *     The mutex to destroy
 * @return
 *     On success, globus_mutex_destroy() returns GLOBUS_SUCCESS. Otherwise,
 *     a non-zero implementation-specific error value is returned. 
 */
extern
int
globus_mutex_destroy(
    globus_mutex_t *                    mutex)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->mutex_destroy)
    {
        rc = globus_l_thread_impl->mutex_destroy(mutex);
    }

    return rc;
}
/* globus_mutex_destroy() */

/**
 * @brief Lock a mutex
 * @ingroup globus_mutex
 * @details
 * The globus_mutex_lock() function locks the mutex pointed to by its
 * @a mutex parameter.

 * Upon successful return, the thread calling globus_mutex_lock() has an
 * exclusive lock on the resources protected by @a mutex. Other threads calling
 * globus_mutex_lock() will wait until that thread later calls
 * globus_mutex_unlock() or globus_cond_wait() with that mutex. Depending on
 * the thread model, calling globus_mutex_lock on a mutex locked by the current
 * thread will either return an error or result in deadlock.
 *
 * @param mutex
 *     The mutex to lock.
 * @return
 *     On success, globus_mutex_lock() returns GLOBUS_SUCCESS. Otherwise,
 *     a non-zero implementation-specific error value is returned. 
 */
extern
int
globus_mutex_lock(
    globus_mutex_t *                    mutex)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->mutex_lock)
    {
        rc = globus_l_thread_impl->mutex_lock(mutex);
    }

    return rc;
}
/* globus_mutex_lock() */


/**
 * @brief Unlock a mutex
 * @ingroup globus_mutex
 * @details
 * The globus_mutex_unlock() function unlocks the mutex pointed to by its
 * @a mutex parameter.  Upon successful
 * return, the thread calling globus_mutex_unlock() no longer has an
 * exclusive lock on the resources protected by @a mutex. Another thread
 * calling globus_mutex_lock() may be unblocked so that it may acquire
 * the mutex. Behavior is undefined if globus_mutex_unlock is called with
 * an unlocked mutex.
 *
 * @param mutex
 *     The mutex to unlock.
 * @return
 *     On success, globus_mutex_unlock() returns GLOBUS_SUCCESS. Otherwise,
 *     a non-zero implementation-specific error value is returned. 
 */
extern
int
globus_mutex_unlock(
    globus_mutex_t *                    mutex)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->mutex_unlock)
    {
        rc = globus_l_thread_impl->mutex_unlock(mutex);
    }

    return rc;
}
/* globus_mutex_unlock() */

/**
 * @brief Lock a mutex if it is not locked
 * @ingroup globus_mutex
 * @details
 * The globus_mutex_trylock() function locks the mutex pointed to by its
 * @a mutex parameter if no thread has already locked the mutex. If 
 * @a mutex is locked, then globus_mutex_trylock() returns EBUSY and does
 * not block the current thread or lock the mutex.  Upon successful
 * return, the thread calling globus_mutex_trylock() has an exclusive 
 * lock on the resources protected by @a mutex. Other threads calling
 * globus_mutex_lock() will wait until that thread later calls
 * globus_mutex_unlock() or globus_cond_wait() with that mutex. 
 *
 * @param mutex
 *     The mutex to lock.
 * @return
 *     On success, globus_mutex_trylock() returns GLOBUS_SUCCESS and locks the
 *     mutex. If another thread holds the lock, globus_mutex_trylock()
 *     returns EBUSY. Otherwise, a non-zero implementation-specific error value
 *     is returned. 
 */
extern
int
globus_mutex_trylock(
    globus_mutex_t *                    mutex)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->mutex_trylock)
    {
        rc = globus_l_thread_impl->mutex_trylock(mutex);
    }

    return rc;
}
/* globus_mutex_trylock() */

/**
 * @defgroup globus_cond Condition Variables
 * @ingroup globus_thread
 * @brief Condition Variables
 *
 * @details
 * The globus_cond_t provides condition variables for signalling events
 * between threads interested in particular state. One or many threads
 * may wait on a condition variable until it is signalled, at which point
 * they can attempt to lock a mutex related to that condition's state
 * and process the event.
 *
 * In a non-threaded model, the condition variable wait operations are
 * used to poll the event driver to handle any operations that have been
 * scheduled for execution by the globus_callback system or I/O system.
 * In this way, applications written to  use those systems to handle
 * nonblocking operations will work with either a threaded or nonthreaded
 * runtime choice.
 */

/** @brief Initialize a condition variable
 * @ingroup globus_cond
 * The globus_cond_init() function creates a condition 
 * variable that can be used for event signalling between threads.
 *
 * @param cond
 *     Pointer to the condition variable to initialize.
 * @param attr
 *     Condition variable attributes.
 * 
 * @return
 *     On success, globus_cond_init() initializes the condition variable and
 *     returns GLOBUS_SUCCESS. Otherwise, a non-0 value is returned.
 */
extern
int
globus_cond_init(
    globus_cond_t *                     cond,
    globus_condattr_t *                 attr)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->cond_init)
    {
        rc = globus_l_thread_impl->cond_init(cond, attr);
    }

    return rc;

}
/* globus_cond_init() */

/**
 * @brief Destroy a condition variable
 * @ingroup globus_cond
 * @details
 * The globus_cond_destroy() function destroys the condition variable
 * pointed to by its @a cond parameter. After a condition variable is
 * destroyed it may no longer be used
 * unless it is again initialized by globus_cond_init(). 
 *
 * @param cond
 *     The condition variable to destroy.
 * @return
 *     On success, globus_cond_destroy() returns GLOBUS_SUCCESS. Otherwise,
 *     a non-zero implementation-specific error value is returned. 
 */
extern
int
globus_cond_destroy(
    globus_cond_t *                     cond)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->cond_destroy)
    {
        rc = globus_l_thread_impl->cond_destroy(cond);
    }

    return rc;

}
/* globus_cond_destroy() */

/**
 * @brief Wait for a condition to be signalled
 * @ingroup globus_cond
 * @details
 * The globus_cond_wait() function atomically unlocks the mutex pointed to
 * by the @a mutex parameter and blocks the current thread until the
 * condition variable pointed to by @a cond is signalled by either
 * globus_cond_signal() or globus_cond_broadcast(). Behavior is undefined
 * if globus_cond_wait() is called with the mutex pointed to by the @a
 * mutex variable unlocked.
 *
 * @param cond
 *     The condition variable to wait for.
 * @param mutex
 *     The mutex associated with the condition state.
 *
 * @return
 *     On success, globus_cond_wait() unlocks the mutex and blocks the current
 *     thread until it has been signalled, returning GLOBUS_SUCCES. Otherwise,
 *     globus_cond_wait() returns an implementation-specific non-zero error
 *     value.
 */
extern
int
globus_cond_wait(
    globus_cond_t *                     cond,
    globus_mutex_t *                    mutex)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->cond_wait)
    {
        rc = globus_l_thread_impl->cond_wait(cond, mutex);
    }

    return rc;

}
/* globus_cond_wait() */

/**
 * @brief Wait for a condition to be signalled
 * @ingroup globus_cond
 * @details
 * The globus_cond_timedwait() function atomically unlocks the mutex
 * pointed to by the @a mutex parameter and blocks the current thread until
 * either the condition variable pointed to by @a cond is signalled by
 * another thread or the current time exceeds the value pointed to by the
 * @a abstime parameter. If the timeout occurs before the condition is
 * signalled, globus_cond_timedwait() returns ETIMEDOUT. Behavior is
 * undefined if globus_cond_timedwait() is called with the mutex pointed to
 * by the @a mutex variable unlocked.
 *
 * @param cond
 *     The condition variable to wait for.
 * @param mutex
 *     The mutex associated with the condition state.
 * @param abstime
 *     The absolute time to wait until.
 *
 * @return
 *     On success, globus_cond_timedwait() unlocks the mutex and blocks the
 *     current thread until it has been signalled, returning GLOBUS_SUCCES.
 *     If a timeout occurs before signal, globus_cond_timedwait() unlocks
 *     the mutex and returns ETIMEDOUT. Otherwise, 
 *     globus_cond_timedwait() returns an implementation-specific non-zero
 *     error value.
 */
extern
int
globus_cond_timedwait(
    globus_cond_t *                     cond,
    globus_mutex_t *                    mutex,
    globus_abstime_t *                  abstime)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->cond_timedwait)
    {
        rc = globus_l_thread_impl->cond_timedwait(cond, mutex, abstime);
    }

    return rc;
}
/* globus_cond_timedwait() */

/**
 * @brief Signal a condition to a thread
 * @ingroup globus_cond
 * @details
 * The globus_cond_signal() function signals a condition as occurring.
 * This will unblock at least one thread waiting for that condition.
 *
 * @param cond
 *     A pointer to the condition variable to signal.
 * @return
 *     Upon success, globus_cond_signal() returns GLOBUS_SUCCESS. If
 *     an error occurs, globus_cond_signal() returns an implementation-specific
 *     non-zero error code.
 */
extern int
globus_cond_signal(
    globus_cond_t *                     cond)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->cond_signal)
    {
        rc = globus_l_thread_impl->cond_signal(cond);
    }

    return rc;
}
/* globus_cond_signal() */

/**
 * @brief Signal a condition to multiple threads
 * @ingroup globus_cond
 * @details
 * The globus_cond_signal() function signals a condition as occurring.
 * This will unblock all threads waiting for that condition.
 *
 * @param cond
 *     A pointer to the condition variable to signal.
 * @return
 *     Upon success, globus_cond_broadcast() returns GLOBUS_SUCCESS. If
 *     an error occurs, globus_cond_broadcast() returns an
 *     implementation-specific non-zero error code.
 */
extern
int
globus_cond_broadcast(
    globus_cond_t *                     cond)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->cond_broadcast)
    {
        rc = globus_l_thread_impl->cond_broadcast(cond);
    }

    return rc;
}
/* globus_cond_broadcast() */

/**
 * @brief Initialize a mutex attribute
 * @ingroup globus_mutex
 * @details
 * The globus_mutexattr_init() function initializes the mutex attribute
 * structure pointed to by its @a attr parameter. Currently there are
 * no attribute values that can be set via this API, so there's no real
 * use to calling this function.
 *
 * @param attr
 *     Attribute structure to initialize.
 *
 * @return
 *     Upon success, globus_mutexattr_init() returns GLOBUS_SUCCESS and
 *     modifies the attribute pointed to by @a attr. If an error occurs,
 *     globus_mutexattr_init() returns an implementation-specific non-zero
 *     error code.
 */
extern
int
globus_mutexattr_init(
    globus_mutexattr_t *                attr)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->mutexattr_init)
    {
        rc = globus_l_thread_impl->mutexattr_init(attr);
    }

    return rc;
}
/* globus_mutexattr_init() */

/**
 * @brief Destroy a mutex attribute
 * @ingroup globus_mutex
 * @details
 * The globus_mutexattr_destroy() function destroys the mutex attribute
 * structure pointed to by its @a attr parameter. 
 *
 * @param attr
 *     Attribute structure to destroy.
 *
 * @return
 *     Upon success, globus_mutexattr_destroy() returns GLOBUS_SUCCESS and
 *     modifies the attribute pointed to by @a attr. If an error occurs,
 *     globus_mutexattr_destroy() returns an implementation-specific non-zero
 *     error code.
 */
extern
int
globus_mutexattr_destroy(
    globus_mutexattr_t *                attr)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->mutexattr_destroy)
    {
        rc = globus_l_thread_impl->mutexattr_destroy(attr);
    }

    return rc;
}
/* globus_mutexattr_destroy() */

/**
 * @brief Initialize a condition variable attribute
 * @ingroup globus_cond
 * @details
 * The globus_condattr_init() function initializes the condition variable
 * attribute structure pointed to by its @a cond_attr parameter to the
 * system default values.
 *
 * @param cond_attr
 *     Attribute structure to initialize.
 *
 * @return
 *     Upon success, globus_condattr_init() returns GLOBUS_SUCCESS and
 *     modifies the attribute pointed to by @a cond_attr. If an error occurs,
 *     globus_condattr_init() returns an implementation-specific non-zero
 *     error code.
 */
extern int
globus_condattr_init(
    globus_condattr_t *                 cond_attr)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->condattr_init)
    {
        rc = globus_l_thread_impl->condattr_init(cond_attr);
    }

    return rc;
}
/* globus_condattr_init() */
    
/**
 * @brief Destroy a condition attribute
 * @ingroup globus_cond
 * @details
 * The globus_condattr_destroy() function destroys the condition variable
 * attribute structure pointed to by its @a cond_attr parameter. 
 *
 * @param cond_attr
 *     Attribute structure to destroy.
 *
 * @return
 *     Upon success, globus_condattr_destroy() returns GLOBUS_SUCCESS and
 *     modifies the attribute pointed to by @a cond_attr. If an error occurs,
 *     globus_condattr_destroy() returns an implementation-specific non-zero
 *     error code.
 */
extern int
globus_condattr_destroy(
    globus_condattr_t *                 cond_attr)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->condattr_destroy)
    {
        rc = globus_l_thread_impl->condattr_destroy(cond_attr);
    }

    return rc;
}
/* globus_condattr_destroy() */

/**
 * @brief Set callback space associated with a condition variable attribute
 * @ingroup globus_cond
 * The globus_condattr_setspace() function sets the callback space to use
 * with condition variables created with this attribute. Callback spaces
 * are used to control how callbacks are issued to different threads. See
 * @link globus_callback_spaces Callback Spaces @endlink for more information
 * on callback spaces.
 *
 * @param cond_attr
 *     Condition variable attribute to modify.
 * @param space
 *     Callback space to associate with the attribute.
 *  
 * @return
 *     On success, globus_condattr_setspace() returns GLOBUS_SUCCESS and
 *     adds a reference to the callback space to the condition variable
 *     attribute. If an error occurs, globus_condattr_setspace() returns
 *     an implementation-specific non-zero error code.
 */
extern int
globus_condattr_setspace(
    globus_condattr_t *                 cond_attr,
    int                                 space)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->condattr_setspace)
    {
        rc = globus_l_thread_impl->condattr_setspace(cond_attr, space);
    }

    return rc;
}
/* globus_condattr_setspace() */

/**
 * @brief Get callback space associated with a condition variable attribute
 * @ingroup globus_cond
 * The globus_condattr_getspace() function copies the value of the callback
 * space associated with a condition variable attribute to the integer 
 * pointed to by the @a space parameter. 
 *
 * @param cond_attr
 *     Condition variable attribute to modify.
 * @param space
 *     Pointer to an integer to be set to point to the callback space
 *     associated with cond_attr.
 *  
 * @return
 *     On success, globus_condattr_getspace() returns GLOBUS_SUCCESS and
 *     modifies the value pointed to by @a space to refer to the callback
 *     space associated with @a cond_attr.
 *     If an error occurs, globus_condattr_getspace() returns
 *     an implementation-specific non-zero error code.
 */
extern int
globus_condattr_getspace(
    globus_condattr_t *                 cond_attr,
    int *                               space)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->condattr_getspace)
    {
        rc = globus_l_thread_impl->condattr_getspace(cond_attr, space);
    }
    else
    {
        rc = 0;
    }

    return rc;
}
/* globus_condattr_getspace() */

/**
 * @brief Create a new thread
 * @ingroup globus_thread
 * @details
 * The globus_thread_create() function creates a new thread of execution
 * in the current process to run the function pointed to by the @a func
 * parameter passed the @a user_arg value as its only parameter. This
 * new thread will be detached, so that storage associated with the thread
 * will be automatically reclaimed by the operating system. A thread
 * identifier will be copied to the value pointed by the @a thread
 * parameter if it is non-NULL. The caller may use this thread identifier
 * to signal or cancel this thread. The @a attr parameter is ignored by
 * this function. If the "none" threading model is used by an application,
 * then this function will always fail. One alternative that will work both
 * with and without threads is to use the functions in the
 * @link globus_callback Globus Callback API @endlink.
 *
 * @param thread
 *     Pointer to a variable to contain the new thread's identifier.
 * @param attr
 *     Ignored
 * @param func
 *     Pointer to a function to start in the new thread.
 * @param user_arg
 *     Argument to the new thread's function.
 *
 * @return
 *     On success, globus_thread_create() will start a new thread, invoking
 *     (*func)(user_arg), modify the value pointed to by the @a thread
 *     parameter to contain the new thread's identifier and return
 *     GLOBUS_SUCCESS. If an error occurs, then the value of @a thread is
 *     undefined and globus_thread_create() returns an implementation-specific
 *     non-zero error value.
 */
extern
int
globus_thread_create(
    globus_thread_t *                   thread,
    globus_threadattr_t *               attr,
    globus_thread_func_t                func,
    void *                              user_arg)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_create)
    {
        rc = globus_l_thread_impl->thread_create(thread, attr, func, user_arg);
    }
    else
    {
        rc = EINVAL;
    }

    return rc;
}
/* globus_thread_create() */

#if USE_SYMBOL_LABELS
__asm__(".symver globus_thread_key_create_compat,"
        "globus_thread_key_create@GLOBUS_COMMON_11");
__asm__(".symver globus_thread_key_create_new,"
        "globus_thread_key_create@@GLOBUS_COMMON_14");

#define globus_thread_key_create globus_thread_key_create_new
int
globus_thread_key_create_compat(
    pthread_key_t *                     key,
    globus_thread_key_destructor_func_t destructor)
{
    return pthread_key_create(key, destructor);
}
/* globus_thread_getspecific_compat() */
#endif
/**
 * @defgroup globus_thread_key Thread-Specific Storage
 * @ingroup globus_thread
 * @brief Thread-Specific Storage
 *
 * @details
 * The globus_thread_key_t data type acts as a key to thread-specific
 * storage. For each key created by globus_thread_key_create(), each 
 * thread may store and retrieve its own value. 
 */
/**
 * @brief Create a key for thread-specific storage
 * @ingroup globus_thread_key
 * @details
 * The globus_thread_key_create() function creates a new key for
 * thread-specific data. The new key will be available for all threads
 * to store a distinct value. If the function pointer @a destructor
 * is non-NULL, then that function will be invoked when a thread exits
 * that has a non-NULL value associated with the key.
 *
 * @param key
 *     Pointer to be set to the new key.
 * @param destructor
 *     Pointer to a function to call when a thread exits to free the key's
 *     value.
 *
 * @return
 *     On success, globus_thread_create_key() will create a new key to
 *     thread-local storage and return GLOBUS_SUCCESS. If an error occurs, then
 *     the value of @a key is undefined and globus_thread_create_key() returns
 *     an implementation-specific non-zero error value.
 */
extern
int
globus_thread_key_create(
    globus_thread_key_t *               key,
    globus_thread_key_destructor_func_t destructor)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_key_create)
    {
        rc = globus_l_thread_impl->thread_key_create(key, destructor);
    }

    return rc;
}
/* globus_thread_key_create() */

/**
 * @brief Delete a thread-local storage key
 * @ingroup globus_thread_key
 * @details
 * The globus_thread_key_delete() function deletes the key used for a 
 * thread-local storage association. The destructor function for this
 * key will no longer be called after this function returns. The behavior
 * of subsequent calls to globus_thread_getspecific() or
 * globus_thread_setspecific() with this key will be undefined.
 *
 * @param key
 *     Key to destroy.
 *
 * @return
 *     On success, globus_thread_key_delete() will delete a thread-local
 *     storage key and return GLOBUS_SUCCESS.  If an error occurs, then
 *     the value of @a key is undefined and globus_thread_create_key() returns
 *     an implementation-specific non-zero error value.
 */
extern
int
globus_thread_key_delete(
    globus_thread_key_t                 key)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_key_delete)
    {
        rc = globus_l_thread_impl->thread_key_delete(key);
    }

    return rc;
}
/* globus_thread_key_delete() */

/**
 * @defgroup globus_thread_once One-time execution
 * @ingroup globus_thread
 * @brief One-time execution
 *
 * @details
 * The globus_thread_once_t provides a way for applications and libraries
 * to execute some code exactly one time, independent of the number of 
 * threads which attempt to execute it. To use this, statically initialize
 * a globus_thread_once_t control with the value GLOBUS_THREAD_ONCE_INIT,
 * and pass a pointer to a function to execute once, along with the control,
 * to globus_thread_once().
 */

#if USE_SYMBOL_LABELS
__asm__(".symver globus_thread_once_compat,"
        "globus_thread_once@GLOBUS_COMMON_11");
__asm__(".symver globus_thread_once_new,"
        "globus_thread_once@@GLOBUS_COMMON_14");

#define globus_thread_once globus_thread_once_new

int
globus_thread_once_compat(
    pthread_once_t *                    once, 
    void                                (*init_routine)(void))
{
    return pthread_once(once, init_routine);
}
/* globus_thread_once_compat() */
#endif

/**
 * @brief Execute a function one time
 * @ingroup globus_thread_once
 * @details
 * The globus_thread_once() function will execute the function pointed to
 * by its @a init_routine parameter one time for each unique
 * globus_thread_once_t object passed to it, independent of the number
 * of threads calling it. The @a once value must be a static value
 * initialized to GLOBUS_THREAD_ONCE_INIT.
 *
 * @param once
 *     A pointer to the value used to govern whether the function passed via
 *     the @a init_routine parameter has executed.
 * @param init_routine
 *     Function to execute one time. It is called with no parameters.
 * 
 * @return
 *     On success, globus_thread_once() guarantees that the function 
 *     pointed to by @a init_routine has run, and that subsequent calls to
 *     globus_thread_once() with the same value of @a once will not execute
 *     that function, and returns GLOBUS_SUCCESS. If an error occurs, 
 *     globus_thread_once() returns an implementation-specific non-zero error
 *     value.
 */
extern
int
globus_thread_once(
    globus_thread_once_t *              once,
    void                                (*init_routine)(void))
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_once)
    {
        rc = globus_l_thread_impl->thread_once(once, init_routine);
    }
    else
    {
        rc = EINVAL;
    }

    return rc;
}
/* globus_thread_once() */

#if USE_SYMBOL_LABELS
__asm__(".symver globus_thread_getspecific_compat,"
        "globus_thread_getspecific@GLOBUS_COMMON_11");
__asm__(".symver globus_thread_getspecific_new,"
        "globus_thread_getspecific@@GLOBUS_COMMON_14");

#define globus_thread_getspecific globus_thread_getspecific_new
void *
globus_thread_getspecific_compat(
    pthread_key_t                       key)
{
    return pthread_getspecific(key);
}
/* globus_thread_getspecific_compat() */
#endif

/**
 * @brief Get a thread-specific data value
 * @ingroup globus_thread_key
 * @details
 * The globus_thread_getspecific() function returns the value associated
 * with the thread-specific data key passed as its first parameter. This
 * function returns NULL if the value has not been set by the current
 * thread. The return value is undefined if the key is not valid.
 * @param key
 *     Thread-specific data key to look up.
 * @return
 *     The value passed to a previous call to globus_thread_setspecific() in
 *     the current thread for this key.
 */
extern
void *
globus_thread_getspecific(
    globus_thread_key_t                 key)
{
    void *                              val = NULL;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }
    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_getspecific)
    {
        val = globus_l_thread_impl->thread_getspecific(key);
    }

    return val;
}
/* globus_thread_getspecific() */

#if USE_SYMBOL_LABELS
__asm__(".symver globus_thread_setspecific_compat,"
        "globus_thread_setspecific@GLOBUS_COMMON_11");
__asm__(".symver globus_thread_setspecific_new,"
        "globus_thread_setspecific@@GLOBUS_COMMON_14");

#define globus_thread_setspecific globus_thread_setspecific_new
int
globus_thread_setspecific_compat(
    pthread_key_t                       key,
    void *                              value)
{
    return pthread_setspecific(key, value);
}
/* globus_thread_getspecific_compat() */
#endif
/**
 * @brief Set a thread-specific data value
 * @ingroup globus_thread_key
 * @details
 * The globus_thread_setspecific() function associates a thread-specific
 * value with a data key. If the key had a previous value set in the
 * current thread, it is replaced, but the destructor function is not
 * called for the old value.
 * @param key
 *     Thread-specific data key to store.
 * @param value
 *     A pointer to data to store as the thread-specific data for this thread.
 * @return
 *     On success, globus_thread_setspecific() stores value in the
 *     thread-specific data for the specified key and returns GLOBUS_SUCCESS.
 *     If an error occurs, globus_thread_setspecific() returns an
 *     implementation-specific non-zero error code and does not modify the
 *     key's value for this thread.
 */
extern
int
globus_thread_setspecific(
    globus_thread_key_t                 key,
    void *                              value)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_setspecific)
    {
        rc = globus_l_thread_impl->thread_setspecific(key, value);
    }

    return rc;
}
/* globus_thread_setspecific() */

/**
 * @brief Yield execution to another thread
 * @ingroup globus_thread
 * @details
 * The globus_thread_yield() function yields execution to other threads
 * which are ready for execution. The current thread may continue to
 * execute if there are no other threads in the system's ready queue.
 */
extern
void
globus_thread_yield(void)
{

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }
    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_yield)
    {
        globus_l_thread_impl->thread_yield();
    }
}
/* globus_thread_yield() */

/**
 * @brief Terminate the current thread
 * @ingroup globus_thread
 * @details
 * The globus_thread_exit() terminates the current thread with the value
 * passed to it. This function does not return.
 */
extern
void
globus_thread_exit(
    void *                              value)
{

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }
    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_exit)
    {
        globus_l_thread_impl->thread_exit(value);
    }
    exit((intptr_t)value);
}
/* globus_thread_exit() */

/**
 * @brief Modify the current thread's signal mask
 * @ingroup globus_thread
 * @details
 * The globus_thread_sigmask() function modifies the current thread's
 * signal mask and returns the old value of the signal mask in the value
 * pointed to by the @a old_mask parameter. The @a how parameter can be
 * one of SIG_BLOCK, SIG_UNBLOCK, or SIG_SETMASK to control how the
 * signal mask is modified.
 * 
 * @param how
 *     Flag indicating how to interpret @a new_mask if it is non-NULL. If 
 *     @a how is SIG_BLOCK, then all signals in @a new_mask are blocked, as
 *     well as any which were previously blocked. If
 *     @a how is SIG_UNBLOCK, then all signals in which were previously blocked
 *     in @a new_mask are unblocked. If @a how is SIG_SETMASK, then the old
 *     signal mask is replaced with the value of @a new_mask.
 * @param new_mask
 *     Set of signals to block or unblock, based on the @a how parameter.
 * @param old_mask
 *     A pointer to be set to the old signal mask associated with the current
 *     thread.
 *
 * @return
 *     On success, globus_thread_sigmask() modifies the signal mask,
 *     modifies the value pointed to by @a old_mask with the signal mask
 *     prior to this function's execution and returns GLOBUS_SUCCESS. If an
 *     error occurs, globus_thread_sigmask() returns an implementation-specific
 *     non-zero error value.
 */
extern
int
globus_thread_sigmask(
    int                                 how,
    const sigset_t *                    new_mask,
    sigset_t *                          old_mask)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_sigmask)
    {
        rc = globus_l_thread_impl->thread_sigmask(how, new_mask, old_mask);
    }

    return rc;
}
/* globus_thread_sigmask() */

/**
 * @brief Send a signal to a thread
 * @ingroup globus_thread
 * @details
 * The globus_thread_kill() function sends the signal specified by the
 * @a sig number to the thread whose ID matches the @a thread parameter.
 * Depending on the signal mask of that thread, this may result in
 * a signal being delivered or not, and depending on the process's
 * signal actions, a signal handler, termination, or no operation will
 * occur in that thread.
 *
 * @param thread
 *     The thread identifier of the thread to signal.
 * @param sig
 *     The signal to send to the thread.
 * @return
 *     On success, globus_thread_kill() queues the signal for delivery to the
 *     specified thread and returns GLOBUS_SUCCESS. If an error occurs, 
 *     globus_thread_kill() returns an implementation-specific non-zero error
 *     value.
 */
extern
int
globus_thread_kill(
    globus_thread_t                     thread,
    int                                 sig)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_kill)
    {
        rc = globus_l_thread_impl->thread_kill(thread, sig);
    }

    return rc;
}
/* globus_thread_kill() */

/**
 * @brief Determine the current thread's ID
 * @ingroup globus_thread
 * @details
 * The globus_thread_self() function returns the thread identifier of the
 * current thread. This value is unique among all threads which are running
 * at any given time.
 * @return 
 *     The globus_thread_self() function returns the current thread's ID.
 */
extern
globus_thread_t
globus_thread_self(void)
{
    globus_thread_t                     result;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    memset(&result, 0, sizeof(globus_thread_t));

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_self)
    {
        result = globus_l_thread_impl->thread_self();
    }

    return result;
}
/* globus_thread_self() */

/**
 * @brief Check whether thread identifiers match
 * @ingroup globus_thread
 * @details
 * The globus_thread_equal() function checks whether the thread identifiers
 * passed as the @a thread1 and @a thread2 parameters refer to the same
 * thread. If so, globus_thread_equal() returns GLOBUS_TRUE; otherwise
 * GLOBUS_FALSE.
 *
 * @param thread1
 *     Thread identifier to compare.
 * @param thread2
 *     Thread identifier to compare.
 * @retval GLOBUS_TRUE thread1 and thread2 refer to the same thread.
 * @retval GLOBUS_TRUE thread1 and thread2 do not refer to the same thread.
 */
extern
globus_bool_t
globus_thread_equal(
    globus_thread_t                     thread1,
    globus_thread_t                     thread2)
{
    globus_bool_t                       result = GLOBUS_TRUE;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_equal)
    {
        result = globus_l_thread_impl->thread_equal(thread1, thread2);
    }

    return result;
}
/* globus_thread_equal() */

/**
 * @brief Indicate whether the active thread model supports preemption
 * @ingroup globus_thread
 * @return 
 *     The globus_thread_preemptive_threads() function returns GLOBUS_TRUE
 *     if the current thread model supports thread preemption; otherwise
 *     it returns GLOBUS_FALSE.
 */
extern
globus_bool_t
globus_thread_preemptive_threads(void)
{
    globus_bool_t                       result = GLOBUS_TRUE;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->preemptive_threads)
    {
        result = globus_l_thread_impl->preemptive_threads();
    }

    return result;
}
/* globus_thread_preemptive_threads() */

/**
 * @brief Determine if threads are supported
 * @ingroup globus_thread
 * @details
 * The globus_i_am_only_thread() function determines whether multiple
 * threads may be running in this process. 
 * @return 
 *     The globus_i_am_only_thread() function returns GLOBUS_TRUE if the
 *     current thread model is the "none" thread model; GLOBUS_FALSE otherwise.
 */
extern
globus_bool_t
globus_i_am_only_thread(void)
{
    globus_bool_t                       result = GLOBUS_TRUE;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->i_am_only_thread)
    {
        result = globus_l_thread_impl->i_am_only_thread();
    }

    return result;
}
/* globus_i_am_only_thread() */

/**
 * @brief Execute a function with thread cleanup in case of cancellation
 * @ingroup globus_thread
 * @details
 * The globus_thread_cancellable_func() function provides an interface to
 * POSIX thread cancellation points that does not rely on preprocessor
 * macros. It is roughly equivalent to 
 * @code
 * pthread_cleanup_push(cleanup_func, cleanup_arg);
 * (*func)(arg);
 * pthread_cleanup_pop(execute_cleanup)
 * @endcode
 *
 * @param func
 *     Pointer to a function which may be cancelled.
 * @param arg
 *     Parameter to the @a func function.
 * @param cleanup_func
 *     Pointer to a function to execute if thread cancellation occurs during
 *     @a func.
 * @param cleanup_arg
 *     Parameter to the @a cleanup_func function.
 * @param execute_cleanup
 *     Flag indicating whether the function pointed to by @a cleanup_func
 *     should be executed after @a func completes even if it is not cancelled.
 *
 * @return
 *     globus_thread_cancellable_func() returns the value returned by @a func.
 */
extern
void *
globus_thread_cancellable_func(
    void *                              (*func)(void *),
    void *                              arg,
    void                                (*cleanup_func)(void *),
    void *                              cleanup_arg,
    globus_bool_t                       execute_cleanup)
{
    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }
    if (globus_l_thread_impl->thread_cancellable_func)
    {
        return globus_l_thread_impl->thread_cancellable_func(
                func, arg, cleanup_func, cleanup_arg, execute_cleanup);
    }
    else
    {
        return NULL;
    }
}
/* globus_thread_cancellable_func() */

/**
 * @brief Cancel a thread
 * @ingroup globus_thread
 * @details
 * The globus_thread_cancel() function cancels the thread with the
 * identifier @a thr if it is still executing. If it is running with a 
 * cancellation cleanup stack, the functions in that stack are executed.
 * The target thread's cancel state determines when the cancellation is
 * delivered.
 *
 * @param thr
 *     The id of the thread to cancel
 *
 * @return
 *     On success, the globus_thread_cancel() function delivers the
 *     cancellation to the target thread and returns GLOBUS_SUCCESS. If an
 *     error occurs, globus_thread_cancel() returns an implementation-specific
 *     non-zero error value.
 */
extern
int
globus_thread_cancel(globus_thread_t thr)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_cancel)
    {
        rc = globus_l_thread_impl->thread_cancel(thr);
    }

    return rc;
}
/* globus_thread_cancel() */

/**
 * @brief Thread cancellation point
 * @ingroup globus_thread
 * @details
 * The globus_thread_testcancel() function acts as a cancellation point
 * for the current thread. If a thread has called globus_thread_cancel()
 * and cancellation is enabled, this will cause the thread to be cancelled
 * and any functions on the thread's cleanup stack to be executed. This
 * function will not return if the thread is cancelled.
 */
extern
void
globus_thread_testcancel(void)
{

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }
    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_testcancel)
    {
        globus_l_thread_impl->thread_testcancel();
    }
}

/**
 * @brief Set the thread's cancellable state
 * @ingroup globus_thread
 * @details
 * The globus_thread_setcancelstate() function sets the current
 * cancellation state to either GLOBUS_THREAD_CANCEL_DISABLE or
 * GLOBUS_THREAD_CANCEL_ENABLE, do control whether globus_thread_cancel()
 * is able to cancel this thread.
 *
 * @param state
 *     The desired cancellation state. If the value is
 *     GLOBUS_THREAD_CANCEL_DISABLE, then cancellation will be disabled for
 *     this thread. If the value is GLOBUS_THREAD_CANCEL_ENABLE, then
 *     cancellation will be enabled for this thread.
 * @param oldstate
 *     A pointer to a value which will be set to the value of the thread's 
 *     cancellation state when this function call began. This may be NULL if
 *     the caller is not interested in the previous value.
 * @return
 *     On success, the globus_thread_setcancelstate() function modifies the
 *     thread cancellation state, modifies oldstate (if non-NULL) to the value
 *     of its previous state, and returns GLOBUS_SUCCESS. If an error occurs,
 *     globus_thread_setcancelstate() returns an implementation-specific
 *     non-zero error value.
 */
extern
int
globus_thread_setcancelstate(
    int                                 state,
    int *                               oldstate)
{
    int                                 rc = 0;

    if (globus_l_thread_impl == NULL)
    {
        globus_i_thread_pre_activate();
    }

    globus_assert(globus_l_thread_impl == globus_l_activated_thread_impl);

    if (globus_l_thread_impl->thread_setcancelstate)
    {
        rc = globus_l_thread_impl->thread_setcancelstate(state, oldstate);
    }

    return rc;
}
