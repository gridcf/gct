/*
 * Copyright 1999-2017 University of Chicago
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

/**
 * @file read_vhost_cred_dir.c Read all credentials in a directory
 */

#include "gssapi.h"
#include "globus_gsi_system_config.h"

OM_uint32
globus_i_gss_read_vhost_cred_dir(
    OM_uint32                          *minor_status,
    const char                         *dirname,
    gss_cred_id_t                     **output_credentials_array,
    size_t                             *output_credentials_array_count)
{
    OM_uint32                           major_status = GSS_S_COMPLETE;
    void                               *credential_buffer = NULL;
    gss_cred_id_t                      *credential_array = NULL;
    size_t                              credential_array_count = 0;
    size_t                              credential_array_size = 0;
    char                               *new_dirname = NULL;
    DIR                                *dir_handle = NULL;
    struct dirent                      *dir_entry = NULL;
    int                                 rc = 0;

    if (output_credentials_array == NULL ||
        output_credentials_array_count == NULL)
    {
        major_status = GSS_S_FAILURE | GSS_S_CALL_INACCESSIBLE_WRITE;
        *minor_status = GLOBUS_FAILURE;

        goto invalid_parameter;
    }

    if (dirname == NULL)
    {
        *minor_status = GLOBUS_GSI_SYSCONFIG_GET_VHOST_CRED_DIR(&new_dirname);
        if (*minor_status != GLOBUS_SUCCESS)
        {
            major_status = GSS_S_FAILURE;

            goto no_dir;
        }
        dirname = new_dirname;
    }

    if (dirname == NULL)
    {
        major_status = GSS_S_FAILURE;
        *minor_status = GLOBUS_FAILURE;

        goto no_dir;
    }

    dir_handle = opendir(dirname);
    if (dir_handle == NULL)
    {
        major_status = GSS_S_FAILURE;
        *minor_status = GLOBUS_FAILURE;

        goto opendir_fail;
    }

    while ((rc = globus_libc_readdir_r(dir_handle, &dir_entry)) == 0
        && dir_entry != NULL)
    {
        char                            full_path[
            strlen(dir_entry->d_name) + strlen(dirname) + 2];
        char                            import_name[
            strlen(dir_entry->d_name) + strlen(dirname) + 4];
        struct stat                     st;

        if (strcmp(dir_entry->d_name, ".") == 0
            || strcmp(dir_entry->d_name, "..") == 0)
        {
            goto skip_entry;
        }

        sprintf(full_path, "%s/%s", dirname, dir_entry->d_name);
        rc = stat(full_path, &st);
        if (rc != 0)
        {
            goto skip_entry;
        }

        if ((st.st_mode & S_IFDIR) == 0)
        {
            goto skip_entry;
        }

        sprintf(import_name, "p=%s/%s", dirname, dir_entry->d_name);

        if (credential_array_count == credential_array_size)
        {
            gss_cred_id_t          *tmp = NULL;

            tmp = realloc(credential_array,
                2
                * (credential_array_size ? credential_array_size : 1)
                * sizeof(gss_cred_id_t));

            if (tmp == NULL)
            {
                major_status = GSS_S_FAILURE;
                *minor_status = GLOBUS_FAILURE;

                goto realloc_credential_fail;
            }
            credential_array = tmp;
            credential_array_size = 2
                    * (credential_array_size ? credential_array_size : 1);
        }
        major_status = gss_import_cred(
            minor_status,
            &credential_array[credential_array_count],
            GSS_C_NO_OID,
            1,
            &(gss_buffer_desc)
            {
                .value = import_name,
                .length = strlen(import_name),
            },
            0,
            NULL);

        free(credential_buffer);
        credential_buffer = NULL;

        if (major_status != GSS_S_COMPLETE)
        {
            goto import_cred_fail;
        }
        credential_array_count++;
skip_entry:
        free(dir_entry);
        dir_entry = NULL;
    }
    if (major_status != GSS_S_COMPLETE)
    {
import_cred_fail:
realloc_credential_fail:
        free(credential_buffer);
        credential_buffer = NULL;

        for (size_t i = 0; i < credential_array_count; i++)
        {
            gss_release_cred(
                    minor_status,
                    &credential_array[i]);
        }
        free(credential_array);

        credential_array_count = 0;
        credential_array_size = 0;
        credential_array = NULL;
    }
    if (dir_entry != NULL)
    {
        free(dir_entry);
        dir_entry = NULL;
    }
    closedir(dir_handle);
    dir_handle = NULL;

opendir_fail:
    free(new_dirname);
no_dir:
    *output_credentials_array = credential_array;
    *output_credentials_array_count = credential_array_count;
invalid_parameter:
    return major_status;
}
/* globus_i_gss_read_vhost_cred_dir() */

