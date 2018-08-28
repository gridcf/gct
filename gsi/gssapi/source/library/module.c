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
 * @file module.c
 * @brief GSSAPI module activation code
 */
#endif

#include "gssapi.h"
#include "version.h"
#include "globus_openssl.h"
#include "globus_i_gsi_gss_utils.h"

#include "gsi.conf.h"

#ifdef WIN32
#define strcasecmp stricmp
#define strncasecmp strnicmp
#else
#include <strings.h>
#include <pwd.h>
#endif

static int globus_l_gsi_gssapi_activate(void);
static int globus_l_gsi_gssapi_deactivate(void);

/**
 * @brief Debugging level
 * @details
 * Currently this isn't terribly well defined. The idea is that 0 is no
 * debugging output, and 9 is a whole lot.
 */
int                                     globus_i_gsi_gssapi_debug_level;

/**
 * @brief Debugging Log File
 * @details
 * Debugging output gets written to this file
 */
FILE *                                  globus_i_gsi_gssapi_debug_fstream;

/**
 * @brief Minimum TLS protocol version
 * @details
 * Choose the minimum TLS protocol version to support. One of TLS1_VERSION,
 * TLS1_1_VERSION, TLS1_2_VERSION or 0 for lowest (TLS1_VERSION). SSLv3
 * and below disallowed.
 */
int                               globus_i_gsi_gssapi_min_tls_protocol;

/**
 * @brief Maximum TLS protocol version
 * @details
 * Choose the maximum TLS protocol version to support. One of TLS1_VERSION,
 * TLS1_1_VERSION, TLS1_2_VERSION or 0 for highest. SSLv3 and below disallowed.
 */
int                               globus_i_gsi_gssapi_max_tls_protocol;

/**
 * @brief SSL Cipher List
 * @details
 * Choose the default set of ciphers to support
 */
const char *                            globus_i_gsi_gssapi_cipher_list;
/**
 * @brief VHost cert owner
 */
uid_t                                   globus_i_gsi_gssapi_vhost_cred_owner;

/**
 * @brief Honor Server SSL Cipher List Order
 * @details
 *
 * Choose whether to assume the server ciphers are ordered by preference
 */
globus_bool_t                           globus_i_gsi_gssapi_server_cipher_order ;

globus_bool_t                           globus_i_backward_compatible_mic = GLOBUS_TRUE;

globus_bool_t                           globus_i_accept_backward_compatible_mic = GLOBUS_TRUE;
/**
 * Module descriptor static initializer.
 */
