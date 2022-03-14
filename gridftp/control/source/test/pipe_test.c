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
 *  This program tests the globus_ftp_control_data library
 */
#include "globus_ftp_control.h"
#include "globus_common.h"
#include <string.h>
#include "test_common.h"
#include "globus_preload.h"

#define TEST_ITERATIONS 10

void
test_result(
    globus_result_t                             result,
    char *                                      msg);

globus_result_t
pipe_test(
    int                                        depth);

void
failure_end(
    char *                                      msg);


void 
command_cb(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *				error,
    globus_ftp_control_response_t *		ftp_response);

typedef struct send_cmd_cb_arg_s 
{
    ftp_test_monitor_t *                  monitor;
    int                                   cmd_nr;
}
send_cmd_cb_arg_t;

static unsigned short                     g_bs_port;
static ftp_test_monitor_t                 g_server_monitor;
static globus_list_t *                    g_server_handle_list = GLOBUS_NULL;
static globus_bool_t                      g_test_done = GLOBUS_FALSE;

void 
server_stop_callback(
    void *                                      callback_arg,
    struct globus_ftp_control_server_s *        server_handle,
    globus_object_t *                           error)
{
    ftp_test_monitor_t *                        bs_monitor;

    bs_monitor = (ftp_test_monitor_t *)callback_arg;

    globus_mutex_lock(&bs_monitor->mutex);
    {
        bs_monitor->done = GLOBUS_TRUE;
        globus_cond_signal(&bs_monitor->cond);
    }
    globus_mutex_unlock(&bs_monitor->mutex);
}


void
bs_connect_callback(
    void *                                      callback_arg,
    struct globus_ftp_control_handle_s *        handle,
    globus_object_t *                           error,
    globus_ftp_control_response_t *             ftp_response)
{
    ftp_test_monitor_t *                        bs_monitor;

    if(error != GLOBUS_NULL)
    {
        verbose_printf(3, "bs_connect_callback() : %s\n",
		       globus_object_printable_to_string(error));
	globus_assert(GLOBUS_FALSE);
    }

    bs_monitor = (ftp_test_monitor_t *)callback_arg;

    verbose_printf(3, "bs_connect_callback() : start\n");
    globus_mutex_lock(&bs_monitor->mutex);
    {
        bs_monitor->done = GLOBUS_TRUE;
        globus_cond_signal(&bs_monitor->cond);
    }
    globus_mutex_unlock(&bs_monitor->mutex);
}

void
stop_server(
    globus_ftp_control_server_t *               server)
{
    ftp_test_monitor_t                          bs_monitor;
    globus_result_t                             res;

    globus_mutex_lock(&g_server_monitor.mutex);
    g_test_done = GLOBUS_TRUE;
    globus_mutex_unlock(&g_server_monitor.mutex);

    ftp_test_monitor_init(&bs_monitor);    
    res = globus_ftp_control_server_stop(
              server,
              server_stop_callback,
              (void *)&bs_monitor);
    assert(res == GLOBUS_SUCCESS);

    globus_mutex_lock(&bs_monitor.mutex);
    {
        while(!bs_monitor.done)
        {
            globus_cond_wait(&bs_monitor.cond, &bs_monitor.mutex);
        }
    }
    globus_mutex_unlock(&bs_monitor.mutex);

    res = globus_ftp_control_server_handle_destroy(server);
    assert(res == GLOBUS_SUCCESS);
}

void
connect_to_bs_server(
    globus_ftp_control_handle_t *               handle)
{
    globus_result_t                             res;
    ftp_test_monitor_t                          bs_monitor;

    ftp_test_monitor_init(&bs_monitor);    

    verbose_printf(3, "connect_to_bs_server() : Connecting to port %d\n",
		   g_bs_port);

    res = globus_ftp_control_connect(
	handle,
	"localhost",
	g_bs_port,
	bs_connect_callback,
	(void *)&bs_monitor);
    assert(res == GLOBUS_SUCCESS);

    globus_mutex_lock(&bs_monitor.mutex);
    {
        while(!bs_monitor.done)
        {
            globus_cond_wait(&bs_monitor.cond, &bs_monitor.mutex);
        }
    }
    globus_mutex_unlock(&bs_monitor.mutex);
}

