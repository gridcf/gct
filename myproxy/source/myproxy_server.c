/*
 * myproxy-server
 *
 * program to store user's delegated credentials for later retrieval
 */

#include "myproxy_common.h"     /* all needed headers included here */

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#ifndef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#endif

int have_voms = 0;
void (*get_voms_proxy_impl)();

static char usage[] = \
"\n"\
"Syntax: myproxy-server [-p|-port #] [-c config-file] [-s storage-dir] ...\n"\
"        myproxy-server [-h|-help] [-version]\n"\
"\n"\
"   Options\n"\
"       -h | --help                 Displays usage\n"\
"       -u | --usage                \n"\
"                                   \n"\
"       -v | --verbose              Display debugging messages\n"\
"       -V | --version              Displays version\n"\
"       -d | --debug                Run in debug mode (don't fork)\n"\
"       -c | --config               Specifies configuration file to use\n"\
"       -l | --listen  <hostname>   Specifies hostname/ip to listen to\n"\
"       -p | --port    <portnumber> Specifies the port to run on\n"\
"       -P | --pidfile <path>       Specifies a file to write the pid to\n"\
"       -z | --portfile <path>      Specifies a file to write the port to\n"\
"       -s | --storage <directory>  Specifies the credential storage directory\n"\
"\n";

struct option long_options[] =
{
    {"debug",            no_argument, NULL, 'd'},
    {"help",             no_argument, NULL, 'h'},
    {"listen",     required_argument, NULL, 'l'},
    {"port",       required_argument, NULL, 'p'},
    {"pidfile",    required_argument, NULL, 'P'},
    {"portfile",   required_argument, NULL, 'z'},
    {"config",     required_argument, NULL, 'c'},
    {"storage",    required_argument, NULL, 's'},
    {"usage",            no_argument, NULL, 'u'},
    {"verbose",          no_argument, NULL, 'v'},
    {"version",          no_argument, NULL, 'V'},
    {0, 0, 0, 0}
};

static char short_options[] = "dhc:l:p:P:z:s:vVuD:";

static char version[] =
"myproxy-server version " MYPROXY_VERSION " (" MYPROXY_VERSION_DATE ") "  "\n";

/* Signal handling */
typedef void Sigfunc(int);

Sigfunc *my_signal(int signo, Sigfunc *func);
void sig_exit(int signo);
void sig_chld(int signo);
void sig_hup(int signo);
void sig_ign(int signo);

/* Function declarations */
int init_arguments(int argc,
                   char *argv[],
                   myproxy_socket_attrs_t *server_attrs,
                   myproxy_server_context_t *server_context);

int myproxy_init_server(myproxy_socket_attrs_t *server_attrs);

int handle_config(myproxy_server_context_t *server_context);

int handle_client(myproxy_socket_attrs_t *server_attrs,
                  myproxy_server_context_t *server_context);

void respond_with_error_and_die(myproxy_socket_attrs_t *attrs,
                                const char *error,
                                myproxy_server_context_t *context);

void send_response(myproxy_socket_attrs_t *server_attrs,
                   myproxy_response_t *response,
                   char *client_name,
                   int ignore_net_error);

void get_proxy(myproxy_socket_attrs_t *server_attrs,
               myproxy_creds_t *creds,
               myproxy_request_t *request,
               myproxy_response_t *response,
               int max_proxy_lifetime);

void put_proxy(myproxy_socket_attrs_t *server_attrs,
               myproxy_creds_t *creds,
               myproxy_response_t *response,
               int max_cred_lifetime);

void info_proxy(myproxy_creds_t *creds, myproxy_response_t *response);

void destroy_proxy(myproxy_creds_t *creds, myproxy_response_t *response);

void change_passwd(myproxy_creds_t *creds, char *new_passphrase,
                   myproxy_response_t *response);

static void failure(const char *failure_message);

static void my_failure(const char *failure_message);

static void my_failure_chld(const char *failure_message);

static char *timestamp(void);

static int become_daemon_step1(void);
static int become_daemon_step2(void);
static int become_daemon_step3(char);

static void write_pfile(const char path[], long val);

static int myproxy_check_policy(myproxy_server_context_t *context,
                                myproxy_socket_attrs_t *attrs,
                                myproxy_server_peer_t *client,
                                const char *policy_name,
                                const char **server_policy,
                                const char *credential_policy,
                                const char **default_credential_policy);

static int myproxy_authorize_accept(myproxy_server_context_t *context,
                                    myproxy_socket_attrs_t *attrs,
                                    myproxy_request_t *client_request,
                                    myproxy_server_peer_t *client);

/* returns 1 if passphrase matches, 0 otherwise */
static int
verify_passphrase(struct myproxy_creds *creds,
                  myproxy_request_t *client_request,
                  char *client_name,
                  myproxy_server_context_t* config);

/* returns -1 if authentication failed,
            0 if authentication succeeded,
            1 if certificate-based (renewal) authentication succeeded */
static int authenticate_client(myproxy_socket_attrs_t *attrs,
                               struct myproxy_creds *creds,
                               myproxy_request_t *client_request,
                               char *client_name,
                               myproxy_server_context_t* config,
                               int already_authenticated,
                               int allowed_to_renew);

/* Delegate requested credentials to the client */
void get_credentials(myproxy_socket_attrs_t *attrs,
                     myproxy_creds_t        *creds,
                     myproxy_request_t      *request,
                     myproxy_response_t     *response,
                     int                     max_proxy_lifetime);

/* Accept end-entity credentials from client */
void put_credentials(myproxy_socket_attrs_t *attrs,
                     myproxy_creds_t        *creds,
                     myproxy_response_t     *response,
                     int                     max_cred_lifetime);

/* Helper function for put_proxy() and put_credentials() */
void check_and_store_credentials(const char              path[],
                                 myproxy_creds_t        *creds,
                                 myproxy_response_t     *response,
                                 int                     max_cred_lifetime);


static int debug = 0;
static int readconfig = 1;      /* do we need to read config file? */
static int cleanshutdown = 0;   /* should we shutdown? */
static int caonly = 0;          /* CA-only mode */
static int startup_pipe[2];
static int listenfd = -1;

