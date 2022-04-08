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

#include "globus_xio.h"
#include "globus_xio_tcp_driver.h"
#include "globus_xio_gsi.h"
#include "globus_gfork.h"
#include "gfs_i_gfork_plugin.h"
#include "globus_preload.h"


#ifdef __GNUC__
#define GlobusDynClientFuncName(func) static const char * _dyn_func_name __attribute__((__unused__)) = #func
#else
#define GlobusDynClientFuncName(func) static const char * _dyn_func_name = #func
#endif

#define GlobusDynClientError(error_msg, _type)                              \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            NULL,                                                           \
            NULL,                                                           \
            _type,                                                          \
            __FILE__,                                                       \
            _dyn_func_name,                                                 \
            __LINE__,                                                       \
            "%s",                                                           \
            (error_msg)))

enum 
{
    GLOBUS_DYNCLIENT_ERROR_PARM = 1
};

static globus_bool_t                    g_quiet = GLOBUS_FALSE;
static globus_bool_t                    g_use_gsi;
static globus_xio_handle_t              g_xio_handle;
static uint32_t                         g_at_once = 0;
static uint32_t                         g_total_cons = 0;
static globus_xio_driver_t              g_tcp_driver;
static globus_xio_driver_t              g_gsi_driver;

static
globus_result_t
gfs_l_dynclient_master_options(
    int                                 argc,
    char **                             argv);

static
void
gfs_l_dynclient_log(
    globus_result_t                     result,
    int                                 level,
    char *                              fmt,
    ...)
{
    va_list                             ap;

    if(g_quiet)
    {
        return;
    }

    va_start(ap, fmt);

    fprintf(stderr, "[gridftp gfork plugin] : ");
    if(result != GLOBUS_SUCCESS)
    {
        char * err_str = globus_error_print_friendly(
            globus_error_peek(result));

        fprintf(stderr, "ERROR : %s : ", err_str);
        globus_free(err_str);
    }
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}


static
globus_result_t
gfs_l_dynclient_xio_setup()
{
    globus_result_t                     result;
    globus_xio_attr_t                   attr;
    globus_xio_stack_t                  stack;

    result = globus_xio_attr_init(&attr);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_init;
    }
    result = globus_xio_stack_init(&stack, NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_stack_init;
    }

    result = globus_xio_driver_load("tcp", &g_tcp_driver);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_tcp;
    }

    result = globus_xio_stack_push_driver(stack, g_tcp_driver);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_tcp_push;
    }

    if(g_use_gsi)
    {
        result = globus_xio_driver_load("gsi", &g_gsi_driver);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_gsi;
        }
        result = globus_xio_stack_push_driver(stack, g_gsi_driver);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_gsi_push;
        }
    }

    result = globus_xio_handle_create(&g_xio_handle, stack);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_handle;
    }

    return GLOBUS_SUCCESS;

error_handle:
error_gsi_push:
error_gsi:
error_tcp_push:
error_tcp:
error_stack_init:
error_attr_init:

    return result;
}

int
main(
    int                                 argc,
    char **                             argv)
{
    globus_result_t                     result;
    int                                 rc;
    globus_byte_t                       buffer[GF_DYN_PACKET_LEN];
    uint32_t                            tmp32;
    char *                              be_cs;
    char *                              reg_cs;
    globus_size_t                       nbytes;

    LTDL_SET_PRELOADED_SYMBOLS();
    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    if(rc != 0)
    {
        result = GLOBUS_FAILURE;
        goto error_activate;
    }

    result = gfs_l_dynclient_master_options(argc, argv);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_opts;
    }
    if(argc < 3)
    {
        fprintf(stderr, "%s [options] <backend contact string>"
            " <frontend contact string>\n", argv[0]);
        fprintf(stderr, "Use -help for more information\n");
        exit(0);
    }

    be_cs = argv[argc - 2];
    reg_cs = argv[argc - 1];

    result = gfs_l_dynclient_xio_setup();
    if(result != GLOBUS_SUCCESS)
    {
        goto error_xio;
    }

    memset(buffer, '\0', GF_DYN_PACKET_LEN);
    buffer[GF_VERSION_NDX] = GF_VERSION;
    buffer[GF_MSG_TYPE_NDX] = GFS_GFORK_MSG_TYPE_DYNBE;

    tmp32 = htonl(g_at_once);
    memcpy(&buffer[GF_DYN_AT_ONCE_NDX], &tmp32, sizeof(uint32_t));

    tmp32 = htonl(g_total_cons);
    memcpy(&buffer[GF_DYN_TOTAL_NDX], &tmp32, sizeof(uint32_t));

    tmp32 = htonl(1);
    memcpy(&buffer[GF_DYN_ENTRY_COUNT_NDX], &tmp32, sizeof(uint32_t));

    strncpy((char *) &buffer[GF_DYN_CS_NDX], be_cs, GF_DYN_CS_LEN - 1);

    result = globus_xio_open(g_xio_handle, reg_cs, NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_open;
    }
    result = globus_xio_write(
        g_xio_handle, buffer, 
        GF_DYN_PACKET_LEN, GF_DYN_PACKET_LEN, &nbytes, NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_write;
    }
    /* read reply */
    result = globus_xio_read(
        g_xio_handle, buffer,
        GF_DYN_PACKET_LEN, GF_DYN_PACKET_LEN, &nbytes, NULL);
    if(result != GLOBUS_SUCCESS)
    {
        gfs_l_dynclient_log(GLOBUS_SUCCESS, 0,
            "Read failed\n");
        goto error_read;
    }
    result = globus_xio_close(g_xio_handle, NULL);
    if(result != GLOBUS_SUCCESS)
    {
        gfs_l_dynclient_log(GLOBUS_SUCCESS, 0,
            "Close failed\n");
        goto error_close;
    }

    gfs_l_dynclient_log(GLOBUS_SUCCESS, 1,
        "proper net commication with %s\n",
        reg_cs);

    if(buffer[GF_MSG_TYPE_NDX] == GFS_GFORK_MSG_TYPE_ACK)
    {
        gfs_l_dynclient_log(GLOBUS_SUCCESS, 0,
            "SUCCESS: registered %s to %s",
            be_cs, reg_cs);
        rc = 0;
    }
    else
    {
        gfs_l_dynclient_log(GLOBUS_SUCCESS, 0,
            "ERROR: %s rejected registration of %s",
            reg_cs, be_cs);
        rc = 1;
    }

    return rc;

