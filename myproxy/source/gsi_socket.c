/*
 * gsi_socket.c
 *
 * See gsi_socket.h for documentation.
 */

#include "myproxy_common.h"
#include "gsi_socket_priv.h"


/*********************************************************************
 *
 * Internal functions
 *
 */


static int
GSI_SOCKET_set_error_string(GSI_SOCKET *self,
                            char *buffer)
{
    if (self->error_string) {
        free(self->error_string);
    }
    self->error_string = strdup(buffer);
    return GSI_SOCKET_SUCCESS;
}

/*
 * append_gss_status()
 *
 * Given a gssapi status and and indicator (gssapi error or mechanism-
 * specific error), append the errors strings to the given string.
 *
 * Returns number of bytes written to buffer, -1 if error was truncated
 * because the buffer was too small.
 */
static int
append_gss_status(char *buffer,
                  int bufferlen,
                  const OM_uint32 gss_code,
                  const int type)
{
    OM_uint32 min_stat;
    gss_buffer_desc error_string;
    OM_uint32 context = 0;
    int total_chars = 0;
    int chars;

    assert(buffer != NULL);

    do
    {
        gss_display_status(&min_stat, gss_code, type, GSS_C_NULL_OID,
                           &context, &error_string);

        if ((error_string.value != NULL) &&
            (error_string.length > 0))
        {
            chars = my_strncpy(buffer, error_string.value, bufferlen);

            if (chars == -1)
            {
                return -1;
            }

            total_chars += chars;
            buffer = &buffer[chars];
            bufferlen -= chars;
        }

        (void) gss_release_buffer(&min_stat, &error_string);

    } while(context);

    return total_chars;
}

/*
 * read_all()
 *
 * Read all the requested bytes into the requested buffer.
 */
static int
read_all(const int sock,
         char *buffer,
         const int nbytes)
{
    int total_bytes_read = 0;
    int bytes_read;

    assert(buffer != NULL);

    while (total_bytes_read < nbytes)
    {
        bytes_read = read(sock, &buffer[total_bytes_read],
                          nbytes - total_bytes_read);

        if (bytes_read == -1)
        {
            return -1;
        }

        if (bytes_read == 0)
        {
            /* EOF */
            errno = EPIPE;
            return -1;
        }
        total_bytes_read += bytes_read;
    }

    return total_bytes_read;
}

/*
 * write_all()
 *
 * Write all the requested bytes to the given socket.
 */
static int
write_all(const int sock,
          const char *buffer,
          const int nbytes)
{
    int total_bytes_written = 0;
    int bytes_written;

    assert(buffer != NULL);

    while (total_bytes_written < nbytes)
    {
        bytes_written = write(sock, &buffer[total_bytes_written],
                              nbytes - total_bytes_written);

        if (bytes_written == -1)
        {
            return -1;
        }

        if (bytes_written == 0)
        {
            /* EOF */
            errno = EPIPE;
            return -1;
        }

        total_bytes_written += bytes_written;
    }

    return total_bytes_written;
}


/*
 * read_token()
 *
 * Read and allocate a token from the given socket.
 */
static int
read_token(const int sock,
           char **p_buffer,
           const int max_token_len)
{
    enum header_fields
    {
        flag                            = 0,
        major_version                   = 1,
        minor_version                   = 2,
        length_high_byte                = 3,
        length_low_byte                 = 4
    };
    int tot_buffer_len = 0, retval;

    assert(p_buffer != NULL);
    *p_buffer = NULL;

    do {
        unsigned char header[5];
        char *bufferp;
        int data_len, buffer_len;
        fd_set rfds;
        struct timeval tv = { 0 };

        if (read_all(sock, (char *)header, sizeof(header)) < 0) {
            if (errno == EPIPE && tot_buffer_len > 0) goto done;
            if (*p_buffer != NULL) {
                free(*p_buffer);
                *p_buffer = NULL;
            }
            return -1;
        }

        /*
         * Check and make sure token looks right
         */
        if (((header[flag] < 20) || (header[flag] > 26)) ||
            (header[major_version] != 3)) {
            if (*p_buffer != NULL) {
                free(*p_buffer);
                *p_buffer = NULL;
            }
            errno = EINVAL;
            return -1;
        }

        data_len = (header[length_high_byte] << 8) + header[length_low_byte];

        buffer_len = data_len + sizeof(header);

        if (max_token_len > 0 && (tot_buffer_len+buffer_len) > max_token_len) {
            if (*p_buffer != NULL) {
                free(*p_buffer);
                *p_buffer = NULL;
            }
            verror_put_string("max_token_len (%d) exceeded", max_token_len);
            errno = ENOMEM;
            return -1;
        }

        bufferp = *p_buffer = realloc(*p_buffer, tot_buffer_len+buffer_len);

        if (bufferp == NULL) {
            if (*p_buffer != NULL) {
                free(*p_buffer);
                *p_buffer = NULL;
            }
            return -1;
        }

        bufferp += tot_buffer_len;
        tot_buffer_len += buffer_len;

        memcpy(bufferp, header, sizeof(header));

        bufferp += sizeof(header);

        if (read_all(sock, bufferp, data_len) < 0)
        {
            free(*p_buffer);
            *p_buffer = NULL;
            return -1;
        }

        /* Check for more data on the socket.  We want the entire
           message and SSL may have fragmented it. */
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        retval = select(sock + 1, &rfds, NULL, NULL, &tv);
        if (retval < 0) {
            free(*p_buffer);
            *p_buffer = NULL;
            return -1;
        }
    } while (retval == 1);

done:
    return tot_buffer_len;
}

/*
 * write_token()
 *
 * Write a token to the the given socket.
 *
 * Returns 0 on success, -1 on error.
 */
static int
write_token(const int sock,
            const char *buffer,
            const size_t buffer_size)
{
    int return_value;

    assert(buffer != NULL);

    return_value = write_all(sock, buffer, buffer_size);

    return (return_value == -1 ? -1 : 0);
}

