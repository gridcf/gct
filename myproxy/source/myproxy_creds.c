/*
 * myproxy_creds.c
 *
 * Routines for storing and retrieving credentials.
 *
 * See myproxy_creds.h for documentation.
 */

#include "myproxy_common.h"	/* all needed headers included here */

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

/* Files should only be readable by me */
#define FILE_MODE               0600

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define X509_OBJECT_get0_X509(o) (o)->data.x509
#define X509_OBJECT_new() calloc(1, sizeof(X509_OBJECT))
#define X509_OBJECT_free(o) \
    do { \
        X509_OBJECT *otmp = (o); \
        X509_OBJECT_free_contents(otmp); \
        free(otmp); \
    } while (0)
#endif


/**********************************************************************
 *
 * Internal variables
 *
 */

static char *storage_dir = NULL;
static int searched_for_storage_dir = 0;
static int max_namelen = -1;

/**********************************************************************
 *
 * Internal functions
 *
 */

/*
 * mystrdup()
 *
 * Wrapper around strdup()
 */
static char *
mystrdup(const char *string)
{
    char *dup = NULL;
    
    assert(string != NULL);
    
    dup = strdup(string);
    
    if (dup == NULL)
    {
        verror_put_errno(errno);
    }
    
    return dup;
}


/*
 * file_exists()
 *
 * Check for existance of a file.
 *
 * Returns 1 if exists, 0 if not, -1 on error.
 */
static int
file_exists(const char *path)
{
    struct stat			statbuf = {0}; /* initialize with 0s */
    int				return_value = -1;

    if (path == NULL)
    {
	verror_put_errno(EINVAL);
	return -1;
    }
    
    if (stat(path, &statbuf) == -1)
    {
	switch (errno)
	{
	  case ENOENT:
	  case ENOTDIR:
	    /* File does not exist */
	    return_value = 0;
	    break;
	    
	  default:
	    /* Some error */
	    return_value = -1;
	    break;
	}
    }
    else
    {
	/* File exists */
	return_value = 1;
    }
    
    return return_value;
}


static int
check_storage_directory_safety()
{
    struct safe_id_range_list trusted_uids, trusted_gids;
	struct passwd *pw = NULL;
    int trust_type;

    /* check permissions on full path; make these WARNINGs for now */
    errno = 0;
    safe_init_id_range_list(&trusted_uids);
    safe_init_id_range_list(&trusted_gids);
    safe_add_id_to_list(&trusted_uids, geteuid());
	pw = getpwuid(geteuid());
    trust_type = safe_is_path_trusted_r(storage_dir,
                                        &trusted_uids, &trusted_gids);
    safe_destroy_id_range_list(&trusted_uids);
    safe_destroy_id_range_list(&trusted_gids);
    switch (trust_type) {
    case SAFE_PATH_TRUSTED_CONFIDENTIAL: /* accessible/modifiable only by us */
        break;
    case SAFE_PATH_TRUSTED:
    case SAFE_PATH_TRUSTED_STICKY_DIR:
        myproxy_log("WARNING: safe_is_path_trusted_r: permissions on %s do not provide confidentiality", storage_dir);
        break;
    case SAFE_PATH_UNTRUSTED:
        if (geteuid() == 0) {
            myproxy_log("WARNING: safe_is_path_trusted_r: %s can be modified by users/groups other than uid=0/gid=0", storage_dir, pw->pw_name);
        } else {
            myproxy_log("WARNING: safe_is_path_trusted_r: %s can be modified by users/groups other than %s and uid=0/gid=0", storage_dir, pw->pw_name);
        }
        break;
    case SAFE_PATH_ERROR:
    default:
        myproxy_log("WARNING: safe_is_path_trusted_r: unable to check permissions on %s: %s", storage_dir, strerror(errno));
        break;
    }

    return 0;                   /* just warn for now */
}

static int
locate_storage_directory()
{
    struct stat statbuf = {0}; /* initialize with 0s */
    int return_code = -1;
    char *gl_storage_dir = NULL;

    if (storage_dir == NULL) {
        char *GL;
        searched_for_storage_dir = 1;
        GL = getenv("GLOBUS_LOCATION");
        if (stat("/var/lib/myproxy", &statbuf) == 0) {
            storage_dir = mystrdup("/var/lib/myproxy");
            if (!storage_dir) goto error;
        }
        /* if /var/lib/myproxy doesn't exist, look for /var/myproxy */
        if (storage_dir == NULL && stat("/var/myproxy", &statbuf) == 0) {
            storage_dir = mystrdup("/var/myproxy");
            if (!storage_dir) goto error;
        }
        /* if /var/myproxy doesn't exist, look for $GL/var/myproxy */
        if (storage_dir == NULL && GL != NULL) {
            gl_storage_dir =
                (char *)malloc(strlen(GL)+strlen("/var/myproxy")+1);
            if (!gl_storage_dir) {
                verror_put_errno(errno);
                verror_put_string("malloc() failed");
                goto error;
            }
            sprintf(gl_storage_dir, "%s/var/myproxy", GL);
            if (stat(gl_storage_dir, &statbuf) == 0) {
                storage_dir = gl_storage_dir;
                gl_storage_dir = NULL;
            }
        }
        if (storage_dir == NULL) {
            verror_put_string("did not find a storage directory");
            if (!GL) verror_put_string("GLOBUS_LOCATION not set");
            goto error;
        }
    }

    return 0;

 error:
    if (gl_storage_dir) free(gl_storage_dir);
    return return_code;
}

/*
 * check_storage_directory()
 *
 * Check for existance and permissions on storage directory.
 * Do not create storage directory if it doesn't exist.
 *
 * Returns 0 if ok, -1 on error.
 */
static int
check_storage_directory()
{
    struct stat statbuf = {0}; /* initialize with 0s */
    int return_code = -1;
	struct passwd *pw = NULL;
    static int firsttime = 1;

    if (storage_dir == NULL && !searched_for_storage_dir) {
        locate_storage_directory();
        searched_for_storage_dir = 1;
    }

    if (storage_dir == NULL) {
	    verror_put_errno(ENOENT);
	    goto error;
    }

	if (stat(storage_dir, &statbuf) == -1) {
	    verror_put_errno(errno);
	    verror_put_string("could not stat directory %s", storage_dir);
	    goto error;
	}
    
    if (!S_ISDIR(statbuf.st_mode))
    {
        verror_put_string("%s is not a directory", storage_dir);
        goto error;
    }
    
    /* Make sure it's owned by me */
    if (statbuf.st_uid != geteuid()) {
        pw = getpwuid(geteuid());
        if (pw) {
            verror_put_string("%s not owned by %s",
                              storage_dir, pw->pw_name);
        } else {
            verror_put_string("%s not owned by uid %d",
                              storage_dir, geteuid());
        }
        goto error;
    }
    
    /* Make sure it's not readable or writable by anyone else */
    if ((statbuf.st_mode & S_IRWXG) ||
        (statbuf.st_mode & S_IRWXO)) {
        verror_put_string("permissions on %s must be 0700", storage_dir);
        goto error;
    }

    if (max_namelen == -1) {
        if (getenv("MYPROXY_CREDS_MAX_NAMELEN")) {
            max_namelen = atoi(getenv("MYPROXY_CREDS_MAX_NAMELEN"));
        } else {
            max_namelen = MYPROXY_CREDS_MAX_NAMELEN;
        }
    }

    if (firsttime) { /* just once */
        check_storage_directory_safety();
        myproxy_log("using storage directory %s", storage_dir);
        firsttime = 0;
    }

    /* Success */
    return_code = 0;
    
  error:
    return return_code;
}


