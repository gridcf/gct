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

/**
 * @file http_throughput_test.c
 * @brief HTTP Throughput Test
 *
 * Test that clients can send and receive HTTP message bodies.
 *
 * Test parameters are
 * - -v "HTTP/1.0"|"HTTP/1.1"<br>
 *   Set HTTP version to use
 * - -t "chunked"|"identity"<br>
 *   Set transfer encoding (for HTTP/1.1 transfers only)
 * - -b buffer-size<br>
 *   Set the size (in bytes) to be read/written at a time
 * - -i iterations
 *   Set the number of transfer iterations to do
 *
 * The test client will send the POST request for the /post-test URI.
 *
 * The test server will 
 * - verify the receipt of the /post-test URI
 * - generate a response
 *
 */

#include "globus_common.h"
#include "http_performance_common.h"
#include "globus_xio_tcp_driver.h"
#include "globus_utp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <getopt.h>
#endif

#define HTTP_1_0 "HTTP/1.0"
#define HTTP_1_1 "HTTP/1.1"
#define CHUNKED  "chunked"
#define IDENTITY "identity"


static
void
globus_l_xio_test_server_request_callback(
    void *                              user_arg,
    globus_result_t                     result,
    const char *                        method,
    const char *                        uri,
    globus_xio_http_version_t           http_version,
    globus_hashtable_t                  headers);

void
globus_l_xio_test_server_handle_request(
    void *                              ignored);

static void usage(const char * cmd)
{
    printf("Usage: %s [options] [-c|-s] \n"
            "Options:\n"
            "    -v \"HTTP/1.0\" | \"HTTP/1.1\"    HTTP version\n"
            "    -t \"chunked\" | \"identity\"     Client Transfer-Encoding\n"
            "    -i iterations                     Set number of iterations\n"
            "    -b buffer-size                    Size of reads and writes\n",
            cmd);
}

int
client_test(
    http_test_info_t *			info,
    int					timer)	
{
    int                                 rc = 0;
    globus_result_t                     result;
    int                                 header_cnt = 0;
    char                                content_length_buffer[64];
    globus_xio_http_header_t            headers[2];
    globus_xio_handle_t                 handle;
    int                                 i;
    size_t                              nbytes;
    globus_xio_data_descriptor_t        descriptor;
    int                                 status_code;
    char *                              reason_phrase;


    globus_utp_start_timer(timer);
    if (info->transfer_encoding != NULL)
    {
        headers[header_cnt].name = "Transfer-Encoding";
        headers[header_cnt].value = info->transfer_encoding;

        header_cnt++;

    }

    if ((info->version == GLOBUS_XIO_HTTP_VERSION_1_0) ||
            ((info->transfer_encoding != NULL)
                && strcmp(info->transfer_encoding, IDENTITY) == 0))
    {
        sprintf(content_length_buffer, "%lu", (unsigned long) info->size);

        headers[header_cnt].name = "Content-Length";
        headers[header_cnt].value = &content_length_buffer[0];

        header_cnt++;
    }

    handle = NULL;

    result = http_test_client_request(
	    &handle,
	    info->tcp_driver,
	    info->http_driver,
	    info->stack,
	    info->contact,
	    "%2fpost-test",
	    "POST",
	    info->version,
	    headers,
	    header_cnt);

    if (result != GLOBUS_SUCCESS)
    {
	fprintf(stderr, "Error making request: %s\n",
		globus_object_printable_to_string(
		globus_error_get(result)));
	rc = 50;
	goto error_exit;
    }

    for (i = 0; i < info->iterations; i++)
    {
	result = globus_xio_write(
		handle,
		info->buffer,
		info->size,
		info->size,
		&nbytes,
		NULL);

	if (result == GLOBUS_SUCCESS)
	{
	    if (nbytes != info->size)
	    {
		fprintf(stderr, "Didn't write all I expected.\n");
	    }
	}
	else
	{
	    fprintf(stderr, "Error writing data: %s\n",
		globus_object_printable_to_string(globus_error_peek(result)));
	}
    }
    globus_xio_handle_cntl(
	    handle,
	    info->http_driver,
	    GLOBUS_XIO_HTTP_HANDLE_SET_END_OF_ENTITY);
    /* READ RESPONSE */
    result = globus_xio_data_descriptor_init(&descriptor, handle);
    if (result != GLOBUS_SUCCESS)
    {
	rc = 51;

        goto close_exit;
    }
    result = globus_xio_read(
            handle,
            info->buffer,
            0,
            0,
            NULL,
            descriptor);
    if (result != GLOBUS_SUCCESS)
    {
        rc = 51;
        goto close_exit;
    }

    result = globus_xio_data_descriptor_cntl(
            descriptor,
            info->http_driver,
            GLOBUS_XIO_HTTP_GET_RESPONSE,
            &status_code,
            &reason_phrase,
            NULL,
            NULL);
    if (result != GLOBUS_SUCCESS || status_code < 200 || status_code > 299)
    {
        fprintf(stderr, "Get failed with \"%03d %s\"\n",
                status_code,
                reason_phrase);

        rc = 51;
        goto close_exit;
    }

    result = globus_xio_read(
            handle,
            info->buffer,
            info->size,
            1,
            &nbytes,
            NULL);
    if (result && !http_is_eof(result))
    {       
        fprintf(stderr, "Error reading eof from http: %s\n",
                globus_error_print_friendly(globus_error_get(result)));
    }

close_exit:
    globus_xio_close(handle, NULL);
    globus_utp_stop_timer(timer);

error_exit:

    return rc;
}
/* client_test() */


