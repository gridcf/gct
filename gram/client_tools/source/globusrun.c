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

/******************************************************************************
globusrun.c

Description:

CVS Information:

    $Source$
    $Date$
    $Revision$
    $Author$
******************************************************************************/

/******************************************************************************
                             Include header files
******************************************************************************/


#include "globus_common.h"

#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>

#include "globus_gram_client.h"
#include "globus_gram_protocol.h"
#include "globus_gass_server_ez.h"
#include "globus_rsl.h"

#include "globus_gss_assist.h"
#include "version.h" /* provides local_version */


/******************************************************************************
                               Type definitions
******************************************************************************/
typedef struct globus_i_globusrun_gram_monitor_s
{
    globus_bool_t                       done;
    globus_mutex_t                      mutex;
    globus_cond_t                       cond;

    globus_bool_t                       verbose;
    unsigned long                       job_state;
    int                                 submit_done;
    int                                 failure_code;
    char *                              failure_message;
    globus_url_t                        job_contact;
    char *                              job_contact_string;
    globus_list_t *                     premature_callbacks;
} globus_i_globusrun_gram_monitor_t;


static
globus_io_secure_delegation_mode_t  globus_l_delegation_mode =
        GLOBUS_IO_SECURE_DELEGATION_MODE_LIMITED_PROXY;

/*****************************************************************************
                          Module specific prototypes
*****************************************************************************/

/**** Creation of RSL Substitution variable for GASS url ****/
static int
globus_l_globusrun_rsl_gass_subst(globus_rsl_t *  request,
				  char *          globusrun_url_string);

/**** RSL Substitution of GASS redirection of stdout and stderr ****/
static int
globus_l_globusrun_rsl_output_subst(globus_rsl_t *  request,
				    char *          globusrun_url_string);

/**** RSL Substitution of (dryrun=yes) for -dryrun flag ****/
static int
globus_l_globusrun_rsl_dryrun_subst(globus_rsl_t *request);

/**** Run job using GRAM ****/
static int
globus_l_globusrun_gramrun(char *          request_string,
			   unsigned long   options,
			   char *          rm_contact);

static
int
globus_l_globusrun_refresh_proxy(
    char *			job_contact);
static
int
globus_l_globusrun_stop_manager(
    char *			job_contact);

static int
globus_l_globusrun_kill_job(char * job_contact);

static int
globus_l_globusrun_status_job(char * job_contact);

static
void
globus_l_submit_callback(
    void *                              user_callback_arg,
    const char *                        job_contact,
    globus_gram_client_job_info_t *     job_info);

static
void
globus_l_refresh_callback(
    void *                              user_callback_arg,
    globus_gram_protocol_error_t        operation_failure_code,
    const char *                        job_contact,
    globus_gram_protocol_job_state_t    job_state,
    globus_gram_protocol_error_t        job_failure_code);

static
void
globus_l_globusrun_sigint_handler(void * user_arg);

/*****************************************************************************
                          Module specific variables
*****************************************************************************/
enum
{
    GLOBUSRUN_ARG_QUIET                 = 2,
    GLOBUSRUN_ARG_DRYRUN                = 4,
    GLOBUSRUN_ARG_PARSE_ONLY            = 8,
    GLOBUSRUN_ARG_AUTHENTICATE_ONLY     = 16,
    GLOBUSRUN_ARG_USE_GASS              = 32,
    GLOBUSRUN_ARG_ALLOW_READS           = 64,
    GLOBUSRUN_ARG_ALLOW_WRITES          = 128,
    GLOBUSRUN_ARG_IGNORE_CTRLC          = 256,
    GLOBUSRUN_ARG_BATCH                 = 512,
    GLOBUSRUN_ARG_STATUS                = 1024,
    GLOBUSRUN_ARG_LIST                  = 2048,
    GLOBUSRUN_ARG_BATCH_FAST            = 4096,
    GLOBUSRUN_ARG_JOB_MANAGER_VERSION   = 8192
};

static char *  oneline_usage
   =  "globusrun [-help] [-f RSL file] [-s][-b][-d][...] [-r RM] [RSL]\n";

static char *  long_usage = 
"\n" 
"Syntax: globusrun [options] [RSL String]\n"
"        globusrun -version[s]\n"
"        globusrun -help\n"
"\n" 
"    Options\n"
"    -help | -usage\n"
"           Display help\n"
"    -version\n"
"           Display version\n"
"    -versions\n"
"           Display versions of all activated modules\n"
"    -f <rsl filename> | -file <rsl filename> \n"
"           Read RSL from the local file <rsl filename>. The RSL can\n"
"           be a single job request\n"
"    -q | -quiet\n"
"           Quiet mode (do not print diagnostic messages)\n"
"    -o | -output-enable\n"
"           Use the GASS Server library to redirect standout output\n"
"           and standard error to globusrun. Implies -quiet\n"
"    -s | -server\n"
"           $(GLOBUSRUN_GASS_URL) can be used to access files local\n"
"           to the submission machine via GASS. Implies -output-enable\n"
"           and -quiet\n"
"    -w | -write-allow\n"
"           Enable the GASS Server library and allow writing to\n"
"           GASS URLs. Implies -server and -quiet.\n"
"    -r <resource manager> | -resource  <resource manager> \n"
"           Submit the RSL job request to the specified resource manager.\n"
"           A resource manager can be specified in the following ways: \n\n"
"           host\n"
"           host:port\n"
"           host:port/service\n"
"           host/service\n"
"           host:/service\n"
"           host::subject\n"
"           host:port:subject\n"
"           host/service:subject\n"
"           host:/service:subject\n"
"           host:port/service:subject\n\n"
"           For those resource manager contacts which omit the port, \n"
"           service or subject field the following defaults are used:\n\n"
"           port = 2119 \n"
"           service = jobmanager \n"
"           subject = subject based on hostname\n\n"
"           This is a required argument when submitting a single RSL\n"
"           request\n"
"    -n | -no-interrupt\n"
"           Cause SIGINT to terminate globusrun, while leaving the\n"
"           submitted job to run to completion. By default the SIGINT\n"
"           signal will be trapped and the job will be terminated\n"
"    -k | -kill <job ID>\n"
"           Kill a disconnected globusrun job\n"
"    -status <job ID>\n"
"           Print the current status of the specified job.\n"
"    -b | -batch\n"
"           Cause globusrun to terminate after the job is successfully\n"
"           submitted to the scheduler. Useful for batch jobs.\n"
"           If used with -s, files may be staged in to the job, but stdout\n"
"           and stderr will not be redirected.\n"
"           The \"handle\" or job ID of the submitted job will be written on\n"
"           stdout.\n"
"    -F | -fast-batch\n"
"           Similar to -b but will exit as soon as the job has been sent\n"
"           to the GRAM job manager service without waiting for a callback\n"
"           with job submission state. Useful for hosts which are not able\n"
"           to receive job state callbacks.\n"
"    -full-proxy | -D\n"
"           Delegate a full proxy instead of a limited proxy.\n"
"    -refresh-proxy | -y <job ID>\n"
"           Cause globusrun to delegate a new proxy to the job named by the\n"
"           <job ID>\n"
"    -stop-manager <job  ID>\n"
"           Cause globusrun to stop the job manager, without killing the\n"
"           job. If the save_state RSL attribute is present, then a\n"
"           job manager can be restarted by using the restart RSL attribute.\n"
"\n"
"    Diagnostic Options\n"
"    -p | -parse\n"
"           Parse and validate the RSL only. Do not submit the job to\n"
"           a GRAM gatekeeper\n"
"    -a | -authenticate-only\n"
"           Submit a gatekeeper \"ping\" request only. Do not parse the\n"
"           RSL or submit the job request. Requires the -resource-manger \n"
"           argument\n"
"    -d | -dryrun\n"
"           Submit the RSL to the job manager as a \"dryrun\" test\n"
"           The request will be parsed and authenticated. The job manager\n"
"           will execute all of the preliminary operations, and stop\n"
"           just before the job request would be executed\n"
"    -j | -job-manager-version\n"
"           Display the version of the job manager running at a particular\n"
"           contact.\n"
"\n";