globus_module_descriptor_t		globus_i_gsi_gssapi_module =
{
    "globus_gsi_gssapi",
    globus_l_gsi_gssapi_activate,
    globus_l_gsi_gssapi_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

/**
 * State variables needed for dealing with the case when globus module
 * activation isn't used.
 *
 */

globus_thread_once_t                once_control = GLOBUS_THREAD_ONCE_INIT;
globus_mutex_t                      globus_i_gssapi_activate_mutex;
globus_bool_t                       globus_i_gssapi_active = GLOBUS_FALSE;

static 
int
globus_l_gsi_gssapi_read_config(char **gsi_conf_datap)
{
    char *                              gsi_conf_data = NULL;
    char *                              gsi_conf_path = NULL;
    int                                 gsi_conf = -1;
    struct stat                         st = {0};
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 rc = GLOBUS_SUCCESS;

    result = globus_eval_path("${sysconfdir}/grid-security/gsi.conf",
            &gsi_conf_path);
    if (result != GLOBUS_SUCCESS)
    {
        rc = result;
        goto path_eval_fail;
    }

    gsi_conf = open(gsi_conf_path, O_RDONLY);
    if (gsi_conf != -1)
    {
        size_t gsi_conf_size, remain;

        rc = fstat(gsi_conf, &st);
        if (rc != 0)
        {
            rc = GLOBUS_FAILURE;
            goto fstat_fail;
        }

        if (st.st_size + 1 > SIZE_MAX)
        {
            rc = GLOBUS_FAILURE;
            goto too_big_fail;
        }
        remain = gsi_conf_size = (size_t) st.st_size;

        gsi_conf_data = malloc(gsi_conf_size + 1);
        if (gsi_conf_data == NULL)
        {
            rc = GLOBUS_FAILURE;
            goto conf_data_malloc_fail;
        }
        gsi_conf_data[gsi_conf_size] = '\0';
        do
        {
            rc = read(gsi_conf, gsi_conf_data + gsi_conf_size - remain, remain);
            if (rc < 0)
            {
                if (errno == EINTR || errno == EBUSY || errno == EAGAIN)
                {
                    rc = 0;
                }
                else
                {
                    rc = GLOBUS_FAILURE;
                    goto read_conf_data_fail;
                }
            }
            remain -= rc;
        } while (remain > 0);
        rc = GLOBUS_SUCCESS;
    }
    else
    {
        rc = GLOBUS_SUCCESS;
    }

read_conf_data_fail:
    if (rc != GLOBUS_SUCCESS)
    {
        free(gsi_conf_data);
        gsi_conf_data = NULL;
    }
conf_data_malloc_fail:
too_big_fail:
fstat_fail:
    free(gsi_conf_path);
    if (gsi_conf != -1)
    {
        close(gsi_conf);
    }
path_eval_fail:
    *gsi_conf_datap = gsi_conf_data;
    return rc;
}
/* globus_l_gsi_gssapi_read_config() */

static
void
globus_l_gsi_trim_whitespace(
    char                                *s)
{
    char *t = s;
    char *n;

    while (*t && isspace(*t))
    {
        t++;
    }
    if (t != s)
    {
        memmove(s, t, strlen(t)+1);
    }
    t = n = s;

    // Move t along, keeping n at the last non-whitespace we see
    while (*t)
    {
        while (*t && !isspace(*t))
        {
            n = t++;
        }
        while (*t && isspace(*t))
        {
            t++;
        }
    }
    *(n+1) = '\0';
}
/* globus_l_gsi_trim_whitespace() */

static
int
globus_l_gsi_gssapi_parse_config(
    char *gsi_conf_data)
{
    char                               *p = gsi_conf_data;
    char                               *n = NULL; //newline
    char                               *c = NULL; //comment-start
    char                               *e = NULL; //equal
    int                                 rc = GLOBUS_SUCCESS;
    const char                          conf_key_prefix[] = "GLOBUS_GSSAPI_";
    const char                         *conf_keys[] = {
        "GLOBUS_GSSAPI_NAME_COMPATIBILITY",
        "GLOBUS_GSSAPI_MIN_TLS_PROTOCOL",
        "GLOBUS_GSSAPI_MAX_TLS_PROTOCOL",
        "GLOBUS_GSSAPI_CIPHERS",
        "GLOBUS_GSSAPI_SERVER_CIPHER_ORDER",
        "GLOBUS_GSSAPI_BACKWARD_COMPATIBLE_MIC",
        "GLOBUS_GSSAPI_VHOST_CRED_OWNER",
        NULL
    };

    while (p && *p)
    {
        n = strchr(p, '\n');
        if (n != NULL)
        {
            *n = '\0';
        }
        c = strchr(p, '#');
        if (c)
        {
            *c = '\0';
        }
        if (*p)
        {
            e = strchr(p, '=');
            if (e == NULL)
            {
                rc = GLOBUS_FAILURE;
                goto conf_parse_error;
            }
            *e = '\0';
            globus_l_gsi_trim_whitespace(p);
            if (strlen(e+1) > 0)
            {
                e++;
                globus_l_gsi_trim_whitespace(e);

                for (int i = 0; conf_keys[i] != NULL; i++)
                {
                    if (strcmp(p, conf_keys[i]+sizeof(conf_key_prefix)-1) == 0
                        && !getenv(conf_keys[i]))
                    {
                        char *newe = strdup(e);
                        if (!newe)
                        {
                            rc = GLOBUS_FAILURE;
                            goto conf_set_fail;
                        }
                        globus_module_setenv(conf_keys[i], newe);
                        if (getenv(conf_keys[i]) == NULL)
                        {
                            globus_libc_setenv(conf_keys[i], newe, 1);
                        }

                        break;
                    }
                }
            }
        }
        if (n == NULL)
        {
            p = NULL;
        }
        else
        {
            p = n+1;
        }
    }
conf_set_fail:
conf_parse_error:
    return rc;
}

/**
 * Module activation
 */
static
int
globus_l_gsi_gssapi_activate(void)
{
    int                                 rc = GLOBUS_SUCCESS;
    char *                              tmp_string;
    char *                              gsi_conf_data;

    rc = globus_module_activate(GLOBUS_COMMON_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto common_activate_fail;
    }
    rc = globus_l_gsi_gssapi_read_config(&gsi_conf_data);
    if (rc != GLOBUS_SUCCESS)
    {
        goto read_conf_data_fail;
    }
    if (gsi_conf_data == NULL)
    {
        gsi_conf_data = strdup(globus_l_gsi_conf_string);
        if (gsi_conf_data == NULL)
        {
            rc = GLOBUS_FAILURE;
            goto strdup_default_data_fail;
        }
    }
    /* Don't allow an environment override */
    globus_libc_unsetenv("GLOBUS_GSSAPI_VHOST_CRED_OWNER");
    rc = globus_l_gsi_gssapi_parse_config(gsi_conf_data);
    if (rc != GLOBUS_SUCCESS)
    {
        goto parse_conf_data_fail;
    }
    
    tmp_string = globus_module_getenv("GLOBUS_GSSAPI_DEBUG_LEVEL");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_gsi_gssapi_debug_level = atoi(tmp_string);
    
        if(globus_i_gsi_gssapi_debug_level < 0)
        {
            globus_i_gsi_gssapi_debug_level = 0;
        }
    }

    tmp_string = globus_module_getenv("GLOBUS_GSSAPI_DEBUG_FILE");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_gsi_gssapi_debug_fstream = fopen(tmp_string, "a");
        if(!globus_i_gsi_gssapi_debug_fstream)
        {
            rc = GLOBUS_FAILURE;
            goto debug_open_fail;
        }
    }
    else
    {
        globus_i_gsi_gssapi_debug_fstream = stderr;
        if(!globus_i_gsi_gssapi_debug_fstream)
        {
            rc = GLOBUS_FAILURE;
            goto debug_stderr_fail;
        }
    }

    tmp_string = globus_module_getenv("GLOBUS_GSSAPI_NAME_COMPATIBILITY");
    if(tmp_string != NULL)
    {
        if (strcmp(tmp_string, "STRICT_GT2") == 0)
        {
            gss_i_name_compatibility_mode = GSS_I_COMPATIBILITY_STRICT_GT2;
        }
        else if (strcmp(tmp_string, "STRICT_RFC2818") == 0)
        {
            gss_i_name_compatibility_mode = GSS_I_COMPATIBILITY_STRICT_RFC2818;
        }
        else if (strcmp(tmp_string, "HYBRID") == 0)
        {
            gss_i_name_compatibility_mode = GSS_I_COMPATIBILITY_HYBRID;
        }
        else
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(
                1,
                (_GGSL("Unknown GLOBUS_GSSAPI_NAME_COMPATIBILITY value: %s\n"),
                        tmp_string));
            gss_i_name_compatibility_mode = GSS_I_COMPATIBILITY_STRICT_RFC2818;
        }
    }
    else
    {
        gss_i_name_compatibility_mode = GSS_I_COMPATIBILITY_STRICT_RFC2818;
    }

    tmp_string = globus_module_getenv("GLOBUS_GSSAPI_MIN_TLS_PROTOCOL");
    if(tmp_string != NULL)
    {
        if (strcmp(tmp_string, "TLS1_VERSION_DEPRECATED") == 0)
        {
            globus_i_gsi_gssapi_min_tls_protocol = TLS1_VERSION;
        }
        else if (strcmp(tmp_string, "TLS1_1_VERSION_DEPRECATED") == 0)
        {
            globus_i_gsi_gssapi_min_tls_protocol = TLS1_1_VERSION;
        }
        else if (strcmp(tmp_string, "TLS1_2_VERSION") == 0)
        {
            globus_i_gsi_gssapi_min_tls_protocol = TLS1_2_VERSION;
        }
        else if (strcmp(tmp_string, "0") == 0)
        {
            globus_i_gsi_gssapi_min_tls_protocol = 0;
        }
        else
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(
                1,
                (_GGSL("Unknown GLOBUS_GSSAPI_MIN_TLS_PROTOCOL value: %s;"
                       "defaulting to TLS1_2_VERSION\n"),
                        tmp_string));
            globus_i_gsi_gssapi_min_tls_protocol = TLS1_2_VERSION;
        }
    }
    else
    {
        globus_i_gsi_gssapi_min_tls_protocol = TLS1_2_VERSION;
    }

    tmp_string = globus_module_getenv("GLOBUS_GSSAPI_MAX_TLS_PROTOCOL");
    if(tmp_string != NULL)
    {
        if (strcmp(tmp_string, "TLS1_VERSION_DEPRECATED") == 0)
        {
            globus_i_gsi_gssapi_max_tls_protocol = TLS1_VERSION;
        }
        else if (strcmp(tmp_string, "TLS1_1_VERSION_DEPRECATED") == 0)
        {
            globus_i_gsi_gssapi_max_tls_protocol = TLS1_1_VERSION;
        }
        else if (strcmp(tmp_string, "TLS1_2_VERSION") == 0)
        {
            globus_i_gsi_gssapi_max_tls_protocol = TLS1_2_VERSION;
        }
        else if (strcmp(tmp_string, "0") == 0)
        {
            globus_i_gsi_gssapi_max_tls_protocol = 0;
        }
        else
        {
            GLOBUS_I_GSI_GSSAPI_DEBUG_PRINT(
                1,
                (_GGSL("Unknown GLOBUS_GSSAPI_MIN_TLS_PROTOCOL value: %s;"
                       "defaulting to 0 (highest)\n"),
                        tmp_string));
            globus_i_gsi_gssapi_max_tls_protocol = 0;
        }
    }
    else
    {
        globus_i_gsi_gssapi_max_tls_protocol = 0;
    }

    tmp_string = globus_module_getenv("GLOBUS_GSSAPI_CIPHERS");
    if (tmp_string != NULL)
    {
        globus_i_gsi_gssapi_cipher_list = tmp_string;
    }

    tmp_string = globus_module_getenv("GLOBUS_GSSAPI_SERVER_CIPHER_ORDER");
    if (tmp_string != NULL)
    {
        if (strcasecmp(tmp_string, "true") == 0 ||
            strcasecmp(tmp_string, "yes") == 0 ||
            strcmp(tmp_string, "1") == 0)
        {
            globus_i_gsi_gssapi_server_cipher_order = GLOBUS_TRUE;
        }
    }
