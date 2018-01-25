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
 * @file globus_gsi_system_config.c
 * @brief Globus GSI System Config Library
 * @author Sam Lang, Sam Meder
 */
#endif

#include "globus_common.h"
#include "globus_gsi_system_config.h"
#include "globus_i_gsi_system_config.h"
#include "openssl/rand.h"
#ifndef WIN32
#include <pwd.h>
#endif
#include <time.h>
#include <errno.h>
#ifndef WIN32
#include <sys/times.h>
#endif
#include "version.h"

#ifdef WIN32
/* Note: On the Windows side the default paths must be determined at
         runtime, so they can't be defined by constants as the Unix
         default paths are. Instead a set of functions that return
         appropriate paths and filenames have been implemented. */

/* Function Prototypes */
const char *win32_secure_path(void);
const char *win32_cwd(void);
const char *x509_installed_trusted_cert_dir(void);
const char *x509_installed_cert_dir(void);
const char *installed_gridmap(void);
const char *installed_authz_file(void);
const char *win32_etc(void);
const char *x509_default_trusted_cert_dir(void);
const char *x509_default_cert_dir(void);
const char *default_gridmap(void);
const char *default_authz_file(void);
const char *default_gaa_file(void);

#define WIN32_FALLBACK_PATH             "c:\\temp"
#define WIN32_SECURE_PATH               win32_secure_path()
#endif

#ifndef DEFAULT_SECURE_TMP_DIR
#ifdef WIN32
#define DEFAULT_SECURE_TMP_DIR          WIN32_SECURE_PATH
#else
#define DEFAULT_SECURE_TMP_DIR          "/tmp"
#endif
#endif

#ifndef DEFAULT_EGD_PATH
#define DEFAULT_EGD_PATH                DEFAULT_SECURE_TMP_DIR
#endif

#ifndef DEFAULT_RANDOM_FILE
#define DEFAULT_RANDOM_FILE             DEFAULT_SECURE_TMP_DIR
#endif

#ifdef WIN32
#include <io.h>
#else
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#endif

/* ToDo: HACK! This is undefined on the Windows side so do this for now */
#ifdef WIN32
#define flavor "win32dbg"
#endif

#define X509_CERT_DIR                   "X509_CERT_DIR"
#define X509_CERT_FILE                  "X509_CERT_FILE"
#define X509_USER_PROXY                 "X509_USER_PROXY"
#define X509_USER_CERT                  "X509_USER_CERT"
#define X509_USER_KEY                   "X509_USER_KEY"
#define X509_UNIQUE_PROXY_FILE          "x509up_p"
#define X509_USER_PROXY_FILE            "x509up_u"
#define SIGNING_POLICY_FILE_EXTENSION   ".signing_policy"
#define X509_VHOST_CRED_DIR             "X509_VHOST_CRED_DIR"

/* This is added after the CA name hash to make the policy filename */
#define SIGNING_POLICY_FILE_EXTENSION   ".signing_policy"

/* Win32 Definitions */
#ifdef WIN32
#define FILE_SEPARATOR                  "\\"
#define X509_DEFAULT_USER_CERT          ".globus\\usercert.pem"
#define X509_DEFAULT_USER_KEY           ".globus\\userkey.pem"
#define X509_DEFAULT_PKCS12_FILE        ".globus\\usercred.p12"
#define X509_DEFAULT_TRUSTED_CERT_DIR   x509_default_trusted_cert_dir()
#define X509_INSTALLED_TRUSTED_CERT_DIR x509_installed_trusted_cert_dir()
#define X509_LOCAL_TRUSTED_CERT_DIR     ".globus\\certificates"
#define X509_DEFAULT_CERT_DIR           x509_default_cert_dir()
#define X509_INSTALLED_CERT_DIR         x509_installed_cert_dir()
#define X509_LOCAL_CERT_DIR             ".globus"
#define DEFAULT_GRIDMAP                 default_gridmap()
#define INSTALLED_GRIDMAP               installed_gridmap()
#define LOCAL_GRIDMAP                   ".gridmap"
#define DEFAULT_AUTHZ_FILE              ".gsi-authz.conf"
#define INSTALLED_AUTHZ_FILE            installed_authz_file()
#define LOCAL_AUTHZ_FILE                ".gsi-authz.conf"

/* Note: Authz Lib Is Going Away So These Definitions Should Be OK */
#define DEFAULT_AUTHZ_LIB_FILE_BASE     "gsi-authz_lib"
#define DEFAULT_AUTHZ_LIB_FILE_DIR      "\\etc\\grid-security\\"
#define DEFAULT_AUTHZ_LIB_FILE_EXTENSION ".conf"
#define HOME_AUTHZ_LIB_FILE_BASE        ".gsi-authz_lib"
#define INSTALLED_AUTHZ_LIB_DIR         "etc\\"

#define DEFAULT_GAA_FILE                default_gaa_file()
#define INSTALLED_GAA_FILE              "etc\\gsi-gaa.conf"  /* Relative to CWD*/
#define LOCAL_GAA_FILE                  ".gsi-gaa.conf"      /* Relative to CWD */


#else /* UNIX definitions */
#define FILE_SEPARATOR                  "/"
#define X509_DEFAULT_USER_CERT          ".globus/usercert.pem"
#define X509_DEFAULT_USER_KEY           ".globus/userkey.pem"
#define X509_DEFAULT_PKCS12_FILE        ".globus/usercred.p12"
#define X509_DEFAULT_TRUSTED_CERT_DIR   "/etc/grid-security/certificates"
#define X509_INSTALLED_TRUSTED_CERT_DIR "share/certificates"
#define X509_LOCAL_TRUSTED_CERT_DIR     ".globus/certificates"
#define X509_DEFAULT_CERT_DIR           "/etc/grid-security"
#define X509_INSTALLED_CERT_DIR         "etc"
#define X509_LOCAL_CERT_DIR             ".globus"
#define DEFAULT_GRIDMAP                 "/etc/grid-security/grid-mapfile"
#define INSTALLED_GRIDMAP               "etc/grid-mapfile"
#define LOCAL_GRIDMAP                   ".gridmap"
#define DEFAULT_AUTHZ_FILE              "/etc/grid-security/gsi-authz.conf"
#define INSTALLED_AUTHZ_FILE            "etc/gsi-authz.conf"
#define LOCAL_AUTHZ_FILE                ".gsi-authz.conf"
#define DEFAULT_AUTHZ_LIB_FILE_BASE     "gsi-authz_lib"
#define DEFAULT_AUTHZ_LIB_FILE_DIR      "/etc/grid-security/"
#define DEFAULT_AUTHZ_LIB_FILE_EXTENSION ".conf"
#define INSTALLED_AUTHZ_LIB_DIR         "etc/"
#define HOME_AUTHZ_LIB_FILE_BASE        ".gsi-authz_lib"
#define DEFAULT_GAA_FILE                "/etc/grid-security/gsi-gaa.conf"
#define INSTALLED_GAA_FILE              "etc/gsi-gaa.conf"
#define LOCAL_GAA_FILE                  ".gsi-gaa.conf"

#endif

#define X509_HOST_PREFIX                "host"
#define X509_CERT_SUFFIX                "cert.pem"
#define X509_KEY_SUFFIX                 "key.pem"

#define X509_HASH_LENGTH                8

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

#define GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR \
    globus_error_put(globus_error_wrap_errno_error( \
        GLOBUS_GSI_SYSCONFIG_MODULE, \
        errno, \
        GLOBUS_GSI_SYSCONFIG_ERROR_ERRNO, \
        __FILE__, \
        __func__, \
        __LINE__, \
        "Could not allocate enough memory"))


int                                     globus_i_gsi_sysconfig_debug_level;
FILE *                                  globus_i_gsi_sysconfig_debug_fstream;

static int globus_l_gsi_sysconfig_activate(void);
static int globus_l_gsi_sysconfig_deactivate(void);

int globus_i_gsi_sysconfig_debug_level = 0;


#ifdef TARGET_ARCH_CYGWIN
static int globus_l_gsi_sysconfig_skip_check = 0;
#endif

/**
 * Module descriptor static initializer.
 */
globus_module_descriptor_t globus_i_gsi_sysconfig_module =
{
    "globus_sysconfig",
    globus_l_gsi_sysconfig_activate,
    globus_l_gsi_sysconfig_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

/**
 * Module activation
 */
static
int
globus_l_gsi_sysconfig_activate(void)
{
    int                                 result = (int) GLOBUS_SUCCESS;
    const char *                        random_file = NULL;
#ifndef OPENSSL_NO_EGD
    char *                              egd_path = NULL;
#endif
    #ifndef WIN32
    clock_t                             uptime;
    struct tms                          proc_times;
    #endif
    char                                buffer[200];
    char *                              tmp_string;

    tmp_string = getenv("GLOBUS_GSI_SYSCONFIG_DEBUG_LEVEL");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_gsi_sysconfig_debug_level = atoi(tmp_string);

        if(globus_i_gsi_sysconfig_debug_level < 0)
        {
            globus_i_gsi_sysconfig_debug_level = 0;
        }
    }

    tmp_string = getenv("GLOBUS_GSI_SYSCONFIG_DEBUG_FILE");
    if(tmp_string != GLOBUS_NULL)
    {
        globus_i_gsi_sysconfig_debug_fstream = fopen(tmp_string, "w");
        if(globus_i_gsi_sysconfig_debug_fstream == NULL)
        {
            result = (int) GLOBUS_FAILURE;
            goto exit;
        }
    }
    else
    {
        /* if the env. var. isn't set, use stderr */
        globus_i_gsi_sysconfig_debug_fstream = stderr;
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

#ifdef TARGET_ARCH_CYGWIN
    /* windows permissions are a pain, and cygwin won't give reliable 
    permissions when a non-posix path is used anyways */
    if(getenv("GLOBUS_IGNORE_CRED_PERMS"))
    {
        globus_l_gsi_sysconfig_skip_check = 1;
    }
#endif

    result = globus_module_activate(GLOBUS_COMMON_MODULE);

    if(result != 0)
    {
        return result;
    }

    /* OpenSSL's random generator is fed with random
     * information, which requires system dependant information
     * (path names)
     */

    random_file = RAND_file_name(buffer, 200);
    if(random_file)
    {
        RAND_load_file(random_file, 1024L * 1024L);
    }

#ifndef OPENSSL_NO_EGD
    egd_path = getenv("EGD_PATH");
    if(egd_path == NULL)
    {
        egd_path = DEFAULT_EGD_PATH;
    }

    RAND_egd(egd_path);
#endif

    if(RAND_status() == 0)
    {
        /* this function does a RAND_add based on the
         * filename - provides platform independence
         */

        GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(DEFAULT_RANDOM_FILE);
		/* ToDo: look at return code? */

        /* probably overestimating the entropy in the below */
#ifndef WIN32	/* ToDo: Do this for Win32? */
        uptime = times(&proc_times);

        RAND_add((void *) &uptime, sizeof(clock_t), 2);
        RAND_add((void *) &proc_times, sizeof(struct tms), 8);
#endif
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_FPRINTF(
        2, (globus_i_gsi_sysconfig_debug_fstream,
            "RAND_status = %d\n", RAND_status()));

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;

 exit:
    return result;
}

/**
 * Module deactivation
 *
 */
static
int
globus_l_gsi_sysconfig_deactivate(void)
{
    int                                 result = (int) GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    RAND_cleanup();

    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;

    if(globus_i_gsi_sysconfig_debug_fstream != stderr)
    {
        fclose(globus_i_gsi_sysconfig_debug_fstream);
    }

    return result;
}
/* globus_l_gsi_sysconfig_deactivate() */


globus_result_t
globus_i_gsi_sysconfig_create_cert_dir_string(
    char **                             cert_dir,
    char **                             cert_dir_value,
    const char *                        format,
    ...)
{
    va_list                             ap;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *cert_dir = NULL;

    va_start(ap, format);

    *cert_dir_value = globus_common_v_create_string(format, ap);

    va_end(ap);

    if(*cert_dir_value == NULL)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    result = GLOBUS_GSI_SYSCONFIG_DIR_EXISTS(*cert_dir_value);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
        free(*cert_dir_value);
        goto exit;
    }

    *cert_dir = *cert_dir_value;

    result = GLOBUS_SUCCESS;

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}


globus_result_t
globus_i_gsi_sysconfig_create_cert_string(
    char **                             cert_string,
    char **                             cert_string_value,
    const char *                        format,
    ...)
{
    va_list                             ap;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *cert_string = NULL;

    va_start(ap, format);

    *cert_string_value = globus_common_v_create_string(format, ap);

    va_end(ap);

    if(*cert_string_value == NULL)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    result = GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE(*cert_string_value);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
        free(*cert_string_value);
        goto exit;
    }

    *cert_string = *cert_string_value;

    result = GLOBUS_SUCCESS;

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

globus_result_t
globus_i_gsi_sysconfig_create_key_string(
    char **                             key_string,
    char **                             key_string_value,
    const char *                        format,
    ...)
{
    va_list                             ap;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *key_string = NULL;

    va_start(ap, format);

    *key_string_value = globus_common_v_create_string(format, ap);

    va_end(ap);

    if(*key_string_value == NULL)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    result = GLOBUS_GSI_SYSCONFIG_CHECK_KEYFILE(*key_string_value);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
        goto exit;
    }

    *key_string = *key_string_value;

    result = GLOBUS_SUCCESS;

 exit:

    if(*key_string_value &&
       *key_string_value != *key_string)
    {
        free(*key_string_value);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */

#if defined(WIN32) || defined(DOXYGEN)  /* define all the *_win32 functions */

/* These UNIX Macros are undefined on the windows side */
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)


/**
 * @brief Win32 - Set Key Permissions
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Set the file permissions of a file to read only by the user
 * which are the permissions that should be set for all private keys.
 *
 * @param filename
 *
 * @return
 *        GLOBUS_SUCCESS or an error object id
 */
globus_result_t
globus_gsi_sysconfig_set_key_permissions_win32(
    char *                              filename)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    int					                fd = -1;
    struct _stat                        stx;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if((fd = _open(filename, O_RDONLY|O_CREAT)) < 0)
    {
        result = globus_error_put(
            globus_error_wrap_errno_error(
                GLOBUS_GSI_SYSCONFIG_MODULE,
                errno,
                GLOBUS_GSI_SYSCONFIG_ERROR_ERRNO,
                __FILE__,
                __func__,
                __LINE__,
                "Error opening keyfile for reading\n"));
        goto exit;
    }

    if(_fstat(fd, &stx) != 0)
    {
        result = globus_error_put(
            globus_error_wrap_errno_error(
                GLOBUS_GSI_SYSCONFIG_MODULE,
                errno,
                GLOBUS_GSI_SYSCONFIG_ERROR_ERRNO,
                __FILE__,
                __func__,
                __LINE__,
                "Error getting status of keyfile\n"));
        goto exit;
    }

    /*
     * use any stat output as random data, as it will 
     * have file sizes, and last use times in it. 
     */
    RAND_add((void*)&stx, sizeof(stx), 2);

    if(S_ISDIR(stx.st_mode))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_IS_DIR,
            (_GSSL("File: %s"), filename));
        goto exit;
    }
    else if(!S_ISREG(stx.st_mode))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_REGULAR,
            (_GSSL("File: %s"), filename));
        goto exit;
    }
    else if(stx.st_nlink != 1)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_HAS_LINKS,
            (_GSSL("File: %s"), filename));
        goto exit;
    }

    if(_chmod(filename, S_IREAD) < 0)
    {
        result = globus_error_put(
            globus_error_wrap_errno_error(
                GLOBUS_GSI_SYSCONFIG_MODULE,
                errno,
                GLOBUS_GSI_SYSCONFIG_ERROR_SETTING_PERMS,
                __FILE__,
                __func__,
                __LINE__,
                "Error setting permissions to user read only of file: %s\n", 
                filename));
        goto exit;
    }

 exit:
    if (fd >= 0)
    {
	close(fd);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

/**
 * @brief Win32 - Get HOME Directory
 * @ingroup globus_i_gsi_sysconfig_win32
 * @details
 * Get the HOME directory, currently c:\windows
 * 
 * @param home_dir
 *        The home directory of the current user
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise
 *        an error object is returned.
 */
globus_result_t
globus_gsi_sysconfig_get_home_dir_win32(
    char **                             home_dir)
{
    char *                              temp_home_dir;
    char                                buffer[256];
    char *                              home_drive = NULL;
    char *                              home_path  = NULL;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *home_dir = NULL;

    home_drive = getenv("HOMEDRIVE");
    if(home_drive == NULL)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
        	result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_HOME_DIR,
            (_GSSL("Could not get a home directory for this machine")));

        goto exit;
    }

    home_path = getenv("HOMEPATH");
    if(home_path == NULL)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
        	result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_HOME_DIR,
            (_GSSL("Could not get a home directory for this machine")));

        goto exit;
    }

    /* Build the composite string */
    sprintf(buffer,"%s%s",home_drive,home_path);

    temp_home_dir = malloc(strlen(buffer) + 1);

    if(temp_home_dir)
    {
        strncpy(temp_home_dir, buffer, strlen(buffer) + 1);
        result = GLOBUS_GSI_SYSCONFIG_DIR_EXISTS(temp_home_dir);
        if(result != GLOBUS_SUCCESS)
        {
            free(temp_home_dir);
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_HOME_DIR);
            goto exit;
        }

        *home_dir = temp_home_dir;
    }
    else
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_HOME_DIR,
            (_GSSL("Could not get a defined HOME directory\n")));
        goto exit;
    }

    result = GLOBUS_SUCCESS;

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

#endif

/**
 * @brief Win32 - File Exists
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Check that the file exists
 *
 * @param filename the file to check
 *
 * @return 
 *        GLOBUS_SUCCESS (even if the file doesn't exist) - in some
 *        abortive cases an error object identifier is returned
 */
globus_result_t
globus_gsi_sysconfig_file_exists_win32(
    const char *                        filename)
{
    struct stat                         stx;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if (stat(filename,&stx) == -1)
    {
        switch(errno)
        {
          case ENOENT:
          case ENOTDIR:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_DOES_NOT_EXIST,
                (_GSSL("%s is not a valid file"), filename));            
            goto exit;

          case EACCES:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_BAD_PERMISSIONS,
                (_GSSL("Could not read %s"), filename));            
            goto exit;

          default:
            result = globus_error_put(
                globus_error_wrap_errno_error(
                    GLOBUS_GSI_SYSCONFIG_MODULE,
                    errno,
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHECKING_FILE_EXISTS,
                    __FILE__,
                    __func__,
                    __LINE__,
                    "Error getting status of file: %s\n",
                    filename));
            goto exit;

        }
    }

    /*
     * use any stat output as random data, as it will 
     * have file sizes, and last use times in it. 
     */
    RAND_add((void*)&stx,sizeof(stx),2);

    if (stx.st_size == 0)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_ZERO_LENGTH,
            (_GSSL("File: %s"), filename));            
        goto exit;
    }

    if(stx.st_mode & S_IFDIR)
    { 
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_IS_DIR,
            (_GSSL("File: %s"), filename));       
    }
    else if((stx.st_mode & S_IFMT) &
            ~ (S_IFREG | S_IFDIR))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_REGULAR,
            (_GSSL("File: %s"), filename));
    }

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}    

/**
 * @brief Win32 - Directory Exists
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Check that the directory exists
 *
 * @param filename the file to check
 *
 * @return 
 *        GLOBUS_SUCCESS if the directory exists, otherwise an error
 *        object identifier.
 */
