/*
 * myproxy-get-credential
 *
 * Webserver program to retrieve a end-entity credential from a myproxy-server
 */

#include "myproxy_common.h"     /* all needed headers included here */

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

static char usage[] = \
"\n"
"Syntax: myproxy-retrieve [-l username] ...\n"
"        myproxy-retrieve [-usage|-help] [-version]\n"
"\n"
"   Options\n"
"       -h | --help                       Displays usage\n"
"       -u | --usage                                    \n"
"                                                      \n"
"       -v | --verbose                    Display debugging messages\n"
"       -V | --version                    Displays version\n"
"       -l | --username        <username> Username for the delegated proxy\n"
"       -s | --pshost          <hostname> Hostname of the myproxy-server\n"
"       -p | --psport          <port #>   Port of the myproxy-server\n"
"       -a | --authorization   <path>     Specify credential to renew\n"
"       -d | --dn_as_username             Use subject of the authorization\n"
"                                         credential (or default credential\n"
"                                         if -a not used) as the default\n"
"                                         username instead of $LOGNAME\n"
"       -k | --credname        <name>     Specify credential name\n"
"       -c | --certfile        <filename> Certificate file name\n"
"       -y | --keyfile         <filename> Key file name\n"
"       -S | --stdin_pass                 Read passphrase from stdin\n"
"       -T | --trustroots                 Manage trust roots\n"
"       -n | --no_passphrase              Don't prompt for passphrase\n"
"\n";

struct option long_options[] =
{
    {"help",                   no_argument, NULL, 'h'},
    {"pshost",           required_argument, NULL, 's'},
    {"psport",           required_argument, NULL, 'p'},
    {"usage",                  no_argument, NULL, 'u'},
    {"username",         required_argument, NULL, 'l'},
    {"verbose",                no_argument, NULL, 'v'},
    {"version",                no_argument, NULL, 'V'},
    {"authorization",    required_argument, NULL, 'r'},
    {"dn_as_username",         no_argument, NULL, 'd'},
    {"credname",         required_argument, NULL, 'k'},
    {"stdin_pass",             no_argument, NULL, 'S'},
    {"no_passphrase",          no_argument, NULL, 'n'},
    {"certfile",         required_argument, NULL, 'c'},
    {"keyfile",          required_argument, NULL, 'y'},
    {"trustroots",             no_argument, NULL, 'T'},
    {0, 0, 0, 0}
};

static char short_options[] = "hus:p:l:t:c:y:vVa:dk:SnT";

static char version[] =
"myproxy-retrieve version " MYPROXY_VERSION " (" MYPROXY_VERSION_DATE ") "  "\n";

void
init_arguments(int argc, char *argv[],
               myproxy_socket_attrs_t *attrs,
               myproxy_request_t *request);

int
store_credential( char *delegfile,
                  char *certfile,
                  char *keyfile );

int
buffer2file( const char *buffer,
             int   size,
             int   fd );

int
write_cert( char       *path,
            const char *buffer );

int
write_key( char       *path,
           const char *buffer );

int
mkpath( char *path );

/*
 * Use setvbuf() instead of setlinebuf() since cygwin doesn't support
 * setlinebuf().
 */
#define my_setlinebuf(stream)   setvbuf((stream), (char *) NULL, _IOLBF, 0)

/* location of delegated proxy */
static char *certfile               = NULL;     /* certificate file name */
static char *keyfile                = NULL;     /* key file name */
static int   dn_as_username         = 0;
static int   read_passwd_from_stdin = 0;
static int   use_empty_passwd       = 0;

