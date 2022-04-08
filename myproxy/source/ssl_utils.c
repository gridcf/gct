/*
 * ssl_utils.c
 *
 * Routines for interacting directly with SSL, X509 certificates, etc.
 */


#include "myproxy_common.h"	/* all needed headers included here */

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#define PEM_CALLBACK(func)	func, NULL
#define PEM_NO_CALLBACK		NULL, NULL

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define EVP_PKEY_id(k) (k)->type
#endif

/**********************************************************************
 *
 * Constants
 *
 */
#define PROXY_DEFAULT_LIFETIME		-1L /* magic # for lifetime */
                                            /*   of signing cert    */

/**********************************************************************
 *
 * Internal data structures
 *
 */
struct _ssl_credentials
{
    X509		*certificate;
    EVP_PKEY		*private_key;
    STACK_OF(X509)	*certificate_chain;

    globus_gsi_proxy_handle_t	proxy_req;
};

struct _ssl_proxy_restrictions
{
    /* 0 = unrestricted, 1 = limited */
    int			limited_proxy;

    /* Proxy lifetime in seconds, 0 means default, -1 means maximum */
    long		lifetime;
};

/**********************************************************************
 *
 * Internal variables.
 *
 */

/*
 * Holder for pass phrase so callback function can find it.
 */
static const char *_ssl_pass_phrase = NULL;

/**********************************************************************
 *
 * Internal functions.
 *
 */

/*
 * ssl_error_to_verror()
 *
 * Transfer an error description out of the ssl error handler to verror.
 */
void
ssl_error_to_verror()
{
	unsigned long error;
	ERR_STATE *error_state;
	const char *error_data;
	int error_number;

    while ((error = ERR_peek_error()) != 0)
    {
        /* Find data for last error */
        error_state = ERR_get_state();

        error_number = (error_state->bottom + 1) % ERR_NUM_ERRORS;
	
        error_data = error_state->err_data[error_number];

        /* Now add to verror state */
        verror_put_string("%s", ERR_error_string(error, NULL));

        if (error_data != NULL)
        {
            verror_put_string("%s", error_data);
        }

        /* Pop error off of stack */
        ERR_get_error();
    }
    
    ERR_clear_error();
}

/*
 * globus_error_to_verror()
 *
 * Transfer an error description out of the Globus error handler to verror.
 */
void
globus_error_to_verror(globus_result_t result)
{
    globus_object_t *error;
    char *desc;

    error = globus_error_get(result);
    if (!error) return;
    desc = globus_error_print_chain(error);
    globus_object_free(error);
    if (!desc) return;
    verror_put_string("%s", desc);
    free(desc);
}

/*
 * bio_from_buffer()
 *
 * Given a buffer of length buffer_len, return a memory bio with the
 * contents of the buffer.
 *
 * Returns pointer to bio on success, NULL on error.
 */
static BIO *
bio_from_buffer(const unsigned char		*buffer,
		int				buffer_len)
{
    BIO			*bio = NULL;

    assert(buffer != NULL);
    
    bio = BIO_new(BIO_s_mem());

    if (bio == NULL)
    {
	verror_put_string("Failed creating memory BIO");
	ssl_error_to_verror();
	goto error;
    }

    if (BIO_write(bio, (unsigned char *) buffer, buffer_len) == SSL_ERROR)
    {
	verror_put_string("Failed writing buffer to BIO");
	ssl_error_to_verror();
	BIO_free(bio);
	bio = NULL;
	goto error;
    }

  error:
    return bio;
}


/*
 * bio_to_buffer()
 *
 * Given a bio return the contents of the bio in a buffer.
 * pbuffer is set to point to the allocated buffer, and pbuffer_len
 * is filled in with the buffer length. Caller should free *pbuffer.
 *
 * Returns SSL_SUCCESS or SSL_ERROR.
 */
static int
bio_to_buffer(BIO				*bio,
	      unsigned char			**pbuffer,
	      int				*pbuffer_len)
{
    char 		*buffer = NULL;
    int			buffer_len;
    int			return_status = SSL_ERROR;
    
    assert(bio != NULL);
    
    buffer_len = BIO_pending(bio);
    
    buffer = malloc(buffer_len+1);
    memset(buffer, '\0', buffer_len+1);
    
    if (buffer == NULL)
    {
	verror_put_string("Failed dumping BIO to buffer (malloc() failed)");
	verror_put_errno(errno);
	goto error;
    }
    
    if (BIO_read(bio, buffer, buffer_len) == SSL_ERROR)
    {
	verror_put_string("Failed dumping BIO to buffer (BIO_read() failed)");
	ssl_error_to_verror();
	goto error;
    }

    /* Success */
    *pbuffer = (unsigned char *)buffer;
    *pbuffer_len = buffer_len;
    return_status = SSL_SUCCESS;
    
  error:
    if (return_status == SSL_ERROR)
    {
	if (buffer != NULL)
	{
	    free(buffer);
	}
    }
    
    return return_status;
}

 
/*
 * ssl_cert_chain_free()
 *
 * Free the given certificate chain and all it contents.
 */
static void
ssl_cert_chain_free(STACK_OF(X509)			*cert_chain)
{
    if (cert_chain != NULL)
    {
	sk_X509_pop_free(cert_chain, X509_free);
    }
}

/*
 * ssl_credentials_free_contents()
 *
 * Free all the contents of the given credentials without freeing
 * the credentials structure itself.
 */
static void
ssl_credentials_free_contents(SSL_CREDENTIALS	*creds)
{
    if (creds != NULL)
    {
	if (creds->certificate != NULL) 
	{
	    X509_free(creds->certificate);
	}
	
	if (creds->private_key != NULL)
	{
	    EVP_PKEY_free(creds->private_key);
	}
	
	if (creds->certificate_chain != NULL)
	{
	    ssl_cert_chain_free(creds->certificate_chain);
	}
    }
}

static int
creds_from_bio(BIO *bio, SSL_CREDENTIALS **creds)
{
   STACK_OF(X509)      *cert_chain = NULL;
   X509                *cert = NULL;
   unsigned char       number_of_certs;
   int                 cert_index;
   int                 return_status = SSL_ERROR;
 
   if (BIO_read(bio, &number_of_certs, sizeof(number_of_certs)) == SSL_ERROR) {
      verror_put_string("Failed unpacking chain from buffer"
                        "(reading number of certificates)");
      ssl_error_to_verror();
      return SSL_ERROR;
   }

   if (number_of_certs == 0) {
      verror_put_string("Failed unpacking chain from buffer"
                        "(number of certificates is zero)");
      ssl_error_to_verror();
      return SSL_ERROR;
   }

   cert = d2i_X509_bio(bio, NULL /* make new cert */);

   if (cert == NULL) {
      verror_put_string("Failed unpacking chain from buffer"
                        "(reading user's certificate)");
      ssl_error_to_verror();
      goto end;
   }

   /* Now read the certificate chain */
   cert_chain = sk_X509_new_null();
   for (cert_index = 1; cert_index < number_of_certs; cert_index++) {
      X509  *x509;

      x509 = d2i_X509_bio(bio, NULL /* make new cert */);
      if (x509 == NULL) {
         verror_put_string("Failed unpacking chain from buffer"
                           "(reading certificate)");
         ssl_error_to_verror();
         goto end;
      }

      if (sk_X509_push(cert_chain, x509) == SSL_ERROR) {
         verror_put_string("Failed unpacking chain from buffer"
                           "(building a new chain)");
         ssl_error_to_verror();
         X509_free(x509);
         goto end;
      }
   }

   *creds = ssl_credentials_new();
   if (*creds == NULL) {
       verror_put_string("Failed unpacking chain from buffer"
                         "(building a new chain)");
       goto end;
   }
   (*creds)->certificate_chain = cert_chain;
   cert_chain = NULL;
   (*creds)->certificate = cert;
   cert = NULL;

   return_status = SSL_SUCCESS;

end:
   if (cert)
      X509_free(cert);
   if (cert_chain)
      ssl_cert_chain_free(cert_chain);

   return return_status;
}

