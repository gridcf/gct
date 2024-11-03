/*
 * CA extension implementation file
 *
 */

#include "myproxy_common.h"
#ifndef OPENSSL_NO_ENGINE
#include <openssl/engine.h>
#endif
#include <openssl/ui.h>

#define BUF_SIZE 16384

#ifndef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#endif

#define SECONDS_PER_HOUR (60 * 60)

#if OPENSSL_VERSION_NUMBER < 0x10100000L

#define EVP_PKEY_id(k) (k)->type

#define EVP_PKEY_get0_RSA(k) (k)->pkey.rsa

static
void
RSA_get0_key(const RSA *r, const BIGNUM **n, const BIGNUM **e, const BIGNUM **d)
{
    if (n != NULL)
    {
        *n = r->n;
    }
    if (e != NULL)
    {
        *e = r->e;
    }
    if (d != NULL)
    {
        *d = r->d;
    }
}

#endif

static int 
read_cert_request(GSI_SOCKET *self,
		  unsigned char **buffer,
		  size_t *length) {

  int             return_value = 1;
  unsigned char * input_buffer = NULL;
  size_t          input_buffer_length;

  if (self == NULL) {
    verror_put_string("read_cert_request(): Socket is null");
    goto error;
  }

  if (GSI_SOCKET_read_token(self, &input_buffer,
			    &input_buffer_length) == GSI_SOCKET_ERROR) {
    verror_put_string("read_cert_request(): Read from socket failed");
    goto error;
  }

  *buffer = input_buffer;
  *length = input_buffer_length;

  return_value = 0;

 error:
  if ( return_value ) {
    if ( input_buffer != NULL ) {
      myproxy_debug("freeing buffer");
      free(input_buffer);
      input_buffer = NULL;
    }
  }

  return return_value;

}

static int 
send_certificate(GSI_SOCKET *self,
		 unsigned char *buffer,
		 size_t length) {

  if (GSI_SOCKET_write_buffer(self, (const char *)buffer, 
			      length) == GSI_SOCKET_ERROR) {
    verror_put_string("Error writing certificate to client!");
    return 1;
  }

  return 0;

}

static void 
add_key_value( char * key, char * value, char buffer[] ) {

  strcat( buffer, key );
  strcat( buffer, "=" );
  if ( value == NULL ) {
    strcat( buffer, "NULL" );
  } else {
    strcat( buffer, value );
  }
  strcat( buffer, "\n" );
}


static int 
external_callout( X509_REQ                 *request, 
		  X509                     **cert,
		  myproxy_request_t        *client_request,
		  myproxy_server_context_t *server_context) {

  int return_value = 1;

  char buffer[BUF_SIZE];
  char intbuf[128];

  pid_t pid;
  int fds[3];
  int status;

  FILE * pipestream = NULL;
  X509 * certificate = NULL;

  memset(buffer, '\0', BUF_SIZE);
  memset(intbuf, '\0', 128);

  myproxy_debug("callout using: %s", 
		server_context->certificate_issuer_program);

  if ((pid = myproxy_popen(fds,
			   server_context->certificate_issuer_program,
			   NULL)) < 0) {
    return -1; /* myproxy_popen will set verror */
  }

  /* writing to program */
  pipestream = fdopen( fds[0], "w" );

  if ( pipestream == NULL ) {
    verror_put_string("File stream to stdin pipe creation problem.");
    return 1;
  }

  add_key_value( "username", client_request->username, buffer );
  add_key_value( "passphrase", client_request->passphrase, buffer );

  sprintf( intbuf, "%d", client_request->proxy_lifetime );
  add_key_value( "proxy_lifetime", (char*)intbuf, buffer );
  memset(intbuf, '\0', 128);

  add_key_value( "retrievers", client_request->retrievers, buffer );
  add_key_value( "renewers", client_request->renewers, buffer );
  add_key_value( "credname", client_request->credname, buffer );
  add_key_value( "creddesc", client_request->creddesc, buffer );
  add_key_value( "authzcreds", client_request->authzcreds, buffer );
  add_key_value( "keyretrieve", client_request->keyretrieve, buffer );
  add_key_value( "trusted_retrievers", client_request->trusted_retrievers,
		 buffer );

  sprintf( intbuf, "%d", server_context->max_cert_lifetime );
  add_key_value( "max_cert_lifetime", (char*)intbuf, buffer );
  memset(intbuf, '\0', 128);

  fprintf( pipestream, "%s\n", buffer );

  PEM_write_X509_REQ( pipestream, request );

  fflush( pipestream );

  fclose( pipestream );

  close(fds[0]);

  /* wait for program to exit */

  if( waitpid(pid, &status, 0) == -1 ) {
    verror_put_string("waitpid() failed for external callout child");
    verror_put_errno(errno);
    goto error;
  }

  /* check status and read appropriate content */

  /* if exit != 0 - read and log message from program stderr */

  if ( status != 0 ) {
    verror_put_string("external process exited abnormally\n");
    memset(buffer, '\0', BUF_SIZE);
    if ( read( fds[2], buffer, BUF_SIZE ) > 0 ) {
      verror_put_string("%s", buffer);
    } else {
      verror_put_string("did not receive an error string from callout");
    }
    goto error;
  }

  /* retrieve the certificate */

  pipestream = fdopen( fds[1], "r" );

  if ( pipestream == NULL ) {
    verror_put_string("File stream to stdout pipe creation problem.");
    verror_put_errno(errno);
    goto error;
  }

  certificate = PEM_read_X509( pipestream, NULL, NULL, NULL );

  if (certificate == NULL) {
    verror_put_string("Error reading certificate from external program.");
    ssl_error_to_verror();
    goto error;
  } else {
    myproxy_debug("Received certificate from external callout.");
  }

  fclose( pipestream );

  close(fds[1]);
  close(fds[2]);

  /* good to go */

  *cert = certificate;

  return_value = 0;

 error:

  memset(buffer, '\0', BUF_SIZE);
  memset(intbuf, '\0', 128);

  return return_value;

}

