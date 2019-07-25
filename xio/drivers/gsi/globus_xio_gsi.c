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

#include "globus_i_xio_gsi.h"
#include "globus_xio_util.h"
#include "version.h"

/* 32 MB */
#define MAX_TOKEN_LENGTH 2<<24

static gss_OID_desc gss_l_openssl_mech_oid =
        {9, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01"};
static gss_OID_desc * gss_l_openssl_mech = &gss_l_openssl_mech_oid;

static gss_OID_desc gss_nt_host_ip_oid =
    { 10, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x02" };
static gss_OID_desc * GLOBUS_GSS_C_NT_HOST_IP = &gss_nt_host_ip_oid;

static gss_OID_desc GSS_SNI_CREDENTIALS_OID =
   {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x03\x04"};

static gss_OID_desc GSS_ALPN_OID =
   {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x03\x05"};



static globus_bool_t globus_l_xio_gsi_host_ip_supported;

/* default attributes */
static globus_l_attr_t                  globus_l_xio_gsi_attr_default =
{
    .credential                         = GSS_C_NO_CREDENTIAL,
    .req_flags                          = GSS_C_MUTUAL_FLAG,
    .time_req                           = 0,
    .mech_type                          = 0,
    .channel_bindings                   = GSS_C_NO_CHANNEL_BINDINGS,
    .wrap_tokens                        = GLOBUS_FALSE,
    .buffer_size                        = 131072, /* 128K default read buffer */
    .prot_level                         =
        GLOBUS_XIO_GSI_PROTECTION_LEVEL_INTEGRITY,
    .target_name                        = GSS_C_NO_NAME,
    .init                               = GLOBUS_TRUE,
    .authz_mode                         = GLOBUS_XIO_GSI_NO_AUTHORIZATION,
    .credentials_dir                    = NULL,
};

static int                              connection_count = 0;
static globus_mutex_t                   connection_mutex;

static
globus_result_t
globus_l_xio_gsi_attr_destroy(
    void *                              driver_attr);

static
globus_result_t
globus_l_xio_gsi_setup_target_name(
    globus_l_handle_t *                 handle);

static
void
globus_l_xio_gsi_read_token_cb(
    struct globus_i_xio_op_s *          op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg);


static
void
globus_l_xio_gsi_read_delegation_token_cb(
    struct globus_i_xio_op_s *          op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg);

static
void
globus_l_xio_gsi_close_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg);

static int
globus_l_xio_gsi_activate();

static int
globus_l_xio_gsi_deactivate();

GlobusXIODefineModule(gsi) =
{
    "globus_xio_gsi",
    globus_l_xio_gsi_activate,
    globus_l_xio_gsi_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

GlobusDebugDefine(GLOBUS_XIO_GSI);

/*
 *  initialize a driver attribute
 */
static
globus_result_t
globus_l_xio_gsi_attr_init(
    void **                             out_attr)
{
    globus_l_attr_t *                   attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_gsi_attr_init);
    GlobusXIOGSIDebugEnter();
    
    if(!out_attr)
    {
        GlobusXIOGSIDebugExitWithError();
        return GlobusXIOErrorParameter("out_attr");
    }
    
    /*
     *  create a gsi attr structure and intialize its values
     */
    attr = (globus_l_attr_t *) calloc(1, sizeof(globus_l_attr_t));
    if(!attr)
    {
        result = GlobusXIOErrorMemory("attr");
        goto error_attr;
    }
    attr->target_name = GSS_C_NO_NAME;

    /* set to default attributes */
    
    *attr = globus_l_xio_gsi_attr_default;
    *out_attr = attr;

    GlobusXIOGSIDebugExit();
    return GLOBUS_SUCCESS;

error_attr:
    GlobusXIOGSIDebugExitWithError();
    return result;
}

/*
 *  modify the attribute structure
 */
static
globus_result_t
globus_l_xio_gsi_attr_cntl(
    void *                              driver_attr,
    int                                 cmd,
    va_list                             ap)
{
    globus_l_attr_t *                       attr = driver_attr;
    gss_cred_id_t *                         out_cred = NULL;
    OM_uint32 *                             out_flags = NULL;
    OM_uint32                               minor_status = 0;
    OM_uint32                               major_status = GSS_S_COMPLETE;
    globus_bool_t *                         out_bool = NULL;
    globus_xio_gsi_protection_level_t *     out_prot_level = NULL;
    globus_xio_gsi_proxy_mode_t *           out_proxy_mode = NULL;
    globus_xio_gsi_proxy_mode_t             proxy_mode
        = GLOBUS_XIO_GSI_PROXY_MODE_LIMITED;
    globus_xio_gsi_delegation_mode_t *      out_delegation_mode = NULL;
    globus_xio_gsi_delegation_mode_t        delegation_mode
        = GLOBUS_XIO_GSI_DELEGATION_MODE_NONE;
    globus_xio_gsi_authorization_mode_t *   out_authz_mode = NULL;
    globus_bool_t                           ssl_wrap = GLOBUS_TRUE;
    globus_result_t                         result = GLOBUS_SUCCESS;
    globus_size_t *                         out_size = NULL;
    const char *                            in_directory = NULL;
    char **                                 in_protocols = NULL;
    gss_name_t *                            out_name = NULL;
    gss_name_t                              in_name = GSS_C_NO_NAME;
    globus_bool_t                           in_bool = GLOBUS_TRUE;
    GlobusXIOName(globus_l_xio_gsi_attr_cntl);
    GlobusXIOGSIDebugEnter();

    if (attr == NULL)
    {
        result = GlobusXIOErrorParameter("driver_attr");
        goto error_invalid;
    }    

    switch(cmd) 
    {
      case GLOBUS_XIO_GSI_GET_TARGET_NAME:
        out_name = va_arg(ap, gss_name_t *);
        *out_name = attr->target_name;
        break;
      case GLOBUS_XIO_GSI_SET_TARGET_NAME:
        if(attr->target_name != GSS_C_NO_NAME)
        {
            gss_release_name(&minor_status,
                             &attr->target_name);
            attr->target_name = GSS_C_NO_NAME;
        }

        in_name = va_arg(ap, gss_name_t);
        if(in_name != GSS_C_NO_NAME)
        {
            major_status = gss_duplicate_name(&minor_status,
                                              in_name,
                                              &attr->target_name);
            if(GSS_ERROR(major_status))
            {
                result = GlobusXIOErrorWrapGSSFailed("gss_duplicate_name",
                                                     major_status,
                                                     minor_status);
            }
        }
        break;
      
      case GLOBUS_XIO_GSI_FORCE_SERVER_MODE:
        in_bool = va_arg(ap, globus_bool_t);
        attr->init = !in_bool;
        break;
        
        /*
         * Credential
         */
      case GLOBUS_XIO_GSI_SET_CREDENTIAL:
        attr->credential = va_arg(ap, gss_cred_id_t);
        break;

      case GLOBUS_XIO_GSI_SET_CREDENTIALS_DIR:
        in_directory = va_arg(ap, const char *);
        free(attr->credentials_dir); 
        if (in_directory == NULL)
        {
            attr->credentials_dir = NULL;
        }
        else
        {
            attr->credentials_dir = strdup(in_directory);
        }
        break;

      case GLOBUS_XIO_GSI_SET_APPLICATION_PROTOCOLS:
        in_protocols = va_arg(ap, char **);

        free(attr->alpn_list);
        if (in_protocols == NULL)
        {
            attr->alpn_list = NULL;
            attr->alpn_list_len = 0;
        }
        else
        {
            size_t                      alpn_list_len = 0;
            for (size_t i = 0; in_protocols[i] != NULL; i++)
            {
                alpn_list_len += 1 + strlen(in_protocols[i]);
            }
            attr->alpn_list = malloc(alpn_list_len);
            for (size_t i = 0, j = 0; in_protocols[i] != NULL; i++)
            {
                size_t protocol_len = strlen(in_protocols[i]);

                attr->alpn_list[j++] = protocol_len;
                memcpy(
                    &attr->alpn_list[j],
                    in_protocols[i],
                    protocol_len);
                j += protocol_len;
                attr->alpn_list_len = j;
            }
        }
        break;
      case GLOBUS_XIO_GSI_GET_CREDENTIAL:
        out_cred = va_arg(ap, gss_cred_id_t *);
        *out_cred = attr->credential;
        break;
        
        /*
         * GSSAPI flags
         */
      case GLOBUS_XIO_GSI_SET_GSSAPI_REQ_FLAGS:
        attr->req_flags = va_arg(ap, OM_uint32);
        break;
      case GLOBUS_XIO_GSI_GET_GSSAPI_REQ_FLAGS:
        out_flags = va_arg(ap, OM_uint32 *);
        *out_flags = attr->req_flags;
        break;

        /*
         * Authorization mode
         */
      case GLOBUS_XIO_GSI_SET_AUTHORIZATION_MODE:
        attr->authz_mode = va_arg(ap, globus_xio_gsi_authorization_mode_t);
        break;
      case GLOBUS_XIO_GSI_GET_AUTHORIZATION_MODE:
        out_authz_mode = va_arg(ap, globus_xio_gsi_authorization_mode_t *);
        *out_authz_mode = attr->authz_mode;
        break;

        /*
         * Proxy mode
         */ 
      case GLOBUS_XIO_GSI_SET_PROXY_MODE:
        proxy_mode = va_arg(ap, globus_xio_gsi_proxy_mode_t);
        if(proxy_mode == GLOBUS_XIO_GSI_PROXY_MODE_FULL)
        {
            /* set limited flag and clear many flag */
            attr->req_flags |= GSS_C_GLOBUS_LIMITED_PROXY_FLAG;
            attr->req_flags &= ~GSS_C_GLOBUS_LIMITED_PROXY_MANY_FLAG;
        }
        else if(proxy_mode == GLOBUS_XIO_GSI_PROXY_MODE_LIMITED)
        {
            /* clear any proxy related flags */
            attr->req_flags &= (~GSS_C_GLOBUS_LIMITED_PROXY_FLAG &
                                ~GSS_C_GLOBUS_LIMITED_PROXY_MANY_FLAG);
        }
        else if(proxy_mode == GLOBUS_XIO_GSI_PROXY_MODE_MANY)
        {
            /* set many flag and clear limited flag */
            attr->req_flags &= ~GSS_C_GLOBUS_LIMITED_PROXY_FLAG;
            attr->req_flags |= GSS_C_GLOBUS_LIMITED_PROXY_MANY_FLAG;
        }
        break;
      case GLOBUS_XIO_GSI_GET_PROXY_MODE:
        out_proxy_mode = va_arg(ap, globus_xio_gsi_proxy_mode_t *);
        if(attr->req_flags & GSS_C_GLOBUS_LIMITED_PROXY_FLAG)
        {
            *out_proxy_mode = GLOBUS_XIO_GSI_PROXY_MODE_FULL;
        }
        else if(attr->req_flags & GSS_C_GLOBUS_LIMITED_PROXY_MANY_FLAG)
        {
            *out_proxy_mode = GLOBUS_XIO_GSI_PROXY_MODE_MANY;
        }
        else
        { 
            *out_proxy_mode = GLOBUS_XIO_GSI_PROXY_MODE_LIMITED;
        }
        break;
        /*
         * Delegation mode
         */
      case GLOBUS_XIO_GSI_SET_DELEGATION_MODE:
        delegation_mode = va_arg(ap, globus_xio_gsi_delegation_mode_t);
        if(delegation_mode == GLOBUS_XIO_GSI_DELEGATION_MODE_NONE)
        {
            attr->req_flags &= (~GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG &
                                ~GSS_C_DELEG_FLAG);
        }
        else if(delegation_mode == GLOBUS_XIO_GSI_DELEGATION_MODE_FULL)
        { 
            attr->req_flags |= GSS_C_DELEG_FLAG;
            attr->req_flags &= ~GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG;
            attr->req_flags &= ~GSS_C_GLOBUS_SSL_COMPATIBLE;
            attr->wrap_tokens = GLOBUS_TRUE;
        }
        else if(delegation_mode == GLOBUS_XIO_GSI_DELEGATION_MODE_LIMITED)
        {
            attr->req_flags |= GSS_C_DELEG_FLAG;
            attr->req_flags |= GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG;
            attr->req_flags &= ~GSS_C_GLOBUS_SSL_COMPATIBLE;
            attr->wrap_tokens = GLOBUS_TRUE;            
        }
        break;
      case GLOBUS_XIO_GSI_GET_DELEGATION_MODE:
        out_delegation_mode = va_arg(ap, globus_xio_gsi_delegation_mode_t *);

        if(attr->req_flags & GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG)
        {
            *out_delegation_mode = GLOBUS_XIO_GSI_DELEGATION_MODE_LIMITED;
        }
        else if(attr->req_flags & GSS_C_DELEG_FLAG)
        {
            *out_delegation_mode = GLOBUS_XIO_GSI_DELEGATION_MODE_FULL;
        }
        else
        {
            *out_delegation_mode = GLOBUS_XIO_GSI_DELEGATION_MODE_NONE;
        }
        break;

      case GLOBUS_XIO_GSI_SET_ALLOW_MISSING_SIGNING_POLICY:
        in_bool = va_arg(ap, globus_bool_t);
        if(in_bool)
        {
            attr->req_flags |= GSS_C_GLOBUS_ALLOW_MISSING_SIGNING_POLICY;
        }
        else
        {
            attr->req_flags &= ~GSS_C_GLOBUS_ALLOW_MISSING_SIGNING_POLICY;
        }
        break;
      case GLOBUS_XIO_GSI_GET_ALLOW_MISSING_SIGNING_POLICY:
        out_bool = va_arg(ap, globus_bool_t *);
        if(out_bool)
        {
            *out_bool = attr->req_flags & GSS_C_GLOBUS_ALLOW_MISSING_SIGNING_POLICY;
        }
        break;
        
        /*
         * SSL compatibility mode
         */
      case GLOBUS_XIO_GSI_SET_SSL_COMPATIBLE:
        ssl_wrap = va_arg(ap, globus_bool_t);
        
        if(ssl_wrap == GLOBUS_TRUE)
        { 
            attr->req_flags |= GSS_C_GLOBUS_SSL_COMPATIBLE;
            attr->req_flags &= ~(GSS_C_DELEG_FLAG |
                                 GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG);
            attr->wrap_tokens = GLOBUS_FALSE;
        }
        else
        {
            attr->req_flags &= ~GSS_C_GLOBUS_SSL_COMPATIBLE;
        }
        break;
        /*
         * Anonymous authentication
         */
      case GLOBUS_XIO_GSI_SET_ANON:
        attr->req_flags |= GSS_C_ANON_FLAG;
        attr->req_flags &= ~(GSS_C_DELEG_FLAG |
                             GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG);
        break;

        /*
         * Wrap mode
         */
      case GLOBUS_XIO_GSI_SET_WRAP_MODE:
        attr->wrap_tokens = va_arg(ap, globus_bool_t);
        break;
      case GLOBUS_XIO_GSI_GET_WRAP_MODE:
        out_bool = va_arg(ap, globus_bool_t *);
        *out_bool = attr->wrap_tokens;
        break;

        /*
         * Read buffer size
         */
      case GLOBUS_XIO_GSI_SET_BUFFER_SIZE:
        attr->buffer_size = va_arg(ap, globus_size_t);

        /* having a reasonable min buffer size simplifies the code */
        
        if(attr->buffer_size < 512)
        {
            attr->buffer_size = 512;
        }
        break;
      case GLOBUS_XIO_GSI_GET_BUFFER_SIZE:
        out_size = va_arg(ap, globus_size_t *);
        *out_size = attr->buffer_size;
        break;

        /*
         * Protection level
         */
      case GLOBUS_XIO_GSI_SET_PROTECTION_LEVEL:
        attr->prot_level = va_arg(ap, globus_xio_gsi_protection_level_t);

        /* make sure that the right req flags are set */
        
        if(attr->prot_level == GLOBUS_XIO_GSI_PROTECTION_LEVEL_PRIVACY)
        {
            attr->req_flags |= GSS_C_CONF_FLAG;
        }
        else if(attr->prot_level == GLOBUS_XIO_GSI_PROTECTION_LEVEL_INTEGRITY)
        {
            attr->req_flags |= GSS_C_INTEG_FLAG;
        }
        else
        {
            /* clear the flags for protection level none*/
            attr->req_flags &= ~(GSS_C_INTEG_FLAG|GSS_C_CONF_FLAG);
        }
        break;
      case GLOBUS_XIO_GSI_GET_PROTECTION_LEVEL:
        out_prot_level = va_arg(ap, globus_xio_gsi_protection_level_t *);
        *out_prot_level = attr->prot_level;
        break;
      
      default:
        result = GlobusXIOErrorInvalidCommand(cmd);
        goto error_invalid;
    }

    GlobusXIOGSIDebugExit();
    return GLOBUS_SUCCESS;
    
 error_invalid:
    GlobusXIOGSIDebugExitWithError();
    return result;
}

/*
 *  copy an attribute structure
 */
static
globus_result_t
globus_l_xio_gsi_attr_copy(
    void **                             dst,
    void *                              src)
{
    globus_l_attr_t *                   attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_gsi_attr_copy);
    GlobusXIOGSIDebugEnter();

    if(!src)
    {
        result = GlobusXIOErrorParameter("src");
        goto error_attr;        
    }

    if(!dst)
    {
        result = GlobusXIOErrorParameter("dst");
        goto error_attr;        
    }
    
    attr = (globus_l_attr_t *) malloc(sizeof(globus_l_attr_t));
    if(!attr)
    {
        result = GlobusXIOErrorMemory("attr");
        goto error_attr;
    }
    
    memcpy(attr, src, sizeof(globus_l_attr_t));
    
    if(attr->target_name != GSS_C_NO_NAME)
    {
        OM_uint32                       major_status;
        OM_uint32                       minor_status;
    
        major_status = gss_duplicate_name(&minor_status,
                                          attr->target_name,
                                          &attr->target_name);
        if(GSS_ERROR(major_status))
        {
            free(attr);
            result = GlobusXIOErrorWrapGSSFailed("gss_duplicate_name",
                                                 major_status,
                                                 minor_status);
            goto error_attr;
        }
    }
    if (attr->alpn_list != NULL)
    {
        unsigned char                  *cp = NULL;

        cp = malloc(attr->alpn_list_len);
        if (cp == NULL)
        {
            result = GlobusXIOErrorMemory("alpn_list");
            globus_l_xio_gsi_attr_destroy(attr);
            attr = NULL;
        }
        memcpy(cp, attr->alpn_list, attr->alpn_list_len);
        attr->alpn_list = cp;
    }
    if (attr->credentials_dir != NULL)
    {
        attr->credentials_dir = strdup(attr->credentials_dir);
        if (attr->credentials_dir == NULL)
        {
            result = GlobusXIOErrorMemory("credentials_dir");
            globus_l_xio_gsi_attr_destroy(attr);
            attr = NULL;
        }
    }

    *dst = attr;

    GlobusXIOGSIDebugExit();
    return GLOBUS_SUCCESS;
    
 error_attr:
    GlobusXIOGSIDebugExitWithError();
    return result;
}

/*
 *  destroy an attr structure
 */
static
globus_result_t
globus_l_xio_gsi_attr_destroy(
    void *                              driver_attr)
{
    globus_l_attr_t *                   attr = driver_attr;
    GlobusXIOName(globus_l_xio_gsi_attr_destroy);
    GlobusXIOGSIDebugEnter();

    if (attr == NULL)
    {
        GlobusXIOGSIDebugExitWithError();
        return GlobusXIOErrorParameter("driver_attr");
    }
    
    if (attr->target_name != GSS_C_NO_NAME)
    {
        OM_uint32                       minor_status;        
        gss_release_name(&minor_status,
                         &attr->target_name);
    }
    free(attr->credentials_dir);
    free(attr->alpn_list);
    free(attr);

    GlobusXIOGSIDebugExit();
    return GLOBUS_SUCCESS;
}

/*
 *  destroy the link structure
 */
static
globus_result_t
globus_l_xio_gsi_link_destroy(
    void *                              driver_link)
{
    return driver_link 
        ? globus_l_xio_gsi_attr_destroy(driver_link) : GLOBUS_SUCCESS;
}

/*
 * Accept callback - just copies server into link if not null
 * (server is just the attr passed to server init)
 */

void
globus_l_xio_gsi_accept_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    GlobusXIOName(globus_l_xio_gsi_accept_cb);
    GlobusXIOGSIDebugInternalEnter();

    if(result != GLOBUS_SUCCESS)
    {
        globus_l_xio_gsi_attr_destroy(user_arg);
        goto error;
    }
    
    globus_xio_driver_finished_accept(op, user_arg, result);
    
    GlobusXIOGSIDebugInternalExit();
    return;
    
 error:
    globus_xio_driver_finished_accept(op, NULL, result);
    GlobusXIOGSIDebugInternalExitWithError();
    return;
}

/*
 * Accept - pass to callback
 */
static globus_result_t
globus_l_xio_gsi_accept(
    void *                              driver_server,
    globus_xio_operation_t              accept_op)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_attr_t *                   attr;
    GlobusXIOName(globus_l_xio_gsi_accept);
    GlobusXIOGSIDebugEnter();
    
    if(driver_server)
    {
        result = globus_l_xio_gsi_attr_copy((void **) (void *) &attr, driver_server);
    }
    else
    {
        result = globus_l_xio_gsi_attr_init((void **) (void *) &attr);
    }
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_gsi_attr_init/copy", result);
        goto error;
    }

    /* since we are on the accept side set init to false */
    
    attr->init = GLOBUS_FALSE;
    result = globus_xio_driver_pass_accept(
        accept_op, globus_l_xio_gsi_accept_cb, attr);

 error:
    GlobusXIOGSIDebugExit();
    return result;
}