static int
creds_to_bio(SSL_CREDENTIALS *chain, BIO **bio)
{
    unsigned char number_of_certs;
    BIO  *output_bio = NULL;
    int index;
    int return_status = SSL_ERROR;

    output_bio = BIO_new(BIO_s_mem());
    if (output_bio == NULL) {
       verror_put_string("BIO_new() failed");
       ssl_error_to_verror();
       return SSL_ERROR;
    }

    number_of_certs = 1;
    if (chain->certificate_chain != NULL)
      number_of_certs += sk_X509_num(chain->certificate_chain);

    if (BIO_write(output_bio, &number_of_certs,sizeof(number_of_certs)) == SSL_ERROR) {
       verror_put_string("Failed dumping chain to buffer"
                         "(BIO_write() failed)");
       ssl_error_to_verror();
       goto end;
    }

    if (i2d_X509_bio(output_bio, chain->certificate) == SSL_ERROR) {
       verror_put_string("Failed dumping chain to buffer "
                         "(write of user's certificate failed)");
       ssl_error_to_verror();
       goto end;
    }

    for (index = 0; index < sk_X509_num(chain->certificate_chain); index++) {
       X509  *cert;

       cert = (X509 *) sk_X509_value(chain->certificate_chain, index);
       if (i2d_X509_bio(output_bio, cert) == SSL_ERROR) {
          verror_put_string("Failed dumping chain to buffer "
                            "(write of cert chain failed)");
          ssl_error_to_verror();
          goto end;
       }
    }
    *bio = output_bio;
    output_bio = NULL;
    return_status = SSL_SUCCESS;
    
end:
    if (output_bio)
       BIO_free(output_bio);

    return return_status;
}

/*
 * my_init()
 *
 * Do any needed initialization for these routines.
 * Should be called first. Can be called multiple times.
 */
static void
my_init()
{
    static int my_inited = 0;
    
    if (my_inited == 0)
    {
	my_inited = 1;

	/* Initialize the ssleay libraries */

	SSL_load_error_strings();

    SSL_library_init();

	globus_module_activate(GLOBUS_GSI_PROXY_MODULE);
	globus_module_activate(GLOBUS_GSI_CREDENTIAL_MODULE);
	globus_module_activate(GLOBUS_GSI_SYSCONFIG_MODULE);
	globus_module_activate(GLOBUS_GSI_CERT_UTILS_MODULE);
    }
}

	
/*
 * my_pass_phrase_callback()
 *
 * Callback from PEM_read_PrivateKey() in ssl_load_user_key()
 * to return the passphrase stored in _ssl_pass_phrase.
 */
static int
my_pass_phrase_callback(char			*buffer,
			 int			buffer_len,
			 int			verify /* Ignored */,
			void                    *u)
{
    /* SSL libs supply these, make sure they are reasonable */
    assert(buffer != NULL);
    assert(buffer_len > 0);
    
    if (_ssl_pass_phrase == NULL)
    {
	strcpy(buffer, "");
    }
    else
    {
	strncpy(buffer, _ssl_pass_phrase, buffer_len);
	buffer[buffer_len - 1] = '\0';
    }

    return strlen(buffer);
}
	     

/*
 * ssl_x509_request_to_buffer()
 *
 * Dump the given X509 request structure to an allocated buffer.
 *
 * Returns SSL_SUCCESS or SSL_ERROR
 */
static int
ssl_x509_request_to_buffer(X509_REQ		*request,
			   unsigned char	**buffer,
			   int			*buffer_length)
{
    int				return_status = SSL_ERROR;
    BIO				*bio = NULL;
    
    assert(request != NULL);
    assert(buffer != NULL);
    assert(buffer_length != NULL);

    bio = BIO_new(BIO_s_mem());
    
    if (bio == NULL)
    {
	verror_put_string("Failed dumping X509 request to buffer (BIO_new() failed)");
	ssl_error_to_verror();
	goto error;
    }

    
    if (i2d_X509_REQ_bio(bio, request) == SSL_ERROR)
    {
	verror_put_string("Failed dumping X509 request to buffer");
	ssl_error_to_verror();
	goto error;
    }
    
    if (bio_to_buffer(bio, buffer, buffer_length) == SSL_ERROR)
    {
	goto error;
    }
        
    /* Success */
    return_status = SSL_SUCCESS;
    
  error:
    if (bio != NULL)
    {
	BIO_free(bio);
    }
    
    return return_status;
}


/**********************************************************************
 *
 * API Functions
 *
 */


void
ssl_credentials_destroy(SSL_CREDENTIALS		*creds)
{
    my_init();
    
    if (creds != NULL)
    {
	ssl_credentials_free_contents(creds);
	
	free(creds);
    }
}


int
ssl_proxy_file_destroy(const char *proxyfile)
{
    FILE *fp;
    long offset, i;
    char zero = '\0';
    struct stat s;
    int return_status = SSL_ERROR;
    
    assert(proxyfile != NULL);

    fp = fopen(proxyfile, "r+");
    if (!fp) {
	verror_put_string("fopen(%s): %s\n", proxyfile, strerror(errno));
	goto error;
    }
    /* Don't get fooled into zeroing out the wrong file via tricks
       with links and the like. */
    if (fstat(fileno(fp), &s) < 0) {
	verror_put_string("fstat(%s): %s\n", proxyfile, strerror(errno));
	goto error;
    }
    if (S_ISDIR(s.st_mode)) {
	verror_put_string("proxy file %s is a directory!\n", proxyfile);
	goto error;
    }
    if (!S_ISREG(s.st_mode)) {
	verror_put_string("proxy file %s is not a regular file!\n",
			  proxyfile);
	goto error;
    }
    if (s.st_nlink != 1) {
	verror_put_string("proxy file %s has links!\n", proxyfile);
	goto error;
    }
    if (fseek(fp, 0L, SEEK_END) < 0) {
	verror_put_string("fseek(%s): %s\n", proxyfile, strerror(errno));
	goto error;
    }
    offset = ftell(fp);
    if (offset < 0) {
	verror_put_string("ftell(%s): %s\n", proxyfile, strerror(errno));
	goto error;
    }
    if (fseek(fp, 0L, SEEK_SET) < 0) {
	verror_put_string("fseek(%s): %s\n", proxyfile, strerror(errno));
	goto error;
    }
    for (i=0; i < offset; i++) {
	if (fwrite(&zero, 1, 1, fp) != 1) {
	    verror_put_string("fwrite(%s): %s\n", proxyfile,
			      strerror(errno));
	    goto error;
	}
    }

    return_status = SSL_SUCCESS;

 error:
    if (fp) fclose(fp);
    if (unlink(proxyfile) < 0) { /* always try to unlink it, even on error */
	verror_put_string("unlink: %s\n", strerror(errno));
	return SSL_ERROR;
    }

    return return_status;
}	
		     
		     
int
ssl_certificate_load_from_file(SSL_CREDENTIALS	*creds,
			       const char	*path)
{
    FILE		*cert_file = NULL;
    X509		*cert = NULL;
    int			return_status = SSL_ERROR;
    STACK_OF(X509)	*cert_chain = NULL;
    
    assert(creds != NULL);
    assert(path != NULL);

    my_init();
    
    cert_file = fopen(path, "r");
    
    if (cert_file == NULL) 
    {
	verror_put_string("Error opening certificate file %s", path);
	verror_put_errno(errno);
	goto error;
    }
    
    if ((cert = PEM_read_X509(cert_file, NULL, PEM_NO_CALLBACK)) == NULL)
    {
	verror_put_string("Error reading certificate %s", path);
	ssl_error_to_verror();
	goto error;
    }

    if (creds->certificate != NULL)
    {
	X509_free(creds->certificate);
    }
    
    creds->certificate = cert;

    /* Ok, now read the certificate chain */

    /* Create empty stack */
    cert_chain = sk_X509_new_null();
    
    while (1)
    {
	cert = NULL;
	
    ERR_clear_error();          /* clear any prior OpenSSL errors */
	if ((cert = PEM_read_X509(cert_file, NULL, PEM_NO_CALLBACK)) == NULL)
	{
	    /*
	     * If we just can't find a start line then we've reached EOF.
	     */
	    if (ERR_GET_REASON(ERR_peek_error()) == PEM_R_NO_START_LINE)
	    {
		/* Just EOF, clear error and break out of loop */
		ERR_clear_error();
		break;
	    }

	    /* Actual error */
	    verror_put_string("Error parsing certificate chain");
	    ssl_error_to_verror();
	    goto error;
	}

	/* Add to chain */
	if (sk_X509_insert(cert_chain, cert,
		      sk_X509_num(cert_chain)) == SSL_ERROR)
	{
	    verror_put_string("Error parsing certificate chain");
	    ssl_error_to_verror();
	    goto error;
	}
    } /* while(1) */

    creds->certificate_chain = cert_chain;
    
    /* Success */
    return_status = SSL_SUCCESS;
    
  error:
    if (cert_file != NULL) 
    {
	fclose(cert_file);
    }
    
    return return_status;
}

