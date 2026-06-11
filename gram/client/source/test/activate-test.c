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

#include "globus_gram_client.h"
#include "globus_preload.h"

int main(int argc, char *argv[])
{
    int rc;

    LTDL_SET_PRELOADED_SYMBOLS();

    printf("1..2\n");
    rc = !(!globus_module_activate(GLOBUS_GRAM_CLIENT_MODULE));
    printf("%s 1 - activate\n", rc==0?"ok":"not ok");

    rc = !(!globus_module_deactivate(GLOBUS_GRAM_CLIENT_MODULE));
    printf("%s 2 - deactivate\n", rc==0?"ok":"not ok");

    return 0;
}
