/*
 * myproxy-get-delegation
 *
 * Webserver program to retrieve a delegated credential from a myproxy-server
 */

#include "myproxy_common.h"	/* all needed headers included here */

static char usage[] = \
"\n"
"Syntax: myproxy-logon [-t hours] [-l username] ...\n"
"        myproxy-logon [-usage|-help] [-version]\n"
"\n"
"   Options\n"
"       -h | --help                       Displays usage\n"
"       -u | --usage                                    \n"
"                                                      \n"
"       -v | --verbose                    Display debugging messages\n"
"       -V | --version                    Displays version\n"
"       -l | --username        <username> Username for the delegated proxy\n"
"       -t | --proxy_lifetime  <hours>    Lifetime of proxies delegated by\n" 
"                                         the server (default 12 hours)\n"
"       -o | --out             <path>     Location of delegated proxy\n"
"                                         (use '-' for stdout)\n"
"       -s | --pshost          <hostname> Hostname of the myproxy-server\n"
"       -p | --psport          <port #>   Port of the myproxy-server\n"
"       -a | --authorization   <path>     Specify credential to renew\n"
"       -d | --dn_as_username             Use subject of the authorization\n"
"                                         credential (or default credential\n"
"                                         if -a not used) as the default\n"
"                                         username instead of $LOGNAME\n"
"       -k | --credname        <name>     Specify credential name\n"
"       -S | --stdin_pass                 Read passphrase from stdin\n"
"       -T | --trustroots                 Manage trust roots\n"
"       -b | --bootstrap                  Bootstrap trust in myproxy-server\n"
"       -n | --no_passphrase              Don't prompt for passphrase\n"
"       -N | --no_credentials             Authenticate only. Don't retrieve\n"
"                                         credentials.\n"
"       -q | --quiet                      Only output on error\n"
"       -m | --voms            <voms>     Include VOMS attributes\n"
"       -Q | --certreq <path>             Use provided certificate request\n"
"\n";

struct option long_options[] =
{
    {"help",                   no_argument, NULL, 'h'},
    {"pshost",           required_argument, NULL, 's'},
    {"psport",           required_argument, NULL, 'p'},
    {"proxy_lifetime",   required_argument, NULL, 't'},
    {"out",              required_argument, NULL, 'o'},
    {"usage",                  no_argument, NULL, 'u'},
    {"username",         required_argument, NULL, 'l'},
    {"verbose",                no_argument, NULL, 'v'},
    {"version",                no_argument, NULL, 'V'},
    {"authorization",    required_argument, NULL, 'a'},
    {"dn_as_username",         no_argument, NULL, 'd'},
    {"credname",	 required_argument, NULL, 'k'},
    {"stdin_pass",             no_argument, NULL, 'S'},
    {"trustroots",             no_argument, NULL, 'T'},
    {"bootstrap",              no_argument, NULL, 'b'},
    {"no_passphrase",          no_argument, NULL, 'n'},
    {"no_passphrase",          no_argument, NULL, 'N'},
    {"quiet",                  no_argument, NULL, 'q'},
    {"voms",            required_argument, NULL, 'm'},
    {"certreq",          required_argument, NULL, 'Q'},
    {0, 0, 0, 0}
};

static char short_options[] = "hus:p:l:t:o:vVa:dk:SnNTbqm:Q:";

static char version[] =
"myproxy-logon version " MYPROXY_VERSION " (" MYPROXY_VERSION_DATE ") "  "\n";

void 
init_arguments(int argc, char *argv[], 
	       myproxy_socket_attrs_t *attrs,
	       myproxy_request_t *request); 

int
voms_proxy_init();

/*
 * Use setvbuf() instead of setlinebuf() since cygwin doesn't support
 * setlinebuf().
 */
#define my_setlinebuf(stream)	setvbuf((stream), (char *) NULL, _IOLBF, 0)

/* location of delegated proxy */
static char *outputfile = NULL;
static int dn_as_username = 0;
static int read_passwd_from_stdin = 0;
static int use_empty_passwd = 0;
static int quiet = 0;
static int bootstrap = 0;
static int no_credentials = 0;
static char **voms = NULL;
static char **vomses = NULL;
static int debug = 0;

