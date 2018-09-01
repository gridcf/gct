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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file import_cred.c
 * @author Sam Lang, Sam Meder
 */
#endif

#include "gssapi_openssl.h"
#include "globus_i_gsi_gss_utils.h"
#include "globus_gsi_system_config.h"

#include <string.h>

/* Only build if we have the extended GSSAPI */
#ifdef  _HAVE_GSI_EXTENDED_GSSAPI

/**
 * @brief Import a credential
 * @ingroup globus_gsi_gssapi_extensions
 * @details
 * This function will import credentials exported by
 * gss_export_cred(). It is intended to allow a multiple use
 * application to checkpoint delegated credentials.  
 *
 * @param minor_status
 *        The minor status returned by this function. This parameter
 *        will be 0 upon success.
 * @param output_cred_handle
 *        Upon success, this parameter will contain the imported
 *        credential. When no longer needed this credential should be
 *        freed using gss_release_cred().
 * @param desired_mech
 *        This parameter may be used to specify the desired security
 *        mechanism. May be GSS_C_NO_OID.
 * @param option_req
 *        This parameter indicates which option_req value was used to
 *        produce the import_buffer.
 * @param import_buffer
 *        A buffer produced by gss_export_credential().
 * @param time_req
 *        The requested period of validity (seconds) for the imported
 *        credential. May be NULL.
 * @param time_rec
 *        This parameter will contain the received period of validity
 *        of the imported credential upon success. May be NULL.
 * @retval GSS_S_COMPLETE Success
 * @retval GSS_S_BAD_MECH Requested security mechanism is unavailable
 * @retval GSS_S_DEFECTIVE_TOKEN import_buffer is defective
 * @retval GSS_S_FAILURE General failure
 */
