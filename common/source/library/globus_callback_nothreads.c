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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

#include "globus_i_common_config.h"
#include "globus_module.h"
#include "globus_callback.h"
#include "globus_i_callback.h"
#include "globus_priority_q.h"
#include "globus_handle_table.h"
#include "globus_thread_common.h"
#include "globus_libc.h"
#include "globus_list.h"
#include "globus_print.h"

#define GLOBUS_L_CALLBACK_INFO_BLOCK_SIZE 32
#define GLOBUS_L_CALLBACK_SPACE_BLOCK_SIZE 16
#define GLOBUS_L_CALLBACK_SIGNAL_BLOCK_SIZE 64

/* this is the number of ready oneshots that will be fired after time has
 * expired in globus_callback_space_poll()
 */
#define GLOBUS_L_CALLBACK_POST_STOP_ONESHOTS 10

#ifdef _WIN32
#define pause() Sleep(1000);
#endif

static
int
globus_l_callback_activate(void);

static
int
globus_l_callback_deactivate(void);

#include "version.h"

globus_module_descriptor_t              globus_i_callback_module_nonthreaded =
{
    "globus_callback_nonthreaded",
    globus_l_callback_activate,
    globus_l_callback_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

typedef enum
{
    GLOBUS_L_CALLBACK_QUEUE_NONE,
    GLOBUS_L_CALLBACK_QUEUE_TIMED,
    GLOBUS_L_CALLBACK_QUEUE_READY
} globus_l_callback_queue_state;

typedef struct globus_l_callback_info_s
{
    globus_callback_handle_t            handle;

    globus_callback_func_t              callback_func;
    void *                              callback_args;
    
    /* start_time only filled in for oneshots with delay and periodics */ 
    globus_abstime_t                    start_time;
    /* period only filled in for periodics */ 
    globus_reltime_t                    period;
    globus_bool_t                       is_periodic;
    globus_l_callback_queue_state       in_queue;

    int                                 running_count;
    
    globus_callback_func_t              unregister_callback;
    void *                              unreg_args;

    struct globus_l_callback_space_s *  my_space;
    
    /* used by ready queue macros */
    struct globus_l_callback_info_s *   next;
} globus_l_callback_info_t;

typedef struct
{
    globus_l_callback_info_t *          head;
    globus_l_callback_info_t **         tail;
} globus_l_callback_ready_queue_t;

typedef struct globus_l_callback_space_s
{
    globus_callback_space_t             handle;
    globus_priority_q_t                 timed_queue;
    globus_l_callback_ready_queue_t     ready_queue;
    int                                 depth;
} globus_l_callback_space_t;

typedef struct
{
    globus_bool_t                       restarted;
    const globus_abstime_t *            time_stop;
    globus_bool_t                       signaled;
    globus_l_callback_space_t *         active_space;
    globus_l_callback_info_t *          callback_info;
} globus_l_callback_restart_info_t;

typedef struct
{
    volatile globus_bool_t              pending;
    globus_callback_func_t              callback;
    void *                              user_arg;
    globus_callback_space_t             space;
    
#ifdef HAVE_SIGACTION
    struct sigaction                    old_action;
#endif
    globus_bool_t                       persist;
    globus_bool_t                       running;
    globus_callback_func_t              unregister_callback;
    void *                              unreg_arg;
} globus_l_callback_signal_handler_t;

typedef struct
{
    void                                (*wakeup)(void *);
    void *                              user_arg;
} globus_l_callback_wakeup_handler_t;

static globus_handle_table_t            globus_l_callback_handle_table;
static globus_handle_table_t            globus_l_callback_space_table;
static globus_memory_t                  globus_l_callback_info_memory;
static globus_memory_t                  globus_l_callback_space_memory;

static globus_l_callback_space_t        globus_l_callback_global_space;
static globus_l_callback_restart_info_t * globus_l_callback_restart_info;

static int                              globus_l_callback_signal_handlers_size;
static globus_l_callback_signal_handler_t **
    globus_l_callback_signal_handlers;
static volatile globus_bool_t           globus_l_callback_signal_pending;
static globus_list_t *                  globus_l_callback_wakeup_handlers;

/**
 * globus_l_callback_requeue
 *
 * Called by globus_l_callback_blocked_cb, globus_callback_space_poll, and
 * globus_callback_adjust_period. Used to requeue a periodic callback after it
 * has blocked or completed
 */

static
void
globus_l_callback_requeue(
    globus_l_callback_info_t *          callback_info,
    const globus_abstime_t *            time_now)
{
    globus_bool_t                       ready;
    globus_l_callback_space_t *         i_space;
    const globus_abstime_t *            tmp_time;
    globus_abstime_t                    l_time_now;
    
    ready = GLOBUS_TRUE;
    i_space = callback_info->my_space;
    
    /* first check to see if anything in the timed queue is ready */
    tmp_time = (globus_abstime_t *)
        globus_priority_q_first_priority(&i_space->timed_queue);
    if(tmp_time)
    {
        if(!time_now)
        {
            GlobusTimeAbstimeGetCurrent(l_time_now);
            time_now = &l_time_now;
        }
        
        while(tmp_time && globus_abstime_cmp(tmp_time, time_now) <= 0)
        {
            globus_l_callback_info_t *          ready_info;
            
            ready_info = (globus_l_callback_info_t *)
                globus_priority_q_dequeue(&i_space->timed_queue);
            
            ready_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_READY;
            
            GlobusICallbackReadyEnqueue(&i_space->ready_queue, ready_info);
                    
            tmp_time = (globus_abstime_t *)
                globus_priority_q_first_priority(&i_space->timed_queue);
        }
    }
    
    /* see if this is going in the priority q */
    if(globus_reltime_cmp(&callback_info->period, &globus_i_reltime_zero) > 0)
    {
        if(!time_now)
        {
            GlobusTimeAbstimeGetCurrent(l_time_now);
            time_now = &l_time_now;
        }
        
        GlobusTimeAbstimeInc(callback_info->start_time, callback_info->period);
    
        if(globus_abstime_cmp(time_now, &callback_info->start_time) < 0)
        {
            ready = GLOBUS_FALSE;
            callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_TIMED;
            
            globus_priority_q_enqueue(
                &i_space->timed_queue,
                callback_info,
                &callback_info->start_time);
        }
    }
    
    if(ready)
    {
        callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_READY;
        
        GlobusICallbackReadyEnqueue(&i_space->ready_queue, callback_info);
    }
}

/**
 * globus_l_callback_blocked_cb
 *
 * This call is registered with globus_thread_blocking_callback_push.  It is
 * called when a user calls globus_thread_blocking_will_block/globus_cond_wait
 *
 * When called, this function will requeue a periodic callback iff .
 * globus_thread_blocking_will_block/globus_cond_wait was called on that
 * callbacks 'space' or if that callback belongs to the global space
 */

static
void
globus_l_callback_blocked_cb(
    globus_thread_callback_index_t      index,
    globus_callback_space_t             space,
    void *                              user_args)
{
    globus_l_callback_restart_info_t *  restart_info;
    
    restart_info = (globus_l_callback_restart_info_t *) user_args;
    
    if(restart_info && !restart_info->restarted)
    {
        globus_l_callback_info_t *      callback_info;

        callback_info = restart_info->callback_info;

        if(callback_info->my_space->handle == GLOBUS_CALLBACK_GLOBAL_SPACE ||
            callback_info->my_space->handle == space)
        {
            if(callback_info->is_periodic)
            {
                globus_l_callback_requeue(callback_info, GLOBUS_NULL);
            }

            restart_info->restarted = GLOBUS_TRUE;
        }
    }
}

/*
 * destructor for globus_handle_table.  called whenever the reference for
 * a callback_info goes to zero.
 */
static
void
globus_l_callback_info_destructor(
    void *                              datum)
{
    globus_l_callback_info_t *          callback_info;
    
    callback_info = (globus_l_callback_info_t *) datum;
    
    /* global space is local storage, is not managed */
    if(callback_info->my_space->handle != GLOBUS_CALLBACK_GLOBAL_SPACE)
    {
        globus_handle_table_decrement_reference(
            &globus_l_callback_space_table, callback_info->my_space->handle);
    }

    globus_memory_push_node(
        &globus_l_callback_info_memory, callback_info);
}

/* 
 * destructor for globus_handle_table.  called whenever the reference for
 * a space goes to zero.
 *
 */
static
void
globus_l_callback_space_destructor(
    void *                              datum)
{
    globus_l_callback_space_t *         space;
    
    space = (globus_l_callback_space_t *) datum;
    
    globus_priority_q_destroy(&space->timed_queue);
    
    globus_memory_push_node(
        &globus_l_callback_space_memory, space);
}

static
int
globus_l_callback_activate()
{
    int                                 rc;

    rc = globus_module_activate(GLOBUS_THREAD_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
        return rc;
    }

    globus_handle_table_init(
        &globus_l_callback_handle_table,
        globus_l_callback_info_destructor);
    
    globus_handle_table_init(
        &globus_l_callback_space_table,
        globus_l_callback_space_destructor);

    /* init global 'space' */
    globus_l_callback_global_space.handle = GLOBUS_CALLBACK_GLOBAL_SPACE;
    GlobusICallbackReadyInit(&globus_l_callback_global_space.ready_queue);
    globus_priority_q_init(
        &globus_l_callback_global_space.timed_queue,
        (globus_priority_q_cmp_func_t) globus_abstime_cmp);
    globus_l_callback_global_space.depth = 0;
    
    globus_memory_init(
        &globus_l_callback_info_memory,
        sizeof(globus_l_callback_info_t),
        GLOBUS_L_CALLBACK_INFO_BLOCK_SIZE);

    globus_memory_init(
        &globus_l_callback_space_memory,
        sizeof(globus_l_callback_space_t),
        GLOBUS_L_CALLBACK_SPACE_BLOCK_SIZE);

    globus_l_callback_restart_info = GLOBUS_NULL;
    
    globus_l_callback_signal_handlers_size =    
        GLOBUS_L_CALLBACK_SIGNAL_BLOCK_SIZE;
    globus_l_callback_signal_handlers = (globus_l_callback_signal_handler_t **)
        globus_calloc(
            globus_l_callback_signal_handlers_size,
            sizeof(globus_l_callback_signal_handler_t *));
    
    globus_l_callback_signal_pending = GLOBUS_FALSE;
    globus_l_callback_wakeup_handlers = GLOBUS_NULL;

    return GLOBUS_SUCCESS;
}

static
int
globus_l_callback_deactivate()
{
    int                                 i;
    
    globus_priority_q_destroy(&globus_l_callback_global_space.timed_queue);
    
    /* any handles left here will be destroyed by destructor.
     * important that globus_l_callback_handle_table be destroyed
     * BEFORE globus_l_callback_space_table since destructor for the former
     * accesses the latter
     */
    globus_handle_table_destroy(&globus_l_callback_handle_table);
    globus_handle_table_destroy(&globus_l_callback_space_table);
    
    globus_memory_destroy(&globus_l_callback_info_memory);
    globus_memory_destroy(&globus_l_callback_space_memory);
    
        for(i = 0; i < globus_l_callback_signal_handlers_size; i++)
    {
        if(globus_l_callback_signal_handlers[i])
        {
#ifdef HAVE_SIGACTION
            sigaction(
                i,
                &globus_l_callback_signal_handlers[i]->old_action,
                GLOBUS_NULL);
#endif
            globus_free(globus_l_callback_signal_handlers[i]);
        }
    }
    globus_free(globus_l_callback_signal_handlers);
    
    while(!globus_list_empty(globus_l_callback_wakeup_handlers))
    {
        globus_free(
            globus_list_remove(
                &globus_l_callback_wakeup_handlers,
                globus_l_callback_wakeup_handlers));
    }
    
    return globus_module_deactivate(GLOBUS_THREAD_MODULE);
}

/**
 * globus_l_callback_register
 *
 * called by the external register functions.
 * -- populate a callback_info structure.
 */

static
globus_result_t
globus_l_callback_register(
    globus_callback_handle_t *          callback_handle,
    const globus_abstime_t *            start_time,
    const globus_reltime_t *            period,
    globus_callback_func_t              callback_func,
    void *                              callback_user_args,
    globus_callback_space_t             space,
    globus_bool_t                       priority)
{
    globus_l_callback_info_t *          callback_info;

    if(!callback_func)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_ARGUMENT(
            "globus_callback_space_register_oneshot", "callback_func");
    }

    callback_info = (globus_l_callback_info_t *)
        globus_memory_pop_node(&globus_l_callback_info_memory);
    if(!callback_info)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_MEMORY_ALLOC(
            "globus_l_callback_register", "callback_info");
    }

    if(space == GLOBUS_CALLBACK_GLOBAL_SPACE)
    {
        callback_info->my_space = &globus_l_callback_global_space;
    }
    else
    {
        /* get internal space structure and increment its ref count */
        globus_l_callback_space_t *     i_space;

        i_space = (globus_l_callback_space_t *)
            globus_handle_table_lookup(
                &globus_l_callback_space_table, space);
        if(!i_space)
        {
            globus_memory_push_node(
                &globus_l_callback_info_memory, callback_info);

            return GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_SPACE(
                "globus_l_callback_register");
        }

        globus_handle_table_increment_reference(
            &globus_l_callback_space_table, space);

        callback_info->my_space = i_space;
    }
    
    if(callback_handle)
    {
        /* if user passed callback_handle, there are two refs to this
         * info, me and user.  User had better unregister this handle
         * to free up the memory
         */
        callback_info->handle = globus_handle_table_insert(
            &globus_l_callback_handle_table, callback_info, 2);

        *callback_handle = callback_info->handle;
    }
    else
    {
        callback_info->handle = globus_handle_table_insert(
            &globus_l_callback_handle_table, callback_info, 1);
    }
    
    callback_info->callback_func = callback_func;
    callback_info->callback_args = callback_user_args;
    callback_info->running_count = 0;
    callback_info->unregister_callback = GLOBUS_NULL;
    
    if(period)
    {
        /* periodics need valid start times */
        if(!start_time)
        {
            GlobusTimeAbstimeGetCurrent(callback_info->start_time);
        }
        GlobusTimeReltimeCopy(callback_info->period, *period);
        callback_info->is_periodic = GLOBUS_TRUE;
    }
    else
    {
        callback_info->is_periodic = GLOBUS_FALSE;
    }
    
    if(start_time)
    {
        if(globus_time_abstime_is_infinity(start_time))
        {
            /* this will never run... must be a periodic that will be
             * restarted with globus_callback_adjust_period()
             */
            callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_NONE;
            
            /* if the user didnt pass a handle in for this, then
             * this will cause the callback_info to be freed
             * -- user doesnt know what they're doing, but no harm
             * done
             */
            globus_handle_table_decrement_reference(
               &globus_l_callback_handle_table, callback_info->handle);
        }
        else
        {
            GlobusTimeAbstimeCopy(callback_info->start_time, *start_time);
            callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_TIMED;
            
            globus_priority_q_enqueue(
                &callback_info->my_space->timed_queue,
                callback_info,
                &callback_info->start_time);
        }
    }
    else
    {
        callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_READY;
        
        if(priority)
        {
            GlobusICallbackReadyEnqueueFirst(
                &callback_info->my_space->ready_queue, callback_info);
        }
        else
        {
            GlobusICallbackReadyEnqueue(
                &callback_info->my_space->ready_queue, callback_info);
        }
    }
    
    return GLOBUS_SUCCESS;
}