int
main(int argc, char *argv[])
{
    myproxy_socket_attrs_t *socket_attrs;
    myproxy_request_t      *client_request;
    myproxy_response_t     *server_response;
    char                   *pshost;
    char                    delegfile[MAXPATHLEN];
    char                   *request_buffer = NULL;
    int                     requestlen;
    int                     retval     = -1;
    int                     deletefile =  0;

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
    client_request->version = strdup(MYPROXY_VERSION);
    client_request->command_type = MYPROXY_RETRIEVE_CERT;

    pshost = getenv("MYPROXY_SERVER");
    if (pshost != NULL) {
        socket_attrs->pshost = strdup(pshost);
    }

    client_request->proxy_lifetime = 60*60*MYPROXY_DEFAULT_DELEG_HOURS;

    if (getenv("MYPROXY_SERVER_PORT")) {
        socket_attrs->psport = atoi(getenv("MYPROXY_SERVER_PORT"));
    } else {
        socket_attrs->psport = MYPROXY_SERVER_PORT;
    }

    if (getuid() == 0) {
        get_host_credential_filenames( &certfile, &keyfile );
    } else {
        get_user_credential_filenames( &certfile, &keyfile );
    }

    /* Initialize client arguments and create client request object */
    init_arguments(argc, argv, socket_attrs, client_request);

    if (!certfile && !keyfile) {
        fprintf(stderr, "Unable to determine credential output locations.\n"
                "Use --certfile and --keyfile options.\n");
        goto error;
    } else if (!certfile) {
        fprintf(stderr, "Unable to determine certificate output location.\n"
                "Use --certfile option.\n");
        goto error;
    } else if (!keyfile) {
        fprintf(stderr, "Unable to determine private key output location.\n"
                "Use --keyfile option.\n");
        goto error;
    }

    if (access(certfile, F_OK) == 0) {
        fprintf(stderr, "%s exists.\n", certfile);
        goto error;
    }

    if (access(keyfile, F_OK) == 0) {
        fprintf(stderr, "%s exists.\n", keyfile);
        goto error;
    }

    /* Bootstrap trusted certificate directory if none exists. */
    if (client_request->want_trusted_certs) {
        char *cert_dir = NULL;
        globus_result_t res;

        globus_module_activate(GLOBUS_GSI_CERT_UTILS_MODULE);
        res = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&cert_dir);
        if (res != GLOBUS_SUCCESS) {
            globus_object_free(globus_error_get(res));
            myproxy_bootstrap_trust(socket_attrs);
        }
        if (cert_dir) free(cert_dir);
    }

    /* Connect to server. */
    if (myproxy_init_client(socket_attrs) < 0) {
        verror_print_error(stderr);
        goto error;
    }

    /* Attempt anonymous-mode credential retrieval if we don't have a
       credential. */
    GSI_SOCKET_allow_anonymous(socket_attrs->gsi_socket, 1);

     /* Authenticate client to server */
    if (myproxy_authenticate_init(socket_attrs, NULL) < 0) {
        verror_print_error(stderr);
        if (client_request->want_trusted_certs &&
            strstr(verror_get_string(), "CRL") != NULL) {
            verror_clear();
            myproxy_log("CRL error detected.  Attempting to recover.");
            switch (myproxy_clean_crls()) {
            case -1:
                verror_print_error(stderr);
            case 0:
                goto error;
            case 1:
                if (myproxy_init_client(socket_attrs) < 0) {
                    verror_print_error(stderr);
                    goto error;
                }
                if (myproxy_authenticate_init(socket_attrs, NULL) < 0) {
                    verror_print_error(stderr);
                    goto error;
                }
            }
        } else {
            goto error;
        }
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
            goto error;
        }
    }

    if (client_request->username == NULL) { /* set default username */
        if (dn_as_username) {
            if (client_request->authzcreds) {
                if (ssl_get_base_subject_file(client_request->authzcreds,
                                              &client_request->username)) {
                    fprintf(stderr, "Cannot get subject name from %s\n",
                            client_request->authzcreds);
                    goto error;
                }
            } else {
                if (ssl_get_base_subject_file(NULL,
                                              &client_request->username)) {
                    fprintf(stderr,
                            "Cannot get subject name from your certificate\n");
                    goto error;
                }
            }
        } else {
            char *username = NULL;
            if (!(username = getenv("LOGNAME"))) {
                fprintf(stderr, "Please specify a username.\n");
                goto error;
            }
            client_request->username = strdup(username);
        }
    }

    /* Serialize client request object */
    requestlen = myproxy_serialize_request_ex(client_request, &request_buffer);
    if (requestlen < 0) {
        verror_print_error(stderr);
        goto error;
    }

    /* Send request to the myproxy-server */
    if (myproxy_send(socket_attrs, request_buffer, requestlen) < 0) {
        verror_print_error(stderr);
        goto error;
    }
    free(request_buffer);
    request_buffer = NULL;

    /* Continue unless the response is not OK */
    if (myproxy_recv_response_ex(socket_attrs, server_response,
                                 client_request) != 0) {
        verror_print_error(stderr);
        goto error;
    }

    /* Accept delegated credentials from server */
    deletefile = 1;
    if (myproxy_accept_credentials(socket_attrs, delegfile,
                                   sizeof(delegfile)) < 0) {
        verror_print_error(stderr);
        goto error;
    }

    if( store_credential( delegfile, certfile, keyfile ) < 0 )
    {
       fprintf( stderr, "Problem storing to: %s and %s\n", certfile, keyfile );
       goto error;
    }

    ssl_proxy_file_destroy(delegfile);

    /* host credentials should not be encrypted */
    if (getuid() == 0) {
        SSL_CREDENTIALS *creds;

        creds = ssl_credentials_new();
        ssl_private_key_load_from_file(creds, keyfile,
                                       client_request->passphrase, NULL);
        ssl_private_key_store_to_file(creds, keyfile, NULL);
        ssl_credentials_destroy(creds);
    }

    printf("Credentials for %s have been stored in\n%s and\n%s.\n",
           client_request->username, certfile, keyfile);

    /* Store file in trusted directory if requested and returned */
    if (client_request->want_trusted_certs) {
        if (server_response->trusted_certs != NULL) {
            if (myproxy_install_trusted_cert_files(server_response->trusted_certs) != 0) {
                verror_print_error(stderr);
                goto error;
            } else {
                char *path;
                path = get_trusted_certs_path();
        if (path) {
            printf("Trust roots have been installed in %s.\n", path);
            free(path);
        }
            }
        } else {
            myproxy_debug("Requested trusted certs but didn't get any.\n");
        }
    }

    retval = 0;