globus_result_t
globus_gsi_sysconfig_dir_exists_win32(
    const char *             filename)
{
    struct stat                         stx;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if (stat(filename,&stx) == -1)
    {
        switch(errno)
        {
          case ENOENT:
          case ENOTDIR:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_DOES_NOT_EXIST,
                (_GSSL("%s is not a valid directory"), filename));            
            goto exit;

          case EACCES:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_BAD_PERMISSIONS,
                (_GSSL("Could not read %s"), filename));            
            goto exit;

          default:
            result = globus_error_put(
                globus_error_wrap_errno_error(
                    GLOBUS_GSI_SYSCONFIG_MODULE,
                    errno,
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHECKING_FILE_EXISTS,
                    __FILE__,
                    __func__,
                    __LINE__,
                    "Error getting status of certificate directory: %s\n",
                    filename));
            goto exit;

        }
    }

    /*
     * use any stat output as random data, as it will 
     * have file sizes, and last use times in it. 
     */
    RAND_add((void*)&stx,sizeof(stx),2);

    /* != 0 size test will always fail in windows so it was removed */

    if(!(stx.st_mode & S_IFDIR))
    { 
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_DIR,
            (_GSSL("%s is not a directory"), filename));       
    }

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}    

/**
 * @brief Win32 - Check File Status for Key
 * @ingroup globus_i_gsi_sysconfig_win32
 * @details
 * This is a convenience function used to check the status of a 
 * private key file.  The desired status is only the current user has
 * ownership and read permissions, everyone else should not be able
 * to access it.
 * 
 * @param filename
 *        The name of the file to check the status of
 *
 * @return 
 *        GLOBUS_SUCCESS if the status of the file was able
 *        to be determined.  Otherwise, an error object
 *        identifier
 *
 */
globus_result_t
globus_gsi_sysconfig_check_keyfile_win32(
    const char *                        filename)
{
    return globus_gsi_sysconfig_check_keyfile_uid_win32(filename, -1);
}

/**
 * @brief Win32 - Check File Status for Key
 * @ingroup globus_i_gsi_sysconfig_win32
 * @details
 * This is a convenience function used to check the status of a 
 * private key file.  The desired status is only the current user has
 * ownership and read permissions, everyone else should not be able
 * to access it.
 * 
 * @param filename
 *        The name of the file to check the status of
 *
 * @return 
 *        GLOBUS_SUCCESS if the status of the file was able
 *        to be determined.  Otherwise, an error object
 *        identifier
 *
 */
globus_result_t
globus_gsi_sysconfig_check_keyfile_uid_win32(
    const char *                        filename,
    uid_t                               uid)
{
    struct stat                         stx;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if (stat(filename,&stx) == -1)
    {
        switch (errno)
        {
          case ENOENT:
          case ENOTDIR:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_DOES_NOT_EXIST,
                (_GSSL("%s is not a valid file"), filename));
            goto exit;

          case EACCES:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_BAD_PERMISSIONS,
                (_GSSL("Could not read %s"), filename));            
            goto exit;

          default:
            result = globus_error_put(
                globus_error_wrap_errno_error(
                    GLOBUS_GSI_SYSCONFIG_MODULE,
                    errno,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING,
                    __FILE__,
                    __func__,
                    __LINE__,
                    "Error getting status of file: %s\n",
                    filename));
            goto exit;
        }
    }

    /*
     * use any stat output as random data, as it will 
     * have file sizes, and last use times in it. 
     */
    RAND_add((void*)&stx,sizeof(stx),2);

    /*
     * Note that unix-like ownership and permissions are not supported by
     * Windows so geteuid() and rwx mode tests are not done. Maybe later Win32
     * file security using Access Control Lists can be incorporated, but this
     * is an architectural and would need to be considered and implemented in a
     * comprehensive way.
     */

    /* make sure size isn't zero */
    if (stx.st_size == 0)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_ZERO_LENGTH,
            (_GSSL("File: %s"), filename));            
        goto exit;
    }

    /* make sure its not a directory */
    if(stx.st_mode & S_IFDIR)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_IS_DIR,
            (_GSSL("File: %s"), filename));        
    }
    else if((stx.st_mode & S_IFMT)
            & ~(S_IFREG | S_IFDIR))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_REGULAR,
            (_GSSL("File: %s"), filename));
    }

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;

    return result;
}

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL

/**
 * @brief Win32 - Check File Status for Cert
 * @ingroup globus_i_gsi_sysconfig_win32
 * @details
 * This is a convenience function used to check the status of a 
 * certificate file.  The desired status is the current user has
 * ownership and read/write permissions, while group and others only
 * have read permissions.
 * 
 * @param filename
 *        The name of the file to check the status of
 *
 * @return 
 *        GLOBUS_SUCCESS if the status of the file was able
 *        to be determined.  Otherwise, an error object
 *        identifier
 *
 */
globus_result_t
globus_gsi_sysconfig_check_certfile_win32(
    const char *                        filename)
{
    return globus_gsi_sysconfig_check_certfile_uid_win32(filename, -1);
}

/**
 * @brief Win32 - Check File Status for Cert
 * @ingroup globus_i_gsi_sysconfig_win32
 * @details
 * This is a convenience function used to check the status of a 
 * certificate file.  The desired status is the current user has
 * ownership and read/write permissions, while group and others only
 * have read permissions.
 * 
 * @param filename
 *        The name of the file to check the status of
 *
 * @return 
 *        GLOBUS_SUCCESS if the status of the file was able
 *        to be determined.  Otherwise, an error object
 *        identifier
 *
 */
globus_result_t
globus_gsi_sysconfig_check_certfile_uid_win32(
    const char *                        filename,
    uid_t                               uid)
{
    struct stat                         stx;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if (stat(filename,&stx) == -1)
    {
        switch (errno)
        {
          case ENOENT:
          case ENOTDIR:
            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_DOES_NOT_EXIST,
                (_GSSL("%s is not a valid file"), filename));
            goto exit;

          case EACCES:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_BAD_PERMISSIONS,
                (_GSSL("Could not read %s"), filename));
            goto exit;

          default:
            result = globus_error_put(
                globus_error_wrap_errno_error(
                    GLOBUS_GSI_SYSCONFIG_MODULE,
                    errno,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_FILENAME,
                    __FILE__,
                    __func__,
                    __LINE__,
                    "Error getting status of file %s\n",
                    filename));
            goto exit;
        }
    }

    /*
     * use any stat output as random data, as it will 
     * have file sizes, and last use times in it. 
     */
    RAND_add((void*)&stx,sizeof(stx),2);

    /*
     * Note that unix-like ownership and permissions are not suppored by Windows so
     * geteuid() and rwx mode tests are not done. Maybe later Win32 file security 
     * using Access Control Lists can be incorporated, but this is an architectural
     * and would need to be considered and implemented in a comprehensive way.
     */

    /* make sure size isn't zero */
    if (stx.st_size == 0)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_ZERO_LENGTH,
            (_GSSL("File: %s"), filename));            
        goto exit;
    }

    if(stx.st_mode & S_IFDIR)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_IS_DIR,
            (_GSSL("File: %s"), filename));
    }
    else if((stx.st_mode & S_IFMT) &
            ~(S_IFREG | S_IFDIR))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_REGULAR,
            (_GSSL("File: %s"), filename));
    }

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}
#endif

/**
 * @brief Win32 - Get Current Working Directory
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Get the current working directory on a windows system
 *
 * @param working_dir
 *        The working directory to get
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object
 *        ID is returned
 */
globus_result_t
globus_gsi_sysconfig_get_current_working_dir_win32(
    char **                             working_dir)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char *                              buffer = NULL;
    char *                              result_buffer = NULL;
    int                                 length = 128;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    buffer = malloc(length);
    if(!buffer)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    while(1)
    {
        result_buffer = _getcwd(buffer, length);
        if(!result_buffer && errno == ERANGE)
        {
            length *= 2;
            if(!(result_buffer = realloc(buffer, length)))
            {
                free(buffer);
                result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                goto exit;
            }

            buffer = result_buffer;
        }
        else if(!result_buffer)
        {
            result = 
                globus_error_put(globus_error_wrap_errno_error(
                    GLOBUS_GSI_SYSCONFIG_MODULE,
                    errno,
                    GLOBUS_GSI_SYSCONFIG_ERROR_ERRNO,
                    __FILE__,
                    __func__,
                    __LINE__,
                    "Couldn't get the current working directory"));
        }
        else
        {
            break;
        }
    }

    *working_dir = result_buffer;

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief Win32 - Make Absolute Path
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Make the filename into an absolute path string based
 * on the current working directory.
 *
 * @param filename
 *        the filename to get the absolute path of.  
 * @param absolute_path
 *        The resulting absolute path
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise
 *        an error object ID is returned
 */
globus_result_t
globus_gsi_sysconfig_make_absolute_path_for_filename_win32(
    char *                              filename,
    char **                             absolute_path)
{
    int                                 length;
    char *                              cwd = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if(filename[0] != '\\' && !(filename[1] == ':' && filename[2] == '\\'))
    {
        result = GLOBUS_GSI_SYSCONFIG_GET_CURRENT_WORKING_DIR(&cwd);
        if(result != GLOBUS_SUCCESS)
        {
            cwd = NULL;
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CWD);
            goto exit;
        }

        length = strlen(cwd) + strlen(filename) + 2;

        *absolute_path = malloc(length);
        if(!*absolute_path)
        {
            result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }
        globus_libc_snprintf(*absolute_path, length, "%s\\%s", cwd, filename);
    }
    else
    {
        length = strlen(filename) + 1;

        *absolute_path = malloc(length);
        if(!*absolute_path)
        {
            result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }
        globus_libc_snprintf(*absolute_path, length, "%s", filename);
    }

 exit:

    if(cwd != NULL)
    {
        free(cwd);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief Win32 - Split Directory and Filename
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Split the directory and filename portions of a filename string
 * into two separate strings
 *
 * @param full_filename
 * @param dir_string
 * @param filename_string
 *
 * @return
 */
globus_result_t
globus_gsi_sysconfig_split_dir_and_filename_win32(
    char *                              full_filename,
    char **                             dir_string,
    char **                             filename_string)
{
    int                                 dir_string_length;
    int                                 filename_string_length;
    char *                              split_index = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *dir_string = NULL;
    *filename_string = NULL;

    split_index = strrchr(full_filename, '\\');
    if(!split_index)
    {
        *dir_string = NULL;
        filename_string_length = strlen(full_filename) + 1;
        *filename_string = malloc(filename_string_length);
        if(!*filename_string)
        {
            result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

        globus_libc_snprintf(*filename_string, filename_string_length, 
                             "%s", full_filename); 
    }
    else
    {
        dir_string_length = split_index - full_filename + 1;

        *dir_string = malloc(dir_string_length);

        if(!*dir_string)
        {
            result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

        globus_libc_snprintf(*dir_string,
                             dir_string_length, "%s", full_filename);

        filename_string_length = strlen(full_filename) - dir_string_length + 1;

        *filename_string = malloc(filename_string_length);

        if(!*filename_string)
        {
            result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            if(*dir_string)
            {
                free(*dir_string);
            }
            goto exit;
        }

        globus_libc_snprintf(*filename_string,
                             filename_string_length, "%s",
                             &full_filename[dir_string_length]);
    }

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief Win32 - Get User ID
 * @ingroup globus_i_gsi_sysconfig_win32
 * @details
 * Get a unique string representing the current user.  
 */
globus_result_t
globus_gsi_sysconfig_get_user_id_string_win32(
    char **                             user_id_string)
{
    int                                 uid;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    result = globus_gsi_sysconfig_get_username_win32(user_id_string);

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;

    return GLOBUS_SUCCESS;
}

/**
 * @brief Win32 - Get Username
 * @ingroup globus_i_gsi_sysconfig_win32
 * @details
 * Get the username of the current user.  
 */
globus_result_t
globus_gsi_sysconfig_get_username_win32(
    char **                             username)
{
    char *                              name;
    int                                 size;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if(name = getenv("USERNAME")) 
    {
        size = strlen(name) + 1;
        *username = malloc(size);
        if(*username) 
        {
            strncpy(*username,name,size);
        }
        else 
        {
            result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        }
    }

    /* getenv failed */
    else 
    {
        *username = NULL;
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
     	    result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_HOME_DIR,
            (_GSSL("Could not find username for this use")));
     }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;

    return result;
}

/**
 * @brief Win32 - Get Process ID
 * @ingroup globus_i_gsi_sysconfig_win32
 * @details
 * Get a unique string representing the current process.  
 */
globus_result_t
globus_gsi_sysconfig_get_proc_id_string_win32(
    char **                             proc_id_string)
{
    int                                 pid;
    int                                 len;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    pid = GetCurrentProcessId();

    len = globus_libc_printf_length("%d",pid);

    len++;

    if((*proc_id_string = malloc(len)) == NULL)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    globus_libc_snprintf(*proc_id_string,len,"%d",pid);

    result = GLOBUS_SUCCESS;

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief Win32 - Get Trusted CA Cert Dir
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Get the Trusted Certificate Directory containing the trusted
 * Certificate Authority certificates.  This directory is determined
 * in the order shown below.  Failure in one method results in attempting
 * the next.
 *
 * <ol>
 * <li> <b>X509_CERT_DIR environment variable</b> - if this is set, the
 * trusted certificates will be searched for in that directory.  This
 * variable allows the end user to specify the location of trusted
 * certificates.
 * <li> <b>x509_cert_dir registry key</b> - If
 * this registry key is set on windows, the directory it points to should
 * contain the trusted certificates.  The path to the registry key is
 * software\\Globus\\GSI
 * <li> <b>\\&lt;user home directory&gt;\\</b><b>.globus\\certificates</b> - If this
 * directory exists, and the previous methods of determining the trusted
 * certs directory failed, this directory will be used.  
 * <li> <b>Host Trusted Cert Dir</b> - This location is intended
 * to be independent of the globus installation ($GLOBUS_LOCATION), and 
 * is generally only writeable by the host system administrator.  
 * <li> <b>Globus Install Trusted Cert Dir</b> - this
 * is $GLOBUS_LOCATION\\share\\certificates.  
 * </ol>
 *
 * @param cert_dir
 *        The trusted certificates directory
 * @return
 *        GLOBUS_SUCCESS if no error occurred, and a sufficient trusted
 *        certificates directory was found.  Otherwise, an error object 
 *        identifier returned.
 */
globus_result_t
globus_gsi_sysconfig_get_cert_dir_win32(
    char **                             cert_dir)
{
    char *                              env_cert_dir = NULL;
    char *                              local_cert_dir = NULL;
    char *                              default_cert_dir = NULL;
    char *                              installed_cert_dir = NULL;
    globus_result_t                     result;
    globus_object_t *                   err = NULL;
    char *                              home = NULL;
    char *                              location;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *cert_dir = NULL;

    if(getenv(X509_CERT_DIR))
    {
        result = globus_i_gsi_sysconfig_create_cert_dir_string(
            cert_dir, 
            &env_cert_dir,
            "%s",
            getenv(X509_CERT_DIR));
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
            goto done;
        }
    }

    /* now check for a trusted CA directory in the user's home directory */
    if(!(*cert_dir))
    {
        result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);

        if(result == GLOBUS_SUCCESS)
        { 
            result = globus_i_gsi_sysconfig_create_cert_dir_string(
                cert_dir, 
                &local_cert_dir,
                "%s%s%s",
                home,
                FILE_SEPARATOR,
                X509_LOCAL_TRUSTED_CERT_DIR);
            if(result != GLOBUS_SUCCESS &&
               !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
                goto done;
            }
            else if (result != GLOBUS_SUCCESS)
            {
                err = globus_error_get(result);
                if (err)
                {
                    globus_object_free(err);
                }
            }
        }
        else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result) &&
                !GLOBUS_GSI_SYSCONFIG_FILE_HAS_BAD_PERMISSIONS(result))
        {
	    home = NULL;
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
            goto done;
        }
        else
        {
            err = globus_error_get(result);
            if (err)
            {
                globus_object_free(err);
            }
        }
    }

    /* now look in /etc/grid-security/certificates */
    if (!(*cert_dir))
    {
        result = globus_i_gsi_sysconfig_create_cert_dir_string(
            cert_dir,
            &installed_cert_dir,
            "%s",
            X509_DEFAULT_TRUSTED_CERT_DIR);
        if(result != GLOBUS_SUCCESS &&
           !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
            goto done;
        }
        else if (result != GLOBUS_SUCCESS)
        {
            err = globus_error_get(result);
            if (err)
            {
                globus_object_free(err);
            }
        }
    }

    /* now look in  $GLOBUS_LOCATION/share/certificates */
    if (!(*cert_dir))
    {
        globus_location(&location);

        if (location)
        {
            result = globus_i_gsi_sysconfig_create_cert_dir_string(
                cert_dir,
                &default_cert_dir,
                "%s%s%s",
                location,
                FILE_SEPARATOR,
                X509_INSTALLED_TRUSTED_CERT_DIR);
            if(result != GLOBUS_SUCCESS &&
               !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
                goto done;
            }
            else if (result != GLOBUS_SUCCESS)
            {
                err = globus_error_get(result);
                if (err)
                {
                    globus_object_free(err);
                }
            }
        }
    }

    if(!(*cert_dir))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR,
            (_GSSL("The trusted certificates directory could not be "
             "found in any of the following locations: \n"
             "1) env. var. X509_CERT_DIR\n"
             "2) $HOME/.globus/certificates\n"
             "3) /etc/grid-security/certificates"
             "\n4) $GLOBUS_LOCATION/share/certificates\n")));

        goto done;
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_FPRINTF(
        2, (stderr, "Using cert_dir = %s\n", 
            (*cert_dir ? *cert_dir : "null")));

    result = GLOBUS_SUCCESS;

 done:

    if(result != GLOBUS_SUCCESS)
    {
        *cert_dir = NULL;
    }

    if(home != NULL)
    {
	free(home);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;

    return result;
}

/**
 * @brief Win32 - Get User Certificate Filename
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Get the User Certificate Filename based on the current user's
 * environment.  The following locations are searched for cert and key
 * files in order:
 * 
 * <ol>
 * <li>environment variables X509_USER_CERT and X509_USER_KEY
 * <li>registry keys x509_user_cert and x509_user_key in software\\Globus\\GSI
 * <li><b>&lt;users home directory&gt;\\</b><b>.globus\\usercert.pem</b> and 
 *     <b>&lt;users home directory&gt;\\</b><b>.globus\\userkey.pem</b>
 * <li><b>&lt;users home directory&gt;\\</b><b>.globus\\usercred.p12</b> - this is a PKCS12 credential
 * </ol>
 *
 * @param user_cert
 *        pointer the filename of the user certificate
 * @param user_key
 *        pointer to the filename of the user key
 * @return
 *        GLOBUS_SUCCESS if the cert and key files were found in one
 *        of the possible locations, otherwise an error object identifier
 *        is returned
 */
