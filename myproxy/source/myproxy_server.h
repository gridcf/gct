/*
 * myproxy_server.h
 *
 * Myproxy server header file
 */
#ifndef __MYPROXY_SERVER_H
#define __MYPROXY_SERVER_H

#define MYPROXY_SERVER_POLICY_TYPE_FQAN "FQAN:"
#define MYPROXY_SERVER_POLICY_TYPE_SUBJECT "SUBJECT:"

extern int errno;

typedef struct myproxy_usage_s {
    int   pam_used;
    int   sasl_used;
    int   cred_pphrase_used;
    int   trusted_retr;
    int   certauthz_used;
    int   pubcookie_used;
    int   ca_used;
    int   credentials_exist;
    int   trustroots_sent;
    char  client_ip[256];
} myproxy_usage_t;


typedef struct myproxy_server_context_s
{
  char *my_name;                    /* My name for logging and such */
  int run_as_daemon;                /* Run as a daemon? */
  char *config_file;                /* configuration file */
  char *pidfile;		    /* pid file */
  char *portfile;		    /* port file */
  char *passphrase_policy_pgm;      /* external program for password check */
  char **accepted_credential_dns;   /* List of creds that can be stored */
  char **authorized_retriever_dns;  /* List of DNs we'll delegate to */
  char **default_retriever_dns;     /* List of DNs we'll delegate to */
  char **trusted_retriever_dns;     /* DNs we'll delegate to w/o passwd */
  char **default_trusted_retriever_dns; /* DNs we'll delegate to w/o pass */
  char **authorized_renewer_dns;    /* List of DNs that can renew creds */
  char **default_renewer_dns; 	    /* List of DNs that can renew creds */
  char **authorized_key_retrievers_dns; /* List of DNs that can retrieve keys */
  char **default_key_retrievers_dns;    /* List of DNs that can retrieve keys */
  int max_proxy_lifetime;	    /* Max life (in seconds) of retrieved creds */
  int max_cred_lifetime;	    /* Max life (in seconds) of stored creds */
  char *cert_dir;		    /* Trusted certificate directory to send */
  char *pam_policy;                 /* How we depend on PAM for passwd auth */
  char *pam_id;                     /* Application name we present to PAM */
  char *sasl_policy;                /* SASL required, sufficient, disabled */
  char *certificate_issuer_program; /* CA callout external program */
  char *certificate_issuer_cert;    /* CA certificate */
  char *certificate_issuer_key;     /* CA signing key */
  const void *certificate_hashalg; /* hash algorithm for issued EECs (EVP_MD *)*/
  char *certificate_request_checker; /* callout for checking certreqs */
  char *certificate_issuer_checker; /* callout for checking issued certs */
  char *certificate_openssl_engine_id;   /* Which OpenSSL engine to use */
  char *certificate_openssl_engine_lockfile; /* synchronize engine calls */
  char **certificate_openssl_engine_pre; /* Which 'pre' commands to use */
  char **certificate_openssl_engine_post;/* Which 'post' commands to use */
  char *certificate_issuer_key_passphrase; /* CA signing key passphrase */
  char *certificate_issuer_subca_certfile; /* Sub-CA certs to be sent with CA-GET */
  char *certificate_issuer_email_domain; /* CA email domain for alt name */
  char *certificate_extfile;        /* CA extension file */
  char *certificate_extapp;         /* CA extension call-out */
  char *certificate_mapfile;        /* CA gridmap file if not the default */
  char *certificate_mapapp;         /* gridmap call-out */
  int   max_cert_lifetime;          /* like proxy_lifetime for the CA */
  int   min_keylen;                 /* minimum keylength for the CA */
  char *certificate_serialfile;     /* path to serialnumber file for CA */
  int   certificate_serial_skip;    /* CA serial number increment */
  char *certificate_out_dir;        /* path to certificate directory */
  char *ca_ldap_server;             /* URL to CA ldap user DN server */
  char *ca_ldap_uid_attribute;      /* Username attribute name */
  char *ca_ldap_searchbase;         /* Search base DN for ldap query */
  char *ca_ldap_connect_dn;         /* Optional connect-as ldap DN */
  char *ca_ldap_connect_passphrase; /* Optional connect-as ldap passphrase */
  char *ca_ldap_dn_attribute;       /* Opt - pull dn from record attr */
  int   ca_ldap_start_tls;          /* Optional LDAP StartTLS */
  char *accepted_credentials_mapfile; /* Force username/userDN gridmap lookup */
  char *accepted_credentials_mapapp;/* gridmap call-out */
  int check_multiple_credentials;   /* Check multiple creds for U/P match */
  char *syslog_ident;               /* Identity for logging to syslog */
  int syslog_facility;              /* syslog facility */
  int limited_proxy;                /* Should we delegate a limited proxy? */
  int request_timeout;              /* Timeout for child processes */
  int request_size_limit;           /* Size limit for incoming requests */
  int allow_self_authz;             /* Allow client subject to match cert? */
  char *proxy_extfile;              /* Extensions for issued proxies */
  char *proxy_extapp;               /* proxy extension call-out */
  int obsolete1;                    /* Obsolete: was disable_usage_stats */
  char *obsolete2;                  /* Obsolete: was usage_stats_target */
  myproxy_usage_t usage;
  int allow_voms_attribute_requests;/* Support VONAME/VOMSES in requests? */
  char *voms_userconf;              /* VOMS confuration file */
} myproxy_server_context_t;

typedef struct myproxy_server_peer_t {
  char name[1024];	/* shouldn't be allocated dynamicaly? */
  char **fqans;
} myproxy_server_peer_t;


/**********************************************************************
 *
 * Routines from myproxy_server_config.c
 *
 */

/*
 * myproxy_server_config_read()
 *
 * Read the configuration file as indicated in the context, parse
 * it and store the results in the context.
 *
 * Returns 0 on success, -1 on error setting verror.
 */
int myproxy_server_config_read(myproxy_server_context_t *context);

/*
 * myproxy_server_clear_context()
 *
 * Re-initialize the myproxy_server_context_t structure,
 * deallocating memory as needed.
 */
void myproxy_server_clear_context(myproxy_server_context_t *context);

/*
 * myproxy_server_check_policy_list()
 *
 * Check to see if the given client matches an entry the dn_list.
 *
 * Returns 1 if match found, 0 if no match found,
 * -1 on error, setting verror.
 */
int myproxy_server_check_policy_list(const char **dn_list,
				     const char *client_name);

/*
 * myproxy_server_check_policy_list_ext()
 *
 * Same as myproxy_server_check_policy_list() but receives more detailed
 * client description.
 */
int myproxy_server_check_policy_list_ext(const char **dn_list,
					 myproxy_server_peer_t *client);

/*
 * myproxy_server_check_policy()
 *
 * Check to see if the given client matches the dn_regex.
 *
 * Returns 1 if match found, 0 if no match found,
 * -1 on error, setting verror.
 */
int myproxy_server_check_policy(const char *dn_regex,
				const char *client);

/*
 * myproxy_server_check_policy_ext()
 *
 * Same as myproxy_server_check_policy() but receives more detailed client
 * description.
 */
int myproxy_server_check_policy_ext(const char *dn_regex,
				    myproxy_server_peer_t *client);
#endif /* !__MYPROXY_SERVER_H */