error:
    if (certfile) free(certfile);
    if (keyfile) free(keyfile);
    verror_clear();

    /* free memory allocated */
    myproxy_free(socket_attrs, client_request, server_response);

    if( deletefile )
    {
      ssl_proxy_file_destroy(delegfile);
    }

    return retval;
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
        case 's':       /* pshost name */
            attrs->pshost = strdup(optarg);
            break;
        case 'p':       /* psport */
            attrs->psport = atoi(optarg);
            break;
        case 'h':       /* print help and exit */
        case 'u':       /* print help and exit */
            printf("%s", usage);
            exit(0);
            break;
        case 'l':       /* username */
            request->username = strdup(optarg);
            break;
        case 'a':       /* special authorization */
            request->authzcreds = strdup(optarg);
            use_empty_passwd = 1;
            break;
        case 'n':       /* no passphrase */
            use_empty_passwd = 1;
            break;
        case 'v':
            myproxy_debug_set_level(1);
            break;
        case 'V':       /* print version and exit */
            printf("%s", version);
            exit(0);
            break;
        case 'd':       /* use the certificate subject (DN) as the default
                           username instead of LOGNAME */
            dn_as_username = 1;
            break;
        case 'k':       /* credential name */
            request->credname = strdup (optarg);
            break;
        case 'S':
            read_passwd_from_stdin = 1;
            break;
        case 'T':
            request->want_trusted_certs = 1;
            myproxy_debug("Requesting trusted certificates.\n");
            break;
        case 'c':       /* credential file name */
            if (certfile) free(certfile);
            certfile = strdup(optarg);
            break;
        case 'y':       /* key file name */
            if (keyfile) free(keyfile);
            keyfile = strdup(optarg);
            break;
        default:        /* print usage and exit */
            fprintf(stderr, "%s", usage);
            exit(1);
            break;
        }
    }

    /* Check to see if myproxy-server specified */
    if (attrs->pshost == NULL) {
        fprintf(stderr, "Unspecified myproxy-server.  Set the MYPROXY_SERVER environment variable to\nthe hostname of the myproxy-server or run with '-s server-hostname'.\n");
        exit(1);
    }

    return;
}

int
store_credential( char *delegfile,
                  char *certfile,
                  char *keyfile )
{
    unsigned char       *input_buffer       = NULL;
    int                 retval              = -1;


    assert(delegfile != NULL);
    assert(certfile != NULL);
    assert(keyfile != NULL);

    if (buffer_from_file(delegfile, &input_buffer, NULL) < 0)
    {
      fprintf(stderr, "open(%s) failed: %s\n", delegfile, strerror(errno));
      goto error;
    }

    if (write_cert(certfile, (const char *)input_buffer) < 0)
    {
      goto error;
    }

    if (write_key(keyfile, (const char *)input_buffer) < 0)
    {
      goto error;
    }

    retval = 0;
error:
    free(input_buffer);
    return(retval);
}