/**
 * globus_callback_space_register_oneshot
 *
 * external function that registers a one shot some delay from now.
 *
 */

globus_result_t
globus_callback_space_register_oneshot_nothreads(
    globus_callback_handle_t *          callback_handle,
    const globus_reltime_t *            delay_time,
    globus_callback_func_t              callback_func,
    void *                              callback_user_args,
    globus_callback_space_t             space)
{
    globus_abstime_t                    start_time;
    
    if(delay_time)
    {
        if(globus_reltime_cmp(delay_time, &globus_i_reltime_zero) <= 0)
        {
            delay_time = GLOBUS_NULL;
        }
        else if(globus_time_reltime_is_infinity(delay_time))
        {
            /* user is being goofy here, but I'll allow it */
            GlobusTimeAbstimeCopy(start_time, globus_i_abstime_infinity);
        }
        else
        {
            GlobusTimeAbstimeGetCurrent(start_time);
            GlobusTimeAbstimeInc(start_time, *delay_time);
        }
    }

    return globus_l_callback_register(
        callback_handle,
        delay_time
            ? &start_time
            : GLOBUS_NULL,
        GLOBUS_NULL,
        callback_func,
        callback_user_args,
        space,
        GLOBUS_FALSE);
}

/**
 * globus_callback_space_register_periodic
 *
 * external function that registers a periodic to start some delay from now.
 *
 */

