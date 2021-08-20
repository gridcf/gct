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


#include "globus_thread_rw_mutex.h"
#include "globus_libc.h"

typedef struct globus_i_rw_mutex_waiter_s
{
    globus_cond_t                       cond;
    globus_bool_t                       is_reader;
    globus_bool_t                       acquired;
    struct globus_i_rw_mutex_waiter_s * pnext;
} globus_i_rw_mutex_waiter_t;

static
int
globus_i_rw_mutex_wait(
    globus_rw_mutex_t *                 rw_lock,
    globus_bool_t                       is_reader)
{
    globus_i_rw_mutex_waiter_t *        waiter;
    int                                 rc;

    if(rw_lock->idle)
    {
        /* use one of the idle waiter structures */
        waiter = rw_lock->idle;
        rw_lock->idle = waiter->pnext;
    }
    else
    {
        /* no idle waiter structures, allocate one now */
        waiter = (globus_i_rw_mutex_waiter_t *)
            globus_malloc(sizeof(globus_i_rw_mutex_waiter_t));
        if(waiter)
        {
            rc = globus_cond_init(&waiter->cond, GLOBUS_NULL);
            if(rc != 0)
            {
                globus_free(waiter);
                waiter = GLOBUS_NULL;
            }
        }

        if(!waiter)
        {
            return -1;
        }
    }

    /* initialize waiter and put on queue */
    waiter->is_reader = is_reader;
    waiter->acquired = GLOBUS_FALSE;
    waiter->pnext = GLOBUS_NULL;
    
    *rw_lock->tail = waiter;
    rw_lock->tail = &waiter->pnext;

    while(!waiter->acquired)
    {
        globus_cond_wait(&waiter->cond, &rw_lock->mutex);
    }

    /* acquired lock, put waiter structure in idle list for reuse */
    waiter->pnext = rw_lock->idle;
    rw_lock->idle = waiter;

    return 0;
}

static
void
globus_i_rw_mutex_signal(
    globus_rw_mutex_t *                 rw_lock)
{
    globus_i_rw_mutex_waiter_t *        waiter;

    /* when this called, writing is definitely false, but there may still be
     * readers
     */
    waiter = rw_lock->waiters;

    if(waiter)
    {
        if(waiter->is_reader)
        {
            do
            {
                rw_lock->readers++;
                waiter->acquired = GLOBUS_TRUE;
                globus_cond_signal(&waiter->cond);

                /* just signaled this reader, take out of the queue */
                rw_lock->waiters = waiter = waiter->pnext;
            } while(waiter && waiter->is_reader);
        }
        else if(rw_lock->readers == 0)
        {
            /* take this writer out of the queue and signal it */
            rw_lock->waiters = waiter->pnext;

            rw_lock->writing = GLOBUS_TRUE;
            waiter->acquired = GLOBUS_TRUE;
            globus_cond_signal(&waiter->cond);
        }

        /* if we took the last waiter, reset the tail pointer */
        if(!rw_lock->waiters)
        {
            rw_lock->tail = &rw_lock->waiters;
        }
    }
}

static
int
globus_i_rw_mutex_readlock(
    globus_rw_mutex_t *                 rw_lock)
{
    int                                 rc;

    if(rw_lock->writing || rw_lock->waiters)
    {
        /* lock is busy, wait for it */
        rc = globus_i_rw_mutex_wait(rw_lock, GLOBUS_TRUE);
    }
    else
    {
        rw_lock->readers++;
        rc = 0;
    }

    return rc;
}

static
int
globus_i_rw_mutex_writelock(
    globus_rw_mutex_t *                 rw_lock)
{
    int                                 rc;

    if(rw_lock->readers > 0 || rw_lock->writing)
    {
        /* lock is busy, wait for it */
        rc = globus_i_rw_mutex_wait(rw_lock, GLOBUS_FALSE);
    }
    else
    {
        rw_lock->writing = GLOBUS_TRUE;
        rc = 0;
    }

    return rc;
}

static
void
globus_i_rw_mutex_readunlock(
    globus_rw_mutex_t *                 rw_lock)
{
    rw_lock->readers--;
    globus_i_rw_mutex_signal(rw_lock);
}

static
void
globus_i_rw_mutex_writeunlock(
    globus_rw_mutex_t *                 rw_lock)
{
    rw_lock->writing = GLOBUS_FALSE;
    globus_i_rw_mutex_signal(rw_lock);
}