globus_result_t
globus_gsi_sysconfig_get_user_cert_filename_win32(
    char **                             user_cert,
    char **                             user_key)
{
    char *                              home = NULL;
    char *                              env_user_cert = NULL;
    char *                              env_user_key = NULL;
    char *                              default_user_cert = NULL;
    char *                              default_user_key = NULL;
    char *                              default_pkcs12_user_cred = NULL;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;


    /* first, check environment variables for valid filenames */

    if(user_cert)
    {
        *user_cert = NULL;
        if(getenv(X509_USER_CERT))
        {
            result = globus_i_gsi_sysconfig_create_cert_string(
                user_cert,
                &env_user_cert,
                "%s",
                getenv(X509_USER_CERT));
            if(result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }            
        }

        if(!(*user_cert))
        {
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);
            if(result == GLOBUS_SUCCESS)
            {
                result = globus_i_gsi_sysconfig_create_cert_string(
                    user_cert,
                    &default_user_cert,
                    "%s%s%s",
                    home,
                    FILE_SEPARATOR,
                    X509_DEFAULT_USER_CERT);

                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                home = NULL;
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }
        }
    }

    if(user_key)
    { 
        *user_key = NULL;
        result = GLOBUS_SUCCESS;

        if(getenv(X509_USER_KEY))
        {
            result = globus_i_gsi_sysconfig_create_key_string(
                user_key,
                &env_user_key,
                "%s",
                getenv(X509_USER_KEY));
            if(result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                goto done;
            }
        }

        if(!(*user_key))
        {
            if(!home)
            {
                result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);
            }

            if(result == GLOBUS_SUCCESS)
            {
                result = globus_i_gsi_sysconfig_create_key_string(
                    user_key,
                    &default_user_key,
                    "%s%s%s",
                    home,
                    FILE_SEPARATOR,
                    X509_DEFAULT_USER_KEY);
                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                home = NULL;
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }
        }
    }

    /* if the cert & key don't exist in the default locations
     * or those specified by the environment variables, a
     * pkcs12 cert will be searched for
     */
    if(user_cert && user_key && !(*user_cert) && !(*user_key))
    {
        result = GLOBUS_SUCCESS;
        if(!home)
        { 
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);
        }

        if(result == GLOBUS_SUCCESS)
        {
            result = globus_i_gsi_sysconfig_create_key_string(
                user_key,
                &default_pkcs12_user_cred,
                "%s%s%s",
                home,
                FILE_SEPARATOR,
                X509_DEFAULT_PKCS12_FILE);
            if(result != GLOBUS_SUCCESS &&
               !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                goto done;
            }
            *user_cert = globus_libc_strdup(*user_key);
        }
        else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
        {
            home = NULL;
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
            goto done;            
        }
    }

    if(user_cert && !(*user_cert))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING,
            (_GSSL("The user cert could not be found in: \n"
             "1) env. var. X509_USER_CERT\n"
             "2) $HOME/.globus/usercert.pem\n"
             "3) $HOME/.globus/usercred.p12\n\n")));
        goto done;
    }

    if(user_key && !(*user_key))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING,
            (_GSSL("The user key could not be found in:\n,"
             "1) env. var. X509_USER_KEY\n"
             "2) $HOME/.globus/userkey.pem\n"
             "3) $HOME/.globus/usercred.p12\n\n")));
        goto done;
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_FPRINTF(
        2, (stderr,"Using x509_user_cert=%s\n      x509_user_key =%s\n",
            user_cert ? *user_cert : "NULL",
            user_key ? *user_key : "NULL"));

    result = GLOBUS_SUCCESS;

 done:
    if(result != GLOBUS_SUCCESS && user_cert)
    {
        *user_cert = NULL;
    }

    if(home)
    {
        free(home);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief Win32 - Get Host Certificate and Key Filenames
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Get the Host Certificate and Key Filenames based on the current user's
 * environment.  The host cert and key are searched for in the following 
 * locations (in order):
 *
 * <ol>
 * <li>X509_USER_CERT and X509_USER_KEY environment variables
 * <li>registry keys x509_user_cert and x509_user_key in software\\Globus\\GSI
 * <li><b>&lt;GLOBUS_LOCATION&gt;\\etc\\host[cert|key].pem</b>
 * <li><b>&lt;users home directory&gt;\\</b><b>.globus\\host[cert|key].pem</b>
 * </ol>
 * 
 * @param host_cert
 *        pointer to the host certificate filename
 * @param host_key
 *        pointer to the host key filename
 *
 * @return
 *        GLOBUS_SUCCESS if the host cert and key were found, otherwise
 *        an error object identifier is returned 
 */
globus_result_t
globus_gsi_sysconfig_get_host_cert_filename_win32(
    char **                             host_cert,
    char **                             host_key)
{
    char *                              home = NULL;
    char *                              env_host_cert = NULL;
    char *                              env_host_key = NULL;
    char *                              default_host_cert = NULL;
    char *                              default_host_key = NULL;
    char *                              installed_host_cert = NULL;
    char *                              installed_host_key = NULL;
    char *                              local_host_cert = NULL;
    char *                              local_host_key = NULL;
    char *                              location = NULL;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *host_cert = NULL;
    *host_key = NULL;

    /* first check environment variables for valid filenames */

    if(getenv(X509_USER_CERT) && getenv(X509_USER_KEY))
    {
        result = globus_i_gsi_sysconfig_create_cert_string(
            host_cert,
            &env_host_cert,
            "%s",
            getenv(X509_USER_CERT));
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
            goto done;
        }

        result = globus_i_gsi_sysconfig_create_key_string(
            host_key,
            &env_host_key,
            "%s",
            getenv(X509_USER_KEY));
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
            goto done;
        }
    }

    /* now check default locations for valid filenames */
    if(!(*host_cert) && !(*host_key))
    {
        result = globus_i_gsi_sysconfig_create_cert_string(
            host_cert,
            & default_host_cert,
            "%s%s%s%s",
            X509_DEFAULT_CERT_DIR,
            FILE_SEPARATOR,
            X509_HOST_PREFIX,
            X509_CERT_SUFFIX);

        if(result == GLOBUS_SUCCESS)
        { 
            result = globus_i_gsi_sysconfig_create_key_string(
                host_key,
                & default_host_key,
                "%s%s%s%s",
                X509_DEFAULT_CERT_DIR,
                FILE_SEPARATOR,
                X509_HOST_PREFIX,
                X509_KEY_SUFFIX);
            if(result != GLOBUS_SUCCESS &&
               !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                goto done;
            }
        }
        else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
            goto done;
        }
    }

    /* now check installed location for host cert */
    if(!(*host_cert) && !(*host_key)) 
    {
        result = globus_location(&location);

        if(location)
        {
            result = globus_i_gsi_sysconfig_create_cert_string(
                host_cert,
                &installed_host_cert,
                "%s%s%s%s%s%s",
                location,
                FILE_SEPARATOR,
                X509_INSTALLED_CERT_DIR,
                FILE_SEPARATOR,
                X509_HOST_PREFIX,
                X509_CERT_SUFFIX);

            if(result == GLOBUS_SUCCESS)
            { 
                result = globus_i_gsi_sysconfig_create_key_string(
                    host_key,
                    &installed_host_key,
                    "%s%s%s%s%s%s",
                    location,
                    FILE_SEPARATOR,
                    X509_INSTALLED_CERT_DIR,
                    FILE_SEPARATOR,
                    X509_HOST_PREFIX,
                    X509_KEY_SUFFIX);
                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }
        }
    }

    if(!(*host_cert) && !(*host_key)) 
    {
        result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);

        if(result == GLOBUS_SUCCESS)
        {
            result = globus_i_gsi_sysconfig_create_cert_string(
                host_cert,
                &local_host_cert,
                "%s%s%s%s%s%s",
                home,
                FILE_SEPARATOR,
                X509_LOCAL_CERT_DIR,
                FILE_SEPARATOR,
                X509_HOST_PREFIX,
                X509_CERT_SUFFIX);

            if(result == GLOBUS_SUCCESS)
            { 
                result = globus_i_gsi_sysconfig_create_key_string(
                    host_key,
                    & local_host_key,
                    "%s%s%s%s%s%s",
                    home,
                    FILE_SEPARATOR,
                    X509_LOCAL_CERT_DIR,
                    FILE_SEPARATOR,
                    X509_HOST_PREFIX,
                    X509_KEY_SUFFIX);
                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }
        }
        else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
            home = NULL;
            goto done;
        }
    }

    if(!(*host_cert) || !(*host_key))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_FILENAME,
            (_GSSL("The host cert could not be found in: \n"
             "1) env. var. X509_USER_CERT\n"
             "2) /etc/grid-security/hostcert.pem\n"
             "3) $GLOBUS_LOCATION/etc/hostcert.pem\n"
             "4) $HOME/.globus/hostcert.pem\n\n"
             "The host key could not be found in:\n"
             "1) env. var. X509_USER_KEY\n"
             "2) /etc/grid-security/hostkey.pem\n"
             "3) $GLOBUS_LOCATION/etc/hostkey.pem\n"
             "4) $HOME/.globus/hostkey.pem\n\n")));
        goto done;
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_FPRINTF(
        2, (stderr, "Using x509_user_cert=%s\n      x509_user_key =%s\n",
            *host_cert , *host_key));

    result = GLOBUS_SUCCESS;

 done:
    if(result != GLOBUS_SUCCESS)
    {
        *host_cert = NULL;
        *host_key = NULL;
    }

    if(home)
    {
        free(home);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief Win32 - Get Service Certificate and Key Filenames
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Get the Service Certificate Filename based on the current user's
 * environment.  The host cert and key are searched for in the following 
 * locations (in order):
 *
 * <ol>
 * <li>X509_USER_CERT and X509_USER_KEY environment variables
 * <li>registry keys x509_user_cert and x509_user_key in software\\Globus\\GSI
 * <li>GLOBUS_LOCATION\\etc\\{service_name}\\{service_name}[cert|key].pem
 *     So for example, if my service was named: myservice, the location
 *     of the certificate would be: 
 *     <b>&lt;GLOBUS_LOCATION&gt;\\etc\\myservice\\myservicecert.pem</b>
 * <li><b>&lt;users home&gt;\\</b><b>.globus\\{service_name}\\{service_name}[cert|key].pem</b>
 * </ol>
 * 
 * @param service_name
 *        The name of the service which allows us to determine the
 *        locations of cert and key files to look for
 * @param service_cert
 *        pointer to the host certificate filename
 * @param service_key
 *        pointer to the host key filename
 *
 * @return
 *        GLOBUS_SUCCESS if the service cert and key were found, otherwise
 *        an error object identifier 
 */
globus_result_t
globus_gsi_sysconfig_get_service_cert_filename_win32(
    char *                              service_name,
    char **                             service_cert,
    char **                             service_key)
{
    char *                              home = NULL;
    char *                              env_service_cert = NULL;
    char *                              env_service_key = NULL;
    char *                              default_service_cert = NULL;
    char *                              default_service_key = NULL;
    char *                              installed_service_cert = NULL;
    char *                              installed_service_key = NULL;
    char *                              local_service_cert = NULL;
    char *                              local_service_key = NULL;
    char *                              location = NULL;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *service_cert = NULL;
    *service_key = NULL;

    if(service_name == NULL)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_INVALID_ARG,
            (_GSSL("Empty service name")));
        goto done;
    }

    /* first check environment variables for valid filenames */

    if(getenv(X509_USER_CERT) && getenv(X509_USER_KEY))
    {
        result = globus_i_gsi_sysconfig_create_cert_string(
            service_cert,
            &env_service_cert,
            "%s",
            getenv(X509_USER_CERT));

        if(result != GLOBUS_SUCCESS)
        { 
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
            goto done;
        }

        result = globus_i_gsi_sysconfig_create_key_string(
            service_key,
            &env_service_key,
            "%s",
            getenv(X509_USER_KEY));

        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
            goto done;
        }
    }

    /* now check default locations for valid filenames */
    if(!(*service_cert) && !(*service_key))
    {
        result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);

        if(result == GLOBUS_SUCCESS)
        {
            result = globus_i_gsi_sysconfig_create_cert_string(
                service_cert,
                &default_service_cert,
                "%s%s%s%s%s%s",
                X509_DEFAULT_CERT_DIR,
                FILE_SEPARATOR,
                service_name,
                FILE_SEPARATOR,
                service_name,
                X509_CERT_SUFFIX);

            if(result == GLOBUS_SUCCESS)
            { 
                result = globus_i_gsi_sysconfig_create_key_string(
                    service_key,
                    & default_service_key,
                    "%s%s%s%s%s%s",
                    X509_DEFAULT_CERT_DIR,
                    FILE_SEPARATOR,
                    service_name,
                    FILE_SEPARATOR,
                    service_name,
                    X509_KEY_SUFFIX);

                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;                
            }
        }
        else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
            home = NULL;
            goto done;
        }
    }

    /* now check intstalled location for service cert */
    if(!(*service_cert) && !(*service_key))
    {
        globus_location(&location);

        if(location)
        {
            result = globus_i_gsi_sysconfig_create_cert_string(
                service_cert,
                &installed_service_cert,
                "%s%s%s%s%s%s%s%s",
                location,
                FILE_SEPARATOR,
                X509_INSTALLED_CERT_DIR,
                FILE_SEPARATOR,
                service_name,
                FILE_SEPARATOR,
                service_name,
                X509_CERT_SUFFIX);

            if(result == GLOBUS_SUCCESS)
            { 
                result = globus_i_gsi_sysconfig_create_key_string(
                    service_key,
                    &installed_service_key,
                    "%s%s%s%s%s%s%s%s",
                    location,
                    FILE_SEPARATOR,
                    X509_INSTALLED_CERT_DIR,
                    FILE_SEPARATOR,
                    service_name,
                    FILE_SEPARATOR,
                    service_name,
                    X509_KEY_SUFFIX);
                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }
        }
    }

    if(!(*service_cert) && !(*service_key))
    {
        result = GLOBUS_SUCCESS;
        if(!home)
        {
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);
        }

        if(result == GLOBUS_SUCCESS)
        {
            result = globus_i_gsi_sysconfig_create_cert_string(
                service_cert,
                &local_service_cert,
                "%s%s%s%s%s%s%s",
                home,
                FILE_SEPARATOR,
                X509_LOCAL_CERT_DIR,
                FILE_SEPARATOR,
                service_name,
                FILE_SEPARATOR,
                service_name,
                X509_CERT_SUFFIX);

            if(result == GLOBUS_SUCCESS)
            { 
                result = globus_i_gsi_sysconfig_create_key_string(
                    service_key,
                    &local_service_key,
                    "%s%s%s%s%s%s%s%s",
                    home,
                    FILE_SEPARATOR,
                    X509_LOCAL_CERT_DIR,
                    FILE_SEPARATOR,
                    service_name,
                    FILE_SEPARATOR,
                    service_name,
                    X509_KEY_SUFFIX);
                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }
        }
        else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
            home = NULL;
            goto done;            
        }
    }

    if(!(*service_cert) || !(*service_key))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_FILENAME,
            (_GSSL("\nThe service cert could not be found in: \n"
             "1) env. var. X509_USER_CERT\n"
             "2) /etc/grid-security/%s/%scert.pem\n"
             "3) $GLOBUS_LOCATION/etc/%s/%scert.pem\n"
             "4) $HOME/.globus/%s/%scert.pem\n\n"
             "The service key could not be found in:\n"
             "1) env. var. X509_USER_KEY\n"
             "2) /etc/grid-security/%s/%skey.pem\n"
             "3) $GLOBUS_LOCATION/etc/%s/%skey.pem\n"
             "4) $HOME/.globus/%s/%skey.pem\n\n"),
             service_name, service_name,
             service_name, service_name,
             service_name, service_name,
             service_name, service_name,
             service_name, service_name,
             service_name, service_name));
        goto done;
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_FPRINTF(
        2, (stderr, "Using x509_user_cert=%s\n      x509_user_key =%s\n",
            *service_cert , *service_key));

    result = GLOBUS_SUCCESS;

 done:
    if(result != GLOBUS_SUCCESS)
    {
        *service_cert = NULL;
        *service_key = NULL;
    }

    if(home)
    {
        free(home);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief Win32 - Get Proxy Filename
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Get the proxy cert filename based on the following
 * search order:
 * 
 * <ol>
 * <li> X509_USER_PROXY environment variable - This environment variable
 * is set by the at run time for the specific application.  If
 * the proxy_file_type variable is set to GLOBUS_PROXY_OUTPUT
 *  (a proxy filename for writing is requested), 
 * and the X509_USER_PROXY is set, this will be the 
 * resulting value of the user_proxy filename string passed in.  If the
 * proxy_file_type is set to GLOBUS_PROXY_INPUT and X509_USER_PROXY is 
 * set, but the file it points to does not exist, 
 * or has some other readability issues, the 
 * function will continue checking using the other methods available.
 * 
 * <li> check the registry key: x509_user_proxy.  Just as with
 * the environment variable, if the registry key is set, and proxy_file_type
 * is GLOBUS_PROXY_OUTPUT, the string set to be the proxy 
 * filename will be this registry
 * key's value.  If proxy_file_type is GLOBUS_PROXY_INPUT, and the 
 * file doesn't exist, the function will check the next method 
 * for the proxy's filename.
 * 
 * <li> Check the default location for the proxy file.  The default
 * location should be 
 * set to reside in the temp directory on that host, with the filename
 * taking the format:  x509_u&lt;user id&gt;
 * where &lt;user id&gt; is some unique string for that user on the host
 * </ol>
 *
 * @param user_proxy
 *        the proxy filename of the user
 * @param proxy_file_type
 *        Switch for determining whether to return a existing proxy filename or
 *        if a filename suitable for creating a proxy should be returned
 *
 * @return
 *        GLOBUS_SUCCESS or an error object identifier
 */
globus_result_t
globus_gsi_sysconfig_get_proxy_filename_win32(
    char **                             user_proxy,
    globus_gsi_proxy_file_type_t        proxy_file_type)
{
    char *                              env_user_proxy = NULL;
    char *                              env_value = NULL;
    char *                              default_user_proxy = NULL;
    globus_result_t                     result;
    char *                              user_id_string = NULL;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *user_proxy = NULL;

    if((env_value = getenv(X509_USER_PROXY)))
    {
        if(proxy_file_type == GLOBUS_PROXY_FILE_OUTPUT)
        {
            *user_proxy = strdup(env_value);
            if(*user_proxy == NULL)
            {
                result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                goto done;
            }
        }
        else
        { 
            result = globus_i_gsi_sysconfig_create_key_string(
                user_proxy,
                &env_user_proxy,
                "%s",
                env_value);
            if(result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PROXY_FILENAME);
                goto done;
            }
        }
    }

    if (!*user_proxy)
    {
        result = GLOBUS_GSI_SYSCONFIG_GET_USER_ID_STRING(&user_id_string);
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PROXY_FILENAME);
            goto done;
        }

        if(proxy_file_type == GLOBUS_PROXY_FILE_OUTPUT)
        {
            *user_proxy = globus_common_create_string(
                "%s%s%s%s",
                DEFAULT_SECURE_TMP_DIR,
                FILE_SEPARATOR,
                X509_USER_PROXY_FILE,
                user_id_string);
            if(*user_proxy == NULL)
            {
                result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                goto done;
            }
        }
        else
        {
            result = globus_i_gsi_sysconfig_create_key_string(
                user_proxy,
                &default_user_proxy,
                "%s%s%s%s",
                DEFAULT_SECURE_TMP_DIR,
                FILE_SEPARATOR,
                X509_USER_PROXY_FILE,
                user_id_string);

            if(result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PROXY_FILENAME);
                goto done;
            }
        }
    }

    if(!(*user_proxy))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT( 
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PROXY_FILENAME,
            (_GSSL("A file location for%s the proxy cert could not be found in: \n"
             "1) env. var. X509_USER_PROXY\n"
             "2) /tmp/x509up_u%s\n"),
             (proxy_file_type == GLOBUS_PROXY_FILE_INPUT) ? "" : " writing",
             user_id_string ? user_id_string : "NULL"));

        goto done;
    }

    result = GLOBUS_SUCCESS;

 done:
    if(result != GLOBUS_SUCCESS)
    {
        *user_proxy = NULL;
    }

    if(user_id_string)
    {
        free(user_id_string);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief Win32 - Get CA Cert Filenames
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Gets a list of trusted CA certificate filenames in 
 * a trusted CA certificate directory.  
 *
 * @param ca_cert_dir
 *        The trusted CA certificate directory to get the filenames from
 * @param ca_cert_list
 *        The resulting list of CA certificate filenames.  This is
 *        a globus list structure.  
 *        @see globus_fifo_t
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */
globus_result_t
globus_gsi_sysconfig_get_ca_cert_files_win32(
    char *                              ca_cert_dir,
    globus_fifo_t *                     ca_cert_list)
{
    int                                 file_length;
    char *                              full_filename_path = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;
    HANDLE                              file_search_handle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATA                     file_data;
    char                                file_search_string[MAX_PATH];

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if(!ca_cert_dir)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CA_CERT_FILENAMES,
            (_GSSL("NULL parameter ca_cert_dir passed to function: %s"),
             __func__));
        goto exit;
    }

    if(!ca_cert_list)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CA_CERT_FILENAMES,
            (_GSSL("NULL parameter ca_cert_list passed to function: %s"),
             __func__));
        goto exit;
    }

    /* build a wild card search string */    
    sprintf(file_search_string,"%s\\*.*",ca_cert_dir);

    /* search the directory for the first file */
    file_search_handle = FindFirstFile(file_search_string,(LPVOID) &file_data);
    if(file_search_handle == INVALID_HANDLE_VALUE)
    {
        result = globus_error_put(
            globus_error_wrap_errno_error(
                GLOBUS_GSI_SYSCONFIG_MODULE,
                errno,
                GLOBUS_GSI_SYSCONFIG_ERROR_ERRNO,
                __FILE__,
                __func__,
                __LINE__,
                "Error opening directory: %s", ca_cert_dir));
        goto exit;
    }

    /* collect all the files in the directory (first one's already there) */
    do
    {
        file_length = strlen(file_data.cFileName);
        /* check the following:
         * 
         * - file length is greater than or equal to 10
         * - first 8 characters are alpha-numeric
         * - 9th character is '.'
         * - characters after the '.' are numeric
         */

        full_filename_path = 
            globus_common_create_string(
                "%s%s%s", ca_cert_dir, FILE_SEPARATOR, file_data.cFileName);

        if(full_filename_path == NULL)
        {
            while((full_filename_path =
                   (char *) globus_fifo_dequeue(ca_cert_list)) != NULL)
            {
                free(full_filename_path);
            }
            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CA_CERT_FILENAMES,
                (_GSSL("Couldn't get full pathname for CA cert")));
            goto exit;
        }

        if((result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
                full_filename_path)) == GLOBUS_SUCCESS)
        {

            if(file_length >= (X509_HASH_LENGTH + 2) &&
               (*(file_data.cFileName + X509_HASH_LENGTH) == '.') &&
               (strspn(file_data.cFileName, "0123456789abcdefABCDEF") 
                == X509_HASH_LENGTH) &&
               (strspn((file_data.cFileName + (X509_HASH_LENGTH + 1)), 
                       "0123456789") == (file_length - 9)))
            {
                globus_fifo_enqueue(ca_cert_list, (void *)full_filename_path);
            }
            else
            {
                free(full_filename_path);
            }
        }
        else
        {
            free(full_filename_path);
        }

    } while(FindNextFile(file_search_handle,&file_data));

    result = GLOBUS_SUCCESS;

 exit:

    if(file_search_handle != INVALID_HANDLE_VALUE)
    {
    FindClose(file_search_handle);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;

    return result;
}

