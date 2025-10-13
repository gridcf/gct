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

#include <string.h>

#include <openssl/crypto.h>
#include <openssl/bn.h>

#include "xmalloc.h"
#include "sshbuf.h"
#include "ssh2.h"
#include "sshkey.h"
#include "cipher.h"
#include "kex.h"
#include "log.h"
#include "packet.h"
#include "dh.h"
#include "ssh-gss.h"
#include "monitor_wrap.h"
#include "misc.h"      /* servconf.h needs misc.h for struct ForwardOptions */
#include "servconf.h"
#include "ssh-gss.h"
#include "digest.h"
#include "ssherr.h"

static void kex_gss_send_error(Gssctxt *ctxt, struct ssh *ssh);
extern ServerOptions options;

static int input_kexgss_init(int, u_int32_t, struct ssh *);
static int input_kexgss_continue(int, u_int32_t, struct ssh *);
static int input_kexgssgex_groupreq(int, u_int32_t, struct ssh *);
static int input_kexgssgex_init(int, u_int32_t, struct ssh *);
static int input_kexgssgex_continue(int, u_int32_t, struct ssh *);

int
kexgss_server(struct ssh *ssh)
{
	struct kex *kex = ssh->kex;
	gss_OID oid;
	char *mechs;

	/* Initialise GSSAPI */

	/* If we're rekeying, privsep means that some of the private structures
	 * in the GSSAPI code are no longer available. This kludges them back
	 * into life
	 */
	if (!ssh_gssapi_oid_table_ok()) {
		mechs = ssh_gssapi_server_mechanisms();
		free(mechs);
	}

	debug2_f("Identifying %s", kex->name);
	oid = ssh_gssapi_id_kex(NULL, kex->name, kex->kex_type);
	if (oid == GSS_C_NO_OID)
		fatal("Unknown gssapi mechanism");

	debug2_f("Acquiring credentials");

	if (GSS_ERROR(mm_ssh_gssapi_server_ctx(&kex->gss, oid))) {
		kex_gss_send_error(kex->gss, ssh);
		fatal("Unable to acquire credentials for the server");
	}

	ssh_gssapi_build_ctx(&kex->gss);
	if (kex->gss == NULL)
		fatal("Unable to allocate memory for gss context");

	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_INIT, &input_kexgss_init);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_CONTINUE, &input_kexgss_continue);
	debug("Wait SSH2_MSG_KEXGSS_INIT");
	return 0;
}

static inline void
kexgss_accept_ctx(struct ssh *ssh,
		  gss_buffer_desc *recv_tok,
		  gss_buffer_desc *send_tok,
		  OM_uint32 *ret_flags)
{
	Gssctxt *gss = ssh->kex->gss;
	int r;

	gss->major = mm_ssh_gssapi_accept_ctx(gss, recv_tok, send_tok, ret_flags);
	gss_release_buffer(&gss->minor, recv_tok);

	if (gss->major != GSS_S_COMPLETE && send_tok->length == 0)
		fatal("Zero length token output when incomplete");

	if (gss->buf.value == NULL)
		fatal("No client public key");

	if (gss->major & GSS_S_CONTINUE_NEEDED) {
		debug("Sending GSSAPI_CONTINUE");
		if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_CONTINUE)) != 0 ||
		    (r = sshpkt_put_string(ssh, send_tok->value, send_tok->length)) != 0 ||
		    (r = sshpkt_send(ssh)) != 0)
			fatal("sshpkt failed: %s", ssh_err(r));
		gss_release_buffer(&gss->minor, send_tok);
	}
}

