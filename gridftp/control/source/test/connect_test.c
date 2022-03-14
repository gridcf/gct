/*
 * Copyright 1999-2017 University of Chicago
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

#include "globus_common.h"
#include <stdbool.h>
#include "globus_preload.h"
#include "globus_ftp_control.h"
#include "gssapi.h"

#define TEST_USER "tester"
#define TEST_PASSWORD "password"

typedef struct
{
    globus_ftp_control_server_t         server;
    unsigned short                      port;
    globus_list_t                      *sessions;
    globus_ftp_control_auth_requirements_t
                                        auth_requirements;
    globus_object_t                    *error;
    globus_mutex_t                      mutex;
    globus_cond_t                       cond;
}
auth_test_server_t;

typedef struct
{
    auth_test_server_t                 *server;
    globus_ftp_control_handle_t         handle;
    enum
    {
        SERVER_ACCEPT,
        SERVER_AUTH,
        SERVER_CMDS,
        SERVER_QUIT,
        SERVER_DONE,
    }
    server_state;
}
auth_test_server_session_t;

typedef struct
{
    globus_ftp_control_handle_t         handle;
    bool                                expect_success;
    globus_ftp_control_auth_info_t      auth_info;
    bool                                use_auth;
    enum
    { 
        CMD_CONNECT,
        CMD_AUTHENTICATE,
        CMD_QUIT,
        CMD_DONE,
    }
    command;
    globus_cond_t                       cond;
    globus_mutex_t                      mutex;
    globus_object_t                    *error;
}
auth_test_client_t;

static auth_test_server_t               cleartext_server;
static auth_test_server_t               tls_server;
static auth_test_server_t               gssapi_server;
static
void
globus_l_server_close_callback(
    void *                              callback_arg,
    globus_ftp_control_handle_t        *handle,
    globus_object_t 		       *error,
    globus_ftp_control_response_t      *ftp_response);
static
void
globus_l_server_response_callback(
    void                               *callback_arg,
    globus_ftp_control_handle_t        *handle,
    globus_object_t                    *error);

static
void
globus_l_client_callback(
    void *                              callback_arg,
    globus_ftp_control_handle_t        *handle,
    globus_object_t 		       *error,
    globus_ftp_control_response_t      *ftp_response)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    auth_test_client_t                 *client = callback_arg;

    globus_mutex_lock(&client->mutex);

    if (error != NULL)
    {
        client->error = globus_object_copy(error);
        result = globus_ftp_control_force_close(
            handle,
            globus_l_client_callback,
            client);

        client->command = CMD_QUIT;

        if (result != GLOBUS_SUCCESS)
        {
            client->command = CMD_DONE;
            globus_cond_signal(&client->cond);
        }
    }
    else
    {
        switch (client->command)
        {
            case CMD_CONNECT:
                result = globus_ftp_control_authenticate_ex(
                    handle,
                    &client->auth_info,
                    client->use_auth,
                    globus_l_client_callback,
                    client);
                client->command = CMD_AUTHENTICATE;
                break;

            case CMD_AUTHENTICATE:
                result = globus_ftp_control_quit(
                    handle,
                    globus_l_client_callback,
                    client);
                client->command = CMD_QUIT;
                if (result == GLOBUS_SUCCESS 
                    && ftp_response->code > 399)
                {
                    client->error = globus_error_construct_string(
                            NULL,
                            NULL,
                            "Authentication failed: %.*s\n",
                            (int) ftp_response->response_length,
                            ftp_response->response_buffer);
                }
                break;

            case CMD_QUIT:
                client->command = CMD_DONE;
            case CMD_DONE:
                globus_cond_signal(&client->cond);
                break;
        }
        if (result != GLOBUS_SUCCESS)
        {
            client->error = globus_error_get(result);
            client->command = CMD_DONE;
            globus_cond_signal(&client->cond);
        }
    }
    globus_mutex_unlock(&client->mutex);
}

globus_result_t
authenticate_clear(
    auth_test_server_t                 *server)
{
    auth_test_client_t                  test_client =
    {
        .command = CMD_CONNECT,
    };
    globus_xio_attr_t                   attr = NULL;
    globus_reltime_t                    timeout = {0};
    globus_result_t                     result = GLOBUS_SUCCESS;

    GlobusTimeReltimeSet(timeout, 5, 0);

    globus_mutex_init(&test_client.mutex, NULL);
    globus_cond_init(&test_client.cond, NULL);

    result = globus_ftp_control_handle_init(
        &test_client.handle);
    if (result != GLOBUS_SUCCESS)
    {
        goto control_handle_init_fail;
    }

    result = globus_io_attr_get_xio_attr(
        &test_client.handle.cc_handle.io_attr,
        &attr);
    if (result != GLOBUS_SUCCESS)
    {
        goto xio_attr_get_fail;
    }

    result = globus_xio_attr_cntl(
        attr,
        NULL,
        GLOBUS_XIO_ATTR_SET_TIMEOUT_ALL,
        NULL,
        &timeout,
        NULL);
    if (result != GLOBUS_SUCCESS)
    {
        goto xio_attr_cntl_fail;
    }

    result = globus_ftp_control_auth_info_init(
        &test_client.auth_info,
        NULL,
        false,
        TEST_USER,
        TEST_PASSWORD,
        NULL,
        NULL);
    if (result != GLOBUS_SUCCESS)
    {
        goto auth_info_init_fail;
    }

    result = globus_ftp_control_connect(
        &test_client.handle,
        "localhost",
        server->port,
        globus_l_client_callback,
        &test_client);
    if (result != GLOBUS_SUCCESS)
    {
        goto connect_fail;
    }

    globus_mutex_lock(&test_client.mutex);
    while (test_client.command != CMD_DONE)
    {
        globus_cond_wait(&test_client.cond, &test_client.mutex);
    }
    globus_mutex_unlock(&test_client.mutex);

connect_fail:
auth_info_init_fail:
xio_attr_cntl_fail:
xio_attr_get_fail:
    globus_ftp_control_handle_destroy(&test_client.handle);
    if (result != GLOBUS_SUCCESS)
    {
control_handle_init_fail:
        test_client.error = globus_error_get(result);
    }
    globus_cond_destroy(&test_client.cond);
    globus_mutex_destroy(&test_client.mutex);

    return (test_client.error == NULL) ? GLOBUS_SUCCESS : globus_error_put(test_client.error);
}

globus_result_t
authenticate_gssapi(
    auth_test_server_t                 *server,
    OM_uint32                           req_flags)
{
    auth_test_client_t                  test_client =
    {
        .command = CMD_CONNECT,
    };
    globus_xio_attr_t                   attr = NULL;
    globus_reltime_t                    timeout = {0};
    globus_result_t                     result = GLOBUS_SUCCESS;

    GlobusTimeReltimeSet(timeout, 5, 0);

    globus_mutex_init(&test_client.mutex, NULL);
    globus_cond_init(&test_client.cond, NULL);

    result = globus_ftp_control_handle_init(
        &test_client.handle);
    if (result != GLOBUS_SUCCESS)
    {
        goto control_handle_init_fail;
    }

    result = globus_io_attr_get_xio_attr(
        &test_client.handle.cc_handle.io_attr,
        &attr);
    if (result != GLOBUS_SUCCESS)
    {
        goto xio_attr_get_fail;
    }

    result = globus_xio_attr_cntl(
        attr,
        NULL,
        GLOBUS_XIO_ATTR_SET_TIMEOUT_ALL,
        NULL,
        &timeout,
        NULL);
    if (result != GLOBUS_SUCCESS)
    {
        goto xio_attr_cntl_fail;
    }

    result = globus_ftp_control_auth_info_init(
        &test_client.auth_info,
        GSS_C_NO_CREDENTIAL,
        true,
        TEST_USER,
        TEST_PASSWORD,
        NULL,
        "/CN=test");
    if (result != GLOBUS_SUCCESS)
    {
        goto auth_info_init_fail;
    }
    test_client.auth_info.req_flags = req_flags;
    test_client.use_auth = true;

    result = globus_ftp_control_connect(
        &test_client.handle,
        "localhost",
        server->port,
        globus_l_client_callback,
        &test_client);
    if (result != GLOBUS_SUCCESS)
    {
        goto connect_fail;
    }

    globus_mutex_lock(&test_client.mutex);
    while (test_client.command != CMD_DONE)
    {
        globus_cond_wait(&test_client.cond, &test_client.mutex);
    }
    globus_mutex_unlock(&test_client.mutex);
connect_fail:
auth_info_init_fail:
xio_attr_cntl_fail:
xio_attr_get_fail:
    globus_ftp_control_handle_destroy(&test_client.handle);
    if (result != GLOBUS_SUCCESS)
    {
control_handle_init_fail:
        test_client.error = globus_error_get(result);
    }
    globus_cond_destroy(&test_client.cond);
    globus_mutex_destroy(&test_client.mutex);

    return (test_client.error == NULL) ? GLOBUS_SUCCESS : globus_error_put(test_client.error);
}

globus_result_t
authenticate_tls(
    auth_test_server_t                 *server,
    OM_uint32                           req_flags)
{
    auth_test_client_t                  test_client =
    {
        .command = CMD_CONNECT,
    };
    globus_xio_attr_t                   attr = NULL;
    globus_reltime_t                    timeout = {0};
    globus_result_t                     result = GLOBUS_SUCCESS;

    GlobusTimeReltimeSet(timeout, 5, 0);

    globus_mutex_init(&test_client.mutex, NULL);
    globus_cond_init(&test_client.cond, NULL);

    result = globus_ftp_control_handle_init(
        &test_client.handle);
    if (result != GLOBUS_SUCCESS)
    {
        goto control_handle_init_fail;
    }

    result = globus_io_attr_get_xio_attr(
        &test_client.handle.cc_handle.io_attr,
        &attr);
    if (result != GLOBUS_SUCCESS)
    {
        goto xio_attr_get_fail;
    }

    result = globus_xio_attr_cntl(
        attr,
        NULL,
        GLOBUS_XIO_ATTR_SET_TIMEOUT_ALL,
        NULL,
        &timeout,
        NULL);
    if (result != GLOBUS_SUCCESS)
    {
        goto xio_attr_cntl_fail;
    }

    result = globus_ftp_control_auth_info_init(
        &test_client.auth_info,
        GSS_C_NO_CREDENTIAL,
        false,
        TEST_USER,
        TEST_PASSWORD,
        NULL,
        "/CN=test");
    if (result != GLOBUS_SUCCESS)
    {
        goto auth_info_init_fail;
    }

    test_client.auth_info.req_flags = req_flags;
    test_client.use_auth = false;

    result = globus_ftp_control_use_tls(
        &test_client.handle,
        &test_client.auth_info);
    if (result != GLOBUS_SUCCESS)
    {
        goto use_tls_fail;
    }

    result = globus_ftp_control_connect(
        &test_client.handle,
        "localhost",
        server->port,
        globus_l_client_callback,
        &test_client);
    if (result != GLOBUS_SUCCESS)
    {
        goto connect_fail;
    }

    globus_mutex_lock(&test_client.mutex);
    while (test_client.command != CMD_DONE)
    {
        globus_cond_wait(&test_client.cond, &test_client.mutex);
    }
    globus_mutex_unlock(&test_client.mutex);
connect_fail:
use_tls_fail:
auth_info_init_fail:
xio_attr_cntl_fail:
xio_attr_get_fail:
    globus_ftp_control_handle_destroy(&test_client.handle);
    if (result != GLOBUS_SUCCESS)
    {
control_handle_init_fail:
        test_client.error = globus_error_get(result);
    }
    globus_cond_destroy(&test_client.cond);
    globus_mutex_destroy(&test_client.mutex);

    return (test_client.error == NULL) ? GLOBUS_SUCCESS : globus_error_put(test_client.error);
}


bool
authenticate_clear_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_clear(&cleartext_server);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to cleartext server%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result == GLOBUS_SUCCESS;
}

static
bool
authenticate_clear_to_tls(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_clear(&tls_server);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to tls server (this is expected)%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result != GLOBUS_SUCCESS;
}

static
bool
authenticate_clear_to_gssapi(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_clear(&gssapi_server);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to gssapi server (this is expected)%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result != GLOBUS_SUCCESS;
}

static
bool
authenticate_gssapi_anonymous_to_clear(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_gssapi(&cleartext_server, GSS_C_ANON_FLAG);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to cleartext server (this is expected)%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result != GLOBUS_SUCCESS;
}

static
bool
authenticate_gssapi_anonymous_to_tls(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_gssapi(&tls_server, GSS_C_ANON_FLAG);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to tls server (this is expected)%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result != GLOBUS_SUCCESS;
}

static
bool
authenticate_gssapi_anonymous_to_gssapi(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_gssapi(&gssapi_server, GSS_C_ANON_FLAG);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to gssapi server%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result == GLOBUS_SUCCESS;
}

static
bool
authenticate_tls_anonymous_to_clear(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_tls(&cleartext_server, GSS_C_ANON_FLAG|GSS_C_CONF_FLAG);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to cleartext server (this is expected)%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result != GLOBUS_SUCCESS;
}

static
bool
authenticate_tls_anonymous_to_tls(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_tls(&tls_server, GSS_C_ANON_FLAG|GSS_C_CONF_FLAG);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to tls server%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result == GLOBUS_SUCCESS;
}

static
bool
authenticate_tls_anonymous_to_gssapi(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_tls(&gssapi_server, GSS_C_ANON_FLAG|GSS_C_CONF_FLAG);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to gssapi server (this is expected)%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result != GLOBUS_SUCCESS;
}

static
bool
authenticate_gssapi_to_clear(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_gssapi(&cleartext_server, GSS_C_MUTUAL_FLAG);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to cleartext server (this is expected)%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result != GLOBUS_SUCCESS;
}

static
bool
authenticate_gssapi_to_tls(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_gssapi(&tls_server, GSS_C_MUTUAL_FLAG);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to tls server (this is expected)%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result != GLOBUS_SUCCESS;
}

static
bool
authenticate_gssapi_to_gssapi(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_gssapi(&gssapi_server, GSS_C_MUTUAL_FLAG);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to gssapi server%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result == GLOBUS_SUCCESS;
}

static
bool
authenticate_tls_to_clear(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_tls(&cleartext_server, GSS_C_MUTUAL_FLAG|GSS_C_CONF_FLAG);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to cleartext server (this is expected)%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result != GLOBUS_SUCCESS;
}

static
bool
authenticate_tls_to_tls(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_tls(&tls_server, GSS_C_MUTUAL_FLAG|GSS_C_CONF_FLAG);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to tls server%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result == GLOBUS_SUCCESS;
}

static
bool
authenticate_tls_to_gssapi(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = authenticate_tls(&gssapi_server, GSS_C_MUTUAL_FLAG|GSS_C_CONF_FLAG);

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error authenticating to gssapi server (this is expected)%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");
        free(msg);
    }
    return result != GLOBUS_SUCCESS;
}


static
void
globus_l_command_callback(
    void                               *callback_arg,
    globus_ftp_control_handle_t        *handle,
    globus_object_t                    *error,
    union globus_ftp_control_command_u *command)
{
    auth_test_server_session_t         *session = callback_arg;
    if (command != NULL)
    {
        switch (command->code)
        {
            case GLOBUS_FTP_CONTROL_COMMAND_QUIT:
                session->server_state = SERVER_QUIT;
                globus_ftp_control_send_response(
                    handle,
                    "221 Service closing control connection.",
                    globus_l_server_response_callback,
                    callback_arg);
                break;

            default:
                globus_ftp_control_send_response(
                    handle,
                    "502 Command not implemented.",
                    globus_l_server_response_callback,
                    callback_arg);
                break;
        }
    }
}

static
void
globus_l_server_auth_callback(
    void                               *callback_arg,
    globus_ftp_control_handle_t        *handle,
    globus_object_t                    *error,
    globus_ftp_control_auth_info_t     *auth_result)
{
    auth_test_server_session_t         *session = callback_arg;
    globus_result_t                     res;
    char *                              username;
    globus_bool_t                       accepted;

    if (error)
    {
        session->server_state = SERVER_DONE;
        res = globus_ftp_control_force_close(
            handle,
            globus_l_server_close_callback,
            callback_arg);
        return;
    }

    accepted = GLOBUS_FALSE;
    if(strcmp(auth_result->user, TEST_USER) == 0
        && strcmp(auth_result->password, TEST_PASSWORD) == 0)
    {
        username = auth_result->user;
        accepted = true;
    }

    if(accepted)
    {
        res = globus_ftp_control_send_response(
                  handle,
                  "230 User %s logged on.\r\n",
                  globus_l_server_response_callback,
                  callback_arg,
                  username);
    }
    else
    {
        res = globus_ftp_control_send_response(
                  handle,
                  "530 No local mapping for Globus ID.\r\n",
                  globus_l_server_response_callback,
                  callback_arg);
    }

    if (res != GLOBUS_SUCCESS)
    {
        abort();
    }
}

static
void
globus_l_server_close_callback(
    void *                              callback_arg,
    globus_ftp_control_handle_t        *handle,
    globus_object_t 		       *error,
    globus_ftp_control_response_t      *ftp_response)
{
    globus_l_server_response_callback(callback_arg, handle, error);
}

static
void
globus_l_server_response_callback(
    void                               *callback_arg,
    globus_ftp_control_handle_t        *handle,
    globus_object_t                    *error)
{
    auth_test_server_session_t         *session = callback_arg;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_ftp_control_auth_requirements_t     
                                        auth_req;

    if (error)
    {
        return;
    }

    switch (session->server_state)
    {

        case SERVER_ACCEPT:
            session->server_state = SERVER_AUTH;
            auth_req = session->server->auth_requirements;

            res = globus_ftp_control_server_authenticate(
                handle,
                auth_req,
                globus_l_server_auth_callback,
                callback_arg);
            break;
        case SERVER_AUTH:
            session->server_state = SERVER_CMDS;
            res = globus_ftp_control_read_commands(
                handle,
                globus_l_command_callback,
                callback_arg);
            break;
        case SERVER_CMDS:
            break;
        case SERVER_QUIT:
            session->server_state = SERVER_DONE;
            res = globus_ftp_control_force_close(
                handle,
                globus_l_server_close_callback,
                callback_arg);
            break;
        case SERVER_DONE:
            {
                globus_list_t           *entry = NULL;
                globus_mutex_lock(&session->server->mutex);

                entry = globus_list_search(session->server->sessions, session);
                globus_list_remove(&session->server->sessions, entry);
                globus_ftp_control_handle_destroy(handle);
                globus_mutex_unlock(&session->server->mutex);
                free(session);
            }
    }
    if (res != GLOBUS_SUCCESS)
    {
        abort();
    }
}

static
void
globus_l_server_accept_callback(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *                           error)
{
    globus_result_t                     res;
    auth_test_server_session_t         *session = callback_arg;

    if (error != NULL)
    {
        session->server_state = SERVER_DONE;
        res = globus_ftp_control_force_close(
            handle,
            globus_l_server_close_callback,
            callback_arg);
    }

    res = globus_ftp_control_send_response(
        handle,
        "220 Globus GridFTP Test Server\r\n",
        globus_l_server_response_callback,
        callback_arg);
}

static
void 
globus_l_server_listen_callback(
    void *                              callback_arg,
    globus_ftp_control_server_t *       server_handle,
    globus_object_t *                   error)
{
    auth_test_server_t                 *server = callback_arg;
    auth_test_server_session_t         *session = NULL;
    globus_result_t                     res = GLOBUS_SUCCESS;

    if (error)
    {
        return;
    }

    session = malloc(sizeof(auth_test_server_session_t));
    if (session == NULL)
    {
        abort();
    }
    *session = (auth_test_server_session_t)
    {
        .server = server,
        .server_state = SERVER_ACCEPT,
    };

    res = globus_ftp_control_handle_init(&session->handle);
    if (res != GLOBUS_SUCCESS)
    {
        abort();
    }
    globus_mutex_lock(&server->mutex);
    globus_list_insert(&server->sessions, session);
    globus_mutex_unlock(&server->mutex);

    res = globus_ftp_control_server_accept(
        server_handle,
        &session->handle,
        globus_l_server_accept_callback,
        session);
    if (res != GLOBUS_SUCCESS)
    {
        abort();
    }
}

static
globus_result_t
initialize_servers(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_io_attr_t                    tls_server_attr = NULL;

    result = globus_ftp_control_server_handle_init(&cleartext_server.server);

    if (result != GLOBUS_SUCCESS)
    {
        goto failed_init_cleartext;
    }

    result = globus_ftp_control_server_listen(
        &cleartext_server.server,
        &cleartext_server.port,
        globus_l_server_listen_callback,
        &cleartext_server);

    if (result != GLOBUS_SUCCESS)
    {
        goto failed_listen_cleartext;
    }
    globus_mutex_init(&cleartext_server.mutex, NULL);
    cleartext_server.auth_requirements =
        GLOBUS_FTP_CONTROL_AUTH_REQ_USER
        | GLOBUS_FTP_CONTROL_AUTH_REQ_PASS;
    
    result = globus_ftp_control_server_handle_init(
        &tls_server.server);
    if (result != GLOBUS_SUCCESS)
    {
        goto failed_init_tls;
    }
    result = globus_io_tcpattr_init(
        &tls_server_attr);
    if (result != GLOBUS_SUCCESS)
    {
        goto failed_init_tls_attr;
    }
    result = globus_io_attr_set_secure_authentication_mode(
        &tls_server_attr,
        GLOBUS_IO_SECURE_AUTHENTICATION_MODE_GSSAPI,
        GSS_C_NO_CREDENTIAL);
    if (result != GLOBUS_SUCCESS)
    {
        goto failed_set_tls_attr;
    }
    result = globus_io_attr_set_secure_channel_mode(
        &tls_server_attr,
        GLOBUS_IO_SECURE_CHANNEL_MODE_SSL_WRAP);
    if (result != GLOBUS_SUCCESS)
    {
        goto failed_set_tls_attr;
    }
    result = globus_ftp_control_server_listen_ex(
        &tls_server.server,
        &tls_server_attr,
        &tls_server.port,
        globus_l_server_listen_callback,
        &tls_server);
    if (result != GLOBUS_SUCCESS)
    {
        goto failed_listen_tls;
    }
    globus_mutex_init(&tls_server.mutex, NULL);
    tls_server.auth_requirements =
        GLOBUS_FTP_CONTROL_AUTH_REQ_USER
        | GLOBUS_FTP_CONTROL_AUTH_REQ_PASS
        | GLOBUS_FTP_CONTROL_AUTH_REQ_TLS;

    result = globus_ftp_control_server_handle_init(
        &gssapi_server.server);
    if (result != GLOBUS_SUCCESS)
    {
        goto failed_init_gssapi;
    }

    result = globus_ftp_control_server_listen(
        &gssapi_server.server,
        &gssapi_server.port,
        globus_l_server_listen_callback,
        &gssapi_server);
    globus_mutex_init(&gssapi_server.mutex, NULL);
    globus_cond_init(&gssapi_server.cond, NULL);
    gssapi_server.auth_requirements =
        GLOBUS_FTP_CONTROL_AUTH_REQ_USER
        | GLOBUS_FTP_CONTROL_AUTH_REQ_PASS
        | GLOBUS_FTP_CONTROL_AUTH_REQ_GSSAPI;

    if (result != GLOBUS_SUCCESS)
    {
        globus_ftp_control_server_handle_destroy(&gssapi_server.server);
failed_init_gssapi:
failed_listen_tls:
failed_init_tls_attr:
failed_set_tls_attr:
        globus_ftp_control_server_handle_destroy(&tls_server.server);
failed_init_tls:
failed_listen_cleartext:
        globus_ftp_control_server_handle_destroy(&cleartext_server.server);
    }

    if (tls_server_attr != NULL)
    {
        globus_io_tcpattr_destroy(&tls_server_attr);
    }
failed_init_cleartext:
    return result;
}
/* initialize_servers() */

