/* $OpenBSD: ssh-pkcs11.c,v 1.63 2024/08/15 00:51:51 djm Exp $ */
/*
 * Copyright (c) 2010 Markus Friedl.  All rights reserved.
 * Copyright (c) 2014 Pedro Martelletto. All rights reserved.
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

#ifdef ENABLE_PKCS11

#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>

#include <ctype.h>
#include <string.h>
#include <dlfcn.h>

#include "openbsd-compat/sys-queue.h"
#include "openbsd-compat/openssl-compat.h"

#include <openssl/ecdsa.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <openssl/evp.h>

#define CRYPTOKI_COMPAT
#include "pkcs11.h"

#include "log.h"
#include "misc.h"
#include "sshkey.h"
#include "ssh-pkcs11.h"
#include "digest.h"
#include "xmalloc.h"

struct pkcs11_slotinfo {
	CK_TOKEN_INFO		token;
	CK_SESSION_HANDLE	session;
	int			logged_in;
};

struct pkcs11_module {
	char			*module_path;
	void			*handle;
	CK_FUNCTION_LIST	*function_list;
	CK_INFO			info;
	CK_ULONG		nslots;
	CK_SLOT_ID		*slotlist;
	struct pkcs11_slotinfo	*slotinfo;
	int			valid;
	int			refcount;
};

struct pkcs11_provider {
	char			*name;
	struct pkcs11_module	*module; /* can be shared between various providers */
	int			refcount;
	int			valid;
	TAILQ_ENTRY(pkcs11_provider) next;
};

TAILQ_HEAD(, pkcs11_provider) pkcs11_providers;

struct pkcs11_key {
	struct pkcs11_provider	*provider;
	CK_ULONG		slotidx;
	char			*keyid;
	int			keyid_len;
	char			*label;
};

int pkcs11_interactive = 0;

#if defined(OPENSSL_HAS_ECC) && defined(HAVE_EC_KEY_METHOD_NEW)
static void
ossl_error(const char *msg)
{
	unsigned long    e;

	error_f("%s", msg);
	while ((e = ERR_get_error()) != 0)
		error_f("libcrypto error: %s", ERR_error_string(e, NULL));
}
#endif /* OPENSSL_HAS_ECC && HAVE_EC_KEY_METHOD_NEW */

int
pkcs11_init(int interactive)
{
	pkcs11_interactive = interactive;
	TAILQ_INIT(&pkcs11_providers);
	return (0);
}

/*
 * finalize a provider shared library, it's no longer usable.
 * however, there might still be keys referencing this provider,
 * so the actual freeing of memory is handled by pkcs11_provider_unref().
 * this is called when a provider gets unregistered.
 */
static void
pkcs11_module_finalize(struct pkcs11_module *m)
{
	CK_RV rv;
	CK_ULONG i;

	debug_f("%p refcount %d valid %d", m, m->refcount, m->valid);
	if (!m->valid)
		return;
	for (i = 0; i < m->nslots; i++) {
		if (m->slotinfo[i].session &&
		    (rv = m->function_list->C_CloseSession(
		    m->slotinfo[i].session)) != CKR_OK)
			error("C_CloseSession failed: %lu", rv);
	}
	if ((rv = m->function_list->C_Finalize(NULL)) != CKR_OK)
		error("C_Finalize failed: %lu", rv);
	m->valid = 0;
	m->function_list = NULL;
	dlclose(m->handle);
}

/*
 * remove a reference to the pkcs11 module.
 * called when a provider is unregistered.
 */
static void
pkcs11_module_unref(struct pkcs11_module *m)
{
	debug_f("%p refcount %d", m, m->refcount);
	if (--m->refcount <= 0) {
		pkcs11_module_finalize(m);
		if (m->valid)
			error_f("%p still valid", m);
		free(m->slotlist);
		free(m->slotinfo);
		free(m->module_path);
		free(m);
	}
}

/*
 * finalize a provider shared library, it's no longer usable.
 * however, there might still be keys referencing this provider,
 * so the actual freeing of memory is handled by pkcs11_provider_unref().
 * this is called when a provider gets unregistered.
 */
static void
pkcs11_provider_finalize(struct pkcs11_provider *p)
{
	debug_f("%p refcount %d valid %d", p, p->refcount, p->valid);
	if (!p->valid)
		return;
	pkcs11_module_unref(p->module);
	p->module = NULL;
	p->valid = 0;
}

/*
 * remove a reference to the provider.
 * called when a key gets destroyed or when the provider is unregistered.
 */
static void
pkcs11_provider_unref(struct pkcs11_provider *p)
{
	debug_f("provider \"%s\" refcount %d", p->name, p->refcount);
	if (--p->refcount <= 0) {
		free(p->name);
		if (p->module)
			pkcs11_module_unref(p->module);
		free(p);
	}
}

/* unregister all providers, keys might still point to the providers */
void
pkcs11_terminate(void)
{
	struct pkcs11_provider *p;

	while ((p = TAILQ_FIRST(&pkcs11_providers)) != NULL) {
		TAILQ_REMOVE(&pkcs11_providers, p, next);
		pkcs11_provider_finalize(p);
		pkcs11_provider_unref(p);
	}
}

/* lookup provider by module path */
static struct pkcs11_module *
pkcs11_provider_lookup_module(char *module_path)
{
	struct pkcs11_provider *p;

	TAILQ_FOREACH(p, &pkcs11_providers, next) {
		debug("check %p %s (%s)", p, p->name, p->module->module_path);
		if (!strcmp(module_path, p->module->module_path))
			return (p->module);
	}
	return (NULL);
}

/* lookup provider by name */
static struct pkcs11_provider *
pkcs11_provider_lookup(char *provider_id)
{
	struct pkcs11_provider *p;

	TAILQ_FOREACH(p, &pkcs11_providers, next) {
		debug("check provider \"%s\"", p->name);
		if (!strcmp(provider_id, p->name))
			return (p);
	}
	return (NULL);
}

int pkcs11_del_provider_by_uri(struct pkcs11_uri *);

/* unregister provider by name */
int
pkcs11_del_provider(char *provider_id)
{
	int rv;
	struct pkcs11_uri *uri;

	debug_f("called, provider_id = %s", provider_id);

      if (provider_id == NULL)
          return 0;

	uri = pkcs11_uri_init();
	if (uri == NULL)
		fatal("Failed to init PKCS#11 URI");

	if (strlen(provider_id) >= strlen(PKCS11_URI_SCHEME) &&
	    strncmp(provider_id, PKCS11_URI_SCHEME, strlen(PKCS11_URI_SCHEME)) == 0) {
		if (pkcs11_uri_parse(provider_id, uri) != 0)
			fatal("Failed to parse PKCS#11 URI");
	} else {
		uri->module_path = strdup(provider_id);
	}

	rv = pkcs11_del_provider_by_uri(uri);
	pkcs11_uri_cleanup(uri);
	return rv;
}

/* unregister provider by PKCS#11 URI */
int
pkcs11_del_provider_by_uri(struct pkcs11_uri *uri)
{
	struct pkcs11_provider *p;
	int rv = -1;
	char *provider_uri = pkcs11_uri_get(uri);

	debug3_f("called with provider %s", provider_uri);

	if ((p = pkcs11_provider_lookup(provider_uri)) != NULL) {
		TAILQ_REMOVE(&pkcs11_providers, p, next);
		pkcs11_provider_finalize(p);
		pkcs11_provider_unref(p);
		rv = 0;
	}
	free(provider_uri);
	return rv;
}

static RSA_METHOD *rsa_method;
static int rsa_idx = 0;
#if defined(OPENSSL_HAS_ECC) && defined(HAVE_EC_KEY_METHOD_NEW)
static EC_KEY_METHOD *ec_key_method;
static int ec_key_idx = 0;
#endif /* OPENSSL_HAS_ECC && HAVE_EC_KEY_METHOD_NEW */

/*
 * This can't be in the ssh-pkcs11-uri, becase we can not depend on
 * PKCS#11 structures in ssh-agent (using client-helper communication)
 */
int
pkcs11_uri_write(const struct sshkey *key, FILE *f)
{
	char *p = NULL;
	struct pkcs11_uri uri;
	struct pkcs11_key *k11;

	/* sanity - is it a RSA key with associated app_data? */
	switch (key->type) {
	case KEY_RSA: {
               const RSA *rsa = EVP_PKEY_get0_RSA(key->pkey);
		k11 = RSA_get_ex_data(rsa, rsa_idx);
		break;
		}
#ifdef HAVE_EC_KEY_METHOD_NEW
	case KEY_ECDSA: {
               const EC_KEY * ecdsa = EVP_PKEY_get0_EC_KEY(key->pkey);
		k11 = EC_KEY_get_ex_data(ecdsa, ec_key_idx);
		break;
		}
#endif
	default:
		error("Unknown key type %d", key->type);
		return -1;
	}
	if (k11 == NULL) {
		error("Failed to get ex_data for key type %d", key->type);
		return (-1);
	}

	/* omit type -- we are looking for private-public or private-certificate pairs */
	uri.id = k11->keyid;
	uri.id_len = k11->keyid_len;
	uri.token = k11->provider->module->slotinfo[k11->slotidx].token.label;
	uri.object = k11->label;
	uri.module_path = k11->provider->module->module_path;
	uri.lib_manuf = k11->provider->module->info.manufacturerID;
	uri.manuf = k11->provider->module->slotinfo[k11->slotidx].token.manufacturerID;
	uri.serial = k11->provider->module->slotinfo[k11->slotidx].token.serialNumber;

	p = pkcs11_uri_get(&uri);
	/* do not cleanup -- we do not allocate here, only reference */
	if (p == NULL)
		return -1;

	fprintf(f, " %s", p);
	free(p);
	return 0;
}

/* release a wrapped object */
static void
pkcs11_k11_free(void *parent, void *ptr, CRYPTO_EX_DATA *ad, int idx,
    long argl, void *argp)
{
	struct pkcs11_key	*k11 = ptr;

	debug_f("parent %p ptr %p idx %d", parent, ptr, idx);
	if (k11 == NULL)
		return;
	if (k11->provider)
		pkcs11_provider_unref(k11->provider);
	free(k11->keyid);
	free(k11->label);
	free(k11);
}

