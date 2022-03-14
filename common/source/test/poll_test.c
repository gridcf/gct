/*
 * Copyright 1999-2006 University of Chicago
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#include "globus_common.h"
#include "globus_test_tap.h"

globus_bool_t   test_failed;
int             basic_test_count;

void
basic_handler(
              void *user_arg);
globus_bool_t
                basic_test();

globus_mutex_t  basic_test_mutex;
globus_cond_t   basic_test_cond;
globus_bool_t   basic_test_done;

/* basic_periodic added to test req 2438 */
void
basic_periodic_handler(
                       void *user_arg);
globus_bool_t
                basic_periodic_test();

globus_mutex_t  basic_periodic_test_mutex;
globus_cond_t   basic_periodic_test_cond;
globus_bool_t   basic_periodic_test_done;
int             basic_periodic_test_ctr;


int             cancel_count;
void
cancel_signal_handler(
                      void *user_arg);

void
cancel_handler(
               void *user_arg);
globus_bool_t
                cancel_test();

globus_mutex_t  cancel_test_mutex;
globus_cond_t   cancel_test_cond;
globus_bool_t   cancel_test_done;
globus_mutex_t  cancel_signal_mutex;
globus_cond_t   cancel_signal_cond;
globus_bool_t   cancel_signal_done;

void
nested_wait_handler(
                    void *user_arg);
globus_bool_t
                nested_wait_test();

globus_mutex_t  nested_wait_A_mutex;
globus_cond_t   nested_wait_A_cond;
globus_mutex_t  nested_wait_B_mutex;
globus_cond_t   nested_wait_B_cond;
globus_bool_t   nested_wait_test_done;
globus_bool_t
                time_starve_test();

void            verbose_printf(int level, char *s,...);

void
time_starve_handler_1(
                      void *user_arg);

globus_bool_t   globus_l_callback_test_done = GLOBUS_FALSE;

int             verbose_print_level = 0;

/*
 * Most Unix systems define ETIME and ETIMEDOUT. Unicos defines ETIMEDOUT and
 * ETIMEOUT. The IS_TIMEOUT macro accounts for this variation.
 */

#ifdef ETIME
#define IS_ETIME(err) ((err) == ETIME)
#else
#define IS_ETIME(err) 0
#endif

#ifdef ETIMEDOUT
#define IS_ETIMEDOUT(err) ((err) == ETIMEDOUT)
#else
#define IS_ETIMEDOUT(err) 0
#endif

#ifdef ETIMEOUT
#define IS_ETIMEOUT(err) ((err) == ETIMEOUT)
#else
#define IS_ETIMEOUT(err) 0
#endif

#define IS_TIMEOUT(err) (IS_ETIME(err)     || \
                       IS_ETIMEDOUT(err) || \
                       IS_ETIMEOUT(err))

/*
 * CPU HOG test
 */
globus_bool_t   globus_l_cpu_hog_test;
globus_mutex_t  cpu_hog_mutex;
globus_abstime_t cpu_hog_stop_time;
globus_bool_t
                cpu_hog_restart_test();

void
cpu_hog_restart_block_handler(
                              void *user_arg);

void
cpu_hog_restart_time_handler(
                             void *user_arg);

/* END CPU HOG TEST */

/*
 * RANDOM STRESS test
 */
#define RANDOM_STRESS_TIMES_TO_TEST   10
static globus_mutex_t random_stress_mutex;
static globus_mutex_t random_stress_registered_mutex;
static globus_cond_t random_stress_cond;
static globus_mutex_t random_stress_own_thread_mutex;
static globus_cond_t random_stress_own_thread_cond;

static globus_bool_t random_stress_registered;
static globus_bool_t random_stress_success;
static globus_bool_t random_stress_done;
static globus_bool_t random_stress_wakeme;
static globus_callback_handle_t stress_callback_handle;
globus_bool_t
                random_stress_test();

void
random_stress_queued_handler(
                             void *user_arg);

void
random_stress_whatever_handler(
                               void *user_arg);

void
random_stress_count_handler(
                            void *user_arg);

/* end random stress test */

/*
 * ADJUST PERIOD TEST
 */
static globus_mutex_t adjust_test_mutex;
static globus_cond_t adjust_test_cond;
static int      adjust_test_done;
static globus_bool_t adjust_test_success;
static globus_callback_handle_t adjust_callback_handle_1;
static globus_callback_handle_t adjust_callback_handle_2;
globus_bool_t
                adjust_period_test();

void
adjust_period_requeue_callback(
                               void *user_args);

void
adjust_period_set_infinity_callback(
                                    void *user_args);