static char *
strmd5(const char *s, unsigned char *digest)
{
    MD5_CTX md5;
    unsigned char   d[16];
    int     i;
    char mbuf[33];

    MD5_Init(&md5);
    MD5_Update(&md5,s,strlen(s));
    MD5_Final(d,&md5);

    if (digest) 
       memcpy(digest,d,sizeof(d));
    for (i=0; i<16; i++) {
       int     dd = d[i] & 0x0f;
       mbuf[2*i+1] = dd<10 ? dd+'0' : dd-10+'a';
       dd = d[i] >> 4;
       mbuf[2*i] = dd<10 ? dd+'0' : dd-10+'a';
    }
    mbuf[32] = 0;
    return mystrdup(mbuf);
}
        
    
/*
 * get_storage_locations()
 *
 * Given an user name return the path where the credentials for that
 * username should be stored and the path where data about the credentials
 * should be stored.
 *
 * Return 0 on success, -1 on error.
 */
static int
get_storage_locations(const char *username,
		      const char *credname,
                      char **creds_path,
                      char **data_path,
		      char **lock_path)
{
    int return_code = -1;
    int long_username = 0;
    int long_credname = 0;
    char *sterile_username = NULL;
    char *sterile_credname = NULL;
    const char *creds_suffix = ".creds";
    const char *data_suffix = ".data";
    const char *lock_suffix = ".lock";
    
    assert(username != NULL);
    assert(creds_path != NULL);
    assert(data_path != NULL);
    assert(lock_path != NULL);

    if (check_storage_directory() == -1) {
        goto error;
    }

    if (strlen(username) > max_namelen) {
        long_username = 1;
    }

    if (long_username || strchr(username, '/')) {
       sterile_username = strmd5(username, NULL);

       if (sterile_username == NULL) {
           goto error;
       }

    } else {
       sterile_username = mystrdup(username);

       if (sterile_username == NULL) {
	   goto error;
       }
       
       sterilize_string(sterile_username);
    }

    if (*creds_path) (*creds_path)[0] = '\0';
    if (*data_path) (*data_path)[0] = '\0';
    if (*lock_path) (*lock_path)[0] = '\0';
   
    if (!credname) {
    	if (my_append(creds_path, storage_dir,
		      "/", sterile_username, creds_suffix,
		      NULL) == -1) {
	    verror_put_string("Internal error: creds_path too small: "
			      "%s line %s", __FILE__, __LINE__);
	    goto error;
    	}
    	if (my_append(data_path, storage_dir,
		      "/", sterile_username, data_suffix,
		      NULL) == -1) {
	    verror_put_string("Internal error: data_path too small: "
			      "%s line %s", __FILE__, __LINE__);
	    goto error;
    	}
    	if (my_append(lock_path, storage_dir,
		      "/", sterile_username, lock_suffix,
		      NULL) == -1) {
	    verror_put_string("Internal error: lock_path too small: "
			      "%s line %s", __FILE__, __LINE__);
	    goto error;
    	}

    } else {

        if (strlen(credname) > max_namelen) {
            long_credname = 1;
        }

        if (long_credname || strchr(credname, '/')) {
            sterile_credname = strmd5(credname, NULL);

            if (sterile_credname == NULL) {
                goto error;
            }

        } else {
            sterile_credname = mystrdup(credname);

            if (sterile_credname == NULL) {
                goto error;
            }
       
            sterilize_string(sterile_credname);
        }
    
    	if (my_append(creds_path, storage_dir,
				"/", sterile_username, "-",
				sterile_credname, creds_suffix, NULL) == -1) {
         verror_put_string("Internal error: creds_path too small: %s line %s",
			   __FILE__, __LINE__);
       	 goto error;
    	}
    	if (my_append(data_path, storage_dir,
				"/", sterile_username, "-",
				sterile_credname, data_suffix, NULL) == -1)
    	{
       	 verror_put_string("Internal error: data_path too small: %s line %s",
       	                   __FILE__, __LINE__);
       	 goto error;
    	}
    	if (my_append(lock_path, storage_dir,
				"/", sterile_username, "-",
				sterile_credname, lock_suffix, NULL) == -1)
    	{
       	 verror_put_string("Internal error: lock_path too small: %s line %s",
       	                   __FILE__, __LINE__);
       	 goto error;
    	}
    }

    /* Success */
    return_code = 0;

  error:
    if (sterile_username != NULL) {
        free(sterile_username);
    }
    if (sterile_credname != NULL) {
        free(sterile_credname);
    }
    
    return return_code;
}


/*
 * write_data_file()
 *
 * Write the data in the myproxy_creds structure to the
 * file name given, creating the file with the given mode.
 *
 * Returns 0 on success, -1 on error.
 */
static int
write_data_file(const struct myproxy_creds *creds,
                const char *data_file_path,
                const mode_t data_file_mode)
{

    int data_fd = -1;
    FILE *data_stream = NULL;
    char *tmpfilename = NULL;
    int bufsiz;
    int return_code = -1;

    if (data_file_path == NULL) {
        goto error;
    }

    bufsiz = strlen(data_file_path)+15;
    tmpfilename = malloc(bufsiz);
    snprintf(tmpfilename, bufsiz, "%s.temp.XXXXXX", data_file_path);

    data_fd = mkstemp(tmpfilename);
    if (data_fd == -1)
    {
        verror_put_errno(errno);
        verror_put_string("opening %s for writing", tmpfilename);
        goto error;
    }

    if (data_file_mode != 0600) { /* mkstemp creates file with 0600 */
        fchmod(data_fd, data_file_mode);
    }

    /* Now open as stream for easier IO */
    data_stream = fdopen(data_fd, "w");
    
    if (data_stream == NULL)
    {
        verror_put_errno(errno);
        verror_put_string("reopening storage file %s", data_file_path);
        goto error;
    }

    fprintf (data_stream, "OWNER=%s\n",creds->owner_name);
    fprintf (data_stream, "LIFETIME=%d\n", creds->lifetime);

    if (creds->credname != NULL)
	fprintf (data_stream, "NAME=%s\n", creds->credname);

    if (creds->creddesc != NULL)
	fprintf (data_stream, "DESCRIPTION=%s\n", creds->creddesc);

    if (creds->retrievers != NULL)
	fprintf (data_stream, "RETRIEVERS=%s\n", creds->retrievers);

    if (creds->keyretrieve != NULL)
	fprintf (data_stream, "KEYRETRIEVERS=%s\n", creds->keyretrieve);

    if (creds->trusted_retrievers != NULL)
	fprintf (data_stream, "TRUSTED_RETRIEVERS=%s\n",
		 creds->trusted_retrievers);

    if (creds->renewers != NULL)
	fprintf (data_stream, "RENEWERS=%s\n", creds->renewers);

    if (creds->username != NULL)
    fprintf (data_stream, "USERNAME=%s\n", creds->username);

    fprintf (data_stream, "END_OPTIONS\n");

    fclose(data_stream);
    data_fd = -1;

    if (rename(tmpfilename, data_file_path) < 0) {
        verror_put_string("rename(%s,%s) failed", tmpfilename, data_file_path);
        verror_put_errno(errno);
        goto error;
    }

    /* Success */
    return_code = 0;
    
  error:
    if (data_fd != -1) {
        if (data_stream != NULL) {
            fclose(data_stream); /* this does close(data_fd) */
        } else {
            close(data_fd);
        }
    }
    if (tmpfilename) {
        if (return_code == -1) {
            unlink(tmpfilename);
        }
        free(tmpfilename);
    }
    
    return return_code;
}