int
ssl_certificate_push(SSL_CREDENTIALS    *creds,
                     X509 *cert)
{
    assert(creds != NULL);
    assert(cert != NULL);

    /* Place the given cert on top of other certs in creds */
    if (creds->certificate != NULL) {

      if (creds->certificate_chain == NULL)
        creds->certificate_chain = sk_X509_new_null();

      if (sk_X509_insert(creds->certificate_chain, creds->certificate, 0)
                                          == SSL_ERROR) {
	verror_put_string("Error inserting certificate into creds cert chain");
	ssl_error_to_verror();
        return SSL_ERROR;
      }
    }
    creds->certificate = cert;

    return SSL_SUCCESS;
}

int
ssl_private_key_load_from_file(SSL_CREDENTIALS	*creds,
			       const char	*path,
			       const char	*pass_phrase,
			       const char	*pass_phrase_prompt)
{
    FILE		*key_file = NULL;
    EVP_PKEY		*key = NULL;
    int			return_status = SSL_ERROR;
    
    assert(creds != NULL);
    assert(path != NULL);
    
    my_init();
    
    /* 
     * Put pass phrase where the callback function can find it.
     */
    _ssl_pass_phrase = pass_phrase;

    if (pass_phrase_prompt) EVP_set_pw_prompt((char *)pass_phrase_prompt);

    key_file = fopen(path, "r");
    
    if (key_file == NULL)
    {
	verror_put_string("Error opening key file %s", path);
	verror_put_errno(errno);
	goto error;
    }

    if ((key = PEM_read_PrivateKey(key_file, NULL, (pass_phrase_prompt) ?
            NULL : my_pass_phrase_callback, NULL)) == NULL)
    {
	unsigned long error, reason;
	
	error = ERR_peek_error();
	reason = ERR_GET_REASON(error);

	/* If this is a bad password, return a better error message */
	if (reason == EVP_R_BAD_DECRYPT ||
	    reason == PEM_R_BAD_PASSWORD_READ)
	{
	    verror_put_string("Bad password");
	}
	else 
	{
	    verror_put_string("Error reading private key %s", path);
	    ssl_error_to_verror();
	}
	
	goto error;
    }
    
    if (creds->private_key != NULL)
    {
	EVP_PKEY_free(creds->private_key);
    }

    creds->private_key = key;
    
    /* Success */
    return_status = SSL_SUCCESS;

  error:
    if (key_file != NULL)
    {
	fclose(key_file);
    }
    _ssl_pass_phrase = NULL;
    
    return return_status;
}

int
ssl_private_key_store_to_file(SSL_CREDENTIALS *creds,
                              const char *path,
                              const char *pass_phrase)
{
    BIO *keybio = 0;
    const EVP_CIPHER		*cipher;
    int				pass_phrase_len;
    int return_status = SSL_ERROR;

    keybio = BIO_new_file(path, "w");
    if (!keybio) {
        verror_put_string("failed to open %s", path);
        goto error;
    }

    if (pass_phrase == NULL)
    {
        /* No encryption */
        cipher = NULL;
        pass_phrase_len = 0;
    }
    else
    {
        /* Encrypt with pass phrase */
        /* XXX This is my best guess at a cipher */
        cipher = EVP_des_ede3_cbc();
        pass_phrase_len = strlen(pass_phrase);
    }

    /* Replaced PEM_write_bio_PrivateKey() with PEM_ASN1_write_bio() because
       starting with OpenSSL 1.0 PEM_write_bio_PrivateKey() wouldn't put "RSA"
       in "BEGIN RSA PRIVATE KEY" that could cause some grid utilities and such
       to fail. We should probably still consider reverting back to
       PEM_write_bio_PrivateKey() in the future as PEM_write_bio_PrivateKey()
       uses PEM_write_bio_PKCS8PrivateKey() which "uses the more more secure
       PKCS#8 private key format with a high iteration count" per the CHANGES
       file in the openssl tree */
    if (PEM_ASN1_write_bio((int (*)())i2d_PrivateKey,
		((EVP_PKEY_id(creds->private_key) == EVP_PKEY_DSA)?
				PEM_STRING_DSA:PEM_STRING_RSA),
                           keybio, (void *)creds->private_key, cipher,
                                 (unsigned char *) pass_phrase,
                                 pass_phrase_len,
                                 PEM_NO_CALLBACK) == SSL_ERROR)
    {
        verror_put_string("Error packing private key");
        ssl_error_to_verror();
        goto error;
    }

    return_status = SSL_SUCCESS;

 error:
    if (keybio) BIO_free(keybio);

    return return_status;
}

int
ssl_private_key_is_encrypted(const char	*path)
{
    FILE		*key_file = NULL;
    EVP_PKEY		*key = NULL;
    int			return_status = -1;
    
    my_init();
    
    key_file = fopen(path, "r");
    
    if (key_file == NULL) {
	verror_put_string("Error opening key file %s", path);
	verror_put_errno(errno);
	goto cleanup;		/* error */
    }

    _ssl_pass_phrase = NULL;
    ERR_clear_error();

    if ((key = PEM_read_PrivateKey(key_file, NULL,
		PEM_CALLBACK(my_pass_phrase_callback))) == NULL)
    {
	unsigned long error, reason;
	
	error = ERR_peek_error();
	reason = ERR_GET_REASON(error);
	if (error == 0 ||
	    reason == EVP_R_BAD_DECRYPT ||
	    reason == PEM_R_BAD_PASSWORD_READ ||
	    reason == PEM_R_NOT_PROC_TYPE)
	{
	    return_status = 1;		/* key is encrypted */
	    goto cleanup;
	} else {
	    verror_put_string("Error reading private key %s", path);
	    ssl_error_to_verror();
	    goto cleanup;	/* error */
	}
    }

    return_status = 0;		/* key unencrypted */
    
 cleanup:
    if (key_file) fclose(key_file);
    if (key) EVP_PKEY_free(key);
    ERR_clear_error();

    return return_status;	/* key unencrypted */
}

