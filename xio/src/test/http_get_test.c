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
 * @file http_get_test.c
 * @brief HTTP Get Test
 *
 * Test that clients can receive HTTP message bodies.
 *
 * Test parameters are
 * - -f filename<br>
 *   Name of file to be used as the test message
 * - -v "HTTP/1.0"|"HTTP/1.1"<br>
 *   Set HTTP version to use
 * - -t "chunked"|"identity"<br>
 *   Set transfer encoding (for HTTP/1.1 transfers only)
 * - -b buffer-size<br>
 *   Set the size (in bytes) to be read/written at a time
 *
 * The test client will send the GET request for the /get-test URI.
 *
 * The test server will 
 * - verify the receipt of the /get-test URI
 * - generate a response which contains the test file as the body.
 *
 * Test test client will then compare the response message with the contents
 * of the file.
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

int                                     done = 0;
globus_byte_t *                         message_body;
long                                    file_size;
globus_xio_http_version_t               version = GLOBUS_XIO_HTTP_VERSION_UNSET;
size_t                                  buffer_size = 0;
char *                                  transfer_encoding = NULL;
globus_xio_driver_t                     tcp_driver;
globus_xio_driver_t                     http_driver;
globus_xio_stack_t                      stack;

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

static
int
globus_l_xio_test_read_file(
    const char *                        filename);

static
globus_result_t
globus_l_xio_test_write_buffer(
    globus_xio_handle_t                 handle,
    globus_byte_t *                     message,
    globus_size_t                       message_size,
    globus_size_t                       buffer_size);

static
globus_result_t
globus_l_xio_test_read_buffer(
    globus_xio_handle_t                 handle,
    globus_byte_t *                     message,
    globus_size_t                       message_size,
    globus_size_t                       buffer_size);

void
globus_l_xio_test_server_handle_request(
    void *                              ignored);

static void usage(const char * cmd)
{
    printf("Usage: %s [options] [-c|-s] -f filename\n"
            "Options:\n"
            "    -v \"HTTP/1.0\" | \"HTTP/1.1\"    HTTP version\n"
            "    -t \"chunked\" | \"identity\"     Client Transfer-Encoding\n"
            "    -b buffer-size                    Size of reads and writes\n",
            cmd);
}

