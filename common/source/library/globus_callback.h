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

#ifndef GLOBUS_CALLBACK_H
#define GLOBUS_CALLBACK_H

/**
 * @file globus_callback.h
 * @brief Globus Callback API
 */

/**
 * @defgroup globus_callback Globus Callback
 * @ingroup globus_common
 * @brief Globus Callback Function Interface
 */

#include "globus_common_include.h"
#include "globus_module.h"
#include "globus_time.h"

#ifdef __cplusplus
extern "C" {
#endif

extern
globus_module_descriptor_t       globus_i_callback_module;

/**
 * @brief Module descriptor
 * @hideinitializer
 * @ingroup globus_callback
 *
 * @details
 * Module descriptor for for globus_callback module.  Must be activated before
 * any of the following api is called.
 *
 * Note:  You would not normally activate this module directly.  Activating
 * the GLOBUS_COMMON_MODULE will in turn activate this also.
 */
#define GLOBUS_CALLBACK_MODULE (&globus_i_callback_module)

/**
 * @brief Module descriptor
 * @hideinitializer
 * @ingroup globus_callback
 * @deprecated
 *
 * @details
 * Backward compatible name
 */
#define GLOBUS_POLL_MODULE GLOBUS_CALLBACK_MODULE

/**
 * @brief Error types
 * @hideinitializer
 * @ingroup globus_callback
 *
 * @details
 * Possible error types returned by the api in this module.  You can use the
 * error API to check results against these types.
 *
 * @see globus_generic_error_utility
 */
typedef enum
{
    /** The callback handle is not valid or it has already been destroyed */
    GLOBUS_CALLBACK_ERROR_INVALID_CALLBACK_HANDLE = 1024,
    /** The space handle is not valid or it has already been destroyed */
    GLOBUS_CALLBACK_ERROR_INVALID_SPACE,
    /** Could not allocate memory for an internal structure */
    GLOBUS_CALLBACK_ERROR_MEMORY_ALLOC,
    /** One of the arguments is NULL or out of range */
    GLOBUS_CALLBACK_ERROR_INVALID_ARGUMENT,
    /** Attempt to unregister callback again */
    GLOBUS_CALLBACK_ERROR_ALREADY_CANCELED,
    /** Attempt to retrieve info about a callback not in callers's stack */
    GLOBUS_CALLBACK_ERROR_NO_ACTIVE_CALLBACK
} globus_callback_error_type_t;

/**
 * @brief Periodic callback handle
 * @ingroup globus_callback
 *
 * @details
 * This handle can be copied or compared,
 * and represented as NULL with GLOBUS_NULL_HANDLE
 */
typedef int                             globus_callback_handle_t;

/**
 * @brief Callback space handle
 * @ingroup globus_callback
 *
 * @details
 * This handle can be copied or compared
 * and represented as NULL with GLOBUS_NULL_HANDLE
 */
typedef int                             globus_callback_space_t;

/**
 * @brief Callback space attribute
 * @ingroup globus_callback
 *
 * @details
 * This handle can be copied and represented as NULL with GLOBUS_NULL
 */
typedef struct globus_l_callback_space_attr_s * globus_callback_space_attr_t;

/**
 * @defgroup globus_callback_api Globus Callback API
 * @ingroup globus_callback
 * @brief Globus Callback API
 */

/**
 * @name Convenience Macros 
 */

/* @{ */

/**
 * @brief Poll the global callback space
 * @hideinitializer
 * @ingroup globus_callback_api
 *
 * @details
 * Specifies the global space for globus_callback_space_poll(). argument is
 * the timeout
 *
 * @see globus_callback_space_poll()
 */
#define globus_callback_poll(a)                                             \
    globus_callback_space_poll((a), GLOBUS_CALLBACK_GLOBAL_SPACE)

/**
 * @brief Blocking poll of the global callback space
 * @ingroup globus_callback_api
 * @hideinitializer
 *
 * @details
 * Specifies that globus_callback_space_poll() should poll on the global space
 * with an infinite timeout
 *
 * @see globus_callback_space_poll()
 */
#define globus_poll_blocking()                                              \
    globus_callback_poll(&globus_i_abstime_infinity)

/**
 * @brief Nonblocking poll of the global callback space
 * @ingroup globus_callback_api
 * @hideinitializer
 *
 * @details
 * Specifies that globus_callback_space_poll() should poll on the global space
 * with an immediate timeout
 *
 * @see globus_callback_space_poll()
 */
#define globus_poll_nonblocking()                                           \
    globus_callback_poll(&globus_i_abstime_zero)

/**
 * @brief Nonblocking poll of the global callback space
 * @ingroup globus_callback_api
 * @hideinitializer
 *
 * @details
 * Specifies that globus_callback_space_poll() should poll on the global space
 * with an immediate timeout
 *
 * @see globus_callback_space_poll()
 */
#define globus_poll()                                                       \
    globus_poll_nonblocking()

/**
 * @brief Wake up callback polling thread
 * @ingroup globus_callback_api
 * @hideinitializer
 * @details
 * Counterpart to globus_poll().
 *
 * @details
 * @see globus_callback_signal_poll()
 */
#define globus_signal_poll()                                                \
    globus_callback_signal_poll()

/**
 * @brief Register a oneshot function in the global callback space
 * @hideinitializer
 * @ingroup globus_callback_api
 *
 * @details
 * Specifies the global space for globus_callback_space_register_oneshot()
 * all other arguments are the same as specified there.
 *
 * @see globus_callback_space_register_oneshot()
 */
#define globus_callback_register_oneshot(                                   \
        callback_handle,                                                    \
        delay_time,                                                         \
        callback_func,                                                      \
        callback_user_arg)                                                  \
    globus_callback_space_register_oneshot(                                 \
        (callback_handle),                                                  \
        (delay_time),                                                       \
        (callback_func),                                                    \
        (callback_user_arg),                                                \
        GLOBUS_CALLBACK_GLOBAL_SPACE)