int
main(int argc, char *argv[])
{
    pid_t childpid, otherpid;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    sigset_t mysigset;
    struct pidfh *pfh = NULL;
    void * voms_lib_handle;

    myproxy_socket_attrs_t         *socket_attrs;
    myproxy_server_context_t       *server_context;

    /* check library version */
    if (myproxy_check_version()) {
        fprintf(stderr, "MyProxy library version mismatch.\n"
                "Expecting %s.  Found %s.  Exiting.\n",
                MYPROXY_VERSION_DATE, myproxy_version(0,0,0));
        exit(1);
    }
    voms_lib_handle = dlopen("libmyproxy_voms.so", RTLD_LAZY|RTLD_LOCAL);
    if (voms_lib_handle != NULL)
    {
        have_voms = 1;
        get_voms_proxy_impl = dlsym(voms_lib_handle, "get_voms_proxy");
    }


    socket_attrs    = malloc(sizeof(*socket_attrs));
    memset(socket_attrs, 0, sizeof(*socket_attrs));

    server_context  = malloc(sizeof(*server_context));
    memset(server_context, 0, sizeof(*server_context));

    sigemptyset(&mysigset);

    /* Set context defaults */
    server_context->run_as_daemon = 1;

    if (init_arguments(argc, argv, socket_attrs, server_context) < 0) {
        fprintf(stderr, "%s", usage);
        exit(1);
    }

    /*
     * Test to see if we're run out of inetd
     * If so, then stdin will be connected to a socket,
     * so getpeername() will succeed.
     * If we're not run out of inetd, do the proper daemon setup
     * by calling become_daemon_step1(), but save the daemon fork()
     * in become_daemon_step2() until after some sanity checks.
     */
    if (getpeername(fileno(stdin), (struct sockaddr *) &client_addr, &client_addr_len) < 0) {
        server_context->run_as_daemon = 1;
        if (!debug) {
            if (become_daemon_step1() < 0) {
                fprintf(stderr, "Error starting daemon.  Exiting.\n");
                exit(1);
            }
        }
    } else {
        server_context->run_as_daemon = 0;
        close(1);
        (void) open("/dev/null",O_WRONLY);
    }

    /* Initialize Logging */
    if (debug) {
        myproxy_debug_set_level(1);
        myproxy_log_use_stream(stderr);
    } else {
        myproxy_log_use_syslog(LOG_DAEMON, server_context->my_name);
    }

    /*
     * Logging initialized: For here on use myproxy_log functions
     * instead of fprintf() and ilk.
     */
    myproxy_log("myproxy-server %s starting at %s",
                myproxy_version(0,0,0), timestamp());

    /* If process is killed or Ctrl-C */
    my_signal(SIGTERM, sig_exit);
    sigaddset(&mysigset, SIGTERM);
    my_signal(SIGINT,  sig_exit);
    sigaddset(&mysigset, SIGINT);

    /* Read my configuration */
    if (handle_config(server_context) < 0) {
        myproxy_log_verror();
        myproxy_log("Exiting.");
        exit(1);
    }

    /* Make sure all's well with the storage directory. */
    if (myproxy_check_storage_dir() == -1) {
        myproxy_log_verror();
        if (is_certificate_authority_configured(server_context)) {
            myproxy_log("No valid storage directory found. Running in CA-only mode.");
            verror_clear();
            caonly = 1;
        } else {
            myproxy_log("Exiting.  Please fix errors with storage directory and restart.");
            exit(1);
        }
    }

    if(server_context->certificate_openssl_engine_id) {
#ifndef OPENSSL_NO_ENGINE
        if(!initialise_openssl_engine(server_context)) {
            myproxy_log_verror();
            my_failure("Could not initialise OpenSSL engine.");
        }
#else
        myproxy_log("Openssl has no engine support.");
        myproxy_log("Can not use certificate_openssl_engine_id option.");
        myproxy_log("Exiting.");
        exit(1);
#endif
    }

    if (!server_context->run_as_daemon) {
        server_context->usage.client_ip[0] = '\0';
        getnameinfo((struct sockaddr *)&client_addr,
                    sizeof(client_addr),
                    server_context->usage.client_ip,
                    sizeof(server_context->usage.client_ip),
                    NULL, 0,
                    NI_NUMERICHOST);
        myproxy_log("Connection from %s", server_context->usage.client_ip);
        socket_attrs->socket_fd = fileno(stdin);
        if (handle_client(socket_attrs, server_context) < 0) {
            my_failure("error in handle_client()");
        }
    } else {
        /* Initialize the server before becoming a daemon to catch
           errors before exit of parent process. */
        listenfd = myproxy_init_server(socket_attrs);

        /* Run as a daemon */
        if (!debug) {
            if (become_daemon_step2() < 0) {
                my_failure("Error forking daemon.  Exiting.\n");
            }
        }
        /* no exit() allowed before become_daemon_step3() call */

        if (getuid() == 0 && !server_context->pidfile) {
            server_context->pidfile = "/run/myproxy.pid";
        }
        if (server_context->pidfile) {
            /* It'd be nice to call pidfile_open() before forking the
               daemon process, but we'd lose our POSIX file lock on the
               pidfile when the original process exits, so we
               create/lock/write pidfile here after forking a new
               daemon process. */
            pfh = pidfile_open(server_context->pidfile, 0600, &otherpid);
            if (pfh == NULL) {
                if (errno == EEXIST) {
                    myproxy_log("Daemon already running, pid=%ld, pidfile=%s.\n"
                        "Use the -P option to run multiple "
                        "myproxy-server instances with different pidfiles.",
                        (long)otherpid, server_context->pidfile);
                    if (!debug) become_daemon_step3(1); /* notify parent */
                    exit(1);
                }
                /* If we cannot create pidfile from other reasons, only warn. */
                myproxy_log("Cannot open or create pidfile %s",
                            server_context->pidfile);
            }
        }
        if (pfh) pidfile_write(pfh);
        if (server_context->portfile) {
            write_pfile(server_context->portfile, socket_attrs->psport);
        }

        /* Set up signal handling to deal with zombie processes left over  */
        my_signal(SIGCHLD, sig_chld);
        sigaddset(&mysigset, SIGCHLD);

        /* Re-read configuration file on SIGHUP */
        my_signal(SIGHUP, sig_hup);
        sigaddset(&mysigset, SIGHUP);

        if (!debug) {
            become_daemon_step3(0); /* all done with initialization */
        }

        /* Set up concurrent server */
        while (1) {

            /* make sure Globus hasn't blocked signals we care about */
#ifdef HAVE_PTHREAD_SIGMASK
            pthread_sigmask(SIG_UNBLOCK, &mysigset, NULL);
#else
            sigprocmask(SIG_UNBLOCK, &mysigset, NULL);
#endif

            socket_attrs->socket_fd = accept(listenfd,
                                             (struct sockaddr *) &client_addr,
                                             &client_addr_len);
            if (cleanshutdown) goto parent_exit;
            if (handle_config(server_context) < 0) {
                myproxy_log_verror();
                my_failure("error in handle_config()");
            }
            if (socket_attrs->socket_fd < 0) {
                if (errno == EINTR) {
                    continue;
                } else {
                    myproxy_log_perror("Error in accept()");
                    continue;
                }
            }
            if (!debug) {
                childpid = fork();

                if (childpid < 0) {              /* check for error */
                    myproxy_log_perror("Error in fork");
                    close(socket_attrs->socket_fd);
                } else if (childpid != 0) {
                    /* Parent */
                    /* parent closes connected socket */
                    close(socket_attrs->socket_fd);
                    continue;       /* while(1) */
                }

                /* child process */
                server_context->usage.client_ip[0] = '\0';
                getnameinfo((struct sockaddr *)&client_addr,
                            sizeof(client_addr),
                            server_context->usage.client_ip,
                            sizeof(server_context->usage.client_ip),
                            NULL, 0,
                            NI_NUMERICHOST);
                myproxy_log("Connection from %s", server_context->usage.client_ip);
                close(0);
                close(1);
                if (!debug) {
                    close(2);
                }
                close(listenfd);
                if (pfh) pidfile_close(pfh);
                if (server_context->request_timeout == 0) {
                    alarm(MYPROXY_DEFAULT_TIMEOUT);
                } else if (server_context->request_timeout > 0) {
                    alarm(server_context->request_timeout);
                }
            }
            my_signal(SIGCHLD, SIG_DFL);
            if (handle_client(socket_attrs, server_context) < 0) {
                my_failure_chld("error in handle_client()");
            }
            _exit(0);
        }
    }

 parent_exit:
    pidfile_remove(pfh);
    return 0;
}

int
handle_config(myproxy_server_context_t *server_context)
{
    if (readconfig) {
        if (myproxy_server_config_read(server_context) == -1) {
            return -1;
        }
        readconfig = 0;         /* reset the flag now that we've read it */

        /* Check to see if config file had syslog_ident
           or syslog_facility specified.
           If so, then re-open the syslog with the new name.       */
        if ((!debug) &&
            ((server_context->syslog_ident != NULL) ||
             (server_context->syslog_facility != LOG_DAEMON))) {
            closelog();
            if (server_context->syslog_ident != NULL) {
                myproxy_log_use_syslog(server_context->syslog_facility,
                                       server_context->syslog_ident);
            } else {
                myproxy_log_use_syslog(server_context->syslog_facility,
                                       server_context->my_name);
            }
        }

        /*
         * set up gridmap file if explicitly defined.
         * if not, default to the usual place, but do not over write
         * the env var if previously defined.
         */
        if ( server_context->certificate_mapfile != NULL ) {
            setenv( "GRIDMAP", server_context->certificate_mapfile, 1 );
        } else {
            setenv( "GRIDMAP", "/etc/grid-security/grid-mapfile", 0 );
        }
    }

    return 0;
}

