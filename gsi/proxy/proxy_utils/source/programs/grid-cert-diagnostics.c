/*
 * Copyright 1999-2015 University of Chicago
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
#include "globus_gsi_credential.h"
#include "globus_gss_assist.h"
#include "globus_openssl.h"

#include <openssl/bn.h>
#include <openssl/x509v3.h>

#ifdef _WIN32
#include <getopt.h>
#endif

#if OPENSSL_VERSION_NUMBER < 0x10100000L

#define EVP_PKEY_get0_RSA(k) (k)->pkey.rsa

static
void
RSA_get0_key(const RSA *r, const BIGNUM **n, const BIGNUM **e, const BIGNUM **d)
{
    if (n != NULL)
    {
        *n = r->n;
    }
    if (e != NULL)
    {
        *e = r->e;
    }
    if (d != NULL)
    {
        *d = r->d;
    }
}
#endif

static gss_OID_desc                    *GSS_C_NT_HOST_IP = &(gss_OID_desc) {
        10, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x02"
};
static gss_OID_desc                    *GSS_C_NT_X509 = &(gss_OID_desc) {
        10, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x03"
    };
static
void
environment_check(void);

static
void
directory_check(void);

static
void
time_check(void);

static
void
default_cred_check(void);

static
void
cert_check(const char *cert_to_check, const char * cert_check_name);

static
void
check_trusted_certs(void);

static
void
check_service(const char *service);

static
void
check_gridftp(char *service);

static
void
base64_encode(
    const char                         *input,
    size_t                              input_len,
    char                              **output,
    size_t                             *output_len);

static
void
base64_decode(
    char                               *input,
    size_t                              input_len,
    char                              **output,
    size_t                             *output_len);

static
void
check_service_cert_chain(
    const char                         *host_string,
    X509                               *cert,
    STACK_OF(X509)                     *cert_chain);

static
int
get_tz_offset(
    const struct tm                    *epoch,
    intmax_t                           *offset);

static
globus_result_t
setup_callback_data(
    globus_gsi_callback_data_t         *callback_data,
    globus_bool_t                       check_policy_for_self_signed);

static
char *
indent_string(const char * str);

int
main(int argc, char * argv[])
{
    int                                 rc;
    globus_module_descriptor_t *        failed_module;
    globus_bool_t                       personal = GLOBUS_FALSE;
    int                                 ch;
    globus_bool_t                       do_time_check = GLOBUS_FALSE;
    char *                              cert_to_check = NULL;
    char *                              cert_check_name = NULL;
    char *                              service_to_check = NULL;
    char *                              gridftp_to_check = NULL;
    globus_module_descriptor_t *        modules[] =
    {
        GLOBUS_COMMON_MODULE,
        GLOBUS_GSI_SYSCONFIG_MODULE,
        GLOBUS_GSI_CREDENTIAL_MODULE,
        GLOBUS_GSI_CALLBACK_MODULE,
        GLOBUS_OPENSSL_MODULE,
        NULL
    };

    while ((ch = getopt(argc, argv, "phc:ns:g:m:H:")) != -1)
    {
        switch (ch)
        {
        case 'p':
            personal = GLOBUS_TRUE;
            break;
        case 'c':
            cert_to_check = strdup(optarg);
            break;
        case 'H':
            cert_check_name = strdup(optarg);
            break;
        case 'n':
            do_time_check = GLOBUS_TRUE;
        case 's':
            service_to_check = strdup(optarg);
            break;
        case 'g':
            gridftp_to_check = strdup(optarg);
            break;
        case 'm':
            if ((strcmp(optarg, "STRICT_GT2") == 0) ||
                (strcmp(optarg, "STRICT_RFC2818") == 0) ||
                (strcmp(optarg, "HYBRID") == 0))
            {
                globus_libc_setenv(
                        "GLOBUS_GSSAPI_NAME_COMPATIBILITY",
                        optarg,
                        1);
            }
            else
            {
                printf("Unknown name compatibility mode '%s'\n", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        default:
        case 'h':
            printf("Usage: %s [OPTIONS]\n\n"
                   "Print diagnostic information about certificates and the GSI environment.\n\n"
                   "OPTIONS:\n"
                   "  %-27s%s\n"
                   "  %-27s%s\n"
                   "  %-27s%s\n"
                   "  %-27s%s\n"
                   "  %-27s%s\n"
                   "  %-27s%s\n"
                   "  %-27s%s\n"
                   "  %-27s%s\n"
                   "  %-27s%s\n"
                   "  %-27s%s\n"
                   "  %-27s%s\n"
                   "  %-27s%s\n",
                   strrchr(argv[0], '/') != NULL
                    ? strrchr(argv[0], '/')+1 : argv[0],
                   "-p", "Perform checks on use certificates [default: no]",
                   "-h", "Print this help message",
                   "-n", "Enable check for time synchronization",
                   "-c CERT | -c -", "Check the validity of the certificate in the",
                   "", "file CERT, '-' for standard input [default: none]",
                   "-H HOSTNAME", "When checking a cert with -c, see if its",
                   "", "name matches HOSTNAME",
                   "-m NAME-MODE", "GSSAPI name comparison mode {STRICT_GT2, HYBRID, STRICT_RFC2818} [HYBRID]",
                   "-s HOST[:PORT]",
                   "Contact the service at HOST:PORT and check its",
                   "", "certificate for validity [default: none]",
                   "-g  HOST[:PORT]",
                   "Contact the GridFTP service at HOST:PORT and",
                   "", "check its certificate validity [default: none]");
            exit(1);
        }
    }

    rc = globus_module_activate_array(modules, &failed_module);
    if (rc != GLOBUS_SUCCESS)
    {
        printf("Internal error: error activating %s: %d\n",
                failed_module->module_name, rc);
        goto out;
    }

    environment_check();

    directory_check();

    if (do_time_check)
    {
        time_check();
    }

    if (personal)
    {
        default_cred_check();
    }

    if (cert_to_check)
    {
        cert_check(cert_to_check, cert_check_name);
    }

    check_trusted_certs();

    if (service_to_check)
    {
        if (strchr(service_to_check, ':') == NULL)
        {
            service_to_check = globus_common_create_string(
                    "%s:443",
                    service_to_check);
        }
        check_service(service_to_check);
    }
    if (gridftp_to_check)
    {
        if (strchr(gridftp_to_check, ':') == NULL)
        {
            gridftp_to_check = globus_common_create_string("%s:2811",
                    gridftp_to_check);
        }
        check_gridftp(gridftp_to_check);
    }

out:
    globus_module_deactivate_all();

    return 0;
}

static
void
environment_check(void)
{
    char                               *home,
                                       *location,
                                       *cert_dir,
                                       *cert,
                                       *key,
                                       *gridmap;

    printf("Checking Environment Variables\n"
           "==============================\n");

    printf("Checking if HOME is set... ");
    home = getenv("HOME");
    printf("%s\n", home ? home : "no");
    printf("Checking if GLOBUS_LOCATION is set... ");
    location = getenv("GLOBUS_LOCATION");
    printf("%s\n", location ? location : "no");
    if (!location)
    {
        printf("Checking for default GLOBUS_LOCATION... ");
        globus_location(&location);
        printf("%s\n", location ? location : "no");
    }

    printf("Checking if X509_CERT_DIR is set... ");
    cert_dir = getenv("X509_CERT_DIR");
    printf("%s\n", cert_dir ? cert_dir : "no");
    printf("Checking if X509_USER_CERT is set... ");
    cert = getenv("X509_USER_CERT");
    printf("%s\n", cert ? cert : "no");
    printf("Checking if X509_USER_KEY is set... ");
    key = getenv("X509_USER_KEY");
    printf("%s\n", key ? key : "no");
    printf("Checking if X509_USER_PROXY is set... ");
    key = getenv("X509_USER_PROXY");
    printf("%s\n", key ? key : "no");
    printf("Checking if GRIDMAP is set... ");
    gridmap = getenv("GRIDMAP");
    printf("%s\n", gridmap ? gridmap : "no");
}

static
void
directory_check(void)
{
    globus_result_t                     result;
    char                               *cert_dir,
                                       *home,
                                       *gridmap;

    printf("\nChecking Security Directories\n"
           "=======================\n");
    printf("Determining trusted cert path... ");
    result = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&cert_dir);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s",
               indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
    }
    else
    {
        printf("%s\n", cert_dir);
    }

    printf("Checking for cog.properties... ");
    result = GLOBUS_GSI_SYSCONFIG_GET_HOME_DIR(&home);
    if (result == GLOBUS_SUCCESS)
    {
        char * cog_properties_path = globus_common_create_string(
                "%s/.globus/cog.properties",
                home);
        if (cog_properties_path == NULL)
        {
            printf("failed\n");
        }
        else if (access(cog_properties_path, F_OK) == 0)
        {
            printf("found\n"
"    WARNING: If the cog.properties file contains security properties, \n"
"             Java apps will ignore the security paths described in the GSI\n"
"             documentation\n");
        }
        else
        {
            printf("not found\n");
        }
    }
    else
    {
        printf("failed\n%s\n",
               indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
    }

    printf("Checking for default gridmap location... ");
    result = GLOBUS_GSI_SYSCONFIG_GET_GRIDMAP_FILENAME(&gridmap);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
    }
    else
    {
        printf("%s\n", gridmap);
    }

    printf("Checking if default gridmap exists... ");
    result = GLOBUS_GSI_SYSCONFIG_FILE_EXISTS(gridmap);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
    }
    else
    {
        printf("yes\n");
    }

}
/* directory_check() */