/* Use fcntl() for POSIX file locking. Lock is released when file is closed. */
static int
lock_file(int fd)
{
    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    while( fcntl( fd, F_SETLKW, &fl ) < 0 )
    {
	if ( errno != EINTR )
	{
	    return -1;
	}
    }
    return 0;
}

/*
 * serial number handling liberally borrowed from KCA with the addition
 * of file locking
 */

static int 
assign_serial_number( X509 *cert, 
		      myproxy_server_context_t *server_context ) {

  int retval = 1;
  long serialset;

  BIGNUM       * serial = NULL;
  ASN1_INTEGER * current = NULL, * next = NULL;
  char buf[1024];
  char *serialfile = NULL;

  /* all the io variables */

  BIO   * serialbio = NULL;
  int     fd;
  FILE  * serialstream = NULL;

  myproxy_debug("Assigning serial number");

  serial  = BN_new();
  current = ASN1_INTEGER_new();

  if ( (serial ==NULL) || (current==NULL) ) {
    verror_put_string("Bignum/asn1 INT init failure\n");
    ssl_error_to_verror();
    goto error;
  }

  if (server_context->certificate_serialfile) {
      serialfile = server_context->certificate_serialfile;
  } else {
      const char *sdir;
      sdir = myproxy_get_storage_dir();
      if (sdir == NULL) {
	  goto error;
      }
      serialfile = malloc(strlen(sdir)+strlen("/serial")+1);
      sprintf(serialfile, "%s/serial", sdir);
  }

  /* open(), lock, open stream and create BIO */

  fd = open( serialfile, O_RDWR|O_CREAT, 0600 );

  if ( fd == -1 ) {
    verror_put_string("Call to open() failed on %s\n", serialfile);
    verror_put_errno(errno);
    goto error;
  }

  if ( lock_file(fd) == -1 ) {
    verror_put_string("Failed to get lock on file descriptor\n");
    verror_put_errno(errno);
    goto error;
  }

  serialstream = fdopen( fd, "w+" );

  if ( serialstream == NULL ) {
    verror_put_string("Unable to open file stream\n");
    verror_put_errno(errno);
    goto error;
  }

  /* check if file is empty, and if so, initialize with 1 */
  if (fseek(serialstream, 0L, SEEK_END) < 0) {
    verror_put_string("Unable to seek file stream\n");
    verror_put_errno(errno);
    goto error;
  }

  serialset = ftell(serialstream);
  if (serialset) rewind(serialstream);

  serialbio = BIO_new_fp( serialstream, BIO_CLOSE );

  if ( serialbio == NULL ) {
    verror_put_string("BIO_new_fp failure.\n");
    ssl_error_to_verror();
    goto error;
  }

  if (serialset) {
      if (!a2i_ASN1_INTEGER(serialbio, current, buf, sizeof(buf))) {
	  verror_put_string("Asn1 int read/conversion error\n");
      ssl_error_to_verror();
	  goto error;
      } else {
	  myproxy_debug("Loaded serial number 0x%s from %s", buf, serialfile);
      }
  } else {
      ASN1_INTEGER_set(current, server_context->certificate_serial_skip);
  }

  serial = BN_bin2bn( current->data, current->length, serial );
  if ( serial == NULL ) {
    verror_put_string("Error converting to bignum\n");
    ssl_error_to_verror();
    goto error;
  }

  if (!BN_add_word(serial, server_context->certificate_serial_skip)) {
    verror_put_string("Error incrementing serial number\n");
    ssl_error_to_verror();
    goto error;
  }

  if (!(next = BN_to_ASN1_INTEGER(serial, NULL))) {
    verror_put_string("Error converting new serial to ASN1\n");
    ssl_error_to_verror();
    goto error;
  }

  if (BIO_reset(serialbio) != 0) {
    verror_put_string("Error resetting serialbio\n");
    ssl_error_to_verror();
    goto error;
  }
  i2a_ASN1_INTEGER(serialbio, next);
  BIO_puts(serialbio, "\n");


  /* the call to BIO_free with the CLOSE flags will take care of
   * the underlying file stream and close()ing the file descriptor,
   * which will release the lock.
   */
  
  BIO_free(serialbio);
  serialbio    = NULL;
  serialstream = NULL;

  if (!X509_set_serialNumber(cert, current)) {
    verror_put_string("Error assigning serialnumber\n");
    ssl_error_to_verror();
    goto error;
  }

  myproxy_debug("serial number assigned");

  retval = 0;

 error:
  if (serial)
    BN_free(serial);
  if (current)
    ASN1_INTEGER_free(current);
  if(next)
    ASN1_INTEGER_free(next);
  if(serialbio)
    BIO_free(serialbio);
  if(serialstream)
    serialstream = NULL;


  return(retval);


}