int
handle_client(myproxy_socket_attrs_t *attrs,
              myproxy_server_context_t *context)
{
    myproxy_server_peer_t client;
    char  *client_buffer = NULL;
    int   requestlen;
    int   use_ca_callout = 0;
    int   found_auth_cred = 0;
    int   num_auth_creds = 0;
    char  *command_name = NULL;

    myproxy_creds_t *client_creds;
    myproxy_creds_t *all_creds;
    myproxy_creds_t *cur_cred;
    myproxy_request_t *client_request;
    myproxy_response_t *server_response;

    client_creds    = malloc(sizeof(*client_creds));
    memset(client_creds, 0, sizeof(*client_creds));

    client_request  = malloc(sizeof(*client_request));
    memset(client_request, 0, sizeof(*client_request));

    server_response = malloc(sizeof(*server_response));
    memset(server_response, 0, sizeof(*server_response));

    memset(&client, 0, sizeof(client));

    /* Create a new gsi socket */
    attrs->gsi_socket = GSI_SOCKET_new(attrs->socket_fd);
    if (attrs->gsi_socket == NULL) {
        myproxy_log_perror("GSI_SOCKET_new()");
        return -1;
    }

    if (context->request_size_limit > 0) {
        GSI_SOCKET_set_max_token_len(attrs->gsi_socket,
                                     context->request_size_limit);
    }

    /* Authenticate server to client and get DN of client */
    if (myproxy_authenticate_accept_fqans(attrs, client.name,
          sizeof(client.name), &client.fqans) < 0) {
        /* Client_name may not be set on error so don't use it. */
        myproxy_log_verror();
        respond_with_error_and_die(attrs, "authentication failed", context);
    }

    /* Log client name */
    myproxy_log("Authenticated client %s", client.name);

    if (client.fqans && *client.fqans) {
        char **attributes = client.fqans;
        myproxy_debug("Client's attributes: ");
        while (attributes && *attributes) {
            myproxy_debug("%s", *attributes);
            attributes++;
        }
    }

    /* Receive client request */
    requestlen = myproxy_recv_ex(attrs, &client_buffer);
    if (requestlen <= 0) {
        myproxy_log_verror();
        respond_with_error_and_die(attrs, "Error in myproxy_recv_ex()", context);
    }

    /* Deserialize client request */
    if (myproxy_deserialize_request(client_buffer, requestlen,
                                    client_request) < 0) {
        myproxy_log_verror();
        respond_with_error_and_die(attrs, "error parsing request", context);
    }
    free(client_buffer);
    client_buffer = NULL;

    /* Set response OK unless error... */
    server_response->response_type = MYPROXY_OK_RESPONSE;

    /* Log received client request. We log before the authorization
     * check, so we have the request info for troubleshooting purposes
     * even if the request is denied. */
    switch (client_request->command_type) {
      case MYPROXY_GET_PROXY:
        command_name = "GET"; break;
      case MYPROXY_RETRIEVE_CERT:
        command_name = "RETRIEVE"; break;
      case MYPROXY_PUT_PROXY:
        command_name = "PUT"; break;
      case MYPROXY_INFO_PROXY:
        command_name = "INFO"; break;
      case MYPROXY_DESTROY_PROXY:
        command_name = "DESTROY"; break;
      case MYPROXY_CHANGE_CRED_PASSPHRASE:
        command_name = "CHANGE_CRED_PASSPHRASE"; break;
      case MYPROXY_STORE_CERT:
        command_name = "STORE"; break;
      case MYPROXY_GET_TRUSTROOTS:
        command_name = "GET TRUSTROOTS"; break;
      default:
        myproxy_log("Received UNKNOWN command: %d",
                    client_request->command_type);
        respond_with_error_and_die(attrs, "UNKNOWN command in request.\n", context);
    }
    if (client_request->username && client_request->username[0]) {
        myproxy_log("Received %s request for username %s",
                    command_name, client_request->username);
    } else {
        myproxy_log("Received %s request", command_name);
    }
    if (client_request->credname != NULL) {
        myproxy_debug("  Credname: %s", client_request->credname);
    }
    if (client_request->proxy_lifetime) {
        myproxy_debug("  Requested lifetime: %d seconds",
                      client_request->proxy_lifetime);
        if (client_request->proxy_lifetime < 0) { /* integer overflow */
            myproxy_log("requested lifetime is negative. setting to 0 instead.");
            client_request->proxy_lifetime = 0;
        }
    }
    if (client_request->retrievers != NULL) {
        myproxy_debug("  Retriever policy: %s", client_request->retrievers);
    }
    if (client_request->renewers != NULL) {
        myproxy_debug("  Renewer policy: %s", client_request->renewers);
    }
    if (client_request->keyretrieve != NULL) {
        myproxy_debug("  Key Retriever policy: %s",
                      client_request->keyretrieve);
    }

    /* Check client version */
    if (strcmp(client_request->version, MYPROXY_VERSION) != 0) {
        myproxy_log("client %s Invalid version number (%s) received",
                    client.name, client_request->version);
        respond_with_error_and_die(attrs,
                                   "Invalid version number received.\n", context);
    }

    if (client_request->command_type != MYPROXY_GET_TRUSTROOTS) {
        /* Check client username */
        if ((client_request->username == NULL) ||
            (strlen(client_request->username) == 0))
        {
            myproxy_log("client %s Invalid username (%s) received",
                        client.name,
                        (client_request->username == NULL ? "<NULL>" :
                         client_request->username));
            respond_with_error_and_die(attrs,
                                       "Invalid username received.\n", context);
        }
    }

    if (client_request->command_type == MYPROXY_GET_PROXY) {
        /* If the check_multiple_credentials option has been set AND no
         * client_request->credname is specified, then check ALL credentials
         * with the specified username for one that matches all other criteria
         * set by the user.  If we find at least one credential that is okay
         * according to myproxy_authorize_accept, we SET the credname and
         * continue processing as normal.  (Thus we know that the credential
         * with that username AND credname will be utilized.)  Otherwise, we
         * error out here since there are no matching credentials with the given
         * username and other user-specified criteria (e.g. passphrase).  */
        if ((context->check_multiple_credentials) &&
            (client_request->credname == NULL) &&
            /* Do an initial check for authz of "default" credential */
            (myproxy_authorize_accept(context,attrs,
                                      client_request,&client) != 0)) {

            /* Create a new temp cred struct pointer to fetch all creds */
            all_creds = malloc(sizeof(*all_creds));
            memset(all_creds, 0, sizeof(*all_creds));
            /* For fetching all creds, we need set only the username */
            all_creds->username = strdup(client_request->username);

            if ((num_auth_creds = myproxy_admin_retrieve_all(all_creds)) >= 0) {
                /* Loop through all_creds searching for authorized credential */
                found_auth_cred = 0;
                cur_cred = all_creds;
                while ((!found_auth_cred) && (cur_cred != NULL)) {
                    myproxy_debug("Checking credential for '%s' named '%s'",
                                  cur_cred->username,cur_cred->credname);
                    /* Copy the cur_cred->credname (if present) into the
                     * client_request structure. Be sure to free later. */
                    if (cur_cred->credname)
                        client_request->credname = strdup(cur_cred->credname);
                    /* Check to see if the credname is authorized */
                    if (myproxy_authorize_accept(context,attrs,client_request,
                                                 &client) == 0) {
                        found_auth_cred = 1;  /* Good! Authz success! */
                    } else {
                        /* Free up char memory allocated by strdup earlier */
                        if (cur_cred->credname) {
                            free(client_request->credname);
                            client_request->credname = NULL;
                        }
                        cur_cred = cur_cred->next;   /* Try next cred in list */
                    }
                } /* end while ((!found_auth_cred) && (cur_cred != NULL)) */
            } /* end if (myproxy_admin_retrieve_all) */

            myproxy_creds_free(all_creds);
        } /*** END check_multiple_credentials ***/
    }

    /* All authorization policies are enforced in this function. */
    if (myproxy_authorize_accept(context, attrs,
                                 client_request, &client) < 0) {
        myproxy_log("authorization failed");
        myproxy_free(NULL, client_request, server_response);
        respond_with_error_and_die(attrs, verror_get_string(), context);
    }

    /* Fill in client_creds with info from the request that describes
       the credentials the request applies to.
       We must do this *after* processing check_multiple_credentials above. */
    client_creds->owner_name     = strdup(client.name);
    client_creds->username       = strdup(client_request->username);
    client_creds->passphrase     = strdup(client_request->passphrase);
    client_creds->lifetime       = client_request->proxy_lifetime;
    if (client_request->retrievers != NULL)
        client_creds->retrievers = strdup(client_request->retrievers);
    if (client_request->keyretrieve != NULL)
        client_creds->keyretrieve = strdup(client_request->keyretrieve);
    if (client_request->trusted_retrievers != NULL)
        client_creds->trusted_retrievers =
            strdup(client_request->trusted_retrievers);
    if (client_request->renewers != NULL)
        client_creds->renewers   = strdup(client_request->renewers);
    if (client_request->credname != NULL)
        client_creds->credname   = strdup (client_request->credname);
    if (client_request->creddesc != NULL)
        client_creds->creddesc   = strdup (client_request->creddesc);

    /* Handle client request */
    switch (client_request->command_type) {
      case MYPROXY_GET_PROXY:

        if (caonly ||
            !myproxy_creds_exist(client_request->username,
                                 client_request->credname)) {
            use_ca_callout = 1;
        }
        /* fall through to MYPROXY_RETRIEVE_CERT */

      case MYPROXY_RETRIEVE_CERT:

        if (!use_ca_callout) {
            /* Retrieve the credentials from the repository */
            if (myproxy_creds_retrieve(client_creds) < 0) {
                respond_with_error_and_die(attrs, verror_get_string(), context);
            }

            myproxy_debug("  Owner: %s", client_creds->username);
            myproxy_debug("  Location: %s", client_creds->location);
            myproxy_debug("  Max. delegation lifetime: %d seconds",
                          client_creds->lifetime);
            if (context->max_proxy_lifetime) {
                myproxy_debug("  Server max_proxy_lifetime: %d seconds",
                              context->max_proxy_lifetime);
            }

            /* Are credentials locked? */
            if (client_creds->lockmsg) {
                char *error, *msg="credential locked\n";
                error = malloc(strlen(msg) + strlen(client_creds->lockmsg) + 1);
                strcpy(error, msg);
                strcat(error, client_creds->lockmsg);
                respond_with_error_and_die(attrs, error, context);
            }

            if (myproxy_creds_verify(client_creds) < 0) {
                myproxy_creds_free(client_creds);
                myproxy_free(NULL, client_request, server_response);
                respond_with_error_and_die(attrs, verror_get_string(), context);
            }
        }

        if (client_request->want_trusted_certs) {
      case MYPROXY_GET_TRUSTROOTS:

            if (context->cert_dir) {
                server_response->trusted_certs =
                    myproxy_get_certs(context->cert_dir);
                if (server_response->trusted_certs) {
                    myproxy_log("Sending trust roots to %s", client.name);
                } else {
                    myproxy_log("myproxy_get_certs() failed");
                    myproxy_log_verror();
                }
            } else {
                myproxy_log("WARNING: client requested trusted certificates but "
                            "cert_dir not configured");
            }
        }

        /* Send initial OK response */
        if (client_request->command_type != MYPROXY_GET_TRUSTROOTS) {
            send_response(attrs, server_response, client.name, 0);
            /* Any trustroots wanted as addl. info would have been sent
               in this send.  No need to send them again later. */
            if (server_response->trusted_certs) {
                myproxy_certs_free(server_response->trusted_certs);
                server_response->trusted_certs = NULL;
                context->usage.trustroots_sent = 1;
            }
        }

        if (client_request->command_type == MYPROXY_GET_PROXY)
        {
            /* Delegate the credential and set final server_response */

            if (use_ca_callout) {
                context->usage.ca_used = 1;
                myproxy_debug("using CA callout");
                get_certificate_authority(attrs, client_creds, client_request,
                                          server_response, context);
            } else {
                myproxy_debug("retrieving proxy");
                if (context->proxy_extfile) {
                    if (myproxy_set_extensions_from_file(context->proxy_extfile) < 0) {
                        myproxy_log("myproxy_set_extensions_from_file() failed");
                        myproxy_log_verror(); verror_clear();
                    }
                } else if (context->proxy_extapp) {
                    if (myproxy_set_extensions_from_callout(context->proxy_extapp,
                        client_request->username, client_creds->location) < 0) {
                        myproxy_log("myproxy_set_extensions_from_callout() failed");
                        myproxy_log_verror(); verror_clear();
                    }
                }
                if (have_voms != 0 && get_voms_proxy_impl != NULL &&
                    client_request->voname != NULL &&
                    context->allow_voms_attribute_requests) {
                    get_voms_proxy_impl(attrs, client_creds, client_request,
                                        server_response,
                                        context);
                }
                else
                    get_proxy(attrs, client_creds, client_request, server_response,
                              context->max_proxy_lifetime);
            }
        }
        else if (client_request->command_type == MYPROXY_RETRIEVE_CERT)
        {
            /* Delegate the credential and set final server_response */
            get_credentials(attrs, client_creds, client_request, server_response,
                            context->max_proxy_lifetime);
        }
        break;

      case MYPROXY_PUT_PROXY:
        if (myproxy_check_passphrase_policy(client_request->passphrase,
                                            context->passphrase_policy_pgm,
                                            client_request->username,
                                            client_request->credname,
                                            client_request->retrievers,
                                            client_request->renewers,
                                            client.name) < 0) {
            myproxy_creds_free(client_creds);
            myproxy_free(NULL, client_request, server_response);
            respond_with_error_and_die(attrs, verror_get_string(), context);
        }

        /* Send initial OK response */
        send_response(attrs, server_response, client.name, 0);

        /* Store the credentials in the repository and
           set final server_response */
        put_proxy(attrs, client_creds, server_response,
                  context->max_cred_lifetime);
        break;

      case MYPROXY_INFO_PROXY:
        info_proxy(client_creds, server_response);
        if (server_response->info_creds == client_creds) {
            client_creds = NULL; /* avoid potential double-free */
        }
        break;
      case MYPROXY_DESTROY_PROXY:
        destroy_proxy(client_creds, server_response);
        break;

      case MYPROXY_CHANGE_CRED_PASSPHRASE:
        /* change credential passphrase*/
        if (myproxy_check_passphrase_policy(client_request->new_passphrase,
                                            context->passphrase_policy_pgm,
                                            client_request->username,
                                            client_request->credname,
                                            client_request->retrievers,
                                            client_request->renewers,
                                            client.name) < 0) {
            myproxy_creds_free(client_creds);
            myproxy_free(NULL, client_request, server_response);
            respond_with_error_and_die(attrs, verror_get_string(), context);
        }

        change_passwd(client_creds, client_request->new_passphrase,
                      server_response);
        break;

      case MYPROXY_STORE_CERT:
        /* Store the end-entity credential */
        /* Send initial OK response */
        send_response(attrs, server_response, client.name, 0);

        /* Store the credentials in the repository and
           set final server_response */
        put_credentials(attrs, client_creds, server_response,
                        context->max_cred_lifetime);
        break;

      default:
        server_response->error_string = strdup("Unknown command.\n");
        break;
    }

    /* return server response */
    /* ignore any send errors for this final OK message since currently some clients
       may close without waiting for this terminating message to be received
       due to a timing issue */
    send_response(attrs, server_response, client.name, 1 /* ignore net errors */);

    if (server_response->trusted_certs) {
        context->usage.trustroots_sent = 1;
    }

    /* Log request */
    myproxy_log("Client %s disconnected", client.name);

    /* free stuff up */
    myproxy_creds_free(client_creds);
    myproxy_free(attrs, client_request, server_response);
    myproxy_free_extensions();

    if (client.fqans) {
        char **p;
        for (p = client.fqans; p && *p; p++)
            free(*p);
        free(client.fqans);
    }

    return 0;
}

