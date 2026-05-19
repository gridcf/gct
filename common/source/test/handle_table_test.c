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
 * @file handle_table_test.c
 * @brief Test the globus_handle_table_t data type
 * @author Michael Lebman
 */

#include "globus_common.h"
#include "globus_test_tap.h"

static int                          destructor_count = 0;
static
void
destructor(void *datum)
{

    printf("    destructor() called; count is %d\n", destructor_count++);
}

int
main(void)
{
    int                                *data,
                                        i;
    globus_handle_table_t               handle_table;
    int                                 object1,
                                        object2,
                                        object3;
    int                                 object1_count = 1;
    int                                 object2_count = 1;
    int                                 object3_count = 3;
    globus_handle_t                     object1_handle,
                                        object2_handle,
                                        object3_handle;

    printf("1..26\n");
    globus_module_activate(GLOBUS_COMMON_MODULE);

    /* create a handle table */
    printf("    Creating handle table...\n");
    ok(globus_handle_table_init(&handle_table, destructor) == 0,
       "globus_handle_table_init");

    /* add some objects to reference */
    printf("    Adding elements...\n");
    ok((object1_handle = globus_handle_table_insert(
                           &handle_table, &object1, object1_count)) != GLOBUS_NULL_HANDLE,
       "object_1_insert");
    ok((object2_handle = globus_handle_table_insert(
                           &handle_table, &object2, object2_count)) != GLOBUS_NULL_HANDLE,
       "object_2_insert");
    ok((object3_handle = globus_handle_table_insert(
                           &handle_table, &object3, object3_count)) != GLOBUS_NULL_HANDLE,
       "object_3_insert");

    /* increment reference counts */
    printf("    Incrementing references...\n");
    ok(globus_handle_table_increment_reference_by(
              &handle_table, object3_handle, 5), "increment_object_3_by_5");
    object3_count += 5;
    ok(globus_handle_table_increment_reference(&handle_table, object2_handle),
       "increment_object_2");
    object2_count++;

    /* look up some objects */
    ok((data = globus_handle_table_lookup(
                                    &handle_table, object3_handle)) != NULL,
       "lookup_object_3");
    ok(data == &object3, "match_object_3");
    ok((data = globus_handle_table_lookup(
            &handle_table, object1_handle)) != NULL,
        "lookup_object_1");
    ok(data == &object1, "match_object_1");
    ok((data = globus_handle_table_lookup(
        &handle_table, object2_handle)) != NULL,
        "lookup_object_2");
    ok(data == &object2, "match_object_2");

    /* remove some objects and reduce reference counts */
    printf("    Removing references...\n");
    ok(globus_handle_table_decrement_reference(
            &handle_table,
            object1_handle) == GLOBUS_FALSE,
        "decrement_reference_object_1");
    object1_count--;
    for (i = 0; i < object3_count; i++)
    {
        ok(globus_handle_table_decrement_reference(
                &handle_table, object3_handle) == GLOBUS_TRUE ||
                (i == object3_count-1), "decrement_object_3_%d", i);
    }
    object3_count = 0;
    globus_handle_table_decrement_reference(&handle_table, object2_handle);
    object2_count--;

    /* perform additional lookups */
    printf("    Looking up references...\n");
    ok((data = globus_handle_table_lookup(&handle_table, object3_handle)) == NULL,
        "lookup_dereference_object_3");
    ok((data = globus_handle_table_lookup(&handle_table, object1_handle)) == NULL,
        "lookup_dereference_object_1");
    ok((data = globus_handle_table_lookup(&handle_table, object2_handle)) != NULL,
        "lookup_dererenced_object_2");
    ok(destructor_count == 2, "destructor_count");

    /* display the final counts */
    printf("    Final counts--\n");
    printf("    object1_count: %d\n", object1_count);
    printf("    object2_count: %d\n", object2_count);
    printf("    object3_count: %d\n", object3_count);

    /* destroy the table */
    printf("    Destroying the table...\n");
    globus_handle_table_destroy(&handle_table);
    ok(destructor_count == 3, "destructor_count_after_destroy");

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return TEST_EXIT_CODE;
}