static void
add_ext(X509V3_CTX *ctxp, X509 *cert, int nid, char *value) {
    X509_EXTENSION *ex;
    ex = X509V3_EXT_conf_nid(NULL, ctxp, nid, value);
    X509_add_ext(cert,ex,-1);
    X509_EXTENSION_free(ex);
}

static int
write_certificate(X509 *cert, const char serial[], const char dir[]) {
    BIO *bp=NULL;
    char *path;
    int rval = -1, fd;

    path = malloc(strlen(dir)+strlen(serial)+strlen("/.pem")+1);
    sprintf(path, "%s/%s.pem", dir, serial);
    if ((fd = open(path, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR)) < 0) {
        myproxy_log("failed to create %s: %s", path, strerror(errno));
        goto error;
    }
    close(fd);
	if ((bp=BIO_new(BIO_s_file())) == NULL) {
        myproxy_debug("BIO_new(BIO_s_file()) failed");
        goto error;
    }
    if (BIO_write_filename(bp, path) <= 0) {
        myproxy_debug("BIO_write_filename(%s) failed", path);
        goto error;
    }
    myproxy_debug("writing certificate to %s", path);
    X509_print(bp, cert);
    PEM_write_bio_X509(bp, cert);

    rval = 0;

 error:
    free(path);
	BIO_free_all(bp);

    return rval;
}

static EVP_PKEY  *e_cakey=NULL;
#ifndef OPENSSL_NO_ENGINE
static ENGINE    *engine=NULL;
static int        engine_used=0;
#endif