static
void
time_check(void)
{
    struct addrinfo                    *ai,
                                       *aip;
    int                                 rc;
    int                                 sfd = -1;
    unsigned char                       buf[4];
    intmax_t                            delta;
    time_t                              now;
    struct tm                           unix_epoch_tm;
    time_t                              unix_epoch_time;
    intmax_t                            tz_off;
    intmax_t                            seconds_since_unix_epoch;
    intmax_t                            local_seconds_since_unix_epoch ;

    printf("\nChecking clock synchronization\n"
            "=============================\n");

    rc = getaddrinfo("time.nist.gov", "time",
        &(const struct addrinfo) {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_DGRAM,
            .ai_protocol = 0,
        }, &ai);
    if (rc != 0)
    {
        printf("WARNING: Unable to resolve time.nist.gov:time: %s",
                gai_strerror(rc));
        goto resolv_fail;
    }

    for (aip = ai; aip != NULL; aip = aip->ai_next)
    {
        sfd = socket(aip->ai_family, aip->ai_socktype, aip->ai_protocol);

        if (sfd == -1)
        {
            continue;
        }
        rc = connect(sfd, aip->ai_addr, aip->ai_addrlen);
        if (rc == 0)
        {
            break;
        }
        else
        {
            close(sfd);
            sfd = -1;
        }
    }

    if (sfd == -1)
    {
        printf("WARNING: Unable to create socket... skipping sync test\n");
        goto connect_fail;
    }
    rc = setsockopt(
            sfd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            & (struct timeval) { .tv_sec = 5 },
            sizeof(struct timeval));
    if (rc < 0)
    {
        printf("WARNING: Unable to set rcv timeout\n");
        goto recvtimeout_fail;
    }
    rc = send(sfd, buf, 0, 0);
    if (rc < 0)
    {
        perror("WARNING: Unable to contact time.nist.gov\n");
        goto sendto_fail;
    }
    now = time(NULL);
    rc = recv(sfd, buf, 4, 0);
    if (rc < 4)
    {
        printf("WARNING: Unparsable response from time.nist.gov\n");
        goto recv_fail;
    }

    /* Time protocol (RFC 868) sends 4 byte network byte order seconds
     * since 1900-01-01 UTC.
     *
     * . Create a time_t using mktime from the UNIX epoch, since the
     *   delta between that epoch and the time protocol 0 value is defined
     *   in RFC 868 and some mktime functions can't handle dates before 1901
     * . Normalize that time struct using localtime, which populates any
     *   non-standardized zone info in a struct tm
     * . Parse the zone offset using strftime()
     * . Compute the difference between now and the unix epoch using
     *   difftime, adjusting for the time zone offset of the epoch
     * . Compare the locally computed seconds since the unix epoch with
     *   the returned time epoch.
     */
    unix_epoch_tm = (struct tm) {
        .tm_mday = 1,
        .tm_mon = 0,
        .tm_year = 70,
        .tm_isdst = -1
    };
    unix_epoch_time = mktime(&unix_epoch_tm);
    if (unix_epoch_time == (time_t) -1)
    {
        printf("WARNING: Unable to determine time\n");
        goto mktime_fail;
    }
    unix_epoch_tm = *localtime(&unix_epoch_time);
    rc = get_tz_offset(&unix_epoch_tm, &tz_off);
    if (rc != 0)
    {
        printf("WARNING: Unable to determine time\n");
        goto tz_offset_fail;
    }

    seconds_since_unix_epoch = ((intmax_t) (((uint32_t) buf[0]) << 24)
                     + (((uint32_t) buf[1]) << 16)
                     + (((uint32_t) buf[2]) << 8)
                     + (((uint32_t) buf[3]))) - INTMAX_C(2208988800);

    local_seconds_since_unix_epoch = (intmax_t)
            difftime(now, unix_epoch_time) - tz_off;

    delta = imaxabs(local_seconds_since_unix_epoch - seconds_since_unix_epoch);

    if (delta > 1)
    {
        printf("WARNING: clock skew %"PRIdMAX" seconds\n", delta);
        goto skew_fail;
    }

    printf("ok (clock skew %"PRIdMAX" seconds)\n", delta);

skew_fail:
tz_offset_fail:
mktime_fail:
recv_fail:
sendto_fail:
recvtimeout_fail:
    close(sfd);
connect_fail:
    freeaddrinfo(ai);
resolv_fail:
    return;
}
/* time_check() */

