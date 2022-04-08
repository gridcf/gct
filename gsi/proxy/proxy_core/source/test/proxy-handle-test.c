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
#if OPENSSL_VERSION_NUMBER < 0x10000000L
#define GENERAL_NAME_set0_value(gn, t, dns) \
    do \
    { \
        GENERAL_NAME *g = (gn); \
        g->type = (t); \
        g->d.dNSName = (dns);\
    } \
    while (0)
#endif

#define DEFINE_ASN1_CMP_OF(type, i2d) \
    static int \
    type##_cmp(type *A, type *B) \
    { \
        int res = 1; \
        int alen = i2d(A, NULL); \
        int blen = i2d(B, NULL); \
        if (alen != blen) \
        { \
            res = 0; \
        } \
        else \
        { \
            unsigned char ader[alen]; \
            unsigned char bder[blen]; \
            unsigned char *aderptr = ader; \
            unsigned char *bderptr = bder; \
            i2d(A, &aderptr); \
            i2d(B, &bderptr); \
            res = !memcmp(ader, bder, alen); \
        } \
        return res; \
    }

DEFINE_ASN1_CMP_OF(X509_REQ, i2d_X509_REQ)
DEFINE_ASN1_CMP_OF(X509_EXTENSION, i2d_X509_EXTENSION)

static
bool
proxy_handle_init_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_init(NULL, NULL);

    if (result == GLOBUS_SUCCESS)
    {
        return false;
    }
    return true;
}

static
bool
proxy_handle_destroy_null_test(void)
{
    globus_gsi_proxy_handle_destroy(NULL);

    return true;
}

static
bool
proxy_handle_init_destroy_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_proxy_handle_t           handle = NULL;

    result = globus_gsi_proxy_handle_init(&handle, NULL);

    if (result != GLOBUS_SUCCESS)
    {
        return false;
    }

    result = globus_gsi_proxy_handle_destroy(handle);
    if (result != GLOBUS_SUCCESS)
    {
        return false;
    }

    return true;
}

static
bool
proxy_handle_set_req_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_set_req(NULL, NULL);

    if (result == GLOBUS_SUCCESS)
    {
        return false;
    }

    return true;
}

static
bool
proxy_handle_get_req_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    X509_REQ                           *req = NULL;

    result = globus_gsi_proxy_handle_init(&handle, NULL);

    if (result != GLOBUS_SUCCESS)
    {
        return false;
    }

    result = globus_gsi_proxy_handle_get_req(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }

    result = globus_gsi_proxy_handle_get_req(handle, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }

    result = globus_gsi_proxy_handle_get_req(NULL, &req);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }

    (void) globus_gsi_proxy_handle_destroy(handle);

    return ok;
}

static
bool
proxy_handle_set_get_req_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    X509_REQ                           *req = NULL;
    X509_REQ                           *orig_req = NULL;
    BIO                                *req_bio;

    result = globus_gsi_proxy_handle_init(&handle, NULL);

    if (result != GLOBUS_SUCCESS)
    {
        return false;
    }

    result = globus_gsi_proxy_handle_get_req(handle, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }

    req_bio = BIO_new(BIO_s_mem());
    if (req_bio == NULL)
    {
        ok = false;
        goto fail_bio;
    }
    result = globus_gsi_proxy_create_req(
            handle,
            req_bio);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto fail_create_req;
    }
    if (d2i_X509_REQ_bio(req_bio, &orig_req) == NULL)
    {
        ok = false;
        goto fail_extract_req;
    }
    result = globus_gsi_proxy_handle_set_req(handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_req(handle, &req);
    if (result == GLOBUS_SUCCESS || req != NULL)
    {
        ok = false;
    }

    result = globus_gsi_proxy_handle_set_req(handle, orig_req);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto fail_set_req;
    }
    result = globus_gsi_proxy_handle_get_req(handle, &req);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto fail_get_req;
    }

    if (req == orig_req)
    {
        ok = false;
    }
    if (X509_REQ_cmp(orig_req, req) != 1)
    {
        ok = false;
    }
    X509_REQ_free(req);