static
void
globus_l_server_stop_callback(
    void *                              callback_arg,
    globus_ftp_control_server_t        *server_handle,
    globus_object_t                    *error)
{
    auth_test_server_t                 *server = callback_arg;

    globus_mutex_lock(&server->mutex);
    server->port = 0;
    globus_cond_signal(&server->cond);
    globus_mutex_unlock(&server->mutex);
}

#define TEST_CASE(x) { x, #x }
typedef struct
{
    bool                               (*test)(void);
    const char                          *name;
}
test_case_t;
int main(int argc, char *argv[])
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    test_case_t                         tests[] =
    {
        TEST_CASE(authenticate_clear_test),
        TEST_CASE(authenticate_clear_to_tls),
        TEST_CASE(authenticate_clear_to_gssapi),
        TEST_CASE(authenticate_gssapi_anonymous_to_clear),
        TEST_CASE(authenticate_gssapi_anonymous_to_tls),
        TEST_CASE(authenticate_gssapi_anonymous_to_gssapi),
        TEST_CASE(authenticate_tls_anonymous_to_clear),
        TEST_CASE(authenticate_tls_anonymous_to_tls),
        TEST_CASE(authenticate_tls_anonymous_to_gssapi),
        TEST_CASE(authenticate_gssapi_to_clear),
        TEST_CASE(authenticate_gssapi_to_tls),
        TEST_CASE(authenticate_gssapi_to_gssapi),
        TEST_CASE(authenticate_tls_to_clear),
        TEST_CASE(authenticate_tls_to_tls),
        TEST_CASE(authenticate_tls_to_gssapi),
    };
    size_t                              num_tests
        = sizeof(tests) / sizeof(tests[0]);
    size_t                              failed = 0;

    printf("1..%zu\n", num_tests);

    LTDL_SET_PRELOADED_SYMBOLS();
    globus_module_activate(GLOBUS_FTP_CONTROL_MODULE);

    result = initialize_servers();

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = NULL;

        msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(
            stderr,
            "Error initializing servers%s%s\n",
            msg != NULL ? ": " : "",
            msg != NULL ? msg : "");

        failed = 99;
        free(msg);

        goto exit;
    }

    for (size_t i = 0; i < num_tests; i++)
    {
        bool ok = false;

        ok = tests[i].test();

        printf("%s %zu - %s\n", ok ? "ok" : "not ok", i + 1, tests[i].name);
        failed += !ok;
    }

    globus_mutex_lock(&gssapi_server.mutex);
    globus_ftp_control_server_stop(
        &gssapi_server.server,
        globus_l_server_stop_callback,
        &gssapi_server);
    while (gssapi_server.port != 0)
    {
        globus_cond_wait(&gssapi_server.cond, &gssapi_server.mutex);
    }
    globus_mutex_unlock(&gssapi_server.mutex);

    globus_mutex_lock(&tls_server.mutex);
    globus_ftp_control_server_stop(
        &tls_server.server,
        globus_l_server_stop_callback,
        &tls_server);
    while (tls_server.port != 0)
    {
        globus_cond_wait(&tls_server.cond, &tls_server.mutex);
    }
    globus_mutex_unlock(&tls_server.mutex);

    globus_mutex_lock(&cleartext_server.mutex);
    globus_ftp_control_server_stop(
        &cleartext_server.server,
        globus_l_server_stop_callback,
        &cleartext_server);
    while (cleartext_server.port != 0)
    {
        globus_cond_wait(&cleartext_server.cond, &cleartext_server.mutex);
    }
    globus_mutex_unlock(&cleartext_server.mutex);
exit:
    globus_module_deactivate(GLOBUS_FTP_CONTROL_MODULE);
    return failed;
}