static
void
default_cred_check(void)
{
    globus_result_t                     result;
    globus_gsi_cred_handle_t            handle = NULL;
    char                               *cert, *key;
    char                               *p = NULL;
    char                               *subject_name = NULL;
    X509                               *x509_cert = NULL;
    EVP_PKEY                           *privkey = NULL,
                                       *pubkey = NULL;
    globus_gsi_callback_data_t          callback_data = NULL;
    const BIGNUM                       *pub_n = NULL;
    const BIGNUM                       *priv_n = NULL;
    char                               *local_user = NULL;
    int                                 rc;
    int                                 key_type = 0;

    result = globus_gsi_cred_handle_init(
            &handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        printf("Internal error: initializing credential handle, skipping "
               "rest of credential checks\n");
        goto out;
    }

    printf("\nChecking Default Credentials\n"
           "============================\n");
    printf("Determining certificate and key file names... ");
    result = GLOBUS_GSI_SYSCONFIG_GET_USER_CERT_FILENAME(&cert, &key);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s",
               indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
        printf("Skipping rest of credential checks\n");
        goto out;
    }
    else if (cert == NULL || key == NULL)
    {
        printf("failed\nSkipping rest of credential checks\n");
        goto out;
    }

    printf("Certificate Path: \"%s\"\n", cert);
    printf("Key Path: \"%s\"\n", key);

    if (!strncmp(cert, "SC:", 3))
    {
        EVP_set_pw_prompt("Enter card pin:");
    }
    else
    {
        EVP_set_pw_prompt("Enter GRID pass phrase for this identity: ");
    }

    if ((p = strrchr(cert, '.')) != NULL && !strcmp(p, ".p12"))
    {
        printf("Reading pkcs12 credentials\n");
        result = globus_gsi_cred_read_pkcs12(handle, cert);
    }
    else
    {
        printf("Reading certificate... ");
        result = globus_gsi_cred_read_cert(handle, cert);
    }

    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
        goto out;
    }
    else
    {
        printf("ok\n");
    }

    if ((p = strrchr(key, '.')) == NULL || strcmp(p, ".p12"))
    {
        printf("Reading private key...\n");
        result = globus_gsi_cred_read_key(handle, key, NULL);

        if (result != GLOBUS_SUCCESS)
        {
            globus_object_t * error;

            error = globus_error_peek(result);

            if(globus_error_match_openssl_error(error,
                                        ERR_LIB_PEM,
                                        PEM_F_PEM_DO_HEADER,
                                        PEM_R_BAD_DECRYPT))
            {
                printf("failed\nUnable to decrypt private key: incorrect passphrase or corrupted file.\n");
            }
            else
            {
                printf("failed\n%s\n",
                    indent_string(
                        globus_error_print_friendly(globus_error_peek(result))));
            }
            goto out;
        }
        else
        {
            printf("ok\n");
        }
    }

    printf("Checking Certificate Subject... ");
    result = globus_gsi_cred_get_subject_name(handle, &subject_name);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
    }
    else
    {
        printf("\"%s\"\n", subject_name);
    }

    printf("Checking cert... ");
    result = globus_gsi_cred_get_cert(handle, &x509_cert);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
    }
    else
    {
        printf("ok\n");
    }

    printf("Checking key... ");
    result = globus_gsi_cred_get_key(handle, &privkey);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
    }
    else
    {
        printf("ok\n");
    }

    pubkey = X509_PUBKEY_get(X509_get_X509_PUBKEY(x509_cert));
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    key_type = EVP_PKEY_type(pubkey->type);
#else
    key_type = EVP_PKEY_base_id(pubkey);
#endif
    printf("Checking that certificate contains an RSA key... ");
    if (key_type != EVP_PKEY_RSA)
    {
        printf("failed\nKey type is %d\n", key_type);
        goto out;
    }
    else
    {
        printf("ok\n");
    }

    printf("Checking that private key is an RSA key... ");
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    key_type = EVP_PKEY_type(privkey->type);
#else
    key_type = EVP_PKEY_base_id(privkey);