/*
 * read_data_file()
 *
 * Read the data contained in the given data file and fills in the
 * given creds structure.
 *
 * Returns 0 on success, -1 on error.
 */
static int
read_data_file(struct myproxy_creds *creds,
               const char *datafile_path)
{
    FILE *data_stream = NULL;
    char *data_stream_mode = "r";
    int done = 0;
    int line_number = 0;
    int return_code = -1;
    char *line_buffer = NULL;
    size_t line_buffer_len = 512;

    assert(creds != NULL);
    assert(datafile_path != NULL);
    
    myproxy_creds_free_contents(creds);	/* initialize creds structure */

    data_stream = fopen(datafile_path, data_stream_mode);
    
    if (data_stream == NULL)
    {
        verror_put_errno(errno);
        verror_put_string("opening %s for reading", datafile_path);
        goto error;
    }

    line_buffer = (char *)malloc(line_buffer_len);
    assert(line_buffer != NULL);

    while (!done)
    {
        char *variable;
        char *value;
        int len;
        
        if (fgets(line_buffer, line_buffer_len, data_stream) == NULL)
        {
            int errno_save = errno;
            
            if (feof(data_stream))
            {
                verror_put_string("unexpected EOF reading %s", datafile_path);
                goto error;
            }
            else
            {
                verror_put_errno(errno_save);
                verror_put_string("reading %s", datafile_path);
                goto error;
            }
            /* Not reached */
        }

        len = strlen(line_buffer);
        while (line_buffer[len-1] != '\n') { /* didn't get a full line */
            char *more;
            line_buffer_len *= 2;
            line_buffer = realloc(line_buffer, line_buffer_len);
            assert(line_buffer != NULL);
            more = line_buffer+len;
            if (fgets(more, line_buffer_len-len, data_stream) == NULL) {
                verror_put_errno(errno);
                verror_put_string("reading %s", datafile_path);
                goto error;
            }
            len = strlen(line_buffer);
        }

        /* Remove terminating newline */
        line_buffer[len-1] = '\0';

        line_number++;
        
        variable = line_buffer;
        
        value = strchr(line_buffer, '=');

        if (value != NULL)
        {
            /* NUL-terminate variable name */
            *value = '\0';

            /* ...and advance value to point at value */
            value++;
        }

        if (strcmp(variable, "END_OPTIONS") == 0) 
        {
            done = 1;
            break;
        }
        
        /* Everything else requires values to be non-NULL */
        if (value == NULL)
        {
            verror_put_string("malformed line: %s line %d",
                              datafile_path, line_number);
            goto error;
        }
        
        if (strcmp(variable, "OWNER") == 0)
        {
            creds->owner_name = mystrdup(value);
            
            if (creds->owner_name == NULL)
            {
                goto error;
            }
            continue;
        }

	/* We no longer store a PASSPHRASE element.
	   Read it in for backwards compatibility only. */
        if (strcmp(variable, "PASSPHRASE") == 0)
        {
            creds->passphrase = mystrdup(value);
            
            if (creds->passphrase == NULL)
            {
                goto error;
            }
            continue;
        }
       
        if (strcmp(variable, "RETRIEVERS") == 0)
        {
            creds->retrievers = mystrdup(value);
            
            if (creds->retrievers == NULL)
            {
                goto error;
            }
            continue;
        }
       
        if (strcmp(variable, "KEYRETRIEVERS") == 0)
        {
            creds->keyretrieve = mystrdup(value);
            
            if (creds->keyretrieve == NULL)
            {
                goto error;
            }
            continue;
        }
        
        if (strcmp(variable, "TRUSTED_RETRIEVERS") == 0)
        {
            creds->trusted_retrievers = mystrdup(value);
            
            if (creds->trusted_retrievers == NULL)
            {
                goto error;
            }
            continue;
        }
        
        if (strcmp(variable, "RENEWERS") == 0)
        {
            creds->renewers = mystrdup(value);
            
            if (creds->renewers == NULL)
            {
                goto error;
            }
            continue;
        }
        
        if (strcmp(variable, "NAME") == 0)
        {
            creds->credname = mystrdup(value);
            
            if (creds->credname == NULL)
            {
                goto error;
            }
            continue;
        }
        
        if (strcmp(variable, "DESCRIPTION") == 0)
        {
            creds->creddesc= mystrdup(value);
            
            if (creds->creddesc == NULL)
            {
                goto error;
            }
            continue;
        }
        
        if (strcmp(variable, "LIFETIME") == 0)
        {
            creds->lifetime = (int) strtol(value, NULL, 10);
            
            continue;
        }
        
        if (strcmp(variable, "USERNAME") == 0)
        {
            creds->username = mystrdup(value);
            
            if (creds->username == NULL)
            {
                goto error;
            }
            continue;
        }
        
        /* Unrecognized varibale */
        verror_put_string("unrecognized line: %s line %d",
                          datafile_path, line_number);
        goto error;
    }

    /* Success */
    return_code = 0;
    
  error:
    if (data_stream != NULL)
    {
        fclose(data_stream);
    }
    if (line_buffer != NULL)
    {
        free(line_buffer);
    }
    
    return return_code;
}

static int
write_lock_file(const char *filename, const char *reason)
{
    int lock_fd = -1;
    FILE *lock_stream = NULL;
    char *tmpfilename = NULL;
    int bufsiz;
    int return_code = -1;

    if (filename == NULL) {
        goto error;
    }

    bufsiz = strlen(filename)+15;
    tmpfilename = malloc(bufsiz);
    snprintf(tmpfilename, bufsiz, "%s.temp.XXXXXX", filename);

    lock_fd = mkstemp(tmpfilename);
    if (lock_fd == -1) {
        verror_put_errno(errno);
        verror_put_string("opening %s for writing", tmpfilename);
        goto error;
    }

    /* Now open as stream for easier IO */
    lock_stream = fdopen(lock_fd, "w");
    if (lock_stream == NULL) {
        verror_put_errno(errno);
        verror_put_string("reopening lock file %s", filename);
        goto error;
    }

    fprintf(lock_stream, "%s", reason);
    fclose(lock_stream);
    lock_stream = NULL;
    lock_fd = -1;

    if (rename(tmpfilename, filename) < 0) {
        verror_put_string("rename(%s,%s) failed", tmpfilename, filename);
        verror_put_errno(errno);
        goto error;
    }

    /* Success */
    return_code = 0;

 error:
    if (lock_stream) {
        fclose(lock_stream);
    } else if (lock_fd >= 0) {
        close(lock_fd);
    }
    if (tmpfilename) {
        if (return_code == -1) {
            unlink(tmpfilename);
        }
        free(tmpfilename);
    }

    return return_code;
}

/*
** Check trusted certificates directory, create if needed.
*/
int
myproxy_check_trusted_certs_dir()
{
    char *path = NULL;
    struct stat statbuf;
    
    path = get_trusted_certs_path();
    
    if (path == NULL)
    {
        goto error;
    }

    myproxy_debug("Trusted cert dir is %s\n", path);
    
    if (stat(path, &statbuf) == -1)
    {
        switch(errno)
        {
          case ENOENT:
            myproxy_debug("%s does not exist. Creating.\n", path);
            if (make_path(path) == -1)
            {
                goto error;
            }
            break;
            
          default:
            verror_put_errno(errno);
            verror_put_string("stat(%s)", path);
            goto error;
        }
    }
    else if (!S_ISDIR(statbuf.st_mode))
    {
        verror_put_string("Trusted certificates directory \"%s\" is not a directory.\n",
        path);
        goto error;
    }

    free(path);
    
    /* Success */
    return 0;
    
  error:
    if (path != NULL)
    {
        free(path);
    }
    return -1;
}
    