/* END ADJUST PERIOD TEST */

int
main(int argc, char *argv[])
{
    int                                 ctr;

    for (ctr = 0; ctr < argc; ctr++)
    {
        if (strcmp(argv[ctr], "-verbose") == 0)
        {
            if (ctr + 1 >= argc)
            {
                verbose_print_level = 1;
            }
            else
            {
                verbose_print_level = atoi(argv[ctr + 1]);
                ctr++;
            }
        }
    }

    basic_test_count = 0;
    ctr = 1;

    printf("1..7\n");
    verbose_printf(0, "==================================\n");
    verbose_printf(0, "   Starting poll tests pass %d\n", ctr);
    verbose_printf(0, "==================================\n");
    verbose_printf(0, "----------------------------------\n");
    verbose_printf(0, "Performing basic one_shot test [pass %d]\n", ctr);
    ok(basic_test(), "basic_test");

    verbose_printf(0, "----------------------------------\n");
    verbose_printf(0, "Performing basic periodic test [pass %d]\n", ctr);
    ok(basic_periodic_test(), "basic_periodic_test");

    verbose_printf(0, "----------------------------------\n");
    verbose_printf(0, "Performing nested wait test [pass %d]\n", ctr);
    ok(nested_wait_test(), "nested_wait_test");

    verbose_printf(0, "----------------------------------\n");
    verbose_printf(0, "Performing Time/Starvation Test [pass %d]\n", ctr);
    ok(time_starve_test(), "time_starve_test");

    verbose_printf(0, "----------------------------------\n");
    verbose_printf(0, "CPU HOG Test [pass %d]\n", ctr);
    ok(cpu_hog_restart_test(), "cpu_hog_restart_test");

    verbose_printf(0, "----------------------------------\n");
    verbose_printf(0, "Random Stress Test [pass %d]\n", ctr);
    ok(random_stress_test(), "random_stress_test");

    verbose_printf(0, "----------------------------------\n");
    verbose_printf(0, "Adjust Period Test [pass %d]\n", ctr);
    ok(adjust_period_test(), "adjust_period_test");

    verbose_printf(0, "==================================\n");
    verbose_printf(0, "        End poll tests \n");
    verbose_printf(0, "==================================\n");

    return TEST_EXIT_CODE;
}
/*
 * BASIC TEST
 */
globus_bool_t
basic_test()
{
    globus_reltime_t                    start_time;

    GlobusTimeReltimeSet(start_time, 0, 0);

    basic_test_done = GLOBUS_FALSE;

    globus_module_activate(GLOBUS_CALLBACK_MODULE);

    globus_mutex_init(&basic_test_mutex,
                      (globus_mutexattr_t *)GLOBUS_NULL);
    globus_cond_init(&basic_test_cond,
                     (globus_condattr_t *)GLOBUS_NULL);

/* register basic callback */
    verbose_printf(3, "basic_test(), registering oneshot\n");
    globus_callback_register_oneshot(
                                     GLOBUS_NULL,
                                     &start_time,
                                     basic_handler,
                                     GLOBUS_NULL);

    globus_mutex_lock(&basic_test_mutex);
    {
        verbose_printf(3, "basic_test(), waiting for end\n");
        while (!basic_test_done)
        {
            globus_cond_wait(
                             &basic_test_cond,
                             &basic_test_mutex);
        }
    }
    globus_mutex_unlock(&basic_test_mutex);

    globus_mutex_destroy(&basic_test_mutex);
    globus_cond_destroy(&basic_test_cond);

    globus_module_deactivate(GLOBUS_CALLBACK_MODULE);

    return GLOBUS_TRUE;
}

void
basic_handler(
              void *user_arg)
{
    verbose_printf(1, "basic_handler(), start\n");
    basic_test_count++;
    if (basic_test_count > 1)
    {
        verbose_printf(0, "*** Basic Test failed *** one shot called more than once\n");
        test_failed = GLOBUS_TRUE;
    }
    globus_mutex_lock(&basic_test_mutex);
    {
        basic_test_done = GLOBUS_TRUE;
        verbose_printf(1, "basic_handler(), signaling basic wait.\n");
        globus_cond_signal(&basic_test_cond);
    }
    globus_mutex_unlock(&basic_test_mutex);
}
/* END BASIC TEST */


/* BASIC PERIODIC TEST */