#endif

    if (key_type != EVP_PKEY_RSA)
    {
        printf("failed\nPrivate key is %d\n", key_type);
        goto out;
    }
    else
    {
        printf("ok\n");
    }

    printf("Checking that public and private keys have the same modulus... ");
    RSA_get0_key(EVP_PKEY_get0_RSA(pubkey), &pub_n, NULL, NULL);
    RSA_get0_key(EVP_PKEY_get0_RSA(privkey), &priv_n, NULL, NULL);

    if (BN_cmp(pub_n, priv_n) != 0)
    {
        printf("failed\n"
               "Private key modulus: %s\n"
               "Public key modulus : %s\n",
               BN_bn2hex(pub_n),
               BN_bn2hex(priv_n));
        printf("Certificate and and private key don't match");
        goto out;
    }
    else
    {
        printf("ok\n");
    }

    printf("Checking certificate trust chain... ");
    result = setup_callback_data(&callback_data, GLOBUS_TRUE);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n%s\n",
               "    Internal error setting up callback data",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
    }
    else
    {
        result = globus_gsi_cred_verify_cert_chain(
            handle, callback_data);
        if (result != GLOBUS_SUCCESS)
        {
            printf("failed\n%s\n",
                    indent_string(
                        globus_error_print_friendly(globus_error_peek(result))));
        }
        else
        {
            printf("ok\n");
        }
    }

    printf("Checking if subject is in gridmap... ");
    rc = globus_gss_assist_gridmap(subject_name, &local_user);
    if (rc != 0)
    {
        printf("error looking for %s in gridmap\n", subject_name);
    }
    else if (local_user != NULL)
    {
        printf("%s\n", local_user);
    }
    else
    {
        printf("no\n");
    }
out:
    return;
}
/* default_cred_check() */

static
void
cert_check(const char *cert_to_check, const char *cert_check_name)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_gsi_cred_handle_t            handle = NULL;
    BIO                                *cert_bio = NULL;
    char                               *subject_name = NULL;
    X509                               *x509_cert = NULL;
    EVP_PKEY                           *pubkey = NULL;
    globus_gsi_callback_data_t          callback_data = NULL;
    char                               *compat_name = NULL;
    int                                 key_type = 0;

    printf("\nChecking Certificate\n"
           "====================\n");
    result = globus_gsi_cred_handle_init(&handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n",
                indent_string(
                    globus_error_print_friendly(
                            globus_error_peek(result))));
        goto handle_init_fail;
    }
    if (strcmp(cert_to_check, "-") == 0)
    {
        printf("Checking cert from stdin... ");

        if((cert_bio = BIO_new_fp(stdin, BIO_NOCLOSE)) == NULL)
        {
            printf("failed\nError opening certificate, skipping rest of certificate tests\n");
            goto bio_new_fail;
        }
    }
    else
    {
        printf("Checking cert at %s... ", cert_to_check);
        cert_bio = BIO_new_file(cert_to_check, "rb");
    }

    result = globus_gsi_cred_read_cert_bio(handle, cert_bio);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n",
                indent_string(
                    globus_error_print_friendly(
                            globus_error_peek(result))));
        goto read_bio_fail;
    }
    else
    {
        printf("ok\n");
    }

    printf("Checking Certificate Subject... ");
    result = globus_gsi_cred_get_subject_name(handle, &subject_name);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
    }
    else
    {
        printf("\"%s\"\n", subject_name);
    }
    printf("Checking cert... ");
    result = globus_gsi_cred_get_cert(handle, &x509_cert);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
    }
    else
    {
        printf("ok\n");
    }

    if (cert_check_name != NULL)
    {
        int name_equal = 0;
        OM_uint32 major_status=0, minor_status=0;
        gss_name_t x509_gss_name = GSS_C_NO_NAME;
        gss_name_t check_gss_name = GSS_C_NO_NAME;
        const char *name_mode = getenv("GLOBUS_GSSAPI_NAME_COMPATIBILITY");
        int rc;

        if (name_mode != NULL && strcmp(name_mode, "STRICT_RFC2818") == 0)
        {
            compat_name = strdup(cert_check_name);
        }
        else
        {
            struct addrinfo *res;
            char hbuf[NI_MAXHOST];

            rc = getaddrinfo(cert_check_name, NULL, NULL, &res);
            if (rc == 0)
            {
                rc = getnameinfo(
                    res->ai_addr,
                    res->ai_addrlen,
                    hbuf, (socklen_t) sizeof(hbuf),
                    NULL, (socklen_t) 0,
                    NI_NUMERICHOST);
                compat_name = globus_common_create_string("%s/%s",
                        cert_check_name,
                        hbuf);
                freeaddrinfo(res);
            }
            else
            {
                compat_name = strdup(cert_check_name);
            }
        }

        printf("Comparing certificate against hostname %s...",
                cert_check_name);

        major_status = gss_import_name(
                &minor_status,
                &(gss_buffer_desc)
                {
                    .value = x509_cert,
                    .length = sizeof(X509*)
                },
                GSS_C_NT_X509,
                &x509_gss_name);
        if (major_status != GSS_S_COMPLETE)
        {
            printf("failed importing certificate name\n%s\n",
                indent_string(
                    globus_error_print_friendly(
                            globus_error_peek(minor_status))));
            goto import_x509_name_fail;
        }

        major_status = gss_import_name(
                &minor_status,
                &(gss_buffer_desc) {
                    .value = (void *) compat_name,
                    .length = strlen(compat_name)
                },
                GSS_C_NT_HOST_IP,
                &check_gss_name);
        if (major_status != GSS_S_COMPLETE)
        {
            printf("failed importing host name\n%s\n",
                indent_string(
                    globus_error_print_friendly(
                            globus_error_peek(minor_status))));
            goto import_check_name_fail;
        }

        major_status = gss_compare_name(
                &minor_status,
                x509_gss_name,
                check_gss_name,
                &name_equal);

        if (major_status != GSS_S_COMPLETE)
        {
            printf("failed comparison\n%s\n",
                indent_string(
                    globus_error_print_friendly(
                            globus_error_peek(minor_status))));
            goto name_compare_fail;
        }
        printf("%s\n", name_equal ? "ok" : "failed");

name_compare_fail:
        gss_release_name(&minor_status, &check_gss_name);
import_check_name_fail:
        gss_release_name(&minor_status, &x509_gss_name);
import_x509_name_fail:
        ;
    }

    pubkey = X509_PUBKEY_get(X509_get_X509_PUBKEY(x509_cert));
    printf("Checking that certificate contains an RSA key... ");
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    key_type = EVP_PKEY_type(pubkey->type);
#else
    key_type = EVP_PKEY_base_id(pubkey);