int
init_arguments(int argc, char *argv[],
               myproxy_socket_attrs_t *attrs,
               myproxy_server_context_t *context)
{
    extern char *optarg;

    int arg;
    int arg_error = 0;

    char *last_directory_seperator;
    char directory_seperator = '/';

    /* NULL implies INADDR_ANY */
    attrs->pshost = NULL;

    if (getenv("MYPROXY_SERVER_PORT")) {
        attrs->psport = atoi(getenv("MYPROXY_SERVER_PORT"));
    } else {
        attrs->psport = MYPROXY_SERVER_PORT;
    }

    /* Get my name, removing any preceding path */
    last_directory_seperator = strrchr(argv[0], directory_seperator);

    if (last_directory_seperator == NULL)
    {
        context->my_name = strdup(argv[0]);
    }
    else
    {
        context->my_name = strdup(last_directory_seperator + 1);
    }

    while((arg = getopt_long(argc, argv, short_options,
                             long_options, NULL)) != EOF)
    {
        switch(arg)
        {
          case 'l':       /* listen to hostname / ipaddr */
            attrs->pshost = strdup(optarg);
            break;
          case 'p':       /* port */
            attrs->psport = atoi(optarg);
            break;
          case 'P':       /* pidfile */
            context->pidfile = strdup(optarg);
            break;
          case 'z':       /* portfile */
            context->portfile = strdup(optarg);
            break;
          case 'h':       /* print help and exit */
            printf("%s", usage);
            exit(0);
            break;
          case 'c':
            context->config_file = malloc(strlen(optarg) + 1);
            strcpy(context->config_file, optarg);
            break;
          case 'v':
            myproxy_debug_set_level(1);
            break;
          case 'V':       /* print version and exit */
            printf("%s", version);
            exit(0);
            break;
          case 's':       /* set the credential storage directory */
            myproxy_set_storage_dir(optarg);
            break;
          case 'u':       /* print version and exit */
            printf("%s", usage);
            exit(0);
            break;
          case 'd':
            debug = 1;
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
        arg_error = -1;
    }

    return arg_error;
}

static int
bind_socket(const char *hostname, int port)
{
    int sock = -1;
    struct addrinfo hints, *res, *ressave;
    int on = 1;
    struct linger lin = {0,0};
    int n;
    char portstr[6] = {0}, *portstrp = NULL;

    /* getaddrinfo() requires either hostname or port to be set */
    assert(hostname || port);

    if (port) {
        snprintf(portstr, 6, "%d", port);
        portstrp=portstr;
    }

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags    = AI_PASSIVE;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    n = getaddrinfo(hostname, portstrp, &hints, &res);
    if (n < 0) {
        myproxy_log("getaddrinfo error: %s", gai_strerror(n));
        return -1;
    }
    ressave=res;

    while (res) {
        char chosenhost[NI_MAXHOST] = { 0 };
        char chosenport[NI_MAXSERV] = { 0 };

        getnameinfo(res->ai_addr, res->ai_addrlen,
                    chosenhost, sizeof(chosenhost),
                    chosenport, sizeof(chosenport),
                    NI_NUMERICHOST|NI_NUMERICSERV);

        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

        if (sock >= 0) {

            /* Allow reuse of socket */
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on));
            setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&lin, sizeof(lin));

            if (bind(sock, res->ai_addr, res->ai_addrlen) == 0) {
                myproxy_log("Socket bound to %s:%s", chosenhost, chosenport);
                break;
            }

            if (errno == EADDRINUSE) {
                myproxy_log("Port %s on %s already in use, probably by another "
                    "myproxy-server instance.\nUse the -p option to run "
                    "multiple myproxy-server instances on different "
                    "ports.", chosenport, chosenhost);
            } else {
                myproxy_log("Failed to bind socket to %s:%s: %s",
                            chosenhost, chosenport, strerror(errno));
            }
            close(sock);
            sock = -1;
        } else {
            myproxy_log("Failed to create socket for %s:%s: %s",
                        chosenhost, chosenport, strerror(errno));
        }
        res = res->ai_next;
    }

    freeaddrinfo(ressave);

    return sock;
}

/*
 * myproxy_init_server()
 *
 * Create a generic server socket ready on the given port ready to accept.
 *
 * returns the listener fd on success
 */