static inline int
kexgss_final(struct ssh *ssh,
	     gss_buffer_desc *send_tok,
	     OM_uint32 *ret_flags)
{
	struct kex *kex = ssh->kex;
	Gssctxt *gss = kex->gss;
	gss_buffer_desc msg_tok;
	u_char hash[SSH_DIGEST_MAX_LENGTH];
	size_t hashlen;
	struct sshbuf *shared_secret = NULL;
	int r;

	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_INIT, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_CONTINUE, NULL);

	if (GSS_ERROR(gss->major)) {
		kex_gss_send_error(kex->gss, ssh);
		if (send_tok->length > 0) {
			if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_CONTINUE)) != 0 ||
			    (r = sshpkt_put_string(ssh, send_tok->value, send_tok->length)) != 0 ||
			    (r = sshpkt_send(ssh)) != 0)
				fatal("sshpkt failed: %s", ssh_err(r));
		}
		ssh_packet_disconnect(ssh, "GSSAPI Key Exchange handshake failed");
	}

	if (!(*ret_flags & GSS_C_MUTUAL_FLAG))
		fatal("Mutual Authentication flag wasn't set");

	if (!(*ret_flags & GSS_C_INTEG_FLAG))
		fatal("Integrity flag wasn't set");

	if (GSS_ERROR(mm_ssh_gssapi_sign(gss, &gss->buf, &msg_tok)))
		fatal("Couldn't get MIC");

	if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_COMPLETE)) != 0 ||
	    (r = sshpkt_put_stringb(ssh, gss->server_pubkey)) != 0 ||
	    (r = sshpkt_put_string(ssh, msg_tok.value, msg_tok.length)) != 0)
		fatal("sshpkt failed: %s", ssh_err(r));

	if (send_tok->length != 0) {
		if ((r = sshpkt_put_u8(ssh, 1)) != 0 || /* true */
		    (r = sshpkt_put_string(ssh, send_tok->value, send_tok->length)) != 0)
			fatal("sshpkt failed: %s", ssh_err(r));
	} else {
		if ((r = sshpkt_put_u8(ssh, 0)) != 0) /* false */
			fatal("sshpkt failed: %s", ssh_err(r));
	}
	if ((r = sshpkt_send(ssh)) != 0)
		fatal("sshpkt_send failed: %s", ssh_err(r));

	gss_release_buffer(&gss->minor, send_tok);
	gss_release_buffer(&gss->minor, &msg_tok);

	hashlen = gss->hashlen;
	memcpy(hash, gss->hash, hashlen);
	explicit_bzero(gss->hash, sizeof(gss->hash));
	shared_secret = gss->shared_secret;
	gss->shared_secret = NULL;

	if (gss_kex_context == NULL)
		gss_kex_context = gss;
	else
		ssh_gssapi_delete_ctx(&kex->gss);

	if ((r = kex_derive_keys(ssh, hash, hashlen, shared_secret)) == 0)
		r = kex_send_newkeys(ssh);

	/* If this was a rekey, then save out any delegated credentials we
	 * just exchanged.  */
	if (options.gss_store_rekey)
		ssh_gssapi_rekey_creds();

	if (kex->gss != NULL) {
		sshbuf_free(gss->server_pubkey);
		gss->server_pubkey = NULL;
	}
	explicit_bzero(hash, sizeof(hash));
	sshbuf_free(shared_secret);
	return r;
}

static int
input_kexgss_init(int type,
		  u_int32_t seq,
		  struct ssh *ssh)
{
	struct kex *kex = ssh->kex;
	Gssctxt *gss = kex->gss;
	struct sshbuf *empty;
	struct sshbuf *client_pubkey = NULL;
	gss_buffer_desc recv_tok, send_tok = GSS_C_EMPTY_BUFFER;
	OM_uint32 ret_flags = 0;
	int r;