/**
 * @brief Win32 - Remove all proxies owned by current uid
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Removes all proxies (all delegated and grid-proxy-init generated
 * proxies) found in the secure temp directory that are owned by the
 * current user.
 *
 * @param default_filename
 *        The filename of the default proxy
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */
globus_result_t
globus_gsi_sysconfig_remove_all_owned_files_win32(
    char *                              default_filename)
{
    struct _stat                         stx;
    char *                              full_filename = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;
    HANDLE                              search_handle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATA                     file_data;
    char                                search_string[MAX_PATH];
    static char                         msg[65] = "DESTROYED BY GLOBUS\r\n";
    int                                 f;
    int                                 size, rec, left;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    /* build a wild card search string */    
    sprintf(search_string,"%s\\*.*",DEFAULT_SECURE_TMP_DIR);

    /* search the directory for the first file */
    search_handle = FindFirstFile(search_string,(LPVOID) &file_data);
    if(search_handle == INVALID_HANDLE_VALUE)
    {
        result = globus_error_put(
            globus_error_wrap_errno_error(
                GLOBUS_GSI_SYSCONFIG_MODULE,
                errno,
                GLOBUS_GSI_SYSCONFIG_ERROR_ERRNO,
                __FILE__,
                __func__,
                __LINE__,
                "Error opening directory: %s", DEFAULT_SECURE_TMP_DIR));
        goto exit;
    }

    /* go through all the files in the directory (first one's already there) */
    do
    {
        if((default_filename && 
            !strcmp(file_data.cFileName, default_filename)) ||
           !strncmp(file_data.cFileName,
                    X509_UNIQUE_PROXY_FILE,
                    strlen(X509_UNIQUE_PROXY_FILE)))
        {
            full_filename = globus_common_create_string(
                "%s%s%s",
                DEFAULT_SECURE_TMP_DIR,
                FILE_SEPARATOR,
                file_data.cFileName);

            if(_stat(full_filename, &stx) == -1)
            {
                continue;
            }

            RAND_add((void *) &stx, sizeof(stx), 2);

            f = _open(full_filename, O_RDWR);
            if (f) 
            {
                size = lseek(f, 0L, SEEK_END);
                lseek(f, 0L, SEEK_SET);
                if (size > 0) 
                {
                    rec = size / 64;
                    left = size - rec * 64;
                    while (rec)
                    {
                        write(f, msg, 64);
                        rec--;
                    }
                    if (left)
                    { 
                        write(f, msg, left);
                    }
                }
                close(f);
            }

            DeleteFile(full_filename);

            free(full_filename);
        }

    } while(FindNextFile(search_handle,&file_data));

 exit:

    if(search_handle != INVALID_HANDLE_VALUE)
    {
    FindClose(search_handle);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief Win32 - Get the path and file name of the grid map file
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Get the path and file name of the grid map file.
 *
 * @param filename
 *        Contains the location of the grid map file upon successful return
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */
globus_result_t
globus_gsi_sysconfig_get_gridmap_filename_win32(
    char **                             filename)
{
    int                                 super_user;
    char *                              home_dir = NULL;
    char *                              gridmap_env = NULL;
    char *                              gridmap_filename = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if(((gridmap_env = (char *) getenv("GRIDMAP"))   != NULL) ||
       ((gridmap_env = (char *) getenv("GLOBUSMAP")) != NULL) ||
       ((gridmap_env = (char *) getenv("globusmap")) != NULL) ||
       ((gridmap_env = (char *) getenv("GlobusMap")) != NULL))
    {
        gridmap_filename = globus_common_create_string(
            "%s",
            gridmap_env);
        if(!gridmap_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }
    }

    if(!gridmap_filename)
    {
        globus_gsi_sysconfig_is_superuser_win32(&super_user);
        if(super_user)
        {
            /* being run as root */

            gridmap_filename = globus_common_create_string(
                "%s",
                DEFAULT_GRIDMAP);
            if(!gridmap_filename)
            {
                GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                goto exit;
            }
        }
        else
        {
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home_dir);

            if(result == GLOBUS_SUCCESS)
            {
                gridmap_filename = globus_common_create_string(
                    "%s%s%s",
                    home_dir,
                    FILE_SEPARATOR,
                    LOCAL_GRIDMAP);
                if(!gridmap_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GRIDMAP_FILENAME);
                goto exit;                
            }
        }
    }

    if(!gridmap_filename)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GRIDMAP_FILENAME,
            (_GSSL("A valid gridmap file could not be found.")));
        goto exit;
    }

    *filename = gridmap_filename;

 exit:

    if(home_dir != NULL)
    {
        free(home_dir);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief Win32 - Get the path and file name of the grid map file
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Get the path and file name of the authorization callback
 * configuration file 
 *
 * @param filename
 *        Contains the location of the authorization callback configuration
 *        file upon successful return
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */
globus_result_t
globus_gsi_sysconfig_get_authz_conf_filename_win32(
    char **                             filename)
{
    char *                              home_dir = NULL;
    char *                              authz_env = NULL;
    char *                              authz_filename = NULL;
    char *                              location = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if((authz_env = (char *) getenv("GSI_AUTHZ_CONF"))   != NULL)
    {
        authz_filename = globus_common_create_string(
            "%s",
            authz_env);
        if(!authz_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

        result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
            authz_filename);

        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_FILENAME);
            goto exit;
        }
    }
    else
    { 
        authz_filename = globus_common_create_string(
            "%s",
            DEFAULT_AUTHZ_FILE);
        if(!authz_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

        result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
            authz_filename);

        if(result != GLOBUS_SUCCESS)
        {
            if(GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                free(authz_filename);
                authz_filename = NULL;
            }
            else
            { 
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_FILENAME);
                goto exit;
            }
        }

        if(authz_filename == NULL)
        {
            globus_location(&location);

            if(location)
            {
                authz_filename = globus_common_create_string(
                    "%s%s%s",
                    location,
                    FILE_SEPARATOR,
                    INSTALLED_AUTHZ_FILE);
                if(!authz_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }

                result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
                    authz_filename);

                if(result != GLOBUS_SUCCESS)
                {
                    if(GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                    {
                        free(authz_filename);
                        authz_filename = NULL;
                    }
                    else
                    { 
                        GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                            result,
                            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_FILENAME);
                        goto exit;
                    }
                }
            }
        }

        if(authz_filename == NULL)
        {
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home_dir);

            if(result == GLOBUS_SUCCESS)
            {
                authz_filename = globus_common_create_string(
                    "%s%s%s",
                    home_dir,
                    FILE_SEPARATOR,
                    LOCAL_AUTHZ_FILE);
                if(!authz_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }
                result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
                    authz_filename);

                if(result != GLOBUS_SUCCESS)
                {
                    free(authz_filename);
                    authz_filename = NULL;

                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_FILENAME);
                    goto exit;
                }

            }
            else
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_FILENAME);
                goto exit;
            }
        }
    }

    *filename = authz_filename;
    authz_filename = NULL;

 exit:

    if(home_dir != NULL)
    {
        free(home_dir);
    }

    if(authz_filename != NULL)
    {
        free(authz_filename);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief Win32 - Get authorization configuration filename
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Get the path and file name of the authorization callback library
 * configuration file 
 *
 * @param filename
 *        Contains the location of the authorization callback library
 *        configuration file upon successful return
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */
globus_result_t
globus_gsi_sysconfig_get_authz_lib_conf_filename_win32(
    char **                             filename)
{
    char *                              home_dir = NULL;
    char *                              authz_lib_env = NULL;
    char *                              authz_lib_filename = NULL;
    char *                              location = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if((authz_lib_env = (char *) getenv("GSI_AUTHZ_LIB_CONF"))   != NULL)
    {
        authz_lib_filename = globus_common_create_string(
            "%s",
            authz_lib_env);
        if(!authz_lib_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

	result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
            authz_lib_filename);

        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_LIB_FILENAME);
            goto exit;
        }
    }
    else
    { 
        authz_lib_filename = globus_common_create_string(
            "%s%s_%s%s",
	    DEFAULT_AUTHZ_LIB_FILE_DIR,
	    DEFAULT_AUTHZ_LIB_FILE_BASE,
	    flavor,
            DEFAULT_AUTHZ_LIB_FILE_EXTENSION);
        if(!authz_lib_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

	result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
            authz_lib_filename);

        if(result != GLOBUS_SUCCESS)
        {
            if(GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                free(authz_lib_filename);
                authz_lib_filename = NULL;
            }
            else
            { 
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_LIB_FILENAME);
                goto exit;
            }
        }

        if(authz_lib_filename == NULL)
        {
            globus_location(&location);

            if(location)
            {
                authz_lib_filename = globus_common_create_string(
                    "%s%s%s%s_%s%s",
                    location,
                    FILE_SEPARATOR,
		    INSTALLED_AUTHZ_LIB_DIR,
		    DEFAULT_AUTHZ_LIB_FILE_BASE,
		    flavor,
		    DEFAULT_AUTHZ_LIB_FILE_EXTENSION);
                if(!authz_lib_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }

		result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
                    authz_lib_filename);

                if(result != GLOBUS_SUCCESS)
                {
                    if(GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                    {
                        free(authz_lib_filename);
                        authz_lib_filename = NULL;
                    }
                    else
                    { 
                        GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                            result,
                            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_LIB_FILENAME);
                        goto exit;
                    }
                }
            }
        }

        if(authz_lib_filename == NULL)
        {
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home_dir);

            if(result == GLOBUS_SUCCESS)
            {
                authz_lib_filename = globus_common_create_string(
                    "%s%s%s_%s%s",
                    home_dir,
                    FILE_SEPARATOR,
                    HOME_AUTHZ_LIB_FILE_BASE,
		    flavor,
		    DEFAULT_AUTHZ_LIB_FILE_EXTENSION);
                if(!authz_lib_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }
                result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
                    authz_lib_filename);

                if(result != GLOBUS_SUCCESS)
                {
                    free(authz_lib_filename);
                    authz_lib_filename = NULL;

                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_LIB_FILENAME);
                    goto exit;
                }

            }
            else
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_LIB_FILENAME);
                goto exit;
            }
        }
    }

    *filename = authz_lib_filename;
    authz_lib_filename = NULL;

 exit:

    if(home_dir != NULL)
    {
        free(home_dir);
    }

    if(authz_lib_filename != NULL)
    {
        free(authz_lib_filename);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;

}

/**
 * @brief Win32 - Get the path and file name of the gaa config file
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Get the path and file name of the gaa config configuration file .
 *
 * @param filename
 *        Contains the location of the authorization callback configuration
 *        file upon successful return
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */
globus_result_t
globus_gsi_sysconfig_get_gaa_conf_filename_win32(
    char **                             filename)
{
    char *                              home_dir = NULL;
    char *                              gaa_env = NULL;
    char *                              gaa_filename = NULL;
    char *                              location = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if((gaa_env = (char *) getenv("GSI_GAA_CONF"))   != NULL)
    {
        gaa_filename = globus_common_create_string(
            "%s",
            gaa_env);
        if(!gaa_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

        result = GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE(
            gaa_filename);

        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GAA_FILENAME);
            goto exit;
        }
    }
    else
    { 
        gaa_filename = globus_common_create_string(
            "%s",
            DEFAULT_GAA_FILE);
        if(!gaa_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

        result = GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE(
            gaa_filename);

        if(result != GLOBUS_SUCCESS)
        {
            if(GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                free(gaa_filename);
                gaa_filename = NULL;
            }
            else
            { 
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GAA_FILENAME);
                goto exit;
            }
        }

        if(gaa_filename == NULL)
        {
            globus_location(&location);

            if(location)
            {
                gaa_filename = globus_common_create_string(
                    "%s%s%s",
                    location,
                    FILE_SEPARATOR,
                    INSTALLED_GAA_FILE);
                if(!gaa_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }

                result = GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE(
                    gaa_filename);

                if(result != GLOBUS_SUCCESS)
                {
                    if(GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                    {
                        free(gaa_filename);
                        gaa_filename = NULL;
                    }
                    else
                    { 
                        GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                            result,
                            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GAA_FILENAME);
                        goto exit;
                    }
                }
            }
        }

        if(gaa_filename == NULL)
        {
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home_dir);

            if(result == GLOBUS_SUCCESS)
            {
                gaa_filename = globus_common_create_string(
                    "%s%s%s",
                    home_dir,
                    FILE_SEPARATOR,
                    LOCAL_GAA_FILE);
                if(!gaa_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }
                result = GLOBUS_GSI_SYSCONFIG_CHECK_CERTFILE(
                    gaa_filename);

                if(result != GLOBUS_SUCCESS)
                {
                    free(gaa_filename);
                    gaa_filename = NULL;

                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GAA_FILENAME);
                    goto exit;
                }

            }
            else
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GAA_FILENAME);
                goto exit;
            }
        }
    }

    *filename = gaa_filename;
    gaa_filename = NULL;

 exit:

    if(home_dir != NULL)
    {
        free(home_dir);
    }

    if(gaa_filename != NULL)
    {
        free(gaa_filename);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;

}

/**
 * @brief Win32 - Check if the current user is root
 * @details
 * Checks whether the current user is root.
 * @ingroup globus_gsi_sysconfig_win32
 *
 * @param is_superuser
 *        1 if the user is the superuser
 *        0 if not
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */ 
globus_result_t
globus_gsi_sysconfig_is_superuser_win32(
    int *                               is_superuser)
{
    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    /* Always true for now */
    *is_superuser = 1;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;

    return GLOBUS_SUCCESS;
}

/**
 * @brief Win32 - Get Signing Policy Filename
 * @ingroup globus_gsi_sysconfig_win32
 * @details
 * Get the Signing Policy Filename on the current system,
 * based on the CA's subject name, and the trusted certificates
 * directory
 *
 * @param ca_name
 *        The X.509 subject name of the CA to get the signing policy of.
 *        The hash of the CA is generated from this
 *
 * @param cert_dir
 *        The trusted CA certificates directory, containing the singing_policy
 *        files of the trusted CAs.
 *
 * @param signing_policy_filename
 *        The resulting singing_policy filename
 * @return 
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 */
globus_result_t
globus_gsi_sysconfig_get_signing_policy_filename_win32(
    X509_NAME *                         ca_name,
    char *                              cert_dir,
    char **                             signing_policy_filename)
{
    char *                              signing_policy = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;
    unsigned long                       hash;
    char *                              ca_cert_dir = NULL;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *signing_policy_filename = NULL;

    if (cert_dir == NULL)
    {
        result = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&ca_cert_dir);
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_SIGNING_POLICY);
            goto exit;
        }
    }
    else
    {
        ca_cert_dir = cert_dir;
    }

    if(ca_name == NULL)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_SIGNING_POLICY,
            (_GSSL("NULL parameter ca_name passed to: %s"), __func__));
        goto exit;
    }

    hash = X509_NAME_hash(ca_name);

    signing_policy = globus_common_create_string(
        "%s%s%08lx%s", 
        ca_cert_dir, FILE_SEPARATOR, hash, SIGNING_POLICY_FILE_EXTENSION);

    if(signing_policy == NULL)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(signing_policy);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_SIGNING_POLICY);
        goto exit;
    }

    *signing_policy_filename = signing_policy;

 exit:

    if(ca_cert_dir != NULL &&
       cert_dir == NULL)
    {
        free(ca_cert_dir);
    }

    if(signing_policy != NULL &&
       result != GLOBUS_SUCCESS)
    {
        free(signing_policy);
        *signing_policy_filename = NULL;
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}
#endif

/* END WIN32 SYSCONFIG DEFINITIONS */

#if defined(DOXYGEN) || !defined(WIN32)

/* BEGIN UNIX SYSCONFIG DEFINITIONS */

/**
 * @brief UNIX - Set Key Permissions
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Set the file permissions of a file to read-write only by the user
 * which are the permissions that should be set for all private keys.
 *
 * @param filename
 *
 * @return
 *        GLOBUS_SUCCESS or an error object id
 */