fail_get_req:
fail_set_req:
    X509_REQ_free(orig_req);
fail_extract_req:
fail_create_req:
    BIO_free(req_bio);
fail_bio:
    (void) globus_gsi_proxy_handle_destroy(handle);

    return ok;
}

static
bool
proxy_handle_set_private_key_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;

    result = globus_gsi_proxy_handle_set_private_key(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }

    return ok;
}

static
bool
proxy_handle_get_private_key_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    EVP_PKEY                           *pk = NULL;

    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }
    result = globus_gsi_proxy_handle_get_private_key(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_private_key(handle, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_private_key(handle, &pk);
    if (result == GLOBUS_SUCCESS || pk != NULL)
    {
        ok = false;
    }
    globus_gsi_proxy_handle_destroy(handle);

no_handle:
    return ok;
}

static
bool
proxy_handle_set_get_private_key_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    RSA                                *rsa = NULL;
    EVP_PKEY                           *pk_orig = NULL;
    EVP_PKEY                           *pk = NULL;

    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }
    result = globus_gsi_proxy_handle_get_private_key(handle, &pk);
    if (result == GLOBUS_SUCCESS || pk != NULL)
    {
        ok = false;
    }
    rsa = RSA_generate_key(2048, RSA_F4, NULL, NULL);
    if (rsa == NULL)
    {
        ok = false;
        goto no_rsa;
    }
    pk_orig = EVP_PKEY_new();
    if (pk_orig == NULL)
    {
        ok = false;
        goto no_pkey;
    }
    if (EVP_PKEY_set1_RSA(pk_orig, rsa) != 1)
    {
        ok = false;
        goto set_rsa_fail;
    }
    result = globus_gsi_proxy_handle_set_private_key(handle, pk_orig);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_private_key(handle, &pk);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (pk == pk_orig)
    {
        ok = false;
    }

    if (EVP_PKEY_cmp(pk, pk_orig) != 1)
    {
        ok = false;
    }

    EVP_PKEY_free(pk);
set_rsa_fail:
    EVP_PKEY_free(pk_orig);
no_pkey:
    RSA_free(rsa);
no_rsa:
    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_set_type_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;

    result = globus_gsi_proxy_handle_set_type(NULL, 0);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }

    return ok;
}

static
bool
proxy_handle_get_type_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_cert_utils_cert_type_t   type = 0;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;

    result = globus_gsi_proxy_handle_get_type(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }

    result = globus_gsi_proxy_handle_get_type(NULL, &type);
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

    result = globus_gsi_proxy_handle_get_type(handle, NULL);
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
proxy_handle_set_get_type_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_cert_utils_cert_type_t   type = 0;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    globus_gsi_cert_utils_cert_type_t   types[] =
    {
        GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_IMPERSONATION_PROXY,
        GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_INDEPENDENT_PROXY,
        GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_LIMITED_PROXY,
        GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_RESTRICTED_PROXY,
        GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2_PROXY,
        GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2_LIMITED_PROXY,
        GLOBUS_GSI_CERT_UTILS_TYPE_RFC_IMPERSONATION_PROXY,
        GLOBUS_GSI_CERT_UTILS_TYPE_RFC_INDEPENDENT_PROXY,
        GLOBUS_GSI_CERT_UTILS_TYPE_RFC_LIMITED_PROXY,
        GLOBUS_GSI_CERT_UTILS_TYPE_RFC_RESTRICTED_PROXY,
    };
    size_t                              num_types = sizeof(types)/sizeof(*types);

    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }

    result = globus_gsi_proxy_handle_get_type(handle, &type);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (type != GLOBUS_GSI_CERT_UTILS_TYPE_RFC_IMPERSONATION_PROXY)
    {
        ok = false;
    }

    for (size_t i = 0; i < num_types; i++)
    {
        result = globus_gsi_proxy_handle_set_type(handle, types[i]);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
        }
        result = globus_gsi_proxy_handle_get_type(handle, &type);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
        }
        if (type != types[i])
        {
            ok = false;
        }
    }

    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_set_time_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;

    result = globus_gsi_proxy_handle_set_time_valid(NULL, 0);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }

    return ok;
}