int
myproxy_init_server(myproxy_socket_attrs_t *attrs)
{
    int listen_sock = -1;
    GSI_SOCKET *tmp_gsi_sock;

    if ((tmp_gsi_sock = GSI_SOCKET_new(0)) == NULL) {
        failure("malloc() failed in GSI_SOCKET_new()");
    }
    if (GSI_SOCKET_check_creds(tmp_gsi_sock) == GSI_SOCKET_ERROR) {
        char error_string[1024] = { 0 };
        GSI_SOCKET_get_error_string(tmp_gsi_sock, error_string,
                                    sizeof(error_string));
        myproxy_log("Problem with server credentials.\n%s\n",
                    error_string);
        exit(1);
    }
    GSI_SOCKET_destroy(tmp_gsi_sock);

    if (attrs->pshost || attrs->psport) {
        myproxy_debug("using getaddrinfo() to configure listen socket");
        listen_sock = bind_socket(attrs->pshost, attrs->psport);
    } else { /* just create unbound IPv4 socket for now */
        int on = 1;
        struct linger lin = {0,0};

        myproxy_debug("creating IPv4 listen socket without binding");
        listen_sock = socket(AF_INET, SOCK_STREAM, 0);

        /* Allow reuse of socket */
        setsockopt(listen_sock,
                   SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on));
        setsockopt(listen_sock,
                   SOL_SOCKET, SO_LINGER, (char *)&lin, sizeof(lin));
    }
    if (listen_sock == -1) {
        failure("Error creating server socket");
    }

    if (listen(listen_sock, INT_MAX) < 0) {
        failure("Error in listen()");
    }

    if (attrs->psport == 0) {
        struct sockaddr_storage addr;
        socklen_t socklen = sizeof(addr);

        if (getsockname(listen_sock, (struct sockaddr *) &addr, &socklen) < 0) {
            failure("Error in getsockname()");
        }
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in saddr;
            memcpy(&saddr, &addr, sizeof(saddr));
            attrs->psport = ntohs(saddr.sin_port);
        }
#ifdef AF_INET6
        else if (addr.ss_family == AF_INET6) {
            struct sockaddr_in6 saddr6;
            memcpy(&saddr6, &addr, sizeof(saddr6));
            attrs->psport = ntohs(saddr6.sin6_port);
        }
#endif
    }

    /* Got this far? Then log success! */
    myproxy_log("Starting myproxy-server on %s:%d...",
                ((attrs->pshost == NULL) ? "*" : attrs->pshost),
                attrs->psport);

    return listen_sock;
}

void
respond_with_error_and_die(myproxy_socket_attrs_t *attrs,
                           const char *error, myproxy_server_context_t *context)
{
    myproxy_response_t          response = {0}; /* initialize with 0s */
    int                         responselen;
    char                        *response_buffer = NULL;


    memset (&response, 0, sizeof (response));
    response.version = strdup(MYPROXY_VERSION);
    response.response_type = MYPROXY_ERROR_RESPONSE;
    response.authorization_data = NULL;
    response.error_string = strdup(error);

    responselen = myproxy_serialize_response_ex(&response,
                                                &response_buffer);

    if (responselen < 0) {
        my_failure_chld("error in myproxy_serialize_response()");
    }

    if (myproxy_send(attrs, response_buffer, responselen) < 0) {
        my_failure_chld("error in myproxy_send()\n");
    }

    myproxy_log("Exiting: %s", error);

    myproxy_free(attrs, NULL, NULL);

    if(debug) exit(1); else _exit(1);
}

void send_response(myproxy_socket_attrs_t *attrs, myproxy_response_t *response,
                   char *client_name, int ignore_net_error)
{
    char *server_buffer = NULL;
    int responselen;
    assert(response != NULL);

    /* set version */
    response->version = malloc(strlen(MYPROXY_VERSION) + 1);
    sprintf(response->version, "%s", MYPROXY_VERSION);

    responselen = myproxy_serialize_response_ex(response, &server_buffer);

    if (responselen < 0) {
        my_failure_chld("error in myproxy_serialize_response()");
    }

    /* Log response */
    if (response->response_type == MYPROXY_OK_RESPONSE) {
        myproxy_debug("Sending OK response to client %s", client_name);
    } else if (response->response_type == MYPROXY_ERROR_RESPONSE) {
        myproxy_debug("Sending ERROR response \"%s\" to client %s",
                      response->error_string, client_name);
    }

    if (myproxy_send(attrs, server_buffer, responselen) < 0) {
        int error_number = GSI_SOCKET_get_errno(attrs->gsi_socket);

        myproxy_log_verror();
        if (!(ignore_net_error &&
              (error_number == EPIPE ||
               error_number == ECONNRESET)))
            my_failure_chld("error in myproxy_send()\n");
    }
    free(response->version);
    response->version = NULL;
    free(server_buffer);

    return;
}

/**********************************************************************
 *
 * Routines to handle client requests to the server.
 *
 */

/* Delegate requested credentials to the client */
void get_proxy(myproxy_socket_attrs_t *attrs,
               myproxy_creds_t *creds,
               myproxy_request_t *request,
               myproxy_response_t *response,
               int max_proxy_lifetime)
{
    int lifetime = 0;

    if (request->proxy_lifetime > 0) {
        lifetime = request->proxy_lifetime;
    }
    if (creds->lifetime > 0) {
        if (lifetime > 0) {
            lifetime = MIN(lifetime, creds->lifetime);
        } else {
            lifetime = creds->lifetime;
        }
    }
    if (max_proxy_lifetime > 0) {
        if (lifetime > 0) {
            lifetime = MIN(lifetime, max_proxy_lifetime);
        } else {
            lifetime = max_proxy_lifetime;
        }
    }

    if (myproxy_init_delegation(attrs, creds->location, lifetime,
                                request->passphrase) < 0) {
        myproxy_log_verror();
        response->response_type = MYPROXY_ERROR_RESPONSE;
        response->error_string = strdup("Unable to delegate credentials.\n");
    } else {
        myproxy_log("Delegating credentials for %s lifetime=%d",
                    creds->owner_name, lifetime);
        response->response_type = MYPROXY_OK_RESPONSE;
    }
}

/* Delegate requested credentials to the client */
void get_credentials(myproxy_socket_attrs_t *attrs,
                     myproxy_creds_t        *creds,
                     myproxy_request_t      *request,
                     myproxy_response_t     *response,
                     int                     max_proxy_lifetime)
{
    if (myproxy_get_credentials(attrs, creds->location) < 0) {
        myproxy_log_verror();
        response->response_type = MYPROXY_ERROR_RESPONSE;
        response->error_string = strdup("Unable to retrieve credentials.\n");
    } else {
        myproxy_log("Sent credentials for %s", creds->owner_name);
        response->response_type = MYPROXY_OK_RESPONSE;
    }
}


/* Accept delegated credentials from client */
void put_proxy(myproxy_socket_attrs_t *attrs,
               myproxy_creds_t *creds,
               myproxy_response_t *response,
               int max_cred_lifetime)
{
    char delegfile[MAXPATHLEN] = { 0 };

    if (myproxy_accept_delegation(attrs, delegfile, sizeof(delegfile),
                                  creds->passphrase) < 0) {
        myproxy_log_verror();
        response->response_type = MYPROXY_ERROR_RESPONSE;
        response->error_string = strdup("Failed to accept credentials.\n");
        return;
    }

    myproxy_debug("  Accepted delegation: %s", delegfile);

    check_and_store_credentials(delegfile, creds, response, max_cred_lifetime);
}

/* Accept end-entity credentials from client */
void put_credentials(myproxy_socket_attrs_t *attrs,
                     myproxy_creds_t        *creds,
                     myproxy_response_t     *response,
                     int                     max_cred_lifetime)
{
    char delegfile[MAXPATHLEN] = { 0 };

    if (myproxy_accept_credentials(attrs,
                                   delegfile,
                                   sizeof(delegfile)) < 0)
    {
        myproxy_log_verror();
        response->response_type = MYPROXY_ERROR_RESPONSE;
        response->error_string = strdup("Failed to accept credentials.\n");
        return;
    }

    myproxy_debug("  Accepted credentials: %s", delegfile);

    check_and_store_credentials(delegfile, creds, response, max_cred_lifetime);
}

void check_and_store_credentials(const char              path[],
                                 myproxy_creds_t        *creds,
                                 myproxy_response_t     *response,
                                 int                     max_cred_lifetime)
{
    time_t cred_expiration = 0;
    int cred_lifetime = 0;

    if (ssl_verify_cred(path) < 0) {
        myproxy_log_verror();
        response->response_type = MYPROXY_ERROR_RESPONSE;
        response->error_string = strdup("Credentials are not valid.\n");
        goto cleanup;
    }

    if (max_cred_lifetime) {
        ssl_get_times(path, NULL, &cred_expiration);
        if (cred_expiration == 0) {
            myproxy_log_verror();
            response->response_type = MYPROXY_ERROR_RESPONSE;
            response->error_string =
                strdup("Unable to get expiration time from credentials.\n");
            goto cleanup;
        }
        cred_lifetime = cred_expiration-time(0);
        if (cred_lifetime <= 0) {
            response->response_type = MYPROXY_ERROR_RESPONSE;
            response->error_string =
                strdup("Credential expired!\n");
            goto cleanup;
        }
                            /* up to 1hr clock skew*/
        if (cred_lifetime > max_cred_lifetime + 3599) {
            char errstr[200];
            response->response_type = MYPROXY_ERROR_RESPONSE;
            snprintf(errstr, 200, "Credential lifetime (%d hours) exceeds maximum allowed by server (%d hours).\n", cred_lifetime/60/60, max_cred_lifetime/60/60);
            response->error_string = strdup(errstr);
            goto cleanup;
        }
    }

    creds->location = strdup(path);

    if (myproxy_creds_store(creds) < 0) {
        myproxy_log_verror();
        response->response_type = MYPROXY_ERROR_RESPONSE;
        response->error_string = strdup("Unable to store credentials.\n");
    } else {
        response->response_type = MYPROXY_OK_RESPONSE;
    }

cleanup:
    /* Clean up temporary delegation */
    if (path[0]) ssl_proxy_file_destroy(path);
}