/**
 * @brief Register a periodic function in the global callback space
 * @ingroup globus_callback_api
 * @hideinitializer
 *
 * @details
 * Specifies the global space for globus_callback_space_register_periodic()
 * all other arguments are the same as specified there.
 *
 * @see globus_callback_space_register_periodic()
 */
#define globus_callback_register_periodic(                                  \
        callback_handle,                                                    \
        delay_time,                                                         \
        period,                                                             \
        callback_func,                                                      \
        callback_user_arg)                                                  \
    globus_callback_space_register_periodic(                                \
        (callback_handle),                                                  \
        (delay_time),                                                       \
        (period),                                                           \
        (callback_func),                                                    \
        (callback_user_arg),                                                \
        GLOBUS_CALLBACK_GLOBAL_SPACE)

/**
 * @brief Register a signal handler in the global callback space
 * @hideinitializer
 * @ingroup globus_callback_api
 *
 * @details
 * Specifies the global space for
 * globus_callback_space_register_signal_handler() all other arguments are
 * the same as specified there.
 *
 * @see globus_callback_space_register_signal_handler()
 */
#define globus_callback_register_signal_handler(                            \
        signum,                                                             \
        persist,                                                            \
        callback_func,                                                      \
        callback_user_arg)                                                  \
    globus_callback_space_register_signal_handler(                          \
        (signum),                                                           \
        (persist),                                                          \
        (callback_func),                                                    \
        (callback_user_arg),                                                \
        GLOBUS_CALLBACK_GLOBAL_SPACE)

/* @} */

/**
 * @name Callback Prototypes
 */
/* @{ */

