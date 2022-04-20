/*
 * myproxy-store
 *
 * Client program to store a end-entity credential to a myproxy-server
 */

#include "myproxy_common.h"	/* all needed headers included here */

/* Location of default proxy */
#define MYPROXY_DEFAULT_USERCERT  "usercert.pem"
#define MYPROXY_DEFAULT_USERKEY   "userkey.pem"
#define MYPROXY_DEFAULT_DIRECTORY ".globus"

static char usage[] =
    "\n"
    "Syntax: myproxy-store [-c #hours] [-t #hours] [-l username] [-r retrievers] [-w renewers] ...\n"
    "        myproxy-store [-usage|-help] [-version]\n"
    "\n"
    "   Options\n"
    "       -h | --help                       Displays usage\n"
    "       -u | --usage                                    \n"
    "                                                      \n"
    "       -v | --verbose                    Display debugging messages\n"
    "       -V | --version                    Displays version\n"
    "       -s | --pshost         <hostname>  Hostname of the myproxy-server\n"
    "                                         Can also set MYPROXY_SERVER env. var.\n"
    "       -p | --psport         <port #>    Port of the myproxy-server\n"
    "       -c | --certfile       <filename>  Certificate file name\n"
    "       -y | --keyfile        <filename>  Key file name\n"
    "       -l | --username       <username>  Username for the delegated proxy\n"
    "       -t | --proxy_lifetime <hours>     Lifetime of proxies delegated by\n"
    "                                         server (default 12 hours).\n"
    "       -a | --allow_anonymous_retrievers Allow credentials to be retrieved\n"
    "                                         with just username/passphrase\n"
    "       -A | --allow_anonymous_renewers   Allow credentials to be renewed by\n"
    "                                         any client (not recommended)\n"
    "       -x | --regex_dn_match             Set regular expression matching mode\n"
    "                                         for following policy options\n"
    "       -X | --match_cn_only              Set CN matching mode (default)\n"
    "                                         for following policy options\n"
    "       -r | --retrievable_by <dn>        Allow specified entity to retrieve\n"
    "                                         credential\n"
    "       -R | --renewable_by   <dn>        Allow specified entity to renew\n"
    "                                         credential\n"
    "       -Z | --retrievable_by_cert <dn>   Allow specified entity to retrieve\n"
    "                                         credential w/o passphrase\n"
    "       -E | --retrieve_key <dn>          Allow specified entity to retrieve\n"
    "                                         credential key\n"
    "       -d | --dn_as_username             Use the proxy certificate subject\n"
    "                                         (DN) as the default username,\n"
    "                                         instead of the LOGNAME env. var.\n"
    "       -k | --credname       <name>      Specifies credential name\n"
    "       -K | --creddesc       <desc>      Specifies credential description\n"
    "\n";

struct option long_options[] = {
    {"help",                             no_argument, NULL, 'h'},
    {"usage",                            no_argument, NULL, 'u'},
    {"certfile",                   required_argument, NULL, 'c'},
    {"keyfile",                    required_argument, NULL, 'y'},
    {"proxy_lifetime",             required_argument, NULL, 't'},
    {"pshost",                     required_argument, NULL, 's'},
    {"psport",                     required_argument, NULL, 'p'},
    {"username",                   required_argument, NULL, 'l'},
    {"verbose",                          no_argument, NULL, 'v'},
    {"version",                          no_argument, NULL, 'V'},
    {"dn_as_username",                   no_argument, NULL, 'd'},
    {"allow_anonymous_retrievers",       no_argument, NULL, 'a'},
    {"allow_anonymous_renewers",         no_argument, NULL, 'A'},
    {"retrievable_by",             required_argument, NULL, 'r'},
    {"retrievable_by_cert",        required_argument, NULL, 'Z'},
    {"renewable_by",               required_argument, NULL, 'R'},
    {"retrieve_key",               required_argument, NULL, 'E'},
    {"regex_dn_match",                   no_argument, NULL, 'x'},
    {"match_cn_only",                    no_argument, NULL, 'X'},
    {"credname",                   required_argument, NULL, 'k'},
    {"creddesc",                   required_argument, NULL, 'K'},
    {0, 0, 0, 0}
};

