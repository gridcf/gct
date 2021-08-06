/* $OpenBSD: cipher-ctr.c,v 1.11 2010/10/01 23:05:32 djm Exp $ */
/*
 * Copyright (c) 2003 Markus Friedl <markus@openbsd.org>
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
#include "includes.h"

#if defined(WITH_OPENSSL) && !defined(OPENSSL_HAVE_EVPCTR)
#include <sys/types.h>

#include <stdarg.h>
#include <string.h>

#include <openssl/evp.h>

#include "xmalloc.h"
#include "log.h"

/* compatibility with old or broken OpenSSL versions */
#include "openbsd-compat/openssl-compat.h"

#ifndef USE_BUILTIN_RIJNDAEL
#include <openssl/aes.h>
#endif

struct ssh_aes_ctr_ctx
{
	EVP_CIPHER_CTX	ecbctx;
	u_char		aes_counter[AES_BLOCK_SIZE];
};

/*
 * increment counter 'ctr',
 * the counter is of size 'len' bytes and stored in network-byte-order.
 * (LSB at ctr[len-1], MSB at ctr[0])
 */
static void
ssh_ctr_inc(u_char *ctr, size_t len)
{
	int i;

	for (i = len - 1; i >= 0; i--)
		if (++ctr[i])	/* continue on overflow */
			return;
}

static int
ssh_aes_ctr(EVP_CIPHER_CTX *ctx, u_char *dest, const u_char *src,
    LIBCRYPTO_EVP_INL_TYPE len)
{
	struct ssh_aes_ctr_ctx *c;
	size_t n = 0;
	u_char ctrbuf[AES_BLOCK_SIZE*256];
	u_char buf[AES_BLOCK_SIZE*256];

	if (len == 0)
		return (1);
	if ((c = EVP_CIPHER_CTX_get_app_data(ctx)) == NULL)
		return (0);

	for (; len > 0; len -= sizeof(u_int)) {
		u_int r,a,b;

		if (n == 0) {
			int outl, i, buflen;

			buflen = MIN(len, sizeof(ctrbuf));

			for(i = 0; i < buflen; i += AES_BLOCK_SIZE) {
				memcpy(&ctrbuf[i], c->aes_counter, AES_BLOCK_SIZE);
				ssh_ctr_inc(c->aes_counter, AES_BLOCK_SIZE);
			}

			EVP_EncryptUpdate(&c->ecbctx, buf, &outl,
				ctrbuf, buflen);
		}

		memcpy(&a, src, sizeof(a));
		memcpy(&b, &buf[n], sizeof(b));
		r = a ^ b;
		memcpy(dest, &r, sizeof(r));
		src += sizeof(a);
		dest += sizeof(r);

		n = (n + sizeof(b)) % sizeof(buf);
	}
	memset(ctrbuf, '\0', sizeof(ctrbuf));
	memset(buf, '\0', sizeof(buf));
	return (1);
}

static int
ssh_aes_ctr_init(EVP_CIPHER_CTX *ctx, const u_char *key, const u_char *iv,
    int enc)
{
	struct ssh_aes_ctr_ctx *c;

	if ((c = EVP_CIPHER_CTX_get_app_data(ctx)) == NULL) {
		c = xmalloc(sizeof(*c));
		EVP_CIPHER_CTX_set_app_data(ctx, c);
	}

	EVP_CIPHER_CTX_init(&c->ecbctx);

	if (key != NULL) {
		const EVP_CIPHER *cipher;
		switch(EVP_CIPHER_CTX_key_length(ctx)*8) {
			case 128:
				cipher = EVP_aes_128_ecb();
				break;
			case 192:
				cipher = EVP_aes_192_ecb();
				break;
			case 256:
				cipher = EVP_aes_256_ecb();
				break;
			default:
				fatal("ssh_aes_ctr_init: wrong aes key length");
		}
		if(!EVP_EncryptInit_ex(&c->ecbctx, cipher, NULL, key, NULL))
			fatal("ssh_aes_ctr_init: cannot initialize aes encryption");
		EVP_CIPHER_CTX_set_padding(&c->ecbctx, 0);
	}
	if (iv != NULL)
		memcpy(c->aes_counter, iv, AES_BLOCK_SIZE);
	return (1);
}

static int
ssh_aes_ctr_cleanup(EVP_CIPHER_CTX *ctx)
{
	struct ssh_aes_ctr_ctx *c;

	if ((c = EVP_CIPHER_CTX_get_app_data(ctx)) != NULL) {
		EVP_CIPHER_CTX_cleanup(&c->ecbctx);
		memset(c, 0, sizeof(*c));
		free(c);
		EVP_CIPHER_CTX_set_app_data(ctx, NULL);
	}
	return (1);
}

void
ssh_aes_ctr_iv(EVP_CIPHER_CTX *evp, int doset, u_char * iv, size_t len)
{
	struct ssh_aes_ctr_ctx *c;

	if ((c = EVP_CIPHER_CTX_get_app_data(evp)) == NULL)
		fatal("ssh_aes_ctr_iv: no context");
	if (doset)
		memcpy(c->aes_counter, iv, len);
	else
		memcpy(iv, c->aes_counter, len);
}

const EVP_CIPHER *
evp_aes_128_ctr(void)
{
	static EVP_CIPHER aes_ctr;

	memset(&aes_ctr, 0, sizeof(EVP_CIPHER));
	aes_ctr.nid = NID_undef;
	aes_ctr.block_size = AES_BLOCK_SIZE;
	aes_ctr.iv_len = AES_BLOCK_SIZE;
	aes_ctr.key_len = 16;
	aes_ctr.init = ssh_aes_ctr_init;
	aes_ctr.cleanup = ssh_aes_ctr_cleanup;
	aes_ctr.do_cipher = ssh_aes_ctr;
#ifndef SSH_OLD_EVP
	aes_ctr.flags = EVP_CIPH_CBC_MODE | EVP_CIPH_VARIABLE_LENGTH |
	    EVP_CIPH_ALWAYS_CALL_INIT | EVP_CIPH_CUSTOM_IV |
	    EVP_CIPH_FLAG_FIPS;
#endif
	return (&aes_ctr);
}

#endif /* defined(WITH_OPENSSL) && !defined(OPENSSL_HAVE_EVPCTR) */
