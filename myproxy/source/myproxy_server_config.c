/*
 * myproxy_server_config.c
 *
 * Routines from reading and parsing the server configuration.
 *
 * See myproxy_server.h for documentation.
 */

#define SYSLOG_NAMES            /* for facilitynames */

#include "myproxy_common.h"	/* all needed headers included here */

#if defined(HAVE_REGCOMP) && defined(HAVE_REGEX_H)
#include <regex.h>

#elif defined(HAVE_COMPILE) && defined(HAVE_REGEXPR_H)
#include <regexpr.h>

#else
#define NO_REGEX_SUPPORT

#endif

#define REGULAR_EXP 1
#define NON_REGULAR_EXP 0

/**********************************************************************
 *
 * Internal Functions
 *
 */

static void
free_ptr(char **p)
{
    if (!p) return;
    if (!*p) return;
    free(*p);
    *p = NULL;
}

struct config_directives {
	char *name;	/* Directive name */
	size_t minargs;	/* Minimal number of arguments */
	size_t maxargs;	/* Maximal number of arguments */
};

/*
 * Specify this constant for 'minargs' and 'maxargs' if you
 * don't want to check the given limit.
 *
 * And yes, I can't make it static variable because it is
 * used in the file-scope variable initialization  ;((
 */
#define NARGS_DONTCHECK SIZE_MAX

static struct config_directives our_conf[] = {
	{"allowed_clients", 0, NARGS_DONTCHECK},
	{"accepted_credentials", 0, NARGS_DONTCHECK},
	{"allowed_services", 0, NARGS_DONTCHECK},
	{"authorized_retrievers", 0, NARGS_DONTCHECK},
	{"default_retrievers", 0, NARGS_DONTCHECK},
	{"authorized_renewers", 0, NARGS_DONTCHECK},
	{"default_renewers", 0, NARGS_DONTCHECK},
	{"authorized_key_retrievers", 0, NARGS_DONTCHECK},
	{"default_key_retrievers", 0, NARGS_DONTCHECK},
	{"trusted_retrievers", 0, NARGS_DONTCHECK},
	{"default_trusted_retrievers", 0, NARGS_DONTCHECK},
	{"passphrase_policy_program", 1, 1},
	{"max_proxy_lifetime", 1, 1},
	{"max_cred_lifetime", 1, 1},
	{"ignore_globus_limited_proxy_flag", 1, 1},
	{"allow_self_authorization", 1, 1},
	{"cert_dir", 1, 1},
	{"pam", 1, 1},
	{"pam_id", 1, 1},
	{"sasl", 1, 1},
#if defined(HAVE_LIBSASL2)
	{"sasl_mech", 1, 1},
	{"sasl_serverFQDN", 1, 1},
	{"sasl_user_realm", 1, 1},
#endif
	{"certificate_issuer_program", 1, 1},
	{"certificate_issuer_cert", 1, 1},
	{"certificate_issuer_key", 1, 1},
	{"certificate_issuer_hashalg", 1, 1},
	{"certificate_request_checker", 1, 1},
	{"certificate_issuer_checker", 1, 1},
	{"certificate_issuer_key_passphrase", 1, 1},
	{"certificate_issuer_subca_certfile", 1, 1},
	{"certificate_openssl_engine_id", 1, 1},
	{"certificate_openssl_engine_lockfile", 1, 1},
	{"certificate_openssl_engine_pre", 0, NARGS_DONTCHECK},
	{"certificate_openssl_engine_post", 0, NARGS_DONTCHECK},
	{"certificate_issuer_email_domain", 1, 1},
	{"certificate_extfile", 1, 1},
	{"certificate_extapp", 1, 1},
	{"certificate_mapfile", 1, 1},
	{"certificate_mapap", 1, 1},
	{"max_cert_lifetime", 1, 1},
	{"min_keylen", 1, 1},
	{"certificate_serialfile", 1, 1},
	{"certificate_serial_skip", 1, 1},
	{"certificate_out_dir", 1, 1},
	{"ca_ldap_server", 1, 1},
	{"ca_ldap_searchbase", 1, 1},
	{"ca_ldap_connect_dn", 1, 1},
	{"ca_ldap_connect_passphrase", 1, 1},
	{"ca_ldap_uid_attribute", 1, 1},
	{"ca_ldap_dn_attribute", 1, 1},
	{"ca_ldap_start_tls", 1, 1},
	{"accepted_credentials_mapfile", 1, 1},
	{"accepted_credentials_mapapp", 1, 1},
	{"check_multiple_credentials", 1, 1},
#if defined(HAVE_OCSP)
	{"ocsp_policy", 1, 1},
	{"ocsp_responder_url", 1, 1},
	{"ocsp_responder_cert", 1, 1},
#endif /* defined(HAVE_OCSP) */
	{"syslog_ident", 1, 1},
	{"syslog_facility", 1, 1},
	{"slave_servers", 0, NARGS_DONTCHECK},
	{"request_timeout", 1, 1},
	{"request_size_limit", 1, 1},
	{"proxy_extfile", 1, 1},
	{"proxy_extapp", 1, 1},
#ifdef HAVE_VOMS
	{"voms_userconf", 1, 1},
	{"allow_voms_attribute_requests", 1, 1},
#endif
/* Terminating entity */
	{NULL, 0, 0}
};

/*
 * plural_args()
 *
 * Returns the right form for the verb 'arguments' for the
 * provided number of arguments.
 */
static const char *
plural_args(int n)
{
    if (n == 1)
        return "argument";
    else
        return "arguments";
}

/*
 * check_config_line()
 *
 * Verifies that the splitted line tokens are appropriate for the
 * given directive.  Just now it checks minimal and maximal number
 * of arguments -- this enables other code to safely use 'token[n]'
 * without overflowing the array index.
 *
 * This function prints warnings via myproxy_log().
 */