/*colon following an option indicates option takes an argument */
static char short_options[] = "uhl:vVdr:R:Z:xXaAk:K:t:c:y:s:p:E:";

static char version[] =
    "myproxy-store version " MYPROXY_VERSION " (" MYPROXY_VERSION_DATE ") "
    "\n";

static char *certfile               = NULL;	/* certificate file name */
static char *keyfile                = NULL;	/* key file name */
static int   dn_as_username         = 0;
static int   verbose                = 0;

/* Function declarations */
int 
init_arguments(    int                      argc,
	           char                    *argv[],
	           myproxy_socket_attrs_t  *attrs,
                   myproxy_request_t       *request);

int 
makecertfile(      const char               certfile[],
	           const char               keyfile[],
                   char                   **credbuf);

#define		SECONDS_PER_HOUR			(60 * 60)

int 
main(int   argc, 
     char *argv[])
{
    char                   *pshost             = NULL;
    char                   *request_buffer     = NULL;
    char                   *credkeybuf         = NULL;
    int                     requestlen;
    int                     return_value = 1;

    myproxy_socket_attrs_t *socket_attrs;
    myproxy_request_t      *client_request;
    myproxy_response_t     *server_response;

    /* check library version */
    if (myproxy_check_version()) {
	fprintf(stderr, "MyProxy library version mismatch.\n"
		"Expecting %s.  Found %s.\n",
		MYPROXY_VERSION_DATE, myproxy_version(0,0,0));
	exit(1);
    }

    myproxy_log_use_stream(stderr);

    socket_attrs = malloc(sizeof(*socket_attrs));
    memset(socket_attrs, 0, sizeof(*socket_attrs));

    client_request = malloc(sizeof(*client_request));
    memset(client_request, 0, sizeof(*client_request));

    server_response = malloc(sizeof(*server_response));
    memset(server_response, 0, sizeof(*server_response));

    /* setup defaults */
    client_request->version = malloc(strlen(MYPROXY_VERSION) + 1);
    strcpy(client_request->version, MYPROXY_VERSION);
    client_request->command_type = MYPROXY_STORE_CERT;

    pshost = getenv("MYPROXY_SERVER");

    if (pshost != NULL) {
	socket_attrs->pshost = strdup(pshost);
    }

    if (getenv("MYPROXY_SERVER_PORT")) {
	socket_attrs->psport = atoi(getenv("MYPROXY_SERVER_PORT"));
    } else {
	socket_attrs->psport = MYPROXY_SERVER_PORT;
    }

    globus_module_activate(GLOBUS_GSI_SYSCONFIG_MODULE);
    GLOBUS_GSI_SYSCONFIG_GET_USER_CERT_FILENAME( &certfile,
						 &keyfile );

    client_request->proxy_lifetime = SECONDS_PER_HOUR *
                                     MYPROXY_DEFAULT_DELEG_HOURS;

    /* Initialize client arguments and create client request object */

    if (init_arguments(argc, argv, socket_attrs, client_request) != 0) {
        goto cleanup;
    }

    if (!certfile && !keyfile) {
	fprintf(stderr, "Credentials not found in default location.\n"
		"Use --certfile and --keyfile options.\n");
	goto cleanup;
    } else if (!certfile) {
	fprintf(stderr, "Certificate not found in default location.\n"
		"Use --certfile option.\n");
	goto cleanup;
    } else if (!keyfile) {
	fprintf(stderr, "Private key not found in default location.\n"
		"Use --keyfile option.\n");
	goto cleanup;
    }

    /*
     ** Read Credential and Key files
     */
    if( makecertfile(certfile, keyfile, &credkeybuf) < 0 )
    {
      fprintf( stderr, "makecertfile failed\n" );
      goto cleanup;
    }

    /* Set up client socket attributes */
    if (myproxy_init_client(socket_attrs) < 0) {
	verror_print_error(stderr);
        goto cleanup;
    }

    if (client_request->username == NULL) { /* set default username */
        if (dn_as_username) {
            if (ssl_get_base_subject_file(certfile,
                                          &client_request->username)) {
                fprintf(stderr,
                        "Cannot get subject name from your certificate\n");
                goto cleanup;
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


    /* Authenticate client to server */
    if (myproxy_authenticate_init(socket_attrs, NULL) < 0) {
	verror_print_error(stderr);
        goto cleanup;
    }

    /* Serialize client request object */
    requestlen = myproxy_serialize_request_ex(client_request, &request_buffer);

    if (requestlen < 0) {
        verror_print_error(stderr);
        goto cleanup;
    }

    /* Send request to the myproxy-server */
    if (myproxy_send(socket_attrs, request_buffer, requestlen) < 0) {
        verror_print_error(stderr);
        goto cleanup;
    }
    free(request_buffer);
    request_buffer = NULL;

    /* Continue unless the response is not OK */
    if (myproxy_recv_response_ex(socket_attrs,
				 server_response, client_request) != 0) {
        verror_print_error(stderr);
        goto cleanup;
    }

    /* Send end-entity credentials to server. */
    if (myproxy_init_credentials(socket_attrs,
				 credkeybuf) < 0) {
        verror_print_error(stderr);
        goto cleanup;
    }

    /* Get final response from server */
    if (myproxy_recv_response(socket_attrs, server_response) != 0) {
        verror_print_error(stderr);
        goto cleanup;
    }

    printf( "Credentials saved to myproxy server.\n" );

    return_value = 0;

 cleanup:
    /* free memory allocated */
    myproxy_free(socket_attrs, client_request, server_response);
    if (credkeybuf) free(credkeybuf);
    if (certfile) free(certfile);
    if (keyfile) free(keyfile);

    return return_value;
}

int
init_arguments(int                     argc,
	       char                   *argv[],
	       myproxy_socket_attrs_t *attrs, 
               myproxy_request_t      * request)
{
    extern char *optarg;
    int expr_type = MATCH_CN_ONLY;	/*default */
    int arg;

    while ((arg = getopt_long(argc,
				  argv,
				  short_options,
				  long_options, NULL)) != EOF) {
	switch (arg) {
	case 's':		/* pshost name */
	    attrs->pshost = strdup(optarg);
	    break;

	case 'p':		/* psport */
	    attrs->psport = atoi(optarg);
	    break;

	case 'c':		/* credential file name */
	    if (certfile) free(certfile);
	    certfile = strdup(optarg);
	    break;

	case 'y':		/* key file name */
	    if (keyfile) free(keyfile);
	    keyfile = strdup(optarg);
	    break;

	case 'u':		/* print help and exit */
	    printf("%s", usage);
	    exit(0);
	    break;

	case 't':		/* Specify proxy lifetime in hours */
	    request->proxy_lifetime = SECONDS_PER_HOUR * atoi(optarg);
        if (request->proxy_lifetime < 0) {
            fprintf(stderr, "Requested lifetime (-t option) out of bounds.\n");
            exit(1);
        }
	    break;

	case 'h':		/* print help and exit */
	    printf("%s", usage);
	    exit(0);
	    break;

	case 'l':		/* username */
	    request->username = strdup(optarg);
	    break;

	case 'v':		/* verbose */
	    myproxy_debug_set_level(1);
	    verbose = 1;
	    break;

	case 'V':		/* print version and exit */
	    printf("%s", version);
	    exit(0);
	    break;


	case 'r':		/* retrievers list */
	    if (request->retrievers) {
		fprintf(stderr,
			"Only one -a or -r option may be specified.\n");
		exit(1);
	    }

	    if (expr_type == REGULAR_EXP) {
		
                /* Copy as is */
		request->retrievers = strdup(optarg);
	    } else {
		request->retrievers =
		    (char *) malloc(strlen(optarg) + 6);
		strcpy(request->retrievers, "*/CN=");
		myproxy_debug("authorized retriever %s",
			      request->retrievers);
		request->retrievers =
		    strcat(request->retrievers, optarg);
	    }
	    break;

	case 'R':		/* renewers list */
            /*
            ** This needs to be readdressed.  Right now, the private key is
            ** being stored encrypted.  This is a problem if the user calls
            ** /myproxy-get-delegation with the -a option.  The call will
            ** fail because an unencrypted password is being looked for.
            ** So, do we want to add code to unencrypt the private key if
            ** this option is used?
            */
	    if (request->renewers) {
		fprintf(stderr,
			"Only one -A or -R option may be specified.\n");
		exit(1);
	    }

	    if (expr_type == REGULAR_EXP) {
		/* Copy as is */
		request->renewers = strdup(optarg);
	    } else {
		request->renewers =
		    (char *) malloc(strlen(optarg) + 6);
		strcpy(request->renewers, "*/CN=");
		myproxy_debug("authorized renewer %s", request->renewers);
		request->renewers = strcat(request->renewers, optarg);
	    }
	    break;

	case 'Z':		/* retrievers list */
	    if (request->trusted_retrievers) {
		fprintf(stderr,
			"Only one -Z option may be specified.\n");
		exit(1);
	    }

	    if (expr_type == REGULAR_EXP) {
		
                /* Copy as is */
		request->trusted_retrievers = strdup(optarg);
	    } else {
		request->trusted_retrievers =
		    (char *) malloc(strlen(optarg) + 6);
		strcpy(request->trusted_retrievers, "*/CN=");
		myproxy_debug("trusted retriever %s",
			      request->trusted_retrievers);
		request->trusted_retrievers =
		    strcat(request->trusted_retrievers, optarg);
	    }
	    break;

        case 'E' :              /* key retriever list */ 
	    if (expr_type == REGULAR_EXP) {
		/* Copy as is */
		request->keyretrieve = strdup(optarg);
	    } else {
		request->keyretrieve =
		    (char *) malloc(strlen(optarg) + 6);
		strcpy(request->keyretrieve, "*/CN=");
		myproxy_debug("authorized key retriever %s",
			      request->keyretrieve);
		request->keyretrieve =
		    strcat(request->keyretrieve, optarg);
	    }
	    break;

	case 'd':		/* 
				 ** use the certificate subject (DN) as the 
				 ** default username instead of LOGNAME 
				 */
	    dn_as_username = 1;
	    break;

	case 'x':		/*set expression type to regex */
	    expr_type = REGULAR_EXP;
	    myproxy_debug("expr-type = regex");
	    break;

	case 'X':		/*set expression type to common name */
	    expr_type = MATCH_CN_ONLY;
	    myproxy_debug("expr-type = CN");
	    break;

	case 'a':		/*allow anonymous retrievers */
	    if (request->retrievers) {
		fprintf(stderr,
			"Only one -a or -r option may be specified.\n");
		exit(1);
	    }

	    request->retrievers = strdup("*");
	    myproxy_debug("anonymous retrievers allowed");
	    break;

	case 'A':		/*allow anonymous renewers */
	    if (request->renewers) {
		fprintf(stderr,
			"Only one -A or -R option may be specified.\n");
		exit(1);
	    }

	    request->renewers = strdup("*");
	    myproxy_debug("anonymous renewers allowed");
	    break;

	case 'k':		/*credential name */
	    request->credname = strdup(optarg);
	    break;

	case 'K':		/*credential description */
	    request->creddesc = strdup(optarg);
	    break;

	default:		/* print usage and exit */
	    fprintf(stderr, "%s", usage);
	    exit(1);
	    break;
	}
    }

    /* Check to see if myproxy-server specified */
    if (attrs->pshost == NULL) {
        fprintf(stderr, "%s", usage);
	fprintf(stderr,
		"Unspecified myproxy-server! Either set the MYPROXY_SERVER environment variable or explicitly set the myproxy-server via the -s flag\n");
	return -1;
    }

    return 0;
}

int 
makecertfile(const char   certfile[],
             const char   keyfile[],
             char       **credbuf)
{
    unsigned char *certbuf = NULL;
    unsigned char *keybuf  = NULL;
    int         retval  = -1;
    struct stat s;
    int         bytes;
    static char BEGINCERT[] = "-----BEGIN CERTIFICATE-----";
    static char ENDCERT[] = "-----END CERTIFICATE-----";
    static char BEGINKEY1[] = "-----BEGIN RSA PRIVATE KEY-----";
    static char BEGINKEY2[] = "-----BEGIN PRIVATE KEY-----";
    static char BEGINKEY3[] = "-----BEGIN ENCRYPTED PRIVATE KEY-----";
    static char ENDKEY1[] = "-----END RSA PRIVATE KEY-----";
    static char ENDKEY2[] = "-----END PRIVATE KEY-----";
    static char ENDKEY3[] = "-----END ENCRYPTED PRIVATE KEY-----";
    char        *certstart; 
    char        *certend;
    int          size;
    char        *keystart; 
    char        *keyend;


    /* Figure out how much memory we are going to need */
    if (stat( certfile, &s ) < 0) {
        fprintf(stderr, "Failed to stat %s: %s\n", certfile, strerror(errno));
        goto cleanup;
    }
    bytes = s.st_size;
    if (stat( keyfile, &s ) < 0) {
        fprintf(stderr, "Failed to stat %s: %s\n", keyfile, strerror(errno));
        goto cleanup;
    }
    bytes += s.st_size;

    *credbuf = malloc( bytes + 1 );
    memset(*credbuf, 0, (bytes + 1));

    /* Read the certificate(s) into a buffer. */
    if (buffer_from_file(certfile, &certbuf, NULL) < 0) {
	fprintf(stderr, "Failed to read %s\n", certfile);
	goto cleanup;
    }

    /* Read the key into a buffer. */
    if (buffer_from_file(keyfile, &keybuf, NULL) < 0) {
        fprintf(stderr, "Failed to read %s\n", keyfile);
        goto cleanup;
    }

    if ((certstart = strstr((const char *)certbuf, BEGINCERT)) == NULL)
    {
      fprintf(stderr, "%s doesn't contain '%s'.\n",  certfile, BEGINCERT);
      goto cleanup;
    }

    if ((certend = strstr(certstart, ENDCERT)) == NULL)
    {
      fprintf(stderr, "%s doesn't contain '%s'.\n", certfile, ENDCERT);
      goto cleanup;
    }
    certend += strlen(ENDCERT);
    size = certend-certstart;

    strncat( *credbuf, certstart, size ); 
    strcat( *credbuf, "\n" );
    certstart += size;

    /* Write the key. */
    if ((keystart = strstr((const char *)keybuf, BEGINKEY1)) == NULL
	&& (keystart = strstr((const char *)keybuf, BEGINKEY2)) == NULL
	&& (keystart = strstr((const char *)keybuf, BEGINKEY3)) == NULL) {
	fprintf(stderr, "%s doesn't contain '%s' nor '%s' nor %s.\n", keyfile,
					BEGINKEY1, BEGINKEY2, BEGINKEY3);
	goto cleanup;
    }

    if ((keyend = strstr(keystart, ENDKEY1)) != NULL)
	keyend += strlen(ENDKEY1);
    else if ((keyend = strstr(keystart, ENDKEY2)) != NULL)
	keyend += strlen(ENDKEY2);
    else if ((keyend = strstr(keystart, ENDKEY3)) != NULL)
	keyend += strlen(ENDKEY3);
    else {
	fprintf(stderr, "%s doesn't contain '%s' nor '%s' nor %s.\n", keyfile, ENDKEY1,
						ENDKEY2, ENDKEY3);
	goto cleanup;
    }

    size = keyend-keystart;

    strncat( *credbuf, keystart, size );
    strcat( *credbuf, "\n" );

    /* Write any remaining certificates. */
    while ((certstart = strstr(certstart, BEGINCERT)) != NULL) {

        if ((certend = strstr(certstart, ENDCERT)) == NULL) {
            fprintf(stderr, "Can't find matching '%s' in %s.\n", ENDCERT,
                    certfile);
            goto cleanup;
        }
        certend += strlen(ENDCERT);
        size = certend-certstart;

        strncat( *credbuf, certstart, size ); 
        strcat( *credbuf, "\n" ); 
        certstart += size;
    }

    retval = 0;

  cleanup:
    if (certbuf) free(certbuf);
    if (keybuf) free(keybuf);

    return (retval);
}