error_close:
error_read:
error_write:
error_open:
error_xio:
error_opts:
error_activate:
    gfs_l_dynclient_log(result, 0, "");

    return 2;
}

static
globus_result_t
gfs_l_dynclient_opts_max(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    int                                 sc;
    int                                 max;
    GlobusDynClientFuncName(gfs_l_dynclient_opts_max);

    sc = sscanf(opt[0], "%u", &max);
    if(sc != 1)
    {
        goto error;
    }
    *out_parms_used = 1;
    g_at_once = max;
    return GLOBUS_SUCCESS;
error:
    return GlobusDynClientError(
        "max be an integer",
        GLOBUS_DYNCLIENT_ERROR_PARM);

    return GLOBUS_SUCCESS;
}


static
globus_result_t
gfs_l_dynclient_opts_total(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    int                                 sc;
    int                                 con;
    GlobusDynClientFuncName(gfs_l_dynclient_opts_max);

    sc = sscanf(opt[0], "%u", &con);
    if(sc != 1)
    {
        goto error;
    }
    *out_parms_used = 1;
    g_total_cons = con;
    return GLOBUS_SUCCESS;
error:
    return GlobusDynClientError(
        "The total must be an integer",
        GLOBUS_DYNCLIENT_ERROR_PARM);

    return GLOBUS_SUCCESS;
}



static
globus_result_t
gfs_l_dynclient_opts_help(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    globus_options_help(opts_handle);

    printf("Dynamic client program\n");
    exit(0);
    return GLOBUS_SUCCESS;
}

static
globus_result_t
gfs_l_dynclient_opts_quiet(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    g_quiet = GLOBUS_TRUE;

    return GLOBUS_SUCCESS;
}

static
globus_result_t
gfs_l_dynclient_opts_gsi(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    globus_bool_t                       b = GLOBUS_FALSE;

    if(strcasecmp(opt[0], "t") == 0 ||
        strcasecmp(opt[0], "y") == 0 ||
        strcasecmp(opt[0], "yes") == 0 ||
        strcasecmp(opt[0], "true") == 0)
    {
        b = GLOBUS_TRUE;
    }

    g_use_gsi = b;
    *out_parms_used = 1;

    return GLOBUS_SUCCESS;
}

globus_options_entry_t                   gfork_l_opts_table[] =
{
    {"quiet", "q", NULL, NULL,
        "print no output",
        0, gfs_l_dynclient_opts_quiet},
    {"help", "h", NULL, NULL,
        "print the help message",
        0, gfs_l_dynclient_opts_help},
    {"gsi", "G", NULL, "<bool>",
        "Enable or disable GSI.  Default is on.",
        1, gfs_l_dynclient_opts_gsi},
    {"total", "t", NULL, "<int>",
        "set the maximum total number of connections allowed to the backend",
        1, gfs_l_dynclient_opts_total},
    {"max", "m", NULL, "<int>",
        "set the maximum number of connections allowed to the backend at one time",
        1, gfs_l_dynclient_opts_max},
    {NULL, NULL, NULL, NULL, NULL, 0, NULL}
};

static
globus_result_t
gfs_l_dynclient_master_opts_unknown(
    globus_options_handle_t             opts_handle,
    void *                              unknown_arg,
    int                                 argc,
    char **                             argv)
{
    return GLOBUS_SUCCESS;
}



static
globus_result_t
gfs_l_dynclient_master_options(
    int                                 argc,
    char **                             argv)
{
    globus_options_handle_t             opt_h;
    globus_result_t                     result;

    globus_options_init(
        &opt_h, gfs_l_dynclient_master_opts_unknown, NULL);
    globus_options_add_table(opt_h, gfork_l_opts_table, NULL);
    result = globus_options_command_line_process(opt_h, argc, argv);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    return GLOBUS_SUCCESS;
error:
    return result;
}