static
bool
proxy_handle_get_time_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 time_valid = 0;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;

    result = globus_gsi_proxy_handle_get_time_valid(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }

    result = globus_gsi_proxy_handle_get_time_valid(NULL, &time_valid);
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

    result = globus_gsi_proxy_handle_get_time_valid(handle, NULL);
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
proxy_handle_set_get_time_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 time_valid = 0;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    globus_gsi_cert_utils_cert_type_t   time_valids[] =
    {
        -1,
        0,
        1,
        time(NULL),
        INT_MAX,
    };
    size_t                              num_times = sizeof(time_valids)/sizeof(*time_valids);

    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }

    result = globus_gsi_proxy_handle_get_time_valid(handle, &time_valid);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (time_valid != 0)
    {
        ok = false;
    }

    for (size_t i = 0; i < num_times; i++)
    {
        result = globus_gsi_proxy_handle_set_time_valid(handle, time_valids[i]);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
        }
        result = globus_gsi_proxy_handle_get_time_valid(handle, &time_valid);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
        }
        if (time_valid != time_valids[i])
        {
            ok = false;
        }
    }

    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_set_policy_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;

    result = globus_gsi_proxy_handle_set_policy(NULL, NULL, 0, 0);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }

    return ok;
}

static
bool
proxy_handle_get_policy_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    unsigned char                      *policy_data = NULL;
    int                                 policy_len = 0;
    int                                 policy_nid = 0;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;

    result = globus_gsi_proxy_handle_get_policy(NULL, NULL, NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_policy(NULL, &policy_data, NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_policy(NULL, NULL, &policy_len, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_policy(NULL, NULL, NULL, &policy_nid);
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

    result = globus_gsi_proxy_handle_get_policy(handle, &policy_data, NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_policy(handle, NULL, &policy_len, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_policy(handle, NULL, NULL, &policy_nid);
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
proxy_handle_set_get_policy_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    unsigned char                      *policy_data = NULL;
    int                                 policy_len = 0;
    int                                 policy_nid = 0;
    bool                                ok = true;
    unsigned char                       hello_policy[] = "hello";
    globus_gsi_proxy_handle_t           handle = NULL;

    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }

    result = globus_gsi_proxy_handle_get_policy(handle, &policy_data, &policy_len, &policy_nid);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (policy_len != 0)
    {
        ok = false;
    }
    if (policy_nid != NID_id_ppl_inheritAll)
    {
        ok = false;
    }
    free(policy_data);
    policy_data = NULL;

    result = globus_gsi_proxy_handle_set_policy(handle, NULL, 0, NID_id_ppl_inheritAll);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }

    result = globus_gsi_proxy_handle_get_policy(handle, &policy_data, &policy_len, &policy_nid);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (policy_len != 0)
    {
        ok = false;
    }
    if (policy_nid != NID_id_ppl_inheritAll)
    {
        ok = false;
    }
    free(policy_data);
    policy_data = NULL;

    result = globus_gsi_proxy_handle_set_policy(handle, hello_policy, sizeof(hello_policy)-1, test_policy_nid);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }

    result = globus_gsi_proxy_handle_get_policy(handle, &policy_data, &policy_len, &policy_nid);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (policy_len != sizeof(hello_policy)-1)
    {
        ok = false;
    }
    if (policy_nid != test_policy_nid)
    {
        ok = false;
    }
    if (memcmp(policy_data, hello_policy, sizeof(hello_policy)-1) != 0)
    {
        ok = false;
    }
    free(policy_data);
    policy_data = NULL;

    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_add_extension_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_proxy_handle_t           handle = NULL;
    bool                                ok = true;

    result = globus_gsi_proxy_handle_add_extension(NULL, NULL);
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
    result = globus_gsi_proxy_handle_add_extension(handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_get_extensions_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_proxy_handle_t           handle = NULL;
    bool                                ok = true;
    STACK_OF(X509_EXTENSION)           *extensions = NULL;

    result = globus_gsi_proxy_handle_get_extensions(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_extensions(NULL, &extensions);
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
    result = globus_gsi_proxy_handle_get_extensions(handle, NULL);
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
proxy_handle_set_extensions_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_proxy_handle_t           handle = NULL;
    bool                                ok = true;
    STACK_OF(X509_EXTENSION)           *extensions = NULL;

    result = globus_gsi_proxy_handle_set_extensions(NULL, NULL);
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
    result = globus_gsi_proxy_handle_set_extensions(handle, extensions);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_add_extension_test(void)
{
    const char                         *example_name = "example.globus.org";
    ASN1_IA5STRING                     *dns_name_string = NULL;
    GENERAL_NAME                       *dns_name = NULL;
    GENERAL_NAMES                      *subject_alt_names = NULL;
    X509_EXTENSION                     *ext_orig = NULL;
    STACK_OF(X509_EXTENSION)           *ext = NULL;
    bool                                ok = true;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_proxy_handle_t           handle = NULL;


    dns_name_string = ASN1_IA5STRING_new();
    if (dns_name_string == NULL)
    {
        ok = false;
        goto no_dns_name_string;
    }
    ASN1_STRING_set(dns_name_string, example_name, strlen(example_name));

    dns_name = GENERAL_NAME_new();
    if (dns_name == NULL)
    {
        ok = false;
        goto no_dns_name;
    }
    GENERAL_NAME_set0_value(dns_name, GEN_DNS, dns_name_string);
    dns_name_string = NULL;

    subject_alt_names = GENERAL_NAMES_new();
    if (subject_alt_names == NULL)
    {
        ok = false;
        goto no_subject_alt_names;
    }
    sk_GENERAL_NAME_push(subject_alt_names, ASN1_dup_of(GENERAL_NAME, i2d_GENERAL_NAME, d2i_GENERAL_NAME, dns_name));

    ext_orig = X509V3_EXT_i2d(
            NID_subject_alt_name, 1, subject_alt_names);
    if (ext_orig == NULL)
    {
        ok = false;
        goto no_ext_orig;
    }
    
    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }
    result = globus_gsi_proxy_handle_add_extension(handle, ext_orig);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_extensions(handle, &ext);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (sk_X509_EXTENSION_num(ext) != 1)
    {
        ok = false;
    }
    if (ext_orig == sk_X509_EXTENSION_value(ext, 0))
    {
        ok = false;
    }
    if (X509_EXTENSION_cmp(ext_orig, sk_X509_EXTENSION_value(ext, 0)) != 1)
    {
        ok = false;
    }

    sk_X509_EXTENSION_pop_free(ext, X509_EXTENSION_free);

    ASN1_IA5STRING_free(dns_name_string);
no_dns_name_string:
    GENERAL_NAME_free(dns_name);
no_dns_name:
    sk_GENERAL_NAME_pop_free(subject_alt_names, GENERAL_NAME_free);
no_subject_alt_names:
    X509_EXTENSION_free(ext_orig);
no_ext_orig:
    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}


static
bool
proxy_handle_set_get_extensions_test(void)
{
    const char                         *example_name = "example.globus.org";
    ASN1_IA5STRING                     *dns_name_string = NULL;
    GENERAL_NAME                       *dns_name = NULL;
    GENERAL_NAMES                      *subject_alt_names = NULL;
    STACK_OF(X509_EXTENSION)           *ext_orig = NULL;
    STACK_OF(X509_EXTENSION)           *ext = NULL;
    bool                                ok = true;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_proxy_handle_t           handle = NULL;


    dns_name_string = ASN1_IA5STRING_new();
    if (dns_name_string == NULL)
    {
        ok = false;
        goto no_dns_name_string;
    }
    ASN1_STRING_set(dns_name_string, example_name, strlen(example_name));

    dns_name = GENERAL_NAME_new();
    if (dns_name == NULL)
    {
        ok = false;
        goto no_dns_name;
    }
    GENERAL_NAME_set0_value(dns_name, GEN_DNS, dns_name_string);
    dns_name_string = NULL;

    subject_alt_names = GENERAL_NAMES_new();
    if (subject_alt_names == NULL)
    {
        ok = false;
        goto no_subject_alt_names;
    }
    sk_GENERAL_NAME_push(subject_alt_names, ASN1_dup_of(GENERAL_NAME, i2d_GENERAL_NAME, d2i_GENERAL_NAME, dns_name));

    ext_orig = sk_X509_EXTENSION_new_null();
    if (ext_orig == NULL)
    {
        ok = false;
        goto no_ext_orig;
    }

    sk_X509_EXTENSION_push(ext_orig, X509V3_EXT_i2d(
            NID_subject_alt_name, 1, subject_alt_names));
    
    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }
    result = globus_gsi_proxy_handle_set_extensions(handle, ext_orig);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_extensions(handle, &ext);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (sk_X509_EXTENSION_num(ext) != 1)
    {
        ok = false;
    }
    if (sk_X509_EXTENSION_value(ext_orig, 0) == sk_X509_EXTENSION_value(ext, 0))
    {
        ok = false;
    }
    if (X509_EXTENSION_cmp(sk_X509_EXTENSION_value(ext_orig, 0), sk_X509_EXTENSION_value(ext, 0)) != 1)
    {
        ok = false;
    }

    sk_X509_EXTENSION_pop_free(ext, X509_EXTENSION_free);

    ASN1_IA5STRING_free(dns_name_string);
no_dns_name_string:
    GENERAL_NAME_free(dns_name);
no_dns_name:
    sk_GENERAL_NAME_pop_free(subject_alt_names, GENERAL_NAME_free);
no_subject_alt_names:
    sk_X509_EXTENSION_pop_free(ext_orig, X509_EXTENSION_free);
no_ext_orig:
    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_set_pathlen_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;

    result = globus_gsi_proxy_handle_set_pathlen(NULL, 0);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_set_pathlen(NULL, 1);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    return ok;
}

static
bool
proxy_handle_get_pathlen_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_proxy_handle_t           handle = NULL;
    bool                                ok = true;
    int                                 pathlen = 0;

    result = globus_gsi_proxy_handle_get_pathlen(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_pathlen(NULL, &pathlen);
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
    result = globus_gsi_proxy_handle_get_pathlen(handle, NULL);
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
proxy_handle_set_get_pathlen_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_proxy_handle_t           handle = NULL;
    bool                                ok = true;
    int                                 pathlen = 0;
    int                                 pathlens[] = 
    {
        0,
        1,
        10
    };
    int                                 num_tests = sizeof(pathlens)/sizeof(*pathlens);

    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }
    result = globus_gsi_proxy_handle_get_pathlen(handle, &pathlen);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (pathlen != -1)
    {
        ok = false;
    }

    for (size_t i = 0; i < num_tests; i++)
    {
        result = globus_gsi_proxy_handle_set_pathlen(handle, pathlens[i]);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
        }
        result = globus_gsi_proxy_handle_get_pathlen(handle, &pathlen);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
        }
        if (pathlen != pathlens[i])
        {
            ok = false;
        }
    }
    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_clear_cert_info_null_test(void)
{
    bool                                ok = true;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_clear_cert_info(NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    return ok;
}

static
bool
proxy_handle_clear_cert_info_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    unsigned char                      *policy_data = NULL;
    int                                 policy_len = 0;
    int                                 policy_nid = 0;
    bool                                ok = true;
    unsigned char                       hello_policy[] = "hello";
    globus_gsi_proxy_handle_t           handle = NULL;

    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }

    result = globus_gsi_proxy_handle_set_policy(handle, hello_policy, sizeof(hello_policy)-1, test_policy_nid);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }

    result = globus_gsi_proxy_handle_clear_cert_info(handle);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }

    result = globus_gsi_proxy_handle_get_policy(handle, &policy_data, &policy_len, &policy_nid);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (policy_len != 0)
    {
        ok = false;
    }
    if (policy_nid != NID_id_ppl_inheritAll)
    {
        ok = false;
    }
    free(policy_data);
    policy_data = NULL;

    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_set_proxy_cert_info_null_test(void)
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
proxy_handle_get_proxy_cert_info_null_test(void)
{
    bool                                ok = true;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_proxy_handle_t           handle = NULL;
    PROXY_CERT_INFO_EXTENSION          *pci = NULL;

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
proxy_handle_set_get_proxy_cert_info_test(void)
{
    bool                                ok = true;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_proxy_handle_t           handle = NULL;
    PROXY_CERT_INFO_EXTENSION          *pci = NULL;
    PROXY_CERT_INFO_EXTENSION          *new_pci = NULL;
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
    if (OBJ_obj2nid(pci->proxyPolicy->policyLanguage) != NID_id_ppl_inheritAll)
    {
        ok = false;
    }
    ASN1_OBJECT_free(pci->proxyPolicy->policyLanguage);
    pci->proxyPolicy->policyLanguage = OBJ_nid2obj(test_policy_nid);
    if (pci->proxyPolicy->policy == NULL)
    {
        pci->proxyPolicy->policy = ASN1_OCTET_STRING_new();
    }
    ASN1_OCTET_STRING_set(pci->proxyPolicy->policy, hello_policy, sizeof(hello_policy)-1);

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
    if (OBJ_obj2nid(pci->proxyPolicy->policyLanguage) != test_policy_nid)
    {
        ok = false;
    }
    PROXY_CERT_INFO_EXTENSION_free(pci);
    PROXY_CERT_INFO_EXTENSION_free(new_pci);
    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_set_common_name_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;

    result = globus_gsi_proxy_handle_set_common_name(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_set_common_name(NULL, "name");
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
    result = globus_gsi_proxy_handle_set_common_name(handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }

    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_get_common_name_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    char                               *name = NULL;

    result = globus_gsi_proxy_handle_get_common_name(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_common_name(NULL, &name);
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
    result = globus_gsi_proxy_handle_get_common_name(handle, NULL);
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
proxy_handle_set_get_common_name_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    char                               *orig_name = "me";
    char                               *name = NULL;

    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }
    result = globus_gsi_proxy_handle_get_common_name(handle, &name);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (name != NULL)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_set_common_name(handle, orig_name);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_common_name(handle, &name);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (name == NULL)
    {
        ok = false;
    }
    if (name == orig_name)
    {
        ok = false;
    }
    if (strcmp(name, orig_name) != 0)
    {
        ok = false;
    }

    free(name);
    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_set_is_limited_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;

    result = globus_gsi_proxy_handle_set_is_limited(NULL, GLOBUS_TRUE);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_set_is_limited(NULL, GLOBUS_FALSE);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }

    globus_gsi_proxy_handle_destroy(handle);
    return ok;
}

static
bool
proxy_handle_is_limited_null_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    globus_bool_t                       is_limited = false;

    result = globus_gsi_proxy_is_limited(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_is_limited(NULL, &is_limited);
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
    result = globus_gsi_proxy_is_limited(handle, NULL);
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
proxy_handle_set_is_limited_test(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    globus_bool_t                       is_limited = false;

    result = globus_gsi_proxy_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }
    result = globus_gsi_proxy_handle_set_is_limited(handle, GLOBUS_FALSE);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_is_limited(handle, &is_limited);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (is_limited)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_set_is_limited(handle, GLOBUS_TRUE);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_is_limited(handle, &is_limited);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (!is_limited)
    {
        ok = false;
    }

    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static
bool
proxy_handle_get_signing_algorithm_null_test(void)
{
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    const EVP_MD                       *algorithm = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_get_signing_algorithm(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_signing_algorithm(NULL, &algorithm);
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
    result = globus_gsi_proxy_handle_get_signing_algorithm(handle, NULL);
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
proxy_handle_get_signing_algorithm_test(void)
{
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    globus_gsi_proxy_handle_attrs_t     attr = NULL;
    const EVP_MD                       *algorithm = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_attrs_init(&attr);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_attr;
    }
    result = globus_gsi_proxy_handle_attrs_set_signing_algorithm(attr, EVP_md5());
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_attr;
    }
    result = globus_gsi_proxy_handle_init(&handle, attr);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_handle;
    }
    result = globus_gsi_proxy_handle_get_signing_algorithm(handle, &algorithm);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
    }
    if (algorithm != EVP_md5())
    {
        ok = false;
    }

    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    globus_gsi_proxy_handle_attrs_destroy(attr);
no_attr:
    return ok;
}