static int
assist_write_token(void *sock,
                   void *buffer,
                   size_t buffer_size)
{
    assert(sock != NULL);
    assert(buffer != NULL);

    return write_token(*((int *) sock), (char *) buffer, buffer_size);
}

/*
 * GSI_SOCKET_set_error_from_verror()
 *
 * Set the given GSI_SOCKET's error state from verror.
 */
static void
GSI_SOCKET_set_error_from_verror(GSI_SOCKET *self)
{
    char                *string;

    if (verror_is_error() == 0)
    {
        return;
    }

    string = verror_get_string();

    if (string != NULL)
    {
        if (self->error_string) {
            free(self->error_string);
        }
        self->error_string = strdup(string);
    }

    self->error_number = verror_get_errno();
}


/*********************************************************************
 *
 * API Functions
 *
 */

GSI_SOCKET *
GSI_SOCKET_new(int sock)
{
    GSI_SOCKET *self = NULL;

    self = malloc(sizeof(GSI_SOCKET));

    if (self == NULL)
    {
        return NULL;
    }

    memset(self, 0, sizeof(GSI_SOCKET));

    self->gss_context = GSS_C_NO_CONTEXT;
    self->sock = sock;

    globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    globus_module_activate(GLOBUS_GSI_SYSCONFIG_MODULE);

    return self;
}


void
GSI_SOCKET_destroy(GSI_SOCKET *self)
{
    if (self == NULL)
    {
        return;
    }

    if (self->gss_context != GSS_C_NO_CONTEXT)
    {
        gss_buffer_desc output_token_desc  = GSS_C_EMPTY_BUFFER;

        gss_delete_sec_context(&self->minor_status,
                               &self->gss_context,
                               &output_token_desc);

        /* XXX Should deal with output_token_desc here */
        gss_release_buffer(&self->minor_status, &output_token_desc);
    }

    if (self->peer_name != NULL)
    {
        free(self->peer_name);
    }

    if (self->error_string)
    {
        free(self->error_string);
    }

    if (self->certreq)
    {
        free(self->certreq);
    }

    free(self);

#if 0 /* http://bugzilla.globus.org/bugzilla/show_bug.cgi?id=6909 */
    globus_module_deactivate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    globus_module_deactivate(GLOBUS_GSI_SYSCONFIG_MODULE);
#endif
}


int
GSI_SOCKET_get_error_string(GSI_SOCKET *self,
                            char *buffer,
                            int bufferlen)
{
    int total_chars = 0;
    int chars;


    if ((buffer == NULL) || (bufferlen == 0))
    {
        /* Punt */
        return -1;
    }

    if (self == NULL)
    {
        return my_strncpy(buffer, "GSI SOCKET not initialized", bufferlen);
    }

    if (self->error_string != NULL)
    {
        chars = my_strncpy(buffer, self->error_string, bufferlen);

        if (chars == -1)
        {
            goto truncated;
        }

        total_chars += chars;
        buffer = &buffer[chars];
        bufferlen -= chars;
    }

    if (self->error_number != 0)
    {
        if (total_chars && bufferlen && *(buffer - 1) != '\n') {
            *buffer = '\n'; buffer++; total_chars++; bufferlen--;
        }

        chars = my_strncpy(buffer, strerror(self->error_number), bufferlen);

        if (chars == -1)
        {
            goto truncated;
        }

        total_chars += chars;
        buffer = &buffer[chars];
        bufferlen -= chars;
    }

    if (self->major_status)
    {
        if (total_chars && bufferlen && *(buffer - 1) != '\n') {
            *buffer = '\n'; buffer++; total_chars++; bufferlen--;
        }

        /* Parse errors from gss-assist routines */
        switch(self->major_status)
        {
          case GSS_S_DEFECTIVE_TOKEN | GSS_S_CALL_INACCESSIBLE_READ:
          case GSS_S_DEFECTIVE_TOKEN | GSS_S_CALL_INACCESSIBLE_WRITE:
            chars = my_strncpy(buffer, "Connection closed.", bufferlen);
            break;
          default:
            chars = append_gss_status(buffer, bufferlen,
                                      self->major_status,
                                      GSS_C_GSS_CODE);

            if (chars == -1)
            {
                goto truncated;
            }

            total_chars += chars;
            buffer = &buffer[chars];
            bufferlen -= chars;

            chars = append_gss_status(buffer, bufferlen,
                                      self->minor_status,
                                      GSS_C_MECH_CODE);

            if (chars == -1)
            {
                goto truncated;
            }
        }

        total_chars += chars;
        buffer = &buffer[chars];
        bufferlen -= chars;
    }

    if (total_chars == 0)
    {
        /* No error */
        buffer[0] = '\0';
    }

    return total_chars;

  truncated:
    return -1;
}

void
GSI_SOCKET_clear_error(GSI_SOCKET *self)
{
    if (self == NULL)
    {
        return;
    }

    if (self->error_string != NULL)
    {
        free(self->error_string);
        self->error_string = NULL;
    }
    self->error_number = 0;
    self->major_status = 0;
    self->minor_status = 0;
}


int
GSI_SOCKET_allow_anonymous(GSI_SOCKET *self, const int value)
{
    if (self == NULL)
    {
        return GSI_SOCKET_ERROR;
    }

    self->allow_anonymous = value;

    return GSI_SOCKET_SUCCESS;
}

int
GSI_SOCKET_peer_used_limited_proxy(GSI_SOCKET *self)
{
    if (self == NULL) {
        return GSI_SOCKET_ERROR;
    }
    return self->limited_proxy;
}

int
GSI_SOCKET_set_peer_limited_proxy(GSI_SOCKET *self, int flag)
{
    if (self == NULL) {
        return GSI_SOCKET_ERROR;
    }
    self->limited_proxy = flag;
    return 0;
}

int
GSI_SOCKET_set_max_token_len(GSI_SOCKET *self, int bytes)
{
    if (self == NULL) {
        return GSI_SOCKET_ERROR;
    }
    self->max_token_len = bytes;
    return 0;
}