/* find a single 'obj' for given attributes */
static int
pkcs11_find(struct pkcs11_provider *p, CK_ULONG slotidx, CK_ATTRIBUTE *attr,
    CK_ULONG nattr, CK_OBJECT_HANDLE *obj)
{
	CK_FUNCTION_LIST	*f;
	CK_SESSION_HANDLE	session;
	CK_ULONG		nfound = 0;
	CK_RV			rv;
	int			ret = -1;

	f = p->module->function_list;
	session = p->module->slotinfo[slotidx].session;
	if ((rv = f->C_FindObjectsInit(session, attr, nattr)) != CKR_OK) {
		error("C_FindObjectsInit failed (nattr %lu): %lu", nattr, rv);
		return (-1);
	}
	if ((rv = f->C_FindObjects(session, obj, 1, &nfound)) != CKR_OK ||
	    nfound != 1) {
		debug("C_FindObjects failed (nfound %lu nattr %lu): %lu",
		    nfound, nattr, rv);
	} else
		ret = 0;
	if ((rv = f->C_FindObjectsFinal(session)) != CKR_OK)
		error("C_FindObjectsFinal failed: %lu", rv);
	return (ret);
}

static int
pkcs11_login_slot(struct pkcs11_provider *provider, struct pkcs11_slotinfo *si,
    CK_USER_TYPE type)
{
	char			*pin = NULL, prompt[1024];
	CK_RV			 rv;

	if (provider == NULL || si == NULL || !provider->valid) {
		error("no pkcs11 (valid) provider found");
		return (-1);
	}

	if (!pkcs11_interactive) {
		error("need pin entry%s",
		    (si->token.flags & CKF_PROTECTED_AUTHENTICATION_PATH) ?
		    " on reader keypad" : "");
		return (-1);
	}
	if (si->token.flags & CKF_PROTECTED_AUTHENTICATION_PATH)
		verbose("Deferring PIN entry to reader keypad.");
	else {
		snprintf(prompt, sizeof(prompt), "Enter PIN for '%.32s': ",
		    si->token.label);
		if ((pin = read_passphrase(prompt, RP_ALLOW_EOF|RP_ALLOW_STDIN)) == NULL) {
			debug_f("no pin specified");
			return (-1);	/* bail out */
		}
	}
	rv = provider->module->function_list->C_Login(si->session, type, (u_char *)pin,
	    (pin != NULL) ? strlen(pin) : 0);
	if (pin != NULL)
		freezero(pin, strlen(pin));

	switch (rv) {
	case CKR_OK:
	case CKR_USER_ALREADY_LOGGED_IN:
		/* success */
		break;
	case CKR_PIN_LEN_RANGE:
		error("PKCS#11 login failed: PIN length out of range");
		return -1;
	case CKR_PIN_INCORRECT:
		error("PKCS#11 login failed: PIN incorrect");
		return -1;
	case CKR_PIN_LOCKED:
		error("PKCS#11 login failed: PIN locked");
		return -1;
	default:
		error("PKCS#11 login failed: error %lu", rv);
		return -1;
	}
	si->logged_in = 1;
	return (0);
}

static int
pkcs11_login(struct pkcs11_key *k11, CK_USER_TYPE type)
{
	if (k11 == NULL || k11->provider == NULL || !k11->provider->valid ||
	    k11->provider->module == NULL || !k11->provider->module->valid) {
		error("no pkcs11 (valid) provider found");
		return (-1);
	}

	return pkcs11_login_slot(k11->provider,
	    &k11->provider->module->slotinfo[k11->slotidx], type);
}


static int
pkcs11_check_obj_bool_attrib(struct pkcs11_key *k11, CK_OBJECT_HANDLE obj,
    CK_ATTRIBUTE_TYPE type, int *val)
{
	struct pkcs11_slotinfo	*si;
	CK_FUNCTION_LIST	*f;
	CK_BBOOL		flag = 0;
	CK_ATTRIBUTE		attr;
	CK_RV			 rv;

	*val = 0;

	if (!k11->provider || !k11->provider->valid ||
	    !k11->provider->module || !k11->provider->module->valid) {
		error("no pkcs11 (valid) provider found");
		return (-1);
	}

	f = k11->provider->module->function_list;
	si = &k11->provider->module->slotinfo[k11->slotidx];

	attr.type = type;
	attr.pValue = &flag;
	attr.ulValueLen = sizeof(flag);

	rv = f->C_GetAttributeValue(si->session, obj, &attr, 1);
	if (rv != CKR_OK) {
		error("C_GetAttributeValue failed: %lu", rv);
		return (-1);
	}
	*val = flag != 0;
	debug_f("provider \"%s\" slot %lu object %lu: attrib %lu = %d",
	    k11->provider->name, k11->slotidx, obj, type, *val);
	return (0);
}

static int
pkcs11_get_key(struct pkcs11_key *k11, CK_MECHANISM_TYPE mech_type)
{
	struct pkcs11_slotinfo	*si;
	CK_FUNCTION_LIST	*f;
	CK_OBJECT_HANDLE	 obj;
	CK_RV			 rv;
	CK_OBJECT_CLASS		 private_key_class;
	CK_BBOOL		 true_val;
	CK_MECHANISM		 mech;
	CK_ATTRIBUTE		 key_filter[3];
	int			 always_auth = 0;
	int			 did_login = 0;

	if (!k11->provider || !k11->provider->valid ||
	    !k11->provider->module || !k11->provider->module->valid) {
		error("no pkcs11 (valid) provider found");
		return (-1);
	}

	f = k11->provider->module->function_list;
	si = &k11->provider->module->slotinfo[k11->slotidx];

	if ((si->token.flags & CKF_LOGIN_REQUIRED) && !si->logged_in) {
		if (pkcs11_login(k11, CKU_USER) < 0) {
			error("login failed");
			return (-1);
		}
		did_login = 1;
	}

	memset(&key_filter, 0, sizeof(key_filter));
	private_key_class = CKO_PRIVATE_KEY;
	key_filter[0].type = CKA_CLASS;
	key_filter[0].pValue = &private_key_class;
	key_filter[0].ulValueLen = sizeof(private_key_class);

	key_filter[1].type = CKA_ID;
	key_filter[1].pValue = k11->keyid;
	key_filter[1].ulValueLen = k11->keyid_len;

	true_val = CK_TRUE;
	key_filter[2].type = CKA_SIGN;
	key_filter[2].pValue = &true_val;
	key_filter[2].ulValueLen = sizeof(true_val);

	/* try to find object w/CKA_SIGN first, retry w/o */
	if (pkcs11_find(k11->provider, k11->slotidx, key_filter, 3, &obj) < 0 &&
	    pkcs11_find(k11->provider, k11->slotidx, key_filter, 2, &obj) < 0) {
		error("cannot find private key");
		return (-1);
	}

	memset(&mech, 0, sizeof(mech));
	mech.mechanism = mech_type;
	mech.pParameter = NULL_PTR;
	mech.ulParameterLen = 0;

	if ((rv = f->C_SignInit(si->session, &mech, obj)) != CKR_OK) {
		error("C_SignInit failed: %lu", rv);
		return (-1);
	}

	pkcs11_check_obj_bool_attrib(k11, obj, CKA_ALWAYS_AUTHENTICATE,
	    &always_auth); /* ignore errors here */
	if (always_auth && !did_login) {
		debug_f("always-auth key");
		if (pkcs11_login(k11, CKU_CONTEXT_SPECIFIC) < 0) {
			error("login failed for always-auth key");
			return (-1);
		}
	}

	return (0);
}

/* openssl callback doing the actual signing operation */
static int
pkcs11_rsa_private_encrypt(int flen, const u_char *from, u_char *to, RSA *rsa,
    int padding)
{
	struct pkcs11_key	*k11;
	struct pkcs11_slotinfo	*si;
	CK_FUNCTION_LIST	*f;
	CK_ULONG		tlen = 0;
	CK_RV			rv;
	int			rval = -1;

	if ((k11 = RSA_get_ex_data(rsa, rsa_idx)) == NULL) {
		error("RSA_get_ex_data failed");
		return (-1);
	}

	if (pkcs11_get_key(k11, CKM_RSA_PKCS) == -1) {
		error("pkcs11_get_key failed");
		return (-1);
	}

	f = k11->provider->module->function_list;
	si = &k11->provider->module->slotinfo[k11->slotidx];
	tlen = RSA_size(rsa);

	/* XXX handle CKR_BUFFER_TOO_SMALL */
	rv = f->C_Sign(si->session, (CK_BYTE *)from, flen, to, &tlen);
	if (rv == CKR_OK)
		rval = tlen;
	else
		error("C_Sign failed: %lu", rv);

	return (rval);
}

static int
pkcs11_rsa_private_decrypt(int flen, const u_char *from, u_char *to, RSA *rsa,
    int padding)
{
	return (-1);
}

static int
pkcs11_rsa_start_wrapper(void)
{
	if (rsa_method != NULL)
		return (0);
	rsa_method = RSA_meth_dup(RSA_get_default_method());
	if (rsa_method == NULL)
		return (-1);
	rsa_idx = RSA_get_ex_new_index(0, "ssh-pkcs11-rsa",
	    NULL, NULL, pkcs11_k11_free);
	if (rsa_idx == -1)
		return (-1);
	if (!RSA_meth_set1_name(rsa_method, "pkcs11") ||
	    !RSA_meth_set_priv_enc(rsa_method, pkcs11_rsa_private_encrypt) ||
	    !RSA_meth_set_priv_dec(rsa_method, pkcs11_rsa_private_decrypt)) {
		error_f("setup pkcs11 method failed");
		return (-1);
	}
	return (0);
}