/**
 * @brief Globus callback prototype
 * @ingroup globus_callback_api
 *
 * @details
 * This is the signature of the function registered with the 
 * globus_callback_register_* calls.
 *
 * If this is a periodic callback, it is guaranteed that the call canNOT
 * be reentered unless globus_thread_blocking_space_will_block() is called
 * (explicitly, or implicitly via globus_cond_wait()).  Also, if
 * globus_callback_unregister() is called to cancel this periodic from within 
 * this callback, it is guaranteed that the callback will NOT be requeued again
 * 
 * If the function will block at all, the user should call 
 * globus_callback_get_timeout() to see how long this function can safely block
 * or call globus_thread_blocking_space_will_block()
 *
 * @param user_arg
 *        The user argument registered with this callback
 *
 * @return
 *        - void
 * 
 * @see globus_callback_space_register_oneshot()
 * @see globus_callback_space_register_periodic()
 * @see globus_thread_blocking_space_will_block()
 * @see globus_callback_get_timeout()
 */
typedef
void
(*globus_callback_func_t)(
    void *                              user_arg);

/* @} */

/**
 * @name Oneshot Callbacks
 */
/* @{ */

/**
 * @brief Register a oneshot some delay from now
 * @ingroup globus_callback_api
 *
 * @details
 * This function registers the callback_func to start some delay_time from
 * now.  
 *
 * @param callback_handle
 *        Storage for a handle.  This may be NULL.  If it is NOT NULL, you
 *        must unregister the callback to reclaim resources.
 *
 * @param delay_time
 *        The relative time from now to fire this callback.  If NULL, will fire
 *        as soon as possible
 *
 * @param callback_func
 *        the user func to call
 *
 * @param callback_user_arg
 *        user arg that will be passed to callback
 *
 * @param space
 *        The space with which to register this callback
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_ARGUMENT
 *        - GLOBUS_CALLBACK_ERROR_MEMORY_ALLOC
 *        - GLOBUS_SUCCESS
 * 
 * @see globus_callback_func_t
 * @see globus_callback_spaces
 */
globus_result_t
globus_callback_space_register_oneshot(
    globus_callback_handle_t *          callback_handle,
    const globus_reltime_t *            delay_time,
    globus_callback_func_t              callback_func,
    void *                              callback_user_arg,
    globus_callback_space_t             space);

/* @} */

/**
 * @name Periodic Callbacks
 */
/* @{ */

/**
 * @brief Register a periodic callback
 * @ingroup globus_callback_api
 *
 * @details
 * This function registers a periodic callback_func to start some delay_time 
 * and run every period from then.
 *
 * @param callback_handle
 *        Storage for a handle.  This may be NULL.  If it is NOT NULL, you
 *        must cancel the periodic to reclaim resources.
 *
 * @param delay_time
 *        The relative time from now to fire this callback.  If NULL, will fire
 *        the first callback as soon as possible
 *
 * @param period
 *        The relative period of this callback
 *
 * @param callback_func
 *        the user func to call
 *
 * @param callback_user_arg
 *        user arg that will be passed to callback
 *
 * @param space
 *        The space with which to register this callback
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_ARGUMENT
 *        - GLOBUS_CALLBACK_ERROR_MEMORY_ALLOC
 *        - GLOBUS_SUCCESS
 * 
 * @see #globus_callback_unregister()
 * @see #globus_callback_func_t
 * @see @link globus_callback_spaces @endlink
 */
globus_result_t
globus_callback_space_register_periodic(
    globus_callback_handle_t *          callback_handle,
    const globus_reltime_t *            delay_time,
    const globus_reltime_t *            period,
    globus_callback_func_t              callback_func,
    void *                              callback_user_arg,
    globus_callback_space_t             space);