static
globus_result_t
globus_l_xio_gsi_server_init(
    void *                              driver_attr,
    const globus_xio_contact_t *        contact_info,
    globus_xio_operation_t              op)
{
    globus_result_t                     result;
    void *                              server = NULL;
    
    if(driver_attr)
    {
        result =  globus_l_xio_gsi_attr_copy(&server, driver_attr);
        if(result != GLOBUS_SUCCESS)
        {
            return result;
        }
    }
    
    result = globus_xio_driver_pass_server_init(op, contact_info, server);
    if(result != GLOBUS_SUCCESS)
    {
        if(server)
        {
            globus_l_xio_gsi_attr_destroy(server);
        }
    }
    
    return result;
}

static
globus_result_t
globus_l_xio_gsi_server_destroy(
    void *                              server)
{
    return server ? globus_l_xio_gsi_attr_destroy(server) : GLOBUS_SUCCESS;
}

/*
 * destroy driver structure - internal use only 
 */

static
void
globus_l_xio_gsi_handle_destroy(
    globus_l_handle_t *                 handle)
{
    OM_uint32                           minor_status;
    GlobusXIOName(globus_l_xio_gsi_handle_free);
    GlobusXIOGSIDebugInternalEnter();

    if(handle->attr != NULL)
    {
        globus_l_xio_gsi_attr_destroy(handle->attr);
    }

    if(handle->context != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(&minor_status,
                               &handle->context,
                               GSS_C_NO_BUFFER);
    }

    if(handle->delegated_cred != GSS_C_NO_CREDENTIAL)
    {
        gss_release_cred(&minor_status,
                         &handle->delegated_cred);
    }

    if(handle->credential != GSS_C_NO_CREDENTIAL)
    {
        gss_release_cred(&minor_status,
                         &handle->credential);
    }
    
    if(handle->peer_name != GSS_C_NO_NAME)
    {
        gss_release_name(&minor_status,
                         &handle->peer_name);
    }

    if(handle->local_name != GSS_C_NO_NAME)
    {
        gss_release_name(&minor_status,
                         &handle->local_name);
    }

    if(handle->write_iovec != NULL)
    {
        free(handle->write_iovec);
    }

    if(handle->write_headers != NULL)
    {
        free(handle->write_headers);
    }

    if(handle->unwrapped_buffer != NULL)
    {
        free(handle->unwrapped_buffer);
    }

    if(handle->read_buffer)
    { 
        free(handle->read_buffer);
    }

    if(handle->result_obj)
    {
        globus_object_free(handle->result_obj);
    }
    if (handle->host_name)
    {
        free(handle->host_name);
    }
    if (handle->cred_array != NULL)
    {
        for (
            size_t i = 0;
            i < handle->cred_array_length / sizeof(gss_cred_id_t);
            i++)
        {
            gss_release_cred(&minor_status, &handle->cred_array[i]);
        }
        free(handle->cred_array);
    }

    free(handle);

    GlobusXIOGSIDebugInternalExit();
    return;
}

/*
 * Check if a security token is a pure SSL token or if it was wrapped and
 * calculate it's length - internal use only
 */

static
globus_bool_t
globus_l_xio_gsi_is_ssl_token(
    void *                              token,
    globus_size_t *                     length)
{
    unsigned char * t = (unsigned char *) token;
    globus_bool_t                       result;

    GlobusXIOName(globus_l_xio_gsi_is_ssl_token);
    GlobusXIOGSIDebugInternalEnter();

    if(t[0] >= 20 &&
       t[0] <= 26 &&
       t[1] == 3)
    {
        /* it's a SSL token */
        *length = (t[3] << 8) | t[4];
        *length += 5;
        result = GLOBUS_TRUE;
    }
    else
    {
        /* it's wrapped */
        *length = ((globus_size_t) (*((t)++))) << 24;         
        *length |= ((globus_size_t) (*((t)++))) << 16;
        *length |= ((globus_size_t) (*((t)++))) << 8;
        *length |= ((globus_size_t) (*((t)++)));
        result = GLOBUS_FALSE;
    }
    
    GlobusXIOGSIDebugInternalExit();
    return result;
}

/*
 * Fill out a user supplied iovec with plaintext from a unwrapped buffer -
 * internal only
 */

static globus_result_t
globus_l_xio_gsi_unwrapped_buffer_to_iovec(
    globus_l_handle_t *                 handle,
    globus_size_t *                     bytes_read)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    GlobusXIOName(globus_l_xio_gsi_unwrapped_buffer_to_iovec);
    GlobusXIOGSIDebugInternalEnter();


    /* bytes_read keeps track of the number of bytes transferred */
    
    *bytes_read = 0;

    /* move data as long as there are more iovecs to fill */
    
    for( ; handle->user_iovec_index < handle->user_iovec_count;
         handle->user_iovec_index++)
    {
        /* if the iovec can hold all remaining unwrapped data fill it and
         * return
         */
        if(handle->user_iovec[handle->user_iovec_index].iov_len -
           handle->user_iovec_offset >= handle->unwrapped_buffer_length -
           handle->unwrapped_buffer_offset)
        {
            *bytes_read += handle->unwrapped_buffer_length -
                handle->unwrapped_buffer_offset;
            memcpy((globus_byte_t *) 
                   handle->user_iovec[handle->user_iovec_index].iov_base +
                   handle->user_iovec_offset,
                   &handle->unwrapped_buffer[handle->unwrapped_buffer_offset],
                   handle->unwrapped_buffer_length -
                   handle->unwrapped_buffer_offset);
            handle->user_iovec_offset += handle->unwrapped_buffer_length -
                handle->unwrapped_buffer_offset;
            /* reset variables */
            handle->unwrapped_buffer_offset = 0;
            handle->unwrapped_buffer_length = 0;
            free(handle->unwrapped_buffer);
            handle->unwrapped_buffer = NULL;
            goto done;
        }
        /* else fill it and continue */
        else
        {
            memcpy((globus_byte_t *)
                   handle->user_iovec[handle->user_iovec_index].iov_base +
                   handle->user_iovec_offset,
                   &handle->unwrapped_buffer[handle->unwrapped_buffer_offset],
                   handle->user_iovec[handle->user_iovec_index].iov_len -
                   handle->user_iovec_offset);
            *bytes_read +=
                (handle->user_iovec[handle->user_iovec_index].iov_len -
                 handle->user_iovec_offset);
            handle->unwrapped_buffer_offset +=
                (handle->user_iovec[handle->user_iovec_index].iov_len -
                 handle->user_iovec_offset);
            handle->user_iovec_offset = 0; 
        }
    }

 done:
    GlobusXIOGSIDebugPrintf(
        GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
        (_XIOSL("[%s:%d] Transferred %d bytes\n"), _xio_name,
         handle->connection_id,*bytes_read));

    GlobusXIOGSIDebugInternalExit();
    return result;
}

/*
 * Fill out a user supplied iovec with plaintext from a wrapped buffer. Should
 * only be called if we are out of unwrapped data - internal only
 */
static globus_result_t
globus_l_xio_gsi_wrapped_buffer_to_iovec(
    globus_l_handle_t *                 handle,
    globus_size_t *                     bytes_read,
    globus_size_t                       offset,
    globus_size_t                       frame_length)
{
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    gss_buffer_desc                     wrapped_buf;
    gss_buffer_desc                     unwrapped_buf;
    globus_result_t                     result;
    int                                 conf_state = 0;
    gss_qop_t                           qop_state = GSS_C_QOP_DEFAULT;

    GlobusXIOName(globus_l_xio_gsi_wrapped_buffer_to_iovec);
    GlobusXIOGSIDebugInternalEnter();

    /* unwrap */
    
    wrapped_buf.value = &handle->read_buffer[offset];
    wrapped_buf.length = frame_length;

    major_status = gss_unwrap(&minor_status,
                              handle->context,
                              &wrapped_buf,
                              &unwrapped_buf,
                              &conf_state,
                              &qop_state);

    if(GSS_ERROR(major_status))
    {
        result = GlobusXIOErrorWrapGSSFailed("gss_unwrap",
                                             major_status,
                                             minor_status);
        GlobusXIOGSIDebugInternalExitWithError();
        return result;
    }

    handle->unwrapped_buffer = unwrapped_buf.value;
    handle->unwrapped_buffer_length = unwrapped_buf.length;
    handle->unwrapped_buffer_offset = 0;

    /* fill */
    
    result = globus_l_xio_gsi_unwrapped_buffer_to_iovec(handle,
                                                        bytes_read);
    GlobusXIOGSIDebugInternalExit();
    return result;
}

/* Write callback received after writing a security token. Checks the
 * result and registers another read unless the handle indicates we are
 * done - internal only
 */