globus_result_t
globus_callback_space_register_periodic_nothreads(
    globus_callback_handle_t *          callback_handle,
    const globus_reltime_t *            delay_time,
    const globus_reltime_t *            period,
    globus_callback_func_t              callback_func,
    void *                              callback_user_args,
    globus_callback_space_t             space)
{
    globus_abstime_t                    start_time;

    if(!period)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_ARGUMENT(
            "globus_callback_space_register_periodic", "period");
    }
    
    if(delay_time)
    {
        if(globus_reltime_cmp(delay_time, &globus_i_reltime_zero) <= 0)
        {
            delay_time = GLOBUS_NULL;
        }
        else if(globus_time_reltime_is_infinity(delay_time))
        {
            GlobusTimeAbstimeCopy(start_time, globus_i_abstime_infinity);
        }
        else
        {
            GlobusTimeAbstimeGetCurrent(start_time);
            GlobusTimeAbstimeInc(start_time, *delay_time);
        }
    }
    
    if(globus_time_reltime_is_infinity(period))
    {
        /* infinite periods start life out as a oneshot,
         * globus_callback_adjust_period() is used to revive them
         */
        period = GLOBUS_NULL;
    }
    
    return globus_l_callback_register(
        callback_handle,
        delay_time
            ? &start_time
            : GLOBUS_NULL,
        period,
        callback_func,
        callback_user_args,
        space,
        GLOBUS_FALSE);
}

/**
 * globus_l_callback_cancel_kickout
 *
 * driver callback to kickout unregister callback. registered by
 * globus_callback_register_cancel
 *
 * This is only going to get registered if the canceled callback was not
 * running or already complete.
 */

static
void
globus_l_callback_cancel_kickout_cb(
    void *                              user_args)
{
    globus_l_callback_info_t *          callback_info;

    callback_info = (globus_l_callback_info_t *) user_args;

    callback_info->unregister_callback(callback_info->unreg_args);

    /* this will cause the callback_info to be freed */
    globus_handle_table_decrement_reference(
        &globus_l_callback_handle_table, callback_info->handle);
}

/**
 * globus_callback_unregister
 *
 * external function that cancels a previously registered callback.  will not
 * interrupt an already running callback.  also handles case where callback has
 * already completed.  it is safe to call this within the callback
 * that is being cancelled.
 *
 * the combination of this func and adjust period may cause some confusion in
 * understanding the operation.  remember that adjust period can make a
 * callback appear to be a oneshot (if adjust period is passed a null period,
 * is_periodic will become false)... 
 */

globus_result_t
globus_callback_unregister_nothreads(
    globus_callback_handle_t            callback_handle,
    globus_callback_func_t              unregister_callback,
    void *                              unreg_args,
    globus_bool_t *                     active)
{
    globus_l_callback_info_t *          callback_info;

    callback_info = (globus_l_callback_info_t *)
        globus_handle_table_lookup(
                &globus_l_callback_handle_table, callback_handle);

    if(!callback_info)
    {
        /* this is definitely an error,
         * if user had the handle and didnt destroy it (or cancel it),
         * it has to exist
         */
        return GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_CALLBACK_HANDLE(
            "globus_callback_unregister");
    }

    /* this doesnt catch a previously unregistered callback that passed a
     * NULL unregister -- bad things may happen in that case
     */
    if(callback_info->unregister_callback)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_ALREADY_CANCELED(
            "globus_callback_unregister");
    }

    callback_info->unregister_callback = unregister_callback;
    callback_info->unreg_args = unreg_args;

    if(callback_info->running_count > 0)
    {
        if(callback_info->is_periodic)
        {
            /* would only be in queue if it was restarted */
            if(callback_info->in_queue == GLOBUS_L_CALLBACK_QUEUE_TIMED)
            {
                globus_priority_q_remove(
                    &callback_info->my_space->timed_queue, callback_info);
            }
            else if(callback_info->in_queue == GLOBUS_L_CALLBACK_QUEUE_READY)
            {
                GlobusICallbackReadyRemove(
                    &callback_info->my_space->ready_queue, callback_info);
            }
            
            callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_NONE;
            callback_info->is_periodic = GLOBUS_FALSE;
        }

        /* unregister callback will get registered when running_count == 0 */

        /* this decrements the user's reference */
        globus_handle_table_decrement_reference(
            &globus_l_callback_handle_table, callback_handle);
        
        if(active)
        {
            *active = GLOBUS_TRUE;
        }
        
        return GLOBUS_SUCCESS;
    }
    else
    {
        /*
         * if the callback_info is not in the queue, it can only mean
         * that it has been suspended (by adjust_period) or it was a oneshot.
         * In this case, I would have already decremented the ref once.  I'll
         * let the globus_l_callback_cancel_kickout_cb decr the last ref
         */
        if(callback_info->in_queue)
        {
            if(callback_info->in_queue == GLOBUS_L_CALLBACK_QUEUE_TIMED)
            {
                globus_priority_q_remove(
                    &callback_info->my_space->timed_queue, callback_info);
            }
            else if(callback_info->in_queue == GLOBUS_L_CALLBACK_QUEUE_READY)
            {
                GlobusICallbackReadyRemove(
                    &callback_info->my_space->ready_queue, callback_info);
            }
            
            globus_handle_table_decrement_reference(
                &globus_l_callback_handle_table, callback_handle);
                    
            callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_NONE;
        }
        
        if(unregister_callback)
        {
            globus_callback_space_register_oneshot(
                GLOBUS_NULL,
                GLOBUS_NULL,
                globus_l_callback_cancel_kickout_cb,
                callback_info,
                callback_info->my_space->handle);
        }
        else
        {
            /* not kicking one out, so decr last ref */
            globus_handle_table_decrement_reference(
                &globus_l_callback_handle_table, callback_handle);
        }
        
        if(active)
        {
            *active = GLOBUS_FALSE;
        }
        
        return GLOBUS_SUCCESS;
    }
}