void
basic_periodic_handler(
                       void *user_arg)
{
    verbose_printf(2, "basic_periodic_handler() : called\n");

    basic_periodic_test_ctr++;
}
globus_bool_t
basic_periodic_test()
{
    globus_reltime_t                    start_time;
    globus_reltime_t                    period_time;
    globus_abstime_t                    wait_time;

    globus_module_activate(GLOBUS_CALLBACK_MODULE);

    basic_periodic_test_ctr = 0;
    GlobusTimeReltimeSet(start_time, 2, 0);
    GlobusTimeReltimeSet(period_time, 2, 0);

    basic_periodic_test_done = GLOBUS_FALSE;

    globus_mutex_init(
                      &basic_periodic_test_mutex,
                      (globus_mutexattr_t *)GLOBUS_NULL);
    globus_cond_init(
                     &basic_periodic_test_cond,
                     (globus_condattr_t *)GLOBUS_NULL);

    verbose_printf(3, "basic_periodic_test(), registering handler\n");
    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &start_time,
                                      &period_time,
                                      basic_periodic_handler,
                                      GLOBUS_NULL);

    verbose_printf(1, "waiting 20 secs\n");
    GlobusTimeAbstimeSet(wait_time, 20, 0);
    globus_mutex_lock(&basic_periodic_test_mutex);
    {
        int                                 save_errno;

        do
        {
            save_errno = globus_cond_timedwait(
                                               &basic_periodic_test_cond,
                                               &basic_periodic_test_mutex,
                                               &wait_time);
        }
        while (save_errno != ETIMEDOUT);
    }
    globus_mutex_unlock(&basic_periodic_test_mutex);

    globus_module_deactivate(GLOBUS_CALLBACK_MODULE);

    globus_mutex_destroy(&basic_periodic_test_mutex);
    globus_cond_destroy(&basic_periodic_test_cond);

    if (basic_periodic_test_ctr > 0)
    {
        return GLOBUS_TRUE;
    }
    else
    {
        return GLOBUS_FALSE;
    }
}
/* END BASIC PERIODIC TEST */


/*
 * NESTED WAIT TEST
 */
globus_bool_t
nested_wait_test()
{
    globus_reltime_t                    start_time;

    GlobusTimeReltimeSet(start_time, 0, 0);
    globus_module_activate(GLOBUS_CALLBACK_MODULE);

    globus_mutex_init(&nested_wait_A_mutex, GLOBUS_NULL);
    globus_cond_init(&nested_wait_A_cond, GLOBUS_NULL);
    globus_mutex_init(&nested_wait_B_mutex, GLOBUS_NULL);
    globus_cond_init(&nested_wait_B_cond, GLOBUS_NULL);
/* register basic callback */
    verbose_printf(3, "nested_wait_test() : resigtering peridoic event\n");
    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &start_time,
                                      &start_time,
                                      nested_wait_handler,
                                      GLOBUS_NULL);

    globus_mutex_lock(&nested_wait_A_mutex);
    {
        while (!nested_wait_test_done)
        {
            verbose_printf(1, "nested_wait_test() : waiting on A\n");
            globus_cond_wait(
                             &nested_wait_A_cond,
                             &nested_wait_A_mutex);
        }
    }
    globus_mutex_unlock(&nested_wait_A_mutex);

    globus_mutex_destroy(&nested_wait_A_mutex);
    globus_cond_destroy(&nested_wait_A_cond);
    globus_mutex_destroy(&nested_wait_B_mutex);
    globus_cond_destroy(&nested_wait_B_cond);

    globus_module_deactivate(GLOBUS_CALLBACK_MODULE);

    return GLOBUS_TRUE;
}

void
nested_wait_handler(
                    void *user_arg)
{
    static int                          count = 0;
    static globus_bool_t                signaled = GLOBUS_FALSE;

    verbose_printf(10, "nested_wait_handler() : entering\n");
    count++;
    if (count == 5)
    {
        globus_mutex_lock(&nested_wait_B_mutex);
        {
            while (!signaled)
            {
                verbose_printf(3, "nested_wait_handler() : waiting on B\n");
                globus_cond_wait(
                                 &nested_wait_B_cond,
                                 &nested_wait_B_mutex);
            }
        }
        globus_mutex_unlock(&nested_wait_B_mutex);

        globus_mutex_lock(&nested_wait_A_mutex);
        {
            verbose_printf(3, "nested_wait_handler() : signaling A\n");
            nested_wait_test_done = GLOBUS_TRUE;
            globus_cond_signal(&nested_wait_A_cond);
        }
        globus_mutex_unlock(&nested_wait_A_mutex);
    }
    else if (count == 10)
    {
        globus_mutex_lock(&nested_wait_B_mutex);
        {
            verbose_printf(3, "nested_wait_handler() : signaling B\n");
            signaled = GLOBUS_TRUE;
            globus_cond_signal(&nested_wait_B_cond);
        }
        globus_mutex_unlock(&nested_wait_B_mutex);
    }
}
/* END NESTED WAIT TEST */