static void
globus_l_xio_gsi_write_token_cb(
    struct globus_i_xio_op_s *          op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_handle_t *                 handle;
    globus_xio_iovec_t *                iovec;
    int                                 iovec_count;
    globus_size_t                       wait_for;
    gss_buffer_desc                     tmp_buffer;
    OM_uint32                           minor_status;

    GlobusXIOName(globus_l_xio_gsi_write_token_cb);
    GlobusXIOGSIDebugInternalEnter();

    handle = (globus_l_handle_t *) user_arg;
     
    GlobusXIOGSIDebugPrintf(
        GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
        (_XIOSL("[%s:%d] Wrote token of length %d\n"), _xio_name,
         handle->connection_id,nbytes));
    
    /* read iovec was used to write a sec token */
    
    tmp_buffer.length = handle->read_iovec[1].iov_len;
    tmp_buffer.value = handle->read_iovec[1].iov_base;

    gss_release_buffer(&minor_status, &tmp_buffer);

    /* reset the read_iovec */
    
    handle->read_iovec[1].iov_base = handle->read_buffer;
    handle->read_iovec[1].iov_len = handle->attr->buffer_size;

    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailedWithMessage(result,
            "The GSI XIO driver failed to establish a secure connection.  %s",
            "The failure occured during a handshake write.");
        goto error_pass_close;
    }
    
    if(handle->done == GLOBUS_TRUE)
    {
        /* done */
        if(handle->result_obj != NULL)
        {
            goto error_pass_close;
        }
        
        GlobusXIOGSIDebugPrintf(
            GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
            (_XIOSL("[%s:%d] Done with security handshake\n"), _xio_name,
             handle->connection_id));
        
        globus_xio_driver_finished_open(handle, op, result);
        GlobusXIOGSIDebugInternalExit();
        return;
    }
    else
    {
        GlobusXIOGSIDebugPrintf(
            GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
            (_XIOSL("[%s:%d] Trying to read another token\n"), _xio_name,
             handle->connection_id));

        /* read another sec token */
        iovec = &(handle->read_iovec[1]);
        iovec_count = 1;
        /* ssl record header is 5 bytes */
        wait_for = 5;
        handle->bytes_read = 0;

        result = globus_xio_driver_pass_read(op, iovec, iovec_count, wait_for,
                                globus_l_xio_gsi_read_token_cb, handle);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailedWithMessage(result,
                "The GSI XIO driver failed to establish a secure connection. %s",
                "The failure occured while posting a handshake read.");
            goto error_pass_close;
        }
    }
    
    GlobusXIOGSIDebugInternalExit();
    return;
    
 error_pass_close:
    if(handle->result_obj == NULL)
    { 
        handle->result_obj = globus_error_get(result);
    }
    
    if(globus_xio_driver_pass_close(
           op,
           globus_l_xio_gsi_close_cb, handle) != GLOBUS_SUCCESS)
    {
        result = globus_error_put(handle->result_obj);
        handle->result_obj = NULL;
    
        globus_l_xio_gsi_handle_destroy(handle);
        globus_xio_driver_finished_open(NULL, op, result);

    }
    GlobusXIOGSIDebugInternalExitWithError();
    return;
}



/* Read callback received after reading a security token. Checks the
 * result, passes the read token to the right gss call and starts a write if
 * the gss call did emit another token. - internal only
 */
static
void
globus_l_xio_gsi_read_token_cb(
    struct globus_i_xio_op_s *          op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_handle_t *                 handle = user_arg;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           minor_status = GLOBUS_SUCCESS;
    gss_buffer_desc 		        output_token = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc 		        input_token = {.value = NULL};
    globus_xio_iovec_t *                iovec = NULL;
    int                                 iovec_count = 0;
    globus_size_t                       wait_for = 0;
    globus_size_t                       offset = 0;
    globus_size_t                       prev_offset = 0;
    int                                 header = 0;
    
    GlobusXIOName(globus_l_xio_gsi_read_token_cb);
    GlobusXIOGSIDebugInternalEnter();

    if (result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailedWithMessage(result,
            "The GSI XIO driver failed to establish a secure connection. %s",
            "The failure occured during a handshake read.");
        if(globus_xio_error_is_eof(result) == GLOBUS_TRUE)
        {
            handle->eof = GLOBUS_TRUE;
            handle->result_obj = globus_error_get(result);
        }
        else
        { 
            goto error_pass_close;
        }
    }

    handle->bytes_read += nbytes;

    if (handle->bytes_read <= offset + 5)
    {
        /* Not enough of token header to do anything */
        major_status = GSS_S_CONTINUE_NEEDED;
    }
    else
    {
        do
        {
            /* if it is not a ssl token we have a header */
            if(globus_l_xio_gsi_is_ssl_token(&handle->read_buffer[offset],
                                             &wait_for) ==
               GLOBUS_FALSE)
            {
                header = 4;
            }
            else
            {
                header = 0;
            }

            if(wait_for > MAX_TOKEN_LENGTH)
            {
                result = GlobusXioGSIErrorTokenTooBig();
                goto error_pass_close;
            }        

            prev_offset = offset;
            offset = offset + wait_for + header;
        
            GlobusXIOGSIDebugPrintf(
                GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
                (_XIOSL("[%s:%d] Bytes in buffer %d;bytes wanted %d\n"), _xio_name,
                 handle->connection_id, handle->bytes_read, offset));
        
            /* read more if we have not received a full token or in the case of non
             * wrapped ssl at least a full record.
             */
        
            if(offset > handle->bytes_read)
            {
                if(handle->eof == GLOBUS_FALSE)
                { 
                    handle->bytes_read -= prev_offset;
                    memmove(handle->read_buffer,
                            &handle->read_buffer[prev_offset],
                            handle->bytes_read);

                    offset -= prev_offset;
                    
                    /* grow read buffer so we can read a full token */
                    
                    if(offset > handle->attr->buffer_size)
                    {
                        unsigned char *     tmp_ptr;
                        
                        tmp_ptr = realloc(handle->read_buffer,
                                          offset);
                        if(!tmp_ptr)
                        {
                            result = GlobusXIOErrorMemory("handle->read_buffer");
                            goto error_pass_close;
                        }
                        
                        handle->attr->buffer_size = offset;
                        handle->read_buffer = tmp_ptr;
                    }

                    handle->read_iovec[1].iov_base =
                        &(handle->read_buffer[handle->bytes_read]);
                    handle->read_iovec[1].iov_len =
                        handle->attr->buffer_size - handle->bytes_read;
                    iovec = &(handle->read_iovec[1]);
                    iovec_count = 1;
                    
                    result = globus_xio_driver_pass_read(
                        op, iovec, iovec_count,
                        offset - handle->bytes_read,
                        globus_l_xio_gsi_read_token_cb, handle);
                    if(result != GLOBUS_SUCCESS)
                    {
                        result = GlobusXIOErrorWrapFailedWithMessage(result,
                "The GSI XIO driver failed to establish a secure connection. %s",
                "The failure occured while posting a handshake read.");
                        goto error_pass_close;
                    }
                    GlobusXIOGSIDebugInternalExit();
                    return;
                }
                else
                {
                    goto error_pass_close;
                }
            }
            else
            {
                input_token.length = wait_for;
                input_token.value = &handle->read_buffer[offset - wait_for];
            }

            GlobusXIOGSIDebugPrintf(
                GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
                (_XIOSL("[%s:%d] Read input token of length %d\n"), _xio_name,
                 handle->connection_id, input_token.length));
        
            /* init/accept sec context */
        
            if(handle->attr->init == GLOBUS_TRUE)
            {
                major_status = gss_init_sec_context(
                    &minor_status,
                    handle->attr->credential,
                    &handle->context,
                    handle->attr->target_name,
                    handle->attr->mech_type,
                    handle->attr->req_flags,
                    handle->attr->time_req, 
                    handle->attr->channel_bindings,
                    &input_token,
                    &handle->mech_used,
                    &output_token,
                    &handle->ret_flags,
                    &handle->time_rec);
            }
            else
            {
                if (handle->context == GSS_C_NO_CONTEXT)
                {
                    if (handle->attr->credentials_dir != NULL)
                    {
                        major_status = globus_gss_assist_read_vhost_cred_dir(
                            &minor_status,
                            handle->attr->credentials_dir,
                            &handle->cred_array,
                            &handle->cred_array_length);

                        if (!GSS_ERROR(major_status))
                        {
                            major_status = gss_set_sec_context_option(
                                &minor_status,
                                &handle->context,
                                &GSS_SNI_CREDENTIALS_OID,
                                &(gss_buffer_desc)
                                {
                                    .value = handle->cred_array,
                                    .length = handle->cred_array_length,
                                });
                        }
                    }
                    if (!GSS_ERROR(major_status)
                        && handle->attr->alpn_list != NULL)
                    {
                        /* Don't worry about errors here,
                         * OpenSSL < 1.0.2 doesn't support ALPN 
                         */
                        gss_set_sec_context_option(
                            &minor_status,
                            &handle->context,
                            &GSS_ALPN_OID,
                            &(gss_buffer_desc)
                            {
                                .value = handle->attr->alpn_list,
                                .length = handle->attr->alpn_list_len,
                            });
                    }
                }
                if (!GSS_ERROR(major_status))
                {
                    major_status = gss_accept_sec_context(
                        &minor_status,
                        &handle->context,
                        handle->attr->credential,
                        &input_token,
                        handle->attr->channel_bindings,
                        &handle->peer_name,
                        &handle->mech_used,
                        &output_token,
                        &handle->ret_flags,
                        &handle->time_rec,
                        &handle->delegated_cred);
                }
            }

            GlobusXIOGSIDebugPrintf(
                GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
                (_XIOSL("[%s:%d] Generated output token of length %d\n"), _xio_name,
                 handle->connection_id, output_token.length));
        
            if(GSS_ERROR(major_status))
            {
                result = GlobusXIOErrorWrapGSSFailed(
                    handle->attr->init == GLOBUS_TRUE ? "gss_init_sec_context" :
                    "gss_accept_sec_context",
                    major_status,
                    minor_status);
            
                /* if we have a output token try to send it */
                if(output_token.length == 0)
                {
                    if(handle->result_obj)
                    {
                        globus_object_free(handle->result_obj);
                        handle->result_obj = NULL;
                    }
                    goto error_pass_close;
                }
                else
                {
                    if(handle->result_obj)
                    {
                        globus_object_free(handle->result_obj);
                    }
                    handle->result_obj = globus_error_get(result);
                    handle->done = GLOBUS_TRUE;
                }
            }
        }
        while(major_status == GSS_S_CONTINUE_NEEDED &&
              output_token.length == 0 &&
              handle->bytes_read > offset + 5);
    }
        
    if(major_status == GSS_S_COMPLETE)
    {
        /* get the wrap size limit and peer and local names */
        handle->done = GLOBUS_TRUE;

        if (handle->attr->prot_level ==
           GLOBUS_XIO_GSI_PROTECTION_LEVEL_PRIVACY &&
           !(handle->ret_flags & GSS_C_CONF_FLAG))
        {
            result = GlobusXioGSIErrorBadProtectionLevel();
            goto error_pass_close;
        }
        
        major_status = gss_wrap_size_limit(
            &minor_status,
            handle->context,
            handle->attr->prot_level ==
            GLOBUS_XIO_GSI_PROTECTION_LEVEL_PRIVACY,
            GSS_C_QOP_DEFAULT,
            (4294967295U),
            &handle->max_wrap_size);
        if(GSS_ERROR(major_status))
        {
            result = GlobusXIOErrorWrapGSSFailed("gss_wrap_size_limit",
                                                 major_status,
                                                 minor_status);
            goto error_pass_close;
        }
        
        if(handle->attr->init == GLOBUS_TRUE)
        { 
            major_status = gss_inquire_context(&minor_status,
                                               handle->context,
                                               &handle->local_name,
                                               &handle->peer_name,
                                               GLOBUS_NULL,
                                               GLOBUS_NULL,
                                               GLOBUS_NULL,
                                               GLOBUS_NULL,
                                               GLOBUS_NULL);
            if(GSS_ERROR(major_status))
            {
                result = GlobusXIOErrorWrapGSSFailed("gss_inquire_context",
                                                     major_status,
                                                     minor_status);
                goto error_pass_close;
            }
        }
        else
        {
            major_status = gss_inquire_context(&minor_status,
                                               handle->context,
                                               GLOBUS_NULL,
                                               &handle->local_name,
                                               GLOBUS_NULL,
                                               GLOBUS_NULL,
                                               GLOBUS_NULL,
                                               GLOBUS_NULL,
                                               GLOBUS_NULL);
            if(GSS_ERROR(major_status))
            {
                result = GlobusXIOErrorWrapGSSFailed("gss_inquire_context",
                                                     major_status,
                                                     minor_status);
                goto error_pass_close;
            }

            /* Do authorization here */
            if(handle->attr->target_name != GSS_C_NO_NAME)
            {
                int equal;
                gss_buffer_desc target_namebuf = {0};
                gss_OID target_oid;
                
                major_status = 
                    gss_compare_name(&minor_status,
                                     handle->peer_name,
                                     handle->attr->target_name,
                                     &equal);

                if(GSS_ERROR(major_status))
                {
                    result = GlobusXIOErrorWrapGSSFailed("gss_compare_name",
                                                         major_status,
                                                         minor_status);
                }

                if (!equal)
                {
                    /* RFC 2743: 2.4.3
                     * If either name presented to GSS_Compare_name() denotes
                     * an anonymous principal, GSS_Compare_name() shall
                     * indicate FALSE.
                     *
                     * GT2 version of GSSAPI incorrectly returned TRUE. In
                     * strict mode, we get false, but, if our target is
                     * anonymous, we don't care what the peer is.
                     */
                    major_status = gss_display_name(
                            &minor_status,
                            handle->attr->target_name,
                            &target_namebuf,
                            &target_oid);
                    gss_release_buffer(&minor_status, &target_namebuf);
                    if(GSS_ERROR(major_status))
                    {
                        result = GlobusXIOErrorWrapGSSFailed("gss_display_name",
                                                             major_status,
                                                             minor_status);
                        goto error_pass_close;
                    }
                    if (target_oid != GSS_C_NO_OID
                        && target_oid->length == GSS_C_NT_ANONYMOUS->length
                        && memcmp(target_oid->elements,
                                GSS_C_NT_ANONYMOUS->elements,
                                target_oid->length) == 0)
                    {
                        result = GLOBUS_SUCCESS;
                        equal = GLOBUS_TRUE;
                    }
                }

                if(!equal)
                {
                    char *              expected_name;
                    char *              actual_name;
                    gss_buffer_desc     name_buffer;

                    major_status = gss_display_name(&minor_status,
                                                    handle->peer_name,
                                                    &name_buffer,
                                                    NULL);
                    
                    if(GSS_ERROR(major_status))
                    {
                        result = GlobusXIOErrorWrapGSSFailed("gss_display_name",
                                                             major_status,
                                                             minor_status);
                        goto error_pass_close;
                    }

                    actual_name = name_buffer.value;

                    major_status = gss_display_name(&minor_status,
                                                    handle->attr->target_name,
                                                    &name_buffer,
                                                    NULL);
                    
                    if(GSS_ERROR(major_status))
                    {
                        free(actual_name);
                        result = GlobusXIOErrorWrapGSSFailed("gss_display_name",
                                                             major_status,
                                                             minor_status);
                        goto error_pass_close;
                    }

                    expected_name = name_buffer.value;
                    
                    
                    result = GlobusXioGSIAuthorizationFailed(actual_name, expected_name);
                    free(expected_name);
                    free(actual_name);
                    goto error_pass_close;
                }                
            }            
        }        
    }
    else if(handle->eof == GLOBUS_TRUE)
    {
        goto error_pass_close;
    }

    /* deal with excess data */
    
    if(offset < handle->bytes_read)
    {
        handle->bytes_read -= offset;
        memmove(handle->read_buffer,
                &handle->read_buffer[offset],
                handle->bytes_read);
    }
    else
    {
        handle->bytes_read = 0;
    }

    if(output_token.length != 0 && handle->eof == GLOBUS_FALSE)
    {
        /* send the output token */
        if(handle->attr->wrap_tokens == GLOBUS_TRUE)
        {
            iovec = handle->read_iovec;
            iovec_count = 2;
            GlobusLXIOGSICreateHeader(iovec[0], output_token.length);
            
            /* needs to be reset */
            
            iovec[1].iov_len = output_token.length;
            iovec[1].iov_base = output_token.value;
            
            wait_for = iovec[0].iov_len + iovec[1].iov_len;
        }
        else
        {
            iovec = &(handle->read_iovec[1]);
            iovec_count = 1;
            iovec[0].iov_len = output_token.length;
            iovec[0].iov_base = output_token.value;
            wait_for = iovec[0].iov_len;
        }
        
        result = globus_xio_driver_pass_write(op, iovec, iovec_count, wait_for,
                                 globus_l_xio_gsi_write_token_cb, handle);
        if(result != GLOBUS_SUCCESS)
        {
            gss_release_buffer(&minor_status, &output_token);
            handle->read_iovec[1].iov_base = handle->read_buffer;
            handle->read_iovec[1].iov_len = handle->attr->buffer_size;
            goto error_pass_close;
        }
    }
    else if(handle->done == GLOBUS_TRUE)
    {
        /* we're done */
        GlobusXIOGSIDebugPrintf(
            GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
            (_XIOSL("[%s:%d] Done with security handshake\n"), _xio_name,
             handle->connection_id));

        globus_xio_driver_finished_open(handle, op, result);
    }
    else
    {
        /* read another token */
        handle->read_iovec[1].iov_base =
            &(handle->read_buffer[handle->bytes_read]);
        handle->read_iovec[1].iov_len =
            handle->attr->buffer_size - handle->bytes_read;
        iovec = &(handle->read_iovec[1]);
        iovec_count = 1;

        if(handle->bytes_read < 5)
        { 
            /* ssl record header is 5 bytes */
            wait_for = 5 - handle->bytes_read;
        }
        else
        {
            wait_for = 0;
        }
        
        result = globus_xio_driver_pass_read(op, iovec, iovec_count, wait_for,
                                globus_l_xio_gsi_read_token_cb, handle);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailedWithMessage(result,
               "The GSI XIO driver failed to establish a secure connection. %s",
               "The failure occured while posting a handshake read.");
            goto error_pass_close;
        }
    }
    GlobusXIOGSIDebugInternalExit();
    return;

 error_pass_close:
    if(handle->result_obj == NULL)
    { 
        handle->result_obj = globus_error_get(result);
    }
                
    if(globus_xio_driver_pass_close(
           op,globus_l_xio_gsi_close_cb, handle) != GLOBUS_SUCCESS)
    {
        result = globus_error_put(handle->result_obj);
        handle->result_obj = NULL;
        
        globus_l_xio_gsi_handle_destroy(handle);
        globus_xio_driver_finished_open(NULL, op, result);
    }
    GlobusXIOGSIDebugInternalExitWithError();
    return;
}