int
ssl_proxy_from_pem(SSL_CREDENTIALS		*creds,
		   const unsigned char		*buffer,
		   int				buffer_len,
		   const char			*pass_phrase)
{
    BIO			*bio = NULL;
    X509		*cert = NULL;
    EVP_PKEY		*key = NULL;
    STACK_OF(X509)	*cert_chain = NULL;
    int			return_status = SSL_ERROR;

    assert(creds != NULL);
    assert(buffer != NULL);

    my_init();
    
    /* 
     * Put pass phrase where the callback function can find it.
     */
    _ssl_pass_phrase = pass_phrase;
    ERR_clear_error();

    bio = bio_from_buffer(buffer, buffer_len);

    if (bio == NULL)
    {
	goto error;
    }

    /*
     * Proxy file contains proxy certificate followed by proxy
     * private key, followed by the certificate chain.
     */

    /* Read proxy certificate */
    if (PEM_read_bio_X509(bio, &cert, PEM_NO_CALLBACK) == NULL)
    {
	verror_put_string("Error parsing proxy certificate");
	ssl_error_to_verror();
	goto error;
    }

    /* Read proxy private key */
    if ((key = PEM_read_bio_PrivateKey(bio, NULL,
		PEM_CALLBACK(my_pass_phrase_callback))) == NULL)
    {
	unsigned long error, reason;
	
	error = ERR_peek_error();
	reason = ERR_GET_REASON(error);

	/* If this is a bad password, return a better error message */
	if (reason == EVP_R_BAD_DECRYPT ||
	    reason == PEM_R_BAD_PASSWORD_READ)
	{
	    verror_put_string("Bad password");
	}
	else 
	{
	    verror_put_string("Error parsing private key");
	    ssl_error_to_verror();
	}
	
	goto error;
    }

    /* Ok, now read the certificate chain */

    /* Create empty stack */
    cert_chain = sk_X509_new_null();
    
    while (1)
    {
	X509 *certificate = NULL;
	
    ERR_clear_error();          /* clear any prior OpenSSL errors */
	if (PEM_read_bio_X509(bio, &certificate,
			      PEM_NO_CALLBACK) == NULL)
	{
	    /*
	     * If we just can't find a start line then we've reached EOF.
	     */
	    if (ERR_GET_REASON(ERR_peek_error()) == PEM_R_NO_START_LINE)
	    {
		/* Just EOF, clear error and break out of loop */
		ERR_clear_error();
		break;
	    }

	    /* Actual error */
	    verror_put_string("Error parsing certificate chain from proxy");
	    ssl_error_to_verror();
	    goto error;
	}

	/* Add to chain */
	if (sk_X509_insert(cert_chain, certificate,
		      sk_X509_num(cert_chain)) == SSL_ERROR)
	{
	    verror_put_string("Error parsing certificate chain from proxy");
	    ssl_error_to_verror();
	    goto error;
	}
    } /* while(1) */

    /*
     * Ok, everything has been successfully read, now store it into
     * creds, removing any existing contents.
     */
    ssl_credentials_free_contents(creds);
    
    creds->private_key = key;
    creds->certificate = cert;
    creds->certificate_chain = cert_chain;
    
    /* Success */
    return_status = SSL_SUCCESS;
    
  error:
    if (return_status == SSL_ERROR)
    {
	/*
	 * On error, clean up any key, cert or chain. On success
	 * we don't want to do this as they are part of the creds.
	 */

	if (cert != NULL)
	{
	    X509_free(cert);
	}
	
	if (key != NULL)
	{
	    EVP_PKEY_free(key);

	}
	
	if (cert_chain)
	{
	    ssl_cert_chain_free(cert_chain);
	}
    }

    if (bio != NULL)
    {
	BIO_free(bio);
    }
    
    return return_status;
}

    
int
ssl_proxy_load_from_file(SSL_CREDENTIALS	*creds,
			 const char		*path,
			 const char		*pass_phrase)
{
    unsigned char	*buffer = NULL;
    int			buffer_len;
    int			return_status = SSL_ERROR;
    
    assert(creds != NULL);
    assert(path != NULL);

    my_init();

    /* Read the whole contents of the given file */
    if (buffer_from_file(path, &buffer, &buffer_len) == -1)
    {
	goto error;
    }

    if (ssl_proxy_from_pem(creds, buffer, buffer_len, pass_phrase) == SSL_ERROR)
    {
	verror_prepend_string("Error reading proxy from %s", path);
	goto error;
    }
    
    /* Success */
    return_status = SSL_SUCCESS;
    
  error:
    if (buffer != NULL)
    {
	free(buffer);
    }

    return return_status;
}


int
ssl_proxy_to_pem(SSL_CREDENTIALS		*creds,
		 unsigned char			**pbuffer,
		 int				*pbuffer_len,
		 const char			*pass_phrase)
{
    BIO				*bio = NULL;
    const EVP_CIPHER		*cipher;
    int				pass_phrase_len;
    int				cert_chain_index;
    int				return_status = SSL_ERROR;
    
    assert(creds != NULL);
    assert(pbuffer != NULL);
    assert(pbuffer_len != NULL);

    my_init();

    bio = BIO_new(BIO_s_mem());

    if (bio == NULL)
    {
	verror_put_string("Failed creating memory BIO");
	ssl_error_to_verror();
	goto error;
    }

    /*
     * Write out proxy certificate, followed by proxy private key and
     * then followed by the cert chain.
     */
    if (creds->certificate == NULL)
    {
	verror_put_string("Malformed proxy credentials (No certificate)");
	goto error;
    }
    
    if (PEM_write_bio_X509(bio, creds->certificate) == SSL_ERROR)
    {
	verror_put_string("Error packing proxy certificate");
	ssl_error_to_verror();
	goto error;
    }

    if (creds->private_key) {   /* just write certs if no private key */

    if (pass_phrase == NULL)
    {
	/* No encryption */
	cipher = NULL;
	pass_phrase_len = 0;
    }
    else
    {
	/* Encrypt with pass phrase */
	/* XXX This is my best guess at a cipher */
	cipher = EVP_des_ede3_cbc();
	pass_phrase_len = strlen(pass_phrase);
    }

    /* Replaced PEM_write_bio_PrivateKey() with PEM_ASN1_write_bio() because
       starting with OpenSSL 1.0 PEM_write_bio_PrivateKey() wouldn't put "RSA"
       in "BEGIN RSA PRIVATE KEY" that could cause some grid utilities and such
       to fail. We should probably still consider reverting back to
       PEM_write_bio_PrivateKey() in the future as PEM_write_bio_PrivateKey()
       uses PEM_write_bio_PKCS8PrivateKey() which "uses the more more secure
       PKCS#8 private key format with a high iteration count" per the CHANGES
       file in the openssl tree */
    if (PEM_ASN1_write_bio((int (*)())i2d_PrivateKey,
		((EVP_PKEY_id(creds->private_key) == EVP_PKEY_DSA)?
				PEM_STRING_DSA:PEM_STRING_RSA),
                           bio, (void *)creds->private_key, cipher,
				 (unsigned char *) pass_phrase,
				 pass_phrase_len,
				 PEM_NO_CALLBACK) == SSL_ERROR)
    {
	verror_put_string("Error packing private key");
	ssl_error_to_verror();
	goto error;
    }

    }
    
    if (creds->certificate_chain != NULL)
    {
	
	for (cert_chain_index = 0;
	     cert_chain_index < sk_X509_num(creds->certificate_chain);
	     cert_chain_index++)
	{
	    X509				*cert;
	
	    cert = (X509 *) sk_X509_value(creds->certificate_chain,
				     cert_chain_index);
	
	    if (PEM_write_bio_X509(bio, cert) == SSL_ERROR)
	    {
		verror_put_string("Error packing certificate chain");
		ssl_error_to_verror();
		goto error;
	    }
	}
    }
    
    /* OK, bio is filled, now dump to buffer */
    if (bio_to_buffer(bio, pbuffer, pbuffer_len) == SSL_ERROR)
    {
	goto error;
    }
    
    /* Success */
    return_status = SSL_SUCCESS;
    
  error:
    if (bio != NULL)
    {
	BIO_free(bio);
    }

    return return_status;
}
    
    
    
int
ssl_proxy_store_to_file(SSL_CREDENTIALS		*proxy_creds,
			const char		*path,
			const char		*pass_phrase)
{
    int				fd = -1;
    int				open_flags;
    int				return_status = SSL_ERROR;
    unsigned char		*buffer = NULL;
    int				buffer_len;
    mode_t			file_mode = 0;
    
    assert(proxy_creds != NULL);
    assert(path != NULL);
    
    my_init();
    
    /*
     * Use open to open the file so we can make sure it doesn't already
     * exist.
     */
    open_flags = O_CREAT | O_EXCL | O_WRONLY;
    file_mode = S_IRUSR | S_IWUSR;	/* 0600 */
    
    fd = open(path, open_flags, file_mode);
    
    if (fd == -1)
    {
	verror_put_string("Error creating %s", path);
	verror_put_errno(errno);
	goto error;
    }

    /*
     * Dump proxy to buffer
     */
    if (ssl_proxy_to_pem(proxy_creds, &buffer,
			 &buffer_len, pass_phrase) == SSL_ERROR)
    {
	goto error;
    }
    
    if (write(fd, buffer, buffer_len) == -1)
    {
	verror_put_errno(errno);
	verror_put_string("Error writing proxy to %s", path);
	goto error;
    }
    
    /* Success */
    return_status = SSL_SUCCESS;

  error:
    if (buffer != NULL)
    {
	free(buffer);
    }

    if (fd != -1)
    {
	close(fd);

	if (return_status == SSL_ERROR)
	{
	    /* Remove any file we created */
	    ssl_proxy_file_destroy(path);
	}
    }

    return return_status;
}

    
SSL_CREDENTIALS *
ssl_credentials_new()
{
    SSL_CREDENTIALS *creds = NULL;

    my_init();
    
    creds = malloc(sizeof(*creds));
    
    if (creds == NULL)
    {
	verror_put_errno(errno);
	goto error;
    }
    
    memset(creds, 0, sizeof(SSL_CREDENTIALS));
    
  error:
    return creds;
}

