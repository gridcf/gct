/* $OpenBSD: auth2-gss.c,v 1.33 2021/12/19 22:12:07 djm Exp $ */

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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AS IS'' AND ANY EXPRESS OR
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

#include <sys/types.h>

#include <stdarg.h>

#include "xmalloc.h"
#include "sshkey.h"
#include "hostfile.h"
#include "auth.h"
#include "ssh2.h"
#include "log.h"
#include "dispatch.h"
#include "sshbuf.h"
#include "ssherr.h"
#include "misc.h"
#include "servconf.h"
#include "packet.h"
#include "kex.h"
#include "ssh-gss.h"
#include "monitor_wrap.h"

extern ServerOptions options;

static void ssh_gssapi_userauth_error(Gssctxt *ctxt, struct ssh *ssh);
static int input_gssapi_token(int type, u_int32_t plen, struct ssh *ssh);
static int input_gssapi_mic(int type, u_int32_t plen, struct ssh *ssh);
static int input_gssapi_exchange_complete(int type, u_int32_t plen, struct ssh *ssh);
static int input_gssapi_errtok(int, u_int32_t, struct ssh *);

/*
 * The 'gssapi_keyex' userauth mechanism.
 */
static int
userauth_gsskeyex(struct ssh *ssh, const char *method)
{
	Authctxt *authctxt = ssh->authctxt;
	int r, authenticated = 0;
	struct sshbuf *b = NULL, *b2 = NULL;
	gss_buffer_desc mic, gssbuf, gssbuf2;
	u_char *p;
	size_t len;

	if ((r = sshpkt_get_string(ssh, &p, &len)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		fatal_fr(r, "parsing");

	if ((b = sshbuf_new()) == NULL)
		fatal_f("sshbuf_new failed");

	if ((b2 = sshbuf_new()) == NULL)
		fatal_f("sshbuf_new failed");

	mic.value = p;
	mic.length = len;

	ssh_gssapi_buildmic(b, authctxt->user, authctxt->service,
	    "gssapi-keyex", ssh->kex->session_id);

	if ((gssbuf.value = sshbuf_mutable_ptr(b)) == NULL)
		fatal_f("sshbuf_mutable_ptr failed");
	gssbuf.length = sshbuf_len(b);

	/* client may have used empty username to determine target
	   name from GSSAPI context */
	ssh_gssapi_buildmic(b2, "", authctxt->service, "gssapi-keyex",
	    ssh->kex->session_id);

	if ((gssbuf2.value = sshbuf_mutable_ptr(b2)) == NULL)
		fatal_f("sshbuf_mutable_ptr failed");
	gssbuf2.length = sshbuf_len(b2);

	/* gss_kex_context is NULL with privsep, so we can't check it here */
	if (!GSS_ERROR(PRIVSEP(ssh_gssapi_checkmic(gss_kex_context,
	    &gssbuf, &mic))) ||
	    !GSS_ERROR(PRIVSEP(ssh_gssapi_checkmic(gss_kex_context,
	    &gssbuf2, &mic)))) {
		if (authctxt->valid && authctxt->user && authctxt->user[0]) {
			authenticated =
			    PRIVSEP(ssh_gssapi_userok(authctxt->user,
			    authctxt->pw, 1));
		}
	}

	sshbuf_free(b);
	sshbuf_free(b2);
	free(mic.value);

	return (authenticated);
}

/*
 * We only support those mechanisms that we know about (ie ones that we know
 * how to check local user kuserok and the like)
 */
static int
userauth_gssapi(struct ssh *ssh, const char *method)
{
	Authctxt *authctxt = ssh->authctxt;
	gss_OID_desc goid = {0, NULL};
	Gssctxt *ctxt = NULL;
	int r, present;
	u_int mechs;
	OM_uint32 ms;
	size_t len;
	u_char *doid = NULL;

	if ((r = sshpkt_get_u32(ssh, &mechs)) != 0)
		fatal_fr(r, "parse packet");

	if (mechs == 0) {
		debug("Mechanism negotiation is not supported");
		return (0);
	}

	do {
		mechs--;

		free(doid);

		present = 0;
		if ((r = sshpkt_get_string(ssh, &doid, &len)) != 0)
			fatal_fr(r, "parse oid");

		if (len > 2 && doid[0] == SSH_GSS_OIDTYPE &&
		    doid[1] == len - 2) {
			goid.elements = doid + 2;
			goid.length   = len - 2;
			ssh_gssapi_test_oid_supported(&ms, &goid, &present);
		} else {
			logit("Badly formed OID received");
		}
	} while (mechs > 0 && !present);

	if (!present) {
		free(doid);
		authctxt->server_caused_failure = 1;
		return (0);
	}

	/* authctxt->valid may be 0 if we haven't yet determined
	   username from gssapi context. */
	if (authctxt->user == NULL) {
		debug2_f("disabled because of invalid user");
		free(doid);
		return (0);
	}

	if (GSS_ERROR(PRIVSEP(ssh_gssapi_server_ctx(&ctxt, &goid)))) {
		if (ctxt != NULL)
			ssh_gssapi_delete_ctx(&ctxt);
		free(doid);
		authctxt->server_caused_failure = 1;
		return (0);
	}

	authctxt->methoddata = (void *)ctxt;

	/* Return the OID that we received */
	if ((r = sshpkt_start(ssh, SSH2_MSG_USERAUTH_GSSAPI_RESPONSE)) != 0 ||
	    (r = sshpkt_put_string(ssh, doid, len)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		fatal_fr(r, "send packet");

	free(doid);

	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_TOKEN, &input_gssapi_token);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_ERRTOK, &input_gssapi_errtok);
	authctxt->postponed = 1;

	return (0);
}

static int
input_gssapi_token(int type, u_int32_t plen, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	Gssctxt *gssctxt;
	gss_buffer_desc send_tok = GSS_C_EMPTY_BUFFER;
	gss_buffer_desc recv_tok;
	OM_uint32 maj_status, min_status, flags = 0;
	u_char *p;
	size_t len;
	int r;

	if (authctxt == NULL || (authctxt->methoddata == NULL && !use_privsep))
		fatal("No authentication or GSSAPI context");

	gssctxt = authctxt->methoddata;
	if ((r = sshpkt_get_string(ssh, &p, &len)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		fatal_fr(r, "parse packet");

	recv_tok.value = p;
	recv_tok.length = len;
	maj_status = PRIVSEP(ssh_gssapi_accept_ctx(gssctxt, &recv_tok,
	    &send_tok, &flags));

	free(p);

	if (GSS_ERROR(maj_status)) {
		ssh_gssapi_userauth_error(gssctxt, ssh);
		if (send_tok.length != 0) {
			if ((r = sshpkt_start(ssh,
			    SSH2_MSG_USERAUTH_GSSAPI_ERRTOK)) != 0 ||
			    (r = sshpkt_put_string(ssh, send_tok.value,
			    send_tok.length)) != 0 ||
			    (r = sshpkt_send(ssh)) != 0)
				fatal_fr(r, "send ERRTOK packet");
		}
		authctxt->postponed = 0;
		ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_TOKEN, NULL);
		userauth_finish(ssh, 0, "gssapi-with-mic", NULL);
	} else {
		if (send_tok.length != 0) {
			if ((r = sshpkt_start(ssh,
			    SSH2_MSG_USERAUTH_GSSAPI_TOKEN)) != 0 ||
			    (r = sshpkt_put_string(ssh, send_tok.value,
			    send_tok.length)) != 0 ||
			    (r = sshpkt_send(ssh)) != 0)
				fatal_fr(r, "send TOKEN packet");
		}
		if (maj_status == GSS_S_COMPLETE) {
			ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_TOKEN, NULL);
			if (flags & GSS_C_INTEG_FLAG)
				ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_MIC,
				    &input_gssapi_mic);
			else
				ssh_dispatch_set(ssh,
				    SSH2_MSG_USERAUTH_GSSAPI_EXCHANGE_COMPLETE,
				    &input_gssapi_exchange_complete);
		}
	}

	gss_release_buffer(&min_status, &send_tok);
	return 0;
}

static int
input_gssapi_errtok(int type, u_int32_t plen, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	Gssctxt *gssctxt;
	gss_buffer_desc send_tok = GSS_C_EMPTY_BUFFER;
	gss_buffer_desc recv_tok;
	OM_uint32 maj_status;
	int r;
	u_char *p;
	size_t len;

	if (authctxt == NULL || (authctxt->methoddata == NULL && !use_privsep))
		fatal("No authentication or GSSAPI context");

	gssctxt = authctxt->methoddata;
	if ((r = sshpkt_get_string(ssh, &p, &len)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		fatal_fr(r, "parse packet");
	recv_tok.value = p;
	recv_tok.length = len;

	/* Push the error token into GSSAPI to see what it says */
	maj_status = PRIVSEP(ssh_gssapi_accept_ctx(gssctxt, &recv_tok,
	    &send_tok, NULL));

	free(recv_tok.value);

	/* We can't return anything to the client, even if we wanted to */
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_TOKEN, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_ERRTOK, NULL);

	/* The client will have already moved on to the next auth */

	gss_release_buffer(&maj_status, &send_tok);
	return 0;
}

static void
gssapi_set_username(struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	char *lname = NULL;

	if ((authctxt->user == NULL) || (authctxt->user[0] == '\0')) {
		PRIVSEP(ssh_gssapi_localname(&lname));
		if (lname && lname[0] != '\0') {
			if (authctxt->user) free(authctxt->user);
			authctxt->user = lname;
			debug("set username to %s from gssapi context", lname);
			authctxt->pw = PRIVSEP(getpwnamallow(ssh, authctxt->user));
			if (authctxt->pw) {
				authctxt->valid = 1;
#ifdef USE_PAM
				if (options.use_pam)
					PRIVSEP(start_pam(ssh));
#endif
			}
		} else {
			debug("failed to set username from gssapi context");
			ssh_packet_send_debug(ssh,
			    "failed to set username from gssapi context");
		}
	}
}

/*
 * This is called when the client thinks we've completed authentication.
 * It should only be enabled in the dispatch handler by the function above,
 * which only enables it once the GSSAPI exchange is complete.
 */

static int
input_gssapi_exchange_complete(int type, u_int32_t plen, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	int r, authenticated = 0;
	const char *displayname;

	if (authctxt == NULL || (authctxt->methoddata == NULL && !use_privsep))
		fatal("No authentication or GSSAPI context");

	gssapi_set_username(ssh);

	/*
	 * We don't need to check the status, because we're only enabled in
	 * the dispatcher once the exchange is complete
	 */

	if ((r = sshpkt_get_end(ssh)) != 0)
		fatal_fr(r, "parse packet");

	/* user should be set if valid but we double-check here */
	if (authctxt->valid && authctxt->user && authctxt->user[0]) {
		authenticated = PRIVSEP(ssh_gssapi_userok(authctxt->user,
		    authctxt->pw, 1));
	}

	if ((!use_privsep || mm_is_monitor()) &&
	    (displayname = ssh_gssapi_displayname()) != NULL)
		auth2_record_info(authctxt, "%s", displayname);

	authctxt->postponed = 0;
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_TOKEN, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_ERRTOK, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_MIC, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_EXCHANGE_COMPLETE, NULL);
	userauth_finish(ssh, authenticated, "gssapi-with-mic", NULL);
	return 0;
}

static int
input_gssapi_mic(int type, u_int32_t plen, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	Gssctxt *gssctxt;
	int r, authenticated = 0;
	char *micuser;
	struct sshbuf *b;
	gss_buffer_desc mic, gssbuf;
	const char *displayname;
	u_char *p;
	size_t len;

	if (authctxt == NULL || (authctxt->methoddata == NULL && !use_privsep))
		fatal("No authentication or GSSAPI context");

	gssctxt = authctxt->methoddata;

	if ((r = sshpkt_get_string(ssh, &p, &len)) != 0)
		fatal_fr(r, "parse packet");
	if ((b = sshbuf_new()) == NULL)
		fatal_f("sshbuf_new failed");
	mic.value = p;
	mic.length = len;
#ifdef WITH_SELINUX
	if (authctxt->role && authctxt->role[0] != 0)
		xasprintf(&micuser, "%s/%s", authctxt->user, authctxt->role);
	else
#endif
		micuser = authctxt->user;
	ssh_gssapi_buildmic(b, micuser, authctxt->service,
	    "gssapi-with-mic", ssh->kex->session_id);

	if ((gssbuf.value = sshbuf_mutable_ptr(b)) == NULL)
		fatal_f("sshbuf_mutable_ptr failed");
	gssbuf.length = sshbuf_len(b);

	gssapi_set_username(ssh);

	if (!GSS_ERROR(PRIVSEP(ssh_gssapi_checkmic(gssctxt, &gssbuf, &mic)))) {
		if (authctxt->valid && authctxt->user && authctxt->user[0]) {
			authenticated =
			    PRIVSEP(ssh_gssapi_userok(authctxt->user,
			    authctxt->pw, 0));
		}
	} else {
		logit("GSSAPI MIC check failed");
	}

	sshbuf_free(b);
	if (micuser != authctxt->user)
		free(micuser);
	free(mic.value);

	if ((!use_privsep || mm_is_monitor()) &&
	    (displayname = ssh_gssapi_displayname()) != NULL)
		auth2_record_info(authctxt, "%s", displayname);

	authctxt->postponed = 0;
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_TOKEN, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_ERRTOK, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_MIC, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_EXCHANGE_COMPLETE, NULL);
	userauth_finish(ssh, authenticated, "gssapi-with-mic", NULL);
	return 0;
}

static void ssh_gssapi_userauth_error(Gssctxt *ctxt, struct ssh *ssh) {
	char *errstr;
	OM_uint32 maj, min;
	int r;

	errstr = PRIVSEP(ssh_gssapi_last_error(ctxt, &maj, &min));
	if (errstr) {
		if ((r = sshpkt_start(ssh,
		    SSH2_MSG_USERAUTH_GSSAPI_ERROR)) != 0 ||
		    (r = sshpkt_put_u32(ssh, maj)) != 0 ||
		    (r = sshpkt_put_u32(ssh, min)) != 0 ||
		    (r = sshpkt_put_cstring(ssh, errstr)) != 0 ||
		    (r = sshpkt_put_cstring(ssh, "")) != 0 ||
		    (r = sshpkt_send(ssh)) != 0 ||
		    (r = ssh_packet_write_wait(ssh)) != 0)
			fatal_fr(r, "");
		free(errstr);
	}
}

Authmethod method_gsskeyex = {
	"gssapi-keyex",
	NULL,
	userauth_gsskeyex,
	&options.gss_authentication
};

Authmethod method_gssapi = {
	"gssapi-with-mic",
	NULL,
	userauth_gssapi,
	&options.gss_authentication
};

#endif /* GSSAPI */
