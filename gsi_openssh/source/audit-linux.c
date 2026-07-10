/*
 * Copyright 2010 Red Hat, Inc.  All rights reserved.
 * Use is subject to license terms.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Red Hat author: Jan F. Chadima <jchadima@redhat.com>
 */

#include "includes.h"
#if defined(USE_LINUX_AUDIT)
#include <libaudit.h>
#include <unistd.h>
#include <string.h>

#include "log.h"
#include "audit.h"
#include "sshkey.h"
#include "hostfile.h"
#include "auth.h"
#include "misc.h"      /* servconf.h needs misc.h for struct ForwardOptions */
#include "servconf.h"
#include "canohost.h"
#include "packet.h"
#include "cipher.h"
#include "channels.h"
#include "session.h"

#define AUDIT_LOG_SIZE 256

extern ServerOptions options;
extern Authctxt *the_authctxt;
extern u_int utmp_len;
const char *audit_username(void);

static void
linux_audit_user_logxxx(int uid, const char *username, const char *hostname,
    const char *ip, const char *ttyn, int success, int event)
{
	int audit_fd, rc, saved_errno;

	if ((audit_fd = audit_open()) < 0) {
		if (errno == EINVAL || errno == EPROTONOSUPPORT ||
		    errno == EAFNOSUPPORT)
			return; /* No audit support in kernel */
		else
			goto fatal_report; /* Must prevent login */
	}
        if (hostname != NULL && strcmp(hostname, "UNKNOWN") == 0)
                hostname = NULL;
	rc = audit_log_acct_message(audit_fd, event,
	    NULL, "login", username ? username : "(unknown)",
	    username == NULL ? uid : -1, hostname, ip, ttyn, success);
	saved_errno = errno;
	close(audit_fd);

	/*
	 * Do not report error if the error is EPERM and sshd is run as non
	 * root user.
	 */
	if ((rc == -EPERM) && (geteuid() != 0))
		rc = 0;
	errno = saved_errno;

	if (rc < 0) {
fatal_report:
		fatal("linux_audit_write_entry failed: %s", strerror(errno));
	}
}

static void
linux_audit_user_auth(int uid, const char *username,
    const char *ip, const char *ttyn, int success, int event)
{
	int audit_fd, rc, saved_errno;
	static const char *event_name[] = {
		"maxtries exceeded",
		"root denied",
		"success",
		"none",
		"password",
		"challenge-response",
		"pubkey",
		"hostbased",
		"gssapi",
		"invalid user",
		"nologin",
		"connection closed",
		"connection abandoned",
		"unknown"
	};

	audit_fd = audit_open();
	if (audit_fd < 0) {
		if (errno == EINVAL || errno == EPROTONOSUPPORT ||
		    errno == EAFNOSUPPORT)
			return; /* No audit support in kernel */
		else
			goto fatal_report; /* Must prevent login */
	}

	if ((event < 0) || (event > SSH_AUDIT_UNKNOWN))
		event = SSH_AUDIT_UNKNOWN;

	rc = audit_log_acct_message(audit_fd, AUDIT_USER_AUTH,
	    NULL, event_name[event], username ? username : "(unknown)",
	    username == NULL ? uid : -1, NULL, ip, ttyn, success);
	saved_errno = errno;
	close(audit_fd);
	/*
	 * Do not report error if the error is EPERM and sshd is run as non
	 * root user.
	 */
	if ((rc == -EPERM) && (geteuid() != 0))
		rc = 0;
	errno = saved_errno;
	if (rc < 0) {
fatal_report:
		fatal("linux_audit_write_entry failed: %s", strerror(errno));
	}
}

