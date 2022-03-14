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
 * @file timedwait_test.c
 * @brief globus_cond_timedwait() tests
 */

#include "globus_common.h"
#include "globus_test_tap.h"

static globus_mutex_t mutex;
static globus_cond_t cond;
static volatile globus_bool_t done;

static
void
wait_func(int timeout,
	  globus_bool_t * signalled,
	  globus_bool_t * timedout);

static
void
wakeup_func(
    void *			        arg)
{
    globus_mutex_lock(&mutex);
    done = GLOBUS_TRUE;
    globus_cond_signal(&cond);
    globus_mutex_unlock(&mutex);
}

/**
 * @brief Tests for globus_cond_timedwait
 */
int
timedwait_test(void)
{
    int			i;
    globus_reltime_t    delay_time;
    /* Test definitions:
     * time when signal function should be called,
     * timeout for globus_cond_timedwait()
     */
    struct test
    {
        int signal_time;
        int timeout_time;
        const char *name;
    };
    struct test tests[] = 
    {
        /**
         * @test
         * Call globus_cond_signal() before globus_cond_timedwait() timeout
         */
        { 1, 5, "signal_before_timeout" },
        /**
         * @test
         * Call globus_cond_signal() after globus_cond_timedwait() timeout
         */
        { 5, 1, "timeout_before_signal" },
        /**
         * @test
         * Call globus_cond_signal() before calling globus_cond_timedwait()
         */
        { 5, -1, "timeout_before_cond_wait_called" }
    };


    globus_module_activate(GLOBUS_COMMON_MODULE);

    globus_mutex_init(&mutex, GLOBUS_NULL);
    globus_cond_init(&cond, GLOBUS_NULL);

    printf("1..3\n");

    for (i = 0; i < 3; i++)
    {
	globus_bool_t signalled = GLOBUS_FALSE;
	globus_bool_t timedout = GLOBUS_FALSE;

	fprintf(stderr, "Test %d: Signal at %d, timeout at %d\n",
	                   i+1,
	                   tests[i].signal_time,
	                   tests[i].timeout_time);

	done = GLOBUS_FALSE;

        GlobusTimeReltimeSet(delay_time, tests[i].signal_time, 0);	
	globus_callback_register_oneshot(GLOBUS_NULL,
	                                 &delay_time,
					 wakeup_func,
					 GLOBUS_NULL);
	wait_func(tests[i].timeout_time,
		  &signalled,
		  &timedout);
	ok((((tests[i].signal_time < tests[i].timeout_time) && signalled) ||
	   ((tests[i].signal_time > tests[i].timeout_time) && !signalled)), 
           tests[i].name);
    }

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return TEST_EXIT_CODE;
}

int main(int argc, char *argv[])
{
    return timedwait_test();
}

static
void
wait_func(int delta,
	  int * signalled,
	  int * timedout)
{
    int			save_errno = 0;
    globus_abstime_t	timeout;

    globus_mutex_lock(&mutex);

    /* Absolute timeout */
    timeout.tv_sec = time(GLOBUS_NULL) + delta;
    timeout.tv_nsec = 0;

    while(!done)
    {
	save_errno = globus_cond_timedwait(&cond,
					   &mutex,
					   &timeout);
	if(save_errno == ETIMEDOUT)
	{
	    /* time-out occurred */
	    *timedout = GLOBUS_TRUE;
	    break;
	}
    }
    *signalled = done;

    globus_mutex_unlock(&mutex);
}