/*
 *  Open callback. Calls the intial init sec context and sends the output token
 *  or reads the initial token for the accept side - internal only
 */
static
void
globus_l_xio_gsi_open_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_handle_t *                 handle;
    globus_xio_iovec_t *                iovec;
    int                                 iovec_count;
    globus_size_t                       wait_for;
    
    GlobusXIOName(globus_l_xio_gsi_open_cb);
    GlobusXIOGSIDebugInternalEnter();

    handle = (globus_l_handle_t *) user_arg;
    
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailedWithMessage(result,
            "The GSI XIO driver failed to establish a connection%s",
            " via the underlying protocol.");
        goto error_destroy_handle;
    }
    
    result = globus_l_xio_gsi_setup_target_name(handle);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_pass_close;
    }
    
    if(handle->attr->init == GLOBUS_TRUE)
    {
        OM_uint32                       major_status = GSS_S_COMPLETE;
        OM_uint32                       minor_status = GLOBUS_SUCCESS;
        gss_buffer_desc 	        output_token = GSS_C_EMPTY_BUFFER;

        if (handle->context == GSS_C_NO_CONTEXT
            && handle->attr->alpn_list != NULL)
        {
            /* Don't worry about errors here,
             * OpenSSL < 1.0.2 doesn't support ALPN 
             */
            gss_set_sec_context_option(
                &minor_status,
                &handle->context,
                &GSS_ALPN_OID,
                &(gss_buffer_desc)
                {
                    .value = handle->attr->alpn_list,
                    .length = handle->attr->alpn_list_len,
                });
        }
        if (!GSS_ERROR(major_status))
        {
            major_status = gss_init_sec_context(
                &minor_status,
                handle->attr->credential,
                &handle->context,
                handle->attr->target_name,
                handle->attr->mech_type,
                handle->attr->req_flags,
                handle->attr->time_req, 
                handle->attr->channel_bindings,
                GSS_C_NO_BUFFER,
                &handle->mech_used,
                &output_token,
                &handle->ret_flags,
                &handle->time_rec);
        }
        GlobusXIOGSIDebugPrintf(
            GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
            (_XIOSL("[%s:%d] Generated output token of length %d\n"), _xio_name,
             handle->connection_id, output_token.length));

        if(GSS_ERROR(major_status))
        {
            result = GlobusXIOErrorWrapGSSFailed("gss_init_sec_context",
                                                 major_status,
                                                 minor_status);

            if(output_token.length == 0)
            {
                goto error_pass_close;
            }
            else
            {
                handle->result_obj = globus_error_get(result);
                handle->done = GLOBUS_TRUE;
            }
        }
        else if(major_status == GSS_S_COMPLETE)
        {
            /* if we are already done, deal with it here */
            handle->done = GLOBUS_TRUE;

            major_status = gss_wrap_size_limit(
                &minor_status,
                handle->context,
                handle->attr->prot_level ==
                GLOBUS_XIO_GSI_PROTECTION_LEVEL_PRIVACY,
                GSS_C_QOP_DEFAULT,
                (4294967295U),
                &handle->max_wrap_size);

            if(GSS_ERROR(major_status))
            {
                result = GlobusXIOErrorWrapGSSFailed("gss_wrap_size_limit",
                                                     major_status,
                                                     minor_status);
                if(output_token.length != 0)
                {
                    gss_release_buffer(&minor_status, &output_token);
                }
                goto error_pass_close;
            }

            major_status = gss_inquire_context(&minor_status,
                                               handle->context,
                                               &handle->local_name,
                                               &handle->peer_name,
                                               GLOBUS_NULL,
                                               GLOBUS_NULL,
                                               GLOBUS_NULL,
                                               GLOBUS_NULL,
                                               GLOBUS_NULL);
            if(GSS_ERROR(major_status))
            {
                result = GlobusXIOErrorWrapGSSFailed("gss_inquire_context",
                                                     major_status,
                                                     minor_status);

                if(output_token.length != 0)
                {
                    gss_release_buffer(&minor_status, &output_token);
                }
                goto error_pass_close;
            }
        }
        
        if(handle->attr->wrap_tokens == GLOBUS_TRUE)
        {
            iovec = handle->read_iovec;
            iovec_count = 2;
             GlobusLXIOGSICreateHeader(iovec[0], output_token.length);

            /* needs to be reset once I start reading */
            
            iovec[1].iov_len = output_token.length;
            iovec[1].iov_base = output_token.value;

            wait_for = iovec[0].iov_len + iovec[1].iov_len;
        }
        else
        {
            iovec = &(handle->read_iovec[1]);
            iovec_count = 1;
            iovec[0].iov_len = output_token.length;
            iovec[0].iov_base = output_token.value;
            wait_for = iovec[0].iov_len;
        }
        
        result = globus_xio_driver_pass_write(op, iovec, iovec_count, wait_for,
                                 globus_l_xio_gsi_write_token_cb, handle);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailedWithMessage(result,
                "The GSI XIO driver failed to post a write%s",
                ".");
            gss_release_buffer(&minor_status, &output_token);
            handle->read_iovec[1].iov_base = handle->read_buffer;
            handle->read_iovec[1].iov_len = handle->attr->buffer_size;
            goto  error_pass_close;
        }
    }
    else
    {
        /* read first token */
        iovec = &(handle->read_iovec[1]);
        iovec_count = 1;
        /* ssl record header is 5 bytes */
        wait_for = 5;

        result = globus_xio_driver_pass_read(op, iovec, iovec_count, wait_for,
                                globus_l_xio_gsi_read_token_cb, handle);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailedWithMessage(result,
                "The GSI XIO driver failed to post a read %s",
                "during the secure handshake.");
            goto  error_pass_close;
        }
    }
    GlobusXIOGSIDebugInternalExit();
    return;

 error_pass_close:
    if(handle->result_obj == NULL)
    { 
        handle->result_obj = globus_error_get(result);
    }
                
    if(globus_xio_driver_pass_close(
           op, globus_l_xio_gsi_close_cb, handle) == GLOBUS_SUCCESS)
    {
        GlobusXIOGSIDebugInternalExitWithError();
        return;
    }
    
    result = globus_error_put(handle->result_obj);
    handle->result_obj = NULL;
    
 error_destroy_handle:
    globus_l_xio_gsi_handle_destroy(handle);
    globus_xio_driver_finished_open(NULL, op, result);
    GlobusXIOGSIDebugInternalExitWithError();
    return;
}   

/* open interface function. just initializes the handle and passes */

static
globus_result_t
globus_l_xio_gsi_open(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_result_t                     result;
    globus_l_handle_t *                 handle;
    
    GlobusXIOName(globus_l_xio_gsi_open);
    GlobusXIOGSIDebugEnter();
    
    handle = calloc(1, sizeof(globus_l_handle_t));
    if(!handle)
    {
        result = GlobusXIOErrorMemory("handle");
        goto error;
    }
    
    if(driver_attr)
    {
        result = globus_l_xio_gsi_attr_copy((void **) (void *) &handle->attr,
                                            driver_attr);
        if(result == GLOBUS_SUCCESS && driver_link)
        {
            handle->attr->init = GLOBUS_FALSE;
        }
    }
    else if(driver_link)
    {
        result = globus_l_xio_gsi_attr_copy((void **) (void *) &handle->attr,
                                            driver_link);
    }
    else
    {
        result = globus_l_xio_gsi_attr_copy(
            (void **) (void *) &handle->attr,
            (void *) &globus_l_xio_gsi_attr_default);        
    }
    
    if(result != GLOBUS_SUCCESS)
    {
        free(handle);
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_gsi_attr_copy", result);
        goto error;
    }

    handle->context = GSS_C_NO_CONTEXT;
    handle->delegated_cred = GSS_C_NO_CREDENTIAL;
    handle->credential = GSS_C_NO_CREDENTIAL;
    handle->peer_name = GSS_C_NO_NAME;
    handle->local_name = GSS_C_NO_NAME;
    handle->done = GLOBUS_FALSE;
    handle->eof = GLOBUS_FALSE;
    handle->read_buffer = malloc(handle->attr->buffer_size);

    globus_mutex_lock(&connection_mutex);
    {
        handle->connection_id = connection_count;
        connection_count++;
    }
    globus_mutex_unlock(&connection_mutex);
    
    if(!handle->read_buffer)
    {
        globus_l_xio_gsi_attr_destroy(handle->attr);
        free(handle);
        result = GlobusXIOErrorMemory("handle->read_buffer");
        goto error;
    }

    handle->read_iovec[0].iov_len = 4;
    handle->read_iovec[0].iov_base = handle->header;
    handle->read_iovec[1].iov_len = handle->attr->buffer_size;
    handle->read_iovec[1].iov_base = handle->read_buffer;
    
    if(handle->attr->init == GLOBUS_FALSE)
    {
        handle->ret_flags = handle->attr->req_flags;
    }
    
    handle->xio_driver_handle = globus_xio_operation_get_driver_handle(op);
    if (contact_info->host)
    {
        handle->host_name = globus_libc_strdup(contact_info->host);
        if (handle->host_name == NULL)
        {
            globus_l_xio_gsi_handle_destroy(handle);
            result = GlobusXIOErrorMemory("handle->host_name");
            goto error;
        }
    }

    result = globus_xio_driver_pass_open(
        op, contact_info, globus_l_xio_gsi_open_cb, handle);

    if(result != GLOBUS_SUCCESS)
    {
        globus_l_xio_gsi_handle_destroy(handle);
        goto error;
    }

    GlobusXIOGSIDebugExit();
    return GLOBUS_SUCCESS;
    
 error:
    GlobusXIOGSIDebugExitWithError();
    return result;
}

/*
 *  close callback. Only called upon failure in open. Destroys the handle.
 */
static
void
globus_l_xio_gsi_close_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{   
    globus_l_handle_t *                 handle;
    
    GlobusXIOName(globus_l_xio_gsi_close_cb);
    GlobusXIOGSIDebugInternalEnter();

    handle = (globus_l_handle_t *) user_arg;

    if(handle->result_obj)
    {
        result = globus_error_put(handle->result_obj);
        handle->result_obj = NULL;
    }
    
    globus_xio_driver_finished_open(NULL, op, result);

    globus_l_xio_gsi_handle_destroy(handle);
    GlobusXIOGSIDebugInternalExit();
    return;
}

/*
 * close interface function. Destroys handle and passes null callback.
 */

static
globus_result_t
globus_l_xio_gsi_close(
    void *                              driver_specific_handle,
    void *                              attr,
    globus_xio_operation_t              op)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    GlobusXIOName(globus_l_xio_gsi_close);
    GlobusXIOGSIDebugEnter();
    
    if(!driver_specific_handle)
    {
        GlobusXIOGSIDebugExitWithError();
        return GlobusXIOErrorParameter("driver_specific_handle");
    }
    
    globus_l_xio_gsi_handle_destroy(
        (globus_l_handle_t *) driver_specific_handle);
    
    result = globus_xio_driver_pass_close(op, NULL, NULL);
    GlobusXIOGSIDebugExit();
    return result;
}

/*
 *  read callback - internal only
 */
