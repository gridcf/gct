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
 * @file memory_test.c
 * @brief Test case for globus_memory_t
 */

#include "globus_common.h"
#include "globus_thread_common.h"

static globus_memory_t                   mem;

#define MEM_INIT_SIZE      15
#ifndef TARGET_ARCH_WIN32
#define POPS               100000
#else
#define POPS                 1000
#endif

const char * reference_output = 
    "abc.....abc.....abc.....abc.....abc....." 
    "abc.....abc.....abc.....abc.....abc....." 
    "abc.....abc.....abc.....abc.....abc.....";
typedef struct mem_test_s
{
    char a;
    char b;
    char c;
    char x[4];
    
} mem_test_t;

int mem_init(mem_test_t * m, int cnt);

int dump(globus_byte_t * buf, int size);

int globus_memory_test(void)
{
   int                         rc = GLOBUS_SUCCESS;
   mem_test_t *                mem_ptr[POPS];
   int                         cnt = 0;

   printf("1..1\n");

   /**
    * @test
    * Create a globus_memory_t with globus_memory_init(), pop a whole
    * lot of memory off of it, dump the initial set of memory, and then
    * push the nodes back.
    */
   globus_module_activate(GLOBUS_COMMON_MODULE);

   globus_memory_init(&mem,
		      sizeof(mem_test_t),
		      MEM_INIT_SIZE);

   for(cnt = 0; cnt < POPS; cnt++)
   {
       mem_ptr[cnt] = ( mem_test_t *) globus_memory_pop_node(&mem); 
       mem_init(mem_ptr[cnt], cnt);
   }

   /* nodes are aligned on mod 8 boundaries, add 1 to 7 to make 8 */
   rc = dump((globus_byte_t *) mem_ptr[0], MEM_INIT_SIZE * (sizeof(mem_test_t) + 1));
   printf("%s\n", (rc==0)?"ok":"not ok");
   
   globus_memory_push_node(&mem, (globus_byte_t *)mem_ptr[0]);
   
   for(cnt = 1; cnt < POPS; cnt++)
   {
       globus_memory_push_node(&mem, (globus_byte_t *) mem_ptr[cnt]);
   }

   globus_memory_destroy(&mem);

   globus_module_deactivate(GLOBUS_COMMON_MODULE);

   return rc;
}
int main(int argc, char * argv[])
{
    return globus_memory_test();
}

int
mem_init(mem_test_t * m, int cnt)
{
    memset(m, 0, sizeof(mem_test_t) + 1);
    m->a = 'a';
    m->b = 'b';
    m->c = 'c';
 
    return GLOBUS_TRUE;
}

int dump(globus_byte_t * buf, int size)
{
    int  ctr;
    int  col = 0;
    int failed=0;
    
    for(ctr = 0; ctr < size; ctr++)
    {
        if (reference_output[ctr] != (isprint(buf[ctr]) ? buf[ctr] : '.'))
        {
            failed++;
        }
	col++;
        if(col >= 40)
	{
	    col = 0;
	}
    }
    return failed;
}
