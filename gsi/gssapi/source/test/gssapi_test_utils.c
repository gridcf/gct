/*
 * Copyright 1999-2008 University of Chicago
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

#include "gssapi_test_utils.h"
#include "openssl/x509.h"
#include "openssl/x509v3.h"

static const gss_OID_desc globus_l_gss_mech_oid_globus_gssapi_openssl =
        {9, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01"};

const gss_OID_desc * const globus_i_gss_mech_globus_gssapi_openssl =
                &globus_l_gss_mech_oid_globus_gssapi_openssl;

static const gss_OID_desc globus_l_gss_proxycertinfo_extension_oid =
     {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x01\x06"};
const gss_OID_desc * const globus_i_gss_proxycertinfo_extension =
                &globus_l_gss_proxycertinfo_extension_oid;

static const gss_OID_desc globus_l_gss_ext_x509_cert_chain_oid_desc =
     {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x01\x08"};
const gss_OID_desc * const globus_i_gss_ext_x509_cert_chain_oid =
                &globus_l_gss_ext_x509_cert_chain_oid_desc;


static gss_OID_desc globus_l_gss_nt_host_ip_oid =
    { 10, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x02" };
gss_OID_desc * globus_i_gss_nt_host_ip = &globus_l_gss_nt_host_ip_oid;

static gss_OID_desc globus_l_gss_nt_x509_oid =
    { 10, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x03" };
gss_OID_desc * globus_i_gss_nt_x509 = &globus_l_gss_nt_x509_oid;


static int
get_token(
    int                                 fd,
    unsigned char **                    token,
    size_t *                            token_length);

static int
put_token(
    int                                 fd,
    unsigned char *                     token,
    size_t                              token_length);


static OM_uint32
accept_sec_context(
    int                                 client_fd,
    char **                             name,
    gss_ctx_id_t *                      context,
    gss_cred_id_t *                     delegated_cred,
    gss_cred_id_t                       credential);

static OM_uint32
init_sec_context(
    int                                 client_fd,
    gss_cred_id_t                       credential,
    gss_ctx_id_t *                      context);


gss_cred_id_t 
globus_gsi_gssapi_test_acquire_credential()
{
    gss_cred_id_t                       credential = GSS_C_NO_CREDENTIAL;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status;
    
    major_status = gss_acquire_cred(&minor_status,
                                    GSS_C_NO_NAME,
                                    GSS_C_INDEFINITE,
                                    GSS_C_NO_OID_SET,
                                    GSS_C_BOTH,
                                    &credential,
                                    NULL,
                                    NULL);
    
    if(major_status != GSS_S_COMPLETE)
    {
        globus_gsi_gssapi_test_print_error(stderr, major_status, minor_status);
        globus_gsi_gssapi_test_print_result(stderr, minor_status);
        return GSS_C_NO_CREDENTIAL;
    }
    
    return credential;
}

void 
globus_gsi_gssapi_test_release_credential(
    gss_cred_id_t *                     credential)
{
    OM_uint32                           minor_status;

    gss_release_cred(&minor_status, credential);
}

globus_bool_t
globus_gsi_gssapi_test_authenticate(
    int                                 fd,
    globus_bool_t                       server, 
    gss_cred_id_t                       credential, 
    gss_ctx_id_t *                      context_handle, 
    char **                             user_id, 
    gss_cred_id_t *                     delegated_cred)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    
    if (server == GLOBUS_TRUE) 
    {
        major_status = accept_sec_context(fd, 
					  user_id,
					  context_handle, 
					  delegated_cred, 
					  credential);
    }
    else 
    {
        major_status = init_sec_context(fd,
					credential,
					context_handle);
    }

    return major_status == GSS_S_COMPLETE ? GLOBUS_TRUE : GLOBUS_FALSE;
}

int
test_establish_contexts_with_mechs(
    gss_ctx_id_t                       *init_context,
    gss_ctx_id_t                       *accept_context,
    const gss_OID                       init_mec_type,
    OM_uint32                           flags,
    OM_uint32                          *major_status,
    OM_uint32                          *minor_status)
    
{
    int                                 rc = 0;
    OM_uint32                           init_major_status;
    OM_uint32                           init_minor_status;
    OM_uint32                           accept_major_status;
    OM_uint32                           accept_minor_status;
    OM_uint32                           release_minor_status;
    gss_buffer_desc                     init_token = {0};
    gss_buffer_desc                     accept_token = {0};

    assert(init_context != NULL);
    assert(accept_context != NULL);
    assert(major_status != NULL);
    assert(minor_status != NULL);

    *init_context = GSS_C_NO_CONTEXT;
    *accept_context = GSS_C_NO_CONTEXT;
    *major_status = 0;
    *minor_status = 0;

    do
    {
        init_major_status = gss_init_sec_context(
                &init_minor_status,
                GSS_C_NO_CREDENTIAL,
                init_context,
                GSS_C_NO_NAME,
                init_mec_type,
                flags,
                0,
                GSS_C_NO_CHANNEL_BINDINGS,
                &accept_token,
                NULL,
                &init_token,
                NULL,
                NULL);

        if (GSS_ERROR(init_major_status))
        {
            *major_status = init_major_status;
            *minor_status = init_minor_status;
            rc = 1;

            goto init_fail;
        }

        if (accept_token.length != 0)
        {
            gss_release_buffer(&release_minor_status, &accept_token);
            accept_token = (gss_buffer_desc) {0};
        }


        if (init_token.length != 0)
        {
            accept_major_status = gss_accept_sec_context(
                    &accept_minor_status,
                    accept_context,
                    GSS_C_NO_CREDENTIAL,
                    &init_token,
                    GSS_C_NO_CHANNEL_BINDINGS,
                    NULL,
                    NULL,
                    &accept_token,
                    NULL,
                    NULL,
                    NULL);
            if (GSS_ERROR(accept_major_status))
            {
                *major_status = accept_major_status;
                *minor_status = accept_minor_status;
                rc = 1;
                goto accept_fail;
            }
            gss_release_buffer(&release_minor_status, &init_token);
            init_token = (gss_buffer_desc) {0};
        }
    }
    while (init_major_status == GSS_S_CONTINUE_NEEDED &&
           accept_major_status == GSS_S_CONTINUE_NEEDED);
    
    if (rc != 0)
    {
init_fail:
accept_fail:
        if (*init_context != GSS_C_NO_CONTEXT)
        {
            gss_delete_sec_context(&release_minor_status, init_context, NULL);
            *init_context = GSS_C_NO_CONTEXT;
        }
        if (*accept_context != GSS_C_NO_CONTEXT)
        {
            gss_delete_sec_context(&release_minor_status, accept_context, NULL);
            *accept_context = GSS_C_NO_CONTEXT;
        }
    }
    if (init_token.length != 0)
    {
        gss_release_buffer(&release_minor_status, &init_token);
    }
    if (accept_token.length != 0)
    {
        gss_release_buffer(&release_minor_status, &accept_token);
    }

    return rc;
}


int
test_establish_contexts(
    gss_ctx_id_t                       *init_context,
    gss_ctx_id_t                       *accept_context,
    OM_uint32                           flags,
    OM_uint32                          *major_status,
    OM_uint32                          *minor_status)
    
{
    return test_establish_contexts_with_mechs(
            init_context,
            accept_context,
            GSS_C_NO_OID,
            flags,
            major_status,
            minor_status);
}


void 
globus_gsi_gssapi_test_cleanup(
    gss_ctx_id_t *                      context_handle,
    char *                              userid,
    gss_cred_id_t *                     delegated_cred)
{
    OM_uint32                           minor_status;
    
    free(userid);
    
    gss_delete_sec_context(&minor_status, context_handle, GSS_C_NO_BUFFER);
    
    if (delegated_cred != NULL)
    {
        gss_release_cred(&minor_status, delegated_cred);
    }
}

globus_bool_t
globus_gsi_gssapi_test_export_context(
    FILE *                              context_file,
    gss_ctx_id_t *                      context)
{
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    globus_bool_t                       result = GLOBUS_TRUE;
    gss_buffer_desc                     export_token = GSS_C_EMPTY_BUFFER;

    major_status = gss_export_sec_context(
        &minor_status,
        context,
        (gss_buffer_t) & export_token);

    if(GSS_ERROR(major_status))
    {
        globus_gsi_gssapi_test_print_error(
            stderr, major_status, minor_status);

        result = GLOBUS_FALSE;
        goto exit;
    }

    if(fwrite(export_token.value, export_token.length, 1, context_file) < 1)
    {
        fprintf(stderr,"\nLINE %d ERROR: Couldn't write context to file\n\n",
                __LINE__);
        gss_release_buffer(&minor_status, &export_token);
        result = GLOBUS_FALSE;
        goto exit;        
    }

    gss_release_buffer(&minor_status, &export_token);
    
 exit:
    return result;
}

globus_bool_t
globus_gsi_gssapi_test_import_context(
    FILE *                              context_file,
    gss_ctx_id_t *                      context)
{
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    globus_bool_t                       result = GLOBUS_TRUE;
    gss_buffer_desc                     import_token = GSS_C_EMPTY_BUFFER;

    fseek(context_file, 0, SEEK_END);
    import_token.length = ftell(context_file);
    fseek(context_file, 0, SEEK_SET);
    import_token.value = malloc(import_token.length);

    fread(import_token.value, import_token.length, 1, context_file);
    
    major_status = gss_import_sec_context(
        &minor_status,
        (gss_buffer_t) & import_token,
        context);

    if(GSS_ERROR(major_status))
    {
        globus_gsi_gssapi_test_print_error(
            stderr, major_status, minor_status);
        gss_release_buffer(&minor_status, &import_token);
        result = GLOBUS_FALSE;
        goto exit;
    }
    
    gss_release_buffer(&minor_status, &import_token);
    
 exit:

    return result;
}

globus_bool_t
globus_gsi_gssapi_test_send_hello(
    int                                 fd,
    gss_ctx_id_t                        context)
{
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    globus_bool_t                       result = GLOBUS_TRUE;
    static char *                       hello = "HelloHello";
    gss_buffer_desc                     send_token = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc                     input_token;
    long                                rc;
    long                                written = 0;
    
    input_token.length = 11;
    input_token.value = hello;
    
    major_status = gss_wrap(&minor_status,
                            context,
                            0,
                            GSS_C_QOP_DEFAULT,
                            (gss_buffer_t) &input_token,
                            NULL,
                            (gss_buffer_t) &send_token);
    
    if(GSS_ERROR(major_status))
    {
        globus_gsi_gssapi_test_print_error(
            stderr, major_status, minor_status);
        result = GLOBUS_FALSE;
        goto exit;
    }

    while(send_token.length - written &&
          (rc = send(fd, &((char *) send_token.value)[written],
                      send_token.length - written, 0)) > 0 &&
          (written += rc));

    if(rc < 0)
    {
        result = GLOBUS_FALSE;
    }

    /*printf("Wrote %d out of %d bytes\n", written, send_token.length); */
    
    gss_release_buffer(&minor_status, &send_token);

    
 exit:
    return result;
}

