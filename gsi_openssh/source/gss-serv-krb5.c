/* $OpenBSD: gss-serv-krb5.c,v 1.9 2018/07/09 21:37:55 markus Exp $ */

/*
 * Copyright (c) 2001-2007 Simon Wilkinson. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"

#ifdef GSSAPI
#ifdef KRB5

#include <sys/types.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "xmalloc.h"
#include "sshkey.h"
#include "hostfile.h"
#include "auth.h"
#include "log.h"
#include "misc.h"
#include "servconf.h"

#include "ssh-gss.h"

extern Authctxt *the_authctxt;
extern ServerOptions options;

#ifdef HEIMDAL
# include <krb5.h>
#endif
#ifdef HAVE_GSSAPI_KRB5_H
# include <gssapi_krb5.h>
#elif HAVE_GSSAPI_GSSAPI_KRB5_H
# include <gssapi/gssapi_krb5.h>
#endif

/* all commands are allowed by default */
char **k5users_allowed_cmds = NULL;

static int ssh_gssapi_k5login_exists();
static int ssh_gssapi_krb5_cmdok(krb5_principal, const char *, const char *,
    int);

static krb5_context krb_context = NULL;
extern int ssh_krb5_kuserok(krb5_context, krb5_principal, const char *, int);

/* Initialise the krb5 library, for the stuff that GSSAPI won't do */

static int
ssh_gssapi_krb5_init(void)
{
	krb5_error_code problem;

	if (krb_context != NULL)
		return 1;

	problem = krb5_init_context(&krb_context);
	if (problem) {
		logit("Cannot initialize krb5 context");
		return 0;
	}

	return 1;
}

/* Check if this user is OK to login. This only works with krb5 - other
 * GSSAPI mechanisms will need their own.
 * Returns true if the user is OK to log in, otherwise returns 0
 */