globus_result_t
globus_gsi_sysconfig_set_key_permissions_unix(
    char *                              filename)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    int					fd = -1;
    mode_t                              oldmask;
    struct stat                         stx, stx2;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    oldmask = globus_libc_umask(0077);
    if((fd = globus_libc_open(
		    filename, O_RDONLY|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR)) < 0)
    {
        result = globus_error_put(
            globus_error_wrap_errno_error(
                GLOBUS_GSI_SYSCONFIG_MODULE,
                errno,
                GLOBUS_GSI_SYSCONFIG_ERROR_ERRNO,
                __FILE__,
                __func__,
                __LINE__,
                "Error opening keyfile for reading\n"));
        goto exit;
    }

    if(lstat(filename, &stx) != 0 || fstat(fd, &stx2) != 0)
    {
        result = globus_error_put(
            globus_error_wrap_errno_error(
                GLOBUS_GSI_SYSCONFIG_MODULE,
                errno,
                GLOBUS_GSI_SYSCONFIG_ERROR_ERRNO,
                __FILE__,
                __func__,
                __LINE__,
                "Error getting status of keyfile\n"));
        goto exit;
    }

    /*
     * use any stat output as random data, as it will 
     * have file sizes, and last use times in it. 
     */
    RAND_add((void*)&stx, sizeof(stx), 2);

    if(S_ISDIR(stx.st_mode))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_IS_DIR,
            (_GSSL("File: %s"), filename));
        goto exit;
    }
    else if(!S_ISREG(stx.st_mode))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_REGULAR,
            (_GSSL("File: %s"), filename));
        goto exit;
    }
    else if(stx.st_nlink != 1)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_HAS_LINKS,
            (_GSSL("File: %s"), filename));
        goto exit;
    }
    else if(stx.st_ino != stx2.st_ino || stx.st_dev != stx2.st_dev)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_HAS_CHANGED,
            (_GSSL("File: %s"), filename));
        goto exit;
    }

    if(fchmod(fd, S_IRUSR|S_IWUSR) < 0)
    {
        result = globus_error_put(
            globus_error_wrap_errno_error(
                GLOBUS_GSI_SYSCONFIG_MODULE,
                errno,
                GLOBUS_GSI_SYSCONFIG_ERROR_SETTING_PERMS,
                __FILE__,
                __func__,
                __LINE__,
                "Error setting permissions to user read only of file: %s\n", 
                filename));
        goto exit;
    }

 exit:
    if (fd >= 0)
    {
	close(fd);
    }
    globus_libc_umask(oldmask);
    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get User ID
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get a unique string representing the current user.  This is just
 * the uid converted to a string.  
 *
 * @param user_id_string
 *        A unique string representing the user
 *
 * @return
 *        GLOBUS_SUCCESS unless an error occurred
 */
globus_result_t
globus_gsi_sysconfig_get_user_id_string_unix(
    char **                             user_id_string)
{
#ifndef HAVE_GETEUID
    *user_id_string = globus_libc_strdup("0");

    if (*user_id_string == NULL)
    {
        return GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
    }
    else
    {
        return GLOBUS_SUCCESS;
    }
#else
    uid_t                               uid;
    int                                 len;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    uid = geteuid();

    len = globus_libc_printf_length("%d",uid);

    len++;

    if((*user_id_string = malloc(len)) == NULL)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    globus_libc_snprintf(*user_id_string,len,"%d",uid);

    result = GLOBUS_SUCCESS;

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
#endif
}

/**
 * @brief UNIX - Get Username
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get the username of the current user.
 *
 * @param username
 *        This parameter will contain the current user name upon a successful
 *        return. It is the users responsibility to free memory allocated for
 *        this return value.
 * @return
 *        GLOBUS_SUCCESS unless an error occurred
 */
globus_result_t
globus_gsi_sysconfig_get_username_unix(
    char **                             username)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    struct passwd                       pwd;
    struct passwd *                     pwd_result;
    char *                              buf;
    int                                 buf_len;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    /* the below seems to be fairly portable */
#ifdef _SC_GETPW_R_SIZE_MAX
    buf_len = sysconf(_SC_GETPW_R_SIZE_MAX) + 1;
    if(buf_len < 1)
    {
        buf_len = 1024;
    }
#else
    buf_len = 1024;
#endif

    buf = malloc(buf_len);

    if(buf == NULL)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    if(globus_libc_getpwuid_r(geteuid(),
                              &pwd,
                              buf,
                              buf_len,
                              &pwd_result) != 0)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PW_ENTRY,
            (_GSSL("Error occured for uid: %d"),geteuid()));        
        goto exit;
    }

    if(pwd_result == NULL || pwd_result->pw_name == NULL)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PW_ENTRY,
            (_GSSL("Error occured for uid: %d"),geteuid()));        
        goto exit;        
    }

    *username = malloc(strlen(pwd_result->pw_name) + 1);

    if(!*username)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    strncpy(*username, pwd_result->pw_name, 
            strlen(pwd_result->pw_name) + 1);

 exit:

    if(buf != NULL)
    {
        free(buf);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get Process ID
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get a unique string representing the current process.  This is just
 * the pid converted to a string.  
 *
 * @param proc_id_string
 *        A unique string representing the process
 *
 * @return
 *        GLOBUS_SUCCESS unless an error occurred
 */
globus_result_t
globus_gsi_sysconfig_get_proc_id_string_unix(
    char **                             proc_id_string)
{
    pid_t                               pid;
    int                                 len;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    pid = getpid();

    len = globus_libc_printf_length("%d",pid);

    len++;

    if((*proc_id_string = malloc(len)) == NULL)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    globus_libc_snprintf(*proc_id_string,len,"%d",pid);

    result = GLOBUS_SUCCESS;

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Make Absolute Path
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Make the filename into an absolute path string based
 * on the current working directory.
 *
 * @param filename
 *        the filename to get the absolute path of.  
 * @param absolute_path
 *        The resulting absolute path.  This needs to 
 *        be freed when no longer needed.
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise
 *        an error object ID is returned
 */
globus_result_t
globus_gsi_sysconfig_make_absolute_path_for_filename_unix(
    char *                              filename,
    char **                             absolute_path)
{
    int                                 length;
    char *                              cwd = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if(filename[0] != '/')
    {
        result = GLOBUS_GSI_SYSCONFIG_GET_CURRENT_WORKING_DIR(&cwd);
        if(result != GLOBUS_SUCCESS)
        {
            cwd = NULL;
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CWD);
            goto exit;
        }

        length = strlen(cwd) + strlen(filename) + 2;

        *absolute_path = malloc(length);
        if(!*absolute_path)
        {
            result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }
        globus_libc_snprintf(*absolute_path, length, "%s/%s", cwd, filename);
    }
    else
    {
        length = strlen(filename) + 1;

        *absolute_path = malloc(length);
        if(!*absolute_path)
        {
            result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }
        globus_libc_snprintf(*absolute_path, length, "%s", filename);
    }

 exit:

    if(cwd != NULL)
    {
        free(cwd);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Split Directory and Filename
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Split the directory and filename portions of a filename string
 * into two separate strings
 *
 * @param full_filename
 *        The filename to split.  Splits on the last occurrence of '/'
 *        where the directory is everything before the last '/', and
 *        the filename is everything after.
 * @param dir_string  
 *        The directory portion of the filename string.  If no '/' is found
 *        throughout the string, this variable points to NULL.
 *        This needs to be freed when no longer needed.
 * @param filename_string
 *        The filename portion of the filename string.  If no '/' is found
 *        throughout, this variable is a duplicate of the full_filename 
 *        parameter.  This needs to be freed when no longer needed.
 *
 * @return
 *        GLOBUS_SUCCESS if no error occurred.  Otherwise an error object ID
 *        is returned.
 */
globus_result_t
globus_gsi_sysconfig_split_dir_and_filename_unix(
    char *                              full_filename,
    char **                             dir_string,
    char **                             filename_string)
{
    int                                 dir_string_length;
    int                                 filename_string_length;
    char *                              split_index = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *dir_string = NULL;
    *filename_string = NULL;

    split_index = strrchr(full_filename, '/');
    if(!split_index)
    {
        *dir_string = NULL;
        filename_string_length = strlen(full_filename) + 1;
        *filename_string = malloc(filename_string_length);
        if(!*filename_string)
        {
            result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

        globus_libc_snprintf(*filename_string, filename_string_length, 
                             "%s", full_filename); 
    }
    else
    {
        dir_string_length = split_index - full_filename + 1;

        *dir_string = malloc(dir_string_length);

        if(!*dir_string)
        {
            result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

        globus_libc_snprintf(*dir_string,
                             dir_string_length, "%s", full_filename);

        filename_string_length = strlen(full_filename) - dir_string_length + 1;

        *filename_string = malloc(filename_string_length);

        if(!*filename_string)
        {
            result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            if(*dir_string)
            {
                free(*dir_string);
            }
            goto exit;
        }

        globus_libc_snprintf(*filename_string,
                             filename_string_length, "%s",
                             &full_filename[dir_string_length]);
    }

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get Current Working Directory
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get the current working directory on the system.  
 *
 * @param working_dir
 *        The current working directory
 * @return
 *        GLOBUS_SUCCESS or an error object identifier
 */
globus_result_t
globus_gsi_sysconfig_get_current_working_dir_unix(
    char **                             working_dir)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char *                              buffer = NULL;
    char *                              result_buffer = NULL;
    int                                 length = 128;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    buffer = malloc(length);
    if(!buffer)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    while(1)
    {
        result_buffer = getcwd(buffer, length);
        if(!result_buffer && errno == ERANGE)
        {
            length *= 2;
            if(!(result_buffer = realloc(buffer, length)))
            {
                free(buffer);
                result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                goto exit;
            }

            buffer = result_buffer;
        }
        else if(!result_buffer)
        {
            result = 
                globus_error_put(globus_error_wrap_errno_error(
                    GLOBUS_GSI_SYSCONFIG_MODULE,
                    errno,
                    GLOBUS_GSI_SYSCONFIG_ERROR_ERRNO,
                    __FILE__,
                    __func__,
                    __LINE__,
                    "Couldn't get the current working directory"));
        }
        else
        {
            break;
        }
    }

    *working_dir = result_buffer;

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get HOME Directory
 * @ingroup globus_gsi_sysconfig_unix
 * Get the HOME Directory of the current user.  Should
 * be the $HOME environment variable.
 *
 * @param home_dir
 *        The home directory of the current user
 * @return
 *        GLOBUS_SUCCESS if no error occured, otherwise
 *        an error object is returned.
 */
globus_result_t
globus_gsi_sysconfig_get_home_dir_unix(
    char **                             home_dir)
{
    char *                              temp_home_dir;
    struct passwd                       pwd;
    struct passwd *                     pwd_result;
    char *                              buf;
    int                                 buf_len;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *home_dir = NULL;

    /* the below seems to be fairly portable */

#ifdef _SC_GETPW_R_SIZE_MAX
    buf_len = sysconf(_SC_GETPW_R_SIZE_MAX) + 1;
    if(buf_len < 1)
    {
        buf_len = 1024;
    }
#else
    buf_len = 1024;
#endif

    buf = malloc(buf_len);

    if(buf == NULL)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    if(globus_libc_getpwuid_r(geteuid(),
                              &pwd,
                              buf,
                              buf_len,
                              &pwd_result) != 0)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PW_ENTRY,
            (_GSSL("Error occured for uid: %d"),geteuid()));        
        goto exit;
    }

    if(pwd_result == NULL || pwd_result->pw_dir == NULL)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PW_ENTRY,
            (_GSSL("Error occured for uid: %d"),geteuid()));        
        goto exit;        
    }

    temp_home_dir = malloc(strlen(pwd_result->pw_dir) + 1);
    if(temp_home_dir)
    {
        strncpy(temp_home_dir, pwd_result->pw_dir, 
                strlen(pwd_result->pw_dir) + 1);

        result = GLOBUS_GSI_SYSCONFIG_DIR_EXISTS(temp_home_dir);
        if(result != GLOBUS_SUCCESS)
        {
            free(temp_home_dir);
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_HOME_DIR);
            goto exit;
        }

        *home_dir = temp_home_dir;
    }
    else
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_HOME_DIR,
            (_GSSL("Could not get a defined HOME directory for user id: %d\n"),
             geteuid()));
        goto exit;
    }

    result = GLOBUS_SUCCESS;

 exit:

    if(buf != NULL)
    {
        free(buf);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - File Exists
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Check if the file exists
 *
 * @param filename
 *        The filename of the file to check for
 *
 * @return
 *        GLOBUS_SUCCESS if the file exists and is readable,
 *        otherwise an error object identifier
 */
globus_result_t
globus_gsi_sysconfig_file_exists_unix(
    const char *                        filename)
{
    struct stat                         stx;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if (stat(filename,&stx) == -1)
    {
        switch(errno)
        {
          case ENOENT:
          case ENOTDIR:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_DOES_NOT_EXIST,
                (_GSSL("%s is not a valid file"), filename));            
            goto exit;

          case EACCES:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_BAD_PERMISSIONS,
                (_GSSL("Could not read %s"), filename));            
            goto exit;

          default:
            result = globus_error_put(
                globus_error_wrap_errno_error(
                    GLOBUS_GSI_SYSCONFIG_MODULE,
                    errno,
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHECKING_FILE_EXISTS,
                    __FILE__,
                    __func__,
                    __LINE__,
                    "Error getting status of file: %s\n",
                    filename));
            goto exit;

        }
    }

    /*
     * use any stat output as random data, as it will 
     * have file sizes, and last use times in it. 
     */
    RAND_add((void*)&stx,sizeof(stx),2);

    if (stx.st_size == 0)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_ZERO_LENGTH,
            (_GSSL("File: %s"), filename));            
        goto exit;
    }

    if(stx.st_mode & S_IFDIR)
    { 
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_IS_DIR,
            (_GSSL("File: %s"), filename));       
    }
    else if((stx.st_mode & S_IFMT) &
            ~ (S_IFREG | S_IFLNK | S_IFDIR))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_REGULAR,
            (_GSSL("File: %s"), filename));
    }

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}    

/**
 * @brief UNIX - Directory Exists
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Check if the directory exists
 *
 * @param filename
 *        The filename of the directory to check for
 *
 * @return
 *        GLOBUS_SUCCESS if the directory exists, otherwise an error
 *        object identifier.
 */
globus_result_t
globus_gsi_sysconfig_dir_exists_unix(
    const char *                        filename)
{
    struct stat                         stx;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if (stat(filename,&stx) == -1)
    {
        switch(errno)
        {
          case ENOENT:
          case ENOTDIR:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_DOES_NOT_EXIST,
                (_GSSL("%s is not a valid directory"), filename));            
            goto exit;

          case EACCES:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_BAD_PERMISSIONS,
                (_GSSL("Could not read %s"), filename));            
            goto exit;

          default:
            result = globus_error_put(
                globus_error_wrap_errno_error(
                    GLOBUS_GSI_SYSCONFIG_MODULE,
                    errno,
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHECKING_FILE_EXISTS,
                    __FILE__,
                    __func__,
                    __LINE__,
                    "Error getting status of certificate directory: %s\n",
                    filename));
            goto exit;

        }
    }

    /*
     * use any stat output as random data, as it will 
     * have file sizes, and last use times in it. 
     */
    RAND_add((void*)&stx,sizeof(stx),2);

    if(!(stx.st_mode & S_IFDIR))
    { 
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_DIR,
            (_GSSL("%s is not a directory"), filename));       
    }

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}    


/**
 * @brief UNIX - Check File Status for Key
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * This is a convenience function used to check the status of a 
 * private key file.  The desired status is only the current user has
 * ownership and read permissions, everyone else should not be able
 * to access it.
 * 
 * @param filename
 *        The name of the file to check the status of
 *
 * @return 
 *        GLOBUS_SUCCESS if the status of the file was able
 *        to be determined.  Otherwise, an error object
 *        identifier
 *
 */
globus_result_t
globus_gsi_sysconfig_check_keyfile_unix(
    const char *                        filename)
{
    return globus_gsi_sysconfig_check_keyfile_uid_unix(filename, geteuid());
}

/**
 * @brief UNIX - Check File Status for Key
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * This is a convenience function used to check the status of a 
 * private key file.  The desired status is only the specified user has
 * ownership and read permissions, everyone else should not be able
 * to access it.
 * 
 * @param filename
 *        The name of the file to check the status of
 * @param uid
 *        The owner of the file to check the status of
 *
 * @return 
 *        GLOBUS_SUCCESS if the status of the file was able
 *        to be determined.  Otherwise, an error object
 *        identifier
 *
 */
globus_result_t
globus_gsi_sysconfig_check_keyfile_uid_unix(
    const char *                        filename,
    uid_t                               uid)
{
    struct stat                         stx;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

#ifdef TARGET_ARCH_CYGWIN
    if(globus_l_gsi_sysconfig_skip_check)
    {
        return globus_gsi_sysconfig_file_exists_unix(filename);
    }
#endif

    if (stat(filename,&stx) == -1)
    {
        switch (errno)
        {
          case ENOENT:
          case ENOTDIR:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_DOES_NOT_EXIST,
                (_GSSL("%s is not a valid file"), filename));
            goto exit;

          case EACCES:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_BAD_PERMISSIONS,
                (_GSSL("Could not read %s"), filename));            
            goto exit;

          default:
            result = globus_error_put(
                globus_error_wrap_errno_error(
                    GLOBUS_GSI_SYSCONFIG_MODULE,
                    errno,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING,
                    __FILE__,
                    __func__,
                    __LINE__,
                    "Error getting status of file: %s\n",
                    filename));
            goto exit;
        }
    }

    /*
     * use any stat output as random data, as it will 
     * have file sizes, and last use times in it. 
     */
    RAND_add((void*)&stx,sizeof(stx),2);

    if (stx.st_uid != uid)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_OWNED,
            (_GSSL("%s is not owned by current user"), filename));    
        goto exit;
    }

    /* check that the key file is not x by user, or rwx by group or others */
    if (stx.st_mode & (S_IXUSR | 
                       S_IRGRP | S_IWGRP | S_IXGRP |
                       S_IROTH | S_IWOTH | S_IXOTH))
    {
        GLOBUS_I_GSI_SYSCONFIG_DEBUG_FPRINTF(
            2, (stderr, "checkstat:%s:mode:%o\n", filename, stx.st_mode)); 

        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_BAD_PERMISSIONS,
            (_GSSL("Permissions on %s are too permissive. Maximum allowable permissions are 600"), filename));
        goto exit;
    }

    if (stx.st_size == 0)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_ZERO_LENGTH,
            (_GSSL("File: %s"), filename));            
        goto exit;
    }

    if(stx.st_mode & S_IFDIR)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_IS_DIR,
            (_GSSL("File: %s"), filename));        
    }
    else if((stx.st_mode & S_IFMT)
            & ~(S_IFLNK | S_IFREG | S_IFDIR))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_REGULAR,
            (_GSSL("File: %s"), filename));
    }

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;

    return result;
}


/**
 * @brief UNIX - Check File Status for Cert
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * This is a convenience function used to check the status of a 
 * certificate file.  The desired status is the current user has
 * ownership and read/write permissions, while group and others only
 * have read permissions.
 * 
 * @param filename
 *        The name of the file to check the status of
 *
 * @return 
 *        GLOBUS_SUCCESS if the status of the file was able
 *        to be determined.  Otherwise, an error object
 *        identifier
 *
 */
globus_result_t
globus_gsi_sysconfig_check_certfile_unix(
    const char *                        filename)
{
    return globus_gsi_sysconfig_check_certfile_uid_unix(filename, geteuid());
}


