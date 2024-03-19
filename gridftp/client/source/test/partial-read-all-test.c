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
 * makes sure that the ftp client and control libraries will handle the
 * partial get of a file, with the read_all attribute set to true.
 */
#include "globus_ftp_client.h"
#include "globus_ftp_client_test_common.h"

static globus_mutex_t lock;
static globus_cond_t cond;
static globus_bool_t done;
static globus_bool_t error = GLOBUS_FALSE;
#define SIZE 20

static
void
done_cb(
	void *					user_arg,
	globus_ftp_client_handle_t *		handle,
	globus_object_t *			err)
{
    char * tmpstr;

    if(err)
    {
	tmpstr = globus_object_printable_to_string(err);
	printf("%s\n", tmpstr); 
        error = GLOBUS_TRUE;
	globus_libc_free(tmpstr);
    }
    globus_mutex_lock(&lock);
    done = GLOBUS_TRUE;
    globus_cond_signal(&cond);
    globus_mutex_unlock(&lock);
}

static
void
intermediate_cb(
    void *					user_arg,
    globus_ftp_client_handle_t *		handle,
    globus_object_t *				err,
    globus_byte_t *				buffer,
    globus_size_t				length,
    globus_off_t				offset,
    globus_bool_t				eof)
{
    printf("intermediate cb: [%"GLOBUS_OFF_T_FORMAT",%zu]\n", offset, length);
    fwrite(buffer, 1, length, stdout);
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
    printf("[%"GLOBUS_OFF_T_FORMAT",%zu]\n", offset, length);
    fwrite(buffer, 1, length - offset, stdout);
}

int main(int argc,
	 char *argv[])
{
    globus_ftp_client_handle_t			handle;
    globus_ftp_client_operationattr_t		attr;
    globus_byte_t *				buffer;
    globus_size_t				buffer_length;
    globus_result_t				result;
    char *					src;
    char *					dst;
    globus_ftp_client_handleattr_t		handle_attr;
    globus_ftp_control_mode_t			mode;
    int						i;
    globus_ftp_control_parallelism_t		parallelism;

    LTDL_SET_PRELOADED_SYMBOLS();
    globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
    globus_ftp_client_handleattr_init(&handle_attr);
    globus_ftp_client_operationattr_init(&attr);

    parallelism.mode = GLOBUS_FTP_CONTROL_PARALLELISM_FIXED;
    parallelism.fixed.size = 1;
    
    mode = GLOBUS_FTP_CONTROL_MODE_STREAM;

    for(i = 1; i < argc; i++)
    {
	if(strcmp(argv[i], "-P") == 0 && i + 1 < argc)
	{
	    mode = GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK;

	    parallelism.mode = GLOBUS_FTP_CONTROL_PARALLELISM_FIXED;
	    parallelism.fixed.size = atoi(argv[i+1]);

	    test_remove_arg(&argc, argv, &i, 1);
	}
    }
    test_parse_args(argc, 
		    argv,
		    &handle_attr,
		    &attr,
		    &src,
		    &dst);

    buffer = globus_libc_malloc(SIZE);
    buffer_length = SIZE;
    
    globus_mutex_init(&lock, GLOBUS_NULL);
    globus_cond_init(&cond, GLOBUS_NULL);

    globus_ftp_client_operationattr_set_mode(&attr,
				             mode);
    globus_ftp_client_operationattr_set_parallelism(&attr,
					            &parallelism);
    globus_ftp_client_operationattr_set_read_all(&attr,
					         GLOBUS_TRUE,
					         intermediate_cb,
					         GLOBUS_NULL);
    globus_ftp_client_handle_init(&handle, &handle_attr);

    done = GLOBUS_FALSE;
    result = globus_ftp_client_partial_get(&handle,
				   src,
				   &attr,
				   GLOBUS_NULL,
				   10,
				   30,
				   done_cb,
				   0);
    if(result != GLOBUS_SUCCESS)
    {
	fprintf(stderr, "%s", globus_object_printable_to_string(globus_error_get(result)));
	error = GLOBUS_TRUE;
	done = GLOBUS_TRUE;
    }
    else
    {
	globus_ftp_client_register_read(
	    &handle,
	    buffer,
	    buffer_length,
	    data_cb,
	    0);
    }
    globus_mutex_lock(&lock);
    while(!done)
    {
	globus_cond_wait(&cond, &lock);
    }
    globus_mutex_unlock(&lock);

    globus_ftp_client_handle_destroy(&handle);
    globus_libc_free(buffer);
    
    globus_module_deactivate_all();

    if(test_abort_count && error)
    {
	return 0;
    }
    return error;
}