/**
 * @brief Unregister a callback
 * @ingroup globus_callback_api
 * 
 * @details
 * This function will cancel a callback and free the resources 
 * associcated with the callback handle.  If the callback was able to be 
 * canceled immediately (or if it has already run), GLOBUS_SUCCESS is returned 
 * and it is guaranteed that there are no running instances of the callback.
 *
 * If the callback is currently running (or unstoppably about to be run), then
 * the callback is prevented from being requeued, but, the 'official' cancel
 * is deferred until the last running instance of the callback returns. If you 
 * need to know when the callback is guaranteed to have been canceled, pass an 
 * unregister callback.
 *
 * If you would like to know if you unregistered a callback before it ran, 
 * pass storage for a boolean 'active'.  This will be GLOBUS_TRUE if callback
 * was running.  GLOBUS_FALSE otherwise.
 *
 * @param callback_handle
 *        the handle received from a globus_callback_space_register_*()
 *        call
 *
 * @param unregister_callback
 *        the function to call when the callback has been canceled and
 *        there are no running instances of it. This will be
 *        delivered to the same space used in the register call.
 *
 * @param unreg_arg
 *        user arg that will be passed to the unregister callback
 *
 * @param active
 *        storage for an indication of whether the callback was running when
 *        this call was made
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_CALLBACK_HANDLE
 *        - GLOBUS_CALLBACK_ERROR_ALREADY_CANCELED
 *        - GLOBUS_SUCCESS
 * 
 * @see #globus_callback_space_register_periodic()
 * @see #globus_callback_func_t
 */
globus_result_t
globus_callback_unregister(
    globus_callback_handle_t            callback_handle,
    globus_callback_func_t              unregister_callback,
    void *                              unreg_arg,
    globus_bool_t *                     active);

/**
 * @brief Adjust the delay of a oneshot callback
 * @ingroup globus_callback_api
 *
 * @details
 * This function allows a user to adjust the delay of a previously
 * registered callback.  It is safe to call this within or outside of
 * the callback that is being modified.
 *
 * Note if the oneshot has already been fired, this function will still return
 * GLOBUS_SUCCESS, but won't affect anything.
 *
 * @param callback_handle
 *        the handle received from a globus_callback_space_register_oneshot()
 *        call
 *
 * @param new_delay
 *        The new delay from now.  If NULL, then callback will be fired as
 *        soon as possible.
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_CALLBACK_HANDLE
 *        - GLOBUS_CALLBACK_ERROR_ALREADY_CANCELED
 *        - GLOBUS_SUCCESS
 * 
 * @see globus_callback_space_register_periodic()
 */
globus_result_t
globus_callback_adjust_oneshot(
    globus_callback_handle_t            callback_handle,
    const globus_reltime_t *            new_delay);

/**
 * @brief Adjust the period of a periodic callback.
 * @ingroup globus_callback_api
 *
 * @details
 * This function allows a user to adjust the period of a previously
 * registered callback.  It is safe to call this within or outside of
 * the callback that is being modified.
 *
 * This func also allows a user to effectively 'suspend' a periodic callback
 * until another time by passing a period of NULL.  The callback can later
 * be resumed by passing in a new period.
 *
 * Note that the callback will not be fired sooner than 'new_period' from now. 
 * A 'suspended' callback must still be unregistered to free its resources.
 *
 * @param callback_handle
 *        the handle received from a globus_callback_space_register_periodic()
 *        call
 *
 * @param new_period
 *        The new period.  If NULL or globus_i_reltime_infinity, then
 *        callback will be 'suspended' as soon as the last running instance of
 *        it returns.
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_CALLBACK_HANDLE
 *        - GLOBUS_CALLBACK_ERROR_ALREADY_CANCELED
 *        - GLOBUS_SUCCESS
 * 
 * @see globus_callback_space_register_periodic()
 */
globus_result_t
globus_callback_adjust_period(
    globus_callback_handle_t            callback_handle,
    const globus_reltime_t *            new_period);
/* @} */

/**
 * @name Callback Polling
 */
/* @{ */