/* redirect private key operations for rsa key to pkcs11 token */
static int
pkcs11_rsa_wrap(struct pkcs11_provider *provider, CK_ULONG slotidx,
    CK_ATTRIBUTE *keyid_attrib, CK_ATTRIBUTE *label_attrib, RSA *rsa)
{
	struct pkcs11_key	*k11;

	if (pkcs11_rsa_start_wrapper() == -1)
		return (-1);

	k11 = xcalloc(1, sizeof(*k11));
	k11->provider = provider;
	provider->refcount++;	/* provider referenced by RSA key */
	k11->slotidx = slotidx;
	/* identify key object on smartcard */
	k11->keyid_len = keyid_attrib->ulValueLen;
	if (k11->keyid_len > 0) {
		k11->keyid = xmalloc(k11->keyid_len);
		memcpy(k11->keyid, keyid_attrib->pValue, k11->keyid_len);
	}

	if (label_attrib->ulValueLen > 0 ) {
		k11->label = xmalloc(label_attrib->ulValueLen+1);
		memcpy(k11->label, label_attrib->pValue, label_attrib->ulValueLen);
		k11->label[label_attrib->ulValueLen] = 0;
	}

	if (RSA_set_method(rsa, rsa_method) != 1)
		fatal_f("RSA_set_method failed");
	if (RSA_set_ex_data(rsa, rsa_idx, k11) != 1)
		fatal_f("RSA_set_ex_data failed");
	return (0);
}

#if defined(OPENSSL_HAS_ECC) && defined(HAVE_EC_KEY_METHOD_NEW)
/* openssl callback doing the actual signing operation */
static ECDSA_SIG *
ecdsa_do_sign(const unsigned char *dgst, int dgst_len, const BIGNUM *inv,
    const BIGNUM *rp, EC_KEY *ec)
{
	struct pkcs11_key	*k11;
	struct pkcs11_slotinfo	*si;
	CK_FUNCTION_LIST	*f;
	CK_ULONG		siglen = 0, bnlen;
	CK_RV			rv;
	ECDSA_SIG		*ret = NULL;
	u_char			*sig;
	BIGNUM			*r = NULL, *s = NULL;

	if ((k11 = EC_KEY_get_ex_data(ec, ec_key_idx)) == NULL) {
		ossl_error("EC_KEY_get_ex_data failed for ec");
		return (NULL);
	}

	if (pkcs11_get_key(k11, CKM_ECDSA) == -1) {
		error("pkcs11_get_key failed");
		return (NULL);
	}

	f = k11->provider->module->function_list;
	si = &k11->provider->module->slotinfo[k11->slotidx];

	siglen = ECDSA_size(ec);
	sig = xmalloc(siglen);

	/* XXX handle CKR_BUFFER_TOO_SMALL */
	rv = f->C_Sign(si->session, (CK_BYTE *)dgst, dgst_len, sig, &siglen);
	if (rv != CKR_OK) {
		error("C_Sign failed: %lu", rv);
		goto done;
	}
	if (siglen < 64 || siglen > 132 || siglen % 2) {
		error_f("bad signature length: %lu", (u_long)siglen);
		goto done;
	}
	bnlen = siglen/2;
	if ((ret = ECDSA_SIG_new()) == NULL) {
		error("ECDSA_SIG_new failed");
		goto done;
	}
	if ((r = BN_bin2bn(sig, bnlen, NULL)) == NULL ||
	    (s = BN_bin2bn(sig+bnlen, bnlen, NULL)) == NULL) {
		ossl_error("BN_bin2bn failed");
		ECDSA_SIG_free(ret);
		ret = NULL;
		goto done;
	}
	if (!ECDSA_SIG_set0(ret, r, s)) {
		error_f("ECDSA_SIG_set0 failed");
		ECDSA_SIG_free(ret);
		ret = NULL;
		goto done;
	}
	r = s = NULL; /* now owned by ret */
	/* success */
 done:
	BN_free(r);
	BN_free(s);
	free(sig);

	return (ret);
}

static int
pkcs11_ecdsa_start_wrapper(void)
{
	int (*orig_sign)(int, const unsigned char *, int, unsigned char *,
	    unsigned int *, const BIGNUM *, const BIGNUM *, EC_KEY *) = NULL;

	if (ec_key_method != NULL)
		return (0);
	ec_key_idx = EC_KEY_get_ex_new_index(0, "ssh-pkcs11-ecdsa",
	    NULL, NULL, pkcs11_k11_free);
	if (ec_key_idx == -1)
		return (-1);
	ec_key_method = EC_KEY_METHOD_new(EC_KEY_OpenSSL());
	if (ec_key_method == NULL)
		return (-1);
	EC_KEY_METHOD_get_sign(ec_key_method, &orig_sign, NULL, NULL);
	EC_KEY_METHOD_set_sign(ec_key_method, orig_sign, NULL, ecdsa_do_sign);
	return (0);
}

static int
pkcs11_ecdsa_wrap(struct pkcs11_provider *provider, CK_ULONG slotidx,
    CK_ATTRIBUTE *keyid_attrib, CK_ATTRIBUTE *label_attrib, EC_KEY *ec)
{
	struct pkcs11_key	*k11;

	if (pkcs11_ecdsa_start_wrapper() == -1)
		return (-1);

	k11 = xcalloc(1, sizeof(*k11));
	k11->provider = provider;
	provider->refcount++;	/* provider referenced by ECDSA key */
	k11->slotidx = slotidx;
	/* identify key object on smartcard */
	k11->keyid_len = keyid_attrib->ulValueLen;
	if (k11->keyid_len > 0) {
		k11->keyid = xmalloc(k11->keyid_len);
		memcpy(k11->keyid, keyid_attrib->pValue, k11->keyid_len);
	}
	if (label_attrib->ulValueLen > 0 ) {
		k11->label = xmalloc(label_attrib->ulValueLen+1);
		memcpy(k11->label, label_attrib->pValue, label_attrib->ulValueLen);
		k11->label[label_attrib->ulValueLen] = 0;
	}

	if (EC_KEY_set_method(ec, ec_key_method) != 1)
		fatal_f("EC_KEY_set_method failed");
	if (EC_KEY_set_ex_data(ec, ec_key_idx, k11) != 1)
		fatal_f("EC_KEY_set_ex_data failed");

	return (0);
}
#endif /* OPENSSL_HAS_ECC && HAVE_EC_KEY_METHOD_NEW */

/* remove trailing spaces. Note, that this does NOT guarantee the buffer
 * will be null terminated if there are no trailing spaces! */
static char *
rmspace(u_char *buf, size_t len)
{
	size_t i;

	if (len == 0)
		return buf;
	for (i = len - 1; i > 0; i--)
		if (buf[i] == ' ')
			buf[i] = '\0';
		else
			break;
	return buf;
}
/* Used to printf fixed-width, space-padded, unterminated strings using %.*s */
#define RMSPACE(s) (int)sizeof(s), rmspace(s, sizeof(s))

/*
 * open a pkcs11 session and login if required.
 * if pin == NULL we delay login until key use
 */
static int
pkcs11_open_session(struct pkcs11_provider *p, CK_ULONG slotidx, char *pin,
    CK_ULONG user)
{
	struct pkcs11_slotinfo	*si;
	CK_FUNCTION_LIST	*f;
	CK_RV			rv;
	CK_SESSION_HANDLE	session;
	int			login_required, ret;

	f = p->module->function_list;
	si = &p->module->slotinfo[slotidx];

	login_required = si->token.flags & CKF_LOGIN_REQUIRED;

	/* fail early before opening session */
	if (login_required && !pkcs11_interactive &&
	    (pin == NULL || strlen(pin) == 0)) {
		error("pin required");
		return (-SSH_PKCS11_ERR_PIN_REQUIRED);
	}
	if ((rv = f->C_OpenSession(p->module->slotlist[slotidx], CKF_RW_SESSION|
	    CKF_SERIAL_SESSION, NULL, NULL, &session)) != CKR_OK) {
		error("C_OpenSession failed for slot %lu: %lu", slotidx, rv);
		return (-1);
	}
	if (login_required && pin != NULL && strlen(pin) != 0) {
		rv = f->C_Login(session, user, (u_char *)pin, strlen(pin));
		if (rv != CKR_OK && rv != CKR_USER_ALREADY_LOGGED_IN) {
			error("C_Login failed: %lu", rv);
			ret = (rv == CKR_PIN_LOCKED) ?
			    -SSH_PKCS11_ERR_PIN_LOCKED :
			    -SSH_PKCS11_ERR_LOGIN_FAIL;
			if ((rv = f->C_CloseSession(session)) != CKR_OK)
				error("C_CloseSession failed: %lu", rv);
			return (ret);
		}
		si->logged_in = 1;
	}
	si->session = session;
	return (0);
}

static int
pkcs11_key_included(struct sshkey ***keysp, int *nkeys, struct sshkey *key)
{
	int i;

	for (i = 0; i < *nkeys; i++)
		if (sshkey_equal(key, (*keysp)[i]))
			return (1);
	return (0);
}

