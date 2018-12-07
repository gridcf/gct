/*
 * Copyright 1999-2009 University of Chicago
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
 * @file globus_gram_job_manager_config.c
 * @brief Resource Allocation Job Manager Configuration
 */

#include "globus_gram_job_manager.h"
#include "globus_common.h"
#include <sys/utsname.h>

static int
globus_l_gram_tokenize(
    char *                              command,
    char **                             args,
    int *                               n);

static
int
globus_l_env_present(
    void *                              datum,
    void *                              arg);

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

/**
 * Initialize configuration based on command-line arguments
 *
 * @param config
 *     LRMA-specific configuration state
 * @param argc
 *     Count of command-line arguments to the job manager.
 * @param argv
 *     Array of command-line arguments to the job manager.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_GATEKEEPER_MISCONFIGURED
 *     Command-line includes -help
 */
int
globus_gram_job_manager_config_init(
    globus_gram_job_manager_config_t *  config,
    int                                 argc,
    char **                             argv)
{
    int                                 i;
    int                                 rc = 0;
    char *                              tmp;
    char                                hostname[MAXHOSTNAMELEN];
    struct utsname                      utsname;
    char *                              conf_path = NULL;
    char *                              dot;
    char *                              gatekeeper_contact;

    memset(config, 0, sizeof(globus_gram_job_manager_config_t));

    /* if -conf is passed then get the arguments from the file
     * specified
     */
    if (argc > 2 && !strcmp(argv[1],"-conf"))
    {
        char ** newargv;
        char * newbuf;
        int newargc = 52;
        int length;
        FILE *fp;

        newargv = malloc(newargc * sizeof(char *)); /* not freed */
        newargv[0] = argv[0];

        /* get file length via fseek & ftell */
        if ((fp = fopen(argv[2], "r")) == NULL)
        {
            globus_gram_job_manager_log(
                    NULL,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_FATAL,
                    "event=gram.config.end level=FATAL path=\"%s\" "
                    "status=-1 msg=\"%s\" errno=%d reason=\"%s\"\n",
                    argv[2],
                    "Error opening configuration file",
                    errno,
                    strerror(errno));
            exit(1);
        }
        conf_path = argv[2];
        fseek(fp, 0, SEEK_END);
        length = ftell(fp);
        if (length <=0)
        {
            globus_gram_job_manager_log(
                    NULL,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_FATAL,
                    "event=gram.config.end level=FATAL path=\"%s\" "
                    "status=-1 msg=\"%s\" errno=%d reason=\"%s\"\n",
                    conf_path,
                    "Error determining config file length",
                    errno,
                    strerror(errno));
           exit(1);
        }
        rewind(fp);

        newbuf = malloc(length+1);  /* don't free */
        i = fread(newbuf, 1, length, fp);
        if (i < 0)
        {
            globus_gram_job_manager_log(
                    NULL,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_FATAL,
                    "event=gram.config.end level=FATAL path=\"%s\" "
                    "status=-1 msg=\"%s\" errno=%d reason=\"%s\"\n",
                    conf_path,
                    "Error reading configuration file",
                    errno,
                    strerror(errno));
            exit(1);
        }
        newbuf[i] = '\0';
        fclose(fp);

        newargv[0] = argv[0];
        newargc--;
        globus_l_gram_tokenize(newbuf, &newargv[1], &newargc);

        for (i=3; i<argc; i++)
            newargv[++newargc] = strdup(argv[i]);

        argv = newargv;
        argc = newargc + 1;
    }
    /* Default log level if nothing specified on command-line or config file */
    config->log_levels = -1;

    /*
     * Parse the command line arguments
     */
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-k") == 0)
        {
            config->kerberos = GLOBUS_TRUE;
        }
        else if ((strcmp(argv[i], "-home") == 0) && (i + 1 < argc))
        {
            config->globus_location = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-target-globus-location") == 0)
                 && (i + 1 < argc))
        {
            config->target_globus_location = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-type") == 0) && (i + 1 < argc))
        {
            config->jobmanager_type = strdup(argv[++i]);
        }
        else if((strcmp(argv[i], "-history") == 0) && (i + 1 < argc))
        {
            config->job_history_dir = strdup(argv[++i]);
        }
        else if (strcmp(argv[i], "-cache-location") == 0)
        {
            config->cache_location = strdup(argv[++i]);
        }
        else if (strcmp(argv[i], "-scratch-dir-base") == 0)
        {
            config->scratch_dir_base = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-condor-arch") == 0) && (i + 1 < argc))
        {
            config->condor_arch = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-condor-os") == 0) && (i + 1 < argc))
        {
            config->condor_os = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-globus-gatekeeper-host") == 0)
                 && (i + 1 < argc))
        {
            config->globus_gatekeeper_host = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-globus-gatekeeper-port") == 0)
                 && (i + 1 < argc))
        {
            config->globus_gatekeeper_port = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-globus-gatekeeper-subject") == 0)
                 && (i + 1 < argc))
        {
            config->globus_gatekeeper_subject = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-globus-host-manufacturer") == 0)
                 && (i + 1 < argc))
        {
            config->globus_host_manufacturer = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-globus-host-cputype") == 0)
                 && (i + 1 < argc))
        {
            config->globus_host_cputype = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-globus-host-osname") == 0)
                 && (i + 1 < argc))
        {
            config->globus_host_osname = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-globus-host-osversion") == 0)
                 && (i + 1 < argc))
        {
            config->globus_host_osversion = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-globus-tcp-port-range") == 0)
                 && (i + 1 < argc))
        {
            config->tcp_port_range = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-globus-tcp-source-range") == 0)
                 && (i + 1 < argc))
        {
            config->tcp_source_range = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-state-file-dir") == 0)
                 && (i + 1 < argc))
        {
            config->job_state_file_dir = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-x509-cert-dir") == 0)
                 && (i + 1 < argc))
        {
            config->x509_cert_dir = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-extra-envvars") == 0)
                 && (i + 1 < argc))
        {
            char * extra_envvars = strdup(argv[++i]);
            char *p, *q;

            p = extra_envvars;

            while (p && *p)
            {
                q = strchr(p, ',');

                if (q)
                {
                    *q = 0;
                }

                globus_list_insert(
                        &config->extra_envvars,
                        strdup(p));

                if (q)
                {
                    p = q+1;
                }
                else
                {
                    p = q;
                }
            }
            free(extra_envvars);
        }
        else if ((strcasecmp(argv[i], "-seg-module" ) == 0)
                 && (i + 1 < argc))
        {
            config->seg_module = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-audit-directory") == 0) 
                && (i+1 < argc))
        {
            globus_eval_path(argv[++i], &config->auditing_dir);
        }
        else if ((strcmp(argv[i], "-globus-toolkit-version") == 0)
                && (i+1 < argc))
        {
            config->globus_version = strdup(argv[++i]);
        }
        else if (strcmp(argv[i], "-disable-streaming") == 0)
        {
            /* Ignore this request, as we don't do streaming any more */
            config->streaming_disabled = GLOBUS_FALSE;
        }
        else if (strcmp(argv[i], "-service-tag") == 0
                && (i+1 < argc))
        {
            config->service_tag = strdup(argv[++i]);
        }
        else if (strcmp(argv[i], "-enable-syslog") == 0)
        {
            config->syslog_enabled = GLOBUS_TRUE;
        }
        else if (strcmp(argv[i], "-stdio-log") == 0
                && (i+1 < argc))
        {
            /* Backward-compatible definition of -stdio-log based on
             * -log-pattern implementation
             */
            config->log_pattern = globus_common_create_string(
                    "%s/gram_$(DATE).log",
                    argv[++i]);
        }
        else if (strcmp(argv[i], "-log-pattern") == 0
                && (i+1 < argc))
        {
            config->log_pattern = strdup(argv[++i]);
        }
        else if ((strcmp(argv[i], "-globus-job-dir") == 0)
                 && (i + 1 < argc))
        {
            char * tmp_user_name = getenv("LOGNAME");
	    config->job_dir_home =
                globus_common_create_string(
                    "%s/%s",
                    argv[++i],
                    tmp_user_name ? tmp_user_name : "USER");

            if (config->job_dir_home == NULL)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

                goto out;
            }
	    globus_gram_job_manager_log(
                NULL,
                GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
                "event=gram.config.info "
                "level=TRACE "
                "option=\"-globus-job-dir\" "
                "path=\"%s\" "
                "\n",
                config->job_dir_home);
        }
	else if (strcmp(argv[i], "-log-levels") == 0
                && (i+1 < argc))
        {
            rc = globus_i_gram_parse_log_levels(
                    argv[++i],
                    &config->log_levels,
                    NULL);
        }
        else if ((strcmp(argv[i], "-disable-usagestats") == 0) ||
                 (strcmp(argv[i], "-no-disable-usagestats") == 0))
        {
            globus_gram_job_manager_log(
                NULL,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.config level=WARN path=\"%s\" "
                "argument=%s reason=\"Usage statistics collection is no longer supported. Argument will be ignored.\"\n",
                conf_path ? conf_path : "ARGV",
                argv[i]);
        }
        else if (strcmp(argv[i], "-usagestats-targets") == 0
                && (i+1 < argc))
        {
            globus_gram_job_manager_log(
                NULL,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.config level=WARN path=\"%s\" "
                "argument=%s reason=\"Usage statistics collection is no longer supported. Argument will be ignored.\"\n",
                conf_path ? conf_path : "ARGV",
                argv[i++]);
        }
        else if (strcmp(argv[i], "-enable-callout") == 0)
        {
            config->enable_callout = GLOBUS_TRUE;
        }
        else if ((strcasecmp(argv[i], "-help" ) == 0) ||
                 (strcasecmp(argv[i], "--help") == 0))
        {
            fprintf(stderr,
                    "Usage: globus-gram-jobmanager\n"
                    "\n"
                    "Required Arguments:\n"
                    "\t-type jobmanager type, i.e. fork, lsf ...\n"
                    "\t-globus-host-manufacturer manufacturer\n"
                    "\t-globus-host-cputype cputype\n"
                    "\t-globus-host-osname osname\n"
                    "\t-globus-host-osversion osversion\n"
                    "\t-globus-gatekeeper-host host\n"
                    "\t-globus-gatekeeper-port port\n"
                    "\t-globus-gatekeeper-subject subject\n"
                    "\n"
                    "Options:\n"
                    "\t-home globus_location\n"
                    "\t-target-globus-location globus_location\n"
                    "\t-condor-arch arch, i.e. SUN4x\n"
                    "\t-condor-os os, i.e. SOLARIS26\n"
                    "\t-history job-history-directory\n" 
                    "\t-scratch-dir-base scratch-directory\n"
                    "\t-enable-syslog\n"
                    "\t-stdio-log DIRECTORY\n"
                    "\t-log-levels TRACE|INFO|DEBUG|WARN|ERROR|FATAL\n"
                    "\t-state-file-dir state-directory\n"
                    "\t-globus-tcp-port-range <min port #>,<max port #>\n"
                    "\t-globus-tcp-source-range <min port #>,<max port #>\n"
                    "\t-x509-cert-dir DIRECTORY\n"
                    "\t-cache-location PATH\n"
                    "\t-k\n"
                    "\t-extra-envvars VAR1,VAR2,...\n"
                    "\t-seg-module SEG-MODULE\n"
                    "\t-audit-directory DIRECTORY\n"
                    "\t-globus-toolkit-version VERSION\n"
                    "\t-enable-callout\n"
		    "\t-globus-job-dir DIRECTORY\n"
                    "\n"
                    "Note: if type=condor then\n"
                    "      -condor-os & -condor-arch are required.\n"
                    "\n");
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_GATEKEEPER_MISCONFIGURED;
            goto out;
        }
        else
        {
            globus_gram_job_manager_log(
                NULL,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.config level=ERROR path=\"%s\" "
                "argument=%s reason=\"Invalid command-line option\"\n",
                conf_path ? conf_path : "ARGV",
                argv[i] ? argv[i] : "");
        }
    }

    /* If log levels were not specified on the command-line or configuration, set the
     * service default
     */
    if (config->log_levels == -1)
    {
        config->log_levels = 0;
    }
    /* Always have these at a minimum */
    config->log_levels |= GLOBUS_GRAM_JOB_MANAGER_LOG_FATAL
                       |  GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR;

    /* Verify that required values are present */
    if(config->jobmanager_type == NULL)
    {
        globus_gram_job_manager_log(
            NULL,
            GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
            "event=gram.config level=ERROR path=\"%s\" argument=\"-type\" reason=\"Missing -type command-line option\"\n",
            conf_path ? conf_path : "ARGV");

        rc = GLOBUS_GRAM_PROTOCOL_ERROR_GATEKEEPER_MISCONFIGURED;
        goto out;
    }

    if(config->home == NULL)
    {
        config->home =  strdup(getenv("HOME"));
	if (config->home == NULL)
	    {
		rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

		goto out;
	    }
    }

    if (config->service_tag == NULL)
    {
        config->service_tag = strdup("untagged");
    }

    if (config->tcp_port_range == NULL)
    {
        char * ev = getenv("GLOBUS_TCP_PORT_RANGE");

        if (ev != NULL)
        {
            config->tcp_port_range = strdup(ev);
        }
    }
    if (config->tcp_source_range == NULL)
    {
        char * ev = getenv("GLOBUS_TCP_SOURCE_RANGE");

        if (ev != NULL)
        {
            config->tcp_source_range = strdup(ev);
        }
    }

    if (! globus_list_search_pred(
            config->extra_envvars, globus_l_env_present, "PATH"))
    {
        char * path = strdup("PATH");
        if (!path)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto out;
        }

        globus_list_insert(&config->extra_envvars, path);
    }

    /* Now initialize values from our environment */
    config->logname = strdup(getenv("LOGNAME"));
    if (config->logname == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto out;
    }

    if (config->globus_location == NULL)
    {
        rc = globus_location(&config->globus_location);
        if (rc != GLOBUS_SUCCESS)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto out;
        }
    }

    if (config->target_globus_location == NULL)
    {
        config->target_globus_location = strdup(config->globus_location);
        if (config->target_globus_location == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto out;
        }
    }
    if (config->job_state_file_dir == NULL)
    {
        rc = globus_eval_path("${localstatedir}/lib/globus/gram_job_state",
            &config->job_state_file_dir);

        if (rc != 0 || config->job_state_file_dir == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto out;
        }
    }

    if (config->job_dir_home == NULL)
    {
        config->job_dir_home = 
            globus_common_create_string("%s/%s",
                    config->job_state_file_dir,
                    config->logname);

        if (config->job_dir_home == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto out;
        }
    }

    if (config->scratch_dir_base == NULL)
    {
        config->scratch_dir_base = strdup(
                config->home);
        if (config->scratch_dir_base == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

            goto out;
        }
    }

    rc = globus_libc_gethostname(hostname, sizeof(hostname));
    if (rc != GLOBUS_SUCCESS)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto out;
    }

    config->hostname = strdup(hostname);
    if (config->hostname == GLOBUS_NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
        goto out;
    }

    config->short_hostname = strdup(hostname);
    dot = strchr(config->short_hostname, '.');
    if (dot != NULL)
    {
        *dot = 0;
    }

    rc = uname(&utsname);

    if (rc >= 0)
    {
        if (config->globus_host_osname == NULL)
        {
            config->globus_host_osname = strdup(utsname.sysname);
        }
        if (config->globus_host_osversion == NULL)
        {
            if (strcmp(utsname.sysname, "AIX") == 0)
            {
                config->globus_host_osversion = globus_common_create_string(
                    "%s.%s",
                    utsname.version,
                    utsname.release);
            }
            else
            {
                config->globus_host_osversion = globus_common_create_string(
                    "%s",
                    utsname.release);
            }
        }
    }
    gatekeeper_contact = getenv("GLOBUS_GATEKEEPER_CONTACT_STRING");

    if (gatekeeper_contact)
    {
	char *colon;
	char *save = strdup(gatekeeper_contact);

	gatekeeper_contact = save;

	if (gatekeeper_contact)
	{
	    colon = strchr(gatekeeper_contact, ':');
	    if (colon)
	    {
		if (!config->globus_gatekeeper_host)
		{
		    *colon = '\0';
		    config->globus_gatekeeper_host = strdup(gatekeeper_contact);
		}
		gatekeeper_contact = colon + 1;

		colon = strchr(gatekeeper_contact, ':');
		if (colon)
		{
		    if (!config->globus_gatekeeper_port)
		    {
			*colon = '\0';
			config->globus_gatekeeper_port =
					strdup(gatekeeper_contact);
		    }
		    gatekeeper_contact = colon + 1;

		    if (!config->globus_gatekeeper_subject)
		    {
			config->globus_gatekeeper_subject =
			    strdup(gatekeeper_contact);
		    }
		}
	    }
	}
	free(save);
    }

    rc = globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto out;
    }

    rc = globus_gram_job_manager_gsi_get_subject(&config->subject);
    if (rc != GLOBUS_SUCCESS)
    {
        goto out;
    }

    config->proxy_timeout = 10*60;