#define globusrun_l_args_error(a) \
{ \
    fprintf(stderr, \
			"\nERROR: " \
			a \
			"\n\nSyntax: %s\n" \
			"\nUse -help to display full usage\n", \
			oneline_usage); \
    globus_module_deactivate_all(); \
    exit(-1); \
}

#define globusrun_l_args_error_fmt(fmt,arg) \
{ \
    fprintf(stderr, \
			"\nERROR: " \
			fmt \
			"\n\nSyntax: %s\n" \
			"\nUse -help to display full usage\n", \
			arg, oneline_usage); \
    globus_module_deactivate_all(); \
    exit(-1); \
}



int
test_job_id( char *    value,
	     void *    ignored,
	     char **   errmsg )
{
    int res = (strncmp(value,"https://", strlen("https://")));
    if (res)
	*errmsg = globus_libc_strdup("invalid format of job ID");
    return res;
}

enum { arg_q = 1, arg_o, arg_s, arg_w, arg_n, arg_b,
	     arg_p, arg_d, arg_a,
	     arg_r, arg_f, arg_k, arg_y, arg_status,
	     arg_stop_manager,
	     arg_F, arg_j, arg_full_proxy,
	     arg_num = arg_full_proxy };

#define listname(x) x##_aliases
#define namedef(id,alias1,alias2) \
static char * listname(id)[] = { alias1, alias2, NULL }

#define defname(x) x##_definition
#define flagdef(id,alias1,alias2) \
namedef(id,alias1,alias2); \
static globus_args_option_descriptor_t defname(id) = { id, listname(id), 0, \
						NULL, NULL }
#define funcname(x) x##_predicate_test
#define paramsname(x) x##_predicate_params
#define oneargdef(id,alias1,alias2,testfunc,testparams) \
namedef(id,alias1,alias2); \
static globus_args_valid_predicate_t funcname(id)[] = { testfunc }; \
static void* paramsname(id)[] = { (void *) testparams }; \
globus_args_option_descriptor_t defname(id) = \
    { (int) id, (char **) listname(id), 1, funcname(id), (void **) paramsname(id) }

flagdef(arg_q, "-q", "-quiet");
flagdef(arg_o, "-o", "-output-enable");
flagdef(arg_s, "-s", "-server");
flagdef(arg_w, "-w", "-write-allow");
flagdef(arg_n, "-n", "-no-interrupt");
flagdef(arg_b, "-b", "-batch");
flagdef(arg_p, "-p", "-parse");
flagdef(arg_d, "-d", "-dryrun");
flagdef(arg_a, "-a", "-authenticate-only");
flagdef(arg_F, "-F", "-fast-batch");
flagdef(arg_j, "-j", "-job-manager-version");
flagdef(arg_full_proxy, "-D", "-full-proxy");

static int arg_f_mode = O_RDONLY;

    oneargdef(arg_f, "-f", "-file", globus_validate_filename, &arg_f_mode);
    oneargdef(arg_r, "-r", "-resource", NULL, NULL);
    oneargdef(arg_k, "-k", "-kill", test_job_id, NULL);
    oneargdef(arg_y, "-y", "-refresh-proxy", test_job_id, NULL);
    oneargdef(arg_stop_manager, "-stop-manager", NULL, test_job_id, NULL);
    oneargdef(arg_status, "-status", NULL, test_job_id, NULL);
    static globus_args_option_descriptor_t args_options[arg_num];

#define setupopt(id) args_options[id-1] = defname(id)

#define globusrun_i_args_init() \
	setupopt(arg_q); setupopt(arg_o); setupopt(arg_s); \
	setupopt(arg_w); setupopt(arg_n); setupopt(arg_b); \
	setupopt(arg_p); setupopt(arg_d); setupopt(arg_a); \
	setupopt(arg_r); setupopt(arg_f); setupopt(arg_k); setupopt(arg_y); \
	setupopt(arg_stop_manager); \
	setupopt(arg_status); \
	setupopt(arg_F); \
        setupopt(arg_j); \
	setupopt(arg_full_proxy);

static globus_bool_t globus_l_globusrun_ctrlc = GLOBUS_FALSE;
static globus_bool_t globus_l_globusrun_ctrlc_handled = GLOBUS_FALSE;

