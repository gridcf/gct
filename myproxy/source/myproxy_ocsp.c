/*
 * Copyright 2003, John Viega and Matt Messier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * 3. Neither the names of the authors nor the names of the
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Modified from the Secure Programming Cookbook for C and C++ by John
 * Viega and Matt Messier (http://www.secureprogramming.com/).
 */

#include "myproxy_common.h"
#include "myproxy_ocsp.h"
#include "myproxy_ocsp_aia.h"

#if defined(HAVE_OCSP)
#include <openssl/bio.h>
#include <openssl/conf.h>
#include <openssl/ocsp.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

typedef enum {
  MYPROXY_OCSPRESULT_ERROR_NOTCONFIGURED     = -14,
  MYPROXY_OCSPRESULT_ERROR_NOAIAOCSPURI      = -13,
  MYPROXY_OCSPRESULT_ERROR_INVALIDRESPONSE   = -12,
  MYPROXY_OCSPRESULT_ERROR_CONNECTFAILURE    = -11,
  MYPROXY_OCSPRESULT_ERROR_SIGNFAILURE       = -10,
  MYPROXY_OCSPRESULT_ERROR_BADOCSPADDRESS    = -9,
  MYPROXY_OCSPRESULT_ERROR_OUTOFMEMORY       = -8,
  MYPROXY_OCSPRESULT_ERROR_UNKNOWN           = -7,
  MYPROXY_OCSPRESULT_ERROR_UNAUTHORIZED      = -6,
  MYPROXY_OCSPRESULT_ERROR_SIGREQUIRED       = -5,
  MYPROXY_OCSPRESULT_ERROR_TRYLATER          = -3,
  MYPROXY_OCSPRESULT_ERROR_INTERNALERROR     = -2,
  MYPROXY_OCSPRESULT_ERROR_MALFORMEDREQUEST  = -1,
  MYPROXY_OCSPRESULT_CERTIFICATE_VALID       = 0,
  MYPROXY_OCSPRESULT_CERTIFICATE_REVOKED     = 1
} myproxy_ocspresult_t;

static char      *responder_url = NULL;
static STACK_OF(X509) *responder_cert = NULL;
static char      *policy      = NULL;
static X509      *sign_cert   = NULL;
static EVP_PKEY  *sign_key    = NULL;
static long      skew         = MYPROXY_DEFAULT_CLOCK_SKEW;
static long      maxage       = -1;
static int       usenonce     = 0;

int
myproxy_ocsp_set_responder(const char *newurl) {
    if (responder_url) free(responder_url);
    responder_url = strdup(newurl);
    return 0;
}

int
myproxy_ocsp_set_responder_cert(const char *path) {
    BIO *    in = NULL;
    X509 *   x  = NULL;
    int      count;
    int      rval = -1;

	sk_X509_pop_free(responder_cert, X509_free);
    responder_cert = NULL;

    in = BIO_new(BIO_s_file());
    if (in == NULL || BIO_read_filename(in, path) <= 0) {
        verror_put_string("error reading %s", path);
        goto exit;
    }
    responder_cert = sk_X509_new_null();
    if (!responder_cert) {
        verror_put_string("sk_X509_new_null() failed in "
                          "myproxy_ocsp_set_responder_cert()");
        goto exit;
    }
    for (count = 0; ; count++) {
        x = PEM_read_bio_X509(in, NULL, NULL, NULL);
        if (x == NULL) {
            if ((ERR_GET_REASON(ERR_peek_error()) ==
                 PEM_R_NO_START_LINE) && (count > 0)) {
                ERR_clear_error();
                break;
            } else {
                verror_put_string("error reading %s", path);
                goto exit;
            }
        }
        sk_X509_insert(responder_cert,x,sk_X509_num(responder_cert));
        x = NULL;
    }

    rval = 0;                   /* success */

 exit:
    if (in) BIO_free_all(in);
    if (x)  X509_free(x);

    return rval;
}

int
myproxy_ocsp_set_policy(const char *newpolicy) {
    if (policy) free(policy);
    policy = strdup(newpolicy);
    return 0;
}

int
myproxy_ocsp_set_signer(X509 *new_sign_cert, EVP_PKEY *new_sign_key) {
    sign_cert = new_sign_cert;
    sign_key = new_sign_key;
    return 0;
}

int
myproxy_ocsp_set_times(long newskew, long newmaxage) {
    skew = newskew;
    maxage = newmaxage;
    return 0;
}

int
myproxy_ocsp_use_nonce(int newusenonce) {
    usenonce = newusenonce;
    return 0;
}

#if !defined(HAVE_OCSP)

int myproxy_ocsp_verify(X509 *cert, X509 *issuer) {
    return MYPROXY_OCSPRESULT_ERROR_NOTCONFIGURED;
}

#else