OM_uint32 
GSS_CALLCONV gss_import_cred(
    OM_uint32 *                         minor_status,
    gss_cred_id_t *                     output_cred_handle,
    const gss_OID                       desired_mech,
    OM_uint32                           option_req,
    const gss_buffer_t                  import_buffer,
    OM_uint32                           time_req,
    OM_uint32 *                         time_rec)
{
    globus_result_t                     local_result = GLOBUS_SUCCESS;
    OM_uint32                           major_status = GSS_S_COMPLETE;
    OM_uint32                           local_minor_status = GLOBUS_SUCCESS;
    BIO *                               bp = NULL;
    char *                              filename = NULL;
    FILE *                              fp = NULL;
    FILE *                              cert_fp = NULL;
    FILE *                              key_fp = NULL;
    int                                 rc = 0;
    struct stat                         st = { .st_mode = 0 };
    DIR                                *dir = NULL;

    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    /* module activation if not already done by calling
     * globus_module_activate
     */
    globus_thread_once(
        &once_control,
        globus_l_gsi_gssapi_activate_once);
    
    globus_mutex_lock(&globus_i_gssapi_activate_mutex);
    if (!globus_i_gssapi_active)
    {
        globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE);
    }
    globus_mutex_unlock(&globus_i_gssapi_activate_mutex);
    
    *minor_status = (OM_uint32) GLOBUS_SUCCESS;

    if (import_buffer == NULL ||
        import_buffer ==  GSS_C_NO_BUFFER ||
        import_buffer->length < 1) 
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status, 
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Invalid import_buffer passed to function: %s"),
             __func__));
        major_status = GSS_S_FAILURE;
        goto exit;
    }

    if (output_cred_handle == NULL )
    { 
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
            (_GGSL("Invalid output_cred_handle parameter passed to function: %s"),
             __func__));
        major_status = GSS_S_FAILURE;
        goto exit;
    }
    
    if (import_buffer->length > 0)
    {
        if(option_req == GSS_IMPEXP_OPAQUE_FORM)
        {
            bp = BIO_new(BIO_s_mem());
            
            BIO_write(bp,
                      import_buffer->value,
                      import_buffer->length);
        }
        else if(option_req == GSS_IMPEXP_MECH_SPECIFIC) 
        {
            char *                      p;
            size_t                      pathlen = 0;

            p = memchr(import_buffer->value, '=', import_buffer->length);
            if (p == NULL)
            {
                GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                    minor_status,
                    GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
                    (_GGSL("Invalid import_buffer parameter passed to function: %s"),
                     __func__));
                major_status = GSS_S_FAILURE;
                goto exit;
            }

            pathlen = import_buffer->length -
                    (p - (char *) import_buffer->value);

            filename = malloc(pathlen);

            if (filename == NULL)
            {
                GLOBUS_GSI_GSSAPI_MALLOC_ERROR(minor_status);
                major_status = GSS_S_FAILURE;
                goto exit;

            }

            memcpy(filename, p + 1, pathlen-1);
            filename[pathlen-1] = '\0';

            rc = stat(filename, &st);
            if (rc == 0 && st.st_mode & S_IFDIR)
            {
                struct dirent          *entry = NULL;
                char                    buffer[256];

                dir = opendir(filename);
                if (dir == NULL)
                {
                    GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                        minor_status,
                        GLOBUS_GSI_GSSAPI_ERROR_IMPORT_FAIL,
                        (_GGSL("Couldn't open the dir: %s"),
                         filename));
                    major_status = GSS_S_FAILURE;
                    goto exit;
                }
                while (((rc = globus_libc_readdir_r(dir, &entry)) == 0)
                    && entry != NULL)
                {
                    const char         *end = NULL;
                    if (((end = strstr(entry->d_name, "cert.pem")) != NULL)
                        && strcmp(end, "cert.pem") == 0)
                    {
                        char            cert_path[
                            strlen(filename) + strlen(entry->d_name) + 2];

                        snprintf(
                            cert_path,
                            sizeof(cert_path),
                            "%s/%s",
                            filename,
                            entry->d_name);

                        local_result = GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE(
                            cert_path);
#ifndef WIN32
                        if (local_result != GLOBUS_SUCCESS
                            && getuid() == 0
                            && globus_i_gsi_gssapi_vhost_cred_owner != 0)
                        {
                            local_result =
                                GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE_UID(
                                    cert_path,
                                    globus_i_gsi_gssapi_vhost_cred_owner);
                        }
#endif
                        if (local_result != GLOBUS_SUCCESS)
                        {
                            *minor_status = local_result;
                            major_status = GSS_S_FAILURE;

                            goto exit;
                        }
                        if (cert_fp == NULL)
                        {
                            cert_fp = fopen(cert_path, "r");
                        }
                    }
                    else if (((end = strstr(entry->d_name, "key.pem")) != NULL)
                        && strcmp(end, "key.pem") == 0)
                    {
                        char            key_path[
                            strlen(filename) + strlen(entry->d_name) + 2];

                        snprintf(
                            key_path,
                            sizeof(key_path),
                            "%s/%s",
                            filename,
                            entry->d_name);

                        local_result = GLOBUS_GSI_SYSCONFIG_CHECK_KEYFILE(
                            key_path);
#ifndef WIN32
                        if (local_result != GLOBUS_SUCCESS
                            && getuid() == 0
                            && globus_i_gsi_gssapi_vhost_cred_owner != 0)
                        {
                            local_result =
                                GLOBUS_GSI_SYSCONFIG_CHECK_KEYFILE_UID(
                                    key_path,
                                    globus_i_gsi_gssapi_vhost_cred_owner);
                        }
#endif
                        if (local_result != GLOBUS_SUCCESS)
                        {
                            *minor_status = local_result;
                            major_status = GSS_S_FAILURE;

                            goto exit;
                        }
                        if (key_fp == NULL)
                        {
                            key_fp = fopen(key_path, "r");
                        }
                    }
                }
                if (cert_fp == NULL || key_fp == NULL)
                {
                    GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                        minor_status,
                        GLOBUS_GSI_GSSAPI_ERROR_IMPORT_FAIL,
                        (_GGSL("Couldn't open the file: %s"),
                         filename));
                    major_status = GSS_S_FAILURE;
                    goto exit;
                }

                bp = BIO_new(BIO_s_mem());
                while (!feof(cert_fp))
                {
                    rc = fread(buffer, 1, sizeof(buffer), cert_fp);

                    if (rc > 0)
                    {
                        BIO_write(bp, buffer, rc);
                    }
                }
                while (!feof(key_fp))
                {
                    rc = fread(buffer, 1, sizeof(buffer), key_fp);

                    if (rc > 0)
                    {
                        BIO_write(bp, buffer, rc);
                    }
                }
            }
            else
            {
                local_result = GLOBUS_GSI_SYSCONFIG_CHECK_KEYFILE(filename);
                if (local_result != GLOBUS_SUCCESS)
                {
                    *minor_status = local_result;
                    major_status = GSS_S_FAILURE;
                    goto exit;
                }
                if ((fp = fopen(filename,"r")) == NULL)
                {
                    GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                        minor_status,
                        GLOBUS_GSI_GSSAPI_ERROR_IMPORT_FAIL,
                        (_GGSL("Couldn't open the file: %s"),
                         filename));
                    major_status = GSS_S_FAILURE;
                    goto exit;
                }
            
                bp = BIO_new(BIO_s_file());
                BIO_set_fp(bp, fp, BIO_CLOSE);
            }
        }
        else
        {
            GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_ARGUMENT,
                (_GGSL("Invalid option req of: %d, not supported"),
                 option_req));
            major_status = GSS_S_FAILURE;
            goto exit;
        }
    }
    else
    {
        GLOBUS_GSI_GSSAPI_ERROR_RESULT(
            minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_TOKEN_FAIL,
            (_GGSL("Invalid token passed to function")));
        major_status = GSS_S_DEFECTIVE_TOKEN;
        goto exit;
    }
    
    major_status = globus_i_gsi_gss_cred_read_bio(
        &local_minor_status,
        GSS_C_BOTH,
        output_cred_handle,
        bp);

    if(GSS_ERROR(major_status))
    {
        GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
            minor_status, local_minor_status,
            GLOBUS_GSI_GSSAPI_ERROR_IMPORT_FAIL);
        goto exit;
    }
    
    /* If I understand this right, time_rec should contain the time
     * until the cert expires */    
    if (time_rec != NULL)
    {
        time_t lifetime;

        local_result = globus_gsi_cred_get_lifetime(
            ((gss_cred_id_desc *) *output_cred_handle)->cred_handle,
            &lifetime);
        if(local_result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_GSSAPI_ERROR_CHAIN_RESULT(
                minor_status, local_result,
                GLOBUS_GSI_GSSAPI_ERROR_WITH_GSI_CREDENTIAL);
            major_status = GSS_S_FAILURE;
            goto exit;
        }
        *time_rec = (OM_uint32) lifetime;
    }

    if(desired_mech != NULL)
    {
        if (g_OID_equal(desired_mech, gss_mech_globus_gssapi_openssl))
        {
            (*output_cred_handle)->mech =
                    (const gss_OID) gss_mech_globus_gssapi_openssl;
        }
        else if (g_OID_equal(desired_mech, gss_mech_globus_gssapi_openssl_micv2))
        {
            (*output_cred_handle)->mech =
                    (const gss_OID) gss_mech_globus_gssapi_openssl_micv2;
        }
        else
        {
            GLOBUS_GSI_GSSAPI_ERROR_RESULT(
                minor_status,
                GLOBUS_GSI_GSSAPI_ERROR_BAD_MECH,
                (_GGSL("The desired_mech: %s, is not supported"),
                 ((gss_OID_desc *)desired_mech)->elements));
            major_status = GSS_S_BAD_MECH;
            goto exit;
        }
    }
        
 exit:
    if (bp) 
    {
        BIO_free(bp);
    }
    if (cert_fp != NULL)
    {
        fclose(cert_fp);
    }
    if (key_fp != NULL)
    {
        fclose(key_fp);
    }
    if (dir != NULL)
    {
        closedir(dir);
    }
    free(filename);
    return major_status;
}

#endif /* _HAVE_GSI_EXTENDED_GSSAPI */