/**
 * @brief Poll for ready callbacks
 * @ingroup globus_callback_api
 *
 * @details
 * This function is used to poll for registered callbacks.  
 *
 * For non-threaded builds, callbacks are not/can not be delivered unless this
 * is called.  Any call to this can cause callbacks registered with the 
 * 'global' space to be fired.  Whereas callbacks registered with a user's 
 * space will only be delivered when this is called with that space.
 *
 * For threaded builds, this only needs to be called to poll user spaces with
 * behavior == GLOBUS_CALLBACK_SPACE_BEHAVIOR_SINGLE.  The 'global' space
 * and other user spaces are constantly polled in a separate thread.  
 * (If it is called in a threaded build for these spaces, it will just yield
 * its thread)
 *
 * In general, you never need to call this function directly.  It is called
 * (when necessary) by globus_cond_wait().  The only case in which a user may
 * wish to call this explicitly is if the application has no aspirations of 
 * ever being built threaded.
 *
 * This function (when not yielding) will block up to timestop or until 
 * globus_callback_signal_poll() is called by one of the fired callbacks.  It
 * will always try and kick out ready callbacks, regardless of the timestop.
 *
 * @param timestop
 *        The time to block until.  If this is NULL or less than the cuurent
 *        time, an attempt to fire only ready callbacks is made (no blocking).
 *
 * @param space
 *        The callback space to poll.  Note: regardless of what space is passed
 *        here, the 'global' space is also always polled.
 *
 * @return
 *        - void
 * 
 * @see globus_callback_spaces
 * @see globus_condattr_setspace()
 */
void
globus_callback_space_poll(
    const globus_abstime_t *            timestop,
    globus_callback_space_t             space);

/**
 * @brief Signal the poll
 * @ingroup globus_callback_api
 *
 * @details
 * This function signals globus_callback_space_poll() that something has
 * changed and it should return to its caller as soon as possible.
 *
 * In general, you never need to call this function directly.  It is called
 * (when necessary) by globus_cond_signal() or globus_cond_broadcast.  The only
 * case in which a user may wish to call this explicitly is if the application 
 * has no aspirations of ever being built threaded.
 *
 * @return
 *        - void
 * 
 * @see globus_callback_space_poll()
 */
void
globus_callback_signal_poll();
/* @} */

/**
 * @name Miscellaneous
 */
/* @{ */

/**
 * @brief Get the amount of time left in a callback
 * @ingroup globus_callback_api
 *
 * @details
 * This function retrieves the remaining time a callback is allowed to run.
 * If a callback has already timed out, time_left will be set to zero and
 * GLOBUS_TRUE returned.  This function is intended to be called within a 
 * callback's stack, but is harmless to call anywhere (will return 
 * GLOBUS_FALSE and an infinite time_left)
 *
 * @param time_left
 *        storage for the remaining time.
 *
 * @return
 *        - GLOBUS_FALSE if time remaining
 *        - GLOBUS_TRUE if already timed out
 */
globus_bool_t
globus_callback_get_timeout(
    globus_reltime_t *                  time_left);

/**
 * @brief See if there is remaining time in a callback
 * @ingroup globus_callback_api
 *
 * @details
 * This function returns GLOBUS_TRUE if the running time of a callback has
 * already expired.  This function is intended to be called within a callback's
 * stack, but is harmless to call anywhere (will return GLOBUS_FALSE)
 *
 * @return
 *        - GLOBUS_FALSE if time remaining
 *        - GLOBUS_TRUE if already timed out
 */
globus_bool_t
globus_callback_has_time_expired();

/**
 * @brief See if a callback has been restarted.
 * @ingroup globus_callback_api
 *
 * @details
 * If the callback is a oneshot, this merely means the callback called 
 * globus_thread_blocking_space_will_block (or globus_cond_wait() at 
 * some point.
 *
 * For a periodic, it signifies the same and also that the periodic has been
 * requeued.  This means that the callback function may be reentered if the
 * period is short enough (on a threaded build)
 *
 * @return
 *        - GLOBUS_FALSE if not restarted
 *        - GLOBUS_TRUE if restarted
 */
globus_bool_t
globus_callback_was_restarted();

/* @} */

/**
 * @defgroup globus_callback_spaces Globus Callback Spaces
 * @ingroup globus_callback
 * @brief Globus Callback Spaces
 */

/**
 * @brief Global callback space
 * @hideinitializer
 * @ingroup globus_callback_spaces
 *
 * @details
 * The 'global' space handle.
 *
 * This is the default space handle implied if no spaces are 
 * explicitly created.
 */