/*
 * MULTIQ TEST
 */
globus_bool_t
time_starve_test()
{
    long                                count_1 = 0;
    long                                count_2 = 0;
    long                                count_3 = 0;
    long                                count_4 = 0;
    long                                count_5 = 0;
    long                                count_6 = 0;
    long                                count_7 = 0;
    long                                secs = 20;
    globus_reltime_t                    period;
    long                                base = 1;
    globus_abstime_t                    wait_time;
    globus_mutex_t                      mutex;
    globus_cond_t                       cond;
    double                              rate1;
    double                              rate2;
    double                              rate3;
    double                              rate4;
    double                              rate5;
    double                              rate6;
    double                              rate7;
    double                              accuracy1;
    double                              accuracy2;
    double                              accuracy3;
    double                              accuracy4;
    double                              accuracy5;
    double                              accuracy6;
    double                              accuracy7;
    globus_reltime_t                    start_time;

    GlobusTimeReltimeSet(start_time, 0, 0);

    globus_module_activate(GLOBUS_COMMON_MODULE);
    GlobusTimeAbstimeSet(wait_time, secs, 0);

    GlobusTimeReltimeSet(period, 1, 0);
    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &start_time,
                                      &period,
                                      time_starve_handler_1,
                                      (void *)&count_7);

    GlobusTimeReltimeSet(period, 0, 100000);
    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &start_time,
                                      &period,
                                      time_starve_handler_1,
                                      (void *)&count_6);

    GlobusTimeReltimeSet(period, 0, 10000);
    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &start_time,
                                      &period,
                                      time_starve_handler_1,
                                      (void *)&count_5);

    GlobusTimeReltimeSet(period, 0, 1000);
    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &start_time,
                                      &period,
                                      time_starve_handler_1,
                                      (void *)&count_4);

    GlobusTimeReltimeSet(period, 0, 100);
    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &start_time,
                                      &period,
                                      time_starve_handler_1,
                                      (void *)&count_3);

    GlobusTimeReltimeSet(period, 0, 10);
    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &start_time,
                                      &period,
                                      time_starve_handler_1,
                                      (void *)&count_2);

    GlobusTimeReltimeSet(period, 0, 1);
    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &start_time,
                                      &period,
                                      time_starve_handler_1,
                                      (void *)&count_1);

    globus_cond_init(&cond,
                     (globus_condattr_t *)GLOBUS_NULL);
    globus_mutex_init(&mutex,
                      (globus_mutexattr_t *)GLOBUS_NULL);

    verbose_printf(1, "Waiting for %d seconds...\n", secs);
    globus_mutex_lock(&mutex);
    {
        globus_bool_t                       expired = GLOBUS_FALSE;
        int                                 save_errno;

#ifdef BUILD_LITE
        while (!expired)
#endif
        {
            save_errno = globus_cond_timedwait(
                                               &cond,
                                               &mutex,
                                               &wait_time);

            if (IS_TIMEOUT(save_errno))
            {
                expired = GLOBUS_TRUE;
            }
        }
    }
    globus_mutex_unlock(&mutex);

    rate1 = (secs * 1000000.0 / (base));
    rate2 = (secs * 1000000.0 / (base * 10));
    rate3 = (secs * 1000000.0 / (base * 100));
    rate4 = (secs * 1000000.0 / (base * 1000));
    rate5 = (secs * 1000000.0 / (base * 10000));
    rate6 = (secs * 1000000.0 / (base * 100000));
    rate7 = (secs * 1000000.0 / (base * 1000000));

    accuracy1 = (double)(count_1) / rate1;
    accuracy2 = (double)(count_2) / rate2;
    accuracy3 = (double)(count_3) / rate3;
    accuracy4 = (double)(count_4) / rate4;
    accuracy5 = (double)(count_5) / rate5;
    accuracy6 = (double)(count_6) / rate6;
    accuracy7 = (double)(count_7) / rate7;


    verbose_printf(1, "calls made/expected     accuracy\n");
    verbose_printf(1, "-------------------     --------\n");
    verbose_printf(1, "  %8d / %8d" "   %10.1f\n",
                   count_1, (long)rate1, accuracy1 * 100);
    verbose_printf(1, "  %8d / %8d" "   %10.1f\n",
                   count_2, (long)rate2, accuracy2 * 100);
    verbose_printf(1, "  %8d / %8d" "   %10.1f\n",
                   count_3, (long)rate3, accuracy3 * 100);
    verbose_printf(1, "  %8d / %8d" "   %10.1f\n",
                   count_4, (long)rate4, accuracy4 * 100);
    verbose_printf(1, "  %8d / %8d" "   %10.1f\n",
                   count_5, (long)rate5, accuracy5 * 100);
    verbose_printf(1, "  %8d / %8d" "   %10.1f\n",
                   count_6, (long)rate6, accuracy6 * 100);
    verbose_printf(1, "  %8d / %8d" "   %10.1f\n",
                   count_7, (long)rate7, accuracy7 * 100);

    globus_mutex_destroy(&mutex);
    globus_cond_destroy(&cond);
    globus_module_deactivate(GLOBUS_COMMON_MODULE);