int
GSI_SOCKET_context_established(GSI_SOCKET *self)
{
    if (self->gss_context == GSS_C_NO_CONTEXT) {
        return 0;
    }

    return 1;
}

/* XXX This routine really needs a complete overhaul */
int
GSI_SOCKET_use_creds(GSI_SOCKET *self,
                     const char *creds)
{
    int return_code = GSI_SOCKET_ERROR;

    if (creds == NULL)
    {
        /* XXX Do nothing for now */
        return_code = GSI_SOCKET_SUCCESS;
    }
    else
    {
        unsetenv("X509_USER_CERT");
        unsetenv("X509_USER_KEY");
        return_code = (setenv("X509_USER_PROXY", creds, 1) == -1) ? GSI_SOCKET_ERROR : GSI_SOCKET_SUCCESS;
    }

    return return_code;
}

int
GSI_SOCKET_check_creds(GSI_SOCKET *self)
{
    gss_cred_id_t               creds = GSS_C_NO_CREDENTIAL;
    int                         return_value = GSI_SOCKET_ERROR;

    if (self == NULL) {
        return GSI_SOCKET_ERROR;
    }

    self->major_status = globus_gss_assist_acquire_cred(&self->minor_status,
                                                        GSS_C_BOTH,
                                                        &creds);

    if (self->major_status != GSS_S_COMPLETE) {
        goto error;
    }

    /* Success */
    return_value = GSI_SOCKET_SUCCESS;

  error:
    if (creds != GSS_C_NO_CREDENTIAL) {
        OM_uint32 minor_status;

        gss_release_cred(&minor_status, &creds);
    }

    return return_value;
}

int
GSI_SOCKET_authentication_init(GSI_SOCKET *self, gss_name_t accepted_peer_names[])
{
    int                         token_status;
    gss_cred_id_t               creds = GSS_C_NO_CREDENTIAL;
    gss_name_t                  server_gss_name = GSS_C_NO_NAME;
    OM_uint32                   req_flags = 0, ret_flags = 0;
    int                         return_value = GSI_SOCKET_ERROR;
    gss_buffer_desc             gss_buffer = { 0 };
    int                         i, rc = 0, sock;
    FILE                        *fp = NULL;
    char                        *cert_dir = NULL;
    globus_result_t             res;

    if (self == NULL)
    {
        return GSI_SOCKET_ERROR;
    }

    if (accepted_peer_names == NULL ||
        accepted_peer_names[0] == GSS_C_NO_NAME) {
        return GSI_SOCKET_ERROR;
    }

    if (self->gss_context != GSS_C_NO_CONTEXT)
    {
        GSI_SOCKET_set_error_string(self, "GSI_SOCKET already authenticated");
        goto error;
    }

    res = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&cert_dir);
    if (res == GLOBUS_SUCCESS) {
        myproxy_debug("using trusted certificates directory %s", cert_dir);
    } else {
        verror_put_string("error getting trusted certificates directory");
        globus_error_to_verror(res);
        goto error;
    }

    self->major_status = globus_gss_assist_acquire_cred(&self->minor_status,
                                                        GSS_C_INITIATE,
                                                        &creds);

    if (self->major_status != GSS_S_COMPLETE) {
        if (self->allow_anonymous) {
            req_flags |= GSS_C_ANON_FLAG;
            myproxy_debug("no valid credentials found -- "
                          "performing anonymous authentication");
        } else {
            goto error;
        }
    }

    req_flags |= GSS_C_REPLAY_FLAG;
    req_flags |= GSS_C_MUTUAL_FLAG;
    req_flags |= GSS_C_CONF_FLAG;
    req_flags |= GSS_C_INTEG_FLAG;

    if ((sock = dup(self->sock)) < 0) {
        GSI_SOCKET_set_error_string(self, "dup() of socket fd failed");
        self->error_number = errno;
        goto error;
    }
    if ((fp = fdopen(sock, "r")) == NULL) {
        GSI_SOCKET_set_error_string(self, "fdopen() of socket failed");
        self->error_number = errno;
        goto error;
    }
    if (setvbuf(fp, NULL, _IONBF, 0) != 0) {
        GSI_SOCKET_set_error_string(self, "setvbuf() for socket failed");
        self->error_number = errno;
        goto error;
    }

    self->major_status =
        globus_gss_assist_init_sec_context(&self->minor_status,
                                           creds,
                                           &self->gss_context,
                                           "GSI-NO-TARGET",
                                           req_flags,
                                           &ret_flags,
                                           &token_status,
                                           globus_gss_assist_token_get_fd,
                                           (void *)fp,
                                           assist_write_token,
                                           (void *)&self->sock);

    if (self->major_status != GSS_S_COMPLETE) {
        goto error;
    }


    /* Verify that all service requests were honored. */
    req_flags &= ~(GSS_C_ANON_FLAG); /* GSI GSSAPI doesn't set this flag */
    if ((req_flags & ret_flags) != req_flags) {
        GSI_SOCKET_set_error_string(self,
                                    "requested GSSAPI service not supported");
        goto error;
    }

    if (ret_flags & GSS_C_GLOBUS_LIMITED_PROXY_FLAG) {
        self->limited_proxy = 1;
    }

    /* Check the authenticated identity of the server. */
    self->major_status = gss_inquire_context(&self->minor_status,
                                             self->gss_context,
                                             NULL,
                                             &server_gss_name,
                                             NULL, NULL, NULL, NULL, NULL);
    if (self->major_status != GSS_S_COMPLETE) {
        GSI_SOCKET_set_error_string(self, "gss_inquire_context() failed");
        goto error;
    }

    self->major_status = gss_display_name(&self->minor_status,
                                          server_gss_name, &gss_buffer, NULL);
    if (self->major_status != GSS_S_COMPLETE) {
        GSI_SOCKET_set_error_string(self, "gss_display_name() failed");
        goto error;
    }

    self->peer_name = strdup(gss_buffer.value);
    myproxy_debug("server name: %s", self->peer_name);
    myproxy_debug("checking that server name is acceptable...");

    /* We told gss_assist_init_sec_context() not to check the server
       name so we can check it manually here. */
    for (i = 0; accepted_peer_names[i] != GSS_C_NO_NAME; i++) {
        gss_buffer_desc apn_gss_buffer = { 0 };

        self->major_status = gss_compare_name(&self->minor_status,
                                              server_gss_name,
                                              accepted_peer_names[i], &rc);
        if (self->major_status != GSS_S_COMPLETE) {
            OM_uint32 stM, stm;
            gss_buffer_desc errbuf = {0};

            char error_string[1050];
            stM = gss_display_status(&stm, self->minor_status, GSS_C_MECH_CODE,
                GSS_C_NO_OID, NULL, &errbuf);
            if (stM == GSS_S_COMPLETE && errbuf.length > 0)
            {
                strncpy(error_string, errbuf.value, sizeof error_string);
                error_string[1049] = '\0';
                gss_release_buffer(&stm, &errbuf);
            }
            else
            {
                strncpy(error_string, "gss_compare_name failed",
                        sizeof error_string);
                error_string[1049] = '\0';
            }

            GSI_SOCKET_set_error_string(self, error_string);
            goto error;
        }

        self->major_status = gss_display_name(&self->minor_status,
                                              accepted_peer_names[i],
                                              &apn_gss_buffer, NULL);
        if (self->major_status != GSS_S_COMPLETE) {
            GSI_SOCKET_set_error_string(self, "gss_display_name() failed");
            goto error;
        }

        if (rc) {
            myproxy_debug("server name matches \"%s\"",
                          apn_gss_buffer.value);
            break;
        } else {
            myproxy_debug("server name does not match \"%s\"",
                          apn_gss_buffer.value);
        }
        gss_release_buffer(&self->minor_status, &apn_gss_buffer);
    }
    if (!rc) {          /* no match with acceptable target names */
        GSI_SOCKET_set_error_string(self, "authenticated peer name does not match");
        return_value = GSI_SOCKET_UNAUTHORIZED;
        goto error;
    }
    myproxy_debug("authenticated server name is acceptable");

    /* Success */
    return_value = GSI_SOCKET_SUCCESS;

  error:
    {
        OM_uint32 minor_status;
        gss_release_cred(&minor_status, &creds);
        gss_release_buffer(&minor_status, &gss_buffer);
        gss_release_name(&minor_status, &server_gss_name);
    }
    if (cert_dir) free(cert_dir);
    if (fp) fclose(fp);

    return return_value;
}