#define GLOBUS_CALLBACK_GLOBAL_SPACE -2

/**
 * @brief Callback space behaviors describe how a space behaves.
 * @ingroup globus_callback_spaces
 *
 * @details
 * In a non-threaded build all spaces exhibit a
 * behavior == _BEHAVIOR_SINGLE.  Setting a specific behavior in this case
 * is ignored.
 * 
 * In a threaded build, _BEHAVIOR_SINGLE retains all the rules and
 * behaviors of a non-threaded build while _BEHAVIOR_THREADED makes the
 * space act as the global space.
 *
 * Setting a space's behavior to _BEHAVIOR_SINGLE guarantees that the 
 * poll protection will always be there and all callbacks are serialized and
 * only kicked out when polled for.  In a threaded build, it is still necessary
 * to poll for callbacks in a _BEHAVIOR_SINGLE space. (globus_cond_wait()
 * will take care of this for you also)
 *
 * Setting a space's behavior to _BEHAVIOR_SERIALIZED guarantees that the 
 * poll protection will always be there and all callbacks are serialized.  In a
 * threaded build, it is NOT necessary to poll for callbacks in a 
 * _BEHAVIOR_SERIALIZED space.  Callbacks in this space will be delivered as
 * soon as possible, but only one outstanding (and unblocked) callback will be
 * allowed at any time.
 *
 * Setting a space's behavior to _BEHAVIOR_THREADED allows the user to 
 * have the poll protection provided by spaces when built non-threaded, yet,
 * be fully threaded when built threaded (where poll protection is not needed)
 */
typedef enum
{
    /** The default behavior.  Indicates that you always want poll protection
     * and single threaded behavior (callbacks need to be explicitly polled for
     */
    GLOBUS_CALLBACK_SPACE_BEHAVIOR_SINGLE,
    /** Indicates that you want poll protection and all callbacks to be 
     * serialized (but they do not need to be polled for in a threaded build)
     */
    GLOBUS_CALLBACK_SPACE_BEHAVIOR_SERIALIZED,
    /** Indicates that you only want poll protection */
    GLOBUS_CALLBACK_SPACE_BEHAVIOR_THREADED
} globus_callback_space_behavior_t;

/**
 * @brief Initialize a user space
 * @ingroup globus_callback_spaces
 *
 * @details
 * This creates a user space.
 *
 * @param space
 *        storage for the initialized space handle.  This must be destroyed
 *        with globus_callback_space_destroy()
 *
 * @param attr
 *        a space attr descibing desired behaviors.  If GLOBUS_NULL, 
 *        the default behavior of GLOBUS_CALLBACK_SPACE_BEHAVIOR_SINGLE 
 *        is assumed.  This attr is copied into the space, so it is acceptable
 *        to destroy the attr as soon as it is no longer needed
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_ARGUMENT on NULL space
 *        - GLOBUS_CALLBACK_ERROR_MEMORY_ALLOC
 *        - GLOBUS_SUCCESS
 *
 * @see globus_condattr_setspace()
 * @see globus_io_attr_set_callback_space()
 */
globus_result_t
globus_callback_space_init(
    globus_callback_space_t *           space,
    globus_callback_space_attr_t        attr);

/**
 * @brief Take a reference to a space
 * @ingroup globus_callback_spaces
 *
 * @details
 * A library which has been 'given' a space to provide callbacks on would use
 * this to take a reference on the user's space.  This prevents mayhem should
 * a user destroy a space before the library is done with it.  This reference
 * should be destroyed with globus_callback_space_destroy() (think dup())
 *
 * @param space
 *        space to reference
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_SPACE
 *        - GLOBUS_SUCCESS
 */
globus_result_t
globus_callback_space_reference(
    globus_callback_space_t             space);

