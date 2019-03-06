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

#include "globus_common.h"
#include "globus_gsi_system_config.h"
#include "gssapi.h"
#include "globus_gss_assist.h"
#include "globus_gsi_credential.h"
#include "globus_gridmap_callout_error.h"

#include <stdlib.h>
#include <openssl/ssl.h>
#include <stdio.h>

#if OPENSSL_VERSION_NUMBER < 0x0090801fL
#define GT_D2I_ARG_CAST (unsigned char **)
#else
#define GT_D2I_ARG_CAST
#endif

#if OPENSSL_VERSION_NUMBER < 0x0090801fL
#define GT_ASN1_GET_OBJECT_CAST 
#else
#define GT_ASN1_GET_OBJECT_CAST (const unsigned char **)
#endif


/* 1.2.3.4.4.3.2.1.7.8 */
static const gss_OID_desc               ggvm_cert_chain_oid =
    {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x01\x08"}; 

/* 1.3.6.1.4.1.5923.1.1.1.6 */
static const char*                      ggvm_eppn_oid =
    "1.3.6.1.4.1.5923.1.1.1.6";

static
globus_result_t
ggvm_load_cert_from_file(
    char *                              certfile,
    X509 **                             out_cert)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    FILE *                              certfp = NULL;
    X509 *                              cert;

    certfp = fopen(certfile, "r");
    if(!certfp)
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("error opening file."));
        goto err;
    }

    cert = PEM_read_X509(certfp, 0, 0, 0);
    if(!cert)
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("file does not contain a valid PEM certificate."));
        goto err;
    }

    *out_cert = cert;

err:
    if(certfp)
    {
        fclose(certfp);
    }

    return result;
}

static
globus_result_t
ggvm_extract_cert_from_chain(
    gss_ctx_id_t                        context,
    X509 **                             out_cert)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    gss_buffer_set_t                    cert_chain_buffers = 0;
    X509 *                              cert = NULL;
    const unsigned char *               ptr;
    int                                 cert_index = 0;

    major_status = gss_inquire_sec_context_by_oid(
        &minor_status,
        context,
        (gss_OID) &ggvm_cert_chain_oid,
        &cert_chain_buffers);

    if(GSS_ERROR(major_status))
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("can't extract cert chain"));
        goto err;
    }

    while(!cert && cert_index < cert_chain_buffers->count)
    {
        globus_gsi_cert_utils_cert_type_t   cert_type;
        
        ptr = cert_chain_buffers->elements[cert_index].value;
        cert = d2i_X509(
            NULL,
            GT_D2I_ARG_CAST &ptr,
            cert_chain_buffers->elements[cert_index].length);
        if(cert == NULL)
        {
            GLOBUS_GRIDMAP_CALLOUT_ERROR(
                result,
                GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
                ("error reading cert chain"));
            goto err;
        }

        result = globus_gsi_cert_utils_get_cert_type(cert, &cert_type);
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GRIDMAP_CALLOUT_ERROR(
                result,
                GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
                ("error searching for EEC in cert chain"));
            goto err;
        }

        if(cert_type != GLOBUS_GSI_CERT_UTILS_TYPE_EEC)
        {
            X509_free(cert);
            cert = NULL;
            cert_index++;
        }
    }

    if(!cert)
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("no EEC found in cert chain"));
        goto err;
    }

    *out_cert = cert;

err:
    if(cert_chain_buffers)
    {
        gss_release_buffer_set(&minor_status, &cert_chain_buffers);
    }

    return result;
}


