/*
 * Copyright (c) 2001-2009 Simon Wilkinson. All rights reserved.
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

#if defined(GSSAPI) && defined(WITH_OPENSSL)

#include "includes.h"

#include <openssl/crypto.h>
#include <openssl/bn.h>

#include <string.h>

#include "xmalloc.h"
#include "sshbuf.h"
#include "ssh2.h"
#include "sshkey.h"
#include "cipher.h"
#include "kex.h"
#include "log.h"
#include "packet.h"
#include "dh.h"
#include "digest.h"
#include "ssherr.h"

#include "ssh-gss.h"

static int input_kexgss_hostkey(int, u_int32_t, struct ssh *);
static int input_kexgss_continue(int, u_int32_t, struct ssh *);
static int input_kexgss_complete(int, u_int32_t, struct ssh *);
static int input_kexgss_error(int, u_int32_t, struct ssh *);
static int input_kexgssgex_group(int, u_int32_t, struct ssh *);
static int input_kexgssgex_continue(int, u_int32_t, struct ssh *);
static int input_kexgssgex_complete(int, u_int32_t, struct ssh *);

static int
kexgss_final(struct ssh *ssh)
{
	struct kex *kex = ssh->kex;
	Gssctxt *gss = kex->gss;
	struct sshbuf *empty = NULL;
	struct sshbuf *shared_secret = NULL;
	u_char hash[SSH_DIGEST_MAX_LENGTH];
	size_t hashlen;
	int r;

	/*
	 * We _must_ have received a COMPLETE message in reply from the
	 * server, which will have set server_blob and msg_tok
	 */

	/* compute shared secret */
	switch (kex->kex_type) {
	case KEX_GSS_GRP1_SHA1:
	case KEX_GSS_GRP14_SHA1:
	case KEX_GSS_GRP14_SHA256:
	case KEX_GSS_GRP16_SHA512:
		r = kex_dh_dec(kex, gss->server_blob, &shared_secret);
		break;
	case KEX_GSS_C25519_SHA256:
		if (sshbuf_ptr(gss->server_blob)[sshbuf_len(gss->server_blob)] & 0x80)
			fatal("The received key has MSB of last octet set!");
		r = kex_c25519_dec(kex, gss->server_blob, &shared_secret);
		break;
	case KEX_GSS_NISTP256_SHA256:
		if (sshbuf_len(gss->server_blob) != 65)
			fatal("The received NIST-P256 key did not match "
			      "expected length (expected 65, got %zu)",
			      sshbuf_len(gss->server_blob));

		if (sshbuf_ptr(gss->server_blob)[0] != POINT_CONVERSION_UNCOMPRESSED)
			fatal("The received NIST-P256 key does not have first octet 0x04");

		r = kex_ecdh_dec(kex, gss->server_blob, &shared_secret);
		break;
	default:
		r = SSH_ERR_INVALID_ARGUMENT;
		break;
	}
	if (r != 0) {
		ssh_gssapi_delete_ctx(&kex->gss);
		goto out;
	}

	if ((empty = sshbuf_new()) == NULL) {
		ssh_gssapi_delete_ctx(&kex->gss);
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}

	hashlen = sizeof(hash);
	r = kex_gen_hash(kex->hash_alg, kex->client_version,
			 kex->server_version, kex->my, kex->peer,
			 (gss->server_host_key_blob ? gss->server_host_key_blob : empty),
			 kex->client_pub, gss->server_blob, shared_secret,
			 hash, &hashlen);
	sshbuf_free(empty);
	if (r != 0)
		fatal_f("Unexpected KEX type %d", kex->kex_type);

	gss->buf.value = hash;
	gss->buf.length = hashlen;

	/* Verify that the hash matches the MIC we just got. */
	if (GSS_ERROR(ssh_gssapi_checkmic(gss, &gss->buf, &gss->msg_tok)))
		sshpkt_disconnect(ssh, "Hash's MIC didn't verify");

	gss_release_buffer(&gss->minor, &gss->msg_tok);

	if (kex->gss_deleg_creds)
		ssh_gssapi_credentials_updated(gss);

	if (gss_kex_context == NULL)
		gss_kex_context = gss;
	else
		ssh_gssapi_delete_ctx(&kex->gss);

	if ((r = kex_derive_keys(ssh, hash, hashlen, shared_secret)) == 0)
		r = kex_send_newkeys(ssh);

	if (kex->gss != NULL) {
		sshbuf_free(gss->server_host_key_blob);
		gss->server_host_key_blob = NULL;
		sshbuf_free(gss->server_blob);
		gss->server_blob = NULL;
	}
