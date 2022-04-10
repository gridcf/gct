/*
 * myproxy.c
 *
 * See myproxy.h for documentation
 *
 */

#include "myproxy_common.h"     /* all needed headers included here */

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

/**********************************************************************
 *
 * Internal functions
 *
 */
static int convert_message(const char           *buffer,
                           const char           *varname,
                           int                  flags,
                           char                 **line);

/* Values for convert_message() flags */
#define CONVERT_MESSAGE_NO_FLAGS                0x0000
#define CONVERT_MESSAGE_ALLOW_MULTIPLE          0x0001

#define CONVERT_MESSAGE_DEFAULT_FLAGS           CONVERT_MESSAGE_NO_FLAGS
#define CONVERT_MESSAGE_KNOWN_FLAGS             CONVERT_MESSAGE_ALLOW_MULTIPLE


static int parse_command(const char                     *command_str,
                         myproxy_proto_request_type_t   *command_value);

/* returns 0 if character not found */
static int findchr (const char *p, const char c)
{
  int i = 0;

  while (*(p+i) != c && *(p+i) != '\0') i++;

  return (*(p+i) == '\0')?0:i;
}

static int countchr (const char *p, const char c)
{
    int i = 0;

    while (*p != '\0') {
        if (*p == c) i++;
        p++;
    }

    return i;
}

static int parse_add_creds (char *response_str, char ***pstrs, int *num_creds)
{
    char *p = response_str;
    int tmp = 0, len = 0;
    int idx = 0;
    int num_entries;
    char **strs;

    /* allocate memory for a string-list, returned to caller */
    num_entries = countchr(response_str, ',') + 1;
    *pstrs = strs = (char **)malloc(num_entries*sizeof(char *));

    do
    {
        tmp = findchr(p+len, ',');

        if (tmp == 0) /* last credential name */
        {
            size_t slen;
            slen = strlen (p+len);
            strs[idx] = (char *) malloc(slen + 1);
            if (strncpy (strs[idx], p+len, slen) == NULL)
                return -1;

            strs[idx++][slen] = '\0';
        }
        else
        {
            strs[idx] = (char *) malloc (tmp + 1);
            if (strncpy (strs[idx], p+len, tmp) == NULL)
                return -1;

            strs[idx++][tmp] = '\0';
        }

        len += (tmp + 1);
    }
    while (tmp != 0);

    assert(num_entries == idx);

    *num_creds = idx;
    return 0;
}

/**
 * Return the timeout for a socket connection.  This function checks
 * the environment varialbe "MYPROXY_SOCKET_TIMEOUT" and returns that
 * value if it is non-negative.  Otherwise, it returns a default
 * timeout value of 10 seconds.
 *
 * @return The timeout for a socket connection in seconds.  Default is 10.
 */
static int get_socket_timeout(void) {

    int retval = 10;
    char *timeoutStr = NULL;
    int timeout;

    if (getenv("MYPROXY_SOCKET_TIMEOUT")) {
        timeoutStr = getenv("MYPROXY_SOCKET_TIMEOUT");
        timeout = atoi(timeoutStr);
        if (timeout >= 0) {
            retval = timeout;
        }
    }
    return(retval);
}

/**
 * Check to see if a socket should be bound to a particular port in
 * a given range.  This function checks the environment variables
 * "MYPROXY_TCP_PORT_RANGE" and "GLOBUS_TCP_PORT_RANGE".  If either
 * is set, then we attempt to "bind" the passed-in socket to a port
 * in that range.  If no port range has been set, or the bind is
 * successful, return 1.  Otherwise return 0.  Note that the passed-in
 * socket is not freed here upon failure.  You should do that yourself.
 *
 * @param sockfd The previously created socket we want to try to bind
 *               to a port in a given range.
 * @return 1 if bind of socket to a port is successful or if no port
 *         range environment variable was specified.  0 otherwise.
 */
static int check_port_range(int sockfd, struct sockaddr *addr) {

    int retval = 1;   /* Assume success; 0 is failure */
    char *port_range;
    unsigned short port = 0, min_port = 0, max_port = 0;
    char *c;
    struct sockaddr_in sin;
#ifdef AF_INET6
    struct sockaddr_in6 sin6;
#endif

    if ((port_range = getenv("MYPROXY_TCP_PORT_RANGE")) ||
        (port_range = getenv("GLOBUS_TCP_PORT_RANGE"))) {

        /* Replace comma in port range with space */
        c = strchr(port_range, ',');
        if (c) {
            *c = ' ';
        }

        if (addr->sa_family == AF_INET) {
            memcpy(&sin, addr, sizeof(sin));
            sin.sin_addr.s_addr = INADDR_ANY;
        }
#ifdef AF_INET6
        else if (addr->sa_family == AF_INET6) {
            memcpy(&sin6, addr, sizeof(sin6));
            sin6.sin6_addr = in6addr_any;
        }
#endif
        else {
            verror_put_string("unknown IP address type");
            return 0;
        }

        if (sscanf(port_range, "%hu %hu", &min_port, &max_port) == 2) {
            int bind_rval = 0;

            port = min_port;
            if (addr->sa_family == AF_INET) {
                sin.sin_port = htons(port);
                bind_rval = bind(sockfd,
                                 (struct sockaddr *)&sin, sizeof(sin));
            }
#ifdef AF_INET6
            else if (addr->sa_family == AF_INET6) {
                sin6.sin6_port = htons(port);
                bind_rval = bind(sockfd,
                                 (struct sockaddr *)&sin6, sizeof(sin6));
            }
#endif
            while (bind_rval < 0) {
                if (errno != EADDRINUSE) {
                    verror_put_errno(errno);
                    verror_put_string("Error in bind()");
                    retval = 0;  /* bind failed */
                    break;
                } else if (port >= max_port) {
                    verror_put_string(
                        "No available ports in range %hu-%hu.",
                        min_port, max_port);
                    retval = 0; /* no available port */
                    break;
                }

                if (addr->sa_family == AF_INET) {
                    sin.sin_port = htons(++port);
                    bind_rval = bind(sockfd,
                                     (struct sockaddr *)&sin, sizeof(sin));
                }
#ifdef AF_INET6
                else if (addr->sa_family == AF_INET6) {
                    sin6.sin6_port = htons(++port);
                    bind_rval = bind(sockfd,
                                     (struct sockaddr *)&sin6, sizeof(sin6));
                }
#endif
            }
            if (retval == 1) {
                myproxy_debug("Socket bound to port %hu.\n", port);
            }
        } else {
            verror_put_errno(errno);
            verror_put_string("Error parsing port range (%s)", port_range);
            retval = 0;
        }
    }
    return(retval);
}

/**
 * Attempt to connect to a given socket with a timeout (defaults to 10
 * seconds).  This function attemps to duplicate the standard "connect()"
 * function, however with a timeout for unsuccessful connections (using
 * "select()").  "get_socket_timeout()" is called to find how long a
 * socket connection should wait before timing out.  Upon successful
 * connection, 0 is returned.  Otherwise -1 is returned.
 *
 * @param sockfd A socket file descriptor to connect to.
 * @param serv_addr a sockaddr struct which has been populated by the
 *        appropriate values for the connection
 * @param addrlen The size of the serv_addr struct.
 * @return 0 if successfully connected. -1 otherwise.
 */
static int connect_with_timeout(int sockfd,
    const struct sockaddr *serv_addr, socklen_t addrlen) {

    struct timeval tv;
    int flags, res;
    fd_set rset, wset;
    socklen_t slen;
    int optval;

    tv.tv_sec = get_socket_timeout();
    tv.tv_usec = 0;

    /* Set socket to be non-blocking */
    if ((flags = fcntl(sockfd, F_GETFL, NULL)) < 0) {
        return(flags);
    }
    flags |= O_NONBLOCK;
    if ((res = fcntl(sockfd, F_SETFL, flags)) < 0) {
        return(res);
    }

    /* Try to connect to socket with a timeout */
    res = connect(sockfd, (struct sockaddr *)serv_addr, addrlen);
    if (res < 0) { /* Couldn't connect right away, try select() */
        if (errno == EINPROGRESS) { /* connect in progress, now try select */
            do {
                FD_ZERO(&rset);
                FD_SET(sockfd,&rset);
                wset = rset;
                res = select(sockfd + 1, &rset, &wset, NULL, &tv);
                if (res < 0) {
                    if (errno != EINTR) { /* Error connecting */
                        return(res);
                    }
                } else if (res > 0) { /* Socket selected for write */
                    slen = sizeof(int);
                    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR,
                                   (void*)(&optval), &slen) < 0) {
                        return(-1);
                    }
                    if (optval) { /* Error in delayed connection */
                        errno = optval;
                        return(-1);
                    }
                    break; /* out of do...while loop - good so far*/
                } else { /* res == 0 -> timeout in select() */
                    errno = ETIMEDOUT;
                    return(-1);
                }
            } while (1);
        } else {
            return(-1);
        }
    }

    /* Made it this far -> success.  Set socket to blocking mode again. */
    if ((flags = fcntl(sockfd, F_GETFL, NULL)) < 0) {
        return(flags);
    }
    flags &= (~O_NONBLOCK);
    if ((res = fcntl(sockfd, F_SETFL, flags)) < 0) {
        return(res);
    }

    return(0);
}

/**
 * Attempt to connect a socket to a specific host/port.
 * This function attempts to connect a socket
 * (with a timeout) to a given host:port.  If the host is given as a
 * FQDN which resolves to multiple IPs, we loop through the IPs until we
 * have successfully connected or we cannot find a valid IP to connect to.
 *
 * @param host The FQDN of a host to attempt to connect to.
 * @param port The port to connect to.
 * @return socket descriptor upon successful connection, -1 otherwise
 */
static int connect_socket_to_host(char *host, int port) {

    struct addrinfo hints, *res, *ressave;
    char service[6];
    int sockfd = -1;
    int n;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(service, 6, "%d", port);

    n = getaddrinfo(host, service, &hints, &res);
    if (n < 0) {
        verror_put_string("Unknown host \"%s\"\n", host);
        return(-1);
    }

    ressave = res;

    while (res) {
        sockfd = socket(res->ai_family,
                        res->ai_socktype,
                        res->ai_protocol);

        if (!(sockfd < 0)) {
            if (check_port_range(sockfd, res->ai_addr)) {
                char straddr[INET6_ADDRSTRLEN];
                getnameinfo(res->ai_addr, res->ai_addrlen,
                            straddr, sizeof(straddr),
                            NULL, 0, NI_NUMERICHOST);
                myproxy_debug("Attempting to connect to %s:%d\n", straddr, port);
                if (connect_with_timeout(sockfd,
                                         res->ai_addr, res->ai_addrlen) < 0) {
                    verror_put_errno(errno);
                    verror_put_string("Unable to connect to %s:%d\n", straddr,port);
                } else { /* Success! */
                    break; /* out of while loop */
                }
            } /* check_port_range() */
            close(sockfd);
            sockfd = -1;
        }
        res=res->ai_next;
    }

    freeaddrinfo(ressave);
    return(sockfd);
}

/**
 * Loop through a list of MyProxy hosts trying to get a connected
 * socket.  This function takes in a list of MyProxy hosts and a port,
 * and returns a connected socket to one of those hosts.  In the process,
 * the hostlist variable is updated to reflect the single MyProxy host
 * that was connected to.  This is so future procedures know which host
 * is being used.
 *
 * @param hostlist A comma-separated list of MyProxy hosts to try to connect to.
 *        Upon successful connection, this list is overwritten by the single
 *        host which was actually connected to.
 * @param port The port to try to connect to.
 */
