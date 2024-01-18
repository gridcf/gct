#if defined(HAVE_LIBSASL2)

#include "myproxy_common.h"     /* all needed headers included here */

static sasl_conn_t *conn = NULL;
static char *prompt = NULL;

static int
send_response_sasl_data(myproxy_socket_attrs_t *attrs,
                        myproxy_response_t* server_response,
                        const char *data, int data_len)
{
    char client_buffer[SASL_BUFFER_SIZE], buf[SASL_BUFFER_SIZE];
    int  bufferlen, result;
    unsigned len;

    authorization_data_t*  auth_data;

    result = sasl_encode64(data, data_len, buf, SASL_BUFFER_SIZE, &len);
    assert(len < SASL_BUFFER_SIZE);
    buf[len] = '\0';
    if (result != SASL_OK) {
        verror_put_string(
                          "Encoding data in base64 failed in send_response_sasl_data");
        return -1;
    }

    auth_data = authorization_create_response(
                                              server_response->authorization_data,
                                              AUTHORIZETYPE_SASL,
                                              buf,
                                              len);

    if (auth_data == NULL) {
        verror_put_string(
                          "Cannot create authorization response in send_response_sasl_data");
        return -1;
    }

    if (auth_data->client_data_len + sizeof(int) > sizeof(client_buffer)) {
        verror_put_string("Internal buffer too small send_response_sasl_data");
        return -1;
    }

    (*client_buffer) = AUTHORIZETYPE_SASL;
    bufferlen = auth_data->client_data_len + sizeof(int);

    memcpy(client_buffer + sizeof(int), auth_data->client_data,
           auth_data->client_data_len);

    if (myproxy_send(attrs, client_buffer, bufferlen) < 0)
        return -1;
    return 0;
}


static int
recv_response_sasl_data(myproxy_socket_attrs_t *attrs,
                        myproxy_response_t* server_response,
                        char *data)
{
    char *response_data;
    int result;
    unsigned len;
    authorization_data_t*  auth_data;

    if (myproxy_recv_response(attrs, server_response) < 0)
        return -1;

    auth_data = authorization_create_response(
                                              server_response->authorization_data,
                                              AUTHORIZETYPE_SASL,
                                              NULL,
                                              0);

    response_data = auth_data->server_data;
    result = sasl_decode64(response_data, strlen(response_data),
                           data, SASL_BUFFER_SIZE, &len);
    if (result != SASL_OK) {
        verror_put_string("Decoding data from base64 failed.\n");
        verror_put_errno(errno);
        return -1;
    }
    data[len] = '\0';
    return len;
}

static int
sasl_string_callback(void *context,
                     int id,
                     const char **result,
                     unsigned *len)
{
    const char *value = (const char *)context;

    if (! result)
        return SASL_BADPARAM;

    *result = value;
    if (len)
        *len = value ? strlen(value) : 0;

    return SASL_OK;
}


static int
sasl_secret_callback(sasl_conn_t *conn,
                     void *context __attribute__((unused)),
                     int id,
                     sasl_secret_t **psecret)
{
    char password[MAX_PASS_LEN];
    size_t len;

    if (! conn || ! psecret || id != SASL_CB_PASS)
        return SASL_BADPARAM;

    if (!prompt) prompt = strdup("Password: ");
    if (myproxy_read_passphrase(password, MAX_PASS_LEN, prompt) < 0){
        return SASL_FAIL;
    }

    len = strlen(password);

    *psecret = (sasl_secret_t *) malloc(sizeof(sasl_secret_t) + len);

    if (! *psecret) {
        memset(password, 0, len);
        return SASL_NOMEM;
    }

    (*psecret)->len = len;
    strcpy((char *)(*psecret)->data, password);
    memset(password, 0, len);

    return SASL_OK;
}


static int
sasl_prompt_callback(void *context __attribute__((unused)),
                     int id,
                     const char *challenge,
                     const char *prompt,
                     const char *defresult,
                     const char **result,
                     unsigned *len)
{
    char input[MAX_PASS_LEN];

    if ((id != SASL_CB_ECHOPROMPT && id != SASL_CB_NOECHOPROMPT)
        || !prompt || !result || !len)
        return SASL_BADPARAM;

    if (! defresult)
        defresult = "";

    fputs(prompt, stdout);
    if (challenge)
        printf(" [challenge: %s]", challenge);
    printf(" [%s]: ", defresult);
    fflush(stdout);

    if (id == SASL_CB_NOECHOPROMPT) {
        if (myproxy_read_passphrase(input, MAX_PASS_LEN, "") < 0) {
            return SASL_FAIL;
        }
    } else {
        fgets(input, 1024, stdin);
    }
    if (input[0])
        *result = strdup(input);
    else
        *result = strdup(defresult);

    memset(input, 0L, strlen(input));

    if (! *result)
        return SASL_NOMEM;

    *len = strlen(*result);

    return SASL_OK;
}