out:
	explicit_bzero(kex->c25519_client_key, sizeof(kex->c25519_client_key));
	explicit_bzero(hash, sizeof(hash));
	sshbuf_free(shared_secret);
	sshbuf_free(kex->client_pub);
	kex->client_pub = NULL;
	return r;
}

static int
kexgss_init_ctx(struct ssh *ssh,
		gss_buffer_desc *token_ptr)
{
	struct kex *kex = ssh->kex;
	Gssctxt *gss = kex->gss;
	gss_buffer_desc send_tok = GSS_C_EMPTY_BUFFER;
	OM_uint32 ret_flags;
	int r;

	debug("Calling gss_init_sec_context");

	gss->major = ssh_gssapi_init_ctx(gss, kex->gss_deleg_creds,
					 token_ptr, &send_tok, &ret_flags);

	if (GSS_ERROR(gss->major)) {
		/* XXX Useless code: Missing send? */
		if (send_tok.length != 0) {
			if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_CONTINUE)) != 0 ||
			    (r = sshpkt_put_string(ssh, send_tok.value, send_tok.length)) != 0)
				fatal("sshpkt failed: %s", ssh_err(r));
		}
		fatal("gss_init_context failed");
	}

	/* If we've got an old receive buffer get rid of it */
	if (token_ptr != GSS_C_NO_BUFFER)
		gss_release_buffer(&gss->minor, token_ptr);

	if (gss->major == GSS_S_COMPLETE) {
		/* If mutual state flag is not true, kex fails */
		if (!(ret_flags & GSS_C_MUTUAL_FLAG))
			fatal("Mutual authentication failed");

		/* If integ avail flag is not true kex fails */
		if (!(ret_flags & GSS_C_INTEG_FLAG))
			fatal("Integrity check failed");
	}

	/*
	 * If we have data to send, then the last message that we
	 * received cannot have been a 'complete'.
	 */
	if (send_tok.length != 0) {
		if (gss->first) {
			if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_INIT)) != 0 ||
			    (r = sshpkt_put_string(ssh, send_tok.value, send_tok.length)) != 0 ||
			    (r = sshpkt_put_stringb(ssh, kex->client_pub)) != 0)
				fatal("failed to construct packet: %s", ssh_err(r));
			gss->first = 0;
		} else {
			if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_CONTINUE)) != 0 ||
			    (r = sshpkt_put_string(ssh, send_tok.value, send_tok.length)) != 0)
				fatal("failed to construct packet: %s", ssh_err(r));
		}
		if ((r = sshpkt_send(ssh)) != 0)
			fatal("failed to send packet: %s", ssh_err(r));
		gss_release_buffer(&gss->minor, &send_tok);

		/* If we've sent them data, they should reply */
		ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_HOSTKEY, &input_kexgss_hostkey);
		ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_CONTINUE, &input_kexgss_continue);
		ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_COMPLETE, &input_kexgss_complete);
		ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_ERROR, &input_kexgss_error);
		return 0;
	}
	/* No data, and not complete */
	if (gss->major != GSS_S_COMPLETE)
		fatal("Not complete, and no token output");

	if  (gss->major & GSS_S_CONTINUE_NEEDED)
		return kexgss_init_ctx(ssh, token_ptr);

	return kexgss_final(ssh);
}

int
kexgss_client(struct ssh *ssh)
{
	struct kex *kex = ssh->kex;
	int r;

	/* Initialise our GSSAPI world */
	ssh_gssapi_build_ctx(&kex->gss);
	if (ssh_gssapi_id_kex(kex->gss, kex->name, kex->kex_type) == GSS_C_NO_OID)
		fatal("Couldn't identify host exchange");

	if (ssh_gssapi_import_name(kex->gss, kex->gss_host))
		fatal("Couldn't import hostname");

	if (kex->gss_client &&
	    ssh_gssapi_client_identity(kex->gss, kex->gss_client))
		fatal("Couldn't acquire client credentials");

	/* Step 1 */
	switch (kex->kex_type) {
	case KEX_GSS_GRP1_SHA1:
	case KEX_GSS_GRP14_SHA1:
	case KEX_GSS_GRP14_SHA256:
	case KEX_GSS_GRP16_SHA512:
		r = kex_dh_keypair(kex);
		break;
	case KEX_GSS_NISTP256_SHA256:
		r = kex_ecdh_keypair(kex);
		break;
	case KEX_GSS_C25519_SHA256:
		r = kex_c25519_keypair(kex);
		break;
	default:
		fatal_f("Unexpected KEX type %d", kex->kex_type);
	}
	if (r != 0) {
		ssh_gssapi_delete_ctx(&kex->gss);
		return r;
	}
	return kexgss_init_ctx(ssh, GSS_C_NO_BUFFER);
}