int
main(int argc, char *argv[]) 
{    
    myproxy_socket_attrs_t *socket_attrs;
    myproxy_request_t      *client_request;
    myproxy_response_t     *server_response;
    int return_value = 1;

    /* check library version */
    if (myproxy_check_version()) {
	fprintf(stderr, "MyProxy library version mismatch.\n"
		"Expecting %s.  Found %s.\n",
		MYPROXY_VERSION_DATE, myproxy_version(0,0,0));
	exit(1);
    }

    myproxy_log_use_stream (stderr);

    my_setlinebuf(stdout);
    my_setlinebuf(stderr);

    socket_attrs = malloc(sizeof(*socket_attrs));
    memset(socket_attrs, 0, sizeof(*socket_attrs));

    client_request = malloc(sizeof(*client_request));
    memset(client_request, 0, sizeof(*client_request));

    server_response = malloc(sizeof(*server_response));
    memset(server_response, 0, sizeof(*server_response));

    /* Setup defaults */
    myproxy_set_delegation_defaults(socket_attrs, client_request);

    /* Initialize client arguments and create client request object */
    init_arguments(argc, argv, socket_attrs, client_request);

    if (!outputfile && !no_credentials) {
	globus_module_activate(GLOBUS_GSI_SYSCONFIG_MODULE);
	GLOBUS_GSI_SYSCONFIG_GET_PROXY_FILENAME(&outputfile,
						GLOBUS_PROXY_FILE_OUTPUT);
    }

    /* Connect to server and authenticate.
       Bootstrap trust roots as needed. */
    if (myproxy_bootstrap_client(socket_attrs,
                                 client_request->want_trusted_certs,
                                 bootstrap) < 0) {
        verror_print_error(stderr);
        goto cleanup;
    }

    if (!use_empty_passwd) {
       /* Allow user to provide a passphrase */
	int rval;
	if (read_passwd_from_stdin) {
	    rval = myproxy_read_passphrase_stdin(
			   client_request->passphrase,
			   sizeof(client_request->passphrase),
			   NULL);
	} else {
	    rval = myproxy_read_passphrase(client_request->passphrase,
					   sizeof(client_request->passphrase),
					   NULL);
	}
	if (rval == -1) {
	    verror_print_error(stderr);
	    goto cleanup;
	}
    }

    if (client_request->username == NULL) { /* set default username */
	if (dn_as_username) {
	    if (client_request->authzcreds) {
		if (ssl_get_base_subject_file(client_request->authzcreds,
					      &client_request->username)) {
		    fprintf(stderr, "Cannot get subject name from %s.\n",
			    client_request->authzcreds);
		    goto cleanup;
		}
	    } else {
		if (ssl_get_base_subject_file(NULL,
					      &client_request->username)) {
		    fprintf(stderr,
			    "Cannot get subject name from your certificate.\n");
		    goto cleanup;
		}
	    }
	} else {
	    char *username = NULL;
	    if (!(username = getenv("LOGNAME"))) {
		fprintf(stderr, "Please specify a username.\n");
		goto cleanup;
	    }
	    client_request->username = strdup(username);
	}
    }

    if (myproxy_get_delegation(socket_attrs, client_request, NULL,
			       server_response, outputfile)!=0) {
	fprintf(stderr, "Failed to receive credentials.\n");
	verror_print_error(stderr);
	goto cleanup;
    }

    if (outputfile) {
        if (voms && (! has_voms_extension(outputfile)) ) {
            if (voms_proxy_init() != 0) {
                fprintf(stderr, "Failed to add VOMS attributes.\n");
                verror_print_error(stderr);
                goto cleanup;
            }
        }

        if (!quiet)
            printf("A credential has been received for user %s in %s.\n",
                   client_request->username, outputfile);
        free(outputfile);
        outputfile = NULL;
        verror_clear();
    }

    /* Store file in trusted directory if requested and returned */
    if (client_request->want_trusted_certs) {
        if (server_response->trusted_certs != NULL) {
            if (myproxy_install_trusted_cert_files(server_response->trusted_certs) != 0) {       
		verror_print_error(stderr);
		goto cleanup;
            } else {
		char *path;
		path = get_trusted_certs_path();
        if (path) {
            if (!quiet) {
                printf("Trust roots have been installed in %s.\n", path);
            }
            free(path);
        }
	    }
        } else {
            myproxy_debug("Requested trusted certs but didn't get any.\n");
        }
    }
    
    return_value = 0;

 cleanup:
    /* free memory allocated */
    myproxy_free(socket_attrs, client_request, server_response);
    if (voms) free_array_list(&voms);
    if (vomses) free_array_list(&vomses);
    return return_value;
}