static int 
generate_certificate( X509_REQ                 *request, 
		      X509                     **certificate,
		      EVP_PKEY                 *pkey,
		      myproxy_request_t        *client_request,
		      myproxy_server_context_t *server_context) { 

  int             return_value = 1;  
  int             not_after;
  int             lockfd = -1;
  int             i;
  char          * userdn = NULL;
  char          * serial = NULL;

  X509           * issuer_cert = NULL;
  X509           * cert = NULL;
  X509_NAME      * subject = NULL;
  EVP_PKEY       * cakey = NULL;
  X509V3_CTX       ctx, *ctxp;

  FILE * inkey = NULL;
  FILE * issuer_cert_file = NULL;

  globus_result_t globus_result;

  myproxy_debug("Generating certificate internally.");

  cert = X509_new();

  ctxp = &ctx;		/* needed for X509V3 macros */
  X509V3_set_ctx_nodb(ctxp);

  if (cert == NULL) {
    verror_put_string("Problem creating new X509.");
    ssl_error_to_verror();
    goto error;
  }

  /* subject info */

  /* this has already been called successfully, but... */

  if ( user_dn_lookup( client_request->username, &userdn,
		       server_context ) ) {
    verror_put_string("unknown username: %s", client_request->username);
    goto error;
  }

  subject = X509_get_subject_name(cert);

  globus_result =
      globus_gsi_cert_utils_get_x509_name(userdn, strlen(userdn), subject);
  if (globus_result != GLOBUS_SUCCESS) {
      verror_put_string("globus_gsi_cert_utils_get_x509_name() failed");
      globus_error_to_verror(globus_result);
      goto error;
  }

  /* Verify that the subject has been correctly encoded and fix any
     problems we find.*/
  for (i = 0; i < X509_NAME_entry_count(subject); i++)
  {
      X509_NAME_ENTRY *ne = NULL;
      ASN1_STRING *str = NULL;
      ASN1_OBJECT *obj = NULL;

      ne = X509_NAME_get_entry(subject, i);
      str = X509_NAME_ENTRY_get_data(ne);
      obj = X509_NAME_ENTRY_get_object(ne);

      if ((OBJ_obj2nid(obj) == NID_domainComponent) &&
          (str->type == V_ASN1_PRINTABLESTRING)) {
          myproxy_debug("Setting DC type to IA5String.");
          str->type = V_ASN1_IA5STRING;
      }
      if ((OBJ_obj2nid(obj) == NID_pkcs9_emailAddress) &&
          (str->type == V_ASN1_PRINTABLESTRING)) {
          myproxy_debug("Setting emailAddress type to IA5String.");
          str->type = V_ASN1_IA5STRING;
      }
  }

  /* issuer info */

  issuer_cert_file = fopen(server_context->certificate_issuer_cert, "r");
  if (issuer_cert_file == NULL) {
      verror_put_string("Error opening certificate file %s",
			server_context->certificate_issuer_cert);
      verror_put_errno(errno);
      goto error;
  }
  
  if ((issuer_cert = PEM_read_X509(issuer_cert_file,
				   NULL, NULL, NULL)) == NULL)
  {
      verror_put_string("Error reading certificate %s",
			server_context->certificate_issuer_cert);
      ssl_error_to_verror();
      fclose(issuer_cert_file);
      goto error;
  }
  fclose(issuer_cert_file);

  myproxy_debug("certificate_issuer_cert: %s",
                server_context->certificate_issuer_cert );

  X509_set_issuer_name(cert, X509_get_subject_name(issuer_cert));

  X509V3_set_ctx(ctxp, issuer_cert, cert, NULL, NULL, 0);

  /* version, ttl, etc */

  X509_set_version(cert, 0x2); /* this is actually version 3 */

  if (assign_serial_number(cert, server_context)) {
    verror_put_string("Error assigning serial number to cert");
    goto error;
  }

  if (!server_context->max_cert_lifetime) {
    not_after = MIN(client_request->proxy_lifetime,
		    SECONDS_PER_HOUR * MYPROXY_DEFAULT_DELEG_HOURS);
  } else {
    not_after = MIN(client_request->proxy_lifetime,
		    server_context->max_cert_lifetime);
  }

  myproxy_debug("cert lifetime: %d", not_after );

  /* allow 5m clock skew */
  X509_gmtime_adj(X509_get_notBefore(cert), -(MYPROXY_DEFAULT_CLOCK_SKEW));
  X509_gmtime_adj(X509_get_notAfter(cert), (long)not_after);
  
  X509_set_pubkey(cert, pkey);

  /* extensions */

  if (server_context->certificate_extfile ||
      server_context->certificate_extapp) {
      CONF *extconf = NULL;
      long errorline = -1;
      extconf = NCONF_new(NULL);
      if (server_context->certificate_extfile) {
	  if (NCONF_load(extconf, server_context->certificate_extfile,
			 &errorline) <= 0) {
	      if (errorline <= 0) {
		  verror_put_string("OpenSSL error loading the certificate_extfile '%s'", server_context->certificate_extfile);
	      } else {
		  verror_put_string("OpenSSL error on line %ld of certificate_extfile '%s'\n", errorline, server_context->certificate_extfile);
	      }
	      goto error;
	  }
	  myproxy_debug("Successfully loaded extensions file %s.",
			server_context->certificate_extfile);
      } else {
	  pid_t childpid;
	  int fds[3];
	  int exit_status;
	  FILE *nconf_stream = NULL;
	  myproxy_debug("calling %s", server_context->certificate_extapp);
	  if ((childpid = myproxy_popen(fds,
					server_context->certificate_extapp,
					client_request->username,
					NULL)) < 0) {
	      return -1; /* myproxy_popen will set verror */
	  }
	  close(fds[0]);
	  if (waitpid(childpid, &exit_status, 0) == -1) {
	      verror_put_string("wait() failed for extapp child");
	      verror_put_errno(errno);
	      return -1;
	  }
	  if (exit_status != 0) {
	      FILE *fp = NULL;
	      char buf[100];
	      verror_put_string("Certificate extension call-out returned non-zero.");
	      fp = fdopen(fds[1], "r");
	      if (fp) {
		  while (fgets(buf, 100, fp) != NULL) {
		      verror_put_string("%s", buf);
		  }
		  fclose(fp);
	      }
	      fp = fdopen(fds[2], "r");
	      if (fp) {
		  while (fgets(buf, 100, fp) != NULL) {
		      verror_put_string("%s", buf);
		  }
		  fclose(fp);
	      }
	      goto error;
	  }
	  close(fds[2]);
	  nconf_stream = fdopen(fds[1], "r");
	  if (NCONF_load_fp(extconf, nconf_stream, &errorline) <= 0) {
	      if (errorline <= 0) {
		  verror_put_string("OpenSSL error parsing output of certificate_extapp call-out.");
	      } else {
		  verror_put_string("OpenSSL error parsing line %ld of of certificate_extapp call-out output.", errorline);
	      }
	      fclose(nconf_stream);
	      goto error;
	  }
	  fclose(nconf_stream);
      }
      X509V3_set_nconf(&ctx, extconf);
      if (!X509V3_EXT_add_nconf(extconf, &ctx, "default", cert))
      {
	  verror_put_string("OpenSSL error adding extensions.");
      ssl_error_to_verror();
	  goto error;
      }
      myproxy_debug("Successfully added extensions.");
  } else {			/* add some defaults */
      add_ext(ctxp, cert, NID_key_usage, "critical,Digital Signature, Key Encipherment, Data Encipherment");
      add_ext(ctxp, cert, NID_ext_key_usage, "clientAuth");
      add_ext(ctxp, cert, NID_basic_constraints, "critical,CA:FALSE");
      add_ext(ctxp, cert, NID_subject_key_identifier, "hash");
  }
  if (server_context->certificate_issuer_email_domain) {
      char *email;
      email = malloc(strlen(client_request->username)+strlen("email:@")+1+
		     strlen(server_context->certificate_issuer_email_domain));
      sprintf(email, "email:%s@%s", client_request->username,
	      server_context->certificate_issuer_email_domain);
      add_ext(ctxp, cert, NID_subject_alt_name, email);
      free(email);
  }

  /* load ca key */

#ifndef OPENSSL_NO_ENGINE
  if (engine) {
      if (server_context->certificate_openssl_engine_lockfile) {
          lockfd = open(server_context->certificate_openssl_engine_lockfile,
                        O_RDWR|O_CREAT, 0600);

          if (lockfd == -1) {
              verror_put_string("Call to open() failed on %s", server_context->certificate_openssl_engine_lockfile);
              verror_put_errno(errno);
              goto error;
          }

          if ( lock_file(lockfd) == -1 ) {
              verror_put_string("Failed to get lock on %s", server_context->certificate_openssl_engine_lockfile);
              verror_put_errno(errno);
              goto error;
          }
      }

      if (!ENGINE_set_default(engine, ENGINE_METHOD_ALL)) {
          verror_put_string("ENGINE_set_default(ENGINE_METHOD_ALL) failed.");
          ssl_error_to_verror();
          goto error;
      }
  }
#endif

  if(e_cakey) {
      cakey = e_cakey;
  } else {
      inkey = fopen( server_context->certificate_issuer_key, "r");

      if (!inkey) {
         verror_put_string("Could not open cakey file handle: %s",
	     	      server_context->certificate_issuer_key);
         verror_put_errno(errno);
         goto error;
      }

      cakey = PEM_read_PrivateKey( inkey, NULL, NULL,
	           (char *)server_context->certificate_issuer_key_passphrase );

      fclose(inkey);
  }

  if ( cakey == NULL ) {
    verror_put_string("Could not load cakey for certificate signing.");
    ssl_error_to_verror();
    goto error;
  } else {
    myproxy_debug("certificate_issuer_key: %s",
                  server_context->certificate_issuer_key );
  }

  if (!X509_check_private_key(issuer_cert,cakey)) {
      verror_put_string("CA certificate and CA private key do not match.");
      ssl_error_to_verror();
      goto error;
  }

  /* sign it */

  myproxy_debug("Signing internally generated certificate.");

  if (!X509_sign(cert, cakey,
                 (const EVP_MD *)server_context->certificate_hashalg ) ) {
    verror_put_string("Certificate/cakey sign failed.");
    ssl_error_to_verror();
    goto error;
  } 
  serial = i2s_ASN1_OCTET_STRING(NULL, X509_get_serialNumber(cert));
#ifndef OPENSSL_NO_ENGINE
  if (engine) {
      engine_used=1;
      if (lockfd != -1) close(lockfd);
      if (!ENGINE_set_default(engine, ENGINE_METHOD_NONE)) {
          verror_put_string("ENGINE_set_default(ENGINE_METHOD_NONE) failed.");
          ssl_error_to_verror();
          goto error;
      }
  }
#endif

  return_value = 0;

  *certificate = cert;

  myproxy_log("Issued certificate for user \"%s\", with DN \"%s\", "
              "lifetime \"%d\", and serial number \"0x%s\"",
              client_request->username, userdn, 
              not_after,
              serial
             );

  if (server_context->certificate_out_dir) {
      write_certificate(cert, serial, server_context->certificate_out_dir);
  }

 error:
  if (return_value) {
    if ( cert != NULL ) {
      X509_free(cert);
    }
  }
  if (cakey && !e_cakey)
    EVP_PKEY_free( cakey );
  if (userdn) {
    free(userdn);
    userdn = NULL;
  }
  if (serial)
    free(serial);
  if (lockfd != -1) close(lockfd);

  return return_value;

}