#if defined(OPENSSL_HAS_ECC) && defined(HAVE_EC_KEY_METHOD_NEW)
static struct sshkey *
pkcs11_fetch_ecdsa_pubkey(struct pkcs11_provider *p, CK_ULONG slotidx,
    CK_OBJECT_HANDLE *obj)
{
	CK_ATTRIBUTE		 key_attr[4];
	int			 nattr = 4;
	CK_SESSION_HANDLE	 session;
	CK_FUNCTION_LIST	*f = NULL;
	CK_RV			 rv;
	ASN1_OCTET_STRING	*octet = NULL;
	EC_KEY			*ec = NULL;
	EC_GROUP		*group = NULL;
	struct sshkey		*key = NULL;
	const unsigned char	*attrp = NULL;
	int			 i;
	int			 nid;

	memset(&key_attr, 0, sizeof(key_attr));
	key_attr[0].type = CKA_ID;
	key_attr[1].type = CKA_LABEL;
	key_attr[2].type = CKA_EC_POINT;
	key_attr[3].type = CKA_EC_PARAMS;

	session = p->module->slotinfo[slotidx].session;
	f = p->module->function_list;

	/* figure out size of the attributes */
	rv = f->C_GetAttributeValue(session, *obj, key_attr, nattr);
	if (rv != CKR_OK) {
		error("C_GetAttributeValue failed: %lu", rv);
		return (NULL);
	}

	/*
	 * Allow CKA_ID (always first attribute) to be empty, but
	 * ensure that none of the others are zero length.
	 * XXX assumes CKA_ID is always first.
	 */
	if (key_attr[2].ulValueLen == 0 ||
	    key_attr[3].ulValueLen == 0) {
		error("invalid attribute length");
		return (NULL);
	}

	/* allocate buffers for attributes */
	for (i = 0; i < nattr; i++)
		if (key_attr[i].ulValueLen > 0)
			key_attr[i].pValue = xcalloc(1, key_attr[i].ulValueLen);

	/* retrieve ID, public point and curve parameters of EC key */
	rv = f->C_GetAttributeValue(session, *obj, key_attr, nattr);
	if (rv != CKR_OK) {
		error("C_GetAttributeValue failed: %lu", rv);
		goto fail;
	}

	ec = EC_KEY_new();
	if (ec == NULL) {
		error("EC_KEY_new failed");
		goto fail;
	}

	attrp = key_attr[3].pValue;
	group = d2i_ECPKParameters(NULL, &attrp, key_attr[3].ulValueLen);
	if (group == NULL) {
		ossl_error("d2i_ECPKParameters failed");
		goto fail;
	}

	if (EC_KEY_set_group(ec, group) == 0) {
		ossl_error("EC_KEY_set_group failed");
		goto fail;
	}

	if (key_attr[2].ulValueLen <= 2) {
		error("CKA_EC_POINT too small");
		goto fail;
	}

	attrp = key_attr[2].pValue;
	octet = d2i_ASN1_OCTET_STRING(NULL, &attrp, key_attr[2].ulValueLen);
	if (octet == NULL) {
		ossl_error("d2i_ASN1_OCTET_STRING failed");
		goto fail;
	}
	attrp = octet->data;
	if (o2i_ECPublicKey(&ec, &attrp, octet->length) == NULL) {
		ossl_error("o2i_ECPublicKey failed");
		goto fail;
	}

	nid = sshkey_ecdsa_key_to_nid(ec);
	if (nid < 0) {
		error("couldn't get curve nid");
		goto fail;
	}

	if (pkcs11_ecdsa_wrap(p, slotidx, &key_attr[0], &key_attr[1], ec))
		goto fail;

	key = sshkey_new(KEY_UNSPEC);
	if (key == NULL) {
		error("sshkey_new failed");
		goto fail;
	}

	EVP_PKEY_free(key->pkey);
	if ((key->pkey = EVP_PKEY_new()) == NULL)
		fatal("EVP_PKEY_new failed");
	if (EVP_PKEY_set1_EC_KEY(key->pkey, ec) != 1)
		fatal("EVP_PKEY_set1_EC_KEY failed");
	key->ecdsa_nid = nid;
	key->type = KEY_ECDSA;
	key->flags |= SSHKEY_FLAG_EXT;

fail:
	for (i = 0; i < nattr; i++)
		free(key_attr[i].pValue);
	if (ec)
		EC_KEY_free(ec);
	if (group)
		EC_GROUP_free(group);
	if (octet)
		ASN1_OCTET_STRING_free(octet);

	return (key);
}
#endif /* OPENSSL_HAS_ECC && HAVE_EC_KEY_METHOD_NEW */

static struct sshkey *
pkcs11_fetch_rsa_pubkey(struct pkcs11_provider *p, CK_ULONG slotidx,
    CK_OBJECT_HANDLE *obj)
{
	CK_ATTRIBUTE		 key_attr[4];
	int			 nattr = 4;
	CK_SESSION_HANDLE	 session;
	CK_FUNCTION_LIST	*f = NULL;
	CK_RV			 rv;
	RSA			*rsa = NULL;
	BIGNUM			*rsa_n, *rsa_e;
	struct sshkey		*key = NULL;
	int			 i;

	memset(&key_attr, 0, sizeof(key_attr));
	key_attr[0].type = CKA_ID;
	key_attr[1].type = CKA_LABEL;
	key_attr[2].type = CKA_MODULUS;
	key_attr[3].type = CKA_PUBLIC_EXPONENT;

	session = p->module->slotinfo[slotidx].session;
	f = p->module->function_list;

	/* figure out size of the attributes */
	rv = f->C_GetAttributeValue(session, *obj, key_attr, nattr);
	if (rv != CKR_OK) {
		error("C_GetAttributeValue failed: %lu", rv);
		return (NULL);
	}

	/*
	 * Allow CKA_ID (always first attribute) to be empty, but
	 * ensure that none of the others are zero length.
	 * XXX assumes CKA_ID is always first.
	 */
	if (key_attr[2].ulValueLen == 0 ||
	    key_attr[3].ulValueLen == 0) {
		error("invalid attribute length");
		return (NULL);
	}

	/* allocate buffers for attributes */
	for (i = 0; i < nattr; i++)
		if (key_attr[i].ulValueLen > 0)
			key_attr[i].pValue = xcalloc(1, key_attr[i].ulValueLen);

	/* retrieve ID, modulus and public exponent of RSA key */
	rv = f->C_GetAttributeValue(session, *obj, key_attr, nattr);
	if (rv != CKR_OK) {
		error("C_GetAttributeValue failed: %lu", rv);
		goto fail;
	}

	rsa = RSA_new();
	if (rsa == NULL) {
		error("RSA_new failed");
		goto fail;
	}

	rsa_n = BN_bin2bn(key_attr[2].pValue, key_attr[2].ulValueLen, NULL);
	rsa_e = BN_bin2bn(key_attr[3].pValue, key_attr[3].ulValueLen, NULL);
	if (rsa_n == NULL || rsa_e == NULL) {
		error("BN_bin2bn failed");
		goto fail;
	}
	if (!RSA_set0_key(rsa, rsa_n, rsa_e, NULL))
		fatal_f("set key");
	rsa_n = rsa_e = NULL; /* transferred */

	if (pkcs11_rsa_wrap(p, slotidx, &key_attr[0], &key_attr[1], rsa))
		goto fail;

	key = sshkey_new(KEY_UNSPEC);
	if (key == NULL) {
		error("sshkey_new failed");
		goto fail;
	}

	EVP_PKEY_free(key->pkey);
	if ((key->pkey = EVP_PKEY_new()) == NULL)
		fatal("EVP_PKEY_new failed");
	if (EVP_PKEY_set1_RSA(key->pkey, rsa) != 1)
		fatal("EVP_PKEY_set1_RSA failed");
	key->type = KEY_RSA;
	key->flags |= SSHKEY_FLAG_EXT;

fail:
	for (i = 0; i < nattr; i++)
		free(key_attr[i].pValue);
	RSA_free(rsa);

	return (key);
}

static int
pkcs11_fetch_x509_pubkey(struct pkcs11_provider *p, CK_ULONG slotidx,
    CK_OBJECT_HANDLE *obj, struct sshkey **keyp, char **labelp)
{
	CK_ATTRIBUTE		 cert_attr[4];
	int			 nattr = 4;
	CK_SESSION_HANDLE	 session;
	CK_FUNCTION_LIST	*f = NULL;
	CK_RV			 rv;
	X509			*x509 = NULL;
	X509_NAME		*x509_name = NULL;
	EVP_PKEY		*evp;
	RSA			*rsa = NULL;
#ifdef OPENSSL_HAS_ECC
	EC_KEY			*ec = NULL;
#endif
	struct sshkey		*key = NULL;
	int			 i;
#if defined(OPENSSL_HAS_ECC) && defined(HAVE_EC_KEY_METHOD_NEW)
	int			 nid;
#endif
	const u_char		*cp;
	char			*subject = NULL;

	*keyp = NULL;
	*labelp = NULL;

	memset(&cert_attr, 0, sizeof(cert_attr));
	cert_attr[0].type = CKA_ID;
	cert_attr[1].type = CKA_LABEL;
	cert_attr[2].type = CKA_SUBJECT;
	cert_attr[3].type = CKA_VALUE;

	session = p->module->slotinfo[slotidx].session;
	f = p->module->function_list;

	/* figure out size of the attributes */
	rv = f->C_GetAttributeValue(session, *obj, cert_attr, nattr);
	if (rv != CKR_OK) {
		error("C_GetAttributeValue failed: %lu", rv);
		return -1;
	}

	/*
	 * Allow CKA_ID (always first attribute) to be empty, but
	 * ensure that none of the others are zero length.
	 * XXX assumes CKA_ID is always first.
	 */
	if (cert_attr[1].ulValueLen == 0 ||
	    cert_attr[2].ulValueLen == 0 ||
	    cert_attr[3].ulValueLen == 0) {
		error("invalid attribute length");
		return -1;
	}

	/* allocate buffers for attributes */
	for (i = 0; i < nattr; i++)
		if (cert_attr[i].ulValueLen > 0)
			cert_attr[i].pValue = xcalloc(1, cert_attr[i].ulValueLen);

	/* retrieve ID, subject and value of certificate */
	rv = f->C_GetAttributeValue(session, *obj, cert_attr, nattr);
	if (rv != CKR_OK) {
		error("C_GetAttributeValue failed: %lu", rv);
		goto out;
	}

	/* Decode DER-encoded cert subject */
	cp = cert_attr[1].pValue;
	if ((x509_name = d2i_X509_NAME(NULL, &cp,
	    cert_attr[1].ulValueLen)) == NULL ||
	    (subject = X509_NAME_oneline(x509_name, NULL, 0)) == NULL)
		subject = xstrdup("invalid subject");
	X509_NAME_free(x509_name);

	cp = cert_attr[3].pValue;
	if ((x509 = d2i_X509(NULL, &cp, cert_attr[3].ulValueLen)) == NULL) {
		error("d2i_x509 failed");
		goto out;
	}

	if ((evp = X509_get_pubkey(x509)) == NULL) {
		error("X509_get_pubkey failed");
		goto out;
	}

	if (EVP_PKEY_base_id(evp) == EVP_PKEY_RSA) {
		if (EVP_PKEY_get0_RSA(evp) == NULL) {
			error("invalid x509; no rsa key");
			goto out;
		}
		if ((rsa = RSAPublicKey_dup(EVP_PKEY_get0_RSA(evp))) == NULL) {
			error("RSAPublicKey_dup failed");
			goto out;
		}

		if (pkcs11_rsa_wrap(p, slotidx, &cert_attr[0], &cert_attr[1], rsa))
			goto out;

		key = sshkey_new(KEY_UNSPEC);
		if (key == NULL) {
			error("sshkey_new failed");
			goto out;
		}

		EVP_PKEY_free(key->pkey);
		if ((key->pkey = EVP_PKEY_new()) == NULL)
			fatal("EVP_PKEY_new failed");
		if (EVP_PKEY_set1_RSA(key->pkey, rsa) != 1)
			fatal("EVP_PKEY_set1_RSA failed");
		key->type = KEY_RSA;
		key->flags |= SSHKEY_FLAG_EXT;
#if defined(OPENSSL_HAS_ECC) && defined(HAVE_EC_KEY_METHOD_NEW)
	} else if (EVP_PKEY_base_id(evp) == EVP_PKEY_EC) {
		if (EVP_PKEY_get0_EC_KEY(evp) == NULL) {
			error("invalid x509; no ec key");
			goto out;
		}
		if ((ec = EC_KEY_dup(EVP_PKEY_get0_EC_KEY(evp))) == NULL) {
			error("EC_KEY_dup failed");
			goto out;
		}

		nid = sshkey_ecdsa_key_to_nid(ec);
		if (nid < 0) {
			error("couldn't get curve nid");
			goto out;
		}

		if (pkcs11_ecdsa_wrap(p, slotidx, &cert_attr[0], &cert_attr[1], ec))
			goto out;

		key = sshkey_new(KEY_UNSPEC);
		if (key == NULL) {
			error("sshkey_new failed");
			goto out;
		}

		EVP_PKEY_free(key->pkey);
		if ((key->pkey = EVP_PKEY_new()) == NULL)
			fatal("EVP_PKEY_new failed");
		if (EVP_PKEY_set1_EC_KEY(key->pkey, ec) != 1)
			fatal("EVP_PKEY_set1_EC_KEY failed");
		key->ecdsa_nid = nid;
		key->type = KEY_ECDSA;
		key->flags |= SSHKEY_FLAG_EXT;
#endif /* OPENSSL_HAS_ECC && HAVE_EC_KEY_METHOD_NEW */
	} else {
		error("unknown certificate key type");
		goto out;
	}
 out:
	for (i = 0; i < nattr; i++)
		free(cert_attr[i].pValue);
	X509_free(x509);
	RSA_free(rsa);
#ifdef OPENSSL_HAS_ECC
	EC_KEY_free(ec);
#endif
	if (key == NULL) {
		free(subject);
		return -1;
	}
	/* success */
	*keyp = key;
	*labelp = subject;
	return 0;
}