static int
check_config_line(struct config_directives *conf_dirs,
                  const char **tokens)
{
    size_t i, nargs;
    const char *d;
    struct config_directives *e = NULL;

    if (tokens == NULL || tokens[0] == NULL) {
        return 0;
    }
    d = tokens[0];

    /*
     * Search for the directive, exit silently if it wasn't found:
     * we check only those directives that were provided to us
     * and aren't going to warn about the extra ones -- this is
     * up to other layers.
     */
    for (e = NULL, i = 0; conf_dirs[i].name != NULL; i++) {
        if (strcmp(d, conf_dirs[i].name) == 0) {
            e = conf_dirs + i;
            break;
        }
    }
    if (e == NULL)
        return 0;

    /* Do we need to check anything? */
    if (e->minargs == NARGS_DONTCHECK && e->maxargs == NARGS_DONTCHECK)
        return 0;

    for (i = 1; tokens[i] != NULL && tokens[i][0] != '#'; i++);
    nargs = i - 1;

    if ((e->minargs != NARGS_DONTCHECK && nargs < e->minargs) ||
     (e->maxargs != NARGS_DONTCHECK && nargs > e->maxargs)) {
        char expl[1024];

        if (e->minargs == e->maxargs) {
            snprintf(expl, sizeof(expl), "takes exactly %d %s",
                     (int)e->minargs, plural_args(e->minargs));
        } else if (e->minargs == NARGS_DONTCHECK) {
            snprintf(expl, sizeof(expl), "wants no more than %d %s",
                     (int)e->maxargs, plural_args(e->maxargs));
        } else if (e->maxargs == NARGS_DONTCHECK) {
            snprintf(expl, sizeof(expl), "wants no less than %d %s",
                     (int)e->minargs, plural_args(e->minargs));
        } else {
            snprintf(expl, sizeof(expl), "takes from %d to %d arguments",
                     (int)e->minargs, (int)e->maxargs);
        }
        myproxy_log("Directive '%s': supplied %d %s, %s.\n",
         d, nargs, plural_args(nargs), expl);
        return -1;
    }

    return 0;
}

/*
 * clear_server_context()
 *
 * Initialize the server context before filling in the configuration
 * values.  Enables myproxy_server_config_read() to be called
 * multiple times on changes to the config file.
 */
static void
clear_server_context(myproxy_server_context_t *context)
{
    free_array_list(&context->accepted_credential_dns);
    free_array_list(&context->authorized_retriever_dns);
    free_array_list(&context->default_retriever_dns);
    free_array_list(&context->authorized_renewer_dns);
    free_array_list(&context->default_renewer_dns);
    free_array_list(&context->authorized_key_retrievers_dns);
    free_array_list(&context->default_key_retrievers_dns);
    free_array_list(&context->trusted_retriever_dns);
    free_array_list(&context->default_trusted_retriever_dns);
    free_ptr(&context->passphrase_policy_pgm);
    context->max_proxy_lifetime = 0;
    context->max_cred_lifetime = 0;
    context->limited_proxy = 0;
    context->request_size_limit = 0x100000; /* 1MB default */
    free_ptr(&context->cert_dir);
    free_ptr(&context->pam_policy);
    free_ptr(&context->pam_id);
    free_ptr(&context->sasl_policy);
    free_ptr(&context->certificate_issuer_program);
    free_ptr(&context->certificate_issuer_cert);
    free_ptr(&context->certificate_issuer_key);
    context->certificate_hashalg = EVP_sha256();
    free_ptr(&context->certificate_request_checker);
    free_ptr(&context->certificate_issuer_checker);
    free_ptr(&context->certificate_issuer_key_passphrase);
    free_ptr(&context->certificate_issuer_subca_certfile);
    free_ptr(&context->certificate_openssl_engine_id);
    free_ptr(&context->certificate_openssl_engine_lockfile);
    free_array_list(&context->certificate_openssl_engine_pre);
    free_array_list(&context->certificate_openssl_engine_post);
    free_ptr(&context->certificate_issuer_email_domain);
    free_ptr(&context->certificate_extfile);
    free_ptr(&context->certificate_extapp);
    free_ptr(&context->certificate_mapfile);
    free_ptr(&context->certificate_mapapp);
    context->max_cert_lifetime = 0;
    context->min_keylen = 0;
    free_ptr(&context->certificate_serialfile);
    context->certificate_serial_skip = 1;
    free_ptr(&context->certificate_out_dir);
    free_ptr(&context->ca_ldap_server);
    free_ptr(&context->ca_ldap_searchbase);
    free_ptr(&context->ca_ldap_connect_dn);
    free_ptr(&context->ca_ldap_connect_passphrase);
    free_ptr(&context->ca_ldap_uid_attribute);
    free_ptr(&context->ca_ldap_dn_attribute);
    context->ca_ldap_start_tls = 0;
    free_ptr(&context->accepted_credentials_mapfile);
    free_ptr(&context->accepted_credentials_mapapp);
    context->check_multiple_credentials = 0;
    free_ptr(&context->syslog_ident);
    context->syslog_facility = LOG_DAEMON;
#if defined(HAVE_LIBSASL2)
    free_ptr(&myproxy_sasl_mech);
    free_ptr(&myproxy_sasl_serverFQDN);
    free_ptr(&myproxy_sasl_user_realm);
#endif
    memset(&context->usage, 0, sizeof(context->usage));
    free_ptr(&context->voms_userconf);
    context->allow_voms_attribute_requests = 0;
}

void
myproxy_server_clear_context(myproxy_server_context_t *context)
{
    clear_server_context(context);
}

/*
 * decode_facility()
 *
 * Return the syslog facility number given a facility string.
 */