int
audit_keyusage(struct ssh *ssh, int host_user, const char *key_fp, const struct sshkey_cert *cert, const char *issuer_fp, int rv)
{
	char buf[AUDIT_LOG_SIZE];
	int audit_fd, rc, saved_errno;
	const char *rip;
	u_int i;

	audit_fd = audit_open();
	if (audit_fd < 0) {
		if (errno == EINVAL || errno == EPROTONOSUPPORT ||
					 errno == EAFNOSUPPORT)
			return 1; /* No audit support in kernel */
		else
			return 0; /* Must prevent login */
	}
	rip = ssh_remote_ipaddr(ssh);
	snprintf(buf, sizeof(buf), "%s_auth grantors=auth-key", host_user ? "pubkey" : "hostbased");
	rc = audit_log_acct_message(audit_fd, AUDIT_USER_AUTH, NULL,
		buf, audit_username(), -1, NULL, rip, NULL, rv);
	if ((rc < 0) && ((rc != -1) || (getuid() == 0)))
		goto out;
	snprintf(buf, sizeof(buf), "op=negotiate kind=auth-key fp=%s", key_fp);
	rc = audit_log_user_message(audit_fd, AUDIT_CRYPTO_KEY_USER, buf, NULL,
		rip, NULL, rv);
	if ((rc < 0) && ((rc != -1) || (getuid() == 0)))
		goto out;

	if (cert) {
		char *pbuf;

		pbuf = audit_encode_nv_string("key_id", cert->key_id, 0);
		if (pbuf == NULL)
			goto out;
		snprintf(buf, sizeof(buf), "cert %s cert_serial=%llu cert_issuer_alg=\"%s\" cert_issuer_fp=\"%s\"",
			pbuf, (unsigned long long)cert->serial, sshkey_type(cert->signature_key), issuer_fp);
		free(pbuf);
		rc = audit_log_acct_message(audit_fd, AUDIT_USER_AUTH, NULL,
			buf, audit_username(), -1, NULL, rip, NULL, rv);
		if ((rc < 0) && ((rc != -1) || (getuid() == 0)))
			goto out;

		for (i = 0; cert->principals != NULL && i < cert->nprincipals; i++) {
			pbuf = audit_encode_nv_string("cert_principal", cert->principals[i], 0);
			if (pbuf == NULL)
				goto out;
			snprintf(buf, sizeof(buf), "principal %s", pbuf);
			free(pbuf);
			rc = audit_log_acct_message(audit_fd, AUDIT_USER_AUTH, NULL,
				buf, audit_username(), -1, NULL, rip, NULL, rv);
			if ((rc < 0) && ((rc != -1) || (getuid() == 0)))
				goto out;
		}
	}
out:
	saved_errno = errno;
	audit_close(audit_fd);
	errno = saved_errno;
	/* do not report error if the error is EPERM and sshd is run as non root user */
	return (rc >= 0) || ((rc == -EPERM) && (getuid() != 0));
}

static int user_login_count = 0;

/* Below is the sshd audit API code */

void
audit_connection_from(const char *host, int port)
{
	/* not implemented */
}

int
audit_run_command(struct ssh *ssh, const char *command)
{
	char * audit_hostname = options.use_dns ? remote_hostname(ssh) : NULL;
	if (!user_login_count++)
		linux_audit_user_logxxx(the_authctxt->pw->pw_uid, NULL,
		    audit_hostname,
		    ssh_remote_ipaddr(ssh),
		    "ssh", 1, AUDIT_USER_LOGIN);
	linux_audit_user_logxxx(the_authctxt->pw->pw_uid, NULL,
	    audit_hostname,
	    ssh_remote_ipaddr(ssh),
	    "ssh", 1, AUDIT_USER_START);
	free(audit_hostname);
	return 0;
}

void
audit_end_command(struct ssh *ssh, int handle, const char *command)
{
	char * audit_hostname = options.use_dns ? remote_hostname(ssh) : NULL;
	linux_audit_user_logxxx(the_authctxt->pw->pw_uid, NULL,
	    audit_hostname,
	    ssh_remote_ipaddr(ssh),
	    "ssh", 1, AUDIT_USER_END);
	if (user_login_count && !--user_login_count)
		linux_audit_user_logxxx(the_authctxt->pw->pw_uid, NULL,
		    audit_hostname,
		    ssh_remote_ipaddr(ssh),
		    "ssh", 1, AUDIT_USER_LOGOUT);
	free(audit_hostname);
}

void
audit_count_session_open(void)
{
	user_login_count++;
}

