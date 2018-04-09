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

#include "globus_gram_client.h"
#include "globus_preload.h"

static
void
callback_func(
    void *				user_arg,
    char *				job_contact,
    int					state,
    int					errorcode);

/* add and remove a callback contact */
static
int
add_remove_callback_contact_test(void)
{
    char * callback_contact;
    int rc;

    rc = globus_module_activate(GLOBUS_GRAM_CLIENT_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    rc = globus_gram_client_callback_allow(
	    callback_func,
	    GLOBUS_NULL,
	    &callback_contact);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    rc = globus_gram_client_callback_disallow(
	    callback_contact);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    globus_libc_free(callback_contact);
    return globus_module_deactivate(GLOBUS_GRAM_CLIENT_MODULE);
}

/* remove a bogus callback contact */
static
int 
remove_bogus_callback_contact(void)
{
    int rc;

    rc = globus_module_activate(GLOBUS_GRAM_CLIENT_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    rc = globus_gram_client_callback_disallow(
	    "bogus_callback_contact");

    if(rc == GLOBUS_SUCCESS)
    {
	return !rc;
    }
    return globus_module_deactivate(GLOBUS_GRAM_CLIENT_MODULE);
}

/* create a few callback contacts, and remove them out of order */
static
int
remove_callback_contacts_out_of_order(void)
{
    char *callback_contact[3];
    int rc;

    rc = globus_module_activate(GLOBUS_GRAM_CLIENT_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    rc = globus_gram_client_callback_allow(
	    callback_func,
	    GLOBUS_NULL,
	    &callback_contact[0]);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    rc = globus_gram_client_callback_allow(
	    callback_func,
	    GLOBUS_NULL,
	    &callback_contact[1]);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    rc = globus_gram_client_callback_allow(
	    callback_func,
	    GLOBUS_NULL,
	    &callback_contact[2]);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    rc = globus_gram_client_callback_disallow(callback_contact[1]);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    rc = globus_gram_client_callback_disallow(callback_contact[2]);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    rc = globus_gram_client_callback_disallow(callback_contact[0]);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    globus_libc_free(callback_contact[0]);
    globus_libc_free(callback_contact[1]);
    globus_libc_free(callback_contact[2]);
    return globus_module_deactivate(GLOBUS_GRAM_CLIENT_MODULE);
}

/* create a callback contact, and then don't disable it before
 * deactivating.
 */
static 
int
deactivate_before_callback_disallow(void)
{
    int rc;
    char * callback_contact;

    rc = globus_module_activate(GLOBUS_GRAM_CLIENT_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    rc = globus_gram_client_callback_allow(
	    callback_func,
	    GLOBUS_NULL,
	    &callback_contact);
    if(rc != GLOBUS_SUCCESS)
    {
	return rc;
    }
    rc = globus_gram_client_callback_disallow(callback_contact);
    if(rc != GLOBUS_SUCCESS)
    {
        return rc;
    }
    globus_libc_free(callback_contact);
    return globus_module_deactivate(GLOBUS_GRAM_CLIENT_MODULE);
}

int main(int argc, char *argv[])
{
    int rc;
    int test_num = 0;
    int not_ok = 0;

    LTDL_SET_PRELOADED_SYMBOLS();

    printf("1..4\n");

    rc = add_remove_callback_contact_test();
    printf("%s %d - add_remove_callback_contact_test\n",
            rc ? "not ok" : "ok", ++test_num);
    not_ok |= rc;

    rc = remove_bogus_callback_contact();
    printf("%s %d - remove_bogus_callback_contact\n",
            rc ? "not ok" : "ok", ++test_num);
    not_ok |= rc;

    rc = remove_callback_contacts_out_of_order();
    printf("%s %d - remove_callback_contacts_out_of_order\n",
            rc ? "not ok" : "ok", ++test_num);
    not_ok |= rc;

    rc = deactivate_before_callback_disallow();
    printf("%s %d - deactivate_before_callback_disallow\n",
            rc ? "not ok" : "ok", ++test_num);
    not_ok |= rc;

    return not_ok;
}

static
void
callback_func(
    void *				user_arg,
    char *				job_contact,
    int					state,
    int					errorcode)
{
    return;
}