int
ssl_certreq_pem_to_der(char *certreq,
                       unsigned char **buffer, int *buffer_length)
{
    int return_status = SSL_ERROR;
    BIO *bio = NULL;
    X509_REQ *req = NULL;

    my_init();
    
    assert(certreq != NULL);
    assert(buffer != NULL);
    assert(buffer_length != NULL);

    if (certreq[0] == '-' && certreq[1] == '\0') {
        bio = BIO_new_fp(stdin,BIO_NOCLOSE);
    } else {
        bio = BIO_new_file(certreq, "r");
    }
    if (!bio) {
        ssl_error_to_verror();
        goto error;
    }

    req=PEM_read_bio_X509_REQ(bio,NULL,NULL,NULL);
    if (!req) {
        ssl_error_to_verror();
        goto error;
    }

    if (ssl_x509_request_to_buffer(req,
                                   buffer,
                                   buffer_length) == SSL_ERROR) {
        ssl_error_to_verror();
        goto error;
    }

    /* Success */
    return_status = SSL_SUCCESS;

 error:
    if (bio) BIO_free(bio);
	if (req) X509_REQ_free(req);
    
    return return_status;
    
}

int
ssl_proxy_delegation_init(SSL_CREDENTIALS	**new_creds,
			  unsigned char		**buffer,
			  int			*buffer_length,
			  int			requested_bits,
			  void			(*callback)(int,int,void *))
{
    int				return_status = SSL_ERROR;
    globus_result_t		local_result;
    globus_gsi_proxy_handle_attrs_t proxy_handle_attrs = NULL;
    BIO	      			*bio = NULL;
    char                        *GT_PROXY_MODE = NULL;
    char                *keybitsenv = NULL;
    int                 keybits = MYPROXY_DEFAULT_KEYBITS;

    my_init();
    
    assert(new_creds != NULL);
    assert(buffer != NULL);
    assert(buffer_length != NULL);

    if ((keybitsenv = getenv("MYPROXY_KEYBITS")) != NULL) {
        keybits = atoi(keybitsenv);
    }

    *new_creds = ssl_credentials_new();

    globus_gsi_proxy_handle_attrs_init(&proxy_handle_attrs);
    globus_gsi_proxy_handle_attrs_set_keybits(proxy_handle_attrs, keybits);

    /* globus_gsi_proxy_handle_init() initializes proxy_req->type, which is used
     * when the GT_PROXY_MODE environment variable is unset */
    local_result = globus_gsi_proxy_handle_init(&(*new_creds)->proxy_req,
						proxy_handle_attrs);
    /* done with proxy_handle_attrs now */
    globus_gsi_proxy_handle_attrs_destroy(proxy_handle_attrs);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_proxy_handle_init() failed");
    globus_error_to_verror(local_result);
	goto error;
    }
    GT_PROXY_MODE = getenv("GT_PROXY_MODE");
    if (GT_PROXY_MODE) {
        if (strcmp(GT_PROXY_MODE, "old") == 0) {
            local_result =
                globus_gsi_proxy_handle_set_type((*new_creds)->proxy_req,
                       GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2_PROXY);
#if defined(GLOBUS_GSI_CERT_UTILS_IS_RFC_PROXY)
        } else if (strcmp(GT_PROXY_MODE, "rfc") == 0) {
            local_result =
                globus_gsi_proxy_handle_set_type((*new_creds)->proxy_req,
                       GLOBUS_GSI_CERT_UTILS_TYPE_RFC_IMPERSONATION_PROXY);
#endif
        } else {
	    verror_put_string("Unsupported value for GT_PROXY_MODE: %s",
			      GT_PROXY_MODE);
	globus_error_to_verror(GLOBUS_FAILURE);
	    goto error;
        }
	if (local_result != GLOBUS_SUCCESS) {
	    verror_put_string("globus_gsi_proxy_handle_set_type() failed");
        globus_error_to_verror(local_result);
	    goto error;
        }
    }
    bio = BIO_new(BIO_s_mem());
    if (bio == NULL) {
	verror_put_string("BIO_new() failed");
	goto error;
    }
    local_result = globus_gsi_proxy_create_req((*new_creds)->proxy_req, bio);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_proxy_create_req() failed");
    globus_error_to_verror(local_result);
	goto error;
    }
    if (bio_to_buffer(bio, buffer, buffer_length) == SSL_ERROR) {
	verror_put_string("bio_to_buffer() failed");
	goto error;
    }
    
    /* Success */
    return_status = SSL_SUCCESS;

  error:

    if (bio) {
	BIO_free(bio);
    }
    
    return return_status;
}


int
ssl_proxy_delegation_finalize(SSL_CREDENTIALS	*creds,
			      unsigned char	*buffer,
			      int		buffer_length)
{
    BIO				*bio = NULL;
    int				return_status = SSL_ERROR;
    unsigned char		number_of_certs;
    globus_result_t		local_result;
    globus_gsi_cred_handle_t	cred_handle;

    assert(creds != NULL);
    assert(buffer != NULL);
    
    /* Transfer the buffer to a bio */
    bio = bio_from_buffer(buffer, buffer_length);

    if (bio == NULL)
    {
	verror_put_string("Failed unpacking proxy certificate from buffer");
	goto error;
    }

    /*
     * Buffer contains:
     *		-a bytes containing the number of certificates.
     *          -the proxy certificate
     *          -the certificate chain
     */

    /* Read number of certificates for backward compatibility */
    if (BIO_read(bio, &number_of_certs, sizeof(number_of_certs)) == SSL_ERROR)
    {
	verror_put_string("Failed unpacking proxy certificate from buffer (reading number of certificates)");
	ssl_error_to_verror();
	goto error;
    }

    if (number_of_certs == 0) 
    {
	verror_put_string("Failed unpacking proxy certificate from buffer (number of certificates == 0)");
	ssl_error_to_verror();
	goto error;
    }

    if (creds->proxy_req) {

    /* read the proxy certificate and certificate chain */
    local_result = globus_gsi_proxy_assemble_cred(creds->proxy_req,
						  &cred_handle, bio);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_proxy_assemble_cred() failed");
    globus_error_to_verror(local_result);
	goto error;
    }

    /* don't need the proxy_req anymore */
    globus_gsi_proxy_handle_destroy(creds->proxy_req);

    /* pull out what we need from the cred_handle */
    local_result = globus_gsi_cred_get_cert(cred_handle,
					    &creds->certificate);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_cred_get_cert() failed");
    globus_error_to_verror(local_result);
	goto error;
    }
    local_result = globus_gsi_cred_get_key(cred_handle,
					   &creds->private_key);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_cred_get_key() failed");
    globus_error_to_verror(local_result);
	goto error;
    }
    local_result = globus_gsi_cred_get_cert_chain(cred_handle,
						  &creds->certificate_chain);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_cred_get_cert_chain() failed");
    globus_error_to_verror(local_result);
	goto error;
    }
    globus_gsi_cred_handle_destroy(cred_handle);

    } else {
        X509                   *proxy_cert = NULL;
        int                     cert_index = 0;
        STACK_OF(X509)         *cert_chain = NULL;

        /* Now read the certificate */
        proxy_cert = d2i_X509_bio(bio, NULL /* make new cert */);
        if (proxy_cert == NULL)
        {
            verror_put_string("Failed unpacking certificate from buffer (reading certificate)");
            ssl_error_to_verror();
            goto error;
        }	

        cert_index++;
    
        /* Now read the certificate chain */
        cert_chain = sk_X509_new_null();
    
        while (cert_index < number_of_certs)
        {
            X509		*cert;
	
            cert = d2i_X509_bio(bio, NULL /* make new cert */);
    
            if (cert == NULL)
            {
                verror_put_string("Failed unpacking certificate from buffer (reading cert chain)");
                ssl_error_to_verror();
                goto error;
            }

            if (sk_X509_push(cert_chain, cert) == SSL_ERROR)
            {
                verror_put_string("Failed unpacking certificate from buffer (building cert chain)");
                ssl_error_to_verror();
                X509_free(cert);
                goto error;
            }

            cert_index++;
        }
    
        /* Success */

        /* XXX Should free any current contents first */
        creds->certificate = proxy_cert;
        creds->certificate_chain = cert_chain;
    }

    return_status = SSL_SUCCESS;
    
  error:
    if (bio != NULL)
    {
	BIO_free(bio);
    }
    
    return return_status;
}