#if 0
static int
have_rsa_key(const RSA *rsa)
{
	const BIGNUM *rsa_n, *rsa_e;

	RSA_get0_key(rsa, &rsa_n, &rsa_e, NULL);
	return rsa_n != NULL && rsa_e != NULL;
}
#endif

static void
note_key(struct pkcs11_provider *p, CK_ULONG slotidx, const char *context,
    struct sshkey *key)
{
	char *fp;

	if ((fp = sshkey_fingerprint(key, SSH_FP_HASH_DEFAULT,
	    SSH_FP_DEFAULT)) == NULL) {
		error_f("sshkey_fingerprint failed");
		return;
	}
	debug2("%s: provider %s slot %lu: %s %s", context, p->name,
	    (u_long)slotidx, sshkey_type(key), fp);
	free(fp);
}

/*
 * lookup certificates for token in slot identified by slotidx,
 * add 'wrapped' public keys to the 'keysp' array and increment nkeys.
 * keysp points to an (possibly empty) array with *nkeys keys.
 */
static int
pkcs11_fetch_certs(struct pkcs11_provider *p, CK_ULONG slotidx,
    struct sshkey ***keysp, char ***labelsp, int *nkeys, struct pkcs11_uri *uri)
{
	struct sshkey		*key = NULL;
	CK_OBJECT_CLASS		 key_class;
	CK_ATTRIBUTE		 key_attr[3];
	int			 nattr = 1;
	CK_SESSION_HANDLE	 session;
	CK_FUNCTION_LIST	*f = NULL;
	CK_RV			 rv;
	CK_OBJECT_HANDLE	 obj;
	CK_ULONG		 n = 0;
	int			 ret = -1;
	char			*label;

	memset(&key_attr, 0, sizeof(key_attr));
	memset(&obj, 0, sizeof(obj));

	key_class = CKO_CERTIFICATE;
	key_attr[0].type = CKA_CLASS;
	key_attr[0].pValue = &key_class;
	key_attr[0].ulValueLen = sizeof(key_class);

	if (uri->id != NULL) {
		key_attr[nattr].type = CKA_ID;
		key_attr[nattr].pValue = uri->id;
		key_attr[nattr].ulValueLen = uri->id_len;
		nattr++;
	}
	if (uri->object != NULL) {
		key_attr[nattr].type = CKA_LABEL;
		key_attr[nattr].pValue = uri->object;
		key_attr[nattr].ulValueLen = strlen(uri->object);
		nattr++;
	}

	session = p->module->slotinfo[slotidx].session;
	f = p->module->function_list;

	rv = f->C_FindObjectsInit(session, key_attr, nattr);
	if (rv != CKR_OK) {
		error("C_FindObjectsInit failed: %lu", rv);
		goto fail;
	}

	while (1) {
		CK_CERTIFICATE_TYPE	ck_cert_type;

		rv = f->C_FindObjects(session, &obj, 1, &n);
		if (rv != CKR_OK) {
			error("C_FindObjects failed: %lu", rv);
			goto fail;
		}
		if (n == 0)
			break;

		memset(&ck_cert_type, 0, sizeof(ck_cert_type));
		memset(&key_attr, 0, sizeof(key_attr));
		key_attr[0].type = CKA_CERTIFICATE_TYPE;
		key_attr[0].pValue = &ck_cert_type;
		key_attr[0].ulValueLen = sizeof(ck_cert_type);

		rv = f->C_GetAttributeValue(session, obj, key_attr, 1);
		if (rv != CKR_OK) {
			error("C_GetAttributeValue failed: %lu", rv);
			goto fail;
		}

		key = NULL;
		label = NULL;
		switch (ck_cert_type) {
		case CKC_X_509:
			if (pkcs11_fetch_x509_pubkey(p, slotidx, &obj,
			    &key, &label) != 0) {
				error("failed to fetch key");
				continue;
			}
			break;
		default:
			error("skipping unsupported certificate type %lu",
			    ck_cert_type);
			continue;
		}
		note_key(p, slotidx, __func__, key);
		if (pkcs11_key_included(keysp, nkeys, key)) {
			debug2_f("key already included");;
			sshkey_free(key);
		} else {
			/* expand key array and add key */
			*keysp = xrecallocarray(*keysp, *nkeys,
			    *nkeys + 1, sizeof(struct sshkey *));
			(*keysp)[*nkeys] = key;
			if (labelsp != NULL) {
				*labelsp = xrecallocarray(*labelsp, *nkeys,
				    *nkeys + 1, sizeof(char *));
				(*labelsp)[*nkeys] = xstrdup((char *)label);
			}
			*nkeys = *nkeys + 1;
			debug("have %d keys", *nkeys);
		}
	}

	ret = 0;
fail:
	rv = f->C_FindObjectsFinal(session);
	if (rv != CKR_OK) {
		error("C_FindObjectsFinal failed: %lu", rv);
		ret = -1;
	}

	return (ret);
}

/*
 * lookup public keys for token in slot identified by slotidx,
 * add 'wrapped' public keys to the 'keysp' array and increment nkeys.
 * keysp points to an (possibly empty) array with *nkeys keys.
 */
static int
pkcs11_fetch_keys(struct pkcs11_provider *p, CK_ULONG slotidx,
    struct sshkey ***keysp, char ***labelsp, int *nkeys, struct pkcs11_uri *uri)
{
	struct sshkey		*key = NULL;
	CK_OBJECT_CLASS		 key_class;
	CK_ATTRIBUTE		 key_attr[3];
	int			 nattr = 1;
	CK_SESSION_HANDLE	 session;
	CK_FUNCTION_LIST	*f = NULL;
	CK_RV			 rv;
	CK_OBJECT_HANDLE	 obj;
	CK_ULONG		 n = 0;
	int			 ret = -1;

	memset(&key_attr, 0, sizeof(key_attr));
	memset(&obj, 0, sizeof(obj));

	key_class = CKO_PUBLIC_KEY;
	key_attr[0].type = CKA_CLASS;
	key_attr[0].pValue = &key_class;
	key_attr[0].ulValueLen = sizeof(key_class);

	if (uri->id != NULL) {
		key_attr[nattr].type = CKA_ID;
		key_attr[nattr].pValue = uri->id;
		key_attr[nattr].ulValueLen = uri->id_len;
		nattr++;
	}
	if (uri->object != NULL) {
		key_attr[nattr].type = CKA_LABEL;
		key_attr[nattr].pValue = uri->object;
		key_attr[nattr].ulValueLen = strlen(uri->object);
		nattr++;
	}

	session = p->module->slotinfo[slotidx].session;
	f = p->module->function_list;

	rv = f->C_FindObjectsInit(session, key_attr, nattr);
	if (rv != CKR_OK) {
		error("C_FindObjectsInit failed: %lu", rv);
		goto fail;
	}