void info_proxy(myproxy_creds_t *creds, myproxy_response_t *response) {
    if (myproxy_creds_retrieve_all(creds) < 0) {
        myproxy_log_verror();
        response->response_type = MYPROXY_ERROR_RESPONSE;
        response->error_string = strdup(verror_get_string());
    } else {
        response->response_type = MYPROXY_OK_RESPONSE;
        response->info_creds = creds; /* beware shallow copy here */
    }
}

void destroy_proxy(myproxy_creds_t *creds, myproxy_response_t *response) {

    myproxy_debug("Deleting credentials for username \"%s\"", creds->username);
    myproxy_debug("  Owner is \"%s\"", creds->owner_name);
    myproxy_debug("  Delegation lifetime is %d seconds", creds->lifetime);

    if (myproxy_creds_delete(creds) < 0) {
        myproxy_log_verror();
        response->response_type = MYPROXY_ERROR_RESPONSE;
        response->error_string = strdup(verror_get_string());
    } else {
        response->response_type = MYPROXY_OK_RESPONSE;
    }

}

void change_passwd(myproxy_creds_t *creds, char *new_passphrase,
                   myproxy_response_t *response) {

    myproxy_debug("Changing pass phrase for username \"%s\"", creds->username);
    myproxy_debug("  Owner is \"%s\"", creds->owner_name);

    if (myproxy_creds_change_passphrase(creds, new_passphrase) < 0) {
        myproxy_log_verror();
        response->response_type = MYPROXY_ERROR_RESPONSE;
        response->error_string = strdup("Unable to change pass phrase.\n");
    } else {
        response->response_type = MYPROXY_OK_RESPONSE;
    }

}

/*
 * my_signal
 *
 * installs a signal handler, and returns the old handler.
 * This emulates the semi-standard signal() function in a
 * standard way using the Posix sigaction function.
 *
 * from Stevens, 1998, section 5.8
 */
Sigfunc *my_signal(int signo, Sigfunc *func)
{
    struct sigaction new_action, old_action;

    new_action.sa_handler = func;
    sigemptyset( &new_action.sa_mask );
    new_action.sa_flags = 0;

    if (signo == SIGALRM) {
#ifdef SA_INTERRUPT
        new_action.sa_flags |= SA_INTERRUPT;  /* SunOS 4.x */
#endif
    }
    else {
#ifdef SA_RESTART
        new_action.sa_flags |= SA_RESTART;    /* SVR4, 4.4BSD */
#endif
    }

    if (sigaction(signo, &new_action, &old_action) < 0) {
        return SIG_ERR;
    }
    else {
        return old_action.sa_handler;
    }
}

/* Signal handlers here.
   Call only asynchronous-safe functions!
   This means no logging! */
void
sig_chld(int signo) {
    pid_t pid;
    int   stat;

    while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0);
    return;
}

void sig_hup(int signo) {
    readconfig = 1;             /* set the flag */
}

void sig_exit(int signo) {
    if (listenfd >= 0) close(listenfd); /* force break out of accept() */
    cleanshutdown = 1;
}


static void
failure(const char *failure_message) {
    myproxy_log_perror("Failure: %s", failure_message);
    exit(1);
}

static void
my_failure(const char *failure_message) {
    myproxy_log("Failure: %s", failure_message);
    exit(1);
}

static void
my_failure_chld(const char *failure_message) {
    myproxy_log("Failure: %s", failure_message);
    if(debug) exit(1); else _exit(1);
}


static char *
timestamp(void)
{
    time_t clock;
    struct tm *tmp;

    time(&clock);
    tmp = (struct tm *)localtime(&clock);
    return (char *)asctime(tmp);
}

/* Do these steps right at the start. */
static int
become_daemon_step1()
{
    int fd = 0;
    int fdlimit;

    /* Steps taken from UNIX Programming FAQ */

    /* 4. `chdir("/")' to ensure that our process doesn't keep any directory in use */
    chdir("/");

    /* 5. umask(0077) as a defensive measure to restrict
    permissions on any files we create. */
    umask(0077);

    /* 6. Close all file descriptors */
    fdlimit = sysconf(_SC_OPEN_MAX);
    while (fd < fdlimit)
        close(fd++);

    /* 7.Establish new open descriptors for stdin, stdout and stderr */
    (void)open("/dev/null", O_RDWR);
    dup(0);
    dup(0);
#ifdef TIOCNOTTY
    fd = open("/dev/tty", O_RDWR);
    if (fd >= 0) {
        ioctl(fd, TIOCNOTTY, 0);
        (void)close(fd);
    }
#endif /* TIOCNOTTY */
    return 0;
}

/* Save fork() until after we've done some sanity checks. */
static int
become_daemon_step2()
{
    pid_t childpid;
    char byte = 1;

    /* Create a pipe to notify the original process when
      initialization is complete per
      http://0pointer.de/public/systemd-man/daemon.html */
    if (pipe(startup_pipe) < 0) {
        perror("Error in pipe()");
        return -1;
    }

    /* 1. Fork off a child so the new process is not a process group leader */
    childpid = fork();
    switch (childpid) {
      case 0:         /* child */
        close(startup_pipe[0]);
        break;
      case -1:        /* error */
        perror("Error in fork()");
        return -1;
      default:        /* exit the original process */
        close(startup_pipe[1]);
        read(startup_pipe[0], &byte, 1); /* wait for child to signal */
        close(startup_pipe[0]);
        _exit(byte);
    }

    /* 2. Set session id to become a process group and session group leader */
    if (setsid() < 0) {
        perror("Error in setsid()");
        return -1;
    }

    /* 3. Fork again so the parent, (the session group leader), can exit.
          This means that we, as a non-session group leader, can never
          regain a controlling terminal.
    */
    signal(SIGHUP, SIG_IGN);
    childpid = fork();
    switch (childpid) {
      case 0:             /* child */
        break;
      case -1:            /* error */
        perror("Error in fork()");
        return -1;
      default:            /* exit the original process */
        _exit(0);
    }

    return 0;
}

/* We're all done starting up, so signal the original process to exit. */
static int
become_daemon_step3(char status)
{
    write(startup_pipe[1], &status, 1);
    close(startup_pipe[1]);
    return 0;
}

static void
write_pfile(const char path[], long val)
{
    FILE *f = NULL;

    f = fopen(path, "wb");
    if (f == NULL) {
        myproxy_log("Couldn't create \"%s\": %s",
                    path, strerror(errno));
    } else {
        fprintf(f, "%ld\n", val);
        fclose(f);
    }
}

/*
 * check that all following conditions hold:
 * (1) the client_name matches the server-wide policy (eg authorized_retrievers)
 * (2) if the per-credential credential_policy isn't empty than the client_name
 *     is allowed by the policy
 * (3) if the per-credential credential_policy is empty and the server default
 *     policy is not than the client_name matches the server-wide policy
 *     (eg default_retrievers)
 */
static int myproxy_check_policy(myproxy_server_context_t *context,
                                myproxy_socket_attrs_t *attrs,
                                myproxy_server_peer_t *client,
                                const char *policy_name,
                                const char **server_policy,
                                const char *credential_policy,
                                const char **default_credential_policy)
{
    int authorization_ok = -1;

    myproxy_debug("applying %s policy", policy_name);
    authorization_ok = myproxy_server_check_policy_list_ext(server_policy, client);
    if (authorization_ok != 1) {
        verror_put_string("\"%s\" not authorized by server's %s policy",
                          client->name, policy_name);
        return authorization_ok;
    }

    if (credential_policy != NULL) {
        authorization_ok = myproxy_server_check_policy_ext(credential_policy, client);
        if (authorization_ok != 1) {
            verror_put_string("\"%s\" not authorized by credential's %s policy",
                              client->name, policy_name);
            return authorization_ok;
        }
    } else if (default_credential_policy != NULL) {
        authorization_ok = myproxy_server_check_policy_list_ext(default_credential_policy, client);
        if (authorization_ok != 1) {
            verror_put_string("\"%s\" not authorized by server's default %s policy",
                              client->name, policy_name);
            return authorization_ok;
        }
    }

    return authorization_ok;
}

static void
no_creds_abort(myproxy_socket_attrs_t *attrs, char username[], char credname[],
               myproxy_server_context_t *context)
{
    verror_clear();  /* don't distract with other errors */
    if (!credname) {
        verror_put_string("No credentials exist for username \"%s\".",
                          username);
    } else {
        verror_put_string("No credentials exist with username \"%s\" and credential name \"%s\".", username, credname);
    }
    respond_with_error_and_die(attrs, verror_get_string(), context);
}


/* Check if we're granting access to a certificate with the same
   identity as the requester (so-called "self-authz").
   If the request is to access a credential in the repository,
   check that. Otherwise, lookup the subject of the certificate
   we'd issue from the CA.
*/
static int
check_self_authz(myproxy_server_context_t *context,
                 myproxy_creds_t *creds,
                 myproxy_server_peer_t *client)
{
    char *subject = NULL;
    int rval = 1;               /* default allow */

    if (context->allow_self_authz == 0) {
        if (creds->location) {
            if (ssl_get_base_subject_file(creds->location, &subject)) {
                verror_put_string("internal error: ssl_get_base_subject_file(%s) failed in check_self_authz()", creds->location);
                return -1;          /* error */
            }
        } else {
            if (user_dn_lookup(creds->username,
                               &subject, context)) {
                verror_put_string("unknown username: %s",
                                  creds->username);
                return -1;          /* error */
            }
        }
        if (strcasecmp(client->name, subject) == 0) {
            verror_put_string("self-authz detected");
            rval = 0;           /* not allowed */
        }
    }

    if (subject)
        free(subject);


    return rval;
}