static
void
globus_l_xio_gsi_read_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_handle_t *                 handle;
    globus_size_t                       wait_for;
    globus_size_t                       bytes_read = 1;
    globus_size_t                       frame_length;
    globus_size_t                       offset;
    globus_size_t                       header;
    globus_bool_t                       ssl_record;
    globus_bool_t                       no_header = GLOBUS_FALSE;

    GlobusXIOName(globus_l_xio_gsi_read_cb);
    GlobusXIOGSIDebugInternalEnter();

    handle = (globus_l_handle_t *) user_arg;

    GlobusXIOGSIDebugPrintf(
        GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
        (_XIOSL("[%s:%d] Read %d bytes\n"),
         _xio_name, handle->connection_id, nbytes));
    
    if(result != GLOBUS_SUCCESS)
    {
        if(globus_xio_error_is_eof(result) == GLOBUS_TRUE)
        {
            handle->eof = GLOBUS_TRUE;
        }
        else if(nbytes == 0)
        {
            /* only error if error isn't eof and we haven't read anything */
            goto error;
        }
        
        /* save result */
        handle->result_obj = globus_error_get(result);
        result = GLOBUS_SUCCESS;
    }

    /* if we aren't using security, just transfer the buffer */
    /* no need to check wait_for since we should have read enough */
    
    if(handle->attr->prot_level == GLOBUS_XIO_GSI_PROTECTION_LEVEL_NONE) 
    {
        handle->unwrapped_buffer_length = nbytes;
        
        result = globus_l_xio_gsi_unwrapped_buffer_to_iovec(
            handle,&bytes_read);
        /* above will return result from handle */
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_l_xio_gsi_unwrapped_buffer_to_iovec", result);
            goto error;
        }
        else
        {
            handle->bytes_returned += bytes_read;
        }

        GlobusXIOGSIDebugPrintf(
            GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
            (_XIOSL("[%s:%d] Finished (%d bytes returned) \n"),
             _xio_name, handle->connection_id, handle->bytes_returned));

        if(handle->result_obj && handle->unwrapped_buffer == NULL)
        {
            result = globus_error_put(handle->result_obj);
            handle->result_obj = NULL;
        }
        
        globus_xio_driver_finished_read(op, result, handle->bytes_returned);
        GlobusXIOGSIDebugInternalExit();
        return;
    }
    
    /* figure out how much more to read */
    
    wait_for = globus_xio_operation_get_wait_for(op);

    wait_for -= handle->bytes_returned;

    handle->bytes_read += nbytes;

    offset = 0;

    /* check if we need to deal with header */
    
    ssl_record = globus_l_xio_gsi_is_ssl_token(handle->read_buffer,
                                               &frame_length);

    if(ssl_record == GLOBUS_TRUE)
    {
        header = 0;
    }
    else
    {
        header = 4;
    }

    if(frame_length > MAX_TOKEN_LENGTH)
    {
        result = GlobusXioGSIErrorTokenTooBig();
        goto error;
    }
    
    /* while we have full frames convert wrapped data to unwrapped data
       and push it to the user
    */
    
    while(frame_length + offset + header <= handle->bytes_read &&
          (wait_for > 0 || bytes_read > 0) && no_header == GLOBUS_FALSE &&
          result == GLOBUS_SUCCESS && handle->unwrapped_buffer == NULL)
    {
        offset += header;
        
        result = globus_l_xio_gsi_wrapped_buffer_to_iovec(
            handle, &bytes_read, offset,
            frame_length);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_l_xio_gsi_wrapped_buffer_to_iovec", result);
            goto error;
        }

        /* don't let wait_for underflow */
        
        if(wait_for > bytes_read)
        {
            wait_for -= bytes_read;
        }
        else
        {
            wait_for = 0;
        }
        
        handle->bytes_returned += bytes_read;
        
        offset += frame_length;

        /* get the length of the next frame */
        
        if(handle->bytes_read > offset + 5)
        {
            ssl_record = globus_l_xio_gsi_is_ssl_token(
                &handle->read_buffer[offset],
                &frame_length);
            if(ssl_record == GLOBUS_TRUE)
            {
                header = 0;
            }
            else
            {
                header = 4;
            }

            if(frame_length > MAX_TOKEN_LENGTH)
            {
                result = GlobusXioGSIErrorTokenTooBig();
                goto error;
            }
        }
        else
        {
            no_header = GLOBUS_TRUE;
        }
    }

    handle->bytes_read -= offset;

    /* reset the read buffer */
    
    if(handle->bytes_read)
    {
        memmove(handle->read_buffer, &handle->read_buffer[offset],
               handle->bytes_read);
    }

    /* now that we have unwrapped as much as possible check result */

    if(handle->result_obj && wait_for > 0)
    {
        result = globus_error_put(handle->result_obj);
        handle->result_obj = NULL;
    }
    
    if(result == GLOBUS_SUCCESS && wait_for > 0)
    {
        if(no_header == GLOBUS_TRUE)
        {
            /* read at least another header */
            wait_for = 5 - handle->bytes_read;
        }
        else
        {
            /* or the next full frame if we already have the header info */
            wait_for = frame_length - handle->bytes_read + header;

            if(frame_length + header > handle->attr->buffer_size)
            {
                unsigned char *                 tmp_ptr;
                
                tmp_ptr = realloc(handle->read_buffer,
                                         frame_length + header);
                if(!tmp_ptr)
                {
                    result = GlobusXIOErrorMemory("handle->read_buffer");
                    goto error;
                }
                
                handle->attr->buffer_size = frame_length + header;
                handle->read_buffer = tmp_ptr;
            }
        }

        /* set up the iovec for the next read */
        
        handle->read_iovec[1].iov_base =
            &(handle->read_buffer[handle->bytes_read]);
        handle->read_iovec[1].iov_len =
            handle->attr->buffer_size - handle->bytes_read;

        GlobusXIOGSIDebugPrintf(
            GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
            (_XIOSL("[%s:%d] Registering read of size: %d\n"),
             _xio_name, handle->connection_id, wait_for));
        
        result = globus_xio_driver_pass_read(op, &(handle->read_iovec[1]),
                                1, wait_for,
                                globus_l_xio_gsi_read_cb, handle);
    }
    else
    {
        GlobusXIOGSIDebugPrintf(
            GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
            (_XIOSL("[%s:%d] Finished (%d bytes returned) \n"),
             _xio_name, handle->connection_id, handle->bytes_returned));

        /* done with either error or success */
        globus_xio_driver_finished_read(op, result, handle->bytes_returned);
    }
    GlobusXIOGSIDebugInternalExit();
    return;
    
 error:
    globus_xio_driver_finished_read(op, result, handle->bytes_returned);
    GlobusXIOGSIDebugInternalExitWithError();
    return;
}

/* read interface function */

static
globus_result_t
globus_l_xio_gsi_read(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_handle_t *                 handle;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_size_t                       wait_for;
    globus_size_t                       bytes_read = 1;
    globus_size_t                       frame_length = 0;
    globus_size_t                       offset;
    globus_size_t                       header = 0;
    globus_bool_t                       ssl_record;
    globus_bool_t                       no_header = GLOBUS_TRUE;


    GlobusXIOName(globus_l_xio_gsi_read);
    GlobusXIOGSIDebugEnter();

    if(!driver_specific_handle)
    {
        result = GlobusXIOErrorParameter("driver_specific_handle");
        goto error;
    }

    handle = (globus_l_handle_t *) driver_specific_handle;
    
    wait_for = globus_xio_operation_get_wait_for(op);

    GlobusXIOGSIDebugPrintf(
        GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
        (_XIOSL("[%s:%d] Waiting for %d bytes.\n"),
         _xio_name, handle->connection_id, wait_for));

    /* reset read related state */
    
    handle->bytes_returned = 0;
    handle->user_iovec = (globus_xio_iovec_t *)iovec;
    handle->user_iovec_count = iovec_count;
    handle->user_iovec_index = 0;
    handle->user_iovec_offset = 0;
    assert(handle->result_obj == NULL);
    
    /* if we don't get any iovecs and wait_for is greater thant 0 return an
     * error
     */
    
    if(iovec_count < 1)
    {
        if(wait_for > 0)
        {
            result = GlobusXIOErrorParameter("iovec_count");
        }
        goto error;
    }

    /* if we previously have read data (say during end of handshake) move data
     * to unwrapped buffer variable and reset the read buffer
     */
    
    if(handle->bytes_read != 0 &&
       handle->attr->prot_level == GLOBUS_XIO_GSI_PROTECTION_LEVEL_NONE)
    {
        assert(handle->unwrapped_buffer == NULL);
        handle->unwrapped_buffer = handle->read_buffer;
        handle->read_buffer = malloc(handle->attr->buffer_size);

        if(!handle->read_buffer)
        {
            result = GlobusXIOErrorMemory("handle->read_buffer");
            goto error;
        }

        handle->unwrapped_buffer_length = handle->bytes_read;
        handle->bytes_read = 0;
        handle->unwrapped_buffer_offset = 0;
    }

    /* if we have unwrapped data return it to user */
    
    if(handle->unwrapped_buffer_length != 0)
    {
        result = globus_l_xio_gsi_unwrapped_buffer_to_iovec(
            handle, &bytes_read);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_l_xio_gsi_unwrapped_buffer_to_iovec", result);
            goto error;
        }
        
        if(wait_for > bytes_read)
        { 
            wait_for -= bytes_read;
        }
        else
        {
            wait_for = 0;
        }
        
        handle->bytes_returned += bytes_read;

        GlobusXIOGSIDebugPrintf(
            GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
            (_XIOSL("[%s:%d] Transferred previously read/unwrapped bytes: %d\n"),
             _xio_name, handle->connection_id, bytes_read));
    }

    /* deal with any buffered wrapped data */
    
    if(wait_for > 0 && handle->bytes_read != 0)
    {
        if(handle->bytes_read < 5)
        {
            no_header = GLOBUS_TRUE;
        }
        else
        {
            no_header = GLOBUS_FALSE;

            offset = 0;
            
            ssl_record = globus_l_xio_gsi_is_ssl_token(handle->read_buffer,
                                                       &frame_length);
            if(ssl_record == GLOBUS_TRUE)
            {
                header = 0;
            }
            else
            {
                header = 4;
            }

            if(frame_length > MAX_TOKEN_LENGTH)
            {
                result = GlobusXioGSIErrorTokenTooBig();
                goto error;
            }
            
            while(frame_length + offset + header <= handle->bytes_read &&
                  (wait_for > 0 || bytes_read > 0) &&
                  handle->unwrapped_buffer == NULL &&
                  no_header == GLOBUS_FALSE)
            {
                offset += header;
                result = globus_l_xio_gsi_wrapped_buffer_to_iovec(
                    handle, &bytes_read, offset, frame_length);
                if(result != GLOBUS_SUCCESS)
                {
                    result = GlobusXIOErrorWrapFailed(
                        "globus_l_xio_gsi_wrapped_buffer_to_iovec", result);
                    goto error;
                }

                if(wait_for > bytes_read)
                { 
                    wait_for -= bytes_read;
                }
                else
                {
                    wait_for = 0;
                }

                GlobusXIOGSIDebugPrintf(
                    GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
                    (_XIOSL("[%s:%d] Transferred previously read/wrapped bytes: %d\n"),
                     _xio_name, handle->connection_id, bytes_read));
                
                handle->bytes_returned += bytes_read;

                offset += frame_length;
                
                if(handle->bytes_read > offset + 4)
                {
                    ssl_record = globus_l_xio_gsi_is_ssl_token(
                        &handle->read_buffer[offset],
                        &frame_length);
                    if(ssl_record == GLOBUS_TRUE)
                    { 
                        header = 0;
                    }
                    else
                    { 
                        header = 4;
                    }
                    
                    if(frame_length > MAX_TOKEN_LENGTH)
                    {
                        result = GlobusXioGSIErrorTokenTooBig();
                        goto error;
                    }
                }
                else
                {
                    no_header = GLOBUS_TRUE;
                }
            }

            handle->bytes_read -= offset;                
            
            if(handle->bytes_read > 0)
            {
                memmove(handle->read_buffer, &handle->read_buffer[offset],
                       handle->bytes_read);
            }
        }
    }

    GlobusXIOGSIDebugPrintf(
        GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
        (_XIOSL("[%s:%d] Total bytes transferred: %d\n"),
         _xio_name, handle->connection_id, handle->bytes_returned));

    if(handle->result_obj && wait_for > 0)
    {
        result = globus_error_put(handle->result_obj);
        handle->result_obj = NULL;
    }    
    
    if(wait_for == 0 || result != GLOBUS_SUCCESS)
    {
        /* done */
        GlobusXIOGSIDebugPrintf(
            GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
            (_XIOSL("[%s:%d] Finished (%d bytes returned) \n"),
             _xio_name, handle->connection_id, handle->bytes_returned));

        globus_xio_driver_finished_read(op, result, handle->bytes_returned);
    }
    else if(handle->attr->prot_level != GLOBUS_XIO_GSI_PROTECTION_LEVEL_NONE)
    {
        /* read either a new header or a new frame */
        if(no_header == GLOBUS_TRUE)
        {
            wait_for = 4 + wait_for;
        }
        else
        {
            wait_for = frame_length + header;
        }

        if(wait_for > handle->attr->buffer_size)
        {
            unsigned char *             tmp_ptr;
            
            tmp_ptr = realloc(handle->read_buffer,
                              wait_for);
            if(!tmp_ptr)
            {
                result =
                    GlobusXIOErrorMemory("handle->read_buffer");
                goto error;
            }
            
            handle->attr->buffer_size = wait_for;
            handle->read_buffer = tmp_ptr;
        }

        wait_for -= handle->bytes_read;

        handle->read_iovec[1].iov_base =
            &(handle->read_buffer[handle->bytes_read]);
        handle->read_iovec[1].iov_len =
            handle->attr->buffer_size - handle->bytes_read;

        GlobusXIOGSIDebugPrintf(
            GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
            (_XIOSL("[%s:%d] Registering read of size: %d\n"),
             _xio_name, handle->connection_id, wait_for));

        result = globus_xio_driver_pass_read(op, &(handle->read_iovec[1]),
                                             1, wait_for,
                                             globus_l_xio_gsi_read_cb, handle);
    }
    else
    {
        /* if no protection just read as much as we need */
        if(handle->user_iovec_index > 0 ||
           handle->user_iovec_offset > 0)
        { 
            handle->unwrapped_buffer = malloc(wait_for);
            if(!handle->unwrapped_buffer)
            {
                result =
                    GlobusXIOErrorMemory("handle->unwrapped_buffer");
                goto error;            
            }
            handle->unwrapped_buffer_length = wait_for;
            assert(handle->unwrapped_buffer_offset == 0);
            handle->read_iovec[1].iov_base = handle->unwrapped_buffer;
            handle->read_iovec[1].iov_len = wait_for;

            GlobusXIOGSIDebugPrintf(
                GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
                (_XIOSL("[%s:%d] Registering read of size: %d (no protection)\n"),
                 _xio_name, handle->connection_id, wait_for));

            result = globus_xio_driver_pass_read(op, &(handle->read_iovec[1]),
                                    1, wait_for,
                                    globus_l_xio_gsi_read_cb, handle);
        }
        else
        {
            GlobusXIOGSIDebugPrintf(
                GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
                (_XIOSL("[%s:%d] Registering read of size: %d (null callback)\n"),
                 _xio_name, handle->connection_id, wait_for));
            result = globus_xio_driver_pass_read(
                        op, (globus_xio_iovec_t *)iovec,
                                    iovec_count, wait_for, NULL, handle);
        }
    }

    GlobusXIOGSIDebugExit();
    return result;

 error:
    GlobusXIOGSIDebugExitWithError();
    return result;
}

/*
 *  write callback - internal only
 */