int
main(int argc, char* argv[])
{
    char *                             request_string    = NULL;
    char *                             request_file      = NULL;
    char *                             rm_contact        = NULL;
    char *                             program           = NULL;
    globus_rsl_t *                     request_ast       = NULL;
    globus_list_t *                    options_found     = NULL;
    globus_list_t *                    list              = NULL;
    globus_args_option_instance_t *    instance          = NULL;
    unsigned short                     gass_port         = 0;
    unsigned long                      options           = 0UL;
    int                                err               = GLOBUS_SUCCESS;
    globus_gass_transfer_listener_t   listener		 =0;
    globus_gass_transfer_listenerattr_t * attr		 =NULL;
    char *                             scheme		 =NULL;
    globus_gass_transfer_requestattr_t * reqattr	 =NULL;
    const char *                             activation_err  = NULL;

    err = globus_module_activate(GLOBUS_COMMON_MODULE);
    if ( err != GLOBUS_SUCCESS )
    {
        activation_err = "Error initializing globus\n";
    }

    if(activation_err == NULL)
    {
        err = globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE);
        if ( err != GLOBUS_SUCCESS )
        {
            activation_err = "Error initializing GSI GSS ASSIST\n";
            return 1;
        }
    }

    if(activation_err == NULL)
    {
        err = globus_module_activate(GLOBUS_GRAM_CLIENT_MODULE);
        if ( err != GLOBUS_SUCCESS)
        {
            activation_err = globus_gram_protocol_error_string(err);
        }
    }
    if(activation_err == NULL)
    {
        err = globus_module_activate(GLOBUS_GASS_SERVER_EZ_MODULE);
        if ( err != GLOBUS_SUCCESS )
        {
            activation_err = "Error initializing gass_server_ez\n";
        }
    }
    
    if (strrchr(argv[0],'/'))
        program = strrchr(argv[0],'/') + 1;
    else
        program = argv[0];

    globusrun_i_args_init();

    if ( 0 > (err = globus_args_scan( &argc,
                               &argv,
                               arg_num,
                               args_options,
                               "globusrun",
                               &local_version,
                               oneline_usage,
                               long_usage,
                               &options_found,
                               NULL   )) )  /* error on argument line */
    {
        globus_module_deactivate_all();
        exit(err == GLOBUS_FAILURE ? 1 : 0);
    }

    /* maximum one unflagged argument should remain: the RSL string */
    if (argc > 2)
    {
        globusrun_l_args_error("too many request strings specified");
    }
    
    if (argc > 1)
        request_string = globus_libc_strdup(argv[1]);

    if (activation_err != NULL)
    {
        fprintf(stderr, "%s", activation_err);

        exit(-2);
    }

    for (list = options_found;
         !globus_list_empty(list);
         list = globus_list_rest(list))
    {
        instance = globus_list_first(list);

        switch(instance->id_number)
        {
        case arg_w:
            options |= GLOBUSRUN_ARG_ALLOW_WRITES;
        case arg_s:
            options |= GLOBUSRUN_ARG_ALLOW_READS;
        case arg_o:
            options |= GLOBUSRUN_ARG_USE_GASS;
        case arg_q:
            options |= GLOBUSRUN_ARG_QUIET;
            break;

        case arg_n:
            options |= GLOBUSRUN_ARG_IGNORE_CTRLC;
            break;

        case arg_a:
            options |= GLOBUSRUN_ARG_AUTHENTICATE_ONLY;
            break;

        case arg_p:
            options |= GLOBUSRUN_ARG_PARSE_ONLY;
            break;

        case arg_b:
            options |= GLOBUSRUN_ARG_BATCH;
            break;

        case arg_d:
            options |= GLOBUSRUN_ARG_DRYRUN;
            break;

        case arg_F:
            options |= GLOBUSRUN_ARG_BATCH_FAST|GLOBUSRUN_ARG_BATCH;
            break;

        case arg_r:
            rm_contact=globus_libc_strdup(instance->values[0]);
            if(rm_contact == NULL)
            {
                globusrun_l_args_error_fmt("resolving resource manager %s",
                                           instance->values[0] );
            }
            break;

        case arg_f:
            request_file = globus_libc_strdup(instance->values[0]);
            break;

        case arg_k:
            return(globus_l_globusrun_kill_job(instance->values[0]));
            break;

        case arg_full_proxy:
            globus_l_delegation_mode =
                    GLOBUS_IO_SECURE_DELEGATION_MODE_FULL_PROXY;
            break;

        case arg_y:
            return globus_l_globusrun_refresh_proxy(instance->values[0]);
            break;

        case arg_stop_manager:
            return globus_l_globusrun_stop_manager(instance->values[0]);

        case arg_status:
            return(globus_l_globusrun_status_job(instance->values[0]));
            break;

        case arg_j:
            options |= GLOBUSRUN_ARG_JOB_MANAGER_VERSION;
            break;

        default:
            globusrun_l_args_error_fmt("parse panic, arg id = %d",
                                       instance->id_number );
            break;
        }
    }

    globus_args_option_instance_list_free( &options_found );

    if(options & GLOBUSRUN_ARG_AUTHENTICATE_ONLY)
    {
	if(!rm_contact)
	{
	    globusrun_l_args_error("no resource manager contact specified"
				   "for authentication test" );
	}
	err = globus_gram_client_ping(rm_contact);
	if(err == GLOBUS_SUCCESS)
	{
	    fprintf(stdout,
				"\nGRAM Authentication test successful\n");
	    return 0;
	}
	else
	{
	    fprintf(stdout,
				"\nGRAM Authentication test failure: %s\n",
				globus_gram_protocol_error_string(err));
	    return 1;
	}
    }  /* authentication test */
    else if (options & GLOBUSRUN_ARG_JOB_MANAGER_VERSION)
    {
        globus_hashtable_t              extensions;
        globus_gram_protocol_extension_t *
                                        extension;

	if(!rm_contact)
	{
	    globusrun_l_args_error("no resource manager contact specified"
				   "for version check" );
	}
	err = globus_gram_client_get_jobmanager_version(
                rm_contact,
                &extensions);
	if(err == GLOBUS_SUCCESS)
	{
            extension = globus_hashtable_lookup(&extensions, "toolkit-version");
            if (extension)
            {
                printf("Toolkit version: %s\n", extension->value);
            }

            extension = globus_hashtable_lookup(&extensions, "version");
            if (extension)
            {
                printf("Job Manager version: %s\n", extension->value);
            }

	    return 0;
	}
	else
	{
	    fprintf(stdout,
				"\nGRAM version check failed : %s\n",
				globus_gram_protocol_error_string(err));
	    return 1;
	}
    }

    if ( (request_string!=NULL)
	 && (request_file!=NULL) )
    {
	    globusrun_l_args_error("cannot specify both request string and "
				   "request file" );
    }

    if ( request_file != NULL)
    {
	int fd;

	fd = open(request_file, O_RDONLY);
	if ( fd >= 0 )
	{
	    int i;
	    char c;
            struct stat stbuf;
	    globus_off_t len = 0;

            if ((fstat(fd, &stbuf) == 0) && (S_ISREG(stbuf.st_mode)))
            {
                len = stbuf.st_size;
                request_string = malloc(sizeof (char) * (len + 1));
                if (request_string)
                {
                    i=0;

                    while ( (i<len)  && read(fd, &c, 1) > 0)
                    {
                        request_string[i] = c;
                        i++;
                    }
                    request_string[i] = '\0';

                }
                else
                {
                    globusrun_l_args_error_fmt("out of memory: %s",request_file);
                }
            }
            else
            {
                globusrun_l_args_error_fmt("not a regular file: %s", request_file);
            }
            free (request_file);
            request_file = NULL;
            close (fd);
	}
	else
	{
	    globusrun_l_args_error_fmt("cannot open request file: %s",
				       request_file);
	}
    }

    if ( (request_string==NULL)
	 && (request_file==NULL))
    {
	globusrun_l_args_error("must specify a request string or "
			       "request file");
    }

    request_ast = globus_rsl_parse(request_string);
    if(request_ast == NULL)
    {
	globusrun_l_args_error_fmt("cannot parse RSL %s",
				   request_string );
    }

    if(!globus_rsl_is_boolean(request_ast))
    {
	fprintf(stderr,
			    "%s Error: Bad RSL\n",
			    program);
	err=GLOBUS_GRAM_PROTOCOL_ERROR_BAD_RSL;
	return err;
    }

    if(options & GLOBUSRUN_ARG_PARSE_ONLY)
    {
	printf("RSL Parsed Successfully...\n");
	return 0;
    }

    if((!globus_rsl_is_boolean_multi(request_ast)) &&
       rm_contact == NULL)
    {
	globusrun_l_args_error("no resource manager contact");
    }

    if (options & GLOBUSRUN_ARG_USE_GASS)
    {
	/* transform the RSL to send "free" output streams to our
	 * stdout/stderr via gass, rather than to /dev/null */
	char *gass_server_url = NULL;
	unsigned long server_ez_opts=0UL;
	char *url_relation_string;
	char *relation_format="rsl_substitution=(GLOBUSRUN_GASS_URL %s)";

	server_ez_opts |=
	    GLOBUS_GASS_SERVER_EZ_LINE_BUFFER
	    | GLOBUS_GASS_SERVER_EZ_TILDE_EXPAND
	    | GLOBUS_GASS_SERVER_EZ_TILDE_USER_EXPAND;

	if ( !(options & GLOBUSRUN_ARG_BATCH) ) {
	    server_ez_opts |=
		GLOBUS_GASS_SERVER_EZ_STDOUT_ENABLE
		| GLOBUS_GASS_SERVER_EZ_STDERR_ENABLE;

	    if(options & GLOBUSRUN_ARG_ALLOW_WRITES)
	    {
		server_ez_opts |=
		    GLOBUS_GASS_SERVER_EZ_WRITE_ENABLE;
	    }
	}

	if(options & GLOBUSRUN_ARG_ALLOW_READS)
	{
	    server_ez_opts |=
		GLOBUS_GASS_SERVER_EZ_READ_ENABLE;
	}

	err = globus_gass_server_ez_init(&listener,
                                         attr,
                                         scheme,
                                         reqattr,
                                         server_ez_opts,
                                         NULL);

 	gass_server_url=globus_gass_transfer_listener_get_base_url(listener);

	if((err != GLOBUS_SUCCESS))
	{
	    fprintf(stderr,
				"%s Error: initializing GASS (%d)\n",
				program,
				err);
	    goto hard_exit;
	}

	url_relation_string = globus_malloc(strlen(relation_format) +
					    strlen(gass_server_url));

	if(url_relation_string == NULL)
	{
	    err=GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;

	    fprintf(stderr,
				"%s Error: Malloc failed\n",
				program);
	    goto hard_exit;
	}


	globus_libc_lock();
	sprintf(url_relation_string,
		relation_format,
		gass_server_url);
	globus_libc_unlock();

	/* replace stdout and stderr in request if not in batch mode*/
	if ( !(options & GLOBUSRUN_ARG_BATCH) ) {
	    globus_l_globusrun_rsl_output_subst(request_ast,
						url_relation_string);
	}

	globus_l_globusrun_rsl_gass_subst(request_ast,
					  url_relation_string);

	globus_free (url_relation_string);

	gass_server_url = NULL;
    }
    if(options & GLOBUSRUN_ARG_DRYRUN)
    {
	globus_l_globusrun_rsl_dryrun_subst(request_ast);
    }
    if(request_string)
    {
	globus_free(request_string);
    }
    request_string = globus_rsl_unparse(request_ast);

    if(globus_rsl_is_boolean_multi(request_ast))
    {
        fprintf(stderr, "Multi-requests not supported with this version of globusrun\n");
        err = EXIT_FAILURE;
    }
    else
    {
	char *req;

	req = globus_rsl_unparse(request_ast);

	err = globus_l_globusrun_gramrun(req,
					 options,
					 rm_contact);
	globus_free(req);
    }