/* Check authorization for all incoming requests.  The authorization
 * rules are as follows.
 * RETRIEVE:
 *   Credentials must exist.
 *   Client DN must match server-wide authorized_key_retrievers policy.
 *   Client DN must match credential-specific authorized_key_retrievers policy.
 *   Also, see below.
 * RETRIEVE and GET with passphrase (credential retrieval):
 *   Client DN must match server-wide authorized_retrievers policy.
 *   Client DN must match credential-specific authorized_retrievers policy.
 *   Passphrase in request must match passphrase for credentials.
 * RETRIEVE and GET with certificate (credential renewal):
 *   Client DN must match server-wide authorized_renewers policy.
 *   Client DN must match credential-specific authorized_renewers policy.
 *   If !allow_self_authz, client DN must not match credential DN.
 *   DN in second X.509 authentication must match owner of credentials.
 *   Private key can not be encrypted in this case.
 * RETRIEVE and GET from trusted_retrievers:
 *   Client DN must match server-wide trusted_retrievers policy.
 *   Client DN must match credential-specific trusted_retrievers policy.
 *   If !allow_self_authz, client DN must not match credential DN.
 * GET_TRUSTROOTS:
 *   Client DN must match server-wide authorized_retrievers policy.
 * PUT, STORE, and DESTROY:
 *   If accepted_credentials_mapfile or accepted_credentials_mapapp,
 *   client_name / client_request->username map entry must be present/valid.
 *   Client DN must match accepted_credentials.
 *   If credentials already exist for the username, the client must own them.
 * INFO:
 *   Always allow here.  Ownership checking done in info_proxy().
 * CHANGE_CRED_PASSPHRASE:
 *   Client DN must match accepted_credentials.
 *   Client DN must match credential owner.
 *   Passphrase in request must match passphrase for credentials.
 */
static int
myproxy_authorize_accept(myproxy_server_context_t *context,
                         myproxy_socket_attrs_t *attrs,
                         myproxy_request_t *client_request,
                         myproxy_server_peer_t *client)
{
    int   credentials_exist = 0;
    int   client_owns_credentials = 0;
    int   authorization_ok = -1; /* 1 = success, 0 = failure, -1 = error */
    int   allowed_to_retrieve = 0;
    int   allowed_to_renew = 0;
    int   trusted_retriever = 0;
    int   return_status = -1;
    myproxy_creds_t creds = { 0 };
    char  *userdn = NULL;

    if (caonly) {
        switch (client_request->command_type) {
          case MYPROXY_GET_PROXY:
          case MYPROXY_GET_TRUSTROOTS:
            break;
          default:
            verror_put_string("command not supported by MyProxy CA");
            respond_with_error_and_die(attrs, verror_get_string(), context);
        }
    }

    if (client_request->command_type != MYPROXY_GET_TRUSTROOTS)
    {
        if (caonly) {
            credentials_exist = 0;
        } else {
            credentials_exist =
                myproxy_creds_exist(client_request->username,
                                    client_request->credname);
        }

        if (credentials_exist == -1) {
            myproxy_log_verror();
            verror_put_string("Error checking credential existence");
            goto end;
        }

        creds.username = strdup(client_request->username);
        if (client_request->credname) {
            creds.credname = strdup(client_request->credname);
        }

        if (credentials_exist) {
            if (myproxy_creds_retrieve(&creds) < 0) {
                verror_put_string("Unable to retrieve credential information");
                goto end;
            }

            context->usage.credentials_exist = credentials_exist;

            if (strcmp(creds.owner_name, client->name) == 0) {
                client_owns_credentials = 1;
            }
        }
    }

    switch (client_request->command_type) {
      case MYPROXY_RETRIEVE_CERT:
        authorization_ok =
            myproxy_check_policy(context, attrs, client,
                        "authorized_key_retrievers",
                        (const char **)context->authorized_key_retrievers_dns,
                        creds.keyretrieve,
                        (const char **)context->default_key_retrievers_dns);
        if (authorization_ok != 1)
            goto end;

        if (!credentials_exist) {
            no_creds_abort(attrs, client_request->username, client_request->credname,
                           context);
        }
        /* fall through to MYPROXY_GET_PROXY */

      case MYPROXY_GET_PROXY:
        /* check trusted_retrievers */
        authorization_ok =
            myproxy_check_policy(context, attrs, client,
                        "trusted_retrievers",
                        (const char **)context->trusted_retriever_dns,
                        creds.trusted_retrievers,
                        (const char **)context->default_trusted_retriever_dns);
        if (authorization_ok == 1) {
            if (check_self_authz(context, &creds, client) != 1) {
                myproxy_log_verror();
                myproxy_log("self-authz not allowed for trusted retriever");
            } else {
                trusted_retriever = 1;
                context->usage.trusted_retr = 1;
                myproxy_log("trusted retrievers policy matched");
            }
        }

        allowed_to_retrieve =
            myproxy_check_policy(context, attrs, client,
                   "authorized_retrievers",
                   (const char **)context->authorized_retriever_dns,
                   creds.retrievers,
                   (const char **)context->default_retriever_dns);

        allowed_to_renew =
            myproxy_check_policy(context, attrs, client,
                   "authorized_renewers",
                   (const char **)context->authorized_renewer_dns,
                   creds.renewers,
                   (const char **)context->default_renewer_dns);

        if (!allowed_to_retrieve && !allowed_to_renew) {
            goto end;
        }

        /* log non-fatal errors collected so far and clear them
           so we don't confuse the client with too much diagnostics */
        if (debug) myproxy_log_verror();
        verror_clear();

        /* if it appears that we need to use the ca callouts because
         * of no stored creds, we should check if the ca is configured
         * and if the user exists in the mapfile if not using the
         * external program callout.
         */
        if (!credentials_exist) {
            if ( (context->certificate_issuer_program == NULL) &&
                 (context->certificate_issuer_cert == NULL) ) {
                no_creds_abort(attrs, client_request->username,
                               client_request->credname, context);
            }

            if (context->certificate_issuer_cert) {

                if ( user_dn_lookup( client_request->username,
                                     &userdn, context ) ) {
                    verror_put_string("unknown username: %s",
                                      client_request->username);
                    respond_with_error_and_die(attrs, verror_get_string(), context);
                }
                if (userdn) {
                    free(userdn);
                    userdn = NULL;
                }
            }
        }

        /* this call may set context->limited_proxy */
        authorization_ok =
            authenticate_client(attrs, &creds, client_request, client->name,
                                context, trusted_retriever, allowed_to_renew);

        if (authorization_ok < 0) {
            if (!verror_is_error()) {
                /* if we don't have a good error message already,
                   it means we had insufficient authentication */
                if (client_request->passphrase[0] == '\0') {
                    verror_put_string("no passphrase");
                }
                verror_put_string("authentication failed");
            }
            goto end;            /* authentication failed */
        } else if (authorization_ok == 0) {
            authorization_ok = allowed_to_retrieve;
        } else if (authorization_ok == 1) { /* renewal */
            if (check_self_authz(context, &creds, client) != 1) {
                authorization_ok = -1;
                verror_put_string("self-authz not allowed for renewer");
            }
        }

        if (authorization_ok != 1) {
            goto end;
        }

        if (context->limited_proxy == -1) { /* config says ignore limited */
            GSI_SOCKET_set_peer_limited_proxy(attrs->gsi_socket, 0);
        } else if (context->limited_proxy == 1) {
            GSI_SOCKET_set_peer_limited_proxy(attrs->gsi_socket, 1);
        }

        if (GSI_SOCKET_peer_used_limited_proxy(attrs->gsi_socket)) {
            myproxy_debug("client authenticated with a limited proxy chain");
            if (!credentials_exist) {
                verror_put_string("MyProxy CA will not accept limited proxy for authentication.");
                authorization_ok = 0;
                goto end;
            }
            if (client_request->command_type == MYPROXY_RETRIEVE_CERT) {
                switch(ssl_limited_proxy_file(creds.location)) {
                  case 1:
                    break;       /* ok */
                  case 0:
                    verror_put_string("Client with limited proxy may not retrieve full credentials.");
                    authorization_ok = 0;
                    goto end;
                  default:
                    verror_put_string("Can't determine if credentials contain a limited proxy.");
                    authorization_ok = 0;
                    goto end;
                }
            }
        }
        break;

      case MYPROXY_GET_TRUSTROOTS:
        /* just check authorized_retrievers */
        authorization_ok = myproxy_check_policy(
            context, attrs, client, "authorized_retrievers",
            (const char **)context->authorized_retriever_dns, NULL, NULL);

        if (authorization_ok != 1) {
            verror_put_string("\"%s\" not authorized to retrieve credentials from this "
                              "server (authorized_retrievers policy)", client->name);
            goto end;
        }
        break;

      case MYPROXY_PUT_PROXY:
      case MYPROXY_STORE_CERT:
      case MYPROXY_DESTROY_PROXY:
        /* Check for a valid mapping in accepted_credentials_mapfile or
         * accepted_credentials_mapapp.  Note that accept_credmap returns 0
         * upon success (or if no check of mapfile/mapapp is needed). */
        if (accept_credmap(client->name,client_request->username,context)) {
            goto end;  /* No valid UserDN/Username mapping found! */
        }

        /* Is this client authorized to store credentials here? */
        authorization_ok =
            myproxy_server_check_policy_list_ext((const char **)context->accepted_credential_dns, client);
        if (authorization_ok != 1) {
            verror_put_string("\"%s\" not authorized to store credentials on this server (accepted_credentials policy)", client->name);
            goto end;
        }

        if (credentials_exist == 1) {
            if (!client_owns_credentials) {
                if ((client_request->command_type == MYPROXY_PUT_PROXY) ||
                    (client_request->command_type == MYPROXY_STORE_CERT)) {
                    verror_put_string("Credentials are already stored for user %s",
                                      client_request->username);
                    if (client_request->credname) {
                        verror_put_string("and credential name \"%s\"",
                                          client_request->credname);
                    }
                    verror_put_string("and they are not owned by\n\"%s\",",
                                      client->name);
                    verror_put_string("so you may not overwrite them.");
                    verror_put_string("Please choose a different username or credential name or");
                    verror_put_string("contact your myproxy-server administrator.");
                } else {
                    verror_put_string("Credentials not owned by \"%s\".",
                                      client->name);
                }
                goto end;
            }
        }
        break;

      case MYPROXY_INFO_PROXY:
        /* Authorization checking done inside the processing of the
           INFO request, since there may be multiple credentials stored
           under this username. */
        authorization_ok = 1;
        break;

      case MYPROXY_CHANGE_CRED_PASSPHRASE:
        if (!client_owns_credentials) {
            verror_put_string("'%s' does not own the credentials",
                              client->name);
            goto end;
        }

        authorization_ok = verify_passphrase(&creds, client_request,
                                             client->name, context);
        if (!authorization_ok) {
            verror_put_string("invalid pass phrase");
            goto end;
        }
        break;

      default:
        verror_put_string("unknown command");
        goto end;
    }

    if (authorization_ok == -1) {
        verror_put_string("Error checking authorization");
        goto end;
    }

    if (authorization_ok != 1) {
        verror_put_string("authorization failed");
        goto end;
    }

    return_status = 0;

end:
    if (creds.passphrase)
        memset(creds.passphrase, 0, strlen(creds.passphrase));
    myproxy_creds_free_contents(&creds);

    return return_status;
}