void
audit_session_open(struct logininfo *li)
{
	if (!user_login_count++)
		linux_audit_user_logxxx(li->uid, NULL,
		    options.use_dns ? li->hostname : NULL,
		    options.use_dns ? NULL : li->hostname,
		    li->line, 1, AUDIT_USER_LOGIN);
	linux_audit_user_logxxx(li->uid, NULL,
	    options.use_dns ? li->hostname : NULL,
	    options.use_dns ? NULL : li->hostname,
	    li->line, 1, AUDIT_USER_START);
}

void
audit_session_close(struct logininfo *li)
{
	linux_audit_user_logxxx(li->uid, NULL,
	    options.use_dns ? li->hostname : NULL,
	    options.use_dns ? NULL : li->hostname,
	    li->line, 1, AUDIT_USER_END);
	if (user_login_count && !--user_login_count)
		linux_audit_user_logxxx(li->uid, NULL,
		    options.use_dns ? li->hostname : NULL,
		    options.use_dns ? NULL : li->hostname,
		    li->line, 1, AUDIT_USER_LOGOUT);
}

void
audit_event(struct ssh *ssh, ssh_audit_event_t event)
{
	char * audit_hostname = options.use_dns ? remote_hostname(ssh) : NULL;

	switch(event) {
	case SSH_NOLOGIN:
	case SSH_LOGIN_ROOT_DENIED:
		linux_audit_user_auth(-1, audit_username(),
			ssh_remote_ipaddr(ssh), "ssh", 0, event);
		linux_audit_user_logxxx(-1, audit_username(), audit_hostname,
			ssh_remote_ipaddr(ssh), "ssh", 0, AUDIT_USER_LOGIN);
		break;
	case SSH_AUTH_FAIL_PASSWD:
		if (options.use_pam)
			break;
	/* Fallthrough */
	case SSH_LOGIN_EXCEED_MAXTRIES:
	case SSH_AUTH_FAIL_KBDINT:
	case SSH_AUTH_FAIL_PUBKEY:
	case SSH_AUTH_FAIL_HOSTBASED:
	case SSH_AUTH_FAIL_GSSAPI:
		linux_audit_user_auth(-1, audit_username(),
			ssh_remote_ipaddr(ssh), "ssh", 0, event);
		break;

	case SSH_CONNECTION_CLOSE:
		if (user_login_count) {
			while (user_login_count--)
				linux_audit_user_logxxx(the_authctxt->pw->pw_uid, NULL,
				    audit_hostname,
				    ssh_remote_ipaddr(ssh),
				    "ssh", 1, AUDIT_USER_END);
			linux_audit_user_logxxx(the_authctxt->pw->pw_uid, NULL,
			    audit_hostname,
			    ssh_remote_ipaddr(ssh),
			    "ssh", 1, AUDIT_USER_LOGOUT);
		}
		break;

	case SSH_CONNECTION_ABANDON:
	case SSH_INVALID_USER:
		linux_audit_user_logxxx(-1, audit_username(),
			audit_hostname,
			ssh_remote_ipaddr(ssh), "ssh", 0, AUDIT_USER_LOGIN);
		break;
	default:
		debug_f("unhandled event %d", event);
		break;
	}
	free(audit_hostname);
}

void
audit_unsupported_body(struct ssh *ssh, int what)
{
#ifdef AUDIT_CRYPTO_SESSION
	char buf[AUDIT_LOG_SIZE];
	static const char *name[] = { "cipher", "mac", "comp" };
	char *s;
	int audit_fd;

	snprintf(buf, sizeof(buf), "op=unsupported-%s direction=? cipher=? ksize=? rport=%d laddr=%s lport=%d ",
		name[what], ssh_remote_port(ssh), (s = get_local_ipaddr(ssh_packet_get_connection_in(ssh))),
		ssh_local_port(ssh));
	free(s);
	audit_fd = audit_open();
	if (audit_fd < 0)
		/* no problem, the next instruction will be fatal() */
		return;
	audit_log_user_message(audit_fd, AUDIT_CRYPTO_SESSION,
			buf, NULL, ssh_remote_ipaddr(ssh), NULL, 0);
	audit_close(audit_fd);
#endif
}

static const char *direction[] = { "from-server", "from-client", "both" };