static int 
arraylen(char **options) {
  char **ptr;
  int c = 0;

  ptr = options;
  while(*ptr++!=NULL) c++;

  return c;
}

#ifndef OPENSSL_NO_ENGINE
void shutdown_openssl_engine(void) {
  if (e_cakey) EVP_PKEY_free( e_cakey );
  if (engine) ENGINE_finish(engine);

  /* there is a bug in OpenSSL 0.9.7d which causes a segmentation fault if I call ENGINE_cleanup() here
   * unless the key has been used.  So we only call it if the key has been used.
  */

  if (engine_used) ENGINE_cleanup();
}
#endif

static int ui_read_fn(UI *ui, UI_STRING *ui_string) {
    switch(UI_get_string_type(ui_string)) {
  	case UIT_PROMPT:
	case UIT_VERIFY:
	    if(UI_get_input_flags(ui_string) & UI_INPUT_FLAG_ECHO) {
		UI_set_result(ui, ui_string, (char *) UI_get0_user_data(ui));
		return 1;
	    } else {
            return 0; /* not supported! */
	    }
	case UIT_BOOLEAN:
	default:
	    return 0; /* not supported! */
    }
}

static int ui_write_fn(UI *ui, UI_STRING *ui_string) {
    switch(UI_get_string_type(ui_string)) {
	case UIT_ERROR:
	    verror_put_string("%s", UI_get0_output_string(ui_string));
	    break;
	case UIT_INFO:
	    myproxy_log("%s", UI_get0_output_string(ui_string));
	    break;
	default:
	    break;
    }
    return 1;
}