/**
 * @brief UNIX - Check File Status for Cert
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * This is a convenience function used to check the status of a 
 * certificate file.  The desired status is the current user has
 * ownership and read/write permissions, while group and others only
 * have read permissions.
 * 
 * @param filename
 *        The name of the file to check the status of
 * @param uid
 *        The user id that may be the owner of the file
 *
 * @return 
 *        GLOBUS_SUCCESS if the status of the file was able
 *        to be determined.  Otherwise, an error object
 *        identifier
 *
 */
globus_result_t
globus_gsi_sysconfig_check_certfile_uid_unix(
    const char *                        filename,
    uid_t                               uid)
{
    struct stat                         stx;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

#ifdef TARGET_ARCH_CYGWIN
    if(globus_l_gsi_sysconfig_skip_check)
    {
        return globus_gsi_sysconfig_file_exists_unix(filename);
    }
#endif

    if (stat(filename,&stx) == -1)
    {
        switch (errno)
        {
          case ENOENT:
          case ENOTDIR:
            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_DOES_NOT_EXIST,
                (_GSSL("%s is not a valid file"), filename));
            goto exit;

          case EACCES:

            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_FILE_BAD_PERMISSIONS,
                (_GSSL("Could not read %s"), filename));
            goto exit;

          default:
            result = globus_error_put(
                globus_error_wrap_errno_error(
                    GLOBUS_GSI_SYSCONFIG_MODULE,
                    errno,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_FILENAME,
                    __FILE__,
                    __func__,
                    __LINE__,
                    "Error getting status of file %s\n",
                    filename));
            goto exit;
        }
    }

    /*
     * use any stat output as random data, as it will 
     * have file sizes, and last use times in it. 
     */
    RAND_add((void*)&stx,sizeof(stx),2);

    if (stx.st_uid != uid)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_OWNED,
            (_GSSL("%s is not owned by current user"), filename));    
        goto exit;
    }

    /* check that the cert file is not x by user, or wx by group or others */
    if (stx.st_mode & (S_IXUSR |
                       S_IWGRP | S_IXGRP |
                       S_IWOTH | S_IXOTH))
    {
        GLOBUS_I_GSI_SYSCONFIG_DEBUG_FPRINTF(
            2, (stderr, "checkstat:%s:mode:%o\n",filename,stx.st_mode));

        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_BAD_PERMISSIONS,
            (_GSSL("Permissions on %s are too permissive. Maximum allowable permissions are 644"), filename));
        goto exit;
    }

    if (stx.st_size == 0)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_ZERO_LENGTH,
            (_GSSL("File: %s"), filename));            
        goto exit;
    }

    if(stx.st_mode & S_IFDIR)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_IS_DIR,
            (_GSSL("File: %s"), filename));
    }
    else if((stx.st_mode & S_IFMT) &
            ~(S_IFREG | S_IFLNK | S_IFDIR))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_FILE_NOT_REGULAR,
            (_GSSL("File: %s"), filename));
    }

 exit:

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get Trusted CA Cert Dir
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get the Trusted Certificate Directory containing the trusted
 * Certificate Authority certificates.  This directory is determined
 * in the order shown below.  Failure in one method results in attempting
 * the next.
 *
 * <ol>
 * <li> <b>X509_CERT_DIR environment variable</b> - if this is set, the
 * trusted certificates will be searched for in that directory.  This
 * variable allows the end user to specify the location of trusted
 * certificates.
 * <li> <b>$HOME/.globus/certificates</b> - If this
 * directory exists, and the previous methods of determining the trusted
 * certs directory failed, this directory will be used.  
 * <li> <b>/etc/grid-security/certificates</b> - This location is intended
 * to be independent of the globus installation ($GLOBUS_LOCATION), and 
 * is generally only writeable by the host system administrator.  
 * <li> <b>$GLOBUS_LOCATION/share/certificates</b>
 * </ol>
 *
 * @param cert_dir
 *        The trusted certificates directory
 * @return
 *        GLOBUS_SUCCESS if no error occurred, and a sufficient trusted
 *        certificates directory was found.  Otherwise, an error object 
 *        identifier returned.
 */
globus_result_t
globus_gsi_sysconfig_get_cert_dir_unix(
    char **                             cert_dir)
{
    char *                              env_cert_dir = NULL;
    char *                              local_cert_dir = NULL;
    char *                              default_cert_dir = NULL;
    char *                              installed_cert_dir = NULL;
    globus_result_t                     result;
    globus_object_t *                   err = NULL;
    char *                              home = NULL;
    char *                              location;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *cert_dir = NULL;

    if(getenv(X509_CERT_DIR))
    {
        result = globus_i_gsi_sysconfig_create_cert_dir_string(
            cert_dir, 
            & env_cert_dir,
            "%s",
            getenv(X509_CERT_DIR));
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
            goto done;
        }
    }

    /* now check for a trusted CA directory in the user's home directory */
    if(!(*cert_dir))
    {
        result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);

        if(result == GLOBUS_SUCCESS)
        { 
            result = globus_i_gsi_sysconfig_create_cert_dir_string(
                cert_dir, 
                &local_cert_dir,
                "%s%s%s",
                home,
                FILE_SEPARATOR,
                X509_LOCAL_TRUSTED_CERT_DIR);
            if(result != GLOBUS_SUCCESS &&
               !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result) &&
               !GLOBUS_GSI_SYSCONFIG_FILE_HAS_BAD_PERMISSIONS(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
                goto done;
            }
            else if (result != GLOBUS_SUCCESS)
            {
                err = globus_error_get(result);
                if (err)
                {
                    globus_object_free(err);
                }
            }
        }
        else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result) &&
                !GLOBUS_GSI_SYSCONFIG_FILE_HAS_BAD_PERMISSIONS(result))
        {
	    home = NULL;
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
            goto done;
        }
        else
        {
            err = globus_error_get(result);
            if (err)
            {
                globus_object_free(err);
            }
        }
    }

    /* now look in /etc/grid-security/certificates */
    if (!(*cert_dir))
    {
        result = globus_i_gsi_sysconfig_create_cert_dir_string(
            cert_dir,
            &installed_cert_dir,
            "%s",
            X509_DEFAULT_TRUSTED_CERT_DIR);
        if(result != GLOBUS_SUCCESS &&
           !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result) &&
           !GLOBUS_GSI_SYSCONFIG_FILE_HAS_BAD_PERMISSIONS(result))
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
            goto done;
        }
        else if (result != GLOBUS_SUCCESS)
        {
            err = globus_error_get(result);
            if (err)
            {
                globus_object_free(err);
            }
        }
    }

    /* now look in  $GLOBUS_LOCATION/share/certificates */
    if (!(*cert_dir))
    {
        globus_location(&location);

        if (location)
        {
            result = globus_i_gsi_sysconfig_create_cert_dir_string(
                cert_dir,
                &default_cert_dir,
                "%s%s%s",
                location,
                FILE_SEPARATOR,
                X509_INSTALLED_TRUSTED_CERT_DIR);
            if(result != GLOBUS_SUCCESS &&
               !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result) &&
               !GLOBUS_GSI_SYSCONFIG_FILE_HAS_BAD_PERMISSIONS(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
                goto done;
            }
            else if (result != GLOBUS_SUCCESS)
            {
                err = globus_error_get(result);
                if (err)
                {
                    globus_object_free(err);
                }
            }
        }
    }

    if(!(*cert_dir))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR,
            (_GSSL("The trusted certificates directory could not be "
             "found in any of the following locations: \n"
             "1) env. var. X509_CERT_DIR\n"
             "2) $HOME/.globus/certificates\n"
             "3) /etc/grid-security/certificates"
             "\n4) $GLOBUS_LOCATION/share/certificates\n")));

        goto done;
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_FPRINTF(
        2, (stderr, "Using cert_dir = %s\n", 
            (*cert_dir ? *cert_dir : "null")));

    result = GLOBUS_SUCCESS;

 done:

    if(result != GLOBUS_SUCCESS)
    {
        *cert_dir = NULL;
    }

    if(home != NULL)
    {
	free(home);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;

    return result;
}

/**
 * @brief UNIX - Get User Certificate and Key Filenames
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get the User Certificate Filename based on the current user's
 * environment.  The following locations are searched for cert and key
 * files in order:
 * 
 * <ol>
 * <li>environment variables X509_USER_CERT and X509_USER_KEY
 * <li>$HOME/.globus/usercert.pem and 
 *     $HOME/.globus/userkey.pem
 * <li>$HOME/.globus/usercred.p12 - this is a PKCS12 credential
 * </ol>
 *
 * @param user_cert
 *        pointer the filename of the user certificate
 * @param user_key
 *        pointer to the filename of the user key
 * @return
 *        GLOBUS_SUCCESS if the cert and key files were found in one
 *        of the possible locations, otherwise an error object identifier
 *        is returned
 */