int
ssl_proxy_delegation_sign(SSL_CREDENTIALS		*creds,
			  SSL_PROXY_RESTRICTIONS	*restrictions,
			  unsigned char			*input_buffer,
			  int				input_buffer_length,
			  unsigned char			**output_buffer,
			  int				*output_buffer_length)
{
    X509			*proxy_certificate = NULL;
    int				return_status = SSL_ERROR;
    BIO				*bio = NULL;
    unsigned char		number_of_certs;
    int				index;
    globus_gsi_proxy_handle_t	proxy_handle = NULL;
    globus_gsi_cred_handle_t	cred_handle = NULL;
    globus_result_t		local_result;
    globus_gsi_cert_utils_cert_type_t   cert_type;
    STACK_OF(X509_EXTENSION) *extensions = NULL;
    
    assert(creds != NULL);
    assert(creds->certificate);
    assert(creds->private_key);
    assert(input_buffer != NULL);
    assert(output_buffer != NULL);
    assert(output_buffer_length != NULL);

    my_init();

    /* initialize cred_handle with our credential so we can use
       Globus GSI API */
    local_result = globus_gsi_cred_handle_init(&cred_handle, NULL);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_cred_handle_init() failed");
    globus_error_to_verror(local_result);
	goto error;
    }
    local_result = globus_gsi_cred_set_cert(cred_handle, creds->certificate);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_cred_set_cert() failed");
    globus_error_to_verror(local_result);
	goto error;
    }
    local_result = globus_gsi_cred_set_key(cred_handle, creds->private_key);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_cred_set_key() failed");
    globus_error_to_verror(local_result);
	goto error;
    }
    local_result = globus_gsi_cred_set_cert_chain(cred_handle,
						  creds->certificate_chain);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_cred_set_cert_chain() failed");
    globus_error_to_verror(local_result);
	goto error;
    }

    /* proxy handle is the proxy we're going to sign */
    local_result = globus_gsi_proxy_handle_init(&proxy_handle, NULL);

    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_proxy_handle_init() failed");
    globus_error_to_verror(local_result);
	goto error;
    }

    /* what type of certificate do we have in the repository? */
    local_result = globus_gsi_cert_utils_get_cert_type(creds->certificate,
						       &cert_type);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_cert_utils_get_cert_type() failed");
    globus_error_to_verror(local_result);
	goto error;
    }

    /* get proxy request */
    bio = BIO_new(BIO_s_mem());
    if (bio == NULL) {
	verror_put_string("BIO_new() failed");
	goto error;
    }
    if (BIO_write(bio, input_buffer, input_buffer_length) < 0) {
	verror_put_string("BIO_write() failed");
	goto error;
    }
    local_result = globus_gsi_proxy_inquire_req(proxy_handle, bio);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_proxy_inquire_req() failed");
    globus_error_to_verror(local_result);
	goto error;
    }
    BIO_free(bio);
    bio = NULL;

    /* Set lifetime and limited options on proxy before signing. */
    if (GLOBUS_GSI_CERT_UTILS_IS_PROXY(cert_type)) {
	local_result = globus_gsi_proxy_handle_set_type(proxy_handle,
							cert_type);
	if (local_result != GLOBUS_SUCCESS) {
	    verror_put_string("globus_gsi_proxy_handle_set_type() failed");
        globus_error_to_verror(local_result);
	    goto error;
	}
    }
    if (restrictions && restrictions->limited_proxy) {
	globus_gsi_proxy_handle_get_type(proxy_handle, &cert_type);
	if (GLOBUS_GSI_CERT_UTILS_IS_GSI_3_PROXY(cert_type)) {
	    globus_gsi_proxy_handle_set_type(proxy_handle,
			      GLOBUS_GSI_CERT_UTILS_TYPE_GSI_3_LIMITED_PROXY);
#if defined(GLOBUS_GSI_CERT_UTILS_IS_RFC_PROXY)
	} else if (GLOBUS_GSI_CERT_UTILS_IS_RFC_PROXY(cert_type)) {
	    globus_gsi_proxy_handle_set_type(proxy_handle,
			      GLOBUS_GSI_CERT_UTILS_TYPE_RFC_LIMITED_PROXY);

#endif
	} else if (GLOBUS_GSI_CERT_UTILS_IS_GSI_2_PROXY(cert_type)) {
	    globus_gsi_proxy_handle_set_type(proxy_handle,
			      GLOBUS_GSI_CERT_UTILS_TYPE_GSI_2_LIMITED_PROXY);
	} else {
	    verror_put_string("unknown proxy type for limited proxy");
	    goto error;
	}
    }
    if (!restrictions || !restrictions->lifetime) {
	globus_gsi_proxy_handle_set_time_valid(proxy_handle,
					       PROXY_DEFAULT_LIFETIME/60);
    } else if (restrictions->lifetime > 0) {
	globus_gsi_proxy_handle_set_time_valid(proxy_handle,
					       restrictions->lifetime/60);
    }

#if defined(HAVE_GLOBUS_GSI_PROXY_HANDLE_SET_EXTENSIONS)
    /* add any additional extensions */
    myproxy_get_extensions(&extensions);
    if (extensions) {
        local_result =
            globus_gsi_proxy_handle_set_extensions(proxy_handle, extensions);
        if (local_result != GLOBUS_SUCCESS) {
            verror_put_string("globus_gsi_proxy_handle_set_extensions() failed");
            globus_error_to_verror(local_result);
            goto error;
        }
    }
#endif

    /* send number of certificates in reply for backward compatibility */
    bio = BIO_new(BIO_s_mem());
    if (bio == NULL) {
	verror_put_string("BIO_new() failed");
	goto error;
    }
    number_of_certs = 2;
    if (creds->certificate_chain != NULL)
      number_of_certs += sk_X509_num(creds->certificate_chain);
    if (BIO_write(bio, &number_of_certs, sizeof(number_of_certs)) == SSL_ERROR)
    {
	verror_put_string("Failed dumping proxy certificate to buffer (BIO_write() failed)");
	ssl_error_to_verror();
	goto error;
    }

    /* sign request and write out proxy certificate to bio */
    local_result = globus_gsi_proxy_sign_req(proxy_handle, cred_handle, bio);
    if (local_result != GLOBUS_SUCCESS) {
	verror_put_string("globus_gsi_proxy_sign_req() failed");
    globus_error_to_verror(local_result);
	goto error;
    }

    /* then write out our signing certificate... */
    if (i2d_X509_bio(bio, creds->certificate) == SSL_ERROR)
    {
	verror_put_string("Failed dumping proxy certificate to buffer (write of signing cert failed)");
	ssl_error_to_verror();
	goto error;
    }

    /* ...and any other certificates in the chain. */
    for (index = 0; index < sk_X509_num(creds->certificate_chain); index++)
    {
	X509		*cert;
	
	cert = sk_X509_value(creds->certificate_chain, index);
	
	if (i2d_X509_bio(bio, cert) == SSL_ERROR)
	{
	    verror_put_string("Failed dumping proxy certificate to buffer (write of cert chain failed)");
	    ssl_error_to_verror();
	    goto error;
	}
    }

    /* Now dump bio's contents to buffer */
    if (bio_to_buffer(bio, output_buffer, output_buffer_length) == SSL_ERROR)
    {
	goto error;
    }
         
    /* Success */
    return_status = SSL_SUCCESS;

  error:
    if (bio != NULL)
    {
	BIO_free(bio);
    }
    
    if (proxy_certificate != NULL)
    {
	X509_free(proxy_certificate);
    }

    if (proxy_handle)
    {
	globus_gsi_proxy_handle_destroy(proxy_handle);
    }

    if (cred_handle)
    {
	globus_gsi_cred_handle_destroy(cred_handle);
    }
    
    if (extensions)
    {
        sk_X509_EXTENSION_free(extensions);
    }

    return return_status;
}


void
ssl_free_buffer(unsigned char *buffer)
{
    if (buffer != NULL)
    {
	free(buffer);
    }
}


SSL_PROXY_RESTRICTIONS *
ssl_proxy_restrictions_new()
{
    SSL_PROXY_RESTRICTIONS		*restrictions = NULL;
    
    restrictions = malloc(sizeof(SSL_PROXY_RESTRICTIONS));

    if (restrictions == NULL)
    {
	verror_put_string("malloc() failed");
	verror_put_errno(errno);
	return NULL;
    }
    
    /* Set defaults */
    restrictions->limited_proxy = 0;	/* Not limited */
    restrictions->lifetime = 0;		/* 0 == default */
    
    return restrictions;
}

