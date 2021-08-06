/*
 * Copyright (c) 2005 Daniel Walsh <dwalsh@redhat.com>
 * Copyright (c) 2014 Petr Lautrbach <plautrba@redhat.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Linux-specific portability code - just SELinux support for sshd at present
 */

#include "includes.h"

#if defined(WITH_SELINUX) || defined(LINUX_OOM_ADJUST)
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "log.h"
#include "xmalloc.h"
#include "misc.h"      /* servconf.h needs misc.h for struct ForwardOptions */
#include "servconf.h"
#include "port-linux.h"
#include "misc.h"
#include "sshkey.h"
#include "hostfile.h"
#include "auth.h"

#ifdef WITH_SELINUX
#include <selinux/selinux.h>
#include <selinux/context.h>
#include <selinux/get_context_list.h>
#include <selinux/get_default_type.h>

#ifdef HAVE_LINUX_AUDIT
#include <libaudit.h>
#include <unistd.h>
#endif

/* Wrapper around is_selinux_enabled() to log its return value once only */
int
sshd_selinux_enabled(void)
{
	static int enabled = -1;

	if (enabled == -1) {
		enabled = (is_selinux_enabled() == 1);
		debug("SELinux support %s", enabled ? "enabled" : "disabled");
	}

	return (enabled);
}

/* Send audit message */
static int
sshd_selinux_send_audit_message(int success, security_context_t default_context,
		       security_context_t selected_context)
{
	int rc=0;
#ifdef HAVE_LINUX_AUDIT
	char *msg = NULL;
	int audit_fd = audit_open();
	security_context_t default_raw=NULL;
	security_context_t selected_raw=NULL;
	rc = -1;
	if (audit_fd < 0) {
		if (errno == EINVAL || errno == EPROTONOSUPPORT ||
					errno == EAFNOSUPPORT)
				return 0; /* No audit support in kernel */
		error("Error connecting to audit system.");
		return rc;
	}
	if (selinux_trans_to_raw_context(default_context, &default_raw) < 0) {
		error("Error translating default context.");
		default_raw = NULL;
	}
	if (selinux_trans_to_raw_context(selected_context, &selected_raw) < 0) {
		error("Error translating selected context.");
		selected_raw = NULL;
	}
	if (asprintf(&msg, "sshd: default-context=%s selected-context=%s",
		     default_raw ? default_raw : (default_context ? default_context: "?"),
		     selected_context ? selected_raw : (selected_context ? selected_context :"?")) < 0) {
		error("Error allocating memory.");
		goto out;
	}
	if (audit_log_user_message(audit_fd, AUDIT_USER_ROLE_CHANGE,
				   msg, NULL, NULL, NULL, success) <= 0) {
		error("Error sending audit message.");
		goto out;
	}
	rc = 0;
      out:
	free(msg);
	freecon(default_raw);
	freecon(selected_raw);
	close(audit_fd);
#endif
	return rc;
}

static int
mls_range_allowed(security_context_t src, security_context_t dst)
{
	struct av_decision avd;
	int retval;
	access_vector_t bit;
	security_class_t class;

	debug_f("src:%s dst:%s", src, dst);
	class = string_to_security_class("context");
	if (!class) {
		error("string_to_security_class failed to translate security class context");
		return 1;
	}
	bit = string_to_av_perm(class, "contains");
	if (!bit) {
		error("string_to_av_perm failed to translate av perm contains");
		return 1;
	}
	retval = security_compute_av(src, dst, class, bit, &avd);
	if (retval || ((bit & avd.allowed) != bit))
		return 0;

	return 1;
}

static int
get_user_context(const char *sename, const char *role, const char *lvl,
	security_context_t *sc) {
#ifdef HAVE_GET_DEFAULT_CONTEXT_WITH_LEVEL
	if (lvl == NULL || lvl[0] == '\0' || get_default_context_with_level(sename, lvl, NULL, sc) != 0) {
	        /* User may have requested a level completely outside of his 
	           allowed range. We get a context just for auditing as the
	           range check below will certainly fail for default context. */
#endif
		if (get_default_context(sename, NULL, sc) != 0) {
			*sc = NULL;
			return -1;
		}
#ifdef HAVE_GET_DEFAULT_CONTEXT_WITH_LEVEL
	}
#endif
	if (role != NULL && role[0]) {
		context_t con;
		char *type=NULL;
		if (get_default_type(role, &type) != 0) {
			error("get_default_type: failed to get default type for '%s'",
				role);
			goto out;
		}
		con = context_new(*sc);
		if (!con) {
			goto out;
		}
		context_role_set(con, role);
		context_type_set(con, type);
		freecon(*sc);
		*sc = strdup(context_str(con));
		context_free(con);
		if (!*sc)
			return -1;
	}
#ifdef HAVE_GET_DEFAULT_CONTEXT_WITH_LEVEL
	if (lvl != NULL && lvl[0]) {
		/* verify that the requested range is obtained */
		context_t con;
		security_context_t obtained_raw;
		security_context_t requested_raw;
		con = context_new(*sc);
		if (!con) {
			goto out;
		}
		context_range_set(con, lvl);
		if (selinux_trans_to_raw_context(*sc, &obtained_raw) < 0) {
			context_free(con);
			goto out;
		}
		if (selinux_trans_to_raw_context(context_str(con), &requested_raw) < 0) {
			freecon(obtained_raw);
			context_free(con);
			goto out;
		}

		debug("get_user_context: obtained context '%s' requested context '%s'",
			obtained_raw, requested_raw);
		if (strcmp(obtained_raw, requested_raw)) {
			/* set the context to the real requested one but fail */
			freecon(requested_raw);
			freecon(obtained_raw);
			freecon(*sc);
			*sc = strdup(context_str(con));
			context_free(con);
			return -1;
		}
		freecon(requested_raw);
		freecon(obtained_raw);
		context_free(con);
	}
#endif
	return 0;
      out:
	freecon(*sc);
	*sc = NULL;
	return -1;
}