static int
input_kexgss_hostkey(int type,
		     u_int32_t seq,
		     struct ssh *ssh)
{
	Gssctxt *gss = ssh->kex->gss;
	u_char *tmp = NULL;
	size_t tmp_len = 0;
	int r;

	debug("Received KEXGSS_HOSTKEY");
	if (gss->server_host_key_blob)
		fatal("Server host key received more than once");
	if ((r = sshpkt_get_string(ssh, &tmp, &tmp_len)) != 0)
		fatal("Failed to read server host key: %s", ssh_err(r));
	if ((gss->server_host_key_blob = sshbuf_from(tmp, tmp_len)) == NULL)
		fatal("sshbuf_from failed");
	return 0;
}

static int
input_kexgss_continue(int type,
		      u_int32_t seq,
		      struct ssh *ssh)
{
	Gssctxt *gss = ssh->kex->gss;
	gss_buffer_desc recv_tok = GSS_C_EMPTY_BUFFER;
	int r;

	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_HOSTKEY, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_CONTINUE, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_COMPLETE, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_ERROR, NULL);

	debug("Received GSSAPI_CONTINUE");
	if (gss->major == GSS_S_COMPLETE)
		fatal("GSSAPI Continue received from server when complete");
	if ((r = ssh_gssapi_sshpkt_get_buffer_desc(ssh, &recv_tok)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		fatal("Failed to read token: %s", ssh_err(r));
	if  (!(gss->major & GSS_S_CONTINUE_NEEDED))
		fatal("Didn't receive a SSH2_MSG_KEXGSS_COMPLETE when I expected it");
	return kexgss_init_ctx(ssh, &recv_tok);
}

static int
input_kexgss_complete(int type,
		      u_int32_t seq,
		      struct ssh *ssh)
{
	Gssctxt *gss = ssh->kex->gss;
	gss_buffer_desc recv_tok = GSS_C_EMPTY_BUFFER;
	u_char c;
	int r;

	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_HOSTKEY, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_CONTINUE, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_COMPLETE, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_ERROR, NULL);

	debug("Received GSSAPI_COMPLETE");
	if (gss->msg_tok.value != NULL)
	        fatal("Received GSSAPI_COMPLETE twice?");
	if ((r = sshpkt_getb_froms(ssh, &gss->server_blob)) != 0 ||
	    (r = ssh_gssapi_sshpkt_get_buffer_desc(ssh, &gss->msg_tok)) != 0)
		fatal("Failed to read message: %s", ssh_err(r));

	/* Is there a token included? */
	if ((r = sshpkt_get_u8(ssh, &c)) != 0)
		fatal("sshpkt failed: %s", ssh_err(r));
	if (c) {
		if ((r = ssh_gssapi_sshpkt_get_buffer_desc(ssh, &recv_tok)) != 0)
			fatal("Failed to read token: %s", ssh_err(r));
		/* If we're already complete - protocol error */
		if (gss->major == GSS_S_COMPLETE)
			sshpkt_disconnect(ssh, "Protocol error: received token when complete");
	} else {
		if (gss->major != GSS_S_COMPLETE)
			sshpkt_disconnect(ssh, "Protocol error: did not receive final token");
	}
	if ((r = sshpkt_get_end(ssh)) != 0)
		fatal("Expecting end of packet.");

	if  (gss->major & GSS_S_CONTINUE_NEEDED)
		return kexgss_init_ctx(ssh, &recv_tok);

	gss_release_buffer(&gss->minor, &recv_tok);
	return kexgss_final(ssh);
}

static int
input_kexgss_error(int type,
		   u_int32_t seq,
		   struct ssh *ssh)
{
	Gssctxt *gss = ssh->kex->gss;
	u_char *msg;
	int r;