	while (1) {
		CK_KEY_TYPE	ck_key_type;
		CK_UTF8CHAR	label[256];

		rv = f->C_FindObjects(session, &obj, 1, &n);
		if (rv != CKR_OK) {
			error("C_FindObjects failed: %lu", rv);
			goto fail;
		}
		if (n == 0)
			break;

		memset(&ck_key_type, 0, sizeof(ck_key_type));
		memset(&key_attr, 0, sizeof(key_attr));
		key_attr[0].type = CKA_KEY_TYPE;
		key_attr[0].pValue = &ck_key_type;
		key_attr[0].ulValueLen = sizeof(ck_key_type);
		key_attr[1].type = CKA_LABEL;
		key_attr[1].pValue = &label;
		key_attr[1].ulValueLen = sizeof(label) - 1;

		rv = f->C_GetAttributeValue(session, obj, key_attr, 2);
		if (rv != CKR_OK) {
			error("C_GetAttributeValue failed: %lu", rv);
			goto fail;
		}

		label[key_attr[1].ulValueLen] = '\0';

		switch (ck_key_type) {
		case CKK_RSA:
			key = pkcs11_fetch_rsa_pubkey(p, slotidx, &obj);
			break;
#if defined(OPENSSL_HAS_ECC) && defined(HAVE_EC_KEY_METHOD_NEW)
		case CKK_ECDSA:
			key = pkcs11_fetch_ecdsa_pubkey(p, slotidx, &obj);
			break;
#endif /* OPENSSL_HAS_ECC && HAVE_EC_KEY_METHOD_NEW */
		default:
			/* XXX print key type? */
			key = NULL;
			error("skipping unsupported key type");
		}

		if (key == NULL) {
			error("failed to fetch key");
			continue;
		}
		note_key(p, slotidx, __func__, key);
		if (pkcs11_key_included(keysp, nkeys, key)) {
			debug2_f("key already included");;
			sshkey_free(key);
		} else {
			/* expand key array and add key */
			*keysp = xrecallocarray(*keysp, *nkeys,
			    *nkeys + 1, sizeof(struct sshkey *));
			(*keysp)[*nkeys] = key;
			if (labelsp != NULL) {
				*labelsp = xrecallocarray(*labelsp, *nkeys,
				    *nkeys + 1, sizeof(char *));
				(*labelsp)[*nkeys] = xstrdup((char *)label);
			}
			*nkeys = *nkeys + 1;
			debug("have %d keys", *nkeys);
		}
	}

	ret = 0;
fail:
	rv = f->C_FindObjectsFinal(session);
	if (rv != CKR_OK) {
		error("C_FindObjectsFinal failed: %lu", rv);
		ret = -1;
	}

	return (ret);
}

#ifdef WITH_PKCS11_KEYGEN
#define FILL_ATTR(attr, idx, typ, val, len) \
	{ (attr[idx]).type=(typ); (attr[idx]).pValue=(val); (attr[idx]).ulValueLen=len; idx++; }

static struct sshkey *
pkcs11_rsa_generate_private_key(struct pkcs11_provider *p, CK_ULONG slotidx,
    char *label, CK_ULONG bits, CK_BYTE keyid, u_int32_t *err)
{
	struct pkcs11_slotinfo	*si;
	char			*plabel = label ? label : "";
	int			 npub = 0, npriv = 0;
	CK_RV			 rv;
	CK_FUNCTION_LIST	*f;
	CK_SESSION_HANDLE	 session;
	CK_BBOOL		 true_val = CK_TRUE, false_val = CK_FALSE;
	CK_OBJECT_HANDLE	 pubKey, privKey;
	CK_ATTRIBUTE		 tpub[16], tpriv[16];
	CK_MECHANISM		 mech = {
	    CKM_RSA_PKCS_KEY_PAIR_GEN, NULL_PTR, 0
	};
	CK_BYTE			 pubExponent[] = {
	    0x01, 0x00, 0x01 /* RSA_F4 in bytes */
	};
	pubkey_filter[0].pValue = &pubkey_class;
	cert_filter[0].pValue = &cert_class;

	*err = 0;

	FILL_ATTR(tpub, npub, CKA_TOKEN, &true_val, sizeof(true_val));
	FILL_ATTR(tpub, npub, CKA_LABEL, plabel, strlen(plabel));
	FILL_ATTR(tpub, npub, CKA_ENCRYPT, &false_val, sizeof(false_val));
	FILL_ATTR(tpub, npub, CKA_VERIFY, &true_val, sizeof(true_val));
	FILL_ATTR(tpub, npub, CKA_VERIFY_RECOVER, &false_val,
	    sizeof(false_val));
	FILL_ATTR(tpub, npub, CKA_WRAP, &false_val, sizeof(false_val));
	FILL_ATTR(tpub, npub, CKA_DERIVE, &false_val, sizeof(false_val));
	FILL_ATTR(tpub, npub, CKA_MODULUS_BITS, &bits, sizeof(bits));
	FILL_ATTR(tpub, npub, CKA_PUBLIC_EXPONENT, pubExponent,
	    sizeof(pubExponent));
	FILL_ATTR(tpub, npub, CKA_ID, &keyid, sizeof(keyid));

	FILL_ATTR(tpriv, npriv, CKA_TOKEN,  &true_val, sizeof(true_val));
	FILL_ATTR(tpriv, npriv, CKA_LABEL,  plabel, strlen(plabel));
	FILL_ATTR(tpriv, npriv, CKA_PRIVATE,  &true_val, sizeof(true_val));
	FILL_ATTR(tpriv, npriv, CKA_SENSITIVE,  &true_val, sizeof(true_val));
	FILL_ATTR(tpriv, npriv, CKA_DECRYPT,  &false_val, sizeof(false_val));
	FILL_ATTR(tpriv, npriv, CKA_SIGN,  &true_val, sizeof(true_val));
	FILL_ATTR(tpriv, npriv, CKA_SIGN_RECOVER,  &false_val,
	    sizeof(false_val));
	FILL_ATTR(tpriv, npriv, CKA_UNWRAP,  &false_val, sizeof(false_val));
	FILL_ATTR(tpriv, npriv, CKA_DERIVE,  &false_val, sizeof(false_val));
	FILL_ATTR(tpriv, npriv, CKA_ID, &keyid, sizeof(keyid));

	f = p->function_list;
	si = &p->slotinfo[slotidx];
	session = si->session;

	if ((rv = f->C_GenerateKeyPair(session, &mech, tpub, npub, tpriv, npriv,
	    &pubKey, &privKey)) != CKR_OK) {
		error_f("key generation failed: error 0x%lx", rv);
		*err = rv;
		return NULL;
	}

	return pkcs11_fetch_rsa_pubkey(p, slotidx, &pubKey);
}

static int
h2i(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	else
		return -1;
}

static int
pkcs11_decode_hex(const char *hex, unsigned char **dest, size_t *rlen)
{
	size_t	i, len;

	if (dest)
		*dest = NULL;
	if (rlen)
		*rlen = 0;

	if ((len = strlen(hex)) % 2)
		return -1;
	len /= 2;

	*dest = xmalloc(len);

	for (i = 0; i < len; i++) {
		int hi, low;

		hi = h2i(hex[2 * i]);
		lo = h2i(hex[(2 * i) + 1]);
		if (hi == -1 || lo == -1)
			return -1;
		(*dest)[i] = (hi << 4) | lo;
	}

	if (rlen)
		*rlen = len;

	return 0;
}

static struct ec_curve_info {
	const char	*name;
	const char	*oid;
	const char	*oid_encoded;
	size_t		 size;
} ec_curve_infos[] = {
	{"prime256v1",	"1.2.840.10045.3.1.7",	"06082A8648CE3D030107", 256},
	{"secp384r1",	"1.3.132.0.34",		"06052B81040022",	384},
	{"secp521r1",	"1.3.132.0.35",		"06052B81040023",	521},
	{NULL,		NULL,			NULL,			0},
};

static struct sshkey *
pkcs11_ecdsa_generate_private_key(struct pkcs11_provider *p, CK_ULONG slotidx,
    char *label, CK_ULONG bits, CK_BYTE keyid, u_int32_t *err)
{
	struct pkcs11_slotinfo	*si;
	char			*plabel = label ? label : "";
	int			 i;
	size_t			 ecparams_size;
	unsigned char		*ecparams = NULL;
	int			 npub = 0, npriv = 0;
	CK_RV			 rv;
	CK_FUNCTION_LIST	*f;
	CK_SESSION_HANDLE	 session;
	CK_BBOOL		 true_val = CK_TRUE, false_val = CK_FALSE;
	CK_OBJECT_HANDLE	 pubKey, privKey;
	CK_MECHANISM		 mech = {
	    CKM_EC_KEY_PAIR_GEN, NULL_PTR, 0
	};
	CK_ATTRIBUTE		 tpub[16], tpriv[16];

	*err = 0;

	for (i = 0; ec_curve_infos[i].name; i++) {
		if (ec_curve_infos[i].size == bits)
			break;
	}
	if (!ec_curve_infos[i].name) {
		error_f("invalid key size %lu", bits);
		return NULL;
	}
	if (pkcs11_decode_hex(ec_curve_infos[i].oid_encoded, &ecparams,
	    &ecparams_size) == -1) {
		error_f("invalid oid");
		return NULL;
	}

	FILL_ATTR(tpub, npub, CKA_TOKEN, &true_val, sizeof(true_val));
	FILL_ATTR(tpub, npub, CKA_LABEL, plabel, strlen(plabel));
	FILL_ATTR(tpub, npub, CKA_ENCRYPT, &false_val, sizeof(false_val));
	FILL_ATTR(tpub, npub, CKA_VERIFY, &true_val, sizeof(true_val));
	FILL_ATTR(tpub, npub, CKA_VERIFY_RECOVER, &false_val,
	    sizeof(false_val));
	FILL_ATTR(tpub, npub, CKA_WRAP, &false_val, sizeof(false_val));
	FILL_ATTR(tpub, npub, CKA_DERIVE, &false_val, sizeof(false_val));
	FILL_ATTR(tpub, npub, CKA_EC_PARAMS, ecparams, ecparams_size);
	FILL_ATTR(tpub, npub, CKA_ID, &keyid, sizeof(keyid));

	FILL_ATTR(tpriv, npriv, CKA_TOKEN, &true_val, sizeof(true_val));
	FILL_ATTR(tpriv, npriv, CKA_LABEL, plabel, strlen(plabel));
	FILL_ATTR(tpriv, npriv, CKA_PRIVATE, &true_val, sizeof(true_val));
	FILL_ATTR(tpriv, npriv, CKA_SENSITIVE, &true_val, sizeof(true_val));
	FILL_ATTR(tpriv, npriv, CKA_DECRYPT, &false_val, sizeof(false_val));
	FILL_ATTR(tpriv, npriv, CKA_SIGN, &true_val, sizeof(true_val));
	FILL_ATTR(tpriv, npriv, CKA_SIGN_RECOVER, &false_val,
	    sizeof(false_val));
	FILL_ATTR(tpriv, npriv, CKA_UNWRAP, &false_val, sizeof(false_val));
	FILL_ATTR(tpriv, npriv, CKA_DERIVE, &false_val, sizeof(false_val));
	FILL_ATTR(tpriv, npriv, CKA_ID, &keyid, sizeof(keyid));

	f = p->function_list;
	si = &p->slotinfo[slotidx];
	session = si->session;

	if ((rv = f->C_GenerateKeyPair(session, &mech, tpub, npub, tpriv, npriv,
	    &pubKey, &privKey)) != CKR_OK) {
		error_f("key generation failed: error 0x%lx", rv);
		*err = rv;
		return NULL;
	}

	return pkcs11_fetch_ecdsa_pubkey(p, slotidx, &pubKey);
}
#endif /* WITH_PKCS11_KEYGEN */

