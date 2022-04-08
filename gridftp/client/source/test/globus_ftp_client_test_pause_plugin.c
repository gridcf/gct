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

#include "globus_ftp_client.h"
#include "globus_ftp_client_plugin.h"

#include "globus_ftp_client_test_pause_plugin.h"


#define GLOBUS_L_FTP_CLIENT_TEST_PAUSE_PLUGIN_NAME "globus_ftp_client_test_pause_plugin"
#define GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(d, func) \
    result = globus_ftp_client_plugin_set_##func##_func(d, globus_l_ftp_client_test_pause_plugin_##func); \
    if(result != GLOBUS_SUCCESS) goto result_exit;

static globus_bool_t globus_l_ftp_client_test_pause_plugin_activate(void);
static globus_bool_t globus_l_ftp_client_test_pause_plugin_deactivate(void);

static globus_ftp_client_plugin_t globus_l_ftp_client_static_plugin;
globus_module_descriptor_t		globus_i_ftp_client_test_pause_plugin_module =
{
    "globus_ftp_client_test_pause_plugin",
    globus_l_ftp_client_test_pause_plugin_activate,
    globus_l_ftp_client_test_pause_plugin_deactivate,
    GLOBUS_NULL
};

static
int
globus_l_ftp_client_test_pause_plugin_activate(void)
{
    int rc;

    rc = globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
    globus_ftp_client_test_pause_plugin_init(&globus_l_ftp_client_static_plugin);
    return rc;
}

static
int
globus_l_ftp_client_test_pause_plugin_deactivate(void)
{
    return globus_module_deactivate(GLOBUS_FTP_CLIENT_MODULE);
}


static
void
globus_l_ftp_client_test_pause_plugin_connect(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url)
{
    *(int *) plugin_specific = 1;
}

static
void
globus_l_ftp_client_test_pause_plugin_get(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
}

static
void
globus_l_ftp_client_test_pause_plugin_delete(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
}


static
void
globus_l_ftp_client_test_pause_plugin_mkdir(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
}

static
void
globus_l_ftp_client_test_pause_plugin_rmdir(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
}


static
void
globus_l_ftp_client_test_pause_plugin_list(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
}

static
void
globus_l_ftp_client_test_pause_plugin_verbose_list(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
}

static
void
globus_l_ftp_client_test_pause_plugin_machine_list(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
}

static
void
globus_l_ftp_client_test_pause_plugin_move(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					source_url,
    const char *					dest_url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
}

static
void
globus_l_ftp_client_test_pause_plugin_put(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
}

static
void
globus_l_ftp_client_test_pause_plugin_response(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    globus_object_t *					err,
    const globus_ftp_control_response_t *		response)
{
    if(*(int *)plugin_specific != 0)
    {
        *(int*)plugin_specific = 0;
        printf("Connection established. Press <Enter> to continue.\n");
        getchar();
    }
}

static
globus_ftp_client_plugin_t *
globus_l_ftp_client_test_pause_plugin_copy(
    globus_ftp_client_plugin_t *			self,
    void *						plugin_specific)
{
    globus_ftp_client_plugin_t * 			copy;
    globus_result_t					result;
    copy = globus_libc_malloc(sizeof(globus_ftp_client_plugin_t));
    if(!copy)
    {
	return GLOBUS_NULL;
    }

    result = globus_ftp_client_test_pause_plugin_init(copy);

    if(result != GLOBUS_SUCCESS)
    {
	globus_libc_free(copy);

	return GLOBUS_NULL;
    }
    return copy;
}

static
void
globus_l_ftp_client_test_pause_plugin_destroy(
    globus_ftp_client_plugin_t *			self,
    void *						plugin_specific)
{
    globus_ftp_client_test_pause_plugin_destroy(self);
    globus_libc_free(self);
}

static
void 
globus_l_ftp_client_test_pause_plugin_third_party_transfer(
    globus_ftp_client_plugin_t *		plugin,
    void *					plugin_specific,
    globus_ftp_client_handle_t *		handle,
    const char *				source_url,
    const globus_ftp_client_operationattr_t *	source_attr,
    const char *				dest_url,
    const globus_ftp_client_operationattr_t *	dest_attr,
    globus_bool_t				restart)
{
}

globus_result_t
globus_ftp_client_test_pause_plugin_init(
    globus_ftp_client_plugin_t *			plugin)
{
    globus_result_t					result;
    globus_object_t *					err;
    int *						plugin_specific;

    plugin_specific = globus_libc_malloc(sizeof(int));
    result = globus_ftp_client_plugin_init(
	    plugin,
	    GLOBUS_L_FTP_CLIENT_TEST_PAUSE_PLUGIN_NAME,
	    GLOBUS_FTP_CLIENT_CMD_MASK_ALL,
	    plugin_specific);
    if(result != GLOBUS_SUCCESS)
    {
	return result;
    }

    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, copy);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, destroy);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, list);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, verbose_list);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, machine_list);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, mkdir);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, rmdir);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, delete);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, move);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, get);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, put);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, third_party_transfer);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, connect);
    GLOBUS_FTP_CLIENT_PAUSE_PLUGIN_SET_FUNC(plugin, response);

    return GLOBUS_SUCCESS;

result_exit:
    err = globus_error_get(result);
    globus_ftp_client_plugin_destroy(plugin);
    return globus_error_put(err);
}

globus_result_t
globus_ftp_client_test_pause_plugin_destroy(
    globus_ftp_client_plugin_t *			plugin)
{
    void * d;
    globus_ftp_client_plugin_get_plugin_specific(plugin, &d);
    globus_libc_free(d);
    return GLOBUS_SUCCESS;
}