hard_exit:

    if ( (options & GLOBUSRUN_ARG_USE_GASS) &&
	 (gass_port != 0U))
    {
	globus_gass_server_ez_shutdown (listener);
    }

    globus_free (request_string);

    globus_module_deactivate_all();

    return err;
} /* main() */

/******************************************************************************
Function: globus_l_globusrun_new_subst()

Description:

Parameters:

Returns:
******************************************************************************/
static globus_rsl_t *
globus_l_globusrun_new_subst(char *var)
{
    globus_rsl_t *relation;
    char *relation_format = "%s=$(GLOBUSRUN_GASS_URL)/dev/%s";
    char *val;

    val = globus_malloc((2 * strlen(var)) +
			strlen(relation_format));

    globus_libc_lock();
    sprintf(val, relation_format, var, var);
    globus_libc_unlock();

    relation = globus_rsl_parse(val);

    globus_free(val);

    return relation;
} /* globus_l_globusrun_new_subst() */

/******************************************************************************
Function: globus_l_globusrun_var_exists()

Description:

Parameters:

Returns:
******************************************************************************/
static globus_bool_t
globus_l_globusrun_var_exists(globus_rsl_t *subrequest,
			      char *val)
{
    if(globus_rsl_is_relation_eq(subrequest))
    {
	if(strcmp(globus_rsl_relation_get_attribute(subrequest), val) == 0)
	{
	    return GLOBUS_TRUE;
	}
    }
    return GLOBUS_FALSE;
} /* globus_l_globusrun_var_exists() */

/******************************************************************************
Function: globus_l_globusrun_rsl_dryrun_subst()

Description:

Parameters:

Returns:
******************************************************************************/
static int
globus_l_globusrun_rsl_dryrun_subst(globus_rsl_t *request)
{
    int level = 0;

    /* for multirequests */
    if(globus_rsl_is_boolean_multi(request))
    {
	globus_list_t *lists;

	lists = (globus_list_t *) globus_rsl_boolean_get_operand_list(request);

	while(!globus_list_empty(lists))
	{
	    level += globus_l_globusrun_rsl_dryrun_subst(
		globus_list_first(lists));
	    lists=globus_list_rest(lists);
	}
    }
    else if(globus_rsl_is_boolean(request))
    {
	globus_list_t *l;
	globus_bool_t dryrun_exists = GLOBUS_FALSE;

	l = globus_rsl_boolean_get_operand_list(request);
	while(!globus_list_empty(l))
	{

	    if(globus_rsl_is_boolean(globus_list_first(l)))
	    {
		level += globus_l_globusrun_rsl_dryrun_subst(
		    globus_list_first(l));
	    }

	    if(globus_l_globusrun_var_exists(globus_list_first(l),
						    "dryrun"))
	    {
		dryrun_exists = GLOBUS_TRUE;
	    }

	    l = globus_list_rest(l);
	}

	if(level == 0)
	{
	    if(!dryrun_exists)
	    {
		globus_list_insert(
		    globus_rsl_boolean_get_operand_list_ref(request),
		    globus_rsl_parse("dryrun = yes"));

	    }
	    level++;
	}
    }
    return level;
} /* globus_l_globusrun_rsl_dryrun_subst() */