globus_result_t
globus_callback_adjust_oneshot_nothreads(
    globus_callback_handle_t            callback_handle,
    const globus_reltime_t *            new_delay)
{
    globus_l_callback_info_t *          callback_info;

    callback_info = (globus_l_callback_info_t *)
        globus_handle_table_lookup(
            &globus_l_callback_handle_table, callback_handle);
    if(!callback_info || callback_info->is_periodic)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_CALLBACK_HANDLE(
            "globus_callback_adjust_period");
    }
    
    /* this doesnt catch a previously unregistered callback that passed a
     * NULL unregister -- bad things may happen in that case
     */
    if(callback_info->unregister_callback)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_ALREADY_CANCELED(
            "globus_callback_unregister");
    }
    
    if(!new_delay)
    {
        new_delay = &globus_i_reltime_zero;
    }
    
    if(callback_info->in_queue)
    {
        if(globus_reltime_cmp(new_delay, &globus_i_reltime_zero) > 0)
        {
            GlobusTimeAbstimeGetCurrent(callback_info->start_time);
            GlobusTimeAbstimeInc(callback_info->start_time, *new_delay);
            
            if(callback_info->in_queue == GLOBUS_L_CALLBACK_QUEUE_TIMED)
            {
                globus_priority_q_modify(
                    &callback_info->my_space->timed_queue,
                    callback_info,
                    &callback_info->start_time);
            }
            else
            {
                GlobusICallbackReadyRemove(
                    &callback_info->my_space->ready_queue, callback_info);
                
                callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_TIMED;
                
                globus_priority_q_enqueue(
                    &callback_info->my_space->timed_queue,
                    callback_info,
                    &callback_info->start_time);
            }
        }
        else if(callback_info->in_queue == GLOBUS_L_CALLBACK_QUEUE_TIMED)
        {
            globus_priority_q_remove(
                &callback_info->my_space->timed_queue, callback_info);
            
            callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_READY;
            
            GlobusICallbackReadyEnqueue(
                &callback_info->my_space->ready_queue, callback_info);
        }
    }
    
    return GLOBUS_SUCCESS;
}

/**
 * globus_callback_adjust_period
 *
 * external function to allow a user to adjust the period of a previously
 * registered callback.  it is safe to call this within or outside of
 * the callback that is being modified.
 *
 * this func also allows a user to 'suspend' a periodic callback till another
 * time by passing a period of globus_i_reltime_infinity.  the callback can
 * be resumed by passing in a new period at some other time.
 *
 * this function could cause confusion in understanding this code.  When a
 * periodic is suspended, it 'becomes' non-periodic (ie, is_periodic is set to
 * false)
 */

globus_result_t
globus_callback_adjust_period_nothreads(
    globus_callback_handle_t            callback_handle,
    const globus_reltime_t *            new_period)
{
    globus_l_callback_info_t *          callback_info;

    callback_info = (globus_l_callback_info_t *)
        globus_handle_table_lookup(
            &globus_l_callback_handle_table, callback_handle);
    if(!callback_info)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_CALLBACK_HANDLE(
            "globus_callback_adjust_period");
    }
    
    /* this doesnt catch a previously unregistered callback that passed a
     * NULL unregister -- bad things may happen in that case
     */
    if(callback_info->unregister_callback)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_ALREADY_CANCELED(
            "globus_callback_unregister");
    }
    
    if(!new_period || globus_time_reltime_is_infinity(new_period))
    {
        /* doing this will cause this not to be requeued if currently running
         */
        callback_info->is_periodic = GLOBUS_FALSE;

        /* may or may not be in queue depending on if its not running or its
         * been restarted.  if its not in queue, no problem... it won't get
         * queued again
         */
        if(callback_info->in_queue)
        {
            if(callback_info->in_queue == GLOBUS_L_CALLBACK_QUEUE_TIMED)
            {
                globus_priority_q_remove(
                    &callback_info->my_space->timed_queue, callback_info);
            }
            else if(callback_info->in_queue == GLOBUS_L_CALLBACK_QUEUE_READY)
            {
                GlobusICallbackReadyRemove(
                    &callback_info->my_space->ready_queue, callback_info);
            }
            
            if(callback_info->running_count == 0)
            {
                globus_handle_table_decrement_reference(
                    &globus_l_callback_handle_table, callback_handle);
            }
                    
            callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_NONE;
        }
    }
    else
    {
        callback_info->is_periodic = GLOBUS_TRUE;
        GlobusTimeReltimeCopy(callback_info->period, *new_period);
        
        if(globus_reltime_cmp(new_period, &globus_i_reltime_zero) > 0)
        {
            GlobusTimeAbstimeGetCurrent(callback_info->start_time);
            GlobusTimeAbstimeInc(callback_info->start_time, *new_period);
            
           /* may or may not be in queue depending on if its not running or its
            * been restarted.  if its not in queue and its running, no problem...
            * when it gets requeued it will be with the new priority
            */
            if(callback_info->in_queue == GLOBUS_L_CALLBACK_QUEUE_TIMED)
            {
                globus_priority_q_modify(
                    &callback_info->my_space->timed_queue,
                    callback_info,
                    &callback_info->start_time);
            }
            else if(callback_info->in_queue == GLOBUS_L_CALLBACK_QUEUE_READY)
            {
                GlobusICallbackReadyRemove(
                    &callback_info->my_space->ready_queue, callback_info);
                
                callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_TIMED;
                
                globus_priority_q_enqueue(
                    &callback_info->my_space->timed_queue,
                    callback_info,
                    &callback_info->start_time);
            }
            else if(callback_info->running_count == 0)
            {
                /* it wasnt in the queue and its not running...  we must have
                 * previously set this non-periodic... I need to requeue it
                 * and take my ref to it back
                 */
                callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_TIMED;
                
                globus_priority_q_enqueue(
                    &callback_info->my_space->timed_queue,
                    callback_info,
                    &callback_info->start_time);
            
                globus_handle_table_increment_reference(
                    &globus_l_callback_handle_table, callback_handle);
            }
        }
        else if(callback_info->in_queue != GLOBUS_L_CALLBACK_QUEUE_READY)
        {
            /* may or may not be in queue depending on if its not running or its
             * been restarted.  if its not in queue and its running, no problem...
             * when it gets requeued it will be with the new priority
             */
            if(callback_info->in_queue == GLOBUS_L_CALLBACK_QUEUE_TIMED)
            {
                globus_priority_q_remove(
                    &callback_info->my_space->timed_queue, callback_info);
                
                callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_READY;
                
                GlobusICallbackReadyEnqueue(
                    &callback_info->my_space->ready_queue, callback_info);
            }
            else if(callback_info->running_count == 0)
            {
                /* it wasnt in the queue and its not running...  we must have
                 * previously set this non-periodic... I need to requeue it
                 * and take my ref to it back
                 */
                callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_READY;
                
                GlobusICallbackReadyEnqueue(
                    &callback_info->my_space->ready_queue, callback_info);
                    
                globus_handle_table_increment_reference(
                    &globus_l_callback_handle_table, callback_handle);
            }
        }
    }

    return GLOBUS_SUCCESS;
}

