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
 * @file module_test.c
 * @brief Module Tests
 */

#include "globus_common.h"
#include "globus_test_tap.h"

#include <stdio.h>
#include <string.h>

extern void
globus_i_module_dump(
    FILE *                              out_f);

static int active_modules[3] = {0};

globus_bool_t
module1_activate(void);

globus_bool_t
module1_deactivate(void);

void
module1_atexit(void);

void *
module1_get_pointer_func(void);

globus_bool_t
module2_activate(void);

globus_bool_t
module2_deactivate(void);

void
module2_atexit(void);

void *
module2_get_pointer_func(void);

globus_bool_t
module3_activate(void);

globus_bool_t
module3_deactivate(void);

void
module3_atexit(void);

void *
module3_get_pointer_func(void);

static globus_module_descriptor_t module1 =
{
    "Module1",
    module1_activate,
    module1_deactivate,
    module1_atexit,
    module1_get_pointer_func
};

static globus_module_descriptor_t module2 =
{
    "Module2",
    module2_activate,
    module2_deactivate,
    module2_atexit,
    module2_get_pointer_func
};

static globus_module_descriptor_t module3 =
{
    "Module3",
    module3_activate,
    module3_deactivate,
    module3_atexit,
    module3_get_pointer_func
};


/** @brief Module test cases */
int
module_test(void)
{
    int                                 rc;
    void *                              mod_pointer;
    const char                          *name="GLOBUS_ENV_TEST_VAR";
    char *                              value1 = "value1";
    

    printf("1..16\n");

    /**
     * @test
     * Use globus_module_setenv() to set a module_environment
     * variable. Then, use globus_module_getenv() to check its value
     */
    globus_module_setenv("GLOBUS_ENV_TEST_VAR", value1);
    ok(strcmp(globus_module_getenv(name), value1) == 0, "globus_module_getenv");
    
    /**
     * @test
     * Deactivate a non-activated module with globus_module_deactivate()
     */
    rc = globus_module_deactivate(GLOBUS_COMMON_MODULE);
    ok(rc != GLOBUS_SUCCESS, "deactivate_non_active_module");
    
    /**
     * @test
     * Activate GLOBUS_COMMON_MODULE with globus_module_activate()
     */
    rc = globus_module_activate(GLOBUS_COMMON_MODULE);
    ok(rc == GLOBUS_SUCCESS, "activate_globus_common");

    /**
     * @test
     * Get a module pointer with globus_module_get_module_pointer()
     */
    mod_pointer = globus_module_get_module_pointer(&module1);
    ok(mod_pointer == (void *) 0x1, "globus_module_get_module_pointer");
   
    /**
     * @test
     * Activate a module with globus_module_activate()
     */
    rc = globus_module_activate(&module1);
    ok(rc == GLOBUS_SUCCESS &&
       active_modules[0] == 1 &&
       active_modules[1] == 1 &&
       active_modules[2] == 1, "activate_module1");

    /**
     * @test
     * Activate an already active module with globus_module_activate()
     */
    rc = globus_module_activate(&module2);
    ok(rc == GLOBUS_SUCCESS &&
       active_modules[0] == 1 &&
       active_modules[1] == 1 &&
       active_modules[2] == 1, "activate_module2");
   
    /**
     * @test
     * Activate an already active module with globus_module_activate()
     */
    rc = globus_module_activate(&module3);
    ok(rc == GLOBUS_SUCCESS &&
       active_modules[0] == 1 &&
       active_modules[1] == 1 &&
       active_modules[2] == 1, "activate_module3");
   
    /**
     * @test
     * Deactivate an already active module with globus_module_deactivate().
     * It should remain active.
     */
    rc = globus_module_deactivate(&module3);
    ok(rc == GLOBUS_SUCCESS &&
       active_modules[0] == 1 &&
       active_modules[1] == 1 &&
       active_modules[2] == 1, "deactivate_module3");
   
    /**
     * @test
     * Activate an already active module with globus_module_activate()
     */
    rc = globus_module_activate(&module3);
    ok(rc == GLOBUS_SUCCESS &&
       active_modules[0] == 1 &&
       active_modules[1] == 1 &&
       active_modules[2] == 1, "activate_module3_again");
   
    /**
     * @test
     * Deactivate an already active module with globus_module_deactivate().
     * All modules should remain active except module1.
     */
    rc = globus_module_deactivate(&module1);
    ok(rc == GLOBUS_SUCCESS &&
       active_modules[0] == 0 &&
       active_modules[1] == 1 &&
       active_modules[2] == 1, "deactivate_module1");
   
    /**
     * @test
     * Deactivate an already deactivated module with globus_module_deactivate().
     * This should not change activate state.
     */
    rc = globus_module_deactivate(&module1);
    ok(rc != GLOBUS_SUCCESS &&
       active_modules[0] == 0 &&
       active_modules[1] == 1 &&
       active_modules[2] == 1, "deactivate_module1_too_many_times");
   
    /**
     * @test
     * Deactivate an active module with globus_module_deactivate()
     */
    rc = globus_module_deactivate(&module2);
    ok(rc == GLOBUS_SUCCESS &&
       active_modules[0] == 0 &&
       active_modules[1] == 0 &&
       active_modules[2] == 1, "deactivate_module2");
   
    /**
     * @test
     * Deactivate an active module with globus_module_deactivate()
     */
    rc = globus_module_deactivate(&module3);
    ok(rc == GLOBUS_SUCCESS &&
       active_modules[0] == 0 &&
       active_modules[1] == 0 &&
       active_modules[2] == 0, "deactivate_module3");
   
    /**
     * @test
     * Reactivate module1 with globus_module_activate()
     */
    rc = globus_module_activate(&module1);
    ok(rc == GLOBUS_SUCCESS &&
       active_modules[0] == 1 &&
       active_modules[1] == 1 &&
       active_modules[2] == 1, "reactivate_module1");
   
    /*
     * @test
     * Deactivate all modules with globus_module_deactivate_all()
     */
    rc = globus_module_deactivate_all();
    ok(rc == GLOBUS_SUCCESS &&
       active_modules[0] == 0 &&
       active_modules[1] == 0 &&
       active_modules[2] == 0, "deactivate_all");

    /*
     * @test
     * Deactivate a non-activated module with globus_module_deactivate()
     */
    rc = globus_module_deactivate(GLOBUS_COMMON_MODULE);
    ok(rc != GLOBUS_SUCCESS, "deactivate_non_active_common");

    return TEST_EXIT_CODE;
}

