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
#include "globus_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "globus_xio.h"
#include "globus_xio_http.h"
#include "globus_xio_tcp_driver.h"

#include "http_test_common.h"

#ifdef _WIN32
#include <getopt.h>
#endif

static globus_mutex_t                   lock;
static globus_cond_t                    cond;

typedef struct
{
    globus_bool_t                       cause_timeout;
    globus_bool_t                       expect_timeout;
    globus_xio_operation_type_t         timeout_state;
    int                                 timeout;

    globus_xio_attr_t                   attr;
    char *                              contact;

    globus_xio_operation_type_t         state;
    globus_byte_t                       buffer[5];

    globus_xio_server_t                 server;
    globus_xio_handle_t                 handle;
    globus_result_t                     result;
}
globus_l_timeout_info_t;

globus_xio_driver_t                     globus_l_tcp_driver;
globus_xio_driver_t                     globus_l_http_driver;
globus_xio_stack_t                      globus_l_http_stack;

globus_xio_attr_t                       timeout_attr;

static
void
usage(char * pgm)
{
    printf("%s ARGS\n"
 "  -t server|client\n"
 "     Configure the server or client to cause a timeout. If server is\n"
 "     selected, then it will delay its part of the operation longer than the\n"
 "     timeout value, causing the client to time out (and vice versa).\n"
 "  -T timeout-in-ms\n"
 "     Set the timeout value in ms\n"
 "  -a\n"
 "     Expect / cause the accept operation to delay longer than the timeout.\n"
 "  -r\n"
 "     Expect / cause a read operation to delay longer than the timeout.\n",
 pgm);
}

static
void
state_machine(
    void *                              user_arg);

static
globus_bool_t
globus_l_timeout_callback(
    globus_xio_handle_t                 handle,
    globus_xio_operation_type_t         type,
    void *                              user_arg);

/*
 * Timeout test:
 *  -t server|client
 *     Configure the server or client to cause a timeout. If server is
 *     selected, then it will delay its part of the operation longer than the
 *     timeout value, causing the client to time out (and vice versa).
 *  -T timeout-in-ms
 *     Set the timeout value in ms
 *  -a
 *     Expect / cause the accept operation to delay longer than the timeout.
 *  -r
 *     Expect / cause a read operation to delay longer than the timeout.
 *
 */