static int
verify_cert_hostname(X509 *cert, char *hostname) {
  int                   extcount, i, j, ok = 0;
  char                  name[256];
  X509_NAME             *subj;
  const char            *extstr;
  CONF_VALUE            *nval;
  const unsigned char   *data;
  X509_EXTENSION        *ext;
  X509V3_EXT_METHOD     *meth;
  STACK_OF(CONF_VALUE)  *val;

  if ((extcount = X509_get_ext_count(cert)) > 0) {
    for (i = 0;  !ok && i < extcount;  i++) {
      ext = X509_get_ext(cert, i);
      extstr = OBJ_nid2sn(OBJ_obj2nid(X509_EXTENSION_get_object(ext)));
      if (!strcasecmp(extstr, "subjectAltName")) {
        if (!(meth = (X509V3_EXT_METHOD *)X509V3_EXT_get(ext))) break;
        data = X509_EXTENSION_get_data(ext)->data;

        val = meth->i2v(meth, meth->d2i(0, &data, X509_EXTENSION_get_data(ext)->length), 0);
        for (j = 0;  j < sk_CONF_VALUE_num(val);  j++) {
          nval = sk_CONF_VALUE_value(val, j);
          if (!strcasecmp(nval->name, "DNS") && !strcasecmp(nval->value, hostname)) {
            ok = 1;
            break;
          }
        }
      }
    }
  }

  if (!ok && (subj = X509_get_subject_name(cert)) &&
      X509_NAME_get_text_by_NID(subj, NID_commonName, name, sizeof(name)) > 0) {
    name[sizeof(name) - 1] = '\0';
    if (!strcasecmp(name, hostname)) ok = 1;
  }

  return ok;
}

static BIO *
my_connect_ssl(char *host, int port, SSL_CTX **ctx) {
  BIO *conn = 0;

  if (!(conn = BIO_new_ssl_connect(*ctx))) goto error_exit;
  BIO_set_conn_hostname(conn, host);
  char chport[6];
  snprintf(chport, sizeof(chport), "%d", port);
  BIO_set_conn_port(conn, chport);

  if (BIO_do_connect(conn) <= 0) goto error_exit;
  return conn;

error_exit:
  if (conn) BIO_free_all(conn);
  return 0;
}

static BIO *
my_connect(char *host, int port, int ssl, SSL_CTX **ctx) {
  BIO *conn;
  SSL *ssl_ptr;

  if (ssl) {
    if (!(conn = my_connect_ssl(host, port, ctx))) goto error_exit;
    BIO_get_ssl(conn, &ssl_ptr);
    if (!verify_cert_hostname(SSL_get_peer_certificate(ssl_ptr), host))
      goto error_exit;
    if (SSL_get_verify_result(ssl_ptr) != X509_V_OK) goto error_exit;
    return conn;
  }

  if (!(conn = BIO_new_connect(host))) goto error_exit;
  char chport[6];
  snprintf(chport, sizeof(chport), "%d", port);
  BIO_set_conn_port(conn, chport);
  if (BIO_do_connect(conn) <= 0) goto error_exit;
  return conn;

error_exit:
  if (conn) BIO_free_all(conn);
  return 0;
}