void
ssl_proxy_restrictions_destroy(SSL_PROXY_RESTRICTIONS *restrictions)
{
    if (restrictions != NULL)
    {
	free(restrictions);
    }
}

int
ssl_proxy_restrictions_set_lifetime(SSL_PROXY_RESTRICTIONS	*restrictions,
				    const long			lifetime)
{
    int				return_value = SSL_ERROR;
    
    /* Check arguments */
    if (restrictions == NULL)
    {
	verror_put_errno(EINVAL);
	goto error;
    }
    
    if (lifetime < 0L)
    {
	verror_put_errno(EINVAL);
	goto error;
    }

    /* OK */
    restrictions->lifetime = lifetime;
    return_value = SSL_SUCCESS;

    /* keep minimum lifetime at 5min for clock skew issues */
    if (restrictions->lifetime > 0 &&
        restrictions->lifetime < MYPROXY_DEFAULT_CLOCK_SKEW) {
        restrictions->lifetime = MYPROXY_DEFAULT_CLOCK_SKEW;
    }
    
  error:
    return return_value;
}

int
ssl_proxy_restrictions_set_limited(SSL_PROXY_RESTRICTIONS	*restrictions,
				    const int			limited)
{
    int				return_value = SSL_ERROR;
    
    /* Check arguments */
    if (restrictions == NULL)
    {
	verror_put_errno(EINVAL);
	goto error;
    }
    
    if (limited < 0)
    {
	verror_put_errno(EINVAL);
	goto error;
    }

    /* OK */
    restrictions->limited_proxy = limited;
    return_value = SSL_SUCCESS;

  error:
    return return_value;
}

int
ssl_get_base_subject_file(const char *proxyfile, char **subject)
{
   SSL_CREDENTIALS	*creds = NULL;
   int			return_value = -1;
   char			path[MAXPATHLEN];

   if (proxyfile == NULL) {
      char *user_cert = NULL;
      
      GLOBUS_GSI_SYSCONFIG_GET_PROXY_FILENAME(&user_cert,
					      GLOBUS_PROXY_FILE_INPUT);
      if (user_cert == NULL) {
	  GLOBUS_GSI_SYSCONFIG_GET_USER_CERT_FILENAME(&user_cert, NULL);
	  if (user_cert == NULL) {
	      verror_put_string("Unable to locate certificate to determine "
				"subject name.");
	      goto error;
	  }
      }
      strncpy(path, user_cert, sizeof(path)-1);
      free(user_cert);
   } else {
      strncpy(path, proxyfile, sizeof(path)-1);
   }

   creds = ssl_credentials_new();

   if (ssl_certificate_load_from_file(creds, path) != SSL_SUCCESS)
       goto error;

   if (ssl_get_base_subject(creds, subject) != SSL_SUCCESS)
       goto error;

   return_value = 0;

   error:
   if (creds) ssl_credentials_destroy(creds);
   return return_value;
}

int
ssl_get_base_subject(SSL_CREDENTIALS *creds, char **subject)
{
   char       client[1024];
   X509_NAME  *client_subject = NULL;
    
   client_subject = X509_NAME_dup(X509_get_subject_name(creds->certificate));
   if (client_subject == NULL) {
      return SSL_ERROR;
   }

   sk_X509_unshift(creds->certificate_chain, creds->certificate);
   globus_gsi_cert_utils_get_base_name(client_subject,
				       creds->certificate_chain);
   (void)sk_X509_shift(creds->certificate_chain);

   X509_NAME_oneline(client_subject, client, sizeof(client));
   *subject = strdup(client);
   X509_NAME_free(client_subject);

   return SSL_SUCCESS;
}

int
ssl_creds_to_buffer(SSL_CREDENTIALS *creds, unsigned char **buffer,
                    int *buffer_length)
{
   BIO  *bio = NULL;

   if (creds_to_bio(creds, &bio) == SSL_ERROR)
       return SSL_ERROR;

   if (bio_to_buffer(bio, buffer, buffer_length) == SSL_ERROR) {
       BIO_free(bio);
       return SSL_ERROR;
   }

   BIO_free(bio);

   return SSL_SUCCESS;
}

int
ssl_creds_from_buffer(unsigned char *buffer, int buffer_length,
                      SSL_CREDENTIALS **creds)
{
   BIO  *bio = NULL;

   bio = bio_from_buffer(buffer, buffer_length);
   if (bio == NULL)
      return SSL_ERROR;

   if (creds_from_bio(bio, creds) == SSL_ERROR) {
      BIO_free(bio);
      return SSL_ERROR;
   }

   BIO_free(bio);
   return SSL_SUCCESS;
}

int
ssl_creds_certificate_is_proxy(SSL_CREDENTIALS *creds)
{
    int return_status = -1;
    globus_result_t local_result;
    globus_gsi_cert_utils_cert_type_t cert_type;

    my_init();

    local_result = globus_gsi_cert_utils_get_cert_type(creds->certificate,
                                                       &cert_type);
    if (local_result != GLOBUS_SUCCESS) {
        verror_put_string("globus_gsi_cert_utils_get_cert_type() failed");
        globus_error_to_verror(local_result);
        goto error;
    }

    if (GLOBUS_GSI_CERT_UTILS_IS_PROXY(cert_type)) {
        return_status = 1; /* certificate is proxy */
    } else {
        return_status = 0; /* certificate is not proxy */
    }
 
  error:
    return return_status;
}

int
ssl_sign(unsigned char *data, int length,
         SSL_CREDENTIALS *creds,
	 unsigned char **signature, int *signature_len)
{
   EVP_MD_CTX *ctx = EVP_MD_CTX_create();

   *signature = malloc(EVP_PKEY_size(creds->private_key));
   if (*signature == NULL) {
      verror_put_string("malloc()");
      verror_put_errno(errno);
      return SSL_ERROR;
   }

   EVP_SignInit(ctx, EVP_sha256());
   EVP_SignUpdate(ctx, (void *)data, length);
   if (EVP_SignFinal(ctx, *signature, (unsigned int *)signature_len,
		     creds->private_key) != 1) {
      verror_put_string("Creating signature (EVP_SignFinal())");
      ssl_error_to_verror();
      free(*signature);
      EVP_MD_CTX_destroy(ctx);
      return SSL_ERROR;
   }

   EVP_MD_CTX_destroy(ctx);
   return SSL_SUCCESS;
}

int
ssl_verify(unsigned char *data, int length,
           SSL_CREDENTIALS *creds,
	   unsigned char *signature, int signature_len)
{
   EVP_MD_CTX *ctx = EVP_MD_CTX_create();
   EVP_PKEY *pubkey = NULL;

   EVP_VerifyInit(ctx, EVP_sha256());
   EVP_VerifyUpdate(ctx, (void*) data, length);
   pubkey = X509_get_pubkey(creds->certificate);
   if (EVP_VerifyFinal(ctx, signature, signature_len, pubkey) != 1 ) {
      verror_put_string("Verifying signature (EVP_VerifyFinal())");
      ssl_error_to_verror();
      EVP_MD_CTX_destroy(ctx);
      EVP_PKEY_free(pubkey);
      return SSL_ERROR;
   }

   EVP_MD_CTX_destroy(ctx);
   EVP_PKEY_free(pubkey);
   return SSL_SUCCESS;
}

