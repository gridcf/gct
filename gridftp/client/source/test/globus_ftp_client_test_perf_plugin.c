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

#include "globus_ftp_client_test_perf_plugin.h"
#include "globus_ftp_client_perf_plugin.h"

static globus_bool_t globus_l_ftp_client_test_perf_plugin_activate(void);
static globus_bool_t globus_l_ftp_client_test_perf_plugin_deactivate(void);

globus_module_descriptor_t globus_i_ftp_client_test_perf_plugin_module =
{
    "globus_ftp_client_test_perf_plugin",
    globus_l_ftp_client_test_perf_plugin_activate,
    globus_l_ftp_client_test_perf_plugin_deactivate,
    GLOBUS_NULL
};

static
int
globus_l_ftp_client_test_perf_plugin_activate(void)
{
    int rc;

    rc = globus_module_activate(GLOBUS_FTP_CLIENT_PERF_PLUGIN_MODULE);
    return rc;
}

static
int
globus_l_ftp_client_test_perf_plugin_deactivate(void)
{
    return globus_module_deactivate(GLOBUS_FTP_CLIENT_PERF_PLUGIN_MODULE);
}

static
void perf_plugin_begin_cb(
    void *                                          user_specific,
    globus_ftp_client_handle_t *                    handle,
    const char *                                    source_url,
    const char *                                    dest_url,
    globus_bool_t                                   restart)
{
    globus_libc_fprintf(stderr, "perf_plugin_begin_cb\n");
}

static
void perf_plugin_marker_cb(
    void *                                          user_specific,
    globus_ftp_client_handle_t *                    handle,
    long                                            time_stamp_int,
    char                                            time_stamp_tenth,
    int                                             stripe_ndx,
    int                                             num_stripes,
    globus_off_t                                    nbytes)
{
    globus_libc_fprintf(stderr, "perf_plugin_marker_cb\n");
    globus_libc_fprintf(stderr, "time_stamp   %ld.%d\n", time_stamp_int, time_stamp_tenth);
    globus_libc_fprintf(stderr, "stripe_ndx   %d\n", stripe_ndx);
    globus_libc_fprintf(stderr, "num_stripes  %d\n", num_stripes);
    globus_libc_fprintf(stderr, "nbytes       %" GLOBUS_OFF_T_FORMAT "\n", nbytes);
}

static
void perf_plugin_complete_cb(
    void *                                          user_specific,
    globus_ftp_client_handle_t *                    handle,
    globus_bool_t                                   success)
{
    globus_libc_fprintf(stderr, "perf_plugin_complete_cb\n");
}

globus_result_t
globus_ftp_client_test_perf_plugin_init(
    globus_ftp_client_plugin_t *			plugin)
{
    return globus_ftp_client_perf_plugin_init(
        plugin,
        perf_plugin_begin_cb,
        perf_plugin_marker_cb,
        perf_plugin_complete_cb,
        GLOBUS_NULL);
}

globus_result_t
globus_ftp_client_test_perf_plugin_destroy(
    globus_ftp_client_plugin_t *			plugin)
{
    return globus_ftp_client_perf_plugin_destroy(plugin);
}