/**********************************************************************
 *
 * API routines
 *
 */

int
myproxy_creds_store(const struct myproxy_creds *creds)
{
    char *creds_path = NULL;
    char *data_path = NULL;
    char *lock_path = NULL;
    char *path_prefix = NULL, *path_end = NULL;
    mode_t data_file_mode = FILE_MODE;
    mode_t creds_file_mode = FILE_MODE;
    int return_code = -1;
   
    if ((creds == NULL) ||
        (creds->username == NULL) ||
        (creds->owner_name == NULL) ||
        (creds->location == NULL)) {
        verror_put_errno(EINVAL);
	goto error;
    }

    if (get_storage_locations(creds->username, creds->credname,
                              &creds_path, &data_path, &lock_path) == -1) {
        goto error;
    }

    /* info about credential */
    if (write_data_file(creds, data_path, data_file_mode) == -1) {
	verror_put_string ("Error writing data file");
	goto clean_up;
    }

    /* credential */
    path_prefix = strdup(creds->location);
    path_end = strrchr(path_prefix, '/');
    if (path_end) {
        *path_end = '\0';
    }
    if (strncmp(path_prefix, creds_path, strlen(path_prefix)) == 0) {
        /* If we're in the same directory (and thus on the same
           filesystem), we can do an atomic rename. */
        if (rename(creds->location, creds_path) < 0) {
            verror_put_string("rename(%s,%s) failed", creds->location,
                              creds_path);
            verror_put_errno(errno);
            goto clean_up;
        }
    } else {
        if (copy_file(creds->location, creds_path, creds_file_mode) == -1) {
            verror_put_string ("Error writing credential file");
            goto clean_up;
        }
        ssl_proxy_file_destroy(creds->location);
    }

    /* administrative locks */
    if (creds->lockmsg) {
        if (write_lock_file(lock_path, creds->lockmsg) < 0) {
            verror_put_string("Error writing lockfile");
            goto clean_up;
        }
    } else {
        unlink(lock_path);
    }
	
    /* Success */
    return_code = 0;

clean_up:
    /* XXX */
    /* Remove files on error */
    if (return_code == -1)
    {
        unlink(data_path);
        ssl_proxy_file_destroy(creds_path);
    }
    if (creds_path) free(creds_path);
    if (data_path) free(data_path);
    if (lock_path) free(lock_path);
    if (path_prefix) free(path_prefix);

error:
    return return_code;
}

int
myproxy_creds_retrieve(struct myproxy_creds *creds)
{
    char *creds_path = NULL;
    char *data_path = NULL;
    char *lock_path = NULL;
    char *username = NULL;
    FILE *lockfile = NULL;
    int return_code = -1;
    
    
    if ((creds == NULL) || (creds->username == NULL)) {
        verror_put_errno(EINVAL);
	goto error;
    }

    /* stash username */
    username = mystrdup(creds->username);

    if (get_storage_locations(creds->username, creds->credname,
                              &creds_path, &data_path, &lock_path) == -1) {
	goto error;
    }

    if (read_data_file(creds, data_path) == -1) {
	if (verror_get_errno() == ENOENT) {
	    verror_clear();
	    verror_put_string("Credentials do not exist");
	} else {
	    verror_put_string("Can't read credentials");
	}
	goto error;
    }

    /* read lockmsg in lockfile if it exists */
    if (creds->lockmsg) {
	free(creds->lockmsg);
	creds->lockmsg = NULL;
    }
    if ((lockfile = fopen(lock_path, "r")) != NULL) {
	long len;
	fseek(lockfile, 0, SEEK_END);
	len = ftell(lockfile);
	rewind(lockfile);
	if (len < 0) {
	    verror_put_string("Failed to access %s", lock_path);
	    fclose(lockfile);
	    goto error;
	}
	len++;
	creds->lockmsg = malloc(len);
	fgets(creds->lockmsg, len, lockfile);
	fclose(lockfile);
    }

    /* reset username from stashed value */
    if (creds->username == NULL) {
        creds->username = username;
    } else {
        free(username);
    }
    username = NULL;
    assert(creds->location == NULL);
    creds->location = mystrdup(creds_path);
    ssl_get_times(creds_path, &creds->start_time, &creds->end_time);

    /* Success */
    return_code = 0;

error:
    if (creds_path) free(creds_path);
    if (data_path) free(data_path);
    if (lock_path) free(lock_path);
    if (username) free(username);

    return return_code;
}

/*
 * returns 1 if creds structure matches the query parameters; 0 otherwise
 */
static int
myproxy_creds_match(struct myproxy_creds *creds,
                    char *username, char *owner_name, char *credname,
                    time_t start_time, time_t end_time)
{
    if (username && strcmp(username, creds->username))
        return 0;
    if (owner_name && strcmp(owner_name, creds->owner_name))
        return 0;
    if (credname &&
        ((!creds->credname && credname[0] != '\0') ||
         (creds->credname && strcmp(credname, creds->credname))))
        return 0;
    if ((start_time && start_time > creds->end_time) ||
        (end_time && end_time < creds->end_time))
        return 0;

    return 1;
}

/*
 * We implement the query logic of both myproxy_creds_retrieve_all()
 * and myproxy_admin_retrieve_all() in this function here since
 * querying the repository has gotten sufficiently complex that we
 * don't want it implemented in multiple places. Note that because of
 * the translations we do between username/credname and the actual
 * filename used to store the credentials, we do a brute force scan,
 * calling myproxy_creds_retrieve() for each credentials, relying on
 * that function to set username/credname/etc. correctly for us, again
 * so we have just one function that does the translation. Beware
 * trying to optimize this function, because the handling of usernames
 * containing '/' and '-' characters can cause surprises.
 */