int
GSI_SOCKET_authentication_accept(GSI_SOCKET *self)
{
    gss_cred_id_t               creds = GSS_C_NO_CREDENTIAL;
    int                         token_status;
    int                         return_value = GSI_SOCKET_ERROR;
    OM_uint32                   gss_flags = 0;
    int                         sock;
    FILE                        *fp = NULL;
    char                        *cert_dir = NULL;
    globus_result_t             res;

    if (self == NULL) {
        return GSI_SOCKET_ERROR;
    }

    if (self->gss_context != GSS_C_NO_CONTEXT) {
        GSI_SOCKET_set_error_string(self, "GSI_SOCKET already authenticated");
        goto error;
    }

    res = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&cert_dir);
    if (res == GLOBUS_SUCCESS) {
        myproxy_debug("using trusted certificates directory %s", cert_dir);
    } else {
        verror_put_string("error getting trusted certificates directory");
        globus_error_to_verror(res);
        goto error;
    }

    self->major_status = globus_gss_assist_acquire_cred(&self->minor_status,
                                                        GSS_C_ACCEPT,
                                                        &creds);

    if (self->major_status != GSS_S_COMPLETE) {
        goto error;
    }

    /* These are supposed to be return flags only, according to RFC
       2774, but GSI helpfully uses them as request flags too. */
    gss_flags |= GSS_C_REPLAY_FLAG;
    gss_flags |= GSS_C_MUTUAL_FLAG;
    gss_flags |= GSS_C_CONF_FLAG;
    gss_flags |= GSS_C_INTEG_FLAG;

    if ((sock = dup(self->sock)) < 0) {
        GSI_SOCKET_set_error_string(self, "dup() of socket fd failed");
        self->error_number = errno;
        goto error;
    }
    if ((fp = fdopen(sock, "r")) == NULL) {
        GSI_SOCKET_set_error_string(self, "fdopen() of socket failed");
        self->error_number = errno;
        goto error;
    }
    if (setvbuf(fp, NULL, _IONBF, 0) != 0) {
    GSI_SOCKET_set_error_string(self, "setvbuf() for socket failed");
        self->error_number = errno;
        goto error;
    }

    self->major_status =
        globus_gss_assist_accept_sec_context(&self->minor_status,
                                             &self->gss_context,
                                             creds,
                                             &self->peer_name,
                                             &gss_flags,
                                             NULL, /* u2u flag */
                                             &token_status,
                                             NULL, /* Delegated creds
                                                    * added in Globus 1.1.3
                                                    */
                                             globus_gss_assist_token_get_fd,
                                             (void *)fp,
                                             assist_write_token,
                                             (void *)&self->sock);

    if (self->major_status != GSS_S_COMPLETE) {
        goto error;
    }

    if (!(gss_flags & GSS_C_CONF_FLAG)) {
        GSI_SOCKET_set_error_string(self,
                                    "requested confidentiality GSSAPI service"
                                    " but it is not available");
        goto error;
    }

    if (gss_flags & GSS_C_GLOBUS_LIMITED_PROXY_FLAG) {
        self->limited_proxy = 1;
    }

    /* Success */
    return_value = GSI_SOCKET_SUCCESS;

  error:
    if (creds != GSS_C_NO_CREDENTIAL) {
        OM_uint32 minor_status;

        gss_release_cred(&minor_status, &creds);
    }
    if (cert_dir) free(cert_dir);
    if (fp) fclose(fp);

    return return_value;
}