out:
    return rc;
}
/* globus_gram_job_manager_config_init() */

/**
 * Free all memory allocated when the globus_gram_job_manager_config_init() was called.
 *
 * @param config
 *     Configuration structure destroy.
 */
void
globus_gram_job_manager_config_destroy(
    globus_gram_job_manager_config_t *  config)
{
    if (config->globus_location)
    {
        free(config->globus_location);
    }
    if (config->target_globus_location)
    {
        free(config->target_globus_location);
    }
    if (config->jobmanager_type)
    {
        free(config->jobmanager_type);
    }
    if (config->job_history_dir)
    {
        free(config->job_history_dir);
    }
    if (config->cache_location)
    {
        free(config->cache_location);
    }
    if (config->scratch_dir_base)
    {
        free(config->scratch_dir_base);
    }
    if (config->condor_arch)
    {
        free(config->condor_arch);
    }
    if (config->condor_os)
    {
        free(config->condor_os);
    }
    if (config->globus_gatekeeper_host)
    {
        free(config->globus_gatekeeper_host);
    }
    if (config->globus_gatekeeper_port)
    {
        free(config->globus_gatekeeper_port);
    }
    if (config->globus_gatekeeper_subject)
    {
        free(config->globus_gatekeeper_subject);
    }
    if (config->globus_host_manufacturer)
    {
        free(config->globus_host_manufacturer);
    }
    if (config->globus_host_cputype)
    {
        free(config->globus_host_cputype);
    }
    if (config->globus_host_osname)
    {
        free(config->globus_host_osname);
    }
    if (config->globus_host_osversion)
    {
        free(config->globus_host_osversion);
    }
    if (config->tcp_port_range)
    {
        free(config->tcp_port_range);
    }
    if (config->tcp_source_range)
    {
        free(config->tcp_source_range);
    }
    if (config->job_state_file_dir)
    {
        free(config->job_state_file_dir);
    }
    if (config->x509_cert_dir)
    {
        free(config->x509_cert_dir);
    }
    if (config->extra_envvars)
    {
        globus_list_destroy_all(config->extra_envvars, free);
    }
    if (config->seg_module)
    {
        free(config->seg_module);
    }
    if (config->auditing_dir)
    {
        free(config->auditing_dir);
    }
    if (config->globus_version)
    {
        free(config->globus_version);
    }
    /*
    if (config->log_pattern)
    {
        free(config->log_pattern);
    }
    */
    if (config->subject)
    {
        free(config->subject);
    }
    if (config->home)
    {
        free(config->home);
    }
    if (config->logname)
    {
        free(config->logname);
    }
    if (config->hostname)
    {
        free(config->hostname);
    }
    if (config->short_hostname)
    {
        free(config->short_hostname);
    }
    if (config->service_tag)
    {
        free(config->service_tag);
    }
    if (config->job_dir_home)
    {
        free(config->job_dir_home);
    }
}
/* globus_gram_job_manager_config_destroy() */

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * Split string into command-line arguments
 *
 * The @a command string is split into whitespace-delimited strings and adds
 * them to the @a args array. If the string begins with single quote or double
 * quote it will be combined with following strings until the closing quote is
 * found. At most *n-1 arguments can be parsed from the @a command string. *n
 * will be modified to contain the number of tokens in the @a args array when
 * this function returns.
 * 
 * @param command
 *     String to tokenize
 * @param args
 *     Pointer to array of strings of at most *n elements.
 * @param n
 *     In: maximum number of lements in @a args. Out: actual number of 
 *     elements in @a args.
 *
 * @retval 0
 *     Success
 * @retval -1
 *     Too many arguments
 */