void
globus_l_xio_gsi_write_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    int                                 i;
    globus_l_handle_t *                 handle;
    GlobusXIOName(globus_l_xio_gsi_write_cb);
    GlobusXIOGSIDebugInternalEnter();

    handle = (globus_l_handle_t *) user_arg;

    GlobusXIOGSIDebugPrintf(
        GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
        (_XIOSL("[%s:%d] Wrote %d bytes. \n"),
         _xio_name, handle->connection_id, nbytes));


    /* free the memory used for writes */
    
    if(handle->frame_writes == GLOBUS_FALSE)
    { 
        for(i = 0;i < handle->write_iovec_count;i++)
        {
            if(handle->write_iovec[i].iov_base != NULL)
            {
                free(handle->write_iovec[i].iov_base);
                handle->write_iovec[i].iov_base = NULL;
            }
        }
    }
    else
    {
        for(i = 1;i < handle->write_iovec_count;i += 2)
        {
            /* exclude the headers for framed writes */
            if(handle->write_iovec[i].iov_base != NULL)
            {
                free(handle->write_iovec[i].iov_base);
                handle->write_iovec[i].iov_base = NULL;
                handle->write_iovec[i - 1].iov_base = NULL;
            }
        }
    }

    if(result != GLOBUS_SUCCESS &&
       nbytes != globus_xio_operation_get_wait_for(op))
    {
        handle->bytes_written = 0;
    }
    
    globus_xio_driver_finished_write(op, result, handle->bytes_written);
    GlobusXIOGSIDebugInternalExit();
    return;
}

/*
 * write interface function
 */
typedef struct gsi_l_write_bounce_s
{
    void *                              driver_specific_handle;
    int                                 iovec_count;
    globus_xio_operation_t              op;
    globus_xio_iovec_t                  iovec[1];
} gsi_l_write_bounce_t;

static
void
globus_l_xio_gsi_write_bounce(
    void *                              user_arg)
{
    globus_l_handle_t *                 handle;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_size_t                       wait_for;
    gss_buffer_desc                     plaintext_buffer;
    gss_buffer_desc                     wrapped_buffer;
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    globus_size_t                       frame_length;
    int                                 i;
    int                                 j;
    int                                 conf_state;
    globus_size_t                       iovec_offset;
    size_t                              write_iovec_count = 0;
    /* for bounce */
    void *                              driver_specific_handle;
    int                                 iovec_count;
    globus_xio_operation_t              op;
    globus_xio_iovec_t *                iovec;
    gsi_l_write_bounce_t *              bounce;

    GlobusXIOName(globus_l_xio_gsi_write_bounce);
    GlobusXIOGSIDebugEnter();

    bounce = (gsi_l_write_bounce_t *) user_arg;
    driver_specific_handle = bounce->driver_specific_handle;
    op = bounce->op;
    iovec_count = bounce->iovec_count;
    iovec = bounce->iovec;

    handle = (globus_l_handle_t *) driver_specific_handle;

    j = 0;
    while(j < iovec_count && iovec[j].iov_len == 0)
    {
        j++;
    }

    /* wrap first iovec up to max wrap size */
    /* need this info to detect if we are dealing with SSL based GSSAPI */
    
    plaintext_buffer.value = iovec[j].iov_base;

    if(iovec[j].iov_len > handle->max_wrap_size)
    {
        plaintext_buffer.length = handle->max_wrap_size;
        iovec_offset = handle->max_wrap_size;
    }
    else
    { 
        plaintext_buffer.length = iovec[j].iov_len;
        iovec_offset = 0;
    }
    
    major_status = gss_wrap(&minor_status,
                            handle->context,
                            handle->attr->prot_level
                            == GLOBUS_XIO_GSI_PROTECTION_LEVEL_PRIVACY,
                            GSS_C_QOP_DEFAULT,
                            &plaintext_buffer,
                            &conf_state,
                            &wrapped_buffer);

    if(GSS_ERROR(major_status))
    {
        result = GlobusXIOErrorWrapGSSFailed("gss_wrap",
                                             major_status,
                                             minor_status);
        goto error;
    }

    /* figure out how many iovecs I need */

    for(i = 0;i < iovec_count; i++)
    {
        write_iovec_count += iovec[i].iov_len / handle->max_wrap_size;
        if(iovec[i].iov_len % handle->max_wrap_size != 0)
        {
            write_iovec_count++;
        }
        handle->bytes_written += iovec[i].iov_len;
    }

    /* frame any non SSL writes */
    
    if(globus_l_xio_gsi_is_ssl_token(wrapped_buffer.value, &frame_length)
       != GLOBUS_TRUE)
    {
        if(write_iovec_count > handle->write_header_count)
        {
            void *                      tmp_ptr;

            tmp_ptr = realloc(handle->write_headers,
                                     4 * write_iovec_count);

            if(tmp_ptr == NULL)
            {
                result = GlobusXIOErrorMemory("handle->write_headers");
                goto error;
            }

            handle->write_headers = tmp_ptr;

            handle->write_header_count = write_iovec_count;
        }   

        handle->frame_writes = GLOBUS_TRUE;
        
        write_iovec_count *= 2;
    }

    /* allocate enough write iovecs */
    
    if(write_iovec_count > handle->write_iovec_count)
    {
        void *                          tmp_ptr;

        tmp_ptr = realloc(handle->write_iovec,
                          sizeof(globus_xio_iovec_t)*write_iovec_count);
        if(tmp_ptr == NULL)
        {
            result = GlobusXIOErrorMemory("handle->write_iovec");
            goto error;
        }

        handle->write_iovec = tmp_ptr;
        handle->write_iovec_count = write_iovec_count;
        memset(handle->write_iovec, 0,
               sizeof(globus_xio_iovec_t)*write_iovec_count);
    }

    /* if we didn't wrap complete iovec start with first iovec found, otherwise
     * start with next.
     */
    
    if(iovec_offset)
    {
        i = j;
    }
    else
    {
        i = j + 1;
    }

    /* initialize 1st write iovec (&header) */
    
    if(handle->frame_writes == GLOBUS_TRUE)
    {
        handle->write_iovec[j].iov_base = handle->write_headers;
        handle->write_iovec[j].iov_len = 4;
        GlobusLXIOGSICreateHeader(handle->write_iovec[j],
                                  wrapped_buffer.length);
        j++;
        wait_for = 4;
    }
    else
    {
        wait_for = 0;    
    }
    
    handle->write_iovec[j].iov_base = wrapped_buffer.value;
    handle->write_iovec[j].iov_len = wrapped_buffer.length;
    wait_for +=  wrapped_buffer.length;
    j++;

    /* wrap and initialize the rest */
    
    for(;i < iovec_count;i++)
    {
        if(iovec[i].iov_len == 0)
        {
            continue;
        }
        
        do
        { 
            plaintext_buffer.value = (globus_byte_t *) 
                iovec[i].iov_base + iovec_offset;
            
            if(iovec[i].iov_len - iovec_offset > handle->max_wrap_size)
            {
                plaintext_buffer.length = handle->max_wrap_size;
                iovec_offset += handle->max_wrap_size;
            }
            else
            { 
                plaintext_buffer.length = iovec[i].iov_len - iovec_offset;
                iovec_offset = 0;
            }
            
            major_status = gss_wrap(&minor_status,
                                    handle->context,
                                    handle->attr->prot_level
                                    == GLOBUS_XIO_GSI_PROTECTION_LEVEL_PRIVACY,
                                    GSS_C_QOP_DEFAULT,
                                    &plaintext_buffer,
                                    &conf_state,
                                    &wrapped_buffer);
            
            if(GSS_ERROR(major_status))
            {
                result = GlobusXIOErrorWrapGSSFailed("gss_wrap",
                                                     major_status,
                                                     minor_status);
                goto free_wrapped;
            }

            if(handle->frame_writes == GLOBUS_TRUE)
            { 
                handle->write_iovec[j].iov_base = handle->write_headers + 2*j;
                handle->write_iovec[j].iov_len = 4;
                GlobusLXIOGSICreateHeader(handle->write_iovec[j],
                                          wrapped_buffer.length);
                j++;
                wait_for += 4;
            }

            handle->write_iovec[j].iov_base = wrapped_buffer.value;
            handle->write_iovec[j].iov_len = wrapped_buffer.length;
            j++;
            wait_for += wrapped_buffer.length;
        }
        while(iovec_offset);
    }

    GlobusXIOGSIDebugPrintf(
        GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
        (_XIOSL("[%s:%d] Got %d bytes to write."
         " Waiting for %d wrapped bytes to be written\n"),
         _xio_name, handle->connection_id, handle->bytes_written, wait_for));
    
    /* pass the write */
    
    result = globus_xio_driver_pass_write(op, handle->write_iovec,
                             write_iovec_count, wait_for,
                             globus_l_xio_gsi_write_cb, handle);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    globus_free(bounce);
    GlobusXIOGSIDebugExit();
    return;
    
 free_wrapped:
    if(handle->frame_writes == GLOBUS_FALSE)
    { 
        for(i = 0;i < j;i++)
        {
            if(handle->write_iovec[i].iov_base != NULL)
            {
                free(handle->write_iovec[i].iov_base);
                handle->write_iovec[i].iov_base = NULL;
            }
        }
    }
    else
    {
        for(i = 1;i < j;i += 2)
        {
            /* exclude the headers for framed writes */
            if(handle->write_iovec[i].iov_base != NULL)
            {
                free(handle->write_iovec[i].iov_base);
                handle->write_iovec[i].iov_base = NULL;
                handle->write_iovec[i - 1].iov_base = NULL;
            }
        }
    }    
 error:
    globus_free(bounce);
    globus_xio_driver_finished_write(op, result, 0);

    GlobusXIOGSIDebugExitWithError();

    return;
}

static
globus_result_t
globus_l_xio_gsi_write(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_handle_t *                 handle;
    globus_result_t                     result;
    int                                 i;
    int                                 j;
    globus_size_t                       wait_for;
    gsi_l_write_bounce_t *              bounce;
    int                                 sz;
    GlobusXIOName(globus_l_xio_gsi_write);
    GlobusXIOGSIDebugEnter();

    if(!driver_specific_handle)
    {
        result = GlobusXIOErrorParameter("driver_specific_handle");
        goto error;
    }

    handle = (globus_l_handle_t *) driver_specific_handle;

    /* get wait_for here */
    wait_for = globus_xio_operation_get_wait_for(op);

    /* see if we have nothing to do */
    if(iovec_count < 1)
    {
        /* if trying to wait for more than we pass in error out */
        if(wait_for > 0)
        {
            result = GlobusXIOErrorParameter("iovec_count");
            goto error;
        }
    }

    /* no protection -> just pass the write with a null callback */
    if(handle->attr->prot_level == GLOBUS_XIO_GSI_PROTECTION_LEVEL_NONE)
    {
        GlobusXIOGSIDebugPrintf(
            GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
            (_XIOSL("[%s:%d] Passed through. No protection\n"),
             _xio_name, handle->connection_id));
        result = globus_xio_driver_pass_write(
            op, (globus_xio_iovec_t *)iovec, iovec_count, wait_for,
            NULL, handle);
        GlobusXIOGSIDebugExit();
        return result;
    }
    
    handle->frame_writes = GLOBUS_FALSE;
    handle->bytes_written = 0;

    /* find first non empty iovec */
    j = 0;
    while(j < iovec_count && iovec[j].iov_len == 0)
    {
        j++;
    }

    /* if all iovecs are empty then we are done */
    if(j == iovec_count)
    {
        GlobusXIOGSIDebugPrintf(
            GLOBUS_XIO_GSI_DEBUG_INTERNAL_TRACE,
            (_XIOSL("[%s:%d] Passed through. Empty iovecs\n"),
             _xio_name, handle->connection_id));
        result = globus_xio_driver_pass_write(
                    op, (globus_xio_iovec_t *)iovec, iovec_count, wait_for,
                    NULL, handle);
         GlobusXIOGSIDebugExit();
        return result;
    }

    sz = iovec_count > 0 ? (iovec_count-1)*sizeof(globus_xio_iovec_t) : 0;
    sz += sizeof(gsi_l_write_bounce_t);

    bounce = (gsi_l_write_bounce_t *) globus_malloc(sz);

    bounce->driver_specific_handle = driver_specific_handle;
    bounce->op = op;
    bounce->iovec_count = iovec_count;
    for(i = 0; i < iovec_count; i++)
    {
        bounce->iovec[i].iov_base = iovec[i].iov_base;
        bounce->iovec[i].iov_len = iovec[i].iov_len;
    }
    globus_callback_register_oneshot(
        NULL,
        NULL,
        globus_l_xio_gsi_write_bounce,
        bounce);

/*
    globus_l_xio_gsi_write_bounce(bounce);
*/
    return GLOBUS_SUCCESS;
error:
    GlobusXIOGSIDebugExitWithError();
    return result;
}




/* Write callback received after writing a delegation token. Checks the
 * result and registers another read unless the handle indicates we are
 * done - internal only
 */
static void
globus_l_xio_gsi_write_delegation_token_cb(
    struct globus_i_xio_op_s *          op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_delegation_handle_t *      handle;
    gss_buffer_desc                     tmp_buffer;
    OM_uint32                           minor_status;

    GlobusXIOName(globus_l_xio_gsi_write_delegation_token_cb);
    GlobusXIOGSIDebugInternalEnter();

    handle = (globus_l_delegation_handle_t *) user_arg;
     
    /* iovec was used to write a sec token */
    
    tmp_buffer.length = handle->iovec[1].iov_len;
    tmp_buffer.value = handle->iovec[1].iov_base;

    gss_release_buffer(&minor_status, &tmp_buffer);
    
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    
    if(handle->done == GLOBUS_TRUE)
    {
        if(handle->result_obj)
        {
            result = globus_error_put(handle->result_obj);
            handle->result_obj = NULL;
        }
        
        /* call callback */
        if(handle->init_callback)
        {
            handle->init_callback(result,
                                  handle->user_arg);
        }
        else
        {
            handle->accept_callback(result,
                                    handle->cred,
                                    handle->time_rec,
                                    handle->user_arg);
        }

        free(handle);
        
        GlobusXIOGSIDebugInternalExit();
        return;
    }
    else
    {
        handle->reading_header = GLOBUS_TRUE;
        result = globus_xio_driver_pass_read(op, handle->iovec, 1, 4,
                                globus_l_xio_gsi_read_delegation_token_cb,
                                handle);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
    }
    
    GlobusXIOGSIDebugInternalExit();
    return;
    
 error:
    /* call callback */
    if(handle->init_callback)
    {
        handle->init_callback(result,
                              handle->user_arg);
    }
    else
    {
        handle->accept_callback(result,
                                handle->cred,
                                handle->time_rec,
                                handle->user_arg);
    }

    if(handle->result_obj)
    {
        globus_object_free(handle->result_obj);
    }
    free(handle);
    
    GlobusXIOGSIDebugInternalExitWithError();
    return;
}

/* Read callback received after reading a delegation token. Checks the
 * result, passes the read token to the right gss call and starts a write if
 * the gss call did emit another token. - internal only
 */