static
globus_result_t 
ggvm_verify_cert(
    X509 *                              cert,
    X509 *                              ca_cert,
    time_t                              shared_exp)
{
    X509_STORE *                        ca_store = NULL;
    X509_STORE_CTX *                    cert_ctx = NULL;
    int                                 rc;
    globus_result_t                     result = GLOBUS_FAILURE;

    ca_store = X509_STORE_new();
    if(ca_store == NULL)
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("store allocation failed."));
        goto err;
    }

    rc = X509_STORE_add_cert(ca_store, ca_cert);
    if(rc != 1)
    {
        result = globus_error_put(
                globus_error_construct_openssl_error(
                    GLOBUS_GRIDMAP_CALLOUT_ERROR_MODULE, NULL));
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("ca cert add failed."));
        goto err;
    }

    cert_ctx = X509_STORE_CTX_new();
    if(cert_ctx == NULL)
    {
        result = globus_error_put(
                globus_error_construct_openssl_error(
                    GLOBUS_GRIDMAP_CALLOUT_ERROR_MODULE, NULL));
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("conxtext allocation failed."));
        goto err;
    }

    rc = X509_STORE_CTX_init(cert_ctx, ca_store, cert, NULL);
    if(rc != 1)
    {
        result = globus_error_put(
                globus_error_construct_openssl_error(
                    GLOBUS_GRIDMAP_CALLOUT_ERROR_MODULE, NULL));
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("context initialization failed."));
        goto err;
    }
    if(shared_exp > 0)
    {
        X509_STORE_CTX_set_flags(
            cert_ctx, X509_V_FLAG_USE_CHECK_TIME);
        X509_STORE_CTX_set_time(cert_ctx, 0, shared_exp - 1);
    }
    
    rc = X509_verify_cert(cert_ctx);
    if(rc != 1)
    {
        unsigned long err_num = X509_STORE_CTX_get_error(cert_ctx);
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("Couldn't verify credential containing ePPN extension: %s",
            X509_verify_cert_error_string(err_num)));
        goto err;
    }

    X509_STORE_CTX_free(cert_ctx);
    X509_STORE_free(ca_store);

    return GLOBUS_SUCCESS;

err:
    if(cert_ctx)
    {
        X509_STORE_CTX_free(cert_ctx);
    }
    if(ca_store)
    {
        X509_STORE_free(ca_store);
    }

    return result;
}

static
globus_result_t
ggvm_get_eppn(
    X509 *                              cert,
    char **                             data,
    long *                              length)
{
    ASN1_OBJECT *                       eppn_obj;
    int                                 extpos;
    X509_EXTENSION *                    eppn_ext;
    ASN1_STRING *                       eppn_str;
    unsigned char *                     eppn_data;
    int                                 tag;
    int                                 xclass;

    if (!(eppn_obj = OBJ_txt2obj(ggvm_eppn_oid, 0)))
        return GLOBUS_FAILURE;

    if ((extpos = X509_get_ext_by_OBJ(cert, eppn_obj, -1)) < 0)
        return GLOBUS_FAILURE;

    if (!(eppn_ext = X509_get_ext(cert, extpos)))
        return GLOBUS_FAILURE;

    if (!(eppn_str = X509_EXTENSION_get_data(eppn_ext)))
        return GLOBUS_FAILURE;

    eppn_data = eppn_str->data;

    if (ASN1_get_object(GT_ASN1_GET_OBJECT_CAST &eppn_data,
            length, &tag, &xclass, eppn_str->length) == 0x80)
        return GLOBUS_FAILURE;

    *data = (char *) eppn_data;
    *length = eppn_str->length;

    return GLOBUS_SUCCESS;
}