static int
decode_facility(const char *name)
{
#if HAVE_DECL_FACILITYNAMES
    CODE *c;
#endif

    if (isdigit(*name))
        return (atoi(name));

#if HAVE_DECL_FACILITYNAMES
    for (c = facilitynames; c->c_name; c++)
        if (!strcasecmp(name, c->c_name))
            return (c->c_val);
#else
    myproxy_log("warning: operating system facilitynames declaration not found. syslog_facility can support only numeric values.");
#endif

    myproxy_log("warning: unknown syslog_facility (%s) in myproxy-server.config. defaulting to LOG_DAEMON.", name);

    return (LOG_DAEMON);
}


/*
 * line_parse_callback()
 *
 * Callback for vparse_stream().
 *
 * This function should return 0 unless it wants parsing to stop
 * which should only happen on fatal error - e.g. malloc() failing.
 */
static int
line_parse_callback(void *context_arg,
		    int line_number,
		    const char **tokens)
{
    myproxy_server_context_t *context = context_arg;
    const char *directive;
    int return_code = -1;
    int index;
    
    assert(context != NULL);
    
    if ((tokens == NULL) || (*tokens == NULL) || (**tokens == '#')) {
	return 0; /* Blank line or comment */
    }

    /* Check basic sanity */
    if (check_config_line(our_conf, tokens) != 0)
        return -1;

    directive = tokens[0];
    
    /* allowed_clients is the old name for accepted_credentials */
    if ((strcmp(directive, "allowed_clients") == 0) ||
	(strcmp(directive, "accepted_credentials") == 0)) {
	for (index=1; tokens[index] != NULL; index++) {
	    context->accepted_credential_dns =
		add_entry(context->accepted_credential_dns, tokens[index]);
	    if (context->accepted_credential_dns == NULL) {
		goto error;
	    }
	}
    }

    /* allowed_services is the old name for authorized_retrievers */
    else if ((strcmp(directive, "allowed_services") == 0) ||
	     (strcmp(directive, "authorized_retrievers") == 0)) {
	for (index=1; tokens[index] != NULL; index++) {
	    context->authorized_retriever_dns =
		add_entry(context->authorized_retriever_dns,
			  tokens[index]);
	    if (context->authorized_retriever_dns == NULL) {
		goto error;
	    }
	}
    }
    else if((strcmp(directive, "default_retrievers") == 0)) {
	for (index=1; tokens[index] != NULL; index++) {
	    context->default_retriever_dns =
		add_entry(context->default_retriever_dns,
			  tokens[index]);
	    if (context->default_retriever_dns == NULL) {
		goto error;
	    }
	}
    }
    else if (strcmp(directive, "authorized_renewers") == 0) {
	for (index=1; tokens[index] != NULL; index++) {
	    context->authorized_renewer_dns =
		add_entry(context->authorized_renewer_dns,
			  tokens[index]);
	    if (context->authorized_renewer_dns == NULL) {
		goto error;
	    }
	}
    }
    else if (strcmp(directive, "default_renewers") == 0) {
	for (index=1; tokens[index] != NULL; index++) {
	    context->default_renewer_dns =
		add_entry(context->default_renewer_dns,
			  tokens[index]);
	    if (context->default_renewer_dns == NULL) {
		goto error;
	    }
	}
    }
    else if (strcmp(directive, "authorized_key_retrievers") == 0) {
	for (index=1; tokens[index] != NULL; index++) {
	    context->authorized_key_retrievers_dns =
		add_entry(context->authorized_key_retrievers_dns,
			  tokens[index]);
	    if (context->authorized_key_retrievers_dns == NULL) {
		goto error;
	    }
	}
    }
    else if (strcmp(directive, "default_key_retrievers") == 0) {
	for (index=1; tokens[index] != NULL; index++) {
	    context->default_key_retrievers_dns =
		add_entry(context->default_key_retrievers_dns,
			  tokens[index]);
	    if (context->default_key_retrievers_dns == NULL) {
		goto error;
	    }
	}
    }
    else if (strcmp(directive, "trusted_retrievers") == 0) {
	for (index=1; tokens[index] != NULL; index++) {
	    context->trusted_retriever_dns =
		add_entry(context->trusted_retriever_dns,
			  tokens[index]);
	    if (context->trusted_retriever_dns == NULL) {
		goto error;
	    }
	}
    }
    else if (strcmp(directive, "default_trusted_retrievers") == 0) {
	for (index=1; tokens[index] != NULL; index++) {
	    context->default_trusted_retriever_dns =
		add_entry(context->default_trusted_retriever_dns,
			  tokens[index]);
	    if (context->default_trusted_retriever_dns == NULL) {
		goto error;
	    }
	}
    }
    else if (strcmp(directive, "passphrase_policy_program") == 0) {
	context->passphrase_policy_pgm = strdup(tokens[1]);
    }
    else if (strcmp(directive, "max_proxy_lifetime") == 0) {
	context->max_proxy_lifetime = 60*60*atoi(tokens[1]);
    }
    else if (strcmp(directive, "max_cred_lifetime") == 0) {
	context->max_cred_lifetime = 60*60*atoi(tokens[1]);
    }
    else if (strcmp(directive, "ignore_globus_limited_proxy_flag") == 0) {
        if ((!strcasecmp(tokens[1], "true")) ||
            (!strcasecmp(tokens[1], "enabled")) ||
            (!strcasecmp(tokens[1], "yes")) ||
            (!strcasecmp(tokens[1], "on")) ||
            (!strcmp(tokens[1], "1"))) {
            context->limited_proxy = -1;
        }
    }
    else if (strcmp(directive, "allow_self_authorization") == 0) {
        if ((!strcasecmp(tokens[1], "true")) ||
            (!strcasecmp(tokens[1], "enabled")) ||
            (!strcasecmp(tokens[1], "yes")) ||
            (!strcasecmp(tokens[1], "on")) ||
            (!strcmp(tokens[1], "1"))) {
            context->allow_self_authz = 1;
        }
    }
    else if (strcmp(directive, "cert_dir") == 0) {
	context->cert_dir = strdup(tokens[1]);
    }
    else if (strcmp(directive, "pam") == 0) {
	context->pam_policy = strdup(tokens[1]);
    }
    else if (strcmp(directive, "pam_id") == 0) {
	context->pam_id = strdup(tokens[1]);
    }
    else if (strcmp(directive, "sasl") == 0) {
	context->sasl_policy = strdup(tokens[1]);
    }
#if defined(HAVE_LIBSASL2)
    else if (strcmp(directive, "sasl_mech") == 0) {
	myproxy_sasl_mech = strdup(tokens[1]);
    }
    else if (strcmp(directive, "sasl_serverFQDN") == 0) {
	myproxy_sasl_serverFQDN = strdup(tokens[1]);
    }
    else if (strcmp(directive, "sasl_user_realm") == 0) {
	myproxy_sasl_user_realm = strdup(tokens[1]);
    }
#endif

    /* these were added to support the online CA functionality */
    else if (strcmp(directive, "certificate_issuer_program") == 0) {
	context->certificate_issuer_program = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_issuer_cert") == 0) {
	context->certificate_issuer_cert = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_issuer_key") == 0) {
	context->certificate_issuer_key = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_issuer_hashalg") == 0) {
        OpenSSL_add_all_digests();
        context->certificate_hashalg = EVP_get_digestbyname(tokens[1]);
        if (context->certificate_hashalg == NULL) {
            verror_put_string("Unknown or unsupported certificate_issuer_hashalg (%s)", tokens[1]);
            goto error;
        }
        myproxy_debug("certificate_issuer_hashalg is %s\n",
                      OBJ_nid2ln(EVP_MD_type((const EVP_MD *)context->certificate_hashalg)));
    }
    else if (strcmp(directive, "certificate_request_checker") == 0) {
	context->certificate_request_checker = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_issuer_checker") == 0) {
	context->certificate_issuer_checker = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_issuer_key_passphrase") == 0) {
	context->certificate_issuer_key_passphrase = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_issuer_subca_certfile") == 0) {
	context->certificate_issuer_subca_certfile = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_openssl_engine_id") == 0) {
        context->certificate_openssl_engine_id = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_openssl_engine_lockfile") == 0) {
        context->certificate_openssl_engine_lockfile = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_openssl_engine_pre") == 0) {
        for (index=1; tokens[index] != NULL; index++) {
            context->certificate_openssl_engine_pre =
                add_entry(context->certificate_openssl_engine_pre,
                          tokens[index]);
            if (context->certificate_openssl_engine_pre == NULL) {
                goto error;
            }
        }
    }
    else if (strcmp(directive, "certificate_openssl_engine_post") == 0) {
        for (index=1; tokens[index] != NULL; index++) {
            context->certificate_openssl_engine_post =
                add_entry(context->certificate_openssl_engine_post,
                          tokens[index]);
            if (context->certificate_openssl_engine_post == NULL) {
                goto error;
            }
        }
    }
    else if (strcmp(directive, "certificate_issuer_email_domain") == 0) {
	context->certificate_issuer_email_domain = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_extfile") == 0) {
	context->certificate_extfile = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_extapp") == 0) {
	context->certificate_extapp = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_mapfile") == 0) {
	context->certificate_mapfile = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_mapapp") == 0) {
	context->certificate_mapapp = strdup(tokens[1]);
    }
    else if (strcmp(directive, "max_cert_lifetime") == 0) {
	context->max_cert_lifetime = 60*60*atoi(tokens[1]);
    }
    else if (strcmp(directive, "min_keylen") == 0) {
	context->min_keylen = atoi(tokens[1]);
    }
    else if (strcmp(directive, "certificate_serialfile") == 0) {
	context->certificate_serialfile = strdup(tokens[1]);
    }
    else if (strcmp(directive, "certificate_serial_skip") == 0) {
	context->certificate_serial_skip = atoi(tokens[1]);
    }
    else if (strcmp(directive, "certificate_out_dir") == 0) {
	context->certificate_out_dir = strdup(tokens[1]);
    }

    /* added for username-to-dn ldap support for internal CA */
    else if (strcmp(directive, "ca_ldap_server") == 0) {
	context->ca_ldap_server = strdup(tokens[1]);
    }
    else if (strcmp(directive, "ca_ldap_searchbase") == 0) {
	context->ca_ldap_searchbase = strdup(tokens[1]);
    }
    else if (strcmp(directive, "ca_ldap_connect_dn") == 0) {
	context->ca_ldap_connect_dn = strdup(tokens[1]);
    }
    else if (strcmp(directive, "ca_ldap_connect_passphrase") == 0) {
	context->ca_ldap_connect_passphrase = strdup(tokens[1]);
    }
    else if (strcmp(directive, "ca_ldap_uid_attribute") == 0) {
	context->ca_ldap_uid_attribute = strdup(tokens[1]);
    }
    else if (strcmp(directive, "ca_ldap_dn_attribute") == 0) {
	context->ca_ldap_dn_attribute = strdup(tokens[1]);
    }
    else if (strcmp(directive, "ca_ldap_start_tls") == 0) {
        if ((!strcasecmp(tokens[1], "true")) ||
            (!strcasecmp(tokens[1], "enabled")) ||
            (!strcasecmp(tokens[1], "yes")) ||
            (!strcasecmp(tokens[1], "on")) ||
            (!strcmp(tokens[1], "1"))) {
            context->ca_ldap_start_tls = 1;
        }
    }

    /* added by Terry Fleury to support web portal security */
    else if (strcmp(directive, "accepted_credentials_mapfile") == 0) {
        context->accepted_credentials_mapfile = strdup(tokens[1]);
    }
    else if (strcmp(directive, "accepted_credentials_mapapp") == 0) {
        context->accepted_credentials_mapapp = strdup(tokens[1]);
    }
    else if (strcmp(directive, "check_multiple_credentials") == 0) {
        context->check_multiple_credentials = 0;
        if ((!strcasecmp(tokens[1], "true")) ||
            (!strcasecmp(tokens[1], "enabled")) ||
            (!strcasecmp(tokens[1], "yes")) ||
            (!strcasecmp(tokens[1], "on")) ||
            (!strcmp(tokens[1], "1"))) {
            context->check_multiple_credentials = 1;
        }
    }

    /* OCSP stuff */
    else if (strcmp(directive, "ocsp_policy") == 0) {
#if defined(HAVE_OCSP)
        myproxy_ocsp_set_policy(tokens[1]);
#else
        verror_put_string("OCSP is configured in myproxy-server.config but the myproxy-server is linked with OpenSSL without OCSP support.");
        goto error;
#endif
    }
    else if (strcmp(directive, "ocsp_responder_url") == 0) {
#if defined(HAVE_OCSP)
        myproxy_ocsp_set_responder(tokens[1]);
#else
        verror_put_string("OCSP is configured in myproxy-server.config but the myproxy-server is linked with OpenSSL without OCSP support.");
        goto error;
#endif
    }
    else if (strcmp(directive, "ocsp_responder_cert") == 0) {
#if defined(HAVE_OCSP)
        myproxy_ocsp_set_responder_cert(tokens[1]);
#else
        verror_put_string("OCSP is configured in myproxy-server.config but the myproxy-server is linked with OpenSSL without OCSP support.");
        goto error;
#endif
    }

    /* added by Terry Fleury for enhanced logging */
    else if (strcmp(directive, "syslog_ident") == 0) {
        context->syslog_ident = strdup(tokens[1]);
    }
    else if (strcmp(directive, "syslog_facility") == 0) {
        context->syslog_facility = decode_facility(tokens[1]);
    }

    else if (strcmp(directive, "slave_servers") == 0) {
        /* ignore. used by myproxy-replicate. */
    }

    else if (strcmp(directive, "request_timeout") == 0) {
	context->request_timeout = atoi(tokens[1]);
    }

    else if (strcmp(directive, "request_size_limit") == 0) {
	context->request_size_limit = atoi(tokens[1]);
    }

    else if (strcmp(directive, "proxy_extfile") == 0) {
#if defined(HAVE_GLOBUS_GSI_PROXY_HANDLE_SET_EXTENSIONS)
        context->proxy_extfile = strdup(tokens[1]);
#else
        verror_put_string("proxy_extfile is configured in myproxy-server.config but the myproxy-server is linked with GSI libraries (prior to GT 4.2.0) without extension support.");
        goto error;
#endif
    }
    else if (strcmp(directive, "proxy_extapp") == 0) {
#if defined(HAVE_GLOBUS_GSI_PROXY_HANDLE_SET_EXTENSIONS)
        context->proxy_extapp = strdup(tokens[1]);
#else
        verror_put_string("proxy_extapp is configured in myproxy-server.config but the myproxy-server is linked with GSI libraries (prior to GT 4.2.0) without extension support.");
        goto error;
#endif
    }
    else if ((strcmp(directive, "disable_usage_stats") == 0) ||
             (strcmp(directive, "usage_stats_target") == 0)) {
        myproxy_log("warning: usage statistics collection is no longer supported. Directive (%s) in myproxy-server.config will be ignored.", directive);
    }
#ifdef HAVE_VOMS
    else if (strcmp(directive, "voms_userconf") == 0) {
        context->voms_userconf = strdup(tokens[1]);
    }
    else if (strcmp(directive, "allow_voms_attribute_requests") == 0) {
        if ((!strcasecmp(tokens[1], "true")) ||
            (!strcasecmp(tokens[1], "enabled")) ||
            (!strcasecmp(tokens[1], "yes")) ||
            (!strcasecmp(tokens[1], "on")) ||
            (!strcmp(tokens[1], "1"))) {
            context->allow_voms_attribute_requests = 1;
        }
    }
#endif
    else {
	myproxy_log("warning: unknown directive (%s) in myproxy-server.config",
		    directive);
    }

    return_code = 0;
    
  error:
    return return_code;
}