void 
init_arguments(int argc, 
	       char *argv[], 
	       myproxy_socket_attrs_t *attrs,
	       myproxy_request_t *request) 
{   
    extern char *optarg;
    int arg;

    while((arg = getopt_long(argc, argv, short_options, 
				 long_options, NULL)) != EOF) 
    {
        switch(arg) 
        {
	case 't':       /* Specify proxy lifetime in seconds */
	  request->proxy_lifetime = 60*60*atoi(optarg);
      if (request->proxy_lifetime < 0) {
          fprintf(stderr, "Requested lifetime (-t option) out of bounds.\n");
          exit(1);
      }
	  break;
        case 's': 	/* pshost name */
	    attrs->pshost = strdup(optarg);
            break;
        case 'p': 	/* psport */
            attrs->psport = atoi(optarg);
            break;
	case 'h': 	/* print help and exit */
        case 'u': 	/* print help and exit */
            printf("%s", usage);
            exit(0);
            break;
        case 'l':	/* username */
            request->username = strdup(optarg);
            break;
        case 'o':	/* output file */
            outputfile = strdup(optarg);
            if (outputfile[0] == '-' && outputfile[1] == '\0') {
                if (voms) {
                    fprintf(stderr, "-voms is not compatible with -o -\n");
                    exit(1);
                }
                quiet = 1;
            }
            break;    
	case 'a':       /* special authorization */
	    request->authzcreds = strdup(optarg);
	    use_empty_passwd = 1;
	    break;
	case 'n':       /* no passphrase */
	    use_empty_passwd = 1;
	    break;
	case 'N':
	    no_credentials = 1;
	    break;
	case 'q':
	    quiet = 1;
	    break;
        case 'Q':
            request->certreq = strdup(optarg);
            break;
	case 'b':
	    bootstrap = 1;
	    request->want_trusted_certs = 1; /* -b implies -T */
        myproxy_debug("Requesting trusted certificates.\n");
	    break;
	case 'v':
        debug = 1;
	    myproxy_debug_set_level(1);
	    break;
        case 'V':       /* print version and exit */
            printf("%s", version);
            exit(0);
            break;
	case 'd':   /* use the certificate subject (DN) as the default
		       username instead of LOGNAME */
	    dn_as_username = 1;
	    break;
	case 'k':   /* credential name */
	    request->credname = strdup (optarg);
	    break;
	case 'S':
	    read_passwd_from_stdin = 1;
	    break;
	case 'T':
	    request->want_trusted_certs = 1;
            myproxy_debug("Requesting trusted certificates.\n");
	    break;
        case 'm':
            if (outputfile && outputfile[0] == '-' && outputfile[1] == '\0') {
                fprintf(stderr, "-voms is not compatible with -o -\n");
                exit(1);
            }
            voms = add_entry(voms, optarg);
            break;
        default:        /* print usage and exit */ 
            fprintf(stderr, "%s", usage);
	    exit(1);
	    break;	
        }
    }

    if (optind != argc) {
	fprintf(stderr, "%s: invalid option -- %s\n", argv[0],
		argv[optind]);
	fprintf(stderr, "%s", usage);
	exit(1);
    }

    /* Check to see if myproxy-server specified */
    if (attrs->pshost == NULL) {
	fprintf(stderr, "Unspecified myproxy-server. Please set the MYPROXY_SERVER environment variable\nor set the myproxy-server hostname via the -s flag.\n");
	exit(1);
    }

    if (voms) {
        int i;
        char * voms_userconf = NULL;
        for (i = 0; voms[i] != NULL; i++) {
            myproxy_request_add_voname(request, voms[i]);
        }
        voms_userconf = getenv("VOMS_USERCONF");
        if (voms_userconf != NULL) {
            vomses = get_vomses(voms_userconf);
            if (vomses != NULL) {
                for (i = 0; vomses[i]; i++) {
                    myproxy_request_add_vomses(request, vomses[i]);
                }
            }
        }
    }

    return;
}