	debug("SSH2_MSG_KEXGSS_INIT received");
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_INIT, NULL);

	if ((r = ssh_gssapi_sshpkt_get_buffer_desc(ssh, &recv_tok)) != 0 ||
	    (r = sshpkt_getb_froms(ssh, &client_pubkey)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		fatal("sshpkt failed: %s", ssh_err(r));

	switch (kex->kex_type) {
	case KEX_GSS_GRP1_SHA1:
	case KEX_GSS_GRP14_SHA1:
	case KEX_GSS_GRP14_SHA256:
	case KEX_GSS_GRP16_SHA512:
		r = kex_dh_enc(kex, client_pubkey, &gss->server_pubkey, &gss->shared_secret);
		break;
	case KEX_GSS_NISTP256_SHA256:
		r = kex_ecdh_enc(kex, client_pubkey, &gss->server_pubkey, &gss->shared_secret);
		break;
	case KEX_GSS_C25519_SHA256:
		r = kex_c25519_enc(kex, client_pubkey, &gss->server_pubkey, &gss->shared_secret);
		break;
	default:
		fatal_f("Unexpected KEX type %d", kex->kex_type);
	}
	if (r != 0) {
		sshbuf_free(client_pubkey);
		gss_release_buffer(&gss->minor, &recv_tok);
		ssh_gssapi_delete_ctx(&kex->gss);
		return r;
	}

	/* Send SSH_MSG_KEXGSS_HOSTKEY here, if we want */

	if ((empty = sshbuf_new()) == NULL) {
		sshbuf_free(client_pubkey);
		gss_release_buffer(&gss->minor, &recv_tok);
		ssh_gssapi_delete_ctx(&kex->gss);
		return SSH_ERR_ALLOC_FAIL;
	}

	/* Calculate the hash early so we can free the
	 * client_pubkey, which has reference to the parent
	 * buffer state->incoming_packet
	 */
	gss->hashlen = sizeof(gss->hash);
	r = kex_gen_hash(kex->hash_alg, kex->client_version, kex->server_version,
			 kex->peer, kex->my, empty, client_pubkey, gss->server_pubkey,
			 gss->shared_secret, gss->hash, &gss->hashlen);
	sshbuf_free(empty);
	sshbuf_free(client_pubkey);
	if (r != 0) {
		gss_release_buffer(&gss->minor, &recv_tok);
		ssh_gssapi_delete_ctx(&kex->gss);
		return r;
	}

	gss->buf.value = gss->hash;
	gss->buf.length = gss->hashlen;

	kexgss_accept_ctx(ssh, &recv_tok, &send_tok, &ret_flags);
	if (gss->major & GSS_S_CONTINUE_NEEDED)
		return 0;

	return kexgss_final(ssh, &send_tok, &ret_flags);
}

static int
input_kexgss_continue(int type,
		      u_int32_t seq,
		      struct ssh *ssh)
{
	Gssctxt *gss = ssh->kex->gss;
	gss_buffer_desc recv_tok, send_tok = GSS_C_EMPTY_BUFFER;
	OM_uint32 ret_flags = 0;
	int r;

	if ((r = ssh_gssapi_sshpkt_get_buffer_desc(ssh, &recv_tok)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		fatal("sshpkt failed: %s", ssh_err(r));

	kexgss_accept_ctx(ssh, &recv_tok, &send_tok, &ret_flags);
	if (gss->major & GSS_S_CONTINUE_NEEDED)
		return 0;

	return kexgss_final(ssh, &send_tok, &ret_flags);
}

/*******************************************************/
/******************** KEXGSSGEX ************************/
/*******************************************************/

int
kexgssgex_server(struct ssh *ssh)
{
	struct kex *kex = ssh->kex;
	gss_OID oid;
	char *mechs;

	/* Initialise GSSAPI */

	/* If we're rekeying, privsep means that some of the private structures
	 * in the GSSAPI code are no longer available. This kludges them back
	 * into life
	 */
	if (!ssh_gssapi_oid_table_ok()) {
		mechs = ssh_gssapi_server_mechanisms();
		free(mechs);
	}

	debug2_f("Identifying %s", kex->name);
	oid = ssh_gssapi_id_kex(NULL, kex->name, kex->kex_type);
	if (oid == GSS_C_NO_OID)
		fatal("Unknown gssapi mechanism");

	debug2_f("Acquiring credentials");

	if (GSS_ERROR(mm_ssh_gssapi_server_ctx(&kex->gss, oid)))
		fatal("Unable to acquire credentials for the server");

	ssh_gssapi_build_ctx(&kex->gss);
	if (kex->gss == NULL)
		fatal("Unable to allocate memory for gss context");

	debug("Doing group exchange");
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_GROUPREQ, &input_kexgssgex_groupreq);
	return 0;
}

static inline void
kexgssgex_accept_ctx(struct ssh *ssh,
		     gss_buffer_desc *recv_tok,
		     gss_buffer_desc *send_tok,
		     OM_uint32 *ret_flags)
{
	Gssctxt *gss = ssh->kex->gss;
	int r;

	gss->major = mm_ssh_gssapi_accept_ctx(gss, recv_tok, send_tok, ret_flags);
	gss_release_buffer(&gss->minor, recv_tok);

	if (gss->major != GSS_S_COMPLETE && send_tok->length == 0)
		fatal("Zero length token output when incomplete");

	if (gss->dh_client_pub == NULL)
		fatal("No client public key");

	if (gss->major & GSS_S_CONTINUE_NEEDED) {
		debug("Sending GSSAPI_CONTINUE");
		if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_CONTINUE)) != 0 ||
		    (r = sshpkt_put_string(ssh, send_tok->value, send_tok->length)) != 0 ||
		    (r = sshpkt_send(ssh)) != 0)
			fatal("sshpkt failed: %s", ssh_err(r));
		gss_release_buffer(&gss->minor, send_tok);
	}
}