/**
 * globus_callback_space_init
 *
 * -- attrs are ignored here since there are none that make sense in
 *    a non-threaded build
 *
 */

globus_result_t
globus_callback_space_init_nothreads(
    globus_callback_space_t *           space,
    globus_callback_space_attr_t        attr)
{
    globus_l_callback_space_t *         i_space;

    if(!space)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_ARGUMENT(
            "globus_callback_space_init", "space");
    }

    i_space = (globus_l_callback_space_t *)
        globus_memory_pop_node(&globus_l_callback_space_memory);
    if(!i_space)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_MEMORY_ALLOC(
            "globus_callback_space_init", "i_space");
    }

    GlobusICallbackReadyInit(&i_space->ready_queue);
    globus_priority_q_init(
        &i_space->timed_queue,
        (globus_priority_q_cmp_func_t) globus_abstime_cmp);

    i_space->handle =
        globus_handle_table_insert(
            &globus_l_callback_space_table, i_space, 1);
    i_space->depth = 0;
    
    *space = i_space->handle;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_callback_space_reference_nothreads(
    globus_callback_space_t             space)
{
    globus_bool_t                       in_table;
    
    if(space == GLOBUS_CALLBACK_GLOBAL_SPACE)
    {
        return GLOBUS_SUCCESS;
    }
    
    in_table = globus_handle_table_increment_reference(
        &globus_l_callback_space_table, space);
        
    if(!in_table)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_SPACE(
            "globus_callback_space_reference");
    }

    return GLOBUS_SUCCESS;
}

/**
 * globus_callback_space_destroy
 *
 * while it does not make sense to do so, this can be called while there are
 * still pending callbacks within this space.  The space will not really be
 * destroyed untill all callbacks referencing it are destroyed.
 */

globus_result_t
globus_callback_space_destroy_nothreads(
    globus_callback_space_t             space)
{
    globus_l_callback_space_t *         i_space;
    
    if(space == GLOBUS_CALLBACK_GLOBAL_SPACE)
    {
        return GLOBUS_SUCCESS;
    }
    
    i_space = (globus_l_callback_space_t *)
        globus_handle_table_lookup(
                &globus_l_callback_space_table, space);
    if(!i_space)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_SPACE(
            "globus_callback_space_destroy");
    }
    
    globus_handle_table_decrement_reference(
        &globus_l_callback_space_table, space);

    return GLOBUS_SUCCESS;
}

/**
 * globus_callback_space_attr_*
 *
 * -- All of these are no-ops since there arent any meaning full attrs
 *      in a non-threaded build
 */