static
globus_result_t
ggvm_get_myproxy_userid(
    gss_ctx_id_t                        context,
    char *                              subject,
    char **                             userid,
    X509 *                              shared_user_cert,
    STACK_OF(X509) *                    shared_user_chain,
    time_t                              shared_exp)
{
    X509 *                              user_cert = NULL;
    X509 *                              myproxy_ca_cert = NULL;
    char *                              myproxy_ca_cert_file = NULL;
    char *                              myproxy_authorized_dn = NULL;
    int                                 authorized_dn_length;
    char *                              eppn;
    long                                length;
    char *                              at;
    globus_result_t                     result = GLOBUS_SUCCESS;

    myproxy_ca_cert_file = getenv("GLOBUS_MYPROXY_CA_CERT");
    if(!myproxy_ca_cert_file)
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("GLOBUS_MYPROXY_CA_CERT not set."));
        goto error;
    }

    myproxy_authorized_dn = getenv("GLOBUS_MYPROXY_AUTHORIZED_DN");
    if(!myproxy_authorized_dn)
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("GLOBUS_MYPROXY_AUTHORIZED_DN not set."));
        goto error;
    }

    /* xinetd does not allow to specify env vars with spaces.
    Use ~ and the callout translates ~ to ' ' */
    at = myproxy_authorized_dn;
    while ((at = strchr(at, '~')) != NULL) {
        *at = ' ';
        ++at;
    }

    /* extract user cert */
    if(shared_user_cert)
    {
        globus_gsi_cert_utils_cert_type_t   cert_type;
        
        result = globus_gsi_cert_utils_get_cert_type(
            shared_user_cert, &cert_type);
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GRIDMAP_CALLOUT_ERROR(
                result,
                GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
                ("error checking shared user cert type"));
            goto error;
        }

        if(cert_type == GLOBUS_GSI_CERT_UTILS_TYPE_EEC)
        {
            user_cert = shared_user_cert;
        }
        else if(shared_user_chain)
        {
            result = globus_gsi_cert_utils_get_eec(
                shared_user_chain, &user_cert);            
        }
        if(result != GLOBUS_SUCCESS || !user_cert)
        {
            GLOBUS_GRIDMAP_CALLOUT_ERROR(
                result,
                GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
                ("EEC not found in shared user cert chain"));
            goto error;
        }
    }
    else
    {
        result = ggvm_extract_cert_from_chain(context, &user_cert);
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GRIDMAP_CALLOUT_ERROR(
                result,
                GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
                ("Could not extract user credentials."));
            goto error;
        }
    }

    /* load ca cert for verify */
    result = ggvm_load_cert_from_file(myproxy_ca_cert_file, &myproxy_ca_cert);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("Unable to load myproxy CA cert."));
        goto error;
    }

    /* verify cert */
    result = ggvm_verify_cert(user_cert, myproxy_ca_cert, shared_exp);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("Cert was not issued by correct CA."));
        goto error;
    }

    /* verify certificate subject */
    authorized_dn_length = strlen(myproxy_authorized_dn);
    if (strlen(subject) <= authorized_dn_length)
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("Cert subject is shorter than GLOBUS_MYPROXY_AUTHORIZED_DN"));
        goto error;
    }

    if(strncmp(subject, myproxy_authorized_dn, authorized_dn_length))
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("Cert subject does not match GLOBUS_MYPROXY_AUTHORIZED_DN"));
        goto error;
    }
    /* get eppn from user certificate*/
    result = ggvm_get_eppn(user_cert, &eppn, &length);
    if (result != GLOBUS_SUCCESS)
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("Could not get eppn from user certificate"));
        goto error;
    }

    if(!getenv("GLOBUS_EPPN_KEEP_DOMAIN"))
    {
        /* get userid from eppn */
        if (!(at = strchr(eppn, '@')))
        {
            GLOBUS_GRIDMAP_CALLOUT_ERROR(
                result,
                GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
                ("Could not find '@' in eppn"));
            goto error;
        }

        *at = 0;
    }
    *userid = globus_libc_strdup(eppn);

error:

    if(myproxy_ca_cert)
    {
        X509_free(myproxy_ca_cert);
    }
    if(user_cert && !shared_user_cert)
    {
        X509_free(user_cert);
    }
    return result;
}


globus_result_t
ggvm_get_subject(
    gss_ctx_id_t                        context,
    char **                             subject)
{
    gss_name_t                          peer;
    gss_buffer_desc                     peer_name_buffer;
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    int                                 initiator;
    globus_result_t                     result = GLOBUS_SUCCESS;

    major_status = gss_inquire_context(&minor_status,
                                       context,
                                       GLOBUS_NULL,
                                       GLOBUS_NULL,
                                       GLOBUS_NULL,
                                       GLOBUS_NULL,
                                       GLOBUS_NULL,
                                       &initiator,
                                       GLOBUS_NULL);

    if(GSS_ERROR(major_status))
    {
        GLOBUS_GRIDMAP_CALLOUT_GSS_ERROR(result, major_status, minor_status);
        goto error;
    }

    major_status = gss_inquire_context(&minor_status,
                                       context,
                                       initiator ? GLOBUS_NULL : &peer,
                                       initiator ? &peer : GLOBUS_NULL,
                                       GLOBUS_NULL,
                                       GLOBUS_NULL,
                                       GLOBUS_NULL,
                                       GLOBUS_NULL,
                                       GLOBUS_NULL);

    if(GSS_ERROR(major_status))
    {
        GLOBUS_GRIDMAP_CALLOUT_GSS_ERROR(result, major_status, minor_status);
        goto error;
    }

    major_status = gss_display_name(&minor_status,
                                    peer,
                                    &peer_name_buffer,
                                    GLOBUS_NULL);

    if(GSS_ERROR(major_status))
    {
        GLOBUS_GRIDMAP_CALLOUT_GSS_ERROR(result, major_status, minor_status);
        gss_release_name(&minor_status, &peer);
        goto error;
    }


    *subject = globus_libc_strdup(peer_name_buffer.value);
    gss_release_buffer(&minor_status, &peer_name_buffer);
    gss_release_name(&minor_status, &peer);

    return GLOBUS_SUCCESS;

error:
    return result;
}

