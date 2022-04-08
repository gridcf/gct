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

#include "globus_ftp_client_test_restart_plugin.h"

typedef enum
{
    GLOBUS_FTP_CLIENT_IDLE,
    GLOBUS_FTP_CLIENT_CHMOD,
    GLOBUS_FTP_CLIENT_CHGRP,
    GLOBUS_FTP_CLIENT_UTIME,
    GLOBUS_FTP_CLIENT_SYMLINK,
    GLOBUS_FTP_CLIENT_MLSR,
    GLOBUS_FTP_CLIENT_CKSM,
    GLOBUS_FTP_CLIENT_DELETE,
    GLOBUS_FTP_CLIENT_MKDIR,
    GLOBUS_FTP_CLIENT_RMDIR,
    GLOBUS_FTP_CLIENT_MOVE,
    GLOBUS_FTP_CLIENT_LIST,
    GLOBUS_FTP_CLIENT_NLST,
    GLOBUS_FTP_CLIENT_MLSD,
    GLOBUS_FTP_CLIENT_MLST,
    GLOBUS_FTP_CLIENT_GET,
    GLOBUS_FTP_CLIENT_PUT,
    GLOBUS_FTP_CLIENT_TRANSFER
}
plugin_operation_t;

typedef struct
{
    globus_ftp_client_test_restart_plugin_when_t		when;
    globus_ftp_client_test_restart_plugin_when_t		next;
    char *					source_url;
    globus_ftp_client_operationattr_t		source_attr;
    char *					dest_url;
    globus_ftp_client_operationattr_t		dest_attr;

    int                                         chmod_file_mode;
    char *                      chgrp_group;
    struct tm                   utime_time;
    globus_off_t				checksum_offset;
    globus_off_t				checksum_length;
    const char *				checksum_alg;

    plugin_operation_t				op;
    globus_reltime_t				timeout;
}
globus_l_ftp_restart_plugin_specific_t;

#define GLOBUS_L_FTP_CLIENT_TEST_RESTART_PLUGIN_NAME \
    "globus_ftp_client_test_restart_plugin"
#define GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(d, func) \
    result = globus_ftp_client_plugin_set_##func##_func(d, globus_l_ftp_client_test_restart_plugin_##func); \
    if(result != GLOBUS_SUCCESS) goto result_exit;
#define GLOBUS_L_FTP_CLIENT_RESTART_PLUGIN_RETURN(plugin) \
    if(plugin == GLOBUS_NULL) \
    {\
	return globus_error_put(globus_error_construct_string(\
		GLOBUS_FTP_CLIENT_MODULE,\
		GLOBUS_NULL,\
		"[%s] NULL plugin at %s\n",\
		GLOBUS_FTP_CLIENT_MODULE->module_name,\
		myname));\
    }

static int           globus_l_ftp_client_test_restart_range = 0;

static globus_bool_t globus_l_ftp_client_test_restart_plugin_activate(void);
static globus_bool_t globus_l_ftp_client_test_restart_plugin_deactivate(void);

static
void
globus_l_ftp_client_test_restart_plugin_do_restart(
    globus_ftp_client_handle_t *			handle,
    globus_l_ftp_restart_plugin_specific_t *		d);

globus_module_descriptor_t globus_i_ftp_client_test_restart_plugin_module =
{
    "globus_ftp_client_test_restart_plugin",
    globus_l_ftp_client_test_restart_plugin_activate,
    globus_l_ftp_client_test_restart_plugin_deactivate,
    GLOBUS_NULL
};

/**
 * Module activation
 */
static
globus_bool_t
globus_l_ftp_client_test_restart_plugin_activate(void)
{
    char *                              rangecnt;
    if((rangecnt = getenv("FTP_TEST_RESTART_AFTER_RANGE")))
    {
        globus_l_ftp_client_test_restart_range = atoi(rangecnt);
        if(globus_l_ftp_client_test_restart_range < 0)
        {
            globus_l_ftp_client_test_restart_range = 0;
        }
    }
    return globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
}