static int get_connected_myproxy_host_socket(char *hostlist, int port) {

    int retsock = -1;     /* Connected socket to be returned */
    char *pshost = NULL;  /* Copy of hostlist for strtok */
    char *tok;            /* Result of strtok(pshost) */
    int connected = 0;    /* Assume failed connection */
    int spec_port = port;

    /* Assume hostlist is a comma separated list of MyProxy hosts.  */
    /* Try to create a socket connection to each one until success. */
    pshost = strdup(hostlist);
    tok = strtok(pshost, ",");
    while (tok != NULL) {
        char *tok2 = strchr(tok, ':');

        if (tok2 != NULL) { /* server-specific port specified */
            *tok2 = '\0';
            spec_port = strtol(++tok2, (char **)NULL, 10);
            if (spec_port == 0) {
                verror_put_errno(errno);
                verror_put_string("Error determining port (%s) for host %s\n", tok2, tok);
                if (retsock != -1) {
                    close(retsock);
                    retsock = -1;
                }
                break;
            }
        }
        else
            spec_port = port;

        retsock = connect_socket_to_host(tok, spec_port);
        if (retsock >= 0) { /* Success! */
            connected = 1;
            break; /* out of while loop */
        } else { /* Failed. Get new socket and try next host */
            verror_put_string("Unable to connect to %s\n", tok);
        }

        tok = strtok(NULL, ",");  /* Try next MyProxy host in the list */
    }

    if (connected) { /* Rewrite hostlist to actual (single) connected host */
        strcpy(hostlist,tok);
        myproxy_debug("Successfully connected to %s:%d\n", hostlist, spec_port);
    }
    if (pshost) free(pshost);

    return retsock;
}



static const char *
encode_command(const myproxy_proto_request_type_t       command_value);

static int
parse_string(const char                 *str,
             int                        *value);

static int
encode_integer(int                              value,
               char                             *string,
               int                              string_len);

static int
parse_response_type(const char                          *type_str,
                    myproxy_proto_response_type_t       *type_value);

static const char *
encode_response(myproxy_proto_response_type_t   response_value);

static int
string_to_int(const char                        *string,
              int                               *integer);

static char *
parse_entry(char *buffer, authorization_data_t *data);

static int
parse_auth_data(char *buffer, authorization_data_t ***auth_data);

/* Values for string_to_int() */
#define STRING_TO_INT_SUCCESS           1
#define STRING_TO_INT_ERROR             -1
#define STRING_TO_INT_NONNUMERIC        0

/**********************************************************************
 *
 * Exported functions
 *
 */

char *
myproxy_version(int *major, int *minor, int *micro) {
    if (major) *major = MYPROXY_VERSION_MAJOR;
    if (minor) *minor = MYPROXY_VERSION_MINOR;
    if (micro) *micro = MYPROXY_VERSION_MICRO;
    return MYPROXY_VERSION_DATE;
}

int
myproxy_check_version_ex(int major, int minor, int micro) {
    if (major != MYPROXY_VERSION_MAJOR) return 1;
    if (minor != MYPROXY_VERSION_MINOR) return 2;
    if (micro != MYPROXY_VERSION_MICRO) return 3;
    return 0;
}

int
myproxy_bootstrap_trust(myproxy_socket_attrs_t *attrs)
{
    char *cert_dir = NULL, *tmp_cert_dir = NULL, *work_dir = NULL;
    int return_value = -1;
    BIO *sbio = 0;
    SSL_CTX *ctx = 0;
    SSL *ssl = 0;
    STACK_OF(X509) *sk = 0;
    int i;
    char buf[BUFSIZ], buf2[BUFSIZ];
    int sockfd = -1;
    mode_t prev_umask = 0;
    X509 *x;

    myproxy_log("Bootstrapping MyProxy server root of trust.");

    globus_module_activate(GLOBUS_GSI_PROXY_MODULE);
    globus_module_activate(GLOBUS_GSI_CREDENTIAL_MODULE);
    globus_module_activate(GLOBUS_GSI_CERT_UTILS_MODULE);

    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    /* make writable only by user */
    prev_umask = umask(S_IWGRP|S_IWOTH);

    /* initialize X509_CERT_DIR */
    cert_dir = get_trusted_certs_path();
    if (!cert_dir) {
        goto error;
    }
    if (access(cert_dir, X_OK) == 0) {
        myproxy_debug("%s exists. Updating.", cert_dir);
        work_dir = cert_dir;
    } else {     /* create temporary directory for atomic bootstrap */
        tmp_cert_dir = strdup(cert_dir);
        if (tmp_cert_dir[strlen(tmp_cert_dir) - 1] == '/') {
            tmp_cert_dir[strlen(tmp_cert_dir) - 1] = '\0';
        }
        snprintf(buf, BUFSIZ, ".%d/", getpid());
        if (my_append(&tmp_cert_dir, buf, NULL) == -1) {
            goto error;
        }
        if (make_path(tmp_cert_dir) == -1) {
            goto error;
        }
        work_dir = tmp_cert_dir;
    }

    /* get trust root(s) from the myproxy-server */
    #if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(ctx, TLS1_VERSION);
    #else
    ctx = SSL_CTX_new(SSLv23_client_method());
    /* No longer setting SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS since it seemed
     * like a stop-gap measure to interoperate with broken SSL */
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    #endif

    if (!(sbio = BIO_new_ssl(ctx, 1))) goto error;
    if ( (sockfd = get_connected_myproxy_host_socket(
                   attrs->pshost, attrs->psport) ) < 0) {
        goto error;
    }
    BIO_get_ssl(sbio, &ssl);
    SSL_set_fd(ssl, sockfd);

    if (BIO_do_handshake(sbio) <= 0) goto error;
    if (BIO_write(sbio, "0", 1) < 1) goto error;    /* GSI deleg flag */

    sk=SSL_get_peer_cert_chain(ssl);
    x = sk_X509_value(sk,0);    /* start with EEC */
    for (i = 1; i < sk_X509_num(sk); i++) {
        X509 *xp;
        BIO *certbio, *policybio;
        unsigned long hash;
        char path[MAXPATHLEN], tmppath[MAXPATHLEN];

        xp = x;
        x = sk_X509_value(sk,i);
        hash = X509_subject_name_hash(x);
        snprintf(path, MAXPATHLEN, "%s%08lx.0", work_dir, hash);
        snprintf(tmppath, MAXPATHLEN, "%s.tmp", path);
        certbio = BIO_new_file(tmppath, "w");
        if (!certbio) {
            verror_put_string("failed to open %s", tmppath);
            goto error;
        }
        PEM_write_bio_X509(certbio,x);
        BIO_free(certbio);
        if (rename(tmppath, path) < 0) { /* atomic update */
            verror_put_errno(errno);
            verror_put_string("Unable to rename %s to %s\n",
                              tmppath, path);
            goto error;
        }
        myproxy_debug("renamed %s to %s", tmppath, path);
        myproxy_debug("wrote trusted certificate to %s", path);
        snprintf(path, MAXPATHLEN, "%s%08lx.signing_policy",
                 work_dir, hash);
        snprintf(tmppath, MAXPATHLEN, "%s.tmp", path);
        policybio = BIO_new_file(tmppath, "w");
        if (!policybio) {
            verror_put_string("failed to open %s", tmppath);
            goto error;
        }
        X509_NAME_oneline(X509_get_subject_name(x),buf,sizeof buf);
        X509_NAME_oneline(X509_get_subject_name(xp),buf2,sizeof buf2);
        BIO_printf(policybio, "access_id_CA X509 '%s'\npos_rights globus CA:sign\ncond_subjects globus '\"%s\"'\n", buf, buf2);
        BIO_free(policybio);
        if (rename(tmppath, path) < 0) { /* atomic update */
            verror_put_errno(errno);
            verror_put_string("Unable to rename %s to %s\n",
                              tmppath, path);
            goto error;
        }
        myproxy_debug("renamed %s to %s", tmppath, path);
        myproxy_debug("wrote trusted certificate policy to %s", path);
        if (i == 1) {
            myproxy_log("New trusted MyProxy server: %s", buf2, hash);
        }
        myproxy_log("New trusted CA (%08lx.0): %s", hash, buf);
    }

    if (tmp_cert_dir) {       /* commit the bootstrapped directory. */
        if (rename(tmp_cert_dir, cert_dir) < 0) {
            verror_put_errno(errno);
            verror_put_string("Unable to rename %s to %s\n",
                              tmp_cert_dir, cert_dir);
            goto error;
        }
        myproxy_debug("renamed %s to %s", tmp_cert_dir, cert_dir);
    }

    return_value = 0;           /* success */

 error:
    if (ctx) {
        SSL_CTX_free(ctx);
    }
    if (sbio) {
        BIO_free_all(sbio);
    }
    if (return_value) {
        ssl_error_to_verror();
        myproxy_log("trust root bootstrap failed");
        myproxy_log_verror();
        if (cert_dir) rmdir(cert_dir);
    }
    if (cert_dir) free(cert_dir);
    if (tmp_cert_dir) free(tmp_cert_dir);
    if (prev_umask) umask(prev_umask);          /* restore umask */

    return return_value;
}

int
myproxy_bootstrap_client(myproxy_socket_attrs_t *socket_attrs,
                         int bootstrap_if_no_cert_dir,
                         int bootstrap_even_if_cert_dir_exists)
{
    int return_value = -1;

    /* Bootstrap trusted certificate directory if none exists. */
    if (bootstrap_if_no_cert_dir) {
        char *cert_dir = NULL;
        globus_result_t res;

        globus_module_activate(GLOBUS_GSI_CERT_UTILS_MODULE);
        res = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&cert_dir);
        if (res != GLOBUS_SUCCESS) {
            globus_object_free(globus_error_get(res));
            if (myproxy_bootstrap_trust(socket_attrs) < 0) {
                goto cleanup;
            }
        }
        if (cert_dir) free(cert_dir);
    }

    /* Connect to server. */
    if (myproxy_init_client(socket_attrs) < 0) {
        goto cleanup;
    }

    /* Attempt anonymous-mode credential retrieval if we don't have a
       credential. */
    GSI_SOCKET_allow_anonymous(socket_attrs->gsi_socket, 1);

    /* Authenticate client to server */
    if (myproxy_authenticate_init(socket_attrs, NULL) < 0) {
        if (bootstrap_if_no_cert_dir &&
            strstr(verror_get_string(), "CRL") != NULL) {
            myproxy_log("CRL error detected.  Attempting to recover.");
            switch (myproxy_clean_crls()) {
              case -1:
                verror_print_error(stderr);
              case 0:
                goto cleanup;
            }
            verror_clear();
        } else if (bootstrap_if_no_cert_dir &&
                   strstr(verror_get_string(),
                          "Can't get the local trusted CA certificate") != NULL) {
            if (bootstrap_even_if_cert_dir_exists) {
                if (myproxy_bootstrap_trust(socket_attrs) < 0) {
                    goto cleanup;
                }
                verror_clear();
            } else {
                verror_put_string("The CA that signed the myproxy-server's certificate is untrusted.");
                verror_put_string("If you want to trust the CA, re-run with the -b option.");
                goto cleanup;
            }
        } else if (GSI_SOCKET_check_creds(socket_attrs->gsi_socket)
                   == GSI_SOCKET_SUCCESS) {
            /* If we tried with credentials and failed, then
               try again with anonymous authentication. */
            myproxy_debug("%s", verror_get_string());
            myproxy_debug("Certificate authentication error. Trying anonymous.");
            verror_clear();
            GSI_SOCKET_use_creds(socket_attrs->gsi_socket, "/dev/null");
        } else {
            goto cleanup; /* can't recover, so don't re-try */
        }
        /* Try again after recovery attempt... */
        if (myproxy_init_client(socket_attrs) < 0) {
            goto cleanup;
        }
        GSI_SOCKET_allow_anonymous(socket_attrs->gsi_socket, 1);
        if (myproxy_authenticate_init(socket_attrs, NULL) < 0) {
            goto cleanup;
        }
    }

    return_value = 0;           /* success */

 cleanup:
    return return_value;
}