/*
 * regex_compare()
 *
 * Does string match regex?
 *
 * Returns 1 if match, 0 if they don't and -1 on error setting verror.
 */
static int
regex_compare(const char *regex,
	      const char *string)
{
    int			result;

#ifndef NO_REGEX_SUPPORT
    char 		*buf;
    char		*bufp;
    int			escaped = 0;

    myproxy_debug("REGEX (%s), STRING (%s)", regex?:"NULL", string?:"NULL");

    /*
     * First we convert the regular expression from the human-readable
     * form (e.g. *.domain.com) to the machine-readable form
     * (e.g. ^.*\.domain\.com$).
     *
     * Make a buffer large enough to hold the largest possible converted
     * regex from the string plus our extra characters (two at the
     * beginning, two at the end, plus a NULL).
     */
    buf = (char *) malloc(2 * strlen(regex) + 5);

    if (!buf)
    {
	verror_put_errno(errno);
	verror_put_string("malloc() failed");
	return -1;
    }

    bufp = buf;
    *bufp++ = '^';
    *bufp++ = '(';

    while (*regex)
    {

	switch(*regex)
	{

	case '*':
	    /* unescaped '*' turns into '.*' */
	    if (!escaped)
		*bufp++ = '.';
	    *bufp++ = '*';
	    escaped = 0;
	    break;

	case '?':
	    /* unescaped '?' turns into '.' */
	    if (!escaped)
		*bufp++ = '.';
	    else
		*bufp++ = '?';
	    escaped = 0;
	    break;

	case '\\':
	    /* '\' escapes the succeeding character */
	    if (!escaped)
		escaped = 1;
	    else {
		*bufp++ = '\\';
		escaped = 0;
	    }
	    break;

	case '.':
	    /* unescaped '.' turns into '\.' */
	    if (!escaped)
		*bufp++ = '\\';
	    *bufp++ = '.';
	    escaped = 0;
	    break;

	default:
	    if (escaped)
		*bufp++ = '\\';
	    *bufp++ = *regex;
	    escaped = 0;
	}

	regex++;
    }

    *bufp++ = ')';
    *bufp++ = '$';
    *bufp++ = '\0';
    myproxy_debug("TRANSLATED ERE (%s)", buf);

#ifdef HAVE_REGCOMP
    {
        regex_t preg = { 0 };

	if (regcomp(&preg, buf, REG_EXTENDED))
	{
	    verror_put_string("Error parsing string \"%s\"",
			      regex);
	    /* Non-fatal error, just indicate failure to match */
	    result = 0;
	}
	else
	{
	    result = (regexec(&preg, string, 0, NULL, 0) == 0);
	    regfree(&preg);
	}
    }

#elif HAVE_COMPILE
    {
	char *expbuf;

	expbuf = compile(buf, NULL, NULL);

	if (!expbuf)
	{
	    verror_put_string("Error parsing string \"%s\"",
			      regex);
	    /* Non-fatal error, just indicate failure to match */
	    result = 0;

	} else {
	    result = step(string, expbuf);
	    free(expbuf);
	}
    }
#else

    /*
     * If we've gotten here then there is an error in the configuration
     * process or this file's #ifdefs
     */
    error -  No regular expression support found.

#endif

    if (buf)
	free(buf);

#else /* NOREGEX_SUPPORT */

    /* No regular expression support */
    result = (strcmp(regex, string) == 0);

#endif /* NO_REGEX_SUPPORT */

    return result;

}