globus_result_t
globus_callback_space_attr_init_nothreads(
    globus_callback_space_attr_t *      attr)
{
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_callback_space_attr_destroy_nothreads(
    globus_callback_space_attr_t        attr)
{
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_callback_space_attr_set_behavior_nothreads(
    globus_callback_space_attr_t        attr,
    globus_callback_space_behavior_t    behavior)
{
    return GLOBUS_SUCCESS;
}

globus_result_t
globus_callback_space_attr_get_behavior_nothreads(
    globus_callback_space_attr_t        attr,
    globus_callback_space_behavior_t *  behavior)
{
    if(!behavior)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_ARGUMENT(
            "globus_callback_space_attr_get_behavior", "behavior");
    }

    *behavior = GLOBUS_CALLBACK_SPACE_BEHAVIOR_SINGLE;

    return GLOBUS_SUCCESS;
}

/**
 * globus_l_callback_get_next
 *
 * check queue for ready entry, pass back next ready time if no callback ready
 * else return callback info.
 *
 */

static
globus_l_callback_info_t *
globus_l_callback_get_next(
    globus_l_callback_space_t *         i_space,
    const globus_abstime_t *            time_now,
    globus_abstime_t *                  ready_time)
{
    const globus_abstime_t *            tmp_time;
    globus_l_callback_info_t *          callback_info;

    /* first check to see if anything in the timed queue is ready */
    tmp_time = (globus_abstime_t *)
        globus_priority_q_first_priority(&i_space->timed_queue);
    if(tmp_time)
    {
        while(tmp_time && globus_abstime_cmp(tmp_time, time_now) <= 0)
        {
            callback_info = (globus_l_callback_info_t *)
                globus_priority_q_dequeue(&i_space->timed_queue);
            
            callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_READY;
            
            GlobusICallbackReadyEnqueue(&i_space->ready_queue, callback_info);
                    
            tmp_time = (globus_abstime_t *)
                globus_priority_q_first_priority(&i_space->timed_queue);
        }
    }

    GlobusICallbackReadyDequeue(&i_space->ready_queue, callback_info);

    if(callback_info)
    {
        callback_info->in_queue = GLOBUS_L_CALLBACK_QUEUE_NONE;
    }
    else if(tmp_time)
    {
        GlobusTimeAbstimeCopy(*ready_time, *tmp_time);
    }
    else
    {
        GlobusTimeAbstimeCopy(*ready_time, globus_i_abstime_infinity);
    }
            
    return callback_info;
}

static
void
globus_l_callback_signal_kickout(
    void *                              user_arg)
{
    globus_l_callback_signal_handler_t *handler;
    
    handler = (globus_l_callback_signal_handler_t *) user_arg;
    handler->callback(handler->user_arg);
    
    if(--handler->running == 0 && !handler->persist)
    {
        if(handler->unregister_callback)
        {
            handler->unregister_callback(handler->unreg_arg);
        }
        globus_callback_space_destroy(handler->space);
        globus_free(handler);
    }
}

static
void
globus_l_callback_handle_signals(void)
{
    while(globus_l_callback_signal_pending)
    {
        int                             i;
        
        globus_l_callback_signal_pending = GLOBUS_FALSE;
        
        for(i = 0; i < globus_l_callback_signal_handlers_size; i++)
        {
            globus_l_callback_signal_handler_t *handler;
            
            handler = globus_l_callback_signal_handlers[i];
            if(handler && handler->pending)
            {
                globus_result_t         result;
                
                handler->pending = GLOBUS_FALSE;
                handler->running++;
                
                if(!handler->persist)
                {
                    globus_l_callback_signal_handlers[i] = GLOBUS_NULL;
#ifdef HAVE_SIGACTION
                    sigaction(i, &handler->old_action, GLOBUS_NULL);
#endif
                }
                
                result = globus_l_callback_register(
                    GLOBUS_NULL,
                    GLOBUS_NULL,
                    GLOBUS_NULL,
                    globus_l_callback_signal_kickout,
                    handler,
                    handler->space,
                    GLOBUS_TRUE);
                if(result != GLOBUS_SUCCESS)
                {
                    globus_panic(
                        GLOBUS_CALLBACK_MODULE,
                        result,
                        "[globus_l_callback_handle_signals] "
                            "Couldn't register callback");
                }
            }
        }
    }
}

/**
 * globus_callback_space_poll
 *
 * external function to poll for callbacks.  will poll at least the passed
 * space.  may also poll global 'space'
 *
 */
void
globus_callback_space_poll_nothreads(
    const globus_abstime_t *            timestop,
    globus_callback_space_t             space)
{
    globus_bool_t                       done;
    globus_l_callback_space_t *         i_space;
    globus_abstime_t                    time_now;
    globus_l_callback_restart_info_t *  last_restart_info;
    globus_l_callback_restart_info_t    restart_info;
    globus_thread_callback_index_t      idx;
    int                                 post_stop_counter;

    i_space = GLOBUS_NULL;

    if(space != GLOBUS_CALLBACK_GLOBAL_SPACE)
    {
        i_space = (globus_l_callback_space_t *)
            globus_handle_table_lookup(
                &globus_l_callback_space_table, space);
    }
    
    if(!timestop)
    {
        timestop = &globus_i_abstime_zero;
    }
    
    /*
     * If we get signaled, we will jump out of this function asap
     */
    restart_info.signaled = GLOBUS_FALSE;
    /* i wonder if this should be limited by last_restart_info->time_stop */
    restart_info.time_stop = timestop;
    
    last_restart_info = globus_l_callback_restart_info;
    globus_l_callback_restart_info = &restart_info;
    
    globus_thread_blocking_callback_push(
        globus_l_callback_blocked_cb,
        &restart_info,
        &idx);
    
    GlobusTimeAbstimeGetCurrent(time_now);
    
    done = GLOBUS_FALSE;
    post_stop_counter = GLOBUS_L_CALLBACK_POST_STOP_ONESHOTS;
    
    globus_l_callback_global_space.depth++;
    if(i_space)
    {
        restart_info.active_space = i_space;
        i_space->depth++;
    }
    else
    {
        restart_info.active_space = &globus_l_callback_global_space;
    }
    
    do
    {
        globus_l_callback_info_t *      callback_info;
        globus_abstime_t                space_ready_time;
        globus_abstime_t                global_ready_time;
        
        if(globus_l_callback_signal_pending)
        {
            globus_l_callback_handle_signals();
        }
        
        callback_info = GLOBUS_NULL;

        /* first we'll see if there is a callback ready on the polled space */
        if(i_space)
        {
            callback_info = globus_l_callback_get_next(
                i_space, &time_now, &space_ready_time);
        }

        /* if we didnt get one from the polled space, check the global queue */
        if(!callback_info)
        {
            callback_info = globus_l_callback_get_next(
                &globus_l_callback_global_space,
                &time_now,
                &global_ready_time);
        }

        if(callback_info)
        {
            /* we got a callback, kick it out */
            restart_info.restarted = GLOBUS_FALSE;
            restart_info.callback_info = callback_info;

            callback_info->running_count++;

            callback_info->callback_func(callback_info->callback_args);

            callback_info->running_count--;
            
            GlobusTimeAbstimeGetCurrent(time_now);
            
            /* a periodic that was canceled has is_periodic == false */
            if(!callback_info->is_periodic &&
                callback_info->running_count == 0)
            {
                /* if this was unregistered, we need to register for the
                 * unregister callback.  the kickout will decr the last ref
                 */
                if(callback_info->unregister_callback)
                {
                    globus_callback_space_register_oneshot(
                        GLOBUS_NULL,
                        GLOBUS_NULL,
                        globus_l_callback_cancel_kickout_cb,
                        callback_info,
                        callback_info->my_space->handle);
                }
                else
                {
                    /* no unreg callback so I'll decrement my ref */
                    globus_handle_table_decrement_reference(
                       &globus_l_callback_handle_table, callback_info->handle);
                }
            }
            else if(callback_info->is_periodic && !restart_info.restarted)
            {
                globus_l_callback_requeue(callback_info, &time_now);
            }
            
            done = restart_info.signaled;
            if(!done && globus_abstime_cmp(timestop, &time_now) <= 0)
            {
                globus_l_callback_info_t *  peek;
               
                /* time has expired, but we'll call up to 
                 * GLOBUS_L_CALLBACK_POST_STOP_ONESHOTS oneshots
                 * that are ready to go
                 */
                peek = GLOBUS_NULL;
                if(i_space)
                {
                    GlobusICallbackReadyPeak(&i_space->ready_queue, peek);
                }
                if(!peek)
                {
                    GlobusICallbackReadyPeak(
                        &globus_l_callback_global_space.ready_queue, peek);
                }
                
                if(!peek || peek->is_periodic || post_stop_counter-- == 0)
                {
                    done = GLOBUS_TRUE;
                }
            }
        }
        else
        {
            globus_abstime_t *          first_ready_time;
            
            /* pick whoever's next is ready first */
            first_ready_time = &global_ready_time;
            if(i_space)
            {
                if(globus_abstime_cmp(
                    &global_ready_time, &space_ready_time) > 0)
                {
                    first_ready_time = &space_ready_time;
                }
            }
        
            /* no callbacks were ready */
            if(globus_abstime_cmp(timestop, first_ready_time) > 0)
            {
                /* sleep until first one is ready */
                globus_reltime_t        sleep_time;
                unsigned long           usec;

                GlobusTimeAbstimeDiff(sleep_time, *first_ready_time, time_now);
                GlobusTimeReltimeToUSec(usec, sleep_time);

                if(usec > 0 && !globus_l_callback_signal_pending)
                {
                    /* still a race here.. might miss a signal, too bad.
                     * need pselect which doesnt appear portable enough yet
                     */
                    globus_libc_usleep(usec);
                }
            }
            else if(globus_time_abstime_is_infinity(timestop)
                && !globus_l_callback_signal_pending)
            {
                /* we can only get here if both queues are empty
                 * and we are blocking forever. in this case, it is not
                 * possible for a new callback to be registered, except by
                 * a signal handler. pause will wake up in that case
                 */
                /* still a race here.. might miss a signal, too bad.
                 * need pselect which doesnt appear portable enough yet
                 */
                pause();
            }
            else
            {
                /* won't be any ready before our time is up */
                done = GLOBUS_TRUE;
            }
            
            if(!done)
            {
                GlobusTimeAbstimeGetCurrent(time_now);
                if(globus_abstime_cmp(timestop, &time_now) <= 0)
                {
                    done = GLOBUS_TRUE;
                }
            }
        }
    } while(!done);
    
    globus_l_callback_global_space.depth--;
    if(i_space)
    {
        i_space->depth--;
    }
    /*
     * If I was signaled, I need to pass that signal on to my parent poller
     * because I cant be sure that the signal was just for me
     */
    if(last_restart_info && restart_info.signaled)
    {
        last_restart_info->signaled = GLOBUS_TRUE;
    }
    
    globus_l_callback_restart_info = last_restart_info;
    
    globus_thread_blocking_callback_pop(&idx);
}

void
globus_callback_signal_poll_nothreads(void)
{
    if(globus_l_callback_restart_info)
    {
        globus_l_callback_restart_info->signaled = GLOBUS_TRUE;
    }
}

/**
 * globus_callback_space_get
 *
 * allow a user to get the current space from within a callback
 */
globus_result_t
globus_callback_space_get_nothreads(
    globus_callback_space_t *           space)
{
    if(!space)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_ARGUMENT(
            "globus_callback_space_get", "space");
    }
    
    if(!globus_l_callback_restart_info)
    {
        return GLOBUS_L_CALLBACK_CONSTRUCT_NO_ACTIVE_CALLBACK(
            "globus_callback_space_get");
    }
    
    *space = globus_l_callback_restart_info->callback_info->my_space->handle;
    
    return GLOBUS_SUCCESS;
}