#ifndef OPENSSL_NO_ENGINE
int initialise_openssl_engine(myproxy_server_context_t *server_context) {
    ENGINE *e;
    EVP_PKEY *cakey;
    const char *engine_id = server_context->certificate_openssl_engine_id;

    /* first set-up a UI that does not actually prompt.*/
    UI_METHOD *ui_method = UI_create_method("MyProxy-OpenSSL Interface");
    UI_method_set_reader(ui_method, ui_read_fn);
    UI_method_set_writer(ui_method, ui_write_fn);

	SSL_load_error_strings();
    ENGINE_load_builtin_engines();

    myproxy_log("Initialising OpenSSL signing engine '%s'....", engine_id);
    e = ENGINE_by_id(engine_id);
    if(!e) {
        verror_put_string("Could not find engine '%s'.", engine_id);
        ENGINE_cleanup();
        UI_destroy_method(ui_method);
        return 0;
    }
	if(server_context->certificate_openssl_engine_pre) {
	    char **pre_cmds;
	    int pre_num;
        pre_cmds = server_context->certificate_openssl_engine_pre;
	    pre_num = arraylen(pre_cmds);
	    while(pre_num--) {
            char *name, *value=NULL;
            char *n = strchr(pre_cmds[0], ':');
            if(n==NULL) {
                name=pre_cmds[0];
            } else {
                n[0]=0;
                name=pre_cmds[0];
                value=n+1;
            }
         	if(!ENGINE_ctrl_cmd_string(e, name, value, 0)) {
                fprintf(stderr, "Failed pre command (%s - %s:%s)\n",
                        engine_id, name, value ? value : "(NULL)");
                ENGINE_free(e);
                ENGINE_cleanup();
	            UI_destroy_method(ui_method);
                return 0;
         	}
         	pre_cmds++;
	    }
    }
    if(!ENGINE_init(e)) {
	    verror_put_string("Could not initialise engine '%s'.", engine_id);
        ssl_error_to_verror();
        ENGINE_free(e);
        ENGINE_cleanup();
        UI_destroy_method(ui_method);
        return 0;
    }
    /* ENGINE_init() returned a functional reference, so free the structural
     * reference from ENGINE_by_id(). */
    ENGINE_free(e);
    if(server_context->certificate_openssl_engine_post) {
        char **post_cmds;
        int post_num;
        post_cmds=server_context->certificate_openssl_engine_post;
        post_num = arraylen(post_cmds);
        while(post_num--) {
            char *name, *value=NULL;
            char *n;
            n = strchr(post_cmds[0], ':');
            if(n==NULL) {
                name=post_cmds[0];
            } else {
                n[0]=0;
                name=post_cmds[0];
                value=n+1;
            }
            if(!ENGINE_ctrl_cmd_string(e, name, value, 0)) {
                fprintf(stderr, "Failed post command (%s - %s:%s)\n",
                        engine_id, name, value ? value : "(NULL)");
                ENGINE_free(e);
                ENGINE_cleanup();
	            UI_destroy_method(ui_method);
                return 0;
            }
            post_cmds++;
        }
    }

    cakey = ENGINE_load_private_key(e, server_context->certificate_issuer_key, ui_method, (char *)server_context->certificate_issuer_key_passphrase);

  	if (cakey == NULL) {        /* may not be fatal... */
        verror_put_string("WARNING: Could not load ENGINE cakey at %s.",
                          server_context->certificate_issuer_key);
        ssl_error_to_verror();
        myproxy_log_verror();
        verror_clear();
	}

    if(atexit(&shutdown_openssl_engine)!=0) {
        verror_put_string("Could not register shutdown handler for engine '%s'.", engine_id);
	    if (cakey) EVP_PKEY_free( cakey );
        ENGINE_finish(e);
        ENGINE_cleanup();
        UI_destroy_method(ui_method);
        return 0;
	} 

    myproxy_log("Initialised engine '%s' (CAKey=%s)", engine_id, server_context->certificate_issuer_key);

	/* Share with the other functions in this module. */
	e_cakey = cakey; 
	engine  = e;

	UI_destroy_method(ui_method);
	return 1;
}
#endif