static void
ssh_selinux_get_role_level(char **role, const char **level,
    Authctxt *the_authctxt)
{
	*role = NULL;
	*level = NULL;
	if (the_authctxt) {
		if (the_authctxt->role != NULL) {
			char *slash;
			*role = xstrdup(the_authctxt->role);
			if ((slash = strchr(*role, '/')) != NULL) {
				*slash = '\0';
				*level = slash + 1;
			}
		}
	}
}

/* Return the default security context for the given username */
static int
sshd_selinux_getctxbyname(char *pwname, security_context_t *default_sc,
    security_context_t *user_sc, int inetd, Authctxt *the_authctxt)
{
	char *sename, *lvl;
	char *role;
	const char *reqlvl;
	int r = 0;
	context_t con = NULL;

	ssh_selinux_get_role_level(&role, &reqlvl, the_authctxt);

#ifdef HAVE_GETSEUSERBYNAME
	if ((r=getseuserbyname(pwname, &sename, &lvl)) != 0) {
		sename = NULL;
		lvl = NULL;
	}
#else
	sename = pwname;
	lvl = "";
#endif

	if (r == 0) {
#ifdef HAVE_GET_DEFAULT_CONTEXT_WITH_LEVEL
		r = get_default_context_with_level(sename, lvl, NULL, default_sc);
#else
		r = get_default_context(sename, NULL, default_sc);
#endif
	}

	if (r == 0) {
		/* If launched from xinetd, we must use current level */
		if (inetd) {
			security_context_t sshdsc=NULL;

			if (getcon_raw(&sshdsc) < 0)
				fatal("failed to allocate security context");

			if ((con=context_new(sshdsc)) == NULL)
				fatal("failed to allocate selinux context");
			reqlvl = context_range_get(con);
			freecon(sshdsc);
			if (reqlvl !=NULL && lvl != NULL && strcmp(reqlvl, lvl) == 0)
			    /* we actually don't change level */
			    reqlvl = "";

			debug_f("current connection level '%s'", reqlvl);

		}

		if ((reqlvl != NULL && reqlvl[0]) || (role != NULL && role[0])) {
			r = get_user_context(sename, role, reqlvl, user_sc);

			if (r == 0 && reqlvl != NULL && reqlvl[0]) {
				security_context_t default_level_sc = *default_sc;
				if (role != NULL && role[0]) {
					if (get_user_context(sename, role, lvl, &default_level_sc) < 0)
						default_level_sc = *default_sc;
				}
				/* verify that the requested range is contained in the user range */
				if (mls_range_allowed(default_level_sc, *user_sc)) {
					logit("permit MLS level %s (user range %s)", reqlvl, lvl);
				} else {
					r = -1;
					error("deny MLS level %s (user range %s)", reqlvl, lvl);
				}
				if (default_level_sc != *default_sc)
					freecon(default_level_sc);
			}
		} else {
			*user_sc = *default_sc;
		}
	}
	if (r != 0) {
		error_f("Failed to get default SELinux security "
		    "context for %s", pwname);
	}

#ifdef HAVE_GETSEUSERBYNAME
	free(sename);
	free(lvl);
#endif

	if (role != NULL)
		free(role);
	if (con)
		context_free(con);

	return (r);
}

/* Setup environment variables for pam_selinux */
static int
sshd_selinux_setup_variables(int(*set_it)(char *, const char *), int inetd,
    Authctxt *the_authctxt)
{
	const char *reqlvl;
	char *role;
	char *use_current;
	int rv;

	debug3_f("setting execution context");

	ssh_selinux_get_role_level(&role, &reqlvl, the_authctxt);

	rv = set_it("SELINUX_ROLE_REQUESTED", role ? role : "");

	if (inetd) {
		use_current = "1";
	} else {
		use_current = "";
		rv = rv || set_it("SELINUX_LEVEL_REQUESTED", reqlvl ? reqlvl: "");
	}

	rv = rv || set_it("SELINUX_USE_CURRENT_RANGE", use_current);

	if (role != NULL)
		free(role);

	return rv;
}

static int
sshd_selinux_setup_pam_variables(int inetd,
    int(pam_setenv)(char *, const char *), Authctxt *the_authctxt)
{
	return sshd_selinux_setup_variables(pam_setenv, inetd, the_authctxt);
}