static
int
globus_l_gram_tokenize(char * command, char ** args, int * n)
{
  int i, x;
  char * cp;
  char * cp2;
  char ** arg;
  char * tmp_str = NULL;

  arg = args;
  i = *n - 1;

  for (cp = strtok(command, " \t\n"); cp != 0; )
  {
      if ( cp[0] == '\'' && cp[strlen(cp) - 1] != '\'' )
      {
         cp2 = strtok(NULL, "'\n");
         tmp_str = malloc((strlen(cp) + strlen(cp2) + 2));
         sprintf(tmp_str, "%s %s", &cp[1], cp2);
      }
      else if ( cp[0] == '"' && cp[strlen(cp) - 1] != '"' )
      {
         cp2 = strtok(NULL, "\"\n");
         tmp_str = malloc((strlen(cp) + strlen(cp2) + 2));
         sprintf(tmp_str, "%s %s", &cp[1], cp2);
      }
      else
      {
         if (( cp[0] == '"' && cp[strlen(cp) - 1] == '"' ) ||
             ( cp[0] == '\'' && cp[strlen(cp) - 1] == '\'' ))
         {
             tmp_str = malloc(strlen(cp));
             x = strlen(cp)-2;
             strncpy(tmp_str, &cp[1], x);
             tmp_str[x] = '\0';
         }
         else
         {
             tmp_str = cp;
         }
      }

      *arg = tmp_str;
      i--;
      if (i == 0)
          return(-1); /* too many args */
      arg++;
      cp = strtok(NULL, " \t\n");
  }

  *arg = (char *) 0;
  *n = *n - i - 1;
  return(0);

}
/* globus_l_gram_tokenize() */