int
server_test(
    http_test_info_t *			info,
    int					timer)
{
    int                                 rc = 0;
    globus_result_t                     result;
    http_test_server_t                  test_server;


    globus_utp_start_timer(timer);
    result = http_test_server_init(
            &test_server,
            info->tcp_driver,
            info->http_driver,
            info->stack);
    test_server.info = info;

    if (result != GLOBUS_SUCCESS)
    {
        rc = 29;

        goto error_exit;
    }

    result = http_test_server_register_handler(
            &test_server,
            "/post-test",
            globus_l_xio_test_server_request_callback,
            &test_server);

    printf("%s\n", test_server.contact);

    fflush(stdout);

    result = http_test_server_run(&test_server);

    if (result != GLOBUS_SUCCESS)
    {
        rc = 30;

        goto error_exit;
    }
    http_test_server_destroy(&test_server);
    globus_utp_stop_timer(timer);

error_exit:
    return rc;
}
/* server_test() */

static
void
globus_l_xio_test_server_request_callback(
    void *                              user_arg,
    globus_result_t                     result,
    const char *                        method,
    const char *                        uri,
    globus_xio_http_version_t           http_version,
    globus_hashtable_t                  headers)
{
    http_test_server_t *                test_server;
    http_test_info_t *			info;
    globus_xio_http_header_t            response_headers[2];
    globus_size_t                       header_cnt=0;
    char                                content_length_buffer[64];
    int                                 rc=0;
    int					i;
    size_t				nbytes;

    test_server = (http_test_server_t*) user_arg;
    info = test_server->info;
    if (result == GLOBUS_SUCCESS &&
            method != NULL && uri != NULL &&
            (strcmp(method, "POST") == 0) &&
            (strcmp(uri, "/post-test") == 0))
    {
	
	for (i = 0; i < info->iterations; i++)
	{
	    result = globus_xio_read(
		    test_server->handle,
		    info->buffer,
		    info->size,
		    info->size,
		    &nbytes,
		    NULL);

	    if (result != GLOBUS_SUCCESS || nbytes != info->size)
	    {
		fprintf(stderr, "Error reading from http: %s\n",
		    globus_object_printable_to_string(
			globus_error_peek(result)));
	    }
	}
    }
    else
    {
        rc = 404;
        goto error_respond_exit;
    } 
    result = globus_xio_read(
	    test_server->handle,
	    info->buffer,
	    info->size,
	    1,
	    &nbytes,
	    NULL);

    if (result && !http_is_eof(result))
    {
        fprintf(stderr, "Error reading eof from http: %s\n",
                globus_error_print_friendly(globus_error_get(result)));
    }

    if (info->transfer_encoding != NULL)
    {
	response_headers[header_cnt].name = "Transfer-Encoding";
	response_headers[header_cnt].value = info->transfer_encoding;

	header_cnt++;
    }

    if ((http_version == GLOBUS_XIO_HTTP_VERSION_1_0) ||
	    ((info->transfer_encoding != NULL)
		&& strcmp(info->transfer_encoding, IDENTITY) == 0))
    {
	    sprintf(content_length_buffer, "%lu", (unsigned long) info->size);

	    response_headers[header_cnt].name = "Content-Length";
	    response_headers[header_cnt].value = &content_length_buffer[0];

	    header_cnt++;
    }

    result = http_test_server_respond(
	    test_server,
	    rc,
	    NULL,
	    response_headers,
	    header_cnt);

    if (result != GLOBUS_SUCCESS)
    {
	goto error_exit;
    }
    result = globus_xio_write(
            test_server->handle,
            info->buffer,
            1,
            1,
            &nbytes,
            NULL);
    globus_xio_handle_cntl(
            test_server->handle,
            info->http_driver,
            GLOBUS_XIO_HTTP_HANDLE_SET_END_OF_ENTITY);
    info->size = throughput_next_size(info->size);
    if (info->size == -1)
    {
        http_test_server_close_handle(test_server);
        http_test_server_shutdown(test_server);
    }

    return;

error_respond_exit:
    http_test_server_respond(
            test_server,
            rc,
            NULL,
            NULL,
            0);

error_exit:
    http_test_server_close_handle(test_server);
    http_test_server_shutdown(test_server);

}
/* globus_l_xio_test_server_request_callback() */