static
bool
proxy_handle_get_keybits_null_test(void)
{
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    int                                 keybits = 0;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_get_keybits(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_keybits(NULL, &keybits);
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
    result = globus_gsi_proxy_handle_get_keybits(handle, NULL);
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
proxy_handle_get_keybits_test(void)
{
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    globus_gsi_proxy_handle_attrs_t     attr = NULL;
    int                                 test_keybits[] =
    {
        1024,
        2048,
        4096,
    };
    size_t                              num_tests = sizeof(test_keybits)/sizeof(*test_keybits);
    int                                 keybits = 0;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_attrs_init(&attr);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_attr;
    }
    for (size_t i = 0; i < num_tests; i++)
    {
        globus_gsi_proxy_handle_attrs_set_keybits(attr, test_keybits[i]);

        result = globus_gsi_proxy_handle_init(&handle, attr);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
            goto no_handle;
        }
        result = globus_gsi_proxy_handle_get_keybits(handle, &keybits);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
        }
        if (keybits != test_keybits[i])
        {
            ok = false;
        }

        globus_gsi_proxy_handle_destroy(handle);
    }
no_handle:
    globus_gsi_proxy_handle_attrs_destroy(attr);
no_attr:
    return ok;
}

static
bool
proxy_handle_get_init_prime_null_test(void)
{
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    int                                 init_prime = 0;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_get_init_prime(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_init_prime(NULL, &init_prime);
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
    result = globus_gsi_proxy_handle_get_init_prime(handle, NULL);
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
proxy_handle_get_init_prime_test(void)
{
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    globus_gsi_proxy_handle_attrs_t     attr = NULL;
    int                                 test_init_prime[] =
    {
        RSA_3,
        RSA_F4,
    };
    size_t                              num_tests = sizeof(test_init_prime)/sizeof(*test_init_prime);
    int                                 init_prime = 0;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_attrs_init(&attr);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_attr;
    }
    for (size_t i = 0; i < num_tests; i++)
    {
        globus_gsi_proxy_handle_attrs_set_init_prime(attr, test_init_prime[i]);

        result = globus_gsi_proxy_handle_init(&handle, attr);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
            goto no_handle;
        }
        result = globus_gsi_proxy_handle_get_init_prime(handle, &init_prime);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
        }
        if (init_prime != test_init_prime[i])
        {
            ok = false;
        }

        globus_gsi_proxy_handle_destroy(handle);
    }