static inline int
kexgssgex_final(struct ssh *ssh,
		gss_buffer_desc *send_tok,
		OM_uint32 *ret_flags)
{
	struct kex *kex = ssh->kex;
	Gssctxt *gss = kex->gss;
	gss_buffer_desc msg_tok;
	u_char hash[SSH_DIGEST_MAX_LENGTH];
	size_t hashlen;
	const BIGNUM *pub_key, *dh_p, *dh_g;
	struct sshbuf *shared_secret = NULL;
	struct sshbuf *empty = NULL;
	int r;

	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_INIT, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_CONTINUE, NULL);

	if (GSS_ERROR(gss->major)) {
		if (send_tok->length > 0) {
			if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_CONTINUE)) != 0 ||
			    (r = sshpkt_put_string(ssh, send_tok->value, send_tok->length)) != 0 ||
			    (r = sshpkt_send(ssh)) != 0)
				fatal("sshpkt failed: %s", ssh_err(r));
		}
		fatal("accept_ctx died");
	}

	if (!(*ret_flags & GSS_C_MUTUAL_FLAG))
		fatal("Mutual Authentication flag wasn't set");

	if (!(*ret_flags & GSS_C_INTEG_FLAG))
		fatal("Integrity flag wasn't set");

	/* calculate shared secret */
	shared_secret = sshbuf_new();
	if (shared_secret == NULL) {
		ssh_gssapi_delete_ctx(&kex->gss);
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if ((r = kex_dh_compute_key(kex, gss->dh_client_pub, shared_secret)) != 0) {
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
	r = kexgex_hash(kex->hash_alg, kex->client_version, kex->server_version,
			kex->peer, kex->my, empty, kex->min, kex->nbits, kex->max, dh_p, dh_g,
			gss->dh_client_pub, pub_key, sshbuf_ptr(shared_secret),
			sshbuf_len(shared_secret), hash, &hashlen);
	sshbuf_free(empty);
	if (r != 0)
		fatal("kexgex_hash failed: %s", ssh_err(r));

	gss->buf.value = hash;
	gss->buf.length = hashlen;

	if (GSS_ERROR(mm_ssh_gssapi_sign(gss, &gss->buf, &msg_tok)))
		fatal("Couldn't get MIC");

	if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_COMPLETE)) != 0 ||
	    (r = sshpkt_put_bignum2(ssh, pub_key)) != 0 ||
	    (r = sshpkt_put_string(ssh, msg_tok.value, msg_tok.length)) != 0)
		fatal("sshpkt failed: %s", ssh_err(r));

	if (send_tok->length != 0) {
		if ((r = sshpkt_put_u8(ssh, 1)) != 0 || /* true */
		    (r = sshpkt_put_string(ssh, send_tok->value, send_tok->length)) != 0)
			fatal("sshpkt failed: %s", ssh_err(r));
	} else {
		if ((r = sshpkt_put_u8(ssh, 0)) != 0) /* false */
			fatal("sshpkt failed: %s", ssh_err(r));
	}
	if ((r = sshpkt_send(ssh)) != 0)
		fatal("sshpkt_send failed: %s", ssh_err(r));

	gss_release_buffer(&gss->minor, send_tok);
	gss_release_buffer(&gss->minor, &msg_tok);

	if (gss_kex_context == NULL)
		gss_kex_context = gss;
	else
		ssh_gssapi_delete_ctx(&kex->gss);

	/* Finally derive the keys and send them */
	if ((r = kex_derive_keys(ssh, hash, hashlen, shared_secret)) == 0)
		r = kex_send_newkeys(ssh);

	/* If this was a rekey, then save out any delegated credentials we
	 * just exchanged.  */
	if (options.gss_store_rekey)
		ssh_gssapi_rekey_creds();

	if (kex->gss != NULL)
		BN_clear_free(gss->dh_client_pub);

out:
	explicit_bzero(hash, sizeof(hash));
	DH_free(kex->dh);
	kex->dh = NULL;
	sshbuf_free(shared_secret);
	return r;
}

static int
input_kexgssgex_groupreq(int type,
			 u_int32_t seq,
			 struct ssh *ssh)
{
	struct kex *kex = ssh->kex;
	const BIGNUM *dh_p, *dh_g;
	int min = -1, max = -1, nbits = -1;
	int cmin = -1, cmax = -1; /* client proposal */
	int r;

	/* 5. S generates an ephemeral key pair (do the allocations early) */