/**
 * globus_callback_space_get_depth
 *
 * allow a user to get the current nesting level of a space
 */
int
globus_callback_space_get_depth_nothreads(
    globus_callback_space_t             space)
{
    globus_l_callback_space_t *         i_space;
    
    if(space == GLOBUS_CALLBACK_GLOBAL_SPACE)
    {
        i_space = &globus_l_callback_global_space;
    }
    else
    {
        i_space = (globus_l_callback_space_t *)
            globus_handle_table_lookup(
                &globus_l_callback_space_table, space);
        if(!i_space)
        {
            return -1;
        }
    }
    
    return i_space->depth;
}

globus_bool_t
globus_callback_space_is_single_nothreads(
    globus_callback_space_t             space)
{
    return GLOBUS_TRUE;
}

/**
 * globus_callback_get_timeout
 *
 * returns true if already timed out.. remaining time is in time_left
 */

globus_bool_t
globus_callback_get_timeout_nothreads(
    globus_reltime_t *                  time_left)
{
    globus_l_callback_space_t *         i_space;
    globus_l_callback_info_t *          peek;
    
    if(!globus_l_callback_restart_info)
    {
        GlobusTimeReltimeCopy(*time_left, globus_i_reltime_infinity);

        return GLOBUS_FALSE;
    }
    
    i_space = globus_l_callback_restart_info->active_space;
    GlobusICallbackReadyPeak(&i_space->ready_queue, peek);
    if(!peek && i_space->handle != GLOBUS_CALLBACK_GLOBAL_SPACE)
    {
        GlobusICallbackReadyPeak(
            &globus_l_callback_global_space.ready_queue, peek);
    }
    
    if(peek)
    {
        GlobusTimeReltimeCopy(*time_left, globus_i_reltime_zero);
        
        return GLOBUS_TRUE;
    }
    else
    {
        globus_abstime_t                time_now;
        const globus_abstime_t *        space_time;
        const globus_abstime_t *        global_time;
        const globus_abstime_t *        earlier_time;
        
        global_time = GLOBUS_NULL;
        
        space_time = (globus_abstime_t *)
            globus_priority_q_first_priority(&i_space->timed_queue);
        if(i_space->handle != GLOBUS_CALLBACK_GLOBAL_SPACE)
        {
            global_time = (globus_abstime_t *)
                globus_priority_q_first_priority(
                    &globus_l_callback_global_space.timed_queue);
        }
        
        earlier_time = space_time;
        if(space_time && global_time)
        {
            if(globus_abstime_cmp(space_time, global_time) > 0)
            {
                earlier_time = global_time;
            }
        }
        else if(global_time)
        {
            earlier_time = global_time;
        }
        
        if(!earlier_time || globus_abstime_cmp(
            earlier_time, globus_l_callback_restart_info->time_stop) > 0)
        {
            earlier_time = globus_l_callback_restart_info->time_stop;
        }
        
        GlobusTimeAbstimeGetCurrent(time_now);
        if(globus_abstime_cmp(&time_now, earlier_time) >= 0)
        {
            GlobusTimeReltimeCopy(*time_left, globus_i_reltime_zero);
    
            return GLOBUS_TRUE;
        }
        else if(globus_time_abstime_is_infinity(earlier_time))
        {
            GlobusTimeReltimeCopy(*time_left, globus_i_reltime_infinity);
        }
        else
        {
            GlobusTimeAbstimeDiff(*time_left, time_now, *earlier_time);
        }
    }
    
    return GLOBUS_FALSE;
}

globus_bool_t
globus_callback_has_time_expired_nothreads()
{
    globus_reltime_t                    time_left;
    
    return globus_callback_get_timeout(&time_left);
}

globus_bool_t
globus_callback_was_restarted_nothreads()
{
    return globus_l_callback_restart_info
        ? globus_l_callback_restart_info->restarted
        : GLOBUS_FALSE;
}

static
globus_bool_t
globus_l_callback_uncatchable_signal(
    int                                 signum)
{
#ifndef _WIN32
/* i would have used a switch here, but some of the signal numbers have the
 * same value
 */
    if(
#ifdef SIGKILL
        signum == SIGKILL ||
#endif
#ifdef SIGSEGV
        signum == SIGSEGV ||
#endif
#ifdef SIGABRT
        signum == SIGABRT ||
#endif
#ifdef SIGBUS
        signum == SIGBUS ||
#endif
#ifdef SIGFPE
        signum == SIGFPE ||
#endif
#ifdef SIGILL
        signum == SIGILL ||
#endif
#ifdef SIGIOT
        signum == SIGIOT ||
#endif
#ifdef SIGPIPE
        signum == SIGPIPE ||
#endif
#ifdef SIGEMT
        signum == SIGEMT ||
#endif
#ifdef SIGSYS
        signum == SIGSYS ||
#endif
#ifdef SIGTRAP
        signum == SIGTRAP ||
#endif
#ifdef SIGSTOP
        signum == SIGSTOP ||
#endif
#ifdef SIGCONT
        signum == SIGCONT ||
#endif
#ifdef SIGWAITING
        signum == SIGWAITING ||
#endif
        0)
    {
        return GLOBUS_TRUE;
    }
    else
#endif
    {
        return GLOBUS_FALSE;
    }
}

