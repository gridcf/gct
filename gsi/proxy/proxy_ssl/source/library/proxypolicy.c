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


#include <stdio.h>
#include <openssl/err.h>
#include <openssl/asn1t.h>
#include <openssl/objects.h>

#include "proxypolicy.h"

ASN1_SEQUENCE(PROXYPOLICY) =
{
    ASN1_SIMPLE(PROXYPOLICY, policy_language, ASN1_OBJECT),
    ASN1_OPT(PROXYPOLICY, policy, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(PROXYPOLICY)

IMPLEMENT_ASN1_FUNCTIONS(PROXYPOLICY)
IMPLEMENT_ASN1_DUP_FUNCTION(PROXYPOLICY)

#if OPENSSL_VERSION_NUMBER < 0x10000000L
/**
 * @ingroup proxypolicy
 *
 * Creates an ASN1_METHOD structure, which contains
 * pointers to routines that convert any PROXYPOLICY
 * structure to its associated ASN.1 DER encoded form
 * and vice-versa.
 *
 * @return the ASN1_METHOD object
 */

ASN1_METHOD * PROXYPOLICY_asn1_meth()
{
    static ASN1_METHOD proxypolicy_asn1_meth =
    {
        (i2d_of_void *) i2d_PROXYPOLICY,
        (d2i_of_void *) d2i_PROXYPOLICY,
        (void * (*)(void)) PROXYPOLICY_new,
        (void (*)(void *)) PROXYPOLICY_free
    };
    return (&proxypolicy_asn1_meth);
}
/* PROXYPOLICY_asn1_meth() */
#endif

/**
 * @ingroup proxypolicy
 *
 * Compares two PROXYPOLICY structs for equality
 * This function first compares the policy language numeric
 * id's, if they're equal, it then compares the two policies.
 *
 * @return 0 if equal, nonzero if not
 */
int PROXYPOLICY_cmp(
    const PROXYPOLICY *                 a,
    const PROXYPOLICY *                 b)
{

    if((OBJ_obj2nid(a->policy_language) != OBJ_obj2nid(b->policy_language)) ||
       ASN1_STRING_cmp((ASN1_STRING *)a->policy, (ASN1_STRING *)b->policy))
    {
        return 1;
    }
    return 0;
}


/**
 * @ingroup proxypolicy
 *
 * Prints the PROXYPOLICY struct using the BIO stream
 *
 * @param bp the BIO stream to print to
 * @param policy the PROXYPOLICY to print
 *
 * @return 1 on success, 0 on error
 */
int PROXYPOLICY_print(
    BIO *                               bp,
    PROXYPOLICY *                       policy)
{
    STACK_OF(CONF_VALUE) *              values = NULL;

    values = i2v_PROXYPOLICY(PROXYPOLICY_x509v3_ext_meth(),
                             policy,
                             values);

    X509V3_EXT_val_prn(bp, values, 0, 1);

    sk_CONF_VALUE_pop_free(values, X509V3_conf_free);
    return 1;
}


/**
 * @ingroup proxypolicy
 *
 * Prints the PROXYPOLICY to the FILE * stream
 *
 * @param fp the FILE* stream to print to
 * @param policy the PROXYPOLICY to print
 *
 * @return number of bytes printed, -2 or -1 on error
 */
int PROXYPOLICY_print_fp(
    FILE *                              fp,
    PROXYPOLICY *                       policy)
{
    int                                 ret;

    BIO * bp = BIO_new(BIO_s_file());
    BIO_set_fp(bp, fp, BIO_NOCLOSE);
    ret = PROXYPOLICY_print(bp, policy);
    BIO_free(bp);

    return (ret);
}


/**
 * @ingroup proxypolicy
 *
 * Sets the policy language of the PROXYPOLICY
 *
 * @param policy the PROXYPOLICY to set the policy language of
 * @param policy_language the policy language to set it to
 *
 * @return 1 on success, 0 on error
 */
int PROXYPOLICY_set_policy_language(
    PROXYPOLICY *                       policy,
    ASN1_OBJECT *                       policy_language)
{
    if(policy_language != NULL)
    {
        ASN1_OBJECT_free(policy->policy_language);
        policy->policy_language = OBJ_dup(policy_language);
        return 1;
    }
    return 0;
}

/**
 * @ingroup proxypolicy
 *
 * Gets the policy language of the PROXYPOLICY
 *
 * @param policy the proxy policy to get the policy language
 * of
 *
 * @return the policy language as an ASN1_OBJECT
 */
ASN1_OBJECT * PROXYPOLICY_get_policy_language(
    PROXYPOLICY *                       policy)
{
    return policy->policy_language;
}

/**
 * @ingroup proxypolicy
 *
 * Sets the policy of the PROXYPOLICY
 *
 * @param proxypolicy the proxy policy to set the policy of
 * @param policy the policy to set it to
 * @param length the length of the policy
 *
 * @return 1 on success, 0 on error
 */
int PROXYPOLICY_set_policy(
    PROXYPOLICY *                       proxypolicy,
    unsigned char *                     policy,
    int                                 length)
{
    if(policy != NULL)
    {
        unsigned char *                 copy = malloc(length);
        memcpy(copy, policy, length);

        if(!proxypolicy->policy)
        {
            proxypolicy->policy = ASN1_OCTET_STRING_new();
        }

        ASN1_OCTET_STRING_set(proxypolicy->policy, copy, length);

    }
    else
    {
        if(proxypolicy->policy)
        {
            ASN1_OCTET_STRING_free(proxypolicy->policy);
        }
    }

    return 1;
}


/**
 * @ingroup proxypolicy
 *
 * Gets the policy of a PROXYPOLICY
 *
 * @param policy the PROXYPOLICY to get the policy of
 * @param length the length of the returned policy - this value
 *        gets set by this function
 *
 * @return the policy
 */
unsigned char * PROXYPOLICY_get_policy(
    PROXYPOLICY *                       policy,
    int *                               length)
{
    if(policy->policy)
    {
        (*length) = ASN1_STRING_length(policy->policy);
        if(*length > 0 && ASN1_STRING_get0_data(policy->policy))
        {
            unsigned char *                 copy = malloc(*length);
            memcpy(copy, ASN1_STRING_get0_data(policy->policy), *length);
            return copy;
        }
    }

    return NULL;
}


X509V3_EXT_METHOD * PROXYPOLICY_x509v3_ext_meth()
{
    static X509V3_EXT_METHOD proxypolicy_x509v3_ext_meth =
    {
        -1,
        X509V3_EXT_MULTILINE,
        NULL,
        (X509V3_EXT_NEW) PROXYPOLICY_new,
        (X509V3_EXT_FREE) PROXYPOLICY_free,
        (X509V3_EXT_D2I) d2i_PROXYPOLICY,
        (X509V3_EXT_I2D) i2d_PROXYPOLICY,
        NULL, NULL,
        (X509V3_EXT_I2V) i2v_PROXYPOLICY,
        NULL,
        NULL, NULL,
        NULL
    };
    return (&proxypolicy_x509v3_ext_meth);
}

STACK_OF(CONF_VALUE) * i2v_PROXYPOLICY(
    struct v3_ext_method *              method,
    PROXYPOLICY *                       ext,
    STACK_OF(CONF_VALUE) *              extlist)
{
    unsigned char *                     policy = NULL;
    char                                policy_lang[128];
    unsigned char *                     tmp_string = NULL;
    unsigned char *                     index = NULL;
    int                                 nid;
    int                                 policy_length;

    X509V3_add_value("Proxy Policy:", NULL, &extlist);

    nid = OBJ_obj2nid(PROXYPOLICY_get_policy_language(ext));

    if(nid != NID_undef)
    {
        BIO_snprintf(policy_lang, 128, " %s", OBJ_nid2ln(nid));
    }
    else
    {
        policy_lang[0] = ' ';
        i2t_ASN1_OBJECT(&policy_lang[1],
                        127,
                        PROXYPOLICY_get_policy_language(ext));
    }

    X509V3_add_value("    Policy Language",
                     policy_lang,
                     &extlist);

    policy = PROXYPOLICY_get_policy(ext, &policy_length);

    if(!policy)
    {
        X509V3_add_value("    Policy", " EMPTY", &extlist);
    }
    else
    {
        X509V3_add_value("    Policy:", NULL, &extlist);

        tmp_string = policy;
        while (policy_length > 0)
        {
            int                         policy_line_length;

            index = memchr(tmp_string, '\n', (size_t) policy_length);

            /* Weird to indent the last line only... */
            if (!index)
            {
                char *                  last_string;

                policy_line_length = policy_length;

                last_string = malloc(policy_line_length + 9);
                BIO_snprintf(
                        last_string,
                        (size_t) (policy_line_length +9),
                        "%8s%.*s", "",
                        policy_line_length,
                        (char *) tmp_string);
                X509V3_add_value(NULL, last_string, &extlist);
                free(last_string);
            }
            else
            {
                *(index++) = '\0';
                policy_line_length = index - tmp_string;

                X509V3_add_value(NULL, (char *) tmp_string, &extlist);

                tmp_string = index;
            }
            policy_length -= policy_line_length;
        }

        free(policy);
    }

    return extlist;
}