/******************************************************************************
Function: globus_l_globusrun_rsl_gass_subst()

Description:

Parameters:

Returns:
******************************************************************************/
static int
globus_l_globusrun_rsl_gass_subst(globus_rsl_t *request,
				    char *globusrun_gass_url_string)
{
    int level = 0;

    /* for multirequests */
    if(globus_rsl_is_boolean_multi(request))
    {
	globus_list_t *lists;

	lists = (globus_list_t *) globus_rsl_boolean_get_operand_list(request);

	while(!globus_list_empty(lists))
	{
	    level += globus_l_globusrun_rsl_gass_subst(
		globus_list_first(lists),
		globusrun_gass_url_string);
	    lists=globus_list_rest(lists);
	}
    }
    else if(globus_rsl_is_boolean(request))
    {
	globus_list_t *l;

	l = globus_rsl_boolean_get_operand_list(request);
	while(!globus_list_empty(l))
	{

	    if(globus_rsl_is_boolean(globus_list_first(l)))
	    {
		level += globus_l_globusrun_rsl_gass_subst(
		    globus_list_first(l),
		    globusrun_gass_url_string);
	    }

	    l = globus_list_rest(l);
	}

	if(level == 0)
	{
	    globus_list_insert(
		globus_rsl_boolean_get_operand_list_ref(request),
		globus_rsl_parse(globus_libc_strdup(globusrun_gass_url_string)));
	    level++;
	}
    }
    return level;
} /* globus_l_globusrun_rsl_gass_subst() */

/******************************************************************************
Function: globus_l_globusrun_rsl_output_subst()

Description:

Parameters:

Returns:
******************************************************************************/
static int
globus_l_globusrun_rsl_output_subst(globus_rsl_t *request,
				    char *globusrun_gass_url_string)
{
    int level = 0;

    /* for multirequests */
    if(globus_rsl_is_boolean_multi(request))
    {
	globus_list_t *lists;

	lists = (globus_list_t *) globus_rsl_boolean_get_operand_list(request);

	while(!globus_list_empty(lists))
	{
	    level += globus_l_globusrun_rsl_output_subst(
		globus_list_first(lists),
		globusrun_gass_url_string);
	    lists=globus_list_rest(lists);
	}
    }
    else if(globus_rsl_is_boolean(request))
    {
	globus_list_t *l;
	globus_bool_t stdout_exists = GLOBUS_FALSE;
	globus_bool_t stderr_exists = GLOBUS_FALSE;

	l = globus_rsl_boolean_get_operand_list(request);
	while(!globus_list_empty(l))
	{

	    if(globus_rsl_is_boolean(globus_list_first(l)))
	    {
		level += globus_l_globusrun_rsl_output_subst(
		    globus_list_first(l),
		    globusrun_gass_url_string);
	    }

	    if(globus_l_globusrun_var_exists(globus_list_first(l),
						    "stdout"))
	    {
		stdout_exists=GLOBUS_TRUE;
	    }
	    if(globus_l_globusrun_var_exists(globus_list_first(l),
						    "stderr"))
	    {
		stderr_exists=GLOBUS_TRUE;
	    }

	    l = globus_list_rest(l);
	}

	if(level == 0)
	{
	    if(!stdout_exists)
	    {
		globus_list_insert(
		    globus_rsl_boolean_get_operand_list_ref(request),
		    globus_l_globusrun_new_subst("stdout"));
	    }
	    if(!stderr_exists)
	    {
		globus_list_insert(
		    globus_rsl_boolean_get_operand_list_ref(request),
		    globus_l_globusrun_new_subst("stderr"));
	    }
	    level++;
	}
    }
    return level;
} /* globus_l_globusrun_rsl_output_subst() */

/******************************************************************************
Function: globus_l_globusrun_gram_callback_func()

Description:

Parameters:

Returns:
******************************************************************************/
static
void
globus_l_globusrun_gram_callback_func(
    void *                              user_arg,
    const char *                        job_contact,
    globus_gram_client_job_info_t *     job_info)
{
    globus_i_globusrun_gram_monitor_t * monitor;
    globus_url_t                        job_contact_url;
    globus_gram_protocol_extension_t *  entry;
    int                                 rc;

    monitor = (globus_i_globusrun_gram_monitor_t *) user_arg;

    globus_mutex_lock(&monitor->mutex);
    if (!monitor->job_contact_string)
    {
        globus_gram_client_job_info_t * tmp = malloc(sizeof(globus_gram_client_job_info_t));
        globus_gram_protocol_extension_t * ext;

        if (job_info->extensions)
        {
            rc = globus_hashtable_init(
                &tmp->extensions,
                3,
                globus_hashtable_string_hash,
                globus_hashtable_string_keyeq);

            for (entry = globus_hashtable_first(&job_info->extensions);
                 entry != NULL;
                 entry = globus_hashtable_next(&job_info->extensions))
            {
                ext = malloc(sizeof(globus_gram_protocol_extension_t));
                ext->attribute = strdup(entry->attribute);
                ext->value = strdup(entry->value);

                globus_hashtable_insert(&tmp->extensions, ext->attribute, ext);
            }
        }
        else
        {
            tmp->extensions = NULL;
        }

        tmp->job_contact = strdup(job_contact);
        tmp->job_state = job_info->job_state;
        tmp->protocol_error_code = job_info->protocol_error_code;
        globus_list_insert(&monitor->premature_callbacks, tmp);
        globus_mutex_unlock(&monitor->mutex);
        return;
    }

    if (strcmp(monitor->job_contact_string, job_contact) != 0)
    {
        rc = globus_url_parse(job_contact, &job_contact_url);
        if (rc != GLOBUS_SUCCESS)
        {
            if (monitor->verbose)
            {
                fprintf(stderr, "Error parsing job contact: %s\n", job_contact);
            }
            globus_mutex_unlock(&monitor->mutex);
            return;
        }

        if(strcmp(monitor->job_contact.url_path, job_contact_url.url_path) == 0)
        {
            if (monitor->verbose)
            {
                fprintf(stderr, "Job moved to new contact: %s\n", job_contact);
            }
            /* Job has moved */
            free(monitor->job_contact_string);
            monitor->job_contact_string = strdup(job_contact);
            globus_url_destroy(&monitor->job_contact);
            memcpy(
                    &monitor->job_contact,
                    &job_contact_url,
                    sizeof(globus_url_t));
        }
        else
        {
            globus_url_destroy(&job_contact_url);
            globus_mutex_unlock(&monitor->mutex);
            return;
        }
    }

    monitor->job_state = job_info->job_state;

    switch (job_info->job_state)
    {
    case GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING:
	if(monitor->verbose)
	{
	    printf("GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING\n");
	}
	break;
    case GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_IN:
	if(monitor->verbose)
	{
	    printf("GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_IN\n");
	}
	break;
    case GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_OUT:
	if(monitor->verbose)
	{
	    printf("GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_OUT\n");
	}
	break;
    case GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE:
	if(monitor->verbose)
	{
	    printf("GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE\n");
	}
	break;
    case GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED:
	if(monitor->verbose)
	{
	    printf("GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED\n");
	}
        monitor->done = GLOBUS_TRUE;
	monitor->failure_code = job_info->protocol_error_code;
        if (job_info->extensions)
        {
            entry = globus_hashtable_lookup(
                    &job_info->extensions,
                    "gt3-failure-message");

            if (entry != NULL)
            {
                monitor->failure_message = strdup(entry->value);
            }
        }
	break;
    case GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE:
	if(monitor->verbose)
	{
	    printf("GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE\n");
            if (job_info->extensions)
            {
                entry = globus_hashtable_lookup(
                        &job_info->extensions,
                        "exit-code");

                if (entry != NULL)
                {
                    printf("exit code: %s\n", entry->value);
                }
            }
	}
        monitor->done = GLOBUS_TRUE;
	break;
    case GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED:
	if(monitor->verbose)
	{
	    printf("GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED\n");
	}
        monitor->done = GLOBUS_TRUE;
	break;
    }

    globus_cond_signal(&monitor->cond);
    globus_mutex_unlock(&monitor->mutex);
} /* globus_l_globusrun_gram_callback_func() */

