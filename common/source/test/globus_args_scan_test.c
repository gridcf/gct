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

#include "globus_common.h"
#include "globus_test_tap.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

const char * oneline_usage
    = "globus_args_test [-foo] [-replicate file N] [-host hn] [-file file]";

const char * long_usage
    =   "\nglobus_args_test [options] x\n" \
        "OPTIONS\n" \
        "\t -foo              : triggers the \"foo mode\"\n" \
        "\t -replicate file N : replicates an existing file N times\n" \
        "\t -host  hostname   : defines a hostname\n" \
        "\t -file  x          : defines a filename\n\n";

#define foo_id   1
#define rep_id   2
#define hn_id    3
#define np_id    4
#define file_id  5

static char *  foo_aliases[]  = { "-foo", NULL };
static char *  np_aliases[]   = { "-np", NULL };
static char *  file_aliases[] = { "-file", "-f", NULL };
static char *  rep_aliases[]  = { "-replicate", "-rep", "-r", NULL };
static char *  hn_aliases[]   = { "-host", "-hn", "-h", NULL };


static int validate_filename_parms
    = O_RDONLY;

static globus_validate_int_parms_t validate_int_parms
    = { GLOBUS_VALIDATE_INT_MINMAX, 0, 64 };

static globus_args_valid_predicate_t np_checks[] 
    = { globus_validate_int };

static void* np_check_parms[] 
    = { (void *) &validate_int_parms };

static globus_args_valid_predicate_t rep_checks[] 
    = { globus_validate_filename, globus_validate_int };

static void* rep_check_parms[]
    = { (void *) &validate_filename_parms, (void *) &validate_int_parms };

static globus_args_valid_predicate_t file_checks[] 
    = { globus_validate_filename };

static void* file_check_parms[]
    = { (void *) &validate_filename_parms };


/* forward decl. */
int
test_hostname( char * value, void * parms, char ** error_msg );

static globus_args_valid_predicate_t hn_checks[]
    = { test_hostname };

static globus_args_option_descriptor_t option_list[]
    = { {foo_id,  foo_aliases,  0, NULL, NULL} ,
        {np_id,   np_aliases,   1, np_checks,   np_check_parms} ,
        {rep_id,  rep_aliases,  2, rep_checks,  rep_check_parms} ,
        {hn_id,   hn_aliases,   1, hn_checks,   NULL} ,
        {file_id, file_aliases, 1, file_checks, file_check_parms} };


int
test_hostname( char * value, void * parms, char ** error_msg )
{
    struct addrinfo *                   ai = NULL;
    int                                 rc;

    rc = getaddrinfo(value, NULL, NULL, &ai);

    if (rc != 0)
    {
        const char * tmp_msg;
        tmp_msg = gai_strerror(rc);
        if (tmp_msg)
        {
            *error_msg = strdup(tmp_msg);
        }
    }
    if (ai)
    {
        freeaddrinfo(ai);
    }

    return rc;
}


int
do_test( char *argv[] )
{
    globus_args_option_instance_t *  option;
    globus_list_t *                  options_found;
    globus_list_t *                  list;
    char *                           error_msg;
    int                              argc;
    int                              n_options;
    int                              err;
    int                              i;
    globus_bool_t                    at_newline;

    for (argc=0; argv[argc]; argc++)
	;

    n_options = sizeof(option_list)/sizeof(globus_args_option_descriptor_t);

    error_msg = NULL;
    printf("    Calling globus_args_scan with: ");
    for (int p = 0; p < argc; p++)
    {
        printf("%s ", argv[p]);
    }
    printf("\n");
    err = globus_args_scan( &argc,
			    &argv,
			    n_options,
			    option_list,
			    "footest",
			    NULL,
			    oneline_usage,
			    long_usage,
			    &options_found,
			    &error_msg    );

    printf("    globus_args_scan returned %d\n", err);
    at_newline = GLOBUS_TRUE;

    if (error_msg)
    {
        for (int p = 0; error_msg[p]; p++)
        {
            if (at_newline)
            {
                printf("    ");
                at_newline = GLOBUS_FALSE;
            }
            if (error_msg[p] != '\n')
            {
                putchar(error_msg[p]);
            }
            else
            {
                printf("\n");
                at_newline = 1;
            }
        }
    }
    else
	printf("    error_msg = null\n");

    if (err >= 0)
    {
        printf("    after args_scan : argc = %d\n", argc);
        for (i=0; i<argc; i++)
            printf("    \targv[%2d] = [%s]\n", i, (argv[i]) ? argv[i] : "NULL" );

	printf("    option list : \n");

	for (list = options_found;
	     !globus_list_empty(list);
	     list = globus_list_rest(list))
	{
	    option = globus_list_first(list);
	    printf("    \tid=%d arity=%d\n", option->id_number, option->arity);
	    for (i=0; i<option->arity; i++)
		printf("    \t\tval[%2d] = [%s]\n", i,
		       (option->values[i]) ? option->values[i] : "NULL" );
	}

	globus_args_option_instance_list_free( &options_found );
    }

    return err;
}


static char * test1[] = { 
    "globus_args_test", "-foo", NULL };    /* ok */

static char * test2[] = {
    "globus_args_test", "-file", "Makefile", NULL };   /* ok */

static char * test3[] = {
    "globus_args_test", "-rep", "Makefile", "3", NULL };   /* ok */

static char * test4[] = {
    "globus_args_test", "-np", "12", NULL };   /* ok */

static char * testhexok[] = {
    "globus_args_test", "-np", "0x2F", NULL };  /* ok */

static char * testhexmax[] = {
    "globus_args_test", "-np", "0xAB", NULL };  /* fail */

static char * testoctok[] = {
    "globus_args_test", "-np", "077", NULL };  /* ok */

static char * testoctmax[] = {
    "globus_args_test", "-np", "0101", NULL };  /* fail */

static char * test7[] = {
    "globus_args_test", "-np", "-1", NULL };  /* fail */

static char * test8[] = {
    "globus_args_test", "-np", "65", NULL };  /* fail */

static char * test9[] = {
    "globus_args_test", "-file", "does/not/exist", NULL };  /* fail */

static char * testhnok[] = {
    "globus_args_test", "-hn", "www.globus.org", NULL }; /*ok*/

static char * testhnfail[] = {
    "globus_args_test", "-hn", "smutt.mqs.anl.gov", NULL }; /*fail*/


int
main(int argc, char *argv[])
{
    globus_module_activate(GLOBUS_COMMON_MODULE);

    printf("1..13\n");
    ok(do_test(test1) == 1, "test1");
    ok(do_test(test2) == 1, "test2");
    ok(do_test(test3) == 1, "test3");
    ok(do_test(test4) == 1, "test4");
    ok(do_test(testhexok) == 1, "testhexok");
    ok(do_test(testhexmax) < 0, "testhexmax");
    ok(do_test(testoctok) == 1, "testoctok");
    ok(do_test(testoctmax) < 0, "testoctmax");
    ok(do_test(test7) < 0, "test7");
    ok(do_test(test8) < 0, "test8");
    ok(do_test(test9) < 0, "test9");

    skip(getenv("NO_EXTERNAL_NET") != NULL,
        ok(do_test(testhnok) == 1, "testhnok"));
    skip(getenv("NO_EXTERNAL_NET") != NULL,
        ok(do_test(testhnfail) < 0, "testhnfail"));

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return TEST_EXIT_CODE;
}