/*
 * TODO: make a better decision allow them to make 1 to many calls
 */
    if (count_1 == 0 ||
            count_2 == 0 ||
            count_3 == 0 ||
            count_4 == 0 ||
            count_5 == 0 ||
            count_6 == 0 ||
            count_7 == 0 ||
            (accuracy1 - (1.0 + 1.0 / rate1)) > 0.001 ||
            (accuracy2 - (1.0 + 1.0 / rate2)) > 0.001 ||
            (accuracy3 - (1.0 + 1.0 / rate3)) > 0.001 ||
            (accuracy4 - (1.0 + 1.0 / rate4)) > 0.001 ||
            (accuracy5 - (1.0 + 1.0 / rate5)) > 0.001 ||
            (accuracy6 - (1.0 + 1.0 / rate6)) > 0.001 ||
            (accuracy7 - (1.0 + 1.0 / rate7)) > 0.001)
    {
        return GLOBUS_FALSE;
    }
    return GLOBUS_TRUE;
}

void
time_starve_handler_1(
                      void *user_arg)
{
    long                               *count = (long *)user_arg;

    (*count)++;

    verbose_printf(10, "time_starve_handler_1() : entering\n");
}
/* END MULTIQ TEST */
/*
 * CANCEL TEST
 */
void
cancel_signal_handler(
                      void *user_arg)
{
    static int                          count = 0;

    count++;

    if (cancel_test_done)
    {
        return;
    }
    verbose_printf(3, "cancel_signal_handle() : entering\n");
    if (count == 1)
    {
        globus_mutex_lock(&cancel_signal_mutex);
        {
            while (!cancel_signal_done)
            {
                globus_cond_wait(&cancel_signal_cond,
                                 &cancel_signal_mutex);
            }
        }
        globus_mutex_unlock(&cancel_signal_mutex);
    }
    else if (count > 5)
    {
        globus_mutex_lock(&cancel_test_mutex);
        {
            cancel_test_done = GLOBUS_TRUE;
            globus_cond_signal(&cancel_test_cond);
        }
        globus_mutex_unlock(&cancel_test_mutex);
    }
}
/*
 * I no longer trust the cancel test to be testing what I had hoped it would
 * test.
 */
void
cancel_handler(
               void *user_arg)
{
    cancel_count++;

    if (cancel_count == 2)
    {
        globus_callback_handle_t           *handle;

        handle = (globus_callback_handle_t *)user_arg;
        globus_callback_unregister(
                            *handle, GLOBUS_NULL, GLOBUS_NULL, GLOBUS_NULL);
        globus_mutex_lock(&cancel_signal_mutex);
        {
            verbose_printf(3, "signaling cancel_signal_cond\n");
            cancel_signal_done = GLOBUS_TRUE;
            globus_cond_signal(&cancel_signal_cond);
        }
        globus_mutex_unlock(&cancel_signal_mutex);
    }
    else if (cancel_count > 2)
    {
        verbose_printf(0, "**ERROR** Cancel failed\n");
    }
}
globus_bool_t
cancel_test()
{
    globus_callback_handle_t            handle;
    globus_reltime_t                    start_time;
    globus_reltime_t                    period_time;


    GlobusTimeReltimeSet(start_time, 0, 0);
    GlobusTimeReltimeSet(period_time, 0, 10);

    globus_module_activate(GLOBUS_COMMON_MODULE);

    cancel_test_done = GLOBUS_FALSE;
    cancel_signal_done = GLOBUS_FALSE;

    globus_cond_init(&cancel_test_cond,
                     (globus_condattr_t *)GLOBUS_NULL);
    globus_mutex_init(&cancel_test_mutex,
                      (globus_mutexattr_t *)GLOBUS_NULL);
    globus_cond_init(&cancel_signal_cond,
                     (globus_condattr_t *)GLOBUS_NULL);
    globus_mutex_init(&cancel_signal_mutex,
                      (globus_mutexattr_t *)GLOBUS_NULL);

    globus_callback_register_periodic(
                                      &handle,
                                      &start_time,
                                      &period_time,
                                      cancel_handler,
                                      (void *)&handle);

    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &start_time,
                                      &period_time,
                                      cancel_signal_handler,
                                      (void *)&handle);

    globus_mutex_lock(&cancel_test_mutex);
    {
        while (!cancel_test_done)
        {
            globus_cond_wait(&cancel_test_cond,
                             &cancel_test_mutex);
        }
    }
    globus_mutex_unlock(&cancel_test_mutex);


    globus_mutex_destroy(&cancel_test_mutex);
    globus_cond_destroy(&cancel_test_cond);
    globus_mutex_destroy(&cancel_signal_mutex);
    globus_cond_destroy(&cancel_signal_cond);
    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return GLOBUS_TRUE;
}
/* END CANCEL TEST */