int
myproxy_init_client(myproxy_socket_attrs_t *attrs) {
    myproxy_debug("MyProxy %s", myproxy_version(0,0,0));

    assert(attrs);
    if (attrs->gsi_socket) {
        GSI_SOCKET_destroy(attrs->gsi_socket);
        attrs->gsi_socket = NULL;
        close(attrs->socket_fd);
    }
    attrs->socket_fd = -1;

    attrs->socket_fd = get_connected_myproxy_host_socket(
        attrs->pshost, attrs->psport);

    /* If we got a good socket, allocate a GSI_SOCKET as well */
    if (attrs->socket_fd >= 0) {
        attrs->gsi_socket = GSI_SOCKET_new(attrs->socket_fd);
        if (attrs->gsi_socket == NULL) {
            /* Problem with GSI_SOCKET_new, close the 'normal' socket */
            verror_put_string("GSI_SOCKET_new()\n");
            close(attrs->socket_fd);
            attrs->socket_fd = -1;
        } else { /* Everything is good! Clear out the error string. */
            verror_clear();
        }
    }

    return attrs->socket_fd;
}

/* Returns 0 if old checks should be performed or non-zero
   if new checks should be performed. */
static int
new_server_identity_check_behavior_needed()
{
   char *compat = NULL;

   compat = getenv("GLOBUS_GSSAPI_NAME_COMPATIBILITY");
   if (compat == NULL || strcmp(compat, "STRICT_RFC2818"))
   {
       return 0; /* Perform old checks */
   } else {
       return 1; /* Perform new checks */
   }
}

int
myproxy_authenticate_init(myproxy_socket_attrs_t *attrs,
                          const char *proxyfile)
{
   char error_string[1024];
   char peer_name[1024] = "";
   gss_name_t accepted_peer_names[3] = { 0 };
   char *server_dn;
   int  rval, return_value = -1;

   assert(attrs);

   if (GSI_SOCKET_use_creds(attrs->gsi_socket,
                            proxyfile) == GSI_SOCKET_ERROR) {
       GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                   sizeof(error_string));
       verror_put_string("Error setting credentials to use: %s\n",
                         error_string);
       goto error;
   }

   /*
    * What identity to we expect the server to have?
    */
   server_dn = getenv("MYPROXY_SERVER_DN");
   if (server_dn) {
       gss_buffer_desc server_dn_buffer;
       gss_buffer_desc status_buffer;
       OM_uint32 major_status, minor_status;

       myproxy_debug("Expecting non-standard server DN \"%s\"\n", server_dn);

       server_dn_buffer.length = strlen(server_dn);
       server_dn_buffer.value = server_dn;

       major_status = gss_import_name(
            &minor_status,
            &server_dn_buffer,
            GSS_C_NO_OID,
            &accepted_peer_names[0]);

       if (major_status != GSS_S_COMPLETE)
       {
           OM_uint32 stmin;

           major_status = gss_display_status(&stmin,
                minor_status,
                GSS_C_MECH_CODE,
                GSS_C_NO_OID,
                NULL,
                &status_buffer);
           if (major_status == GSS_S_COMPLETE) {
               verror_put_string("Error getting name of remote party: %s\n",
                                   status_buffer.value);
               gss_release_buffer(&minor_status, &status_buffer);
           } else {
               verror_put_string("Error getting name of remote party");
           }
       }
   } else {
       char *fqhn;
       if (new_server_identity_check_behavior_needed()) {
           OM_uint32 major_status, minor_status;
           gss_buffer_desc hostip;
           static gss_OID_desc gss_nt_host_ip_oid =
                { 10, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x02" };
           gss_OID_desc * gss_nt_host_ip = &gss_nt_host_ip_oid;

           hostip.value = strdup(attrs->pshost);
           if (hostip.value == NULL)
           {
                 verror_put_string("Error getting name of remote party: %s\n",
                                   strerror(errno));
                 goto error;
           }
           hostip.length = strlen(hostip.value);
           major_status = gss_import_name(
                &minor_status,
                &hostip,
                gss_nt_host_ip,
                &accepted_peer_names[0]);
           free(hostip.value);
           if (GSS_ERROR(major_status))
           {
               OM_uint32                msg_context = 0;
               OM_uint32                local_major_status, local_minor_status;
               gss_buffer_desc          status_string = {0};

               local_major_status = gss_display_status(
                    &local_minor_status,
                    minor_status,
                    GSS_C_MECH_CODE,
                    GSS_C_NO_OID,
                    &msg_context,
                    &status_string);

               if (!GSS_ERROR(local_major_status))
               {
                   verror_put_string("%.*s",
                        (int) status_string.length, status_string.value);
                   gss_release_buffer(&local_minor_status, &status_string);
               }
               else
               {
                   verror_put_string(
                        "Error getting name of remote party: GSSAPI ERROR %d\n",
                        (int) major_status);
               }

               goto error;
           }
       } else { /* old way */
           gss_buffer_desc name_buf;
           const char *services[] = { "myproxy", "host" };
           int s;
           OM_uint32 major_status, minor_status;

             fqhn = GSI_SOCKET_get_peer_hostname(attrs->gsi_socket);
             if (!fqhn) {
                 GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                             sizeof(error_string));
                 verror_put_string("Error getting name of remote party: %s\n",
                                   error_string);
                 goto error;
             }
             for (s = 0; s < (sizeof services)/(sizeof *services); s++)
             {
               name_buf.value = globus_common_create_string("%s@%s",
                    services[s], fqhn);
               name_buf.length = strlen(name_buf.value);

               major_status = gss_import_name(
                    &minor_status,
                    &name_buf,
                    GSS_C_NT_HOSTBASED_SERVICE,
                    &accepted_peer_names[s]);
             }

             free(fqhn);
       }
   }

   rval = GSI_SOCKET_authentication_init(attrs->gsi_socket,
                                         accepted_peer_names);
   if (rval == GSI_SOCKET_UNAUTHORIZED) {
       /* This is a common error.  Send the GSI errors to debug and
          return a more friendly error message in verror(). */
       GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                   sizeof(error_string));
       myproxy_debug("Error authenticating: %s\n", error_string);
       GSI_SOCKET_get_peer_name(attrs->gsi_socket, peer_name,
                                sizeof(peer_name));
       if (server_dn) {
           verror_put_string("Server authorization failed.  Server identity\n"
                             "(%s)\ndoes not match $MYPROXY_SERVER_DN\n"
                             "(%s).\nIf the server identity is acceptable, "
                             "set\nMYPROXY_SERVER_DN=\"%s\"\n"
                             "and try again.\n",
                             peer_name, server_dn, peer_name);
       } else {
           verror_put_string("Server authorization failed.  Server identity "
                             "does not match expected identity.\n"
                             "If the server identity is acceptable, "
                             "set\nMYPROXY_SERVER_DN=\"%s\"\n"
                             "and try again.\n",
                             peer_name);
       }
       goto error;
   } else if (rval == GSI_SOCKET_ERROR) {
       GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                   sizeof(error_string));
       verror_put_string("Error authenticating: %s\n", error_string);
       goto error;
   }

   return_value = 0;

 error:
   if (accepted_peer_names[0]) free(accepted_peer_names[0]);
   if (accepted_peer_names[1]) free(accepted_peer_names[1]);
   if (accepted_peer_names[2]) free(accepted_peer_names[2]);

   return return_value;
}

int
myproxy_authenticate_accept_fqans(myproxy_socket_attrs_t *attrs, char *client_name, const int namelen, char ***fqans)
{
    char error_string[1024];

    assert(client_name != NULL);
    assert(attrs);

    if (GSI_SOCKET_authentication_accept(attrs->gsi_socket) == GSI_SOCKET_ERROR) {
        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));

        verror_put_string("Error authenticating client: %s\n", error_string);

        return -1;
    }

    if (GSI_SOCKET_get_peer_name(attrs->gsi_socket,
                                 client_name,
                                 namelen) == GSI_SOCKET_ERROR) {
        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));
        verror_put_string("Error getting client name: %s\n", error_string);
        return -1;
    }

    if (fqans && strcmp(client_name, "<anonymous>") &&
        (GSI_SOCKET_get_peer_fqans(attrs->gsi_socket, fqans) ==
         GSI_SOCKET_ERROR)) {
        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));
        myproxy_debug("No client VOMS attributes (%s). Continuing without attributes support.\n", error_string);
    }

    return 0;
}

int
myproxy_authenticate_accept(myproxy_socket_attrs_t *attrs, char *client_name, const int namelen)
{
   return myproxy_authenticate_accept_fqans(attrs, client_name, namelen, NULL);
}

int
myproxy_init_delegation(myproxy_socket_attrs_t *attrs, const char *delegfile, const int lifetime, char *passphrase)
{

    char error_string[1024];

    if (attrs == NULL)
        return -1;

    if (GSI_SOCKET_delegation_init_ext(attrs->gsi_socket,
                                       delegfile,
                                       lifetime,
                                       passphrase) == GSI_SOCKET_ERROR) {

        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));

        verror_put_string("Error delegating credentials: %s\n", error_string);
        return -1;
    }
    return 0;
}

int
myproxy_accept_delegation(myproxy_socket_attrs_t *attrs, char *data, const int datalen, char *passphrase)
{
    char error_string[1024];

    assert(attrs);
    assert(data != NULL);

    if (GSI_SOCKET_delegation_accept_ext(attrs->gsi_socket, data, datalen, passphrase) == GSI_SOCKET_ERROR) {
        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));
        verror_put_string("Error accepting delegated credentials: %s\n", error_string);
        return -1;
    }

    return 0;
}

int
myproxy_accept_delegation_ex(myproxy_socket_attrs_t *attrs, char **credentials,
                             int *credential_len, char *passphrase)
{
    char error_string[1024];

    assert(attrs);
    assert(credentials != NULL);

    if (GSI_SOCKET_delegation_accept(attrs->gsi_socket,
                                     (unsigned char **)credentials,
                                     credential_len,
                                     passphrase) == GSI_SOCKET_ERROR) {
        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));
        verror_put_string("Error accepting delegated credentials: %s\n",
                          error_string);
        return -1;
    }

    return 0;
}

int
myproxy_request_cert(myproxy_socket_attrs_t *attrs, char *certreq,
                     char **credentials, int *credential_len)
{
    assert(attrs);
    assert(certreq);
    GSI_SOCKET_delegation_set_certreq(attrs->gsi_socket, certreq);

    return myproxy_accept_delegation_ex(attrs, credentials,
                                        credential_len, NULL);
}

int
myproxy_serialize_request(const myproxy_request_t *request, char *data,
                          const int datalen)
{
    int len;
    char *buf = NULL;

    assert(data != NULL);
    assert(datalen > 0);

    len = myproxy_serialize_request_ex(request, &buf);
    if (len <= 0) {
        if (buf) free(buf);
        return len;
    }
    if (len >= datalen) {
        verror_put_string("Buffer size exceeded in myproxy_serialize_request().");
        if (buf) free(buf);
        return -1;
    }
    memcpy(data, buf, len);
    free(buf);
    return len;
}