static int 
myproxy_creds_retrieve_all_ex(struct myproxy_creds *creds)
{
    char *username = NULL, *sterile_username = NULL;
    char *credname = NULL, *owner_name = NULL;
    time_t end_time = 0, start_time = 0;
    size_t sterile_username_len = 0;
    struct myproxy_creds *cur_cred = NULL, *new_cred = NULL;
    DIR *dir = NULL;
    struct dirent *de = NULL;
    int return_code = -1, numcreds=0;

    if (check_storage_directory() == -1) {
        goto error;
    }

    if (creds == NULL) {
        verror_put_errno(EINVAL);
        goto error;
    }

    /* stash query values so we can test each credential */
    if (creds->username) {
        username = creds->username;
        creds->username = NULL;
        if (strchr(username, '/')) {
            sterile_username = strmd5(username, NULL);
        } else {
            sterile_username = strdup(username);
        }
        if (sterile_username == NULL) {
            goto error;
        }
        sterilize_string(sterile_username);
        sterile_username_len = strlen(sterile_username);
    }
    if (creds->owner_name) {
        owner_name = creds->owner_name;
        creds->owner_name = NULL;
    }
    if (creds->credname) {
        credname = creds->credname;
        creds->credname = NULL;
    }
    if (creds->start_time) {
        start_time = creds->start_time;
        creds->start_time = 0;
    }
    if (creds->end_time) {
        end_time = creds->end_time;
        creds->end_time = 0;
    }

    /*
     * cur_cred always points to the last valid credential in the list.
     * If cur_cred is NULL, we haven't found any credentials yet.
     * The first cred in the list is the one passed in.  Other creds
     *    in the list are ones we allocated and added.
     */

    new_cred = creds; /* new_cred is what we're filling in */

    /*
     * first add the credential w/o a credname, if one exists, because
     * we always want it to be first on the list.
     */
    if (sterile_username &&
        (!credname || credname[0] == '\0')) { /* only if no credname query */
        assert(new_cred->username == NULL);
        assert(new_cred->credname == NULL);
        new_cred->username = strdup(sterile_username);
        if (myproxy_creds_retrieve(new_cred) == 0) {
            if (myproxy_creds_match(new_cred, username,
                                    owner_name, credname,
                                    start_time, end_time)) {
                cur_cred = new_cred;
                new_cred = malloc(sizeof(struct myproxy_creds));
                memset(new_cred, 0, sizeof(struct myproxy_creds));
                numcreds++;
            }
        } else {
            verror_clear();     /* OK if we don't find creds w/o credname */
        }
    }

    /*
     * next search for credentials with a credname, by scanning the
     * entire directory...
     */
    if ((dir = opendir(storage_dir)) == NULL) {
        verror_put_string("failed to open credential storage directory");
        goto error;
    }
    while ((de = readdir(dir)) != NULL) {
        if (!strncmp(de->d_name+strlen(de->d_name)-5, ".data", 5)) {
            char *cname = NULL, *dot, *dash;

            /* optimization: skip credential right away if username
                             doesn't match */
            if (sterile_username && 
                strncmp(de->d_name, sterile_username, sterile_username_len)) {
                continue;
            }

            dash = strchr (de->d_name, '-');
            dot = strrchr(de->d_name, '.');
            *dot = '\0';
            if (dash) { /*Credential with a name */
                *dash = '\0';
                cname = dash+1;
            }
            if (new_cred->username) free(new_cred->username);
            if (new_cred->credname) free(new_cred->credname);
            new_cred->username = strdup(de->d_name);
            if (cname) {
                new_cred->credname = strdup(cname);
            } else {
                new_cred->credname = NULL;
            }
            if (myproxy_creds_retrieve(new_cred) == 0) {
                if (sterile_username && !new_cred->credname)
                    continue;   /* already handled cred w/o name */
                if (!myproxy_creds_match(new_cred, username,
                                         owner_name, credname,
                                         start_time, end_time)) {
                    continue;
                }
                if (cur_cred) cur_cred->next = new_cred;
                cur_cred = new_cred;
                new_cred = malloc(sizeof(struct myproxy_creds));
                memset(new_cred, 0, sizeof(struct myproxy_creds));
                numcreds++;
            } else {
                verror_put_string("failed to retrieve credentials for "
                                  "username \"%s\", credname \"%s\"",
                                  de->d_name, cname ? cname : "");
                myproxy_log_verror(); /* internal error; should not happen */
                verror_clear();
            }
	    }
	}
    closedir(dir);

    return_code = numcreds;

 error:
    if (username) free(username);
    if (sterile_username) free(sterile_username);
    if (owner_name) free(owner_name);
    if (credname) free(credname);
    if (cur_cred && new_cred) {
        myproxy_creds_free_contents(new_cred);
        free(new_cred);
    }
    return return_code;
}

int myproxy_creds_retrieve_all(struct myproxy_creds *creds)
{
    int return_code = -1;
    char *username = NULL, *credname = NULL, *owner_name = NULL;

    if ((creds == NULL) || (creds->username == NULL) ||
        (creds->owner_name == NULL)) {
        verror_put_errno(EINVAL);
        return -1;
    }

    /* stash query values for error message */
    username = strdup(creds->username);
    owner_name = strdup(creds->owner_name);
    if (creds->credname) {
        credname = strdup(creds->credname);
    }

    return_code = myproxy_creds_retrieve_all_ex(creds);

    if (return_code > 0) {
        return_code = 0;
    } else if (return_code == 0) {
        if (credname) {
            verror_put_string("no credentials found with name %s for user %s, "
                              ", owner \"%s\"",
                              credname, username, owner_name);
        } else {
            verror_put_string("no credentials found for user %s, owner \"%s\"",
                              username, owner_name);
        }
        return_code = -1;
    }

    free(username);
    free(owner_name);
    if (credname) free(credname);

    return return_code;
}

int myproxy_admin_retrieve_all(struct myproxy_creds *creds)
{
    return myproxy_creds_retrieve_all_ex(creds);
}

int
myproxy_creds_exist(const char *username, const char *credname)
{
    char *creds_path = NULL;
    char *data_path = NULL;
    char *lock_path = NULL;
    int rc = -1;

    if (username == NULL)
    {
	verror_put_errno(EINVAL);
	goto done;
    }

    if (get_storage_locations(username, credname,
                              &creds_path, &data_path, &lock_path) == -1) {
	goto done;
    }

    rc = file_exists(creds_path);
    
    switch(rc)
    {
      case 0:
	/* File does not exist */
	goto done;

      case 1:
	/* File exists, keep checking */
	break;
	
      case -1:
	/* Error */
	goto done;

      default:
	/* Should not be here */
	verror_put_string("file_exists(%s) return unknown value (%d)",
			  creds_path, rc);
	rc = -1;
	goto done;
    }

    rc = file_exists(data_path);
    
    switch(rc)
    {
      case 0:
	/* File does not exist */
	goto done;

      case 1:
	/* File exists, keep checking */
	break;
	
      case -1:
	/* Error */
	goto done;

      default:
	/* Should not be here */
	verror_put_string("file_exists(%s) return unknown value (%d)",
			  data_path, rc);
	rc = -1;
	goto done;
    }
    
    /* Everything seems to exist */
    
    /* XXX Should check for expiration? */

 done:
    if (creds_path) free(creds_path);
    if (data_path) free(data_path);
    if (lock_path) free(lock_path);

    return rc;
}

int
myproxy_creds_is_owner(const char		*username, 
			const char 		*credname, 
			const char		*client_name)
{
    char *creds_path = NULL;
    char *data_path = NULL;
    char *lock_path = NULL;
    struct myproxy_creds retrieved_creds = {0}; /* initialize with 0s */
    int return_code = -1;

    assert(username != NULL);
    assert(client_name != NULL);
    
    if (get_storage_locations(username, credname,
                              &creds_path, &data_path, &lock_path) == -1)
    {
        goto error;
    }

    if (read_data_file(&retrieved_creds, data_path) == -1)
    {
        goto error;
    }

    if (strcmp(retrieved_creds.owner_name, client_name) == 0)
    {
	/* Is owner */
	return_code = 1;
    }
    else
    {
	/* Is not owner */
	return_code = 0;
    }
    
  error:
    myproxy_creds_free_contents(&retrieved_creds);
    if (creds_path) free(creds_path);
    if (data_path) free(data_path);
    if (lock_path) free(lock_path);
    
    return return_code;
}

int
myproxy_creds_delete(const struct myproxy_creds *creds)
{
    char *creds_path = NULL;
    char *data_path = NULL;
    char *lock_path = NULL;
    int return_code = -1;
    
    if ((creds == NULL) || (creds->username == NULL)) {
        verror_put_errno(EINVAL);
        return -1;
    }
    
    if (get_storage_locations(creds->username, creds->credname,
                              &creds_path, &data_path, &lock_path) == -1) {
        goto error;
    }

    if (unlink(data_path) == -1) {
	if (errno == ENOENT) {
	    verror_put_string("Credentials do not exist.");
	} else {
	    verror_put_errno(errno);
	    verror_put_string("deleting credentials data file %s: %s",
			      data_path, verror_strerror());
	}
        goto error;
    }

    if (ssl_proxy_file_destroy(creds_path) != SSL_SUCCESS) {
	verror_put_string("deleting credentials file %s", creds_path);
        goto error;
    }
    
    unlink(lock_path);		/* may not exist */

    /* Success */
    return_code = 0;
    
  error:
    if (creds_path) free(creds_path);
    if (data_path) free(data_path);
    if (lock_path) free(lock_path);

    return return_code;
}