int
main(
    int                                 argc,
    char *                              argv[])
{
    int                                 rc;
    char *                              contact = NULL;
    globus_l_timeout_info_t             client_timeout_info;
    globus_l_timeout_info_t             server_timeout_info;
    globus_reltime_t                    timeout;

    client_timeout_info.cause_timeout = GLOBUS_TRUE;
    client_timeout_info.expect_timeout = GLOBUS_FALSE;
    server_timeout_info.cause_timeout = GLOBUS_FALSE;
    server_timeout_info.expect_timeout = GLOBUS_TRUE;

    client_timeout_info.timeout_state =
        GLOBUS_XIO_OPERATION_TYPE_ACCEPT;
    server_timeout_info.timeout_state =
        GLOBUS_XIO_OPERATION_TYPE_ACCEPT;
    client_timeout_info.timeout = 1000;
    server_timeout_info.timeout = 1000;

    while ((rc = getopt(argc, argv, "t:T:arh")) != EOF)
    {
        switch (rc)
        {
            case 'h':
                usage(argv[0]);
                exit(0);
            case 't':
                if (strcmp(optarg, "client") == 0)
                {
                    client_timeout_info.cause_timeout = GLOBUS_TRUE;
                    client_timeout_info.expect_timeout = GLOBUS_FALSE;
                    server_timeout_info.cause_timeout = GLOBUS_FALSE;
                    server_timeout_info.expect_timeout = GLOBUS_TRUE;
                }
                else if (strcmp(optarg, "server") == 0)
                {
                    client_timeout_info.cause_timeout = GLOBUS_FALSE;
                    client_timeout_info.expect_timeout = GLOBUS_TRUE;
                    server_timeout_info.cause_timeout = GLOBUS_TRUE;
                    server_timeout_info.expect_timeout = GLOBUS_FALSE;
                }
                else
                {
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 'T':
                client_timeout_info.timeout = atoi(optarg);
                server_timeout_info.timeout = atoi(optarg);
                break;
            case 'a':
                client_timeout_info.timeout_state =
                    GLOBUS_XIO_OPERATION_TYPE_ACCEPT;
                server_timeout_info.timeout_state =
                    GLOBUS_XIO_OPERATION_TYPE_ACCEPT;
                break;
            case 'r':
                client_timeout_info.timeout_state =
                    GLOBUS_XIO_OPERATION_TYPE_READ;
                server_timeout_info.timeout_state =
                    GLOBUS_XIO_OPERATION_TYPE_READ;
                break;
            default:
                usage(argv[0]);
                exit(1);
        }
    }

    rc = http_test_initialize(
            &globus_l_tcp_driver,
            &globus_l_http_driver,
            &globus_l_http_stack);

    if (rc != GLOBUS_SUCCESS)
    {
        exit(2);
    }

    globus_mutex_init(&lock, NULL);
    globus_cond_init(&cond, NULL);

    GlobusTimeReltimeSet(timeout,
            0,
            (server_timeout_info.timeout * 1000));

    globus_xio_attr_init(&client_timeout_info.attr);
    globus_xio_attr_init(&server_timeout_info.attr);
    switch (server_timeout_info.timeout_state)
    {
        case GLOBUS_XIO_OPERATION_TYPE_ACCEPT:
            if (client_timeout_info.cause_timeout)
            {
                globus_xio_attr_cntl(
                        server_timeout_info.attr,
                        NULL,
                        GLOBUS_XIO_ATTR_SET_TIMEOUT_ACCEPT,
                        globus_l_timeout_callback,
                        &timeout,
                        NULL);
            }
            else
            {
                fprintf(stderr,
                        "Unable to handle server-caused accept timeout\n");
                exit(6);
            }
            break;

        case GLOBUS_XIO_OPERATION_TYPE_READ:
            if (client_timeout_info.cause_timeout)
            {
                globus_xio_attr_cntl(
                        server_timeout_info.attr,
                        NULL,
                        GLOBUS_XIO_ATTR_SET_TIMEOUT_READ,
                        globus_l_timeout_callback,
                        &timeout,
                        NULL);
            }
            else
            {
                globus_xio_attr_cntl(
                        client_timeout_info.attr,
                        NULL,
                        GLOBUS_XIO_ATTR_SET_TIMEOUT_READ,
                        globus_l_timeout_callback,
                        &timeout,
                        NULL);
            }
            break;

        default:
            fprintf(stderr, "Error: invalid timeout state\n");
            exit(3);
    }

    /* Set up client attributes */
    globus_xio_attr_cntl(
            client_timeout_info.attr,
            globus_l_http_driver,
            GLOBUS_XIO_HTTP_ATTR_SET_REQUEST_METHOD,
            "POST");

    /* Modulate timeout state to causer's related state for delaying */
    if (client_timeout_info.cause_timeout)
    {
        switch (client_timeout_info.timeout_state)
        {
            case GLOBUS_XIO_OPERATION_TYPE_ACCEPT:
                client_timeout_info.timeout_state =
                    GLOBUS_XIO_OPERATION_TYPE_OPEN;
                break;

            case GLOBUS_XIO_OPERATION_TYPE_READ:
                client_timeout_info.timeout_state =
                    GLOBUS_XIO_OPERATION_TYPE_WRITE;
                break;

            case GLOBUS_XIO_OPERATION_TYPE_WRITE:
            case GLOBUS_XIO_OPERATION_TYPE_OPEN:
            case GLOBUS_XIO_OPERATION_TYPE_CLOSE:
            case GLOBUS_XIO_OPERATION_TYPE_FINISHED:
            case GLOBUS_XIO_OPERATION_TYPE_NONE:
            case GLOBUS_XIO_OPERATION_TYPE_DRIVER:
            case GLOBUS_XIO_OPERATION_TYPE_DD:
            case GLOBUS_XIO_OPERATION_TYPE_SERVER_INIT:
                fprintf(stderr,
                        "Error: unexpected state: %d\n",
                        client_timeout_info.state);
                exit(4);
        }
    }
    else
    {
        globus_assert(server_timeout_info.cause_timeout);
        switch (server_timeout_info.timeout_state)
        {
            case GLOBUS_XIO_OPERATION_TYPE_ACCEPT:
                fprintf(stderr,
                        "Invalid option (server-caused accept timeout)\n");
                exit(5);

            case GLOBUS_XIO_OPERATION_TYPE_READ:
                server_timeout_info.timeout_state =
                    GLOBUS_XIO_OPERATION_TYPE_WRITE;
                break;

            case GLOBUS_XIO_OPERATION_TYPE_WRITE:
            case GLOBUS_XIO_OPERATION_TYPE_OPEN:
            case GLOBUS_XIO_OPERATION_TYPE_CLOSE:
            case GLOBUS_XIO_OPERATION_TYPE_FINISHED:
            case GLOBUS_XIO_OPERATION_TYPE_NONE:
            case GLOBUS_XIO_OPERATION_TYPE_DRIVER:
            case GLOBUS_XIO_OPERATION_TYPE_DD:
            case GLOBUS_XIO_OPERATION_TYPE_SERVER_INIT:
                fprintf(stderr,
                        "Error: unexpected state: %d\n",
                        server_timeout_info.state);
                exit(4);
        }
    }

    /* create server */
    server_timeout_info.state = GLOBUS_XIO_OPERATION_TYPE_ACCEPT;
    server_timeout_info.handle = NULL;
    server_timeout_info.result = GLOBUS_SUCCESS;
    server_timeout_info.contact = NULL;
    globus_xio_server_create(
            &server_timeout_info.server,
            server_timeout_info.attr,
            globus_l_http_stack);

    globus_xio_server_get_contact_string(
            server_timeout_info.server,
            &contact);

    client_timeout_info.contact = globus_common_create_string("http://%s/%s",
            contact,
            "ok");

    /* create client handle */
    client_timeout_info.state = GLOBUS_XIO_OPERATION_TYPE_OPEN;
    client_timeout_info.handle = NULL;
    client_timeout_info.result = GLOBUS_SUCCESS;
    client_timeout_info.server = NULL;

    globus_xio_handle_create(
            &client_timeout_info.handle,
            globus_l_http_stack);

    /* oneshot to start server state machine */
    globus_callback_register_oneshot(
            NULL,
            &globus_i_reltime_zero,
            state_machine,
            &server_timeout_info);

    /* oneshot to start client state machine */
    globus_callback_register_oneshot(
            NULL,
            &globus_i_reltime_zero,
            state_machine,
            &client_timeout_info);

    /* wait for both state machines to terminate */
    globus_mutex_lock(&lock);
    while (client_timeout_info.state != GLOBUS_XIO_OPERATION_TYPE_NONE &&
           server_timeout_info.state != GLOBUS_XIO_OPERATION_TYPE_NONE)
    {
        globus_cond_wait(&cond, &lock);
    }
    globus_mutex_unlock(&lock);
    globus_mutex_destroy(&lock);

    globus_module_deactivate_all();

    return (client_timeout_info.result ||
            server_timeout_info.result ||
            client_timeout_info.expect_timeout ||
            server_timeout_info.expect_timeout);
}
/* main() */


static
globus_bool_t
result_is_timeout(
    globus_result_t                     result)
{
   return (result != GLOBUS_SUCCESS &&
            globus_error_match(
                globus_error_peek(result),
                GLOBUS_XIO_MODULE,
                GLOBUS_XIO_ERROR_CANCELED));
}
/* result_is_timeout() */

static
void
accept_callback(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 server_handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_timeout_info_t *           info = user_arg;

    globus_mutex_lock(&lock);
    if (result != GLOBUS_SUCCESS)
    {
        if (info->timeout_state == info->state &&
            info->expect_timeout &&
            result_is_timeout(result))
        {
            /* hit expected result */
            info->expect_timeout = GLOBUS_FALSE;

            result = GLOBUS_SUCCESS;
        }
        else
        {
            fprintf(stderr, "Unexpected accept failure\n");
        }

        info->state = GLOBUS_XIO_OPERATION_TYPE_FINISHED;
    }
    else
    {
        info->state = GLOBUS_XIO_OPERATION_TYPE_OPEN;
    }
    info->handle = server_handle;
    info->result = result;
    globus_mutex_unlock(&lock);
    state_machine(info);
}
/* accept_callback() */

static
void
open_close_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_timeout_info_t *           info = user_arg;

    globus_mutex_lock(&lock);
    if (result != GLOBUS_SUCCESS)
    {
        if (info->state == info->timeout_state &&
            info->expect_timeout &&
            result_is_timeout(result))
        {
            /* hit expected result */
            info->expect_timeout = GLOBUS_FALSE;

            result = GLOBUS_SUCCESS;
        }

        info->state = GLOBUS_XIO_OPERATION_TYPE_FINISHED;
    }
    else
    {
        if (info->state == GLOBUS_XIO_OPERATION_TYPE_OPEN &&
            info->server != NULL)
        {
            info->state = GLOBUS_XIO_OPERATION_TYPE_READ;
        }
        else if (info->state == GLOBUS_XIO_OPERATION_TYPE_OPEN)
        {
            info->state = GLOBUS_XIO_OPERATION_TYPE_WRITE;
        }
        else
        {
            info->state = GLOBUS_XIO_OPERATION_TYPE_FINISHED;
        }
    }
    info->result = result;
    globus_mutex_unlock(&lock);
    state_machine(info);
}
/* open_close_callback() */