int
myproxy_serialize_request_ex(const myproxy_request_t *request, char **data)
{
    int len;
    char lifetime_string[64];
    const char *command_string;

    assert(data != NULL);
    if (*data) (*data)[0] = '\0';

    /* version */
    len = my_append(data, MYPROXY_VERSION_STRING,
                    request->version, "\n", NULL);
    if (len < 0)
        return -1;

    /* command type */
    command_string = encode_command((myproxy_proto_request_type_t)request->command_type);
    if (command_string == NULL) {
        return -1;
    }
    len = my_append(data, MYPROXY_COMMAND_STRING,
                    command_string, "\n", NULL);
    if (len < 0)
        return -1;

    /* username */
    len = my_append(data, MYPROXY_USERNAME_STRING,
                    request->username, "\n", NULL);
    if (len < 0)
        return -1;

    /* passphrase */
    len = my_append(data, MYPROXY_PASSPHRASE_STRING,
                    request->passphrase, "\n", NULL);
    if (len < 0)
        return -1;

    /* new passphrase */
    if (request->new_passphrase[0]!= '\0')
    {
        len = my_append(data, MYPROXY_NEW_PASSPHRASE_STRING,
                        request->new_passphrase, "\n", NULL);
        if (len < 0)  return -1;
    }

    /* lifetime */
    if (encode_integer(request->proxy_lifetime,
                       lifetime_string,
                       sizeof(lifetime_string)) == -1)
    {
        return -1;
    }

    len = my_append(data, MYPROXY_LIFETIME_STRING,
                    lifetime_string, "\n", NULL);
    if (len < 0)
        return -1;

    /* retrievers */
    if (request->retrievers != NULL)
    {
        len = my_append(data, MYPROXY_RETRIEVER_STRING,
                        request->retrievers, "\n", NULL);
        if (len < 0)
            return -1;
    }

    /* renewers */
    if (request->renewers != NULL)
    {
        len = my_append(data, MYPROXY_RENEWER_STRING,
                        request->renewers, "\n", NULL);
        if (len < 0)
            return -1;
    }

    /* credential name */
    if (request->credname!= NULL)
    {
        char *buf = strdup (request->credname);
        strip_char (buf, '\n');

        len = my_append(data, MYPROXY_CRED_PREFIX, "_",
                        MYPROXY_CRED_NAME_STRING,
                        buf, "\n", NULL);
        free(buf);
        if (len < 0)
            return -1;
    }

    /* credential description */
    if (request->creddesc != NULL)
    {
        char *buf = strdup (request->creddesc);
        strip_char (buf, '\n');
        len = my_append(data, MYPROXY_CRED_PREFIX, "_",
                        MYPROXY_CRED_DESC_STRING,
                        buf, "\n", NULL);
        free(buf);
        if (len < 0)
            return -1;
    }

    /* key retrievers */
    if (request->keyretrieve != NULL)
    {
        len = my_append(data, MYPROXY_KEY_RETRIEVER_STRING,
                        request->keyretrieve, "\n", NULL);
        if (len < 0)
            return -1;
    }

    /* trusted retrievers */
    if (request->trusted_retrievers != NULL)
    {
        len = my_append(data, MYPROXY_TRUSTED_RETRIEVER_STRING,
                        request->trusted_retrievers, "\n", NULL);
        if (len < 0)
            return -1;
    }

    /* trusted root certificates */
    if (request->want_trusted_certs) {
        myproxy_debug("requesting trusted certificates download");
        len = my_append(data, MYPROXY_TRUSTED_CERTS_STRING,
                        "1", "\n", NULL);
        if (len < 0)
            return -1;
    }

    /* voname */
    if (request->voname) {
        char *tok = NULL, *vonameDup = NULL;
        vonameDup = strdup(request->voname);
        if (vonameDup == NULL) {
            return -1;
        }
        for (tok = strtok(vonameDup, "\n");
             tok != NULL;
             tok = strtok(NULL, "\n")) {
            len = my_append(data, MYPROXY_VONAME_STRING, tok, "\n", NULL);
            if (len < 0) {
                break;
            }
        }
        if (vonameDup) {
            free(vonameDup);
        }
        if (len < 0) {
            return -1;
        }
    }

    /* vomses */
    if (request->vomses) {
        char *tok = NULL, *vomsesDup = NULL;
        vomsesDup = strdup(request->vomses);
        if (vomsesDup == NULL) {
            return -1;
        }
        for (tok = strtok(vomsesDup, "\n");
             tok != NULL;
             tok = strtok(NULL, "\n")) {
            len = my_append(data, MYPROXY_VOMSES_STRING, tok, "\n", NULL);
            if (len < 0) {
                break;
            }
        }
        if (vomsesDup) {
            free(vomsesDup);
        }
        if (len < 0) {
            return -1;
        }
    }

    return len + 1;
}

int
myproxy_deserialize_request(const char *data, const int datalen,
                            myproxy_request_t *request)
{
    int len, return_code = -1;
    char *tmp=NULL, *buf=NULL, *new_data=NULL;

    assert(request != NULL);
    assert(data != NULL);

    /* if the input data isn't null terminated, fix it now. */
    if (data[datalen - 1] != '\0') {
        new_data = malloc(datalen + 1);
        memcpy(new_data, data, datalen);
        new_data[datalen] = '\0';
        data = new_data;
    }

    /* version */
    len = convert_message(data,
                          MYPROXY_VERSION_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);

    if (len <= -1)
    {
        verror_prepend_string("Error parsing version from client request");
        goto error;
    }

    request->version = strdup(buf);

    if (request->version == NULL)
    {
        verror_put_errno(errno);
        goto error;
    }

    /* command */
    len = convert_message(data,
                          MYPROXY_COMMAND_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);

    if (len <= -1)
    {
        verror_prepend_string("Error parsing command from client request");
        goto error;
    }

    if (parse_command(buf, &request->command_type) == -1)
    {
        goto error;
    }

    /* username */
    len = convert_message(data,
                          MYPROXY_USERNAME_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);
    if (len <= -1)
    {
        verror_prepend_string("Error parsing usename from client request");
        goto error;
    }

    request->username = strdup(buf);

    if (request->username == NULL)
    {
        verror_put_errno(errno);
        goto error;
    }

    /* passphrase */
    len = convert_message(data,
                          MYPROXY_PASSPHRASE_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);

    if (len <= -1)
    {
        verror_prepend_string("Error parsing passphrase from client request");
        goto error;
    }

    /* XXX request_passphrase is a static buffer. Why? */
    strncpy(request->passphrase, buf, sizeof(request->passphrase) - 1);

    /* new passphrase (for change passphrase only) */
    len = convert_message(data,
                          MYPROXY_NEW_PASSPHRASE_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);

    if (len == -1)
    {
        verror_prepend_string("Error parsing passphrase from client request");
        goto error;
    }
    else
        if (len == -2)
            request->new_passphrase[0] = '\0';
        else
            strncpy (request->new_passphrase, buf,
            sizeof(request->new_passphrase) - 1);

    /* lifetime */
    len = convert_message(data,
                          MYPROXY_LIFETIME_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);
    if (len <= -1)
    {
        verror_prepend_string("Error parsing lifetime from client request");
        goto error;
    }

    if (parse_string(buf, &request->proxy_lifetime) == -1)
    {
        goto error;
    }

    /* retriever */
    len = convert_message(data,
                          MYPROXY_RETRIEVER_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);

    if (len == -2)  /*-2 indicates string not found*/
        request->retrievers = NULL;
    else
    if (len <= -1)
    {
        verror_prepend_string("Error parsing retriever from client request");
        goto error;
    }
    else
    {
        request->retrievers = strdup(buf);

        if (request->retrievers == NULL)
        {
            verror_put_errno(errno);
            goto error;
        }
    }


    /* renewer */
    len = convert_message(data,
                          MYPROXY_RENEWER_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);

    if (len == -2)  /*-2 indicates string not found*/
        request->renewers = NULL;
    else
        if (len <= -1)
        {
            verror_prepend_string("Error parsing renewer from client request");
            goto error;
        }
        else
        {
            request->renewers = strdup(buf);

            if (request->renewers == NULL)
            {
                verror_put_errno(errno);
                goto error;
            }
        }

    /* credential name */
    if (tmp) tmp[0] = '\0';
    len = my_append(&tmp, MYPROXY_CRED_PREFIX, "_",
                    MYPROXY_CRED_NAME_STRING, NULL);

    if (len == -1) {
        goto error;
    }

    len = convert_message(data,
                          tmp, CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);

    if (len == -2)  /*-2 indicates string not found - assign default*/
        request->credname = NULL;
    else
        if (len <= -1)
        {
            verror_prepend_string("Error parsing credential name from client request");
            goto error;
        }
        else
        {
            request->credname = strdup(buf);

            if (request->credname == NULL)
            {
                verror_put_errno(errno);
                goto error;
            }
        }

    /* credential description */
    if (tmp) tmp[0] = '\0';
    len = my_append(&tmp, MYPROXY_CRED_PREFIX, "_",
                    MYPROXY_CRED_DESC_STRING, NULL);

    len = convert_message(data,
                          tmp, CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);

    if (len == -2)  /*-2 indicates string not found*/
        request->creddesc = NULL;
    else
        if (len <= -1)
        {
            verror_prepend_string("Error parsing credential description from client request");
            goto error;
        }
        else
        {
            request->creddesc = strdup(buf);

            if (request->creddesc == NULL)
            {
                verror_put_errno(errno);
                goto error;
            }
        }

    /* key retriever */
    len = convert_message(data,
                          MYPROXY_KEY_RETRIEVER_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);

    if (len == -2)  /*-2 indicates string not found*/
        request->keyretrieve = NULL;
    else
        if (len <= -1)
        {
            verror_prepend_string("Error parsing key retriever from client request");
            goto error;
        }
        else
        {
            request->keyretrieve = strdup(buf);

            if (request->keyretrieve == NULL)
            {
                verror_put_errno(errno);
                goto error;
            }
        }

    /* trusted retriever */
    len = convert_message(data,
                          MYPROXY_TRUSTED_RETRIEVER_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);

    if (len == -2)  /*-2 indicates string not found*/
        request->trusted_retrievers = NULL;
    else
        if (len <= -1)
        {
            verror_prepend_string("Error parsing trusted retrievers from client request");
            goto error;
        }
        else
        {
            request->trusted_retrievers = strdup(buf);

            if (request->trusted_retrievers == NULL)
            {
                verror_put_errno(errno);
                goto error;
            }
        }

    /* trusted root certificates */
    len = convert_message(data,
                          MYPROXY_TRUSTED_CERTS_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);

    if (len == -2)  /*-2 indicates string not found*/
        request->want_trusted_certs = 0;
    else
    if (len <= -1)
    {
        verror_prepend_string("Error parsing TRUSTED_CERTS in client request");
        goto error;
    }
    else
    {
        if (string_to_int(buf, &request->want_trusted_certs) !=
            STRING_TO_INT_SUCCESS) {
            verror_prepend_string("Error parsing TRUSTED_CERTS in client request");
            goto error;
        }
    }

    /* voname */
    len = convert_message(data,
                          MYPROXY_VONAME_STRING,
                          CONVERT_MESSAGE_ALLOW_MULTIPLE,
                          &buf);

    if (len == -2) { /* -2 indicates string not found */
        request->voname = NULL;
    } else {
        if (len <= -1) {
            verror_prepend_string("Error parsing VONAME in client request");
            goto error;
        } else {
            request->voname = strdup(buf);
            if (request->voname == NULL) {
                verror_put_errno(errno);
                goto error;
            }
        }
    }

    /* vomses */
    len = convert_message(data,
                          MYPROXY_VOMSES_STRING,
                          CONVERT_MESSAGE_ALLOW_MULTIPLE,
                          &buf);

    if (len == -2) /* -2 indicates string not found */
        request->vomses = NULL;
    else
    if (len <= -1)
    {
        verror_prepend_string("Error parsing VOMSES in client request");
        goto error;
    }
    else
    {
        request->vomses = strdup(buf);
        if (request->vomses == NULL) {
            verror_put_errno(errno);
            goto error;
        }
    }

    /* Success */
    return_code = 0;

 error:
    if (tmp) free(tmp);
    if (buf) free(buf);
    if (new_data) free(new_data);

    return return_code;
}