int
myproxy_creds_lock(const struct myproxy_creds *creds, const char *reason)
{
    char *creds_path = NULL;
    char *data_path = NULL;
    char *lock_path = NULL;
    int return_code = -1;
    
    if ((creds == NULL) || (creds->username == NULL) || (reason == NULL)) {
        verror_put_errno(EINVAL);
        return -1;
    }
    
    if (get_storage_locations(creds->username, creds->credname,
                              &creds_path, &data_path, &lock_path) == -1) {
        goto error;
    }

    if (write_lock_file(lock_path, reason) < 0) {
        verror_put_string("Error writing lockfile");
        goto error;
    }

    /* Success */
    return_code = 0;
    
  error:
    if (creds_path) free(creds_path);
    if (data_path) free(data_path);
    if (lock_path) free(lock_path);

    return return_code;
}

int
myproxy_creds_unlock(const struct myproxy_creds *creds)
{
    char *creds_path = NULL;
    char *data_path = NULL;
    char *lock_path = NULL;
    int return_code = -1;
    
    if ((creds == NULL) || (creds->username == NULL)) {
        verror_put_errno(EINVAL);
        return -1;
    }
    
    if (get_storage_locations(creds->username, creds->credname,
                              &creds_path, &data_path, &lock_path) == -1) {
        goto error;
    }

    unlink(lock_path);

    /* Success */
    return_code = 0;
    
  error:
    if (creds_path) free(creds_path);
    if (data_path) free(data_path);
    if (lock_path) free(lock_path);
    return return_code;
}

/* Server password change function - called from myproxy_server.
   Checks existing password before changing it */ 
int
myproxy_creds_change_passphrase(const struct myproxy_creds *creds,
				const char *new_passphrase)
{
    char *creds_path = NULL;
    char *data_path = NULL;
    char *lock_path = NULL;
    mode_t data_file_mode = FILE_MODE;
    struct myproxy_creds tmp_creds = {0}; /* initialize with 0s */
    int return_code = -1;
    SSL_CREDENTIALS *ssl_creds = NULL;
    
    if ((creds == NULL) || (creds->username == NULL)) {
	verror_put_errno(EINVAL);
	goto error;
    }
    
    if (get_storage_locations(creds->username, creds->credname,
                              &creds_path, &data_path, &lock_path) == -1) {
        goto error;
    }

    if ((ssl_creds = ssl_credentials_new()) == NULL) {
	goto error;
    }

    if (ssl_proxy_load_from_file(ssl_creds, creds_path, creds->passphrase) !=
	SSL_SUCCESS) {
	goto error;
    }

    if (read_data_file(&tmp_creds, data_path) == -1) {
        goto error;
    }
   
    /* Remove and rewrite with modified password.  Crude but works */ 
    if (unlink(data_path) == -1) {
        verror_put_errno(errno);
        verror_put_string("deleting credentials data file %s: %s", data_path,
                          verror_strerror());
        goto error;
    }
    if (ssl_proxy_file_destroy(creds_path) == SSL_ERROR) {
        verror_put_string("deleting credentials data file %s", creds_path);
        goto error;
    }

    /* overwrite old passphrase with new */
    if (new_passphrase && new_passphrase[0])
	tmp_creds.passphrase = strdup(new_passphrase);

    if (write_data_file(&tmp_creds, data_path, data_file_mode) == -1) {
	verror_put_string ("Error writing data file");
       	goto error;
    }
    if (ssl_proxy_store_to_file(ssl_creds, creds_path, tmp_creds.passphrase) !=
	SSL_SUCCESS) {
	goto error;
    }

    /* Success */
    return_code = 0;
    
  error:
    myproxy_creds_free_contents(&tmp_creds);
    ssl_credentials_destroy(ssl_creds);
    if (creds_path) free(creds_path);
    if (data_path) free(data_path);
    if (lock_path) free(lock_path);

    return return_code;
}

int
myproxy_creds_encrypted(const struct myproxy_creds *creds)
{
    char *creds_path = NULL;
    char *data_path = NULL;
    char *lock_path = NULL;
    int rc = -1;
    
    if ((creds == NULL) || (creds->username == NULL)) {
	verror_put_errno(EINVAL);
	goto error;
    }
    
    if (get_storage_locations(creds->username, creds->credname,
                              &creds_path, &data_path, &lock_path) == -1) {
	goto error;
    }

    rc = ssl_private_key_is_encrypted(creds_path);
 error:
    if (creds_path) free(creds_path);
    if (data_path) free(data_path);
    if (lock_path) free(lock_path);

    return rc;
}

int
myproxy_creds_verify_passphrase(const struct myproxy_creds *creds,
				const char *passphrase)
{
    char *creds_path = NULL;
    char *data_path = NULL;
    char *lock_path = NULL;
    char *tmp = NULL;
    int return_code = -1;
    SSL_CREDENTIALS *ssl_creds = NULL;
    
    if ((creds == NULL) || (creds->username == NULL) ||
	(passphrase == NULL)) {
	verror_put_errno(EINVAL);
	goto error;
    }
    
    if (get_storage_locations(creds->username, creds->credname,
                              &creds_path, &data_path, &lock_path) == -1) {
        goto error;
    }

    /*
     * Verify the passphrase here.
     * If the private key is encrypted, verify the passphrase by attempting
     * to decrypt.
     * Otherwise, if we have a crypted passphrase in the myproxy_creds
     * struct, verify against that (for backwards compatibility).
     */
    if (ssl_private_key_is_encrypted(creds_path) == 1 &&
	(ssl_creds = ssl_credentials_new()) != NULL &&
	ssl_private_key_load_from_file(ssl_creds, creds_path, passphrase,
				       NULL) == SSL_SUCCESS) {
	return_code = 1;
    }
    else if (creds->passphrase &&
	     strlen(passphrase) >= MIN_PASS_PHRASE_LEN &&
	     (tmp = (char *)DES_crypt(passphrase,
		   &creds->owner_name[strlen(creds->owner_name)-3])) != NULL &&
	     strcmp(creds->passphrase, tmp) == 0) {
	return_code = 1;
    }
    else
	return_code = 0;

  error:
    ssl_credentials_destroy(ssl_creds);
    if (creds_path) free(creds_path);
    if (data_path) free(data_path);
    if (lock_path) free(lock_path);

    return return_code;
}

void myproxy_creds_free(struct myproxy_creds *creds)
{
    if (!creds) return;
    if (creds->next) myproxy_creds_free(creds->next);
    myproxy_creds_free_contents(creds);
    free(creds);
}

