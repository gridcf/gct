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

#include "globus_gsi_system_config.h"

int main(int argc, char *argv[])
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    globus_module_activate(GLOBUS_GSI_SYSCONFIG_MODULE);

    if (argc < 2)
    {
        fprintf(stderr, "Error: %s cmd [args..]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    else if (strcmp(argv[1], "set_key_permissions") == 0
        && argc == 3)
    {
        result = GLOBUS_GSI_SYSCONFIG_SET_KEY_PERMISSIONS(argv[2]);
    }
    else if (strcmp(argv[1], "get_home_dir") == 0
        && argc == 2)
    {
        char                           *home_dir = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home_dir);

        if (home_dir != NULL)
        {
            printf("%s\n", home_dir);
        }
        free(home_dir);
    }
    else if (strcmp(argv[1], "file_exists") == 0
        && argc == 3)
    {
        result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(argv[2]);
    }
    else if (strcmp(argv[1], "dir_exists") == 0
        && argc == 3)
    {
        result = GLOBUS_GSI_SYSCONFIG_DIR_EXISTS(argv[2]);
    }
    else if (strcmp(argv[1], "check_keyfile") == 0
        && argc == 3)
    {
        result = GLOBUS_GSI_SYSCONFIG_CHECK_KEYFILE(argv[2]);
    }
    else if (strcmp(argv[1], "check_certfile") == 0
        && argc == 3)
    {
        result = GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE(argv[2]);
    }
    else if (strcmp(argv[1], "get_cert_dir") == 0
        && argc == 2)
    {
        char                           *cert_dir = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&cert_dir);

        if (cert_dir != NULL)
        {
            printf("%s\n", cert_dir);
        }
    }
    else if (strcmp(argv[1], "get_user_cert_filename") == 0
        && argc == 2)
    {
        char                           *user_cert_filename = NULL;
        char                           *user_key_filename = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_USER_CERT_FILENAME(
                &user_cert_filename,
                &user_key_filename);

        if (user_cert_filename != NULL)
        {
            printf("%s\n", user_cert_filename);
        }
        if (user_key_filename != NULL)
        {
            printf("%s\n", user_key_filename);
        }

    }
    else if (strcmp(argv[1], "get_host_cert_filename") == 0
        && argc == 2)
    {
        char                           *host_cert_filename = NULL;
        char                           *host_key_filename = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_HOST_CERT_FILENAME(
                &host_cert_filename,
                &host_key_filename);

        if (host_cert_filename != NULL)
        {
            printf("%s\n", host_cert_filename);
        }
        if (host_key_filename != NULL)
        {
            printf("%s\n", host_key_filename);
        }
    }
    else if (strcmp(argv[1], "get_service_cert_filename") == 0
        && argc == 3)
    {
        char                           *service_cert_filename = NULL;
        char                           *service_key_filename = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_SERVICE_CERT_FILENAME(
                argv[2],
                &service_cert_filename,
                &service_key_filename);

        if (service_cert_filename != NULL)
        {
            printf("%s", service_cert_filename);
        }
        if (service_key_filename != NULL)
        {
            printf("%s", service_key_filename);
        }
    }
    else if (strcmp(argv[1], "get_proxy_filename") == 0
        && argc == 3)
    {
        int                             proxy_file_type = atoi(argv[3]);
        char                           *proxy_filename = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_PROXY_FILENAME(
                &proxy_filename,
                (globus_gsi_proxy_file_type_t) proxy_file_type);

        if (proxy_filename != NULL)
        {
            printf("%s\n", proxy_filename);
        }
    }
    else if (strcmp(argv[1], "get_signing_policy_filename") == 0
        && argc == 4)
    {
        globus_assert_string(0, "Test not implemented");
    }
    else if (strcmp(argv[1], "get_ca_cert_files") == 0
        && argc == 3)
    {
        globus_fifo_t                   ca_cert_list;

        result = GLOBUS_GSI_SYSCONFIG_GET_CA_CERT_FILES(
                argv[2],
                &ca_cert_list);

        if (result == GLOBUS_SUCCESS)
        {
            while (!globus_fifo_empty(&ca_cert_list))
            {
                char                   *cert = NULL;
                
                cert = globus_fifo_dequeue(&ca_cert_list);
                printf("%s\n", cert);
                free(cert);
            }
        }
    }
    else if (strcmp(argv[1], "get_current_working_dir") == 0
        && argc == 2)
    {
        char                           *working_dir = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_CURRENT_WORKING_DIR(&working_dir);

        if (working_dir != NULL)
        {
            printf("%s\n", working_dir);
        }
    }
    else if (strcmp(argv[1], "make_absolute_path_for_filename") == 0
        && argc == 3)
    {
        char                           *absolute_path = NULL;

        result = GLOBUS_GSI_SYSCONFIG_MAKE_ABSOLUTE_PATH_FOR_FILENAME(
                argv[2],
                &absolute_path);

        if (absolute_path != NULL)
        {
            printf("%s\n", absolute_path);
        }
    }
    else if (strcmp(argv[1], "split_dir_and_filename") == 0
        && argc == 3)
    {
        char                           *dir_string = NULL;
        char                           *filename_string = NULL;

        result = GLOBUS_GSI_SYSCONFIG_SPLIT_DIR_AND_FILENAME(
                argv[2],
                &dir_string,
                &filename_string);

        if (dir_string != NULL)
        {
            printf("%s\n", dir_string);
        }
        if (filename_string != NULL)
        {
            printf("%s\n", filename_string);
        }
    }
    else if (strcmp(argv[1], "remove_all_owned_files") == 0
        && argc == 3)
    {
        result = GLOBUS_GSI_SYSCONFIG_REMOVE_ALL_OWNED_FILES(argv[2]);
    }
    else if (strcmp(argv[1], "is_superuser") == 0
        && argc == 2)
    {
        int                             is_superuser = -1;

        result = GLOBUS_GSI_SYSCONFIG_IS_SUPERUSER(&is_superuser);

        printf("%d\n", is_superuser);
    }
    else if (strcmp(argv[1], "get_user_id_string") == 0
        && argc == 2)
    {
        char                           *user_id_string = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_USER_ID_STRING(&user_id_string);

        if (user_id_string != NULL)
        {
            printf("%s\n", user_id_string);
        }
    }
    else if (strcmp(argv[1], "get_username") == 0
        && argc == 2)
    {
        char                           *username = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_USERNAME(
                &username);

        if (username != NULL)
        {
            printf("%s\n", username);
        }
    }
    else if (strcmp(argv[1], "get_proc_id_string") == 0
        && argc == 2)
    {
        char                           *proc_id_string = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_PROC_ID_STRING(
                &proc_id_string);

        if (proc_id_string != NULL)
        {
            printf("%s\n", proc_id_string);
        }
    }
    else if (strcmp(argv[1], "get_gridmap_filename") == 0
        && argc == 2)
    {
        char                           *gridmap_filename = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_GRIDMAP_FILENAME(
                &gridmap_filename);

        if (gridmap_filename != NULL)
        {
            printf("%s\n", gridmap_filename);
        }
    }
    else if (strcmp(argv[1], "get_authz_conf_filename") == 0
        && argc == 2)
    {
        char                           *authz_conf_filename = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_AUTHZ_CONF_FILENAME(
                &authz_conf_filename);

        if (authz_conf_filename != NULL)
        {
            printf("%s\n", authz_conf_filename);
        }
    }
    else if (strcmp(argv[1], "get_unique_proxy_filename") == 0)
    {
        char                           *unique_filename = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_UNIQUE_PROXY_FILENAME(
                &unique_filename);

        if (unique_filename != NULL)
        {
            printf("%s\n", unique_filename);
        }
    }
    else if (strcmp(argv[1], "get_vhost_cred_dir") == 0)
    {
        char                           *dir_name = NULL;

        result = GLOBUS_GSI_SYSCONFIG_GET_VHOST_CRED_DIR(
                &dir_name);

        if (dir_name != NULL)
        {
            printf("%s\n", dir_name);
        }
    }
    else
    {

        fprintf(stderr, "Invalid arguments:");
        for (int i = 1; i < argc; i++)
        {
            fprintf(stderr, " %s", argv[i]);
        }
        fprintf(stderr, "\n");
    }

    if (result != GLOBUS_SUCCESS)
    {
        char                           *msg = NULL;

        msg = globus_error_print_friendly(globus_error_peek(result));

        fprintf(stderr, "ERROR: %s\n", msg ? msg : "Unknown");
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}
