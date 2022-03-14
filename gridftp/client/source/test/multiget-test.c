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

/*
 * multi-get.
 *
 * Stress test the ftp client library; have many handles dealing with
 * multiple file retrieves.
 */
#include "globus_ftp_client.h"
#include "globus_ftp_client_test_common.h"


#define SIZE 42
static int error = 0;
globus_mutex_t lock;
globus_cond_t cond;
int handle_count;

static
void 
register_get(globus_ftp_client_handle_t *	handle);

static
void
done_cb(
	void *					user_arg,
	globus_ftp_client_handle_t *		handle,
	globus_object_t *			err)
{
    char * tmpstr;
    long iterations_left;

    if(err)
    {
	tmpstr = globus_object_printable_to_string(err);
	printf("%s\n", tmpstr); 
        error = GLOBUS_TRUE;
	globus_libc_free(tmpstr);
    }
    globus_ftp_client_handle_get_user_pointer(handle,
					      (void **) &iterations_left);
    iterations_left--;
    globus_ftp_client_handle_set_user_pointer(handle,
					      (void *) iterations_left);
    register_get(handle);
}

static
void
data_cb(
    void *					user_arg,
    globus_ftp_client_handle_t *		handle,
    globus_object_t *				err,
    globus_byte_t *				buffer,
    globus_size_t				length,
    globus_off_t				offset,
    globus_bool_t				eof)
{
    fwrite(buffer, 1, length, stdout);
    if(!eof)
    {
	globus_ftp_client_register_read(handle,
					buffer,
					SIZE,
					data_cb,
					0);
    }
}

static globus_ftp_client_operationattr_t		attr;
static char *					src;
globus_ftp_client_handleattr_t		handle_attr;

int main(int argc, char *argv[])
{
    globus_ftp_client_handle_t *		handles;
    int						num_handles = 0;
    long					num_iterations = 0;
    char *					dst;
    int						i;
    globus_bool_t				caching = GLOBUS_FALSE;

    /* Parse local arguments */
    for(i = 1; i < argc; i++)
    {
	if(strcmp(argv[i], "-H") == 0 && i + 1 < argc)
	{
	    num_handles = atoi(argv[i+1]);
	    test_remove_arg(&argc, argv, &i, 1);
	}
	if(strcmp(argv[i], "-I") == 0 && i + 1 < argc)
	{
	    num_iterations = atol(argv[i+1]);

	    test_remove_arg(&argc, argv, &i, 1);
	}
	if(strcmp(argv[i], "-C") == 0 && i < argc)
	{
	    caching = GLOBUS_TRUE;

	    test_remove_arg(&argc, argv, &i, 0);
	}
    }

    if(num_handles <= 0) num_handles = 1;
    if(num_iterations <= 0) num_iterations = 1;

    handle_count = num_handles;

    LTDL_SET_PRELOADED_SYMBOLS();
    globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
    globus_ftp_client_handleattr_init(&handle_attr);
    globus_ftp_client_operationattr_init(&attr);

    test_parse_args(argc, 
		    argv,
		    &handle_attr,
		    &attr,
		    &src,
		    &dst);


    globus_mutex_init(&lock, GLOBUS_NULL);
    globus_cond_init(&cond, GLOBUS_NULL);

    handles = globus_libc_malloc(num_handles * sizeof(globus_ftp_client_handle_t));
    for(i = 0; i < num_handles; i++)
    {
	globus_ftp_client_handle_init(&handles[i],  &handle_attr);
	if(caching)
	{
	    globus_ftp_client_handle_cache_url_state(&handles[i],
		                              src);
	}
	globus_ftp_client_handle_set_user_pointer(&handles[i],
						  (void *) num_iterations);
	register_get(&handles[i]);
    }
    globus_mutex_lock(&lock);
    while(handle_count > 0)
    {
	globus_cond_wait(&cond, &lock);
    }
    globus_mutex_unlock(&lock);

    for(i = 0; i < num_handles; i++)
    {
	globus_ftp_client_handle_destroy(&handles[i]);
    }

    globus_module_deactivate_all();

    if(test_abort_count && error)
    {
	return 0;
    }
    return error;
}

static void 
register_get(globus_ftp_client_handle_t *	handle)
{
    globus_byte_t *				buffer;
    long					iterations_left;
    globus_result_t				result;

    globus_ftp_client_handle_get_user_pointer(handle,
					      (void **) &iterations_left);

    if(iterations_left > 0)
    {
	result = globus_ftp_client_get(handle,
				       src,
				       &attr,
				       GLOBUS_NULL,
				       done_cb,
				       0);
	if(result)
	{
	    globus_object_t * error;
	    char * errstr;
	    
	    error = globus_error_get(result);
	    errstr = globus_object_printable_to_string(error);
	    
	    fprintf(stderr, "%s", errstr);
	    globus_libc_free(errstr);
	    globus_object_free(error);
	    iterations_left--;
	    globus_ftp_client_handle_set_user_pointer(handle,
						      (void *) iterations_left);
	    if(iterations_left == 0)
	    {
		goto no_more_iterations;
	    }
	}
	else
	{
	    buffer = globus_libc_malloc(SIZE);

	    globus_ftp_client_register_read(
		handle,
		buffer,
		SIZE,
		data_cb,
		0);
	}
    }
    else
    {
    no_more_iterations:
	globus_mutex_lock(&lock);
	handle_count--;
	globus_cond_signal(&cond);
	globus_mutex_unlock(&lock);
    }
}