no_handle:
    globus_gsi_proxy_handle_attrs_destroy(attr);
no_attr:
    return ok;
}

static
bool
proxy_handle_get_clock_skew_allowable_null_test(void)
{
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    int                                 clock_skew_allowable = 0;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_get_clock_skew_allowable(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_clock_skew_allowable(NULL, &clock_skew_allowable);
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
    result = globus_gsi_proxy_handle_get_clock_skew_allowable(handle, NULL);
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
proxy_handle_get_clock_skew_allowable_test(void)
{
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    globus_gsi_proxy_handle_attrs_t     attr = NULL;
    int                                 test_skew[] =
    {
        60,
        5*60,
    };
    size_t                              num_tests = sizeof(test_skew)/sizeof(*test_skew);
    int                                 skew = 0;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_attrs_init(&attr);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_attr;
    }
    for (size_t i = 0; i < num_tests; i++)
    {
        globus_gsi_proxy_handle_attrs_set_clock_skew_allowable(attr, test_skew[i]);

        result = globus_gsi_proxy_handle_init(&handle, attr);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
            goto no_handle;
        }
        result = globus_gsi_proxy_handle_get_clock_skew_allowable(handle, &skew);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
        }
        if (skew != test_skew[i])
        {
            ok = false;
        }

        globus_gsi_proxy_handle_destroy(handle);
    }