/* The purpose of the function is to find out if a Kerberos principal is
 * allowed to log in as the given local user. This is a general problem with
 * Kerberized services because by design the Kerberos principals are
 * completely independent from the local user names. This is one of the
 * reasons why Kerberos is working well on different operating systems like
 * Windows and UNIX/Linux. Nevertheless a relationship between a Kerberos
 * principal and a local user name must be established because otherwise every
 * access would be granted for every principal with a valid ticket.
 *
 * Since it is a general issue libkrb5 provides some functions for
 * applications to find out about the relationship between the Kerberos
 * principal and a local user name. They are krb5_kuserok() and
 * krb5_aname_to_localname().
 *
 * krb5_kuserok() can be used to "Determine if a principal is authorized to
 * log in as a local user" (from the MIT Kerberos documentation of this
 * function). Which is exactly what we are looking for and should be the
 * preferred choice. It accepts the Kerberos principal and a local user name
 * and let libkrb5 or its plugins determine if they relate to each other or
 * not.
 *
 * krb5_aname_to_localname() can use used to "Convert a principal name to a
 * local name" (from the MIT Kerberos documentation of this function). It
 * accepts a Kerberos principle and returns a local name and it is up to the
 * application to do any additional checks. There are two issues using
 * krb5_aname_to_localname(). First, since POSIX user names are case
 * sensitive, the calling application in general has no other choice than
 * doing a case-sensitive string comparison between the name returned by
 * krb5_aname_to_localname() and the name used at the login prompt. When the
 * users are provided by a case in-sensitive server, e.g. Active Directory,
 * this might lead to login failures because the user typing the name at the
 * login prompt might not be aware of the right case. Another issue might be
 * caused if there are multiple alias names available for a single user. E.g.
 * the canonical name of a user is user@group.department.example.com but there
 * exists a shorter login name, e.g. user@example.com, to safe typing at the
 * login prompt. Here krb5_aname_to_localname() can only return the canonical
 * name, but if the short alias is used at the login prompt authentication
 * will fail as well. All this can be avoided by using krb5_kuserok() and
 * configuring krb5.conf or using a suitable plugin to meet the needs of the
 * given environment.
 *
 * The Fedora and RHEL version of openssh contain two patches which modify the
 * access control behavior:
 *  - openssh-6.6p1-kuserok.patch
 *  - openssh-6.6p1-force_krb.patch
 *
 * openssh-6.6p1-kuserok.patch adds a new option KerberosUseKuserok for
 * sshd_config which controls if krb5_kuserok() is used to check if the
 * principle is authorized or if krb5_aname_to_localname() should be used.
 * The reason to add this patch was that krb5_kuserok() by default checks if
 * a .k5login file exits in the users home-directory. With this the user can
 * give access to his account for any given principal which might be
 * in violation with company policies and it would be useful if this can be
 * rejected. Nevertheless the patch ignores the fact that krb5_kuserok() does
 * no only check .k5login but other sources as well and checking .k5login can
 * be disabled for all applications in krb5.conf as well. With this new
 * option KerberosUseKuserok set to 'no' (and this is the default for RHEL7
 * and Fedora 21) openssh can only use krb5_aname_to_localname() with the
 * restrictions mentioned above.
 *
 * openssh-6.6p1-force_krb.patch adds a ksu like behaviour to ssh, i.e. when
 * using GSSAPI authentication only commands configured in the .k5user can be
 * executed. Here the wrong assumption that krb5_kuserok() only checks
 * .k5login is made as well. In contrast ksu checks .k5login directly and
 * does not use krb5_kuserok() which might be more useful for the given
 * purpose. Additionally this patch is not synced with
 * openssh-6.6p1-kuserok.patch.
 *
 * The current patch tries to restore the usage of krb5_kuserok() so that e.g.
 * localauth plugins can be used. It does so by adding a forth parameter to
 * ssh_krb5_kuserok() which indicates whether .k5login exists or not. If it
 * does not exists krb5_kuserok() is called even if KerberosUseKuserok is set
 * to 'no' because the intent of the option is to not check .k5login and if it
 * does not exists krb5_kuserok() returns a result without checking .k5login.
 * If .k5login does exists and KerberosUseKuserok is 'no' we fall back to
 * krb5_aname_to_localname(). This is in my point of view an acceptable
 * limitation and does not break the current behaviour.
 *
 * Additionally with this patch ssh_krb5_kuserok() is called in
 * ssh_gssapi_krb5_cmdok() instead of only krb5_aname_to_localname() is
 * neither .k5login nor .k5users exists to allow plugin evaluation via
 * krb5_kuserok() as well.
 *
 * I tried to keep the patch as minimal as possible, nevertheless I see some
 * areas for improvement which, if they make sense, have to be evaluated
 * carefully because they might change existing behaviour and cause breaks
 * during upgrade:
 * - I wonder if disabling .k5login usage make sense in sshd or if it should
 *   be better disabled globally in krb5.conf
 * - if really needed openssh-6.6p1-kuserok.patch should be fixed to really
 *   only disable checking .k5login and maybe .k5users
 * - the ksu behaviour should be configurable and maybe check the .k5login and
 *   .k5users files directly like ksu itself does
 * - to make krb5_aname_to_localname() more useful an option for sshd to use
 *   the canonical name (the one returned by getpwnam()) instead of the name
 *   given at the login prompt might be useful */

static int
ssh_gssapi_krb5_userok(ssh_gssapi_client *client, char *name)
{
	krb5_principal princ;
	int retval;
	const char *errmsg;
	int k5login_exists;

	if (ssh_gssapi_krb5_init() == 0)
		return 0;

	if ((retval = krb5_parse_name(krb_context, client->exportedname.value,
	    &princ))) {
		errmsg = krb5_get_error_message(krb_context, retval);
		logit("krb5_parse_name(): %.100s", errmsg);
		krb5_free_error_message(krb_context, errmsg);
		return 0;
	}
	/* krb5_kuserok() returns 1 if .k5login DNE and this is self-login.
	 * We have to make sure to check .k5users in that case. */
	k5login_exists = ssh_gssapi_k5login_exists();
	/* NOTE: .k5login and .k5users must opened as root, not the user,
	 * because if they are on a krb5-protected filesystem, user credentials
	 * to access these files aren't available yet. */
	if (ssh_krb5_kuserok(krb_context, princ, name, k5login_exists)
			&& k5login_exists) {
		retval = 1;
		logit("Authorized to %s, krb5 principal %s (krb5_kuserok)",
		    name, (char *)client->displayname.value);
	} else if (ssh_gssapi_krb5_cmdok(princ, client->exportedname.value,
		name, k5login_exists)) {
		retval = 1;
		logit("Authorized to %s, krb5 principal %s "
		    "(ssh_gssapi_krb5_cmdok)",
		    name, (char *)client->displayname.value);
	} else
		retval = 0;

	krb5_free_principal(krb_context, princ);
	return retval;
}