static
void
data_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_l_timeout_info_t *           info = user_arg;

    globus_mutex_lock(&lock);
    if (result != GLOBUS_SUCCESS)
    {
        if (info->state == info->timeout_state &&
            info->expect_timeout &&
            result_is_timeout(result))
        {
            /* hit expected result */
            info->expect_timeout = GLOBUS_FALSE;

            result = GLOBUS_SUCCESS;
        }

        info->state = GLOBUS_XIO_OPERATION_TYPE_CLOSE;
    }
    else
    {
        globus_assert(info->state == GLOBUS_XIO_OPERATION_TYPE_READ ||
                      info->state == GLOBUS_XIO_OPERATION_TYPE_WRITE);

        if (info->state == GLOBUS_XIO_OPERATION_TYPE_READ &&
            info->server != NULL)
        {
            info->state = GLOBUS_XIO_OPERATION_TYPE_WRITE;
        }
        else if (info->state == GLOBUS_XIO_OPERATION_TYPE_WRITE &&
                 info->server != NULL)
        {
            info->state = GLOBUS_XIO_OPERATION_TYPE_CLOSE;
        }
        else if (info->state == GLOBUS_XIO_OPERATION_TYPE_WRITE)
        {
            info->state = GLOBUS_XIO_OPERATION_TYPE_READ;
        }
        else
        {
            info->state = GLOBUS_XIO_OPERATION_TYPE_WRITE;
        }
    }
    info->result = result;
    globus_mutex_unlock(&lock);
    state_machine(info);
}
/* data_callback() */

