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
 * chmod test.
 *
 * makes sure that the ftp client and control libraries are able to
 * change file modes
 */
#include "globus_ftp_client.h"
#include "globus_ftp_client_test_common.h"

static globus_mutex_t lock;
static globus_cond_t cond;
static globus_bool_t done;
static globus_bool_t error = GLOBUS_FALSE;

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

    if(err) { printf("done with%s error\n", tmpstr);
	      error = GLOBUS_TRUE; }
    globus_mutex_lock(&lock);
    done = GLOBUS_TRUE;
    globus_cond_signal(&cond);
    globus_mutex_unlock(&lock);
       
}

int main(int argc, char * argv[])
{
    globus_ftp_client_handle_t			handle;
    globus_ftp_client_operationattr_t		attr;
    globus_result_t				result;
    globus_ftp_client_handleattr_t		handle_attr;
    char *					src = NULL;
    char *					dst = NULL;
    int                                         mode;
    extern char *                               optarg;
    extern int                                  optind;
    int                                         c;

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

    optind = 1;
    while((c = getopt(argc, argv, "A:")) != -1)
    {
	switch(c)
	{
	  case 'A':
	    mode = strtol(optarg, NULL, 0);
	    break;
	}
    }	


    globus_ftp_client_operationattr_set_type(&attr,
	                                     GLOBUS_FTP_CONTROL_TYPE_ASCII);

    globus_ftp_client_handleattr_set_cache_all(&handle_attr,
                                               GLOBUS_TRUE);
    
    globus_ftp_client_handle_init(&handle,  &handle_attr);

    done = GLOBUS_FALSE;
    result = globus_ftp_client_chmod(&handle,
				      src,
				      mode,
				      &attr,
				      done_cb,
				      0);
    if(result != GLOBUS_SUCCESS)
    {
	done = GLOBUS_TRUE;
    }

    globus_mutex_lock(&lock);
    while(!done)
    {
	globus_cond_wait(&cond, &lock);
    }
    globus_mutex_unlock(&lock);
    
    globus_ftp_client_handle_destroy(&handle);
    globus_module_deactivate_all();

    if(test_abort_count && error)
    {
	return 0;
    }
    return error;
}
