	debug("Received Error");
	if ((r = sshpkt_get_u32(ssh, &gss->major)) != 0 ||
	    (r = sshpkt_get_u32(ssh, &gss->minor)) != 0 ||
	    (r = sshpkt_get_string(ssh, &msg, NULL)) != 0 ||
	    (r = sshpkt_get_string(ssh, NULL, NULL)) != 0 || /* lang tag */
	    (r = sshpkt_get_end(ssh)) != 0)
		fatal("sshpkt_get failed: %s", ssh_err(r));
	fatal("GSSAPI Error: \n%.400s", msg);
	return 0;
}

/*******************************************************/
/******************** KEXGSSGEX ************************/
/*******************************************************/

int
kexgssgex_client(struct ssh *ssh)
{
	struct kex *kex = ssh->kex;
	int r;

	/* Initialise our GSSAPI world */
	ssh_gssapi_build_ctx(&kex->gss);
	if (ssh_gssapi_id_kex(kex->gss, kex->name, kex->kex_type) == GSS_C_NO_OID)
		fatal("Couldn't identify host exchange");

	if (ssh_gssapi_import_name(kex->gss, kex->gss_host))
		fatal("Couldn't import hostname");

	if (kex->gss_client &&
	    ssh_gssapi_client_identity(kex->gss, kex->gss_client))
		fatal("Couldn't acquire client credentials");

	debug("Doing group exchange");
	kex->min = DH_GRP_MIN;
	kex->max = DH_GRP_MAX;
	kex->nbits = dh_estimate(kex->dh_need * 8);

	if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_GROUPREQ)) != 0 ||
	    (r = sshpkt_put_u32(ssh, kex->min)) != 0 ||
	    (r = sshpkt_put_u32(ssh, kex->nbits)) != 0 ||
	    (r = sshpkt_put_u32(ssh, kex->max)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		fatal("Failed to construct a packet: %s", ssh_err(r));

	debug("Wait SSH2_MSG_KEXGSS_GROUP");
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_GROUP, &input_kexgssgex_group);
	return 0;
}

static int
kexgssgex_final(struct ssh *ssh)
{
	struct kex *kex = ssh->kex;
	Gssctxt *gss = kex->gss;
	struct sshbuf *buf = NULL;
	struct sshbuf *empty = NULL;
	struct sshbuf *shared_secret = NULL;
	BIGNUM *dh_server_pub = NULL;
	const BIGNUM *pub_key, *dh_p, *dh_g;
	u_char hash[SSH_DIGEST_MAX_LENGTH];
	size_t hashlen;
	int r = SSH_ERR_INTERNAL_ERROR;

	/*
	 * We _must_ have received a COMPLETE message in reply from the
	 * server, which will have set server_blob and msg_tok
	 */

	/* 7. C verifies that the key Q_S is valid */
	/* 8. C computes shared secret */
	if ((buf = sshbuf_new()) == NULL ||
	    (r = sshbuf_put_stringb(buf, gss->server_blob)) != 0 ||
	    (r = sshbuf_get_bignum2(buf, &dh_server_pub)) != 0) {
		ssh_gssapi_delete_ctx(&kex->gss);
		goto out;
	}
	sshbuf_free(buf);
	buf = NULL;

	if ((shared_secret = sshbuf_new()) == NULL) {
		ssh_gssapi_delete_ctx(&kex->gss);
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}

	if ((r = kex_dh_compute_key(kex, dh_server_pub, shared_secret)) != 0) {
		ssh_gssapi_delete_ctx(&kex->gss);
		goto out;
	}

	if ((empty = sshbuf_new()) == NULL) {
		ssh_gssapi_delete_ctx(&kex->gss);
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}

	DH_get0_key(kex->dh, &pub_key, NULL);
	DH_get0_pqg(kex->dh, &dh_p, NULL, &dh_g);
	hashlen = sizeof(hash);
	r = kexgex_hash(kex->hash_alg, kex->client_version,
			kex->server_version, kex->my, kex->peer,
			(gss->server_host_key_blob ? gss->server_host_key_blob : empty),
			kex->min, kex->nbits, kex->max, dh_p, dh_g, pub_key,
			dh_server_pub, sshbuf_ptr(shared_secret), sshbuf_len(shared_secret),
			hash, &hashlen);
	sshbuf_free(empty);
	if (r != 0)
		fatal("Failed to calculate hash: %s", ssh_err(r));

	gss->buf.value = hash;
	gss->buf.length = hashlen;

	/* Verify that the hash matches the MIC we just got. */
	if (GSS_ERROR(ssh_gssapi_checkmic(gss, &gss->buf, &gss->msg_tok)))
		sshpkt_disconnect(ssh, "Hash's MIC didn't verify");

	gss_release_buffer(&gss->minor, &gss->msg_tok);

	if (kex->gss_deleg_creds)
		ssh_gssapi_credentials_updated(gss);

	if (gss_kex_context == NULL)
		gss_kex_context = gss;
	else
		ssh_gssapi_delete_ctx(&kex->gss);

	/* Finally derive the keys and send them */
	if ((r = kex_derive_keys(ssh, hash, hashlen, shared_secret)) == 0)
		r = kex_send_newkeys(ssh);

	if (kex->gss != NULL) {
		sshbuf_free(gss->server_host_key_blob);
		gss->server_host_key_blob = NULL;
		sshbuf_free(gss->server_blob);
		gss->server_blob = NULL;
	}
out:
	explicit_bzero(hash, sizeof(hash));
	DH_free(kex->dh);
	kex->dh = NULL;
	BN_clear_free(dh_server_pub);
	sshbuf_free(shared_secret);
	return r;
}