void
audit_kex_body(struct ssh *ssh, int ctos, char *enc, char *mac, char *compress,
    char *pfs, pid_t pid, uid_t uid)
{
#ifdef AUDIT_CRYPTO_SESSION
	char buf[AUDIT_LOG_SIZE];
	int audit_fd, audit_ok;
	const struct sshcipher *cipher = cipher_by_name(enc);
	char *s;

	snprintf(buf, sizeof(buf), "op=start direction=%s cipher=%s ksize=%d mac=%s pfs=%s spid=%jd suid=%jd rport=%d laddr=%s lport=%d ",
		direction[ctos], enc, cipher ? 8 * cipher->key_len : 0, mac, pfs,
		(intmax_t)pid, (intmax_t)uid,
		ssh_remote_port(ssh), (s = get_local_ipaddr(ssh_packet_get_connection_in(ssh))), ssh_local_port(ssh));
	free(s);
	audit_fd = audit_open();
	if (audit_fd < 0) {
		if (errno == EINVAL || errno == EPROTONOSUPPORT ||
					 errno == EAFNOSUPPORT)
			return; /* No audit support in kernel */
		else
			fatal("cannot open audit"); /* Must prevent login */
	}
	audit_ok = audit_log_user_message(audit_fd, AUDIT_CRYPTO_SESSION,
			buf, NULL, ssh_remote_ipaddr(ssh), NULL, 1);
	audit_close(audit_fd);
	/* do not abort if the error is EPERM and sshd is run as non root user */
	if ((audit_ok < 0) && ((audit_ok != -1) || (getuid() == 0)))
		fatal("cannot write into audit"); /* Must prevent login */
#endif
}

void
audit_session_key_free_body(struct ssh *ssh, int ctos, pid_t pid, uid_t uid)
{
	char buf[AUDIT_LOG_SIZE];
	int audit_fd, audit_ok;
	char *s;

	snprintf(buf, sizeof(buf), "op=destroy kind=session fp=? direction=%s spid=%jd suid=%jd rport=%d laddr=%s lport=%d ",
		 direction[ctos], (intmax_t)pid, (intmax_t)uid,
		 ssh_remote_port(ssh),
		 (s = get_local_ipaddr(ssh_packet_get_connection_in(ssh))),
		 ssh_local_port(ssh));
	free(s);
	audit_fd = audit_open();
	if (audit_fd < 0) {
		if (errno != EINVAL && errno != EPROTONOSUPPORT &&
					 errno != EAFNOSUPPORT)
			error("cannot open audit");
		return;
	}
	audit_ok = audit_log_user_message(audit_fd, AUDIT_CRYPTO_KEY_USER,
			buf, NULL, ssh_remote_ipaddr(ssh), NULL, 1);
	audit_close(audit_fd);
	/* do not abort if the error is EPERM and sshd is run as non root user */
	if ((audit_ok < 0) && ((audit_ok != -1) || (getuid() == 0)))
		error("cannot write into audit");
}

void
audit_destroy_sensitive_data(struct ssh *ssh, const char *fp, pid_t pid, uid_t uid)
{
	char buf[AUDIT_LOG_SIZE];
	int audit_fd, audit_ok;

	snprintf(buf, sizeof(buf), "op=destroy kind=server fp=%s direction=? spid=%jd suid=%jd ",
		fp, (intmax_t)pid, (intmax_t)uid);
	audit_fd = audit_open();
	if (audit_fd < 0) {
		if (errno != EINVAL && errno != EPROTONOSUPPORT &&
					 errno != EAFNOSUPPORT)
			error("cannot open audit");
		return;
	}
	audit_ok = audit_log_user_message(audit_fd, AUDIT_CRYPTO_KEY_USER,
			buf, NULL,
			ssh_remote_ipaddr(ssh), /*FIXME listening_for_clients() ? NULL : ssh_remote_ipaddr(ssh) */
			NULL, 1);
	audit_close(audit_fd);
	/* do not abort if the error is EPERM and sshd is run as non root user */
	if ((audit_ok < 0) && ((audit_ok != -1) || (getuid() == 0)))
		error("cannot write into audit");
}
#endif /* USE_LINUX_AUDIT */