/* Test for existence of .k5login.
 * We need this as part of our .k5users check, because krb5_kuserok()
 * returns success if .k5login DNE and user is logging in as himself.
 * With .k5login absent and .k5users present, we don't want absence
 * of .k5login to authorize self-login.  (absence of both is required)
 * Returns 1 if .k5login is available, 0 otherwise.
 */
static int
ssh_gssapi_k5login_exists()
{
	char file[MAXPATHLEN];
	struct passwd *pw = the_authctxt->pw;
	char *k5login_directory = NULL;
	int ret = 0;

	ret = ssh_krb5_get_k5login_directory(krb_context, &k5login_directory);
	debug3_f("k5login_directory = %s (rv=%d)", k5login_directory, ret);
	if (k5login_directory == NULL || ret != 0) {
		/* If not set, the library will look for  k5login
		 * files in the user's home directory, with the filename  .k5login.
		 */
		snprintf(file, sizeof(file), "%s/.k5login", pw->pw_dir);
	} else {
		/* If set, the library will look for a local user's k5login file
		 * within the named directory, with a filename corresponding to the
		 * local username.
		 */
		snprintf(file, sizeof(file), "%s%s%s", k5login_directory, 
			k5login_directory[strlen(k5login_directory)-1] != '/' ? "/" : "",
			pw->pw_name);
	}
	debug_f("Checking existence of file %s", file);

	return access(file, F_OK) == 0;
}

/* check .k5users for login or command authorization
 * Returns 1 if principal is authorized, 0 otherwise.
 * If principal is authorized, (global) k5users_allowed_cmds may be populated.
 */
static int
ssh_gssapi_krb5_cmdok(krb5_principal principal, const char *name,
    const char *luser, int k5login_exists)
{
	FILE *fp;
	char file[MAXPATHLEN];
	char *line = NULL;
	struct stat st;
	struct passwd *pw = the_authctxt->pw;
	int found_principal = 0;
	int ncommands = 0, allcommands = 0;
	u_long linenum = 0;
	size_t linesize = 0;

	snprintf(file, sizeof(file), "%s/.k5users", pw->pw_dir);
	/* If both .k5login and .k5users DNE, self-login is ok. */
	if ( !options.enable_k5users || (!k5login_exists && (access(file, F_OK) == -1))) {
                return ssh_krb5_kuserok(krb_context, principal, luser,
                                        k5login_exists);
	}
	if ((fp = fopen(file, "r")) == NULL) {
		int saved_errno = errno;
		/* 2nd access check to ease debugging if file perms are wrong.
		 * But we don't want to report this if .k5users simply DNE. */
		if (access(file, F_OK) == 0) {
			logit("User %s fopen %s failed: %s",
			    pw->pw_name, file, strerror(saved_errno));
		}
		return 0;
	}
	/* .k5users must be owned either by the user or by root */
	if (fstat(fileno(fp), &st) == -1) {
		/* can happen, but very wierd error so report it */
		logit("User %s fstat %s failed: %s",
		    pw->pw_name, file, strerror(errno));
		fclose(fp);
		return 0;
	}
	if (!(st.st_uid == pw->pw_uid || st.st_uid == 0)) {
		logit("User %s %s is not owned by root or user",
		    pw->pw_name, file);
		fclose(fp);
		return 0;
	}
	/* .k5users must be a regular file.  krb5_kuserok() doesn't do this
	  * check, but we don't want to be deficient if they add a check. */
	if (!S_ISREG(st.st_mode)) {
		logit("User %s %s is not a regular file", pw->pw_name, file);
		fclose(fp);
		return 0;
	}
	/* file exists; initialize k5users_allowed_cmds (to none!) */
	k5users_allowed_cmds = xcalloc(++ncommands,
	    sizeof(*k5users_allowed_cmds));

	/* Check each line.  ksu allows unlimited length lines. */
	while (!allcommands && getline(&line, &linesize, fp) != -1) {
		linenum++;
		char *token;

		/* we parse just like ksu, even though we could do better */
		if ((token = strtok(line, " \t\n")) == NULL)
			continue;
		if (strcmp(name, token) == 0) {
			/* we matched on client principal */
			found_principal = 1;
			if ((token = strtok(NULL, " \t\n")) == NULL) {
				/* only shell is allowed */
				k5users_allowed_cmds[ncommands-1] =
				    xstrdup(pw->pw_shell);
				k5users_allowed_cmds =
				    xreallocarray(k5users_allowed_cmds, ++ncommands,
					sizeof(*k5users_allowed_cmds));
				break;
			}
			/* process the allowed commands */
			while (token) {
				if (strcmp(token, "*") == 0) {
					allcommands = 1;
					break;
				}
				k5users_allowed_cmds[ncommands-1] =
				    xstrdup(token);
				k5users_allowed_cmds =
				    xreallocarray(k5users_allowed_cmds, ++ncommands,
					sizeof(*k5users_allowed_cmds));
				token = strtok(NULL, " \t\n");
			}
		}
       }
	free(line);
	if (k5users_allowed_cmds) {
		/* terminate vector */
		k5users_allowed_cmds[ncommands-1] = NULL;
		/* if all commands are allowed, free vector */
		if (allcommands) {
			int i;
			for (i = 0; i < ncommands; i++) {
				free(k5users_allowed_cmds[i]);
			}
			free(k5users_allowed_cmds);
			k5users_allowed_cmds = NULL;
		}
	}
	fclose(fp);
	return found_principal;
}
 