static int
pkcs11_initialize_provider(struct pkcs11_uri *uri, struct pkcs11_provider **providerp)
{
	int need_finalize = 0;
	int ret = -1;
	struct pkcs11_provider *p = NULL;
	void *handle = NULL;
	CK_RV (*getfunctionlist)(CK_FUNCTION_LIST **);
	CK_RV rv;
	CK_FUNCTION_LIST *f = NULL;
	CK_TOKEN_INFO *token;
	CK_ULONG i;
	char *provider_module = NULL;
	struct pkcs11_module *m = NULL;

	/* if no provider specified, fallback to p11-kit */
	if (uri->module_path == NULL) {
#ifdef PKCS11_DEFAULT_PROVIDER
		provider_module = strdup(PKCS11_DEFAULT_PROVIDER);
#else
		error_f("No module path provided");
 		goto fail;
#endif
	} else {
		provider_module = strdup(uri->module_path);
	}
	p = xcalloc(1, sizeof(*p));
	p->name = pkcs11_uri_get(uri);

	if (lib_contains_symbol(provider_module, "C_GetFunctionList") != 0) {
		error("provider %s is not a PKCS11 library", provider_module);
		goto fail;
	}
	if ((m = pkcs11_provider_lookup_module(provider_module)) != NULL
	   && m->valid) {
		debug_f("provider module already initialized: %s", provider_module);
		free(provider_module);
		/* Skip the initialization of PKCS#11 module */
		m->refcount++;
		p->module = m;
		p->valid = 1;
		TAILQ_INSERT_TAIL(&pkcs11_providers, p, next);
		p->refcount++;	/* add to provider list */
		*providerp = p;
		return 0;
	} else {
		m = xcalloc(1, sizeof(*m));
		p->module = m;
		m->refcount++;
	}

	/* open shared pkcs11-library */
	if ((handle = dlopen(provider_module, RTLD_NOW)) == NULL) {
		error("dlopen %s failed: %s", provider_module, dlerror());
		goto fail;
	}
	if ((getfunctionlist = dlsym(handle, "C_GetFunctionList")) == NULL)
		fatal("dlsym(C_GetFunctionList) failed: %s", dlerror());
	p->module->handle = handle;
	/* setup the pkcs11 callbacks */
	if ((rv = (*getfunctionlist)(&f)) != CKR_OK) {
		error("C_GetFunctionList for provider %s failed: %lu",
		    provider_module, rv);
		goto fail;
	}
	m->function_list = f;
	if ((rv = f->C_Initialize(NULL)) != CKR_OK) {
		error("C_Initialize for provider %s failed: %lu",
		    provider_module, rv);
		goto fail;
	}
	need_finalize = 1;
	if ((rv = f->C_GetInfo(&m->info)) != CKR_OK) {
		error("C_GetInfo for provider %s failed: %lu",
		    provider_module, rv);
		goto fail;
	}
	rmspace(m->info.manufacturerID, sizeof(m->info.manufacturerID));
	if (uri->lib_manuf != NULL &&
	    strncmp(uri->lib_manuf, m->info.manufacturerID, 32)) {
		debug_f("Skipping provider %s not matching library_manufacturer",
		    m->info.manufacturerID);
		goto fail;
	}
	rmspace(m->info.libraryDescription, sizeof(m->info.libraryDescription));
	debug("provider %s: manufacturerID <%.32s> cryptokiVersion %d.%d"
	    " libraryDescription <%.32s> libraryVersion %d.%d",
	    provider_module,
	    m->info.manufacturerID,
	    m->info.cryptokiVersion.major,
	    m->info.cryptokiVersion.minor,
	    m->info.libraryDescription,
	    m->info.libraryVersion.major,
	    m->info.libraryVersion.minor);

	if ((rv = f->C_GetSlotList(CK_TRUE, NULL, &m->nslots)) != CKR_OK) {
		error("C_GetSlotList failed: %lu", rv);
		goto fail;
	}
	if (m->nslots == 0) {
		debug_f("provider %s returned no slots", provider_module);
		ret = -SSH_PKCS11_ERR_NO_SLOTS;
		goto fail;
	}
	m->slotlist = xcalloc(m->nslots, sizeof(CK_SLOT_ID));
	if ((rv = f->C_GetSlotList(CK_TRUE, m->slotlist, &m->nslots))
	    != CKR_OK) {
		error("C_GetSlotList for provider %s failed: %lu",
		    provider_module, rv);
		goto fail;
	}
	m->slotinfo = xcalloc(m->nslots, sizeof(struct pkcs11_slotinfo));
	p->valid = 1;
	m->valid = 1;
	for (i = 0; i < m->nslots; i++) {
		token = &m->slotinfo[i].token;
		if ((rv = f->C_GetTokenInfo(m->slotlist[i], token))
		    != CKR_OK) {
			error("C_GetTokenInfo for provider %s slot %lu "
			    "failed: %lu", provider_module, (u_long)i, rv);
			token->flags = 0;
			continue;
		}
		debug("provider %s slot %lu: label <%.*s> "
		    "manufacturerID <%.*s> model <%.*s> serial <%.*s> "
		    "flags 0x%lx",
		    provider_module, (unsigned long)i,
		    RMSPACE(token->label), RMSPACE(token->manufacturerID),
		    RMSPACE(token->model), RMSPACE(token->serialNumber),
		    token->flags);
	}
	m->module_path = provider_module;
	provider_module = NULL;

	/* now owned by caller */
	*providerp = p;

	TAILQ_INSERT_TAIL(&pkcs11_providers, p, next);
	p->refcount++;	/* add to provider list */

	return 0;
fail:
	if (need_finalize && (rv = f->C_Finalize(NULL)) != CKR_OK)
		error("C_Finalize for provider %s failed: %lu",
		    provider_module, rv);
	free(provider_module);
	if (m) {
		free(m->slotlist);
		free(m);
	}
	if (p) {
		free(p->name);
		free(p);
	}
	if (handle)
		dlclose(handle);
	return (ret);
}

/*
 * register a new provider, fails if provider already exists. if
 * keyp is provided, fetch keys.
 */
static int
pkcs11_register_provider_by_uri(struct pkcs11_uri *uri, char *pin,
    struct sshkey ***keyp, char ***labelsp, struct pkcs11_provider **providerp,
    CK_ULONG user)
{
	int nkeys;
	int ret = -1;
	struct pkcs11_provider *p = NULL;
	CK_ULONG i;
	CK_TOKEN_INFO *token;
	char *provider_uri = NULL;

	if (providerp == NULL)
		goto fail;
	*providerp = NULL;

	if (keyp != NULL)
		*keyp = NULL;

	if ((ret = pkcs11_initialize_provider(uri, &p)) != 0) {
		goto fail;
	}

	provider_uri = pkcs11_uri_get(uri);
	if (pin == NULL && uri->pin != NULL) {
		pin = uri->pin;
	}
	nkeys = 0;
	for (i = 0; i < p->module->nslots; i++) {
		token = &p->module->slotinfo[i].token;
		if ((token->flags & CKF_TOKEN_INITIALIZED) == 0) {
			debug2_f("ignoring uninitialised token in "
			    "provider %s slot %lu", provider_uri, (u_long)i);
			continue;
		}
		if (uri->token != NULL &&
		    strncmp(token->label, uri->token, 32) != 0) {
			debug2_f("ignoring token not matching label (%.32s) "
			    "specified by PKCS#11 URI in slot %lu",
			    token->label, (unsigned long)i);
			continue;
		}
		if (uri->manuf != NULL &&
		    strncmp(token->manufacturerID, uri->manuf, 32) != 0) {
			debug2_f("ignoring token not matching requrested "
			    "manufacturerID (%.32s) specified by PKCS#11 URI in "
			    "slot %lu", token->manufacturerID, (unsigned long)i);
			continue;
		}
		if (uri->serial != NULL &&
		    strncmp(token->serialNumber, uri->serial, 16) != 0) {
			debug2_f("ignoring token not matching requrested "
			    "serialNumber (%s) specified by PKCS#11 URI in "
			    "slot %lu", token->serialNumber, (unsigned long)i);
			continue;
		}
		debug("provider %s slot %lu: label <%.32s> manufacturerID <%.32s> "
		    "model <%.16s> serial <%.16s> flags 0x%lx",
		    provider_uri, (unsigned long)i,
		    token->label, token->manufacturerID, token->model,
		    token->serialNumber, token->flags);
		/*
		 * open session if not yet opened, login with pin and
		 * retrieve public keys (if keyp is provided)
		 */
		if ((p->module->slotinfo[i].session != 0 ||
		    (ret = pkcs11_open_session(p, i, pin, user)) != 0) && /* ??? */
		    keyp == NULL)
			continue;
		pkcs11_fetch_keys(p, i, keyp, labelsp, &nkeys, uri);
		pkcs11_fetch_certs(p, i, keyp, labelsp, &nkeys, uri);
		if (nkeys == 0 && !p->module->slotinfo[i].logged_in &&
		    pkcs11_interactive) {
			/*
			 * Some tokens require login before they will
			 * expose keys.
			 */
			debug3_f("Trying to login as there were no keys found");
			if (pkcs11_login_slot(p, &p->module->slotinfo[i],
			    CKU_USER) < 0) {
				error("login failed");
				continue;
			}
			pkcs11_fetch_keys(p, i, keyp, labelsp, &nkeys, uri);
			pkcs11_fetch_certs(p, i, keyp, labelsp, &nkeys, uri);
		}
		if (nkeys == 0 && uri->object != NULL) {
			debug3_f("No keys found. Retrying without label (%.32s) ",
			    uri->object);
			/* Try once more without the label filter */
			char *label = uri->object;
			uri->object = NULL; /* XXX clone uri? */
			pkcs11_fetch_keys(p, i, keyp, labelsp, &nkeys, uri);
			pkcs11_fetch_certs(p, i, keyp, labelsp, &nkeys, uri);
			uri->object = label;
		}
	}
	pin = NULL; /* Will be cleaned up with URI */

	/* now owned by caller */
	*providerp = p;

	free(provider_uri);
	return (nkeys);
fail:
	if (p) {
		TAILQ_REMOVE(&pkcs11_providers, p, next);
	      	pkcs11_provider_unref(p);
	}
	if (ret > 0)
		ret = -1;
	return (ret);
}