no_handle:
    globus_gsi_proxy_handle_attrs_destroy(attr);
no_attr:
    return ok;
}

static
bool
proxy_handle_get_key_get_callback_null_test(void)
{
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    void                                (*callback)(int, int, void *) = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_get_key_gen_callback(NULL, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }
    result = globus_gsi_proxy_handle_get_key_gen_callback(NULL, &callback);
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
    result = globus_gsi_proxy_handle_get_key_gen_callback(handle, NULL);
    if (result == GLOBUS_SUCCESS)
    {
        ok = false;
    }

    globus_gsi_proxy_handle_destroy(handle);
no_handle:
    return ok;
}

static void test_key_gen_callback(int a, int b, void *c) {;}

static
bool
proxy_handle_get_key_get_callback_test(void)
{
    bool                                ok = true;
    globus_gsi_proxy_handle_t           handle = NULL;
    globus_gsi_proxy_handle_attrs_t     attr = NULL;
    void                                (*callbacks[])(int, int, void *) =
    {
        NULL,
        test_key_gen_callback,
    };
    void                                (*callback)(int, int, void *) = NULL;
    size_t                              num_tests = sizeof(callbacks)/sizeof(callbacks[0]);
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_gsi_proxy_handle_attrs_init(&attr);
    if (result != GLOBUS_SUCCESS)
    {
        ok = false;
        goto no_attr;
    }
    for (size_t i = 0; i < num_tests; i++)
    {
        result = globus_gsi_proxy_handle_attrs_set_key_gen_callback(
                attr,
                callbacks[i]);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
            break;
        }
        result = globus_gsi_proxy_handle_init(&handle, attr);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
            break;
        }
        result = globus_gsi_proxy_handle_get_key_gen_callback(handle, &callback);
        if (result != GLOBUS_SUCCESS)
        {
            ok = false;
        }
        if (callback != callbacks[i])
        {
            ok = false;
        }
        globus_gsi_proxy_handle_destroy(handle);
    }
    globus_gsi_proxy_handle_attrs_destroy(attr);
no_attr:
    return ok;
}

int
main(int argc, char *argv[])
{
    struct test_case                    test_cases[] =
    {
        TEST_CASE_INITIALIZER(proxy_handle_init_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_destroy_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_init_destroy_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_req_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_req_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_get_req_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_private_key_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_private_key_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_get_private_key_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_type_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_type_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_get_type_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_time_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_time_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_get_time_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_policy_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_policy_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_get_policy_test),
        TEST_CASE_INITIALIZER(proxy_handle_add_extension_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_extensions_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_extensions_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_add_extension_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_get_extensions_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_pathlen_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_pathlen_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_get_pathlen_test),
        TEST_CASE_INITIALIZER(proxy_handle_clear_cert_info_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_clear_cert_info_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_proxy_cert_info_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_proxy_cert_info_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_get_proxy_cert_info_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_common_name_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_common_name_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_get_common_name_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_is_limited_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_is_limited_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_set_is_limited_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_signing_algorithm_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_signing_algorithm_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_keybits_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_keybits_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_init_prime_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_init_prime_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_clock_skew_allowable_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_clock_skew_allowable_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_key_get_callback_null_test),
        TEST_CASE_INITIALIZER(proxy_handle_get_key_get_callback_test),
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
