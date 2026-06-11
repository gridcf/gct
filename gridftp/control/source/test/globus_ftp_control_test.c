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

#include "globus_ftp_control_test.h"
#include "globus_common.h"
#include <string.h>

globus_bool_t
connect_wrapper(
    globus_ftp_control_handle_t *               control_handle);

globus_bool_t
disconnect_wrapper(
    globus_ftp_control_handle_t *               control_handle);

globus_bool_t
simple_control_test_wrapper(
    globus_ftp_control_handle_t *               control_handle);

globus_bool_t
simple_data_test_wrapper(
    globus_ftp_control_handle_t *               control_handle);

globus_bool_t
eb_simple_data_test_wrapper(
    globus_ftp_control_handle_t *               control_handle);

globus_bool_t
outstanding_io_test_wrapper(
    globus_ftp_control_handle_t *               control_handle);

globus_bool_t
simple_dir_test_wrapper(
    globus_ftp_control_handle_t *               control_handle);

globus_bool_t
abort_test_wrapper(
    globus_ftp_control_handle_t *               control_handle);

/*****************************************************************
 *  test function desriptors
 *
 *  when adding a test add its function descriptor and
 *  a string describing it to the "test_array" table, and
 *  increase the TEST_COUNT by one.
 ****************************************************************/
typedef
globus_bool_t
    (*globus_ftp_control_test_func_t)(
        globus_ftp_control_handle_t *           control_handle);

typedef struct test_entry_s
{
    globus_ftp_control_test_func_t              test_func;
    char *                                      name;
} test_entry_t;

test_entry_t                                    test_array[] =
{
    {connect_wrapper, "connect handle test"},
    {disconnect_wrapper, "disconnect handle test"},
    {simple_dir_test_wrapper, "simple directory test"},
    {simple_data_test_wrapper, "simple data test"},
    {abort_test_wrapper, "abort test"},
    {outstanding_io_test_wrapper, "outstanding io data test"},
    {simple_control_test_wrapper, "simple control test"},
    {async_control_test, "asynchronous control test"},
    {GLOBUS_NULL, GLOBUS_NULL}
};

#define TEST_COUNT                              1

test_entry_t                                    eb_test_array[] =
{
    {eb_simple_data_test_wrapper, "eb simple data test"},
    {GLOBUS_NULL, GLOBUS_NULL}
};

#define EB_TEST_COUNT                           1

login_t                                         login_info = {
    ":globus-mapping:",
    "",
    "",
    "",
    0
};
extern int verbose_print_level;
int
main(
    int                                         argc,
    char *                                      argv[])
{
    int                                         ctr;
    globus_ftp_control_handle_t                 control_handle;
    globus_result_t                             result;
    int                                         tests_run = 0;
    int                                         tests_passed = 0;
    int                                         tests_failed = 0;
    globus_bool_t                               rc;
    globus_ftp_control_handle_t                 handles[TEST_COUNT];

    verbose_print_level = 1;

    /*
     *  get the verbose print level
     */
    for(ctr = 0; ctr < argc; ctr++)
    {
        if(strcmp(argv[ctr], "--verbose") == 0)
        {
            if(ctr + 1 >= argc)
            {
                verbose_print_level = 1;
            }
            else
            {
                verbose_print_level = atoi(argv[ctr+1]);
                ctr++;
            }
        }
        else if(strcmp(argv[ctr], "--host") == 0)
        {
            if(argc < ctr+2)
            {
                help_print();
                exit(1);
            }
            strcpy(login_info.hostname, argv[ctr+1]);
            login_info.port = atoi(argv[ctr+2]);
        }
        else if(strcmp(argv[ctr], "--help") == 0)
        {
            help_print();
        }
        else if(strcmp(argv[ctr], "--login") == 0)
        {
            if(argc < ctr+3)
            {
                help_print();
                exit(1);
            }
            strcpy(login_info.login, argv[ctr+1]);
            strcpy(login_info.password, argv[ctr+2]);
            strcpy(login_info.dir, argv[ctr+3]);
            ctr += 3;
        }
    }
    if (login_info.dir[0] == 0)
    {
        getcwd(login_info.dir, sizeof(login_info.dir));
    }

    /*
     *  first test
     */
    globus_module_activate(GLOBUS_FTP_CONTROL_MODULE);
    tests_run++;
    result = globus_ftp_control_handle_init(&handles[0]);
    connect_control_handle(
        &handles[0],
        login_info.login,
        login_info.password,
        login_info.dir,
        login_info.hostname,
        login_info.port);
    globus_module_deactivate(GLOBUS_FTP_CONTROL_MODULE);
    tests_passed++;
    verbose_printf(1, "test #%d) activate/connect/deactivate passed\n", tests_run);

    /*
     *  second test
     */
    globus_module_activate(GLOBUS_FTP_CONTROL_MODULE);
    tests_run++;
    for(ctr = 0; ctr < TEST_COUNT; ctr++)
    {
        result = globus_ftp_control_handle_init(&handles[ctr]);
        if(result != GLOBUS_SUCCESS)
        {
            tests_failed++;
            verbose_printf(1,
                        "activate/deactivate test # %d failed\n",
                       tests_run);
            exit(1);
        }
        if(ctr % 2)
        {
            result = globus_ftp_control_handle_destroy(&handles[ctr]);
            if(result != GLOBUS_SUCCESS)
            {
                tests_failed++;
                verbose_printf(1,
                        "activate/deactivate test # %d failed\n",
                       tests_run);
                exit(1);
            }
        }
    }
    rc = globus_module_deactivate(GLOBUS_FTP_CONTROL_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
        tests_failed++;
        verbose_printf(1,
                       "activate/deactivate test # %d failed\n",
                       tests_run);
        exit(1);
    }
    tests_passed++;
    verbose_printf(1, "test #%d) activate/deactivate passed\n", tests_run);


    /*
     *  initailize variables for the test
     */
    rc = globus_module_activate(GLOBUS_COMMON_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
        globus_libc_printf("unable to activate common module\n");
        exit(1);
    }
    /*
     *  run stream mode tests
     */
    rc = globus_module_activate(GLOBUS_FTP_CONTROL_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
        globus_libc_printf("unable to activate gsiftp module\n");
        exit(1);
    }

    globus_ftp_control_handle_init(&control_handle);
    for(ctr = 0; ctr < EB_TEST_COUNT; ctr++)
    {
        tests_run++;
        if(eb_test_array[ctr].test_func(&control_handle))
        {
            tests_passed++;
            verbose_printf(1, "test #%d) \"%s\" passed.\n",
                       tests_run, eb_test_array[ctr].name);
        }
        else
        {
            tests_failed++;
            printf("Failed\n");
            verbose_printf(1, "test #%d) \"%s\" failed.\n",
                       tests_run, eb_test_array[ctr].name);
            exit(1);
        }
    }
    globus_ftp_control_handle_destroy(&control_handle);

    globus_ftp_control_handle_init(&control_handle);
    for(ctr = 0; ctr < TEST_COUNT; ctr++)
    {
        tests_run++;
        if(test_array[ctr].test_func(&control_handle))
        {
            tests_passed++;
            verbose_printf(1, "test #%d) \"%s\" passed.\n",
                           tests_run, test_array[ctr].name);
        }
        else
        {
            tests_failed++;
            printf("Failed\n");
            verbose_printf(1, "test #%d) \"%s\" failed.\n",
                           tests_run, test_array[ctr].name);
            exit(1);
        }
    }
    globus_ftp_control_handle_destroy(&control_handle);

    rc = globus_module_deactivate(GLOBUS_FTP_CONTROL_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
        globus_libc_printf("unable to activate gsiftp module\n");
        exit(1);
    }

    verbose_printf(1, "%d tests run.  %d passed.  %d failed\n",
                   tests_run, tests_passed, tests_failed);
    verbose_printf(0, "Success\n");

    return 0;
}