void
verbose_printf(int level, char *s,...)
{
    va_list                             ap;


    if (level > verbose_print_level)
    {
        return;
    }
    printf("    ");
    va_start(ap, s);

    vprintf(s, ap);
}
/*
 * CPU HOG TEST
 */
globus_bool_t
cpu_hog_restart_test()
{
    globus_bool_t                       expired = GLOBUS_FALSE;
    int                                 save_errno;
    globus_abstime_t                    wait_time;
    int                                 secs = 5;
    globus_mutex_t                      mutex;
    globus_cond_t                       cond;
    globus_reltime_t                    delay;
    globus_reltime_t                    period;

    globus_l_cpu_hog_test = GLOBUS_TRUE;
    globus_module_activate(GLOBUS_CALLBACK_MODULE);

    GlobusTimeReltimeSet(delay, 0, 0);
    GlobusTimeReltimeSet(period, 0, 500000);
    GlobusTimeAbstimeCopy(cpu_hog_stop_time, globus_i_abstime_infinity);
    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &delay,
                                      &period,
                                      cpu_hog_restart_time_handler,
                                      GLOBUS_NULL);

    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &delay,
                                      &period,
                                      cpu_hog_restart_block_handler,
                                      GLOBUS_NULL);

    globus_cond_init(&cond,
                     (globus_condattr_t *)GLOBUS_NULL);
    globus_mutex_init(&mutex,
                      (globus_mutexattr_t *)GLOBUS_NULL);

    wait_time.tv_sec = time(GLOBUS_NULL) + secs;
    wait_time.tv_nsec = 0;

    verbose_printf(1, "Waiting %d seconds....\n", secs);
    globus_mutex_lock(&mutex);
    {
#ifdef BUILD_LITE
        while (!expired)
#endif
        {
            save_errno = globus_cond_timedwait(
                                               &cond,
                                               &mutex,
                                               &wait_time);

            if (IS_TIMEOUT(save_errno))
            {
                expired = GLOBUS_TRUE;
            }
        }
    }
    globus_mutex_unlock(&mutex);
    globus_module_deactivate(GLOBUS_CALLBACK_MODULE);

    globus_mutex_destroy(&mutex);
    globus_cond_destroy(&cond);

    return globus_l_cpu_hog_test;
}

void
cpu_hog_restart_block_handler(
                              void *user_arg)
{
    globus_reltime_t                    ok_time;
    globus_reltime_t                    time_left;
    int                                 res;

    globus_callback_get_timeout(&time_left);
    GlobusTimeReltimeSet(ok_time, 0, 40000);
    res = globus_reltime_cmp(&time_left, &ok_time);
    if (res < 0)
    {
        verbose_printf(1, "*** CPU HOG POSSIBLE ERROR ***, bad timeout\n");

        verbose_printf(3, "***time left|||\n");
        if (verbose_print_level > 3)
        {
            GlobusTimeReltimePrintf(time_left);
        }
        verbose_printf(3, "***time ok|||\n");
        if (verbose_print_level > 3)
        {
            GlobusTimeReltimePrintf(ok_time);
        }
        GlobusTimeReltimeSet(ok_time, 0, 40000);
        if (globus_reltime_cmp(&time_left, &ok_time) < 0)
        {
            verbose_printf(1, "*** CPU HOG POSSIBLE ERROR ***, bad timeout\n");
        }
    }
    res = globus_reltime_cmp(&time_left,
                             (globus_reltime_t *)&globus_i_reltime_infinity);
    if (res >= 0)
    {
        verbose_printf(1, "***CPU HOG ERROR*** timeout is infinity\n");
/* globus_l_cpu_hog_test = GLOBUS_FALSE; */
    }
    globus_thread_blocking_will_block();
}