static
void
state_machine(
    void *                              user_arg)
{
    globus_reltime_t                    delay;
    globus_l_timeout_info_t *           info = user_arg;

    /* If the timeout is to be caused by this side of the transfer, then check
     * current state, and if it's the timeout state, then we'll clear the
     * cause_timeout flag and then reregister this oneshot with the delay of
     * (timeout * 5) and continue the state machine.
     */

    globus_mutex_lock(&lock);
    if (info->cause_timeout && info->timeout_state == info->state)
    {
        /* register oneshot at (timeout * 5) */
        GlobusTimeReltimeSet(delay,
                0,
                (info->timeout * 5 * 1000));

        info->cause_timeout = GLOBUS_FALSE;
        info->state = GLOBUS_XIO_OPERATION_TYPE_CLOSE;

        globus_callback_register_oneshot(
                NULL,
                &delay,
                state_machine,
                info);
        globus_mutex_unlock(&lock);
        return;
    }

    /* process current state */
    switch (info->state)
    {
        case GLOBUS_XIO_OPERATION_TYPE_ACCEPT:
            globus_assert(info->server);
            globus_xio_server_register_accept(
                    info->server,
                    accept_callback,
                    info);
            break;

        case GLOBUS_XIO_OPERATION_TYPE_OPEN:
            globus_xio_register_open(
                    info->handle,
                    info->contact,
                    info->attr,
                    open_close_callback,
                    info);
            break;

        case GLOBUS_XIO_OPERATION_TYPE_READ:
            globus_xio_register_read(
                    info->handle,
                    info->buffer,
                    sizeof(info->buffer),
                    1,
                    NULL,
                    data_callback,
                    info);
            break;

        case GLOBUS_XIO_OPERATION_TYPE_WRITE:
            strcpy((char *) info->buffer, "ok\n");

            globus_xio_register_write(
                    info->handle,
                    info->buffer,
                    strlen((char *) info->buffer),
                    strlen((char *) info->buffer),
                    NULL,
                    data_callback,
                    info);
            break;

        case GLOBUS_XIO_OPERATION_TYPE_CLOSE:
            globus_xio_register_close(
                    info->handle,
                    NULL,
                    open_close_callback,
                    info);
            break;

        case GLOBUS_XIO_OPERATION_TYPE_FINISHED:
            globus_cond_signal(&cond);
            info->state = GLOBUS_XIO_OPERATION_TYPE_NONE;
            break;

        case GLOBUS_XIO_OPERATION_TYPE_NONE:
        case GLOBUS_XIO_OPERATION_TYPE_DRIVER:
        case GLOBUS_XIO_OPERATION_TYPE_DD:
        case GLOBUS_XIO_OPERATION_TYPE_SERVER_INIT:
            fprintf(stderr,
                    "Error: unexpected state: %d\n",
                    info->state);
            info->result = GLOBUS_FAILURE;
            info->state = GLOBUS_XIO_OPERATION_TYPE_NONE;
            globus_cond_signal(&cond);
            break;
    }
    globus_mutex_unlock(&lock);
}
/* state_machine() */

static
globus_bool_t
globus_l_timeout_callback(
    globus_xio_handle_t                 handle,
    globus_xio_operation_type_t         type,
    void *                              user_arg)
{
    return GLOBUS_TRUE;
}
/* globus_l_timeout_callback() */
