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

#include "globus_xio.h"
#include "globus_xio_util.h"
#include "globus_xio_gridftp_driver.h"
#include "globus_ftp_client.h"
#include "globus_preload.h"

#define CHUNK_SIZE 5000
#define FILE_NAME_LEN 1024
#define ALG_NAME_LEN 25

#define test_res(res) _test_res(res, __LINE__)

void
_test_res(
    globus_result_t                         res,
    int                                     line)
{
    if(res == GLOBUS_SUCCESS)
    {
        return;
    }

    fprintf(stderr, "ERROR (%d): %s\n", line, globus_error_print_chain(
        globus_error_peek(res)));

    globus_assert(0);
}

void
help()
{
    fprintf(stdout, 
        "globus-xio-gridftp [options]\n"
        "-----------------\n"
        "specify -c <contact string> to communicate with the server"
        "\n"
        "-f : file name"
        "\n"
        "use -r to read the file from server"
        "\n"
        "-w to write the file to server"
        "\n");
}

int
main(
    int                                     argc,
    char **                                 argv)
{
    globus_xio_driver_t                     gridftp_driver;
    globus_xio_stack_t                      stack;
    globus_xio_handle_t                     xio_handle;
    globus_xio_attr_t                       attr = NULL;
    const char *                            cs = getenv("FTP_TEST_SOURCE_HOST");
    const char *                            subject = getenv("GLOBUS_FTP_CLIENT_TEST_SUBJECT");
    globus_result_t                         res;
    int                                     ctr;
    globus_bool_t                           read = GLOBUS_TRUE;
    globus_bool_t                           user_handle = GLOBUS_FALSE;
    globus_bool_t                           partial_xfer = GLOBUS_FALSE;
    globus_bool_t                           seek = GLOBUS_FALSE;
    globus_bool_t                           append = GLOBUS_FALSE;
    globus_bool_t                           eret_esto = GLOBUS_FALSE;
    int                                     rc;
    const char *                            filename = NULL;
    char                                    eret_esto_alg_str[ALG_NAME_LEN];
    FILE *                                  fp;
    globus_ftp_client_handle_t              ftp_handle;

    LTDL_SET_PRELOADED_SYMBOLS();

    for(ctr = 1; ctr < argc; ctr++)
    {
        if(strcmp(argv[ctr], "-h") == 0)
        {
            help();
            return 0;
        }
        if(strcmp(argv[ctr], "-c") == 0)
        {
            if (ctr + 1 < argc)
            {
                cs = argv[++ctr];
            }
            else        
            {
                help();
                exit(1);
            }
        }
        else if(strcmp(argv[ctr], "-f") == 0)
        {
            if (ctr + 1 < argc)
            {
                filename =  argv[++ctr];
            }
            else        
            {
                help();
                exit(1);
            }
        }
        else if(strcmp(argv[ctr], "-r") == 0)
        {
            read = GLOBUS_TRUE;
        }       
        else if(strcmp(argv[ctr], "-w") == 0)
        {
            read = GLOBUS_FALSE;
        }
        else if(strcmp(argv[ctr], "-u") == 0)
        {
            user_handle = GLOBUS_TRUE;
            res = globus_ftp_client_handle_init(&ftp_handle, GLOBUS_NULL);
            test_res(res);
        }
        else if(strcmp(argv[ctr], "-p") == 0)
        {
            partial_xfer = GLOBUS_TRUE;
        }
        else if(strcmp(argv[ctr], "-e") == 0)
        {
            if (ctr + 1 < argc)
            {
                strcpy(eret_esto_alg_str, argv[ctr + 1]);
                ctr++;
                eret_esto = GLOBUS_TRUE;
            }
        }
        else if(strcmp(argv[ctr], "-s") == 0)
        {
            seek = GLOBUS_TRUE;
        }
        else if(strcmp(argv[ctr], "-a") == 0)
        {
            append = GLOBUS_TRUE;
        }
        else if (strcmp(argv[ctr], "-subject") == 0 && (ctr + 1) < argc)
        {
            subject = argv[++ctr];
        }
        else
        {
            help();
            exit(1);
        }
    }
    printf("cs=%s\n", cs);

    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    globus_assert(rc == GLOBUS_SUCCESS);

    res = globus_xio_driver_load("gridftp", &gridftp_driver);
    test_res(res);
    res = globus_xio_stack_init(&stack, NULL);
    test_res(res);
    res = globus_xio_stack_push_driver(stack, gridftp_driver);
    test_res(res);
    
  
    res = globus_xio_handle_create(&xio_handle, stack);
    test_res(res);
    res = globus_xio_stack_destroy(stack);
    test_res(res);
    res = globus_xio_attr_init(&attr);
    test_res(res);
    if (user_handle)
    {
        res = globus_xio_attr_cntl(attr, gridftp_driver,
            GLOBUS_XIO_GRIDFTP_SET_HANDLE, &ftp_handle);
        test_res(res);
    }
    if (partial_xfer)
    {
        res = globus_xio_attr_cntl(attr, gridftp_driver, 
            GLOBUS_XIO_GRIDFTP_SET_PARTIAL_TRANSFER, GLOBUS_TRUE);
        test_res(res);
    }
    if (append)
    {
        res = globus_xio_attr_cntl(attr, gridftp_driver, 
            GLOBUS_XIO_GRIDFTP_SET_APPEND, GLOBUS_TRUE);
        test_res(res);
    }
    if (eret_esto)
    {
        if (read)
        {
            res = globus_xio_attr_cntl(attr, gridftp_driver, 
                GLOBUS_XIO_GRIDFTP_SET_ERET, eret_esto_alg_str);
        }
        else
        {
            res = globus_xio_attr_cntl(attr, gridftp_driver, 
                GLOBUS_XIO_GRIDFTP_SET_ESTO, eret_esto_alg_str);
        }
        test_res(res);
    }
    if (subject)
    {
        res = globus_xio_attr_cntl(attr, gridftp_driver,
                GLOBUS_XIO_GRIDFTP_SET_AUTH,
                GSS_C_NO_CREDENTIAL,
                NULL,
                NULL,
                NULL,
                subject);
        test_res(res);
    }
    fprintf(stderr, "# Opening %s\n", cs);
    res = globus_xio_open(xio_handle, cs, attr);
    test_res(res);

    if(!read)
    {
        globus_byte_t                   buffer[CHUNK_SIZE + 1];
        size_t                          nbytes;
        int i, x, j = 0;
        fp = fopen(filename, "r");
        if (fp == NULL)
        {
            fprintf(stderr, "ERROR: file could not be open\n");
            globus_assert(0);
        }
        while(!feof(fp))
        {
            for (i = 0; i< CHUNK_SIZE + 1; i++)
                buffer[i] = '\0';
            x = fread(buffer, CHUNK_SIZE, 1, fp);
            nbytes = strlen((char *) buffer);
            res = globus_xio_write(
                xio_handle,
                buffer,
                nbytes,
                nbytes,
                &nbytes,
                NULL);
            test_res(res); 
            if (seek)
            {
                j += 2*nbytes;
                res = globus_xio_handle_cntl(xio_handle, gridftp_driver,
                    GLOBUS_XIO_GRIDFTP_SEEK, j);
                test_res(res); 
            }
        } 
        fclose(fp);  
        
    }
    else
    {
        globus_byte_t                   buffer[CHUNK_SIZE + 1];
        size_t                          nbytes;
        int                             i, j = 0;
        fp = fopen(filename, "w");
        if (fp == NULL)
        {
            fprintf(stderr, "ERROR: file could not be open\n");
            globus_assert(0);
        }
        while(1)
        {
            for (i=0; i<CHUNK_SIZE + 1; i++)
                buffer[i] = '\0';
            res = globus_xio_read(
                xio_handle,
                buffer,
                sizeof(buffer) - 1,
                1,
                &nbytes,
                NULL);
            fputs((const char *)buffer, fp);
            if (res == GLOBUS_SUCCESS)
            {
                if (seek)
                {
                    j += 2*nbytes;
                    res = globus_xio_handle_cntl(xio_handle, gridftp_driver,
                        GLOBUS_XIO_GRIDFTP_SEEK, j);
                    test_res(res); 
                }
            }
            else
            {
                break;
            }
        }
        fclose(fp);
    }

    res = globus_xio_close(xio_handle, NULL);
    test_res(res);

    if (user_handle)
    {
        globus_ftp_client_handle_destroy(&ftp_handle);  
    }
    res = globus_xio_driver_unload(gridftp_driver);
    test_res(res);
 
    rc = globus_module_deactivate(GLOBUS_XIO_MODULE);
    globus_assert(rc == GLOBUS_SUCCESS);
    return 0;
}