/* Retrieve the local username associated with a set of Kerberos
 * credentials. Hopefully we can use this for the 'empty' username
 * logins discussed in the draft  */
static int
ssh_gssapi_krb5_localname(ssh_gssapi_client *client, char **user) {
	krb5_principal princ;
	int retval;

	if (ssh_gssapi_krb5_init() == 0)
		return 0;

	if ((retval=krb5_parse_name(krb_context, client->displayname.value,
	    &princ))) {
		logit("krb5_parse_name(): %.100s",
		    krb5_get_err_text(krb_context,retval));
		return 0;
	}

	/* We've got to return a malloc'd string */
	*user = (char *)xmalloc(256);
	if (krb5_aname_to_localname(krb_context, princ, 256, *user)) {
		free(*user);
		*user = NULL;
		return(0);
	}

	return(1);
}

/* This writes out any forwarded credentials from the structure populated
 * during userauth. Called after we have setuid to the user */

static int
ssh_gssapi_krb5_storecreds(ssh_gssapi_client *client)
{
	krb5_ccache ccache;
	krb5_error_code problem;
	krb5_principal princ;
	OM_uint32 maj_status, min_status;
	const char *new_ccname, *new_cctype;
	const char *errmsg;
	int set_env = 0;

	if (client->creds == NULL) {
		debug("No credentials stored");
		return 0;
	}

	if (ssh_gssapi_krb5_init() == 0)
		return 0;

#ifdef HEIMDAL
# ifdef HAVE_KRB5_CC_NEW_UNIQUE
	if ((problem = krb5_cc_new_unique(krb_context, krb5_fcc_ops.prefix,
	    NULL, &ccache)) != 0) {
		errmsg = krb5_get_error_message(krb_context, problem);
		logit("krb5_cc_new_unique(): %.100s", errmsg);
# else
	if ((problem = krb5_cc_gen_new(krb_context, &krb5_fcc_ops, &ccache))) {
	    logit("krb5_cc_gen_new(): %.100s",
		krb5_get_err_text(krb_context, problem));
# endif
		krb5_free_error_message(krb_context, errmsg);
		return 0;
	}
#else
	if ((problem = ssh_krb5_cc_new_unique(krb_context, &ccache, &set_env)) != 0) {
		errmsg = krb5_get_error_message(krb_context, problem);
		logit("ssh_krb5_cc_new_unique(): %.100s", errmsg);
		krb5_free_error_message(krb_context, errmsg);
		return 0;
	}
#endif	/* #ifdef HEIMDAL */

	if ((problem = krb5_parse_name(krb_context,
	    client->exportedname.value, &princ))) {
		errmsg = krb5_get_error_message(krb_context, problem);
		logit("krb5_parse_name(): %.100s", errmsg);
		krb5_free_error_message(krb_context, errmsg);
		return 0;
	}

	if ((problem = krb5_cc_initialize(krb_context, ccache, princ))) {
		errmsg = krb5_get_error_message(krb_context, problem);
		logit("krb5_cc_initialize(): %.100s", errmsg);
		krb5_free_error_message(krb_context, errmsg);
		krb5_free_principal(krb_context, princ);
		krb5_cc_destroy(krb_context, ccache);
		return 0;
	}

	krb5_free_principal(krb_context, princ);

	if ((maj_status = gss_krb5_copy_ccache(&min_status,
	    client->creds, ccache))) {
		logit("gss_krb5_copy_ccache() failed");
		krb5_cc_destroy(krb_context, ccache);
		return 0;
	}

	new_cctype = krb5_cc_get_type(krb_context, ccache);
	new_ccname = krb5_cc_get_name(krb_context, ccache);
	xasprintf(&client->store.envval, "%s:%s", new_cctype, new_ccname);

	if (set_env) {
		client->store.envvar = "KRB5CCNAME";
	}
	if ((strcmp(new_cctype, "FILE") == 0) || (strcmp(new_cctype, "DIR") == 0))
		client->store.filename = xstrdup(new_ccname);

#ifdef USE_PAM
	if (options.use_pam && set_env)
		do_pam_putenv(client->store.envvar, client->store.envval);
#endif

	krb5_cc_close(krb_context, ccache);

	client->store.data = krb_context;

	return set_env;
}

static int
ssh_gssapi_krb5_updatecreds(ssh_gssapi_ccache *store,
    ssh_gssapi_client *client)
{
	krb5_ccache ccache = NULL;
	krb5_principal principal = NULL;
	char *name = NULL;
	krb5_error_code problem;
	OM_uint32 maj_status, min_status;

	if ((problem = krb5_cc_resolve(krb_context, store->envval, &ccache))) {
                logit("krb5_cc_resolve(): %.100s",
                    krb5_get_err_text(krb_context, problem));
                return 0;
	}

	/* Find out who the principal in this cache is */
	if ((problem = krb5_cc_get_principal(krb_context, ccache,
	    &principal))) {
		logit("krb5_cc_get_principal(): %.100s",
		    krb5_get_err_text(krb_context, problem));
		krb5_cc_close(krb_context, ccache);
		return 0;
	}

	if ((problem = krb5_unparse_name(krb_context, principal, &name))) {
		logit("krb5_unparse_name(): %.100s",
		    krb5_get_err_text(krb_context, problem));
		krb5_free_principal(krb_context, principal);
		krb5_cc_close(krb_context, ccache);
		return 0;
	}


	if (strcmp(name,client->exportedname.value)!=0) {
		debug("Name in local credentials cache differs. Not storing");
		krb5_free_principal(krb_context, principal);
		krb5_cc_close(krb_context, ccache);
		krb5_free_unparsed_name(krb_context, name);
		return 0;
	}
	krb5_free_unparsed_name(krb_context, name);

	/* Name matches, so lets get on with it! */

	if ((problem = krb5_cc_initialize(krb_context, ccache, principal))) {
		logit("krb5_cc_initialize(): %.100s",
		    krb5_get_err_text(krb_context, problem));
		krb5_free_principal(krb_context, principal);
		krb5_cc_close(krb_context, ccache);
		return 0;
	}

	krb5_free_principal(krb_context, principal);

	if ((maj_status = gss_krb5_copy_ccache(&min_status, client->creds,
	    ccache))) {
		logit("gss_krb5_copy_ccache() failed. Sorry!");
		krb5_cc_close(krb_context, ccache);
		return 0;
	}

	return 1;
}

ssh_gssapi_mech gssapi_kerberos_mech = {
	"toWM5Slw5Ew8Mqkay+al2g==",
	"Kerberos",
	{9, "\x2A\x86\x48\x86\xF7\x12\x01\x02\x02"},
	NULL,
	&ssh_gssapi_krb5_userok,
	&ssh_gssapi_krb5_localname,
	&ssh_gssapi_krb5_storecreds,
	&ssh_gssapi_krb5_updatecreds
};

#endif /* KRB5 */

#endif /* GSSAPI */