static int
pkcs11_register_provider(char *provider_id, char *pin, struct sshkey ***keyp,
    char ***labelsp, struct pkcs11_provider **providerp, CK_ULONG user)
{
	struct pkcs11_uri *uri = NULL;
	int r;

	debug_f("called, provider_id = %s", provider_id);

	uri = pkcs11_uri_init();
	if (uri == NULL)
		fatal("failed to init PKCS#11 URI");

	if (strlen(provider_id) >= strlen(PKCS11_URI_SCHEME) &&
	    strncmp(provider_id, PKCS11_URI_SCHEME, strlen(PKCS11_URI_SCHEME)) == 0) {
		if (pkcs11_uri_parse(provider_id, uri) != 0)
			fatal("Failed to parse PKCS#11 URI");
	} else {
		uri->module_path = strdup(provider_id);
	}

	r = pkcs11_register_provider_by_uri(uri, pin, keyp, labelsp, providerp, user);
	pkcs11_uri_cleanup(uri);

	return r;
}

int
pkcs11_add_provider_by_uri(struct pkcs11_uri *uri, char *pin,
    struct sshkey ***keyp, char ***labelsp)
{
	struct pkcs11_provider *p = NULL;
	int nkeys;
	char *provider_uri = pkcs11_uri_get(uri);

	debug_f("called, provider_uri = %s", provider_uri);

	nkeys = pkcs11_register_provider_by_uri(uri, pin, keyp, labelsp, &p, CKU_USER);

	/* no keys found or some other error, de-register provider */
	if (nkeys <= 0 && p != NULL) {
		TAILQ_REMOVE(&pkcs11_providers, p, next);
		pkcs11_provider_finalize(p);
		pkcs11_provider_unref(p);
	}
	if (nkeys == 0)
		debug_f("provider %s returned no keys", provider_uri);

	free(provider_uri);
	return nkeys;
}

/*
 * register a new provider and get number of keys hold by the token,
 * fails if provider already exists
 */
int
pkcs11_add_provider(char *provider_id, char *pin,
    struct sshkey ***keyp, char ***labelsp)
{
	struct pkcs11_uri *uri;
	int nkeys;

	uri = pkcs11_uri_init();
	if (uri == NULL)
		fatal("Failed to init PKCS#11 URI");

	if (strlen(provider_id) >= strlen(PKCS11_URI_SCHEME) &&
	    strncmp(provider_id, PKCS11_URI_SCHEME, strlen(PKCS11_URI_SCHEME)) == 0) {
		if (pkcs11_uri_parse(provider_id, uri) != 0)
			fatal("Failed to parse PKCS#11 URI");
	} else {
		uri->module_path = strdup(provider_id);
	}

	nkeys = pkcs11_add_provider_by_uri(uri, pin, keyp, labelsp);
	pkcs11_uri_cleanup(uri);

	return (nkeys);
}

#ifdef WITH_PKCS11_KEYGEN
struct sshkey *
pkcs11_gakp(char *provider_id, char *pin, unsigned int slotidx, char *label,
    unsigned int type, unsigned int bits, unsigned char keyid, u_int32_t *err)
{
	struct pkcs11_provider	*p = NULL;
	struct pkcs11_slotinfo	*si;
	CK_FUNCTION_LIST	*f;
	CK_SESSION_HANDLE	 session;
	struct sshkey		*k = NULL;
	int			 ret = -1, reset_pin = 0, reset_provider = 0;
	CK_RV			 rv;

	*err = 0;

	if ((p = pkcs11_provider_lookup(provider_id)) != NULL)
		debug_f("provider \"%s\" available", provider_id);
	else if ((ret = pkcs11_register_provider(provider_id, pin, NULL, NULL,
	    &p, CKU_SO)) < 0) {
		debug_f("could not register provider %s", provider_id);
		goto out;
	} else
		reset_provider = 1;

	f = p->function_list;
	si = &p->slotinfo[slotidx];
	session = si->session;

	if ((rv = f->C_SetOperationState(session , pin, strlen(pin),
	    CK_INVALID_HANDLE, CK_INVALID_HANDLE)) != CKR_OK) {
		debug_f("could not supply SO pin: %lu", rv);
		reset_pin = 0;
	} else
		reset_pin = 1;

	switch (type) {
	case KEY_RSA:
		if ((k = pkcs11_rsa_generate_private_key(p, slotidx, label,
		    bits, keyid, err)) == NULL) {
			debug_f("failed to generate RSA key");
			goto out;
		}
		break;
	case KEY_ECDSA:
		if ((k = pkcs11_ecdsa_generate_private_key(p, slotidx, label,
		    bits, keyid, err)) == NULL) {
			debug_f("failed to generate ECDSA key");
			goto out;
		}
		break;
	default:
		*err = SSH_PKCS11_ERR_GENERIC;
		debug_f("unknown type %d", type);
		goto out;
	}

out:
	if (reset_pin)
		f->C_SetOperationState(session , NULL, 0, CK_INVALID_HANDLE,
		    CK_INVALID_HANDLE);

	if (reset_provider)
		pkcs11_del_provider(provider_id);

	return (k);
}

struct sshkey *
pkcs11_destroy_keypair(char *provider_id, char *pin, unsigned long slotidx,
    unsigned char keyid, u_int32_t *err)
{
	struct pkcs11_provider	*p = NULL;
	struct pkcs11_slotinfo	*si;
	struct sshkey		*k = NULL;
	int			 reset_pin = 0, reset_provider = 0;
	CK_ULONG		 nattrs;
	CK_FUNCTION_LIST	*f;
	CK_SESSION_HANDLE	 session;
	CK_ATTRIBUTE		 attrs[16];
	CK_OBJECT_CLASS		 key_class;
	CK_KEY_TYPE		 key_type;
	CK_OBJECT_HANDLE	 obj = CK_INVALID_HANDLE;
	CK_RV			 rv;

	*err = 0;

	if ((p = pkcs11_provider_lookup(provider_id)) != NULL) {
		debug_f("using provider \"%s\"", provider_id);
	} else if (pkcs11_register_provider(provider_id, pin, NULL, NULL, &p,
	    CKU_SO) < 0) {
		debug_f("could not register provider %s",
		    provider_id);
		goto out;
	} else
		reset_provider = 1;

	f = p->function_list;
	si = &p->slotinfo[slotidx];
	session = si->session;

	if ((rv = f->C_SetOperationState(session , pin, strlen(pin),
	    CK_INVALID_HANDLE, CK_INVALID_HANDLE)) != CKR_OK) {
		debug_f("could not supply SO pin: %lu", rv);
		reset_pin = 0;
	} else
		reset_pin = 1;

	/* private key */
	nattrs = 0;
	key_class = CKO_PRIVATE_KEY;
	FILL_ATTR(attrs, nattrs, CKA_CLASS, &key_class, sizeof(key_class));
	FILL_ATTR(attrs, nattrs, CKA_ID, &keyid, sizeof(keyid));

	if (pkcs11_find(p, slotidx, attrs, nattrs, &obj) == 0 &&
	    obj != CK_INVALID_HANDLE) {
		if ((rv = f->C_DestroyObject(session, obj)) != CKR_OK) {
			debug_f("could not destroy private key 0x%hhx",
			    keyid);
			*err = rv;
			goto out;
		}
	}

	/* public key */
	nattrs = 0;
	key_class = CKO_PUBLIC_KEY;
	FILL_ATTR(attrs, nattrs, CKA_CLASS, &key_class, sizeof(key_class));
	FILL_ATTR(attrs, nattrs, CKA_ID, &keyid, sizeof(keyid));

	if (pkcs11_find(p, slotidx, attrs, nattrs, &obj) == 0 &&
	    obj != CK_INVALID_HANDLE) {

		/* get key type */
		nattrs = 0;
		FILL_ATTR(attrs, nattrs, CKA_KEY_TYPE, &key_type,
		    sizeof(key_type));
		rv = f->C_GetAttributeValue(session, obj, attrs, nattrs);
		if (rv != CKR_OK) {
			debug_f("could not get key type of public key 0x%hhx",
			    keyid);
			*err = rv;
			key_type = -1;
		}
		if (key_type == CKK_RSA)
			k = pkcs11_fetch_rsa_pubkey(p, slotidx, &obj);
		else if (key_type == CKK_ECDSA)
			k = pkcs11_fetch_ecdsa_pubkey(p, slotidx, &obj);

		if ((rv = f->C_DestroyObject(session, obj)) != CKR_OK) {
			debug_f("could not destroy public key 0x%hhx", keyid);
			*err = rv;
			goto out;
		}
	}

out:
	if (reset_pin)
		f->C_SetOperationState(session , NULL, 0, CK_INVALID_HANDLE,
		    CK_INVALID_HANDLE);

	if (reset_provider)
		pkcs11_del_provider(provider_id);

	return (k);
}
#endif /* WITH_PKCS11_KEYGEN */
#else /* ENABLE_PKCS11 */

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>

#include "log.h"
#include "sshkey.h"

int
pkcs11_init(int interactive)
{
	error("%s: dlopen() not supported", __func__);
	return (-1);
}

int
pkcs11_add_provider(char *provider_id, char *pin, struct sshkey ***keyp,
    char ***labelsp)
{
	error("%s: dlopen() not supported", __func__);
	return (-1);
}

void
pkcs11_terminate(void)
{
	error("%s: dlopen() not supported", __func__);
}
#endif /* ENABLE_PKCS11 */