int
myproxy_serialize_response(const myproxy_response_t *response,
                           char *data, const int datalen)
{
    int len;
    char *buf = NULL;

    assert(data != NULL);
    assert(datalen > 0);

    len = myproxy_serialize_response_ex(response, &buf);
    if (len <= 0) {
        if (buf) free(buf);
        return len;
    }
    if (len >= datalen) {
        verror_put_string("Buffer size exceeded in myproxy_serialize_response().");
        if (buf) free(buf);
        return -1;
    }
    memcpy(data, buf, len);
    free(buf);
    return len;
}

int
myproxy_serialize_response_ex(const myproxy_response_t *response,
                              char **data)
{
    int len;
    authorization_data_t **p;
    const char *response_string;

    assert(data != NULL);
    assert(response != NULL);

    if (*data) (*data)[0] = '\0';

    /*Version*/
    len = my_append(data, MYPROXY_VERSION_STRING,
                    response->version, "\n", NULL);
    if (len < 0)
        return -1;

    response_string = encode_response((myproxy_proto_response_type_t) response->response_type);

    /*Response string*/
    if (response_string == NULL) {
        return -1;
    }

    len = my_append(data, MYPROXY_RESPONSE_TYPE_STRING,
                    response_string, "\n", NULL);
    if (len < 0)
        return -1;

    /*Authorization data*/
    if ((p = response->authorization_data)) {
        while (*p) {
            len = my_append(data, MYPROXY_AUTHORIZATION_STRING,
                            authorization_get_name((*p)->method), ":",
                            (*p)->server_data, "\n", NULL);
            if (len < 0)
                return -1;
            p++;
        }
    }

    /* Include credential info in OK response to INFO request */
    if (response->response_type == MYPROXY_OK_RESPONSE &&
        response->info_creds) {
        int first_cred = 1;
        myproxy_creds_t *cred;
        char date[40];
        for (cred = response->info_creds; cred != NULL; cred = cred->next) {
            /* Include name on first cred only.  Other creds are indexed by
               name, so there is no need for an additional name field. */
            if (cred->credname && first_cred) {
                len = my_append(data, MYPROXY_CRED_PREFIX,
                                "_", MYPROXY_CRED_NAME_STRING,
                                cred->credname, "\n", NULL);
                if (len == -1)
                    goto error;
            }
            assert(cred->credname || first_cred);
            if (cred->creddesc) {
                if (first_cred) {
                    len = my_append(data,
                                    MYPROXY_CRED_PREFIX,
                                    "_", MYPROXY_CRED_DESC_STRING,
                                    cred->creddesc, "\n", NULL);
                } else {
                    len = my_append(data,
                                    MYPROXY_CRED_PREFIX,
                                    "_", cred->credname,
                                    "_", MYPROXY_CRED_DESC_STRING,
                                    cred->creddesc, "\n", NULL);
                }
                if (len == -1)
                    goto error;
            }
            sprintf(date, "%lu",  cred->start_time);
            if (first_cred) {
                len = my_append(data, MYPROXY_CRED_PREFIX,
                                "_", MYPROXY_START_TIME_STRING,
                                date, "\n", NULL);
            } else {
                len = my_append(data, MYPROXY_CRED_PREFIX,
                                "_", cred->credname,
                                "_", MYPROXY_START_TIME_STRING,
                                date, "\n", NULL);
            }
            if (len == -1)
                goto error;
            sprintf(date, "%lu", cred->end_time);
            if (first_cred) {
                len = my_append(data, MYPROXY_CRED_PREFIX,
                                "_", MYPROXY_END_TIME_STRING,
                                date, "\n", NULL);
            } else {
                len = my_append(data, MYPROXY_CRED_PREFIX,
                                "_", cred->credname,
                                "_", MYPROXY_END_TIME_STRING,
                                date, "\n", NULL);
            }
            if (len == -1)
                goto error;
            if (first_cred) {
                len = my_append(data, MYPROXY_CRED_PREFIX,
                                "_", MYPROXY_CRED_OWNER_STRING,
                                cred->owner_name, "\n", NULL);
            } else {
                len = my_append(data, MYPROXY_CRED_PREFIX,
                                "_", cred->credname,
                                "_", MYPROXY_CRED_OWNER_STRING,
                                cred->owner_name, "\n", NULL);
            }
            if (len == -1)
                goto error;
            if (cred->retrievers) {
                if (first_cred) {
                    len = my_append(data,
                                    MYPROXY_CRED_PREFIX,
                                    "_", MYPROXY_RETRIEVER_STRING,
                                    cred->retrievers, "\n", NULL);
                } else {
                    len = my_append(data,
                                    MYPROXY_CRED_PREFIX,
                                    "_", cred->credname,
                                    "_", MYPROXY_RETRIEVER_STRING,
                                    cred->retrievers, "\n", NULL);
                }
                if (len == -1)
                    goto error;
            }
            if (cred->keyretrieve) {
                if (first_cred) {
                    len = my_append(data,
                                    MYPROXY_CRED_PREFIX,
                                    "_", MYPROXY_KEY_RETRIEVER_STRING,
                                    cred->keyretrieve, "\n", NULL);
                } else {
                    len = my_append(data,
                                    MYPROXY_CRED_PREFIX,
                                    "_", cred->credname,
                                    "_", MYPROXY_KEY_RETRIEVER_STRING,
                                    cred->keyretrieve, "\n", NULL);
                }
                if (len == -1)
                    goto error;
            }
            if (cred->trusted_retrievers) {
                if (first_cred) {
                    len = my_append(data,
                                    MYPROXY_CRED_PREFIX,
                                    "_", MYPROXY_TRUSTED_RETRIEVER_STRING,
                                    cred->trusted_retrievers, "\n", NULL);
                } else {
                    len = my_append(data,
                                    MYPROXY_CRED_PREFIX,
                                    "_", cred->credname,
                                    "_", MYPROXY_TRUSTED_RETRIEVER_STRING,
                                    cred->trusted_retrievers, "\n", NULL);
                }
                if (len == -1)
                    goto error;
            }
            if (cred->renewers) {
                if (first_cred) {
                    len = my_append(data,
                                    MYPROXY_CRED_PREFIX,
                                    "_", MYPROXY_RENEWER_STRING,
                                    cred->renewers, "\n", NULL);
                } else {
                    len = my_append(data,
                                    MYPROXY_CRED_PREFIX,
                                    "_", cred->credname,
                                    "_", MYPROXY_RENEWER_STRING,
                                    cred->renewers, "\n", NULL);
                }
                if (len == -1)
                    goto error;
            }
            if (cred->lockmsg) {
                char *newline;
                newline = strchr(cred->lockmsg, '\n');
                if (newline) {
                    *newline = '\0'; /* only send first line */
                }
                if (first_cred) {
                    len = my_append(data,
                                    MYPROXY_CRED_PREFIX,
                                    "_", MYPROXY_LOCKMSG_STRING,
                                    cred->lockmsg, "\n", NULL);
                } else {
                    len = my_append(data,
                                    MYPROXY_CRED_PREFIX,
                                    "_", cred->credname,
                                    "_", MYPROXY_LOCKMSG_STRING,
                                    cred->lockmsg, "\n", NULL);
                }
                if (newline) {
                    *newline = '\n';
                }
                if (len == -1)
                    goto error;
            }
            first_cred = 0;
        }
        if (response->info_creds->next) {
            len = my_append(data,
                            MYPROXY_ADDITIONAL_CREDS_STRING, NULL);
            if (len < 0)
                return -1;
            for (cred = response->info_creds->next;
                 cred != NULL;
                 cred = cred->next) {
                if (cred->next) {
                    len = my_append(data, cred->credname,
                                    "," , NULL);
                } else {
                    len = my_append(data, cred->credname,
                                    NULL);
                }
                if (len < 0)
                    return -1;
            }
            len = my_append(data,
                            "\n", NULL);
            if (len < 0)
                return -1;
        }
    }

    /* Only add error string(s) if necessary */
    if (response->response_type == MYPROXY_ERROR_RESPONSE) {
        char *start, *end;
        /* send each line individually */
        for (start = response->error_string;
             (end = strchr(start, '\n')) != NULL;
             start = end + 1) {
            *end = '\0';
            len = my_append(data, MYPROXY_ERROR_STRING,
                            start, "\n", NULL);
            if (len < 0) return -1;
        }
        /* send the last line */
        if (start[0] != '\0') {
            len = my_append(data, MYPROXY_ERROR_STRING,
                            start, "\n", NULL);
            if (len < 0) return -1;
        }
    }

    /* Include trusted certificates */
    if (response->trusted_certs) {
        myproxy_certs_t *cert;

        len = my_append(data, MYPROXY_TRUSTED_CERTS_STRING,
                        NULL);
        if (len < 0)
            return -1;

        for (cert = response->trusted_certs; cert; cert = cert->next) {
        if (strchr(cert->filename, ',')) {
                myproxy_log("skipping trusted cert w/ filename containing ',': %s", cert->filename);
                continue;
        }
            if (cert->next) {
                len = my_append(data, cert->filename,
                                "," , NULL);
            } else {
                len = my_append(data, cert->filename,
                                NULL);
            }
            if (len < 0)
                return -1;
        }
        len = my_append(data, "\n", NULL);
        if (len < 0)
            return -1;


        for (cert = response->trusted_certs; cert; cert = cert->next) {
            char *b64data;
            if (b64_encode(cert->contents, cert->size, &b64data) < 0) {
                goto error;
            }
            /* myproxy_debug("got b64:\n%s\n", b64data); */
            len = my_append(data, MYPROXY_FILEDATA_PREFIX,
                            "_", cert->filename, "=",
                            b64data,
                            "\n", NULL);
            free(b64data);

            if (len < 0)
                return -1;
        }
    }

    /* myproxy_debug("sending %s\n", data); */

    return len + 1;

    error:
        return -1;
}