/**
 * @brief Destroy a reference to a user space
 * @ingroup globus_callback_spaces
 *
 * @details
 * This will destroy a reference to a previously initialized space.  Space will
 * not actually be destroyed until all callbacks registered with this space 
 * have been run and unregistered (if the user has a handle to that callback)
 * AND all references (from globus_callback_space_reference()) have been
 * destroyed.
 *
 * @param space
 *        space to destroy, previously initialized by 
 *        globus_callback_space_init() or referenced with 
 *        globus_callback_space_reference()
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_SPACE
 *        - GLOBUS_SUCCESS
 * 
 * @see globus_callback_space_init()
 * @see globus_callback_space_reference()
 */
globus_result_t
globus_callback_space_destroy(
    globus_callback_space_t             space);

/**
 * @brief Initialize a space attr
 * @ingroup globus_callback_spaces
 *
 * @details
 * Currently, the only attr to set is the behavior.  The default behavior
 * associated with this attr is GLOBUS_CALLBACK_SPACE_BEHAVIOR_SINGLE
 *
 * @param attr
 *        storage for the intialized attr.  Must be destroyed with
 *        globus_callback_space_attr_destroy()
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_ARGUMENT on NULL attr
 *        - GLOBUS_CALLBACK_ERROR_MEMORY_ALLOC
 *        - GLOBUS_SUCCESS
 */
globus_result_t
globus_callback_space_attr_init(
    globus_callback_space_attr_t *      attr);

/**
 * @brief Destroy a space attr
 * @ingroup globus_callback_spaces
 * @details
 * @param attr
 *        attr to destroy, previously initialized with 
 *        globus_callback_space_attr_init()
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_ARGUMENT on NULL attr
 *        - GLOBUS_SUCCESS
 * 
 * @see globus_callback_space_attr_init()
 */
globus_result_t
globus_callback_space_attr_destroy(
    globus_callback_space_attr_t        attr);

/**
 * @brief Set the behavior of a space
 * @ingroup globus_callback_spaces
 *
 * @param attr
 *        attr to associate behavior with
 *
 * @param behavior
 *        desired behavior
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_ARGUMENT
 *        - GLOBUS_SUCCESS
 * 
 * @see globus_callback_space_behavior_t
 */
globus_result_t
globus_callback_space_attr_set_behavior(
    globus_callback_space_attr_t        attr,
    globus_callback_space_behavior_t    behavior);

/**
 * @brief Get the behavior associated with an attr
 *
 * @ingroup globus_callback_spaces
 *
 * @details
 * Note: for a non-threaded build, this will always pass back a behavior ==
 * GLOBUS_CALLBACK_SPACE_BEHAVIOR_SINGLE.
 *
 * @param attr
 *        attr on which to query behavior
 *
 * @param behavior
 *        storage for the behavior
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_ARGUMENT
 *        - GLOBUS_SUCCESS
 */
globus_result_t
globus_callback_space_attr_get_behavior(
    globus_callback_space_attr_t        attr,
    globus_callback_space_behavior_t *  behavior);

/**
 * @brief Retrieve the space of a currently running callback
 * @ingroup globus_callback_spaces
 *
 * @param space
 *        storage for the handle to the space currently running
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_ARGUMENT on NULL space
 *        - GLOBUS_CALLBACK_ERROR_NO_ACTIVE_CALLBACK
 *        - GLOBUS_SUCCESS
 */
globus_result_t
globus_callback_space_get(
    globus_callback_space_t *           space);

/**
 * @brief Retrieve the current nesting level of a space
 * @ingroup globus_callback_spaces
 *
 * @param space
 *        The space to query.
 *
 * @return
 *      - the current nesting level
 *      - -1 on invalid space
 */
int
globus_callback_space_get_depth(
    globus_callback_space_t             space);

/**
 * @brief See if the specified space is a single threaded behavior space 
 * @ingroup globus_callback_spaces
 *
 * @param space
 *        the space to query
 *
 * @return
 *        - GLOBUS_TRUE if space's behavior is _BEHAVIOR_SINGLE
 *        - GLOBUS_FALSE otherwise
 */
globus_bool_t
globus_callback_space_is_single(
    globus_callback_space_t             space);