/* Chain verifying is inspired by proxy_verify_chain() from GSI. */
int
ssl_verify_gsi_chain(SSL_CREDENTIALS *chain)
{
   int                   return_status = SSL_ERROR;
   int                   i,j;
   char                  *certdir = NULL;
   X509                  *cert = NULL, *issuer = NULL;
   X509_LOOKUP           *lookup = NULL;
   X509_STORE            *cert_store = NULL;
   X509_STORE_CTX        *csc;
   SSL                   *ssl = NULL;
   SSL_CTX               *sslContext = NULL;
   globus_result_t       res;

   int                                 callback_data_index;
   globus_gsi_cert_utils_cert_type_t   cert_type;
   globus_gsi_callback_data_t          callback_data = NULL;

   csc = X509_STORE_CTX_new();
   cert_store=X509_STORE_new();
   if (chain->certificate_chain != NULL) {
      for (i = 0; i < sk_X509_num(chain->certificate_chain); i++) {
	 cert = sk_X509_value(chain->certificate_chain, i);
	 j = X509_STORE_add_cert(cert_store, cert);
	 if (!j) {
	    if ((ERR_GET_REASON(ERR_peek_error()) == 
		                       X509_R_CERT_ALREADY_IN_HASH_TABLE)) {
	       ERR_clear_error();
	       break;
	    }
	    else {
	       verror_put_string("X509_STORE_add_cert()");
	       ssl_error_to_verror();
	       goto end;
	    }
	 }
      }
   }
   lookup = X509_STORE_add_lookup(cert_store, X509_LOOKUP_hash_dir());
   if (lookup == NULL) {
      verror_put_string("X509_STORE_add_lookup()");
      ssl_error_to_verror();
      goto end;
   }

   res = GLOBUS_GSI_SYSCONFIG_GET_CERT_DIR(&certdir);
   if (res != GLOBUS_SUCCESS) {
      verror_put_string("failed to find GSI CA cert directory");
      globus_error_to_verror(res);
      goto end;
   }
   X509_LOOKUP_add_dir(lookup, certdir, X509_FILETYPE_PEM);
   X509_STORE_CTX_init(csc, cert_store, chain->certificate, NULL);
   
   #if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
   sslContext = SSL_CTX_new(TLS_server_method());
   #else
   sslContext = SSL_CTX_new(SSLv23_server_method());
   #endif
   if (sslContext == NULL) {
      verror_put_string("Initializing SSL_CTX");
      ssl_error_to_verror();
      goto end;
   }
   #if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
   SSL_CTX_set_min_proto_version(sslContext, TLS1_VERSION);
   #else
   SSL_CTX_set_options(sslContext, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
   #endif

   SSL_CTX_set_purpose(sslContext, X509_PURPOSE_ANY);

   ssl = SSL_new(sslContext);
   if (ssl == NULL) {
      verror_put_string("Initializing SSL");
      ssl_error_to_verror();
      goto end;
   }

   /* override the check_issued with our version */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
   csc->check_issued = globus_gsi_callback_check_issued;
#else
   X509_STORE_set_check_issued(X509_STORE_CTX_get0_store(csc), globus_gsi_callback_check_issued);
#endif

   X509_STORE_CTX_set_app_data(csc, (void*)ssl);

   X509_STORE_CTX_set_depth(csc, 100); /* allow more than 9 certs in chain */

#if defined(X509_V_FLAG_ALLOW_PROXY_CERTS)
   X509_STORE_CTX_set_flags(csc, X509_V_FLAG_ALLOW_PROXY_CERTS);
#endif

   globus_gsi_callback_data_init(&callback_data);
   globus_gsi_callback_set_cert_dir(callback_data, certdir);
   globus_gsi_callback_get_X509_STORE_callback_data_index(&callback_data_index);
   X509_STORE_CTX_set_ex_data(csc, callback_data_index, (void *)callback_data);
   X509_STORE_CTX_set_verify_cb(csc, globus_gsi_callback_create_proxy_callback);

   /* check OCSP status of the EEC */
   if (globus_gsi_cert_utils_get_cert_type(chain->certificate,
                                           &cert_type) != GLOBUS_SUCCESS) {
       verror_put_string("globus_gsi_cert_utils_get_cert_type() failed");
       goto end;
   }
   if (!GLOBUS_GSI_CERT_UTILS_IS_PROXY(cert_type)) {
       cert = chain->certificate;
   } else {
       for (i = 0; i < sk_X509_num(chain->certificate_chain); i++) {
           cert = sk_X509_value(chain->certificate_chain, i);
           if (globus_gsi_cert_utils_get_cert_type(cert, &cert_type)
               != GLOBUS_SUCCESS) {
               verror_put_string("globus_gsi_cert_utils_get_cert_type() failed");
               goto end;
           }
           if (!GLOBUS_GSI_CERT_UTILS_IS_PROXY(cert_type)) {
               break;
           }
       }
   }
   if (X509_STORE_CTX_get1_issuer(&issuer, csc, cert) != 1) {
       verror_put_string("X509_STORE_CTX_get1_issuer() failed");
       ssl_error_to_verror();
       goto end;
   }

   if(X509_verify_cert(csc) != 1) {
      verror_put_string("X509_verify_cert() failed: %s",
			(char *)X509_verify_cert_error_string(X509_STORE_CTX_get_error(csc)));
      goto end;
   }

   if (myproxy_ocsp_verify(cert, issuer) == 1) {
       verror_put_string("OCSP says EEC is revoked!");
       goto end;
   }

   return_status = SSL_SUCCESS;

end:
   X509_STORE_CTX_free(csc);
   if (issuer)
       X509_free(issuer);
   if (ssl)
      SSL_free(ssl);
   if (sslContext)
      SSL_CTX_free(sslContext);
   if (certdir)
      free(certdir);
   if (cert_store)
      X509_STORE_free(cert_store);
   globus_gsi_callback_data_destroy(callback_data);

   return return_status;
}

int
ssl_limited_proxy_chain(SSL_CREDENTIALS *chain)
{
    X509 *cert = NULL;
    globus_gsi_cert_utils_cert_type_t   cert_type;
    int i;

    if (globus_gsi_cert_utils_get_cert_type(chain->certificate, &cert_type)
        != GLOBUS_SUCCESS) {
        verror_put_string("globus_gsi_cert_utils_get_cert_type() failed");
        return -1;
    }
    if (GLOBUS_GSI_CERT_UTILS_IS_LIMITED_PROXY(cert_type)) {
        return 1;
    }
    for (i = 0; i < sk_X509_num(chain->certificate_chain); i++) {
        cert = (X509 *)sk_X509_value(chain->certificate_chain, i);
        if (globus_gsi_cert_utils_get_cert_type(cert, &cert_type)
            != GLOBUS_SUCCESS) {
            verror_put_string("globus_gsi_cert_utils_get_cert_type() failed");
            return -1;
        }
        if (GLOBUS_GSI_CERT_UTILS_IS_LIMITED_PROXY(cert_type)) {
            return 1;
        }
    }
    return 0;
}


int
ssl_limited_proxy_file(const char path[])
{
   SSL_CREDENTIALS	*creds = NULL;
   int			return_value = -1;

   creds = ssl_credentials_new();

   if (ssl_certificate_load_from_file(creds, path) != SSL_SUCCESS)
       goto error;

   return_value = ssl_limited_proxy_chain(creds);

   error:
   if (creds) ssl_credentials_destroy(creds);
   return return_value;
}

int
ssl_get_times(const char *path, time_t *not_before, time_t *not_after)
{
   FILE         *cert_file = NULL;
   X509         *cert = NULL;
   char         *tz = NULL;

   assert(path != NULL);

   my_init();
    
   cert_file = fopen(path, "r");
   if (cert_file == NULL) {
      verror_put_string("Failure opening file \"%s\"", path);
      verror_put_errno(errno);
      return -1;
   }

   if (not_before)
       *not_before = 0;
   if (not_after)
       *not_after = 0;

   tz = getenv("TZ");
   setenv("TZ", "", 1);
   tzset();

   while ((cert = PEM_read_X509(cert_file, NULL, PEM_NO_CALLBACK)) != NULL) {
       if (not_before) {
	   time_t new_not_before;
	   globus_gsi_cert_utils_make_time(X509_get_notBefore(cert),
					   &new_not_before);
	   if (*not_before == 0 || *not_before < new_not_before) {
	       *not_before = new_not_before;
	   }
       }
       if (not_after) {
	   time_t new_not_after;
	   globus_gsi_cert_utils_make_time(X509_get_notAfter(cert),
					   &new_not_after);
	   if (*not_after == 0 || *not_after > new_not_after) {
	       *not_after = new_not_after;
	   }
       }
       X509_free(cert);
       cert = NULL;
   }

   if (tz)
       setenv("TZ", tz, 1);
   else
       unsetenv("TZ");
   tzset();

   fclose(cert_file);
   ERR_clear_error();		/* clear EOF error */

   return 0;
}

int
ssl_verify_cred(const char path[])
{
    SSL_CREDENTIALS *ssl_creds = NULL;

    /* Do the certificates check out with OpenSSL? */
	if ((ssl_creds = ssl_credentials_new()) == NULL ||
        ssl_certificate_load_from_file(ssl_creds, path) != SSL_SUCCESS ||
        ssl_verify_gsi_chain(ssl_creds) != SSL_SUCCESS) {
        ssl_credentials_destroy(ssl_creds);
        return -1;
    }
    ssl_credentials_destroy(ssl_creds);
    return 0;
}