int
main(
    int                                 argc,
    char *                              argv[])
{
    return module_test();
}


globus_bool_t
module1_activate(void)
{
    active_modules[0]=1;
    
    globus_module_activate(&module2);
    return GLOBUS_SUCCESS;
}

globus_bool_t
module1_deactivate(void)
{
    active_modules[0]=0;

    globus_module_deactivate(&module2);
    return GLOBUS_SUCCESS;
}

void
module1_atexit(void)
{
    fprintf(stdout, "atexit test 1 successful\n");
    fflush(stdout);
}

void *
module1_get_pointer_func(void)
{
    return (void *) 0x1;
}

globus_bool_t
module2_activate(void)
{
    active_modules[1]=1;

    globus_module_activate(&module3);
    return GLOBUS_SUCCESS;
}

globus_bool_t
module2_deactivate(void)
{
    active_modules[1]=0;

    globus_module_deactivate(&module3);
    return GLOBUS_SUCCESS;
}

void
module2_atexit(void)
{
    fprintf(stdout, "atexit test 2 successful\n");
    fflush(stdout);
}

void *
module2_get_pointer_func(void)
{
    return (void *) 0x2;
}

globus_bool_t
module3_activate(void)
{
    active_modules[2]=1;

    return GLOBUS_SUCCESS;
}

globus_bool_t
module3_deactivate(void)
{
    active_modules[2]=0;

    return GLOBUS_SUCCESS;
}

void
module3_atexit(void)
{
    fprintf(stdout, "atexit test 3 successful\n");
    fflush(stdout);
}

void *
module3_get_pointer_func(void)
{
    return (void *) 0x3;
}