static
void
globus_l_resend_premature_callback(
    void *                              arg)
{
    globus_i_globusrun_gram_monitor_t * monitor = arg;

    globus_mutex_lock(&monitor->mutex);

    while (!globus_list_empty(monitor->premature_callbacks))
    {
        globus_gram_client_job_info_t *     info;
        info = globus_list_remove(
                &monitor->premature_callbacks,
                monitor->premature_callbacks);

        globus_mutex_unlock(&monitor->mutex);
        globus_l_globusrun_gram_callback_func(
            monitor,
            info->job_contact,
            info);
        globus_mutex_lock(&monitor->mutex);

        globus_gram_protocol_hash_destroy(&info->extensions);
        free(info->job_contact);
        free(info);
    }
    globus_mutex_unlock(&monitor->mutex);
}
/* globus_l_resend_premature_callback() */

/******************************************************************************
Function: globus_l_globusrun_gramrun()

Description:

Parameters:

Returns:
******************************************************************************/
static int
globus_l_globusrun_gramrun(char * request_string,
			   unsigned long options,
			   char *rm_contact)
{
    char *callback_contact = NULL;
    globus_i_globusrun_gram_monitor_t monitor = {0};
    int err;
    globus_bool_t verbose = !(options & GLOBUSRUN_ARG_QUIET);
    globus_bool_t send_commit = GLOBUS_FALSE;
    int tmp1, tmp2;
    globus_gram_client_attr_t attr = NULL;

    if (globus_l_delegation_mode !=
                GLOBUS_IO_SECURE_DELEGATION_MODE_LIMITED_PROXY)
    {
        err = globus_gram_client_attr_init(&attr);
        if (err != GLOBUS_SUCCESS)
        {

            fprintf(stderr,
                   "Error initialized attribute %s (errorcode %d)\n",
                                globus_gram_protocol_error_string(err),
                                err);
            goto hard_exit;
        }
        err = globus_gram_client_attr_set_delegation_mode(
                attr,
                globus_l_delegation_mode);
        if (err != GLOBUS_SUCCESS)
        {

            fprintf(stderr,
               "Error setting delegation mode attribute: %s (errorcode %d)\n",
               globus_gram_protocol_error_string(err),
               err);
            goto hard_exit;
        }
    }

    monitor.done = GLOBUS_FALSE;
    monitor.failure_code = 0;
    monitor.verbose=verbose;
    monitor.job_state = 0;
    memset(&monitor.job_contact, 0, sizeof(globus_url_t));
    monitor.job_contact_string = NULL;
    monitor.submit_done = GLOBUS_FALSE;
    monitor.failure_message = NULL;
    globus_mutex_init(&monitor.mutex, NULL);
    globus_cond_init(&monitor.cond, NULL);

    /* trap SIGINTs */
    if(!(options & GLOBUSRUN_ARG_IGNORE_CTRLC))
    {
        globus_callback_register_signal_handler(
            SIGINT,
            GLOBUS_FALSE,
            globus_l_globusrun_sigint_handler,
            &monitor);
    }

    err = globus_gram_client_info_callback_allow(
        globus_l_globusrun_gram_callback_func,
        (void *) &monitor,
        &callback_contact);

    if(err != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
                            "Initializing GRAM Callback failed because %s (errorcode %d)\n",
                            globus_gram_protocol_error_string(err),
                            err);

        goto hard_exit;
    }
    else if(verbose)
    {
        printf("globus_gram_client_callback_allow "
                           "successful\n");
    }

    globus_mutex_lock(&monitor.mutex);
    err = globus_gram_client_register_job_request_with_info(
            rm_contact,
            request_string,
            GLOBUS_GRAM_PROTOCOL_JOB_STATE_ALL,
            callback_contact,
            attr,
            globus_l_submit_callback,
            &monitor);
    if (err != GLOBUS_SUCCESS)
    {
	if(callback_contact)
	{
	    globus_gram_client_callback_disallow(callback_contact);
	    globus_free(callback_contact);
	}
        fprintf(stderr,
                            "GRAM Job submission failed because %s (error code %d)\n",
                            globus_gram_protocol_error_string(err),
                            err);
	goto hard_exit;
    }

    while (!monitor.submit_done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
        err = monitor.failure_code;
    }
    globus_mutex_unlock(&monitor.mutex);

    if(err == GLOBUS_GRAM_PROTOCOL_ERROR_WAITING_FOR_COMMIT)
    {
	send_commit = GLOBUS_TRUE;
	err = globus_gram_client_job_signal(monitor.job_contact_string,
				GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_COMMIT_REQUEST,
					    "commit",
					    &tmp1,
					    &tmp2);
    }

    if(err != GLOBUS_SUCCESS)
    {
	if(callback_contact)
	{
	    globus_gram_client_callback_disallow(callback_contact);
	    globus_free(callback_contact);
	}

	if((err == GLOBUS_GRAM_PROTOCOL_ERROR_DRYRUN) &&
	   (options & GLOBUSRUN_ARG_DRYRUN))
	{
	    if(verbose)
	    {
		fprintf(stderr,
				    "Dryrun successful\n");
	    }
	    err=0;
	}
	else
	{
	    fprintf(stderr,
				"GRAM Job submission failed because %s (error code %d)\n",
				monitor.failure_message
                                    ? monitor.failure_message
                                    : globus_gram_protocol_error_string(err),
				err);

	}

	if  ((options & GLOBUSRUN_ARG_BATCH) && monitor.job_contact_string)
	    printf("%s\n",monitor.job_contact_string);

	goto hard_exit;
    }
    else if(verbose)
    {
	printf("GRAM Job submission successful\n");
    }

    if  (options & GLOBUSRUN_ARG_BATCH)
    {
	printf("%s\n",monitor.job_contact_string);
    }

    globus_mutex_lock(&monitor.mutex);

    /* If we're running in fast batch mode, and don't need to allow GASS
     * reads for staging, we will exit immediately without waiting for any
     * job state callbacks.
     */
    if((options &
            (GLOBUSRUN_ARG_BATCH | GLOBUSRUN_ARG_ALLOW_READS
                | GLOBUSRUN_ARG_BATCH_FAST))
        == (GLOBUSRUN_ARG_BATCH|GLOBUSRUN_ARG_BATCH_FAST))
    {
        monitor.done = GLOBUS_TRUE;
    }

    if (monitor.premature_callbacks)
    {
        globus_callback_register_oneshot(
                NULL,
                NULL,
                globus_l_resend_premature_callback,
                &monitor);
    }
    while(!monitor.done)
    {
        /* If we're running in batch mode and need to allow for GASS reads
         * we have to wait until the job is submitted and finished staging
         */
        if ((options & GLOBUSRUN_ARG_BATCH) &&
            (monitor.job_state != 0 &&
             monitor.job_state != GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED
                    &&
             monitor.job_state != GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_IN))
        {
            monitor.done = GLOBUS_TRUE;
            continue;
        }
	globus_cond_wait(&monitor.cond, &monitor.mutex);
	if(globus_l_globusrun_ctrlc && (!globus_l_globusrun_ctrlc_handled))
	{
	    if(verbose)
	    {
		printf("Cancelling job...\n");
	    }
	    globus_gram_client_job_cancel(monitor.job_contact_string);
	    globus_l_globusrun_ctrlc_handled = GLOBUS_TRUE;
	}
    }
    globus_mutex_unlock(&monitor.mutex);
    
    if(!(options & GLOBUSRUN_ARG_IGNORE_CTRLC))
    {
        globus_callback_unregister_signal_handler(
            SIGINT,
            NULL,
            NULL);
    }

    /* If we're using two phase commits then we need to send commit end
     * signal if the job is DONE
     */
    err = GLOBUS_SUCCESS;
    if (monitor.job_state == GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE ||
        monitor.job_state == GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED)
    {
        if(send_commit == GLOBUS_TRUE)
        {
            err = globus_gram_client_job_signal(monitor.job_contact_string,
                    GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_COMMIT_END,
                    "commit",
                    &tmp1,
                    &tmp2);
        }
        if (monitor.job_state == GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED)
        {
            err = monitor.failure_code;
        }
    }
    else
    {
        err = monitor.failure_code;
    }

    if (options & GLOBUSRUN_ARG_BATCH)
    {
        globus_gram_client_job_callback_unregister(
                monitor.job_contact_string,
                callback_contact,
                &tmp1,
                &tmp2);
    }

    globus_gram_client_callback_disallow(callback_contact);
    globus_free(callback_contact);

    globus_mutex_destroy(&monitor.mutex);
    globus_cond_destroy(&monitor.cond);

    if((err == GLOBUS_GRAM_PROTOCOL_ERROR_DRYRUN) &&
             (options & GLOBUSRUN_ARG_DRYRUN))
    {
        if(verbose)
        {
            fprintf(stderr,
                                "Dryrun successful\n");
        }
    }
    else if(err != GLOBUS_SUCCESS)
    {
        fprintf(stderr,
                            "GRAM Job failed because %s (error code %d)\n",
                            globus_gram_protocol_error_string(err),
                            err);
        if (monitor.failure_message != NULL)
        {
            fprintf(stderr, "Details: %s\n", monitor.failure_message);
        }
    }