/**
 * Module deactivation
 */
static
globus_bool_t
globus_l_ftp_client_test_restart_plugin_deactivate(void)
{
    return globus_module_deactivate(GLOBUS_FTP_CLIENT_MODULE);
}

static
void
globus_l_ftp_client_test_restart_plugin_authenticate(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_control_auth_info_t *		auth_info)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    d = plugin_specific;

    if(d->when == FTP_RESTART_AT_AUTH)
    {
	fprintf(stderr, "[restart plugin]: About to restart during authentication\n");
	globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
    }
    d->next = FTP_RESTART_AT_AUTH_RESPONSE;
    return;
}

static
void
globus_l_ftp_client_test_restart_plugin_connect(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    d = plugin_specific;

    if(d->when == FTP_RESTART_AT_CONNECT)
    {
	fprintf(stderr, "[restart plugin]: About to restart during connect\n");
	globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
    }
    d->next = FTP_RESTART_AT_CONNECT_RESPONSE;
    return;
}

static
void
globus_l_ftp_client_test_restart_plugin_get(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_GET;
	d->source_url = globus_libc_strdup(url);
	
	globus_ftp_client_operationattr_copy(&d->source_attr,
					     attr);
    }
    else
    {
	fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_chmod(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    int                                                 mode,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;
    
    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_CHMOD;
	d->chmod_file_mode = mode;
	d->source_url = globus_libc_strdup(url);
	
	globus_ftp_client_operationattr_copy(&d->source_attr,
					     attr);
    }
    else
    {
	fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_chgrp(
    globus_ftp_client_plugin_t *            plugin,
    void *                      plugin_specific,
    globus_ftp_client_handle_t *            handle,
    const char *                    url,
    const char *                    group,
    const globus_ftp_client_operationattr_t *       attr,
    globus_bool_t                   restart)
{
    globus_l_ftp_restart_plugin_specific_t *        d;
    
    if(!restart)
    {
    d = plugin_specific;
    d->op = GLOBUS_FTP_CLIENT_CHGRP;
    d->chgrp_group = globus_libc_strdup(group);
    d->source_url = globus_libc_strdup(url);
    
    globus_ftp_client_operationattr_copy(&d->source_attr,
                         attr);
    }
    else
    {
    fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_utime(
    globus_ftp_client_plugin_t *            plugin,
    void *                      plugin_specific,
    globus_ftp_client_handle_t *            handle,
    const char *                    url,
    const struct tm *               utime_time,
    const globus_ftp_client_operationattr_t *       attr,
    globus_bool_t                   restart)
{
    globus_l_ftp_restart_plugin_specific_t *        d;
    
    if(!restart)
    {
    d = plugin_specific;
    d->op = GLOBUS_FTP_CLIENT_UTIME;
    d->utime_time = *utime_time;
    d->source_url = globus_libc_strdup(url);
    
    globus_ftp_client_operationattr_copy(&d->source_attr,
                         attr);
    }
    else
    {
    fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_symlink(
    globus_ftp_client_plugin_t *            plugin,
    void *                      plugin_specific,
    globus_ftp_client_handle_t *            handle,
    const char *                    url,
    const char *                    link_url,
    const globus_ftp_client_operationattr_t *       attr,
    globus_bool_t                   restart)
{
    globus_l_ftp_restart_plugin_specific_t *        d;
    
    if(!restart)
    {
    d = plugin_specific;
    d->op = GLOBUS_FTP_CLIENT_SYMLINK;
    d->source_url = globus_libc_strdup(url);
    d->dest_url = globus_libc_strdup(link_url);
    
    globus_ftp_client_operationattr_copy(&d->source_attr,
                         attr);
    }
    else
    {
    fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_cksm(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    globus_off_t				        offset,
    globus_off_t				        length,
    const char *			        	algorithm,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;
    
    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_CKSM;
	d->checksum_offset = offset;
	d->checksum_length = length;
	d->checksum_alg = algorithm;
	d->source_url = globus_libc_strdup(url);
	
	globus_ftp_client_operationattr_copy(&d->source_attr,
					     attr);
    }
    else
    {
	fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_delete(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_DELETE;
	d->source_url = globus_libc_strdup(url);
	
	globus_ftp_client_operationattr_copy(&d->source_attr,
					     attr);
    }
    else
    {
	fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}


static
void
globus_l_ftp_client_test_restart_plugin_mkdir(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_MKDIR;
	d->source_url = globus_libc_strdup(url);
	
	globus_ftp_client_operationattr_copy(&d->source_attr,
					     attr);
    }
    else
    {
	fprintf(stderr, "[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_rmdir(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_RMDIR;
	d->source_url = globus_libc_strdup(url);
	
	globus_ftp_client_operationattr_copy(&d->source_attr,
					     attr);
    }
    else
    {
	fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_list(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_NLST;
	d->source_url = globus_libc_strdup(url);
	
	globus_ftp_client_operationattr_copy(&d->source_attr,
					     attr);
    }
    else
    {
	fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_mlst(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_MLST;
	d->source_url = globus_libc_strdup(url);
	
	globus_ftp_client_operationattr_copy(&d->source_attr,
					     attr);
    }
    else
    {
	fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_verbose_list(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;
    
    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_LIST;
	d->source_url = globus_libc_strdup(url);
	
	globus_ftp_client_operationattr_copy(&d->source_attr,
					     attr);
    }
    else
    {
	fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_machine_list(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;
    
    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_MLSD;
	d->source_url = globus_libc_strdup(url);
	
	globus_ftp_client_operationattr_copy(&d->source_attr,
					     attr);
    }
    else
    {
	fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_recursive_list(
    globus_ftp_client_plugin_t *            plugin,
    void *                      plugin_specific,
    globus_ftp_client_handle_t *            handle,
    const char *                    url,
    const globus_ftp_client_operationattr_t *       attr,
    globus_bool_t                   restart)
{
    globus_l_ftp_restart_plugin_specific_t *        d;
    
    if(!restart)
    {
    d = plugin_specific;
    d->op = GLOBUS_FTP_CLIENT_MLSR;
    d->source_url = globus_libc_strdup(url);
    
    globus_ftp_client_operationattr_copy(&d->source_attr,
                         attr);
    }
    else
    {
    fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_move(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					source_url,
    const char *					dest_url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_MOVE;
	d->source_url = globus_libc_strdup(source_url);
	d->dest_url = globus_libc_strdup(dest_url);
	
	globus_ftp_client_operationattr_copy(&d->source_attr,
					     attr);
    }
    else
    {
	fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}


static
void
globus_l_ftp_client_test_restart_plugin_put(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const globus_ftp_client_operationattr_t *		attr,
    globus_bool_t					restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_PUT;
	d->dest_url = globus_libc_strdup(url);
	
	globus_ftp_client_operationattr_copy(&d->dest_attr,
					     attr);
    }
    else
    {
	fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_command(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    const char *					command_name)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    d = plugin_specific;

    if(strncmp(command_name, "SITE HELP", strlen("SITE HELP")) == 0)
    {
	if(d->when == FTP_RESTART_AT_SITE_HELP)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during SITE HELP\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_SITE_HELP_RESPONSE;
    }
    else if(strncmp(command_name, "FEAT", strlen("FEAT")) == 0)
    {
	if(d->when == FTP_RESTART_AT_FEAT)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during FEAT\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle,d);
	}
	d->next = FTP_RESTART_AT_FEAT_RESPONSE;
    }
    else if(strncmp(command_name, "TYPE", strlen("TYPE")) == 0)
    {
	if(d->when == FTP_RESTART_AT_TYPE)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during TYPE\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_TYPE_RESPONSE;
    }
    else if(strncmp(command_name, "MODE", strlen("MODE")) == 0)
    {
	if(d->when == FTP_RESTART_AT_MODE)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during MODE\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_MODE_RESPONSE;
    }
    else if(strncmp(command_name, "OPTS RETR", strlen("OPTS RETR")) == 0)
    {
	if(d->when == FTP_RESTART_AT_OPTS_RETR)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during OPTS RETR\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_OPTS_RETR_RESPONSE;
    }
    else if(strncmp(command_name, "PASV", strlen("PASV")) == 0)
    {
	if(d->when == FTP_RESTART_AT_PASV)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during PASV\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_PASV_RESPONSE;
    }
    else if(strncmp(command_name, "PORT", strlen("PORT")) == 0)
    {
	if(d->when == FTP_RESTART_AT_PORT)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during PORT\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_PORT_RESPONSE;
    }
    else if(strncmp(command_name, "REST", strlen("REST")) == 0)
    {
	if(d->when == FTP_RESTART_AT_REST)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during REST\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_REST_RESPONSE;
    }
    else if(strncmp(command_name, "RETR", strlen("RETR")) == 0)
    {
	if(d->when == FTP_RESTART_AT_RETR)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during RETR\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_RETR_RESPONSE;
    }
    else if(strncmp(command_name, "STOR", strlen("STOR")) == 0)
    {
	if(d->when == FTP_RESTART_AT_STOR)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during STOR\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_STOR_RESPONSE;
    }
    else if(strncmp(command_name, "LIST", strlen("LIST")) == 0)
    {
	if(d->when == FTP_RESTART_AT_LIST)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during LIST\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_LIST_RESPONSE;
    }
    else if(strncmp(command_name, "NLST", strlen("NLST")) == 0)
    {
	if(d->when == FTP_RESTART_AT_NLST)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during NLST\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_NLST_RESPONSE;
    }
    else if(strncmp(command_name, "MLSD", strlen("MLSD")) == 0)
    {
	if(d->when == FTP_RESTART_AT_MLSD)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during MLSD\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_MLSD_RESPONSE;
    }
    else if(strncmp(command_name, "MLSR", strlen("MLSR")) == 0)
    {
    if(d->when == FTP_RESTART_AT_MLSR)
    {
        fprintf(stderr,"[restart plugin]: About to restart during MLSR\n");
        globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
    }
    d->next = FTP_RESTART_AT_MLSR_RESPONSE;
    }
    else if(strncmp(command_name, "MLST", strlen("MLST")) == 0)
    {
	if(d->when == FTP_RESTART_AT_MLST)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during MLST\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_MLST_RESPONSE;
    }
    else if(strncmp(command_name, "MKD", strlen("MKD")) == 0)
    {
	if(d->when == FTP_RESTART_AT_MKD)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during MKD\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_MKD_RESPONSE;
    }
    else if(strncmp(command_name, "RMD", strlen("RMD")) == 0)
    {
	if(d->when == FTP_RESTART_AT_RMD)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during RMD\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_RMD_RESPONSE;
    }
    else if(strncmp(command_name, "DELE", strlen("DELE")) == 0)
    {
	if(d->when == FTP_RESTART_AT_DELE)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during DELE\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_DELE_RESPONSE;
    }
    else if(strncmp(command_name, "SITE CHMOD", strlen("SITE CHMOD")) == 0)
    {
	if(d->when == FTP_RESTART_AT_CHMOD)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during CHMOD\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_CHMOD_RESPONSE;
    }
    else if(strncmp(command_name, "SITE CHGRP", strlen("SITE CHGRP")) == 0)
    {
    if(d->when == FTP_RESTART_AT_CHGRP)
    {
        fprintf(stderr,"[restart plugin]: About to restart during CHGRP\n");
        globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
    }
    d->next = FTP_RESTART_AT_CHGRP_RESPONSE;
    }
    else if(strncmp(command_name, "SITE UTIME", strlen("SITE UTIME")) == 0)
    {
    if(d->when == FTP_RESTART_AT_UTIME)
    {
        fprintf(stderr,"[restart plugin]: About to restart during UTIME\n");
        globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
    }
    d->next = FTP_RESTART_AT_UTIME_RESPONSE;
    }
    else if(strncmp(command_name, "SITE SYMLINK", strlen("SITE SYMLINK")) == 0)
    {
    if(d->when == FTP_RESTART_AT_SYMLINK)
    {
        fprintf(stderr,"[restart plugin]: About to restart during SYMLINK\n");
        globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
    }
    d->next = FTP_RESTART_AT_SYMLINK_RESPONSE;
    }
    else if(strncmp(command_name, "CKSM", strlen("CKSM")) == 0)
    {
	if(d->when == FTP_RESTART_AT_CKSM)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during CKSM\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_CKSM_RESPONSE;
    }
    else if(strncmp(command_name, "RNFR", strlen("RNFR")) == 0)
    {
	if(d->when == FTP_RESTART_AT_RNFR)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during RNFR\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_RNFR_RESPONSE;
    }
    else if(strncmp(command_name, "RNTO", strlen("RNTO")) == 0)
    {
	if(d->when == FTP_RESTART_AT_RNTO)
	{
	    fprintf(stderr,"[restart plugin]: About to restart during RNTO\n");
	    globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
	}
	d->next = FTP_RESTART_AT_RNTO_RESPONSE;
    }
    return;
}

static
void
globus_l_ftp_client_test_restart_plugin_response(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const char *					url,
    globus_object_t *					err,
    const globus_ftp_control_response_t *		response)
{
    globus_l_ftp_restart_plugin_specific_t *		d;
    globus_bool_t                                       force = GLOBUS_FALSE;

    d = plugin_specific;
    if(response->code == 111 &&
        globus_l_ftp_client_test_restart_range > 0 &&
        (d->when == FTP_RESTART_AT_STOR_RESPONSE ||
         d->when == FTP_RESTART_AT_RETR_RESPONSE))
    {
        if(--globus_l_ftp_client_test_restart_range == 0)
        {
            force = GLOBUS_TRUE;
        }
    }
    if(d->next == d->when || force)
    {
	fprintf(stderr,"[restart plugin]: About to restart during response (when=%d)\n",
	       (int) d->when);
	globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
    }
    return;
}

static
void
globus_l_ftp_client_test_restart_plugin_read(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const globus_byte_t *				buffer,
    globus_size_t					buffer_length)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    d = plugin_specific;
    if(d->when == FTP_RESTART_AT_READ)
    {
	fprintf(stderr,"[restart plugin]: About to restart during read\n");
	globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
    }
    return;
}

static
void
globus_l_ftp_client_test_restart_plugin_data(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    globus_object_t *					error,
    const globus_byte_t *				buffer,
    globus_size_t					length,
    globus_off_t					offset,
    globus_bool_t					eof)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    d = plugin_specific;
    if(d->when == FTP_RESTART_AT_DATA)
    {
	fprintf(stderr,"[restart plugin]: About to restart during data callback\n");
	globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
    }
    return;
}

static
void
globus_l_ftp_client_test_restart_plugin_write(
    globus_ftp_client_plugin_t *			plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *			handle,
    const globus_byte_t *				buffer,
    globus_size_t					buffer_length,
    globus_off_t					offset,
    globus_bool_t					eof)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    d = plugin_specific;
    if(d->when == FTP_RESTART_AT_WRITE)
    {
	fprintf(stderr,"[restart plugin]: About to restart during write\n");
	globus_l_ftp_client_test_restart_plugin_do_restart(handle, d);
    }
    return;
}

static
globus_ftp_client_plugin_t *
globus_l_ftp_client_test_restart_plugin_copy(
    globus_ftp_client_plugin_t *			self,
    void *						plugin_specific)
{
    globus_ftp_client_plugin_t *			newguy;
    globus_l_ftp_restart_plugin_specific_t *		d;
    globus_result_t					result;

    d = (globus_l_ftp_restart_plugin_specific_t *) plugin_specific;

    newguy = globus_libc_malloc(sizeof(globus_ftp_client_plugin_t));
    if(newguy == GLOBUS_NULL)
    {
	goto error_exit;
    }
    result = globus_ftp_client_test_restart_plugin_init(newguy);
    if(result != GLOBUS_SUCCESS)
    {
	goto free_exit;
    }
    result = globus_ftp_client_test_restart_plugin_set_restart_point(
	    newguy,
	    d->when,
	    &d->timeout);
    if(result != GLOBUS_SUCCESS)
    {
	goto destroy_exit;
    }
    return newguy;

destroy_exit:
    globus_ftp_client_test_restart_plugin_destroy(newguy);
free_exit:
    globus_libc_free(newguy);
error_exit:

    return GLOBUS_NULL;
}

static
void
globus_l_ftp_client_test_restart_plugin_destroy(
    globus_ftp_client_plugin_t *			self,
    void *						plugin_specific)
{
    globus_ftp_client_test_restart_plugin_destroy(self);
    globus_libc_free(self);
}

static
void 
globus_l_ftp_client_test_restart_plugin_third_party_transfer(
    globus_ftp_client_plugin_t *		plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *		handle,
    const char *				source_url,
    const globus_ftp_client_operationattr_t *	source_attr,
    const char *				dest_url,
    const globus_ftp_client_operationattr_t *	dest_attr,
    globus_bool_t				restart)
{
    globus_l_ftp_restart_plugin_specific_t *		d;

    d = plugin_specific;
    if(!restart)
    {
	d = plugin_specific;
	d->op = GLOBUS_FTP_CLIENT_TRANSFER;
	d->source_url = globus_libc_strdup(source_url);
	globus_ftp_client_operationattr_copy(&d->source_attr,
					     source_attr);
	d->dest_url = globus_libc_strdup(dest_url);
	globus_ftp_client_operationattr_copy(&d->dest_attr,
					     dest_attr);
    }
    else
    {
	fprintf(stderr,"[restart plugin]: We've been restarted\n");
    }
}


static
void 
globus_l_ftp_client_test_restart_plugin_fault(
    globus_ftp_client_plugin_t *		plugin,
    void *						plugin_specific,
    globus_ftp_client_handle_t *		handle,
    const char *				url,
    globus_object_t *				error)
{
    fprintf(stderr,"[restart plugin]: Fault detected\n");
}

globus_result_t
globus_ftp_client_test_restart_plugin_init(
    globus_ftp_client_plugin_t *			plugin)
{
    globus_object_t *					err;
    globus_result_t					result;
    static char * myname = "globus_ftp_client_test_restart_plugin_init";
    globus_l_ftp_restart_plugin_specific_t *		d;

    if(plugin == GLOBUS_NULL)
    {
	return globus_error_put(globus_error_construct_string(
		GLOBUS_FTP_CLIENT_MODULE,
		GLOBUS_NULL,
		"[%s] NULL plugin at %s\n",
		GLOBUS_FTP_CLIENT_MODULE->module_name,
		myname));
    }

    d = globus_libc_calloc(1, sizeof(globus_l_ftp_restart_plugin_specific_t));
    if(d == GLOBUS_NULL)
    {
	return globus_error_put(globus_error_construct_string(
		    GLOBUS_FTP_CLIENT_MODULE,
		    GLOBUS_NULL,
		    "[%s] Could not allocate internal data structure at %s\n",
		    GLOBUS_FTP_CLIENT_MODULE->module_name,
		    myname));
    }

    d->when = FTP_RESTART_NEVER;
    GlobusTimeReltimeSet(d->timeout,0,0);

    result = globus_ftp_client_plugin_init(plugin,
	                          GLOBUS_L_FTP_CLIENT_TEST_RESTART_PLUGIN_NAME,
				  GLOBUS_FTP_CLIENT_CMD_MASK_ALL,
				  d);

    if(result != GLOBUS_SUCCESS)
    {
	globus_free(d);
	return result;
    }

    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, copy);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, destroy);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, list);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, verbose_list);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, machine_list);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, mlst);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, chmod);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, chgrp);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, utime);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, symlink);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, recursive_list);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, cksm);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, delete);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, mkdir);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, rmdir);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, move);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, get);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, put);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, third_party_transfer);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, connect);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, authenticate);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, read);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, write);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, data);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, command);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, response);
    GLOBUS_FTP_CLIENT_RESTART_PLUGIN_SET_FUNC(plugin, fault);

    return GLOBUS_SUCCESS;