int
client_main(
    const char *                        filename,
    const char *                        contact,
    globus_xio_http_version_t           http_version)
{
    int                                 rc;
    globus_result_t                     result;
    int                                 header_cnt = 0;
    char                                content_length_buffer[64];
    globus_xio_http_header_t            headers[2];
    globus_xio_data_descriptor_t        descriptor;
    globus_byte_t                       buffer[1];
    globus_xio_handle_t                 handle;
    int                                 status_code;
    char *                              reason_phrase;

    rc = globus_l_xio_test_read_file(filename);
    if (rc != 0)
    {
        goto error_exit;
    }

    if (transfer_encoding != NULL)
    {
        headers[header_cnt].name = "Transfer-Encoding";
        headers[header_cnt].value = transfer_encoding;

        header_cnt++;

    }

    if ((http_version == GLOBUS_XIO_HTTP_VERSION_1_0) ||
            ((transfer_encoding != NULL)
                && strcmp(transfer_encoding, IDENTITY) == 0))
    {
        sprintf(content_length_buffer, "%ld", file_size);

        headers[header_cnt].name = "Content-Length";
        headers[header_cnt].value = &content_length_buffer[0];

        header_cnt++;
    }

    result = http_test_client_request(
            &handle,
            tcp_driver,
            http_driver,
            stack,
            contact,
            "%2fget-test",
            "GET",
            http_version,
            headers,
            header_cnt);

    if (result != GLOBUS_SUCCESS)
    {
        fprintf(stderr, "Error making request: %s\n",
                globus_object_printable_to_string(globus_error_get(result)));
        rc = 50;
        goto error_exit;
    }

    /* READ RESPONSE */
    result = globus_xio_data_descriptor_init(&descriptor, handle);
    if (result != GLOBUS_SUCCESS)
    {
        rc = 51;
        goto close_exit;
    }
    result = globus_xio_read(
            handle,
            buffer,
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
            http_driver,
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

    result = globus_l_xio_test_read_buffer(
            handle,
            message_body,
            file_size,
            buffer_size);

    if (result != GLOBUS_SUCCESS)
    {
        rc = 52;
        goto close_exit;
    }

close_exit:
    globus_xio_close(handle, NULL);
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
/* client_main() */


static
int
server_main(
    const char *                        filename)
{
    int                                 rc;
    globus_result_t                     result;
    http_test_server_t                  test_server;

    rc = globus_l_xio_test_read_file(filename);

    if (rc != 0)
    {
        goto error_exit;
    }

    result = http_test_server_init(
            &test_server,
            tcp_driver,
            http_driver,
            stack);

    if (result != GLOBUS_SUCCESS)
    {
        rc = 29;

        goto error_exit;
    }

    result = http_test_server_register_handler(
            &test_server,
            "/get-test",
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

error_exit:
    return rc;
}
/* server_main() */

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
    http_test_server_t *                test_server = user_arg;
    globus_xio_http_header_t            response_headers[2];
    globus_size_t                       header_cnt=0;
    char                                content_length_buffer[64];
    int                                 rc=0;

    if (result == GLOBUS_SUCCESS &&
            method != NULL && uri != NULL &&
            (strcmp(method, "GET") == 0) &&
            (strcmp(uri, "/get-test") == 0))
    {
        if (transfer_encoding != NULL)
        {
            response_headers[header_cnt].name = "Transfer-Encoding";
            response_headers[header_cnt].value = transfer_encoding;

            header_cnt++;
        }

        if ((http_version == GLOBUS_XIO_HTTP_VERSION_1_0) ||
                ((transfer_encoding != NULL)
                    && strcmp(transfer_encoding, IDENTITY) == 0))
        {
                sprintf(content_length_buffer, "%ld", file_size);

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
    }
    else
    {
        rc = 404;

        goto error_respond_exit;
    }

    result = globus_l_xio_test_write_buffer(
            test_server->handle,
            message_body,
            (globus_size_t) file_size,
            buffer_size);

    if (result != GLOBUS_SUCCESS)
    {
        fprintf(stderr, "Error writing buffer: %s\n",
                globus_object_printable_to_string(globus_error_peek(result)));
        goto error_exit;
    }

    http_test_server_shutdown(test_server);
    http_test_server_close_handle(test_server);

    return;

error_respond_exit:
    http_test_server_respond(
            test_server,
            rc,
            NULL,
            NULL,
            0);

error_exit:
    http_test_server_shutdown(test_server);
    http_test_server_close_handle(test_server);

}
/* globus_l_xio_test_server_request_callback() */

static
int
globus_l_xio_test_read_file(
    const char *                        filename)
{
    int                                 rc;
    FILE *                              fp;

    fp = fopen(filename, "rb");

    if (fp == NULL)
    {
        rc = 35;
        fprintf(stderr, "Error opening %s: %s\n",
                filename, strerror(errno));

        goto error_exit;
    }

    rc = fseek(fp, 0, SEEK_END);

    if (rc < 0)
    {
        rc = 36;
        fprintf(stderr, "Error seeking %s: %s\n",
                filename, strerror(errno));

        goto fclose_exit;
    }

    file_size = ftell(fp);
    if (file_size < 0)
    {
        rc = 37;
        fprintf(stderr, "Error getting file size: %s\n",
                strerror(errno));

        goto fclose_exit;
    }
    rewind(fp);

    message_body = globus_libc_malloc(file_size+1);
    if (message_body == NULL)
    {
        rc = 38;
        fprintf(stderr, "Error allocating buffer: %s\n",
                strerror(errno));

        goto fclose_exit;
    }
    rc = fread(message_body, (size_t) file_size, 1, fp);

    if (rc != 1)
    {
        rc = 39;
        fprintf(stderr, "Error reading %s: %s\n",
                filename, strerror(errno));
        goto fclose_exit;
    }
    message_body[(size_t)file_size] = '\0';

    rc = 0;

fclose_exit:
    fclose(fp);
error_exit:
    return rc;
}
/* globus_l_xio_test_read_file() */

static
globus_result_t
globus_l_xio_test_write_buffer(
    globus_xio_handle_t                 handle,
    globus_byte_t *                     message,
    globus_size_t                       message_size,
    globus_size_t                       buffer_size)
{
    globus_byte_t *                     ptr = message;
    globus_size_t                       left = message_size;
    globus_size_t                       to_write;
    globus_size_t                       nbytes;
    globus_result_t                     result = GLOBUS_SUCCESS;
    GlobusXIOName(globus_l_xio_test_write_buffer);

    if (buffer_size == 0)
    {
        buffer_size = 2000;
    }
    while ((left > 0) && (result == GLOBUS_SUCCESS))
    {
        to_write = (left > buffer_size) ? buffer_size : left;
        result = globus_xio_write(
                handle,
                ptr,
                to_write,
                to_write,
                &nbytes,
                NULL);

        if (result == GLOBUS_SUCCESS)
        {
            if (nbytes != to_write)
            {
                fprintf(stderr, "Didn't write all I expected.\n");
                result = GlobusXIOErrorEOF();
            }
            left -= nbytes;
            ptr += nbytes;
        }
        else
        {
            fprintf(stderr, "Error writing data: %s\n",
                    globus_object_printable_to_string(globus_error_peek(result)));
        }
    }

    globus_xio_handle_cntl(
            handle,
            http_driver,
            GLOBUS_XIO_HTTP_HANDLE_SET_END_OF_ENTITY);

    return result;
}
/* globus_l_xio_test_write_buffer() */

static
globus_result_t
globus_l_xio_test_read_buffer(
    globus_xio_handle_t                 handle,
    globus_byte_t *                     message,
    globus_size_t                       message_size,
    globus_size_t                       buffer_size)
{
    globus_size_t                       offset=0;
    globus_size_t                       left = message_size;
    globus_size_t                       nbytes;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_byte_t *                     buffer;
    GlobusXIOName(globus_l_xio_test_read_buffer);

    if (buffer_size == 0)
    {
        buffer_size = 2000;
    }
    buffer = globus_libc_malloc(buffer_size);

    if (buffer == NULL)
    {
        result = GlobusXIOErrorMemory("buffer");
        return result;
    }
    while ((left > 0) || (result == GLOBUS_SUCCESS))
    {
        nbytes = 0;
        result = globus_xio_read(
                handle,
                buffer,
                buffer_size,
                1,
                &nbytes,
                NULL);

        if (nbytes > 0)
        {
            if (left > 0)
            {
                if (memcmp(message+offset, buffer, nbytes) != 0)
                {
                    fprintf(stderr, "File doesn't match\n");
                    result = GlobusXIOErrorParameter("buffer");
                }

                left -= nbytes;
            }
            else
            {
                fprintf(stderr, "File doesn't match\n");
                result = GlobusXIOErrorParameter("buffer");
            }
            offset += nbytes;
        }
    }
    if (offset == message_size && http_is_eof(result))
    {
        result = GLOBUS_SUCCESS;
    }
    else if (result != GLOBUS_SUCCESS)
    {
        fprintf(stderr, "Error reading from http: %s\n",
                globus_object_printable_to_string(globus_error_peek(result)));

        fprintf(stderr, "after reading %lu of %lu bytes\n",
                (unsigned long) offset, (unsigned long) message_size);
    }

    return result;
}
/* globus_l_xio_test_read_buffer() */

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
    char *                              filename = NULL;
    char *                              contact = NULL;
    globus_bool_t                       server = GLOBUS_FALSE;
    char                                gets_buffer[1024];

    while ((rc = getopt(argc, argv, "hf:cst:b:v:")) != EOF)
    {
        switch (rc)
        {
            case 'h':
                usage(argv[0]);
                exit(0);
            case 'f':
                filename = optarg;
                break;
            case 'c':
                server = GLOBUS_FALSE;
                contact = fgets(gets_buffer, sizeof(gets_buffer), stdin);
                break;
            case 's':
                server = GLOBUS_TRUE;
                contact = NULL;
                break;
            case 't':
                if ((strcmp(optarg, CHUNKED) == 0)
                        || (strcmp(optarg, IDENTITY) == 0))
                {
                    transfer_encoding = optarg;
                }
                else
                {
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 'b':
                rc = sscanf(optarg, "%zd", &buffer_size);

                if (rc != 1)
                {
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 'v':
                if (strcmp(optarg, HTTP_1_0) == 0)
                {
                    version = GLOBUS_XIO_HTTP_VERSION_1_0;
                }
                else if (strcmp(optarg, HTTP_1_1) == 0)
                {
                    version = GLOBUS_XIO_HTTP_VERSION_1_1;
                }
                else
                {
                    usage(argv[0]);
                    exit(1);
                }
                break;
            default:
                usage(argv[0]);
                exit(1);
        }
    }
    if (((!server) && (contact == NULL)) || (filename == NULL))
    {
        usage(argv[0]);
        exit(1);
    }

    rc = http_test_initialize(&tcp_driver, &http_driver, &stack);

    if (rc != 0)
    {
        goto error_exit;
    }

    if (server)
    {
        rc = server_main(
                filename);
    }
    else
    {
        rc = client_main(
                filename,
                contact,
                version);
    }

    globus_xio_stack_destroy(stack);
    globus_xio_driver_unload(http_driver);
    globus_xio_driver_unload(tcp_driver);

    globus_module_deactivate_all();

error_exit:
    return rc;
}
/* main() */