#endif
    if (key_type != EVP_PKEY_RSA)
    {
        printf("failed\nKey type is %d\n", key_type);
    }
    else
    {
        printf("ok\n");
    }

    printf("Checking certificate trust chain... ");
    result = setup_callback_data(&callback_data, GLOBUS_TRUE);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n%s\n",
               "    Internal error setting up callback data",
               indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
        goto setup_callback_data_fail;
    }
    result = globus_gsi_cred_verify_cert_chain(
        handle, callback_data);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
    }
    else
    {
        printf("ok\n");
    }

    globus_gsi_callback_data_destroy(callback_data);
setup_callback_data_fail:
read_bio_fail:
    BIO_free(cert_bio);
bio_new_fail:
    globus_gsi_cred_handle_destroy(handle);
handle_init_fail:
    if (compat_name != NULL)
    {
        free(compat_name);
    }
    return;
}
/* cert_check() */

static
void
check_trusted_certs(void)
{
    int                                 rc;
    globus_result_t                     result;
    char                               *cert_dir;
    globus_fifo_t                       cert_list = {0};
    globus_gsi_cred_handle_t            handle = NULL;
    globus_gsi_callback_data_t          callback_data = NULL;

    printf("\nChecking trusted certificates...\n"
           "================================\n");
    rc = globus_fifo_init(&cert_list);
    if (rc != GLOBUS_SUCCESS)
    {
        printf("Internal error: out of memory\n");
        goto fifo_init_fail;
    }

    result = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(
            &cert_dir);
    if (result != GLOBUS_SUCCESS)
    {
        printf("%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
        goto get_ca_cert_dir_fail;
    }
    result = setup_callback_data(&callback_data, GLOBUS_FALSE);
    if (result != GLOBUS_SUCCESS)
    {
        printf("%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
        goto setup_callback_data_fail;
    }

    printf("Getting trusted certificate list...\n");
    result = GLOBUS_GSI_SYSCONFIG_GET_CA_CERT_FILES(cert_dir, &cert_list);
    if (result != GLOBUS_SUCCESS)
    {
        printf("%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
        goto get_ca_cert_files_fail;
    }
    result = globus_gsi_cred_handle_init(
            &handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        printf("%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
        goto init_handle_fail;
    }
    while (! globus_fifo_empty(&cert_list))
    {
        char * ca_cert_file = globus_fifo_dequeue(&cert_list);
        char * ca_subject_name;
        unsigned long hash;
        X509_NAME * x509_subject_name;
        char * signing_policy_filename;
        char hash_string[16];

        printf("Checking CA file %s... ", ca_cert_file);

        result = globus_gsi_cred_read_cert(handle, ca_cert_file);
        if (result != GLOBUS_SUCCESS)
        {
            printf("failed\n%s\n",
                    indent_string(
                        globus_error_print_friendly(globus_error_peek(result))));
            continue;
        }
        printf("ok\nChecking that certificate hash matches filename... ");
        result = globus_gsi_cred_get_X509_subject_name(
            handle, &x509_subject_name);
        if (result != GLOBUS_SUCCESS)
        {
            printf("failed\n%s\n",
                    indent_string(
                        globus_error_print_friendly(globus_error_peek(result))));
            continue;
        }

        hash = X509_NAME_hash(x509_subject_name);
        sprintf(hash_string, "%08lx.0", hash);

        if (strstr(ca_cert_file, hash_string) == 0)
        {
            printf("failed\n    CA hash '%s' does not match CA filename\n", hash_string);
            continue;
        }
        printf("ok\nChecking CA certificate name for %s...", hash_string);
        result = globus_gsi_cred_get_subject_name(handle, &ca_subject_name);
        if (result != GLOBUS_SUCCESS)
        {
            printf("failed\n%s\n",
                    indent_string(
                        globus_error_print_friendly(globus_error_peek(result))));
            continue;
        }

        printf("ok (%s)\nChecking if signing policy exists for %s... ", ca_subject_name, hash_string);
        result = GLOBUS_GSI_SYSCONFIG_GET_SIGNING_POLICY_FILENAME(
                x509_subject_name,
                cert_dir,
                &signing_policy_filename);
        if (result != GLOBUS_SUCCESS)
        {
            printf("failed\n%s\n",
                    indent_string(
                        globus_error_print_friendly(globus_error_peek(result))));
            continue;
        }
        free(signing_policy_filename);

        printf("ok\nVerifying certificate chain for %s... ",
               hash_string);
        result = globus_gsi_cred_verify_cert_chain(
            handle, callback_data);
        if (result != GLOBUS_SUCCESS)
        {
            printf("failed\n%s\n",
                    indent_string(
                        globus_error_print_friendly(globus_error_peek(result))));
            continue;
        }
        printf("ok\n");
    }
setup_callback_data_fail:
    globus_gsi_cred_handle_destroy(handle);
init_handle_fail:
get_ca_cert_files_fail:
    free(cert_dir);
get_ca_cert_dir_fail:
    globus_fifo_destroy_all(&cert_list, free);
fifo_init_fail:
    return;
}
/* check_trusted_certs() */

static
void
check_service(const char *service)
{
    int                                 rc;
    SSL_CTX                            *ctx;
    SSL                                *ssl;
    BIO                                *web = NULL;
    #if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    const SSL_METHOD                   *method = TLS_method();
    #else
    const SSL_METHOD                   *method = SSLv23_method();
    #endif
    X509                               *peer_cert = NULL;
    STACK_OF(X509)                     *cert_chain = NULL;
    STACK_OF(SSL_CIPHER)               *ciphers = NULL;

    printf("\nChecking service...\n"
           "===================\n");
    ctx = SSL_CTX_new(method);
    if (ctx == NULL)
    {
        printf("Unable to create TLS context\n");
        ERR_print_errors_fp(stderr);
        goto ctx_new_fail;
    }
    #if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    SSL_CTX_set_min_proto_version(ctx,TLS1_VERSION);
    #else
    SSL_CTX_set_options(ctx,SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);
    #endif
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_verify_depth(ctx, 9);
    web = BIO_new_ssl_connect(ctx);
    if (!web)
    {
        printf("Unable to create connection BIO\n");
        goto new_ssl_connect_fail;
    }
    rc = BIO_set_conn_hostname(web, service);
    if (rc != 1)
    {
        printf("Unable to set SSL connection\n");
        goto set_conn_hostname_fail;
    }
    BIO_get_ssl(web, &ssl);
    if (ssl == NULL)
    {
        printf("Unable to get SSL\n");
        goto get_ssl_fail;
    }
    printf("Connecting to %s... ", service);
    rc = BIO_do_connect(web);
    if(rc != 1)
    {
        printf("failed\n");
        goto connect_fail;
    }
    else
    {
        printf("ok\n");
    }

    printf("Performing TLS handshake... ");
    rc = BIO_do_handshake(web);
    if (rc != 1)
    {
        printf("failed\n");
        goto handshake_fail;
    }
    else
    {
        printf("ok\n");
    }

    printf("Checking TLS version... %s\n", SSL_get_version(ssl));

    printf("Checking ciphers...");
    ciphers = SSL_get_ciphers(ssl);

    while (sk_SSL_CIPHER_num(ciphers) > 0)
    {
        const SSL_CIPHER *cipher = sk_SSL_CIPHER_pop(ciphers);
        printf(" %s", SSL_CIPHER_get_name(cipher));
    }
    printf("\n");

    peer_cert = SSL_get_peer_certificate(ssl);
    cert_chain = SSL_get_peer_cert_chain(ssl);

    check_service_cert_chain(
            service,
            peer_cert,
            cert_chain);

handshake_fail:
connect_fail:
get_ssl_fail:
set_conn_hostname_fail:
new_ssl_connect_fail:
ctx_new_fail:
    return;
}
/* check_trusted_certs() */

static
void
check_gridftp(char *service)
{
    int                                 rc;
    SSL_CTX                            *ctx;
    SSL                                *ssl;
    BIO                                *cbio = NULL, *rbio = NULL, *wbio = NULL;
    #if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    const SSL_METHOD                   *method = TLS_method();
    #else
    const SSL_METHOD                   *method = SSLv23_method();
    #endif
    X509                               *peer_cert = NULL;
    STACK_OF(X509)                     *cert_chain = NULL;
    STACK_OF(SSL_CIPHER)               *ciphers = NULL;
    char                                banner[256];
    char                                response[256];

    printf("\nChecking gridftp service...\n"
           "===========================\n");
    ctx = SSL_CTX_new(method);
    if (ctx == NULL)
    {
        printf("Unable to create TLS context\n");
        ERR_print_errors_fp(stderr);
        goto ctx_new_fail;
    }
    #if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    SSL_CTX_set_min_proto_version(ctx,TLS1_VERSION);
    #else
    SSL_CTX_set_options(ctx,SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);
    #endif
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_verify_depth(ctx, 9);

    rbio = BIO_new(BIO_s_mem());
    if (!rbio)
    {
        goto rbio_new_fail;
    }
    wbio = BIO_new(BIO_s_mem());
    if (!wbio)
    {
        goto wbio_new_fail;
    }
    printf("Connecting to %s... ", service);
    cbio = BIO_new_connect(service);
    if (!cbio)
    {
        goto cbio_new_fail;
    }

    /* Receive banner */
    rc = BIO_read(cbio, banner, sizeof(banner));
    if (rc < 3)
    {
        printf("Error reading banner\n");
        goto bad_banner;
    }

    if (rc < 3 || strncmp(banner, "220", 3) != 0)
    {
        printf("not ok\n%s", indent_string(banner));
        goto bad_banner;
    }
    printf("%.*s\n", (int) rc-4, banner+3);

    printf("Initiating authentication protocol... ");
    if (BIO_puts(cbio, "AUTH GSSAPI\r\n") == -1)
    {
        printf("not ok\n");
        goto bad_auth;
    }
    (void) BIO_flush(cbio);
    rc = BIO_read(cbio, response, sizeof response);
    if (rc < 3)
    {
        printf("not ok\n");
        goto bad_auth;
    }

    if (strncmp(response, "334", 3) != 0)
    {
        printf("not ok\n%s", indent_string(response));
        goto bad_auth;
    }
    printf("ok\n");

    ssl = SSL_new(ctx);
    SSL_set_bio(ssl, rbio, wbio);
    SSL_connect(ssl);

    do
    {
        rc = SSL_do_handshake(ssl);
        if (rc == 0 || rc == 1)
        {
            break;
        }

        if ((rc = BIO_ctrl_pending(wbio)) > 0)
        {
            char buf[rc], *encbuf;
            size_t encbufsize;

            rc = BIO_read(wbio, buf, sizeof buf);
            base64_encode(buf, rc, &encbuf, &encbufsize);

            BIO_puts(cbio, "ADAT ");
            BIO_write(cbio, encbuf, encbufsize);
            BIO_puts(cbio, "\r\n");
            (void) BIO_flush(cbio);
            free(encbuf);

            char rbuf[256*1024];
            size_t offset = 0;
            do
            {
                rc = BIO_read(cbio, rbuf+offset, sizeof rbuf-offset-1);
                rbuf[offset+rc] = '\0';
                if (rc >= 0)
                {
                    offset += rc;
                }

                if (rc >= 0 &&
                    (strncmp(rbuf, "234 ADAT=", 9) == 0 ||
                    strncmp(rbuf, "235 ADAT=", 9) == 0 ||
                    strncmp(rbuf, "334 ADAT=", 9) == 0 ||
                    strncmp(rbuf, "335 ADAT=", 9) == 0) &&
                    strstr(rbuf, "\r\n") != NULL)
                {
                    base64_decode(rbuf+9, offset-11, &encbuf, &encbufsize);
                    BIO_write(rbio, encbuf, encbufsize);
                    (void) BIO_flush(rbio);
                    free(encbuf);
                    rc = -1;
                }
            } while (rc != -1);
        }
        else
        {
            break;
        }
    }
    while (rc < 0);


    printf("Checking TLS version... %s\n", SSL_get_version(ssl));

    printf("Checking ciphers...");
    ciphers = SSL_get_ciphers(ssl);

    while (sk_SSL_CIPHER_num(ciphers) > 0)
    {
        const SSL_CIPHER *cipher = sk_SSL_CIPHER_pop(ciphers);
        printf(" %s", SSL_CIPHER_get_name(cipher));
    }
    printf("\n");

    peer_cert = SSL_get_peer_certificate(ssl);
    cert_chain = SSL_get_peer_cert_chain(ssl);

    check_service_cert_chain(
            service,
            peer_cert,
            cert_chain);
cbio_new_fail:
wbio_new_fail:
rbio_new_fail:
bad_auth:
bad_banner:
ctx_new_fail:
    return;
}
/* check_trusted_certs() */


static
void
base64_encode(
    const char                         *input,
    size_t                              input_len,
    char                              **output,
    size_t                             *output_len)
{
    BIO                                *base64, *mem;

    base64 = BIO_new(BIO_f_base64());
    BIO_set_flags(base64, BIO_FLAGS_BASE64_NO_NL);

    mem = BIO_new(BIO_s_mem());

    BIO_push(base64, mem);

    BIO_write(base64, input, input_len);
    (void) BIO_flush(base64);

    *output_len = BIO_ctrl_pending(mem);
    *output = malloc((*output_len)+1);

    BIO_read(mem, *output, *output_len);
    (*output)[*output_len] = '\0';

    BIO_free_all(base64);
}
/* base64_encode() */

static
void
base64_decode(
    char                               *input,
    size_t                              input_len,
    char                              **output,
    size_t                             *output_len)
{
    BIO                                *base64, *mem;
    int                                 rc;

    base64 = BIO_new(BIO_f_base64());
    BIO_set_flags(base64, BIO_FLAGS_BASE64_NO_NL);

    mem = BIO_new_mem_buf(input, input_len);

    BIO_push(base64, mem);

    *output = malloc(input_len);

    rc = BIO_read(base64, *output, input_len);
    *output_len = rc;
    (*output)[rc] = '\0';

    BIO_free_all(base64);
}
/* base64_decode() */

static
void
check_service_cert_chain(
    const char                         *host_string,
    X509                               *cert,
    STACK_OF(X509)                     *cert_chain)
{
    globus_result_t                     result;
    globus_gsi_cred_handle_t            handle = NULL;
    globus_gsi_callback_data_t          callback_data = NULL;
    const X509_NAME                    *n = NULL;
    size_t                              host_len;
    const char                         *no_extensions = " no";
    OM_uint32                           major_status, minor_status;
    int                                 compare_result = GLOBUS_FALSE;
    gss_name_t                          host_name = NULL, cert_name = NULL;
    const char                         *p;

    if ((p = strchr(host_string, ':')) == NULL)
    {
        host_len = strlen(host_string);
    }
    else
    {
        host_len = p - host_string;
    }

    printf("Importing names... ");

    if (cert == NULL || cert_chain == NULL)
    {
        printf("not ok\n    Unable to get certificate chain\n");
        return;
    }

    major_status = gss_import_name(
            &minor_status,
            &(gss_buffer_desc)
            {
                .value = (char *) host_string,
                .length = host_len
            },
            GSS_C_NT_HOST_IP,
            &host_name);

    if (GSS_ERROR(major_status))
    {
        printf("failure\n%s\n",
                indent_string(
                    globus_error_print_friendly(
                            globus_error_peek(minor_status))));
        return;
    }

    major_status = gss_import_name(
            &minor_status,
            &(gss_buffer_desc)
            {
                .value = cert,
                .length = sizeof(X509*)
            },
            GSS_C_NT_X509,
            &cert_name);
    if (GSS_ERROR(major_status))
    {
        printf("not ok\n%s\n",
                indent_string(
                    globus_error_print_friendly(
                            globus_error_peek(minor_status))));
        return;
    }
    printf("ok\n");

    printf("Loading certificate chain... ");
    result = setup_callback_data(&callback_data, GLOBUS_TRUE);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed: unable to initialize callback data\n%s\n",
                indent_string(
                        globus_error_print_friendly(
                                globus_error_peek(result))));
        goto setup_callback_data_fail;
    }
    result = globus_gsi_cred_handle_init(
            &handle, NULL);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed: unable to initialize credential handle\n%s\n",
                indent_string(
                        globus_error_print_friendly(
                                globus_error_peek(result))));
        goto init_cred_handle_fail;
    }
    result = globus_gsi_cred_set_cert(
            handle,
            cert);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed: unable to set credential cert\n%s\n",
                indent_string(
                        globus_error_print_friendly(
                                globus_error_peek(result))));
        goto set_cert_fail;
    }
    result = globus_gsi_cred_set_cert_chain(
            handle,
            cert_chain);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed: unable to set credential cert\n%s\n",
                indent_string(
                        globus_error_print_friendly(
                                globus_error_peek(result))));
        goto set_cert_chain_fail;
    }
    printf("ok\n");


    printf("Checking peer subject name... ");
    n = X509_get_subject_name(cert);
    if (n)
    {
        printf("%s\n", X509_NAME_oneline(n, NULL, 0));
    }
    else
    {
        printf("no\n");
    }
    printf("Checking peer certificate issuer... ");
    n = X509_get_issuer_name(cert);
    if (n)
    {
        printf("%s\n", X509_NAME_oneline(n, NULL, 0));
    }
    else
    {
        printf("no\n");
    }

    printf("Checking subjectAltName extensions...");
    for (int idx = X509_get_ext_by_NID(cert, NID_subject_alt_name, -1);
         idx != -1;
         idx = X509_get_ext_by_NID(cert, NID_subject_alt_name, idx))
    {
#if OPENSSL_VERSION_NUMBER < 0x40000000L
        X509_EXTENSION                 *ext_value;
#else
        const X509_EXTENSION           *ext_value;
#endif
        GENERAL_NAMES                  *subject_alt_names;

        no_extensions = "";
        ext_value = X509_get_ext(cert, idx);
        if (!ext_value)
        {
            continue;
        }
        subject_alt_names = X509V3_EXT_d2i(ext_value);
        if (!subject_alt_names)
        {
            continue;
        }
        while (sk_GENERAL_NAME_num(subject_alt_names) > 0)
        {
            GENERAL_NAME               *subject_alt_name;

            subject_alt_name = sk_GENERAL_NAME_pop(subject_alt_names);
            if (subject_alt_name->type == GEN_DNS)
            {
                printf(" dns:%.*s",
                       (int) ASN1_STRING_length(subject_alt_name->d.dNSName),
                       ASN1_STRING_get0_data(subject_alt_name->d.dNSName));
            }
            else if (subject_alt_name->type == GEN_IPADD)
            {
                printf(" ip:%.*s",
                       (int) ASN1_STRING_length(subject_alt_name->d.iPAddress),
                       ASN1_STRING_get0_data(subject_alt_name->d.iPAddress));
            }
            else
            {
                printf(" UNKNOWN:%d", subject_alt_name->type);
            }
        }
    }
    printf("%s\n", no_extensions);

    printf("Verifying certificate chain... ");
    result = globus_gsi_cred_verify_cert_chain(
        handle, callback_data);
    if (result != GLOBUS_SUCCESS)
    {
        printf("failed\n%s\n",
                indent_string(
                    globus_error_print_friendly(globus_error_peek(result))));
    }
    else
    {
        printf("ok\n");
    }
    printf("Performing name comparison... ");
    major_status = gss_compare_name(
            &minor_status,
            host_name,
            cert_name,
            &compare_result);
    if (GSS_ERROR(major_status))
    {
        printf("failure\n%s\n",
                indent_string(
                    globus_error_print_friendly(
                            globus_error_peek(minor_status))));
    }
    else
    {
        printf("%s\n", compare_result ? "ok" : "not ok");
    }

    gss_release_name(&minor_status, &host_name);
    gss_release_name(&minor_status, &cert_name);