void response_cb(
    void *                                      callback_arg,
    struct globus_ftp_control_handle_s *        handle,
    globus_object_t *				error)
{
    verbose_printf(3, "sent response %s\n", (char *) callback_arg);
    globus_libc_free(callback_arg);
    return;
}

void
read_command_cb(
    void *                                   callback_arg,
    struct globus_ftp_control_handle_s *     handle,
    globus_object_t *                        error,
    union globus_ftp_control_command_u *     command)
{
    if(error != GLOBUS_NULL)
    {
        globus_mutex_lock(&g_server_monitor.mutex);
        if (!g_test_done)
        {
            verbose_printf(3, "read_command_cb: Error %s\n",
                           globus_object_printable_to_string(error));
        }
        globus_mutex_unlock(&g_server_monitor.mutex);
	return;
    }

    verbose_printf(3, "server got command %s\n",
		   command->noop.raw_command);
    
    verbose_printf(3, "server sending response %s in read_command_cb\n",
		   command->noop.raw_command);

    globus_ftp_control_send_response(handle,
				     "222 %s\r\n",
				     response_cb,
				     globus_libc_strdup(
					 command->noop.raw_command),
				     command->noop.raw_command);
}

void
bullshit_auth_callback(
    void *                                   callback_arg,
    struct globus_ftp_control_handle_s *     handle,
    globus_object_t *                        error)
{
    if(error != GLOBUS_NULL)
    {
	verbose_printf(3, "bullshit_auth_callback: Error %s\n",
		       globus_object_printable_to_string(error));
    }

    verbose_printf(3, "server sent accept response\n");

    globus_ftp_control_read_commands(handle,
				     read_command_cb,
				     callback_arg);
}


void
bullshit_accept_callback(
    void *                                   callback_arg,
    struct globus_ftp_control_handle_s *     handle,
    globus_object_t *                        error)
{
    if(error != GLOBUS_NULL)
    {
	verbose_printf(3, "bullshit_auth_callback: Error %s\n",
		       globus_object_printable_to_string(error));
    }

    verbose_printf(3, "server connection accepted\n");

    globus_ftp_control_send_response(handle,
				     "220 Service ready for new user.\r\n",
				     bullshit_auth_callback,
				     callback_arg);
    
}

void 
bullshit_server(
    void *                                      callback_arg,
    struct globus_ftp_control_server_s *        server_handle,
    globus_object_t *                           error)
{
    globus_ftp_control_handle_t *               handle;
   
    if(error != GLOBUS_NULL)
    {
        globus_mutex_lock(&g_server_monitor.mutex);
        if (!g_test_done)
        {
            verbose_printf(1, "server error\n");
        }
        globus_mutex_unlock(&g_server_monitor.mutex);
        return;
    } 

    handle = (globus_ftp_control_handle_t *)
	globus_malloc(sizeof(globus_ftp_control_handle_t));

    globus_ftp_control_handle_init(handle);

    globus_mutex_lock(&g_server_monitor.mutex);
    {
        globus_list_insert(&g_server_handle_list, handle);
    }
    globus_mutex_unlock(&g_server_monitor.mutex);

    verbose_printf(3, "server connection request\n");

    globus_ftp_control_server_accept(
        server_handle,
        handle,
        bullshit_accept_callback,
        &g_server_monitor);
}

void
close_server_connection(
    globus_ftp_control_handle_t *               handle)
{
    ftp_test_monitor_t                          bs_monitor;
    globus_result_t                             res;

    verbose_printf(3, "close_server_connection() : start\n");
    bs_monitor.done = GLOBUS_FALSE;
    ftp_test_monitor_init(&bs_monitor);    
    res = globus_ftp_control_force_close(
              handle,
              bs_connect_callback,
              (void *)&bs_monitor);
    test_result(res, "force_close");
    globus_mutex_lock(&bs_monitor.mutex);
    {
        while(!bs_monitor.done)
        {
            globus_cond_wait(&bs_monitor.cond, &bs_monitor.mutex);
        }
    }
    globus_mutex_unlock(&bs_monitor.mutex);

    res = globus_ftp_control_handle_destroy(handle);
    test_result(res, "handle_destroy");
    verbose_printf(3, "close_server_connection() : end\n");
}