globus_result_t
globus_gsi_sysconfig_get_user_cert_filename_unix(
    char **                             user_cert,
    char **                             user_key)
{
    char *                              home = NULL;
    char *                              env_user_cert = NULL;
    char *                              env_user_key = NULL;
    char *                              default_user_cert = NULL;
    char *                              default_user_key = NULL;
    char *                              default_pkcs12_user_cred = NULL;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;


    /* first, check environment variables for valid filenames */

    if(user_cert)
    {
        *user_cert = NULL;
        if(getenv(X509_USER_CERT))
        {
            result = globus_i_gsi_sysconfig_create_cert_string(
                user_cert,
                &env_user_cert,
                "%s",
                getenv(X509_USER_CERT));
            if(result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }            
        }

        if(!(*user_cert))
        {
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);
            if(result == GLOBUS_SUCCESS)
            {
                result = globus_i_gsi_sysconfig_create_cert_string(
                    user_cert,
                    &default_user_cert,
                    "%s%s%s",
                    home,
                    FILE_SEPARATOR,
                    X509_DEFAULT_USER_CERT);

                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                home = NULL;
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }
        }
    }

    if(user_key)
    { 
        *user_key = NULL;
        result = GLOBUS_SUCCESS;

        if(getenv(X509_USER_KEY))
        {
            result = globus_i_gsi_sysconfig_create_key_string(
                user_key,
                &env_user_key,
                "%s",
                getenv(X509_USER_KEY));
            if(result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                goto done;
            }
        }

        if(!(*user_key))
        {
            if(!home)
            {
                result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);
            }

            if(result == GLOBUS_SUCCESS)
            {
                result = globus_i_gsi_sysconfig_create_key_string(
                    user_key,
                    &default_user_key,
                    "%s%s%s",
                    home,
                    FILE_SEPARATOR,
                    X509_DEFAULT_USER_KEY);
                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                home = NULL;
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }
        }
    }

    /* if the cert & key don't exist in the default locations
     * or those specified by the environment variables, a
     * pkcs12 cert will be searched for
     */
    if(user_cert && user_key && !(*user_cert) && !(*user_key))
    {
        result = GLOBUS_SUCCESS;
        if(!home)
        { 
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);
        }

        if(result == GLOBUS_SUCCESS)
        {
            result = globus_i_gsi_sysconfig_create_key_string(
                user_key,
                &default_pkcs12_user_cred,
                "%s%s%s",
                home,
                FILE_SEPARATOR,
                X509_DEFAULT_PKCS12_FILE);
            if(result != GLOBUS_SUCCESS &&
               !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                goto done;
            }
            *user_cert = globus_libc_strdup(*user_key);
        }
        else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
        {
            home = NULL;
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
            goto done;            
        }
    }

    if(user_cert && !(*user_cert))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING,
            (_GSSL("The user cert could not be found in: \n"
             "1) env. var. X509_USER_CERT\n"
             "2) $HOME/.globus/usercert.pem\n"
             "3) $HOME/.globus/usercred.p12\n\n")));
        goto done;
    }

    if(user_key && !(*user_key))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING,
            (_GSSL("The user key could not be found in:\n,"
             "1) env. var. X509_USER_KEY\n"
             "2) $HOME/.globus/userkey.pem\n"
             "3) $HOME/.globus/usercred.p12\n\n")));
        goto done;
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_FPRINTF(
        2, (stderr,"Using x509_user_cert=%s\n      x509_user_key =%s\n",
            user_cert ? *user_cert : "NULL",
            user_key ? *user_key : "NULL"));

    result = GLOBUS_SUCCESS;

 done:
    if(result != GLOBUS_SUCCESS && user_cert)
    {
        *user_cert = NULL;
    }

    if(home)
    {
        free(home);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get Host Certificate and Key Filenames
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get the Host Certificate and Key Filenames based on the current user's
 * environment.  The host cert and key are searched for in the following 
 * locations (in order):
 *
 * <ol>
 * <li>X509_USER_CERT and X509_USER_KEY environment variables</li>
 * <li><em>$GLOBUS_LOCATION</em>/etc/host[cert|key].pem</li>
 * <li><em>$HOME</em>/.globus/host[cert|key].pem</li>
 * </ol>
 * 
 * @param host_cert
 *        pointer to the host certificate filename
 * @param host_key
 *        pointer to the host key filename
 *
 * @return
 *        GLOBUS_SUCCESS if the host cert and key were found, otherwise
 *        an error object identifier is returned 
 */
globus_result_t
globus_gsi_sysconfig_get_host_cert_filename_unix(
    char **                             host_cert,
    char **                             host_key)
{
    char *                              home = NULL;
    char *                              env_host_cert = NULL;
    char *                              env_host_key = NULL;
    char *                              default_host_cert = NULL;
    char *                              default_host_key = NULL;
    char *                              installed_host_cert = NULL;
    char *                              installed_host_key = NULL;
    char *                              local_host_cert = NULL;
    char *                              local_host_key = NULL;
    char *                              location = NULL;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *host_cert = NULL;
    *host_key = NULL;

    /* first check environment variables for valid filenames */

    if(getenv(X509_USER_CERT) && getenv(X509_USER_KEY))
    {
        result = globus_i_gsi_sysconfig_create_cert_string(
            host_cert,
            &env_host_cert,
            "%s",
            getenv(X509_USER_CERT));
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
            goto done;
        }

        result = globus_i_gsi_sysconfig_create_key_string(
            host_key,
            &env_host_key,
            "%s",
            getenv(X509_USER_KEY));
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
            goto done;
        }
    }

    /* now check default locations for valid filenames */
    if(!(*host_cert) && !(*host_key))
    {
        result = globus_i_gsi_sysconfig_create_cert_string(
            host_cert,
            & default_host_cert,
            "%s%s%s%s",
            X509_DEFAULT_CERT_DIR,
            FILE_SEPARATOR,
            X509_HOST_PREFIX,
            X509_CERT_SUFFIX);

        if(result == GLOBUS_SUCCESS)
        { 
            result = globus_i_gsi_sysconfig_create_key_string(
                host_key,
                & default_host_key,
                "%s%s%s%s",
                X509_DEFAULT_CERT_DIR,
                FILE_SEPARATOR,
                X509_HOST_PREFIX,
                X509_KEY_SUFFIX);
            if(result != GLOBUS_SUCCESS &&
               !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                goto done;
            }
        }
        else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
            goto done;
        }
    }

    /* now check installed location for host cert */
    if(!(*host_cert) && !(*host_key)) 
    {
        globus_location(&location);

        if(location)
        {
            result = globus_i_gsi_sysconfig_create_cert_string(
                host_cert,
                &installed_host_cert,
                "%s%s%s%s%s%s",
                location,
                FILE_SEPARATOR,
                X509_INSTALLED_CERT_DIR,
                FILE_SEPARATOR,
                X509_HOST_PREFIX,
                X509_CERT_SUFFIX);

            if(result == GLOBUS_SUCCESS)
            { 
                result = globus_i_gsi_sysconfig_create_key_string(
                    host_key,
                    &installed_host_key,
                    "%s%s%s%s%s%s",
                    location,
                    FILE_SEPARATOR,
                    X509_INSTALLED_CERT_DIR,
                    FILE_SEPARATOR,
                    X509_HOST_PREFIX,
                    X509_KEY_SUFFIX);
                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }
        }
    }

    if(!(*host_cert) && !(*host_key)) 
    {
        result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);

        if(result == GLOBUS_SUCCESS)
        {
            result = globus_i_gsi_sysconfig_create_cert_string(
                host_cert,
                &local_host_cert,
                "%s%s%s%s%s%s",
                home,
                FILE_SEPARATOR,
                X509_LOCAL_CERT_DIR,
                FILE_SEPARATOR,
                X509_HOST_PREFIX,
                X509_CERT_SUFFIX);

            if(result == GLOBUS_SUCCESS)
            { 
                result = globus_i_gsi_sysconfig_create_key_string(
                    host_key,
                    & local_host_key,
                    "%s%s%s%s%s%s",
                    home,
                    FILE_SEPARATOR,
                    X509_LOCAL_CERT_DIR,
                    FILE_SEPARATOR,
                    X509_HOST_PREFIX,
                    X509_KEY_SUFFIX);
                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }
        }
        else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
            home = NULL;
            goto done;
        }
    }

    if(!(*host_cert) || !(*host_key))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_FILENAME,
            (_GSSL("The host cert could not be found in: \n"
             "1) env. var. X509_USER_CERT\n"
             "2) /etc/grid-security/hostcert.pem\n"
             "3) $GLOBUS_LOCATION/etc/hostcert.pem\n"
             "4) $HOME/.globus/hostcert.pem\n\n"
             "The host key could not be found in:\n"
             "1) env. var. X509_USER_KEY\n"
             "2) /etc/grid-security/hostkey.pem\n"
             "3) $GLOBUS_LOCATION/etc/hostkey.pem\n"
             "4) $HOME/.globus/hostkey.pem\n\n")));
        goto done;
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_FPRINTF(
        2, (stderr, "Using x509_user_cert=%s\n      x509_user_key =%s\n",
            *host_cert , *host_key));

    result = GLOBUS_SUCCESS;

 done:
    if(result != GLOBUS_SUCCESS)
    {
        *host_cert = NULL;
        *host_key = NULL;
    }

    if(home)
    {
        free(home);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get Service Certificate and Key Filenames
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get the Service Certificate Filename based on the current user's
 * environment.  The host cert and key are searched for in the following 
 * locations (in order):
 *
 * <ol>
 * <li>X509_USER_CERT and X509_USER_KEY environment variables
 * <li>\/etc\/grid-security\/{service_name}\/{service_name}[cert|key].pem
 * <li>GLOBUS_LOCATION\/etc\/{service_name}\/{service_name}[cert|key].pem
 *     So for example, if my service was named: myservice, the location
 *     of the certificate would be: 
 *     GLOBUS_LOCATION\/etc\/myservice\/myservicecert.pem
 * <li>\\<users home\\>\/.globus\/{service_name}\/{service_name}[cert|key].pem
 * </ol>
 * 
 * @param service_name
 *        The name of the service which allows us to determine the
 *        locations of cert and key files to look for
 * @param service_cert
 *        pointer to the host certificate filename
 * @param service_key
 *        pointer to the host key filename
 *
 * @return
 *        GLOBUS_SUCCESS if the service cert and key were found, otherwise
 *        an error object identifier 
 */
globus_result_t
globus_gsi_sysconfig_get_service_cert_filename_unix(
    char *                              service_name,
    char **                             service_cert,
    char **                             service_key)
{
    char *                              home = NULL;
    char *                              env_service_cert = NULL;
    char *                              env_service_key = NULL;
    char *                              default_service_cert = NULL;
    char *                              default_service_key = NULL;
    char *                              installed_service_cert = NULL;
    char *                              installed_service_key = NULL;
    char *                              local_service_cert = NULL;
    char *                              local_service_key = NULL;
    char *                              location = NULL;
    globus_result_t                     result;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *service_cert = NULL;
    *service_key = NULL;

    if(service_name == NULL)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_INVALID_ARG,
            (_GSSL("Empty service name")));
        goto done;
    }

    /* first check environment variables for valid filenames */

    if(getenv(X509_USER_CERT) && getenv(X509_USER_KEY))
    {
        result = globus_i_gsi_sysconfig_create_cert_string(
            service_cert,
            &env_service_cert,
            "%s",
            getenv(X509_USER_CERT));

        if(result != GLOBUS_SUCCESS)
        { 
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
            goto done;
        }

        result = globus_i_gsi_sysconfig_create_key_string(
            service_key,
            &env_service_key,
            "%s",
            getenv(X509_USER_KEY));

        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
            goto done;
        }
    }

    /* now check default locations for valid filenames */
    if(!(*service_cert) && !(*service_key))
    {
        result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);

        if(result == GLOBUS_SUCCESS)
        {
            result = globus_i_gsi_sysconfig_create_cert_string(
                service_cert,
                &default_service_cert,
                "%s%s%s%s%s%s",
                X509_DEFAULT_CERT_DIR,
                FILE_SEPARATOR,
                service_name,
                FILE_SEPARATOR,
                service_name,
                X509_CERT_SUFFIX);

            if(result == GLOBUS_SUCCESS)
            { 
                result = globus_i_gsi_sysconfig_create_key_string(
                    service_key,
                    & default_service_key,
                    "%s%s%s%s%s%s",
                    X509_DEFAULT_CERT_DIR,
                    FILE_SEPARATOR,
                    service_name,
                    FILE_SEPARATOR,
                    service_name,
                    X509_KEY_SUFFIX);

                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;                
            }
        }
        else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
            home = NULL;
            goto done;
        }
    }

    /* now check intstalled location for service cert */
    if(!(*service_cert) && !(*service_key))
    {
        globus_location(&location);

        if(location)
        {
            result = globus_i_gsi_sysconfig_create_cert_string(
                service_cert,
                &installed_service_cert,
                "%s%s%s%s%s%s%s%s",
                location,
                FILE_SEPARATOR,
                X509_INSTALLED_CERT_DIR,
                FILE_SEPARATOR,
                service_name,
                FILE_SEPARATOR,
                service_name,
                X509_CERT_SUFFIX);

            if(result == GLOBUS_SUCCESS)
            { 
                result = globus_i_gsi_sysconfig_create_key_string(
                    service_key,
                    &installed_service_key,
                    "%s%s%s%s%s%s%s%s",
                    location,
                    FILE_SEPARATOR,
                    X509_INSTALLED_CERT_DIR,
                    FILE_SEPARATOR,
                    service_name,
                    FILE_SEPARATOR,
                    service_name,
                    X509_KEY_SUFFIX);
                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_KEY_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }
        }
    }

    if(!(*service_cert) && !(*service_key))
    {
        result = GLOBUS_SUCCESS;
        if(!home)
        {
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);
        }

        if(result == GLOBUS_SUCCESS)
        {
            result = globus_i_gsi_sysconfig_create_cert_string(
                service_cert,
                &local_service_cert,
                "%s%s%s%s%s%s%s",
                home,
                FILE_SEPARATOR,
                X509_LOCAL_CERT_DIR,
                FILE_SEPARATOR,
                service_name,
                FILE_SEPARATOR,
                service_name,
                X509_CERT_SUFFIX);

            if(result == GLOBUS_SUCCESS)
            { 
                result = globus_i_gsi_sysconfig_create_key_string(
                    service_key,
                    &local_service_key,
                    "%s%s%s%s%s%s%s%s",
                    home,
                    FILE_SEPARATOR,
                    X509_LOCAL_CERT_DIR,
                    FILE_SEPARATOR,
                    service_name,
                    FILE_SEPARATOR,
                    service_name,
                    X509_KEY_SUFFIX);
                if(result != GLOBUS_SUCCESS &&
                   !GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                {
                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                    goto done;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
                goto done;
            }
        }
        else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_STRING);
            home = NULL;
            goto done;            
        }
    }

    if(!(*service_cert) || !(*service_key))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_FILENAME,
            (_GSSL("\nThe service cert could not be found in: \n"
             "1) env. var. X509_USER_CERT\n"
             "2) /etc/grid-security/%s/%scert.pem\n"
             "3) $GLOBUS_LOCATION/etc/%s/%scert.pem\n"
             "4) $HOME/.globus/%s/%scert.pem\n\n"
             "The service key could not be found in:\n"
             "1) env. var. X509_USER_KEY\n"
             "2) /etc/grid-security/%s/%skey.pem\n"
             "3) $GLOBUS_LOCATION/etc/%s/%skey.pem\n"
             "4) $HOME/.globus/%s/%skey.pem\n\n"),
             service_name, service_name,
             service_name, service_name,
             service_name, service_name,
             service_name, service_name,
             service_name, service_name,
             service_name, service_name));
        goto done;
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_FPRINTF(
        2, (stderr, "Using x509_user_cert=%s\n      x509_user_key =%s\n",
            *service_cert , *service_key));

    result = GLOBUS_SUCCESS;

 done:
    if(result != GLOBUS_SUCCESS)
    {
        *service_cert = NULL;
        *service_key = NULL;
    }

    if(home)
    {
        free(home);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get Proxy Filename
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get the proxy cert filename based on the following
 * search order:
 * 
 * <ol>
 * <li> X509_USER_PROXY environment variable - This environment variable
 * is set by the at run time for the specific application.  If
 * the proxy_file_type variable is set to GLOBUS_PROXY_OUTPUT
 *  (a proxy filename for writing is requested), 
 * and the X509_USER_PROXY is set, this will be the 
 * resulting value of the user_proxy filename string passed in.  If the
 * proxy_file_type is set to GLOBUS_PROXY_INPUT and X509_USER_PROXY is 
 * set, but the file it points to does not exist, 
 * or has some other readability issues, the 
 * function will continue checking using the other methods available.
 * 
 * <li> Check the default location for the proxy file of
 * \/tmp\/x509_u\\<user_id\\> where \\<user id\\> is some unique string for
 * that user on the host 
 * </ol>
 *
 * @param user_proxy
 *        the proxy filename of the user
 * @param proxy_file_type
 *        Switch for determining whether to return a existing proxy filename or
 *        if a filename suitable for creating a proxy should be returned
 *
 * @return
 *        GLOBUS_SUCCESS or an error object identifier
 */
globus_result_t
globus_gsi_sysconfig_get_proxy_filename_unix(
    char **                             user_proxy,
    globus_gsi_proxy_file_type_t        proxy_file_type)
{
    char *                              env_user_proxy = NULL;
    char *                              env_value = NULL;
    char *                              default_user_proxy = NULL;
    globus_result_t                     result;
    char *                              user_id_string = NULL;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *user_proxy = NULL;

    if((env_value = getenv(X509_USER_PROXY)))
    {
        if(proxy_file_type == GLOBUS_PROXY_FILE_OUTPUT)
        {
            *user_proxy = strdup(env_value);
            if(*user_proxy == NULL)
            {
                result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                goto done;
            }
        }
        else
        { 
            result = globus_i_gsi_sysconfig_create_key_string(
                user_proxy,
                &env_user_proxy,
                "%s",
                env_value);
            if(result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PROXY_FILENAME);
                goto done;
            }
        }
    }

    if (!*user_proxy)
    {
        result = GLOBUS_GSI_SYSCONFIG_GET_USER_ID_STRING(&user_id_string);
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PROXY_FILENAME);
            goto done;
        }

        if(proxy_file_type == GLOBUS_PROXY_FILE_OUTPUT)
        {
            *user_proxy = globus_common_create_string(
                "%s%s%s%s",
                DEFAULT_SECURE_TMP_DIR,
                FILE_SEPARATOR,
                X509_USER_PROXY_FILE,
                user_id_string);
            if(*user_proxy == NULL)
            {
                result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                goto done;
            }
        }
        else
        {
            result = globus_i_gsi_sysconfig_create_key_string(
                user_proxy,
                &default_user_proxy,
                "%s%s%s%s",
                DEFAULT_SECURE_TMP_DIR,
                FILE_SEPARATOR,
                X509_USER_PROXY_FILE,
                user_id_string);

            if(result != GLOBUS_SUCCESS)
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PROXY_FILENAME);
                goto done;
            }
        }
    }

    if(!(*user_proxy))
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT( 
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_PROXY_FILENAME,
            (_GSSL("A file location for%s the proxy cert could not be found in: \n"
             "1) env. var. X509_USER_PROXY\n"
             "2) /tmp/x509up_u%s\n"),
             (proxy_file_type == GLOBUS_PROXY_FILE_INPUT) ? "" : " writing",
             user_id_string ? user_id_string : "NULL"));

        goto done;
    }

    result = GLOBUS_SUCCESS;

 done:
    if(result != GLOBUS_SUCCESS)
    {
        *user_proxy = NULL;
    }

    if(user_id_string)
    {
        free(user_id_string);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get Signing Policy Filename
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get the Signing Policy Filename on the current system,
 * based on the CA's subject name, and the trusted certificates
 * directory
 *
 * @param ca_name
 *        The X509 subject name of the CA to get the signing policy of.
 *        The hash of the CA is generated from this
 *
 * @param cert_dir
 *        The trusted CA certificates directory, containing the singing_policy
 *        files of the trusted CA's.
 *
 * @param signing_policy_filename
 *        The resulting singing_policy filename
 * @return 
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 */
globus_result_t
globus_gsi_sysconfig_get_signing_policy_filename_unix(
    X509_NAME *                         ca_name,
    char *                              cert_dir,
    char **                             signing_policy_filename)
{
    char *                              signing_policy = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;
    unsigned long                       hash;
    char *                              ca_cert_dir = NULL;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    *signing_policy_filename = NULL;

    if (cert_dir == NULL)
    {
        result = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&ca_cert_dir);
        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_SIGNING_POLICY);
            goto exit;
        }
    }
    else
    {
        ca_cert_dir = cert_dir;
    }

    if(ca_name == NULL)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_SIGNING_POLICY,
            (_GSSL("NULL parameter ca_name passed to: %s"), __func__));
        goto exit;
    }

    hash = X509_NAME_hash(ca_name);

    signing_policy = globus_common_create_string(
        "%s%s%08lx%s", 
        ca_cert_dir, FILE_SEPARATOR, hash, SIGNING_POLICY_FILE_EXTENSION);

    if(signing_policy == NULL)
    {
        result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
        goto exit;
    }

    result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(signing_policy);
    if(result != GLOBUS_SUCCESS)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_SIGNING_POLICY);
        goto exit;
    }

    *signing_policy_filename = signing_policy;

 exit:

    if(ca_cert_dir != NULL &&
       cert_dir == NULL)
    {
        free(ca_cert_dir);
    }

    if(signing_policy != NULL &&
       result != GLOBUS_SUCCESS)
    {
        free(signing_policy);
        *signing_policy_filename = NULL;
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get CA Cert Filenames
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Gets a list of trusted CA certificate filenames in 
 * a trusted CA certificate directory.  
 *
 * @param ca_cert_dir
 *        The trusted CA certificate directory to get the filenames from
 * @param ca_cert_list
 *        The resulting list of CA certificate filenames.  This is a
 *        a globus list structure.  
 *        @see globus_fifo_t
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */ 
globus_result_t
globus_gsi_sysconfig_get_ca_cert_files_unix(
    char *                              ca_cert_dir,
    globus_fifo_t *                     ca_cert_list)
{
    DIR *                               dir_handle = NULL;
    struct dirent *                     tmp_entry = NULL;
    int                                 file_length;
    char *                              full_filename_path = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if(!ca_cert_dir)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CA_CERT_FILENAMES,
            (_GSSL("NULL parameter ca_cert_dir passed to function: %s"),
             __func__));
        goto exit;
    }

    if(!ca_cert_list)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CA_CERT_FILENAMES,
            (_GSSL("NULL parameter ca_cert_list passed to function: %s"),
             __func__));
        goto exit;
    }

    dir_handle = globus_libc_opendir(ca_cert_dir);
    if(dir_handle == NULL)
    {
        result = globus_error_put(
            globus_error_wrap_errno_error(
                GLOBUS_GSI_SYSCONFIG_MODULE,
                errno,
                GLOBUS_GSI_SYSCONFIG_ERROR_ERRNO,
                __FILE__,
                __func__,
                __LINE__,
                "Error opening directory: %s", ca_cert_dir));
        goto exit;
    }

    while(globus_libc_readdir_r(dir_handle,&tmp_entry) == 0 &&
          tmp_entry != NULL)
    {
        file_length = strlen(tmp_entry->d_name);
        /* check the following:
         * 
         * - file length is greater than or equal to 10
         * - first 8 characters are alpha-numeric
         * - 9th character is '.'
         * - characters after the '.' are numeric
         */

        full_filename_path = 
            globus_common_create_string(
                "%s%s%s", ca_cert_dir, FILE_SEPARATOR, tmp_entry->d_name);

        if(full_filename_path == NULL)
        {
            while((full_filename_path =
                   (char *) globus_fifo_dequeue(ca_cert_list)) != NULL)
            {
                free(full_filename_path);
            }
            GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CA_CERT_FILENAMES,
                (_GSSL("Couldn't get full pathname for CA cert")));
            goto exit;
        }

        if((result = globus_gsi_sysconfig_file_exists_unix(
                full_filename_path)) == GLOBUS_SUCCESS)
        {

            if(file_length >= (X509_HASH_LENGTH + 2) &&
               (*(tmp_entry->d_name + X509_HASH_LENGTH) == '.') &&
               (strspn(tmp_entry->d_name, "0123456789abcdefABCDEF") 
                == X509_HASH_LENGTH) &&
               (strspn((tmp_entry->d_name + (X509_HASH_LENGTH + 1)), 
                       "0123456789") == (file_length - 9)))
            {
                globus_fifo_enqueue(ca_cert_list, (void *)full_filename_path);
            }
            else
            {
                free(full_filename_path);
            }
        }
        else
        {
            free(full_filename_path);
        }

        globus_free(tmp_entry);
    }

    result = GLOBUS_SUCCESS;

 exit:

    if(dir_handle != NULL)
    {
        globus_libc_closedir(dir_handle);
    }

    if(tmp_entry != NULL)
    {
	globus_libc_free(tmp_entry);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;

}