static int
do_check(const char *callout, const X509_REQ *req, const X509 *cert)
{
    pid_t pid;
    int fds[3];
    FILE * pipestream = NULL;
    int status;
    char buffer[BUF_SIZE];

    if (!callout) return 0;

    myproxy_debug("calling %s", callout);

    if ((pid = myproxy_popen(fds, callout, NULL)) < 0) {
        return -1; /* myproxy_popen will set verror */
    }

    /* writing to program */
    pipestream = fdopen( fds[0], "w" );
    if ( pipestream == NULL ) {
        verror_put_string("File stream to stdin pipe creation problem.");
        return -1;
    }

    if (req)
        PEM_write_X509_REQ( pipestream, (X509_REQ *)req );
    if (cert)
        PEM_write_X509( pipestream, (X509 *)cert );
    fflush( pipestream );
    fclose( pipestream );
    close(fds[0]);

    /* wait for program to exit */

    if( waitpid(pid, &status, 0) == -1 ) {
        verror_put_string("waitpid() failed for %s", callout);
        verror_put_errno(errno);
        return -1;
    }

    /* check status and read appropriate content */
    /* if exit != 0 - read and log message from program stderr */
    if ( status != 0 ) {
        verror_put_string("%s returned failure", callout);
        memset(buffer, '\0', BUF_SIZE);
        if ( read( fds[2], buffer, BUF_SIZE ) > 0 ) {
            verror_put_string("%s", buffer);
        } else {
            verror_put_string("did not receive an error string from %s",
                              callout);
        }
        return -1;
    }

    close(fds[1]);
    close(fds[2]);

    return 0;
}

static int
check_certreq(const char *callout, const X509_REQ *req)
{
    return do_check(callout, req, NULL);
}

static int
check_newcert(const char *callout, const X509 *cert)
{
    return do_check(callout, NULL, cert);
}

static int 
handle_certificate(unsigned char            *input_buffer,
		   size_t                   input_buffer_length,
		   unsigned char            **output_buffer,
		   int                      *output_buffer_length,
		   myproxy_request_t        *client_request,
		   myproxy_server_context_t *server_context) {

  int           return_value = 1;
  int           verify;
  long          sub_hash;
  unsigned char md[SHA_DIGEST_LENGTH];
  unsigned int  md_len = 0;
  int           keysize;

  BIO      * request_bio  = NULL;
  X509_REQ * req          = NULL;
  EVP_PKEY * pkey         = NULL;
  X509     * cert         = NULL;
  SSL_CREDENTIALS *creds  = NULL;

  myproxy_debug("handle_certificate()");

  /* load proxy request into bio */
  request_bio = BIO_new(BIO_s_mem());
  if (request_bio == NULL) {
    verror_put_string("BIO_new() failed");
    ssl_error_to_verror();
    goto error;
  }

  if (BIO_write(request_bio, input_buffer, input_buffer_length) < 0) {
    verror_put_string("BIO_write() failed");
    ssl_error_to_verror();
    goto error;
  }

  /* feed bio into req structure, extract private key and verify */

  req = d2i_X509_REQ_bio(request_bio, NULL);

  if (req == NULL) {
    verror_put_string("Request load failed");
    ssl_error_to_verror();
    goto error;
  } else {
    myproxy_debug("Cert request loaded.");
  }

  pkey = X509_REQ_get_pubkey(req);

  if (pkey == NULL) {
    verror_put_string("Could not extract public key from request.");
    ssl_error_to_verror();
    goto error;
  } 

  if (EVP_PKEY_id(pkey) != EVP_PKEY_RSA) {
      verror_put_string("Public key in certificate request is not of type RSA.");
      goto error;
  }

  const BIGNUM *e;
  RSA_get0_key(EVP_PKEY_get0_RSA(pkey), NULL, &e, NULL);
  unsigned long exp = BN_get_word(e);
  myproxy_debug("RSA exponent in certificate request is: %lu", exp);
  if (exp < 65537) {
      verror_put_string("RSA public key in certificate request has weak exponent (%lu).", exp);
      verror_put_string("RSA public key exponent must be 65537 or larger.");
      goto error;
  }

  keysize = RSA_size(EVP_PKEY_get0_RSA(pkey))*8;
  myproxy_debug("RSA key in certificate request is %d bits.", keysize);
  if (server_context->min_keylen &&
      keysize < server_context->min_keylen) {
      verror_put_string("RSA public key in certificate request is too small (%d bits).", keysize);
      verror_put_string("RSA public key must be at least %d bits.",
                        server_context->min_keylen);
      goto error;
  }

  verify = X509_REQ_verify(req, pkey);

  if ( verify != 1 ) {
    verror_put_string("Req/key did not verify: %d", verify );
    ssl_error_to_verror();
    goto error;
  } 

  /* convert pkey into string for output to log */
  ASN1_digest((int (*)())i2d_PUBKEY,EVP_sha1(),(char*)pkey,md,&md_len);
  sub_hash = md[0] + (md[1] + (md[2] + (md[3] >> 1) * 256) * 256) * 256; 

  myproxy_log("Got a cert request for user \"%s\", "
              "with pubkey hash \"0x%lx\", and lifetime \"%d\"",
              client_request->username, 
              sub_hash,
              client_request->proxy_lifetime
             );

  if (check_certreq(server_context->certificate_request_checker, req)) {
      goto error;
  }

  /* check to see if the configuration is sound, and call the appropriate
   * cert generation method based on what has been defined.
   * these checks are duplicated in check_config().
   */

  if ( ( server_context->certificate_issuer_program != NULL ) && 
       ( server_context->certificate_issuer_cert != NULL ) ) {
    verror_put_string("CA config error: both issuer and program defined");
    goto error;
  } 

  if ( ( server_context->certificate_issuer_program == NULL ) && 
       ( server_context->certificate_issuer_cert == NULL ) ) {
    verror_put_string("CA config error: neither issuer or program defined");
    goto error;
  }

  if ( ( server_context->certificate_issuer_cert != NULL ) && 
       ( server_context->certificate_issuer_key == NULL ) ) {
    verror_put_string("CA config error: issuer defined but no key defined");
    goto error;
  }

  if ( ( server_context->certificate_issuer_cert != NULL ) && 
       ( server_context->certificate_issuer_key != NULL ) ) {
    myproxy_debug("Using internal openssl/generate_certificate() code");

    if ( generate_certificate( req, &cert, pkey, 
			       client_request, server_context ) ) {
      verror_put_string("Internal cert generation failed");
      goto error;
    }
  } else {
    myproxy_debug("Using external callout interface.");

    if( external_callout( req, &cert, client_request, server_context ) ) {
      verror_put_string("External callout failed.");
      goto error;
    }
  }

  if (cert == NULL) {
    verror_put_string("Cert pointer NULL - unknown generation failure!");
    goto error;
  }

  if (check_newcert(server_context->certificate_issuer_checker, cert)) {
      goto error;
  }

  if ((creds = ssl_credentials_new()) == NULL) {
    verror_put_string("Failed to create creds!");
    goto error;
  }

  /* Load any intermediate/sub-CA certs if configured */
  if (server_context->certificate_issuer_subca_certfile != NULL) {
    if (ssl_certificate_load_from_file(creds,
        server_context->certificate_issuer_subca_certfile) != SSL_SUCCESS) {
      verror_put_string("Failed to load sub-CA certs from file (%s)!",
                   server_context->certificate_issuer_subca_certfile);
      goto error;
    }
    myproxy_log("Also sending sub-CA certificates from file (%s)",
               server_context->certificate_issuer_subca_certfile);
  }

  /* Place our cert on top of any other certs in creds */
  if (ssl_certificate_push(creds, cert) != SSL_SUCCESS) {
    verror_put_string("Error pushing cert onto creds");
    goto error;
  }

  /* Now convert the creds to return buffer */
  if (ssl_creds_to_buffer(creds, output_buffer, output_buffer_length)
                                           == SSL_ERROR) {
    verror_put_string("Falied to write creds to buffer");
    goto error;
  }

  /* We're good to go */

  return_value = 0;

 error:
  if ( request_bio != NULL ) {
    BIO_free(request_bio);
  }
  if ( req != NULL ) {
    X509_REQ_free( req );
  }
  if ( pkey != NULL ) {
    EVP_PKEY_free( pkey );
  }
  if ( creds != NULL ) {
    ssl_credentials_destroy( creds );
  } else if ( cert != NULL ) {
    X509_free( cert );
  }

  return return_value;

}