/**
 * @defgroup globus_callback_signal Globus Callback Signal Handling
 * @ingroup globus_callback
 * @brief Globus Callback Signal Handling
 */

/**
 * @hideinitializer
 * @ingroup globus_callback_signal
 * 
 * @details
 * Use this to trap interrupts (SIGINT on unix).  In the future, this will
 * also map to handle ctrl-C on win32.
 */
#ifdef SIGINT
#define GLOBUS_SIGNAL_INTERRUPT SIGINT
#else
#define GLOBUS_SIGNAL_INTERRUPT 0
#endif

/**
 * @brief Fire a callback when the specified signal is received.
 * @ingroup globus_callback_signal
 * 
 * @details
 * Note that there is a tiny delay between the time this call returns
 * and the signal is actually handled by this library.  It is likely that, if
 * the signal was received the instant the call returned, it will be lost
 * (this is normally not an issue, since you
 * would call this in your startup code anyway)
 *
 * @param signum
 *        The signal to receive. The following signals are not allowed:
 *        SIGKILL, SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGIOT, SIGPIPE,
 *        SIGEMT, SIGSYS, SIGTRAP, SIGSTOP, SIGCONT, and SIGWAITING
 *
 * @param persist
 *        If GLOBUS_TRUE, keep this callback registered for multiple
 *        signals.  If GLOBUS_FALSE, the signal handler will
 *        automatically be unregistered once the signal has been received.
 *
 * @param callback_func
 *        the user func to call when a signal is received
 *
 * @param callback_user_arg
 *        user arg that will be passed to callback
 *
 * @param space
 *        the space to deliver callbacks to.
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_SPACE
 *        - GLOBUS_CALLBACK_ERROR_INVALID_ARGUMENT
 *        - GLOBUS_SUCCESS otherwise
 */
globus_result_t
globus_callback_space_register_signal_handler(
    int                                 signum,
    globus_bool_t                       persist,
    globus_callback_func_t              callback_func,
    void *                              callback_user_arg,
    globus_callback_space_t             space);

/**
 * @brief Unregister a signal handling callback
 * @ingroup globus_callback_signal
 * 
 * @param signum
 *        The signal to unregister.
 *
 * @param unregister_callback
 *        the function to call when the callback has been canceled and
 *        there are no running instances of it (may be NULL). This will be
 *        delivered to the same space used in the register call.
 *
 * @param unreg_arg
 *        user arg that will be passed to callback
 *
 * @return
 *        - GLOBUS_CALLBACK_ERROR_INVALID_ARGUMENT
 *          if this signal was registered with persist == false, then
 *          there is a race between a signal actually being caught and
 *          therefor automatically unregistered and the attempt to manually
 *          unregister it.  If that race occurs, you will receive this error
 *          just as you would for any signal not registered.
 *        - GLOBUS_SUCCESS otherwise
 */
globus_result_t
globus_callback_unregister_signal_handler(
    int                                 signum,
    globus_callback_func_t              unregister_callback,
    void *                              unreg_arg);

/**
 * @brief Register a wakeup handler with callback library
 * @ingroup globus_callback_signal
 * 
 * @details
 * This is really only needed in non-threaded builds, but for cross builds
 * should be used everywhere that a callback may sleep for an extended period
 * of time.
 * 
 * An example use is for an io poller that sleeps indefinitely on select().  If
 * the callback library receives a signal that it needs to deliver asap, it
 * will call the wakeup handler(s), These wakeup handlers must run as though
 * they were called from a signal handler (don't use any thread utilities).
 * The io poll example will likely write a single byte to a pipe that select()
 * is monitoring.
 * 
 * This handler will not be unregistered until the callback library is
 * deactivated (via common).
 * 
 * @param wakeup
 *       function to call when callback library needs you to return asap
 *       from any blocked callbacks.
 * 
 * @param user_arg
 *       user data that will be passed along in the wakeup handler
 * 
 */
void
globus_callback_add_wakeup_handler(
    void                                (*wakeup)(void *),
    void *                              user_arg);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_CALLBACK_H */