set_cert_chain_fail:
set_cert_fail:
    globus_gsi_cred_handle_destroy(handle);
init_cred_handle_fail:
    globus_gsi_callback_data_destroy(callback_data);
setup_callback_data_fail:
    return;
}
/* check_service_cert_chain() */

static
int
get_tz_offset(
    const struct tm                    *epoch,
    intmax_t                           *offset)
{
    intmax_t tzoff = 0;
    int tzh, tzm;
    char tzbuf[16];
    char tzneg[2];

    if (strftime(tzbuf, sizeof(tzbuf), "%z", epoch) == 0)
    {
        return -1;
    }

    if (sscanf(tzbuf, "%1[-+]%02d:%02d", tzneg, &tzh, &tzm) == 3) {
        tzoff = (tzh * 60 * 60 + tzm * 60) * (*tzneg == '-' ? -1 : 1);
    } else if (sscanf(tzbuf, "%1[-+]%02d%02d", tzneg, &tzh, &tzm) == 3) {
        tzoff = (tzh * 60 * 60 + tzm * 60) * (*tzneg == '-' ? -1 : 1);
    } else if (sscanf(tzbuf, "%1[-+]%02d", tzneg, &tzh) == 2) {
        tzoff = (tzh * 60 * 60) * (*tzneg == '-' ? -1 : 1);
    } else if (sscanf(tzbuf, "%02d", &tzh) == 1) {
        tzoff = (tzh * 60 * 60);
    } else {
        return -1;
    }

    *offset = tzoff;
    return 0;
}
/* get_tz_offset() */