static int
kexgssgex_init_ctx(struct ssh *ssh,
		   gss_buffer_desc *token_ptr)
{
	struct kex *kex = ssh->kex;
	Gssctxt *gss = kex->gss;
	const BIGNUM *pub_key;
	gss_buffer_desc send_tok = GSS_C_EMPTY_BUFFER;
	OM_uint32 ret_flags;
	int r;

	/* Step 2 - call GSS_Init_sec_context() */
	debug("Calling gss_init_sec_context");

	gss->major = ssh_gssapi_init_ctx(gss, kex->gss_deleg_creds,
					 token_ptr, &send_tok, &ret_flags);

	if (GSS_ERROR(gss->major)) {
		/* XXX Useless code: Missing send? */
		if (send_tok.length != 0) {
			if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_CONTINUE)) != 0 ||
			    (r = sshpkt_put_string(ssh, send_tok.value, send_tok.length)) != 0)
				fatal("sshpkt failed: %s", ssh_err(r));
		}
		fatal("gss_init_context failed");
	}

	/* If we've got an old receive buffer get rid of it */
	if (token_ptr != GSS_C_NO_BUFFER)
		gss_release_buffer(&gss->minor, token_ptr);

	if (gss->major == GSS_S_COMPLETE) {
		/* If mutual state flag is not true, kex fails */
		if (!(ret_flags & GSS_C_MUTUAL_FLAG))
			fatal("Mutual authentication failed");

		/* If integ avail flag is not true kex fails */
		if (!(ret_flags & GSS_C_INTEG_FLAG))
			fatal("Integrity check failed");
	}

	/*
	 * If we have data to send, then the last message that we
	 * received cannot have been a 'complete'.
	 */
	if (send_tok.length != 0) {
		if (gss->first) {
	                DH_get0_key(kex->dh, &pub_key, NULL);
			if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_INIT)) != 0 ||
			    (r = sshpkt_put_string(ssh, send_tok.value, send_tok.length)) != 0 ||
			    (r = sshpkt_put_bignum2(ssh, pub_key)) != 0)
				fatal("failed to construct packet: %s", ssh_err(r));
			gss->first = 0;
		} else {
			if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_CONTINUE)) != 0 ||
			    (r = sshpkt_put_string(ssh, send_tok.value, send_tok.length)) != 0)
				fatal("failed to construct packet: %s", ssh_err(r));
		}
		if ((r = sshpkt_send(ssh)) != 0)
			fatal("failed to send packet: %s", ssh_err(r));
		gss_release_buffer(&gss->minor, &send_tok);

		/* If we've sent them data, they should reply */
		ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_HOSTKEY, &input_kexgss_hostkey);
		ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_CONTINUE, &input_kexgssgex_continue);
		ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_COMPLETE, &input_kexgssgex_complete);
		ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_ERROR, &input_kexgss_error);
		return 0;
	}
	/* No data, and not complete */
	if (gss->major != GSS_S_COMPLETE)
		fatal("Not complete, and no token output");

	if  (gss->major & GSS_S_CONTINUE_NEEDED)
		return kexgssgex_init_ctx(ssh, token_ptr);

	return kexgssgex_final(ssh);
}