static
void
globus_l_xio_gsi_read_delegation_token_cb(
    struct globus_i_xio_op_s *          op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_delegation_handle_t *      handle;
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    gss_buffer_desc 		        output_token = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc 		        input_token;
    gss_OID                             mech_type;
    globus_size_t                       wait_for = 0;
    
    GlobusXIOName(globus_l_xio_gsi_read_delegation_token_cb);
    GlobusXIOGSIDebugInternalEnter();

    handle = (globus_l_delegation_handle_t *) user_arg;

    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }


    if(handle->reading_header == GLOBUS_TRUE)
    {
        handle->reading_header = GLOBUS_FALSE;
        GlobusLXIOGSIGetTokenLength(handle->iovec[0],wait_for);
        handle->iovec[1].iov_base = malloc(wait_for);
        if(!handle->iovec[1].iov_base)
        {
            result = GlobusXIOErrorMemory("handle->iovec[1].iov_base");
            goto error;
        }
        handle->iovec[1].iov_len = wait_for;
        result = globus_xio_driver_pass_read(op, &handle->iovec[1], 1,
                                wait_for,
                                globus_l_xio_gsi_read_delegation_token_cb,
                                handle);
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
        GlobusXIOGSIDebugInternalExit();
        return;
    }
    else
    {
        wait_for = 0;
        input_token.value = handle->iovec[1].iov_base;
        input_token.length = nbytes;
    }
    
    /* init/accept sec context */
    
    if(handle->init_callback)
    {
        major_status = gss_init_delegation(
            &minor_status,
            handle->xio_handle->context,
            handle->cred,
            GSS_C_NO_OID,
            handle->restriction_oids,
            handle->restriction_buffers,
            &input_token,
            0,
            handle->time_req,
            &output_token);

        if(GSS_ERROR(major_status))
        {
            result = GlobusXIOErrorWrapGSSFailed("gss_init_delegation",
                                                 major_status,
                                                 minor_status);

            /* if we have a output token try to send it */
            if(output_token.length == 0)
            {
                goto error;
            }
            else
            {
                handle->result_obj = globus_error_get(result);
                handle->done = GLOBUS_TRUE;
            }
        }
        else if(major_status == GSS_S_COMPLETE)
        {
            handle->done = GLOBUS_TRUE;
        }
    }
    else
    {
        major_status = gss_accept_delegation(&minor_status,
                                             handle->xio_handle->context,
                                             handle->restriction_oids,
                                             handle->restriction_buffers,
                                             &input_token,
                                             0,
                                             handle->time_req,
                                             &handle->time_rec,
                                             &handle->cred,
                                             &mech_type,
                                             &output_token);
        
        if(GSS_ERROR(major_status))
        {
            result = GlobusXIOErrorWrapGSSFailed("gss_accept_delegation",
                                                 major_status,
                                                 minor_status);
            /* if we have a output token try to send it */
            if(output_token.length == 0)
            {
                goto error;
            }
            else
            {
                handle->result_obj = globus_error_get(result);
                handle->done = GLOBUS_TRUE;
            }
        }
        else if(major_status == GSS_S_COMPLETE)
        {
            handle->done = GLOBUS_TRUE;
        }
    }

    if(output_token.length != 0)
    {
        /* send the output token */

        GlobusLXIOGSICreateHeader(handle->iovec[0], output_token.length);
        
        handle->iovec[1].iov_len = output_token.length;
        handle->iovec[1].iov_base = output_token.value;
            
        wait_for = 4 + output_token.length;
        
        result = globus_xio_driver_pass_write(op, handle->iovec, 2, wait_for,
                                 globus_l_xio_gsi_write_delegation_token_cb,
                                 handle);
        if(result != GLOBUS_SUCCESS)
        {
            gss_release_buffer(&minor_status, &output_token);
            goto error;
        }
    }
    else if(handle->done == GLOBUS_TRUE)
    {
        /* we're done */
        /* call callback */
        if(handle->init_callback)
        {
            handle->init_callback(result,
                                  handle->user_arg);
        }
        else
        {
            handle->accept_callback(result,
                                    handle->cred,
                                    handle->time_rec,
                                    handle->user_arg);
        }

        free(handle);
    }

    GlobusXIOGSIDebugInternalExit();
    return;

 error:

    /* call callback */
    if(handle->init_callback)
    {
        handle->init_callback(result,
                              handle->user_arg);
    }
    else
    {
        handle->accept_callback(result,
                                handle->cred,
                                handle->time_rec,
                                handle->user_arg);
    }

    if(handle->result_obj)
    {
        globus_object_free(handle->result_obj);
    }

    free(handle);
    
    GlobusXIOGSIDebugInternalExitWithError();
    return;
}

static
void
globus_l_xio_gsi_init_delegation_cb(
    globus_result_t			result,
    void *				user_arg)
{
    globus_l_xio_gsi_delegation_arg_t * monitor;

    monitor = (globus_l_xio_gsi_delegation_arg_t *) user_arg;

    globus_mutex_lock(&monitor->mutex);

    monitor->result = result;
    monitor->done = GLOBUS_TRUE;

    globus_cond_signal(&monitor->cond);
    
    globus_mutex_unlock(&monitor->mutex);

    return;
}

static
void
globus_l_xio_gsi_accept_delegation_cb(
    globus_result_t			result,
    gss_cred_id_t                       delegated_cred,
    OM_uint32                           time_rec,
    void *				user_arg)
{
    globus_l_xio_gsi_delegation_arg_t * monitor;

    monitor = (globus_l_xio_gsi_delegation_arg_t *) user_arg;

    globus_mutex_lock(&monitor->mutex);

    monitor->result = result;

    if(monitor->cred)
    { 
        *(monitor->cred) = delegated_cred;
    }

    if(monitor->time_rec)
    { 
        *(monitor->time_rec) = time_rec;
    }
    
    monitor->done = GLOBUS_TRUE;

    globus_cond_signal(&monitor->cond);
    
    globus_mutex_unlock(&monitor->mutex);

    return;    
}

/*
 * driver cntl interface function
 */
static
globus_result_t
globus_l_xio_gsi_cntl(
    void *                              driver_specific_handle,
    int                                 cmd,
    va_list                             ap)
{
    globus_l_handle_t *                 handle;
    gss_name_t *                        out_name;
    gss_cred_id_t *                     out_cred;
    gss_ctx_id_t *                      out_ctx;
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 rc;
    globus_l_delegation_handle_t *      delegation_handle;
    globus_xio_operation_t              op = NULL;
    gss_buffer_desc                     output_token;
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    globus_l_xio_gsi_delegation_arg_t   monitor;
    
    GlobusXIOName(globus_l_xio_gsi_cntl);
    GlobusXIOGSIDebugEnter();

    if(!driver_specific_handle)
    {
        GlobusXIOGSIDebugExitWithError();
        return GlobusXIOErrorParameter("driver_specific_handle");
    }
    
    handle = (globus_l_handle_t *) driver_specific_handle;

    switch(cmd)
    {
      case GLOBUS_XIO_GSI_SET_CREDENTIAL:
        /* this is bad, but is needed to support old globus io code.
         * ideally, we would have locks and ensure there are no pending ops
         * when this is called
         */
        handle->attr->credential = va_arg(ap, gss_cred_id_t);
        break;
      case GLOBUS_XIO_GSI_GET_CREDENTIAL:
        out_cred = va_arg(ap, gss_cred_id_t *);
        *out_cred = handle->attr->credential;
        break;
        
        /* extract the context */
      case GLOBUS_XIO_GSI_GET_CONTEXT:
        out_ctx = va_arg(ap, gss_ctx_id_t *);
        *out_ctx= handle->context;
        break;
        /* extract the delegated cred */
      case GLOBUS_XIO_GSI_GET_DELEGATED_CRED:
        out_cred = va_arg(ap, gss_cred_id_t *);
        *out_cred = handle->delegated_cred;
        break;
        /* get the peer name */
      case GLOBUS_XIO_GSI_GET_PEER_NAME:
        out_name = va_arg(ap, gss_name_t *);
        *out_name = handle->peer_name;
        break;
        /* get the local name */
      case GLOBUS_XIO_GSI_GET_LOCAL_NAME:
        out_name = va_arg(ap, gss_name_t *);
        *out_name = handle->local_name;
        break;
      case GLOBUS_XIO_GSI_INIT_DELEGATION:

        monitor.done = GLOBUS_FALSE;
        monitor.result = GLOBUS_SUCCESS;
        
        rc = globus_mutex_init(&monitor.mutex, NULL);
        
        assert(rc == GLOBUS_SUCCESS);
        
        rc = globus_cond_init(&monitor.cond, NULL);
        
        assert(rc == GLOBUS_SUCCESS);
            
      case GLOBUS_XIO_GSI_REGISTER_INIT_DELEGATION:
        delegation_handle = (globus_l_delegation_handle_t *)
            malloc(sizeof(globus_l_delegation_handle_t));
        if(!delegation_handle)
        {
            result = GlobusXIOErrorMemory("delegation_handle");
            goto delegation_error;
        }

        delegation_handle->xio_handle = handle;
        delegation_handle->cred = va_arg(ap, gss_cred_id_t);
        delegation_handle->restriction_oids = va_arg(ap, gss_OID_set);
        delegation_handle->restriction_buffers = va_arg(ap, gss_buffer_set_t);
        delegation_handle->time_req = va_arg(ap, OM_uint32);

        if(cmd == GLOBUS_XIO_GSI_INIT_DELEGATION)
        {
            delegation_handle->init_callback =
                globus_l_xio_gsi_init_delegation_cb;
            delegation_handle->user_arg = &monitor;
        }
        else
        { 
            delegation_handle->init_callback =
                va_arg(ap, globus_xio_gsi_delegation_init_callback_t);
            delegation_handle->user_arg = va_arg(ap,void *);
        }

        delegation_handle->accept_callback = NULL;
        delegation_handle->iovec[0].iov_base = delegation_handle->header;
        delegation_handle->iovec[0].iov_len = 4;
        delegation_handle->done = GLOBUS_FALSE;
        delegation_handle->result_obj = NULL;
        
        major_status = gss_init_delegation(
            &minor_status,
            handle->context,
            delegation_handle->cred,
            GSS_C_NO_OID,
            delegation_handle->restriction_oids,
            delegation_handle->restriction_buffers,
            GSS_C_NO_BUFFER,
            0,
            delegation_handle->time_req,
            &output_token);

        if(GSS_ERROR(major_status))
        {
            result = GlobusXIOErrorWrapGSSFailed("gss_init_delegation",
                                                 major_status,
                                                 minor_status);
            goto delegation_error;
        }

        if(major_status & GSS_S_CONTINUE_NEEDED)
        { 
            result = globus_xio_driver_operation_create(&op,
                                                        handle->xio_driver_handle);
            
            if(result != GLOBUS_SUCCESS)
            {
                GlobusXIOErrorWrapFailed("globus_xio_driver_operation_create",
                                         result);
                goto delegation_error;
            }
        
            delegation_handle->iovec[1].iov_base = output_token.value;
            delegation_handle->iovec[1].iov_len = output_token.length;
            
            GlobusLXIOGSICreateHeader(delegation_handle->iovec[0],
                                      output_token.length);
            
            result = globus_xio_driver_pass_write(
                op, delegation_handle->iovec, 2,
                output_token.length + 4,
                globus_l_xio_gsi_write_delegation_token_cb,
                delegation_handle);
            if(result != GLOBUS_SUCCESS)
            {
                goto delegation_error;
            }

            if(cmd == GLOBUS_XIO_GSI_INIT_DELEGATION)
            {
                globus_mutex_lock(&monitor.mutex);
                while(monitor.done == GLOBUS_FALSE)
                {
                    globus_cond_wait(&monitor.cond, &monitor.mutex);
                }
                globus_mutex_unlock(&monitor.mutex);
                
                result = monitor.result;
            }
        }
        else
        {
            free(delegation_handle);
        }

        if(cmd == GLOBUS_XIO_GSI_INIT_DELEGATION)
        {
            globus_mutex_destroy(&monitor.mutex);
            globus_cond_destroy(&monitor.cond);        
        }
        
        break;
      case GLOBUS_XIO_GSI_ACCEPT_DELEGATION:

        monitor.done = GLOBUS_FALSE;
        monitor.result = GLOBUS_SUCCESS;
        monitor.cred = va_arg(ap, gss_cred_id_t *);
        
        rc = globus_mutex_init(&monitor.mutex, NULL);
        
        assert(rc == GLOBUS_SUCCESS);
        
        rc = globus_cond_init(&monitor.cond, NULL);
        
        assert(rc == GLOBUS_SUCCESS);

      case GLOBUS_XIO_GSI_REGISTER_ACCEPT_DELEGATION:

        delegation_handle = (globus_l_delegation_handle_t *)
            malloc(sizeof(globus_l_delegation_handle_t));
        if(!delegation_handle)
        {
            result = GlobusXIOErrorMemory("delegation_handle");
            goto delegation_error;
        }

        delegation_handle->xio_handle = handle;
        delegation_handle->cred = GSS_C_NO_CREDENTIAL;
        delegation_handle->restriction_oids = va_arg(ap, gss_OID_set);
        delegation_handle->restriction_buffers = va_arg(ap, gss_buffer_set_t);
        delegation_handle->time_req = va_arg(ap, OM_uint32);
        delegation_handle->init_callback = NULL;
        
        if(cmd == GLOBUS_XIO_GSI_ACCEPT_DELEGATION)
        {
            delegation_handle->accept_callback =
                globus_l_xio_gsi_accept_delegation_cb;
            delegation_handle->user_arg = &monitor;
            monitor.time_rec = va_arg(ap, OM_uint32 *);
        }
        else
        {
            delegation_handle->accept_callback =
                va_arg(ap, globus_xio_gsi_delegation_accept_callback_t);
            delegation_handle->user_arg = va_arg(ap,void *);
        }
        delegation_handle->iovec[0].iov_base = delegation_handle->header;
        delegation_handle->iovec[0].iov_len = 4;
        delegation_handle->done = GLOBUS_FALSE;
        delegation_handle->result_obj = NULL;
        delegation_handle->reading_header = GLOBUS_TRUE;
        
        result = globus_xio_driver_operation_create(&op,
                                                    handle->xio_driver_handle);
        
        if(result != GLOBUS_SUCCESS)
        {
            GlobusXIOErrorWrapFailed("globus_xio_driver_operation_create",
                                     result);
            goto delegation_error;
        }
        
        result = globus_xio_driver_pass_read(op, delegation_handle->iovec, 1,
                                4,
                                globus_l_xio_gsi_read_delegation_token_cb,
                                delegation_handle);
        if(result != GLOBUS_SUCCESS)
        {
            goto delegation_error;
        }

        if(cmd == GLOBUS_XIO_GSI_ACCEPT_DELEGATION)
        {
            globus_mutex_lock(&monitor.mutex);

            while(monitor.done == GLOBUS_FALSE)
            {
                globus_cond_wait(&monitor.cond, &monitor.mutex);
            }
            
            globus_mutex_unlock(&monitor.mutex);
            
            result = monitor.result;
            globus_mutex_destroy(&monitor.mutex);
            globus_cond_destroy(&monitor.cond);        
        }
        
        break;
      default:
        result = GlobusXIOErrorInvalidCommand(cmd);
        GlobusXIOGSIDebugExitWithError();
        return result;
    }
    
    GlobusXIOGSIDebugExit();
    return GLOBUS_SUCCESS;

 delegation_error:
    if(delegation_handle)
    {
        free(delegation_handle);
    }

    if(op)
    {
        globus_xio_driver_operation_destroy(op);
    }

    if(cmd == GLOBUS_XIO_GSI_INIT_DELEGATION ||
       cmd == GLOBUS_XIO_GSI_ACCEPT_DELEGATION)
    {
        globus_mutex_destroy(&monitor.mutex);
        globus_cond_destroy(&monitor.cond);
    }
    
    GlobusXIOGSIDebugExitWithError();
    return result;
}