int
voms_proxy_init()
{
    int i, hours, minutes, cred_lifetime, rc;
    time_t cred_expiration;
    const char *argv[40];
    char bits[11], vomslife[14];
    int argc = 0;
    pid_t childpid;
    const char *command = "voms-proxy-init";
    X509 *cert = NULL;
    FILE *cert_file = NULL;
    globus_result_t	local_result;
    globus_gsi_cert_utils_cert_type_t cert_type;
    char                *keybitsenv = NULL;
    int                 keybits = MYPROXY_DEFAULT_KEYBITS;

    if (ssl_get_times(outputfile, NULL, &cred_expiration) != 0) {
        verror_put_string("ssl_get_times(%s) failed", outputfile);
        return -1;
    }
    cred_lifetime = cred_expiration-time(0);
    if (cred_lifetime <= 0) {
        verror_put_string("Error: Credential expired!");
        return -1;
    }

    hours = (int)(cred_lifetime/(60*60));
    minutes = (int)(cred_lifetime/60)%60;
    if (minutes) {
        minutes--;
    } else {
        hours--;
        minutes = 59;
    }

    /* what type of proxy certificate do we have? */
    cert_file = fopen(outputfile, "r");
    if (cert_file == NULL) {
        verror_put_string("Failure opening file \"%s\"", outputfile);
        verror_put_errno(errno);
        return -1;
    }
    cert = PEM_read_X509(cert_file, NULL, NULL, NULL);
    fclose(cert_file);
    cert_file = NULL;
    if (cert == NULL) {
        verror_put_string("PEM_read_X509(%s) failed.", outputfile);
        return -1;
    }
    local_result = globus_gsi_cert_utils_get_cert_type(cert,
                                                       &cert_type);
    X509_free(cert);
    cert = NULL;
    if (local_result != GLOBUS_SUCCESS) {
        verror_put_string("globus_gsi_cert_utils_get_cert_type() failed");
        globus_error_to_verror(local_result);
        return -1;
    }

    if ((keybitsenv = getenv("MYPROXY_KEYBITS")) != NULL) {
        keybits = atoi(keybitsenv);
    }

    /* Setup the environment for voms-proxy-init. */
    unsetenv("X509_USER_CERT");
    unsetenv("X509_USER_KEY");
    setenv("X509_USER_PROXY", outputfile, 1);

    argv[argc++] = command;
    argv[argc++] = "-valid";
    snprintf(vomslife, sizeof(vomslife), "%d:%d", hours, minutes);
    argv[argc++] = vomslife;
    argv[argc++] = "-vomslife";
    argv[argc++] = vomslife;
    for (i=0; voms[i] && i < 10; i++) {
        argv[argc++] = "-voms";
        argv[argc++] = voms[i];
    }
    argv[argc++] = "-cert";
    argv[argc++] = outputfile;
    argv[argc++] = "-key";
    argv[argc++] = outputfile;
    argv[argc++] = "-out";
    argv[argc++] = outputfile;
    argv[argc++] = "-bits";
    snprintf(bits, sizeof(bits), "%d", keybits);
    argv[argc++] = bits;
    argv[argc++] = "-noregen";
	if (GLOBUS_GSI_CERT_UTILS_IS_GSI_3_PROXY(cert_type)) {
        argv[argc++] = "-proxyver=3";
#if defined(GLOBUS_GSI_CERT_UTILS_IS_RFC_PROXY)
	} else if (GLOBUS_GSI_CERT_UTILS_IS_RFC_PROXY(cert_type)) {
        argv[argc++] = "-proxyver=4";
#endif
	} else if (GLOBUS_GSI_CERT_UTILS_IS_GSI_2_PROXY(cert_type)) {
        argv[argc++] = "-proxyver=2";
    }
    if (GLOBUS_GSI_CERT_UTILS_IS_LIMITED_PROXY(cert_type)) {
        argv[argc++] = "-limited";
    }
    argv[argc++] = NULL;

    if (debug) {
        char *cmdbuf = NULL;
        join_array(&cmdbuf, (char **)argv, " ");
        myproxy_debug("running: %s", cmdbuf);
        free(cmdbuf);
    }

    if ((childpid = fork()) < 0) {
        verror_put_string("fork() failed");
        verror_put_errno(errno);
        return -1;
    }
    if (childpid == 0) {	/* child */
        execvp(command, (char *const *)argv);
        fprintf(stderr, "failed to run %s: %s\n", command, strerror(errno));
        exit(1);
    }
    if (waitpid(childpid,&rc,0) == -1) {
        verror_put_string("wait() failed for voms-proxy-init child");
        verror_put_errno(errno);
        return -1;
    }

    return rc;
}
