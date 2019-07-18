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

/**
 * @file globus_thread.h
 * @brief Globus Threading Abstraction
 *
 * Globus threads package which can work with either pthreads or without
 * threads, depending on runtime configuration
 */

#if !defined(GLOBUS_THREAD_H)
#define GLOBUS_THREAD_H 1

/* Include header files */
#include "globus_module.h"
#include "globus_time.h"

#if !defined(_WIN32) || defined(__MINGW32__)
#include <unistd.h>
#endif

#if _POSIX_THREADS
#if !defined(HAVE_PTHREAD)
#define HAVE_PTHREAD 1
#endif
#if defined __GNUC__ && defined __EXCEPTIONS
#undef __EXCEPTIONS
#include <pthread.h>
#define __EXCEPTIONS 1
#else
#include <pthread.h>
#endif
#endif /* _POSIX_THREADS */

#if defined(_WIN32)
#include <windows.h>
#define HAVE_WINDOWS_THREADS 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * Default supported thread models (on some systems). Others can conceivably
 * be dynamically loaded if their implementation can use the dummy block in the
 * structures.
 */
#define GLOBUS_THREAD_MODEL_NONE "none"
#define GLOBUS_THREAD_MODEL_PTHREADS "pthread"
#define GLOBUS_THREAD_MODEL_WINDOWS "windows"

/**
 * @brief Thread ID
 * @ingroup globus_thread
 */
typedef union
{
    int none;
#if HAVE_PTHREAD
    pthread_t pthread;
#endif
#if HAVE_WINDOWS_THREADS
    uintptr_t windows;
#endif
    intptr_t dummy;
}
globus_thread_t;

/**
 * @brief Thread attributes
 * @ingroup globus_thread
 */
typedef union
{
    int none;
#if HAVE_PTHREAD
    pthread_attr_t pthread;
#endif
#if HAVE_WINDOWS_THREADS
    void *windows;
#endif
    intptr_t dummy;
}
globus_threadattr_t;

typedef void * (*globus_thread_func_t)(void *);

/**
 * @brief Mutex 
 * @ingroup globus_mutex
 */
typedef union
{
    int none;
#if HAVE_PTHREAD
    pthread_mutex_t pthread;
#endif
#if HAVE_WINDOWS_THREADS
    HANDLE windows;
#endif
    intptr_t dummy;
}
globus_mutex_t;

/**
 * @brief Condition variable
 * @ingroup globus_cond
 */
typedef union
{
    int none;
#if HAVE_PTHREAD
    struct globus_cond_pthread_s
    {
        pthread_cond_t cond;
        globus_bool_t poll_space;
        int space;
    } pthread;
#endif
#if HAVE_WINDOWS_THREADS
    struct globus_cond_windows_s
    {
        HANDLE events[2];
        int numberOfWaiters;
    }
    windows;
#endif
    intptr_t dummy;
}
globus_cond_t;

/**
 * @brief Mutex attribute
 * @ingroup globus_mutex
 */
typedef union
{
    int none;
#if HAVE_PTHREAD
    pthread_mutexattr_t pthread;
#endif
#if HAVE_WINDOWS_THREADS
    struct globus_mutexattr_windows_s
    {
        LPSECURITY_ATTRIBUTES securityAttributes;
    } windows;
#endif
    intptr_t dummy;
}
globus_mutexattr_t;

/**
 * @brief Condition variable attribute
 * @ingroup globus_cond
 */
typedef union
{
    int none;
#if HAVE_PTHREAD
    struct globus_condattr_pthread_s
    {
        pthread_condattr_t attr;
        int space;
    } pthread;
#endif
#if HAVE_WINDOWS_THREADS
    struct globus_condattr_windows_s
    {
        LPSECURITY_ATTRIBUTES securityAttributes;
    } windows;
#endif
    intptr_t dummy;
}
globus_condattr_t;

/**
 * @brief Thread-specific data destructor
 * @ingroup globus_thread
 */
typedef void (* globus_thread_key_destructor_func_t)(void * value);