globus_bool_t
globus_gsi_gssapi_test_receive_hello(
    int                                 fd,
    gss_ctx_id_t                        context)
{
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    globus_bool_t                       result = GLOBUS_TRUE;
    char                                buffer[128];
    long                                rc;
    gss_buffer_desc                     recv_token = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc                     output_token = GSS_C_EMPTY_BUFFER;
    
    while((rc = recv(fd,&buffer[recv_token.length],128,0)) > 0 &&
          (recv_token.length += rc));

    if(rc < 0)
    {
        fprintf(stderr, "System error: %s\n", strerror(errno));
        result = GLOBUS_FALSE;
        goto exit;
    }

    /* printf("Read %d bytes\n", recv_token.length); */
    
    recv_token.value = buffer;

    major_status = gss_unwrap(&minor_status,
                              context,
                              (gss_buffer_t) &recv_token,
                              (gss_buffer_t) &output_token,
                              NULL,
                              NULL);
    if(GSS_ERROR(major_status))
    {
        globus_gsi_gssapi_test_print_error(
            stderr, major_status, minor_status);
        result = GLOBUS_FALSE;
        goto exit;
    }

    if(memcmp(output_token.value, "HelloHello", 11))
    {
        result = GLOBUS_FALSE;
    }

    gss_release_buffer(&minor_status, &output_token);    

 exit:
    return result;
}