/*
 * name_in_list_matches_policy()
 *
 * Is there a name in the given list that matches a regular expression.
 *
 * Returns 1 if there is, 0 if there isn't, -1 on error setting verror.
 */
static int
name_in_list_matches_policy(const char **list,
		const char *policy)
{
    int return_code = -1;

    assert(policy != NULL);
    
    if (list == NULL)
    {
	/* Empty list */
	return_code = 0;
	goto done;
    }

    while (*list != NULL)
    {
	int rc;

	  rc = regex_compare(policy, *list);
	
	if (rc != 0)
	{
	    return_code = rc;
	    goto done;
	}
	
	list++;
    }
    
    /* If we got here we failed to find the name in the list */
    return_code = 0;

  done:
    return return_code;
}

static int
check_config(myproxy_server_context_t *context)
{
    int rval = 0;

    if (!context->accepted_credential_dns) {
	myproxy_debug("accepted_credentials not set.");
	myproxy_debug("server will not allow clients to store credentials.");
    }
    if (!context->authorized_retriever_dns) {
	myproxy_debug("authorized_retrievers not set.");
	myproxy_debug("server will not allow clients to retrieve credentials.");
    }
    if (!context->authorized_renewer_dns) {
	myproxy_debug("authorized_renewers not set.");
	myproxy_debug("server will not allow clients to renew credentials.");
    }
    if (!context->authorized_key_retrievers_dns) {
	myproxy_debug("authorized_key_retrievers not set.");
	myproxy_debug("server will not allow clients to retrieve keys.");
    }
    if (context->allow_self_authz) {
        myproxy_debug("allow_self_authorization is enabled");
    }
#ifdef HAVE_VOMS
    if (context->allow_voms_attribute_requests) {
      myproxy_debug("allow_voms_attribute_requests is set.");
      myproxy_debug("VOMS attributes will be included on request.");
    } else {
      myproxy_debug("allow_voms_attribute_requests is not set.");
      myproxy_debug("VOMS attribute requests will be ignored.");
    }
#endif    
    if (context->trusted_retriever_dns &&
        !strcmp(context->trusted_retriever_dns[0], "*")) {
        if (!context->default_trusted_retriever_dns) {
            verror_put_string("unsafe policy: trusted_retrievers is * but default_trusted_retrievers is not set.");
            verror_put_string("please consult myproxy-server.config(5) man page.");
            rval = -1;
        } else if (!strcmp(context->default_trusted_retriever_dns[0], "*")) {
            verror_put_string("unsafe policy: trusted_retrievers and default_trusted_retrievers are both *.");
            verror_put_string("please consult myproxy-server.config(5) man page.");
            rval = -1;
        }
    }
    if (context->passphrase_policy_pgm) {
	if (access(context->passphrase_policy_pgm, X_OK) < 0) {
	    verror_put_string("passphrase_policy_pgm %s not executable",
			      context->passphrase_policy_pgm);
	    verror_put_errno(errno);
	    rval = -1;
	} else {
	    myproxy_log("passphrase policy checking enabled: %s",
			context->passphrase_policy_pgm);
	}
    }
    if (context->max_proxy_lifetime) {
	myproxy_log("max_proxy_lifetime: %d seconds",
		    context->max_proxy_lifetime);
    }
    if (context->max_cred_lifetime) {
	myproxy_log("max_cred_lifetime: %d seconds",
		    context->max_cred_lifetime);
    }
    if (context->pam_policy &&
	(!strcmp(context->pam_policy, "required") ||
	 (strcmp(context->pam_policy, "sufficient")))) {
#if defined(HAVE_LIBPAM)
	myproxy_log("PAM enabled, policy %s", context->pam_policy);
    if (geteuid()) {
        myproxy_log("warning: PAM is enabled in myproxy-server.config but the myproxy-server is running as non-root. Some PAM modules won't work as non-root.");
    }
#else
    verror_put_string("PAM is configured in myproxy-server.config but the myproxy-server is not linked with PAM libraries.");
    rval = -1;
#endif
    }
    if (context->sasl_policy &&
	(!strcmp(context->sasl_policy, "required") ||
	 (strcmp(context->sasl_policy, "sufficient")))) {
#if defined(HAVE_LIBSASL2)
	myproxy_log("SASL enabled, policy %s", context->sasl_policy);
#else
    verror_put_string("SASL is configured in myproxy-server.config but the myproxy-server is not linked with SASL libraries.");
    rval = -1;
#endif
    }
    if (context->certificate_issuer_program && 
        context->certificate_issuer_cert) {
        verror_put_string("both certificate_issuer_program and certificate_issuer_cert defined");
        rval = -1;
    } 
    if (context->certificate_issuer_program) {
	if (access(context->certificate_issuer_program, X_OK) < 0) {
	    verror_put_string("certificate_issuer_program %s not executable",
			      context->certificate_issuer_program);
	    verror_put_errno(errno);
	    rval = -1;
	} else {
	    myproxy_log("CA enabled: %s", context->certificate_issuer_program);
	}
    }
    if (context->certificate_issuer_cert) {
	if (access(context->certificate_issuer_cert, R_OK) < 0) {
	    verror_put_string("certificate_issuer_cert %s unreadable",
			      context->certificate_issuer_cert);
	    verror_put_errno(errno);
	    rval = -1;
	}
	if (context->certificate_openssl_engine_id) {
	   if (!context->certificate_issuer_key) {
	       verror_put_string("certificate_issuer_key not set");
               verror_put_errno(errno);
               rval = -1;
 	    }
	} else {
	    if (access(context->certificate_issuer_key, R_OK) < 0) {
	        verror_put_string("certificate_issuer_key %s unreadable",
	    		          context->certificate_issuer_key);
	        verror_put_errno(errno);
	        rval = -1;
	    }
	}
    if (context->certificate_extfile &&
        context->certificate_extapp) {
        verror_put_string("either certificate_extfile or certificate_extapp can be specified but not both");
        rval = -1;
    }
	if (context->certificate_extfile &&
	    access(context->certificate_extfile, R_OK) < 0) {
	    verror_put_string("certificate_extfile %s not readable",
			      context->certificate_extfile);
	    verror_put_errno(errno);
	    rval = -1;
	}
	if (context->certificate_extapp &&
	    access(context->certificate_extapp, X_OK) < 0) {
	    verror_put_string("certificate_extapp %s not executable",
			      context->certificate_extapp);
	    verror_put_errno(errno);
	    rval = -1;
	}
	if (context->certificate_mapfile &&
	    access(context->certificate_mapfile, R_OK) < 0) {
	    verror_put_string("certificate_mapfile %s not readable",
			      context->certificate_mapfile);
	    verror_put_errno(errno);
	    rval = -1;
	}
	if (context->certificate_mapapp &&
	    access(context->certificate_mapapp, X_OK) < 0) {
	    verror_put_string("certificate_mapapp %s not executable",
			      context->certificate_mapapp);
	    verror_put_errno(errno);
	    rval = -1;
	}
	if (context->certificate_serialfile) {
        int fd;
        fd = open(context->certificate_serialfile, O_RDWR|O_CREAT, 0600);
        if (fd < 0) {
            verror_put_string("certificate_serialfile %s not writeable",
                              context->certificate_serialfile);
            verror_put_errno(errno);
            rval = -1;
        } else {
            close(fd);
        }
	}
    if (context->certificate_serial_skip <= 0) {
        verror_put_string("certificate_serial_skip (%s) <= 0",
                          context->certificate_serial_skip);
        verror_put_errno(errno);
        rval = -1;
    }
	if (context->certificate_out_dir &&
	    access(context->certificate_out_dir, W_OK) < 0) {
	    verror_put_string("certificate_out_dir %s not writeable",
			      context->certificate_out_dir);
	    verror_put_errno(errno);
	    rval = -1;
	}
	if (!rval) {
	    myproxy_log("CA enabled");
	    if (context->max_cert_lifetime) {
		myproxy_log("max certificate lifetime: %d seconds",
			    context->max_cert_lifetime);
	    }
	    if (context->min_keylen) {
		myproxy_log("minimum key length: %d bits",
                    context->min_keylen);
	    }
	    if (context->ca_ldap_server) {
		if (!context->ca_ldap_searchbase) {
		    verror_put_string("ca_ldap_server requires ca_ldap_searchbase");
		    rval = -1;
		}
		if (!context->ca_ldap_uid_attribute) {
		    verror_put_string("ca_ldap_server requires ca_ldap_uid_attribute");
		    rval = -1;
		}
	    }
	}
	if (context->certificate_request_checker &&
        access(context->certificate_request_checker, X_OK) < 0) {
	    verror_put_string("certificate_request_checker %s not executable",
			      context->certificate_request_checker);
	    verror_put_errno(errno);
	    rval = -1;
    }
	if (context->certificate_issuer_checker &&
        access(context->certificate_issuer_checker, X_OK) < 0) {
	    verror_put_string("certificate_issuer_checker %s not executable",
			      context->certificate_issuer_checker);
	    verror_put_errno(errno);
	    rval = -1;
    }
    }
    if (context->accepted_credentials_mapfile) {
        if (access(context->accepted_credentials_mapfile, R_OK) < 0) {
            verror_put_string("accepted_credentials_mapfile %s not readable",
                              context->accepted_credentials_mapfile);
            verror_put_errno(errno);
            rval = -1;
        } else {
            myproxy_log("using accepted_credentials_mapfile %s",
                        context->accepted_credentials_mapfile);
        }
    }
	if (context->accepted_credentials_mapapp &&
	    access(context->accepted_credentials_mapapp, X_OK) < 0) {
	    verror_put_string("accepted_credentials_mapapp %s not executable",
			      context->accepted_credentials_mapapp);
	    verror_put_errno(errno);
	    rval = -1;
	}
    if (context->check_multiple_credentials) {
        myproxy_log("Checking multiple credentials during authorization");
    }

    if (context->proxy_extfile &&
        context->proxy_extapp) {
        verror_put_string("either proxy_extfile or proxy_extapp can be specified but not both");
        rval = -1;
    }
	if (context->proxy_extfile &&
	    access(context->proxy_extfile, R_OK) < 0) {
	    verror_put_string("proxy_extfile %s not readable",
			      context->proxy_extfile);
	    verror_put_errno(errno);
	    rval = -1;
	}
	if (context->proxy_extapp &&
	    access(context->proxy_extapp, X_OK) < 0) {
	    verror_put_string("proxy_extapp %s not executable",
			      context->proxy_extapp);
	    verror_put_errno(errno);
	    rval = -1;
	}
	if (context->cert_dir == NULL)
            myproxy_log("cert_dir not specified in config file, so "
                        "no trustroots will be provided to clients");
        else if (!myproxy_check_cert_dir(context->cert_dir)) {
	    verror_put_string("The trustroots directory %s has failed sanity"
                        " checks.", context->cert_dir);
	    rval = -1;
        }

    return rval;
}