result_exit:
    err = globus_error_get(result);
    globus_ftp_client_plugin_destroy(plugin);

    return globus_error_put(err);
}

globus_result_t
globus_ftp_client_test_restart_plugin_destroy(
    globus_ftp_client_plugin_t *			plugin)
{
    globus_l_ftp_restart_plugin_specific_t *		d;
    globus_result_t					result;
    static char * myname = "globus_ftp_client_test_restart_plugin_destroy";

    GLOBUS_L_FTP_CLIENT_RESTART_PLUGIN_RETURN(plugin);

    result = globus_ftp_client_plugin_get_plugin_specific(plugin,
                                                          (void **) &d);
    if(result != GLOBUS_SUCCESS)
    {
        return result;
    }

    globus_libc_free(d);

    return globus_ftp_client_plugin_destroy(plugin);
}

globus_result_t
globus_ftp_client_test_restart_plugin_set_restart_point(
    globus_ftp_client_plugin_t *			plugin,
    globus_ftp_client_test_restart_plugin_when_t	when,
    globus_reltime_t *					timeout)
{
    globus_l_ftp_restart_plugin_specific_t *		d;
    globus_result_t					result;
    static char * myname = "globus_ftp_client_test_restart_plugin_set_restart_point";

    GLOBUS_L_FTP_CLIENT_RESTART_PLUGIN_RETURN(plugin);

    result = globus_ftp_client_plugin_get_plugin_specific(plugin,
                                                          (void **) &d);

    if(result == GLOBUS_SUCCESS)
    {
	d->when = when;
	GlobusTimeReltimeCopy(d->timeout, *timeout);

	return GLOBUS_SUCCESS;
    }
    else
    {
	return result;
    }
}