int
globus_rw_mutex_init(
    globus_rw_mutex_t *                 rw_lock,
    globus_rw_mutexattr_t *             attr)
{
    rw_lock->waiters = GLOBUS_NULL;
    rw_lock->tail = &rw_lock->waiters;
    rw_lock->idle = GLOBUS_NULL;
    rw_lock->writing = GLOBUS_FALSE;
    rw_lock->readers = 0;

    return globus_mutex_init(&rw_lock->mutex, GLOBUS_NULL);
}

int
globus_rw_mutex_readlock(
    globus_rw_mutex_t *                 rw_lock)
{
    int                                 rc;

    globus_mutex_lock(&rw_lock->mutex);
    {
        rc = globus_i_rw_mutex_readlock(rw_lock);
    }
    globus_mutex_unlock(&rw_lock->mutex);

    return rc;
}

int
globus_rw_mutex_writelock(
    globus_rw_mutex_t *                 rw_lock)
{
    int                                 rc;

    globus_mutex_lock(&rw_lock->mutex);
    {
        rc = globus_i_rw_mutex_writelock(rw_lock);
    }
    globus_mutex_unlock(&rw_lock->mutex);

    return rc;
}

int
globus_rw_mutex_readunlock(
    globus_rw_mutex_t *                 rw_lock)
{
    globus_mutex_lock(&rw_lock->mutex);
    {
        globus_i_rw_mutex_readunlock(rw_lock);
    }
    globus_mutex_unlock(&rw_lock->mutex);

    return 0;
}

int
globus_rw_mutex_writeunlock(
    globus_rw_mutex_t *                 rw_lock)
{
    globus_mutex_lock(&rw_lock->mutex);
    {
        globus_i_rw_mutex_writeunlock(rw_lock);
    }
    globus_mutex_unlock(&rw_lock->mutex);

    return 0;
}

int
globus_rw_mutex_destroy(
    globus_rw_mutex_t *                 rw_lock)
{
    globus_i_rw_mutex_waiter_t *        waiter;
    globus_i_rw_mutex_waiter_t *        save;

    globus_mutex_lock(&rw_lock->mutex);
    {
        if(rw_lock->readers > 0 || rw_lock->writing)
        {
            globus_mutex_unlock(&rw_lock->mutex);
            globus_assert(
                0 && "globus_rw_mutex_destroy() Destroying BUSY lock");
            return -1;
        }

        waiter = rw_lock->idle;
        while(waiter)
        {
            save = waiter->pnext;

            globus_cond_destroy(&waiter->cond);
            globus_free(waiter);

            waiter = save;
        }
    }
    globus_mutex_unlock(&rw_lock->mutex);

    globus_mutex_destroy(&rw_lock->mutex);

    return 0;
}

int
globus_rw_cond_wait(
    globus_cond_t *                     cond,
    globus_rw_mutex_t *                 rw_lock)
{
    int                                 rc;
    globus_bool_t                       reading;

    globus_mutex_lock(&rw_lock->mutex);
    {
        reading = (rw_lock->readers > 0);

        if(reading)
        {
            globus_i_rw_mutex_readunlock(rw_lock);
        }
        else
        {
            globus_i_rw_mutex_writeunlock(rw_lock);
        }

        rc = globus_cond_wait(cond, &rw_lock->mutex);

        if(reading)
        {
            globus_i_rw_mutex_readlock(rw_lock);
        }
        else
        {
            globus_i_rw_mutex_writelock(rw_lock);
        }
    }
    globus_mutex_unlock(&rw_lock->mutex);

    return rc;
}

int
globus_rw_cond_timedwait(
    globus_cond_t *                     cond,
    globus_rw_mutex_t *                 rw_lock,
    globus_abstime_t *                  abstime)
{
    int                                 rc;
    globus_bool_t                       reading;

    globus_mutex_lock(&rw_lock->mutex);
    {
        reading = (rw_lock->readers > 0);

        if(reading)
        {
            globus_i_rw_mutex_readunlock(rw_lock);
        }
        else
        {
            globus_i_rw_mutex_writeunlock(rw_lock);
        }

        rc = globus_cond_timedwait(cond, &rw_lock->mutex, abstime);

        if(reading)
        {
            globus_i_rw_mutex_readlock(rw_lock);
        }
        else
        {
            globus_i_rw_mutex_writelock(rw_lock);
        }
    }
    globus_mutex_unlock(&rw_lock->mutex);

    return rc;
}