int myproxy_ocsp_verify(X509 *cert, X509 *issuer) {
  BIO                   *bio = 0;
  int                   rc, reason, ssl, status;
  char                  *host = 0, *path = 0, *port = 0, *certdir = 0;
  char                  *aiaocspurl = 0, *chosenurl = 0;
  SSL_CTX               *ctx = 0;
  X509_LOOKUP           *lookup = NULL;
  X509_STORE            *store = 0;
  OCSP_CERTID           *id;
  OCSP_REQUEST          *req = 0;
  OCSP_RESPONSE         *resp = 0;
  OCSP_BASICRESP        *basic = 0;
  myproxy_ocspresult_t  result;
  ASN1_GENERALIZEDTIME  *producedAt, *thisUpdate, *nextUpdate;
  globus_result_t       res;

  if (!policy && !responder_url) {
      result = MYPROXY_OCSPRESULT_ERROR_NOTCONFIGURED;
      goto end;
  }

  result = MYPROXY_OCSPRESULT_ERROR_UNKNOWN;

  if (policy && strstr(policy, "aia")) {
      aiaocspurl = myproxy_get_aia_ocsp_uri(cert);
  }

  if (!responder_url && !aiaocspurl) {
      result = MYPROXY_OCSPRESULT_ERROR_NOTCONFIGURED;
      goto end;
  }

  chosenurl = aiaocspurl ? aiaocspurl : responder_url;
  if (!OCSP_parse_url(chosenurl, &host, &port, &path, &ssl)) {
    host = port = path = 0; /* may return bad ptrs on failure */
    result = MYPROXY_OCSPRESULT_ERROR_BADOCSPADDRESS;
    goto end;
  }

  myproxy_log("querying OCSP responder at %s", chosenurl);

  if (!(req = OCSP_REQUEST_new())) {
    result = MYPROXY_OCSPRESULT_ERROR_OUTOFMEMORY;
    goto end;
  }

  id = OCSP_cert_to_id(0, cert, issuer);
  if (!id || !OCSP_request_add0_id(req, id)) goto end;
  if (usenonce) OCSP_request_add1_nonce(req, 0, -1);

  /* sign the request */
  if (sign_cert && sign_key &&
      !OCSP_request_sign(req, sign_cert, sign_key, EVP_sha256(), 0, 0)) {
    result = MYPROXY_OCSPRESULT_ERROR_SIGNFAILURE;
    goto end;
  }

  /* setup GSI context */
  store=X509_STORE_new();
  lookup = X509_STORE_add_lookup(store, X509_LOOKUP_hash_dir());
  if (lookup == NULL) {
    result = MYPROXY_OCSPRESULT_ERROR_OUTOFMEMORY;
    goto end;
  }
  res = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&certdir);
  if (res != GLOBUS_SUCCESS) {
    verror_put_string("failed to find GSI CA cert directory");
    globus_error_to_verror(res);
    goto end;
  }
  X509_LOOKUP_add_dir(lookup, certdir, X509_FILETYPE_PEM);
  #if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
  ctx = SSL_CTX_new(TLS_client_method());
  #else
  ctx = SSL_CTX_new(SSLv23_client_method());
  #endif
  if (ctx == NULL) {
    result = MYPROXY_OCSPRESULT_ERROR_OUTOFMEMORY;
    goto end;
  }
  #if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
  SSL_CTX_set_min_proto_version(ctx, TLS1_VERSION);
  #else
  SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
  #endif
  SSL_CTX_set_cert_store(ctx, store);
  SSL_CTX_set_verify(ctx,SSL_VERIFY_PEER,NULL);

  /* establish a connection to the OCSP responder */
  if (!(bio = my_connect(host, atoi(port), ssl, &ctx))) {
    result = MYPROXY_OCSPRESULT_ERROR_CONNECTFAILURE;
    goto end;
  }

  /* send the request and get a response */
  resp = OCSP_sendreq_bio(bio, path, req);
  if ((rc = OCSP_response_status(resp)) != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
    switch (rc) {
      case OCSP_RESPONSE_STATUS_MALFORMEDREQUEST:
        result = MYPROXY_OCSPRESULT_ERROR_MALFORMEDREQUEST; break;
      case OCSP_RESPONSE_STATUS_INTERNALERROR:
        result = MYPROXY_OCSPRESULT_ERROR_INTERNALERROR;    break;
      case OCSP_RESPONSE_STATUS_TRYLATER:
        result = MYPROXY_OCSPRESULT_ERROR_TRYLATER;         break;
      case OCSP_RESPONSE_STATUS_SIGREQUIRED:
        result = MYPROXY_OCSPRESULT_ERROR_SIGREQUIRED;      break;
      case OCSP_RESPONSE_STATUS_UNAUTHORIZED:
        result = MYPROXY_OCSPRESULT_ERROR_UNAUTHORIZED;     break;
    }
    goto end;
  }

  /* verify the response */
  result = MYPROXY_OCSPRESULT_ERROR_INVALIDRESPONSE;
  if (!(basic = OCSP_response_get1_basic(resp))) goto end;
  if (usenonce && OCSP_check_nonce(req, basic) <= 0) goto end;

  if (!responder_cert ||
      (rc = OCSP_basic_verify(basic, responder_cert, store,
                              OCSP_TRUSTOTHER)) <= 0)
      if ((rc = OCSP_basic_verify(basic, NULL, store, 0)) <= 0) 
          goto end;

  if (!OCSP_resp_find_status(basic, id, &status, &reason, &producedAt,
                             &thisUpdate, &nextUpdate))
    goto end;
  if (!OCSP_check_validity(thisUpdate, nextUpdate, skew, maxage))
    goto end;

  /* All done.  Set the return code based on the status from the response. */
  if (status == V_OCSP_CERTSTATUS_REVOKED) {
    result = MYPROXY_OCSPRESULT_CERTIFICATE_REVOKED;
    myproxy_log("OCSP status revoked!");
  } else {
    result = MYPROXY_OCSPRESULT_CERTIFICATE_VALID;
    myproxy_log("OCSP status valid");
  }

end:
  if (result < 0 && result != MYPROXY_OCSPRESULT_ERROR_NOTCONFIGURED) {
      ssl_error_to_verror();
      myproxy_log("OCSP check failed");
      myproxy_log_verror();
  }
  if (bio) BIO_free_all(bio);
  if (host) OPENSSL_free(host);
  if (port) OPENSSL_free(port);
  if (path) OPENSSL_free(path);
  if (req) OCSP_REQUEST_free(req);
  if (resp) OCSP_RESPONSE_free(resp);
  if (basic) OCSP_BASICRESP_free(basic);
  if (ctx) SSL_CTX_free(ctx);   /* this does X509_STORE_free(store) */
  if (certdir) free(certdir);
  if (aiaocspurl) free(aiaocspurl);

  return result;
}
#endif