static
void
globus_l_ftp_client_test_restart_plugin_do_restart(
    globus_ftp_client_handle_t *			handle,
    globus_l_ftp_restart_plugin_specific_t *		d)
{
    globus_abstime_t					delay;

    GlobusTimeAbstimeGetCurrent(delay);
    GlobusTimeAbstimeInc(delay, d->timeout);

    d->when = FTP_RESTART_NEVER;

    globus_assert(d->op == GLOBUS_FTP_CLIENT_LIST   ||
		  d->op == GLOBUS_FTP_CLIENT_NLST   ||
		  d->op == GLOBUS_FTP_CLIENT_MLSD   ||
		  d->op == GLOBUS_FTP_CLIENT_MLST   ||
		  d->op == GLOBUS_FTP_CLIENT_MOVE   ||
		  d->op == GLOBUS_FTP_CLIENT_CHMOD  ||
          d->op == GLOBUS_FTP_CLIENT_CHGRP  ||
          d->op == GLOBUS_FTP_CLIENT_UTIME  ||
          d->op == GLOBUS_FTP_CLIENT_SYMLINK ||
          d->op == GLOBUS_FTP_CLIENT_MLSR   ||
		  d->op == GLOBUS_FTP_CLIENT_CKSM   ||
		  d->op == GLOBUS_FTP_CLIENT_DELETE ||
		  d->op == GLOBUS_FTP_CLIENT_MKDIR  ||
		  d->op == GLOBUS_FTP_CLIENT_RMDIR  ||
		  d->op == GLOBUS_FTP_CLIENT_GET    ||
		  d->op == GLOBUS_FTP_CLIENT_PUT    ||
		  d->op == GLOBUS_FTP_CLIENT_TRANSFER);

    if(d->op == GLOBUS_FTP_CLIENT_LIST)
    {
	globus_ftp_client_plugin_restart_verbose_list(handle,
						      d->source_url,
						      &d->source_attr,
						      &delay);
    }
    else if(d->op == GLOBUS_FTP_CLIENT_NLST)
    {
	globus_ftp_client_plugin_restart_list(handle,
					      d->source_url,
					      &d->source_attr,
					      &delay);
    }
    else if(d->op == GLOBUS_FTP_CLIENT_MLSD)
    {
	globus_ftp_client_plugin_restart_machine_list(handle,
					      d->source_url,
					      &d->source_attr,
					      &delay);
    }
    else if(d->op == GLOBUS_FTP_CLIENT_MLSR)
    {
    globus_ftp_client_plugin_restart_recursive_list(handle,
                          d->source_url,
                          &d->source_attr,
                          &delay);
    }
    else if(d->op == GLOBUS_FTP_CLIENT_MLST)
    {
	globus_ftp_client_plugin_restart_mlst(handle,
						d->source_url,
						&d->source_attr,
						&delay);
	
    }    
    else if(d->op == GLOBUS_FTP_CLIENT_CHMOD)
    {
	globus_ftp_client_plugin_restart_chmod(handle,
						d->source_url,
						d->chmod_file_mode,
						&d->source_attr,
						&delay);
	
    }    
    else if(d->op == GLOBUS_FTP_CLIENT_CHGRP)
    {
    globus_ftp_client_plugin_restart_chgrp(handle,
                        d->source_url,
                        d->chgrp_group,
                        &d->source_attr,
                        &delay);
    
    }    
    else if(d->op == GLOBUS_FTP_CLIENT_UTIME)
    {
    globus_ftp_client_plugin_restart_utime(handle,
                        d->source_url,
                        &d->utime_time,
                        &d->source_attr,
                        &delay);
    
    }    
    else if(d->op == GLOBUS_FTP_CLIENT_SYMLINK)
    {
    globus_ftp_client_plugin_restart_symlink(handle,
                        d->source_url,
                        d->dest_url,
                        &d->source_attr,
                        &delay);
    
    }    
    else if(d->op == GLOBUS_FTP_CLIENT_CKSM)
    {
	globus_ftp_client_plugin_restart_cksm(handle,
						d->source_url,
						d->checksum_offset,
						d->checksum_length,
						d->checksum_alg,
						&d->source_attr,
						&delay);
	
    }    
    else if(d->op == GLOBUS_FTP_CLIENT_DELETE)
    {
	globus_ftp_client_plugin_restart_delete(handle,
						d->source_url,
						&d->source_attr,
						&delay);
	
    }
    else if(d->op == GLOBUS_FTP_CLIENT_MKDIR)
    {
	globus_ftp_client_plugin_restart_mkdir(handle,
					       d->source_url,
					       &d->source_attr,
					       &delay);
	
    }
    else if(d->op == GLOBUS_FTP_CLIENT_RMDIR)
    {
	globus_ftp_client_plugin_restart_rmdir(handle,
					       d->source_url,
					       &d->source_attr,
					       &delay);
	
    }
    else if(d->op == GLOBUS_FTP_CLIENT_MOVE)
    {
	globus_ftp_client_plugin_restart_move(handle,
					      d->source_url,
					      d->dest_url,
					      &d->source_attr,
					      &delay);
    }
    else if(d->op == GLOBUS_FTP_CLIENT_GET)
    {
	globus_ftp_client_plugin_restart_get(handle,
					     d->source_url,
					     &d->source_attr,
					     GLOBUS_NULL,
					     &delay);
    }
    else if(d->op == GLOBUS_FTP_CLIENT_PUT)
    {
	globus_ftp_client_plugin_restart_put(handle,
					     d->dest_url,
					     &d->dest_attr,
					     GLOBUS_NULL,
					     &delay);
    }
    else if(d->op == GLOBUS_FTP_CLIENT_TRANSFER)
    {
	/* Enable auto-discovery of restart point */
	globus_ftp_client_plugin_restart_third_party_transfer(handle,
						              d->source_url,
						              &d->source_attr,
						              d->dest_url,
						              &d->dest_attr,
						              GLOBUS_NULL,
						              &delay);
    }
}









