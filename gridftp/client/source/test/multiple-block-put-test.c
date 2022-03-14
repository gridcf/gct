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
 * Multiple block put.
 *
 * Tests for errors handle more data blocks than
 * the size of the file we're transferring.
 */
#include "globus_ftp_client.h"
#include "globus_ftp_client_test_common.h"

static globus_mutex_t lock;
static globus_cond_t cond;
static globus_bool_t done;
static int offset = 0;

#define BUFSIZE 100

static
void
done_cb(
	void *					user_arg,
	globus_ftp_client_handle_t *		handle,
	globus_object_t *			err)
{
    char * tmpstr;

    if(err) tmpstr = " an";
    else    tmpstr = "out";

    printf("done with%s error\n", tmpstr);
    globus_mutex_lock(&lock);
    done = GLOBUS_TRUE;
    globus_cond_signal(&cond);
    globus_mutex_unlock(&lock);
       
}

static
void
data_cb(
    void *					user_arg,
    globus_ftp_client_handle_t *		handle,
    globus_object_t *				err,
    globus_byte_t *				buffer,
    globus_size_t				length,
    globus_off_t				off,
    globus_bool_t				eof)
{
    globus_result_t				result;
    static int					once = 0;
    if(eof || err) {
	printf("Data callback: %zd bytes, %s %s\n", length,
	       eof ? "eof" : "not eof", err ? "error" : "no error");
    }
    /*fwrite(buffer, 1, length, stdout);*/
    if(!eof && !once)
    {
	once=1;
	globus_mutex_lock(&lock);
	result = globus_ftp_client_register_write(handle,
					buffer,
					BUFSIZE,
					offset,
					GLOBUS_TRUE,
					data_cb,
					0);
	if(result) printf("registration failed\n");
	else offset += BUFSIZE;
	globus_mutex_unlock(&lock);
    }
}

int main(int argc, char **argv)
{
    globus_ftp_client_handle_t			handle;
    globus_ftp_client_operationattr_t		attr;
    globus_byte_t *				buffer;
    globus_result_t				result;
    int						i;
    globus_ftp_client_handleattr_t		handle_attr;
    char *					src;
    char *					dst;

    LTDL_SET_PRELOADED_SYMBOLS();
    globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
    globus_ftp_client_handleattr_init(&handle_attr);
    globus_ftp_client_operationattr_init(&attr);

    globus_mutex_init(&lock, GLOBUS_NULL);
    globus_cond_init(&cond, GLOBUS_NULL);

    test_parse_args(argc,
		    argv,
		    &handle_attr,
		    &attr,
		    &src,
		    &dst);

    globus_ftp_client_handle_init(&handle,  &handle_attr);

    done = GLOBUS_FALSE;
    result = globus_ftp_client_put(&handle,
				   dst,
				   &attr,
				   GLOBUS_NULL,
				   done_cb,
				   0);
    globus_mutex_lock(&lock);
    if(result != GLOBUS_SUCCESS)
    {
	done = GLOBUS_TRUE;
    }
    else
    {
	for(i = 0; i < 5; i++)
	{
	    buffer = globus_libc_malloc(BUFSIZE);
	    memset(buffer, 'a', BUFSIZE);
	    globus_ftp_client_register_write(
		&handle,
		buffer,
		BUFSIZE,
		offset,
		GLOBUS_FALSE,
		data_cb,
		0);
	    offset += BUFSIZE;
	}
    }
    
    while(!done)
    {
	globus_cond_wait(&cond, &lock);
    }
    globus_mutex_unlock(&lock);

    globus_ftp_client_handle_destroy(&handle);
    globus_module_deactivate_all();

    return 0;
}