int
auth_sasl_negotiate_client(myproxy_socket_attrs_t *attrs,
                           myproxy_request_t      *client_request)
{
    char server_buffer[SASL_BUFFER_SIZE];
    const char *data;
    int  server_len;
    unsigned len;

    myproxy_response_t server_response = {0};

    sasl_callback_t callbacks[] = {
        { SASL_CB_USER, (int(*)(void)) (&sasl_string_callback),
          client_request->username },
        { SASL_CB_AUTHNAME, (int(*)(void)) (&sasl_string_callback),
          client_request->username },
        { SASL_CB_PASS, (int(*)(void)) (&sasl_secret_callback), NULL },
        { SASL_CB_ECHOPROMPT, (int(*)(void)) (&sasl_prompt_callback), NULL },
        { SASL_CB_NOECHOPROMPT, (int(*)(void)) (&sasl_prompt_callback), NULL },
        { SASL_CB_LIST_END, NULL, NULL }
    };

    int result;
    sasl_security_properties_t secprops;
    const char *chosenmech;
    char *service = "myproxy",
        *iplocal = NULL,
        *ipremote = NULL;
    char *fqdn = NULL;

    myproxy_debug("client: begin SASL negotiation...");

    if (getenv("SASL_PATH")) {
        myproxy_debug("$SASL_PATH is %s", getenv("SASL_PATH"));
    } else {
        myproxy_debug("$SASL_PATH isn't set. Using /usr/lib/sasl2.");
    }

    fqdn = GSI_SOCKET_get_peer_hostname(attrs->gsi_socket);

    memset(server_buffer, 0, sizeof(*server_buffer));

    if (prompt) free(prompt);
    prompt = malloc(strlen(client_request->username)+strlen(fqdn)+15);
    if (!prompt) {
        verror_put_string("malloc() failed in auth_sasl_negotiate_client");
        result = SASL_FAIL;
        goto error;
    }
    sprintf(prompt, "%s@%s's password: ", client_request->username, fqdn);

    result = sasl_client_init(callbacks);
    if (result != SASL_OK) {
        verror_put_string("Allocating sasl connection state failed");
        result = SASL_FAIL;
        goto error;
    }

    myproxy_debug("SASL service: %s/%s", service, fqdn);
    result = sasl_client_new(service, fqdn, iplocal, ipremote, NULL, 0, &conn);
    if (result != SASL_OK) {
        verror_put_string("Allocating sasl connection state failed");
        result = SASL_FAIL;
        goto error;
    }

    /* don't need integrity or privacy, since we're over SSL already.
       in fact, let's disable them to avoid the overhead. */
    memset(&secprops, 0L, sizeof(secprops));
    result = sasl_setprop(conn, SASL_SEC_PROPS, &secprops);
    if (result != SASL_OK) {
        verror_put_string("Setting security properties failed");
        result = SASL_FAIL;
        goto error;
    }

    server_len = recv_response_sasl_data(attrs, &server_response,
                                         server_buffer);
    if (server_len < 0) {
        verror_put_string("SASL negotiation failed");
        result = SASL_FAIL;
        goto error;
    }

    myproxy_debug("Server sent SASL mechs %s.", server_buffer);

    result = sasl_client_start(conn,
                               server_buffer,
                               NULL,
                               &data,
                               &len,
                               &chosenmech);

    if (result != SASL_OK && result != SASL_CONTINUE) {
        verror_put_string("SASL error: %s\n", sasl_errdetail(conn));
        result = SASL_FAIL;
        goto error;
    }

    myproxy_debug("Using SASL mechanism %s", chosenmech);
    strcpy(server_buffer, chosenmech);
    if (data) {
        if (SASL_BUFFER_SIZE - strlen(server_buffer) - 1 < len) {
            verror_put_string("Not enough buffer space for SASL");
            result = SASL_FAIL;
            goto error;
        }
        memcpy(server_buffer + strlen(server_buffer) + 1, data, len);
        len += strlen(server_buffer) + 1;
        data = NULL;
    } else {
        len = strlen(server_buffer);
    }

    if (send_response_sasl_data(attrs, &server_response, server_buffer,
                                len) < 0) {
        result = SASL_FAIL;
        goto error;
    }

    authorization_data_free(server_response.authorization_data);
    server_response.authorization_data = NULL;

    while (result == SASL_CONTINUE) {

        server_len = recv_response_sasl_data(attrs, &server_response,
                                             server_buffer);
        if (server_len < 0) {
            result = SASL_FAIL;
            goto error;
        }

        result = sasl_client_step(conn, server_buffer, server_len, NULL,
                                  &data, &len);
        if (result != SASL_OK && result != SASL_CONTINUE) {
            verror_put_string("Performing SASL negotiation failed");
            result = SASL_FAIL;
            goto error;
        }
        if (data && len) {
            if (send_response_sasl_data(attrs, &server_response, data,
                                        len) < 0) {
                result = SASL_FAIL;
                goto error;
            }
        } else /* if (result != SASL_OK) */ {
            if (send_response_sasl_data(attrs, &server_response, "", 0) < 0) {
                result = SASL_FAIL;
                goto error;
            }
        }

        authorization_data_free(server_response.authorization_data);
        server_response.authorization_data = NULL;
    }

    myproxy_debug("SASL negotiation finished.");

 error:
    if (fqdn) free(fqdn);
    if (server_response.authorization_data) {
        authorization_data_free(server_response.authorization_data);
    }
    if (server_response.version) {
        free(server_response.version);
    }
    if (conn) {
        sasl_dispose(&conn);
        conn = NULL;
    }
    sasl_done();

    return result;
}

#endif /* defined(HAVE_LIBSASL2) */