/**********************************************************************
 *
 * API Functions
 *
 */

static const char default_config_file[] = "/etc/myproxy-server.config";

int
myproxy_server_config_read(myproxy_server_context_t *context)
{
    FILE *config_stream = NULL;
    const char *config_open_mode = "r";
    int rc;
    int return_code = -1;

    if (context == NULL) 
    {
	verror_put_errno(EINVAL);
	return -1;
    }
    
    if (context->config_file == NULL) {
	if (access(default_config_file, R_OK) == 0) {
	    context->config_file = strdup(default_config_file);
	    if (context->config_file == NULL) {
		verror_put_string("strdup() failed");
		return -1;
	    }
	} else {
	    char *conf, *GL;
	    GL = getenv("GLOBUS_LOCATION");
	    if (!GL) {
		verror_put_string("$GLOBUS_LOCATION undefined.  "
				  "myproxy-server.config not found.\n");
		return -1;
	    }
	    conf = (char *)malloc(strlen(GL)+strlen(default_config_file)+1);
	    if (!conf) {
		perror("malloc()");
		exit(1);
	    }
	    sprintf(conf, "%s%s", GL, default_config_file);
	    if (access(conf, R_OK) < 0) {
		fprintf(stderr, "%s not found.\n", conf);
		exit(1);
	    }
	    context->config_file = conf;
	}
    }

    config_stream = fopen(context->config_file, config_open_mode);

    if (config_stream == NULL)
    {
	verror_put_errno(errno);
	verror_put_string("opening configuration file \"%s\"",
			  context->config_file);
	goto error;
    }
    myproxy_log("reading configuration file %s", context->config_file);
    
    /* Clear any outstanding error */
    verror_clear();

    /* Clear any existing configuration */
    clear_server_context(context);

    rc = vparse_stream(config_stream,
		       NULL /* Default vparse options */,
		       line_parse_callback,
		       context);
    
    if (rc == -1)
    {
	verror_put_string("Error parsing configuration file %s",
			  context->config_file);
	goto error;
    }

    if (verror_is_error())
    {
	/* Some sort of error occurred during parsing */
	goto error;
    }
    
    return_code = check_config(context);
    
  error:
    if (config_stream != NULL)
    {
	fclose(config_stream);
    }
    
    return return_code;
}