#ifndef WIN32
    tmp_string = globus_module_getenv("GLOBUS_GSSAPI_VHOST_CRED_OWNER");
    if(tmp_string != GLOBUS_NULL)
    {
        long                            buflen = -1;
        buflen = sysconf(_SC_GETPW_R_SIZE_MAX);

        assert(buflen > 0);
        char buffer[buflen];
        struct passwd pwd = {0};
        struct passwd *res = NULL;
        
        rc = getpwnam_r(tmp_string, &pwd, buffer, (size_t) buflen, &res);

        if (rc == 0)
        {
            globus_i_gsi_gssapi_vhost_cred_owner = pwd.pw_uid;
        }
        else
        {
            globus_i_gsi_gssapi_vhost_cred_owner = 0;
        }
    }
#endif

    if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    {
        globus_i_backward_compatible_mic = GLOBUS_TRUE;
        globus_i_accept_backward_compatible_mic = GLOBUS_TRUE;

        tmp_string = globus_module_getenv(
                "GLOBUS_GSSAPI_BACKWARD_COMPATIBLE_MIC");
        if (tmp_string != NULL
            && (strcasecmp(tmp_string, "false") == 0 ||
            strcasecmp(tmp_string, "no") == 0 ||
            strcmp(tmp_string, "0") == 0))
        {
            globus_i_backward_compatible_mic = GLOBUS_FALSE;
        }
        tmp_string = globus_module_getenv(
                "GLOBUS_GSSAPI_ACCEPT_BACKWARD_COMPATIBLE_MIC");
        if (tmp_string != NULL
            && (strcasecmp(tmp_string, "false") == 0 ||
            strcasecmp(tmp_string, "no") == 0 ||
            strcmp(tmp_string, "0") == 0))
        {
            globus_i_accept_backward_compatible_mic = GLOBUS_FALSE;
        }
    }
    else
    {
        globus_i_backward_compatible_mic = GLOBUS_FALSE;
        globus_i_accept_backward_compatible_mic = GLOBUS_FALSE;
    }

    rc = globus_module_activate(GLOBUS_OPENSSL_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto activate_openssl_module_fail;
    }
    rc = globus_module_activate(GLOBUS_GSI_PROXY_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto activate_gsi_proxy_fail;
    }
    rc = globus_module_activate(GLOBUS_GSI_CALLBACK_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        goto activate_gsi_callback_fail;
    }

    GLOBUS_I_GSI_GSSAPI_INTERNAL_DEBUG_EXIT;

    globus_i_gssapi_active = GLOBUS_TRUE;

    if (rc != GLOBUS_SUCCESS)
    {
activate_gsi_callback_fail:
        globus_module_deactivate(GLOBUS_GSI_PROXY_MODULE);
activate_gsi_proxy_fail:
        globus_module_deactivate(GLOBUS_OPENSSL_MODULE);
activate_openssl_module_fail:
        if (globus_i_gsi_gssapi_debug_fstream != NULL &&
            globus_i_gsi_gssapi_debug_fstream != stderr)
        {
            fclose(globus_i_gsi_gssapi_debug_fstream);
            globus_i_gsi_gssapi_debug_fstream = NULL;
        }
    }