hard_exit:

    if(monitor.job_contact_string != NULL)
    {
	globus_gram_client_job_contact_free(monitor.job_contact_string);
    }
    globus_url_destroy(&monitor.job_contact);

    return err;
} /* globus_l_globusrun_gramrun() */

/******************************************************************************
Function: globus_l_globusrun_kill_job()

Description:

Parameters:

Returns:
******************************************************************************/
static int
globus_l_globusrun_kill_job(char * job_contact)
{

    int err;

    err = globus_gram_client_job_cancel(job_contact);
    if ( err != GLOBUS_SUCCESS )
    {
	fprintf(stderr, "Error canceling job\n");
    }

    return err;
}

static
int
globus_l_globusrun_refresh_proxy(
    char *			job_contact)
{
    int err;
    globus_i_globusrun_gram_monitor_t monitor;
    globus_gram_client_attr_t attr = NULL;
    
    monitor.done = GLOBUS_FALSE;
    monitor.failure_code = 0;
    monitor.verbose = GLOBUS_FALSE;
    monitor.job_state = 0;
    monitor.job_contact_string = NULL;
    monitor.submit_done = GLOBUS_FALSE;
    globus_mutex_init(&monitor.mutex, NULL);
    globus_cond_init(&monitor.cond, NULL);

    if (globus_l_delegation_mode !=
                GLOBUS_IO_SECURE_DELEGATION_MODE_LIMITED_PROXY)
    {
        err = globus_gram_client_attr_init(&attr);
        if (err != GLOBUS_SUCCESS)
        {

            fprintf(stderr,
                   "Error initialized attribute %s (errorcode %d)\n",
                                globus_gram_protocol_error_string(err),
                                err);
            goto hard_exit;
        }
        err = globus_gram_client_attr_set_delegation_mode(
                attr,
                globus_l_delegation_mode);
        if (err != GLOBUS_SUCCESS)
        {

            fprintf(stderr,
               "Error setting delegation mode attribute: %s (errorcode %d)\n",
               globus_gram_protocol_error_string(err),
               err);
            goto hard_exit;
        }
    }

    err = globus_gram_client_register_job_refresh_credentials(
            job_contact,
            GSS_C_NO_CREDENTIAL,
            attr,
            globus_l_refresh_callback,
            &monitor);

    if ( err != GLOBUS_SUCCESS )
    {
	fprintf(stderr, "Error refreshing proxy: %s\n",
		            globus_gram_client_error_string(err));
    }
    while (!monitor.submit_done)
    {
        globus_cond_wait(&monitor.cond, &monitor.mutex);
        err = monitor.failure_code;
    }
    globus_mutex_unlock(&monitor.mutex);
    globus_mutex_destroy(&monitor.mutex);
    globus_cond_destroy(&monitor.cond);

hard_exit:
    return err;
}