int is_certificate_authority_configured(myproxy_server_context_t
                                        *context) {
    return (context->certificate_issuer_program ||
            context->certificate_issuer_cert);
}


void get_certificate_authority(myproxy_socket_attrs_t   *server_attrs, 
			       myproxy_creds_t          *creds,
			       myproxy_request_t        *client_request,
			       myproxy_response_t       *response,
			       myproxy_server_context_t *server_context) {

  unsigned char * input_buffer = NULL;
  size_t	  input_buffer_length;
  unsigned char	* output_buffer = NULL;
  int		  output_buffer_length;

  myproxy_debug("Calling CA Extensions");

  response->response_type = MYPROXY_ERROR_RESPONSE;

  verror_clear();

  if ( read_cert_request( server_attrs->gsi_socket, 
			  &input_buffer, &input_buffer_length) ) {
    verror_put_string("Unable to read request from client");
    myproxy_log_verror();
    response->error_string = \
      strdup("Unable to read cert request from client.\n");
    goto error;
  }

  if ( handle_certificate( input_buffer, input_buffer_length,
			   &output_buffer, &output_buffer_length,
			   client_request, server_context ) ) {
    verror_put_string("CA failed to generate certificate");
    response->error_string = strdup("Certificate generation failure.\n");
    myproxy_log_verror();
    goto error;
  }

  if ( send_certificate( server_attrs->gsi_socket,
			 output_buffer, output_buffer_length ) ) {
    myproxy_log_verror();
    myproxy_debug("Failure to send response to client!");
    goto error;
  }

  response->response_type = MYPROXY_OK_RESPONSE;

 error:
  if ( input_buffer != NULL ) {
    GSI_SOCKET_free_token( input_buffer );
  }
  if ( output_buffer != NULL ) {
    ssl_free_buffer( output_buffer );
  }

}