void
globus_l_xio_test_server_handle_request(
    void *                              ignored)
{
}
/* globus_l_xio_test_server_handle_request() */

int
main(
    int                                 argc,
    char *                              argv[])
{
    int                                 rc;
    globus_bool_t                       server = GLOBUS_FALSE;
    char                                gets_buffer[1024];
    http_test_info_t *			info;
    performance_t                       perf;

    info = (http_test_info_t *) globus_malloc(sizeof(http_test_info_t));
    info->version = GLOBUS_XIO_HTTP_VERSION_UNSET;
    info->transfer_encoding = NULL;
    info->iterations = 100;
    info->temp_iterations = 100;
    info->size = 0;
    info->contact = NULL;
    while ((rc = getopt(argc, argv, "h:cst:b:v:i:")) != EOF)
    {
        switch (rc)
        {
            case 'h':
                usage(argv[0]);
                exit(0);
            case 'c':
                server = GLOBUS_FALSE;
                info->contact = fgets(gets_buffer, sizeof(gets_buffer), stdin);
                break;
            case 's':
                server = GLOBUS_TRUE;
                info->contact = NULL;
                break;
            case 't':
                if ((strcmp(optarg, CHUNKED) == 0)
                        || (strcmp(optarg, IDENTITY) == 0))
                {
                    info->transfer_encoding = optarg;
                }
                else
                {
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 'b':
                sscanf(optarg, "%zd", &info->size);

                if (rc != 1)
                {
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 'v':
                if (strcmp(optarg, HTTP_1_0) == 0)
                {
                    info->version = GLOBUS_XIO_HTTP_VERSION_1_0;
                }
                else if (strcmp(optarg, HTTP_1_1) == 0)
                {
                    info->version = GLOBUS_XIO_HTTP_VERSION_1_1;
                }
                else
                {
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 'i':
                info->iterations = atoi(optarg);

                if (info->iterations <= 0)
                {
                    usage(argv[0]);
                    exit(1);
                }
		info->temp_iterations = info->iterations;
                break;
            default:
                usage(argv[0]);
                exit(1);
        }
    }
    if ((!server) && (info->contact == NULL))
    {
        usage(argv[0]);
        exit(1);
    }

    if (info->iterations > 1 && info->version == GLOBUS_XIO_HTTP_VERSION_1_0)
    {
        fprintf(stderr,
                "Can't have multiple iterations with HTTP/1.0 server\n");
        usage(argv[0]);
        exit(1);
    }

    rc = http_test_initialize(
		&info->tcp_driver, &info->http_driver, &info->stack);

    if (rc != 0)
    {
        goto error_exit;
    }

    if (server)
    {
	performance_init(
	    &perf,
	    server_test,
	    throughput_next_size,
	    info->iterations,
	    "throughput-globus-xio-http",
	    info->size);
	performance_start_slave(&perf, info);
    }
    else
    {
	performance_init(
	    &perf,
	    client_test,
	    throughput_next_size,
	    info->iterations,
	    "throughput-globus-xio-http",
	    info->size);
	rc = performance_start_master(&perf, info);
	if (rc != 0)
	{
	    goto error_exit;
	}
	performance_write_timers(&perf);
    }

    globus_xio_stack_destroy(info->stack);
    globus_xio_driver_unload(info->tcp_driver);
    globus_xio_driver_unload(info->http_driver);

    globus_module_deactivate_all();

error_exit:
    if (rc == 0)
    {
        printf("Success\n");
    }
    else
    {
        printf("Error\n");
    }
    return rc;
}
/* main() */