int
myproxy_deserialize_response(myproxy_response_t *response,
                             const char *data, const int datalen)
{
    int len, return_code = -1;
    int value, i, num_creds;
    char *tmp=NULL, *buf=NULL, *new_data=NULL;

    assert(response != NULL);
    assert(data != NULL);

    /* if the input data isn't null terminated, fix it now. */
    if (data[datalen - 1] != '\0') {
        new_data = malloc(datalen + 1);
        memcpy(new_data, data, datalen);
        new_data[datalen] = '\0';
        data = new_data;
    }

    if (response->authorization_data) {
        free(response->authorization_data);
        response->authorization_data = NULL;
    }

    /* myproxy_debug("received %s\n", data); */

    len = convert_message(data,
                          MYPROXY_VERSION_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);
    if (len < 0) {
        goto error;
    }
    if (response->version) {
        free(response->version);
    }
    response->version = strdup(buf);
    if (response->version == NULL) {
        verror_put_errno(errno);
        goto error;
    }

    len = convert_message(data,
                          MYPROXY_RESPONSE_TYPE_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);
    if (len < 0) {
        goto error;
    }

    if (parse_response_type(buf,
                            &response->response_type) == -1) {
        goto error;
    }

    if (response->response_type == MYPROXY_ERROR_RESPONSE) {
        /* It's ok if ERROR not present */
        response->error_string = 0;
        len = convert_message(data,
                              MYPROXY_ERROR_STRING,
                              CONVERT_MESSAGE_ALLOW_MULTIPLE,
                              &response->error_string);
        return_code = 0;
        goto error;
    }

    /* Parse any cred info in response */

    /* start time */
    if (tmp) tmp[0] = '\0';
    len = my_append(&tmp, MYPROXY_CRED_PREFIX, "_",
                    MYPROXY_START_TIME_STRING, NULL);
    if (len < 0) goto error;
    len = convert_message(data, tmp, CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &buf);
    if (len == -1) goto error;

    if (len > 0) {              /* credential info present */
        response->info_creds = malloc(sizeof(struct myproxy_creds));
        memset(response->info_creds, 0, sizeof(struct myproxy_creds));

        switch(string_to_int(buf, &value)) {
          case STRING_TO_INT_SUCCESS:
            response->info_creds->start_time = value;
            break;
          case STRING_TO_INT_NONNUMERIC:
            verror_put_string("Non-numeric characters in CRED_START_TIME \"%s\"", buf);
            goto error;
          case STRING_TO_INT_ERROR:
            goto error;
        }

        if (tmp) tmp[0] = '\0';
        len = my_append(&tmp, MYPROXY_CRED_PREFIX,
                        "_", MYPROXY_END_TIME_STRING, NULL);
        if (len < 0) goto error;

        len = convert_message(data, tmp,
                              CONVERT_MESSAGE_DEFAULT_FLAGS,
                              &buf);

        if (len > 0) {
            switch(string_to_int(buf, &value)) {
              case STRING_TO_INT_SUCCESS:
                response->info_creds->end_time = value;
                break;
              case STRING_TO_INT_NONNUMERIC:
                verror_put_string("Non-numeric characters in CRED_END_TIME \"%s\"", buf);
                goto error;
              case STRING_TO_INT_ERROR:
                goto error;
            }
        }

        if (tmp) tmp[0] = '\0';
        len = my_append(&tmp, MYPROXY_CRED_PREFIX,
                        "_", MYPROXY_CRED_NAME_STRING, NULL);
        if (len < 0) goto error;

        len = convert_message(data, tmp,
                              CONVERT_MESSAGE_DEFAULT_FLAGS,
                              &buf);
        if (len == -1) goto error;
        if (len > 0)
            response->info_creds->credname = strdup(buf);

        if (tmp) tmp[0] = '\0';
        len = my_append(&tmp, MYPROXY_CRED_PREFIX,
                        "_", MYPROXY_CRED_DESC_STRING, NULL);
        if (len < 0) goto error;

        len = convert_message(data, tmp,
                              CONVERT_MESSAGE_DEFAULT_FLAGS,
                              &buf);
        if (len == -1) goto error;
        if (len > 0)
            response->info_creds->creddesc = strdup(buf);

        if (tmp) tmp[0] = '\0';
        len = my_append(&tmp, MYPROXY_CRED_PREFIX,
                        "_", MYPROXY_CRED_OWNER_STRING, NULL);
        if (len < 0) goto error;

        len = convert_message(data, tmp,
                              CONVERT_MESSAGE_DEFAULT_FLAGS,
                              &buf);
        if (len == -1) goto error;
        if (len >= 0)
            response->info_creds->owner_name = strdup(buf);

        if (tmp) tmp[0] = '\0';
        len = my_append(&tmp, MYPROXY_CRED_PREFIX,
                        "_", MYPROXY_RETRIEVER_STRING, NULL);
        if (len < 0) goto error;

        len = convert_message(data, tmp,
                              CONVERT_MESSAGE_DEFAULT_FLAGS,
                              &buf);
        if (len == -1) goto error;
        if (len >= 0)
            response->info_creds->retrievers = strdup(buf);

        if (tmp) tmp[0] = '\0';
        len = my_append(&tmp, MYPROXY_CRED_PREFIX,
                        "_", MYPROXY_KEY_RETRIEVER_STRING, NULL);
        if (len < 0) goto error;

        len = convert_message(data, tmp,
                              CONVERT_MESSAGE_DEFAULT_FLAGS,
                              &buf);
        if (len == -1) goto error;
        if (len >= 0)
            response->info_creds->keyretrieve = strdup(buf);

        if (tmp) tmp[0] = '\0';
        len = my_append(&tmp, MYPROXY_CRED_PREFIX,
                        "_", MYPROXY_TRUSTED_RETRIEVER_STRING, NULL);
        if (len < 0) goto error;

        len = convert_message(data, tmp,
                              CONVERT_MESSAGE_DEFAULT_FLAGS,
                              &buf);
        if (len == -1) goto error;
        if (len >= 0)
            response->info_creds->trusted_retrievers = strdup(buf);

        if (tmp) tmp[0] = '\0';
        len = my_append(&tmp, MYPROXY_CRED_PREFIX,
                        "_", MYPROXY_RENEWER_STRING, NULL);
        if (len < 0) goto error;

        len = convert_message(data, tmp,
                              CONVERT_MESSAGE_DEFAULT_FLAGS,
                              &buf);
        if (len == -1) goto error;
        if (len >= 0)
            response->info_creds->renewers = strdup(buf);

        if (tmp) tmp[0] = '\0';
        len = my_append(&tmp, MYPROXY_CRED_PREFIX,
                        "_", MYPROXY_LOCKMSG_STRING, NULL);
        if (len < 0) goto error;

        len = convert_message(data, tmp,
                              CONVERT_MESSAGE_DEFAULT_FLAGS,
                              &buf);
        if (len == -1) goto error;
        if (len >= 0)
            response->info_creds->lockmsg = strdup(buf);

        len = convert_message(data, MYPROXY_ADDITIONAL_CREDS_STRING,
                              CONVERT_MESSAGE_DEFAULT_FLAGS,
                              &buf);

        if (len == -1) goto error;
        if (len >= 0) {         /* addl credentials */
            char **strs;
            struct myproxy_creds *cred = response->info_creds;

            len = parse_add_creds(buf, &strs, &num_creds);
            if (len == -1) {
                verror_put_string("Error parsing additional cred string");
                goto error;
            }

            for (i = 0; i < num_creds; i++) {
                cred->next = malloc(sizeof(struct myproxy_creds));
                cred = cred->next;
                memset(cred, 0, sizeof(struct myproxy_creds));

                cred->credname = strdup(strs[i]);

                if (tmp) tmp[0] = '\0';
                len = my_append(&tmp,
                                MYPROXY_CRED_PREFIX, "_", strs[i],
                                "_", MYPROXY_CRED_DESC_STRING, NULL);
                if (len == -1) goto error;

                len = convert_message(data, tmp,
                                      CONVERT_MESSAGE_DEFAULT_FLAGS,
                                      &buf);
                if (len == -1) goto error;

                if (len >= 0)
                    cred->creddesc = strdup(buf);

                if (tmp) tmp[0]='\0';
                len = my_append(&tmp,
                                MYPROXY_CRED_PREFIX, "_", strs[i],
                                "_", MYPROXY_START_TIME_STRING,
                                NULL);
                if (len == -1) goto error;

                len = convert_message(data, tmp,
                                      CONVERT_MESSAGE_DEFAULT_FLAGS,
                                      &buf);
                if (len == -1) goto error;
                if (len > 0) {
                    switch(string_to_int(buf, &value)) {
                      case STRING_TO_INT_SUCCESS:
                        cred->start_time = value;
                        break;
                      case STRING_TO_INT_NONNUMERIC:
                        verror_put_string("Non-numeric characters in CRED_START_TIME \"%s\"", buf);
                        goto error;
                      case STRING_TO_INT_ERROR:
                        goto error;
                    }
                }

                if (tmp) tmp[0] = '\0';
                len = my_append(&tmp,
                                MYPROXY_CRED_PREFIX, "_", strs[i],
                                "_", MYPROXY_END_TIME_STRING, NULL);
                if (len == -1) goto error;

                len = convert_message(data, tmp,
                                      CONVERT_MESSAGE_DEFAULT_FLAGS,
                                      &buf);
                if (len == -1) goto error;
                if (len > 0) {
                    switch(string_to_int(buf, &value)) {
                      case STRING_TO_INT_SUCCESS:
                        cred->end_time = value;
                        break;
                      case STRING_TO_INT_NONNUMERIC:
                        verror_put_string("Non-numeric characters in CRED_END_TIME \"%s\"", buf);
                        goto error;
                      case STRING_TO_INT_ERROR:
                        goto error;
                    }
                }

                if (tmp) tmp[0] = '\0';
                len = my_append(&tmp,
                                MYPROXY_CRED_PREFIX, "_", strs[i],
                                "_", MYPROXY_CRED_OWNER_STRING,
                                NULL);
                if (len == -1) goto error;

                len = convert_message(data, tmp,
                                      CONVERT_MESSAGE_DEFAULT_FLAGS,
                                      &buf);
                if (len == -1) goto error;

                if (len >= 0)
                    cred->owner_name = strdup(buf);

                if (tmp) tmp[0] = '\0';
                len = my_append(&tmp,
                                MYPROXY_CRED_PREFIX, "_", strs[i],
                                "_", MYPROXY_RETRIEVER_STRING,
                                NULL);
                if (len == -1) goto error;

                len = convert_message(data, tmp,
                                      CONVERT_MESSAGE_DEFAULT_FLAGS,
                                      &buf);
                if (len == -1) goto error;

                if (len >= 0)
                    cred->retrievers = strdup(buf);

                if (tmp) tmp[0] = '\0';
                len = my_append(&tmp,
                                MYPROXY_CRED_PREFIX, "_", strs[i],
                                "_", MYPROXY_KEY_RETRIEVER_STRING,
                                NULL);
                if (len == -1) goto error;

                len = convert_message(data, tmp,
                                      CONVERT_MESSAGE_DEFAULT_FLAGS,
                                      &buf);
                if (len == -1) goto error;

                if (len >= 0)
                    cred->keyretrieve = strdup(buf);

                if (tmp) tmp[0] = '\0';
                len = my_append(&tmp,
                                MYPROXY_CRED_PREFIX, "_", strs[i],
                                "_", MYPROXY_TRUSTED_RETRIEVER_STRING,
                                NULL);
                if (len == -1) goto error;

                len = convert_message(data, tmp,
                                      CONVERT_MESSAGE_DEFAULT_FLAGS,
                                      &buf);
                if (len == -1) goto error;

                if (len >= 0)
                    cred->trusted_retrievers = strdup(buf);

                if (tmp) tmp[0] = '\0';
                len = my_append(&tmp,
                                MYPROXY_CRED_PREFIX, "_", strs[i],
                                "_", MYPROXY_RENEWER_STRING, NULL);
                if (len == -1) goto error;

                len = convert_message(data, tmp,
                                      CONVERT_MESSAGE_DEFAULT_FLAGS,
                                      &buf);
                if (len == -1) goto error;

                if (len >= 0)
                    cred->renewers = strdup(buf);

                if (tmp) tmp[0] = '\0';
                len = my_append(&tmp,
                                MYPROXY_CRED_PREFIX, "_", strs[i],
                                "_", MYPROXY_LOCKMSG_STRING, NULL);
                if (len == -1) goto error;

                len = convert_message(data, tmp,
                                      CONVERT_MESSAGE_DEFAULT_FLAGS,
                                      &buf);
                if (len == -1) goto error;

                if (len >= 0)
                    cred->lockmsg = strdup(buf);
            }
            /* de-allocate string-list from parse_add_creds() */
            for (i = 0; i < num_creds; i++) {
                free(strs[i]);
            }
            free(strs);
        }
    }

    len = convert_message(data,
                          MYPROXY_AUTHORIZATION_STRING,
                          CONVERT_MESSAGE_ALLOW_MULTIPLE,
                          &buf);
    if (len > 0) {
        if (parse_auth_data(buf,
                            &response->authorization_data)) {
            verror_put_string("Error parsing authorization data from server response");
            goto error;
        }
    }

    len = convert_message(data,
                          MYPROXY_TRUSTED_CERTS_STRING,
                          CONVERT_MESSAGE_DEFAULT_FLAGS,
                          &tmp);
    if (len > 0) {
        char *tok, *files;
        myproxy_certs_t *curr=NULL;

        files = strdup(tmp);
        for (tok = strtok(files, ",");
             tok; tok = strtok(NULL, ",")) {

            if (curr == NULL) {
                response->trusted_certs = curr =
                    (myproxy_certs_t *)malloc(sizeof(myproxy_certs_t));
            } else {
                curr->next = (myproxy_certs_t *)malloc(sizeof(myproxy_certs_t));
                curr = curr->next;
            }
            memset(curr, 0, sizeof(myproxy_certs_t));
            curr->filename = strdup(tok);
            myproxy_debug("got cert file: %s\n", curr->filename);

            if (tmp) tmp[0] = '\0';
            len = my_append(&tmp,
                            MYPROXY_FILEDATA_PREFIX, "_", tok, "=",
                            NULL);
            if (len == -1) goto error;

            len = convert_message(data, tmp,
                                  CONVERT_MESSAGE_DEFAULT_FLAGS,
                                  &buf);
            if (len == -1) goto error;

        curr->size = b64_decode(buf, &curr->contents);
        if (curr->size == (size_t) -1) {
            verror_put_string("b64 decode failed!");
            goto error;
            }
            /* myproxy_debug("contents:\n%s\n", curr->contents); */
        }
        free(files);
    }

    /* Success */
    return_code = 0;

 error:
    if (tmp) free(tmp);
    if (buf) free(buf);
    if (new_data) free(new_data);

    return return_code;
}

