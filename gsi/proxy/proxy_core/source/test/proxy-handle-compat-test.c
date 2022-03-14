/*
 * Copyright 1999-2016 University of Chicago
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

#define GLOBUS_GSI_PROXY_CORE_COMPAT_0
#include "globus_common.h"
#include "globus_gsi_proxy.h"

#include <stdbool.h>

struct test_case
{
    const char                         *test_name;
    bool                              (*test_func)(void);
};

static int test_policy_nid;

#define TEST_CASE_INITIALIZER(c) { #c, c }

static
bool
proxy_handle_set_proxy_cert_info_compat_null_test(void)
{
    bool                                ok = true;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_set_proxy_cert_info(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    return ok;
}

static
bool
proxy_handle_get_proxy_cert_info_compat_null_test(void)
{
    bool                                ok = true;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_proxy_handle_t           handle = NULL;
    PROXYCERTINFO                      *pci = NULL;

    result = globus_gsi_proxy_handle_get_proxy_cert_info(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_proxy_cert_info(NULL, &pci);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }
    result = globus_gsi_proxy_handle_get_proxy_cert_info(handle, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_set_get_proxy_cert_info_compat_test(void)
{
    bool                                ok = true;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_proxy_handle_t           handle = NULL;
    PROXYCERTINFO                      *pci = NULL;
    PROXYCERTINFO                       *new_pci = NULL;
    unsigned char                       hello_policy[] = "hello";

    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }
    result = globus_gsi_proxy_handle_get_proxy_cert_info(handle, &pci);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (OBJ_obj2nid(pci->policy->policy_language) != NID_id_ppl_inheritAll)
    {
        ok = false;
    }
    ASN1_OBJECT_free(pci->policy->policy_language);
    pci->policy->policy_language = OBJ_nid2obj(test_policy_nid);
    if (pci->policy->policy == NULL)
    {
        pci->policy->policy = ASN1_OCTET_STRING_new();
    }
    ASN1_OCTET_STRING_set(pci->policy->policy, hello_policy, sizeof(hello_policy)-1);

    result = globus_gsi_proxy_handle_set_proxy_cert_info(handle, pci);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_proxy_cert_info(handle, &new_pci);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (new_pci == pci)
    {
        ok = false;
    }
    if (OBJ_obj2nid(pci->policy->policy_language) != test_policy_nid)
    {
        ok = false;
    }
    PROXYCERTINFO_free(pci);
    PROXYCERTINFO_free(new_pci);
    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}


int
main(int argc, char *argv[])
{
    struct test_case                    test_cases[] =
    {
        TEST_CASE_INITIALIZER(proxy_handle_set_proxy_cert_info_compat_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_proxy_cert_info_compat_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_get_proxy_cert_info_compat_test),
    };
    size_t                              num_test_cases = sizeof(test_cases)/sizeof(test_cases[0]);
    int                                 failed = 0;

    globus_module_activate(GLOBUS_GSI_PROXY_MODULE);

    printf("1..%zu\n", num_test_cases);
    test_policy_nid = OBJ_create("1.3.6.1.4.1.3536.9999", "GlobusTest", "Globus Test");

    for (size_t i = 0; i < num_test_cases; i++)
    {
        if (test_cases[i].test_func())
        {
            printf("ok %zu - %s\n", i+1, test_cases[i].test_name);
        }
        else
        {
            failed++;
            printf("not ok %zu - %s\n", i+1, test_cases[i].test_name);
        }
    }
    globus_module_deactivate(GLOBUS_GSI_PROXY_MODULE);

    return failed;
}
