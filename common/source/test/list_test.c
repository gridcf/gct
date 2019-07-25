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
 * @file list_test.c
 * @brief Globus List Test Cases
 */

#include "globus_common.h"
#include "globus_test_tap.h"

int 
matches(void *datum, void *args)
{
    /* both the datum and the args refer to pointers to ints */
    int                                 current_item;
    int                                 target;
    /* validate parameters */
    if (datum == NULL || args == NULL)
        return 0;

    /*
     * we don't really need to store the data, but this implementation helps
     * document the code
     */
    current_item = *(int *)(datum);
    target = *(int *)(args);
    if (current_item == target)
        return 1;

    return 0;
}


/** @brief Globus List Test Cases*/
int
list_test(void)
{
    globus_list_t                      *list = GLOBUS_NULL;
    int                                 rc;
    void                               *ptr;
    int                                 x1 = 1;
    int                                 x2 = 2;
    int                                 x3 = 3;
    int                                 x4 = 4;
    int                                 search_item;
    globus_list_t                      *sub_list;
    int                                 size;
    printf("1..25\n");
    globus_module_activate(GLOBUS_COMMON_MODULE);

    /**
     * @test
     * Call globus_list_size() with an empty list
     */
    size = globus_list_size(list);
    ok(size == 0, "size_of_empty_list");

    /**
     * @test
     * Insert a datum into a globus_list_t with globus_list_insert()
     */
    rc = globus_list_insert(&list, &x1);
    ok(rc == 0, "insert_item_into_empty_list");

    /**
     * @test
     * Add a datum to a globus_list_t with globus_list_cons()
     */
    list = globus_list_cons(&x2, list);
    ok(list != NULL, "globus_list_cons");

    /**
     * @test
     * Verify that globus_list_size() returns 2 after adding two data
     * items to a list.
     */
    size = globus_list_size(list);
    ok(size == 2, "size_of_list_after_insert_and_cons");

    /**
     * @test
     * Insert x3 into a list with globus_list_insert()
     */
    ok(globus_list_insert(&list, &x3) == 0, "insert_x3");

    /**
     * @test
     * Insert x4 into a list with globus_list_insert()
     */
    ok(globus_list_insert(&list, &x4) == 0, "insert_x4");

    /**
     * @test
     * Verify that the size of the list is now 4 with globus_list_size()
     */
    ok(globus_list_size(list) == 4, "check_new_size");

    /**
     * @test
     * Search for the first item in a list with globus_list_search()
     */
    sub_list = globus_list_search(list, &x1);
    ok(sub_list != NULL, "search_first");

    /**
     * @test
     * Use globus_list_first() to return the datum from the returned list node.
     * Verify that the search result matches the value searched for.
     */
    ptr = globus_list_first(sub_list);
    ok(*((int *)ptr) == x1, "search_first_value");

    /**
     * @test
     * Search for the third item in a list with globus_list_search()
     */
    sub_list = globus_list_search(list, &x3);
    ok(sub_list != NULL, "search_third");

    /**
     * @test
     * Use globus_list_first() to return the datum from the returned list node.
     * Verify that the search result matches the value searched for.
     */
    ptr = globus_list_first(sub_list);
    ok(*((int *)ptr) == x3, "search_third_value");

    /**
     * @test
     * Search for the fourth item in a list with globus_list_search()
     */
    sub_list = globus_list_search(list, &x4);
    ok(sub_list != NULL, "search_last");
    /**
     * @test
     * Use globus_list_first() to return the datum from the returned list node.
     * Verify that the search result matches the value searched for.
     */
    ptr = globus_list_first(sub_list);
    ok(*((int *)ptr) == x4, "search_last_value");

    /**
     * @test
     * Search for the second item in a list with globus_list_search()
     */
    sub_list = globus_list_search(list, &x2);
    ok(sub_list != NULL, "search_second");

    /**
     * @test
     * Use globus_list_first() to return the datum from the returned list node.
     * Verify that the search result matches the value searched for.
     */
    ptr = globus_list_first(sub_list);
    ok(*((int *)ptr) == x2, "search_second_value");

    /**
     * @test
     * Search for the fourth item in a list with globus_list_search_pred()
     */
    search_item = 4;
    sub_list = globus_list_search_pred(list, matches, &search_item);
    ok(sub_list != NULL, "search_pred_4");

    /**
     * @test
     * Search for the second item in a list with globus_list_search_pred()
     */
    search_item = 2;
    sub_list = globus_list_search_pred(list, matches, &search_item);
    ok(sub_list != NULL, "search_pred_2");

    /**
     * @test
     * Search for the third item in a list with globus_list_search_pred()
     */
    search_item = 3;
    sub_list = globus_list_search_pred(list, matches, &search_item);
    ok(sub_list != NULL, "search_pred_3");

    /**
     * @test
     * Search for the first item in a list with globus_list_search_pred()
     */
    search_item = 1;
    sub_list = globus_list_search_pred(list, matches, &search_item);
    ok(sub_list != NULL, "search_pred_1");

    /**
     * @test
     * Search for the second item in the list with globus_list_search()
     */
    sub_list = globus_list_search(list, &x2);
    ok(sub_list != NULL, "search_2");

    /**
     * @test
     * Remove the second item in the list with globus_list_remove()
     */
    ok(globus_list_remove(&list, sub_list) == &x2, "remove_2");

    /**
     * @test
     * Remove the first item in the list with globus_list_remove()
     */
    ok(globus_list_remove(&list, list) == &x4, "remove_first");
    /**
     * @test
     * Remove the first item in the list with globus_list_remove()
     */
    ok(globus_list_remove(&list, list) == &x3, "remove_first_again");
    /**
     * @test
     * Remove the first item in the list with globus_list_remove()
     */
    ok(globus_list_remove(&list, list) == &x1, "remove_first_again");

    /**
     * @test
     * Verify that the list is empty with globus_list_empty()
     */
    ok(globus_list_empty(list), "empty_list");

    globus_list_free(list);

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return TEST_EXIT_CODE;
}

int
main(int argc, char *argv[])
{
    return list_test();
}