#ifdef HAVE_SIGACTION
static
void
globus_l_callback_signal_handler(
    int                                 signum)
{
    globus_l_callback_signal_handler_t *handler = NULL;
    
    if(globus_l_callback_signal_handlers &&
        signum >= 0 && signum < globus_l_callback_signal_handlers_size)
    {
        handler = globus_l_callback_signal_handlers[signum];
        if(handler)
        {
            globus_l_callback_wakeup_handler_t * wakeup_handler;
            globus_list_t *             tmp;
            
            handler->pending = GLOBUS_TRUE;
            globus_l_callback_signal_pending = GLOBUS_TRUE;
            
            /* wake up folks */
            for(tmp = globus_l_callback_wakeup_handlers;
                !globus_list_empty(tmp);
                tmp = globus_list_rest(tmp))
            {
                wakeup_handler = (globus_l_callback_wakeup_handler_t *)
                    globus_list_first(tmp);
                    
                wakeup_handler->wakeup(wakeup_handler->user_arg);
            }
            
            if(globus_l_callback_restart_info)
            {
                /* just in case I missed somebody, don't let them take anymore
                 * time
                 */
                globus_l_callback_restart_info->time_stop = 
                    &globus_i_abstime_zero;
                globus_l_callback_restart_info->signaled = GLOBUS_TRUE;
            }
        }
    }
}
#endif /* HAVE_SIGACTION */

globus_result_t
globus_callback_space_register_signal_handler_nothreads(
    int                                 signum,
    globus_bool_t                       persist,
    globus_callback_func_t              callback_func,
    void *                              callback_user_arg,
    globus_callback_space_t             space)
{
    globus_l_callback_signal_handler_t *handler;
    globus_result_t                     result;
    
    if(!callback_func)
    {
        result = GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_ARGUMENT(
            "globus_callback_space_register_signal_handler", "callback_func");
        goto error_params;
    }
    
    if(globus_l_callback_uncatchable_signal(signum) ||
        signum < 0 ||
        (signum < globus_l_callback_signal_handlers_size &&
            globus_l_callback_signal_handlers[signum]))
    {
        result = GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_ARGUMENT(
            "globus_callback_space_register_signal_handler", "signum");
        goto error_params;
    }
    
    result = globus_callback_space_reference(space);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_params;
    }
    
    handler = (globus_l_callback_signal_handler_t *)
        globus_calloc(1, sizeof(globus_l_callback_signal_handler_t));
    if(!handler)
    {
        result = GLOBUS_L_CALLBACK_CONSTRUCT_MEMORY_ALLOC(
            "globus_callback_space_register_signal_handler", "handler");
        goto error_handler;
    }
    
    handler->callback = callback_func;
    handler->user_arg = callback_user_arg;
    handler->space = space;
    handler->persist = persist;
    
#ifdef HAVE_SIGACTION
    {
        struct sigaction                action;
        
        memset(&action, '\0', sizeof(action));
        sigemptyset(&action.sa_mask);
        action.sa_handler = globus_l_callback_signal_handler;
        if(sigaction(signum, &action, &handler->old_action) < 0)
        {
            result = GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_ARGUMENT(
                "globus_callback_space_register_signal_handler", "signum");
            goto error_action;
        }
    }
#endif

    if(signum >= globus_l_callback_signal_handlers_size)
    {
        globus_l_callback_signal_handler_t ** volatile old_table;
        globus_l_callback_signal_handler_t ** new_table;
        int                             new_size;
        
        /* cant let a signal handler muck with this while i change it,
         * might miss a signal, too bad.  not likely to ever need to increase
         * size anyway and, even if we did, most sig handlers are registered
         * before program does anything
         */
        old_table = globus_l_callback_signal_handlers;
        globus_l_callback_signal_handlers = GLOBUS_NULL;
        
        new_size = globus_l_callback_signal_handlers_size + 
            GLOBUS_L_CALLBACK_SIGNAL_BLOCK_SIZE;
        if(signum >= new_size)
        {
            new_size = signum + 1;
        }
        
        new_table = (globus_l_callback_signal_handler_t **)
            globus_realloc(
                old_table,
                new_size * sizeof(globus_l_callback_signal_handler_t *));
        if(!new_table)
        {
            globus_l_callback_signal_handlers = old_table;
            result = GLOBUS_L_CALLBACK_CONSTRUCT_MEMORY_ALLOC(
                "globus_callback_space_register_signal_handler",
                "new_table");
            goto error_resize;
        }
        memset(
            new_table + 
                globus_l_callback_signal_handlers_size * 
                sizeof(globus_l_callback_signal_handler_t *),
            0,
            GLOBUS_L_CALLBACK_SIGNAL_BLOCK_SIZE *
                sizeof(globus_l_callback_signal_handler_t *));
        globus_l_callback_signal_handlers = new_table;
        globus_l_callback_signal_handlers_size = new_size;
    }
    
    globus_l_callback_signal_handlers[signum] = handler;
    
    return GLOBUS_SUCCESS;

error_resize:
#ifdef HAVE_SIGACTION
    sigaction(signum, &handler->old_action, GLOBUS_NULL);
error_action:
#endif
    globus_free(handler);
error_handler:
    globus_callback_space_destroy(space);
error_params:
    return result;
}

globus_result_t
globus_callback_unregister_signal_handler_nothreads(
    int                                 signum,
    globus_callback_func_t              unregister_callback,
    void *                              unreg_arg)
{
    globus_l_callback_signal_handler_t *handler;
    globus_result_t                     result;
    
    if(signum >= globus_l_callback_signal_handlers_size ||
        signum < 0 ||
        !globus_l_callback_signal_handlers[signum])
    {
        result = GLOBUS_L_CALLBACK_CONSTRUCT_INVALID_ARGUMENT(
            "globus_callback_space_unregister_signal_handler", "signum");
        goto error_params;
    }
    
    handler = globus_l_callback_signal_handlers[signum];
    globus_l_callback_signal_handlers[signum] = GLOBUS_NULL;
    
#ifdef HAVE_SIGACTION
    sigaction(signum, &handler->old_action, GLOBUS_NULL);
#endif
    
    if(!handler->running)
    {
        result = GLOBUS_SUCCESS;
        if(unregister_callback)
        {
            result = globus_callback_space_register_oneshot(
                GLOBUS_NULL,
                GLOBUS_NULL,
                unregister_callback,
                unreg_arg,
                handler->space);
        }
        
        globus_callback_space_destroy(handler->space);
        globus_free(handler);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_register;
        }
    }
    else
    {
        handler->persist = GLOBUS_FALSE;
        handler->unregister_callback = unregister_callback;
        handler->unreg_arg = unreg_arg;
    }

    return GLOBUS_SUCCESS;

error_register:
error_params:
    return result;
}

void
globus_callback_add_wakeup_handler_nothreads(
    void                                (*wakeup)(void *),
    void *                              user_arg)
{
    globus_l_callback_wakeup_handler_t *wakeup_handler;
    
    wakeup_handler = (globus_l_callback_wakeup_handler_t *)
        globus_malloc(sizeof(globus_l_callback_wakeup_handler_t));
    if(wakeup_handler)
    {
        wakeup_handler->wakeup = wakeup;
        wakeup_handler->user_arg = user_arg;
        
        globus_list_insert(&globus_l_callback_wakeup_handlers, wakeup_handler);
    }
}

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