void myproxy_creds_free_contents(struct myproxy_creds *creds)
{
    if (creds == NULL) return;
    if (creds->username != NULL)	free(creds->username);
    if (creds->passphrase != NULL)	free(creds->passphrase);
    if (creds->owner_name != NULL)	free(creds->owner_name);
    if (creds->location != NULL)	free(creds->location);
    if (creds->retrievers != NULL)	free(creds->retrievers);
    if (creds->keyretrieve != NULL)	free(creds->keyretrieve);
    if (creds->trusted_retrievers != NULL) free(creds->trusted_retrievers);
    if (creds->renewers != NULL)	free(creds->renewers);
    if (creds->credname != NULL)	free(creds->credname);
    if (creds->creddesc != NULL)	free(creds->creddesc);
    if (creds->lockmsg != NULL)     free(creds->lockmsg);
    memset(creds, 0, sizeof(struct myproxy_creds));
}

void myproxy_certs_free(struct myproxy_certs *certs)
{
    if (!certs) return;
    if (certs->filename) free(certs->filename);
    if (certs->contents) free(certs->contents);
    myproxy_certs_free(certs->next);
    free(certs);
}

int myproxy_set_storage_dir(const char *dir)
{
    if (storage_dir) {
	free(storage_dir);
	storage_dir = NULL;
    }
    storage_dir=strdup(dir);
    searched_for_storage_dir = 0;
    if (!storage_dir) {
	verror_put_errno(errno);
	verror_put_string("strdup() failed");
	return -1;
    }
    return 0;
}

int myproxy_check_storage_dir()
{
    return check_storage_directory();
}

const char *myproxy_get_storage_dir()
{
    if (check_storage_directory() < 0) {
	return NULL;
    }
    return storage_dir;
}

int
myproxy_print_cred_info(myproxy_creds_t *creds, FILE *out)
{
    if (!creds) return -1;
    for (; creds; creds = creds->next) {
	time_t time_diff = 0, now = 0;
	float days = 0.0;
	if (creds->owner_name) fprintf(out, "owner: %s\n", creds->owner_name);
	if (creds->username)   fprintf(out, "username: %s\n", creds->username);
        if (creds->credname)   fprintf(out, "  name: %s\n", creds->credname);
	if (creds->creddesc)   fprintf(out, "  description: %s\n",
				       creds->creddesc);
	if (creds->retrievers) fprintf(out, "  retrieval policy: %s\n",
				       creds->retrievers);
	if (creds->renewers)   fprintf(out, "  renewal policy: %s\n",
				       creds->renewers);
	if (creds->keyretrieve) fprintf(out, "  key retrieval policy: %s\n",
				       creds->keyretrieve);
	if (creds->trusted_retrievers)
	    fprintf(out, "  trusted retrieval policy: %s\n",
				       creds->trusted_retrievers);
	if (creds->lockmsg)    fprintf(out, "  locked: %s\n", creds->lockmsg);
	now = time(0);
	if (creds->end_time > now) {
	    time_diff = creds->end_time - now;
	    days = time_diff / 86400.0;
	}
	fprintf(out, "  timeleft: %ld:%02ld:%02ld", 
		(long)(time_diff / 3600),
		(long)(time_diff % 3600) / 60,
		(long)time_diff % 60 );
	if (days > 1.0) {
	    fprintf(out, "  (%.1f days)\n", days);
	} else {
	    fprintf(out, "\n");
	}
    }
    return 0;
}

int
myproxy_check_cert_dir(const char cert_dir[])
{
    DIR *dir = NULL;
    struct dirent *de = NULL;
    char path[MAXPATHLEN];
    struct stat s;

    if ((dir = opendir(cert_dir)) == NULL) {
	verror_put_string("failed to open %s", cert_dir);
	return 0;
    }
    while ((de = readdir(dir)) != NULL) {
	snprintf(path, MAXPATHLEN, "%s/%s", cert_dir, de->d_name);
        if (stat(path, &s) < 0) {
            myproxy_log("stat(%s) failed: %s", path, strerror(errno));
            goto failure;
        }
	if (!S_ISREG(s.st_mode)) { /* only regular files, please */
            continue;
	}
        if (!(s.st_mode & S_IROTH)) { /* must be world-readable */
	    verror_put_string("FAILURE: %s not world readable. ", path);
            goto failure;
        }
    }
    closedir(dir);

    return 1;

 failure:
    if (dir != NULL)
        closedir(dir);
    return 0;
}


myproxy_certs_t *
myproxy_get_certs(const char cert_dir[])
{
    DIR *dir = NULL;
    struct dirent *de = NULL;
    myproxy_certs_t *head=NULL, *curr=NULL;
    char path[MAXPATHLEN];
    struct stat s;

    if ((dir = opendir(cert_dir)) == NULL) {
	verror_put_string("failed to open %s", cert_dir);
	return NULL;
    }
    while ((de = readdir(dir)) != NULL) {
	snprintf(path, MAXPATHLEN, "%s/%s", cert_dir, de->d_name);
    if (stat(path, &s) < 0) {
        myproxy_log("stat(%s) failed: %s", path, strerror(errno));
        goto failure;
    }
	if (!S_ISREG(s.st_mode)) { /* only regular files, please */
        continue;
	}
        if (!(s.st_mode & S_IROTH)) { /* must be world-readable */
	    myproxy_log("WARNING: %s not world readable; skipping it", cert_dir);
            continue;
        }
	if (curr == NULL) {
	    curr = head = (myproxy_certs_t *)malloc(sizeof(myproxy_certs_t));
	} else {
	    curr->next = (myproxy_certs_t *)malloc(sizeof(myproxy_certs_t));
	    curr = curr->next;
	}
	memset(curr, 0, sizeof(myproxy_certs_t));
	curr->filename = strdup(de->d_name);
    curr->size = s.st_size;
	if (buffer_from_file(path, (unsigned char **)&curr->contents,
			     NULL) < 0) {
	    goto failure;
	}
    }
    closedir(dir);

    return head;

 failure:
    if (dir != NULL)
        closedir(dir);
    myproxy_certs_free(head);
    return NULL;
}

/*
** Install a list of files in trusted certificates directory.
*/

#define TRUSTED_INSTALL_LOG     "myproxy-install-log"

int
myproxy_install_trusted_cert_files(myproxy_certs_t *trusted_certs)
{
    myproxy_certs_t *trusted_cert;
    char *file_path = NULL;
    char *tmp_path = NULL;
    int tmp_len;
    FILE *file = NULL;
    char *log_file_name = NULL;
    FILE *log_file = NULL;
    
    if (trusted_certs == NULL)
    {
        return 0;
    }
    
    /* Make writable only by user */
    umask(S_IWGRP|S_IWOTH);
    
    if (myproxy_check_trusted_certs_dir() != 0)
    {
        goto error;
    }

    log_file_name = get_trusted_file_path(TRUSTED_INSTALL_LOG);
    
    if (log_file_name == NULL)
    {
        goto error;
    }

    myproxy_debug("Writing out trusted certificate files. Logging to %s\n",
                  log_file_name);

    log_file = fopen(log_file_name, "w");
    
    if (log_file == NULL)
    {
        verror_put_errno(errno);
        verror_put_string("fopen(%s)", log_file_name);
        goto error;
    }

    for (trusted_cert = trusted_certs;
         trusted_cert != NULL;
         trusted_cert = trusted_cert->next)
    {
    
        /*
        ** Sanity check structure
        */
        if ((trusted_cert == NULL) ||
            (trusted_cert->filename == NULL) ||
            (trusted_cert->contents == NULL))
        {
            myproxy_debug("Malformed trusted_cert ignored.\n");
            continue;
        }

        file_path = get_trusted_file_path(trusted_cert->filename);
        if (file_path == NULL)
        {
            goto error;
        }

        tmp_len = strlen(file_path)+strlen(".tmp")+1;
        tmp_path = malloc(tmp_len);
        snprintf(tmp_path, tmp_len, "%s%s", file_path, ".tmp");

        myproxy_debug("Creating trusted cert file: %s\n", file_path);
        
        file = fopen(tmp_path, "w");
        if (file == NULL)
        {
            verror_put_errno(errno);
            verror_put_string("Error opening \"%s\"", tmp_path);
            goto error;
        }
        if (fwrite(trusted_cert->contents, trusted_cert->size, 1, file) != 1) {
            verror_put_errno(errno);
            verror_put_string("Unable to write to %s", tmp_path);
            fclose(file);
            file = NULL;
            goto error;
        }
        fclose(file);
        file = NULL;
        if (rename(tmp_path, file_path) < 0) {
            verror_put_errno(errno);
            verror_put_string("Unable to rename %s to %s",
                              tmp_path, file_path);
            goto error;
        }
        fprintf(log_file, "%lld: %s\n", (long long) time(NULL), file_path);
        free(file_path);
        file_path = NULL;
        free(tmp_path);
        tmp_path = NULL;
    }        

    free(log_file_name);
    fclose(log_file);
    
    myproxy_debug("Trusted cert file writing complete.\n");
    
    return 0;

  error:
    if (log_file_name != NULL)
    {
        free(log_file_name);
    }
    if (log_file != NULL)
    {
        fclose(log_file);
    }
    if (file != NULL)
    {
        fclose(file);
    }
    if (file_path != NULL)
    {
        free(file_path);
    }
    if (tmp_path != NULL)
    {
        free(tmp_path);
    }
    return -1;
}

