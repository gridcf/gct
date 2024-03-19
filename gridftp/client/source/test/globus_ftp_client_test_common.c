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

#include "globus_common.h"

#include <stdio.h>
#include <string.h>
#ifdef WIN32
#include <fcntl.h>
#else
#include <unistd.h>
#endif

#include "globus_ftp_client.h"
#include "globus_ftp_client_plugin.h"

#include "globus_ftp_client_test_restart_plugin.h"
#include "globus_ftp_client_test_abort_plugin.h"
#include "globus_ftp_client_debug_plugin.h"
#include "globus_ftp_client_restart_plugin.h"
#include "globus_ftp_client_test_perf_plugin.h"
#include "globus_ftp_client_test_throughput_plugin.h"
#include "globus_ftp_client_test_pause_plugin.h"
#include "globus_preload.h"

int test_abort_count = 0;

void
test_parse_args(int argc,
		char **argv,
		globus_ftp_client_handleattr_t * handle_attr,
		globus_ftp_client_operationattr_t * operation_attr,
		char **src,
		char **dst)
{
    int c;
    extern char * optarg;
    extern int opterr;
    globus_reltime_t timeout;
    globus_ftp_client_plugin_t *plugin;
    globus_ftp_control_dcau_t dcau;
    globus_abstime_t deadline_time;
    globus_reltime_t interval_time;
    int max_retries;
    long long interval;
    long long deadline;
    char * subject;

    *src = GLOBUS_NULL;
    *dst = GLOBUS_NULL;

    setvbuf(stdout, 0, _IONBF, 0);
    
#ifdef WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif

    opterr = 0;
    while((c = getopt(argc, argv, "-f:a:ps:d:r:zMTc:t:i")) != -1)
    {
	switch(c)
	{
	case 'a':
	    globus_module_activate(GLOBUS_FTP_CLIENT_TEST_ABORT_PLUGIN_MODULE);

	    plugin = globus_libc_malloc(sizeof(globus_ftp_client_plugin_t));
	    globus_ftp_client_test_abort_plugin_init(plugin);


	    if(atoi(optarg) >= FTP_ABORT_LAST ||
	       atoi(optarg) < 0)
	    {
		printf("Abort plugin argument out of range\n");
		globus_module_deactivate_all();
		exit(1);
	    }
	    globus_ftp_client_test_abort_plugin_set_abort_point(plugin,
							        atoi(optarg));

	    globus_ftp_client_test_abort_plugin_set_abort_counter(
		plugin,
		&test_abort_count);

	    globus_ftp_client_handleattr_add_plugin(handle_attr, plugin);

	    break;
	case 'p':
	    plugin = globus_libc_malloc(sizeof(globus_ftp_client_plugin_t));
	    globus_module_activate(GLOBUS_FTP_CLIENT_DEBUG_PLUGIN_MODULE);
	    globus_ftp_client_debug_plugin_init(plugin, stderr, "[Debug Plugin]");

	    globus_ftp_client_handleattr_add_plugin(handle_attr, plugin);

	    break;
	case 'M':
	    plugin = globus_libc_malloc(sizeof(globus_ftp_client_plugin_t));
	    globus_module_activate(GLOBUS_FTP_CLIENT_TEST_PERF_PLUGIN_MODULE);
	    globus_ftp_client_test_perf_plugin_init(plugin);

	    globus_ftp_client_handleattr_add_plugin(handle_attr, plugin);

	    break;
	case 'T':
	    plugin = globus_libc_malloc(sizeof(globus_ftp_client_plugin_t));
	    globus_module_activate(GLOBUS_FTP_CLIENT_TEST_THROUGHPUT_PLUGIN_MODULE);
	    globus_ftp_client_test_throughput_plugin_init(plugin);

	    globus_ftp_client_handleattr_add_plugin(handle_attr, plugin);

	    break;

	case 'z':
	    globus_module_activate(GLOBUS_FTP_CLIENT_TEST_PAUSE_PLUGIN_MODULE);

	    plugin = globus_libc_malloc(sizeof(globus_ftp_client_plugin_t));
	    globus_ftp_client_test_pause_plugin_init(plugin);

	    globus_ftp_client_handleattr_add_plugin(handle_attr, plugin);

	    break;
	case 'r':
	    globus_module_activate(GLOBUS_FTP_CLIENT_TEST_RESTART_PLUGIN_MODULE);
	    plugin = globus_libc_malloc(sizeof(globus_ftp_client_plugin_t));
	    globus_ftp_client_test_restart_plugin_init(plugin);
	    if(atoi(optarg) >= FTP_RESTART_LAST ||
	       atoi(optarg) < 0)
	    {
		printf("Restart plugin argument out of range\n");
		globus_module_deactivate_all();
		exit(1);
	    }
	    else
	    {
		char *p;
		p = strchr(optarg, ',');
		if(p)
		{
		    GlobusTimeReltimeSet(timeout, atoi(p+1),0);
		}
		else
		{
		    GlobusTimeReltimeSet(timeout, 0, 0);
		}
		globus_ftp_client_test_restart_plugin_set_restart_point(
		    plugin,
		    atoi(optarg),
		    &timeout);
		globus_ftp_client_handleattr_add_plugin(handle_attr, plugin);
	    }

	    break;
	case 's':
	    *src = optarg;
	    break;
	case 'd':
	    *dst = optarg;
	    break;
	case 'c':
	    if(!strcmp(optarg, "none"))
	    {
		dcau.mode = GLOBUS_FTP_CONTROL_DCAU_NONE;
		globus_ftp_client_operationattr_set_dcau(operation_attr,
							  &dcau);
	    }
	    else if(!strcmp(optarg, "self"))
	    {
		dcau.mode = GLOBUS_FTP_CONTROL_DCAU_SELF;
		globus_ftp_client_operationattr_set_dcau(operation_attr,
							  &dcau);
	    }
	    else
	    {
		dcau.mode = GLOBUS_FTP_CONTROL_DCAU_SUBJECT;
		dcau.subject.subject = optarg;
		globus_ftp_client_operationattr_set_dcau(operation_attr,
							  &dcau);
	    }
	    break;
	case 't':
	    if(!strcmp(optarg, "clear"))
	    {
		globus_ftp_client_operationattr_set_data_protection(
			operation_attr,
			GLOBUS_FTP_CONTROL_PROTECTION_CLEAR);
	    }
	    else if(!strcmp(optarg, "safe"))
	    {
		globus_ftp_client_operationattr_set_data_protection(
			operation_attr,
			GLOBUS_FTP_CONTROL_PROTECTION_SAFE);
	    }
	    else if(!strcmp(optarg, "private"))
	    {
		globus_ftp_client_operationattr_set_data_protection(
			operation_attr,
			GLOBUS_FTP_CONTROL_PROTECTION_PRIVATE);
	    }
	    break;
	case 'f':
	    globus_module_activate(GLOBUS_FTP_CLIENT_RESTART_PLUGIN_MODULE);
	    sscanf(optarg, "%d,%lld,%lld", &max_retries, &interval, &deadline);

	    if(interval < 0.1)
	    {
		GlobusTimeReltimeSet(interval_time, 0, 0);
	    }
	    else
	    {
		GlobusTimeReltimeSet(interval_time, interval, 0);
	    }
	    deadline_time.tv_sec = deadline;
	    deadline_time.tv_nsec = 0;

	    plugin = globus_libc_malloc(sizeof(globus_ftp_client_plugin_t));
	    globus_ftp_client_restart_plugin_init(plugin,
		                                  max_retries,
		                                  &interval_time,
						  &deadline_time);
	    globus_ftp_client_handleattr_add_plugin(handle_attr, plugin);
	    break;

	case 'i':
	    globus_ftp_client_operationattr_set_control_protection(
			operation_attr,
			GLOBUS_FTP_CONTROL_PROTECTION_SAFE);
	    break;
	case '?':
        /*  globus_module_deactivate_all();
	    exit(0); 
	*/
	    break;
	}
    }
    subject = getenv("GLOBUS_FTP_CLIENT_TEST_SUBJECT");
    if(subject)
    {
        globus_ftp_client_operationattr_set_authorization(
            operation_attr,
            GSS_C_NO_CREDENTIAL,
            ":globus-mapping:",
            "",
            GLOBUS_NULL,
            subject);
    }
}

void
test_remove_arg(int *argc, char **argv, int *start, int num_of_options)
{
    int j;

    for(j = *start; j + num_of_options + 1 < *argc; j++)
    {
	argv[j] = argv[j + num_of_options + 1];
    }
    *argc -= num_of_options + 1;
    (*start)--;
}