static int
do_setenv(char *name, const char *value)
{
	return setenv(name, value, 1);
}

int
sshd_selinux_setup_env_variables(int inetd, void *the_authctxt)
{
	Authctxt *authctxt = (Authctxt *) the_authctxt;
	return sshd_selinux_setup_variables(do_setenv, inetd, authctxt);
}

/* Set the execution context to the default for the specified user */
void
sshd_selinux_setup_exec_context(char *pwname, int inetd,
    int(pam_setenv)(char *, const char *), void *the_authctxt, int use_pam)
{
	security_context_t user_ctx = NULL;
	int r = 0;
	security_context_t default_ctx = NULL;
	Authctxt *authctxt = (Authctxt *) the_authctxt;

	if (!sshd_selinux_enabled())
		return;

	if (use_pam) {
		/* do not compute context, just setup environment for pam_selinux */
		if (sshd_selinux_setup_pam_variables(inetd, pam_setenv, authctxt)) {
			switch (security_getenforce()) {
			case -1:
				fatal_f("security_getenforce() failed");
			case 0:
				error_f("SELinux PAM variable setup failure. Continuing in permissive mode.");
			break;
			default:
				fatal_f("SELinux PAM variable setup failure. Aborting connection.");
			}
		}
		return;
	}

	debug3_f("setting execution context");

	r = sshd_selinux_getctxbyname(pwname, &default_ctx, &user_ctx, inetd, authctxt);
	if (r >= 0) {
		r = setexeccon(user_ctx);
		if (r < 0) {
			error_f("Failed to set SELinux execution context %s for %s",
			    user_ctx, pwname);
		}
#ifdef HAVE_SETKEYCREATECON
		else if (setkeycreatecon(user_ctx) < 0) {
			error_f("Failed to set SELinux keyring creation context %s for %s",
			    user_ctx, pwname);
		}
#endif
	}
	if (user_ctx == NULL) {
		user_ctx = default_ctx;
	}
	if (r < 0 || user_ctx != default_ctx) {
		/* audit just the case when user changed a role or there was
		   a failure */
		sshd_selinux_send_audit_message(r >= 0, default_ctx, user_ctx);
	}
	if (r < 0) {
		switch (security_getenforce()) {
		case -1:
			fatal_f("security_getenforce() failed");
		case 0:
			error_f("ELinux failure. Continuing in permissive mode.");
			break;
		default:
			fatal_f("SELinux failure. Aborting connection.");
		}
	}
	if (user_ctx != NULL && user_ctx != default_ctx)
		freecon(user_ctx);
	if (default_ctx != NULL)
		freecon(default_ctx);

	debug3_f("done");
}

void
sshd_selinux_copy_context(void)
{
	char *ctx;

	if (!sshd_selinux_enabled())
		return;

	if (getexeccon((security_context_t *)&ctx) != 0) {
		logit_f("getexeccon failed with %s", strerror(errno));
		return;
	}
	if (ctx != NULL) {
		/* unset exec context before we will lose this capabililty */
		if (setexeccon(NULL) != 0)
			fatal_f("setexeccon failed with %s", strerror(errno));
		if (setcon(ctx) != 0)
			fatal_f("setcon failed with %s", strerror(errno));
		freecon(ctx);
	}
}

void
sshd_selinux_change_privsep_preauth_context(void)
{
	int len;
	char line[1024], *preauth_context = NULL, *cp, *arg;
	const char *contexts_path;
	FILE *contexts_file;
	struct stat sb;

	contexts_path = selinux_openssh_contexts_path();
	if (contexts_path == NULL) {
		debug3_f("Failed to get the path to SELinux context");
		return;
	}

	if ((contexts_file = fopen(contexts_path, "r")) == NULL) {
		debug_f("Failed to open SELinux context file");
		return;
	}

	if (fstat(fileno(contexts_file), &sb) != 0 ||
	    sb.st_uid != 0 || (sb.st_mode & 022) != 0) {
		logit_f("SELinux context file needs to be owned by root"
		    " and not writable by anyone else");
		fclose(contexts_file);
		return;
	}

	while (fgets(line, sizeof(line), contexts_file)) {
		/* Strip trailing whitespace */
		for (len = strlen(line) - 1; len > 0; len--) {
			if (strchr(" \t\r\n", line[len]) == NULL)
				break;
			line[len] = '\0';
		}

		if (line[0] == '\0')
			continue;

		cp = line;
		arg = strdelim(&cp);
		if (arg && *arg == '\0')
			arg = strdelim(&cp);

		if (arg && strcmp(arg, "privsep_preauth") == 0) {
			arg = strdelim(&cp);
			if (!arg || *arg == '\0') {
				debug_f("privsep_preauth is empty");
				fclose(contexts_file);
				return;
			}
			preauth_context = xstrdup(arg);
		}
	}
	fclose(contexts_file);

	if (preauth_context == NULL) {
		debug_f("Unable to find 'privsep_preauth' option in"
		    " SELinux context file");
		return;
	}

	ssh_selinux_change_context(preauth_context);
	free(preauth_context);
}

#endif
#endif