int
myproxy_server_check_policy_list_ext(const char **policy_list, myproxy_server_peer_t *client)
{
    const char *policy;
    int ret;

    if ((policy_list == NULL) || (client == NULL)) {
	return 0;
    }

    while ((policy = *policy_list++) != NULL) {
       ret = myproxy_server_check_policy_ext(policy, client);
       if (ret == 1)
	  return 1;
    }

    return 0;
}

int
myproxy_server_check_policy_list(const char **dn_list, const char *client)
{
   myproxy_server_peer_t peer;
   
   memset(&peer, 0, sizeof(peer));
   strncpy(peer.name, client, sizeof(peer.name)-1);
   return myproxy_server_check_policy_list_ext(dn_list, &peer);
}

int
myproxy_server_check_policy_ext(const char *policy, myproxy_server_peer_t *client)
{
    if ((policy == NULL) || (client == NULL)) {
	return 0;
    }

    if (strncasecmp(policy, MYPROXY_SERVER_POLICY_TYPE_FQAN,
	            strlen(MYPROXY_SERVER_POLICY_TYPE_FQAN)) == 0) {
       if (client->fqans == NULL)
	  return 0;
       policy += strlen(MYPROXY_SERVER_POLICY_TYPE_FQAN);
       return name_in_list_matches_policy((const char **)client->fqans, policy);
    } else if (strncasecmp(policy, MYPROXY_SERVER_POLICY_TYPE_SUBJECT,
	     strlen(MYPROXY_SERVER_POLICY_TYPE_SUBJECT)) == 0) {
       policy += strlen(MYPROXY_SERVER_POLICY_TYPE_SUBJECT);
    }

    return regex_compare(policy, client->name);
}

int
myproxy_server_check_policy(const char *dn_regex, const char *client)
{
   myproxy_server_peer_t peer;

   memset(&peer, 0, sizeof(peer));
   strncpy(peer.name, client, sizeof(peer.name)-1);
   return myproxy_server_check_policy_ext(dn_regex, &peer);
}