int
GSI_SOCKET_get_peer_name(GSI_SOCKET *self,
                         char *buffer,
                         const int buffer_len)
{
    int return_value = GSI_SOCKET_ERROR;

    if (self == NULL)
    {
        return GSI_SOCKET_ERROR;
    }

    if (buffer == NULL)
    {
        self->error_number = EINVAL;
        return GSI_SOCKET_ERROR;
    }

    if (self->peer_name == NULL)
    {
        GSI_SOCKET_set_error_string(self, "Client not authenticated");
        goto error;
    }

    return_value = my_strncpy(buffer, self->peer_name, buffer_len);

    if (return_value == -1)
    {
        return_value = GSI_SOCKET_TRUNCATED;
        goto error;
    }

    /* SUCCESS */
    return_value = GSI_SOCKET_SUCCESS;

  error:
    return return_value;
}


char *
GSI_SOCKET_get_peer_hostname(GSI_SOCKET *self)
{
    struct sockaddr_storage addr;
    socklen_t                   addr_len = sizeof(addr);
    char host                   [NI_MAXHOST];
    int                         loopback = 0;

    if (getpeername(self->sock, (struct sockaddr *) &addr,
                    &addr_len) < 0) {
        self->error_number = errno;
        GSI_SOCKET_set_error_string(self, "Could not get peer address");
        return NULL;
    }

    if (getnameinfo((struct sockaddr *) &addr, addr_len,
                    host, sizeof(host),
                    NULL, 0, NI_NAMEREQD)) {
        self->error_number = errno;
        GSI_SOCKET_set_error_string(self, "Could not get peer hostname");
        return NULL;
    }

    /* check for localhost / loopback */
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in sadder;

        memcpy(&sadder, &addr, sizeof(sadder));
        if (ntohl(sadder.sin_addr.s_addr) == INADDR_LOOPBACK) {
            loopback = 1;
        }
    }
#ifdef AF_INET6
    else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6 saddr6;

        memcpy(&saddr6, &addr, sizeof(saddr6));
        if (IN6_IS_ADDR_LOOPBACK(&saddr6.sin6_addr)) {
            loopback = 1;
        }
    }
#endif

    if (loopback) {
        char buf[MAXHOSTNAMELEN];
        if (gethostname(buf, sizeof(buf)) < 0) {
            self->error_number = errno;
            GSI_SOCKET_set_error_string(self, "gethostname() failed");
            return NULL;
        }
        return strdup(buf);
    }

    return strdup(host);
}

int
GSI_SOCKET_get_peer_fqans(GSI_SOCKET *self, char ***fqans)
{
    static int once = 1;
    static void *myproxy_voms_handle = 0;
    static int (*myproxy_voms_get_peer_fqans)() = 0;

    if (once)
    {
        once = 0;
        myproxy_voms_handle = dlopen("libmyproxy_voms.so",
            RTLD_LAZY|RTLD_LOCAL);
        if (myproxy_voms_handle != NULL)
        {
            myproxy_voms_get_peer_fqans = dlsym(myproxy_voms_handle,
                    "GSI_SOCKET_get_peer_fqans");
        }
    }

    *fqans = NULL;

    if (myproxy_voms_get_peer_fqans != NULL)
    {
        return myproxy_voms_get_peer_fqans(self, fqans);
    }
    else
    {
        return 0;
    }
}

int
GSI_SOCKET_write_buffer(GSI_SOCKET *self,
                        const char *buffer,
                        const size_t buffer_len)
{
    int return_value = GSI_SOCKET_ERROR;

    if (self == NULL)
    {
        return GSI_SOCKET_ERROR;
    }

#if 0
    if (buffer[buffer_len - 1] == '\0') {
        myproxy_debug("writing a null-terminated message");
    } else {
        myproxy_debug("writing a non-null-terminated message");
    }
#endif

    if ((buffer == NULL) || (buffer_len == 0))
    {
        return 0;
    }

    if (self->gss_context == GSS_C_NO_CONTEXT)
    {
        /* No context established, just send in the clear */
        return_value = write_token(self->sock, buffer, buffer_len);

        if (return_value == -1)
        {
            self->error_number = errno;
            GSI_SOCKET_set_error_string(self, "failed to write token");
            goto error;
        }
    }
    else
    {
        /* Encrypt buffer before sending */
        gss_buffer_desc unwrapped_buffer;
        gss_buffer_desc wrapped_buffer;
        int conf_state;

        unwrapped_buffer.value = (char *) buffer;
        unwrapped_buffer.length = buffer_len;

        self->major_status = gss_wrap(&self->minor_status,
                                      self->gss_context,
                                      1 /* encrypt */,
                                      GSS_C_QOP_DEFAULT,
                                      &unwrapped_buffer,
                                      &conf_state,
                                      &wrapped_buffer);

        if (self->major_status != GSS_S_COMPLETE)
        {
            goto error;
        }

        if (!conf_state) {
            GSI_SOCKET_set_error_string(self, "GSI_SOCKET failed to encrypt");
            goto error;
        }

        return_value = write_token(self->sock, wrapped_buffer.value,
                                   wrapped_buffer.length);

        if (return_value == -1)
        {
            self->error_number = errno;
            GSI_SOCKET_set_error_string(self, "failed to write token");
            gss_release_buffer(&self->minor_status, &wrapped_buffer);
            goto error;
        }

        gss_release_buffer(&self->minor_status, &wrapped_buffer);
    }
/*     fprintf(stderr, "\nwrote:\n%s\n", buffer); */
  error:
    return return_value;
}

static
size_t safe_strlen(const char s[], size_t bufsiz)
{
    int i;
    for (i = 0; i < bufsiz; i++) {
        if (s[i] == '\0') {
            return i;
        }
    }
    return i;
}