/**
 * @brief Parse log level specification
 * 
 * @details
 *     The globus_i_gram_parse_log_levels() function parses the log level string passed
 *     in its first argument to the mask pointed to by  the second argument.
 *     The log level string contains one or more level names combined by the
 *     "|" delimiter. The set of valid level names is:
 *     - FATAL
 *     - ERROR
 *     - WARN
 *     - INFO
 *     - DEBUG
 *     - TRACE
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_GATEKEEPER_MISCONFIGURED
 *     Server misconfigured
 */
int
globus_i_gram_parse_log_levels(
    const char *                        unparsed_string,
    int *                               log_levels,
    char **                             error_string)
{
    char *                  log_level_string = strdup(unparsed_string);
    char *                  level_string = NULL;
    char *                  last_string = NULL;
    int                     rc = GLOBUS_SUCCESS;

    if (log_level_string == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

        goto error_exit;
    }
    if (error_string != NULL)
    {
        *error_string = NULL;
    }
    *log_levels = 0;

    for (level_string = strtok_r(log_level_string, "|", &last_string);
         level_string != NULL;
         level_string = strtok_r(NULL, "|", &last_string))
    {
        if (strcmp(level_string, "FATAL") == 0)
        {
            *log_levels |= GLOBUS_GRAM_JOB_MANAGER_LOG_FATAL;
        }
        else if (strcmp(level_string, "ERROR") == 0)
        {
            *log_levels |= GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR;
        }
        else if (strcmp(level_string, "WARN") == 0)
        {
            *log_levels |= GLOBUS_GRAM_JOB_MANAGER_LOG_WARN;
        }
        else if (strcmp(level_string, "INFO") == 0)
        {
            *log_levels |= GLOBUS_GRAM_JOB_MANAGER_LOG_INFO;
        }
        else if (strcmp(level_string, "DEBUG") == 0)
        {
            *log_levels |= GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG;
        }
        else if (strcmp(level_string, "TRACE") == 0)
        {
            *log_levels |= GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE;
        }
        else
        {
            globus_gram_job_manager_log(
                    NULL,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.config.info "
                    "level=ERROR "
                    "status=-1 "
                    "msg=\"%s\" "
                    "string=\"%s\" "
                    "error_at=\"%s\" "
                    "\n",
                    "Error parsing log level string",
                    unparsed_string,
                    level_string);

            rc = GLOBUS_GRAM_PROTOCOL_ERROR_GATEKEEPER_MISCONFIGURED;

            if (error_string != NULL)
            {
                *error_string = globus_common_create_string(
                        "Error parsing log level string '%s' at '%s'\n",
                        unparsed_string,
                        level_string);
            }
            break;
        }
    }
    free(log_level_string);

error_exit:
    return rc;
}
/* globus_i_gram_parse_log_levels() */

static
int
globus_l_env_present(
    void *                              datum,
    void *                              arg)
{
    char *datum_str = datum, *arg_str = arg;
    char *equal;
    size_t arglen;

    if (datum != arg && ((!datum) || (!arg)))
    {
        return 0;
    }

    arglen = strlen(arg_str);
    equal = strchr(datum_str, '=');

    if (equal && arglen > 0)
    {
        if ((equal - datum_str) == arglen)
        {
            return (strncmp(datum_str, arg_str, equal-datum_str) == 0);
        }
    }
    else
    {
        return (strcmp(datum_str, arg_str) == 0);
    }
    return 0;
}
/* globus_l_env_present() */

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