static
int
globus_l_globusrun_stop_manager(
    char *			job_contact)
{
    int err;
    int tmp1,tmp2;

    err = globus_gram_client_job_signal(
            job_contact,
	    GLOBUS_GRAM_PROTOCOL_JOB_SIGNAL_STOP_MANAGER,
	    NULL,
	    &tmp1,
	    &tmp2);

    if(err != GLOBUS_SUCCESS)
    {
	fprintf(stderr, "Error stopping job manager: %s\n",
		            globus_gram_client_error_string(err));
    }
    return err;
}
/******************************************************************************
Function: globus_l_globusrun_status_job()

Description:

Parameters:

Returns:
******************************************************************************/
static
int
globus_l_globusrun_status_job(
    char *                              job_contact)
{
    int                                 failure_code;
    int                                 err;
    globus_gram_client_job_info_t       info = {0};
    globus_gram_protocol_extension_t *  entry;

    err = globus_gram_client_job_status_with_info(job_contact, &info);
    failure_code = info.protocol_error_code;

    if (err != GLOBUS_SUCCESS)
    {
        /* In GRAM2, if we could not connect to the job manager, we assumed
         * it had terminated and that the job has completed. 
         *
         * In GRAM5, we might be able to contact a job manager, but it might
         * have no more information about a particular job that terminated.
         * In that case, we treat it the same as the above case. We
         * check that we ahve the version extension to determine whether this
         * is a GRAM2 or GRAM5 service.
         */
	if (failure_code == GLOBUS_GRAM_PROTOCOL_ERROR_CONTACTING_JOB_MANAGER ||
            (info.extensions != NULL &&
            (globus_hashtable_lookup(&info.extensions, "version") != 0 &&
            failure_code == GLOBUS_GRAM_PROTOCOL_ERROR_JOB_CONTACT_NOT_FOUND)))
	{
            err = GLOBUS_SUCCESS;
	    printf("DONE\n");
	}
	else
	{
	    printf("ERROR\n");
	    fprintf(stderr,
                 "GRAM Job status failed because %s (error code %d)\n",
                 globus_gram_protocol_error_string(failure_code),
                 info.protocol_error_code);
            globus_gram_client_job_info_destroy(&info);
	    return failure_code;
	}
    }
    else
    {
	switch (info.job_state)
	{
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING:
	    printf("PENDING\n");
	    break;
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE:
	    printf("ACTIVE\n");
	    break;
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED:
	    printf("FAILED\n");
	    break;
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_SUSPENDED:
	    printf("SUSPENDED\n");
	    break;
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE:
	    printf("DONE\n");
            if (info.extensions)
            {
                entry = globus_hashtable_lookup(
                        &info.extensions,
                        "exit-code");

                if (entry != NULL)
                {
                    printf("exit code: %s\n", entry->value);
                }
            }
	    break;
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED:
	    printf("UNSUBMITTED\n");
	    break;
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_IN:
	    printf("STAGE_IN\n");
	    break;
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_OUT:
	    printf("STAGE_OUT\n");
	    break;
	default:
	    printf("UNKNOWN JOB STATE %d\n", info.job_state);
	    break;
	}
    }

    return err;
} /* globus_l_globusrun_status_job() */

static 
void
globus_l_globusrun_sigint_handler(void * user_arg)
{
    globus_i_globusrun_gram_monitor_t * monitor = user_arg;
    globus_mutex_lock(&monitor->mutex);
    globus_l_globusrun_ctrlc = GLOBUS_TRUE;
    globus_cond_signal(&monitor->cond);
    globus_mutex_unlock(&monitor->mutex);

} /* globus_l_globusrun_sigint_handler() */

static
void
globus_l_submit_callback(
    void *                              user_callback_arg,
    const char *                        job_contact,
    globus_gram_client_job_info_t *     info)
{
    globus_i_globusrun_gram_monitor_t * monitor = user_callback_arg;
    globus_gram_protocol_extension_t *  entry = NULL;

    globus_mutex_lock(&monitor->mutex);
    monitor->submit_done = GLOBUS_TRUE;
    if (job_contact)
    {
        monitor->job_contact_string = strdup(job_contact);
        globus_url_parse(monitor->job_contact_string, &monitor->job_contact);
    }
    if (info->protocol_error_code != GLOBUS_SUCCESS)
    {
        const char * err = globus_gram_protocol_error_string(
                info->protocol_error_code);

        if (info->extensions != NULL)
        {
            entry = globus_hashtable_lookup(
                    &info->extensions,
                    "gt3-failure-message");
        }

        monitor->failure_code = info->protocol_error_code;
        monitor->failure_message = (entry && entry->value)
                ? strdup(entry->value)
                : strdup(err);
    }
    else if (info->job_state > monitor->job_state)
    {
        monitor->job_state = info->job_state;
        entry = globus_hashtable_lookup(
                &info->extensions,
                "job-failure-code");

        if (entry)
        {
            monitor->failure_code = atoi(entry->value);
        }
    }
    globus_cond_signal(&monitor->cond);
    globus_mutex_unlock(&monitor->mutex);
}
/* globus_l_submit_callback() */

static
void
globus_l_refresh_callback(
    void *                              user_callback_arg,
    globus_gram_protocol_error_t        operation_failure_code,
    const char *                        job_contact,
    globus_gram_protocol_job_state_t    job_state,
    globus_gram_protocol_error_t        job_failure_code)
{
    globus_i_globusrun_gram_monitor_t * monitor = user_callback_arg;

    globus_mutex_lock(&monitor->mutex);
    monitor->submit_done = GLOBUS_TRUE;
    if (job_contact)
    {
        monitor->job_contact_string = strdup(job_contact);
        globus_url_parse(monitor->job_contact_string, &monitor->job_contact);
    }
    if (operation_failure_code != GLOBUS_SUCCESS)
    {
        const char * err = globus_gram_protocol_error_string(
                operation_failure_code);
        monitor->failure_code = operation_failure_code;
        monitor->failure_message = globus_libc_strdup(err);
    }
    else if (job_state > monitor->job_state)
    {
        monitor->failure_code = job_failure_code;
    }
    globus_cond_signal(&monitor->cond);
    globus_mutex_unlock(&monitor->mutex);
}
/* globus_l_refresh_callback() */