globus_bool_t
simple_control_test_wrapper(
    globus_ftp_control_handle_t *               control_handle)
{
    globus_bool_t                               rc;

    rc = connect_control_handle(control_handle,
             login_info.login,
             login_info.password,
             login_info.dir,
             login_info.hostname,
             login_info.port);
    if(rc)
    {
        rc = simple_control_test(control_handle);
    }
    if(rc)
    {
        rc = disconnect_control_handle(control_handle);
    }

    return rc;
}

globus_bool_t
connect_wrapper(
    globus_ftp_control_handle_t *               control_handle)
{
    return connect_control_handle(
            control_handle,
            login_info.login,
            login_info.password,
            login_info.dir,
            login_info.hostname,
            login_info.port);
}

globus_bool_t
disconnect_wrapper(
    globus_ftp_control_handle_t *               control_handle)
{
    globus_bool_t                               rc;

    rc = disconnect_control_handle(control_handle);

    return rc;
}

globus_bool_t
simple_data_test_wrapper(
    globus_ftp_control_handle_t *               control_handle)
{
    globus_bool_t                               rc;

    rc = connect_control_handle(control_handle,
               login_info.login,
               login_info.password,
               login_info.dir,
             login_info.hostname,
             login_info.port);
    if(rc)
    {
        rc = simple_data_test(control_handle);
    }
    if(rc)
    {
        rc = disconnect_control_handle(control_handle);
    }

    return rc;
}

globus_bool_t
eb_simple_data_test_wrapper(
    globus_ftp_control_handle_t *               control_handle)
{
    globus_bool_t                               rc;

    rc = connect_control_handle(control_handle,
               login_info.login,
               login_info.password,
               login_info.dir,
               login_info.hostname,
               login_info.port);
    if(rc)
    {
        rc = eb_data_test(control_handle);
    }
    if(rc)
    {
        rc = disconnect_control_handle(control_handle);
    }

    return rc;
}

globus_bool_t
outstanding_io_test_wrapper(
    globus_ftp_control_handle_t *               control_handle)
{
    globus_bool_t                               rc;

    rc = connect_control_handle(control_handle,
               login_info.login,
               login_info.password,
               login_info.dir,
             login_info.hostname,
             login_info.port);
    if(rc)
    {
        rc = outstanding_io_test(control_handle);
    }
    if(rc)
    {
        rc = disconnect_control_handle(control_handle);
    }

    return rc;
}

globus_bool_t
simple_dir_test_wrapper(
    globus_ftp_control_handle_t *               control_handle)
{
    globus_bool_t                               rc;

    rc = connect_control_handle(control_handle,
               login_info.login,
               login_info.password,
               login_info.dir,
             login_info.hostname,
             login_info.port);
    if(rc)
    {
        rc = simple_dir_test(control_handle);
    }
    if(rc)
    {
        rc = disconnect_control_handle(control_handle);
    }

    return rc;
}

globus_bool_t
abort_test_wrapper(
    globus_ftp_control_handle_t *               control_handle)
{
    globus_bool_t                               rc;

    rc = connect_control_handle(control_handle,
               login_info.login,
               login_info.password,
               login_info.dir,
             login_info.hostname,
             login_info.port);
    if(rc)
    {
        rc = abort_test(control_handle);
    }
    if(rc)
    {
        rc = disconnect_control_handle(control_handle);
    }

    return rc;
}