/**
 * @brief UNIX - Remove all proxies owned by current uid
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Removes all proxies (ie. all delegated and grid-proxy-init generated
 * proxies) found in the secure tmp directory that are owned by the
 * current user.
 *
 * @param default_filename
 *        The filename of the default proxy
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */ 
globus_result_t
globus_gsi_sysconfig_remove_all_owned_files_unix(
    char *                              default_filename)
{
    struct stat                         stx;
    char *                              full_filename = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;
    DIR *                               secure_tmp_dir = NULL;
    struct dirent *                     dir_entry = NULL;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    secure_tmp_dir = globus_libc_opendir(DEFAULT_SECURE_TMP_DIR);
    if(!secure_tmp_dir)
    {
        result = globus_error_put(
            globus_error_wrap_errno_error(
                GLOBUS_GSI_SYSCONFIG_MODULE,
                errno,
                GLOBUS_GSI_SYSCONFIG_ERROR_ERRNO,
                __FILE__,
                __func__,
                __LINE__,
                "Error opening directory: %s\n",
                DEFAULT_SECURE_TMP_DIR));
        goto exit;
    }

    while(globus_libc_readdir_r(secure_tmp_dir, &dir_entry) == 0 &&
          dir_entry != NULL)
    {
        if((default_filename && 
            !strcmp(dir_entry->d_name, default_filename)) ||
           !strncmp(dir_entry->d_name,
                    X509_UNIQUE_PROXY_FILE,
                    strlen(X509_UNIQUE_PROXY_FILE)))
        {
            full_filename = globus_common_create_string(
                "%s%s%s",
                DEFAULT_SECURE_TMP_DIR,
                FILE_SEPARATOR,
                dir_entry->d_name);

            if(stat(full_filename, &stx) == -1)
            {
                globus_free(dir_entry);
                continue;
            }

            RAND_add((void *) &stx, sizeof(stx), 2);

#ifdef HAVE_GETEUID
            if(stx.st_uid == geteuid())
#endif
            {
                static char             msg[65]
                    = "DESTROYED BY GLOBUS\r\n";
                int                     f = open(full_filename, O_RDWR);
                int                     size, rec, left;
                if (f) 
                {
                    size = lseek(f, 0L, SEEK_END);
                    lseek(f, 0L, SEEK_SET);
                    if (size > 0) 
                    {
                        rec = size / 64;
                        left = size - rec * 64;
                        while (rec)
                        {
                            write(f, msg, 64);
                            rec--;
                        }
                        if (left)
                        { 
                            write(f, msg, left);
                        }
                    }
                    close(f);
                }
                remove(full_filename);
            }

            free(full_filename);
        }
        globus_free(dir_entry);
    }

 exit:

    if(secure_tmp_dir != NULL)
    {
        globus_libc_closedir(secure_tmp_dir);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Check if the current user is root
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Checks whether the current user is root.
 *
 * @param is_superuser
 *        1 if the user is the superuser
 *        0 if not
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */ 
globus_result_t
globus_gsi_sysconfig_is_superuser_unix(
    int *                               is_superuser)
{
#ifdef HAVE_GETEUID
    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if(geteuid() == 0)
    {
        *is_superuser = 1;
    }
    else
    {
        *is_superuser = 0;
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return GLOBUS_SUCCESS;
#else
    *is_superuser = 1;
    return GLOBUS_SUCCESS;
#endif
}

/**
 * @brief UNIX - Get the path and file name of the grid map file
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get the path and file name of the grid map file.
 *
 * @param filename
 *        Contains the location of the grid map file upon successful return
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */ 
globus_result_t
globus_gsi_sysconfig_get_gridmap_filename_unix(
    char **                             filename)
{
    char *                              home_dir = NULL;
    char *                              gridmap_env = NULL;
    char *                              gridmap_filename = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if(((gridmap_env = (char *) getenv("GRIDMAP"))   != NULL) ||
       ((gridmap_env = (char *) getenv("GLOBUSMAP")) != NULL) ||
       ((gridmap_env = (char *) getenv("globusmap")) != NULL) ||
       ((gridmap_env = (char *) getenv("GlobusMap")) != NULL))
    {
        gridmap_filename = globus_common_create_string(
            "%s",
            gridmap_env);
        if(!gridmap_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }
    }

#ifdef HAVE_GETEUID
    if(!gridmap_filename)
    {
        if(geteuid() == 0)
        {
            /* being run as root */

            gridmap_filename = globus_common_create_string(
                "%s",
                DEFAULT_GRIDMAP);
            if(!gridmap_filename)
            {
                GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                goto exit;
            }
        }
        else
        {
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home_dir);

            if(result == GLOBUS_SUCCESS)
            {
                gridmap_filename = globus_common_create_string(
                    "%s%s%s",
                    home_dir,
                    FILE_SEPARATOR,
                    LOCAL_GRIDMAP);
                if(!gridmap_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }
            }
            else if(!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GRIDMAP_FILENAME);
                goto exit;                
            }
        }
    }
#endif

    if(!gridmap_filename)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GRIDMAP_FILENAME,
            (_GSSL("A valid gridmap file could not be found.")));
        goto exit;
    }

    *filename = gridmap_filename;

 exit:

    if(home_dir != NULL)
    {
        free(home_dir);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get the path of authorization callback configuration
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get the path and file name of the authorization callback
 * configuration file 
 *
 * @param filename
 *        Contains the location of the authorization callback configuration
 *        file upon successful return 
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */ 
globus_result_t
globus_gsi_sysconfig_get_authz_conf_filename_unix(
    char **                             filename)
{
    char *                              home_dir = NULL;
    char *                              authz_env = NULL;
    char *                              authz_filename = NULL;
    char *                              location = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if((authz_env = (char *) getenv("GSI_AUTHZ_CONF"))   != NULL)
    {
        authz_filename = globus_common_create_string(
            "%s",
            authz_env);
        if(!authz_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

        result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
            authz_filename);

        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_FILENAME);
            goto exit;
        }
    }
    else
    { 
        authz_filename = globus_common_create_string(
            "%s",
            DEFAULT_AUTHZ_FILE);
        if(!authz_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

        result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
            authz_filename);

        if(result != GLOBUS_SUCCESS)
        {
            if(GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                free(authz_filename);
                authz_filename = NULL;
            }
            else
            { 
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_FILENAME);
                goto exit;
            }
        }

        if(authz_filename == NULL)
        {
            globus_location(&location);

            if(location)
            {
                authz_filename = globus_common_create_string(
                    "%s%s%s",
                    location,
                    FILE_SEPARATOR,
                    INSTALLED_AUTHZ_FILE);
                if(!authz_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }

                result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
                    authz_filename);

                if(result != GLOBUS_SUCCESS)
                {
                    if(GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                    {
                        free(authz_filename);
                        authz_filename = NULL;
                    }
                    else
                    { 
                        GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                            result,
                            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_FILENAME);
                        goto exit;
                    }
                }
            }
        }

        if(authz_filename == NULL)
        {
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home_dir);

            if(result == GLOBUS_SUCCESS)
            {
                authz_filename = globus_common_create_string(
                    "%s%s%s",
                    home_dir,
                    FILE_SEPARATOR,
                    LOCAL_AUTHZ_FILE);
                if(!authz_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }
                result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
                    authz_filename);

                if(result != GLOBUS_SUCCESS)
                {
                    free(authz_filename);
                    authz_filename = NULL;

                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_FILENAME);
                    goto exit;
                }

            }
            else
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_FILENAME);
                goto exit;
            }
        }
    }

    *filename = authz_filename;
    authz_filename = NULL;

 exit:

    if(home_dir != NULL)
    {
        free(home_dir);
    }

    if(authz_filename != NULL)
    {
        free(authz_filename);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get the path and file name of the authorization callback configuration file 
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get the path and file name of the authorization callback
 * configuration file 
 *
 * @param filename
 *        Contains the location of the authorization callback configuration
 *        file upon successful return 
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */ 
globus_result_t
globus_gsi_sysconfig_get_authz_lib_conf_filename_unix(
    char **                             filename)
{
    char *                              home_dir = NULL;
    char *                              authz_lib_env = NULL;
    char *                              authz_lib_filename = NULL;
    char *                              location = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if((authz_lib_env = (char *) getenv("GSI_AUTHZ_LIB_CONF"))   != NULL)
    {
        authz_lib_filename = globus_common_create_string(
            "%s",
            authz_lib_env);
        if(!authz_lib_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

        result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(authz_lib_filename);

        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_LIB_FILENAME);
            goto exit;
        }
    }
    else
    { 
        authz_lib_filename = globus_common_create_string(
            "%s%s_%s%s",
	    DEFAULT_AUTHZ_LIB_FILE_DIR,
	    DEFAULT_AUTHZ_LIB_FILE_BASE,
	    flavor,
            DEFAULT_AUTHZ_LIB_FILE_EXTENSION);
        if(!authz_lib_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

        result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(authz_lib_filename);

        if(result != GLOBUS_SUCCESS)
        {
            if(GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                free(authz_lib_filename);
                authz_lib_filename = NULL;
            }
            else
            { 
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_LIB_FILENAME);
                goto exit;
            }
        }

        if(authz_lib_filename == NULL)
        {
            globus_location(&location);

            if(location)
            {
                authz_lib_filename = globus_common_create_string(
                    "%s%s%s%s_%s%s",
                    location,
                    FILE_SEPARATOR,
		    INSTALLED_AUTHZ_LIB_DIR,
		    DEFAULT_AUTHZ_LIB_FILE_BASE,
		    flavor,
		    DEFAULT_AUTHZ_LIB_FILE_EXTENSION);
                if(!authz_lib_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }

		result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(authz_lib_filename);

                if(result != GLOBUS_SUCCESS)
                {
                    if(GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                    {
                        free(authz_lib_filename);
                        authz_lib_filename = NULL;
                    }
                    else
                    { 
                        GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                            result,
                            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_LIB_FILENAME);
                        goto exit;
                    }
                }
            }
        }

        if(authz_lib_filename == NULL)
        {
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home_dir);

            if(result == GLOBUS_SUCCESS)
            {
                authz_lib_filename = globus_common_create_string(
                    "%s%s%s_%s%s",
                    home_dir,
                    FILE_SEPARATOR,
                    HOME_AUTHZ_LIB_FILE_BASE,
		    flavor,
		    DEFAULT_AUTHZ_LIB_FILE_EXTENSION);
                if(!authz_lib_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }
                result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(authz_lib_filename);

                if(result != GLOBUS_SUCCESS)
                {
                    free(authz_lib_filename);
                    authz_lib_filename = NULL;

                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_LIB_FILENAME);
                    goto exit;
                }

            }
            else
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_AUTHZ_LIB_FILENAME);
                goto exit;
            }
        }
    }

    *filename = authz_lib_filename;
    authz_lib_filename = NULL;

 exit:

    if(home_dir != NULL)
    {
        free(home_dir);
    }

    if(authz_lib_filename != NULL)
    {
        free(authz_lib_filename);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get the path and file name of the gaa configuration file 
 * @ingroup globus_gsi_sysconfig_unix
 * @details
 * Get the path and file name of the GAA configuration file 
 *
 * @param filename
 *        Contains the location of the GAA callback configuration
 *        file upon successful return 
 * @return
 *        GLOBUS_SUCCESS if no error occurred, otherwise an error object ID
 *        is returned
 */ 
globus_result_t
globus_gsi_sysconfig_get_gaa_conf_filename_unix(
    char **                             filename)
{
    char *                              home_dir = NULL;
    char *                              gaa_env = NULL;
    char *                              gaa_filename = NULL;
    char *                              location = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if((gaa_env = (char *) getenv("GSI_GAA_CONF"))   != NULL)
    {
        gaa_filename = globus_common_create_string(
            "%s",
            gaa_env);
        if(!gaa_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

	result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
            gaa_filename);

        if(result != GLOBUS_SUCCESS)
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GAA_FILENAME);
            goto exit;
        }
    }
    else
    { 
        gaa_filename = globus_common_create_string(
            "%s",
            DEFAULT_GAA_FILE);
        if(!gaa_filename)
        {
            GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto exit;
        }

	result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
            gaa_filename);

        if(result != GLOBUS_SUCCESS)
        {
            if(GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
            {
                free(gaa_filename);
                gaa_filename = NULL;
            }
            else
            { 
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GAA_FILENAME);
                goto exit;
            }
        }

        if(gaa_filename == NULL)
        {
            globus_location(&location);

            if(location)
            {
                gaa_filename = globus_common_create_string(
                    "%s%s%s",
                    location,
                    FILE_SEPARATOR,
                    INSTALLED_GAA_FILE);
                if(!gaa_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }

		result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(
                    gaa_filename);

                if(result != GLOBUS_SUCCESS)
                {
                    if(GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result))
                    {
                        free(gaa_filename);
                        gaa_filename = NULL;
                    }
                    else
                    { 
                        GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                            result,
                            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GAA_FILENAME);
                        goto exit;
                    }
                }
            }
        }

        if(gaa_filename == NULL)
        {
            result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home_dir);

            if(result == GLOBUS_SUCCESS)
            {
                gaa_filename = globus_common_create_string(
                    "%s%s%s",
                    home_dir,
                    FILE_SEPARATOR,
                    LOCAL_GAA_FILE);
                if(!gaa_filename)
                {
                    GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
                    goto exit;
                }
                result = globus_gsi_sysconfig_check_certfile_unix(
                    gaa_filename);

                if(result != GLOBUS_SUCCESS)
                {
                    free(gaa_filename);
                    gaa_filename = NULL;

                    GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                        result,
                        GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GAA_FILENAME);
                    goto exit;
                }

            }
            else
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_GAA_FILENAME);
                goto exit;
            }
        }
    }

    *filename = gaa_filename;
    gaa_filename = NULL;

 exit:

    if(home_dir != NULL)
    {
        free(home_dir);
    }

    if(gaa_filename != NULL)
    {
        free(gaa_filename);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}
#endif /* done defining *_unix functions */

/**
 * @brief Get Unique Proxy Filename
 * @ingroup globus_gsi_sysconfig_shared
 * @details
 * Get a unique proxy cert filename.  This is mostly used
 * for delegated proxy credentials.  Each filename returned
 * is going to be unique for each time the function is called.
 * 
 * @param unique_filename
 *        the unique filename for a delegated proxy cert
 *
 * @return
 *        GLOBUS_SUCCESS or an error object identifier
 */
globus_result_t
globus_gsi_sysconfig_get_unique_proxy_filename(
    char **                             unique_filename)
{
    globus_result_t                     result;
    char *                              proc_id_string = NULL;
    char                                unique_tmp_name[L_tmpnam];
    char *                              unique_postfix = NULL;
    static int                          i = 0;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    memset(unique_tmp_name, 0, L_tmpnam);

    *unique_filename = NULL;

    result = GLOBUS_GSI_SYSCONFIG_GET_PROC_ID_STRING(&proc_id_string);
    if(result != GLOBUS_SUCCESS)
    {
        proc_id_string = NULL;
        GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_DELEG_FILENAME);
        goto done;
    }

    if(tmpnam(unique_tmp_name) == NULL)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_DELEG_FILENAME,
            (_GSSL("Could not get a unique filename for the temporary proxy cert")));
        goto done;
    }

    unique_postfix = strrchr(unique_tmp_name, FILE_SEPARATOR[0]);
    if (unique_postfix == NULL)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_DELEG_FILENAME,
            (_GSSL("Could not get a unique filename for the temporary proxy cert")));
        goto done;
    }
    ++unique_postfix;

    do
    {
        *unique_filename = globus_common_create_string("%s%s%s%s.%s.%d", DEFAULT_SECURE_TMP_DIR,
                                                       FILE_SEPARATOR, X509_UNIQUE_PROXY_FILE,
                                                       proc_id_string, unique_postfix, ++i);

        if(*unique_filename == NULL)
        {
            result = GLOBUS_GSI_SYSTEM_CONFIG_MALLOC_ERROR;
            goto done;
        }

        result = GLOBUS_GSI_SYSCONFIG_SET_KEY_PERMISSIONS(*unique_filename);

        if(result != GLOBUS_SUCCESS)
        {
            free(*unique_filename);
            if(i > 25)
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_DELEG_FILENAME);
                goto done;
            }
        }
        else
        {
            break;
        }
    }
    while(1);

    result = GLOBUS_SUCCESS;

 done:

    if(proc_id_string != NULL)
    {
        free(proc_id_string);
    }

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;
    return result;
}

/**
 * @brief UNIX - Get vhost credential dir
 * @ingroup globus_gsi_sysconfig_shared
 * @details
 *     Get the Trusted Certificate Directory containing the trusted Certificate
 *     Authority certificates.  This directory is determined in the order shown
 *     below.  Failure in one method results in attempting the next.
 *
 * -# `$X509_VHOST_CRED_DIR` environment variable
 * -# `$GLOBUS_LOCATION/etc/vhosts`
 * -# `/etc/grid-security/vhosts`
 *
 * If this function is successful, it modifies **dir_name_out** to point to
 * the directory path. The caller must free this value.
 */
globus_result_t
globus_gsi_sysconfig_get_vhost_cred_dir(
    /** [out] Pointer to set to the vhosts directory */
    char                              **dir_name_out)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char *                              dir_name = NULL;
    char *                              env_value = NULL;
    char *                              env_dir_name = NULL;
    char *                              location = NULL;
    char *                              installed_dir_name = NULL;
    char *                              local_dir_name = NULL;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_ENTER;

    if (dir_name_out == NULL)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CA_CERT_FILENAMES,
            (_GSSL("NULL parameter dir_name_out passed to function: %s"),
             __func__));
        return result;
    }

    /* first check environment variables for a valid path */
    if ((env_value = getenv(X509_VHOST_CRED_DIR)) != NULL)
    {
        result = globus_i_gsi_sysconfig_create_cert_dir_string(
            &dir_name,
            &env_dir_name,
            "%s",
            env_value);

        if (result != GLOBUS_SUCCESS
            && (!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result)))
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
            goto done;
        }
    }

    /* now check installed location for service cert */
    if (dir_name == NULL)
    {
        globus_location(&location);

        if (location != NULL)
        {
            result = globus_i_gsi_sysconfig_create_cert_dir_string(
                &dir_name,
                &installed_dir_name,
                "%s%s%s%s%s",
                location,
                FILE_SEPARATOR,
                X509_INSTALLED_CERT_DIR,
                FILE_SEPARATOR,
                "vhosts");

            if (result != GLOBUS_SUCCESS
                && (!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result)))
            {
                GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                    result,
                    GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
                goto done;
            }
        }
    }

    /* Now check local location */
    if (dir_name == NULL)
    {
        result = globus_i_gsi_sysconfig_create_cert_dir_string(
            &dir_name,
            &local_dir_name,
            "%s%s%s",
            X509_DEFAULT_CERT_DIR,
            FILE_SEPARATOR,
            "vhosts");

        if (result != GLOBUS_SUCCESS
            && (!GLOBUS_GSI_SYSCONFIG_FILE_DOES_NOT_EXIST(result)))
        {
            GLOBUS_GSI_SYSCONFIG_ERROR_CHAIN_RESULT(
                result,
                GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_DIR);
            goto done;
        }
    }

    if (dir_name == NULL)
    {
        GLOBUS_GSI_SYSCONFIG_ERROR_RESULT(
            result,
            GLOBUS_GSI_SYSCONFIG_ERROR_GETTING_CERT_FILENAME,
            (_GSSL("\nThe vhost cert dir could not be found in: \n"
             "1) env. var. X509_VHOST_CRED_DIR\n"
             "2) $GLOBUS_LOCATION%s%s%s%s\n"
             "3) %s%s%s\n"),
             /* 2 */
             FILE_SEPARATOR,
             X509_INSTALLED_CERT_DIR,
             FILE_SEPARATOR,
             "vhosts",
             /* 3 */
             X509_DEFAULT_CERT_DIR,
             FILE_SEPARATOR,
             "vhosts"));
        goto done;
    }

done:
    free(location);

    *dir_name_out = dir_name;

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_FPRINTF(
        2, (stderr, "Using x509_vhost_cred_diR=%s\n",
            dir_name?dir_name : "NULL"));

    GLOBUS_I_GSI_SYSCONFIG_DEBUG_EXIT;

    /**
     * @return
     * If successful, this function returns GLOBUS_SUCCESS.
     * Otherwise, this function returns an error result.
     */
    return result;
}
/* globus_gsi_sysconfig_get_vhost_cred_dir() */


/* 
 *  Windows Default Directory And File Routines
 */
#ifdef WIN32

const char *win32_secure_path(void)
{
    static char                         buffer[MAX_PATH+1] = {0};
    int rc;

    if (buffer[0] == 0)
    {
        rc = GetTempPath((DWORD) sizeof(buffer), buffer);

        if (rc == 0)
        {
            strcpy(buffer, WIN32_FALLBACK_PATH);
        }
        else
        {
            buffer[strlen(buffer)-1] = 0;
        }
    }
    return buffer;
}


/*--------------------------------------------------*/
/* Get Globus Location Or Current Working Directory */
const char *win32_cwd(void)
{
    char *                              location = NULL;
    char *                              tmp_path        = NULL;
    char *                              temp_path       = NULL;
    char *                              cwd             = NULL;
    static char                         buffer[MAX_PATH];

    /* Collect environment all variables we might need */    
    tmp_path        = getenv("TMP");
    temp_path       = getenv("TEMP");
    globus_location(&location);
    cwd = _getcwd(buffer,sizeof(buffer) - 1);

    if(location)
    {
        return location;
    }
    else if(cwd) 
    {
        return buffer;
    }
    /* Use $TMP */      
    else if(tmp_path) 
    {
        return tmp_path;
    }
    /* Use $TEMP */      
    else if(temp_path) 
    {
        return temp_path;
    }
    /* Fallback, use c:\temp */
    else
    {
        return WIN32_FALLBACK_PATH;
    }
}

/* Relative to Current Working Directory */
const char *x509_installed_trusted_cert_dir(void)
{
    static char                         buffer[MAX_PATH];
    sprintf(buffer,"%s%s",win32_cwd(),"\\share\\certificates");
    return buffer;
}

/* Relative to Current Working Directory */
const char *x509_installed_cert_dir(void)
{
    static char                         buffer[MAX_PATH];
    sprintf(buffer,"%s%s",win32_cwd(),"\\etc");
    return buffer;
}

/* Relative to Current Working Directory */
const char *installed_gridmap(void)
{
    static char                         buffer[MAX_PATH];
    sprintf(buffer,"%s%s",win32_cwd(),"\\etc\\grid-mapfile");
    return buffer;
}

/* Relative to Current Working Directory */
const char *installed_authz_file(void)
{
    static char                         buffer[MAX_PATH];
    sprintf(buffer,"%s%s",win32_cwd(),"\\etc\\gsi-authz.conf");
    return buffer;
}

/* Relative to Current Working Directory */
const char *default_gaa_file(void)
{
    static char                         buffer[MAX_PATH];
    sprintf(buffer,"%s%s",win32_cwd(),"\\etc\\grid-security\\gsi-gaa.conf");
    return buffer;
}


/*---------------------------*/
/* Get Windows etc Directory */
const char *win32_etc(void)
{
    char *                              system_root     = NULL;
    char *                              tmp_path        = NULL;
    char *                              temp_path       = NULL;
    char *                              cwd             = NULL;
    static char                         buffer[MAX_PATH];

    system_root     = getenv("SystemRoot");
    if(system_root)
    {
        sprintf(buffer,"%s\\system32\\drivers\\etc",system_root);
        return buffer;
    }
    else
    {
        sprintf(buffer,"c:\\winnt\\system32\\drivers\\etc",system_root);
        return buffer;
    }
}

/* Relative To etc Directory */
const char *x509_default_trusted_cert_dir(void)
{
    static char                         buffer[MAX_PATH];
    sprintf(buffer,"%s%s",win32_etc(),"\\grid-security\\certificates");
    return buffer;
}

/* Relative To etc Directory */
const char *x509_default_cert_dir(void)
{
    static char                         buffer[MAX_PATH];
    sprintf(buffer,"%s%s",win32_etc(),"\\grid-security");
    return buffer;
}

/* Relative To etc Directory */
const char *default_gridmap(void)
{
    static char                         buffer[MAX_PATH];
    sprintf(buffer,"%s%s",win32_etc(),"\\grid-security\\grid-mapfile");
    return buffer;
}

/* Relative To etc Directory */
const char *default_authz_file(void)
{
    static char                         buffer[MAX_PATH];
    sprintf(buffer,"%s%s",win32_etc(),"\\grid-security\\gsi-auth.conf");
    return buffer;
}

#endif  /* WIN32 */