int GSI_SOCKET_read_token(GSI_SOCKET *self,
                          unsigned char **pbuffer,
                          size_t *pbuffer_len)
{
    int                 bytes_read;
    static unsigned char *saved_buffer = NULL; /* not thread safe! */
    static int          saved_buffer_len = 0;
    unsigned char       *buffer;
    int                 return_status = GSI_SOCKET_ERROR;

    if (saved_buffer) {

        buffer = saved_buffer;
        bytes_read = saved_buffer_len;
        saved_buffer = NULL;
        saved_buffer_len = 0;

    } else {

        bytes_read = read_token(self->sock,
                                (char **) &buffer,
                                self->max_token_len);

        if (bytes_read == -1)
        {
            self->error_number = errno;
            GSI_SOCKET_set_error_string(self, "failed to read token");
            goto error;
        }

    if (bytes_read == 0)
    {
        self->error_number = errno;
        GSI_SOCKET_set_error_string(self, "connection closed");
        goto error;
    }

        if (self->gss_context != GSS_C_NO_CONTEXT)
        {
            /* Need to unwrap read data */
            gss_buffer_desc unwrapped_buffer;
            gss_buffer_desc wrapped_buffer;
            int conf_state;
            gss_qop_t qop_state;

            wrapped_buffer.value = buffer;
            wrapped_buffer.length = bytes_read;

            self->major_status = gss_unwrap(&self->minor_status,
                                            self->gss_context,
                                            &wrapped_buffer,
                                            &unwrapped_buffer,
                                            &conf_state,
                                            &qop_state);

            free(buffer);

            if (self->major_status != GSS_S_COMPLETE)
            {
                goto error;
            }

            buffer = unwrapped_buffer.value;
            bytes_read = unwrapped_buffer.length;
        }

    }

    if (bytes_read == 0)
    {
        self->error_number = errno;
        GSI_SOCKET_set_error_string(self, "connection closed");
        goto error;
    }

    /* HACK: We may have multiple tokens concatenated together here.
       Unfortunately, our protocol doesn't do a good job of message
       framing.  Still, we can find the start/end of some messages
       by looking for the standard VERSION string at the start. */
    if (strncmp((const char *)buffer, "VERSION", strlen("VERSION")) == 0) {
        size_t token_len = safe_strlen((const char *)buffer, bytes_read) + 1;
        if (bytes_read > token_len) {

            /* Our buffer is bigger than one message.  Just return the
               one message here and save the rest for later. */

            char *old_buffer;

            old_buffer = (char *)buffer;
            saved_buffer_len = bytes_read - token_len;
            buffer = malloc(token_len);
            memcpy(buffer, old_buffer, token_len);
            saved_buffer = malloc(saved_buffer_len);
            memcpy(saved_buffer, old_buffer+token_len, saved_buffer_len);
            bytes_read = token_len;
            free(old_buffer);
        }
    }

    /* Success */
    *pbuffer = buffer;
    *pbuffer_len = bytes_read;
    return_status = GSI_SOCKET_SUCCESS;
/*     fprintf(stderr, "\nread:\n%s\n", buffer); */

#if 0
    if (buffer[bytes_read - 1] == '\0') {
        myproxy_debug("read a null-terminated message");
    } else {
        myproxy_debug("read a non-null-terminated message");
    }
#endif

  error:
    return return_status;
}

void GSI_SOCKET_free_token(unsigned char *buffer)
{
    if (buffer != NULL)
    {
        free(buffer);
    }
}

int GSI_SOCKET_delegation_init_ext(GSI_SOCKET *self,
                                   const char *source_credentials,
                                   int lifetime,
                                   const char *passphrase)
{
    int                         return_value = GSI_SOCKET_ERROR;
    SSL_CREDENTIALS             *creds = NULL;
    SSL_PROXY_RESTRICTIONS      *proxy_restrictions = NULL;
    unsigned char               *input_buffer = NULL;
    size_t                      input_buffer_length;
    unsigned char               *output_buffer = NULL;
    int                         output_buffer_length;


    if (self == NULL)
    {
        goto error;
    }

    if (self->gss_context == GSS_C_NO_CONTEXT)
    {
        GSI_SOCKET_set_error_string(self, "GSI_SOCKET not authenticated");
        goto error;
    }

    /*
     * Load proxy we are going to use to sign delegation
     */
    creds = ssl_credentials_new();

    if (creds == NULL)
    {
        GSI_SOCKET_set_error_from_verror(self);
        goto error;
    }

    if (passphrase && passphrase[0] == '\0') {
        passphrase = NULL;
    }

    if (ssl_proxy_load_from_file(creds, source_credentials,
                                 passphrase) == SSL_ERROR)
    {
        GSI_SOCKET_set_error_from_verror(self);
        goto error;
    }

    /*
     * Read the certificate request from the client
     */
    if (GSI_SOCKET_read_token(self, &input_buffer,
                              &input_buffer_length) == GSI_SOCKET_ERROR)
    {
        goto error;
    }

    /* HACK: We may get an error message rather than a certreq... */
    if (strncmp((const char *)input_buffer, "VERSION",
                strlen("VERSION")) == 0) {
        myproxy_response_t *response;
        response = malloc(sizeof(*response));
        memset(response, 0, sizeof(*response));
        myproxy_handle_response((const char *)input_buffer,
                                input_buffer_length,
                                response);
        myproxy_free(NULL, NULL, response);
        GSI_SOCKET_set_error_string(self, "server-side error: check server logs");
        goto error;
    }

    /*
     * Set up the restrictions on the proxy
     */
    proxy_restrictions = ssl_proxy_restrictions_new();

    if (proxy_restrictions == NULL)
    {
        goto error;
    }

    if (ssl_proxy_restrictions_set_lifetime(proxy_restrictions,
                                            (long) lifetime) == SSL_ERROR)
    {
        goto error;
    }

    if (GSI_SOCKET_peer_used_limited_proxy(self)) {
        ssl_proxy_restrictions_set_limited(proxy_restrictions, 1);
    }

    /*
     * Sign the request
     */
    if (ssl_proxy_delegation_sign(creds,
                                  proxy_restrictions,
                                  input_buffer,
                                  input_buffer_length,
                                  &output_buffer,
                                  &output_buffer_length) == SSL_ERROR)
    {
        GSI_SOCKET_set_error_from_verror(self);
        goto error;
    }

    /*
     * Write the proxy certificate back to user
     */
    if (GSI_SOCKET_write_buffer(self,
                                (const char *)output_buffer,
                                output_buffer_length) == GSI_SOCKET_ERROR)
    {
        goto error;
    }

    /* Success */
    return_value = GSI_SOCKET_SUCCESS;

  error:
    if (input_buffer != NULL)
    {
        GSI_SOCKET_free_token(input_buffer);
    }

    if (output_buffer != NULL)
    {
        ssl_free_buffer(output_buffer);
    }

    if (creds != NULL)
    {
        ssl_credentials_destroy(creds);
    }

    if (proxy_restrictions != NULL)
    {
        ssl_proxy_restrictions_destroy(proxy_restrictions);
    }

    return return_value;
}