debug_stderr_fail:
debug_open_fail:
parse_conf_data_fail:
strdup_default_data_fail:
    free(gsi_conf_data);
read_conf_data_fail:
    if (rc != GLOBUS_SUCCESS)
    {
        globus_module_deactivate(GLOBUS_COMMON_MODULE);
    }
common_activate_fail:
    return rc;
}
/* globus_l_gsi_gssapi_activate() */

/**
 * Module deactivation
 *
 */
static
int
globus_l_gsi_gssapi_deactivate(void)
{
    GLOBUS_I_GSI_GSSAPI_DEBUG_ENTER;

    globus_module_deactivate(GLOBUS_GSI_CALLBACK_MODULE);
    globus_module_deactivate(GLOBUS_GSI_PROXY_MODULE);
    globus_module_deactivate(GLOBUS_OPENSSL_MODULE);
    globus_module_deactivate(GLOBUS_COMMON_MODULE);
    globus_i_gssapi_active = GLOBUS_FALSE;
    GLOBUS_I_GSI_GSSAPI_INTERNAL_DEBUG_EXIT;

    return GLOBUS_SUCCESS;
}
/* globus_l_gsi_gssapi_deactivate() */

void
globus_l_gsi_gssapi_activate_once(void)
{
    globus_mutex_init(&globus_i_gssapi_activate_mutex, NULL);
}