void
cpu_hog_restart_time_handler(
                             void *user_arg)
{
}
/* END CPU HOG TEST */

/*
 * RANDOM STRESS test
 */
globus_bool_t
random_stress_test()
{
    int                                 ctr;
    int                                 count = 0;
    globus_reltime_t                    delay;
    globus_reltime_t                    period;

    globus_module_activate(GLOBUS_CALLBACK_MODULE);

    globus_mutex_init(&random_stress_mutex,
                      (globus_mutexattr_t *)GLOBUS_NULL);
    globus_mutex_init(&random_stress_registered_mutex,
                      (globus_mutexattr_t *)GLOBUS_NULL);
    globus_cond_init(&random_stress_cond,
                     (globus_condattr_t *)GLOBUS_NULL);
    globus_mutex_init(&random_stress_own_thread_mutex,
                      (globus_mutexattr_t *)GLOBUS_NULL);
    globus_cond_init(&random_stress_own_thread_cond,
                     (globus_condattr_t *)GLOBUS_NULL);

    GlobusTimeReltimeSet(delay, 0, 0);
    GlobusTimeReltimeSet(period, 0, 20);

    random_stress_success = GLOBUS_TRUE;
    random_stress_done = GLOBUS_FALSE;
    random_stress_wakeme = GLOBUS_FALSE;

    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &delay,
                                      &period,
                                      random_stress_whatever_handler,
                                      GLOBUS_NULL);

    GlobusTimeReltimeSet(period, 0, 0);
    globus_callback_register_periodic(
                                      GLOBUS_NULL,
                                      &delay,
                                      &period,
                                      random_stress_queued_handler,
                                      GLOBUS_NULL);

    for (ctr = 0; ctr < RANDOM_STRESS_TIMES_TO_TEST; ctr++)
    {
        verbose_printf(1, "start of loop\n");

        count = 0;
        globus_mutex_lock(&random_stress_registered_mutex);
        {
            globus_callback_register_periodic(
                                              &stress_callback_handle,
                                              &delay,
                                              &period,
                                              random_stress_count_handler,
                                              (void *)&count);
            random_stress_registered = GLOBUS_TRUE;
        }
        globus_mutex_unlock(&random_stress_registered_mutex);

        verbose_printf(1, "waiting on random_stress_cond\n");
        globus_mutex_lock(&random_stress_mutex);
        {
            while (!random_stress_done)
            {
                globus_cond_wait(&random_stress_cond,
                                 &random_stress_mutex);
            }
        }
        random_stress_done = GLOBUS_FALSE;
        globus_mutex_unlock(&random_stress_mutex);
    }

    globus_module_deactivate(GLOBUS_CALLBACK_MODULE);

    globus_mutex_destroy(&random_stress_mutex);
    globus_mutex_destroy(&random_stress_registered_mutex);
    globus_cond_destroy(&random_stress_cond);
    globus_mutex_destroy(&random_stress_own_thread_mutex);
    globus_cond_destroy(&random_stress_own_thread_cond);

    return random_stress_success;
}

void
random_stress_queued_handler(
                             void *user_arg)
{
    globus_libc_usleep(50);
}

void
random_stress_count_handler(
                            void *user_arg)
{
    int                                *x;

    x = (int *)user_arg;

    (*x)++;

    globus_mutex_lock(&random_stress_registered_mutex);
    {
        if (!random_stress_registered)
        {
            verbose_printf(0, "***ERROR*** random_stress_count_handler() : callback should not be registered\n");
            random_stress_success = GLOBUS_FALSE;
        }
        else if (*x == 50)
        {
            globus_callback_unregister(
             stress_callback_handle, GLOBUS_NULL, GLOBUS_NULL, GLOBUS_NULL);
            random_stress_registered = GLOBUS_FALSE;
        }
    }
    globus_mutex_unlock(&random_stress_registered_mutex);

    if (*x == 50)
    {
        globus_mutex_lock(&random_stress_mutex);
        {
            verbose_printf(1, "signaling random stress cond\n");
            random_stress_done = GLOBUS_TRUE;
            globus_cond_signal(&random_stress_cond);


        }
        globus_mutex_unlock(&random_stress_mutex);
    }
}

void
random_stress_whatever_handler(
                               void *user_arg)
{
    globus_libc_usleep(20);
}
/*
 * Adjust period test
 */