int
GSI_SOCKET_delegation_accept(GSI_SOCKET *self,
                             unsigned char **delegated_credentials,
                             int *delegated_credentials_len,
                             char *passphrase)
{
    int                 return_value = GSI_SOCKET_ERROR;
    SSL_CREDENTIALS     *creds = NULL;
    unsigned char       *output_buffer = NULL;
    int                 output_buffer_len;
    unsigned char       *input_buffer = NULL;
    size_t              input_buffer_len;
    unsigned char       *fmsg;
    int                 i;

    if (self == NULL)
    {
        return GSI_SOCKET_ERROR;
    }

    if ((delegated_credentials == NULL) ||
        (delegated_credentials_len == 0))
    {
        self->error_number = EINVAL;
        goto error;
    }

    if (self->gss_context == GSS_C_NO_CONTEXT)
    {
        GSI_SOCKET_set_error_string(self, "GSI_SOCKET not authenticated");
        return GSI_SOCKET_ERROR;
    }

    if (self->certreq) {

        creds = ssl_credentials_new();
        if (ssl_certreq_pem_to_der(self->certreq, &output_buffer,
                                   &output_buffer_len) == SSL_ERROR) {
            GSI_SOCKET_set_error_from_verror(self);
            goto error;
        }

    } else {

        /* Generate proxy certificate request and send */
        if (ssl_proxy_delegation_init(&creds, &output_buffer, &output_buffer_len,
                                      0 /* default number of bits */,
                                      NULL /* No callback */) == SSL_ERROR)
        {
            GSI_SOCKET_set_error_from_verror(self);
            goto error;
        }

    }

    if (GSI_SOCKET_write_buffer(self, (const char *)output_buffer,
                                output_buffer_len) == GSI_SOCKET_ERROR)
    {
        goto error;
    }

    /* Now read the signed certificate */
    if (GSI_SOCKET_read_token(self, &input_buffer,
                              &input_buffer_len) == GSI_SOCKET_ERROR)
    {
        goto error;
    }

    /* HACK: We may get just an error message rather than a cert... */
    if (strncmp((const char *)input_buffer, "VERSION",
                strlen("VERSION")) == 0) {
        myproxy_response_t *response;
        response = malloc(sizeof(*response));
        memset(response, 0, sizeof(*response));
        myproxy_handle_response((const char *)input_buffer,
                                input_buffer_len,
                                response);
        myproxy_free(NULL, NULL, response);
        GSI_SOCKET_set_error_string(self,
                                    "server-side error: check server logs");
        goto error;
    }

    /* MAJOR HACK:
       We don't have application-level framing in our protocol.
       We can't separate the certificate chain easily from
       the final protocol message, so just discard it. */
    fmsg = input_buffer;
    for (i = 0; i < input_buffer_len-strlen("VERSION"); i++, fmsg++) {
        if (strncmp((const char *)fmsg, "VERSION", strlen("VERSION")) == 0) {
            input_buffer_len = fmsg-input_buffer;
            break;
        }
    }

    if (ssl_proxy_delegation_finalize(creds, input_buffer,
                                      input_buffer_len) == SSL_ERROR)
    {
        GSI_SOCKET_set_error_from_verror(self);
        goto error;
    }

    if (passphrase && passphrase[0] == '\0') {
        passphrase = NULL;
    }
    if (ssl_proxy_to_pem(creds, delegated_credentials,
                         delegated_credentials_len, passphrase) == SSL_ERROR)
    {
        GSI_SOCKET_set_error_from_verror(self);
        goto error;
    }

    /* Success */
    return_value = GSI_SOCKET_SUCCESS;

  error:
    if (creds != NULL)
    {
        ssl_credentials_destroy(creds);
    }

    if (input_buffer != NULL)
    {
        GSI_SOCKET_free_token(input_buffer);
    }

    if (output_buffer != NULL)
    {
        ssl_free_buffer(output_buffer);
    }

    return return_value;
}

int
GSI_SOCKET_delegation_accept_ext(GSI_SOCKET *self,
                                 char *delegated_credentials,
                                 int delegated_credentials_len,
                                 char *passphrase)
{
    int                 return_value = GSI_SOCKET_ERROR;
    unsigned char       *output_buffer = NULL;
    int                 output_buffer_len;
    char                *filename = NULL;
    int                 fd = -1;


    if (GSI_SOCKET_delegation_accept(self, &output_buffer, &output_buffer_len,
                                     passphrase) != GSI_SOCKET_SUCCESS) {
        goto error;
    }

    /* Now store the credentials */
    filename = myproxy_creds_path_template();
    if (filename == NULL) {     /* should never happen */
        verror_put_string("myproxy_creds_path_template() failed");
        goto error;
    }

    fd = mkstemp(filename);
    if (fd == -1)
    {
        verror_put_string("Error creating temporary file (%s)", filename);
        verror_put_errno(errno);
        goto error;
    }

    if (write(fd, output_buffer, output_buffer_len) == -1)
    {
        verror_put_errno(errno);
        verror_put_string("Error writing proxy to %s", filename);
        goto error;
    }

    if (delegated_credentials != NULL)
    {
        if (my_strncpy(delegated_credentials, filename,
                       delegated_credentials_len) < 0) {
            verror_put_string("credential path too long");
            goto error;
        }
    }

    /* Success */
    return_value = GSI_SOCKET_SUCCESS;

  error:
    if (output_buffer != NULL)
    {
        ssl_free_buffer(output_buffer);
    }
    if (fd >= 0) close(fd);
    if (return_value != GSI_SOCKET_SUCCESS && fd >= 0)
    {
        ssl_proxy_file_destroy(filename);
    }
    if (filename) free(filename);

    return return_value;
}