static
globus_result_t
setup_callback_data(
    globus_gsi_callback_data_t         *callback_data,
    globus_bool_t                       check_policy_for_self_signed)
{
    globus_result_t                     result;
    char                               *cert_dir = NULL;


    *callback_data = NULL;

    result = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&cert_dir);
    if (result != GLOBUS_SUCCESS)
    {
        goto get_cert_dir_failed;
    }
    result = globus_gsi_callback_data_init(callback_data);
    if (result != GLOBUS_SUCCESS)
    {
        goto init_failed;
    }
    result = globus_gsi_callback_set_cert_dir(*callback_data, cert_dir);
    if (result != GLOBUS_SUCCESS)
    {
        goto set_cert_dir_failed;
    }
    result = globus_gsi_callback_set_check_policy_for_self_signed_certs(
            *callback_data, check_policy_for_self_signed);
    if (result != GLOBUS_SUCCESS)
    {
set_cert_dir_failed:
        globus_gsi_callback_data_destroy(*callback_data);
    }
init_failed:
get_cert_dir_failed:
    free(cert_dir);
    return result;
}
/* setup_callback_data() */

static
char *
indent_string(const char * str)
{
    const char * new_line;
    const char * old_line;
    char * output;
    int i=1;

    if (str == NULL)
    {
        return NULL;
    }

    for (new_line = str; new_line != NULL; new_line = strchr(new_line+1, '\n'))
    {
        i++;
    }

    output = malloc(strlen(str) + (i*4) + 1);
    i = 0;

    for (new_line = strchr(str, '\n'), old_line = str;
         new_line != NULL;
         old_line = new_line+1, new_line = strchr(new_line+1, '\n'))
    {
        memcpy(output+i, "    ", 4);
        i += 4;
        memcpy(output+i, old_line, new_line - old_line);
        i += new_line - old_line;
        output[i++] = '\n';
    }
    output[i] = 0;

    return output;
}
/* indent_string() */