static int
do_authz_handshake(myproxy_socket_attrs_t *attrs,
                   struct myproxy_creds *creds,
                   myproxy_request_t *client_request,
                   char *client_name,
                   myproxy_server_context_t* config,
                   author_method_t methods[],
                   authorization_data_t *auth_data)
{
    myproxy_response_t server_response = {0};
    char  *client_buffer = NULL;
    int   client_length;
    int   return_status = -1;
    authorization_data_t *client_auth_data = NULL;
    author_method_t client_auth_method;

    assert(auth_data != NULL);

    memset(&server_response, 0, sizeof(server_response));

    myproxy_debug("sending MYPROXY_AUTHORIZATION_RESPONSE");
    authorization_init_server(&server_response.authorization_data, methods);
    server_response.response_type = MYPROXY_AUTHORIZATION_RESPONSE;
    send_response(attrs, &server_response, client_name, 0);

    /* Wait for client's response. Its first four bytes are supposed to
       contain a specification of the method that the client chose for
       authorization. */
    client_length = myproxy_recv_ex(attrs, &client_buffer);
    if (client_length <= 0)
        goto end;

    client_auth_method = (author_method_t)(*client_buffer);
    myproxy_debug("client chose %s",
                  authorization_get_name(client_auth_method));
    /* fill in the client's response and return pointer to filled data */
    client_auth_data = authorization_store_response(
                          client_buffer + sizeof(client_auth_method),
                          client_length - sizeof(client_auth_method),
                          client_auth_method,
                          server_response.authorization_data);
    if (client_auth_data == NULL)
        goto end;

    if (auth_data->server_data) free(auth_data->server_data);
    auth_data->server_data = strdup(client_auth_data->server_data);
    if (auth_data->client_data) free(auth_data->client_data);
    auth_data->client_data = malloc(client_auth_data->client_data_len);
    if (auth_data->client_data == NULL) {
        verror_put_string("malloc() failed");
        verror_put_errno(errno);
        goto end;
    }
    memcpy(auth_data->client_data, client_auth_data->client_data,
           client_auth_data->client_data_len);
    auth_data->client_data_len = client_auth_data->client_data_len;
    auth_data->method = client_auth_data->method;

#if defined(HAVE_LIBSASL2)
    if (auth_data->method == AUTHORIZETYPE_SASL) {
        config->usage.sasl_used = 1;
        if (auth_sasl_negotiate_server(attrs, client_request) < 0) {
            verror_put_string("SASL authentication failed");
            goto end;
        }
    }
#endif

    if (authorization_check_ex(auth_data, creds,
                               client_name, config) == 1) {
        return_status = 0;
    }

end:
    authorization_data_free(server_response.authorization_data);
    if (client_buffer) free(client_buffer);

    return return_status;
}

static int
verify_passphrase(struct myproxy_creds *creds,
                  myproxy_request_t *client_request,
                  char *client_name,
                  myproxy_server_context_t* config)
{
    authorization_data_t auth_data = { 0 };
    int return_status;
    auth_data.server_data = NULL;
    auth_data.client_data = strdup(client_request->passphrase);
    auth_data.client_data_len =
        strlen(client_request->passphrase) + 1;
    auth_data.method = AUTHORIZETYPE_PASSWD;
    return_status = authorization_check_ex(&auth_data, creds,
                                           client_name, config);
    free(auth_data.client_data);
    return return_status;
}

/* returns -1 if authentication failed,
            0 if authentication succeeded,
            1 if certificate-based (renewal) authentication succeeded */
static int
authenticate_client(myproxy_socket_attrs_t *attrs,
                    struct myproxy_creds *creds,
                    myproxy_request_t *client_request,
                    char *client_name,
                    myproxy_server_context_t* config,
                    int already_authenticated,
                    int allowed_to_renew)
{
    int return_status = -1, authcnt, certauth = 0;
    int i, j;
    author_method_t methods[AUTHORIZETYPE_NUMMETHODS] = { 0 };
    author_status_t status[AUTHORIZETYPE_NUMMETHODS] = { 0 };
    authorization_data_t auth_data = { 0 };

    authcnt = already_authenticated; /* if already authenticated, just
                                        do required methods */
    for (i = 0; i < AUTHORIZETYPE_NUMMETHODS; i++) {
        if ((i == AUTHORIZETYPE_CERT || i == AUTHORIZETYPE_CERT256) &&
            allowed_to_renew != 1) {
            status[i] = AUTHORIZEMETHOD_DISABLED;
        } else {
            status[i] = authorization_get_status(i, creds, client_name, config);
        }
    }

    /* First, check any required methods. */
    for (i = 0; i < AUTHORIZETYPE_NUMMETHODS; i++) {
        if (status[i] == AUTHORIZEMETHOD_REQUIRED) {
            /* password is a special case for now.
               don't send password challenges. */
            if (i == AUTHORIZETYPE_PASSWD) {
                if (verify_passphrase(creds, client_request,
                                      client_name, config) != 1) {
                    /* verify_passphrase() will set verror */
                    goto end;
                }
                authcnt++;
            } else {
                methods[0] = i;
                if (do_authz_handshake(attrs, creds, client_request,
                                       client_name, config,
                                       methods, &auth_data) < 0) {
                    verror_put_string("authentication failed");
                    goto end;
                }
                if (i == AUTHORIZETYPE_CERT || i == AUTHORIZETYPE_CERT256) {
                    certauth = 1;
                }
                authcnt++;
            }
        }
    }

    /* if none required, try sufficient */
    if (authcnt == 0) {
        /* if we already have a password, try it now */
        if (status[AUTHORIZETYPE_PASSWD] == AUTHORIZEMETHOD_SUFFICIENT &&
            client_request->passphrase[0] != '\0') {
            if (verify_passphrase(creds, client_request,
                                  client_name, config) == 1) {
                authcnt++;
            } else {
                /* if given password was bad,
                   fail immediately for a more helpful error message */
                /* verify_passphrase() will set verror */
                goto end;
            }
        }
    }
    if (authcnt == 0) {
        for (i = 0, j = 0; i < AUTHORIZETYPE_NUMMETHODS; i++) {
            if (status[i] == AUTHORIZEMETHOD_SUFFICIENT &&
                i != AUTHORIZETYPE_PASSWD) {
                methods[j++] = i;
            }
        }
        if (j > 0) {
            if (do_authz_handshake(attrs, creds, client_request, client_name,
                                   config, methods, &auth_data) < 0) {
                verror_put_string("authentication failed");
                goto end;
            }
            if (auth_data.method == AUTHORIZETYPE_CERT ||
                auth_data.method == AUTHORIZETYPE_CERT256) {
                certauth = 1;
            }
            authcnt++;
        }
    }

    if (certauth) {
        return_status = 1;
    } else if (authcnt) {
        return_status = 0;
    }

end:
    authorization_data_free_contents(&auth_data);
    return return_status;
}