/**
 * @brief Thread-specific data key
 * @ingroup globus_thread
 */
typedef union
{
    int none;
#if HAVE_PTHREAD
    pthread_key_t pthread;
#endif
#if HAVE_WINDOWS_THREADS
    struct globus_thread_key_windows_s
    {
        DWORD TLSIndex;
        globus_thread_key_destructor_func_t destructorFunction;
    } windows;
#endif
    /*
     * Backward-compatibility hack for fedora/debian bnaries, must not
     * be bigger than sizeof(pthread_key_t)
     */
    int32_t dummy;
}
globus_thread_key_t;

/**
 * @brief Thread once structure
 * @ingroup globus_thread_once
 */
typedef union
{
    int32_t none;
#if HAVE_PTHREAD
    pthread_once_t pthread;
#endif
#if HAVE_WINDOWS_THREADS
    int windows;
#endif
    int32_t dummy;
}
globus_thread_once_t;

/**
 * @def GLOBUS_THREAD_ONCE_INIT
 * @brief Thread once initializer value
 * @ingroup globus_thread_once
 * @hideinitializer
 */
extern const globus_thread_once_t GLOBUS_THREAD_ONCE_INIT_VALUE;
#if HAVE_PTHREAD
#   define GLOBUS_THREAD_ONCE_INIT { .pthread = PTHREAD_ONCE_INIT }
#elif HAVE_WINDOWS_THREADS
#   define GLOBUS_THREAD_ONCE_INIT { .windows = 0 }
#else
#   define GLOBUS_THREAD_ONCE_INIT { .none = 0 }
#endif

int
globus_thread_set_model(
    const char *                        model);

int
globus_mutex_init(
    globus_mutex_t *                    mutex,
    globus_mutexattr_t *                attr);

int
globus_mutex_destroy(
    globus_mutex_t *                    mutex);

int
globus_mutex_lock(
    globus_mutex_t *                    mutex);

int
globus_mutex_unlock(
    globus_mutex_t *                    mutex);

int
globus_mutex_trylock(
    globus_mutex_t *                    mutex);

int
globus_cond_init(
    globus_cond_t *                     cond,
    globus_condattr_t *                 attr);

int
globus_cond_destroy(
    globus_cond_t *                     cond);

int
globus_cond_wait(
    globus_cond_t *                     cond,
    globus_mutex_t *                    mutex);

int
globus_cond_timedwait(
    globus_cond_t *                     cond,
    globus_mutex_t *                    mutex,
    globus_abstime_t *                  abstime);

int
globus_cond_signal(
    globus_cond_t *                     cond);

int
globus_cond_broadcast(
    globus_cond_t *                     cond);

int
globus_condattr_init(
    globus_condattr_t *                 cond_attr);

int
globus_condattr_destroy(
    globus_condattr_t *                 cond_attr);

int
globus_condattr_setspace(
    globus_condattr_t *                 cond_attr,
    int                                 space);

int
globus_condattr_getspace(
    globus_condattr_t *                 cond_attr,
    int *                               space);

int
globus_thread_create(
    globus_thread_t *                   thread,
    globus_threadattr_t *               attr,
    globus_thread_func_t                func,
    void *                              user_arg);

void *
globus_thread_getspecific(
    globus_thread_key_t                 key);

int
globus_thread_setspecific(
    globus_thread_key_t                 key,
    void *                              value);

int
globus_thread_key_create(
    globus_thread_key_t *               key,
    globus_thread_key_destructor_func_t func);

int
globus_thread_key_delete(
    globus_thread_key_t                 key);

int
globus_thread_once(
    globus_thread_once_t *              once,
    void (*init_routine)(void));

void
globus_thread_yield(void);

int
globus_thread_sigmask(
    int                                 how,
    const sigset_t *                    newmask,
    sigset_t *                          oldmask);

int
globus_thread_kill(
    globus_thread_t                     thread,
    int                                 sig);

void
globus_thread_exit(void *value);

globus_thread_t
globus_thread_self(void);

int
globus_thread_equal(
    globus_thread_t                     thread1,
    globus_thread_t                     thread2);