	debug("SSH2_MSG_KEXGSS_GROUPREQ received");
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_GROUPREQ, NULL);

	/* store client proposal to provide valid signature */
	if ((r = sshpkt_get_u32(ssh, &cmin)) != 0 ||
	    (r = sshpkt_get_u32(ssh, &nbits)) != 0 ||
	    (r = sshpkt_get_u32(ssh, &cmax)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		fatal("sshpkt failed: %s", ssh_err(r));

	kex->nbits = nbits;
	kex->min = cmin;
	kex->max = cmax;
	min = MAX(DH_GRP_MIN, cmin);
	max = MIN(DH_GRP_MAX, cmax);
	nbits = MAXIMUM(DH_GRP_MIN, nbits);
	nbits = MINIMUM(DH_GRP_MAX, nbits);

	if (max < min || nbits < min || max < nbits)
		fatal("GSS_GEX, bad parameters: %d !< %d !< %d", min, nbits, max);

	kex->dh = mm_choose_dh(min, nbits, max);
	if (kex->dh == NULL) {
		sshpkt_disconnect(ssh, "Protocol error: no matching group found");
		fatal("Protocol error: no matching group found");
	}

	DH_get0_pqg(kex->dh, &dh_p, NULL, &dh_g);
	if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_GROUP)) != 0 ||
	    (r = sshpkt_put_bignum2(ssh, dh_p)) != 0 ||
	    (r = sshpkt_put_bignum2(ssh, dh_g)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		fatal("sshpkt failed: %s", ssh_err(r));

	if ((r = ssh_packet_write_wait(ssh)) != 0)
		fatal("ssh_packet_write_wait: %s", ssh_err(r));

	/* Compute our exchange value in parallel with the client */
	if ((r = dh_gen_key(kex->dh, kex->we_need * 8)) != 0) {
		ssh_gssapi_delete_ctx(&kex->gss);
		DH_free(kex->dh);
		kex->dh = NULL;
		return r;
	}

	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_INIT, &input_kexgssgex_init);
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_CONTINUE, &input_kexgssgex_continue);
	debug("Wait SSH2_MSG_KEXGSS_INIT");
	return 0;
}

static int
input_kexgssgex_init(int type,
		     u_int32_t seq,
		     struct ssh *ssh)
{
	Gssctxt *gss = ssh->kex->gss;
	gss_buffer_desc recv_tok, send_tok = GSS_C_EMPTY_BUFFER;
	OM_uint32 ret_flags = 0;
	int r;

	debug("SSH2_MSG_KEXGSS_INIT received");
	ssh_dispatch_set(ssh, SSH2_MSG_KEXGSS_INIT, NULL);

	if ((r = ssh_gssapi_sshpkt_get_buffer_desc(ssh, &recv_tok)) != 0 ||
	    (r = sshpkt_get_bignum2(ssh, &gss->dh_client_pub)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		fatal("sshpkt failed: %s", ssh_err(r));

	/* Send SSH_MSG_KEXGSS_HOSTKEY here, if we want */

	kexgssgex_accept_ctx(ssh, &recv_tok, &send_tok, &ret_flags);
	if (gss->major & GSS_S_CONTINUE_NEEDED)
		return 0;

	return kexgssgex_final(ssh, &send_tok, &ret_flags);
}

static int
input_kexgssgex_continue(int type,
			 u_int32_t seq,
			 struct ssh *ssh)
{
	Gssctxt *gss = ssh->kex->gss;
	gss_buffer_desc recv_tok, send_tok = GSS_C_EMPTY_BUFFER;
	OM_uint32 ret_flags = 0;
	int r;

	if ((r = ssh_gssapi_sshpkt_get_buffer_desc(ssh, &recv_tok)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		fatal("sshpkt failed: %s", ssh_err(r));

	kexgssgex_accept_ctx(ssh, &recv_tok, &send_tok, &ret_flags);
	if (gss->major & GSS_S_CONTINUE_NEEDED)
		return 0;

	return kexgssgex_final(ssh, &send_tok, &ret_flags);
}

static void
kex_gss_send_error(Gssctxt *ctxt, struct ssh *ssh) {
	char *errstr;
	OM_uint32 maj, min;
	int r;

	errstr = mm_ssh_gssapi_last_error(ctxt, &maj, &min);
	if (errstr) {
		if ((r = sshpkt_start(ssh, SSH2_MSG_KEXGSS_ERROR)) != 0 ||
		    (r = sshpkt_put_u32(ssh, maj)) != 0 ||
		    (r = sshpkt_put_u32(ssh, min)) != 0 ||
		    (r = sshpkt_put_cstring(ssh, errstr)) != 0 ||
		    (r = sshpkt_put_cstring(ssh, "")) != 0 ||
		    (r = sshpkt_send(ssh)) != 0)
			fatal("sshpkt failed: %s", ssh_err(r));
		if ((r = ssh_packet_write_wait(ssh)) != 0)
			fatal("ssh_packet_write_wait: %s", ssh_err(r));
		/* XXX - We should probably log the error locally here */
		free(errstr);
	}
}

#endif /* defined(GSSAPI) && defined(WITH_OPENSSL) */