int
GSI_SOCKET_delegation_set_certreq(GSI_SOCKET *self,
                                  char *certreq)
{
    if (self->certreq) {
        free(self->certreq);
        self->certreq = NULL;
    }
    self->certreq = strdup(certreq);
    return GSI_SOCKET_SUCCESS;
}

int GSI_SOCKET_credentials_accept_ext(GSI_SOCKET *self,
                                      char       *credentials,
                                      int         credentials_len)
{
    int                        return_value       = GSI_SOCKET_ERROR;
    SSL_CREDENTIALS           *creds              = NULL;
    SSL_PROXY_RESTRICTIONS    *proxy_restrictions = NULL;
    unsigned char             *input_buffer       = NULL;
    size_t                     input_buffer_length;
    unsigned char             *fmsg;
    int                        i;
    char                      *filename = NULL;
    char                      *certstart;
    int                        rval,
                               fd                 = 0;
    int                        size;


    if (self == NULL)
    {
        goto error;
    }

    if (self->gss_context == GSS_C_NO_CONTEXT)
    {
        GSI_SOCKET_set_error_string(self, "GSI_SOCKET not authenticated");
        goto error;
    }

    /* Read the Cred sent from the client. */
    if (GSI_SOCKET_read_token(self,
                              &input_buffer,
                              &input_buffer_length) == GSI_SOCKET_ERROR)
    {
        goto error;
    }
    myproxy_debug( "Read credentials" );

    /* MAJOR HACK:
       We don't have application-level framing in our protocol.
       We can't separate the certificate chain easily from
       the final protocol message, so just discard it. */
    fmsg = input_buffer;
    for (i = 0; i < input_buffer_length-strlen("VERSION"); i++, fmsg++) {
        if (strncmp((const char *)fmsg, "VERSION", strlen("VERSION")) == 0) {
            input_buffer_length = fmsg-input_buffer;
            break;
        }
    }

    /* Now store the credentials */
    filename = myproxy_creds_path_template();
    if (filename == NULL) {     /* should never happen */
        verror_put_string("myproxy_creds_path_template() failed");
        goto error;
    }

    fd = mkstemp(filename);
    if (fd == -1)
    {
        verror_put_string("Error creating temporary file (%s)", filename);
        verror_put_errno(errno);
        goto error;
    }

    size = strlen( (char *)input_buffer );

    certstart = (char *)input_buffer;

    while (size)
    {
        if ((rval = write(fd, certstart, size)) < 0)
        {
            perror("write");
            goto error;
        }

        size -= rval;
        certstart += rval;
    }

    if (write(fd, "\n\0", 1) < 0)
    {
        perror("write");
        goto error;
    }

    if (my_strncpy(credentials, filename, credentials_len) < 0) {
        verror_put_string("credential path too long");
        goto error;
    }

    /* Success */
    return_value = GSI_SOCKET_SUCCESS;

  error:
    if (input_buffer != NULL)
    {
        GSI_SOCKET_free_token(input_buffer);
    }

    if (creds != NULL)
    {
        ssl_credentials_destroy(creds);
    }

    if (proxy_restrictions != NULL)
    {
        ssl_proxy_restrictions_destroy(proxy_restrictions);
    }

    if (fd >= 0)
    {
        close(fd);
    }

    if (return_value != GSI_SOCKET_SUCCESS && fd >= 0)
    {
        ssl_proxy_file_destroy(filename);
    }

    if (filename) free(filename);

    return return_value;
}

int
GSI_SOCKET_credentials_init_ext(GSI_SOCKET *self,
                                const char *source_credentials)
{
    int                        return_value       = GSI_SOCKET_ERROR;
    SSL_PROXY_RESTRICTIONS    *proxy_restrictions = NULL;
    unsigned char             *output_buffer      = NULL;

    if (self == NULL)
    {
        goto error;
    }

    if (self->gss_context == GSS_C_NO_CONTEXT)
    {
        GSI_SOCKET_set_error_string(self, "GSI_SOCKET not authenticated");
        goto error;
    }

    if (GSI_SOCKET_write_buffer(self,
                                source_credentials,
                                strlen(source_credentials) + 1)
        == GSI_SOCKET_ERROR)
    {
        goto error;
    }

    /* Success */
    return_value = GSI_SOCKET_SUCCESS;

  error:
    if (output_buffer != NULL)
    {
        ssl_free_buffer(output_buffer);
    }

    if (proxy_restrictions != NULL)
    {
        ssl_proxy_restrictions_destroy(proxy_restrictions);
    }

    return return_value;
}

int
GSI_SOCKET_get_creds(GSI_SOCKET *self,
                     const char *source_credentials)
{
    int                          return_value       = GSI_SOCKET_ERROR;
    unsigned char               *output_buffer      = NULL;
    int                          output_buffer_length;


    if (self == NULL)
    {
        goto error;
    }

    if (self->gss_context == GSS_C_NO_CONTEXT)
    {
        GSI_SOCKET_set_error_string(self, "GSI_SOCKET not authenticated");
        goto error;
    }

    if (buffer_from_file(source_credentials, &output_buffer,
                         &output_buffer_length) < 0) {
        GSI_SOCKET_set_error_from_verror(self);
        goto error;
    }

    /*
     * Write the proxy certificate back to user
     */
    myproxy_debug( "Sending credential" );
    if (GSI_SOCKET_write_buffer(self,
                  (const char *)output_buffer,
                                output_buffer_length) == GSI_SOCKET_ERROR)
    {
        goto error;
    }

    /* Success */
    return_value = GSI_SOCKET_SUCCESS;

  error:
    if (output_buffer != NULL)
    {
        free(output_buffer);
    }

    return return_value;
}

int
GSI_SOCKET_get_errno(GSI_SOCKET *self)
{
    if (self)
        return self->error_number;

    return 0;
}