int 
main(
    int                                         argc,
    char *                                      argv[])
{ 
    globus_result_t                             res;
    int                                         ctr;
    int                                         rc;
    globus_ftp_control_server_t                 server;

    LTDL_SET_PRELOADED_SYMBOLS();
    printf("1..1\n");
    for(ctr = 0; ctr < argc; ctr++)
    {
        if(strcmp(argv[ctr], "--verbose") == 0)
        {
            if(ctr + 1 >= argc)
            {
                verbose_print_level = 1;
            }
            else
            {
                verbose_print_level = atoi(argv[ctr+1]);
                ctr++;
            }
        }
    }

    /*
     *  activate
     */
    rc = globus_module_activate(GLOBUS_FTP_CONTROL_MODULE);
    if(rc) res = globus_error_put(GLOBUS_ERROR_NO_INFO);
    else   res = GLOBUS_SUCCESS;
    test_result(res, "globus_module_activate failed");
    
    /* create a bullshit server */
    ftp_test_monitor_init(&g_server_monitor);

    globus_ftp_control_server_handle_init(&server);
    globus_ftp_control_server_listen(
        &server, 
        &g_bs_port,
        bullshit_server,
        GLOBUS_NULL);

    pipe_test(10);

    stop_server(&server);

    rc = globus_module_deactivate(GLOBUS_FTP_CONTROL_MODULE);
    if(rc) res = globus_error_put(GLOBUS_ERROR_NO_INFO);
    else   res = GLOBUS_SUCCESS;
    test_result(res, "deactivate");

    printf("ok - pipe test with depth 10\n");
    
    return 0;
}

/*
 *  test several read using the big buffer test
 */
globus_result_t
pipe_test(
    int                                        depth)
{
    int                                        ctr;
    int                                        i;
    globus_result_t                            res;
    globus_ftp_control_handle_t                handle;
    ftp_test_monitor_t                         done_monitor;
    send_cmd_cb_arg_t *                        cb_arg;


    cb_arg = malloc(sizeof(send_cmd_cb_arg_t)*depth);

    ftp_test_monitor_init(&done_monitor);
    done_monitor.result = GLOBUS_SUCCESS;


    res = globus_ftp_control_handle_init(&handle);
    test_result(res, "handle init");

    connect_to_bs_server(&handle);

    for(ctr = 0; ctr < TEST_ITERATIONS; ctr++)
    {
        done_monitor.done = GLOBUS_FALSE;
        done_monitor.count = 0;

	for(i=0;i<depth;i++)
	{
	    cb_arg[i].monitor = &done_monitor;
	    cb_arg[i].cmd_nr = i;
		

	    res = globus_ftp_control_send_command(&handle,
						  "%d\r\n",
						  command_cb,
						  (void *) &(cb_arg[i]),
						  i);

	    test_result(res, "sent command");
	}

        globus_mutex_lock(&done_monitor.mutex);
        {
            while(done_monitor.count < depth && 
                  !done_monitor.done)
            {
                globus_cond_wait(&done_monitor.cond, &done_monitor.mutex);
            }
        }
        globus_mutex_unlock(&done_monitor.mutex);
    }

    close_server_connection(&handle);

    return GLOBUS_SUCCESS;
}

void 
command_cb(
    void *                                      callback_arg,
    globus_ftp_control_handle_t *               handle,
    globus_object_t *				error,
    globus_ftp_control_response_t *		ftp_response)
{
    send_cmd_cb_arg_t *                         cb_arg;

    cb_arg = (send_cmd_cb_arg_t *) callback_arg;

    
    if(error != GLOBUS_NULL)
    {
	verbose_printf(3, "command_cb() : %s\n",
		       globus_object_printable_to_string(error));
	assert(0);
    }

    
    verbose_printf(3,"Got response %s to command %d\n",
		   ftp_response->response_buffer,
		   cb_arg->cmd_nr);
    
    globus_mutex_lock(&cb_arg->monitor->mutex);
    {
	cb_arg->monitor->count++;
	globus_cond_signal(&cb_arg->monitor->cond);
    }
    globus_mutex_unlock(&cb_arg->monitor->mutex);

    return;
}

void
failure_end(
    char *                                      msg)
{
    printf("not ok - %s", msg);
    exit(1);
}


void
test_result(
    globus_result_t                             res,
    char *                                      msg)
{
    if(res != GLOBUS_SUCCESS)
    {
        verbose_printf(1, "error:%s\n",
            globus_object_printable_to_string(globus_error_get(res)));
        failure_end(msg);
    }
}
