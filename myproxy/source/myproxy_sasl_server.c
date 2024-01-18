#if defined(HAVE_LIBSASL2)

#include "myproxy_common.h"     /* all needed headers included here */

int myproxy_sasl_authenticated = 0;
char *myproxy_sasl_mech = NULL;
char *myproxy_sasl_serverFQDN = NULL;
char *myproxy_sasl_user_realm = NULL;

static sasl_conn_t *conn = NULL;

static void
sasl_free_conn(void)
{
  if (conn)
    sasl_dispose(&conn);
}


static int
sasl_my_log(void *context __attribute__((unused)),
            int priority,
            const char *message)
{
  const char *label;

  if (! message)
    return SASL_BADPARAM;

  switch (priority) {
  case SASL_LOG_ERR:
    label = "Error";
    break;
  case SASL_LOG_NOTE:
    label = "Info";
    break;
  default:
    label = "Other";
    break;
  }

  myproxy_log("SASL %s: %s\n", label, message);

  return SASL_OK;
}


static sasl_callback_t callbacks[] = {
  {
    SASL_CB_LOG, (int(*)(void)) (&sasl_my_log), NULL
  }, {
    SASL_CB_LIST_END, NULL, NULL
  }
};

static int
send_response_sasl_data(myproxy_socket_attrs_t *attrs,
                        const char *data, int data_len)
{
    myproxy_response_t response = {0};
    authorization_data_t*       auth_data;
    char        buf[SASL_BUFFER_SIZE];
    int         result;
    unsigned    len=0;

    result = sasl_encode64(data, data_len, buf, SASL_BUFFER_SIZE, &len);
    buf[len] = '\0';
    if (result != SASL_OK) {
        verror_put_string("Encoding SASL data in base64 failed.\n");
        verror_put_errno(errno);
        return -1;
    }
    myproxy_debug("S: %s", buf);

    memset(&response, 0, sizeof (response));
    response.version = strdup(MYPROXY_VERSION);
    response.response_type = MYPROXY_AUTHORIZATION_RESPONSE;
    response.authorization_data = malloc(sizeof(authorization_data_t*) * 2);
    response.authorization_data[0] = malloc(sizeof(authorization_data_t));
    response.authorization_data[1] = NULL;

    auth_data = response.authorization_data[0];
    auth_data->server_data = strdup(buf);
    auth_data->client_data = NULL;
    auth_data->client_data_len = 0;
    auth_data->method = AUTHORIZETYPE_SASL;

    len = myproxy_serialize_response(&response, buf, sizeof(buf));
    if (len < 0) {
        verror_put_string("error in myproxy_serialize_response()");
        return -1;
    }

    if (myproxy_send(attrs, buf, len) < 0) {
        verror_put_string("error in myproxy_send()\n");
        return -1;
    }

    free(response.version);
    authorization_data_free(response.authorization_data);

    return 0;
}


static int
recv_response_sasl_data(myproxy_socket_attrs_t *attrs, char *data)
{
    char  buf[SASL_BUFFER_SIZE];
    int   result;
    unsigned len;
    author_method_t client_auth_method;
    char *b64data;

    int   client_data_len = 0;

    len = myproxy_recv(attrs, buf, sizeof(buf));

    if (len <= 0)
        return -1;

    client_auth_method = (*buf);
    if (client_auth_method != AUTHORIZETYPE_SASL) {
        verror_put_string("SASL method not match.\n");
        verror_put_errno(errno);
        return -1;
    }

    client_data_len = len - sizeof(int);

    b64data = buf + sizeof(int);
    myproxy_debug("C: %s", b64data);
    result = sasl_decode64(b64data, strnlen(b64data, client_data_len),
                           data, SASL_BUFFER_SIZE, &len);
    if (result != SASL_OK) {
        myproxy_log("Decoding data from base64 failed in recv_response_sasl_data.");
        return -1;
    }
    data[len] = '\0';

    return len;
}