globus_bool_t
adjust_period_test()
{
    globus_reltime_t                    period;
    globus_reltime_t                    delay;
    int                                 count = 0;
    int                                 count2 = 0;

    adjust_test_done = 0;
    adjust_test_success = GLOBUS_TRUE;

    globus_module_activate(GLOBUS_CALLBACK_MODULE);

    globus_mutex_init(&adjust_test_mutex,
                      (globus_mutexattr_t *)GLOBUS_NULL);
    globus_cond_init(&adjust_test_cond,
                     (globus_condattr_t *)GLOBUS_NULL);

    GlobusTimeReltimeSet(period, 0, 50000);
    GlobusTimeReltimeSet(delay, 0, 0);
    globus_callback_register_periodic(
                                      &adjust_callback_handle_1,
                                      &delay,
                                      &period,
                                      adjust_period_requeue_callback,
                                      (void *)&count);

    globus_callback_register_periodic(
                                      &adjust_callback_handle_2,
                                      &delay,
                                      &period,
                                      adjust_period_set_infinity_callback,
                                      (void *)&count2);

    globus_mutex_lock(&adjust_test_mutex);
    {
        while (adjust_test_done < 2)
        {
            globus_cond_wait(&adjust_test_cond,
                             &adjust_test_mutex);
        }
    }
    globus_mutex_unlock(&adjust_test_mutex);

    globus_mutex_destroy(&adjust_test_mutex);
    globus_cond_destroy(&adjust_test_cond);

    globus_module_deactivate(GLOBUS_CALLBACK_MODULE);

    return adjust_test_success;
}

void
adjust_period_requeue_callback(
                               void *user_args)
{
    int                                *count;
    globus_reltime_t                    new_period;

    verbose_printf(3, "adjust_period_requeue_callback() : entering\n");
    if (adjust_test_done < 1)
    {
        return;
    }
    count = (int *)user_args;
    (*count)++;

    verbose_printf(3, "adjust_period_requeue_callback() : count = %d\n", *count);
    if (*count == 10)
    {
/* end test */
        verbose_printf(3, "adjust_period_requeue_callback() : signaling\n");
        globus_mutex_lock(&adjust_test_mutex);
        {
            adjust_test_done++;
            globus_cond_signal(&adjust_test_cond);
        }
        globus_mutex_unlock(&adjust_test_mutex);
    }
    else if (*count == 5)
    {
        verbose_printf(3, "adjust_period_requeue_callback() : WILL_BLOCK\n");
        GlobusTimeReltimeSet(new_period, 0, 50000);
        globus_thread_blocking_will_block();
        if (globus_callback_adjust_period(adjust_callback_handle_1,
                                          &new_period) != GLOBUS_SUCCESS)
        {
            verbose_printf(0, "**ERROR** adjust_period_requeue_callback() : couldnt adjust in a restarted thread\n");
            adjust_test_success = GLOBUS_FALSE;
        }
    }
    else
    {
/*
 * TODO:  check to see if period is actually getting changed to legit value
 */
        GlobusTimeReltimeSet(new_period, 0, 40000);
        if (globus_callback_adjust_period(adjust_callback_handle_1,
                                          &new_period) != GLOBUS_SUCCESS)
        {
            verbose_printf(0, "**ERROR** adjust_period_requeue_callback() : adjustment not made\n");
            adjust_test_success = GLOBUS_FALSE;

        }
    }
    verbose_printf(3, "adjust_period_requeue_callback() : exiting\n");
}

void
adjust_period_set_infinity_callback(
                                    void *user_args)
{
    int                                *count;
    globus_reltime_t                    new_period;


    verbose_printf(3, "adjust_period_set_infinity_callback() : entering\n");
    count = (int *)user_args;

    (*count)++;
    GlobusTimeReltimeCopy(new_period, globus_i_reltime_infinity);
    if (globus_callback_adjust_period(adjust_callback_handle_2, &new_period) != GLOBUS_SUCCESS)
    {
        verbose_printf(0, "**ERROR** adjust_period_set_infinity_callback() : adjust failed\n");
        adjust_test_success = GLOBUS_FALSE;
        adjust_test_done++;
        return;
    }
    if (*count > 1)
    {
        verbose_printf(0, "**ERROR** adjust_period_set_infinity_callback() : period set to infinity and called more than once\n");
        adjust_test_success = GLOBUS_FALSE;
    }
    else
    {
        verbose_printf(3, "adjust_period_set_infinity_callback() : signaling\n");
        globus_mutex_lock(&adjust_test_mutex);
        {
            adjust_test_done++;
            globus_cond_signal(&adjust_test_cond);
        }
        globus_mutex_unlock(&adjust_test_mutex);
    }

    verbose_printf(3, "adjust_period_set_infinity_callback() : exiting\n");
}
/* END ADJUST PERIOD test */