int
write_cert( char       *path,
            const char *buffer )
{
    int          fd = 0;
    static char  BEGINCERT[] = "-----BEGIN CERTIFICATE-----";
    static char  ENDCERT[]   = "-----END CERTIFICATE-----";
    const char  *certstart,
                *certend;
    int          retval      = -1;
    int          size;

    assert(path != NULL);
    assert(buffer != NULL);

    if( make_path( path ) < 0 )
    {
        verror_print_error(stderr);
        goto error;
    }

    /* Open the output file. */
    if ((fd = open(path, O_CREAT | O_EXCL | O_WRONLY,
                 S_IRUSR | S_IWUSR)) < 0)
    {
      if( errno == EEXIST )
      {
        fprintf(stderr, "open(%s) failed: This file already exists.\nmyproxy-retrieve will not overwrite end-entity credentials.\n", path );
        goto error;
      }

      fprintf(stderr, "Open(%s) failed: %s\n", path, strerror(errno));
      goto error;
    }

    if ((certstart = strstr(buffer, BEGINCERT)) == NULL)
    {
      fprintf(stderr, "CRED doesn't contain '%s'.\n",  BEGINCERT);
      goto error;
    }

    if ((certend = strstr(certstart, ENDCERT)) == NULL)
    {
      fprintf(stderr, "CRED doesn't contain '%s'.\n", ENDCERT);
      goto error;
    }
    certend += strlen(ENDCERT);
    size = certend-certstart;

    if( buffer2file( certstart, size, fd ) != 0 )
    {
      fprintf(stderr, "Could not write cert to: '%s'.\n", path);
      goto error;
    }

    certstart += size;

    while ((certstart = strstr(certstart, BEGINCERT)) != NULL) {

        if ((certend = strstr(certstart, ENDCERT)) == NULL) {
            fprintf(stderr, "Can't find matching '%s' in %s.\n", ENDCERT,
                    certfile);
            goto error;
        }
        certend += strlen(ENDCERT);
        size = certend-certstart;

        buffer2file( certstart, size, fd );
        certstart += size;
    }

    retval = 0;

error:
    if( fd >= 0 )
    {
      close( fd );
    }

    return( retval );
}

int
write_key( char       *path,
           const char *buffer )
{
    int          fd = 0;
    static char  BEGINKEY1[] = "-----BEGIN RSA PRIVATE KEY-----";
    static char  BEGINKEY2[] = "-----BEGIN PRIVATE KEY-----";
    static char  BEGINKEY3[] = "-----BEGIN ENCRYPTED PRIVATE KEY-----";
    static char  ENDKEY1[]   = "-----END RSA PRIVATE KEY-----";
    static char  ENDKEY2[]   = "-----END PRIVATE KEY-----";
    static char  ENDKEY3[]   = "-----END ENCRYPTED PRIVATE KEY-----";
    const char  *keystart,
                *keyend;
    int          retval     = -1;
    int          size;

    if( make_path( path ) < 0 )
    {
        verror_print_error(stderr);
        goto error;
    }

    /* Open the output file. */
    if ((fd = open(path, O_CREAT | O_EXCL | O_WRONLY,
                 S_IRUSR | S_IWUSR)) < 0)
    {
      if( errno == EEXIST )
      {
        fprintf(stderr, "open(%s) failed: This file already exists.\nmyproxy-retrieve will not overwrite end-entity credentials.\n", path );
        goto error;
      }

      fprintf(stderr, "open(%s) failed: %s\n", path, strerror(errno));
      goto error;
    }

    /* Write the key. */
    if ((keystart = strstr(buffer, BEGINKEY1)) == NULL
        && (keystart = strstr(buffer, BEGINKEY2)) == NULL
        && (keystart = strstr(buffer, BEGINKEY3)) == NULL)
    {
      fprintf(stderr, "CREDKEY doesn't contain '%s' nor '%s' nor '%s'.\n",
                                BEGINKEY1, BEGINKEY2, BEGINKEY3);
      goto error;
    }

    if ((keyend = strstr(keystart, ENDKEY1)) != NULL)
        keyend += strlen(ENDKEY1);
    else if ((keyend = strstr(keystart, ENDKEY2)) != NULL)
        keyend += strlen(ENDKEY2);
    else if ((keyend = strstr(keystart, ENDKEY3)) != NULL)
        keyend += strlen(ENDKEY3);
    else
    {
      fprintf(stderr, "CREDKEY doesn't contain '%s' nor '%s' nor '%s'.\n",
                                ENDKEY1, ENDKEY2, ENDKEY3);
      goto error;
    }

    size = keyend-keystart;

    if( buffer2file( keystart, size, fd ) != 0 )
    {
      fprintf(stderr, "Could not write key to: '%s'.\n", path);
      goto error;
    }

    retval = 0;

error:
    if( fd >= 0)
    {
      close( fd );
    }

    return( retval );
}

int
buffer2file( const char *buffer,
             int   size,
             int   fd )
{
    int   rval;
    const char *certstart;

    certstart = buffer;

    while (size)
    {
      if ((rval = write(fd, certstart, size)) < 0)
      {
          perror("write");
          return( -1 );
      }
      size -= rval;
      certstart += rval;
    }

    if (write(fd, "\n", 1) < 0)
    {
      perror("write");
      return(-1);
    }

    return( 0 );
}