int
auth_sasl_negotiate_server(myproxy_socket_attrs_t *attrs,
                           myproxy_request_t *client_request)
{
    char  client_buffer[SASL_BUFFER_SIZE];
    int   client_data_len = 0;

    unsigned len;
    int count;
    const char *data;
    sasl_security_properties_t secprops;
    int result;
    int rval = -1;

    char *iplocal = NULL, *ipremote = NULL;
    char *service = "myproxy";

    char *userandrealm = NULL;

    myproxy_debug("Server: begin SASL negotiation...");
    myproxy_sasl_authenticated = 0;

    if (getenv("SASL_PATH")) {
        myproxy_debug("$SASL_PATH is %s", getenv("SASL_PATH"));
    } else {
        myproxy_debug("$SASL_PATH isn't set. Using /usr/lib/sasl2.");
    }

    result = sasl_server_init(callbacks, service);
    if (result != SASL_OK) {
        myproxy_log("Initializing libsasl failed.");
        return -1;
    }

    atexit(&sasl_done);

    result = sasl_server_new(service,
                             myproxy_sasl_serverFQDN,
                             myproxy_sasl_user_realm,
                             iplocal,
                             ipremote,
                             NULL,
                             0,
                             &conn);
    if (result != SASL_OK) {
        myproxy_log("Allocating sasl connection state failed.");
        return -1;
    }

    atexit(&sasl_free_conn);

    /* don't need integrity or privacy, since we're over SSL already.
       in fact, let's disable them to avoid the overhead. */
    memset(&secprops, 0L, sizeof(secprops));
    result = sasl_setprop(conn, SASL_SEC_PROPS, &secprops);
    if (result != SASL_OK) {
        myproxy_log("Setting security properties failed.");
        return -1;
    }

    if (myproxy_sasl_mech) {
        myproxy_debug("Forcing use of SASL mechanism %s", myproxy_sasl_mech);
        data = myproxy_sasl_mech;
        if (! data) {
            myproxy_log("Duplicate string for SASL negotiation failed");
            return -1;
        }
        len = strlen(data);
        count = 1;
    } else {
        myproxy_debug("Generating SASL mechanism list...");
        result = sasl_listmech(conn,
                               NULL,
                               NULL,
                               " ",
                               NULL,
                               &data,
                               &len,
                               &count);
        if (result != SASL_OK) {
            myproxy_log("Generating SASL mechanism list failed.");
            return -1;
        }
        if (count == 0) {
            myproxy_log("No SASL mechanisms available.");
            return -1;
        }
    }

    myproxy_debug("Sending list of %d mechanism(s): %s", count, data);
    if (send_response_sasl_data(attrs, data, len) < 0) {
        return -1;
    }

    myproxy_debug("Waiting for client mechanism...");
    len = recv_response_sasl_data(attrs, client_buffer);

    if (myproxy_sasl_mech && strcasecmp(myproxy_sasl_mech, client_buffer)) {
        myproxy_log(
            "Client chose something other than the mandatory mechanism.");
        return -1;
    }
    if (strlen(client_buffer) < len) {
        data = client_buffer + strlen(client_buffer) + 1;
        len = len - strlen(client_buffer) - 1;
    } else {
        data = NULL;
        len = 0;
    }

    result = sasl_server_start(conn,
                               client_buffer,
                               data,
                               len,
                               &data,
                               &len);
    if (result != SASL_OK && result != SASL_CONTINUE) {
        myproxy_log("Starting SASL negotiation failed.");
        verror_put_string("%s", sasl_errdetail(conn));
        return -1;
    }

    while (result == SASL_CONTINUE) {
        if (data) {
            myproxy_debug("Sending response...");
            if (send_response_sasl_data(attrs, data, len) < 0) {
                return -1;
            }
        } else {
            myproxy_log("No SASL data to send--something's wrong");
            return -1;
        }

        myproxy_debug("Waiting for client reply...");
        client_data_len = recv_response_sasl_data(attrs, client_buffer);
        data = NULL;
        result = sasl_server_step(conn, client_buffer, client_data_len,
                                  &data, &len);

        if (result != SASL_OK && result != SASL_CONTINUE) {
            verror_put_string("%s", sasl_errdetail(conn));
            myproxy_log("Performing SASL negotiation failed.");
            return -1;
        }
    }
    myproxy_debug("SASL negotiation complete.");

    if (sasl_getprop(conn, SASL_USERNAME, (const void **)&data) != SASL_OK) {
        myproxy_log("Error: SASL username is NULL.");
        return -1;
    }

    if (myproxy_sasl_user_realm) {
        size_t len;
        len = strlen(client_request->username) +
            strlen(myproxy_sasl_user_realm) + 2;
        userandrealm = malloc(len);
        snprintf(userandrealm, len, "%s@%s",
                 client_request->username, myproxy_sasl_user_realm);
    } else {
        userandrealm = strdup(client_request->username);
    }

    if (strcmp((char *)data, userandrealm) != 0) {
        myproxy_log("Authentication failure: SASL username (%s) and "
                    "request username (%s) differ.\n", (char *)data,
                    userandrealm);
        goto error;
    }

    if (sasl_getprop(conn, SASL_AUTHUSER, (const void **)&data) != SASL_OK) {
        myproxy_log("Error: SASL username is NULL.");
        goto error;
    }

    if (strcmp((char *)data, userandrealm) != 0) {
        myproxy_log("Authentication failure: SASL authuser (%s) and "
                    "request username (%s) differ.\n", (char *)data,
                    userandrealm);
        goto error;
    }

    myproxy_sasl_authenticated = 1; /* for later sanity checks */
    rval = 0;

 error:
    if (userandrealm) free(userandrealm);

    return rval;
}

#endif /* defined(HAVE_LIBSASL2) */
