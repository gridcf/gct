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
 * @file fifo_test.c
 * @brief Test the globus_fifo_t implementation
 * @author Michael Lebman
 */

#include "globus_common.h"
#include "globus_test_tap.h"

/**
 * @test
 * Test to exercise globus_fifo_t implementation
 * - Create a FIFO with globus_fifo_init()
 * - Check that it's empty globus_fifo_empty()
 * - Add items with globus_fifo_enqueue() and check values with
 *   globus_fifo_peek() and globus_fifo_tail_peek()
 * - Remove items with globus_fifo_remove() and globus_fifo_dequeue()
 * - Copy fifo with globus_fifo_copy() and compare contents
 * - Move fifo with globus_fifo_move()
 * - Destroy fifo with globus_fifo_destroy()
 */
int fifo_test(void)
{
    int                                    *data,
                                            i;
    int                                     currentFifoSize;
    globus_fifo_t                           currentFifo;
    globus_fifo_t                          *newFifoPtr;
    globus_fifo_t                           relocatedFifo;
    int                                     numOfItems;
    int                                    *middleItem = NULL;
    int                                     middleIndex;
    int                                    *copyData;

    printf("1..42\n");
    numOfItems = 8;

    globus_module_activate(GLOBUS_COMMON_MODULE);

    printf(" Creating FIFO...\n");

    /* create a FIFO */
    ok(globus_fifo_init(&currentFifo) == 0, "fifo_init");

    printf(" Verifying FIFO is empty...\n");
    ok(globus_fifo_empty(&currentFifo), "fifo_empty");

    printf(" Adding data...\n");

    middleIndex = numOfItems / 2;
    /* add a bunch of data */
    for (i = 0; i < numOfItems; i++)
    {
        data = malloc(sizeof(int));
        assert(data != NULL);
        *data = i;

        ok(globus_fifo_enqueue(&currentFifo, data) == 0, "globus_fifo_enqueue");

        /* store the middle item for use later */
        if (i == middleIndex)
            middleItem = data;
    }
    currentFifoSize = i;

    printf(" Verifying data...\n");

    /* check the size */
    ok (globus_fifo_size(&currentFifo) == currentFifoSize, "globus_fifo_size");

    /* check the first item */
    ok((data = (int *)globus_fifo_peek(&currentFifo)) != NULL,
        "peek");
    assert(data);
    ok(*data == 0, "first_item_check");

    /* check the last item */
    ok((data = globus_fifo_tail_peek(&currentFifo)) != NULL,
        "tail_peek");
    assert(data);
    ok(*data == currentFifoSize-1, "last_item_check");

    printf(" Manipulating the FIFO...\n");

    /* remove an item in the middle */
    ok((data = (int *)globus_fifo_remove(&currentFifo, middleItem)) != NULL,
        "remove_middle_item");
    ok(data && middleItem && (*data == *middleItem), "middle_value_check");

    /* remove an item at the beginning */
    ok((data = globus_fifo_dequeue(&currentFifo)) != NULL, "fifo_dequeue");
    assert(data);
    ok(*data == 0, "first_data_dequeue_check");

    /* remove an item at the end */
    ok((data = (int *)globus_fifo_tail_peek(&currentFifo)) != NULL,
      "tail_peek");
    ok(*data == currentFifoSize - 1, "tail_value_check");
    ok(( data = (int *)globus_fifo_remove(&currentFifo, data)) != NULL,
        "tail_remove");
    ok(*data == currentFifoSize - 1, "tail_value_remove_check");

    printf(" Verifying altered size...\n");

    /* check the size- it should be the original size - 3 */
    currentFifoSize -= 3;
    ok(globus_fifo_size(&currentFifo) == currentFifoSize, "file_size_check");

    printf(" Creating a copy...\n");

    /* copy the FIFO to another FIFO */
    ok((newFifoPtr = globus_fifo_copy(&currentFifo)) != NULL,
        "fifo_copy");

    /* check the size on the new FIFO */
    ok(globus_fifo_size(newFifoPtr) == currentFifoSize, "fifo_copy_size_check");

    printf(" Verifying contents of copy...\n");

    /* check whether both FIFOs contain the same set of items */
    while (!globus_fifo_empty(&currentFifo))
    {
        
        ok((data = globus_fifo_dequeue(&currentFifo)) != NULL, "dequeue_first");
        ok((copyData = globus_fifo_dequeue(newFifoPtr)) != NULL,
            "dequeue_copy_first");
        ok(*data == *copyData, "compare_first");

        /* free the data in preparation of destroying the FIFO's */
        /*
         * NOTE: Do not destroy the copy data; the copy consists of pointers
         * to the same data, so the copy is freed when the original data is
         * freed
         */
        free(data);
    }

    printf(" Relocating contents of original FIFO...\n");

    /* move the original FIFO to another FIFO */
    ok(globus_fifo_move(&relocatedFifo, &currentFifo) == 0, "fifo_move");

    /* destroy both FIFO's */
    globus_fifo_destroy(&currentFifo);
    globus_fifo_destroy(newFifoPtr);

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    return TEST_EXIT_CODE;
}

int 
main(int argc, char *argv[])
{
    return fifo_test();
}