globus_result_t
globus_gridmap_eppn_callout(
    va_list                             ap)
{
    gss_ctx_id_t                        context;
    char *                              service;
    char *                              desired_identity;
    char *                              identity_buffer;
    char *                              found_identity = NULL;
    char *                              subject = NULL;
    unsigned int                        buffer_length;
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 rc;
    char *                              shared_user_buf = NULL;
    X509 *                              shared_user_cert = NULL;
    STACK_OF(X509) *                    shared_user_chain = NULL;
    time_t                              shared_exp = 0;
    
    rc = globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    rc = globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE);
    rc = globus_module_activate(GLOBUS_GRIDMAP_CALLOUT_ERROR_MODULE);

    context = va_arg(ap, gss_ctx_id_t);
    service = va_arg(ap, char *);
    desired_identity = va_arg(ap, char *);
    identity_buffer = va_arg(ap, char *);
    buffer_length = va_arg(ap, unsigned int);


    if(strcmp(service, "sharing") == 0)
    {
        globus_gsi_cred_handle_t    tmp_cred_handle = NULL;
        shared_user_buf = va_arg(ap, char *);
        
        result = globus_gsi_cred_read_cert_buffer(
            shared_user_buf, 
            &tmp_cred_handle, 
            &shared_user_cert, 
            &shared_user_chain, 
            &subject);
        
        globus_gsi_cred_get_goodtill(tmp_cred_handle, &shared_exp);
        globus_gsi_cred_handle_destroy(tmp_cred_handle);
    }
    else
    {
        result = ggvm_get_subject(context, &subject);
    }
    
    if(result != GLOBUS_SUCCESS || subject == NULL)
    {
        GLOBUS_GRIDMAP_CALLOUT_ERROR(
            result,
            GLOBUS_GRIDMAP_CALLOUT_GSSAPI_ERROR,
            ("Could not extract user identity."));
        goto error;
    }

    result = ggvm_get_myproxy_userid(
        context, subject, &found_identity, shared_user_cert, shared_user_chain, shared_exp);
    if(result == GLOBUS_SUCCESS)
    {
        if(desired_identity && strcmp(found_identity, desired_identity) != 0)
        {
            GLOBUS_GRIDMAP_CALLOUT_ERROR(
                result,
                GLOBUS_GRIDMAP_CALLOUT_LOOKUP_FAILED,
                ("Credentials specify id of %s, can not allow id of %s.\n",
                 found_identity, desired_identity));
            globus_free(found_identity);
            goto error;
        }
    }
    else
    {
        /* proceed with gridmap lookup */
        if(desired_identity == NULL)
        {
            rc = globus_gss_assist_gridmap(subject, &found_identity);
            if(rc != 0)
            {
                GLOBUS_GRIDMAP_CALLOUT_ERROR(
                    result,
                    GLOBUS_GRIDMAP_CALLOUT_LOOKUP_FAILED,
                    ("Could not map %s\n", subject));
                goto error;
            }
        }
        else
        {
            rc = globus_gss_assist_userok(subject, desired_identity);
            if(rc != 0)
            {
                GLOBUS_GRIDMAP_CALLOUT_ERROR(
                    result,
                    GLOBUS_GRIDMAP_CALLOUT_LOOKUP_FAILED,
                    ("Could not map %s to %s\n",
                     subject, desired_identity));
                goto error;
            }
            found_identity = globus_libc_strdup(desired_identity);
        }
        result = GLOBUS_SUCCESS;
    }

    if(found_identity)
    {
        if(strlen(found_identity) + 1 > buffer_length)
        {
            GLOBUS_GRIDMAP_CALLOUT_ERROR(
                result,
                GLOBUS_GRIDMAP_CALLOUT_BUFFER_TOO_SMALL,
                ("Local identity length: %d Buffer length: %d\n",
                 strlen(found_identity), buffer_length));
        }
        else
        {
            strcpy(identity_buffer, found_identity);
        }
        globus_free(found_identity);
    }


error:

    if(subject)
    {
        globus_free(subject);
    }
    if(shared_user_cert)
    {
        X509_free(shared_user_cert);
    }
    if(shared_user_chain)
    {
        sk_X509_free(shared_user_chain);
    }

    globus_module_deactivate(GLOBUS_GRIDMAP_CALLOUT_ERROR_MODULE);
    globus_module_deactivate(GLOBUS_GSI_GSSAPI_MODULE);
    globus_module_deactivate(GLOBUS_GSI_GSS_ASSIST_MODULE);

    return result;
}
