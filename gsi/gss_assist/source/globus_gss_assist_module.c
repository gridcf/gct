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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

/**
 * @file globus_gss_assist_module.c
 * @brief GSS Assist Module Descriptor
 * @author Sam Lang, Sam Meder
 */

#include "globus_i_gss_assist.h"
#include "globus_gsi_system_config.h"
#include "globus_callout.h"
#include "version.h"
#include <stdlib.h>

static int globus_l_gsi_gss_assist_activate(void);
static int globus_l_gsi_gss_assist_deactivate(void);

int                               globus_i_gsi_gss_assist_debug_level = 0;
FILE *                            globus_i_gsi_gss_assist_debug_fstream = NULL;

globus_mutex_t                    globus_i_gsi_gss_assist_mutex;

/**
 * Module descriptor static initializer.
 */
globus_module_descriptor_t globus_i_gsi_gss_assist_module =
{
    "globus_gss_assist",
    globus_l_gsi_gss_assist_activate,
    globus_l_gsi_gss_assist_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

/**
 * Module activation
 */
static
int
globus_l_gsi_gss_assist_activate(void)
{
    char *                              tmp_string;
    static char *                       _function_name_ =
        "globus_l_gsi_gss_assist_activate";

    tmp_string = getenv("GLOBUS_GSI_GSS_ASSIST_DEBUG_LEVEL");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_gsi_gss_assist_debug_level = atoi(tmp_string);
        if(globus_i_gsi_gss_assist_debug_level < 0)
        {
            globus_i_gsi_gss_assist_debug_level = 0;
        }
    }

    tmp_string = getenv("GLOBUS_GSI_GSS_ASSIST_DEBUG_FILE");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_gsi_gss_assist_debug_fstream = fopen(tmp_string, "w");
        if(globus_i_gsi_gss_assist_debug_fstream == NULL)
        {
            goto exit;
        }
    }
    else
    {
        globus_i_gsi_gss_assist_debug_fstream = stderr;
    }

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_ENTER;

    globus_module_activate(GLOBUS_COMMON_MODULE);
    globus_module_activate(GLOBUS_CALLOUT_MODULE);
    globus_module_activate(GLOBUS_GSI_SYSCONFIG_MODULE);
    globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE);

    globus_mutex_init(&globus_i_gsi_gss_assist_mutex, NULL);

 exit:
    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_EXIT;    
    return GLOBUS_SUCCESS;
}

/**
 * Module deactivation
 */
static
int
globus_l_gsi_gss_assist_deactivate(void)
{
    static char *                       _function_name_ =
        "globus_l_gsi_gss_assist_deactivate";
    
    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_ENTER;
    
    globus_mutex_destroy(&globus_i_gsi_gss_assist_mutex);

    globus_module_deactivate(GLOBUS_GSI_GSSAPI_MODULE);
    globus_module_deactivate(GLOBUS_GSI_SYSCONFIG_MODULE);
    globus_module_deactivate(GLOBUS_CALLOUT_MODULE);
    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_EXIT;
    return GLOBUS_SUCCESS;
}
/* globus_l_gsi_gss_assist_deactivate() */

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