globus_bool_t
globus_i_am_only_thread(void);

globus_bool_t
globus_thread_preemptive_threads(void);

void *
globus_thread_cancellable_func(
    void *                              (*func)(void *),
    void *                              arg,
    void                                (*cleanup_func)(void *),
    void *                              cleanup_arg,
    globus_bool_t                       execute_cleanup);

int
globus_thread_cancel(globus_thread_t thr);

void
globus_thread_testcancel(void);

int
globus_thread_setcancelstate(
    int                                 state,
    int *                               oldstate);

/**
 * @brief Disable thread cancellation value
 * @ingroup globus_thread
 * @see globus_thread_setcancelstate()
 */
#define GLOBUS_THREAD_CANCEL_DISABLE 0
/**
 * @brief Enable thread cancellation value
 * @ingroup globus_thread
 * @see globus_thread_setcancelstate()
 */
#define GLOBUS_THREAD_CANCEL_ENABLE 1

/* Module definition */
int
globus_i_thread_pre_activate();

extern
globus_module_descriptor_t       globus_i_thread_module;

/**
 * @brief Thread Module
 * @ingroup globus_thread
 * @hideinitializer
 */
#define GLOBUS_THREAD_MODULE (&globus_i_thread_module)

typedef struct
{
    int (*mutex_init)(globus_mutex_t *mutex, globus_mutexattr_t *attr);
    int (*mutex_destroy)(globus_mutex_t *mutex);
    int (*mutex_lock)(globus_mutex_t *mutex);
    int (*mutex_unlock)(globus_mutex_t *mutex);
    int (*mutex_trylock)(globus_mutex_t *mutex);
    int (*cond_init)(globus_cond_t *cond, globus_condattr_t *attr);
    int (*cond_destroy)(globus_cond_t *cond);
    int (*cond_wait)(globus_cond_t *cond, globus_mutex_t *mutex);
    int (*cond_timedwait)(globus_cond_t *cond, globus_mutex_t *mutex, globus_abstime_t *abstime);
    int (*cond_signal)(globus_cond_t *cond);
    int (*cond_broadcast)(globus_cond_t *cond);
    int (*mutexattr_init)(globus_mutexattr_t *attr);
    int (*mutexattr_destroy)(globus_mutexattr_t *attr);
    int (*condattr_init)(globus_condattr_t *attr);
    int (*condattr_destroy)(globus_condattr_t *attr);
    int (*condattr_setspace)(globus_condattr_t *attr, int space);
    int (*condattr_getspace)(globus_condattr_t *attr, int *space);
    int (*thread_create)(globus_thread_t *thread, globus_threadattr_t *attr, globus_thread_func_t func, void * user_arg);
    int (*thread_key_create)(globus_thread_key_t *key, globus_thread_key_destructor_func_t func);
    int (*thread_key_delete)(globus_thread_key_t key);
    int (*thread_once)(globus_thread_once_t *once, void (*init_func)(void));
    void *(*thread_getspecific)(globus_thread_key_t key);
    int (*thread_setspecific)(globus_thread_key_t key, void *value); 
    void (*thread_yield)(void);
    void (*thread_exit)(void *value);
    int (*thread_sigmask)(int how, const sigset_t *newmask, sigset_t *oldmask);
    int (*thread_kill)(globus_thread_t thread, int sig);
    int (*thread_setcancelstate)(int state, int *oldstate);
    void (*thread_testcancel)(void);
    int (*thread_cancel)(globus_thread_t thread);
    globus_thread_t (*thread_self)(void);
    int (*thread_equal)(globus_thread_t thread1, globus_thread_t thread2);
    globus_bool_t (*preemptive_threads)(void);
    globus_bool_t (*i_am_only_thread)(void);
    void * (*thread_cancellable_func)(
        void * (*func)(void *), void *func_arg, void (*cleanup_func)(void *), void * cleanup_arg, globus_bool_t execute_cleanup);
    int (*thread_pre_activate)(void);
}
globus_thread_impl_t;

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_THREAD_H  */