globus_bool_t
globus_gsi_gssapi_test_dump_cert_chain(
    char *                              filename,
    gss_ctx_id_t                        context)
{
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    int                                 i;
    globus_bool_t                       result = GLOBUS_TRUE;
    FILE *                              dump_file;
    gss_OID_desc                        cert_chain_oid =
        {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x01\x08"}; 
    gss_buffer_set_t                    cert_chain_buffers;
    X509 *                              cert;
    const unsigned char *                   tmp_ptr;
    
    dump_file = fopen(filename,"w");

    if(dump_file == NULL)
    {
        fprintf(stderr,"\nLINE %d ERROR: Couldn't open %s\n\n",
                __LINE__, filename);
        result = GLOBUS_FALSE;
        goto exit;
    }

    major_status = gss_inquire_sec_context_by_oid(
        &minor_status,
        context,
        &cert_chain_oid,
        &cert_chain_buffers);

    if(GSS_ERROR(major_status))
    {
        globus_gsi_gssapi_test_print_error(
            stderr, major_status, minor_status);
        result = GLOBUS_FALSE;
        goto exit;
    }

    for(i = 0; i < cert_chain_buffers->count; i++)
    {
        tmp_ptr = cert_chain_buffers->elements[i].value;
        cert = d2i_X509(NULL, &tmp_ptr,
                        cert_chain_buffers->elements[i].length);
        if(cert == NULL)
        {
            fprintf(stderr,"\nLINE %d ERROR: Couldn't deserialize cert\n\n",
                    __LINE__);
            result = GLOBUS_FALSE;
            gss_release_buffer_set(&minor_status,
                                   &cert_chain_buffers);
            goto exit;
        }
        
        X509_print_fp(dump_file,
                      cert);
        X509_free(cert);
    }

    gss_release_buffer_set(&minor_status,
                           &cert_chain_buffers);
 exit:
    if(dump_file)
    {
        fclose(dump_file);
    }
    
    return result;
}

static int
get_token(
    int                                 fd,
    unsigned char **                    token,
    size_t *                            token_length)
{
    size_t                              num_read = 0;
    ssize_t                             n_read;
    unsigned char                       token_length_buffer[4];

    while(num_read < 4)
    {
        n_read = recv(fd,
                      token_length_buffer + num_read,
                      4 - num_read, 0);

        if(n_read < 0)
        {
            if(errno == EINTR)
            { 
                continue;
            }
            else
            { 
                return errno;
            }
        }
        else
        { 
            num_read += n_read;
        }
    }
    
    /* decode the token length from network byte order: 4 byte, big endian */

    *token_length  = (((size_t) token_length_buffer[0]) << 24) & 0xffff;
    *token_length |= (((size_t) token_length_buffer[1]) << 16) & 0xffff;
    *token_length |= (((size_t) token_length_buffer[2]) <<  8) & 0xffff;
    *token_length |= (((size_t) token_length_buffer[3])      ) & 0xffff;

    if(*token_length > 1<<24)
    {
        /* token too large */
        return 1;
    }
    
    *token = malloc(*token_length);

    if(*token == NULL)
    {
        return errno;
    }

    num_read = 0;

    while(num_read < *token_length)
    {
        n_read = recv(fd,
                      *token + num_read,
                      *token_length - num_read, 0);

        if(n_read < 0)
        {
            if(errno == EINTR)
            { 
                continue;
            }
            else
            { 
                return errno;
            }
        }
        else
        { 
            num_read += n_read;
        }
    }
    
    return 0;
}

static int
put_token(
    int                                 fd,
    unsigned char *                     token,
    size_t                              token_length)
{
    size_t                              num_written = 0;
    ssize_t                             n_written;
    unsigned char                       token_length_buffer[4];

    /* encode the token length in network byte order: 4 byte, big endian */

    token_length_buffer[0] = (unsigned char) ((token_length >> 24) & 0xff);
    token_length_buffer[1] = (unsigned char) ((token_length >> 16) & 0xff);
    token_length_buffer[2] = (unsigned char) ((token_length >>  8) & 0xff);
    token_length_buffer[3] = (unsigned char) ((token_length      ) & 0xff);

    while(num_written < 4)
    {
        n_written = send(fd,
                          token_length_buffer + num_written,
                          4 - num_written, 0);
        if(n_written < 0)
        {
            if(errno == EINTR)
            { 
                continue;
            }
            else
            { 
                return errno;
            }
        }
        else
        { 
            num_written += n_written;
        }
    }
    
    num_written = 0;

    while(num_written < token_length)
    {
        n_written = send(fd,
                          token + num_written,
                          token_length - num_written, 0);
        if(n_written < 0)
        {
            if(errno == EINTR)
            { 
                continue;
            }
            else
            { 
                return errno;
            }
        }
        else
        { 
            num_written += n_written;
        }
    }

    return 0;
}


static OM_uint32
accept_sec_context(
    int                                 client_fd,
    char **                             name,
    gss_ctx_id_t *                      context,
    gss_cred_id_t *                     delegated_cred,
    gss_cred_id_t                       credential)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status;
    OM_uint32                           minor_status2;
    OM_uint32                           ret_flags = 0;
    int                                 token_status = 0;
    gss_name_t                          client_name = GSS_C_NO_NAME;
    gss_buffer_desc                     input_token  = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc                     output_token = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc                     name_buffer;
    gss_OID                             mech_type = GSS_C_NO_OID;
    OM_uint32                           time_ret;
    
    if(credential == GSS_C_NO_CREDENTIAL)
    {
        globus_libc_printf("Failed to acquire credentials\n");
        return 1;
    }

    do
    {
        token_status = get_token(client_fd,
                                 (unsigned char **) &input_token.value,
                                 &input_token.length);
        if(token_status != 0)
        {
            major_status = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_READ;
            break;
        }

        major_status = gss_accept_sec_context(&minor_status,
                                              context,
                                              credential,
                                              &input_token,
                                              GSS_C_NO_CHANNEL_BINDINGS,
                                              &client_name,
                                              &mech_type,
                                              &output_token,
                                              &ret_flags,
                                              &time_ret,
                                              delegated_cred);

	if(major_status != GSS_S_COMPLETE &&
	   major_status != GSS_S_CONTINUE_NEEDED)
	{
            char *                      error_string = NULL;
            globus_object_t *           error_obj = NULL;

            error_obj = globus_error_get((globus_result_t) minor_status);
            error_string = globus_error_print_chain(error_obj);
            fprintf(stderr, "ERROR CHAIN:\n%s\n", error_string);
            free(error_string);
            globus_object_free(error_obj);
	    abort();
	}

        if(output_token.length != 0)
        {
            token_status = put_token(client_fd,
                                      output_token.value,
                                      output_token.length);
            if(token_status != 0)
            {
                major_status =
                    GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_WRITE;
            }
	    
            gss_release_buffer(&minor_status2,
                               &output_token);
        }

        if (input_token.length > 0)
        {
            gss_release_buffer(&minor_status2,
                               &input_token);
        }

        if (GSS_ERROR(major_status))
        {
            if (context != NULL)
            {
                printf("    Failed to establish security context (accept).\n");
                globus_gsi_gssapi_test_print_error(
                    stderr, major_status, minor_status);
                gss_delete_sec_context(&minor_status2, context, GSS_C_NO_BUFFER);
                break;
            }
        }
    }
    while(major_status & GSS_S_CONTINUE_NEEDED);

    /* authentication failed */

    if(major_status != GSS_S_COMPLETE)
    {
        return 1;
    }

    /* authentication succeeded, figure out who it is */

    major_status = gss_display_name(&minor_status,
				    client_name,
				    &name_buffer,
				    NULL);

    *name = (char *)name_buffer.value;

    gss_release_name(&minor_status2, &client_name);

    return major_status;
}