int myproxy_creds_verify(const struct myproxy_creds *creds)
{
    char *creds_path = NULL;
    char *data_path = NULL;
    char *lock_path = NULL;
    int return_code = -1;

    if (!creds || !creds->username) {
        verror_put_errno(EINVAL);
        goto error;
    }

    if (get_storage_locations(creds->username, creds->credname,
                              &creds_path, &data_path, &lock_path) == -1) {
        goto error;
    }

    /* Do the certificates check out with OpenSSL? */
    if (ssl_verify_cred(creds_path) < 0) {
        goto error;
    }

    /* Success */
    return_code = 0;

  error:
    if (creds_path) free(creds_path);
    if (data_path) free(data_path);
    if (lock_path) free(lock_path);

    return return_code;
}

#define UNLINK_CRL(path)                                               \
    if (unlink(path) == 0) {                                           \
        myproxy_log("removed bad CRL file at %s", path);               \
        return_value = 1;                                              \
    } else {                                                           \
        myproxy_log("failed to unlink %s: %s", path, strerror(errno)); \
    }                                                                  \
    continue;


int
myproxy_clean_crls()
{
    char *cert_dir = NULL;
    DIR *dir = NULL;
    struct dirent *de = NULL;
    int return_value = -1;
	X509_STORE *store = NULL;
	X509_STORE_CTX *ctx = NULL;
	X509_LOOKUP *lookup = NULL;
	X509_OBJECT *xobj = NULL;
	X509_CRL *x = NULL;
	EVP_PKEY *pkey = NULL;
    BIO *in = NULL;
    char path[MAXPATHLEN];
    int ok = 0;

    cert_dir = get_trusted_certs_path();

    if (cert_dir == NULL) {
        goto error;
    }

    myproxy_debug("Trusted cert dir is %s\n", cert_dir);
    
    if ((dir = opendir(cert_dir)) == NULL) {
        verror_put_string("failed to open trusted cert dir");
        verror_put_errno(errno);
        goto error;
    }

    store = X509_STORE_new();
    lookup=X509_STORE_add_lookup(store,X509_LOOKUP_hash_dir());
    if (lookup == NULL) {
        verror_put_string("X509_STORE_add_lookup() failed");
        ssl_error_to_verror();
        goto error;
    }
    if (!X509_LOOKUP_add_dir(lookup,cert_dir,X509_FILETYPE_PEM)) {
        verror_put_string("X509_LOOKUP_add_dir() failed");
        ssl_error_to_verror();
        goto error;
    }
    ERR_clear_error();

    ctx = X509_STORE_CTX_new();
    if (!ctx) {
        verror_put_string("X509_STORE_CTX_new() failed");
        ssl_error_to_verror();
        goto error;
    }

    if(!X509_STORE_CTX_init(ctx, store, NULL, NULL)) {
        verror_put_string("X509_STORE_CTX_init() failed");
        ssl_error_to_verror();
        goto error;
    }

    while ((de = readdir(dir)) != NULL) {
        if (!strstr(de->d_name, ".r")) {
            continue;
        }
        snprintf(path, MAXPATHLEN, "%s%s", cert_dir, de->d_name);
        if (in) BIO_free_all(in);
        in = BIO_new(BIO_s_file());
        if (BIO_read_filename(in, path) <= 0) {
            myproxy_log("can't read %s", path);
            UNLINK_CRL(path);
        }
        if (x) X509_CRL_free(x);
		x=PEM_read_bio_X509_CRL(in, NULL, NULL, NULL);
        if (!x) {
            myproxy_log("can't parse CRL at %s", path);
            UNLINK_CRL(path);
        }
        BIO_free_all(in);
        in = NULL;
        xobj = X509_OBJECT_new();
        ok = X509_STORE_get_by_subject(ctx, X509_LU_X509,
                                       X509_CRL_get_issuer(x), xobj);
        if(ok <= 0) {
            myproxy_log("CRL issuer certificate not found for %s", path);
            UNLINK_CRL(path);
        }
        if (pkey) EVP_PKEY_free(pkey);
        pkey = X509_get_pubkey(X509_OBJECT_get0_X509(xobj));
        X509_OBJECT_free(xobj);
        if(!pkey) {
            myproxy_log("unable to get CRL issuer public key for %s", path);
            UNLINK_CRL(path);
        }
        ok = X509_CRL_verify(x, pkey);
        EVP_PKEY_free(pkey);
        pkey = NULL;
        if (!ok) {
            myproxy_log("bad CRL signature: %s", path);
            UNLINK_CRL(path);
        }
        ok = X509_cmp_time(X509_CRL_get_lastUpdate(x), NULL);
        if (ok == 0) {
            myproxy_log("bad CRL last update field: %s", path);
            UNLINK_CRL(path);
        }
        if (ok > 0) {
            myproxy_log("CRL not yet valid: %s", path);
            UNLINK_CRL(path);
        }
        if (X509_CRL_get_nextUpdate(x)) {
            ok=X509_cmp_time(X509_CRL_get_nextUpdate(x), NULL);
            if (ok == 0) {
                myproxy_log("BAD CRL next update field: %s", path);
                UNLINK_CRL(path);
            }
            if (ok < 0) {
                myproxy_log("CRL has expired: %s", path);
                UNLINK_CRL(path);
            }
        }
        X509_CRL_free(x);
        x = NULL;
    }
        
    if (return_value < 0) return_value = 0;

 error:
    if (cert_dir) free(cert_dir);
    if (dir) closedir(dir);
    if (pkey) EVP_PKEY_free(pkey);
    if (x) X509_CRL_free(x);
    if (in) BIO_free_all(in);
    if (store) {
        X509_STORE_CTX_free(ctx);
        X509_STORE_free(store);
    }

    return return_value;
}

char *
myproxy_creds_path_template()
{
    if (storage_dir) {
        char *path;
        
        path = malloc(strlen(storage_dir)+12);
        sprintf(path, "%s/tmp.XXXXXX", storage_dir);
        return path;
    }

    return strdup("/tmp/myproxy.XXXXXX");
}