int
myproxy_send(myproxy_socket_attrs_t *attrs,
             const char *data, const int datalen)
{
    char error_string[1024];

    assert(data != NULL);

    if (GSI_SOCKET_write_buffer(attrs->gsi_socket, data, datalen) == GSI_SOCKET_ERROR)
    {
        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));
        verror_put_string("Error writing: %s\n", error_string);
        return -1;
    }
    return 0;
}

int
myproxy_recv(myproxy_socket_attrs_t *attrs,
             char *data, const int datalen)
{
    unsigned char *buffer = NULL;
    char error_string[1024];
    size_t readlen;

    assert(data != NULL);

    if (GSI_SOCKET_read_token(attrs->gsi_socket, &buffer,
                              &readlen) == GSI_SOCKET_ERROR) {
        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));
        verror_put_string("Error reading: %s\n", error_string);
        return -1;
    }
    if (readlen > datalen) {
        memcpy(data, buffer, datalen);
        free(buffer);
        verror_put_string("Response was truncated\n");
        return -2;
    }
    memcpy(data, buffer, readlen);
    free(buffer);
    return readlen;
}

int
myproxy_recv_ex(myproxy_socket_attrs_t *attrs, char **data)
{
    size_t readlen;
    char error_string[1024];

    if (GSI_SOCKET_read_token(attrs->gsi_socket, (unsigned char **)data,
                              &readlen) == GSI_SOCKET_ERROR) {
        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));
        verror_put_string("Error reading: %s\n", error_string);
        return -1;
    }
    return readlen;
}

int
myproxy_recv_response(myproxy_socket_attrs_t *attrs,
                      myproxy_response_t *response)
{
    int responselen;
    char *response_buffer = NULL;
    int rval;

    /* Receive a response from the server */
    responselen = myproxy_recv_ex(attrs, &response_buffer);
    if (responselen < 0) {
        return(-1);
    }

    if (responselen == 0) {
        verror_put_string("Server closed connection.\n");
        return(-1);
    }

    rval = myproxy_handle_response(response_buffer, responselen, response);
    free(response_buffer);
    return rval;
}

int
myproxy_handle_response(const char *response_buffer,
                        int responselen,
                        myproxy_response_t *response)
{
    /* Make a response object from the response buffer */
    if (myproxy_deserialize_response(response, response_buffer,
                                     responselen) < 0) {
        return(-1);
    }

    /* Check version */
    if (strcmp(response->version, MYPROXY_VERSION) != 0) {
        verror_put_string("Error: Received invalid version number from server");
        return(-1);
    }

    /* Check response */
    switch(response->response_type) {
      case MYPROXY_ERROR_RESPONSE:
        verror_put_string("ERROR from myproxy-server:\n%s",
                          response->error_string);
        return(-1);
        break;
      case MYPROXY_OK_RESPONSE:
      case MYPROXY_AUTHORIZATION_RESPONSE:
        break;
      default:
        verror_put_string("Received unknown response type");
        return(-1);
        break;
    }
    return 0;
}

int myproxy_recv_response_ex(myproxy_socket_attrs_t *socket_attrs,
                             myproxy_response_t *server_response,
                             myproxy_request_t *client_request)
{
    do {
        if (myproxy_recv_response(socket_attrs, server_response) != 0) {
            return -1;
        }
        if (server_response->response_type == MYPROXY_AUTHORIZATION_RESPONSE) {
            if (myproxy_handle_authorization(socket_attrs, server_response,
                                             client_request) != 0) {
                return -1;
            }
            authorization_data_free(server_response->authorization_data);
            server_response->authorization_data = NULL;
        }
    } while (server_response->response_type == MYPROXY_AUTHORIZATION_RESPONSE);

    return 0;
}

int myproxy_handle_authorization(myproxy_socket_attrs_t *attrs,
                                 myproxy_response_t *server_response,
                                 myproxy_request_t *client_request)
{
    myproxy_proto_response_type_t response_type;
    authorization_data_t *d = NULL;
    /* just pointer into server_response->authorization_data, no memory is
       allocated for this pointer */
    int return_status = -1;
    char *buffer = NULL;
    int bufferlen;

    response_type = server_response->response_type;
    if (response_type == MYPROXY_AUTHORIZATION_RESPONSE) {
        /* Server wants authorization. Try the possibilities. */
        if (client_request->authzcreds != NULL) { /* We have an AUTHZ cert. */
            d = authorization_create_response(
                   server_response->authorization_data,
                   AUTHORIZETYPE_CERT, client_request->authzcreds,
                   strlen(client_request->authzcreds) + 1);
        } else {
            verror_put_string("No credentials for renewal authorization.");
        }
#if defined(HAVE_LIBSASL2)
        if (d == NULL) { /* No luck with AUTHORIZETYPE_CERT. Try SASL. */
            d = authorization_create_response(
                   server_response->authorization_data,
                   AUTHORIZETYPE_SASL, "", 1);
        }
#endif
        if (d == NULL) { /* No luck with previous methods. Try PASSWD. */
            d = authorization_create_response(
                   server_response->authorization_data,
                   AUTHORIZETYPE_PASSWD,
                   client_request->passphrase,
                   strlen(client_request->passphrase) + 1);
        }
        if (d == NULL) { /* No acceptable methods found. */
            verror_put_string("Unable to respond to server's authentication challenge.");
            goto end;
        }

        bufferlen = d->client_data_len + sizeof(int);
        buffer = malloc(bufferlen);
        if (!buffer) {
            verror_put_string("malloc() failed");
            goto end;
        }
        memset(buffer, '\0', bufferlen);
        (*buffer) = d->method;

        memcpy(buffer + sizeof(int), d->client_data, d->client_data_len);

        /* Send the authorization data to the server */
        if (myproxy_send(attrs, buffer, bufferlen) < 0) {
            goto end;
        }

#if defined(HAVE_LIBSASL2)
        /* SASL method requires more negotiation. */
        if (d->method == AUTHORIZETYPE_SASL) {
            if (auth_sasl_negotiate_client(attrs, client_request) < 0)
                goto end;
        }
#endif
    }

    return_status = 0;
end:
    if (buffer) free(buffer);

    return return_status;
}

void
myproxy_free(myproxy_socket_attrs_t *attrs,
             myproxy_request_t *request,
             myproxy_response_t *response)
{
    if (attrs != NULL) {
        if (attrs->pshost != NULL)
            free(attrs->pshost);
        GSI_SOCKET_destroy(attrs->gsi_socket);
        close(attrs->socket_fd);
        free(attrs);
    }

    if (request != NULL) {
        if (request->version != NULL)
            free(request->version);
        if (request->username != NULL)
            free(request->username);
        if (request->retrievers != NULL)
            free(request->retrievers);
        if (request->renewers != NULL)
            free(request->renewers);
        if (request->credname != NULL)
            free(request->credname);
        if (request->creddesc != NULL)
            free(request->creddesc);
        if (request->authzcreds != NULL)
            free(request->authzcreds);
        if (request->keyretrieve != NULL)
            free(request->keyretrieve);
        if (request->trusted_retrievers != NULL)
            free(request->trusted_retrievers);
        if (request->voname != NULL)
            free(request->voname);
        if (request->vomses != NULL)
            free(request->vomses);
        free(request);
    }

    if (response != NULL) {
        if (response->version != NULL)
            free(response->version);
        if (response->authorization_data != NULL)
            authorization_data_free(response->authorization_data);
        if (response->error_string != NULL)
            free(response->error_string);
        if (response->info_creds != NULL) {
            myproxy_creds_free(response->info_creds);
        }
        if (response->trusted_certs != NULL) {
            myproxy_certs_free(response->trusted_certs);
        }
        free(response);
    }
}

int
myproxy_request_add_voname(myproxy_request_t *client_request,
                           const char *voname)
{

    int return_status = -1;

    if (client_request == NULL) {
        verror_put_string("NULL client_request passed.");
        goto error;
    }
    if (voname == NULL) {
        verror_put_string("NULL voname passed.");
        goto error;
    }

    if (client_request->voname == NULL) {
        client_request->voname = strdup(voname);
        if (client_request->voname == NULL) {
            verror_put_string("strdup() failed");
            goto error;
        }
    } else {
        if (my_append(&(client_request->voname), "\n", voname, NULL) < 0) {
            verror_put_string("my_append failed");
            goto error;
        }
    }

    return_status = 0;

  error:
    return return_status;
}

int
myproxy_request_add_vomses(myproxy_request_t *client_request,
                           const char *vomses)
{

    int return_status = -1;

    if (client_request == NULL) {
        verror_put_string("NULL client_request passed.");
        goto error;
    }
    if (vomses == NULL) {
        verror_put_string("NULL vomses passed.");
        goto error;
    }

    if (client_request->vomses == NULL) {
        client_request->vomses = strdup(vomses);
        if (client_request->vomses == NULL) {
            verror_put_string("strdup() failed");
            goto error;
        }
    } else {
        if (my_append(&(client_request->vomses), "\n", vomses, NULL) < 0) {
            verror_put_string("my_append failed");
            goto error;
        }
    }

    return_status = 0;

  error:
    return return_status;
}


/*--------- Helper functions ------------*/
/*
 * convert_message()
 *
 * Searches a buffer and locates varname. Stores contents of varname into line
 * e.g. convert_message(buf, "VERSION=", &version);
 * The line argument should be a pointer to NULL or a malloc'ed buffer.
 * The line buffer will be realloc'ed as required.
 * The buffer MUST BE NULL TERMINATED.
 *
 * flags is a bitwise or of the following values:
 *     CONVERT_MESSAGE_ALLOW_MULTIPLE      Allow a multiple instances of
 *                                         varname, in which case the rvalues
 *                                         are concatenated.
 *
 * Returns the number of characters copied into the line (not including the
 * terminating '\0'). On error returns -1, setting verror. Returns -2
 * if string not found
 */