static OM_uint32
init_sec_context(
    int                                 client_fd,
    gss_cred_id_t                       credential,
    gss_ctx_id_t *                      context)
{
    OM_uint32			        minor_status2 = 0;
    OM_uint32			        minor_status = 0;
    OM_uint32			        major_status = GSS_S_COMPLETE;
    OM_uint32			        req_flags  = GSS_C_MUTUAL_FLAG|GSS_C_DELEG_FLAG;
    OM_uint32			        ret_flags  = 0;
    int				        token_status = 0;
    gss_name_t			        target_name = GSS_C_NO_NAME;
    globus_bool_t		        context_established = GLOBUS_FALSE;
    gss_OID *			        actual_mech_type = NULL;
    OM_uint32			        time_ret;
    gss_buffer_desc		        input_token = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc 		        output_token = GSS_C_EMPTY_BUFFER;

    major_status = gss_inquire_cred(&minor_status,
				    credential,
				    &target_name,
				    NULL,
				    NULL,
				    NULL);

    if(major_status != GSS_S_COMPLETE)
    {
	globus_libc_printf("Failed to determine my name\n");
	return 1;
    }

    while(!context_established)
    {
	major_status = gss_init_sec_context(&minor_status,
					    credential,
					    context,
					    target_name,
					    GSS_C_NO_OID, /*mech_type*/
					    req_flags,
					    0, /* default time */
					    GSS_C_NO_CHANNEL_BINDINGS,
					    &input_token,
					    actual_mech_type,
					    &output_token,
					    &ret_flags,
					    &time_ret);
	if(major_status != GSS_S_COMPLETE &&
	   major_status != GSS_S_CONTINUE_NEEDED)
	{
            char *                      error_string = NULL;
            globus_object_t *           error_obj = NULL;

            error_obj = globus_error_get((globus_result_t) minor_status);
            error_string = globus_error_print_chain(error_obj);
            fprintf(stderr, "ERROR CHAIN:\n%s\n", error_string);
            free(error_string);
            globus_object_free(error_obj);
	    abort();
	}
	/* free any token we've just processed */
	if(input_token.length > 0)
	{
	    gss_release_buffer(&minor_status2,
                               &input_token);
	}
	
	/* and send any new token to the server */
	if(output_token.length != 0)
	{
	    token_status = put_token(client_fd,
				     output_token.value,
				     output_token.length);
	    if(token_status != 0)
	    {
		major_status =
		    GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_WRITE;
	    }
            gss_release_buffer(&minor_status,
                               &output_token);
	}
	
	if (GSS_ERROR(major_status))
	{
	    printf("    Failed to establish security context (init).\n");
            globus_gsi_gssapi_test_print_error(
                stderr, major_status, minor_status);

	    if (*context != GSS_C_NO_CONTEXT)
	    {
		gss_delete_sec_context(&minor_status2,
				       context,
				       GSS_C_NO_BUFFER);
		break;
	    }
	}
	
	if(major_status & GSS_S_CONTINUE_NEEDED)
	{
	    token_status = get_token(client_fd,
				     (unsigned char **) &input_token.value,
				     &input_token.length);
	    if(token_status != 0)
	    {
		major_status = 
		    GSS_S_DEFECTIVE_TOKEN | GSS_S_CALL_INACCESSIBLE_READ;
		break;
	    }
	}
	else
	{
	    context_established = 1;
	}
    } /* while() */

    if (target_name != GSS_C_NO_NAME)
    {
        gss_release_name(&minor_status2,&target_name);
    }

    return major_status;
}