static int
input_kexgssgex_group(int type,
		      u_int32_t seq,
		      struct ssh *ssh)
{
	struct kex *kex = ssh->kex;
	BIGNUM *p = NULL;
	BIGNUM *g = NULL;
	int r;

	debug("Received SSH2_MSG_KEXGSS_GROUP");
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_GROUP, NULL);

	if ((r = sshpkt_get_bignum2(ssh, &p)) != 0 ||
	    (r = sshpkt_get_bignum2(ssh, &g)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		fatal("shpkt_get_bignum2 failed: %s", ssh_err(r));

	if (BN_num_bits(p) < kex->min || BN_num_bits(p) > kex->max)
		fatal("GSSGRP_GEX group out of range: %d !< %d !< %d",
		    kex->min, BN_num_bits(p), kex->max);

	if ((kex->dh = dh_new_group(g, p)) == NULL)
		fatal("dn_new_group() failed");
	p = g = NULL; /* belong to kex->dh now */

	if ((r = dh_gen_key(kex->dh, kex->we_need * 8)) != 0) {
		ssh_gssapi_delete_ctx(&kex->gss);
		DH_free(kex->dh);
		kex->dh = NULL;
		return r;
	}

	return kexgssgex_init_ctx(ssh, GSS_C_NO_BUFFER);
}

static int
input_kexgssgex_continue(int type,
			 u_int32_t seq,
			 struct ssh *ssh)
{
	Gssctxt *gss = ssh->kex->gss;
	gss_buffer_desc recv_tok = GSS_C_EMPTY_BUFFER;
	int r;

	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_HOSTKEY, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_CONTINUE, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_COMPLETE, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_ERROR, NULL);

	debug("Received GSSAPI_CONTINUE");
	if (gss->major == GSS_S_COMPLETE)
		fatal("GSSAPI Continue received from server when complete");
	if ((r = ssh_gssapi_sshpkt_get_buffer_desc(ssh, &recv_tok)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		fatal("Failed to read token: %s", ssh_err(r));
	if  (!(gss->major & GSS_S_CONTINUE_NEEDED))
		fatal("Didn't receive a SSH2_MSG_KEXGSS_COMPLETE when I expected it");
	return kexgssgex_init_ctx(ssh, &recv_tok);
}

static int
input_kexgssgex_complete(int type,
		      u_int32_t seq,
		      struct ssh *ssh)
{
	Gssctxt *gss = ssh->kex->gss;
	gss_buffer_desc recv_tok = GSS_C_EMPTY_BUFFER;
	u_char c;
	int r;

	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_HOSTKEY, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_CONTINUE, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_COMPLETE, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_ERROR, NULL);

	debug("Received GSSAPI_COMPLETE");
	if (gss->msg_tok.value != NULL)
	        fatal("Received GSSAPI_COMPLETE twice?");
	if ((r = sshpkt_getb_froms(ssh, &gss->server_blob)) != 0 ||
	    (r = ssh_gssapi_sshpkt_get_buffer_desc(ssh, &gss->msg_tok)) != 0)
		fatal("Failed to read message: %s", ssh_err(r));

	/* Is there a token included? */
	if ((r = sshpkt_get_u8(ssh, &c)) != 0)
		fatal("sshpkt failed: %s", ssh_err(r));
	if (c) {
		if ((r = ssh_gssapi_sshpkt_get_buffer_desc(ssh, &recv_tok)) != 0)
			fatal("Failed to read token: %s", ssh_err(r));
		/* If we're already complete - protocol error */
		if (gss->major == GSS_S_COMPLETE)
			sshpkt_disconnect(ssh, "Protocol error: received token when complete");
	} else {
		if (gss->major != GSS_S_COMPLETE)
			sshpkt_disconnect(ssh, "Protocol error: did not receive final token");
	}
	if ((r = sshpkt_get_end(ssh)) != 0)
		fatal("Expecting end of packet.");

	if  (gss->major & GSS_S_CONTINUE_NEEDED)
		return kexgssgex_init_ctx(ssh, &recv_tok);

	gss_release_buffer(&gss->minor, &recv_tok);
	return kexgssgex_final(ssh);
}

#endif /* defined(GSSAPI) && defined(WITH_OPENSSL) */