static globus_result_t
gsi_l_attr_parse_auth(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func)
{
    globus_result_t                     result;
    globus_xio_gsi_authorization_mode_t type;
    GlobusXIOName(gsi_l_attr_parse_auth);
    GlobusXIOGSIDebugEnter();

    if(strcasecmp(val, "none") == 0)
    {
        type = GLOBUS_XIO_GSI_NO_AUTHORIZATION;
    }
    else if(strcasecmp(val, "self") == 0)
    {
        type = GLOBUS_XIO_GSI_SELF_AUTHORIZATION;
    }
    else if(strcasecmp(val, "host") == 0)
    {
        type = GLOBUS_XIO_GSI_HOST_AUTHORIZATION;
    }
    else if(strcasecmp(val, "id") == 0)
    {
        type = GLOBUS_XIO_GSI_IDENTITY_AUTHORIZATION;
    }
    else
    {
        result = GlobusXIOErrorParse(val);
        goto done;
    }

    result = globus_xio_string_cntl_bouncer(cntl_func, attr, cmd, type);
done:
    GlobusXIOGSIDebugExit();

    return result;
}

static globus_result_t
gsi_l_attr_parse_prot(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func)
{
    globus_xio_gsi_protection_level_t   type;
    globus_result_t                     result;
    GlobusXIOName(gsi_l_attr_parse_prot);
    GlobusXIOGSIDebugEnter();

    if(strcasecmp(val, "none") == 0)
    {
        type = GLOBUS_XIO_GSI_PROTECTION_LEVEL_NONE;
    }
    else if(strcasecmp(val, "private") == 0)
    {
        type = GLOBUS_XIO_GSI_PROTECTION_LEVEL_PRIVACY;
    }
    else if(strcasecmp(val, "integrity") == 0)
    {
        type = GLOBUS_XIO_GSI_PROTECTION_LEVEL_INTEGRITY;
    }
    else
    {
        result = GlobusXIOErrorParse(val);
        goto done;
    }
    result = globus_xio_string_cntl_bouncer(cntl_func, attr, cmd, type);

done:
    GlobusXIOGSIDebugExit();

    return result;
}

static globus_result_t
gsi_l_attr_parse_del(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func)
{
    globus_xio_gsi_delegation_mode_t    type;
    globus_result_t                     result;
    GlobusXIOName(gsi_l_attr_parse_del);
    GlobusXIOGSIDebugEnter();

    if(strcasecmp(val, "none") == 0)
    {
        type = GLOBUS_XIO_GSI_DELEGATION_MODE_NONE;
    }
    else if(strcasecmp(val, "full") == 0)
    {
        type = GLOBUS_XIO_GSI_DELEGATION_MODE_FULL;
    }
    else if(strcasecmp(val, "limited") == 0)
    {
        type = GLOBUS_XIO_GSI_DELEGATION_MODE_LIMITED;
    }
    else
    {
        result = GlobusXIOErrorParse(val);
        goto done;
    }

    result = globus_xio_string_cntl_bouncer(cntl_func, attr, cmd, type);
done:
    GlobusXIOGSIDebugExit();

    return result;
}

static globus_result_t
gsi_l_attr_parse_proxy(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func)
{
    globus_xio_gsi_proxy_mode_t         type;
    globus_result_t                     result;
    GlobusXIOName(gsi_l_attr_parse_proxy);
    GlobusXIOGSIDebugEnter();

    if(strcasecmp(val, "many") == 0)
    {
        type = GLOBUS_XIO_GSI_PROXY_MODE_MANY;
    }
    else if(strcasecmp(val, "full") == 0)
    {
        type = GLOBUS_XIO_GSI_PROXY_MODE_FULL;
    }
    else if(strcasecmp(val, "limited") == 0)
    {
        type = GLOBUS_XIO_GSI_PROXY_MODE_LIMITED;
    }
    else
    {
        result = GlobusXIOErrorParse(val);
        goto done;
    }

    result = globus_xio_string_cntl_bouncer(cntl_func, attr, cmd, type);
done:
    GlobusXIOGSIDebugExit();

    return result;
}

static globus_result_t
gsi_l_attr_parse_subject(
    void *                              attr,
    const char *                        key,
    const char *                        val,
    int                                 cmd,
    globus_xio_driver_attr_cntl_t       cntl_func)
{
    gss_buffer_desc                     send_tok;
    OM_uint32                           min_stat;
    OM_uint32                           maj_stat;
    gss_name_t                          target_name;
    globus_result_t                     result;
    GlobusXIOName(gsi_l_attr_parse_subject);
    GlobusXIOGSIDebugEnter();

    send_tok.value = (void *) val;
    send_tok.length = strlen(val) + 1;
    maj_stat = gss_import_name(
        &min_stat,
        &send_tok,
        GSS_C_NT_USER_NAME,
        &target_name);
    if(maj_stat == GSS_S_COMPLETE &&
        target_name != GSS_C_NO_NAME)
    {
        result = globus_xio_string_cntl_bouncer(cntl_func, attr, cmd, target_name);
        gss_release_name(&min_stat, &target_name);
    }
    else
    {
        result = GlobusXIOErrorParse(val);
    }
    GlobusXIOGSIDebugExit();

    return result;
}

static globus_xio_string_cntl_table_t  gsi_l_string_opts_table[] =
{
    {"subject", GLOBUS_XIO_GSI_SET_TARGET_NAME, gsi_l_attr_parse_subject},
    {"protection", GLOBUS_XIO_GSI_SET_PROTECTION_LEVEL, gsi_l_attr_parse_prot},
    {"delegation", GLOBUS_XIO_GSI_SET_DELEGATION_MODE, gsi_l_attr_parse_del},
    {"auth", GLOBUS_XIO_GSI_SET_AUTHORIZATION_MODE, gsi_l_attr_parse_auth},
    {"proxy", GLOBUS_XIO_GSI_SET_PROXY_MODE, gsi_l_attr_parse_proxy},
    {"ssl_compatible", GLOBUS_XIO_GSI_SET_SSL_COMPATIBLE, globus_xio_string_cntl_bool},
    {"application_protocols", GLOBUS_XIO_GSI_SET_APPLICATION_PROTOCOLS, globus_xio_string_cntl_string_list},
    {"credentials_dir", GLOBUS_XIO_GSI_SET_CREDENTIALS_DIR, globus_xio_string_cntl_string},
    {NULL, 0, NULL}
};


/*
 * Driver load function
 */
static globus_result_t
globus_l_xio_gsi_init(
    globus_xio_driver_t *               out_driver)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     result;

    GlobusXIOName(globus_l_xio_gsi_init);
    GlobusXIOGSIDebugEnter();

    result = globus_xio_driver_init(&driver, "gsi", NULL);
    if(result != GLOBUS_SUCCESS)
    {
        GlobusXIOGSIDebugExitWithError();
        return result;
    }

    globus_xio_driver_set_transform(
        driver,
        globus_l_xio_gsi_open,
        globus_l_xio_gsi_close,
        globus_l_xio_gsi_read,
        globus_l_xio_gsi_write,
        globus_l_xio_gsi_cntl,
        NULL);

    globus_xio_driver_set_server(
        driver,
        globus_l_xio_gsi_server_init,
        globus_l_xio_gsi_accept,
        globus_l_xio_gsi_server_destroy,
        NULL,
        NULL,
        globus_l_xio_gsi_link_destroy);

    globus_xio_driver_set_attr(
        driver,
        globus_l_xio_gsi_attr_init,
        globus_l_xio_gsi_attr_copy,
        globus_l_xio_gsi_attr_cntl,
        globus_l_xio_gsi_attr_destroy);

    globus_xio_driver_string_cntl_set_table(
        driver,
        gsi_l_string_opts_table);
    
    *out_driver = driver;

    GlobusXIOGSIDebugExit();
    return GLOBUS_SUCCESS;
}

/*
 * Driver unload function
 */
static void
globus_l_xio_gsi_destroy(
    globus_xio_driver_t                 driver)
{
    GlobusXIOName(globus_l_xio_gsi_destroy);
    GlobusXIOGSIDebugEnter();
    globus_xio_driver_destroy(driver);
    GlobusXIOGSIDebugExit();
    return;
}

/*
 * Driver definition
 */
GlobusXIODefineDriver(
    gsi,
    globus_l_xio_gsi_init,
    globus_l_xio_gsi_destroy);

/*
 * Module activation
 */
static
int
globus_l_xio_gsi_activate(void)
{
    int                                 rc;
    OM_uint32                           major_status, minor_status;
    gss_OID_set                         name_types;
    int                                 present;

    GlobusXIOName(globus_l_xio_gsi_activate);
    GlobusDebugInit(GLOBUS_XIO_GSI, TRACE INTERNAL_TRACE);
    GlobusXIOGSIDebugEnter();
    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    if( rc != GLOBUS_SUCCESS)
    {
        GlobusXIOGSIDebugExitWithError();
        GlobusDebugDestroy(GLOBUS_XIO_GSI);
        return rc;
    }
    rc = globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    if( rc != GLOBUS_SUCCESS)
    {
        globus_module_deactivate(GLOBUS_XIO_MODULE);
        GlobusXIOGSIDebugExitWithError();
        GlobusDebugDestroy(GLOBUS_XIO_GSI);
        return rc;
    }    
    major_status = gss_inquire_names_for_mech(
            &minor_status,
            gss_l_openssl_mech ,
            &name_types);

    globus_l_xio_gsi_host_ip_supported = GLOBUS_FALSE;
    if (major_status == GSS_S_COMPLETE)
    {
        major_status = gss_test_oid_set_member(
                &minor_status, 
                GLOBUS_GSS_C_NT_HOST_IP,
                name_types,
                &present);

        if (major_status == GSS_S_COMPLETE && present)
        {
            globus_l_xio_gsi_host_ip_supported = GLOBUS_TRUE;
        }
        gss_release_oid_set(&minor_status, &name_types);
    }

    GlobusXIORegisterDriver(gsi);
    globus_mutex_init(&connection_mutex,NULL);
    GlobusXIOGSIDebugExit();
    return rc;
}

/*
 * Module deactivation
 */
static
int
globus_l_xio_gsi_deactivate(void)
{
    int                                 rc;
    GlobusXIOName(globus_l_xio_gsi_deactivate);
    GlobusXIOGSIDebugEnter();
    GlobusXIOUnRegisterDriver(gsi);
    rc = globus_module_deactivate(GLOBUS_XIO_MODULE);
    rc += globus_module_deactivate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    globus_mutex_destroy(&connection_mutex);
    GlobusXIOGSIDebugExit();
    GlobusDebugDestroy(GLOBUS_XIO_GSI);
    return rc;
}

/*
 * Set up the target name based on the authorization mode
 */
static
globus_result_t
globus_l_xio_gsi_setup_target_name(
    globus_l_handle_t *                 handle)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    char *                              contact_string;
    gss_buffer_desc                     name_buffer;
    globus_xio_contact_t                contact_info;
    GlobusXIOName(globus_l_xio_gsi_setup_target_name);
    GlobusXIOGSIDebugInternalEnter();

    switch(handle->attr->authz_mode)
    {
      case GLOBUS_XIO_GSI_HOST_AUTHORIZATION:
        result = globus_xio_driver_handle_cntl(
            handle->xio_driver_handle,
            GLOBUS_XIO_QUERY,
            GLOBUS_XIO_GET_REMOTE_NUMERIC_CONTACT,
            &contact_string);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_xio_driver_handle_cntl failed to query remote contact",
                result);
            goto error;
        }
        
        result = globus_xio_contact_parse(&contact_info, contact_string);
        globus_free(contact_string);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_xio_contact_parse", result);
            goto error;
        }
        
        if(handle->attr->target_name != GSS_C_NO_NAME)
        {
            gss_release_name(&minor_status,
                             &handle->attr->target_name);
            handle->attr->target_name = GSS_C_NO_NAME;
        }

        if (globus_l_xio_gsi_host_ip_supported)
        {
            name_buffer.value = handle->host_name;
            name_buffer.length = strlen(name_buffer.value);

            major_status = gss_import_name(
                &minor_status,
                &name_buffer,
                GLOBUS_GSS_C_NT_HOST_IP,
                &handle->attr->target_name);
            globus_xio_contact_destroy(&contact_info);
            if(major_status != GLOBUS_SUCCESS)
            {
                result = globus_error_put(
                        globus_error_construct_gssapi_error(
                        GLOBUS_GSI_GSSAPI_MODULE,
                        NULL,
                        major_status,
                        minor_status));

                goto error;
            }
        }
        else
        {
            result = globus_gss_assist_authorization_host_name(
                contact_info.host,
                &handle->attr->target_name);
            globus_xio_contact_destroy(&contact_info);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_gss_assist_authorization_host_name", result); 
                goto error;
            }
        }
        break;
      case GLOBUS_XIO_GSI_IDENTITY_AUTHORIZATION:
        if(handle->attr->target_name == GSS_C_NO_NAME)
        {
            result = GlobusXioGSIErrorEmptyTargetName();
            goto error;
        }
        break;
      case GLOBUS_XIO_GSI_SELF_AUTHORIZATION:
        if(handle->attr->target_name != GSS_C_NO_NAME)
        {
            gss_release_name(&minor_status,
                             &handle->attr->target_name);
            handle->attr->target_name = GSS_C_NO_NAME;
        }
        
        if(handle->attr->credential == GSS_C_NO_CREDENTIAL)
        { 
            major_status = gss_acquire_cred(&minor_status,
                                            GSS_C_NO_NAME,
                                            GSS_C_INDEFINITE,
                                            GSS_C_NO_OID_SET,
                                            GSS_C_BOTH,
                                            &handle->credential,
                                            NULL,
                                            NULL);
            if(GSS_ERROR(major_status))
            {
                result = GlobusXIOErrorWrapGSSFailed("gss_acquire_cred",
                                                     major_status,
                                                     minor_status);
                goto error;
            }
            handle->attr->credential = handle->credential;
        }
        
        major_status = gss_inquire_cred(&minor_status,
                                        handle->attr->credential,
                                        &handle->attr->target_name,
                                        NULL,
                                        NULL,
                                        NULL);
        if(GSS_ERROR(major_status))
        {
            result = GlobusXIOErrorWrapGSSFailed("gss_inquire_cred",
                                                 major_status,
                                                 minor_status);
            goto error;
        }            
        break;
      case GLOBUS_XIO_GSI_NO_AUTHORIZATION:
      default:
        if(handle->attr->target_name != GSS_C_NO_NAME)
        {
            gss_release_name(&minor_status,
                             &handle->attr->target_name);
            handle->attr->target_name = GSS_C_NO_NAME;
        }
    }

    GlobusXIOGSIDebugInternalExit();
    return GLOBUS_SUCCESS;
    
 error:
    GlobusXIOGSIDebugInternalExitWithError();    
    return result;
}