static int
convert_message(const char                      *buffer,
                const char                      *varname,
                const int                       flags,
                char                            **line)
{
    int                         foundone = 0;
    char                        *varname_start;
    int                         return_value = -1;
    int                         line_index = 0;
    const char                  *buffer_p;

    assert(buffer != NULL);

    assert(varname != NULL);
    assert(line != NULL);

    if ((flags & ~CONVERT_MESSAGE_KNOWN_FLAGS) != 0)
    {
        verror_put_string("Illegal flags value (%d)", flags);
        goto error;
    }

    /*
     * Our current position in buffer is in buffer_p. Since we're
     * done modifying buffer buffer_p can be a const.
     */
    buffer_p = buffer;

    while ((varname_start = strstr(buffer_p, varname)) != NULL)
    {
        char                    *value_start;
        int                     value_length;

        /* Have is this the first varname we've found? */
        if (foundone == 1)
        {
            /* No. Is that OK? */
            if (flags & CONVERT_MESSAGE_ALLOW_MULTIPLE)
            {
                /* Yes. Add carriage return to existing line and concatenate */
                *line = realloc(*line, line_index + 2);
                (*line)[line_index] = '\n';
                line_index++;
                (*line)[line_index] = '\0';
            }
            else
            {
                /* No. That's an error */
                verror_put_string("Multiple values found in convert_message()");
                goto error;
            }
        }

        /* Find start of value */
        value_start = &varname_start[strlen(varname)];

        /* Find length of value (might be zero) */
        value_length = strcspn(value_start, "\n");

        *line = realloc(*line, line_index+value_length + 1);
        /* Copy it over */
        strncpy((*line)+line_index, value_start, value_length);
        line_index += value_length;

        /* Make sure line stays NULL-terminated */
        (*line)[line_index] = '\0';

        /* Indicate we've found a match */
        foundone = 1;

        /* Advance our buffer position pointer */
        buffer_p = &value_start[value_length];
    }

    /* Did we find anything */
    if (foundone == 0)
    {
        /* verror_put_string("No value found"); */
        return_value = -2; /*string not found*/
        goto error;
    }

    /* Success */
    return_value = strlen(*line);

  error:
    if (return_value == -1 || return_value == -2)
    {
        /* Don't return anything in line on error */
        if (*line) (*line)[0] = '\0';
    }

    return return_value;
}

/*
 * parse_command()
 *
 * Parse command_str return the respresentation of the command in
 * command_value.
 *
 * Returns 0 on success, -1 on error setting verror.
 */
static int
parse_command(const char                        *command_str,
              myproxy_proto_request_type_t      *command_value)
{
    int                         value;
    int                         return_value = -1;

    assert(command_str != NULL);
    assert(command_value != NULL);

    /* XXX Should also handle string commands */

    switch (string_to_int(command_str, &value))
    {
      case STRING_TO_INT_SUCCESS:
        return_value = 0;
        *command_value = (myproxy_proto_request_type_t) value;
        break;

      case STRING_TO_INT_NONNUMERIC:
        verror_put_string("Non-numeric characters in command string \"%s\"",
                          command_str);
        break;

      case STRING_TO_INT_ERROR:
        break;
    }

    return return_value;
}


/*
 * encode_command()
 *
 * Return a string encoding of the command in command_value.
 * Returns NULL on error, setting verror.
 */
static const char *
encode_command(const myproxy_proto_request_type_t       command_value)
{
    const char *string;

    /*
     * XXX Should return actual string description.
     */
    switch(command_value)
    {
      case MYPROXY_GET_PROXY:
        string = "0";
        break;

      case MYPROXY_PUT_PROXY:
        string = "1";
        break;

      case MYPROXY_INFO_PROXY:
        string = "2";
        break;

      case MYPROXY_DESTROY_PROXY:
        string = "3";
        break;

      case MYPROXY_CHANGE_CRED_PASSPHRASE:
        string = "4";
        break;

      case MYPROXY_STORE_CERT:
        string = "5";
        break;

      case MYPROXY_RETRIEVE_CERT:
        string = "6";
        break;

      case MYPROXY_GET_TRUSTROOTS:
        string = "7";
        break;

      default:
        /* Should never get here */
        string = NULL;
        verror_put_string("Internal error: Bad command type(%d)",
                          command_value);
        break;
    }

    return string;
}


/*
 * parse_string
 *
 * Given a string representation of an integer value, fill in the given
 * integer with its integral value.
 *
 * Currently the string is just an ascii representation of the integer.
 *
 * Returns 0 on success, -1 on error setting verror.
 */
static int
parse_string(const char                 *str,
             int                        *value)
{
    int                         val;
    int                         return_value = -1;

    assert(str != NULL);
    assert(value != NULL);

    /* XXX Should also handle string commands */

    switch (string_to_int(str, &val))
    {
      case STRING_TO_INT_SUCCESS:
        return_value = 0;
        *value = val;
        break;

      case STRING_TO_INT_NONNUMERIC:
        verror_put_string("Non-numeric characters in string \"%s\"",
                          str);
        break;

      case STRING_TO_INT_ERROR:
        break;
    }

    return return_value;
}


/*
 * encode_integer()
 *
 * Encode the given integer as a string into the given buffer with
 * length of buffer_len.
 *
 * Returns 0 on success, -1 on error setting verror.
 */
static int
encode_integer(int                              value,
               char                             *string,
               int                              string_len)
{
    /* Buffer large enough to hold string representation of lifetime */
    char buffer[20];

    assert(string != NULL);

    sprintf(buffer, "%d", value);

    if (my_strncpy(string, buffer, string_len) == -1)
    {
        return -1;
    }

    return 0;
}


/*
 * parse_response_type()
 *
 * Given a string representation of a response_type, fill in type_value
 * with the value.
 *
 * Currently the string is just an ascii representation of the value.
 *
 * Returns 0 on success, -1 on error setting verror.
 */
static int
parse_response_type(const char                          *type_str,
                    myproxy_proto_response_type_t       *type_value)
{
    int                         value;
    int                         return_value = -1;

    assert(type_str != NULL);
    assert(type_value != NULL);

    /* XXX Should also handle string representations */

    switch (string_to_int(type_str, &value))
    {
      case STRING_TO_INT_SUCCESS:
        return_value = 0;
        *type_value = (myproxy_proto_response_type_t) value;
        break;

      case STRING_TO_INT_NONNUMERIC:
        verror_put_string("Non-numeric characters in string \"%s\"",
                          type_str);
        break;

      case STRING_TO_INT_ERROR:
        break;
    }

    return return_value;
}

/*
 * encode_response()
 *
 * Return a string encoding of the response_type in response_value.
 * Returns NULL on error.
 */
static const char *
encode_response(const myproxy_proto_response_type_t     response_value)
{
    const char *string;

    /*
     * XXX Should return actual string description.
     */
    switch(response_value)
    {
      case MYPROXY_OK_RESPONSE:
        string = "0";
        break;

      case MYPROXY_ERROR_RESPONSE:
        string = "1";
        break;

      case MYPROXY_AUTHORIZATION_RESPONSE:
        string = "2";
        break;

      default:
        /* Should never get here */
        string = NULL;
        verror_put_string("Internal error: Bad response type (%d)",
                          response_value);
        break;
    }

    return string;
}

/*
 * string_to_int()
 *
 * Convert a string representation of an integer into an integer.
 *
 * Returns 1 on success, 0 if string contains non-numeric characters,
 * -1 on error setting verror.
 */
static int
string_to_int(const char                        *string,
              int                               *integer)
{
    char                        *parse_end = NULL;
    int                         base = 0 /* Any */;
    long int                    value;
    int                         return_value = -1;

    assert(string != NULL);
    assert(integer != NULL);

    /* Check for empty string */
    if (strlen(string) == 0)
    {
        verror_put_string("Zero-length string");
        goto error;
    }

    value = strtol(string, &parse_end, base);

    if (value == LONG_MIN)
    {
        verror_put_string("Underflow error");
        goto error;
    }

    if (value == LONG_MAX)
    {
        verror_put_string("Overflow error");
        goto error;
    }

    /* Make sure we parsed all the characters in string */
    if (*parse_end != '\0')
    {
        return_value = 0;
        goto error;
    }

    /* Success */
    *integer = (int) value;
    return_value = 1;

  error:
    return return_value;
}

/* Returns pointer to last processed char in the buffer or NULL on error */
/* The entries are separated either by '\n' or by '\0' */
static char *
parse_entry(char *buffer, authorization_data_t *data)
{
    char *str;
    char *str_method;
    char *p = buffer;
    author_method_t method;

    assert (data != NULL);

    while (*p == '\0')
        p++;
    str_method = p;

    if ((p = strchr(str_method, ':')) == NULL) {
        verror_put_string("Parse error");
        return NULL;
    }
    *p = '\0';
    method = authorization_get_method(str_method);

    str = p + 1;

    if ((p = strchr(str, '\n')))
        *p = '\0';

    data->server_data = malloc(strlen(str) + 1);
    if (data->server_data == NULL) {
        verror_put_errno(errno);
        return NULL;
    }
    strcpy(data->server_data, str);
    data->client_data = NULL;
    data->client_data_len = 0;
    data->method = method;

    return str + strlen(str);
}

/*
  Parse buffer into author_data. The buffer is supposed to be '0'-terminated
*/
static int
parse_auth_data(char *buffer, authorization_data_t ***auth_data)
{
    char *p = buffer;
    char *buffer_end;
    void *tmp;
    authorization_data_t **data = NULL;
    int num_data = 0;
    authorization_data_t entry;
    int return_status = -1;

    data = malloc(sizeof(*data));
    if (data == NULL) {
        verror_put_errno(errno);
        return -1;
    }
    data[0] = NULL;

    buffer_end = buffer + strlen(buffer);
    do {
        p = parse_entry(p, &entry);
        if (p == NULL)
            goto end;

        if (entry.method == AUTHORIZETYPE_NULL)
            continue;

        tmp = realloc(data, (num_data + 1 + 1) * sizeof(*data));
        if (tmp == NULL) {
            verror_put_errno(errno);
            goto end;
        }
        data = tmp;

        data[num_data] = malloc(sizeof(entry));
        if (data[num_data] == NULL) {
            verror_put_errno(errno);
            goto end;
        }

        data[num_data]->server_data = entry.server_data;
        data[num_data]->client_data = entry.client_data;
        data[num_data]->client_data_len = entry.client_data_len;
        data[num_data]->method = entry.method;
        data[num_data + 1] = NULL;
        num_data++;
    } while (p < buffer_end);

    return_status = 0;
    *auth_data = data;

end:
    if (return_status == -1)
        authorization_data_free(data);
    return return_status;
}

int
myproxy_init_credentials(myproxy_socket_attrs_t *attrs,
                         const char             *delegfile)
{

    char error_string[1024];

    if (attrs == NULL)
        return -1;

    if (GSI_SOCKET_credentials_init_ext(attrs->gsi_socket,
                                        delegfile) == GSI_SOCKET_ERROR) {

        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));

        verror_put_string("Error storing  credentials: %s\n", error_string);
        return -1;
    }
    return 0;
}

/*
** Accepts a credential and stores the information in a temp file
** delegfile.
*/
int
myproxy_accept_credentials(myproxy_socket_attrs_t *attrs,
                           char                   *delegfile,
                           int                     delegfile_len)
{
    char error_string[1024];

    if (attrs == NULL)
        return -1;

    if (GSI_SOCKET_credentials_accept_ext(attrs->gsi_socket,
                                          delegfile,
                                          delegfile_len) == GSI_SOCKET_ERROR)
    {
        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));

        verror_put_string("Error accepting credentials: %s\n", error_string);
        return -1;
    }
    return 0;
}

/*
** Retrieves a credential from the repository and sends it to the client.
*/
int
myproxy_get_credentials(myproxy_socket_attrs_t *attrs,
                        const char             *delegfile)
{
    char error_string[1024];

    if (attrs == NULL)
        return -1;

    if (GSI_SOCKET_get_creds(attrs->gsi_socket,
                             delegfile) == GSI_SOCKET_ERROR)
    {
        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));

        verror_put_string("Error getting credentials: %s\n", error_string);
        return -1;
    }

    return 0;
}