void
globus_gsi_gssapi_test_print_error(
    FILE *                              stream,
    OM_uint32                           major_status,
    OM_uint32                           minor_status)
{
    OM_uint32                           message_context = 0;
    OM_uint32                           minor_status2;
    gss_buffer_desc                     status_string;

    do {
        if (gss_display_status(&minor_status2,
                               major_status,
                               GSS_C_GSS_CODE,
                               GSS_C_NO_OID,
                               &message_context,
                               &status_string) == GSS_S_COMPLETE)
        {
            if (status_string.length)
            {
                fprintf(stream,"\nERROR: %.*s\n",
                        (int) status_string.length,
                        (char *) status_string.value);
            }
            gss_release_buffer(&minor_status2, &status_string);
        }
        if (gss_display_status(&minor_status2,
                               minor_status,
                               GSS_C_MECH_CODE,
                               GSS_C_NO_OID,
                               &message_context,
                               &status_string) == GSS_S_COMPLETE)
        {
            if (status_string.length)
            {
                fprintf(stream,"\nERROR: %.*s\n",
                        (int) status_string.length,
                        (char *) status_string.value);
            }
            gss_release_buffer(&minor_status2, &status_string);
        }
    }
    while (message_context != 0);
}
/* globus_gsi_gssapi_test_print_error() */

void
globus_gsi_gssapi_test_print_result(
    FILE *                              stream,
    globus_result_t                     result)
{
    globus_object_t *                   err;
    char *                              msg;

    err = globus_error_peek(result);

    msg = globus_error_print_friendly(err);

    fprintf(stream, "%s", msg);

    free(msg);
}
